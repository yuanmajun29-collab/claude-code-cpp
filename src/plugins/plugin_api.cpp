#include "plugins/plugin_api.h"
#include "util/string_utils.h"
#include "util/file_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>
#include <dlfcn.h>

namespace claude {

namespace fs = std::filesystem;
using json = nlohmann::json;

// ============================================================
// MemoryEntry
// ============================================================

std::string MemoryEntry::to_json() const {
    json j;
    j["id"] = id;
    j["content"] = content;
    j["type"] = static_cast<int>(type);
    j["tags"] = tags;
    j["access_count"] = access_count;
    j["relevance_score"] = relevance_score;
    return j.dump();
}

MemoryEntry MemoryEntry::from_json(const std::string& json_str) {
    MemoryEntry e;
    try {
        auto j = json::parse(json_str);
        e.id = j.value("id", "");
        e.content = j.value("content", "");
        e.type = static_cast<MemoryType>(j.value("type", 0));
        e.tags = j.value("tags", "");
        e.access_count = j.value("access_count", 0);
        e.relevance_score = j.value("relevance_score", 0.0);
    } catch (...) {}
    return e;
}

// ============================================================
// MemoryStore
// ============================================================

MemoryStore::MemoryStore(const std::string& storage_path) {
    init(storage_path);
}

void MemoryStore::init(const std::string& storage_path) {
    storage_path_ = storage_path;
    if (fs::exists(storage_path_)) {
        load();
    }
}

void MemoryStore::store(const MemoryEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = entry.id.empty() ? util::generate_uuid() : entry.id;
    MemoryEntry e = entry;
    e.id = id;
    e.created = std::chrono::system_clock::now();
    e.last_accessed = e.created;
    entries_[id] = e;
    dirty_ = true;
}

std::vector<MemoryEntry> MemoryStore::recall(MemoryType type, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;
    for (const auto& [_, entry] : entries_) {
        if (entry.type == type) result.push_back(entry);
    }
    std::sort(result.begin(), result.end(),
              [](const MemoryEntry& a, const MemoryEntry& b) {
                  return a.access_count > b.access_count;
              });
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

std::vector<MemoryEntry> MemoryStore::search(const std::string& query, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;
    std::string lower_query = util::to_lower(query);

    for (const auto& [_, entry] : entries_) {
        std::string lower_content = util::to_lower(entry.content);
        std::string lower_tags = util::to_lower(entry.tags);
        if (lower_content.find(lower_query) != std::string::npos ||
            lower_tags.find(lower_query) != std::string::npos) {
            result.push_back(entry);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const MemoryEntry& a, const MemoryEntry& b) {
                  return a.access_count > b.access_count;
              });

    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

std::optional<MemoryEntry> MemoryStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    if (it != entries_.end()) return it->second;
    return std::nullopt;
}

void MemoryStore::erase(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(id);
    dirty_ = true;
}

void MemoryStore::touch(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    if (it != entries_.end()) {
        it->second.last_accessed = std::chrono::system_clock::now();
        it->second.access_count++;
    }
}

std::vector<MemoryEntry> MemoryStore::all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;
    for (const auto& [_, entry] : entries_) result.push_back(entry);
    return result;
}

size_t MemoryStore::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void MemoryStore::save() const {
    if (!dirty_ || storage_path_.empty()) return;

    json j = json::array();
    for (const auto& [_, entry] : entries_) {
        j.push_back(json::parse(entry.to_json()));
    }

    std::error_code ec;
    fs::create_directories(fs::path(storage_path_).parent_path(), ec);

    std::ofstream f(storage_path_);
    if (f.is_open()) {
        f << j.dump(2);
        dirty_ = false;
    }
}

void MemoryStore::load() {
    std::ifstream f(storage_path_);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;
        if (!j.is_array()) return;

        for (const auto& item : j) {
            MemoryEntry e;
            e.id = item.value("id", "");
            e.content = item.value("content", "");
            e.type = static_cast<MemoryType>(item.value("type", 0));
            e.tags = item.value("tags", "");
            e.access_count = item.value("access_count", 0);
            e.relevance_score = item.value("relevance_score", 0.0);
            if (!e.id.empty()) entries_[e.id] = e;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Memory load error: {}", e.what());
    }
}

MemoryStore::~MemoryStore() {
    save();
}

// ============================================================
// PluginManager
// ============================================================

PluginManager::~PluginManager() {
    for (auto& [name, handle] : handles_) {
        if (handle) dlclose(handle);
    }
}

bool PluginManager::load(const std::string& path) {
    // For P7, we support .json manifest files that describe tools/commands
    // Future: support .so shared libraries
    fs::path p(path);

    if (p.extension() == ".json") {
        try {
            std::ifstream f(path);
            if (!f.is_open()) return false;
            json manifest;
            f >> manifest;

            PluginInfo info;
            info.name = manifest.value("name", p.stem().string());
            info.version = manifest.value("version", "0.0.0");
            info.description = manifest.value("description", "");

            if (manifest.contains("tools") && manifest["tools"].is_array()) {
                for (const auto& t : manifest["tools"]) {
                    info.provides_tools.push_back(t.get<std::string>());
                }
            }
            if (manifest.contains("commands") && manifest["commands"].is_array()) {
                for (const auto& c : manifest["commands"]) {
                    info.provides_commands.push_back(c.get<std::string>());
                }
            }

            plugins_[info.name] = info;
            spdlog::info("Loaded plugin: {} v{}", info.name, info.version);
            return true;
        } catch (const std::exception& e) {
            spdlog::warn("Plugin load error: {}", e.what());
            return false;
        }
    }

    // Future: dlopen for .so files
    void* handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        spdlog::warn("dlopen failed for {}: {}", path, dlerror());
        return false;
    }
    handles_[path] = handle;
    return true;
}

void PluginManager::unload(const std::string& name) {
    plugins_.erase(name);
}

std::vector<PluginInfo> PluginManager::plugins() {
    std::vector<PluginInfo> result;
    for (const auto& [_, info] : plugins_) result.push_back(info);
    return result;
}

void PluginManager::scan_directory(const std::string& dir) {
    if (!fs::is_directory(dir)) return;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".json" || ext == ".so") {
                load(entry.path().string());
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
