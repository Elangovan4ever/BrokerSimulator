#include "finnhub_controller.hpp"
#include <spdlog/spdlog.h>
#include <drogon/drogon.h>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

namespace broker_sim {

FinnhubController::FinnhubController(std::shared_ptr<SessionManager> session_mgr,
                                     std::shared_ptr<DataSource> data_source,
                                     const Config& cfg)
    : session_mgr_(std::move(session_mgr))
    , data_source_(std::move(data_source))
    , cfg_(cfg) {
    session_mgr_->add_event_callback([this](const std::string& session_id, const Event& ev) {
        std::lock_guard<std::mutex> lock(last_mutex_);
        if (ev.event_type == EventType::QUOTE) {
            const auto& q = std::get<QuoteData>(ev.data);
            double ts = std::chrono::duration_cast<std::chrono::seconds>(ev.timestamp.time_since_epoch()).count();
            last_quotes_[session_id][ev.symbol] = {q.bid_price, q.ask_price, ts};
        } else if (ev.event_type == EventType::TRADE) {
            const auto& t = std::get<TradeData>(ev.data);
            int64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count();
            last_trades_[session_id][ev.symbol] = {t.price, static_cast<double>(t.size), ts_ns};
        }
    });
}

drogon::HttpResponsePtr FinnhubController::json_resp(json body, int code) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(body.dump());
    return resp;
}

drogon::HttpResponsePtr FinnhubController::unauthorized() {
    return json_resp(json{{"error","unauthorized"}}, 401);
}

bool FinnhubController::authorize(const drogon::HttpRequestPtr& req) {
    if (cfg_.auth.token.empty()) return true;
    // Finnhub uses ?token= query param
    auto token = req->getParameter("token");
    if (!token.empty() && token == cfg_.auth.token) return true;
    // Also check Authorization header
    auto auth = req->getHeader("authorization");
    std::string expected = "Bearer " + cfg_.auth.token;
    return auth == expected;
}

std::shared_ptr<Session> FinnhubController::default_session() {
    auto sessions = session_mgr_->list_sessions();
    if (!sessions.empty()) return sessions.front();
    return nullptr;
}

std::string FinnhubController::symbol_param(const drogon::HttpRequestPtr& req) {
    return req->getParameter("symbol");
}

Timestamp FinnhubController::parse_date(const std::string& date_str) {
    std::tm tm = {};
    std::istringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        // Try Unix timestamp
        try {
            int64_t ts = std::stoll(date_str);
            return Timestamp{} + std::chrono::seconds(ts);
        } catch (...) {
            return std::chrono::system_clock::now();
        }
    }
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// ============================================================================
// Real-time data endpoints
// ============================================================================

void FinnhubController::quote(const drogon::HttpRequestPtr& req,
                              std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = default_session();
    if (!session) { cb(json_resp(json{{"error","session not found"}},404)); return; }
    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto it = last_quotes_[session->id].find(sym);
    if (it == last_quotes_[session->id].end()) { cb(json_resp(json{{"error","quote not found"}},404)); return; }
    auto q = it->second;
    // Finnhub quote format
    json out{
        {"c", q.ask_price},              // Current price (using ask as proxy)
        {"d", 0.0},                       // Change
        {"dp", 0.0},                      // Percent change
        {"h", q.ask_price},               // High price of the day
        {"l", q.bid_price},               // Low price of the day
        {"o", (q.bid_price + q.ask_price) / 2.0}, // Open price
        {"pc", q.bid_price},              // Previous close price
        {"t", static_cast<int64_t>(q.ts)} // Timestamp
    };
    cb(json_resp(out));
}

void FinnhubController::trades(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = default_session();
    if (!session) { cb(json_resp(json{{"error","session not found"}},404)); return; }
    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto it = last_trades_[session->id].find(sym);
    if (it == last_trades_[session->id].end()) { cb(json_resp(json{{"error","trade not found"}},404)); return; }
    auto t = it->second;
    // Finnhub trade format (WebSocket style)
    json out{
        {"data", json::array({{
            {"p", t.price},
            {"s", sym},
            {"t", t.ts_ns / 1000000},  // milliseconds
            {"v", t.size},
            {"c", json::array()}       // conditions
        }})},
        {"type", "trade"}
    };
    cb(json_resp(out));
}

// ============================================================================
// Historical data endpoints
// ============================================================================

void FinnhubController::candle(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"s","no_data"}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto resolution = req->getParameter("resolution");
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    if (from_str.empty() || to_str.empty()) {
        cb(json_resp(json{{"error","from and to required"}},400));
        return;
    }

    Timestamp from_ts = parse_date(from_str);
    Timestamp to_ts = parse_date(to_str);

    // Map resolution to multiplier and timespan
    int multiplier = 1;
    std::string timespan = "minute";
    if (resolution == "1") { multiplier = 1; timespan = "minute"; }
    else if (resolution == "5") { multiplier = 5; timespan = "minute"; }
    else if (resolution == "15") { multiplier = 15; timespan = "minute"; }
    else if (resolution == "30") { multiplier = 30; timespan = "minute"; }
    else if (resolution == "60") { multiplier = 1; timespan = "hour"; }
    else if (resolution == "D") { multiplier = 1; timespan = "day"; }
    else if (resolution == "W") { multiplier = 1; timespan = "week"; }
    else if (resolution == "M") { multiplier = 1; timespan = "month"; }

    auto bars = data_source_->get_bars(sym, from_ts, to_ts, multiplier, timespan, 5000);

    if (bars.empty()) {
        cb(json_resp(json{{"s","no_data"}}));
        return;
    }

    // Finnhub candle format
    json out;
    out["s"] = "ok";
    out["c"] = json::array();  // close
    out["h"] = json::array();  // high
    out["l"] = json::array();  // low
    out["o"] = json::array();  // open
    out["v"] = json::array();  // volume
    out["t"] = json::array();  // timestamp

    for (const auto& bar : bars) {
        out["c"].push_back(bar.close);
        out["h"].push_back(bar.high);
        out["l"].push_back(bar.low);
        out["o"].push_back(bar.open);
        out["v"].push_back(bar.volume);
        out["t"].push_back(std::chrono::duration_cast<std::chrono::seconds>(
            bar.timestamp.time_since_epoch()).count());
    }

    cb(json_resp(out));
}

// ============================================================================
// Company information endpoints
// ============================================================================

void FinnhubController::company_profile(const drogon::HttpRequestPtr& req,
                                        std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"error","no data source"}},500)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto profile = data_source_->get_company_profile(sym);
    if (!profile) {
        cb(json_resp(json{{"error","profile not found"}},404));
        return;
    }

    // Finnhub company profile format
    json out{
        {"country", profile->country},
        {"currency", profile->currency},
        {"estimateCurrency", profile->estimate_currency},
        {"exchange", profile->exchange},
        {"finnhubIndustry", profile->industry},
        {"ipo", ""}, // Would need date formatting
        {"logo", profile->logo},
        {"marketCapitalization", profile->market_capitalization},
        {"name", profile->name},
        {"phone", profile->phone},
        {"shareOutstanding", profile->share_outstanding},
        {"ticker", profile->symbol},
        {"weburl", profile->weburl}
    };
    cb(json_resp(out));
}

void FinnhubController::company_peers(const drogon::HttpRequestPtr& req,
                                      std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto peers = data_source_->get_company_peers(sym, 20);
    cb(json_resp(json(peers)));
}

void FinnhubController::basic_financials(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"metric", json::object()}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto financials = data_source_->get_basic_financials(sym);
    if (!financials) {
        cb(json_resp(json{{"metric", json::object()}, {"symbol", sym}}));
        return;
    }

    // Finnhub basic financials format
    json metric{
        {"52WeekHigh", financials->fifty_two_week_high},
        {"52WeekLow", financials->fifty_two_week_low},
        {"beta", financials->beta},
        {"dividendYieldIndicatedAnnual", financials->dividend_yield_ttm},
        {"epsBasicExclExtraItemsTTM", financials->eps_ttm},
        {"freeCashFlowPerShareTTM", financials->free_cash_flow_per_share_ttm},
        {"marketCapitalization", financials->market_capitalization},
        {"pbAnnual", financials->pb},
        {"peBasicExclExtraTTM", financials->pe_ttm},
        {"peExclExtraTTM", financials->forward_pe},
        {"revenuePerShareTTM", financials->revenue_per_share_ttm}
    };

    json out{
        {"metric", metric},
        {"metricType", "all"},
        {"symbol", sym}
    };
    cb(json_resp(out));
}

// ============================================================================
// News and sentiment endpoints
// ============================================================================

void FinnhubController::company_news(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    Timestamp from_ts = from_str.empty() ?
        std::chrono::system_clock::now() - std::chrono::hours(24 * 7) :
        parse_date(from_str);
    Timestamp to_ts = to_str.empty() ?
        std::chrono::system_clock::now() :
        parse_date(to_str);

    auto news = data_source_->get_company_news(sym, from_ts, to_ts, 50);

    json out = json::array();
    for (const auto& item : news) {
        out.push_back({
            {"category", item.category},
            {"datetime", std::chrono::duration_cast<std::chrono::seconds>(
                item.datetime.time_since_epoch()).count()},
            {"headline", item.headline},
            {"id", item.id},
            {"image", item.image},
            {"related", item.related},
            {"source", item.source},
            {"summary", item.summary},
            {"url", item.url}
        });
    }
    cb(json_resp(out));
}

void FinnhubController::market_news(const drogon::HttpRequestPtr& req,
                                    std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto category = req->getParameter("category");
    if (category.empty()) category = "general";

    // For market news, use empty symbol to get general news
    auto now = std::chrono::system_clock::now();
    auto news = data_source_->get_company_news("", now - std::chrono::hours(24), now, 50);

    json out = json::array();
    for (const auto& item : news) {
        if (category == "general" || item.category == category) {
            out.push_back({
                {"category", item.category},
                {"datetime", std::chrono::duration_cast<std::chrono::seconds>(
                    item.datetime.time_since_epoch()).count()},
                {"headline", item.headline},
                {"id", item.id},
                {"image", item.image},
                {"related", item.related},
                {"source", item.source},
                {"summary", item.summary},
                {"url", item.url}
            });
        }
    }
    cb(json_resp(out));
}

void FinnhubController::news_sentiment(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto sentiment = data_source_->get_news_sentiment(sym);
    if (!sentiment) {
        cb(json_resp(json{{"symbol", sym}, {"buzz", json::object()}, {"sentiment", json::object()}}));
        return;
    }

    json out{
        {"buzz", {
            {"articlesInLastWeek", sentiment->articles_in_last_week},
            {"buzz", sentiment->buzz},
            {"weeklyAverage", sentiment->weekly_average}
        }},
        {"companyNewsScore", sentiment->company_news_score},
        {"sectorAverageBullishPercent", sentiment->sector_average_bullish_percent},
        {"sectorAverageNewsScore", sentiment->sector_average_news_score},
        {"sentiment", {
            {"bearishPercent", sentiment->bearish_percent},
            {"bullishPercent", sentiment->bullish_percent}
        }},
        {"symbol", sym}
    };
    cb(json_resp(out));
}

// ============================================================================
// Corporate actions endpoints
// ============================================================================

void FinnhubController::dividends(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    Timestamp from_ts = from_str.empty() ?
        std::chrono::system_clock::now() - std::chrono::hours(24 * 365) :
        parse_date(from_str);
    Timestamp to_ts = to_str.empty() ?
        std::chrono::system_clock::now() + std::chrono::hours(24 * 90) :
        parse_date(to_str);

    auto divs = data_source_->get_dividends(sym, from_ts, to_ts, 100);

    json out = json::array();
    for (const auto& d : divs) {
        auto format_date = [](Timestamp ts) -> std::string {
            auto time_t = std::chrono::system_clock::to_time_t(ts);
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");
            return ss.str();
        };

        out.push_back({
            {"symbol", d.symbol},
            {"date", format_date(d.date)},
            {"amount", d.amount},
            {"adjustedAmount", d.adjusted_amount},
            {"payDate", format_date(d.pay_date)},
            {"recordDate", format_date(d.record_date)},
            {"declarationDate", format_date(d.declaration_date)},
            {"currency", d.currency}
        });
    }
    cb(json_resp(out));
}

void FinnhubController::splits(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    Timestamp from_ts = from_str.empty() ?
        std::chrono::system_clock::now() - std::chrono::hours(24 * 365 * 5) :
        parse_date(from_str);
    Timestamp to_ts = to_str.empty() ?
        std::chrono::system_clock::now() :
        parse_date(to_str);

    auto splits_data = data_source_->get_splits(sym, from_ts, to_ts, 100);

    json out = json::array();
    for (const auto& s : splits_data) {
        auto time_t = std::chrono::system_clock::to_time_t(s.date);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

        out.push_back({
            {"symbol", s.symbol},
            {"date", ss.str()},
            {"fromFactor", s.from_factor},
            {"toFactor", s.to_factor}
        });
    }
    cb(json_resp(out));
}

// ============================================================================
// Analyst data endpoints
// ============================================================================

void FinnhubController::earnings_calendar(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"earningsCalendar", json::array()}},200)); return; }

    auto sym = req->getParameter("symbol");  // Optional for calendar
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    Timestamp from_ts = from_str.empty() ?
        std::chrono::system_clock::now() - std::chrono::hours(24 * 7) :
        parse_date(from_str);
    Timestamp to_ts = to_str.empty() ?
        std::chrono::system_clock::now() + std::chrono::hours(24 * 30) :
        parse_date(to_str);

    auto earnings = data_source_->get_earnings_calendar(sym, from_ts, to_ts, 100);

    json calendar = json::array();
    for (const auto& e : earnings) {
        auto time_t = std::chrono::system_clock::to_time_t(e.date);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

        calendar.push_back({
            {"date", ss.str()},
            {"epsActual", e.eps_actual},
            {"epsEstimate", e.eps_estimate},
            {"hour", e.hour},
            {"quarter", e.quarter},
            {"revenueActual", e.revenue_actual},
            {"revenueEstimate", e.revenue_estimate},
            {"symbol", e.symbol},
            {"year", e.year}
        });
    }

    json out{{"earningsCalendar", calendar}};
    cb(json_resp(out));
}

void FinnhubController::recommendation(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto now = std::chrono::system_clock::now();
    auto recs = data_source_->get_recommendation_trends(sym, now - std::chrono::hours(24 * 365), now, 12);

    json out = json::array();
    for (const auto& r : recs) {
        auto time_t = std::chrono::system_clock::to_time_t(r.period);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

        out.push_back({
            {"buy", r.buy},
            {"hold", r.hold},
            {"period", ss.str()},
            {"sell", r.sell},
            {"strongBuy", r.strong_buy},
            {"strongSell", r.strong_sell},
            {"symbol", r.symbol}
        });
    }
    cb(json_resp(out));
}

void FinnhubController::price_target(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto target = data_source_->get_price_targets(sym);
    if (!target) {
        cb(json_resp(json{{"symbol", sym}}));
        return;
    }

    auto time_t = std::chrono::system_clock::to_time_t(target->last_updated);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

    json out{
        {"lastUpdated", ss.str()},
        {"symbol", sym},
        {"targetHigh", target->target_high},
        {"targetLow", target->target_low},
        {"targetMean", target->target_mean},
        {"targetMedian", target->target_median}
    };
    cb(json_resp(out));
}

void FinnhubController::upgrade_downgrade(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","symbol required"}},400)); return; }

    auto now = std::chrono::system_clock::now();
    auto grades = data_source_->get_upgrades_downgrades(sym, now - std::chrono::hours(24 * 365), now, 50);

    json out = json::array();
    for (const auto& g : grades) {
        auto time_t = std::chrono::system_clock::to_time_t(g.grade_time);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

        out.push_back({
            {"action", g.action},
            {"company", g.company},
            {"fromGrade", g.from_grade},
            {"gradeTime", ss.str()},
            {"symbol", g.symbol},
            {"toGrade", g.to_grade}
        });
    }
    cb(json_resp(out));
}

} // namespace broker_sim
