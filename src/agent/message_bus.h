#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

namespace claude {
namespace agent {

using json = nlohmann::json;

// Forward declarations
class Coordinator;
class Team;

// Message envelope for inter-agent communication
struct BusMessage {
    std::string id;
    std::string from;      // Agent ID
    std::string to;        // Target agent ID or "broadcast"
    std::string type;      // "task_result", "request", "info", "cancel"
    json payload;
    std::chrono::system_clock::time_point timestamp;
};

// Callback for message delivery
using MessageCallback = std::function<void(const BusMessage&)>;

// MessageBus — pub/sub message system for agent coordination
class MessageBus {
public:
    MessageBus() = default;
    ~MessageBus() = default;

    // Publish a message (sends to specific agent or broadcasts)
    void publish(BusMessage msg);

    // Subscribe to messages (receives messages addressed to agent_id or broadcasts)
    void subscribe(const std::string& agent_id, MessageCallback callback);

    // Unsubscribe
    void unsubscribe(const std::string& agent_id);

    // Get pending messages for an agent (polling interface)
    std::vector<BusMessage> poll(const std::string& agent_id);

    // Clear all pending messages for an agent
    void clear(const std::string& agent_id);

    // Get total message count
    size_t message_count() const;

    // Get subscriber count
    size_t subscriber_count() const;

private:
    struct Subscriber {
        std::string agent_id;
        MessageCallback callback;
    };

    std::vector<Subscriber> subscribers_;
    std::vector<BusMessage> pending_;
    mutable std::mutex mutex_;
    uint64_t next_id_ = 1;
};

}  // namespace agent
}  // namespace claude
