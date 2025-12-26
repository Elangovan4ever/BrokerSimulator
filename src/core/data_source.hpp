#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <optional>
#include "event_queue.hpp"

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

struct TradeRecord {
    Timestamp timestamp;
    std::string symbol;
    double price;
    int64_t size;
    int exchange;
    std::string conditions;
    int tape;
};

struct QuoteRecord {
    Timestamp timestamp;
    std::string symbol;
    double bid_price;
    int64_t bid_size;
    double ask_price;
    int64_t ask_size;
    int bid_exchange;
    int ask_exchange;
    int tape;
};

struct BarRecord {
    Timestamp timestamp;
    std::string symbol;
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
    double vwap;
    int64_t trade_count;
};

struct CompanyNewsRecord {
    Timestamp datetime;
    std::string headline;
    std::string summary;
    std::string source;
    std::string url;
    std::string image;
    std::string category;
    std::string related;
    int64_t id{0};
};

struct CompanyProfileRecord {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string industry;
    Timestamp ipo;
    double market_capitalization{0.0};
    double share_outstanding{0.0};
    std::string country;
    std::string currency;
    std::string estimate_currency;
    std::string weburl;
    std::string logo;
    std::string phone;
    std::string raw_json;
};

struct NewsSentimentRecord {
    std::string symbol;
    uint32_t articles_in_last_week{0};
    double buzz{0.0};
    double weekly_average{0.0};
    double company_news_score{0.0};
    double sector_average_bullish_percent{0.0};
    double sector_average_news_score{0.0};
    double bullish_percent{0.0};
    double bearish_percent{0.0};
    std::string raw_json;
};

struct BasicFinancialsRecord {
    std::string symbol;
    Timestamp metric_date;
    double market_capitalization{0.0};
    double pe_ttm{0.0};
    double forward_pe{0.0};
    double pb{0.0};
    double dividend_yield_ttm{0.0};
    double revenue_per_share_ttm{0.0};
    double eps_ttm{0.0};
    double free_cash_flow_per_share_ttm{0.0};
    double beta{0.0};
    double fifty_two_week_high{0.0};
    double fifty_two_week_low{0.0};
    std::string raw_json;
};

struct DividendRecord {
    std::string id;
    std::string symbol;
    Timestamp date;
    double amount{0.0};
    double adjusted_amount{0.0};
    Timestamp pay_date;
    Timestamp record_date;
    Timestamp declaration_date;
    std::string currency;
    int frequency{0};
    std::string dividend_type;
};

struct StockDividendsQuery {
    std::optional<std::string> ticker;
    std::optional<std::string> ticker_gt;
    std::optional<std::string> ticker_gte;
    std::optional<std::string> ticker_lt;
    std::optional<std::string> ticker_lte;
    std::optional<Timestamp> ex_dividend_date;
    std::optional<Timestamp> ex_dividend_date_gt;
    std::optional<Timestamp> ex_dividend_date_gte;
    std::optional<Timestamp> ex_dividend_date_lt;
    std::optional<Timestamp> ex_dividend_date_lte;
    std::optional<Timestamp> record_date;
    std::optional<Timestamp> record_date_gt;
    std::optional<Timestamp> record_date_gte;
    std::optional<Timestamp> record_date_lt;
    std::optional<Timestamp> record_date_lte;
    std::optional<Timestamp> declaration_date;
    std::optional<Timestamp> declaration_date_gt;
    std::optional<Timestamp> declaration_date_gte;
    std::optional<Timestamp> declaration_date_lt;
    std::optional<Timestamp> declaration_date_lte;
    std::optional<Timestamp> pay_date;
    std::optional<Timestamp> pay_date_gt;
    std::optional<Timestamp> pay_date_gte;
    std::optional<Timestamp> pay_date_lt;
    std::optional<Timestamp> pay_date_lte;
    std::optional<double> cash_amount;
    std::optional<double> cash_amount_gt;
    std::optional<double> cash_amount_gte;
    std::optional<double> cash_amount_lt;
    std::optional<double> cash_amount_lte;
    std::optional<int> frequency;
    std::optional<std::string> dividend_type;
    std::string sort{"ex_dividend_date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_ex_dividend_date;
};

struct SplitRecord {
    std::string symbol;
    Timestamp date;
    double from_factor{0.0};
    double to_factor{0.0};
};

struct EarningsCalendarRecord {
    std::string symbol;
    Timestamp date;
    int quarter{0};
    int year{0};
    double eps_estimate{0.0};
    double eps_actual{0.0};
    double revenue_estimate{0.0};
    double revenue_actual{0.0};
    std::string hour;
};

struct RecommendationRecord {
    std::string symbol;
    Timestamp period;
    int strong_buy{0};
    int buy{0};
    int hold{0};
    int sell{0};
    int strong_sell{0};
};

struct PriceTargetRecord {
    std::string symbol;
    Timestamp last_updated;
    int number_analysts{0};
    double target_high{0.0};
    double target_low{0.0};
    double target_mean{0.0};
    double target_median{0.0};
};

struct UpgradeDowngradeRecord {
    std::string symbol;
    Timestamp grade_time;
    std::string company;
    std::string from_grade;
    std::string to_grade;
    std::string action;
};

enum class MarketEventType : uint8_t { TRADE = 0, QUOTE = 1 };

struct MarketEvent {
    Timestamp timestamp;
    MarketEventType type;
    TradeRecord trade;
    QuoteRecord quote;
};

class DataSource {
public:
    virtual ~DataSource() = default;

    virtual void stream_trades(const std::vector<std::string>& symbols,
                               Timestamp start_time,
                               Timestamp end_time,
                               const std::function<void(const TradeRecord&)>& cb) = 0;

    virtual void stream_quotes(const std::vector<std::string>& symbols,
                               Timestamp start_time,
                               Timestamp end_time,
                               const std::function<void(const QuoteRecord&)>& cb) = 0;

    // Chronological merged stream of trades+quotes.
    virtual void stream_events(const std::vector<std::string>& symbols,
                               Timestamp start_time,
                               Timestamp end_time,
                               const std::function<void(const MarketEvent&)>& cb) = 0;

    // Query helpers for API endpoints.
    virtual std::vector<TradeRecord> get_trades(const std::string& symbol,
                                                Timestamp start_time,
                                                Timestamp end_time,
                                                size_t limit) = 0;

    virtual std::vector<QuoteRecord> get_quotes(const std::string& symbol,
                                                Timestamp start_time,
                                                Timestamp end_time,
                                                size_t limit) = 0;

    virtual std::vector<BarRecord> get_bars(const std::string& symbol,
                                            Timestamp start_time,
                                            Timestamp end_time,
                                            int multiplier,
                                            const std::string& timespan,
                                            size_t limit) = 0;

    virtual std::vector<CompanyNewsRecord> get_company_news(const std::string& symbol,
                                                            Timestamp start_time,
                                                            Timestamp end_time,
                                                            size_t limit) = 0;

    virtual std::optional<CompanyProfileRecord> get_company_profile(const std::string& symbol) = 0;

    virtual std::vector<std::string> get_company_peers(const std::string& symbol,
                                                       size_t limit) = 0;

    virtual std::optional<NewsSentimentRecord> get_news_sentiment(const std::string& symbol) = 0;

    virtual std::optional<BasicFinancialsRecord> get_basic_financials(const std::string& symbol) = 0;

    virtual std::vector<DividendRecord> get_dividends(const std::string& symbol,
                                                      Timestamp start_time,
                                                      Timestamp end_time,
                                                      size_t limit) = 0;
    virtual std::vector<DividendRecord> get_stock_dividends(const StockDividendsQuery& query) = 0;

    virtual std::vector<SplitRecord> get_splits(const std::string& symbol,
                                                Timestamp start_time,
                                                Timestamp end_time,
                                                size_t limit) = 0;

    virtual std::vector<EarningsCalendarRecord> get_earnings_calendar(const std::string& symbol,
                                                                      Timestamp start_time,
                                                                      Timestamp end_time,
                                                                      size_t limit) = 0;

    virtual std::vector<RecommendationRecord> get_recommendation_trends(const std::string& symbol,
                                                                        Timestamp start_time,
                                                                        Timestamp end_time,
                                                                        size_t limit) = 0;

    virtual std::optional<PriceTargetRecord> get_price_targets(const std::string& symbol) = 0;

    virtual std::vector<UpgradeDowngradeRecord> get_upgrades_downgrades(const std::string& symbol,
                                                                       Timestamp start_time,
                                                                       Timestamp end_time,
                                                                       size_t limit) = 0;
};

} // namespace broker_sim
