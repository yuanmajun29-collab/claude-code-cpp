#include "tools/lsp_tool.h"
#include "tools/tool_registry.h"
#include "protocol/lsp/lsp_types.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

// ============================================================
// LSP sub-tools (individual tool classes for each LSP feature)
// ============================================================

class LspGoToDefinitionTool : public ToolBase {
public:
    explicit LspGoToDefinitionTool(LspToolManager& mgr) : mgr_(mgr) {}
    std::string name() const override { return "lsp_definition"; }
    std::string description() const override { return "Go to definition of symbol at position"; }
    std::string category() const override { return "lsp"; }
    ToolInputSchema input_schema() const override {
        ToolInputSchema s;
        s.type = "object";
        s.properties["language"] = {"string", "Language (e.g., cpp, python, typescript)"};
        s.properties["uri"] = {"string", "File URI"};
        s.properties["line"] = {"number", "Line number (0-based)"};
        s.properties["character"] = {"number", "Column (0-based)"};
        s.required = {"language", "uri", "line", "character"};
        return s;
    }
    std::string system_prompt() const override {
        return "Use 'lsp_definition' to find where a symbol is defined.";
    }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override {
        (void)ctx;
        try {
            auto input = nlohmann::json::parse(input_json);
            std::string lang = input.value("language", "");
            std::string uri = input.value("uri", "");
            int line = input.value("line", 0);
            int character = input.value("character", 0);

            auto locations = mgr_.go_to_definition(lang, uri, line, character);
            if (locations.empty()) {
                return ToolOutput::ok("No definition found");
            }

            std::string output;
            for (const auto& loc : locations) {
                output += loc.uri + ":" + std::to_string(loc.range.start.line + 1) +
                          ":" + std::to_string(loc.range.start.character + 1) + "\n";
            }
            return ToolOutput::ok(output);
        } catch (const std::exception& e) {
            return ToolOutput::err(std::string("LSP error: ") + e.what());
        }
    }
private:
    LspToolManager& mgr_;
};

class LspFindReferencesTool : public ToolBase {
public:
    explicit LspFindReferencesTool(LspToolManager& mgr) : mgr_(mgr) {}
    std::string name() const override { return "lsp_references"; }
    std::string description() const override { return "Find all references to a symbol"; }
    std::string category() const override { return "lsp"; }
    ToolInputSchema input_schema() const override {
        ToolInputSchema s;
        s.type = "object";
        s.properties["language"] = {"string", "Language"};
        s.properties["uri"] = {"string", "File URI"};
        s.properties["line"] = {"number", "Line number (0-based)"};
        s.properties["character"] = {"number", "Column (0-based)"};
        s.required = {"language", "uri", "line", "character"};
        return s;
    }
    std::string system_prompt() const override {
        return "Use 'lsp_references' to find all usages of a symbol.";
    }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override {
        (void)ctx;
        try {
            auto input = nlohmann::json::parse(input_json);
            std::string lang = input.value("language", "");
            std::string uri = input.value("uri", "");
            int line = input.value("line", 0);
            int character = input.value("character", 0);

            auto locations = mgr_.find_references(lang, uri, line, character);
            if (locations.empty()) {
                return ToolOutput::ok("No references found");
            }

            std::string output;
            for (const auto& loc : locations) {
                output += loc.uri + ":" + std::to_string(loc.range.start.line + 1) + "\n";
            }
            return ToolOutput::ok(output);
        } catch (const std::exception& e) {
            return ToolOutput::err(std::string("LSP error: ") + e.what());
        }
    }
private:
    LspToolManager& mgr_;
};

class LspDiagnosticsTool : public ToolBase {
public:
    explicit LspDiagnosticsTool(LspToolManager& mgr) : mgr_(mgr) {}
    std::string name() const override { return "lsp_diagnostics"; }
    std::string description() const override { return "Get diagnostics (errors, warnings) for a file"; }
    std::string category() const override { return "lsp"; }
    ToolInputSchema input_schema() const override {
        ToolInputSchema s;
        s.type = "object";
        s.properties["language"] = {"string", "Language"};
        s.properties["uri"] = {"string", "File URI"};
        s.required = {"language", "uri"};
        return s;
    }
    std::string system_prompt() const override {
        return "Use 'lsp_diagnostics' to check for errors and warnings in a file.";
    }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override {
        (void)ctx;
        try {
            auto input = nlohmann::json::parse(input_json);
            std::string lang = input.value("language", "");
            std::string uri = input.value("uri", "");

            auto diags = mgr_.get_diagnostics(lang, uri);
            if (diags.empty()) {
                return ToolOutput::ok("No diagnostics");
            }

            std::string output;
            for (const auto& d : diags) {
                output += d + "\n";
            }
            return ToolOutput::ok(output);
        } catch (const std::exception& e) {
            return ToolOutput::err(std::string("LSP error: ") + e.what());
        }
    }
private:
    LspToolManager& mgr_;
};

class LspHoverTool : public ToolBase {
public:
    explicit LspHoverTool(LspToolManager& mgr) : mgr_(mgr) {}
    std::string name() const override { return "lsp_hover"; }
    std::string description() const override { return "Get hover documentation for a symbol"; }
    std::string category() const override { return "lsp"; }
    ToolInputSchema input_schema() const override {
        ToolInputSchema s;
        s.type = "object";
        s.properties["language"] = {"string", "Language"};
        s.properties["uri"] = {"string", "File URI"};
        s.properties["line"] = {"number", "Line (0-based)"};
        s.properties["character"] = {"number", "Column (0-based)"};
        s.required = {"language", "uri", "line", "character"};
        return s;
    }
    std::string system_prompt() const override {
        return "Use 'lsp_hover' to get documentation for a symbol at a position.";
    }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override {
        (void)ctx;
        try {
            auto input = nlohmann::json::parse(input_json);
            std::string lang = input.value("language", "");
            std::string uri = input.value("uri", "");
            int line = input.value("line", 0);
            int character = input.value("character", 0);

            auto hover = mgr_.hover(lang, uri, line, character);
            if (!hover) {
                return ToolOutput::ok("No hover information");
            }
            return ToolOutput::ok(hover->contents);
        } catch (const std::exception& e) {
            return ToolOutput::err(std::string("LSP error: ") + e.what());
        }
    }
private:
    LspToolManager& mgr_;
};

class LspRenameTool : public ToolBase {
public:
    explicit LspRenameTool(LspToolManager& mgr) : mgr_(mgr) {}
    std::string name() const override { return "lsp_rename"; }
    std::string description() const override { return "Rename a symbol across the project"; }
    std::string category() const override { return "lsp"; }
    ToolInputSchema input_schema() const override {
        ToolInputSchema s;
        s.type = "object";
        s.properties["language"] = {"string", "Language"};
        s.properties["uri"] = {"string", "File URI"};
        s.properties["line"] = {"number", "Line (0-based)"};
        s.properties["character"] = {"number", "Column (0-based)"};
        s.properties["new_name"] = {"string", "New symbol name"};
        s.required = {"language", "uri", "line", "character", "new_name"};
        return s;
    }
    std::string system_prompt() const override {
        return "Use 'lsp_rename' to rename a symbol across all files in the project.";
    }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override {
        (void)ctx;
        try {
            auto input = nlohmann::json::parse(input_json);
            std::string lang = input.value("language", "");
            std::string uri = input.value("uri", "");
            int line = input.value("line", 0);
            int character = input.value("character", 0);
            std::string new_name = input.value("new_name", "");

            auto edit = mgr_.rename(lang, uri, line, character, new_name);
            if (!edit) {
                return ToolOutput::err("Rename failed or not supported");
            }

            // edit is std::optional, the map access should work
            std::string output = "Rename would affect " + std::to_string(edit->changes.size()) + " file(s)";
            return ToolOutput::ok(output);
        } catch (const std::exception& e) {
            return ToolOutput::err(std::string("LSP error: ") + e.what());
        }
    }
private:
    LspToolManager& mgr_;
};

// ============================================================
// LspToolManager implementation
// ============================================================

LspToolManager::LspToolManager() = default;

void LspToolManager::register_tools(ToolRegistry& registry) {
    auto def_tool = std::make_unique<LspGoToDefinitionTool>(*this);
    auto ref_tool = std::make_unique<LspFindReferencesTool>(*this);
    auto diag_tool = std::make_unique<LspDiagnosticsTool>(*this);
    auto hover_tool = std::make_unique<LspHoverTool>(*this);
    auto rename_tool = std::make_unique<LspRenameTool>(*this);

    if (!registry.has_tool("lsp_definition"))
        registry.register_tool(std::move(def_tool));
    if (!registry.has_tool("lsp_references"))
        registry.register_tool(std::move(ref_tool));
    if (!registry.has_tool("lsp_diagnostics"))
        registry.register_tool(std::move(diag_tool));
    if (!registry.has_tool("lsp_hover"))
        registry.register_tool(std::move(hover_tool));
    if (!registry.has_tool("lsp_rename"))
        registry.register_tool(std::move(rename_tool));
}

bool LspToolManager::start_server(const std::string& language, const std::string& root_uri,
                                   const std::vector<std::string>& command) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (servers_.count(language)) {
        spdlog::warn("LSP: Server for '{}' already running", language);
        return true;
    }

    auto client = std::make_unique<lsp::LspClient>();
    if (!client->start(command, root_uri)) {
        return false;
    }

    servers_[language] = std::move(client);
    spdlog::info("LSP: Server for '{}' started", language);
    return true;
}

void LspToolManager::stop_server(const std::string& language) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it != servers_.end()) {
        it->second->stop();
        servers_.erase(it);
        spdlog::info("LSP: Server for '{}' stopped", language);
    }
}

bool LspToolManager::is_running(const std::string& language) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    return it != servers_.end() && it->second->is_running();
}

bool LspToolManager::open_document(const std::string& language, const std::string& uri,
                                    const std::string& content, int version) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return false;
    it->second->did_open(uri, language, content, version);
    return true;
}

bool LspToolManager::update_document(const std::string& language, const std::string& uri,
                                      const std::string& content, int version) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return false;
    it->second->did_change(uri, content, version);
    return true;
}

bool LspToolManager::close_document(const std::string& language, const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return false;
    it->second->did_close(uri);
    return true;
}

std::vector<std::string> LspToolManager::get_diagnostics(const std::string& language,
                                                           const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return {};
    auto diags = it->second->get_diagnostics(uri);
    std::vector<std::string> result;
    for (const auto& d : diags) {
        result.push_back(d.message);
    }
    return result;
}

std::vector<lsp::Location> LspToolManager::go_to_definition(const std::string& language,
                                                             const std::string& uri,
                                                             int line, int character) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return {};
    return it->second->go_to_definition(uri, {line, character});
}

std::vector<lsp::Location> LspToolManager::find_references(const std::string& language,
                                                            const std::string& uri,
                                                            int line, int character) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return {};
    return it->second->find_references(uri, {line, character});
}

std::optional<lsp::HoverResult> LspToolManager::hover(const std::string& language,
                                                       const std::string& uri,
                                                       int line, int character) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return std::nullopt;
    return it->second->hover(uri, {line, character});
}

std::optional<lsp::WorkspaceEdit> LspToolManager::rename(const std::string& language,
                                                          const std::string& uri,
                                                          int line, int character,
                                                          const std::string& new_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(language);
    if (it == servers_.end()) return std::nullopt;
    return it->second->rename(uri, {line, character}, new_name);
}

}  // namespace claude
