#include "permissions/permission_manager.h"
#include "util/string_utils.h"
#include <algorithm>
#include <filesystem>

namespace claude {

namespace fs = std::filesystem;

// ============================================================
// PermissionManager
// ============================================================

void PermissionManager::add_rule(const PermissionRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.push_back(rule);
}

void PermissionManager::remove_rule(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
                       [&](const PermissionRule& r) { return r.pattern == pattern; }),
        rules_.end());
}

PermissionDecision PermissionManager::check(const std::string& /*action*/, const std::string& target) const {
    PermissionDecision decision;
    decision.level = default_level_;

    if (mode_ == "bypass" || mode_ == "yolo") {
        decision.level = PermissionLevel::Allow;
        decision.reason = "Bypass mode enabled";
        return decision;
    }

    // Check rules in order (first match wins)
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& rule : rules_) {
        if (match_pattern(rule.pattern, target)) {
            decision.level = rule.level;
            decision.matched_rule = rule.pattern;
            decision.reason = rule.description;
            return decision;
        }
    }

    return decision;
}

void PermissionManager::set_mode(const std::string& mode) {
    mode_ = mode;
    if (mode == "bypass" || mode == "yolo") {
        default_level_ = PermissionLevel::Allow;
    }
}

void PermissionManager::load_defaults() {
    // Default rules for file operations
    add_rule({"**/.git/**", PermissionLevel::Deny, "Git internal files"});
    add_rule({"**/node_modules/**", PermissionLevel::Ask, "Dependencies"});
    add_rule({"/etc/**", PermissionLevel::Deny, "System configuration"});
    add_rule({"/usr/**", PermissionLevel::Deny, "System files"});
    add_rule({"/boot/**", PermissionLevel::Deny, "Boot files"});
    add_rule({"/dev/**", PermissionLevel::Deny, "Device files"});
}

bool PermissionManager::match_pattern(const std::string& pattern, const std::string& text) {
    // Match anything
    if (pattern == "*" || pattern == "**") return true;

    // Recursive glob matching
    auto match_impl = [](const std::string& p, const std::string& t, size_t pi, size_t ti, auto& self) -> bool {
        while (pi < p.size() && ti < t.size()) {
            if (pi + 1 < p.size() && p[pi] == '*' && p[pi + 1] == '*') {
                // ** — match zero or more path segments
                // Skip past ** and any following /
                pi += 2;
                while (pi < p.size() && p[pi] == '/') pi++;
                // Try matching at every position (including current)
                for (size_t pos = ti; pos <= t.size(); ++pos) {
                    if (self(p, t, pi, pos, self)) return true;
                }
                return false;
            } else if (p[pi] == '*') {
                // * — match any characters within a segment (not /)
                pi++;
                while (ti < t.size() && t[ti] != '/') {
                    if (self(p, t, pi, ti, self)) return true;
                    ti++;
                }
                // Also try matching * as empty
                return self(p, t, pi, ti, self);
            } else if (p[pi] == t[ti] || (p[pi] == '?' && t[ti] != '/')) {
                pi++;
                ti++;
            } else {
                return false;
            }
        }
        // Skip trailing /** (matches empty)
        while (pi + 1 < p.size() && p[pi] == '/' && p[pi + 1] == '*' && (pi + 2 < p.size() && p[pi + 2] == '*')) {
            pi += 3;
        }
        return pi >= p.size() && ti >= t.size();
    };

    return match_impl(pattern, text, 0, 0, match_impl);
}

std::vector<PermissionRule> PermissionManager::rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

// ============================================================
// Sandbox
// ============================================================

}  // namespace claude
