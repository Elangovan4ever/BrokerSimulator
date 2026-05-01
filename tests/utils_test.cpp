#include <gtest/gtest.h>

#include "../src/core/utils.hpp"

using broker_sim::utils::parse_ts_any;
using broker_sim::utils::ts_to_iso;
using broker_sim::utils::ts_to_ns;

TEST(UtilsTest, ParseIsoTimestampPreservesFractionalSecondsAndOffset) {
    auto utc = parse_ts_any("2025-05-12T13:42:13.668868687+00:00");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(ts_to_ns(*utc), 1747057333668868687LL);

    auto eastern = parse_ts_any("2025-05-12T09:42:13.668868687-04:00");
    ASSERT_TRUE(eastern.has_value());
    EXPECT_EQ(ts_to_ns(*eastern), 1747057333668868687LL);
}

TEST(UtilsTest, TimestampIsoFormattingPreservesFractionalSeconds) {
    auto ts = parse_ts_any("2025-05-12T13:42:13.668868687+00:00");
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts_to_iso(*ts), "2025-05-12T13:42:13.668868687Z");

    auto whole_second = parse_ts_any("2025-05-12T13:42:13Z");
    ASSERT_TRUE(whole_second.has_value());
    EXPECT_EQ(ts_to_iso(*whole_second), "2025-05-12T13:42:13Z");
}
