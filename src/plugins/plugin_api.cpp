#include "plugins/plugin_api.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <fstream>
#include <algorithm>

namespace claude {

using json = nlohmann::json;

// ============================================================
// PluginManager Implementation
// ============================================================

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    // Unload all plugins
    for (auto& [name, handle] : handles_) {
        if (handle) {
            // Try to call cleanup
            using CleanupFn = void (*)();
            auto cleanup = reinterpret_cast<CleanupFn>(dlsym(handle, "claude_plugin_cleanup"));
            if (cleanup) cleanup();
            dlclose(handle);
        }
    }
}

bool PluginManager::load(const std::string& path) {
    // Check if it's a JSON manifest
    if (path.ends_with(".json")) {
        return load_json_manifest(path);
    }

    // Check if it's a shared library
    if (path.ends_with(".so")) {
        return load_shared_library(path);
    }

    return false;
}

bool PluginManager::load_json_manifest(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json manifest = json::parse(f);
        PluginInfo info;
        info.name = manifest.value("name", "");
        if (info.name.empty()) return false;

        info.version = manifest.value("version", "0.0.0");
        info.description = manifest.value("description", "");
        info.author = manifest.value("author", "");

        if (manifest.contains("provides_tools")) {
            for (const auto& t : manifest["provides_tools"]) {
                info.provides_tools.push_back(t.get<std::string>());
            }
        }
        if (manifest.contains("provides_commands")) {
            for (const auto& c : manifest["provides_commands"]) {
                info.provides_commands.push_back(c.get<std::string>());
            }
        }

        plugins_[info.name] = info;
        spdlog::info("Plugin loaded (manifest): {} v{}", info.name, info.version);
        return true;
    } catch (...) {
        return false;
    }
}

bool PluginManager::load_shared_library(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        spdlog::error("Failed to load plugin {}: {}", path, dlerror());
        return false;
    }

    // Get plugin info
    using NameFn = const char* (*)();
    using VersionFn = const char* (*)();
    using DescFn = const char* (*)();
    using InitFn = void (*)();

    auto name_fn = reinterpret_cast<NameFn>(dlsym(handle, "claude_plugin_name"));
    auto version_fn = reinterpret_cast<VersionFn>(dlsym(handle, "claude_plugin_version"));
    auto desc_fn = reinterpret_cast<DescFn>(dlsym(handle, "claude_plugin_description"));
    auto init_fn = reinterpret_cast<InitFn>(dlsym(handle, "claude_plugin_init"));

    if (!name_fn) {
        spdlog::error("Plugin {} missing claude_plugin_name symbol", path);
        dlclose(handle);
        return false;
    }

    PluginInfo info;
    info.name = name_fn();
    info.version = version_fn ? version_fn() : "0.0.0";
    info.description = desc_fn ? desc_fn() : "";

    // Call init if available
    if (init_fn) init_fn();

    plugins_[info.name] = info;
    handles_[info.name] = handle;
    spdlog::info("Plugin loaded (.so): {} v{}", info.name, info.version);
    return true;
}

void PluginManager::unload(const std::string& name) {
    auto it = handles_.find(name);
    if (it != handles_.end()) {
        using CleanupFn = void (*)();
        auto cleanup = reinterpret_cast<CleanupFn>(dlsym(it->second, "claude_plugin_cleanup"));
        if (cleanup) cleanup();
        dlclose(it->second);
        handles_.erase(it);
    }
    plugins_.erase(name);
    spdlog::info("Plugin unloaded: {}", name);
}

std::vector<PluginInfo> PluginManager::plugins() {
    std::vector<PluginInfo> result;
    for (const auto& [name, info] : plugins_) {
        result.push_back(info);
    }
    return result;
}

void PluginManager::scan_directory(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) return;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto path = entry.path().string();
            if (path.ends_with(".json") || path.ends_with(".so")) {
                load(path);
            }
        }
    }
}

std::optional<PluginInfo> PluginManager::get_plugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it != plugins_.end()) return it->second;
    return std::nullopt;
}

}  // namespace claude
