#include "ui/output_renderer.h"
#include "ui/terminal.h"
#include "util/string_utils.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace claude {
namespace ui {

void OutputRenderer::render_text(const std::string& text) {
    // Close any open thinking block
    if (in_thinking_block_) {
        render_thinking_end();
    }
    std::cout << text << std::flush;
}

void OutputRenderer::render_tool_start(const std::string& tool_name, const std::string& /*tool_input*/) {
    ensure_newline();
    std::cout << gray("\n⚡ " + tool_name + "...") << std::flush;
}

void OutputRenderer::render_tool_result(const std::string& tool_name, bool success, const std::string& output) {
    ensure_newline();
    if (success) {
        std::cout << green("✅ " + tool_name) << "\n";
    } else {
        std::cout << yellow("⚠️ " + tool_name + " (non-zero exit)") << "\n";
    }

    // Show truncated output
    if (!output.empty()) {
        std::string display = output;
        // Remove timing info from last line for cleaner display
        auto lines = util::split(display, "\n");
        // Remove last line if it's just timing
        if (!lines.empty()) {
            auto last = util::trim(lines.back());
            if (last.find('(') == 0 && last.find(")") != std::string::npos &&
                last.find("ms") != std::string::npos) {
                lines.pop_back();
            }
        }

        // Show first few lines
        int max_lines = 8;
        int shown = 0;
        std::string truncated;
        for (const auto& line : lines) {
            if (shown >= max_lines) {
                truncated = "\n  ... (" + std::to_string(lines.size()) + " total lines)";
                break;
            }
            truncated += dim("  " + line) + "\n";
            shown++;
        }
        std::cout << truncated;
    }
}

void OutputRenderer::render_tool_error(const std::string& tool_name, const std::string& error) {
    ensure_newline();
    std::cout << red("❌ " + tool_name + ": " + error) << "\n";
}

void OutputRenderer::render_thinking_start() {
    if (!show_thinking_) {
        in_thinking_block_ = true;
        pending_thinking_.clear();
        return;
    }
    ensure_newline();
    std::cout << dim("💭 Thinking...") << "\n" << std::flush;
    in_thinking_block_ = true;
}

void OutputRenderer::render_thinking_chunk(const std::string& text) {
    if (!show_thinking_) {
        pending_thinking_ += text;
        return;
    }
    std::cout << dim(text) << std::flush;
}

void OutputRenderer::render_thinking_end() {
    in_thinking_block_ = false;
    pending_thinking_.clear();
    if (show_thinking_) {
        std::cout << "\n" << std::flush;
    }
}

void OutputRenderer::render_cost_summary(double cost_usd, int64_t input_tokens, int64_t output_tokens,
                                           double api_duration_ms, int tool_calls) {
    ensure_newline();

    std::ostringstream oss;
    oss << gray("─────");
    oss << gray(" Tokens: " + std::to_string(input_tokens) + " in, " + std::to_string(output_tokens) + " out");

    if (cost_usd > 0) {
        if (cost_usd > 0.5) {
            oss << " · $" << std::fixed << std::setprecision(2) << cost_usd;
        } else {
            oss << " · $" << cost_usd;
        }
    }

    if (api_duration_ms > 0) {
        oss << " · " << util::format_duration(api_duration_ms / 1000.0);
    }

    if (tool_calls > 0) {
        oss << " · " + std::to_string(tool_calls) + " tool calls";
    }

    oss << gray(" ─────");
    std::cout << oss.str() << "\n" << std::flush;
}

void OutputRenderer::render_error(const std::string& message) {
    ensure_newline();
    std::cout << red("Error: " + message) << "\n" << std::flush;
}

void OutputRenderer::flush() {
    std::cout << std::flush;
}

void OutputRenderer::ensure_newline() {
    // Check if cursor is at start of line (after a prompt or previous output)
    // This is a simple heuristic
}

void OutputRenderer::render_markdown_inline(const std::string& /*text*/) {
    // Inline markdown rendering for streaming text
    // For P2, we keep it simple - just pass through
}

}  // namespace ui
}  // namespace claude
