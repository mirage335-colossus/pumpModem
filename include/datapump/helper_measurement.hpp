#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <span>
#include <optional>
#include <vector>

namespace datapump::helper {

// Frequencies are signed complex-baseband Hz in the receiver's nominal sample
// clock. The complete initial high-pilot search must fit the capture Nyquist
// interval. audio_error_bound_ppm controls the low-pilot search separation.
struct HelperMeasurementConfig {
    double sample_rate = 0;
    double low_hz = 550;
    double high_hz = 2400;
    double high_search_min_hz = 0;
    double high_search_max_hz = 0;
    double audio_error_bound_ppm = 100;
    // Declared relative COMMON oscillator phase diffusion, radians/sqrt(s),
    // in nominal capture seconds. Zero explicitly assumes no such process.
    // Independent sample-clock frequency acceleration is not modeled here.
    double phase_noise_radians_per_sqrt_second = 0;
};

struct HelperDwellMeasurement {
    bool valid = false;
    double time_seconds = 0; // midpoint of captured aperture
    double first_sample_time_seconds = 0, end_sample_time_seconds = 0;
    double shift_hz = 0; // signed RF shift coefficient, including sideband
    double high_frequency_hz = 0, low_frequency_hz = 0;
    // Complex pilot phases at the first supplied sample, radians modulo 2 pi.
    // These observations have no authority over an unobserved subsequent hop.
    double high_phase_radians = 0, low_phase_radians = 0;
    // White-residual/numerical marginal errors. The common Wiener process
    // remains separate because its covariance cancels in pilot separation.
    double high_sigma_hz = 0, low_sigma_hz = 0;
    double common_phase_frequency_sigma_hz = 0;
    double high_cn0_db_hz = 0, low_cn0_db_hz = 0;
};

struct HelperMeasurementSnapshot {
    bool valid = false;
    std::size_t dwell_count = 0;
    double time_seconds = 0;
    // At time_seconds: f_observed = (1 + audio_ppm*1e-6)*f_nominal
    // + shift_ppm*1e-6*shift_hz. Both errors are relative link quantities.
    double audio_ppm = 0, shift_ppm = 0;
    double audio_drift_ppm_per_second = 0, shift_drift_ppm_per_second = 0;
    // Conditional 1-sigma errors, including configured common Wiener phase
    // diffusion in independent apertures. Static fractional drift is linear;
    // abrupt changes and frequency acceleration are outside this model.
    double audio_sigma_ppm = 0, shift_sigma_ppm = 0;
    double audio_shift_covariance_ppm2 = 0;
    // Coordinates: audio ppm, shift ppm, audio ppm/s, shift ppm/s, all dated
    // at time_seconds. Preserve cross terms when predicting a later date.
    std::array<std::array<double,4>,4> covariance{};
};

struct HelperFrequencyPrediction {
    bool valid = false;
    double time_seconds = 0, frequency_hz = 0;
    // Uncertainty of the fitted mean at the requested date, including drift
    // covariance. Conditional Gaussian engineering errors, not calibrated
    // coverage or a future oscillator-stability guarantee.
    double mean_sigma_hz = 0;
    // Optional independent future aperture's common Wiener frequency error.
    // Combine this and the NEW measurement's thermal error for a residual
    // comparison; a state estimate's sigma alone does not cover that residual.
    double aperture_process_sigma_hz = 0;
};

// Phase discontinuity at a known hop boundary, after subtracting the two
// locally fitted constant-frequency phase advances. Invalid if either aperture
// is missing or the boundary does not lie between them. The result is modulo
// 2*pi, conditional on the phase-evolution model and boundary timing.
std::optional<double> helper_hop_phase_change(const HelperDwellMeasurement& previous,
                                              const HelperDwellMeasurement& current,
                                              double boundary_seconds);

// Independent pilot-only measurement receiver. Input is complex I/Q containing
// the entire search interval; no acquisition center is inferred from truth.
// Each call supplies one settled aperture in a known dwell, with random phase
// acquired independently. The linear clock model is fitted only to observations
// within this instance, not extrapolated as an oscillator-stability guarantee.
class HelperMeasurement {
public:
    explicit HelperMeasurement(HelperMeasurementConfig config);
    HelperDwellMeasurement observe(std::span<const std::complex<float>> samples,
                                   double shift_hz, double first_sample_time_seconds);
    HelperMeasurementSnapshot snapshot() const;
    HelperFrequencyPrediction predict(double nominal_hz, double shift_hz,
        double time_seconds, double aperture_seconds = 0) const;
    void reset();
private:
    HelperMeasurementConfig config_;
    std::vector<HelperDwellMeasurement> observations_;
};
}
