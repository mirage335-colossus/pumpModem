#include "appraisal/hf_vector_oblique.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
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
using Table = HfVectorObliqueTable;
using C = std::complex<double>;

double impulse_check(const std::shared_ptr<const Table>& table, const HfVectorObliqueSpec& spec) {
    // Independently compute the expected sample clock of a delayed impulse
    // with a nonzero starting index; verify arbitrary chunks and full tail.
    constexpr std::size_t prefix = 13, chunk = 4;
    constexpr std::size_t start = 672133;
    constexpr C impulse{.75, -.25};
    HfVectorObliqueStream stream(table, spec, start);
    std::array<C, prefix> input{};
    std::array<C, prefix + HfVectorObliqueStream::tail_samples()> result{};
    input[0] = impulse;
    for (std::size_t i = 0; i < input.size(); i += chunk) {
        const auto n = std::min(chunk, input.size() - i);
        stream.process(std::span{input}.subspan(i, n), std::span{result}.subspan(i, n));
    }
    const auto tail = std::span{result}.subspan(prefix);
    if (stream.drain(tail.first(17)) != 17 || stream.drain(tail.subspan(17)) != tail.size() - 17 ||
        stream.drain(tail) != 0) throw std::runtime_error("HF vector oblique stream tail extent differed");
    double maximum = 0.;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const double seconds = (start + i - Table::modelling_delay_samples) / Table::sample_rate_hz;
        const auto want = i >= Table::first_lag_samples() && i <= Table::last_lag_samples() ?
            impulse * table->coefficient_at_position(i, -spec.drift_mps * (seconds - spec.epoch_seconds)) : C{};
        maximum = std::max(maximum, std::abs(result[i] - want));
    }
    return maximum;
}

struct ToneCheck { double reference_error = 0., chunk_error = 0.; };

ToneCheck moving_tone_check(const std::shared_ptr<const Table>& table, const HfVectorObliqueSpec& spec) {
    constexpr std::size_t count = 2 * Table::last_lag_samples() + 31;
    constexpr std::uint64_t start = 872137;
    constexpr double angle = 2 * std::numbers::pi * 196.5 / Table::sample_rate_hz;
    using Output = std::array<C, count + Table::last_lag_samples()>;
    std::array<C, count> input;
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = std::polar(.8, angle * i + .431);
    const auto run = [&](std::size_t chunk) {
        Output output{};
        HfVectorObliqueStream stream(table, spec, start);
        for (std::size_t i = 0; i < count; i += chunk) {
            const auto n = std::min(chunk, count - i);
            stream.process(std::span{input}.subspan(i, n), std::span{output}.subspan(i, n));
        }
        for (std::size_t i = count; i < output.size(); i += chunk) {
            const auto n = std::min(chunk, output.size() - i);
            if (stream.drain(std::span{output}.subspan(i, n)) != n)
                throw std::runtime_error("HF vector oblique tone lost the delayed tail");
        }
        return output;
    };
    const auto bulk = run(count);
    const auto chunked = run(17);
    ToneCheck check;
    for (std::size_t i = 0; i < bulk.size(); ++i) {
        const double time = (start + i - Table::modelling_delay_samples) / Table::sample_rate_hz;
        const double position = -spec.drift_mps * (time - spec.epoch_seconds);
        C frequency_response{};
        for (std::size_t l = Table::first_lag_samples(); l <= Table::last_lag_samples(); ++l)
            if (i >= l && i - l < count)
                frequency_response += table->coefficient_at_position(l, position) * std::polar(1., -angle * l);
        const auto expected = std::polar(.8, angle * i + .431) * frequency_response;
        check.reference_error = std::max(check.reference_error, std::abs(bulk[i] - expected));
        check.chunk_error = std::max(check.chunk_error, std::abs(bulk[i] - chunked[i]));
    }
    return check;
}

Json values(std::span<const double> seq) {
    Json::Array out;
    for (const auto d : seq) out.emplace_back(d);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    using namespace appraisal;
    try {
        std::cout << std::setprecision(17);
        const std::string_view mode = argc == 3 ? argv[2] : "";
        const std::size_t wide = mode == "carrier-wide5" ? 5 : mode == "carrier-wide7" ? 7 : 0;
        if (argc < 2 || argc > 3 || (argc == 3 && mode != "carrier" && wide == 0))
            throw std::invalid_argument("usage: high_fidelity_vector_oblique_probe primary|holdout|off|no-b|cross|primary-3d|holdout-3d|3d-planar|3d-in-plane [carrier], or holdout-3d carrier-wide5|carrier-wide7");
        HfVectorObliqueSpec spec;
        const auto name = std::string_view(argv[1]);
        if (name == "holdout") { spec.field = 1; spec.drift_mps = 7.5; spec.epoch_seconds = 520.; }
        else if (name == "off") spec.perturbations = false;
        else if (name == "no-b") spec.magnetic = false;
        else if (name == "cross") spec.antenna = 1;
        else if (name == "primary-3d") spec.field = 2;
        else if (name == "holdout-3d") { spec.field = 3; spec.drift_mps = 7.5; spec.epoch_seconds = 520.; }
        else if (name == "3d-planar") { spec.field = 2; spec.perturbations = false; }
        else if (name == "3d-in-plane") { spec.field = 2; spec.cross_plane = false; }
        else if (name != "primary") throw std::invalid_argument("unknown vector oblique field");
        const bool carrier_only = argc == 3;
        const auto start = std::chrono::steady_clock::now();
        HfVectorObliqueTableBuild result;
        if (wide) result.checks = assess_hf_vector_oblique_carrier_resolution(spec, wide);
        else if (carrier_only) result.checks = assess_hf_vector_oblique_carrier(spec);
        else result = build_hf_vector_oblique_table(spec);
        const auto& checks = result.checks;
        const auto g = hf_vector_oblique_geometry(spec);
        const double impulse_error = result.table ? impulse_check(result.table, spec) : 0.;
        const auto tone = result.table ? moving_tone_check(result.table, spec) : ToneCheck{};
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        auto out = object({{"kind", "appraisal_hf_vector_oblique_numerics"},
            {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA}, {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
            {"field", static_cast<std::uint64_t>(spec.field)}, {"antenna", static_cast<std::uint64_t>(spec.antenna)},
            {"perturbations", spec.perturbations}, {"magnetic", spec.magnetic},
            {"drift_mps", spec.drift_mps}, {"epoch_seconds", spec.epoch_seconds},
            {"carrier_hz", Table::carrier_hz}, {"carrier_only", carrier_only},
            {"incidence_deg", g.incidence_deg}, {"height_m", g.height_m}, {"receiver_x_m", g.receiver_x_m},
            {"period_m", g.period_m}, {"thickness_m", g.thickness_m}, {"peak_plasma_hz", g.peak_plasma_hz},
            {"collisions_per_second", g.collision_rate}, {"corrugated_slice_m", g.corrugated_slice_m},
            {"corrugation_center_fraction", g.corrugation_center_fraction}, {"field_tesla", values(g.field_tesla)},
            {"antenna_axis", values(g.antenna_axis)}, {"launch_tm_fraction", g.launch_tm_fraction},
            {"density_harmonic_x", values(g.density_harmonic_x)}, {"density_harmonic_phase_rad", values(g.density_harmonic_phase_rad)},
            {"qualified", checks.qualified}, {"elapsed_seconds", elapsed}, {"rf_solves", checks.rf_solves},
            {"matrix_products", checks.matrix_products}, {"preparation_peak_workspace_bytes", checks.preparation_peak_workspace_bytes},
            {"prepared_table_workspace_bytes", result.table ? result.table->workspace_bytes() : 0u},
            {"output_half_orders", checks.output_half_orders}, {"checked_frequencies", checks.checked_frequencies},
            {"checked_positions", checks.checked_positions}, {"minimum_vacuum_cosine", checks.minimum_vacuum_cosine},
            {"checked_table_assembly_positions", checks.checked_table_assembly_positions},
            {"maximum_flux_excess", checks.maximum_flux_excess}, {"maximum_solve_residual", checks.maximum_solve_residual},
            {"mesh_voltage_error", checks.mesh_voltage_error}, {"order_voltage_error", checks.order_voltage_error},
            {"refined_order_voltage_error", checks.refined_order_voltage_error}, {"maximum_edge_flux", checks.maximum_edge_flux},
            {"frequency_voltage_error", checks.frequency_voltage_error}, {"fir_voltage_error", checks.fir_voltage_error},
            {"table_assembly_voltage_error", checks.table_assembly_voltage_error},
            {"carrier_reflected_flux", checks.carrier_reflected_flux}, {"carrier_transmitted_flux", checks.carrier_transmitted_flux},
            {"carrier_scattered_reflected_flux", checks.carrier_scattered_reflected_flux},
            {"carrier_crosspolar_reflected_flux", checks.carrier_crosspolar_reflected_flux},
            {"carrier_crosspolar_scattered_flux", checks.carrier_crosspolar_scattered_flux},
            {"minimum_carrier_power", checks.minimum_carrier_power}, {"maximum_carrier_power", checks.maximum_carrier_power},
            {"minimum_carrier_position_m", checks.minimum_carrier_position_m},
            {"certified_minimum_power_lower_bound", checks.certified_minimum_power_lower_bound},
            {"minimum_checked_band_power_lower_bound", checks.minimum_checked_band_power_lower_bound},
            {"minimum_checked_band_power_witness_offset_hz", checks.minimum_checked_band_power_witness_offset_hz},
            {"relative_voltage_error_at_minimum", checks.relative_voltage_error_at_minimum},
            {"period_averaged_discarded_energy_fraction", checks.period_averaged_discarded_energy_fraction},
            {"period_averaged_raw_precursor_energy_fraction", checks.period_averaged_raw_precursor_energy_fraction},
            {"interpolation_peak_error_bound", checks.interpolation_peak_error_bound},
            {"retarded_peak_error_bound", checks.retarded_peak_error_bound}, {"impulse_chunk_tail_error", impulse_error},
            {"moving_tone_reference_error", tone.reference_error}, {"moving_tone_chunk_error", tone.chunk_error}});
        if (spec.field >= 2) {
            Json::Array density_modes;
            for (const auto& mode : g.transverse_density_modes)
                density_modes.push_back(object({{"m", mode.m}, {"n", mode.n},
                    {"amplitude_x", mode.amplitude_x}, {"phase_rad", mode.phase_rad}}));
            out["cross_plane"] = spec.cross_plane;
            out["period_y_m"] = g.period_y_m;
            out["receiver_y_m"] = g.receiver_y_m;
            out["drift_y_mps"] = g.drift_y_mps;
            out["trajectory_winding_y"] = g.trajectory_winding_y;
            out["transverse_density_modes"] = std::move(density_modes);
            out["output_half_orders_y"] = checks.output_half_orders_y;
            out["output_trajectory_half_orders"] = checks.output_trajectory_half_orders;
            out["coarse_half_orders"] = Json::Array{checks.coarse_half_orders[0], checks.coarse_half_orders[1]};
            out["refined_half_orders"] = Json::Array{checks.refined_half_orders[0], checks.refined_half_orders[1]};
            out["maximum_rf_dense_terms"] = checks.maximum_rf_dense_terms;
            out["rf_dense_terms_limit"] = checks.rf_dense_terms_limit;
            out["maximum_admitted_spatial_modes"] = checks.maximum_admitted_spatial_modes;
            const auto workspace_bound = carrier_only ?
                (wide ? hf_vector_wide_carrier_workspace_bound_bytes : Table::three_d_preparation_workspace_bound_bytes) :
                hf_vector_oblique_preparation_workspace_bound_bytes(spec);
            out["preparation_workspace_bound_bytes"] = workspace_bound;
            out["carrier_witnesses_qualified"] = checks.carrier_witnesses_qualified;
            out["carrier_out_of_plane_reflected_flux"] = checks.carrier_out_of_plane_reflected_flux;
            out["carrier_out_of_plane_transmitted_flux"] = checks.carrier_out_of_plane_transmitted_flux;
            out["carrier_cross_plane_ablation_compared"] = checks.carrier_cross_plane_ablation_compared;
            out["carrier_cross_plane_ablation_voltage_error"] = checks.carrier_cross_plane_ablation_voltage_error;
            out["maximum_edge_x_flux"] = checks.maximum_edge_x_flux;
            out["maximum_edge_y_flux"] = checks.maximum_edge_y_flux;
            out["carrier_refined_edge_flux"] = checks.carrier_refined_edge_flux;
            out["carrier_refined_edge_x_flux"] = checks.carrier_refined_edge_x_flux;
            out["carrier_refined_edge_y_flux"] = checks.carrier_refined_edge_y_flux;
        }
        if (wide) {
            out["wide_carrier_probe"] = true;
            out["requested_coarse_half_order_each_axis"] = wide - 1;
            out["requested_production_half_order_each_axis"] = wide;
            out["requested_refined_half_order_each_axis"] = wide + 1;
            const auto refined_width = 2 * (wide + 1) + 1;
            out["largest_spatial_mode_count"] = refined_width * refined_width;
            out["carrier_dense_terms_limit"] = hf_vector_wide_carrier_dense_terms_bound;
            out["edge_flux_scope"] = "mixed-launch reflected and transmitted flux on either outer axis of the full unfolded spatial grid, with corners counted once in the union";
        }
        write_json(std::cout, out);
        std::cout << '\n';
        if (wide) return checks.carrier_witnesses_qualified ? 0 : 3;
        return carrier_only || (checks.qualified && result.table && impulse_error <= 2e-15 &&
            tone.reference_error <= 2e-13 && tone.chunk_error == 0.) ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "HF vector oblique probe: " << error.what() << '\n';
        return 1;
    }
}
