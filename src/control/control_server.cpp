#include "control_server.hpp"
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
        // Use unlimited queue (0) for sessions to hold all preloaded events
        cfg.queue_capacity = cfg_.defaults.session_queue_capacity;
        cfg.overflow_policy = "block";  // Sessions should never drop events
        auto body = req->getBody();
        std::optional<std::string> requested_id;
        if (!body.empty()) {
            auto j = json::parse(body);
            cfg.symbols = j.value("symbols", std::vector<std::string>{});
            cfg.initial_capital = j.value("initial_capital", cfg_.defaults.initial_capital);
            cfg.speed_factor = j.value("speed_factor", cfg_.defaults.speed_factor);
            cfg.live_bar_aggr_source = j.value("live_bar_aggr_source", std::string{"trades"});
            cfg.live_aggr_bar_stream_freq_ms = j.value("live_aggr_bar_stream_freq", cfg_.defaults.live_aggr_bar_stream_freq_ms);
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
        auto start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cfg.start_time.time_since_epoch()).count();
        spdlog::info("createSession: parsed start_time_ns={}, live_bar_aggr_source={}, live_aggr_bar_stream_freq_ms={}", start_ns, cfg.live_bar_aggr_source, cfg.live_aggr_bar_stream_freq_ms);
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
    auto manager = session_mgr_;
    std::thread([manager, session_id]() {
        manager->destroy_session(session_id);
    }).detach();
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

void ControlServer::start(const drogon::HttpRequestPtr& req,
                          std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                          std::string session_id) {
    if (!authorize(req)) { callback(unauthorized()); return; }
    auto session = session_mgr_->get_session(session_id);
    if (!session) {
        callback(json_resp(json{{"error", "session not found"}}, 404));
        return;
    }
    if (session->status == SessionStatus::RUNNING || session->status == SessionStatus::PAUSED) {
        callback(json_resp(json{{"error", "session already started"}}, 400));
        return;
    }
    try {
        session_mgr_->start_session(session_id);
        callback(json_resp(json{{"status", "started"}, {"session_id", session_id}}));
    } catch (const std::exception& e) {
        spdlog::error("start_session failed: {}", e.what());
        callback(json_resp(json{{"error", e.what()}}, 500));
    }
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
    auto manager = session_mgr_;
    std::thread([manager, session_id]() {
        manager->stop_session(session_id);
    }).detach();
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

void ControlServer::on_event(const std::string& session_id, const Event& ev) {
    auto j = event_to_json(ev);
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        auto& dq = events_[session_id];
        dq.push_back(j);
        if (dq.size() > max_events_per_session_) dq.pop_front();
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
    } else if (ev.event_type == EventType::BAR) {
        const auto& b = std::get<BarData>(ev.data);
        out["open"] = b.open;
        out["high"] = b.high;
        out["low"] = b.low;
        out["close"] = b.close;
        out["volume"] = b.volume;
        if (b.vwap) out["vwap"] = *b.vwap;
        if (b.trade_count) out["trade_count"] = *b.trade_count;
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

} // namespace broker_sim
