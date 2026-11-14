#include "appraisal/hf_fullwave.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <numbers>
#include <optional>
#include <string_view>

namespace {

using namespace appraisal;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;

constexpr double frequency_hz = 3.4e6;
constexpr double response_tolerance = .005;
constexpr double delay_tolerance_s = 5e-6;
constexpr double minimum_reflected_power = 1e-8;
constexpr double fine_delta_hz = 10.;
constexpr double coarse_delta_hz = 20.;

struct FrozenColumn {
    const char* name;
    double collision_per_second;
    double d_density_per_m3;
};

constexpr std::array columns{
    FrozenColumn{"f-lossless", 0., 0.},
    FrozenColumn{"f-collisional", 60., 0.},
    FrozenColumn{"df-quiet", 60., 8e8},
    FrozenColumn{"df-enhanced", 60., 8e9},
};

Json response_json(const HfFullwaveResponse& response, double maximum_step_m) {
    const auto r = response.reflection;
    const auto t = response.transmission;
    return object({{"maximum_step_m", maximum_step_m}, {"slabs", response.slabs},
                   {"reflection", Json::Array{r.real(), r.imag()}},
                   {"transmission", Json::Array{t.real(), t.imag()}},
                   {"reflected_power", response.reflected_power()},
                   {"transmitted_power", response.transmitted_power()},
                   {"absorbed_power", response.absorbed_power()}});
}

double response_difference(const HfFullwaveResponse& a, const HfFullwaveResponse& b) {
    return std::max(std::abs(a.reflection - b.reflection), std::abs(a.transmission - b.transmission));
}

std::optional<double> reflection_delay(const HfVerticalColumn& profile, const HfFullwaveResponse& center,
                                       double step_m, double delta_hz) {
    if (center.reflected_power() < minimum_reflected_power) return std::nullopt;
    const auto lower = solve_hf_vertical_column(frequency_hz - delta_hz, profile, step_m);
    const auto upper = solve_hf_vertical_column(frequency_hz + delta_hz, profile, step_m);
    if (lower.reflected_power() < minimum_reflected_power || upper.reflected_power() < minimum_reflected_power)
        return std::nullopt;
    const double low_phase = std::arg(center.reflection * std::conj(lower.reflection));
    const double high_phase = std::arg(upper.reflection * std::conj(center.reflection));
    if (std::max(std::abs(low_phase), std::abs(high_phase)) >= std::numbers::pi / 2)
        return std::nullopt;
    return (low_phase + high_phase) / (4 * std::numbers::pi * delta_hz);
}

Json optional_json(std::optional<double> x) {
    return x ? Json(*x) : Json{};
}

Json column_row(const FrozenColumn& column, double plasma_ratio, bool& all_converged) {
    const HfVerticalColumn profile{plasma_ratio * frequency_hz, column.collision_per_second, column.d_density_per_m3};
    const auto coarse = solve_hf_vertical_column(frequency_hz, profile, 20.);
    const auto medium = solve_hf_vertical_column(frequency_hz, profile, 10.);
    const auto fine = solve_hf_vertical_column(frequency_hz, profile, 5.);
    const double mesh_error = response_difference(medium, fine);
    const bool converged = mesh_error <= response_tolerance;
    all_converged = all_converged && converged;

    const auto delay_fine = reflection_delay(profile, fine, 5., fine_delta_hz);
    const auto delay_frequency_coarse = reflection_delay(profile, fine, 5., coarse_delta_hz);
    const auto delay_mesh_coarse = reflection_delay(profile, medium, 10., fine_delta_hz);
    const bool delay_available = converged && delay_fine && delay_frequency_coarse && delay_mesh_coarse &&
        std::max(std::abs(*delay_fine - *delay_frequency_coarse),
                 std::abs(*delay_fine - *delay_mesh_coarse)) <= delay_tolerance_s;
    const char* delay_status = "resolved";
    if (!converged) delay_status = "complex_response_not_mesh_converged";
    else if (!delay_fine || !delay_frequency_coarse || !delay_mesh_coarse) delay_status = "reflection_null_or_phase_step";
    else if (!delay_available) delay_status = "reflection_delay_not_converged";
    return object({{"column", column.name}, {"peak_f_plasma_over_rf", plasma_ratio},
                   {"peak_f_plasma_frequency_hz", profile.f_peak_plasma_frequency_hz},
                   {"f_collision_per_second", profile.f_collision_per_second},
                   {"d_peak_electron_density_per_m3", profile.d_peak_electron_density_per_m3},
                   {"responses", Json::Array{response_json(coarse, 20.), response_json(medium, 10.), response_json(fine, 5.)}},
                   {"complex_difference_20m_to_10m", response_difference(coarse, medium)},
                   {"complex_difference_10m_to_5m", mesh_error}, {"response_converged", converged},
                   {"reflection_delay_status", delay_status},
                   {"reflection_delay_s", delay_available ? Json(*delay_fine) : Json{}},
                   {"reflection_delay_candidates_s", object({
                       {"step_5m_delta_10hz", optional_json(delay_fine)},
                       {"step_5m_delta_20hz", optional_json(delay_frequency_coarse)},
                       {"step_10m_delta_10hz", optional_json(delay_mesh_coarse)}})}});
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: high_fidelity_fullwave_probe\n"
                     "Evaluate the fixed 14-column 3.4 MHz vertical plasma campaign and emit JSON.\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "The full-wave component probe accepts only --help.\n";
        return 2;
    }

    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto start = std::chrono::steady_clock::now();
    try {
        bool all_converged = true;
        Json::Array rows;
        for (const auto& column : columns)
            for (const double ratio : {.9999, 1., 1.0001})
                rows.push_back(column_row(column, ratio, all_converged));
        for (const double ratio : {.97, 1.03})
            rows.push_back(column_row(columns[2], ratio, all_converged));

        using G = HfVerticalColumn;
        const auto report = object({
            {"schema_version", 1}, {"experiment", "frozen-vertical-plasma-cutoff"},
            {"scope", "single-frequency scalar normal-incidence component with vacuum ports and zero magnetic field"},
            {"source_identity", object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
                                         {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
                                         {"source_commit", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},
                                         {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER},
                                         {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG}})},
            {"rf_hz", frequency_hz}, {"time_convention", "exp(-i omega t)"},
            {"lower_port_m", 0.}, {"upper_port_m", G::top_m},
            {"density_shape", "cos(pi * (z-center) / (2 * half_width))^2 inside each support"},
            {"f_center_m", G::f_center_m}, {"f_half_width_m", G::f_half_width_m},
            {"d_center_m", G::d_center_m}, {"d_half_width_m", G::d_half_width_m},
            {"d_center_collision_per_second", G::d_center_collision_per_second},
            {"d_collision_scale_height_m", G::d_collision_scale_height_m},
            {"mesh_complex_tolerance", response_tolerance}, {"delay_tolerance_s", delay_tolerance_s},
            {"delay_minimum_reflected_power", minimum_reflected_power},
            {"delay_maximum_absolute_local_phase_step_rad", std::numbers::pi / 2},
            {"all_complex_responses_converged", all_converged}, {"receiver_executed", false},
            {"wall_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()},
            {"rows", std::move(rows)}
        });
        write_json(std::cout, report);
        std::cout << '\n';
        return all_converged ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Full-wave component failed: " << error.what() << '\n';
        return 1;
    }
}
