#include "lexer.hpp"

#include <utility>

namespace toyc {
namespace {

using Spelling = std::pair<std::string_view, Kind>;

constexpr Spelling keywords[] = {
    {"int", Kind::Int}, {"void", Kind::Void}, {"const", Kind::Const},
    {"if", Kind::If}, {"else", Kind::Else}, {"while", Kind::While},
    {"break", Kind::Break}, {"continue", Kind::Continue}, {"return", Kind::Return},
};

// Put two-character operators before their one-character prefixes.
constexpr Spelling symbols[] = {
    {"<=", Kind::LessEqual}, {">=", Kind::GreaterEqual}, {"==", Kind::Equal},
    {"!=", Kind::NotEqual}, {"&&", Kind::And}, {"||", Kind::Or},
    {"(", Kind::LParen}, {")", Kind::RParen}, {"{", Kind::LBrace}, {"}", Kind::RBrace},
    {",", Kind::Comma}, {";", Kind::Semicolon}, {"=", Kind::Assign},
    {"+", Kind::Plus}, {"-", Kind::Minus}, {"*", Kind::Star}, {"/", Kind::Slash},
    {"%", Kind::Percent}, {"!", Kind::Bang}, {"<", Kind::Less}, {">", Kind::Greater},
};

bool digit(char c) {
    return c >= '0' && c <= '9';
}

bool identStart(char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Report a valid non-ASCII code point once. Invalid UTF-8 recovers one byte at a time.
std::size_t utf8Width(std::string_view remaining) {
    const auto lead = static_cast<unsigned char>(remaining[0]);
    std::size_t width = 1;
    if (lead >= 0xC2 && lead <= 0xDF) width = 2;
    else if (lead >= 0xE0 && lead <= 0xEF) width = 3;
    else if (lead >= 0xF0 && lead <= 0xF4) width = 4;
    if (width == 1 || remaining.size() < width) return 1;

    const auto second = static_cast<unsigned char>(remaining[1]);
    if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F) ||
        (lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)) return 1;
    for (std::size_t i = 1; i < width; ++i) {
        if ((static_cast<unsigned char>(remaining[i]) & 0xC0) != 0x80) return 1;
    }
    return width;
}

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    LexResult run() {
        while (!end()) scan();
        result_.tokens.push_back({Kind::End, {}, pos_});
        return std::move(result_);
    }

private:
    bool end() const { return pos_.offset == source_.size(); }
    char current() const { return end() ? '\0' : source_[pos_.offset]; }
    std::string_view rest() const { return source_.substr(pos_.offset); }
    bool startsWith(std::string_view text) const {
        return rest().substr(0, text.size()) == text;
    }

    void advance() {
        const char c = source_[pos_.offset++];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && current() == '\n') ++pos_.offset;
            ++pos_.line;
            pos_.column = 0;
        } else {
            ++pos_.column;
        }
    }

    void emit(Kind kind, SourcePos start) {
        result_.tokens.push_back({kind, source_.substr(start.offset, pos_.offset - start.offset), start});
    }

    void scan() {
        const SourcePos start = pos_;
        if (space(current())) {
            advance();
            return;
        }
        if (startsWith("//")) {
            while (!end() && current() != '\n' && current() != '\r') advance();
            return;
        }
        if (startsWith("/*")) {
            advance();
            advance();
            while (!end() && !startsWith("*/")) advance();
            if (end()) {
                result_.diagnostics.push_back({start, "unterminated comment"});
            } else {
                advance();
                advance();
            }
            return;
        }
        if (identStart(current())) {
            do { advance(); } while (identStart(current()) || digit(current()));
            const auto word = source_.substr(start.offset, pos_.offset - start.offset);
            for (const auto& [text, kind] : keywords) {
                if (word == text) {
                    emit(kind, start);
                    return;
                }
            }
            emit(Kind::Ident, start);
            return;
        }
        if (digit(current())) {
            do { advance(); } while (digit(current()));
            emit(Kind::Integer, start);
            return;
        }
        for (const auto& [text, kind] : symbols) {
            if (startsWith(text)) {
                for (std::size_t i = 0; i < text.size(); ++i) advance();
                emit(kind, start);
                return;
            }
        }

        const std::size_t width = utf8Width(rest());
        const auto invalid = rest().substr(0, width);
        const auto lead = static_cast<unsigned char>(invalid[0]);
        const std::string detail = (width == 1 && (lead < 32 || lead >= 127))
            ? "illegal input byte " + std::to_string(lead)
            : "illegal character '" + std::string(invalid) + "'";
        result_.diagnostics.push_back({start, detail});
        pos_.offset += width;
        pos_.column += width;
    }

    std::string_view source_;
    SourcePos pos_;
    LexResult result_;
};

} // namespace

LexResult lex(std::string_view source) {
    return Lexer(source).run();
}

} // namespace toyc
