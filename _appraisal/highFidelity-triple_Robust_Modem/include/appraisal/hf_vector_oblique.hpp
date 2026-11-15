#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace appraisal {

// One exactly homogeneous-in-z layer, periodic in x, invariant in y.
// N_e(x) = mean + 2 Re sum_{j=1,2} density_harmonics[j-1] exp(i j K x).
struct HfVectorPlasmaLayer {
    double thickness_m, mean_electrons_per_m3, collisions_per_second;
    std::array<std::complex<double>, 2> density_harmonics{};
};

// Vacuum ports with unit |vertical power flux| at each dir/order/polarization.
// Matrices are row-major in ascending diffraction order, then TE, TM. Port
// waves use fixed lab E_y (TE) and E_x (TM) signs in both directions.
struct HfVectorScattering {
    std::size_t half_orders = 0;
    std::vector<std::complex<double>> reflect_below, reflect_above, transmit_up, transmit_down;
    double minimum_vacuum_cosine = 1., maximum_flux_excess = 0., maximum_solve_residual = 0.;
    std::size_t matrix_products = 0;
    std::size_t port_modes() const noexcept { return 2 * (2 * half_orders + 1); }
};

// exp(-i omega t); transverse wave number is k0*sin(incidence) + 2*pi*m/period.
// The coupled finite Fourier problem keeps both tangential E and H. Closed or
// near-grazing vacuum orders are deliberately outside this bounded experiment.
HfVectorScattering solve_hf_vector_oblique(double frequency_hz, double sine_incidence,
    double period_m, std::size_t half_orders, std::array<double, 3> field_tesla,
    std::span<const HfVectorPlasmaLayer> bottom_to_top, std::stop_token = {});

struct HfVectorObliqueSpec {
    bool perturbations = true, magnetic = true;
    unsigned field = 0, antenna = 0;
    double drift_mps = 8., epoch_seconds = 600.;
};

struct HfVectorObliqueGeometry {
    double incidence_deg, height_m, receiver_x_m, period_m, thickness_m;
    double peak_plasma_hz, collision_rate, corrugated_slice_m;
    std::array<double, 3> field_tesla, antenna_axis;
    double launch_tm_fraction;
    double corrugation_center_fraction;
    std::array<double, 2> density_harmonic_x, density_harmonic_phase_rad;
};

HfVectorObliqueGeometry hf_vector_oblique_geometry(const HfVectorObliqueSpec&);
void validate_hf_vector_oblique_spec(const HfVectorObliqueSpec&, std::uint64_t horizon_samples);

struct HfVectorObliqueChecks {
    bool qualified = false;
    std::size_t rf_solves = 0, matrix_products = 0, preparation_peak_workspace_bytes = 0;
    std::size_t output_half_orders = 0, checked_frequencies = 0, checked_positions = 0;
    std::size_t checked_table_assembly_positions = 0; // frequency/position pairs
    double minimum_vacuum_cosine = 1., maximum_flux_excess = 0., maximum_solve_residual = 0.;
    double mesh_voltage_error = 0., order_voltage_error = 0., refined_order_voltage_error = 0.;
    double maximum_edge_flux = 0., frequency_voltage_error = 0., fir_voltage_error = 0.;
    double table_assembly_voltage_error = 0.;
    double carrier_reflected_flux = 0., carrier_transmitted_flux = 0., carrier_scattered_reflected_flux = 0.;
    double carrier_crosspolar_reflected_flux = 0., carrier_crosspolar_scattered_flux = 0.;
    double minimum_carrier_power = 0., maximum_carrier_power = 0., minimum_carrier_position_m = 0.;
    double certified_minimum_power_lower_bound = 0.;
    double minimum_checked_band_power_lower_bound = 0., minimum_checked_band_power_witness_offset_hz = 0.;
    double relative_voltage_error_at_minimum = 0.;
    double period_averaged_discarded_energy_fraction = 0., period_averaged_raw_precursor_energy_fraction = 0.;
    double interpolation_peak_error_bound = 0., retarded_peak_error_bound = 0.;
};

// Four carrier-only solves for early mesh/order screening. The result has
// qualified=false because no broadband waveform has been checked or built.
HfVectorObliqueChecks assess_hf_vector_oblique_carrier(const HfVectorObliqueSpec&, std::stop_token = {});

class HfVectorObliqueTable;
struct HfVectorObliqueTableBuild {
    std::shared_ptr<const HfVectorObliqueTable> table;
    HfVectorObliqueChecks checks;
};

class HfVectorObliqueTable {
public:
    static constexpr double carrier_hz = 1e7, sample_rate_hz = 1920., occupied_half_band_hz = 384.;
    static constexpr double horizon_seconds = 2400.;
    static constexpr std::size_t modelling_delay_samples = 32, retained_late_samples = 96;
    static constexpr std::size_t taps = 1 + modelling_delay_samples + retained_late_samples;
    static constexpr std::size_t position_intervals = 256;
    static constexpr std::size_t preparation_workspace_bound_bytes = 16u * 1024u * 1024u;
    static constexpr std::size_t first_lag_samples() noexcept { return 2; }
    static constexpr std::size_t last_lag_samples() noexcept { return first_lag_samples() + taps - 1; }
    std::size_t workspace_bytes() const noexcept;
    // x = -v(t - epoch), with t on the physical output clock before the
    // added model delay. The finite Fourier model is exactly periodic.
    std::complex<double> coefficient_at_position(std::size_t lag, double position_m) const;
    // Position k*period/position_intervals, from k=0 through its periodic
    // endpoint. Slopes are in units of one position interval.
    std::complex<double> coefficient_knot_value(std::size_t lag, std::size_t knot) const;
    std::complex<double> coefficient_knot_slope(std::size_t lag, std::size_t knot) const;

private:
    using Taps = std::array<std::complex<double>, taps>;
    struct Knot { Taps value{}, slope{}; };
    friend HfVectorObliqueTableBuild build_hf_vector_oblique_table(const HfVectorObliqueSpec&, std::stop_token);
    friend class HfVectorObliqueStream;
    HfVectorObliqueSpec spec_;
    HfVectorObliqueGeometry geometry_;
    std::vector<Knot> knots_;
};

HfVectorObliqueTableBuild build_hf_vector_oblique_table(const HfVectorObliqueSpec&, std::stop_token = {});

class HfVectorObliqueStream {
public:
    HfVectorObliqueStream(std::shared_ptr<const HfVectorObliqueTable>, const HfVectorObliqueSpec&,
                          std::uint64_t initial_output_sample = 0);
    std::complex<double> step(std::complex<double> input);
    void process(std::span<const std::complex<double>>, std::span<std::complex<double>>);
    std::size_t drain(std::span<std::complex<double>>);
    static constexpr std::size_t tail_samples() noexcept { return HfVectorObliqueTable::last_lag_samples(); }
    std::size_t workspace_bytes() const noexcept;

private:
    std::complex<double> render(std::complex<double>);
    std::shared_ptr<const HfVectorObliqueTable> table_;
    std::array<std::complex<double>, HfVectorObliqueTable::last_lag_samples() + 1> history_{};
    std::uint64_t output_sample_ = 0;
    std::size_t cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
