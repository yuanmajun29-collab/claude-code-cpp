#include "engine/context_builder.h"
#include "util/git_utils.h"
#include "util/platform.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include "util/process_utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace claude {

namespace fs = std::filesystem;

std::string ContextBuilder::get_current_date() const {
    return util::get_iso_date();
}

std::string ContextBuilder::read_claude_md(const std::string& directory) {
    fs::path dir(directory);
    if (dir.empty()) dir = fs::current_path();

    // Search for CLAUDE.md in current dir and parent dirs
    std::vector<fs::path> claude_mds;

    fs::path current = dir;
    while (true) {
        auto md_path = current / "CLAUDE.md";
        if (fs::exists(md_path)) {
            claude_mds.push_back(md_path);
        }
        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    // Also check .claude/CLAUDE.md
    auto dot_claude_md = dir / ".claude" / "CLAUDE.md";
    if (fs::exists(dot_claude_md)) {
        claude_mds.push_back(dot_claude_md);
    }

    if (claude_mds.empty()) return "";

    // Read all CLAUDE.md files and combine
    std::ostringstream result;
    for (const auto& path : claude_mds) {
        auto content = util::read_file(path);
        if (content) {
            result << "# " << path.string() << "\n\n" << *content << "\n\n";
        }
    }
    return result.str();
}

std::string ContextBuilder::get_git_status(const std::string& cwd) {
    if (!util::is_git_repo(cwd)) return "";

    try {
        std::string branch = util::get_git_branch(cwd);
        std::string main_branch = util::get_default_branch(cwd);
        std::string status = util::get_git_status(cwd);
        std::string log = util::get_git_log(cwd, 5);
        std::string user = util::get_git_user(cwd);

        std::ostringstream oss;
        oss << "This is the git status at the start of the conversation. "
            << "Note that this status is a snapshot in time, and will not update during the conversation.\n\n"
            << "Current branch: " << branch << "\n"
            << "Main branch (you will usually use this for PRs): " << main_branch << "\n";

        if (!user.empty()) {
            oss << "Git user: " << user << "\n";
        }

        oss << "\nStatus:\n" << (status.empty() ? "(clean)" : status) << "\n\n"
            << "Recent commits:\n" << (log.empty() ? "(none)" : log) << "\n";

        // Truncate if too long
        auto s = oss.str();
        if (s.size() > 2000) {
            return s.substr(0, 2000) + "\n... (truncated because it exceeds 2k characters. "
                   "If you need more information, run \"git status\" using BashTool)";
        }
        return s;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to get git status: {}", e.what());
        return "";
    }
}

std::string ContextBuilder::get_environment_info() {
    std::ostringstream oss;
    oss << "Working directory: " << util::current_working_directory() << "\n"
        << "Shell: " << util::get_shell_name() << "\n"
        << "OS: " << (util::is_linux ? "Linux" : util::is_macos ? "macOS" : "Unknown") << "\n"
        << "Date: " << get_current_date() << "\n";
    return oss.str();
}

SystemPrompt ContextBuilder::build_system_context(const SessionConfig& config) {
    std::string cwd = config.working_directory.empty() ? util::current_working_directory() : config.working_directory;

    // Use cached context if directory hasn't changed
    if (system_context_cached_ && cached_cwd_ == cwd) {
        return {cached_system_context_, std::nullopt};
    }

    std::ostringstream ctx;
    ctx << "<environment_info>\n" << get_environment_info() << "</environment_info>\n\n";

    std::string git = get_git_status(cwd);
    if (!git.empty()) {
        ctx << "<git_status>\n" << git << "</git_status>\n\n";
    }

    cached_system_context_ = ctx.str();
    cached_cwd_ = cwd;
    system_context_cached_ = true;

    return {cached_system_context_, std::nullopt};
}

SystemPrompt ContextBuilder::build_user_context(const SessionConfig& config) {
    if (user_context_cached_) {
        return {cached_user_context_, std::nullopt};
    }

    std::ostringstream ctx;

    std::string cwd = config.working_directory.empty() ? util::current_working_directory() : config.working_directory;
    std::string claude_md = read_claude_md(cwd);
    if (!claude_md.empty()) {
        ctx << claude_md << "\n";
    }

    ctx << "Today's date is " << get_current_date() << ".\n";

    cached_user_context_ = ctx.str();
    user_context_cached_ = true;

    return {cached_user_context_, std::nullopt};
}

void ContextBuilder::clear_cache() {
    system_context_cached_ = false;
    user_context_cached_ = false;
    cached_system_context_.clear();
    cached_user_context_.clear();
    cached_cwd_.clear();
}

}  // namespace claude
