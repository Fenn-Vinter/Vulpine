#include <cctype>
#include <lexer.hpp>
#include <string_view>
#include <tokens.hpp>

Lexer::Lexer() = default;
Lexer::~Lexer() = default;

auto Lexer::clearLexer() -> void { tokens.clear(); }

auto Lexer::retrieveTokens() -> Tokens_t* { return &tokens; }

auto Lexer::lexIdentifier(const std::string_view& src, size_t& i) -> TokenEntry {
    size_t start = i;

    if (std::isalpha(src[i]) || src[i] == '_') {
        i++;
        while (i < src.length() && (std::isalnum(src[i]) || src[i] == '_')) {
            i++;
        }

        std::string_view name = src.substr(start, i - start);

        if (auto it = StringToToken.find(name); it != StringToToken.end()) {
            return {it->second, name};
        }

        return {TokenType::Identifier, name};
    }
    return {TokenType::Identifier, ""};
}

auto Lexer::lexNumber(const std::string_view& src, size_t& i) -> TokenEntry {
    size_t start = i;
    TokenType type = TokenType::UInt_Literal;

    if (src[i] == '-') {
        type = TokenType::Int_Literal;
        i++;
    }

    if (i + 1 < src.length()) {
        if (src[i] == '0' && src[i + 1] == 'x') { type = TokenType::Hex_Literal; i += 2; }
        else if (src[i] == '0' && src[i + 1] == 'b') { type = TokenType::Binary_Literal; i += 2; }
    }

    while (i < src.length()) {
        if (std::isdigit(src[i]) || (type == TokenType::Hex_Literal && std::isxdigit(src[i]))) {
            i++;
        } else if (src[i] == '\'') {
            if (i > 0 && std::isdigit(src[i - 1]) && i + 1 < src.length() && std::isdigit(src[i + 1])) {
                i++;
            } else {
                break;
            }
        } else if (src[i] == '.' && type != TokenType::Hex_Literal && type != TokenType::Binary_Literal) {
            type = TokenType::Float_Literal;
            i++;
        } else {
            break;
        }
    }

    return {type, src.substr(start, i - start)};
}

auto Lexer::lexify(const std::string_view& src) -> Tokens_t* {
    clearLexer();
    size_t i = 0;

    while (i < src.length()) {
        // 1. Always clear out whitespaces first
        if (std::isspace(src[i])) {
            i++;
            continue;
        }

        // 2. Clear out comments
        if (i + 3 < src.length() && src.substr(i, 4) == "/---") {
            i += 4;
            // Check if it's a multi-line block comment: /---/
            bool isMultiLine = (i < src.length() && src[i] == '/');
            if (isMultiLine) {
                i++; // consume the inner '/'
                while (i + 3 < src.length() && src.substr(i, 4) != "---/") {
                    i++;
                }
                i += 4; // consume "---/"
            } else {
                // Line comment: skip until newline
                while (i < src.length() && src[i] != '\n') {
                    i++;
                }
            }
            continue;
        }

        bool matched = false;

        // 3. Match Numbers
        if (!matched && (std::isdigit(src[i]) || (src[i] == '-' && i + 1 < src.length() && std::isdigit(src[i + 1])))) {
            tokens.push_back(lexNumber(src, i));
            matched = true;
        }

        // 4. Match Identifiers & Keywords (Prioritized over symbols to protect text loops)
        if (!matched && (std::isalpha(src[i]) || src[i] == '_')) {
            tokens.push_back(lexIdentifier(src, i));
            matched = true;
        }

        // 5. Match Strings
        if (!matched && src[i] == '"') {
            size_t start = i;
            i++;
            while (i < src.length() && src[i] != '"') {
                i++;
            }
            if (i < src.length()) {
                i++; // Consumes trailing quote safely
            }
            tokens.push_back({TokenType::String_Literal, src.substr(start, i - start)});
            matched = true;
        }

        // 6. Match Multi-Character Tokens (like operators or arrows)
        if (!matched && i + 1 < src.length()) {
            std::string_view twoChar = src.substr(i, 2);
            if (auto it = StringToToken.find(twoChar); it != StringToToken.end()) {
                tokens.push_back({it->second, twoChar});
                i += 2;
                matched = true;
            }
        }

        // 7. Match Single-Character Tokens (brackets, colons, single symbols)
        if (!matched) {
            std::string_view oneChar = src.substr(i, 1);
            if (auto it = StringToToken.find(oneChar); it != StringToToken.end()) {
                tokens.push_back({it->second, oneChar});
                i += 1;
                matched = true;
            }
        }

        // 8. Fallback step for raw unmatched spaces or characters
        if (!matched) {
            i++;
        }
    }
    return &tokens;
}
