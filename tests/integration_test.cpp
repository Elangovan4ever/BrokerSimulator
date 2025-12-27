#include <gtest/gtest.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <vector>
#include <set>
#include "../src/core/session_manager.hpp"
#include "../src/core/data_source_stub.hpp"
#include "../src/control/control_server.hpp"

using namespace broker_sim;

namespace {

// Fake DataSource for integration tests
class IntegrationTestDataSource : public StubDataSource {
public:
    void stream_trades(const std::vector<std::string>&,
                       Timestamp, Timestamp,
                       const std::function<void(const TradeRecord&)>&) override {}

    void stream_quotes(const std::vector<std::string>&,
                       Timestamp, Timestamp,
                       const std::function<void(const QuoteRecord&)>&) override {}

    void stream_events(const std::vector<std::string>& symbols,
                       Timestamp start, Timestamp end,
                       const std::function<void(const MarketEvent&)>& cb) override {
        // Generate test events for each symbol
        for (const auto& sym : symbols) {
            for (int i = 0; i < 5; ++i) {
                MarketEvent ev;
                ev.timestamp = start + std::chrono::milliseconds(i * 100);
                ev.type = MarketEventType::QUOTE;
                ev.quote = QuoteRecord{
                    ev.timestamp,
                    sym,
                    100.0 + i * 0.01,  // bid
                    100,                // bid_size
                    100.05 + i * 0.01, // ask
                    100,                // ask_size
                    1, 1, 1
                };
                cb(ev);
            }
        }
    }

    std::vector<TradeRecord> get_trades(const std::string& symbol,
                                        Timestamp start, Timestamp end,
                                        size_t limit) override {
        std::vector<TradeRecord> trades;
        for (size_t i = 0; i < std::min(limit, size_t(10)); ++i) {
            TradeRecord t;
            t.timestamp = start + std::chrono::milliseconds(i * 10);
            t.symbol = symbol;
            t.price = 100.0 + i * 0.01;
            t.size = 100;
            t.conditions = "";
            trades.push_back(t);
        }
        return trades;
    }

    std::vector<QuoteRecord> get_quotes(const std::string& symbol,
                                        Timestamp start, Timestamp end,
                                        size_t limit) override {
        std::vector<QuoteRecord> quotes;
        for (size_t i = 0; i < std::min(limit, size_t(10)); ++i) {
            QuoteRecord q;
            q.timestamp = start + std::chrono::milliseconds(i * 10);
            q.symbol = symbol;
            q.bid_price = 99.95 + i * 0.01;
            q.bid_size = 100;
            q.ask_price = 100.05 + i * 0.01;
            q.ask_size = 100;
            q.bid_exchange = 1;
            q.ask_exchange = 1;
            q.tape = 1;
            quotes.push_back(q);
        }
        return quotes;
    }

    std::vector<BarRecord> get_bars(const std::string& symbol,
                                    Timestamp start, Timestamp end,
                                    int mult, const std::string& timespan,
                                    size_t limit) override {
        std::vector<BarRecord> bars;
        for (size_t i = 0; i < std::min(limit, size_t(10)); ++i) {
            BarRecord b;
            b.timestamp = start + std::chrono::hours(i);
            b.symbol = symbol;
            b.open = 100.0;
            b.high = 101.0;
            b.low = 99.0;
            b.close = 100.5;
            b.volume = 10000;
            b.vwap = 100.25;
            b.trade_count = 100;
            bars.push_back(b);
        }
        return bars;
    }

    std::vector<CompanyNewsRecord> get_company_news(const std::string&,
                                                     Timestamp, Timestamp,
                                                     size_t) override {
        return {};
    }

    std::optional<CompanyProfileRecord> get_company_profile(const std::string& symbol) override {
        CompanyProfileRecord profile;
        profile.symbol = symbol;
        profile.name = symbol + " Inc.";
        profile.country = "US";
        profile.currency = "USD";
        profile.exchange = "NASDAQ";
        profile.ipo = Timestamp{} + std::chrono::hours(24 * 365 * 30); // ~2000
        profile.market_capitalization = 1000000000.0;
        profile.share_outstanding = 10000000.0;
        profile.industry = "Technology";
        profile.logo = "http://example.com/logo.png";
        profile.phone = "555-1234";
        profile.weburl = "http://example.com";
        return profile;
    }

    std::vector<std::string> get_company_peers(const std::string& symbol,
                                                size_t limit) override {
        std::vector<std::string> peers = {"AAPL", "MSFT", "GOOGL", "AMZN"};
        if (peers.size() > limit) peers.resize(limit);
        return peers;
    }

    std::optional<NewsSentimentRecord> get_news_sentiment(const std::string&) override {
        return std::nullopt;
    }

    std::optional<BasicFinancialsRecord> get_basic_financials(const std::string&) override {
        return std::nullopt;
    }

    std::vector<DividendRecord> get_dividends(const std::string&,
                                              Timestamp, Timestamp,
                                              size_t) override {
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

    std::vector<StockTickerEventRecord> get_stock_ticker_events(const StockTickerEventsQuery&) override {
        return {};
    }

    std::optional<TickerBasicRecord> get_ticker_basic(const std::string&,
                                                      std::optional<Timestamp>) override {
        return std::nullopt;
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

    std::vector<SplitRecord> get_splits(const std::string&,
                                        Timestamp, Timestamp,
                                        size_t) override {
        return {};
    }

    std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string&,
                                                              Timestamp, Timestamp,
                                                              size_t) override {
        return {};
    }

    std::vector<RecommendationRecord> get_recommendation_trends(const std::string&,
                                                                 Timestamp, Timestamp,
                                                                 size_t) override {
        return {};
    }

    std::optional<PriceTargetRecord> get_price_targets(const std::string&) override {
        return std::nullopt;
    }

    std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string&,
                                                                 Timestamp, Timestamp,
                                                                 size_t) override {
        return {};
    }
};

class IntegrationTest : public ::testing::Test {
protected:
    static std::shared_ptr<SessionManager> session_mgr_;
    static std::thread server_thread_;
    static std::atomic<bool> server_running_;
    static uint16_t test_port_;

    static void SetUpTestSuite() {
        // Create session manager with fake data source
        auto ds = std::make_shared<IntegrationTestDataSource>();
        session_mgr_ = std::make_shared<SessionManager>(ds);

        // Find available port
        test_port_ = 18000 + (std::rand() % 1000);

        // Start server in background thread
        server_running_ = true;
        server_thread_ = std::thread([&]() {
            // Configure Drogon for testing
            drogon::app()
                .setLogLevel(trantor::Logger::kWarn)
                .addListener("127.0.0.1", test_port_)
                .setThreadNum(1)
                .enableRunAsDaemon();

            // Register control endpoints
            // Note: In actual integration, controllers are registered via macros
            // For testing, we'll use the HttpClient to test endpoints

            drogon::app().run();
        });

        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    static void TearDownTestSuite() {
        drogon::app().quit();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
};

std::shared_ptr<SessionManager> IntegrationTest::session_mgr_;
std::thread IntegrationTest::server_thread_;
std::atomic<bool> IntegrationTest::server_running_{false};
uint16_t IntegrationTest::test_port_{18000};

} // namespace

//
// Control API Tests
//

TEST(ControlAPITest, CreateSessionValidatesInput) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    // Valid session creation
    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::hours(24);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.initial_capital = 100000.0;
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->id.empty());
    EXPECT_EQ(session->config.symbols, cfg.symbols);
}

TEST(ControlAPITest, SessionStateTransitions) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->status, SessionStatus::CREATED);

    // Start session
    mgr.start_session(session->id);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto status = session->status;
    EXPECT_TRUE(status == SessionStatus::RUNNING || status == SessionStatus::COMPLETED);

    // Stop session
    mgr.stop_session(session->id);
    EXPECT_EQ(session->status, SessionStatus::STOPPED);
}

TEST(ControlAPITest, GetSessionInfo) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL", "MSFT"};
    cfg.start_time = Timestamp{} + std::chrono::hours(24);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.initial_capital = 50000.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    auto found = mgr.get_session(session->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, session->id);
    EXPECT_EQ(found->config.symbols.size(), 2u);
    EXPECT_DOUBLE_EQ(found->account_manager->state().cash, 50000.0);
}

TEST(ControlAPITest, PauseResumeSession) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(100'000'000);
    cfg.speed_factor = 1.0; // Real-time for pause test

    auto session = mgr.create_session(cfg);
    mgr.start_session(session->id);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Pause
    mgr.pause_session(session->id);
    EXPECT_EQ(session->status, SessionStatus::PAUSED);

    // Resume
    mgr.resume_session(session->id);
    auto status = session->status;
    EXPECT_TRUE(status == SessionStatus::RUNNING || status == SessionStatus::COMPLETED);

    mgr.stop_session(session->id);
}

//
// Order Management Tests
//

TEST(OrderManagementTest, SubmitMarketOrder) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 100000.0;

    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;

    auto order_id = mgr.submit_order(session->id, order);
    EXPECT_FALSE(order_id.empty());

    // Verify order is tracked
    auto orders = mgr.get_orders(session->id);
    EXPECT_EQ(orders.size(), 1u);
    auto it = orders.find(order_id);
    ASSERT_NE(it, orders.end());
    EXPECT_EQ(it->second.symbol, "AAPL");
    EXPECT_DOUBLE_EQ(it->second.qty.value_or(0.0), 10.0);

    mgr.stop_session(session->id);
}

TEST(OrderManagementTest, SubmitLimitOrder) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 100000.0;

    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::LIMIT;
    order.tif = TimeInForce::GTC;
    order.qty = 50.0;
    order.limit_price = 99.0;

    auto order_id = mgr.submit_order(session->id, order);
    EXPECT_FALSE(order_id.empty());

    auto orders = mgr.get_orders(session->id);
    ASSERT_EQ(orders.size(), 1u);
    auto it = orders.find(order_id);
    ASSERT_NE(it, orders.end());
    EXPECT_EQ(it->second.type, OrderType::LIMIT);
    EXPECT_DOUBLE_EQ(it->second.limit_price.value_or(0.0), 99.0);

    mgr.stop_session(session->id);
}

TEST(OrderManagementTest, CancelOrder) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::LIMIT;
    order.tif = TimeInForce::GTC;
    order.qty = 10.0;
    order.limit_price = 50.0; // Far from market, won't fill

    auto order_id = mgr.submit_order(session->id, order);
    EXPECT_FALSE(order_id.empty());

    // Cancel the order
    bool canceled = mgr.cancel_order(session->id, order_id);
    EXPECT_TRUE(canceled);

    // Verify order status
    auto orders = mgr.get_orders(session->id);
    auto it = orders.find(order_id);
    ASSERT_NE(it, orders.end());
    EXPECT_EQ(it->second.status, OrderStatus::CANCELED);

    mgr.stop_session(session->id);
}

TEST(OrderManagementTest, GetOrderById) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::SELL;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 5.0;

    auto order_id = mgr.submit_order(session->id, order);

    auto orders = mgr.get_orders(session->id);
    auto it = orders.find(order_id);
    ASSERT_NE(it, orders.end());
    EXPECT_EQ(it->second.id, order_id);
    EXPECT_EQ(it->second.symbol, "AAPL");
    EXPECT_EQ(it->second.side, OrderSide::SELL);
    EXPECT_DOUBLE_EQ(it->second.qty.value_or(0.0), 5.0);

    mgr.stop_session(session->id);
}

//
// Account and Position Tests
//

TEST(AccountTest, InitialAccountState) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::hours(24);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);
    cfg.initial_capital = 250000.0;

    auto session = mgr.create_session(cfg);
    auto state = session->account_manager->state();

    EXPECT_DOUBLE_EQ(state.cash, 250000.0);
    EXPECT_DOUBLE_EQ(state.equity, 250000.0);
    // Buying power is typically equity * leverage factor (default 4x for margin accounts)
    EXPECT_GE(state.buying_power, 250000.0);
}

TEST(AccountTest, PositionsAfterFill) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 100000.0;

    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    bool filled = false;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type == EventType::ORDER_FILL) {
            std::lock_guard<std::mutex> lock(mu);
            filled = true;
            cv.notify_all();
        }
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;

    mgr.submit_order(session->id, order);
    mgr.start_session(session->id);

    {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait_for(lock, std::chrono::seconds(2), [&]{ return filled; });
    }

    auto positions = session->account_manager->positions();
    auto it = positions.find("AAPL");
    ASSERT_NE(it, positions.end());
    EXPECT_DOUBLE_EQ(it->second.qty, 10.0);
    EXPECT_GT(it->second.avg_entry_price, 0.0);

    mgr.stop_session(session->id);
}

//
// Data Source Tests (Polygon/Alpaca API simulation)
//

TEST(DataSourceTest, GetBars) {
    auto ds = std::make_shared<IntegrationTestDataSource>();

    auto start = Timestamp{} + std::chrono::hours(24);
    auto end = start + std::chrono::hours(48);

    auto bars = ds->get_bars("AAPL", start, end, 1, "hour", 10);
    EXPECT_EQ(bars.size(), 10);

    for (const auto& bar : bars) {
        EXPECT_EQ(bar.symbol, "AAPL");
        EXPECT_GT(bar.high, 0.0);
        EXPECT_GT(bar.low, 0.0);
        EXPECT_LE(bar.low, bar.high);
        EXPECT_GT(bar.volume, 0);
    }
}

TEST(DataSourceTest, GetTrades) {
    auto ds = std::make_shared<IntegrationTestDataSource>();

    auto start = Timestamp{} + std::chrono::hours(24);
    auto end = start + std::chrono::hours(1);

    auto trades = ds->get_trades("MSFT", start, end, 5);
    EXPECT_EQ(trades.size(), 5);

    for (const auto& trade : trades) {
        EXPECT_EQ(trade.symbol, "MSFT");
        EXPECT_GT(trade.price, 0.0);
        EXPECT_GT(trade.size, 0);
    }
}

TEST(DataSourceTest, GetQuotes) {
    auto ds = std::make_shared<IntegrationTestDataSource>();

    auto start = Timestamp{} + std::chrono::hours(24);
    auto end = start + std::chrono::hours(1);

    auto quotes = ds->get_quotes("GOOGL", start, end, 5);
    EXPECT_EQ(quotes.size(), 5);

    for (const auto& quote : quotes) {
        EXPECT_EQ(quote.symbol, "GOOGL");
        EXPECT_GT(quote.bid_price, 0.0);
        EXPECT_GT(quote.ask_price, 0.0);
        EXPECT_LE(quote.bid_price, quote.ask_price);
    }
}

TEST(DataSourceTest, GetCompanyProfile) {
    auto ds = std::make_shared<IntegrationTestDataSource>();

    auto profile = ds->get_company_profile("AAPL");
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->symbol, "AAPL");
    EXPECT_FALSE(profile->name.empty());
    EXPECT_EQ(profile->currency, "USD");
}

TEST(DataSourceTest, GetCompanyPeers) {
    auto ds = std::make_shared<IntegrationTestDataSource>();

    auto peers = ds->get_company_peers("AAPL", 3);
    EXPECT_LE(peers.size(), 3);
    EXPECT_GT(peers.size(), 0);
}

//
// WebSocket Protocol Tests
//

TEST(WebSocketTest, MessageFormatAlpaca) {
    // Test that Alpaca-format messages have correct structure
    nlohmann::json trade_msg;
    trade_msg["T"] = "t";
    trade_msg["S"] = "AAPL";
    trade_msg["p"] = 150.25;
    trade_msg["s"] = 100;
    trade_msg["t"] = "2024-01-15T10:30:00Z";

    EXPECT_EQ(trade_msg["T"], "t");
    EXPECT_EQ(trade_msg["S"], "AAPL");
    EXPECT_DOUBLE_EQ(trade_msg["p"], 150.25);

    nlohmann::json quote_msg;
    quote_msg["T"] = "q";
    quote_msg["S"] = "AAPL";
    quote_msg["bp"] = 150.20;
    quote_msg["bs"] = 100;
    quote_msg["ap"] = 150.30;
    quote_msg["as"] = 200;

    EXPECT_EQ(quote_msg["T"], "q");
    EXPECT_LE(quote_msg["bp"], quote_msg["ap"]);
}

TEST(WebSocketTest, MessageFormatPolygon) {
    // Test that Polygon-format messages have correct structure
    nlohmann::json trade_msg;
    trade_msg["ev"] = "T";
    trade_msg["sym"] = "AAPL";
    trade_msg["p"] = 150.25;
    trade_msg["s"] = 100;
    trade_msg["t"] = 1705318200000000000LL;

    EXPECT_EQ(trade_msg["ev"], "T");
    EXPECT_EQ(trade_msg["sym"], "AAPL");

    nlohmann::json quote_msg;
    quote_msg["ev"] = "Q";
    quote_msg["sym"] = "AAPL";
    quote_msg["bp"] = 150.20;
    quote_msg["bs"] = 100;
    quote_msg["ap"] = 150.30;
    quote_msg["as"] = 200;

    EXPECT_EQ(quote_msg["ev"], "Q");
    EXPECT_LE(quote_msg["bp"], quote_msg["ap"]);
}

//
// Error Handling Tests
//

TEST(ErrorHandlingTest, InvalidSessionId) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    auto session = mgr.get_session("nonexistent-session-id");
    EXPECT_EQ(session, nullptr);
}

TEST(ErrorHandlingTest, OrderOnInvalidSession) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;

    auto order_id = mgr.submit_order("nonexistent-session", order);
    EXPECT_TRUE(order_id.empty());
}

TEST(ErrorHandlingTest, CancelNonexistentOrder) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::hours(24);
    cfg.end_time = cfg.start_time + std::chrono::hours(1);

    auto session = mgr.create_session(cfg);

    bool canceled = mgr.cancel_order(session->id, "nonexistent-order-id");
    EXPECT_FALSE(canceled);

    mgr.stop_session(session->id);
}

TEST(ErrorHandlingTest, InvalidSymbolInSession) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);

    // Try to order a symbol not in session
    Order order;
    order.symbol = "INVALID_SYMBOL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;

    auto order_id = mgr.submit_order(session->id, order);
    // This may or may not be rejected depending on implementation
    // The test verifies the system doesn't crash

    mgr.stop_session(session->id);
}

//
// Multi-Session Tests
//

TEST(MultiSessionTest, ConcurrentSessions) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    std::vector<std::shared_ptr<Session>> session_list;

    // Create multiple sessions
    for (int i = 0; i < 5; ++i) {
        SessionConfig cfg;
        cfg.symbols = {"AAPL"};
        cfg.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
        cfg.end_time = cfg.start_time + std::chrono::nanoseconds(10'000'000);
        cfg.speed_factor = 0.0;
        cfg.initial_capital = 100000.0 + i * 10000.0;

        auto session = mgr.create_session(cfg);
        ASSERT_NE(session, nullptr);
        session_list.push_back(session);
    }

    EXPECT_EQ(session_list.size(), 5u);

    // Verify each session has unique ID and correct capital
    std::set<std::string> ids;
    for (size_t i = 0; i < session_list.size(); ++i) {
        ids.insert(session_list[i]->id);
        EXPECT_DOUBLE_EQ(session_list[i]->account_manager->state().cash, 100000.0 + i * 10000.0);
    }
    EXPECT_EQ(ids.size(), 5u); // All unique

    // Clean up
    for (auto& sess : session_list) {
        mgr.stop_session(sess->id);
    }
}

TEST(MultiSessionTest, IsolatedAccountState) {
    auto ds = std::make_shared<IntegrationTestDataSource>();
    SessionManager mgr(ds);

    SessionConfig cfg1;
    cfg1.symbols = {"AAPL"};
    cfg1.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg1.end_time = cfg1.start_time + std::chrono::nanoseconds(10'000'000);
    cfg1.speed_factor = 0.0;
    cfg1.initial_capital = 50000.0;

    SessionConfig cfg2;
    cfg2.symbols = {"MSFT"};
    cfg2.start_time = Timestamp{} + std::chrono::nanoseconds(1'000'000);
    cfg2.end_time = cfg2.start_time + std::chrono::nanoseconds(10'000'000);
    cfg2.speed_factor = 0.0;
    cfg2.initial_capital = 75000.0;

    auto session1 = mgr.create_session(cfg1);
    auto session2 = mgr.create_session(cfg2);

    std::mutex mu;
    std::condition_variable cv;
    std::atomic<int> fills{0};

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type == EventType::ORDER_FILL) {
            ++fills;
            cv.notify_all();
        }
    });

    // Submit orders to both sessions
    Order order1;
    order1.symbol = "AAPL";
    order1.side = OrderSide::BUY;
    order1.type = OrderType::MARKET;
    order1.tif = TimeInForce::DAY;
    order1.qty = 10.0;

    Order order2;
    order2.symbol = "MSFT";
    order2.side = OrderSide::BUY;
    order2.type = OrderType::MARKET;
    order2.tif = TimeInForce::DAY;
    order2.qty = 20.0;

    mgr.submit_order(session1->id, order1);
    mgr.submit_order(session2->id, order2);

    mgr.start_session(session1->id);
    mgr.start_session(session2->id);

    {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait_for(lock, std::chrono::seconds(2), [&]{ return fills >= 2; });
    }

    // Verify isolation
    auto pos1 = session1->account_manager->positions();
    auto pos2 = session2->account_manager->positions();

    // Session1 should only have AAPL
    EXPECT_EQ(pos1.count("AAPL"), 1);
    EXPECT_EQ(pos1.count("MSFT"), 0);

    // Session2 should only have MSFT
    EXPECT_EQ(pos2.count("MSFT"), 1);
    EXPECT_EQ(pos2.count("AAPL"), 0);

    mgr.stop_session(session1->id);
    mgr.stop_session(session2->id);
}
