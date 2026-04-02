#include "engine/stream_parser.h"
#include <spdlog/spdlog.h>

namespace claude {

void StreamParser::reset() {
    message_ = Message{};
    stop_reason_.clear();
    usage_ = TokenUsage{};
    complete_ = false;
    current_block_ = nullptr;
    current_tool_input_buffer_.clear();
    current_block_index_ = -1;
}

bool StreamParser::feed_event(const StreamEvent& event) {
    switch (event.type) {
        case StreamEventType::MessageStart:
            if (event.message) {
                message_.id = event.message->id;
                message_.model = event.message->model;
                message_.usage = event.message->usage;
            }
            break;

        case StreamEventType::MessageDelta:
            if (event.usage_delta) {
                usage_.output_tokens += event.usage_delta->output_tokens;
            }
            if (event.delta_text && !event.delta_text->empty() && *event.delta_text != "null") {
                stop_reason_ = *event.delta_text;
            }
            break;

        case StreamEventType::MessageStop:
            complete_ = true;
            message_.usage = usage_;
            break;

        case StreamEventType::ContentBlockStart:
            if (event.content_block) {
                message_.content.push_back(*event.content_block);
                current_block_index_ = static_cast<int>(message_.content.size()) - 1;
                current_block_ = &message_.content.back();

                if (current_block_->type == ContentBlock::Type::ToolUse) {
                    current_tool_input_buffer_.clear();
                }
            }
            break;

        case StreamEventType::ContentBlockDelta:
            if (current_block_) {
                if (current_block_->type == ContentBlock::Type::Text && event.delta_text) {
                    current_block_->text += *event.delta_text;
                } else if (current_block_->type == ContentBlock::Type::Thinking && event.delta_text) {
                    current_block_->thinking += *event.delta_text;
                } else if (current_block_->type == ContentBlock::Type::ToolUse && event.delta_text) {
                    current_tool_input_buffer_ += *event.delta_text;
                }
            }
            break;

        case StreamEventType::ContentBlockStop:
            if (current_block_ && current_block_->type == ContentBlock::Type::ToolUse) {
                current_block_->tool_call.input_json = current_tool_input_buffer_;
                current_tool_input_buffer_.clear();
            }
            current_block_ = nullptr;
            current_block_index_ = -1;
            break;

        case StreamEventType::Error:
            if (event.error_message) {
                spdlog::error("Stream error: {}", *event.error_message);
                complete_ = true;
                stop_reason_ = "error";
            }
            break;

        default:
            break;
    }
    return true;
}

bool StreamParser::has_tool_calls() const {
    for (const auto& block : message_.content) {
        if (block.type == ContentBlock::Type::ToolUse) return true;
    }
    return false;
}

std::vector<ToolCall> StreamParser::get_tool_calls() const {
    return message_.tool_calls();
}

std::string StreamParser::text_output() const {
    return message_.text_content();
}

std::string StreamParser::thinking_output() const {
    std::string result;
    for (const auto& block : message_.content) {
        if (block.type == ContentBlock::Type::Thinking) {
            result += block.thinking;
        }
    }
    return result;
}

}  // namespace claude
