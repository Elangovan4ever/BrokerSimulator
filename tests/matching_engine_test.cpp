#include <gtest/gtest.h>
#include <chrono>
#include "../src/core/matching_engine.hpp"

using namespace broker_sim;

static NBBO make_nbbo(const std::string& sym, double bid, int64_t bid_sz, double ask, int64_t ask_sz, int64_t ts=1) {
    NBBO n;
    n.symbol = sym;
    n.bid_price = bid;
    n.bid_size = bid_sz;
    n.ask_price = ask;
    n.ask_size = ask_sz;
    n.timestamp = ts;
    return n;
}

TEST(MatchingEngineTest, MarketOrderFillsImmediately) {
    MatchingEngine eng;
    Order o;
    o.id = "m1";
    o.symbol = "AAPL";
    o.side = OrderSide::BUY;
    o.type = OrderType::MARKET;
    o.tif = TimeInForce::DAY;
    o.qty = 10.0;
    auto nbbo = make_nbbo("AAPL", 100.0, 100, 101.0, 100);
    auto result = eng.update_nbbo(nbbo);
    ASSERT_TRUE(result.fills.empty()); // no pending orders yet
    auto f = eng.submit_order(o);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->fill_qty, 10.0);
    EXPECT_DOUBLE_EQ(f->fill_price, 101.0);
}

TEST(MatchingEngineTest, IocDoesNotEnqueue) {
    MatchingEngine eng;
    Order o;
    o.id = "ioc";
    o.symbol = "MSFT";
    o.side = OrderSide::BUY;
    o.type = OrderType::LIMIT;
    o.tif = TimeInForce::IOC;
    o.qty = 10.0;
    o.limit_price = 99.0;
    auto nbbo = make_nbbo("MSFT", 100.0, 100, 101.0, 100);
    eng.update_nbbo(nbbo);
    auto f = eng.submit_order(o);
    EXPECT_FALSE(f.has_value()); // not marketable, not enqueued
    EXPECT_TRUE(eng.get_pending_orders().empty());
}

TEST(MatchingEngineTest, FokInsufficientSize) {
    MatchingEngine eng;
    Order o;
    o.id = "fok";
    o.symbol = "TSLA";
    o.side = OrderSide::BUY;
    o.type = OrderType::LIMIT;
    o.tif = TimeInForce::FOK;
    o.qty = 10.0;
    o.limit_price = 200.0;
    auto nbbo = make_nbbo("TSLA", 199.0, 100, 200.0, 5); // only 5 available on ask
    eng.update_nbbo(nbbo);
    auto f = eng.submit_order(o);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->fill_qty, 0.0); // should not fill
    EXPECT_TRUE(eng.get_pending_orders().empty());
}

TEST(MatchingEngineTest, StopTriggersOnce) {
    MatchingEngine eng;
    Order o;
    o.id = "stop";
    o.symbol = "IBM";
    o.side = OrderSide::SELL;
    o.type = OrderType::STOP;
    o.tif = TimeInForce::DAY;
    o.qty = 5.0;
    o.stop_price = 95.0;
    // NBBO above stop -> no fill
    eng.update_nbbo(make_nbbo("IBM", 100.0, 100, 101.0, 100));
    auto f = eng.submit_order(o);
    EXPECT_FALSE(f.has_value());
    // Drop below stop -> trigger
    auto res1 = eng.update_nbbo(make_nbbo("IBM", 94.0, 100, 95.0, 100));
    ASSERT_EQ(res1.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(res1.fills[0].fill_qty, 5.0);
    // Further NBBO updates should not produce more fills for the same order
    auto res2 = eng.update_nbbo(make_nbbo("IBM", 93.0, 100, 94.0, 100));
    EXPECT_TRUE(res2.fills.empty());
}

TEST(MatchingEngineTest, TrailingStopSellTriggersOnDrop) {
    MatchingEngine eng;
    Order o;
    o.id = "trail";
    o.symbol = "NFLX";
    o.side = OrderSide::SELL;
    o.type = OrderType::TRAILING_STOP;
    o.tif = TimeInForce::DAY;
    o.qty = 5.0;
    o.trail_price = 2.0; // $2 trail
    eng.update_nbbo(make_nbbo("NFLX", 100.0, 100, 101.0, 100)); // hwm = 100.5 mid
    auto f = eng.submit_order(o);
    EXPECT_FALSE(f.has_value());
    // Mid drops more than $2 -> trigger
    auto res = eng.update_nbbo(make_nbbo("NFLX", 95.0, 100, 96.0, 100));
    ASSERT_EQ(res.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(res.fills[0].fill_qty, 5.0);
}

TEST(MatchingEngineTest, StopLimitTriggersThenAwaitsLimit) {
    MatchingEngine eng;
    Order o;
    o.id = "stoplim";
    o.symbol = "AMZN";
    o.side = OrderSide::BUY;
    o.type = OrderType::STOP_LIMIT;
    o.tif = TimeInForce::DAY;
    o.qty = 10.0;
    o.stop_price = 100.0;
    o.limit_price = 99.5;
    // Below stop -> no trigger
    eng.update_nbbo(make_nbbo("AMZN", 98.0, 100, 99.0, 100));
    auto f = eng.submit_order(o);
    EXPECT_FALSE(f.has_value());
    // Trigger stop when ask crosses 100 but limit not marketable -> no fill yet
    auto res1 = eng.update_nbbo(make_nbbo("AMZN", 100.0, 100, 101.0, 100));
    EXPECT_TRUE(res1.fills.empty());
    // Later becomes marketable at/below limit -> fill
    auto res2 = eng.update_nbbo(make_nbbo("AMZN", 99.0, 100, 99.5, 100));
    ASSERT_EQ(res2.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(res2.fills[0].fill_qty, 10.0);
}

TEST(MatchingEngineTest, OrderExpiresOnTimestamp) {
    MatchingEngine eng;
    Order o;
    o.id = "exp";
    o.symbol = "AAPL";
    o.side = OrderSide::BUY;
    o.type = OrderType::LIMIT;
    o.tif = TimeInForce::DAY;
    o.qty = 5.0;
    o.limit_price = 95.0;
    o.expire_at = Timestamp{} + std::chrono::nanoseconds(5);
    auto f = eng.submit_order(o);
    EXPECT_FALSE(f.has_value()); // queued, no NBBO yet
    auto res = eng.update_nbbo(make_nbbo("AAPL", 100.0, 100, 101.0, 100, 10));
    ASSERT_EQ(res.expired.size(), 1u);
    EXPECT_EQ(res.expired[0].id, "exp");
}
