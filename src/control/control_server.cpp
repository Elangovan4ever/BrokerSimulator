#include "control_server.hpp"
#include "alpaca_format.hpp"
#include "../core/utils.hpp"
#include <spdlog/spdlog.h>
#include <drogon/drogon.h>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <cctype>

using json = nlohmann::json;

namespace broker_sim {

ControlServer::ControlServer(std::shared_ptr<SessionManager> session_mgr,
                             const Config& cfg)
    : session_mgr_(std::move(session_mgr))
    , cfg_(cfg) {
    session_mgr_->add_event_callback([this](const std::string& session_id, const Event& ev) {
        on_event(session_id, ev);
    });
}

drogon::HttpResponsePtr ControlServer::unauthorized() {
    return json_resp(json{{"error", "unauthorized"}}, 401);
}

drogon::HttpResponsePtr ControlServer::json_resp(json body, int code) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(body.dump());
    return resp;
}

bool ControlServer::authorize(const drogon::HttpRequestPtr& req) {
    if (cfg_.auth.token.empty()) return true;
    auto ip = req->getPeerAddr().toIp();
    if (!limiter_.allow(ip)) {
        return false;
    }
    auto auth = req->getHeader("authorization");
    std::string expected = "Bearer " + cfg_.auth.token;
    return auth == expected;
}

std::shared_ptr<Session> ControlServer::resolve_session_from_param(const drogon::HttpRequestPtr& req) {
    auto sid = req->getParameter("session_id");
    if (!sid.empty()) return session_mgr_->get_session(sid);
    auto sessions = session_mgr_->list_sessions();
    if (!sessions.empty()) return sessions.front();
    return nullptr;
}

std::string ControlServer::resolve_symbol_from_param(const drogon::HttpRequestPtr& req) {
    return req->getParameter("symbol");
}

void ControlServer::createSession(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        SessionConfig cfg;
        cfg.queue_capacity = cfg_.websocket.queue_size;
        cfg.overflow_policy = cfg_.websocket.overflow_policy;
        auto body = req->getBody();
        std::optional<std::string> requested_id;
        if (!body.empty()) {
            auto j = json::parse(body);
            cfg.symbols = j.value("symbols", std::vector<std::string>{});
            cfg.initial_capital = j.value("initial_capital", cfg_.defaults.initial_capital);
            cfg.speed_factor = j.value("speed_factor", cfg_.defaults.speed_factor);
            if (j.contains("session_id") && !j["session_id"].is_null()) {
                requested_id = j["session_id"].get<std::string>();
            }
            std::string start = j.value("start_time", "");
            std::string end = j.value("end_time", "");
            auto now = std::chrono::system_clock::now();
            cfg.start_time = now;
            cfg.end_time = now;
            if (!start.empty()) {
                std::tm tm{};
                std::istringstream ss(start);
                ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
                cfg.start_time = std::chrono::system_clock::from_time_t(timegm(&tm));
            }
            if (!end.empty()) {
                std::tm tm{};
                std::istringstream ss(end);
                ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
                cfg.end_time = std::chrono::system_clock::from_time_t(timegm(&tm));
            }
        }
        auto session = session_mgr_->create_session(cfg, requested_id);
        // Don't auto-start: preload_events blocks on ClickHouse query which can timeout the HTTP request
        // User should call POST /sessions/{id}/start separately
        callback(json_resp(json{{"session_id", session->id}, {"status", "created"}}, 201));
    } catch (const std::exception& e) {
        spdlog::error("create_session failed: {}", e.what());
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::listSessions(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    json arr = json::array();
    for (auto& s : session_mgr_->list_sessions()) {
        size_t qsize = s->event_queue ? s->event_queue->size() : 0;
        uint64_t qdrop = s->event_queue ? s->event_queue->dropped() : 0;
        arr.push_back({
            {"id", s->id},
            {"status", static_cast<int>(s->status)},
            {"cash", s->cash},
            {"equity", s->equity},
            {"queue_size", qsize},
            {"queue_dropped", qdrop},
            {"created_at", utils::ts_to_iso(s->created_at)},
            {"started_at", s->started_at ? utils::ts_to_iso(*s->started_at) : ""},
            {"start_time", utils::ts_to_iso(s->config.start_time)},
            {"end_time", utils::ts_to_iso(s->config.end_time)},
            {"speed_factor", s->time_engine->speed()},
            {"current_time", utils::ts_to_iso(s->time_engine->current_time())},
            {"symbols", s->config.symbols},
            {"initial_capital", s->config.initial_capital}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::getSession(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                               std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    json out{
        {"id", session->id},
        {"status", static_cast<int>(session->status)},
        {"created_at", utils::ts_to_iso(session->created_at)},
        {"started_at", session->started_at ? utils::ts_to_iso(*session->started_at) : ""},
        {"start_time", utils::ts_to_iso(session->config.start_time)},
        {"end_time", utils::ts_to_iso(session->config.end_time)},
        {"speed_factor", session->time_engine->speed()},
        {"current_time", utils::ts_to_iso(session->time_engine->current_time())},
        {"symbols", session->config.symbols}
    };
    callback(json_resp(out));
}

void ControlServer::deleteSession(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                  std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    session_mgr_->destroy_session(session_id);
    callback(json_resp(json{{"status","deleted"},{"session_id",session_id}}));
}

void ControlServer::stats(const drogon::HttpRequestPtr& req,
                          std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                          std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    size_t qsize = session->event_queue ? session->event_queue->size() : 0;
    uint64_t qdrop = session->event_queue ? session->event_queue->dropped() : 0;
    json out{
        {"id", session->id},
        {"status", static_cast<int>(session->status)},
        {"queue_size", qsize},
        {"queue_dropped", qdrop},
        {"last_event_ns", session->last_event_ns.load(std::memory_order_acquire)},
        {"events_enqueued", session->events_enqueued.load(std::memory_order_acquire)},
        {"events_dropped", session->events_dropped.load(std::memory_order_acquire)}
    };
    callback(json_resp(out));
}

void ControlServer::submitOrder(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        Order order;
        order.symbol = body.at("symbol").get<std::string>();
        order.side = body.value("side", "buy") == "buy" ? OrderSide::BUY : OrderSide::SELL;
        std::string type = body.value("type", "market");
        if (type == "limit") order.type = OrderType::LIMIT;
        else if (type == "stop") order.type = OrderType::STOP;
        else if (type == "stop_limit") order.type = OrderType::STOP_LIMIT;
        else order.type = OrderType::MARKET;
        std::string tif = body.value("tif", "day");
        if (tif == "ioc") order.tif = TimeInForce::IOC;
        else if (tif == "fok") order.tif = TimeInForce::FOK;
        else order.tif = TimeInForce::DAY;
        if (body.contains("qty")) order.qty = body["qty"].get<double>();
        if (body.contains("limit_price")) order.limit_price = body["limit_price"].get<double>();
        if (body.contains("stop_price")) order.stop_price = body["stop_price"].get<double>();
        auto id = session_mgr_->submit_order(session_id, order);
        if (id.empty()) {
            callback(json_resp(json{{"error", "session not found"}}, 404));
            return;
        }
        orders_submitted_.fetch_add(1, std::memory_order_relaxed);
        callback(json_resp(json{{"order_id", id}}, 201));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::listOrders(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                               std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto orders = session_mgr_->get_orders(session_id);
    json arr = json::array();
    for (auto& kv : orders) {
        const auto& o = kv.second;
        arr.push_back({
            {"id", o.id},
            {"symbol", o.symbol},
            {"side", o.side == OrderSide::BUY ? "buy" : "sell"},
            {"type", static_cast<int>(o.type)},
            {"status", static_cast<int>(o.status)},
            {"qty", o.qty.value_or(0.0)},
            {"filled_qty", o.filled_qty},
            {"limit_price", o.limit_price.value_or(0.0)},
            {"stop_price", o.stop_price.value_or(0.0)}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::account(const drogon::HttpRequestPtr& req,
                            std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                            std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto state = session->account_manager->state();
    auto positions = session->account_manager->positions();
    json pos_arr = json::array();
    for (auto& kv : positions) {
        const auto& p = kv.second;
        pos_arr.push_back({
            {"symbol", p.symbol},
            {"qty", p.qty},
            {"avg_entry_price", p.avg_entry_price},
            {"market_value", p.market_value},
            {"cost_basis", p.cost_basis},
            {"unrealized_pl", p.unrealized_pl}
        });
    }
    json out{
        {"cash", state.cash},
        {"equity", state.equity},
        {"buying_power", state.buying_power},
        {"regt_buying_power", state.regt_buying_power},
        {"daytrading_buying_power", state.daytrading_buying_power},
        {"long_market_value", state.long_market_value},
        {"short_market_value", state.short_market_value},
        {"initial_margin", state.initial_margin},
        {"maintenance_margin", state.maintenance_margin},
        {"accrued_fees", state.accrued_fees},
        {"pdt", state.pattern_day_trader},
        {"positions", pos_arr}
    };
    callback(json_resp(out));
}

void ControlServer::eventLog(const drogon::HttpRequestPtr& req,
                             std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                             std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    std::ifstream f("logs/session_" + session_id + ".events.jsonl");
    if (!f.is_open()) {
        callback(json_resp(json{{"error","log not found"}},404));
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(ss.str());
    callback(resp);
}

void ControlServer::events(const drogon::HttpRequestPtr& req,
                           std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                           std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    size_t limit = 100;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = std::stoul(lim);
    json arr = json::array();
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        auto& dq = events_[session_id];
        size_t n = std::min(limit, dq.size());
        for (size_t i = 0; i < n; ++i) {
            arr.push_back(dq.front());
            dq.pop_front();
        }
    }
    callback(json_resp(arr));
}

void ControlServer::performance(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session || !session->perf) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    size_t limit = 200;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto points = session->perf->points(limit);
    auto metrics = session->perf->metrics();
    json series = json::array();
    for (const auto& p : points) {
        auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(p.timestamp.time_since_epoch()).count();
        series.push_back({{"ts_ns", ts}, {"equity", p.equity}});
    }
    json out{
        {"series", series},
        {"metrics", {
            {"total_return", metrics.total_return},
            {"max_drawdown", metrics.max_drawdown},
            {"sharpe", metrics.sharpe}
        }}
    };
    callback(json_resp(out));
}

void ControlServer::sessionTime(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    json out{
        {"session_id", session->id},
        {"current_time", utils::ts_to_iso(session->time_engine->current_time())},
        {"current_time_ns", utils::ts_to_ns(session->time_engine->current_time())},
        {"speed_factor", session->time_engine->speed()},
        {"paused", session->time_engine->is_paused()},
        {"running", session->time_engine->is_running()}
    };
    callback(json_resp(out));
}

void ControlServer::pause(const drogon::HttpRequestPtr& req,
                          std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                          std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    session_mgr_->pause_session(session_id);
    callback(json_resp(json::object()));
}

void ControlServer::resume(const drogon::HttpRequestPtr& req,
                           std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                           std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    session_mgr_->resume_session(session_id);
    callback(json_resp(json::object()));
}

void ControlServer::setSpeed(const drogon::HttpRequestPtr& req,
                             std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                             std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        double speed = body.value("speed", 0.0);
        session_mgr_->set_speed(session_id, speed);
        callback(json_resp(json{{"speed", speed}}));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

static std::optional<Timestamp> parse_ts_iso(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return std::nullopt;
    auto t = timegm(&tm);
    return Timestamp{} + std::chrono::seconds(t);
}

static bool is_digits_only(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c){ return std::isdigit(c); });
}

static std::optional<Timestamp> parse_ts_any(const std::string& s) {
    if (s.empty()) return std::nullopt;
    if (is_digits_only(s)) {
        int64_t v = 0;
        try {
            v = std::stoll(s);
        } catch (...) {
            return std::nullopt;
        }
        if (s.size() >= 19) {
            return Timestamp{} + std::chrono::nanoseconds(v);
        }
        if (s.size() >= 16) {
            return Timestamp{} + std::chrono::microseconds(v);
        }
        if (s.size() >= 13) {
            return Timestamp{} + std::chrono::milliseconds(v);
        }
        return Timestamp{} + std::chrono::seconds(v);
    }
    if (auto iso = parse_ts_iso(s)) return iso;
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) return std::nullopt;
    auto t = timegm(&tm);
    return Timestamp{} + std::chrono::seconds(t);
}

static void resolve_time_range(const drogon::HttpRequestPtr& req,
                               const Session& session,
                               Timestamp& start,
                               Timestamp& end) {
    start = session.config.start_time;
    end = session.config.end_time;
    std::vector<std::string> start_keys = {"start", "from", "timestamp.gte", "timestamp.gt"};
    std::vector<std::string> end_keys = {"end", "to", "timestamp.lte", "timestamp.lt"};
    for (const auto& key : start_keys) {
        auto v = req->getParameter(key);
        if (!v.empty()) {
            if (auto t = parse_ts_any(v)) start = *t;
            break;
        }
    }
    for (const auto& key : end_keys) {
        auto v = req->getParameter(key);
        if (!v.empty()) {
            if (auto t = parse_ts_any(v)) end = *t;
            break;
        }
    }
    if (end < start) end = start;
}

static int64_t ts_to_ns(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();
}

static int64_t ts_to_ms(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

static int64_t ts_to_sec(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
}

static std::string format_date(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

static json parse_conditions(const std::string& raw) {
    json arr = json::array();
    if (raw.empty()) return arr;
    std::string token;
    std::stringstream ss(raw);
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        try {
            int v = std::stoi(token);
            arr.push_back(v);
        } catch (...) {
            // Preserve raw text if not numeric.
            arr.push_back(token);
        }
    }
    if (arr.empty()) arr.push_back(raw);
    return arr;
}

void ControlServer::jumpTo(const drogon::HttpRequestPtr& req,
                           std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                           std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        std::string ts = body.value("timestamp", "");
        auto parsed = parse_ts_iso(ts);
        if (!parsed) { callback(json_resp(json{{"error","invalid timestamp"}},400)); return; }
        session_mgr_->jump_to(session_id, *parsed);
        callback(json_resp(json{{"timestamp", ts}}));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::fastForward(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        std::string ts = body.value("timestamp", "");
        auto parsed = parse_ts_iso(ts);
        if (!parsed) { callback(json_resp(json{{"error","invalid timestamp"}},400)); return; }
        session_mgr_->fast_forward(session_id, *parsed);
        callback(json_resp(json{{"timestamp", ts}}));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::watermark(const drogon::HttpRequestPtr& req,
                              std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                              std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto wm = session_mgr_->watermark_ns(session_id);
    if (!wm) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    callback(json_resp(json{{"watermark_ns", *wm}}));
}

void ControlServer::stop(const drogon::HttpRequestPtr& req,
                         std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                         std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    session_mgr_->stop_session(session_id);
    callback(json_resp(json::object()));
}

void ControlServer::cancel(const drogon::HttpRequestPtr& req,
                           std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                           std::string session_id,
                           std::string order_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    bool ok = session_mgr_->cancel_order(session_id, order_id);
    if (!ok) {
        callback(json_resp(json{{"error","not found"}},404));
        return;
    }
    orders_canceled_.fetch_add(1, std::memory_order_relaxed);
    callback(json_resp(json{{"status","canceled"}}));
}

void ControlServer::applyDividend(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                  std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        auto symbol = body.value("symbol", "");
        double amount = body.value("amount_per_share", 0.0);
        if (symbol.empty() || amount == 0.0) {
            callback(json_resp(json{{"error","symbol and amount_per_share required"}},400));
            return;
        }
        if (!session_mgr_->apply_dividend(session_id, symbol, amount)) {
            callback(json_resp(json{{"error","session not found"}},404));
            return;
        }
        callback(json_resp(json{{"status","applied"},{"symbol",symbol},{"amount_per_share",amount}}));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::applySplit(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                               std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    try {
        auto body = json::parse(req->getBody());
        auto symbol = body.value("symbol", "");
        double ratio = body.value("split_ratio", 0.0);
        if (symbol.empty() || ratio <= 0.0) {
            callback(json_resp(json{{"error","symbol and split_ratio required"}},400));
            return;
        }
        if (!session_mgr_->apply_split(session_id, symbol, ratio)) {
            callback(json_resp(json{{"error","session not found"}},404));
            return;
        }
        callback(json_resp(json{{"status","applied"},{"symbol",symbol},{"split_ratio",ratio}}));
    } catch (const std::exception& e) {
        callback(json_resp(json{{"error", e.what()}}, 400));
    }
}

void ControlServer::alpacaAccount(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto st = session->account_manager->state();
    callback(json_resp(alpaca_format::format_account(st, session->id)));
}

void ControlServer::alpacaPositions(const drogon::HttpRequestPtr& req,
                                    std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto positions = session->account_manager->positions();
    json arr = json::array();
    for (auto& kv : positions) {
        const auto& p = kv.second;
        if (std::abs(p.qty) > 0.0001) {
            arr.push_back(alpaca_format::format_position(p));
        }
    }
    callback(json_resp(arr));
}

void ControlServer::alpacaPosition(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                   std::string symbol) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto positions = session->account_manager->positions();
    auto it = positions.find(symbol);
    if (it == positions.end()) { callback(json_resp(json{{"error","position not found"}},404)); return; }
    callback(json_resp(alpaca_format::format_position(it->second)));
}

void ControlServer::alpacaOrders(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto orders = session_mgr_->get_orders(session->id);
    json arr = json::array();
    for (auto& kv : orders) {
        arr.push_back(alpaca_format::format_order(kv.second));
    }
    callback(json_resp(arr));
}

void ControlServer::alpacaAssets(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    // Minimal stub: return symbols from session config as tradable assets.
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json::array())); return; }
    json arr = json::array();
    for (auto& sym : session->config.symbols) {
        arr.push_back({
            {"id", sym},
            {"class", "us_equity"},
            {"exchange", "NYSE"},
            {"symbol", sym},
            {"status", "active"},
            {"tradable", true},
            {"marginable", true},
            {"shortable", true},
            {"easy_to_borrow", true}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::alpacaClock(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto next_open = now_t;
    auto next_close = now_t + 6 * 60 * 60; // +6h stub
    json out{
        {"timestamp", now_t},
        {"is_open", true},
        {"next_open", next_open},
        {"next_close", next_close}
    };
    callback(json_resp(out));
}

void ControlServer::alpacaCalendar(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    // Stub empty calendar
    callback(json_resp(json::array()));
}

void ControlServer::alpacaActivities(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json::array())); return; }
    auto orders = session_mgr_->get_orders(session->id);
    json arr = json::array();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    for (auto& kv : orders) {
        const auto& o = kv.second;
        if (o.filled_qty <= 0.0) continue;
        json act;
        act["id"] = o.id;
        act["activity_type"] = "FILL";
        act["transaction_time"] = now;
        act["type"] = "fill";
        act["price"] = o.limit_price.value_or(0.0);
        act["qty"] = o.filled_qty;
        act["side"] = o.side == OrderSide::BUY ? "buy" : "sell";
        act["symbol"] = o.symbol;
        act["order_id"] = o.id;
        arr.push_back(act);
    }
    callback(json_resp(arr));
}

void ControlServer::alpacaPortfolioHistory(const drogon::HttpRequestPtr& req,
                                           std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    // Minimal stub with current equity/cash as today's value and yesterday baseline.
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto yday_t = std::chrono::system_clock::to_time_t(now - std::chrono::hours(24));
    json out;
    out["timestamp"] = json::array({yday_t, now_t});
    out["equity"] = json::array({session->cash, session->equity});
    out["profit_loss"] = json::array({0.0, session->equity - session->cash});
    double pl_pct = session->cash > 0 ? (session->equity - session->cash) / session->cash : 0.0;
    out["profit_loss_pct"] = json::array({0.0, pl_pct});
    out["base_value"] = session->cash;
    out["timeframe"] = "1D";
    callback(json_resp(out));
}

void ControlServer::alpacaBars(const drogon::HttpRequestPtr& req,
                               std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    Timestamp start, end;
    resolve_time_range(req, *session, start, end);
    auto timeframe = req->getParameter("timeframe");
    if (timeframe.empty()) timeframe = "1Min";
    int multiplier = 1;
    std::string timespan = "minute";
    try {
        if (timeframe.find("Min") != std::string::npos) {
            multiplier = std::stoi(timeframe.substr(0, timeframe.find("Min")));
            timespan = "minute";
        } else if (timeframe.find("Hour") != std::string::npos) {
            multiplier = std::stoi(timeframe.substr(0, timeframe.find("Hour")));
            timespan = "hour";
        } else if (timeframe.find("Day") != std::string::npos) {
            multiplier = std::stoi(timeframe.substr(0, timeframe.find("Day")));
            timespan = "day";
        }
    } catch (...) {
        multiplier = 1;
        timespan = "minute";
    }
    size_t limit = 100;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto bars = ds->get_bars(symbol, start, end, multiplier, timespan, limit);
    json arr = json::array();
    for (const auto& b : bars) {
        arr.push_back({
            {"t", ts_to_sec(b.timestamp)},
            {"o", b.open},
            {"h", b.high},
            {"l", b.low},
            {"c", b.close},
            {"v", b.volume},
            {"vw", b.vwap},
            {"n", b.trade_count}
        });
    }
    json out{
        {"bars", arr},
        {"symbol", symbol},
        {"next_page_token", nullptr}
    };
    callback(json_resp(out));
}


void ControlServer::polygonSnapshot(const drogon::HttpRequestPtr& req,
                                    std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto symbol = resolve_symbol_from_param(req);
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto nbbo = session->matching_engine->get_nbbo(symbol);
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto t_it = last_trades_[session->id].find(symbol);
    json out;
    out["symbol"] = symbol;
    if (nbbo) {
        out["bid"] = {{"price", nbbo->bid_price}, {"size", nbbo->bid_size}};
        out["ask"] = {{"price", nbbo->ask_price}, {"size", nbbo->ask_size}};
    }
    if (t_it != last_trades_[session->id].end()) {
        out["last_trade"] = {{"price", t_it->second.price}, {"size", t_it->second.size}, {"timestamp_ns", t_it->second.ts_ns}};
    }
    callback(json_resp(out));
}

void ControlServer::polygonLastQuote(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto symbol = resolve_symbol_from_param(req);
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto it = last_quotes_[session->id].find(symbol);
    if (it == last_quotes_[session->id].end()) {
        callback(json_resp(json{{"error","quote not found"}},404));
        return;
    }
    auto q = it->second;
    json out{
        {"symbol", symbol},
        {"bid_price", q.bid_price},
        {"bid_size", q.bid_size},
        {"ask_price", q.ask_price},
        {"ask_size", q.ask_size},
        {"timestamp_ns", q.ts_ns}
    };
    callback(json_resp(out));
}

void ControlServer::polygonLastTrade(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto symbol = resolve_symbol_from_param(req);
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto it = last_trades_[session->id].find(symbol);
    if (it == last_trades_[session->id].end()) {
        callback(json_resp(json{{"error","trade not found"}},404));
        return;
    }
    auto t = it->second;
    json out{
        {"symbol", symbol},
        {"price", t.price},
        {"size", t.size},
        {"timestamp_ns", t.ts_ns}
    };
    callback(json_resp(out));
}

void ControlServer::polygonAggs(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string ticker, std::string multiplier, std::string timespan,
                                std::string from, std::string to) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    bool path_range = false;
    if (!from.empty()) {
        if (auto t = parse_ts_any(from)) { start = *t; path_range = true; }
    }
    if (!to.empty()) {
        if (auto t = parse_ts_any(to)) { end = *t; path_range = true; }
    }
    if (!path_range) {
        resolve_time_range(req, *session, start, end);
    }
    int mult = 1;
    try { mult = std::max(1, std::stoi(multiplier)); } catch (...) { mult = 1; }
    size_t limit = 5000;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto bars = ds->get_bars(ticker, start, end, mult, timespan, limit);
    json results = json::array();
    for (const auto& b : bars) {
        results.push_back({
            {"v", b.volume},
            {"vw", b.vwap},
            {"o", b.open},
            {"c", b.close},
            {"h", b.high},
            {"l", b.low},
            {"t", ts_to_ms(b.timestamp)},
            {"n", b.trade_count}
        });
    }
    json out{
        {"ticker", ticker},
        {"queryCount", results.size()},
        {"resultsCount", results.size()},
        {"adjusted", true},
        {"status", "OK"},
        {"results", results}
    };
    callback(json_resp(out));
}

void ControlServer::polygonPrev(const drogon::HttpRequestPtr& req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                std::string ticker) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    polygonAggs(req, std::move(callback), ticker, "1", "day", "", "");
}

void ControlServer::polygonTrades(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                  std::string ticker) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    Timestamp start, end;
    resolve_time_range(req, *session, start, end);
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto trades = ds->get_trades(ticker, start, end, limit);
    json results = json::array();
    size_t seq = 0;
    for (const auto& t : trades) {
        int64_t ts_ns = ts_to_ns(t.timestamp);
        results.push_back({
            {"conditions", parse_conditions(t.conditions)},
            {"exchange", t.exchange},
            {"id", std::to_string(seq)},
            {"participant_timestamp", ts_ns},
            {"price", t.price},
            {"sequence_number", static_cast<int64_t>(seq)},
            {"sip_timestamp", ts_ns},
            {"size", t.size},
            {"tape", t.tape}
        });
        ++seq;
    }
    json out{
        {"results", results},
        {"results_count", results.size()},
        {"request_id", "sim"}
    };
    callback(json_resp(out));
}

void ControlServer::polygonQuotes(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                  std::string ticker) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    Timestamp start, end;
    resolve_time_range(req, *session, start, end);
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto quotes = ds->get_quotes(ticker, start, end, limit);
    json results = json::array();
    size_t seq = 0;
    for (const auto& q : quotes) {
        int64_t ts_ns = ts_to_ns(q.timestamp);
        results.push_back({
            {"ask_exchange", q.ask_exchange},
            {"ask_price", q.ask_price},
            {"ask_size", q.ask_size},
            {"bid_exchange", q.bid_exchange},
            {"bid_price", q.bid_price},
            {"bid_size", q.bid_size},
            {"indicators", json::array()},
            {"participant_timestamp", ts_ns},
            {"sequence_number", static_cast<int64_t>(seq)},
            {"sip_timestamp", ts_ns},
            {"tape", q.tape}
        });
        ++seq;
    }
    json out{
        {"results", results},
        {"results_count", results.size()},
        {"request_id", "sim"}
    };
    callback(json_resp(out));
}

void ControlServer::polygonSnapshotAll(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto sessions = session_mgr_->list_sessions();
    if (sessions.empty()) { callback(json_resp(json{{"tickers", json::array()}})); return; }
    auto session = sessions.front();
    json arr = json::array();
    std::lock_guard<std::mutex> lock(last_mutex_);
    for (auto& kv : last_quotes_[session->id]) {
        const auto& sym = kv.first;
        const auto& q = kv.second;
        json snap;
        snap["ticker"] = sym;
        snap["lastQuote"] = {{"P", q.ask_price}, {"S", q.ask_size}, {"p", q.bid_price}, {"s", q.bid_size}};
        auto tit = last_trades_[session->id].find(sym);
        if (tit != last_trades_[session->id].end()) {
            snap["lastTrade"] = {{"p", tit->second.price}, {"s", tit->second.size}, {"t", tit->second.ts_ns}};
        }
        arr.push_back(snap);
    }
    callback(json_resp(json{{"tickers", arr}}));
}

void ControlServer::finnhubQuote(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto symbol = resolve_symbol_from_param(req);
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    std::lock_guard<std::mutex> lock(last_mutex_);
    auto it = last_quotes_[session->id].find(symbol);
    if (it == last_quotes_[session->id].end()) {
        callback(json_resp(json{{"error","quote not found"}},404));
        return;
    }
    auto q = it->second;
    json out{
        {"c", q.ask_price},
        {"b", q.bid_price},
        {"a", q.ask_price},
        {"t", q.ts_ns / 1000000000},
        {"dp", 0},
        {"pc", q.bid_price}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubTrades(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto symbol = resolve_symbol_from_param(req);
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    Timestamp start, end;
    resolve_time_range(req, *session, start, end);
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto trades = ds->get_trades(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& t : trades) {
        arr.push_back({
            {"p", t.price},
            {"s", symbol},
            {"t", ts_to_ms(t.timestamp)},
            {"v", t.size}
        });
    }
    json out{
        {"data", arr},
        {"type", "trade"}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubCandles(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto res = req->getParameter("resolution");
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    if (res.empty() || from.empty() || to.empty()) {
        callback(json_resp(json{{"error","resolution, from, to required"}},400));
        return;
    }
    int multiplier = 1;
    std::string timespan = "minute";
    if (res == "D") {
        timespan = "day";
    } else if (res == "W") {
        timespan = "week";
    } else if (res == "M") {
        timespan = "month";
    } else {
        try { multiplier = std::max(1, std::stoi(res)); } catch (...) { multiplier = 1; }
        timespan = "minute";
    }
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    auto bars = ds->get_bars(symbol, start, end, multiplier, timespan, 0);
    if (bars.empty()) {
        callback(json_resp(json{{"s","no_data"}}));
        return;
    }
    json c = json::array();
    json h = json::array();
    json l = json::array();
    json o = json::array();
    json v = json::array();
    json t = json::array();
    for (const auto& b : bars) {
        c.push_back(b.close);
        h.push_back(b.high);
        l.push_back(b.low);
        o.push_back(b.open);
        v.push_back(b.volume);
        t.push_back(ts_to_sec(b.timestamp));
    }
    json out{
        {"c", c},
        {"h", h},
        {"l", l},
        {"o", o},
        {"v", v},
        {"t", t},
        {"s", "ok"}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubProfile(const drogon::HttpRequestPtr& req,
                                   std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto profile = ds->get_company_profile(symbol);
    if (!profile) {
        json out{
            {"ticker", symbol},
            {"name", symbol + " Inc"},
            {"exchange", "NASDAQ"},
            {"currency", "USD"},
            {"country", "US"}
        };
        callback(json_resp(out));
        return;
    }
    if (!profile->raw_json.empty()) {
        try {
            auto j = json::parse(profile->raw_json);
            callback(json_resp(j));
            return;
        } catch (...) {
        }
    }
    json out{
        {"ticker", profile->symbol},
        {"name", profile->name},
        {"exchange", profile->exchange},
        {"currency", profile->currency},
        {"country", profile->country},
        {"industry", profile->industry},
        {"ipo", format_date(profile->ipo)},
        {"marketCapitalization", profile->market_capitalization},
        {"shareOutstanding", profile->share_outstanding},
        {"weburl", profile->weburl},
        {"logo", profile->logo},
        {"phone", profile->phone}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubPeers(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json::array())); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json::array())); return; }
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto peers = ds->get_company_peers(symbol, limit);
    callback(json_resp(json(peers)));
}

void ControlServer::finnhubCompanyNews(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json::array())); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto news = ds->get_company_news(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& n : news) {
        arr.push_back({
            {"category", n.category},
            {"datetime", ts_to_sec(n.datetime)},
            {"headline", n.headline},
            {"id", n.id},
            {"image", n.image},
            {"related", n.related},
            {"source", n.source},
            {"summary", n.summary},
            {"url", n.url}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::finnhubNewsSentiment(const drogon::HttpRequestPtr& req,
                                         std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto sentiment = ds->get_news_sentiment(symbol);
    if (!sentiment) {
        json out{{"buzz", {{"articlesInLastWeek",0}, {"weeklyAverage",0}}},
                 {"companyNewsScore", 0.0}};
        callback(json_resp(out));
        return;
    }
    if (!sentiment->raw_json.empty()) {
        try {
            auto j = json::parse(sentiment->raw_json);
            callback(json_resp(j));
            return;
        } catch (...) {
        }
    }
    json out{
        {"buzz", {{"articlesInLastWeek", sentiment->articles_in_last_week},
                  {"weeklyAverage", sentiment->weekly_average},
                  {"buzz", sentiment->buzz}}},
        {"companyNewsScore", sentiment->company_news_score},
        {"sectorAverageBullishPercent", sentiment->sector_average_bullish_percent},
        {"sectorAverageNewsScore", sentiment->sector_average_news_score},
        {"sentiment", {{"bullishPercent", sentiment->bullish_percent},
                       {"bearishPercent", sentiment->bearish_percent}}}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubMetric(const drogon::HttpRequestPtr& req,
                                  std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto basics = ds->get_basic_financials(symbol);
    if (!basics) {
        json out{{"metric", {{"peBasicExclExtraTTM", 0.0}, {"psTTM", 0.0}}}};
        callback(json_resp(out));
        return;
    }
    if (!basics->raw_json.empty()) {
        try {
            auto j = json::parse(basics->raw_json);
            callback(json_resp(j));
            return;
        } catch (...) {
        }
    }
    json metric{
        {"marketCapitalization", basics->market_capitalization},
        {"peTTM", basics->pe_ttm},
        {"forwardPE", basics->forward_pe},
        {"pb", basics->pb},
        {"dividendYieldTTM", basics->dividend_yield_ttm},
        {"revenuePerShareTTM", basics->revenue_per_share_ttm},
        {"epsTTM", basics->eps_ttm},
        {"freeCashFlowPerShareTTM", basics->free_cash_flow_per_share_ttm},
        {"beta", basics->beta},
        {"52WeekHigh", basics->fifty_two_week_high},
        {"52WeekLow", basics->fifty_two_week_low}
    };
    callback(json_resp(json{{"metric", metric}}));
}

void ControlServer::finnhubEarningsCalendar(const drogon::HttpRequestPtr& req,
                                            std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto rows = ds->get_earnings_calendar(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& e : rows) {
        arr.push_back({
            {"symbol", e.symbol},
            {"date", format_date(e.date)},
            {"quarter", e.quarter},
            {"year", e.year},
            {"epsEstimate", e.eps_estimate},
            {"epsActual", e.eps_actual},
            {"revenueEstimate", e.revenue_estimate},
            {"revenueActual", e.revenue_actual},
            {"hour", e.hour}
        });
    }
    callback(json_resp(json{{"earningsCalendar", arr}}));
}

void ControlServer::finnhubRecommendation(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto rows = ds->get_recommendation_trends(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& r : rows) {
        arr.push_back({
            {"symbol", r.symbol},
            {"period", format_date(r.period)},
            {"strongBuy", r.strong_buy},
            {"buy", r.buy},
            {"hold", r.hold},
            {"sell", r.sell},
            {"strongSell", r.strong_sell}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::finnhubPriceTarget(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto row = ds->get_price_targets(symbol);
    if (!row) { callback(json_resp(json::object())); return; }
    json out{
        {"symbol", row->symbol},
        {"lastUpdated", format_date(row->last_updated)},
        {"numberAnalysts", row->number_analysts},
        {"targetHigh", row->target_high},
        {"targetLow", row->target_low},
        {"targetMean", row->target_mean},
        {"targetMedian", row->target_median}
    };
    callback(json_resp(out));
}

void ControlServer::finnhubUpgradeDowngrade(const drogon::HttpRequestPtr& req,
                                            std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json{{"error","data source unavailable"}},500)); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 50;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto rows = ds->get_upgrades_downgrades(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& u : rows) {
        arr.push_back({
            {"symbol", u.symbol},
            {"gradeTime", ts_to_sec(u.grade_time)},
            {"company", u.company},
            {"fromGrade", u.from_grade},
            {"toGrade", u.to_grade},
            {"action", u.action}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::finnhubDividend(const drogon::HttpRequestPtr& req,
                                    std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json::array())); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 100;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto rows = ds->get_dividends(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& d : rows) {
        arr.push_back({
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
    callback(json_resp(arr));
}

void ControlServer::finnhubSplit(const drogon::HttpRequestPtr& req,
                                 std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto symbol = req->getParameter("symbol");
    if (symbol.empty()) { callback(json_resp(json{{"error","symbol required"}},400)); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto ds = session_mgr_->data_source();
    if (!ds) { callback(json_resp(json::array())); return; }
    auto from = req->getParameter("from");
    auto to = req->getParameter("to");
    Timestamp start = session->config.start_time;
    Timestamp end = session->config.end_time;
    if (auto t = parse_ts_any(from)) start = *t;
    if (auto t = parse_ts_any(to)) end = *t;
    size_t limit = 100;
    auto lim = req->getParameter("limit");
    if (!lim.empty()) limit = static_cast<size_t>(std::stoul(lim));
    auto rows = ds->get_splits(symbol, start, end, limit);
    json arr = json::array();
    for (const auto& s : rows) {
        arr.push_back({
            {"symbol", s.symbol},
            {"date", format_date(s.date)},
            {"fromFactor", s.from_factor},
            {"toFactor", s.to_factor}
        });
    }
    callback(json_resp(arr));
}

void ControlServer::health(const drogon::HttpRequestPtr&,
                           std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    json out{
        {"status", "ok"},
        {"sessions", session_mgr_->list_sessions().size()},
        {"events_published", events_published_.load()},
        {"orders_submitted", orders_submitted_.load()},
        {"orders_canceled", orders_canceled_.load()},
        {"event_queue_dropped", 0}
    };
    uint64_t total_drop = 0;
    for (auto& s : session_mgr_->list_sessions()) {
        if (s->event_queue) total_drop += s->event_queue->dropped();
    }
    out["event_queue_dropped"] = total_drop;
    callback(json_resp(out));
}

void ControlServer::on_event(const std::string& session_id, const Event& ev) {
    auto j = event_to_json(ev);
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        auto& dq = events_[session_id];
        dq.push_back(j);
        if (dq.size() > max_events_per_session_) dq.pop_front();
        events_published_.fetch_add(1, std::memory_order_relaxed);
    }

    {
        std::lock_guard<std::mutex> lock2(last_mutex_);
        if (ev.event_type == EventType::QUOTE) {
            const auto& q = std::get<QuoteData>(ev.data);
            int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count();
            last_quotes_[session_id][ev.symbol] = {q.bid_price, static_cast<double>(q.bid_size), q.ask_price, static_cast<double>(q.ask_size), ts};
        } else if (ev.event_type == EventType::TRADE) {
            const auto& t = std::get<TradeData>(ev.data);
            int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count();
            last_trades_[session_id][ev.symbol] = {t.price, static_cast<double>(t.size), ts, t.exchange, t.conditions};
        }
    }

    WsController::broadcast_to_session(session_id, j.dump());
}

json ControlServer::event_to_json(const Event& ev) {
    json out;
    out["symbol"] = ev.symbol;
    out["event_type"] = static_cast<int>(ev.event_type);
    out["sequence"] = ev.sequence;
    auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count();
    out["timestamp_ns"] = ts;
    if (ev.event_type == EventType::QUOTE) {
        const auto& q = std::get<QuoteData>(ev.data);
        out["bid_price"] = q.bid_price;
        out["bid_size"] = q.bid_size;
        out["ask_price"] = q.ask_price;
        out["ask_size"] = q.ask_size;
        out["bid_exchange"] = q.bid_exchange;
        out["ask_exchange"] = q.ask_exchange;
    } else if (ev.event_type == EventType::TRADE) {
        const auto& t = std::get<TradeData>(ev.data);
        out["price"] = t.price;
        out["size"] = t.size;
        out["exchange"] = t.exchange;
        out["conditions"] = t.conditions;
    } else if (ev.event_type == EventType::ORDER_NEW ||
               ev.event_type == EventType::ORDER_FILL ||
               ev.event_type == EventType::ORDER_CANCEL ||
               ev.event_type == EventType::ORDER_EXPIRE) {
        const auto& o = std::get<OrderData>(ev.data);
        out["order_id"] = o.order_id;
        out["client_order_id"] = o.client_order_id;
        out["qty"] = o.qty;
        out["filled_qty"] = o.filled_qty;
        out["filled_avg_price"] = o.filled_avg_price;
        out["status"] = o.status;
    }
    return out;
}

void ControlServer::alpacaWatchlists(const drogon::HttpRequestPtr& req,
                                     std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json::array())); return; }
    json arr = json::array();
    auto it = watchlists_.find(session->id);
    if (it != watchlists_.end()) {
        for (auto& wl : it->second) {
            json obj;
            obj["id"] = wl.id;
            obj["name"] = wl.name;
            obj["assets"] = wl.symbols;
            arr.push_back(obj);
        }
    }
    callback(json_resp(arr));
}

namespace {
std::string gen_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char hex[] = "0123456789abcdef";
    uint64_t a = rng();
    uint64_t b = rng();
    std::string s(32, '0');
    for (int i = 0; i < 16; ++i) s[i] = hex[(a >> (i * 4)) & 0xF];
    for (int i = 0; i < 16; ++i) s[16 + i] = hex[(b >> (i * 4)) & 0xF];
    return s;
}
} // namespace

void ControlServer::alpacaCreateWatchlist(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&callback) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto body = json::parse(req->getBody());
    std::string name = body.value("name", "watchlist");
    auto symbols = body.value("symbols", std::vector<std::string>{});
    Watchlist wl{gen_id(), name, symbols};
    auto& list = watchlists_[session->id];
    list.push_back(wl);
    json resp;
    resp["id"] = wl.id;
    resp["name"] = wl.name;
    resp["assets"] = wl.symbols;
    callback(json_resp(resp, 201));
}

void ControlServer::alpacaGetWatchlist(const drogon::HttpRequestPtr& req,
                                       std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                       std::string wid) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto it = watchlists_.find(session->id);
    if (it == watchlists_.end()) { callback(json_resp(json{{"error","not found"}},404)); return; }
    for (auto& wl : it->second) {
        if (wl.id == wid) {
            json obj;
            obj["id"] = wl.id;
            obj["name"] = wl.name;
            obj["assets"] = wl.symbols;
            callback(json_resp(obj));
            return;
        }
    }
    callback(json_resp(json{{"error","not found"}},404));
}

void ControlServer::alpacaDeleteWatchlist(const drogon::HttpRequestPtr& req,
                                          std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                          std::string wid) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = resolve_session_from_param(req);
    if (!session) { callback(json_resp(json{{"error","session not found"}},404)); return; }
    auto it = watchlists_.find(session->id);
    if (it == watchlists_.end()) { callback(json_resp(json{{"error","not found"}},404)); return; }
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const Watchlist& w){ return w.id == wid; }), vec.end());
    callback(json_resp(json{{"status","deleted"}}));
}

} // namespace broker_sim
