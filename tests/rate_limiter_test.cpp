#include <gtest/gtest.h>
#include "../src/core/rate_limiter.hpp"
#include <thread>
#include <vector>

using namespace broker_sim;

TEST(RateLimiterTest, AllowsWithinLimit) {
    RateLimiter rl(5, std::chrono::seconds(1));
    int allowed = 0;
    for (int i = 0; i < 5; ++i) {
        if (rl.allow("k")) ++allowed;
    }
    EXPECT_EQ(allowed, 5);
    EXPECT_FALSE(rl.allow("k"));
}

TEST(RateLimiterTest, ResetsAfterWindow) {
    RateLimiter rl(2, std::chrono::seconds(1));
    EXPECT_TRUE(rl.allow("k"));
    EXPECT_TRUE(rl.allow("k"));
    EXPECT_FALSE(rl.allow("k"));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(rl.allow("k"));
}
