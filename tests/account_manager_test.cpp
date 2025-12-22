#include <gtest/gtest.h>
#include "../src/core/account_manager.hpp"

using namespace broker_sim;

TEST(AccountManagerTest, ApplyFillWithFees) {
    AccountManager mgr(1000.0);
    Fill fill{"order-1", 10.0, 10.0, 0, false};
    mgr.apply_fill("AAPL", fill, OrderSide::BUY, 1.25);
    auto st = mgr.state();
    EXPECT_DOUBLE_EQ(st.cash, 1000.0 - 100.0 - 1.25);
    EXPECT_DOUBLE_EQ(st.accrued_fees, 1.25);
}

TEST(AccountManagerTest, DividendUpdatesCash) {
    AccountManager mgr(1000.0);
    Fill fill{"order-2", 10.0, 20.0, 0, false};
    mgr.apply_fill("AAPL", fill, OrderSide::BUY, 0.0);
    mgr.apply_dividend("AAPL", 0.5);
    auto st = mgr.state();
    EXPECT_DOUBLE_EQ(st.cash, 1000.0 - 200.0 + 5.0);
}

TEST(AccountManagerTest, SplitAdjustsPosition) {
    AccountManager mgr(1000.0);
    Fill fill{"order-3", 10.0, 10.0, 0, false};
    mgr.apply_fill("AAPL", fill, OrderSide::BUY, 0.0);
    mgr.apply_split("AAPL", 2.0);
    auto positions = mgr.positions();
    auto it = positions.find("AAPL");
    ASSERT_TRUE(it != positions.end());
    EXPECT_DOUBLE_EQ(it->second.qty, 20.0);
    EXPECT_DOUBLE_EQ(it->second.avg_entry_price, 5.0);
}
