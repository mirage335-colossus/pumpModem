#include "appraisal/hf_fullwave.hpp"
#include "appraisal/hf_fullwave_kernel.hpp"
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
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace appraisal;
using Complex = std::complex<double>;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;

constexpr double rf_hz = 3.4e6, fs_hz = 1920., occupied_hz = 768.;
constexpr double c = 299792458., two_pi = 2. * std::numbers::pi;
constexpr double fine_step_m = 5., mesh_check_step_m = 10.;
constexpr double complex_tolerance = .005, discarded_energy_tolerance = 1e-6;
constexpr std::size_t model_delay = 32, late_samples = 96;
constexpr std::array<std::size_t, 3> grid_sizes{256, 512, 1024};
constexpr std::size_t mesh_grid_size = grid_sizes.front(), max_grid_size = grid_sizes.back();
constexpr std::size_t occupied_holdouts = 51, guard_holdouts_per_side = 7;
constexpr std::size_t holdouts = occupied_holdouts + 2 * guard_holdouts_per_side;
constexpr std::size_t solve_cap = 9216;
constexpr std::array<std::size_t, 5> profile_interval_counts{16, 32, 64, 128, 256};
constexpr std::array profile_rf_offsets{-864., -768., 0., 768., 864.};
constexpr std::size_t max_profile_q_index = 2 * profile_interval_counts.back();
constexpr std::size_t profile_solve_cap = 10300;

struct ProfilePath {
    const char* name;
    double initial_ratio, final_ratio;
};
constexpr std::array paths{ProfilePath{"cutoff_and_d_density", .9999, 1.0001},
                           ProfilePath{"d_density_only_above_cutoff", 1.03, 1.03}};
constexpr std::size_t profiles = 3;
constexpr double d_density_initial = 8e8, d_density_final = 8e9;
constexpr double f_collision = 60.;
constexpr std::size_t path_count = paths.size();
constexpr std::size_t declared_max_solves = path_count * profiles * (max_grid_size + mesh_grid_size + 2 * holdouts);
constexpr std::size_t profile_max_solves = path_count * (max_profile_q_index + 1) * profile_rf_offsets.size() * 2;
static_assert(declared_max_solves <= solve_cap);
static_assert(profile_max_solves <= profile_solve_cap);
static_assert(std::ranges::all_of(profile_interval_counts, [](auto n) { return max_profile_q_index % (2 * n) == 0; }));
constexpr std::array profile_labels{"start", "independent_midpoint", "end"};
constexpr std::array port_labels{"reflection", "transmission"};
constexpr double d_bottom = HfVerticalColumn::d_center_m - HfVerticalColumn::d_half_width_m;
constexpr std::array minimum_lag{2. * d_bottom / c * fs_hz, HfVerticalColumn::top_m / c * fs_hz};

double grid_offset(std::size_t n, std::size_t i) {
    return fs_hz * (static_cast<double>(i) / n - .5);
}

std::array<double, holdouts> holdout_offsets() {
    std::array<double, holdouts> out{};
    std::size_t next = 0;
    // Shift the interior partition away from every dyadic training bin.
    const double shift = std::sqrt(2.) / 3.;
    const auto add_band = [&](double low, double high, std::size_t bins) {
        for (std::size_t i = 0; i < bins; ++i)
            out[next++] = low + (high - low) * ((i + shift) / bins);
    };
    add_band(-fs_hz / 2., -occupied_hz, guard_holdouts_per_side);
    add_band(-occupied_hz, occupied_hz, occupied_holdouts);
    add_band(occupied_hz, fs_hz / 2., guard_holdouts_per_side);
    return out;
}

HfVerticalColumn profile_at(const ProfilePath& path, double fraction) {
    return {rf_hz * (path.initial_ratio + fraction * (path.final_ratio - path.initial_ratio)), f_collision,
            d_density_initial + fraction * (d_density_final - d_density_initial)};
}

Json source_identity() {
    return object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
                   {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
                   {"source_commit", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},
                   {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER},
                   {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG}});
}

class Solver {
public:
    explicit Solver(std::size_t cap) : cap_(cap) {}

    HfFullwaveResponse get(double offset, const HfVerticalColumn& profile, double step_m) {
        if (calls >= cap_) throw std::logic_error("full-wave kernel RF-solve cap reached");
        ++calls;
        return solve_hf_vertical_column(rf_hz + offset, profile, step_m);
    }
    std::size_t calls = 0;

private:
    std::size_t cap_;
};

Complex port(const HfFullwaveResponse& value, std::size_t p) {
    return p == 0 ? value.reflection : value.transmission;
}

struct ProfileSamples {
    explicit ProfileSamples(HfVerticalColumn value) : profile(value) {}
    HfVerticalColumn profile;
    std::array<std::optional<HfFullwaveResponse>, max_grid_size> fine_grid;
    std::array<HfFullwaveResponse, mesh_grid_size> coarse_grid;
    std::array<HfFullwaveResponse, holdouts> fine_holdout;
    std::array<HfFullwaveResponse, holdouts> coarse_holdout;

    const HfFullwaveResponse& fine(std::size_t n, std::size_t i, Solver& solver) {
        auto& bin = fine_grid[i * (max_grid_size / n)];
        if (!bin) bin = solver.get(grid_offset(n, i), profile, fine_step_m);
        return *bin;
    }
};

struct BandErrors {
    double occupied_grid = 0., occupied_offgrid = 0., guard_grid = 0., guard_offgrid = 0.;

    void add(double offset, bool offgrid, double error) {
        const bool occupied = std::abs(offset) <= occupied_hz;
        auto& which = occupied ? (offgrid ? occupied_offgrid : occupied_grid) : (offgrid ? guard_offgrid : guard_grid);
        which = std::max(which, error);
    }
    bool pass() const {
        return std::max({occupied_grid, occupied_offgrid, guard_grid, guard_offgrid}) <= complex_tolerance;
    }
    Json json() const {
        return object({{"occupied_grid", occupied_grid}, {"occupied_offgrid", occupied_offgrid},
                       {"guard_grid", guard_grid}, {"guard_offgrid", guard_offgrid}, {"pass", pass()}});
    }
};

struct KernelResult {
    HfFullwaveKernel kernel;
    BandErrors reconstruction;
    std::optional<BandErrors> grid_refinement;
};

using PortResult = std::array<KernelResult, port_labels.size()>;
using Results = std::array<PortResult, profiles>;
using MeshChecks = std::array<std::array<BandErrors, port_labels.size()>, profiles>;
using MidpointChecks = std::array<BandErrors, port_labels.size()>;

template<class F>
void evaluate_points(std::size_t n, F f) {
    for (std::size_t i = 0; i < n; ++i) f(grid_offset(n, i), false, i);
    const auto offsets = holdout_offsets();
    for (std::size_t i = 0; i < holdouts; ++i) f(offsets[i], true, i);
}

Complex tapered_delayed_target(Complex solver_value, double offset) {
    return std::conj(solver_value) * std::polar(hf_fullwave_guard_taper(offset, fs_hz, occupied_hz),
                                                -two_pi * offset / fs_hz * model_delay);
}

KernelResult make_kernel(ProfileSamples& samples, std::size_t n, std::size_t p, Solver& solver,
                         const KernelResult* previous) {
    std::vector<Complex> grid(n);
    for (std::size_t i = 0; i < n; ++i) grid[i] = port(samples.fine(n, i, solver), p);
    KernelResult result{design_hf_fullwave_kernel(grid, {fs_hz, occupied_hz, minimum_lag[p], model_delay, late_samples}),
                        {}, {}};
    if (previous) result.grid_refinement.emplace();
    evaluate_points(n, [&](double offset, bool offgrid, std::size_t i) {
        const auto target = tapered_delayed_target(offgrid ? port(samples.fine_holdout[i], p) : grid[i], offset);
        const auto actual = hf_fullwave_fir_response(result.kernel.fir, offset / fs_hz);
        result.reconstruction.add(offset, offgrid, std::abs(actual - target));
        if (previous) {
            const auto coarse = hf_fullwave_fir_response(previous->kernel.fir, offset / fs_hz);
            result.grid_refinement->add(offset, offgrid, std::abs(actual - coarse));
        }
    });
    return result;
}

MeshChecks mesh_check(std::array<ProfileSamples, profiles>& samples, Solver& solver) {
    MeshChecks mesh{};
    for (std::size_t k = 0; k < profiles; ++k) {
        auto& s = samples[k];
        evaluate_points(mesh_grid_size, [&](double offset, bool offgrid, std::size_t i) {
            const auto& a = offgrid ? s.fine_holdout[i] : s.fine(mesh_grid_size, i, solver);
            const auto& b = offgrid ? s.coarse_holdout[i] : s.coarse_grid[i];
            for (std::size_t p = 0; p < port_labels.size(); ++p)
                mesh[k][p].add(offset, offgrid, std::abs(port(a, p) - port(b, p)));
        });
    }
    return mesh;
}

MidpointChecks midpoint_check(std::array<ProfileSamples, profiles>& samples, const Results& candidate,
                              std::size_t n, Solver& solver) {
    MidpointChecks errors{};
    evaluate_points(n, [&](double offset, bool offgrid, std::size_t i) {
        const auto& target = offgrid ? samples[1].fine_holdout[i] : samples[1].fine(n, i, solver);
        for (std::size_t p = 0; p < port_labels.size(); ++p) {
            // Smoothstep is exactly 1/2 at the declared transition midpoint.
            const auto a = hf_fullwave_fir_response(candidate.front()[p].kernel.fir, offset / fs_hz);
            const auto b = hf_fullwave_fir_response(candidate.back()[p].kernel.fir, offset / fs_hz);
            errors[p].add(offset, offgrid, std::abs(.5 * (a + b) - tapered_delayed_target(port(target, p), offset)));
        }
    });
    return errors;
}

bool truncated_energy_pass(const HfFullwaveKernel& kernel) {
    return kernel.discarded_pre_energy_fraction + kernel.discarded_tail_energy_fraction <= discarded_energy_tolerance;
}

Json kernel_json(const KernelResult& r, const BandErrors& mesh) {
    const auto& k = r.kernel;
    const bool refined = r.grid_refinement && r.grid_refinement->pass();
    const bool admitted = r.reconstruction.pass() && refined && mesh.pass() && truncated_energy_pass(k);
    return object({{"status", admitted ? "admitted_static" : "unresolved_static"},
                   {"first_lag_samples", k.fir.first_tap_sample},
                   {"final_lag_samples", k.fir.first_tap_sample + k.fir.taps.size() - 1},
                   {"tap_count", k.fir.taps.size()},
                   {"untapered_grid_mean_power", k.untapered_grid_mean_power},
                   {"tapered_grid_mean_power", k.tapered_grid_mean_power},
                   {"solver_returned_only_zero_amplitudes_on_grid", !k.solver_grid_nonzero},
                   {"finite_band_prearrival_energy_fraction", k.prearrival_energy_fraction},
                   {"discarded_pre_energy_fraction", k.discarded_pre_energy_fraction},
                   {"discarded_tail_energy_fraction", k.discarded_tail_energy_fraction},
                   {"discarded_total_energy_pass", truncated_energy_pass(k)},
                   {"reconstruction_max_complex_error", r.reconstruction.json()},
                   {"grid_refinement_max_complex_change", r.grid_refinement ? r.grid_refinement->json() : Json{}},
                   {"mesh_10m_to_5m_max_complex_change", mesh.json()}});
}

Json path_row(const ProfilePath& path, Solver& solver, bool& all_admitted) {
    const auto calls_before = solver.calls;
    std::array samples{ProfileSamples(profile_at(path, 0.)), ProfileSamples(profile_at(path, .5)),
                       ProfileSamples(profile_at(path, 1.))};
    const auto offsets = holdout_offsets();
    for (auto& s : samples) {
        for (std::size_t i = 0; i < holdouts; ++i) {
            s.fine_holdout[i] = solver.get(offsets[i], s.profile, fine_step_m);
            s.coarse_holdout[i] = solver.get(offsets[i], s.profile, mesh_check_step_m);
        }
        for (std::size_t i = 0; i < mesh_grid_size; ++i)
            s.coarse_grid[i] = solver.get(grid_offset(mesh_grid_size, i), s.profile, mesh_check_step_m);
    }
    const auto mesh = mesh_check(samples, solver);
    Results previous{};
    Json::Array looks;
    bool any_admitted = false, both_admitted = false;
    for (const auto n : grid_sizes) {
        Results next{};
        for (std::size_t k = 0; k < profiles; ++k)
            for (std::size_t p = 0; p < port_labels.size(); ++p)
                next[k][p] = make_kernel(samples[k], n, p, solver, n == grid_sizes.front() ? nullptr : &previous[k][p]);
        const auto midpoint = midpoint_check(samples, next, n, solver);
        Json::Array ports;
        bool all_refined = true;
        any_admitted = false;
        both_admitted = true;
        for (std::size_t p = 0; p < port_labels.size(); ++p) {
            bool static_pass = true;
            Json::Array profile_rows;
            for (std::size_t k = 0; k < profiles; ++k) {
                const auto& r = next[k][p];
                const bool refined = r.grid_refinement && r.grid_refinement->pass();
                all_refined &= refined;
                static_pass &= r.reconstruction.pass() && refined && mesh[k][p].pass() && truncated_energy_pass(r.kernel);
                const auto& profile = samples[k].profile;
                profile_rows.push_back(object({{"profile", profile_labels[k]},
                    {"f_peak_plasma_frequency_hz", profile.f_peak_plasma_frequency_hz},
                    {"d_peak_electron_density_per_m3", profile.d_peak_electron_density_per_m3},
                    {"result", kernel_json(r, mesh[k][p])}}));
            }
            const bool admitted = static_pass && midpoint[p].pass();
            any_admitted |= admitted;
            both_admitted &= admitted;
            ports.push_back(object({{"port", port_labels[p]}, {"status", admitted ? "admitted" : "unresolved"},
                {"vacuum_time_of_flight_lower_bound_samples", minimum_lag[p]},
                {"vacuum_time_of_flight_lower_bound_seconds", minimum_lag[p] / fs_hz},
                {"all_frozen_kernels_pass", static_pass},
                {"midpoint_interpolation_max_complex_error", midpoint[p].json()}, {"profiles", std::move(profile_rows)}}));
        }
        looks.push_back(object({{"grid_size", n}, {"rf_solves_so_far_for_path", solver.calls - calls_before},
                                {"all_firs_grid_refined", all_refined}, {"ports", std::move(ports)}}));
        if (all_refined) break;
        previous = std::move(next);
    }
    all_admitted &= both_admitted;
    return object({{"path", path.name}, {"status", both_admitted ? "admitted" : any_admitted ? "partial" : "unresolved"},
                   {"rf_solves", solver.calls - calls_before}, {"looks", std::move(looks)}});
}

// Each q is exactly representable and has one integer cache key across all
// looks. At a given look the odd q indices are excluded from the knot set.
double profile_q(std::size_t index) {
    return static_cast<double>(index) / max_profile_q_index;
}

struct MeshResponses {
    HfFullwaveResponse coarse;
    HfFullwaveResponse fine;
};

using ProfileResponses = std::array<MeshResponses, profile_rf_offsets.size()>;

class ProfileRefinementCache {
public:
    ProfileRefinementCache() : samples_(max_profile_q_index + 1) {}

    void prepare(const ProfilePath& path, std::size_t stride, Solver& solver) {
        for (std::size_t index = 0; index <= max_profile_q_index; index += stride) {
            if (samples_[index]) continue;
            ProfileResponses samples;
            const auto profile = profile_at(path, profile_q(index));
            for (std::size_t f = 0; f < profile_rf_offsets.size(); ++f) {
                const double offset = profile_rf_offsets[f];
                samples[f] = {solver.get(offset, profile, mesh_check_step_m), solver.get(offset, profile, fine_step_m)};
            }
            samples_[index] = samples;
            ++profiles_solved;
        }
    }

    const MeshResponses& at(std::size_t index, std::size_t f) const {
        return samples_[index].value()[f];
    }

    std::size_t profiles_solved = 0;

private:
    std::vector<std::optional<ProfileResponses>> samples_;
};

struct ProfileError {
    double max_error = -1.;
    double q = 0.;
    double offset_hz = 0.;

    void observe(double error, std::size_t q_index, double offset) {
        if (error > max_error) {
            max_error = error;
            q = profile_q(q_index);
            offset_hz = offset;
        }
    }

    bool pass() const { return max_error >= 0. && max_error <= complex_tolerance; }
    Json json() const {
        return object({{"max_absolute_complex_error", max_error}, {"q", q}, {"rf_offset_hz", offset_hz}, {"pass", pass()}});
    }
};

struct ProfilePortChecks {
    ProfileError fine_interpolation;
    ProfileError coarse_interpolation;
    ProfileError knot_mesh;
    ProfileError midpoint_mesh;

    bool pass() const { return fine_interpolation.pass() && knot_mesh.pass() && midpoint_mesh.pass(); }
};

Json profile_refinement_path(const ProfilePath& path, ProfileRefinementCache& cache, std::size_t intervals,
                             Solver& solver, bool& all_pass) {
    const auto stride = max_profile_q_index / (2 * intervals);
    const auto calls_before = solver.calls;
    cache.prepare(path, stride, solver);

    std::array<ProfilePortChecks, port_labels.size()> checks{};
    for (std::size_t i = 0; i <= 2 * intervals; ++i) {
        const bool midpoint = (i % 2) != 0;
        const auto index = i * stride;
        for (std::size_t f = 0; f < profile_rf_offsets.size(); ++f) {
            const auto& samples = cache.at(index, f);
            const double offset = profile_rf_offsets[f];
            for (std::size_t p = 0; p < port_labels.size(); ++p) {
                auto& row = checks[p];
                auto& mesh_error = midpoint ? row.midpoint_mesh : row.knot_mesh;
                mesh_error.observe(std::abs(port(samples.fine, p) - port(samples.coarse, p)), index, offset);
                if (midpoint) {
                    const auto& lower = cache.at(index - stride, f);
                    const auto& upper = cache.at(index + stride, f);
                    const auto fine = .5 * (port(lower.fine, p) + port(upper.fine, p));
                    const auto coarse = .5 * (port(lower.coarse, p) + port(upper.coarse, p));
                    row.fine_interpolation.observe(std::abs(fine - port(samples.fine, p)), index, offset);
                    row.coarse_interpolation.observe(std::abs(coarse - port(samples.coarse, p)), index, offset);
                }
            }
        }
    }

    bool path_pass = true;
    Json::Array ports;
    for (std::size_t p = 0; p < port_labels.size(); ++p) {
        const auto& row = checks[p];
        path_pass &= row.pass();
        ports.push_back(object({{"port", port_labels[p]}, {"pass", row.pass()},
            {"midpoint_interpolation_5m", row.fine_interpolation.json()},
            {"midpoint_interpolation_10m_diagnostic", row.coarse_interpolation.json()},
            {"knot_mesh_10m_to_5m", row.knot_mesh.json()}, {"midpoint_mesh_10m_to_5m", row.midpoint_mesh.json()}}));
    }
    all_pass &= path_pass;
    return object({{"path", path.name}, {"pass", path_pass}, {"checked_knot_profiles", intervals + 1},
                   {"checked_independent_midpoint_profiles", intervals}, {"unique_profiles_solved", cache.profiles_solved},
                   {"new_rf_solves", solver.calls - calls_before}, {"ports", std::move(ports)}});
}

int run_profile_refinement(Solver& solver, std::chrono::steady_clock::time_point start) {
    std::array<ProfileRefinementCache, path_count> caches;
    Json::Array looks, intervals_json, offsets_json, path_inventory;
    for (const auto n : profile_interval_counts) intervals_json.push_back(n);
    for (const auto f : profile_rf_offsets) offsets_json.push_back(f);
    for (const auto& path : paths) {
        path_inventory.push_back(object({{"path", path.name}, {"f_peak_plasma_over_center_rf_at_q0", path.initial_ratio},
                                         {"f_peak_plasma_over_center_rf_at_q1", path.final_ratio}}));
    }
    bool all_pass = false;
    Json first_passing_interval_count;
    for (const auto intervals : profile_interval_counts) {
        all_pass = true;
        Json::Array path_rows;
        for (std::size_t p = 0; p < paths.size(); ++p)
            path_rows.push_back(profile_refinement_path(paths[p], caches[p], intervals, solver, all_pass));
        looks.push_back(object({{"intervals", intervals}, {"cumulative_rf_solves", solver.calls},
                                {"all_paths_and_ports_pass", all_pass}, {"paths", std::move(path_rows)}}));
        if (all_pass) {
            first_passing_interval_count = intervals;
            break;
        }
    }

    using G = HfVerticalColumn;
    const auto report = object({
        {"schema_version", 1}, {"experiment", "frozen-vertical-plasma-profile-refinement"},
        {"scope", "scalar B=0 normal incidence, five discrete RF offsets and one midpoint per profile interval"},
        {"source_identity", source_identity()}, {"rf_hz", rf_hz}, {"rf_offsets_hz", std::move(offsets_json)},
        {"occupied_half_band_hz", occupied_hz}, {"reference_ports_m", Json::Array{0., G::top_m}},
        {"f_center_m", G::f_center_m}, {"f_half_width_m", G::f_half_width_m}, {"f_collision_per_second", f_collision},
        {"d_center_m", G::d_center_m}, {"d_half_width_m", G::d_half_width_m},
        {"d_center_collision_per_second", G::d_center_collision_per_second},
        {"d_collision_scale_height_m", G::d_collision_scale_height_m},
        {"density_shape", "cos(pi * (z-center) / (2 * half_width))^2 inside each support"},
        {"d_peak_electron_density_at_q0_per_m3", d_density_initial},
        {"d_peak_electron_density_at_q1_per_m3", d_density_final}, {"path_inventory", std::move(path_inventory)},
        {"parameter_path", "q in [0,1]; linearly vary peak F plasma frequency and peak D electron density"},
        {"interpolation", "linear complex interpolation in q of the original untapered static solver response, exp(-i omega t)"},
        {"midpoints", "static solutions at q=(j+0.5)/N are excluded from interpolation at that look and cached for finer looks"},
        {"spatial_mesh_m", Json::Array{mesh_check_step_m, fine_step_m}},
        {"declared_interval_counts", std::move(intervals_json)}, {"q_cache_denominator", max_profile_q_index},
        {"engineering_max_absolute_complex_error", complex_tolerance},
        {"stop_rule", "stop at the first look where every port and path passes 5 m midpoint interpolation and 10 m to 5 m mesh at every knot and midpoint across all five offsets; otherwise withhold at 256 intervals"},
        {"rf_solve_cap", profile_solve_cap}, {"declared_max_rf_solves", profile_max_solves}, {"actual_rf_solves", solver.calls},
        {"first_passing_interval_count", first_passing_interval_count}, {"all_paths_and_ports_pass_at_final_look", all_pass},
        {"full_band_qualified", false}, {"causal_operator_qualified", false}, {"slowness_qualified", false},
        {"communications_qualified", false}, {"receiver_executed", false}, {"evolving_plasma_solved", false},
        {"wall_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()},
        {"looks", std::move(looks)}});
    write_json(std::cout, report);
    std::cout << '\n';
    return all_pass ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: high_fidelity_fullwave_kernel_probe [--profile-refinement]\n"
                     "Evaluate the bounded 3.4 MHz frozen-profile FIR feasibility inventory and emit JSON.\n"
                     "With --profile-refinement, measure static complex interpolation over nested profile grids.\n";
        return 0;
    }
    const bool profile_refinement = argc == 2 && std::string_view(argv[1]) == "--profile-refinement";
    if (argc != 1 && !profile_refinement) {
        std::cerr << "The full-wave kernel probe accepts only --help or --profile-refinement.\n";
        return 2;
    }
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto start = std::chrono::steady_clock::now();
    Solver solver(profile_refinement ? profile_solve_cap : solve_cap);
    try {
        if (profile_refinement) return run_profile_refinement(solver, start);
        bool all_admitted = true;
        Json::Array rows, offgrid;
        for (const double f : holdout_offsets()) offgrid.push_back(f);
        for (const auto& path : paths) rows.push_back(path_row(path, solver, all_admitted));
        using G = HfVerticalColumn;
        const auto report = object({
            {"schema_version", 1}, {"experiment", "frozen-vertical-plasma-to-causal-fir"},
            {"scope", "scalar B=0 normal incidence; finite-band frozen profiles and two-endpoint output-time interpolation"},
            {"source_identity", source_identity()},
            {"rf_hz", rf_hz}, {"complex_sample_rate_hz", fs_hz}, {"occupied_half_band_hz", occupied_hz},
            {"reference_ports_m", Json::Array{0., G::top_m}}, {"f_center_m", G::f_center_m},
            {"f_half_width_m", G::f_half_width_m}, {"f_collision_per_second", f_collision},
            {"d_center_m", G::d_center_m}, {"d_half_width_m", G::d_half_width_m},
            {"d_center_collision_per_second", G::d_center_collision_per_second},
            {"d_collision_scale_height_m", G::d_collision_scale_height_m},
            {"density_shape", "cos(pi * (z-center) / (2 * half_width))^2 inside each support"},
            {"phase_convention", "conjugate the exp(-i omega t) static response at each physical RF; keep carrier phase"},
            {"processing", "remove ceil(vacuum lower bound) envelope delay before direct inverse DFT; taper only the guard; shift by an additional modelling delay; restore the bulk lag; no renormalization"},
            {"added_modelling_delay_samples", model_delay}, {"added_modelling_delay_seconds", model_delay / fs_hz},
            {"retained_samples_after_bulk_plus_modelling_delay", late_samples},
            {"guard_taper", "complementary quintic smoothstep: one at occupied edge, zero at Nyquist"},
            {"prearrival_interpretation", "raw two-sided finite-band energy; a taper spreads the impulse in time and is not physical acausality"},
            {"discarded_energy_denominator", "sum of the raw finite-band period energy; fractions are zero for a zero-energy period"},
            {"profile_interpolation", "smoothstep of output time over one input history; midpoint target solves arithmetic F peak frequency and D peak density independently"},
            {"spatial_mesh_m", Json::Array{mesh_check_step_m, fine_step_m}}, {"mesh_grid_size", mesh_grid_size},
            {"frequency_grid_sizes", Json::Array{grid_sizes[0], grid_sizes[1], grid_sizes[2]}},
            {"frequency_grid", "signed bins [-N/2,N/2) with spacing sample_rate/N; fine solves cached across dyadic sizes"},
            {"independent_offgrid_offsets_hz", std::move(offgrid)}, {"profiles_per_path", profiles},
            {"engineering_max_absolute_complex_error", complex_tolerance},
            {"engineering_max_discarded_energy_fraction", discarded_energy_tolerance},
            {"stop_rule", "first consecutive-grid complex convergence for all frozen FIRs in the path, otherwise stop at 1024; other failed gates remain unresolved"},
            {"rf_solve_cap", solve_cap}, {"declared_max_rf_solves", declared_max_solves}, {"actual_rf_solves", solver.calls},
            {"all_ports_admitted_at_final_look", all_admitted}, {"communications_qualified", false},
            {"receiver_executed", false}, {"evolving_plasma_solved", false},
            {"wall_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()},
            {"paths", std::move(rows)}});
        write_json(std::cout, report);
        std::cout << '\n';
        return all_admitted ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Full-wave kernel probe aborted after " << solver.calls << " RF solves: " << error.what() << '\n';
        return 2;
    }
}
