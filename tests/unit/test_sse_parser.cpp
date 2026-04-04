#include <gtest/gtest.h>
#include "api/sse_parser.h"
#include "engine/message.h"

using namespace claude;

TEST(SSEParserTest, ParseEventLine) {
    SSEParser parser;
    auto result = parser.parse_line("event: message_start");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->event, "message_start");
    EXPECT_TRUE(result->data.empty());
}

TEST(SSEParserTest, ParseDataLine) {
    SSEParser parser;
    auto result = parser.parse_line("data: {\"type\":\"text\"}");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, "{\"type\":\"text\"}");
}

TEST(SSEParserTest, ParseIdLine) {
    SSEParser parser;
    auto result = parser.parse_line("id: evt_123");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, "evt_123");
}

TEST(SSEParserTest, IgnoreEmptyLine) {
    SSEParser parser;
    EXPECT_FALSE(parser.parse_line("").has_value());
}

TEST(SSEParserTest, IgnoreComment) {
    SSEParser parser;
    EXPECT_FALSE(parser.parse_line(": this is a comment").has_value());
}

TEST(SSEParserTest, ParseMessageStartEvent) {
    SSEParser parser;
    std::string data = R"({"type":"message_start","message":{"id":"msg_01","model":"claude-sonnet-4","usage":{"input_tokens":100,"output_tokens":0}}})";
    auto event = parser.parse_event("message_start", data);
    EXPECT_EQ(event.type, StreamEventType::MessageStart);
    ASSERT_TRUE(event.message.has_value());
    EXPECT_EQ(event.message->id, "msg_01");
    EXPECT_EQ(event.message->model, "claude-sonnet-4");
}

TEST(SSEParserTest, ParseContentBlockStart) {
    SSEParser parser;
    std::string data = R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})";
    auto event = parser.parse_event("content_block_start", data);
    EXPECT_EQ(event.type, StreamEventType::ContentBlockStart);
    ASSERT_TRUE(event.content_block.has_value());
    EXPECT_EQ(event.content_block->type, ContentBlock::Type::Text);
}

TEST(SSEParserTest, ParseToolUseBlockStart) {
    SSEParser parser;
    std::string data = R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"call_1","name":"Bash"}})";
    auto event = parser.parse_event("content_block_start", data);
    EXPECT_EQ(event.type, StreamEventType::ContentBlockStart);
    ASSERT_TRUE(event.content_block.has_value());
    EXPECT_EQ(event.content_block->type, ContentBlock::Type::ToolUse);
    EXPECT_EQ(event.content_block->tool_call.id, "call_1");
    EXPECT_EQ(event.content_block->tool_call.name, "Bash");
}

TEST(SSEParserTest, ParseContentBlockDelta) {
    SSEParser parser;
    std::string data = R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})";
    auto event = parser.parse_event("content_block_delta", data);
    EXPECT_EQ(event.type, StreamEventType::ContentBlockDelta);
    ASSERT_TRUE(event.delta_text.has_value());
    EXPECT_EQ(*event.delta_text, "Hello");
}

TEST(SSEParserTest, ParseInputJsonDelta) {
    SSEParser parser;
    std::string data = R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"comm"}})";
    auto event = parser.parse_event("content_block_delta", data);
    EXPECT_EQ(event.type, StreamEventType::ContentBlockDelta);
    ASSERT_TRUE(event.delta_text.has_value());
    EXPECT_EQ(*event.delta_text, "{\"comm");
}

TEST(SSEParserTest, ParseMessageDelta) {
    SSEParser parser;
    std::string data = R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":50}})";
    auto event = parser.parse_event("message_delta", data);
    EXPECT_EQ(event.type, StreamEventType::MessageDelta);
    ASSERT_TRUE(event.usage_delta.has_value());
    EXPECT_EQ(event.usage_delta->output_tokens, 50);
    ASSERT_TRUE(event.stop_reason.has_value());
    EXPECT_EQ(*event.stop_reason, "end_turn");
}

TEST(SSEParserTest, ParseError) {
    SSEParser parser;
    std::string data = R"({"type":"error","error":{"message":"Rate limit exceeded"}})";
    auto event = parser.parse_event("error", data);
    EXPECT_EQ(event.type, StreamEventType::Error);
    ASSERT_TRUE(event.error_message.has_value());
    EXPECT_EQ(*event.error_message, "Rate limit exceeded");
}

TEST(SSEParserTest, AccumulateToolInput) {
    SSEParser parser;
    parser.accumulate_tool_input("{\"co");
    parser.accumulate_tool_input("mmand\":");
    parser.accumulate_tool_input("\"ls\"}");
    EXPECT_EQ(parser.tool_input_buffer(), "{\"command\":\"ls\"}");

    parser.reset_tool_input();
    EXPECT_TRUE(parser.tool_input_buffer().empty());
}
