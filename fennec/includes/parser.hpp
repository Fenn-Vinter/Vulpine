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
    std::string name;          // Unqualified name ("VGA_BUFFER")
    std::string fqn;           // Fully Qualified Name ("A7F9::net::0::VGA_BUFFER")
    TokenType type;
    uint32_t fileId;           // Encoded directory/file index
};

struct Scope {
    std::string scopePath;     // e.g. "A7F9::net::0"
    size_t anonymousBlockCount = 0; // Incremented every time an anonymous {} is entered
    
    // Quick lookup within *this* immediate scope
    std::unordered_map<std::string_view, SymbolInfo> symbols;
    
    Scope* parent = nullptr;
};

class Parser {
public:
    Parser();
    ~Parser();

    auto Parse(const Tokens_t* tokens, std::string_view filepath) -> std::vector<std::unique_ptr<BaseNode>>;

private:
    const Tokens_t* m_tokens = nullptr;
    std::vector<std::unique_ptr<ProjectNode>> m_projects;
    ProjectNode* m_activeProject = nullptr;
    size_t m_pos = 0;

    // --- Error Recovery & Diagnostics ---
    bool m_hasError = false;
    bool m_panicMode = false;

    auto reportError(const TokenEntry& token, std::string_view message) -> void;
    auto synchronize() -> void;

    // --- File & Provenance Tracking ---
    uint32_t m_currentFileId = 0;
    std::string m_currentFileScopePrefix; // e.g. "A7F9"

    // --- Scope Management ---
    Scope* m_currentScope = nullptr;
    
    // Enter named scope (e.g. namespace or function)
    auto enterScope(std::string_view scopeName) -> void;
    
    // Enter anonymous block scope ({})
    auto enterAnonymousScope() -> void;
    
    auto exitScope() -> void;
    
    auto lookup(std::string_view name) -> SymbolInfo*;
    auto define(std::string_view name, TokenType type) -> SymbolInfo&;

    // --- Parsing Helpers ---
    auto peek() const -> const TokenEntry&;
    auto peekNext() const -> const TokenEntry&;
    auto consume() -> const TokenEntry&;
    auto expect(TokenType type) -> bool;
    auto isAtEnd() const -> bool;

    // --- Grammar Parsers ---
    auto parseProjectDecl() -> std::unique_ptr<ProjectNode>;
    auto parseBlock() -> std::unique_ptr<BlockNode>;
    auto parseStatement() -> std::unique_ptr<BaseNode>;
    auto parseExpression() -> std::unique_ptr<BaseNode>;
    auto parseVariableDecl() -> std::unique_ptr<VariableNode>;
    auto parseFunctionDecl() -> std::unique_ptr<BaseNode>;
    auto parsePrimary() -> std::unique_ptr<BaseNode>;
    
    // --- Special Block Extensions (Vulpine Features) ---
    auto parseAsmBlock() -> std::unique_ptr<BaseNode>;
    auto parseExportDecl() -> std::unique_ptr<BaseNode>;
};