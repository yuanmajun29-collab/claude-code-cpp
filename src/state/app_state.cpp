#include "state/app_state.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace claude {

using json = nlohmann::json;
namespace fs = std::filesystem;

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
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        // Build session data
        json session_data;
        session_data["session_id"] = session_config_.session_id;
        session_data["working_directory"] = session_config_.working_directory;

        // Save message history (last 200 messages)
        json msgs = json::array();
        int start = std::max(0, static_cast<int>(messages_.size()) - 200);
        for (int i = start; i < static_cast<int>(messages_.size()); i++) {
            json msg;
            msg["role"] = static_cast<int>(messages_[i].role);
            msg["content"] = messages_[i].text_content();
            msgs.push_back(msg);
        }
        session_data["messages"] = msgs;

        // Save stats
        session_data["stats"] = {
            {"total_input_tokens", session_stats_.total_input_tokens},
            {"total_output_tokens", session_stats_.total_output_tokens},
            {"total_tool_calls", session_stats_.total_tool_calls},
            {"total_turns", session_stats_.total_turns}
        };

        // Write to file (atomic: write temp + rename)
        auto path = std::filesystem::path(session_config_.working_directory) /
                    ".claude" / ("session_" + session_config_.session_id + ".json");
        auto tmp_path = path.string() + ".tmp";

        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(tmp_path);
        if (ofs.is_open()) {
            ofs << session_data.dump(2);
            ofs.close();
            std::filesystem::rename(tmp_path, path);
            spdlog::info("Session saved: {}", path.string());
        } else {
            spdlog::error("Failed to save session: {}", tmp_path);
        }
    } catch (const std::exception& e) {
        spdlog::error("Session save error: {}", e.what());
    }
}

void AppState::restore_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto path = std::filesystem::path(session_config_.working_directory) /
                    ".claude" / ("session_" + session_id + ".json");

        if (!std::filesystem::exists(path)) {
            spdlog::warn("Session file not found: {}", path.string());
            session_config_.session_id = session_id;
            return;
        }

        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            spdlog::error("Failed to open session file: {}", path.string());
            return;
        }

        json session_data = json::parse(ifs);
        ifs.close();

        session_config_.session_id = session_id;
        session_config_.working_directory = session_data.value("working_directory",
                                                                session_config_.working_directory);

        // Restore messages
        messages_.clear();
        if (session_data.contains("messages")) {
            for (const auto& m : session_data["messages"]) {
                Message msg;
                msg.role = static_cast<MessageRole>(m.value("role", 0));
                msg.content.push_back(ContentBlock::make_text(m.value("content", "")));
                messages_.push_back(msg);
            }
        }

        // Restore stats
        if (session_data.contains("stats")) {
            const auto& stats = session_data["stats"];
            session_stats_.total_input_tokens = stats.value("total_input_tokens", 0);
            session_stats_.total_output_tokens = stats.value("total_output_tokens", 0);
            session_stats_.total_tool_calls = stats.value("total_tool_calls", 0);
            session_stats_.total_turns = stats.value("total_turns", 0);
        }

        spdlog::info("Session restored: {} ({} messages)", session_id, messages_.size());
    } catch (const std::exception& e) {
        spdlog::error("Session restore error: {}", e.what());
    }
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
