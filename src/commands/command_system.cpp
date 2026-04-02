#include "commands/command_system.h"
#include "util/string_utils.h"
#include <algorithm>

namespace claude {

bool SlashCommand::matches(const std::string& input) const {
    if (input == name) return true;
    if (input == "/" + name) return true;
    for (const auto& alias : aliases) {
        if (input == alias || input == "/" + alias) return true;
    }
    return false;
}

void CommandSystem::register_command(const SlashCommand& cmd) {
    commands_[cmd.name] = cmd;
}

int CommandSystem::execute(const std::string& input) {
    std::string trimmed = util::trim(input);

    // Check if it's a command (starts with /)
    if (trimmed.empty() || trimmed[0] != '/') return -1;

    // Extract command name
    std::string cmd_name = trimmed.substr(1);
    auto space_pos = cmd_name.find(' ');
    std::string args_str;
    if (space_pos != std::string::npos) {
        args_str = cmd_name.substr(space_pos + 1);
        cmd_name = cmd_name.substr(0, space_pos);
    }

    // Find command (also check aliases)
    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        // Search aliases
        for (const auto& [name, cmd] : commands_) {
            for (const auto& alias : cmd.aliases) {
                if (alias == cmd_name) { it = commands_.find(name); break; }
            }
            if (it != commands_.end()) break;
        }
        if (it == commands_.end()) return -1;
    }

    // Parse args
    std::vector<std::string> args;
    if (!args_str.empty()) {
        args = util::split(args_str, ' ');
    }

    if (it->second.handler) {
        return it->second.handler(args);
    }
    return -1;
}

std::string CommandSystem::help_text() const {
    std::ostringstream oss;

    // Group by category
    std::map<std::string, std::vector<const SlashCommand*>> by_category;
    for (const auto& [_, cmd] : commands_) {
        by_category[cmd.category].push_back(&cmd);
    }

    for (const auto& [category, cmds] : by_category) {
        oss << "\n📋 " << category << ":\n";
        for (const auto* cmd : cmds) {
            oss << "  /" << cmd->name;
            for (const auto& alias : cmd->aliases) {
                oss << ", /" << alias;
            }
            oss << " — " << cmd->description << "\n";
        }
    }

    return oss.str();
}

std::vector<SlashCommand> CommandSystem::commands() const {
    std::vector<SlashCommand> result;
    for (const auto& [_, cmd] : commands_) {
        result.push_back(cmd);
    }
    return result;
}

const SlashCommand* CommandSystem::find(const std::string& name) const {
    auto it = commands_.find(name);
    if (it != commands_.end()) return &it->second;
    return nullptr;
}

void CommandSystem::register_builtins() {
    // Help
    register_command({
        "help", "Show available commands", "/help", "general",
        {"h"}, [](const auto&) -> int {
            // Help is rendered by the caller (since it needs access to CommandSystem)
            return 0;
        }
    });

    // Quit
    register_command({
        "quit", "Exit the application", "/quit", "general",
        {"exit", "q"}, [](const auto&) -> int { return 1; }
    });

    // Clear
    register_command({
        "clear", "Clear conversation history", "/clear", "session",
        {}, [](const auto&) -> int { return 100; }  // Special code: clear history
    });

    // Compact
    register_command({
        "compact", "Compact conversation context", "/compact", "session",
        {}, [](const auto&) -> int { return 101; }
    });

    // Status
    register_command({
        "status", "Show session statistics", "/status", "session",
        {}, [](const auto&) -> int { return 102; }
    });

    // Model
    register_command({
        "model", "Show or change the model", "/model [model_id]", "config",
        {}, [](const auto&) -> int { return 103; }
    });

    // Tools
    register_command({
        "tools", "List available tools", "/tools", "info",
        {}, [](const auto&) -> int { return 104; }
    });

    // Cost
    register_command({
        "cost", "Show token usage and cost", "/cost", "info",
        {}, [](const auto&) -> int { return 105; }
    });

    // MCP
    register_command({
        "mcp", "Manage MCP servers", "/mcp [list|add|remove]", "mcp",
        {}, [](const auto&) -> int { return 106; }
    });

    // Doctor
    register_command({
        "doctor", "Check system health and configuration", "/doctor", "debug",
        {}, [](const auto&) -> int { return 107; }
    });

    // Bug
    register_command({
        "bug", "Report a bug", "/bug", "general",
        {}, [](const auto&) -> int { return 108; }
    });
}

}  // namespace claude
