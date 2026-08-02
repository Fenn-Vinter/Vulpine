#include <AST.hpp>
#include "lexicon.hpp"
#include <iostream>
#include <memory>
#include <parser.hpp>
#include <string_view>
#include <VulpineSettings.hpp>
#include <format>
#include <functional>

// Helper to generate a short, fast hash string from a file path
static auto hashFilePath(std::string_view path) -> std::string {
    size_t hashVal = std::hash<std::string_view>{}(path);
    return std::format("{:04X}", static_cast<uint32_t>(hashVal & 0xFFFF));
}

Parser::Parser() {
    // Scope initialization deferred until Parse() receives filepath
}

Parser::~Parser() {
    while (m_currentScope != nullptr) {
        exitScope();
    }
}

// --- Centralized Error Handling & Recovery ---

auto Parser::reportError(const TokenEntry& token, std::string_view message) -> void {
    auto formatted = std::format("[PARSER ERROR] [{}:{}:{}] {}",
                                 m_currentFileScopePrefix,
                                 token.line,
                                 token.column,
                                 message);
    std::cerr << formatted << "\n";
    m_errors.push_back(formatted);
    m_hasError = true;
}

auto Parser::getErrors() const -> const std::vector<std::string>& {
    return m_errors;
}

auto Parser::hasErrors() const -> bool {
    return !m_errors.empty();
}

auto Parser::synchronize() -> void {
    m_panicMode = false;
    
    if (isAtEnd()) return;
    consume(); // Always advance past the error-causing token once

    while (!isAtEnd()) {
        if (peek().type == TokenType::Semicolon) {
            consume();
            return;
        }

        switch (peek().type) {
            case TokenType::Project:
            case TokenType::Fn:
            case TokenType::Let:
            case TokenType::LeftBrace:
            case TokenType::RightBrace:
                return;
            default:
                consume();
        }
    }
}

// --- Main Parse Entry Point ---

auto Parser::Parse(const Tokens_t* tokens, std::string_view filepath) -> std::vector<std::unique_ptr<BaseNode>> {
    m_tokens = tokens;
    m_pos = 0;
    m_hasError = false;
    m_panicMode = false;
    m_errors.clear();

    // Seed file path prefix and reset scope tree
    m_currentFileScopePrefix = hashFilePath(filepath);
    
    while (m_currentScope != nullptr) {
        exitScope();
    }
    enterScope("global");

    std::vector<std::unique_ptr<BaseNode>> rootNodes;

    while (!isAtEnd()) {
        try {
            auto node = parseStatement();
            if (node) {
                rootNodes.push_back(std::move(node));
            } else if (!m_panicMode && !isAtEnd()) {
                reportError(peek(), std::format("Unexpected token '{}'", peek().str));
                synchronize();
            }
        } catch (const std::exception& e) {
            reportError(isAtEnd() ? (*m_tokens)[m_pos - 1] : peek(), e.what());
            synchronize();
        }
    }

    return rootNodes;
}

auto Parser::isAtEnd() const -> bool {
    return m_pos >= m_tokens->size();
}

auto Parser::peek() const -> const TokenEntry& {
    if (m_pos >= m_tokens->size()) {
        static TokenEntry eofToken{TokenType::Eof, "", 0, 0};
        return eofToken;
    }
    return (*m_tokens)[m_pos];
}

auto Parser::peekNext() const -> const TokenEntry& {
    if (m_pos + 1 >= m_tokens->size()) return peek();
    return (*m_tokens)[m_pos + 1];
}

auto Parser::consume() -> const TokenEntry& {
    if (!isAtEnd()) return (*m_tokens)[m_pos++];
    return peek();
}

auto Parser::expect(TokenType type) -> bool {
    if (!isAtEnd() && peek().type == type) {
        consume();
        return true;
    }

    auto it = TokenToString.find(type);
    std::string_view typeName = (it != TokenToString.end()) ? it->second : "Unknown";
    
    reportError(peek(), std::format("Expected token type '{}', got '{}'", 
                                   typeName, 
                                   peek().str));
    m_panicMode = true;
    return false;
}

// --- Dynamic Path Scope Management ---

auto Parser::enterScope(std::string_view scopeName) -> void {
    std::string newPath;
    if (m_currentScope == nullptr) {
        newPath = std::format("{}::{}", m_currentFileScopePrefix, scopeName);
    } else {
        newPath = std::format("{}::{}", m_currentScope->scopePath, scopeName);
    }

    m_currentScope = new Scope{
        .scopePath = std::move(newPath),
        .anonymousBlockCount = 0,
        .symbols = {},
        .parent = m_currentScope
    };
}

auto Parser::enterAnonymousScope() -> void {
    size_t blockIdx = 0;
    if (m_currentScope != nullptr) {
        blockIdx = m_currentScope->anonymousBlockCount++;
    }
    enterScope(std::to_string(blockIdx));
}

auto Parser::exitScope() -> void {
    if (m_currentScope == nullptr) return;
    Scope* old = m_currentScope;
    m_currentScope = m_currentScope->parent;
    delete old;
}

// --- Symbol Definition & Lookup ---

auto Parser::define(std::string_view name, TokenType type) -> SymbolInfo& {
    SymbolInfo* existing = lookup(name);
    if (existing != nullptr) {
        reportError(peek(), std::format("Redeclaration error: Symbol '{}' already exists at scope '{}'", name, existing->fqn));
        m_panicMode = true;
        return *existing;
    }

    std::string fqn = std::format("{}::{}", m_currentScope->scopePath, name);

    SymbolInfo info{
        .name = std::string(name),
        .fqn = std::move(fqn),
        .type = type,
        .fileId = m_currentFileId
    };

    auto [it, inserted] = m_currentScope->symbols.emplace(name, std::move(info));
    return it->second;
}

auto Parser::lookup(std::string_view name) -> SymbolInfo* {
    Scope* search = m_currentScope;
    while (search != nullptr) {
        auto it = search->symbols.find(name);
        if (it != search->symbols.end()) {
            return &it->second;
        }
        search = search->parent;
    }
    return nullptr;
}

// --- Grammar Parsers ---

// recursive descent parsing function to parse an expression while respecting operator precedence and associativity
auto Parser::parseExpression() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;

    auto left = parsePrimary();
    left = parsePostfixCast(std::move(left));

    if (peek().type == TokenType::Semicolon || peek().type == TokenType::Eof) return left;

    if ((TokenUtils::isOperator(peek().type) || TokenUtils::isComparativeOperator(peek().type)) && peek().type != TokenType::Assign) {
        auto opToken = consume();
        auto right = parseExpression();

        const BaseNode* leftRaw = left.get();
        const BaseNode* rightRaw = right.get();
        auto binOpNode = std::make_unique<BinaryOpNode>(opToken.type);
        binOpNode->setLeft(std::move(left));
        binOpNode->setRight(std::move(right));

        TokenType resultType = deriveBinaryResultType(leftRaw, rightRaw, opToken.type);
        if (resultType == TokenType::Invalid) {
            reportError(opToken, std::format("Invalid operator '{}' for operand types", opToken.str));
        }
        binOpNode->setResultType(resultType);
        return binOpNode;
    }

    return left;
}


auto Parser::parsePrimary() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;

    const TokenEntry& token = peek();
    
    if (token.type == TokenType::Identifier) {
        consume();
        auto varRef = std::make_unique<VariableRefNode>(token.str);
        if (auto symbol = lookup(token.str); symbol) {
            varRef->setResolvedType(symbol->type);
        }
        return varRef;
    } else if (TokenUtils::isLiteral(token.type)) {
        consume();
        std::string rawValue = std::string(token.str);
        if (token.type == TokenType::String_Literal && rawValue.size() >= 2 && rawValue.front() == '"' && rawValue.back() == '"') {
            rawValue = rawValue.substr(1, rawValue.size() - 2);
        }

        TokenType literalType = token.type;
        switch (token.type) {
            case TokenType::Int_Literal:
                literalType = TokenType::Int;
                break;
            case TokenType::UInt_Literal:
                literalType = TokenType::UInt;
                break;
            case TokenType::Float_Literal:
                literalType = TokenType::Float;
                break;
            case TokenType::Char_Literal:
                literalType = TokenType::Char;
                break;
            case TokenType::String_Literal:
                literalType = TokenType::String;
                break;
            default:
                break;
        }

        return std::make_unique<LiteralNode>(literalType, std::move(rawValue));
    } else if (TokenUtils::isUnaryOperator(token.type)) {
        consume();
        auto unaryNode = std::make_unique<UnaryOpNode>(token.type);
        unaryNode->setOperand(parsePrimary());
        return unaryNode;
    } else if (token.type == TokenType::LeftParentheses) {
        consume(); // consume '('
        auto expr = parseExpression();
        expect(TokenType::RightParentheses); // expect ')'
        return parsePostfixCast(std::move(expr));
    }

    reportError(token, std::format("Unexpected token '{}' in expression", token.str));
    m_panicMode = true;
    return nullptr;
}

auto Parser::parsePostfixCast(std::unique_ptr<BaseNode> expr) -> std::unique_ptr<BaseNode> {
    if (peek().type == TokenType::Colon) {
        consume();
        if (TokenUtils::isTypedef(peek().type)) {
            TokenType castType = peek().type;
            consume();
            if (!canCast(expr->getType(), castType)) {
                reportError(peek(), std::format("Illegal cast from {} to {}", TokenToString.at(expr->getType()), TokenToString.at(castType)));
            }
            auto castNode = std::make_unique<CastNode>(castType);
            castNode->setExpression(std::move(expr));
            return castNode;
        }
        reportError(peek(), "Expected type after ':' cast operator.");
    }
    return expr;
}

auto Parser::deriveBinaryResultType(const BaseNode* left, const BaseNode* right, TokenType op) -> TokenType {
    TokenType leftType = left ? left->getType() : TokenType::Invalid;
    TokenType rightType = right ? right->getType() : TokenType::Invalid;

    if (leftType == TokenType::Invalid || rightType == TokenType::Invalid) {
        return TokenType::Invalid;
    }

    if (TokenUtils::isComparativeOperator(op)) {
        if (leftType == rightType && 
            (TokenUtils::isNumericType(leftType) || leftType == TokenType::String_Literal || leftType == TokenType::Bool)) {
            return TokenType::Bool;
        }
        if (TokenUtils::isNumericType(leftType) && TokenUtils::isNumericType(rightType)) {
            return TokenType::Bool;
        }
        return TokenType::Invalid;
    }

    if (TokenUtils::isOperator(op)) {
        if (leftType == rightType) {
            return leftType;
        }
        if (TokenUtils::isNumericType(leftType) && TokenUtils::isNumericType(rightType)) {
            int leftWidth = TokenUtils::getTypeWidth(leftType);
            int rightWidth = TokenUtils::getTypeWidth(rightType);
            if (leftWidth < 0 || rightWidth < 0) return TokenType::Invalid;
            return (leftWidth >= rightWidth) ? leftType : rightType;
        }
    }

    return TokenType::Invalid;
}

auto Parser::canCast(TokenType src, TokenType dst) -> bool {
    if (src == dst) return true;
    if (!TokenUtils::isNumericType(src) || !TokenUtils::isNumericType(dst)) return false;

    int srcWidth = TokenUtils::getTypeWidth(src);
    int dstWidth = TokenUtils::getTypeWidth(dst);
    if (srcWidth < 0 || dstWidth < 0) return false;
    return dstWidth <= srcWidth;
}

auto Parser::parseProjectDecl() -> std::unique_ptr<ProjectNode> {
    consume(); // consume 'project'
    auto node = std::make_unique<ProjectNode>();

    if (peek().type == TokenType::Identifier) {
        node->setProjectName(consume().str);
    } else {
        reportError(peek(), "Expected project identifier after 'project' keyword.");
        return nullptr;
    }

    if (expect(TokenType::LeftBrace)) {
        while (!isAtEnd() && peek().type != TokenType::RightBrace) {
            TokenType keyType = peek().type;
            std::string_view keyStr = peek().str;
            consume(); // consume key token

            // Special case: 'ruleset' block does NOT use '=' (ruleset { ... })
            if (keyType == TokenType::Ruleset || keyStr == "ruleset") {
                if (expect(TokenType::LeftBrace)) {
                    while (!isAtEnd() && peek().type != TokenType::RightBrace) {
                        std::string_view ruleKeyStr = consume().str; // grab rule name (e.g. "allow_c_style_decl")
                        
                        if (!expect(TokenType::Assign)) {
                            synchronize();
                            continue;
                        }
                        
                        if (peek().type == TokenType::Bool || peek().type == TokenType::True || peek().type == TokenType::False || peek().str == "true" || peek().str == "false") {
                            bool ruleValue = (peek().type == TokenType::True || peek().str == "true");
                            consume();
                            node->setRule(ruleKeyStr, ruleValue);
                        } else {
                            reportError(peek(), "Expected boolean value for rule definition.");
                            consume(); 
                        }
                        expect(TokenType::Semicolon);
                    }
                    expect(TokenType::RightBrace);
                }
                continue; // Skip the semicolon check at the end of key-value pairs
            }

            // All standard key/value settings require '='
            if (!expect(TokenType::Assign)) {
                synchronize();
                continue;
            }
            
            if (keyType == TokenType::Version || keyStr == "version") {
                if (peek().type == TokenType::String_Literal || peek().type == TokenType::Identifier) {
                    std::string_view rawStr = consume().str;
                    if (rawStr.size() >= 2 && rawStr.front() == '"' && rawStr.back() == '"') {
                        rawStr = rawStr.substr(1, rawStr.size() - 2);
                    }
                    node->setVulpineVersion(VulpineSettings::VulpineVersions::resolveVersionAlias(rawStr));
                } else {
                    reportError(peek(), "Expected version string literal or alias for 'version' key.");
                    return nullptr;
                }
            }
            else if (keyType == TokenType::Arch || keyStr == "arch") {
                if (peek().type == TokenType::LeftBracket) {
                    consume(); // Consume '['
                    while (!isAtEnd() && peek().type != TokenType::RightBracket) {
                        node->addArchitecture(consume().str);
                        if (peek().type == TokenType::Comma) consume();
                    }
                    expect(TokenType::RightBracket);
                } else {
                    node->addArchitecture(consume().str);
                }
            }
            else if (keyType == TokenType::Entry || keyStr == "entry") {
                if (peek().type == TokenType::Identifier) {
                    node->setEntryPoint(consume().str);
                } else {
                    reportError(peek(), "Expected valid function identifier for project entry point.");
                    return nullptr;
                }
            }
            else if (keyType == TokenType::Format || keyStr == "format") {
                if (peek().type == TokenType::String_Literal || peek().type == TokenType::Identifier) {
                    consume();
                } else {
                    reportError(peek(), "Expected string literal or identifier for 'format' key.");
                }
            }
            else {
                reportError(peek(), std::format("Unknown project configuration key '{}'", keyStr));
                if (!isAtEnd() && peek().type != TokenType::Semicolon) {
                    consume();
                }
            }

            expect(TokenType::Semicolon);
        }
        expect(TokenType::RightBrace);
    }

    if (!VulpineSettings::VulpineVersions::isValidVersion(node->getVulpineVersion())) {
        reportError(peek(), std::format("Invalid or unsupported Vulpine version: '{}'", node->getVulpineVersion()));
        return nullptr;
    }

    // Bind project instance pointer so variable rules function properly
    m_activeProject = node.get();

    return node;
}

auto Parser::parseStatement() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;
    
    if (peek().type == TokenType::Return) {
        const TokenEntry& returnToken = peek();
        consume(); // eat 'return'
        auto expr = parseExpression(); // gather everything up to the semicolon
        expect(TokenType::Semicolon); // eat ';'

        if (expr && m_currentFunctionReturnType != TokenType::Invalid) {
            TokenType exprType = expr->getType();
            bool validReturn = (exprType == m_currentFunctionReturnType);
            if (!validReturn && exprType == TokenType::Nullptr) {
                validReturn = (m_currentFunctionReturnType == TokenType::Nullptr || TokenUtils::isWildcard(m_currentFunctionReturnType));
            }
            if (!validReturn) {
                reportError(returnToken, std::format(
                    "Return expression type '{}' does not match function return type '{}'.",
                    TokenToString.at(exprType), TokenToString.at(m_currentFunctionReturnType)));
            }
        }

        auto returnNode = std::make_unique<ReturnNode>();
        returnNode->setExpression(std::move(expr));
        return returnNode;
    }

    switch (peek().type) {
        case TokenType::Fn:
            return parseFunctionDecl();
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
    if (!expect(TokenType::LeftBrace)) return nullptr;
    
    enterAnonymousScope();                
    auto blockNode = std::make_unique<BlockNode>();
    
    while (peek().type != TokenType::RightBrace && !isAtEnd()) {
        auto stmt = parseStatement();
        if (stmt) {
            blockNode->addStatement(std::move(stmt));
        } else if (m_panicMode) {
            synchronize();
        }
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

        SymbolInfo* check = lookup(name);
        if (check != nullptr) {
            reportError(peek(), std::format("Variable collision: '{}' already declared in scope path '{}'", name, check->fqn));
            return nullptr;
        }

        define(name, deducedType);
        auto varNode = std::make_unique<VariableNode>(std::string(name));
        varNode->setDeclaredType(deducedType);
        if (expect(TokenType::Assign)) varNode->setValue(parseExpression());
        expect(TokenType::Semicolon);
        return varNode;
    } 
    
    if (expect(TokenType::Let)) {
        if (m_activeProject && m_activeProject->isRuleEnabled(decline_vulpine_style_decl)) {
            reportError(peek(), "Vulpine-style 'let' declarations are disabled by project ruleset.");
            return nullptr;
        }
        
        if (peek().type != TokenType::Identifier) {
            reportError(peek(), "Expected identifier after 'let' keyword.");
            return nullptr;
        }
        
        std::string_view name = consume().str;

        SymbolInfo* check = lookup(name);
        if (check != nullptr) {
            reportError(peek(), std::format("Variable collision: '{}' already declared in scope path '{}'", name, check->fqn));
            return nullptr;
        }

        TokenType deducedType = TokenType::AutoWild;
        if (expect(TokenType::Colon)) {
            if (TokenUtils::isTypedef(peek().type)) {
                deducedType = consume().type;
            } else if (peek().type == TokenType::Identifier) {
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
    
    reportError(peek(), std::format("Unexpected statement starting with '{}'", peek().str));
    consume();
    return nullptr;
}

auto Parser::parseFunctionDecl() -> std::unique_ptr<BaseNode> {
    consume(); // Consume 'fn'

    if (peek().type != TokenType::Identifier) {
        reportError(peek(), "Expected function name after 'fn'.");
        return nullptr;
    }

    std::string_view fnName = consume().str;
    define(fnName, TokenType::Fn);

    auto funcNode = std::make_unique<FunctionNode>();
    funcNode->setName(std::string(fnName));

    // Expect parameter list ()
    if (!expect(TokenType::LeftParentheses)) return nullptr;
    
    while (!isAtEnd() && peek().type != TokenType::RightParentheses) {
        if (peek().type == TokenType::Identifier) {
            std::string_view paramName = consume().str;
            TokenType paramType = TokenType::AutoWild;
            if (expect(TokenType::Colon)) {
                if (TokenUtils::isTypedef(peek().type)) {
                    paramType = consume().type;
                } else {
                    reportError(peek(), "Expected type after parameter ':'");
                }
            }

            define(paramName, paramType);
            auto paramNode = std::make_unique<VariableNode>(std::string(paramName));
            paramNode->setDeclaredType(paramType);
            funcNode->addParam(std::move(paramNode));
        }

        if (peek().type == TokenType::Comma) {
            consume();
            continue;
        }

        if (peek().type != TokenType::RightParentheses) {
            break;
        }
    }

    if (!expect(TokenType::RightParentheses)) return nullptr;

    if (peek().type == TokenType::Colon) {
        consume();
        if (peek().type == TokenType::Nullptr) {
            funcNode->setReturnType(consume().type);
        } else if (TokenUtils::isTypedef(peek().type)) {
            funcNode->setReturnType(consume().type);
        } else {
            reportError(peek(), "Expected return type after function ':'");
        }
    }

    TokenType previousReturnType = m_currentFunctionReturnType;
    m_currentFunctionReturnType = funcNode->getReturnType();

    // Parse function body block { ... }
    if (peek().type == TokenType::LeftBrace) {
        auto body = parseBlock();
        if (body) {
            for (auto& stmt : body->moveBody()) {
                funcNode->addBodyNode(std::move(stmt));
            }
        }
        m_currentFunctionReturnType = previousReturnType;
        return funcNode;
    }

    m_currentFunctionReturnType = previousReturnType;

    reportError(peek(), "Expected '{' to start function body.");
    return nullptr;
}