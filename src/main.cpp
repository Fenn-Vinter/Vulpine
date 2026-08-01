#include <AST.hpp>
#include <fennec.hpp>
#include <lexicon.hpp>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
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
    std::cout << "  -o <path>    Set output filepath (Optional, default: out.vxe)\n";
    std::cout << "  -d           Enable debug mode\n";
    std::cout << "  -h           Show this help message\n";
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
            std::cout << indent << "Function (" << tokenTypeToString(TokenType::Fn) << "): " << func->getName() << "()\n";
            
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
        case NodeType::Literal: {
            auto* lit = static_cast<const LiteralNode*>(node);
            std::cout << indent << "Literal: " << lit->getValue() << "\n";
            break;
        }
        case NodeType::BinaryOp: {
            std::cout << indent << "BinaryOp Node\n";
            break;
        }
        case NodeType::UnaryOp: {
            std::cout << indent << "UnaryOp Node\n";
            break;
        }
        case NodeType::Cast: {
            std::cout << indent << "Cast Node\n";
            break;
        }
        case NodeType::VariableRef: {
            std::cout << indent << "VariableRef Node\n";
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

    auto AST = fennec.ParserInstance()->Parse(tokens, options.input_file);

    std::cout << "AST Size: " << AST.size() << "\n";

    std::cout << "--- AST Tree Structure ---\n";
    for (const auto& node : AST) {
        printNode(node.get());
    }

    return 0;
}