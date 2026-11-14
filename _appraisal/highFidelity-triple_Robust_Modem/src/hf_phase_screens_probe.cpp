#include "appraisal/hf_phase_screens.hpp"
#include "appraisal/hf_wave_optics.hpp"
#include "triad_probe_json.hpp"

#include <chrono>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace appraisal;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;

Json witness(const HfPhaseScreensError& e) {
    return object({{"absolute_error", e.absolute_error}, {"reference_amplitude", e.reference_amplitude},
        {"x_m", e.x_m}, {"offset_hz", e.offset_hz}});
}

Json slab_witness(const HfPhaseScreensSlabWitness& w) {
    return object({{"x_m", w.x_m}, {"offset_hz", w.offset_hz}, {"amplitude_4", w.amplitude_4},
        {"amplitude_8", w.amplitude_8}, {"amplitude_16", w.amplitude_16},
        {"difference_2_4", w.difference_2_4}, {"difference_4_8", w.difference_4_8},
        {"difference_8_16", w.difference_8_16}, {"relative_allowance", w.relative_allowance},
        {"relative_resolved", w.relative_resolved}});
}

Json null_point(const HfPhaseScreensNullPoint& p) {
    return object({{"x_m", p.x_m}, {"amplitude_4", p.amplitude_4}, {"amplitude_8", p.amplitude_8},
        {"amplitude_16", p.amplitude_16}, {"amplitude_fir", p.amplitude_fir}, {"difference_4_8", p.difference_4_8},
        {"difference_8_16", p.difference_8_16}, {"difference_fir_4", p.difference_fir_4},
        {"difference_fir_16", p.difference_fir_16}, {"relative_slab_allowance", p.relative_slab_allowance},
        {"relative_fir_allowance", p.relative_fir_allowance}, {"relative_resolved", p.relative_resolved},
        {"fir_response_rf", Json::Array{p.fir_response_rf.real(), p.fir_response_rf.imag()}},
        {"reference_16_rf", Json::Array{p.reference_16_rf.real(), p.reference_16_rf.imag()}}});
}

Json null_window(const HfPhaseScreensNullWindow& w) {
    Json::Array ranges;
    for (const auto& r : w.unresolved_ranges_m) ranges.push_back(Json::Array{r[0], r[1]});
    return object({{"center_x_m", w.center_x_m}, {"offset_hz", w.offset_hz}, {"anchor_error", w.anchor_error},
        {"sampled_points", w.sampled_points}, {"unresolved_points", w.unresolved_points},
        {"minimum_reference", null_point(w.minimum_reference)}, {"minimum_fir", null_point(w.minimum_fir)},
        {"worst_slab_relative", null_point(w.worst_slab_relative)}, {"worst_fir_relative", null_point(w.worst_fir_relative)},
        {"unresolved_ranges_m", std::move(ranges)}});
}

Json complex_voltage(std::complex<double> v) {
    return object({{"real", v.real()}, {"imag", v.imag()}, {"amplitude", std::abs(v)}});
}

int wave_thickness() {
    using W = HfSingleGaussianThicknessWitness;
    constexpr double f = HfPhaseScreensTable::carrier_hz + W::offset_hz;
    const auto start = std::chrono::steady_clock::now();
    const auto sample = hf_phase_screens_gaussian_thickness_witness();
    const auto uniform = solve_hf_wave_optics(f, 0., 0., 0.).rf;
    Json::Array rows;
    double thin_reference_error = 0., max_change = 0., max_last_slice_change = 0.;
    for (std::size_t i = 0; i < W::x_m.size(); ++i) {
        const auto expected = solve_hf_wave_optics(f, -.00857, W::x_m[i], 0.).rf / uniform;
        const auto& v = sample.atmospheric_slices[i];
        thin_reference_error = std::max(thin_reference_error, std::abs(sample.thin_vacuum[i] - expected));
        max_change = std::max(max_change, std::abs(expected - v[3]));
        max_last_slice_change = std::max(max_last_slice_change, std::abs(v[3] - v[2]));
        rows.push_back(object({{"x_m", W::x_m[i]}, {"analytic_thin", complex_voltage(expected)},
            {"numeric_thin_vacuum", complex_voltage(sample.thin_vacuum[i])},
            {"one_slice_ambient", complex_voltage(v[0])}, {"two_slices", complex_voltage(v[1])},
            {"four_slices", complex_voltage(v[2])}, {"eight_slices", complex_voltage(v[3])},
            {"difference_analytic_to_eight", std::abs(expected - v[3])},
            {"difference_4_to_8", std::abs(v[3] - v[2])}}));
    }
    const bool numerics_ok = thin_reference_error <= 2e-6 && sample.finer_mesh_error_at_850m <= 1e-5 &&
        sample.bigger_box_error_at_850m <= 1e-5;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto result = object({{"kind", "appraisal_single_gaussian_thickness_diagnostic"},
        {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA}, {"rf_hz", f}, {"offset_hz", W::offset_hz},
        {"voltages_deembedded_by_each_uniform_spherical_reference", true},
        {"plasma_spherical_reference_amplitude_vs_vacuum", sample.spherical_baseline_amplitude},
        {"thin_reference_error", thin_reference_error}, {"maximum_analytic_to_eight_difference", max_change},
        {"maximum_four_to_eight_difference", max_last_slice_change},
        {"finer_mesh_error_at_850m", sample.finer_mesh_error_at_850m},
        {"bigger_box_error_at_850m", sample.bigger_box_error_at_850m},
        {"numerics_ok", numerics_ok}, {"elapsed_seconds", seconds}, {"rows", std::move(rows)}});
    write_json(std::cout, result);
    std::cout << '\n';
    return numerics_ok ? 0 : 3;
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        HfPhaseScreensSpec s;
        if (argc != 2) throw std::invalid_argument("usage: high_fidelity_phase_screens_probe off|primary|holdout|wave-thickness");
        const auto name = std::string_view(argv[1]);
        if (name == "wave-thickness") return wave_thickness();
        if (name == "off") s.perturbations = false;
        else if (name == "holdout") s = {true, 1, 750., 35., 490.};
        else if (name != "primary") throw std::invalid_argument("unknown phase screen field");
        const auto start = std::chrono::steady_clock::now();
        const auto build = build_hf_phase_screens_table(s);
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto& c = build.checks;
        Json::Array minima;
        if (s.perturbations) for (const auto& w : c.slab_minima) minima.push_back(slab_witness(w));
        Json::Array nulls;
        if (s.perturbations) for (const auto& w : c.null_windows) nulls.push_back(null_window(w));
        Json result = object({{"kind", "appraisal_hf_phase_screens_numerics"}, {"field", static_cast<std::uint64_t>(s.field)},
            {"perturbations", s.perturbations}, {"impact_y_m", s.impact_y_m}, {"drift_mps", s.drift_mps},
            {"closest_seconds", s.closest_seconds}, {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
            {"qualified", c.qualified}, {"preparation_seconds", seconds}, {"field_solves", c.field_solves},
            {"checked_positions", c.checked_positions}, {"checked_frequency_offsets", c.checked_frequency_offsets},
            {"preparation_peak_workspace_bytes", c.preparation_peak_workspace_bytes},
            {"minimum_carrier_power", c.minimum_carrier_power}, {"maximum_carrier_power", c.maximum_carrier_power},
            {"unresolved_trajectory_points", c.unresolved_trajectory_points}, {"maximum_plane_power_error", c.maximum_plane_power_error},
            {"minimum_longitudinal_flux", c.minimum_longitudinal_flux}, {"maximum_edge_spectral_fraction", c.maximum_edge_spectral_fraction},
            {"maximum_large_angle_fraction", c.maximum_large_angle_fraction},
            {"maximum_discarded_energy_fraction", c.maximum_discarded_energy_fraction},
            {"maximum_raw_precursor_energy_fraction", c.maximum_raw_precursor_energy_fraction},
            {"retarded_peak_error_bound", c.retarded_peak_error_bound}, {"single_screen_difference", c.single_screen_difference},
            {"deep_slab_qualified", c.deep_slab_qualified}, {"deep_slab_witnesses", c.deep_slab_witnesses},
            {"slab_cut_points", c.slab_cut_points},
            {"unresolved_slab_witnesses", c.unresolved_slab_witnesses}, {"slab_minima", std::move(minima)},
            {"worst_slab_relative", slab_witness(c.worst_slab_relative)},
            {"null_windows_qualified", c.null_windows_qualified}, {"null_windows", std::move(nulls)},
            {"null_window_step_m", HfPhaseScreensTable::null_window_step_m},
            {"null_window_radius_m", HfPhaseScreensTable::null_window_radius_m},
            {"mesh", witness(c.mesh)}, {"box", witness(c.box)}, {"slices_1_vs_2", witness(c.slices_1_vs_2)},
            {"slices_2_vs_4", witness(c.slices_2_vs_4)}, {"frequency", witness(c.frequency)},
            {"trajectory", witness(c.trajectory)}, {"fir", witness(c.fir)}});
        write_json(std::cout, result);
        std::cout << '\n';
        return build.table && c.qualified ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "HF phase screens probe: " << e.what() << '\n';
        return 1;
    }
}
