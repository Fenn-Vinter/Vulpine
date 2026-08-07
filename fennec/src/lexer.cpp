#include <cctype>
#include <lexer.hpp>
#include <string_view>
#include <tokens.hpp>

Lexer::Lexer() = default;
Lexer::~Lexer() = default;

auto Lexer::clearLexer() -> void { tokens.clear(); }

auto Lexer::retrieveTokens() -> Tokens_t* { return &tokens; }

auto Lexer::lexIdentifier(const std::string_view& src, size_t& i, size_t line, size_t col) -> TokenEntry {
    size_t start = i;

    if (std::isalpha(src[i]) || src[i] == '_') {
        i++;
        while (i < src.length() && (std::isalnum(src[i]) || src[i] == '_')) {
            i++;
        }

        std::string_view name = src.substr(start, i - start);

        if (auto it = StringToToken.find(name); it != StringToToken.end()) {
            return {it->second, name, line, col};
        }

        return {TokenType::Identifier, name, line, col};
    }
    return {TokenType::Identifier, "", line, col};
}

auto Lexer::lexNumber(const std::string_view& src, size_t& i, size_t line, size_t col) -> TokenEntry {
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

    return {type, src.substr(start, i - start), line, col};
}

auto Lexer::lexify(const std::string_view& src) -> Tokens_t* {
    clearLexer();
    size_t i = 0;
    size_t line = 1;
    size_t column = 1;

    while (i < src.length()) {
        // 1. Whitespaces & newline tracking
        if (std::isspace(static_cast<unsigned char>(src[i]))) {
            if (src[i] == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            i++;
            continue;
        }

        // 2. Comments
        if (i + 3 < src.length() && src.substr(i, 4) == "/---") {
            i += 4;
            column += 4;
            
            bool isMultiLine = (i < src.length() && src[i] == '/');
            if (isMultiLine) {
                i++; // consume inner '/'
                column++;
                while (i + 3 < src.length() && src.substr(i, 4) != "---/") {
                    if (src[i] == '\n') {
                        line++;
                        column = 1;
                    } else {
                        column++;
                    }
                    i++;
                }
                if (i + 3 < src.length()) {
                    i += 4; // consume "---/"
                    column += 4;
                }
            } else {
                // Single line comment
                while (i < src.length() && src[i] != '\n') {
                    i++;
                    column++;
                }
            }
            continue;
        }

        bool matched = false;
        size_t tokenStartCol = column;

        // 3. Numbers
        if (!matched && (std::isdigit(static_cast<unsigned char>(src[i])) || 
            (src[i] == '-' && i + 1 < src.length() && std::isdigit(static_cast<unsigned char>(src[i + 1]))))) {
            size_t prevI = i;
            tokens.push_back(lexNumber(src, i, line, tokenStartCol));
            column += (i - prevI);
            matched = true;
        }

        // 4. Identifiers & Keywords
        if (!matched && (std::isalpha(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
            size_t prevI = i;
            tokens.push_back(lexIdentifier(src, i, line, tokenStartCol));
            column += (i - prevI);
            matched = true;
        }

        // 5. Strings
        if (!matched && src[i] == '"') {
            size_t start = i;
            i++; // skip opening quote
            column++;
            
            while (i < src.length() && src[i] != '"') {
                if (src[i] == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
                i++;
            }
            
            if (i < src.length()) {
                i++; // skip closing quote
                column++;
            }
            
            tokens.push_back({TokenType::String_Literal, src.substr(start, i - start), line, tokenStartCol});
            matched = true;
        }

        // 6. Multi-Character Tokens
        if (!matched && i + 1 < src.length()) {
            std::string_view twoChar = src.substr(i, 2);
            if (auto it = StringToToken.find(twoChar); it != StringToToken.end()) {
                tokens.push_back({it->second, twoChar, line, tokenStartCol});
                i += 2;
                column += 2;
                matched = true;
            }
        }

        // 7. Single-Character Tokens
        if (!matched) {
            std::string_view oneChar = src.substr(i, 1);
            if (auto it = StringToToken.find(oneChar); it != StringToToken.end()) {
                tokens.push_back({it->second, oneChar, line, tokenStartCol});
                i += 1;
                column += 1;
                matched = true;
            }
        }

        // 8. Fallback for unmatched raw characters
        if (!matched) {
            i++;
            column++;
        }
    }
    return &tokens;
}
