#include "AST.hpp"
#include <fennec.hpp>
#include <lexicon.hpp>
#include <iostream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <fstream>


void print_help() {
    std::println("Fennec Compiler Usage:");
    std::println("  -i <path>    Set input filepath (Required)");
    std::println("  -o <path>    Set output filepath (Optional, default: out.vxe)");
    std::println("  -d           Enable debug mode");
    std::println("  -h           Show this help message");
}

struct CompilerOptions {
    std::string_view input_file;
    std::string_view output_file = "out.vxe"; 
    bool debug_mode = false;
};

void printNode(const BaseNode* node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 2, ' ');

    switch (node->nodeType()) {
        case NodeType_Variable: {
            auto* var = static_cast<const VariableNode*>(node);
            std::cout << indent << "Variable: " << var->getName() << "\n";
            if (var->getValue()) printNode(var->getValue(), depth + 1);
            break;
        }
        case NodeType_Literal: {
            auto* lit = static_cast<const LiteralNode*>(node);
            std::cout << indent << "Literal: " << lit->getValue() << "\n";
            break;
        }
        case NodeType_BinaryOp: {
            auto* bin = static_cast<const BinaryOpNode*>(node);
            std::cout << indent << "BinaryOp\n";
            // Note: You may need to add getLeft/getRight accessors to BinaryOpNode
            break;
        }
        // Add other nodes as you implement them...
        default:
            std::cout << indent << "Unknown Node Type: " << node->nodeType() << "\n";
            break;
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
            // Check if another argument follows the flag safely
            if (i + 1 < args.size()) {
                options.input_file = args[++i]; // Advance loop index to grab value
            } else {
                std::println(stderr, "Error: -i requires a file path value.");
                return 1;
            }
        } 
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < args.size()) {
                options.output_file = args[++i];
            } else {
                std::println(stderr, "Error: -o requires a file path value.");
                return 1;
            }
        } 
        else {
            std::println(stderr, "Error: Unknown option '{}'. Run with -h for help.", arg);
            return 1;
        }
    }

    if (options.input_file.empty()) {
        std::println(stderr, "Error: Missing required input file path (-i).");
        return 1;
    }

    std::ifstream file(std::string(options.input_file), std::ios::binary);

    if (!file.is_open()) {
        std::println(stderr, "Error: Could not open source file '{}'", options.input_file);
        return 1; 
    }

    file >> std::noskipws; 

    std::string source = std::views::istream<char>(file) | std::ranges::to<std::string>();

    std::println("File size: {} bytes", source.size());

    auto fennec = Fennec();

    auto* tokens = fennec.LexerInstance()->lexify(source);

    if (false) {
        for (const auto& entry : *tokens) {
            std::cout << "str: " << entry.str 
                    << " | type: " << TokenToString.at(entry.type) 
                    << "\n";
        }
    }

    auto AST = fennec.ParserInstance()->Parse(tokens);

    std::cout << "AST Size: " << AST.size() << "\n";

    std::cout << "--- AST Tree Structure ---\n";
    for (const auto& node : AST) {
        printNode(node.get());
    }

    return 0;
}