#pragma once
#include <memory>

#include <lexer.hpp>
#include <parser.hpp>

class Fennec {
    public:
        Fennec() 
            : lexer(std::make_unique<Lexer>()), 
              parser(std::make_unique<Parser>()) {}
        ~Fennec() = default;

        auto LexerInstance() -> Lexer* { return lexer.get(); }
        auto ParserInstance() -> Parser* { return parser.get(); }
    private:
        std::unique_ptr<Lexer> lexer = {};
        std::unique_ptr<Parser> parser = {};
};
