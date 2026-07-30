#pragma once
#include <tokens.hpp>
#include <vector>
#include <memory>
#include <AST.hpp>
#include <unordered_map>

struct SymbolInfo {
    std::string name;
    TokenType type;
};

struct Scope {
    std::unordered_map<std::string_view, SymbolInfo> symbols;
    Scope* parent = nullptr;
};

class Parser {
public:
    Parser();
    ~Parser();

    auto Parse(const Tokens_t* tokens) -> std::vector<std::unique_ptr<BaseNode>>;

private:
    const Tokens_t* m_tokens = nullptr;
    std::vector<std::unique_ptr<ProjectNode>> m_projects;
    ProjectNode* m_activeProject = nullptr;
    size_t m_pos = 0;

    // --- Scope Management ---
    Scope* m_currentScope = nullptr;
    auto enterScope() -> void;
    auto exitScope() -> void;
    auto lookup(std::string_view name) -> SymbolInfo*;
    auto define(std::string_view name, TokenType type) -> void;

    // --- Parsing Helpers ---
    auto peek() const -> const TokenEntry&;
    auto peekNext() const -> const TokenEntry&;
    auto consume() -> const TokenEntry&;
    auto expect(TokenType type) -> bool;
    auto isAtEnd() const -> bool;

    auto parseProjectDecl() -> std::unique_ptr<ProjectNode>;
    auto parseBlock() -> std::unique_ptr<BlockNode>;
    auto parseStatement() -> std::unique_ptr<BaseNode>;
    auto parseExpression() -> std::unique_ptr<BaseNode>;
    auto parseVariableDecl() -> std::unique_ptr<VariableNode>;
};