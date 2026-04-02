#pragma once

#include "engine/message.h"
#include <string>
#include <vector>

namespace claude {

// Context builder for system prompts and environment info
class ContextBuilder {
public:
    ContextBuilder() = default;

    // Build system context (git status, environment info, etc.)
    // This is prepended to each conversation and cached
    SystemPrompt build_system_context(const SessionConfig& config);

    // Build user context (CLAUDE.md, current date, etc.)
    SystemPrompt build_user_context(const SessionConfig& config);

    // Read CLAUDE.md files from the working directory tree
    std::string read_claude_md(const std::string& directory);

    // Get git status for the current working directory
    std::string get_git_status(const std::string& cwd);

    // Get environment information string
    std::string get_environment_info();

    // Get current date string
    std::string get_current_date() const;

    // Clear cached context (for when directory changes)
    void clear_cache();

private:
    // Cached system context
    std::string cached_system_context_;
    std::string cached_cwd_;
    bool system_context_cached_ = false;

    // Cached user context
    std::string cached_user_context_;
    bool user_context_cached_ = false;
};

}  // namespace claude
