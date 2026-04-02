#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

namespace claude {
namespace agent {

using json = nlohmann::json;

// Agent status
enum class AgentStatus {
    Idle,
    Running,
    Waiting,
    Completed,
    Failed,
    Cancelled
};

// Agent task
struct AgentTask {
    std::string id;
    std::string description;
    std::string agent_type;  // "coder", "reviewer", "tester", etc.
    json input;
    json output;
    AgentStatus status = AgentStatus::Idle;
    std::string error_message;
    double start_time_ms = 0;
    double end_time_ms = 0;
};

// Agent capability
struct AgentCapability {
    std::string name;
    std::string description;
    std::vector<std::string> tool_names;
};

// Agent definition
struct AgentDef {
    std::string name;
    std::string type;
    std::string description;
    std::string model_id;
    std::string system_prompt;
    std::vector<AgentCapability> capabilities;
    std::vector<std::string> available_tools;
};

// Agent instance (runs a task)
class AgentInstance {
public:
    explicit AgentInstance(const AgentDef& definition);
    ~AgentInstance() = default;

    const AgentDef& definition() const { return def_; }
    AgentStatus status() const { return status_; }
    const AgentTask& current_task() const { return task_; }

    // Execute a task
    AgentTask execute(const AgentTask& task);

    // Cancel current execution
    void cancel();

    // Get capabilities
    std::vector<AgentCapability> capabilities() const { return def_.capabilities; }

private:
    AgentDef def_;
    AgentStatus status_ = AgentStatus::Idle;
    AgentTask task_;
    bool cancelled_ = false;
};

// Coordinator - manages multiple agents and task routing
class Coordinator {
public:
    Coordinator() = default;
    ~Coordinator() = default;

    // Register agent type
    void register_agent(const AgentDef& def);

    // Create agent instance
    std::shared_ptr<AgentInstance> create_agent(const std::string& type);

    // Submit task to the best matching agent
    std::string submit_task(const std::string& description, const json& input = {});

    // Get task result
    std::optional<AgentTask> get_task_result(const std::string& task_id);

    // Run a task synchronously
    AgentTask run_task(const std::string& type, const AgentTask& task);

    // List available agent types
    std::vector<std::string> agent_types() const;

    // Get agent definition
    std::optional<AgentDef> get_agent_def(const std::string& type) const;

    // Register built-in agent types
    void register_builtins();

private:
    std::map<std::string, AgentDef> agent_defs_;
    std::map<std::string, std::shared_ptr<AgentInstance>> active_agents_;
    std::map<std::string, AgentTask> tasks_;
    std::mutex mutex_;
    int next_task_id_ = 1;
};

}  // namespace agent
}  // namespace claude
