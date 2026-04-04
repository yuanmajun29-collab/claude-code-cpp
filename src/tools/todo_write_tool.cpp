#include "tools/todo_write_tool.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

namespace claude {

ToolInputSchema TodoWriteTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["todos"] = {"array", "Array of todo items, each with id, content, status, and optional priority"};
    schema.required = {"todos"};
    return schema;
}

std::string TodoWriteTool::system_prompt() const {
    return R"(The TodoWrite tool manages the session task checklist.

**Parameters:**
- todos (required): Array of todo items, each with:
  - id (string): Unique identifier
  - content (string): Description of the task
  - status (string): "pending", "in_progress", or "completed"
  - priority (number, optional): Priority (higher = more important)

**Behavior:**
- Replaces the entire todo list with the provided items
- Use this to track progress on complex multi-step tasks
- Common pattern: read existing todos, update status, write back
- Good practice: mark items as in_progress when starting, completed when done)";
}

ToolOutput TodoWriteTool::execute(const std::string& input_json, ToolContext& /*ctx*/) {
    try {
        auto j = json::parse(input_json);

        if (!j.contains("todos") || !j["todos"].is_array()) {
            return ToolOutput::err("todos must be an array");
        }

        auto old_todos = todos_;
        todos_.clear();

        for (const auto& item : j["todos"]) {
            TodoItem todo;
            todo.id = item.value("id", "");
            todo.content = item.value("content", "");
            todo.status = item.value("status", "pending");
            todo.priority = item.value("priority", 0);

            // Validate status
            if (todo.status != "pending" && todo.status != "in_progress" &&
                todo.status != "completed") {
                todo.status = "pending";
            }

            if (todo.id.empty()) {
                todo.id = util::generate_uuid();
            }

            if (!todo.content.empty()) {
                todos_.push_back(todo);
            }
        }

        // Sort by priority (descending), then by pending first
        std::sort(todos_.begin(), todos_.end(), [](const TodoItem& a, const TodoItem& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            if (a.status != b.status) return a.status < b.status;  // pending < in_progress < completed
            return false;
        });

        // Build summary
        int completed = 0;
        int in_progress = 0;
        int pending = 0;
        for (const auto& t : todos_) {
            if (t.status == "completed") completed++;
            else if (t.status == "in_progress") in_progress++;
            else pending++;
        }

        std::ostringstream oss;
        oss << "Todo list updated (" << todos_.size() << " items):\n";

        if (!todos_.empty()) {
            for (const auto& t : todos_) {
                std::string icon;
                if (t.status == "completed") icon = "✅";
                else if (t.status == "in_progress") icon = "🔄";
                else icon = "⬜";

                oss << "  " << icon << " " << t.content;
                if (t.priority > 0) oss << " [priority:" << t.priority << "]";
                oss << "\n";
            }
        }

        oss << "\n  Summary: " << completed << " completed, " << in_progress
            << " in progress, " << pending << " pending";

        return ToolOutput::ok(oss.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
