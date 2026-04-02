#pragma once

#include "engine/message.h"
#include <string>

namespace claude {

// Stream parser for converting Anthropic SSE events to internal format
// This is a higher-level parser that builds up the complete response
// as events stream in (similar to how query.ts processes the stream)
class StreamParser {
public:
    StreamParser() = default;

    // Reset state for a new stream
    void reset();

    // Feed a raw SSE event and update internal state
    // Returns true if the event was processed successfully
    bool feed_event(const StreamEvent& event);

    // Get the accumulated assistant message
    const Message& message() const { return message_; }

    // Get the current stop reason
    const std::string& stop_reason() const { return stop_reason_; }

    // Get accumulated token usage
    const TokenUsage& usage() const { return usage_; }

    // Check if any tool calls were made
    bool has_tool_calls() const;

    // Get tool calls from the message
    std::vector<ToolCall> get_tool_calls() const;

    // Check if stream is complete
    bool is_complete() const { return complete_; }

    // Get accumulated text output (for display)
    std::string text_output() const;

    // Get accumulated thinking output
    std::string thinking_output() const;

private:
    Message message_;
    std::string stop_reason_;
    TokenUsage usage_;
    bool complete_ = false;

    // Current content block being built
    ContentBlock* current_block_ = nullptr;
    std::string current_tool_input_buffer_;
    int current_block_index_ = -1;
};

}  // namespace claude
