#include "api/sse_parser.h"
#include "engine/message.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace claude {

std::optional<SSELine> SSEParser::parse_line(const std::string& line) {
    if (line.empty()) {
        return std::nullopt;
    }

    // Comments start with ':'
    if (line[0] == ':') {
        return std::nullopt;
    }

    SSELine sse_line;

    if (line.substr(0, 6) == "event:") {
        sse_line.event = line.size() > 7 ? line.substr(7) : "";
        // Trim leading space
        if (!sse_line.event.empty() && sse_line.event[0] == ' ') {
            sse_line.event = sse_line.event.substr(1);
        }
        return sse_line;
    }

    if (line.substr(0, 5) == "data:") {
        sse_line.data = line.size() > 6 ? line.substr(6) : "";
        if (!sse_line.data.empty() && sse_line.data[0] == ' ') {
            sse_line.data = sse_line.data.substr(1);
        }
        return sse_line;
    }

    if (line.substr(0, 3) == "id:") {
        sse_line.id = line.size() > 4 ? line.substr(4) : "";
        if (!sse_line.id.empty() && sse_line.id[0] == ' ') {
            sse_line.id = sse_line.id.substr(1);
        }
        return sse_line;
    }

    return std::nullopt;
}

StreamEvent SSEParser::parse_event(const std::string& event_type, const std::string& json_data) {
    StreamEvent event;
    event.type = StreamEventType::Error;
    event.data = json_data;

    try {
        auto j = json::parse(json_data);

        std::string normalized = normalize_event_type(event_type);

        if (normalized == "message_start") {
            event.type = StreamEventType::MessageStart;
            if (j.contains("message")) {
                auto& msg = j["message"];
                Message m;
                m.id = msg.value("id", "");
                m.model = msg.value("model", "");
                if (msg.contains("usage")) {
                    auto& u = msg["usage"];
                    m.usage.input_tokens = u.value("input_tokens", 0);
                    m.usage.output_tokens = u.value("output_tokens", 0);
                    m.usage.cache_creation_input_tokens = u.value("cache_creation_input_tokens", 0);
                    m.usage.cache_read_input_tokens = u.value("cache_read_input_tokens", 0);
                }
                event.message = std::move(m);
            }
        } else if (normalized == "message_delta") {
            event.type = StreamEventType::MessageDelta;
            if (j.contains("usage")) {
                auto& u = j["usage"];
                TokenUsage usage;
                usage.output_tokens = u.value("output_tokens", 0);
                event.usage_delta = usage;
            }
            if (j.contains("delta")) {
                auto sr = j["delta"].value("stop_reason", "");
                if (!sr.empty()) {
                    event.stop_reason = sr;
                }
            }
        } else if (normalized == "message_stop") {
            event.type = StreamEventType::MessageStop;
        } else if (normalized == "content_block_start") {
            event.type = StreamEventType::ContentBlockStart;
            if (j.contains("content_block")) {
                auto& cb = j["content_block"];
                ContentBlock block;
                std::string cb_type = cb.value("type", "text");
                if (cb_type == "text") {
                    block.type = ContentBlock::Type::Text;
                    block.text = cb.value("text", "");
                } else if (cb_type == "tool_use") {
                    block.type = ContentBlock::Type::ToolUse;
                    block.tool_call.id = cb.value("id", "");
                    block.tool_call.name = cb.value("name", "");
                } else if (cb_type == "thinking") {
                    block.type = ContentBlock::Type::Thinking;
                    block.thinking = cb.value("thinking", "");
                }
                event.content_block = block;
            }
        } else if (normalized == "content_block_delta") {
            event.type = StreamEventType::ContentBlockDelta;
            if (j.contains("delta")) {
                auto& delta = j["delta"];
                std::string delta_type = delta.value("type", "text_delta");
                if (delta_type == "text_delta") {
                    event.delta_text = delta.value("text", "");
                } else if (delta_type == "thinking_delta") {
                    event.delta_text = delta.value("thinking", "");
                } else if (delta_type == "input_json_delta") {
                    event.delta_text = delta.value("partial_json", "");
                } else if (delta_type == "tool_use") {
                    // Some events use "tool_use" type
                    event.delta_text = delta.value("partial_json", "");
                }
            }
        } else if (normalized == "content_block_stop") {
            event.type = StreamEventType::ContentBlockStop;
        } else if (normalized == "error") {
            event.type = StreamEventType::Error;
            event.error_message = "API Error";
            if (j.contains("error") && j["error"].is_object()) {
                event.error_message = j["error"].value("message", std::string("unknown"));
            }
        }
    } catch (const json::parse_error& e) {
        spdlog::warn("SSE parse error: {}", e.what());
        event.type = StreamEventType::Error;
        event.error_message = "JSON parse error: " + std::string(e.what());
    }

    return event;
}

std::string SSEParser::accumulate_tool_input(const std::string& delta) {
    tool_input_buffer_ += delta;
    return tool_input_buffer_;
}

std::string SSEParser::normalize_event_type(const std::string& event_type) {
    if (event_type.empty()) return "content_block_delta";
    return event_type;
}

}  // namespace claude
