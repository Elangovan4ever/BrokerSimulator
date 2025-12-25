#pragma once

#include <drogon/WebSocketController.h>
#include <mutex>
#include <set>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"

namespace broker_sim {

/**
 * Simple WebSocket controller for broadcasting session status updates.
 *
 * Clients connect to /ws/status and receive automatic updates about all sessions:
 * - Current simulation time
 * - Session status (running/paused/stopped)
 * - Events processed count
 * - Speed factor
 *
 * No authentication required, no subscription needed - all connected clients
 * receive all session updates automatically.
 */
class StatusWsController : public drogon::WebSocketController<StatusWsController> {
public:
    static const bool isAutoCreation = false;

    /**
     * Initialize with session manager reference.
     */
    static void init(std::shared_ptr<SessionManager> session_mgr);

    /**
     * Broadcast session status update to all connected clients.
     * Called by SessionManager during event processing.
     */
    static void broadcast_session_status(const std::string& session_id,
                                          const std::string& status,
                                          int64_t current_time_ns,
                                          uint64_t events_processed,
                                          double speed_factor);

    /**
     * Broadcast that a session has been created/deleted/status changed.
     */
    static void broadcast_session_event(const std::string& event_type,
                                         const std::string& session_id);

    // Drogon WebSocket interface
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                             const drogon::WebSocketConnectionPtr& conn) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/status");
    WS_PATH_LIST_END

private:
    static std::shared_ptr<SessionManager> session_mgr_;
    static std::mutex conn_mutex_;
    static std::set<drogon::WebSocketConnectionPtr> connections_;

    // Send current state of all sessions to a newly connected client
    static void send_initial_state(const drogon::WebSocketConnectionPtr& conn);
};

} // namespace broker_sim
