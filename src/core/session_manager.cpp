#include "session_manager.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include "data_source_stub.hpp"
#include "checkpoint.hpp"

namespace broker_sim {

Session::Session(const std::string& session_id, const SessionConfig& cfg)
    : id(session_id)
    , config(cfg)
    , time_engine(std::make_shared<TimeEngine>())
    , event_queue(std::make_shared<EventQueue>(cfg.queue_capacity, cfg.overflow_policy))
    , matching_engine(std::make_shared<MatchingEngine>())
    , account_manager(std::make_shared<AccountManager>(cfg.initial_capital))
    , perf(std::make_shared<PerformanceTracker>())
    , created_at(std::chrono::system_clock::now())
    , cash(cfg.initial_capital)
    , equity(cfg.initial_capital)
    , events_processed(0)
    , last_checkpoint_events(0) {
    auto start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cfg.start_time.time_since_epoch()).count();
    spdlog::info("Session {} created: start_time_ns={}, speed={}", session_id, start_ns, cfg.speed_factor);
    time_engine->set_time(cfg.start_time);
    time_engine->set_speed(cfg.speed_factor);
    perf->record(cfg.start_time, cfg.initial_capital);
}

Session::~Session() {
    stop();
}

void Session::stop() {
    should_stop.store(true);
    time_engine->stop();
    if (event_queue) event_queue->stop();
    for (auto& t : feed_threads) {
        if (t && t->joinable()) t->join();
    }
    if (polling_thread && polling_thread->joinable()) {
        polling_thread->join();
    }
    if (worker_thread && worker_thread->joinable()) {
        worker_thread->join();
    }
}

SessionManager::SessionManager(std::shared_ptr<DataSource> data_source,
                               ExecutionConfig exec_cfg,
                               FeeConfig fee_cfg)
    : exec_cfg_(exec_cfg)
    , fee_cfg_(fee_cfg)
    , data_source_(std::move(data_source)) {
    if (!data_source_) {
        data_source_ = std::make_shared<StubDataSource>();
    }
}

SessionManager::~SessionManager() {
    stop_shared_feeder();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : sessions_) {
        kv.second->stop();
    }
}

std::shared_ptr<Session> SessionManager::create_session(const SessionConfig& config,
                                                        std::optional<std::string> session_id) {
    std::string id = session_id.value_or(generate_uuid());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it != sessions_.end()) {
            return it->second;
        }
    }
    auto session = std::make_shared<Session>(id, config);

    // Apply execution configuration to matching engine
    session->matching_engine->set_config(exec_cfg_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = session;
    }

    try {
        std::string wal_dir = exec_cfg_.wal_directory.empty() ? "logs" : exec_cfg_.wal_directory;
        std::filesystem::create_directories(wal_dir);

        {
            std::lock_guard<std::mutex> l(log_mutex_);
            session_logs_[id] = std::ofstream(wal_dir + "/session_" + id + ".events.jsonl",
                                              std::ios::out | std::ios::trunc);
        }

        if (exec_cfg_.enable_wal) {
            session->wal = std::make_unique<WalLogger>(wal_path(wal_dir, id));
        }

        // Attempt recovery from prior checkpoint
        if (restore_session(session)) {
            spdlog::info("Recovered session {} from checkpoint and WAL", id);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to init event log for session {}: {}", id, e.what());
    }

    return session;
}

std::shared_ptr<Session> SessionManager::get_session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Session>> SessionManager::list_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Session>> out;
    out.reserve(sessions_.size());
    for (auto& kv : sessions_) out.push_back(kv.second);
    return out;
}

void SessionManager::start_session(const std::string& session_id) {
    auto session = get_session(session_id);
    if (!session) return;
    session->status = SessionStatus::RUNNING;
    session->started_at = std::chrono::system_clock::now();
    session->time_engine->start();
    session->should_stop.store(false);
    if (exec_cfg_.enable_shared_feed) {
        start_shared_feeder();
    } else if (exec_cfg_.poll_interval_seconds > 0) {
        start_polling_feeder(session);
    } else {
        preload_events(session);
    }
    session->worker_thread = std::make_unique<std::thread>(
        [this, session]() { run_session_loop(session); }
    );
}

void SessionManager::pause_session(const std::string& session_id) {
    auto session = get_session(session_id);
    if (session) {
        session->time_engine->pause();
        session->status = SessionStatus::PAUSED;
        if (session->wal) {
            nlohmann::json w{{"event","session_paused"},{"session_id",session_id}};
            session->wal->append(w);
        }
    }
}

void SessionManager::resume_session(const std::string& session_id) {
    auto session = get_session(session_id);
    if (session) {
        session->time_engine->resume();
        session->status = SessionStatus::RUNNING;
        if (session->wal) {
            nlohmann::json w{{"event","session_resumed"},{"session_id",session_id}};
            session->wal->append(w);
        }
    }
}

void SessionManager::stop_session(const std::string& session_id) {
    auto session = get_session(session_id);
    if (session) {
        // Save checkpoint before stopping
        save_session_checkpoint(session_id);

        session->stop();
        session->status = SessionStatus::STOPPED;

        if (session->wal) {
            nlohmann::json w{{"event","session_stopped"},{"session_id",session_id}};
            session->wal->append(w);
        }
    }
    if (exec_cfg_.enable_shared_feed) {
        bool any_running = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : sessions_) {
                if (kv.second->status == SessionStatus::RUNNING) {
                    any_running = true;
                    break;
                }
            }
        }
        if (!any_running) stop_shared_feeder();
    }
}

void SessionManager::destroy_session(const std::string& session_id) {
    // Save checkpoint before destroying
    save_session_checkpoint(session_id);

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            session = it->second;
            sessions_.erase(it);
        }
    }
    if (session) {
        session->stop();
    }
    if (exec_cfg_.enable_shared_feed) {
        bool any_running = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : sessions_) {
                if (kv.second->status == SessionStatus::RUNNING) {
                    any_running = true;
                    break;
                }
            }
        }
        if (!any_running) stop_shared_feeder();
    }
}

std::string SessionManager::submit_order(const std::string& session_id, Order order) {
    auto session = get_session(session_id);
    if (!session) return {};
    if (order.id.empty()) order.id = generate_uuid();
    if (order.client_order_id.empty()) order.client_order_id = order.id;
    order.is_maker = false;
    int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    order.created_at_ns = now_ns;
    order.submitted_at_ns = now_ns;
    order.updated_at_ns = now_ns;
    if (exec_cfg_.enable_latency && exec_cfg_.fixed_latency_us > 0) {
        int64_t base_ns = session->last_event_ns.load(std::memory_order_acquire);
        if (base_ns == 0) {
            base_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                session->config.start_time.time_since_epoch()).count();
        }
        order.min_exec_timestamp = base_ns + exec_cfg_.fixed_latency_us * 1000;
    }

    // Basic OPG/CLS handling: constrain to session window.
    auto now_ts = std::chrono::system_clock::now();
    if (order.tif == TimeInForce::OPG) {
        // Only allow up to 5 minutes after session start.
        Timestamp cutoff = session->config.start_time + std::chrono::minutes(5);
        order.expire_at = cutoff;
        if (now_ts > cutoff) {
            spdlog::warn("Order rejected: OPG past window id={}", order.id);
            return {};
        }
    } else if (order.tif == TimeInForce::CLS) {
        // Set expiry at session end; allow submits only during session.
        order.expire_at = session->config.end_time;
        if (now_ts > session->config.end_time) {
            spdlog::warn("Order rejected: CLS past session end id={}", order.id);
            return {};
        }
    } else if (order.tif == TimeInForce::DAY) {
        order.expire_at = session->config.end_time;
    }

    // Simple buying power check for buys using latest ask/limit.
    double est_price = 0.0;
    if (order.limit_price) est_price = *order.limit_price;
    auto nbbo = session->matching_engine->get_nbbo(order.symbol);
    if (order.side == OrderSide::BUY) {
        if (nbbo) est_price = nbbo->ask_price;
        if (order.qty && est_price > 0.0) {
            double notional = (*order.qty) * est_price;
            if (!session->account_manager->has_buying_power(notional, true)) {
                spdlog::warn("Order rejected: insufficient buying power order={} notional={}", order.id, notional);
                return {};
            }
        }
    } else { // SELL
        if (!exec_cfg_.allow_shorting) {
            auto positions = session->account_manager->positions();
            auto it = positions.find(order.symbol);
            double pos_qty = (it != positions.end()) ? it->second.qty : 0.0;
            if (order.qty && pos_qty < *order.qty) {
                spdlog::warn("Order rejected: shorting disabled order={} qty={}", order.id, order.qty.value_or(0.0));
                return {};
            }
        } else {
            if (order.qty && order.limit_price) {
                double notional = (*order.qty) * (*order.limit_price);
                if (!session->account_manager->has_buying_power(notional, false)) {
                    spdlog::warn("Order rejected: insufficient margin for short order={} notional={}", order.id, notional);
                    return {};
                }
            }
        }
    }

    if (order.type == OrderType::LIMIT && nbbo) {
        bool marketable = false;
        if (order.side == OrderSide::BUY && order.limit_price) {
            marketable = *order.limit_price >= nbbo->ask_price;
        } else if (order.side == OrderSide::SELL && order.limit_price) {
            marketable = *order.limit_price <= nbbo->bid_price;
        }
        order.is_maker = !marketable;
    }

    // Circuit breaker check - reject orders for halted symbols
    if (exec_cfg_.enable_circuit_breakers) {
        std::lock_guard<std::mutex> lock(session->halt_mutex);
        // Check for expired halts first
        auto now_ts = std::chrono::system_clock::now();
        auto end_it = session->halt_end_times.find(order.symbol);
        if (end_it != session->halt_end_times.end() && now_ts >= end_it->second) {
            // Halt has expired, remove it
            session->halted_symbols.erase(order.symbol);
            session->halt_end_times.erase(end_it);
        }
        // Check if symbol is currently halted
        if (session->halted_symbols.find(order.symbol) != session->halted_symbols.end()) {
            spdlog::warn("Order rejected: {} is halted (circuit breaker active)", order.symbol);
            return {};
        }
    }

    // Short Sale Restriction (SSR) check - SEC Rule 201 Alternative Uptick Rule
    // When SSR is active, short sales must be at or above the current national best bid
    if (exec_cfg_.enable_short_sale_restrictions && order.side == OrderSide::SELL) {
        // Check if this is a short sale (selling more than we own)
        auto positions = session->account_manager->positions();
        auto pos_it = positions.find(order.symbol);
        double pos_qty = (pos_it != positions.end()) ? pos_it->second.qty : 0.0;
        bool is_short_sale = order.qty && (*order.qty > pos_qty);

        if (is_short_sale) {
            std::lock_guard<std::mutex> lock(session->ssr_mutex);
            if (session->ssr_symbols.find(order.symbol) != session->ssr_symbols.end()) {
                // SSR is active for this symbol - enforce uptick rule
                if (nbbo && nbbo->bid_price > 0.0) {
                    // For market orders, we can't guarantee the price, so reject
                    if (order.type == OrderType::MARKET) {
                        spdlog::warn("Order rejected: Market short sell not allowed for {} (SSR active)",
                                     order.symbol);
                        return {};
                    }
                    // For limit orders, price must be >= national best bid
                    if (order.limit_price && *order.limit_price < nbbo->bid_price) {
                        spdlog::warn("Order rejected: Short sell limit ${:.2f} below NBB ${:.2f} for {} (SSR active)",
                                     *order.limit_price, nbbo->bid_price, order.symbol);
                        return {};
                    }
                }
            }
        }
    }

    auto fill = session->matching_engine->submit_order(order);
    upsert_order(session, order);
    {
        std::lock_guard<std::mutex> lock(session->orders_mutex);
        auto it = session->orders.find(order.id);
        if (it != session->orders.end()) {
            it->second.updated_at_ns = now_ns;
        }
    }
    {
        Event ev;
        ev.timestamp = std::chrono::system_clock::now();
        ev.sequence = 0;
        ev.event_type = EventType::ORDER_NEW;
        ev.symbol = order.symbol;
        ev.data = OrderData{order.id, order.client_order_id, order.qty.value_or(0.0),
                            order.filled_qty, 0.0, "new"};
        std::vector<EventCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbacks_copy = event_callbacks_;
        }
        for (auto& cb : callbacks_copy) {
            if (cb) cb(session->id, ev);
        }
    }
    {
        std::lock_guard<std::mutex> l(log_mutex_);
        auto it = session_logs_.find(session_id);
        if (it != session_logs_.end() && it->second.good()) {
            it->second << fmt::format(R"({{"event":"order_submitted","id":"{}","symbol":"{}","side":"{}","type":{},"qty":{},"limit":{},"stop":{}}})",
                                       order.id, order.symbol, (order.side == OrderSide::BUY ? "BUY" : "SELL"),
                                       static_cast<int>(order.type),
                                       order.qty.value_or(0.0),
                                       order.limit_price.value_or(0.0),
                                       order.stop_price.value_or(0.0))
                       << "\n";
        }
    }
    if (session->wal) {
        nlohmann::json w{
            {"ts_ns", session->last_event_ns.load(std::memory_order_acquire)},
            {"event", "order_submitted"},
            {"id", order.id},
            {"symbol", order.symbol},
            {"side", order.side == OrderSide::BUY ? "BUY" : "SELL"},
            {"type", static_cast<int>(order.type)},
            {"tif", static_cast<int>(order.tif)},
            {"qty", order.qty.value_or(0.0)},
            {"limit", order.limit_price.value_or(0.0)},
            {"stop", order.stop_price.value_or(0.0)}
        };
        session->wal->append(w);
    }
    if (fill && fill->fill_qty > 0.0) process_fill(session, *fill);

    // IOC/FOK remainder canceled immediately if not fully filled.
    if (order.qty) {
        double remaining = *order.qty - order.filled_qty - (fill ? fill->fill_qty : 0.0);
        if (remaining > 0.0 && (order.tif == TimeInForce::IOC || order.tif == TimeInForce::FOK)) {
            order.filled_qty += fill ? fill->fill_qty : 0.0;
            order.status = OrderStatus::CANCELED;
            order.updated_at_ns = now_ns;
            order.canceled_at_ns = now_ns;
            upsert_order(session, order);
            Event ev;
            ev.timestamp = std::chrono::system_clock::now();
            ev.sequence = 0;
            ev.event_type = EventType::ORDER_CANCEL;
            ev.symbol = order.symbol;
            ev.data = OrderData{order.id, order.client_order_id, order.qty.value_or(0.0),
                                order.filled_qty, 0.0, "canceled"};
            std::vector<EventCallback> callbacks_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                callbacks_copy = event_callbacks_;
            }
            for (auto& cb : callbacks_copy) {
                if (cb) cb(session->id, ev);
            }
        }
    }
    return order.id;
}

bool SessionManager::cancel_order(const std::string& session_id, const std::string& order_id) {
    auto session = get_session(session_id);
    if (!session) return false;
    bool canceled = session->matching_engine->cancel_order(order_id);
    std::optional<Order> order_opt;
    if (canceled) {
        std::lock_guard<std::mutex> lock(session->orders_mutex);
        auto it = session->orders.find(order_id);
        if (it != session->orders.end()) {
            it->second.status = OrderStatus::CANCELED;
            int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            it->second.updated_at_ns = now_ns;
            it->second.canceled_at_ns = now_ns;
            order_opt = it->second;
        }
    }
    if (canceled) {
        std::lock_guard<std::mutex> l(log_mutex_);
        auto it = session_logs_.find(session_id);
        if (it != session_logs_.end() && it->second.good()) {
            it->second << R"({"event":"order_canceled","id":")" << order_id << "\"}\n";
        }
    }
    if (canceled && session && session->wal) {
        nlohmann::json w{
            {"ts_ns", session->last_event_ns.load(std::memory_order_acquire)},
            {"event","order_canceled"},
            {"id", order_id}
        };
        session->wal->append(w);
    }
    if (canceled) {
        Event ev;
        ev.timestamp = std::chrono::system_clock::now();
        ev.sequence = 0;
        ev.event_type = EventType::ORDER_CANCEL;
        ev.symbol = order_opt ? order_opt->symbol : "";
        double qty = order_opt ? order_opt->qty.value_or(0.0) : 0.0;
        double filled_qty = order_opt ? order_opt->filled_qty : 0.0;
        ev.data = OrderData{order_id, order_opt ? order_opt->client_order_id : order_id,
                            qty, filled_qty, 0.0, "canceled"};
        std::vector<EventCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbacks_copy = event_callbacks_;
        }
        for (auto& cb : callbacks_copy) {
            if (cb) cb(session_id, ev);
        }
    }
    return canceled;
}

std::unordered_map<std::string, Order> SessionManager::get_orders(const std::string& session_id) const {
    auto session = get_session(session_id);
    if (!session) return {};
    std::lock_guard<std::mutex> lock(session->orders_mutex);
    return session->orders;
}

void SessionManager::add_event_callback(EventCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callbacks_.push_back(std::move(cb));
}

void SessionManager::run_session_loop(std::shared_ptr<Session> session) {
    spdlog::info("Session {} loop starting, queue_size={}", session->id, session->event_queue->size());
    try {
        size_t processed = 0;
        while (!session->should_stop.load()) {
            auto ev_opt = session->event_queue->wait_and_pop();
            if (!ev_opt) {
                spdlog::info("Session {} loop: wait_and_pop returned empty", session->id);
                break;
            }
            const Event& ev = *ev_opt;
            if (!session->time_engine->wait_for_next_event(ev.timestamp)) {
                spdlog::info("Session {} loop: wait_for_next_event returned false", session->id);
                break;
            }
            process_event(session, ev, true);
            processed++;
            if (processed == 1 || processed % 10000 == 0) {
                spdlog::info("Session {} processed {} events", session->id, processed);
            }
        }
        spdlog::info("Session {} loop ended, processed {} events", session->id, processed);
        if (!session->should_stop.load()) {
            session->status = SessionStatus::COMPLETED;
            session->completed_at = std::chrono::system_clock::now();
        }
    } catch (const std::exception& e) {
        session->status = SessionStatus::ERROR;
        spdlog::error("Session {} error: {}", session->id, e.what());
    }
}

void SessionManager::process_event(std::shared_ptr<Session> session, const Event& ev, bool emit_callbacks) {
    // Track event processing for periodic checkpointing
    session->events_processed.fetch_add(1, std::memory_order_relaxed);

    append_event_log(session->id,
        fmt::format(R"({{"ts_ns":{},"seq":{},"symbol":"{}","type":{}}})",
                    std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count(),
                    ev.sequence,
                    ev.symbol,
                    static_cast<int>(ev.event_type)));
    session->last_event_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count(),
                                 std::memory_order_release);
    if (session->wal) {
        nlohmann::json w{
            {"ts_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count()},
            {"event","market_event"},
            {"symbol", ev.symbol},
            {"type", static_cast<int>(ev.event_type)},
            {"seq", ev.sequence}
        };
        if (ev.event_type == EventType::QUOTE) {
            const auto& q = std::get<QuoteData>(ev.data);
            w["bid_price"] = q.bid_price;
            w["bid_size"] = q.bid_size;
            w["ask_price"] = q.ask_price;
            w["ask_size"] = q.ask_size;
            w["bid_exch"] = q.bid_exchange;
            w["ask_exch"] = q.ask_exchange;
        } else if (ev.event_type == EventType::TRADE) {
            const auto& t = std::get<TradeData>(ev.data);
            w["price"] = t.price;
            w["size"] = t.size;
            w["exchange"] = t.exchange;
            w["conditions"] = t.conditions;
        }
        session->wal->append(w);
    }
    if (ev.event_type == EventType::QUOTE) {
        const auto& q = std::get<QuoteData>(ev.data);
        NBBO nbbo{ev.symbol, q.bid_price, q.bid_size, q.ask_price, q.ask_size,
                  std::chrono::duration_cast<std::chrono::nanoseconds>(ev.timestamp.time_since_epoch()).count()};
        auto result = session->matching_engine->update_nbbo(nbbo);
        for (auto& f : result.fills) process_fill(session, f);
        for (auto& o : result.expired) {
            o.status = OrderStatus::EXPIRED;
            o.expired_at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                ev.timestamp.time_since_epoch()).count();
            o.updated_at_ns = o.expired_at_ns;
            upsert_order(session, o);
            Event oe;
            oe.timestamp = ev.timestamp;
            oe.sequence = ev.sequence;
            oe.event_type = EventType::ORDER_EXPIRE;
            oe.symbol = o.symbol;
            oe.data = OrderData{o.id, o.client_order_id.empty() ? o.id : o.client_order_id,
                                o.qty.value_or(0.0), o.filled_qty, 0.0, "expired"};
            std::vector<EventCallback> callbacks_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                callbacks_copy = event_callbacks_;
            }
            for (auto& cb : callbacks_copy) {
                if (cb) cb(session->id, oe);
            }
        }
        // Mark to market using mid-price.
        session->account_manager->mark_to_market(ev.symbol, nbbo.mid_price());
        enforce_margin(session);
    } else if (ev.event_type == EventType::TRADE) {
        const auto& t = std::get<TradeData>(ev.data);
        session->account_manager->mark_to_market(ev.symbol, t.price);

        // SSR check: If stock drops 10%+ from prior close, trigger SSR
        if (exec_cfg_.enable_short_sale_restrictions) {
            std::lock_guard<std::mutex> lock(session->ssr_mutex);
            auto prior_it = session->prior_close.find(ev.symbol);
            if (prior_it != session->prior_close.end() && prior_it->second > 0.0) {
                double drop_pct = (prior_it->second - t.price) / prior_it->second * 100.0;
                if (drop_pct >= exec_cfg_.ssr_threshold_pct) {
                    if (session->ssr_symbols.find(ev.symbol) == session->ssr_symbols.end()) {
                        session->ssr_symbols.insert(ev.symbol);
                        spdlog::info("SSR triggered for {} (down {:.2f}% from prior close)",
                                     ev.symbol, drop_pct);
                    }
                }
            }
        }
    } else if (ev.event_type == EventType::HALT) {
        // Trading halt - add symbol to halted set
        const auto& h = std::get<HaltData>(ev.data);
        if (h.is_halted) {
            std::lock_guard<std::mutex> lock(session->halt_mutex);
            session->halted_symbols.insert(ev.symbol);
            // Set halt end time if duration is configured
            if (exec_cfg_.luld_halt_duration_sec > 0) {
                auto halt_end = ev.timestamp + std::chrono::seconds(exec_cfg_.luld_halt_duration_sec);
                session->halt_end_times[ev.symbol] = halt_end;
            }
            spdlog::info("Trading halted for {} (reason: {})", ev.symbol, h.reason);
        } else {
            // Resume trading
            std::lock_guard<std::mutex> lock(session->halt_mutex);
            session->halted_symbols.erase(ev.symbol);
            session->halt_end_times.erase(ev.symbol);
            spdlog::info("Trading resumed for {}", ev.symbol);
        }
    } else if (ev.event_type == EventType::RESUME) {
        // Trading resume
        std::lock_guard<std::mutex> lock(session->halt_mutex);
        session->halted_symbols.erase(ev.symbol);
        session->halt_end_times.erase(ev.symbol);
        spdlog::info("Trading resumed for {}", ev.symbol);
    } else if (ev.event_type == EventType::DIVIDEND) {
        // Apply dividend automatically if enabled
        if (exec_cfg_.enable_auto_corporate_actions) {
            const auto& d = std::get<DividendData>(ev.data);
            session->account_manager->apply_dividend(ev.symbol, d.amount_per_share);
            session->cash = session->account_manager->state().cash;
            if (session->wal) {
                nlohmann::json w{
                    {"event", "dividend"},
                    {"symbol", ev.symbol},
                    {"amount_per_share", d.amount_per_share}
                };
                session->wal->append(w);
            }
            spdlog::info("Applied dividend for {}: ${:.4f}/share", ev.symbol, d.amount_per_share);
        }
    } else if (ev.event_type == EventType::SPLIT) {
        // Apply stock split automatically if enabled
        if (exec_cfg_.enable_auto_corporate_actions) {
            const auto& s = std::get<SplitData>(ev.data);
            double ratio = s.ratio();
            session->account_manager->apply_split(ev.symbol, ratio);
            session->cash = session->account_manager->state().cash;
            if (session->wal) {
                nlohmann::json w{
                    {"event", "split"},
                    {"symbol", ev.symbol},
                    {"ratio", ratio}
                };
                session->wal->append(w);
            }
            spdlog::info("Applied split for {}: {}:{} (ratio {:.4f})",
                         ev.symbol, s.from_factor, s.to_factor, ratio);
        }
    }
    session->equity = session->account_manager->state().equity;
    if (session->perf) {
        session->perf->record(ev.timestamp, session->equity);
    }

    if (emit_callbacks) {
        std::vector<EventCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbacks_copy = event_callbacks_;
        }
        for (auto& cb : callbacks_copy) {
            if (cb) cb(session->id, ev);
        }
    }

    // Periodic checkpointing
    maybe_checkpoint(session);
}

void SessionManager::process_fill(std::shared_ptr<Session> session, const Fill& fill) {
    auto order_opt = find_order(session, fill.order_id);
    if (!order_opt) {
        spdlog::warn("Fill for unknown order {}", fill.order_id);
        return;
    }
    auto order = *order_opt;
    order.filled_qty += fill.fill_qty;
    order.last_fill_price = fill.fill_price;
    if (order.qty && order.filled_qty >= *order.qty) {
        order.status = OrderStatus::FILLED;
        order.filled_at_ns = fill.timestamp;
    } else {
        order.status = OrderStatus::PARTIALLY_FILLED;
    }
    order.updated_at_ns = fill.timestamp;
    upsert_order(session, order);

    Fill applied_fill = fill;
    if (exec_cfg_.enable_market_impact && exec_cfg_.market_impact_bps > 0.0) {
        auto nbbo = session->matching_engine->get_nbbo(order.symbol);
        if (nbbo) {
            double available = order.side == OrderSide::BUY ? nbbo->ask_size : nbbo->bid_size;
            if (available > 0.0 && applied_fill.fill_price > 0.0) {
                double ratio = std::min(1.0, applied_fill.fill_qty / available);
                double impact_bps = exec_cfg_.market_impact_bps * ratio / 10000.0;
                if (order.side == OrderSide::BUY) {
                    applied_fill.fill_price *= (1.0 + impact_bps);
                } else {
                    applied_fill.fill_price *= (1.0 - impact_bps);
                }
            }
        }
    }
    if (exec_cfg_.enable_slippage && exec_cfg_.fixed_slippage_bps != 0.0) {
        double bps = exec_cfg_.fixed_slippage_bps / 10000.0;
        if (order.side == OrderSide::BUY) {
            applied_fill.fill_price *= (1.0 + bps);
        } else {
            applied_fill.fill_price *= (1.0 - bps);
        }
    }
    if (exec_cfg_.enable_latency && exec_cfg_.fixed_latency_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(exec_cfg_.fixed_latency_us));
    }

    double fees = 0.0;
    if (applied_fill.fill_qty > 0.0 && applied_fill.fill_price > 0.0) {
        bool is_sell = order.side == OrderSide::SELL;
        fees = fee_cfg_.calculate_fees(applied_fill.fill_qty, applied_fill.fill_price, is_sell, order.is_maker);
    }

    session->account_manager->apply_fill(order.symbol, applied_fill, order.side, fees);
    session->cash = session->account_manager->state().cash;
    session->equity = session->account_manager->state().equity;
    if (session->perf) {
        session->perf->record(Timestamp{} + std::chrono::nanoseconds(fill.timestamp), session->equity);
    }
    spdlog::info("Fill: order={} side={} qty={} price={} cash={} equity={}",
                 fill.order_id,
                 order.side == OrderSide::BUY ? "BUY" : "SELL",
                 applied_fill.fill_qty, applied_fill.fill_price,
                 session->cash, session->equity);
    if (session->wal) {
        nlohmann::json w{
            {"ts_ns", fill.timestamp},
            {"event","fill"},
            {"order_id", fill.order_id},
            {"symbol", order.symbol},
            {"side", order.side == OrderSide::BUY ? "BUY" : "SELL"},
            {"qty", applied_fill.fill_qty},
            {"price", applied_fill.fill_price}
        };
        session->wal->append(w);
    }
    append_event_log(session->id,
        fmt::format(R"({{"event":"fill","order_id":"{}","symbol":"{}","side":"{}","qty":{},"price":{},"ts":{}}})",
                    fill.order_id, order.symbol,
                    order.side == OrderSide::BUY ? "BUY" : "SELL",
                    applied_fill.fill_qty, applied_fill.fill_price,
                    fill.timestamp));

    Event ev;
    ev.timestamp = Timestamp{} + std::chrono::nanoseconds(fill.timestamp);
    ev.sequence = 0;
    ev.event_type = EventType::ORDER_FILL;
    ev.symbol = order.symbol;
    ev.data = OrderData{order.id, order.client_order_id, order.qty.value_or(0.0), order.filled_qty,
                        applied_fill.fill_price,
                        order.status == OrderStatus::FILLED ? "filled" : "partially_filled"};
    std::vector<EventCallback> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_copy = event_callbacks_;
    }
    for (auto& cb : callbacks_copy) {
        if (cb) cb(session->id, ev);
    }
}

bool SessionManager::apply_dividend(const std::string& session_id, const std::string& symbol, double amount_per_share) {
    auto session = get_session(session_id);
    if (!session) return false;
    session->account_manager->apply_dividend(symbol, amount_per_share);
    session->cash = session->account_manager->state().cash;
    session->equity = session->account_manager->state().equity;
    if (session->wal) {
        nlohmann::json w{
            {"event","dividend"},
            {"symbol", symbol},
            {"amount_per_share", amount_per_share}
        };
        session->wal->append(w);
    }
    append_event_log(session_id,
        fmt::format(R"({{"event":"dividend","symbol":"{}","amount_per_share":{}}})",
                    symbol, amount_per_share));
    return true;
}

bool SessionManager::apply_split(const std::string& session_id, const std::string& symbol, double split_ratio) {
    auto session = get_session(session_id);
    if (!session) return false;
    session->account_manager->apply_split(symbol, split_ratio);
    session->cash = session->account_manager->state().cash;
    session->equity = session->account_manager->state().equity;
    if (session->wal) {
        nlohmann::json w{
            {"event","split"},
            {"symbol", symbol},
            {"ratio", split_ratio}
        };
        session->wal->append(w);
    }
    append_event_log(session_id,
        fmt::format(R"({{"event":"split","symbol":"{}","ratio":{}}})",
                    symbol, split_ratio));
    return true;
}

void SessionManager::preload_events(std::shared_ptr<Session> session) {
    if (!data_source_) return;
    auto symbols = session->config.symbols;
    auto start = session->config.start_time;
    auto end = session->config.end_time;

    data_source_->stream_events(symbols, start, end, [this, session](const MarketEvent& ev) {
        enqueue_event(session, ev);
    });
}

void SessionManager::start_polling_feeder(std::shared_ptr<Session> session) {
    if (!data_source_) return;
    auto symbols = session->config.symbols;
    auto start = session->config.start_time;
    auto end = session->config.end_time;
    int window_secs = exec_cfg_.poll_interval_seconds;
    if (window_secs <= 0) return;
    session->polling_thread = std::make_unique<std::thread>(
        [this, session, symbols, start, end, window_secs]() {
            Timestamp cursor = start;
            auto window = std::chrono::seconds(window_secs);
            while (!session->should_stop.load() && cursor < end) {
                Timestamp window_end = std::min(cursor + window, end);
                data_source_->stream_events(symbols, cursor, window_end, [this, session](const MarketEvent& ev) {
                    enqueue_event(session, ev);
                });
                cursor = window_end;
                std::this_thread::sleep_for(window);
            }
        }
    );
}

void SessionManager::append_event_log(const std::string& session_id, const std::string& payload) {
    std::lock_guard<std::mutex> l(log_mutex_);
    auto it = session_logs_.find(session_id);
    if (it != session_logs_.end() && it->second.good()) {
        it->second << payload << "\n";
    }
}

void SessionManager::stop_feeds(std::shared_ptr<Session> session) {
    (void)session;
    // DataSource streaming API is currently blocking without cancel; threads will exit when stream ends.
}

void SessionManager::start_shared_feeder() {
    if (shared_feed_running_.exchange(true)) return;
    if (!data_source_) {
        shared_feed_running_.store(false, std::memory_order_release);
        return;
    }
    shared_feed_thread_ = std::make_unique<std::thread>([this]() {
        while (shared_feed_running_.load(std::memory_order_acquire)) {
            std::vector<std::shared_ptr<Session>> running_sessions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& kv : sessions_) {
                    if (kv.second->status == SessionStatus::RUNNING) {
                        running_sessions.push_back(kv.second);
                    }
                }
            }
            if (running_sessions.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            Timestamp min_start = running_sessions.front()->config.start_time;
            Timestamp max_end = running_sessions.front()->config.end_time;
            std::vector<std::string> symbols;
            symbols.reserve(64);
            for (const auto& s : running_sessions) {
                if (s->config.start_time < min_start) min_start = s->config.start_time;
                if (s->config.end_time > max_end) max_end = s->config.end_time;
                for (const auto& sym : s->config.symbols) symbols.push_back(sym);
            }
            std::sort(symbols.begin(), symbols.end());
            symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());

            data_source_->stream_events(symbols, min_start, max_end,
                [this, running_sessions, symbols](const MarketEvent& ev) {
                for (const auto& s : running_sessions) {
                    if (ev.timestamp < s->config.start_time || ev.timestamp > s->config.end_time) continue;
                    if (!s->config.symbols.empty() &&
                        std::find(s->config.symbols.begin(), s->config.symbols.end(),
                                  (ev.type == MarketEventType::QUOTE ? ev.quote.symbol : ev.trade.symbol)) == s->config.symbols.end()) {
                        continue;
                    }
                    enqueue_event(s, ev);
                }
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void SessionManager::stop_shared_feeder() {
    if (!shared_feed_running_.exchange(false)) return;
    if (shared_feed_thread_ && shared_feed_thread_->joinable()) {
        shared_feed_thread_->join();
    }
    shared_feed_thread_.reset();
}

bool SessionManager::enqueue_event(std::shared_ptr<Session> session, const MarketEvent& ev) {
    bool ok = false;
    if (ev.type == MarketEventType::QUOTE) {
        ok = session->event_queue->push(ev.timestamp, EventType::QUOTE, ev.quote.symbol,
            QuoteData{ev.quote.bid_price, ev.quote.bid_size, ev.quote.ask_price, ev.quote.ask_size,
                      ev.quote.bid_exchange, ev.quote.ask_exchange, ev.quote.tape});
    } else {
        ok = session->event_queue->push(ev.timestamp, EventType::TRADE, ev.trade.symbol,
            TradeData{ev.trade.price, ev.trade.size, ev.trade.exchange, ev.trade.conditions, ev.trade.tape});
    }
    session->events_enqueued.fetch_add(1, std::memory_order_relaxed);
    if (!ok) session->events_dropped.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

void SessionManager::enforce_margin(std::shared_ptr<Session> session) {
    if (!exec_cfg_.enable_margin_call_checks) return;
    auto st = session->account_manager->state();
    if (st.maintenance_margin <= 0.0) {
        session->margin_call_active.store(false, std::memory_order_release);
        return;
    }
    if (st.equity >= st.maintenance_margin) {
        session->margin_call_active.store(false, std::memory_order_release);
        return;
    }
    if (session->margin_call_active.exchange(true)) return;
    if (!exec_cfg_.enable_forced_liquidation) return;

    auto positions = session->account_manager->positions();
    for (const auto& kv : positions) {
        const auto& pos = kv.second;
        if (pos.qty == 0.0) continue;
        auto nbbo = session->matching_engine->get_nbbo(pos.symbol);
        if (!nbbo) continue;
        double price = pos.qty > 0 ? nbbo->bid_price : nbbo->ask_price;
        if (price <= 0.0) continue;
        Order order;
        order.id = generate_uuid();
        order.client_order_id = order.id;
        order.symbol = pos.symbol;
        order.side = pos.qty > 0 ? OrderSide::SELL : OrderSide::BUY;
        order.type = OrderType::MARKET;
        order.tif = TimeInForce::DAY;
        order.qty = std::abs(pos.qty);
        order.status = OrderStatus::NEW;
        int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        order.created_at_ns = now_ns;
        order.submitted_at_ns = now_ns;
        order.updated_at_ns = now_ns;
        upsert_order(session, order);

        Event ev;
        ev.timestamp = Timestamp{} + std::chrono::nanoseconds(nbbo->timestamp);
        ev.sequence = 0;
        ev.event_type = EventType::ORDER_NEW;
        ev.symbol = order.symbol;
        ev.data = OrderData{order.id, order.client_order_id, order.qty.value_or(0.0), 0.0, 0.0, "liquidation_new"};
        std::vector<EventCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbacks_copy = event_callbacks_;
        }
        for (auto& cb : callbacks_copy) {
            if (cb) cb(session->id, ev);
        }

        Fill fill{order.id, order.qty.value_or(0.0), price, nbbo->timestamp, false};
        process_fill(session, fill);
    }

    auto st_after = session->account_manager->state();
    if (st_after.equity >= st_after.maintenance_margin) {
        session->margin_call_active.store(false, std::memory_order_release);
    }
}

std::optional<Order> SessionManager::find_order(std::shared_ptr<Session> session, const std::string& order_id) {
    std::lock_guard<std::mutex> lock(session->orders_mutex);
    auto it = session->orders.find(order_id);
    if (it != session->orders.end()) return it->second;
    return std::nullopt;
}

void SessionManager::upsert_order(std::shared_ptr<Session> session, const Order& order) {
    std::lock_guard<std::mutex> lock(session->orders_mutex);
    session->orders[order.id] = order;
}

std::string SessionManager::generate_uuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char hex[] = "0123456789abcdef";
    uint64_t a = rng();
    uint64_t b = rng();
    std::string s(64, '0');
    for (int i = 0; i < 16; ++i) {
        s[i * 2] = hex[(a >> (i * 4)) & 0xF];
        s[i * 2 + 1] = hex[(a >> (i * 4 + 4)) & 0xF];
    }
    for (int i = 0; i < 16; ++i) {
        s[32 + i * 2] = hex[(b >> (i * 4)) & 0xF];
        s[32 + i * 2 + 1] = hex[(b >> (i * 4 + 4)) & 0xF];
    }
    return s;
}

void SessionManager::set_speed(const std::string& session_id, double speed) {
    auto session = get_session(session_id);
    if (session) {
        session->config.speed_factor = speed;
        session->time_engine->set_speed(speed);
    }
}

void SessionManager::jump_to(const std::string& session_id, Timestamp ts) {
    auto session = get_session(session_id);
    if (session) {
        bool was_running = session->status == SessionStatus::RUNNING;
        bool was_paused = session->status == SessionStatus::PAUSED;
        session->should_stop.store(true);
        session->time_engine->stop();
        if (session->event_queue) session->event_queue->stop();
        for (auto& t : session->feed_threads) {
            if (t && t->joinable()) t->join();
        }
        session->feed_threads.clear();
        if (session->polling_thread && session->polling_thread->joinable()) {
            session->polling_thread->join();
        }
        session->polling_thread.reset();
        if (session->worker_thread && session->worker_thread->joinable()) {
            session->worker_thread->join();
        }
        session->worker_thread.reset();
        session->event_queue = std::make_shared<EventQueue>(session->config.queue_capacity,
                                                            session->config.overflow_policy);
        session->matching_engine = std::make_shared<MatchingEngine>();
        session->account_manager = std::make_shared<AccountManager>(session->config.initial_capital);
        session->perf = std::make_shared<PerformanceTracker>();
        {
            std::lock_guard<std::mutex> lock(session->orders_mutex);
            session->orders.clear();
        }
        session->last_event_ns.store(0, std::memory_order_release);
        session->cash = session->config.initial_capital;
        session->equity = session->config.initial_capital;
        session->perf->record(ts, session->equity);
        session->time_engine->set_time(ts);
        session->config.start_time = ts;
        session->should_stop.store(false);

        if (was_running || was_paused) {
            session->status = SessionStatus::RUNNING;
            session->time_engine->start();
            if (exec_cfg_.poll_interval_seconds > 0) {
                start_polling_feeder(session);
            } else {
                preload_events(session);
            }
            session->worker_thread = std::make_unique<std::thread>(
                [this, session]() { run_session_loop(session); }
            );
            if (was_paused) {
                session->time_engine->pause();
                session->status = SessionStatus::PAUSED;
            }
        }
    }
}

void SessionManager::fast_forward(const std::string& session_id, Timestamp ts) {
    auto session = get_session(session_id);
    if (!session) return;

    bool was_running = session->status == SessionStatus::RUNNING;
    bool was_paused = session->status == SessionStatus::PAUSED;

    session->should_stop.store(true);
    session->time_engine->stop();
    if (session->event_queue) session->event_queue->stop();
    for (auto& t : session->feed_threads) {
        if (t && t->joinable()) t->join();
    }
    session->feed_threads.clear();
    if (session->polling_thread && session->polling_thread->joinable()) {
        session->polling_thread->join();
    }
    session->polling_thread.reset();
    if (session->worker_thread && session->worker_thread->joinable()) {
        session->worker_thread->join();
    }
    session->worker_thread.reset();

    if (session->event_queue) session->event_queue->reset();
    session->should_stop.store(false);

    while (true) {
        auto next = session->event_queue->peek();
        if (!next) break;
        if (next->timestamp > ts) break;
        auto ev_opt = session->event_queue->pop();
        if (!ev_opt) break;
        session->time_engine->set_time(ev_opt->timestamp);
        process_event(session, *ev_opt, false);
    }
    session->time_engine->set_time(ts);

    if (was_running || was_paused) {
        session->status = SessionStatus::RUNNING;
        session->time_engine->start();
        if (exec_cfg_.poll_interval_seconds > 0) {
            start_polling_feeder(session);
        }
        session->worker_thread = std::make_unique<std::thread>(
            [this, session]() { run_session_loop(session); }
        );
        if (was_paused) {
            session->time_engine->pause();
            session->status = SessionStatus::PAUSED;
        }
    }
}

std::optional<int64_t> SessionManager::watermark_ns(const std::string& session_id) const {
    auto session = get_session(session_id);
    if (!session) return std::nullopt;
    return session->last_event_ns.load(std::memory_order_acquire);
}

void SessionManager::save_session_checkpoint(const std::string& session_id) {
    auto session = get_session(session_id);
    if (!session) return;

    std::string wal_dir = exec_cfg_.wal_directory.empty() ? "logs" : exec_cfg_.wal_directory;

    Checkpoint ck;
    ck.session_id = session->id;
    ck.account = session->account_manager->state();
    ck.positions = session->account_manager->positions();
    {
        std::lock_guard<std::mutex> lock(session->orders_mutex);
        ck.orders = session->orders;
    }
    ck.last_event_ns = session->last_event_ns.load(std::memory_order_acquire);
    ck.events_processed = session->events_processed.load(std::memory_order_acquire);

    // Get pending orders from matching engine to preserve their state
    auto pending = session->matching_engine->get_pending_orders();
    for (const auto& ord : pending) {
        ck.orders[ord.id] = ord;
    }

    save_checkpoint(ck, wal_dir);
    session->last_checkpoint_events.store(ck.events_processed, std::memory_order_release);

    // Archive old WAL and cleanup old archives
    int64_t ckpt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    truncate_wal_after_checkpoint(session_id, ckpt_ns, wal_dir);
    cleanup_old_checkpoints(session_id, 3, wal_dir);

    // Recreate WAL logger for new entries
    if (exec_cfg_.enable_wal) {
        session->wal = std::make_unique<WalLogger>(wal_path(wal_dir, session_id));
    }

    spdlog::debug("Saved checkpoint for session {} at {} events", session_id, ck.events_processed);
}

bool SessionManager::restore_session(std::shared_ptr<Session> session) {
    std::string wal_dir = exec_cfg_.wal_directory.empty() ? "logs" : exec_cfg_.wal_directory;

    auto ck = load_checkpoint(session->id, wal_dir);
    if (!ck) return false;

    // Restore account state and positions
    session->account_manager->restore_state(ck->account);
    session->account_manager->restore_positions(ck->positions);

    // Restore orders
    {
        std::lock_guard<std::mutex> lock(session->orders_mutex);
        session->orders = ck->orders;
    }

    // Restore NBBO cache to matching engine
    for (const auto& kv : ck->nbbo_cache) {
        session->matching_engine->update_nbbo(kv.second);
    }

    // Restore pending orders to matching engine
    for (const auto& kv : ck->orders) {
        if (kv.second.status == OrderStatus::ACCEPTED ||
            kv.second.status == OrderStatus::PARTIALLY_FILLED ||
            kv.second.status == OrderStatus::PENDING_NEW) {
            Order ord = kv.second;
            session->matching_engine->submit_order(ord);
        }
    }

    session->last_event_ns.store(ck->last_event_ns, std::memory_order_release);
    session->events_processed.store(ck->events_processed, std::memory_order_release);
    session->last_checkpoint_events.store(ck->events_processed, std::memory_order_release);
    session->cash = ck->account.cash;
    session->equity = ck->account.equity;

    // Replay WAL entries after checkpoint
    replay_wal_entries(session, ck->last_event_ns);

    return true;
}

void SessionManager::maybe_checkpoint(std::shared_ptr<Session> session) {
    if (exec_cfg_.checkpoint_interval_events <= 0) return;

    uint64_t current = session->events_processed.load(std::memory_order_relaxed);
    uint64_t last = session->last_checkpoint_events.load(std::memory_order_relaxed);

    if (current - last >= static_cast<uint64_t>(exec_cfg_.checkpoint_interval_events)) {
        save_session_checkpoint(session->id);
    }
}

void SessionManager::replay_wal_entries(std::shared_ptr<Session> session, int64_t after_ns) {
    std::string wal_dir = exec_cfg_.wal_directory.empty() ? "logs" : exec_cfg_.wal_directory;

    auto entries = load_wal_entries_after(session->id, after_ns, wal_dir);
    if (entries.empty()) return;

    spdlog::info("Replaying {} WAL entries for session {}", entries.size(), session->id);

    for (const auto& entry : entries) {
        if (entry.event_type == "fill") {
            // Replay fill
            Fill fill;
            fill.order_id = entry.data.value("order_id", "");
            fill.fill_qty = entry.data.value("qty", 0.0);
            fill.fill_price = entry.data.value("price", 0.0);
            fill.timestamp = entry.ts_ns;
            fill.is_partial = entry.data.value("is_partial", false);

            if (!fill.order_id.empty() && fill.fill_qty > 0) {
                process_fill(session, fill);
            }
        } else if (entry.event_type == "order_submitted") {
            // Restore order to session
            Order order;
            order.id = entry.data.value("id", "");
            order.symbol = entry.data.value("symbol", "");
            order.side = entry.data.value("side", "BUY") == "BUY" ? OrderSide::BUY : OrderSide::SELL;
            order.type = static_cast<OrderType>(entry.data.value("type", 0));
            order.tif = static_cast<TimeInForce>(entry.data.value("tif", 0));
            order.qty = entry.data.value("qty", 0.0);
            double lp = entry.data.value("limit", 0.0);
            if (lp > 0.0) order.limit_price = lp;
            double sp = entry.data.value("stop", 0.0);
            if (sp > 0.0) order.stop_price = sp;
            order.status = OrderStatus::ACCEPTED;

            if (!order.id.empty()) {
                upsert_order(session, order);
                session->matching_engine->submit_order(order);
            }
        } else if (entry.event_type == "order_canceled") {
            std::string order_id = entry.data.value("id", "");
            if (!order_id.empty()) {
                session->matching_engine->cancel_order(order_id);
                std::lock_guard<std::mutex> lock(session->orders_mutex);
                auto it = session->orders.find(order_id);
                if (it != session->orders.end()) {
                    it->second.status = OrderStatus::CANCELED;
                }
            }
        } else if (entry.event_type == "market_event") {
            // Replay market event for NBBO update
            int type = entry.data.value("type", 0);
            if (type == static_cast<int>(EventType::QUOTE)) {
                NBBO nbbo;
                nbbo.symbol = entry.data.value("symbol", "");
                nbbo.bid_price = entry.data.value("bid_price", 0.0);
                nbbo.bid_size = entry.data.value("bid_size", int64_t{0});
                nbbo.ask_price = entry.data.value("ask_price", 0.0);
                nbbo.ask_size = entry.data.value("ask_size", int64_t{0});
                nbbo.timestamp = entry.ts_ns;

                if (!nbbo.symbol.empty()) {
                    auto result = session->matching_engine->update_nbbo(nbbo);
                    for (auto& f : result.fills) {
                        process_fill(session, f);
                    }
                    session->account_manager->mark_to_market(nbbo.symbol, nbbo.mid_price());
                }
            } else if (type == static_cast<int>(EventType::TRADE)) {
                std::string symbol = entry.data.value("symbol", "");
                double price = entry.data.value("price", 0.0);
                if (!symbol.empty() && price > 0.0) {
                    session->account_manager->mark_to_market(symbol, price);
                }
            }
        } else if (entry.event_type == "dividend") {
            std::string symbol = entry.data.value("symbol", "");
            double amount = entry.data.value("amount_per_share", 0.0);
            if (!symbol.empty()) {
                session->account_manager->apply_dividend(symbol, amount);
            }
        } else if (entry.event_type == "split") {
            std::string symbol = entry.data.value("symbol", "");
            double ratio = entry.data.value("ratio", 1.0);
            if (!symbol.empty() && ratio != 1.0) {
                session->account_manager->apply_split(symbol, ratio);
            }
        }

        session->last_event_ns.store(entry.ts_ns, std::memory_order_release);
    }

    session->equity = session->account_manager->state().equity;
    session->cash = session->account_manager->state().cash;
}

} // namespace broker_sim
