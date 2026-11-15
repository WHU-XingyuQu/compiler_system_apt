#pragma once

#include "ast.hpp"

namespace toyc {

// Run only after successful lexing and parsing.
std::vector<Diagnostic> checkSemantics(const Program& program, SourcePos endOfFile);

} // namespace toyc
