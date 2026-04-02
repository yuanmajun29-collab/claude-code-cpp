#pragma once

#include <string>
#include <optional>

namespace claude {
namespace util {

// Platform detection
#ifdef _WIN32
inline constexpr bool is_windows = true;
inline constexpr bool is_unix = false;
#else
inline constexpr bool is_windows = false;
inline constexpr bool is_unix = true;
#endif

#ifdef __APPLE__
inline constexpr bool is_macos = true;
#else
inline constexpr bool is_macos = false;
#endif

#ifdef __linux__
inline constexpr bool is_linux = true;
#else
inline constexpr bool is_linux = false;
#endif

// Get home directory
std::string get_home_dir();

// Get config directory (~/.config/claude-code on Linux, ~/Library/Application Support/claude-code on macOS)
std::string get_config_dir();

// Get data directory
std::string get_data_dir();

// Get temp directory
std::string get_temp_dir();

// Get shell name
std::string get_shell_name();

// Get terminal width (columns)
int get_terminal_width();

// Check if running in a TTY
bool is_tty();

// Check if running in a terminal with color support
bool has_color_support();

}  // namespace util
}  // namespace claude
