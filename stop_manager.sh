#!/bin/bash

# ============================================================================
# BrokerSimulator Manager UI - Stop Script
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Paths
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MANAGER_DIR="$SCRIPT_DIR/manager-ui"
LOG_DIR="$MANAGER_DIR/logs"

# Port
MANAGER_PORT=5174

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║           BrokerSimulator Manager UI - Stop Script               ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_info() {
    echo -e "${CYAN}ℹ $1${NC}"
}

kill_port() {
    local port=$1
    local pids=$(lsof -t -i:$port 2>/dev/null)
    if [ -n "$pids" ]; then
        echo "$pids" | xargs kill -9 2>/dev/null || true
        return 0
    fi
    return 1
}

print_banner

echo ""
print_info "Stopping Manager UI..."

# Kill by PID file if exists
if [ -f "$LOG_DIR/manager.pid" ]; then
    PID=$(cat "$LOG_DIR/manager.pid")
    if kill -0 $PID 2>/dev/null; then
        kill $PID 2>/dev/null || true
        print_success "Stopped Manager UI (PID: $PID)"
    fi
    rm -f "$LOG_DIR/manager.pid"
fi

# Kill by port as fallback
if kill_port $MANAGER_PORT; then
    print_success "Killed process on port $MANAGER_PORT"
else
    print_info "No process running on port $MANAGER_PORT"
fi

echo ""
print_success "Manager UI stopped"
echo ""
