#pragma once

#include "ast.hpp"

namespace toyc {

struct ParseResult {
    Program program;
    std::vector<Diagnostic> diagnostics;
};

// Requires a token stream ending with Kind::End.
ParseResult parse(const std::vector<Token>& tokens);

} // namespace toyc
