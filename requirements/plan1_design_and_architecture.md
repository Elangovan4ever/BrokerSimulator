# Broker Simulation System - Design & Architecture Document

**Version:** 1.0
**Date:** 2025-12-20
**Author:** ElanTradePro Development Team
**Status:** Draft for Review

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Requirements Overview](#2-requirements-overview)
3. [System Architecture](#3-system-architecture)
4. [Technology Stack](#4-technology-stack)
5. [Component Design](#5-component-design)
6. [API Specifications](#6-api-specifications)
7. [Data Models](#7-data-models)
8. [Time Travel & Event Ordering](#8-time-travel--event-ordering)
9. [Order Matching Engine](#9-order-matching-engine)
10. [WebSocket Streaming](#10-websocket-streaming)
11. [Session Management](#11-session-management)
12. [Database Integration](#12-database-integration)
13. [Configuration & Deployment](#13-configuration--deployment)
14. [Testing Strategy](#14-testing-strategy)
15. [Performance Considerations](#15-performance-considerations)
16. [Security Considerations](#16-security-considerations)
17. [Future Enhancements](#17-future-enhancements)

---

## 1. Executive Summary

### 1.1 Purpose

This document defines the design and architecture for a **Broker Simulation System** that enables high-fidelity backtesting of intraday algorithmic trading strategies. The system simulates three broker/data provider APIs:

1. **Alpaca Simulator** - Trading execution (orders, positions, account management)
2. **Polygon Simulator** - Market data (trades, quotes, aggregates)
3. **Finnhub Simulator** - Market data (quotes, candles, news)

### 1.2 Goals

- **Realism**: Simulate real broker behavior with tick-level precision
- **No Look-Ahead Bias**: Strict chronological event processing
- **Seamless Integration**: ElanTradePro treats simulators as real brokers
- **Performance**: Process millions of events faster than real-time
- **Scalability**: Support 10+ concurrent strategy backtests

### 1.3 Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Core Engine** | C++ or Rust | Maximum performance for event processing, order matching |
| **API Layer** | C++ (Crow/Drogon) or Rust (Axum) | Native performance, no Python GIL overhead |
| **WebSocket** | Native C++/Rust libraries | Low-latency streaming |
| **Database** | ClickHouse | Already contains 10TB historical data |
| **Order Matching** | NBBO-based | Matches available data (no full order book) |
| **Concurrency** | True multi-threading | Parallel session execution |

### 1.4 Why NOT Python?

This section documents why Python was rejected for the core simulation engine:

| Concern | Impact on Backtesting |
|---------|----------------------|
| **Global Interpreter Lock (GIL)** | Cannot parallelize CPU-bound event processing across cores |
| **Interpreted Execution** | 10-100x slower than compiled languages for tight loops |
| **Memory Overhead** | Python objects ~8x larger than C++ structs |
| **Garbage Collection** | Unpredictable latency spikes during high-throughput periods |
| **Real Money Decisions** | Backtest results inform live trading; speed = more iterations = better strategies |

### 1.5 Performance Comparison

| Operation | Python | C++/Rust |
|-----------|--------|----------|
| Parse & process 1M tick events | ~5-10 seconds | ~0.1-0.2 seconds |
| Order matching (per order) | ~50 μs | ~0.5 μs |
| Memory per market event | ~500 bytes | ~64 bytes |
| 10 parallel sessions | GIL-limited | True parallelism |
| Full day backtest (1 symbol) | ~30 seconds | ~1-2 seconds |
| Full day backtest (100 symbols) | ~50 minutes | ~2-3 minutes |

### 1.6 Language Choice: C++ vs Rust

| Factor | C++ | Rust |
|--------|-----|------|
| **Existing Codebase** | Already used (finnhub_sync.cpp, polygon_importer.cpp) | New to project |
| **Performance** | Excellent | Excellent (comparable) |
| **Memory Safety** | Manual (risk of bugs) | Compile-time guaranteed |
| **Development Speed** | Moderate | Moderate (steeper learning curve) |
| **ClickHouse Driver** | clickhouse-cpp (mature) | clickhouse-rs (good) |
| **HTTP/WebSocket** | Crow, Drogon, Boost.Beast | Axum, Actix-web, Tokio |
| **Team Familiarity** | Already in use | Requires learning |

**Recommendation:** Use **C++** since it's already in your codebase and you have working examples. Consider Rust for future rewrites if memory safety becomes a concern.

---

## 2. Requirements Overview

### 2.1 Functional Requirements

#### FR-1: Market Data Replay
- Stream historical tick data (trades, quotes) from ClickHouse
- Merge multiple symbol streams in chronological order
- Support variable replay speeds (1x, 10x, 100x, max)
- Provide REST endpoints for historical data queries

#### FR-2: Order Execution Simulation
- Accept orders via REST API (mimicking Alpaca)
- Execute orders against simulated NBBO
- Support order types: market, limit, stop, stop_limit, trailing_stop
- Support time-in-force: day, gtc, ioc, fok, opg, cls
- Generate fill events with realistic pricing

#### FR-3: Account & Portfolio Management
- Track cash balance, equity, buying power
- Maintain position records with P&L calculations
- Enforce margin requirements (Reg-T: 2x leverage)
- Calculate unrealized P&L as prices update

#### FR-4: Time Travel Control
- Set simulation start time to any historical date
- Pause, resume, and stop simulation
- Control replay speed dynamically
- Jump to specific timestamps (fast-forward)

#### FR-5: Multi-Session Support
- Run multiple independent backtest sessions
- Isolate account state per session
- Support parallel execution across CPU cores

### 2.2 Non-Functional Requirements

| Requirement | Target |
|-------------|--------|
| Latency (order to fill) | < 1ms in simulation time |
| Throughput | ≥100,000 events/second per session |
| Concurrent Sessions | 10+ simultaneous backtests |
| Memory per Session | < 500MB (streaming, no bulk load) |
| Startup Time | < 5 seconds |
| Data Volume | Handle 10TB historical data efficiently |
| Availability | Local service, restart-tolerant |

### 2.3 Validation Benchmarks

| Scenario | Target Performance |
|----------|-------------------|
| Single-day, 1 symbol, max speed | ~1–2 seconds |
| Single-day, 100 symbols, max speed | ~2–3 minutes |
| Order→fill simulation path | < 1 ms latency |
| 10 parallel sessions | No contention, linear scaling |
| WebSocket push latency | < 1 ms |

---

## 3. System Architecture

### 3.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ElanTradePro (Mac)                             │
│                    React Frontend + Python Backend                          │
│                    Unified Broker Interface Layer                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
┌───────────────────────┐ ┌───────────────────────┐ ┌───────────────────────┐
│   ALPACA SIMULATOR    │ │   POLYGON SIMULATOR   │ │   FINNHUB SIMULATOR   │
│      Port: 8100       │ │      Port: 8200       │ │      Port: 8300       │
│                       │ │                       │ │                       │
│  ┌─────────────────┐  │ │  ┌─────────────────┐  │ │  ┌─────────────────┐  │
│  │   REST API      │  │ │  │   REST API      │  │ │  │   REST API      │  │
│  │   (Drogon/C++)  │  │ │  │   (Drogon/C++)  │  │ │  │   (Drogon/C++)  │  │
│  └─────────────────┘  │ │  └─────────────────┘  │ │  └─────────────────┘  │
│  ┌─────────────────┐  │ │  ┌─────────────────┐  │ │  ┌─────────────────┐  │
│  │   WebSocket     │  │ │  │   WebSocket     │  │ │  │   WebSocket     │  │
│  │   (Drogon WS)   │  │ │  │   (Drogon WS)   │  │ │  │   (Drogon WS)   │  │
│  └─────────────────┘  │ │  └─────────────────┘  │ │  └─────────────────┘  │
└───────────┬───────────┘ └───────────┬───────────┘ └───────────┬───────────┘
            │                         │                         │
            └─────────────────────────┼─────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SIMULATION CORE ENGINE                              │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐            │
│  │   Time Clock    │  │   Event Queue   │  │  Order Matching │            │
│  │   Controller    │  │   (Priority)    │  │     Engine      │            │
│  │                 │  │                 │  │                 │            │
│  │ • current_time  │  │ • trades        │  │ • market orders │            │
│  │ • speed_factor  │  │ • quotes        │  │ • limit orders  │            │
│  │ • is_paused     │  │ • order events  │  │ • stop orders   │            │
│  │ • set_time()    │  │ • fill events   │  │ • partial fills │            │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘            │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐            │
│  │    Session      │  │    Account      │  │   Data Source   │            │
│  │    Manager      │  │    Manager      │  │   (ClickHouse)  │            │
│  │                 │  │                 │  │                 │            │
│  │ • create()      │  │ • cash          │  │ • query_trades  │            │
│  │ • get()         │  │ • positions     │  │ • query_quotes  │            │
│  │ • destroy()     │  │ • orders        │  │ • query_bars    │            │
│  │ • list()        │  │ • margin        │  │ • stream_data   │            │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘            │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            CLICKHOUSE DATABASE                              │
│                                                                             │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐       │
│  │ stock_trades │ │ stock_quotes │ │ stock_*_bars │ │  finnhub_*   │       │
│  │              │ │              │ │              │ │              │       │
│  │ • timestamp  │ │ • timestamp  │ │ • timestamp  │ │ • company_   │       │
│  │ • symbol     │ │ • symbol     │ │ • symbol     │ │   news       │       │
│  │ • price      │ │ • bid/ask    │ │ • o/h/l/c/v  │ │ • market_    │       │
│  │ • size       │ │ • bid/ask_sz │ │ • vwap       │ │   news       │       │
│  │ • conditions │ │ • exchange   │ │              │ │ • earnings   │       │
│  └──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘       │
│                                                                             │
│                         ~10TB Historical Data                               │
│                    Trades: 2015-present (nanosecond)                        │
│                    Quotes: 2020-present (nanosecond)                        │
│                    Bars: 2003-present (various intervals)                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Component Interaction Flow

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           SIMULATION EVENT LOOP                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. INITIALIZE                                                               │
│     ┌────────────┐                                                           │
│     │ Session    │ ──► Create account with initial capital                   │
│     │ Start      │ ──► Set simulation start time                             │
│     │            │ ──► Load symbol list for backtesting                      │
│     └────────────┘                                                           │
│                                                                              │
│  2. DATA LOADING                                                             │
│     ┌────────────┐                                                           │
│     │ ClickHouse │ ──► Query trades/quotes for symbols in time window        │
│     │ Query      │ ──► ORDER BY timestamp ASC                                │
│     │            │ ──► Stream results via cursor (memory efficient)          │
│     └────────────┘                                                           │
│                                                                              │
│  3. EVENT LOOP (repeated until end of data or stop)                          │
│     ┌────────────┐     ┌────────────┐     ┌────────────┐                    │
│     │ Get Next   │ ──► │ Update     │ ──► │ Check      │                    │
│     │ Event      │     │ Clock      │     │ Orders     │                    │
│     └────────────┘     └────────────┘     └────────────┘                    │
│            │                                     │                           │
│            ▼                                     ▼                           │
│     ┌────────────┐     ┌────────────┐     ┌────────────┐                    │
│     │ Stream to  │     │ Execute    │     │ Update     │                    │
│     │ Client WS  │     │ Fills      │     │ Positions  │                    │
│     └────────────┘     └────────────┘     └────────────┘                    │
│            │                 │                   │                           │
│            ▼                 ▼                   ▼                           │
│     ┌────────────┐     ┌────────────┐     ┌────────────┐                    │
│     │ Strategy   │ ──► │ Place      │ ──► │ Queue      │                    │
│     │ Receives   │     │ Order      │     │ Order      │                    │
│     │ Data       │     │ (REST)     │     │ Event      │                    │
│     └────────────┘     └────────────┘     └────────────┘                    │
│                                                                              │
│  4. COMPLETION                                                               │
│     ┌────────────┐                                                           │
│     │ End of     │ ──► Calculate final P&L                                   │
│     │ Data       │ ──► Generate performance report                           │
│     │            │ ──► Cleanup session resources                             │
│     └────────────┘                                                           │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Technology Stack

### 4.1 Core Technologies (C++ Implementation)

| Component | Technology | Version | Purpose |
|-----------|------------|---------|---------|
| **Language** | C++ | C++20 | High-performance core engine |
| **Build System** | CMake | 3.20+ | Cross-platform build configuration |
| **HTTP/REST Framework** | Drogon | 1.9+ | High-performance async HTTP server |
| **WebSocket** | Drogon WebSocket | Built-in | Low-latency real-time streaming |
| **JSON Parsing** | nlohmann/json | 3.11+ | Fast JSON serialization |
| **Database Driver** | clickhouse-cpp | 2.5+ | Native ClickHouse connectivity |
| **Date/Time** | Howard Hinnant date | 3.0+ | Modern date/time handling |
| **Threading** | std::thread + thread pool | C++20 | True parallel execution |
| **Logging** | spdlog | 1.12+ | High-performance structured logging |
| **Testing** | Google Test | 1.14+ | Unit and integration testing |

### 4.2 Alternative: Rust Implementation

If Rust is preferred for memory safety:

| Component | Technology | Version | Purpose |
|-----------|------------|---------|---------|
| **Language** | Rust | 1.75+ | Memory-safe high-performance |
| **Async Runtime** | Tokio | 1.35+ | Async I/O and threading |
| **HTTP/REST Framework** | Axum | 0.7+ | Ergonomic async HTTP server |
| **WebSocket** | tokio-tungstenite | 0.21+ | Async WebSocket |
| **JSON Parsing** | serde_json | 1.0+ | Zero-copy deserialization |
| **Database Driver** | clickhouse-rs | 1.1+ | Async ClickHouse driver |
| **Date/Time** | chrono | 0.4+ | Timezone-aware datetime |
| **Logging** | tracing | 0.1+ | Structured async logging |
| **Testing** | built-in + tokio-test | - | Async test support |

### 4.3 Development Tools

| Tool | Purpose |
|------|---------|
| **CMake** | Build system (C++) |
| **Cargo** | Build system (Rust) |
| **clang-format** | C++ code formatting |
| **clang-tidy** | C++ static analysis |
| **rustfmt** | Rust code formatting |
| **clippy** | Rust linting |
| **Valgrind/ASan** | Memory debugging (C++) |

### 4.4 Infrastructure

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Database** | ClickHouse | Historical market data storage (10TB) |
| **Process Manager** | systemd | Service management |
| **Reverse Proxy** | nginx (optional) | SSL termination, load balancing |

### 4.5 C++ Libraries Summary

```cmake
# CMakeLists.txt dependencies
find_package(Drogon REQUIRED)        # HTTP/WebSocket server
find_package(nlohmann_json REQUIRED) # JSON handling
find_package(spdlog REQUIRED)        # Logging
find_package(GTest REQUIRED)         # Testing
find_package(clickhouse-cpp REQUIRED) # ClickHouse driver

# Header-only libraries
# - date.h (Howard Hinnant)
# - concurrentqueue (lock-free queue)
```

### 4.6 Why Drogon for C++ HTTP?

| Framework | Requests/sec | Latency (p99) | WebSocket | Notes |
|-----------|-------------|---------------|-----------|-------|
| **Drogon** | 500,000+ | <1ms | Yes | Best overall for our use case |
| Crow | 300,000+ | <2ms | Yes | Simpler but less features |
| Boost.Beast | 400,000+ | <1ms | Yes | Lower-level, more code |
| Pistache | 200,000+ | <3ms | No | No WebSocket support |

Drogon is recommended because:
- Built-in WebSocket support (critical for streaming)
- Async I/O with coroutines (C++20)
- Built-in JSON support
- Active development and good documentation

---

## 5. Component Design

### 5.1 Directory Structure (C++ Implementation)

```
/home/elan/projects/broker_simulator/
├── CMakeLists.txt                 # Root CMake configuration
├── README.md                      # Project documentation
├── .env.example                   # Environment template
├── config/
│   └── settings.json              # Runtime configuration
│
├── src/
│   ├── main.cpp                   # Application entry point
│   │
│   ├── core/                      # Shared core components
│   │   ├── CMakeLists.txt
│   │   ├── time_engine.hpp        # Global clock controller
│   │   ├── time_engine.cpp
│   │   ├── event_queue.hpp        # Lock-free priority queue
│   │   ├── event_queue.cpp
│   │   ├── data_source.hpp        # ClickHouse interface
│   │   ├── data_source.cpp
│   │   ├── session_manager.hpp    # Multi-session management
│   │   ├── session_manager.cpp
│   │   ├── thread_pool.hpp        # Worker thread pool
│   │   └── models.hpp             # Shared data structures
│   │
│   ├── alpaca/                    # Alpaca simulator
│   │   ├── CMakeLists.txt
│   │   ├── alpaca_server.hpp      # Drogon HTTP server
│   │   ├── alpaca_server.cpp
│   │   ├── controllers/
│   │   │   ├── orders_controller.hpp
│   │   │   ├── orders_controller.cpp
│   │   │   ├── account_controller.hpp
│   │   │   ├── account_controller.cpp
│   │   │   ├── positions_controller.hpp
│   │   │   └── positions_controller.cpp
│   │   ├── websocket/
│   │   │   ├── trade_updates_ws.hpp
│   │   │   └── trade_updates_ws.cpp
│   │   ├── services/
│   │   │   ├── order_manager.hpp
│   │   │   ├── order_manager.cpp
│   │   │   ├── account_manager.hpp
│   │   │   ├── account_manager.cpp
│   │   │   ├── matching_engine.hpp
│   │   │   └── matching_engine.cpp
│   │   └── models.hpp             # Alpaca-specific structs
│   │
│   ├── polygon/                   # Polygon simulator
│   │   ├── CMakeLists.txt
│   │   ├── polygon_server.hpp
│   │   ├── polygon_server.cpp
│   │   ├── controllers/
│   │   │   ├── aggregates_controller.hpp
│   │   │   ├── trades_controller.hpp
│   │   │   ├── quotes_controller.hpp
│   │   │   └── last_controller.hpp
│   │   ├── websocket/
│   │   │   ├── market_data_ws.hpp
│   │   │   └── market_data_ws.cpp
│   │   └── models.hpp
│   │
│   ├── finnhub/                   # Finnhub simulator
│   │   ├── CMakeLists.txt
│   │   ├── finnhub_server.hpp
│   │   ├── finnhub_server.cpp
│   │   ├── controllers/
│   │   │   ├── quote_controller.hpp
│   │   │   ├── candles_controller.hpp
│   │   │   └── news_controller.hpp
│   │   ├── websocket/
│   │   │   ├── trades_ws.hpp
│   │   │   └── trades_ws.cpp
│   │   └── models.hpp
│   │
│   └── control/                   # Simulation control API
│       ├── CMakeLists.txt
│       ├── control_server.hpp
│       ├── control_server.cpp
│       └── controllers/
│           ├── sessions_controller.hpp
│           └── time_controller.hpp
│
├── include/                       # Public headers
│   └── broker_simulator/
│       └── api.hpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_main.cpp              # GTest main
│   ├── core/
│   │   ├── test_time_engine.cpp
│   │   ├── test_event_queue.cpp
│   │   └── test_matching_engine.cpp
│   ├── alpaca/
│   ├── polygon/
│   └── integration/
│
├── scripts/
│   ├── build.sh                   # Build script
│   ├── run_simulator.sh           # Start all services
│   └── benchmark.sh               # Performance benchmarking
│
├── third_party/                   # External dependencies
│   ├── concurrentqueue/           # Lock-free queue
│   └── date/                      # Howard Hinnant date
│
└── docs/
    ├── api_reference.md
    ├── building.md
    └── architecture.md
```

### 5.2 Core Component: Time Engine

```cpp
// src/core/time_engine.hpp

#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;
using Nanoseconds = std::chrono::nanoseconds;

/**
 * Controls the simulation clock for time-travel backtesting.
 *
 * Thread-safe: All public methods can be called from any thread.
 * All components reference this clock to ensure synchronized,
 * chronologically-ordered event processing.
 */
class TimeEngine {
public:
    using TimeListener = std::function<void(Timestamp)>;

    TimeEngine() = default;
    ~TimeEngine() = default;

    // Non-copyable, non-movable (shared state)
    TimeEngine(const TimeEngine&) = delete;
    TimeEngine& operator=(const TimeEngine&) = delete;

    /**
     * Get current simulation time.
     */
    Timestamp current_time() const {
        return current_time_.load(std::memory_order_acquire);
    }

    /**
     * Jump to a specific point in time.
     */
    void set_time(Timestamp time) {
        current_time_.store(time, std::memory_order_release);
        notify_listeners(time);
    }

    /**
     * Advance clock to specified time (cannot go backwards).
     */
    void advance_to(Timestamp time) {
        Timestamp current = current_time_.load(std::memory_order_acquire);
        while (time > current) {
            if (current_time_.compare_exchange_weak(current, time,
                    std::memory_order_release, std::memory_order_acquire)) {
                notify_listeners(time);
                break;
            }
        }
    }

    /**
     * Wait for next event with optional speed factor.
     *
     * @param event_time Time of the next event
     * @return true if should continue, false if stopped
     */
    bool wait_for_next_event(Timestamp event_time) {
        if (!is_running_.load(std::memory_order_acquire)) {
            return false;
        }

        // Handle pause
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] {
                return !is_paused_.load(std::memory_order_acquire) ||
                       !is_running_.load(std::memory_order_acquire);
            });
        }

        if (!is_running_.load(std::memory_order_acquire)) {
            return false;
        }

        // Apply speed factor delay
        double speed = speed_factor_.load(std::memory_order_acquire);
        if (speed > 0.0) {
            auto current = current_time_.load(std::memory_order_acquire);
            auto diff = std::chrono::duration_cast<Nanoseconds>(event_time - current);
            if (diff.count() > 0) {
                auto sleep_time = Nanoseconds(static_cast<int64_t>(diff.count() / speed));
                std::this_thread::sleep_for(sleep_time);
            }
        }

        advance_to(event_time);
        return true;
    }

    // Control methods
    void start() {
        is_running_.store(true, std::memory_order_release);
        is_paused_.store(false, std::memory_order_release);
    }

    void pause() {
        is_paused_.store(true, std::memory_order_release);
    }

    void resume() {
        is_paused_.store(false, std::memory_order_release);
        pause_cv_.notify_all();
    }

    void stop() {
        is_running_.store(false, std::memory_order_release);
        pause_cv_.notify_all();
    }

    // Speed control: 0 = max speed, 1.0 = real-time, 10.0 = 10x faster
    void set_speed(double factor) {
        speed_factor_.store(factor, std::memory_order_release);
    }

    double speed() const {
        return speed_factor_.load(std::memory_order_acquire);
    }

    /**
     * Fast-forward to a target time without emitting events.
     *
     * @param target_time Time to jump to
     * @param skip_events If true, events between current and target are discarded
     * @return Number of events skipped (if skip_events=true)
     */
    size_t fast_forward(Timestamp target_time, bool skip_events = true) {
        if (target_time <= current_time()) {
            return 0;
        }

        is_fast_forwarding_.store(true, std::memory_order_release);
        size_t skipped = 0;

        if (skip_events) {
            // Mark that events should be skipped until target_time
            skip_until_time_.store(target_time, std::memory_order_release);
        }

        set_time(target_time);
        is_fast_forwarding_.store(false, std::memory_order_release);
        return skipped;
    }

    /**
     * Check if currently fast-forwarding.
     */
    bool is_fast_forwarding() const {
        return is_fast_forwarding_.load(std::memory_order_acquire);
    }

    /**
     * Check if an event at the given time should be skipped (during fast-forward).
     */
    bool should_skip_event(Timestamp event_time) const {
        Timestamp skip_until = skip_until_time_.load(std::memory_order_acquire);
        if (skip_until == Timestamp{}) {
            return false;
        }
        return event_time < skip_until;
    }

    /**
     * Clear the skip-until marker (call after fast-forward completes).
     */
    void clear_skip_marker() {
        skip_until_time_.store(Timestamp{}, std::memory_order_release);
    }

    // State queries
    bool is_running() const { return is_running_.load(std::memory_order_acquire); }
    bool is_paused() const { return is_paused_.load(std::memory_order_acquire); }

    // Listener registration
    void add_listener(TimeListener listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        listeners_.push_back(std::move(listener));
    }

private:
    void notify_listeners(Timestamp time) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        for (const auto& listener : listeners_) {
            listener(time);
        }
    }

    std::atomic<Timestamp> current_time_{Timestamp{}};
    std::atomic<double> speed_factor_{0.0};  // 0 = max speed
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> is_fast_forwarding_{false};
    std::atomic<Timestamp> skip_until_time_{Timestamp{}};

    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;

    std::mutex listeners_mutex_;
    std::vector<TimeListener> listeners_;
};

} // namespace broker_sim
```

**Fast-Forward Usage Example:**

```cpp
// Jump to market open, skipping pre-market events
auto market_open = parse_timestamp("2024-01-15 09:30:00");
time_engine.fast_forward(market_open, /*skip_events=*/true);

// In event loop, check if event should be skipped
while (auto event = event_queue.pop()) {
    if (time_engine.should_skip_event(event->timestamp)) {
        continue;  // Skip this event
    }
    // Process event normally
}
time_engine.clear_skip_marker();
```

### 5.3 Core Component: Event Queue

```cpp
// src/core/event_queue.hpp

#pragma once

#include <chrono>
#include <atomic>
#include <optional>
#include <string>
#include <variant>
#include <queue>
#include <mutex>
#include "concurrentqueue/concurrentqueue.h"  // Lock-free queue

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

/**
 * Types of events in the simulation.
 */
enum class EventType {
    TRADE,
    QUOTE,
    BAR,
    ORDER_NEW,
    ORDER_FILL,
    ORDER_CANCEL,
    ORDER_EXPIRE
};

/**
 * Event data payload variants.
 */
struct TradeData {
    double price;
    int64_t size;
    int exchange;
    std::string conditions;
    int tape;
};

struct QuoteData {
    double bid_price;
    int64_t bid_size;
    double ask_price;
    int64_t ask_size;
    int bid_exchange;
    int ask_exchange;
    int tape;
};

struct BarData {
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
    double vwap;
    int64_t trade_count;
};

struct OrderData {
    std::string order_id;
    std::string client_order_id;
    double qty;
    double filled_qty;
    double filled_avg_price;
    std::string status;
};

using EventPayload = std::variant<TradeData, QuoteData, BarData, OrderData>;

/**
 * A timestamped event in the simulation.
 *
 * Events are ordered by timestamp, then by sequence number
 * to ensure deterministic ordering of same-timestamp events.
 */
struct Event {
    Timestamp timestamp;
    uint64_t sequence;
    EventType event_type;
    std::string symbol;
    EventPayload data;

    // Comparison for priority queue (min-heap: earliest first)
    bool operator>(const Event& other) const {
        if (timestamp != other.timestamp) {
            return timestamp > other.timestamp;
        }
        return sequence > other.sequence;
    }
};

/**
 * Priority queue for time-ordered event processing.
 *
 * Merges multiple data streams (trades, quotes, orders)
 * into a single chronologically-sorted sequence.
 *
 * Thread-safe implementation using lock-free concurrent queue
 * for high-throughput event processing.
 */
class EventQueue {
public:
    EventQueue() : sequence_(0) {}

    /**
     * Add event to queue (thread-safe).
     */
    void push(Timestamp timestamp, EventType type,
              const std::string& symbol, EventPayload data) {
        Event event{
            .timestamp = timestamp,
            .sequence = sequence_.fetch_add(1, std::memory_order_relaxed),
            .event_type = type,
            .symbol = symbol,
            .data = std::move(data)
        };

        std::lock_guard<std::mutex> lock(mutex_);
        heap_.push(std::move(event));
    }

    /**
     * Remove and return next event (thread-safe).
     */
    std::optional<Event> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (heap_.empty()) {
            return std::nullopt;
        }
        Event event = std::move(const_cast<Event&>(heap_.top()));
        heap_.pop();
        return event;
    }

    /**
     * View next event without removing (thread-safe).
     */
    std::optional<Event> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (heap_.empty()) {
            return std::nullopt;
        }
        return heap_.top();
    }

    /**
     * Get queue size.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return heap_.size();
    }

    /**
     * Check if queue is empty.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return heap_.empty();
    }

    /**
     * Clear all events.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!heap_.empty()) {
            heap_.pop();
        }
        sequence_.store(0, std::memory_order_relaxed);
    }

private:
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> heap_;
    std::atomic<uint64_t> sequence_;
    mutable std::mutex mutex_;
};

/**
 * Lock-free event queue for high-throughput scenarios.
 *
 * Use this when single-threaded consumption is acceptable
 * but producers need lock-free push operations.
 */
class LockFreeEventQueue {
public:
    LockFreeEventQueue() : sequence_(0) {}

    /**
     * Add event (lock-free, multiple producers safe).
     */
    void push(Timestamp timestamp, EventType type,
              const std::string& symbol, EventPayload data) {
        Event event{
            .timestamp = timestamp,
            .sequence = sequence_.fetch_add(1, std::memory_order_relaxed),
            .event_type = type,
            .symbol = symbol,
            .data = std::move(data)
        };
        queue_.enqueue(std::move(event));
    }

    /**
     * Try to dequeue an event.
     */
    std::optional<Event> try_pop() {
        Event event;
        if (queue_.try_dequeue(event)) {
            return event;
        }
        return std::nullopt;
    }

    /**
     * Approximate size (may not be exact in concurrent access).
     */
    size_t size_approx() const {
        return queue_.size_approx();
    }

private:
    moodycamel::ConcurrentQueue<Event> queue_;
    std::atomic<uint64_t> sequence_;
};

} // namespace broker_sim
```

### 5.4 Core Component: Data Source

```cpp
// src/core/data_source.hpp

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <clickhouse/client.h>
#include "event_queue.hpp"

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

/**
 * Configuration for ClickHouse connection.
 */
struct DataSourceConfig {
    std::string host = "localhost";
    uint16_t port = 9000;
    std::string database = "polygon";
    std::string user = "default";
    std::string password = "";
};

/**
 * Trade data from ClickHouse.
 */
struct TradeRecord {
    Timestamp timestamp;
    std::string symbol;
    double price;
    int64_t size;
    int exchange;
    std::string conditions;
    int tape;
};

/**
 * Quote data from ClickHouse.
 */
struct QuoteRecord {
    Timestamp timestamp;
    std::string symbol;
    double bid_price;
    int64_t bid_size;
    double ask_price;
    int64_t ask_size;
    int bid_exchange;
    int ask_exchange;
    int tape;
};

/**
 * Bar/OHLCV data from ClickHouse.
 */
struct BarRecord {
    Timestamp timestamp;
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
    std::optional<double> vwap;
    std::optional<int64_t> trade_count;
};

/**
 * Timeframe to table mapping.
 */
enum class Timeframe {
    SECOND,
    MINUTE,
    FIVE_MINUTE,
    FIFTEEN_MINUTE,
    THIRTY_MINUTE,
    HOUR,
    FOUR_HOUR,
    DAY
};

/**
 * Interface to ClickHouse for historical market data.
 *
 * Provides efficient streaming queries for large datasets.
 * Uses clickhouse-cpp library for native protocol connection.
 */
class ClickHouseDataSource {
public:
    explicit ClickHouseDataSource(const DataSourceConfig& config)
        : config_(config), client_(nullptr) {}

    ~ClickHouseDataSource() {
        disconnect();
    }

    // Non-copyable
    ClickHouseDataSource(const ClickHouseDataSource&) = delete;
    ClickHouseDataSource& operator=(const ClickHouseDataSource&) = delete;

    /**
     * Establish database connection.
     */
    void connect() {
        clickhouse::ClientOptions options;
        options.SetHost(config_.host);
        options.SetPort(config_.port);
        options.SetDefaultDatabase(config_.database);
        options.SetUser(config_.user);
        options.SetPassword(config_.password);

        client_ = std::make_unique<clickhouse::Client>(options);
    }

    /**
     * Close database connection.
     */
    void disconnect() {
        client_.reset();
    }

    /**
     * Check if connected.
     */
    bool is_connected() const {
        return client_ != nullptr;
    }

    /**
     * Stream trades for given symbols in time order.
     *
     * Uses callback for memory-efficient processing.
     *
     * @param symbols List of stock tickers
     * @param start_time Start of time range
     * @param end_time End of time range
     * @param callback Function called for each trade
     */
    void stream_trades(
        const std::vector<std::string>& symbols,
        Timestamp start_time,
        Timestamp end_time,
        std::function<void(const TradeRecord&)> callback
    ) {
        std::string symbol_list = build_symbol_list(symbols);
        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);

        std::string query = fmt::format(R"(
            SELECT
                timestamp,
                symbol,
                price,
                size,
                exchange,
                conditions,
                tape
            FROM stock_trades
            WHERE symbol IN ({})
              AND timestamp >= '{}'
              AND timestamp < '{}'
            ORDER BY timestamp ASC
        )", symbol_list, start_str, end_str);

        client_->Select(query, [&callback](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                TradeRecord trade;
                trade.timestamp = extract_timestamp(block[0], row);
                trade.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
                trade.price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                trade.size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
                trade.exchange = block[4]->As<clickhouse::ColumnInt32>()->At(row);
                trade.conditions = block[5]->As<clickhouse::ColumnString>()->At(row);
                trade.tape = block[6]->As<clickhouse::ColumnInt32>()->At(row);
                callback(trade);
            }
        });
    }

    /**
     * Stream NBBO quotes for given symbols in time order.
     *
     * @param symbols List of stock tickers
     * @param start_time Start of time range
     * @param end_time End of time range
     * @param callback Function called for each quote
     */
    void stream_quotes(
        const std::vector<std::string>& symbols,
        Timestamp start_time,
        Timestamp end_time,
        std::function<void(const QuoteRecord&)> callback
    ) {
        std::string symbol_list = build_symbol_list(symbols);
        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);

        std::string query = fmt::format(R"(
            SELECT
                timestamp,
                symbol,
                bid_price,
                bid_size,
                ask_price,
                ask_size,
                bid_exchange,
                ask_exchange,
                tape
            FROM stock_quotes
            WHERE symbol IN ({})
              AND timestamp >= '{}'
              AND timestamp < '{}'
            ORDER BY timestamp ASC
        )", symbol_list, start_str, end_str);

        client_->Select(query, [&callback](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                QuoteRecord quote;
                quote.timestamp = extract_timestamp(block[0], row);
                quote.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
                quote.bid_price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                quote.bid_size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
                quote.ask_price = block[4]->As<clickhouse::ColumnFloat64>()->At(row);
                quote.ask_size = block[5]->As<clickhouse::ColumnInt64>()->At(row);
                quote.bid_exchange = block[6]->As<clickhouse::ColumnInt32>()->At(row);
                quote.ask_exchange = block[7]->As<clickhouse::ColumnInt32>()->At(row);
                quote.tape = block[8]->As<clickhouse::ColumnInt32>()->At(row);
                callback(quote);
            }
        });
    }

    /**
     * Get OHLCV bars for a symbol.
     *
     * @param symbol Stock ticker
     * @param timeframe Bar size
     * @param start_time Start of range
     * @param end_time End of range
     * @param limit Maximum bars to return
     * @return Vector of bar records
     */
    std::vector<BarRecord> get_bars(
        const std::string& symbol,
        Timeframe timeframe,
        Timestamp start_time,
        Timestamp end_time,
        size_t limit = 5000
    ) {
        std::string table = get_table_for_timeframe(timeframe);
        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);

        std::string query = fmt::format(R"(
            SELECT
                timestamp,
                open,
                high,
                low,
                close,
                volume,
                vwap,
                trade_count
            FROM {}
            WHERE symbol = '{}'
              AND timestamp >= '{}'
              AND timestamp < '{}'
            ORDER BY timestamp ASC
            LIMIT {}
        )", table, symbol, start_str, end_str, limit);

        std::vector<BarRecord> bars;

        client_->Select(query, [&bars](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                BarRecord bar;
                bar.timestamp = extract_timestamp(block[0], row);
                bar.open = block[1]->As<clickhouse::ColumnFloat64>()->At(row);
                bar.high = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                bar.low = block[3]->As<clickhouse::ColumnFloat64>()->At(row);
                bar.close = block[4]->As<clickhouse::ColumnFloat64>()->At(row);
                bar.volume = block[5]->As<clickhouse::ColumnInt64>()->At(row);

                // Handle nullable fields
                auto vwap_col = block[6]->As<clickhouse::ColumnNullable>();
                if (!vwap_col->IsNull(row)) {
                    bar.vwap = vwap_col->Nested()->As<clickhouse::ColumnFloat64>()->At(row);
                }

                auto tc_col = block[7]->As<clickhouse::ColumnNullable>();
                if (!tc_col->IsNull(row)) {
                    bar.trade_count = tc_col->Nested()->As<clickhouse::ColumnInt64>()->At(row);
                }

                bars.push_back(std::move(bar));
            }
        });

        return bars;
    }

    /**
     * Merge trades and quotes into a unified event stream.
     *
     * This is the core method for backtesting - it streams both
     * trades and quotes in strict chronological order.
     */
    void stream_events(
        const std::vector<std::string>& symbols,
        Timestamp start_time,
        Timestamp end_time,
        EventQueue& event_queue
    ) {
        // Stream trades
        stream_trades(symbols, start_time, end_time,
            [&event_queue](const TradeRecord& trade) {
                TradeData data{
                    .price = trade.price,
                    .size = trade.size,
                    .exchange = trade.exchange,
                    .conditions = trade.conditions,
                    .tape = trade.tape
                };
                event_queue.push(trade.timestamp, EventType::TRADE,
                                trade.symbol, std::move(data));
            });

        // Stream quotes
        stream_quotes(symbols, start_time, end_time,
            [&event_queue](const QuoteRecord& quote) {
                QuoteData data{
                    .bid_price = quote.bid_price,
                    .bid_size = quote.bid_size,
                    .ask_price = quote.ask_price,
                    .ask_size = quote.ask_size,
                    .bid_exchange = quote.bid_exchange,
                    .ask_exchange = quote.ask_exchange,
                    .tape = quote.tape
                };
                event_queue.push(quote.timestamp, EventType::QUOTE,
                                quote.symbol, std::move(data));
            });
    }

private:
    static std::string build_symbol_list(const std::vector<std::string>& symbols) {
        std::string result;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) result += ", ";
            result += "'" + symbols[i] + "'";
        }
        return result;
    }

    static std::string format_timestamp(Timestamp ts) {
        auto time_t = std::chrono::system_clock::to_time_t(ts);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static Timestamp extract_timestamp(const clickhouse::ColumnRef& col, size_t row) {
        auto ts_col = col->As<clickhouse::ColumnDateTime64>();
        auto micros = ts_col->At(row);
        return Timestamp{} + std::chrono::microseconds(micros);
    }

    static std::string get_table_for_timeframe(Timeframe tf) {
        static const std::unordered_map<Timeframe, std::string> table_map = {
            {Timeframe::SECOND, "stock_second_bars"},
            {Timeframe::MINUTE, "stock_minute_bars"},
            {Timeframe::FIVE_MINUTE, "stock_5minute_bars"},
            {Timeframe::FIFTEEN_MINUTE, "stock_15minute_bars"},
            {Timeframe::THIRTY_MINUTE, "stock_30minute_bars"},
            {Timeframe::HOUR, "stock_hour_bars"},
            {Timeframe::FOUR_HOUR, "stock_4hour_bars"},
            {Timeframe::DAY, "stock_daily_bars"}
        };
        auto it = table_map.find(tf);
        return it != table_map.end() ? it->second : "stock_minute_bars";
    }

    DataSourceConfig config_;
    std::unique_ptr<clickhouse::Client> client_;
};

} // namespace broker_sim
```

---

## 6. API Specifications

### 6.1 Alpaca Simulator API

#### 6.1.1 Trading Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/v2/orders` | Create new order |
| GET | `/v2/orders` | List orders (filter by status) |
| GET | `/v2/orders/{order_id}` | Get order by ID |
| DELETE | `/v2/orders/{order_id}` | Cancel order |
| DELETE | `/v2/orders` | Cancel all orders |
| PATCH | `/v2/orders/{order_id}` | Replace order |
| GET | `/v2/account` | Get account details |
| GET | `/v2/positions` | List all positions |
| GET | `/v2/positions/{symbol}` | Get position by symbol |
| DELETE | `/v2/positions/{symbol}` | Close position |
| DELETE | `/v2/positions` | Close all positions |

#### 6.1.2 Market Data Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/v2/stocks/{symbol}/trades` | Historical trades |
| GET | `/v2/stocks/{symbol}/quotes` | Historical quotes |
| GET | `/v2/stocks/{symbol}/bars` | Historical bars |
| GET | `/v2/stocks/trades/latest` | Latest trades (multi) |
| GET | `/v2/stocks/quotes/latest` | Latest quotes (multi) |

#### 6.1.3 WebSocket Streams

**Trading Stream** (`wss://localhost:8100/stream`)

Authentication:
```json
{"action": "auth", "key": "API_KEY", "secret": "API_SECRET"}
```

Subscribe:
```json
{"action": "listen", "data": {"streams": ["trade_updates"]}}
```

Trade Update Event:
```json
{
  "stream": "trade_updates",
  "data": {
    "event": "fill",
    "execution_id": "uuid",
    "order": { /* order object */ },
    "timestamp": "2025-01-15T10:30:00.123456Z",
    "price": "150.25",
    "qty": "100",
    "position_qty": "100"
  }
}
```

**Market Data Stream** (`wss://localhost:8100/v2/sip`)

Authentication:
```json
{"action": "auth", "key": "API_KEY", "secret": "API_SECRET"}
```

Subscribe:
```json
{"action": "subscribe", "trades": ["AAPL"], "quotes": ["AAPL"], "bars": ["AAPL"]}
```

### 6.2 Polygon Simulator API

#### 6.2.1 REST Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/v2/aggs/ticker/{symbol}/range/{mult}/{timespan}/{from}/{to}` | Aggregates |
| GET | `/v3/trades/{symbol}` | Historical trades |
| GET | `/v3/quotes/{symbol}` | Historical quotes |
| GET | `/v2/last/trade/{symbol}` | Last trade |
| GET | `/v2/last/nbbo/{symbol}` | Last NBBO quote |

#### 6.2.2 WebSocket Stream

**Connection:** `wss://localhost:8200/stocks`

Authentication:
```json
{"action": "auth", "params": "API_KEY"}
```

Subscribe:
```json
{"action": "subscribe", "params": "T.AAPL,Q.AAPL,AM.AAPL"}
```

Message Types:
- `T` - Trade
- `Q` - Quote
- `AM` - Aggregate per minute
- `A` - Aggregate per second

### 6.3 Finnhub Simulator API

#### 6.3.1 REST Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/quote` | Real-time quote |
| GET | `/api/v1/stock/candle` | OHLCV candles |
| GET | `/api/v1/news` | Market news |
| GET | `/api/v1/company-news` | Company-specific news |

#### 6.3.2 WebSocket Stream

**Connection:** `wss://localhost:8300?token=API_KEY`

Subscribe:
```json
{"type": "subscribe", "symbol": "AAPL"}
```

Trade Message:
```json
{
  "type": "trade",
  "data": [
    {"s": "AAPL", "p": 150.25, "t": 1705312200000, "v": 100, "c": ["1"]}
  ]
}
```

### 6.4 Control API

#### 6.4.1 Session Management

| Method | Path | Description |
|--------|------|-------------|
| POST | `/sessions` | Create new session |
| GET | `/sessions` | List all sessions |
| GET | `/sessions/{id}` | Get session details |
| DELETE | `/sessions/{id}` | Destroy session |
| POST | `/sessions/{id}/start` | Start simulation |
| POST | `/sessions/{id}/pause` | Pause simulation |
| POST | `/sessions/{id}/resume` | Resume simulation |
| POST | `/sessions/{id}/stop` | Stop simulation |

#### 6.4.2 Time Control

| Method | Path | Description |
|--------|------|-------------|
| GET | `/sessions/{id}/time` | Get current sim time |
| POST | `/sessions/{id}/time` | Set simulation time |
| POST | `/sessions/{id}/speed` | Set replay speed |

---

## 7. Data Models

### 7.1 Alpaca Models

```cpp
// src/alpaca/models.hpp

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace broker_sim::alpaca {

using Timestamp = std::chrono::system_clock::time_point;
using json = nlohmann::json;

/**
 * Order side enumeration.
 */
enum class OrderSide {
    BUY,
    SELL
};

inline std::string to_string(OrderSide side) {
    return side == OrderSide::BUY ? "buy" : "sell";
}

inline OrderSide order_side_from_string(const std::string& s) {
    return s == "buy" ? OrderSide::BUY : OrderSide::SELL;
}

/**
 * Order type enumeration.
 */
enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT,
    TRAILING_STOP
};

inline std::string to_string(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "market";
        case OrderType::LIMIT: return "limit";
        case OrderType::STOP: return "stop";
        case OrderType::STOP_LIMIT: return "stop_limit";
        case OrderType::TRAILING_STOP: return "trailing_stop";
    }
    return "market";
}

/**
 * Time-in-force enumeration.
 */
enum class TimeInForce {
    DAY,
    GTC,
    IOC,
    FOK,
    OPG,
    CLS
};

inline std::string to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::DAY: return "day";
        case TimeInForce::GTC: return "gtc";
        case TimeInForce::IOC: return "ioc";
        case TimeInForce::FOK: return "fok";
        case TimeInForce::OPG: return "opg";
        case TimeInForce::CLS: return "cls";
    }
    return "day";
}

/**
 * Order status enumeration.
 */
enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    DONE_FOR_DAY,
    CANCELED,
    EXPIRED,
    REPLACED,
    PENDING_CANCEL,
    PENDING_REPLACE,
    PENDING_NEW,
    ACCEPTED,
    REJECTED
};

inline std::string to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW: return "new";
        case OrderStatus::PARTIALLY_FILLED: return "partially_filled";
        case OrderStatus::FILLED: return "filled";
        case OrderStatus::DONE_FOR_DAY: return "done_for_day";
        case OrderStatus::CANCELED: return "canceled";
        case OrderStatus::EXPIRED: return "expired";
        case OrderStatus::REPLACED: return "replaced";
        case OrderStatus::PENDING_CANCEL: return "pending_cancel";
        case OrderStatus::PENDING_REPLACE: return "pending_replace";
        case OrderStatus::PENDING_NEW: return "pending_new";
        case OrderStatus::ACCEPTED: return "accepted";
        case OrderStatus::REJECTED: return "rejected";
    }
    return "new";
}

/**
 * Order class enumeration.
 */
enum class OrderClass {
    SIMPLE,
    BRACKET,
    OCO,
    OTO
};

/**
 * Request body for POST /v2/orders.
 */
struct CreateOrderRequest {
    std::string symbol;
    std::optional<double> qty;
    std::optional<double> notional;
    OrderSide side;
    OrderType type;
    TimeInForce time_in_force;
    std::optional<double> limit_price;
    std::optional<double> stop_price;
    std::optional<double> trail_price;
    std::optional<double> trail_percent;
    bool extended_hours = false;
    std::optional<std::string> client_order_id;
    OrderClass order_class = OrderClass::SIMPLE;

    static CreateOrderRequest from_json(const json& j);
};

/**
 * Order object returned by API.
 */
struct Order {
    std::string id;
    std::string client_order_id;
    Timestamp created_at;
    Timestamp updated_at;
    std::optional<Timestamp> submitted_at;
    std::optional<Timestamp> filled_at;
    std::optional<Timestamp> expired_at;
    std::optional<Timestamp> canceled_at;
    std::optional<Timestamp> failed_at;
    std::optional<Timestamp> replaced_at;
    std::optional<std::string> replaced_by;
    std::optional<std::string> replaces;
    std::string asset_id;
    std::string symbol;
    std::string asset_class = "us_equity";
    std::optional<double> notional;
    std::optional<double> qty;
    double filled_qty = 0.0;
    std::optional<double> filled_avg_price;
    std::string order_class_str;
    std::string order_type_str;
    std::string type_str;
    std::string side_str;
    std::optional<std::string> position_intent;
    std::string time_in_force_str;
    std::optional<double> limit_price;
    std::optional<double> stop_price;
    OrderStatus status;
    bool extended_hours = false;
    std::vector<Order> legs;
    std::optional<double> trail_percent;
    std::optional<double> trail_price;
    std::optional<double> hwm;  // High-water mark for trailing stops
    std::string source = "api";

    json to_json() const;
};

/**
 * Position object returned by API.
 */
struct Position {
    std::string asset_id;
    std::string symbol;
    std::string exchange;
    std::string asset_class = "us_equity";
    bool asset_marginable = true;
    double qty;
    double avg_entry_price;
    std::string side;  // "long" or "short"
    double market_value;
    double cost_basis;
    double unrealized_pl;
    double unrealized_plpc;
    double unrealized_intraday_pl;
    double unrealized_intraday_plpc;
    double current_price;
    double lastday_price;
    double change_today;
    double qty_available;

    json to_json() const;
};

/**
 * Account object returned by API.
 */
struct Account {
    std::string id;
    std::string account_number;
    std::string status = "ACTIVE";
    std::string crypto_status = "ACTIVE";
    std::string currency = "USD";
    double buying_power;
    double regt_buying_power;
    double daytrading_buying_power;
    double effective_buying_power;
    double non_marginable_buying_power;
    double options_buying_power;
    double cash;
    double accrued_fees = 0.0;
    double portfolio_value;
    bool pattern_day_trader = false;
    bool trading_blocked = false;
    bool transfers_blocked = false;
    bool account_blocked = false;
    bool shorting_enabled = true;
    bool trade_suspended_by_user = false;
    Timestamp created_at;
    double multiplier = 2.0;
    double equity;
    double last_equity;
    double long_market_value;
    double short_market_value;
    double position_market_value;
    double initial_margin;
    double maintenance_margin;
    double last_maintenance_margin;
    double sma;
    int daytrade_count = 0;
    int crypto_tier = 1;

    json to_json() const;
};

} // namespace broker_sim::alpaca
```

### 7.2 Polygon Models

```cpp
// src/polygon/models.hpp

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace broker_sim::polygon {

using json = nlohmann::json;

/**
 * OHLCV bar from aggregates endpoint.
 */
struct AggregateBar {
    int64_t v;                      // Volume
    std::optional<double> vw;       // Volume-weighted average price
    double o;                       // Open price
    double c;                       // Close price
    double h;                       // High price
    double l;                       // Low price
    int64_t t;                      // Unix timestamp in milliseconds
    std::optional<int64_t> n;       // Number of transactions
    std::optional<bool> otc;        // OTC indicator

    json to_json() const;
};

/**
 * Response from /v2/aggs endpoint.
 */
struct AggregatesResponse {
    std::string ticker;
    bool adjusted;
    int query_count;
    int results_count;
    std::string status;
    std::vector<AggregateBar> results;
    std::optional<std::string> next_url;
    std::string request_id;

    json to_json() const;
};

/**
 * Trade from /v3/trades endpoint.
 */
struct Trade {
    std::optional<std::vector<int>> conditions;
    int exchange;
    std::string id;
    int64_t participant_timestamp;  // nanoseconds
    double price;
    int64_t sequence_number;
    int64_t sip_timestamp;          // nanoseconds
    int size;
    int tape;
    std::optional<int> trf_id;
    std::optional<int64_t> trf_timestamp;

    json to_json() const;
};

/**
 * Response from /v3/trades endpoint.
 */
struct TradesResponse {
    std::vector<Trade> results;
    std::string status;
    std::string request_id;
    std::optional<std::string> next_url;

    json to_json() const;
};

/**
 * Quote from /v3/quotes endpoint.
 */
struct Quote {
    std::optional<int> ask_exchange;
    std::optional<double> ask_price;
    std::optional<int> ask_size;
    std::optional<int> bid_exchange;
    std::optional<double> bid_price;
    std::optional<int> bid_size;
    std::optional<std::vector<int>> conditions;
    std::optional<std::vector<int>> indicators;
    int64_t participant_timestamp;  // nanoseconds
    int64_t sequence_number;
    int64_t sip_timestamp;          // nanoseconds
    int tape;
    std::optional<int64_t> trf_timestamp;

    json to_json() const;
};

/**
 * Response from /v3/quotes endpoint.
 */
struct QuotesResponse {
    std::vector<Quote> results;
    std::string status;
    std::string request_id;
    std::optional<std::string> next_url;

    json to_json() const;
};

/**
 * Last trade from /v2/last/trade endpoint.
 */
struct LastTrade {
    std::string T;                   // Symbol
    std::optional<std::vector<int>> c;  // Conditions
    std::string i;                   // Trade ID
    double p;                        // Price
    int64_t q;                       // Sequence number
    int s;                           // Size
    int64_t t;                       // SIP timestamp (nanoseconds)
    int x;                           // Exchange ID
    int64_t y;                       // Participant timestamp (nanoseconds)
    int z;                           // Tape

    json to_json() const;
};

/**
 * Last NBBO from /v2/last/nbbo endpoint.
 */
struct LastNBBO {
    std::string T;                   // Symbol
    double P;                        // Ask price
    int S;                           // Ask size
    int X;                           // Ask exchange
    std::optional<std::vector<int>> i;  // Indicators
    double p;                        // Bid price
    int64_t q;                       // Sequence number
    int s;                           // Bid size
    int64_t t;                       // SIP timestamp (nanoseconds)
    int x;                           // Bid exchange
    int64_t y;                       // Participant timestamp (nanoseconds)
    int z;                           // Tape

    json to_json() const;
};

// WebSocket message models

/**
 * Trade message for WebSocket stream.
 */
struct WSTradeMessage {
    std::string ev = "T";
    std::string sym;
    int x;                           // exchange
    std::string i;                   // trade id
    int z;                           // tape
    double p;                        // price
    int s;                           // size
    std::optional<std::vector<int>> c;  // conditions
    int64_t t;                       // timestamp (ms)
    int64_t q;                       // sequence number
    std::optional<int> trfi;
    std::optional<int64_t> trft;

    json to_json() const;
};

/**
 * Quote message for WebSocket stream.
 */
struct WSQuoteMessage {
    std::string ev = "Q";
    std::string sym;
    int bx;                          // bid exchange
    double bp;                       // bid price
    int bs;                          // bid size
    int ax;                          // ask exchange
    double ap;                       // ask price
    int as_;                         // ask size (as is reserved keyword)
    std::optional<int> c;            // condition
    std::optional<std::vector<int>> i;  // indicators
    int64_t t;                       // timestamp (ms)
    int64_t q;                       // sequence number
    int z;                           // tape

    json to_json() const;
};

/**
 * Aggregate bar message for WebSocket stream.
 */
struct WSAggregateMessage {
    std::string ev;                  // "AM" or "A"
    std::string sym;
    int64_t v;                       // volume
    int64_t av;                      // accumulated volume
    double op;                       // official open price
    double vw;                       // VWAP
    double o;                        // open
    double c;                        // close
    double h;                        // high
    double l;                        // low
    double a;                        // VWAP for day
    std::optional<int> z;            // average trade size
    int64_t s;                       // start timestamp (ms)
    int64_t e;                       // end timestamp (ms)
    std::optional<bool> otc;

    json to_json() const;
};

} // namespace broker_sim::polygon
```

### 7.3 Finnhub Models

```cpp
// src/finnhub/models.hpp

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace broker_sim::finnhub {

using json = nlohmann::json;

/**
 * Quote from /api/v1/quote endpoint.
 */
struct Quote {
    double c;                        // Current price
    double d;                        // Change
    double dp;                       // Percent change
    double h;                        // High price of the day
    double l;                        // Low price of the day
    double o;                        // Open price of the day
    double pc;                       // Previous close price
    int64_t t;                       // Timestamp (Unix seconds)

    json to_json() const {
        return json{
            {"c", c}, {"d", d}, {"dp", dp},
            {"h", h}, {"l", l}, {"o", o},
            {"pc", pc}, {"t", t}
        };
    }
};

/**
 * OHLCV candle data.
 */
struct Candle {
    std::vector<double> c;           // Close prices
    std::vector<double> h;           // High prices
    std::vector<double> l;           // Low prices
    std::vector<double> o;           // Open prices
    std::vector<int64_t> t;          // Timestamps (Unix seconds)
    std::vector<double> v;           // Volumes
    std::string s;                   // Status: "ok" or "no_data"

    json to_json() const {
        return json{
            {"c", c}, {"h", h}, {"l", l},
            {"o", o}, {"t", t}, {"v", v}, {"s", s}
        };
    }
};

/**
 * News article from news endpoints.
 */
struct NewsArticle {
    std::string category;
    int64_t datetime;                // Unix seconds
    std::string headline;
    int64_t id;
    std::string image;
    std::string related;
    std::string source;
    std::string summary;
    std::string url;

    json to_json() const {
        return json{
            {"category", category},
            {"datetime", datetime},
            {"headline", headline},
            {"id", id},
            {"image", image},
            {"related", related},
            {"source", source},
            {"summary", summary},
            {"url", url}
        };
    }
};

/**
 * Single trade in WebSocket message.
 */
struct WSTradeData {
    std::string s;                   // Symbol
    double p;                        // Price
    int64_t t;                       // Timestamp (Unix milliseconds)
    double v;                        // Volume
    std::optional<std::vector<std::string>> c;  // Conditions

    json to_json() const {
        json j = {{"s", s}, {"p", p}, {"t", t}, {"v", v}};
        if (c) j["c"] = *c;
        return j;
    }
};

/**
 * Trade message from WebSocket.
 */
struct WSTradeMessage {
    std::string type = "trade";
    std::vector<WSTradeData> data;

    json to_json() const {
        json data_arr = json::array();
        for (const auto& d : data) {
            data_arr.push_back(d.to_json());
        }
        return json{{"type", type}, {"data", data_arr}};
    }
};

} // namespace broker_sim::finnhub
```

---

## 8. Time Travel & Event Ordering

### 8.1 Preventing Look-Ahead Bias

The simulation must never allow strategies to see future data. This is achieved through:

1. **Single Global Clock**: All components reference `TimeEngine.current_time`
2. **Chronological Processing**: Events processed in strict timestamp order
3. **Order Execution Delay**: Orders execute on next tick, not current tick

```
Timeline Visualization:
═══════════════════════════════════════════════════════════════════

Time:     T0          T1          T2          T3          T4
          │           │           │           │           │
Events:   Trade       Quote       Trade       Quote       Trade
          AAPL        AAPL        AAPL        AAPL        AAPL
          $150.00     B:149.99    $150.05     B:150.04    $150.10
                      A:150.01                A:150.06
          │           │           │           │           │
          ▼           ▼           ▼           ▼           ▼
Strategy: Receives    Receives    Strategy    Order       Fill
          T0 data     T1 data     places      queued      executed
                                  BUY order   for T3      at $150.06
                                  at T2                   (ask)
          │           │           │           │           │
          └───────────┴───────────┴───────────┴───────────┘
                    Strategy can ONLY see past data
                    Order placed at T2 executes at T3

═══════════════════════════════════════════════════════════════════
```

### 8.2 Event Ordering Rules

1. **Primary Sort**: Timestamp (ascending)
2. **Secondary Sort**: Sequence number (for same-timestamp events)
3. **Tertiary Sort**: Event type priority:
   - Market data events (trades, quotes) before order events
   - Order events before fill events

### 8.3 Time Control API

```python
# Session time control endpoints

POST /sessions/{id}/time
{
    "timestamp": "2025-01-15T09:30:00Z"
}

POST /sessions/{id}/speed
{
    "speed_factor": 10.0  // 10x real-time, 0 for max speed
}

POST /sessions/{id}/fast_forward
{
    "target_time": "2025-01-15T10:00:00Z",
    "emit_events": false  // Skip event emission for speed
}
```

---

## 9. Order Matching Engine

### 9.1 Matching Logic

The order matching engine simulates fills using NBBO data since we don't have full order book depth.

```cpp
// src/alpaca/services/matching_engine.hpp

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <algorithm>
#include "../models.hpp"

namespace broker_sim::alpaca {

/**
 * Current National Best Bid/Offer.
 */
struct NBBO {
    std::string symbol;
    double bid_price;
    int64_t bid_size;
    double ask_price;
    int64_t ask_size;
    int64_t timestamp;

    double mid_price() const {
        return (bid_price + ask_price) / 2.0;
    }
};

/**
 * Represents an order fill.
 */
struct Fill {
    std::string order_id;
    double fill_qty;
    double fill_price;
    int64_t timestamp;
    bool is_partial;
};

/**
 * Simulates order execution against NBBO quotes.
 *
 * Execution Rules:
 * 1. Market orders fill immediately at NBBO (buy at ask, sell at bid)
 * 2. Limit orders fill when price crosses or touches limit
 * 3. Stop orders become market orders when stop price triggered
 * 4. Stop-limit orders become limit orders when stop triggered
 * 5. Trailing stops track high-water mark (HWM)
 *
 * Thread-safe: All public methods are protected by mutex.
 */
class MatchingEngine {
public:
    MatchingEngine() = default;

    /**
     * Update NBBO and check for fills on pending orders.
     *
     * @param nbbo Current NBBO for a symbol
     * @return List of fills generated
     */
    std::vector<Fill> update_nbbo(const NBBO& nbbo) {
        std::lock_guard<std::mutex> lock(mutex_);

        current_nbbo_[nbbo.symbol] = nbbo;
        std::vector<Fill> fills;

        // Check all pending orders for this symbol
        auto it = pending_orders_.begin();
        while (it != pending_orders_.end()) {
            if (it->second.symbol != nbbo.symbol) {
                ++it;
                continue;
            }

            auto fill = try_fill(it->second, nbbo);
            if (fill) {
                fills.push_back(*fill);
                if (!fill->is_partial) {
                    it = pending_orders_.erase(it);
                    continue;
                }
            }
            ++it;
        }

        return fills;
    }

    /**
     * Submit new order for execution.
     *
     * @param order Order to submit
     * @return Immediate fill if applicable, or nullopt if order queued
     */
    std::optional<Fill> submit_order(Order& order) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto nbbo_it = current_nbbo_.find(order.symbol);
        if (nbbo_it == current_nbbo_.end()) {
            // No NBBO yet, queue order
            pending_orders_[order.id] = order;
            return std::nullopt;
        }

        const NBBO& nbbo = nbbo_it->second;

        switch (order.type) {
            case OrderType::MARKET:
                return execute_market_order(order, nbbo);

            case OrderType::LIMIT:
                if (is_marketable_limit(order, nbbo)) {
                    return execute_limit_order(order, nbbo);
                }
                pending_orders_[order.id] = order;
                return std::nullopt;

            case OrderType::STOP:
                if (is_stop_triggered(order, nbbo)) {
                    return execute_market_order(order, nbbo);
                }
                pending_orders_[order.id] = order;
                return std::nullopt;

            case OrderType::STOP_LIMIT:
                if (is_stop_triggered(order, nbbo)) {
                    if (is_marketable_limit(order, nbbo)) {
                        return execute_limit_order(order, nbbo);
                    }
                }
                pending_orders_[order.id] = order;
                return std::nullopt;

            case OrderType::TRAILING_STOP:
                update_trailing_stop_hwm(order, nbbo);
                if (is_trailing_stop_triggered(order, nbbo)) {
                    return execute_market_order(order, nbbo);
                }
                pending_orders_[order.id] = order;
                return std::nullopt;
        }

        return std::nullopt;
    }

    /**
     * Cancel a pending order.
     *
     * @param order_id Order ID to cancel
     * @return true if order was found and canceled
     */
    bool cancel_order(const std::string& order_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_orders_.erase(order_id) > 0;
    }

    /**
     * Get current NBBO for a symbol.
     */
    std::optional<NBBO> get_nbbo(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = current_nbbo_.find(symbol);
        if (it != current_nbbo_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * Get all pending orders.
     */
    std::vector<Order> get_pending_orders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Order> orders;
        orders.reserve(pending_orders_.size());
        for (const auto& [id, order] : pending_orders_) {
            orders.push_back(order);
        }
        return orders;
    }

private:
    std::optional<Fill> try_fill(Order& order, const NBBO& nbbo) {
        switch (order.type) {
            case OrderType::MARKET:
                return execute_market_order(order, nbbo);

            case OrderType::LIMIT:
                if (is_marketable_limit(order, nbbo)) {
                    return execute_limit_order(order, nbbo);
                }
                break;

            case OrderType::STOP:
                if (is_stop_triggered(order, nbbo)) {
                    return execute_market_order(order, nbbo);
                }
                break;

            case OrderType::STOP_LIMIT:
                if (is_stop_triggered(order, nbbo)) {
                    if (is_marketable_limit(order, nbbo)) {
                        return execute_limit_order(order, nbbo);
                    }
                }
                break;

            case OrderType::TRAILING_STOP:
                update_trailing_stop_hwm(order, nbbo);
                if (is_trailing_stop_triggered(order, nbbo)) {
                    return execute_market_order(order, nbbo);
                }
                break;
        }
        return std::nullopt;
    }

    /**
     * Execute market order at current NBBO.
     */
    Fill execute_market_order(const Order& order, const NBBO& nbbo) {
        double fill_price;
        int64_t available_size;

        if (order.side == OrderSide::BUY) {
            fill_price = nbbo.ask_price;
            available_size = nbbo.ask_size;
        } else {
            fill_price = nbbo.bid_price;
            available_size = nbbo.bid_size;
        }

        double remaining = order.qty.value_or(0.0) - order.filled_qty;
        double fill_qty = std::min(remaining, static_cast<double>(available_size));
        bool is_partial = fill_qty < remaining;

        return Fill{
            .order_id = order.id,
            .fill_qty = fill_qty,
            .fill_price = fill_price,
            .timestamp = nbbo.timestamp,
            .is_partial = is_partial
        };
    }

    /**
     * Execute limit order at limit price or better.
     */
    Fill execute_limit_order(const Order& order, const NBBO& nbbo) {
        double fill_price;
        int64_t available_size;

        if (order.side == OrderSide::BUY) {
            // Buy at ask or limit, whichever is lower
            fill_price = std::min(nbbo.ask_price, order.limit_price.value_or(nbbo.ask_price));
            available_size = nbbo.ask_size;
        } else {
            // Sell at bid or limit, whichever is higher
            fill_price = std::max(nbbo.bid_price, order.limit_price.value_or(nbbo.bid_price));
            available_size = nbbo.bid_size;
        }

        double remaining = order.qty.value_or(0.0) - order.filled_qty;
        double fill_qty = std::min(remaining, static_cast<double>(available_size));
        bool is_partial = fill_qty < remaining;

        return Fill{
            .order_id = order.id,
            .fill_qty = fill_qty,
            .fill_price = fill_price,
            .timestamp = nbbo.timestamp,
            .is_partial = is_partial
        };
    }

    /**
     * Check if limit order would execute immediately.
     */
    bool is_marketable_limit(const Order& order, const NBBO& nbbo) const {
        if (!order.limit_price) return false;

        if (order.side == OrderSide::BUY) {
            return *order.limit_price >= nbbo.ask_price;
        } else {
            return *order.limit_price <= nbbo.bid_price;
        }
    }

    /**
     * Check if stop order should be activated.
     */
    bool is_stop_triggered(const Order& order, const NBBO& nbbo) const {
        if (!order.stop_price) return false;

        double trigger_price = nbbo.mid_price();

        if (order.side == OrderSide::BUY) {
            return trigger_price >= *order.stop_price;
        } else {
            return trigger_price <= *order.stop_price;
        }
    }

    /**
     * Check if trailing stop should execute.
     */
    bool is_trailing_stop_triggered(const Order& order, const NBBO& nbbo) const {
        if (!order.hwm) return false;

        double trigger_price = nbbo.mid_price();

        if (order.side == OrderSide::SELL) {
            // For long position trailing stop
            if (order.trail_price) {
                return trigger_price <= *order.hwm - *order.trail_price;
            } else if (order.trail_percent) {
                return trigger_price <= *order.hwm * (1.0 - *order.trail_percent / 100.0);
            }
        } else {
            // For short position trailing stop
            if (order.trail_price) {
                return trigger_price >= *order.hwm + *order.trail_price;
            } else if (order.trail_percent) {
                return trigger_price >= *order.hwm * (1.0 + *order.trail_percent / 100.0);
            }
        }

        return false;
    }

    /**
     * Update high-water mark for trailing stop.
     */
    void update_trailing_stop_hwm(Order& order, const NBBO& nbbo) {
        double current_price = nbbo.mid_price();

        if (!order.hwm) {
            order.hwm = current_price;
        } else if (order.side == OrderSide::SELL) {
            // Track highest price for sell trailing stop
            order.hwm = std::max(*order.hwm, current_price);
        } else {
            // Track lowest price for buy trailing stop
            order.hwm = std::min(*order.hwm, current_price);
        }
    }

    std::unordered_map<std::string, NBBO> current_nbbo_;
    std::unordered_map<std::string, Order> pending_orders_;
    mutable std::mutex mutex_;
};

} // namespace broker_sim::alpaca
```

### 9.2 Partial Fill Handling

```
Order: BUY 1000 AAPL @ Market

NBBO at T1: ask = $150.00, ask_size = 300
NBBO at T2: ask = $150.02, ask_size = 400
NBBO at T3: ask = $150.05, ask_size = 500

Fills:
T1: 300 shares @ $150.00
T2: 400 shares @ $150.02
T3: 300 shares @ $150.05 (order complete)

Final: filled_qty = 1000, filled_avg_price = $150.024
```

### 9.3 Latency & Slippage Configuration

For realistic backtesting, the matching engine supports configurable latency and slippage simulation:

```cpp
// src/alpaca/services/execution_config.hpp

#pragma once

namespace broker_sim::alpaca {

/**
 * Configuration for realistic order execution simulation.
 */
struct ExecutionConfig {
    // Latency simulation
    bool enable_latency = false;
    int64_t fixed_latency_us = 0;       // Fixed latency in microseconds
    int64_t random_latency_max_us = 0;  // Random additional latency [0, max)

    // Slippage simulation
    bool enable_slippage = false;
    double fixed_slippage_bps = 0.0;    // Fixed slippage in basis points
    double random_slippage_max_bps = 0.0; // Random additional slippage [0, max)

    // Execution behavior
    bool execute_on_next_tick = true;   // If true, orders execute on next NBBO update
    bool respect_nbbo_size = true;      // If true, partial fills based on NBBO size

    /**
     * Calculate slippage-adjusted price.
     *
     * @param price Original execution price
     * @param is_buy True for buy orders (slippage increases price)
     * @return Adjusted price with slippage applied
     */
    double apply_slippage(double price, bool is_buy) const {
        if (!enable_slippage) return price;

        double slippage_bps = fixed_slippage_bps;
        if (random_slippage_max_bps > 0) {
            // Add random component (implementation uses thread-local RNG)
            slippage_bps += random_double() * random_slippage_max_bps;
        }

        double multiplier = 1.0 + (slippage_bps / 10000.0);
        return is_buy ? price * multiplier : price / multiplier;
    }

    /**
     * Calculate execution delay in microseconds.
     */
    int64_t get_latency_us() const {
        if (!enable_latency) return 0;

        int64_t latency = fixed_latency_us;
        if (random_latency_max_us > 0) {
            latency += static_cast<int64_t>(random_double() * random_latency_max_us);
        }
        return latency;
    }

private:
    static double random_double() {
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng);
    }
};

} // namespace broker_sim::alpaca
```

**Configuration Examples:**

| Scenario | Settings |
|----------|----------|
| **Ideal (no friction)** | `enable_latency=false`, `enable_slippage=false` |
| **Conservative estimate** | `fixed_latency_us=1000`, `fixed_slippage_bps=1.0` |
| **Realistic retail** | `fixed_latency_us=5000`, `random_latency_max_us=10000`, `fixed_slippage_bps=2.0`, `random_slippage_max_bps=3.0` |
| **Stress test** | `fixed_latency_us=50000`, `fixed_slippage_bps=10.0` |

### 9.4 Reg-T Margin & Buying Power Checks

Before executing orders, the matching engine validates that sufficient buying power is available. This prevents unrealistic backtests where strategies overspend available capital.

```cpp
// src/alpaca/services/margin_calculator.hpp

#pragma once

#include <cstdint>
#include <optional>

namespace broker_sim::alpaca {

/**
 * Reg-T margin requirements and buying power calculation.
 *
 * Standard Reg-T margin: 50% initial, 25% maintenance
 * Pattern Day Trader (PDT): $25k minimum, 4x intraday buying power
 */
struct MarginConfig {
    double initial_margin_rate = 0.50;      // 50% for Reg-T
    double maintenance_margin_rate = 0.25;  // 25% maintenance
    bool pattern_day_trader = false;        // If true, 4x intraday BP
    double pdt_minimum_equity = 25000.0;    // PDT minimum account value
};

struct BuyingPowerResult {
    double buying_power;              // Available buying power
    double regt_buying_power;         // Reg-T overnight BP (2x)
    double daytrading_buying_power;   // PDT intraday BP (4x if qualified)
    bool is_marginable;               // True if position is marginable
};

class MarginCalculator {
public:
    explicit MarginCalculator(const MarginConfig& config) : config_(config) {}

    /**
     * Calculate available buying power for an account.
     */
    BuyingPowerResult calculate_buying_power(
        double cash,
        double long_market_value,
        double short_market_value,
        double maintenance_margin_used
    ) const {
        double equity = cash + long_market_value - short_market_value;
        double excess_margin = equity - maintenance_margin_used;

        BuyingPowerResult result;
        result.regt_buying_power = excess_margin / config_.initial_margin_rate;

        if (config_.pattern_day_trader && equity >= config_.pdt_minimum_equity) {
            // PDT gets 4x intraday buying power
            result.daytrading_buying_power = excess_margin * 4.0;
        } else {
            result.daytrading_buying_power = result.regt_buying_power;
        }

        result.buying_power = result.daytrading_buying_power;
        result.is_marginable = true;
        return result;
    }

    /**
     * Check if order can be executed with available buying power.
     * Returns nullopt if valid, or rejection reason if insufficient.
     */
    std::optional<std::string> validate_order(
        const Order& order,
        const Account& account,
        double current_price
    ) const {
        double order_value = order.qty * current_price;
        double required_bp = order_value;

        // For margin accounts, only need 50% initial margin
        if (account.margin_enabled) {
            required_bp = order_value * config_.initial_margin_rate;
        }

        // Check buying power
        double available_bp = account.is_intraday_trade
            ? account.daytrading_buying_power
            : account.regt_buying_power;

        if (required_bp > available_bp) {
            return "insufficient buying power: required=" +
                   std::to_string(required_bp) +
                   ", available=" + std::to_string(available_bp);
        }

        // PDT rule check: cannot day trade if equity < $25k
        if (account.is_intraday_trade &&
            account.pattern_day_trader &&
            account.equity < config_.pdt_minimum_equity) {
            return "PDT minimum equity not met: $" +
                   std::to_string(account.equity) + " < $25,000";
        }

        return std::nullopt;  // Order is valid
    }

private:
    MarginConfig config_;
};

} // namespace broker_sim::alpaca
```

**Order Validation Flow:**

1. Order submitted via REST `/v2/orders`
2. Matching engine calls `MarginCalculator::validate_order()`
3. If validation fails:
   - Order status set to `rejected`
   - WS emits `{event:"rejected", order:{...}, reason:"insufficient buying power"}`
   - REST returns 403 with error message
4. If validation passes:
   - Order proceeds to matching logic
   - Buying power reserved until fill or cancel

**Buying Power Updates:**

| Event | Buying Power Change |
|-------|---------------------|
| Order submitted | Reduce by order value × margin rate |
| Order filled | Reduce by fill value, release excess |
| Order canceled | Release reserved buying power |
| Position closed | Increase by position proceeds |

---

## 10. WebSocket Streaming

### 10.1 Alpaca Trade Updates

```cpp
// src/alpaca/websocket/trade_updates_ws.hpp

#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

namespace broker_sim::alpaca {

using json = nlohmann::json;
using WebSocketConnectionPtr = drogon::WebSocketConnectionPtr;

/**
 * Handles Alpaca trade_updates WebSocket stream.
 *
 * Implements Drogon WebSocket controller for real-time trade updates.
 */
class TradeUpdatesWsController : public drogon::WebSocketController<TradeUpdatesWsController> {
public:
    /**
     * Handle new WebSocket connection.
     */
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                              const WebSocketConnectionPtr& wsConnPtr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.insert(wsConnPtr);
        LOG_INFO << "New Alpaca trade updates client connected";
    }

    /**
     * Handle incoming WebSocket message.
     */
    void handleNewMessage(const WebSocketConnectionPtr& wsConnPtr,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        if (type != drogon::WebSocketMessageType::Text) {
            return;
        }

        try {
            auto data = json::parse(message);
            std::string action = data.value("action", "");

            if (action == "auth") {
                handle_auth(wsConnPtr, data);
            } else if (action == "listen") {
                handle_listen(wsConnPtr, data);
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Error parsing WebSocket message: " << e.what();
        }
    }

    /**
     * Handle connection close.
     */
    void handleConnectionClosed(const WebSocketConnectionPtr& wsConnPtr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(wsConnPtr);
        authenticated_clients_.erase(wsConnPtr);
        LOG_INFO << "Alpaca trade updates client disconnected";
    }

    /**
     * Broadcast trade update to all authenticated clients.
     */
    void broadcast_trade_update(const std::string& session_id, const json& event) {
        json message = {
            {"stream", "trade_updates"},
            {"data", event}
        };
        std::string msg_str = message.dump();

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [conn, key] : authenticated_clients_) {
            if (auto ptr = conn.lock()) {
                ptr->send(msg_str);
            }
        }
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/stream", drogon::Get);
    WS_PATH_LIST_END

private:
    void handle_auth(const WebSocketConnectionPtr& wsConnPtr, const json& data) {
        std::string key = data.value("key", "");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            authenticated_clients_[wsConnPtr] = key;
        }

        json response = {
            {"stream", "authorization"},
            {"data", {
                {"status", "authorized"},
                {"action", "authenticate"}
            }}
        };
        wsConnPtr->send(response.dump());
    }

    void handle_listen(const WebSocketConnectionPtr& wsConnPtr, const json& data) {
        std::vector<std::string> streams;
        if (data.contains("data") && data["data"].contains("streams")) {
            streams = data["data"]["streams"].get<std::vector<std::string>>();
        }

        json response = {
            {"stream", "listening"},
            {"data", {{"streams", streams}}}
        };
        wsConnPtr->send(response.dump());
    }

    std::unordered_set<WebSocketConnectionPtr> clients_;
    std::unordered_map<WebSocketConnectionPtr, std::string> authenticated_clients_;
    mutable std::mutex mutex_;
};

} // namespace broker_sim::alpaca
```

### 10.2 Polygon Market Data

```cpp
// src/polygon/websocket/market_data_ws.hpp

#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

namespace broker_sim::polygon {

using json = nlohmann::json;
using WebSocketConnectionPtr = drogon::WebSocketConnectionPtr;

/**
 * Handles Polygon market data WebSocket stream.
 *
 * Supports subscriptions to:
 * - T.{symbol} - Trades
 * - Q.{symbol} - Quotes
 * - AM.{symbol} - Minute aggregates
 * - A.{symbol} - Second aggregates
 */
class MarketDataWsController : public drogon::WebSocketController<MarketDataWsController> {
public:
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                              const WebSocketConnectionPtr& wsConnPtr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.insert(wsConnPtr);
        subscriptions_[wsConnPtr] = {};

        // Send connection confirmation
        json response = json::array({{
            {"ev", "status"},
            {"status", "connected"},
            {"message", "Connected Successfully"}
        }});
        wsConnPtr->send(response.dump());

        LOG_INFO << "New Polygon market data client connected";
    }

    void handleNewMessage(const WebSocketConnectionPtr& wsConnPtr,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        if (type != drogon::WebSocketMessageType::Text) {
            return;
        }

        try {
            auto data = json::parse(message);
            std::string action = data.value("action", "");

            if (action == "auth") {
                handle_auth(wsConnPtr, data);
            } else if (action == "subscribe") {
                handle_subscribe(wsConnPtr, data);
            } else if (action == "unsubscribe") {
                handle_unsubscribe(wsConnPtr, data);
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Error parsing WebSocket message: " << e.what();
        }
    }

    void handleConnectionClosed(const WebSocketConnectionPtr& wsConnPtr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(wsConnPtr);
        subscriptions_.erase(wsConnPtr);
        authenticated_.erase(wsConnPtr);
        LOG_INFO << "Polygon market data client disconnected";
    }

    /**
     * Broadcast trade to subscribed clients.
     */
    void broadcast_trade(const std::string& symbol, int exchange,
                         const std::string& trade_id, int tape,
                         double price, int size, int64_t timestamp,
                         int64_t sequence) {
        std::string channel = "T." + symbol;

        json message = json::array({{
            {"ev", "T"},
            {"sym", symbol},
            {"x", exchange},
            {"i", trade_id},
            {"z", tape},
            {"p", price},
            {"s", size},
            {"t", timestamp},
            {"q", sequence}
        }});

        send_to_subscribers(channel, message.dump());
    }

    /**
     * Broadcast quote to subscribed clients.
     */
    void broadcast_quote(const std::string& symbol,
                         int bid_exchange, double bid_price, int bid_size,
                         int ask_exchange, double ask_price, int ask_size,
                         int tape, int64_t timestamp, int64_t sequence) {
        std::string channel = "Q." + symbol;

        json message = json::array({{
            {"ev", "Q"},
            {"sym", symbol},
            {"bx", bid_exchange},
            {"bp", bid_price},
            {"bs", bid_size},
            {"ax", ask_exchange},
            {"ap", ask_price},
            {"as", ask_size},
            {"z", tape},
            {"t", timestamp},
            {"q", sequence}
        }});

        send_to_subscribers(channel, message.dump());
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/stocks", drogon::Get);
    WS_PATH_LIST_END

private:
    void handle_auth(const WebSocketConnectionPtr& wsConnPtr, const json& data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            authenticated_.insert(wsConnPtr);
        }

        json response = json::array({{
            {"ev", "status"},
            {"status", "auth_success"},
            {"message", "authenticated"}
        }});
        wsConnPtr->send(response.dump());
    }

    void handle_subscribe(const WebSocketConnectionPtr& wsConnPtr, const json& data) {
        std::string params = data.value("params", "");
        auto channels = split_channels(params);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& channel : channels) {
                subscriptions_[wsConnPtr].insert(channel);
            }
        }

        json response = json::array({{
            {"ev", "status"},
            {"status", "success"},
            {"message", "subscribed to: " + params}
        }});
        wsConnPtr->send(response.dump());
    }

    void handle_unsubscribe(const WebSocketConnectionPtr& wsConnPtr, const json& data) {
        std::string params = data.value("params", "");
        auto channels = split_channels(params);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& channel : channels) {
                subscriptions_[wsConnPtr].erase(channel);
            }
        }

        json response = json::array({{
            {"ev", "status"},
            {"status", "success"},
            {"message", "unsubscribed from: " + params}
        }});
        wsConnPtr->send(response.dump());
    }

    void send_to_subscribers(const std::string& channel, const std::string& message) {
        // Extract channel type (T, Q, AM, A)
        std::string channel_type = channel.substr(0, channel.find('.'));
        std::string wildcard = channel_type + ".*";

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [conn, subs] : subscriptions_) {
            if (subs.count(channel) || subs.count(wildcard)) {
                if (auto ptr = conn.lock()) {
                    ptr->send(message);
                }
            }
        }
    }

    static std::vector<std::string> split_channels(const std::string& params) {
        std::vector<std::string> channels;
        std::istringstream ss(params);
        std::string channel;
        while (std::getline(ss, channel, ',')) {
            // Trim whitespace
            size_t start = channel.find_first_not_of(" \t");
            size_t end = channel.find_last_not_of(" \t");
            if (start != std::string::npos) {
                channels.push_back(channel.substr(start, end - start + 1));
            }
        }
        return channels;
    }

    std::unordered_set<WebSocketConnectionPtr> clients_;
    std::unordered_map<WebSocketConnectionPtr, std::unordered_set<std::string>> subscriptions_;
    std::unordered_set<WebSocketConnectionPtr> authenticated_;
    mutable std::mutex mutex_;
};

} // namespace broker_sim::polygon
```

### 10.3 Backpressure Handling

WebSocket clients may fall behind during high-throughput periods. The simulator implements backpressure handling to prevent unbounded memory growth:

```cpp
// src/core/bounded_queue.hpp

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace broker_sim {

/**
 * Thread-safe bounded queue with backpressure policy.
 */
template<typename T>
class BoundedQueue {
public:
    enum class OverflowPolicy {
        BLOCK,      // Block producer until space available
        DROP_OLDEST, // Drop oldest message to make room
        DROP_NEWEST  // Reject new message if full
    };

    explicit BoundedQueue(size_t max_size,
                          OverflowPolicy policy = OverflowPolicy::DROP_OLDEST)
        : max_size_(max_size), policy_(policy) {}

    /**
     * Push item to queue. Behavior on full queue depends on policy.
     *
     * @return true if item was queued, false if dropped
     */
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (queue_.size() >= max_size_) {
            switch (policy_) {
                case OverflowPolicy::BLOCK:
                    not_full_.wait(lock, [this] {
                        return queue_.size() < max_size_;
                    });
                    break;

                case OverflowPolicy::DROP_OLDEST:
                    queue_.pop();
                    dropped_count_++;
                    break;

                case OverflowPolicy::DROP_NEWEST:
                    dropped_count_++;
                    return false;
            }
        }

        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    /**
     * Pop item from queue, blocking if empty.
     */
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });

        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    /**
     * Try to pop without blocking.
     */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    uint64_t dropped_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_count_;
    }

private:
    std::queue<T> queue_;
    size_t max_size_;
    OverflowPolicy policy_;
    uint64_t dropped_count_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

} // namespace broker_sim
```

**Backpressure Configuration:**

| Scenario | Queue Size | Policy | Notes |
|----------|-----------|--------|-------|
| **Low latency** | 100 | DROP_OLDEST | Discard stale data, keep current |
| **Guaranteed delivery** | 10000 | BLOCK | Slow producer if client lags |
| **Best effort** | 1000 | DROP_NEWEST | Reject new if overwhelmed |

**Batch Sends:** During market open/close bursts, messages are batched into arrays (e.g., 10-50 messages per WebSocket frame) to reduce syscall overhead.

### 10.4 Streaming Fallback for Polled APIs

Some data sources (e.g., Finnhub news, fundamentals) are polled REST endpoints rather than WebSocket streams. The simulator presents these as streaming by:

1. **Pre-loading Event Timeline:** During session creation, query ClickHouse for all relevant events (news, filings, earnings) within the date range and insert them into the unified event queue at their original timestamps.

2. **Unified Event Stream:** All events—market data (trades/quotes), order updates, and polled data—flow through a single chronologically-ordered stream. This ensures:
   - No look-ahead: news at 10:00 AM is emitted at 10:00 AM sim time
   - Consistent ordering: strategies receive all data in correct sequence

3. **WS Delivery:** Polled data is pushed to clients over the same WebSocket connection as streaming data, using appropriate message types (e.g., `{type:"news", data:{...}}`).

```cpp
// Example: Loading news events into session timeline
void DataSource::load_news_events(
    Session& session,
    const std::vector<std::string>& symbols,
    uint64_t start_ns,
    uint64_t end_ns
) {
    auto news_items = query_news_range(symbols, start_ns, end_ns);
    for (const auto& news : news_items) {
        session.event_queue().push(Event{
            .timestamp_ns = news.published_at_ns,
            .sequence = sequence_++,
            .type = EventType::News,
            .symbol = news.symbol,
            .payload = serialize_news(news)
        });
    }
}
```

**Supported Polled Data Types:**
| Source | Data Type | ClickHouse Table | WS Message Type |
|--------|-----------|------------------|-----------------|
| Finnhub | Company News | `company_news` | `news` |
| Finnhub | Earnings Calendar | `earnings_calendar` | `earnings` |
| Finnhub | SEC Filings | `sec_filings` | `filing` |
| Polygon | Corporate Actions | `corporate_actions` | `corp_action` |

---

## 11. Session Management

### 11.1 Session Isolation

Each backtest session runs independently with its own:

- Account state (cash, positions, orders)
- Time clock
- Event queue
- WebSocket connections

```cpp
// src/core/session_manager.hpp

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <optional>
#include <chrono>
#include <mutex>
#include <functional>
#include <uuid/uuid.h>

#include "time_engine.hpp"
#include "event_queue.hpp"
#include "data_source.hpp"
#include "../alpaca/services/matching_engine.hpp"

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

/**
 * Configuration for a backtest session.
 */
struct SessionConfig {
    std::vector<std::string> symbols;
    Timestamp start_time;
    Timestamp end_time;
    double initial_capital = 100000.0;
    double speed_factor = 0.0;  // 0 = max speed
    std::vector<std::string> data_sources = {"trades", "quotes"};
};

/**
 * Session status enumeration.
 */
enum class SessionStatus {
    CREATED,
    RUNNING,
    PAUSED,
    STOPPED,
    COMPLETED,
    ERROR
};

inline std::string to_string(SessionStatus status) {
    switch (status) {
        case SessionStatus::CREATED: return "created";
        case SessionStatus::RUNNING: return "running";
        case SessionStatus::PAUSED: return "paused";
        case SessionStatus::STOPPED: return "stopped";
        case SessionStatus::COMPLETED: return "completed";
        case SessionStatus::ERROR: return "error";
    }
    return "unknown";
}

/**
 * Represents an isolated backtest session.
 *
 * Each session has its own:
 * - Time engine for clock control
 * - Event queue for chronological processing
 * - Matching engine for order execution
 * - Account state
 */
/**
 * Real-time session statistics for monitoring.
 */
struct SessionStats {
    std::atomic<uint64_t> events_processed{0};
    std::atomic<uint64_t> trades_processed{0};
    std::atomic<uint64_t> quotes_processed{0};
    std::atomic<uint64_t> orders_submitted{0};
    std::atomic<uint64_t> fills_executed{0};
    std::atomic<uint64_t> queue_depth{0};
    std::atomic<double> events_per_second{0.0};
    std::atomic<int64_t> lag_microseconds{0};  // Real time - sim time delta

    Timestamp last_update_time;
    uint64_t last_event_count{0};

    /**
     * Update events/sec calculation (call periodically).
     */
    void update_rate() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_update_time).count();
        if (elapsed > 0) {
            uint64_t current = events_processed.load();
            events_per_second.store((current - last_event_count) / elapsed);
            last_event_count = current;
            last_update_time = now;
        }
    }

    /**
     * Get stats as JSON for API response.
     */
    nlohmann::json to_json() const {
        return {
            {"events_processed", events_processed.load()},
            {"trades_processed", trades_processed.load()},
            {"quotes_processed", quotes_processed.load()},
            {"orders_submitted", orders_submitted.load()},
            {"fills_executed", fills_executed.load()},
            {"queue_depth", queue_depth.load()},
            {"events_per_second", events_per_second.load()},
            {"lag_microseconds", lag_microseconds.load()}
        };
    }
};

struct Session {
    std::string id;
    SessionConfig config;
    std::shared_ptr<TimeEngine> time_engine;
    std::shared_ptr<EventQueue> event_queue;
    std::shared_ptr<alpaca::MatchingEngine> matching_engine;

    SessionStatus status = SessionStatus::CREATED;
    Timestamp created_at;
    std::optional<Timestamp> started_at;
    std::optional<Timestamp> completed_at;

    // Account state
    double cash;
    double equity;
    std::unordered_map<std::string, double> positions;  // symbol -> qty

    // Session statistics
    SessionStats stats;

    // Background thread for event processing
    std::unique_ptr<std::thread> worker_thread;
    std::atomic<bool> should_stop{false};

    Session(const std::string& session_id, const SessionConfig& cfg)
        : id(session_id)
        , config(cfg)
        , time_engine(std::make_shared<TimeEngine>())
        , event_queue(std::make_shared<EventQueue>())
        , matching_engine(std::make_shared<alpaca::MatchingEngine>())
        , created_at(std::chrono::system_clock::now())
        , cash(cfg.initial_capital)
        , equity(cfg.initial_capital) {

        time_engine->set_time(cfg.start_time);
        time_engine->set_speed(cfg.speed_factor);
    }

    ~Session() {
        stop();
    }

    void stop() {
        should_stop.store(true);
        time_engine->stop();
        if (worker_thread && worker_thread->joinable()) {
            worker_thread->join();
        }
    }
};

/**
 * Manages multiple concurrent backtest sessions.
 *
 * Each session runs in its own thread for true parallel execution.
 * Thread-safe: All public methods are protected by mutex.
 */
class SessionManager {
public:
    explicit SessionManager(std::shared_ptr<ClickHouseDataSource> data_source)
        : data_source_(std::move(data_source)) {}

    ~SessionManager() {
        // Stop all sessions on destruction
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, session] : sessions_) {
            session->stop();
        }
    }

    /**
     * Create a new backtest session.
     *
     * @param config Session configuration
     * @return Pointer to created session
     */
    std::shared_ptr<Session> create_session(const SessionConfig& config) {
        std::string session_id = generate_uuid();

        auto session = std::make_shared<Session>(session_id, config);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_[session_id] = session;
        }

        return session;
    }

    /**
     * Get session by ID.
     */
    std::shared_ptr<Session> get_session(const std::string& session_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        return it != sessions_.end() ? it->second : nullptr;
    }

    /**
     * List all sessions.
     */
    std::vector<std::shared_ptr<Session>> list_sessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<Session>> result;
        result.reserve(sessions_.size());
        for (const auto& [id, session] : sessions_) {
            result.push_back(session);
        }
        return result;
    }

    /**
     * Start running a session.
     *
     * Spawns a background thread to process events.
     */
    void start_session(const std::string& session_id) {
        auto session = get_session(session_id);
        if (!session) {
            throw std::runtime_error("Session not found: " + session_id);
        }

        session->status = SessionStatus::RUNNING;
        session->started_at = std::chrono::system_clock::now();
        session->time_engine->start();
        session->should_stop.store(false);

        // Start background thread
        session->worker_thread = std::make_unique<std::thread>(
            [this, session]() {
                run_session_loop(session);
            }
        );
    }

    /**
     * Pause a running session.
     */
    void pause_session(const std::string& session_id) {
        auto session = get_session(session_id);
        if (session) {
            session->time_engine->pause();
            session->status = SessionStatus::PAUSED;
        }
    }

    /**
     * Resume a paused session.
     */
    void resume_session(const std::string& session_id) {
        auto session = get_session(session_id);
        if (session) {
            session->time_engine->resume();
            session->status = SessionStatus::RUNNING;
        }
    }

    /**
     * Stop a session.
     */
    void stop_session(const std::string& session_id) {
        auto session = get_session(session_id);
        if (session) {
            session->stop();
            session->status = SessionStatus::STOPPED;
        }
    }

    /**
     * Remove a session.
     */
    void destroy_session(const std::string& session_id) {
        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(session_id);
            if (it != sessions_.end()) {
                session = it->second;
                sessions_.erase(it);
            }
        }

        if (session) {
            session->stop();
        }
    }

    /**
     * Register callback for event processing.
     */
    using EventCallback = std::function<void(const std::string& session_id, const Event&)>;
    void set_event_callback(EventCallback callback) {
        event_callback_ = std::move(callback);
    }

private:
    /**
     * Main event loop for a session.
     */
    void run_session_loop(std::shared_ptr<Session> session) {
        try {
            // Load events from data source into queue
            data_source_->stream_events(
                session->config.symbols,
                session->config.start_time,
                session->config.end_time,
                *session->event_queue
            );

            // Process events in chronological order
            while (!session->should_stop.load()) {
                auto event_opt = session->event_queue->pop();
                if (!event_opt) {
                    // No more events
                    break;
                }

                const Event& event = *event_opt;

                // Wait for simulated time
                if (!session->time_engine->wait_for_next_event(event.timestamp)) {
                    // Stopped
                    break;
                }

                // Process the event
                process_event(session, event);

                // Notify callback if registered
                if (event_callback_) {
                    event_callback_(session->id, event);
                }
            }

            if (!session->should_stop.load()) {
                session->status = SessionStatus::COMPLETED;
                session->completed_at = std::chrono::system_clock::now();
            }

        } catch (const std::exception& e) {
            session->status = SessionStatus::ERROR;
            LOG_ERROR << "Session " << session->id << " error: " << e.what();
        }
    }

    /**
     * Process a single event.
     */
    void process_event(std::shared_ptr<Session> session, const Event& event) {
        if (event.event_type == EventType::QUOTE) {
            // Update NBBO and check for fills
            const auto& quote_data = std::get<QuoteData>(event.data);
            alpaca::NBBO nbbo{
                .symbol = event.symbol,
                .bid_price = quote_data.bid_price,
                .bid_size = quote_data.bid_size,
                .ask_price = quote_data.ask_price,
                .ask_size = quote_data.ask_size,
                .timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    event.timestamp.time_since_epoch()).count()
            };

            auto fills = session->matching_engine->update_nbbo(nbbo);

            // Process fills (update positions, broadcast to clients)
            for (const auto& fill : fills) {
                process_fill(session, fill);
            }
        }
        // Handle other event types as needed
    }

    /**
     * Process an order fill.
     */
    void process_fill(std::shared_ptr<Session> session, const alpaca::Fill& fill) {
        // Update position
        // Update cash
        // Broadcast fill event
    }

    /**
     * Generate UUID for session ID.
     */
    static std::string generate_uuid() {
        uuid_t uuid;
        uuid_generate_random(uuid);
        char uuid_str[37];
        uuid_unparse_lower(uuid, uuid_str);
        return std::string(uuid_str);
    }

    std::shared_ptr<ClickHouseDataSource> data_source_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    mutable std::mutex mutex_;
    EventCallback event_callback_;
};

} // namespace broker_sim
```

---

## 12. Database Integration

### 12.1 ClickHouse Schema Reference

The simulator reads from these existing tables:

```sql
-- Tick-level trades (nanosecond precision)
CREATE TABLE stock_trades (
    timestamp DateTime64(9),  -- nanoseconds
    symbol LowCardinality(String),
    price Decimal64(4),
    size UInt32,
    exchange LowCardinality(String),
    conditions Array(String),
    tape LowCardinality(String),
    trade_id String,
    sequence_number UInt64
) ENGINE = ReplacingMergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (symbol, timestamp, sequence_number);

-- NBBO quotes (nanosecond precision)
CREATE TABLE stock_quotes (
    timestamp DateTime64(9),
    symbol LowCardinality(String),
    bid_price Decimal64(4),
    bid_size UInt32,
    ask_price Decimal64(4),
    ask_size UInt32,
    bid_exchange LowCardinality(String),
    ask_exchange LowCardinality(String),
    tape LowCardinality(String),
    sequence_number UInt64
) ENGINE = ReplacingMergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (symbol, timestamp, sequence_number);

-- Various bar tables (minute, second, hour, daily)
CREATE TABLE stock_minute_bars (
    timestamp DateTime,
    symbol LowCardinality(String),
    open Decimal64(4),
    high Decimal64(4),
    low Decimal64(4),
    close Decimal64(4),
    volume UInt64,
    vwap Decimal64(4),
    trade_count UInt32
) ENGINE = ReplacingMergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (symbol, timestamp);
```

### 12.2 Query Optimization

```sql
-- Efficient query for streaming trades
SELECT
    timestamp,
    symbol,
    price,
    size,
    exchange,
    conditions,
    tape
FROM stock_trades
WHERE symbol IN ('AAPL', 'MSFT', 'GOOGL')
  AND timestamp >= '2025-01-15 09:30:00'
  AND timestamp < '2025-01-15 16:00:00'
ORDER BY timestamp ASC
SETTINGS max_block_size = 10000;

-- Use PREWHERE for partition pruning
SELECT *
FROM stock_trades
PREWHERE timestamp >= '2025-01-15'
WHERE symbol = 'AAPL'
ORDER BY timestamp ASC;
```

---

## 13. Configuration & Deployment

### 13.1 Configuration File (JSON)

```json
// config/settings.json
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
    "bind_address": "127.0.0.1"
  },
  "defaults": {
    "initial_capital": 100000.0,
    "speed_factor": 0.0,
    "max_sessions": 20
  },
  "execution": {
    "enable_latency": false,
    "fixed_latency_us": 0,
    "enable_slippage": false,
    "fixed_slippage_bps": 0.0
  },
  "websocket": {
    "queue_size": 1000,
    "overflow_policy": "drop_oldest",
    "batch_size": 50
  },
  "logging": {
    "level": "info",
    "format": "json",
    "file": "/var/log/broker_simulator/simulator.log"
  }
}
```

### 13.2 C++ Configuration Loader

```cpp
// src/core/config.hpp

#pragma once

#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

namespace broker_sim {

using json = nlohmann::json;

struct DatabaseConfig {
    std::string host = "localhost";
    uint16_t port = 9000;
    std::string database = "polygon";
    std::string user = "default";
    std::string password = "";
};

struct ServiceConfig {
    uint16_t control_port = 8000;
    uint16_t alpaca_port = 8100;
    uint16_t polygon_port = 8200;
    uint16_t finnhub_port = 8300;
    std::string bind_address = "127.0.0.1";
};

struct Config {
    DatabaseConfig database;
    ServiceConfig services;
    double default_initial_capital = 100000.0;
    double default_speed_factor = 0.0;
    int max_sessions = 20;
    std::string log_level = "info";

    static Config load(const std::string& path) {
        std::ifstream f(path);
        json j = json::parse(f);

        Config cfg;
        if (j.contains("database")) {
            auto& db = j["database"];
            cfg.database.host = db.value("host", "localhost");
            cfg.database.port = db.value("port", 9000);
            cfg.database.database = db.value("database", "polygon");
            cfg.database.user = db.value("user", "default");
            cfg.database.password = db.value("password", "");
        }
        if (j.contains("services")) {
            auto& svc = j["services"];
            cfg.services.control_port = svc.value("control_port", 8000);
            cfg.services.alpaca_port = svc.value("alpaca_port", 8100);
            cfg.services.polygon_port = svc.value("polygon_port", 8200);
            cfg.services.finnhub_port = svc.value("finnhub_port", 8300);
            cfg.services.bind_address = svc.value("bind_address", "127.0.0.1");
        }
        if (j.contains("defaults")) {
            cfg.default_initial_capital = j["defaults"].value("initial_capital", 100000.0);
            cfg.default_speed_factor = j["defaults"].value("speed_factor", 0.0);
            cfg.max_sessions = j["defaults"].value("max_sessions", 20);
        }
        return cfg;
    }
};

} // namespace broker_sim
```

### 13.3 Build & Deploy (CMake)

```bash
#!/bin/bash
# scripts/build.sh

set -e

BUILD_TYPE=${1:-Release}
BUILD_DIR="build"

echo "Building broker_simulator ($BUILD_TYPE)..."

mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE
cmake --build . -j$(nproc)

echo "Build complete: $BUILD_DIR/broker_simulator"
```

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.20)
project(broker_simulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(Drogon REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)

# Main executable
add_executable(broker_simulator
    src/main.cpp
    src/core/time_engine.cpp
    src/core/session_manager.cpp
    src/core/data_source.cpp
    src/alpaca/controllers.cpp
    src/alpaca/matching_engine.cpp
    src/polygon/controllers.cpp
    src/finnhub/controllers.cpp
)

target_link_libraries(broker_simulator PRIVATE
    Drogon::Drogon
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    clickhouse-cpp-lib
    uuid
)

# Install
install(TARGETS broker_simulator DESTINATION bin)
install(FILES config/settings.json DESTINATION etc/broker_simulator)
```

### 13.4 Startup Script

```bash
#!/bin/bash
# scripts/start_simulator.sh

set -e

INSTALL_DIR="/home/elan/projects/broker_simulator"
CONFIG_FILE="${INSTALL_DIR}/config/settings.json"
LOG_DIR="/var/log/broker_simulator"

mkdir -p $LOG_DIR

echo "Starting Broker Simulator..."
exec ${INSTALL_DIR}/build/broker_simulator --config $CONFIG_FILE
```

### 13.5 systemd Service

```ini
# /etc/systemd/system/broker-simulator.service

[Unit]
Description=Broker Simulation Service (C++)
After=network.target clickhouse-server.service

[Service]
Type=simple
User=elan
WorkingDirectory=/home/elan/projects/broker_simulator
ExecStart=/home/elan/projects/broker_simulator/build/broker_simulator --config /home/elan/projects/broker_simulator/config/settings.json
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
Environment=MALLOC_ARENA_MAX=2

[Install]
WantedBy=multi-user.target
```

```bash
# Installation commands
sudo cp broker-simulator.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable broker-simulator
sudo systemctl start broker-simulator
sudo systemctl status broker-simulator
```

---

## 14. Testing Strategy

### 14.1 Unit Tests

```cpp
// tests/test_core/test_matching_engine.cpp

#include <gtest/gtest.h>
#include "matching_engine.hpp"
#include "models/order.hpp"

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;

    void SetUp() override {
        engine = MatchingEngine();
    }
};

TEST_F(MatchingEngineTest, MarketBuyFillsAtAsk) {
    NBBO nbbo{
        .symbol = "AAPL",
        .bid_price = 150.00,
        .bid_size = 100,
        .ask_price = 150.05,
        .ask_size = 200,
        .timestamp_ns = 1705312200000000000
    };

    Order order{
        .id = "test-1",
        .symbol = "AAPL",
        .qty = 100,
        .side = OrderSide::Buy,
        .type = OrderType::Market
    };

    engine.update_nbbo(nbbo);
    auto fill = engine.submit_order(order);

    ASSERT_TRUE(fill.has_value());
    EXPECT_DOUBLE_EQ(fill->fill_price, 150.05);  // Ask price
    EXPECT_EQ(fill->fill_qty, 100);
}

TEST_F(MatchingEngineTest, LimitOrderQueuedWhenNotMarketable) {
    NBBO nbbo{
        .symbol = "AAPL",
        .bid_price = 150.00,
        .bid_size = 100,
        .ask_price = 150.05,
        .ask_size = 200,
        .timestamp_ns = 1705312200000000000
    };

    Order order{
        .id = "test-2",
        .symbol = "AAPL",
        .qty = 100,
        .side = OrderSide::Buy,
        .type = OrderType::Limit,
        .limit_price = 149.50  // Below ask
    };

    engine.update_nbbo(nbbo);
    auto fill = engine.submit_order(order);

    ASSERT_FALSE(fill.has_value());  // Order queued, not filled
    EXPECT_EQ(engine.pending_order_count(), 1);
}

TEST_F(MatchingEngineTest, PartialFillByNBBOSize) {
    NBBO nbbo{
        .symbol = "AAPL",
        .bid_price = 150.00,
        .bid_size = 50,  // Only 50 available
        .ask_price = 150.05,
        .ask_size = 50,
        .timestamp_ns = 1705312200000000000
    };

    Order order{
        .id = "test-3",
        .symbol = "AAPL",
        .qty = 100,  // Want 100, only 50 available
        .side = OrderSide::Buy,
        .type = OrderType::Market
    };

    engine.update_nbbo(nbbo);
    auto fill = engine.submit_order(order);

    ASSERT_TRUE(fill.has_value());
    EXPECT_EQ(fill->fill_qty, 50);  // Partial fill
    EXPECT_EQ(fill->remaining_qty, 50);  // 50 still pending
}
```

### 14.2 Integration Tests

```cpp
// tests/test_integration/test_backtest_flow.cpp

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "session_manager.hpp"
#include "data_source.hpp"

class BacktestFlowTest : public ::testing::Test {
protected:
    std::unique_ptr<SessionManager> session_manager;
    std::unique_ptr<DataSource> data_source;

    void SetUp() override {
        data_source = std::make_unique<ClickHouseDataSource>(test_config());
        session_manager = std::make_unique<SessionManager>(data_source.get());
    }
};

TEST_F(BacktestFlowTest, FullBacktestFlowNoTrades) {
    // Test complete backtest from start to finish
    SessionConfig config{
        .symbols = {"AAPL"},
        .start_time = parse_iso8601("2025-01-15T09:30:00Z"),
        .end_time = parse_iso8601("2025-01-15T10:00:00Z"),
        .initial_capital = 100000.0,
        .speed_factor = 0  // Max speed
    };

    auto session = session_manager->create_session(config);
    EXPECT_EQ(session->status(), SessionStatus::Created);

    session_manager->start_session(session->id());

    // Wait for completion (with timeout)
    auto timeout = std::chrono::seconds(30);
    auto start = std::chrono::steady_clock::now();
    while (session->status() == SessionStatus::Running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_LT(std::chrono::steady_clock::now() - start, timeout);
    }

    EXPECT_EQ(session->status(), SessionStatus::Completed);
    EXPECT_DOUBLE_EQ(session->account().cash, 100000.0);  // No trades
}

TEST_F(BacktestFlowTest, SessionPauseResume) {
    SessionConfig config{
        .symbols = {"AAPL"},
        .start_time = parse_iso8601("2025-01-15T09:30:00Z"),
        .end_time = parse_iso8601("2025-01-15T16:00:00Z"),
        .initial_capital = 100000.0,
        .speed_factor = 1.0  // Real-time speed
    };

    auto session = session_manager->create_session(config);
    session_manager->start_session(session->id());
    EXPECT_EQ(session->status(), SessionStatus::Running);

    // Pause
    session_manager->pause_session(session->id());
    EXPECT_EQ(session->status(), SessionStatus::Paused);
    auto paused_time = session->current_sim_time();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(session->current_sim_time(), paused_time);  // Time frozen

    // Resume
    session_manager->resume_session(session->id());
    EXPECT_EQ(session->status(), SessionStatus::Running);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_GT(session->current_sim_time(), paused_time);  // Time advancing
}
```

### 14.3 Validation Tests

```cpp
// tests/test_validation/test_no_lookahead.cpp

#include <gtest/gtest.h>
#include "session_manager.hpp"
#include "test_helpers.hpp"

class NoLookaheadTest : public ::testing::Test {
protected:
    std::unique_ptr<SessionManager> session_manager;
    std::unique_ptr<DataSource> data_source;

    void SetUp() override {
        data_source = std::make_unique<ClickHouseDataSource>(test_config());
        session_manager = std::make_unique<SessionManager>(data_source.get());
    }
};

TEST_F(NoLookaheadTest, OrderCannotSeeFutureData) {
    // Verify orders placed at time T only see data up to T
    SessionConfig config{
        .symbols = {"AAPL"},
        .start_time = parse_iso8601("2025-01-15T09:30:00Z"),
        .end_time = parse_iso8601("2025-01-15T10:00:00Z"),
        .initial_capital = 100000.0,
        .speed_factor = 0
    };

    auto session = session_manager->create_session(config);
    session_manager->start_session(session->id());

    // Wait until session reaches a known point
    wait_until_sim_time(session.get(), parse_iso8601("2025-01-15T09:35:00Z"));

    // Place order at current sim time
    auto order = session->place_order({
        .symbol = "AAPL",
        .side = OrderSide::Buy,
        .type = OrderType::Market,
        .qty = 100
    });

    // Get the fill
    auto fill = wait_for_fill(session.get(), order.id);

    // Verify fill timestamp is >= order submission time
    EXPECT_GE(fill.timestamp_ns, order.submitted_at_ns);

    // Verify fill price matches historical NBBO at fill time, not future
    auto expected_ask = get_historical_ask(
        data_source.get(), "AAPL", fill.timestamp_ns
    );
    EXPECT_DOUBLE_EQ(fill.price, expected_ask);
}

TEST_F(NoLookaheadTest, EventOrderingMarketDataBeforeOrders) {
    // Verify market data events are processed before order events at same timestamp
    SessionConfig config{
        .symbols = {"AAPL"},
        .start_time = parse_iso8601("2025-01-15T09:30:00Z"),
        .end_time = parse_iso8601("2025-01-15T09:31:00Z"),
        .initial_capital = 100000.0,
        .speed_factor = 0
    };

    auto session = session_manager->create_session(config);

    std::vector<uint64_t> event_timestamps;
    std::vector<EventType> event_types;

    session->on_event([&](const Event& e) {
        event_timestamps.push_back(e.timestamp_ns);
        event_types.push_back(e.type);
    });

    session_manager->start_session(session->id());
    wait_for_completion(session.get());

    // Verify: at any timestamp, QUOTE/TRADE events come before ORDER events
    for (size_t i = 1; i < event_timestamps.size(); ++i) {
        if (event_timestamps[i] == event_timestamps[i-1]) {
            bool prev_is_market = (event_types[i-1] == EventType::Quote ||
                                   event_types[i-1] == EventType::Trade);
            bool curr_is_order = (event_types[i] == EventType::OrderUpdate);

            // Market data should come before order events at same timestamp
            EXPECT_TRUE(!curr_is_order || !prev_is_market ||
                       event_types[i-1] != EventType::OrderUpdate);
        }
    }
}
```

---

## 15. Performance Considerations

### 15.1 Bottlenecks

| Component | Potential Bottleneck | Mitigation |
|-----------|---------------------|------------|
| ClickHouse queries | Large result sets | Streaming cursors, batch processing |
| Event processing | Thread contention | Lock-free queues, per-session threads |
| WebSocket broadcast | Many clients | Connection pooling, message batching |
| Memory | Loading tick data | Streaming, not loading all at once |

### 15.2 Optimization Strategies

1. **Streaming Queries**: Use `execute_iter()` instead of `execute()` to stream results
2. **Batch Processing**: Process events in batches of 1000-10000
3. **Connection Pooling**: Reuse database connections
4. **Message Batching**: Batch WebSocket messages during high-volume periods
5. **Process Isolation**: Run each session in separate process for CPU parallelism

### 15.3 Benchmarks

Target performance metrics:

| Metric | Target | Notes |
|--------|--------|-------|
| Events/second | 100,000+ | Per session |
| Latency (event to client) | < 1ms | In-process |
| Concurrent sessions | 10+ | CPU-bound limit |
| Memory per session | < 500MB | Streaming data |
| Startup time | < 5 seconds | Service initialization |

---

## 16. Security Considerations

### 16.1 Authentication

For simulation purposes, authentication is **simulated but not enforced**:

- Accept any API key/secret for Alpaca
- Accept any API key for Polygon
- Accept any token for Finnhub

This maintains API compatibility without real credential management.

### 16.2 Network Security

- Services bind to localhost by default
- Optional TLS via nginx reverse proxy
- No sensitive data transmitted (all simulation)

### 16.3 Data Protection

- Historical market data is read-only
- Session state is ephemeral (in-memory)
- No persistent storage of trading activity

---

## 17. Future Enhancements

### 17.1 Planned for V1.1

| Feature | Description | Priority |
|---------|-------------|----------|
| **Commissions & Fees** | Per-order commission, SEC/TAF fees, ECN rebates | High |
| **Corporate Actions** | Handle splits, dividends on open positions | High |
| **Checkpoint/Resume** | Save/restore session state for long backtests | Medium |
| **Shared Stream Fan-out** | Multiple sessions share data stream for overlapping symbols | Medium |

### 17.2 Phase 2 Enhancements

| Feature | Description | Priority |
|---------|-------------|----------|
| **Options Support** | Extend to options trading simulation | High |
| **Crypto Markets** | 24/7 crypto trading simulation | Medium |
| **Multi-Asset** | Unified session across asset classes | Medium |
| **Extended Hours** | Pre-market (4AM) and after-hours (8PM) simulation | Low |

### 17.3 Phase 3 Enhancements

| Feature | Description | Priority |
|---------|-------------|----------|
| **Market Impact Model** | Simulate price impact for large orders based on volume | High |
| **Network Latency Simulation** | Configurable network/exchange latency modeling | Medium |
| **Performance Analytics** | Built-in Sharpe, drawdown, P&L analytics | Medium |
| **Order Book Simulation** | Full L2/L3 depth simulation (when data available) | Low |

### 17.4 Commissions & Fees Configuration (Planned)

```cpp
// src/core/fee_config.hpp

#pragma once

namespace broker_sim {

/**
 * Commission and fee configuration for realistic cost modeling.
 */
struct FeeConfig {
    // Per-share commission (most brokers: $0 for retail)
    double per_share_commission = 0.0;

    // Flat per-order commission
    double per_order_commission = 0.0;

    // SEC fee (currently ~$27.80 per million for sales)
    double sec_fee_per_million = 27.80;

    // TAF fee (currently ~$0.000166 per share for sales)
    double taf_fee_per_share = 0.000166;

    // FINRA trading activity fee (capped at $8.30)
    double finra_taf_cap = 8.30;

    // Exchange fees/rebates (depends on routing)
    double maker_rebate_per_share = 0.0;   // Negative = rebate
    double taker_fee_per_share = 0.0;

    /**
     * Calculate total fees for an order.
     *
     * @param qty Number of shares
     * @param price Execution price
     * @param is_sell True for sell orders (SEC fee applies)
     * @param is_maker True if order added liquidity
     * @return Total fees (positive = cost, negative = rebate)
     */
    double calculate_fees(double qty, double price, bool is_sell, bool is_maker) const {
        double fees = per_order_commission + (qty * per_share_commission);

        if (is_sell) {
            double notional = qty * price;
            fees += notional * sec_fee_per_million / 1000000.0;
            fees += std::min(qty * taf_fee_per_share, finra_taf_cap);
        }

        if (is_maker) {
            fees += qty * maker_rebate_per_share;  // Usually negative (rebate)
        } else {
            fees += qty * taker_fee_per_share;
        }

        return fees;
    }
};

/**
 * Common broker fee presets.
 */
namespace FeePresets {
    // Zero commission (Robinhood, Alpaca, etc.)
    inline FeeConfig zero_commission() {
        return FeeConfig{};
    }

    // Interactive Brokers (Pro pricing)
    inline FeeConfig ibkr_pro() {
        FeeConfig cfg;
        cfg.per_share_commission = 0.005;  // $0.005/share, min $1
        cfg.taker_fee_per_share = 0.003;
        cfg.maker_rebate_per_share = -0.002;
        return cfg;
    }

    // Realistic retail (includes SEC/TAF, no commission)
    inline FeeConfig realistic_retail() {
        FeeConfig cfg;
        cfg.sec_fee_per_million = 27.80;
        cfg.taf_fee_per_share = 0.000166;
        return cfg;
    }
}

} // namespace broker_sim
```

---

## Appendix A: API Response Examples

### A.1 Alpaca Account Response

```json
{
  "id": "33ea97f0-3747-4e9d-8d79-425749b39070",
  "account_number": "PA33CKLPDJGG",
  "status": "ACTIVE",
  "crypto_status": "ACTIVE",
  "currency": "USD",
  "buying_power": "49107.44",
  "regt_buying_power": "49107.44",
  "daytrading_buying_power": "0",
  "effective_buying_power": "49107.44",
  "non_marginable_buying_power": "23054",
  "options_buying_power": "24553.72",
  "cash": "23054",
  "accrued_fees": "0",
  "portfolio_value": "26053.44",
  "pattern_day_trader": false,
  "trading_blocked": false,
  "transfers_blocked": false,
  "account_blocked": false,
  "shorting_enabled": true,
  "multiplier": "2",
  "equity": "26053.44",
  "last_equity": "26053.44",
  "long_market_value": "2999.44",
  "short_market_value": "0",
  "position_market_value": "2999.44",
  "initial_margin": "1499.72",
  "maintenance_margin": "996.07",
  "sma": "26042.4",
  "daytrade_count": 0,
  "crypto_tier": 1
}
```

### A.2 Alpaca Position Response

```json
{
  "asset_id": "b0b6dd9d-8b9b-48a9-ba46-b9d54906e415",
  "symbol": "AAPL",
  "exchange": "NASDAQ",
  "asset_class": "us_equity",
  "asset_marginable": true,
  "qty": "2",
  "avg_entry_price": "273.37",
  "side": "long",
  "market_value": "547.34",
  "cost_basis": "546.74",
  "unrealized_pl": "0.6",
  "unrealized_plpc": "0.0010974137615686",
  "unrealized_intraday_pl": "0",
  "unrealized_intraday_plpc": "0",
  "current_price": "273.67",
  "lastday_price": "273.67",
  "change_today": "0",
  "qty_available": "2"
}
```

### A.3 Alpaca Order Response

```json
{
  "id": "52e48b23-1452-4e41-9993-98c0bd7c9aa4",
  "client_order_id": "b03bd54f-f2fd-46c2-8b5b-b88834d3d69b",
  "created_at": "2025-11-14T01:07:05.351042Z",
  "updated_at": "2025-11-14T09:00:04.090527Z",
  "submitted_at": "2025-11-14T09:00:03.954485Z",
  "filled_at": "2025-11-14T09:00:04.08502Z",
  "expired_at": null,
  "canceled_at": null,
  "asset_id": "1e80b378-a5e3-4727-96bf-9c64fdd4e7e2",
  "symbol": "F",
  "asset_class": "us_equity",
  "qty": "1",
  "filled_qty": "1",
  "filled_avg_price": "13.28",
  "order_class": "",
  "order_type": "limit",
  "type": "limit",
  "side": "buy",
  "position_intent": "buy_to_open",
  "time_in_force": "day",
  "limit_price": "14",
  "stop_price": null,
  "status": "filled",
  "extended_hours": true,
  "trail_percent": null,
  "trail_price": null,
  "hwm": null,
  "source": "access_key"
}
```

### A.4 Polygon Aggregates Response

```json
{
  "ticker": "AAPL",
  "adjusted": true,
  "queryCount": 5,
  "resultsCount": 5,
  "status": "OK",
  "results": [
    {
      "v": 1410,
      "vw": 272.0268,
      "o": 272,
      "c": 272.09,
      "h": 272.2,
      "l": 272,
      "t": 1766134800000,
      "n": 68
    }
  ],
  "next_url": "https://api.polygon.io/v2/aggs/..."
}
```

### A.5 Polygon Trades Response

```json
{
  "results": [
    {
      "conditions": [12, 37],
      "exchange": 11,
      "id": "53939",
      "participant_timestamp": 1766192391431726071,
      "price": 273.14,
      "sequence_number": 10091834,
      "sip_timestamp": 1766192391432064645,
      "size": 2,
      "tape": 3
    }
  ],
  "status": "OK",
  "request_id": "12491a171d6b40a337a243acd3f08cfe"
}
```

### A.6 Polygon Quotes Response

```json
{
  "results": [
    {
      "ask_exchange": 12,
      "ask_price": 273.2,
      "ask_size": 500,
      "bid_exchange": 11,
      "bid_price": 273,
      "bid_size": 400,
      "indicators": [604],
      "participant_timestamp": 1766192389637902494,
      "sequence_number": 76891164,
      "sip_timestamp": 1766192389638249910,
      "tape": 3
    }
  ],
  "status": "OK"
}
```

### A.7 Polygon Last Trade Response

```json
{
  "results": {
    "T": "AAPL",
    "c": [12, 37],
    "i": "53939",
    "p": 273.14,
    "q": 10091834,
    "s": 2,
    "t": 1766192391432064645,
    "x": 11,
    "y": 1766192391431726071,
    "z": 3
  },
  "status": "OK"
}
```

### A.8 Polygon Last NBBO Response

```json
{
  "results": {
    "P": 273.2,
    "S": 500,
    "T": "AAPL",
    "X": 12,
    "i": [604],
    "p": 273,
    "q": 76891164,
    "s": 400,
    "t": 1766192389638249910,
    "x": 11,
    "y": 1766192389637902494,
    "z": 3
  },
  "status": "OK"
}
```

### A.9 Finnhub Quote Response

```json
{
  "c": 273.67,
  "d": 1.48,
  "dp": 0.5437,
  "h": 274.6,
  "l": 269.9,
  "o": 272.155,
  "pc": 272.19,
  "t": 1766178000
}
```

### A.10 Finnhub News Response

```json
[
  {
    "category": "top news",
    "datetime": 1766246401,
    "headline": "Article headline...",
    "id": 7563970,
    "image": "https://...",
    "related": "",
    "source": "CNBC",
    "summary": "Article summary...",
    "url": "https://..."
  }
]
```

---

## Appendix B: Exchange Code Mappings

### B.1 Polygon Exchange IDs

| ID | Exchange | MIC |
|----|----------|-----|
| 1 | NYSE | XNYS |
| 2 | NASDAQ | XNAS |
| 3 | NYSE American | XASE |
| 4 | NYSE Arca | ARCX |
| 10 | NASDAQ BX | XBOS |
| 11 | NASDAQ PSX | XPHL |
| 12 | IEX | IEXG |

### B.2 Tape Mappings

| Code | Tape | Securities |
|------|------|------------|
| 1 | A | NYSE listed |
| 2 | B | NYSE Arca / NYSE American |
| 3 | C | NASDAQ listed |

---

## Appendix C: Trade Condition Codes

### C.1 Common Conditions

| Code | Meaning |
|------|---------|
| 0 | Regular Sale |
| 1 | Acquisition |
| 2 | Average Price Trade |
| 7 | Bunched Trade |
| 12 | Intermarket Sweep |
| 14 | Odd Lot Trade |
| 37 | Form T (Extended Hours) |
| 41 | Official Close Price |

---

## Appendix D: Data Source Configuration (Updated 2025-12-22)

### D.1 ClickHouse Database

**Database Name:** `market_data` (NOT `polygon`)

The simulator connects to ClickHouse on localhost:9000 by default. Configure in `config/settings.json`:

```json
{
  "database": {
    "host": "localhost",
    "port": 9000,
    "database": "market_data",
    "user": "default",
    "password": ""
  }
}
```

### D.2 Real Historical Data Requirement

**CRITICAL:** The BrokerSimulator uses ONLY real historical data from ClickHouse.
- No fake/stub data is generated for Polygon or Finnhub APIs
- If data doesn't exist for a symbol/timeframe, the API returns empty results
- The StubDataSource (fallback when ClickHouse is unavailable) returns empty data, not fake data
- This ensures backtesting results are meaningful for live trading decisions

### D.3 Available ClickHouse Tables

#### Polygon Data Tables (stock_*)
| Table | Use | Column Notes |
|-------|-----|--------------|
| stock_trades | Tick trades | timestamp (DateTime64), price (Decimal), size (UInt32) |
| stock_quotes | NBBO quotes | sip_timestamp (DateTime64), bid_price, ask_price, bid_size, ask_size |
| stock_daily_bars | Daily OHLCV | date (Date), open, high, low, close, volume |
| stock_minute_bars | 1-min bars | Same structure as daily |
| stock_second_bars | 1-sec bars | Same structure as daily |
| stock_5m_bars, stock_15m_bars, etc. | Aggregated bars | Various timeframes |
| stock_splits | Stock splits | ticker, execution_date, split_from, split_to |
| stock_dividends | Dividends | From Polygon |
| ticker_details | Company info | ticker, name, description, market_cap, etc. |
| stock_news | News articles | Published articles |

#### Finnhub Data Tables (finnhub_*)
| Table | Use | Notes |
|-------|-----|-------|
| finnhub_company_profiles | Company profiles | symbol, name, industry, market_cap |
| finnhub_company_peers | Peer symbols | symbol, peer |
| finnhub_company_news | Company news | symbol, datetime, headline, summary |
| finnhub_news_sentiment | Sentiment scores | buzz, bullish_percent, bearish_percent |
| finnhub_basic_financials | Key metrics | PE, PB, dividend yield, etc. |
| finnhub_dividends | Dividend history | ex_date, payment_date, amount |
| finnhub_earnings_calendar | Earnings dates | date, eps_estimate, eps_actual |
| finnhub_recommendation_trends | Analyst ratings | strong_buy, buy, hold, sell counts |
| finnhub_price_targets | Price targets | target_high, target_low, target_mean |
| finnhub_upgrades_downgrades | Rating changes | from_grade, to_grade, action |

### D.3.1 ClickHouse LowCardinality Column Handling (Fixed 2025-12-22)

**IMPORTANT:** Many Finnhub tables use `LowCardinality(String)` columns for storage efficiency.
The clickhouse-cpp library's `As<ColumnString>()` returns nullptr for LowCardinality columns,
causing segfaults if accessed directly.

**Solution:** Use `CAST(column AS String)` in SQL queries for all LowCardinality columns:

```sql
-- WRONG: toString() preserves LowCardinality type
SELECT toString(symbol), name FROM finnhub_company_profiles  -- Still returns LowCardinality(String)

-- CORRECT: CAST converts to regular String
SELECT CAST(symbol AS String), name FROM finnhub_company_profiles  -- Returns String
```

**Affected columns by table:**
| Table | LowCardinality Columns |
|-------|------------------------|
| finnhub_company_profiles | symbol, country, currency, estimate_currency |
| finnhub_company_peers | symbol, peer |
| finnhub_company_news | symbol, category |
| finnhub_news_sentiment | symbol |
| finnhub_basic_financials | symbol |
| finnhub_dividends | symbol, currency |
| finnhub_earnings_calendar | symbol, hour |
| finnhub_recommendation_trends | symbol |
| finnhub_price_targets | symbol |
| finnhub_upgrades_downgrades | symbol, from_grade, to_grade, action |

### D.4 Alpaca Account Persistence (PostgreSQL)

Currently, Alpaca account state is stored in-memory per session. For production:

**Planned PostgreSQL Tables:**
```sql
-- Account balances and configuration
CREATE TABLE alpaca_accounts (
    account_id UUID PRIMARY KEY,
    session_id VARCHAR(64),
    cash DECIMAL(18,2),
    buying_power DECIMAL(18,2),
    equity DECIMAL(18,2),
    created_at TIMESTAMP,
    updated_at TIMESTAMP
);

-- Position tracking
CREATE TABLE alpaca_positions (
    id SERIAL PRIMARY KEY,
    account_id UUID REFERENCES alpaca_accounts(account_id),
    symbol VARCHAR(16),
    qty DECIMAL(18,8),
    avg_entry_price DECIMAL(18,6),
    market_value DECIMAL(18,2),
    unrealized_pl DECIMAL(18,2),
    updated_at TIMESTAMP
);

-- Order history
CREATE TABLE alpaca_orders (
    order_id UUID PRIMARY KEY,
    account_id UUID REFERENCES alpaca_accounts(account_id),
    symbol VARCHAR(16),
    side VARCHAR(8),
    type VARCHAR(16),
    qty DECIMAL(18,8),
    limit_price DECIMAL(18,6),
    stop_price DECIMAL(18,6),
    status VARCHAR(32),
    filled_qty DECIMAL(18,8),
    filled_avg_price DECIMAL(18,6),
    created_at TIMESTAMP,
    updated_at TIMESTAMP
);

-- Fill/execution history
CREATE TABLE alpaca_fills (
    fill_id UUID PRIMARY KEY,
    order_id UUID REFERENCES alpaca_orders(order_id),
    qty DECIMAL(18,8),
    price DECIMAL(18,6),
    commission DECIMAL(12,4),
    executed_at TIMESTAMP
);
```

---

*Document End*
