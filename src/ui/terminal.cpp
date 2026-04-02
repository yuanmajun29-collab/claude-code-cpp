#include "ui/terminal.h"
#include "util/platform.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <sstream>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

namespace claude {
namespace ui {

// ============================================================
// Terminal capabilities
// ============================================================

int terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_col;
    return 80;
}

int terminal_height() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_row;
    return 24;
}

bool has_color() {
    if (!is_tty()) return false;
    const char* term = getenv("TERM");
    return term && std::string(term) != "dumb";
}

bool is_tty() {
    return ::isatty(STDOUT_FILENO) != 0;
}

// ============================================================
// Styled text
// ============================================================

std::string bold(const std::string& t)    { return has_color() ? std::string("\033[1m") + t + "\033[0m" : t; }
std::string dim(const std::string& t)     { return has_color() ? std::string("\033[2m") + t + "\033[0m" : t; }
std::string red(const std::string& t)     { return has_color() ? std::string("\033[31m") + t + "\033[0m" : t; }
std::string green(const std::string& t)   { return has_color() ? std::string("\033[32m") + t + "\033[0m" : t; }
std::string yellow(const std::string& t)  { return has_color() ? std::string("\033[33m") + t + "\033[0m" : t; }
std::string blue(const std::string& t)    { return has_color() ? std::string("\033[34m") + t + "\033[0m" : t; }
std::string cyan(const std::string& t)    { return has_color() ? std::string("\033[36m") + t + "\033[0m" : t; }
std::string gray(const std::string& t)    { return has_color() ? std::string("\033[90m") + t + "\033[0m" : t; }

// ============================================================
// Markdown rendering (simplified)
// ============================================================

std::string render_markdown(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    std::istringstream stream(text);
    std::string line;
    bool in_code_block = false;

    while (std::getline(stream, line)) {
        // Code blocks
        if (line.find("```") == 0) {
            in_code_block = !in_code_block;
            if (in_code_block) {
                result += dim(line) + "\n";
            } else {
                result += dim(line) + "\n";
            }
            continue;
        }

        if (in_code_block) {
            result += dim("  " + line) + "\n";
            continue;
        }

        // Headers
        if (line.find("# ") == 0) {
            result += bold(yellow(line.substr(2))) + "\n";
            continue;
        }
        if (line.find("## ") == 0) {
            result += bold(line.substr(3)) + "\n";
            continue;
        }
        if (line.find("### ") == 0) {
            result += bold(cyan(line.substr(4))) + "\n";
            continue;
        }

        // Inline formatting (simplified)
        std::string rendered = line;
        // Bold: **text**
        // (Skipping inline formatting for simplicity in terminal)

        // Lists
        if (rendered.find("- ") == 0 || rendered.find("* ") == 0) {
            rendered = "  " + rendered;
        }

        // Numbered lists
        if (rendered.size() >= 3 && rendered[0] >= '0' && rendered[0] <= '9' && rendered[1] == '.') {
            // Keep as-is
        }

        result += rendered + "\n";
    }

    return result;
}

// ============================================================
// Progress spinner
// ============================================================

ProgressSpinner::ProgressSpinner(const std::string& message)
    : message_(message) {}

ProgressSpinner::~ProgressSpinner() { stop(); }

void ProgressSpinner::start() {
    if (running_) return;
    running_ = true;
    // Simple implementation: just print the message
    // Full animation would need a background thread
    std::cerr << color::yellow() << "⏳ " << message_ << color::reset() << std::flush;
}

void ProgressSpinner::stop() {
    if (!running_) return;
    running_ = false;
    std::cerr << "\r" << std::string(80, ' ') << "\r" << std::flush;
}

void ProgressSpinner::set_message(const std::string& message) {
    message_ = message;
    if (running_) {
        std::cerr << "\r" << std::string(80, ' ') << "\r"
                  << color::yellow() << "⏳ " << message_ << color::reset() << std::flush;
    }
}

// ============================================================
// Input helpers
// ============================================================

std::string read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (std::getline(std::cin, line)) return line;
    return "";
}

bool confirm(const std::string& question) {
    std::cout << question << " (y/n) " << std::flush;
    std::string answer;
    if (std::getline(std::cin, answer)) {
        char c = answer.empty() ? 'n' : answer[0];
        return c == 'y' || c == 'Y';
    }
    return false;
}

int select(const std::string& question, const std::vector<std::string>& options) {
    if (options.empty()) return -1;
    if (options.size() == 1) return 0;

    std::cout << question << "\n";
    for (size_t i = 0; i < options.size(); i++) {
        std::cout << "  " << cyan(std::to_string(i + 1) + ".") << " " << options[i] << "\n";
    }
    std::cout << "> " << std::flush;

    std::string answer;
    if (std::getline(std::cin, answer)) {
        try {
            int choice = std::stoi(answer);
            if (choice >= 1 && static_cast<size_t>(choice) <= options.size()) return choice - 1;
        } catch (...) {}
    }
    return -1;
}

std::string read_password(const std::string& prompt) {
    std::cout << prompt << std::flush;

    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    std::string password;
    std::getline(std::cin, password);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    std::cout << "\n" << std::flush;
    return password;
}

// ============================================================
// Terminal control
// ============================================================

void clear_screen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

void save_cursor() { std::cout << "\0337" << std::flush; }
void restore_cursor() { std::cout << "\0338" << std::flush; }
void move_cursor(int row, int col) {
    std::cout << "\033[" << row << ";" << col << "H" << std::flush;
}

// ============================================================
// Text formatting
// ============================================================

std::string word_wrap(const std::string& text, int width) {
    if (width <= 0) width = terminal_width() - 2;
    if (width < 20) width = 20;

    std::string result;
    std::istringstream words(text);
    std::string word;
    int col = 0;

    while (words >> word) {
        if (static_cast<int>(word.size()) > width) {
            // Break long words
            for (size_t i = 0; i < word.size(); i += width) {
                if (col > 0) { result += "\n"; col = 0; }
                result += word.substr(i, width);
                col = static_cast<int>(word.substr(i, width).size());
            }
        } else {
            if (col + static_cast<int>(word.size()) > width && col > 0) {
                result += "\n";
                col = 0;
            }
            if (col > 0) { result += " "; col++; }
            result += word;
            col += static_cast<int>(word.size());
        }
    }

    return result;
}

std::string truncate_line(const std::string& text, int max_width) {
    if (max_width <= 0) max_width = terminal_width() - 2;
    if (static_cast<int>(text.size()) <= max_width) return text;
    return text.substr(0, max_width - 3) + "...";
}

}  // namespace ui
}  // namespace claude
