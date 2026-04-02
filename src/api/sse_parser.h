#pragma once

#include <string>
#include <optional>
#include <vector>

namespace claude {

// Parsed SSE line
struct SSELine {
    std::string event;
    std::string data;
    std::string id;
};

// SSE stream parser for Anthropic API responses
class SSEParser {
public:
    SSEParser() = default;

    // Parse a single line from SSE stream
    // Returns std::nullopt if the line is incomplete or a comment
    std::optional<SSELine> parse_line(const std::string& line);

    // Parse a complete SSE event (all fields collected) into a StreamEvent
    struct StreamEvent parse_event(const std::string& event_type, const std::string& json_data);

    // Accumulate tool input JSON chunks
    // The Anthropic API sends tool input as a series of JSON delta strings
    // that need to be concatenated and then parsed
    std::string accumulate_tool_input(const std::string& delta);

    // Reset tool input accumulator
    void reset_tool_input() { tool_input_buffer_.clear(); }

    // Get accumulated tool input
    const std::string& tool_input_buffer() const { return tool_input_buffer_; }

    // Parse Anthropic stream event type from string
    static std::string normalize_event_type(const std::string& event_type);

private:
    std::string tool_input_buffer_;
    std::string buffer_;  // For multi-line data fields
};

}  // namespace claude
