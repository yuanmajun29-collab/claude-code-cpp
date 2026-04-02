#pragma once

#include <string>
#include <functional>

namespace claude {
namespace ui {

// ANSI color codes
namespace color {
    inline const char* reset()   { return "\033[0m"; }
    inline const char* bold()    { return "\033[1m"; }
    inline const char* dim()     { return "\033[2m"; }
    inline const char* red()     { return "\033[31m"; }
    inline const char* green()   { return "\033[32m"; }
    inline const char* yellow()  { return "\033[33m"; }
    inline const char* blue()    { return "\033[34m"; }
    inline const char* magenta() { return "\033[35m"; }
    inline const char* cyan()    { return "\033[36m"; }
    inline const char* gray()    { return "\033[90m"; }
}

// Terminal capabilities
int terminal_width();
int terminal_height();
bool has_color();
bool is_tty();

// Styled text helpers
std::string bold(const std::string& text);
std::string dim(const std::string& text);
std::string red(const std::string& text);
std::string green(const std::string& text);
std::string yellow(const std::string& text);
std::string blue(const std::string& text);
std::string cyan(const std::string& text);
std::string gray(const std::string& text);

// Markdown-like rendering for terminal
std::string render_markdown(const std::string& text);

// Progress indicator
class ProgressSpinner {
public:
    ProgressSpinner(const std::string& message = "");
    ~ProgressSpinner();

    void start();
    void stop();
    void set_message(const std::string& message);
    bool is_running() const { return running_; }

private:
    std::string message_;
    bool running_ = false;
};

// Simple prompt helper (reads a line with optional prefix)
std::string read_line(const std::string& prompt = "");

// Confirmation prompt (y/n)
bool confirm(const std::string& question);

// Multi-choice selection
int select(const std::string& question, const std::vector<std::string>& options);

// Password input (no echo)
std::string read_password(const std::string& prompt = "");

// Clear screen
void clear_screen();

// Save/restore cursor position
void save_cursor();
void restore_cursor();
void move_cursor(int row, int col);

// Word wrap text to terminal width
std::string word_wrap(const std::string& text, int width = 0);

// Truncate to single line with ellipsis
std::string truncate_line(const std::string& text, int max_width);

}  // namespace ui
}  // namespace claude
