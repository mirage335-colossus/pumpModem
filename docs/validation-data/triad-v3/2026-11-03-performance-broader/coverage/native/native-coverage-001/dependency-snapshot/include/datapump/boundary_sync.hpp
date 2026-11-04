#pragma once
#include "datapump/types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace datapump::boundary_sync {
inline constexpr std::size_t interval_bytes = 128;
inline constexpr std::size_t interval_bits = interval_bytes * 8;
inline constexpr std::size_t marker_bits = 192;
inline constexpr std::size_t maximum_slip_bits = 7;
inline constexpr std::size_t maximum_marker_loss_bits = 80;
inline constexpr std::size_t maximum_marker_errors = 8;
inline constexpr std::size_t marker_tail_bits = 32;
inline constexpr unsigned false_match_bits = 84;
inline constexpr std::uint8_t unknown_bit = 2;

// Exactly one marker precedes every complete coded interval. Empty input is
// empty output; no terminal marker is emitted. Input contains individual 0/1
// bits and its size must be a multiple of interval_bits.
std::size_t encoded_size(std::size_t data_bits);
Bytes insert(std::span<const std::uint8_t> bits,
             std::size_t limit = default_memory_limit);

struct Interval {
    // Unknown bits are zero in bytes only when accompanied by these masks.
    std::array<std::uint8_t, interval_bytes> bytes{};
    std::array<std::uint8_t, interval_bytes> erasures{}; // 1 for an erased byte.
    std::array<std::uint8_t, interval_bytes> erasure_bits{}; // MSB-first bit mask.
    std::uint64_t first_stream_symbol = 0;
    bool marker_recognized = false;
};

// Incremental plaintext collector. Timing and Data-mask positions belong to
// the physical receiver. Slots are 0/1 observations or unknown_bit; no unknown
// slot contributes marker evidence. Each emitted interval has a fixed width.
// Scratch is bounded by one coded interval plus a marker search neighborhood;
// output is delivered synchronously rather than retained in an internal queue.
class Collector {
public:
    using Sink = std::function<void(const Interval&)>;
    // first_stream_symbol addresses the first supplied slot; leading_missing
    // is a known missing initial marker prefix, already reflected in that
    // address. Without timed_slots, inferred marker deletions restore nominal
    // positions. With timed_slots, each supplied slot already has its exact
    // clock position (including unknown slots); marker matching never adds
    // positions that the physical receiver has already counted.
    explicit Collector(std::uint64_t first_stream_symbol = 0,
                       std::size_t leading_missing_bits = 0,
                       bool timed_slots = false);
    ~Collector();
    Collector(Collector&&) noexcept;
    Collector& operator=(Collector&&) noexcept;
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;
    void push(std::span<const std::uint8_t> bits, const Sink& sink);
    // Only a true physical stream completion authorizes filling unobserved
    // final coded slots. false retains the incomplete interval unchanged.
    // A marker with no observed coded slots never manufactures an interval.
    void finish(bool stream_complete, const Sink& sink);
    std::size_t buffered_bits() const noexcept;
    std::size_t working_bytes() const noexcept;
    bool leading_marker_recognized() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Recovery {
    // Known bits retain their values; unknown bits remain unknown_bit.
    Bytes bits;
    bool leading_marker_recognized = false;
};
// Bounded-input convenience wrapper. Incomplete final intervals are retained
// only inside Collector; this wrapper returns completed intervals. Set
// stream_complete only after the physical end rule has actually been met.
Recovery recover_stream(std::span<const std::uint8_t> wire_bits,
                        std::size_t limit = default_memory_limit,
                        std::size_t leading_missing_bits = 0,
                        bool stream_complete = false);
Bytes recover(std::span<const std::uint8_t> wire_bits,
              std::size_t limit = default_memory_limit,
              bool stream_complete = false);
}
