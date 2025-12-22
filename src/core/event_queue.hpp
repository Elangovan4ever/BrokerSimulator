#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

enum class EventType {
    TRADE,
    QUOTE,
    BAR,
    ORDER_NEW,
    ORDER_FILL,
    ORDER_CANCEL,
    ORDER_EXPIRE,
    DIVIDEND,       // Corporate action: dividend payment
    SPLIT,          // Corporate action: stock split
    HALT,           // Trading halt (circuit breaker, news, etc.)
    RESUME          // Trading resume after halt
};

struct TradeData {
    double price;
    int64_t size;
    int exchange;
    std::string conditions;
    int tape;
};

struct QuoteData {
    double bid_price;
    int64_t bid_size;
    double ask_price;
    int64_t ask_size;
    int bid_exchange;
    int ask_exchange;
    int tape;
};

struct BarData {
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
    std::optional<double> vwap;
    std::optional<int64_t> trade_count;
};

struct OrderData {
    std::string order_id;
    std::string client_order_id;
    double qty;
    double filled_qty;
    double filled_avg_price;
    std::string status;
};

struct DividendData {
    double amount_per_share;
    std::string ex_date;        // Ex-dividend date
    std::string pay_date;       // Payment date
    std::string record_date;    // Record date
};

struct SplitData {
    double from_factor;         // Original shares (e.g., 1)
    double to_factor;           // New shares (e.g., 4 for 4:1 split)
    double ratio() const { return to_factor / from_factor; }
};

struct HaltData {
    std::string reason;         // LUDP, LULD, NEWS, etc.
    std::string halt_code;      // Exchange-specific halt code
    bool is_halted;             // true = halt, false = resume
};

using EventPayload = std::variant<TradeData, QuoteData, BarData, OrderData, DividendData, SplitData, HaltData>;

struct Event {
    Timestamp timestamp;
    uint64_t sequence;
    EventType event_type;
    std::string symbol;
    EventPayload data;

    bool operator>(const Event& other) const {
        if (timestamp != other.timestamp) {
            return timestamp > other.timestamp;
        }
        return sequence > other.sequence;
    }
};

class EventQueue {
public:
    EventQueue(size_t max_size = 0, std::string overflow_policy = "block")
        : max_size_(max_size), overflow_policy_(std::move(overflow_policy)), sequence_(0) {}

    // Returns true if enqueued, false if dropped.
    bool push(Timestamp ts, EventType type, const std::string& symbol, EventPayload data) {
        if (stopped_.load(std::memory_order_acquire)) return false;
        Event ev{ts, sequence_.fetch_add(1, std::memory_order_relaxed), type, symbol, std::move(data)};
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_.load(std::memory_order_acquire)) return false;
        if (max_size_ > 0 && heap_.size() >= max_size_) {
            if (overflow_policy_ == "drop_oldest") {
                if (!heap_.empty()) heap_.pop();
            } else {
                dropped_count_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }
        heap_.push(std::move(ev));
        cv_.notify_one();
        return true;
    }

    std::optional<Event> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (heap_.empty()) return std::nullopt;
        Event ev = std::move(const_cast<Event&>(heap_.top()));
        heap_.pop();
        return ev;
    }

    std::optional<Event> wait_and_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return stopped_.load(std::memory_order_acquire) || !heap_.empty(); });
        if (stopped_.load(std::memory_order_acquire) && heap_.empty()) return std::nullopt;
        Event ev = std::move(const_cast<Event&>(heap_.top()));
        heap_.pop();
        return ev;
    }

    std::optional<Event> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (heap_.empty()) return std::nullopt;
        return heap_.top();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return heap_.size();
    }

    uint64_t dropped() const {
        return dropped_count_.load(std::memory_order_relaxed);
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return heap_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!heap_.empty()) heap_.pop();
        sequence_.store(0, std::memory_order_relaxed);
    }

    void stop() {
        stopped_.store(true, std::memory_order_release);
        cv_.notify_all();
    }

    void reset() {
        stopped_.store(false, std::memory_order_release);
    }

private:
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> heap_;
    size_t max_size_{0};
    std::string overflow_policy_{"block"};
    std::atomic<uint64_t> sequence_;
    std::atomic<uint64_t> dropped_count_{0};
    std::atomic<bool> stopped_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * Lock-free event queue for high-throughput scenarios.
 *
 * Uses a ring buffer with atomic operations for lock-free push/pop.
 * Best used when single-threaded consumption is acceptable but
 * producers need lock-free push operations.
 *
 * Note: This is a FIFO queue, not a priority queue.
 * For chronological ordering, ensure events are pushed in order
 * or use the regular EventQueue for strict ordering.
 */
class LockFreeEventQueue {
public:
    explicit LockFreeEventQueue(size_t capacity = 1024 * 1024)
        : capacity_(next_power_of_two(capacity))
        , mask_(capacity_ - 1)
        , buffer_(new Cell[capacity_])
        , sequence_(0) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~LockFreeEventQueue() {
        delete[] buffer_;
    }

    // Non-copyable
    LockFreeEventQueue(const LockFreeEventQueue&) = delete;
    LockFreeEventQueue& operator=(const LockFreeEventQueue&) = delete;

    /**
     * Add event (lock-free, multiple producers safe).
     * Returns true if successful, false if queue is full.
     */
    bool push(Timestamp ts, EventType type, const std::string& symbol, EventPayload data) {
        if (stopped_.load(std::memory_order_acquire)) return false;

        Event event{
            ts,
            sequence_.fetch_add(1, std::memory_order_relaxed),
            type,
            symbol,
            std::move(data)
        };

        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(event);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * Try to dequeue an event (lock-free).
     */
    std::optional<Event> try_pop() {
        Cell* cell;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // Queue is empty
                return std::nullopt;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        Event result = std::move(cell->data);
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return result;
    }

    /**
     * Blocking pop with timeout.
     */
    std::optional<Event> wait_and_pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (stopped_.load(std::memory_order_acquire)) return std::nullopt;
            if (auto ev = try_pop()) {
                return ev;
            }
            // Brief spin before retry
            std::this_thread::yield();
        }
        return std::nullopt;
    }

    /**
     * Approximate size (may not be exact in concurrent access).
     */
    size_t size_approx() const {
        size_t enq = enqueue_pos_.load(std::memory_order_relaxed);
        size_t deq = dequeue_pos_.load(std::memory_order_relaxed);
        return enq >= deq ? enq - deq : 0;
    }

    /**
     * Check if approximately empty.
     */
    bool empty_approx() const {
        return size_approx() == 0;
    }

    /**
     * Get capacity.
     */
    size_t capacity() const {
        return capacity_;
    }

    /**
     * Stop the queue (for shutdown).
     */
    void stop() {
        stopped_.store(true, std::memory_order_release);
    }

    /**
     * Reset the queue (clear and restart).
     */
    void reset() {
        stopped_.store(false, std::memory_order_release);
        // Note: Full reset requires synchronization
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
        sequence_.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

private:
    static size_t next_power_of_two(size_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
    }

    struct Cell {
        std::atomic<size_t> sequence;
        Event data;
    };

    const size_t capacity_;
    const size_t mask_;
    Cell* buffer_;
    std::atomic<uint64_t> sequence_;

    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) std::atomic<size_t> dequeue_pos_;
    std::atomic<bool> stopped_{false};
};

/**
 * Sorted event buffer for merging multiple lock-free queues.
 *
 * Use this when you have multiple LockFreeEventQueues feeding into
 * a single consumer that needs chronologically sorted events.
 */
class EventMerger {
public:
    explicit EventMerger(size_t buffer_capacity = 10000)
        : buffer_capacity_(buffer_capacity) {}

    /**
     * Add an event to the merge buffer.
     */
    void add(Event event) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(std::move(event));

        // Sort if buffer is getting full
        if (buffer_.size() >= buffer_capacity_) {
            sort_buffer();
        }
    }

    /**
     * Get the next event in chronological order.
     * Call after adding all events for a batch.
     */
    std::optional<Event> pop_oldest() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return std::nullopt;

        // Find oldest event
        auto oldest = std::min_element(buffer_.begin(), buffer_.end(),
            [](const Event& a, const Event& b) {
                if (a.timestamp != b.timestamp) {
                    return a.timestamp < b.timestamp;
                }
                return a.sequence < b.sequence;
            });

        Event result = std::move(*oldest);
        buffer_.erase(oldest);
        return result;
    }

    /**
     * Get all events sorted chronologically.
     */
    std::vector<Event> drain_sorted() {
        std::lock_guard<std::mutex> lock(mutex_);
        sort_buffer();
        std::vector<Event> result = std::move(buffer_);
        buffer_.clear();
        return result;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.empty();
    }

private:
    void sort_buffer() {
        std::sort(buffer_.begin(), buffer_.end(),
            [](const Event& a, const Event& b) {
                if (a.timestamp != b.timestamp) {
                    return a.timestamp < b.timestamp;
                }
                return a.sequence < b.sequence;
            });
    }

    size_t buffer_capacity_;
    std::vector<Event> buffer_;
    mutable std::mutex mutex_;
};

} // namespace broker_sim
