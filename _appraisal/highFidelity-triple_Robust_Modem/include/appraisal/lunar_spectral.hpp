#pragma once

#include "appraisal/channel.hpp"

namespace appraisal {
struct LunarSpectralPlan {
    double latent_sample_rate_hz, recurrence_seconds, maximum_horizon_seconds;
    double maximum_envelope_drift_samples;
    std::size_t fft_size, delay_taps, latent_samples, spectral_draws_bound, workspace_bytes;
    std::uint64_t first_delay_tap;
};

// This numerical model is defined at the native EME sample rate. The fixed
// FFT grid and coefficient order do not depend on the observation length.
LunarSpectralPlan lunar_spectral_plan(const LunarSpectralSpec&, double rf_hz, double sample_rate_hz,
                                    unsigned interpolation_half_width, std::uint64_t horizon_samples);
}
