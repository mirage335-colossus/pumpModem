#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <span>

namespace datapump::helper {

// Experimental bounded schedule-epoch estimator. Settled pilot estimates on
// each side supply independent complex hop gains; no RF phase is propagated
// across the boundary. Edge samples are scored against the known pilot ramps.
// This is NOT an absolute two-pilot phase/timing ambiguity qualification.
class HelperBoundaryTracker {
public:
    static constexpr std::size_t maximum_candidates=129, maximum_boundaries=32;
    static constexpr std::size_t maximum_edge_samples=4096;
    static constexpr std::size_t maximum_scratch_bytes=
        2*maximum_edge_samples*sizeof(std::complex<double>)+maximum_candidates*sizeof(double);
    struct Config {
        double sample_rate=24000;
        double search_half_width_seconds=.008;
        double grid_seconds=.00025;
        double ramp_seconds=.004;
        std::size_t retained_boundaries=16;
    };
    struct Pilot {
        bool valid=false;
        double anchor_sample=0,frequency_hz=0;
        std::complex<double> amplitude_at_anchor{};
        double phase_sigma_radians=0,frequency_sigma_hz=0;
    };
    struct Hop {
        std::array<Pilot,2> pilots{}; // high and low, independently measured
        double observation_first_sample=0,observation_end_sample=0;
    };
    struct Input {
        // Samples must cover the complete search interval plus both ramps.
        std::span<const std::complex<float>> samples;
        double first_sample=0;
        // Schedule prediction with relative clock evolution applied. Every
        // input uses the SAME retained unknown epoch; only its rate evolves.
        double predicted_boundary_sample=0;
        double audio_scale=1;
        Hop before,after;
        // Actual captured frontier, including both settled observations.
        double latest_observation_end_sample=0;
    };
    struct Result {
        bool available=false;
        double boundary_sample=0,epoch_offset_samples=0;
        // The original prior range is deliberately retained. The profile is
        // data-dependent and uses noisy adjacent-hop templates shared between
        // rows: its curvature is NOT an independently calibrated covariance.
        double prior_first_sample=0,prior_end_sample=0;
        double latest_observation_end_sample=0;
        double score_improvement=0;
        std::size_t boundaries=0,candidates=0,sample_products=0;
    };

    explicit HelperBoundaryTracker(Config);
    Result observe(const Input&);
    // Persistent object and bounded per-call stack scratch are separate; this
    // total is their simultaneous maximum, excluding caller-owned samples.
    std::size_t workspace_bytes() const noexcept { return sizeof(*this)+maximum_scratch_bytes; }
    std::size_t persistent_bytes() const noexcept { return sizeof(*this); }

    // Guarded Hann correlation at an already acquired DATA-ONLY frequency.
    // This does not count another frequency/phase observation or update any
    // oscillator history. The same retained aperture should be supplied for
    // both pilots, and must not overlap the boundary window.
    static Pilot settled_pilot(std::span<const std::complex<float>> samples,
                               double first_sample,double sample_rate,
                               double frequency_hz,double phase_sigma_radians,
                               double frequency_sigma_hz);
private:
    Config config_;
    std::array<std::array<double,maximum_candidates>,maximum_boundaries> scores_{};
    std::array<double,maximum_candidates> total_{};
    std::size_t grid_count_=0,head_=0,count_=0;
    double last_boundary_=0,last_edge_end_=0;
    bool have_last_=false;
};
}
