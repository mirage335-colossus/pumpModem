#pragma once
#include "datapump/types.hpp"
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <span>

namespace datapump {
struct ReceivedItem {
    std::string id;
    std::string filename;
    Bytes data;
    bool validated = false;
    bool authenticated = false;
};
// The cache owns payloads in RAM; eviction never writes received content to disk.
class ReceiveCache {
public:
    explicit ReceiveCache(std::size_t capacity=default_memory_limit);
    void put(ReceivedItem item);
    bool erase(const std::string& id);
    std::optional<ReceivedItem> get(const std::string& id, bool validated_only=false) const;
    std::size_t size_bytes() const;
private:
    std::size_t capacity_, used_=0;
    std::deque<ReceivedItem> items_;
    mutable std::mutex mutex_;
};
class TransmitGate {
public:
    using Clock=std::chrono::steady_clock;
    explicit TransmitGate(std::chrono::milliseconds delay=std::chrono::seconds(6));
    void started(Clock::time_point now=Clock::now());
    void finished(Clock::time_point now=Clock::now());
    std::chrono::milliseconds remaining(Clock::time_point now=Clock::now()) const;
private:
    std::chrono::milliseconds delay_;
    std::optional<Clock::time_point> next_;
    bool active_=false;
};
struct FrequencyBand { std::string name; double low_hz, high_hz; };
class BandSchedule {
public:
    BandSchedule(std::vector<FrequencyBand> bands, std::uint32_t dwell_seconds);
    const FrequencyBand& at(std::uint64_t elapsed_seconds) const;
private:
    std::vector<FrequencyBand> bands_;
    std::uint32_t dwell_;
};
std::vector<std::uint64_t> drift_candidates(std::uint64_t center, unsigned window, bool encrypted);
std::uint64_t sample_nanoseconds(std::uint64_t sample, std::uint32_t sample_rate);
std::string json_escape(std::string_view text);
std::string base64_encode(std::span<const std::uint8_t> bytes);
// Escape terminal controls while leaving ordinary UTF-8 and pipe bytes alone.
std::string terminal_text(std::span<const std::uint8_t> bytes);
Bytes read_bounded(std::istream& input, std::size_t limit);
void write_new_file(const std::string& path, std::span<const std::uint8_t> bytes);
}
