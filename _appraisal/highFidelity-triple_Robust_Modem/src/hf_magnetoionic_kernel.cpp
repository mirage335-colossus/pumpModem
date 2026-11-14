#include "appraisal/hf_magnetoionic_kernel.hpp"
#include "appraisal/hf_magnetoionic.hpp"
#include "hf_slab_scattering.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {

using Table = HfMagnetoionicKernelTable;
using Complex = std::complex<double>;
using Field = HfMagnetoionicProfileStream::Field;
using hf_slab_detail::c;
using hf_slab_detail::two_pi;
constexpr double solver_flux_tolerance = 1e-8;
constexpr double reflection_front_m = 2. * (HfVerticalColumn::d_center_m - HfVerticalColumn::d_half_width_m);
constexpr double earliest_arrival = reflection_front_m / c * Table::sample_rate_hz;
constexpr auto refined_bins = 2 * Table::frequency_bins;
constexpr std::size_t holdouts_per_interval = 2;
constexpr std::array null_witness_meshes{Table::production_step_m, 5., 2.5};
constexpr double original_null_witness_offset_hz = -118.7;
static_assert(earliest_arrival > Table::first_lag_samples() - 1 && earliest_arrival <= Table::first_lag_samples());
constexpr std::size_t ordinary_rf_solves(std::size_t intervals) {
    // Three mesh/frequency witness profiles use the odd N256 frequencies and
    // reuse all coincident N128 solves. Only those profiles get coarse-mesh checks.
    return (intervals + 4) * (Table::frequency_bins + Table::frequency_holdouts) +
        intervals * holdouts_per_interval * Table::frequency_holdouts;
}
static_assert(Table::tilted_rf_solve_limit == ordinary_rf_solves(Table::tilted_profile_intervals));
static_assert(Table::parallel_rf_solve_limit == ordinary_rf_solves(Table::parallel_profile_intervals) + null_witness_meshes.size());

double gyrofrequency_hz(HfMagnetoionicPath path) {
    const auto [bx, by, bz] = Table::field_at_path(path);
    return hf_slab_detail::elementary_charge * std::hypot(bx, by, bz) / (two_pi * hf_slab_detail::electron_mass);
}

double cutoff_sign(HfMagnetoionicPath path) {
    switch (path) {
    case HfMagnetoionicPath::tilted_lower: return -1.;
    case HfMagnetoionicPath::parallel_upper: return 1.;
    }
    throw std::invalid_argument("invalid magnetic profile path");
}

void validate_q(double q) {
    if (!std::isfinite(q) || q < 0. || q > 1.) throw std::invalid_argument("invalid magnetic profile fraction");
}

std::size_t validate_output(std::size_t p) {
    if (p >= Field{}.size()) throw std::invalid_argument("invalid magnetic laboratory polarization index");
    return p;
}

double frequency_offset(std::size_t n, std::size_t i) {
    return Table::sample_rate_hz * (static_cast<double>(i) / n - .5);
}

Field reflected_x_launch(const HfMagnetoionicResponse& response) {
    return {response.reflection[0], response.reflection[2]};
}

Complex target(Complex value, double offset) {
    return std::conj(value) * std::polar(hf_fullwave_guard_taper(offset, Table::sample_rate_hz,
        Table::occupied_half_band_hz), -two_pi * offset / Table::sample_rate_hz * Table::modelling_delay_samples);
}

void observe(HfFullwaveBandError& error, double value, double q, double offset) {
    if (!std::isfinite(value)) throw std::runtime_error("non-finite magnetic kernel error");
    if (value > error.maximum) error = {value, q, offset};
}

bool below_tolerance(const std::array<HfFullwaveBandError, 2>& errors) {
    return std::ranges::all_of(errors, [](const auto& e) {
        return e.maximum >= 0. && e.maximum <= Table::complex_tolerance;
    });
}

void observe_scale(HfMagnetoionicScaleChecks& checks, Complex raw, double error, double q, double offset) {
    if (std::abs(offset) > Table::occupied_half_band_hz) return;
    const auto amplitude = std::abs(raw);
    HfMagnetoionicScaleWitness row{-1., amplitude, error, q, offset};
    ++checks.samples;
    if (error >= amplitude) ++checks.absolute_error_at_least_raw;
    if (amplitude == 0.) {
        ++checks.exact_zero_raw;
    } else {
        const auto relative = error / amplitude;
        if (std::isfinite(relative)) row.relative_error = relative;
    }
    if (checks.minimum_raw_amplitude.raw_amplitude < 0. || amplitude < checks.minimum_raw_amplitude.raw_amplitude)
        checks.minimum_raw_amplitude = row;
    if (row.relative_error > checks.maximum_relative_error.relative_error) checks.maximum_relative_error = row;
    if (error > checks.raw_at_maximum_absolute_error.absolute_error) checks.raw_at_maximum_absolute_error = row;
}

std::pair<std::size_t, double> bracket(double q, std::size_t intervals) {
    const auto x = q * intervals;
    const auto lower = std::min(static_cast<std::size_t>(x), intervals - 1);
    return {lower, x - lower};
}

std::array<double, 4> cubic_weights(double u) {
    const double v = 1. - u;
    return {v * v * (1. + 2. * u), u * u * (1. + 2. * v), u * v * v, -u * u * v};
}

Complex cubic_value(const std::array<double, 4>& w, Complex a, Complex b, Complex da, Complex db) {
    return w[0] * a + w[1] * b + w[2] * da + w[3] * db;
}

double clamped_profile(std::size_t sample, std::size_t delayed_transition_first, double duration_samples) {
    if (sample <= delayed_transition_first) return 0.;
    const auto u = static_cast<double>(sample - delayed_transition_first) / duration_samples;
    return u >= 1. ? 1. : .5 * (1. - std::cos(std::numbers::pi * u));
}

} // namespace

std::size_t Table::profile_intervals(HfMagnetoionicPath path) {
    switch (path) {
    case HfMagnetoionicPath::tilted_lower: return tilted_profile_intervals;
    case HfMagnetoionicPath::parallel_upper: return parallel_profile_intervals;
    }
    throw std::invalid_argument("invalid magnetic profile path");
}

std::size_t Table::rf_solve_limit(HfMagnetoionicPath path) {
    switch (path) {
    case HfMagnetoionicPath::tilted_lower: return tilted_rf_solve_limit;
    case HfMagnetoionicPath::parallel_upper: return parallel_rf_solve_limit;
    }
    throw std::invalid_argument("invalid magnetic profile path");
}

std::array<double, 3> Table::field_at_path(HfMagnetoionicPath path) {
    switch (path) {
    case HfMagnetoionicPath::tilted_lower: return magnetic_field_tesla;
    case HfMagnetoionicPath::parallel_upper: return {0., 0., 50e-6};
    }
    throw std::invalid_argument("invalid magnetic profile path");
}

HfVerticalColumn Table::profile_at_q(double q, HfMagnetoionicPath path) {
    validate_q(q);
    const double cutoff_peak = carrier_hz * std::sqrt(1. + cutoff_sign(path) * (gyrofrequency_hz(path) / carrier_hz));
    return {cutoff_peak * (.9999 + .0002 * q), 60., 8e8};
}

double Table::cutoff_offset_hz(double q, HfMagnetoionicPath path) {
    const double fp = profile_at_q(q, path).f_peak_plasma_frequency_hz;
    const double gyro = gyrofrequency_hz(path);
    return .5 * (std::hypot(gyro, 2. * fp) - cutoff_sign(path) * gyro) - carrier_hz;
}

std::array<double, Table::frequency_holdouts> hf_magnetoionic_holdout_offsets_hz(double q, HfMagnetoionicPath path) {
    const double cutoff = Table::cutoff_offset_hz(q, path);
    return {-952.1, -837.3, -653.7, -384., -257.3, -118.7, -47.1,
             47.9, 119.3, 256.7, 384., 654.3, 836.7, 952.9, cutoff - 1., cutoff, cutoff + 1.};
}

double hf_magnetoionic_frozen_reflected_power_tolerance() {
    // Error bounds for the two lab components give the Euclidean error of
    // the vector. The physical reflected norm cannot exceed the full output
    // norm for a unit launch. No field is rescaled to pass this check.
    const double bound = std::sqrt(1. + solver_flux_tolerance) + std::numbers::sqrt2 * Table::complex_tolerance;
    return bound * bound - 1.;
}

HfMagnetoionicKernelBuild build_hf_magnetoionic_kernel_table(std::stop_token stop, HfMagnetoionicPath path) {
    const auto field = Table::field_at_path(path);
    auto table = std::shared_ptr<Table>(new Table(path));
    const auto intervals = table->profile_intervals();
    const auto rf_solve_limit = Table::rf_solve_limit(path);
    const std::array witness_knots{std::size_t{0}, intervals / 2, intervals};
    HfMagnetoionicKernelChecks checks;
    for (auto& knots : checks.knot_polarization) knots.resize(intervals + 1);

    auto solve = [&](double q, double f, double step) {
        if (stop.stop_requested()) throw std::runtime_error("magnetic kernel construction cancelled");
        if (checks.rf_solves >= rf_solve_limit) throw std::logic_error("magnetic kernel exceeded RF-solve budget");
        ++checks.rf_solves;
        const auto raw = solve_hf_magnetoionic_vertical(Table::carrier_hz + f, Table::profile_at_q(q, path), field, step);
        if (!std::isfinite(raw.maximum_output_power) || raw.maximum_output_power > 1. + solver_flux_tolerance ||
            !std::isfinite(raw.minimum_longitudinal_permittivity))
            throw std::runtime_error("unqualified magnetic solver field response");
        checks.maximum_solver_output_power = std::max(checks.maximum_solver_output_power, raw.maximum_output_power);
        checks.minimum_longitudinal_permittivity = std::min(checks.minimum_longitudinal_permittivity, raw.minimum_longitudinal_permittivity);
        const auto reflected = reflected_x_launch(raw);
        if (step == Table::production_step_m && std::abs(f) <= Table::occupied_half_band_hz) {
            const double power = std::norm(reflected[0]) + std::norm(reflected[1]);
            if (power > 0.) {
                for (std::size_t p = 0; p < reflected.size(); ++p) {
                    const double fraction = std::norm(reflected[p]) / power;
                    auto& minimum = checks.minimum_occupied_projection_fraction[p];
                    auto& maximum = checks.maximum_occupied_projection_fraction[p];
                    if (minimum.fraction_of_vector_power < 0. || fraction < minimum.fraction_of_vector_power)
                        minimum = {fraction, power, q, f};
                    if (fraction > maximum.fraction_of_vector_power) maximum = {fraction, power, q, f};
                }
            }
        }
        return reflected;
    };
    const auto observe_flux = [&](Field x) {
        checks.maximum_frozen_reflected_power_excess = std::max(checks.maximum_frozen_reflected_power_excess,
                                                              std::norm(x[0]) + std::norm(x[1]) - 1.);
    };
    const auto observe_carrier = [&](Field x) {
        const double p0 = std::norm(x[0]), p1 = std::norm(x[1]);
        const std::array powers{p0, p1, p0 + p1};
        for (std::size_t i = 0; i < powers.size(); ++i) {
            checks.minimum_carrier_power[i] = std::min(checks.minimum_carrier_power[i], powers[i]);
            checks.maximum_carrier_power[i] = std::max(checks.maximum_carrier_power[i], powers[i]);
        }
    };
    const auto design = [&](std::span<const Field> grid) {
        std::array<HfFullwaveFir, 2> out;
        std::array<Complex, refined_bins> spectrum;
        for (std::size_t p = 0; p < out.size(); ++p) {
            for (std::size_t i = 0; i < grid.size(); ++i) spectrum[i] = grid[i][p];
            auto response = design_hf_fullwave_kernel(std::span{spectrum}.first(grid.size()), {Table::sample_rate_hz,
                Table::occupied_half_band_hz, earliest_arrival, Table::modelling_delay_samples, Table::retained_late_samples});
            if (response.fir.first_tap_sample != Table::first_lag_samples() || response.fir.taps.size() != Table::taps_per_output)
                throw std::logic_error("unexpected magnetic kernel lag support");
            const auto discarded = response.discarded_pre_energy_fraction + response.discarded_tail_energy_fraction;
            checks.maximum_discarded_energy[p] = std::max(checks.maximum_discarded_energy[p], discarded);
            out[p] = std::move(response.fir);
        }
        return out;
    };
    const auto check_frequency = [&](const std::array<HfFullwaveFir, 2>& fir, Field raw, double q, double f,
                                     std::array<HfFullwaveBandError, 2>& errors,
                                     std::array<HfMagnetoionicScaleChecks, 2>* scale = nullptr) {
        Field actual;
        for (std::size_t p = 0; p < actual.size(); ++p) {
            actual[p] = hf_fullwave_fir_response(fir[p], f / Table::sample_rate_hz);
            const auto error = std::abs(actual[p] - target(raw[p], f));
            observe(errors[p], error, q, f);
            if (scale) observe_scale((*scale)[p], raw[p], error, q, f);
        }
        observe_flux(actual);
        return actual;
    };
    const auto knot_checks_pass = [&] {
        return below_tolerance(checks.knot_grid) && below_tolerance(checks.knot_offgrid) &&
            below_tolerance(checks.witness_frequency_refinement) && below_tolerance(checks.witness_mesh) &&
            std::ranges::all_of(checks.maximum_discarded_energy, [](double v) { return v <= Table::discarded_energy_tolerance; }) &&
            checks.maximum_frozen_reflected_power_excess <= hf_magnetoionic_frozen_reflected_power_tolerance();
    };

    std::array<Field, hf_magnetoionic_polarization_offsets_hz.size()> initial_polarization{};
    const auto observe_polarization = [&](std::size_t k, const auto& grid, const auto& offsets, const auto& holdouts) {
        for (std::size_t i = 0; i < hf_magnetoionic_polarization_offsets_hz.size(); ++i) {
            const double f = hf_magnetoionic_polarization_offsets_hz[i];
            Field sample;
            if (f == 0.) {
                sample = grid[grid.size() / 2];
            } else {
                const auto at = std::ranges::find(offsets, f);
                if (at == offsets.end()) throw std::logic_error("missing fixed magnetic polarization witness");
                sample = holdouts[static_cast<std::size_t>(at - offsets.begin())];
            }
            if (k == 0) initial_polarization[i] = sample;
            const auto& initial = initial_polarization[i];
            const double px = std::norm(sample[0]), py = std::norm(sample[1]);
            auto& row = checks.knot_polarization[i][k];
            row.vector_power = px + py;
            if (row.vector_power > 0.) {
                const auto xy = sample[0] * std::conj(sample[1]);
                row.normalized_stokes = {(px - py) / row.vector_power, 2. * xy.real() / row.vector_power,
                    2. * xy.imag() / row.vector_power};
                const auto initial_power = std::norm(initial[0]) + std::norm(initial[1]);
                if (initial_power > 0.) {
                    const auto inner_product = std::conj(initial[0]) * sample[0] + std::conj(initial[1]) * sample[1];
                    row.overlap_power_with_initial = std::min(1., std::norm(inner_product) / (initial_power * row.vector_power));
                }
            }
        }
    };
    const auto qualify_knot = [&](std::size_t k, bool witness) {
        const double q = static_cast<double>(k) / intervals;
        const auto offsets = hf_magnetoionic_holdout_offsets_hz(q, path);
        std::array<Field, Table::frequency_bins> grid;
        std::array<Field, Table::frequency_holdouts> holdouts;
        for (std::size_t i = 0; i < grid.size(); ++i)
            grid[i] = solve(q, frequency_offset(grid.size(), i), Table::production_step_m);
        for (std::size_t i = 0; i < holdouts.size(); ++i) holdouts[i] = solve(q, offsets[i], Table::production_step_m);
        observe_carrier(grid[grid.size() / 2]);
        observe_polarization(k, grid, offsets, holdouts);
        const auto fir = design(grid);
        for (std::size_t p = 0; p < fir.size(); ++p)
            std::ranges::copy(fir[p].taps, table->knots_[k].values[p].begin());
        for (std::size_t i = 0; i < grid.size(); ++i)
            check_frequency(fir, grid[i], q, frequency_offset(grid.size(), i), checks.knot_grid);
        for (std::size_t i = 0; i < holdouts.size(); ++i)
            check_frequency(fir, holdouts[i], q, offsets[i], checks.knot_offgrid, &checks.knot_offgrid_scale);
        ++checks.knot_profiles_checked;
        if (!witness) return;

        std::array<Field, refined_bins> refined_grid;
        for (std::size_t i = 0; i < refined_grid.size(); ++i)
            refined_grid[i] = i % 2 == 0 ? grid[i / 2] : solve(q, frequency_offset(refined_grid.size(), i), Table::production_step_m);
        const auto refined_fir = design(refined_grid);
        const auto check_refined = [&](Field raw, double f) {
            const auto a = check_frequency(fir, raw, q, f, checks.knot_grid);
            const auto b = check_frequency(refined_fir, raw, q, f, checks.knot_grid);
            for (std::size_t p = 0; p < a.size(); ++p)
                observe(checks.witness_frequency_refinement[p], std::abs(a[p] - b[p]), q, f);
        };
        for (std::size_t i = 0; i < refined_grid.size(); ++i)
            check_refined(refined_grid[i], frequency_offset(refined_grid.size(), i));
        for (std::size_t i = 0; i < holdouts.size(); ++i) {
            check_refined(holdouts[i], offsets[i]);
            const auto coarse = solve(q, offsets[i], Table::witness_step_m);
            for (std::size_t p = 0; p < coarse.size(); ++p) {
                const auto error = std::abs(holdouts[i][p] - coarse[p]);
                observe(checks.witness_mesh[p], error, q, offsets[i]);
                observe_scale(checks.witness_mesh_scale[p], holdouts[i][p], error, q, offsets[i]);
            }
        }
    };
    for (const auto k : witness_knots) qualify_knot(k, true);
    checks.witnesses_qualified = knot_checks_pass();
    if (!checks.witnesses_qualified) return {nullptr, checks};
    for (std::size_t k = 0; k <= intervals; ++k)
        if (std::ranges::find(witness_knots, k) == witness_knots.end()) qualify_knot(k, false);
    if (!knot_checks_pass()) return {nullptr, checks};

    // Three-point slopes of the reflected field, separately in each fixed
    // laboratory component. Cubic interpolation requires no phase or mode
    // labels and remains defined at a projected null.
    auto& knots = table->knots_;
    for (std::size_t k = 0; k < knots.size(); ++k) {
        for (std::size_t p = 0; p < Field{}.size(); ++p) {
            for (std::size_t l = 0; l < Table::taps_per_output; ++l) {
                const auto value = [&](std::size_t j) { return knots[j].values[p][l]; };
                const auto end = intervals;
                if (k == 0) knots[k].slopes[p][l] = .5 * (-3. * value(0) + 4. * value(1) - value(2));
                else if (k == end) knots[k].slopes[p][l] = .5 * (3. * value(end) - 4. * value(end - 1) + value(end - 2));
                else knots[k].slopes[p][l] = .5 * (value(k + 1) - value(k - 1));
            }
        }
    }

    const std::array fractions{std::sqrt(2.) / 5., std::sqrt(3.) / 2.};
    const auto interpolated_firs = [&](double q) {
        std::array<HfFullwaveFir, 2> fir;
        for (std::size_t p = 0; p < fir.size(); ++p) {
            fir[p].first_tap_sample = Table::first_lag_samples();
            fir[p].taps.resize(Table::taps_per_output);
            for (std::size_t l = 0; l < Table::taps_per_output; ++l)
                fir[p].taps[l] = table->coefficient_at_q(p, Table::first_lag_samples() + l, q);
        }
        return fir;
    };
    std::array<std::array<double, Table::taps_per_output>, 2> maximum_derivatives{};
    for (std::size_t k = 0; k < intervals; ++k) {
        const auto& a = knots[k];
        const auto& b = knots[k + 1];
        for (std::size_t p = 0; p < Field{}.size(); ++p) {
            for (std::size_t l = 0; l < Table::taps_per_output; ++l) {
                const auto da = a.slopes[p][l], db = b.slopes[p][l];
                const auto middle = 3. * (b.values[p][l] - a.values[p][l]) - da - db;
                // The derivative is a quadratic Bézier curve. Its largest
                // control magnitude bounds every point on this interval.
                const auto derivative = intervals * std::max({std::abs(da), std::abs(middle), std::abs(db)});
                maximum_derivatives[p][l] = std::max(maximum_derivatives[p][l], derivative);
            }
        }
        for (const auto fraction : fractions) {
            const double q = (k + fraction) / intervals;
            const auto fir = interpolated_firs(q);
            for (const auto f : hf_magnetoionic_holdout_offsets_hz(q, path))
                check_frequency(fir, solve(q, f, Table::production_step_m), q, f, checks.profile_holdout,
                                &checks.profile_holdout_scale);
            ++checks.holdout_profiles_checked;
        }
    }
    if (path == HfMagnetoionicPath::parallel_upper) {
        const double q = (1. + fractions[1]) / Table::tilted_profile_intervals;
        const double f = original_null_witness_offset_hz;
        const auto production = solve(q, f, null_witness_meshes[0]);
        const auto intermediate = solve(q, f, null_witness_meshes[1]);
        const auto fine = solve(q, f, null_witness_meshes[2]);
        const auto fir = interpolated_firs(q);
        auto& row = checks.original_null_witness.emplace();
        row.profile_q = q;
        row.offset_hz = f;
        row.fine_vector_power = std::norm(fine[0]) + std::norm(fine[1]);
        Field approximated;
        for (std::size_t p = 0; p < fir.size(); ++p) {
            const auto z = hf_fullwave_fir_response(fir[p], f / Table::sample_rate_hz);
            approximated[p] = z;
            row.fine_amplitude[p] = std::abs(fine[p]);
            row.fir_amplitude[p] = std::abs(z);
            row.fir_to_production_mesh_error[p] = std::abs(z - target(production[p], f));
            row.production_to_fine_mesh_error[p] = std::abs(production[p] - fine[p]);
            row.intermediate_to_fine_mesh_error[p] = std::abs(intermediate[p] - fine[p]);
            row.fir_to_fine_mesh_error[p] = std::abs(z - target(fine[p], f));
        }
        // Keep the mesh difference visible as a guard at the exposed x null.
        // This is a local sampled criterion, without a spatial error theorem.
        const double reference = row.fine_amplitude[0];
        const double guarded = row.fir_to_fine_mesh_error[0] + row.intermediate_to_fine_mesh_error[0];
        const double low = std::max(0., reference - guarded), high = reference + guarded;
        row.x_guarded_absolute_error = guarded;
        row.x_guarded_power_interval = {low * low, high * high};
        row.x_resolved = reference > 0. && guarded <= Table::original_null_relative_tolerance * reference;
        observe_flux(approximated);
    }
    for (std::size_t p = 0; p < Field{}.size(); ++p) {
        for (std::size_t l = 0; l < Table::taps_per_output; ++l) {
            const auto offset_samples = static_cast<double>(Table::first_lag_samples() + l) - Table::modelling_delay_samples;
            checks.peak_error_seconds_per_q[p] += maximum_derivatives[p][l] * std::abs(offset_samples) / Table::sample_rate_hz;
        }
    }
    table->peak_error_seconds_per_q_ = checks.peak_error_seconds_per_q;
    checks.qualified = checks.rf_solves == rf_solve_limit && knot_checks_pass() && below_tolerance(checks.profile_holdout);
    if (checks.original_null_witness) checks.qualified &= checks.original_null_witness->x_resolved;
    for (std::size_t p = 0; p < Field{}.size(); ++p)
        checks.qualified &= table->retarded_peak_error_bound(Table::transition_seconds, p) <= Table::rate_peak_tolerance;
    if (!checks.qualified) table.reset();
    return {std::move(table), checks};
}

std::size_t Table::workspace_bytes() const noexcept { return sizeof(*this) + knots_.capacity() * sizeof(Knot); }

double Table::retarded_peak_error_bound(double duration_seconds, std::size_t output_index) const {
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.) throw std::invalid_argument("invalid magnetic transition duration");
    return std::numbers::pi / (2. * duration_seconds) * peak_error_seconds_per_q_[validate_output(output_index)];
}

Complex Table::coefficient_at_q(std::size_t output_index, std::size_t lag, double q) const {
    validate_q(q);
    const auto p = validate_output(output_index);
    if (lag < first_lag_samples() || lag > last_lag_samples()) return {};
    const auto [k, u] = bracket(q, profile_intervals());
    const auto l = lag - first_lag_samples();
    const auto& a = knots_[k];
    const auto& b = knots_[k + 1];
    return cubic_value(cubic_weights(u), a.values[p][l], b.values[p][l], a.slopes[p][l], b.slopes[p][l]);
}

double hf_magnetoionic_profile_fraction(std::size_t sample, std::size_t transition_first, double duration_samples) {
    if (!std::isfinite(duration_samples) || duration_samples <= 0. ||
        transition_first > std::numeric_limits<std::size_t>::max() - Table::modelling_delay_samples)
        throw std::invalid_argument("invalid magnetic profile clock");
    return clamped_profile(sample, transition_first + Table::modelling_delay_samples, duration_samples);
}

HfMagnetoionicProfileStream::HfMagnetoionicProfileStream(std::shared_ptr<const Table> table, std::size_t first,
                                                         double duration_seconds, std::size_t initial)
    : table_(std::move(table)), delayed_transition_first_(first), duration_samples_(duration_seconds * Table::sample_rate_hz),
      position_(initial) {
    if (!table_ || !std::isfinite(duration_samples_) || duration_samples_ <= 0. ||
        first > std::numeric_limits<std::size_t>::max() - Table::modelling_delay_samples)
        throw std::invalid_argument("magnetic stream requires a qualified kernel table and valid profile clock");
    for (std::size_t p = 0; p < Field{}.size(); ++p)
        if (table_->retarded_peak_error_bound(duration_seconds, p) > Table::rate_peak_tolerance)
            throw std::invalid_argument("magnetic stream transition exceeds finite-kernel peak error bound");
    delayed_transition_first_ += Table::modelling_delay_samples;
}

Field HfMagnetoionicProfileStream::render(Complex input) {
    history_[cursor_] = input;
    const auto [k, u] = bracket(clamped_profile(position_, delayed_transition_first_, duration_samples_), table_->profile_intervals());
    const auto& a = table_->knots_[k];
    const auto& b = table_->knots_[k + 1];
    const auto weights = cubic_weights(u);
    Field output{};
    for (std::size_t p = 0; p < output.size(); ++p) {
        const auto dot = [&](const auto& taps) {
            Complex sum{};
            auto h = cursor_ >= Table::first_lag_samples() ? cursor_ - Table::first_lag_samples()
                : history_.size() - (Table::first_lag_samples() - cursor_);
            for (const auto tap : taps) {
                sum += tap * history_[h];
                h = h == 0 ? history_.size() - 1 : h - 1;
            }
            return sum;
        };
        if (u == 0.) output[p] = dot(a.values[p]);
        else if (u == 1.) output[p] = dot(b.values[p]);
        else output[p] = cubic_value(weights, dot(a.values[p]), dot(b.values[p]), dot(a.slopes[p]), dot(b.slopes[p]));
    }
    if (++cursor_ == history_.size()) cursor_ = 0;
    ++position_;
    return output;
}

Field HfMagnetoionicProfileStream::step(Complex input) {
    if (draining_ || position_ == std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("magnetic stream input after drain or sample limit");
    remaining_tail_ = tail_samples();
    return render(input);
}

void HfMagnetoionicProfileStream::process(std::span<const Complex> input, std::span<Field> output) {
    if (input.size() != output.size() || draining_ || input.size() > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("invalid magnetic profile stream block or input after drain");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = render(input[i]);
    if (!input.empty()) remaining_tail_ = tail_samples();
}

std::size_t HfMagnetoionicProfileStream::drain(std::span<Field> output) {
    const auto n = std::min(output.size(), remaining_tail_);
    if (n > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("magnetic profile clock overflow while draining");
    draining_ = true;
    for (std::size_t i = 0; i < n; ++i) output[i] = render({});
    remaining_tail_ -= n;
    return n;
}

std::size_t HfMagnetoionicProfileStream::workspace_bytes() const noexcept { return sizeof(*this); }

} // namespace appraisal
