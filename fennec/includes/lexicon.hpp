#pragma once
#include <string_view>
#include <unordered_map>

#define INT_LIST \
    X(AutoInt, "iauto") \
    X(Int, "int") \
    X(I8, "i8") \
    X(I16, "i16") \
    X(I32, "i32") \
    X(I64, "i64") \
    X(I128, "i128")

#define UINT_LIST \
    X(AutoUInt, "uauto") \
    X(UInt, "uint") \
    X(U8, "u8") \
    X(U16, "u16") \
    X(U32, "u32") \
    X(U64, "u64") \
    X(U128, "u128")

#define WILDCARD_LIST \
    X(AutoWild, "wauto") \
    X(Wild, "wild") \
    X(W8, "w8") \
    X(W16, "w16") \
    X(W32, "w32") \
    X(W64, "w64") \
    X(W128, "w128")

#define FLOAT_LIST \
    X(AutoFloat, "fauto") \
    X(Float, "float") \
    X(F16, "f16") \
    X(F32, "f32") \
    X(F64, "f64") \
    X(Double, "double") \
    X(F128, "f128")

#define BOOL_LIST \
    X(Bool, "bool") \
    X(True, "true") \
    X(False, "false")

#define TYPES_LIST \
    INT_LIST \
    UINT_LIST \
    WILDCARD_LIST \
    FLOAT_LIST \
    BOOL_LIST \
    X(Char, "char") \
    X(Byte, "byte") \
    X(Bit, "bit") \

#define FLOW_LIST \
    X(Loop, "loop") \
    X(For, "for") \
    X(While, "while") \
    X(If, "if") \
    X(Break, "break") \
    X(Continue, "continue") \
    X(Route, "route") \
    X(Waypoint, "waypoint")

#define KEYWORD_LIST \
    X(Let, "let") \
    X(Const, "const") \
    X(Fn, "fn") \
    X(Class, "class") \
    X(Print, "print") \
    X(Return, "return") \
    X(Else, "else") \
    X(Export, "export") \
    X(Nullptr, "nullptr")

#define OPERATOR_LIST \
    X(Add, "+") \
    X(Subtract, "-") \
    X(Multiply, "*") \
    X(Divide, "/") \
    X(Modulo, "%") \
    X(Assign, "=") 

#define COMPARATIVE_OPERATOR_LIST \
    X(EQUAL, "==") \
    X(NOT_EQUAL, "!=") \
    X(NOT, "!") \
    X(LESS_EQUAL, "<=") \
    X(GREATER_EQUAL, ">=") \
    X(LESS, "<") \
    X(GREATER, ">") \
    X(AND, "&&") \
    X(AND_BIT, "&") \
    X(OR, "||") \
    X(OR_BIT, "|") \
    X(XOR, "&|")

#define OPERATIVE_ASSIGN_LIST \
    X(Add_Assign, "+=") \
    X(Subtract_Assign, "-=") \
    X(Multiply_Assign, "*=") \
    X(Divide_Assign, "/=") \
    X(Modulo_Assign, "%=")

#define UNARY_OPERATOR_LIST \
    X(Increment, "++") \
    X(Decrement, "--") \
    X(SQRT, "//") \
    X(Exponent, "**") \
    X(Address, "@") \
    X(Deref, "^")

#define LITERAL_LIST \
    X(Identifier, "%Identifier%") \
    X(Int_Literal, "%Int_Literal%") \
    X(UInt_Literal, "\%UInt_Literal%") \
    X(Float_Literal, "\%Float_Literal%") \
    X(String_Literal, "\%String_Literal%") \
    X(Char_Literal, "\%Char_Literal%") \
    X(Hex_Literal, "\%Hex_Literal%") \
    X(Binary_Literal, "\%Binary_Literal%")

#define PUNCTUATION_LIST \
    X(Colon, ":") \
    X(Semicolon, ";") \
    X(Comma, ",") \
    X(Punctuation, ".") \
    X(LeftBrace, "{") \
    X(RightBrace, "}") \
    X(LeftBracket, "[") \
    X(RightBracket, "]") \
    X(LeftParentheses, "(") \
    X(RightParentheses, ")")

#define PROJECT_LIST \
    X(Project, "project") \
    X(Version, "version") \
    X(Arch, "arch") \
    X(Entry, "entry") \
    X(Publisher, "publisher") \
    X(Output, "output") \
    X(ABI, "abi") \
    X(Optimization, "optimization") \
    X(Ruleset, "ruleset") \
    X(Format, "format") \
    X(Linker, "linker")

#define RULESET_LIST \
    X(allow_c_style_decl, "allow_c_style_decl") \
    X(decline_vulpine_style_decl, "decline_vulpine_style_decl") \
    X(decline_syscalls, "decline_syscalls") \
    X(decline_inline_languages, "decline_inline_languages") \
    X(decline_vulpine_standard_library, "decline_vulpine_standard_library") \
    X(decline_pointer_decleration, "decline_pointer_decleration")

#define TOKEN_LIST \
    FLOW_LIST \
    KEYWORD_LIST \
    OPERATOR_LIST \
    OPERATIVE_ASSIGN_LIST \
    UNARY_OPERATOR_LIST \
    LITERAL_LIST \
    TYPES_LIST \
    COMPARATIVE_OPERATOR_LIST \
    PUNCTUATION_LIST \
    PROJECT_LIST \
    RULESET_LIST \
    X(Eof, "EOF") \
    X(Invalid, "%INVALID%")

enum class TokenType {
    #define X(name, str) name,
    TOKEN_LIST
    #undef X
};

inline const std::unordered_map<std::string_view, TokenType> StringToToken = {
    #define X(name, str) {str, TokenType::name},
    TOKEN_LIST
    #undef X
};

inline const std::unordered_map<TokenType, std::string_view> TokenToString = {
    #define X(name, str) {TokenType::name, str},
    TOKEN_LIST
    #undef X
};

namespace TokenUtils {
    inline auto isInt(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            INT_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isUInt(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            UINT_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isFloat(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            FLOAT_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isWildcard(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            WILDCARD_LIST
            #undef X
            case TokenType::Nullptr:
                return true;
            default: return false;
        }
    }

    inline auto isPointerType(TokenType t) -> bool {
        return t == TokenType::Nullptr;
    }

    inline auto isTypedef(TokenType t) -> bool {
        return (isWildcard(t) || isFloat(t) || isUInt(t) || isInt(t) || t == TokenType::Bool || t == TokenType::Char || t == TokenType::Nullptr);
    }

    inline auto isNumericType(TokenType t) -> bool {
        return (isInt(t) || isUInt(t) || isFloat(t) || t == TokenType::Bool || t == TokenType::Char);
    }

    inline auto getTypeWidth(TokenType t) -> int {
        switch (t) {
            case TokenType::Bool: return 1;
            case TokenType::Char: return 8;
            case TokenType::I8:
            case TokenType::U8: return 8;
            case TokenType::I16:
            case TokenType::U16: return 16;
            case TokenType::Int:
            case TokenType::UInt:
            case TokenType::I32:
            case TokenType::U32:
            case TokenType::W32:
            case TokenType::F32: return 32;
            case TokenType::AutoInt:
            case TokenType::AutoUInt:
            case TokenType::AutoWild: return static_cast<int>(sizeof(void*) * 8);
            case TokenType::I64:
            case TokenType::U64:
            case TokenType::W64:
            case TokenType::F64: return 64;
            case TokenType::I128:
            case TokenType::U128:
            case TokenType::W128:
            case TokenType::F128: return 128;
            case TokenType::Nullptr: return static_cast<int>(sizeof(void*) * 8);
            default: return -1;
        }
    }

    inline auto isOperator(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            OPERATOR_LIST
            OPERATIVE_ASSIGN_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isComparativeOperator(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            COMPARATIVE_OPERATOR_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isUnaryOperator(TokenType t) -> bool {
        switch (t) {
            case TokenType::NOT:
                return true;
            #define X(name, str) case TokenType::name:
            UNARY_OPERATOR_LIST
            #undef X
                return true;
            default: return false;
        }
    }

    inline auto isLiteral(TokenType t) -> bool {
        switch (t) {
            #define X(name, str) case TokenType::name:
            LITERAL_LIST
            #undef X
            case TokenType::Nullptr:
                return true;
            default: return false;
        }
    }
}