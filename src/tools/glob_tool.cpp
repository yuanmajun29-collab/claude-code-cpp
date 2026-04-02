#include "tools/glob_tool.h"
#include "util/file_utils.h"
#include "util/process_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

const std::vector<std::string>& GlobTool::exclude_patterns() {
    static const std::vector<std::string> excludes = {
        ".git/", ".git\\",
        "node_modules/", "node_modules\\",
        "__pycache__/", "__pycache__\\",
        ".hg/", ".hg\\",
        ".svn/", ".svn\\",
        ".DS_Store",
        "Thumbs.db",
        "*.pyc",
        ".venv/", "venv/",
        ".env",
        "dist/", "build/", ".cache/",
        "target/",  // Rust
        "vendor/",  // Go
    };
    return excludes;
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
    return R"(The Glob tool finds files matching a glob pattern.

**Parameters:**
- pattern (required): Glob pattern (*, **, ?, [])
- path: Base directory (default: working directory)

**Examples:**
- "**/*.cpp" — all .cpp files recursively
- "src/**/*.h" — all headers in src/
- "Makefile" — exact filename match

**Behavior:**
- Uses system glob matching
- Excludes .git, node_modules, __pycache__, etc.
- Returns matching file paths)";
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

        // Use find command for glob pattern matching
        // Convert glob to find-friendly format
        std::string find_cmd = "find " + base_dir.string() + " -type f";

        // Build exclude conditions
        for (const auto& exclude : exclude_patterns()) {
            // Remove trailing slash
            std::string exc = exclude;
            if (exc.back() == '/' || exc.back() == '\\') {
                exc = exc.substr(0, exc.size() - 1);
            }
            find_cmd += " ! -path '*" + exc + "*'";
        }

        // Add name pattern
        // For simple patterns like "*.cpp", use -name
        // For "**/*.cpp", use -name with the basename part
        std::string name_pattern = pattern;
        auto last_slash = pattern.rfind('/');
        if (last_slash != std::string::npos) {
            name_pattern = pattern.substr(last_slash + 1);
        }
        // Handle ** prefix
        if (name_pattern.find("**") == 0) {
            name_pattern = name_pattern.substr(2);
            if (name_pattern.find('/') == 0) name_pattern = name_pattern.substr(1);
        }

        if (!name_pattern.empty() && name_pattern != "*" && name_pattern != "**") {
            find_cmd += " -name '" + name_pattern + "'";
        }

        // Limit output
        find_cmd += " 2>/dev/null | head -500";

        util::ProcessOptions opts;
        opts.working_directory = ctx.working_directory;
        opts.timeout_seconds = 30;
        opts.merge_stderr = true;

        auto result = util::exec_command(find_cmd, opts);

        if (result.timed_out) {
            return ToolOutput::err("Glob command timed out");
        }

        if (result.exit_code != 0 && result.stdout_output.empty()) {
            // find might return 1 for permission errors but still output results
        }

        if (result.stdout_output.empty()) {
            return ToolOutput::ok("No files matched pattern: " + pattern);
        }

        // Format output
        auto files = util::split(result.stdout_output, "\n");
        std::ostringstream oss;
        for (const auto& f : files) {
            auto trimmed = util::trim(f);
            if (!trimmed.empty()) {
                oss << trimmed << "\n";
            }
        }

        std::string output = oss.str();
        auto count = util::split(output, "\n");
        // Count non-empty lines
        int file_count = 0;
        for (const auto& line : count) {
            if (!util::trim(line).empty()) file_count++;
        }

        return ToolOutput::ok(std::to_string(file_count) + " files matched:\n" + output);

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
