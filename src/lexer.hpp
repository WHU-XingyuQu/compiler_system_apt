#pragma once

#include "token.hpp"

#include <vector>

namespace toyc {

struct LexResult {
    std::vector<Token> tokens; // The last token is always End.
    std::vector<Diagnostic> diagnostics;
};

LexResult lex(std::string_view source);

} // namespace toyc
