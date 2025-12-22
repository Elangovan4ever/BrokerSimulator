#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace broker_sim {

class WalLogger {
public:
    WalLogger(const std::string& path, size_t max_bytes = 50 * 1024 * 1024)
        : base_path_(path), max_bytes_(max_bytes) {
        open_stream(base_path_);
    }

    void append(const nlohmann::json& j) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!stream_.is_open()) return;
        stream_ << j.dump() << "\n";
        stream_.flush();
        current_size_ += j.dump().size() + 1;
        if (current_size_ >= max_bytes_) {
            rotate();
        }
    }

private:
    void open_stream(const std::string& p) {
        stream_.open(p, std::ios::out | std::ios::app);
        current_size_ = std::filesystem::exists(p) ? std::filesystem::file_size(p) : 0;
    }

    void rotate() {
        stream_.close();
        ++roll_idx_;
        std::string new_path = base_path_ + "." + std::to_string(roll_idx_);
        open_stream(new_path);
    }

    std::string base_path_;
    size_t max_bytes_;
    size_t current_size_{0};
    size_t roll_idx_{0};
    std::ofstream stream_;
    std::mutex mu_;
};

} // namespace broker_sim
