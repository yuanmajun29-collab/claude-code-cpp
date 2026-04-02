#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>

namespace claude {

enum class PermissionLevel {
    Allow,
    Deny,
    Ask
};

struct PermissionRule {
    std::string pattern;       // Glob pattern for matching
    PermissionLevel level = PermissionLevel::Ask;
    std::string description;
};

struct PermissionDecision {
    PermissionLevel level;
    std::string matched_rule;
    std::string reason;
};

// Permission manager with configurable rules
class PermissionManager {
public:
    PermissionManager() = default;

    // Add a rule
    void add_rule(const PermissionRule& rule);

    // Remove rules by pattern
    void remove_rule(const std::string& pattern);

    // Check permission for an action
    PermissionDecision check(const std::string& action, const std::string& target) const;

    // Set default permission level
    void set_default(PermissionLevel level) { default_level_ = level; }
    PermissionLevel default_level() const { return default_level_; }

    // Set/get mode
    void set_mode(const std::string& mode);
    std::string mode() const { return mode_; }

    // Load rules from config
    void load_defaults();

    // Simple glob matching
    static bool match_pattern(const std::string& pattern, const std::string& text);

    // Get all rules
    std::vector<PermissionRule> rules() const;

private:
    std::vector<PermissionRule> rules_;
    PermissionLevel default_level_ = PermissionLevel::Ask;
    std::string mode_ = "default";  // "default", "bypass", "yolo"
    mutable std::mutex mutex_;
};

// Sandbox execution context
class Sandbox {
public:
    enum class Level {
        None,       // No sandboxing
        Restricted, // Limited filesystem access
        Full        // Full isolation (future)
    };

    Sandbox() = default;
    explicit Sandbox(Level level);

    void set_level(Level level) { level_ = level; }
    Level level() const { return level_; }

    // Check if a path is accessible
    bool is_path_allowed(const std::string& path) const;

    // Check if a command is allowed
    bool is_command_allowed(const std::string& command) const;

    // Add allowed path prefix
    void allow_path(const std::string& prefix);

    // Add blocked path prefix
    void block_path(const std::string& prefix);

    // Add allowed command pattern
    void allow_command(const std::string& pattern);

    // Add blocked command pattern
    void block_command(const std::string& pattern);

    // Glob matching for command patterns
    static bool match_glob(const std::string& text, const std::string& pattern);

private:
    Level level_ = Level::None;
    std::vector<std::string> allowed_paths_;
    std::vector<std::string> blocked_paths_;
    std::vector<std::string> allowed_commands_;
    std::vector<std::string> blocked_commands_;
};

}  // namespace claude
