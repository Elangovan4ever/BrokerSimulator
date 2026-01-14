#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <optional>
#include <unordered_map>
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
    std::string raw_json;
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
    std::string raw_json;
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

struct StockSplitsQuery {
    std::optional<std::string> ticker;
    std::optional<std::string> ticker_gt;
    std::optional<std::string> ticker_gte;
    std::optional<std::string> ticker_lt;
    std::optional<std::string> ticker_lte;
    std::optional<Timestamp> execution_date;
    std::optional<Timestamp> execution_date_gt;
    std::optional<Timestamp> execution_date_gte;
    std::optional<Timestamp> execution_date_lt;
    std::optional<Timestamp> execution_date_lte;
    std::string sort{"execution_date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_execution_date;
};

struct StockNewsQuery {
    std::optional<std::string> ticker;
    std::optional<Timestamp> published_utc;
    std::optional<Timestamp> published_utc_gt;
    std::optional<Timestamp> published_utc_gte;
    std::optional<Timestamp> published_utc_lt;
    std::optional<Timestamp> published_utc_lte;
    std::optional<Timestamp> cursor_published_utc;
    std::optional<std::string> cursor_id;
    std::string sort{"published_utc"};
    std::string order{"descending"};
    size_t limit{10};
    std::optional<Timestamp> max_published_utc;
};

struct StockTickerEventsQuery {
    std::string ticker;
    std::vector<std::string> types;
    std::string sort{"date"};
    std::string order{"desc"};
    size_t limit{10};
    std::optional<Timestamp> max_date;
};

struct StockIposQuery {
    std::optional<std::string> ticker;
    std::optional<std::string> ipo_status;
    std::optional<Timestamp> announced_date;
    std::optional<Timestamp> announced_date_gt;
    std::optional<Timestamp> announced_date_gte;
    std::optional<Timestamp> announced_date_lt;
    std::optional<Timestamp> announced_date_lte;
    std::optional<Timestamp> listing_date;
    std::optional<Timestamp> listing_date_gt;
    std::optional<Timestamp> listing_date_gte;
    std::optional<Timestamp> listing_date_lt;
    std::optional<Timestamp> listing_date_lte;
    std::optional<Timestamp> issue_start_date;
    std::optional<Timestamp> issue_start_date_gt;
    std::optional<Timestamp> issue_start_date_gte;
    std::optional<Timestamp> issue_start_date_lt;
    std::optional<Timestamp> issue_start_date_lte;
    std::optional<Timestamp> issue_end_date;
    std::optional<Timestamp> issue_end_date_gt;
    std::optional<Timestamp> issue_end_date_gte;
    std::optional<Timestamp> issue_end_date_lt;
    std::optional<Timestamp> issue_end_date_lte;
    std::optional<Timestamp> last_updated;
    std::optional<Timestamp> last_updated_gt;
    std::optional<Timestamp> last_updated_gte;
    std::optional<Timestamp> last_updated_lt;
    std::optional<Timestamp> last_updated_lte;
    std::string sort{"listing_date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_date;
};

struct StockShortInterestQuery {
    std::optional<std::string> ticker;
    std::optional<Timestamp> settlement_date;
    std::optional<Timestamp> settlement_date_gt;
    std::optional<Timestamp> settlement_date_gte;
    std::optional<Timestamp> settlement_date_lt;
    std::optional<Timestamp> settlement_date_lte;
    std::string sort{"settlement_date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_settlement_date;
};

struct StockShortVolumeQuery {
    std::optional<std::string> ticker;
    std::optional<Timestamp> trade_date;
    std::optional<Timestamp> trade_date_gt;
    std::optional<Timestamp> trade_date_gte;
    std::optional<Timestamp> trade_date_lt;
    std::optional<Timestamp> trade_date_lte;
    std::string sort{"date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_trade_date;
};

struct FinancialsQuery {
    std::optional<std::string> ticker;
    std::optional<std::string> cik;
    std::optional<std::string> timeframe;
    std::optional<std::string> fiscal_period;
    std::optional<int> fiscal_year;
    std::optional<Timestamp> period_of_report_date;
    std::optional<Timestamp> period_of_report_date_gt;
    std::optional<Timestamp> period_of_report_date_gte;
    std::optional<Timestamp> period_of_report_date_lt;
    std::optional<Timestamp> period_of_report_date_lte;
    std::optional<Timestamp> filing_date;
    std::optional<Timestamp> filing_date_gt;
    std::optional<Timestamp> filing_date_gte;
    std::optional<Timestamp> filing_date_lt;
    std::optional<Timestamp> filing_date_lte;
    std::string sort{"period_of_report_date"};
    std::string order{"desc"};
    size_t limit{10};
    size_t offset{0};
    std::optional<Timestamp> max_period_of_report_date;
};

struct StockSplitRecord {
    std::string id;
    std::string ticker;
    Timestamp execution_date;
    double split_from{0.0};
    double split_to{0.0};
};

struct StockNewsRecord {
    std::string id;
    Timestamp published_utc;
    Timestamp updated_utc;
    std::string publisher_name;
    std::string publisher_homepage_url;
    std::string publisher_logo_url;
    std::string publisher_favicon_url;
    std::string title;
    std::string author;
    std::string article_url;
    std::string amp_url;
    std::string image_url;
    std::string description;
    std::vector<std::string> tickers;
    std::vector<std::string> keywords;
};

struct StockNewsInsightRecord {
    std::string article_id;
    Timestamp published_utc;
    std::string ticker;
    std::string sentiment;
    std::string sentiment_reasoning;
    std::optional<double> sentiment_score;
    std::optional<double> relevance_score;
};

struct StockTickerEventRecord {
    std::string entity_name;
    std::string event_type;
    std::string event_date;
    std::string new_ticker;
    std::string raw_json;
};

struct TickerBasicRecord {
    std::string name;
    std::string composite_figi;
    std::string cik;
};

struct StockIpoRecord {
    std::string ticker;
    std::optional<Timestamp> announced_date;
    std::optional<Timestamp> listing_date;
    std::optional<Timestamp> issue_start_date;
    std::optional<Timestamp> issue_end_date;
    std::optional<Timestamp> last_updated;
    std::string ipo_status;
    std::string raw_json;
};

struct StockShortInterestRecord {
    std::string ticker;
    Timestamp settlement_date;
    std::optional<double> short_interest;
    std::optional<double> avg_daily_volume;
    std::optional<double> days_to_cover;
    std::string raw_json;
};

struct StockShortVolumeRecord {
    std::string ticker;
    Timestamp trade_date;
    std::optional<uint64_t> total_volume;
    std::optional<uint64_t> short_volume;
    std::optional<uint64_t> exempt_volume;
    std::optional<uint64_t> non_exempt_volume;
    std::optional<double> short_volume_ratio;
    std::optional<uint64_t> nyse_short_volume;
    std::optional<uint64_t> nyse_short_volume_exempt;
    std::optional<uint64_t> nasdaq_carteret_short_volume;
    std::optional<uint64_t> nasdaq_carteret_short_volume_exempt;
    std::optional<uint64_t> nasdaq_chicago_short_volume;
    std::optional<uint64_t> nasdaq_chicago_short_volume_exempt;
    std::optional<uint64_t> adf_short_volume;
    std::optional<uint64_t> adf_short_volume_exempt;
    std::string raw_json;
};

struct FinancialsRecord {
    std::string ticker;
    std::string cik;
    std::string company_name;
    Timestamp start_date;
    Timestamp end_date;
    Timestamp filing_date;
    Timestamp acceptance_datetime;
    std::string timeframe;
    std::string fiscal_period;
    std::string fiscal_year;
    std::string source_filing_url;
    std::string form;
    std::string currency;
    Timestamp period_of_report;
    std::unordered_map<std::string, double> balance_sheet;
    std::unordered_map<std::string, double> income_statement;
    std::unordered_map<std::string, double> cash_flow_statement;
    std::unordered_map<std::string, double> comprehensive_income;
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

struct FinnhubIpoRecord {
    std::string symbol;
    Timestamp date;
    std::string exchange;
    std::string name;
    std::optional<uint64_t> number_of_shares;
    std::string price_range;
    std::string status;
    std::optional<uint64_t> total_shares_value;
    std::string raw_json;
};

struct FinnhubInsiderTransactionRecord {
    std::string symbol;
    std::string filing_id;
    Timestamp transaction_date;
    std::string name;
    std::optional<double> share;
    std::optional<double> change;
    std::optional<double> transaction_price;
    std::string transaction_code;
    std::string raw_json;
};

struct FinnhubSecFilingRecord {
    std::string symbol;
    Timestamp filed_date;
    Timestamp accepted_datetime;
    std::string form;
    std::string access_number;
    std::string report_url;
    std::string raw_json;
};

struct FinnhubCongressionalTradingRecord {
    std::string symbol;
    Timestamp transaction_date;
    std::string name;
    std::string position;
    std::string owner_type;
    std::string transaction_type;
    std::optional<uint64_t> amount_from;
    std::optional<uint64_t> amount_to;
    std::string asset_name;
    Timestamp filing_date;
    std::string raw_json;
};

struct FinnhubInsiderSentimentRecord {
    std::string symbol;
    uint16_t year{0};
    uint8_t month{0};
    std::optional<double> change;
    std::optional<double> mspr;
};

struct FinnhubEpsEstimateRecord {
    std::string symbol;
    Timestamp period;
    std::optional<uint16_t> quarter;
    std::optional<uint16_t> year;
    std::optional<double> eps_avg;
    std::optional<double> eps_high;
    std::optional<double> eps_low;
    std::optional<uint16_t> number_analysts;
    std::string freq;
};

struct FinnhubRevenueEstimateRecord {
    std::string symbol;
    Timestamp period;
    std::optional<uint16_t> quarter;
    std::optional<uint16_t> year;
    std::optional<double> revenue_avg;
    std::optional<double> revenue_high;
    std::optional<double> revenue_low;
    std::optional<uint16_t> number_analysts;
    std::string freq;
};

struct FinnhubEarningsHistoryRecord {
    std::string symbol;
    Timestamp period;
    std::optional<uint16_t> quarter;
    std::optional<uint16_t> year;
    std::optional<double> actual;
    std::optional<double> estimate;
    std::optional<double> surprise;
    std::optional<double> surprise_percent;
};

struct FinnhubSocialSentimentRecord {
    std::string symbol;
    Timestamp at_time;
    std::optional<uint32_t> mention;
    std::optional<double> positive_score;
    std::optional<double> negative_score;
    std::optional<uint32_t> positive_mention;
    std::optional<uint32_t> negative_mention;
    std::optional<double> score;
};

struct FinnhubOwnershipRecord {
    std::string symbol;
    Timestamp report_date;
    std::string organization;
    std::optional<double> position;
    std::optional<double> position_change;
    std::optional<double> percent_held;
    std::string raw_json;
};

struct FinnhubFinancialsStandardizedRecord {
    std::string symbol;
    std::string statement;
    Timestamp period;
    std::string freq;
    std::string currency;
    std::string data_json;
};

struct FinnhubFinancialsReportedRecord {
    std::string symbol;
    Timestamp period;
    std::string freq;
    std::string access_number;
    std::string form;
    Timestamp filed_date;
    Timestamp accepted_datetime;
    std::string data_json;
};

enum class MarketEventType : uint8_t { TRADE = 0, QUOTE = 1 };

enum class UnifiedEventType : uint8_t { QUOTE = 0, TRADE = 1, BAR = 2 };

struct MarketEvent {
    Timestamp timestamp;
    MarketEventType type;
    TradeRecord trade;
    QuoteRecord quote;
};

struct UnifiedMarketEvent {
    Timestamp timestamp;
    UnifiedEventType type;
    TradeRecord trade;
    QuoteRecord quote;
    BarRecord bar;
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

    // Stream 1-second bars (for live_bar_aggr_source="1s").
    virtual void stream_second_bars(const std::vector<std::string>& symbols,
                                    Timestamp start_time,
                                    Timestamp end_time,
                                    const std::function<void(const BarRecord&)>& cb) = 0;

    // Chronological merged stream of trades+quotes+1s bars (for live_bar_aggr_source="1s").
    virtual void stream_events_with_bars(const std::vector<std::string>& symbols,
                                         Timestamp start_time,
                                         Timestamp end_time,
                                         const std::function<void(const UnifiedMarketEvent&)>& cb) = 0;

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
    virtual std::vector<StockSplitRecord> get_stock_splits(const StockSplitsQuery& query) = 0;
    virtual std::vector<StockNewsRecord> get_stock_news(const StockNewsQuery& query) = 0;
    virtual std::vector<StockNewsInsightRecord> get_stock_news_insights(const std::vector<std::string>& article_ids) = 0;
    virtual std::vector<StockTickerEventRecord> get_stock_ticker_events(const StockTickerEventsQuery& query) = 0;
    virtual std::optional<TickerBasicRecord> get_ticker_basic(const std::string& ticker,
                                                              std::optional<Timestamp> max_date) = 0;
    virtual std::vector<StockIpoRecord> get_stock_ipos(const StockIposQuery& query) = 0;
    virtual std::vector<StockShortInterestRecord> get_stock_short_interest(const StockShortInterestQuery& query) = 0;
    virtual std::vector<StockShortVolumeRecord> get_stock_short_volume(const StockShortVolumeQuery& query) = 0;
    virtual std::vector<FinancialsRecord> get_stock_financials(const FinancialsQuery& query) = 0;

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

    virtual std::vector<FinnhubIpoRecord> get_finnhub_ipo_calendar(Timestamp start_time,
                                                                   Timestamp end_time,
                                                                   size_t limit) = 0;

    virtual std::vector<CompanyNewsRecord> get_finnhub_market_news(Timestamp start_time,
                                                                   Timestamp end_time,
                                                                   size_t limit) = 0;

    virtual std::vector<FinnhubInsiderTransactionRecord> get_finnhub_insider_transactions(const std::string& symbol,
                                                                                           Timestamp start_time,
                                                                                           Timestamp end_time,
                                                                                           size_t limit) = 0;

    virtual std::vector<FinnhubSecFilingRecord> get_finnhub_sec_filings(const std::string& symbol,
                                                                        Timestamp start_time,
                                                                        Timestamp end_time,
                                                                        size_t limit) = 0;

    virtual std::vector<FinnhubCongressionalTradingRecord> get_finnhub_congressional_trading(const std::string& symbol,
                                                                                              Timestamp start_time,
                                                                                              Timestamp end_time,
                                                                                              size_t limit) = 0;

    virtual std::vector<FinnhubInsiderSentimentRecord> get_finnhub_insider_sentiment(const std::string& symbol,
                                                                                      Timestamp start_time,
                                                                                      Timestamp end_time,
                                                                                      size_t limit) = 0;

    virtual std::vector<FinnhubEpsEstimateRecord> get_finnhub_eps_estimates(const std::string& symbol,
                                                                            Timestamp start_time,
                                                                            Timestamp end_time,
                                                                            const std::string& freq,
                                                                            size_t limit) = 0;

    virtual std::vector<FinnhubRevenueEstimateRecord> get_finnhub_revenue_estimates(const std::string& symbol,
                                                                                    Timestamp start_time,
                                                                                    Timestamp end_time,
                                                                                    const std::string& freq,
                                                                                    size_t limit) = 0;

    virtual std::vector<FinnhubEarningsHistoryRecord> get_finnhub_earnings_history(const std::string& symbol,
                                                                                    Timestamp start_time,
                                                                                    Timestamp end_time,
                                                                                    size_t limit) = 0;

    virtual std::vector<FinnhubSocialSentimentRecord> get_finnhub_social_sentiment(const std::string& symbol,
                                                                                    Timestamp start_time,
                                                                                    Timestamp end_time,
                                                                                    size_t limit) = 0;

    virtual std::vector<FinnhubOwnershipRecord> get_finnhub_ownership(const std::string& symbol,
                                                                      Timestamp start_time,
                                                                      Timestamp end_time,
                                                                      size_t limit) = 0;

    virtual std::vector<FinnhubFinancialsStandardizedRecord> get_finnhub_financials_standardized(
        const std::string& symbol,
        const std::string& statement,
        const std::string& freq,
        Timestamp start_time,
        Timestamp end_time,
        size_t limit) = 0;

    virtual std::vector<FinnhubFinancialsReportedRecord> get_finnhub_financials_reported(
        const std::string& symbol,
        const std::string& freq,
        Timestamp start_time,
        Timestamp end_time,
        size_t limit) = 0;
};

} // namespace broker_sim
