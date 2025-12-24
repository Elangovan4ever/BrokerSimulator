#include "data_source_clickhouse.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace broker_sim {

ClickHouseDataSource::ClickHouseDataSource(const ClickHouseConfig& cfg)
    : cfg_(cfg) {}

ClickHouseDataSource::~ClickHouseDataSource() {
    disconnect();
}

void ClickHouseDataSource::connect() {
    clickhouse::ClientOptions opts;
    opts.SetHost(cfg_.host);
    opts.SetPort(cfg_.port);
    opts.SetDefaultDatabase(cfg_.database);
    opts.SetUser(cfg_.user);
    opts.SetPassword(cfg_.password);
    client_ = std::make_unique<clickhouse::Client>(opts);
    spdlog::info("Connected to ClickHouse {}:{} db={}", cfg_.host, cfg_.port, cfg_.database);
}

void ClickHouseDataSource::disconnect() {
    client_.reset();
}

void ClickHouseDataSource::stream_trades(const std::vector<std::string>& symbols,
                                         Timestamp start_time,
                                         Timestamp end_time,
                                         const std::function<void(const TradeRecord&)>& cb) {
    if (!client_) return;
    std::string sym_list = build_symbol_list(symbols);
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string query = fmt::format(R"(
        SELECT timestamp, symbol, toFloat64(price), toInt64(size), toInt32(exchange), conditions, toInt32(tape)
        FROM stock_trades
        WHERE symbol IN ({})
          AND timestamp >= '{}'
          AND timestamp < '{}'
        ORDER BY timestamp ASC
    )", sym_list, start_str, end_str);

    client_->Select(query, [&cb](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            TradeRecord tr;
            tr.timestamp = extract_ts(block[0], row);
            tr.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
            tr.price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
            tr.size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
            tr.exchange = block[4]->As<clickhouse::ColumnInt32>()->At(row);
            tr.conditions = block[5]->As<clickhouse::ColumnString>()->At(row);
            tr.tape = block[6]->As<clickhouse::ColumnInt32>()->At(row);
            cb(tr);
        }
    });
}

void ClickHouseDataSource::stream_quotes(const std::vector<std::string>& symbols,
                                         Timestamp start_time,
                                         Timestamp end_time,
                                         const std::function<void(const QuoteRecord&)>& cb) {
    if (!client_) return;
    std::string sym_list = build_symbol_list(symbols);
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string query = fmt::format(R"(
        SELECT sip_timestamp, symbol, toFloat64(bid_price), bid_size, toFloat64(ask_price), ask_size, bid_exchange, ask_exchange, tape
        FROM stock_quotes
        WHERE symbol IN ({})
          AND sip_timestamp >= '{}'
          AND sip_timestamp < '{}'
        ORDER BY sip_timestamp ASC
    )", sym_list, start_str, end_str);

    client_->Select(query, [&cb](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            QuoteRecord q;
            q.timestamp = extract_ts(block[0], row);
            q.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
            q.bid_price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
            q.bid_size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
            q.ask_price = block[4]->As<clickhouse::ColumnFloat64>()->At(row);
            q.ask_size = block[5]->As<clickhouse::ColumnInt64>()->At(row);
            q.bid_exchange = block[6]->As<clickhouse::ColumnInt32>()->At(row);
            q.ask_exchange = block[7]->As<clickhouse::ColumnInt32>()->At(row);
            q.tape = block[8]->As<clickhouse::ColumnInt32>()->At(row);
            cb(q);
        }
    });
}

void ClickHouseDataSource::stream_events(const std::vector<std::string>& symbols,
                                         Timestamp start_time,
                                         Timestamp end_time,
                                         const std::function<void(const MarketEvent&)>& cb) {
    if (!client_) return;
    std::string sym_list = build_symbol_list(symbols);
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // Union trades and quotes in chronological order.
    std::string query = fmt::format(R"(
        SELECT ts, symbol, kind, price, size, bid_price, bid_size, ask_price, ask_size, exchange, conditions, tape, bid_exch, ask_exch
        FROM (
            SELECT timestamp as ts,
                   symbol,
                   toUInt8(1) as kind,
                   toFloat64(price) as price,
                   toInt64(size) as size,
                   toFloat64(price) as bid_price,
                   toInt64(size) as bid_size,
                   toFloat64(price) as ask_price,
                   toInt64(size) as ask_size,
                   toInt32(exchange) as exchange,
                   conditions,
                   toInt32(tape) as tape,
                   toInt32(exchange) as bid_exch,
                   toInt32(exchange) as ask_exch
            FROM stock_trades
            WHERE symbol IN ({})
              AND timestamp >= '{}'
              AND timestamp < '{}'
            UNION ALL
            SELECT sip_timestamp as ts,
                   symbol,
                   toUInt8(0) as kind,
                   toFloat64(bid_price) as price,
                   toInt64(bid_size) as size,
                   toFloat64(bid_price) as bid_price,
                   toInt64(bid_size) as bid_size,
                   toFloat64(ask_price) as ask_price,
                   toInt64(ask_size) as ask_size,
                   toInt32(bid_exchange) as exchange,
                   '' as conditions,
                   toInt32(tape) as tape,
                   toInt32(bid_exchange) as bid_exch,
                   toInt32(ask_exchange) as ask_exch
            FROM stock_quotes
            WHERE symbol IN ({})
              AND sip_timestamp >= '{}'
              AND sip_timestamp < '{}'
        )
        ORDER BY ts ASC, kind ASC
    )", sym_list, start_str, end_str, sym_list, start_str, end_str);

    // Batch events to avoid blocking TCP read with slow callback processing.
    // This prevents ClickHouse send timeout from TCP backpressure.
    std::vector<MarketEvent> batch;
    batch.reserve(100000);  // Pre-allocate for typical batch size

    client_->Select(query, [&batch](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            MarketEvent ev;
            ev.timestamp = extract_ts(block[0], row);
            ev.trade.timestamp = ev.timestamp;
            ev.quote.timestamp = ev.timestamp;
            ev.trade.symbol = ev.quote.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
            auto kind = block[2]->As<clickhouse::ColumnUInt8>()->At(row);
            ev.type = (kind == 0) ? MarketEventType::QUOTE : MarketEventType::TRADE;
            double price = block[3]->As<clickhouse::ColumnFloat64>()->At(row);
            int64_t size = block[4]->As<clickhouse::ColumnInt64>()->At(row);
            double bid_price = block[5]->As<clickhouse::ColumnFloat64>()->At(row);
            int64_t bid_size = block[6]->As<clickhouse::ColumnInt64>()->At(row);
            double ask_price = block[7]->As<clickhouse::ColumnFloat64>()->At(row);
            int64_t ask_size = block[8]->As<clickhouse::ColumnInt64>()->At(row);
            int exchange = block[9]->As<clickhouse::ColumnInt32>()->At(row);
            auto cond_sv = block[10]->As<clickhouse::ColumnString>()->At(row);
            std::string conditions(cond_sv.data(), cond_sv.size());
            int tape = block[11]->As<clickhouse::ColumnInt32>()->At(row);
            int bid_exch = block[12]->As<clickhouse::ColumnInt32>()->At(row);
            int ask_exch = block[13]->As<clickhouse::ColumnInt32>()->At(row);

            if (ev.type == MarketEventType::TRADE) {
                ev.trade.price = price;
                ev.trade.size = size;
                ev.trade.exchange = exchange;
                ev.trade.conditions = conditions;
                ev.trade.tape = tape;
            } else {
                ev.quote.bid_price = bid_price;
                ev.quote.bid_size = bid_size;
                ev.quote.ask_price = ask_price;
                ev.quote.ask_size = ask_size;
                ev.quote.bid_exchange = bid_exch;
                ev.quote.ask_exchange = ask_exch;
                ev.quote.tape = tape;
            }
            batch.push_back(std::move(ev));
        }
    });

    // Process batched events after query completes (no TCP backpressure)
    for (const auto& ev : batch) {
        cb(ev);
    }
}

std::vector<TradeRecord> ClickHouseDataSource::get_trades(const std::string& symbol,
                                                          Timestamp start_time,
                                                          Timestamp end_time,
                                                          size_t limit) {
    std::vector<TradeRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
    std::string query = fmt::format(R"(
        SELECT timestamp, symbol, toFloat64(price), toInt64(size), toInt32(exchange), conditions, toInt32(tape)
        FROM stock_trades
        WHERE symbol = '{}'
          AND timestamp >= '{}'
          AND timestamp < '{}'
        ORDER BY timestamp ASC
        {}
    )", symbol, start_str, end_str, limit_clause);

    client_->Select(query, [&out](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            TradeRecord tr;
            tr.timestamp = extract_ts(block[0], row);
            tr.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
            tr.price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
            tr.size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
            tr.exchange = block[4]->As<clickhouse::ColumnInt32>()->At(row);
            tr.conditions = block[5]->As<clickhouse::ColumnString>()->At(row);
            tr.tape = block[6]->As<clickhouse::ColumnInt32>()->At(row);
            out.push_back(std::move(tr));
        }
    });
    return out;
}

std::vector<QuoteRecord> ClickHouseDataSource::get_quotes(const std::string& symbol,
                                                          Timestamp start_time,
                                                          Timestamp end_time,
                                                          size_t limit) {
    std::vector<QuoteRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
    std::string query = fmt::format(R"(
        SELECT sip_timestamp, symbol, toFloat64(bid_price), toInt64(bid_size), toFloat64(ask_price), toInt64(ask_size), toInt32(bid_exchange), toInt32(ask_exchange), toInt32(tape)
        FROM stock_quotes
        WHERE symbol = '{}'
          AND sip_timestamp >= '{}'
          AND sip_timestamp < '{}'
        ORDER BY sip_timestamp ASC
        {}
    )", symbol, start_str, end_str, limit_clause);

    client_->Select(query, [&out](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            QuoteRecord q;
            q.timestamp = extract_ts(block[0], row);
            q.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
            q.bid_price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
            q.bid_size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
            q.ask_price = block[4]->As<clickhouse::ColumnFloat64>()->At(row);
            q.ask_size = block[5]->As<clickhouse::ColumnInt64>()->At(row);
            q.bid_exchange = block[6]->As<clickhouse::ColumnInt32>()->At(row);
            q.ask_exchange = block[7]->As<clickhouse::ColumnInt32>()->At(row);
            q.tape = block[8]->As<clickhouse::ColumnInt32>()->At(row);
            out.push_back(std::move(q));
        }
    });
    return out;
}

std::vector<BarRecord> ClickHouseDataSource::get_bars(const std::string& symbol,
                                                      Timestamp start_time,
                                                      Timestamp end_time,
                                                      int multiplier,
                                                      const std::string& timespan,
                                                      size_t limit) {
    std::vector<BarRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    auto interval = interval_expr(multiplier, timespan);
    std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
    std::string query = fmt::format(R"(
        SELECT
            toStartOfInterval(timestamp, {}) AS bucket,
            argMin(price, timestamp) AS open,
            max(price) AS high,
            min(price) AS low,
            argMax(price, timestamp) AS close,
            sum(size) AS volume,
            if(sum(size) = 0, 0, sum(price * size) / sum(size)) AS vwap,
            count() AS trade_count
        FROM stock_trades
        WHERE symbol = '{}'
          AND timestamp >= '{}'
          AND timestamp < '{}'
        GROUP BY bucket
        ORDER BY bucket ASC
        {}
    )", interval, symbol, start_str, end_str, limit_clause);

    client_->Select(query, [&out, &symbol](const clickhouse::Block& block) {
        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            BarRecord b;
            b.timestamp = extract_ts(block[0], row);
            b.symbol = symbol;
            b.open = block[1]->As<clickhouse::ColumnFloat64>()->At(row);
            b.high = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
            b.low = block[3]->As<clickhouse::ColumnFloat64>()->At(row);
            b.close = block[4]->As<clickhouse::ColumnFloat64>()->At(row);
            b.volume = block[5]->As<clickhouse::ColumnInt64>()->At(row);
            b.vwap = block[6]->As<clickhouse::ColumnFloat64>()->At(row);
            b.trade_count = block[7]->As<clickhouse::ColumnUInt64>()->At(row);
            out.push_back(std::move(b));
        }
    });
    return out;
}

std::vector<CompanyNewsRecord> ClickHouseDataSource::get_company_news(const std::string& symbol,
                                                                      Timestamp start_time,
                                                                      Timestamp end_time,
                                                                      size_t limit) {
    std::vector<CompanyNewsRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // finnhub_company_news: category is LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT datetime, headline, summary, source_name, url, image, CAST(category AS String), related, id
        FROM finnhub_company_news
        WHERE symbol = '{}'
          AND datetime >= '{}'
          AND datetime < '{}'
        ORDER BY datetime DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                CompanyNewsRecord n;
                n.datetime = extract_ts(block[0], row);
                n.headline = block[1]->As<clickhouse::ColumnString>()->At(row);
                n.summary = block[2]->As<clickhouse::ColumnString>()->At(row);
                n.source = block[3]->As<clickhouse::ColumnString>()->At(row);
                n.url = block[4]->As<clickhouse::ColumnString>()->At(row);
                n.image = block[5]->As<clickhouse::ColumnString>()->At(row);
                n.category = block[6]->As<clickhouse::ColumnString>()->At(row);
                n.related = block[7]->As<clickhouse::ColumnString>()->At(row);
                n.id = block[8]->As<clickhouse::ColumnInt64>()->At(row);
                out.push_back(std::move(n));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_company_news failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::optional<CompanyProfileRecord> ClickHouseDataSource::get_company_profile(const std::string& symbol) {
    if (!client_) return std::nullopt;
    // LowCardinality(String) columns need CAST(... AS String) for clickhouse-cpp
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), name, exchange, industry, ipo,
               toFloat64(market_capitalization) as market_cap,
               toFloat64(share_outstanding) as shares,
               CAST(country AS String), CAST(currency AS String), CAST(estimate_currency AS String),
               weburl, logo, phone, raw_json
        FROM finnhub_company_profiles
        WHERE symbol = '{}'
        ORDER BY inserted_at DESC
        LIMIT 1
    )", symbol);
    std::optional<CompanyProfileRecord> out;
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            if (block.GetRowCount() == 0) return;
            CompanyProfileRecord p;
            p.symbol = block[0]->As<clickhouse::ColumnString>()->At(0);
            p.name = block[1]->As<clickhouse::ColumnString>()->At(0);
            p.exchange = block[2]->As<clickhouse::ColumnString>()->At(0);
            p.industry = block[3]->As<clickhouse::ColumnString>()->At(0);
            p.ipo = extract_ts_any(block[4], 0);
            if (auto v = get_nullable_float(block[5], 0)) p.market_capitalization = *v;
            if (auto v = get_nullable_float(block[6], 0)) p.share_outstanding = *v;
            p.country = block[7]->As<clickhouse::ColumnString>()->At(0);
            p.currency = block[8]->As<clickhouse::ColumnString>()->At(0);
            p.estimate_currency = block[9]->As<clickhouse::ColumnString>()->At(0);
            p.weburl = block[10]->As<clickhouse::ColumnString>()->At(0);
            p.logo = block[11]->As<clickhouse::ColumnString>()->At(0);
            p.phone = block[12]->As<clickhouse::ColumnString>()->At(0);
            p.raw_json = block[13]->As<clickhouse::ColumnString>()->At(0);
            out = std::move(p);
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_company_profile failed: {}", e.what());
    }
    return out;
}

std::vector<std::string> ClickHouseDataSource::get_company_peers(const std::string& symbol,
                                                                  size_t limit) {
    std::vector<std::string> out;
    if (!client_) return out;
    // peer is LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT CAST(peer AS String)
        FROM finnhub_company_peers
        WHERE symbol = '{}'
        ORDER BY inserted_at DESC
        {}
    )", symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                auto sv = block[0]->As<clickhouse::ColumnString>()->At(row);
                out.emplace_back(sv.data(), sv.size());
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_company_peers failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::optional<NewsSentimentRecord> ClickHouseDataSource::get_news_sentiment(const std::string& symbol) {
    if (!client_) return std::nullopt;
    // symbol is LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), articles_in_last_week, buzz, weekly_average, company_news_score,
               sector_average_bullish_percent, sector_average_news_score, bullish_percent,
               bearish_percent, raw_json
        FROM finnhub_news_sentiment
        WHERE symbol = '{}'
        ORDER BY inserted_at DESC
        LIMIT 1
    )", symbol);
    std::optional<NewsSentimentRecord> out;
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            if (block.GetRowCount() == 0) return;
            NewsSentimentRecord s;
            s.symbol = block[0]->As<clickhouse::ColumnString>()->At(0);
            if (auto v = get_nullable_uint32(block[1], 0)) s.articles_in_last_week = *v;
            if (auto v = get_nullable_float(block[2], 0)) s.buzz = *v;
            if (auto v = get_nullable_float(block[3], 0)) s.weekly_average = *v;
            if (auto v = get_nullable_float(block[4], 0)) s.company_news_score = *v;
            if (auto v = get_nullable_float(block[5], 0)) s.sector_average_bullish_percent = *v;
            if (auto v = get_nullable_float(block[6], 0)) s.sector_average_news_score = *v;
            if (auto v = get_nullable_float(block[7], 0)) s.bullish_percent = *v;
            if (auto v = get_nullable_float(block[8], 0)) s.bearish_percent = *v;
            s.raw_json = block[9]->As<clickhouse::ColumnString>()->At(0);
            out = std::move(s);
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_news_sentiment failed: {}", e.what());
    }
    return out;
}

std::optional<BasicFinancialsRecord> ClickHouseDataSource::get_basic_financials(const std::string& symbol) {
    if (!client_) return std::nullopt;
    // symbol is LowCardinality(String), market_capitalization is Decimal
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), metric_date, toFloat64(market_capitalization), pe_ttm, forward_pe, pb,
               dividend_yield_ttm, revenue_per_share_ttm, eps_ttm, free_cash_flow_per_share_ttm,
               beta, fifty_two_week_high, fifty_two_week_low, raw_json
        FROM finnhub_basic_financials
        WHERE symbol = '{}'
        ORDER BY inserted_at DESC
        LIMIT 1
    )", symbol);
    std::optional<BasicFinancialsRecord> out;
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            if (block.GetRowCount() == 0) return;
            BasicFinancialsRecord b;
            b.symbol = block[0]->As<clickhouse::ColumnString>()->At(0);
            b.metric_date = extract_ts_any(block[1], 0);
            if (auto v = get_nullable_float(block[2], 0)) b.market_capitalization = *v;
            if (auto v = get_nullable_float(block[3], 0)) b.pe_ttm = *v;
            if (auto v = get_nullable_float(block[4], 0)) b.forward_pe = *v;
            if (auto v = get_nullable_float(block[5], 0)) b.pb = *v;
            if (auto v = get_nullable_float(block[6], 0)) b.dividend_yield_ttm = *v;
            if (auto v = get_nullable_float(block[7], 0)) b.revenue_per_share_ttm = *v;
            if (auto v = get_nullable_float(block[8], 0)) b.eps_ttm = *v;
            if (auto v = get_nullable_float(block[9], 0)) b.free_cash_flow_per_share_ttm = *v;
            if (auto v = get_nullable_float(block[10], 0)) b.beta = *v;
            if (auto v = get_nullable_float(block[11], 0)) b.fifty_two_week_high = *v;
            if (auto v = get_nullable_float(block[12], 0)) b.fifty_two_week_low = *v;
            b.raw_json = block[13]->As<clickhouse::ColumnString>()->At(0);
            out = std::move(b);
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_basic_financials failed: {}", e.what());
    }
    return out;
}

std::vector<DividendRecord> ClickHouseDataSource::get_dividends(const std::string& symbol,
                                                                Timestamp start_time,
                                                                Timestamp end_time,
                                                                size_t limit) {
    std::vector<DividendRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // finnhub_dividends: symbol, currency are LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String),
               toDateTime(ex_date) as dt,
               toFloat64(amount) as amount,
               toFloat64(amount) as adjusted_amount,
               toDateTime(payment_date) as pay_dt,
               toDateTime(record_date) as record_dt,
               toDateTime(declared_date) as decl_dt,
               CAST(currency AS String)
        FROM finnhub_dividends
        WHERE symbol = '{}'
          AND ex_date >= toDate('{}')
          AND ex_date < toDate('{}')
        ORDER BY ex_date DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                DividendRecord d;
                d.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                d.date = extract_ts(block[1], row);
                d.amount = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                d.adjusted_amount = block[3]->As<clickhouse::ColumnFloat64>()->At(row);
                d.pay_date = extract_ts(block[4], row);
                d.record_date = extract_ts(block[5], row);
                d.declaration_date = extract_ts(block[6], row);
                d.currency = block[7]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(d));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_dividends failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<SplitRecord> ClickHouseDataSource::get_splits(const std::string& symbol,
                                                          Timestamp start_time,
                                                          Timestamp end_time,
                                                          size_t limit) {
    std::vector<SplitRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // Use stock_splits table from Polygon data
    std::string query = fmt::format(R"(
        SELECT ticker,
               toDateTime(execution_date) as dt,
               toFloat64(split_from) as from_factor,
               toFloat64(split_to) as to_factor
        FROM stock_splits
        WHERE ticker = '{}'
          AND execution_date >= toDate('{}')
          AND execution_date < toDate('{}')
        ORDER BY execution_date DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                SplitRecord s;
                s.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                s.date = extract_ts(block[1], row);
                s.from_factor = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                s.to_factor = block[3]->As<clickhouse::ColumnFloat64>()->At(row);
                out.push_back(std::move(s));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_splits failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<EarningsCalendarRecord> ClickHouseDataSource::get_earnings_calendar(const std::string& symbol,
                                                                                Timestamp start_time,
                                                                                Timestamp end_time,
                                                                                size_t limit) {
    std::vector<EarningsCalendarRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // symbol, hour are LowCardinality(String); eps/revenue are Decimal
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), date, quarter, year,
               toFloat64(eps_estimate), toFloat64(eps_actual),
               toFloat64(revenue_estimate), toFloat64(revenue_actual),
               CAST(hour AS String)
        FROM finnhub_earnings_calendar
        WHERE symbol = '{}'
          AND date >= toDate('{}')
          AND date < toDate('{}')
        ORDER BY date DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                EarningsCalendarRecord e;
                e.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                e.date = extract_ts_any(block[1], row);
                if (auto v = get_nullable_uint16(block[2], row)) e.quarter = *v;
                if (auto v = get_nullable_uint16(block[3], row)) e.year = *v;
                if (auto v = get_nullable_float(block[4], row)) e.eps_estimate = *v;
                if (auto v = get_nullable_float(block[5], row)) e.eps_actual = *v;
                if (auto v = get_nullable_float(block[6], row)) e.revenue_estimate = *v;
                if (auto v = get_nullable_float(block[7], row)) e.revenue_actual = *v;
                e.hour = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(e));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_earnings_calendar failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<RecommendationRecord> ClickHouseDataSource::get_recommendation_trends(const std::string& symbol,
                                                                                  Timestamp start_time,
                                                                                  Timestamp end_time,
                                                                                  size_t limit) {
    std::vector<RecommendationRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // symbol is LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), period, strong_buy, buy, hold, sell, strong_sell
        FROM finnhub_recommendation_trends
        WHERE symbol = '{}'
          AND period >= toDate('{}')
          AND period < toDate('{}')
        ORDER BY period DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                RecommendationRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[1], row);
                r.strong_buy = block[2]->As<clickhouse::ColumnUInt16>()->At(row);
                r.buy = block[3]->As<clickhouse::ColumnUInt16>()->At(row);
                r.hold = block[4]->As<clickhouse::ColumnUInt16>()->At(row);
                r.sell = block[5]->As<clickhouse::ColumnUInt16>()->At(row);
                r.strong_sell = block[6]->As<clickhouse::ColumnUInt16>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_recommendation_trends failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::optional<PriceTargetRecord> ClickHouseDataSource::get_price_targets(const std::string& symbol) {
    if (!client_) return std::nullopt;
    // symbol is LowCardinality(String); target fields are Decimal
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), last_updated, number_analysts,
               toFloat64(target_high), toFloat64(target_low),
               toFloat64(target_mean), toFloat64(target_median)
        FROM finnhub_price_targets
        WHERE symbol = '{}'
        ORDER BY inserted_at DESC
        LIMIT 1
    )", symbol);
    std::optional<PriceTargetRecord> out;
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            if (block.GetRowCount() == 0) return;
            PriceTargetRecord p;
            p.symbol = block[0]->As<clickhouse::ColumnString>()->At(0);
            p.last_updated = extract_ts_any(block[1], 0);
            if (auto v = get_nullable_uint16(block[2], 0)) p.number_analysts = *v;
            if (auto v = get_nullable_float(block[3], 0)) p.target_high = *v;
            if (auto v = get_nullable_float(block[4], 0)) p.target_low = *v;
            if (auto v = get_nullable_float(block[5], 0)) p.target_mean = *v;
            if (auto v = get_nullable_float(block[6], 0)) p.target_median = *v;
            out = std::move(p);
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_price_targets failed: {}", e.what());
    }
    return out;
}

std::vector<UpgradeDowngradeRecord> ClickHouseDataSource::get_upgrades_downgrades(const std::string& symbol,
                                                                                 Timestamp start_time,
                                                                                 Timestamp end_time,
                                                                                 size_t limit) {
    std::vector<UpgradeDowngradeRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // symbol, from_grade, to_grade, action are LowCardinality(String)
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), grade_time, company,
               CAST(from_grade AS String), CAST(to_grade AS String), CAST(action AS String)
        FROM finnhub_upgrades_downgrades
        WHERE symbol = '{}'
          AND grade_time >= toDateTime('{}')
          AND grade_time < toDateTime('{}')
        ORDER BY grade_time DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                UpgradeDowngradeRecord u;
                u.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                u.grade_time = extract_ts_any(block[1], row);
                u.company = block[2]->As<clickhouse::ColumnString>()->At(row);
                u.from_grade = block[3]->As<clickhouse::ColumnString>()->At(row);
                u.to_grade = block[4]->As<clickhouse::ColumnString>()->At(row);
                u.action = block[5]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(u));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_upgrades_downgrades failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::string ClickHouseDataSource::build_symbol_list(const std::vector<std::string>& symbols) {
    std::string out;
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) out += ", ";
        out += "'" + symbols[i] + "'";
    }
    return out;
}

std::string ClickHouseDataSource::format_timestamp(Timestamp ts) {
    auto tt = std::chrono::system_clock::to_time_t(ts);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&tt), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

Timestamp ClickHouseDataSource::extract_ts(const clickhouse::ColumnRef& col, size_t row) {
    auto c = col->As<clickhouse::ColumnDateTime64>();
    auto micros = c->At(row);
    return Timestamp{} + std::chrono::microseconds(micros);
}

std::string ClickHouseDataSource::interval_expr(int multiplier, const std::string& timespan) {
    int mult = std::max(1, multiplier);
    std::string unit;
    if (timespan == "second" || timespan == "sec" || timespan == "s") {
        unit = "second";
    } else if (timespan == "minute" || timespan == "min" || timespan == "m") {
        unit = "minute";
    } else if (timespan == "hour" || timespan == "h") {
        unit = "hour";
    } else if (timespan == "day" || timespan == "d") {
        unit = "day";
    } else if (timespan == "week" || timespan == "w") {
        unit = "week";
    } else if (timespan == "month" || timespan == "M") {
        unit = "month";
    } else {
        unit = "minute";
    }
    return fmt::format("INTERVAL {} {}", mult, unit);
}

std::string ClickHouseDataSource::limit_clause(size_t limit) {
    if (limit == 0) return "";
    return fmt::format(" LIMIT {}", limit);
}

Timestamp ClickHouseDataSource::extract_ts_any(const clickhouse::ColumnRef& col, size_t row) {
    if (auto n = col->As<clickhouse::ColumnNullable>()) {
        if (n->IsNull(row)) return Timestamp{};
        return extract_ts_any(n->Nested(), row);
    }
    if (auto c = col->As<clickhouse::ColumnDateTime64>()) {
        auto micros = c->At(row);
        return Timestamp{} + std::chrono::microseconds(micros);
    }
    if (auto c = col->As<clickhouse::ColumnDateTime>()) {
        auto secs = c->At(row);
        return Timestamp{} + std::chrono::seconds(secs);
    }
    if (auto c = col->As<clickhouse::ColumnDate>()) {
        auto days = c->At(row);
        return Timestamp{} + std::chrono::hours(24 * days);
    }
    return Timestamp{};
}

std::optional<double> ClickHouseDataSource::get_nullable_float(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto f = nested->As<clickhouse::ColumnFloat64>()) return f->At(row);
        if (auto f = nested->As<clickhouse::ColumnFloat32>()) return f->At(row);
        if (auto d = nested->As<clickhouse::ColumnDecimal>()) {
            auto scale = static_cast<double>(d->GetScale());
            auto raw = d->At(row);
            double denom = std::pow(10.0, scale);
            return static_cast<double>(raw) / denom;
        }
    }
    if (auto f = col->As<clickhouse::ColumnFloat64>()) return f->At(row);
    if (auto f = col->As<clickhouse::ColumnFloat32>()) return f->At(row);
    if (auto d = col->As<clickhouse::ColumnDecimal>()) {
        auto scale = static_cast<double>(d->GetScale());
        auto raw = d->At(row);
        double denom = std::pow(10.0, scale);
        return static_cast<double>(raw) / denom;
    }
    return std::nullopt;
}

std::optional<uint32_t> ClickHouseDataSource::get_nullable_uint32(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto v = nested->As<clickhouse::ColumnUInt32>()) return v->At(row);
        if (auto v = nested->As<clickhouse::ColumnUInt16>()) return v->At(row);
        if (auto v = nested->As<clickhouse::ColumnUInt8>()) return v->At(row);
    }
    if (auto v = col->As<clickhouse::ColumnUInt32>()) return v->At(row);
    if (auto v = col->As<clickhouse::ColumnUInt16>()) return v->At(row);
    if (auto v = col->As<clickhouse::ColumnUInt8>()) return v->At(row);
    return std::nullopt;
}

std::optional<uint16_t> ClickHouseDataSource::get_nullable_uint16(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto v = nested->As<clickhouse::ColumnUInt16>()) return v->At(row);
        if (auto v = nested->As<clickhouse::ColumnUInt8>()) return v->At(row);
    }
    if (auto v = col->As<clickhouse::ColumnUInt16>()) return v->At(row);
    if (auto v = col->As<clickhouse::ColumnUInt8>()) return v->At(row);
    return std::nullopt;
}

std::optional<int> ClickHouseDataSource::get_nullable_int32(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto v = nested->As<clickhouse::ColumnInt32>()) return v->At(row);
        if (auto v = nested->As<clickhouse::ColumnInt64>()) return static_cast<int>(v->At(row));
    }
    if (auto v = col->As<clickhouse::ColumnInt32>()) return v->At(row);
    if (auto v = col->As<clickhouse::ColumnInt64>()) return static_cast<int>(v->At(row));
    return std::nullopt;
}

std::optional<std::string> ClickHouseDataSource::get_nullable_string(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto v = nested->As<clickhouse::ColumnString>()) {
            auto sv = v->At(row);
            return std::string(sv.data(), sv.size());
        }
    }
    if (auto v = col->As<clickhouse::ColumnString>()) {
        auto sv = v->At(row);
        return std::string(sv.data(), sv.size());
    }
    return std::nullopt;
}

} // namespace broker_sim
