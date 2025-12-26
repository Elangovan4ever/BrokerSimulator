#include "ws_controller.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

using json = nlohmann::json;

namespace broker_sim {

// Static member definitions
std::shared_ptr<SessionManager> WsController::session_mgr_;
Config WsController::cfg_;
std::mutex WsController::conn_mutex_;
std::unordered_map<drogon::WebSocketConnectionPtr, WsConnectionState> WsController::conn_states_;
std::unordered_map<std::string, std::vector<drogon::WebSocketConnectionPtr>> WsController::session_conns_;
std::mutex WsController::queue_mutex_;
std::condition_variable WsController::queue_cv_;
std::unordered_map<std::string, std::deque<std::string>> WsController::outbox_;
std::atomic<bool> WsController::worker_running_{false};
std::unique_ptr<std::thread> WsController::worker_;

void WsController::init(std::shared_ptr<SessionManager> session_mgr, const Config& cfg) {
    session_mgr_ = std::move(session_mgr);
    cfg_ = cfg;

    // Register event callback to broadcast events to WebSocket clients
    session_mgr_->add_event_callback([](const std::string& session_id, const Event& ev) {
        broadcast_event(session_id, ev);
    });

    start_worker();
    spdlog::info("WebSocket controller initialized");
}

void WsController::shutdown() {
    worker_running_.store(false);
    queue_cv_.notify_all();
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
}

void WsController::broadcast_to_session(const std::string& session_id, const std::string& msg) {
    enqueue(session_id, msg);
}

WsApiType WsController::get_api_type(const std::string& path) {
    if (path.find("/alpaca") != std::string::npos) {
        return WsApiType::ALPACA;
    } else if (path.find("/polygon") != std::string::npos) {
        return WsApiType::POLYGON;
    } else if (path.find("/finnhub") != std::string::npos) {
        return WsApiType::FINNHUB;
    }
    return WsApiType::GENERIC;
}

bool WsController::authorize_connection(const drogon::HttpRequestPtr& req, WsApiType api_type) {
    if (cfg_.auth.token.empty()) return true;

    // Finnhub uses query param token
    if (api_type == WsApiType::FINNHUB) {
        auto token = req->getParameter("token");
        return token == cfg_.auth.token;
    }

    // Others use Authorization header or will authenticate via message
    auto auth = req->getHeader("authorization");
    if (!auth.empty()) {
        std::string expected = "Bearer " + cfg_.auth.token;
        return auth == expected;
    }

    // For Alpaca/Polygon, auth can happen after connection via message
    return true;
}

void WsController::handleNewConnection(const drogon::HttpRequestPtr& req,
                                       const drogon::WebSocketConnectionPtr& conn) {
    if (!session_mgr_) {
        conn->send(R"({"error":"server not ready"})");
        conn->shutdown();
        return;
    }

    std::string path = req->getPath();
    WsApiType api_type = get_api_type(path);

    // Check authorization
    if (!authorize_connection(req, api_type)) {
        json err;
        if (api_type == WsApiType::ALPACA) {
            err = {{"T", "error"}, {"code", 401}, {"msg", "unauthorized"}};
        } else if (api_type == WsApiType::POLYGON) {
            err = {{"status", "auth_failed"}, {"message", "unauthorized"}};
        } else {
            err = {{"error", "unauthorized"}};
        }
        conn->send(err.dump());
        conn->shutdown();
        return;
    }

    // Get session ID from query param
    auto session_id = req->getParameter("session_id");

    // Create connection state
    WsConnectionState state;
    state.api_type = api_type;
    state.session_id = session_id;

    // For Finnhub with token, consider authenticated
    if (api_type == WsApiType::FINNHUB && !req->getParameter("token").empty()) {
        state.authenticated = true;
    }

    // If no auth required, auto-authenticate
    if (cfg_.auth.token.empty()) {
        state.authenticated = true;
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conn_states_[conn] = state;
        if (!session_id.empty()) {
            session_conns_[session_id].push_back(conn);
        }
    }

    // Send welcome message based on API type
    json welcome;
    if (api_type == WsApiType::ALPACA) {
        welcome = json::array();
        welcome.push_back({{"T", "success"}, {"msg", "connected"}});
        conn->send(welcome.dump());
    } else if (api_type == WsApiType::POLYGON) {
        welcome = json::array();
        welcome.push_back({{"ev", "status"}, {"status", "connected"}, {"message", "Connected Successfully"}});
        conn->send(welcome.dump());
    } else if (api_type == WsApiType::FINNHUB) {
        welcome = {{"type", "ping"}};
        conn->send(welcome.dump());
    } else {
        welcome = {{"status", "connected"}};
        if (!session_id.empty()) {
            welcome["session_id"] = session_id;
        }
        conn->send(welcome.dump());
    }

    spdlog::debug("WebSocket connection established: path={} session={}", path, session_id);
}

void WsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);

    auto it = conn_states_.find(conn);
    if (it != conn_states_.end()) {
        const auto& session_id = it->second.session_id;
        if (!session_id.empty()) {
            auto& conns = session_conns_[session_id];
            conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
            if (conns.empty()) {
                session_conns_.erase(session_id);
            }
        }
        conn_states_.erase(it);
    }
}

void WsController::handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                                    std::string&& message,
                                    const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text) {
        return;  // Only handle text messages
    }
    json msg;
    try {
        msg = json::parse(message);
    } catch (const json::parse_error& e) {
        spdlog::warn("WebSocket message parse error: {}", e.what());
        conn->send(R"({"error":"invalid json"})");
        return;
    }

    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = conn_states_.find(conn);
    if (it == conn_states_.end()) {
        return;
    }

    auto& state = it->second;
    switch (state.api_type) {
        case WsApiType::ALPACA:
            handle_alpaca_message(conn, state, msg);
            break;
        case WsApiType::POLYGON:
            handle_polygon_message(conn, state, msg);
            break;
        case WsApiType::FINNHUB:
            handle_finnhub_message(conn, state, msg);
            break;
        default:
            handle_generic_message(conn, state, msg);
            break;
    }
}

// ============================================================================
// Alpaca Protocol Handler
// ============================================================================

void WsController::handle_alpaca_message(const drogon::WebSocketConnectionPtr& conn,
                                         WsConnectionState& state,
                                         const json& msg) {
    std::string action = msg.value("action", "");

    if (action == "auth") {
        // Alpaca auth: {"action":"auth","key":"...","secret":"..."}
        std::string key = msg.value("key", "");
        std::string secret = msg.value("secret", "");

        // Validate against config (key can be session_id, secret is token)
        bool valid = cfg_.auth.token.empty() ||
                     secret == cfg_.auth.token ||
                     key == cfg_.auth.token;

        if (valid) {
            state.authenticated = true;
            if (!key.empty() && state.session_id.empty()) {
                // Use key as session_id if not already set
                auto session = session_mgr_->get_session(key);
                if (session) {
                    state.session_id = key;
                    session_conns_[key].push_back(conn);
                }
            }
            send_alpaca_auth_success(conn);
        } else {
            json err = json::array();
            err.push_back({{"T", "error"}, {"code", 401}, {"msg", "auth failed"}});
            conn->send(err.dump());
        }
    }
    else if (action == "subscribe") {
        if (!state.authenticated) {
            json err = json::array();
            err.push_back({{"T", "error"}, {"code", 401}, {"msg", "not authenticated"}});
            conn->send(err.dump());
            return;
        }

        // Handle subscriptions: {"action":"subscribe","trades":["AAPL"],"quotes":["MSFT"],"bars":["*"]}
        if (msg.contains("trades") && msg["trades"].is_array()) {
            for (const auto& sym : msg["trades"]) {
                state.subscriptions[SubscriptionType::TRADES].insert(sym.get<std::string>());
            }
        }
        if (msg.contains("quotes") && msg["quotes"].is_array()) {
            for (const auto& sym : msg["quotes"]) {
                state.subscriptions[SubscriptionType::QUOTES].insert(sym.get<std::string>());
            }
        }
        if (msg.contains("bars") && msg["bars"].is_array()) {
            for (const auto& sym : msg["bars"]) {
                state.subscriptions[SubscriptionType::BARS].insert(sym.get<std::string>());
            }
        }

        send_alpaca_subscription_update(conn, state);
    }
    else if (action == "unsubscribe") {
        // Handle unsubscriptions
        if (msg.contains("trades") && msg["trades"].is_array()) {
            for (const auto& sym : msg["trades"]) {
                state.subscriptions[SubscriptionType::TRADES].erase(sym.get<std::string>());
            }
        }
        if (msg.contains("quotes") && msg["quotes"].is_array()) {
            for (const auto& sym : msg["quotes"]) {
                state.subscriptions[SubscriptionType::QUOTES].erase(sym.get<std::string>());
            }
        }
        if (msg.contains("bars") && msg["bars"].is_array()) {
            for (const auto& sym : msg["bars"]) {
                state.subscriptions[SubscriptionType::BARS].erase(sym.get<std::string>());
            }
        }

        send_alpaca_subscription_update(conn, state);
    }
    else if (action == "listen") {
        // Order updates subscription: {"action":"listen","data":{"streams":["trade_updates"]}}
        if (msg.contains("data") && msg["data"].contains("streams")) {
            for (const auto& stream : msg["data"]["streams"]) {
                if (stream == "trade_updates") {
                    state.subscriptions[SubscriptionType::ORDER_UPDATES].insert("*");
                }
            }
        }
        json resp = json::array();
        resp.push_back({{"T", "success"}, {"msg", "listening"}});
        conn->send(resp.dump());
    }
}

void WsController::send_alpaca_auth_success(const drogon::WebSocketConnectionPtr& conn) {
    json resp = json::array();
    resp.push_back({{"T", "success"}, {"msg", "authenticated"}});
    conn->send(resp.dump());
}

void WsController::send_alpaca_subscription_update(const drogon::WebSocketConnectionPtr& conn,
                                                    const WsConnectionState& state) {
    json resp = json::array();
    json sub = {{"T", "subscription"}};

    // Build subscription lists
    json trades = json::array();
    json quotes = json::array();
    json bars = json::array();

    auto trades_it = state.subscriptions.find(SubscriptionType::TRADES);
    if (trades_it != state.subscriptions.end()) {
        for (const auto& s : trades_it->second) trades.push_back(s);
    }
    auto quotes_it = state.subscriptions.find(SubscriptionType::QUOTES);
    if (quotes_it != state.subscriptions.end()) {
        for (const auto& s : quotes_it->second) quotes.push_back(s);
    }
    auto bars_it = state.subscriptions.find(SubscriptionType::BARS);
    if (bars_it != state.subscriptions.end()) {
        for (const auto& s : bars_it->second) bars.push_back(s);
    }

    sub["trades"] = trades;
    sub["quotes"] = quotes;
    sub["bars"] = bars;
    resp.push_back(sub);
    conn->send(resp.dump());
}

// ============================================================================
// Polygon Protocol Handler
// ============================================================================

void WsController::handle_polygon_message(const drogon::WebSocketConnectionPtr& conn,
                                          WsConnectionState& state,
                                          const json& msg) {
    std::string action = msg.value("action", "");

    if (action == "auth") {
        // Polygon auth: {"action":"auth","params":"API_KEY"}
        std::string params = msg.value("params", "");
        bool valid = cfg_.auth.token.empty() || params == cfg_.auth.token;

        if (valid) {
            state.authenticated = true;
            send_polygon_auth_success(conn);
        } else {
            json err = json::array();
            err.push_back({{"ev", "status"}, {"status", "auth_failed"}, {"message", "Authentication failed"}});
            conn->send(err.dump());
        }
    }
    else if (action == "subscribe") {
        if (!state.authenticated) {
            json err = json::array();
            err.push_back({{"ev", "status"}, {"status", "error"}, {"message", "Not authenticated"}});
            conn->send(err.dump());
            return;
        }

        // Polygon subscribe: {"action":"subscribe","params":"T.AAPL,Q.AAPL,AM.AAPL"}
        std::string params = msg.value("params", "");
        std::vector<std::string> subscribed;

        std::stringstream ss(params);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (token.empty()) continue;

            // Parse prefix: T.AAPL, Q.AAPL, AM.AAPL, A.AAPL
            size_t dot = token.find('.');
            if (dot == std::string::npos) continue;

            std::string prefix = token.substr(0, dot);
            std::string symbol = token.substr(dot + 1);

            if (prefix == "T") {
                state.subscriptions[SubscriptionType::TRADES].insert(symbol);
            } else if (prefix == "Q") {
                state.subscriptions[SubscriptionType::QUOTES].insert(symbol);
            } else if (prefix == "AM" || prefix == "A") {
                state.subscriptions[SubscriptionType::BARS].insert(symbol);
            }
            subscribed.push_back(token);
        }

        send_polygon_subscription_update(conn, subscribed);
    }
    else if (action == "unsubscribe") {
        std::string params = msg.value("params", "");
        std::stringstream ss(params);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t dot = token.find('.');
            if (dot == std::string::npos) continue;

            std::string prefix = token.substr(0, dot);
            std::string symbol = token.substr(dot + 1);

            if (prefix == "T") {
                state.subscriptions[SubscriptionType::TRADES].erase(symbol);
            } else if (prefix == "Q") {
                state.subscriptions[SubscriptionType::QUOTES].erase(symbol);
            } else if (prefix == "AM" || prefix == "A") {
                state.subscriptions[SubscriptionType::BARS].erase(symbol);
            }
        }

        json resp = json::array();
        resp.push_back({{"ev", "status"}, {"status", "success"}, {"message", "unsubscribed"}});
        conn->send(resp.dump());
    }
}

void WsController::send_polygon_auth_success(const drogon::WebSocketConnectionPtr& conn) {
    json resp = json::array();
    resp.push_back({{"ev", "status"}, {"status", "auth_success"}, {"message", "authenticated"}});
    conn->send(resp.dump());
}

void WsController::send_polygon_subscription_update(const drogon::WebSocketConnectionPtr& conn,
                                                     const std::vector<std::string>& subscribed) {
    json resp = json::array();
    for (const auto& sub : subscribed) {
        resp.push_back({{"ev", "status"}, {"status", "success"}, {"message", "subscribed to: " + sub}});
    }
    conn->send(resp.dump());
}

// ============================================================================
// Finnhub Protocol Handler
// ============================================================================

void WsController::handle_finnhub_message(const drogon::WebSocketConnectionPtr& conn,
                                          WsConnectionState& state,
                                          const json& msg) {
    std::string type = msg.value("type", "");

    if (type == "subscribe") {
        // Finnhub: {"type":"subscribe","symbol":"AAPL"}
        std::string symbol = msg.value("symbol", "");
        if (!symbol.empty()) {
            state.subscriptions[SubscriptionType::TRADES].insert(symbol);
            // Finnhub doesn't send confirmation, just starts sending data
        }
    }
    else if (type == "unsubscribe") {
        std::string symbol = msg.value("symbol", "");
        if (!symbol.empty()) {
            state.subscriptions[SubscriptionType::TRADES].erase(symbol);
        }
    }
    else if (type == "ping") {
        // Respond to ping with pong
        conn->send(R"({"type":"pong"})");
    }
}

void WsController::send_finnhub_ping(const drogon::WebSocketConnectionPtr& conn) {
    conn->send(R"({"type":"ping"})");
}

// ============================================================================
// Generic Protocol Handler
// ============================================================================

void WsController::handle_generic_message(const drogon::WebSocketConnectionPtr& conn,
                                          WsConnectionState& state,
                                          const json& msg) {
    std::string action = msg.value("action", msg.value("type", ""));

    if (action == "subscribe") {
        // Generic: {"action":"subscribe","symbols":["AAPL"],"types":["trades","quotes"]}
        std::vector<std::string> symbols = msg.value("symbols", std::vector<std::string>{"*"});
        std::vector<std::string> types = msg.value("types", std::vector<std::string>{"trades", "quotes", "bars"});

        for (const auto& t : types) {
            SubscriptionType st;
            if (t == "trades") st = SubscriptionType::TRADES;
            else if (t == "quotes") st = SubscriptionType::QUOTES;
            else if (t == "bars") st = SubscriptionType::BARS;
            else if (t == "orders") st = SubscriptionType::ORDER_UPDATES;
            else continue;

            for (const auto& sym : symbols) {
                state.subscriptions[st].insert(sym);
            }
        }

        state.authenticated = true;  // Generic protocol doesn't require auth
        conn->send(R"({"status":"subscribed"})");
    }
    else if (action == "unsubscribe") {
        std::vector<std::string> symbols = msg.value("symbols", std::vector<std::string>{});
        for (auto& [type, syms] : state.subscriptions) {
            for (const auto& s : symbols) {
                syms.erase(s);
            }
        }
        conn->send(R"({"status":"unsubscribed"})");
    }
    else if (action == "set_session") {
        std::string session_id = msg.value("session_id", "");
        if (!session_id.empty() && session_mgr_->get_session(session_id)) {
            // Remove from old session
            if (!state.session_id.empty()) {
                auto& old_conns = session_conns_[state.session_id];
                old_conns.erase(std::remove(old_conns.begin(), old_conns.end(), conn), old_conns.end());
            }
            state.session_id = session_id;
            session_conns_[session_id].push_back(conn);
            conn->send(json{{"status", "session_set"}, {"session_id", session_id}}.dump());
        } else {
            conn->send(R"({"error":"session not found"})");
        }
    }
}

// ============================================================================
// Event Formatting
// ============================================================================

std::string WsController::format_trade_alpaca(const std::string& symbol, const TradeData& trade, Timestamp ts) {
    json msg = json::array();
    json item;
    item["T"] = "t";
    item["S"] = symbol;
    item["p"] = trade.price;
    item["s"] = trade.size;
    item["t"] = utils::ts_to_iso(ts);
    item["x"] = trade.exchange;
    item["z"] = trade.tape;
    if (!trade.conditions.empty()) {
        item["c"] = json::array({trade.conditions});
    }
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_quote_alpaca(const std::string& symbol, const QuoteData& quote, Timestamp ts) {
    json msg = json::array();
    json item;
    item["T"] = "q";
    item["S"] = symbol;
    item["bp"] = quote.bid_price;
    item["bs"] = quote.bid_size;
    item["ap"] = quote.ask_price;
    item["as"] = quote.ask_size;
    item["bx"] = quote.bid_exchange;
    item["ax"] = quote.ask_exchange;
    item["t"] = utils::ts_to_iso(ts);
    item["z"] = quote.tape;
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_bar_alpaca(const std::string& symbol, const BarData& bar, Timestamp ts) {
    json msg = json::array();
    json item;
    item["T"] = "b";
    item["S"] = symbol;
    item["o"] = bar.open;
    item["h"] = bar.high;
    item["l"] = bar.low;
    item["c"] = bar.close;
    item["v"] = bar.volume;
    item["t"] = utils::ts_to_iso(ts);
    item["n"] = bar.trade_count.value_or(0);
    item["vw"] = bar.vwap.value_or(0.0);
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_order_alpaca(const OrderData& order, Timestamp ts) {
    json msg = json::array();
    json item;
    item["T"] = "trade_update";
    item["event"] = order.status;
    json order_obj;
    order_obj["id"] = order.order_id;
    order_obj["client_order_id"] = order.client_order_id;
    order_obj["qty"] = order.qty;
    order_obj["filled_qty"] = order.filled_qty;
    order_obj["filled_avg_price"] = order.filled_avg_price;
    order_obj["status"] = order.status;
    item["order"] = order_obj;
    item["timestamp"] = utils::ts_to_iso(ts);
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_trade_polygon(const std::string& symbol, const TradeData& trade, Timestamp ts) {
    json msg = json::array();
    json item;
    item["ev"] = "T";
    item["sym"] = symbol;
    item["p"] = trade.price;
    item["s"] = trade.size;
    item["t"] = utils::ts_to_ms(ts);
    item["x"] = trade.exchange;
    item["z"] = trade.tape;
    if (!trade.conditions.empty()) {
        item["c"] = json::array({trade.conditions});
    }
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_quote_polygon(const std::string& symbol, const QuoteData& quote, Timestamp ts) {
    json msg = json::array();
    json item;
    item["ev"] = "Q";
    item["sym"] = symbol;
    item["bp"] = quote.bid_price;
    item["bs"] = quote.bid_size;
    item["ap"] = quote.ask_price;
    item["as"] = quote.ask_size;
    item["bx"] = quote.bid_exchange;
    item["ax"] = quote.ask_exchange;
    item["t"] = utils::ts_to_ms(ts);
    item["z"] = quote.tape;
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_bar_polygon(const std::string& symbol, const BarData& bar, Timestamp ts) {
    json msg = json::array();
    json item;
    item["ev"] = "AM";
    item["sym"] = symbol;
    item["o"] = bar.open;
    item["h"] = bar.high;
    item["l"] = bar.low;
    item["c"] = bar.close;
    item["v"] = bar.volume;
    item["s"] = utils::ts_to_ms(ts);
    item["e"] = utils::ts_to_ms(ts) + 60000;  // 1 minute bar
    item["vw"] = bar.vwap.value_or(0.0);
    item["n"] = bar.trade_count.value_or(0);
    msg.push_back(item);
    return msg.dump();
}

std::string WsController::format_trade_finnhub(const std::string& symbol, const TradeData& trade, Timestamp ts) {
    json msg = {
        {"type", "trade"},
        {"data", json::array({{
            {"s", symbol},
            {"p", trade.price},
            {"v", trade.size},
            {"t", utils::ts_to_ms(ts)},
            {"c", json::array()}  // conditions
        }})}
    };
    return msg.dump();
}

// ============================================================================
// Event Broadcasting
// ============================================================================

void WsController::broadcast_event(const std::string& session_id, const Event& event) {
    std::lock_guard<std::mutex> lock(conn_mutex_);

    auto it = session_conns_.find(session_id);
    if (it == session_conns_.end()) return;

    for (auto& conn : it->second) {
        if (!conn || !conn->connected()) continue;

        auto state_it = conn_states_.find(conn);
        if (state_it == conn_states_.end()) continue;

        const auto& state = state_it->second;
        if (!state.authenticated) continue;

        std::string msg;

        switch (event.event_type) {
            case EventType::TRADE: {
                if (!state.is_subscribed(SubscriptionType::TRADES, event.symbol)) continue;
                const auto& trade = std::get<TradeData>(event.data);
                if (state.api_type == WsApiType::ALPACA) {
                    msg = format_trade_alpaca(event.symbol, trade, event.timestamp);
                } else if (state.api_type == WsApiType::POLYGON) {
                    msg = format_trade_polygon(event.symbol, trade, event.timestamp);
                } else if (state.api_type == WsApiType::FINNHUB) {
                    msg = format_trade_finnhub(event.symbol, trade, event.timestamp);
                } else {
                    msg = json{{"type", "trade"}, {"symbol", event.symbol},
                               {"price", trade.price}, {"size", trade.size},
                               {"timestamp", utils::ts_to_ns(event.timestamp)}}.dump();
                }
                break;
            }
            case EventType::QUOTE: {
                if (!state.is_subscribed(SubscriptionType::QUOTES, event.symbol)) continue;
                const auto& quote = std::get<QuoteData>(event.data);
                if (state.api_type == WsApiType::ALPACA) {
                    msg = format_quote_alpaca(event.symbol, quote, event.timestamp);
                } else if (state.api_type == WsApiType::POLYGON) {
                    msg = format_quote_polygon(event.symbol, quote, event.timestamp);
                } else {
                    msg = json{{"type", "quote"}, {"symbol", event.symbol},
                               {"bid", quote.bid_price}, {"ask", quote.ask_price},
                               {"timestamp", utils::ts_to_ns(event.timestamp)}}.dump();
                }
                break;
            }
            case EventType::BAR: {
                if (!state.is_subscribed(SubscriptionType::BARS, event.symbol)) continue;
                const auto& bar = std::get<BarData>(event.data);
                if (state.api_type == WsApiType::ALPACA) {
                    msg = format_bar_alpaca(event.symbol, bar, event.timestamp);
                } else if (state.api_type == WsApiType::POLYGON) {
                    msg = format_bar_polygon(event.symbol, bar, event.timestamp);
                } else {
                    msg = json{{"type", "bar"}, {"symbol", event.symbol},
                               {"o", bar.open}, {"h", bar.high}, {"l", bar.low}, {"c", bar.close},
                               {"v", bar.volume}}.dump();
                }
                break;
            }
            case EventType::ORDER_NEW:
            case EventType::ORDER_FILL:
            case EventType::ORDER_CANCEL:
            case EventType::ORDER_EXPIRE: {
                if (!state.is_subscribed(SubscriptionType::ORDER_UPDATES, "*")) continue;
                const auto& order = std::get<OrderData>(event.data);
                if (state.api_type == WsApiType::ALPACA) {
                    msg = format_order_alpaca(order, event.timestamp);
                } else {
                    msg = json{{"type", "order_update"}, {"order_id", order.order_id},
                               {"status", order.status}, {"filled_qty", order.filled_qty}}.dump();
                }
                break;
            }
        }

        if (!msg.empty()) {
            conn->send(msg);
        }
    }
}

void WsController::broadcast(const std::string& session_id, const std::string& msg) {
    enqueue(session_id, msg);
}

void WsController::enqueue(const std::string& session_id, const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        auto& q = outbox_[session_id];
        if (cfg_.websocket.queue_size > 0 &&
            q.size() >= static_cast<size_t>(cfg_.websocket.queue_size)) {
            if (cfg_.websocket.overflow_policy == "drop_oldest") {
                if (!q.empty()) q.pop_front();
            } else {
                return;  // drop_newest
            }
        }
        q.push_back(msg);
    }
    queue_cv_.notify_one();
}

void WsController::send_batch(const std::string& session_id, const std::vector<std::string>& msgs) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = session_conns_.find(session_id);
    if (it == session_conns_.end()) return;

    auto& conns = it->second;
    conns.erase(std::remove_if(conns.begin(), conns.end(),
        [](const drogon::WebSocketConnectionPtr& c) { return !c || !c->connected(); }),
        conns.end());

    for (auto& conn : conns) {
        auto state_it = conn_states_.find(conn);
        if (state_it == conn_states_.end()) continue;

        auto& state = state_it->second;

        // Check if this connection is slow - if so, drop messages
        if (should_drop_for_slow_consumer(state)) {
            state.messages_dropped += msgs.size();
            continue;
        }

        size_t batch_bytes = 0;
        for (const auto& msg : msgs) {
            batch_bytes += msg.size();
            conn->send(msg);
        }

        // Update backpressure state
        update_backpressure(conn, batch_bytes);
        state.messages_sent += msgs.size();
        state.bytes_sent += batch_bytes;
    }
}

void WsController::start_worker() {
    if (worker_running_.exchange(true)) return;
    worker_ = std::make_unique<std::thread>(worker_loop);
}

void WsController::worker_loop() {
    try {
        using namespace std::chrono_literals;
        auto interval = std::chrono::milliseconds(std::max(1, cfg_.websocket.flush_interval_ms));

        while (worker_running_.load()) {
        std::unordered_map<std::string, std::vector<std::string>> to_send;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, interval, [] {
                if (!worker_running_.load()) return true;
                for (const auto& kv : outbox_) {
                    if (!kv.second.empty()) return true;
                }
                return false;
            });

            if (!worker_running_.load()) break;

            for (auto& kv : outbox_) {
                auto& q = kv.second;
                if (q.empty()) continue;

                int batch = std::max(1, cfg_.websocket.batch_size);
                std::vector<std::string> msgs;
                msgs.reserve(static_cast<size_t>(batch));

                for (int i = 0; i < batch && !q.empty(); ++i) {
                    msgs.push_back(std::move(q.front()));
                    q.pop_front();
                }

                if (!msgs.empty()) {
                    to_send.emplace(kv.first, std::move(msgs));
                }
            }
        }

        for (auto& kv : to_send) {
            send_batch(kv.first, kv.second);
        }
        }  // end while
    } catch (const std::exception& e) {
        spdlog::error("WsController worker_loop exception: {}", e.what());
    } catch (...) {
        spdlog::error("WsController worker_loop unknown exception");
    }
}

//
// Backpressure management
// Note: All functions assume conn_mutex_ is already held (or acquire it themselves)
//

void WsController::update_backpressure(const drogon::WebSocketConnectionPtr& conn, size_t bytes_sent) {
    // Called from send_batch which already holds conn_mutex_
    auto it = conn_states_.find(conn);
    if (it == conn_states_.end()) return;

    auto& state = it->second;
    auto& bp = state.backpressure;

    // Update pending counts
    bp.pending_bytes += bytes_sent;
    bp.pending_messages += 1;

    // Check if we've exceeded high watermark
    if (bp.pending_bytes > BackpressureState::HIGH_WATERMARK_BYTES ||
        bp.pending_messages > BackpressureState::HIGH_WATERMARK_MESSAGES) {
        if (!bp.is_slow) {
            bp.is_slow = true;
            spdlog::warn("WebSocket connection marked slow: pending_bytes={}, pending_msgs={}",
                         bp.pending_bytes, bp.pending_messages);
        }
    }
}

void WsController::on_message_drained(const drogon::WebSocketConnectionPtr& conn, size_t bytes) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = conn_states_.find(conn);
    if (it == conn_states_.end()) return;

    auto& state = it->second;
    auto& bp = state.backpressure;

    // Decrement pending counts
    if (bp.pending_bytes >= bytes) {
        bp.pending_bytes -= bytes;
    } else {
        bp.pending_bytes = 0;
    }
    if (bp.pending_messages > 0) {
        bp.pending_messages -= 1;
    }

    // Check if we've fallen below low watermark
    if (bp.is_slow &&
        bp.pending_bytes < BackpressureState::LOW_WATERMARK_BYTES &&
        bp.pending_messages < BackpressureState::LOW_WATERMARK_MESSAGES) {

        bp.is_slow = false;
        bp.last_drain_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        spdlog::info("WebSocket connection recovered: pending_bytes={}, pending_msgs={}",
                     bp.pending_bytes, bp.pending_messages);
    }
}

bool WsController::should_drop_for_slow_consumer(const WsConnectionState& state) {
    // Called from send_batch which already holds conn_mutex_
    return state.backpressure.is_slow;
}

size_t WsController::count_slow_connections(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(conn_mutex_);

    auto it = session_conns_.find(session_id);
    if (it == session_conns_.end()) return 0;

    size_t count = 0;
    for (const auto& conn : it->second) {
        auto state_it = conn_states_.find(conn);
        if (state_it != conn_states_.end() && state_it->second.backpressure.is_slow) {
            ++count;
        }
    }
    return count;
}

void WsController::log_backpressure_stats() {
    std::lock_guard<std::mutex> lock(conn_mutex_);

    size_t total_connections = conn_states_.size();
    size_t slow_connections = 0;
    uint64_t total_sent = 0;
    uint64_t total_dropped = 0;

    for (const auto& kv : conn_states_) {
        const auto& state = kv.second;
        if (state.backpressure.is_slow) {
            ++slow_connections;
        }
        total_sent += state.messages_sent;
        total_dropped += state.messages_dropped;
    }

    if (slow_connections > 0 || total_dropped > 0) {
        spdlog::info("WebSocket backpressure stats: connections={}, slow={}, sent={}, dropped={}",
                     total_connections, slow_connections, total_sent, total_dropped);
    }
}

} // namespace broker_sim
