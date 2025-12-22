#include "account_manager.hpp"
#include <algorithm>

namespace broker_sim {

AccountManager::AccountManager(double initial_cash) {
    state_.cash = initial_cash;
    recompute_equity();
}

Position AccountManager::apply_fill(const std::string& symbol, const Fill& fill, OrderSide side, double fees) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[symbol];
    pos.symbol = symbol;
    double qty_change = side == OrderSide::BUY ? fill.fill_qty : -fill.fill_qty;

    double old_qty = pos.qty;
    double new_qty = old_qty + qty_change;

    if (new_qty == 0.0) {
        pos.qty = 0.0;
        pos.avg_entry_price = 0.0;
        pos.market_value = 0.0;
        pos.cost_basis = 0.0;
        pos.unrealized_pl = 0.0;
    } else if ((old_qty >= 0 && new_qty > 0) || (old_qty <= 0 && new_qty < 0)) {
        // Same direction; adjust average price
        double total_cost = pos.avg_entry_price * old_qty + fill.fill_price * qty_change;
        pos.qty = new_qty;
        pos.avg_entry_price = total_cost / pos.qty;
    } else {
        // Reducing or flipping position; avg cost stays unless flip
        pos.qty = new_qty;
        if ((old_qty > 0 && new_qty < 0) || (old_qty < 0 && new_qty > 0)) {
            pos.avg_entry_price = fill.fill_price;
        }
    }

    // Cash update
    double cash_delta = side == OrderSide::BUY ? -fill.fill_qty * fill.fill_price : fill.fill_qty * fill.fill_price;
    state_.cash += cash_delta;
    state_.cash -= fees;
    state_.accrued_fees += fees;

    mark_to_market_locked(symbol, fill.fill_price);
    recompute_equity();
    return pos;
}

void AccountManager::mark_to_market(const std::string& symbol, double last_price) {
    std::lock_guard<std::mutex> lock(mutex_);
    mark_to_market_locked(symbol, last_price);
    recompute_equity();
}

void AccountManager::recompute_equity() {
    state_.long_market_value = 0.0;
    state_.short_market_value = 0.0;
    for (auto& kv : positions_) {
        if (kv.second.qty >= 0)
            state_.long_market_value += kv.second.market_value;
        else
            state_.short_market_value += std::abs(kv.second.market_value);
    }
    state_.equity = state_.cash + state_.long_market_value - state_.short_market_value;
    // Reg-T buying power: 2x equity if margin allowed; PDT 4x intraday
    state_.regt_buying_power = state_.equity * 2.0;
    state_.daytrading_buying_power = state_.equity >= pdt_threshold_ ? state_.equity * 4.0 : 0.0;
    state_.buying_power = state_.equity >= pdt_threshold_ ? state_.daytrading_buying_power
                                                          : state_.regt_buying_power;
    state_.initial_margin = std::max(state_.long_market_value, state_.short_market_value) * initial_margin_rate_;
    state_.maintenance_margin = std::max(state_.long_market_value, state_.short_market_value) * maintenance_margin_rate_;
    state_.pattern_day_trader = state_.equity >= pdt_threshold_;
}

bool AccountManager::has_buying_power(double notional, bool is_long) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Quick notional check vs computed buying power.
    if (notional > state_.buying_power) return false;
    // Projected book for initial margin requirement.
    double projected_long = state_.long_market_value + (is_long ? notional : 0.0);
    double projected_short = state_.short_market_value + (is_long ? 0.0 : notional);
    double projected_equity = state_.equity; // equity roughly unchanged by margin purchase
    double projected_initial_margin = std::max(projected_long, projected_short) * initial_margin_rate_;
    if (projected_equity < projected_initial_margin) return false;
    return true;
}

AccountState AccountManager::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::unordered_map<std::string, Position> AccountManager::positions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positions_;
}

void AccountManager::mark_to_market_locked(const std::string& symbol, double last_price) {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return;
    auto& pos = it->second;
    pos.market_value = pos.qty * last_price;
    pos.cost_basis = pos.qty * pos.avg_entry_price;
    pos.unrealized_pl = pos.market_value - pos.cost_basis;
}

void AccountManager::apply_dividend(const std::string& symbol, double amount_per_share) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return;
    double cash_delta = it->second.qty * amount_per_share;
    state_.cash += cash_delta;
    recompute_equity();
}

void AccountManager::apply_split(const std::string& symbol, double split_ratio) {
    if (split_ratio <= 0.0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return;
    auto& pos = it->second;
    pos.qty *= split_ratio;
    pos.avg_entry_price /= split_ratio;
    pos.market_value = pos.qty * pos.avg_entry_price;
    pos.cost_basis = pos.qty * pos.avg_entry_price;
    pos.unrealized_pl = pos.market_value - pos.cost_basis;
    recompute_equity();
}

void AccountManager::restore_state(const AccountState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
}

void AccountManager::restore_positions(const std::unordered_map<std::string, Position>& positions) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_ = positions;
    recompute_equity();
}

AccountState& AccountManager::state_mutable() {
    return state_;
}

std::unordered_map<std::string, Position>& AccountManager::positions_mutable() {
    return positions_;
}

} // namespace broker_sim
