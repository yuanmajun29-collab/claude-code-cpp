#include "tools/tool_registry.h"
#include <sstream>

namespace claude {

void ToolRegistry::register_tool(std::unique_ptr<ToolBase> tool) {
    if (!tool) return;
    std::lock_guard<std::mutex> lock(mutex_);
    tools_[tool->name()] = std::move(tool);
}

void ToolRegistry::register_factory(const std::string& /*name*/, ToolFactory factory) {
    auto tool = factory();
    if (tool) {
        register_tool(std::move(tool));
    }
}

ToolBase* ToolRegistry::find_tool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(name);
    return it != tools_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> ToolRegistry::tool_names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

std::vector<ToolBase*> ToolRegistry::tools_by_category(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ToolBase*> result;
    for (const auto& [_, tool] : tools_) {
        if (tool->category() == category) {
            result.push_back(tool.get());
        }
    }
    return result;
}

std::string ToolRegistry::tools_json_schema_array() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& [_, tool] : tools_) {
        if (!first) oss << ",";
        first = false;
        oss << "{"
            << "\"name\":\"" << tool->name() << "\","
            << "\"description\":\"" << tool->description() << "\","
            << "\"input_schema\":" << /* serialize schema */ "{}"
            << "}";
    }
    oss << "]";
    return oss.str();
}

std::string ToolRegistry::tools_system_prompt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    for (const auto& [_, tool] : tools_) {
        oss << "## " << tool->name() << "\n"
            << tool->system_prompt() << "\n\n";
    }
    return oss.str();
}

}  // namespace claude
