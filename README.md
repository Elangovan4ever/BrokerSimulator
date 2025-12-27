# BrokerSimulator

BrokerSimulator runs a C++ simulation service (control + Alpaca/Polygon/Finnhub APIs) and a React Manager UI.

## Prerequisites
- CMake 3.20+
- C++ compiler with C++20 support (clang or gcc)
- Node.js 18+ and npm (for Manager UI)
- ClickHouse reachable from the simulator host

## Configuration

### Simulator (`config/settings.json`)
Key settings:
- ClickHouse connection (`clickhouse.host`, `clickhouse.port`, `clickhouse.database`)
- Service ports (`services.control_port`, `services.alpaca_port`, `services.polygon_port`, `services.finnhub_port`)
- Bind address (`services.bind_address`)
- Logging (`logging.file`)

Defaults are in `config/settings.json`:
```json
{
  "clickhouse": { "host": "localhost", "port": 9000, "database": "market_data" },
  "services": { "control_port": 8000, "alpaca_port": 8100, "polygon_port": 8200, "finnhub_port": 8300 }
}
```

### Manager UI (`manager-ui/.env.example`)
The UI reads Vite env vars. Copy `manager-ui/.env.example` to `manager-ui/.env` and edit as needed:
```bash
cp manager-ui/.env.example manager-ui/.env
```
Expected values:
```
VITE_SIMULATOR_HOST=elanlinux
VITE_CONTROL_PORT=8000
VITE_ALPACA_PORT=8100
VITE_POLYGON_PORT=8200
VITE_FINNHUB_PORT=8300
VITE_WS_PORT=8400
```
See `manager-ui/README.md` for full details.

### Integration tests (`integration-test/.env.test`)
Used by Jest to reach the simulator and real APIs (Polygon/Finnhub/Alpaca).

## Build the Simulator
```bash
cmake -S . -B build -DUSE_DROGON=ON
cmake --build build -j
```

## Run the Simulator
```bash
./start.sh           # start
./start.sh restart   # restart
./stop.sh            # stop
```

Status/logs:
```bash
./scripts/broker_ctl.sh status
./scripts/broker_ctl.sh logs
```

## Run the Manager UI
```bash
./start_ui.sh        # dev server on http://localhost:5174
./stop_ui.sh
```

Manual start:
```bash
cd manager-ui
npm install
npm run dev
```

## Integration Tests (optional)
```bash
cd integration-test
npm test
```
