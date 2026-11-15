#include "appraisal/hf_vector_two_hop.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {

using namespace appraisal;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;
using Table = HfVectorTwoHopTable;
using C = std::complex<double>;
using Routes = HfVectorTwoHopRoutes;

Json branch_errors_json(const HfVectorTwoHopBranchErrors& c) {
    return object({{"mesh_voltage_error", c.mesh_voltage_error}, {"refined_order_voltage_error", c.refined_order_voltage_error},
        {"frequency_voltage_error", c.frequency_voltage_error}, {"staged_frequency_voltage_error", c.staged_frequency_voltage_error},
        {"staged_mesh_voltage_error", c.staged_mesh_voltage_error},
        {"staged_refined_order_voltage_error", c.staged_refined_order_voltage_error}});
}

Json temporal_fade_json(const HfVectorTwoHopTemporalFade& c) {
    const auto run = [](const HfVectorTwoHopFadeRun& r) {
        return object({{"qualifying_samples", r.qualifying_samples}, {"longest_run_samples", r.longest_run_samples},
            {"longest_sample_span_seconds", r.longest_sample_span_seconds}});
    };
    return object({{"scope", Table::temporal_fade_scope}, {"evaluated", c.evaluated}, {"sample_count", c.sample_count},
        {"sample_step_seconds", c.sample_step_seconds}, {"voltage_allowance", c.voltage_allowance},
        {"mean_incoherent_band_power", c.mean_incoherent_band_power},
        {"carrier", run(c.carrier)}, {"band", run(c.band)}, {"joint", run(c.joint)}});
}

Json checks_json(const HfVectorTwoHopChecks& c) {
    Json::Array nulls;
    for (std::size_t i = 0; i < c.checked_null_witnesses; ++i) {
        const auto& n = c.null_witnesses[i];
        nulls.push_back(object({{"offset_hz", n.offset_hz}, {"displacement_m", n.displacement_m},
            {"assembled_power", n.assembled_power}, {"direct_power", n.direct_power}, {"voltage_error", n.voltage_error}}));
    }
    return object({{"carrier_qualified", c.carrier_qualified}, {"qualified", c.qualified},
        {"specified_fade_observed", c.specified_fade_observed}, {"rf_points", c.rf_points}, {"rf_solves", c.rf_solves},
        {"two_hop_rf_points", c.two_hop_rf_points}, {"one_hop_rf_points", c.one_hop_rf_points},
        {"moving_angle_fit_points", c.moving_angle_fit_points}, {"checked_moving_angle_witnesses", c.checked_moving_angle_witnesses},
        {"checked_moving_angle_refinement_points", c.checked_moving_angle_refinement_points},
        {"matrix_products", c.matrix_products}, {"maximum_rf_dense_terms", c.maximum_rf_dense_terms},
        {"preparation_peak_workspace_bytes", c.preparation_peak_workspace_bytes},
        {"checked_frequency_witnesses", c.checked_frequency_witnesses}, {"checked_displacement_witnesses", c.checked_displacement_witnesses},
        {"scanned_displacements", c.scanned_displacements}, {"checked_fade_witnesses", c.checked_fade_witnesses},
        {"checked_angular_witnesses", c.checked_angular_witnesses}, {"checked_staged_modes", c.checked_staged_modes},
        {"checked_linear_velocity_witnesses", c.checked_linear_velocity_witnesses},
        {"scanned_null_displacements", c.scanned_null_displacements}, {"checked_null_witnesses", c.checked_null_witnesses},
        {"minimum_assembled_null_power", c.minimum_assembled_null_power}, {"minimum_direct_null_power", c.minimum_direct_null_power},
        {"null_witnesses", std::move(nulls)},
        {"minimum_vacuum_cosine", c.minimum_vacuum_cosine}, {"maximum_flux_excess", c.maximum_flux_excess},
        {"maximum_solve_residual", c.maximum_solve_residual}, {"maximum_ground_power", c.maximum_ground_power},
        {"maximum_outer_order_flux", c.maximum_outer_order_flux}, {"one_hop_carrier_power", c.one_hop_carrier_power},
        {"two_hop_carrier_power", c.two_hop_carrier_power}, {"two_hop_intermediate_nonspecular_flux", c.two_hop_intermediate_nonspecular_flux},
        {"two_hop_intermediate_cross_plane_flux", c.two_hop_intermediate_cross_plane_flux},
        {"maximum_mesh_voltage_error", c.maximum_mesh_voltage_error}, {"maximum_refined_order_voltage_error", c.maximum_refined_order_voltage_error},
        {"maximum_frequency_voltage_error", c.maximum_frequency_voltage_error}, {"maximum_fir_voltage_error", c.maximum_fir_voltage_error},
        {"maximum_displacement_tap_error", c.maximum_displacement_tap_error}, {"maximum_discarded_energy_fraction", c.maximum_discarded_energy_fraction},
        {"minimum_carrier_power", c.minimum_carrier_power}, {"minimum_incoherent_carrier_power", c.minimum_incoherent_carrier_power},
        {"minimum_band_power", c.minimum_band_power}, {"minimum_band_incoherent_power", c.minimum_band_incoherent_power},
        {"average_incoherent_band_power", c.average_incoherent_band_power}, {"carrier_fade_displacement_m", c.carrier_fade_displacement_m},
        {"band_fade_displacement_m", c.band_fade_displacement_m}, {"maximum_band_quadrature_power_error", c.maximum_band_quadrature_power_error},
        {"relative_voltage_error_at_witness_minimum", c.relative_voltage_error_at_witness_minimum},
        {"retarded_peak_error_bound", c.retarded_peak_error_bound}, {"maximum_staged_support_error", c.maximum_staged_support_error},
        {"maximum_staged_frequency_voltage_error", c.maximum_staged_frequency_voltage_error},
        {"maximum_linear_velocity_voltage_error", c.maximum_linear_velocity_voltage_error},
        {"maximum_carrier_shift_voltage_error", c.maximum_carrier_shift_voltage_error},
        {"maximum_moving_angle_voltage_error", c.maximum_moving_angle_voltage_error},
        {"maximum_moving_angle_staged_voltage_error", c.maximum_moving_angle_staged_voltage_error},
        {"two_hop_voltage_errors", branch_errors_json(c.two_hop_voltage_errors)},
        {"one_hop_voltage_errors", branch_errors_json(c.one_hop_voltage_errors)},
        {"temporal_fade", temporal_fade_json(c.temporal_fade)},
        {"maximum_angular_freezing_voltage_error", c.maximum_angular_freezing_voltage_error},
        {"maximum_longitudinal_footprint_offset_m", c.maximum_longitudinal_footprint_offset_m},
        {"maximum_cross_plane_footprint_offset_m", c.maximum_cross_plane_footprint_offset_m}});
}

Json axes_json(const HfVectorTwoHopAxisSearch& search) {
    Json::Array axes;
    for (std::size_t i = 0; i < search.axes.size(); ++i) {
        const auto& a = search.axes[i];
        axes.push_back(object({{"receive_axis_index", i}, {"specified_fade_observed", a.specified_fade_observed},
            {"minimum_carrier_power", a.minimum_carrier_power}, {"minimum_incoherent_carrier_power", a.minimum_incoherent_carrier_power},
            {"minimum_band_power", a.minimum_band_power}, {"minimum_band_incoherent_power", a.minimum_band_incoherent_power},
            {"average_incoherent_band_power", a.average_incoherent_band_power},
            {"carrier_fade_displacement_m", a.carrier_fade_displacement_m}, {"band_fade_displacement_m", a.band_fade_displacement_m}}));
    }
    return axes;
}

void angular_json(Json& out, const HfVectorTwoHopAngularScreen& screen) {
    const auto yz_json = [](const std::array<C, 2>& yz) {
        return Json::Array{Json::Array{yz[0].real(), yz[0].imag()}, Json::Array{yz[1].real(), yz[1].imag()}};
    };
    Json::Array axes, witnesses;
    for (const auto error : screen.maximum_axis_errors) axes.emplace_back(error);
    for (const auto& w : screen.witnesses)
        witnesses.push_back(object({{"displacement_m", w.displacement_m}, {"elapsed_seconds", w.elapsed_seconds},
            {"one_hop_incidence_deg", w.one_hop_incidence_deg}, {"one_hop_vacuum_range_m", w.one_hop_vacuum_range_m},
            {"fixed_angle_yz", yz_json(w.fixed_angle_yz)}, {"recomputed_angle_yz", yz_json(w.recomputed_angle_yz)}}));
    out["nominal_elapsed_seconds"] = screen.nominal_elapsed_seconds;
    out["selected_axis_within_limit"] = screen.selected_axis_within_limit;
    out["maximum_axis_errors"] = std::move(axes);
    out["angular_witnesses"] = std::move(witnesses);
}

void angular_fit_json(Json& out, const HfVectorTwoHopAngularFit& fit) {
    Json::Array attempts, cells;
    for (std::size_t i = 0; i < fit.attempts_run; ++i) {
        const auto& a = fit.attempts[i];
        attempts.push_back(object({{"fit_nodes", a.fit_nodes}, {"checked_cells", a.checked_cells}, {"qualified", a.qualified},
            {"maximum_complex_voltage_error", a.maximum_complex_voltage_error},
            {"maximum_staged_voltage_error", a.maximum_staged_voltage_error}}));
    }
    for (const auto& c : fit.cells)
        cells.push_back(object({{"displacement_m", c.displacement_m}, {"offset_hz", c.offset_hz},
            {"elapsed_seconds", c.elapsed_seconds}, {"half_orders", c.half_orders}, {"refined_vertical", c.refined_vertical}}));
    out["selected_fit_nodes"] = fit.selected_fit_nodes;
    out["checked_extreme_carrier_points"] = fit.checked_extreme_carrier_points;
    out["extreme_carrier_qualified"] = fit.extreme_carrier_qualified;
    out["angular_fit_attempts"] = std::move(attempts);
    out["angular_fit_cells"] = std::move(cells);
}

struct StreamChecks { double direct_error = 0., chunk_error = 0.; };

StreamChecks assess_stream(std::shared_ptr<const Table> table, const HfVectorTwoHopSpec& spec) {
    constexpr std::size_t input_count = 379, length = input_count + HfVectorTwoHopStream::tail_samples();
    constexpr auto angle = 2. * std::numbers::pi * 73.5 / Table::sample_rate_hz;
    const auto g = hf_vector_two_hop_geometry(spec);
    const auto start = static_cast<std::uint64_t>(Table::sample_rate_hz * (g.f_height_epoch_seconds + 32.));
    std::array<C, input_count> source;
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = std::polar(.6 + .2 * std::sin(.036 * i), angle * i);
    const auto run = [&](std::size_t chunk) {
        std::array<C, length> result{};
        HfVectorTwoHopStream stream(table, spec, start);
        for (std::size_t i = 0; i < input_count; i += chunk) {
            const auto size = std::min(chunk, input_count - i);
            stream.process(std::span{source}.subspan(i, size), std::span{result}.subspan(i, size));
        }
        for (std::size_t i = input_count; i < result.size(); i += chunk) {
            const auto size = std::min(chunk, result.size() - i);
            if (stream.drain(std::span{result}.subspan(i, size)) != size) throw std::runtime_error("two-hop tail ended early");
        }
        if (stream.drain(result) != 0) throw std::runtime_error("two-hop stream exceeded declared tail");
        return result;
    };
    const auto bulk = run(input_count);
    const auto chunks = run(23);
    StreamChecks result;
    for (std::size_t i = 0; i < bulk.size(); ++i) {
        C direct{};
        const auto t = (static_cast<double>(start + i) - Table::modelling_delay_samples) / Table::sample_rate_hz;
        for (std::size_t lag = Table::first_lag_samples(); lag <= Table::last_lag_samples(); ++lag)
            if (i >= lag && i - lag < source.size()) direct += table->coefficient_at_time(lag, t) * source[i - lag];
        result.direct_error = std::max(result.direct_error, std::abs(direct - bulk[i]));
        result.chunk_error = std::max(result.chunk_error, std::abs(chunks[i] - bulk[i]));
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        const auto start = std::chrono::steady_clock::now();
        auto out = object({{"kind", "appraisal_hf_vector_two_hop_numerics"}, {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
            {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA}});
        bool passed = false;
        if (argc == 2 && std::string_view(argv[1]) == "axis-search") {
            const auto results = assess_hf_vector_two_hop_axes();
            out["mode"] = "axis-search";
            out["selected_axis_index"] = results.selected_axis_index;
            out["checks"] = checks_json(results.checks);
            out["axes"] = axes_json(results);
            passed = results.checks.carrier_qualified && results.selected_axis_index < results.axes.size() &&
                results.axes[results.selected_axis_index].specified_fade_observed;
        } else {
            if (argc < 4 || argc > 5)
                throw std::invalid_argument("usage: high_fidelity_vector_two_hop_probe primary|holdout two-hop|one-hop|both carrier|full [axis-index], primary|holdout one-hop angular|angular-interpolation [axis-index], or axis-search");
            const std::string_view field = argv[1], route = argv[2], mode = argv[3];
            HfVectorTwoHopSpec spec;
            if (field == "holdout") spec.field = HfVectorTwoHopField::holdout;
            else if (field != "primary") throw std::invalid_argument("unknown two-hop field");
            if (route == "two-hop") spec.routes = Routes::two_hop;
            else if (route == "one-hop") spec.routes = Routes::one_hop;
            else if (route != "both") throw std::invalid_argument("unknown two-hop route selection");
            if (argc == 5) {
                const std::string_view raw = argv[4];
                const auto end = raw.data() + raw.size();
                const auto parsed = std::from_chars(raw.data(), end, spec.receive_axis_index);
                if (parsed.ec != std::errc{} || parsed.ptr != end) throw std::invalid_argument("invalid two-hop receive axis");
            }
            HfVectorTwoHopTableBuild result;
            bool angular_passed = false;
            if (mode == "full") result = build_hf_vector_two_hop_table(spec);
            else if (mode == "carrier") result.checks = assess_hf_vector_two_hop_carrier(spec);
            else if (mode == "angular") {
                const auto screen = assess_hf_vector_two_hop_angular(spec);
                result.checks = screen.checks;
                angular_json(out, screen);
                angular_passed = screen.selected_axis_within_limit;
            }
            else if (mode == "angular-interpolation") {
                const auto fit = assess_hf_vector_two_hop_angular_fit(spec);
                result.checks = fit.checks;
                angular_fit_json(out, fit);
                angular_passed = fit.selected_fit_nodes != 0;
            }
            else throw std::invalid_argument("unknown two-hop preflight mode");
            const auto g = hf_vector_two_hop_geometry(spec);
            const auto stream = result.table ? assess_stream(result.table, spec) : StreamChecks{};
            out["mode"] = mode;
            out["field"] = field;
            out["route"] = route;
            out["receive_axis_index"] = spec.receive_axis_index;
            out["two_hop_vacuum_range_m"] = g.two_hop_vacuum_range_m;
            out["one_hop_vacuum_range_m"] = g.one_hop_vacuum_range_m;
            out["f_height_amplitude_m"] = g.f_height_amplitude_m;
            out["f_height_period_seconds"] = g.f_height_period_seconds;
            out["table_workspace_bytes"] = result.table ? result.table->workspace_bytes() : 0u;
            out["moving_angle_fit_coefficient_count"] = result.moving_angle_fit_coefficients.size();
            out["stream_direct_error"] = stream.direct_error;
            out["stream_chunk_error"] = stream.chunk_error;
            out["checks"] = checks_json(result.checks);
            passed = (mode == "angular" || mode == "angular-interpolation") ? angular_passed : mode == "carrier" ? result.checks.carrier_qualified :
                result.table && result.checks.qualified && stream.direct_error <= 2e-13 && stream.chunk_error == 0.;
        }
        out["elapsed_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        write_json(std::cout, out);
        std::cout << '\n';
        return passed ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "HF vector two-hop probe: " << error.what() << '\n';
        return 1;
    }
}
