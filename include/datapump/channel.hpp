#pragma once
#include "datapump/streaming_modem.hpp"
#include <memory>

namespace datapump::modem {
// Receiver-clock PCM from independent, free-running endpoints. The source is
// interpolated across a seeded fractional startup boundary; no symbol timing
// or framing metadata is passed to the receiver. Memory is bounded regardless
// of the transmission duration and reads may use any positive block size.
class SampledSimulationChannel {
public:
    static constexpr std::size_t workspace_bound=80*1024;
    SampledSimulationChannel(Config config, ChannelConfig channel);
    ~SampledSimulationChannel();
    SampledSimulationChannel(SampledSimulationChannel&&) noexcept;
    SampledSimulationChannel& operator=(SampledSimulationChannel&&) noexcept;
    // The constructor prepares the first burst. Call this before each later
    // source, after any idle noise. Only the waveform queue and its independent
    // start schedule reset; receiver time and both oscillators keep running.
    void begin_burst();
    std::size_t read(StreamingTransmitter& source, std::span<float> output,
                     std::stop_token stop = {});
    void read_noise(std::span<float> output, std::stop_token stop = {});
    std::uint64_t received_samples() const;
    // Source time already traversed by output PCM, excluding interpolation
    // lookahead. For transmitter progress only; never receiver acquisition.
    std::uint64_t transmitted_samples() const;
    std::size_t working_bytes() const;
    // Channel diagnostics only. A receiver must acquire these from its PCM.
    double startup_offset_samples() const;
    double carrier_phase_radians() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Accelerated matched-filter channel. Timing boundaries accumulate relative
// crystal error; carrier offset is integrated analytically, including loss of
// coherence within a long observation. Wiener phase endpoints are joined by
// linear ramps on at most 16 subintervals per observation. Work is independent
// of the number of carrier cycles or hours represented by an observation.
// This models the existing matched chip statistic, not a chip-clock tracking
// loop. It does not compensate oscillator error in the receiver. This is an
// analytical/test helper, not the free-running sampled simulation transport.
class SimulationChannel {
public:
    static constexpr std::size_t workspace_bound=80*1024;
    SimulationChannel(Config config, ChannelConfig channel);
    ~SimulationChannel();
    SimulationChannel(SimulationChannel&&) noexcept;
    SimulationChannel& operator=(SimulationChannel&&) noexcept;
    std::optional<SymbolObservation> process(SymbolObservation observation);
    // Delay/no-signal intervals are measured on the receiver clock and do not
    // advance the transmitter trajectory.
    SymbolObservation noise(std::uint64_t sample_count);
    std::uint64_t received_samples() const;
    // Exclusive receiver timestamp at the end of preview_last(). Eight source
    // samples of lookahead keep its interpolation taps inside known history.
    std::uint64_t preview_end_samples() const;
    double phase_noise_radians() const;
    std::size_t working_bytes() const;
    // Actual recent source PCM, time-warped and phase-rotated using the same
    // stored phase trajectory as process(). Preview noise has its own seeded
    // sample-indexed generator: plotting never changes decoder randomness.
    // At most 2048 samples, called at the current source/channel position.
    void preview_last(const StreamingTransmitter& source, std::span<float> output) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
