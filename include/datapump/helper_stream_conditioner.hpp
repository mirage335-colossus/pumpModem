#pragma once

#include "datapump/resampler.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace datapump::helper {

// A small streaming composition of the production adjustable Resampler and a
// continuous complex NCO. It does not acquire a pilot or qualify estimates.
// For input frequency a*f+b, it mixes by -b in INPUT time and samples the
// result with input step 1/a. No probe-derived synchronization belongs here.
class HelperStreamConditioner {
public:
    static constexpr std::size_t block_capacity=256;
    static constexpr std::size_t clock_history_capacity=16;
    struct Config {
        std::uint32_t sample_rate=0;
        std::uint64_t first_input_sample=0,first_output_sample=0;
        // The caller's Helper-measured time origin or an independently
        // justified external timing prior, never a probe-derived/true
        // simulator source timestamp. The conditioner does not resolve the
        // two-pilot phase ambiguity. This clock is
        // integrated continuously as audio_scale changes; it is never
        // recomputed as a_current * an absolute input timestamp.
        long double clock_time_at_first_input_seconds=0;
        double fractional_input_sample=0;
        // Numerical agreement required between the Helper-intended source
        // clock and the ACTUAL production resampler output map. This is not
        // an estimate of the Helper clock's physical uncertainty.
        double maximum_clock_error_seconds=1e-8;
        // Optional standalone A/B baseline. False retains the original
        // scalar phase calculation; true uses a bounded unit-phasor recurrence.
        bool use_recursive_oscillator=true;
    };
    struct Correction {
        double audio_scale=1,common_frequency_hz=0;
        // Production Resampler slew in fractional input step per output sec.
        double maximum_slew_per_second=.001;
    };
    struct LocalPhase {
        long double anchor_input_sample=0;
        // Absolute phase of the NCO multiplier at anchor_input_sample, not an
        // increment. E.g. 2*pi*f_pilot*clock_time_at_input(anchor)-pilot_phase.
        double correction_phase_radians=0;
        std::uint64_t support_first_sample=0,support_end_sample=0,captured_end_sample=0;
    };
    struct Progress {
        std::size_t consumed=0,produced=0;
        std::uint64_t output_first_sample=0;
        long double source_first_sample=0,source_last_sample=0;
        // Local pilot phase authority and time-map agreement are distinct.
        // A caller asserting complete correction must require both.
        bool phase_supported=false,timing_supported=false,correction_supported=false;
        bool timing_available=false;
        double maximum_timing_error_seconds=0;
    };

    HelperStreamConditioner(Config,Correction);
    HelperStreamConditioner(const HelperStreamConditioner&)=delete;
    HelperStreamConditioner& operator=(const HelperStreamConditioner&)=delete;
    HelperStreamConditioner(HelperStreamConditioner&&)=delete;
    HelperStreamConditioner& operator=(HelperStreamConditioner&&)=delete;
    void update(Correction);
    // May be installed for a buffered, already observed hop before its samples
    // are emitted. No past output is revised. The anchor is applied at the
    // first unprocessed sample in its support; authority expires at its end.
    // The caller qualifies the captured frontier and the single-hop support;
    // these numeric fields cannot establish that physical evidence themselves.
    bool set_phase(const LocalPhase&) noexcept;
    void clear_phase() noexcept;
    // Remove both authority and the old hop's multiplier. The following
    // phase is unknown until set_phase supplies a fresh one; the caller must
    // explicitly mark output in that interval as phase-uncorrected.
    void clear_phase_and_multiplier() noexcept;
    long double clock_time_at_input(long double absolute_input_sample) const;
    bool phase_supported_at(long double source_input_sample) const noexcept;
    // Signed Helper-intended clock minus nominal output date, using the
    // returned ACTUAL resampler source coordinate. Unknown pruned history is
    // unavailable, never extrapolated backward with the newest rate.
    std::optional<long double> timing_error_at(long double source_input_sample,
                                              std::uint64_t output_sample) const noexcept;
    bool timing_supported_at(long double source_input_sample,
                             std::uint64_t output_sample) const noexcept;

    // Repeat with unconsumed input. Absolute input dates must be contiguous.
    // source_coordinates, when present, receives the exact production
    // resampler source coordinate for each returned complex output sample.
    Progress process(std::span<const std::complex<float>> input,
                     std::span<std::complex<float>> output,
                     std::uint64_t first_input_sample,bool end=false,
                     std::span<long double> source_coordinates={});
    std::uint64_t next_input_sample() const noexcept {return next_input_sample_;}
    std::uint64_t next_output_sample() const noexcept {return next_output_sample_;}
    long double next_source_sample() const noexcept;
    bool finished() const noexcept;
    double lookahead_seconds() const noexcept;
    std::size_t workspace_bytes() const noexcept;
private:
    struct MixerState {
        std::complex<double> phasor{1,0};
        double phase=0; // used only by the scalar comparison path
        bool pending=false,active=false;
        LocalPhase phase_reference{};
        std::uint64_t active_first=0,active_end=0;
    };
    static void validate(Correction);
    void advance(MixerState&,std::uint64_t) const noexcept;
    struct ClockSegment {std::uint64_t first=0;long double seconds=0;double scale=1;};
    std::optional<long double> clock_time(long double sample) const noexcept;
    void prune_clock_history() noexcept;
    Config config_;
    Correction correction_;
    audio::Resampler real_,imaginary_;
    std::uint64_t next_input_sample_=0,next_output_sample_=0;
    std::array<ClockSegment,clock_history_capacity> clock_history_{};
    std::size_t clock_history_size_=0;
    MixerState mixer_;
    std::array<float,block_capacity> real_input_{},imaginary_input_{},real_output_{},imaginary_output_{};
    std::array<MixerState,block_capacity> after_{};
    std::array<long double,block_capacity> source_coordinates_{};
    std::size_t real_dates_=0,imaginary_dates_=0;
};
}
