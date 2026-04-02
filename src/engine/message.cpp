#include "engine/message.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace claude {

// ============================================================
// TokenUsage
// ============================================================

std::string TokenUsage::to_string() const {
    std::ostringstream oss;
    oss << "input=" << input_tokens
        << " cache_read=" << cache_read_input_tokens
        << " cache_write=" << cache_creation_input_tokens
        << " output=" << output_tokens
        << " total=" << total();
    return oss.str();
}

double TokenUsage::estimate_cost_usd() const {
    // Claude Sonnet 4 pricing (approximate)
    constexpr double input_per_1m = 3.0;
    constexpr double cache_read_per_1m = 0.30;
    constexpr double cache_write_per_1m = 3.75;
    constexpr double output_per_1m = 15.0;

    return (input_tokens / 1e6 * input_per_1m)
         + (cache_read_input_tokens / 1e6 * cache_read_per_1m)
         + (cache_creation_input_tokens / 1e6 * cache_write_per_1m)
         + (output_tokens / 1e6 * output_per_1m);
}

// ============================================================
// QueryOptions helpers
// ============================================================

std::string QueryOptions::to_debug_string() const {
    std::ostringstream oss;
    oss << "model=" << model.model_id
        << " max_tokens=" << model.max_tokens
        << " messages=" << messages.size()
        << " tools=" << "N/A"  // tools are separate
        << " stream=" << (stream ? "true" : "false");
    return oss.str();
}

// ============================================================
// Message builders
// ============================================================

Message Message::system(const std::string& text) {
    Message msg;
    msg.role = MessageRole::System;
    msg.content.push_back({ContentBlock::Type::Text, text, {}, "", ""});
    msg.timestamp = std::chrono::system_clock::now();
    return msg;
}

Message Message::user(const std::string& text) {
    Message msg;
    msg.role = MessageRole::User;
    msg.content.push_back({ContentBlock::Type::Text, text, {}, "", ""});
    msg.timestamp = std::chrono::system_clock::now();
    return msg;
}

Message Message::assistant(const std::string& text, const std::string& model_id) {
    Message msg;
    msg.role = MessageRole::Assistant;
    msg.content.push_back({ContentBlock::Type::Text, text, {}, "", ""});
    msg.model = model_id;
    msg.timestamp = std::chrono::system_clock::now();
    return msg;
}

Message Message::tool_result(const std::string& /*tool_use_id*/, const std::string& content, bool /*is_error*/) {
    Message msg;
    msg.role = MessageRole::Tool;
    // Tool results are sent differently in the API
    // This is a simplified representation
    msg.content.push_back({ContentBlock::Type::Text, content, {}, "", ""});
    msg.timestamp = std::chrono::system_clock::now();
    return msg;
}

}  // namespace claude
