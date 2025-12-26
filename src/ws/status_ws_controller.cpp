#include "status_ws_controller.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace broker_sim {

// Static member initialization
std::shared_ptr<SessionManager> StatusWsController::session_mgr_;
std::mutex StatusWsController::conn_mutex_;
std::set<drogon::WebSocketConnectionPtr> StatusWsController::connections_;
std::atomic<bool> StatusWsController::worker_running_{false};
std::unique_ptr<std::thread> StatusWsController::worker_;

void StatusWsController::init(std::shared_ptr<SessionManager> session_mgr) {
    session_mgr_ = std::move(session_mgr);
    start_worker();
    spdlog::info("StatusWsController initialized");
}

void StatusWsController::shutdown() {
    stop_worker();
}

namespace {
const char* status_to_string(SessionStatus status) {
    switch (status) {
        case SessionStatus::CREATED: return "CREATED";
        case SessionStatus::RUNNING: return "RUNNING";
        case SessionStatus::PAUSED: return "PAUSED";
        case SessionStatus::STOPPED: return "STOPPED";
        case SessionStatus::COMPLETED: return "COMPLETED";
        case SessionStatus::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}
} // namespace

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
        s["status_name"] = status_to_string(session->status);
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

    std::vector<drogon::WebSocketConnectionPtr> conns;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conns.assign(connections_.begin(), connections_.end());
    }

    std::vector<drogon::WebSocketConnectionPtr> stale;
    for (const auto& conn : conns) {
        if (!conn || !conn->connected()) {
            stale.push_back(conn);
            continue;
        }
        try {
            conn->send(payload);
        } catch (const std::exception& e) {
            spdlog::warn("StatusWs: send failed, removing connection: {}", e.what());
            stale.push_back(conn);
        }
    }

    if (!stale.empty()) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (const auto& conn : stale) {
            connections_.erase(conn);
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

    std::vector<drogon::WebSocketConnectionPtr> conns;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conns.assign(connections_.begin(), connections_.end());
    }

    std::vector<drogon::WebSocketConnectionPtr> stale;
    for (const auto& conn : conns) {
        if (!conn || !conn->connected()) {
            stale.push_back(conn);
            continue;
        }
        try {
            conn->send(payload);
        } catch (const std::exception& e) {
            spdlog::warn("StatusWs: send failed, removing connection: {}", e.what());
            stale.push_back(conn);
        }
    }

    if (!stale.empty()) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (const auto& conn : stale) {
            connections_.erase(conn);
        }
    }
}

void StatusWsController::start_worker() {
    if (worker_running_.exchange(true)) {
        return;
    }
    worker_ = std::make_unique<std::thread>(worker_loop);
}

void StatusWsController::stop_worker() {
    if (!worker_running_.exchange(false)) {
        return;
    }
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
}

void StatusWsController::worker_loop() {
    using namespace std::chrono;
    struct TickState {
        int64_t last_sent_ns{0};
        double speed{0.0};
        SessionStatus status{SessionStatus::CREATED};
        bool initialized{false};
    };

    std::unordered_map<std::string, TickState> tick_states;

    while (worker_running_.load(std::memory_order_acquire)) {
        auto start = steady_clock::now();

        if (session_mgr_) {
            auto sessions = session_mgr_->list_sessions();
            for (const auto& session : sessions) {
                int64_t current_ns = duration_cast<nanoseconds>(
                    session->time_engine->current_time().time_since_epoch()).count();
                double speed = session->config.speed_factor;
                auto& tick = tick_states[session->id];

                if (!tick.initialized) {
                    tick.last_sent_ns = current_ns;
                    tick.status = session->status;
                    tick.speed = speed;
                    tick.initialized = true;
                } else if (tick.status != session->status) {
                    if (current_ns > tick.last_sent_ns) {
                        tick.last_sent_ns = current_ns;
                    }
                    tick.status = session->status;
                    tick.speed = speed;
                } else if (tick.speed != speed) {
                    if (current_ns > tick.last_sent_ns) {
                        tick.last_sent_ns = current_ns;
                    }
                    tick.speed = speed;
                }

                if (session->status == SessionStatus::RUNNING && speed > 0.0) {
                    int64_t step_ns = static_cast<int64_t>(std::llround(speed * 1e9));
                    if (step_ns > 0) {
                        int64_t next_ns = tick.last_sent_ns + step_ns;
                        if (current_ns > next_ns) {
                            next_ns = current_ns;
                        }
                        tick.last_sent_ns = next_ns;
                    } else if (current_ns > tick.last_sent_ns) {
                        tick.last_sent_ns = current_ns;
                    }
                } else if (current_ns > tick.last_sent_ns) {
                    tick.last_sent_ns = current_ns;
                }

                int64_t send_ns = tick.last_sent_ns;
                broadcast_session_status(
                    session->id,
                    status_to_string(session->status),
                    send_ns,
                    session->events_processed.load(std::memory_order_relaxed),
                    speed);
            }
        }

        auto elapsed = steady_clock::now() - start;
        auto sleep_for = seconds(1) - duration_cast<nanoseconds>(elapsed);
        if (sleep_for.count() > 0) {
            std::this_thread::sleep_for(sleep_for);
        }
    }
}

} // namespace broker_sim
