#include <AST.hpp>
#include <fennec.hpp>
#include <lexicon.hpp>
#include <emitter.hpp>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>

std::string_view tokenTypeToString(TokenType type) {
    auto it = TokenToString.find(type);
    if (it != TokenToString.end()) {
        return it->second;
    }
    return "Unknown_Token";
}

void print_help() {
    std::cout << "Fennec Compiler Usage:\n";
    std::cout << "  -i <path>    Set input filepath (Required)\n";
    std::cout << "  -o <path>    Set output filepath or directory for generated IR (Optional, default: out.ll)\n";
    std::cout << "  -d           Enable debug mode\n";
    std::cout << "  -h           Show this help message\n";
}

static auto project_output_format(const std::vector<std::unique_ptr<BaseNode>>& nodes) -> std::string_view {
    for (const auto& node : nodes) {
        if (node->nodeType() == NodeType::Project) {
            return static_cast<const ProjectNode*>(node.get())->getOutputType();
        }
    }
    return {};
}

static auto resolve_output_path(std::string_view outputPath, std::string_view inputPath, std::string_view /*outputFormat*/) -> std::string {
    namespace fs = std::filesystem;

    fs::path out(outputPath);
    fs::path in(inputPath);
    std::string_view irExtension = "ll";

    if (out.empty() || out == "." || out == "./" || out == ".\\" || outputPath.back() == '/' || outputPath.back() == '\\') {
        out = fs::path(outputPath) / in.stem();
        out.replace_extension(irExtension);
        return out.string();
    }

    if (fs::exists(out) && fs::is_directory(out)) {
        out /= in.stem();
        out.replace_extension(irExtension);
        return out.string();
    }

    if (out.extension().empty()) {
        out.replace_extension(irExtension);
    }

    return out.string();
}

struct CompilerOptions {
    std::string_view input_file;
    std::string_view output_file = "out.vxe"; 
    bool debug_mode = false;
};

void printNode(const BaseNode* node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 2, ' ');

    // Updated to use the type-safe enum class 'NodeType'
    switch (node->nodeType()) {
        case NodeType::Project: {
            auto* proj = static_cast<const ProjectNode*>(node);
            std::cout << indent << "Project: " << proj->getProjectName() << "\n";
            std::cout << indent << "  Version: " << proj->getVulpineVersion() << "\n";
            std::cout << indent << "  Entry: " << proj->getEntryPoint() << "\n";
            break;
        }
        case NodeType::Function: {
            auto* func = static_cast<const FunctionNode*>(node);
            std::cout << indent << "Function (" << tokenTypeToString(TokenType::Fn) << "): " << func->getName();
            if (func->getReturnType() != TokenType::Invalid) {
                std::cout << " -> " << tokenTypeToString(func->getReturnType());
            }
            std::cout << "()\n";
            
            // Print function parameters
            for (const auto& param : func->getParams()) {
                printNode(param.get(), depth + 1);
            }
            // Print function body statements
            for (const auto& stmt : func->getBody()) {
                printNode(stmt.get(), depth + 1);
            }
            break;
        }
        case NodeType::Block: {
            auto* block = static_cast<const BlockNode*>(node);
            std::cout << indent << "Block {\n";
            for (const auto& stmt : block->getBody()) {
                printNode(stmt.get(), depth + 1);
            }
            std::cout << indent << "}\n";
            break;
        }
        case NodeType::Variable: {
            auto* var = static_cast<const VariableNode*>(node);
            std::cout << indent << "Variable (" << tokenTypeToString(TokenType::Let) << "): " << var->getName();
            
            if (var->getDeclaredType() != TokenType::Invalid) {
                std::cout << " : " << tokenTypeToString(var->getDeclaredType());
            }
            std::cout << "\n";

            if (var->getValue()) {
                printNode(var->getValue(), depth + 1);
            }
            break;
        }
        case NodeType::If: {
            auto* ifNode = static_cast<const IfNode*>(node);
            std::cout << indent << "If Statement\n";
            if (ifNode->getCondition()) {
                std::cout << indent << "  Condition:\n";
                printNode(ifNode->getCondition(), depth + 2);
            }
            std::cout << indent << "  Body {\n";
            for (const auto& stmt : ifNode->getBody()) {
                printNode(stmt.get(), depth + 2);
            }
            std::cout << indent << "  }\n";
            break;
        }
        case NodeType::Return: {
            auto* ret = static_cast<const ReturnNode*>(node);
            std::cout << indent << "Return\n";
            printNode(ret->getExpression(), depth + 1);
            break;
        }
        case NodeType::Literal: {
            auto* lit = static_cast<const LiteralNode*>(node);
            std::cout << indent << "Literal(" << tokenTypeToString(lit->getLiteralType()) << "): " << lit->getValue() << "\n";
            break;
        }
        case NodeType::BinaryOp: {
            auto* bin = static_cast<const BinaryOpNode*>(node);
            std::cout << indent << "BinaryOp(" << tokenTypeToString(bin->getOp()) << ") : " << tokenTypeToString(bin->getType()) << "\n";
            printNode(bin->getLeft(), depth + 1);
            printNode(bin->getRight(), depth + 1);
            break;
        }
        case NodeType::UnaryOp: {
            auto* unary = static_cast<const UnaryOpNode*>(node);
            std::cout << indent << "UnaryOp(" << tokenTypeToString(unary->getOp()) << ") : " << tokenTypeToString(unary->getType()) << "\n";
            printNode(unary->getOperand(), depth + 1);
            break;
        }
        case NodeType::Cast: {
            auto* cast = static_cast<const CastNode*>(node);
            std::cout << indent << "Cast(to " << tokenTypeToString(cast->getType()) << ")\n";
            printNode(cast->getExpression(), depth + 1);
            break;
        }
        case NodeType::VariableRef: {
            auto* ref = static_cast<const VariableRefNode*>(node);
            std::cout << indent << "VariableRef: " << ref->getName();
            if (ref->getType() != TokenType::Invalid) {
                std::cout << " : " << tokenTypeToString(ref->getType());
            }
            std::cout << "\n";
            break;
        }
        case NodeType::Base: {
            std::cout << indent << "Base Node\n";
            break;
        }
        default: {
            // Cast enum value to unsigned int for hex formatting
            std::cout << indent << "Unknown Node (0x" << std::hex 
                      << static_cast<unsigned int>(node->nodeType()) 
                      << std::dec << ")\n";
            break;
        }
    }
}

auto main(int argc, char* argv[]) -> int {
    if (argc <= 1) {
        print_help();
        return 0;
    }

    CompilerOptions options;
    auto args = std::span(argv, argc);

    for (size_t i = 1; i < args.size(); ++i) {
        std::string_view arg(args[i]);

        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        } 
        else if (arg == "-d" || arg == "--debug") {
            options.debug_mode = true;
        } 
        else if (arg == "-i" || arg == "--input") {
            if (i + 1 < args.size()) {
                options.input_file = args[++i];
            } else {
                std::cerr << "Error: -i requires a file path value.\n";
                return 1;
            }
        } 
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < args.size()) {
                options.output_file = args[++i];
            } else {
                std::cerr << "Error: -o requires a file path value.\n";
                return 1;
            }
        } 
        else {
            std::cerr << "Error: Unknown option '" << arg << "'. Run with -h for help.\n";
            return 1;
        }
    }

    if (options.input_file.empty()) {
        std::cerr << "Error: Missing required input file path (-i).\n";
        return 1;
    }

    std::ifstream file(std::string(options.input_file), std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open source file '" << options.input_file << "'\n";
        return 1; 
    }

    file >> std::noskipws; 

    std::string source = std::views::istream<char>(file) | std::ranges::to<std::string>();

    std::cout << "File size: " << source.size() << " bytes\n";

    auto fennec = Fennec();

    auto* tokens = fennec.LexerInstance()->lexify(source);

    if (options.debug_mode) {
        std::cout << "--- Lexer Tokens ---\n";
        for (const auto& entry : *tokens) {
            std::cout << "str: " << entry.str 
                      << " | type: " << tokenTypeToString(entry.type) 
                      << "\n";
        }
    }

    auto parser = fennec.ParserInstance();
    auto AST = parser->Parse(tokens, options.input_file);

    std::cout << "AST Size: " << AST.size() << "\n";

    if (parser->hasErrors()) {
        std::cout << "--- Parse Errors ---\n";
        for (auto const& error : parser->getErrors()) {
            std::cout << error << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "--- AST Tree Structure ---\n";
    for (const auto& node : AST) {
        printNode(node.get());
    }

    if (parser->hasErrors()) {
        std::cerr << "Compilation aborted due to parse errors. No output generated.\n";
        return 1;
    }

    std::string_view format = project_output_format(AST);
    std::string effectiveOutput = resolve_output_path(options.output_file, options.input_file, format);

    emitter codegen;
    codegen.emit(AST, effectiveOutput);

    std::cout << "Generated output: " << effectiveOutput << "\n";
    return 0;
}