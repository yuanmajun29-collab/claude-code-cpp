#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <filesystem>

namespace claude {

// Plugin source type
enum class PluginType {
    Json,      // JSON manifest (built-in tools/commands)
    SharedLib, // .so dynamic library
    Wasm       // WebAssembly (future)
};

// Loaded plugin descriptor
struct PluginDescriptor {
    std::string name;
    std::string version;
    std::string description;
    PluginType type;
    std::string path;
    bool loaded = false;
};

// Plugin loader — discovers and loads plugins (JSON manifests and .so shared libraries)
class PluginLoader {
public:
    PluginLoader();
    ~PluginLoader();

    // Load plugins from a directory
    std::vector<PluginDescriptor> load_directory(const std::string& dir_path);

    // Load a single plugin
    bool load_plugin(const std::string& path);

    // Unload a plugin
    bool unload_plugin(const std::string& name);

    // Unload all plugins
    void unload_all();

    // List loaded plugins
    std::vector<PluginDescriptor> loaded_plugins() const;

    // Check if a plugin is loaded
    bool is_loaded(const std::string& name) const;

    // Register a plugin from JSON manifest
    bool load_json_manifest(const std::string& path);

    // Register a plugin from shared library
    bool load_shared_library(const std::string& path);

    // Plugin search paths
    void add_search_path(const std::string& path);
    std::vector<std::string> search_paths() const;

private:
    struct LoadedPlugin {
        PluginDescriptor descriptor;
        void* handle = nullptr;
        void* plugin_api = nullptr;
    };

    bool validate_json_manifest(const nlohmann::json& manifest) const;

    std::map<std::string, LoadedPlugin> plugins_;
    std::vector<std::string> search_paths_;
    mutable std::mutex mutex_;
};

}  // namespace claude
