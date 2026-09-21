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
namespace acoustic_ofdm { class Transmitter; class Receiver; }

inline constexpr std::size_t sync_symbols = 64;
inline constexpr std::size_t training_symbols = 128;
inline constexpr std::size_t pilot_spacing = 32;
inline constexpr std::size_t pilot_symbols = 4;

// Both callbacks always receive exactly physical_interval_bits elements. Bits
// are MSB-first within a constellation label. Soft evidence is positive for one;
// zero is an erasure. There are no received lengths or modulation headers.
using IntervalReader = std::function<bool(std::span<std::uint8_t>)>;
using IntervalSink = std::function<void(std::span<const float>)>;
// Optional display-only payload-symbol observation. Receiver values are actual
// normalized/equalized points before slicing; transmitter values are mapper
// outputs. Observer exceptions are ignored and cannot change transport results.
using SymbolObserver = std::function<void(std::complex<float>)>;

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
// Cartesian Gray-labelled QAM with unit average symbol energy.
std::vector<std::complex<double>> square_qam_constellation(unsigned order);
// Exact max-log squared-distance differences, positive for label bit one.
// Metrics has log2(order) elements; returns the closest Gray constellation label.
unsigned square_qam_soft_demodulate(unsigned order,std::complex<double> value,std::span<double> metrics);
double root_raised_cosine(double symbol_time, double rolloff);
std::complex<double> sync_symbol(std::size_t index);
std::size_t interval_symbols(const Profile& profile, std::size_t interval_index=0);
std::size_t total_interval_symbols(const Profile& profile, std::size_t interval_count);
std::size_t pulse_tail_symbols(const Profile& profile);
std::size_t preamble_symbols(const Profile& profile);
// Exact waveform length before physical-end silence, from local geometry only.
std::uint64_t transmission_samples(const Profile& profile, std::size_t interval_count);
// Real transmitted silence sufficient for whole-symbol absence scoring. Slow
// single-carrier groups also cover the complete marker/pilot recovery windows;
// their required tail can therefore exceed six seconds by many symbol times.
std::uint64_t end_silence_samples(const Profile& profile);

class Transmitter {
public:
    Transmitter(Profile profile, IntervalReader source, SymbolObserver observer={});
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
    std::unique_ptr<acoustic_ofdm::Transmitter> acoustic_;
};

class Receiver {
public:
    // input_observer receives actual matched-filter I/Q at approximately two
    // samples per nominal symbol, on a free-running display-only cadence. It
    // has no acquired timing, gain or carrier correction and is not evidence
    // of payload symbols. Its exceptions cannot change receiver decisions.
    // Capacity SC rates below 1000 baud may decimate internally before the
    // matched filter; callers still supply PCM and count time at
    // profile.sample_rate.
    Receiver(Profile profile, IntervalSink sink, SymbolObserver observer={},SymbolObserver input_observer={});
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
    std::unique_ptr<acoustic_ofdm::Receiver> acoustic_;
};

} // namespace datapump::fast
