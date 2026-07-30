#pragma once
#include <string>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <utility>
#include <lexicon.hpp>

constexpr unsigned int NodeType_Base = 0x00;
constexpr unsigned int NodeType_Variable = 0x01;
constexpr unsigned int NodeType_If = 0x02;
constexpr unsigned int NodeType_Function = 0x03;
constexpr unsigned int NodeType_Literal = 0x04;
constexpr unsigned int NodeType_BinaryOp = 0x05;
constexpr unsigned int NodeType_UnaryOp = 0x06;
constexpr unsigned int NodeType_Cast = 0x07;

class BaseNode {
public:
    BaseNode() = default;
    virtual ~BaseNode() = default;
    virtual auto nodeType() const -> unsigned int { return NodeType_Base; }
};

class VariableNode : public BaseNode {
public:
    // Fixed: Initializing member 'm_name' with parameter 'name'
    VariableNode(std::string name) : m_name(std::move(name)) {}

    auto nodeType() const -> unsigned int override { return NodeType_Variable; }
    
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

class IfNode : public BaseNode {
public:
    auto nodeType() const -> unsigned int override { return NodeType_If; }

    auto setCondition(std::unique_ptr<BaseNode> cond) -> void { condition = std::move(cond); }
    auto getCondition() const -> BaseNode* { return condition.get(); }

    auto addBodyNode(std::unique_ptr<BaseNode> node) -> void { body.push_back(std::move(node)); }
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& { return body; }

private:
    std::unique_ptr<BaseNode> condition;
    std::vector<std::unique_ptr<BaseNode>> body;
};

class FunctionNode : public BaseNode {
public:
    auto nodeType() const -> unsigned int override { return NodeType_Function; }

    auto setName(std::string name) -> void { this->name = std::move(name); }
    auto getName() const -> const std::string& { return name; }

    auto addParam(std::unique_ptr<VariableNode> param) -> void { params.push_back(std::move(param)); }
    auto addBodyNode(std::unique_ptr<BaseNode> node) -> void { body.push_back(std::move(node)); }

private:
    std::string name;
    std::vector<std::unique_ptr<VariableNode>> params;
    std::vector<std::unique_ptr<BaseNode>> body;
};

// Literal Node: Stores the value and its type
class LiteralNode : public BaseNode {
public:
    LiteralNode(std::string value) : value(std::move(value)) {}
    auto nodeType() const -> unsigned int override { return NodeType_Literal; }
    auto getValue() const -> const std::string& { return value; }
private:
    std::string value;
};

// BinaryOpNode: Handles math (+, -, *, /)
class BinaryOpNode : public BaseNode {
public:
    BinaryOpNode(std::string op) : op(std::move(op)) {}
    auto nodeType() const -> unsigned int override { return NodeType_BinaryOp; }
    
    auto setLeft(std::unique_ptr<BaseNode> n) { left = std::move(n); }
    auto setRight(std::unique_ptr<BaseNode> n) { right = std::move(n); }
private:
    std::string op;
    std::unique_ptr<BaseNode> left;
    std::unique_ptr<BaseNode> right;
};

// UnaryOpNode: Handles pointers (&) and dereferences (*)
class UnaryOpNode : public BaseNode {
public:
    UnaryOpNode(std::string op) : op(std::move(op)) {}
    auto nodeType() const -> unsigned int override { return NodeType_UnaryOp; }
    
    auto setOperand(std::unique_ptr<BaseNode> n) { operand = std::move(n); }
private:
    std::string op;
    std::unique_ptr<BaseNode> operand;
};

// CastNode: Handles type conversion
class CastNode : public BaseNode {
public:
    CastNode(std::string targetType) : targetType(std::move(targetType)) {}
    auto nodeType() const -> unsigned int override { return NodeType_Cast; }
    
    auto setExpression(std::unique_ptr<BaseNode> n) { expr = std::move(n); }
private:
    std::string targetType;
    std::unique_ptr<BaseNode> expr;
};

constexpr unsigned int NodeType_Block = 0x09; // Add this constant

class BlockNode : public BaseNode {
public:
    auto nodeType() const -> unsigned int override { return NodeType_Block; }
    
    // Allows the parser to add statements as it parses them
    auto addStatement(std::unique_ptr<BaseNode> node) -> void {
        body.push_back(std::move(node));
    }
    
    auto getBody() const -> const std::vector<std::unique_ptr<BaseNode>>& {
        return body;
    }

private:
    std::vector<std::unique_ptr<BaseNode>> body;
};

constexpr unsigned int NodeType_Project = 0x0A;
constexpr std::string_view allow_c_style_decl = "allow_c_style_decl";
constexpr std::string_view decline_vulpine_style_decl = "decline_vulpine_style_decl";
constexpr std::string_view decline_syscalls = "decline_syscalls";
constexpr std::string_view decline_inline_languages = "decline_inline_languages";
constexpr std::string_view decline_vulpine_standard_library = "decline_vulpine_standard_library";
constexpr std::string_view decline_pointer_decleration = "decline_pointer_decleration";

class ProjectNode : public BaseNode {
public:
    auto nodeType() const -> unsigned int override { return NodeType_Project; }

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
