#pragma once

#include "engine/message.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>

namespace claude {

// ============================================================
// Tool System - Abstract Base
// ============================================================

// JSON Schema fragment for tool input
struct ToolInputSchema {
    std::string type = "object";
    std::unordered_map<std::string, std::string> properties;  // name -> type description
    std::vector<std::string> required;
};

// Permission check result
enum class PermissionResult {
    Allowed,
    Denied,
    NeedsUserConfirmation
};

// Tool execution context
struct ToolContext {
    std::string working_directory;
    SessionConfig session_config;
    std::function<PermissionResult(const std::string&, const std::string&)> check_permission;
    std::function<void(const std::string&)> log_info;
    std::function<void(const std::string&)> log_error;
};

// Tool execution result
struct ToolOutput {
    bool success = true;
    std::string content;
    std::string error_message;
    bool is_error = false;

    static ToolOutput ok(const std::string& content) {
        return ToolOutput{true, content, "", false};
    }
    static ToolOutput err(const std::string& error) {
        return ToolOutput{false, "", error, true};
    }
};

// Progress callback for long-running tools
using ProgressCallback = std::function<void(int percent, const std::string& status)>;

// Abstract tool base class
class ToolBase {
public:
    virtual ~ToolBase() = default;

    // Tool identity
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // JSON Schema for input parameters
    virtual ToolInputSchema input_schema() const = 0;

    // System prompt description for the model
    virtual std::string system_prompt() const = 0;

    // Execute the tool
    virtual ToolOutput execute(const std::string& input_json, ToolContext& ctx) = 0;

    // Optional: progress support for long-running tools
    virtual bool supports_progress() const { return false; }

    // Whether tool needs user confirmation
    virtual bool requires_confirmation() const { return false; }

    // Category for UI grouping
    virtual std::string category() const { return "general"; }
};

}  // namespace claude
