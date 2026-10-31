#pragma once

#include "datapump/helper_measurement.hpp"
#include "datapump/helper_startup_timing.hpp"

#include <cstddef>

namespace datapump::helper {

// Helper-only source-clock phase unwrapping. The two simultaneous straight
// pilots share the unknown RF/hop phase, so their phase difference observes
// source time modulo 1/(high_hz-low_hz). A qualified Helper startup interval
// chooses the initial branch; subsequent branches use only the previous phase
// interval and an externally declared audio-rate range. No probe enters here.
//
// The phase radii use the local Gaussian/high-SNR model of the pilot fits and
// a union-budget allocation. They are engineering statistical witnesses, not
// calibrated coverage or guarantees for an unknown differential radio path.
class HelperPhaseClock {
public:
    struct Config {
        double high_hz=2400,low_hz=550;
        double minimum_audio_scale=.9997,maximum_audio_scale=1.0003;
        double maximum_gap_seconds=.800001;
        double total_phase_failure_probability=1e-5;
        std::size_t maximum_observations=128;
        double phase_noise_radians_per_sqrt_second=0;
        double numerical_phase_half_width_radians=1e-4;
        // Known transmitter high-minus-low phase, and an externally justified
        // allowance for differential transfer phase. Zero assumes the common
        // complex path used by the startup and two-pilot measurement model.
        double transmitted_phase_difference_radians=0;
        double differential_phase_half_width_radians=0;
    };
    struct Observation {
        bool valid=false;
        double input_seconds=0,source_seconds=0;
        double lower_source_seconds=0,upper_source_seconds=0;
        double phase_half_width_seconds=0;
        std::size_t observations=0;
    };

    explicit HelperPhaseClock(Config);
    // Resets all history and accepts only a qualified finite Helper interval.
    bool seed(const HelperStartupTimingResult&) noexcept;
    Observation observe(const HelperDwellMeasurement&);
    void reset() noexcept;
private:
    Config config_;
    bool seeded_=false,have_=false,seen_=false;
    double onset_lower_=0,onset_upper_=0,last_seen_=0;
    Observation previous_;
    std::size_t count_=0;
};

} // namespace datapump::helper
