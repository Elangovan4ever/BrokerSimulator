#include "matching_engine.hpp"

namespace broker_sim {

void MatchingEngine::set_config(const ExecutionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void MatchingEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_nbbo_.clear();
    pending_orders_.clear();
}

MatchingEngine::MatchResult MatchingEngine::update_nbbo(const NBBO& nbbo) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_nbbo_[nbbo.symbol] = nbbo;
    MatchResult result;

    for (auto it = pending_orders_.begin(); it != pending_orders_.end(); ) {
        if (it->second.symbol != nbbo.symbol) {
            ++it;
            continue;
        }

        // Check for expired orders based on NBBO timestamp
        if (it->second.expire_at) {
            Timestamp nbbo_ts = Timestamp{} + std::chrono::nanoseconds(nbbo.timestamp);
            if (nbbo_ts > *(it->second.expire_at)) {
                it->second.status = OrderStatus::EXPIRED;
                it->second.expired_at_ns = nbbo.timestamp;
                result.expired.push_back(it->second);
                it = pending_orders_.erase(it);
                continue;
            }
        }

        auto fill = try_fill(it->second, nbbo);
        if (fill) {
            result.fills.push_back(*fill);
            if (!fill->is_partial) {
                it = pending_orders_.erase(it);
                continue;
            }
        }
        ++it;
    }
    return result;
}

std::optional<Fill> MatchingEngine::submit_order(Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check for random rejection
    if (should_reject_order()) {
        order.status = OrderStatus::REJECTED;
        return std::nullopt;
    }

    auto it = current_nbbo_.find(order.symbol);
    if (it == current_nbbo_.end()) {
        // No NBBO available, queue the order
        order.status = OrderStatus::ACCEPTED;
        pending_orders_[order.id] = order;
        return std::nullopt;
    }

    return try_fill(order, it->second);
}

std::optional<Fill> MatchingEngine::submit_order_with_latency(Order& order, int64_t current_time_ns) {
    // Calculate latency and set minimum execution time
    int64_t latency_ns = config_.calculate_latency_ns(rng_);
    order.min_exec_timestamp = current_time_ns + latency_ns;

    return submit_order(order);
}

bool MatchingEngine::cancel_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_orders_.find(order_id);
    if (it != pending_orders_.end()) {
        it->second.status = OrderStatus::CANCELED;
        pending_orders_.erase(it);
        return true;
    }
    return false;
}

std::optional<NBBO> MatchingEngine::get_nbbo(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = current_nbbo_.find(symbol);
    if (it != current_nbbo_.end()) return it->second;
    return std::nullopt;
}

std::vector<Order> MatchingEngine::get_pending_orders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> out;
    out.reserve(pending_orders_.size());
    for (const auto& kv : pending_orders_) {
        out.push_back(kv.second);
    }
    return out;
}

std::optional<Order> MatchingEngine::get_order(const std::string& order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_orders_.find(order_id);
    if (it != pending_orders_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MatchingEngine::should_reject_order() {
    if (config_.rejection_probability <= 0.0) return false;
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_) < config_.rejection_probability;
}

bool MatchingEngine::should_fill() {
    if (config_.partial_fill_probability >= 1.0) return true;
    if (config_.partial_fill_probability <= 0.0) return false;
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_) < config_.partial_fill_probability;
}

std::string MatchingEngine::validate_market_hours(const Order& order, Timestamp current_time) const {
    if (!config_.enforce_market_hours) {
        return "";  // Not enforcing market hours
    }

    auto session = config_.get_market_session(current_time);

    switch (session) {
        case ExecutionConfig::MarketSession::REGULAR:
            return "";  // Always allowed during regular hours

        case ExecutionConfig::MarketSession::PREMARKET:
        case ExecutionConfig::MarketSession::AFTERHOURS:
            if (!config_.enable_extended_hours) {
                return "extended hours trading is disabled";
            }
            if (!order.extended_hours) {
                return "order not marked for extended hours trading";
            }
            // Only LIMIT orders allowed in extended hours
            if (order.type != OrderType::LIMIT) {
                return "only limit orders allowed during extended hours";
            }
            return "";

        case ExecutionConfig::MarketSession::CLOSED:
            return "market is closed";
    }
    return "unknown market session";
}

int64_t MatchingEngine::apply_extended_hours_liquidity(int64_t available_size, Timestamp current_time) const {
    double liquidity_mult = config_.get_liquidity_multiplier(current_time);
    return static_cast<int64_t>(available_size * liquidity_mult);
}

double MatchingEngine::apply_execution_costs(double base_price, double qty, bool is_buy) {
    double price = base_price;

    // Apply slippage only - market impact is handled by SessionManager.process_fill()
    // with proper size-based scaling (impact_bps * order_qty / available_qty)
    double slippage_mult = config_.calculate_slippage_multiplier(is_buy, rng_);
    price *= slippage_mult;

    return price;
}

std::optional<Fill> MatchingEngine::try_fill(Order& order, const NBBO& nbbo) {
    // Check latency constraint
    if (order.min_exec_timestamp > 0 && nbbo.timestamp < order.min_exec_timestamp) {
        if (tif_allows_enqueue(order)) {
            order.status = OrderStatus::ACCEPTED;
            pending_orders_[order.id] = order;
        }
        return std::nullopt;
    }

    // Check for crossed market (invalid NBBO)
    if (nbbo.bid_price > 0.0 && nbbo.ask_price > 0.0 && nbbo.bid_price >= nbbo.ask_price) {
        if (tif_allows_enqueue(order)) {
            order.status = OrderStatus::ACCEPTED;
            pending_orders_[order.id] = order;
        }
        return std::nullopt;
    }

    // Check fill probability
    if (!should_fill()) {
        if (tif_allows_enqueue(order)) {
            order.status = OrderStatus::ACCEPTED;
            pending_orders_[order.id] = order;
        }
        return std::nullopt;
    }

    switch (order.type) {
        case OrderType::MARKET:
            return execute_market_order(order, nbbo);

        case OrderType::LIMIT:
            if (is_marketable_limit(order, nbbo)) {
                return execute_limit_order(order, nbbo);
            }
            break;

        case OrderType::STOP:
            if (order.stop_triggered || is_stop_triggered(order, nbbo)) {
                order.stop_triggered = true;
                return execute_market_order(order, nbbo);
            }
            break;

        case OrderType::STOP_LIMIT:
            if (order.stop_triggered || is_stop_triggered(order, nbbo)) {
                order.stop_triggered = true;
                if (is_marketable_limit(order, nbbo)) {
                    return execute_limit_order(order, nbbo);
                }
            }
            break;

        case OrderType::TRAILING_STOP:
            update_trailing_stop_hwm(order, nbbo);
            if (order.stop_triggered || is_trailing_stop_triggered(order, nbbo)) {
                order.stop_triggered = true;
                return execute_market_order(order, nbbo);
            }
            break;
    }

    // Order not filled, check if we should enqueue
    if (!tif_allows_enqueue(order)) {
        return std::nullopt;
    }
    order.status = OrderStatus::ACCEPTED;
    pending_orders_[order.id] = order;
    return std::nullopt;
}

Fill MatchingEngine::execute_market_order(Order& order, const NBBO& nbbo) {
    bool is_buy = (order.side == OrderSide::BUY);
    double base_price = is_buy ? nbbo.ask_price : nbbo.bid_price;
    int64_t available_size = is_buy ? nbbo.ask_size : nbbo.bid_size;

    if (base_price <= 0.0 || available_size <= 0) {
        return Fill{order.id, 0.0, base_price, nbbo.timestamp, true};
    }

    // Apply extended hours liquidity reduction
    Timestamp nbbo_ts = Timestamp{} + std::chrono::nanoseconds(nbbo.timestamp);
    available_size = apply_extended_hours_liquidity(available_size, nbbo_ts);

    double remaining = order.qty.value_or(0.0) - order.filled_qty;

    // Determine fill quantity based on available size
    double fill_qty;
    if (config_.enable_partial_fills) {
        fill_qty = std::min(remaining, static_cast<double>(available_size));
    } else {
        fill_qty = remaining;  // Assume infinite liquidity
    }

    // FOK check - must fill entire order or nothing
    if (order.tif == TimeInForce::FOK && config_.enable_partial_fills) {
        if (static_cast<double>(available_size) < remaining) {
            return Fill{order.id, 0.0, base_price, nbbo.timestamp, true};
        }
    }

    // Apply execution costs (slippage + market impact)
    double fill_price = apply_execution_costs(base_price, fill_qty, is_buy);

    bool is_partial = fill_qty < remaining;

    // Update order state
    order.filled_qty += fill_qty;
    order.last_fill_price = fill_price;
    order.updated_at_ns = nbbo.timestamp;

    if (is_partial) {
        order.status = OrderStatus::PARTIALLY_FILLED;
    } else {
        order.status = OrderStatus::FILLED;
        order.filled_at_ns = nbbo.timestamp;
    }

    return Fill{order.id, fill_qty, fill_price, nbbo.timestamp, is_partial};
}

Fill MatchingEngine::execute_limit_order(Order& order, const NBBO& nbbo) {
    bool is_buy = (order.side == OrderSide::BUY);
    double base_price;
    int64_t available_size;

    if (is_buy) {
        // For buy limit, fill at the better of limit price or ask
        base_price = std::min(nbbo.ask_price, order.limit_price.value_or(nbbo.ask_price));
        available_size = nbbo.ask_size;
    } else {
        // For sell limit, fill at the better of limit price or bid
        base_price = std::max(nbbo.bid_price, order.limit_price.value_or(nbbo.bid_price));
        available_size = nbbo.bid_size;
    }

    if (base_price <= 0.0 || available_size <= 0) {
        return Fill{order.id, 0.0, base_price, nbbo.timestamp, true};
    }

    // Apply extended hours liquidity reduction
    Timestamp nbbo_ts = Timestamp{} + std::chrono::nanoseconds(nbbo.timestamp);
    available_size = apply_extended_hours_liquidity(available_size, nbbo_ts);

    double remaining = order.qty.value_or(0.0) - order.filled_qty;

    // Determine fill quantity
    double fill_qty;
    if (config_.enable_partial_fills) {
        fill_qty = std::min(remaining, static_cast<double>(available_size));
    } else {
        fill_qty = remaining;
    }

    // FOK check
    if (order.tif == TimeInForce::FOK && config_.enable_partial_fills) {
        if (static_cast<double>(available_size) < remaining) {
            return Fill{order.id, 0.0, base_price, nbbo.timestamp, true};
        }
    }

    // For limit orders, fill at base_price (no slippage adjustment)
    // Market impact is handled by SessionManager.process_fill() with size-based scaling
    double fill_price = base_price;

    bool is_partial = fill_qty < remaining;

    // Update order state
    order.filled_qty += fill_qty;
    order.last_fill_price = fill_price;
    order.updated_at_ns = nbbo.timestamp;
    order.is_maker = true;  // Limit orders are typically maker

    if (is_partial) {
        order.status = OrderStatus::PARTIALLY_FILLED;
    } else {
        order.status = OrderStatus::FILLED;
        order.filled_at_ns = nbbo.timestamp;
    }

    return Fill{order.id, fill_qty, fill_price, nbbo.timestamp, is_partial};
}

bool MatchingEngine::is_marketable_limit(const Order& order, const NBBO& nbbo) const {
    if (!order.limit_price) return false;
    if (order.side == OrderSide::BUY) {
        return *order.limit_price >= nbbo.ask_price && nbbo.ask_price > 0;
    }
    return *order.limit_price <= nbbo.bid_price && nbbo.bid_price > 0;
}

bool MatchingEngine::is_stop_triggered(const Order& order, const NBBO& nbbo) const {
    if (!order.stop_price) return false;
    double trigger = side_reference_price(order, nbbo);
    if (order.side == OrderSide::BUY) {
        return trigger >= *order.stop_price;
    }
    return trigger <= *order.stop_price;
}

bool MatchingEngine::is_trailing_stop_triggered(const Order& order, const NBBO& nbbo) const {
    if (!order.hwm) return false;
    double trigger = nbbo.mid_price();

    if (order.side == OrderSide::SELL) {
        // Sell trailing stop triggers when price drops from high water mark
        if (order.trail_price) {
            return trigger <= *order.hwm - *order.trail_price;
        }
        if (order.trail_percent) {
            return trigger <= *order.hwm * (1.0 - *order.trail_percent / 100.0);
        }
    } else {
        // Buy trailing stop triggers when price rises from low water mark
        if (order.trail_price) {
            return trigger >= *order.hwm + *order.trail_price;
        }
        if (order.trail_percent) {
            return trigger >= *order.hwm * (1.0 + *order.trail_percent / 100.0);
        }
    }
    return false;
}

void MatchingEngine::update_trailing_stop_hwm(Order& order, const NBBO& nbbo) {
    double current = nbbo.mid_price();
    if (!order.hwm) {
        order.hwm = current;
    } else if (order.side == OrderSide::SELL) {
        // Track highest price for sell trailing stop
        order.hwm = std::max(*order.hwm, current);
    } else {
        // Track lowest price for buy trailing stop
        order.hwm = std::min(*order.hwm, current);
    }
}

bool MatchingEngine::tif_allows_enqueue(const Order& order) const {
    // IOC and FOK orders cannot be enqueued
    if (order.tif == TimeInForce::IOC || order.tif == TimeInForce::FOK) {
        return false;
    }
    // OPG and CLS not currently modeled; treat as DAY
    return true;
}

double MatchingEngine::side_reference_price(const Order& order, const NBBO& nbbo) const {
    return (order.side == OrderSide::BUY) ? nbbo.ask_price : nbbo.bid_price;
}

} // namespace broker_sim
