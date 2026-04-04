#include "tools/glob_tool.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <fnmatch.h>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

const std::vector<std::string>& GlobTool::exclude_patterns() {
    static const std::vector<std::string> excludes = {
        ".git", "node_modules", "__pycache__",
        ".hg", ".svn", ".DS_Store", "Thumbs.db",
        ".venv", "venv", ".env",
        "dist", "build", ".cache",
        "target",  // Rust
        "vendor",  // Go
    };
    return excludes;
}

ToolInputSchema GlobTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["pattern"] = {"string", "Glob pattern to match files (e.g., '**/*.cpp', 'src/**/*.h')"};
    schema.properties["path"] = {"string", "Base directory to search (default: working directory)"};
    schema.required = {"pattern"};
    return schema;
}

std::string GlobTool::system_prompt() const {
    return R"(The Glob tool finds files matching a glob pattern.

**Parameters:**
- pattern (required): Glob pattern (*, **, ?, [])
- path: Base directory (default: working directory)

**Examples:**
- "**/*.cpp" — all .cpp files recursively
- "src/**/*.h" — all headers in src/
- "Makefile" — exact filename match

**Behavior:**
- Uses native C++ directory iteration
- Excludes .git, node_modules, __pycache__, etc.
- Returns matching file paths sorted by modification time
- Limited to 500 results)";
}

// Check if a path component should be excluded
static bool is_excluded_component(const std::string& component,
                                   const std::vector<std::string>& excludes) {
    for (const auto& exc : excludes) {
        if (component == exc) return true;
    }
    return false;
}

// Match a filename against a simple glob pattern (no directory separators)
static bool match_glob(const std::string& pattern, const std::string& str) {
    return fnmatch(pattern.c_str(), str.c_str(), 0) == 0;
}

// Split a pattern by '/' to get path components
static std::vector<std::string> split_path_pattern(const std::string& pattern) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : pattern) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

ToolOutput GlobTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string pattern = j.value("pattern", "");
        std::string search_path = j.value("path", "");

        if (pattern.empty()) {
            return ToolOutput::err("No pattern specified");
        }

        fs::path base_dir = search_path.empty() ?
            fs::path(ctx.working_directory) : util::expand_home(search_path);
        if (base_dir.is_relative()) {
            base_dir = fs::path(ctx.working_directory) / base_dir;
        }

        std::error_code ec;
        base_dir = fs::canonical(base_dir, ec);
        if (ec || !fs::is_directory(base_dir)) {
            return ToolOutput::err("Directory not found: " + search_path);
        }

        // Determine if pattern has directory components
        bool is_recursive = pattern.find("**") != std::string::npos;
        auto pattern_parts = split_path_pattern(pattern);

        // Extract the filename pattern (last component)
        std::string file_pattern;
        std::string dir_prefix;
        if (pattern_parts.empty()) {
            file_pattern = "*";
        } else {
            file_pattern = pattern_parts.back();
            // Build directory prefix (for patterns like "src/**/*.h")
            for (size_t i = 0; i + 1 < pattern_parts.size(); i++) {
                if (pattern_parts[i] != "**") {
                    if (!dir_prefix.empty()) dir_prefix += "/";
                    dir_prefix += pattern_parts[i];
                }
            }
        }

        // If we have a non-recursive prefix, adjust base_dir
        if (!dir_prefix.empty()) {
            fs::path new_base = base_dir / dir_prefix;
            if (fs::is_directory(new_base)) {
                base_dir = new_base;
            }
        }

        // Collect matching files
        const size_t MAX_RESULTS = 500;
        std::vector<std::pair<fs::path, fs::file_time_type>> matches;
        const auto& excludes = exclude_patterns();

        auto should_exclude = [&excludes](const fs::path& p) -> bool {
            for (const auto& component : p) {
                if (is_excluded_component(component.string(), excludes)) return true;
            }
            return false;
        };

        auto iterate = [&](auto iterator) {
            for (const auto& entry : iterator) {
                if (matches.size() >= MAX_RESULTS * 2) break;  // Safety limit during iteration

                if (!entry.is_regular_file()) continue;

                // Check exclusions on relative path
                auto rel = fs::relative(entry.path(), base_dir, ec);
                if (ec) continue;
                if (should_exclude(rel)) continue;

                // Match filename against pattern
                std::string filename = entry.path().filename().string();
                if (match_glob(file_pattern, filename)) {
                    fs::file_time_type mtime{};
                    std::error_code mtime_ec;
                    mtime = fs::last_write_time(entry.path(), mtime_ec);
                    matches.push_back({entry.path(), mtime});
                }
            }
        };

        if (is_recursive || file_pattern.find("**") != std::string::npos) {
            iterate(fs::recursive_directory_iterator(base_dir,
                fs::directory_options::skip_permission_denied, ec));
        } else {
            iterate(fs::directory_iterator(base_dir,
                fs::directory_options::skip_permission_denied, ec));
        }

        if (matches.empty()) {
            return ToolOutput::ok("No files matched pattern: " + pattern);
        }

        // Sort by modification time (newest first)
        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        // Limit results
        bool truncated = matches.size() > MAX_RESULTS;
        if (truncated) matches.resize(MAX_RESULTS);

        // Format output
        std::ostringstream oss;
        oss << matches.size() << " files matched";
        if (truncated) oss << " (showing first " << MAX_RESULTS << ")";
        oss << ":\n";

        for (const auto& [path, _mtime] : matches) {
            oss << path.string() << "\n";
        }

        return ToolOutput::ok(oss.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
