#pragma once
#include <vector>
#include <lexicon.hpp>

struct TokenEntry {
    TokenType type;
    std::string_view str;
};

using Tokens_t = std::vector<TokenEntry>;
