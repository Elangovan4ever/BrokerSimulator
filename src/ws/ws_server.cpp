#include "ws_server.hpp"
#include <spdlog/spdlog.h>

namespace broker_sim {

WsServer::~WsServer() {
    stop();
}

void WsServer::start(uint16_t port) {
    if (running_.exchange(true)) return;
    server_.clear_access_channels(websocketpp::log::alevel::all);
    server_.init_asio();
    server_.set_open_handler([this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conns_.insert(hdl);
    });
    server_.set_close_handler([this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conns_.erase(hdl);
    });
    server_.set_fail_handler([this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conns_.erase(hdl);
    });
    server_.listen(port);
    server_.start_accept();
    thread_ = std::make_unique<std::thread>([this]() {
        spdlog::info("WS server listening on port {}", server_.get_local_endpoint());
        server_.run();
    });
}

void WsServer::stop() {
    if (!running_.exchange(false)) return;
    server_.stop_listening();
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        for (auto& c : conns_) {
            websocketpp::lib::error_code ec;
            server_.close(c, websocketpp::close::status::going_away, "shutdown", ec);
        }
        conns_.clear();
    }
    server_.stop();
    if (thread_ && thread_->joinable()) thread_->join();
}

void WsServer::broadcast(const std::string& msg) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& c : conns_) {
        websocketpp::lib::error_code ec;
        server_.send(c, msg, websocketpp::frame::opcode::text, ec);
    }
}

} // namespace broker_sim
