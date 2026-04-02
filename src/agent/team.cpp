#include "agent/team.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {
namespace agent {

// ============================================================
// Team
// ============================================================

Team::Team(const std::string& name) : name_(name) {}

void Team::add_member(const std::string& agent_id, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Don't add duplicates
    for (const auto& m : members_) {
        if (m.agent_id == agent_id) return;
    }
    members_.push_back({agent_id, role});
    spdlog::debug("Team '{}': added member {} (role: {})", name_, agent_id, role);
}

void Team::remove_member(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    members_.erase(
        std::remove_if(members_.begin(), members_.end(),
                       [&](const Member& m) { return m.agent_id == agent_id; }),
        members_.end());
    task_results_.erase(agent_id);
    spdlog::debug("Team '{}': removed member {}", name_, agent_id);
}

std::string Team::assign(const std::string& agent_id, const AgentTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Verify member exists
    bool found = false;
    for (const auto& m : members_) {
        if (m.agent_id == agent_id) { found = true; break; }
    }
    if (!found) {
        spdlog::warn("Team '{}': agent {} is not a member", name_, agent_id);
        return "";
    }

    task_results_[agent_id].push_back(task);
    spdlog::debug("Team '{}': assigned task {} to {}", name_, task.id, agent_id);
    return task.id;
}

std::vector<std::string> Team::broadcast(const AgentTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> task_ids;

    for (const auto& m : members_) {
        AgentTask member_task = task;
        member_task.id = task.id + "_" + m.agent_id;
        task_results_[m.agent_id].push_back(member_task);
        task_ids.push_back(member_task.id);
    }

    spdlog::debug("Team '{}': broadcast task to {} members", name_, members_.size());
    return task_ids;
}

std::vector<std::string> Team::members() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& m : members_) {
        result.push_back(m.agent_id);
    }
    return result;
}

std::string Team::role(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : members_) {
        if (m.agent_id == agent_id) return m.role;
    }
    return "";
}

std::vector<AgentTask> Team::results(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_results_.find(agent_id);
    if (it != task_results_.end()) return it->second;
    return {};
}

void Team::clear_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, tasks] : task_results_) {
        tasks.clear();
    }
}

size_t Team::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return members_.size();
}

}  // namespace agent
}  // namespace claude
