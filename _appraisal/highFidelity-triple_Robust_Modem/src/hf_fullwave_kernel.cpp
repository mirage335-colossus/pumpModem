#include "appraisal/hf_fullwave_kernel.hpp"
#include "appraisal/hf_fullwave.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {

using Complex = std::complex<double>;
constexpr double two_pi = 2. * std::numbers::pi;
constexpr std::size_t max_grid_samples = 1024;
constexpr std::size_t max_lag_samples = 4096;
using Table = HfFullwaveKernelTable;
constexpr double d_initial = 8e8, d_final = 8e9;
constexpr double fine_mesh_m = 5., coarse_mesh_m = 10.;
constexpr std::size_t fine_witness_bins = 2 * Table::frequency_bins;
constexpr std::array<std::size_t, Table::mesh_witnesses> witness_knots{0, 16, 32, 48, 64};
constexpr auto holdout_phase_step_limit = std::numbers::pi / 2.;
constexpr double reflection_front_metres = 2. * (HfVerticalColumn::d_center_m - HfVerticalColumn::d_half_width_m);
constexpr std::array earliest_arrival_samples{reflection_front_metres / 299792458. * Table::sample_rate_hz,
    HfVerticalColumn::top_m / 299792458. * Table::sample_rate_hz};
static_assert(Table::maximum_rf_solves == (Table::intervals + 1) * Table::frequency_bins +
    (3 * Table::intervals + 1) * Table::frequency_holdouts +
    Table::mesh_witnesses * (2 * Table::frequency_bins + Table::frequency_holdouts));

std::size_t port_index(HfFullwavePort port) {
    if (port != HfFullwavePort::reflection && port != HfFullwavePort::transmission)
        throw std::invalid_argument("invalid full-wave table port");
    return static_cast<std::size_t>(port);
}

HfVerticalColumn coupled_profile(double q) {
    return {Table::carrier_hz * (.9999 + q * (1.0001 - .9999)), 60., d_initial + q * (d_final - d_initial)};
}

double frequency_offset(std::size_t n, std::size_t i) {
    return Table::sample_rate_hz * (static_cast<double>(i) / n - .5);
}

} // namespace

std::array<double, Table::frequency_holdouts> hf_fullwave_table_holdout_offsets_hz() {
    std::array<double, Table::frequency_holdouts> out{};
    std::size_t next = 0;
    const double shift = std::sqrt(2.) / 3.;
    const auto add_band = [&](double low, double high, std::size_t bins) {
        for (std::size_t i = 0; i < bins; ++i)
            out[next++] = low + (high - low) * ((i + shift) / bins);
    };
    add_band(-Table::sample_rate_hz / 2., -Table::occupied_half_band_hz, 7);
    add_band(-Table::occupied_half_band_hz, Table::occupied_half_band_hz, 51);
    add_band(Table::occupied_half_band_hz, Table::sample_rate_hz / 2., 7);
    return out;
}

double hf_fullwave_frozen_flux_tolerance() {
    constexpr double solver_flux_tolerance = 1e-8;
    const double maximum_norm = std::sqrt(1. + solver_flux_tolerance) + std::numbers::sqrt2 * Table::complex_tolerance;
    return maximum_norm * maximum_norm - 1.;
}

namespace {

Complex response_port(const HfFullwaveResponse& response, std::size_t port) {
    return port == 0 ? response.reflection : response.transmission;
}

Complex table_target(Complex rf_response, double offset) {
    return std::conj(rf_response) * std::polar(hf_fullwave_guard_taper(offset, Table::sample_rate_hz,
        Table::occupied_half_band_hz), -two_pi * offset / Table::sample_rate_hz * Table::modelling_delay_samples);
}

void observe(HfFullwaveBandError& item, double error, double q, double offset) {
    if (!std::isfinite(error)) throw std::runtime_error("non-finite full-wave table check");
    if (error > item.maximum) item = {error, q, offset};
}

std::pair<std::size_t, double> bracket(double q) {
    const double at = q * Table::intervals;
    const auto lower = std::min(static_cast<std::size_t>(at), Table::intervals - 1);
    return {lower, at - lower};
}

double half_cosine_profile(std::size_t sample, std::size_t delayed_start, double duration_samples) {
    if (sample <= delayed_start) return 0.;
    const double u = static_cast<double>(sample - delayed_start) / duration_samples;
    return u >= 1. ? 1. : .5 * (1. - std::cos(std::numbers::pi * u));
}

bool valid_band(double sample_rate, double occupied_half_band) {
    return std::isfinite(sample_rate) && std::isfinite(occupied_half_band) &&
        occupied_half_band > 0. && occupied_half_band < sample_rate / 2.;
}

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

// Complementary quintic smoothstep, with zero first and second derivative
// on either join. The occupied-band values remain untouched.
double taper(double offset, double half_rate, double occupied_half_band) {
    const double f = std::abs(offset);
    if (f <= occupied_half_band) return 1.;
    if (f >= half_rate) return 0.;
    const double x = (half_rate - f) / (half_rate - occupied_half_band);
    return x * x * x * (10. + x * (-15. + 6. * x));
}

std::size_t validate_fir(const HfFullwaveFir& fir) {
    if (fir.taps.empty() || fir.taps.size() > max_grid_samples || fir.first_tap_sample > max_lag_samples ||
        fir.taps.size() - 1 > max_lag_samples - fir.first_tap_sample)
        throw std::invalid_argument("full-wave FIR outside lag or tap budget");
    for (const auto tap : fir.taps)
        if (!finite(tap)) throw std::invalid_argument("non-finite full-wave FIR tap");
    return fir.first_tap_sample + fir.taps.size() - 1;
}

} // namespace

double hf_fullwave_guard_taper(double offset, double sample_rate, double occupied_half_band) {
    if (!std::isfinite(offset) || !valid_band(sample_rate, occupied_half_band))
        throw std::invalid_argument("invalid full-wave taper band");
    return taper(offset, sample_rate / 2., occupied_half_band);
}

HfFullwavePhaseBlend hf_fullwave_phase_blend(double lower_phase, double upper_phase, double fraction) {
    const double delta = upper_phase - lower_phase;
    if (!std::isfinite(delta) || !std::isfinite(fraction) || fraction < 0. || fraction > 1.)
        throw std::invalid_argument("invalid full-wave phase blend coordinate");
    // Combine removal and restoration before taking the phasor. No divisions
    // by a port amplitude or wrapped phase enter this interpolation.
    return {std::polar(1. - fraction, fraction * delta),
            std::polar(fraction, -(1. - fraction) * delta)};
}

HfFullwaveKernel design_hf_fullwave_kernel(std::span<const Complex> rf_grid,
                                          const HfFullwaveKernelDesign& d) {
    const auto n = rf_grid.size();
    if (!std::has_single_bit(n) || n < 16 || n > max_grid_samples ||
        !valid_band(d.sample_rate_hz, d.occupied_half_band_hz) || !std::isfinite(d.earliest_arrival_samples) ||
        d.earliest_arrival_samples < 0. || d.earliest_arrival_samples > max_lag_samples ||
        d.modelling_delay_samples >= n / 2 || d.retained_late_samples >= n / 2)
        throw std::invalid_argument("invalid full-wave kernel grid, band, arrival or support");
    const auto first = static_cast<std::size_t>(std::ceil(d.earliest_arrival_samples));
    if (d.modelling_delay_samples + d.retained_late_samples > max_lag_samples - first)
        throw std::invalid_argument("full-wave kernel lag exceeds budget");
    HfFullwaveKernel result{{first, std::vector<Complex>(1 + d.modelling_delay_samples + d.retained_late_samples)},
                            false, 0., 0., 0., 0., 0.};
    double scale = 0.;
    for (const auto value : rf_grid) {
        if (!finite(value)) throw std::invalid_argument("non-finite full-wave frequency-grid input");
        scale = std::max({scale, std::abs(value.real()), std::abs(value.imag())});
    }
    if (scale == 0.) return result;
    result.solver_grid_nonzero = true;
    const auto half = static_cast<int>(n / 2);
    const auto delay = static_cast<int>(d.modelling_delay_samples);
    const auto late = static_cast<int>(d.retained_late_samples);
    std::vector<Complex> spectrum(n);
    for (int k = -half; k < half; ++k) {
        const auto i = static_cast<std::size_t>(k + half);
        const double f = static_cast<double>(k) / n;
        // Remove only the known integer envelope delay for the circular DFT.
        // The carrier phase is not re-referenced or normalized.
        const Complex scaled{rf_grid[i].real() / scale, -rf_grid[i].imag() / scale};
        spectrum[i] = scaled * std::polar(taper(f * d.sample_rate_hz,
            d.sample_rate_hz / 2., d.occupied_half_band_hz), two_pi * f * first);
        result.untapered_grid_mean_power += std::norm(scaled) / n;
        result.tapered_grid_mean_power += std::norm(spectrum[i]) / n;
    }
    // Scale the DFT for reliable relative energies even on very weak ports.
    // Restore the physical amplitude exactly once; this is not gain normalization.
    result.untapered_grid_mean_power = (result.untapered_grid_mean_power * scale) * scale;
    result.tapered_grid_mean_power = (result.tapered_grid_mean_power * scale) * scale;
    if (!std::isfinite(result.untapered_grid_mean_power) || !std::isfinite(result.tapered_grid_mean_power))
        throw std::invalid_argument("non-finite full-wave frequency-grid power");

    // Bounded direct inverse DFT of the de-embedded response. Compute the
    // whole signed period so the cutoff's omitted energy is observable.
    double total = 0.;
    const double earliest_relative = d.earliest_arrival_samples - first;
    for (int j = -half; j < half; ++j) {
        const auto rotator = std::polar(1., two_pi * j / n);
        // k = -N/2: exp(-i*pi*j) follows the parity of j.
        Complex basis{j % 2 == 0 ? 1. : -1., 0.};
        Complex value{};
        for (const auto bin : spectrum) {
            value += bin * basis;
            basis *= rotator;
        }
        value /= n;
        const auto energy = std::norm(value);
        total += energy;
        if (j < earliest_relative) result.prearrival_energy_fraction += energy;
        if (j < -delay) result.discarded_pre_energy_fraction += energy;
        else if (j > late) result.discarded_tail_energy_fraction += energy;
        else result.fir.taps[static_cast<std::size_t>(j + delay)] = value * scale;
    }
    if (!std::isfinite(total)) throw std::invalid_argument("non-finite full-wave impulse energy");
    if (total > 0.) {
        result.prearrival_energy_fraction /= total;
        result.discarded_pre_energy_fraction /= total;
        result.discarded_tail_energy_fraction /= total;
    }
    return result;
}

Complex hf_fullwave_fir_response(const HfFullwaveFir& fir, double normalized_frequency) {
    validate_fir(fir);
    if (!std::isfinite(normalized_frequency) || std::abs(normalized_frequency) > .5)
        throw std::invalid_argument("full-wave FIR evaluation outside sampled baseband");
    const double phase = -two_pi * normalized_frequency;
    const auto rotator = std::polar(1., phase);
    auto basis = std::polar(1., phase * fir.first_tap_sample);
    Complex result{};
    for (const auto tap : fir.taps) {
        result += tap * basis;
        basis *= rotator;
    }
    return result;
}

HfFullwaveKernelTableBuild build_hf_fullwave_kernel_table(std::stop_token stop) {
    auto table = std::shared_ptr<Table>(new Table);
    HfFullwaveKernelTableChecks checks;
    const auto offsets = hf_fullwave_table_holdout_offsets_hz();
    const double phase_reference = hf_fullwave_d_carrier_phase_radians(Table::carrier_hz, d_initial);
    auto solve = [&](double q, double offset, double mesh_m) {
        if (stop.stop_requested()) throw std::runtime_error("full-wave table construction cancelled");
        if (checks.rf_solves >= Table::maximum_rf_solves) throw std::logic_error("full-wave table exceeded its RF solve budget");
        ++checks.rf_solves;
        return solve_hf_vertical_column(Table::carrier_hz + offset, coupled_profile(q), mesh_m);
    };
    auto design_port = [&](std::span<const HfFullwaveResponse> raw, std::size_t p) {
        std::array<Complex, fine_witness_bins> grid;
        for (std::size_t i = 0; i < raw.size(); ++i) grid[i] = response_port(raw[i], p);
        auto kernel = design_hf_fullwave_kernel(std::span{grid}.first(raw.size()), {Table::sample_rate_hz,
            Table::occupied_half_band_hz, earliest_arrival_samples[p], Table::modelling_delay_samples,
            Table::taps_per_port - 1 - Table::modelling_delay_samples});
        checks.maximum_discarded_energy[p] = std::max(checks.maximum_discarded_energy[p],
            kernel.discarded_pre_energy_fraction + kernel.discarded_tail_energy_fraction);
        checks.maximum_prearrival_energy[p] = std::max(checks.maximum_prearrival_energy[p], kernel.prearrival_energy_fraction);
        return std::move(kernel.fir);
    };
    auto observe_flux = [&](const std::array<Complex, 2>& symbols) {
        const double excess = std::norm(symbols[0]) + std::norm(symbols[1]) - 1.;
        checks.maximum_frozen_flux_excess = std::max(checks.maximum_frozen_flux_excess, excess);
    };
    auto check_response = [&](const std::array<HfFullwaveFir, 2>& fir, const HfFullwaveResponse& raw,
                              double q, double f, std::array<HfFullwaveBandError, 2>& errors) {
        std::array<Complex, 2> actual;
        for (std::size_t p = 0; p < actual.size(); ++p) {
            actual[p] = hf_fullwave_fir_response(fir[p], f / Table::sample_rate_hz);
            observe(errors[p], std::abs(actual[p] - table_target(response_port(raw, p), f)), q, f);
        }
        observe_flux(actual);
    };
    const auto below_complex_tolerance = [](const HfFullwaveBandError& e) {
        return e.maximum >= 0. && e.maximum <= Table::complex_tolerance;
    };
    const auto knot_checks_pass = [&] {
        for (std::size_t p = 0; p < 2; ++p)
            if (!below_complex_tolerance(checks.knot_grid[p]) || !below_complex_tolerance(checks.knot_offgrid[p]) ||
                !below_complex_tolerance(checks.witness_frequency_refinement[p]) || !below_complex_tolerance(checks.witness_mesh[p]) ||
                checks.maximum_discarded_energy[p] > Table::discarded_energy_tolerance)
                return false;
        return checks.maximum_frozen_flux_excess <= hf_fullwave_frozen_flux_tolerance();
    };

    // Retain only bounded fine holdout responses for the q secants. Training
    // and mesh-witness frequency grids are local to the current knot.
    using HoldoutResponses = std::array<HfFullwaveResponse, Table::frequency_holdouts>;
    std::vector<HoldoutResponses> knot_holdouts(Table::intervals + 1);
    auto qualify_knot = [&](std::size_t k, bool witness) {
        const double q = static_cast<double>(k) / Table::intervals;
        const double relative_d_phase = hf_fullwave_d_carrier_phase_radians(Table::carrier_hz,
            coupled_profile(q).d_peak_electron_density_per_m3) - phase_reference;
        auto& knot = table->knots_[k];
        knot.native_phase = {-2. * relative_d_phase, -relative_d_phase};
        std::array<HfFullwaveResponse, Table::frequency_bins> grid;
        for (std::size_t i = 0; i < grid.size(); ++i)
            grid[i] = solve(q, frequency_offset(grid.size(), i), fine_mesh_m);
        for (std::size_t i = 0; i < offsets.size(); ++i) knot_holdouts[k][i] = solve(q, offsets[i], fine_mesh_m);
        for (std::size_t p = 0; p < knot.fir.size(); ++p) knot.fir[p] = design_port(grid, p);
        for (std::size_t i = 0; i < grid.size(); ++i)
            check_response(knot.fir, grid[i], q, frequency_offset(grid.size(), i), checks.knot_grid);
        for (std::size_t i = 0; i < offsets.size(); ++i)
            check_response(knot.fir, knot_holdouts[k][i], q, offsets[i], checks.knot_offgrid);
        ++checks.knot_profiles_checked;
        if (!witness) return;

        std::array<HfFullwaveResponse, fine_witness_bins> refined_grid;
        for (std::size_t i = 0; i < refined_grid.size(); ++i)
            refined_grid[i] = i % 2 == 0 ? grid[i / 2] : solve(q, frequency_offset(refined_grid.size(), i), fine_mesh_m);
        const std::array refined_fir{design_port(refined_grid, 0), design_port(refined_grid, 1)};
        auto compare_frequency = [&](double f, const HfFullwaveResponse& fine) {
            // Check the production representation at the additional witness
            // bins too, so agreement between transforms cannot hide a shared error.
            check_response(knot.fir, fine, q, f, checks.knot_grid);
            for (std::size_t p = 0; p < knot.fir.size(); ++p) {
                const double offset = f / Table::sample_rate_hz;
                observe(checks.witness_frequency_refinement[p], std::abs(hf_fullwave_fir_response(knot.fir[p], offset) -
                    hf_fullwave_fir_response(refined_fir[p], offset)), q, f);
            }
        };
        for (std::size_t i = 0; i < refined_grid.size(); ++i)
            compare_frequency(frequency_offset(refined_grid.size(), i), refined_grid[i]);
        for (std::size_t i = 0; i < offsets.size(); ++i) compare_frequency(offsets[i], knot_holdouts[k][i]);
        auto compare_mesh = [&](double f, const HfFullwaveResponse& fine) {
            const auto coarse = solve(q, f, coarse_mesh_m);
            for (std::size_t p = 0; p < knot.fir.size(); ++p)
                observe(checks.witness_mesh[p], std::abs(response_port(fine, p) - response_port(coarse, p)), q, f);
        };
        for (std::size_t i = 0; i < grid.size(); ++i) compare_mesh(frequency_offset(grid.size(), i), grid[i]);
        for (std::size_t i = 0; i < offsets.size(); ++i) compare_mesh(offsets[i], knot_holdouts[k][i]);
    };
    for (const auto k : witness_knots) qualify_knot(k, true);
    checks.witnesses_qualified = knot_checks_pass();
    if (!checks.witnesses_qualified) return {nullptr, checks};
    for (std::size_t k = 0; k <= Table::intervals; ++k)
        if (std::ranges::find(witness_knots, k) == witness_knots.end()) qualify_knot(k, false);
    if (!knot_checks_pass()) return {nullptr, checks};

    const std::array profile_fractions{std::sqrt(2.) / 5., std::sqrt(3.) / 2.};
    std::array<std::array<double, Table::taps_per_port>, 2> maximum_tap_derivative{};
    for (std::size_t k = 0; k < Table::intervals; ++k) {
        const auto& lower = table->knots_[k];
        const auto& upper = table->knots_[k + 1];
        for (std::size_t p = 0; p < lower.fir.size(); ++p) {
            const double phase_step = upper.native_phase[p] - lower.native_phase[p];
            const double phase_rate = phase_step * Table::intervals;
            const auto endpoint_derotation = std::polar(1., -phase_step);
            for (std::size_t l = 0; l < Table::taps_per_port; ++l) {
                const Complex a = lower.fir[p].taps[l], b = upper.fir[p].taps[l] * endpoint_derotation;
                const auto dz = (b - a) * static_cast<double>(Table::intervals);
                const Complex slope{0., phase_rate};
                // For h=exp(i phi) z, z' + i phi' z is affine in q on a span.
                // Its convex norm cannot exceed the largest endpoint norm.
                const double limit = std::max(std::abs(dz + slope * a), std::abs(dz + slope * b));
                maximum_tap_derivative[p][l] = std::max(maximum_tap_derivative[p][l], limit);
            }
            const auto first = lower.fir[p].first_tap_sample;
            const double flight_seconds = std::max(std::abs(static_cast<double>(first) - Table::modelling_delay_samples),
                static_cast<double>(first + Table::taps_per_port - 1 - Table::modelling_delay_samples)) / Table::sample_rate_hz;
            auto& phase_flight = checks.carrier_flight_radians_seconds_per_q[p];
            phase_flight = std::max(phase_flight, std::abs(phase_rate) * flight_seconds);
        }
        const std::array q{static_cast<double>(k) / Table::intervals,
                           (k + profile_fractions[0]) / Table::intervals,
                           (k + profile_fractions[1]) / Table::intervals,
                           static_cast<double>(k + 1) / Table::intervals};
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            const double f = offsets[i];
            const std::array holdouts{knot_holdouts[k][i], solve(q[1], f, fine_mesh_m),
                                      solve(q[2], f, fine_mesh_m), knot_holdouts[k + 1][i]};
            std::array<std::array<Complex, 2>, 2> actual;
            for (std::size_t p = 0; p < lower.fir.size(); ++p) {
                const auto a = hf_fullwave_fir_response(lower.fir[p], f / Table::sample_rate_hz);
                const auto b = hf_fullwave_fir_response(upper.fir[p], f / Table::sample_rate_hz);
                for (std::size_t j = 0; j < profile_fractions.size(); ++j) {
                    actual[j][p] = hf_fullwave_phase_blend(lower.native_phase[p], upper.native_phase[p], profile_fractions[j]).apply(a, b);
                    observe(checks.profile_holdout[p], std::abs(actual[j][p] - table_target(response_port(holdouts[j + 1], p), f)), q[j + 1], f);
                }
                for (std::size_t s = 0; s + 1 < q.size(); ++s) {
                    const auto a_solver = response_port(holdouts[s], p), b_solver = response_port(holdouts[s + 1], p);
                    if (std::min(std::abs(a_solver), std::abs(b_solver)) < Table::phase_observation_min_amplitude) {
                        ++checks.weak_phase_pairs[p];
                        continue;
                    }
                    const double phase_step = std::abs(std::arg(b_solver * std::conj(a_solver)));
                    checks.largest_observed_phase_step_rad[p] = std::max(checks.largest_observed_phase_step_rad[p], phase_step);
                    if (phase_step >= holdout_phase_step_limit) {
                        ++checks.ambiguous_phase_pairs[p];
                        continue;
                    }
                    ++checks.observed_phase_pairs[p];
                    auto& rate = checks.worst_profile_phase_rate_rad_per_q[p];
                    rate = std::max(rate, phase_step / (q[s + 1] - q[s]));
                }
            }
            for (const auto& response : actual) observe_flux(response);
        }
        checks.holdout_profiles_checked += profile_fractions.size();
    }

    bool passed = checks.rf_solves == Table::maximum_rf_solves && knot_checks_pass();
    for (std::size_t p = 0; p < maximum_tap_derivative.size(); ++p) {
        const auto first = table->knots_.front().fir[p].first_tap_sample;
        for (std::size_t l = 0; l < Table::taps_per_port; ++l) {
            const double lag_seconds = std::abs(static_cast<double>(first + l) - Table::modelling_delay_samples) / Table::sample_rate_hz;
            checks.peak_error_seconds_per_q[p] += maximum_tap_derivative[p][l] * lag_seconds;
        }
        passed &= below_complex_tolerance(checks.profile_holdout[p]) && std::isfinite(checks.peak_error_seconds_per_q[p]);
    }
    table->peak_error_seconds_per_q_ = checks.peak_error_seconds_per_q;
    table->carrier_flight_radians_seconds_per_q_ = checks.carrier_flight_radians_seconds_per_q;
    checks.qualified = passed;
    if (!passed) table.reset();
    return {std::move(table), checks};
}

std::size_t Table::first_lag_samples(HfFullwavePort port) const {
    return knots_.front().fir[port_index(port)].first_tap_sample;
}

std::size_t Table::planned_first_lag_samples(HfFullwavePort port) {
    return static_cast<std::size_t>(std::ceil(earliest_arrival_samples[port_index(port)]));
}

std::size_t Table::last_lag_samples(HfFullwavePort port) const {
    return first_lag_samples(port) + taps_per_port - 1;
}

std::size_t Table::workspace_bytes() const noexcept {
    std::size_t bytes = sizeof(*this);
    for (const auto& knot : knots_)
        for (const auto& fir : knot.fir) bytes += fir.taps.capacity() * sizeof(Complex);
    return bytes;
}

double Table::retarded_peak_error_bound(HfFullwavePort port, double duration_seconds) const {
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.) throw std::invalid_argument("invalid full-wave transition duration");
    return std::numbers::pi / (2. * duration_seconds) * peak_error_seconds_per_q_[port_index(port)];
}

double Table::carrier_flight_phase_bound(HfFullwavePort port, double duration_seconds) const {
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.) throw std::invalid_argument("invalid full-wave transition duration");
    return std::numbers::pi / (2. * duration_seconds) * carrier_flight_radians_seconds_per_q_[port_index(port)];
}

Complex Table::coefficient_at_q(HfFullwavePort port, std::size_t lag, double q) const {
    if (!std::isfinite(q) || q < 0. || q > 1.) throw std::invalid_argument("invalid full-wave profile fraction");
    const auto p = port_index(port);
    const auto first = first_lag_samples(port);
    if (lag < first || lag - first >= taps_per_port) return {};
    const auto [k, u] = bracket(q);
    const auto& a = knots_[k];
    const auto& b = knots_[k + 1];
    return hf_fullwave_phase_blend(a.native_phase[p], b.native_phase[p], u).apply(a.fir[p].taps[lag - first], b.fir[p].taps[lag - first]);
}

HfFullwaveProfileStream::HfFullwaveProfileStream(std::shared_ptr<const Table> table, HfFullwavePort port,
                                                 std::size_t transition_first, double duration_seconds,
                                                 std::size_t initial_output_sample)
    : table_(std::move(table)), port_index_(port_index(port)), transition_first_(transition_first), duration_samples_(0.),
      position_(initial_output_sample) {
    if (!table_ || std::ranges::find(Table::transition_durations_seconds, duration_seconds) == Table::transition_durations_seconds.end() ||
        transition_first_ > std::numeric_limits<std::size_t>::max() - Table::modelling_delay_samples)
        throw std::invalid_argument("full-wave stream requires qualified kernels and a declared transition");
    if (table_->retarded_peak_error_bound(port, duration_seconds) > Table::rate_peak_tolerance)
        throw std::invalid_argument("full-wave stream transition exceeds the finite-kernel peak error bound");
    transition_first_ += Table::modelling_delay_samples;
    duration_samples_ = duration_seconds * Table::sample_rate_hz;
    history_.resize(table_->last_lag_samples(port) + 1);
}

Complex HfFullwaveProfileStream::render(Complex input) {
    history_[cursor_] = input;
    const auto [k, u] = bracket(half_cosine_profile(position_, transition_first_, duration_samples_));
    const auto p = port_index_;
    const auto& lower = table_->knots_[k];
    const auto& upper = table_->knots_[k + 1];
    const auto first = lower.fir[p].first_tap_sample;
    auto h = cursor_ >= first ? cursor_ - first : history_.size() - (first - cursor_);
    Complex a{}, b{};
    for (std::size_t l = 0; l < Table::taps_per_port; ++l) {
        if (u != 1.) a += history_[h] * lower.fir[p].taps[l];
        if (u != 0.) b += history_[h] * upper.fir[p].taps[l];
        h = h == 0 ? history_.size() - 1 : h - 1;
    }
    if (++cursor_ == history_.size()) cursor_ = 0;
    ++position_;
    return hf_fullwave_phase_blend(lower.native_phase[p], upper.native_phase[p], u).apply(a, b);
}

Complex HfFullwaveProfileStream::step(Complex input) {
    if (draining_ || position_ == std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("full-wave stream input after drain or sample limit");
    remaining_tail_ = tail_samples();
    return render(input);
}

void HfFullwaveProfileStream::process(std::span<const Complex> input, std::span<Complex> output) {
    if (input.size() != output.size() || draining_ || input.size() > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("invalid full-wave profile stream block or input after drain");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = render(input[i]);
    if (!input.empty()) remaining_tail_ = tail_samples();
}

std::size_t HfFullwaveProfileStream::drain(std::span<Complex> output) {
    const auto n = std::min(output.size(), remaining_tail_);
    if (n > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("full-wave profile stream position overflow while draining");
    draining_ = true;
    for (std::size_t i = 0; i < n; ++i) output[i] = render({});
    remaining_tail_ -= n;
    return n;
}

std::size_t HfFullwaveProfileStream::workspace_bytes() const noexcept {
    return sizeof(*this) + history_.capacity() * sizeof(Complex);
}

} // namespace appraisal
