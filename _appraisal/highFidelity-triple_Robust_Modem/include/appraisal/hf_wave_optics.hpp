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

// A single translating screen on the ascending leg of an unfolded ideal
// single hop. closest_seconds is on the physical clock at the screen (before
// outgoing flight and the sampled filter's modeling delay).
struct HfWaveOpticsSpec {
    double peak_delta_tecu = -.00857;
    double impact_y_m = 0.;
    double drift_mps = 120.;
    double closest_seconds = 128.;
};

struct HfWaveOpticsResponse {
    std::complex<double> rf{}, derivative_per_m{};
    double series_tail_bound = 0.;
    std::size_t series_terms = 0;
};

// RF uses exp(-i omega t). This includes vacuum and ambient plasma phase,
// normalized only by the nominal free-space link voltage, never by realized
// screen gain. A positive displacement is a screen center at +x or +y.
HfWaveOpticsResponse solve_hf_wave_optics(double frequency_hz, double peak_delta_tecu,
                                       double x_m, double y_m);
void validate_hf_wave_optics_spec(const HfWaveOpticsSpec&);

struct HfWaveOpticsWitness {
    double absolute_error = 0., raw_amplitude = 0., displacement_m = 0., offset_hz = 0.;
};

struct HfWaveOpticsChecks {
    HfWaveOpticsWitness worst_frequency_error{}, worst_position_error{}, minimum_raw_voltage{};
    double maximum_discarded_energy_fraction = 0., maximum_raw_precursor_energy_fraction = 0.;
    double maximum_series_tail_bound = 0., far_field_error_bound = 0.;
    double minimum_sampled_carrier_power = 1., maximum_sampled_carrier_power = 1.;
    std::size_t rf_evaluations = 0, occupied_witnesses = 0, unresolved_witnesses = 0;
    std::size_t maximum_series_terms = 0;
    bool qualified = false;
};

class HfWaveOpticsTable;
struct HfWaveOpticsTableBuild {
    std::shared_ptr<const HfWaveOpticsTable> table;
    HfWaveOpticsChecks checks;
};

class HfWaveOpticsTable {
public:
    static constexpr double carrier_hz = 1e7, sample_rate_hz = 1920., occupied_half_band_hz = 384.;
    static constexpr double light_speed_mps = 299792458.;
    static constexpr double incoming_distance_m = 200000., outgoing_distance_m = 400000.;
    static constexpr double total_distance_m = incoming_distance_m + outgoing_distance_m;
    static constexpr double effective_distance_m = incoming_distance_m * outgoing_distance_m / total_distance_m;
    static constexpr double sigma_x_m = 1600., sigma_y_m = 2400., ambient_tecu = .015, slab_length_m = 3000.;
    static constexpr double maximum_peak_delta_tecu = .010, maximum_impact_m = 2400., maximum_drift_mps = 180.;
    static constexpr double position_step_m = 100., maximum_table_position_m = 12800.;
    static constexpr std::size_t position_intervals = 128, frequency_bins = 128, refinement_bins = 256;
    static constexpr std::size_t modelling_delay_samples = 32, retained_late_samples = 48;
    static constexpr std::size_t taps = 1 + modelling_delay_samples + retained_late_samples;
    static constexpr std::size_t preparation_workspace_bound_bytes = 1024u * 1024u;
    static constexpr std::size_t maximum_rf_evaluations = 23000;
    static constexpr double absolute_tolerance = .0002, nonzero_relative_tolerance = .10;
    static constexpr double voltage_resolution_floor = .0005, discarded_energy_tolerance = 1e-6;
    static constexpr double retarded_peak_tolerance = .005;

    static constexpr std::size_t first_lag_samples() { return 4; }
    static constexpr std::size_t last_lag_samples() { return first_lag_samples() + taps - 1; }
    std::size_t workspace_bytes() const noexcept;
    double retarded_peak_error_bound(double drift_mps) const;
    std::complex<double> coefficient_at_position(std::size_t lag_sample, double displacement_m) const;
    std::complex<double> coefficient_knot_slope(std::size_t lag_sample, std::size_t position_knot) const;

private:
    using Taps = std::array<std::complex<double>, taps>;
    struct Knot { Taps values{}, slopes{}; }; // slope per 100 m, in fixed complex coordinates
    explicit HfWaveOpticsTable(const HfWaveOpticsSpec&);
    friend HfWaveOpticsTableBuild build_hf_wave_optics_table(const HfWaveOpticsSpec&, std::stop_token);
    friend class HfWaveOpticsStream;
    double peak_delta_tecu_, impact_y_m_;
    std::vector<Knot> knots_;
    double derivative_flight_bound_ = 0.;
};

HfWaveOpticsTableBuild build_hf_wave_optics_table(const HfWaveOpticsSpec&, std::stop_token = {});

class HfWaveOpticsStream {
public:
    HfWaveOpticsStream(std::shared_ptr<const HfWaveOpticsTable>, const HfWaveOpticsSpec&,
                      std::uint64_t initial_output_sample = 0);
    std::complex<double> step(std::complex<double> input);
    void process(std::span<const std::complex<double>>, std::span<std::complex<double>>);
    std::size_t drain(std::span<std::complex<double>>);
    static constexpr std::size_t tail_samples() noexcept { return HfWaveOpticsTable::last_lag_samples(); }
    std::size_t workspace_bytes() const noexcept;

private:
    std::complex<double> render(std::complex<double> input);
    std::shared_ptr<const HfWaveOpticsTable> table_;
    double drift_mps_, output_closest_sample_;
    std::uint64_t output_sample_;
    std::array<std::complex<double>, HfWaveOpticsTable::last_lag_samples() + 1> history_{};
    std::size_t cursor_ = 0, remaining_tail_ = 0;
    bool draining_ = false;
};

} // namespace appraisal
