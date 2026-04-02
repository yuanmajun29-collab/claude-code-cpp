#include "state/app_state.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace claude {

AppState::AppState() {
    session_config_.session_id = util::generate_uuid();
    session_config_.working_directory = std::filesystem::current_path().string();
    session_stats_.session_start = std::chrono::system_clock::now();
    session_stats_.last_activity = std::chrono::system_clock::now();
}

void AppState::push_message(const Message& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(msg);
}

void AppState::clear_messages() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
}

void AppState::on_config_change(ConfigCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_callbacks_.push_back(std::move(cb));
}

void AppState::notify_config_change() {
    std::vector<ConfigCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks = config_callbacks_;
    }
    for (const auto& cb : callbacks) {
        try {
            cb();
        } catch (const std::exception& e) {
            spdlog::warn("Config callback error: {}", e.what());
        }
    }
}

void AppState::new_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    session_config_.session_id = util::generate_uuid();
    session_stats_ = SessionStats{};
    session_stats_.session_start = std::chrono::system_clock::now();
    session_stats_.last_activity = std::chrono::system_clock::now();
    cost_tracker_.reset();
    spdlog::info("New session: {}", session_config_.session_id);
}

void AppState::save_session() {
    // Session persistence will be implemented in later phases
    spdlog::debug("Session save (stub): {}", session_config_.session_id);
}

void AppState::restore_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_config_.session_id = session_id;
    // Session restoration will be implemented in later phases
    spdlog::info("Session restore (stub): {}", session_id);
}

void AppState::update_stats(const TokenUsage& usage) {
    session_stats_.total_input_tokens += usage.total_input();
    session_stats_.total_output_tokens += usage.output_tokens;
    session_stats_.last_activity = std::chrono::system_clock::now();
}

void AppState::increment_tool_calls() {
    session_stats_.total_tool_calls++;
    session_stats_.last_activity = std::chrono::system_clock::now();
}

void AppState::increment_turns() {
    session_stats_.total_turns++;
    session_stats_.last_activity = std::chrono::system_clock::now();
}

void AppState::set_working_directory(const std::string& dir) {
    session_config_.working_directory = dir;
}

}  // namespace claude
