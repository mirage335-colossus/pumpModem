#pragma once
#include "datapump/streaming_modem.hpp"
#include "datapump/helper_carrier.hpp"
#include <memory>
#include <functional>

namespace datapump::modem {
// Receiver-clock PCM from independent, free-running endpoints. The source is
// interpolated across a seeded fractional startup boundary; no symbol timing
// or framing metadata is passed to the receiver. Memory is bounded regardless
// of the transmission duration and reads may use any positive block size.
class SampledSimulationChannel {
public:
    struct HelperEnvelope {
        double lead_seconds,tail_seconds;
        constexpr HelperEnvelope(double lead=2,double tail=2):lead_seconds(lead),tail_seconds(tail) {}
    };
    static constexpr std::size_t workspace_bound=80*1024;
    // Guaranteed slack for bounded auxiliary observation during mode handoff.
    static constexpr std::size_t auxiliary_workspace_bound=16*1024;
    // Shared plot/channel reserve also holds the optional rolling helper
    // frontend. Both channel layouts prove this slack without shrinking any
    // ordinary receiver grant or increasing the existing workspace reserve.
    static constexpr std::size_t helper_cohort_workspace_bound=7*1024;
    // Helper-enabled transport uses a separate physical integer-multiple sample
    // clock, then bounded anti-alias decimation back to this logical Config rate.
    // Both signals share clock error, oscillator impairments and physical AWGN.
    // Unsupported helper placements throw without changing logical geometry.
    SampledSimulationChannel(Config config, ChannelConfig channel, helper::Options helper = {},
                             double helper_occupied_bandwidth_hz = 0, HelperEnvelope envelope = {});
    ~SampledSimulationChannel();
    SampledSimulationChannel(SampledSimulationChannel&&) noexcept;
    SampledSimulationChannel& operator=(SampledSimulationChannel&&) noexcept;
    // The constructor prepares the first burst. Call this before each later
    // source, after any idle noise. Only the waveform queue and its independent
    // start schedule reset; receiver time and both oscillators keep running.
    void begin_burst(bool helper_only = false, HelperEnvelope envelope = {});
    // Enabled helpers surround the unchanged source waveform with at least
    // two seconds each of helper-only PCM. Requested minima round upward to
    // complete 0.4-second dwells, and endpoints align to that physical grid.
    // Increase the post-source minimum before the physical final4ms ramp has
    // begun (including centered-FIR lookahead), for actual receiver absence,
    // filter and FFT coverage. This never reads or advances the source.
    // Disabled helpers ignore all envelope settings.
    void extend_helper_tail(double seconds);
    // Takes effect at the next burst; current sampled output stays immutable.
    void set_helper_callsign(std::string callsign);
    // Applies before the first read of the prepared burst, or to the next burst
    // if output has already begun. No extra begin_burst call is required.
    void set_helper_time_key(std::optional<Crypto> key);
    using PhysicalObserver=std::function<void(std::span<const float>,std::uint32_t,std::uint64_t)>;
    // Called before decimation, with a contiguous physical-clock PCM block and
    // its first physical sample. Bounded centered-FIR lookahead may lead the
    // committed logical frontier by at most128 logical samples. Assistance
    // consumers must hold such updates until that frontier reaches them.
    void set_physical_observer(PhysicalObserver observer);
    std::uint32_t physical_sample_rate() const;
    std::uint64_t physical_received_samples() const;
    // Enabled helper output returns a positive short read at the committed
    // payload endpoint, allowing tail extension before subsequent post PCM.
    std::size_t read(StreamingTransmitter& source, std::span<float> output,
                     std::stop_token stop = {});
    void read_noise(std::span<float> output, std::stop_token stop = {});
    std::uint64_t received_samples() const;
    // Absolute logical receiver-clock boundaries, available after the first
    // source read. Ceiling includes every output sample before the fractional
    // boundary. Source completion is distinct from helper-envelope EOF.
    std::optional<std::uint64_t> payload_begin_sample() const;
    std::optional<std::uint64_t> payload_end_sample() const;
    bool payload_finished() const;
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
