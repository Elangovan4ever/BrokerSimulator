#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <thread>
#include "../src/core/time_engine.hpp"

using namespace broker_sim;

TEST(TimeEngineTest, StopInterruptsWait) {
    TimeEngine engine;
    engine.set_time(Timestamp{} + std::chrono::seconds(0));
    engine.set_speed(1.0);
    engine.start();

    auto future = std::async(std::launch::async, [&engine] {
        return engine.wait_for_next_event(Timestamp{} + std::chrono::hours(1));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop();

    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_FALSE(future.get());
}
