#pragma once

#include "data_source.hpp"
#include <clickhouse/client.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

namespace broker_sim {

struct ClickHouseConfig {
    std::string host{"localhost"};
    uint16_t port{9000};
    std::string database{"polygon"};
    std::string user{"default"};
    std::string password{};
};

class ClickHouseDataSource : public DataSource {
public:
    explicit ClickHouseDataSource(const ClickHouseConfig& cfg);
    ~ClickHouseDataSource() override;

    void connect();
    void disconnect();
    bool is_connected() const { return client_ != nullptr; }

    void stream_trades(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const TradeRecord&)>& cb) override;

    void stream_quotes(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const QuoteRecord&)>& cb) override;

    void stream_events(const std::vector<std::string>& symbols,
                       Timestamp start_time,
                       Timestamp end_time,
                       const std::function<void(const MarketEvent&)>& cb) override;

    std::vector<TradeRecord> get_trades(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override;

    std::vector<QuoteRecord> get_quotes(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override;

    std::vector<BarRecord> get_bars(const std::string& symbol,
                                    Timestamp start_time,
                                    Timestamp end_time,
                                    int multiplier,
                                    const std::string& timespan,
                                    size_t limit) override;

    std::vector<CompanyNewsRecord> get_company_news(const std::string& symbol,
                                                    Timestamp start_time,
                                                    Timestamp end_time,
                                                    size_t limit) override;

    std::optional<CompanyProfileRecord> get_company_profile(const std::string& symbol) override;

    std::vector<std::string> get_company_peers(const std::string& symbol,
                                               size_t limit) override;

    std::optional<NewsSentimentRecord> get_news_sentiment(const std::string& symbol) override;

    std::optional<BasicFinancialsRecord> get_basic_financials(const std::string& symbol) override;

    std::vector<DividendRecord> get_dividends(const std::string& symbol,
                                              Timestamp start_time,
                                              Timestamp end_time,
                                              size_t limit) override;

    std::vector<SplitRecord> get_splits(const std::string& symbol,
                                        Timestamp start_time,
                                        Timestamp end_time,
                                        size_t limit) override;

    std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string& symbol,
                                                              Timestamp start_time,
                                                              Timestamp end_time,
                                                              size_t limit) override;

    std::vector<RecommendationRecord> get_recommendation_trends(const std::string& symbol,
                                                                Timestamp start_time,
                                                                Timestamp end_time,
                                                                size_t limit) override;

    std::optional<PriceTargetRecord> get_price_targets(const std::string& symbol) override;

    std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string& symbol,
                                                               Timestamp start_time,
                                                               Timestamp end_time,
                                                               size_t limit) override;

private:
    static std::string build_symbol_list(const std::vector<std::string>& symbols);
    static std::string format_timestamp(Timestamp ts);
    static Timestamp extract_ts(const clickhouse::ColumnRef& col, size_t row);
    static std::string interval_expr(int multiplier, const std::string& timespan);
    static std::string limit_clause(size_t limit);
    static Timestamp extract_ts_any(const clickhouse::ColumnRef& col, size_t row);
    static std::optional<double> get_nullable_float(const clickhouse::ColumnRef& col, size_t row);
    static std::optional<uint32_t> get_nullable_uint32(const clickhouse::ColumnRef& col, size_t row);
    static std::optional<uint16_t> get_nullable_uint16(const clickhouse::ColumnRef& col, size_t row);
    static std::optional<int> get_nullable_int32(const clickhouse::ColumnRef& col, size_t row);
    static std::optional<std::string> get_nullable_string(const clickhouse::ColumnRef& col, size_t row);

    ClickHouseConfig cfg_;
    std::unique_ptr<clickhouse::Client> client_;
};

} // namespace broker_sim
