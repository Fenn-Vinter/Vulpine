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
    Call,
    Block,
    Project,
    Array,
    IndexAccess,
};

class BaseNode {
public:
    BaseNode() = default;
    virtual ~BaseNode() = default;
    virtual auto nodeType() const -> NodeType { return NodeType::Base; }
    virtual auto getType() const -> TokenType { return TokenType::Invalid; }
};

class VariableNode : public BaseNode {
    std::string m_name{};
    TokenType m_declaredType{TokenType::AutoWild};
    std::unique_ptr<BaseNode> m_value{nullptr};
    bool m_isParameterCopy{false};
    bool m_isConst{false};

public:
    VariableNode(std::string name) : m_name{std::move(name)} {}

    auto nodeType() const -> NodeType override { return NodeType::Variable; }
    auto getType() const -> TokenType override { return m_declaredType; }
    
    auto setName(const std::string& name) -> void { m_name = name; }
    auto getName() const -> const std::string& { return m_name; }

    auto setValue(std::unique_ptr<BaseNode> val) -> void { m_value = std::move(val); }
    auto getValue() const -> BaseNode* { return m_value.get(); } 

    auto setDeclaredType(TokenType type) -> void { m_declaredType = type; }
    auto getDeclaredType() const -> TokenType { return m_declaredType; }

    auto setParameterCopy(bool copy) -> void { m_isParameterCopy = copy; }
    auto isParameterCopy() const -> bool { return m_isParameterCopy; }
    auto setConst(bool isConst) -> void { m_isConst = isConst; }
    auto isConst() const -> bool { return m_isConst; }
};

class VariableRefNode : public BaseNode {
    std::string_view m_name{};
    TokenType m_resolvedType{TokenType::Invalid};

public:
    VariableRefNode(std::string_view name, TokenType resolvedType = TokenType::Invalid)
        : m_name{name}, m_resolvedType{resolvedType} {}

    auto nodeType() const -> NodeType override { return NodeType::VariableRef; }
    auto getType() const -> TokenType override { return m_resolvedType; }

    auto setName(const std::string_view& name) -> void { m_name = name; }
    auto getName() const -> const std::string_view& { return m_name; }
    auto setResolvedType(TokenType type) -> void { m_resolvedType = type; }
};

class IfNode : public BaseNode {
    std::unique_ptr<BaseNode> condition{nullptr};
    std::vector<std::unique_ptr<BaseNode>> body{};
    std::unique_ptr<BaseNode> elseBranch{nullptr};

public:
    auto nodeType() const -> NodeType override { return NodeType::If; }

    auto setCondition(std::unique_ptr<BaseNode> cond) -> void { condition = std::move(cond); }
    auto getCondition() const -> BaseNode* { return condition.get(); }

    auto addBodyNode(std::unique_ptr<BaseNode> node) -> void { body.push_back(std::move(node)); }
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& { return body; }

    auto setElseBranch(std::unique_ptr<BaseNode> node) -> void { elseBranch = std::move(node); }
    auto getElseBranch() const -> const BaseNode* { return elseBranch.get(); }
};

class ReturnNode : public BaseNode {
    std::unique_ptr<BaseNode> expr{nullptr};

public:
    auto nodeType() const -> NodeType override { return NodeType::Return; }
    auto getType() const -> TokenType override { return expr ? expr->getType() : TokenType::Invalid; }
    auto setExpression(std::unique_ptr<BaseNode> n) -> void { expr = std::move(n); }
    auto getExpression() const -> const BaseNode* { return expr.get(); }
};

class FunctionNode : public BaseNode {
    std::string name{};
    TokenType returnType{TokenType::Invalid};
    std::vector<std::unique_ptr<VariableNode>> params{};
    std::vector<std::unique_ptr<BaseNode>> body{};

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
};

class LiteralNode : public BaseNode {
    TokenType type{TokenType::Invalid};
    std::string value{};

public:
    LiteralNode(TokenType literalType, std::string value)
        : type{literalType}, value{std::move(value)} {}

    auto nodeType() const -> NodeType override { return NodeType::Literal; }
    auto getType() const -> TokenType override { return type; }
    auto getValue() const -> const std::string& { return value; }
    auto getLiteralType() const -> TokenType { return type; }
    auto isStringLiteral() const -> bool { return type == TokenType::String_Literal; }
};

class BinaryOpNode : public BaseNode {
    TokenType op{TokenType::Invalid};
    std::unique_ptr<BaseNode> left{nullptr};
    std::unique_ptr<BaseNode> right{nullptr};
    TokenType resultType{TokenType::Invalid};

public:
    BinaryOpNode(TokenType op) : op{op} {}

    auto nodeType() const -> NodeType override { return NodeType::BinaryOp; }
    auto getType() const -> TokenType override { return resultType; }
    auto getOp() const -> TokenType { return op; }
    auto getLeft() const -> const BaseNode* { return left.get(); }
    auto getRight() const -> const BaseNode* { return right.get(); }

    auto setLeft(std::unique_ptr<BaseNode> n) -> void { left = std::move(n); }
    auto setRight(std::unique_ptr<BaseNode> n) -> void { right = std::move(n); }
    auto setResultType(TokenType type) -> void { resultType = type; }
};

class UnaryOpNode : public BaseNode {
    TokenType op{TokenType::Invalid};
    std::unique_ptr<BaseNode> operand{nullptr};
    bool isPrefix{true};

public:
    UnaryOpNode(TokenType op) : op{op} {}

    auto nodeType() const -> NodeType override { return NodeType::UnaryOp; }
    auto getType() const -> TokenType override {
        if (op == TokenType::NOT) return TokenType::Bool;
        if (op == TokenType::Address) return TokenType::Nullptr;
        if (op == TokenType::Deref) return TokenType::Nullptr;
        return operand ? operand->getType() : TokenType::Invalid;
    }
    auto getOp() const -> TokenType { return op; }
    auto getOperand() const -> const BaseNode* { return operand.get(); }
    auto setOperand(std::unique_ptr<BaseNode> n) -> void { operand = std::move(n); }
    auto setIsPrefix(bool isPrefix) -> void { this->isPrefix = isPrefix; }
    auto getIsPrefix() const -> bool { return isPrefix; }
};

class CastNode : public BaseNode {
    TokenType targetType{TokenType::Invalid};
    std::unique_ptr<BaseNode> expr{nullptr};

public:
    CastNode(TokenType targetType) : targetType{targetType} {}

    auto nodeType() const -> NodeType override { return NodeType::Cast; }
    auto getType() const -> TokenType override { return targetType; }
    
    auto setExpression(std::unique_ptr<BaseNode> n) -> void { expr = std::move(n); }
    auto getExpression() const -> const BaseNode* { return expr.get(); }
};

class CallNode : public BaseNode {
    std::string functionName{};
    TokenType returnType{TokenType::Invalid};
    std::vector<std::unique_ptr<BaseNode>> arguments{};

public:
    CallNode(std::string name) : functionName{std::move(name)} {}

    auto nodeType() const -> NodeType override { return NodeType::Call; }
    auto getType() const -> TokenType override { return returnType; }

    auto getName() const -> const std::string& { return functionName; }
    auto addArgument(std::unique_ptr<BaseNode> arg) -> void { arguments.push_back(std::move(arg)); }
    auto getArguments() const -> const std::vector<std::unique_ptr<BaseNode>>& { return arguments; }
    auto setReturnType(TokenType type) -> void { returnType = type; }
};

class BlockNode : public BaseNode {
    std::vector<std::unique_ptr<BaseNode>> body{};

public:
    auto nodeType() const -> NodeType override { return NodeType::Block; }
    
    auto addStatement(std::unique_ptr<BaseNode> node) -> void {
        body.push_back(std::move(node));
    }
    
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& {
        return body;
    }

    auto moveBody() -> std::vector<std::unique_ptr<BaseNode>> {
        return std::move(body);
    }
};

constexpr std::string_view allow_c_style_decl = "allow_c_style_decl";
constexpr std::string_view decline_vulpine_style_decl = "decline_vulpine_style_decl";
constexpr std::string_view decline_syscalls = "decline_syscalls";
constexpr std::string_view decline_inline_languages = "decline_inline_languages";
constexpr std::string_view decline_vulpine_standard_library = "decline_vulpine_standard_library";
constexpr std::string_view decline_pointer_decleration = "decline_pointer_decleration";

class ProjectNode : public BaseNode {
    ProjectNode* parent{nullptr};
    std::string_view projectName{};
    std::string_view vulpineVersion{};
    std::string_view entryPoint{};
    std::string_view publisher{};
    std::string_view outputType{};
    std::string_view optimizationLevel{};
    std::vector<std::string_view> architectures{};

    std::unordered_map<std::string_view, bool> ruleset{
        {allow_c_style_decl, false},
        {decline_vulpine_style_decl, false},
        {decline_syscalls, false},
        {decline_inline_languages, false},
        {decline_vulpine_standard_library, false},
        {decline_pointer_decleration, false}
    };

public:
    auto nodeType() const -> NodeType override { return NodeType::Project; }

    using strv = std::string_view;

    auto setProjectName(strv name) -> void { projectName = name; }
    auto setVulpineVersion(strv ver) -> void { vulpineVersion = ver; }
    auto setEntryPoint(strv entry) -> void { entryPoint = entry; }
    auto setPublisher(strv pub) -> void { publisher = pub; }
    auto setRuleset(const std::unordered_map<strv, bool>& newRules) -> void { ruleset = newRules; }
    auto setOutputType(strv type) -> void { outputType = type; }
    auto setOptimizationLevel(strv opt) -> void { optimizationLevel = opt; }
    auto addArchitecture(strv arch) -> void { architectures.push_back(arch); }

    auto setRule(strv ruleName, bool value) -> void { 
        ruleset[ruleName] = value; 
    }

    auto getProjectName() const -> strv { return projectName; }
    auto getVulpineVersion() const -> strv { return vulpineVersion; }
    auto getEntryPoint() const -> strv { return entryPoint; }
    auto getPublisher() const -> strv { return publisher; }
    auto getRuleset() const -> const std::unordered_map<strv, bool>& { return ruleset; }
    auto getOutputType() const -> strv { return outputType; }
    auto getOptimizationLevel() const -> strv { return optimizationLevel; }
    auto getArchitectures() const -> const std::vector<strv>& { return architectures; }

    auto setParent(ProjectNode* p) -> void { parent = p; }
    
    auto isRuleEnabled(strv ruleName) const -> bool {
        auto it = ruleset.find(ruleName);
        if (it != ruleset.end()) {
            return it->second;
        }
        
        if (parent) {
            return parent->isRuleEnabled(ruleName);
        }
        return false; 
    }
};

class ArrayNode : public BaseNode {
    std::string m_name{};
    TokenType m_declaredType{TokenType::AutoWild};
    std::vector<std::unique_ptr<BaseNode>> m_values{};
    size_t m_elementCount{0};
    bool m_isParameterCopy{false};
    bool m_isConst{false};

public:
    [[nodiscard]] auto nodeType() const -> NodeType override { return NodeType::Array; }

    [[nodiscard]] auto getArrayName() const -> const std::string& { return m_name; }
    auto setArrayName(const std::string& name) -> void { m_name = name; }

    [[nodiscard]] auto getDeclaredType() const -> TokenType { return m_declaredType; }
    auto setDeclaredType(TokenType type) -> void { m_declaredType = type; }

    [[nodiscard]] auto getValues() const -> const std::vector<std::unique_ptr<BaseNode>>& { return m_values; }
    [[nodiscard]] auto moveValues() -> std::vector<std::unique_ptr<BaseNode>> { return std::move(m_values); }
    auto setValues(std::vector<std::unique_ptr<BaseNode>> values) -> void { m_values = std::move(values); }
    auto addValue(std::unique_ptr<BaseNode> value) -> void { m_values.push_back(std::move(value)); }

    [[nodiscard]] auto isParameterCopy() const -> bool { return m_isParameterCopy; }
    auto setParameterCopy(bool copy) -> void { m_isParameterCopy = copy; }

    [[nodiscard]] auto isConst() const -> bool { return m_isConst; }
    auto setConst(bool isConst) -> void { m_isConst = isConst; }

    auto setElementCount(size_t count) -> void { m_elementCount = count; }
    [[nodiscard]] auto getElementCount() const -> size_t { return m_elementCount; }
};

class IndexAccessNode : public BaseNode {
    std::unique_ptr<BaseNode> m_target{nullptr};
    std::unique_ptr<BaseNode> m_index{nullptr};

public:
    IndexAccessNode() = default;

    auto nodeType() const -> NodeType override { return NodeType::IndexAccess; }

    auto setTarget(std::unique_ptr<BaseNode> target) -> void { m_target = std::move(target); }
    auto setIndex(std::unique_ptr<BaseNode> index) -> void { m_index = std::move(index); }

    [[nodiscard]] auto getTarget() const -> BaseNode* { return m_target.get(); }
    [[nodiscard]] auto getIndex() const -> BaseNode* { return m_index.get(); }

    [[nodiscard]] auto getType() const -> TokenType override {
        return m_target ? m_target->getType() : TokenType::Invalid;
    }
};