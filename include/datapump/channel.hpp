#pragma once
#include "datapump/streaming_modem.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/capture_transport.hpp"
#include <memory>
#include <functional>
#include <array>

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
    // Explicit opt-in for a caller which consumes the indexed physical PCM
    // before the ordinary logical-rate decimator. The signed frequency domain
    // must include every declared helper/data placement, including negative
    // mono images. It is checked against the physical clock and the actual
    // channel geometry; it does not certify an external radio/audio passband.
    // The ordinary logical output still contains the original, possibly
    // attenuated mixture. A caller must not treat it as corrected wide capture.
    struct PreconversionCapture {
        double minimum_hz=0,maximum_hz=0;
        std::uint32_t minimum_sample_rate=0;
    };
    static constexpr std::size_t workspace_bound=80*1024;
    // Fixed observation/presentation, including the two-record helper END
    // retry owner. Transfer 256 bytes from each optional cohort allowance;
    // both mode-specific combined reserves and the total remain unchanged.
    static constexpr std::size_t auxiliary_workspace_bound=16*1024+256;
    // Shared plot/channel reserve also holds the optional rolling helper
    // frontend. Both channel layouts prove this slack without shrinking any
    // ordinary receiver grant or increasing the existing workspace reserve.
    static constexpr std::size_t helper_cohort_workspace_bound=7*1024-256;
    // The helper-enabled physical route replaces the legacy double-precision
    // interpolation table, leaving this larger mode-specific reserve inside
    // the same 80KiB channel budget. Never use it for a legacy fallback.
    static constexpr std::size_t physical_helper_cohort_workspace_bound=10*1024-256;
    // Helper-enabled transport uses a separate physical integer-multiple sample
    // clock, then bounded anti-alias decimation back to this logical Config rate.
    // Both signals share clock error, oscillator impairments and physical AWGN.
    // Unsupported helper placements throw without changing logical geometry.
    SampledSimulationChannel(Config config, ChannelConfig channel, helper::Options helper = {},
                             double helper_occupied_bandwidth_hz = 0, HelperEnvelope envelope = {},
                             std::optional<PreconversionCapture> preconversion_capture = {});
    // Pure geometry query using the constructor's exact physical-clock
    // selection. It constructs no source, filter, receiver or DSP storage;
    // rejected intermediate plans may create bounded exception strings.
    // It certifies neither channel impairments nor an external radio passband.
    // Disabled helper selects the ordinary clock.
    static std::uint32_t planned_physical_sample_rate(const Config&,const helper::Options&,
        double helper_occupied_bandwidth_hz=0,std::optional<PreconversionCapture> = {});
    ~SampledSimulationChannel();
    SampledSimulationChannel(SampledSimulationChannel&&) noexcept;
    SampledSimulationChannel& operator=(SampledSimulationChannel&&) noexcept;
    // The constructor prepares the first burst. Call this before each later
    // source, after any idle noise. Only the waveform queue and its independent
    // start schedule reset; receiver time and both oscillators keep running.
    void begin_burst(bool helper_only = false, HelperEnvelope envelope = {});
    // Bind a prepared helper-enabled burst before a dated idle interval. The
    // idle is added to the receiver-clock delay, so the Robust source starts at
    // exactly the same coordinate as idle followed by begin_burst/read. Source
    // PCM is not consumed here. Use read(source, ...) through that idle instead
    // of read_noise: only the helper is present, on the same absolute dwell
    // calendar as the later helper. The Robust source's FHSS plan is unchanged.
    // Do not call begin_burst again afterward.
    void prepare_helper_lead(StreamingTransmitter& source,std::uint64_t additional_logical_samples);
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
    // Supply the prepared source's actual metadata/calendar reference before
    // binding a burst when scheduling or temporary source keys changed it.
    void set_helper_power_reference(helper::PowerReference reference);
    using PhysicalObserver=std::function<void(std::span<const float>,std::uint32_t,std::uint64_t)>;
    // Actual receiver tuning metadata, in the same continuous ADC coordinate
    // as PhysicalObserver. No oscillator-error or carrier-phase truth is
    // present. Each retune starts an independent phase segment; settling PCM
    // remains payload input but is not usable reference evidence.
    struct PhysicalRfSegment {
        std::uint64_t first_sample=0,settled_first_sample=0,end_sample=0,phase_segment=0;
        double rf_shift_hz=0;
        int sideband_sign=1;
    };
    using PhysicalRfObserver=std::function<void(const PhysicalRfSegment&,std::uint32_t)>;
    // Explicit sampled-channel laboratory impairment. The RF frequencies are
    // actual retunes, rather than a keyed calendar over fixed physical RF.
    // The configured channel CFO is defined at rf_shift_hz[0] and scales with
    // RF; a seeded independent unknown phase is introduced at every retune.
    // This is not a hardware tuning adapter. Configure before the first read.
    struct RfRetuneSimulation {
        std::array<double,4> rf_shift_hz{};
        double dwell_seconds=.4,settling_seconds=.012;
        std::uint64_t phase_seed=1;
        int sideband_sign=1;
        // Explicit laboratory RF fractional-frequency slope (1/second), at
        // physical ADC time t=sample/sample_rate. Adds sideband*RF*slope*t Hz
        // to the complete analytic mixture. Zero preserves the original path;
        // oscillator accuracy in ppm supplies no value for this field.
        double rf_fractional_slew_per_second=0;
    };
    // Called before decimation, with a contiguous physical-clock PCM block and
    // its first physical sample. Bounded centered-FIR lookahead may lead the
    // committed logical frontier by at most128 logical samples. Assistance
    // consumers must hold such updates until that frontier reaches them.
    void set_physical_observer(PhysicalObserver observer);
    void set_physical_rf_observer(PhysicalRfObserver observer);
    void set_rf_retune_simulation(const RfRetuneSimulation&);
    // Laboratory-only final Helper loss at an absolute physical ADC sample.
    // Configure before the first capture. The normal final 4ms Helper ramp
    // precedes this endpoint; DATA gain, source/tail duration, noise, clocks
    // and sample coordinates are unchanged. Null preserves the ordinary path.
    void set_lab_helper_end_sample(std::optional<std::uint64_t> sample);
    std::uint32_t physical_sample_rate() const;
    // Actual raw-to-logical construction witness. Legacy interpolation has no
    // such raw observation map; its result stays unknown even at equal rates.
    audio::CaptureTransportRecipe capture_transport() const noexcept;
    std::size_t helper_cohort_workspace_bytes() const;
    // Whether the enabled helper's unmodified logical decimator preserves the
    // actual payload passband. False is possible only with explicit
    // preconversion capture; assistance must use separately observed physical
    // PCM then. Disabled helpers add no decimator and return true; this is not
    // a claim about an ordinary channel's separate Nyquist/oscillator limits.
    bool logical_payload_preserved() const;
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
    // Reproducibility metadata for the optional frequency-selective stress.
    struct PerturbationMetadata {
        bool enabled=false;
        std::uint64_t seed=0;
        double doppler_reference_hz=0,slow_depth=0,fast_depth=0,band_width_hz=0;
        std::array<double,2> rates_hz{};
        std::array<double,3> band_centers_hz{};
        std::array<std::array<double,2>,3> initial_phases_radians{};
    };
    PerturbationMetadata perturbation_metadata() const;
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
