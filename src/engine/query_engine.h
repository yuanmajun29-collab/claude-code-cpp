#pragma once

#include "engine/message.h"
#include "api/anthropic_client.h"
#include "tools/tool_registry.h"
#include "tools/tool_base.h"
#include "engine/cost_tracker.h"
#include "engine/context_builder.h"
#include "engine/query_pipeline.h"
#include "engine/stream_parser.h"
#include "state/app_state.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace claude {

// Query engine: the main tool-call loop
// This is the heart of Claude Code - it sends queries to the API,
// handles tool calls, and manages the conversation loop
class QueryEngine {
public:
    QueryEngine(AnthropicClient& client,
                ToolRegistry& tools,
                AppState& state,
                CostTracker& cost_tracker);

    ~QueryEngine() = default;

    // Execute a query (non-streaming)
    QueryResponse execute(const std::string& user_input);

    // Execute a query with streaming callback
    void execute_stream(const std::string& user_input,
                         std::function<void(const StreamEvent&)> on_event,
                         std::function<void(const QueryResponse&)> on_complete);

    // Execute a query with options
    QueryResponse execute_with_options(const QueryOptions& options);

    // Abort current query
    void abort();
    bool is_aborted() const { return aborted_.load(); }

    // Get the current conversation messages
    const std::vector<Message>& messages() const { return messages_; }

    // Clear conversation history
    void clear_history();

    // Set max tool-call loop iterations (safety limit)
    void set_max_iterations(int max) { max_iterations_ = max; }

private:
    // Core tool-call loop
    QueryResponse run_tool_call_loop(const QueryOptions& options);

    // Execute a single tool call
    ToolResult execute_tool_call(const ToolCall& call);

    // Check tool permission
    PermissionResult check_tool_permission(const std::string& tool_name,
                                            const std::string& input_json);

    // Build query options from current state
    QueryOptions build_query_options(const std::string& user_input);

    // Handle stream events during execution
    void handle_stream_event(const StreamEvent& event);

    // Display tool output to user
    void display_tool_result(const ToolCall& call, const ToolOutput& output);

    // Display assistant text to user
    void display_text(const std::string& text);

    // Display thinking to user (if verbose)
    void display_thinking(const std::string& thinking);

    // AnthropicClient reference
    AnthropicClient& client_;

    // Tool registry reference
    ToolRegistry& tools_;

    // App state reference
    AppState& state_;

    // Cost tracker reference
    CostTracker& cost_tracker_;

    // Context builder
    ContextBuilder context_builder_;

    // Query pipeline
    QueryPipeline pipeline_;

    // Conversation messages
    std::vector<Message> messages_;

    // Stream callback
    std::function<void(const StreamEvent&)> stream_callback_;

    // Abort flag
    std::atomic<bool> aborted_{false};

    // Safety limit for tool-call iterations
    int max_iterations_ = 100;

    // Current working directory
    std::string working_directory_;
};

}  // namespace claude
