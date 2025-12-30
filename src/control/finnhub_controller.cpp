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
    , cfg_(cfg) {}

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

std::shared_ptr<Session> FinnhubController::get_session(const drogon::HttpRequestPtr& req) {
    auto session_id = req->getParameter("session_id");
    if (!session_id.empty()) {
        return session_mgr_->get_session(session_id);
    }
    auto sessions = session_mgr_->list_sessions();
    if (!sessions.empty()) return sessions.front();
    return nullptr;
}

Timestamp FinnhubController::current_time(const drogon::HttpRequestPtr& req) {
    auto session = get_session(req);
    if (session && session->time_engine) {
        return session->time_engine->current_time();
    }
    return std::chrono::system_clock::now();
}

std::string FinnhubController::symbol_param(const drogon::HttpRequestPtr& req) {
    return req->getParameter("symbol");
}

std::optional<Timestamp> FinnhubController::parse_date(const std::string& date_str) {
    std::tm tm = {};
    std::istringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        // Try Unix timestamp
        try {
            int64_t ts = std::stoll(date_str);
            return Timestamp{} + std::chrono::seconds(ts);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

static std::string format_date(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

static std::string format_datetime(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

static bool has_timestamp(Timestamp ts) {
    return ts.time_since_epoch().count() != 0;
}

// ============================================================================
// Company information endpoints
// ============================================================================

void FinnhubController::company_profile(const drogon::HttpRequestPtr& req,
                                        std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::object(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json::object(),200)); return; }

    auto profile = data_source_->get_company_profile(sym);
    if (!profile) {
        cb(json_resp(json::object(),200));
        return;
    }
    if (!profile->raw_json.empty()) {
        try {
            auto j = json::parse(profile->raw_json);
            cb(json_resp(j));
            return;
        } catch (...) {
        }
    }

    // Finnhub company profile format
    json out{
        {"country", profile->country},
        {"currency", profile->currency},
        {"estimateCurrency", profile->estimate_currency},
        {"exchange", profile->exchange},
        {"finnhubIndustry", profile->industry},
        {"ipo", has_timestamp(profile->ipo) ? format_date(profile->ipo) : ""},
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
    if (sym.empty()) { cb(json_resp(json::array(),200)); return; }

    auto peers = data_source_->get_company_peers(sym, 20);
    cb(json_resp(json(peers)));
}

void FinnhubController::basic_financials(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"metric", json::object()}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameters"}},422)); return; }

    auto financials = data_source_->get_basic_financials(sym);
    if (!financials) {
        cb(json_resp(json{{"metric", json::object()}, {"symbol", sym}}));
        return;
    }
    if (!financials->raw_json.empty()) {
        try {
            auto j = json::parse(financials->raw_json);
            cb(json_resp(j));
            return;
        } catch (...) {
        }
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
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameter symbol"}},422)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    if (from_str.empty() || to_str.empty()) {
        cb(json_resp(json{{"error","Wrong date format. Please use 2022-01-15 format for from and to params."}},422));
        return;
    }

    auto from_ts = parse_date(from_str);
    auto to_ts = parse_date(to_str);
    if (!from_ts || !to_ts) {
        cb(json_resp(json{{"error","Wrong date format. Please use 2022-01-15 format for from and to params."}},422));
        return;
    }

    auto now = current_time(req);
    if (*to_ts > now) *to_ts = now;
    if (*from_ts > *to_ts) {
        cb(json_resp(json::array(),200));
        return;
    }

    auto news = data_source_->get_company_news(sym, *from_ts, *to_ts, 50);

    json out = json::array();
    for (const auto& item : news) {
        if (!item.raw_json.empty()) {
            try {
                out.push_back(json::parse(item.raw_json));
                continue;
            } catch (...) {
            }
        }
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

    auto now = current_time(req);
    auto news = data_source_->get_finnhub_market_news(Timestamp{}, now, 100);

    json out = json::array();
    for (const auto& item : news) {
        auto matches_category = [&category](const json& j) {
            if (category == "general") return true;
            auto it = j.find("category");
            if (it == j.end()) return false;
            return it->is_string() && it->get<std::string>() == category;
        };
        if (!item.raw_json.empty()) {
            try {
                auto parsed = json::parse(item.raw_json);
                if (matches_category(parsed)) {
                    out.push_back(std::move(parsed));
                }
                continue;
            } catch (...) {
            }
        }
        json built{
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
        };
        if (category == "general" || item.category == category) {
            out.push_back(std::move(built));
        }
    }
    cb(json_resp(out));
}

void FinnhubController::news_sentiment(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"symbol",""}},200)); return; }

    auto sentiment = data_source_->get_news_sentiment(sym);
    if (!sentiment) {
        cb(json_resp(json{{"symbol", sym}}));
        return;
    }
    if (!sentiment->raw_json.empty()) {
        try {
            auto j = json::parse(sentiment->raw_json);
            cb(json_resp(j));
            return;
        } catch (...) {
        }
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
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameters"}},422)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");

    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (parsed) from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (parsed) to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) {
        cb(json_resp(json::array(),200));
        return;
    }

    auto divs = data_source_->get_dividends(sym, from_ts, to_ts, 100);

    json out = json::array();
    for (const auto& d : divs) {
        if (!d.raw_json.empty()) {
            try {
                out.push_back(json::parse(d.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"symbol", d.symbol},
            {"amount", d.amount},
            {"adjustedAmount", d.adjusted_amount},
            {"currency", d.currency},
            {"date", has_timestamp(d.date) ? format_date(d.date) : ""},
            {"payDate", has_timestamp(d.pay_date) ? format_date(d.pay_date) : ""},
            {"recordDate", has_timestamp(d.record_date) ? format_date(d.record_date) : ""},
            {"declarationDate", has_timestamp(d.declaration_date) ? format_date(d.declaration_date) : ""}
        };
        out.push_back(std::move(item));
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

    auto now = current_time(req);
    const bool has_from = !from_str.empty();
    const bool has_to = !to_str.empty();
    if (!sym.empty() && (!has_from || !has_to)) {
        cb(json_resp(json{{"earningsCalendar", json::array()}},200));
        return;
    }

    Timestamp from_ts = now;
    Timestamp to_ts = now + std::chrono::hours(24 * 30);
    if (has_from) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"earningsCalendar", json::array()}},200)); return; }
        from_ts = *parsed;
    }
    if (has_to) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"earningsCalendar", json::array()}},200)); return; }
        to_ts = *parsed;
    }
    if (from_ts > to_ts) { cb(json_resp(json{{"earningsCalendar", json::array()}},200)); return; }

    auto earnings = data_source_->get_earnings_calendar(sym, from_ts, to_ts, 100);

    json calendar = json::array();
    for (const auto& e : earnings) {
        calendar.push_back({
            {"date", has_timestamp(e.date) ? format_date(e.date) : ""},
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
    if (sym.empty()) { cb(json_resp(json::array(),200)); return; }

    auto now = current_time(req);
    auto recs = data_source_->get_recommendation_trends(sym, now - std::chrono::hours(24 * 365), now, 12);

    json out = json::array();
    for (const auto& r : recs) {
        out.push_back({
            {"buy", r.buy},
            {"hold", r.hold},
            {"period", has_timestamp(r.period) ? format_date(r.period) : ""},
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
    if (sym.empty()) {
        cb(json_resp(json{
            {"lastUpdated",""},
            {"numberAnalysts", 1},
            {"symbol",""},
            {"targetHigh", 0},
            {"targetLow", 0},
            {"targetMean", 0},
            {"targetMedian", 0}
        }));
        return;
    }

    auto target = data_source_->get_price_targets(sym);
    if (!target) {
        json out{
            {"lastUpdated", ""},
            {"numberAnalysts", nullptr},
            {"symbol", sym},
            {"targetHigh", nullptr},
            {"targetLow", nullptr},
            {"targetMean", nullptr},
            {"targetMedian", nullptr}
        };
        cb(json_resp(out));
        return;
    }

    json out{
        {"lastUpdated", has_timestamp(target->last_updated) ? format_datetime(target->last_updated) : ""},
        {"numberAnalysts", target->number_analysts},
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
    auto now = current_time(req);
    auto grades = data_source_->get_upgrades_downgrades(sym, now - std::chrono::hours(24 * 365), now, 50);

    json out = json::array();
    for (const auto& g : grades) {
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            g.grade_time.time_since_epoch()).count();

        out.push_back({
            {"action", g.action},
            {"company", g.company},
            {"fromGrade", g.from_grade},
            {"gradeTime", ts},
            {"symbol", g.symbol},
            {"toGrade", g.to_grade}
        });
    }
    cb(json_resp(out));
}

void FinnhubController::ipo_calendar(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"ipoCalendar", json::array()}},200)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = now;
    Timestamp to_ts = now + std::chrono::hours(24 * 30);
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"ipoCalendar", json::array()}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"ipoCalendar", json::array()}},200)); return; }
        to_ts = *parsed;
    }
    if (from_ts > to_ts) { cb(json_resp(json{{"ipoCalendar", json::array()}},200)); return; }

    auto ipos = data_source_->get_finnhub_ipo_calendar(from_ts, to_ts, 1000);
    json calendar = json::array();
    for (const auto& ipo : ipos) {
        if (!ipo.raw_json.empty()) {
            try {
                calendar.push_back(json::parse(ipo.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"date", has_timestamp(ipo.date) ? format_date(ipo.date) : ""},
            {"exchange", ipo.exchange},
            {"name", ipo.name},
            {"numberOfShares", ipo.number_of_shares ? json(*ipo.number_of_shares) : json(nullptr)},
            {"price", ipo.price_range},
            {"status", ipo.status},
            {"symbol", ipo.symbol},
            {"totalSharesValue", ipo.total_shares_value ? json(*ipo.total_shares_value) : json(nullptr)}
        };
        calendar.push_back(std::move(item));
    }
    cb(json_resp(json{{"ipoCalendar", calendar}}));
}

void FinnhubController::insider_transactions(const drogon::HttpRequestPtr& req,
                                             std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }

    auto rows = data_source_->get_finnhub_insider_transactions(sym, from_ts, to_ts, 2000);
    json data = json::array();
    for (const auto& row : rows) {
        if (!row.raw_json.empty()) {
            try {
                data.push_back(json::parse(row.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"change", row.change ? json(*row.change) : json(nullptr)},
            {"currency", ""},
            {"filingDate", has_timestamp(row.transaction_date) ? format_date(row.transaction_date) : ""},
            {"id", row.filing_id},
            {"isDerivative", nullptr},
            {"name", row.name},
            {"share", row.share ? json(*row.share) : json(nullptr)},
            {"source", ""},
            {"symbol", row.symbol},
            {"transactionCode", row.transaction_code},
            {"transactionDate", has_timestamp(row.transaction_date) ? format_date(row.transaction_date) : ""},
            {"transactionPrice", row.transaction_price ? json(*row.transaction_price) : json(nullptr)}
        };
        data.push_back(std::move(item));
    }
    cb(json_resp(json{{"data", data}, {"symbol", sym}}));
}

void FinnhubController::sec_filings(const drogon::HttpRequestPtr& req,
                                    std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json::array(),200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json::array(),200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json::array(),200)); return; }

    auto rows = data_source_->get_finnhub_sec_filings(sym, from_ts, to_ts, 250);
    json out = json::array();
    for (const auto& row : rows) {
        if (!row.raw_json.empty()) {
            try {
                out.push_back(json::parse(row.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"acceptedDate", has_timestamp(row.accepted_datetime) ? format_datetime(row.accepted_datetime) : ""},
            {"accessNumber", row.access_number},
            {"cik", ""},
            {"filedDate", has_timestamp(row.filed_date) ? format_datetime(row.filed_date) : ""},
            {"filingUrl", ""},
            {"form", row.form},
            {"reportUrl", row.report_url},
            {"symbol", row.symbol}
        };
        out.push_back(std::move(item));
    }
    cb(json_resp(out));
}

void FinnhubController::congressional_trading(const drogon::HttpRequestPtr& req,
                                              std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameter symbol"}},422)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }

    auto rows = data_source_->get_finnhub_congressional_trading(sym, from_ts, to_ts, 200);
    json data = json::array();
    for (const auto& row : rows) {
        if (!row.raw_json.empty()) {
            try {
                data.push_back(json::parse(row.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"amountFrom", row.amount_from ? json(*row.amount_from) : json(nullptr)},
            {"amountTo", row.amount_to ? json(*row.amount_to) : json(nullptr)},
            {"assetName", row.asset_name},
            {"filingDate", has_timestamp(row.filing_date) ? format_date(row.filing_date) : ""},
            {"name", row.name},
            {"ownerType", row.owner_type},
            {"position", row.position},
            {"symbol", row.symbol},
            {"transactionDate", has_timestamp(row.transaction_date) ? format_date(row.transaction_date) : ""},
            {"transactionType", row.transaction_type}
        };
        data.push_back(std::move(item));
    }
    cb(json_resp(json{{"data", data}, {"symbol", sym}}));
}

void FinnhubController::insider_sentiment(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }

    auto rows = data_source_->get_finnhub_insider_sentiment(sym, from_ts, to_ts, 200);
    json data = json::array();
    for (const auto& row : rows) {
        json item{
            {"change", row.change ? json(*row.change) : json(nullptr)},
            {"month", row.month},
            {"mspr", row.mspr ? json(*row.mspr) : json(nullptr)},
            {"symbol", row.symbol},
            {"year", row.year}
        };
        data.push_back(std::move(item));
    }
    cb(json_resp(json{{"data", data}, {"symbol", sym}}));
}

void FinnhubController::eps_estimate(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameters"}},422)); return; }
    auto freq = req->getParameter("freq");
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }

    auto rows = data_source_->get_finnhub_eps_estimates(sym, from_ts, to_ts, freq, 200);
    json data = json::array();
    for (const auto& row : rows) {
        json item{
            {"epsAvg", row.eps_avg ? json(*row.eps_avg) : json(nullptr)},
            {"epsHigh", row.eps_high ? json(*row.eps_high) : json(nullptr)},
            {"epsLow", row.eps_low ? json(*row.eps_low) : json(nullptr)},
            {"numberAnalysts", row.number_analysts ? json(*row.number_analysts) : json(nullptr)},
            {"period", has_timestamp(row.period) ? format_date(row.period) : ""},
            {"quarter", row.quarter ? json(*row.quarter) : json(nullptr)},
            {"year", row.year ? json(*row.year) : json(nullptr)}
        };
        data.push_back(std::move(item));
    }
    if (freq.empty() && !rows.empty()) freq = rows.front().freq;
    cb(json_resp(json{{"data", data}, {"symbol", sym}, {"freq", freq}}));
}

void FinnhubController::revenue_estimate(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameters"}},422)); return; }
    auto freq = req->getParameter("freq");
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}, {"freq", freq}},200)); return; }

    auto rows = data_source_->get_finnhub_revenue_estimates(sym, from_ts, to_ts, freq, 200);
    json data = json::array();
    for (const auto& row : rows) {
        json item{
            {"numberAnalysts", row.number_analysts ? json(*row.number_analysts) : json(nullptr)},
            {"period", has_timestamp(row.period) ? format_date(row.period) : ""},
            {"quarter", row.quarter ? json(*row.quarter) : json(nullptr)},
            {"revenueAvg", row.revenue_avg ? json(*row.revenue_avg) : json(nullptr)},
            {"revenueHigh", row.revenue_high ? json(*row.revenue_high) : json(nullptr)},
            {"revenueLow", row.revenue_low ? json(*row.revenue_low) : json(nullptr)},
            {"year", row.year ? json(*row.year) : json(nullptr)}
        };
        data.push_back(std::move(item));
    }
    if (freq.empty() && !rows.empty()) freq = rows.front().freq;
    cb(json_resp(json{{"data", data}, {"symbol", sym}, {"freq", freq}}));
}

void FinnhubController::earnings_history(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json::array(),200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json::array(),200)); return; }
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json::array(),200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json::array(),200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json::array(),200)); return; }

    auto rows = data_source_->get_finnhub_earnings_history(sym, from_ts, to_ts, 200);
    json out = json::array();
    for (const auto& row : rows) {
        json item{
            {"actual", row.actual ? json(*row.actual) : json(nullptr)},
            {"estimate", row.estimate ? json(*row.estimate) : json(nullptr)},
            {"period", has_timestamp(row.period) ? format_date(row.period) : ""},
            {"quarter", row.quarter ? json(*row.quarter) : json(nullptr)},
            {"surprise", row.surprise ? json(*row.surprise) : json(nullptr)},
            {"surprisePercent", row.surprise_percent ? json(*row.surprise_percent) : json(nullptr)},
            {"symbol", row.symbol},
            {"year", row.year ? json(*row.year) : json(nullptr)}
        };
        out.push_back(std::move(item));
    }
    cb(json_resp(out));
}

void FinnhubController::social_sentiment(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }

    auto rows = data_source_->get_finnhub_social_sentiment(sym, from_ts, to_ts, 200);
    json data = json::array();
    for (const auto& row : rows) {
        json item{
            {"atTime", has_timestamp(row.at_time) ? format_datetime(row.at_time) : ""},
            {"mention", row.mention ? json(*row.mention) : json(nullptr)},
            {"positiveScore", row.positive_score ? json(*row.positive_score) : json(nullptr)},
            {"negativeScore", row.negative_score ? json(*row.negative_score) : json(nullptr)},
            {"positiveMention", row.positive_mention ? json(*row.positive_mention) : json(nullptr)},
            {"negativeMention", row.negative_mention ? json(*row.negative_mention) : json(nullptr)},
            {"score", row.score ? json(*row.score) : json(nullptr)}
        };
        data.push_back(std::move(item));
    }
    cb(json_resp(json{{"data", data}, {"symbol", sym}}));
}

void FinnhubController::ownership(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"ownership", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"ownership", json::array()}, {"symbol",""}},200)); return; }

    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;

    auto rows = data_source_->get_finnhub_ownership(sym, from_ts, to_ts, 5000);
    json items = json::array();
    for (const auto& row : rows) {
        if (!row.raw_json.empty()) {
            try {
                items.push_back(json::parse(row.raw_json));
                continue;
            } catch (...) {
            }
        }
        json item{
            {"change", row.position_change ? json(*row.position_change) : json(nullptr)},
            {"filingDate", has_timestamp(row.report_date) ? format_date(row.report_date) : ""},
            {"name", row.organization},
            {"share", row.position ? json(*row.position) : json(nullptr)}
        };
        items.push_back(std::move(item));
    }
    cb(json_resp(json{{"ownership", items}, {"symbol", sym}}));
}

void FinnhubController::financials(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"financials", nullptr}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    auto freq = req->getParameter("freq");
    if (freq.empty() || (freq != "annual" && freq != "quarterly" && freq != "ttm" && freq != "ytd")) {
        cb(json_resp(json{{"error","Wrong frequency value. Only support annual, quarterly, ttm and ytd."}},422));
        return;
    }
    if (sym.empty()) {
        cb(json_resp(json{{"financials", nullptr}, {"symbol",""}},200));
        return;
    }

    auto statement = req->getParameter("statement");
    if (statement.empty()) {
        cb(json_resp(json{{"financials", nullptr}, {"symbol", sym}},200));
        return;
    }

    auto now = current_time(req);
    auto rows = data_source_->get_finnhub_financials_standardized(sym, statement, freq, Timestamp{}, now, 1000);
    json list = json::array();
    for (const auto& row : rows) {
        if (row.data_json.empty()) continue;
        try {
            list.push_back(json::parse(row.data_json));
        } catch (...) {
        }
    }
    cb(json_resp(json{{"financials", list}, {"symbol", sym}}));
}

void FinnhubController::financials_reported(const drogon::HttpRequestPtr& req,
                                            std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    if (!data_source_) { cb(json_resp(json{{"data", json::array()}, {"symbol",""}},200)); return; }

    auto sym = symbol_param(req);
    if (sym.empty()) { cb(json_resp(json{{"error","Missing parameters"}},422)); return; }
    auto freq = req->getParameter("freq");
    auto from_str = req->getParameter("from");
    auto to_str = req->getParameter("to");
    auto now = current_time(req);
    Timestamp from_ts = Timestamp{};
    Timestamp to_ts = now;
    if (!from_str.empty()) {
        auto parsed = parse_date(from_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        from_ts = *parsed;
    }
    if (!to_str.empty()) {
        auto parsed = parse_date(to_str);
        if (!parsed) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }
        to_ts = *parsed;
    }
    if (to_ts > now) to_ts = now;
    if (from_ts > to_ts) { cb(json_resp(json{{"data", json::array()}, {"symbol", sym}},200)); return; }

    auto rows = data_source_->get_finnhub_financials_reported(sym, freq, from_ts, to_ts, 1000);
    json data = json::array();
    std::string cik;
    for (const auto& row : rows) {
        if (row.data_json.empty()) continue;
        try {
            auto parsed = json::parse(row.data_json);
            if (cik.empty()) {
                auto it = parsed.find("cik");
                if (it != parsed.end() && it->is_string()) {
                    cik = it->get<std::string>();
                }
            }
            data.push_back(std::move(parsed));
        } catch (...) {
        }
    }
    cb(json_resp(json{{"cik", cik}, {"data", data}, {"symbol", sym}}));
}

} // namespace broker_sim
