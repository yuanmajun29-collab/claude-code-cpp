#include "memory/memory_store.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace claude {

using json = nlohmann::json;

// ============================================================
// MemoryEntry serialization
// ============================================================

std::string MemoryEntry::to_json() const {
    json j;
    j["id"] = id;
    j["content"] = content;
    j["type"] = memory_type_to_string(type);
    j["tags"] = tags;
    j["access_count"] = access_count;
    j["relevance_score"] = relevance_score;

    auto to_str = [](const auto& tp) -> std::string {
        auto time_t_val = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf;
        localtime_r(&time_t_val, &tm_buf);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        return buf;
    };

    j["created"] = to_str(created);
    j["last_accessed"] = to_str(last_accessed);
    return j.dump();
}

MemoryEntry MemoryEntry::from_json(const std::string& json_str) {
    auto j = json::parse(json_str);
    MemoryEntry entry;
    entry.id = j.value("id", "");
    entry.content = j.value("content", "");
    entry.type = string_to_memory_type(j.value("type", "fact"));
    entry.tags = j.value("tags", "");
    entry.access_count = j.value("access_count", 0);
    entry.relevance_score = j.value("relevance_score", 0.0);

    auto from_str = [](const std::string& s) -> std::chrono::system_clock::time_point {
        std::tm tm_buf{};
        std::istringstream ss(s);
        ss >> std::get_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm_buf));
    };

    if (j.contains("created")) entry.created = from_str(j["created"]);
    if (j.contains("last_accessed")) entry.last_accessed = from_str(j["last_accessed"]);
    return entry;
}

// ============================================================
// MemoryStore
// ============================================================

MemoryStore::MemoryStore(const std::string& storage_path) {
    init(storage_path);
}

void MemoryStore::init(const std::string& storage_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    storage_path_ = storage_path;
    if (!storage_path_.empty()) {
        load();
    }
}

void MemoryStore::store(const MemoryEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryEntry e = entry;
    if (e.id.empty()) {
        e.id = util::generate_uuid();
    }
    if (e.created == std::chrono::system_clock::time_point{}) {
        e.created = std::chrono::system_clock::now();
    }
    e.last_accessed = std::chrono::system_clock::now();
    entries_[e.id] = e;
    dirty_ = true;
}

void MemoryStore::store(const std::string& content, MemoryType type, const std::string& tags) {
    MemoryEntry entry;
    entry.content = content;
    entry.type = type;
    entry.tags = tags;
    store(entry);
}

std::vector<MemoryEntry> MemoryStore::recall(MemoryType type, int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;
    for (const auto& [id, entry] : entries_) {
        if (entry.type == type) {
            result.push_back(entry);
        }
    }

    // Sort by access count (most used first)
    std::sort(result.begin(), result.end(),
              [](const MemoryEntry& a, const MemoryEntry& b) {
                  return a.access_count > b.access_count;
              });

    if (static_cast<int>(result.size()) > limit) {
        result.resize(limit);
    }
    return result;
}

std::vector<MemoryEntry> MemoryStore::search(const std::string& query, int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto& [id, entry] : entries_) {
        std::string lower_content = entry.content;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);

        if (lower_content.find(lower_query) != std::string::npos) {
            result.push_back(entry);
        }
    }

    if (static_cast<int>(result.size()) > limit) {
        result.resize(limit);
    }
    return result;
}

std::optional<MemoryEntry> MemoryStore::get(const std::string& id) const {
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
        dirty_ = true;
    }
}

std::vector<MemoryEntry> MemoryStore::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryEntry> result;
    for (const auto& [id, entry] : entries_) {
        result.push_back(entry);
    }
    return result;
}

size_t MemoryStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void MemoryStore::save() const {
    if (storage_path_.empty() || !dirty_) return;

    json arr = json::array();
    for (const auto& [id, entry] : entries_) {
        arr.push_back(json::parse(entry.to_json()));
    }

    // Atomic write: write to temp then rename
    std::string tmp_path = storage_path_ + ".tmp";
    std::ofstream f(tmp_path);
    if (!f.is_open()) return;
    f << arr.dump(2);
    f.close();

    std::rename(tmp_path.c_str(), storage_path_.c_str());
}

void MemoryStore::load() {
    if (storage_path_.empty()) return;

    std::ifstream f(storage_path_);
    if (!f.is_open()) return;

    try {
        json arr = json::parse(f);
        if (!arr.is_array()) return;

        for (const auto& item : arr) {
            MemoryEntry entry = MemoryEntry::from_json(item.dump());
            entries_[entry.id] = entry;
        }
    } catch (...) {
        // Corrupt file, start fresh
    }
}

MemoryStore::~MemoryStore() {
    save();
}

}  // namespace claude
