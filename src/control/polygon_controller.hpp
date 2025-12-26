#pragma once

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../core/utils.hpp"

namespace broker_sim {

/**
 * Polygon.io API Simulator Controller.
 *
 * Implements the Polygon.io market data API endpoints for:
 * - Aggregates (bars/candlesticks)
 * - Trades (historical and real-time)
 * - Quotes (historical and real-time)
 * - Snapshots
 * - Ticker details
 *
 * API Reference: https://polygon.io/docs/stocks
 */
class PolygonController : public drogon::HttpController<PolygonController> {
public:
    static const bool isAutoCreation = false;

    METHOD_LIST_BEGIN
    // Aggregates (Bars)
    ADD_METHOD_TO(PolygonController::aggs, "/v2/aggs/ticker/{1}/range/{2}/{3}/{4}/{5}", drogon::Get);
    ADD_METHOD_TO(PolygonController::aggsPrev, "/v2/aggs/ticker/{1}/prev", drogon::Get);
    ADD_METHOD_TO(PolygonController::groupedDaily, "/v2/aggs/grouped/locale/us/market/stocks/{1}", drogon::Get);

    // Trades
    ADD_METHOD_TO(PolygonController::trades, "/v3/trades/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::ticksTrades, "/v2/ticks/stocks/trades/{1}/{2}", drogon::Get);
    ADD_METHOD_TO(PolygonController::lastTrade, "/v2/last/trade/{1}", drogon::Get);

    // Quotes
    ADD_METHOD_TO(PolygonController::quotes, "/v3/quotes/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::ticksQuotes, "/v2/ticks/stocks/nbbo/{1}/{2}", drogon::Get);
    ADD_METHOD_TO(PolygonController::lastQuote, "/v2/last/nbbo/{1}", drogon::Get);

    // Corporate Actions
    ADD_METHOD_TO(PolygonController::dividends, "/v3/reference/dividends", drogon::Get);
    ADD_METHOD_TO(PolygonController::splits, "/v3/reference/splits", drogon::Get);

    // Snapshots
    ADD_METHOD_TO(PolygonController::snapshotAll, "/v2/snapshot/locale/us/markets/stocks/tickers", drogon::Get);
    ADD_METHOD_TO(PolygonController::snapshotTicker, "/v2/snapshot/locale/us/markets/stocks/tickers/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::snapshotGainersLosers, "/v2/snapshot/locale/us/markets/stocks/{1}", drogon::Get);

    // Ticker Details
    ADD_METHOD_TO(PolygonController::tickerDetails, "/v3/reference/tickers/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::tickersList, "/v3/reference/tickers", drogon::Get);

    // Technical Indicators
    ADD_METHOD_TO(PolygonController::sma, "/v1/indicators/sma/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::ema, "/v1/indicators/ema/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::rsi, "/v1/indicators/rsi/{1}", drogon::Get);
    ADD_METHOD_TO(PolygonController::macd, "/v1/indicators/macd/{1}", drogon::Get);

    // Open/Close
    ADD_METHOD_TO(PolygonController::dailyOpenClose, "/v1/open-close/{1}/{2}", drogon::Get);

    // Market Status
    ADD_METHOD_TO(PolygonController::marketStatus, "/v1/marketstatus/now", drogon::Get);
    ADD_METHOD_TO(PolygonController::marketHolidays, "/v1/marketstatus/upcoming", drogon::Get);

    // Reference & News
    ADD_METHOD_TO(PolygonController::news, "/v2/reference/news", drogon::Get);
    ADD_METHOD_TO(PolygonController::ipos, "/vX/reference/ipos", drogon::Get);
    ADD_METHOD_TO(PolygonController::shortInterest, "/stocks/v1/short-interest", drogon::Get);
    ADD_METHOD_TO(PolygonController::shortVolume, "/stocks/v1/short-volume", drogon::Get);
    ADD_METHOD_TO(PolygonController::financials, "/vX/reference/financials", drogon::Get);
    METHOD_LIST_END

    PolygonController(std::shared_ptr<SessionManager> session_mgr, const Config& cfg);

    // Aggregates
    void aggs(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
              std::string symbol, std::string multiplier, std::string timespan,
              std::string from, std::string to);
    void aggsPrev(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                  std::string symbol);
    void groupedDaily(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                      std::string date);

    // Trades
    void trades(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                std::string symbol);
    void ticksTrades(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string symbol, std::string date);
    void lastTrade(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                   std::string symbol);

    // Quotes
    void quotes(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                std::string symbol);
    void ticksQuotes(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string symbol, std::string date);
    void lastQuote(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                   std::string symbol);

    // Corporate Actions
    void dividends(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void splits(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Snapshots
    void snapshotAll(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void snapshotTicker(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string symbol);
    void snapshotGainersLosers(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string direction);

    // Ticker Details
    void tickerDetails(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                       std::string symbol);
    void tickersList(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Technical Indicators
    void sma(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
             std::string symbol);
    void ema(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
             std::string symbol);
    void rsi(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
             std::string symbol);
    void macd(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
              std::string symbol);

    // Open/Close
    void dailyOpenClose(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string symbol, std::string date);

    // Market Status
    void marketStatus(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void marketHolidays(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    // Reference & News
    void news(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void ipos(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void shortInterest(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void shortVolume(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void financials(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& cb);

private:
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);
    drogon::HttpResponsePtr error_resp(const std::string& message, int code);
    drogon::HttpResponsePtr unauthorized();
    bool authorize(const drogon::HttpRequestPtr& req);
    std::shared_ptr<Session> get_session(const drogon::HttpRequestPtr& req);

    // Data cache
    struct QuoteCache {
        double bid_price;
        double bid_size;
        double ask_price;
        double ask_size;
        int64_t ts_ns;
    };

    struct TradeCache {
        double price;
        double size;
        int64_t ts_ns;
        std::string conditions;
    };

    std::shared_ptr<SessionManager> session_mgr_;
    Config cfg_;
    std::mutex cache_mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, QuoteCache>> quotes_cache_;
    std::unordered_map<std::string, std::unordered_map<std::string, TradeCache>> trades_cache_;
};

} // namespace broker_sim
