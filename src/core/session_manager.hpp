#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <random>
#include <fstream>
#include <filesystem>

#include "time_engine.hpp"
#include "event_queue.hpp"
#include "matching_engine.hpp"
#include "account_manager.hpp"
#include "performance.hpp"
#include "data_source.hpp"
#include "config.hpp"
#include "wal_logger.hpp"

namespace broker_sim {

struct SessionConfig {
    std::vector<std::string> symbols;
    Timestamp start_time;
    Timestamp end_time;
    double initial_capital{100000.0};
    double speed_factor{0.0};
    size_t queue_capacity{0};
    std::string overflow_policy{"block"};
};

enum class SessionStatus { CREATED, RUNNING, PAUSED, STOPPED, COMPLETED, ERROR };

struct Session {
    std::string id;
    SessionConfig config;
    std::shared_ptr<TimeEngine> time_engine;
    std::shared_ptr<EventQueue> event_queue;
    std::shared_ptr<MatchingEngine> matching_engine;
    std::shared_ptr<AccountManager> account_manager;
    std::shared_ptr<PerformanceTracker> perf;
    SessionStatus status{SessionStatus::CREATED};
    Timestamp created_at;
    std::optional<Timestamp> started_at;
    std::optional<Timestamp> completed_at;
    double cash;
    double equity;
    std::atomic<int64_t> last_event_ns{0};
    std::atomic<bool> margin_call_active{false};
    std::atomic<uint64_t> events_enqueued{0};
    std::atomic<uint64_t> events_dropped{0};
    std::atomic<uint64_t> events_processed{0};
    std::atomic<uint64_t> last_checkpoint_events{0};
    std::vector<std::unique_ptr<std::thread>> feed_threads;
    std::unique_ptr<std::thread> polling_thread;
    std::unordered_map<std::string, Order> orders;
    std::mutex orders_mutex;
    std::unique_ptr<WalLogger> wal;
    std::mutex wal_mutex;
    std::unique_ptr<std::thread> worker_thread;
    std::atomic<bool> should_stop{false};

    // Circuit breaker / halt tracking
    std::unordered_set<std::string> halted_symbols;
    std::unordered_map<std::string, Timestamp> halt_end_times;
    std::mutex halt_mutex;

    // Short Sale Restriction (SSR) tracking - SEC Rule 201
    std::unordered_set<std::string> ssr_symbols;  // Symbols under SSR
    std::unordered_map<std::string, double> prior_close;  // Prior day close prices
    std::mutex ssr_mutex;

    // LULD reference prices (for circuit breaker calculation)
    std::unordered_map<std::string, double> luld_reference_price;
    std::unordered_map<std::string, double> luld_upper_band;
    std::unordered_map<std::string, double> luld_lower_band;

    Session(const std::string& session_id, const SessionConfig& cfg);
    ~Session();
    void stop();
};

class SessionManager {
public:
    using EventCallback = std::function<void(const std::string&, const Event&)>;

    explicit SessionManager(std::shared_ptr<DataSource> data_source = nullptr,
                            ExecutionConfig exec_cfg = {},
                            FeeConfig fee_cfg = {},
                            std::shared_ptr<DataSource> api_data_source = nullptr);
    ~SessionManager();

    std::shared_ptr<Session> create_session(const SessionConfig& config,
                                            std::optional<std::string> session_id = std::nullopt);
    std::shared_ptr<Session> get_session(const std::string& session_id) const;
    std::vector<std::shared_ptr<Session>> list_sessions() const;
    void start_session(const std::string& session_id);
    void pause_session(const std::string& session_id);
    void resume_session(const std::string& session_id);
    void stop_session(const std::string& session_id);
    void destroy_session(const std::string& session_id);
    std::string submit_order(const std::string& session_id, Order order);
    bool cancel_order(const std::string& session_id, const std::string& order_id);
    std::unordered_map<std::string, Order> get_orders(const std::string& session_id) const;
    void add_event_callback(EventCallback cb);
    void set_speed(const std::string& session_id, double speed);
    void jump_to(const std::string& session_id, Timestamp ts);
    void fast_forward(const std::string& session_id, Timestamp ts);
    std::optional<int64_t> watermark_ns(const std::string& session_id) const;
    std::shared_ptr<DataSource> data_source() const { return data_source_; }
    std::shared_ptr<DataSource> api_data_source() const { return api_data_source_; }
    bool apply_dividend(const std::string& session_id, const std::string& symbol, double amount_per_share);
    bool apply_split(const std::string& session_id, const std::string& symbol, double split_ratio);

    /**
     * Save checkpoint for a session (for crash recovery).
     */
    void save_session_checkpoint(const std::string& session_id);

    /**
     * Restore session from checkpoint and replay WAL.
     */
    bool restore_session(std::shared_ptr<Session> session);

private:
    void run_session_loop(std::shared_ptr<Session> session);
    void process_event(std::shared_ptr<Session> session, const Event& event, bool emit_callbacks);
    void process_fill(std::shared_ptr<Session> session, const Fill& fill);
    void stop_feeds(std::shared_ptr<Session> session);
    void preload_events(std::shared_ptr<Session> session);
    void start_polling_feeder(std::shared_ptr<Session> session);
    void start_shared_feeder();
    void stop_shared_feeder();
    bool enqueue_event(std::shared_ptr<Session> session, const MarketEvent& ev);
    std::optional<Order> find_order(std::shared_ptr<Session> session, const std::string& order_id);
    void upsert_order(std::shared_ptr<Session> session, const Order& order);
    void append_event_log(const std::string& session_id, const std::string& payload);
    void enforce_margin(std::shared_ptr<Session> session);
    void maybe_checkpoint(std::shared_ptr<Session> session);
    void replay_wal_entries(std::shared_ptr<Session> session, int64_t after_ns);
    static std::string generate_uuid();

    ExecutionConfig exec_cfg_;
    FeeConfig fee_cfg_;
    std::shared_ptr<DataSource> data_source_;      // For session streaming (stream_events)
    std::shared_ptr<DataSource> api_data_source_;  // For API queries (get_quotes, get_trades, etc.)
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::ofstream> session_logs_;
    std::mutex log_mutex_;
    std::vector<EventCallback> event_callbacks_;
    std::unique_ptr<std::thread> shared_feed_thread_;
    std::atomic<bool> shared_feed_running_{false};
};

} // namespace broker_sim
