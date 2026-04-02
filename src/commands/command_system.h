#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>

namespace claude {

// Slash command for the REPL
struct SlashCommand {
    std::string name;           // Command name (e.g., "help")
    std::string description;    // Short help text
    std::string usage;          // Usage string
    std::string category;       // Category for grouping
    std::vector<std::string> aliases;  // Alternate names
    std::function<int(const std::vector<std::string>&)> handler;

    bool matches(const std::string& input) const;
};

// Command system - manages all slash commands
class CommandSystem {
public:
    CommandSystem() = default;

    // Register a command
    void register_command(const SlashCommand& cmd);

    // Execute a command
    // Returns: 0=handled, -1=not a command, >0=error exit code
    int execute(const std::string& input);

    // Get help text
    std::string help_text() const;

    // Get all commands
    std::vector<SlashCommand> commands() const;

    // Find command by name
    const SlashCommand* find(const std::string& name) const;

    // Register built-in commands (session management)
    void register_builtins();

private:
    std::map<std::string, SlashCommand> commands_;
};

}  // namespace claude
