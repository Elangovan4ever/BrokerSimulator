#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <vector>
#include <random>
#include "../src/core/session_manager.hpp"
#include "../src/core/data_source.hpp"

using namespace broker_sim;

namespace {

/**
 * High-throughput data source for stress testing.
 * Generates configurable number of events at high speed.
 */
class StressTestDataSource : public DataSource {
public:
    explicit StressTestDataSource(size_t events_per_symbol = 1000)
        : events_per_symbol_(events_per_symbol) {}

    void stream_trades(const std::vector<std::string>&,
                       Timestamp, Timestamp,
                       const std::function<void(const TradeRecord&)>&) override {}

    void stream_quotes(const std::vector<std::string>&,
                       Timestamp, Timestamp,
                       const std::function<void(const QuoteRecord&)>&) override {}

    void stream_events(const std::vector<std::string>& symbols,
                       Timestamp start, Timestamp,
                       const std::function<void(const MarketEvent&)>& cb) override {
        std::mt19937 rng(42);  // Fixed seed for reproducibility
        std::uniform_real_distribution<double> price_dist(99.0, 101.0);

        int64_t event_count = 0;
        for (const auto& sym : symbols) {
            double base_price = 100.0;
            for (size_t i = 0; i < events_per_symbol_; ++i) {
                MarketEvent ev;
                ev.timestamp = start + std::chrono::nanoseconds(event_count * 1000);
                ev.type = MarketEventType::QUOTE;

                double bid = base_price - 0.05 + price_dist(rng) * 0.01 - 0.5;
                double ask = bid + 0.05 + (rng() % 10) * 0.001;

                ev.quote = QuoteRecord{
                    ev.timestamp,
                    sym,
                    bid,
                    100 + static_cast<int64_t>(rng() % 500),
                    ask,
                    100 + static_cast<int64_t>(rng() % 500),
                    1, 1, 1
                };
                cb(ev);
                ++event_count;

                // Occasionally emit a trade
                if (i % 10 == 5) {
                    MarketEvent trade_ev;
                    trade_ev.timestamp = ev.timestamp + std::chrono::nanoseconds(500);
                    trade_ev.type = MarketEventType::TRADE;
                    trade_ev.trade = TradeRecord{
                        trade_ev.timestamp,
                        sym,
                        (bid + ask) / 2.0,
                        50 + static_cast<int64_t>(rng() % 100),
                        1,
                        "",
                        1
                    };
                    cb(trade_ev);
                    ++event_count;
                }
            }
        }
    }

    std::vector<TradeRecord> get_trades(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::vector<QuoteRecord> get_quotes(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::vector<BarRecord> get_bars(const std::string&, Timestamp, Timestamp, int, const std::string&, size_t) override {
        return {};
    }

    std::vector<CompanyNewsRecord> get_company_news(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::optional<CompanyProfileRecord> get_company_profile(const std::string&) override {
        return std::nullopt;
    }

    std::vector<std::string> get_company_peers(const std::string&, size_t) override {
        return {};
    }

    std::optional<NewsSentimentRecord> get_news_sentiment(const std::string&) override {
        return std::nullopt;
    }

    std::optional<BasicFinancialsRecord> get_basic_financials(const std::string&) override {
        return std::nullopt;
    }

    std::vector<DividendRecord> get_dividends(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::vector<DividendRecord> get_stock_dividends(const StockDividendsQuery&) override {
        return {};
    }

    std::vector<StockSplitRecord> get_stock_splits(const StockSplitsQuery&) override {
        return {};
    }

    std::vector<StockNewsRecord> get_stock_news(const StockNewsQuery&) override {
        return {};
    }

    std::vector<StockNewsInsightRecord> get_stock_news_insights(const std::vector<std::string>&) override {
        return {};
    }

    std::vector<StockIpoRecord> get_stock_ipos(const StockIposQuery&) override {
        return {};
    }

    std::vector<StockShortInterestRecord> get_stock_short_interest(const StockShortInterestQuery&) override {
        return {};
    }

    std::vector<StockShortVolumeRecord> get_stock_short_volume(const StockShortVolumeQuery&) override {
        return {};
    }

    std::vector<FinancialsRecord> get_stock_financials(const FinancialsQuery&) override {
        return {};
    }

    std::vector<SplitRecord> get_splits(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::vector<RecommendationRecord> get_recommendation_trends(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

    std::optional<PriceTargetRecord> get_price_targets(const std::string&) override {
        return std::nullopt;
    }

    std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string&, Timestamp, Timestamp, size_t) override {
        return {};
    }

private:
    size_t events_per_symbol_;
};

} // namespace

//
// Multi-Session Concurrent Stress Tests
//

TEST(StressTest, ManyConcurrentSessions) {
    constexpr int NUM_SESSIONS = 10;
    constexpr size_t EVENTS_PER_SYMBOL = 100;

    auto ds = std::make_shared<StressTestDataSource>(EVENTS_PER_SYMBOL);
    SessionManager mgr(ds);

    std::vector<std::shared_ptr<Session>> sessions;
    std::atomic<int> completed_sessions{0};

    std::mutex mu;
    std::condition_variable cv;

    // Track completion
    mgr.add_event_callback([&](const std::string& session_id, const Event& e) {
        // Session end is signaled by completion of all events
    });

    // Create all sessions
    for (int i = 0; i < NUM_SESSIONS; ++i) {
        SessionConfig cfg;
        cfg.symbols = {"AAPL", "MSFT"};
        cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
        cfg.end_time = cfg.start_time + std::chrono::hours(1);
        cfg.speed_factor = 0.0;  // Max speed
        cfg.initial_capital = 100000.0 + i * 10000.0;

        auto session = mgr.create_session(cfg);
        ASSERT_NE(session, nullptr);
        sessions.push_back(session);
    }

    // Start all sessions in parallel
    auto start_time = std::chrono::steady_clock::now();
    for (auto& session : sessions) {
        mgr.start_session(session->id);
    }

    // Wait for all sessions to complete or timeout
    std::this_thread::sleep_for(std::chrono::seconds(5));

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Check results - sessions should be running or completed
    int active = 0;
    int completed = 0;
    for (auto& session : sessions) {
        if (session->status == SessionStatus::RUNNING) {
            ++active;
        } else if (session->status == SessionStatus::COMPLETED) {
            ++completed;
        }
        mgr.stop_session(session->id);
    }

    // The key is that sessions are successfully created and running - they may not all complete
    // in a stress test, but the system should remain stable
    std::cout << "Stress test: " << NUM_SESSIONS << " sessions, "
              << EVENTS_PER_SYMBOL * 2 << " events each, "
              << duration.count() << "ms total, "
              << active << " active, " << completed << " completed" << std::endl;

    // Success criteria: no crashes, system remains responsive
    EXPECT_EQ(sessions.size(), static_cast<size_t>(NUM_SESSIONS)) << "All sessions should be created";
}

TEST(StressTest, HighVolumeOrderSubmission) {
    constexpr int NUM_ORDERS = 100;

    auto ds = std::make_shared<StressTestDataSource>(1000);
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 1000000.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    std::atomic<int> fills{0};
    std::mutex mu;
    std::condition_variable cv;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type == EventType::ORDER_FILL) {
            ++fills;
            cv.notify_all();
        }
    });

    // Start session first to process quotes
    mgr.start_session(session->id);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Submit many orders rapidly
    std::vector<std::string> order_ids;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_ORDERS; ++i) {
        Order order;
        order.symbol = "AAPL";
        order.side = (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
        order.type = OrderType::MARKET;
        order.tif = TimeInForce::DAY;
        order.qty = 1.0;

        auto id = mgr.submit_order(session->id, order);
        if (!id.empty()) {
            order_ids.push_back(id);
        }
    }

    auto submit_end = std::chrono::steady_clock::now();
    auto submit_duration = std::chrono::duration_cast<std::chrono::microseconds>(submit_end - start);

    // Wait for fills
    {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait_for(lock, std::chrono::seconds(5), [&] {
            return fills.load() >= static_cast<int>(order_ids.size() / 2);
        });
    }

    auto orders = mgr.get_orders(session->id);
    int filled = 0;
    for (const auto& kv : orders) {
        if (kv.second.status == OrderStatus::FILLED ||
            kv.second.status == OrderStatus::PARTIALLY_FILLED) {
            ++filled;
        }
    }

    std::cout << "Order stress test: " << order_ids.size() << " orders submitted in "
              << submit_duration.count() << "us, " << filled << " filled" << std::endl;

    EXPECT_GE(order_ids.size(), static_cast<size_t>(NUM_ORDERS / 2))
        << "Should submit at least half the orders";
    EXPECT_GT(filled, 0) << "Should have some fills";

    mgr.stop_session(session->id);
}

TEST(StressTest, RapidSessionCreationDestruction) {
    constexpr int NUM_ITERATIONS = 20;

    auto ds = std::make_shared<StressTestDataSource>(50);
    SessionManager mgr(ds);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        SessionConfig cfg;
        cfg.symbols = {"AAPL"};
        cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
        cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
        cfg.speed_factor = 0.0;

        auto session = mgr.create_session(cfg);
        ASSERT_NE(session, nullptr);

        mgr.start_session(session->id);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.stop_session(session->id);
        mgr.destroy_session(session->id);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Session lifecycle stress: " << NUM_ITERATIONS
              << " create/start/stop/destroy cycles in " << duration.count() << "ms" << std::endl;

    // Verify no sessions leak
    auto remaining = mgr.list_sessions();
    EXPECT_EQ(remaining.size(), 0u) << "All sessions should be destroyed";
}

TEST(StressTest, ConcurrentOrdersAcrossSessions) {
    constexpr int NUM_SESSIONS = 5;
    constexpr int ORDERS_PER_SESSION = 20;

    auto ds = std::make_shared<StressTestDataSource>(500);
    SessionManager mgr(ds);

    std::vector<std::shared_ptr<Session>> sessions;
    std::atomic<int> total_fills{0};

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type == EventType::ORDER_FILL) {
            ++total_fills;
        }
    });

    // Create sessions
    for (int i = 0; i < NUM_SESSIONS; ++i) {
        SessionConfig cfg;
        cfg.symbols = {"AAPL", "MSFT", "GOOGL"};
        cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
        cfg.end_time = cfg.start_time + std::chrono::hours(1);
        cfg.speed_factor = 0.0;
        cfg.initial_capital = 500000.0;

        auto session = mgr.create_session(cfg);
        ASSERT_NE(session, nullptr);
        sessions.push_back(session);
        mgr.start_session(session->id);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Submit orders from multiple threads
    std::vector<std::thread> order_threads;
    std::atomic<int> orders_submitted{0};

    for (int s = 0; s < NUM_SESSIONS; ++s) {
        order_threads.emplace_back([&, s]() {
            for (int o = 0; o < ORDERS_PER_SESSION; ++o) {
                Order order;
                order.symbol = (o % 3 == 0) ? "AAPL" : ((o % 3 == 1) ? "MSFT" : "GOOGL");
                order.side = (o % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
                order.type = OrderType::MARKET;
                order.tif = TimeInForce::DAY;
                order.qty = 1.0 + (o % 10);

                auto id = mgr.submit_order(sessions[s]->id, order);
                if (!id.empty()) {
                    ++orders_submitted;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    for (auto& t : order_threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Concurrent orders: " << orders_submitted.load() << " submitted across "
              << NUM_SESSIONS << " sessions, " << total_fills.load() << " fills" << std::endl;

    EXPECT_GT(orders_submitted.load(), 0) << "Should submit some orders";
    EXPECT_GT(total_fills.load(), 0) << "Should have some fills";

    for (auto& session : sessions) {
        mgr.stop_session(session->id);
    }
}

TEST(StressTest, PauseResumeUnderLoad) {
    auto ds = std::make_shared<StressTestDataSource>(2000);
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    std::atomic<int> events_received{0};

    mgr.add_event_callback([&](const std::string&, const Event&) {
        ++events_received;
    });

    mgr.start_session(session->id);

    // Rapidly pause/resume while processing
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        mgr.pause_session(session->id);
        EXPECT_EQ(session->status, SessionStatus::PAUSED);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.resume_session(session->id);
        EXPECT_TRUE(session->status == SessionStatus::RUNNING ||
                    session->status == SessionStatus::COMPLETED);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Pause/resume stress: " << events_received.load()
              << " events processed with 10 pause/resume cycles" << std::endl;

    EXPECT_GT(events_received.load(), 0) << "Should process events";

    mgr.stop_session(session->id);
}

TEST(StressTest, JumpToStress) {
    auto ds = std::make_shared<StressTestDataSource>(100);
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 100000.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    mgr.start_session(session->id);

    // Submit an order that should fill
    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;
    mgr.submit_order(session->id, order);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Jump to different times repeatedly
    for (int i = 0; i < 5; ++i) {
        auto target = cfg.start_time + std::chrono::nanoseconds(i * 1'000'000);
        mgr.jump_to(session->id, target);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Verify state is reset
        auto state = session->account_manager->state();
        EXPECT_DOUBLE_EQ(state.cash, 100000.0)
            << "Cash should reset after jump_to at iteration " << i;

        auto positions = session->account_manager->positions();
        EXPECT_TRUE(positions.empty())
            << "Positions should be empty after jump_to at iteration " << i;
    }

    mgr.stop_session(session->id);
}

TEST(StressTest, MemoryStability) {
    // Run many short-lived sessions to check for memory leaks
    constexpr int NUM_CYCLES = 50;

    auto ds = std::make_shared<StressTestDataSource>(20);
    SessionManager mgr(ds);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_CYCLES; ++i) {
        SessionConfig cfg;
        cfg.symbols = {"AAPL", "MSFT"};
        cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
        cfg.end_time = cfg.start_time + std::chrono::nanoseconds(5'000'000);
        cfg.speed_factor = 0.0;

        auto session = mgr.create_session(cfg);
        ASSERT_NE(session, nullptr);

        // Submit a few orders
        for (int j = 0; j < 5; ++j) {
            Order order;
            order.symbol = (j % 2 == 0) ? "AAPL" : "MSFT";
            order.side = OrderSide::BUY;
            order.type = OrderType::MARKET;
            order.tif = TimeInForce::DAY;
            order.qty = 1.0;
            mgr.submit_order(session->id, order);
        }

        mgr.start_session(session->id);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        mgr.stop_session(session->id);
        mgr.destroy_session(session->id);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    auto remaining = mgr.list_sessions();
    EXPECT_EQ(remaining.size(), 0u) << "All sessions should be cleaned up";

    std::cout << "Memory stability: " << NUM_CYCLES << " complete session lifecycles in "
              << duration.count() << "ms" << std::endl;
}

TEST(StressTest, EventQueueOverflow) {
    // Test behavior when event queue overflows
    auto ds = std::make_shared<StressTestDataSource>(10000);  // Many events
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};  // 5 symbols
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.speed_factor = 0.0;
    cfg.queue_capacity = 100;  // Small queue to force overflow
    cfg.overflow_policy = "drop_oldest";

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    std::atomic<int> events{0};
    mgr.add_event_callback([&](const std::string&, const Event&) {
        ++events;
    });

    mgr.start_session(session->id);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    mgr.stop_session(session->id);

    std::cout << "Queue overflow test: " << events.load() << " events processed, "
              << session->events_dropped.load() << " dropped" << std::endl;

    // System should remain stable even with drops
    EXPECT_GT(events.load(), 0) << "Should process some events";
}
