#!/bin/bash

# ============================================================================
# BrokerSimulator Manager UI - Startup Script
# ============================================================================
# Usage: ./start_manager.sh [options]
# Options:
#   fresh     - Kill existing processes and start fresh
#   build     - Build for production before serving
#   help      - Show this help message
# ============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Paths
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MANAGER_DIR="$SCRIPT_DIR/manager-ui"
LOG_DIR="$MANAGER_DIR/logs"

# Ports
MANAGER_PORT=5174

# ============================================================================
# Helper Functions
# ============================================================================

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║          BrokerSimulator Manager UI - Startup Script             ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_section() {
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "${CYAN}ℹ $1${NC}"
}

check_port() {
    local port=$1
    if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

kill_port() {
    local port=$1
    local pids=$(lsof -t -i:$port 2>/dev/null)
    if [ -n "$pids" ]; then
        echo "$pids" | xargs kill -9 2>/dev/null || true
        sleep 1
    fi
}

wait_for_port() {
    local port=$1
    local max_attempts=30
    local attempt=0

    while [ $attempt -lt $max_attempts ]; do
        if check_port $port; then
            return 0
        fi
        sleep 1
        ((attempt++))
    done
    return 1
}

show_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  fresh     Kill existing processes and start fresh"
    echo "  build     Build for production before serving"
    echo "  help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                # Start development server"
    echo "  $0 fresh          # Kill existing and restart"
    echo "  $0 build          # Build and serve production"
    echo ""
}

# ============================================================================
# Main Script
# ============================================================================

print_banner

# Parse arguments
FRESH_START=false
BUILD_PROD=false

for arg in "$@"; do
    case $arg in
        fresh)
            FRESH_START=true
            ;;
        build)
            BUILD_PROD=true
            ;;
        help|--help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $arg"
            show_help
            exit 1
            ;;
    esac
done

# Create log directory
mkdir -p "$LOG_DIR"

# ============================================================================
# Fresh Start - Kill Existing Processes
# ============================================================================

if [ "$FRESH_START" = true ]; then
    print_section "Killing Existing Processes"

    if check_port $MANAGER_PORT; then
        print_info "Killing process on port $MANAGER_PORT..."
        kill_port $MANAGER_PORT
        print_success "Port $MANAGER_PORT freed"
    else
        print_info "Port $MANAGER_PORT is already free"
    fi
fi

# ============================================================================
# Check for Node.js
# ============================================================================

print_section "Checking Prerequisites"

if ! command -v node &> /dev/null; then
    print_error "Node.js is not installed. Please install Node.js 18+ first."
    exit 1
fi

NODE_VERSION=$(node --version)
print_success "Node.js: $NODE_VERSION"

if ! command -v npm &> /dev/null; then
    print_error "npm is not installed."
    exit 1
fi

NPM_VERSION=$(npm --version)
print_success "npm: $NPM_VERSION"

# ============================================================================
# Install Dependencies
# ============================================================================

print_section "Installing Dependencies"

cd "$MANAGER_DIR"

if [ ! -d "node_modules" ] || [ "$FRESH_START" = true ]; then
    print_info "Installing npm packages..."
    npm install > "$LOG_DIR/npm-install.log" 2>&1
    print_success "Dependencies installed"
else
    print_info "Dependencies already installed"
fi

# ============================================================================
# Start Manager UI
# ============================================================================

print_section "Starting Manager UI"

if [ "$BUILD_PROD" = true ]; then
    print_info "Building for production..."
    npm run build > "$LOG_DIR/build.log" 2>&1
    print_success "Production build complete"

    print_info "Starting production server..."
    npm run preview > "$LOG_DIR/manager.log" 2>&1 &
else
    print_info "Starting development server..."
    npm run dev > "$LOG_DIR/manager.log" 2>&1 &
fi

MANAGER_PID=$!
echo $MANAGER_PID > "$LOG_DIR/manager.pid"

# Wait for server to start
print_info "Waiting for Manager UI to start..."
if wait_for_port $MANAGER_PORT; then
    print_success "Manager UI is running on port $MANAGER_PORT"
else
    print_error "Manager UI failed to start. Check logs: $LOG_DIR/manager.log"
    exit 1
fi

# ============================================================================
# Summary
# ============================================================================

print_section "Startup Complete"

echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    Manager UI Started Successfully               ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${CYAN}Manager UI:${NC}     http://localhost:$MANAGER_PORT"
echo -e "  ${CYAN}Process ID:${NC}     $MANAGER_PID"
echo ""
echo -e "  ${CYAN}Logs:${NC}"
echo "    Manager:      $LOG_DIR/manager.log"
echo ""
echo -e "  ${CYAN}Commands:${NC}"
echo "    View logs:    tail -f $LOG_DIR/manager.log"
echo "    Stop:         ./stop_ui.sh"
echo ""
echo -e "${GREEN}Server is running in background. Use ./stop_ui.sh to stop.${NC}"
echo ""
