#include "agent/agent_manager.h"
#include "agent/message_bus.h"
#include "agent/team.h"
#include "agent/coordinator.h"
#include <spdlog/spdlog.h>

namespace claude {
namespace agent {

AgentManager::AgentManager()
    : coordinator_(std::make_unique<Coordinator>()),
      bus_(std::make_unique<MessageBus>()) {}

Coordinator& AgentManager::coordinator() { return *coordinator_; }

MessageBus& AgentManager::message_bus() { return *bus_; }

Team& AgentManager::create_team(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = teams_.find(name);
    if (it != teams_.end()) return *it->second;
    teams_[name] = std::make_unique<Team>(name);
    spdlog::info("AgentManager: created team '{}'", name);
    return *teams_[name];
}

Team* AgentManager::get_team(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = teams_.find(name);
    return it != teams_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> AgentManager::team_names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : teams_) {
        names.push_back(name);
    }
    return names;
}

void AgentManager::remove_team(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    teams_.erase(name);
    spdlog::info("AgentManager: removed team '{}'", name);
}

void AgentManager::register_builtins() {
    coordinator_->register_builtins();
    spdlog::info("AgentManager: built-in agents registered");
}

void AgentManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    teams_.clear();
    spdlog::info("AgentManager: shutdown complete");
}

}  // namespace agent
}  // namespace claude
