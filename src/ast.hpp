#pragma once

#include "token.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace toyc {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Number {};
struct Name {};
struct Call { std::vector<ExprPtr> arguments; };
struct Unary { ExprPtr operand; };
struct Binary { ExprPtr left, right; };

struct Expr {
    Token token; // Literal, name, callee, or operator, depending on the node.
    std::variant<Number, Name, Call, Unary, Binary> node;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Block {
    std::vector<StmtPtr> statements;
    SourcePos closingBrace;
};
struct Empty {};
struct ExprStmt { ExprPtr expression; };
struct Assign { Token name; ExprPtr value; };
struct Decl { Token name; bool isConst; ExprPtr initializer; };
struct If { ExprPtr condition; StmtPtr thenBranch, elseBranch; };
struct While { ExprPtr condition; StmtPtr body; };
struct Break {};
struct Continue {};
struct Return { ExprPtr value; };

struct Stmt {
    SourcePos pos;
    std::variant<Block, Empty, ExprStmt, Assign, Decl, If, While, Break, Continue, Return> node;
};

struct Function {
    Token returnType;
    Token name;
    std::vector<Token> parameters;
    Block body;
};

struct Program {
    std::vector<Function> functions;
};

} // namespace toyc
