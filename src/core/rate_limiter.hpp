#pragma once

#include <unordered_map>
#include <chrono>
#include <mutex>
#include <string>

namespace broker_sim {

class RateLimiter {
public:
    RateLimiter(size_t max_requests_per_window = 120, std::chrono::seconds window = std::chrono::seconds(60))
        : max_requests_(max_requests_per_window), window_(window) {}

    bool allow(const std::string& key) {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        std::lock_guard<std::mutex> lock(mu_);
        auto& entry = buckets_[key];
        if (now - entry.window_start >= window_) {
            entry.window_start = now;
            entry.count = 0;
        }
        if (entry.count >= max_requests_) return false;
        ++entry.count;
        return true;
    }

private:
    struct Bucket {
        std::chrono::steady_clock::time_point window_start{std::chrono::steady_clock::now()};
        size_t count{0};
    };
    size_t max_requests_;
    std::chrono::seconds window_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::mutex mu_;
};

} // namespace broker_sim
