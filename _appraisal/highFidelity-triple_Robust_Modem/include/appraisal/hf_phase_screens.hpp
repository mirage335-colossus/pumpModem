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

struct HfPhaseScreensSpec {
    bool perturbations = true;
    unsigned field = 0;
    double impact_y_m = 0., drift_mps = 42., closest_seconds = 600.;
};

// Closest approach is on the physical output clock before the added sampled delay.
// The application passes the entire actual output horizon, including quiet scoring.
void validate_hf_phase_screens_spec(const HfPhaseScreensSpec&, std::uint64_t horizon_samples);

struct HfPhaseScreensError {
    double absolute_error = 0., reference_amplitude = 0., x_m = 0., offset_hz = 0.;
};

struct HfPhaseScreensSlabWitness {
    double x_m = 0., offset_hz = 0., amplitude_4 = 0., amplitude_8 = 0., amplitude_16 = 0.;
    double difference_2_4 = 0., difference_4_8 = 0., difference_8_16 = 0., relative_allowance = 0.;
    bool relative_resolved = false;
};

struct HfPhaseScreensNullPoint {
    double x_m = 0., amplitude_4 = 0., amplitude_8 = 0., amplitude_16 = 0., amplitude_fir = 0.;
    double difference_4_8 = 0., difference_8_16 = 0., difference_fir_4 = 0., difference_fir_16 = 0.;
    double relative_slab_allowance = 0., relative_fir_allowance = 0.;
    bool relative_resolved = false;
    std::complex<double> fir_response_rf{}, reference_16_rf{}; // common uniform RF reference removed
};

struct HfPhaseScreensNullWindow {
    double center_x_m = 0., offset_hz = 0., anchor_error = 0.;
    std::size_t sampled_points = 0, unresolved_points = 0;
    HfPhaseScreensNullPoint minimum_reference{}, minimum_fir{}, worst_slab_relative{}, worst_fir_relative{};
    std::vector<std::array<double, 2>> unresolved_ranges_m; // inclusive endpoints of each sampled run
};

struct HfPhaseScreensChecks {
    HfPhaseScreensError mesh{}, box{}, slices_1_vs_2{}, slices_2_vs_4{}, frequency{},
        trajectory{}, fir{};
    double maximum_plane_power_error = 0., minimum_longitudinal_flux = 1.;
    double maximum_edge_spectral_fraction = 0., maximum_large_angle_fraction = 0.;
    double maximum_discarded_energy_fraction = 0., maximum_raw_precursor_energy_fraction = 0.;
    double minimum_carrier_power = 1., maximum_carrier_power = 1.;
    double single_screen_difference = 0., retarded_peak_error_bound = 0.;
    std::array<HfPhaseScreensSlabWitness, 3> slab_minima{};
    HfPhaseScreensSlabWitness worst_slab_relative{};
    std::array<HfPhaseScreensNullWindow, 3> null_windows{};
    std::size_t field_solves = 0, checked_positions = 0, checked_frequency_offsets = 0;
    std::size_t preparation_peak_workspace_bytes = 0, unresolved_trajectory_points = 0;
    std::size_t deep_slab_witnesses = 0, slab_cut_points = 0, unresolved_slab_witnesses = 0;
    bool deep_slab_qualified = false;
    bool null_windows_qualified = false;
    bool qualified = false;
};

class HfPhaseScreensTable;
struct HfPhaseScreensTableBuild {
    std::shared_ptr<const HfPhaseScreensTable> table;
    HfPhaseScreensChecks checks;
};

class HfPhaseScreensTable {
public:
    static constexpr double carrier_hz = 1e7, sample_rate_hz = 1920., occupied_half_band_hz = 384.;
    static constexpr double light_speed_mps = 299792458., total_distance_m = 600000.;
    static constexpr double horizon_seconds = 1600., position_half_span_m = 45000., position_step_m = 62.5;
    static constexpr double null_window_step_m = .125, null_window_radius_m = position_step_m / 4.;
    static constexpr std::size_t position_intervals = 1440, modelling_delay_samples = 32;
    static constexpr std::size_t retained_late_samples = 48, taps = 1 + modelling_delay_samples + retained_late_samples;
    static constexpr std::size_t preparation_workspace_bound_bytes = 56u * 1024u * 1024u;
    static constexpr std::size_t first_lag_samples() noexcept { return 4; }
    static constexpr std::size_t last_lag_samples() noexcept { return first_lag_samples() + taps - 1; }
    std::size_t workspace_bytes() const noexcept;
    double retarded_peak_error_bound(double drift_mps) const;
    std::complex<double> coefficient_at_position(std::size_t lag, double x_m) const;
    std::complex<double> coefficient_knot_slope(std::size_t lag, std::size_t knot) const;

private:
    using Taps = std::array<std::complex<double>, taps>;
    struct Knot { Taps values{}, slopes{}; }; // derivative per position interval
    explicit HfPhaseScreensTable(const HfPhaseScreensSpec&);
    friend HfPhaseScreensTableBuild build_hf_phase_screens_table(const HfPhaseScreensSpec&, std::stop_token);
    friend class HfPhaseScreensStream;
    HfPhaseScreensSpec spec_;
    std::vector<Knot> knots_;
    double derivative_flight_bound_ = 0.;
};

HfPhaseScreensTableBuild build_hf_phase_screens_table(const HfPhaseScreensSpec&, std::stop_token = {});

// Exposes real calculations for independent native analytic controls, not a
// selectable propagation geometry. Returns dimensionless complex RF voltage
// with the common vacuum and uniform-plasma phase divided out.
inline constexpr std::array hf_phase_screens_gaussian_positions_m{-4000., -1250., 0., 1250., 4000.};
std::array<std::complex<double>, hf_phase_screens_gaussian_positions_m.size()>
hf_phase_screens_gaussian_witness(std::stop_token = {});

struct HfSingleGaussianThicknessWitness {
    static constexpr double offset_hz = -118.7;
    static constexpr std::array x_m{800., 825., 850., 875., 900.};
    std::array<std::complex<double>, x_m.size()> thin_vacuum{};
    std::array<std::array<std::complex<double>, 4>, x_m.size()> atmospheric_slices{};
    double finer_mesh_error_at_850m = 0., bigger_box_error_at_850m = 0.;
    double spherical_baseline_amplitude = 1.;
};

HfSingleGaussianThicknessWitness hf_phase_screens_gaussian_thickness_witness(std::stop_token = {});

class HfPhaseScreensStream {
public:
    HfPhaseScreensStream(std::shared_ptr<const HfPhaseScreensTable>, const HfPhaseScreensSpec&,
                         std::uint64_t initial_output_sample = 0);
    std::complex<double> step(std::complex<double> input);
    void process(std::span<const std::complex<double>>, std::span<std::complex<double>>);
    std::size_t drain(std::span<std::complex<double>>);
    static constexpr std::size_t tail_samples() noexcept { return HfPhaseScreensTable::last_lag_samples(); }
    std::size_t workspace_bytes() const noexcept;

private:
    std::complex<double> render(std::complex<double> input);
    std::shared_ptr<const HfPhaseScreensTable> table_;
    std::uint64_t output_sample_, last_output_sample_;
    std::array<std::complex<double>, HfPhaseScreensTable::last_lag_samples() + 1> history_{};
    std::size_t cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
