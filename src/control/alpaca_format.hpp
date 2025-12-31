#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "../core/matching_engine.hpp"
#include "../core/account_manager.hpp"
#include "../core/utils.hpp"

namespace broker_sim {
namespace alpaca_format {

inline std::string order_type_to_string(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "market";
        case OrderType::LIMIT: return "limit";
        case OrderType::STOP: return "stop";
        case OrderType::STOP_LIMIT: return "stop_limit";
        case OrderType::TRAILING_STOP: return "trailing_stop";
    }
    return "market";
}

inline std::string tif_to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::DAY: return "day";
        case TimeInForce::GTC: return "gtc";
        case TimeInForce::IOC: return "ioc";
        case TimeInForce::FOK: return "fok";
        case TimeInForce::OPG: return "opg";
        case TimeInForce::CLS: return "cls";
    }
    return "day";
}

inline std::string order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW: return "new";
        case OrderStatus::PENDING_NEW: return "pending_new";
        case OrderStatus::ACCEPTED: return "accepted";
        case OrderStatus::PARTIALLY_FILLED: return "partially_filled";
        case OrderStatus::FILLED: return "filled";
        case OrderStatus::CANCELED: return "canceled";
        case OrderStatus::REJECTED: return "rejected";
        case OrderStatus::EXPIRED: return "expired";
        case OrderStatus::PENDING_CANCEL: return "pending_cancel";
        case OrderStatus::PENDING_REPLACE: return "pending_replace";
    }
    return "new";
}

inline nlohmann::json maybe_iso(int64_t ns) {
    if (ns <= 0) return nullptr;
    return utils::ns_to_iso(ns);
}

inline nlohmann::json format_order(const Order& o) {
    nlohmann::json result;
    result["id"] = o.id;
    result["client_order_id"] = o.client_order_id;
    result["created_at"] = utils::ns_to_iso(o.created_at_ns);
    result["updated_at"] = utils::ns_to_iso(o.updated_at_ns);
    result["submitted_at"] = utils::ns_to_iso(o.submitted_at_ns);
    result["filled_at"] = maybe_iso(o.filled_at_ns);
    result["expired_at"] = maybe_iso(o.expired_at_ns);
    result["canceled_at"] = maybe_iso(o.canceled_at_ns);
    result["failed_at"] = nullptr;
    result["replaced_at"] = nullptr;
    result["replaced_by"] = nullptr;
    result["replaces"] = nullptr;
    result["asset_id"] = o.symbol;
    result["symbol"] = o.symbol;
    result["asset_class"] = "us_equity";
    result["notional"] = nullptr;
    const double qty = o.qty.value_or(0.0);
    result["qty"] = std::to_string(qty);
    result["filled_qty"] = std::to_string(o.filled_qty);
    result["filled_avg_price"] = o.filled_qty > 0
                                 ? nlohmann::json(std::to_string(o.last_fill_price))
                                 : nlohmann::json(nullptr);
    result["order_class"] = "";
    result["order_type"] = order_type_to_string(o.type);
    result["type"] = order_type_to_string(o.type);
    result["side"] = o.side == OrderSide::BUY ? "buy" : "sell";
    result["position_intent"] = o.side == OrderSide::BUY ? "buy_to_open" : "sell_to_close";
    result["time_in_force"] = tif_to_string(o.tif);
    result["limit_price"] = o.limit_price.has_value() ? nlohmann::json(std::to_string(*o.limit_price))
                                                      : nlohmann::json(nullptr);
    result["stop_price"] = o.stop_price.has_value() ? nlohmann::json(std::to_string(*o.stop_price))
                                                    : nlohmann::json(nullptr);
    result["status"] = order_status_to_string(o.status);
    result["extended_hours"] = false;
    result["legs"] = nullptr;
    result["trail_percent"] = o.trail_percent.has_value()
                                  ? nlohmann::json(std::to_string(*o.trail_percent))
                                                          : nlohmann::json(nullptr);
    result["trail_price"] = o.trail_price.has_value()
                                ? nlohmann::json(std::to_string(*o.trail_price))
                                                      : nlohmann::json(nullptr);
    result["hwm"] = o.hwm.has_value() ? nlohmann::json(std::to_string(*o.hwm))
                                      : nlohmann::json(nullptr);
    result["source"] = nullptr;
    result["subtag"] = nullptr;
    result["expires_at"] = o.expire_at.has_value() ? nlohmann::json(utils::ts_to_iso(*o.expire_at))
                                                   : nlohmann::json(nullptr);
    return result;
}

inline nlohmann::json format_position(const Position& p) {
    double current_price = p.qty != 0.0 ? (p.market_value / std::abs(p.qty)) : 0.0;
    double unrealized_plpc = p.cost_basis != 0.0 ? (p.unrealized_pl / p.cost_basis) * 100.0 : 0.0;

    return {
        {"asset_id", p.symbol},
        {"symbol", p.symbol},
        {"exchange", "NASDAQ"},
        {"asset_class", "us_equity"},
        {"asset_marginable", true},
        {"avg_entry_price", p.avg_entry_price},
        {"qty", std::to_string(static_cast<int64_t>(std::abs(p.qty)))},
        {"side", p.qty >= 0 ? "long" : "short"},
        {"market_value", std::to_string(p.market_value)},
        {"cost_basis", std::to_string(p.cost_basis)},
        {"unrealized_pl", std::to_string(p.unrealized_pl)},
        {"unrealized_plpc", std::to_string(unrealized_plpc)},
        {"unrealized_intraday_pl", std::to_string(p.unrealized_pl)},
        {"unrealized_intraday_plpc", std::to_string(unrealized_plpc)},
        {"current_price", std::to_string(current_price)},
        {"lastday_price", std::to_string(current_price)},
        {"change_today", "0"},
        {"qty_available", std::to_string(static_cast<int64_t>(std::abs(p.qty)))}
    };
}

inline nlohmann::json format_account(const AccountState& st, const std::string& session_id) {
    const double position_market_value = st.long_market_value + st.short_market_value;
    const auto now = std::chrono::system_clock::now();
    const std::string account_id =
        session_id.size() >= 32 ? session_id.substr(0, 32) : session_id;
    nlohmann::json result;
    result["id"] = account_id;
    result["account_number"] = session_id;
    result["status"] = "ACTIVE";
    result["crypto_status"] = "ACTIVE";
    result["crypto_tier"] = 1;
    result["currency"] = "USD";
    result["buying_power"] = std::to_string(st.buying_power);
    result["regt_buying_power"] = std::to_string(st.regt_buying_power);
    result["daytrading_buying_power"] = std::to_string(st.daytrading_buying_power);
    result["effective_buying_power"] = std::to_string(st.buying_power);
    result["options_buying_power"] = std::to_string(st.buying_power);
    result["non_marginable_buying_power"] = std::to_string(st.cash);
    result["cash"] = std::to_string(st.cash);
    result["accrued_fees"] = std::to_string(st.accrued_fees);
    result["pending_reg_taf_fees"] = "0";
    result["intraday_adjustments"] = "0";
    result["portfolio_value"] = std::to_string(st.equity);
    result["pattern_day_trader"] = st.pattern_day_trader;
    result["trading_blocked"] = false;
    result["transfers_blocked"] = false;
    result["account_blocked"] = false;
    result["created_at"] = "2024-01-01T00:00:00Z";
    result["balance_asof"] = utils::ts_to_date(now);
    result["trade_suspended_by_user"] = false;
    result["multiplier"] = "2";
    result["shorting_enabled"] = true;
    result["equity"] = std::to_string(st.equity);
    result["last_equity"] = std::to_string(st.equity);
    result["long_market_value"] = std::to_string(st.long_market_value);
    result["short_market_value"] = std::to_string(st.short_market_value);
    result["position_market_value"] = std::to_string(position_market_value);
    result["initial_margin"] = std::to_string(st.initial_margin);
    result["maintenance_margin"] = std::to_string(st.maintenance_margin);
    result["last_maintenance_margin"] = std::to_string(st.maintenance_margin);
    result["bod_dtbp"] = std::to_string(st.daytrading_buying_power);
    result["sma"] = std::to_string(st.cash);
    result["daytrade_count"] = 0;
    result["options_approved_level"] = 0;
    result["options_trading_level"] = 0;
    result["admin_configurations"] = nlohmann::json::object();
    result["user_configurations"] = nullptr;
    return result;
}

} // namespace alpaca_format
} // namespace broker_sim
