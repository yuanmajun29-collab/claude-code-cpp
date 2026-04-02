#include "engine/query_engine.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>
#include <sstream>

using json = nlohmann::json;

namespace claude {

QueryEngine::QueryEngine(AnthropicClient& client,
                          ToolRegistry& tools,
                          AppState& state,
                          CostTracker& cost_tracker)
    : client_(client)
    , tools_(tools)
    , state_(state)
    , cost_tracker_(cost_tracker)
    , working_directory_(state_.current_session().working_directory) {}

void QueryEngine::abort() {
    aborted_.store(true);
    client_.abort();
}

void QueryEngine::clear_history() {
    messages_.clear();
    context_builder_.clear_cache();
}

QueryOptions QueryEngine::build_query_options(const std::string& user_input) {
    auto& session = state_.current_session();

    QueryOptions options;
    options.model = session.model;
    options.stream = true;

    // Build system prompts
    auto sys_ctx = context_builder_.build_system_context(session);
    auto user_ctx = context_builder_.build_user_context(session);

    // Add tool system prompt
    std::string tools_prompt = tools_.tools_system_prompt();

    if (!sys_ctx.content.empty()) {
        options.system_prompts.push_back(sys_ctx);
    }
    if (!user_ctx.content.empty()) {
        options.system_prompts.push_back(user_ctx);
    }
    if (!tools_prompt.empty()) {
        options.system_prompts.push_back({tools_prompt, std::nullopt});
    }

    // Add user message
    messages_.push_back(Message::user(user_input));

    // Set all messages
    options.messages = messages_;

    return options;
}

QueryResponse QueryEngine::execute(const std::string& user_input) {
    auto options = build_query_options(user_input);
    return run_tool_call_loop(options);
}

void QueryEngine::execute_stream(const std::string& user_input,
                                   std::function<void(const StreamEvent&)> on_event,
                                   std::function<void(const QueryResponse&)> on_complete) {
    stream_callback_ = std::move(on_event);
    auto options = build_query_options(user_input);
    auto response = run_tool_call_loop(options);
    if (on_complete) on_complete(response);
    stream_callback_ = nullptr;
}

QueryResponse QueryEngine::execute_with_options(const QueryOptions& options) {
    return run_tool_call_loop(options);
}

QueryResponse QueryEngine::run_tool_call_loop(const QueryOptions& options) {
    QueryResponse final_response;
    QueryOptions current_options = options;

    for (int iteration = 0; iteration < max_iterations_; ++iteration) {
        if (aborted_.load()) {
            spdlog::info("Query aborted by user");
            final_response.stop_reason = "abort";
            break;
        }

        spdlog::debug("Tool-call loop iteration {}, messages: {}", iteration, current_options.messages.size());

        // Send request to API
        StreamParser parser;
        auto start = std::chrono::steady_clock::now();

        QueryResponse api_response = client_.create_message_stream(
            current_options,
            [this, &parser](const StreamEvent& event) {
                parser.feed_event(event);
                handle_stream_event(event);
            }
        );

        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        // Track usage
        cost_tracker_.record_usage(api_response.usage);
        cost_tracker_.record_model_usage(api_response.message.model, api_response.usage);
        cost_tracker_.record_api_duration(elapsed);

        // Add assistant message to history
        messages_.push_back(api_response.message);
        final_response = api_response;

        // Check if we need to execute tools
        if (!api_response.stopped_by_tool) {
            spdlog::debug("API response stop reason: {}", api_response.stop_reason);
            break;
        }

        // Execute tool calls
        auto tool_calls = api_response.message.tool_calls();
        spdlog::info("Executing {} tool calls", tool_calls.size());

        std::vector<ToolResult> results;
        for (const auto& call : tool_calls) {
            if (aborted_.load()) break;

            auto result = execute_tool_call(call);
            results.push_back(result);
            cost_tracker_.increment_tool_calls();
        }

        if (results.empty()) break;

        // Create tool result message and add to conversation
        auto result_msg = pipeline_.create_tool_result_message(results);
        messages_.push_back(result_msg);

        // Update options for next iteration
        current_options.messages = messages_;
    }

    if (aborted_.load()) {
        final_response.stop_reason = "abort";
    }

    return final_response;
}

ToolResult QueryEngine::execute_tool_call(const ToolCall& call) {
    spdlog::info("Executing tool: {} (id: {})", call.name, call.id);

    auto* tool = tools_.find_tool(call.name);
    if (!tool) {
        std::string error = "Unknown tool: " + call.name;
        spdlog::error(error);
        return {call.id, ToolResultStatus::Error, error, true};
    }

    // Check permissions
    auto perm = check_tool_permission(call.name, call.input_json);
    if (perm == PermissionResult::Denied) {
        std::string error = "Tool execution denied: " + call.name;
        return {call.id, ToolResultStatus::Error, error, true};
    }
    // Note: NeedsUserConfirmation handling would go here in a full implementation

    // Build tool context
    ToolContext ctx;
    ctx.working_directory = working_directory_;
    ctx.session_config = state_.current_session();
    ctx.check_permission = [this](const std::string& name, const std::string& input) {
        return check_tool_permission(name, input);
    };
    ctx.log_info = [](const std::string& msg) { spdlog::info("Tool: {}", msg); };
    ctx.log_error = [](const std::string& msg) { spdlog::error("Tool: {}", msg); };

    // Execute
    auto start = std::chrono::steady_clock::now();
    ToolOutput output = tool->execute(call.input_json, ctx);
    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    cost_tracker_.record_tool_duration(elapsed);

    // Display result
    display_tool_result(call, output);

    // Convert to ToolResult
    ToolResult result;
    result.tool_use_id = call.id;
    result.content = output.content;
    result.is_error = output.is_error;
    result.status = output.is_error ? ToolResultStatus::Error : ToolResultStatus::Success;

    return result;
}

PermissionResult QueryEngine::check_tool_permission(const std::string& /*tool_name*/,
                                                      const std::string& /*input_json*/) {
    auto& session = state_.current_session();
    if (session.permission_mode == "bypass" || session.permission_mode == "yolo") {
        return PermissionResult::Allowed;
    }
    // Default mode: allow all for now, will implement proper permission checking later
    return PermissionResult::Allowed;
}

void QueryEngine::handle_stream_event(const StreamEvent& event) {
    if (stream_callback_) {
        stream_callback_(event);
    }

    switch (event.type) {
        case StreamEventType::ContentBlockDelta:
            if (event.delta_text) {
                display_text(*event.delta_text);
            }
            break;
        default:
            break;
    }
}

void QueryEngine::display_tool_result(const ToolCall& call, const ToolOutput& output) {
    std::cout << "\n⚡ " << call.name;
    if (!output.content.empty()) {
        // Truncate long output for display
        std::string display = output.content;
        if (display.size() > 500) {
            display = display.substr(0, 500) + "... (truncated)";
        }
        std::cout << "\n" << display;
    }
    if (output.is_error) {
        std::cout << "\n❌ Error: " << output.error_message;
    }
    std::cout << std::endl;
}

void QueryEngine::display_text(const std::string& text) {
    std::cout << text << std::flush;
}

void QueryEngine::display_thinking(const std::string& /*thinking*/) {
    // Could display thinking in debug mode
}

}  // namespace claude
