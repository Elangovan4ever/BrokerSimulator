#!/usr/bin/env bash
#
# start.sh - Start or restart the Broker Simulator
#
# Usage:
#   ./start.sh           # Start the simulator
#   ./start.sh restart   # Restart the simulator
#   ./start.sh <config>  # Start with custom config
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "${1:-}" = "restart" ]; then
    exec "$SCRIPT_DIR/scripts/broker_ctl.sh" restart "${2:-}"
else
    exec "$SCRIPT_DIR/scripts/broker_ctl.sh" start "${1:-}"
fi
