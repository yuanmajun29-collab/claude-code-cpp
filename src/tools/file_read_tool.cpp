#include "tools/file_read_tool.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include "util/crypto_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <map>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

// Image MIME types by extension
static const std::map<std::string, std::string> IMAGE_EXTENSIONS = {
    {".png", "image/png"},   {".jpg", "image/jpeg"},  {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},   {".webp", "image/webp"},  {".svg", "image/svg+xml"},
    {".bmp", "image/bmp"},   {".ico", "image/x-icon"},
};

// Magic bytes for common formats
static const std::vector<std::pair<std::vector<uint8_t>, std::string>> MAGIC_BYTES = {
    {{0x89, 0x50, 0x4E, 0x47}, "image/png"},
    {{0xFF, 0xD8, 0xFF}, "image/jpeg"},
    {{0x47, 0x49, 0x46, 0x38}, "image/gif"},
    {{0x52, 0x49, 0x46, 0x46}, "image/webp"},  // RIFF....WEBP
    {{0x42, 0x4D}, "image/bmp"},
    {{0x25, 0x50, 0x44, 0x46}, "application/pdf"},
    {{0x50, 0x4B, 0x03, 0x04}, "application/zip"},
};

ToolInputSchema FileReadTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["file_path"] = "string — Path to the file to read";
    schema.properties["offset"] = "number — Line number to start reading from (1-indexed)";
    schema.properties["limit"] = "number — Maximum number of lines to read";
    schema.required = {"file_path"};
    return schema;
}

std::string FileReadTool::system_prompt() const {
    return R"(The Read tool reads file contents.

**Parameters:**
- file_path (required): Path to the file
- offset: Start line (1-indexed), default 0 (beginning)
- limit: Max lines, default 0 (no limit)

**Behavior:**
- Text files: returns content with line numbers
- Image files (png, jpg, gif, webp, svg, bmp): returns base64-encoded with metadata
- Binary files: detected and reported with file size
- Truncates at 50KB or 2000 lines
- Automatically detects MIME type from magic bytes and extension))";
}

std::string FileReadTool::detect_mime_type(const std::string& path) const {
    // First check extension
    auto ext = fs::path(path).extension().string();
    util::to_lower(ext);
    auto it = IMAGE_EXTENSIONS.find(ext);
    if (it != IMAGE_EXTENSIONS.end()) return it->second;

    // Then check magic bytes
    std::ifstream f(path, std::ios::binary);
    if (!f) return "application/octet-stream";

    std::array<uint8_t, 16> header{};
    f.read(reinterpret_cast<char*>(header.data()), header.size());
    auto n = f.gcount();

    for (const auto& [magic, mime] : MAGIC_BYTES) {
        if (static_cast<size_t>(n) >= magic.size()) {
            bool match = true;
            for (size_t i = 0; i < magic.size(); i++) {
                if (header[i] != magic[i]) { match = false; break; }
            }
            if (match) return mime;
        }
    }

    // Check for text content
    for (int i = 0; i < n; i++) {
        if (header[i] == 0) return "application/octet-stream";
    }

    return "text/plain";
}

ToolOutput FileReadTool::read_image(const std::string& path) const {
    auto content = util::read_file(path);
    if (!content) {
        return ToolOutput::err("Failed to read file: " + path);
    }

    std::string mime = detect_mime_type(path);
    auto file_size = fs::file_size(path);

    // Encode to base64
    std::string base64 = util::base64_encode(*content);

    std::ostringstream output;
    output << "[Image: " << path << "]\n"
           << "Type: " << mime << "\n"
           << "Size: " << util::format_bytes(file_size) << "\n"
           << "Base64 length: " << base64.size() << " chars\n"
           << "data:" << mime << ";base64," << base64;

    return ToolOutput::ok(output.str());
}

ToolOutput FileReadTool::read_text(const std::string& path, int offset, int limit) const {
    auto lines = util::read_file_lines(path, offset, limit);
    if (!lines) {
        return ToolOutput::err("Failed to read file: " + path);
    }

    // Truncate if too many lines
    const size_t MAX_LINES = 2000;
    bool truncated = false;
    if (lines->size() > MAX_LINES) {
        truncated = true;
        lines->resize(MAX_LINES);
    }

    // Format with line numbers
    std::ostringstream oss;
    for (size_t i = 0; i < lines->size(); i++) {
        oss << std::setw(6) << (offset + i + 1) << " | " << lines->at(i) << "\n";
    }

    if (truncated) {
        oss << "\n[Output truncated: showing first " << MAX_LINES
            << " of total lines. Use offset/limit to paginate.]";
    }

    return ToolOutput::ok(oss.str());
}

ToolOutput FileReadTool::read_binary(const std::string& path) const {
    std::string mime = detect_mime_type(path);
    auto file_size = fs::file_size(path);

    std::ostringstream output;
    output << "[Binary file: " << path << "]\n"
           << "Type: " << mime << "\n"
           << "Size: " << util::format_bytes(file_size) << "\n"
           << "Cannot display binary content.";

    return ToolOutput::ok(output.str());
}

ToolOutput FileReadTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string file_path = j.value("file_path", "");
        int offset = j.value("offset", 0);
        int limit = j.value("limit", 0);

        if (file_path.empty()) {
            return ToolOutput::err("No file path specified");
        }

        // Expand home directory
        fs::path path = util::expand_home(file_path);

        // Resolve relative paths against working directory
        if (path.is_relative()) {
            path = fs::path(ctx.working_directory) / path;
        }

        // Normalize
        std::error_code ec;
        path = fs::canonical(path, ec);
        if (ec) {
            return ToolOutput::err("File not found: " + file_path);
        }

        if (!fs::exists(path)) {
            return ToolOutput::err("File not found: " + file_path);
        }

        if (!fs::is_regular_file(path)) {
            return ToolOutput::err("Not a regular file: " + file_path);
        }

        // Check file size limit (100MB)
        auto file_size = fs::file_size(path);
        if (file_size > 100 * 1024 * 1024) {
            return ToolOutput::err("File too large: " + util::format_bytes(file_size) + " (max 100MB)");
        }

        // Detect type and route to appropriate reader
        std::string mime = detect_mime_type(path.string());

        if (util::starts_with(mime, "image/")) {
            return read_image(path.string());
        } else if (mime == "application/octet-stream" || util::is_binary_file(path)) {
            return read_binary(path.string());
        } else {
            return read_text(path.string(), offset, limit);
        }

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
