#pragma once

#include "datapump/fast/profile.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace datapump::fast {

inline constexpr std::size_t sync_symbols = 64;
inline constexpr std::size_t training_symbols = 128;
inline constexpr std::size_t pilot_spacing = 32;
inline constexpr std::size_t pilot_symbols = 4;

// Both callbacks always receive exactly physical_interval_bits elements. Bits
// are MSB-first within a constellation label. Soft evidence is positive for one;
// zero is an erasure. There are no received lengths or modulation headers.
using IntervalReader = std::function<bool(std::span<std::uint8_t>)>;
using IntervalSink = std::function<void(std::span<const float>)>;

struct ModemProgress {
    bool acquired = false;
    bool physical_complete = false;
    std::uint64_t symbols = 0;
    std::uint64_t intervals = 0;
    std::uint64_t erased_intervals = 0;
    double evm = 0;
    double carrier_error_hz = 0;
    double clock_error_ppm = 0;
};

// Public wire primitives make independent constellation/filter vectors possible.
std::vector<std::complex<double>> constellation(unsigned order);
double root_raised_cosine(double symbol_time, double rolloff);
std::complex<double> sync_symbol(std::size_t index);
std::size_t interval_symbols(const Profile& profile);

class Transmitter {
public:
    Transmitter(Profile profile, IntervalReader source);
    ~Transmitter();
    Transmitter(Transmitter&&) noexcept;
    Transmitter& operator=(Transmitter&&) noexcept;
    Transmitter(const Transmitter&) = delete;
    Transmitter& operator=(const Transmitter&) = delete;
    // Ends after the pulse-shaping tail. The physical channel must subsequently
    // supply silence; TX EOF is never forwarded as receiver completion.
    std::size_t read(std::span<float> output);
    bool finished() const;
    std::uint64_t samples_generated() const;
    std::size_t workspace_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Receiver {
public:
    Receiver(Profile profile, IntervalSink sink);
    ~Receiver();
    Receiver(Receiver&&) noexcept;
    Receiver& operator=(Receiver&&) noexcept;
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    void push(std::span<const float> samples);
    // Marks an input EOF only. No missing samples, absent symbols, final
    // interval or physical completion are fabricated.
    void finish();
    const ModemProgress& progress() const;
    std::size_t workspace_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::fast
