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
    // Analyst data
    ADD_METHOD_TO(FinnhubController::ipo_calendar, "/calendar/ipo", drogon::Get);
    ADD_METHOD_TO(FinnhubController::earnings_calendar, "/calendar/earnings", drogon::Get);
    ADD_METHOD_TO(FinnhubController::recommendation, "/stock/recommendation", drogon::Get);
    ADD_METHOD_TO(FinnhubController::price_target, "/stock/price-target", drogon::Get);
    ADD_METHOD_TO(FinnhubController::upgrade_downgrade, "/stock/upgrade-downgrade", drogon::Get);
    // Additional finnhub endpoints
    ADD_METHOD_TO(FinnhubController::insider_transactions, "/stock/insider-transactions", drogon::Get);
    ADD_METHOD_TO(FinnhubController::sec_filings, "/stock/filings", drogon::Get);
    ADD_METHOD_TO(FinnhubController::congressional_trading, "/stock/congressional-trading", drogon::Get);
    ADD_METHOD_TO(FinnhubController::insider_sentiment, "/stock/insider-sentiment", drogon::Get);
    ADD_METHOD_TO(FinnhubController::eps_estimate, "/stock/eps-estimate", drogon::Get);
    ADD_METHOD_TO(FinnhubController::revenue_estimate, "/stock/revenue-estimate", drogon::Get);
    ADD_METHOD_TO(FinnhubController::earnings_history, "/stock/earnings", drogon::Get);
    ADD_METHOD_TO(FinnhubController::social_sentiment, "/stock/social-sentiment", drogon::Get);
    ADD_METHOD_TO(FinnhubController::ownership, "/stock/ownership", drogon::Get);
    ADD_METHOD_TO(FinnhubController::financials, "/stock/financials", drogon::Get);
    ADD_METHOD_TO(FinnhubController::financials_reported, "/stock/financials-reported", drogon::Get);
    METHOD_LIST_END

    FinnhubController(std::shared_ptr<SessionManager> session_mgr,
                      std::shared_ptr<DataSource> data_source,
                      const Config& cfg);

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

    // Analyst data
    void ipo_calendar(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void earnings_calendar(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void recommendation(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void price_target(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void upgrade_downgrade(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

    // Additional finnhub endpoints
    void insider_transactions(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void sec_filings(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void congressional_trading(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void insider_sentiment(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void eps_estimate(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void revenue_estimate(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void earnings_history(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void social_sentiment(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void ownership(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void financials(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void financials_reported(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);

private:
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);
    drogon::HttpResponsePtr unauthorized();
    bool authorize(const drogon::HttpRequestPtr& req);
    std::shared_ptr<Session> get_session(const drogon::HttpRequestPtr& req);
    Timestamp current_time(const drogon::HttpRequestPtr& req);
    std::string symbol_param(const drogon::HttpRequestPtr& req);
    std::optional<Timestamp> parse_date(const std::string& date_str);

    std::shared_ptr<SessionManager> session_mgr_;
    std::shared_ptr<DataSource> data_source_;
    Config cfg_;
};

} // namespace broker_sim
