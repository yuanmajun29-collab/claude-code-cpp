#include "agent/coordinator.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>

namespace claude {
namespace agent {

AgentInstance::AgentInstance(const AgentDef& definition) : def_(definition) {}

AgentTask AgentInstance::execute(const AgentTask& task) {
    task_ = task;
    task_.status = AgentStatus::Running;
    task_.start_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    status_ = AgentStatus::Running;

    // Stub: In production, this would use QueryEngine to execute
    // For P5, we just mark as completed
    spdlog::debug("Agent '{}' executing task: {}", def_.name, task.description);

    task_.output = json::object();
    task_.output["status"] = "completed";
    task_.output["message"] = "Task completed by agent " + def_.name;

    task_.status = cancelled_ ? AgentStatus::Cancelled : AgentStatus::Completed;
    task_.end_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    status_ = task_.status;

    return task_;
}

void AgentInstance::cancel() {
    cancelled_ = true;
    if (status_ == AgentStatus::Running) {
        status_ = AgentStatus::Cancelled;
        task_.status = AgentStatus::Cancelled;
    }
}

void Coordinator::register_agent(const AgentDef& def) {
    std::lock_guard<std::mutex> lock(mutex_);
    agent_defs_[def.type] = def;
    spdlog::info("Registered agent type: {} ({})", def.name, def.type);
}

std::shared_ptr<AgentInstance> Coordinator::create_agent(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_defs_.find(type);
    if (it == agent_defs_.end()) return nullptr;
    return std::make_shared<AgentInstance>(it->second);
}

std::string Coordinator::submit_task(const std::string& description, const json& input) {
    std::lock_guard<std::mutex> lock(mutex_);

    AgentTask task;
    task.id = "task_" + std::to_string(next_task_id_++);
    task.description = description;
    task.input = input;

    tasks_[task.id] = task;
    return task.id;
}

std::optional<AgentTask> Coordinator::get_task_result(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) return it->second;
    return std::nullopt;
}

AgentTask Coordinator::run_task(const std::string& type, const AgentTask& task) {
    auto agent = create_agent(type);
    if (!agent) {
        AgentTask failed = task;
        failed.status = AgentStatus::Failed;
        failed.error_message = "Unknown agent type: " + type;
        return failed;
    }
    return agent->execute(task);
}

std::vector<std::string> Coordinator::agent_types() const {
    std::vector<std::string> types;
    for (const auto& [type, _] : agent_defs_) {
        types.push_back(type);
    }
    return types;
}

std::optional<AgentDef> Coordinator::get_agent_def(const std::string& type) const {
    auto it = agent_defs_.find(type);
    if (it != agent_defs_.end()) return it->second;
    return std::nullopt;
}

void Coordinator::register_builtins() {
    register_agent({
        "Coder", "coder", "General-purpose coding agent",
        "claude-sonnet-4-20250514",
        "You are a coding agent. Write clean, correct code.",
        {{"code_generation", "Write new code", {}}, {"code_review", "Review existing code", {}}},
        {"Bash", "Read", "Write", "Edit", "Glob", "Grep"}
    });

    register_agent({
        "Reviewer", "reviewer", "Code review agent",
        "claude-sonnet-4-20250514",
        "You are a code reviewer. Find bugs, suggest improvements.",
        {{"review", "Review code for issues", {}}},
        {"Read", "Glob", "Grep"}
    });

    register_agent({
        "Tester", "tester", "Test writing agent",
        "claude-sonnet-4-20250514",
        "You are a testing agent. Write comprehensive tests.",
        {{"testing", "Write unit and integration tests", {}}},
        {"Bash", "Read", "Write", "Edit", "Glob", "Grep"}
    });
}

}  // namespace agent
}  // namespace claude
