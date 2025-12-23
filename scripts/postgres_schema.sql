-- BrokerSimulator PostgreSQL Schema for Alpaca Account Persistence
-- Run with: psql -h localhost -U postgres -d broker_sim -f scripts/postgres_schema.sql

-- Create database (run as superuser)
-- CREATE DATABASE broker_sim;

-- Accounts table - stores account configuration and balances
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

-- Positions table - tracks current positions per account
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

-- Orders table - order history
CREATE TABLE IF NOT EXISTS alpaca_orders (
    order_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    client_order_id VARCHAR(64),
    account_id UUID NOT NULL REFERENCES alpaca_accounts(account_id) ON DELETE CASCADE,
    symbol VARCHAR(16) NOT NULL,
    side VARCHAR(8) NOT NULL,  -- 'buy' or 'sell'
    type VARCHAR(16) NOT NULL, -- 'market', 'limit', 'stop', 'stop_limit'
    time_in_force VARCHAR(8) NOT NULL DEFAULT 'day',  -- 'day', 'gtc', 'ioc', 'fok'
    qty DECIMAL(18,8) NOT NULL,
    limit_price DECIMAL(18,6),
    stop_price DECIMAL(18,6),
    status VARCHAR(32) NOT NULL DEFAULT 'new',  -- 'new', 'accepted', 'filled', 'partially_filled', 'cancelled', 'rejected'
    filled_qty DECIMAL(18,8) NOT NULL DEFAULT 0,
    filled_avg_price DECIMAL(18,6),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    submitted_at TIMESTAMP WITH TIME ZONE,
    filled_at TIMESTAMP WITH TIME ZONE,
    cancelled_at TIMESTAMP WITH TIME ZONE
);

-- Fills table - execution history
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

-- Indexes for common queries
CREATE INDEX IF NOT EXISTS idx_positions_account ON alpaca_positions(account_id);
CREATE INDEX IF NOT EXISTS idx_orders_account ON alpaca_orders(account_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON alpaca_orders(account_id, status);
CREATE INDEX IF NOT EXISTS idx_orders_symbol ON alpaca_orders(account_id, symbol);
CREATE INDEX IF NOT EXISTS idx_fills_account ON alpaca_fills(account_id);
CREATE INDEX IF NOT EXISTS idx_fills_order ON alpaca_fills(order_id);

-- Function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ language 'plpgsql';

-- Triggers for updated_at
DROP TRIGGER IF EXISTS update_alpaca_accounts_updated_at ON alpaca_accounts;
CREATE TRIGGER update_alpaca_accounts_updated_at
    BEFORE UPDATE ON alpaca_accounts
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

DROP TRIGGER IF EXISTS update_alpaca_positions_updated_at ON alpaca_positions;
CREATE TRIGGER update_alpaca_positions_updated_at
    BEFORE UPDATE ON alpaca_positions
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

DROP TRIGGER IF EXISTS update_alpaca_orders_updated_at ON alpaca_orders;
CREATE TRIGGER update_alpaca_orders_updated_at
    BEFORE UPDATE ON alpaca_orders
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();
