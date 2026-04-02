#include "tools/agent_tool.h"
#include "agent/coordinator.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

AgentTool::AgentTool(agent::Coordinator& coordinator) : coordinator_(coordinator) {}

std::string AgentTool::name() const {
    return "agent";
}

std::string AgentTool::description() const {
    return "Delegate a task to a specialized sub-agent (coder, reviewer, or tester). "
           "Use this for parallel work, code review, or testing.";
}

ToolInputSchema AgentTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["agent_type"] = "string: Type of agent (coder, reviewer, tester)";
    schema.properties["task"] = "string: Description of the task to perform";
    schema.properties["context"] = "string: Additional context (file paths, code snippets, etc.)";
    schema.required = {"agent_type", "task"};
    return schema;
}

std::string AgentTool::system_prompt() const {
    return "Use the 'agent' tool to delegate tasks to specialized sub-agents:\n"
           "- **coder**: Write, refactor, or modify code\n"
           "- **reviewer**: Review code for bugs, style, and best practices\n"
           "- **tester**: Write and run unit tests\n\n"
           "Provide a clear task description and relevant context (files, code).\n"
           "The agent will return its output as text.";
}

ToolOutput AgentTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto input = nlohmann::json::parse(input_json);

        std::string agent_type = input.value("agent_type", "coder");
        std::string task_desc = input.value("task", "");
        std::string context = input.value("context", "");

        if (task_desc.empty()) {
            return ToolOutput::err("'task' field is required");
        }

        // Check permission
        if (ctx.check_permission) {
            auto perm = ctx.check_permission("agent_execute", agent_type);
            if (perm == PermissionResult::Denied) {
                return ToolOutput::err("Agent execution denied by permission system");
            }
        }

        // Check if agent type is registered
        auto types = coordinator_.agent_types();
        bool found = false;
        for (const auto& t : types) {
            if (t == agent_type) { found = true; break; }
        }
        if (!found) {
            std::string available;
            for (const auto& t : types) {
                if (!available.empty()) available += ", ";
                available += t;
            }
            return ToolOutput::err("Unknown agent type '" + agent_type +
                                   "'. Available: " + available);
        }

        // Build task
        agent::AgentTask task;
        task.description = task_desc;
        task.agent_type = agent_type;
        task.input = input;

        if (!context.empty()) {
            task.input["context"] = context;
        }

        // Run task
        auto result = coordinator_.run_task(agent_type, task);

        if (result.status == agent::AgentStatus::Failed) {
            return ToolOutput::err("Agent failed: " + result.error_message);
        }
        if (result.status == agent::AgentStatus::Cancelled) {
            return ToolOutput::err("Agent task was cancelled");
        }

        // Extract output
        std::string output;
        if (result.output.is_string()) {
            output = result.output.get<std::string>();
        } else if (!result.output.is_null()) {
            output = result.output.dump(2);
        }

        return ToolOutput::ok(output);

    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Agent tool error: ") + e.what());
    }
}

}  // namespace claude
