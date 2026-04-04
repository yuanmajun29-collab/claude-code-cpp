#include "tools/tool_registry.h"
#include <sstream>

using json = nlohmann::json;

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

static json schema_to_json(const ToolInputSchema& schema) {
    json j;
    j["type"] = schema.type;

    json props = json::object();
    for (const auto& [name, prop] : schema.properties) {
        json p;
        p["type"] = prop.type;
        if (!prop.description.empty()) {
            p["description"] = prop.description;
        }
        if (!prop.enum_values.empty()) {
            p["enum"] = prop.enum_values;
        }
        if (prop.type == "array" && !prop.items_type.empty()) {
            p["items"] = json{{"type", prop.items_type}};
        }
        props[name] = p;
    }
    j["properties"] = props;

    if (!schema.required.empty()) {
        j["required"] = schema.required;
    }

    return j;
}

json ToolRegistry::tools_json_array() const {
    std::lock_guard<std::mutex> lock(mutex_);
    json arr = json::array();
    for (const auto& [_, tool] : tools_) {
        json t;
        t["name"] = tool->name();
        t["description"] = tool->description();
        t["input_schema"] = schema_to_json(tool->input_schema());
        arr.push_back(t);
    }
    return arr;
}

std::string ToolRegistry::tools_json_schema_array() const {
    return tools_json_array().dump();
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
