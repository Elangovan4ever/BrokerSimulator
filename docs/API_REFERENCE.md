# BrokerSimulator API Reference

This document provides a complete reference for all API endpoints supported by the BrokerSimulator.

## Table of Contents

1. [Control API](#control-api)
2. [Alpaca API](#alpaca-api)
3. [Polygon API](#polygon-api)
4. [Finnhub API](#finnhub-api)
5. [WebSocket Streams](#websocket-streams)
6. [Error Handling](#error-handling)

---

## Control API

The Control API manages simulation sessions and provides time control capabilities.

**Base URL:** `http://localhost:8000`

### Session Management

#### Create Session

```http
POST /sessions
Content-Type: application/json

{
  "symbols": ["AAPL", "MSFT", "GOOGL"],
  "start_time": "2024-01-02T09:30:00Z",
  "end_time": "2024-01-02T16:00:00Z",
  "initial_capital": 100000.0,
  "speed_factor": 0.0
}
```

**Response:**
```json
{
  "session_id": "abc123",
  "status": "created",
  "config": {
    "symbols": ["AAPL", "MSFT", "GOOGL"],
    "start_time": "2024-01-02T09:30:00Z",
    "end_time": "2024-01-02T16:00:00Z",
    "initial_capital": 100000.0,
    "speed_factor": 0.0
  }
}
```

#### List Sessions

```http
GET /sessions
```

**Response:**
```json
{
  "sessions": [
    {
      "id": "abc123",
      "status": "running",
      "created_at": "2024-01-02T10:00:00Z",
      "symbols": ["AAPL", "MSFT"],
      "current_time": "2024-01-02T11:30:00Z"
    }
  ]
}
```

#### Get Session Details

```http
GET /sessions/{session_id}
```

**Response:**
```json
{
  "id": "abc123",
  "status": "running",
  "config": { ... },
  "account": {
    "cash": 95000.0,
    "equity": 100500.0,
    "buying_power": 201000.0
  },
  "stats": {
    "events_processed": 15000,
    "orders_submitted": 25,
    "fills_executed": 20
  }
}
```

#### Start Session

```http
POST /sessions/{session_id}/start
```

#### Pause Session

```http
POST /sessions/{session_id}/pause
```

#### Resume Session

```http
POST /sessions/{session_id}/resume
```

#### Stop Session

```http
POST /sessions/{session_id}/stop
```

#### Destroy Session

```http
DELETE /sessions/{session_id}
```

### Time Control

#### Set Speed

```http
POST /sessions/{session_id}/speed
Content-Type: application/json

{
  "speed": 10.0
}
```

Speed values:
- `0.0` - Maximum speed (no delay)
- `1.0` - Real-time (1 second = 1 second)
- `10.0` - 10x speed
- `100.0` - 100x speed

#### Jump to Time

```http
POST /sessions/{session_id}/jump
Content-Type: application/json

{
  "timestamp": "2024-01-02T14:00:00Z"
}
```

#### Fast Forward

```http
POST /sessions/{session_id}/fast_forward
Content-Type: application/json

{
  "timestamp": "2024-01-02T15:00:00Z"
}
```

### Order Management

#### Submit Order

```http
POST /sessions/{session_id}/orders
Content-Type: application/json

{
  "symbol": "AAPL",
  "side": "buy",
  "type": "limit",
  "qty": 100,
  "limit_price": 150.00,
  "time_in_force": "day"
}
```

**Order Types:** `market`, `limit`, `stop`, `stop_limit`, `trailing_stop`

**Time in Force:** `day`, `gtc`, `ioc`, `fok`, `opg`, `cls`

#### List Orders

```http
GET /sessions/{session_id}/orders?status=open
```

#### Cancel Order

```http
POST /sessions/{session_id}/orders/{order_id}/cancel
```

### Account & Performance

#### Get Account

```http
GET /sessions/{session_id}/account
```

**Response:**
```json
{
  "cash": 95000.0,
  "equity": 100500.0,
  "buying_power": 201000.0,
  "long_market_value": 5500.0,
  "short_market_value": 0.0,
  "initial_margin": 2750.0,
  "maintenance_margin": 1375.0,
  "pattern_day_trader": false
}
```

#### Get Performance

```http
GET /sessions/{session_id}/performance
```

**Response:**
```json
{
  "total_return": 0.005,
  "total_return_pct": 0.5,
  "sharpe_ratio": 1.25,
  "max_drawdown": -0.02,
  "win_rate": 0.65,
  "profit_factor": 1.8
}
```

#### Get Watermark

```http
GET /sessions/{session_id}/watermark
```

Returns the timestamp of the last processed event (nanoseconds).

### Corporate Actions

#### Apply Dividend

```http
POST /sessions/{session_id}/corporate_actions/dividend
Content-Type: application/json

{
  "symbol": "AAPL",
  "amount_per_share": 0.24
}
```

#### Apply Split

```http
POST /sessions/{session_id}/corporate_actions/split
Content-Type: application/json

{
  "symbol": "AAPL",
  "ratio": 4.0
}
```

### Health Check

```http
GET /health
```

**Response:**
```json
{
  "status": "healthy",
  "uptime_seconds": 3600,
  "active_sessions": 5
}
```

---

## Alpaca API

The Alpaca API simulates the Alpaca trading platform endpoints.

**Base URL:** `http://localhost:8100`

**Authentication:** Include `APCA-API-KEY-ID` and `APCA-API-SECRET-KEY` headers, or use `X-Session-ID` for simulator session binding.

### Account

#### Get Account

```http
GET /v2/account
```

**Response:**
```json
{
  "id": "session_abc123",
  "account_number": "session_abc123",
  "status": "ACTIVE",
  "currency": "USD",
  "buying_power": "201000.00",
  "regt_buying_power": "201000.00",
  "daytrading_buying_power": "0.00",
  "cash": "95000.00",
  "portfolio_value": "100500.00",
  "pattern_day_trader": false,
  "trading_blocked": false,
  "transfers_blocked": false,
  "account_blocked": false,
  "equity": "100500.00",
  "last_equity": "100000.00",
  "long_market_value": "5500.00",
  "short_market_value": "0.00",
  "initial_margin": "2750.00",
  "maintenance_margin": "1375.00",
  "daytrade_count": 0
}
```

### Orders

#### Submit Order

```http
POST /v2/orders
Content-Type: application/json

{
  "symbol": "AAPL",
  "qty": 100,
  "side": "buy",
  "type": "market",
  "time_in_force": "day"
}
```

**Response:**
```json
{
  "id": "order_xyz789",
  "client_order_id": "my_order_1",
  "created_at": "2024-01-02T10:30:00Z",
  "symbol": "AAPL",
  "qty": "100",
  "filled_qty": "0",
  "side": "buy",
  "type": "market",
  "time_in_force": "day",
  "status": "accepted"
}
```

#### List Orders

```http
GET /v2/orders?status=open&limit=100&direction=desc
```

#### Get Order by ID

```http
GET /v2/orders/{order_id}
```

#### Get Order by Client Order ID

```http
GET /v2/orders:by_client_order_id?client_order_id=my_order_1
```

#### Cancel Order

```http
DELETE /v2/orders/{order_id}
```

#### Cancel All Orders

```http
DELETE /v2/orders
```

#### Replace Order

```http
PATCH /v2/orders/{order_id}
Content-Type: application/json

{
  "qty": 150,
  "limit_price": 155.00
}
```

### Positions

#### List Positions

```http
GET /v2/positions
```

**Response:**
```json
[
  {
    "asset_id": "AAPL",
    "symbol": "AAPL",
    "exchange": "NASDAQ",
    "asset_class": "us_equity",
    "avg_entry_price": "150.00",
    "qty": "100",
    "side": "long",
    "market_value": "15500.00",
    "cost_basis": "15000.00",
    "unrealized_pl": "500.00",
    "unrealized_plpc": "0.0333",
    "current_price": "155.00"
  }
]
```

#### Get Position

```http
GET /v2/positions/{symbol}
```

#### Close Position

```http
DELETE /v2/positions/{symbol}
```

#### Close All Positions

```http
DELETE /v2/positions
```

### Market Data

#### Get Trades

```http
GET /v2/stocks/{symbol}/trades?start=2024-01-02T09:30:00Z&end=2024-01-02T16:00:00Z&limit=1000
```

#### Get Quotes

```http
GET /v2/stocks/{symbol}/quotes?start=2024-01-02T09:30:00Z&end=2024-01-02T16:00:00Z&limit=1000
```

#### Get Bars

```http
GET /v2/stocks/{symbol}/bars?timeframe=1Min&start=2024-01-02&end=2024-01-02&limit=1000
```

#### Get Latest Trade

```http
GET /v2/stocks/{symbol}/trades/latest
```

#### Get Latest Quote

```http
GET /v2/stocks/{symbol}/quotes/latest
```

#### Get Snapshot

```http
GET /v2/stocks/{symbol}/snapshot
```

### Market Info

#### Get Clock

```http
GET /v2/clock
```

**Response:**
```json
{
  "timestamp": "2024-01-02T10:30:00Z",
  "is_open": true,
  "next_open": "2024-01-03T09:30:00Z",
  "next_close": "2024-01-02T16:00:00Z"
}
```

#### Get Calendar

```http
GET /v2/calendar?start=2024-01-01&end=2024-01-31
```

#### List Assets

```http
GET /v2/assets?status=active&asset_class=us_equity
```

#### Get Asset

```http
GET /v2/assets/{symbol_or_id}
```

---

## Polygon API

The Polygon API simulates Polygon.io market data endpoints.

**Base URL:** `http://localhost:8200`

**Authentication:** Include `apiKey` query parameter or `Authorization: Bearer <token>` header.

### Aggregates (Bars)

#### Get Aggregates

```http
GET /v2/aggs/ticker/{symbol}/range/{multiplier}/{timespan}/{from}/{to}?adjusted=true&sort=asc&limit=5000
```

**Parameters:**
- `multiplier`: Number of timespan units (e.g., 1, 5, 15)
- `timespan`: `second`, `minute`, `hour`, `day`, `week`, `month`, `quarter`, `year`
- `from`: Start date (YYYY-MM-DD or timestamp)
- `to`: End date (YYYY-MM-DD or timestamp)

**Response:**
```json
{
  "ticker": "AAPL",
  "queryCount": 100,
  "resultsCount": 100,
  "adjusted": true,
  "results": [
    {
      "v": 1000000,
      "vw": 150.25,
      "o": 150.00,
      "c": 150.50,
      "h": 151.00,
      "l": 149.50,
      "t": 1704200400000,
      "n": 5000
    }
  ],
  "status": "OK",
  "request_id": "abc123"
}
```

#### Get Previous Close

```http
GET /v2/aggs/ticker/{symbol}/prev
```

#### Get Grouped Daily

```http
GET /v2/aggs/grouped/locale/us/market/stocks/{date}
```

### Trades

#### Get Trades

```http
GET /v3/trades/{symbol}?timestamp.gte=2024-01-02T09:30:00Z&timestamp.lte=2024-01-02T16:00:00Z&limit=1000
```

**Response:**
```json
{
  "results": [
    {
      "conditions": [0],
      "exchange": 4,
      "id": "12345",
      "participant_timestamp": 1704200400000000000,
      "price": 150.25,
      "sip_timestamp": 1704200400000000000,
      "size": 100,
      "tape": 3
    }
  ],
  "status": "OK",
  "request_id": "xyz789"
}
```

#### Get Last Trade

```http
GET /v2/last/trade/{symbol}
```

### Quotes

#### Get Quotes

```http
GET /v3/quotes/{symbol}?timestamp.gte=2024-01-02T09:30:00Z&timestamp.lte=2024-01-02T16:00:00Z&limit=1000
```

#### Get Last NBBO

```http
GET /v2/last/nbbo/{symbol}
```

**Response:**
```json
{
  "status": "OK",
  "request_id": "abc123",
  "results": {
    "T": "AAPL",
    "P": 150.50,
    "S": 100,
    "p": 150.45,
    "s": 200,
    "t": 1704200400000,
    "x": 1,
    "y": 1,
    "z": 1
  }
}
```

### Snapshots

#### Get All Snapshots

```http
GET /v2/snapshot/locale/us/markets/stocks/tickers?tickers=AAPL,MSFT,GOOGL
```

#### Get Ticker Snapshot

```http
GET /v2/snapshot/locale/us/markets/stocks/tickers/{symbol}
```

#### Get Gainers/Losers

```http
GET /v2/snapshot/locale/us/markets/stocks/{direction}
```

Direction: `gainers` or `losers`

### Reference Data

#### Get Ticker Details

```http
GET /v3/reference/tickers/{symbol}
```

#### List Tickers

```http
GET /v3/reference/tickers?market=stocks&active=true&limit=100
```

### Technical Indicators

#### SMA

```http
GET /v1/indicators/sma/{symbol}?timespan=day&window=20&limit=100
```

#### EMA

```http
GET /v1/indicators/ema/{symbol}?timespan=day&window=20&limit=100
```

#### RSI

```http
GET /v1/indicators/rsi/{symbol}?timespan=day&window=14&limit=100
```

#### MACD

```http
GET /v1/indicators/macd/{symbol}?timespan=day&short_window=12&long_window=26&signal_window=9&limit=100
```

### Open/Close

```http
GET /v1/open-close/{symbol}/{date}
```

### Market Status

```http
GET /v1/marketstatus/now
```

```http
GET /v1/marketstatus/upcoming
```

---

## Finnhub API

The Finnhub API simulates Finnhub.io market data endpoints.

**Base URL:** `http://localhost:8300`

**Authentication:** Include `token` query parameter.

### Quote

```http
GET /api/v1/quote?symbol=AAPL
```

**Response:**
```json
{
  "c": 150.50,
  "d": 1.25,
  "dp": 0.84,
  "h": 151.00,
  "l": 149.00,
  "o": 149.50,
  "pc": 149.25,
  "t": 1704200400
}
```

### Candles

```http
GET /api/v1/stock/candle?symbol=AAPL&resolution=D&from=1704067200&to=1704326400
```

**Resolutions:** `1`, `5`, `15`, `30`, `60`, `D`, `W`, `M`

**Response:**
```json
{
  "c": [150.50, 151.25, 149.75],
  "h": [151.00, 152.00, 150.50],
  "l": [149.00, 150.00, 148.50],
  "o": [149.50, 150.75, 150.00],
  "s": "ok",
  "t": [1704067200, 1704153600, 1704240000],
  "v": [1000000, 1200000, 900000]
}
```

### Company Profile

```http
GET /api/v1/stock/profile2?symbol=AAPL
```

### Company News

```http
GET /api/v1/company-news?symbol=AAPL&from=2024-01-01&to=2024-01-31
```

### Peers

```http
GET /api/v1/stock/peers?symbol=AAPL
```

### Basic Financials

```http
GET /api/v1/stock/metric?symbol=AAPL&metric=all
```

### Analyst Recommendations

```http
GET /api/v1/stock/recommendation?symbol=AAPL
```

### Price Target

```http
GET /api/v1/stock/price-target?symbol=AAPL
```

### Upgrades/Downgrades

```http
GET /api/v1/stock/upgrade-downgrade?symbol=AAPL
```

### Earnings Calendar

```http
GET /api/v1/calendar/earnings?from=2024-01-01&to=2024-01-31&symbol=AAPL
```

### Dividends

```http
GET /api/v1/stock/dividend?symbol=AAPL&from=2023-01-01&to=2024-01-01
```

### Stock Splits

```http
GET /api/v1/stock/split?symbol=AAPL&from=2020-01-01&to=2024-01-01
```

---

## WebSocket Streams

### Alpaca WebSocket

**URL:** `ws://localhost:8400/alpaca/stream`

#### Authentication

```json
{"action": "auth", "key": "your_api_key", "secret": "your_secret"}
```

**Success Response:**
```json
[{"T": "success", "msg": "authenticated"}]
```

#### Subscribe

```json
{"action": "subscribe", "trades": ["AAPL", "MSFT"], "quotes": ["AAPL"], "bars": ["AAPL"]}
```

**Success Response:**
```json
[{"T": "subscription", "trades": ["AAPL", "MSFT"], "quotes": ["AAPL"], "bars": ["AAPL"]}]
```

#### Unsubscribe

```json
{"action": "unsubscribe", "trades": ["MSFT"]}
```

#### Message Types

**Trade:**
```json
[{"T": "t", "S": "AAPL", "p": 150.25, "s": 100, "t": "2024-01-02T10:30:00Z", "x": 4, "z": "C"}]
```

**Quote:**
```json
[{"T": "q", "S": "AAPL", "bp": 150.20, "bs": 200, "ap": 150.25, "as": 100, "t": "2024-01-02T10:30:00Z"}]
```

**Bar:**
```json
[{"T": "b", "S": "AAPL", "o": 150.00, "h": 150.50, "l": 149.75, "c": 150.25, "v": 10000, "t": "2024-01-02T10:30:00Z"}]
```

**Trade Update (Orders):**
```json
[{"T": "trade_update", "event": "fill", "order": {"id": "order_123", "symbol": "AAPL", "qty": 100, "filled_qty": 100, "status": "filled"}}]
```

### Polygon WebSocket

**URL:** `ws://localhost:8400/polygon/ws`

#### Authentication

```json
{"action": "auth", "params": "your_api_key"}
```

**Success Response:**
```json
[{"ev": "status", "status": "auth_success", "message": "authenticated"}]
```

#### Subscribe

```json
{"action": "subscribe", "params": "T.AAPL,Q.AAPL,AM.AAPL"}
```

Subscription prefixes:
- `T.` - Trades
- `Q.` - Quotes
- `AM.` - Minute aggregates
- `A.` - Second aggregates

#### Message Types

**Trade:**
```json
[{"ev": "T", "sym": "AAPL", "p": 150.25, "s": 100, "t": 1704200400000, "x": 4, "z": 3}]
```

**Quote:**
```json
[{"ev": "Q", "sym": "AAPL", "bp": 150.20, "bs": 200, "ap": 150.25, "as": 100, "t": 1704200400000}]
```

**Aggregate:**
```json
[{"ev": "AM", "sym": "AAPL", "o": 150.00, "h": 150.50, "l": 149.75, "c": 150.25, "v": 10000, "s": 1704200400000, "e": 1704200460000}]
```

### Finnhub WebSocket

**URL:** `ws://localhost:8400/finnhub/ws`

#### Subscribe

```json
{"type": "subscribe", "symbol": "AAPL"}
```

#### Unsubscribe

```json
{"type": "unsubscribe", "symbol": "AAPL"}
```

#### Message Types

**Trade:**
```json
{"type": "trade", "data": [{"s": "AAPL", "p": 150.25, "v": 100, "t": 1704200400000, "c": []}]}
```

---

## Error Handling

### HTTP Status Codes

| Code | Description |
|------|-------------|
| 200 | Success |
| 201 | Created |
| 400 | Bad Request - Invalid parameters |
| 401 | Unauthorized - Invalid or missing authentication |
| 403 | Forbidden - Access denied |
| 404 | Not Found - Resource doesn't exist |
| 422 | Unprocessable Entity - Validation failed |
| 429 | Too Many Requests - Rate limit exceeded |
| 500 | Internal Server Error |

### Error Response Format

```json
{
  "code": 40010001,
  "message": "order not found"
}
```

### Common Error Codes

| Code | Message |
|------|---------|
| 40010001 | order not found |
| 40010002 | insufficient buying power |
| 40010003 | position not found |
| 40010004 | invalid order type |
| 40010005 | invalid time in force |
| 40310001 | session not found |
| 40310002 | session already started |
| 40310003 | session is paused |

---

## Rate Limits

The simulator applies rate limits similar to production APIs:

| API | Limit |
|-----|-------|
| Alpaca Trading | 200 requests/minute |
| Alpaca Data | 200 requests/minute |
| Polygon | 100 requests/minute (free tier) |
| Finnhub | 60 requests/minute |
| Control API | No limit |

Rate limit headers are included in responses:
```
X-RateLimit-Limit: 200
X-RateLimit-Remaining: 195
X-RateLimit-Reset: 1704200460
```
