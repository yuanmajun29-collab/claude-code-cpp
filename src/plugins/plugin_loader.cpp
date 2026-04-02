#include "plugins/plugin_loader.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <fstream>

namespace claude {

using json = nlohmann::json;

// Plugin API function types for .so plugins
using PluginInitFn = int (*)(const char* config);
using PluginGetNameFn = const char* (*)();
using PluginGetVersionFn = const char* (*)();
using PluginGetDescriptionFn = const char* (*)();
using PluginCleanupFn = void (*)();

PluginLoader::PluginLoader() = default;

PluginLoader::~PluginLoader() {
    unload_all();
}

void PluginLoader::add_search_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    search_paths_.push_back(path);
}

std::vector<std::string> PluginLoader::search_paths() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return search_paths_;
}

std::vector<PluginDescriptor> PluginLoader::load_directory(const std::string& dir_path) {
    std::vector<PluginDescriptor> loaded;
    std::error_code ec;

    if (!std::filesystem::exists(dir_path, ec) || ec) {
        spdlog::warn("PluginLoader: directory '{}' does not exist", dir_path);
        return loaded;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (ec) break;

        std::string path = entry.path().string();
        std::string ext = entry.path().extension().string();

        try {
            if (ext == ".json") {
                if (load_json_manifest(path)) {
                    auto desc = plugins_[entry.path().stem().string()].descriptor;
                    loaded.push_back(desc);
                }
            } else if (ext == ".so") {
                if (load_shared_library(path)) {
                    auto desc = plugins_[entry.path().stem().string()].descriptor;
                    loaded.push_back(desc);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("PluginLoader: Failed to load '{}': {}", path, e.what());
        }
    }

    spdlog::info("PluginLoader: Loaded {} plugins from '{}'", loaded.size(), dir_path);
    return loaded;
}

bool PluginLoader::load_plugin(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();

    if (ext == ".json") {
        return load_json_manifest(path);
    } else if (ext == ".so") {
        return load_shared_library(path);
    }

    spdlog::error("PluginLoader: Unknown plugin type for '{}'", path);
    return false;
}

bool PluginLoader::load_json_manifest(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::error("PluginLoader: Cannot open '{}'", path);
        return false;
    }

    try {
        json manifest = json::parse(f);
        if (!validate_json_manifest(manifest)) return false;

        std::string name = manifest.value("name", "");
        std::string version = manifest.value("version", "0.0.0");
        std::string description = manifest.value("description", "");

        if (name.empty()) {
            spdlog::error("PluginLoader: Plugin in '{}' has no name", path);
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        LoadedPlugin plugin;
        plugin.descriptor.name = name;
        plugin.descriptor.version = version;
        plugin.descriptor.description = description;
        plugin.descriptor.type = PluginType::Json;
        plugin.descriptor.path = path;
        plugin.descriptor.loaded = true;
        plugin.handle = nullptr;
        plugin.plugin_api = nullptr;

        plugins_[name] = std::move(plugin);

        spdlog::info("PluginLoader: Loaded JSON plugin '{}' v{} from '{}'",
                     name, version, path);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("PluginLoader: Failed to parse '{}': {}", path, e.what());
        return false;
    }
}

bool PluginLoader::load_shared_library(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        spdlog::error("PluginLoader: dlopen '{}' failed: {}", path, dlerror());
        return false;
    }

    // Clear existing errors
    dlerror();

    // Try to get plugin metadata functions
    auto get_name = reinterpret_cast<PluginGetNameFn>(dlsym(handle, "claude_plugin_name"));
    auto get_version = reinterpret_cast<PluginGetVersionFn>(dlsym(handle, "claude_plugin_version"));
    auto get_desc = reinterpret_cast<PluginGetDescriptionFn>(dlsym(handle, "claude_plugin_description"));
    auto init_fn = reinterpret_cast<PluginInitFn>(dlsym(handle, "claude_plugin_init"));

    const char* error = dlerror();
    if (error && !get_name) {
        spdlog::error("PluginLoader: '{}' is not a valid Claude plugin: {}", path, error);
        dlclose(handle);
        return false;
    }

    std::string name = get_name ? get_name() : std::filesystem::path(path).stem().string();
    std::string version = get_version ? get_version() : "0.0.0";
    std::string description = get_desc ? get_desc() : "";

    // Initialize plugin
    if (init_fn) {
        int ret = init_fn("{}");  // Pass empty config for now
        if (ret != 0) {
            spdlog::error("PluginLoader: Plugin '{}' init failed (code={})", name, ret);
            dlclose(handle);
            return false;
        }
    }

    LoadedPlugin plugin;
    plugin.descriptor.name = name;
    plugin.descriptor.version = version;
    plugin.descriptor.description = description;
    plugin.descriptor.type = PluginType::SharedLib;
    plugin.descriptor.path = path;
    plugin.descriptor.loaded = true;
    plugin.handle = handle;
    plugin.plugin_api = nullptr;

    plugins_[name] = std::move(plugin);

    spdlog::info("PluginLoader: Loaded .so plugin '{}' v{} from '{}'",
                 name, version, path);
    return true;
}

bool PluginLoader::unload_plugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = plugins_.find(name);
    if (it == plugins_.end()) return false;

    if (it->second.handle) {
        // Call cleanup if available
        auto cleanup = reinterpret_cast<PluginCleanupFn>(dlsym(it->second.handle, "claude_plugin_cleanup"));
        if (cleanup) cleanup();

        dlclose(it->second.handle);
    }

    plugins_.erase(it);
    spdlog::info("PluginLoader: Unloaded plugin '{}'", name);
    return true;
}

void PluginLoader::unload_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, plugin] : plugins_) {
        if (plugin.handle) {
            auto cleanup = reinterpret_cast<PluginCleanupFn>(dlsym(plugin.handle, "claude_plugin_cleanup"));
            if (cleanup) cleanup();
            dlclose(plugin.handle);
        }
    }
    plugins_.clear();
}

std::vector<PluginDescriptor> PluginLoader::loaded_plugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginDescriptor> result;
    for (const auto& [name, plugin] : plugins_) {
        result.push_back(plugin.descriptor);
    }
    return result;
}

bool PluginLoader::is_loaded(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.find(name) != plugins_.end();
}

bool PluginLoader::validate_json_manifest(const json& manifest) const {
    if (!manifest.is_object()) return false;
    if (!manifest.contains("name") || !manifest["name"].is_string()) return false;
    return true;
}

}  // namespace claude
