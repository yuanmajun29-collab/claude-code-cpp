#include "util/git_utils.h"
#include "util/process_utils.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace claude {
namespace util {

std::optional<std::string> git_exec(const std::string& cwd, const std::vector<std::string>& args) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 10;
    opts.merge_stderr = true;
    auto result = exec_program("git", args, opts);
    if (!result.success()) return std::nullopt;
    return result.stdout_output;
}

bool is_git_repo(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 5;
    opts.merge_stderr = true;
    opts.capture_output = false;
    auto result = exec_program("git", {"rev-parse", "--is-inside-work-tree"}, opts);
    return result.exit_code == 0 && result.stdout_output.find("true") != std::string::npos;
}

std::string get_git_branch(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 5;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"rev-parse", "--abbrev-ref", "HEAD"}, opts);
    return result.success() ? result.stdout_output : "unknown";
}

std::string get_git_status(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 10;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"--no-optional-locks", "status", "--short"}, opts);
    return result.success() ? result.stdout_output : "";
}

std::string get_git_log(const std::string& cwd, int n) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 10;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"--no-optional-locks", "log", "--oneline", "-n", std::to_string(n)}, opts);
    return result.success() ? result.stdout_output : "";
}

std::string get_git_user(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 5;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"config", "user.name"}, opts);
    return result.success() ? result.stdout_output : "";
}

std::string get_default_branch(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 5;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"symbolic-ref", "refs/remotes/origin/HEAD", "--short"}, opts);
    if (result.success() && !result.stdout_output.empty()) {
        auto ref = result.stdout_output;
        auto pos = ref.find("origin/");
        if (pos != std::string::npos) return ref.substr(pos + 7);
    }
    ProcessOptions opts2;
    opts2.working_directory = cwd;
    opts2.timeout_seconds = 5;
    opts2.merge_stderr = true;
    auto main_result = exec_program("git", {"rev-parse", "--verify", "refs/heads/main"}, opts2);
    if (main_result.success()) return "main";
    return "master";
}

std::string get_git_root(const std::string& cwd) {
    ProcessOptions opts;
    opts.working_directory = cwd;
    opts.timeout_seconds = 5;
    opts.merge_stderr = true;
    auto result = exec_program("git", {"rev-parse", "--show-toplevel"}, opts);
    return result.success() ? result.stdout_output : cwd;
}

}  // namespace util
}  // namespace claude
