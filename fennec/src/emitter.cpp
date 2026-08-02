#include "AST.hpp"
#include "lexicon.hpp"
#include <emitter.hpp>
#include <system_error>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Constants.h>

emitter::emitter() 
    : context(),
      module(std::make_unique<llvm::Module>("VulpineModule", context)),
      builder(context) {}

emitter::~emitter() = default;

auto emitter::vulpine_type_to_llvm_type(TokenType type) -> llvm::Type* {
    switch (type) {
        // Pointers & Nothingness (String & Nullptr)
        // Vulpine unifies empty returns and null references into 'nullptr' (opaque ptr in LLVM IR)
        case TokenType::Nullptr: 
        case TokenType::String:
            return llvm::PointerType::get(context, 0);

        // 128-bit Integers
        case TokenType::I128: case TokenType::U128: case TokenType::W128:
            return llvm::Type::getInt128Ty(context);

        // 64-bit Integers
        case TokenType::I64: case TokenType::U64: case TokenType::W64:
            return llvm::Type::getInt64Ty(context);

        // 32-bit Integers (Default target word width for 'int', 'uint', 'auto')
        case TokenType::Int: case TokenType::UInt: case TokenType::AutoInt: case TokenType::AutoUInt:
        case TokenType::I32: case TokenType::U32: case TokenType::W32:
        case TokenType::Wild: case TokenType::AutoWild:
            return llvm::Type::getInt32Ty(context);

        // 16-bit Integers
        case TokenType::I16: case TokenType::U16: case TokenType::W16:
            return llvm::Type::getInt16Ty(context);

        // 8-bit Integers / Bytes / Chars
        case TokenType::I8: case TokenType::U8: case TokenType::W8: 
        case TokenType::Char: case TokenType::Byte:
            return llvm::Type::getInt8Ty(context);

        // Floats
        case TokenType::F16:
            return llvm::Type::getHalfTy(context);
        case TokenType::F32: case TokenType::Float: case TokenType::AutoFloat:
            return llvm::Type::getFloatTy(context);
        case TokenType::F64: case TokenType::Double:
            return llvm::Type::getDoubleTy(context);
        case TokenType::F128:
            return llvm::Type::getFP128Ty(context);

        // Booleans & Bits
        case TokenType::Bool: case TokenType::Bit:
            return llvm::Type::getInt1Ty(context);

        default:
            return nullptr;
    }
}

auto emitter::setup_entry_function(const FunctionNode* node) -> llvm::Function* {
    // 1. Build argument types dynamically from AST node parameters
    std::vector<llvm::Type*> paramTypes;
    for (const auto& param : node->getParams()) {
        llvm::Type* paramTy = vulpine_type_to_llvm_type(param->getType());
        if (!paramTy) {
            // Default parameter fallback if type lookup fails
            paramTy = llvm::Type::getInt32Ty(context);
        }
        paramTypes.push_back(paramTy);
    }

    // 2. Resolve return type (nullptr maps to opaque ptr)
    llvm::Type* retType = vulpine_type_to_llvm_type(node->getReturnType());
    if (!retType) {
        retType = llvm::Type::getInt32Ty(context); // Default to int32 for untyped functions
    }

    // 3. Create raw signature
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        retType,
        paramTypes,
        false // Not variadic
    );

    // 4. Create external linkage entry point
    llvm::Function* entryFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        node->getName(),
        module.get()
    );

    // 5. Mark freestanding attributes
    entryFunc->addFnAttr(llvm::Attribute::NoRecurse);

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(context, "entry", entryFunc);
    builder.SetInsertPoint(entryBlock);

    return entryFunc;
}

auto emitter::emit_variable(const VariableNode* node) -> llvm::Value* {
    llvm::Type* varType = vulpine_type_to_llvm_type(node->getDeclaredType());
    if (!varType) {
        varType = llvm::Type::getInt32Ty(context);
    }

    llvm::AllocaInst* alloc = builder.CreateAlloca(varType, nullptr, node->getName());
    namedValues[node->getName()] = alloc;

    if (node->getValue()) {
        llvm::Value* initVal = emit_node(node->getValue());
        if (initVal) {
            if (initVal->getType() != varType && initVal->getType()->isIntegerTy() && varType->isIntegerTy()) {
                initVal = builder.CreateIntCast(initVal, varType, true, "casttmp");
            }
            builder.CreateStore(initVal, alloc);
        }
    }

    return alloc;
}

auto emitter::emit_literal(const LiteralNode* lit) -> llvm::Value* {
    TokenType literalType = lit->getLiteralType();
    const std::string& value = lit->getValue();

    if (literalType == TokenType::Nullptr) {
        return llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
    }

    if (literalType == TokenType::True) {
        return llvm::ConstantInt::getTrue(context);
    }
    if (literalType == TokenType::False) {
        return llvm::ConstantInt::getFalse(context);
    }

    if (literalType == TokenType::String || literalType == TokenType::String_Literal) {
        return builder.CreateGlobalString(lit->getValue(), "str");
    }

    if (literalType == TokenType::Char || literalType == TokenType::Char_Literal) {
        char ch = value.size() >= 2 ? value[1] : '\0';
        return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), static_cast<int64_t>(ch), true);
    }

    if (TokenUtils::isFloat(literalType)) {
        double floatValue = 0.0;
        if (!value.empty()) {
            floatValue = std::stod(value);
        }
        llvm::Type* llvmType = llvm::Type::getDoubleTy(context);
        if (literalType == TokenType::F32 || literalType == TokenType::Float || literalType == TokenType::AutoFloat) {
            llvmType = llvm::Type::getFloatTy(context);
        } else if (literalType == TokenType::F16) {
            llvmType = llvm::Type::getHalfTy(context);
        }
        return llvm::ConstantFP::get(llvmType, floatValue);
    }

    if (TokenUtils::isInt(literalType) || TokenUtils::isUInt(literalType)) {
        int64_t intValue = 0;
        if (!value.empty()) {
            intValue = std::stoll(value);
        }

        llvm::Type* llvmType = llvm::Type::getInt32Ty(context);
        return llvm::ConstantInt::get(llvmType, intValue, TokenUtils::isInt(literalType));
    }

    return nullptr;
}

auto emitter::emit_cast(const CastNode* node) -> llvm::Value* {
    llvm::Value* exprValue = emit_node(node->getExpression());
    if (!exprValue) return nullptr;

    llvm::Type* targetType = vulpine_type_to_llvm_type(node->getType());
    if (!targetType) return exprValue;

    llvm::Type* sourceType = exprValue->getType();
    if (sourceType == targetType) {
        return exprValue;
    }

    bool sourceIsInt = sourceType->isIntegerTy();
    bool targetIsInt = targetType->isIntegerTy();
    bool sourceIsFloat = sourceType->isFloatingPointTy();
    bool targetIsFloat = targetType->isFloatingPointTy();

    bool sourceUnsigned = TokenUtils::isUInt(node->getExpression()->getType());
    bool targetUnsigned = TokenUtils::isUInt(node->getType());

    if (sourceIsInt && targetIsInt) {
        return builder.CreateIntCast(exprValue, targetType, !sourceUnsigned, "casttmp");
    }

    if (sourceIsInt && targetIsFloat) {
        return sourceUnsigned
            ? builder.CreateUIToFP(exprValue, targetType, "casttmp")
            : builder.CreateSIToFP(exprValue, targetType, "casttmp");
    }

    if (sourceIsFloat && targetIsInt) {
        return targetUnsigned
            ? builder.CreateFPToUI(exprValue, targetType, "casttmp")
            : builder.CreateFPToSI(exprValue, targetType, "casttmp");
    }

    if (sourceIsFloat && targetIsFloat) {
        return builder.CreateFPCast(exprValue, targetType, "casttmp");
    }

    if (sourceType->isPointerTy() && targetType->isPointerTy()) {
        return builder.CreateBitCast(exprValue, targetType, "casttmp");
    }

    return nullptr;
}

auto emitter::emit_node(const BaseNode* node) -> llvm::Value* {
    if (!node) return nullptr;

    switch (node->nodeType()) {
        case NodeType::Literal: {
            auto* lit = static_cast<const LiteralNode*>(node);
            return emit_literal(lit);
        }

        case NodeType::Variable: {
            return emit_variable(static_cast<const VariableNode*>(node));
        }

        case NodeType::VariableRef: {
            auto* ref = static_cast<const VariableRefNode*>(node);
            auto it = namedValues.find(std::string(ref->getName()));
            if (it == namedValues.end()) return nullptr;
            llvm::Value* ptr = it->second;
            auto* alloc = llvm::cast<llvm::AllocaInst>(ptr);
            return builder.CreateLoad(alloc->getAllocatedType(), ptr, std::string(ref->getName()));
        }

        case NodeType::BinaryOp: {
            auto* bin = static_cast<const BinaryOpNode*>(node);
            llvm::Value* lhs = emit_node(bin->getLeft());
            llvm::Value* rhs = emit_node(bin->getRight());
            if (!lhs || !rhs) return nullptr;

            bool isFloat = TokenUtils::isFloat(bin->getType());
            switch (bin->getOp()) {
                case TokenType::Add:
                    return isFloat ? builder.CreateFAdd(lhs, rhs, "addtmp") : builder.CreateAdd(lhs, rhs, "addtmp");
                case TokenType::Subtract:
                    return isFloat ? builder.CreateFSub(lhs, rhs, "subtmp") : builder.CreateSub(lhs, rhs, "subtmp");
                case TokenType::Multiply:
                    return isFloat ? builder.CreateFMul(lhs, rhs, "multmp") : builder.CreateMul(lhs, rhs, "multmp");
                case TokenType::Divide:
                    return isFloat ? builder.CreateFDiv(lhs, rhs, "divtmp") : builder.CreateSDiv(lhs, rhs, "divtmp");
                default:
                    return nullptr;
            }
        }

        case NodeType::Cast: {
            return emit_cast(static_cast<const CastNode*>(node));
        }

        case NodeType::Return: {
            auto* ret = static_cast<const ReturnNode*>(node);
            llvm::Value* value = emit_node(ret->getExpression());
            if (!value) {
                builder.CreateRetVoid();
            } else {
                builder.CreateRet(value);
            }
            return value;
        }

        case NodeType::Block: {
            auto* block = static_cast<const BlockNode*>(node);
            for (const auto& stmt : block->getBody()) {
                emit_node(stmt.get());
            }
            return nullptr;
        }

        default:
            return nullptr;
    }
}

auto emitter::emit_function_body(const FunctionNode* node, llvm::Function* fn) -> void {
    namedValues.clear();

    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        const auto& param = node->getParams()[idx++];
        arg.setName(param->getName());
        llvm::AllocaInst* alloc = builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
        builder.CreateStore(&arg, alloc);
        namedValues[param->getName()] = alloc;
    }

    for (const auto& stmt : node->getBody()) {
        emit_node(stmt.get());
    }

    if (!builder.GetInsertBlock()->getTerminator()) {
        if (fn->getReturnType()->isVoidTy()) {
            builder.CreateRetVoid();
        } else {
            builder.CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
        }
    }
}

auto emitter::emit(const std::vector<std::unique_ptr<BaseNode>>& nodes, std::string_view filepath) -> void {
    const ProjectNode* project = nullptr;

    // First pass: locate the ProjectNode metadata if present
    for (const auto& node : nodes) {
        if (node->nodeType() == NodeType::Project) {
            project = static_cast<const ProjectNode*>(node.get());
            break;
        }
    }

    // Second pass: emit functions and global scope
    for (const auto& node : nodes) {
        switch (node->nodeType()) {
            case NodeType::Function: {
                const auto* functionNode = static_cast<const FunctionNode*>(node.get());

                bool isEntryPoint = project && (functionNode->getName() == project->getEntryPoint());
                llvm::Function* fn = setup_entry_function(functionNode);

                if (isEntryPoint) {
                    // Entry point can be marked specially if needed.
                }

                emit_function_body(functionNode, fn);
                break;
            }
            case NodeType::Variable: {
                const auto* variableNode = static_cast<const VariableNode*>(node.get());
                emit_variable(variableNode);
                break;
            }
            default:
                break;
        }
    }

    // Dump generated LLVM IR module to disk
    std::error_code ec;
    llvm::raw_fd_ostream outFile(filepath, ec, llvm::sys::fs::OF_None);
    if (!ec) {
        module->print(outFile, nullptr);
    }
}
