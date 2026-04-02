#pragma once

#include "tools/tool_base.h"
#include <vector>
#include <string>
#include <optional>

namespace claude {

// Todo item
struct TodoItem {
    std::string id;
    std::string content;
    std::string status;  // "pending", "in_progress", "completed"
    int priority = 0;
};

// TodoWriteTool — manages a session task checklist
class TodoWriteTool : public ToolBase {
public:
    TodoWriteTool() = default;
    ~TodoWriteTool() override = default;

    std::string name() const override { return "TodoWrite"; }
    std::string description() const override {
        return "Manage the session task checklist. Create, update, and track todos for the current task.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "session"; }

    // Get current todo list
    const std::vector<TodoItem>& todos() const { return todos_; }

private:
    std::vector<TodoItem> todos_;
};

}  // namespace claude
