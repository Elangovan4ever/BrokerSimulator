#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include "../src/core/session_manager.hpp"

using namespace broker_sim;

namespace {

class FakeDataSource : public DataSource {
public:
    explicit FakeDataSource(std::vector<MarketEvent> events)
        : events_(std::move(events)) {}

    void stream_trades(const std::vector<std::string>&,
                       Timestamp,
                       Timestamp,
                       const std::function<void(const TradeRecord&)>&) override {}

    void stream_quotes(const std::vector<std::string>&,
                       Timestamp,
                       Timestamp,
                       const std::function<void(const QuoteRecord&)>&) override {}

    void stream_events(const std::vector<std::string>&,
                       Timestamp,
                       Timestamp,
                       const std::function<void(const MarketEvent&)>& cb) override {
        for (const auto& ev : events_) cb(ev);
    }

    std::vector<TradeRecord> get_trades(const std::string&,
                                        Timestamp,
                                        Timestamp,
                                        size_t) override {
        return {};
    }

    std::vector<QuoteRecord> get_quotes(const std::string&,
                                        Timestamp,
                                        Timestamp,
                                        size_t) override {
        return {};
    }

    std::vector<BarRecord> get_bars(const std::string&,
                                    Timestamp,
                                    Timestamp,
                                    int,
                                    const std::string&,
                                    size_t) override {
        return {};
    }

    std::vector<CompanyNewsRecord> get_company_news(const std::string&,
                                                    Timestamp,
                                                    Timestamp,
                                                    size_t) override {
        return {};
    }

    std::optional<CompanyProfileRecord> get_company_profile(const std::string&) override {
        return std::nullopt;
    }

    std::vector<std::string> get_company_peers(const std::string&,
                                               size_t) override {
        return {};
    }

    std::optional<NewsSentimentRecord> get_news_sentiment(const std::string&) override {
        return std::nullopt;
    }

    std::optional<BasicFinancialsRecord> get_basic_financials(const std::string&) override {
        return std::nullopt;
    }

    std::vector<DividendRecord> get_dividends(const std::string&,
                                              Timestamp,
                                              Timestamp,
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
                                        Timestamp,
                                        Timestamp,
                                        size_t) override {
        return {};
    }

    std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string&,
                                                              Timestamp,
                                                              Timestamp,
                                                              size_t) override {
        return {};
    }

    std::vector<RecommendationRecord> get_recommendation_trends(const std::string&,
                                                                Timestamp,
                                                                Timestamp,
                                                                size_t) override {
        return {};
    }

    std::optional<PriceTargetRecord> get_price_targets(const std::string&) override {
        return std::nullopt;
    }

    std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string&,
                                                               Timestamp,
                                                               Timestamp,
                                                               size_t) override {
        return {};
    }

private:
    std::vector<MarketEvent> events_;
};

Timestamp make_ts(int64_t ns) {
    return Timestamp{} + std::chrono::nanoseconds(ns);
}

} // namespace

TEST(SessionManagerTest, MarketOrderFillsAfterFirstQuote) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    std::mutex mu;
    std::condition_variable cv;
    bool filled = false;
    OrderData fill_data{};
    int64_t fill_ts_ns = 0;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_FILL) return;
        const auto& od = std::get<OrderData>(e.data);
        if (od.order_id != order_id) return;
        std::lock_guard<std::mutex> lock(mu);
        filled = true;
        fill_data = od;
        fill_ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            e.timestamp.time_since_epoch()).count();
        cv.notify_all();
    });

    mgr.start_session(session->id);

    std::unique_lock<std::mutex> lock(mu);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return filled; }));
    EXPECT_DOUBLE_EQ(fill_data.filled_qty, 10.0);
    EXPECT_DOUBLE_EQ(fill_data.filled_avg_price, 101.0);
    EXPECT_EQ(fill_ts_ns, t1);

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, PauseStopsEventProgression) {
    int64_t t1 = 1'000'000;
    int64_t t2 = 51'000'000;
    MarketEvent ev1;
    ev1.timestamp = make_ts(t1);
    ev1.type = MarketEventType::QUOTE;
    ev1.quote = QuoteRecord{ev1.timestamp, "MSFT", 200.0, 100, 201.0, 100, 1, 1, 1};
    MarketEvent ev2;
    ev2.timestamp = make_ts(t2);
    ev2.type = MarketEventType::QUOTE;
    ev2.quote = QuoteRecord{ev2.timestamp, "MSFT", 202.0, 100, 203.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev1, ev2});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"MSFT"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(100'000'000);
    cfg.speed_factor = 1.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    int quote_events = 0;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::QUOTE) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            ++quote_events;
            if (quote_events == 1) {
                mgr.pause_session(session->id);
            }
        }
        cv.notify_all();
    });

    mgr.start_session(session->id);

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return quote_events >= 1; }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(mu);
        EXPECT_EQ(quote_events, 1);
    }

    mgr.resume_session(session->id);
    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return quote_events >= 2; }));
    }

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, OrderLifecycleEventsEmitted) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    std::vector<EventType> types;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type == EventType::ORDER_NEW ||
            e.event_type == EventType::ORDER_FILL) {
            std::lock_guard<std::mutex> lock(mu);
            types.push_back(e.event_type);
            cv.notify_all();
        }
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);

    std::unique_lock<std::mutex> lock(mu);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return types.size() >= 2; }));
    EXPECT_EQ(types[0], EventType::ORDER_NEW);
    EXPECT_EQ(types[1], EventType::ORDER_FILL);

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, IOCOrderCancelsWhenNotMarketable) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "MSFT", 200.0, 100, 201.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"MSFT"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    bool canceled = false;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_CANCEL) return;
        std::lock_guard<std::mutex> lock(mu);
        canceled = true;
        cv.notify_all();
    });

    mgr.start_session(session->id);

    Order order;
    order.symbol = "MSFT";
    order.side = OrderSide::BUY;
    order.type = OrderType::LIMIT;
    order.tif = TimeInForce::IOC;
    order.qty = 10.0;
    order.limit_price = 199.0; // Not marketable (ask 201)
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    std::unique_lock<std::mutex> lock(mu);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return canceled; }));

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, OrderExpiresEmitsEvent) {
    int64_t t1 = 5'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    bool expired = false;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_EXPIRE) return;
        std::lock_guard<std::mutex> lock(mu);
        expired = true;
        cv.notify_all();
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::LIMIT;
    order.tif = TimeInForce::GTC;
    order.qty = 10.0;
    order.limit_price = 99.0;
    order.expire_at = make_ts(1'000'000);
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);

    std::unique_lock<std::mutex> lock(mu);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return expired; }));

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, CorporateActionsAdjustAccount) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 1000.0;
    auto session = mgr.create_session(cfg);

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 2.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_TRUE(mgr.apply_dividend(session->id, "AAPL", 0.5));
    auto st = session->account_manager->state();
    EXPECT_DOUBLE_EQ(st.cash, 1000.0 - 2.0 * 101.0 + 1.0);

    EXPECT_TRUE(mgr.apply_split(session->id, "AAPL", 2.0));
    auto positions = session->account_manager->positions();
    auto it = positions.find("AAPL");
    ASSERT_TRUE(it != positions.end());
    EXPECT_DOUBLE_EQ(it->second.qty, 4.0);
    EXPECT_DOUBLE_EQ(it->second.avg_entry_price, 50.5);

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, FeesAppliedOnFill) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    FeeConfig fees;
    fees.per_order_commission = 1.0;
    fees.per_share_commission = 0.0;
    fees.sec_fee_per_million = 0.0;
    fees.taf_fee_per_share = 0.0;
    fees.taker_fee_per_share = 0.0;
    SessionManager mgr(ds, ExecutionConfig{}, fees);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 2000.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    bool filled = false;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_FILL) return;
        std::lock_guard<std::mutex> lock(mu);
        filled = true;
        cv.notify_all();
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 10.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);
    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return filled; }));
    }

    auto st = session->account_manager->state();
    EXPECT_DOUBLE_EQ(st.cash, 2000.0 - 10.0 * 101.0 - 1.0);
    EXPECT_DOUBLE_EQ(st.accrued_fees, 1.0);

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, JumpToResetsState) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 1000.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    bool filled = false;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_FILL) return;
        std::lock_guard<std::mutex> lock(mu);
        filled = true;
        cv.notify_all();
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 2.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);
    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return filled; }));
    }

    mgr.jump_to(session->id, make_ts(2'000'000));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto st = session->account_manager->state();
    EXPECT_DOUBLE_EQ(st.cash, 1000.0);
    auto positions = session->account_manager->positions();
    EXPECT_TRUE(positions.empty());
    auto orders = mgr.get_orders(session->id);
    EXPECT_TRUE(orders.empty());

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, MarginCallForcesLiquidation) {
    int64_t t1 = 1'000'000;
    int64_t t2 = 2'000'000;
    MarketEvent ev1;
    ev1.timestamp = make_ts(t1);
    ev1.type = MarketEventType::QUOTE;
    ev1.quote = QuoteRecord{ev1.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};
    MarketEvent ev2;
    ev2.timestamp = make_ts(t2);
    ev2.type = MarketEventType::QUOTE;
    ev2.quote = QuoteRecord{ev2.timestamp, "AAPL", 19.0, 100, 21.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev1, ev2});
    ExecutionConfig exec_cfg;
    exec_cfg.enable_margin_call_checks = true;
    exec_cfg.enable_forced_liquidation = true;
    SessionManager mgr(ds, exec_cfg);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    cfg.initial_capital = 1000.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    int quote_events = 0;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::QUOTE) return;
        std::lock_guard<std::mutex> lock(mu);
        ++quote_events;
        cv.notify_all();
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 19.0;
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return quote_events >= 2; }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto positions = session->account_manager->positions();
    auto it = positions.find("AAPL");
    if (it != positions.end()) {
        EXPECT_DOUBLE_EQ(it->second.qty, 0.0);
    }

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, FastForwardConsumesEventsWithoutCallbacks) {
    int64_t t1 = 1'000'000;
    int64_t t2 = 2'000'000;
    MarketEvent ev1;
    ev1.timestamp = make_ts(t1);
    ev1.type = MarketEventType::QUOTE;
    ev1.quote = QuoteRecord{ev1.timestamp, "AAPL", 100.0, 100, 101.0, 100, 1, 1, 1};
    MarketEvent ev2;
    ev2.timestamp = make_ts(t2);
    ev2.type = MarketEventType::QUOTE;
    ev2.quote = QuoteRecord{ev2.timestamp, "AAPL", 102.0, 100, 103.0, 100, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev1, ev2});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    int quote_events = 0;
    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::QUOTE) return;
        std::lock_guard<std::mutex> lock(mu);
        ++quote_events;
    });

    mgr.start_session(session->id);
    mgr.pause_session(session->id);

    mgr.fast_forward(session->id, make_ts(t2));

    {
        std::lock_guard<std::mutex> lock(mu);
        EXPECT_EQ(quote_events, 0);
    }
    auto wm = mgr.watermark_ns(session->id);
    ASSERT_TRUE(wm.has_value());
    EXPECT_EQ(*wm, t2);

    mgr.stop_session(session->id);
}

TEST(SessionManagerTest, MarketImpactAdjustsFillPrice) {
    int64_t t1 = 1'000'000;
    MarketEvent ev;
    ev.timestamp = make_ts(t1);
    ev.type = MarketEventType::QUOTE;
    ev.quote = QuoteRecord{ev.timestamp, "AAPL", 99.0, 200, 100.0, 200, 1, 1, 1};

    auto ds = std::make_shared<FakeDataSource>(std::vector<MarketEvent>{ev});
    ExecutionConfig exec_cfg;
    exec_cfg.enable_market_impact = true;
    exec_cfg.market_impact_bps = 10.0; // 10 bps at 100% of available size
    SessionManager mgr(ds, exec_cfg);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts(0);
    cfg.end_time = make_ts(10'000'000);
    cfg.speed_factor = 0.0;
    auto session = mgr.create_session(cfg);

    std::mutex mu;
    std::condition_variable cv;
    double fill_price = 0.0;

    mgr.add_event_callback([&](const std::string&, const Event& e) {
        if (e.event_type != EventType::ORDER_FILL) return;
        const auto& od = std::get<OrderData>(e.data);
        std::lock_guard<std::mutex> lock(mu);
        fill_price = od.filled_avg_price;
        cv.notify_all();
    });

    Order order;
    order.symbol = "AAPL";
    order.side = OrderSide::BUY;
    order.type = OrderType::MARKET;
    order.tif = TimeInForce::DAY;
    order.qty = 100.0; // 50% of available ask size 200 -> 5 bps impact
    auto order_id = mgr.submit_order(session->id, order);
    ASSERT_FALSE(order_id.empty());

    mgr.start_session(session->id);

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]{ return fill_price > 0.0; }));
    }

    EXPECT_NEAR(fill_price, 100.05, 1e-6);
    mgr.stop_session(session->id);
}
