#pragma once

#include <string_view>
#include <vector>
#include <memory>

#include <AST.hpp>
#include <unordered_map>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>

class emitter {
    using strv = std::string_view;

    size_t index = 0;
    
    // Core LLVM state primitives.
    // Order matters: Context must come before Module and Builder!
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> builder;
    std::unordered_map<std::string, llvm::Value*> namedValues;

public:
    emitter();
    ~emitter();

    auto emit(const std::vector<std::unique_ptr<BaseNode>>& nodes, std::string_view filepath, std::string_view format) -> void;

private:
    // Helper to initialize entry point (e.g. main)
    auto setup_entry_function(const FunctionNode* node) -> llvm::Function*;
    auto emit_function_body(const FunctionNode* node, llvm::Function* fn) -> void;

    // AST visitor helpers for codegen
    auto emit_node(const BaseNode* node) -> llvm::Value*;
    auto emit_literal(const LiteralNode* node) -> llvm::Value*;
    auto emit_cast(const CastNode* node) -> llvm::Value*;

    auto emit_function(const FunctionNode* node) -> llvm::Function*;

    auto emit_variable(const VariableNode* node) -> llvm::Value*;

    auto vulpine_type_to_llvm_type(TokenType type) -> llvm::Type*;
};