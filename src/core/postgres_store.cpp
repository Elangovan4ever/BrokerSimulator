#include "postgres_store.hpp"
#include <spdlog/fmt/fmt.h>
#include <sstream>

namespace broker_sim {

PostgresStore::PostgresStore(const PostgresConfig& config)
    : config_(config) {}

PostgresStore::~PostgresStore() {
    disconnect();
}

bool PostgresStore::connect() {
    if (conn_) {
        return true;  // Already connected
    }

    std::string conn_str = fmt::format(
        "host={} port={} dbname={} user={} password={}",
        config_.host, config_.port, config_.database,
        config_.user, config_.password);

    conn_ = PQconnectdb(conn_str.c_str());

    if (PQstatus(conn_) != CONNECTION_OK) {
        spdlog::error("PostgreSQL connection failed: {}", PQerrorMessage(conn_));
        PQfinish(conn_);
        conn_ = nullptr;
        return false;
    }

    return true;
}

void PostgresStore::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool PostgresStore::is_connected() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

bool PostgresStore::exec_sql(const std::string& sql) {
    if (!is_connected()) return false;

    PGresult* res = PQexec(conn_, sql.c_str());
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK ||
              PQresultStatus(res) == PGRES_TUPLES_OK;

    if (!ok) {
        spdlog::error("PostgreSQL exec failed: {}", PQerrorMessage(conn_));
    }

    PQclear(res);
    return ok;
}

PGresult* PostgresStore::query(const std::string& sql) {
    if (!is_connected()) return nullptr;

    PGresult* res = PQexec(conn_, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("PostgreSQL query failed: {}", PQerrorMessage(conn_));
        PQclear(res);
        return nullptr;
    }
    return res;
}

std::string PostgresStore::escape(const std::string& str) {
    if (!conn_) return str;
    char* escaped = PQescapeLiteral(conn_, str.c_str(), str.size());
    if (!escaped) return "''";
    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

bool PostgresStore::ensure_schema() {
    // Create tables if they don't exist
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS alpaca_accounts (
            account_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            session_id VARCHAR(64) UNIQUE NOT NULL,
            cash DECIMAL(18,2) NOT NULL DEFAULT 100000.00,
            buying_power DECIMAL(18,2) NOT NULL DEFAULT 200000.00,
            regt_buying_power DECIMAL(18,2) NOT NULL DEFAULT 200000.00,
            daytrading_buying_power DECIMAL(18,2) NOT NULL DEFAULT 0.00,
            equity DECIMAL(18,2) NOT NULL DEFAULT 100000.00,
            long_market_value DECIMAL(18,2) NOT NULL DEFAULT 0.00,
            short_market_value DECIMAL(18,2) NOT NULL DEFAULT 0.00,
            initial_margin DECIMAL(18,2) NOT NULL DEFAULT 0.00,
            maintenance_margin DECIMAL(18,2) NOT NULL DEFAULT 0.00,
            accrued_fees DECIMAL(18,4) NOT NULL DEFAULT 0.00,
            pattern_day_trader BOOLEAN NOT NULL DEFAULT FALSE,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS alpaca_positions (
            id SERIAL PRIMARY KEY,
            account_id UUID NOT NULL REFERENCES alpaca_accounts(account_id) ON DELETE CASCADE,
            symbol VARCHAR(16) NOT NULL,
            qty DECIMAL(18,8) NOT NULL DEFAULT 0,
            avg_entry_price DECIMAL(18,6) NOT NULL DEFAULT 0,
            market_value DECIMAL(18,2) NOT NULL DEFAULT 0,
            cost_basis DECIMAL(18,2) NOT NULL DEFAULT 0,
            unrealized_pl DECIMAL(18,2) NOT NULL DEFAULT 0,
            updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(account_id, symbol)
        );

        CREATE TABLE IF NOT EXISTS alpaca_orders (
            order_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            client_order_id VARCHAR(64),
            account_id UUID NOT NULL REFERENCES alpaca_accounts(account_id) ON DELETE CASCADE,
            symbol VARCHAR(16) NOT NULL,
            side VARCHAR(8) NOT NULL,
            type VARCHAR(16) NOT NULL,
            time_in_force VARCHAR(8) NOT NULL DEFAULT 'day',
            qty DECIMAL(18,8) NOT NULL,
            limit_price DECIMAL(18,6),
            stop_price DECIMAL(18,6),
            status VARCHAR(32) NOT NULL DEFAULT 'new',
            filled_qty DECIMAL(18,8) NOT NULL DEFAULT 0,
            filled_avg_price DECIMAL(18,6),
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            submitted_at TIMESTAMP WITH TIME ZONE,
            filled_at TIMESTAMP WITH TIME ZONE,
            cancelled_at TIMESTAMP WITH TIME ZONE
        );

        CREATE TABLE IF NOT EXISTS alpaca_fills (
            fill_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            order_id UUID NOT NULL REFERENCES alpaca_orders(order_id) ON DELETE CASCADE,
            account_id UUID NOT NULL REFERENCES alpaca_accounts(account_id) ON DELETE CASCADE,
            symbol VARCHAR(16) NOT NULL,
            side VARCHAR(8) NOT NULL,
            qty DECIMAL(18,8) NOT NULL,
            price DECIMAL(18,6) NOT NULL,
            commission DECIMAL(12,4) NOT NULL DEFAULT 0,
            executed_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_positions_account ON alpaca_positions(account_id);
        CREATE INDEX IF NOT EXISTS idx_orders_account ON alpaca_orders(account_id);
        CREATE INDEX IF NOT EXISTS idx_orders_status ON alpaca_orders(account_id, status);
        CREATE INDEX IF NOT EXISTS idx_fills_account ON alpaca_fills(account_id);
    )";

    return exec_sql(schema);
}

std::optional<std::string> PostgresStore::create_account(const std::string& session_id, double initial_cash) {
    std::string sql = fmt::format(
        "INSERT INTO alpaca_accounts (session_id, cash, buying_power, regt_buying_power, equity) "
        "VALUES ({}, {}, {}, {}, {}) "
        "ON CONFLICT (session_id) DO UPDATE SET cash = EXCLUDED.cash, updated_at = CURRENT_TIMESTAMP "
        "RETURNING account_id::text",
        escape(session_id), initial_cash, initial_cash * 2, initial_cash * 2, initial_cash);

    PGresult* res = query(sql);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return std::nullopt;
    }

    std::string account_id = PQgetvalue(res, 0, 0);
    PQclear(res);
    return account_id;
}

std::optional<std::string> PostgresStore::get_account_id(const std::string& session_id) {
    std::string sql = fmt::format(
        "SELECT account_id::text FROM alpaca_accounts WHERE session_id = {}",
        escape(session_id));

    PGresult* res = query(sql);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return std::nullopt;
    }

    std::string account_id = PQgetvalue(res, 0, 0);
    PQclear(res);
    return account_id;
}

std::optional<AccountState> PostgresStore::load_account(const std::string& session_id) {
    std::string sql = fmt::format(
        "SELECT cash, buying_power, regt_buying_power, daytrading_buying_power, "
        "equity, long_market_value, short_market_value, initial_margin, "
        "maintenance_margin, accrued_fees, pattern_day_trader "
        "FROM alpaca_accounts WHERE session_id = {}",
        escape(session_id));

    PGresult* res = query(sql);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return std::nullopt;
    }

    AccountState state;
    state.cash = std::stod(PQgetvalue(res, 0, 0));
    state.buying_power = std::stod(PQgetvalue(res, 0, 1));
    state.regt_buying_power = std::stod(PQgetvalue(res, 0, 2));
    state.daytrading_buying_power = std::stod(PQgetvalue(res, 0, 3));
    state.equity = std::stod(PQgetvalue(res, 0, 4));
    state.long_market_value = std::stod(PQgetvalue(res, 0, 5));
    state.short_market_value = std::stod(PQgetvalue(res, 0, 6));
    state.initial_margin = std::stod(PQgetvalue(res, 0, 7));
    state.maintenance_margin = std::stod(PQgetvalue(res, 0, 8));
    state.accrued_fees = std::stod(PQgetvalue(res, 0, 9));
    state.pattern_day_trader = std::string(PQgetvalue(res, 0, 10)) == "t";

    PQclear(res);
    return state;
}

bool PostgresStore::save_account(const std::string& session_id, const AccountState& state) {
    std::string sql = fmt::format(
        "UPDATE alpaca_accounts SET "
        "cash = {}, buying_power = {}, regt_buying_power = {}, "
        "daytrading_buying_power = {}, equity = {}, long_market_value = {}, "
        "short_market_value = {}, initial_margin = {}, maintenance_margin = {}, "
        "accrued_fees = {}, pattern_day_trader = {}, updated_at = CURRENT_TIMESTAMP "
        "WHERE session_id = {}",
        state.cash, state.buying_power, state.regt_buying_power,
        state.daytrading_buying_power, state.equity, state.long_market_value,
        state.short_market_value, state.initial_margin, state.maintenance_margin,
        state.accrued_fees, state.pattern_day_trader ? "true" : "false",
        escape(session_id));

    return exec_sql(sql);
}

bool PostgresStore::delete_account(const std::string& session_id) {
    std::string sql = fmt::format(
        "DELETE FROM alpaca_accounts WHERE session_id = {}",
        escape(session_id));
    return exec_sql(sql);
}

std::unordered_map<std::string, Position> PostgresStore::load_positions(const std::string& session_id) {
    std::unordered_map<std::string, Position> positions;

    auto account_id = get_account_id(session_id);
    if (!account_id) return positions;

    std::string sql = fmt::format(
        "SELECT symbol, qty, avg_entry_price, market_value, cost_basis, unrealized_pl "
        "FROM alpaca_positions WHERE account_id = '{}'",
        *account_id);

    PGresult* res = query(sql);
    if (!res) return positions;

    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        Position pos;
        pos.symbol = PQgetvalue(res, i, 0);
        pos.qty = std::stod(PQgetvalue(res, i, 1));
        pos.avg_entry_price = std::stod(PQgetvalue(res, i, 2));
        pos.market_value = std::stod(PQgetvalue(res, i, 3));
        pos.cost_basis = std::stod(PQgetvalue(res, i, 4));
        pos.unrealized_pl = std::stod(PQgetvalue(res, i, 5));
        positions[pos.symbol] = pos;
    }

    PQclear(res);
    return positions;
}

bool PostgresStore::save_position(const std::string& session_id, const Position& pos) {
    auto account_id = get_account_id(session_id);
    if (!account_id) return false;

    // Use upsert to insert or update
    std::string sql = fmt::format(
        "INSERT INTO alpaca_positions (account_id, symbol, qty, avg_entry_price, "
        "market_value, cost_basis, unrealized_pl) "
        "VALUES ('{}', {}, {}, {}, {}, {}, {}) "
        "ON CONFLICT (account_id, symbol) DO UPDATE SET "
        "qty = EXCLUDED.qty, avg_entry_price = EXCLUDED.avg_entry_price, "
        "market_value = EXCLUDED.market_value, cost_basis = EXCLUDED.cost_basis, "
        "unrealized_pl = EXCLUDED.unrealized_pl, updated_at = CURRENT_TIMESTAMP",
        *account_id, escape(pos.symbol), pos.qty, pos.avg_entry_price,
        pos.market_value, pos.cost_basis, pos.unrealized_pl);

    return exec_sql(sql);
}

bool PostgresStore::delete_positions(const std::string& session_id) {
    auto account_id = get_account_id(session_id);
    if (!account_id) return false;

    std::string sql = fmt::format(
        "DELETE FROM alpaca_positions WHERE account_id = '{}'",
        *account_id);
    return exec_sql(sql);
}

bool PostgresStore::save_order(const std::string& session_id, const Order& order) {
    auto account_id = get_account_id(session_id);
    if (!account_id) return false;

    std::string side_str = order.side == OrderSide::BUY ? "buy" : "sell";
    std::string type_str;
    switch (order.type) {
        case OrderType::MARKET: type_str = "market"; break;
        case OrderType::LIMIT: type_str = "limit"; break;
        case OrderType::STOP: type_str = "stop"; break;
        case OrderType::STOP_LIMIT: type_str = "stop_limit"; break;
        default: type_str = "market";
    }

    std::string tif_str;
    switch (order.tif) {
        case TimeInForce::DAY: tif_str = "day"; break;
        case TimeInForce::GTC: tif_str = "gtc"; break;
        case TimeInForce::IOC: tif_str = "ioc"; break;
        case TimeInForce::FOK: tif_str = "fok"; break;
        default: tif_str = "day";
    }

    std::string limit_price = order.limit_price.has_value() && *order.limit_price > 0
        ? std::to_string(*order.limit_price) : "NULL";
    std::string stop_price = order.stop_price.has_value() && *order.stop_price > 0
        ? std::to_string(*order.stop_price) : "NULL";
    double qty = order.qty.value_or(0.0);

    std::string sql = fmt::format(
        "INSERT INTO alpaca_orders (order_id, client_order_id, account_id, symbol, side, "
        "type, time_in_force, qty, limit_price, stop_price, status, submitted_at) "
        "VALUES ('{}', {}, '{}', {}, '{}', '{}', '{}', {}, {}, {}, 'new', CURRENT_TIMESTAMP) "
        "ON CONFLICT (order_id) DO UPDATE SET status = EXCLUDED.status",
        order.id, escape(order.client_order_id), *account_id, escape(order.symbol),
        side_str, type_str, tif_str, qty, limit_price, stop_price);

    return exec_sql(sql);
}

bool PostgresStore::update_order_status(const std::string& order_id, const std::string& status,
                                         double filled_qty, double filled_avg_price) {
    std::string filled_at = (status == "filled") ? ", filled_at = CURRENT_TIMESTAMP" : "";
    std::string cancelled_at = (status == "cancelled") ? ", cancelled_at = CURRENT_TIMESTAMP" : "";

    std::string sql = fmt::format(
        "UPDATE alpaca_orders SET status = '{}', filled_qty = {}, filled_avg_price = {} {} {} "
        "WHERE order_id = '{}'",
        status, filled_qty, filled_avg_price, filled_at, cancelled_at, order_id);

    return exec_sql(sql);
}

std::vector<Order> PostgresStore::load_orders(const std::string& session_id,
                                               const std::string& status_filter) {
    std::vector<Order> orders;

    auto account_id = get_account_id(session_id);
    if (!account_id) return orders;

    std::string where_clause = fmt::format("account_id = '{}'", *account_id);
    if (!status_filter.empty()) {
        where_clause += fmt::format(" AND status = '{}'", status_filter);
    }

    std::string sql = fmt::format(
        "SELECT order_id::text, client_order_id, symbol, side, type, time_in_force, "
        "qty, COALESCE(limit_price, 0), COALESCE(stop_price, 0), status, filled_qty, "
        "COALESCE(filled_avg_price, 0) "
        "FROM alpaca_orders WHERE {} ORDER BY created_at DESC",
        where_clause);

    PGresult* res = query(sql);
    if (!res) return orders;

    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        Order order;
        order.id = PQgetvalue(res, i, 0);
        order.client_order_id = PQgetvalue(res, i, 1);
        order.symbol = PQgetvalue(res, i, 2);

        std::string side = PQgetvalue(res, i, 3);
        order.side = (side == "buy") ? OrderSide::BUY : OrderSide::SELL;

        std::string type = PQgetvalue(res, i, 4);
        if (type == "market") order.type = OrderType::MARKET;
        else if (type == "limit") order.type = OrderType::LIMIT;
        else if (type == "stop") order.type = OrderType::STOP;
        else if (type == "stop_limit") order.type = OrderType::STOP_LIMIT;

        std::string tif = PQgetvalue(res, i, 5);
        if (tif == "day") order.tif = TimeInForce::DAY;
        else if (tif == "gtc") order.tif = TimeInForce::GTC;
        else if (tif == "ioc") order.tif = TimeInForce::IOC;
        else if (tif == "fok") order.tif = TimeInForce::FOK;

        order.qty = std::stod(PQgetvalue(res, i, 6));
        double lp = std::stod(PQgetvalue(res, i, 7));
        if (lp > 0) order.limit_price = lp;
        double sp = std::stod(PQgetvalue(res, i, 8));
        if (sp > 0) order.stop_price = sp;
        order.filled_qty = std::stod(PQgetvalue(res, i, 10));
        order.last_fill_price = std::stod(PQgetvalue(res, i, 11));

        orders.push_back(std::move(order));
    }

    PQclear(res);
    return orders;
}

bool PostgresStore::save_fill(const std::string& session_id, const std::string& order_id,
                               const std::string& symbol, const std::string& side,
                               double qty, double price, double commission) {
    auto account_id = get_account_id(session_id);
    if (!account_id) return false;

    std::string sql = fmt::format(
        "INSERT INTO alpaca_fills (order_id, account_id, symbol, side, qty, price, commission) "
        "VALUES ('{}', '{}', {}, '{}', {}, {}, {})",
        order_id, *account_id, escape(symbol), side, qty, price, commission);

    return exec_sql(sql);
}

std::vector<Fill> PostgresStore::load_fills(const std::string& session_id) {
    std::vector<Fill> fills;

    auto account_id = get_account_id(session_id);
    if (!account_id) return fills;

    std::string sql = fmt::format(
        "SELECT order_id::text, qty, price "
        "FROM alpaca_fills WHERE account_id = '{}' ORDER BY executed_at DESC",
        *account_id);

    PGresult* res = query(sql);
    if (!res) return fills;

    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        Fill fill;
        fill.order_id = PQgetvalue(res, i, 0);
        fill.fill_qty = std::stod(PQgetvalue(res, i, 1));
        fill.fill_price = std::stod(PQgetvalue(res, i, 2));
        fills.push_back(std::move(fill));
    }

    PQclear(res);
    return fills;
}

} // namespace broker_sim
