#pragma once

#include "memory/memory_store.h"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

namespace claude {

// Plugin interface
struct PluginInfo {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::vector<std::string> provides_tools;
    std::vector<std::string> provides_commands;
};

// Plugin loader — loads shared libraries or script plugins
class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    // Load plugin from path
    bool load(const std::string& path);

    // Unload plugin by name
    void unload(const std::string& name);

    // List loaded plugins
    std::vector<PluginInfo> plugins();

    // Scan directory for plugins
    void scan_directory(const std::string& dir);

    // Get plugin info
    std::optional<PluginInfo> get_plugin(const std::string& name);

private:
    bool load_json_manifest(const std::string& path);
    bool load_shared_library(const std::string& path);

    std::map<std::string, PluginInfo> plugins_;
    std::map<std::string, void*> handles_;  // dlopen handles
};

}  // namespace claude
