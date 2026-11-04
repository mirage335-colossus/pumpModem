#pragma once

#include "datapump/helper_delayed_buffer.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace datapump::helper {

// Standalone bounded complete-capture correction path. The producer observes
// Helper samples before release() asks for retrospective plans. It does not
// use an OS clock, acquire a pilot, or inspect a diagnostic probe. Interpolation
// is deliberately linear, after per-input-sample mixing; its band response is
// a measured limitation, not that of the production band-limited resampler.
class HelperCompleteConditioner {
public:
    using Sample=std::complex<float>;
    using CaptureBlock=HelperDelayedBuffer::CaptureBlock;
    using ReleasedBlock=HelperDelayedBuffer::ReleasedBlock;
    struct Config {
        std::uint32_t sample_rate=24000;
        std::size_t maximum_samples=115200,maximum_blocks=12;
        std::uint64_t delay_nanoseconds=2'000'000'000ULL;
        long double clock_jump_tolerance_nanoseconds=1'000'000;
    };
    struct CorrectionEpoch {
        // Receiver's estimated physical boundary, rounded to the first input
        // sample belonging to the new hop. NOT the pilot measurement aperture.
        std::uint64_t first_sample=0,end_sample=0,schedule_slot=0;
        long double anchor_input_sample=0;
        double audio_scale=1,common_frequency_hz=0;
        double audio_drift_ppm_per_second=0;
        double pilot_frequency_hz=2400,measured_pilot_phase_radians=0;
        bool phase_valid=false,clock_valid=false,model_qualified=false;
        // Diagnostic authority only: never enables/disables actual mixing.
        std::uint64_t phase_support_first_sample=0,phase_support_end_sample=0;
        double boundary_half_width_samples=0,phase_sigma_radians=0;
        double frequency_half_width_hz=0,audio_half_width_ppm=0;
        long double latest_helper_observation_end_sample=0;
        // Actual conservative monotonic end of the observations used by this
        // plan; valid correction plans must supply it, <= release frontier.
        std::uint64_t latest_helper_observation_monotonic_nanoseconds=0;
        // Conditional fitted source map and ONE immutable Helper reference.
        // Available models drive a bounded continuous clock servo. The first
        // model chooses the clock before any output; later models never reset
        // its reference. Qualification is independent of map availability.
        bool source_time_valid=false;
        long double source_seconds_at_anchor=0,source_half_width_seconds=0;
        long double reference_input_sample=0;
    };
    struct Event {
        // Missing input is one explicit event with samples_present=false;
        // it contains no synthesized payload and interpolation never crosses it.
        bool samples_present=true;
        Sample sample{};
        std::uint64_t output_sample=0;
        long double source_input_sample=0;
        long double relative_source_seconds=0;
        bool capture_timestamp_valid=false;
        long double capture_system_nanoseconds=0,capture_monotonic_nanoseconds=0;
        long double capture_uncertainty_nanoseconds=0;
        std::uint64_t capture_clock_epoch=0,release_monotonic_nanoseconds=0;
        std::uint64_t missing_first_sample=0,missing_sample_count=0;
        bool capture_time_discontinuity=false,interpolation_supported=true,segment_endpoint=false;
        bool phase_applied=false,clock_applied=false,well_supported=false;
        bool boundary_ambiguous=true,schedule_valid=false;
        std::uint64_t schedule_slot=0,correction_epoch_first_sample=0;
        long double latest_helper_observation_end_sample=0;
        std::uint64_t latest_helper_observation_monotonic_nanoseconds=0;
        bool transmitter_time_valid=false,reference_mismatch=false;
        long double transmitter_seconds=0,transmitter_uncertainty_seconds=0;
        long double reference_input_sample=0;
        // Actual integrated clock minus conditional fitted map in the SAME
        // immutable reference. No fitted/gauge alignment is removed here.
        bool clock_model_discrepancy_valid=false;
        long double clock_model_discrepancy_seconds=0;
    };
    using PlanProvider=std::function<std::span<const CorrectionEpoch>(const ReleasedBlock&)>;
    using OutputSink=std::function<void(const Event&)>;
    explicit HelperCompleteConditioner(Config);
    HelperDelayedBuffer::PushResult push(const CaptureBlock&,std::span<const Sample>);
    // Emission occurs synchronously HERE, incrementally, while capture can
    // continue. drain finishes the interpolation endpoint once the queue is empty; it
    // never bypasses a release deadline or claims later observations. Returned count
    // includes gap events. Sink/provider must not recursively call this object.
    std::size_t release(std::uint64_t now_monotonic_nanoseconds,
                        std::uint64_t helper_frontier_monotonic_nanoseconds,
                        const PlanProvider&,const OutputSink&,bool drain=false);
    std::size_t queued_samples() const noexcept{return buffer_.queued_samples();}
    std::size_t queued_blocks() const noexcept{return buffer_.queued_blocks();}
    std::size_t allocated_bytes() const noexcept;
    std::uint64_t captured_samples_processed() const noexcept{return processed_samples_;}
    std::uint64_t released_samples() const noexcept{return output_samples_;}
    std::uint64_t missing_samples() const noexcept{return missing_samples_;}
    bool reference_preserved() const noexcept{return !reference_mismatch_;}
    std::optional<long double> chosen_reference_input_sample() const noexcept {
        return have_reference_?std::optional<long double>(reference_):std::nullopt;
    }
    long double integrated_clock_at_next_input() const noexcept{return next_clock_seconds_;}
private:
    struct Point {Sample sample{};std::uint64_t input=0;long double clock=0;
        CaptureBlock capture{};CorrectionEpoch epoch{};bool epoch_available=false;
        double clock_scale=1;
        std::uint64_t release_ns=0;bool discontinuity=false;};
    Config config_;
    HelperDelayedBuffer buffer_;
    std::vector<Sample> raw_;
    Point previous_{};
    bool have_previous_=false,have_clock_=false,have_target_=false,drained_=false;
    bool have_reference_=false,reference_mismatch_=false;
    long double next_clock_seconds_=0,next_target_seconds_=0,reference_=0;
    std::uint64_t output_samples_=0,processed_samples_=0,missing_samples_=0,capture_frontier_input_=0,next_clock_input_=0,active_release_ns_=0;
    long double last_output_input_=-1;
    long double extrapolated_clock_after_gap(std::uint64_t) const noexcept;
    void emit(const Point&,const Point&,long double,const OutputSink&,bool endpoint=false);
    void finish_segment(const OutputSink&);
    void emit_gap(std::uint64_t,std::uint64_t,const ReleasedBlock&,const OutputSink&);
};
}
