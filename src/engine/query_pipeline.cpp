#include "engine/query_pipeline.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {

json QueryPipeline::build_request_body(const QueryOptions& options,
                                        const std::string& /*tools_json_schema*/) {
    json body;
    body["model"] = options.model.model_id;
    body["max_tokens"] = options.model.max_tokens;

    if (options.model.temperature > 0) {
        body["temperature"] = options.model.temperature;
    }

    // Thinking
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
        body["system"] = build_system_prompt_json(options.system_prompts);
    }

    // Messages
    body["messages"] = format_messages(options.messages);

    // Stop sequences
    if (!options.stop_sequences.empty()) {
        body["stop_sequences"] = options.stop_sequences;
    }

    // Stream
    body["stream"] = options.stream;

    return body;
}

std::vector<json> QueryPipeline::format_messages(const std::vector<Message>& messages) {
    std::vector<json> result;
    for (const auto& msg : messages) {
        result.push_back(format_message(msg));
    }
    return result;
}

json QueryPipeline::format_message(const Message& msg) {
    json m;
    switch (msg.role) {
        case MessageRole::User:
            m["role"] = "user";
            break;
        case MessageRole::Assistant:
            m["role"] = "assistant";
            break;
        case MessageRole::System:
            // System messages should go in the system field, not messages
            m["role"] = "user";
            break;
        case MessageRole::Tool:
            m["role"] = "user";
            break;
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
                        tool_block["input"] = json::object();
                    }
                } else {
                    tool_block["input"] = json::object();
                }
                content.push_back(tool_block);
                break;
            }
            case ContentBlock::Type::Thinking:
                content.push_back({{"type", "thinking"}, {"thinking", block.thinking}});
                break;
            case ContentBlock::Type::Image:
                content.push_back({{"type", "image"}, {"source", {{"type", "base64"}, {"data", block.image_url}}}});
                break;
        }
    }
    return m;
}

std::string QueryPipeline::build_system_prompt(const std::vector<SystemPrompt>& prompts) {
    std::string result;
    for (const auto& p : prompts) {
        if (!result.empty()) result += "\n\n";
        result += p.content;
    }
    return result;
}

json QueryPipeline::build_system_prompt_json(const std::vector<SystemPrompt>& prompts) {
    if (prompts.empty()) return json::array();

    // If only one prompt without cache control, use simple string
    if (prompts.size() == 1 && !prompts[0].cache_control) {
        return prompts[0].content;
    }

    // Multiple prompts: use array format
    json arr = json::array();
    for (const auto& p : prompts) {
        json item = {{"type", "text"}, {"text", p.content}};
        if (p.cache_control) {
            item["cache_control"] = {{"type", "ephemeral"}};
        }
        arr.push_back(item);
    }
    return arr;
}

bool QueryPipeline::needs_compaction(const std::vector<Message>& messages, int64_t max_tokens) const {
    int64_t estimated = estimate_tokens(messages);
    // Trigger compaction at 80% of max to leave room for response
    return estimated > max_tokens * 8 / 10;
}

std::vector<Message> QueryPipeline::trim_messages(std::vector<Message> messages, int max_messages) {
    if (static_cast<int>(messages.size()) <= max_messages) return messages;

    // Keep the most recent max_messages messages
    auto start = messages.size() - max_messages;
    std::vector<Message> trimmed(messages.begin() + start, messages.end());
    return trimmed;
}

int64_t QueryPipeline::estimate_tokens(const std::vector<Message>& messages) const {
    int64_t total_chars = 0;
    for (const auto& msg : messages) {
        for (const auto& block : msg.content) {
            switch (block.type) {
                case ContentBlock::Type::Text:
                    total_chars += block.text.size();
                    break;
                case ContentBlock::Type::ToolUse:
                    total_chars += block.tool_call.name.size() + block.tool_call.input_json.size();
                    break;
                case ContentBlock::Type::Thinking:
                    total_chars += block.thinking.size();
                    break;
                default:
                    break;
            }
        }
    }
    return static_cast<int64_t>(total_chars / CHARS_PER_TOKEN);
}

json QueryPipeline::format_tool_result(const ToolResult& result) {
    json block;
    block["type"] = result.is_error ? "tool_result" : "tool_result";
    block["tool_use_id"] = result.tool_use_id;
    block["content"] = result.content;
    if (result.is_error) {
        block["is_error"] = true;
    }
    return block;
}

Message QueryPipeline::create_tool_result_message(const std::vector<ToolResult>& results) {
    Message msg;
    msg.role = MessageRole::User;
    msg.timestamp = std::chrono::system_clock::now();

    for (const auto& result : results) {
        ContentBlock block;
        block.type = ContentBlock::Type::Text;
        // Tool results are sent as tool_result content blocks in the API
        // For internal representation, we store the formatted JSON
        json tr;
        tr["type"] = "tool_result";
        tr["tool_use_id"] = result.tool_use_id;
        tr["content"] = result.content;
        if (result.is_error) tr["is_error"] = true;
        block.text = tr.dump();
        msg.content.push_back(block);
    }
    return msg;
}

}  // namespace claude
