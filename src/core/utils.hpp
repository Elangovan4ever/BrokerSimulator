#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>

namespace broker_sim {

using Timestamp = std::chrono::system_clock::time_point;

/**
 * Shared utility functions to avoid code duplication across controllers.
 */
namespace utils {

/**
 * Convert timestamp to nanoseconds since epoch.
 */
inline int64_t ts_to_ns(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();
}

/**
 * Convert timestamp to milliseconds since epoch.
 */
inline int64_t ts_to_ms(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

/**
 * Convert timestamp to seconds since epoch.
 */
inline int64_t ts_to_sec(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
}

/**
 * Convert nanoseconds since epoch to Timestamp.
 */
inline Timestamp ns_to_ts(int64_t ns) {
    return Timestamp{} + std::chrono::nanoseconds(ns);
}

/**
 * Convert milliseconds since epoch to Timestamp.
 */
inline Timestamp ms_to_ts(int64_t ms) {
    return Timestamp{} + std::chrono::milliseconds(ms);
}

/**
 * Format timestamp as ISO 8601 string (e.g., "2024-01-15T10:30:00Z").
 */
inline std::string ts_to_iso(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

/**
 * Format timestamp from nanoseconds as ISO 8601 string.
 */
inline std::string ns_to_iso(int64_t ns) {
    if (ns <= 0) return "";
    return ts_to_iso(ns_to_ts(ns));
}

/**
 * Format timestamp as date string (e.g., "2024-01-15").
 */
inline std::string ts_to_date(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

/**
 * Parse ISO 8601 timestamp string to Timestamp.
 * Supports: "2024-01-15T10:30:00Z" or "2024-01-15T10:30:00"
 */
inline std::optional<Timestamp> parse_iso_ts(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return std::nullopt;
    return Timestamp{} + std::chrono::seconds(timegm(&tm));
}

/**
 * Parse date string to Timestamp.
 */
inline std::optional<Timestamp> parse_date(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) return std::nullopt;
    return Timestamp{} + std::chrono::seconds(timegm(&tm));
}

/**
 * Parse timestamp from various formats (ISO, date, or numeric epoch).
 */
inline std::optional<Timestamp> parse_ts_any(const std::string& s) {
    if (s.empty()) return std::nullopt;

    // Check if numeric
    bool all_digits = !s.empty() && std::all_of(s.begin(), s.end(),
        [](unsigned char c) { return std::isdigit(c); });

    if (all_digits) {
        int64_t v = std::stoll(s);
        if (s.size() >= 19) {
            return Timestamp{} + std::chrono::nanoseconds(v);
        } else if (s.size() >= 16) {
            return Timestamp{} + std::chrono::microseconds(v);
        } else if (s.size() >= 13) {
            return Timestamp{} + std::chrono::milliseconds(v);
        }
        return Timestamp{} + std::chrono::seconds(v);
    }

    // Try ISO format
    if (auto ts = parse_iso_ts(s)) return ts;

    // Try date format
    return parse_date(s);
}

/**
 * Generate a UUID-like ID.
 */
inline std::string generate_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (dist(gen) & 0xFFFFFFFF) << "-";
    ss << std::setw(4) << (dist(gen) & 0xFFFF) << "-";
    ss << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000) << "-";
    ss << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000) << "-";
    ss << std::setw(12) << (dist(gen) & 0xFFFFFFFFFFFFULL);
    return ss.str();
}

/**
 * Convert order side to string.
 */
inline std::string side_to_string(bool is_buy) {
    return is_buy ? "buy" : "sell";
}

/**
 * Convert position side to string.
 */
inline std::string position_side(double qty) {
    return qty >= 0 ? "long" : "short";
}

} // namespace utils
} // namespace broker_sim
