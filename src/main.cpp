#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <string_view>

namespace {

void printDiagnostics(const std::vector<toyc::Diagnostic>& errors, bool includeReason) {
    std::cout << "reject\n";
    for (const auto& error : errors) {
        std::cout << error.pos.line;
        if (includeReason) std::cout << ' ' << error.message;
        std::cout << '\n';
    }
}

void printTokens(const toyc::LexResult& lexed) {
    if (!lexed.diagnostics.empty()) {
        printDiagnostics(lexed.diagnostics, true);
        std::cout << '\n';
    }
    for (std::size_t i = 0; lexed.tokens[i].kind != toyc::Kind::End; ++i) {
        const auto& token = lexed.tokens[i];
        std::cout << i << ':';
        if (token.kind == toyc::Kind::Ident) std::cout << "Ident";
        else if (token.kind == toyc::Kind::Integer) std::cout << "IntConst";
        else std::cout << '\'' << token.text << '\'';
        std::cout << ":\"" << token.text << "\"\n";
    }
}

void checkProgram(const toyc::LexResult& lexed) {
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
    std::stable_sort(errors.begin(), errors.end(), [](const auto& a, const auto& b) {
        return a.pos.offset < b.pos.offset;
    });
    if (parsed.diagnostics.empty()) {
        // Semantic output reports each affected line once; syntax can repeat a line.
        errors.erase(std::unique(errors.begin(), errors.end(), [](const auto& a, const auto& b) {
            return a.pos.line == b.pos.line;
        }), errors.end());
    }
    printDiagnostics(errors, false);
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode != "-token" && mode != "-ast") {
        std::cerr << "Usage: compiler -token|-ast < source.c\n";
        return 2;
    }
    std::ios::sync_with_stdio(false);
    const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
    const auto lexed = toyc::lex(source);
    if (mode == "-token") printTokens(lexed);
    else checkProgram(lexed);
    return 0;
}
