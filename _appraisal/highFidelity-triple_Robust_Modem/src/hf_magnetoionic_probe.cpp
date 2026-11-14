#include "appraisal/hf_magnetoionic_kernel.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {

using namespace appraisal;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;
using Table = HfMagnetoionicKernelTable;
using Complex = std::complex<double>;
using Field = HfMagnetoionicProfileStream::Field;
using Clock = std::chrono::steady_clock;

Json error_json(const HfFullwaveBandError& error) {
    return error.maximum < 0. ? Json{} : object({{"absolute_complex_error", error.maximum},
        {"q", error.profile_q}, {"rf_offset_hz", error.offset_hz}, {"pass", error.maximum <= Table::complex_tolerance}});
}

Json scale_json(const HfMagnetoionicScaleChecks& scale) {
    const auto row = [](const HfMagnetoionicScaleWitness& item) {
        return item.raw_amplitude < 0. ? Json{} : object({{"raw_amplitude", item.raw_amplitude},
            {"relative_error", item.relative_error < 0. ? Json{} : Json{item.relative_error}},
            {"absolute_error", item.absolute_error}, {"q", item.profile_q}, {"rf_offset_hz", item.offset_hz}});
    };
    return object({{"samples", scale.samples}, {"exact_zero_raw", scale.exact_zero_raw},
        {"absolute_error_at_least_raw", scale.absolute_error_at_least_raw}, {"minimum_raw_amplitude", row(scale.minimum_raw_amplitude)},
        {"maximum_relative_error", row(scale.maximum_relative_error)},
        {"raw_at_maximum_absolute_error", row(scale.raw_at_maximum_absolute_error)}});
}

Json projection_json(const HfMagnetoionicProjectionWitness& witness) {
    return witness.fraction_of_vector_power < 0. ? Json{} : object({{"fraction_of_vector_power", witness.fraction_of_vector_power},
        {"vector_power", witness.vector_power}, {"q", witness.profile_q}, {"rf_offset_hz", witness.offset_hz}});
}

Json polarization_json(const HfMagnetoionicKernelChecks& checks, std::size_t intervals) {
    Json::Array offsets;
    for (std::size_t i = 0; i < checks.knot_polarization.size(); ++i) {
        Json::Array knots;
        for (std::size_t k = 0; k < checks.knot_polarization[i].size(); ++k) {
            const auto& row = checks.knot_polarization[i][k];
            if (row.vector_power == 0.) continue;
            const auto& s = row.normalized_stokes;
            knots.push_back(object({{"q", static_cast<double>(k) / intervals}, {"vector_power", row.vector_power},
                {"normalized_stokes_s1_s2_s3", Json::Array{s[0], s[1], s[2]}},
                {"overlap_power_with_q_zero", row.overlap_power_with_initial < 0. ? Json{} : Json{row.overlap_power_with_initial}}}));
        }
        offsets.push_back(object({{"rf_offset_hz", hf_magnetoionic_polarization_offsets_hz[i]}, {"knots", std::move(knots)}}));
    }
    return object({{"convention", "RF solver fixed lab axes: S=(|x|^2-|y|^2,2 Re(x conj(y)),2 Im(x conj(y)))/P"},
        {"fixed_rf_offsets", std::move(offsets)}});
}

Json null_witness_json(const HfMagnetoionicNullWitness& row) {
    const auto pair = [](const std::array<double, 2>& v) { return Json::Array{v[0], v[1]}; };
    return object({{"q", row.profile_q}, {"rf_offset_hz", row.offset_hz}, {"fine_vector_power", row.fine_vector_power},
        {"witness_meshes_m", Json::Array{Table::production_step_m, 5., 2.5}}, {"fine_amplitude_xy", pair(row.fine_amplitude)},
        {"fir_amplitude_xy", pair(row.fir_amplitude)}, {"fir_to_10m_absolute_error_xy", pair(row.fir_to_production_mesh_error)},
        {"mesh_10m_to_2p5m_absolute_difference_xy", pair(row.production_to_fine_mesh_error)},
        {"mesh_5m_to_2p5m_absolute_difference_xy", pair(row.intermediate_to_fine_mesh_error)},
        {"fir_to_2p5m_absolute_error_xy", pair(row.fir_to_fine_mesh_error)},
        {"x_guarded_absolute_error", row.x_guarded_absolute_error}, {"x_guarded_power_interval", pair(row.x_guarded_power_interval)},
        {"x_guarded_relative_error", row.fine_amplitude[0] > 0. ? Json{row.x_guarded_absolute_error / row.fine_amplitude[0]} : Json{}},
        {"x_local_relative_tolerance", Table::original_null_relative_tolerance}, {"x_resolved", row.x_resolved}});
}

Json null_retardation_json(const Table& table, const HfMagnetoionicNullWitness& witness) {
    constexpr double duration_samples = Table::transition_seconds * Table::sample_rate_hz;
    constexpr auto delay = Table::modelling_delay_samples;
    const double exact = delay + duration_samples * std::acos(1. - 2. * witness.profile_q) / std::numbers::pi;
    const auto sample = static_cast<std::size_t>(std::llround(exact));
    const double q = hf_magnetoionic_profile_fraction(sample, 0, duration_samples);
    const double rotation_per_sample = -2. * std::numbers::pi * witness.offset_hz / Table::sample_rate_hz;
    Json::Array outputs;
    for (std::size_t p = 0; p < Field{}.size(); ++p) {
        Complex output_time{}, retarded{};
        for (auto lag = Table::first_lag_samples(); lag <= Table::last_lag_samples(); ++lag) {
            // The native steady tone contributes exp(-i*2*pi*f*lag/Fs).
            // At lag l the retarded coefficient uses source time n-l,
            // while the nominal profile uses output time n-delay.
            const auto tone = std::polar(1., rotation_per_sample * lag);
            const auto q_retarded = hf_magnetoionic_profile_fraction(sample + delay - lag, 0, duration_samples);
            output_time += tone * table.coefficient_at_q(p, lag, q);
            retarded += tone * table.coefficient_at_q(p, lag, q_retarded);
        }
        const double difference = std::abs(output_time - retarded);
        const double bound = table.retarded_peak_error_bound(Table::transition_seconds, p);
        outputs.push_back(object({{"laboratory_output", p == 0 ? "x" : "y"}, {"output_time_amplitude", std::abs(output_time)},
            {"retarded_amplitude", std::abs(retarded)}, {"absolute_difference", difference},
            {"relative_to_sampled_fine_witness_amplitude", witness.fine_amplitude[p] > 0.
                ? Json{difference / witness.fine_amplitude[p]} : Json{}},
            {"finite_fir_peak_difference_bound", bound}, {"bound_consistent", difference <= bound + 2e-12}}));
    }
    return object({{"scope", "Unit steady monochromatic tone at the nearest output sample to the archived profile fraction. Difference of two finite-FIR time conventions."},
        {"transition_seconds", Table::transition_seconds}, {"output_sample", sample}, {"profile_q", q},
        {"profile_q_offset_from_original_witness", q - witness.profile_q}, {"rf_offset_hz", witness.offset_hz},
        {"outputs", std::move(outputs)}});
}

Json waveform_check(const std::shared_ptr<const Table>& table, bool& pass) {
    constexpr double duration = Table::transition_seconds;
    constexpr std::size_t samples = 1024, physical_transition_first = 1200;
    constexpr std::size_t first = static_cast<std::size_t>(duration * Table::sample_rate_hz / 2.) + physical_transition_first - samples / 2;
    constexpr std::size_t tail = Table::last_lag_samples();
    HfMagnetoionicProfileStream whole(table, physical_transition_first, duration, first);
    HfMagnetoionicProfileStream chunked(table, physical_transition_first, duration, first);
    std::array<Complex, samples> input{};
    double peak_input = 0.;
    for (std::size_t i = 0; i < samples; ++i) {
        const double t = i / Table::sample_rate_hz;
        input[i] = std::polar(.54, .3 + 2. * std::numbers::pi * t * 171.53) +
                   std::polar(.38, -.6 - 2. * std::numbers::pi * t * 311.27);
        peak_input = std::max(peak_input, std::abs(input[i]));
    }
    std::array<Field, samples + tail> output{}, chunks{};
    whole.process(input, std::span{output}.first(samples));
    const bool full_drain = whole.drain(std::span{output}.last(tail)) == tail;
    for (std::size_t at = 0; at < samples;) {
        const auto length = std::min<std::size_t>(127, samples - at);
        chunked.process(std::span{input}.subspan(at, length), std::span{chunks}.subspan(at, length));
        at += length;
    }
    const bool split_drain = chunked.drain(std::span{chunks}.subspan(samples, 13)) == 13 &&
        chunked.drain(std::span{chunks}.subspan(samples + 13)) == tail - 13 && chunked.drain(std::span{chunks}.first(1)) == 0;
    const bool chunk_equal = output == chunks;
    const bool causal = std::ranges::all_of(std::span{output}.first(Table::first_lag_samples()),
        [](const auto& x) { return x == Field{}; });
    HfMagnetoionicProfileStream zero_stream(table, physical_transition_first, duration, first);
    constexpr std::size_t zero_samples = 97;
    const std::array<Complex, zero_samples> zero_input{};
    std::array<Field, zero_samples + tail> zero_output;
    zero_stream.process(zero_input, std::span{zero_output}.first(zero_samples));
    const bool zero_exact = zero_stream.drain(std::span{zero_output}.last(tail)) == tail &&
        std::ranges::all_of(zero_output, [](const auto& x) { return x == Field{}; });
    const auto independent_q = [](double clock) {
        const auto u = std::clamp((clock - physical_transition_first) / (duration * Table::sample_rate_hz), 0., 1.);
        const auto s = std::sin(std::numbers::pi * u / 2.);
        return s * s;
    };
    std::array<double, 2> direct_error{}, retarded_difference{}, tail_energy{};
    for (std::size_t n = 0; n < output.size(); ++n) {
        const auto clock = static_cast<double>(first + n);
        const auto q = independent_q(clock - Table::modelling_delay_samples);
        for (std::size_t p = 0; p < Field{}.size(); ++p) {
            Complex direct{}, retarded{};
            for (std::size_t lag = Table::first_lag_samples(); lag <= tail; ++lag) {
                if (lag > n || n - lag >= samples) continue;
                direct += input[n - lag] * table->coefficient_at_q(p, lag, q);
                retarded += input[n - lag] * table->coefficient_at_q(p, lag, independent_q(clock - lag));
            }
            direct_error[p] = std::max(direct_error[p], std::abs(direct - output[n][p]));
            retarded_difference[p] = std::max(retarded_difference[p], std::abs(retarded - output[n][p]));
            if (n >= samples) tail_energy[p] += std::norm(output[n][p]);
        }
    }
    Json::Array component_checks;
    pass = causal && chunk_equal && full_drain && split_drain && zero_exact;
    for (std::size_t p = 0; p < Field{}.size(); ++p) {
        const double bound = peak_input * table->retarded_peak_error_bound(duration, p);
        const bool valid = tail_energy[p] > 0. && direct_error[p] <= 2e-12 && retarded_difference[p] <= bound + 2e-12;
        pass &= valid;
        component_checks.push_back(object({{"laboratory_output", p == 0 ? "x" : "y"},
            {"tail_energy", tail_energy[p]}, {"direct_convolution_max_abs_error", direct_error[p]},
            {"retarded_convolution_max_abs_difference", retarded_difference[p]},
            {"retarded_convolution_difference_bound", bound}, {"pass", valid}}));
    }
    return object({{"input_samples", samples}, {"tail_samples", tail}, {"chunk_identical", chunk_equal},
        {"no_prearrival_output", causal}, {"complete_tail", full_drain && split_drain},
        {"zero_input_and_tail_exact_zero", zero_exact},
        {"stream_workspace_bytes", whole.workspace_bytes()}, {"components", std::move(component_checks)}, {"pass", pass}});
}

int run_table(HfMagnetoionicPath path) {
    constexpr double fast_duration_seconds = .001;
    const auto start = Clock::now();
    const auto result = build_hf_magnetoionic_kernel_table({}, path);
    const auto intervals = Table::profile_intervals(path);
    const auto build_seconds = std::chrono::duration<double>(Clock::now() - start).count();
    const auto& c = result.checks;
    Json::Array ports;
    for (std::size_t p = 0; p < Field{}.size(); ++p) {
        const double d1 = c.peak_error_seconds_per_q[p];
        ports.push_back(object({{"laboratory_output", p == 0 ? "x" : "y"}, {"knot_grid", error_json(c.knot_grid[p])},
            {"knot_offgrid", error_json(c.knot_offgrid[p])}, {"profile_holdout", error_json(c.profile_holdout[p])},
            {"frequency_refinement", error_json(c.witness_frequency_refinement[p])}, {"mesh", error_json(c.witness_mesh[p])},
            {"occupied_knot_offgrid_scale", scale_json(c.knot_offgrid_scale[p])},
            {"occupied_profile_holdout_scale", scale_json(c.profile_holdout_scale[p])},
            {"occupied_mesh_scale", scale_json(c.witness_mesh_scale[p])},
            {"minimum_occupied_projection_fraction", projection_json(c.minimum_occupied_projection_fraction[p])},
            {"maximum_occupied_projection_fraction", projection_json(c.maximum_occupied_projection_fraction[p])},
            {"maximum_discarded_energy_fraction", c.maximum_discarded_energy[p]}, {"peak_error_seconds_per_q", d1},
            {"retarded_peak_error_bound", std::numbers::pi * d1 / (2. * Table::transition_seconds)},
            {"fast_control_peak_error_bound", std::numbers::pi * d1 / (2. * fast_duration_seconds)}}));
    }
    Json waveform;
    bool waveform_pass = false, fast_rejected = false;
    if (result.table) {
        waveform = waveform_check(result.table, waveform_pass);
        try { HfMagnetoionicProfileStream fast(result.table, 0, fast_duration_seconds); }
        catch (const std::invalid_argument&) { fast_rejected = true; }
    }
    Json::Array profile_settings;
    for (const double q : {0., .5, 1.}) {
        const auto profile = Table::profile_at_q(q, path);
        Json::Array offsets;
        for (const auto f : hf_magnetoionic_holdout_offsets_hz(q, path)) offsets.push_back(f);
        profile_settings.push_back(object({{"q", q}, {"f_peak_plasma_frequency_hz", profile.f_peak_plasma_frequency_hz},
            {"d_peak_electron_density_per_m3", profile.d_peak_electron_density_per_m3},
            {"f_collision_per_second", profile.f_collision_per_second}, {"cutoff_offset_hz", Table::cutoff_offset_hz(q, path)},
            {"holdout_offsets_hz", std::move(offsets)}}));
    }
    const auto b = Table::field_at_path(path);
    const auto& minimum = c.minimum_carrier_power;
    const auto& maximum = c.maximum_carrier_power;
    const bool pass = c.qualified && waveform_pass && fast_rejected;
    const auto report = object({
        {"schema_version", 1}, {"experiment", "magnetoionic-layered-reflection-kernel"},
        {"scope", "Frozen collisional magnetized normal incidence and fixed laboratory polarization, followed by an output-time finite-FIR surrogate"},
        {"source_identity", object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
            {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA}, {"source_commit", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},
            {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER}, {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG}})},
        {"carrier_hz", Table::carrier_hz}, {"sample_rate_hz", Table::sample_rate_hz}, {"occupied_half_band_hz", Table::occupied_half_band_hz},
        {"physical_path", path == HfMagnetoionicPath::parallel_upper ? "parallel_upper" : "tilted_lower"},
        {"magnetic_field_tesla", Json::Array{b[0], b[1], b[2]}}, {"transmit_jones_xy", Json::Array{1., 0.}},
        {"first_lag_samples", Table::first_lag_samples()}, {"last_lag_samples", Table::last_lag_samples()},
        {"modelling_delay_samples", Table::modelling_delay_samples}, {"production_step_m", Table::production_step_m},
        {"witness_step_m", Table::witness_step_m}, {"frequency_bins", Table::frequency_bins}, {"intervals", intervals},
        {"profile_holdout_fractions", Json::Array{std::sqrt(2.) / 5., std::sqrt(3.) / 2.}},
        {"profile_settings", std::move(profile_settings)}, {"transition_seconds", Table::transition_seconds},
        {"fast_control_seconds", fast_duration_seconds}, {"fast_control_rejected", fast_rejected},
        {"complex_error_tolerance", Table::complex_tolerance}, {"discarded_energy_tolerance", Table::discarded_energy_tolerance},
        {"retarded_peak_error_tolerance", Table::rate_peak_tolerance},
        {"maximum_frozen_reflected_power_excess", c.maximum_frozen_reflected_power_excess},
        {"frozen_reflected_power_excess_tolerance", hf_magnetoionic_frozen_reflected_power_tolerance()},
        {"maximum_solver_output_power", c.maximum_solver_output_power},
        {"minimum_longitudinal_permittivity", c.minimum_longitudinal_permittivity},
        {"minimum_knot_carrier_power_x_y_total", Json::Array{minimum[0], minimum[1], minimum[2]}},
        {"maximum_knot_carrier_power_x_y_total", Json::Array{maximum[0], maximum[1], maximum[2]}},
        {"rf_solves", c.rf_solves}, {"maximum_rf_solves", Table::rf_solve_limit(path)}, {"knot_profiles_checked", c.knot_profiles_checked},
        {"holdout_profiles_checked", c.holdout_profiles_checked}, {"table_workspace_bytes", result.table ? result.table->workspace_bytes() : 0},
        {"preparation_workspace_bound_bytes", Table::preparation_workspace_bound_bytes},
        {"witnesses_qualified", c.witnesses_qualified}, {"table_qualified", c.qualified}, {"outputs", std::move(ports)},
        {"original_null_witness", c.original_null_witness ? null_witness_json(*c.original_null_witness) : Json{}},
        {"original_null_tone_retardation", result.table && c.original_null_witness
            ? null_retardation_json(*result.table, *c.original_null_witness) : Json{}},
        {"knot_polarization", polarization_json(c, intervals)}, {"waveform", std::move(waveform)}, {"table_build_seconds", build_seconds},
        {"wall_seconds", std::chrono::duration<double>(Clock::now() - start).count()},
        {"evolving_plasma_solved", false}, {"receiver_executed", false}, {"pass", pass}});
    write_json(std::cout, report);
    std::cout << '\n';
    return pass ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const bool parallel_upper = argc == 2 && std::string_view{argv[1]} == "--parallel-upper";
    if (argc != 1 && !parallel_upper) {
        std::cerr << "Usage: high_fidelity_magnetoionic_probe [--parallel-upper]\n";
        return 2;
    }
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    try { return run_table(parallel_upper ? HfMagnetoionicPath::parallel_upper : HfMagnetoionicPath::tilted_lower); }
    catch (const std::exception& error) {
        std::cerr << "Magnetoionic kernel probe aborted: " << error.what() << '\n';
        return 2;
    }
}
