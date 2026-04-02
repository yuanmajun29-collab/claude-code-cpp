#include "protocol/mcp/mcp_auth.h"
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace claude {
namespace mcp {

bool McpAuth::load_from_env(const std::string& env_var) {
    const char* val = std::getenv(env_var.c_str());
    if (val && val[0] != '\0') {
        token_ = val;
        spdlog::debug("MCP Auth: Loaded token from ${} (length={})", env_var, token_.size());
        return true;
    }
    return false;
}

}  // namespace mcp
}  // namespace claude
