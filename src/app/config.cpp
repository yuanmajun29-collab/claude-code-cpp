#include "app/config.h"
#include "util/platform.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

using json = nlohmann::json;

namespace claude {

namespace fs = std::filesystem;

std::string AppConfig::default_config_path() {
    auto config_dir = util::get_config_dir();
    return (fs::path(config_dir) / "config.json").string();
}

AppConfig AppConfig::load_from_env() {
    AppConfig config;
    const char* env;

    env = getenv("ANTHROPIC_API_KEY");
    if (env) config.api_key = env;

    env = getenv("ANTHROPIC_BASE_URL");
    if (env) config.base_url = env;

    env = getenv("CLAUDE_CODE_MODEL");
    if (env) config.model_id = env;

    env = getenv("CLAUDE_CODE_MAX_TOKENS");
    if (env) config.max_tokens = std::stoi(env);

    env = getenv("CLAUDE_CODE_PERMISSION_MODE");
    if (env) config.permission_mode = env;

    env = getenv("CLAUDE_CODE_THEME");
    if (env) config.theme = env;

    env = getenv("CLAUDE_CODE_VERBOSE");
    if (env && std::string(env) == "1") config.verbose = true;

    return config;
}

AppConfig AppConfig::load_from_file(const std::string& path) {
    AppConfig config;

    auto file_path = path.empty() ? default_config_path() : path;
    if (!fs::exists(file_path)) return config;

    try {
        std::ifstream f(file_path);
        if (!f.is_open()) return config;

        json j;
        f >> j;

        if (j.contains("api_key")) config.api_key = j["api_key"].get<std::string>();
        if (j.contains("model_id")) config.model_id = j["model_id"].get<std::string>();
        if (j.contains("base_url")) config.base_url = j["base_url"].get<std::string>();
        if (j.contains("max_tokens")) config.max_tokens = j["max_tokens"].get<int>();
        if (j.contains("timeout_seconds")) config.timeout_seconds = j["timeout_seconds"].get<int>();
        if (j.contains("permission_mode")) config.permission_mode = j["permission_mode"].get<std::string>();
        if (j.contains("auto_compact")) config.auto_compact = j["auto_compact"].get<bool>();
        if (j.contains("max_context_messages")) config.max_context_messages = j["max_context_messages"].get<int>();
        if (j.contains("max_tool_iterations")) config.max_tool_iterations = j["max_tool_iterations"].get<int>();
        if (j.contains("thinking_enabled")) config.thinking_enabled = j["thinking_enabled"].get<bool>();
        if (j.contains("thinking_budget")) config.thinking_budget = j["thinking_budget"].get<int>();
        if (j.contains("theme")) config.theme = j["theme"].get<std::string>();
        if (j.contains("verbose")) config.verbose = j["verbose"].get<bool>();
        if (j.contains("color")) config.color = j["color"].get<bool>();

    } catch (const json::parse_error& e) {
        // Ignore parse errors, use defaults
    } catch (const std::exception& e) {
        // Ignore other errors
    }

    return config;
}

AppConfig AppConfig::load(const std::string& config_path) {
    // Load defaults
    auto config = AppConfig{};

    // Load from config file
    auto from_file = load_from_file(config_path);

    // Load from environment (overrides file)
    auto from_env = load_from_env();

    // Merge: defaults < file < env
    auto merge = [](std::string& target, const std::string& file_val, const std::string& env_val) {
        target = env_val.empty() ? (file_val.empty() ? target : file_val) : env_val;
    };

    merge(config.api_key, from_file.api_key, from_env.api_key);
    merge(config.base_url, from_file.base_url, from_env.base_url);
    merge(config.model_id, from_file.model_id, from_env.model_id);
    merge(config.permission_mode, from_file.permission_mode, from_env.permission_mode);
    merge(config.theme, from_file.theme, from_env.theme);

    if (from_env.max_tokens > 0) config.max_tokens = from_env.max_tokens;
    else if (from_file.max_tokens > 0) config.max_tokens = from_file.max_tokens;

    if (from_env.timeout_seconds > 0) config.timeout_seconds = from_env.timeout_seconds;
    else if (from_file.timeout_seconds > 0) config.timeout_seconds = from_file.timeout_seconds;

    if (from_env.verbose) config.verbose = true;
    else if (from_file.verbose) config.verbose = true;

    if (config.working_directory.empty()) {
        config.working_directory = fs::current_path().string();
    }

    return config;
}

void AppConfig::save_to_file(const std::string& path) const {
    auto file_path = path.empty() ? default_config_path() : path;

    // Ensure directory exists
    auto parent = fs::path(file_path).parent_path();
    fs::create_directories(parent);

    json j;
    if (!api_key.empty()) j["api_key"] = api_key;
    j["model_id"] = model_id;
    j["base_url"] = base_url;
    j["max_tokens"] = max_tokens;
    j["timeout_seconds"] = timeout_seconds;
    j["permission_mode"] = permission_mode;
    j["auto_compact"] = auto_compact;
    j["max_context_messages"] = max_context_messages;
    j["max_tool_iterations"] = max_tool_iterations;
    j["thinking_enabled"] = thinking_enabled;
    j["thinking_budget"] = thinking_budget;
    j["theme"] = theme;
    j["verbose"] = verbose;
    j["color"] = color;

    std::ofstream f(file_path);
    f << j.dump(2);
}

std::string AppConfig::to_json() const {
    json j;
    j["model_id"] = model_id;
    j["base_url"] = base_url;
    j["max_tokens"] = max_tokens;
    j["permission_mode"] = permission_mode;
    j["theme"] = theme;
    return j.dump(2);
}

}  // namespace claude
