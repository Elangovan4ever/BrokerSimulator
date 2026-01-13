#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <spdlog/spdlog.h>
#include "matching_engine.hpp"
#include "account_manager.hpp"

namespace broker_sim {

struct Checkpoint {
    std::string session_id;
    AccountState account;
    std::unordered_map<std::string, Position> positions;
    std::unordered_map<std::string, Order> orders;
    std::unordered_map<std::string, NBBO> nbbo_cache;
    int64_t last_event_ns{0};
    int64_t checkpoint_ns{0};
    uint64_t events_processed{0};
};

inline std::string checkpoint_path(const std::string& dir, const std::string& session_id) {
    return dir + "/session_" + session_id + ".ckpt.json";
}

inline std::string wal_path(const std::string& dir, const std::string& session_id) {
    return dir + "/session_" + session_id + ".wal.jsonl";
}

inline void save_checkpoint(const Checkpoint& ckpt, const std::string& dir = "logs") {
    std::filesystem::create_directories(dir);
    nlohmann::json j;
    j["session_id"] = ckpt.session_id;
    j["checkpoint_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    j["last_event_ns"] = ckpt.last_event_ns;
    j["events_processed"] = ckpt.events_processed;

    j["account"] = {
        {"cash", ckpt.account.cash},
        {"equity", ckpt.account.equity},
        {"buying_power", ckpt.account.buying_power},
        {"regt_buying_power", ckpt.account.regt_buying_power},
        {"daytrading_buying_power", ckpt.account.daytrading_buying_power},
        {"long_market_value", ckpt.account.long_market_value},
        {"short_market_value", ckpt.account.short_market_value},
        {"initial_margin", ckpt.account.initial_margin},
        {"maintenance_margin", ckpt.account.maintenance_margin},
        {"accrued_fees", ckpt.account.accrued_fees},
        {"pdt", ckpt.account.pattern_day_trader}
    };

    nlohmann::json pos = nlohmann::json::array();
    for (const auto& kv : ckpt.positions) {
        pos.push_back({
            {"symbol", kv.second.symbol},
            {"qty", kv.second.qty},
            {"avg_entry_price", kv.second.avg_entry_price},
            {"market_value", kv.second.market_value},
            {"cost_basis", kv.second.cost_basis},
            {"unrealized_pl", kv.second.unrealized_pl}
        });
    }
    j["positions"] = pos;

    nlohmann::json ord = nlohmann::json::array();
    for (const auto& kv : ckpt.orders) {
        const auto& o = kv.second;
        ord.push_back({
            {"id", o.id},
            {"client_order_id", o.client_order_id},
            {"symbol", o.symbol},
            {"side", o.side == OrderSide::BUY ? "BUY" : "SELL"},
            {"type", static_cast<int>(o.type)},
            {"tif", static_cast<int>(o.tif)},
            {"status", static_cast<int>(o.status)},
            {"qty", o.qty.value_or(0.0)},
            {"filled_qty", o.filled_qty},
            {"limit_price", o.limit_price.value_or(0.0)},
            {"stop_price", o.stop_price.value_or(0.0)},
            {"trail_price", o.trail_price.value_or(0.0)},
            {"trail_percent", o.trail_percent.value_or(0.0)},
            {"stop_triggered", o.stop_triggered},
            {"is_maker", o.is_maker},
            {"created_at_ns", o.created_at_ns},
            {"submitted_at_ns", o.submitted_at_ns},
            {"updated_at_ns", o.updated_at_ns},
            {"filled_at_ns", o.filled_at_ns}
        });
    }
    j["orders"] = ord;

    nlohmann::json nbbo = nlohmann::json::array();
    for (const auto& kv : ckpt.nbbo_cache) {
        const auto& n = kv.second;
        nbbo.push_back({
            {"symbol", n.symbol},
            {"bid_price", n.bid_price},
            {"bid_size", n.bid_size},
            {"ask_price", n.ask_price},
            {"ask_size", n.ask_size},
            {"timestamp", n.timestamp}
        });
    }
    j["nbbo_cache"] = nbbo;

    std::string path = checkpoint_path(dir, ckpt.session_id);
    std::string tmp_path = path + ".tmp";

    std::ofstream f(tmp_path, std::ios::out | std::ios::trunc);
    if (f.is_open()) {
        f << j.dump(2);
        f.close();
        try {
            std::filesystem::rename(tmp_path, path);
            spdlog::debug("Saved checkpoint for session {} at event_ns={}", ckpt.session_id, ckpt.last_event_ns);
        } catch (const std::exception& e) {
            spdlog::error("Failed to rename checkpoint for session {}: {}", ckpt.session_id, e.what());
        }
    } else {
        spdlog::error("Failed to save checkpoint for session {}", ckpt.session_id);
    }
}

inline std::optional<Checkpoint> load_checkpoint(const std::string& session_id, const std::string& dir = "logs") {
    std::string path = checkpoint_path(dir, session_id);
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;

    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded()) {
        spdlog::warn("Failed to parse checkpoint for session {}", session_id);
        return std::nullopt;
    }

    Checkpoint ck;
    ck.session_id = session_id;
    ck.last_event_ns = j.value("last_event_ns", int64_t{0});
    ck.checkpoint_ns = j.value("checkpoint_ns", int64_t{0});
    ck.events_processed = j.value("events_processed", uint64_t{0});

    if (j.contains("account")) {
        auto a = j["account"];
        ck.account.cash = a.value("cash", 0.0);
        ck.account.equity = a.value("equity", 0.0);
        ck.account.buying_power = a.value("buying_power", 0.0);
        ck.account.regt_buying_power = a.value("regt_buying_power", 0.0);
        ck.account.daytrading_buying_power = a.value("daytrading_buying_power", 0.0);
        ck.account.long_market_value = a.value("long_market_value", 0.0);
        ck.account.short_market_value = a.value("short_market_value", 0.0);
        ck.account.initial_margin = a.value("initial_margin", 0.0);
        ck.account.maintenance_margin = a.value("maintenance_margin", 0.0);
        ck.account.accrued_fees = a.value("accrued_fees", 0.0);
        ck.account.pattern_day_trader = a.value("pdt", false);
    }

    if (j.contains("positions")) {
        for (const auto& p : j["positions"]) {
            Position pos;
            pos.symbol = p.value("symbol", "");
            pos.qty = p.value("qty", 0.0);
            pos.avg_entry_price = p.value("avg_entry_price", 0.0);
            pos.market_value = p.value("market_value", 0.0);
            pos.cost_basis = p.value("cost_basis", 0.0);
            pos.unrealized_pl = p.value("unrealized_pl", 0.0);
            if (!pos.symbol.empty()) {
                ck.positions[pos.symbol] = pos;
            }
        }
    }

    if (j.contains("orders")) {
        for (const auto& o : j["orders"]) {
            Order ord;
            ord.id = o.value("id", "");
            ord.client_order_id = o.value("client_order_id", "");
            ord.symbol = o.value("symbol", "");
            ord.side = o.value("side", "BUY") == "BUY" ? OrderSide::BUY : OrderSide::SELL;
            ord.type = static_cast<OrderType>(o.value("type", 0));
            ord.tif = static_cast<TimeInForce>(o.value("tif", 0));
            ord.status = static_cast<OrderStatus>(o.value("status", 0));
            ord.qty = o.value("qty", 0.0);
            ord.filled_qty = o.value("filled_qty", 0.0);
            double lp = o.value("limit_price", 0.0);
            if (lp > 0.0) ord.limit_price = lp;
            double sp = o.value("stop_price", 0.0);
            if (sp > 0.0) ord.stop_price = sp;
            double tp = o.value("trail_price", 0.0);
            if (tp > 0.0) ord.trail_price = tp;
            double tpct = o.value("trail_percent", 0.0);
            if (tpct > 0.0) ord.trail_percent = tpct;
            ord.stop_triggered = o.value("stop_triggered", false);
            ord.is_maker = o.value("is_maker", false);
            ord.created_at_ns = o.value("created_at_ns", int64_t{0});
            ord.submitted_at_ns = o.value("submitted_at_ns", int64_t{0});
            ord.updated_at_ns = o.value("updated_at_ns", int64_t{0});
            ord.filled_at_ns = o.value("filled_at_ns", int64_t{0});
            if (!ord.id.empty()) {
                ck.orders[ord.id] = ord;
            }
        }
    }

    if (j.contains("nbbo_cache")) {
        for (const auto& n : j["nbbo_cache"]) {
            NBBO nbbo;
            nbbo.symbol = n.value("symbol", "");
            nbbo.bid_price = n.value("bid_price", 0.0);
            nbbo.bid_size = n.value("bid_size", int64_t{0});
            nbbo.ask_price = n.value("ask_price", 0.0);
            nbbo.ask_size = n.value("ask_size", int64_t{0});
            nbbo.timestamp = n.value("timestamp", int64_t{0});
            if (!nbbo.symbol.empty()) {
                ck.nbbo_cache[nbbo.symbol] = nbbo;
            }
        }
    }

    spdlog::info("Loaded checkpoint for session {} from event_ns={}", session_id, ck.last_event_ns);
    return ck;
}

struct WalEntry {
    int64_t ts_ns{0};
    std::string event_type;
    nlohmann::json data;
};

inline std::vector<WalEntry> load_wal_entries_after(const std::string& session_id,
                                                     int64_t after_ns,
                                                     const std::string& dir = "logs") {
    std::vector<WalEntry> entries;
    std::string path = wal_path(dir, session_id);
    std::ifstream f(path);
    if (!f.is_open()) return entries;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;

        int64_t ts_ns = j.value("ts_ns", int64_t{0});
        if (ts_ns <= after_ns) continue;

        WalEntry entry;
        entry.ts_ns = ts_ns;
        entry.event_type = j.value("event", "");
        entry.data = j;
        entries.push_back(std::move(entry));
    }

    spdlog::info("Loaded {} WAL entries after ns={} for session {}", entries.size(), after_ns, session_id);
    return entries;
}

inline void truncate_wal_after_checkpoint(const std::string& session_id,
                                          int64_t checkpoint_ns,
                                          const std::string& dir = "logs") {
    std::string path = wal_path(dir, session_id);
    std::string archive_path = path + "." + std::to_string(checkpoint_ns) + ".archived";

    if (std::filesystem::exists(path)) {
        std::filesystem::rename(path, archive_path);
        spdlog::debug("Archived WAL for session {} to {}", session_id, archive_path);
    }
}

inline void cleanup_old_checkpoints(const std::string& session_id,
                                    int keep_count = 3,
                                    const std::string& dir = "logs") {
    std::vector<std::filesystem::path> archives;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        if (name.find("session_" + session_id) != std::string::npos &&
            name.find(".archived") != std::string::npos) {
            archives.push_back(entry.path());
        }
    }

    if (archives.size() <= static_cast<size_t>(keep_count)) return;

    std::sort(archives.begin(), archives.end());
    size_t to_remove = archives.size() - keep_count;
    for (size_t i = 0; i < to_remove; ++i) {
        std::filesystem::remove(archives[i]);
        spdlog::debug("Removed old archive: {}", archives[i].string());
    }
}

} // namespace broker_sim
