#include "util/platform.h"
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <array>
#include <sys/ioctl.h>
#include <unistd.h>

namespace claude {
namespace util {

namespace fs = std::filesystem;

std::string get_home_dir() {
    const char* home = getenv("HOME");
    if (home) return home;
    return "/root";
}

std::string get_config_dir() {
    std::string base;
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        base = xdg;
    } else {
        base = get_home_dir() + "/.config";
    }
    return base + "/claude-code";
}

std::string get_data_dir() {
    std::string base;
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        base = xdg;
    } else {
        base = get_home_dir() + "/.local/share";
    }
    return base + "/claude-code";
}

std::string get_temp_dir() {
    const char* tmp = getenv("TMPDIR");
    if (tmp) return tmp;
    return "/tmp";
}

std::string get_shell_name() {
    const char* shell = getenv("SHELL");
    if (!shell) return "sh";
    fs::path p(shell);
    return p.filename().string();
}

int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80;
}

bool is_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

bool has_color_support() {
    if (!is_tty()) return false;
    const char* term = getenv("TERM");
    if (!term) return false;
    std::string t = term;
    return t != "dumb" && t != "emacs";
}

}  // namespace util
}  // namespace claude
