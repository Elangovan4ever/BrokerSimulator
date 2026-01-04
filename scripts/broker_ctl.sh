#!/usr/bin/env bash
#
# broker_ctl.sh - Start/stop/status control for Broker Simulator
#
# Usage:
#   ./scripts/broker_ctl.sh start [config_file]
#   ./scripts/broker_ctl.sh stop
#   ./scripts/broker_ctl.sh restart [config_file]
#   ./scripts/broker_ctl.sh status
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Configuration
CONFIG="${2:-config/settings.json}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BINARY="$BUILD_DIR/src/broker_simulator"
PID_FILE="${PID_FILE:-$ROOT/broker_simulator.pid}"
LOG_DIR="${LOG_DIR:-$ROOT/logs}"
LOG_FILE="$LOG_DIR/simulator.log"
STARTUP_TIMEOUT=30  # seconds to wait for startup

# Ports to check (from settings.json defaults)
CONTROL_PORT=8000
ALPACA_PORT=8100
POLYGON_PORT=8200
FINNHUB_PORT=8300
WS_PORT=8400

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Parse ports from config file if it exists
parse_config_ports() {
    if [ -f "$CONFIG" ]; then
        CONTROL_PORT=$(grep -o '"control_port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG" | grep -o '[0-9]*' || echo "8000")
        ALPACA_PORT=$(grep -o '"alpaca_port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG" | grep -o '[0-9]*' || echo "8100")
        POLYGON_PORT=$(grep -o '"polygon_port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG" | grep -o '[0-9]*' || echo "8200")
        FINNHUB_PORT=$(grep -o '"finnhub_port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG" | grep -o '[0-9]*' || echo "8300")
        WS_PORT=$(grep -o '"ws_port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG" | grep -o '[0-9]*' || echo "8400")
    fi
}

# Check if a port is listening
is_port_listening() {
    local port=$1
    if command -v ss &> /dev/null; then
        ss -tln | grep -q ":$port "
    elif command -v netstat &> /dev/null; then
        netstat -tln | grep -q ":$port "
    else
        # Fallback: try to connect
        (echo > /dev/tcp/127.0.0.1/$port) 2>/dev/null
    fi
}

# Check if all service ports are listening
all_ports_listening() {
    is_port_listening "$CONTROL_PORT" && \
    is_port_listening "$ALPACA_PORT" && \
    is_port_listening "$POLYGON_PORT" && \
    is_port_listening "$FINNHUB_PORT" && \
    is_port_listening "$WS_PORT"
}

# Get PID from file
get_pid() {
    if [ -f "$PID_FILE" ]; then
        cat "$PID_FILE"
    else
        echo ""
    fi
}

# Find any broker_simulator process (orphan detection)
find_orphan_pid() {
    pgrep -f "broker_simulator" 2>/dev/null | head -1 || echo ""
}

# Check if process is running (by PID file or orphan)
is_running() {
    local pid=$(get_pid)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    # Also check for orphan processes
    local orphan=$(find_orphan_pid)
    if [ -n "$orphan" ]; then
        return 0
    fi
    return 1
}

# Get effective PID (from file or orphan)
get_effective_pid() {
    local pid=$(get_pid)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo "$pid"
        return
    fi
    # Check for orphan
    find_orphan_pid
}

# Wait for startup with progress
wait_for_startup() {
    local elapsed=0
    local spin='-\|/'
    local i=0

    echo -n "Waiting for services to start "
    while [ $elapsed -lt $STARTUP_TIMEOUT ]; do
        if all_ports_listening; then
            echo -e " ${GREEN}done${NC}"
            return 0
        fi

        # Check if process died
        if ! is_running; then
            echo -e " ${RED}failed${NC}"
            log_error "Process died during startup. Check $LOG_FILE"
            return 1
        fi

        # Spinner
        i=$(( (i+1) % 4 ))
        printf "\b${spin:$i:1}"

        sleep 0.5
        elapsed=$((elapsed + 1))
    done

    echo -e " ${RED}timeout${NC}"
    log_error "Startup timed out after ${STARTUP_TIMEOUT}s"
    return 1
}

# Start the simulator
do_start() {
    parse_config_ports

    # Check if already running (by PID or orphan process)
    local existing_pid=$(get_effective_pid)
    if [ -n "$existing_pid" ]; then
        log_warn "Broker Simulator is already running (PID: $existing_pid)"
        return 1
    fi

    # Check if ports are in use (extra safety check)
    if is_port_listening "$CONTROL_PORT"; then
        log_error "Port $CONTROL_PORT is already in use by another process"
        log_error "Run 'ss -tlnp | grep $CONTROL_PORT' to find the process"
        return 1
    fi

    # Check binary exists
    if [ ! -x "$BINARY" ]; then
        log_error "broker_simulator not found at $BINARY"
        echo "  Build with: cmake -S . -B build -DUSE_DROGON=ON && cmake --build build -j"
        return 1
    fi

    # Check config exists
    if [ ! -f "$CONFIG" ]; then
        log_error "Config file not found: $CONFIG"
        return 1
    fi

    # Create log directory
    mkdir -p "$LOG_DIR"

    log_info "Starting Broker Simulator..."
    log_info "Config: $CONFIG"
    log_info "Ports: Control=$CONTROL_PORT, Alpaca=$ALPACA_PORT, Polygon=$POLYGON_PORT, Finnhub=$FINNHUB_PORT, WS=$WS_PORT"

    # Start in background
    nohup "$BINARY" "$CONFIG" >> "$LOG_FILE" 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"

    # Wait for startup
    if wait_for_startup; then
        log_info "Broker Simulator started successfully (PID: $pid)"
        log_info "Log file: $LOG_FILE"
        return 0
    else
        # Cleanup on failure
        if is_running; then
            kill "$pid" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
        return 1
    fi
}

# Stop the simulator
do_stop() {
    local pid=$(get_effective_pid)

    if [ -z "$pid" ]; then
        log_warn "Broker Simulator is not running"
        rm -f "$PID_FILE"
        return 0
    fi

    # Check if this is an orphan (not from our PID file)
    local file_pid=$(get_pid)
    if [ "$pid" != "$file_pid" ]; then
        log_warn "Found orphan broker_simulator process (PID: $pid)"
    fi

    log_info "Stopping Broker Simulator (PID: $pid)..."

    # Graceful shutdown with SIGTERM
    kill "$pid" 2>/dev/null || true

    # Wait for process to exit
    local elapsed=0
    local timeout=10
    while [ $elapsed -lt $timeout ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            log_info "Broker Simulator stopped"
            rm -f "$PID_FILE"
            return 0
        fi
        sleep 0.5
        elapsed=$((elapsed + 1))
    done

    # Force kill if still running
    log_warn "Process didn't stop gracefully, sending SIGKILL..."
    kill -9 "$pid" 2>/dev/null || true

    # Also kill any other orphan processes
    pkill -9 -f "broker_simulator" 2>/dev/null || true

    rm -f "$PID_FILE"
    log_info "Broker Simulator killed"
    return 0
}

# Show status
do_status() {
    parse_config_ports

    local pid=$(get_effective_pid)
    if [ -n "$pid" ]; then
        local file_pid=$(get_pid)
        if [ "$pid" != "$file_pid" ]; then
            log_warn "Broker Simulator is running as ORPHAN (PID: $pid)"
        else
            log_info "Broker Simulator is running (PID: $pid)"
        fi

        echo ""
        echo "Port Status:"
        printf "  Control (%s): " "$CONTROL_PORT"
        is_port_listening "$CONTROL_PORT" && echo -e "${GREEN}listening${NC}" || echo -e "${RED}not listening${NC}"

        printf "  Alpaca  (%s): " "$ALPACA_PORT"
        is_port_listening "$ALPACA_PORT" && echo -e "${GREEN}listening${NC}" || echo -e "${RED}not listening${NC}"

        printf "  Polygon (%s): " "$POLYGON_PORT"
        is_port_listening "$POLYGON_PORT" && echo -e "${GREEN}listening${NC}" || echo -e "${RED}not listening${NC}"

        printf "  Finnhub (%s): " "$FINNHUB_PORT"
        is_port_listening "$FINNHUB_PORT" && echo -e "${GREEN}listening${NC}" || echo -e "${RED}not listening${NC}"

        echo ""
        echo "Log file: $LOG_FILE"
        echo "PID file: $PID_FILE"
        return 0
    else
        log_info "Broker Simulator is not running"
        rm -f "$PID_FILE"
        return 1
    fi
}

# Restart
do_restart() {
    do_stop
    sleep 1
    do_start
}

# Show logs
do_logs() {
    if [ -f "$LOG_FILE" ]; then
        tail -f "$LOG_FILE"
    else
        log_error "Log file not found: $LOG_FILE"
        return 1
    fi
}

# Main
case "${1:-help}" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
        do_restart
        ;;
    status)
        do_status
        ;;
    logs)
        do_logs
        ;;
    *)
        echo "Broker Simulator Control Script"
        echo ""
        echo "Usage: $0 {start|stop|restart|status|logs} [config_file]"
        echo ""
        echo "Commands:"
        echo "  start   [config]  Start the simulator (default: config/settings.json)"
        echo "  stop              Stop the simulator"
        echo "  restart [config]  Restart the simulator"
        echo "  status            Show running status and port info"
        echo "  logs              Tail the log file"
        echo ""
        echo "Environment variables:"
        echo "  BUILD_DIR   Build directory (default: ./build)"
        echo "  PID_FILE    PID file location (default: ./broker_simulator.pid)"
        echo "  LOG_DIR     Log directory (default: ./logs)"
        exit 1
        ;;
esac
