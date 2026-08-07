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
#include <sstream>
#include <algorithm>
#include <cctype>
#include <JsonRPC/JsonRPC.hpp>

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
    std::cout << "  -lsp         Starts the Fennec compiler as a Language Server Protocol\n";
}

static auto project_output_format(const std::vector<std::unique_ptr<BaseNode>>& nodes) -> std::string_view {
    for (const auto& node : nodes) {
        if (node->nodeType() == NodeType::Project) {
            return static_cast<const ProjectNode*>(node.get())->getOutputType();
        }
    }
    return {};
}

static auto normalize_format(std::string_view format) -> std::string {
    std::string normalized{format};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return std::tolower(c); });
    if (normalized == "o") normalized = "obj";
    if (normalized == "object") normalized = "obj";
    if (normalized == "llvm") normalized = "ll";
    return normalized;
}

static auto format_extension(std::string_view format) -> std::string_view {
    if (format == "exe") return "exe";
    if (format == "obj") return "o";
    return "ll";
}

static auto resolve_output_path(std::string_view outputPath, std::string_view inputPath, std::string_view outputFormat) -> std::string {
    namespace fs = std::filesystem;

    std::string format = normalize_format(outputFormat);
    if (format.empty()) format = "ll";
    fs::path out(outputPath);
    fs::path in(inputPath);
    std::string_view extension = format_extension(format);

    if (outputPath.empty()) {
        out = in.stem();
        out.replace_extension(extension);
        return out.string();
    }

    if (out.empty() || out == "." || out == "./" || out == ".\\" || outputPath.back() == '/' || outputPath.back() == '\\') {
        out = fs::path(outputPath) / in.stem();
        out.replace_extension(extension);
        return out.string();
    }

    if (fs::exists(out) && fs::is_directory(out)) {
        out /= in.stem();
        out.replace_extension(extension);
        return out.string();
    }

    if (out.extension().empty()) {
        out.replace_extension(extension);
    } else {
        auto currentExt = out.extension().string();
        if (format == "exe" && currentExt != ".exe") {
            out.replace_extension(extension);
        } else if (format == "obj" && currentExt != ".o") {
            out.replace_extension(extension);
        } else if (format == "ll" && currentExt != ".ll") {
            out.replace_extension(extension);
        }
    }

    return out.string();
}

struct CompilerOptions {
    std::string input_file;
    std::string output_file;
    bool debug_mode = false;
};

void printNode(const BaseNode* node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 2, ' ');

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
            
            for (const auto& param : func->getParams()) {
                printNode(param.get(), depth + 1);
            }
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
            if (ret->getExpression()) {
                printNode(ret->getExpression(), depth + 1);
            }
            break;
        }
        case NodeType::Call: {
            auto* call = static_cast<const CallNode*>(node);
            std::cout << indent << "Call: " << call->getName(); 
            if (call->getType() != TokenType::Invalid) {
                std::cout << " -> " << tokenTypeToString(call->getType());
            }
            std::cout << "()\n";
            
            for (const auto& arg : call->getArguments()) {
                printNode(arg.get(), depth + 1);
            }
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
            std::cout << indent << "Unknown Node (0x" << std::hex 
                      << static_cast<unsigned int>(node->nodeType()) 
                      << std::dec << ")\n";
            break;
        }
    }
}

static std::string unescapeJsonString(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            switch (input[i + 1]) {
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                default:   result += input[i]; break;
            }
        } else {
            result += input[i];
        }
    }
    return result;
}

static void publishDiagnostics(std::string_view rawParams) {
    std::string uri;
    auto uriPos = rawParams.find("\"uri\"");
    if (uriPos != std::string_view::npos) {
        auto start = rawParams.find('"', uriPos + 5);
        if (start != std::string_view::npos) {
            auto end = rawParams.find('"', start + 1);
            if (end != std::string_view::npos) {
                uri = std::string(rawParams.substr(start + 1, end - start - 1));
            }
        }
    }
    if (uri.empty()) return;

    std::string source;
    auto textPos = rawParams.find("\"text\"");
    if (textPos != std::string_view::npos) {
        auto start = rawParams.find('"', textPos + 6);
        if (start != std::string_view::npos) {
            size_t end = start + 1;
            while (end < rawParams.size()) {
                if (rawParams[end] == '\\') {
                    end += 2;
                    continue;
                }
                if (rawParams[end] == '"') break;
                end++;
            }
            if (end < rawParams.size()) {
                std::string rawSource = std::string(rawParams.substr(start + 1, end - start - 1));
                source = unescapeJsonString(rawSource);
            }
        }
    }

    if (source.empty()) {
        std::string filePath = uri;
        if (filePath.starts_with("file://")) filePath = filePath.substr(7);
        std::ifstream file(filePath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            source = buffer.str();
        }
    }

    auto fennec = Fennec();
    auto* tokens = fennec.LexerInstance()->lexify(source);
    auto parser = fennec.ParserInstance();
    auto AST = parser->Parse(tokens, uri);

    std::string diagList = "[";
    if (parser->hasErrors()) {
        bool first = true;
        for (const auto& error : parser->getErrors()) {
            if (!first) diagList += ",";
            first = false;
            diagList += std::format(
                "{{"
                "\"range\":{{\"start\":{{\"line\":0,\"character\":0}},\"end\":{{\"line\":0,\"character\":100}}}},"
                "\"severity\":1,"
                "\"source\":\"fennec\","
                "\"message\":\"{}\""
                "}}",
                JsonRPC::escape_json(error)
            );
        }
    }
    diagList += "]";

    std::string paramsJson = std::format(
        "{{\"uri\": \"{}\", \"diagnostics\": {}}}",
        JsonRPC::escape_json(uri),
        diagList
    );

    JsonRPC::Request notification(std::string_view("textDocument/publishDiagnostics"), std::string_view(paramsJson));
    std::cout << notification.get_format() << std::flush;
}

static void runLspLoop() {
    std::cin.sync_with_stdio(false);
    std::cout.sync_with_stdio(false);

    while (std::cin.good()) {
        auto bodyOpt = JsonRPC::StreamParser::read_next_frame();
        if (!bodyOpt.has_value()) {
            break;
        }

        auto reqOpt = JsonRPC::StreamParser::parse_request(*bodyOpt);
        if (!reqOpt.has_value()) {
            continue;
        }

        const auto& req = *reqOpt;

        std::string dummyBody = req.get_body();
        
        if (dummyBody.find("\"initialize\"") != std::string::npos) {
            JsonRPC::Response res;
            res.set_result(
                "{\n"
                "  \"capabilities\": {\n"
                "    \"textDocumentSync\": 1,\n"
                "    \"hoverProvider\": false\n"
                "  }\n"
                "}"
            );
            std::cout << res.get_format() << std::flush;
        } 
        else if (dummyBody.find("textDocument/didOpen") != std::string::npos ||
                 dummyBody.find("textDocument/didChange") != std::string::npos ||
                 dummyBody.find("textDocument/didSave") != std::string::npos) {
            publishDiagnostics(dummyBody);
        }
        else if (dummyBody.find("\"shutdown\"") != std::string::npos) {
            JsonRPC::Response res;
            res.set_result("null");
            std::cout << res.get_format() << std::flush;
        }
        else if (dummyBody.find("\"exit\"") != std::string::npos) {
            break;
        }
    }
}

auto main(int argc, char* argv[]) -> int {
    if (argc <= 1) {
        print_help();
        return 0;
    }

    bool isLSP = false;
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
        else if (arg == "-lsp" || arg == "--lsp") {
            isLSP = true;
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'. Run with -h for help.\n";
            return 1;
        }
    }

    if (isLSP) {
        runLspLoop();
        return 0;
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
    codegen.emit(AST, effectiveOutput, format);

    std::cout << "Generated output: " << effectiveOutput << "\n";
    return 0;
}