#pragma once
#include <lexicon.hpp>
#include <string_view>
#include <tokens.hpp>

class Lexer {
    public:
        Lexer();
        ~Lexer();

        auto lexify(const std::string_view& src) -> Tokens_t*;
        auto retrieveTokens() -> Tokens_t*;
        auto clearLexer() -> void;
    private:
        auto lexIdentifier(const std::string_view& src, size_t& i) -> TokenEntry;
        auto lexNumber(const std::string_view& src, size_t& i) -> TokenEntry;
        
        Tokens_t tokens = {};
};