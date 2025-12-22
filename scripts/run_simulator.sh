#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CONFIG=${1:-config/settings.json}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}

if [ ! -x "$BUILD_DIR/broker_simulator" ]; then
  echo "broker_simulator not built; run: cmake -S . -B build -DUSE_DROGON=ON && cmake --build build -j"
  exit 1
fi

echo "Starting broker_simulator with $CONFIG"
exec "$BUILD_DIR/broker_simulator" "$CONFIG"
