#include "tools/glob_tool_native.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <regex>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

const std::vector<std::string>& GlobTool::exclude_patterns() {
    static const std::vector<std::string> excludes = {
        ".git",
        "node_modules",
        "__pycache__",
        ".hg",
        ".svn",
        ".DS_Store",
        "Thumbs.db",
        "*.pyc",
        ".venv",
        "venv",
        ".env",
        "dist",
        "build",
        ".cache",
        "target",  // Rust
        "vendor",  // Go
    };
    return excludes;
}

std::string GlobTool::glob_to_regex(const std::string& glob) const {
    std::string result;
    bool in_bracket = false;

    for (size_t i = 0; i < glob.size(); i++) {
        char c = glob[i];

        if (in_bracket) {
            if (c == ']') {
                in_bracket = false;
            }
            result += c;
            continue;
        }

        switch (c) {
            case '*':
                if (i + 1 < glob.size() && glob[i + 1] == '*') {
                    // ** matches any number of directories
                    result += ".*";
                    i++;  // Skip second *
                } else {
                    // * matches any characters except /
                    result += "[^/]*";
                }
                break;
            case '?':
                result += "[^/]";
                break;
            case '[':
                in_bracket = true;
                result += c;
                break;
            case ']':
                result += "\\]";
                break;
            case '.':
            case '+':
            case '(':
            case ')':
            case '|':
            case '^':
            case '$':
            case '\\':
                result += '\\';
                result += c;
                break;
            default:
                result += c;
                break;
        }
    }

    return "^" + result + "$";
}

bool GlobTool::is_excluded(const std::string& path, const std::string& base_dir) const {
    std::string relative_path;
    if (util::starts_with(path, base_dir)) {
        relative_path = path.substr(base_dir.size());
        if (!relative_path.empty() && relative_path[0] == '/') {
            relative_path = relative_path.substr(1);
        }
    } else {
        relative_path = path;
    }

    for (const auto& pattern : exclude_patterns()) {
        // Simple name match
        auto last_slash = pattern.find_last_of('/');
        std::string name_only = (last_slash == std::string::npos) ?
                               pattern : pattern.substr(last_slash + 1);

        // Check if any component matches
        std::vector<std::string> components = util::split(relative_path, "/");
        for (const auto& comp : components) {
            if (comp == pattern || comp == name_only) {
                return true;
            }
            // Wildcard match
            if (util::ends_with(pattern, "*")) {
                auto prefix = pattern.substr(0, pattern.size() - 1);
                if (util::starts_with(comp, prefix)) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool GlobTool::is_recursive_pattern(const std::string& pattern) const {
    return pattern.find("**") != std::string::npos;
}

std::pair<std::string, std::string> GlobTool::split_pattern(const std::string& pattern) const {
    auto last_slash = pattern.rfind('/');
    if (last_slash == std::string::npos) {
        return {".", pattern};
    }

    std::string dir = pattern.substr(0, last_slash);
    std::string file = pattern.substr(last_slash + 1);

    if (dir.empty()) {
        dir = ".";
    }

    return {dir, file};
}

bool GlobTool::match_pattern(const std::string& pattern, const std::string& path) const {
    try {
        std::string regex_str = glob_to_regex(pattern);
        std::regex re(regex_str, std::regex::icase);

        // Get filename from path
        auto filename = fs::path(path).filename().string();
        return std::regex_match(filename, re);
    } catch (const std::regex_error& e) {
        spdlog::error("Regex error for pattern '{}': {}", pattern, e.what());
        return false;
    }
}

std::vector<std::string> GlobTool::glob_recursive(
    const std::string& base_dir,
    const std::string& pattern,
    bool recursive) const {

    std::vector<std::string> results;
    auto [dir_part, file_part] = split_pattern(pattern);

    // Normalize directory part
    fs::path search_dir;
    if (fs::path(dir_part).is_absolute()) {
        search_dir = dir_part;
    } else {
        search_dir = fs::path(base_dir) / dir_part;
    }

    if (!fs::exists(search_dir)) {
        return results;
    }

    size_t max_results = 500;
    std::error_code ec;

    if (recursive) {
        // Recursive directory traversal
        auto dir_opts = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::recursive_directory_iterator(search_dir, dir_opts, ec)) {
            if (results.size() >= max_results) break;
            if (!entry.is_regular_file()) continue;

            auto path = entry.path().string();

            // Check exclusion
            if (is_excluded(path, base_dir)) {
                // Skip excluded directories
                if (entry.is_directory()) {
                    entry.disable_recursion_pending();
                }
                continue;
            }

            // Match filename pattern
            if (file_part.empty() || match_pattern(file_part, path)) {
                results.push_back(path);
            }
        }
    } else {
        // Non-recursive (current directory only)
        auto dir_opts = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::directory_iterator(search_dir, dir_opts, ec)) {
            if (results.size() >= max_results) break;
            if (!entry.is_regular_file()) continue;

            auto path = entry.path().string();

            // Check exclusion
            if (is_excluded(path, base_dir)) continue;

            // Match filename pattern
            if (file_part.empty() || match_pattern(file_part, path)) {
                results.push_back(path);
            }
        }
    }

    // Sort results
    std::sort(results.begin(), results.end());

    return results;
}

ToolInputSchema GlobTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["pattern"] = "string — Glob pattern to match files (e.g., '**/*.cpp', 'src/**/*.h')";
    schema.properties["path"] = "string — Base directory to search (default: working directory)";
    schema.required = {"pattern"};
    return schema;
}

std::string GlobTool::system_prompt() const {
    return R"(The Glob tool finds files matching glob patterns using native C++ implementation.

**Parameters:**
- pattern (required): Glob pattern (*, **, ?, [])
- path: Base directory (default: working directory)

**Examples:**
- "*.cpp" — all .cpp files in current directory
- "**/*.cpp" — all .cpp files recursively
- "src/**/*.h" — all .h files under src/
- "test_*.cpp" — all test_*.cpp files

**Pattern Syntax:**
- * matches any characters except /
- ** matches any number of directories
- ? matches any single character
- [abc] matches a, b, or c
- [a-z] matches any lowercase letter

**Excluded by default:**
- .git, node_modules, __pycache__, .hg, .svn
- .DS_Store, Thumbs.db, *.pyc, venv, dist, build
- Limit: 500 results max (to prevent excessive output))";
}

ToolOutput GlobTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string pattern = j.value("pattern", "");
        std::string base_dir = j.value("path", ctx.working_directory);

        if (pattern.empty()) {
            return ToolOutput::err("No pattern specified");
        }

        // Expand home directory
        base_dir = util::expand_home(base_dir);

        // Check if pattern is recursive
        bool recursive = is_recursive_pattern(pattern);

        // Perform glob
        auto results = glob_recursive(base_dir, pattern, recursive);

        // Format output
        std::ostringstream output;
        output << "Found " << results.size() << " files matching '" << pattern << "'";

        if (results.size() >= 500) {
            output << " (truncated at 500)";
        }
        output << ":\n\n";

        for (const auto& file : results) {
            output << file << "\n";
        }

        return ToolOutput::ok(output.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
