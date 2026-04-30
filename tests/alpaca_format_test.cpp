#include <gtest/gtest.h>

#include "../src/control/alpaca_format.hpp"

using namespace broker_sim;

TEST(AlpacaFormatTest, PositionQuantitiesPreserveFractionalShares) {
    Position position;
    position.symbol = "IRWD";
    position.qty = -1747.579602;
    position.avg_entry_price = 5.385;
    position.market_value = -9409.21115777;
    position.cost_basis = -9409.21115777;
    position.unrealized_pl = 0.0;

    auto json = alpaca_format::format_position(position);

    EXPECT_EQ(json["side"], "short");
    EXPECT_EQ(json["qty"], "1747.579602");
    EXPECT_EQ(json["qty_available"], "1747.579602");
}
