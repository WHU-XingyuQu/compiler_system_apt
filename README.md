# ToyC 编译器前端

`compiler -token` 执行词法分析。`compiler -ast` 复用同一个 Lexer，构造抽象语法树（AST），再检查名称、类型和控制流。

## 构建与运行

在项目根目录的 Linux 或 WSL 环境中执行。需要 CMake 3.16 或更新版本，以及支持 C++17 的编译器。将下面的贯穿示例保存为 `input.c` 即可运行。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/compiler -token < input.c
./build/compiler -ast < input.c
```

```c
int main() {
    int x = 2 + 3 * 4;
    return x;
}
```

## 源码结构

| 阅读顺序 | 文件 | 接收和交出的数据 |
| --- | --- | --- |
| 1 | [main.cpp](src/main.cpp#L62) | 从标准输入读取源码，调用 Lexer，按命令行参数选择打印 token 或检查程序 |
| 2 | [token.hpp](src/token.hpp) · [lexer.hpp](src/lexer.hpp) · [lexer.cpp](src/lexer.cpp#L56) | 把源字符切成带类别、原文和位置的 token，并收集词法诊断 |
| 3 | [parser.hpp](src/parser.hpp) · [parser.cpp](src/parser.cpp#L22) · [ast.hpp](src/ast.hpp) | 从 token 构造函数、语句和表达式的 AST，并收集语法诊断 |
| 4 | [semantic.hpp](src/semantic.hpp) · [semantic.cpp](src/semantic.cpp#L50) | 遍历 AST，检查声明、作用域、类型和控制流，返回语义诊断 |
| 构建与验收 | [CMakeLists.txt](CMakeLists.txt) · [check_frontend.py](tools/check_frontend.py) | 构建 `compiler`，用课程样例 ZIP 对照预期输出 |

## 1. 入口读取源码并分发

`main()` 持有完整的 `source`。一个 token 的 `text` 是一段 `std::string_view`，指向这份源字符串。Parser 也可以通过 token 引用原文，因为整个处理过程都发生在 `source` 的生命周期内。[入口源码](src/main.cpp#L69)

```cpp
const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
const auto lexed = toyc::lex(source);
if (mode == "-token") printTokens(lexed);
else checkProgram(lexed);
```

## 2. 词法分析把字符切成 token

`Token::kind` 供 Parser 选择文法分支，`text` 供输出模块保留源码拼写，`pos` 用于定位诊断。行号从 `0` 开始。Lexer 统一在 `advance()` 中更新位置，Windows 的 CRLF 算一个换行。[数据结构](src/token.hpp#L9) · [位置更新](src/lexer.cpp#L74)

```cpp
struct Token {
    Kind kind;
    std::string_view text; // A slice of the input owned by the driver.
    SourcePos pos;
};
```

`scan()` 先记录扫描起点 `start`。如果当前字符是数字，就读完一段连续数字，再由 `emit()` 截取 `[start.offset, pos_.offset)`。因此 `004` 会按原文保留。`-` 单独成为 token，后面由 Parser 根据位置区分取负和减法。[数字扫描](src/lexer.cpp#L123) · [生成 token](src/lexer.cpp#L85)

```cpp
if (digit(current())) {
    do { advance(); } while (digit(current()));
    emit(Kind::Integer, start);
    return;
}
```

标识符分支先读完整个词再查关键字表，`int` 和 `integer` 因而分别得到关键字和普通标识符。符号表把 `>=` 和 `==` 等双字符符号排在单字符符号前。注释分支在匹配 `/` 之前运行，跳过注释内容时仍会更新位置。[关键字识别](src/lexer.cpp#L111) · [符号表](src/lexer.cpp#L17) · [注释处理](src/lexer.cpp#L95)

`-token` 对示例中的声明输出如下。左列是 token 的序号。[输出实现](src/main.cpp#L21)

```text
5:'int':"int"
6:Ident:"x"
7:'=':"="
8:IntConst:"2"
9:'+':"+"
10:IntConst:"3"
11:'*':"*"
12:IntConst:"4"
13:';':";"
```

把 `3` 改成 `$3` 后，`scan()` 在所有正常规则匹配失败时记录 `$` 的诊断，并推进游标。扫描下一轮从 `3` 开始，后面的 token 仍然完整。有效的非 ASCII UTF-8 字符报告一次，损坏的字节序列逐字节恢复。[非法字符处理](src/lexer.cpp#L136)

```cpp
result_.diagnostics.push_back({start, detail});
pos_.offset += width;
pos_.column += width;
```

扫描完毕后，Lexer 追加内部的 `End` 标记，供 Parser 前瞻。`printTokens()` 先打印 `reject`、错误行 `1 illegal character '$'` 和一个空行，然后打印恢复后的 token 序列。打印循环在 `End` 处停止。[扫描循环](src/lexer.cpp#L60) · [诊断与 token 输出](src/main.cpp#L21)

## 3. 语法分析从 token 构造 AST

### 函数和语句

`parseFunction()` 读入 `int main()`，再调用 `parseBlock()` 处理花括号内的语句。`parseStatement()` 看到 `int` 就进入声明分支，看到 `return` 就进入返回分支。标识符开头的语句需要再看下一个 token，只有 `Ident '='` 才进入赋值分支。`parsePrimary()` 在表达式中用标识符之后的 `(` 区分函数调用和变量引用。[函数与语句块](src/parser.cpp#L113) · [语句分流](src/parser.cpp#L147) · [基本表达式](src/parser.cpp#L257)

声明分支读取名字 `x`。有 `=` 时，`parseOr()` 从最低优先级开始解析初值，返回后由声明分支消费分号。`Decl` 节点持有变量名、常量标记和初值子树。[声明解析](src/parser.cpp#L183)

```cpp
const Token name = require(Kind::Ident, "expected variable name");
ExprPtr initializer;
if (match(Kind::Assign)) initializer = parseOr();
else if (isConst) report("expected '=' and an initializer");
expect(Kind::Semicolon, "expected ';'");
return stmt(first, Decl{name, isConst, std::move(initializer)});
```

### 表达式优先级和左结合

表达式从低到高经过 `||`、`&&`、关系与相等、加减、乘除和取模、一元运算与基本表达式。ToyC 的关系和相等运算位于同一层。每一层先让更高优先级的函数读操作数，再消费属于当前层的运算符。加减层和乘除层共用同一个组树函数。[优先级定义](src/parser.cpp#L237) · [左结合实现](src/parser.cpp#L227)

```cpp
ExprPtr parseAdd() { return parseLeftAssociative(&Parser::parseMultiply, {Kind::Plus, Kind::Minus}); }
ExprPtr parseMultiply() {
    return parseLeftAssociative(&Parser::parseUnary, {Kind::Star, Kind::Slash, Kind::Percent});
}
```

```cpp
auto left = (this->*next)();
while (std::find(operators.begin(), operators.end(), peek().kind) != operators.end()) {
    const Token op = take();
    auto right = (this->*next)();
    left = expr(op, Binary{std::move(left), std::move(right)});
}
return left;
```

对 `2 + 3 * 4`，`parseAdd()` 第一次调用 `parseMultiply()` 得到 `2`。消费 `+` 后，它第二次调用 `parseMultiply()`，得到整棵 `3 * 4` 子树，再把两边接到 `+` 节点上。对 `10 - 3 - 2`，第一轮更新 `left` 后，第二轮以 `10 - 3` 为左子树，得到 `(10 - 3) - 2`。循环对应文法 `AddExpr → MulExpr { ('+' | '-') MulExpr }`，在读完首个操作数后才进入循环。

示例在内存里的树形如下。`std::variant` 表示节点类型，`std::unique_ptr` 让父节点拥有子树。括号的分组作用通过树形保存。[AST 定义](src/ast.hpp#L11)

```text
Function(main, int)
└─ Block
   ├─ Decl(x)
   │  └─ Binary(+)
   │     ├─ Number(2)
   │     └─ Binary(*)
   │        ├─ Number(3)
   │        └─ Number(4)
   └─ Return
      └─ Name(x)
```

### 错误恢复与 s05

删掉声明行末的分号后，当前 token 会是下一行的 `return`。下面的 `expect()` 记录当前位置 `2`，失败时保留 `return`，因此下一轮还可以解析返回语句。遇到缺少操作数等错误时，`parseBlock()` 捕获 `ParseError`，用 `syncStatement()` 跳到分号、块边界或可识别的后续语句，再继续读取。[缺失分隔符](src/parser.cpp#L68) · [语句同步](src/parser.cpp#L103) · [语句块恢复](src/parser.cpp#L131)

```cpp
bool expect(Kind kind, const char* message) {
    if (match(kind)) return true;
    report(message);
    return false;
}
```

`s05_while_without_condition` 的第三行是 `while x = x + 1;`。这里没有 `(`，而 `Ident '='` 可以开始赋值语句。`parseControl()` 据此报告一次缺少整个括号条件，留下 `x = x + 1;` 供后面的 `parseStatement()` 读取。恢复路径跳过了条件解析，因而省去读取部分条件后在 `=` 处产生的第二条诊断。[控制语句恢复](src/parser.cpp#L194)

```cpp
const bool opened = match(Kind::LParen);
const bool bodyWithoutCondition = !opened && startsStatement() &&
                                   (!startsExpression() || startsAssignment());
if (bodyWithoutCondition) {
    // The next tokens can start a body, but cannot be a condition.
    // Leave them for parseStatement instead of inventing a second error.
    report("expected a parenthesized condition");
} else {
    if (!opened) report("expected '('");
    condition = parseOr();
    closed = expect(Kind::RParen, "expected ')'");
}
```

当前版本输出 `reject` 后跟一行 `2`，希冀接受这个结果。课程 ZIP 的答案是 `reject` 后跟两行 `2`，所以原样例比对会在这一例显示差异并返回非零退出码。语法诊断仍保留同一行上的独立错误。

## 4. 语义分析沿 AST 检查名字、类型和控制流

### 声明和作用域

`Checker` 用 `std::visit` 按 AST 的节点类型调用对应的检查函数。进入嵌套块时压入一层作用域，离开时弹出。形参和函数体最外层的变量共享一层。声明只在当前层检查重名，查找则由内向外。[函数登记与作用域](src/semantic.cpp#L76) · [按节点分派](src/semantic.cpp#L115)

```cpp
auto* variable = declare(node.name, node.isConst);
if (node.initializer) {
    const auto initial = requireInt(*node.initializer, reachable);
    if (variable && variable->isConst) variable->constant = initial.constant;
}
return next;
```

访问 `Decl(x)` 时，先登记 `x`，再检查初值。`2 + 3 * 4` 可以求得常量值 `14`，只有 `const` 变量才保存该常量属性。稍后读取 `Return(Name(x))` 时，内层优先的查找规则能定位 `x`。[声明检查](src/semantic.cpp#L137) · [变量引用](src/semantic.cpp#L195)

```cpp
for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(name);
    if (found != scope->end()) return &found->second;
}
return nullptr;
```

函数签名单独保存返回类型和参数个数。检查器按源码顺序登记每个函数，并在检查该函数体前先登记它自己的签名，因此自递归可用，提前调用后续函数会被拒绝。`getint` 和 `putint` 的签名在检查程序前预置。[程序检查](src/semantic.cpp#L52) · [函数与调用](src/semantic.cpp#L201)

### 表达式类型和短路

`ExprInfo` 保存类型和可选的 32 位常量值。把示例的初值换成 `putint(1)`，Parser 仍会构造 `Call` 节点。语义分析查出 `putint` 返回 `Void`，`requireInt()` 随即报告源码行号 `1`。返回的 `Error` 向外层表达式传递已经发生的错误。[表达式信息](src/semantic.cpp#L13) · [整数类型检查](src/semantic.cpp#L183)

```cpp
const auto info = check(expr, reachable);
if (info.type != Type::Void) return info;
report(expr.token.pos, "void value used where an int is needed");
return {Type::Error};
```

逻辑运算的右侧始终检查名字和类型。若左侧常量足以确定结果，右侧会被标记为不可达。`0 && (1 / 0)` 因短路通过，`0 && missing` 仍会报告未声明的名字。[二元运算检查](src/semantic.cpp#L233)

### 返回路径

语句可能有四种出口。`next` 表示继续执行下一条语句，其余三种是 `returned`、`broke` 和 `continued`。一个 `if` 可以有多个出口，例如 `if (x > 0) return x;` 的两个分支合起来得到 `returned | next`。[出口和函数检查](src/semantic.cpp#L46) · [条件分支](src/semantic.cpp#L145)

```cpp
Exits exits = next;
for (const auto& statement : block.statements) {
    const Exits current = check(*statement, reachable && (exits & next));
    // Unreachable statements are checked, but cannot add exits to this block.
    if (exits & next) exits = (exits & ~next) | current;
}
return exits;
```

`exits & ~next` 保留之前已经返回、跳出循环或继续下一轮的路径。`| current` 加入从下一条语句产生的新路径。贯穿示例先执行声明，得到 `next`，再访问 `return x;`，得到 `returned`，所以 `main` 的结束大括号不可达。若把最后一条改成上面的条件返回，检查器仍保留假分支的 `next`，于是报告结束大括号所在的行号 `3`。循环在本层处理自己的 `break` 和 `continue`，循环外使用它们会报错。[语句块出口合并](src/semantic.cpp#L105) · [循环出口](src/semantic.cpp#L152)

## 5. 输出最终判定

`checkProgram()` 在词法诊断为空时调用 Parser。只有语法诊断为空才调用语义检查，因此恢复后的部分 AST 留在语法阶段。最终按源位置排序，`-ast` 成功时只打印 `accept`，失败时打印 `reject` 和行号。语法诊断允许同一行出现多次，语义诊断每行保留一条。[检查顺序与输出](src/main.cpp#L36)

```cpp
if (!lexed.diagnostics.empty()) {
    printDiagnostics(lexed.diagnostics, false);
    return;
}
const auto parsed = toyc::parse(lexed.tokens);
auto errors = parsed.diagnostics.empty()
    ? toyc::checkSemantics(parsed.program, lexed.tokens.back().pos) : parsed.diagnostics;
if (errors.empty()) {
    std::cout << "accept\n";
    return;
}
```

## 评测记录

本地使用课程样例 `task_target/cases20261110.zip`。脚本直接读取 ZIP，对照词法 token 和错误行号序列，以及语法与语义的判定和错误行号序列。[本地验收脚本](tools/check_frontend.py)

```sh
python3 tools/check_frontend.py ./build/compiler task_target/cases20261110.zip
```

| 本地验证（WSL Ubuntu，GCC 13.3.0，CMake 3.28.3） | 结果 |
| --- | --- |
| 词法分析 | 30/30 |
| 语法与语义分析 | 24/25，唯一差异为上文的 `s05_while_without_condition` |

样例包 SHA256 为 `1c2b62f5669ca6afebdff16fa03c72bb06c1af1125d34c3827df2e20aa5d9ea3`。

| 希冀提交时间（2026-11-15） | 题目 | 提交版本 | 结果 |
| --- | --- | --- | --- |
| 08:51:11 | 语法与语义分析 | `8a3b43f` | 24/25，96 分，`s05` 为错误输出 |
| 08:52:54 | 词法分析 | `8a3b43f` | 30/30，排行榜 100 分 |
| 09:05:34 | 语法与语义分析 | `3d9790e` | 希冀连接 GitHub 443 端口超时，尚未执行测试。原 ZIP 本地为 25/25 |
| 09:10:15 | 语法与语义分析 | `ec29fa8` | 同一无令牌地址成功拉取，24/25，96 分。只省略诊断文字时 `s05` 仍失分 |
| 09:12:34 | 语法与语义分析 | `088a3b7` | 25/25，`s05` 得 4 分，排行榜 100 分 |
