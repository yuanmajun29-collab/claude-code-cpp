#pragma once

#include <string>
#include <vector>
#include <optional>

namespace claude {

// Application configuration
struct AppConfig {
    // API settings
    std::string api_key;
    std::string model_id = "claude-sonnet-4-20250514";
    std::string base_url = "https://api.anthropic.com";
    int max_tokens = 8192;
    int timeout_seconds = 120;

    // Behavior settings
    std::string permission_mode = "default";
    bool auto_compact = true;
    int max_context_messages = 100;
    int max_tool_iterations = 100;
    bool thinking_enabled = false;
    int thinking_budget = 10000;

    // Display settings
    std::string theme = "dark";
    bool verbose = false;
    bool color = true;
    bool no_color = false;  // CLI flag: --no-color

    // Working directory
    std::string working_directory;

    // Prompt (non-interactive mode)
    std::string prompt;

    // Config file path
    std::string config_path;

    // Load from JSON config file
    static AppConfig load_from_file(const std::string& path);

    // Load from environment variables
    static AppConfig load_from_env();

    // Load with priority: CLI args > env vars > config file > defaults
    static AppConfig load(const std::string& config_path = "");

    // Save to JSON config file
    void save_to_file(const std::string& path) const;

    // Convert to JSON
    std::string to_json() const;

    // Get default config file path
    static std::string default_config_path();
};

}  // namespace claude
