#include <gtest/gtest.h>
#include <chrono>
#include <ctime>
#include "../src/core/config.hpp"

using namespace broker_sim;

namespace {

using TimePoint = std::chrono::system_clock::time_point;

TimePoint make_utc(int year, int month, int day, int hour, int minute, int sec = 0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = sec;
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

} // namespace

TEST(MarketHoursTest, UsesDstForSessionLookup) {
    ExecutionConfig cfg;

    auto dst_time = make_utc(2025, 7, 1, 13, 0, 0);  // 09:00 ET (DST)
    EXPECT_EQ(cfg.get_market_session(dst_time), ExecutionConfig::MarketSession::PREMARKET);

    auto est_time = make_utc(2025, 1, 2, 14, 0, 0);  // 09:00 ET (EST)
    EXPECT_EQ(cfg.get_market_session(est_time), ExecutionConfig::MarketSession::PREMARKET);
}

TEST(MarketHoursTest, NextMarketOpenSkipsClosedHours) {
    ExecutionConfig cfg;

    auto late_dst = make_utc(2025, 7, 1, 0, 30, 0);  // 20:30 ET previous day
    auto next_open_dst = cfg.next_market_open_after(late_dst);
    EXPECT_EQ(next_open_dst, make_utc(2025, 7, 1, 8, 0, 0));  // 04:00 ET

    auto late_est = make_utc(2025, 1, 7, 1, 0, 0);  // 20:00 ET previous day
    auto next_open_est = cfg.next_market_open_after(late_est);
    EXPECT_EQ(next_open_est, make_utc(2025, 1, 7, 9, 0, 0));  // 04:00 ET
}
