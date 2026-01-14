#pragma once

#include <string>
#include <fstream>
#include <optional>
#include <random>
#include <cmath>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace broker_sim {

using json = nlohmann::json;

struct DatabaseConfig {
    std::string host{"localhost"};
    uint16_t port{9000};
    std::string database{"market_data"};
    std::string user{"default"};
    std::string password{};
};

struct PostgresConfig {
    bool enabled{false};
    std::string host{"localhost"};
    uint16_t port{5432};
    std::string database{"broker_sim"};
    std::string user{"postgres"};
    std::string password{};
};

struct ServiceConfig {
    uint16_t control_port{8000};
    uint16_t alpaca_port{8100};
    uint16_t polygon_port{8200};
    uint16_t finnhub_port{8300};
    uint16_t ws_port{8400};
    std::string bind_address{"127.0.0.1"};
};

struct DefaultsConfig {
    double initial_capital{100000.0};
    double speed_factor{0.0}; // 0 = max speed
    int max_sessions{20};
    size_t session_queue_capacity{0};  // 0 = unlimited (for backtest sessions)
    int64_t live_aggr_bar_stream_freq_ms{500};  // milliseconds
};

struct ExecutionConfig {
    // Latency simulation
    bool enable_latency{false};
    int64_t fixed_latency_us{0};           // Fixed latency in microseconds
    int64_t random_latency_max_us{0};      // Max random latency added (uniform distribution)

    // Slippage simulation
    bool enable_slippage{false};
    double fixed_slippage_bps{0.0};        // Fixed slippage in basis points
    double random_slippage_max_bps{0.0};   // Max random slippage (uniform distribution)

    // Market impact simulation
    bool enable_market_impact{false};
    double market_impact_bps{0.0};         // Base market impact in basis points
    double market_impact_per_share{0.0};   // Additional impact per share traded (bps)
    double market_impact_sqrt_coef{0.0};   // Square-root market impact model coefficient

    // Partial fills simulation
    bool enable_partial_fills{true};       // Allow partial fills based on available size
    double partial_fill_probability{1.0};  // Probability of getting a fill at all (0-1)

    // Order rejection simulation
    double rejection_probability{0.0};     // Probability of order rejection (0-1)

    // Position limits
    bool allow_shorting{true};
    double max_position_value{0.0};        // 0 = no limit
    double max_single_order_value{0.0};    // 0 = no limit

    // Margin and risk
    bool enable_margin_call_checks{true};
    bool enable_forced_liquidation{true};
    double maintenance_margin_pct{25.0};   // Maintenance margin requirement %

    // Feed options
    bool enable_shared_feed{false};        // Share data feed across sessions

    // Polling fallback
    int poll_interval_seconds{0};          // >0 enables polling fallback per window

    // Checkpoint/WAL settings
    int checkpoint_interval_events{10000}; // Save checkpoint every N events (0 = disabled)
    bool enable_wal{true};                 // Enable write-ahead logging
    std::string wal_directory{"logs"};     // Directory for WAL and checkpoint files

    // Extended hours trading
    bool enable_extended_hours{true};      // Allow extended hours trading
    bool enforce_market_hours{false};      // Reject orders outside market hours if extended_hours=false

    // Market hours (Eastern Time, minutes from midnight)
    // Pre-market: 4:00 AM - 9:30 AM ET (240 - 570 minutes)
    // Regular:    9:30 AM - 4:00 PM ET (570 - 960 minutes)
    // After-hours: 4:00 PM - 8:00 PM ET (960 - 1200 minutes)
    int premarket_start_minutes{240};      // 4:00 AM ET
    int regular_start_minutes{570};        // 9:30 AM ET
    int regular_end_minutes{960};          // 4:00 PM ET
    int afterhours_end_minutes{1200};      // 8:00 PM ET

    // Extended hours slippage multiplier (typically higher during extended hours)
    double extended_hours_slippage_mult{2.0};  // 2x slippage during extended hours

    // Extended hours liquidity reduction (% of normal liquidity available)
    double extended_hours_liquidity_pct{30.0}; // Only 30% liquidity in extended hours

    // Market holidays (2025 US market holidays - month/day format "MM-DD")
    std::vector<std::string> market_holidays{
        "01-01",  // New Year's Day
        "01-20",  // Martin Luther King Jr. Day
        "02-17",  // Presidents Day
        "04-18",  // Good Friday
        "05-26",  // Memorial Day
        "06-19",  // Juneteenth
        "07-04",  // Independence Day
        "09-01",  // Labor Day
        "11-27",  // Thanksgiving
        "12-25"   // Christmas
    };

    // Short sale restrictions
    bool enable_short_sale_restrictions{true};  // Enable SEC Rule 201 (alternative uptick rule)
    double ssr_threshold_pct{10.0};             // SSR triggered when stock drops 10% from prior close

    // Circuit breakers (LULD - Limit Up Limit Down)
    bool enable_circuit_breakers{true};
    double luld_tier1_pct{5.0};   // Tier 1 (S&P 500, Russell 1000): 5% band
    double luld_tier2_pct{10.0};  // Tier 2 (other NMS stocks): 10% band
    int luld_halt_duration_sec{300};  // 5-minute halt duration

    // Corporate actions
    bool enable_auto_corporate_actions{true};  // Auto-apply dividends/splits from data source

    /**
     * Market session type.
     */
    enum class MarketSession {
        CLOSED,         // Market is closed (overnight, weekends, holidays)
        PREMARKET,      // Pre-market session (4:00 AM - 9:30 AM ET)
        REGULAR,        // Regular trading hours (9:30 AM - 4:00 PM ET)
        AFTERHOURS      // After-hours session (4:00 PM - 8:00 PM ET)
    };

    /**
     * Check if a given date is a market holiday.
     */
    bool is_market_holiday(std::chrono::system_clock::time_point ts) const {
        std::tm tm_et = to_et_tm(ts);
        return is_market_holiday_date(tm_et.tm_mon + 1, tm_et.tm_mday);
    }

    /**
     * Get the current market session for a given timestamp.
     * Assumes timestamp is in UTC, converts to Eastern Time.
     */
    MarketSession get_market_session(std::chrono::system_clock::time_point ts) const {
        std::tm tm_et = to_et_tm(ts);

        // Check for market holidays first
        if (is_market_holiday_date(tm_et.tm_mon + 1, tm_et.tm_mday)) {
            return MarketSession::CLOSED;
        }

        // Check if weekend (Saturday=6, Sunday=0)
        int wday = tm_et.tm_wday;
        if (wday == 0 || wday == 6) {
            return MarketSession::CLOSED;
        }

        int minutes_from_midnight = tm_et.tm_hour * 60 + tm_et.tm_min;

        // Check session based on time
        if (minutes_from_midnight < premarket_start_minutes) {
            return MarketSession::CLOSED;  // Before pre-market
        } else if (minutes_from_midnight < regular_start_minutes) {
            return MarketSession::PREMARKET;
        } else if (minutes_from_midnight < regular_end_minutes) {
            return MarketSession::REGULAR;
        } else if (minutes_from_midnight < afterhours_end_minutes) {
            return MarketSession::AFTERHOURS;
        } else {
            return MarketSession::CLOSED;  // After after-hours
        }
    }

    /**
     * Check if trading is allowed at the given time with the given extended_hours flag.
     */
    bool is_trading_allowed(std::chrono::system_clock::time_point ts, bool extended_hours_order) const {
        if (!enforce_market_hours) {
            return true;  // Not enforcing market hours
        }

        MarketSession session = get_market_session(ts);

        switch (session) {
            case MarketSession::REGULAR:
                return true;  // Always allowed during regular hours
            case MarketSession::PREMARKET:
            case MarketSession::AFTERHOURS:
                return enable_extended_hours && extended_hours_order;
            case MarketSession::CLOSED:
                return false;  // Never allowed when market is closed
        }
        return false;
    }

    /**
     * Get the next market open timestamp (4:00 AM ET) after a given timestamp.
     */
    std::chrono::system_clock::time_point next_market_open_after(std::chrono::system_clock::time_point ts) const {
        std::tm tm_et = to_et_tm(ts);
        int year = tm_et.tm_year + 1900;
        int month = tm_et.tm_mon + 1;
        int day = tm_et.tm_mday;
        int minutes_from_midnight = tm_et.tm_hour * 60 + tm_et.tm_min;

        auto is_trading_day = [this](int y, int m, int d) {
            int wday = day_of_week(y, m, d);
            if (wday == 0 || wday == 6) {
                return false;
            }
            return !is_market_holiday_date(m, d);
        };

        if (is_trading_day(year, month, day) && minutes_from_midnight < premarket_start_minutes) {
            return et_local_to_utc(year, month, day,
                                   premarket_start_minutes / 60,
                                   premarket_start_minutes % 60);
        }

        do {
            add_days(year, month, day, 1);
        } while (!is_trading_day(year, month, day));

        return et_local_to_utc(year, month, day,
                               premarket_start_minutes / 60,
                               premarket_start_minutes % 60);
    }

    /**
     * Check if we're in extended hours (pre-market or after-hours).
     */
    bool is_extended_hours(std::chrono::system_clock::time_point ts) const {
        MarketSession session = get_market_session(ts);
        return session == MarketSession::PREMARKET || session == MarketSession::AFTERHOURS;
    }

    /**
     * Get liquidity multiplier for the current session.
     * Returns 1.0 for regular hours, reduced value for extended hours.
     * Only applies when enforce_market_hours is enabled.
     */
    double get_liquidity_multiplier(std::chrono::system_clock::time_point ts) const {
        if (!enforce_market_hours) {
            return 1.0;  // No market hours enforcement, full liquidity
        }
        if (is_extended_hours(ts)) {
            return extended_hours_liquidity_pct / 100.0;
        }
        return 1.0;
    }

    /**
     * Get slippage multiplier for the current session.
     * Returns 1.0 for regular hours, increased value for extended hours.
     * Only applies when enforce_market_hours is enabled.
     */
    double get_slippage_multiplier(std::chrono::system_clock::time_point ts) const {
        if (!enforce_market_hours) {
            return 1.0;  // No market hours enforcement, normal slippage
        }
        if (is_extended_hours(ts)) {
            return extended_hours_slippage_mult;
        }
        return 1.0;
    }

    /**
     * Calculate total latency in nanoseconds.
     */
    int64_t calculate_latency_ns(std::mt19937_64& rng) const {
        if (!enable_latency) return 0;
        int64_t latency_ns = fixed_latency_us * 1000;
        if (random_latency_max_us > 0) {
            std::uniform_int_distribution<int64_t> dist(0, random_latency_max_us * 1000);
            latency_ns += dist(rng);
        }
        return latency_ns;
    }

    /**
     * Calculate slippage as a multiplier (e.g., 1.0005 for 5bps adverse slippage).
     * For buys: price * slippage > 1 (pay more)
     * For sells: price * slippage < 1 (receive less)
     */
    double calculate_slippage_multiplier(bool is_buy, std::mt19937_64& rng) const {
        if (!enable_slippage) return 1.0;
        double slippage_bps = fixed_slippage_bps;
        if (random_slippage_max_bps > 0) {
            std::uniform_real_distribution<double> dist(0.0, random_slippage_max_bps);
            slippage_bps += dist(rng);
        }
        double multiplier = 1.0 + (slippage_bps / 10000.0);
        return is_buy ? multiplier : (2.0 - multiplier);  // Adverse direction
    }

    /**
     * Calculate market impact price adjustment.
     * Uses a combination of linear and square-root impact models.
     */
    double calculate_market_impact(double qty, double price, bool is_buy) const {
        if (!enable_market_impact) return 0.0;
        double notional = qty * price;
        double impact_bps = market_impact_bps;
        impact_bps += qty * market_impact_per_share;
        if (market_impact_sqrt_coef > 0.0) {
            impact_bps += market_impact_sqrt_coef * std::sqrt(notional / 1000000.0);
        }
        double impact_amount = price * (impact_bps / 10000.0);
        return is_buy ? impact_amount : -impact_amount;
    }

private:
    static int day_of_week(int year, int month, int day) {
        static int table[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (month < 3) {
            year -= 1;
        }
        return (year + year / 4 - year / 100 + year / 400 + table[month - 1] + day) % 7;
    }

    static int nth_weekday_of_month(int year, int month, int weekday, int nth) {
        int first_wday = day_of_week(year, month, 1);
        int day = 1 + ((7 + weekday - first_wday) % 7);
        day += (nth - 1) * 7;
        return day;
    }

    static void add_days(int& year, int& month, int& day, int delta) {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day + delta;
        auto tt = timegm(&tm);
        std::tm tm_out = *std::gmtime(&tt);
        year = tm_out.tm_year + 1900;
        month = tm_out.tm_mon + 1;
        day = tm_out.tm_mday;
    }

    static bool is_us_dst_local(int year, int month, int day, int hour, int minute) {
        int dst_start_day = nth_weekday_of_month(year, 3, 0, 2);
        int dst_end_day = nth_weekday_of_month(year, 11, 0, 1);

        if (month < 3 || month > 11) {
            return false;
        }
        if (month > 3 && month < 11) {
            return true;
        }
        if (month == 3) {
            if (day > dst_start_day) {
                return true;
            }
            if (day < dst_start_day) {
                return false;
            }
            return (hour > 2) || (hour == 2 && minute >= 0);
        }
        if (month == 11) {
            if (day < dst_end_day) {
                return true;
            }
            if (day > dst_end_day) {
                return false;
            }
            return hour < 2;
        }
        return false;
    }

    static std::chrono::system_clock::time_point utc_time_point(int year, int month, int day,
                                                                int hour, int minute, int sec) {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = sec;
        auto tt = timegm(&tm);
        return std::chrono::system_clock::from_time_t(tt);
    }

    static bool is_us_dst_utc(std::chrono::system_clock::time_point ts) {
        auto tt = std::chrono::system_clock::to_time_t(ts);
        std::tm tm_utc = *std::gmtime(&tt);
        int year = tm_utc.tm_year + 1900;
        int dst_start_day = nth_weekday_of_month(year, 3, 0, 2);
        int dst_end_day = nth_weekday_of_month(year, 11, 0, 1);

        auto dst_start = utc_time_point(year, 3, dst_start_day, 7, 0, 0);
        auto dst_end = utc_time_point(year, 11, dst_end_day, 6, 0, 0);
        return ts >= dst_start && ts < dst_end;
    }

    static int et_offset_minutes(std::chrono::system_clock::time_point ts) {
        return is_us_dst_utc(ts) ? -240 : -300;
    }

    static std::tm to_et_tm(std::chrono::system_clock::time_point ts) {
        int offset_min = et_offset_minutes(ts);
        auto adjusted = ts + std::chrono::minutes(offset_min);
        auto tt = std::chrono::system_clock::to_time_t(adjusted);
        return *std::gmtime(&tt);
    }

    static std::chrono::system_clock::time_point et_local_to_utc(int year, int month, int day,
                                                                 int hour, int minute) {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;
        auto tt = timegm(&tm);
        int offset_min = is_us_dst_local(year, month, day, hour, minute) ? -240 : -300;
        return std::chrono::system_clock::from_time_t(tt) - std::chrono::minutes(offset_min);
    }

    bool is_market_holiday_date(int month, int day) const {
        char date_str[6];
        std::snprintf(date_str, sizeof(date_str), "%02d-%02d", month, day);
        std::string date_key(date_str);

        for (const auto& holiday : market_holidays) {
            if (holiday == date_key) {
                return true;
            }
        }
        return false;
    }
};

struct FeeConfig {
    double per_share_commission{0.0};
    double per_order_commission{0.0};
    double sec_fee_per_million{27.80};
    double taf_fee_per_share{0.000166};
    double finra_taf_cap{8.30};
    double maker_rebate_per_share{0.0};
    double taker_fee_per_share{0.0};

    double calculate_fees(double qty, double price, bool is_sell, bool is_maker) const {
        double fees = per_order_commission + (qty * per_share_commission);
        if (is_sell) {
            double notional = qty * price;
            fees += notional * sec_fee_per_million / 1000000.0;
            fees += std::min(qty * taf_fee_per_share, finra_taf_cap);
        }
        if (is_maker) {
            fees += qty * maker_rebate_per_share;
        } else {
            fees += qty * taker_fee_per_share;
        }
        return fees;
    }
};

struct WebsocketConfig {
    int queue_size{1000};
    std::string overflow_policy{"drop_oldest"};
    int batch_size{50};
    int flush_interval_ms{20};
};

struct LoggingConfig {
    std::string level{"info"};
    std::string format{"json"};
    std::string file{"/var/log/broker_simulator/simulator.log"};
};

struct AuthConfig {
    std::string token{};
};

struct Config {
    DatabaseConfig database;    // ClickHouse config (aliased as "clickhouse" in JSON)
    PostgresConfig postgres;    // PostgreSQL config for Alpaca persistence
    ServiceConfig services;
    DefaultsConfig defaults;
    ExecutionConfig execution;
    FeeConfig fees;
    WebsocketConfig websocket;
    LoggingConfig logging;
    AuthConfig auth;
};

inline void load_config(Config& cfg, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::warn("Config file {} not found, using defaults", path);
        return;
    }
    json j = json::parse(f, nullptr, true, true);
    // ClickHouse config: prefer "clickhouse" key, fall back to "database" for backward compatibility
    if (j.contains("clickhouse")) {
        auto& db = j["clickhouse"];
        cfg.database.host = db.value("host", cfg.database.host);
        cfg.database.port = db.value("port", cfg.database.port);
        cfg.database.database = db.value("database", cfg.database.database);
        cfg.database.user = db.value("user", cfg.database.user);
        cfg.database.password = db.value("password", cfg.database.password);
    } else if (j.contains("database")) {
        auto& db = j["database"];
        cfg.database.host = db.value("host", cfg.database.host);
        cfg.database.port = db.value("port", cfg.database.port);
        cfg.database.database = db.value("database", cfg.database.database);
        cfg.database.user = db.value("user", cfg.database.user);
        cfg.database.password = db.value("password", cfg.database.password);
    }
    // PostgreSQL config for Alpaca account persistence
    if (j.contains("postgres")) {
        auto& pg = j["postgres"];
        cfg.postgres.enabled = pg.value("enabled", cfg.postgres.enabled);
        cfg.postgres.host = pg.value("host", cfg.postgres.host);
        cfg.postgres.port = pg.value("port", cfg.postgres.port);
        cfg.postgres.database = pg.value("database", cfg.postgres.database);
        cfg.postgres.user = pg.value("user", cfg.postgres.user);
        cfg.postgres.password = pg.value("password", cfg.postgres.password);
    }
    if (j.contains("services")) {
        auto& svc = j["services"];
        cfg.services.control_port = svc.value("control_port", cfg.services.control_port);
        cfg.services.alpaca_port = svc.value("alpaca_port", cfg.services.alpaca_port);
        cfg.services.polygon_port = svc.value("polygon_port", cfg.services.polygon_port);
        cfg.services.finnhub_port = svc.value("finnhub_port", cfg.services.finnhub_port);
        cfg.services.bind_address = svc.value("bind_address", cfg.services.bind_address);
    }
    if (j.contains("defaults")) {
        auto& d = j["defaults"];
        cfg.defaults.initial_capital = d.value("initial_capital", cfg.defaults.initial_capital);
        cfg.defaults.speed_factor = d.value("speed_factor", cfg.defaults.speed_factor);
        cfg.defaults.max_sessions = d.value("max_sessions", cfg.defaults.max_sessions);
        cfg.defaults.session_queue_capacity = d.value("session_queue_capacity", cfg.defaults.session_queue_capacity);
        cfg.defaults.live_aggr_bar_stream_freq_ms = d.value("live_aggr_bar_stream_freq_ms", cfg.defaults.live_aggr_bar_stream_freq_ms);
        cfg.defaults.live_aggr_bar_stream_freq_ms = d.value("live_aggr_bar_stream_freq", cfg.defaults.live_aggr_bar_stream_freq_ms);
    }
    if (j.contains("execution")) {
        auto& e = j["execution"];
        cfg.execution.enable_latency = e.value("enable_latency", cfg.execution.enable_latency);
        cfg.execution.fixed_latency_us = e.value("fixed_latency_us", cfg.execution.fixed_latency_us);
        cfg.execution.enable_slippage = e.value("enable_slippage", cfg.execution.enable_slippage);
        cfg.execution.fixed_slippage_bps = e.value("fixed_slippage_bps", cfg.execution.fixed_slippage_bps);
        cfg.execution.poll_interval_seconds = e.value("poll_interval_seconds", cfg.execution.poll_interval_seconds);
        cfg.execution.enable_margin_call_checks = e.value("enable_margin_call_checks",
                                                         cfg.execution.enable_margin_call_checks);
        cfg.execution.enable_forced_liquidation = e.value("enable_forced_liquidation",
                                                         cfg.execution.enable_forced_liquidation);
        cfg.execution.enable_shared_feed = e.value("enable_shared_feed",
                                                   cfg.execution.enable_shared_feed);
        cfg.execution.enable_market_impact = e.value("enable_market_impact",
                                                    cfg.execution.enable_market_impact);
        cfg.execution.market_impact_bps = e.value("market_impact_bps",
                                                 cfg.execution.market_impact_bps);
        // Extended hours settings
        cfg.execution.enable_extended_hours = e.value("enable_extended_hours",
                                                      cfg.execution.enable_extended_hours);
        cfg.execution.enforce_market_hours = e.value("enforce_market_hours",
                                                     cfg.execution.enforce_market_hours);
        cfg.execution.premarket_start_minutes = e.value("premarket_start_minutes",
                                                        cfg.execution.premarket_start_minutes);
        cfg.execution.regular_start_minutes = e.value("regular_start_minutes",
                                                      cfg.execution.regular_start_minutes);
        cfg.execution.regular_end_minutes = e.value("regular_end_minutes",
                                                    cfg.execution.regular_end_minutes);
        cfg.execution.afterhours_end_minutes = e.value("afterhours_end_minutes",
                                                       cfg.execution.afterhours_end_minutes);
        cfg.execution.extended_hours_slippage_mult = e.value("extended_hours_slippage_mult",
                                                             cfg.execution.extended_hours_slippage_mult);
        cfg.execution.extended_hours_liquidity_pct = e.value("extended_hours_liquidity_pct",
                                                             cfg.execution.extended_hours_liquidity_pct);
    }
    if (j.contains("fees")) {
        auto& f = j["fees"];
        cfg.fees.per_share_commission = f.value("per_share_commission", cfg.fees.per_share_commission);
        cfg.fees.per_order_commission = f.value("per_order_commission", cfg.fees.per_order_commission);
        cfg.fees.sec_fee_per_million = f.value("sec_fee_per_million", cfg.fees.sec_fee_per_million);
        cfg.fees.taf_fee_per_share = f.value("taf_fee_per_share", cfg.fees.taf_fee_per_share);
        cfg.fees.finra_taf_cap = f.value("finra_taf_cap", cfg.fees.finra_taf_cap);
        cfg.fees.maker_rebate_per_share = f.value("maker_rebate_per_share", cfg.fees.maker_rebate_per_share);
        cfg.fees.taker_fee_per_share = f.value("taker_fee_per_share", cfg.fees.taker_fee_per_share);
    }
    if (j.contains("websocket")) {
        auto& w = j["websocket"];
        cfg.websocket.queue_size = w.value("queue_size", cfg.websocket.queue_size);
        cfg.websocket.overflow_policy = w.value("overflow_policy", cfg.websocket.overflow_policy);
        cfg.websocket.batch_size = w.value("batch_size", cfg.websocket.batch_size);
        cfg.websocket.flush_interval_ms = w.value("flush_interval_ms", cfg.websocket.flush_interval_ms);
    }
    if (j.contains("logging")) {
        auto& l = j["logging"];
        cfg.logging.level = l.value("level", cfg.logging.level);
        cfg.logging.format = l.value("format", cfg.logging.format);
        cfg.logging.file = l.value("file", cfg.logging.file);
    }
    if (j.contains("auth")) {
        auto& a = j["auth"];
        cfg.auth.token = a.value("token", cfg.auth.token);
    }
}

} // namespace broker_sim
