#include "alpaca_controller.hpp"
#include "alpaca_format.hpp"
#include <spdlog/spdlog.h>
#include <drogon/drogon.h>

using json = nlohmann::json;

namespace broker_sim {

AlpacaController::AlpacaController(std::shared_ptr<SessionManager> session_mgr, const Config& cfg)
    : session_mgr_(std::move(session_mgr)), cfg_(cfg) {
    // Register event callback to cache last trades/quotes
    session_mgr_->add_event_callback([this](const std::string& session_id, const Event& ev) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        int64_t ts = utils::ts_to_ns(ev.timestamp);
        if (ev.event_type == EventType::TRADE) {
            const auto& t = std::get<TradeData>(ev.data);
            last_trades_[ev.symbol] = {t.price, ts};
        } else if (ev.event_type == EventType::QUOTE) {
            const auto& q = std::get<QuoteData>(ev.data);
            last_quotes_[ev.symbol] = {q.bid_price, q.ask_price, ts};
        }
    });
}

// ============================================================================
// Helper Methods
// ============================================================================

drogon::HttpResponsePtr AlpacaController::json_resp(json body, int code) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(body.dump());
    return resp;
}

drogon::HttpResponsePtr AlpacaController::error_resp(const std::string& message, int code) {
    return json_resp(json{{"code", code}, {"message", message}}, code);
}

drogon::HttpResponsePtr AlpacaController::unauthorized() {
    return json_resp(json{{"code", 40110000}, {"message", "access key verification failed"}}, 401);
}

bool AlpacaController::authorize(const drogon::HttpRequestPtr& req) {
    if (cfg_.auth.token.empty()) return true;

    // Check Authorization header (Bearer token)
    auto auth = req->getHeader("authorization");
    if (!auth.empty()) {
        std::string expected = "Bearer " + cfg_.auth.token;
        if (auth == expected) return true;
    }

    // Check APCA-API-KEY-ID and APCA-API-SECRET-KEY headers
    auto key_id = req->getHeader("APCA-API-KEY-ID");
    auto secret = req->getHeader("APCA-API-SECRET-KEY");
    if (!secret.empty() && secret == cfg_.auth.token) return true;
    if (!key_id.empty() && key_id == cfg_.auth.token) return true;

    return false;
}

std::shared_ptr<Session> AlpacaController::get_session(const drogon::HttpRequestPtr& req) {
    // First check for session_id in query params
    auto session_id = req->getParameter("session_id");
    if (!session_id.empty()) {
        return session_mgr_->get_session(session_id);
    }

    // Check APCA-API-KEY-ID as session_id
    auto key_id = req->getHeader("APCA-API-KEY-ID");
    if (!key_id.empty()) {
        auto session = session_mgr_->get_session(key_id);
        if (session) return session;
    }

    // Return first available session
    auto sessions = session_mgr_->list_sessions();
    if (!sessions.empty()) return sessions.front();
    return nullptr;
}

json AlpacaController::format_order(const Order& o) {
    return alpaca_format::format_order(o);
}

json AlpacaController::format_position(const Position& p) {
    return alpaca_format::format_position(p);
}

json AlpacaController::format_account(const AccountState& st, const std::string& session_id) {
    return alpaca_format::format_account(st, session_id);
}

// ============================================================================
// Account Endpoint
// ============================================================================

void AlpacaController::account(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto st = session->account_manager->state();
    cb(json_resp(format_account(st, session->id)));
}

// ============================================================================
// Position Endpoints
// ============================================================================

void AlpacaController::positions(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto positions = session->account_manager->positions();
    json arr = json::array();
    for (const auto& [symbol, pos] : positions) {
        if (std::abs(pos.qty) > 0.0001) {  // Only include non-zero positions
            arr.push_back(format_position(pos));
        }
    }
    cb(json_resp(arr));
}

void AlpacaController::getPosition(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                   std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto positions = session->account_manager->positions();
    auto it = positions.find(symbol);
    if (it == positions.end() || std::abs(it->second.qty) < 0.0001) {
        cb(error_resp("position does not exist", 404));
        return;
    }
    cb(json_resp(format_position(it->second)));
}

void AlpacaController::closePosition(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                     std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto positions = session->account_manager->positions();
    auto it = positions.find(symbol);
    if (it == positions.end() || std::abs(it->second.qty) < 0.0001) {
        cb(error_resp("position does not exist", 404));
        return;
    }

    // Create a market order to close the position
    Order order;
    order.symbol = symbol;
    order.qty = std::abs(it->second.qty);
    order.side = it->second.qty > 0 ? OrderSide::SELL : OrderSide::BUY;  // Opposite side to close
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;

    // Check for qty/percentage query params
    auto qty_param = req->getParameter("qty");
    auto pct_param = req->getParameter("percentage");

    if (!qty_param.empty()) {
        order.qty = std::stod(qty_param);
    } else if (!pct_param.empty()) {
        double pct = std::stod(pct_param);
        order.qty = std::abs(it->second.qty) * (pct / 100.0);
    }

    auto order_id = session_mgr_->submit_order(session->id, order);
    if (order_id.empty()) {
        cb(error_resp("failed to create close order", 500));
        return;
    }

    // Get the created order
    auto orders = session_mgr_->get_orders(session->id);
    auto order_it = orders.find(order_id);
    if (order_it != orders.end()) {
        cb(json_resp(format_order(order_it->second)));
    } else {
        cb(json_resp({{"id", order_id}, {"status", "accepted"}}));
    }
}

void AlpacaController::closeAllPositions(const drogon::HttpRequestPtr& req,
                                         std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    bool cancel_orders = req->getParameter("cancel_orders") == "true";

    // Cancel all open orders first if requested
    if (cancel_orders) {
        auto orders = session_mgr_->get_orders(session->id);
        for (const auto& [id, order] : orders) {
            if (order.status == OrderStatus::NEW ||
                order.status == OrderStatus::ACCEPTED ||
                order.status == OrderStatus::PARTIALLY_FILLED) {
                session_mgr_->cancel_order(session->id, id);
            }
        }
    }

    // Close all positions
    auto positions = session->account_manager->positions();
    json results = json::array();

    for (const auto& [symbol, pos] : positions) {
        if (std::abs(pos.qty) < 0.0001) continue;

        Order order;
        order.symbol = symbol;
        order.qty = std::abs(pos.qty);
        order.side = pos.qty > 0 ? OrderSide::SELL : OrderSide::BUY;
        order.type = OrderType::MARKET;
        order.tif = TimeInForce::DAY;

        auto order_id = session_mgr_->submit_order(session->id, order);
        if (!order_id.empty()) {
            results.push_back({
                {"symbol", symbol},
                {"status", 200},
                {"body", {{"id", order_id}}}
            });
        } else {
            results.push_back({
                {"symbol", symbol},
                {"status", 500},
                {"body", {{"message", "failed to close position"}}}
            });
        }
    }

    cb(json_resp(results));
}

// ============================================================================
// Order Endpoints
// ============================================================================

void AlpacaController::orders(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    // Parse query parameters
    std::string status_filter = req->getParameter("status");
    int limit = 50;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::stoi(limit_param);
    }

    auto orders = session_mgr_->get_orders(session->id);
    json arr = json::array();

    for (const auto& [id, order] : orders) {
        // Apply status filter
        if (!status_filter.empty() && status_filter != "all") {
            bool include = false;
            if (status_filter == "open") {
                include = order.status == OrderStatus::NEW ||
                         order.status == OrderStatus::ACCEPTED ||
                         order.status == OrderStatus::PENDING_NEW ||
                         order.status == OrderStatus::PARTIALLY_FILLED;
            } else if (status_filter == "closed") {
                include = order.status == OrderStatus::FILLED ||
                         order.status == OrderStatus::CANCELED ||
                         order.status == OrderStatus::EXPIRED ||
                         order.status == OrderStatus::REJECTED;
            }
            if (!include) continue;
        }

        arr.push_back(format_order(order));
        if (static_cast<int>(arr.size()) >= limit) break;
    }

    cb(json_resp(arr));
}

void AlpacaController::getOrder(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                std::string order_id) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto orders = session_mgr_->get_orders(session->id);
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        cb(error_resp("order not found", 404));
        return;
    }

    cb(json_resp(format_order(it->second)));
}

void AlpacaController::getOrderByClientId(const drogon::HttpRequestPtr& req,
                                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto client_order_id = req->getParameter("client_order_id");
    if (client_order_id.empty()) {
        cb(error_resp("client_order_id required", 400));
        return;
    }

    auto orders = session_mgr_->get_orders(session->id);
    for (const auto& [id, order] : orders) {
        if (order.client_order_id == client_order_id) {
            cb(json_resp(format_order(order)));
            return;
        }
    }

    cb(error_resp("order not found", 404));
}

void AlpacaController::submitOrder(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    try {
        auto body = json::parse(req->getBody());

        Order order;
        order.symbol = body.at("symbol").get<std::string>();
        order.client_order_id = body.value("client_order_id", utils::generate_id());

        // Side
        std::string side = body.value("side", "buy");
        order.side = side == "buy" ? OrderSide::BUY : OrderSide::SELL;

        // Type
        std::string type = body.value("type", "market");
        if (type == "limit") order.type = OrderType::LIMIT;
        else if (type == "stop") order.type = OrderType::STOP;
        else if (type == "stop_limit") order.type = OrderType::STOP_LIMIT;
        else if (type == "trailing_stop") order.type = OrderType::TRAILING_STOP;
        else order.type = OrderType::MARKET;

        // Time in force
        std::string tif = body.value("time_in_force", "day");
        if (tif == "gtc") order.tif = TimeInForce::GTC;
        else if (tif == "ioc") order.tif = TimeInForce::IOC;
        else if (tif == "fok") order.tif = TimeInForce::FOK;
        else if (tif == "opg") order.tif = TimeInForce::OPG;
        else if (tif == "cls") order.tif = TimeInForce::CLS;
        else order.tif = TimeInForce::DAY;

        // Quantity (can be qty or notional)
        if (body.contains("qty")) {
            order.qty = body["qty"].get<double>();
        } else if (body.contains("notional")) {
            // Notional orders need to be converted to qty based on current price
            // For now, we'll return an error - needs price data
            cb(error_resp("notional orders not yet supported", 400));
            return;
        }

        // Prices
        if (body.contains("limit_price")) {
            order.limit_price = body["limit_price"].get<double>();
        }
        if (body.contains("stop_price")) {
            order.stop_price = body["stop_price"].get<double>();
        }
        if (body.contains("trail_price")) {
            order.trail_price = body["trail_price"].get<double>();
        }
        if (body.contains("trail_percent")) {
            order.trail_percent = body["trail_percent"].get<double>();
        }

        // Extended hours not supported in simulation

        // Submit order
        auto order_id = session_mgr_->submit_order(session->id, order);
        if (order_id.empty()) {
            cb(error_resp("order submission failed", 422));
            return;
        }

        // Return the created order
        auto orders = session_mgr_->get_orders(session->id);
        auto it = orders.find(order_id);
        if (it != orders.end()) {
            cb(json_resp(format_order(it->second), 200));
        } else {
            order.id = order_id;
            cb(json_resp(format_order(order), 200));
        }

    } catch (const json::exception& e) {
        cb(error_resp(std::string("invalid request: ") + e.what(), 400));
    } catch (const std::exception& e) {
        cb(error_resp(e.what(), 500));
    }
}

void AlpacaController::replaceOrder(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                    std::string order_id) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto orders = session_mgr_->get_orders(session->id);
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        cb(error_resp("order not found", 404));
        return;
    }

    // Can only replace open orders
    if (it->second.status != OrderStatus::NEW &&
        it->second.status != OrderStatus::ACCEPTED &&
        it->second.status != OrderStatus::PARTIALLY_FILLED) {
        cb(error_resp("order cannot be replaced", 422));
        return;
    }

    try {
        auto body = json::parse(req->getBody());

        // Cancel the old order first
        session_mgr_->cancel_order(session->id, order_id);

        // Create a new order with updated values
        Order new_order = it->second;
        new_order.id.clear();  // Will get new ID
        new_order.client_order_id = body.value("client_order_id", new_order.client_order_id + "_replaced");

        if (body.contains("qty")) {
            new_order.qty = body["qty"].get<double>();
        }
        if (body.contains("limit_price")) {
            new_order.limit_price = body["limit_price"].get<double>();
        }
        if (body.contains("stop_price")) {
            new_order.stop_price = body["stop_price"].get<double>();
        }
        if (body.contains("trail")) {
            new_order.trail_price = body["trail"].get<double>();
        }
        if (body.contains("time_in_force")) {
            std::string tif = body["time_in_force"].get<std::string>();
            if (tif == "gtc") new_order.tif = TimeInForce::GTC;
            else if (tif == "ioc") new_order.tif = TimeInForce::IOC;
            else if (tif == "fok") new_order.tif = TimeInForce::FOK;
            else new_order.tif = TimeInForce::DAY;
        }

        // Reset fill state
        new_order.filled_qty = 0.0;
        new_order.last_fill_price = 0.0;
        new_order.status = OrderStatus::NEW;

        auto new_order_id = session_mgr_->submit_order(session->id, new_order);
        if (new_order_id.empty()) {
            cb(error_resp("replacement order failed", 500));
            return;
        }

        auto new_orders = session_mgr_->get_orders(session->id);
        auto new_it = new_orders.find(new_order_id);
        if (new_it != new_orders.end()) {
            cb(json_resp(format_order(new_it->second)));
        } else {
            new_order.id = new_order_id;
            cb(json_resp(format_order(new_order)));
        }

    } catch (const json::exception& e) {
        cb(error_resp(std::string("invalid request: ") + e.what(), 400));
    }
}

void AlpacaController::cancelOrder(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                   std::string order_id) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    bool ok = session_mgr_->cancel_order(session->id, order_id);
    if (!ok) {
        cb(error_resp("order not found or cannot be canceled", 404));
        return;
    }

    // Return 204 No Content on success
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k204NoContent);
    cb(resp);
}

void AlpacaController::cancelAllOrders(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    auto orders = session_mgr_->get_orders(session->id);
    json results = json::array();

    for (const auto& [id, order] : orders) {
        // Only cancel open orders
        if (order.status != OrderStatus::NEW &&
            order.status != OrderStatus::ACCEPTED &&
            order.status != OrderStatus::PARTIALLY_FILLED) {
            continue;
        }

        bool ok = session_mgr_->cancel_order(session->id, id);
        if (ok) {
            results.push_back({
                {"id", id},
                {"status", 200}
            });
        } else {
            results.push_back({
                {"id", id},
                {"status", 500},
                {"body", {{"message", "failed to cancel"}}}
            });
        }
    }

    cb(json_resp(results));
}

// ============================================================================
// Market Data Endpoints
// ============================================================================

void AlpacaController::getTrades(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    // Get parameters
    auto start = req->getParameter("start");
    auto end = req->getParameter("end");
    int limit = 1000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::stoi(limit_param);
    }

    // Get trades from data source
    json trades = json::array();
    // Note: In production, this would query the data source
    // For now, return empty array or cached data

    cb(json_resp({
        {"trades", trades},
        {"symbol", symbol},
        {"next_page_token", nullptr}
    }));
}

void AlpacaController::getQuotes(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    json quotes = json::array();

    cb(json_resp({
        {"quotes", quotes},
        {"symbol", symbol},
        {"next_page_token", nullptr}
    }));
}

void AlpacaController::getBars(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    json bars = json::array();

    cb(json_resp({
        {"bars", bars},
        {"symbol", symbol},
        {"next_page_token", nullptr}
    }));
}

void AlpacaController::getLatestTrade(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                      std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = last_trades_.find(symbol);
    if (it == last_trades_.end()) {
        cb(error_resp("no trade data available", 404));
        return;
    }

    cb(json_resp({
        {"symbol", symbol},
        {"trade", {
            {"t", utils::ns_to_iso(it->second.second)},
            {"p", it->second.first},
            {"s", 100},  // Placeholder size
            {"x", "V"},
            {"c", json::array()}
        }}
    }));
}

void AlpacaController::getLatestQuote(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                      std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = last_quotes_.find(symbol);
    if (it == last_quotes_.end()) {
        cb(error_resp("no quote data available", 404));
        return;
    }

    auto [bid, ask, ts] = it->second;
    cb(json_resp({
        {"symbol", symbol},
        {"quote", {
            {"t", utils::ns_to_iso(ts)},
            {"bp", bid},
            {"bs", 100},
            {"ap", ask},
            {"as", 100},
            {"bx", "V"},
            {"ax", "V"},
            {"c", json::array()}
        }}
    }));
}

void AlpacaController::getSnapshot(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                   std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    json snapshot = {{"symbol", symbol}};

    // Add latest trade
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto t_it = last_trades_.find(symbol);
        if (t_it != last_trades_.end()) {
            snapshot["latestTrade"] = {
                {"t", utils::ns_to_iso(t_it->second.second)},
                {"p", t_it->second.first},
                {"s", 100},
                {"x", "V"}
            };
        }

        auto q_it = last_quotes_.find(symbol);
        if (q_it != last_quotes_.end()) {
            auto [bid, ask, ts] = q_it->second;
            snapshot["latestQuote"] = {
                {"t", utils::ns_to_iso(ts)},
                {"bp", bid},
                {"bs", 100},
                {"ap", ask},
                {"as", 100}
            };
        }
    }

    // Add NBBO from matching engine if available
    if (session) {
        auto nbbo = session->matching_engine->get_nbbo(symbol);
        if (nbbo) {
            snapshot["latestQuote"] = {
                {"bp", nbbo->bid_price},
                {"bs", nbbo->bid_size},
                {"ap", nbbo->ask_price},
                {"as", nbbo->ask_size}
            };
        }
    }

    cb(json_resp(snapshot));
}

// ============================================================================
// Clock & Calendar Endpoints
// ============================================================================

void AlpacaController::clock(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    Timestamp now;
    if (session) {
        now = session->time_engine->current_time();
    } else {
        now = std::chrono::system_clock::now();
    }

    // Determine if market is open (9:30 AM - 4:00 PM ET on weekdays)
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    // Simplified: assume market hours are 14:30-21:00 UTC
    bool is_open = (tm.tm_wday >= 1 && tm.tm_wday <= 5) &&
                   (tm.tm_hour >= 14 && tm.tm_hour < 21);

    cb(json_resp({
        {"timestamp", utils::ts_to_iso(now)},
        {"is_open", is_open},
        {"next_open", utils::ts_to_iso(now)},  // Simplified
        {"next_close", utils::ts_to_iso(now)}  // Simplified
    }));
}

void AlpacaController::calendar(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Return simplified calendar
    json days = json::array();

    // Generate some sample calendar days
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 30; ++i) {
        auto day = now + std::chrono::hours(24 * i);
        auto t = std::chrono::system_clock::to_time_t(day);
        std::tm tm{};
        gmtime_r(&t, &tm);

        // Skip weekends
        if (tm.tm_wday == 0 || tm.tm_wday == 6) continue;

        days.push_back({
            {"date", utils::ts_to_date(day)},
            {"open", "09:30"},
            {"close", "16:00"}
        });
    }

    cb(json_resp(days));
}

// ============================================================================
// Assets Endpoints
// ============================================================================

void AlpacaController::assets(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Return a sample list of assets
    // In production, this would come from a database
    json assets = json::array();

    std::vector<std::string> sample_symbols = {
        "AAPL", "MSFT", "GOOGL", "AMZN", "META", "TSLA", "NVDA", "JPM", "V", "JNJ"
    };

    for (const auto& sym : sample_symbols) {
        assets.push_back({
            {"id", sym},
            {"class", "us_equity"},
            {"exchange", "NASDAQ"},
            {"symbol", sym},
            {"name", sym},
            {"status", "active"},
            {"tradable", true},
            {"marginable", true},
            {"shortable", true},
            {"easy_to_borrow", true},
            {"fractionable", true}
        });
    }

    cb(json_resp(assets));
}

void AlpacaController::getAsset(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                std::string symbol_or_id) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Return asset info
    cb(json_resp({
        {"id", symbol_or_id},
        {"class", "us_equity"},
        {"exchange", "NASDAQ"},
        {"symbol", symbol_or_id},
        {"name", symbol_or_id},
        {"status", "active"},
        {"tradable", true},
        {"marginable", true},
        {"shortable", true},
        {"easy_to_borrow", true},
        {"fractionable", true}
    }));
}

} // namespace broker_sim
