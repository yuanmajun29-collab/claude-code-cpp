#pragma once

#include "engine/message.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace claude {

using json = nlohmann::json;

// Query pipeline: builds API requests and manages context window
class QueryPipeline {
public:
    QueryPipeline() = default;

    // Build the full JSON request body for the Anthropic API
    json build_request_body(const QueryOptions& options,
                             const std::string& tools_json_schema);

    // Format messages for the API (convert internal Message to API format)
    std::vector<json> format_messages(const std::vector<Message>& messages);

    // Format a single message
    json format_message(const Message& msg);

    // Build system prompt string from system prompt parts
    std::string build_system_prompt(const std::vector<SystemPrompt>& prompts);

    // Build system prompt as JSON array (for cache control)
    json build_system_prompt_json(const std::vector<SystemPrompt>& prompts);

    // Check if context compaction is needed
    bool needs_compaction(const std::vector<Message>& messages, int64_t max_tokens) const;

    // Trim messages to fit within context window
    // Keeps the most recent messages + system prompts
    std::vector<Message> trim_messages(std::vector<Message> messages, int max_messages);

    // Estimate token count for messages (rough estimate: 1 token ≈ 4 chars)
    int64_t estimate_tokens(const std::vector<Message>& messages) const;

    // Format tool result for sending back to the model
    json format_tool_result(const ToolResult& result);

    // Create user message with tool results
    Message create_tool_result_message(const std::vector<ToolResult>& results);

private:
    // Rough token estimate: ~4 characters per token
    static constexpr double CHARS_PER_TOKEN = 4.0;
};

}  // namespace claude
