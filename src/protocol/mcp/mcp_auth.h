#pragma once

#include <string>
#include <map>
#include <optional>

namespace claude {
namespace mcp {

// MCP Authentication — handles auth headers for SSE and streamable HTTP transports
class McpAuth {
public:
    McpAuth() = default;

    // Set auth token (e.g., from config or environment)
    void set_token(const std::string& token) { token_ = token; }

    // Get auth token
    std::optional<std::string> token() const {
        if (token_.empty()) return std::nullopt;
        return token_;
    }

    // Check if auth is configured
    bool has_auth() const { return !token_.empty(); }

    // Build Authorization header value
    // Supports: Bearer <token>, Basic <base64>, or custom scheme
    std::string auth_header() const {
        if (token_.empty()) return "";

        // Auto-detect scheme
        if (token_.find("Bearer ") == 0 || token_.find("bearer ") == 0) {
            return token_;
        }
        if (token_.find("Basic ") == 0 || token_.find("basic ") == 0) {
            return token_;
        }
        // Default to Bearer
        return "Bearer " + token_;
    }

    // Add auth headers to a curl slist
    struct CurlHeaders;  // Forward declaration (avoid curl include)

    // Build headers map for SSE transport
    std::map<std::string, std::string> headers() const {
        std::map<std::string, std::string> h;
        if (has_auth()) {
            h["Authorization"] = auth_header();
        }
        return h;
    }

    // Load token from environment variable
    bool load_from_env(const std::string& env_var = "MCP_AUTH_TOKEN");

private:
    std::string token_;
};

}  // namespace mcp
}  // namespace claude
