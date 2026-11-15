#include "parser.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace toyc {
namespace {

struct ParseError {}; // Unwind only as far as the next statement or function.

template<class Node>
ExprPtr expr(Token token, Node node) {
    return std::make_unique<Expr>(Expr{token, std::move(node)});
}

template<class Node>
StmtPtr stmt(Token token, Node node) {
    return std::make_unique<Stmt>(Stmt{token.pos, std::move(node)});
}

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    ParseResult run() {
        if (at(Kind::End)) report("expected a function definition");
        while (!at(Kind::End)) {
            const auto before = cursor_;
            try {
                result_.program.functions.push_back(parseFunction());
            } catch (const ParseError&) {
                ensureProgress(before);
                while (!at(Kind::End) && !startsFunction()) take();
            }
        }
        return std::move(result_);
    }

private:
    const Token& peek(std::size_t ahead = 0) const {
        return tokens_[std::min(cursor_ + ahead, tokens_.size() - 1)];
    }
    bool at(Kind kind) const { return peek().kind == kind; }
    bool nextIs(Kind kind) const { return peek(1).kind == kind; }
    Token take() {
        const Token token = peek();
        if (token.kind != Kind::End) ++cursor_;
        return token;
    }
    bool match(Kind kind) {
        if (!at(kind)) return false;
        take();
        return true;
    }
    void report(std::string message) {
        result_.diagnostics.push_back({peek().pos, std::move(message)});
    }
    [[noreturn]] void fail(std::string message) {
        report(std::move(message));
        throw ParseError{};
    }
    Token require(Kind kind, const char* message) {
        if (!at(kind)) fail(message);
        return take();
    }
    // Insert a missing delimiter without consuming the next real token.
    bool expect(Kind kind, const char* message) {
        if (match(kind)) return true;
        report(message);
        return false;
    }
    void ensureProgress(std::size_t before) {
        if (cursor_ == before && !at(Kind::End)) take();
    }
    bool startsFunction() const {
        return (at(Kind::Int) || at(Kind::Void)) && nextIs(Kind::Ident) &&
               peek(2).kind == Kind::LParen;
    }
    bool startsAssignment() const { return at(Kind::Ident) && nextIs(Kind::Assign); }

    bool startsExpression() const {
        switch (peek().kind) {
        case Kind::Ident: case Kind::Integer: case Kind::LParen:
        case Kind::Plus: case Kind::Minus: case Kind::Bang:
            return true;
        default:
            return false;
        }
    }

    bool startsStatement() const {
        switch (peek().kind) {
        case Kind::LBrace: case Kind::Semicolon: case Kind::Const: case Kind::Int:
        case Kind::If: case Kind::While: case Kind::Break: case Kind::Continue:
        case Kind::Return:
            return true;
        default:
            return startsExpression();
        }
    }

    void syncStatement() {
        while (!at(Kind::End) && !at(Kind::RBrace) && !at(Kind::LBrace) &&
               !at(Kind::If) && !at(Kind::While) && !at(Kind::Return) &&
               !at(Kind::Break) && !at(Kind::Continue) && !at(Kind::Const) &&
               !at(Kind::Int) && !startsFunction()) {
            if (match(Kind::Semicolon)) return;
            take();
        }
    }

    Function parseFunction() {
        if (!at(Kind::Int) && !at(Kind::Void)) fail("expected function return type");
        const Token type = take();
        const Token name = require(Kind::Ident, "expected function name");
        expect(Kind::LParen, "expected '('");

        std::vector<Token> parameters;
        if (!at(Kind::RParen) && !at(Kind::LBrace) && !at(Kind::End)) {
            do {
                expect(Kind::Int, "expected 'int' parameter type");
                parameters.push_back(require(Kind::Ident, "expected parameter name"));
            } while (match(Kind::Comma));
        }
        expect(Kind::RParen, "expected ')'");
        require(Kind::LBrace, "expected '{'");
        return {type, name, std::move(parameters), parseBlock()};
    }

    Block parseBlock() {
        Block block;
        while (!at(Kind::RBrace) && !at(Kind::End) && !startsFunction()) {
            const auto before = cursor_;
            try {
                block.statements.push_back(parseStatement());
            } catch (const ParseError&) {
                ensureProgress(before);
                syncStatement();
            }
        }
        block.closingBrace = peek().pos;
        expect(Kind::RBrace, "expected '}'");
        return block;
    }

    StmtPtr parseStatement() {
        const Token first = peek();
        if (match(Kind::LBrace)) return stmt(first, parseBlock());
        if (match(Kind::Semicolon)) return stmt(first, Empty{});
        if (match(Kind::Int) || match(Kind::Const)) return parseDeclaration(first);
        if (match(Kind::If) || match(Kind::While)) return parseControl(first);
        if (match(Kind::Break)) {
            expect(Kind::Semicolon, "expected ';'");
            return stmt(first, Break{});
        }
        if (match(Kind::Continue)) {
            expect(Kind::Semicolon, "expected ';'");
            return stmt(first, Continue{});
        }
        if (match(Kind::Return)) {
            ExprPtr value;
            if (!at(Kind::Semicolon) && !at(Kind::RBrace) && !at(Kind::End)) {
                value = parseOr();
            }
            expect(Kind::Semicolon, "expected ';'");
            return stmt(first, Return{std::move(value)});
        }
        if (startsAssignment()) {
            const Token name = take();
            take(); // '='
            auto value = parseOr();
            expect(Kind::Semicolon, "expected ';'");
            return stmt(first, Assign{name, std::move(value)});
        }
        if (!startsStatement()) fail("expected a statement");

        auto value = parseOr();
        expect(Kind::Semicolon, "expected ';'");
        return stmt(first, ExprStmt{std::move(value)});
    }

    StmtPtr parseDeclaration(Token first) {
        const bool isConst = first.kind == Kind::Const;
        if (isConst) expect(Kind::Int, "expected 'int' after 'const'");
        const Token name = require(Kind::Ident, "expected variable name");
        ExprPtr initializer;
        if (match(Kind::Assign)) initializer = parseOr();
        else if (isConst) report("expected '=' and an initializer");
        expect(Kind::Semicolon, "expected ';'");
        return stmt(first, Decl{name, isConst, std::move(initializer)});
    }

    StmtPtr parseControl(Token first) {
        ExprPtr condition;
        bool closed = false;
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
        StmtPtr body;
        if (!closed && !startsStatement()) {
            // Already diagnosed a broken header; skip its malformed tail.
            syncStatement();
            body = stmt(first, Empty{});
        } else {
            body = parseStatement();
        }

        if (first.kind == Kind::While) {
            return stmt(first, While{std::move(condition), std::move(body)});
        }
        StmtPtr elseBranch;
        if (match(Kind::Else)) elseBranch = parseStatement();
        return stmt(first, If{std::move(condition), std::move(body), std::move(elseBranch)});
    }

    using ParseLevel = ExprPtr (Parser::*)();
    ExprPtr parseLeftAssociative(ParseLevel next, std::initializer_list<Kind> operators) {
        auto left = (this->*next)();
        while (std::find(operators.begin(), operators.end(), peek().kind) != operators.end()) {
            const Token op = take();
            auto right = (this->*next)();
            left = expr(op, Binary{std::move(left), std::move(right)});
        }
        return left;
    }

    ExprPtr parseOr() { return parseLeftAssociative(&Parser::parseAnd, {Kind::Or}); }
    ExprPtr parseAnd() { return parseLeftAssociative(&Parser::parseRelation, {Kind::And}); }
    ExprPtr parseRelation() {
        return parseLeftAssociative(&Parser::parseAdd, {
            Kind::Less, Kind::Greater, Kind::LessEqual, Kind::GreaterEqual, Kind::Equal, Kind::NotEqual,
        });
    }
    ExprPtr parseAdd() { return parseLeftAssociative(&Parser::parseMultiply, {Kind::Plus, Kind::Minus}); }
    ExprPtr parseMultiply() {
        return parseLeftAssociative(&Parser::parseUnary, {Kind::Star, Kind::Slash, Kind::Percent});
    }

    ExprPtr parseUnary() {
        if (at(Kind::Plus) || at(Kind::Minus) || at(Kind::Bang)) {
            const Token op = take();
            return expr(op, Unary{parseUnary()});
        }
        return parsePrimary();
    }

    ExprPtr parsePrimary() {
        if (at(Kind::Integer)) return expr(take(), Number{});
        if (at(Kind::Ident)) {
            const Token name = take();
            if (!match(Kind::LParen)) return expr(name, Name{});
            Call call;
            if (!at(Kind::RParen)) {
                do { call.arguments.push_back(parseOr()); } while (match(Kind::Comma));
            }
            expect(Kind::RParen, "expected ')'");
            return expr(name, std::move(call));
        }
        if (match(Kind::LParen)) {
            auto value = parseOr();
            expect(Kind::RParen, "expected ')'");
            return value;
        }
        fail("expected an expression");
    }

    const std::vector<Token>& tokens_;
    std::size_t cursor_ = 0;
    ParseResult result_;
};

} // namespace

ParseResult parse(const std::vector<Token>& tokens) {
    return Parser(tokens).run();
}

} // namespace toyc
