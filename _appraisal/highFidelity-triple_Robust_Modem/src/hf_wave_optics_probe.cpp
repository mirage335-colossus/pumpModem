#include "appraisal/hf_fullwave_kernel.hpp"
#include "appraisal/hf_wave_optics.hpp"
#include "search_fft.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace appraisal;
using Table = HfWaveOpticsTable;
using Complex = std::complex<double>;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;
namespace fft = datapump::modem::detail::search_fft;
using Clock = std::chrono::steady_clock;
constexpr double pi = std::numbers::pi;
constexpr Complex imag{0., 1.};
constexpr double e = 1.602176634e-19, me = 9.1093837139e-31, vacuum_permittivity = 8.8541878188e-12;
constexpr double spatial_tolerance = 2e-8, flux_tolerance = 2e-12;
constexpr double fade_x_m = 850., fade_offset_hz = -118.7;
constexpr std::size_t budget_cells = 30'000'000;
constexpr std::size_t flux_workspace_limit = 64u * 1024u * 1024u;
std::size_t quadrature_cells = 0;

double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
double sq(double x) { return x * x; }

// Independent reference: obtain the scalar index directly from electron
// momentum and dielectric constants. No Taylor coefficients from production.
struct DirectIndex {
    explicit DirectIndex(double f, double delta)
        : k(2. * pi * f / Table::light_speed_mps),
          xb(e * e / (me * vacuum_permittivity * sq(2. * pi * f)) * 1e16 *
              Table::ambient_tecu / Table::slab_length_m),
          xd(xb * delta / Table::ambient_tecu), nb(std::sqrt(1. - xb)) {}

    double phase(double g) const {
        return -k * Table::slab_length_m * xd * g / (nb + std::sqrt(1. - xb - xd * g));
    }
    Complex ambient() const {
        return std::polar(1., k * (Table::total_distance_m - Table::slab_length_m * xb / (1. + nb)));
    }
    double k, xb, xd, nb;
};

struct AxisPoint { double g; Complex fresnel; };

std::vector<AxisPoint> quadrature_axis(double sigma, double center, double alpha, int extent, int dx) {
    const auto n = static_cast<std::size_t>(std::ceil(2. * extent * sigma / dx));
    const double step = 2. * extent * sigma / n;
    std::vector<AxisPoint> out;
    out.reserve(n);
    for (std::size_t j = 0; j < n; ++j) {
        const double u = (j + .5) * step - extent * sigma;
        out.push_back({std::exp(-.5 * sq(u / sigma)), std::polar(step, alpha * sq(u + center))});
    }
    return out;
}

Complex quadrature(double f, double delta, double x, double y, int extent, int dx) {
    const DirectIndex index(f, delta);
    const double alpha = index.k / (2. * Table::effective_distance_m);
    const auto xs = quadrature_axis(Table::sigma_x_m, x, alpha, extent, dx);
    const auto ys = quadrature_axis(Table::sigma_y_m, y, alpha, extent, dx);
    if (xs.size() * ys.size() > budget_cells - quadrature_cells) throw std::runtime_error("quadrature work cap exceeded");
    quadrature_cells += xs.size() * ys.size();
    Complex scattered{};
    for (const auto& row : ys) {
        Complex sum{};
        for (const auto& col : xs) {
            const double phase = index.phase(row.g * col.g);
            // Evaluate exp(i*phase)-1 without erasing a weak tail.
            sum += col.fresnel * Complex{-2. * sq(std::sin(.5 * phase)), std::sin(phase)};
        }
        scattered += row.fresnel * sum;
    }
    return index.ambient() * (1. - imag * (alpha / pi) * scattered);
}

struct SpatialCase { const char* name; double delta, x, y, offset; };
struct SpatialReference {
    Json receipt;
    Complex wide;
    double quadrature_difference;
};

SpatialReference spatial(const SpatialCase& input, bool& pass) {
    const double f = Table::carrier_hz + input.offset;
    const auto solve = solve_hf_wave_optics(f, input.delta, input.x, input.y);
    const auto rough = quadrature(f, input.delta, input.x, input.y, 6, 100);
    const auto middle = quadrature(f, input.delta, input.x, input.y, 6, 50);
    const auto fine = quadrature(f, input.delta, input.x, input.y, 6, 25);
    const auto wide = quadrature(f, input.delta, input.x, input.y, 8, 25);
    const double displacement_error = std::abs(wide - fine);
    const double resolution_error = std::abs(fine - middle);
    const double series_error = std::abs(solve.rf - wide);
    const double amplitude = std::abs(wide);
    const double guarded_error = displacement_error + resolution_error + series_error;
    const bool resolved = amplitude >= Table::voltage_resolution_floor;
    const bool ok = guarded_error <= spatial_tolerance &&
        (!resolved || guarded_error <= Table::nonzero_relative_tolerance * amplitude);
    pass &= ok;
    auto receipt = object({{"id", input.name}, {"peak_delta_tecu", input.delta}, {"x_m", input.x}, {"y_m", input.y},
        {"offset_hz", input.offset}, {"wide_voltage", amplitude}, {"wide_power", sq(amplitude)},
        {"coarse_to_middle_error", std::abs(rough - middle)}, {"middle_to_fine_error", resolution_error},
        {"fine_to_wide_error", displacement_error}, {"series_to_wide_error", series_error},
        {"estimated_guarded_error", guarded_error}, {"guarded_voltage_low", std::max(0., amplitude - guarded_error)},
        {"guarded_voltage_high", amplitude + guarded_error}, {"sampled_nonzero_resolved", resolved}, {"pass", ok}});
    return {std::move(receipt), wide, displacement_error + resolution_error};
}

void propagate(std::vector<Complex>& data, std::size_t n, double dx, double k, double distance) {
    fft::DoublePlan plan(n);
    std::vector<Complex> column(n);
    const auto axis_transform = [&](bool inverse) {
        for (std::size_t row = 0; row < n; ++row) fft::fft(std::span(data).subspan(row * n, n), inverse, plan);
        for (std::size_t c = 0; c < n; ++c) {
            for (std::size_t row = 0; row < n; ++row) column[row] = data[row * n + c];
            fft::fft(column, inverse, plan);
            for (std::size_t row = 0; row < n; ++row) data[row * n + c] = column[row];
        }
    };
    const auto angular = [=](std::size_t bin) {
        const auto centered = bin < n / 2 ? static_cast<double>(bin) : static_cast<double>(bin) - n;
        return centered * (2. * pi / (n * dx));
    };
    axis_transform(false);
    for (std::size_t row = 0; row < n; ++row)
        for (std::size_t c = 0; c < n; ++c)
            data[row * n + c] *= std::polar(1., -distance / (2. * k) * (sq(angular(row)) + sq(angular(c))));
    axis_transform(true);
}

struct Flux {
    Complex center{};
    double before = 0., after = 0., edge_fraction = 0., no_screen_error = 0., roundtrip_error = 0., wall_seconds = 0.;
};

Flux flux(std::size_t n, double dx) {
    const auto start = Clock::now();
    constexpr double w = 2300., z = Table::outgoing_distance_m;
    const DirectIndex waveform(Table::carrier_hz, HfWaveOpticsSpec{}.peak_delta_tecu);
    if (n != 512 && n != 1024) throw std::runtime_error("flux grid exceeds admitted geometries");
    if ((3 * n * n + 2 * n) * sizeof(Complex) > flux_workspace_limit)
        throw std::runtime_error("flux witness exceeded its workspace cap");
    std::vector<Complex> beam(n * n), screened(n * n), phase(n * n);
    const auto position = [=](std::size_t i) { return (static_cast<double>(i) - n / 2) * dx; };
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j < n; ++j) {
        const double x = position(i), y = position(j);
        const auto index = i * n + j;
        beam[index] = std::exp(-.5 * (sq(x / w) + sq(y / w)));
        phase[index] = std::polar(1., waveform.phase(std::exp(-.5 * (sq(x / Table::sigma_x_m) + sq(y / Table::sigma_y_m)))));
        screened[index] = beam[index] * phase[index];
    }
    const auto power = [dx](const std::vector<Complex>& field) {
        return sq(dx) * std::accumulate(field.begin(), field.end(), 0.,
            [](double sum, Complex value) { return sum + std::norm(value); });
    };
    Flux result{};
    result.before = power(beam);
    propagate(screened, n, dx, waveform.k, z);
    result.after = power(screened);
    result.center = screened[n * (n / 2) + n / 2];
    double edges = 0.;
    for (std::size_t r = 0; r < n; ++r) for (std::size_t c = 0; c < n; ++c)
        if (r < n / 8 || c < n / 8 || r >= n - n / 8 || c >= n - n / 8) edges += std::norm(screened[r * n + c]);
    result.edge_fraction = edges * sq(dx) / result.after;
    propagate(screened, n, dx, waveform.k, -z);
    for (std::size_t i = 0; i < screened.size(); ++i)
        result.roundtrip_error = std::max(result.roundtrip_error, std::abs(screened[i] * std::conj(phase[i]) - beam[i]));
    propagate(beam, n, dx, waveform.k, z);
    const Complex spread = sq(w) + imag * (z / waveform.k);
    for (std::size_t r = 0; r < n; ++r) for (std::size_t c = 0; c < n; ++c) {
        const auto exact = sq(w) / spread * std::exp(-.5 * (sq(position(r)) + sq(position(c))) / spread);
        result.no_screen_error = std::max(result.no_screen_error, std::abs(beam[r * n + c] - exact));
    }
    result.wall_seconds = seconds(start);
    return result;
}

Json flux_row(std::size_t n, double dx, const Flux& f, bool& pass) {
    const double relative = std::abs(f.after - f.before) / f.before;
    const bool ok = relative <= flux_tolerance && f.edge_fraction <= 1e-8 && f.no_screen_error <= 2e-8 &&
        f.roundtrip_error <= 2e-12;
    pass &= ok;
    return object({{"dimension", n}, {"spacing_m", dx}, {"incident_plane_integral", f.before},
        {"outgoing_plane_integral", f.after}, {"relative_integral_error", relative}, {"edge_fraction", f.edge_fraction},
        {"no_screen_analytic_error", f.no_screen_error}, {"inverse_screen_and_propagation_error", f.roundtrip_error},
        {"on_axis_power", std::norm(f.center)}, {"wall_seconds", f.wall_seconds}, {"pass", ok}});
}

Json error(const HfWaveOpticsWitness& v) {
    return object({{"x_m", v.displacement_m}, {"offset_hz", v.offset_hz}, {"raw_amplitude", v.raw_amplitude},
        {"absolute_error", v.absolute_error}});
}

Json table_case(const char* name, const HfWaveOpticsSpec& spec, bool& pass,
                std::shared_ptr<const Table>* retained = nullptr) {
    const auto start = Clock::now();
    const auto built = build_hf_wave_optics_table(spec);
    const double table_seconds = seconds(start);
    const auto& c = built.checks;
    if (retained) *retained = built.table;
    pass &= c.qualified && static_cast<bool>(built.table);
    double render_seconds = 0.;
    std::size_t workspace = 0;
    double retarded = 0.;
    if (built.table) {
        constexpr std::size_t samples = 240'000;
        const auto begin = static_cast<std::uint64_t>(spec.closest_seconds * Table::sample_rate_hz) - samples / 2;
        HfWaveOpticsStream stream(built.table, spec, begin);
        workspace = built.table->workspace_bytes() + stream.workspace_bytes();
        retarded = built.table->retarded_peak_error_bound(spec.drift_mps);
        const auto start_render = Clock::now();
        double energy = 0.;
        for (std::size_t i = 0; i < samples; ++i) {
            const auto input = std::polar(1., (i % 64) * pi / 32.);
            energy += std::norm(stream.step(input));
        }
        render_seconds = seconds(start_render);
        pass &= std::isfinite(energy) && energy > 0.;
    }
    return object({{"id", name}, {"peak_delta_tecu", spec.peak_delta_tecu}, {"impact_y_m", spec.impact_y_m},
        {"drift_mps", spec.drift_mps}, {"closest_seconds", spec.closest_seconds}, {"rf_evaluations", c.rf_evaluations},
        {"occupied_witnesses", c.occupied_witnesses}, {"unresolved_witnesses", c.unresolved_witnesses},
        {"maximum_series_terms", c.maximum_series_terms}, {"maximum_series_tail_bound", c.maximum_series_tail_bound},
        {"far_field_error_bound", c.far_field_error_bound}, {"worst_frequency_error", error(c.worst_frequency_error)},
        {"worst_position_error", error(c.worst_position_error)}, {"minimum_raw_voltage", error(c.minimum_raw_voltage)},
        {"maximum_discarded_energy_fraction", c.maximum_discarded_energy_fraction},
        {"maximum_raw_precursor_energy_fraction", c.maximum_raw_precursor_energy_fraction},
        {"minimum_sampled_carrier_power", c.minimum_sampled_carrier_power}, {"maximum_sampled_carrier_power", c.maximum_sampled_carrier_power},
        {"retarded_peak_error_bound", retarded}, {"table_seconds", table_seconds}, {"render_240000_seconds", render_seconds},
        {"workspace_bytes", workspace}, {"qualified", c.qualified}});
}

HfFullwaveFir production_fir_at(const Table& table, double x) {
    HfFullwaveFir fir{Table::first_lag_samples(), {}};
    fir.taps.reserve(Table::taps);
    for (auto lag = Table::first_lag_samples(); lag <= Table::last_lag_samples(); ++lag)
        fir.taps.push_back(table.coefficient_at_position(lag, x));
    return fir;
}

Json archived_fade(const std::shared_ptr<const Table>& table, const HfWaveOpticsSpec& spec,
                   const SpatialReference& independent, double carrier_minimum_x, bool& pass) {
    if (!table) return object({{"pass", false}, {"reason", "centered table unavailable"}});
    constexpr double normalized_frequency = fade_offset_hz / Table::sample_rate_hz;
    constexpr auto design = HfFullwaveKernelDesign{Table::sample_rate_hz, Table::occupied_half_band_hz,
        Table::total_distance_m / Table::light_speed_mps * Table::sample_rate_hz,
        Table::modelling_delay_samples, Table::retained_late_samples};
    std::array<Complex, Table::refinement_bins> fine_nodes{};
    std::array<Complex, Table::frequency_bins> coarse_nodes{};
    for (std::size_t k = 0; k < fine_nodes.size(); ++k) {
        const double f = Table::carrier_hz + Table::sample_rate_hz * (static_cast<double>(k) / fine_nodes.size() - .5);
        fine_nodes[k] = solve_hf_wave_optics(f, spec.peak_delta_tecu, fade_x_m, spec.impact_y_m).rf;
        if (k % 2 == 0) coarse_nodes[k / 2] = fine_nodes[k];
    }
    const auto coarse = design_hf_fullwave_kernel(coarse_nodes, design);
    const auto fine = design_hf_fullwave_kernel(fine_nodes, design);
    const auto response = [](const HfFullwaveFir& fir) {
        return hf_fullwave_fir_response(fir, normalized_frequency);
    };
    const auto sampled_coarse = response(coarse.fir);
    const auto sampled_fine = response(fine.fir);
    const auto production = response(production_fir_at(*table, fade_x_m));
    const auto raw = solve_hf_wave_optics(Table::carrier_hz + fade_offset_hz,
        spec.peak_delta_tecu, fade_x_m, spec.impact_y_m).rf;
    const auto modelling_phasor = std::polar(1., -2. * pi * normalized_frequency * Table::modelling_delay_samples);
    const auto raw_native = std::conj(raw) * modelling_phasor;
    const auto independent_native = std::conj(independent.wide) * modelling_phasor;
    const double guarded_error = std::abs(production - sampled_coarse) + std::abs(sampled_coarse - sampled_fine) +
        std::abs(sampled_fine - independent_native) + independent.quadrature_difference;
    const double independent_voltage = std::abs(independent_native);
    const bool resolved = independent_voltage >= Table::voltage_resolution_floor;

    // Observe a warmed steady tone in the actual renderer at the nearest
    // clock sample. The exact-position response above does not round x.
    const double closest_sample = (spec.closest_seconds + Table::outgoing_distance_m / Table::light_speed_mps) *
        Table::sample_rate_hz + Table::modelling_delay_samples;
    const auto output_sample = static_cast<std::uint64_t>(std::llround(closest_sample +
        fade_x_m * Table::sample_rate_hz / spec.drift_mps));
    const double observed_x = (static_cast<double>(output_sample) - closest_sample) * spec.drift_mps / Table::sample_rate_hz;
    HfWaveOpticsStream stream(table, spec, output_sample - Table::last_lag_samples());
    Complex streamed{};
    for (std::size_t i = 0; i <= Table::last_lag_samples(); ++i)
        streamed = stream.step(std::polar(1., 2. * pi * normalized_frequency * i));
    streamed *= std::polar(1., -2. * pi * normalized_frequency * Table::last_lag_samples());
    const double stream_error = std::abs(streamed - response(production_fir_at(*table, observed_x)));

    constexpr double scan_step_m = 10.;
    constexpr unsigned scan_steps = 640;
    double minimum_power = std::numeric_limits<double>::infinity(), minimum_x = 0.;
    bool local_minimum = true;
    Json::Array trace, local_bracket;
    for (unsigned i = 0; i <= scan_steps; ++i) {
        const double x = i * scan_step_m;
        const double power = std::norm(response(production_fir_at(*table, x)));
        if (power < minimum_power) { minimum_power = power; minimum_x = x; }
        if (i % 10 == 0) trace.push_back(object({{"x_m", x}, {"production_power", power}}));
        if (std::abs(x - fade_x_m) <= scan_step_m) {
            local_bracket.push_back(object({{"x_m", x}, {"production_power", power}}));
            if (x != fade_x_m) local_minimum &= std::norm(production) <= power;
        }
    }
    const double position_error = std::abs(production - sampled_coarse);
    const double frequency_error = std::abs(sampled_coarse - sampled_fine);
    const double truth_error = std::abs(production - independent_native);
    const bool accurate = guarded_error <= Table::absolute_tolerance &&
        (!resolved || guarded_error <= Table::nonzero_relative_tolerance * independent_voltage);
    const bool ok = accurate && stream_error <= 2e-12;
    pass &= ok;
    return object({{"x_m", fade_x_m}, {"y_m", spec.impact_y_m}, {"rf_offset_hz", fade_offset_hz},
        {"standalone_rf_evaluations", fine_nodes.size() + 1}, {"production_power", std::norm(production)},
        {"raw_series_power", std::norm(raw)}, {"independent_quadrature_power", std::norm(independent_native)},
        {"coincident_position_128_bin_power", std::norm(sampled_coarse)}, {"coincident_position_256_bin_power", std::norm(sampled_fine)},
        {"production_to_raw_complex_error", std::abs(production - raw_native)},
        {"production_to_independent_complex_error", truth_error}, {"production_to_coincident_128_error", position_error},
        {"coincident_128_to_256_error", frequency_error}, {"coincident_256_to_independent_error", std::abs(sampled_fine - independent_native)},
        {"independent_spacing_extent_difference", independent.quadrature_difference}, {"estimated_guarded_error", guarded_error},
        {"guarded_relative_error", resolved ? Json{guarded_error / independent_voltage} : Json{}},
        {"voltage_low_about_production", std::max(0., std::abs(production) - guarded_error)},
        {"voltage_high_about_production", std::abs(production) + guarded_error}, {"sampled_nonzero_resolved", resolved},
        {"nearest_stream_sample", output_sample}, {"nearest_stream_x_m", observed_x}, {"nearest_stream_power", std::norm(streamed)},
        {"nearest_stream_to_fir_error", stream_error}, {"local_bracket", std::move(local_bracket)},
        {"archived_position_is_sampled_local_minimum", local_minimum},
        {"raw_carrier_scan_step_m", scan_step_m}, {"raw_carrier_scan_minimum_x_m", carrier_minimum_x},
        {"production_scan_step_m", scan_step_m}, {"production_scan_minimum_x_m", minimum_x},
        {"production_scan_minimum_power", minimum_power}, {"plot_trace_step_m", 10. * scan_step_m},
        {"plot_trace", std::move(trace)}, {"pass", ok}});
}

} // namespace

int main() {
    try {
        const auto start = Clock::now();
        bool pass = true;
        constexpr double delta = HfWaveOpticsSpec{}.peak_delta_tecu;
        Json::Array spatial_rows;
        for (const auto& row : std::array{SpatialCase{"focal-center", delta, 0., 0., 0.},
            SpatialCase{"near-geometric-fold", delta, 486.3, 0., -118.7},
            SpatialCase{"off-axis-held-path", delta, 2437., 1200., 384.},
            SpatialCase{"opposite-signed-column", -delta, 1126., 800., -384.},
            SpatialCase{"far-field-rim", delta, Table::maximum_table_position_m, 0., 319.61}})
            spatial_rows.push_back(spatial(row, pass).receipt);
        double minimum = std::numeric_limits<double>::infinity(), x_min = 0.;
        for (unsigned i = 0; i <= 640; ++i) {
            const double x = i * 10.;
            const auto voltage = std::abs(solve_hf_wave_optics(Table::carrier_hz, delta, x, 0.).rf);
            if (voltage < minimum) { minimum = voltage; x_min = x; }
        }
        auto fade_reference = spatial({"minimum-from-fixed-carrier-scan", delta, fade_x_m, 0., fade_offset_hz}, pass);
        spatial_rows.push_back(std::move(fade_reference.receipt));

        const auto base = flux(512, 125.), dense = flux(1024, 62.5), padded = flux(1024, 125.);
        Json::Array flux_rows;
        flux_rows.push_back(flux_row(512, 125., base, pass));
        flux_rows.push_back(flux_row(1024, 62.5, dense, pass));
        flux_rows.push_back(flux_row(1024, 125., padded, pass));
        const auto grid_difference = std::abs(base.center - dense.center);
        const auto pad_difference = std::abs(base.center - padded.center);
        pass &= grid_difference <= 2e-8 && pad_difference <= 2e-8;

        Json::Array tables;
        tables.push_back(table_case("lens-off", {0., 0., 120., 128.}, pass));
        const HfWaveOpticsSpec centered{delta, 0., 120., 128.};
        std::shared_ptr<const Table> centered_table;
        tables.push_back(table_case("centered-lens", centered, pass, &centered_table));
        tables.push_back(table_case("cross-track-holdout", {delta, 1200., 87., 219.}, pass));
        tables.push_back(table_case("maximum-drift-numerical-witness", {delta, 0., 180., 128.}, pass));
        auto near_minimum = archived_fade(centered_table, centered, fade_reference, x_min, pass);
        std::cout.precision(17);
        write_json(std::cout, object({{"type", "appraisal_hf_wave_optics_probe"},
            {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA}, {"source_revision", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},
            {"bounded_quadrature_cells", quadrature_cells}, {"quadrature_cell_cap", budget_cells}, {"spatial_references", std::move(spatial_rows)},
            {"finite_beam_flux", std::move(flux_rows)}, {"flux_workspace_limit_bytes", flux_workspace_limit}, {"flux_grid_center_error", grid_difference},
            {"flux_padded_center_error", pad_difference}, {"tables", std::move(tables)}, {"archived_fade", std::move(near_minimum)},
            {"wall_seconds", seconds(start)}, {"pass", pass}}));
        std::cout << '\n';
        return pass ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
