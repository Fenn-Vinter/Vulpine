#pragma once
#include <vector>
#include <string_view>
#include "lexicon.hpp"

struct TokenEntry {
    TokenType type{};
    std::string_view str{};
    size_t line{};
    size_t column{};
};

using Tokens_t = std::vector<TokenEntry>;
