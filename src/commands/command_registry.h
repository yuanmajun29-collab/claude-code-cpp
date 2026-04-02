#pragma once

#include <string>
#include <vector>
#include <functional>

namespace claude {

// Command registry (stub for P0)
class CommandRegistry {
public:
    CommandRegistry() = default;

    void register_command(const std::string& name,
                           const std::string& description,
                           std::function<int(const std::vector<std::string>&)> handler);

    std::vector<std::string> command_names() const;
    int execute(const std::string& name, const std::vector<std::string>& args);

private:
    struct Command {
        std::string name;
        std::string description;
        std::function<int(const std::vector<std::string>&)> handler;
    };
    std::vector<Command> commands_;
};

}  // namespace claude
