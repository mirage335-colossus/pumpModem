#pragma once

#include "appraisal/hf_vector_oblique.hpp"

namespace appraisal {

enum class HfVectorTwoHopField { primary, holdout };
enum class HfVectorTwoHopRoutes { two_hop, one_hop, both };

struct HfVectorTwoHopSpec {
    HfVectorTwoHopField field = HfVectorTwoHopField::primary;
    HfVectorTwoHopRoutes routes = HfVectorTwoHopRoutes::both;
    unsigned receive_axis_index = 0; // Real unit axis in yz, five degrees per index.
    bool operator==(const HfVectorTwoHopSpec&) const = default;
};

struct HfVectorTwoHopColumn {
    double lower_height_m, thickness_m, peak_plasma_hz, collisions_per_second;
    std::array<double, 3> field_tesla;
    double slice_thickness_m = 0., slice_center_fraction = 0.;
    std::array<HfVectorGeometryMode, 3> density_modes{};
};

struct HfVectorTwoHopGeometry {
    std::array<HfVectorTwoHopColumn, 2> es;
    HfVectorTwoHopColumn f;
    std::array<double, 2> transverse_period_m;
    double two_hop_incidence_deg, one_hop_incidence_deg, receiver_x_m;
    double ground_relative_permittivity, ground_conductivity_s_m;
    double f_height_amplitude_m, f_height_period_seconds, f_height_epoch_seconds;
    std::array<double, 3> transmit_axis, receive_axis;
    double two_hop_vacuum_range_m, one_hop_vacuum_range_m;
};

HfVectorTwoHopGeometry hf_vector_two_hop_geometry(const HfVectorTwoHopSpec&);
void validate_hf_vector_two_hop_spec(const HfVectorTwoHopSpec&, std::uint64_t horizon_samples);

// TE and TM in the tangent-electric-field gauge of HfVectorScattering.
std::array<std::complex<double>, 2> hf_vector_two_hop_ground_reflection(double frequency_hz,
    double transverse_sine, double relative_permittivity, double conductivity_s_m);

// A single selected ground-bounce contribution. The two matrices share the
// same vacuum flux basis, lattice and global transverse origin. The output
// keeps every descending order and polarization at the receive ground plane.
std::vector<std::complex<double>> compose_hf_vector_two_hop_ground_route(
    const HfVectorScattering& first, const HfVectorScattering& second,
    const HfVectorTransverseGrid&, double frequency_hz, std::array<double, 2> lower_heights_m,
    double ground_relative_permittivity, double ground_conductivity_s_m,
    std::span<const std::complex<double>> launch);

// Selected F contribution at a prescribed displacement. The F reflector
// is stratified and order-diagonal. Results are lab E_y and E_z at the RX.
std::array<std::complex<double>, 2> compose_hf_vector_two_hop_f_route(
    const HfVectorScattering& enter, const HfVectorScattering& f_reflector, const HfVectorScattering& leave,
    const HfVectorTransverseGrid&, double frequency_hz, const HfVectorTwoHopGeometry&,
    std::span<const std::complex<double>> launch, double displacement_m);

struct HfVectorTwoHopNullWitness {
    double offset_hz = 0., displacement_m = 0.;
    double assembled_power = 0., direct_power = 0., voltage_error = 0.;
};

struct HfVectorTwoHopBranchErrors {
    double mesh_voltage_error = 0., refined_order_voltage_error = 0.;
    double frequency_voltage_error = 0., staged_frequency_voltage_error = 0.;
    double staged_mesh_voltage_error = 0., staged_refined_order_voltage_error = 0.;
};

struct HfVectorTwoHopFadeRun {
    std::size_t qualifying_samples = 0, longest_run_samples = 0;
    double longest_sample_span_seconds = 0.;
};

HfVectorTwoHopFadeRun hf_vector_two_hop_circular_fade_run(std::span<const std::uint8_t> qualifying, double sample_step_seconds);

struct HfVectorTwoHopTemporalFade {
    bool evaluated = false;
    std::size_t sample_count = 0;
    double sample_step_seconds = 0., voltage_allowance = 0., mean_incoherent_band_power = 0.;
    HfVectorTwoHopFadeRun carrier, band, joint;
};

struct HfVectorTwoHopChecks {
    bool carrier_qualified = false, qualified = false, specified_fade_observed = false;
    std::size_t rf_solves = 0, rf_points = 0, matrix_products = 0;
    std::size_t two_hop_rf_points = 0, one_hop_rf_points = 0, moving_angle_fit_points = 0;
    std::size_t checked_moving_angle_witnesses = 0, checked_moving_angle_refinement_points = 0;
    std::size_t checked_frequency_witnesses = 0, checked_displacement_witnesses = 0;
    std::size_t scanned_displacements = 0, checked_fade_witnesses = 0;
    std::size_t checked_angular_witnesses = 0, checked_staged_modes = 0, checked_linear_velocity_witnesses = 0;
    std::size_t scanned_null_displacements = 0, checked_null_witnesses = 0;
    std::array<HfVectorTwoHopNullWitness, 10> null_witnesses{};
    std::size_t preparation_peak_workspace_bytes = 0;
    std::uint64_t maximum_rf_dense_terms = 0;
    double minimum_vacuum_cosine = 1., maximum_flux_excess = 0., maximum_solve_residual = 0.;
    double maximum_ground_power = 0., maximum_outer_order_flux = 0.;
    double one_hop_carrier_power = 0., two_hop_carrier_power = 0.;
    double two_hop_intermediate_nonspecular_flux = 0., two_hop_intermediate_cross_plane_flux = 0.;
    double maximum_mesh_voltage_error = 0., maximum_refined_order_voltage_error = 0.;
    double maximum_frequency_voltage_error = 0., maximum_fir_voltage_error = 0.;
    double maximum_displacement_tap_error = 0., maximum_discarded_energy_fraction = 0.;
    double minimum_carrier_power = 0., minimum_incoherent_carrier_power = 0.;
    double minimum_band_power = 0., minimum_band_incoherent_power = 0.;
    double carrier_fade_displacement_m = 0., band_fade_displacement_m = 0.;
    double relative_voltage_error_at_witness_minimum = 0., retarded_peak_error_bound = 0.;
    double maximum_staged_support_error = 0., average_incoherent_band_power = 0.;
    double maximum_staged_frequency_voltage_error = 0.;
    double maximum_linear_velocity_voltage_error = 0., maximum_band_quadrature_power_error = 0.;
    double maximum_angular_freezing_voltage_error = 0.;
    double maximum_moving_angle_voltage_error = 0., maximum_moving_angle_staged_voltage_error = 0.;
    double maximum_carrier_shift_voltage_error = 0.;
    double minimum_assembled_null_power = 0., minimum_direct_null_power = 0.;
    double maximum_longitudinal_footprint_offset_m = 0., maximum_cross_plane_footprint_offset_m = 0.;
    HfVectorTwoHopBranchErrors two_hop_voltage_errors, one_hop_voltage_errors;
    HfVectorTwoHopTemporalFade temporal_fade;
};

// Coherent allowance for the independently witnessed physical branch fits.
// The compiled FIR and clock allowances are separate measurements.
double hf_vector_two_hop_physical_voltage_allowance(const HfVectorTwoHopChecks&);

struct HfVectorTwoHopAxisFinding {
    bool specified_fade_observed = false;
    double minimum_carrier_power = 0., minimum_incoherent_carrier_power = 0.;
    double minimum_band_power = 0., minimum_band_incoherent_power = 0., average_incoherent_band_power = 0.;
    double carrier_fade_displacement_m = 0., band_fade_displacement_m = 0.;
};

struct HfVectorTwoHopAxisSearch {
    static constexpr std::size_t axis_count = 36, no_axis = axis_count;
    unsigned selected_axis_index = no_axis;
    HfVectorTwoHopChecks checks;
    std::array<HfVectorTwoHopAxisFinding, axis_count> axes;
};

HfVectorTwoHopAxisSearch assess_hf_vector_two_hop_axes(std::stop_token = {});

struct HfVectorTwoHopAngularWitness {
    double displacement_m = 0., elapsed_seconds = 0.;
    double one_hop_incidence_deg = 0., one_hop_vacuum_range_m = 0.;
    std::array<std::complex<double>, 2> fixed_angle_yz{}, recomputed_angle_yz{};
};

struct HfVectorTwoHopAngularScreen {
    HfVectorTwoHopChecks checks;
    double nominal_elapsed_seconds = 0.;
    bool selected_axis_within_limit = false;
    std::array<double, HfVectorTwoHopAxisSearch::axis_count> maximum_axis_errors{};
    std::array<HfVectorTwoHopAngularWitness, 4> witnesses{};
};

// Carrier only. Requires the one-hop route, with its separately checked mesh.
// The result does not admit any waveform or receiver configuration.
HfVectorTwoHopAngularScreen assess_hf_vector_two_hop_angular(const HfVectorTwoHopSpec&, std::stop_token = {});

struct HfVectorTwoHopAngularFitAttempt {
    std::size_t fit_nodes = 0, checked_cells = 0;
    double maximum_complex_voltage_error = 0., maximum_staged_voltage_error = 0.;
    bool qualified = false;
};

struct HfVectorTwoHopAngularFit {
    HfVectorTwoHopChecks checks;
    std::size_t attempts_run = 0, selected_fit_nodes = 0;
    std::size_t checked_extreme_carrier_points = 0;
    bool extreme_carrier_qualified = false;
    std::array<HfVectorTwoHopAngularFitAttempt, 2> attempts{};
    struct Cell {
        double displacement_m, offset_hz, elapsed_seconds;
        std::size_t half_orders;
        bool refined_vertical;
    };
    std::vector<Cell> cells;
};

// Moving-angle interpolation diagnostics, at three RF frequencies. A
// qualifying result selects numerical support for further checking only.
HfVectorTwoHopAngularFit assess_hf_vector_two_hop_angular_fit(const HfVectorTwoHopSpec&, std::stop_token = {});

struct HfVectorTwoHopHeightCheck {
    std::complex<double> value;
    double maximum_magnitude, maximum_derivative_per_m;
};

HfVectorTwoHopHeightCheck hf_vector_two_hop_height_bounds(
    const std::array<std::complex<double>, 9>& nodes, double normalized_height, double half_extent_m);

class HfVectorTwoHopTable;
struct HfVectorTwoHopTableBuild {
    std::shared_ptr<const HfVectorTwoHopTable> table;
    HfVectorTwoHopChecks checks;
    // Qualification provenance, not retained by the streaming operator.
    // Height, RF knot, flux port, then [pre, post_y, post_z].
    std::vector<std::complex<double>> moving_angle_fit_coefficients;
};

HfVectorTwoHopChecks assess_hf_vector_two_hop_carrier(const HfVectorTwoHopSpec&, std::stop_token = {});
HfVectorTwoHopTableBuild build_hf_vector_two_hop_table(const HfVectorTwoHopSpec&, std::stop_token = {});

class HfVectorTwoHopTable {
public:
    static constexpr double carrier_hz = 10e6, sample_rate_hz = 1920., occupied_half_band_hz = 384.;
    static constexpr double horizon_seconds = 2400.;
    static constexpr double relative_spreading_reference_range_m = 1000000.;
    static constexpr double fade_half_band_hz = 112.5;
    static constexpr std::size_t fade_time_witnesses = 4097, fade_band_intervals = 64;
    static constexpr std::size_t temporal_fade_samples = fade_time_witnesses - 1;
    static constexpr auto temporal_fade_scope =
        "sampled compiled-FIR coefficient response over one output-time period; no between-sample duration guarantee";
    static constexpr std::size_t moving_angle_fit_nodes = 9;
    static constexpr std::size_t modelling_delay_samples = 32, retained_late_samples = 96;
    static constexpr std::size_t taps = 1 + modelling_delay_samples + retained_late_samples;
    static constexpr std::size_t displacement_intervals = 16;
    static constexpr std::size_t preparation_workspace_bound_bytes = 96u * 1024u * 1024u;
    static constexpr std::size_t first_lag_samples() noexcept { return 3; }
    static constexpr std::size_t last_lag_samples() noexcept { return first_lag_samples() + taps - 1; }

    std::size_t workspace_bytes() const noexcept;
    // Physical output seconds, before the common added model delay.
    std::complex<double> coefficient_at_time(std::size_t lag, double physical_seconds) const;
    // Stored raw taps, before restoring the moving F branch's analytic
    // carrier phase. Individual routes only, in ascending displacement.
    std::size_t coefficient_knot_count(HfVectorTwoHopRoutes) const;
    std::complex<double> coefficient_knot_value(HfVectorTwoHopRoutes, std::size_t lag, std::size_t knot) const;

private:
    using Taps = std::array<std::complex<double>, taps>;
    struct MovingPosition {
        std::size_t first = 0;
        std::array<double, 4> weights{};
        std::complex<double> carrier_phase{};
    };
    friend HfVectorTwoHopTableBuild build_hf_vector_two_hop_table(const HfVectorTwoHopSpec&, std::stop_token);
    friend class HfVectorTwoHopStream;
    MovingPosition position_at_displacement(double) const;
    MovingPosition position_at_time(double) const;
    std::complex<double> coefficient(std::size_t index, const MovingPosition&) const;
    HfVectorTwoHopTemporalFade assess_temporal_fade(double voltage_allowance, std::stop_token) const;
    HfVectorTwoHopSpec spec_;
    HfVectorTwoHopGeometry geometry_;
    Taps two_hop_{};
    std::vector<Taps> one_hop_;
};

class HfVectorTwoHopStream {
public:
    HfVectorTwoHopStream(std::shared_ptr<const HfVectorTwoHopTable>, const HfVectorTwoHopSpec&,
                         std::uint64_t initial_output_sample = 0);
    std::complex<double> step(std::complex<double> input);
    void process(std::span<const std::complex<double>>, std::span<std::complex<double>>);
    std::size_t drain(std::span<std::complex<double>>);
    static constexpr std::size_t tail_samples() noexcept { return HfVectorTwoHopTable::last_lag_samples(); }
    std::size_t workspace_bytes() const noexcept;

private:
    std::complex<double> render(std::complex<double>);
    std::shared_ptr<const HfVectorTwoHopTable> table_;
    std::array<std::complex<double>, HfVectorTwoHopTable::last_lag_samples() + 1> history_{};
    std::uint64_t output_sample_ = 0;
    std::size_t cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
