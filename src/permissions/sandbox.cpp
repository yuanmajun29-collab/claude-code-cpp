#include "permissions/sandbox.h"
#include "util/string_utils.h"
#include <algorithm>
#include <filesystem>

namespace claude {

Sandbox::Sandbox(Level level) : level_(level) {}

bool Sandbox::is_path_allowed(const std::string& path) const {
    if (level_ == Level::None) return true;

    // Normalize path
    std::string norm_path = std::filesystem::absolute(path).string();

    // Check blocked paths first (deny takes priority)
    for (const auto& blocked : blocked_paths_) {
        std::string norm_blocked = std::filesystem::absolute(blocked).string();
        if (util::starts_with(norm_path, norm_blocked)) {
            return false;
        }
    }

    if (level_ == Level::Restricted) {
        // In Restricted mode, only allowed paths are accessible
        for (const auto& allowed : allowed_paths_) {
            std::string norm_allowed = std::filesystem::absolute(allowed).string();
            if (util::starts_with(norm_path, norm_allowed)) {
                return true;
            }
        }
        return false;
    }

    // Level::Full or no restrictions
    return true;
}

bool Sandbox::is_command_allowed(const std::string& command) const {
    if (level_ == Level::None) return true;

    // Extract command name (first word)
    std::string cmd_name = command;
    auto space = command.find(' ');
    if (space != std::string::npos) {
        cmd_name = command.substr(0, space);
    }
    // Remove path prefix
    auto slash = cmd_name.find_last_of('/');
    if (slash != std::string::npos) {
        cmd_name = cmd_name.substr(slash + 1);
    }

    // Check blocked commands
    for (const auto& blocked : blocked_commands_) {
        if (Sandbox::match_glob(cmd_name, blocked)) {
            return false;
        }
    }

    // In Restricted mode, only allowed commands
    if (level_ == Level::Restricted && !allowed_commands_.empty()) {
        for (const auto& allowed : allowed_commands_) {
            if (Sandbox::match_glob(cmd_name, allowed)) {
                return true;
            }
        }
        return false;
    }

    return true;
}

void Sandbox::allow_path(const std::string& prefix) {
    allowed_paths_.push_back(prefix);
}

void Sandbox::block_path(const std::string& prefix) {
    blocked_paths_.push_back(prefix);
}

void Sandbox::allow_command(const std::string& pattern) {
    allowed_commands_.push_back(pattern);
}

void Sandbox::block_command(const std::string& pattern) {
    blocked_commands_.push_back(pattern);
}

// Simple glob matching for command patterns
bool Sandbox::match_glob(const std::string& text, const std::string& pattern) {
    if (pattern == "*") return true;
    if (pattern == text) return true;

    // Simple * matching (no ** support needed for command names)
    auto star = pattern.find('*');
    if (star == std::string::npos) return text == pattern;

    std::string prefix = pattern.substr(0, star);
    std::string suffix = pattern.substr(star + 1);

    if (suffix.empty()) {
        return util::starts_with(text, prefix);
    }
    return util::starts_with(text, prefix) && util::ends_with(text, suffix) &&
           text.size() >= prefix.size() + suffix.size();
}

}  // namespace claude
