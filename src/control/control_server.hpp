#pragma once

#include <thread>
#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <vector>
#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../ws/ws_controller.hpp"
#include "../core/rate_limiter.hpp"
#include <unordered_map>

namespace broker_sim {

class ControlServer : public drogon::HttpController<ControlServer> {
public:
    static const bool isAutoCreation = false;
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ControlServer::createSession, "/sessions", drogon::Post);
    ADD_METHOD_TO(ControlServer::listSessions, "/sessions", drogon::Get);
    ADD_METHOD_TO(ControlServer::getSession, "/sessions/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::deleteSession, "/sessions/{1}", drogon::Delete);
    ADD_METHOD_TO(ControlServer::submitOrder, "/sessions/{1}/orders", drogon::Post);
    ADD_METHOD_TO(ControlServer::listOrders, "/sessions/{1}/orders", drogon::Get);
    ADD_METHOD_TO(ControlServer::account, "/sessions/{1}/account", drogon::Get);
    ADD_METHOD_TO(ControlServer::stats, "/sessions/{1}/stats", drogon::Get);
    ADD_METHOD_TO(ControlServer::eventLog, "/sessions/{1}/events/log", drogon::Get);
    ADD_METHOD_TO(ControlServer::events, "/sessions/{1}/events", drogon::Get);
    ADD_METHOD_TO(ControlServer::performance, "/sessions/{1}/performance", drogon::Get);
    ADD_METHOD_TO(ControlServer::sessionTime, "/sessions/{1}/time", drogon::Get);
    ADD_METHOD_TO(ControlServer::start, "/sessions/{1}/start", drogon::Post);
    ADD_METHOD_TO(ControlServer::pause, "/sessions/{1}/pause", drogon::Post);
    ADD_METHOD_TO(ControlServer::resume, "/sessions/{1}/resume", drogon::Post);
    ADD_METHOD_TO(ControlServer::stop, "/sessions/{1}/stop", drogon::Post);
    ADD_METHOD_TO(ControlServer::setSpeed, "/sessions/{1}/speed", drogon::Post);
    ADD_METHOD_TO(ControlServer::jumpTo, "/sessions/{1}/jump", drogon::Post);
    ADD_METHOD_TO(ControlServer::fastForward, "/sessions/{1}/fast_forward", drogon::Post);
    ADD_METHOD_TO(ControlServer::watermark, "/sessions/{1}/watermark", drogon::Get);
    ADD_METHOD_TO(ControlServer::cancel, "/sessions/{1}/orders/{2}/cancel", drogon::Post);
    ADD_METHOD_TO(ControlServer::applyDividend, "/sessions/{1}/corporate_actions/dividend", drogon::Post);
    ADD_METHOD_TO(ControlServer::applySplit, "/sessions/{1}/corporate_actions/split", drogon::Post);
    ADD_METHOD_TO(ControlServer::alpacaAccount, "/alpaca/v2/account", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaPositions, "/alpaca/v2/positions", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaPosition, "/alpaca/v2/positions/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaOrders, "/alpaca/v2/orders", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaAssets, "/alpaca/v2/assets", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaClock, "/alpaca/v2/clock", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaCalendar, "/alpaca/v2/calendar", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaActivities, "/alpaca/v2/account/activities", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaPortfolioHistory, "/alpaca/v2/account/portfolio/history", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaBars, "/alpaca/v2/bars", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaWatchlists, "/alpaca/v2/watchlists", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaCreateWatchlist, "/alpaca/v2/watchlists", drogon::Post);
    ADD_METHOD_TO(ControlServer::alpacaGetWatchlist, "/alpaca/v2/watchlists/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::alpacaDeleteWatchlist, "/alpaca/v2/watchlists/{1}", drogon::Delete);
    ADD_METHOD_TO(ControlServer::polygonSnapshot, "/polygon/v2/snapshot", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonLastQuote, "/polygon/v2/last_quote", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonLastTrade, "/polygon/v2/last_trade", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonAggs, "/polygon/v2/aggs/ticker/{1}/range/{2}/{3}/{4}/{5}", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonPrev, "/polygon/v2/aggs/ticker/{1}/prev", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonTrades, "/polygon/v3/trades/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonQuotes, "/polygon/v3/quotes/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::polygonSnapshotAll, "/polygon/v2/snapshot/locale/us/markets/stocks/tickers", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubQuote, "/finnhub/quote", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubTrades, "/finnhub/trades", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubCandles, "/finnhub/stock/candle", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubProfile, "/finnhub/stock/profile2", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubPeers, "/finnhub/stock/peers", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubCompanyNews, "/finnhub/company-news", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubNewsSentiment, "/finnhub/news-sentiment", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubMetric, "/finnhub/stock/metric", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubEarningsCalendar, "/finnhub/calendar/earnings", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubRecommendation, "/finnhub/stock/recommendation", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubPriceTarget, "/finnhub/stock/price-target", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubUpgradeDowngrade, "/finnhub/stock/upgrade-downgrade", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubDividend, "/finnhub/stock/dividend", drogon::Get);
    ADD_METHOD_TO(ControlServer::finnhubSplit, "/finnhub/stock/split", drogon::Get);
    ADD_METHOD_TO(ControlServer::health, "/health", drogon::Get);
    METHOD_LIST_END

    ControlServer(std::shared_ptr<SessionManager> session_mgr, const Config& cfg);

    // HTTP handlers
    void createSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void listSessions(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void getSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void deleteSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void submitOrder(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void listOrders(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void account(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void stats(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void eventLog(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void events(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void performance(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void sessionTime(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void start(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void pause(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void resume(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void setSpeed(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void jumpTo(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void fastForward(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void watermark(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void stop(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void cancel(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id, std::string order_id);
    void applyDividend(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void applySplit(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void alpacaAccount(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaPositions(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaPosition(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string symbol);
    void alpacaOrders(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaAssets(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaClock(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaCalendar(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaActivities(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaPortfolioHistory(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaBars(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaWatchlists(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaCreateWatchlist(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void alpacaGetWatchlist(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string wid);
    void alpacaDeleteWatchlist(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string wid);
    void polygonSnapshot(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void polygonLastQuote(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void polygonLastTrade(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void polygonAggs(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                     std::string ticker, std::string multiplier, std::string timespan, std::string from, std::string to);
    void polygonPrev(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                     std::string ticker);
    void polygonTrades(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                       std::string ticker);
    void polygonQuotes(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                       std::string ticker);
    void polygonSnapshotAll(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubQuote(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubTrades(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubCandles(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubProfile(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubPeers(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubCompanyNews(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubNewsSentiment(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubMetric(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubEarningsCalendar(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubRecommendation(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubPriceTarget(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubUpgradeDowngrade(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubDividend(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void finnhubSplit(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void health(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // event propagation
    void on_event(const std::string& session_id, const Event& ev);

private:
    std::shared_ptr<Session> resolve_session_from_param(const drogon::HttpRequestPtr& req);
    std::string resolve_symbol_from_param(const drogon::HttpRequestPtr& req);
    nlohmann::json event_to_json(const Event& ev);
    drogon::HttpResponsePtr unauthorized();
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);

    std::shared_ptr<SessionManager> session_mgr_;
    Config cfg_;

    std::mutex events_mutex_;
    std::unordered_map<std::string, std::deque<nlohmann::json>> events_;
    std::condition_variable events_cv_;
    size_t max_events_per_session_{5000};

    std::mutex last_mutex_;
    struct QuoteView { double bid_price; double bid_size; double ask_price; double ask_size; int64_t ts_ns; };
    struct TradeView { double price; double size; int64_t ts_ns; int exchange{0}; std::string conditions; };
    std::unordered_map<std::string, std::unordered_map<std::string, QuoteView>> last_quotes_;
    std::unordered_map<std::string, std::unordered_map<std::string, TradeView>> last_trades_;

    struct Watchlist {
        std::string id;
        std::string name;
        std::vector<std::string> symbols;
    };
    std::unordered_map<std::string, std::vector<Watchlist>> watchlists_;

    std::atomic<uint64_t> events_published_{0};
    std::atomic<uint64_t> orders_submitted_{0};
    std::atomic<uint64_t> orders_canceled_{0};
    RateLimiter limiter_{500, std::chrono::seconds(60)};

    bool authorize(const drogon::HttpRequestPtr& req);
};

} // namespace broker_sim
