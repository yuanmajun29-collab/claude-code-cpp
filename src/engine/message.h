#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <cstdint>

namespace claude {

// ============================================================
// Message Types
// ============================================================

enum class MessageRole {
    System,
    User,
    Assistant,
    Tool
};

enum class ToolResultStatus {
    Success,
    Error,
    Cancelled
};

// Token usage tracking
struct TokenUsage {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cache_creation_input_tokens = 0;
    int64_t cache_read_input_tokens = 0;

    int64_t total_input() const {
        return input_tokens + cache_creation_input_tokens + cache_read_input_tokens;
    }
    int64_t total() const {
        return total_input() + output_tokens;
    }

    std::string to_string() const;
    double estimate_cost_usd() const;
};

// Tool call from model
struct ToolCall {
    std::string id;
    std::string name;
    std::string input_json;  // JSON string of input parameters
};

// Tool result sent back to model
struct ToolResult {
    std::string tool_use_id;
    ToolResultStatus status = ToolResultStatus::Success;
    std::string content;
    bool is_error = false;
};

// Content block variants for assistant messages
struct ContentBlock {
    enum class Type { Text, ToolUse, Thinking, Image };
    Type type;
    std::string text;
    ToolCall tool_call;
    std::string thinking;
    std::string image_url;
};

// Base message
struct Message {
    std::string id;
    MessageRole role;
    std::vector<ContentBlock> content;
    TokenUsage usage;
    std::string model;
    std::chrono::system_clock::time_point timestamp;

    // Static factory methods
    static Message system(const std::string& text);
    static Message user(const std::string& text);
    static Message assistant(const std::string& text, const std::string& model_id = "");
    static Message tool_result(const std::string& tool_use_id, const std::string& content, bool is_error = false);

    // Convenience
    std::string text_content() const {
        std::string result;
        for (const auto& block : content) {
            if (block.type == ContentBlock::Type::Text) {
                result += block.text;
            }
        }
        return result;
    }

    std::vector<ToolCall> tool_calls() const {
        std::vector<ToolCall> calls;
        for (const auto& block : content) {
            if (block.type == ContentBlock::Type::ToolUse) {
                calls.push_back(block.tool_call);
            }
        }
        return calls;
    }
};

// System prompt
struct SystemPrompt {
    std::string content;
    std::optional<int64_t> cache_control;  // ephemeral breakpoint
};

// ============================================================
// Stream Event Types (SSE from Anthropic API)
// ============================================================

enum class StreamEventType {
    MessageStart,
    MessageDelta,
    MessageStop,
    ContentBlockStart,
    ContentBlockDelta,
    ContentBlockStop,
    ToolUseStart,
    ToolUseDelta,
    ToolUseStop,
    ThinkingDelta,
    Error
};

struct StreamEvent {
    StreamEventType type;
    std::string data;  // Raw JSON payload
    std::optional<Message> message;
    std::optional<ContentBlock> content_block;
    std::optional<std::string> delta_text;
    std::optional<TokenUsage> usage_delta;
    std::optional<std::string> error_message;
};

// ============================================================
// API Request/Response Types
// ============================================================

enum class ThinkingConfig {
    Disabled,
    Enabled,
    BudgetTokens
};

struct ModelConfig {
    std::string model_id;           // e.g. "claude-sonnet-4-20250514"
    int64_t max_tokens = 8192;
    ThinkingConfig thinking = ThinkingConfig::Disabled;
    int64_t thinking_budget = 10000;
    double temperature = 0.0;
    int top_k = 0;  // 0 = auto
    int top_p = 0;  // 0 = auto
};

struct QueryOptions {
    ModelConfig model;
    std::vector<SystemPrompt> system_prompts;
    std::vector<Message> messages;
    std::vector<std::string> stop_sequences;
    bool stream = true;

    std::string to_debug_string() const;
};

struct QueryResponse {
    Message message;
    TokenUsage usage;
    bool stopped_by_tool = false;
    std::string stop_reason;  // "end_turn", "tool_use", "max_tokens"
    double duration_ms = 0.0;
};

// ============================================================
// Session Types
// ============================================================

struct SessionConfig {
    std::string session_id;
    std::string working_directory;
    std::vector<std::string> additional_directories;
    ModelConfig model;
    std::string permission_mode = "default";
    std::string theme = "dark";
};

struct SessionStats {
    int64_t total_input_tokens = 0;
    int64_t total_output_tokens = 0;
    int64_t total_cost_cents = 0;
    int total_turns = 0;
    int total_tool_calls = 0;
    std::chrono::system_clock::time_point session_start;
    std::chrono::system_clock::time_point last_activity;
};

}  // namespace claude
