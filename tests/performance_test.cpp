#include <gtest/gtest.h>
#include "../src/core/performance.hpp"

using namespace broker_sim;

TEST(PerformanceTest, MetricsBasic) {
    PerformanceTracker tracker;
    tracker.record(Timestamp{} + std::chrono::seconds(0), 100.0);
    tracker.record(Timestamp{} + std::chrono::seconds(1), 110.0);
    tracker.record(Timestamp{} + std::chrono::seconds(2), 105.0);
    auto m = tracker.metrics();
    EXPECT_NEAR(m.total_return, 0.05, 1e-6);
    EXPECT_NEAR(m.max_drawdown, (110.0 - 105.0) / 110.0, 1e-6);
    EXPECT_NE(m.sharpe, 0.0);
}
