#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <libpq-fe.h>
#include <spdlog/spdlog.h>
#include "config.hpp"
#include "account_manager.hpp"
#include "matching_engine.hpp"

namespace broker_sim {

/**
 * PostgreSQL-based account persistence for Alpaca trading simulation.
 *
 * Provides persistence for:
 * - Account state (cash, buying power, margin)
 * - Positions (symbol, qty, avg_entry_price)
 * - Orders (order history with status)
 * - Fills (execution history)
 */
class PostgresStore {
public:
    explicit PostgresStore(const PostgresConfig& config);
    ~PostgresStore();

    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const;
    bool ensure_schema();  // Create tables if not exist

    // Account operations
    std::optional<std::string> create_account(const std::string& session_id, double initial_cash);
    std::optional<AccountState> load_account(const std::string& session_id);
    bool save_account(const std::string& session_id, const AccountState& state);
    bool delete_account(const std::string& session_id);

    // Position operations
    std::unordered_map<std::string, Position> load_positions(const std::string& session_id);
    bool save_position(const std::string& session_id, const Position& pos);
    bool delete_positions(const std::string& session_id);

    // Order operations
    bool save_order(const std::string& session_id, const Order& order);
    bool update_order_status(const std::string& order_id, const std::string& status,
                             double filled_qty, double filled_avg_price);
    std::vector<Order> load_orders(const std::string& session_id,
                                    const std::string& status_filter = "");

    // Fill operations
    bool save_fill(const std::string& session_id, const std::string& order_id,
                   const std::string& symbol, const std::string& side,
                   double qty, double price, double commission);
    std::vector<Fill> load_fills(const std::string& session_id);

    // Get the account_id (UUID) for a session
    std::optional<std::string> get_account_id(const std::string& session_id);

private:
    PostgresConfig config_;
    PGconn* conn_{nullptr};

    bool exec_sql(const std::string& sql);
    PGresult* query(const std::string& sql);
    std::string escape(const std::string& str);
};

/**
 * RAII wrapper for PostgreSQL connection in multi-session environment.
 */
class PostgresStoreFactory {
public:
    static std::shared_ptr<PostgresStore> create(const PostgresConfig& config) {
        if (!config.enabled) {
            return nullptr;
        }
        auto store = std::make_shared<PostgresStore>(config);
        if (!store->connect()) {
            spdlog::warn("Failed to connect to PostgreSQL, running without persistence");
            return nullptr;
        }
        if (!store->ensure_schema()) {
            spdlog::warn("Failed to create PostgreSQL schema");
            return nullptr;
        }
        spdlog::info("Connected to PostgreSQL {}:{} db={}",
                     config.host, config.port, config.database);
        return store;
    }
};

} // namespace broker_sim
