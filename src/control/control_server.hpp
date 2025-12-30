#pragma once

#include <thread>
#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <vector>
#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>
#include "../core/session_manager.hpp"
#include "../core/config.hpp"
#include "../ws/ws_controller.hpp"
#include "../core/rate_limiter.hpp"
#include <unordered_map>

namespace broker_sim {

class ControlServer : public drogon::HttpController<ControlServer> {
public:
    static const bool isAutoCreation = false;
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ControlServer::createSession, "/sessions", drogon::Post);
    ADD_METHOD_TO(ControlServer::listSessions, "/sessions", drogon::Get);
    ADD_METHOD_TO(ControlServer::getSession, "/sessions/{1}", drogon::Get);
    ADD_METHOD_TO(ControlServer::deleteSession, "/sessions/{1}", drogon::Delete);
    ADD_METHOD_TO(ControlServer::submitOrder, "/sessions/{1}/orders", drogon::Post);
    ADD_METHOD_TO(ControlServer::listOrders, "/sessions/{1}/orders", drogon::Get);
    ADD_METHOD_TO(ControlServer::account, "/sessions/{1}/account", drogon::Get);
    ADD_METHOD_TO(ControlServer::stats, "/sessions/{1}/stats", drogon::Get);
    ADD_METHOD_TO(ControlServer::eventLog, "/sessions/{1}/events/log", drogon::Get);
    ADD_METHOD_TO(ControlServer::events, "/sessions/{1}/events", drogon::Get);
    ADD_METHOD_TO(ControlServer::performance, "/sessions/{1}/performance", drogon::Get);
    ADD_METHOD_TO(ControlServer::sessionTime, "/sessions/{1}/time", drogon::Get);
    ADD_METHOD_TO(ControlServer::start, "/sessions/{1}/start", drogon::Post);
    ADD_METHOD_TO(ControlServer::pause, "/sessions/{1}/pause", drogon::Post);
    ADD_METHOD_TO(ControlServer::resume, "/sessions/{1}/resume", drogon::Post);
    ADD_METHOD_TO(ControlServer::stop, "/sessions/{1}/stop", drogon::Post);
    ADD_METHOD_TO(ControlServer::setSpeed, "/sessions/{1}/speed", drogon::Post);
    ADD_METHOD_TO(ControlServer::jumpTo, "/sessions/{1}/jump", drogon::Post);
    ADD_METHOD_TO(ControlServer::fastForward, "/sessions/{1}/fast_forward", drogon::Post);
    ADD_METHOD_TO(ControlServer::watermark, "/sessions/{1}/watermark", drogon::Get);
    ADD_METHOD_TO(ControlServer::cancel, "/sessions/{1}/orders/{2}/cancel", drogon::Post);
    ADD_METHOD_TO(ControlServer::applyDividend, "/sessions/{1}/corporate_actions/dividend", drogon::Post);
    ADD_METHOD_TO(ControlServer::applySplit, "/sessions/{1}/corporate_actions/split", drogon::Post);
    METHOD_LIST_END

    ControlServer(std::shared_ptr<SessionManager> session_mgr, const Config& cfg);

    // HTTP handlers
    void createSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void listSessions(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback);
    void getSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void deleteSession(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void submitOrder(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void listOrders(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void account(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void stats(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void eventLog(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void events(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void performance(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void sessionTime(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void start(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void pause(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void resume(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void setSpeed(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void jumpTo(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void fastForward(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void watermark(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void stop(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void cancel(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id, std::string order_id);
    void applyDividend(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);
    void applySplit(const drogon::HttpRequestPtr& req, std::function<void (const drogon::HttpResponsePtr &)> &&callback, std::string session_id);

    // event propagation
    void on_event(const std::string& session_id, const Event& ev);

private:
    std::shared_ptr<Session> resolve_session_from_param(const drogon::HttpRequestPtr& req);
    std::string resolve_symbol_from_param(const drogon::HttpRequestPtr& req);
    nlohmann::json event_to_json(const Event& ev);
    drogon::HttpResponsePtr unauthorized();
    drogon::HttpResponsePtr json_resp(nlohmann::json body, int code = 200);

    std::shared_ptr<SessionManager> session_mgr_;
    Config cfg_;

    std::mutex events_mutex_;
    std::unordered_map<std::string, std::deque<nlohmann::json>> events_;
    std::condition_variable events_cv_;
    size_t max_events_per_session_{5000};

    RateLimiter limiter_{500, std::chrono::seconds(60)};

    bool authorize(const drogon::HttpRequestPtr& req);
};

} // namespace broker_sim
