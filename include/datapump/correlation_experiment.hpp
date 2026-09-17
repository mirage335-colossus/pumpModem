#pragma once

#include <cstddef>
#include <cstdint>

namespace datapump::simulation {

// A bounded experiment on ideal matched-correlation statistics. This does not
// run the modem, find a signal, recover bits, or produce completed receptions.
// The requested residual carrier error is measured after hypothetical
// acquisition. The correlation parameter is a prescribed template geometry,
// not a measurement of this modem's finite waveform or search bank.
struct CorrelationExperimentParameters {
    double cn0_db_hz = -39;
    double symbol_seconds = 501187.2336272725;
    double segment_seconds = 3600;
    double phase_noise_degrees_per_sqrt_second = .5;
    double residual_frequency_hz = 0;
    // Same real, normalized template correlation in every segment. Zero is an
    // orthogonal reference; one cannot distinguish the two bit templates.
    double template_correlation = 0;
    // Includes every tested alternative, including the two binary templates.
    // The union bound does not require those search alternatives to be
    // independent. The two comparison statistics share the correlation above.
    double search_hypotheses = 2;
    double false_alarm_probability = 1e-10;
    std::size_t trials = 10000;
    std::uint64_t seed = 1;
};

struct CorrelationExperimentResult {
    std::uint64_t segments = 0;
    double segment_seconds = 0;
    double expected_coherence = 0;
    // Sum of the matched signal energy / N0 across every segment.
    double expected_signal_energy = 0;
    double threshold = 0;
    double template_correlation = 0;
    std::size_t trials = 0;
    // Signal statistic exceeds the threshold and one correlated alternative.
    // These are detection events, never message RX successes.
    std::size_t detected_correct = 0;
    // Max of two correlated noise-only statistics exceeds the threshold.
    // Zero observations cannot verify an extremely small false-alarm rate.
    std::size_t noise_pair_above = 0;
    double correct_probability = 0;
    double correct_probability_low = 0;
    double correct_probability_high = 0;
    double signal_statistic_mean = 0;
    double signal_statistic_variance = 0;
    // With nonzero Wiener phase diffusion, random path energy is replaced
    // by its exact mean. The resulting detection distribution is approximate;
    // the interval above describes Monte Carlo uncertainty within that model.
    bool phase_mean_energy_approximation = false;
};

// Exact mean normalized correlation energy of a constant-envelope segment
// with Wiener phase diffusion and constant residual frequency. Frequency and
// diffusion are integrated jointly rather than multiplying their losses.
// Duration: (0, 1e18] s; diffusion: [0, 180] deg/sqrt(s); finite frequency with
// |2*pi*frequency*segment_seconds| <= 1e15 radians for portable precision.
double expected_correlation_coherence(double segment_seconds,
    double phase_noise_degrees_per_sqrt_second, double residual_frequency_hz = 0);

// O(trials) work and O(1) storage regardless of represented duration or the
// segment count. Equal-length segments cover the entire symbol, with
// K=ceil(symbol_seconds/requested_segment_seconds), L=symbol_seconds/K.
// Supported: 1..1e6 trials, 1..1e12 segments, |C/N0|<=300 dB-Hz,
// symbol/requested segment durations <=1e18 s, and matched signal energy
// <=1e24. Exceeding a limit is an error, never an omitted interval.
CorrelationExperimentResult correlation_experiment(const CorrelationExperimentParameters&);

} // namespace datapump::simulation
