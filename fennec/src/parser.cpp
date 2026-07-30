#include "AST.hpp"
#include "lexicon.hpp"
#include <iostream>
#include <memory>
#include <parser.hpp>
#include <string_view>
#include <VulpineSettings.hpp>

Parser::Parser() {
    enterScope();
};

Parser::~Parser() {
    while (m_currentScope != nullptr) {
        exitScope();
    }
}

auto Parser::Parse(const Tokens_t* tokens) -> std::vector<std::unique_ptr<BaseNode>> {
    m_tokens = tokens;
    m_pos = 0;
    std::vector<std::unique_ptr<BaseNode>> rootNodes;

    while (!isAtEnd()) {
        auto node = parseStatement();
        if (node) {
            rootNodes.push_back(std::move(node));
        } else {
            // Safety break: prevent infinite loop on malformed input
            if (!isAtEnd()) consume(); 
        }
    }
    return rootNodes;
}

auto Parser::isAtEnd() const -> bool {
    return m_pos >= m_tokens->size();
}

auto Parser::peek() const -> const TokenEntry& {
    return (*m_tokens)[m_pos];
}

auto Parser::peekNext() const -> const TokenEntry& {
    if (m_pos + 1 >= m_tokens->size()) return (*m_tokens)[m_pos];
    return (*m_tokens)[m_pos + 1];
}

auto Parser::consume() -> const TokenEntry& {
    return (*m_tokens)[m_pos++];
}

auto Parser::expect(TokenType type) -> bool {
    if (!isAtEnd() && peek().type == type) {
        consume();
        return true;
    }
    return false;
}

auto Parser::enterScope() -> void {
    m_currentScope = new Scope{ {}, m_currentScope };
}

auto Parser::exitScope() -> void {
    Scope* old = m_currentScope;
    m_currentScope = m_currentScope->parent;
    delete old;
}

auto Parser::define(std::string_view name, TokenType type) -> void {
    m_currentScope->symbols[name] = { std::string(name), type };
}

auto Parser::lookup(std::string_view name) -> SymbolInfo* {
    Scope* search = m_currentScope;
    while (search != nullptr) {
        if (search->symbols.contains(name)) {
            return &search->symbols[name];
        }
        search = search->parent;
    }
    return nullptr;
}

auto Parser::parseExpression() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;
    auto token = consume(); 
    return std::make_unique<LiteralNode>(std::string(token.str));
}

auto Parser::parseProjectDecl() -> std::unique_ptr<ProjectNode> {
    consume(); // consume 'project'
    std::cout << "Parsing project declaration...\n";
    auto node = std::make_unique<ProjectNode>();

    if (peek().type == TokenType::Identifier) {
        node->setProjectName(consume().str);
        std::cout << "Project Name: " << node->getProjectName() << "\n";
    }

    if (expect(TokenType::LeftBrace)) {
        while (!isAtEnd() && peek().type != TokenType::RightBrace) {
            // Grab the token type of the key directly
            TokenType keyType = peek().type;
            consume(); // consume key token

            expect(TokenType::Assign);
            
            if (keyType == TokenType::Version) {
                // Accept either String_Literal or Identifier/Keyword as version string
                if (peek().type == TokenType::String_Literal || peek().type == TokenType::Identifier) {
                    std::string_view rawStr = consume().str;
                    
                    // Strip leading/trailing quotes if present
                    if (rawStr.size() >= 2 && rawStr.front() == '"' && rawStr.back() == '"') {
                        rawStr = rawStr.substr(1, rawStr.size() - 2);
                    }
                    
                    node->setVulpineVersion(VulpineSettings::VulpineVersions::resolveVersionAlias(rawStr));
                    std::cout << "Vulpine Version: " << node->getVulpineVersion() << "\n";
                } else {
                    std::cerr << "[ERROR]: Expected version string for key 'version'.\n";
                    return nullptr;
                }
            }
            else if (keyType == TokenType::Arch) {
                // Parse architecture array or singular identifier/string
                if (expect(TokenType::LeftBracket)) {
                    while (!isAtEnd() && peek().type != TokenType::RightBracket) {
                        node->addArchitecture(consume().str);
                        std::cout << "Added Architecture: " << node->getArchitectures().back() << "\n";
                        if (peek().type == TokenType::Comma) consume();
                    }
                    expect(TokenType::RightBracket);
                } else {
                    node->addArchitecture(consume().str);
                    std::cout << "Added Architecture: " << node->getArchitectures().back() << "\n";
                }
            }
            else if (keyType == TokenType::Entry) {
                if (peek().type == TokenType::Identifier) {
                    node->setEntryPoint(consume().str);
                    std::cout << "Entry Point: " << node->getEntryPoint() << "\n";
                } else {
                    std::cerr << "[ERROR]: Expected valid function identifier for entry point.\n";
                    return nullptr;
                }
            }
            else if (keyType == TokenType::Ruleset) {
                if (expect(TokenType::LeftBrace)) {
                    while (!isAtEnd() && peek().type != TokenType::RightBrace) {
                        auto ruleKey = consume();
                        expect(TokenType::Assign);
                        if (peek().type == TokenType::Bool || peek().type == TokenType::True || peek().type == TokenType::False) {
                            bool ruleValue = (consume().type == TokenType::True);
                            node->setRule(ruleKey.str, ruleValue);
                            std::cout << "Rule: " << ruleKey.str << " = " << (ruleValue ? "true" : "false") << "\n";
                        } else {
                            consume(); // skip invalid value
                        }
                        expect(TokenType::Semicolon);
                    }
                    expect(TokenType::RightBrace);
                }
            }
            else {
                // Skip unknown value expressions to advance safely
                if (!isAtEnd() && peek().type != TokenType::Semicolon) {
                    consume();
                }
            }

            // Consume trailing semicolon if present
            expect(TokenType::Semicolon);
        }
        expect(TokenType::RightBrace);
    }

    // Force validation result
    if (!VulpineSettings::VulpineVersions::isValidVersion(node->getVulpineVersion())) {
        std::cerr << "[ERROR]: Invalid version: " << node->getVulpineVersion() << '\n';
        return nullptr;
    }
    return node;
}

auto Parser::parseStatement() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;
    switch (peek().type) {
        case TokenType::Let:
        case TokenType::Identifier:
            return parseVariableDecl();
        case TokenType::Project:
            return parseProjectDecl();
        case TokenType::LeftParentheses:
        case TokenType::RightParentheses:
            consume();
            return parseStatement();
        default:
            return parseExpression();
    }
}

auto Parser::parseBlock() -> std::unique_ptr<BlockNode> {
    expect(TokenType::LeftBrace); 
    enterScope();                 
    auto blockNode = std::make_unique<BlockNode>();
    while (peek().type != TokenType::RightBrace && !isAtEnd()) {
        blockNode->addStatement(parseStatement());
    }
    exitScope();                  
    expect(TokenType::RightBrace); 
    return blockNode;
}

auto Parser::parseVariableDecl() -> std::unique_ptr<VariableNode> {
    bool isCStyle = (m_activeProject && m_activeProject->isRuleEnabled(allow_c_style_decl) && 
                     TokenUtils::isTypedef(peek().type) && 
                     peekNext().type == TokenType::Identifier);

    if (isCStyle) {
        TokenType deducedType = consume().type;
        std::string_view name = consume().str;
        define(name, deducedType);
        auto varNode = std::make_unique<VariableNode>(std::string(name));
        varNode->setDeclaredType(deducedType);
        if (expect(TokenType::Assign)) varNode->setValue(parseExpression());
        expect(TokenType::Semicolon);
        return varNode;
    } 
    
    if (expect(TokenType::Let)) {
        if (m_activeProject && m_activeProject->isRuleEnabled(decline_vulpine_style_decl)) {
            std::cerr << "[ERROR]: Vulpine-style 'let' is disabled." << std::endl;
            return nullptr;
        }
        if (peek().type != TokenType::Identifier) return nullptr;
        std::string_view name = consume().str;
        TokenType deducedType = TokenType::AutoWild;
        if (expect(TokenType::Colon)) {
            if (TokenUtils::isTypedef(peek().type)) deducedType = consume().type;
            else if (peek().type == TokenType::Identifier) {
                SymbolInfo* info = lookup(consume().str);
                if (info) deducedType = info->type;
            }
        }
        define(name, deducedType);
        auto varNode = std::make_unique<VariableNode>(std::string(name));
        varNode->setDeclaredType(deducedType);
        if (expect(TokenType::Assign)) varNode->setValue(parseExpression());
        expect(TokenType::Semicolon);
        return varNode;
    }
    return nullptr;
}