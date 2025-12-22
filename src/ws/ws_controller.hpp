#pragma once

#include <drogon/WebSocketController.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../core/utils.hpp"

namespace broker_sim {

/**
 * WebSocket API type for protocol-specific message formatting.
 */
enum class WsApiType {
    GENERIC,    // Generic/Control API
    ALPACA,     // Alpaca Trading API format
    POLYGON,    // Polygon.io format
    FINNHUB     // Finnhub format
};

/**
 * Subscription types for market data streams.
 */
enum class SubscriptionType {
    TRADES,
    QUOTES,
    BARS,
    ORDER_UPDATES,
    ALL
};

/**
 * Backpressure state for a connection.
 * Note: All access must be protected by conn_mutex_
 */
struct BackpressureState {
    size_t pending_bytes{0};         // Bytes waiting to be sent
    size_t pending_messages{0};      // Messages waiting to be sent
    bool is_slow{false};             // Connection is slow (buffer exceeds threshold)
    int64_t last_drain_ns{0};        // Last time buffer drained below threshold

    // Thresholds (configurable)
    static constexpr size_t HIGH_WATERMARK_BYTES = 1 * 1024 * 1024;   // 1MB
    static constexpr size_t LOW_WATERMARK_BYTES = 256 * 1024;         // 256KB
    static constexpr size_t HIGH_WATERMARK_MESSAGES = 10000;
    static constexpr size_t LOW_WATERMARK_MESSAGES = 5000;
};

/**
 * Per-connection state tracking.
 * Note: All access must be protected by conn_mutex_
 */
struct WsConnectionState {
    std::string session_id;
    WsApiType api_type = WsApiType::GENERIC;
    bool authenticated = false;

    // Subscriptions by type -> set of symbols ("*" means all)
    std::unordered_map<SubscriptionType, std::unordered_set<std::string>> subscriptions;

    // Backpressure tracking
    BackpressureState backpressure;

    // Statistics
    uint64_t messages_sent{0};
    uint64_t messages_dropped{0};
    uint64_t bytes_sent{0};

    // Check if subscribed to a symbol for a given type
    bool is_subscribed(SubscriptionType type, const std::string& symbol) const {
        auto it = subscriptions.find(type);
        if (it == subscriptions.end()) return false;
        return it->second.count("*") > 0 || it->second.count(symbol) > 0;
    }
};

/**
 * WebSocket controller supporting Alpaca, Polygon, and Finnhub protocols.
 *
 * Protocol Details:
 *
 * ALPACA (/alpaca/stream or /alpaca/ws):
 *   Auth: {"action":"auth","key":"...","secret":"..."}
 *   Subscribe: {"action":"subscribe","trades":["AAPL"],"quotes":["AAPL"],"bars":["AAPL"]}
 *   Trade msg: {"T":"t","S":"AAPL","p":150.50,"s":100,"t":"2024-01-15T10:30:00Z",...}
 *   Quote msg: {"T":"q","S":"AAPL","bp":150.45,"bs":100,"ap":150.55,"as":200,...}
 *
 * POLYGON (/polygon/ws):
 *   Auth: {"action":"auth","params":"API_KEY"}
 *   Subscribe: {"action":"subscribe","params":"T.AAPL,Q.AAPL,AM.AAPL"}
 *   Trade msg: {"ev":"T","sym":"AAPL","p":150.50,"s":100,"t":1705313400000,...}
 *   Quote msg: {"ev":"Q","sym":"AAPL","bp":150.45,"bs":100,"ap":150.55,"as":200,...}
 *
 * FINNHUB (/finnhub/ws):
 *   Auth: Query param ?token=... (validated on connection)
 *   Subscribe: {"type":"subscribe","symbol":"AAPL"}
 *   Trade msg: {"type":"trade","data":[{"s":"AAPL","p":150.50,"v":100,"t":1705313400000}]}
 */
class WsController : public drogon::WebSocketController<WsController> {
public:
    static const bool isAutoCreation = false;

    /**
     * Initialize the WebSocket controller with session manager.
     */
    static void init(std::shared_ptr<SessionManager> session_mgr, const Config& cfg);

    /**
     * Shutdown the WebSocket controller cleanly.
     */
    static void shutdown();

    // Drogon WebSocket interface
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                             const drogon::WebSocketConnectionPtr& conn) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    /**
     * Broadcast an event to all subscribed connections.
     */
    static void broadcast_event(const std::string& session_id, const Event& event);

    /**
     * Send a message to a specific session (all connections).
     */
    static void broadcast(const std::string& session_id, const std::string& msg);

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws");
    WS_PATH_ADD("/stream");
    WS_PATH_ADD("/alpaca/stream");
    WS_PATH_ADD("/alpaca/ws");
    WS_PATH_ADD("/polygon/ws");
    WS_PATH_ADD("/finnhub/ws");
    WS_PATH_LIST_END

    // Public API for enqueueing messages to session connections
    static void broadcast_to_session(const std::string& session_id, const std::string& msg);

private:
    static std::shared_ptr<SessionManager> session_mgr_;
    static Config cfg_;

    // Connection management
    static std::mutex conn_mutex_;
    static std::unordered_map<drogon::WebSocketConnectionPtr, WsConnectionState> conn_states_;

    // Session to connections mapping
    static std::unordered_map<std::string, std::vector<drogon::WebSocketConnectionPtr>> session_conns_;

    // Message queue for batched sending
    static std::mutex queue_mutex_;
    static std::condition_variable queue_cv_;
    static std::unordered_map<std::string, std::deque<std::string>> outbox_;

    // Background worker
    static std::atomic<bool> worker_running_;
    static std::unique_ptr<std::thread> worker_;

    // Determine API type from request path
    static WsApiType get_api_type(const std::string& path);

    // Protocol-specific message handlers
    static void handle_alpaca_message(const drogon::WebSocketConnectionPtr& conn,
                                      WsConnectionState& state,
                                      const nlohmann::json& msg);
    static void handle_polygon_message(const drogon::WebSocketConnectionPtr& conn,
                                       WsConnectionState& state,
                                       const nlohmann::json& msg);
    static void handle_finnhub_message(const drogon::WebSocketConnectionPtr& conn,
                                       WsConnectionState& state,
                                       const nlohmann::json& msg);
    static void handle_generic_message(const drogon::WebSocketConnectionPtr& conn,
                                       WsConnectionState& state,
                                       const nlohmann::json& msg);

    // Format events for different protocols
    static std::string format_trade_alpaca(const std::string& symbol, const TradeData& trade, Timestamp ts);
    static std::string format_quote_alpaca(const std::string& symbol, const QuoteData& quote, Timestamp ts);
    static std::string format_bar_alpaca(const std::string& symbol, const BarData& bar, Timestamp ts);
    static std::string format_order_alpaca(const OrderData& order, Timestamp ts);

    static std::string format_trade_polygon(const std::string& symbol, const TradeData& trade, Timestamp ts);
    static std::string format_quote_polygon(const std::string& symbol, const QuoteData& quote, Timestamp ts);
    static std::string format_bar_polygon(const std::string& symbol, const BarData& bar, Timestamp ts);

    static std::string format_trade_finnhub(const std::string& symbol, const TradeData& trade, Timestamp ts);

    // Send confirmation messages
    static void send_alpaca_auth_success(const drogon::WebSocketConnectionPtr& conn);
    static void send_alpaca_subscription_update(const drogon::WebSocketConnectionPtr& conn,
                                                 const WsConnectionState& state);
    static void send_polygon_auth_success(const drogon::WebSocketConnectionPtr& conn);
    static void send_polygon_subscription_update(const drogon::WebSocketConnectionPtr& conn,
                                                  const std::vector<std::string>& subscribed);
    static void send_finnhub_ping(const drogon::WebSocketConnectionPtr& conn);

    // Authorization
    static bool authorize_connection(const drogon::HttpRequestPtr& req, WsApiType api_type);

    // Background worker
    static void start_worker();
    static void worker_loop();
    static void enqueue(const std::string& session_id, const std::string& msg);
    static void send_batch(const std::string& session_id, const std::vector<std::string>& msgs);

    // Backpressure management
    static void update_backpressure(const drogon::WebSocketConnectionPtr& conn, size_t bytes_sent);
    static void on_message_drained(const drogon::WebSocketConnectionPtr& conn, size_t bytes);
    static bool should_drop_for_slow_consumer(const WsConnectionState& state);
    static size_t count_slow_connections(const std::string& session_id);
    static void log_backpressure_stats();
};

} // namespace broker_sim
