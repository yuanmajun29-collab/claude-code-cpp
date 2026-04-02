#include "api/auth.h"
#include "util/platform.h"
#include "util/file_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace claude {

namespace fs = std::filesystem;

std::string AuthManager::load_api_key_from_env() {
    const char* key = getenv("ANTHROPIC_API_KEY");
    if (key && key[0] != '\0') return key;
    return "";
}

std::string AuthManager::load_api_key_from_config() {
    auto config_dir = util::get_config_dir();
    auto config_path = fs::path(config_dir) / "config.json";

    std::ifstream f(config_path);
    if (!f.is_open()) return "";

    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("api_key")) {
            return j["api_key"].get<std::string>();
        }
    } catch (...) {
        // Ignore parse errors
    }
    return "";
}

AuthResult AuthManager::authenticate(AuthType type) {
    AuthResult result;
    auth_type_ = type;

    if (type == AuthType::ApiKey) {
        auto key = load_api_key_from_env();
        if (key.empty()) {
            key = load_api_key_from_config();
        }
        if (key.empty()) {
            result.success = false;
            result.error = "No API key found. Set ANTHROPIC_API_KEY environment variable or configure in config file.";
            return result;
        }
        current_token_ = key;
        result.success = true;
        result.token = key;
    } else if (type == AuthType::OAuth) {
        // OAuth 2.0 flow requires browser-based authorization
        // Placeholder for future implementation (requires OAuth provider setup)
        result.success = false;
        result.error = "OAuth 2.0 is not yet supported. "
                       "Please use API key authentication by setting ANTHROPIC_API_KEY.";
        spdlog::info("OAuth authentication requested but not implemented in v0.1");
    }

    return result;
}

void AuthManager::refresh_token() {
    if (auth_type_ == AuthType::OAuth) {
        // OAuth refresh flow - stub for P0
        spdlog::warn("OAuth token refresh not implemented");
    }
}

std::string AuthManager::get_valid_token() {
    if (current_token_.empty()) {
        auto result = authenticate(auth_type_);
        if (!result.success) return "";
        return result.token;
    }
    return current_token_;
}

void AuthManager::set_api_key(const std::string& key) {
    current_token_ = key;
    auth_type_ = AuthType::ApiKey;
}

}  // namespace claude
