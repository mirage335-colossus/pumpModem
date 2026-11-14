#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace appraisal {

struct HfFullwaveFir {
    std::size_t first_tap_sample = 0;
    std::vector<std::complex<double>> taps;
};

struct HfFullwaveKernelDesign {
    double sample_rate_hz;
    double occupied_half_band_hz;
    double earliest_arrival_samples; // continuous vacuum time-of-flight lower bound for this port
    std::size_t modelling_delay_samples;
    std::size_t retained_late_samples; // relative to the integer vacuum bound plus modelling delay
};

struct HfFullwaveKernel {
    HfFullwaveFir fir;
    bool solver_grid_nonzero;
    double untapered_grid_mean_power;
    double tapered_grid_mean_power;
    double prearrival_energy_fraction; // raw finite-band response before the continuous bound
    double discarded_pre_energy_fraction;
    double discarded_tail_energy_fraction;
};

// RF solver values use exp(-i*omega*t), ordered by signed baseband bin
// [-N/2, N/2). The streaming convention is exp(+i*omega*t) and is obtained
// by conjugating RF responses at the same physical frequency (no bin reversal).
// Explicitly add delay before discarding precursors; do not rescale taps.
HfFullwaveKernel design_hf_fullwave_kernel(std::span<const std::complex<double>> rf_grid,
                                          const HfFullwaveKernelDesign&);

double hf_fullwave_guard_taper(double offset_hz, double sample_rate_hz, double occupied_half_band_hz);

// Evaluate the shifted FIR at any sampled baseband frequency. Positive lag
// has negative phase slope. No receiver or channel state is involved.
std::complex<double> hf_fullwave_fir_response(const HfFullwaveFir&, double normalized_frequency);

// Appraisal-only, output-time approximation between two frozen profiles.
// Profile 0 applies through transition_first, profile 1 from transition_stop;
// between them the cubic smoothstep blends both over one preserved history.
// The transition is in rendered output samples, including during tail drain.
class HfFullwaveKernelStream {
public:
    HfFullwaveKernelStream(HfFullwaveFir first, HfFullwaveFir last,
                           std::size_t transition_first, std::size_t transition_stop);

    // One output per input. Input and output may be the same buffer.
    void process(std::span<const std::complex<double>> input, std::span<std::complex<double>> output);
    // End input permanently and produce up to output.size() zero-input tail
    // samples. The return count can be smaller; remaining output is untouched.
    std::size_t drain(std::span<std::complex<double>> output);
    std::size_t remaining_tail_samples() const noexcept { return remaining_tail_samples_; }

private:
    std::complex<double> step(std::complex<double>);

    HfFullwaveFir first_;
    HfFullwaveFir last_;
    std::size_t transition_first_;
    std::size_t transition_stop_;
    std::vector<std::complex<double>> history_;
    std::size_t cursor_ = 0;
    std::size_t position_ = 0;
    std::size_t remaining_tail_samples_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
