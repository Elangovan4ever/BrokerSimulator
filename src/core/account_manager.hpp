#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include "matching_engine.hpp"

namespace broker_sim {

struct Position {
    std::string symbol;
    double qty{0.0};
    double avg_entry_price{0.0};
    double market_value{0.0};
    double cost_basis{0.0};
    double unrealized_pl{0.0};
};

struct AccountState {
    double cash{0.0};
    double buying_power{0.0};
    double regt_buying_power{0.0};
    double daytrading_buying_power{0.0};
    double equity{0.0};
    double long_market_value{0.0};
    double short_market_value{0.0};
    double initial_margin{0.0};
    double maintenance_margin{0.0};
    double accrued_fees{0.0};
    bool pattern_day_trader{false};
};

class AccountManager {
public:
    explicit AccountManager(double initial_cash = 100000.0);

    // Apply a fill to positions/cash; returns updated position.
    Position apply_fill(const std::string& symbol, const Fill& fill, OrderSide side, double fees = 0.0);

    AccountState state() const;
    std::unordered_map<std::string, Position> positions() const;

    // Update market values given latest price.
    void mark_to_market(const std::string& symbol, double last_price);

    // Buying power check (Reg-T 50% initial, 25% maintenance, PDT 4x intraday if equity>=25k).
    bool has_buying_power(double notional, bool is_long) const;

    void apply_dividend(const std::string& symbol, double amount_per_share);
    void apply_split(const std::string& symbol, double split_ratio);

    /**
     * Restore state from checkpoint (for crash recovery).
     */
    void restore_state(const AccountState& state);
    void restore_positions(const std::unordered_map<std::string, Position>& positions);

    /**
     * Mutable access to state/positions (for restoration).
     */
    AccountState& state_mutable();
    std::unordered_map<std::string, Position>& positions_mutable();

private:
    mutable std::mutex mutex_;
    void recompute_equity();
    void mark_to_market_locked(const std::string& symbol, double last_price);

    AccountState state_;
    std::unordered_map<std::string, Position> positions_;
    double initial_margin_rate_{0.5};     // 50% initial
    double maintenance_margin_rate_{0.25}; // 25% maintenance
    double pdt_threshold_{25000.0};
};

} // namespace broker_sim
