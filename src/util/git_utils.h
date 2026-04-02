#pragma once

#include <string>
#include <optional>
#include <vector>

namespace claude {
namespace util {

// Execute git command and return stdout
std::optional<std::string> git_exec(const std::string& cwd, const std::vector<std::string>& args);

// Check if directory is a git repo
bool is_git_repo(const std::string& cwd);

// Get current branch name
std::string get_git_branch(const std::string& cwd);

// Get git status output (short format)
std::string get_git_status(const std::string& cwd);

// Get last N commits (oneline)
std::string get_git_log(const std::string& cwd, int n = 5);

// Get git user name
std::string get_git_user(const std::string& cwd);

// Get default branch (main/master)
std::string get_default_branch(const std::string& cwd);

// Get git root directory
std::string get_git_root(const std::string& cwd);

}  // namespace util
}  // namespace claude
