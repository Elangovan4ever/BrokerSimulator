# BrokerSimulator Configuration Guide

This document describes all configuration options available in BrokerSimulator.

## Configuration File

BrokerSimulator loads configuration from a JSON file. The default path is `config/config.json`.

```bash
./broker_simulator --config /path/to/config.json
```

If no configuration file is found, sensible defaults are used.

---

## Configuration Sections

### Database Configuration

Connection settings for ClickHouse, which stores historical market data.

```json
{
  "database": {
    "host": "localhost",
    "port": 9000,
    "database": "polygon",
    "user": "default",
    "password": ""
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `host` | string | `"localhost"` | ClickHouse server hostname |
| `port` | integer | `9000` | ClickHouse native protocol port |
| `database` | string | `"polygon"` | Database name containing market data |
| `user` | string | `"default"` | Database username |
| `password` | string | `""` | Database password |

---

### Service Configuration

Network binding settings for all API servers.

```json
{
  "services": {
    "control_port": 8000,
    "alpaca_port": 8100,
    "polygon_port": 8200,
    "finnhub_port": 8300,
    "ws_port": 8400,
    "bind_address": "127.0.0.1"
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `control_port` | integer | `8000` | Port for Control API |
| `alpaca_port` | integer | `8100` | Port for Alpaca-compatible API |
| `polygon_port` | integer | `8200` | Port for Polygon-compatible API |
| `finnhub_port` | integer | `8300` | Port for Finnhub-compatible API |
| `ws_port` | integer | `8400` | Port for WebSocket streams |
| `bind_address` | string | `"127.0.0.1"` | Network interface to bind to |

**Security Note**: Use `127.0.0.1` for local-only access. Use `0.0.0.0` to allow external connections.

---

### Defaults Configuration

Default values for new backtest sessions.

```json
{
  "defaults": {
    "initial_capital": 100000.0,
    "speed_factor": 0.0,
    "max_sessions": 20
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `initial_capital` | number | `100000.0` | Starting account balance for new sessions |
| `speed_factor` | number | `0.0` | Playback speed multiplier (0 = maximum speed) |
| `max_sessions` | integer | `20` | Maximum concurrent backtest sessions |

**Speed Factor Examples**:
- `0.0` - Process events as fast as possible (backtesting mode)
- `1.0` - Real-time playback (1 second = 1 second)
- `2.0` - 2x speed (1 second = 0.5 seconds)
- `0.5` - Half speed (1 second = 2 seconds)

---

### Execution Configuration

Controls order execution simulation for realistic backtesting.

```json
{
  "execution": {
    "enable_latency": false,
    "fixed_latency_us": 0,
    "random_latency_max_us": 0,
    "enable_slippage": false,
    "fixed_slippage_bps": 0.0,
    "random_slippage_max_bps": 0.0,
    "enable_market_impact": false,
    "market_impact_bps": 0.0,
    "market_impact_per_share": 0.0,
    "market_impact_sqrt_coef": 0.0,
    "enable_partial_fills": true,
    "partial_fill_probability": 1.0,
    "rejection_probability": 0.0,
    "allow_shorting": true,
    "max_position_value": 0.0,
    "max_single_order_value": 0.0,
    "enable_margin_call_checks": true,
    "enable_forced_liquidation": true,
    "maintenance_margin_pct": 25.0,
    "enable_shared_feed": false,
    "poll_interval_seconds": 0,
    "checkpoint_interval_events": 10000,
    "enable_wal": true,
    "wal_directory": "logs"
  }
}
```

#### Latency Simulation

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_latency` | boolean | `false` | Enable order latency simulation |
| `fixed_latency_us` | integer | `0` | Fixed latency in microseconds |
| `random_latency_max_us` | integer | `0` | Maximum random latency added (uniform distribution) |

**Example**: To simulate 50-150us network latency:
```json
{
  "enable_latency": true,
  "fixed_latency_us": 50,
  "random_latency_max_us": 100
}
```

#### Slippage Simulation

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_slippage` | boolean | `false` | Enable price slippage simulation |
| `fixed_slippage_bps` | number | `0.0` | Fixed slippage in basis points |
| `random_slippage_max_bps` | number | `0.0` | Maximum random slippage (uniform distribution) |

**Note**: Slippage is always adverse - buys pay more, sells receive less.

**Example**: To simulate 1-3 bps slippage:
```json
{
  "enable_slippage": true,
  "fixed_slippage_bps": 1.0,
  "random_slippage_max_bps": 2.0
}
```

#### Market Impact Simulation

Models the price impact of large orders using a combination of linear and square-root models.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_market_impact` | boolean | `false` | Enable market impact simulation |
| `market_impact_bps` | number | `0.0` | Base market impact in basis points |
| `market_impact_per_share` | number | `0.0` | Additional impact per share (bps) |
| `market_impact_sqrt_coef` | number | `0.0` | Square-root model coefficient |

**Impact Formula**:
```
total_impact_bps = market_impact_bps
                 + (qty * market_impact_per_share)
                 + (market_impact_sqrt_coef * sqrt(notional / 1,000,000))
```

**Example**: Square-root market impact model:
```json
{
  "enable_market_impact": true,
  "market_impact_bps": 0.5,
  "market_impact_sqrt_coef": 2.0
}
```

#### Partial Fills

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_partial_fills` | boolean | `true` | Allow partial fills based on available size |
| `partial_fill_probability` | number | `1.0` | Probability of getting any fill (0.0-1.0) |

#### Order Rejection

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `rejection_probability` | number | `0.0` | Probability of random order rejection (0.0-1.0) |

#### Position Limits

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `allow_shorting` | boolean | `true` | Allow short selling |
| `max_position_value` | number | `0.0` | Maximum position value (0 = no limit) |
| `max_single_order_value` | number | `0.0` | Maximum single order value (0 = no limit) |

#### Margin and Risk

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_margin_call_checks` | boolean | `true` | Check for margin violations |
| `enable_forced_liquidation` | boolean | `true` | Auto-liquidate on margin call |
| `maintenance_margin_pct` | number | `25.0` | Maintenance margin requirement (%) |

#### Feed Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_shared_feed` | boolean | `false` | Share data feed across sessions |
| `poll_interval_seconds` | integer | `0` | Polling fallback interval (0 = disabled) |

#### Checkpoint/WAL Settings

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `checkpoint_interval_events` | integer | `10000` | Save checkpoint every N events (0 = disabled) |
| `enable_wal` | boolean | `true` | Enable write-ahead logging |
| `wal_directory` | string | `"logs"` | Directory for WAL and checkpoint files |

---

### Fee Configuration

Trading fees and rebates for realistic P&L calculation.

```json
{
  "fees": {
    "per_share_commission": 0.0,
    "per_order_commission": 0.0,
    "sec_fee_per_million": 27.80,
    "taf_fee_per_share": 0.000166,
    "finra_taf_cap": 8.30,
    "maker_rebate_per_share": 0.0,
    "taker_fee_per_share": 0.0
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `per_share_commission` | number | `0.0` | Commission per share traded |
| `per_order_commission` | number | `0.0` | Fixed commission per order |
| `sec_fee_per_million` | number | `27.80` | SEC fee per $1M (sell orders only) |
| `taf_fee_per_share` | number | `0.000166` | FINRA TAF per share (sell orders only) |
| `finra_taf_cap` | number | `8.30` | Maximum TAF fee per trade |
| `maker_rebate_per_share` | number | `0.0` | Rebate for providing liquidity (negative = fee) |
| `taker_fee_per_share` | number | `0.0` | Fee for taking liquidity |

**Example**: Interactive Brokers-like fee structure:
```json
{
  "fees": {
    "per_share_commission": 0.005,
    "per_order_commission": 0.0,
    "sec_fee_per_million": 27.80,
    "taf_fee_per_share": 0.000166,
    "finra_taf_cap": 8.30
  }
}
```

---

### WebSocket Configuration

Settings for the WebSocket streaming server.

```json
{
  "websocket": {
    "queue_size": 1000,
    "overflow_policy": "drop_oldest",
    "batch_size": 50,
    "flush_interval_ms": 20
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `queue_size` | integer | `1000` | Maximum messages per client queue |
| `overflow_policy` | string | `"drop_oldest"` | Queue overflow policy: `"drop_oldest"` or `"drop_newest"` |
| `batch_size` | integer | `50` | Messages per batch send |
| `flush_interval_ms` | integer | `20` | Batch flush interval in milliseconds |

---

### Logging Configuration

Application logging settings.

```json
{
  "logging": {
    "level": "info",
    "format": "json",
    "file": "/var/log/broker_simulator/simulator.log"
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `level` | string | `"info"` | Log level: `"trace"`, `"debug"`, `"info"`, `"warn"`, `"error"` |
| `format` | string | `"json"` | Log format: `"json"` or `"text"` |
| `file` | string | `"/var/log/broker_simulator/simulator.log"` | Log file path |

---

### Authentication Configuration

API authentication settings.

```json
{
  "auth": {
    "token": "your-secret-token"
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `token` | string | `""` | Bearer token for API authentication (empty = no auth) |

When set, all API requests must include the header:
```
Authorization: Bearer your-secret-token
```

---

## Complete Example Configuration

```json
{
  "database": {
    "host": "localhost",
    "port": 9000,
    "database": "polygon",
    "user": "default",
    "password": ""
  },
  "services": {
    "control_port": 8000,
    "alpaca_port": 8100,
    "polygon_port": 8200,
    "finnhub_port": 8300,
    "ws_port": 8400,
    "bind_address": "127.0.0.1"
  },
  "defaults": {
    "initial_capital": 100000.0,
    "speed_factor": 0.0,
    "max_sessions": 20
  },
  "execution": {
    "enable_latency": true,
    "fixed_latency_us": 100,
    "random_latency_max_us": 50,
    "enable_slippage": true,
    "fixed_slippage_bps": 0.5,
    "random_slippage_max_bps": 1.0,
    "enable_market_impact": true,
    "market_impact_bps": 0.5,
    "market_impact_sqrt_coef": 1.5,
    "enable_partial_fills": true,
    "partial_fill_probability": 0.95,
    "allow_shorting": true,
    "enable_margin_call_checks": true,
    "maintenance_margin_pct": 25.0,
    "checkpoint_interval_events": 10000,
    "enable_wal": true,
    "wal_directory": "logs"
  },
  "fees": {
    "per_share_commission": 0.005,
    "per_order_commission": 0.0,
    "sec_fee_per_million": 27.80,
    "taf_fee_per_share": 0.000166,
    "finra_taf_cap": 8.30
  },
  "websocket": {
    "queue_size": 2000,
    "overflow_policy": "drop_oldest",
    "batch_size": 100,
    "flush_interval_ms": 10
  },
  "logging": {
    "level": "info",
    "format": "json",
    "file": "/var/log/broker_simulator/simulator.log"
  },
  "auth": {
    "token": ""
  }
}
```

---

## Environment Variables

Configuration can also be set via environment variables (takes precedence over config file):

| Environment Variable | Config Path |
|---------------------|-------------|
| `BROKER_SIM_DB_HOST` | `database.host` |
| `BROKER_SIM_DB_PORT` | `database.port` |
| `BROKER_SIM_DB_NAME` | `database.database` |
| `BROKER_SIM_DB_USER` | `database.user` |
| `BROKER_SIM_DB_PASSWORD` | `database.password` |
| `BROKER_SIM_CONTROL_PORT` | `services.control_port` |
| `BROKER_SIM_AUTH_TOKEN` | `auth.token` |

---

## Per-Session Overrides

Many execution settings can be overridden when creating a session:

```bash
curl -X POST http://localhost:8000/session \
  -H "Content-Type: application/json" \
  -d '{
    "symbols": ["AAPL", "MSFT"],
    "start_date": "2024-01-02",
    "end_date": "2024-01-05",
    "initial_capital": 50000.0,
    "speed_factor": 1.0,
    "execution": {
      "enable_latency": true,
      "fixed_latency_us": 200
    }
  }'
```

See [API_REFERENCE.md](API_REFERENCE.md) for full session creation options.
