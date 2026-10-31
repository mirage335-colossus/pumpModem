#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::helper {

// Experimental received-sample likelihood for the EXISTING 160 ms auxiliary
// phase inversion. No payload decoding, pilot ramps or cross-hop RF phase.
// Each disjoint dwell has its own complex auxiliary and pilot nuisance gains.
// Conditional engineering likelihood supports are not calibrated guarantees.
class HelperInversionProfile {
public:
    static constexpr std::size_t maximum_candidates=129,maximum_history=96,maximum_bins=384;
    struct Config {
        std::uint32_t sample_rate=24000;
        double search_half_seconds=.008,grid_step_seconds=.000125;
        double aperture_half_seconds=.040,bin_seconds=.00025;
        std::size_t history_capacity=96;
        // Noise-only quadratic-form/search allowance. Plug-in received PSD,
        // channel mismatch and selection still require empirical validation.
        double false_alarm_tail=1e-5,likelihood_drop=8;
        double maximum_frequency_half_width_hz=2;
    };
    enum class Reason { accepted,invalid_input,insufficient_samples,overlapping_observation,
        no_marker_evidence,unresolved_timing,frequency_mismatch,reference_mismatch,
        unavailable_clock,clock_map_mismatch,history_full };
    struct ClockModel {
        bool available=false,reference_preserved=false;
        // Immutable receiver reference, not an absolute transmitter epoch.
        long double reference_input_sample=0;
        long double anchor_input_sample=0,source_seconds_at_anchor=0;
        double audio_scale=1,audio_drift_ppm_per_second=0;
        // Bound for this entire retained history; never divided by sqrt(N).
        double source_half_width_seconds=0,audio_half_width_ppm=0;
        std::uint64_t latest_observation_end_sample=0;
    };
    struct Input {
        std::span<const std::complex<float>> samples;
        std::uint64_t first_sample=0,capture_frontier_end_sample=0,dwell=0;
        // Complete support end of measured frequency/clock coordinates. Zero
        // conservatively labels the entire supplied capture frontier as used.
        std::uint64_t prediction_observation_end_sample=0;
        // Raw sample coordinate for the known-calendar 160 ms marker prior.
        long double predicted_marker_sample=0;
        double nominal_marker_source_seconds=0;
        double auxiliary_frequency_hz=950,frequency_half_width_hz=0;
        double audio_scale=1,audio_half_width_ppm=0;
        std::array<double,2> nuisance_frequency_hz{550,2400};
        bool project_nuisance_pilots=true;
    };
    struct Estimate {
        bool available=false,qualified=false;
        Reason reason=Reason::no_marker_evidence;
        long double marker_input_sample=0,dwell_start_input_sample=0;
        long double marker_lower_input_sample=0,marker_upper_input_sample=0;
        long double dwell_lower_input_sample=0,dwell_upper_input_sample=0;
        // Pooled offset is on the supplied relative SOURCE clock; per-dwell
        // offset is input seconds relative to predicted_marker_sample.
        double offset_seconds=0,lower_offset_seconds=0,upper_offset_seconds=0;
        // Capture/input-clock seconds, enclosing both marker and dwell-start
        // transport. Offset interval above remains source seconds for pooling.
        double conditional_half_width_seconds=0,likelihood_score=0,detection_threshold=0;
        double estimated_complex_noise_power=0,residual_frequency_hz=0;
        bool clock_model_consistent=true;
        double first_half_offset_seconds=0,second_half_offset_seconds=0;
        std::size_t observations=0,candidates=0,bins=0;
        std::uint64_t first_sample=0,end_sample=0,latest_observation_end_sample=0;
        long double reference_input_sample=0;
        bool uses_received_noise_estimate=true,clock_uncertainty_shared=true;
    };
    struct Observation {
        bool stored=false;Reason reason=Reason::invalid_input;
        Estimate dwell;
        std::size_t history_observations=0,raw_samples_processed=0;
    };
    explicit HelperInversionProfile(Config);
    Observation observe(const Input&);
    // Reexpress each ORIGINAL data profile under the current clock once;
    // predictions and reused profiles are never independent new observations.
    // The target marker may belong to an older buffered dwell.
    Estimate estimate(const ClockModel&,double nominal_target_marker_source_seconds,
                      std::uint64_t capture_frontier_end_sample);
    std::size_t observations()const noexcept{return count_;}
    // Persistent prepaid data/scratch only. Focused optimized compilation
    // also checks that method call frames stay below 16 KiB.
    std::size_t workspace_bytes()const noexcept{return sizeof(*this);}
    static constexpr std::size_t temporary_workspace_bound_bytes=16*1024;
    static const char* reason_text(Reason)noexcept;
private:
    struct Row {
        bool valid=false;std::uint64_t dwell=0,first=0,end=0,prediction_end=0;
        long double center=0;double nominal=0,audio=1,clock_radius=0,noise=0;
        double frequency_trials_log=0;
        std::array<double,maximum_candidates> score{},residual_frequency{};
    };
    Config config_;std::size_t candidates_=0,count_=0,next_=0;
    std::array<Row,maximum_history> rows_{};
    bool have_reference_=false,reference_mismatch_=false;
    long double reference_=0;
    std::uint64_t last_end_=0,frontier_=0;
    using Complex=std::complex<double>;
    std::array<Complex,maximum_bins> y_{},projected_{},temporary_{};
    std::array<std::array<Complex,maximum_bins>,2> nuisance_{};
    std::array<std::array<Complex,maximum_bins>,3> carrier_{};
    std::array<double,maximum_bins> roots_{},times_{},envelope_{};
};
}
