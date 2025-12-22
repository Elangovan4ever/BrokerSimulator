# BrokerSimulator Deployment Guide

## Build

```bash
cmake -S . -B build -DUSE_DROGON=ON -DMYSQL_INCLUDE_DIRS=/usr/include/mysql -DMYSQL_LIBRARIES=/usr/lib/x86_64-linux-gnu/libmysqlclient.so
cmake --build build -j
```

## Run

```bash
scripts/run_simulator.sh config/settings.json
```

## Config Notes

Example fee and websocket settings:

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
  },
  "websocket": {
    "queue_size": 1000,
    "overflow_policy": "drop_oldest",
    "batch_size": 50,
    "flush_interval_ms": 20
  }
}
```

## Corporate Actions Endpoints

```bash
curl -X POST http://127.0.0.1:8000/sessions/<id>/corporate_actions/dividend \
  -H "Content-Type: application/json" \
  -d '{"symbol":"AAPL","amount_per_share":0.22}'

curl -X POST http://127.0.0.1:8000/sessions/<id>/corporate_actions/split \
  -H "Content-Type: application/json" \
  -d '{"symbol":"AAPL","split_ratio":2.0}'
```

## Tests

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Performance Benchmarks

```bash
cmake -S . -B build
cmake --build build -j
./build/src/event_queue_bench 1000000
```

## Systemd unit example

`/etc/systemd/system/broker-simulator.service`

```
[Unit]
Description=Broker Simulator
After=network.target

[Service]
Type=simple
User=elan
WorkingDirectory=/home/elan/projects/BrokerSimulator
ExecStart=/home/elan/projects/BrokerSimulator/build/broker_simulator /home/elan/projects/BrokerSimulator/config/settings.json
Restart=on-failure
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now broker-simulator
```

## Log rotation (optional)

`/etc/logrotate.d/broker-simulator`

```
/home/elan/projects/BrokerSimulator/logs/*.log /home/elan/projects/BrokerSimulator/logs/*.wal.jsonl {
    daily
    rotate 7
    compress
    missingok
    notifempty
    copytruncate
}
```
