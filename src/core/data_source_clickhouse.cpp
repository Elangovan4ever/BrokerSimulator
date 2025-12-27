#include "data_source_clickhouse.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

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
    // Set longer timeouts for large result sets
    opts.SetSendRetries(3);
    opts.SetRetryTimeout(std::chrono::seconds(30));
    opts.SetCompressionMethod(clickhouse::CompressionMethod::LZ4);
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
    std::lock_guard<std::mutex> lock(client_mutex_);
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
    std::lock_guard<std::mutex> lock(client_mutex_);
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
    // Note: No mutex lock here - stream_events is called once at session start
    // and batches all events locally before returning. The session loop doesn't
    // call this concurrently with other methods.
    // Reconnect if client is null or stale
    if (!client_) {
        spdlog::info("ClickHouse client not connected, reconnecting...");
        connect();
    }
    std::string sym_list = build_symbol_list(symbols);
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    // Union trades and quotes in chronological order.
    std::string query = fmt::format(R"(
        SELECT ts, symbol, kind, price, size, bid_price, bid_size, ask_price, ask_size, exchange, conditions, tape, bid_exch, ask_exch
        FROM (
            SELECT timestamp as ts,
                   CAST(symbol AS String) as symbol,
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
                   CAST(symbol AS String) as symbol,
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
    batch.reserve(1000000);  // Pre-allocate for large result sets (1M events)

    spdlog::info("Starting ClickHouse query for {} symbols, {} to {}", symbols.size(), start_str, end_str);
    auto query_start = std::chrono::steady_clock::now();

    // Execute query with auto-reconnect on network errors
    auto execute_query = [&]() {
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
    };

    try {
        execute_query();
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse query failed: {}, reconnecting and retrying...", e.what());
        batch.clear();
        connect();  // Reconnect
        execute_query();  // Retry once
    }

    auto query_end = std::chrono::steady_clock::now();
    auto query_ms = std::chrono::duration_cast<std::chrono::milliseconds>(query_end - query_start).count();
    spdlog::info("ClickHouse query completed: {} events in {}ms", batch.size(), query_ms);

    // Process batched events after query completes (no TCP backpressure)
    auto process_start = std::chrono::steady_clock::now();
    for (const auto& ev : batch) {
        cb(ev);
    }
    auto process_end = std::chrono::steady_clock::now();
    auto process_ms = std::chrono::duration_cast<std::chrono::milliseconds>(process_end - process_start).count();
    spdlog::info("Event processing completed: {} events in {}ms", batch.size(), process_ms);
}

std::vector<TradeRecord> ClickHouseDataSource::get_trades(const std::string& symbol,
                                                          Timestamp start_time,
                                                          Timestamp end_time,
                                                          size_t limit) {
    std::vector<TradeRecord> out;
    // Create a new connection for API requests to avoid sharing client with session loop
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);
        std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
        std::string query = fmt::format(R"(
            SELECT
                toDateTime64(timestamp, 9) AS ts,
                CAST(symbol AS String) AS symbol,
                toFloat64(price) AS price,
                toInt64(size) AS size,
                toInt32OrZero(toString(exchange)) AS exchange,
                toString(conditions) AS conditions,
                toInt32OrZero(toString(tape)) AS tape
            FROM stock_trades
            WHERE symbol = '{}'
              AND timestamp >= '{}'
              AND timestamp < '{}'
            ORDER BY timestamp ASC
            {}
        )", symbol, start_str, end_str, limit_clause);

        client.Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                TradeRecord tr;
                tr.timestamp = extract_ts_any(block[0], row);
                tr.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
                tr.price = block[2]->As<clickhouse::ColumnFloat64>()->At(row);
                tr.size = block[3]->As<clickhouse::ColumnInt64>()->At(row);
                tr.exchange = block[4]->As<clickhouse::ColumnInt32>()->At(row);
                tr.conditions = block[5]->As<clickhouse::ColumnString>()->At(row);
                tr.tape = block[6]->As<clickhouse::ColumnInt32>()->At(row);
                out.push_back(std::move(tr));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("get_trades failed: {}", e.what());
    }
    return out;
}

std::vector<QuoteRecord> ClickHouseDataSource::get_quotes(const std::string& symbol,
                                                          Timestamp start_time,
                                                          Timestamp end_time,
                                                          size_t limit) {
    std::vector<QuoteRecord> out;
    // Create a new connection for API requests to avoid sharing client with session loop
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);
        std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
        // CAST symbol to String to handle LowCardinality(String) column
        std::string query = fmt::format(R"(
            SELECT sip_timestamp, CAST(symbol AS String), toFloat64(bid_price), toInt64(bid_size), toFloat64(ask_price), toInt64(ask_size), toInt32(bid_exchange), toInt32(ask_exchange), toInt32(tape)
            FROM stock_quotes
            WHERE symbol = '{}'
              AND sip_timestamp >= '{}'
              AND sip_timestamp < '{}'
            ORDER BY sip_timestamp ASC
            {}
        )", symbol, start_str, end_str, limit_clause);

        client.Select(query, [&out](const clickhouse::Block& block) {
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
    } catch (const std::exception& e) {
        spdlog::warn("get_quotes failed: {}", e.what());
    }
    return out;
}

std::vector<BarRecord> ClickHouseDataSource::get_bars(const std::string& symbol,
                                                      Timestamp start_time,
                                                      Timestamp end_time,
                                                      int multiplier,
                                                      const std::string& timespan,
                                                      size_t limit) {
    std::vector<BarRecord> out;
    // Create a new connection for API requests to avoid sharing client with session loop
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        auto start_str = format_timestamp(start_time);
        auto end_str = format_timestamp(end_time);
        auto interval = interval_expr(multiplier, timespan);
        std::string limit_clause = limit > 0 ? fmt::format(" LIMIT {}", limit) : "";
        std::string query = fmt::format(R"(
            SELECT
                toDateTime64(toStartOfInterval(timestamp, {}), 9) AS bucket,
                toFloat64(argMin(price, timestamp)) AS open,
                toFloat64(max(price)) AS high,
                toFloat64(min(price)) AS low,
                toFloat64(argMax(price, timestamp)) AS close,
                toInt64(sum(size)) AS volume,
                toFloat64(if(sum(size) = 0, 0, sum(price * size) / sum(size))) AS vwap,
                toUInt64(count()) AS trade_count
            FROM stock_trades
            WHERE symbol = '{}'
              AND timestamp >= '{}'
              AND timestamp < '{}'
            GROUP BY bucket
            ORDER BY bucket ASC
            {}
        )", interval, symbol, start_str, end_str, limit_clause);

        client.Select(query, [&out, &symbol](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                BarRecord b;
                b.open = 0.0;
                b.high = 0.0;
                b.low = 0.0;
                b.close = 0.0;
                b.volume = 0;
                b.vwap = 0.0;
                b.trade_count = 0;
                b.timestamp = extract_ts_any(block[0], row);
                b.symbol = symbol;
                if (auto v = get_nullable_float(block[1], row)) b.open = *v;
                if (auto v = get_nullable_float(block[2], row)) b.high = *v;
                if (auto v = get_nullable_float(block[3], row)) b.low = *v;
                if (auto v = get_nullable_float(block[4], row)) b.close = *v;
                b.volume = block[5]->As<clickhouse::ColumnInt64>()->At(row);
                if (auto v = get_nullable_float(block[6], row)) b.vwap = *v;
                b.trade_count = block[7]->As<clickhouse::ColumnUInt64>()->At(row);
                out.push_back(std::move(b));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("get_bars failed: {}", e.what());
    }
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
        SELECT datetime, headline, summary, source_name, url, image, CAST(category AS String), related, id, raw_json
        FROM finnhub_company_news
        WHERE symbol = '{}'
          AND datetime >= '{}'
          AND datetime < '{}'
        ORDER BY datetime DESC
        {}
    )", symbol, start_str, end_str, limit_clause(limit));
    auto run_select = [&]() {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                CompanyNewsRecord n;
                n.datetime = extract_ts_any(block[0], row);
                n.headline = block[1]->As<clickhouse::ColumnString>()->At(row);
                n.summary = block[2]->As<clickhouse::ColumnString>()->At(row);
                n.source = block[3]->As<clickhouse::ColumnString>()->At(row);
                n.url = block[4]->As<clickhouse::ColumnString>()->At(row);
                n.image = block[5]->As<clickhouse::ColumnString>()->At(row);
                n.category = block[6]->As<clickhouse::ColumnString>()->At(row);
                n.related = block[7]->As<clickhouse::ColumnString>()->At(row);
                n.id = block[8]->As<clickhouse::ColumnUInt64>()->At(row);
                n.raw_json = block[9]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(n));
            }
        });
    };
    try {
        run_select();
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_company_news failed: {}, reconnecting...", e.what());
        out.clear();
        connect();
        try {
            run_select();
        } catch (const std::exception& retry_e) {
            spdlog::warn("ClickHouse get_company_news retry failed: {}", retry_e.what());
            out.clear();
        }
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
               CAST(currency AS String),
               raw_json
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
                d.date = extract_ts_any(block[1], row);
                if (auto v = get_nullable_float(block[2], row)) d.amount = *v;
                if (auto v = get_nullable_float(block[3], row)) d.adjusted_amount = *v;
                d.pay_date = extract_ts_any(block[4], row);
                d.record_date = extract_ts_any(block[5], row);
                d.declaration_date = extract_ts_any(block[6], row);
                d.currency = block[7]->As<clickhouse::ColumnString>()->At(row);
                d.raw_json = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(d));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_dividends failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<DividendRecord> ClickHouseDataSource::get_stock_dividends(const StockDividendsQuery& query) {
    std::vector<DividendRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value, const char* op) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} {} '{}'", col, op, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };
        auto add_double = [&where](const std::string& col, const std::optional<double>& value, const char* op) {
            if (!value) return;
            where.push_back(fmt::format("toFloat64({}) {} {}", col, op, *value));
        };

        add_str("ticker", query.ticker, "=");
        add_str("ticker", query.ticker_gt, ">");
        add_str("ticker", query.ticker_gte, ">=");
        add_str("ticker", query.ticker_lt, "<");
        add_str("ticker", query.ticker_lte, "<=");

        add_date("ex_dividend_date", query.ex_dividend_date, "=");
        add_date("ex_dividend_date", query.ex_dividend_date_gt, ">");
        add_date("ex_dividend_date", query.ex_dividend_date_gte, ">=");
        add_date("ex_dividend_date", query.ex_dividend_date_lt, "<");
        add_date("ex_dividend_date", query.ex_dividend_date_lte, "<=");

        add_date("record_date", query.record_date, "=");
        add_date("record_date", query.record_date_gt, ">");
        add_date("record_date", query.record_date_gte, ">=");
        add_date("record_date", query.record_date_lt, "<");
        add_date("record_date", query.record_date_lte, "<=");

        add_date("declaration_date", query.declaration_date, "=");
        add_date("declaration_date", query.declaration_date_gt, ">");
        add_date("declaration_date", query.declaration_date_gte, ">=");
        add_date("declaration_date", query.declaration_date_lt, "<");
        add_date("declaration_date", query.declaration_date_lte, "<=");

        add_date("pay_date", query.pay_date, "=");
        add_date("pay_date", query.pay_date_gt, ">");
        add_date("pay_date", query.pay_date_gte, ">=");
        add_date("pay_date", query.pay_date_lt, "<");
        add_date("pay_date", query.pay_date_lte, "<=");

        add_double("cash_amount", query.cash_amount, "=");
        add_double("cash_amount", query.cash_amount_gt, ">");
        add_double("cash_amount", query.cash_amount_gte, ">=");
        add_double("cash_amount", query.cash_amount_lt, "<");
        add_double("cash_amount", query.cash_amount_lte, "<=");

        if (query.frequency) {
            where.push_back(fmt::format("frequency = {}", *query.frequency));
        }
        if (query.dividend_type && !query.dividend_type->empty()) {
            where.push_back(fmt::format("dividend_type = '{}'", *query.dividend_type));
        }
        if (query.max_ex_dividend_date) {
            auto ts = format_timestamp(*query.max_ex_dividend_date);
            where.push_back(fmt::format("ex_dividend_date <= toDate('{}')", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "ex_dividend_date";
        if (query.sort == "pay_date") sort_col = "pay_date";
        else if (query.sort == "declaration_date") sort_col = "declaration_date";
        else if (query.sort == "record_date") sort_col = "record_date";
        else if (query.sort == "cash_amount") sort_col = "cash_amount";
        else if (query.sort == "ticker") sort_col = "ticker";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";

        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(id AS String),
                   CAST(ticker AS String),
                   cash_amount,
                   CAST(currency AS String),
                   CAST(dividend_type AS String),
                   frequency,
                   toDateTime(ex_dividend_date) AS ex_dividend_date,
                   toDateTime(declaration_date) AS declaration_date,
                   toDateTime(record_date) AS record_date,
                   toDateTime(pay_date) AS pay_date
            FROM stock_dividends
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                DividendRecord d;
                d.id = block[0]->As<clickhouse::ColumnString>()->At(row);
                d.symbol = block[1]->As<clickhouse::ColumnString>()->At(row);
                if (auto v = get_nullable_float(block[2], row)) {
                    d.amount = *v;
                    d.adjusted_amount = *v;
                }
                if (auto v = get_nullable_string(block[3], row)) d.currency = *v;
                if (auto v = get_nullable_string(block[4], row)) d.dividend_type = *v;
                if (auto v = get_nullable_uint16(block[5], row)) d.frequency = static_cast<int>(*v);
                d.date = extract_ts_any(block[6], row);
                d.declaration_date = extract_ts_any(block[7], row);
                d.record_date = extract_ts_any(block[8], row);
                d.pay_date = extract_ts_any(block[9], row);
                out.push_back(std::move(d));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_dividends failed: {}", e.what());
        out.clear();
    }

    return out;
}

std::vector<StockSplitRecord> ClickHouseDataSource::get_stock_splits(const StockSplitsQuery& query) {
    std::vector<StockSplitRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value, const char* op) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} {} '{}'", col, op, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };

        add_str("ticker", query.ticker, "=");
        add_str("ticker", query.ticker_gt, ">");
        add_str("ticker", query.ticker_gte, ">=");
        add_str("ticker", query.ticker_lt, "<");
        add_str("ticker", query.ticker_lte, "<=");

        add_date("execution_date", query.execution_date, "=");
        add_date("execution_date", query.execution_date_gt, ">");
        add_date("execution_date", query.execution_date_gte, ">=");
        add_date("execution_date", query.execution_date_lt, "<");
        add_date("execution_date", query.execution_date_lte, "<=");

        if (query.max_execution_date) {
            auto ts = format_timestamp(*query.max_execution_date);
            where.push_back(fmt::format("execution_date <= toDate('{}')", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "execution_date";
        if (query.sort == "ticker") sort_col = "ticker";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";

        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(id AS String),
                   CAST(ticker AS String),
                   toDateTime(execution_date) AS execution_date,
                   split_from,
                   split_to
            FROM stock_splits
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockSplitRecord s;
                s.id = block[0]->As<clickhouse::ColumnString>()->At(row);
                s.ticker = block[1]->As<clickhouse::ColumnString>()->At(row);
                s.execution_date = extract_ts_any(block[2], row);
                if (auto v = get_nullable_float(block[3], row)) s.split_from = *v;
                if (auto v = get_nullable_float(block[4], row)) s.split_to = *v;
                out.push_back(std::move(s));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_splits failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<StockNewsRecord> ClickHouseDataSource::get_stock_news(const StockNewsQuery& query) {
    std::vector<StockNewsRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_ts = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDateTime64('{}', 3)", col, op, ts));
        };

        if (query.ticker && !query.ticker->empty()) {
            where.push_back(fmt::format("has(tickers, '{}')", *query.ticker));
        }

        add_ts("published_utc", query.published_utc, "=");
        add_ts("published_utc", query.published_utc_gt, ">");
        add_ts("published_utc", query.published_utc_gte, ">=");
        add_ts("published_utc", query.published_utc_lt, "<");
        add_ts("published_utc", query.published_utc_lte, "<=");

        if (query.max_published_utc) {
            auto ts = format_timestamp(*query.max_published_utc);
            where.push_back(fmt::format("published_utc <= toDateTime64('{}', 3)", ts));
        }

        if (query.cursor_published_utc) {
            auto ts = format_timestamp(*query.cursor_published_utc);
            if (query.order == "ascending") {
                if (query.cursor_id && !query.cursor_id->empty()) {
                    where.push_back(fmt::format("(published_utc > toDateTime64('{}', 3) OR (published_utc = toDateTime64('{}', 3) AND id > '{}'))",
                                                ts, ts, *query.cursor_id));
                } else {
                    where.push_back(fmt::format("published_utc > toDateTime64('{}', 3)", ts));
                }
            } else {
                if (query.cursor_id && !query.cursor_id->empty()) {
                    where.push_back(fmt::format("(published_utc < toDateTime64('{}', 3) OR (published_utc = toDateTime64('{}', 3) AND id < '{}'))",
                                                ts, ts, *query.cursor_id));
                } else {
                    where.push_back(fmt::format("published_utc < toDateTime64('{}', 3)", ts));
                }
            }
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string order = (query.order == "ascending") ? "ASC" : "DESC";
        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {}", query.limit);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(id AS String),
                   published_utc,
                   updated_utc,
                   CAST(publisher_name AS String),
                   publisher_homepage_url,
                   publisher_logo_url,
                   publisher_favicon_url,
                   title,
                   author,
                   article_url,
                   amp_url,
                   image_url,
                   description,
                   CAST(tickers AS Array(String)) AS tickers,
                   CAST(keywords AS Array(String)) AS keywords
            FROM stock_news
            {}
            ORDER BY published_utc {} , id {}
            {}
        )", where_clause, order, order, limit_clause);

        auto read_array = [](const clickhouse::ColumnRef& col, size_t row) {
            std::vector<std::string> out;
            auto arr = col->As<clickhouse::ColumnArray>();
            if (!arr) return out;
            auto row_col = arr->GetAsColumn(row);
            auto str_col = row_col->As<clickhouse::ColumnString>();
            if (!str_col) return out;
            out.reserve(str_col->Size());
            for (size_t i = 0; i < str_col->Size(); ++i) {
                auto sv = str_col->At(i);
                out.emplace_back(sv.data(), sv.size());
            }
            return out;
        };

        client.Select(sql, [&out, &read_array](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockNewsRecord n;
                n.id = block[0]->As<clickhouse::ColumnString>()->At(row);
                n.published_utc = extract_ts_any(block[1], row);
                n.updated_utc = extract_ts_any(block[2], row);
                n.publisher_name = block[3]->As<clickhouse::ColumnString>()->At(row);
                n.publisher_homepage_url = block[4]->As<clickhouse::ColumnString>()->At(row);
                n.publisher_logo_url = block[5]->As<clickhouse::ColumnString>()->At(row);
                n.publisher_favicon_url = block[6]->As<clickhouse::ColumnString>()->At(row);
                n.title = block[7]->As<clickhouse::ColumnString>()->At(row);
                n.author = block[8]->As<clickhouse::ColumnString>()->At(row);
                n.article_url = block[9]->As<clickhouse::ColumnString>()->At(row);
                n.amp_url = block[10]->As<clickhouse::ColumnString>()->At(row);
                n.image_url = block[11]->As<clickhouse::ColumnString>()->At(row);
                n.description = block[12]->As<clickhouse::ColumnString>()->At(row);
                n.tickers = read_array(block[13], row);
                n.keywords = read_array(block[14], row);
                out.push_back(std::move(n));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_news failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<StockNewsInsightRecord> ClickHouseDataSource::get_stock_news_insights(const std::vector<std::string>& article_ids) {
    std::vector<StockNewsInsightRecord> out;
    if (article_ids.empty()) return out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::string id_list;
        for (size_t i = 0; i < article_ids.size(); ++i) {
            if (i > 0) id_list += ", ";
            id_list += "'" + article_ids[i] + "'";
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(article_id AS String),
                   published_utc,
                   CAST(ticker AS String),
                   CAST(sentiment AS String),
                   sentiment_reasoning,
                   sentiment_score,
                   relevance_score
            FROM stock_news_insights
            WHERE article_id IN ({})
            ORDER BY published_utc DESC
        )", id_list);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockNewsInsightRecord ins;
                ins.article_id = block[0]->As<clickhouse::ColumnString>()->At(row);
                ins.published_utc = extract_ts_any(block[1], row);
                ins.ticker = block[2]->As<clickhouse::ColumnString>()->At(row);
                ins.sentiment = block[3]->As<clickhouse::ColumnString>()->At(row);
                ins.sentiment_reasoning = block[4]->As<clickhouse::ColumnString>()->At(row);
                if (auto v = get_nullable_float(block[5], row)) ins.sentiment_score = *v;
                if (auto v = get_nullable_float(block[6], row)) ins.relevance_score = *v;
                out.push_back(std::move(ins));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_news_insights failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<StockTickerEventRecord> ClickHouseDataSource::get_stock_ticker_events(const StockTickerEventsQuery& query) {
    std::vector<StockTickerEventRecord> out;
    if (query.ticker.empty()) return out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        where.push_back(fmt::format("(entity_id = '{0}' OR new_ticker = '{0}')", query.ticker));

        if (!query.types.empty()) {
            std::string type_list;
            for (size_t i = 0; i < query.types.size(); ++i) {
                if (i > 0) type_list += ", ";
                type_list += "'" + query.types[i] + "'";
            }
            where.push_back(fmt::format("event_type IN ({})", type_list));
        }

        if (query.max_date) {
            auto ts = format_timestamp(*query.max_date);
            auto date = ts.substr(0, 10);
            where.push_back(fmt::format("(event_date IS NULL OR event_date = '' OR event_date <= '{}')", date));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "event_date";
        if (query.sort == "event_type" || query.sort == "type") {
            sort_col = "event_type";
        } else if (query.sort == "event_date" || query.sort == "date") {
            sort_col = "event_date";
        } else if (query.sort == "ticker") {
            sort_col = "new_ticker";
        }

        std::string order = (query.order == "asc") ? "ASC" : "DESC";
        size_t limit = query.limit > 0 ? query.limit : 10;

        std::string sql = fmt::format(R"(
            SELECT entity_name,
                   CAST(event_type AS String),
                   event_date,
                   new_ticker,
                   raw_json
            FROM stock_ticker_events
            {}
            ORDER BY {} {}
            LIMIT {}
        )", where_clause, sort_col, order, limit);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockTickerEventRecord r;
                r.entity_name = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.event_type = block[1]->As<clickhouse::ColumnString>()->At(row);
                if (auto v = get_nullable_string(block[2], row)) r.event_date = *v;
                r.new_ticker = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.raw_json = block[4]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_ticker_events failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::optional<TickerBasicRecord> ClickHouseDataSource::get_ticker_basic(const std::string& ticker,
                                                                        std::optional<Timestamp> max_date) {
    if (ticker.empty()) return std::nullopt;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        where.push_back(fmt::format("ticker = '{}'", ticker));
        if (max_date) {
            auto ts = format_timestamp(*max_date);
            where.push_back(fmt::format("(snapshot_date IS NULL OR snapshot_date <= toDate('{}'))", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sql = fmt::format(R"(
            SELECT name,
                   composite_figi,
                   cik,
                   raw_json
            FROM tickers
            {}
            ORDER BY snapshot_date DESC
            LIMIT 1
        )", where_clause);

        std::optional<TickerBasicRecord> out;
        client.Select(sql, [&out](const clickhouse::Block& block) {
            if (block.GetRowCount() == 0) return;
            TickerBasicRecord rec;
            rec.name = block[0]->As<clickhouse::ColumnString>()->At(0);
            rec.composite_figi = block[1]->As<clickhouse::ColumnString>()->At(0);
            rec.cik = block[2]->As<clickhouse::ColumnString>()->At(0);
            auto raw_json = block[3]->As<clickhouse::ColumnString>()->At(0);
            if (!raw_json.empty()) {
                try {
                    auto j = nlohmann::json::parse(raw_json);
                    if (j.contains("name") && j["name"].is_string()) rec.name = j["name"].get<std::string>();
                    if (j.contains("composite_figi") && j["composite_figi"].is_string()) {
                        rec.composite_figi = j["composite_figi"].get<std::string>();
                    }
                    if (j.contains("cik") && j["cik"].is_string()) rec.cik = j["cik"].get<std::string>();
                } catch (...) {
                }
            }
            out = std::move(rec);
        });
        return out;
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_ticker_basic failed: {}", e.what());
    }
    return std::nullopt;
}

std::vector<StockIpoRecord> ClickHouseDataSource::get_stock_ipos(const StockIposQuery& query) {
    std::vector<StockIpoRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} = '{}'", col, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };

        add_str("ticker", query.ticker);
        add_str("ipo_status", query.ipo_status);

        add_date("announced_date", query.announced_date, "=");
        add_date("announced_date", query.announced_date_gt, ">");
        add_date("announced_date", query.announced_date_gte, ">=");
        add_date("announced_date", query.announced_date_lt, "<");
        add_date("announced_date", query.announced_date_lte, "<=");

        add_date("listing_date", query.listing_date, "=");
        add_date("listing_date", query.listing_date_gt, ">");
        add_date("listing_date", query.listing_date_gte, ">=");
        add_date("listing_date", query.listing_date_lt, "<");
        add_date("listing_date", query.listing_date_lte, "<=");

        add_date("issue_start_date", query.issue_start_date, "=");
        add_date("issue_start_date", query.issue_start_date_gt, ">");
        add_date("issue_start_date", query.issue_start_date_gte, ">=");
        add_date("issue_start_date", query.issue_start_date_lt, "<");
        add_date("issue_start_date", query.issue_start_date_lte, "<=");

        add_date("issue_end_date", query.issue_end_date, "=");
        add_date("issue_end_date", query.issue_end_date_gt, ">");
        add_date("issue_end_date", query.issue_end_date_gte, ">=");
        add_date("issue_end_date", query.issue_end_date_lt, "<");
        add_date("issue_end_date", query.issue_end_date_lte, "<=");

        add_date("last_updated", query.last_updated, "=");
        add_date("last_updated", query.last_updated_gt, ">");
        add_date("last_updated", query.last_updated_gte, ">=");
        add_date("last_updated", query.last_updated_lt, "<");
        add_date("last_updated", query.last_updated_lte, "<=");

        if (query.max_date) {
            auto ts = format_timestamp(*query.max_date);
            where.push_back(fmt::format(R"(
                (
                    (listing_date IS NOT NULL AND listing_date <= toDate('{0}')) OR
                    (listing_date IS NULL AND announced_date IS NOT NULL AND announced_date <= toDate('{0}')) OR
                    (listing_date IS NULL AND announced_date IS NULL AND issue_end_date IS NOT NULL AND issue_end_date <= toDate('{0}')) OR
                    (listing_date IS NULL AND announced_date IS NULL AND issue_end_date IS NULL AND issue_start_date IS NOT NULL AND issue_start_date <= toDate('{0}')) OR
                    (listing_date IS NULL AND announced_date IS NULL AND issue_end_date IS NULL AND issue_start_date IS NULL)
                )
            )", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "listing_date";
        if (query.sort == "announced_date") sort_col = "announced_date";
        else if (query.sort == "issue_start_date") sort_col = "issue_start_date";
        else if (query.sort == "issue_end_date") sort_col = "issue_end_date";
        else if (query.sort == "last_updated") sort_col = "last_updated";
        else if (query.sort == "ticker") sort_col = "ticker";
        else if (query.sort == "ipo_status") sort_col = "ipo_status";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";
        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(ticker AS String),
                   announced_date,
                   listing_date,
                   issue_start_date,
                   issue_end_date,
                   last_updated,
                   CAST(ipo_status AS String),
                   raw_json
            FROM stock_ipos
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockIpoRecord r;
                r.ticker = block[0]->As<clickhouse::ColumnString>()->At(row);
                auto announced_ts = extract_ts_any(block[1], row);
                if (announced_ts.time_since_epoch().count() != 0) r.announced_date = announced_ts;
                auto listing_ts = extract_ts_any(block[2], row);
                if (listing_ts.time_since_epoch().count() != 0) r.listing_date = listing_ts;
                auto issue_start_ts = extract_ts_any(block[3], row);
                if (issue_start_ts.time_since_epoch().count() != 0) r.issue_start_date = issue_start_ts;
                auto issue_end_ts = extract_ts_any(block[4], row);
                if (issue_end_ts.time_since_epoch().count() != 0) r.issue_end_date = issue_end_ts;
                auto last_updated_ts = extract_ts_any(block[5], row);
                if (last_updated_ts.time_since_epoch().count() != 0) r.last_updated = last_updated_ts;
                r.ipo_status = block[6]->As<clickhouse::ColumnString>()->At(row);
                r.raw_json = block[7]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_ipos failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<StockShortInterestRecord> ClickHouseDataSource::get_stock_short_interest(const StockShortInterestQuery& query) {
    std::vector<StockShortInterestRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} = '{}'", col, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };

        add_str("ticker", query.ticker);
        add_date("settlement_date", query.settlement_date, "=");
        add_date("settlement_date", query.settlement_date_gt, ">");
        add_date("settlement_date", query.settlement_date_gte, ">=");
        add_date("settlement_date", query.settlement_date_lt, "<");
        add_date("settlement_date", query.settlement_date_lte, "<=");

        if (query.max_settlement_date) {
            auto ts = format_timestamp(*query.max_settlement_date);
            where.push_back(fmt::format("settlement_date <= toDate('{}')", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "settlement_date";
        if (query.sort == "ticker") sort_col = "ticker";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";
        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(ticker AS String),
                   settlement_date,
                   short_interest,
                   days_to_cover,
                   raw_json
            FROM stock_short_interest
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockShortInterestRecord r;
                r.ticker = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.settlement_date = extract_ts_any(block[1], row);
                if (auto v = get_nullable_float(block[2], row)) r.short_interest = *v;
                if (auto v = get_nullable_float(block[3], row)) r.days_to_cover = *v;
                r.raw_json = block[4]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_short_interest failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<StockShortVolumeRecord> ClickHouseDataSource::get_stock_short_volume(const StockShortVolumeQuery& query) {
    std::vector<StockShortVolumeRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} = '{}'", col, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };

        add_str("ticker", query.ticker);
        add_date("trade_date", query.trade_date, "=");
        add_date("trade_date", query.trade_date_gt, ">");
        add_date("trade_date", query.trade_date_gte, ">=");
        add_date("trade_date", query.trade_date_lt, "<");
        add_date("trade_date", query.trade_date_lte, "<=");

        if (query.max_trade_date) {
            auto ts = format_timestamp(*query.max_trade_date);
            where.push_back(fmt::format("trade_date <= toDate('{}')", ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = "trade_date";
        if (query.sort == "ticker") sort_col = "ticker";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";
        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(ticker AS String),
                   trade_date,
                   raw_json
            FROM stock_short_volume
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        client.Select(sql, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                StockShortVolumeRecord r;
                r.ticker = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.trade_date = extract_ts_any(block[1], row);
                r.raw_json = block[2]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_short_volume failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinancialsRecord> ClickHouseDataSource::get_stock_financials(const FinancialsQuery& query) {
    std::vector<FinancialsRecord> out;
    try {
        clickhouse::ClientOptions opts;
        opts.SetHost(cfg_.host);
        opts.SetPort(cfg_.port);
        opts.SetDefaultDatabase(cfg_.database);
        opts.SetUser(cfg_.user);
        opts.SetPassword(cfg_.password);
        clickhouse::Client client(opts);

        std::vector<std::string> where;
        auto add_str = [&where](const std::string& col, const std::optional<std::string>& value) {
            if (!value || value->empty()) return;
            where.push_back(fmt::format("{} = '{}'", col, *value));
        };
        auto add_date = [this, &where](const std::string& col, const std::optional<Timestamp>& value, const char* op) {
            if (!value) return;
            auto ts = format_timestamp(*value);
            where.push_back(fmt::format("{} {} toDate('{}')", col, op, ts));
        };
        auto add_int = [&where](const std::string& col, const std::optional<int>& value) {
            if (!value) return;
            where.push_back(fmt::format("{} = {}", col, *value));
        };

        add_str("bs.ticker", query.ticker);
        add_str("bs.cik", query.cik);
        add_str("bs.timeframe", query.timeframe);
        add_str("bs.fiscal_period", query.fiscal_period);
        add_int("bs.fiscal_year", query.fiscal_year);

        const std::string period_col = "ifNull(bs.period_of_report, bs.end_date)";
        add_date(period_col, query.period_of_report_date, "=");
        add_date(period_col, query.period_of_report_date_gt, ">");
        add_date(period_col, query.period_of_report_date_gte, ">=");
        add_date(period_col, query.period_of_report_date_lt, "<");
        add_date(period_col, query.period_of_report_date_lte, "<=");

        add_date("bs.filing_date", query.filing_date, "=");
        add_date("bs.filing_date", query.filing_date_gt, ">");
        add_date("bs.filing_date", query.filing_date_gte, ">=");
        add_date("bs.filing_date", query.filing_date_lt, "<");
        add_date("bs.filing_date", query.filing_date_lte, "<=");

        if (query.max_period_of_report_date) {
            auto ts = format_timestamp(*query.max_period_of_report_date);
            where.push_back(fmt::format("{} <= toDate('{}')", period_col, ts));
        }

        std::string where_clause;
        if (!where.empty()) {
            where_clause = "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) where_clause += " AND ";
                where_clause += where[i];
            }
        }

        std::string sort_col = period_col;
        if (query.sort == "filing_date") sort_col = "bs.filing_date";

        std::string order = (query.order == "asc") ? "ASC" : "DESC";
        std::string limit_clause;
        if (query.limit > 0) {
            limit_clause = fmt::format(" LIMIT {} OFFSET {}", query.limit, query.offset);
        }

        std::string sql = fmt::format(R"(
            SELECT CAST(bs.ticker AS String),
                   bs.cik,
                   bs.company_name,
                   bs.start_date,
                   bs.end_date,
                   bs.filing_date,
                   bs.acceptance_datetime,
                   CAST(bs.timeframe AS String),
                   CAST(bs.fiscal_period AS String),
                   bs.fiscal_year,
                   bs.source_filing_url,
                   CAST(bs.form AS String),
                   CAST(bs.currency AS String),
                   ifNull(bs.period_of_report, bs.end_date) AS period_of_report,
                   bs.assets,
                   bs.current_assets,
                   bs.noncurrent_assets,
                   bs.inventory,
                   bs.accounts_receivable,
                   bs.liabilities,
                   bs.current_liabilities,
                   bs.noncurrent_liabilities,
                   bs.accounts_payable,
                   bs.long_term_debt,
                   bs.equity,
                   bs.intangible_assets,
                   bs.property_plant_and_equipment,
                   inc.revenues,
                   inc.cost_of_revenue,
                   inc.gross_profit,
                   inc.operating_income_loss,
                   inc.net_income_loss,
                   inc.net_income_loss_attributable_to_parent,
                   inc.research_and_development,
                   inc.selling_general_and_administrative,
                   inc.income_tax_expense_benefit,
                   inc.basic_earnings_per_share,
                   inc.diluted_earnings_per_share,
                   inc.basic_average_shares,
                   inc.diluted_average_shares,
                   cf.net_cash_flow,
                   cf.net_cash_flow_from_operating_activities,
                   cf.net_cash_flow_from_investing_activities,
                   cf.net_cash_flow_from_financing_activities,
                   bs.raw_json,
                   inc.raw_json,
                   cf.raw_json
            FROM stock_balance_sheets AS bs
            LEFT JOIN stock_income_statements AS inc
              ON inc.ticker = bs.ticker
             AND ifNull(inc.period_of_report, inc.end_date) = ifNull(bs.period_of_report, bs.end_date)
             AND inc.timeframe = bs.timeframe
             AND inc.fiscal_period = bs.fiscal_period
             AND inc.fiscal_year = bs.fiscal_year
            LEFT JOIN stock_cash_flow_statements AS cf
              ON cf.ticker = bs.ticker
             AND ifNull(cf.period_of_report, cf.end_date) = ifNull(bs.period_of_report, bs.end_date)
             AND cf.timeframe = bs.timeframe
             AND cf.fiscal_period = bs.fiscal_period
             AND cf.fiscal_year = bs.fiscal_year
            {}
            ORDER BY {} {}
            {}
        )", where_clause, sort_col, order, limit_clause);

        auto set_val = [](std::unordered_map<std::string, double>& dest,
                          const std::string& key,
                          const clickhouse::ColumnRef& col,
                          size_t row) {
            if (auto v = get_nullable_float(col, row)) {
                dest[key] = *v;
            }
        };

        auto parse_json = [](const clickhouse::ColumnRef& col, size_t row) {
            auto raw = col->As<clickhouse::ColumnString>()->At(row);
            if (raw.empty()) return nlohmann::json::object();
            try {
                return nlohmann::json::parse(raw);
            } catch (...) {
                return nlohmann::json::object();
            }
        };

        auto json_number = [](const nlohmann::json& obj, const char* key) -> std::optional<double> {
            auto it = obj.find(key);
            if (it == obj.end()) return std::nullopt;
            if (it->is_number_float() || it->is_number_integer() || it->is_number_unsigned()) {
                return it->get<double>();
            }
            return std::nullopt;
        };

        client.Select(sql, [&out, &set_val, &parse_json, &json_number](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinancialsRecord r;
                r.ticker = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.cik = block[1]->As<clickhouse::ColumnString>()->At(row);
                r.company_name = block[2]->As<clickhouse::ColumnString>()->At(row);
                r.start_date = extract_ts_any(block[3], row);
                r.end_date = extract_ts_any(block[4], row);
                r.filing_date = extract_ts_any(block[5], row);
                r.acceptance_datetime = extract_ts_any(block[6], row);
                r.timeframe = block[7]->As<clickhouse::ColumnString>()->At(row);
                r.fiscal_period = block[8]->As<clickhouse::ColumnString>()->At(row);
                if (auto v = get_nullable_uint16(block[9], row)) r.fiscal_year = std::to_string(*v);
                r.source_filing_url = block[10]->As<clickhouse::ColumnString>()->At(row);
                r.form = block[11]->As<clickhouse::ColumnString>()->At(row);
                r.currency = block[12]->As<clickhouse::ColumnString>()->At(row);
                r.period_of_report = extract_ts_any(block[13], row);

                set_val(r.balance_sheet, "assets", block[14], row);
                set_val(r.balance_sheet, "current_assets", block[15], row);
                set_val(r.balance_sheet, "noncurrent_assets", block[16], row);
                set_val(r.balance_sheet, "inventory", block[17], row);
                set_val(r.balance_sheet, "accounts_receivable", block[18], row);
                set_val(r.balance_sheet, "liabilities", block[19], row);
                set_val(r.balance_sheet, "current_liabilities", block[20], row);
                set_val(r.balance_sheet, "noncurrent_liabilities", block[21], row);
                set_val(r.balance_sheet, "accounts_payable", block[22], row);
                set_val(r.balance_sheet, "long_term_debt", block[23], row);
                set_val(r.balance_sheet, "equity", block[24], row);
                set_val(r.balance_sheet, "intangible_assets", block[25], row);
                if (auto v = get_nullable_float(block[26], row)) {
                    r.balance_sheet["fixed_assets"] = *v;
                }

                auto it_liab = r.balance_sheet.find("liabilities");
                auto it_equity = r.balance_sheet.find("equity");
                if (it_liab != r.balance_sheet.end() && it_equity != r.balance_sheet.end()) {
                    r.balance_sheet["liabilities_and_equity"] = it_liab->second + it_equity->second;
                }

                set_val(r.income_statement, "revenues", block[27], row);
                set_val(r.income_statement, "cost_of_revenue", block[28], row);
                set_val(r.income_statement, "gross_profit", block[29], row);
                set_val(r.income_statement, "operating_income_loss", block[30], row);
                set_val(r.income_statement, "net_income_loss", block[31], row);
                set_val(r.income_statement, "net_income_loss_attributable_to_parent", block[32], row);
                set_val(r.income_statement, "research_and_development", block[33], row);
                set_val(r.income_statement, "selling_general_and_administrative_expenses", block[34], row);
                set_val(r.income_statement, "income_tax_expense_benefit", block[35], row);
                set_val(r.income_statement, "basic_earnings_per_share", block[36], row);
                set_val(r.income_statement, "diluted_earnings_per_share", block[37], row);
                set_val(r.income_statement, "basic_average_shares", block[38], row);
                set_val(r.income_statement, "diluted_average_shares", block[39], row);

                set_val(r.cash_flow_statement, "net_cash_flow", block[40], row);
                set_val(r.cash_flow_statement, "net_cash_flow_from_operating_activities", block[41], row);
                set_val(r.cash_flow_statement, "net_cash_flow_from_investing_activities", block[42], row);
                set_val(r.cash_flow_statement, "net_cash_flow_from_financing_activities", block[43], row);

                auto bs_json = parse_json(block[44], row);
                auto inc_json = parse_json(block[45], row);
                auto cf_json = parse_json(block[46], row);

                auto set_json = [&json_number](std::unordered_map<std::string, double>& dest,
                                               const std::string& key,
                                               const nlohmann::json& obj,
                                               const char* json_key) {
                    if (auto v = json_number(obj, json_key)) {
                        dest[key] = *v;
                    }
                };

                set_json(r.balance_sheet, "assets", bs_json, "total_assets");
                set_json(r.balance_sheet, "current_assets", bs_json, "total_current_assets");
                set_json(r.balance_sheet, "liabilities", bs_json, "total_liabilities");
                set_json(r.balance_sheet, "current_liabilities", bs_json, "total_current_liabilities");
                set_json(r.balance_sheet, "equity", bs_json, "total_equity");
                set_json(r.balance_sheet, "equity_attributable_to_parent", bs_json, "total_equity_attributable_to_parent");
                set_json(r.balance_sheet, "liabilities_and_equity", bs_json, "total_liabilities_and_equity");
                set_json(r.balance_sheet, "fixed_assets", bs_json, "property_plant_equipment_net");
                set_json(r.balance_sheet, "inventory", bs_json, "inventories");
                set_json(r.balance_sheet, "accounts_payable", bs_json, "accounts_payable");
                set_json(r.balance_sheet, "long_term_debt", bs_json, "long_term_debt_and_capital_lease_obligations");
                set_json(r.balance_sheet, "other_current_assets", bs_json, "other_current_assets");
                set_json(r.balance_sheet, "other_current_liabilities", bs_json, "accrued_and_other_current_liabilities");
                set_json(r.balance_sheet, "other_noncurrent_assets", bs_json, "other_assets");
                set_json(r.balance_sheet, "other_noncurrent_liabilities", bs_json, "other_noncurrent_liabilities");
                set_json(r.balance_sheet, "intangible_assets", bs_json, "intangible_assets_net");

                auto assets_it = r.balance_sheet.find("assets");
                auto current_assets_it = r.balance_sheet.find("current_assets");
                if (assets_it != r.balance_sheet.end() && current_assets_it != r.balance_sheet.end()) {
                    r.balance_sheet["noncurrent_assets"] = assets_it->second - current_assets_it->second;
                }

                auto liabilities_it = r.balance_sheet.find("liabilities");
                auto current_liab_it = r.balance_sheet.find("current_liabilities");
                if (liabilities_it != r.balance_sheet.end() && current_liab_it != r.balance_sheet.end()) {
                    r.balance_sheet["noncurrent_liabilities"] = liabilities_it->second - current_liab_it->second;
                }

                auto equity_it = r.balance_sheet.find("equity");
                auto equity_parent_it = r.balance_sheet.find("equity_attributable_to_parent");
                if (equity_it != r.balance_sheet.end() && equity_parent_it != r.balance_sheet.end()) {
                    r.balance_sheet["equity_attributable_to_noncontrolling_interest"] =
                        equity_it->second - equity_parent_it->second;
                }

                if (r.balance_sheet.find("commitments_and_contingencies") == r.balance_sheet.end()) {
                    r.balance_sheet["commitments_and_contingencies"] = 0.0;
                }

                set_json(r.income_statement, "revenues", inc_json, "revenue");
                set_json(r.income_statement, "cost_of_revenue", inc_json, "cost_of_revenue");
                set_json(r.income_statement, "gross_profit", inc_json, "gross_profit");
                set_json(r.income_statement, "operating_income_loss", inc_json, "operating_income");
                set_json(r.income_statement, "net_income_loss", inc_json, "consolidated_net_income_loss");
                set_json(r.income_statement, "net_income_loss_attributable_to_parent", inc_json, "net_income_loss_attributable_common_shareholders");
                set_json(r.income_statement, "research_and_development", inc_json, "research_development");
                set_json(r.income_statement, "selling_general_and_administrative_expenses", inc_json, "selling_general_administrative");
                set_json(r.income_statement, "income_tax_expense_benefit", inc_json, "income_taxes");
                set_json(r.income_statement, "income_loss_from_continuing_operations_before_tax", inc_json, "income_before_income_taxes");
                set_json(r.income_statement, "income_loss_before_equity_method_investments", inc_json, "income_before_equity_method_investments");
                set_json(r.income_statement, "operating_expenses", inc_json, "total_operating_expenses");
                set_json(r.income_statement, "nonoperating_income_loss", inc_json, "total_other_income_expense");
                set_json(r.income_statement, "preferred_stock_dividends_and_other_adjustments", inc_json, "preferred_stock_dividends_declared");
                if (r.income_statement.find("nonoperating_income_loss") == r.income_statement.end()) {
                    set_json(r.income_statement, "nonoperating_income_loss", inc_json, "other_income_expense");
                }
                set_json(r.income_statement, "basic_earnings_per_share", inc_json, "basic_earnings_per_share");
                set_json(r.income_statement, "diluted_earnings_per_share", inc_json, "diluted_earnings_per_share");
                set_json(r.income_statement, "basic_average_shares", inc_json, "basic_shares_outstanding");
                set_json(r.income_statement, "diluted_average_shares", inc_json, "diluted_shares_outstanding");

                auto cost_it = r.income_statement.find("cost_of_revenue");
                auto op_exp_it = r.income_statement.find("operating_expenses");
                if (cost_it != r.income_statement.end() && op_exp_it != r.income_statement.end()) {
                    auto total = cost_it->second + op_exp_it->second;
                    r.income_statement["benefits_costs_expenses"] = total;
                    r.income_statement["costs_and_expenses"] = total;
                }

                auto pretax_it = r.income_statement.find("income_loss_from_continuing_operations_before_tax");
                auto tax_it = r.income_statement.find("income_tax_expense_benefit");
                if (pretax_it != r.income_statement.end() && tax_it != r.income_statement.end()) {
                    r.income_statement["income_loss_from_continuing_operations_after_tax"] =
                        pretax_it->second - tax_it->second;
                }
                if (r.income_statement.find("income_loss_before_equity_method_investments") == r.income_statement.end()) {
                    if (pretax_it != r.income_statement.end()) {
                        r.income_statement["income_loss_before_equity_method_investments"] = pretax_it->second;
                    }
                }

                auto net_it = r.income_statement.find("net_income_loss");
                auto net_parent_it = r.income_statement.find("net_income_loss_attributable_to_parent");
                if (net_it != r.income_statement.end() && net_parent_it != r.income_statement.end()) {
                    r.income_statement["net_income_loss_attributable_to_noncontrolling_interest"] =
                        net_it->second - net_parent_it->second;
                }
                if (net_parent_it != r.income_statement.end()) {
                    r.income_statement["net_income_loss_available_to_common_stockholders_basic"] =
                        net_parent_it->second;
                }

                if (r.income_statement.find("preferred_stock_dividends_and_other_adjustments") == r.income_statement.end()) {
                    r.income_statement["preferred_stock_dividends_and_other_adjustments"] = 0.0;
                }
                if (r.income_statement.find("participating_securities_distributed_and_undistributed_earnings_loss_basic") == r.income_statement.end()) {
                    r.income_statement["participating_securities_distributed_and_undistributed_earnings_loss_basic"] = 0.0;
                }

                set_json(r.cash_flow_statement, "net_cash_flow_from_operating_activities", cf_json, "net_cash_from_operating_activities");
                set_json(r.cash_flow_statement, "net_cash_flow_from_operating_activities_continuing", cf_json, "net_cash_from_operating_activities_continuing_operations");
                if (r.cash_flow_statement.find("net_cash_flow_from_operating_activities_continuing") == r.cash_flow_statement.end()) {
                    set_json(r.cash_flow_statement, "net_cash_flow_from_operating_activities_continuing",
                             cf_json, "cash_from_operating_activities_continuing_operations");
                }
                set_json(r.cash_flow_statement, "net_cash_flow_from_investing_activities", cf_json, "net_cash_from_investing_activities");
                set_json(r.cash_flow_statement, "net_cash_flow_from_investing_activities_continuing", cf_json, "net_cash_from_investing_activities_continuing_operations");
                set_json(r.cash_flow_statement, "net_cash_flow_from_financing_activities", cf_json, "net_cash_from_financing_activities");
                set_json(r.cash_flow_statement, "net_cash_flow_from_financing_activities_continuing", cf_json, "net_cash_from_financing_activities_continuing_operations");
                set_json(r.cash_flow_statement, "net_cash_flow", cf_json, "change_in_cash_and_equivalents");

                auto net_cash_it = r.cash_flow_statement.find("net_cash_flow");
                if (net_cash_it != r.cash_flow_statement.end()) {
                    r.cash_flow_statement["net_cash_flow_continuing"] = net_cash_it->second;
                }

                if (r.comprehensive_income.empty()) {
                    r.comprehensive_income["comprehensive_income_loss"] = 0.0;
                    r.comprehensive_income["comprehensive_income_loss_attributable_to_noncontrolling_interest"] = 0.0;
                    r.comprehensive_income["comprehensive_income_loss_attributable_to_parent"] = 0.0;
                    r.comprehensive_income["other_comprehensive_income_loss"] = 0.0;
                    r.comprehensive_income["other_comprehensive_income_loss_attributable_to_parent"] = 0.0;
                }

                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_stock_financials failed: {}", e.what());
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
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), date, quarter, year,
               toFloat64(eps_estimate), toFloat64(eps_actual),
               toFloat64(revenue_estimate), toFloat64(revenue_actual),
               CAST(hour AS String)
        FROM finnhub_earnings_calendar
        WHERE date >= toDate('{}')
          AND date < toDate('{}')
          {}
        ORDER BY date DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
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
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), grade_time, company,
               CAST(from_grade AS String), CAST(to_grade AS String), CAST(action AS String)
        FROM finnhub_upgrades_downgrades
        WHERE grade_time >= toDateTime('{}')
          AND grade_time < toDateTime('{}')
          {}
        ORDER BY grade_time DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
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

std::vector<FinnhubIpoRecord> ClickHouseDataSource::get_finnhub_ipo_calendar(Timestamp start_time,
                                                                             Timestamp end_time,
                                                                             size_t limit) {
    std::vector<FinnhubIpoRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), date, exchange, name, number_of_shares, price_range,
               CAST(status AS String), total_shares_value, raw_json
        FROM finnhub_ipo_calendar
        WHERE date >= toDate('{}')
          AND date < toDate('{}')
        ORDER BY date DESC
        {}
    )", start_str, end_str, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubIpoRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.date = extract_ts_any(block[1], row);
                r.exchange = block[2]->As<clickhouse::ColumnString>()->At(row);
                r.name = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.number_of_shares = get_nullable_uint64(block[4], row);
                r.price_range = block[5]->As<clickhouse::ColumnString>()->At(row);
                r.status = block[6]->As<clickhouse::ColumnString>()->At(row);
                r.total_shares_value = get_nullable_uint64(block[7], row);
                r.raw_json = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_ipo_calendar failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<CompanyNewsRecord> ClickHouseDataSource::get_finnhub_market_news(Timestamp start_time,
                                                                             Timestamp end_time,
                                                                             size_t limit) {
    std::vector<CompanyNewsRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string query = fmt::format(R"(
        SELECT CAST(category AS String), datetime, headline, source_name, url, summary, image, id, raw_json
        FROM finnhub_market_news
        WHERE datetime >= toDateTime('{}')
          AND datetime < toDateTime('{}')
        ORDER BY datetime DESC
        {}
    )", start_str, end_str, limit_clause(limit));
    auto run_select = [&]() {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                CompanyNewsRecord n;
                n.category = block[0]->As<clickhouse::ColumnString>()->At(row);
                n.datetime = extract_ts_any(block[1], row);
                n.headline = block[2]->As<clickhouse::ColumnString>()->At(row);
                n.source = block[3]->As<clickhouse::ColumnString>()->At(row);
                n.url = block[4]->As<clickhouse::ColumnString>()->At(row);
                n.summary = block[5]->As<clickhouse::ColumnString>()->At(row);
                n.image = block[6]->As<clickhouse::ColumnString>()->At(row);
                n.id = block[7]->As<clickhouse::ColumnUInt64>()->At(row);
                n.raw_json = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(n));
            }
        });
    };
    try {
        run_select();
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_market_news failed: {}, reconnecting...", e.what());
        out.clear();
        connect();
        try {
            run_select();
        } catch (const std::exception& retry_e) {
            spdlog::warn("ClickHouse get_finnhub_market_news retry failed: {}", retry_e.what());
            out.clear();
        }
    }
    return out;
}

std::vector<FinnhubInsiderTransactionRecord> ClickHouseDataSource::get_finnhub_insider_transactions(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubInsiderTransactionRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), filing_id, transaction_date, name,
               toFloat64(share), toFloat64(change), toFloat64(transaction_price),
               CAST(transaction_code AS String), raw_json
        FROM finnhub_insider_transactions
        WHERE transaction_date >= toDate('{}')
          AND transaction_date < toDate('{}')
          {}
        ORDER BY transaction_date DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubInsiderTransactionRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.filing_id = block[1]->As<clickhouse::ColumnString>()->At(row);
                r.transaction_date = extract_ts_any(block[2], row);
                r.name = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.share = get_nullable_float(block[4], row);
                r.change = get_nullable_float(block[5], row);
                r.transaction_price = get_nullable_float(block[6], row);
                r.transaction_code = block[7]->As<clickhouse::ColumnString>()->At(row);
                r.raw_json = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_insider_transactions failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubSecFilingRecord> ClickHouseDataSource::get_finnhub_sec_filings(const std::string& symbol,
                                                                                  Timestamp start_time,
                                                                                  Timestamp end_time,
                                                                                  size_t limit) {
    std::vector<FinnhubSecFilingRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), filed_date, accepted_datetime, CAST(form AS String),
               access_number, report_url, raw_json
        FROM finnhub_sec_filings
        WHERE filed_date >= toDate('{}')
          AND filed_date < toDate('{}')
          {}
        ORDER BY filed_date DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubSecFilingRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.filed_date = extract_ts_any(block[1], row);
                r.accepted_datetime = extract_ts_any(block[2], row);
                r.form = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.access_number = block[4]->As<clickhouse::ColumnString>()->At(row);
                r.report_url = block[5]->As<clickhouse::ColumnString>()->At(row);
                r.raw_json = block[6]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_sec_filings failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubCongressionalTradingRecord> ClickHouseDataSource::get_finnhub_congressional_trading(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubCongressionalTradingRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), transaction_date, name,
               CAST(position AS String), CAST(owner_type AS String), CAST(transaction_type AS String),
               amount_from, amount_to, asset_name, filing_date, raw_json
        FROM finnhub_congressional_trading
        WHERE transaction_date >= toDate('{}')
          AND transaction_date < toDate('{}')
          {}
        ORDER BY transaction_date DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubCongressionalTradingRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.transaction_date = extract_ts_any(block[1], row);
                r.name = block[2]->As<clickhouse::ColumnString>()->At(row);
                r.position = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.owner_type = block[4]->As<clickhouse::ColumnString>()->At(row);
                r.transaction_type = block[5]->As<clickhouse::ColumnString>()->At(row);
                r.amount_from = get_nullable_uint64(block[6], row);
                r.amount_to = get_nullable_uint64(block[7], row);
                r.asset_name = block[8]->As<clickhouse::ColumnString>()->At(row);
                r.filing_date = extract_ts_any(block[9], row);
                r.raw_json = block[10]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_congressional_trading failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubInsiderSentimentRecord> ClickHouseDataSource::get_finnhub_insider_sentiment(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubInsiderSentimentRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), year, month, toFloat64(change), toFloat64(mspr)
        FROM finnhub_insider_sentiment
        WHERE makeDate(year, month, 1) >= toDate('{}')
          AND makeDate(year, month, 1) < toDate('{}')
          {}
        ORDER BY year DESC, month DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubInsiderSentimentRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.year = block[1]->As<clickhouse::ColumnUInt16>()->At(row);
                r.month = block[2]->As<clickhouse::ColumnUInt8>()->At(row);
                r.change = get_nullable_float(block[3], row);
                r.mspr = get_nullable_float(block[4], row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_insider_sentiment failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubEpsEstimateRecord> ClickHouseDataSource::get_finnhub_eps_estimates(const std::string& symbol,
                                                                                      Timestamp start_time,
                                                                                      Timestamp end_time,
                                                                                      const std::string& freq,
                                                                                      size_t limit) {
    std::vector<FinnhubEpsEstimateRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string where_freq;
    if (!freq.empty()) {
        if (freq == "quarterly") {
            where_freq = "AND (freq = 'quarterly' OR freq = '')";
        } else {
            where_freq = fmt::format("AND freq = '{}'", freq);
        }
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), period, quarter, year,
               toFloat64(eps_avg), toFloat64(eps_high), toFloat64(eps_low),
               number_analysts, if(freq = '', 'quarterly', CAST(freq AS String))
        FROM finnhub_eps_estimates
        WHERE period >= toDate('{}')
          AND period < toDate('{}')
          {}
          {}
        ORDER BY period DESC
        {}
    )", start_str, end_str, where_symbol, where_freq, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubEpsEstimateRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[1], row);
                r.quarter = get_nullable_uint16(block[2], row);
                r.year = get_nullable_uint16(block[3], row);
                r.eps_avg = get_nullable_float(block[4], row);
                r.eps_high = get_nullable_float(block[5], row);
                r.eps_low = get_nullable_float(block[6], row);
                r.number_analysts = get_nullable_uint16(block[7], row);
                r.freq = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_eps_estimates failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubRevenueEstimateRecord> ClickHouseDataSource::get_finnhub_revenue_estimates(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    const std::string& freq,
    size_t limit) {
    std::vector<FinnhubRevenueEstimateRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string where_freq;
    if (!freq.empty()) {
        if (freq == "quarterly") {
            where_freq = "AND (freq = 'quarterly' OR freq = '')";
        } else {
            where_freq = fmt::format("AND freq = '{}'", freq);
        }
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), period, quarter, year,
               toFloat64(revenue_avg), toFloat64(revenue_high), toFloat64(revenue_low),
               number_analysts, if(freq = '', 'quarterly', CAST(freq AS String))
        FROM finnhub_revenue_estimates
        WHERE period >= toDate('{}')
          AND period < toDate('{}')
          {}
          {}
        ORDER BY period DESC
        {}
    )", start_str, end_str, where_symbol, where_freq, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubRevenueEstimateRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[1], row);
                r.quarter = get_nullable_uint16(block[2], row);
                r.year = get_nullable_uint16(block[3], row);
                r.revenue_avg = get_nullable_float(block[4], row);
                r.revenue_high = get_nullable_float(block[5], row);
                r.revenue_low = get_nullable_float(block[6], row);
                r.number_analysts = get_nullable_uint16(block[7], row);
                r.freq = block[8]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_revenue_estimates failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubEarningsHistoryRecord> ClickHouseDataSource::get_finnhub_earnings_history(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubEarningsHistoryRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), period, quarter, year,
               toFloat64(actual), toFloat64(estimate), toFloat64(surprise), toFloat64(surprise_percent)
        FROM finnhub_earnings_history
        WHERE period >= toDate('{}')
          AND period < toDate('{}')
          {}
        ORDER BY period DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubEarningsHistoryRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[1], row);
                r.quarter = get_nullable_uint16(block[2], row);
                r.year = get_nullable_uint16(block[3], row);
                r.actual = get_nullable_float(block[4], row);
                r.estimate = get_nullable_float(block[5], row);
                r.surprise = get_nullable_float(block[6], row);
                r.surprise_percent = get_nullable_float(block[7], row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_earnings_history failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubSocialSentimentRecord> ClickHouseDataSource::get_finnhub_social_sentiment(
    const std::string& symbol,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubSocialSentimentRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), at_time, mention, positive_score, negative_score,
               positive_mention, negative_mention, score
        FROM finnhub_social_sentiment
        WHERE at_time >= toDateTime('{}')
          AND at_time < toDateTime('{}')
          {}
        ORDER BY at_time DESC
        {}
    )", start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubSocialSentimentRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.at_time = extract_ts_any(block[1], row);
                r.mention = get_nullable_uint32(block[2], row);
                r.positive_score = get_nullable_float(block[3], row);
                r.negative_score = get_nullable_float(block[4], row);
                r.positive_mention = get_nullable_uint32(block[5], row);
                r.negative_mention = get_nullable_uint32(block[6], row);
                r.score = get_nullable_float(block[7], row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_social_sentiment failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubOwnershipRecord> ClickHouseDataSource::get_finnhub_ownership(const std::string& symbol,
                                                                                Timestamp start_time,
                                                                                Timestamp end_time,
                                                                                size_t limit) {
    std::vector<FinnhubOwnershipRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    const char* date_expr =
        "coalesce(report_date, toDate(parseDateTimeBestEffortOrNull(JSONExtractString(raw_json, 'filingDate'))))";
    std::string query = fmt::format(R"(
        WITH {} AS effective_date
        SELECT CAST(symbol AS String), effective_date, organization,
               toFloat64(position), toFloat64(position_change), toFloat64(percent_held), raw_json
        FROM finnhub_ownership
        WHERE effective_date >= toDate('{}')
          AND effective_date < toDate('{}')
          {}
        ORDER BY effective_date DESC
        {}
    )", date_expr, start_str, end_str, where_symbol, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubOwnershipRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.report_date = extract_ts_any(block[1], row);
                r.organization = block[2]->As<clickhouse::ColumnString>()->At(row);
                r.position = get_nullable_float(block[3], row);
                r.position_change = get_nullable_float(block[4], row);
                r.percent_held = get_nullable_float(block[5], row);
                r.raw_json = block[6]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_ownership failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubFinancialsStandardizedRecord> ClickHouseDataSource::get_finnhub_financials_standardized(
    const std::string& symbol,
    const std::string& statement,
    const std::string& freq,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubFinancialsStandardizedRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string where_statement;
    if (!statement.empty()) {
        where_statement = fmt::format("AND statement = '{}'", statement);
    }
    std::string where_freq;
    if (!freq.empty()) {
        where_freq = fmt::format("AND freq = '{}'", freq);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), CAST(statement AS String), period, CAST(freq AS String),
               CAST(currency AS String), data_json
        FROM finnhub_financials_standardized
        WHERE period >= toDate('{}')
          AND period < toDate('{}')
          {}
          {}
          {}
        ORDER BY period DESC
        {}
    )", start_str, end_str, where_symbol, where_statement, where_freq, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubFinancialsStandardizedRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.statement = block[1]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[2], row);
                r.freq = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.currency = block[4]->As<clickhouse::ColumnString>()->At(row);
                r.data_json = block[5]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_financials_standardized failed: {}", e.what());
        out.clear();
    }
    return out;
}

std::vector<FinnhubFinancialsReportedRecord> ClickHouseDataSource::get_finnhub_financials_reported(
    const std::string& symbol,
    const std::string& freq,
    Timestamp start_time,
    Timestamp end_time,
    size_t limit) {
    std::vector<FinnhubFinancialsReportedRecord> out;
    if (!client_) return out;
    auto start_str = format_timestamp(start_time);
    auto end_str = format_timestamp(end_time);
    std::string where_symbol;
    if (!symbol.empty()) {
        where_symbol = fmt::format("AND symbol = '{}'", symbol);
    }
    std::string where_freq;
    if (!freq.empty()) {
        where_freq = fmt::format("AND freq = '{}'", freq);
    }
    std::string query = fmt::format(R"(
        SELECT CAST(symbol AS String), period, CAST(freq AS String), access_number, form,
               filed_date, accepted_datetime, data_json
        FROM finnhub_financials_reported
        WHERE period >= toDate('{}')
          AND period < toDate('{}')
          {}
          {}
        ORDER BY period DESC
        {}
    )", start_str, end_str, where_symbol, where_freq, limit_clause(limit));
    try {
        client_->Select(query, [&out](const clickhouse::Block& block) {
            for (size_t row = 0; row < block.GetRowCount(); ++row) {
                FinnhubFinancialsReportedRecord r;
                r.symbol = block[0]->As<clickhouse::ColumnString>()->At(row);
                r.period = extract_ts_any(block[1], row);
                r.freq = block[2]->As<clickhouse::ColumnString>()->At(row);
                r.access_number = block[3]->As<clickhouse::ColumnString>()->At(row);
                r.form = block[4]->As<clickhouse::ColumnString>()->At(row);
                r.filed_date = extract_ts_any(block[5], row);
                r.accepted_datetime = extract_ts_any(block[6], row);
                r.data_json = block[7]->As<clickhouse::ColumnString>()->At(row);
                out.push_back(std::move(r));
            }
        });
    } catch (const std::exception& e) {
        spdlog::warn("ClickHouse get_finnhub_financials_reported failed: {}", e.what());
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
    // DateTime64(9) stores nanoseconds since epoch
    auto nanos = c->At(row);
    return Timestamp{} + std::chrono::nanoseconds(nanos);
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
        // DateTime64(9) stores nanoseconds since epoch
        auto nanos = c->At(row);
        return Timestamp{} + std::chrono::nanoseconds(nanos);
    }
    if (auto c = col->As<clickhouse::ColumnDateTime>()) {
        auto secs = c->At(row);
        return Timestamp{} + std::chrono::seconds(secs);
    }
    if (auto c = col->As<clickhouse::ColumnDate>()) {
        auto secs = c->At(row);
        return Timestamp{} + std::chrono::seconds(secs);
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

std::optional<uint64_t> ClickHouseDataSource::get_nullable_uint64(const clickhouse::ColumnRef& col, size_t row) {
    if (auto c = col->As<clickhouse::ColumnNullable>()) {
        if (c->IsNull(row)) return std::nullopt;
        auto nested = c->Nested();
        if (auto v = nested->As<clickhouse::ColumnUInt64>()) return v->At(row);
        if (auto v = nested->As<clickhouse::ColumnUInt32>()) return v->At(row);
    }
    if (auto v = col->As<clickhouse::ColumnUInt64>()) return v->At(row);
    if (auto v = col->As<clickhouse::ColumnUInt32>()) return v->At(row);
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
