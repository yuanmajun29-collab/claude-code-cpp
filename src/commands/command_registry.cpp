#include "commands/command_registry.h"

namespace claude {

void CommandRegistry::register_command(const std::string& name,
                                        const std::string& description,
                                        std::function<int(const std::vector<std::string>&)> handler) {
    commands_.push_back({name, description, std::move(handler)});
}

std::vector<std::string> CommandRegistry::command_names() const {
    std::vector<std::string> names;
    names.reserve(commands_.size());
    for (const auto& cmd : commands_) {
        names.push_back(cmd.name);
    }
    return names;
}

int CommandRegistry::execute(const std::string& name, const std::vector<std::string>& args) {
    for (const auto& cmd : commands_) {
        if (cmd.name == name) {
            if (cmd.handler) return cmd.handler(args);
            return 1;
        }
    }
    return 1;  // Command not found
}

}  // namespace claude
