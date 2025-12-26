#pragma once

#include "data_source.hpp"
#include <spdlog/spdlog.h>

namespace broker_sim {

class StubDataSource : public DataSource {
public:
    void stream_trades(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const TradeRecord&)>& cb) override {
        spdlog::warn("StubDataSource: no trades streamed (ClickHouse not linked)");
    }

    void stream_quotes(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const QuoteRecord&)>& cb) override {
        spdlog::warn("StubDataSource: no quotes streamed (ClickHouse not linked)");
    }

    void stream_events(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const MarketEvent&)>& cb) override {
        spdlog::warn("StubDataSource: no events streamed (ClickHouse not linked)");
    }

    std::vector<TradeRecord> get_trades(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::vector<QuoteRecord> get_quotes(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::vector<BarRecord> get_bars(const std::string& symbol,
                                    Timestamp start_time,
                                    Timestamp end_time,
                                    int multiplier,
                                    const std::string& timespan,
                                    size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)multiplier;
        (void)timespan;
        (void)limit;
        return {};
    }

    std::vector<CompanyNewsRecord> get_company_news(const std::string& symbol,
                                                    Timestamp start_time,
                                                    Timestamp end_time,
                                                    size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::optional<CompanyProfileRecord> get_company_profile(const std::string& symbol) override {
        (void)symbol;
        return std::nullopt;
    }

    std::vector<std::string> get_company_peers(const std::string& symbol,
                                               size_t limit) override {
        (void)symbol;
        (void)limit;
        return {};
    }

    std::optional<NewsSentimentRecord> get_news_sentiment(const std::string& symbol) override {
        (void)symbol;
        return std::nullopt;
    }

    std::optional<BasicFinancialsRecord> get_basic_financials(const std::string& symbol) override {
        (void)symbol;
        return std::nullopt;
    }

    std::vector<DividendRecord> get_dividends(const std::string& symbol,
                                              Timestamp start_time,
                                              Timestamp end_time,
                                              size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::vector<DividendRecord> get_stock_dividends(const StockDividendsQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<StockSplitRecord> get_stock_splits(const StockSplitsQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<StockNewsRecord> get_stock_news(const StockNewsQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<StockNewsInsightRecord> get_stock_news_insights(const std::vector<std::string>& article_ids) override {
        (void)article_ids;
        return {};
    }

    std::vector<StockIpoRecord> get_stock_ipos(const StockIposQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<StockShortInterestRecord> get_stock_short_interest(const StockShortInterestQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<StockShortVolumeRecord> get_stock_short_volume(const StockShortVolumeQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<FinancialsRecord> get_stock_financials(const FinancialsQuery& query) override {
        (void)query;
        return {};
    }

    std::vector<SplitRecord> get_splits(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string& symbol,
                                                              Timestamp start_time,
                                                              Timestamp end_time,
                                                              size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::vector<RecommendationRecord> get_recommendation_trends(const std::string& symbol,
                                                                Timestamp start_time,
                                                                Timestamp end_time,
                                                                size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }

    std::optional<PriceTargetRecord> get_price_targets(const std::string& symbol) override {
        (void)symbol;
        return std::nullopt;
    }

    std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string& symbol,
                                                               Timestamp start_time,
                                                               Timestamp end_time,
                                                               size_t limit) override {
        (void)symbol;
        (void)start_time;
        (void)end_time;
        (void)limit;
        return {};
    }
};

} // namespace broker_sim
