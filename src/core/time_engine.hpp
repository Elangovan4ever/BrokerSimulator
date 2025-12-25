#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <spdlog/spdlog.h>

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;
using Nanoseconds = std::chrono::nanoseconds;

/**
 * Controls simulation time for a session.
 * Thread-safe for multi-threaded access.
 */
class TimeEngine {
public:
    using TimeListener = std::function<void(Timestamp)>;

    TimeEngine() = default;
    ~TimeEngine() = default;

    TimeEngine(const TimeEngine&) = delete;
    TimeEngine& operator=(const TimeEngine&) = delete;

    Timestamp current_time() const {
        return current_time_.load(std::memory_order_acquire);
    }

    void set_time(Timestamp ts) {
        current_time_.store(ts, std::memory_order_release);
        notify_listeners(ts);
    }

    void set_speed(double factor) {
        speed_factor_.store(factor, std::memory_order_release);
    }

    double speed() const {
        return speed_factor_.load(std::memory_order_acquire);
    }

    void start() {
        is_running_.store(true, std::memory_order_release);
        is_paused_.store(false, std::memory_order_release);
    }

    void pause() {
        is_paused_.store(true, std::memory_order_release);
    }

    void resume() {
        is_paused_.store(false, std::memory_order_release);
        pause_cv_.notify_all();
    }

    void stop() {
        is_running_.store(false, std::memory_order_release);
        pause_cv_.notify_all();
    }

    bool is_running() const { return is_running_.load(std::memory_order_acquire); }
    bool is_paused() const { return is_paused_.load(std::memory_order_acquire); }

    // Wait until event_time, applying speed factor; returns false if stopped.
    bool wait_for_next_event(Timestamp event_time) {
        if (!is_running()) {
            spdlog::warn("TimeEngine: not running, returning false");
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] {
                return !is_paused_.load(std::memory_order_acquire) || !is_running_.load(std::memory_order_acquire);
            });
        }
        if (!is_running()) {
            spdlog::warn("TimeEngine: stopped after pause wait, returning false");
            return false;
        }

        auto current = current_time_.load(std::memory_order_acquire);
        auto diff = std::chrono::duration_cast<Nanoseconds>(event_time - current);
        double speed = speed_factor_.load(std::memory_order_acquire);

        // Log first event timing
        static std::atomic<int> log_count{0};
        if (log_count.fetch_add(1) < 3) {
            auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
            spdlog::info("TimeEngine: event_time diff={}ms, speed={}", diff_ms, speed);
        }

        if (speed > 0.0 && diff.count() > 0) {
            auto sleep_time = Nanoseconds(static_cast<int64_t>(diff.count() / speed));
            std::this_thread::sleep_for(sleep_time);
        }
        advance_to(event_time);
        return true;
    }

    void add_listener(TimeListener listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        listeners_.push_back(std::move(listener));
    }

private:
    void advance_to(Timestamp ts) {
        Timestamp cur = current_time_.load(std::memory_order_acquire);
        while (ts > cur) {
            if (current_time_.compare_exchange_weak(cur, ts, std::memory_order_release, std::memory_order_acquire)) {
                notify_listeners(ts);
                break;
            }
        }
    }

    void notify_listeners(Timestamp ts) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        for (auto& l : listeners_) l(ts);
    }

    std::atomic<Timestamp> current_time_{Timestamp{}};
    std::atomic<double> speed_factor_{0.0}; // 0 = max speed
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};

    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;

    std::mutex listeners_mutex_;
    std::vector<TimeListener> listeners_;
};

} // namespace broker_sim
