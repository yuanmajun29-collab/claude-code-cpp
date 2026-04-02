#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <random>
#include <iomanip>
#include <chrono>

namespace claude {
namespace util {

// Trim whitespace from both ends
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// Trim from left
inline std::string ltrim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    return start == std::string::npos ? "" : s.substr(start);
}

// Trim from right
inline std::string rtrim(const std::string& s) {
    auto end = s.find_last_not_of(" \t\n\r");
    return end == std::string::npos ? "" : s.substr(0, end + 1);
}

// Split string by delimiter
inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (std::getline(iss, part, delim)) {
        parts.push_back(part);
    }
    return parts;
}

// Split string by delimiter (string)
inline std::vector<std::string> split(const std::string& s, const std::string& delim) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t pos;
    while ((pos = s.find(delim, start)) != std::string::npos) {
        parts.push_back(s.substr(start, pos - start));
        start = pos + delim.size();
    }
    parts.push_back(s.substr(start));
    return parts;
}

// Join strings with delimiter
inline std::string join(const std::vector<std::string>& parts, const std::string& delim) {
    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) oss << delim;
        oss << parts[i];
    }
    return oss.str();
}

// Convert to lowercase
inline std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Convert to uppercase
inline std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

// Check if string starts with prefix
inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Check if string ends with suffix
inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Replace all occurrences
inline std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// Format bytes to human-readable
inline std::string format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && i < 4) {
        size /= 1024.0;
        i++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(i > 0 ? 1 : 0) << size << " " << units[i];
    return oss.str();
}

// Format duration in seconds to human-readable
inline std::string format_duration(double seconds) {
    if (seconds < 0.001) return "<1ms";
    if (seconds < 1.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (seconds * 1000) << "ms";
        return oss.str();
    }
    if (seconds < 60.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << seconds << "s";
        return oss.str();
    }
    int mins = static_cast<int>(seconds) / 60;
    double secs = seconds - mins * 60;
    std::ostringstream oss;
    oss << mins << "m" << std::fixed << std::setprecision(0) << secs << "s";
    return oss.str();
}

// Generate random UUID v4
inline std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xffffffff);

    uint32_t a = dis(gen), b = dis(gen), c = dis(gen), d = dis(gen);
    // Set version 4 bits
    c = (c & 0x0fffffff) | 0x40000000;
    // Set variant bits
    d = (d & 0x3fffffff) | 0x80000000;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << a << "-"
        << std::setw(4) << (b >> 16) << "-"
        << std::setw(4) << (c & 0xffff) << "-"
        << std::setw(4) << (d >> 16) << "-"
        << std::setw(12) << ((d & 0xffff) << 16 | (a & 0xffff));
    return oss.str();
}

// Get current ISO 8601 date
inline std::string get_iso_date() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

// Escape JSON string
inline std::string json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

}  // namespace util
}  // namespace claude
