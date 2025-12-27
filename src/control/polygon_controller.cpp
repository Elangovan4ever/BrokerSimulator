#include "polygon_controller.hpp"
#include <spdlog/spdlog.h>
#include <drogon/drogon.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;

namespace broker_sim {

namespace {
std::string format_et_iso(Timestamp ts) {
    auto adjusted = ts - std::chrono::hours(5);
    auto t = std::chrono::system_clock::to_time_t(adjusted);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf) + "-05:00";
}

std::string trim_copy(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}

std::string format_date(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

bool has_timestamp(Timestamp ts) {
    return ts.time_since_epoch().count() != 0;
}

std::optional<Timestamp> parse_ts_param(const std::string& value) {
    if (value.empty()) return std::nullopt;
    std::string trimmed = value;
    if (!trimmed.empty() && trimmed.back() == 'Z') {
        trimmed.pop_back();
    }
    return utils::parse_ts_any(trimmed);
}

json parse_conditions(const std::string& raw) {
    json out = json::array();
    std::string s = trim_copy(raw);
    if (s.empty()) return out;

    if (s.front() == '[' && s.back() == ']') {
        s = s.substr(1, s.size() - 2);
    }

    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim_copy(token);
        if (token.empty()) continue;
        if ((token.front() == '\'' && token.back() == '\'') ||
            (token.front() == '"' && token.back() == '"')) {
            token = token.substr(1, token.size() - 2);
            token = trim_copy(token);
        }
        if (token.empty()) continue;

        bool is_number = std::all_of(token.begin(), token.end(), [](unsigned char c) {
            return std::isdigit(c) || c == '-';
        });
        if (is_number) {
            try {
                out.push_back(std::stoi(token));
                continue;
            } catch (...) {
                // Fall through to string.
            }
        }
        out.push_back(token);
    }

    return out;
}

std::string base64_decode(const std::string& input) {
    static const std::string kChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::array<int, 256> table{};
    table.fill(-1);
    for (size_t i = 0; i < kChars.size(); ++i) {
        table[static_cast<unsigned char>(kChars[i])] = static_cast<int>(i);
    }

    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (table[c] == -1) break;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string base64_encode(const std::string& input) {
    static const char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kChars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kChars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string url_decode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out.push_back(ch);
            i += 2;
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2)
                    << static_cast<int>(c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::unordered_map<std::string, std::string> parse_query_string(const std::string& input) {
    std::unordered_map<std::string, std::string> params;
    std::stringstream ss(input);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        if (pair.empty()) continue;
        auto pos = pair.find('=');
        if (pos == std::string::npos) {
            params[url_decode(pair)] = "";
            continue;
        }
        auto key = url_decode(pair.substr(0, pos));
        auto val = url_decode(pair.substr(pos + 1));
        params[key] = val;
    }
    return params;
}

std::string build_query_string(const std::vector<std::pair<std::string, std::string>>& params) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [key, val] : params) {
        if (!first) out << '&';
        first = false;
        out << url_encode(key) << '=' << url_encode(val);
    }
    return out.str();
}
} // namespace

PolygonController::PolygonController(std::shared_ptr<SessionManager> session_mgr, const Config& cfg)
    : session_mgr_(std::move(session_mgr)), cfg_(cfg) {
    // Register event callback to cache quotes and trades
    session_mgr_->add_event_callback([this](const std::string& session_id, const Event& ev) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        int64_t ts = utils::ts_to_ns(ev.timestamp);

        if (ev.event_type == EventType::QUOTE) {
            const auto& q = std::get<QuoteData>(ev.data);
            quotes_cache_[session_id][ev.symbol] = {
                q.bid_price, static_cast<double>(q.bid_size),
                q.ask_price, static_cast<double>(q.ask_size), ts
            };
        } else if (ev.event_type == EventType::TRADE) {
            const auto& t = std::get<TradeData>(ev.data);
            trades_cache_[session_id][ev.symbol] = {
                t.price, static_cast<double>(t.size), ts, t.conditions
            };
        }
    });
}

// ============================================================================
// Helper Methods
// ============================================================================

drogon::HttpResponsePtr PolygonController::json_resp(json body, int code) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(body.dump());
    return resp;
}

drogon::HttpResponsePtr PolygonController::error_resp(const std::string& message, int code) {
    return json_resp({
        {"status", "ERROR"},
        {"request_id", utils::generate_id()},
        {"error", message}
    }, code);
}

drogon::HttpResponsePtr PolygonController::unauthorized() {
    return error_resp("Not authorized.", 401);
}

bool PolygonController::authorize(const drogon::HttpRequestPtr& req) {
    if (cfg_.auth.token.empty()) return true;

    // Check Authorization header
    auto auth = req->getHeader("authorization");
    if (!auth.empty()) {
        std::string expected = "Bearer " + cfg_.auth.token;
        if (auth == expected) return true;
    }

    // Check apiKey query param
    auto api_key = req->getParameter("apiKey");
    if (!api_key.empty() && api_key == cfg_.auth.token) return true;

    return false;
}

std::shared_ptr<Session> PolygonController::get_session(const drogon::HttpRequestPtr& req) {
    auto session_id = req->getParameter("session_id");
    if (!session_id.empty()) {
        return session_mgr_->get_session(session_id);
    }
    auto sessions = session_mgr_->list_sessions();
    if (!sessions.empty()) return sessions.front();
    return nullptr;
}

// ============================================================================
// Aggregates (Bars) Endpoints
// ============================================================================

void PolygonController::aggs(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string symbol, std::string multiplier,
                             std::string timespan, std::string from, std::string to) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    // Parse parameters
    int mult = std::stoi(multiplier);
    bool adjusted = req->getParameter("adjusted") != "false";
    std::string sort = req->getParameter("sort");
    if (sort.empty()) sort = "asc";
    int limit = 5000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::min(50000, std::stoi(limit_param));
    }

    // Build response
    json results = json::array();

    // If we have a session with data source, query it
    if (session) {
        auto data_source = session_mgr_->api_data_source();
        if (data_source) {
            // Parse from/to timestamps
            auto from_ts = utils::parse_ts_any(from);
            auto to_ts = utils::parse_ts_any(to);

            // If 'to' is date-only (YYYY-MM-DD), adjust to end of day
            // Date-only strings result in midnight, so from==to gives empty range
            if (to_ts && from_ts && *from_ts == *to_ts) {
                // Same timestamp means date-only input, add 24 hours to 'to'
                *to_ts += std::chrono::hours(24);
            } else if (to_ts && to.size() == 10 && to[4] == '-' && to[7] == '-') {
                // Date-only format detected (YYYY-MM-DD), add 24 hours
                *to_ts += std::chrono::hours(24);
            }

            if (from_ts && to_ts) {
                auto bars = data_source->get_bars(symbol, *from_ts, *to_ts, mult, timespan, limit);
                for (const auto& bar : bars) {
                    json bar_item;
                    bar_item["v"] = bar.volume;
                    bar_item["vw"] = bar.vwap;
                    bar_item["o"] = bar.open;
                    bar_item["c"] = bar.close;
                    bar_item["h"] = bar.high;
                    bar_item["l"] = bar.low;
                    bar_item["t"] = utils::ts_to_ms(bar.timestamp);
                    bar_item["n"] = bar.trade_count;
                    results.push_back(bar_item);
                }
            }
        }
    }

    json response = {
        {"ticker", symbol},
        {"count", results.size()},
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", adjusted},
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}  // Always include for Polygon API compatibility
    };

    cb(json_resp(response));
}

void PolygonController::aggsPrev(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    bool adjusted = req->getParameter("adjusted") != "false";
    json results = json::array();

    // Get previous day's bar (regular session for the last completed trading day)
    if (session && session->time_engine) {
        auto data_source = session_mgr_->api_data_source();
        if (data_source) {
            constexpr int kEtOffsetSec = 5 * 3600;  // ET offset (EST)
            auto now = session->time_engine->current_time();
            auto now_tt = std::chrono::system_clock::to_time_t(now);
            auto et_tt = now_tt - kEtOffsetSec;
            std::tm tm_et = *std::gmtime(&et_tt);
            tm_et.tm_hour = 0;
            tm_et.tm_min = 0;
            tm_et.tm_sec = 0;

            auto et_midnight_tt = timegm(&tm_et);
            for (int attempts = 0; attempts < 10; ++attempts) {
                et_midnight_tt -= 24 * 3600;
                auto day_midnight_tp = std::chrono::system_clock::from_time_t(et_midnight_tt);
                std::tm day_tm = *std::gmtime(&et_midnight_tt);
                bool weekend = (day_tm.tm_wday == 0 || day_tm.tm_wday == 6);
                bool holiday = cfg_.execution.is_market_holiday(day_midnight_tp);
                if (weekend || holiday) {
                    continue;
                }

                std::tm start_tm = day_tm;
                start_tm.tm_hour = 9;
                start_tm.tm_min = 30;
                start_tm.tm_sec = 0;
                std::tm end_tm = day_tm;
                end_tm.tm_hour = 16;
                end_tm.tm_min = 0;
                end_tm.tm_sec = 0;

                auto start_tt = timegm(&start_tm) + kEtOffsetSec;
                auto end_tt = timegm(&end_tm) + kEtOffsetSec;
                auto start = std::chrono::system_clock::from_time_t(start_tt);
                auto end = std::chrono::system_clock::from_time_t(end_tt);

                auto bars = data_source->get_bars(symbol, start, end, 1, "minute", 0);
                if (bars.empty()) {
                    continue;
                }

                double high = bars.front().high;
                double low = bars.front().low;
                double vwap_num = 0.0;
                int64_t volume = 0;
                uint64_t trade_count = 0;
                for (const auto& bar : bars) {
                    high = std::max(high, bar.high);
                    low = std::min(low, bar.low);
                    volume += bar.volume;
                    vwap_num += bar.vwap * static_cast<double>(bar.volume);
                    trade_count += bar.trade_count;
                }
                double vwap = volume > 0 ? vwap_num / static_cast<double>(volume) : 0.0;

                json bar_item;
                bar_item["v"] = volume;
                bar_item["vw"] = vwap;
                bar_item["o"] = bars.front().open;
                bar_item["c"] = bars.back().close;
                bar_item["h"] = high;
                bar_item["l"] = low;
                bar_item["t"] = utils::ts_to_ms(end);
                bar_item["n"] = trade_count;
                results.push_back(bar_item);
                break;
            }
        }
    }

    json response = {
        {"ticker", symbol},
        {"count", results.size()},
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", adjusted},
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()}
    };

    cb(json_resp(response));
}

void PolygonController::groupedDaily(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                     std::string date) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    json results = json::array();

    // Would return all ticker bars for the given date
    // For simulation, return empty or sample data

    json response = {
        {"count", results.size()},
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", true},
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()}
    };

    cb(json_resp(response));
}

// ============================================================================
// Trades Endpoints
// ============================================================================

void PolygonController::trades(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    int limit = 5000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::min(50000, std::stoi(limit_param));
    }

    std::string order = req->getParameter("order");
    if (order.empty()) order = "asc";

    json results = json::array();

    // Query data source for trades
    if (session && session->time_engine) {
        auto data_source = session_mgr_->api_data_source();
        if (data_source) {
            auto timestamp_param = req->getParameter("timestamp");
            auto timestamp_gte = req->getParameter("timestamp.gte");
            auto timestamp_gt = req->getParameter("timestamp.gt");
            auto timestamp_lte = req->getParameter("timestamp.lte");
            auto timestamp_lt = req->getParameter("timestamp.lt");

            Timestamp from_ts, to_ts;
            Timestamp current_time = session->time_engine->current_time();

            // Determine time range based on parameters
            // If no params provided, use session start to current time
            if (timestamp_gte.empty() && timestamp_gt.empty() &&
                timestamp_lte.empty() && timestamp_lt.empty() && timestamp_param.empty()) {
                from_ts = session->config.start_time;
                to_ts = current_time;
            } else {
                // Handle timestamp.gte / timestamp.gt
                if (!timestamp_gte.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_gte)) from_ts = *ts;
                } else if (!timestamp_gt.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_gt))
                        from_ts = *ts + std::chrono::nanoseconds(1);
                } else if (!timestamp_param.empty()) {
                    // Exact timestamp - query a small window around it
                    if (auto ts = utils::parse_ts_any(timestamp_param)) {
                        from_ts = *ts;
                        to_ts = *ts + std::chrono::seconds(1);
                    }
                } else {
                    from_ts = session->config.start_time;
                }

                // Handle timestamp.lte / timestamp.lt
                if (!timestamp_lte.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_lte)) to_ts = *ts;
                } else if (!timestamp_lt.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_lt))
                        to_ts = *ts - std::chrono::nanoseconds(1);
                } else if (to_ts == Timestamp{}) {
                    to_ts = current_time;
                }
            }

            if (from_ts != Timestamp{} && to_ts != Timestamp{} && from_ts <= to_ts) {
                auto trades = data_source->get_trades(symbol, from_ts, to_ts, limit);

                // Apply order (default is asc, reverse for desc)
                if (order == "desc") {
                    std::reverse(trades.begin(), trades.end());
                }

                for (const auto& t : trades) {
                    json trade_item;
                    trade_item["conditions"] = parse_conditions(t.conditions);
                    trade_item["exchange"] = t.exchange;
                    trade_item["id"] = utils::generate_id();
                    trade_item["participant_timestamp"] = utils::ts_to_ns(t.timestamp);
                    trade_item["price"] = t.price;
                    trade_item["sip_timestamp"] = utils::ts_to_ns(t.timestamp);
                    trade_item["size"] = t.size;
                    trade_item["tape"] = t.tape;
                    trade_item["sequence_number"] = utils::ts_to_ns(t.timestamp);
                    trade_item["trf_id"] = 0;
                    trade_item["trf_timestamp"] = utils::ts_to_ns(t.timestamp);
                    results.push_back(trade_item);
                }
            }
        }
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

void PolygonController::lastTrade(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                  std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = trades_cache_[session->id].find(symbol);

    if (it == trades_cache_[session->id].end()) {
        cb(error_resp("No trade data available", 404));
        return;
    }

    auto& t = it->second;
    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"T", symbol},
            {"c", parse_conditions(t.conditions)},  // conditions
            {"f", t.ts_ns},
            {"i", utils::generate_id()},
            {"p", t.price},
            {"q", 0},  // sequence number
            {"r", 0},  // tape
            {"s", static_cast<int64_t>(t.size)},
            {"t", t.ts_ns},
            {"x", 0},  // exchange
            {"y", t.ts_ns},
            {"z", 1}   // tape
        }}
    };

    cb(json_resp(response));
}

// ============================================================================
// Quotes Endpoints
// ============================================================================

void PolygonController::quotes(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    int limit = 5000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::min(50000, std::stoi(limit_param));
    }

    std::string order = req->getParameter("order");
    if (order.empty()) order = "asc";

    json results = json::array();

    // Query data source for quotes
    if (session && session->time_engine) {
        auto data_source = session_mgr_->api_data_source();
        if (data_source) {
            auto timestamp_param = req->getParameter("timestamp");
            auto timestamp_gte = req->getParameter("timestamp.gte");
            auto timestamp_gt = req->getParameter("timestamp.gt");
            auto timestamp_lte = req->getParameter("timestamp.lte");
            auto timestamp_lt = req->getParameter("timestamp.lt");

            Timestamp from_ts, to_ts;
            Timestamp current_time = session->time_engine->current_time();

            // Determine time range based on parameters
            // If no params provided, use session start to current time
            if (timestamp_gte.empty() && timestamp_gt.empty() &&
                timestamp_lte.empty() && timestamp_lt.empty() && timestamp_param.empty()) {
                from_ts = session->config.start_time;
                to_ts = current_time;
            } else {
                // Handle timestamp.gte / timestamp.gt
                if (!timestamp_gte.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_gte)) from_ts = *ts;
                } else if (!timestamp_gt.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_gt))
                        from_ts = *ts + std::chrono::nanoseconds(1);
                } else if (!timestamp_param.empty()) {
                    // Exact timestamp - query a small window around it
                    if (auto ts = utils::parse_ts_any(timestamp_param)) {
                        from_ts = *ts;
                        to_ts = *ts + std::chrono::seconds(1);
                    }
                } else {
                    from_ts = session->config.start_time;
                }

                // Handle timestamp.lte / timestamp.lt
                if (!timestamp_lte.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_lte)) to_ts = *ts;
                } else if (!timestamp_lt.empty()) {
                    if (auto ts = utils::parse_ts_any(timestamp_lt))
                        to_ts = *ts - std::chrono::nanoseconds(1);
                } else if (to_ts == Timestamp{}) {
                    to_ts = current_time;
                }
            }

            if (from_ts != Timestamp{} && to_ts != Timestamp{} && from_ts <= to_ts) {
                auto quotes = data_source->get_quotes(symbol, from_ts, to_ts, limit);

                // Apply order (default is asc, reverse for desc)
                if (order == "desc") {
                    std::reverse(quotes.begin(), quotes.end());
                }

                for (const auto& q : quotes) {
                    json quote_item;
                    quote_item["ask_exchange"] = q.ask_exchange;
                    quote_item["ask_price"] = q.ask_price;
                    quote_item["ask_size"] = q.ask_size;
                    quote_item["bid_exchange"] = q.bid_exchange;
                    quote_item["bid_price"] = q.bid_price;
                    quote_item["bid_size"] = q.bid_size;
                    quote_item["conditions"] = json::array();
                    quote_item["indicators"] = json::array();
                    quote_item["participant_timestamp"] = utils::ts_to_ns(q.timestamp);
                    quote_item["sip_timestamp"] = utils::ts_to_ns(q.timestamp);
                    quote_item["tape"] = q.tape;
                    quote_item["sequence_number"] = utils::ts_to_ns(q.timestamp);
                    results.push_back(quote_item);
                }
            }
        }
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()}
    };
    response["next_url"] = nullptr;

    cb(json_resp(response));
}

void PolygonController::lastQuote(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                  std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    // First try NBBO from matching engine
    auto nbbo = session->matching_engine->get_nbbo(symbol);
    if (nbbo) {
        json results;
        results["T"] = symbol;
        results["P"] = nbbo->ask_price;
        results["S"] = nbbo->ask_size;
        results["p"] = nbbo->bid_price;
        results["s"] = nbbo->bid_size;
        results["t"] = utils::ts_to_ms(session->time_engine->current_time());
        results["x"] = 1;  // Default exchange
        results["y"] = 1;  // Default exchange
        results["z"] = 1;
        results["X"] = 0;
        results["i"] = json::array();
        results["q"] = 0;
        results["c"] = json::array();
        results["f"] = utils::ts_to_ns(session->time_engine->current_time());

        json response;
        response["status"] = "OK";
        response["request_id"] = utils::generate_id();
        response["results"] = results;

        cb(json_resp(response));
        return;
    }

    // Fall back to cached quote
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = quotes_cache_[session->id].find(symbol);
    if (it == quotes_cache_[session->id].end()) {
        cb(error_resp("No quote data available", 404));
        return;
    }

    auto& q = it->second;
    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"T", symbol},
            {"P", q.ask_price},
            {"S", static_cast<int64_t>(q.ask_size)},
            {"p", q.bid_price},
            {"s", static_cast<int64_t>(q.bid_size)},
            {"t", q.ts_ns / 1000000},  // Convert to ms
            {"x", 0},
            {"y", 0},
            {"z", 1}
            ,{"X", 0},
            {"i", json::array()},
            {"q", 0},
            {"c", json::array()},
            {"f", q.ts_ns}
        }}
    };

    cb(json_resp(response));
}

void PolygonController::dividends(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_double_param = [&](const std::string& key, std::optional<double>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stod(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockDividendsQuery query;
    query.max_ex_dividend_date = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "ex_dividend_date";
    if (sort != "ex_dividend_date" && sort != "pay_date" &&
        sort != "declaration_date" && sort != "record_date" &&
        sort != "cash_amount" && sort != "ticker") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = sort;
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (limit_param) limit = *limit_param;
    if (has_error) return;
    if (limit < 1 || limit > 1000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }

    parse_date_param("ex_dividend_date", query.ex_dividend_date);
    parse_date_param("ex_dividend_date.gte", query.ex_dividend_date_gte);
    parse_date_param("ex_dividend_date.gt", query.ex_dividend_date_gt);
    parse_date_param("ex_dividend_date.lte", query.ex_dividend_date_lte);
    parse_date_param("ex_dividend_date.lt", query.ex_dividend_date_lt);

    parse_date_param("record_date", query.record_date);
    parse_date_param("record_date.gte", query.record_date_gte);
    parse_date_param("record_date.gt", query.record_date_gt);
    parse_date_param("record_date.lte", query.record_date_lte);
    parse_date_param("record_date.lt", query.record_date_lt);

    parse_date_param("declaration_date", query.declaration_date);
    parse_date_param("declaration_date.gte", query.declaration_date_gte);
    parse_date_param("declaration_date.gt", query.declaration_date_gt);
    parse_date_param("declaration_date.lte", query.declaration_date_lte);
    parse_date_param("declaration_date.lt", query.declaration_date_lt);

    parse_date_param("pay_date", query.pay_date);
    parse_date_param("pay_date.gte", query.pay_date_gte);
    parse_date_param("pay_date.gt", query.pay_date_gt);
    parse_date_param("pay_date.lte", query.pay_date_lte);
    parse_date_param("pay_date.lt", query.pay_date_lt);

    parse_double_param("cash_amount", query.cash_amount);
    parse_double_param("cash_amount.gte", query.cash_amount_gte);
    parse_double_param("cash_amount.gt", query.cash_amount_gt);
    parse_double_param("cash_amount.lte", query.cash_amount_lte);
    parse_double_param("cash_amount.lt", query.cash_amount_lt);

    parse_int_param("frequency", query.frequency);
    auto dividend_type = get_param("dividend_type");
    if (!dividend_type.empty()) {
        query.dividend_type = dividend_type;
        if (!has_cursor) param_values["dividend_type"] = dividend_type;
    }

    auto ticker_gte = get_param("ticker.gte");
    if (!ticker_gte.empty()) {
        query.ticker_gte = ticker_gte;
        if (!has_cursor) param_values["ticker.gte"] = ticker_gte;
    }
    auto ticker_gt = get_param("ticker.gt");
    if (!ticker_gt.empty()) {
        query.ticker_gt = ticker_gt;
        if (!has_cursor) param_values["ticker.gt"] = ticker_gt;
    }
    auto ticker_lte = get_param("ticker.lte");
    if (!ticker_lte.empty()) {
        query.ticker_lte = ticker_lte;
        if (!has_cursor) param_values["ticker.lte"] = ticker_lte;
    }
    auto ticker_lt = get_param("ticker.lt");
    if (!ticker_lt.empty()) {
        query.ticker_lt = ticker_lt;
        if (!has_cursor) param_values["ticker.lt"] = ticker_lt;
    }

    if (has_error) return;

    StockDividendsQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_dividends(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    json results = json::array();
    for (const auto& d : rows) {
        if (d.symbol.empty() || !has_timestamp(d.date)) continue;
        json item;
        item["ticker"] = d.symbol;
        item["cash_amount"] = d.amount;
        item["dividend_type"] = d.dividend_type.empty() ? "CD" : d.dividend_type;
        item["ex_dividend_date"] = format_date(d.date);
        item["frequency"] = d.frequency;
        item["id"] = d.id.empty() ? utils::generate_id() : d.id;
        if (!d.currency.empty()) item["currency"] = d.currency;
        if (has_timestamp(d.pay_date)) item["pay_date"] = format_date(d.pay_date);
        if (has_timestamp(d.record_date)) item["record_date"] = format_date(d.record_date);
        if (has_timestamp(d.declaration_date)) item["declaration_date"] = format_date(d.declaration_date);
        results.push_back(item);
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker", "ticker.gte", "ticker.gt", "ticker.lte", "ticker.lt",
            "ex_dividend_date", "ex_dividend_date.gte", "ex_dividend_date.gt",
            "ex_dividend_date.lte", "ex_dividend_date.lt",
            "record_date", "record_date.gte", "record_date.gt",
            "record_date.lte", "record_date.lt",
            "declaration_date", "declaration_date.gte", "declaration_date.gt",
            "declaration_date.lte", "declaration_date.lt",
            "pay_date", "pay_date.gte", "pay_date.gt",
            "pay_date.lte", "pay_date.lt",
            "cash_amount", "cash_amount.gte", "cash_amount.gt",
            "cash_amount.lte", "cash_amount.lt",
            "frequency", "dividend_type",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/v3/reference/dividends?cursor="
            : proto + "://" + host + "/v3/reference/dividends?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::splits(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockSplitsQuery query;
    query.max_execution_date = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "execution_date";
    if (sort != "execution_date" && sort != "ticker") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = sort;
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 1000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }

    auto ticker_gte = get_param("ticker.gte");
    if (!ticker_gte.empty()) {
        query.ticker_gte = ticker_gte;
        if (!has_cursor) param_values["ticker.gte"] = ticker_gte;
    }
    auto ticker_gt = get_param("ticker.gt");
    if (!ticker_gt.empty()) {
        query.ticker_gt = ticker_gt;
        if (!has_cursor) param_values["ticker.gt"] = ticker_gt;
    }
    auto ticker_lte = get_param("ticker.lte");
    if (!ticker_lte.empty()) {
        query.ticker_lte = ticker_lte;
        if (!has_cursor) param_values["ticker.lte"] = ticker_lte;
    }
    auto ticker_lt = get_param("ticker.lt");
    if (!ticker_lt.empty()) {
        query.ticker_lt = ticker_lt;
        if (!has_cursor) param_values["ticker.lt"] = ticker_lt;
    }

    parse_date_param("execution_date", query.execution_date);
    parse_date_param("execution_date.gte", query.execution_date_gte);
    parse_date_param("execution_date.gt", query.execution_date_gt);
    parse_date_param("execution_date.lte", query.execution_date_lte);
    parse_date_param("execution_date.lt", query.execution_date_lt);

    if (has_error) return;

    StockSplitsQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_splits(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    json results = json::array();
    for (const auto& s : rows) {
        if (s.ticker.empty() || !has_timestamp(s.execution_date)) continue;
        json item;
        item["ticker"] = s.ticker;
        item["execution_date"] = format_date(s.execution_date);
        item["split_from"] = s.split_from;
        item["split_to"] = s.split_to;
        item["id"] = s.id.empty() ? utils::generate_id() : s.id;
        results.push_back(item);
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker", "ticker.gte", "ticker.gt", "ticker.lte", "ticker.lt",
            "execution_date", "execution_date.gte", "execution_date.gt",
            "execution_date.lte", "execution_date.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/v3/reference/splits?cursor="
            : proto + "://" + host + "/v3/reference/splits?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::news(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"count", 0}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"count", 0}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_ts_filter = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = parse_ts_param(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockNewsQuery query;
    query.max_published_utc = session->time_engine->current_time();

    std::string order = get_param("order");
    if (order.empty()) order = "descending";
    if (order == "asc") order = "ascending";
    if (order == "desc") order = "descending";
    if (order != "ascending" && order != "descending") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor) param_values["order"] = order;

    std::string sort = get_param("sort");
    if (!sort.empty() && sort != "published_utc") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 1000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }

    parse_ts_filter("published_utc", query.published_utc);
    parse_ts_filter("published_utc.gte", query.published_utc_gte);
    parse_ts_filter("published_utc.gt", query.published_utc_gt);
    parse_ts_filter("published_utc.lte", query.published_utc_lte);
    parse_ts_filter("published_utc.lt", query.published_utc_lt);

    if (has_cursor) {
        auto ap = get_param("ap");
        if (!ap.empty()) {
            auto ts = parse_ts_param(ap);
            if (!ts) {
                cb(error_resp("Invalid value for cursor parameter.", 400));
                return;
            }
            query.cursor_published_utc = ts;
        }
        auto as = get_param("as");
        if (!as.empty()) query.cursor_id = as;
    }

    if (has_error) return;

    StockNewsQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_news(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    std::vector<std::string> article_ids;
    article_ids.reserve(rows.size());
    for (const auto& n : rows) {
        if (!n.id.empty()) article_ids.push_back(n.id);
    }

    auto insight_rows = data_source->get_stock_news_insights(article_ids);
    std::unordered_map<std::string, std::vector<StockNewsInsightRecord>> insights_by_article;
    for (auto& ins : insight_rows) {
        insights_by_article[ins.article_id].push_back(std::move(ins));
    }

    json results = json::array();
    for (const auto& n : rows) {
        if (n.id.empty() || !has_timestamp(n.published_utc)) continue;
        json item;
        item["id"] = n.id;
        item["title"] = n.title;
        item["author"] = n.author;
        item["article_url"] = n.article_url;
        item["amp_url"] = n.amp_url;
        item["image_url"] = n.image_url;
        item["description"] = n.description;
        item["tickers"] = n.tickers;
        item["keywords"] = n.keywords;
        item["published_utc"] = utils::ts_to_iso(n.published_utc);
        item["publisher"] = {
            {"name", n.publisher_name},
            {"homepage_url", n.publisher_homepage_url},
            {"logo_url", n.publisher_logo_url},
            {"favicon_url", n.publisher_favicon_url}
        };

        json insights = json::array();
        auto it = insights_by_article.find(n.id);
        if (it != insights_by_article.end()) {
            for (const auto& ins : it->second) {
                json ins_item;
                ins_item["ticker"] = ins.ticker;
                ins_item["sentiment"] = ins.sentiment;
                ins_item["sentiment_reasoning"] = ins.sentiment_reasoning;
                insights.push_back(std::move(ins_item));
            }
        }
        item["insights"] = insights;
        results.push_back(item);
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"count", results.size()}
    };

    if (has_more && !rows.empty()) {
        const auto& last = rows.back();
        param_values["ap"] = utils::ts_to_iso(last.published_utc);
        param_values["as"] = last.id;
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker",
            "published_utc", "published_utc.gte", "published_utc.gt",
            "published_utc.lte", "published_utc.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty()) continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/v2/reference/news?cursor="
            : proto + "://" + host + "/v2/reference/news?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::ipos(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockIposQuery query;
    query.max_date = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "listing_date";
    if (sort != "listing_date" && sort != "announced_date" &&
        sort != "issue_start_date" && sort != "issue_end_date" &&
        sort != "last_updated" && sort != "ticker" && sort != "ipo_status") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = sort;
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 1000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }
    auto ipo_status = get_param("ipo_status");
    if (!ipo_status.empty()) {
        query.ipo_status = ipo_status;
        if (!has_cursor) param_values["ipo_status"] = ipo_status;
    }

    parse_date_param("announced_date", query.announced_date);
    parse_date_param("announced_date.gte", query.announced_date_gte);
    parse_date_param("announced_date.gt", query.announced_date_gt);
    parse_date_param("announced_date.lte", query.announced_date_lte);
    parse_date_param("announced_date.lt", query.announced_date_lt);

    parse_date_param("listing_date", query.listing_date);
    parse_date_param("listing_date.gte", query.listing_date_gte);
    parse_date_param("listing_date.gt", query.listing_date_gt);
    parse_date_param("listing_date.lte", query.listing_date_lte);
    parse_date_param("listing_date.lt", query.listing_date_lt);

    parse_date_param("issue_start_date", query.issue_start_date);
    parse_date_param("issue_start_date.gte", query.issue_start_date_gte);
    parse_date_param("issue_start_date.gt", query.issue_start_date_gt);
    parse_date_param("issue_start_date.lte", query.issue_start_date_lte);
    parse_date_param("issue_start_date.lt", query.issue_start_date_lt);

    parse_date_param("issue_end_date", query.issue_end_date);
    parse_date_param("issue_end_date.gte", query.issue_end_date_gte);
    parse_date_param("issue_end_date.gt", query.issue_end_date_gt);
    parse_date_param("issue_end_date.lte", query.issue_end_date_lte);
    parse_date_param("issue_end_date.lt", query.issue_end_date_lt);

    parse_date_param("last_updated", query.last_updated);
    parse_date_param("last_updated.gte", query.last_updated_gte);
    parse_date_param("last_updated.gt", query.last_updated_gt);
    parse_date_param("last_updated.lte", query.last_updated_lte);
    parse_date_param("last_updated.lt", query.last_updated_lt);

    if (has_error) return;

    StockIposQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_ipos(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    json results = json::array();
    for (const auto& r : rows) {
        json item = json::object();
        if (!r.raw_json.empty()) {
            try {
                item = json::parse(r.raw_json);
            } catch (...) {
                item = json::object();
            }
        }

        if (!r.ticker.empty()) item["ticker"] = r.ticker;
        if (!r.ipo_status.empty()) item["ipo_status"] = r.ipo_status;
        if (r.announced_date && has_timestamp(*r.announced_date)) {
            item["announced_date"] = format_date(*r.announced_date);
        }
        if (r.listing_date && has_timestamp(*r.listing_date)) {
            item["listing_date"] = format_date(*r.listing_date);
        }
        if (r.issue_start_date && has_timestamp(*r.issue_start_date)) {
            item["issue_start_date"] = format_date(*r.issue_start_date);
        }
        if (r.issue_end_date && has_timestamp(*r.issue_end_date)) {
            item["issue_end_date"] = format_date(*r.issue_end_date);
        }
        if (r.last_updated && has_timestamp(*r.last_updated)) {
            item["last_updated"] = format_date(*r.last_updated);
        }
        results.push_back(std::move(item));
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker", "ipo_status",
            "announced_date", "announced_date.gte", "announced_date.gt",
            "announced_date.lte", "announced_date.lt",
            "listing_date", "listing_date.gte", "listing_date.gt",
            "listing_date.lte", "listing_date.lt",
            "issue_start_date", "issue_start_date.gte", "issue_start_date.gt",
            "issue_start_date.lte", "issue_start_date.lt",
            "issue_end_date", "issue_end_date.gte", "issue_end_date.gt",
            "issue_end_date.lte", "issue_end_date.lt",
            "last_updated", "last_updated.gte", "last_updated.gt",
            "last_updated.lte", "last_updated.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/vX/reference/ipos?cursor="
            : proto + "://" + host + "/vX/reference/ipos?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::shortInterest(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockShortInterestQuery query;
    query.max_settlement_date = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "settlement_date";
    if (sort != "settlement_date" && sort != "ticker") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = sort;
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 50000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }

    parse_date_param("settlement_date", query.settlement_date);
    parse_date_param("settlement_date.gte", query.settlement_date_gte);
    parse_date_param("settlement_date.gt", query.settlement_date_gt);
    parse_date_param("settlement_date.lte", query.settlement_date_lte);
    parse_date_param("settlement_date.lt", query.settlement_date_lt);

    if (has_error) return;

    StockShortInterestQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_short_interest(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    json results = json::array();
    for (const auto& r : rows) {
        json item = json::object();
        if (!r.raw_json.empty()) {
            try {
                item = json::parse(r.raw_json);
            } catch (...) {
                item = json::object();
            }
        }
        if (!r.ticker.empty()) item["ticker"] = r.ticker;
        if (has_timestamp(r.settlement_date)) {
            item["settlement_date"] = format_date(r.settlement_date);
        }
        if (r.short_interest) item["short_interest"] = *r.short_interest;
        if (r.avg_daily_volume) item["avg_daily_volume"] = *r.avg_daily_volume;
        if (r.days_to_cover) item["days_to_cover"] = *r.days_to_cover;
        results.push_back(std::move(item));
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker",
            "settlement_date", "settlement_date.gte", "settlement_date.gt",
            "settlement_date.lte", "settlement_date.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/stocks/v1/short-interest?cursor="
            : proto + "://" + host + "/stocks/v1/short-interest?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::shortVolume(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    StockShortVolumeQuery query;
    query.max_trade_date = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "date";
    if (sort != "date" && sort != "ticker") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = (sort == "date") ? "trade_date" : "ticker";
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 50000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }

    parse_date_param("date", query.trade_date);
    parse_date_param("date.gte", query.trade_date_gte);
    parse_date_param("date.gt", query.trade_date_gt);
    parse_date_param("date.lte", query.trade_date_lte);
    parse_date_param("date.lt", query.trade_date_lt);

    if (has_error) return;

    StockShortVolumeQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_short_volume(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    json results = json::array();
    for (const auto& r : rows) {
        json item = json::object();
        if (!r.raw_json.empty()) {
            try {
                item = json::parse(r.raw_json);
            } catch (...) {
                item = json::object();
            }
        }
        if (!r.ticker.empty()) item["ticker"] = r.ticker;
        if (has_timestamp(r.trade_date)) {
            item["date"] = format_date(r.trade_date);
        }
        results.push_back(std::move(item));
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker",
            "date", "date.gte", "date.gt",
            "date.lte", "date.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/stocks/v1/short-volume?cursor="
            : proto + "://" + host + "/stocks/v1/short-volume?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

void PolygonController::financials(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}, {"next_url", nullptr}}));
        return;
    }

    auto cursor = req->getParameter("cursor");
    bool has_cursor = !cursor.empty();
    std::unordered_map<std::string, std::string> param_values;
    if (has_cursor) {
        auto decoded = base64_decode(cursor);
        param_values = parse_query_string(decoded);
    }

    auto get_param = [&](const std::string& key) -> std::string {
        if (has_cursor) {
            auto it = param_values.find(key);
            return it != param_values.end() ? it->second : "";
        }
        return req->getParameter(key);
    };

    bool has_error = false;
    auto fail = [&](const std::string& message) {
        if (!has_error) {
            cb(error_resp(message, 400));
            has_error = true;
        }
    };

    auto parse_date_param = [&](const std::string& key, std::optional<Timestamp>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        auto ts = utils::parse_date(v);
        if (!ts) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        out = ts;
        if (!has_cursor) param_values[key] = v;
    };

    auto parse_int_param = [&](const std::string& key, std::optional<int>& out) {
        auto v = get_param(key);
        if (v.empty() || has_error) return;
        try {
            out = std::stoi(v);
        } catch (...) {
            fail("Invalid value for " + key + " parameter.");
            return;
        }
        if (!has_cursor) param_values[key] = v;
    };

    FinancialsQuery query;
    query.max_period_of_report_date = session->time_engine->current_time();
    query.filing_date_lte = session->time_engine->current_time();

    std::string sort = get_param("sort");
    if (sort.empty()) sort = "period_of_report_date";
    if (sort != "period_of_report_date" && sort != "filing_date") {
        cb(error_resp("Invalid value for sort parameter.", 400));
        return;
    }
    query.sort = sort;
    if (!has_cursor && !sort.empty()) param_values["sort"] = sort;

    std::string order = get_param("order");
    if (order.empty()) order = "desc";
    if (order != "asc" && order != "desc") {
        cb(error_resp("Invalid value for order parameter.", 400));
        return;
    }
    query.order = order;
    if (!has_cursor && !order.empty()) param_values["order"] = order;

    int limit = 10;
    std::optional<int> limit_param;
    parse_int_param("limit", limit_param);
    if (has_error) return;
    if (limit_param) limit = *limit_param;
    if (limit < 1 || limit > 1000) {
        cb(error_resp("Invalid value for limit parameter.", 400));
        return;
    }
    query.limit = static_cast<size_t>(limit);

    if (has_cursor) {
        std::optional<int> ap;
        parse_int_param("ap", ap);
        if (has_error) return;
        if (ap && *ap < 0) {
            cb(error_resp("Invalid value for cursor parameter.", 400));
            return;
        }
        if (ap) query.offset = static_cast<size_t>(*ap);
    }

    auto ticker = get_param("ticker");
    if (!ticker.empty()) {
        query.ticker = ticker;
        if (!has_cursor) param_values["ticker"] = ticker;
    }
    auto cik = get_param("cik");
    if (!cik.empty()) {
        query.cik = cik;
        if (!has_cursor) param_values["cik"] = cik;
    }
    auto timeframe = get_param("timeframe");
    if (!timeframe.empty()) {
        query.timeframe = timeframe;
        if (!has_cursor) param_values["timeframe"] = timeframe;
    }
    auto fiscal_period = get_param("fiscal_period");
    if (!fiscal_period.empty()) {
        query.fiscal_period = fiscal_period;
        if (!has_cursor) param_values["fiscal_period"] = fiscal_period;
    }
    std::optional<int> fiscal_year;
    parse_int_param("fiscal_year", fiscal_year);
    if (has_error) return;
    if (fiscal_year) query.fiscal_year = *fiscal_year;

    parse_date_param("period_of_report_date", query.period_of_report_date);
    parse_date_param("period_of_report_date.gte", query.period_of_report_date_gte);
    parse_date_param("period_of_report_date.gt", query.period_of_report_date_gt);
    parse_date_param("period_of_report_date.lte", query.period_of_report_date_lte);
    parse_date_param("period_of_report_date.lt", query.period_of_report_date_lt);

    parse_date_param("filing_date", query.filing_date);
    parse_date_param("filing_date.gte", query.filing_date_gte);
    parse_date_param("filing_date.gt", query.filing_date_gt);
    parse_date_param("filing_date.lte", query.filing_date_lte);
    parse_date_param("filing_date.lt", query.filing_date_lt);

    if (has_error) return;

    FinancialsQuery fetch_query = query;
    fetch_query.limit = static_cast<size_t>(limit + 1);
    auto rows = data_source->get_stock_financials(fetch_query);

    bool has_more = rows.size() > static_cast<size_t>(limit);
    if (has_more) rows.resize(limit);

    struct FieldMeta {
        const char* label;
        int order;
        const char* unit;
    };

    static const std::unordered_map<std::string, FieldMeta> kBalanceSheetMeta = {
        {"other_current_liabilities", {"Other Current Liabilities", 740, "USD"}},
        {"other_noncurrent_liabilities", {"Other Non-current Liabilities", 820, "USD"}},
        {"assets", {"Assets", 100, "USD"}},
        {"equity_attributable_to_noncontrolling_interest", {"Equity Attributable To Noncontrolling Interest", 1500, "USD"}},
        {"noncurrent_liabilities", {"Noncurrent Liabilities", 800, "USD"}},
        {"other_current_assets", {"Other Current Assets", 250, "USD"}},
        {"accounts_payable", {"Accounts Payable", 710, "USD"}},
        {"long_term_debt", {"Long-term Debt", 810, "USD"}},
        {"fixed_assets", {"Fixed Assets", 320, "USD"}},
        {"intangible_assets", {"Intangible Assets", 330, "USD"}},
        {"equity", {"Equity", 1400, "USD"}},
        {"current_assets", {"Current Assets", 200, "USD"}},
        {"equity_attributable_to_parent", {"Equity Attributable To Parent", 1600, "USD"}},
        {"current_liabilities", {"Current Liabilities", 700, "USD"}},
        {"noncurrent_assets", {"Noncurrent Assets", 300, "USD"}},
        {"liabilities_and_equity", {"Liabilities And Equity", 1900, "USD"}},
        {"inventory", {"Inventory", 230, "USD"}},
        {"liabilities", {"Liabilities", 600, "USD"}},
        {"other_noncurrent_assets", {"Other Non-current Assets", 350, "USD"}},
        {"commitments_and_contingencies", {"Commitments and Contingencies", 900, "USD"}},
    };

    static const std::unordered_map<std::string, FieldMeta> kIncomeStatementMeta = {
        {"net_income_loss_available_to_common_stockholders_basic", {"Net Income/Loss Available To Common Stockholders, Basic", 3700, "USD"}},
        {"operating_expenses", {"Operating Expenses", 1000, "USD"}},
        {"cost_of_revenue", {"Cost Of Revenue", 300, "USD"}},
        {"net_income_loss_attributable_to_parent", {"Net Income/Loss Attributable To Parent", 3500, "USD"}},
        {"income_loss_from_continuing_operations_before_tax", {"Income/Loss From Continuing Operations Before Tax", 1500, "USD"}},
        {"income_loss_before_equity_method_investments", {"Income/Loss Before Equity Method Investments", 1300, "USD"}},
        {"gross_profit", {"Gross Profit", 800, "USD"}},
        {"research_and_development", {"Research and Development", 1030, "USD"}},
        {"net_income_loss", {"Net Income/Loss", 3200, "USD"}},
        {"nonoperating_income_loss", {"Nonoperating Income/Loss", 900, "USD"}},
        {"basic_earnings_per_share", {"Basic Earnings Per Share", 4200, "USD / shares"}},
        {"diluted_earnings_per_share", {"Diluted Earnings Per Share", 4300, "USD / shares"}},
        {"revenues", {"Revenues", 100, "USD"}},
        {"income_loss_from_continuing_operations_after_tax", {"Income/Loss From Continuing Operations After Tax", 1400, "USD"}},
        {"income_tax_expense_benefit", {"Income Tax Expense/Benefit", 2200, "USD"}},
        {"operating_income_loss", {"Operating Income/Loss", 1100, "USD"}},
        {"selling_general_and_administrative_expenses", {"Selling, General, and Administrative Expenses", 1010, "USD"}},
        {"net_income_loss_attributable_to_noncontrolling_interest", {"Net Income/Loss Attributable To Noncontrolling Interest", 3300, "USD"}},
        {"costs_and_expenses", {"Costs And Expenses", 600, "USD"}},
        {"participating_securities_distributed_and_undistributed_earnings_loss_basic", {"Participating Securities, Distributed And Undistributed Earnings/Loss, Basic", 3800, "USD"}},
        {"diluted_average_shares", {"Diluted Average Shares", 4500, "shares"}},
        {"basic_average_shares", {"Basic Average Shares", 4400, "shares"}},
        {"benefits_costs_expenses", {"Benefits Costs and Expenses", 200, "USD"}},
        {"preferred_stock_dividends_and_other_adjustments", {"Preferred Stock Dividends And Other Adjustments", 3900, "USD"}},
    };

    static const std::unordered_map<std::string, FieldMeta> kCashFlowMeta = {
        {"net_cash_flow_from_operating_activities_continuing", {"Net Cash Flow From Operating Activities, Continuing", 200, "USD"}},
        {"net_cash_flow_from_investing_activities", {"Net Cash Flow From Investing Activities", 400, "USD"}},
        {"net_cash_flow_from_financing_activities", {"Net Cash Flow From Financing Activities", 700, "USD"}},
        {"net_cash_flow_from_investing_activities_continuing", {"Net Cash Flow From Investing Activities, Continuing", 500, "USD"}},
        {"net_cash_flow", {"Net Cash Flow", 1100, "USD"}},
        {"net_cash_flow_from_operating_activities", {"Net Cash Flow From Operating Activities", 100, "USD"}},
        {"net_cash_flow_from_financing_activities_continuing", {"Net Cash Flow From Financing Activities, Continuing", 800, "USD"}},
        {"net_cash_flow_continuing", {"Net Cash Flow, Continuing", 1200, "USD"}},
    };

    static const std::unordered_map<std::string, FieldMeta> kComprehensiveMeta = {
        {"comprehensive_income_loss_attributable_to_noncontrolling_interest", {"Comprehensive Income/Loss Attributable To Noncontrolling Interest", 200, "USD"}},
        {"other_comprehensive_income_loss_attributable_to_parent", {"Other Comprehensive Income/Loss Attributable To Parent", 600, "USD"}},
        {"other_comprehensive_income_loss", {"Other Comprehensive Income/Loss", 400, "USD"}},
        {"comprehensive_income_loss_attributable_to_parent", {"Comprehensive Income/Loss Attributable To Parent", 300, "USD"}},
        {"comprehensive_income_loss", {"Comprehensive Income/Loss", 100, "USD"}},
    };

    auto build_section = [](const std::unordered_map<std::string, double>& values,
                            const std::unordered_map<std::string, FieldMeta>& meta) {
        json section = json::object();
        for (const auto& [key, info] : meta) {
            auto it = values.find(key);
            if (it == values.end()) continue;
            section[key] = {
                {"value", it->second},
                {"unit", info.unit},
                {"label", info.label},
                {"order", info.order}
            };
        }
        return section;
    };

    json results = json::array();
    for (const auto& r : rows) {
        json item;
        item["cik"] = r.cik;
        item["company_name"] = r.company_name;
        item["start_date"] = has_timestamp(r.start_date) ? format_date(r.start_date) : "";
        item["end_date"] = has_timestamp(r.end_date) ? format_date(r.end_date) : "";
        item["filing_date"] = has_timestamp(r.filing_date) ? format_date(r.filing_date) : "";
        item["acceptance_datetime"] = has_timestamp(r.acceptance_datetime) ? utils::ts_to_iso(r.acceptance_datetime) : "";
        item["timeframe"] = r.timeframe;
        item["fiscal_period"] = r.fiscal_period;
        item["fiscal_year"] = r.fiscal_year;
        item["source_filing_url"] = r.source_filing_url;
        item["source_filing_file_url"] = "";
        item["sic"] = "";
        item["tickers"] = r.ticker.empty() ? json::array() : json::array({r.ticker});

        json financials = json::object();
        financials["balance_sheet"] = build_section(r.balance_sheet, kBalanceSheetMeta);
        financials["income_statement"] = build_section(r.income_statement, kIncomeStatementMeta);
        financials["cash_flow_statement"] = build_section(r.cash_flow_statement, kCashFlowMeta);
        financials["comprehensive_income"] = build_section(r.comprehensive_income, kComprehensiveMeta);
        item["financials"] = financials;

        results.push_back(std::move(item));
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    if (has_more) {
        param_values["ap"] = std::to_string(query.offset + query.limit);
        if (param_values.find("as") == param_values.end()) param_values["as"] = "";
        param_values["limit"] = std::to_string(limit);
        param_values["order"] = order;
        param_values["sort"] = sort;

        std::vector<std::string> order_keys = {
            "ap", "as",
            "ticker", "cik", "timeframe", "fiscal_period", "fiscal_year",
            "period_of_report_date", "period_of_report_date.gte", "period_of_report_date.gt",
            "period_of_report_date.lte", "period_of_report_date.lt",
            "filing_date", "filing_date.gte", "filing_date.gt",
            "filing_date.lte", "filing_date.lt",
            "limit", "order", "sort"
        };

        std::vector<std::pair<std::string, std::string>> cursor_params;
        for (const auto& key : order_keys) {
            auto it = param_values.find(key);
            if (it == param_values.end()) continue;
            if (it->second.empty() && key != "as") continue;
            cursor_params.emplace_back(key, it->second);
        }

        auto query_str = build_query_string(cursor_params);
        auto next_cursor = base64_encode(query_str);
        auto proto = req->getHeader("x-forwarded-proto");
        if (proto.empty()) proto = "http";
        auto host = req->getHeader("host");
        std::string base = host.empty()
            ? "/vX/reference/financials?cursor="
            : proto + "://" + host + "/vX/reference/financials?cursor=";
        response["next_url"] = base + next_cursor;
    }

    cb(json_resp(response));
}

// ============================================================================
// Snapshots Endpoints
// ============================================================================

void PolygonController::snapshotAll(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    json tickers = json::array();

    // Get cached data for all symbols
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (session) {
        for (const auto& [sym, q] : quotes_cache_[session->id]) {
            json ticker_data = {
                {"ticker", sym},
                {"todaysChange", 0},
                {"todaysChangePerc", 0},
                {"updated", q.ts_ns}
            };

            ticker_data["lastQuote"] = {
                {"P", q.ask_price},
                {"S", static_cast<int64_t>(q.ask_size)},
                {"p", q.bid_price},
                {"s", static_cast<int64_t>(q.bid_size)},
                {"t", q.ts_ns}
            };

            // Add day, min, prevDay for Polygon API compatibility
            ticker_data["day"] = {{"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};
            ticker_data["min"] = {{"av", 0}, {"t", 0}, {"n", 0}, {"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};
            ticker_data["prevDay"] = {{"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};

            auto trade_it = trades_cache_[session->id].find(sym);
            if (trade_it != trades_cache_[session->id].end()) {
                ticker_data["lastTrade"] = {
                    {"p", trade_it->second.price},
                    {"s", static_cast<int64_t>(trade_it->second.size)},
                    {"t", trade_it->second.ts_ns},
                    {"i", "0"},
                    {"x", 0},
                    {"c", json::array()}
                };
            }

            tickers.push_back(ticker_data);
        }
    }

    json response = {
        {"status", "OK"},
        {"count", tickers.size()},
        {"tickers", tickers},
        {"request_id", utils::generate_id()}
    };

    cb(json_resp(response));
}

void PolygonController::snapshotTicker(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                       std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);
    if (!session) { cb(error_resp("session not found", 404)); return; }

    json ticker_data = {
        {"ticker", symbol},
        {"todaysChange", 0},
        {"todaysChangePerc", 0}
    };

    // Get NBBO from matching engine
    auto nbbo = session->matching_engine->get_nbbo(symbol);
    if (nbbo) {
        ticker_data["lastQuote"] = {
            {"P", nbbo->ask_price},
            {"S", nbbo->ask_size},
            {"p", nbbo->bid_price},
            {"s", nbbo->bid_size},
            {"t", utils::ts_to_ns(session->time_engine->current_time())}
        };
    }

    // Add day, min, prevDay for Polygon API compatibility
    ticker_data["day"] = {{"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};
    ticker_data["min"] = {{"av", 0}, {"t", 0}, {"n", 0}, {"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};
    ticker_data["prevDay"] = {{"o", 0}, {"h", 0}, {"l", 0}, {"c", 0}, {"v", 0}, {"vw", 0}};

    // Get cached trade
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto trade_it = trades_cache_[session->id].find(symbol);
    if (trade_it != trades_cache_[session->id].end()) {
        ticker_data["lastTrade"] = {
            {"p", trade_it->second.price},
            {"s", static_cast<int64_t>(trade_it->second.size)},
            {"t", trade_it->second.ts_ns},
            {"i", "0"},
            {"x", 0},
            {"c", json::array()}
        };
        ticker_data["updated"] = trade_it->second.ts_ns;
    }

    json response = {
        {"status", "OK"},
        {"ticker", ticker_data},
        {"request_id", utils::generate_id()}
    };

    cb(json_resp(response));
}

void PolygonController::snapshotGainersLosers(const drogon::HttpRequestPtr& req,
                                              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                              std::string direction) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Would return top gainers or losers based on direction
    json tickers = json::array();

    json response = {
        {"status", "OK"},
        {"tickers", tickers},
        {"request_id", utils::generate_id()}
    };

    cb(json_resp(response));
}

// ============================================================================
// Ticker Details Endpoints
// ============================================================================

void PolygonController::tickerDetails(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                      std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"ticker", symbol},
            {"name", symbol + " Inc."},
            {"market", "stocks"},
            {"locale", "us"},
            {"primary_exchange", "XNAS"},
            {"type", "CS"},  // Common Stock
            {"active", true},
            {"currency_name", "usd"},
            {"cik", "0000000000"},
            {"composite_figi", "BBG000000000"},
            {"share_class_figi", "BBG000000000"},
            {"market_cap", 0},
            {"phone_number", ""},
            {"address", {}},
            {"description", "Simulated ticker for " + symbol},
            {"sic_code", "0000"},
            {"sic_description", ""},
            {"ticker_root", symbol},
            {"homepage_url", ""},
            {"total_employees", 0},
            {"list_date", "2000-01-01"},
            {"branding", {}},
            {"share_class_shares_outstanding", 0},
            {"weighted_shares_outstanding", 0},
            {"round_lot", 100}
        }}
    };

    cb(json_resp(response));
}

void PolygonController::tickersList(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Return sample tickers
    std::vector<std::string> sample_symbols = {
        "AAPL", "MSFT", "GOOGL", "AMZN", "META", "TSLA", "NVDA", "JPM", "V", "JNJ"
    };

    json results = json::array();
    for (const auto& sym : sample_symbols) {
        json ticker_item;
        ticker_item["ticker"] = sym;
        ticker_item["name"] = sym + " Inc.";
        ticker_item["market"] = "stocks";
        ticker_item["locale"] = "us";
        ticker_item["primary_exchange"] = "XNAS";
        ticker_item["type"] = "CS";
        ticker_item["active"] = true;
        ticker_item["currency_name"] = "usd";
        ticker_item["last_updated_utc"] = "2024-01-01T00:00:00Z";
        ticker_item["cik"] = "0000000000";
        ticker_item["composite_figi"] = "BBG000000000";
        ticker_item["share_class_figi"] = "BBG000000000";
        results.push_back(ticker_item);
    }

    json response = {
        {"status", "OK"},
        {"count", results.size()},
        {"results", results},
        {"request_id", utils::generate_id()},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

// ============================================================================
// Technical Indicators Endpoints
// ============================================================================

void PolygonController::sma(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Technical indicators would need historical data
    // Return placeholder for now
    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"underlying", {{"url", ""}}},
            {"values", json::array()}
        }},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

void PolygonController::ema(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"underlying", {{"url", ""}}},
            {"values", json::array()}
        }},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

void PolygonController::rsi(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"underlying", {{"url", ""}}},
            {"values", json::array()}
        }},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

void PolygonController::macd(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    json response = {
        {"status", "OK"},
        {"request_id", utils::generate_id()},
        {"results", {
            {"underlying", {{"url", ""}}},
            {"values", json::array()}
        }},
        {"next_url", nullptr}
    };

    cb(json_resp(response));
}

// ============================================================================
// Open/Close Endpoint
// ============================================================================

void PolygonController::dailyOpenClose(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                       std::string symbol, std::string date) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Would return open/close for the specific date
    // Return placeholder
    json response = {
        {"status", "OK"},
        {"symbol", symbol},
        {"from", date},
        {"open", 0},
        {"high", 0},
        {"low", 0},
        {"close", 0},
        {"volume", 0},
        {"afterHours", 0},
        {"preMarket", 0}
    };

    cb(json_resp(response));
}

// ============================================================================
// Market Status Endpoints
// ============================================================================

void PolygonController::marketStatus(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    Timestamp now;
    if (session) {
        now = session->time_engine->current_time();
    } else {
        now = std::chrono::system_clock::now();
    }

    auto session_type = cfg_.execution.get_market_session(now);
    bool early_hours = session_type == ExecutionConfig::MarketSession::PREMARKET;
    bool after_hours = session_type == ExecutionConfig::MarketSession::AFTERHOURS;
    bool is_regular = session_type == ExecutionConfig::MarketSession::REGULAR;
    bool is_extended = early_hours || after_hours;
    std::string market_status = is_regular ? "open" : (is_extended ? "extended-hours" : "closed");
    std::string exchange_status = is_regular ? "open" : (is_extended ? "extended-hours" : "closed");

    json response = {
        {"market", market_status},
        {"serverTime", format_et_iso(now)},
        {"exchanges", {
            {"nasdaq", exchange_status},
            {"nyse", exchange_status},
            {"otc", is_regular ? "open" : "closed"}
        }},
        {"currencies", {
            {"fx", "open"},
            {"crypto", "open"}
        }},
        {"earlyHours", early_hours},
        {"afterHours", after_hours},
        {"indicesGroups", {
            {"s_and_p", "open"},
            {"societe_generale", "open"},
            {"msci", "open"},
            {"ftse_russell", "open"},
            {"mstar", "open"},
            {"mstarc", "open"},
            {"cccy", "open"},
            {"nasdaq", "open"},
            {"dow_jones", "open"},
            {"cgi", "open"}
        }}
    };

    cb(json_resp(response));
}

void PolygonController::marketHolidays(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    // Return sample market holidays
    json holidays = json::array();
    holidays.push_back({
        {"exchange", "NYSE"},
        {"name", "New Years Day"},
        {"date", "2025-01-01"},
        {"status", "closed"}
    });
    holidays.push_back({
        {"exchange", "NYSE"},
        {"name", "Martin Luther King Jr. Day"},
        {"date", "2025-01-20"},
        {"status", "closed"}
    });

    cb(json_resp(holidays));
}

} // namespace broker_sim
