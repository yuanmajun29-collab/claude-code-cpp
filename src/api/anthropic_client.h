#pragma once

#include "engine/message.h"
#include "api/sse_parser.h"
#include <string>
#include <functional>
#include <memory>
#include <optional>

namespace claude {

// Forward declarations
class AnthropicClient {
public:
    explicit AnthropicClient(const std::string& api_key);
    ~AnthropicClient();

    // Non-streaming request
    QueryResponse create_message(const QueryOptions& options);

    // Streaming request (callback mode)
    using StreamCallback = std::function<void(const StreamEvent&)>;
    QueryResponse create_message_stream(const QueryOptions& options, StreamCallback callback);

    // Configuration
    void set_base_url(const std::string& url) { base_url_ = url; }
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }
    void set_proxy(const std::string& proxy) { proxy_ = proxy; }
    void set_api_key(const std::string& key) { api_key_ = key; }

    // Getters
    const std::string& base_url() const { return base_url_; }
    const std::string& api_key() const { return api_key_; }

    // Load API key from environment
    static std::string load_api_key_from_env();
    static std::string load_api_key_from_config();

    // Abort current request
    void abort();

    // Check if currently in a request
    bool is_busy() const { return busy_; }

private:
    // Build the JSON request body
    std::string build_request_body(const QueryOptions& options);

    // Process non-streaming JSON response
    QueryResponse process_json_response(const std::string& response_body);

    std::string api_key_;
    std::string base_url_ = "https://api.anthropic.com";
    int timeout_seconds_ = 120;
    std::string proxy_;
    bool busy_ = false;
    bool aborted_ = false;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace claude
