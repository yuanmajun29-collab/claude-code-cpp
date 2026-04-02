#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
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

// Convert MemoryType to string
inline std::string memory_type_to_string(MemoryType type) {
    switch (type) {
        case MemoryType::Fact: return "fact";
        case MemoryType::Preference: return "preference";
        case MemoryType::Context: return "context";
        case MemoryType::Lesson: return "lesson";
        case MemoryType::Decision: return "decision";
    }
    return "fact";
}

// Convert string to MemoryType
inline MemoryType string_to_memory_type(const std::string& s) {
    if (s == "preference") return MemoryType::Preference;
    if (s == "context") return MemoryType::Context;
    if (s == "lesson") return MemoryType::Lesson;
    if (s == "decision") return MemoryType::Decision;
    return MemoryType::Fact;
}

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

    // Serialize to JSON string
    std::string to_json() const;

    // Deserialize from JSON string
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

    // Store a memory with auto-generated ID
    void store(const std::string& content, MemoryType type = MemoryType::Fact,
               const std::string& tags = "");

    // Recall memories by type
    std::vector<MemoryEntry> recall(MemoryType type, int limit = 20) const;

    // Search memories by content (simple substring match)
    std::vector<MemoryEntry> search(const std::string& query, int limit = 10) const;

    // Get memory by ID
    std::optional<MemoryEntry> get(const std::string& id) const;

    // Delete memory
    void erase(const std::string& id);

    // Update access time
    void touch(const std::string& id);

    // Get all memories
    std::vector<MemoryEntry> all() const;

    // Count
    size_t size() const;

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

}  // namespace claude
