# BrokerSimulator Agent Notes

This repo hosts the C++ BrokerSimulator (Drogon) plus a React manager UI and integration tests.

## Services and Ports
- Control service: `http://<host>:8000`
- Alpaca trading simulator: `http://<host>:8100`
- Polygon data simulator: `http://<host>:8200`
- Finnhub data simulator: `http://<host>:8300`
- Manager UI runs locally (Vite) and proxies to the services.
- Default host in tests: `elanlinux` (same LAN).

## Key Directories
- `src/`: C++ simulator code (control, alpaca, polygon, finnhub, websocket).
- `manager-ui/`: React UI.
- `integration-test/`: Jest/TypeScript integration tests.
- `requirements/`: Requirements and design docs.
- `scripts/`, `start.sh`, `stop.sh`, `start_ui.sh`, `stop_ui.sh`: launcher helpers.

## Simulation Rules
- No lookahead: REST APIs must not reveal data after current simulated time.
- Past data before current simulated time must be available.
- Session start/end times gate the replay engine and websocket streaming, not historical REST access.

## Session Control
- Sessions are created via the control service, started with `/sessions/{id}/start`.
- `session_id` can be passed as a query param to Alpaca/Polygon/Finnhub endpoints.
- Sessions have `current_time` and `speed_factor` (time travel behavior).

## Data Sources
- ClickHouse database: `market_data` (not `polygon`).
- Polygon data tables: `stock_trades`, `stock_quotes`, `stock_*_bars`, `ticker_details`,
  `stock_dividends`, `stock_splits`, `stock_news`, `stock_short_interest`,
  `stock_short_volume`, `stock_ticker_events`, `stock_balance_sheets`.
- Finnhub tables (synced by `polygonsync` on elanlinux):
  - Global: `finnhub_earnings_calendar`, `finnhub_ipo_calendar`, `finnhub_market_news`
  - Daily per-symbol: `finnhub_company_news`, `finnhub_upgrades_downgrades`,
    `finnhub_insider_transactions`
  - Weekly per-symbol: `finnhub_sec_filings`, `finnhub_dividends`,
    `finnhub_congressional_trading`, `finnhub_insider_sentiment`,
    `finnhub_recommendation_trends`, `finnhub_price_targets`,
    `finnhub_eps_estimates`, `finnhub_revenue_estimates`,
    `finnhub_earnings_history`, `finnhub_social_sentiment`,
    `finnhub_company_profiles`, `finnhub_basic_financials`,
    `finnhub_ownership`, `finnhub_company_peers`, `finnhub_news_sentiment`
  - Additional: `finnhub_financials_reported`, `finnhub_financials_standardized`
- ClickHouse LowCardinality columns must be `CAST(... AS String)` in SQL.

## Alpaca Trading API (Simulator)
- Account: `/v2/account`
- Orders: `/v2/orders`, `/v2/orders/{id}`, `/v2/orders:by_client_order_id`
- Positions: `/v2/positions`, `/v2/positions/{symbol}`
- Cancel/replace endpoints supported; notional orders return 400.
- Market data endpoints exist but are not used for Alpaca tests.

## WebSockets
- Status updates: `/ws/status` (session status + current time).
- Alpaca stream: `/alpaca/stream` or `/alpaca/ws`.

## Integration Tests
- Location: `integration-test/`
- Run all: `npm test` (uses `.env.test`).
- Uses real Polygon/Finnhub APIs for schema comparisons when API keys are set.
- `TEST_SYMBOLS` default: `AAPL,MSFT,AMZN`.
- `TEST_START_DATE` default: `2025-01-13`.
- `FINNHUB_TEST_START_DATE` default: `2025-12-20`.

## Notes
- The simulator filters out future data based on `current_time` for all REST APIs.
- Many tests depend on ClickHouse being populated for the configured date ranges.
