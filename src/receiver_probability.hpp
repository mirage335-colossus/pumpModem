#pragma once

#include <array>
#include <cstddef>

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
    bool operator==(const ReceiverProbabilityParameters&) const = default;
};
struct ReceiverProbability {
    double acquired_correct=0, acquired_wrong=0;
    double retained_correct=0, retained_wrong=0;
    double coherent_acquired_correct=0, coherent_acquired_wrong=0;
    double coherent_retained_correct=0, coherent_retained_wrong=0;
    std::size_t trials=0;
};

// Fixed 4096 common-random-number trials. Shared observations couple the two
// bits and both detector branches; the denominator retains unfitted signal.
// Cost and storage do not grow with represented symbol duration.
ReceiverProbability receiver_probability(const ReceiverProbabilityParameters&);

} // namespace datapump::simulation::detail
