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
constexpr std::array<std::size_t, 3> phase_factored_interval_counts{16, 32, 64};
constexpr std::size_t phase_cells = 400, phase_check_cells = 800, phase_holdouts = 33;
constexpr double phase_quadrature_tolerance = 1e-6;
constexpr double phase_endpoint_tolerance = 1e-12;

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
constexpr std::size_t phase_factored_max_solves = path_count * (2 * phase_factored_interval_counts.back() + 1) *
    profile_rf_offsets.size() * 2;
static_assert(declared_max_solves <= solve_cap);
static_assert(profile_max_solves <= profile_solve_cap);
static_assert(std::ranges::all_of(profile_interval_counts, [](auto n) { return max_profile_q_index % (2 * n) == 0; }));
static_assert(std::ranges::all_of(phase_factored_interval_counts,
    [](auto n) { return max_profile_q_index % (2 * n) == 0; }));
constexpr std::array profile_labels{"start", "independent_midpoint", "end"};
constexpr std::array port_labels{"reflection", "transmission"};
constexpr std::array phase_path_traversals{2., 1.};
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

struct PhaseCheck {
    double max_change_rad = -1.;
    double worst_q = 0.;

    void observe(double q, double change_rad) {
        if (change_rad > max_change_rad) {
            max_change_rad = change_rad;
            worst_q = q;
        }
    }

    bool pass() const { return max_change_rad >= 0. && max_change_rad <= phase_quadrature_tolerance; }
    Json json() const {
        return object({{"max_400_to_800_cell_phase_change_rad", max_change_rad}, {"worst_q", worst_q}, {"pass", pass()}});
    }
};

double d_phase(double q, std::size_t cells) {
    return hf_fullwave_d_carrier_phase_radians(rf_hz, profile_at(paths.front(), q).d_peak_electron_density_per_m3, cells);
}

// Both frozen paths have exactly the same D profile coordinate. Evaluate it
// once per q across the two paths and retain full analytic phase winding.
class DCarrierPhaseCache {
public:
    DCarrierPhaseCache() : initial_phase_(d_phase(0., phase_cells)), initial_check_phase_(d_phase(0., phase_check_cells)) {
        values_.front() = 0.;
        grid_check_.observe(0., 0.);
        const double shift = std::sqrt(2.) / 3.;
        for (std::size_t i = 0; i < phase_holdouts; ++i) {
            const double q = (i + shift) / phase_holdouts;
            const double nominal = d_phase(q, phase_cells) - initial_phase_;
            offgrid_check_.observe(q, std::abs(d_phase(q, phase_check_cells) - initial_check_phase_ - nominal));
        }
    }

    double get(std::size_t index) {
        auto& value = values_[index];
        if (!value) {
            const double q = profile_q(index);
            value = d_phase(q, phase_cells) - initial_phase_;
            grid_check_.observe(q, std::abs(d_phase(q, phase_check_cells) - initial_check_phase_ - *value));
        }
        return *value;
    }

    bool pass() const { return grid_check_.pass() && offgrid_check_.pass(); }
    Json json() const {
        return object({{"shared_q_grid", grid_check_.json()}, {"independent_offgrid_q", offgrid_check_.json()},
                       {"pass", pass()}});
    }

private:
    double initial_phase_;
    double initial_check_phase_;
    PhaseCheck grid_check_;
    PhaseCheck offgrid_check_;
    std::array<std::optional<double>, max_profile_q_index + 1> values_{};
};

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

    bool pass(double tolerance = complex_tolerance) const { return max_error >= 0. && max_error <= tolerance; }
    Json json(double tolerance = complex_tolerance) const {
        return object({{"max_absolute_complex_error", max_error}, {"q", q}, {"rf_offset_hz", offset_hz},
                       {"pass", pass(tolerance)}});
    }
};

struct ProfilePortChecks {
    ProfileError fine_interpolation;
    ProfileError coarse_interpolation;
    ProfileError direct_fine;
    ProfileError direct_coarse;
    ProfileError endpoint_reconstruction;
    ProfileError knot_mesh;
    ProfileError midpoint_mesh;

    bool pass(bool factored) const {
        return fine_interpolation.pass() && knot_mesh.pass() && midpoint_mesh.pass() &&
            (!factored || endpoint_reconstruction.pass(phase_endpoint_tolerance));
    }
};

Json profile_refinement_path(const ProfilePath& path, ProfileRefinementCache& cache, std::size_t intervals,
                             Solver& solver, bool& all_pass, DCarrierPhaseCache* phases) {
    const auto stride = max_profile_q_index / (2 * intervals);
    const auto calls_before = solver.calls;
    cache.prepare(path, stride, solver);

    std::array<ProfilePortChecks, port_labels.size()> checks{};
    for (std::size_t i = 0; i <= 2 * intervals; ++i) {
        const bool midpoint = (i % 2) != 0;
        const auto index = i * stride;
        struct IntervalBlends {
            HfFullwavePhaseBlend lower, middle, upper;
        };
        std::array<IntervalBlends, port_labels.size()> blends{};
        if (phases) {
            // Phase quadrature is checked at the midpoint independently. The
            // prediction uses only endpoint phases and endpoint responses.
            phases->get(index);
            if (midpoint) {
                const double lower = phases->get(index - stride), upper = phases->get(index + stride);
                for (std::size_t p = 0; p < port_labels.size(); ++p) {
                    const double left_phase = phase_path_traversals[p] * lower;
                    const double right_phase = phase_path_traversals[p] * upper;
                    blends[p] = {hf_fullwave_phase_blend(left_phase, right_phase, 0.),
                                 hf_fullwave_phase_blend(left_phase, right_phase, .5),
                                 hf_fullwave_phase_blend(left_phase, right_phase, 1.)};
                }
            }
        }
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
                    const auto lower_fine = port(lower.fine, p), upper_fine = port(upper.fine, p);
                    const auto lower_coarse = port(lower.coarse, p), upper_coarse = port(upper.coarse, p);
                    auto fine = .5 * (lower_fine + upper_fine);
                    auto coarse = .5 * (lower_coarse + upper_coarse);
                    if (phases) {
                        row.direct_fine.observe(std::abs(fine - port(samples.fine, p)), index, offset);
                        row.direct_coarse.observe(std::abs(coarse - port(samples.coarse, p)), index, offset);
                        fine = blends[p].middle.apply(lower_fine, upper_fine);
                        coarse = blends[p].middle.apply(lower_coarse, upper_coarse);
                        const auto restored_lower = blends[p].lower.apply(lower_fine, upper_fine);
                        const auto restored_upper = blends[p].upper.apply(lower_fine, upper_fine);
                        row.endpoint_reconstruction.observe(std::abs(restored_lower - lower_fine), index - stride, offset);
                        row.endpoint_reconstruction.observe(std::abs(restored_upper - upper_fine), index + stride, offset);
                    }
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
        const bool passes = row.pass(phases != nullptr);
        path_pass &= passes;
        auto receipt = object({{"port", port_labels[p]}, {"pass", passes},
            {"midpoint_interpolation_5m", row.fine_interpolation.json()},
            {"midpoint_interpolation_10m_diagnostic", row.coarse_interpolation.json()},
            {"knot_mesh_10m_to_5m", row.knot_mesh.json()}, {"midpoint_mesh_10m_to_5m", row.midpoint_mesh.json()}});
        if (phases) {
            receipt["direct_complex_control_5m"] = row.direct_fine.json();
            receipt["direct_complex_control_10m_diagnostic"] = row.direct_coarse.json();
            receipt["endpoint_reconstruction_5m"] = row.endpoint_reconstruction.json(phase_endpoint_tolerance);
        }
        ports.push_back(std::move(receipt));
    }
    all_pass &= path_pass;
    return object({{"path", path.name}, {"pass", path_pass}, {"checked_knot_profiles", intervals + 1},
                   {"checked_independent_midpoint_profiles", intervals}, {"unique_profiles_solved", cache.profiles_solved},
                   {"new_rf_solves", solver.calls - calls_before}, {"ports", std::move(ports)}});
}

int run_profile_refinement(Solver& solver, std::chrono::steady_clock::time_point start, bool phase_factored) {
    std::array<ProfileRefinementCache, path_count> caches;
    std::optional<DCarrierPhaseCache> phases;
    if (phase_factored) phases.emplace();
    const auto interval_counts = phase_factored ? std::span<const std::size_t>{phase_factored_interval_counts} :
        std::span<const std::size_t>{profile_interval_counts};
    Json::Array looks, intervals_json, offsets_json, path_inventory;
    for (const auto n : interval_counts) intervals_json.push_back(n);
    for (const auto f : profile_rf_offsets) offsets_json.push_back(f);
    for (const auto& path : paths) {
        path_inventory.push_back(object({{"path", path.name}, {"f_peak_plasma_over_center_rf_at_q0", path.initial_ratio},
                                         {"f_peak_plasma_over_center_rf_at_q1", path.final_ratio}}));
    }
    bool all_pass = false;
    Json first_passing_interval_count;
    for (const auto intervals : interval_counts) {
        all_pass = true;
        Json::Array path_rows;
        for (std::size_t p = 0; p < paths.size(); ++p)
            path_rows.push_back(profile_refinement_path(paths[p], caches[p], intervals, solver, all_pass,
                                                       phases ? &*phases : nullptr));
        if (phases) all_pass &= phases->pass();
        auto look = object({{"intervals", intervals}, {"cumulative_rf_solves", solver.calls},
                            {"all_paths_and_ports_pass", all_pass}, {"paths", std::move(path_rows)}});
        if (phases) look["carrier_phase_quadrature"] = phases->json();
        looks.push_back(std::move(look));
        if (all_pass) {
            first_passing_interval_count = intervals;
            break;
        }
    }

    using G = HfVerticalColumn;
    auto report = object({
        {"schema_version", 1}, {"experiment", phase_factored ? "frozen-vertical-plasma-phase-factored-profile-refinement" :
                                                                "frozen-vertical-plasma-profile-refinement"},
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
        {"interpolation", phase_factored ? "linear interpolation of phase-factored complex responses with carrier-phase restoration" :
                                 "linear complex interpolation in q of the original untapered static solver response, exp(-i omega t)"},
        {"midpoints", "static solutions at q=(j+0.5)/N are excluded from interpolation at that look and cached for finer looks"},
        {"spatial_mesh_m", Json::Array{mesh_check_step_m, fine_step_m}},
        {"declared_interval_counts", std::move(intervals_json)}, {"q_cache_denominator", max_profile_q_index},
        {"engineering_max_absolute_complex_error", complex_tolerance},
        {"stop_rule", phase_factored ? "stop at the first look where every port and path passes 5 m midpoint interpolation, endpoint reconstruction, phase quadrature, and 10 m to 5 m mesh at every knot and midpoint across all five offsets; otherwise withhold at 64 intervals" :
                         "stop at the first look where every port and path passes 5 m midpoint interpolation and 10 m to 5 m mesh at every knot and midpoint across all five offsets; otherwise withhold at 256 intervals"},
        {"rf_solve_cap", phase_factored ? phase_factored_max_solves : profile_solve_cap},
        {"declared_max_rf_solves", phase_factored ? phase_factored_max_solves : profile_max_solves},
        {"actual_rf_solves", solver.calls},
        {"first_passing_interval_count", first_passing_interval_count}, {"all_paths_and_ports_pass_at_final_look", all_pass},
        {"full_band_qualified", false}, {"causal_operator_qualified", false}, {"slowness_qualified", false},
        {"communications_qualified", false}, {"receiver_executed", false}, {"evolving_plasma_solved", false},
        {"wall_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()},
        {"looks", std::move(looks)}});
    if (phase_factored)
        report["carrier_phase_factoring"] = object({
            {"definition", "relative to q=0: k0 times the integral of Re(n-1) across the D layer at RF center, in the solver's exp(-i omega t) sign"},
            {"reflection_traversals", phase_path_traversals[0]}, {"transmission_traversals", phase_path_traversals[1]},
            {"algorithm", "remove phase at each knot, interpolate complex residuals and analytic phase linearly between knots, and restore the interpolated phase at q; retain the static solver amplitudes at the knots"},
            {"midpoint_cells", phase_cells}, {"quadrature_check_cells", phase_check_cells},
            {"quadrature_offgrid_q_count", phase_holdouts}, {"quadrature_offgrid_q_rule", "(j+sqrt(2)/3)/33, j=0..32"},
            {"max_one_way_quadrature_change_rad", phase_quadrature_tolerance},
            {"max_endpoint_reconstruction_error", phase_endpoint_tolerance},
            {"phase_quadrature", phases->json()}});
    write_json(std::cout, report);
    std::cout << '\n';
    return all_pass ? 0 : 1;
}

using KernelTable = HfFullwaveKernelTable;
constexpr std::array kernel_ports{HfFullwavePort::reflection, HfFullwavePort::transmission};

Json table_error_json(const HfFullwaveBandError& result) {
    return result.maximum < 0. ? Json{} : object({{"absolute_complex_error", result.maximum},
        {"q", result.profile_q}, {"rf_offset_hz", result.offset_hz}, {"pass", result.maximum <= KernelTable::complex_tolerance}});
}

Json table_waveform_check(std::shared_ptr<const KernelTable> table, HfFullwavePort port, double duration, bool& all_pass) {
    constexpr std::size_t transition_first = 0, input_count = 2048;
    const auto start_sample = static_cast<std::size_t>(duration * KernelTable::sample_rate_hz / 2.) - input_count / 2;
    const auto first_lag = table->first_lag_samples(port), last_lag = table->last_lag_samples(port);
    HfFullwaveProfileStream whole(table, port, transition_first, duration, start_sample);
    HfFullwaveProfileStream split(table, port, transition_first, duration, start_sample);
    std::array<Complex, input_count> input{};
    double peak_input = 0.;
    for (std::size_t n = 0; n < input.size(); ++n) {
        // A known occupied-band complex mixture followed by a finite abrupt
        // source stop. Compare the whole physical finite-FIR tail as well.
        input[n] = std::polar(.54, .3 + two_pi * n * 171.53 / KernelTable::sample_rate_hz) +
            std::polar(.38, -.6 - two_pi * n * 433.21 / KernelTable::sample_rate_hz);
        peak_input = std::max(peak_input, std::abs(input[n]));
    }
    std::vector<Complex> output(input.size() + last_lag), chunked(output.size());
    whole.process(input, std::span{output}.first(input.size()));
    bool all_drained = whole.drain(std::span{output}.subspan(input.size())) == last_lag;
    const auto chunks = std::span{chunked};
    for (std::size_t first = 0; first < input.size();) {
        const auto n = std::min<std::size_t>(127, input.size() - first);
        split.process(std::span{input}.subspan(first, n), chunks.subspan(first, n));
        first += n;
    }
    const auto drained = split.drain(chunks.subspan(input.size(), 17));
    all_drained &= drained == 17 && split.drain(chunks.subspan(input.size() + drained)) == last_lag - drained &&
        split.drain(chunks.first(1)) == 0;
    const double phase_scale = std::numbers::pi / (duration * KernelTable::sample_rate_hz);
    const auto q_at = [&](double absolute) { return std::pow(std::sin(.5 * std::clamp(phase_scale * absolute, 0., std::numbers::pi)), 2); };
    double output_difference = 0., retarded_difference = 0., tail_energy = 0.;
    for (std::size_t n = 0; n < output.size(); ++n) {
        const double clock = static_cast<double>(start_sample + n);
        const double q = q_at(clock - KernelTable::modelling_delay_samples);
        Complex common_time{}, retarded{};
        for (std::size_t lag = first_lag; lag <= last_lag; ++lag) {
            if (lag > n || n - lag >= input.size()) continue;
            const auto x = input[n - lag];
            common_time += table->coefficient_at_q(port, lag, q) * x;
            retarded += table->coefficient_at_q(port, lag, q_at(clock - lag)) * x;
        }
        output_difference = std::max(output_difference, std::abs(output[n] - common_time));
        retarded_difference = std::max(retarded_difference, std::abs(output[n] - retarded));
        if (n >= input.size()) tail_energy += std::norm(output[n]);
    }
    const double limit = peak_input * table->retarded_peak_error_bound(port, duration);
    const bool chunks_identical = output == chunked;
    const bool earliest_causal = std::ranges::all_of(std::span{output}.first(first_lag), [](const Complex x) { return x == Complex{}; });
    const bool pass = all_drained && chunks_identical && earliest_causal && tail_energy > 0. &&
        output_difference <= 2e-12 && retarded_difference <= limit + 2e-12;
    all_pass &= pass;
    return object({{"input_samples", input.size()}, {"first_sample", start_sample}, {"tail_samples", last_lag},
        {"tail_energy", tail_energy}, {"no_prearrival_output", earliest_causal}, {"all_tail_samples_drained", all_drained},
        {"chunk_identical", chunks_identical}, {"direct_output_convolution_max_abs_error", output_difference},
        {"retarded_tap_convolution_max_abs_difference", retarded_difference}, {"retarded_tap_difference_bound", limit}, {"pass", pass}});
}

int run_kernel_table(std::chrono::steady_clock::time_point start) {
    const auto built = build_hf_fullwave_kernel_table();
    const auto& checks = built.checks;
    Json::Array durations, offsets, profile_witnesses, duration_rows, ports;
    for (auto duration : KernelTable::transition_durations_seconds) durations.push_back(duration);
    for (double f : hf_fullwave_table_holdout_offsets_hz()) offsets.push_back(f);
    for (const double q : {0., .25, .5, .75, 1.}) profile_witnesses.push_back(q);
    double selected_duration = 0.;
    bool fast_control_rejected = false;
    bool waveform_pass = false;
    if (built.table) {
        constexpr std::array<double, 4> test_durations{120., 240., 480., 1.};
        for (const auto duration : test_durations) {
            bool both_ports_pass = true;
            Json::Array rate_rows;
            for (std::size_t p = 0; p < kernel_ports.size(); ++p) {
                const auto port = kernel_ports[p];
                const double difference = built.table->retarded_peak_error_bound(port, duration);
                const double carrier_phase = built.table->carrier_flight_phase_bound(port, duration);
                const double flight_seconds = std::max(std::abs(static_cast<double>(built.table->first_lag_samples(port)) -
                    KernelTable::modelling_delay_samples), static_cast<double>(built.table->last_lag_samples(port) -
                    KernelTable::modelling_delay_samples)) / KernelTable::sample_rate_hz;
                const double observed_phase = checks.worst_profile_phase_rate_rad_per_q[p] * std::numbers::pi /
                    (2. * duration) * flight_seconds;
                const bool pass = difference <= KernelTable::rate_peak_tolerance;
                both_ports_pass &= pass;
                rate_rows.push_back(object({{"port", port_labels[p]}, {"retarded_output_peak_error_bound", difference},
                    {"max_physical_relative_retained_lag_seconds", flight_seconds}, {"carrier_coordinate_flight_phase_bound_rad", carrier_phase},
                    {"carrier_phase_below_diagnostic_reference", carrier_phase <= KernelTable::phase_flight_reference_radians},
                    {"sampled_solver_flight_phase_proxy_rad", checks.observed_phase_pairs[p] ? Json(observed_phase) : Json{}}, {"pass", pass}}));
            }
            if (duration == 1.) fast_control_rejected = !both_ports_pass;
            else if (both_ports_pass && selected_duration == 0.) selected_duration = duration;
            duration_rows.push_back(object({{"duration_seconds", duration}, {"fast_control", duration == 1.},
                                            {"both_ports_pass_absolute_rate", both_ports_pass}, {"ports", std::move(rate_rows)}}));
        }
    }
    waveform_pass = selected_duration > 0.;
    for (std::size_t p = 0; p < kernel_ports.size(); ++p) {
        const auto port = kernel_ports[p];
        auto waveform = selected_duration > 0. ? table_waveform_check(built.table, port, selected_duration, waveform_pass) : Json{};
        const auto first_lag = KernelTable::planned_first_lag_samples(port);
        ports.push_back(object({{"port", port_labels[p]}, {"first_lag_samples", first_lag},
            {"last_lag_samples", first_lag + KernelTable::taps_per_port - 1}, {"knot_grid", table_error_json(checks.knot_grid[p])},
            {"knot_offgrid", table_error_json(checks.knot_offgrid[p])}, {"profile_holdout", table_error_json(checks.profile_holdout[p])},
            {"witness_frequency_refinement", table_error_json(checks.witness_frequency_refinement[p])},
            {"witness_spatial_mesh", table_error_json(checks.witness_mesh[p])},
            {"max_discarded_energy_fraction", checks.maximum_discarded_energy[p]},
            {"max_raw_finite_band_prearrival_energy_fraction", checks.maximum_prearrival_energy[p]},
            {"d1_peak_error_seconds_per_q", checks.qualified ? Json(checks.peak_error_seconds_per_q[p]) : Json{}},
            {"phase_secants_resolved_count", checks.observed_phase_pairs[p]}, {"phase_secants_weak_count", checks.weak_phase_pairs[p]},
            {"phase_secants_ambiguous_count", checks.ambiguous_phase_pairs[p]}, {"largest_conditioned_phase_step_rad", checks.largest_observed_phase_step_rad[p]},
            {"worst_sampled_phase_rate_rad_per_q", checks.observed_phase_pairs[p] ? Json(checks.worst_profile_phase_rate_rad_per_q[p]) : Json{}},
            {"waveform_check", std::move(waveform)}}));
    }
    const bool pass = checks.qualified && selected_duration > 0. && waveform_pass && fast_control_rejected;
    const auto report = object({
        {"schema_version", 1}, {"experiment", "frozen-vertical-plasma-full-band-kernel-table"}, {"source_identity", source_identity()},
        {"scope", "constructed full coupled D/F trajectory and scalar B=0 normal incidence; finite-band causal surrogate"},
        {"rf_hz", KernelTable::carrier_hz}, {"sample_rate_hz", KernelTable::sample_rate_hz}, {"occupied_half_band_hz", KernelTable::occupied_half_band_hz},
        {"reference_ports_m", Json::Array{0., HfVerticalColumn::top_m}}, {"path", paths.front().name},
        {"f_peak_over_rf_endpoints", Json::Array{paths.front().initial_ratio, paths.front().final_ratio}},
        {"d_peak_electron_density_endpoints_per_m3", Json::Array{d_density_initial, d_density_final}},
        {"f_collision_per_second", f_collision}, {"profile_intervals", KernelTable::intervals}, {"knot_grid_bins", KernelTable::frequency_bins},
        {"frequency_holdout_offsets_hz", std::move(offsets)}, {"witness_q", std::move(profile_witnesses)},
        {"profile_holdout_rule", "q=(j+sqrt(2)/5)/64 or (j+sqrt(3)/2)/64, j=0..63"},
        {"witness_inventory", "5 m N=512 with 256 cached even bins; 10 m N=256 plus 65 frequency holdouts"},
        {"rf_solve_cap", KernelTable::maximum_rf_solves}, {"actual_rf_solves", checks.rf_solves},
        {"knot_profiles_checked", checks.knot_profiles_checked}, {"holdout_profiles_checked", checks.holdout_profiles_checked},
        {"witnesses_qualified", checks.witnesses_qualified}, {"table_qualified", checks.qualified},
        {"table_resident_bytes", built.table ? Json(built.table->workspace_bytes()) : Json{}},
        {"preparation_workspace_bound_bytes", KernelTable::preparation_workspace_bound_bytes},
        {"max_absolute_complex_error", KernelTable::complex_tolerance}, {"max_discarded_energy_fraction", KernelTable::discarded_energy_tolerance},
        {"frozen_flux_excess_above_one", checks.maximum_frozen_flux_excess}, {"frozen_flux_excess_allowance", hf_fullwave_frozen_flux_tolerance()},
        {"taps_per_port", KernelTable::taps_per_port}, {"modeling_delay_samples", KernelTable::modelling_delay_samples},
        {"retarded_comparison", "output-time q((n-32)/Fs) versus q((n-lag)/Fs) separately for each tap; peak difference divided by peak input"},
        {"clock", "absolute physical output clock before explicit numerical delay; clamped half-cosine profile with constant endpoints"},
        {"absolute_rate_peak_tolerance", KernelTable::rate_peak_tolerance}, {"phase_flight_diagnostic_reference_rad", KernelTable::phase_flight_reference_radians},
        {"conditioned_solver_phase_min_amplitude", KernelTable::phase_observation_min_amplitude},
        {"conditioned_solver_phase_secant_rule", "adjacent knot/holdout exact RF solutions; endpoint amplitudes >=0.05 and principal step <pi/2; sampled diagnostic only"},
        {"frozen_duration_candidates_seconds", std::move(durations)}, {"duration_checks", std::move(duration_rows)},
        {"selected_first_passing_duration_seconds", selected_duration > 0. ? Json(selected_duration) : Json{}},
        {"fast_control_rejected_by_absolute_rate", fast_control_rejected}, {"sampled_full_band_and_waveform_gates_pass", pass},
        {"ports", std::move(ports)}, {"receiver_executed", false}, {"communications_qualified", false}, {"evolving_plasma_solved", false},
        {"wall_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()}});
    write_json(std::cout, report);
    std::cout << '\n';
    return pass ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: high_fidelity_fullwave_kernel_probe [--profile-refinement|--phase-factored-profile-refinement|--kernel-table]\n"
                     "Evaluate the bounded 3.4 MHz frozen-profile FIR feasibility inventory and emit JSON.\n"
                     "With --profile-refinement, measure static complex interpolation over nested profile grids.\n"
                     "With --phase-factored-profile-refinement, test D carrier-phase factoring against direct interpolation.\n"
                     "With --kernel-table, qualify the coupled path's finite-band kernel table and causal rendering.\n";
        return 0;
    }
    const bool phase_factored = argc == 2 && std::string_view(argv[1]) == "--phase-factored-profile-refinement";
    const bool profile_refinement = phase_factored || (argc == 2 && std::string_view(argv[1]) == "--profile-refinement");
    const bool kernel_table = argc == 2 && std::string_view(argv[1]) == "--kernel-table";
    if (argc != 1 && !profile_refinement && !kernel_table) {
        std::cerr << "Unknown full-wave kernel probe option; see --help.\n";
        return 2;
    }
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto start = std::chrono::steady_clock::now();
    Solver solver(phase_factored ? phase_factored_max_solves : profile_refinement ? profile_solve_cap : solve_cap);
    try {
        if (kernel_table) return run_kernel_table(start);
        if (profile_refinement) return run_profile_refinement(solver, start, phase_factored);
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
        std::cerr << "Full-wave kernel probe aborted";
        if (!kernel_table) std::cerr << " after " << solver.calls << " RF solves";
        std::cerr << ": " << error.what() << '\n';
        return 2;
    }
}
