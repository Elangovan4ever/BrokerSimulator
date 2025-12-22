#pragma once

#include <vector>
#include <chrono>
#include <mutex>

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

struct EquityPoint {
    Timestamp timestamp;
    double equity;
};

struct PerformanceMetrics {
    double total_return{0.0};
    double max_drawdown{0.0};
    double sharpe{0.0};
};

class PerformanceTracker {
public:
    void record(Timestamp ts, double equity);
    std::vector<EquityPoint> points(size_t limit = 0) const;
    PerformanceMetrics metrics() const;

private:
    mutable std::mutex mutex_;
    std::vector<EquityPoint> series_;
};

} // namespace broker_sim
