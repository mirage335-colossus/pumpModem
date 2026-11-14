#include "appraisal/hf_wave_optics.hpp"

#include "appraisal/hf_fullwave_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {

using Table = HfWaveOpticsTable;
using Complex = std::complex<double>;
constexpr double two_pi = 2. * std::numbers::pi;
constexpr double classical_electron_radius_m = 2.8179403205e-15; // CODATA 2022
constexpr double tecu = 1e16;
constexpr double earliest_samples = Table::total_distance_m / Table::light_speed_mps * Table::sample_rate_hz;
constexpr std::size_t reference_lag = Table::first_lag_samples() + Table::modelling_delay_samples;
constexpr double series_tolerance = 1e-13;
constexpr double far_field_tolerance = 1e-8;
constexpr std::size_t term_limit = 64;
constexpr std::uint64_t maximum_exact_sample = std::uint64_t{1} << 53;
constexpr double minimum_frequency_hz = Table::carrier_hz - .5 * Table::sample_rate_hz;
constexpr double maximum_frequency_hz = Table::carrier_hz + .5 * Table::sample_rate_hz;
static_assert(earliest_samples > Table::first_lag_samples() - 1 && earliest_samples <= Table::first_lag_samples());
static_assert(Table::position_intervals * Table::position_step_m == Table::maximum_table_position_m);
static_assert(Table::refinement_bins == 2 * Table::frequency_bins);

constexpr HfFullwaveKernelDesign design{Table::sample_rate_hz, Table::occupied_half_band_hz,
    earliest_samples, Table::modelling_delay_samples, Table::retained_late_samples};

constexpr std::array knot_offsets{-864., -560.75, -384., -311.37, -257.19, -118.7, -25., 0.,
    19.75, 127.325, 235.5, 319.75, 384., 532.25, 807.19};
constexpr std::array position_offsets{-384., -302.6, -199.25, -118.7, 0., 62.85, 164.8, 293.45, 384.};
constexpr std::array positions_for_refinement{0u, 5u, 12u, 27u, 128u};
constexpr std::array position_fractions{.317, .731};
constexpr auto expected_evaluations = (Table::position_intervals + 1) * (Table::frequency_bins + knot_offsets.size()) +
    Table::position_intervals * position_fractions.size() * position_offsets.size() +
    positions_for_refinement.size() * (Table::refinement_bins - Table::frequency_bins);
static_assert(expected_evaluations <= Table::maximum_rf_evaluations);

bool finite(Complex value) { return std::isfinite(value.real()) && std::isfinite(value.imag()); }

void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("wave optics preparation cancelled");
}

double plasma_ratio(double wavelength, double column_tecu) {
    return classical_electron_radius_m * wavelength * wavelength * column_tecu * tecu /
        (std::numbers::pi * Table::slab_length_m);
}

constexpr double cauchy_radius = 4.;

double coefficient_bound_log(double optical_thickness, double abs_r) {
    // On |z|=R, |sqrt(1-r*z)-1| <= R*|r|/(1+sqrt(1-R*|r|)).
    const double t = cauchy_radius * abs_r;
    if (t >= 1.) throw std::invalid_argument("wave optics column exceeds the analytic underdense domain");
    return optical_thickness * t / (1. + std::sqrt(1. - t));
}

double tail_bound(double coefficient_log, std::size_t m, double integral_scale) {
    // Cauchy's estimate gives |c_j| <= exp(coefficient_log)/R^j.
    // The jth integrated absolute Gaussian is <= 2*pi*sigma_x*sigma_y/j.
    const double next = static_cast<double>(m + 1);
    return integral_scale * std::exp(coefficient_log - next * std::log(cauchy_radius)) /
        (next * (1. - 1. / cauchy_radius));
}

double far_field_bound(double peak_delta_tecu) {
    // Uniform across the frequency grid and all |x| at or past the table edge.
    // Discarding the y exponent can only increase the bound. The x exponent
    // decreases monotonically in |x| for every term.
    const double alpha_min = std::numbers::pi * minimum_frequency_hz /
        (Table::light_speed_mps * Table::effective_distance_m);
    const double alpha_max = std::numbers::pi * maximum_frequency_hz /
        (Table::light_speed_mps * Table::effective_distance_m);
    if (peak_delta_tecu == 0.) return 0.;
    const double wavelength_max = Table::light_speed_mps / minimum_frequency_hz;
    const double r_max = plasma_ratio(wavelength_max, std::abs(peak_delta_tecu)) /
        (1. - plasma_ratio(wavelength_max, Table::ambient_tecu));
    const double optical_max = two_pi * maximum_frequency_hz * Table::slab_length_m / Table::light_speed_mps;
    const double log_bound = coefficient_bound_log(optical_max, r_max);
    const double integral_scale = 2. * Table::sigma_x_m * Table::sigma_y_m * alpha_max;
    std::array<double, term_limit + 1> coefficients{}, nth_log_coefficients{};
    coefficients[0] = 1.;
    double binomial = 1., correction_bound = 0.;
    for (std::size_t m = 1; m <= term_limit; ++m) {
        const double index = static_cast<double>(m);
        binomial *= std::abs(1.5 - index) * r_max / index;
        nth_log_coefficients[m] = index * optical_max * binomial;
        for (std::size_t k = 1; k <= m; ++k)
            coefficients[m] += nth_log_coefficients[k] * coefficients[m - k];
        coefficients[m] /= index;
        const double a = index / (2. * Table::sigma_x_m * Table::sigma_x_m);
        const double decay = alpha_min * alpha_min * a / (a * a + alpha_max * alpha_max);
        correction_bound += integral_scale * coefficients[m] / index *
            std::exp(-decay * Table::maximum_table_position_m * Table::maximum_table_position_m);
    }
    return correction_bound + tail_bound(log_bound, term_limit, integral_scale);
}

class GaussianSeries {
    struct Term { Complex coefficient, x2_exponent; };
public:
    GaussianSeries(double frequency, double peak_delta_tecu, double y_m) {
        if (!std::isfinite(frequency) || frequency < minimum_frequency_hz || frequency > maximum_frequency_hz ||
            !std::isfinite(peak_delta_tecu) || std::abs(peak_delta_tecu) > Table::maximum_peak_delta_tecu ||
            !std::isfinite(y_m) || std::abs(y_m) > Table::maximum_impact_m)
            throw std::invalid_argument("wave optics frequency, differential column or cross-track impact outside supported bounds");
        const double wavelength = Table::light_speed_mps / frequency;
        const double wave_number = two_pi / wavelength;
        const double ambient_ratio = plasma_ratio(wavelength, Table::ambient_tecu);
        const double ambient_index = std::sqrt(1. - ambient_ratio);
        const double ambient_excess = -Table::slab_length_m * ambient_ratio / (1. + ambient_index);
        ambient_ = std::polar(1., wave_number * Table::total_distance_m +
            wave_number * ambient_excess);
        if (peak_delta_tecu == 0.) return;
        const double alpha = wave_number / (2. * Table::effective_distance_m);
        const auto ia = Complex{0., alpha};
        const double r = plasma_ratio(wavelength, peak_delta_tecu) / (1. - ambient_ratio);
        const double optical = wave_number * Table::slab_length_m * ambient_index;
        const double log_bound = coefficient_bound_log(optical, std::abs(r));
        const double integral_scale = 2. * Table::sigma_x_m * Table::sigma_y_m * alpha;
        std::array<Complex, term_limit + 1> coefficients{};
        std::array<double, term_limit + 1> nth_log_coefficients{};
        coefficients[0] = 1.;
        double binomial = 1.;
        for (std::size_t m = 1; m <= term_limit; ++m) {
            const double index = static_cast<double>(m);
            // c(g)=exp(b(g)), so m*c_m=sum k*b_k*c_(m-k).
            binomial *= (1.5 - index) * (-r) / index;
            nth_log_coefficients[m] = index * optical * binomial;
            for (std::size_t k = 1; k <= m; ++k)
                coefficients[m] += Complex{0., nth_log_coefficients[k]} * coefficients[m - k];
            coefficients[m] /= index;
            const double ax = index / (2. * Table::sigma_x_m * Table::sigma_x_m);
            const double ay = index / (2. * Table::sigma_y_m * Table::sigma_y_m);
            const auto dx = ax - ia, dy = ay - ia;
            // Integral of exp(-a*u^2 + i*alpha*(u+centre)^2). Use
            // i*alpha*a/(a-i*alpha), whose real part is always negative,
            // rather than subtracting separately large phase terms.
            const auto x_exp = ia * ax / dx;
            const auto y_exp = ia * ay / dy;
            const auto x_integral = std::sqrt(std::numbers::pi / dx);
            const auto y_integral = std::sqrt(std::numbers::pi / dy) * std::exp(y_exp * (y_m * y_m));
            terms_[m - 1] = {(-ia / std::numbers::pi) * coefficients[m] * x_integral * y_integral, x_exp};
            terms_count_ = m;
            tail_bound_ = tail_bound(log_bound, m, integral_scale);
            if (tail_bound_ <= series_tolerance) return;
        }
        throw std::runtime_error("wave optics Gaussian expansion exceeded the bounded term budget");
    }

    HfWaveOpticsResponse at(double x) const {
        if (!std::isfinite(x) || std::abs(x) > 1e8)
            throw std::invalid_argument("wave optics displacement outside supported coordinates");
        Complex field{1., 0.}, derivative{};
        for (std::size_t m = 0; m < terms_count_; ++m) {
            const auto& term = terms_[m];
            const auto contribution = term.coefficient * std::exp(term.x2_exponent * (x * x));
            field += contribution;
            derivative += contribution * term.x2_exponent;
        }
        return {ambient_ * field, x != 0. ? (2. * x) * ambient_ * derivative : Complex{}, tail_bound_, terms_count_};
    }

    Complex ambient() const noexcept { return ambient_; }

private:
    std::array<Term, term_limit> terms_{};
    Complex ambient_{};
    double tail_bound_ = 0.;
    std::size_t terms_count_ = 0;
};

double frequency_offset(std::size_t i, std::size_t n) {
    return Table::sample_rate_hz * (static_cast<double>(i) / static_cast<double>(n) - .5);
}

std::array<double, 4> weights(double u) {
    const double v = 1. - u;
    return {v * v * (1. + 2. * u), u * u * (1. + 2. * v), u * v * v, -u * u * v};
}

std::pair<std::size_t, double> bracket(double x) {
    const double u = std::min(std::abs(x), Table::maximum_table_position_m) / Table::position_step_m;
    const auto lower = std::min(static_cast<std::size_t>(u), Table::position_intervals - 1);
    return {lower, u - static_cast<double>(lower)};
}

Complex blend(const std::array<double, 4>& w, Complex a, Complex b, Complex da, Complex db) {
    return w[0] * a + w[1] * b + w[2] * da + w[3] * db;
}

Complex target(Complex rf, double offset) {
    return std::conj(rf) * std::polar(hf_fullwave_guard_taper(offset, Table::sample_rate_hz,
        Table::occupied_half_band_hz), -two_pi * offset / Table::sample_rate_hz * Table::modelling_delay_samples);
}

bool check_error(HfWaveOpticsChecks& c, HfWaveOpticsWitness& worst, Complex actual, Complex rf,
                 double x, double offset) {
    if (!finite(actual) || !finite(rf)) throw std::runtime_error("nonfinite wave optics response witness");
    const HfWaveOpticsWitness row{std::abs(actual - target(rf, offset)), std::abs(rf), x, offset};
    if (row.absolute_error > worst.absolute_error) worst = row;
    if (std::abs(offset) <= Table::occupied_half_band_hz) {
        if (c.occupied_witnesses == 0 || row.raw_amplitude < c.minimum_raw_voltage.raw_amplitude)
            c.minimum_raw_voltage = row;
        ++c.occupied_witnesses;
        if (row.raw_amplitude < Table::voltage_resolution_floor) ++c.unresolved_witnesses;
        else if (row.absolute_error > Table::nonzero_relative_tolerance * row.raw_amplitude) return false;
    }
    return row.absolute_error <= Table::absolute_tolerance;
}

void observe_series(HfWaveOpticsChecks& checks, const HfWaveOpticsResponse& value) {
    if (++checks.rf_evaluations > Table::maximum_rf_evaluations)
        throw std::runtime_error("wave optics RF solve budget exceeded");
    checks.maximum_series_tail_bound = std::max(checks.maximum_series_tail_bound, value.series_tail_bound);
    checks.maximum_series_terms = std::max(checks.maximum_series_terms, value.series_terms);
}

void observe_fir(HfWaveOpticsChecks& checks, const HfFullwaveKernel& result) {
    checks.maximum_discarded_energy_fraction = std::max(checks.maximum_discarded_energy_fraction,
        result.discarded_pre_energy_fraction + result.discarded_tail_energy_fraction);
    checks.maximum_raw_precursor_energy_fraction = std::max(checks.maximum_raw_precursor_energy_fraction,
        result.prearrival_energy_fraction);
}

} // namespace

HfWaveOpticsResponse solve_hf_wave_optics(double frequency, double delta, double x, double y) {
    return GaussianSeries(frequency, delta, y).at(x);
}

void validate_hf_wave_optics_spec(const HfWaveOpticsSpec& spec) {
    if (!std::isfinite(spec.peak_delta_tecu) || std::abs(spec.peak_delta_tecu) > Table::maximum_peak_delta_tecu ||
        !std::isfinite(spec.impact_y_m) || std::abs(spec.impact_y_m) > Table::maximum_impact_m ||
        !std::isfinite(spec.drift_mps) || std::abs(spec.drift_mps) > Table::maximum_drift_mps ||
        !std::isfinite(spec.closest_seconds) || spec.closest_seconds < 0. || spec.closest_seconds > 86400.)
        throw std::invalid_argument("wave optics screen or advection outside supported bounds");
}

HfWaveOpticsTable::HfWaveOpticsTable(const HfWaveOpticsSpec& spec)
    : peak_delta_tecu_(spec.peak_delta_tecu), impact_y_m_(spec.impact_y_m), knots_(position_intervals + 1) {}

std::size_t HfWaveOpticsTable::workspace_bytes() const noexcept {
    return sizeof(*this) + knots_.capacity() * sizeof(Knot);
}

double HfWaveOpticsTable::retarded_peak_error_bound(double drift_mps) const {
    if (!std::isfinite(drift_mps) || std::abs(drift_mps) > maximum_drift_mps)
        throw std::invalid_argument("wave optics drift outside supported bounds");
    return std::abs(drift_mps) * derivative_flight_bound_ / (position_step_m * sample_rate_hz);
}

Complex HfWaveOpticsTable::coefficient_at_position(std::size_t lag, double x) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || !std::isfinite(x))
        throw std::invalid_argument("invalid wave optics lag or displacement");
    const auto [lo, u] = bracket(x);
    const auto j = lag - first_lag_samples();
    return blend(weights(u), knots_[lo].values[j], knots_[lo + 1].values[j],
        knots_[lo].slopes[j], knots_[lo + 1].slopes[j]);
}

Complex HfWaveOpticsTable::coefficient_knot_slope(std::size_t lag, std::size_t position) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || position >= knots_.size())
        throw std::invalid_argument("invalid wave optics lag or position knot");
    return knots_[position].slopes[lag - first_lag_samples()];
}

HfWaveOpticsTableBuild build_hf_wave_optics_table(const HfWaveOpticsSpec& spec, std::stop_token stop) {
    validate_hf_wave_optics_spec(spec);
    cancelled(stop);
    auto table = std::shared_ptr<Table>(new Table(spec));
    HfWaveOpticsChecks checks{};
    checks.far_field_error_bound = far_field_bound(spec.peak_delta_tecu);
    bool good = checks.far_field_error_bound <= far_field_tolerance;
    std::vector<GaussianSeries> series;
    series.reserve(Table::frequency_bins);
    for (std::size_t k = 0; k < Table::frequency_bins; ++k) {
        cancelled(stop);
        series.emplace_back(Table::carrier_hz + frequency_offset(k, Table::frequency_bins),
            spec.peak_delta_tecu, spec.impact_y_m);
    }
    std::array<Complex, Table::frequency_bins> grid{}, derivative_grid{};
    const auto raw_at = [&](double x, double offset) {
        const auto value = solve_hf_wave_optics(Table::carrier_hz + offset,
            spec.peak_delta_tecu, x, spec.impact_y_m);
        observe_series(checks, value);
        return value.rf;
    };
    for (std::size_t p = 0; p <= Table::position_intervals; ++p) {
        cancelled(stop);
        const auto x = static_cast<double>(p) * Table::position_step_m;
        for (std::size_t k = 0; k < grid.size(); ++k) {
            const auto value = series[k].at(x);
            observe_series(checks, value);
            if (k == grid.size() / 2) {
                const double power = std::norm(value.rf);
                checks.minimum_sampled_carrier_power = p == 0 ? power : std::min(checks.minimum_sampled_carrier_power, power);
                checks.maximum_sampled_carrier_power = p == 0 ? power : std::max(checks.maximum_sampled_carrier_power, power);
            }
            grid[k] = p < Table::position_intervals ? value.rf : series[k].ambient();
            derivative_grid[k] = p < Table::position_intervals ?
                value.derivative_per_m * Table::position_step_m : Complex{};
        }
        const auto fir = design_hf_fullwave_kernel(grid, design);
        const auto slope = design_hf_fullwave_kernel(derivative_grid, design);
        observe_fir(checks, fir);
        auto& knot = table->knots_[p];
        std::copy(fir.fir.taps.begin(), fir.fir.taps.end(), knot.values.begin());
        std::copy(slope.fir.taps.begin(), slope.fir.taps.end(), knot.slopes.begin());
        const bool refine = std::find(positions_for_refinement.begin(), positions_for_refinement.end(), p) !=
            positions_for_refinement.end();
        HfFullwaveKernel fine;
        if (refine) {
            std::array<Complex, Table::refinement_bins> dense{};
            for (std::size_t k = 0; k < grid.size(); ++k) {
                dense[2 * k] = grid[k];
                const auto frequency = Table::carrier_hz + frequency_offset(2 * k + 1, dense.size());
                const GaussianSeries odd(frequency, spec.peak_delta_tecu, spec.impact_y_m);
                const auto response = odd.at(x);
                observe_series(checks, response);
                dense[2 * k + 1] = p < Table::position_intervals ? response.rf : odd.ambient();
            }
            fine = design_hf_fullwave_kernel(dense, design);
            observe_fir(checks, fine);
        }
        for (const auto offset : knot_offsets) {
            const auto raw = raw_at(x, offset);
            const auto frequency = offset / Table::sample_rate_hz;
            good &= check_error(checks, checks.worst_frequency_error,
                hf_fullwave_fir_response(fir.fir, frequency), raw, x, offset);
            if (refine)
                good &= check_error(checks, checks.worst_frequency_error,
                    hf_fullwave_fir_response(fine.fir, frequency), raw, x, offset);
        }
    }
    std::array<double, Table::taps> slope_bound{};
    for (std::size_t p = 0; p < Table::position_intervals; ++p) {
        cancelled(stop);
        const auto& lo = table->knots_[p];
        const auto& hi = table->knots_[p + 1];
        for (std::size_t i = 0; i < Table::taps; ++i) {
            // The cubic Hermite derivative is a convex combination of
            // these three derivative Bezier control points.
            const double interior = std::abs(3. * (hi.values[i] - lo.values[i]) - hi.slopes[i] - lo.slopes[i]);
            slope_bound[i] = std::max({slope_bound[i], std::abs(lo.slopes[i]), interior, std::abs(hi.slopes[i])});
        }
        for (const auto fraction : position_fractions) {
            const auto x = (static_cast<double>(p) + fraction) * Table::position_step_m;
            HfFullwaveFir interpolated{Table::first_lag_samples(), std::vector<Complex>(Table::taps)};
            const auto w = weights(fraction);
            for (std::size_t i = 0; i < Table::taps; ++i)
                interpolated.taps[i] = blend(w, lo.values[i], hi.values[i], lo.slopes[i], hi.slopes[i]);
            for (const auto offset : position_offsets)
                good &= check_error(checks, checks.worst_position_error,
                    hf_fullwave_fir_response(interpolated, offset / Table::sample_rate_hz),
                    raw_at(x, offset), x, offset);
        }
    }
    for (std::size_t i = 0; i < Table::taps; ++i)
        table->derivative_flight_bound_ += slope_bound[i] *
            std::abs(static_cast<double>(i + Table::first_lag_samples()) - reference_lag);
    const auto charged = table->workspace_bytes() + series.capacity() * sizeof(GaussianSeries);
    // Reserve ample fixed room for the RF bins and the transient FIR designer.
    if (charged > Table::preparation_workspace_bound_bytes - 64u * 1024u)
        throw std::runtime_error("wave optics table exceeded its charged workspace");
    checks.qualified = good && checks.rf_evaluations == expected_evaluations &&
        checks.maximum_discarded_energy_fraction <= Table::discarded_energy_tolerance &&
        table->retarded_peak_error_bound(spec.drift_mps) <= Table::retarded_peak_tolerance;
    if (!checks.qualified) return {nullptr, checks};
    return {std::move(table), checks};
}

HfWaveOpticsStream::HfWaveOpticsStream(std::shared_ptr<const Table> table, const HfWaveOpticsSpec& spec,
                                     std::uint64_t initial_output_sample)
    : table_(std::move(table)), drift_mps_(spec.drift_mps),
      output_closest_sample_((spec.closest_seconds + Table::outgoing_distance_m / Table::light_speed_mps) *
          Table::sample_rate_hz + Table::modelling_delay_samples), output_sample_(initial_output_sample) {
    validate_hf_wave_optics_spec(spec);
    if (!table_ || table_->peak_delta_tecu_ != spec.peak_delta_tecu || table_->impact_y_m_ != spec.impact_y_m ||
        initial_output_sample >= maximum_exact_sample || table_->retarded_peak_error_bound(drift_mps_) > Table::retarded_peak_tolerance)
        throw std::invalid_argument("wave optics stream requires a matching qualified table, clock and advection");
}

Complex HfWaveOpticsStream::render(Complex input) {
    if (output_sample_ >= maximum_exact_sample) throw std::runtime_error("wave optics stream clock exhausted");
    const double x = (static_cast<double>(output_sample_) - output_closest_sample_) *
        (drift_mps_ / Table::sample_rate_hz);
    history_[cursor_] = input;
    auto index = (cursor_ + history_.size() - Table::first_lag_samples()) % history_.size();
    Complex output{};
    if (std::abs(x) >= Table::maximum_table_position_m || table_->peak_delta_tecu_ == 0.) {
        const auto& ambient = table_->knots_.back().values;
        for (std::size_t i = 0; i < Table::taps; ++i) {
            output += history_[index] * ambient[i];
            index = index != 0 ? index - 1 : history_.size() - 1;
        }
    } else {
        const auto [lo, u] = bracket(x);
        const auto w = weights(u);
        const auto& a = table_->knots_[lo];
        const auto& b = table_->knots_[lo + 1];
        for (std::size_t i = 0; i < Table::taps; ++i) {
            output += history_[index] * blend(w, a.values[i], b.values[i], a.slopes[i], b.slopes[i]);
            index = index != 0 ? index - 1 : history_.size() - 1;
        }
    }
    ++output_sample_;
    if (++cursor_ == history_.size()) cursor_ = 0;
    return output;
}

Complex HfWaveOpticsStream::step(Complex input) {
    if (draining_) throw std::logic_error("wave optics stream is draining");
    return render(input);
}

void HfWaveOpticsStream::process(std::span<const Complex> input, std::span<Complex> output) {
    if (input.size() != output.size() || draining_)
        throw std::invalid_argument("invalid wave optics stream span or state");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = render(input[i]);
}

std::size_t HfWaveOpticsStream::drain(std::span<Complex> output) {
    if (!draining_) {
        draining_ = true;
        remaining_tail_ = tail_samples();
    }
    const auto count = std::min(output.size(), remaining_tail_);
    for (std::size_t i = 0; i < count; ++i) output[i] = render({});
    remaining_tail_ -= count;
    return count;
}

std::size_t HfWaveOpticsStream::workspace_bytes() const noexcept { return sizeof(*this); }

} // namespace appraisal
