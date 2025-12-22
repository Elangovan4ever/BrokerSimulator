#include <spdlog/spdlog.h>
#include <drogon/drogon.h>
#include "core/config.hpp"
#include "core/data_source_stub.hpp"
#ifdef USE_CLICKHOUSE
#include "core/data_source_clickhouse.hpp"
#endif
#include "core/session_manager.hpp"
#include "control/control_server.hpp"
#include "control/alpaca_controller.hpp"
#include "control/polygon_controller.hpp"
#include "control/finnhub_controller.hpp"
#if USE_WEBSOCKETPP
#include "ws/ws_server.hpp"
#endif

int main(int argc, char* argv[]) {
    std::string config_path = "config/settings.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    broker_sim::Config cfg;
    broker_sim::load_config(cfg, config_path);

    spdlog::set_level(spdlog::level::info);
    spdlog::info("Broker Simulator starting. Control port={} bind={}",
                 cfg.services.control_port, cfg.services.bind_address);

    std::shared_ptr<broker_sim::DataSource> data_source;
#ifdef USE_CLICKHOUSE
    try {
        broker_sim::ClickHouseConfig ch_cfg;
        ch_cfg.host = cfg.database.host;
        ch_cfg.port = cfg.database.port;
        ch_cfg.database = cfg.database.database;
        ch_cfg.user = cfg.database.user;
        ch_cfg.password = cfg.database.password;
        auto ch = std::make_shared<broker_sim::ClickHouseDataSource>(ch_cfg);
        ch->connect();
        data_source = ch;
        spdlog::info("Using ClickHouse data source");
    } catch (const std::exception& e) {
        spdlog::warn("Falling back to stub data source: {}", e.what());
    }
#endif
    if (!data_source) {
        data_source = std::make_shared<broker_sim::StubDataSource>();
    }

    auto session_mgr = std::make_shared<broker_sim::SessionManager>(data_source, cfg.execution, cfg.fees);
    broker_sim::WsController::init(session_mgr, cfg);
    // Register Drogon controller for API/WS
    auto api_ctrl = std::make_shared<broker_sim::ControlServer>(session_mgr, cfg);
    auto alpaca_ctrl = std::make_shared<broker_sim::AlpacaController>(session_mgr, cfg);
    auto polygon_ctrl = std::make_shared<broker_sim::PolygonController>(session_mgr, cfg);
    auto finnhub_ctrl = std::make_shared<broker_sim::FinnhubController>(session_mgr, data_source, cfg);
    drogon::app().addListener(cfg.services.bind_address, cfg.services.control_port);
    drogon::app().addListener(cfg.services.bind_address, cfg.services.alpaca_port);
    drogon::app().addListener(cfg.services.bind_address, cfg.services.polygon_port);
    drogon::app().addListener(cfg.services.bind_address, cfg.services.finnhub_port);
    drogon::app().registerController(api_ctrl);
    drogon::app().registerController(alpaca_ctrl);
    drogon::app().registerController(polygon_ctrl);
    drogon::app().registerController(finnhub_ctrl);
    drogon::app().registerController(std::make_shared<broker_sim::WsController>());
    spdlog::info("Starting Drogon listeners on {}:{} (control) / {} (alpaca) / {} (polygon) / {} (finnhub)",
                 cfg.services.bind_address,
                 cfg.services.control_port,
                 cfg.services.alpaca_port,
                 cfg.services.polygon_port,
                 cfg.services.finnhub_port);
    drogon::app().run();
    return 0;
}
