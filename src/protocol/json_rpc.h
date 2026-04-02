#pragma once

#include <string>
#include <map>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace claude {

// JSON-RPC 2.0 error
struct JsonRpcError {
    int code;
    std::string message;
    std::optional<nlohmann::json> data;
};

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::string id;
    std::string method;
    nlohmann::json params;
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    std::string id;
    std::optional<nlohmann::json> result;
    std::optional<JsonRpcError> error;
};

// Standard JSON-RPC error codes
namespace JsonRpcErrorCode {
    constexpr int ParseError     = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams  = -32602;
    constexpr int InternalError  = -32603;
}

// JSON-RPC message handler callback
using JsonRpcMethodHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

// JSON-RPC protocol handler
class JsonRpc {
public:
    JsonRpc() = default;
    ~JsonRpc() = default;

    // Register a method handler
    void register_method(const std::string& method, JsonRpcMethodHandler handler);

    // Unregister a method
    void unregister_method(const std::string& method);

    // Check if method is registered
    bool has_method(const std::string& method) const;

    // Process a raw JSON string and return response
    std::string process(const std::string& raw_message);

    // Build a request
    std::string build_request(const std::string& id, const std::string& method,
                               const nlohmann::json& params = nullptr);

    // Build a notification (no id, no response expected)
    std::string build_notification(const std::string& method,
                                    const nlohmann::json& params = nullptr);

    // Build a response
    std::string build_response(const std::string& id, const nlohmann::json& result);

    // Build an error response
    std::string build_error(const std::string& id, int code, const std::string& message,
                             const nlohmann::json& data = nullptr);

    // Parse a JSON string into a request
    std::optional<JsonRpcRequest> parse_request(const std::string& raw_message);

    // Parse a JSON string into a response
    std::optional<JsonRpcResponse> parse_response(const std::string& raw_message);

    // Generate a unique ID
    std::string generate_id();

    // Get registered method count
    size_t method_count() const;

private:
    // Handle a single request (internal)
    nlohmann::json handle_single_raw(const std::string& raw_message);

    std::map<std::string, JsonRpcMethodHandler> handlers_;
    uint64_t next_id_ = 1;
};

}  // namespace claude
