#include "performance.hpp"
#include <cmath>

namespace broker_sim {

void PerformanceTracker::record(Timestamp ts, double equity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!series_.empty() && series_.back().timestamp == ts) {
        series_.back().equity = equity;
        return;
    }
    series_.push_back({ts, equity});
}

std::vector<EquityPoint> PerformanceTracker::points(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (limit == 0 || series_.size() <= limit) return series_;
    return std::vector<EquityPoint>(series_.end() - limit, series_.end());
}

PerformanceMetrics PerformanceTracker::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PerformanceMetrics out;
    if (series_.size() < 2) return out;

    double start = series_.front().equity;
    double end = series_.back().equity;
    if (start != 0.0) out.total_return = (end - start) / start;

    double peak = series_.front().equity;
    double max_dd = 0.0;
    for (const auto& p : series_) {
        if (p.equity > peak) peak = p.equity;
        double dd = peak > 0.0 ? (peak - p.equity) / peak : 0.0;
        if (dd > max_dd) max_dd = dd;
    }
    out.max_drawdown = max_dd;

    std::vector<double> rets;
    rets.reserve(series_.size() - 1);
    for (size_t i = 1; i < series_.size(); ++i) {
        double prev = series_[i - 1].equity;
        double cur = series_[i].equity;
        if (prev != 0.0) rets.push_back((cur - prev) / prev);
    }
    if (rets.size() >= 2) {
        double mean = 0.0;
        for (double r : rets) mean += r;
        mean /= static_cast<double>(rets.size());
        double var = 0.0;
        for (double r : rets) {
            double d = r - mean;
            var += d * d;
        }
        var /= static_cast<double>(rets.size() - 1);
        double stddev = std::sqrt(var);
        if (stddev > 0.0) {
            out.sharpe = mean / stddev * std::sqrt(252.0);
        }
    }
    return out;
}

} // namespace broker_sim
