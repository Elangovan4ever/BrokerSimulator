#include <gtest/gtest.h>
#include "../src/core/config.hpp"

using namespace broker_sim;

TEST(FeeConfigTest, CalculatesFeesForSell) {
    FeeConfig cfg;
    cfg.per_order_commission = 1.0;
    cfg.per_share_commission = 0.005;
    cfg.sec_fee_per_million = 27.80;
    cfg.taf_fee_per_share = 0.000166;
    cfg.finra_taf_cap = 8.30;
    cfg.taker_fee_per_share = 0.003;

    double fees = cfg.calculate_fees(100.0, 10.0, true, false);
    double expected = 1.0 + (100.0 * 0.005);
    expected += (100.0 * 10.0) * (27.80 / 1000000.0);
    expected += std::min(100.0 * 0.000166, 8.30);
    expected += 100.0 * 0.003;
    EXPECT_NEAR(fees, expected, 1e-6);
}
