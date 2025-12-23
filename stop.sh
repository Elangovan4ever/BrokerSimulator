#!/usr/bin/env bash
#
# stop.sh - Stop the Broker Simulator
#
# Usage:
#   ./stop.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/scripts/broker_ctl.sh" stop
