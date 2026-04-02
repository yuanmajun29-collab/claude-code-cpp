#include "agent/message_bus.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {
namespace agent {

// ============================================================
// MessageBus
// ============================================================

void MessageBus::publish(BusMessage msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (msg.id.empty()) {
        msg.id = "msg_" + std::to_string(next_id_++);
    }
    msg.timestamp = std::chrono::system_clock::now();

    // Deliver to matching subscribers
    bool delivered = false;
    for (const auto& sub : subscribers_) {
        if (msg.to == sub.agent_id || msg.to == "broadcast") {
            sub.callback(msg);
            delivered = true;
        }
    }

    // Store for polling
    pending_.push_back(std::move(msg));

    spdlog::debug("Bus: published message {} from {} to {} (delivered={})",
                  pending_.back().id, pending_.back().from, pending_.back().to, delivered);
}

void MessageBus::subscribe(const std::string& agent_id, MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.push_back({agent_id, std::move(callback)});
    spdlog::debug("Bus: {} subscribed", agent_id);
}

void MessageBus::unsubscribe(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [&](const Subscriber& s) { return s.agent_id == agent_id; }),
        subscribers_.end());
}

std::vector<BusMessage> MessageBus::poll(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BusMessage> result;
    for (const auto& msg : pending_) {
        if (msg.to == agent_id || msg.to == "broadcast") {
            result.push_back(msg);
        }
    }
    return result;
}

void MessageBus::clear(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
                       [&](const BusMessage& m) {
                           return m.to == agent_id || m.from == agent_id;
                       }),
        pending_.end());
}

size_t MessageBus::message_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

size_t MessageBus::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.size();
}

}  // namespace agent
}  // namespace claude
