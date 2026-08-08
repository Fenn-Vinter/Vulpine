#include "AST.hpp"
#include "lexicon.hpp"
#include <emitter.hpp>
#include <system_error>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>

emitter::emitter() 
    : context(),
      module(std::make_unique<llvm::Module>("VulpineModule", context)),
      builder(context) {
    // Initialize all targets to allow cross-compilation target emission
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
}

emitter::~emitter() = default;

auto emitter::vulpine_type_to_llvm_type(TokenType type) -> llvm::Type* {
    switch (type) {
        case TokenType::Nullptr:
        case TokenType::String_Literal:
            return llvm::PointerType::get(context, 0);

        case TokenType::I128: case TokenType::U128: case TokenType::W128:
            return llvm::Type::getInt128Ty(context);

        case TokenType::I64: case TokenType::U64: case TokenType::W64:
            return llvm::Type::getInt64Ty(context);

        case TokenType::Int: case TokenType::UInt:
        case TokenType::I32: case TokenType::U32: case TokenType::W32:
            return llvm::Type::getInt32Ty(context);

        case TokenType::AutoInt: case TokenType::AutoUInt: case TokenType::AutoWild:
            return llvm::Type::getIntNTy(context, static_cast<unsigned>(sizeof(void*) * 8));

        case TokenType::I16: case TokenType::U16: case TokenType::W16:
            return llvm::Type::getInt16Ty(context);

        case TokenType::I8: case TokenType::U8: case TokenType::W8: 
        case TokenType::Char: case TokenType::Byte:
            return llvm::Type::getInt8Ty(context);

        case TokenType::F16:
            return llvm::Type::getHalfTy(context);
        case TokenType::F32: case TokenType::Float: case TokenType::AutoFloat:
            return llvm::Type::getFloatTy(context);
        case TokenType::F64: case TokenType::Double:
            return llvm::Type::getDoubleTy(context);
        case TokenType::F128:
            return llvm::Type::getFP128Ty(context);

        case TokenType::Bool: case TokenType::Bit:
            return llvm::Type::getInt1Ty(context);

        default:
            return nullptr;
    }
}

auto emitter::setup_entry_function(const FunctionNode* node) -> llvm::Function* {
    std::vector<llvm::Type*> paramTypes;
    for (const auto& param : node->getParams()) {
        llvm::Type* baseType = vulpine_type_to_llvm_type(param->getType());
        if (!baseType) {
            baseType = llvm::Type::getInt32Ty(context);
        }
        llvm::Type* paramTy = param->isParameterCopy() ? baseType : llvm::PointerType::get(context, 0);
        paramTypes.push_back(paramTy);
    }

    llvm::Type* retType = vulpine_type_to_llvm_type(node->getReturnType());
    if (!retType) {
        retType = llvm::Type::getInt32Ty(context);
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, false);

    llvm::Function* entryFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        node->getName(),
        module.get()
    );

    entryFunc->addFnAttr(llvm::Attribute::NoRecurse);
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
            if (initVal->getType() != varType) {
                if (initVal->getType()->isIntegerTy() && varType->isIntegerTy()) {
                    initVal = builder.CreateIntCast(initVal, varType, true, "casttmp");
                } else if (initVal->getType()->isPointerTy() && varType->isIntegerTy()) {
                    initVal = builder.CreatePtrToInt(initVal, varType, "ptrtoint");
                } else if (initVal->getType()->isIntegerTy() && varType->isPointerTy()) {
                    initVal = builder.CreateIntToPtr(initVal, varType, "inttoptr");
                } else if (initVal->getType()->isPointerTy() && varType->isPointerTy()) {
                    initVal = builder.CreateBitCast(initVal, varType, "ptrcast");
                }
            }
            builder.CreateStore(initVal, alloc);
        }
    }

    return alloc;
}

auto emitter::emit_array(const ArrayNode* node) -> llvm::Value* {
    llvm::Type* elemType = vulpine_type_to_llvm_type(node->getDeclaredType());
    if (!elemType) elemType = llvm::Type::getInt32Ty(context);

    size_t count = node->getValues().size();
    if (node->getElementCount() > count) {
        count = node->getElementCount();
    }

    llvm::ArrayType* arrType = llvm::ArrayType::get(elemType, count);
    llvm::AllocaInst* alloc = builder.CreateAlloca(arrType, nullptr, node->getArrayName());
    namedValues[node->getArrayName()] = alloc;

    size_t idx = 0;
    for (const auto& valExpr : node->getValues()) {
        llvm::Value* val = emit_node(valExpr.get());
        if (val) {
            llvm::Value* zero = builder.getInt32(0);
            llvm::Value* index = builder.getInt32(idx);
            llvm::Value* gep = builder.CreateGEP(arrType, alloc, {zero, index}, "arr_init_gep");
            builder.CreateStore(val, gep);
        }
        idx++;
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

    if (literalType == TokenType::String_Literal) {
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

    if (sourceType->isPointerTy() && targetType->isPointerTy()) {
        return exprValue;
    }

    if (sourceType->isPointerTy() && targetType->isIntegerTy()) {
        return builder.CreatePtrToInt(exprValue, targetType, "casttmp");
    }

    if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
        return builder.CreateIntToPtr(exprValue, targetType, "casttmp");
    }

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

    return nullptr;
}

static auto emit_to_bool(llvm::IRBuilder<>& builder, llvm::Value* value) -> llvm::Value* {
    llvm::Type* type = value->getType();
    if (type->isIntegerTy(1)) {
        return value;
    }
    if (type->isFloatingPointTy()) {
        return builder.CreateFCmpONE(value, llvm::ConstantFP::get(type, 0.0), "tobool");
    }
    if (type->isIntegerTy()) {
        return builder.CreateICmpNE(value, llvm::ConstantInt::get(type, 0), "tobool");
    }
    if (type->isPointerTy()) {
        return builder.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type)), "tobool");
    }
    return nullptr;
}

auto emitter::emit_if(const IfNode* node) -> llvm::Value* {
    llvm::Value* conditionValue = emit_node(node->getCondition());
    if (!conditionValue) return nullptr;

    llvm::Value* boolCondition = emit_to_bool(builder, conditionValue);
    if (!boolCondition) return nullptr;

    llvm::Function* currentFunction = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(context, "then", currentFunction);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context, "ifcont", currentFunction);
    llvm::BasicBlock* elseBlock = node->getElseBranch() ? llvm::BasicBlock::Create(context, "else", currentFunction) : nullptr;

    if (node->getElseBranch()) {
        builder.CreateCondBr(boolCondition, thenBlock, elseBlock);
    } else {
        builder.CreateCondBr(boolCondition, thenBlock, mergeBlock);
    }

    builder.SetInsertPoint(thenBlock);
    for (const auto& stmt : node->getBody()) {
        emit_node(stmt.get());
    }

    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(mergeBlock);
    }

    if (node->getElseBranch()) {
        builder.SetInsertPoint(elseBlock);
        emit_node(node->getElseBranch());
        if (!builder.GetInsertBlock()->getTerminator()) {
            builder.CreateBr(mergeBlock);
        }
    }

    builder.SetInsertPoint(mergeBlock);
    return nullptr;
}

auto emitter::emit_unary(const UnaryOpNode* node) -> llvm::Value* {
    switch (node->getOp()) {
        case TokenType::NOT: {
            llvm::Value* operand = emit_node(node->getOperand());
            if (!operand) return nullptr;
            llvm::Value* boolValue = emit_to_bool(builder, operand);
            if (!boolValue) return nullptr;
            return builder.CreateNot(boolValue, "nottmp");
        }
        case TokenType::Address: {
            if (auto* varRef = dynamic_cast<const VariableRefNode*>(node->getOperand())) {
                auto it = namedValues.find(std::string(varRef->getName()));
                if (it == namedValues.end()) return nullptr;
                return it->second;
            }

            if (auto* indexAccess = dynamic_cast<const IndexAccessNode*>(node->getOperand())) {
                llvm::Value* targetVal = nullptr;
                if (auto* varRef = dynamic_cast<const VariableRefNode*>(indexAccess->getTarget())) {
                    auto it = namedValues.find(std::string(varRef->getName()));
                    if (it != namedValues.end()) targetVal = it->second;
                }

                llvm::Value* indexVal = emit_node(indexAccess->getIndex());
                llvm::Type* elemType = vulpine_type_to_llvm_type(indexAccess->getType());
                if (!elemType) elemType = llvm::Type::getInt32Ty(context);

                if (auto* alloc = llvm::dyn_cast<llvm::AllocaInst>(targetVal)) {
                    llvm::Value* zero = builder.getInt32(0);
                    return builder.CreateGEP(alloc->getAllocatedType(), targetVal, {zero, indexVal}, "addrgep");
                }
                return builder.CreateGEP(elemType, targetVal, indexVal, "addrgep");
            }
            return nullptr;
        }
        case TokenType::Deref: {
            llvm::Value* operand = emit_node(node->getOperand());
            if (!operand) return nullptr;
            
            llvm::Type* loadType = vulpine_type_to_llvm_type(node->getType());
            if (!loadType) {
                loadType = llvm::Type::getInt32Ty(context);
            }
            return builder.CreateLoad(loadType, operand, "deref_val");
        }
        default:
            return nullptr;
    }
}

auto emitter::emit_index_access(const IndexAccessNode* node) -> llvm::Value* {
    llvm::Value* targetVal = nullptr;

    if (auto* varRef = dynamic_cast<const VariableRefNode*>(node->getTarget())) {
        auto it = namedValues.find(std::string(varRef->getName()));
        if (it != namedValues.end()) {
            targetVal = it->second;
        }
    } else {
        targetVal = emit_node(node->getTarget());
    }

    llvm::Value* indexVal = emit_node(node->getIndex());
    if (!targetVal || !indexVal) return nullptr;

    llvm::Type* elemType = vulpine_type_to_llvm_type(node->getType());
    if (!elemType) {
        elemType = llvm::Type::getInt32Ty(context);
    }

    llvm::Value* gep = nullptr;
    if (auto* alloc = llvm::dyn_cast<llvm::AllocaInst>(targetVal)) {
        llvm::Value* zero = builder.getInt32(0);
        gep = builder.CreateGEP(alloc->getAllocatedType(), targetVal, {zero, indexVal}, "arraygep");
    } else {
        gep = builder.CreateGEP(elemType, targetVal, indexVal, "arraygep");
    }

    return builder.CreateLoad(elemType, gep, "arrayval");
}

auto emitter::emit_node(const BaseNode* node) -> llvm::Value* {
    if (!node) return nullptr;

    switch (node->nodeType()) {
        case NodeType::Literal: {
            return emit_literal(static_cast<const LiteralNode*>(node));
        }

        case NodeType::Variable: {
            return emit_variable(static_cast<const VariableNode*>(node));
        }

        case NodeType::Array: {
            return emit_array(static_cast<const ArrayNode*>(node));
        }

        case NodeType::IndexAccess: {
            return emit_index_access(static_cast<const IndexAccessNode*>(node));
        }

        case NodeType::VariableRef: {
            auto* ref = static_cast<const VariableRefNode*>(node);
            auto it = namedValues.find(std::string(ref->getName()));
            if (it == namedValues.end()) return nullptr;

            llvm::Value* ptr = it->second;

            llvm::Type* loadType = nullptr;
            if (auto* alloc = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
                loadType = alloc->getAllocatedType();
            } else if (ptr->getType()->isPointerTy()) {
                loadType = llvm::Type::getInt32Ty(context);
            }

            if (!loadType) return nullptr;

            return builder.CreateLoad(loadType, ptr, ref->getName());
        }

        case NodeType::BinaryOp: {
            auto* bin = static_cast<const BinaryOpNode*>(node);
            llvm::Value* lhs = emit_node(bin->getLeft());
            llvm::Value* rhs = emit_node(bin->getRight());
            if (!lhs || !rhs) return nullptr;

            bool isFloat = TokenUtils::isFloat(bin->getType());
            bool lhsFloat = lhs->getType()->isFloatingPointTy();
            bool rhsFloat = rhs->getType()->isFloatingPointTy();
            switch (bin->getOp()) {
                case TokenType::Add:
                    return isFloat ? builder.CreateFAdd(lhs, rhs, "addtmp") : builder.CreateAdd(lhs, rhs, "addtmp");
                case TokenType::Subtract:
                    return isFloat ? builder.CreateFSub(lhs, rhs, "subtmp") : builder.CreateSub(lhs, rhs, "subtmp");
                case TokenType::Multiply:
                    return isFloat ? builder.CreateFMul(lhs, rhs, "multmp") : builder.CreateMul(lhs, rhs, "multmp");
                case TokenType::Divide:
                    return isFloat ? builder.CreateFDiv(lhs, rhs, "divtmp") : builder.CreateSDiv(lhs, rhs, "divtmp");
                case TokenType::EQUAL:
                    if (lhsFloat || rhsFloat) {
                        return builder.CreateFCmpOEQ(lhs, rhs, "eqtmp");
                    }
                    return builder.CreateICmpEQ(lhs, rhs, "eqtmp");
                case TokenType::NOT_EQUAL:
                    if (lhsFloat || rhsFloat) {
                        return builder.CreateFCmpONE(lhs, rhs, "netmp");
                    }
                    return builder.CreateICmpNE(lhs, rhs, "netmp");
                case TokenType::LESS:
                    return lhsFloat || rhsFloat ? builder.CreateFCmpOLT(lhs, rhs, "lttmp") : builder.CreateICmpSLT(lhs, rhs, "lttmp");
                case TokenType::LESS_EQUAL:
                    return lhsFloat || rhsFloat ? builder.CreateFCmpOLE(lhs, rhs, "letmp") : builder.CreateICmpSLE(lhs, rhs, "letmp");
                case TokenType::GREATER:
                    return lhsFloat || rhsFloat ? builder.CreateFCmpOGT(lhs, rhs, "gttmp") : builder.CreateICmpSGT(lhs, rhs, "gttmp");
                case TokenType::GREATER_EQUAL:
                    return lhsFloat || rhsFloat ? builder.CreateFCmpOGE(lhs, rhs, "getmp") : builder.CreateICmpSGE(lhs, rhs, "getmp");
                case TokenType::AND: {
                    llvm::Value* leftBool = emit_to_bool(builder, lhs);
                    llvm::Value* rightBool = emit_to_bool(builder, rhs);
                    if (!leftBool || !rightBool) return nullptr;
                    return builder.CreateAnd(leftBool, rightBool, "andtmp");
                }
                case TokenType::OR: {
                    llvm::Value* leftBool = emit_to_bool(builder, lhs);
                    llvm::Value* rightBool = emit_to_bool(builder, rhs);
                    if (!leftBool || !rightBool) return nullptr;
                    return builder.CreateOr(leftBool, rightBool, "ortmp");
                }
                case TokenType::Assign:
                case TokenType::Add_Assign:
                case TokenType::Subtract_Assign:
                case TokenType::Multiply_Assign:
                case TokenType::Divide_Assign:
                case TokenType::Modulo_Assign: {
                    llvm::Value* ptr = nullptr;

                    if (auto* leftRef = dynamic_cast<const VariableRefNode*>(bin->getLeft())) {
                        auto it = namedValues.find(std::string(leftRef->getName()));
                        if (it != namedValues.end()) {
                            ptr = it->second;
                        }
                    } else if (auto* indexAccess = dynamic_cast<const IndexAccessNode*>(bin->getLeft())) {
                        llvm::Value* targetVal = nullptr;
                        if (auto* varRef = dynamic_cast<const VariableRefNode*>(indexAccess->getTarget())) {
                            auto it = namedValues.find(std::string(varRef->getName()));
                            if (it != namedValues.end()) targetVal = it->second;
                        }
                        llvm::Value* indexVal = emit_node(indexAccess->getIndex());
                        if (targetVal && indexVal) {
                            llvm::Type* elemType = vulpine_type_to_llvm_type(indexAccess->getType());
                            if (!elemType) {
                                elemType = llvm::Type::getInt32Ty(context);
                            }
                            if (auto* alloc = llvm::dyn_cast<llvm::AllocaInst>(targetVal)) {
                                llvm::Value* zero = builder.getInt32(0);
                                ptr = builder.CreateGEP(alloc->getAllocatedType(), targetVal, {zero, indexVal}, "arraygep");
                            } else {
                                ptr = builder.CreateGEP(elemType, targetVal, indexVal, "arraygep");
                            }
                        }
                    }

                    if (!ptr) return nullptr;

                    llvm::Type* valueType = nullptr;
                    if (auto* alloc = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
                        valueType = alloc->getAllocatedType();
                    } else {
                        valueType = rhs->getType(); 
                    }

                    llvm::Value* leftValue = nullptr;
                    if (bin->getOp() != TokenType::Assign) {
                        if (ptr->getType()->isPointerTy()) {
                            leftValue = builder.CreateLoad(valueType, ptr, "loadlhs");
                        } else {
                            leftValue = ptr;
                        }
                    }

                    llvm::Value* result = nullptr;
                    switch (bin->getOp()) {
                        case TokenType::Assign:
                            result = rhs;
                            break;
                        case TokenType::Add_Assign:
                            result = builder.CreateAdd(leftValue, rhs, "addassign");
                            break;
                        case TokenType::Subtract_Assign:
                            result = builder.CreateSub(leftValue, rhs, "subassign");
                            break;
                        case TokenType::Multiply_Assign:
                            result = builder.CreateMul(leftValue, rhs, "mulassign");
                            break;
                        case TokenType::Divide_Assign:
                            result = builder.CreateSDiv(leftValue, rhs, "divassign");
                            break;
                        case TokenType::Modulo_Assign:
                            result = builder.CreateSRem(leftValue, rhs, "modassign");
                            break;
                        default:
                            return nullptr;
                    }

                    if (!result) return nullptr;

                    if (ptr->getType()->isPointerTy()) {
                        builder.CreateStore(result, ptr);
                    }

                    return result;
                }
                case TokenType::AND_BIT:
                    return builder.CreateAnd(lhs, rhs, "andbittmp");
                case TokenType::OR_BIT:
                    return builder.CreateOr(lhs, rhs, "orbitmp");
                case TokenType::XOR:
                    return builder.CreateXor(lhs, rhs, "xortmp");
                default:
                    return nullptr;
            }
        }

        case NodeType::UnaryOp: {
            return emit_unary(static_cast<const UnaryOpNode*>(node));
        }

        case NodeType::If: {
            return emit_if(static_cast<const IfNode*>(node));
        }

        case NodeType::Cast: {
            return emit_cast(static_cast<const CastNode*>(node));
        }

        case NodeType::Call: {
            return emit_call(static_cast<const CallNode*>(node));
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

auto emitter::emit_call(const CallNode* node) -> llvm::Value* {
    llvm::Function* function = module->getFunction(node->getName());
    if (!function) {
        return nullptr;
    }

    std::vector<llvm::Value*> args;
    unsigned idx = 0;
    for (const auto& arg : node->getArguments()) {
        llvm::Type* paramType = function->getFunctionType()->getParamType(idx);
        llvm::Value* argValue = nullptr;

        if (paramType->isPointerTy()) {
            if (arg->nodeType() == NodeType::VariableRef) {
                auto* varRef = static_cast<const VariableRefNode*>(arg.get());
                auto it = namedValues.find(std::string(varRef->getName()));
                if (it == namedValues.end()) return nullptr;
                argValue = it->second;
            } else {
                argValue = emit_node(arg.get());
                if (!argValue) return nullptr;
            }
        } else {
            argValue = emit_node(arg.get());
            if (!argValue) return nullptr;
            if (argValue->getType() != paramType) {
                if (argValue->getType()->isIntegerTy() && paramType->isIntegerTy()) {
                    argValue = builder.CreateIntCast(argValue, paramType, true, "argcast");
                }
            }
        }

        args.push_back(argValue);
        idx++;
    }

    return builder.CreateCall(function, args, "calltmp");
}

auto emitter::emit_function_body(const FunctionNode* node, llvm::Function* fn) -> void {
    namedValues.clear();

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(context, "entry", fn);
    builder.SetInsertPoint(entryBlock);

    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        const auto& param = node->getParams()[idx++];
        arg.setName(param->getName());
        if (param->isParameterCopy()) {
            llvm::AllocaInst* alloc = builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
            builder.CreateStore(&arg, alloc);
            namedValues[param->getName()] = alloc;
        } else {
            namedValues[param->getName()] = &arg;
        }
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

static auto normalize_output_format(std::string_view format) -> std::string {
    std::string normalized{format};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return std::tolower(c); });
    if (normalized == "object" || normalized == "o") return "obj";
    if (normalized == "llvm") return "ll";
    if (normalized.empty()) return "ll";
    return normalized;
}

static auto get_target_machine(bool isWindowsTarget) -> std::unique_ptr<llvm::TargetMachine> {
    std::string targetTripleStr = isWindowsTarget 
        ? "x86_64-w64-mingw32" 
        : llvm::sys::getDefaultTargetTriple();

    llvm::Triple triple(targetTripleStr);

    std::error_code ec;
    std::string error;
    
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        llvm::errs() << "Target lookup failed: " << error << "\n";
        return nullptr;
    }

    llvm::TargetOptions opt;
    auto cpu = "generic";
    auto features = "";

    return std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            triple, 
            cpu, 
            features, 
            opt, 
            llvm::Reloc::PIC_
        )
    );
}

static auto emit_object_for_module(llvm::Module* module, std::string_view filepath, bool isWindowsTarget) -> bool {
    auto targetMachine = get_target_machine(isWindowsTarget);
    if (!targetMachine) return false;

    module->setDataLayout(targetMachine->createDataLayout());
    module->setTargetTriple(targetMachine->getTargetTriple());

    std::error_code ec;
    llvm::raw_fd_ostream dest(std::string(filepath), ec, llvm::sys::fs::OF_None);
    if (ec) return false;

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        return false;
    }

    pass.run(*module);
    dest.flush();
    return true;
}

static auto link_executable(std::string_view objectFile, std::string_view exeFile, bool isWindowsTarget) -> bool {
    std::string command;
    if (isWindowsTarget) {
        command = std::string("x86_64-w64-mingw32-gcc ") + std::string(objectFile) + " -o " + std::string(exeFile);
    } else {
        command = std::string("g++ ") + std::string(objectFile) + " -o " + std::string(exeFile);
    }
    int result = std::system(command.c_str());
    return result == 0;
}

auto emitter::emit(const std::vector<std::unique_ptr<BaseNode>>& nodes, std::string_view filepath, std::string_view format) -> void {
    std::string outputMode = normalize_output_format(format);
    bool isWindowsTarget = (filepath.find(".exe") != std::string_view::npos) || (outputMode == "exe");

    // Pass 1: Setup function declarations
    std::vector<std::pair<const FunctionNode*, llvm::Function*>> functionsToEmit;
    for (const auto& node : nodes) {
        if (node->nodeType() == NodeType::Function) {
            const auto* functionNode = static_cast<const FunctionNode*>(node.get());
            llvm::Function* fn = setup_entry_function(functionNode);
            functionsToEmit.emplace_back(functionNode, fn);
        }
    }

    // Pass 2: Emit global variables & function bodies
    for (const auto& node : nodes) {
        if (node->nodeType() == NodeType::Variable) {
            const auto* variableNode = static_cast<const VariableNode*>(node.get());
            emit_variable(variableNode);
        }
    }

    for (const auto& [functionNode, fn] : functionsToEmit) {
        emit_function_body(functionNode, fn);
    }

    if (outputMode == "ll") {
        std::error_code ec;
        llvm::raw_fd_ostream outFile(filepath, ec, llvm::sys::fs::OF_None);
        if (!ec) {
            module->print(outFile, nullptr);
        }
        return;
    }

    if (outputMode == "obj") {
        emit_object_for_module(module.get(), filepath, isWindowsTarget);
        return;
    }

    if (outputMode == "exe") {
        std::filesystem::path objPath = std::filesystem::path(filepath).replace_extension(".o");
        if (emit_object_for_module(module.get(), objPath.string(), isWindowsTarget)) {
            link_executable(objPath.string(), filepath, isWindowsTarget);
        }
        return;
    }

    std::error_code ec;
    llvm::raw_fd_ostream outFile(filepath, ec, llvm::sys::fs::OF_None);
    if (!ec) {
        module->print(outFile, nullptr);
    }
}