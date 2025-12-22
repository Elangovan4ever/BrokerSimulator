#pragma once

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../core/data_source.hpp"

namespace broker_sim {

class FinnhubController : public drogon::HttpController<FinnhubController> {
public:
    static const bool isAutoCreation = false;

    METHOD_LIST_BEGIN
    // Real-time data
    ADD_METHOD_TO(FinnhubController::quote, "/quote", drogon::Get);
    ADD_METHOD_TO(FinnhubController::trades, "/trades", drogon::Get);
    // Historical data
    ADD_METHOD_TO(FinnhubController::candle, "/stock/candle", drogon::Get);
    // Company information
    ADD_METHOD_TO(FinnhubController::company_profile, "/stock/profile2", drogon::Get);
    ADD_METHOD_TO(FinnhubController::company_peers, "/stock/peers", drogon::Get);
    ADD_METHOD_TO(FinnhubController::basic_financials, "/stock/metric", drogon::Get);
    // News and sentiment
    ADD_METHOD_TO(FinnhubController::company_news, "/company-news", drogon::Get);
    ADD_METHOD_TO(FinnhubController::market_news, "/news", drogon::Get);
    ADD_METHOD_TO(FinnhubController::news_sentiment, "/news-sentiment", drogon::Get);
    // Corporate actions
    ADD_METHOD_TO(FinnhubController::dividends, "/stock/dividend", drogon::Get);
    ADD_METHOD_TO(FinnhubController::splits, "/stock/split", drogon::Get);
    // Analyst data
    ADD_METHOD_TO(FinnhubController::earnings_calendar, "/calendar/earnings", drogon::Get);
    ADD_METHOD_TO(FinnhubController::recommendation, "/stock/recommendation", drogon::Get);
    ADD_METHOD_TO(FinnhubController::price_target, "/stock/price-target", drogon::Get);
    ADD_METHOD_TO(FinnhubController::upgrade_downgrade, "/stock/upgrade-downgrade", drogon::Get);
    METHOD_LIST_END

    FinnhubController(std::shared_ptr<SessionManager> session_mgr,
                      std::shared_ptr<DataSource> data_source,
                      const Config& cfg);

    // Real-time data
    void quote(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void trades(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // Historical data
    void candle(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // Company information
    void company_profile(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void company_peers(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void basic_financials(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // News and sentiment
    void company_news(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void market_news(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void news_sentiment(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // Corporate actions
    void dividends(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void splits(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // Analyst data
    void earnings_calendar(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void recommendation(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void price_target(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void upgrade_downgrade(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

private:
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);
    drogon::HttpResponsePtr unauthorized();
    bool authorize(const drogon::HttpRequestPtr& req);
    std::shared_ptr<Session> default_session();
    std::string symbol_param(const drogon::HttpRequestPtr& req);
    Timestamp parse_date(const std::string& date_str);

    std::shared_ptr<SessionManager> session_mgr_;
    std::shared_ptr<DataSource> data_source_;
    Config cfg_;
    std::mutex last_mutex_;
    struct QuoteView { double bid_price; double ask_price; double ts; };
    struct TradeView { double price; double size; int64_t ts_ns; };
    std::unordered_map<std::string, std::unordered_map<std::string, QuoteView>> last_quotes_;
    std::unordered_map<std::string, std::unordered_map<std::string, TradeView>> last_trades_;
};

} // namespace broker_sim

