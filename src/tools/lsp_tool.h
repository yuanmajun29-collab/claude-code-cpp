#pragma once

#include "tools/tool_base.h"
#include "protocol/lsp/lsp_client.h"
#include <memory>
#include <mutex>
#include <map>
#include <optional>

namespace claude {

class ToolRegistry;

// LSP Tool — provides language server protocol features as tools
// Includes: go-to-definition, find-references, rename, diagnostics, hover
class LspToolManager {
public:
    LspToolManager();
    ~LspToolManager() = default;

    // Register LSP tools into a ToolRegistry
    void register_tools(ToolRegistry& registry);

    // Start an LSP server for a language
    bool start_server(const std::string& language, const std::string& root_uri,
                      const std::vector<std::string>& command);

    // Stop an LSP server
    void stop_server(const std::string& language);

    // Check if server is running
    bool is_running(const std::string& language) const;

    // Open a document in the LSP server
    bool open_document(const std::string& language, const std::string& uri,
                       const std::string& content, int version = 0);

    // Update document content (incremental sync)
    bool update_document(const std::string& language, const std::string& uri,
                         const std::string& content, int version);

    // Close document
    bool close_document(const std::string& language, const std::string& uri);

    // Get diagnostics for a document
    std::vector<std::string> get_diagnostics(const std::string& language,
                                              const std::string& uri);

    // Go to definition
    std::vector<lsp::Location> go_to_definition(const std::string& language,
                                                  const std::string& uri,
                                                  int line, int character);

    // Find references
    std::vector<lsp::Location> find_references(const std::string& language,
                                                const std::string& uri,
                                                int line, int character);

    // Hover
    std::optional<lsp::HoverResult> hover(const std::string& language,
                                           const std::string& uri,
                                           int line, int character);

    // Rename symbol
    std::optional<lsp::WorkspaceEdit> rename(const std::string& language,
                                              const std::string& uri,
                                              int line, int character,
                                              const std::string& new_name);

private:
    std::map<std::string, std::unique_ptr<lsp::LspClient>> servers_;
    mutable std::mutex mutex_;
};

}  // namespace claude
