#include "polygon_controller.hpp"
#include <spdlog/spdlog.h>
#include <drogon/drogon.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
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
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", adjusted},
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()}
    };

    // Add next_url for pagination if needed
    if (static_cast<int>(results.size()) >= limit) {
        response["next_url"] = nullptr;  // Would contain pagination URL
    }

    cb(json_resp(response));
}

void PolygonController::aggsPrev(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 std::string symbol) {
    if (!authorize(req)) { cb(unauthorized()); return; }
    auto session = get_session(req);

    json results = json::array();

    // Get previous day's bar
    // In simulation, we might not have this data readily available
    // Return placeholder or query data source

    json response = {
        {"ticker", symbol},
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", true},
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
                    results.push_back(trade_item);
                }
            }
        }
    }

    json response = {
        {"results", results},
        {"status", "OK"},
        {"request_id", utils::generate_id()}
    };

    if (static_cast<int>(results.size()) >= limit) {
        response["next_url"] = nullptr;
    }

    cb(json_resp(response));
}

void PolygonController::ticksTrades(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                    std::string symbol, std::string date) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"results_count", 0}, {"success", true}, {"ticker", symbol}}));
        return;
    }

    int limit = 1000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::min(50000, std::stoi(limit_param));
    }
    bool reverse = req->getParameter("reverse") == "true";

    auto date_ts = utils::parse_date(date);
    if (!date_ts) {
        cb(error_resp("Invalid date format (expected YYYY-MM-DD).", 400));
        return;
    }

    Timestamp from_ts = *date_ts;
    Timestamp to_ts = *date_ts + std::chrono::hours(24);

    auto ts_param = req->getParameter("timestamp");
    if (!ts_param.empty()) {
        if (auto ts = utils::parse_ts_any(ts_param)) {
            from_ts = *ts;
        }
    }
    auto ts_limit_param = req->getParameter("timestampLimit");
    if (!ts_limit_param.empty()) {
        if (auto ts = utils::parse_ts_any(ts_limit_param)) {
            to_ts = *ts;
        }
    }

    if (from_ts > to_ts) std::swap(from_ts, to_ts);

    auto trades = data_source->get_trades(symbol, from_ts, to_ts, limit);
    if (reverse) {
        std::reverse(trades.begin(), trades.end());
    }

    json results = json::array();
    int64_t seq = 1;
    for (const auto& t : trades) {
        int64_t ts_ns = utils::ts_to_ns(t.timestamp);
        json trade_item;
        trade_item["T"] = symbol;
        trade_item["t"] = ts_ns;
        trade_item["y"] = ts_ns;
        trade_item["f"] = ts_ns;
        trade_item["q"] = seq++;
        trade_item["c"] = parse_conditions(t.conditions);
        trade_item["e"] = 0;
        trade_item["i"] = utils::generate_id();
        trade_item["p"] = t.price;
        trade_item["s"] = t.size;
        trade_item["x"] = t.exchange;
        trade_item["r"] = 0;
        trade_item["z"] = t.tape;
        results.push_back(trade_item);
    }

    json response = {
        {"results", results},
        {"results_count", results.size()},
        {"success", true},
        {"ticker", symbol},
        {"db_latency", 0}
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

    cb(json_resp(response));
}

void PolygonController::ticksQuotes(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                    std::string symbol, std::string date) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"results_count", 0}, {"success", true}, {"ticker", symbol}}));
        return;
    }

    int limit = 1000;
    auto limit_param = req->getParameter("limit");
    if (!limit_param.empty()) {
        limit = std::min(50000, std::stoi(limit_param));
    }
    bool reverse = req->getParameter("reverse") == "true";

    auto date_ts = utils::parse_date(date);
    if (!date_ts) {
        cb(error_resp("Invalid date format (expected YYYY-MM-DD).", 400));
        return;
    }

    Timestamp from_ts = *date_ts;
    Timestamp to_ts = *date_ts + std::chrono::hours(24);

    auto ts_param = req->getParameter("timestamp");
    if (!ts_param.empty()) {
        if (auto ts = utils::parse_ts_any(ts_param)) {
            from_ts = *ts;
        }
    }
    auto ts_limit_param = req->getParameter("timestampLimit");
    if (!ts_limit_param.empty()) {
        if (auto ts = utils::parse_ts_any(ts_limit_param)) {
            to_ts = *ts;
        }
    }

    if (from_ts > to_ts) std::swap(from_ts, to_ts);

    auto quotes = data_source->get_quotes(symbol, from_ts, to_ts, limit);
    if (reverse) {
        std::reverse(quotes.begin(), quotes.end());
    }

    json results = json::array();
    int64_t seq = 1;
    for (const auto& q : quotes) {
        int64_t ts_ns = utils::ts_to_ns(q.timestamp);
        json quote_item;
        quote_item["T"] = symbol;
        quote_item["t"] = ts_ns;
        quote_item["y"] = ts_ns;
        quote_item["f"] = ts_ns;
        quote_item["q"] = seq++;
        quote_item["p"] = q.bid_price;
        quote_item["s"] = q.bid_size;
        quote_item["x"] = q.bid_exchange;
        quote_item["P"] = q.ask_price;
        quote_item["S"] = q.ask_size;
        quote_item["X"] = q.ask_exchange;
        quote_item["z"] = q.tape;
        quote_item["c"] = json::array();
        quote_item["i"] = json::array();
        results.push_back(quote_item);
    }

    json response = {
        {"results", results},
        {"results_count", results.size()},
        {"success", true},
        {"ticker", symbol},
        {"db_latency", 0}
    };

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
        }}
    };

    cb(json_resp(response));
}

void PolygonController::dividends(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!authorize(req)) { cb(unauthorized()); return; }

    auto session = get_session(req);
    if (!session || !session->time_engine) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}}));
        return;
    }

    auto data_source = session_mgr_->api_data_source();
    if (!data_source) {
        cb(json_resp({{"results", json::array()}, {"status", "OK"}, {"request_id", utils::generate_id()}}));
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
        {"request_id", utils::generate_id()}
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

            auto trade_it = trades_cache_[session->id].find(sym);
            if (trade_it != trades_cache_[session->id].end()) {
                ticker_data["lastTrade"] = {
                    {"p", trade_it->second.price},
                    {"s", static_cast<int64_t>(trade_it->second.size)},
                    {"t", trade_it->second.ts_ns}
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

    // Get cached trade
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto trade_it = trades_cache_[session->id].find(symbol);
    if (trade_it != trades_cache_[session->id].end()) {
        ticker_data["lastTrade"] = {
            {"p", trade_it->second.price},
            {"s", static_cast<int64_t>(trade_it->second.size)},
            {"t", trade_it->second.ts_ns}
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
            {"weighted_shares_outstanding", 0}
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
        results.push_back(ticker_item);
    }

    json response = {
        {"status", "OK"},
        {"count", results.size()},
        {"results", results},
        {"request_id", utils::generate_id()}
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
        }}
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
        }}
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
        }}
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
        }}
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
        {"afterHours", after_hours}
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
