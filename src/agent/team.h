#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "agent/coordinator.h"

namespace claude {
namespace agent {

// Team — groups agents together for collaborative tasks
class Team {
public:
    Team() = default;
    explicit Team(const std::string& name);
    ~Team() = default;

    // Add agent to team
    void add_member(const std::string& agent_id, const std::string& role = "");

    // Remove agent from team
    void remove_member(const std::string& agent_id);

    // Assign task to specific team member
    std::string assign(const std::string& agent_id, const AgentTask& task);

    // Broadcast task to all members
    std::vector<std::string> broadcast(const AgentTask& task);

    // Get team members
    std::vector<std::string> members() const;

    // Get member role
    std::string role(const std::string& agent_id) const;

    // Set team name
    void set_name(const std::string& name) { name_ = name; }
    std::string name() const { return name_; }

    // Get task results for a member
    std::vector<AgentTask> results(const std::string& agent_id) const;

    // Clear all tasks
    void clear_tasks();

    // Member count
    size_t size() const;

private:
    struct Member {
        std::string agent_id;
        std::string role;
    };

    std::string name_;
    std::vector<Member> members_;
    std::map<std::string, std::vector<AgentTask>> task_results_;
    mutable std::mutex mutex_;
};

}  // namespace agent
}  // namespace claude
