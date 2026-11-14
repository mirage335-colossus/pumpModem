#pragma once

#include "appraisal/hf_fullwave.hpp"
#include "appraisal/hf_fullwave_kernel.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace appraisal {

enum class HfMagnetoionicPath { tilted_lower, parallel_upper };

struct HfMagnetoionicScaleWitness {
    double relative_error = -1., raw_amplitude = -1., absolute_error = -1.;
    double profile_q = 0., offset_hz = 0.;
};

struct HfMagnetoionicScaleChecks {
    HfMagnetoionicScaleWitness minimum_raw_amplitude, maximum_relative_error, raw_at_maximum_absolute_error;
    std::size_t samples = 0, exact_zero_raw = 0, absolute_error_at_least_raw = 0;
};

struct HfMagnetoionicProjectionWitness {
    double fraction_of_vector_power = -1., vector_power = 0.;
    double profile_q = 0., offset_hz = 0.;
};

struct HfMagnetoionicPolarizationWitness {
    std::array<double, 3> normalized_stokes{}; // S1, S2, S3 in the RF convention
    double overlap_power_with_initial = -1., vector_power = 0.;
};

inline constexpr std::array hf_magnetoionic_polarization_offsets_hz{-384., 0., 384.};

// One previously exposed near-null aperture, held out of both profile grids.
// Mesh errors are sampled differences rather than continuous-profile bounds.
struct HfMagnetoionicNullWitness {
    double profile_q = 0., offset_hz = 0., fine_vector_power = 0.;
    std::array<double, 2> fine_amplitude{}, fir_amplitude{}, fir_to_production_mesh_error{},
        production_to_fine_mesh_error{}, intermediate_to_fine_mesh_error{}, fir_to_fine_mesh_error{};
    double x_guarded_absolute_error = 0.;
    std::array<double, 2> x_guarded_power_interval{};
    bool x_resolved = false;
};

struct HfMagnetoionicKernelChecks {
    // Index 0 is laboratory x and 1 is laboratory y, for a fixed unit x launch.
    std::array<HfFullwaveBandError, 2> knot_grid, knot_offgrid, profile_holdout;
    std::array<HfFullwaveBandError, 2> witness_frequency_refinement, witness_mesh;
    std::array<HfMagnetoionicScaleChecks, 2> knot_offgrid_scale, profile_holdout_scale, witness_mesh_scale;
    std::array<HfMagnetoionicProjectionWitness, 2> minimum_occupied_projection_fraction, maximum_occupied_projection_fraction;
    std::array<std::vector<HfMagnetoionicPolarizationWitness>, hf_magnetoionic_polarization_offsets_hz.size()> knot_polarization;
    std::optional<HfMagnetoionicNullWitness> original_null_witness;
    std::array<double, 2> maximum_discarded_energy{}, peak_error_seconds_per_q{};
    std::array<double, 3> minimum_carrier_power{1., 1., 1.}, maximum_carrier_power{}; // x, y, x+y
    double maximum_frozen_reflected_power_excess = 0.;
    double maximum_solver_output_power = 0.;
    double minimum_longitudinal_permittivity = 1.;
    std::size_t rf_solves = 0, knot_profiles_checked = 0, holdout_profiles_checked = 0;
    bool witnesses_qualified = false, qualified = false;
};

class HfMagnetoionicKernelTable;
struct HfMagnetoionicKernelBuild {
    std::shared_ptr<const HfMagnetoionicKernelTable> table;
    HfMagnetoionicKernelChecks checks;
};

// Frozen reflected Jones column. Fixed laboratory axes are retained even at
// component nulls. The two FIRs share a causal support and scalar source.
class HfMagnetoionicKernelTable {
public:
    static constexpr double carrier_hz = 3.4e6, sample_rate_hz = 1920., occupied_half_band_hz = 384.;
    static constexpr std::array magnetic_field_tesla{25e-6, 0., 43.30127018922193e-6};
    static constexpr double production_step_m = 10., witness_step_m = 20.;
    static constexpr double transition_seconds = 600.;
    static constexpr std::size_t tilted_profile_intervals = 16, parallel_profile_intervals = 64;
    static constexpr std::size_t frequency_bins = 128, frequency_holdouts = 17;
    static constexpr std::size_t tilted_rf_solve_limit = 3444, parallel_rf_solve_limit = 12039;
    static constexpr std::size_t modelling_delay_samples = 32, retained_late_samples = 48;
    static constexpr std::size_t taps_per_output = 1 + modelling_delay_samples + retained_late_samples;
    static constexpr std::size_t preparation_workspace_bound_bytes = 2u * 1024u * 1024u;
    static constexpr double complex_tolerance = .005, discarded_energy_tolerance = 1e-6, rate_peak_tolerance = .005;
    static constexpr double original_null_relative_tolerance = .10;

    static constexpr std::size_t first_lag_samples() { return 1; }
    static constexpr std::size_t last_lag_samples() { return first_lag_samples() + taps_per_output - 1; }
    static std::size_t profile_intervals(HfMagnetoionicPath);
    static std::size_t rf_solve_limit(HfMagnetoionicPath);
    static std::array<double, 3> field_at_path(HfMagnetoionicPath);
    static HfVerticalColumn profile_at_q(double q, HfMagnetoionicPath = HfMagnetoionicPath::tilted_lower);
    static double cutoff_offset_hz(double q, HfMagnetoionicPath = HfMagnetoionicPath::tilted_lower);

    HfMagnetoionicPath path() const noexcept { return path_; }
    std::size_t profile_intervals() const noexcept { return knots_.size() - 1; }
    std::size_t workspace_bytes() const noexcept;
    double retarded_peak_error_bound(double duration_seconds, std::size_t output_index) const;
    std::complex<double> coefficient_at_q(std::size_t output_index, std::size_t lag_sample, double q) const;

private:
    using Taps = std::array<std::array<std::complex<double>, taps_per_output>, 2>;
    struct Knot { Taps values{}, slopes{}; }; // slopes per local unit interval

    explicit HfMagnetoionicKernelTable(HfMagnetoionicPath path) : path_(path), knots_(profile_intervals(path) + 1) {}
    friend HfMagnetoionicKernelBuild build_hf_magnetoionic_kernel_table(std::stop_token, HfMagnetoionicPath);
    friend class HfMagnetoionicProfileStream;

    HfMagnetoionicPath path_;
    std::vector<Knot> knots_;
    std::array<double, 2> peak_error_seconds_per_q_{};
};

HfMagnetoionicKernelBuild build_hf_magnetoionic_kernel_table(std::stop_token = {},
    HfMagnetoionicPath = HfMagnetoionicPath::tilted_lower);
std::array<double, HfMagnetoionicKernelTable::frequency_holdouts> hf_magnetoionic_holdout_offsets_hz(double q,
    HfMagnetoionicPath = HfMagnetoionicPath::tilted_lower);
double hf_magnetoionic_frozen_reflected_power_tolerance();

// Clamped half-cosine. The start sample is physical time and this function
// accounts for the declared modeling delay on the observation clock.
double hf_magnetoionic_profile_fraction(std::size_t output_sample, std::size_t transition_first,
                                       double duration_samples);

class HfMagnetoionicProfileStream {
public:
    using Field = std::array<std::complex<double>, 2>;

    HfMagnetoionicProfileStream(std::shared_ptr<const HfMagnetoionicKernelTable>,
                               std::size_t transition_first, double duration_seconds,
                               std::size_t initial_output_sample = 0);

    Field step(std::complex<double> input);
    void process(std::span<const std::complex<double>> input, std::span<Field> output);
    std::size_t drain(std::span<Field> output);
    static constexpr std::size_t tail_samples() noexcept { return HfMagnetoionicKernelTable::last_lag_samples(); }
    std::size_t workspace_bytes() const noexcept;

private:
    Field render(std::complex<double> input);

    std::shared_ptr<const HfMagnetoionicKernelTable> table_;
    std::size_t delayed_transition_first_;
    double duration_samples_;
    std::array<std::complex<double>, HfMagnetoionicKernelTable::last_lag_samples() + 1> history_{};
    std::size_t position_ = 0, cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
