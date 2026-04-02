#pragma once

#include "engine/message.h"
#include "engine/cost_tracker.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <memory>

namespace claude {

// Global application state
class AppState {
public:
    AppState();
    ~AppState() = default;

    // Session configuration
    SessionConfig& current_session() { return session_config_; }
    const SessionConfig& current_session() const { return session_config_; }

    // Session statistics
    SessionStats& stats() { return session_stats_; }
    const SessionStats& stats() const { return session_stats_; }

    // Message history
    std::vector<Message>& messages() { return messages_; }
    const std::vector<Message>& messages() const { return messages_; }
    void push_message(const Message& msg);
    void clear_messages();

    // Cost tracker
    CostTracker& cost_tracker() { return cost_tracker_; }
    const CostTracker& cost_tracker() const { return cost_tracker_; }

    // Configuration change callback
    using ConfigCallback = std::function<void()>;
    void on_config_change(ConfigCallback cb);
    void notify_config_change();

    // Session management
    void new_session();
    void save_session();
    void restore_session(const std::string& session_id);

    // Update stats
    void update_stats(const TokenUsage& usage);
    void increment_tool_calls();
    void increment_turns();

    // Working directory
    void set_working_directory(const std::string& dir);
    const std::string& working_directory() const { return session_config_.working_directory; }

private:
    std::vector<Message> messages_;
    SessionConfig session_config_;
    SessionStats session_stats_;
    CostTracker cost_tracker_;
    std::vector<ConfigCallback> config_callbacks_;
    mutable std::mutex mutex_;
};

}  // namespace claude
