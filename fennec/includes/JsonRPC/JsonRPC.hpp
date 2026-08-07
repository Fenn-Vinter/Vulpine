#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <iostream>
#include <cctype>

namespace JsonRPC {

static constexpr std::string_view Version = "2.0";

namespace ErrorCode {
    inline constexpr int ParseError           = -32700;
    inline constexpr int InvalidRequest       = -32600;
    inline constexpr int MethodNotFound       = -32601;
    inline constexpr int InvalidParams        = -32602;
    inline constexpr int InternalError        = -32603;
    inline constexpr int ServerNotInitialized = -32002;
    inline constexpr int UnknownErrorCode     = -32001;
    inline constexpr int RequestCancelled     = -32800;
    inline constexpr int ContentModified      = -32801;
}

using MessageId = std::variant<int64_t, std::string>;

inline auto escape_json(std::string_view src) -> std::string {
    std::string escaped;
    escaped.reserve(src.size());
    for (char c : src) {
        switch (c) {
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b";  break;
            case '\f': escaped += "\\f";  break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:   escaped += c;      break;
        }
    }
    return escaped;
}

inline auto format_id(const MessageId& id) -> std::string {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::format("\"{}\"", escape_json(arg));
        }
    }, id);
}

inline auto frame_message(std::string_view body) -> std::string {
    return std::format(
        "Content-Length: {}\r\n"
        "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
        "\r\n"
        "{}",
        body.size(), body
    );
}

class Request {
    std::optional<MessageId> m_id{std::nullopt};
    std::string m_method{};
    std::string m_paramsJson{"{}"};

public:
    Request() = default;
    
    Request(MessageId id, std::string_view method, std::string_view paramsJson = "{}")
        : m_id(std::move(id)), m_method(method), m_paramsJson(paramsJson) {}

    Request(std::string_view method, std::string_view paramsJson = "{}")
        : m_id(std::nullopt), m_method(method), m_paramsJson(paramsJson) {}

    ~Request() = default;

    void set_id(MessageId id) { m_id = std::move(id); }
    void clear_id() { m_id = std::nullopt; }
    [[nodiscard]] bool is_notification() const { return !m_id.has_value(); }

    void set_method(std::string_view method) { m_method = method; }
    void set_params_json(std::string_view paramsJson) { m_paramsJson = paramsJson; }

    [[nodiscard]] auto get_method() const -> const std::string& { return m_method; }
    [[nodiscard]] auto get_params_json() const -> const std::string& { return m_paramsJson; }
    [[nodiscard]] auto get_id() const -> const std::optional<MessageId>& { return m_id; }

    [[nodiscard]] auto get_body() const -> std::string {
        std::string id_field;
        if (m_id.has_value()) {
            id_field = std::format("  \"id\": {},\n", format_id(*m_id));
        }

        return std::format(
            "{{\n"
            "  \"jsonrpc\": \"{}\",\n"
            "{}"
            "  \"method\": \"{}\",\n"
            "  \"params\": {}\n"
            "}}",
            Version,
            id_field,
            escape_json(m_method),
            m_paramsJson.empty() ? "{}" : m_paramsJson
        );
    }

    [[nodiscard]] auto get_format() const -> std::string {
        return frame_message(get_body());
    }
};

class Response {
    MessageId m_id{int64_t{0}};
    std::string m_resultJson{"null"};
    bool m_isError{false};
    int m_errorCode{0};
    std::string m_errorMessage{};

public:
    Response() = default;
    explicit Response(MessageId id) : m_id(std::move(id)) {}

    void set_id(MessageId id) { m_id = std::move(id); }

    void set_result(std::string_view resultJson) {
        m_resultJson = resultJson;
        m_isError = false;
    }

    void set_error(int code, std::string_view message) {
        m_errorCode = code;
        m_errorMessage = message;
        m_isError = true;
    }

    [[nodiscard]] auto get_body() const -> std::string {
        if (m_isError) {
            return std::format(
                "{{\n"
                "  \"jsonrpc\": \"{}\",\n"
                "  \"id\": {},\n"
                "  \"error\": {{\n"
                "    \"code\": {},\n"
                "    \"message\": \"{}\"\n"
                "  }}\n"
                "}}",
                Version,
                format_id(m_id),
                m_errorCode,
                escape_json(m_errorMessage)
            );
        }

        return std::format(
            "{{\n"
            "  \"jsonrpc\": \"{}\",\n"
            "  \"id\": {},\n"
            "  \"result\": {}\n"
            "}}",
            Version,
            format_id(m_id),
            m_resultJson.empty() ? "null" : m_resultJson
        );
    }

    [[nodiscard]] auto get_format() const -> std::string {
        return frame_message(get_body());
    }
};

class StreamParser {
private:
    static auto skip_whitespace(std::string_view sv, size_t pos) -> size_t {
        while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\n' || sv[pos] == '\r')) {
            pos++;
        }
        return pos;
    }

    static auto parse_string(std::string_view sv, size_t pos, std::string& out_str) -> size_t {
        if (pos >= sv.size() || sv[pos] != '"') return pos;
        pos++; // Skip opening quote
        out_str.clear();

        while (pos < sv.size()) {
            char c = sv[pos++];
            if (c == '\\' && pos < sv.size()) {
                char escaped = sv[pos++];
                switch (escaped) {
                    case '"':  out_str += '"';  break;
                    case '\\': out_str += '\\'; break;
                    case '/':  out_str += '/';  break;
                    case 'b':  out_str += '\b'; break;
                    case 'f':  out_str += '\f'; break;
                    case 'n':  out_str += '\n'; break;
                    case 'r':  out_str += '\r'; break;
                    case 't':  out_str += '\t'; break;
                    default:   out_str += escaped; break;
                }
            } else if (c == '"') {
                return pos;
            } else {
                out_str += c;
            }
        }
        return pos;
    }

    static auto extract_raw_value(std::string_view sv, size_t pos) -> std::pair<std::string_view, size_t> {
        pos = skip_whitespace(sv, pos);
        if (pos >= sv.size()) return { {}, pos };

        size_t start = pos;
        if (sv[pos] == '{' || sv[pos] == '[') {
            char openChar = sv[pos];
            char closeChar = (openChar == '{') ? '}' : ']';
            int depth = 0;
            bool inString = false;

            while (pos < sv.size()) {
                char c = sv[pos];
                if (c == '\\' && inString) {
                    pos += 2;
                    continue;
                }
                if (c == '"') {
                    inString = !inString;
                } else if (!inString) {
                    if (c == openChar) depth++;
                    else if (c == closeChar) {
                        depth--;
                        if (depth == 0) {
                            pos++;
                            return { sv.substr(start, pos - start), pos };
                        }
                    }
                }
                pos++;
            }
        } else if (sv[pos] == '"') {
            std::string dummy;
            size_t endPos = parse_string(sv, pos, dummy);
            return { sv.substr(start, endPos - start), endPos };
        } else {
            while (pos < sv.size() && sv[pos] != ',' && sv[pos] != '}' && sv[pos] != ']' && !std::isspace(sv[pos])) {
                pos++;
            }
            return { sv.substr(start, pos - start), pos };
        }

        return { {}, pos };
    }

public:
    static auto read_next_frame() -> std::optional<std::string> {
        size_t contentLength = 0;
        std::string line;

        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                break;
            }

            constexpr std::string_view lengthHeader = "Content-Length: ";
            if (line.rfind(lengthHeader, 0) == 0) {
                try {
                    contentLength = std::stoull(line.substr(lengthHeader.size()));
                } catch (...) {
                    return std::nullopt;
                }
            }
        }

        if (contentLength == 0 || std::cin.fail()) {
            return std::nullopt;
        }

        std::string body(contentLength, '\0');
        std::cin.read(&body[0], contentLength);

        if (std::cin.gcount() != static_cast<std::streamsize>(contentLength)) {
            return std::nullopt;
        }

        return body;
    }

    static auto parse_request(std::string_view rawJson) -> std::optional<Request> {
        Request req;
        size_t pos = skip_whitespace(rawJson, 0);
        if (pos >= rawJson.size() || rawJson[pos] != '{') return std::nullopt;
        pos++;

        bool foundMethod = false;

        while (pos < rawJson.size()) {
            pos = skip_whitespace(rawJson, pos);
            if (pos >= rawJson.size() || rawJson[pos] == '}') break;

            if (rawJson[pos] != '"') {
                pos++;
                continue;
            }

            std::string key;
            pos = parse_string(rawJson, pos, key);
            pos = skip_whitespace(rawJson, pos);

            if (pos >= rawJson.size() || rawJson[pos] != ':') break;
            pos++; // skip ':'

            if (key == "method") {
                pos = skip_whitespace(rawJson, pos);
                if (pos < rawJson.size() && rawJson[pos] == '"') {
                    std::string methodVal;
                    pos = parse_string(rawJson, pos, methodVal);
                    req.set_method(methodVal);
                    foundMethod = true;
                }
            } else if (key == "id") {
                pos = skip_whitespace(rawJson, pos);
                if (pos < rawJson.size()) {
                    if (rawJson[pos] == '"') {
                        std::string idStr;
                        pos = parse_string(rawJson, pos, idStr);
                        req.set_id(idStr);
                    } else if (std::isdigit(rawJson[pos]) || rawJson[pos] == '-') {
                        auto [numSv, nextPos] = extract_raw_value(rawJson, pos);
                        try {
                            req.set_id(static_cast<int64_t>(std::stoll(std::string(numSv))));
                        } catch (...) {
                            req.set_id(int64_t{0});
                        }
                        pos = nextPos;
                    }
                }
            } else if (key == "params") {
                auto [paramsSv, nextPos] = extract_raw_value(rawJson, pos);
                req.set_params_json(paramsSv);
                pos = nextPos;
            } else {
                auto [_, nextPos] = extract_raw_value(rawJson, pos);
                pos = nextPos;
            }

            pos = skip_whitespace(rawJson, pos);
            if (pos < rawJson.size() && rawJson[pos] == ',') {
                pos++;
            }
        }

        if (!foundMethod) return std::nullopt;
        return req;
    }
};

}