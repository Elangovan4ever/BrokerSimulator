#include <chrono>
#include <iostream>
#include <string>
#include "../core/event_queue.hpp"

using namespace broker_sim;

int main(int argc, char* argv[]) {
    size_t events = 1000000;
    size_t capacity = 0;
    if (argc > 1) {
        events = static_cast<size_t>(std::stoull(argv[1]));
    }
    if (argc > 2) {
        capacity = static_cast<size_t>(std::stoull(argv[2]));
    }

    EventQueue queue(capacity, "drop_oldest");
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < events; ++i) {
        queue.push(Timestamp{} + std::chrono::nanoseconds(static_cast<int64_t>(i)),
                   EventType::TRADE,
                   "AAPL",
                   TradeData{100.0, 1, 0, "", 0});
    }
    size_t popped = 0;
    while (auto ev = queue.pop()) {
        (void)ev;
        ++popped;
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double seconds = elapsed / 1000.0;
    double rate = seconds > 0 ? static_cast<double>(popped) / seconds : 0.0;
    std::cout << "events=" << popped << " elapsed_ms=" << elapsed
              << " events_per_sec=" << static_cast<long long>(rate) << "\n";
    return 0;
}
