#include "tools/file_write_tool.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

ToolInputSchema FileWriteTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["file_path"] = {"string", "The absolute path to the file to write"};
    schema.properties["content"] = {"string", "The content to write to the file"};
    schema.required = {"file_path", "content"};
    return schema;
}

std::string FileWriteTool::system_prompt() const {
    return R"(The Write tool writes content to a file, creating it if it doesn't exist. 
Overwrites the entire file content.

**Parameters:**
- file_path (required): Path to the file
- content (required): Content to write

**Behavior:**
- Creates parent directories if needed
- Uses atomic write (write to temp + rename) for safety
- Reports diff if overwriting an existing file
- Creates new files if they don't exist
- Use the Edit tool for partial modifications to existing files)";
}

ToolOutput FileWriteTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string file_path = j.value("file_path", "");
        std::string content = j.value("content", "");

        if (file_path.empty()) {
            return ToolOutput::err("No file path specified");
        }

        // Expand home directory
        fs::path path = util::expand_home(file_path);

        // Resolve relative paths
        if (path.is_relative()) {
            path = fs::path(ctx.working_directory) / path;
        }

        // Create parent directories
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);

        bool is_new_file = !fs::exists(path);
        std::string original_content;

        // Read original content for diff
        if (!is_new_file) {
            auto orig = util::read_file(path);
            if (orig) original_content = *orig;
        }

        // Atomic write: write to temp file first, then rename
        std::string temp_path = path.string() + ".claude_tmp_" + util::generate_uuid();

        {
            std::ofstream f(temp_path, std::ios::binary);
            if (!f) {
                return ToolOutput::err("Failed to create temp file: " + file_path);
            }
            f << content;
            if (!f.good()) {
                // Cleanup
                fs::remove(temp_path);
                return ToolOutput::err("Failed to write content: " + file_path);
            }
        }

        // Atomic rename
        std::error_code rename_ec;
        fs::rename(temp_path, path, rename_ec);
        if (rename_ec) {
            fs::remove(temp_path);
            return ToolOutput::err("Failed to write file (rename error): " + file_path);
        }

        // Build result with diff
        auto size = content.size();
        std::ostringstream result;

        if (is_new_file) {
            result << "Created new file: " << path.string() << "\n"
                   << "Size: " << util::format_bytes(size) << "\n";
        } else {
            auto change = static_cast<ssize_t>(size) - static_cast<ssize_t>(original_content.size());
            result << (change >= 0 ? "Updated" : "Updated") << " file: " << path.string() << "\n"
                   << "Size: " << util::format_bytes(size)
                   << " (" << (change >= 0 ? "+" : "") << change << " bytes)\n";

            // Generate simple diff
            if (original_content != content) {
                auto orig_lines = util::split(original_content, "\n");
                auto new_lines = util::split(content, "\n");

                // Find common prefix/suffix
                size_t prefix = 0;
                while (prefix < orig_lines.size() && prefix < new_lines.size() &&
                       orig_lines[prefix] == new_lines[prefix]) prefix++;

                size_t suffix = 0;
                while (suffix < (orig_lines.size() - prefix) &&
                       suffix < (new_lines.size() - prefix) &&
                       orig_lines[orig_lines.size() - 1 - suffix] == new_lines[new_lines.size() - 1 - suffix]) suffix++;

                if (orig_lines.size() - prefix - suffix > 0 || new_lines.size() - prefix - suffix > 0) {
                    result << "\nChanges:\n";
                    size_t old_start = prefix + 1;
                    size_t old_count = orig_lines.size() - prefix - suffix;
                    size_t new_start = prefix + 1;
                    size_t new_count = new_lines.size() - prefix - suffix;

                    result << "@@ -" << old_start << "," << old_count
                           << " +" << new_start << "," << new_count << " @@\n";

                    for (size_t i = prefix; i < orig_lines.size() - suffix; i++) {
                        result << "-" << orig_lines[i] << "\n";
                    }
                    for (size_t i = prefix; i < new_lines.size() - suffix; i++) {
                        result << "+" << new_lines[i] << "\n";
                    }
                }
            }
        }

        // Add file timestamp
        auto mtime = fs::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::clock_cast<std::chrono::system_clock>(mtime));
        auto tt = std::chrono::system_clock::to_time_t(sctp);
        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        result << "Modified: " << time_buf << "\n";

        spdlog::debug("Wrote {} bytes to {}", size, path.string());
        return ToolOutput::ok(result.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
