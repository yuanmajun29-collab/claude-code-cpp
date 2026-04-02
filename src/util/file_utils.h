#pragma once

#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdint>

namespace claude {
namespace util {

namespace fs = std::filesystem;

// Read entire file to string
inline std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Read file lines with optional offset/limit
inline std::optional<std::vector<std::string>> read_file_lines(
    const fs::path& path, int offset = 0, int limit = 0) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::vector<std::string> lines;
    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        if (line_num >= offset) {
            if (limit > 0 && static_cast<int>(lines.size()) >= limit) break;
            lines.push_back(line);
        }
        line_num++;
    }
    return lines;
}

// Write string to file (creates parent dirs)
inline bool write_file(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return true;
}

// Check if file exists
inline bool file_exists(const fs::path& path) {
    return fs::exists(path);
}

// Get file size in bytes
inline std::optional<uintmax_t> file_size(const fs::path& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec) return std::nullopt;
    return sz;
}

// Check if path is within a directory (security check)
inline bool is_path_within(const fs::path& path, const fs::path& base) {
    auto canonical_path = fs::weakly_canonical(path);
    auto canonical_base = fs::weakly_canonical(base);
    auto rel = fs::relative(canonical_path, canonical_base);
    // If the relative path starts with "..", it's outside
    auto rel_str = rel.string();
    if (rel_str.size() >= 2 && rel_str.substr(0, 2) == "..") return false;
    return !rel.empty();
}

// Expand ~ to home directory
inline fs::path expand_home(const fs::path& p) {
    auto s = p.string();
    if (s.size() >= 2 && s[0] == '~' && (s[1] == '/' || s[1] == fs::path::preferred_separator)) {
        const char* home = getenv("HOME");
        if (home) return fs::path(home) / s.substr(2);
    }
    return p;
}

// List files matching extension
inline std::vector<fs::path> list_files(const fs::path& dir,
                                         const std::vector<std::string>& extensions = {},
                                         bool recursive = false) {
    std::vector<fs::path> result;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return result;

    if (recursive) {
        for (auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                if (extensions.empty()) {
                    result.push_back(entry.path());
                } else {
                    auto ext = entry.path().extension().string();
                    for (const auto& e : extensions) {
                        if (ext == e) { result.push_back(entry.path()); break; }
                    }
                }
            }
        }
    } else {
        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                if (extensions.empty()) {
                    result.push_back(entry.path());
                } else {
                    auto ext = entry.path().extension().string();
                    for (const auto& e : extensions) {
                        if (ext == e) { result.push_back(entry.path()); break; }
                    }
                }
            }
        }
    }
    return result;
}

// Detect if file is binary by checking for null bytes in first 8KB
inline bool is_binary_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[8192];
    f.read(buf, sizeof(buf));
    auto n = f.gcount();
    for (int i = 0; i < n; i++) {
        if (static_cast<unsigned char>(buf[i]) == 0) return true;
    }
    return false;
}

}  // namespace util
}  // namespace claude
