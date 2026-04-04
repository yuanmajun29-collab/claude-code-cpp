#include "api/anthropic_client.h"
#include "api/sse_parser.h"
#include "api/auth.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>

using json = nlohmann::json;

namespace claude {

// libcurl header callback
static size_t header_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total_size = size * nmemb;
    str->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Context for real-time SSE streaming
struct StreamingContext {
    SSEParser parser;
    AnthropicClient::StreamCallback callback;
    QueryResponse* response = nullptr;
    ContentBlock* current_block = nullptr;
    std::string current_tool_input_buffer;
    std::string line_buffer;  // Accumulate partial lines
    std::string current_event_type;
    bool* aborted = nullptr;

    void process_sse_line(const std::string& line) {
        auto parsed = parser.parse_line(line);
        if (!parsed) return;

        if (!parsed->event.empty()) {
            current_event_type = parsed->event;
        }
        if (parsed->data.empty()) return;

        auto event = parser.parse_event(current_event_type, parsed->data);
        if (callback) callback(event);

        switch (event.type) {
            case StreamEventType::MessageStart:
                if (event.message) {
                    response->message.id = event.message->id;
                    response->message.model = event.message->model;
                    response->message.usage = event.message->usage;
                }
                break;
            case StreamEventType::MessageDelta:
                if (event.usage_delta) {
                    response->usage.output_tokens += event.usage_delta->output_tokens;
                }
                if (event.delta_text && !event.delta_text->empty() && *event.delta_text != "null") {
                    response->stop_reason = *event.delta_text;
                }
                break;
            case StreamEventType::MessageStop:
                break;
            case StreamEventType::ContentBlockStart:
                if (event.content_block) {
                    response->message.content.push_back(*event.content_block);
                    current_block = &response->message.content.back();
                    if (current_block->type == ContentBlock::Type::ToolUse) {
                        current_tool_input_buffer.clear();
                    }
                }
                break;
            case StreamEventType::ContentBlockDelta:
                if (current_block) {
                    if (current_block->type == ContentBlock::Type::Text && event.delta_text) {
                        current_block->text += *event.delta_text;
                    } else if (current_block->type == ContentBlock::Type::Thinking && event.delta_text) {
                        current_block->thinking += *event.delta_text;
                    } else if (current_block->type == ContentBlock::Type::ToolUse && event.delta_text) {
                        current_tool_input_buffer += *event.delta_text;
                    }
                }
                break;
            case StreamEventType::ContentBlockStop:
                if (current_block && current_block->type == ContentBlock::Type::ToolUse) {
                    current_block->tool_call.input_json = current_tool_input_buffer;
                    current_tool_input_buffer.clear();
                }
                current_block = nullptr;
                break;
            case StreamEventType::Error:
                if (event.error_message) {
                    response->stop_reason = "error";
                    response->message.content.push_back(
                        ContentBlock::make_text(*event.error_message));
                }
                break;
            default:
                break;
        }
        current_event_type.clear();
    }
};

// Streaming write callback: processes SSE events in real-time
static size_t streaming_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* ctx = static_cast<StreamingContext*>(userp);
    size_t total_size = size * nmemb;

    if (ctx->aborted && *ctx->aborted) {
        return 0;  // Abort transfer
    }

    // Append to line buffer and process complete lines
    ctx->line_buffer.append(static_cast<char*>(contents), total_size);

    size_t pos = 0;
    while (pos < ctx->line_buffer.size()) {
        auto nl = ctx->line_buffer.find('\n', pos);
        if (nl == std::string::npos) break;

        std::string line = ctx->line_buffer.substr(pos, nl - pos);
        // Remove \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        ctx->process_sse_line(line);
        pos = nl + 1;
    }

    // Keep remaining partial line
    if (pos > 0) {
        ctx->line_buffer = ctx->line_buffer.substr(pos);
    }

    return total_size;
}

// Non-streaming write callback (collects full body)
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total_size = size * nmemb;
    str->append(static_cast<char*>(contents), total_size);
    return total_size;
}

struct AnthropicClient::Impl {
    CURL* curl = nullptr;

    Impl() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
    }

    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
};

AnthropicClient::AnthropicClient(const std::string& api_key)
    : api_key_(api_key), impl_(std::make_unique<Impl>()) {}

AnthropicClient::~AnthropicClient() = default;

std::string AnthropicClient::load_api_key_from_env() {
    const char* key = getenv("ANTHROPIC_API_KEY");
    return key ? std::string(key) : "";
}

std::string AnthropicClient::load_api_key_from_config() {
    // Delegate to AuthManager
    return AuthManager::load_api_key_from_config();
}

void AnthropicClient::abort() {
    if (impl_ && impl_->curl) {
        // Signal abort - for streaming we check aborted_ flag
        aborted_ = true;
    }
}

std::string AnthropicClient::build_request_body(const QueryOptions& options) {
    json body;

    body["model"] = options.model.model_id;
    body["max_tokens"] = options.model.max_tokens;

    if (options.model.temperature > 0) {
        body["temperature"] = options.model.temperature;
    }

    // Thinking configuration
    if (options.model.thinking == ThinkingConfig::Enabled ||
        options.model.thinking == ThinkingConfig::BudgetTokens) {
        json thinking;
        thinking["type"] = "enabled";
        if (options.model.thinking == ThinkingConfig::BudgetTokens) {
            thinking["budget_tokens"] = options.model.thinking_budget;
        }
        body["thinking"] = thinking;
    }

    // System prompts
    if (!options.system_prompts.empty()) {
        auto& system = body["system"];
        if (options.system_prompts.size() == 1) {
            system = options.system_prompts[0].content;
            if (options.system_prompts[0].cache_control) {
                body["system"] = json::array({
                    {{"type", "text"}, {"text", options.system_prompts[0].content}}
                });
            }
        } else {
            system = json::array();
            for (const auto& sp : options.system_prompts) {
                json item = {{"type", "text"}, {"text", sp.content}};
                if (sp.cache_control) {
                    item["cache_control"] = {{"type", "ephemeral"}};
                }
                system.push_back(item);
            }
        }
    }

    // Messages
    auto& messages = body["messages"] = json::array();
    for (const auto& msg : options.messages) {
        json m;
        switch (msg.role) {
            case MessageRole::User:
                m["role"] = "user";
                break;
            case MessageRole::Assistant:
                m["role"] = "assistant";
                break;
            case MessageRole::Tool:
                m["role"] = "user";
                break;
            default:
                continue;
        }

        auto& content = m["content"] = json::array();
        for (const auto& block : msg.content) {
            switch (block.type) {
                case ContentBlock::Type::Text:
                    content.push_back({{"type", "text"}, {"text", block.text}});
                    break;
                case ContentBlock::Type::ToolUse: {
                    json tool_block = {
                        {"type", "tool_use"},
                        {"id", block.tool_call.id},
                        {"name", block.tool_call.name}
                    };
                    if (!block.tool_call.input_json.empty()) {
                        try {
                            tool_block["input"] = json::parse(block.tool_call.input_json);
                        } catch (...) {
                            tool_block["input"] = {};
                        }
                    } else {
                        tool_block["input"] = {};
                    }
                    content.push_back(tool_block);
                    break;
                }
                case ContentBlock::Type::ToolResult: {
                    json tr = {
                        {"type", "tool_result"},
                        {"tool_use_id", block.tool_result.tool_use_id},
                        {"content", block.tool_result.content}
                    };
                    if (block.tool_result.is_error) {
                        tr["is_error"] = true;
                    }
                    content.push_back(tr);
                    break;
                }
                case ContentBlock::Type::Thinking:
                    content.push_back({{"type", "thinking"}, {"thinking", block.thinking}});
                    break;
                default:
                    break;
            }
        }
        messages.push_back(m);
    }

    // Tools
    if (options.tools_json.is_array() && !options.tools_json.empty()) {
        body["tools"] = options.tools_json;
    }

    // Stop sequences
    if (!options.stop_sequences.empty()) {
        body["stop_sequences"] = options.stop_sequences;
    }

    // Stream mode
    body["stream"] = options.stream;

    return body.dump();
}

QueryResponse AnthropicClient::process_json_response(const std::string& response_body) {
    QueryResponse response;
    try {
        auto j = json::parse(response_body);

        if (j.contains("error")) {
            response.stop_reason = "error";
            response.message.content.push_back(ContentBlock::make_text(
                "API Error: " + j["error"].value("message", "unknown")));
            return response;
        }

        auto& msg_j = j["message"];
        response.message.id = msg_j.value("id", "");
        response.message.model = msg_j.value("model", "");
        response.stop_reason = msg_j.value("stop_reason", "");

        // Parse content blocks
        if (msg_j.contains("content") && msg_j["content"].is_array()) {
            for (const auto& cb : msg_j["content"]) {
                std::string type = cb.value("type", "text");
                ContentBlock block;
                if (type == "text") {
                    block.type = ContentBlock::Type::Text;
                    block.text = cb.value("text", "");
                } else if (type == "tool_use") {
                    block.type = ContentBlock::Type::ToolUse;
                    block.tool_call.id = cb.value("id", "");
                    block.tool_call.name = cb.value("name", "");
                    if (cb.contains("input") && cb["input"].is_object()) {
                        block.tool_call.input_json = cb["input"].dump();
                    }
                }
                response.message.content.push_back(block);
            }
        }

        // Parse usage
        if (msg_j.contains("usage")) {
            auto& u = msg_j["usage"];
            response.usage.input_tokens = u.value("input_tokens", 0);
            response.usage.output_tokens = u.value("output_tokens", 0);
            response.usage.cache_creation_input_tokens = u.value("cache_creation_input_tokens", 0);
            response.usage.cache_read_input_tokens = u.value("cache_read_input_tokens", 0);
        }
    } catch (const json::parse_error& e) {
        response.stop_reason = "error";
        response.message.content.push_back(ContentBlock::make_text(
            "JSON parse error: " + std::string(e.what())));
        spdlog::error("API response parse error: {}", e.what());
    }
    return response;
}

QueryResponse AnthropicClient::process_sse_stream(const std::string& response_body,
                                                    StreamCallback callback) {
    QueryResponse response;
    SSEParser parser;
    ContentBlock* current_block = nullptr;
    std::string current_tool_input_buffer;

    std::istringstream stream(response_body);
    std::string line;
    std::string current_event_type;
    std::string current_data;

    while (std::getline(stream, line)) {
        // Remove \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        auto parsed = parser.parse_line(line);
        if (!parsed) continue;

        if (!parsed->event.empty()) {
            current_event_type = parsed->event;
        }
        if (!parsed->data.empty()) {
            current_data = parsed->data;
        }

        // Process complete events (data field present)
        if (!parsed->data.empty()) {
            auto event = parser.parse_event(current_event_type, parsed->data);

            if (callback) callback(event);

            switch (event.type) {
                case StreamEventType::MessageStart:
                    if (event.message) {
                        response.message.id = event.message->id;
                        response.message.model = event.message->model;
                        response.message.usage = event.message->usage;
                    }
                    break;

                case StreamEventType::MessageDelta:
                    if (event.usage_delta) {
                        response.usage.output_tokens += event.usage_delta->output_tokens;
                    }
                    if (event.delta_text && !event.delta_text->empty() && event.delta_text != "null") {
                        response.stop_reason = *event.delta_text;
                    }
                    break;

                case StreamEventType::MessageStop:
                    break;

                case StreamEventType::ContentBlockStart:
                    if (event.content_block) {
                        response.message.content.push_back(*event.content_block);
                        current_block = &response.message.content.back();

                        // If tool_use block, prepare input accumulator
                        if (current_block->type == ContentBlock::Type::ToolUse) {
                            current_tool_input_buffer.clear();
                        }
                    }
                    break;

                case StreamEventType::ContentBlockDelta:
                    if (current_block) {
                        if (current_block->type == ContentBlock::Type::Text && event.delta_text) {
                            current_block->text += *event.delta_text;
                        } else if (current_block->type == ContentBlock::Type::Thinking && event.delta_text) {
                            current_block->thinking += *event.delta_text;
                        } else if (current_block->type == ContentBlock::Type::ToolUse && event.delta_text) {
                            current_tool_input_buffer += *event.delta_text;
                        }
                    }
                    break;

                case StreamEventType::ContentBlockStop:
                    if (current_block && current_block->type == ContentBlock::Type::ToolUse) {
                        current_block->tool_call.input_json = current_tool_input_buffer;
                        current_tool_input_buffer.clear();
                    }
                    current_block = nullptr;
                    break;

                case StreamEventType::Error:
                    if (event.error_message) {
                        response.stop_reason = "error";
                        response.message.content.push_back(
                            ContentBlock::make_text(*event.error_message));
                    }
                    break;

                default:
                    break;
            }

            // Reset event type for next event
            current_event_type.clear();
        }
    }

    // Check if response has tool calls
    response.stopped_by_tool = (response.stop_reason == "tool_use");

    return response;
}

QueryResponse AnthropicClient::create_message(const QueryOptions& options) {
    if (busy_) {
        QueryResponse err;
        err.stop_reason = "error";
        err.message.content.push_back(ContentBlock::make_text("Another request is in progress"));
        return err;
    }

    auto opts = options;
    opts.stream = false;
    return create_message_stream(opts, nullptr);
}

QueryResponse AnthropicClient::create_message_stream(const QueryOptions& options,
                                                      StreamCallback callback) {
    busy_ = true;
    aborted_ = false;

    auto start_time = std::chrono::steady_clock::now();

    QueryResponse response;

    if (!impl_->curl) {
        response.stop_reason = "error";
        response.message.content.push_back(ContentBlock::make_text("Failed to initialize HTTP client"));
        busy_ = false;
        return response;
    }

    auto* curl = impl_->curl;

    std::string url = base_url_ + "/v1/messages";
    std::string body = build_request_body(options);
    std::string response_body;
    std::string response_headers;

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // POST
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // Request body
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

    // Set content length explicitly
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

    // Headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "anthropic-dangerous-direct-browser-access: true");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Setup streaming or non-streaming write callback
    StreamingContext stream_ctx;
    if (options.stream && callback) {
        stream_ctx.callback = callback;
        stream_ctx.response = &response;
        stream_ctx.aborted = &aborted_;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streaming_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);

    // Timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Proxy
    if (!proxy_.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
    }

    spdlog::debug("Sending API request to {} (model: {}, stream: {})",
                  url, options.model.model_id, options.stream);

    CURLcode res = curl_easy_perform(curl);

    // Cleanup headers
    curl_slist_free_all(headers);

    if (res != CURLE_OK && !aborted_) {
        response.stop_reason = "error";
        std::string err_msg = "HTTP request failed: " + std::string(curl_easy_strerror(res));
        response.message.content.push_back(ContentBlock::make_text(err_msg));
        spdlog::error("API request failed: {}", err_msg);
        busy_ = false;
        return response;
    }

    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200 && !aborted_) {
        spdlog::error("API returned HTTP {}", http_code);
        std::string err_prefix = "API Error (HTTP " + std::to_string(http_code) + "): ";
        std::string err_body = options.stream ? "" : response_body;
        try {
            if (!err_body.empty()) {
                auto j = json::parse(err_body);
                if (j.contains("error")) {
                    std::string msg = j["error"].value("message", err_body);
                    response.stop_reason = "error";
                    response.message.content.push_back(ContentBlock::make_text(err_prefix + msg));
                } else {
                    response.stop_reason = "error";
                    response.message.content.push_back(ContentBlock::make_text(err_prefix + err_body));
                }
            } else {
                response.stop_reason = "error";
                response.message.content.push_back(ContentBlock::make_text(err_prefix + "unknown error"));
            }
        } catch (...) {
            response.stop_reason = "error";
            response.message.content.push_back(ContentBlock::make_text(err_prefix + err_body));
        }
        busy_ = false;
        return response;
    }

    // For non-streaming mode, parse the collected response body
    if (!options.stream || !callback) {
        response = process_json_response(response_body);
    }

    // Calculate duration
    auto end_time = std::chrono::steady_clock::now();
    response.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    response.stopped_by_tool = (response.stop_reason == "tool_use");

    busy_ = false;
    return response;
}

}  // namespace claude
