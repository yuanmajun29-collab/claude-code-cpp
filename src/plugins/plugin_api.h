#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

namespace claude {

// Memory entry types
enum class MemoryType {
    Fact,       // General facts and information
    Preference, // User preferences
    Context,    // Project context
    Lesson,     // Lessons learned
    Decision    // Architecture decisions
};

// Single memory entry
struct MemoryEntry {
    std::string id;
    std::string content;
    MemoryType type = MemoryType::Fact;
    std::string tags;        // Comma-separated tags
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point last_accessed;
    int access_count = 0;
    double relevance_score = 0.0;

    // Serialize to/from JSON
    std::string to_json() const;
    static MemoryEntry from_json(const std::string& json_str);
};

// Memory store — persistent key-value memory with search
class MemoryStore {
public:
    MemoryStore() = default;
    explicit MemoryStore(const std::string& storage_path);

    // Initialize storage
    void init(const std::string& storage_path);

    // Store a memory
    void store(const MemoryEntry& entry);

    // Recall memories by type
    std::vector<MemoryEntry> recall(MemoryType type, int limit = 20);

    // Search memories by content (simple substring match)
    std::vector<MemoryEntry> search(const std::string& query, int limit = 10);

    // Get memory by ID
    std::optional<MemoryEntry> get(const std::string& id);

    // Delete memory
    void erase(const std::string& id);

    // Update access time
    void touch(const std::string& id);

    // Get all memories
    std::vector<MemoryEntry> all();

    // Count
    size_t size();

    // Persist to disk
    void save() const;

    // Load from disk
    void load();

    // Auto-save on destroy
    ~MemoryStore();

private:
    std::string storage_path_;
    std::map<std::string, MemoryEntry> entries_;
    mutable std::mutex mutex_;
    mutable bool dirty_ = false;
};

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
    PluginManager() = default;
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
    std::map<std::string, PluginInfo> plugins_;
    std::map<std::string, void*> handles_;  // dlopen handles
};

}  // namespace claude
