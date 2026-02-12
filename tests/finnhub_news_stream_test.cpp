#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "../src/core/data_source_stub.hpp"
#include "../src/core/session_manager.hpp"

using namespace broker_sim;

namespace {

Timestamp make_ts_ns(int64_t ns) {
    return Timestamp{} + std::chrono::nanoseconds(ns);
}

class FinnhubNewsTestDataSource : public StubDataSource {
public:
    explicit FinnhubNewsTestDataSource(std::vector<CompanyNewsRecord> company_news,
                                       std::vector<CompanyNewsRecord> market_news)
        : company_news_(std::move(company_news)),
          market_news_(std::move(market_news)) {}

    void stream_events(const std::vector<std::string>&,
                       Timestamp,
                       Timestamp,
                       const std::function<void(const MarketEvent&)>&) override {
        // Not needed for these tests; this source only emits news.
    }

    void stream_company_news(const std::vector<std::string>& symbols,
                             Timestamp start_time,
                             Timestamp end_time,
                             const std::function<void(const CompanyNewsRecord&)>& cb) override {
        std::unordered_set<std::string> allowed(symbols.begin(), symbols.end());
        for (const auto& news : company_news_) {
            if (news.datetime < start_time || news.datetime > end_time) continue;
            if (!allowed.empty() && allowed.find(news.symbol) == allowed.end()) continue;
            cb(news);
        }
    }

    void stream_finnhub_market_news(Timestamp start_time,
                                    Timestamp end_time,
                                    const std::function<void(const CompanyNewsRecord&)>& cb) override {
        for (const auto& news : market_news_) {
            if (news.datetime < start_time || news.datetime > end_time) continue;
            cb(news);
        }
    }

private:
    std::vector<CompanyNewsRecord> company_news_;
    std::vector<CompanyNewsRecord> market_news_;
};

}  // namespace

TEST(FinnhubNewsStreamingTest, CompanyNewsSubscriptionEmitsNewsEvent) {
    CompanyNewsRecord company_news;
    company_news.datetime = make_ts_ns(2000000);
    company_news.symbol = "AAPL";
    company_news.related = "AAPL";
    company_news.category = "company";
    company_news.headline = "AAPL earnings beat";
    company_news.summary = "Test summary";
    company_news.source = "UnitTest";
    company_news.url = "https://example.test/news/aapl";
    company_news.id = 101;

    auto ds = std::make_shared<FinnhubNewsTestDataSource>(
        std::vector<CompanyNewsRecord>{company_news},
        std::vector<CompanyNewsRecord>{});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"AAPL"};
    cfg.start_time = make_ts_ns(0);
    cfg.end_time = make_ts_ns(10000000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    std::mutex mu;
    std::condition_variable cv;
    bool got_news = false;
    Event received_event;

    mgr.add_event_callback([&](const std::string& sid, const Event& event) {
        if (sid != session->id) return;
        if (event.event_type != EventType::NEWS) return;
        std::lock_guard<std::mutex> lock(mu);
        got_news = true;
        received_event = event;
        cv.notify_all();
    });

    mgr.start_session(session->id);
    mgr.update_news_subscriptions(session->id, {"AAPL"}, true);

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return got_news; }));
    }

    EXPECT_EQ(received_event.event_type, EventType::NEWS);
    EXPECT_EQ(received_event.symbol, "AAPL");
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::nanoseconds>(
                  received_event.timestamp.time_since_epoch())
                  .count(),
              2000000);

    const auto& payload = std::get<NewsData>(received_event.data);
    EXPECT_EQ(payload.headline, "AAPL earnings beat");
    EXPECT_EQ(payload.category, "company");
    EXPECT_EQ(payload.related, "AAPL");
    EXPECT_EQ(payload.id, 101);

    mgr.stop_session(session->id);
}

TEST(FinnhubNewsStreamingTest, WildcardNewsSubscriptionEmitsMarketNewsEvent) {
    CompanyNewsRecord market_news;
    market_news.datetime = make_ts_ns(3000000);
    market_news.category = "general";
    market_news.headline = "Macro headline";
    market_news.summary = "Market summary";
    market_news.source = "UnitTest";
    market_news.url = "https://example.test/news/market";
    market_news.id = 202;

    auto ds = std::make_shared<FinnhubNewsTestDataSource>(
        std::vector<CompanyNewsRecord>{},
        std::vector<CompanyNewsRecord>{market_news});
    SessionManager mgr(ds);

    SessionConfig cfg;
    cfg.symbols = {"SPY"};
    cfg.start_time = make_ts_ns(0);
    cfg.end_time = make_ts_ns(10000000);
    cfg.speed_factor = 0.0;

    auto session = mgr.create_session(cfg);
    ASSERT_NE(session, nullptr);

    std::mutex mu;
    std::condition_variable cv;
    bool got_news = false;
    Event received_event;

    mgr.add_event_callback([&](const std::string& sid, const Event& event) {
        if (sid != session->id) return;
        if (event.event_type != EventType::NEWS) return;
        std::lock_guard<std::mutex> lock(mu);
        got_news = true;
        received_event = event;
        cv.notify_all();
    });

    mgr.start_session(session->id);
    mgr.update_news_subscriptions(session->id, {"*"}, true);

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return got_news; }));
    }

    EXPECT_EQ(received_event.event_type, EventType::NEWS);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::nanoseconds>(
                  received_event.timestamp.time_since_epoch())
                  .count(),
              3000000);

    const auto& payload = std::get<NewsData>(received_event.data);
    EXPECT_EQ(payload.headline, "Macro headline");
    EXPECT_EQ(payload.category, "general");
    EXPECT_EQ(payload.id, 202);

    mgr.stop_session(session->id);
}
