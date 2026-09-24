#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <complex>
#include <vector>
#include <string>

namespace datapump::simulation::detail {

// Matched-statistic approximation; no PCM, adaptive search or receiver state.
// Energies use one complex noise dimension (two real noise dimensions).
struct ReceiverProbabilityParameters {
    double signal_energy=0, seconds=1, diffusion_degrees=0, residual_frequency=0;
    double timing_coherence=1, timing_uncertainty_chips=0;
    double projection_bin_chips=0;
    bool pulse_shaping=false;
    double frequency_step_hz=0;
    int frequency_bin_min=0, frequency_bin_max=0;
    double noise_dimensions=128, coherent_dimensions=128, section_dimensions=128;
    double noise_condition=1;
    double acquisition_threshold=30, continuation_threshold=5;
    std::array<double,4> weights{.25,.25,.25,.25};
    std::array<double,4> correlations{};
    bool sections=true;
    // Complete receiver-local windows. Correlations and weights describe the
    // actual two projected templates; weights sum to one over the symbol.
    std::uint32_t differential_windows=0;
    double differential_window_seconds=0, differential_tail_seconds=0;
    std::vector<std::complex<double>> differential_correlations;
    std::vector<double> differential_weights;
    // Optional normalized weights for the alternative template. Empty means
    // equal envelopes, as in an unshaped pattern.
    std::vector<double> differential_alternative_weights;
    // Optional means of the actual (limited/projected) transmitted waveform
    // against each normalized template, divided by sqrt(total signal energy).
    // Empty selects a signal exactly proportional to the first template.
    std::vector<std::array<std::complex<double>,2>> differential_signal_coefficients;
    std::size_t requested_trials=4096;
    bool operator==(const ReceiverProbabilityParameters&) const = default;
};
struct ReceiverProbability {
    double acquired_correct=0, acquired_wrong=0;
    double retained_correct=0, retained_wrong=0;
    double coherent_acquired_correct=0, coherent_acquired_wrong=0;
    double coherent_retained_correct=0, coherent_retained_wrong=0;
    std::size_t trials=0;
    bool available=true, differential_model=false, frequency_search_approximation=false;
    std::string unsupported_reason;
    std::size_t frequency_candidates=0;
    double acquired_correct_lower=0, acquired_correct_upper=1;
    double retained_correct_lower=0, retained_correct_upper=1;
    // Admissions supplied by the local branch when both older branches fail.
    double differential_acquired_correct=0;
};

// Up to 4096 deterministic common-random-number trials. Shared observations
// couple the two bits and every detector branch; the denominator retains
// unfitted signal. Local-detector work is bounded at 4096 windows, independently
// of the represented PCM sample count. Unsupported geometry is explicit.
ReceiverProbability receiver_probability(const ReceiverProbabilityParameters&);

// Shared implementation entry point; unsupported geometry returns available
// false rather than extrapolating a probability or silently dropping a detector.
ReceiverProbability differential_receiver_probability(const ReceiverProbabilityParameters&);

} // namespace datapump::simulation::detail
