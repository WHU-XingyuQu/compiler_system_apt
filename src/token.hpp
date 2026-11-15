#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace toyc {

struct SourcePos {
    std::size_t offset = 0;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Diagnostic {
    SourcePos pos;
    std::string message;
};

enum class Kind {
    End, Ident, Integer,
    Int, Void, Const, If, Else, While, Break, Continue, Return,
    LParen, RParen, LBrace, RBrace, Comma, Semicolon,
    Assign, Plus, Minus, Star, Slash, Percent, Bang,
    Less, Greater, LessEqual, GreaterEqual, Equal, NotEqual, And, Or,
};

struct Token {
    Kind kind;
    std::string_view text; // A slice of the input owned by the driver.
    SourcePos pos;
};

} // namespace toyc
