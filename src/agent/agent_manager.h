#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

namespace claude {
namespace agent {

// Forward declarations
class Coordinator;
class MessageBus;
class Team;

// AgentManager — high-level manager for the agent ecosystem
class AgentManager {
public:
    AgentManager();
    ~AgentManager() = default;

    // Access underlying components
    Coordinator& coordinator();
    MessageBus& message_bus();

    // Create a team
    Team& create_team(const std::string& name);

    // Get a team by name
    Team* get_team(const std::string& name);

    // List all team names
    std::vector<std::string> team_names() const;

    // Remove a team
    void remove_team(const std::string& name);

    // Register built-in agents
    void register_builtins();

    // Shutdown all agents and teams
    void shutdown();

private:
    std::unique_ptr<Coordinator> coordinator_;
    std::unique_ptr<MessageBus> bus_;
    std::map<std::string, std::unique_ptr<Team>> teams_;
    mutable std::mutex mutex_;
};

}  // namespace agent
}  // namespace claude
