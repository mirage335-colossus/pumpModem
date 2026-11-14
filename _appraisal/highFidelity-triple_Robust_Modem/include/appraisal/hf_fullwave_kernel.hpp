#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
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

struct HfFullwavePhaseBlend {
    std::complex<double> lower_weight;
    std::complex<double> upper_weight;

    std::complex<double> apply(std::complex<double> lower, std::complex<double> upper) const {
        return lower_weight * lower + upper_weight * upper;
    }
};

// Remove the endpoint carrier phases, interpolate the residual complex pair,
// and restore the interpolated carrier phase. The analytic phases are never
// extracted from the amplitudes. Pass their negatives for native FIR taps.
HfFullwavePhaseBlend hf_fullwave_phase_blend(double lower_phase, double upper_phase, double fraction);

// Evaluate the shifted FIR at any sampled baseband frequency. Positive lag
// has negative phase slope. No receiver or channel state is involved.
std::complex<double> hf_fullwave_fir_response(const HfFullwaveFir&, double normalized_frequency);

enum class HfFullwavePort : std::size_t { reflection, transmission };

struct HfFullwaveBandError {
    double maximum = -1.;
    double profile_q = 0.;
    double offset_hz = 0.;
};

struct HfFullwaveKernelTableChecks {
    std::array<HfFullwaveBandError, 2> knot_grid, knot_offgrid, profile_holdout;
    std::array<HfFullwaveBandError, 2> witness_frequency_refinement, witness_mesh;
    std::array<double, 2> maximum_discarded_energy{}, maximum_prearrival_energy{};
    std::array<double, 2> worst_profile_phase_rate_rad_per_q{}, largest_observed_phase_step_rad{};
    std::array<std::size_t, 2> weak_phase_pairs{}, ambiguous_phase_pairs{}, observed_phase_pairs{};
    std::array<double, 2> peak_error_seconds_per_q{}, carrier_flight_radians_seconds_per_q{};
    double maximum_frozen_flux_excess = 0.;
    std::size_t rf_solves = 0;
    std::size_t knot_profiles_checked = 0, holdout_profiles_checked = 0;
    bool witnesses_qualified = false;
    bool qualified = false;
};

class HfFullwaveKernelTable;
struct HfFullwaveKernelTableBuild {
    std::shared_ptr<const HfFullwaveKernelTable> table;
    HfFullwaveKernelTableChecks checks;
};

// Fixed, appraisal-only constructed path from F/RF=.9999 to 1.0001 and D
// peak density 8e8 to 8e9 m^-3. The table exists only after its qualification.
class HfFullwaveKernelTable {
public:
    static constexpr double carrier_hz = 3.4e6, sample_rate_hz = 1920., occupied_half_band_hz = 768.;
    static constexpr std::size_t intervals = 64, taps_per_port = 129, modelling_delay_samples = 32;
    static constexpr std::size_t frequency_bins = 256, frequency_holdouts = 65, mesh_witnesses = 5;
    static constexpr std::size_t maximum_rf_solves = 32070;
    static constexpr double complex_tolerance = .005, discarded_energy_tolerance = 1e-6;
    static constexpr double rate_peak_tolerance = .005, phase_flight_reference_radians = .1;
    static constexpr double phase_observation_min_amplitude = .05;
    static constexpr std::array transition_durations_seconds{120., 240., 480.};

    std::size_t first_lag_samples(HfFullwavePort) const;
    std::size_t last_lag_samples(HfFullwavePort) const;
    static std::size_t planned_first_lag_samples(HfFullwavePort);
    static constexpr std::size_t preparation_workspace_bound_bytes = 1024u * 1024u;
    std::size_t workspace_bytes() const noexcept;

    // For the clamped half-cosine trajectory, bound the difference between
    // output-time and per-tap retarded coefficients, in units of peak input.
    double retarded_peak_error_bound(HfFullwavePort, double duration_seconds) const;
    double carrier_flight_phase_bound(HfFullwavePort, double duration_seconds) const;
    std::complex<double> coefficient_at_q(HfFullwavePort, std::size_t lag_sample, double q) const;

private:
    HfFullwaveKernelTable() = default;
    friend HfFullwaveKernelTableBuild build_hf_fullwave_kernel_table(std::stop_token);
    friend class HfFullwaveProfileStream;

    struct Knot {
        std::array<HfFullwaveFir, 2> fir;
        std::array<double, 2> native_phase{};
    };
    std::array<Knot, intervals + 1> knots_;
    std::array<double, 2> peak_error_seconds_per_q_{}, carrier_flight_radians_seconds_per_q_{};
};

HfFullwaveKernelTableBuild build_hf_fullwave_kernel_table(std::stop_token = {});
std::array<double, HfFullwaveKernelTable::frequency_holdouts> hf_fullwave_table_holdout_offsets_hz();
double hf_fullwave_frozen_flux_tolerance();

// One forward output clock and preserved causal input history. Pass input at
// the 1920 Hz channel sample rate. transition_first is on the physical output
// clock before the declared modeling delay, with q clamped at both endpoints.
// A nonzero initial_output_sample starts with zero prior input at that clock.
class HfFullwaveProfileStream {
public:
    HfFullwaveProfileStream(std::shared_ptr<const HfFullwaveKernelTable>, HfFullwavePort,
                            std::size_t transition_first, double duration_seconds,
                            std::size_t initial_output_sample = 0);

    std::complex<double> step(std::complex<double> input);
    void process(std::span<const std::complex<double>> input, std::span<std::complex<double>> output);
    std::size_t drain(std::span<std::complex<double>> output);
    std::size_t tail_samples() const noexcept { return history_.size() - 1; }
    std::size_t workspace_bytes() const noexcept;

private:
    std::complex<double> render(std::complex<double> input);

    std::shared_ptr<const HfFullwaveKernelTable> table_;
    std::size_t port_index_;
    std::size_t transition_first_;
    double duration_samples_;
    std::vector<std::complex<double>> history_;
    std::size_t position_ = 0, cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
