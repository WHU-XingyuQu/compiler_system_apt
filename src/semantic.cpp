#include "semantic.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace toyc {
namespace {

enum class Type { Int, Void, Error };
using Constant = std::optional<std::int32_t>;

struct ExprInfo {
    Type type;
    Constant constant = std::nullopt;
};

struct Variable {
    bool isConst;
    Constant constant = std::nullopt;
};

struct Signature {
    Type result;
    std::size_t arity;
};

constexpr auto intMin = std::numeric_limits<std::int32_t>::min();
constexpr auto intMax = std::numeric_limits<std::int32_t>::max();

Constant intValue(std::int64_t value) {
    if (value < intMin || value > intMax) return std::nullopt;
    return static_cast<std::int32_t>(value);
}

std::optional<std::int64_t> magnitude(std::string_view digits) {
    std::int64_t value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) return std::nullopt;
    return value;
}

// Possible ways to leave a statement. A loop handles its own break/continue.
using Exits = unsigned;
constexpr Exits next = 1, returned = 2, broke = 4, continued = 8;

class Checker {
public:
    std::vector<Diagnostic> run(const Program& program, SourcePos eof) {
        functions_.emplace("getint", Signature{Type::Int, 0});
        functions_.emplace("putint", Signature{Type::Void, 1});
        bool hasMain = false;
        for (const auto& function : program.functions) {
            if (function.name.text == "main") {
                hasMain = true;
                if (function.returnType.kind != Kind::Int || !function.parameters.empty()) {
                    report(function.name.pos, "main must return int and take no parameters");
                }
            }
            checkFunction(function);
        }
        if (!hasMain) report(eof, "missing int main()");
        return std::move(diagnostics_);
    }

private:
    void report(SourcePos pos, std::string message) {
        diagnostics_.push_back({pos, std::move(message)});
    }
    void nameError(const Token& name, const char* message) {
        report(name.pos, std::string(message) + " '" + std::string(name.text) + "'");
    }
    Variable* findVariable(std::string_view name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }
    Variable* declare(const Token& name, bool isConst) {
        auto [entry, inserted] = scopes_.back().emplace(name.text, Variable{isConst});
        if (inserted) return &entry->second;
        nameError(name, "duplicate declaration of");
        return nullptr;
    }

    void checkFunction(const Function& fn) {
        returnType_ = fn.returnType.kind == Kind::Int ? Type::Int : Type::Void;
        // Register before visiting the body: self-recursion is visible, later functions are not.
        if (!functions_.emplace(fn.name.text, Signature{returnType_, fn.parameters.size()}).second) {
            nameError(fn.name, "duplicate function");
        }
        scopes_.emplace_back();
        for (const auto& parameter : fn.parameters) declare(parameter, false);
        const Exits exits = checkBlock(fn.body, true);
        if (returnType_ == Type::Int && (exits & next)) {
            report(fn.body.closingBrace, "int function can reach its closing brace without returning");
        }
        scopes_.pop_back();
    }

    Exits checkBlock(const Block& block, bool reachable) {
        Exits exits = next;
        for (const auto& statement : block.statements) {
            const Exits current = check(*statement, reachable && (exits & next));
            // Unreachable statements are checked, but cannot add exits to this block.
            if (exits & next) exits = (exits & ~next) | current;
        }
        return exits;
    }

    Exits check(const Stmt& stmt, bool reachable) {
        return std::visit([&](const auto& node) { return statement(node, stmt.pos, reachable); }, stmt.node);
    }

    Exits statement(const Block& node, SourcePos, bool reachable) {
        scopes_.emplace_back();
        const Exits exits = checkBlock(node, reachable);
        scopes_.pop_back();
        return exits;
    }
    Exits statement(const Empty&, SourcePos, bool) { return next; }
    Exits statement(const ExprStmt& node, SourcePos, bool reachable) {
        check(*node.expression, reachable);
        return next;
    }
    Exits statement(const Assign& node, SourcePos, bool reachable) {
        const auto* variable = findVariable(node.name.text);
        if (!variable) nameError(node.name, "not a declared variable:");
        else if (variable->isConst) nameError(node.name, "assignment to const");
        requireInt(*node.value, reachable);
        return next;
    }
    Exits statement(const Decl& node, SourcePos, bool reachable) {
        auto* variable = declare(node.name, node.isConst);
        if (node.initializer) {
            const auto initial = requireInt(*node.initializer, reachable);
            if (variable && variable->isConst) variable->constant = initial.constant;
        }
        return next;
    }
    Exits statement(const If& node, SourcePos, bool reachable) {
        const auto value = requireInt(*node.condition, reachable).constant;
        const Exits onTrue = check(*node.thenBranch, reachable && (!value || *value != 0));
        const Exits onFalse = node.elseBranch
            ? check(*node.elseBranch, reachable && (!value || *value == 0)) : next;
        return value ? (*value ? onTrue : onFalse) : onTrue | onFalse;
    }
    Exits statement(const While& node, SourcePos, bool reachable) {
        const auto value = requireInt(*node.condition, reachable).constant;
        ++loopDepth_;
        const Exits body = check(*node.body, reachable && (!value || *value != 0));
        --loopDepth_;
        if (value && *value == 0) return next;
        return (body & returned) | ((!value || (body & broke)) ? next : 0);
    }
    Exits statement(const Break&, SourcePos pos, bool) {
        if (loopDepth_ != 0) return broke;
        report(pos, "break outside a loop");
        return next;
    }
    Exits statement(const Continue&, SourcePos pos, bool) {
        if (loopDepth_ != 0) return continued;
        report(pos, "continue outside a loop");
        return next;
    }
    Exits statement(const Return& node, SourcePos pos, bool reachable) {
        if (returnType_ == Type::Int && !node.value) report(pos, "missing int return value");
        if (node.value) {
            const auto value = check(*node.value, reachable);
            if (returnType_ == Type::Void) report(pos, "void function cannot return a value");
            else if (value.type == Type::Void) report(pos, "return value must have type int");
        }
        return returned;
    }

    ExprInfo check(const Expr& expr, bool reachable) {
        return std::visit([&](const auto& node) { return expression(node, expr.token, reachable); }, expr.node);
    }
    ExprInfo requireInt(const Expr& expr, bool reachable) {
        const auto info = check(expr, reachable);
        if (info.type != Type::Void) return info;
        report(expr.token.pos, "void value used where an int is needed");
        return {Type::Error};
    }
    ExprInfo expression(const Number&, const Token& token, bool) {
        const auto value = magnitude(token.text);
        if (value && *value <= intMax) return {Type::Int, intValue(*value)};
        report(token.pos, "int literal is out of range");
        return {Type::Error};
    }
    ExprInfo expression(const Name&, const Token& token, bool) {
        if (const auto* variable = findVariable(token.text)) return {Type::Int, variable->constant};
        if (functions_.count(token.text)) nameError(token, "function is not an int value:");
        else nameError(token, "undeclared name");
        return {Type::Error};
    }
    ExprInfo expression(const Call& call, const Token& callee, bool reachable) {
        const auto fn = functions_.find(callee.text);
        const bool isFunction = fn != functions_.end() && !findVariable(callee.text);
        if (!isFunction) nameError(callee, "not a declared function:");
        bool badArguments = false;
        if (isFunction && fn->second.arity != call.arguments.size()) {
            nameError(callee, "wrong number of arguments for");
            badArguments = true;
        }
        for (const auto& arg : call.arguments) {
            const auto type = check(*arg, reachable).type;
            if (isFunction && !badArguments && type == Type::Void) {
                nameError(callee, "arguments must be int in call to");
                badArguments = true;
            }
        }
        return {isFunction && !badArguments ? fn->second.result : Type::Error};
    }
    ExprInfo expression(const Unary& node, const Token& op, bool reachable) {
        // INT_MIN is written as unary minus applied to magnitude 2147483648.
        if (op.kind == Kind::Minus && std::holds_alternative<Number>(node.operand->node) &&
            magnitude(node.operand->token.text) == -static_cast<std::int64_t>(intMin)) {
            return {Type::Int, intMin};
        }
        const auto operand = requireInt(*node.operand, reachable);
        if (operand.type == Type::Error || !operand.constant) return operand;
        const auto value = static_cast<std::int64_t>(*operand.constant);
        if (op.kind == Kind::Minus) return {Type::Int, intValue(-value)};
        if (op.kind == Kind::Bang) return {Type::Int, intValue(!value)};
        return operand;
    }

    ExprInfo expression(const Binary& node, const Token& op, bool reachable) {
        const auto left = requireInt(*node.left, reachable);
        const bool shortCircuit = left.constant &&
            ((op.kind == Kind::And && *left.constant == 0) ||
             (op.kind == Kind::Or && *left.constant != 0));
        const auto right = requireInt(*node.right, reachable && !shortCircuit);
        if (left.type == Type::Error || right.type == Type::Error) return {Type::Error};
        if (shortCircuit) return {Type::Int, intValue(op.kind == Kind::Or)};
        if ((op.kind == Kind::Slash || op.kind == Kind::Percent) && right.constant == 0) {
            if (reachable) report(op.pos, "division or remainder by zero");
            return {reachable ? Type::Error : Type::Int};
        }
        if (!left.constant || !right.constant) return {Type::Int};

        const std::int64_t l = *left.constant, r = *right.constant;
        switch (op.kind) {
        case Kind::Plus: return {Type::Int, intValue(l + r)};
        case Kind::Minus: return {Type::Int, intValue(l - r)};
        case Kind::Star: return {Type::Int, intValue(l * r)};
        case Kind::Slash: return {Type::Int, intValue(l / r)};
        case Kind::Percent:
            return {Type::Int, l == intMin && r == -1 ? std::nullopt : intValue(l % r)};
        case Kind::Less: return {Type::Int, intValue(l < r)};
        case Kind::Greater: return {Type::Int, intValue(l > r)};
        case Kind::LessEqual: return {Type::Int, intValue(l <= r)};
        case Kind::GreaterEqual: return {Type::Int, intValue(l >= r)};
        case Kind::Equal: return {Type::Int, intValue(l == r)};
        case Kind::NotEqual: return {Type::Int, intValue(l != r)};
        case Kind::And: return {Type::Int, intValue(l && r)};
        case Kind::Or: return {Type::Int, intValue(l || r)};
        default: return {Type::Error};
        }
    }

    std::unordered_map<std::string_view, Signature> functions_;
    std::vector<std::unordered_map<std::string_view, Variable>> scopes_;
    std::vector<Diagnostic> diagnostics_;
    Type returnType_ = Type::Void;
    unsigned loopDepth_ = 0;
};

} // namespace

std::vector<Diagnostic> checkSemantics(const Program& program, SourcePos endOfFile) {
    return Checker().run(program, endOfFile);
}

} // namespace toyc
