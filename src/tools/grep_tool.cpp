#include "tools/grep_tool.h"
#include "util/process_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

ToolInputSchema GrepTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["pattern"] = "string — Regular expression pattern to search for";
    schema.properties["path"] = "string — Directory to search (default: working directory)";
    schema.properties["include"] = "string — File glob to include (e.g., '*.cpp')";
    schema.properties["exclude"] = "string — File glob to exclude (e.g., 'test_*')";
    schema.properties["type"] = "string — File type filter for rg (e.g., 'cpp', 'rust', 'py')";
    schema.properties["context"] = "number — Context lines before and after match (-C)";
    schema.properties["before_context"] = "number — Lines before match (-B)";
    schema.properties["after_context"] = "number — Lines after match (-A)";
    schema.properties["max_matches"] = "number — Maximum matches to return (default 50)";
    schema.properties["case_insensitive"] = "boolean — Case insensitive search (-i)";
    schema.required = {"pattern"};
    return schema;
}

std::string GrepTool::system_prompt() const {
    return R"(The Grep tool searches file contents using regular expressions.

**Parameters:**
- pattern (required): Regex pattern to search
- path: Directory to search (default: working directory)
- include: File pattern filter (e.g., "*.cpp")
- exclude: File pattern to exclude (e.g., "test_*")
- type: ripgrep file type (e.g., "cpp", "rust", "py", "js")
- context: Context lines before/after (shortcut for before_context + after_context)
- before_context: Lines before match
- after_context: Lines after match
- max_matches: Max results (default: 50)
- case_insensitive: Case insensitive search

**Uses ripgrep (rg) for fast searching.**
- Full regex syntax supported
- Returns file:line:content format)";
}

ToolOutput GrepTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string pattern = j.value("pattern", "");
        std::string search_path = j.value("path", "");
        std::string include = j.value("include", "");
        std::string exclude = j.value("exclude", "");
        std::string file_type = j.value("type", "");
        int max_matches = j.value("max_matches", 50);
        int context = j.value("context", 0);
        int before_context = j.value("before_context", 0);
        int after_context = j.value("after_context", 0);
        bool case_insensitive = j.value("case_insensitive", false);

        if (pattern.empty()) {
            return ToolOutput::err("No pattern specified");
        }

        fs::path base_dir = search_path.empty() ?
            fs::path(ctx.working_directory) : fs::path(search_path);
        if (base_dir.is_relative()) {
            base_dir = fs::path(ctx.working_directory) / base_dir;
        }

        // Build ripgrep command
        std::string cmd = "rg --no-heading --line-number --color never --no-messages";

        // Max matches
        cmd += " -m " + std::to_string(max_matches);

        // Context lines
        int effective_before = before_context > 0 ? before_context : context;
        int effective_after = after_context > 0 ? after_context : context;
        if (effective_before > 0) cmd += " -B " + std::to_string(effective_before);
        if (effective_after > 0) cmd += " -A " + std::to_string(effective_after);

        // Case insensitive
        if (case_insensitive) cmd += " -i";

        // File type
        if (!file_type.empty()) {
            cmd += " --type " + file_type;
        }

        // File filters
        if (!include.empty()) {
            cmd += " --glob '" + include + "'";
        }
        if (!exclude.empty()) {
            cmd += " --glob '!{" + exclude + "}'";
        }

        // Search path
        cmd += " " + base_dir.string() + " -- ";

        // Pattern (escape single quotes)
        std::string escaped_pattern;
        for (char c : pattern) {
            if (c == '\'') escaped_pattern += "'\\''";
            else escaped_pattern += c;
        }
        cmd += "'" + escaped_pattern + "'";

        cmd += " 2>/dev/null";

        util::ProcessOptions opts;
        opts.working_directory = ctx.working_directory;
        opts.timeout_seconds = 30;
        opts.max_output_bytes = 50000;

        auto result = util::exec_command(cmd, opts);

        if (result.timed_out) {
            return ToolOutput::err("Search timed out (try narrowing your search)");
        }

        if (result.exit_code != 0 && result.stdout_output.empty()) {
            return ToolOutput::ok("No matches found for: " + pattern);
        }

        // Count matches
        std::string output = result.stdout_output;
        if (output.size() > 50000) {
            output = output.substr(0, 50000) + "\n... (truncated)";
        }

        // Count match lines
        auto lines = util::split(output, "\n");
        int match_count = 0;
        for (const auto& line : lines) {
            if (!util::trim(line).empty()) match_count++;
        }

        return ToolOutput::ok("Found " + std::to_string(match_count) + " matches:\n" + output);

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
