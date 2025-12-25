#include "status_ws_controller.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

namespace broker_sim {

// Static member initialization
std::shared_ptr<SessionManager> StatusWsController::session_mgr_;
std::mutex StatusWsController::conn_mutex_;
std::set<drogon::WebSocketConnectionPtr> StatusWsController::connections_;

void StatusWsController::init(std::shared_ptr<SessionManager> session_mgr) {
    session_mgr_ = std::move(session_mgr);
    spdlog::info("StatusWsController initialized");
}

void StatusWsController::handleNewConnection(const drogon::HttpRequestPtr& req,
                                              const drogon::WebSocketConnectionPtr& conn) {
    try {
        spdlog::info("Status WebSocket client connected from {}", req->getPeerAddr().toIp());

        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            connections_.insert(conn);
        }

        // Send current state of all sessions to the new client
        send_initial_state(conn);

        // Send welcome message
        nlohmann::json welcome;
        welcome["type"] = "connected";
        welcome["message"] = "Connected to session status stream";
        conn->send(welcome.dump());
    } catch (const std::exception& e) {
        spdlog::error("StatusWsController handleNewConnection error: {}", e.what());
    }
}

void StatusWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    connections_.erase(conn);
    spdlog::debug("Status WebSocket client disconnected, {} clients remaining", connections_.size());
}

void StatusWsController::handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                                           std::string&& message,
                                           const drogon::WebSocketMessageType& type) {
    // Handle ping/pong
    if (type == drogon::WebSocketMessageType::Ping) {
        conn->send(message, drogon::WebSocketMessageType::Pong);
        return;
    }

    // Ignore empty messages (heartbeats, keep-alives)
    if (message.empty()) {
        return;
    }

    // Parse message
    try {
        auto msg = nlohmann::json::parse(message);
        std::string action = msg.value("action", "");

        if (action == "ping") {
            nlohmann::json pong;
            pong["type"] = "pong";
            pong["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            conn->send(pong.dump());
        } else if (action == "get_all") {
            // Re-send current state of all sessions
            send_initial_state(conn);
        }
    } catch (const std::exception& e) {
        spdlog::warn("StatusWs: Failed to parse message: {}", e.what());
    }
}

void StatusWsController::send_initial_state(const drogon::WebSocketConnectionPtr& conn) {
    if (!session_mgr_) return;

    auto sessions = session_mgr_->list_sessions();

    nlohmann::json state;
    state["type"] = "initial_state";
    state["sessions"] = nlohmann::json::array();

    for (const auto& session : sessions) {
        nlohmann::json s;
        s["session_id"] = session->id;
        s["status"] = static_cast<int>(session->status);
        s["status_name"] = [&]() {
            switch (session->status) {
                case SessionStatus::CREATED: return "CREATED";
                case SessionStatus::RUNNING: return "RUNNING";
                case SessionStatus::PAUSED: return "PAUSED";
                case SessionStatus::STOPPED: return "STOPPED";
                case SessionStatus::COMPLETED: return "COMPLETED";
                case SessionStatus::ERROR: return "ERROR";
                default: return "UNKNOWN";
            }
        }();
        s["current_time_ns"] = session->time_engine->current_time().time_since_epoch().count();
        s["events_processed"] = session->events_processed.load();
        s["speed_factor"] = session->config.speed_factor;
        s["symbols"] = session->config.symbols;

        state["sessions"].push_back(s);
    }

    conn->send(state.dump());
}

void StatusWsController::broadcast_session_status(const std::string& session_id,
                                                   const std::string& status,
                                                   int64_t current_time_ns,
                                                   uint64_t events_processed,
                                                   double speed_factor) {
    nlohmann::json msg;
    msg["type"] = "session_status";
    msg["session_id"] = session_id;
    msg["status"] = status;
    msg["current_time_ns"] = current_time_ns;
    msg["events_processed"] = events_processed;
    msg["speed_factor"] = speed_factor;

    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (const auto& conn : connections_) {
        if (conn->connected()) {
            conn->send(payload);
        }
    }
}

void StatusWsController::broadcast_session_event(const std::string& event_type,
                                                  const std::string& session_id) {
    nlohmann::json msg;
    msg["type"] = event_type;  // "session_created", "session_deleted", etc.
    msg["session_id"] = session_id;
    msg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (const auto& conn : connections_) {
        if (conn->connected()) {
            conn->send(payload);
        }
    }
}

} // namespace broker_sim
