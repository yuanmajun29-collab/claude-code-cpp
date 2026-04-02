#include "tools/file_edit_tool.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

ToolInputSchema FileEditTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["file_path"] = "string — Path to the file to edit";
    schema.properties["old_string"] = "string — Exact text to find (for single replacement)";
    schema.properties["new_string"] = "string — Replacement text (for single replacement)";
    schema.properties["edits"] = "array — Array of {old_string, new_string} for multiple replacements";
    schema.required = {"file_path"};
    return schema;
}

std::string FileEditTool::system_prompt() const {
    return R"(The Edit tool makes precise edits to files using exact text replacement.

**Single replacement mode:**
- file_path, old_string, new_string
- old_string must match EXACTLY (including whitespace and line endings)

**Multi-replacement mode:**
- file_path, edits[{old_string, new_string}, ...]
- Each edit is matched independently against the ORIGINAL file
- Edits must not overlap

**Rules:**
- old_string must match a UNIQUE, NON-OVERLAPPING region
- Use enough surrounding context to make the match unique
- Do not include large unchanged regions just to connect distant changes
- For binary files, use Write instead
- Changes are atomic (write to temp + rename) — all edits succeed or fail together
- Detects concurrent modifications (file changed externally between read and write)

**Matching:**
- Case-sensitive, whitespace-sensitive
- Leading/trailing newlines matter
- Include enough context for uniqueness (3-5 lines typical))";
}

std::optional<std::string> FileEditTool::apply_replacement(const std::string& content,
                                                             const std::string& old_string,
                                                             const std::string& new_string,
                                                             int& match_count) {
    if (old_string.empty()) {
        return std::nullopt;
    }

    // Count matches
    match_count = 0;
    size_t pos = 0;
    while ((pos = content.find(old_string, pos)) != std::string::npos) {
        match_count++;
        pos += old_string.size();
    }

    if (match_count == 0) {
        return std::nullopt;
    }

    if (match_count > 1) {
        if (old_string == new_string) return content;
        return std::nullopt;
    }

    std::string result = content;
    result.replace(result.find(old_string), old_string.size(), new_string);
    return result;
}

std::string FileEditTool::generate_diff(const std::string& old_content,
                                          const std::string& new_content,
                                          const std::string& file_path) {
    auto old_lines = util::split(old_content, "\n");
    auto new_lines = util::split(new_content, "\n");

    // Find common prefix/suffix
    size_t prefix = 0;
    while (prefix < old_lines.size() && prefix < new_lines.size() &&
           old_lines[prefix] == new_lines[prefix]) prefix++;

    size_t suffix = 0;
    while (suffix < (old_lines.size() - prefix) &&
           suffix < (new_lines.size() - prefix) &&
           old_lines[old_lines.size() - 1 - suffix] == new_lines[new_lines.size() - 1 - suffix]) suffix++;

    std::ostringstream oss;
    oss << "--- " << file_path << "\n";
    oss << "+++ " << file_path << "\n";

    size_t old_start = prefix + 1;
    size_t old_count = old_lines.size() - prefix - suffix;
    size_t new_start = prefix + 1;
    size_t new_count = new_lines.size() - prefix - suffix;

    oss << "@@ -" << old_start;
    if (old_count != 1) oss << "," << old_count;
    oss << " +" << new_start;
    if (new_count != 1) oss << "," << new_count;
    oss << " @@\n";

    for (size_t i = prefix; i < old_lines.size() - suffix; i++) {
        oss << "-" << old_lines[i] << "\n";
    }
    for (size_t i = prefix; i < new_lines.size() - suffix; i++) {
        oss << "+" << new_lines[i] << "\n";
    }

    return oss.str();
}

ToolOutput FileEditTool::do_single_edit(const std::string& file_path,
                                          const std::string& old_string,
                                          const std::string& new_string,
                                          const std::string& cwd) {
    fs::path path = util::expand_home(file_path);
    if (path.is_relative()) {
        path = fs::path(cwd) / path;
    }

    if (!fs::exists(path)) {
        return ToolOutput::err("File not found: " + file_path);
    }

    // Record modification time for concurrent edit detection
    auto mod_time = fs::last_write_time(path);

    auto content_opt = util::read_file(path);
    if (!content_opt) {
        return ToolOutput::err("Failed to read file: " + file_path);
    }

    std::string old_content = *content_opt;
    int match_count = 0;
    auto new_content = apply_replacement(old_content, old_string, new_string, match_count);

    if (!new_content) {
        if (match_count == 0) {
            return ToolOutput::err(
                "old_string not found in file. Ensure it matches exactly "
                "(whitespace, case, newlines). Show more context to make the match unique.");
        } else {
            return ToolOutput::err("old_string found " + std::to_string(match_count) +
                                   " times. The match must be unique. Add more context to disambiguate.");
        }
    }

    if (old_content == *new_content) {
        return ToolOutput::ok("No changes needed (old_string == new_string)");
    }

    // Concurrent modification check
    auto current_mod_time = fs::last_write_time(path);
    if (current_mod_time != mod_time) {
        return ToolOutput::err(
            "File was modified externally between read and write. "
            "The file may have been changed by another process. "
            "Please re-read the file and try the edit again.");
    }

    // Generate diff before writing
    std::string diff = generate_diff(old_content, *new_content, path.string());

    // Atomic write
    std::string temp_path = path.string() + ".claude_tmp_" + util::generate_uuid();
    {
        std::ofstream f(temp_path, std::ios::binary);
        if (!f) {
            return ToolOutput::err("Failed to create temp file for atomic write");
        }
        f << *new_content;
    }

    std::error_code ec;
    fs::rename(temp_path, path, ec);
    if (ec) {
        fs::remove(temp_path);
        return ToolOutput::err("Failed to write file: " + file_path);
    }

    auto change_bytes = static_cast<ssize_t>(new_content->size()) - static_cast<ssize_t>(old_content.size());
    std::ostringstream result;
    result << "Edited " << path.string() << "\n"
           << "Change: " << (change_bytes >= 0 ? "+" : "") << change_bytes << " bytes\n\n"
           << diff;

    return ToolOutput::ok(result.str());
}

ToolOutput FileEditTool::do_multi_edit(const std::string& file_path,
                                         const json& edits_array,
                                         const std::string& cwd) {
    if (!edits_array.is_array() || edits_array.empty()) {
        return ToolOutput::err("edits must be a non-empty array");
    }

    fs::path path = util::expand_home(file_path);
    if (path.is_relative()) {
        path = fs::path(cwd) / path;
    }

    if (!fs::exists(path)) {
        return ToolOutput::err("File not found: " + file_path);
    }

    // Record modification time
    auto mod_time = fs::last_write_time(path);

    auto content_opt = util::read_file(path);
    if (!content_opt) {
        return ToolOutput::err("Failed to read file: " + file_path);
    }

    std::string current_content = *content_opt;
    std::string original_content = current_content;
    int total_edits = 0;

    for (const auto& edit : edits_array) {
        std::string old_str = edit.value("old_string", "");
        std::string new_str = edit.value("new_string", "");

        if (old_str.empty()) {
            return ToolOutput::err("edits[].old_string cannot be empty");
        }

        int match_count = 0;
        auto result = apply_replacement(current_content, old_str, new_str, match_count);

        if (!result) {
            if (match_count == 0) {
                std::string preview = old_str.size() > 80 ? old_str.substr(0, 80) + "..." : old_str;
                return ToolOutput::err("old_string not found in file: \"" + preview + "\"");
            } else {
                return ToolOutput::err("old_string matched " + std::to_string(match_count) +
                                       " times. Each match must be unique.");
            }
        }

        current_content = *result;
        total_edits++;
    }

    // Concurrent modification check
    auto current_mod_time = fs::last_write_time(path);
    if (current_mod_time != mod_time) {
        return ToolOutput::err(
            "File was modified externally during edit. "
            "Please re-read and retry.");
    }

    if (current_content == original_content) {
        return ToolOutput::ok("No changes made (all edits were no-ops)");
    }

    // Generate combined diff
    std::string diff = generate_diff(original_content, current_content, path.string());

    // Atomic write
    std::string temp_path = path.string() + ".claude_tmp_" + util::generate_uuid();
    {
        std::ofstream f(temp_path, std::ios::binary);
        if (!f) {
            return ToolOutput::err("Failed to create temp file");
        }
        f << current_content;
    }

    std::error_code ec;
    fs::rename(temp_path, path, ec);
    if (ec) {
        fs::remove(temp_path);
        return ToolOutput::err("Failed to write file: " + file_path);
    }

    auto change_bytes = static_cast<ssize_t>(current_content.size()) -
                        static_cast<ssize_t>(original_content.size());
    std::ostringstream result;
    result << "Edited " << path.string() << " (" << total_edits << " changes)\n"
           << "Change: " << (change_bytes >= 0 ? "+" : "") << change_bytes << " bytes\n\n"
           << diff;

    return ToolOutput::ok(result.str());
}

ToolOutput FileEditTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string file_path = j.value("file_path", "");

        if (file_path.empty()) {
            return ToolOutput::err("No file path specified");
        }

        if (j.contains("edits")) {
            return do_multi_edit(file_path, j["edits"], ctx.working_directory);
        }

        if (!j.contains("old_string") || !j.contains("new_string")) {
            return ToolOutput::err("Must provide old_string and new_string, or edits array");
        }

        return do_single_edit(file_path,
                               j.value("old_string", ""),
                               j.value("new_string", ""),
                               ctx.working_directory);

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
