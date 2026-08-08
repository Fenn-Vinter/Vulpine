#include <AST.hpp>
#include "lexicon.hpp"
#include <iostream>
#include <memory>
#include <parser.hpp>
#include <string_view>
#include <VulpineSettings.hpp>
#include <format>
#include <functional>
#include <JsonRPC/JsonRPC.hpp>

static auto hashFilePath(std::string_view path) -> std::string {
    size_t hashVal = std::hash<std::string_view>{}(path);
    return std::format("{:04X}", static_cast<uint32_t>(hashVal & 0xFFFF));
}

Parser::Parser() {}

Parser::~Parser() {
    while (m_currentScope != nullptr) {
        exitScope();
    }
}

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

auto Parser::reportWarning(const TokenEntry& token, std::string_view message) -> void {
    auto formatted = std::format("[PARSER WARNING] [{}:{}:{}] {}",
                                 m_currentFileScopePrefix,
                                 token.line,
                                 token.column,
                                 message);
    std::cerr << formatted << "\n";
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
    consume();

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

auto Parser::Parse(const Tokens_t* tokens, std::string_view filepath) -> std::vector<std::unique_ptr<BaseNode>> {
    m_errors.clear();
    m_tokens = tokens;
    m_pos = 0;
    m_hasError = false;
    m_panicMode = false;
    m_currentFilePath = filepath;
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

    reportError(peek(), std::format("Expected token type '{}', got '{}'", typeName, peek().str));
    m_panicMode = true;
    return false;
}

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

auto Parser::define(std::string_view name, TokenType type, bool initialized) -> SymbolInfo& {
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
        .fileId = m_currentFileId,
        .isInitialized = initialized
    };

    auto [it, inserted] = m_currentScope->symbols.emplace(name, std::move(info));
    return it->second;
}

auto Parser::lookup(std::string_view name) -> SymbolInfo* {
    Scope* search = m_currentScope;
    while (search != nullptr) {
        auto it = search->symbols.find(std::string(name));
        if (it != search->symbols.end()) {
            return &it->second;
        }
        search = search->parent;
    }
    return nullptr;
}

auto Parser::parsePostfixAccess(std::unique_ptr<BaseNode> expr) -> std::unique_ptr<BaseNode> {
    while (peek().type == TokenType::LeftBracket) {
        consume();
        auto indexExpr = parseExpression();
        expect(TokenType::RightBracket);

        auto node = std::make_unique<IndexAccessNode>();
        node->setTarget(std::move(expr));
        node->setIndex(std::move(indexExpr));
        expr = std::move(node);
    }
    return expr;
}

auto Parser::parseExpression() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;

    auto left = parsePostfixAccess(parsePrimary());
    left = parsePostfixCast(std::move(left));

    if (!left) return nullptr;

    if (peek().type == TokenType::Semicolon ||
        peek().type == TokenType::Comma ||
        peek().type == TokenType::RightParentheses ||
        peek().type == TokenType::RightBrace ||
        peek().type == TokenType::RightBracket ||
        peek().type == TokenType::Eof) {
        return left;
    }

    if (peek().type == TokenType::Assign) {
        auto opToken = consume();
        auto right = parseExpression();

        if (auto* varRef = dynamic_cast<VariableRefNode*>(left.get())) {
            if (auto symbol = lookup(varRef->getName())) {
                symbol->isInitialized = true;
            }
        } else if (auto* indexAccess = dynamic_cast<IndexAccessNode*>(left.get())) {
            if (auto* varRef = dynamic_cast<VariableRefNode*>(indexAccess->getTarget())) {
                if (auto symbol = lookup(varRef->getName())) {
                    symbol->isInitialized = true;
                }
            }
        }

        auto binOpNode = std::make_unique<BinaryOpNode>(opToken.type);
        binOpNode->setLeft(std::move(left));
        binOpNode->setRight(std::move(right));
        return binOpNode;
    }

    if ((TokenUtils::isOperator(peek().type) || TokenUtils::isComparativeOperator(peek().type)) && peek().type != TokenType::NOT) {
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

auto Parser::parseArrayLiteral() -> std::unique_ptr<ArrayNode> {
    consume(); // Consume '['
    auto arrayNode = std::make_unique<ArrayNode>();

    if (peek().type != TokenType::RightBracket) {
        while (!isAtEnd()) {
            auto elem = parseExpression();
            if (elem) {
                arrayNode->addValue(std::move(elem));
            }

            if (peek().type == TokenType::Comma) {
                consume();
                continue;
            }
            break;
        }
    }

    expect(TokenType::RightBracket);
    return arrayNode;
}

auto Parser::parsePrimary() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;

    const TokenEntry& token = peek();

    if (token.type == TokenType::LeftBracket) {
        return parseArrayLiteral();
    }

    if (token.type == TokenType::Address || TokenUtils::isUnaryOperator(token.type)) {
        consume();
        auto unaryNode = std::make_unique<UnaryOpNode>(token.type);
        unaryNode->setOperand(parsePrimary());
        return unaryNode;
    }

    if (token.type == TokenType::Identifier) {
        consume();
        auto varRef = std::make_unique<VariableRefNode>(token.str);
        if (auto symbol = lookup(token.str)) {
            varRef->setResolvedType(symbol->type);
            if (!symbol->isInitialized) {
                reportError(token, std::format("Use of uninitialized variable '{}'", token.str));
            }
        }
        if (peek().type == TokenType::LeftParentheses) {
            return parseCall(std::move(varRef));
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
            default:
                break;
        }

        return std::make_unique<LiteralNode>(literalType, std::move(rawValue));
    } else if (token.type == TokenType::LeftParentheses) {
        consume();
        auto expr = parseExpression();
        expect(TokenType::RightParentheses);
        return parsePostfixCast(std::move(expr));
    }

    reportError(token, std::format("Unexpected token '{}' in expression", token.str));
    m_panicMode = true;
    return nullptr;
}

auto Parser::parseCall(std::unique_ptr<BaseNode> callee) -> std::unique_ptr<BaseNode> {
    if (!callee) return nullptr;
    if (!expect(TokenType::LeftParentheses)) return callee;

    auto* varRef = dynamic_cast<VariableRefNode*>(callee.get());
    if (!varRef) {
        reportError(peek(), "Call target must be an identifier.");
        return callee;
    }

    auto callNode = std::make_unique<CallNode>(std::string(varRef->getName()));
    if (auto symbol = lookup(varRef->getName())) {
        callNode->setReturnType(symbol->type);
    }

    if (peek().type != TokenType::RightParentheses) {
        while (!isAtEnd()) {
            auto arg = parseExpression();
            if (!arg) break;
            callNode->addArgument(std::move(arg));

            if (peek().type == TokenType::Comma) {
                consume();
                continue;
            }
            break;
        }
    }

    expect(TokenType::RightParentheses);
    return callNode;
}

auto Parser::parsePostfixCast(std::unique_ptr<BaseNode> expr) -> std::unique_ptr<BaseNode> {
    if (peek().type == TokenType::Colon) {
        consume();
        if (TokenUtils::isTypedef(peek().type)) {
            TokenType castType = peek().type;
            consume();
            if (expr && !canCast(expr->getType(), castType)) {
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
        switch (op) {
            case TokenType::Assign:
            case TokenType::Add_Assign:
            case TokenType::Subtract_Assign:
            case TokenType::Multiply_Assign:
            case TokenType::Divide_Assign:
            case TokenType::Modulo_Assign:
                return leftType;
            default:
                break;
        }
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
    if (src == TokenType::Nullptr || dst == TokenType::Nullptr) return true;

    bool srcIsNumeric = TokenUtils::isNumericType(src) || TokenUtils::isWildcard(src);
    bool dstIsNumeric = TokenUtils::isNumericType(dst) || TokenUtils::isWildcard(dst);
    if (srcIsNumeric && dstIsNumeric) {
        int srcWidth = TokenUtils::getTypeWidth(src);
        int dstWidth = TokenUtils::getTypeWidth(dst);
        if (srcWidth < 0 || dstWidth < 0) return false;
        return dstWidth <= srcWidth;
    }

    if ((src == TokenType::Nullptr || TokenUtils::isPointerType(src)) && dstIsNumeric) return true;
    if ((dst == TokenType::Nullptr || TokenUtils::isPointerType(dst)) && srcIsNumeric) return true;
    if ((src == TokenType::Nullptr || TokenUtils::isPointerType(src)) && (dst == TokenType::Nullptr || TokenUtils::isPointerType(dst))) return true;

    return false;
}

auto Parser::parseProjectDecl() -> std::unique_ptr<ProjectNode> {
    consume();
    auto node = std::make_unique<ProjectNode>();

    if (peek().type == TokenType::Identifier) {
        node->setProjectName(consume().str);
    } else {
        reportError(peek(), "Expected project identifier after 'project' keyword.");
        return nullptr;
    }

    if (expect(TokenType::LeftBrace)) {
        while (!isAtEnd() && peek().type != TokenType::RightBrace) {
            const TokenEntry& keyToken = peek();
            TokenType keyType = keyToken.type;
            std::string_view keyStr = keyToken.str;
            consume();

            if (keyType == TokenType::Ruleset || keyStr == "ruleset") {
                if (expect(TokenType::LeftBrace)) {
                    while (!isAtEnd() && peek().type != TokenType::RightBrace) {
                        std::string_view ruleKeyStr = consume().str;

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
                continue;
            }

            if (keyType == TokenType::Version || keyStr == "version") {
                if (expect(TokenType::Assign)) {
                    if (peek().type == TokenType::String_Literal || peek().type == TokenType::Identifier) {
                        std::string_view rawStr = consume().str;
                        if (rawStr.size() >= 2 && rawStr.front() == '"' && rawStr.back() == '"') {
                            rawStr = rawStr.substr(1, rawStr.size() - 2);
                        }
                        node->setVulpineVersion(VulpineSettings::VulpineVersions::resolveVersionAlias(rawStr));
                    } else {
                        reportError(peek(), "Expected version string literal or alias for 'version' key.");
                    }
                }
            } else if (keyType == TokenType::Arch || keyStr == "arch") {
                if (expect(TokenType::Assign)) {
                    if (peek().type == TokenType::LeftBracket) {
                        consume();
                        while (!isAtEnd() && peek().type != TokenType::RightBracket) {
                            std::string_view archVal = consume().str;
                            if (archVal.size() >= 2 && archVal.front() == '"' && archVal.back() == '"') {
                                archVal = archVal.substr(1, archVal.size() - 2);
                            }
                            node->addArchitecture(archVal);
                            if (peek().type == TokenType::Comma) consume();
                        }
                        expect(TokenType::RightBracket);
                    } else {
                        std::string_view archVal = consume().str;
                        if (archVal.size() >= 2 && archVal.front() == '"' && archVal.back() == '"') {
                            archVal = archVal.substr(1, archVal.size() - 2);
                        }
                        node->addArchitecture(archVal);
                    }
                }
            } else if (keyType == TokenType::Entry || keyStr == "entry") {
                if (expect(TokenType::Assign)) {
                    if (peek().type == TokenType::Identifier) {
                        node->setEntryPoint(consume().str);
                    } else {
                        reportError(peek(), "Expected valid function identifier for project entry point.");
                    }
                }
            } else if (keyType == TokenType::Format || keyStr == "format") {
                if (expect(TokenType::Assign)) {
                    if (peek().type == TokenType::String_Literal || peek().type == TokenType::Identifier) {
                        std::string_view formatValue = consume().str;
                        if (formatValue.size() >= 2 && formatValue.front() == '"' && formatValue.back() == '"') {
                            formatValue = formatValue.substr(1, formatValue.size() - 2);
                        }
                        node->setOutputType(formatValue);
                    } else {
                        reportError(peek(), "Expected string literal or identifier for 'format' key.");
                    }
                }
            } else {
                reportError(keyToken, std::format("Unknown project configuration key '{}'", keyStr));
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
    }

    m_activeProject = node.get();
    return node;
}

auto Parser::parseStatement() -> std::unique_ptr<BaseNode> {
    if (isAtEnd()) return nullptr;

    if (peek().type == TokenType::Return) {
        const TokenEntry& returnToken = peek();
        consume();
        auto expr = parseExpression();
        expect(TokenType::Semicolon);

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

    if (peek().type == TokenType::If) {
        return parseIfStatement();
    }

    if (peek().type == TokenType::Let ||
        (m_activeProject && m_activeProject->isRuleEnabled(allow_c_style_decl) && TokenUtils::isTypedef(peek().type))) {
        return parseVariableDecl();
    }

    switch (peek().type) {
        case TokenType::Fn:
            return parseFunctionDecl();
        case TokenType::Project:
            return parseProjectDecl();
        case TokenType::LeftParentheses:
        case TokenType::RightParentheses:
            consume();
            return parseStatement();
        default: {
            auto expr = parseExpression();
            expect(TokenType::Semicolon);
            return expr;
        }
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

auto Parser::parseTypeSpecifier(TokenType& outElementType, bool& outIsArray, size_t& outArraySize) -> bool {
    outIsArray = false;
    outArraySize = 0;

    if (TokenUtils::isTypedef(peek().type)) {
        outElementType = consume().type;
    } else if (peek().type == TokenType::Identifier) {
        SymbolInfo* info = lookup(consume().str);
        if (info) outElementType = info->type;
        else outElementType = TokenType::AutoWild;
    } else {
        return false;
    }

    if (peek().type == TokenType::LeftBracket) {
        consume();
        outIsArray = true;
        if (peek().type == TokenType::Int_Literal) {
            outArraySize = std::stoull(std::string(consume().str));
        }
        expect(TokenType::RightBracket);
    }

    return true;
}

auto Parser::parseVariableDecl() -> std::unique_ptr<BaseNode> {
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

        bool hasInitializer = (peek().type == TokenType::Assign);
        define(name, deducedType, hasInitializer);

        auto varNode = std::make_unique<VariableNode>(std::string(name));
        varNode->setDeclaredType(deducedType);

        if (hasInitializer) {
            consume();
            varNode->setValue(parseExpression());
        }

        expect(TokenType::Semicolon);
        return varNode;
    }

    if (expect(TokenType::Let)) {
        const TokenEntry& letToken = peek();
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

        TokenType elementType = TokenType::AutoWild;
        bool isArray = false;
        size_t arraySize = 0;
        bool hasTypeAnnotation = false;

        if (peek().type == TokenType::Colon) {
            consume();
            hasTypeAnnotation = true;
            if (!parseTypeSpecifier(elementType, isArray, arraySize)) {
                reportError(peek(), "Expected valid type after ':' in variable declaration.");
            }
        }

        std::unique_ptr<BaseNode> initializerNode = nullptr;

        if (peek().type == TokenType::LeftBrace) {
            consume();
            auto initExpr = parseExpression();
            expect(TokenType::RightBrace);
            initializerNode = std::move(initExpr);
        } else if (peek().type == TokenType::Assign) {
            consume();
            initializerNode = parseExpression();
        }

        if (hasTypeAnnotation && initializerNode) {
            bool valueIsArrayNode = (dynamic_cast<ArrayNode*>(initializerNode.get()) != nullptr);
            if (!isArray && valueIsArrayNode) {
                reportWarning(letToken, std::format("Ambiguity in declaration for '{}': Type specifies single scalar '{}' but assigned an array initializer.", name, TokenToString.at(elementType)));
            }
        }

        bool isInitialized = (initializerNode != nullptr);
        define(name, elementType, isInitialized);

        if (isArray) {
            auto arrNode = std::make_unique<ArrayNode>();
            arrNode->setArrayName(std::string(name));
            arrNode->setDeclaredType(elementType);
            arrNode->setElementCount(arraySize);

            if (initializerNode) {
                if (auto* parsedArr = dynamic_cast<ArrayNode*>(initializerNode.get())) {
                    for (auto& val : parsedArr->moveValues()) {
                        arrNode->addValue(std::move(val));
                    }
                } else {
                    arrNode->addValue(std::move(initializerNode));
                }
            }

            expect(TokenType::Semicolon);
            return arrNode;
        }

        auto varNode = std::make_unique<VariableNode>(std::string(name));
        varNode->setDeclaredType(elementType);
        if (initializerNode) {
            varNode->setValue(std::move(initializerNode));
        }

        expect(TokenType::Semicolon);
        return varNode;
    }

    reportError(peek(), std::format("Unexpected statement starting with '{}'", peek().str));
    consume();
    return nullptr;
}

auto Parser::parseIfStatement() -> std::unique_ptr<BaseNode> {
    consume();

    if (!expect(TokenType::LeftParentheses)) {
        return nullptr;
    }

    auto condition = parseExpression();
    if (!expect(TokenType::RightParentheses)) {
        return nullptr;
    }

    auto ifNode = std::make_unique<IfNode>();
    ifNode->setCondition(std::move(condition));

    if (peek().type == TokenType::LeftBrace) {
        auto bodyNode = parseBlock();
        if (bodyNode) {
            for (auto& stmt : bodyNode->moveBody()) {
                ifNode->addBodyNode(std::move(stmt));
            }
        }
    } else {
        auto stmt = parseStatement();
        if (stmt) {
            ifNode->addBodyNode(std::move(stmt));
        }
    }

    if (peek().type == TokenType::Else) {
        consume();
        if (peek().type == TokenType::If) {
            auto elseIfNode = parseIfStatement();
            if (elseIfNode) {
                ifNode->setElseBranch(std::move(elseIfNode));
            }
        } else if (peek().type == TokenType::LeftBrace) {
            auto elseBlock = parseBlock();
            if (elseBlock) {
                ifNode->setElseBranch(std::move(elseBlock));
            }
        } else {
            auto elseStmt = parseStatement();
            if (elseStmt) {
                ifNode->setElseBranch(std::move(elseStmt));
            }
        }
    }

    return ifNode;
}

auto Parser::parseFunctionDecl() -> std::unique_ptr<BaseNode> {
    consume();

    if (peek().type != TokenType::Identifier) {
        reportError(peek(), "Expected function name after 'fn'.");
        return nullptr;
    }

    std::string_view fnName = consume().str;
    define(fnName, TokenType::Fn, true);

    auto funcNode = std::make_unique<FunctionNode>();
    funcNode->setName(std::string(fnName));
    enterScope(fnName);

    if (!expect(TokenType::LeftParentheses)) {
        exitScope();
        return nullptr;
    }

    while (!isAtEnd() && peek().type != TokenType::RightParentheses) {
        bool paramCopy = false;
        bool paramConst = false;
        if (peek().type == TokenType::Let || peek().type == TokenType::Const) {
            paramCopy = true;
            paramConst = peek().type == TokenType::Const;
            consume();
        }

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

            define(paramName, paramType, true);
            auto paramNode = std::make_unique<VariableNode>(std::string(paramName));
            paramNode->setDeclaredType(paramType);
            paramNode->setParameterCopy(paramCopy);
            paramNode->setConst(paramConst);
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

    if (auto symbol = lookup(fnName)) {
        symbol->type = funcNode->getReturnType();
    }

    TokenType previousReturnType = m_currentFunctionReturnType;
    m_currentFunctionReturnType = funcNode->getReturnType();

    if (peek().type == TokenType::LeftBrace) {
        auto body = parseBlock();
        if (body) {
            for (auto& stmt : body->moveBody()) {
                funcNode->addBodyNode(std::move(stmt));
            }
        }
        exitScope();
        m_currentFunctionReturnType = previousReturnType;
        return funcNode;
    }

    exitScope();
    m_currentFunctionReturnType = previousReturnType;

    reportError(peek(), "Expected '{' to start function body.");
    return nullptr;
}