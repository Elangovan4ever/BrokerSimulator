#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <set>
#include <mutex>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace broker_sim {

class WsServer {
public:
    WsServer() = default;
    ~WsServer();

    void start(uint16_t port);
    void stop();
    void broadcast(const std::string& msg);

private:
    using server_t = websocketpp::server<websocketpp::config::asio>;
    server_t server_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> conns_;
    std::mutex conn_mutex_;
};

} // namespace broker_sim
