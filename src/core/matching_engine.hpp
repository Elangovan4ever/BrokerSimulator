#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <random>
#include "event_queue.hpp"
#include "config.hpp"

namespace broker_sim {

enum class OrderSide { BUY, SELL };
enum class OrderType { MARKET, LIMIT, STOP, STOP_LIMIT, TRAILING_STOP };
enum class TimeInForce { DAY, GTC, IOC, FOK, OPG, CLS };
enum class OrderStatus { NEW, PENDING_NEW, ACCEPTED, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED, EXPIRED, PENDING_CANCEL, PENDING_REPLACE };

struct Order {
    std::string id;
    std::string client_order_id;
    std::string symbol;
    OrderSide side;
    OrderType type;
    TimeInForce tif;
    std::optional<Timestamp> expire_at;
    std::optional<double> qty;
    double filled_qty{0.0};
    std::optional<double> limit_price;
    std::optional<double> stop_price;
    std::optional<double> trail_price;
    std::optional<double> trail_percent;
    std::optional<double> hwm;
    bool stop_triggered{false};
    bool is_maker{false};
    bool extended_hours{false};   // Allow execution during pre-market/after-hours
    int64_t min_exec_timestamp{0};
    OrderStatus status{OrderStatus::NEW};
    int64_t created_at_ns{0};
    int64_t submitted_at_ns{0};
    int64_t updated_at_ns{0};
    int64_t filled_at_ns{0};
    int64_t canceled_at_ns{0};
    int64_t expired_at_ns{0};
    double last_fill_price{0.0};
    std::string rejection_reason;  // Reason for rejection if status is REJECTED
};

struct NBBO {
    std::string symbol;
    double bid_price{0.0};
    int64_t bid_size{0};
    double ask_price{0.0};
    int64_t ask_size{0};
    int64_t timestamp{0};
    double mid_price() const { return (bid_price + ask_price) / 2.0; }
};

struct Fill {
    std::string order_id;
    double fill_qty;
    double fill_price;
    int64_t timestamp;
    bool is_partial;
};

/**
 * Order matching engine with realistic execution simulation.
 *
 * Features:
 * - Latency simulation (fixed + random)
 * - Slippage simulation (fixed + random)
 * - Market impact modeling (linear + square-root)
 * - Partial fills based on available liquidity
 * - Order rejection probability
 * - Multiple order types (market, limit, stop, stop-limit, trailing stop)
 * - Time-in-force handling (DAY, GTC, IOC, FOK, OPG, CLS)
 */
class MatchingEngine {
public:
    MatchingEngine() : rng_(std::random_device{}()) {}
    explicit MatchingEngine(const ExecutionConfig& config)
        : config_(config), rng_(std::random_device{}()) {}

    struct MatchResult {
        std::vector<Fill> fills;
        std::vector<Order> expired;
        std::vector<Order> rejected;
    };

    /**
     * Update configuration at runtime.
     */
    void set_config(const ExecutionConfig& config);

    /**
     * Update NBBO and process pending orders.
     */
    MatchResult update_nbbo(const NBBO& nbbo);

    /**
     * Submit a new order for execution.
     * Returns a Fill if immediately executed, nullopt if pending/rejected.
     */
    std::optional<Fill> submit_order(Order& order);

    /**
     * Submit order with latency - sets min_exec_timestamp based on current time.
     */
    std::optional<Fill> submit_order_with_latency(Order& order, int64_t current_time_ns);

    /**
     * Cancel a pending order.
     */
    bool cancel_order(const std::string& order_id);

    /**
     * Get current NBBO for a symbol.
     */
    std::optional<NBBO> get_nbbo(const std::string& symbol) const;

    /**
     * Get all pending orders.
     */
    std::vector<Order> get_pending_orders() const;

    /**
     * Get pending order by ID.
     */
    std::optional<Order> get_order(const std::string& order_id) const;

    /**
     * Clear all pending orders and NBBO data.
     */
    void reset();

private:
    std::optional<Fill> try_fill(Order& order, const NBBO& nbbo);
    Fill execute_market_order(Order& order, const NBBO& nbbo);
    Fill execute_limit_order(Order& order, const NBBO& nbbo);
    bool tif_allows_enqueue(const Order& order) const;
    double side_reference_price(const Order& order, const NBBO& nbbo) const;
    bool is_marketable_limit(const Order& order, const NBBO& nbbo) const;
    bool is_stop_triggered(const Order& order, const NBBO& nbbo) const;
    bool is_trailing_stop_triggered(const Order& order, const NBBO& nbbo) const;
    void update_trailing_stop_hwm(Order& order, const NBBO& nbbo);

    /**
     * Apply slippage and market impact to fill price.
     */
    double apply_execution_costs(double base_price, double qty, bool is_buy);

    /**
     * Check if order should be randomly rejected.
     */
    bool should_reject_order();

    /**
     * Check if fill should occur based on probability.
     */
    bool should_fill();

    /**
     * Validate market hours for order submission.
     * Returns empty string if valid, rejection reason otherwise.
     */
    std::string validate_market_hours(const Order& order, Timestamp current_time) const;

    /**
     * Apply extended hours adjustments to execution.
     * Returns adjusted available size based on reduced liquidity.
     */
    int64_t apply_extended_hours_liquidity(int64_t available_size, Timestamp current_time) const;

    ExecutionConfig config_;
    std::unordered_map<std::string, NBBO> current_nbbo_;
    std::unordered_map<std::string, Order> pending_orders_;
    mutable std::mutex mutex_;
    mutable std::mt19937_64 rng_;
};

} // namespace broker_sim
