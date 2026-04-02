#pragma once

#include <string>
#include <optional>
#include <functional>

namespace claude {

// Authentication types
enum class AuthType {
    ApiKey,
    OAuth
};

struct AuthResult {
    bool success = false;
    std::string token;
    std::string error;
};

// Authentication manager for Anthropic API
class AuthManager {
public:
    AuthManager() = default;

    // Authenticate using the specified type
    AuthResult authenticate(AuthType type = AuthType::ApiKey);

    // Refresh OAuth token
    void refresh_token();

    // Check if currently authenticated
    bool is_authenticated() const { return !current_token_.empty(); }

    // Get valid token (auto-refreshes if needed)
    std::string get_valid_token();

    // Set API key directly
    void set_api_key(const std::string& key);

    // Load API key from environment variable (ANTHROPIC_API_KEY)
    static std::string load_api_key_from_env();

    // Load API key from config file
    static std::string load_api_key_from_config();

    // Get the auth type
    AuthType auth_type() const { return auth_type_; }

private:
    std::string current_token_;
    AuthType auth_type_ = AuthType::ApiKey;
    std::string oauth_refresh_token_;
    std::string oauth_client_id_;
    bool oauth_configured_ = false;
};

}  // namespace claude
