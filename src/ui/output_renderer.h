#pragma once

#include "engine/message.h"
#include <string>

namespace claude {
namespace ui {

// Output renderer for assistant messages
class OutputRenderer {
public:
    OutputRenderer() = default;

    // Render a text chunk (streaming)
    void render_text(const std::string& text);

    // Render tool use info
    void render_tool_start(const std::string& tool_name, const std::string& tool_input);
    void render_tool_result(const std::string& tool_name, bool success, const std::string& output);
    void render_tool_error(const std::string& tool_name, const std::string& error);

    // Render thinking (collapsed by default)
    void render_thinking_start();
    void render_thinking_chunk(const std::string& text);
    void render_thinking_end();

    // Render cost/stats summary
    void render_cost_summary(double cost_usd, int64_t input_tokens, int64_t output_tokens,
                              double api_duration_ms, int tool_calls);

    // Render an error
    void render_error(const std::string& message);

    // Flush pending output
    void flush();

    // Set whether to show thinking
    void set_show_thinking(bool show) { show_thinking_ = show; }

private:
    bool show_thinking_ = false;
    bool in_thinking_block_ = false;
    std::string pending_thinking_;

    // Internal helpers
    void render_markdown_inline(const std::string& text);
    void ensure_newline();
};

}  // namespace ui
}  // namespace claude
