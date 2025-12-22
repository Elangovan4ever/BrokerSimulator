#pragma once

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../core/utils.hpp"

namespace broker_sim {

/**
 * Alpaca Trading API Simulator Controller.
 *
 * Implements the Alpaca Trading API endpoints for:
 * - Account management
 * - Order submission, retrieval, cancellation, and replacement
 * - Position management and liquidation
 * - Market data (historical trades, quotes, bars)
 *
 * API Reference: https://alpaca.markets/docs/api-references/trading-api/
 */
class AlpacaController : public drogon::HttpController<AlpacaController> {
public:
    static const bool isAutoCreation = false;

    METHOD_LIST_BEGIN
    // Account
    ADD_METHOD_TO(AlpacaController::account, "/v2/account", drogon::Get);

    // Positions
    ADD_METHOD_TO(AlpacaController::positions, "/v2/positions", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getPosition, "/v2/positions/{1}", drogon::Get);
    ADD_METHOD_TO(AlpacaController::closePosition, "/v2/positions/{1}", drogon::Delete);
    ADD_METHOD_TO(AlpacaController::closeAllPositions, "/v2/positions", drogon::Delete);

    // Orders
    ADD_METHOD_TO(AlpacaController::orders, "/v2/orders", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getOrder, "/v2/orders/{1}", drogon::Get);
    ADD_METHOD_TO(AlpacaController::submitOrder, "/v2/orders", drogon::Post);
    ADD_METHOD_TO(AlpacaController::replaceOrder, "/v2/orders/{1}", drogon::Patch);
    ADD_METHOD_TO(AlpacaController::cancelOrder, "/v2/orders/{1}", drogon::Delete);
    ADD_METHOD_TO(AlpacaController::cancelAllOrders, "/v2/orders", drogon::Delete);

    // Order by client order ID
    ADD_METHOD_TO(AlpacaController::getOrderByClientId, "/v2/orders:by_client_order_id", drogon::Get);

    // Market Data - Historical
    ADD_METHOD_TO(AlpacaController::getTrades, "/v2/stocks/{1}/trades", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getQuotes, "/v2/stocks/{1}/quotes", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getBars, "/v2/stocks/{1}/bars", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getLatestTrade, "/v2/stocks/{1}/trades/latest", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getLatestQuote, "/v2/stocks/{1}/quotes/latest", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getSnapshot, "/v2/stocks/{1}/snapshot", drogon::Get);

    // Clock & Calendar
    ADD_METHOD_TO(AlpacaController::clock, "/v2/clock", drogon::Get);
    ADD_METHOD_TO(AlpacaController::calendar, "/v2/calendar", drogon::Get);

    // Assets
    ADD_METHOD_TO(AlpacaController::assets, "/v2/assets", drogon::Get);
    ADD_METHOD_TO(AlpacaController::getAsset, "/v2/assets/{1}", drogon::Get);
    METHOD_LIST_END

    AlpacaController(std::shared_ptr<SessionManager> session_mgr, const Config& cfg);

    // Account
    void account(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Positions
    void positions(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void getPosition(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string symbol);
    void closePosition(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                       std::string symbol);
    void closeAllPositions(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Orders
    void orders(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void getOrder(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                  std::string order_id);
    void getOrderByClientId(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void submitOrder(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void replaceOrder(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                      std::string order_id);
    void cancelOrder(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string order_id);
    void cancelAllOrders(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Market Data
    void getTrades(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                   std::string symbol);
    void getQuotes(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                   std::string symbol);
    void getBars(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                 std::string symbol);
    void getLatestTrade(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string symbol);
    void getLatestQuote(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string symbol);
    void getSnapshot(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string symbol);

    // Clock & Calendar
    void clock(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void calendar(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Assets
    void assets(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void getAsset(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                  std::string symbol_or_id);

private:
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);
    drogon::HttpResponsePtr error_resp(const std::string& message, int code);
    drogon::HttpResponsePtr unauthorized();
    bool authorize(const drogon::HttpRequestPtr& req);
    std::shared_ptr<Session> get_session(const drogon::HttpRequestPtr& req);

    // Helpers for formatting
    nlohmann::json format_order(const Order& order);
    nlohmann::json format_position(const Position& pos);
    nlohmann::json format_account(const AccountState& state, const std::string& session_id);

    std::shared_ptr<SessionManager> session_mgr_;
    Config cfg_;

    // Cache for last trades/quotes by symbol
    std::mutex cache_mutex_;
    std::unordered_map<std::string, std::pair<double, int64_t>> last_trades_;  // symbol -> (price, timestamp)
    std::unordered_map<std::string, std::tuple<double, double, int64_t>> last_quotes_;  // symbol -> (bid, ask, timestamp)
};

} // namespace broker_sim
