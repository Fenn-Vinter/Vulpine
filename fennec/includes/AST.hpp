#pragma once
#include <string>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <utility>
#include "lexicon.hpp"

enum class NodeType : unsigned int {
    Base = 0,
    Variable,
    VariableRef,
    Return,
    If,
    Function,
    Literal,
    BinaryOp,
    UnaryOp,
    Cast,
    Block,
    Project
};

class BaseNode {
public:
    BaseNode() = default;
    virtual ~BaseNode() = default;
    virtual auto nodeType() const -> NodeType { return NodeType::Base; }
    virtual auto getType() const -> TokenType { return TokenType::Invalid; }
};

class VariableNode : public BaseNode {
public:
    // Fixed: Initializing member 'm_name' with parameter 'name'
    VariableNode(std::string name) : m_name(std::move(name)) {}

    auto nodeType() const -> NodeType override { return NodeType::Variable; }
    auto getType() const -> TokenType override { return m_declaredType; }
    
    // Member accessors
    auto setName(const std::string& name) -> void { m_name = name; }
    auto getName() const -> const std::string& { return m_name; }

    // Logic accessors
    auto setValue(std::unique_ptr<BaseNode> val) -> void { m_value = std::move(val); }
    auto getValue() const -> BaseNode* { return m_value.get(); } // Fixed: was 'value'

    // Added: Required for the Parser logic
    auto setDeclaredType(TokenType type) -> void { m_declaredType = type; }
    auto getDeclaredType() const -> TokenType { return m_declaredType; }

private:
    std::string m_name;
    TokenType m_declaredType = TokenType::AutoWild;
    std::unique_ptr<BaseNode> m_value;
};

class VariableRefNode : public BaseNode {
public:
    VariableRefNode(std::string_view name, TokenType resolvedType = TokenType::Invalid)
        : m_name(name), m_resolvedType(resolvedType) {}

    auto nodeType() const -> NodeType override { return NodeType::VariableRef; }
    auto getType() const -> TokenType override { return m_resolvedType; }

    auto setName(const std::string_view& name) -> void { m_name = name; }
    auto getName() const -> const std::string_view& { return m_name; }
    auto setResolvedType(TokenType type) -> void { m_resolvedType = type; }
private:
    std::string_view m_name;
    TokenType m_resolvedType{TokenType::Invalid};
};

class IfNode : public BaseNode {
public:
    auto nodeType() const -> NodeType override { return NodeType::If; }

    auto setCondition(std::unique_ptr<BaseNode> cond) -> void { condition = std::move(cond); }
    auto getCondition() const -> BaseNode* { return condition.get(); }

    auto addBodyNode(std::unique_ptr<BaseNode> node) -> void { body.push_back(std::move(node)); }
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& { return body; }

private:
    std::unique_ptr<BaseNode> condition;
    std::vector<std::unique_ptr<BaseNode>> body;
};

class ReturnNode : public BaseNode {
public:
    auto nodeType() const -> NodeType override { return NodeType::Return; }
    auto getType() const -> TokenType override { return expr ? expr->getType() : TokenType::Invalid; }
    auto setExpression(std::unique_ptr<BaseNode> n) -> void { expr = std::move(n); }
    auto getExpression() const -> const BaseNode* { return expr.get(); }

private:
    std::unique_ptr<BaseNode> expr;
};

class FunctionNode : public BaseNode {
public:
    auto nodeType() const -> NodeType override { return NodeType::Function; }
    auto getType() const -> TokenType override { return returnType; }

    auto setName(std::string name) -> void { this->name = std::move(name); }
    auto getName() const -> const std::string& { return name; }

    auto setReturnType(TokenType type) -> void { returnType = type; }
    auto getReturnType() const -> TokenType { return returnType; }

    auto addParam(std::unique_ptr<VariableNode> param) -> void { params.push_back(std::move(param)); }
    auto addBodyNode(std::unique_ptr<BaseNode> node) -> void { body.push_back(std::move(node)); }

    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& { return body; }
    auto getParams() const -> const std::vector<std::unique_ptr<VariableNode>>& { return params; }

private:
    std::string name;
    TokenType returnType{TokenType::Invalid};
    std::vector<std::unique_ptr<VariableNode>> params;
    std::vector<std::unique_ptr<BaseNode>> body;
};

// Literal Node: Stores the literal type and raw value.
class LiteralNode : public BaseNode {
public:
    LiteralNode(TokenType literalType, std::string value)
        : type(literalType), value(std::move(value)) {}

    auto nodeType() const -> NodeType override { return NodeType::Literal; }
    auto getType() const -> TokenType override { return type; }
    auto getValue() const -> const std::string& { return value; }
    auto getLiteralType() const -> TokenType { return type; }
    auto isStringLiteral() const -> bool { return type == TokenType::String_Literal; }

private:
    TokenType type{TokenType::Invalid};
    std::string value;
};

// BinaryOpNode: Handles math (+, -, *, /)
class BinaryOpNode : public BaseNode {
public:
    BinaryOpNode(TokenType op) : op(op) {}
    auto nodeType() const -> NodeType override { return NodeType::BinaryOp; }
    auto getType() const -> TokenType override { return resultType; }
    auto getOp() const -> TokenType { return op; }
    auto getLeft() const -> const BaseNode* { return left.get(); }
    auto getRight() const -> const BaseNode* { return right.get(); }

    auto setLeft(std::unique_ptr<BaseNode> n) { left = std::move(n); }
    auto setRight(std::unique_ptr<BaseNode> n) { right = std::move(n); }
    auto setResultType(TokenType type) { resultType = type; }

private:
    TokenType op{TokenType::Invalid};
    std::unique_ptr<BaseNode> left{nullptr};
    std::unique_ptr<BaseNode> right{nullptr};
    TokenType resultType{TokenType::Invalid};
};

// UnaryOpNode: Handles pointers (&) and dereferences (*)
class UnaryOpNode : public BaseNode {
public:
    UnaryOpNode(TokenType op) : op(op) {}
    auto nodeType() const -> NodeType override { return NodeType::UnaryOp; }
    auto getType() const -> TokenType override { return operand ? operand->getType() : TokenType::Invalid; }
    auto getOp() const -> TokenType { return op; }
    auto getOperand() const -> const BaseNode* { return operand.get(); }
    auto setOperand(std::unique_ptr<BaseNode> n) { operand = std::move(n); }
    auto setIsPrefix(bool isPrefix) { this->isPrefix = isPrefix; }
    auto getIsPrefix() const -> bool { return isPrefix; }
private:
    TokenType op{TokenType::Invalid};
    std::unique_ptr<BaseNode> operand{nullptr};
    bool isPrefix{true}; // true for prefix, false for postfix
};

// CastNode: Handles type conversion
class CastNode : public BaseNode {
public:
    CastNode(TokenType targetType) : targetType(targetType) {}
    auto nodeType() const -> NodeType override { return NodeType::Cast; }
    auto getType() const -> TokenType override { return targetType; }
    
    auto setExpression(std::unique_ptr<BaseNode> n) { expr = std::move(n); }
    auto getExpression() const -> const BaseNode* { return expr.get(); }
private:
    TokenType targetType{TokenType::Invalid};
    std::unique_ptr<BaseNode> expr;
};

class BlockNode : public BaseNode {
public:
    auto nodeType() const -> NodeType override { return NodeType::Block; }
    
    // Allows the parser to add statements as it parses them
    auto addStatement(std::unique_ptr<BaseNode> node) -> void {
        body.push_back(std::move(node));
    }
    
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& {
        return body;
    }

    auto moveBody() -> std::vector<std::unique_ptr<BaseNode>> {
        return std::move(body);
    }

private:
    std::vector<std::unique_ptr<BaseNode>> body;
};

constexpr std::string_view allow_c_style_decl = "allow_c_style_decl";
constexpr std::string_view decline_vulpine_style_decl = "decline_vulpine_style_decl";
constexpr std::string_view decline_syscalls = "decline_syscalls";
constexpr std::string_view decline_inline_languages = "decline_inline_languages";
constexpr std::string_view decline_vulpine_standard_library = "decline_vulpine_standard_library";
constexpr std::string_view decline_pointer_decleration = "decline_pointer_decleration";

class ProjectNode : public BaseNode {
public:
    auto nodeType() const -> NodeType override { return NodeType::Project; }

    using strv = std::string_view;

    // Setters
    auto setProjectName(strv name) -> void { projectName = name; }
    auto setVulpineVersion(strv ver) -> void { vulpineVersion = ver; }
    auto setEntryPoint(strv entry) -> void { entryPoint = entry; }
    auto setPublisher(strv pub) -> void { publisher = pub; }
    auto setRuleset(const std::unordered_map<strv, bool>& newRules) -> void { ruleset = newRules; }
    auto setOutputType(strv type) -> void { outputType = type; }
    auto setOptimizationLevel(strv opt) -> void { optimizationLevel = opt; }
    auto addArchitecture(strv arch) -> void { architectures.push_back(arch); }

    // Ruleset Management
    auto setRule(strv ruleName, bool value) -> void { 
        ruleset[ruleName] = value; 
    }

    // Getters
    auto getProjectName() const -> strv { return projectName; }
    auto getVulpineVersion() const -> strv { return vulpineVersion; }
    auto getEntryPoint() const -> strv { return entryPoint; }
    auto getPublisher() const -> strv { return publisher; }
    auto getRuleset() const -> const std::unordered_map<strv, bool>& { return ruleset; }
    auto getOutputType() const -> strv { return outputType; }
    auto getOptimizationLevel() const -> strv { return optimizationLevel; }
    auto getArchitectures() const -> const std::vector<strv>& { return architectures; }

    auto setParent(ProjectNode* p) -> void { parent = p; }
    
    // Updated lookup with recursion
    auto isRuleEnabled(strv ruleName) const -> bool {
        auto it = ruleset.find(ruleName);
        if (it != ruleset.end()) {
            return it->second;
        }
        // Cascade to parent if rule is not defined locally
        if (parent) {
            return parent->isRuleEnabled(ruleName);
        }
        return false; // Global default
    }
private:
    ProjectNode* parent = nullptr;
    strv projectName;
    strv vulpineVersion;
    strv entryPoint;
    strv publisher;
    strv outputType;
    strv optimizationLevel;
    std::vector<strv> architectures;

    std::unordered_map<strv, bool> ruleset = {
        {allow_c_style_decl, false},
        {decline_vulpine_style_decl, false},
        {decline_syscalls, false},
        {decline_inline_languages, false},
        {decline_vulpine_standard_library, false},
        {decline_pointer_decleration, false}
    };
};
