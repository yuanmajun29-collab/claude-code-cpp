#include "bridge/bridge_server.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>

namespace claude {

// ============================================================
// SessionManager Implementation
// ============================================================

std::string SessionManager::create_session(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    Session session;
    session.id = util::generate_uuid();
    session.created_at = util::get_iso_datetime();
    session.working_directory = ".";

    if (!client_id.empty()) {
        session.client_ids.push_back(client_id);
        client_to_session_[client_id] = session.id;
    }

    sessions_[session.id] = std::move(session);
    spdlog::info("Session created: {} (client: {})", sessions_.rbegin()->first, client_id);
    return sessions_.rbegin()->first;
}

std::string SessionManager::get_session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return "";

    std::ostringstream oss;
    const auto& s = it->second;
    oss << "Session: " << s.id << "\n"
        << "Created: " << s.created_at << "\n"
        << "Working dir: " << s.working_directory << "\n"
        << "Clients: " << s.client_ids.size() << "\n";
    return oss.str();
}

std::vector<std::string> SessionManager::list_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, _] : sessions_) {
        ids.push_back(id);
    }
    return ids;
}

void SessionManager::attach(const std::string& session_id, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        spdlog::warn("Cannot attach: session {} not found", session_id);
        return;
    }

    // Don't add duplicate
    auto& clients = it->second.client_ids;
    if (std::find(clients.begin(), clients.end(), client_id) == clients.end()) {
        clients.push_back(client_id);
    }
    client_to_session_[client_id] = session_id;
    spdlog::info("Client {} attached to session {}", client_id, session_id);
}

void SessionManager::detach(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = client_to_session_.find(client_id);
    if (it == client_to_session_.end()) return;

    auto session_it = sessions_.find(it->second);
    if (session_it != sessions_.end()) {
        auto& clients = session_it->second.client_ids;
        clients.erase(
            std::remove(clients.begin(), clients.end(), client_id),
            clients.end());

        // Destroy session if no more clients
        if (clients.empty()) {
            sessions_.erase(session_it);
            spdlog::info("Session {} destroyed (no clients)", it->second);
        }
    }

    client_to_session_.erase(it);
    spdlog::info("Client {} detached", client_id);
}

void SessionManager::destroy_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;

    // Clean up client mappings
    for (const auto& client_id : it->second.client_ids) {
        client_to_session_.erase(client_id);
    }

    sessions_.erase(it);
    spdlog::info("Session {} destroyed", session_id);
}

}  // namespace claude
