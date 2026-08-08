#pragma once
#include <tokens.hpp>
#include <vector>
#include <memory>
#include <AST.hpp>
#include <unordered_map>
#include <string>
#include <string_view>
#include <cstdint>

struct SymbolInfo {
    std::string name{};          
    std::string fqn{};           
    TokenType type{};
    uint32_t fileId{};           
    bool isInitialized{false};
};

struct Scope {
    std::string scopePath{};     
    size_t anonymousBlockCount{0}; 
    std::unordered_map<std::string, SymbolInfo> symbols{};
    Scope* parent{nullptr};
};

class Parser {
public:
    Parser();
    ~Parser();

    auto Parse(const Tokens_t* tokens, std::string_view filepath) -> std::vector<std::unique_ptr<BaseNode>>;
    auto getErrors() const -> const std::vector<std::string>&;
    auto hasErrors() const -> bool;

private:
    const Tokens_t* m_tokens{nullptr};
    std::vector<std::unique_ptr<ProjectNode>> m_projects{};
    ProjectNode* m_activeProject{nullptr};
    size_t m_pos{0};
    
    bool m_hasError{false};
    bool m_panicMode{false};
    std::vector<std::string> m_errors{};

    auto reportError(const TokenEntry& token, std::string_view message) -> void;
    auto reportWarning(const TokenEntry& token, std::string_view message) -> void;
    auto synchronize() -> void;

    uint32_t m_currentFileId{0};
    std::string m_currentFileScopePrefix{}; 
    std::string m_currentFilePath{};
    TokenType m_currentFunctionReturnType = TokenType::Invalid;
    Scope* m_currentScope{nullptr};
    
    auto enterScope(std::string_view scopeName) -> void;
    auto enterAnonymousScope() -> void;
    auto exitScope() -> void;
    
    auto lookup(std::string_view name) -> SymbolInfo*;
    auto define(std::string_view name, TokenType type, bool initialized) -> SymbolInfo&;
    
    auto peek() const -> const TokenEntry&;
    auto peekNext() const -> const TokenEntry&;
    auto consume() -> const TokenEntry&;
    auto expect(TokenType type) -> bool;
    auto isAtEnd() const -> bool;
    
    auto parseProjectDecl() -> std::unique_ptr<ProjectNode>;
    auto parseBlock() -> std::unique_ptr<BlockNode>;
    auto parseStatement() -> std::unique_ptr<BaseNode>;
    auto parseExpression() -> std::unique_ptr<BaseNode>;
    auto parseVariableDecl() -> std::unique_ptr<BaseNode>;
    auto parseIfStatement() -> std::unique_ptr<BaseNode>;
    auto parseFunctionDecl() -> std::unique_ptr<BaseNode>;
    auto parsePrimary() -> std::unique_ptr<BaseNode>;
    auto parseArrayLiteral() -> std::unique_ptr<ArrayNode>;
    auto parseCall(std::unique_ptr<BaseNode> callee) -> std::unique_ptr<BaseNode>;
    auto parseTypeSpecifier(TokenType& outElementType, bool& outIsArray, size_t& outArraySize) -> bool;
    auto deriveBinaryResultType(const BaseNode* left, const BaseNode* right, TokenType op) -> TokenType;
    auto canCast(TokenType src, TokenType dst) -> bool;
    
    auto parseAsmBlock() -> std::unique_ptr<BaseNode>;
    auto parseExportDecl() -> std::unique_ptr<BaseNode>;
    
    auto parsePostfixAccess(std::unique_ptr<BaseNode> expr) -> std::unique_ptr<BaseNode>;
    auto parsePostfixCast(std::unique_ptr<BaseNode> expr) -> std::unique_ptr<BaseNode>;
};