#include "ssb_local_fir.hpp"
#include "ssb_display_points.hpp"
#include "ssb_inverse.hpp"
#include "ssb_soft_calibration.hpp"
#include "datapump/execution.hpp"
#include "datapump/fast/modem.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {

std::complex<double> ssb_affine_path(const SsbPulseKernelSample& pulse, std::complex<double> gain,
                                    std::complex<double> slope) noexcept {
    return gain * pulse.value - slope * pulse.tap_moment;
}

std::optional<SsbCenterRange> ssb_disjoint_fit_centers(SsbCenterRange fit,
    std::span<const SsbCenterRange> scored, std::size_t stride, std::size_t fir_radius) {
    if (!stride || fir_radius > std::numeric_limits<std::uint64_t>::max() / 2 ||
        fit.past_last <= fit.first || (fit.past_last - fit.first) % stride) return {};
    // The past-last tick is one stride beyond the last actual center.
    // A pad rounded down to a whole stride therefore leaves a strict
    // distance greater than two radii between the actual edge centers.
    const auto pad = (2 * std::uint64_t(fir_radius) / stride) * stride;
    for (const auto score : scored) {
        if (score.past_last <= score.first || (score.past_last - score.first) % stride ||
            score.first % stride != fit.first % stride) return {};
        if (fit.past_last <= score.first) {
            if (score.first < pad) return {};
            fit.past_last = std::min(fit.past_last, score.first - pad);
        } else if (score.past_last <= fit.first) {
            if (score.past_last > std::numeric_limits<std::uint64_t>::max() - pad) return {};
            fit.first = std::max(fit.first, score.past_last + pad);
        } else return {};
        if (fit.past_last <= fit.first) return {};
    }
    return fit;
}

namespace {
using C = std::complex<double>;
constexpr std::size_t max_taps = 9;
constexpr std::size_t max_local_label_bits = 16;
constexpr std::size_t max_window_centers = 2048;
constexpr std::size_t max_interval_centers = 32768;
constexpr double maximum_clock = ssb_maximum_tracking_clock_ratio;
constexpr double exact_tick_limit = double(std::uint64_t{1} << 52);
constexpr double innovation_variance = .25;
constexpr double pilot_margin = 4.;
constexpr double inverse_margin = 6.;
constexpr std::size_t clock_context_count = 4;
constexpr std::size_t clock_fit_words = 5;
constexpr std::size_t clock_tail_words = 8;
constexpr double clock_trial_limit = ssb_local_clock_coarse_ppm.back() * 1e-6;
using Gains = std::array<C, max_taps>;

bool finite(C value) { return std::isfinite(value.real()) && std::isfinite(value.imag()); }

double power(std::span<const C> values) {
    double sum = 0.;
    for (auto value : values) sum += std::norm(value);
    return sum;
}

C inner(std::span<const C> first, std::span<const C> second) {
    C sum{};
    for (std::size_t i = 0; i < first.size(); ++i) sum += std::conj(first[i]) * second[i];
    return sum;
}

struct Grid {
    double first_delay = 0.;
    std::size_t taps = 0;
    double clock = 0.;
    double offset = 0.;
    double observation_delay = 0.;
    std::shared_ptr<const SsbPulseCache> pulse;
};

struct Knot {
    double tick = 0.;
    Gains gains{};
};

struct State {
    Grid grid;
    Knot knot;
};

struct Bridge {
    Knot left, right;
    Gains slopes{};

    Bridge(Knot first, Knot last) : left(first), right(last) {
        const auto elapsed = right.tick - left.tick;
        if (!std::isfinite(elapsed) || elapsed <= 0.)
            throw std::invalid_argument("Fast SSB FIR gain states must advance in input time");
        for (std::size_t i = 0; i < max_taps; ++i)
            slopes[i] = (right.gains[i] - left.gains[i]) / elapsed;
    }

    Gains at(double tick) const {
        Gains result;
        for (std::size_t i = 0; i < max_taps; ++i)
            result[i] = left.gains[i] + (tick - left.tick) * slopes[i];
        return result;
    }
};

struct Group {
    std::size_t first, pilot, end;
};

struct Frame {
    std::size_t ordinal, first, end;
    std::vector<Group> groups;
};

struct Geometry {
    std::uint64_t reference;
    std::size_t stride, fir_radius;
    double beta, radius, epoch;

    double relative(std::uint64_t center) const noexcept {
        return center >= reference ? double(center - reference) : -double(reference - center);
    }
    double tick(const Grid& grid, double symbol) const {
        return (epoch + grid.observation_delay + (symbol + radius) / beta - grid.offset) / (1. + grid.clock);
    }
    std::uint64_t center(const Grid& grid, double symbol) const {
        const auto x = tick(grid, symbol);
        if (!std::isfinite(x) || std::abs(x) >= exact_tick_limit)
            throw std::length_error("Fast SSB FIR exceeds exact input timing");
        const auto steps = static_cast<std::int64_t>(std::ceil(x / stride));
        const auto distance = std::uint64_t(steps < 0 ? -steps : steps) * stride;
        if (steps < 0) {
            if (distance > reference) throw std::length_error("Fast SSB FIR observation precedes capture");
            return reference - distance;
        }
        if (distance > std::numeric_limits<std::uint64_t>::max() - reference)
            throw std::length_error("Fast SSB FIR input timing overflow");
        return reference + distance;
    }
    double symbol(const Grid& grid, double tick) const noexcept {
        return ((1. + grid.clock) * tick + grid.offset - epoch - grid.first_delay) * beta - radius;
    }
    SsbCenterRange range(const Grid& grid, double first, double end) const {
        return {center(grid, first), center(grid, end)};
    }
};

std::optional<std::pair<double, std::size_t>> delay_grid(const Profile& profile,
    const SsbAcquisitionLock& lock) {
    if (lock.atoms.empty() || lock.atoms.size() > DelayDopplerFit::maximum_atoms || !profile.sample_rate ||
        !std::isfinite(profile.symbol_rate) || profile.symbol_rate <= 0. ||
        !std::isfinite(lock.clock_ratio_error) || std::abs(lock.clock_ratio_error) > maximum_clock)
        return {};
    const auto strongest = std::ranges::max_element(lock.atoms, {}, [](const auto& atom) {
        return std::norm(atom.gain);
    });
    if (!finite(strongest->gain) || !(std::norm(strongest->gain) > 0.) ||
        !std::isfinite(strongest->delay_ticks)) return {};
    double minimum = strongest->delay_ticks, maximum = minimum;
    const auto limit = .025 * std::abs(strongest->gain);
    for (const auto& atom : lock.atoms) {
        if (!finite(atom.gain) || !std::isfinite(atom.delay_ticks) || !std::isfinite(atom.frequency_hz) ||
            std::abs(atom.delay_ticks) > .025 * profile.sample_rate)
            return {};
        if (std::abs(atom.gain) < limit) continue;
        minimum = std::min(minimum, atom.delay_ticks);
        maximum = std::max(maximum, atom.delay_ticks);
    }
    const auto step = .5 * profile.sample_rate / profile.symbol_rate;
    const auto first = std::floor((minimum - strongest->delay_ticks) / step);
    const auto last = std::ceil((maximum - strongest->delay_ticks) / step);
    if (last - first + 1 > max_taps) return {};
    return {{strongest->delay_ticks + first * step, static_cast<std::size_t>(last - first + 1)}};
}

std::optional<std::span<const C>> observed(SsbAcquisitionView view, SsbCenterRange range, std::size_t stride) {
    if (view.stride != stride || range.first < view.first_center_input_tick || range.past_last <= range.first)
        return {};
    const auto first = range.first - view.first_center_input_tick;
    const auto last = range.past_last - view.first_center_input_tick;
    if (first % stride || last % stride || last / stride > view.samples.size()) return {};
    return view.samples.subspan(first / stride, (last - first) / stride);
}

// All taps in a row share a half-symbol lattice. A symbol at column k
// through tap a uses lattice ordinate 2*k+a.
struct Basis {
    std::span<const C> samples;
    std::size_t first_symbol, columns, taps, lattice;
    double first_tick, stride;
    std::vector<SsbPulseKernelValue> rows;

    SsbPulseKernelValue at(std::size_t row, std::size_t column, std::size_t tap) const noexcept {
        return rows[row * lattice + 2 * column + tap];
    }
    double tick(std::size_t row) const noexcept { return first_tick + stride * row; }
    std::size_t workspace() const noexcept { return rows.capacity() * sizeof(SsbPulseKernelValue); }
};

std::optional<Basis> make_basis(const Geometry& geometry, const Grid& grid, SsbAcquisitionView view,
    SsbCenterRange range, std::stop_token stop) {
    const auto samples = observed(view, range, geometry.stride);
    if (!samples) return {};
    if (samples->size() > max_window_centers)
        throw std::length_error("Fast SSB FIR observation window exceeds its native bound");
    const auto first_tick = geometry.relative(range.first);
    const auto first_center = geometry.symbol(grid, first_tick);
    const auto last_center = geometry.symbol(grid, first_tick + (samples->size() - 1) * geometry.stride);
    const auto support = grid.pulse->support_symbols();
    const auto first_symbol = std::max(0., std::ceil(first_center - .5 * (grid.taps - 1) - support));
    const auto last_symbol = std::floor(last_center + support) + 1.;
    if (last_symbol <= first_symbol || last_symbol > exact_tick_limit ||
        last_symbol - first_symbol > SsbChannelModel::maximum_row_symbols)
        throw std::length_error("Fast SSB FIR symbol aperture exceeds its native bound");
    const auto columns = static_cast<std::size_t>(last_symbol - first_symbol);
    Basis result{*samples, static_cast<std::size_t>(first_symbol), columns, grid.taps,
        2 * (columns - 1) + grid.taps, first_tick, double(geometry.stride), {}};
    result.rows.resize(samples->size() * result.lattice);
    for (std::size_t row = 0; row < samples->size(); ++row) {
        if (row % 16 == 0 && stop.stop_requested()) return {};
        const auto x = geometry.symbol(grid, result.tick(row)) - first_symbol;
        grid.pulse->half_lattice_values(x, std::span(result.rows).subspan(row * result.lattice, result.lattice));
    }
    return result;
}

struct Labels {
    std::vector<std::optional<C>> known;
    std::vector<int> unknown;
    int unknown_index(std::size_t column) const noexcept { return unknown[column]; }
    std::size_t count = 0;
};

template<class Known>
Labels make_labels(const Basis& basis, const Known& known) {
    Labels result;
    result.known.reserve(basis.columns);
    result.unknown.reserve(basis.columns);
    for (std::size_t i = 0; i < basis.columns; ++i) {
        auto value = known(basis.first_symbol + i);
        result.known.push_back(value);
        result.unknown.push_back(value ? -1 : static_cast<int>(result.count++));
    }
    return result;
}

bool accumulate_packed_gram(HermitianBand& factor, std::span<const C> columns,
    std::size_t rows, std::size_t count, std::stop_token stop, std::size_t& scratch_bytes) {
    constexpr auto limit = SsbChannelModel::maximum_row_symbols;
    scratch_bytes = 0;
    if (count > limit || stop.stop_requested()) return false;
    if (!count) return columns.empty();
    if (columns.size() % count || columns.size() / count != rows) return false;

    const auto triangle = count * (count + 1) / 2;
    std::vector<double> packed(2 * triangle);
    auto* const real = packed.data();
    auto* const imag = real + triangle;
    std::array<double, limit> input_real, input_imag;
    scratch_bytes = packed.capacity() * sizeof(double) + sizeof(input_real) + sizeof(input_imag);
    // Separate real and imaginary lanes allow vectorization across columns.
    // Each entry still accumulates the input rows in their original order.
    for (std::size_t row = 0; row < rows; ++row) {
        if (row % 16 == 0 && stop.stop_requested()) return false;
        const auto* input = columns.data() + row * count;
        for (std::size_t i = 0; i < count; ++i) {
            input_real[i] = input[i].real();
            input_imag[i] = input[i].imag();
            if (!finite(input[i])) return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto ar = input_real[i], ai = input_imag[i];
            if (ar == 0. && ai == 0.) continue;
            const auto offset = i * (i + 1) / 2;
            for (std::size_t j = 0; j <= i; ++j) {
                real[offset + j] += ar * input_real[j] + ai * input_imag[j];
                imag[offset + j] += ar * input_imag[j] - ai * input_real[j];
            }
        }
    }

    factor.reset(count, count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 16 == 0 && stop.stop_requested()) return false;
        auto lower = factor.lower_row(i);
        const auto offset = i * (i + 1) / 2;
        for (std::size_t j = 0; j <= i; ++j) {
            const C entry{real[offset + j], imag[offset + j]};
            if (!finite(entry)) return false;
            lower[j] = entry;
        }
    }
    return !stop.stop_requested();
}

struct Projection {
    HermitianBand factor;
    std::vector<C> columns;
    std::size_t rows = 0, count = 0;
    double ridge = 0.;
    std::size_t gram_scratch = 0;

    Projection(std::size_t row_count, std::size_t column_count, double regularizer)
        : columns(row_count * column_count), rows(row_count), count(column_count), ridge(regularizer) {}

    bool prepare(std::stop_token stop) {
        if (!count) return !stop.stop_requested();
        return accumulate_packed_gram(factor, columns, rows, count, stop, gram_scratch) &&
            factor.factor(ridge, stop) == HermitianBand::FactorResult::complete;
    }

    std::optional<std::vector<C>> solve(std::span<const C> y, std::stop_token stop) const {
        std::vector<C> rhs(count);
        if (!count) return rhs;
        for (std::size_t row = 0; row < rows; ++row) {
            const auto* input = columns.data() + row * count;
            for (std::size_t j = 0; j < count; ++j) rhs[j] += std::conj(input[j]) * y[row];
        }
        if (!factor.solve_in_place(rhs, stop)) return {};
        return rhs;
    }

    std::optional<std::vector<C>> eliminate(std::span<const C> y, std::stop_token stop) const {
        const auto coefficients = solve(y, stop);
        if (!coefficients) return {};
        std::vector<C> result(y.begin(), y.end());
        result.reserve(rows + count);
        if (count) for (std::size_t row = 0; row < rows; ++row) {
            const auto* input = columns.data() + row * count;
            for (std::size_t j = 0; j < count; ++j) result[row] -= input[j] * (*coefficients)[j];
        }
        const auto weight = -std::sqrt(ridge);
        for (auto value : *coefficients) result.push_back(weight * value);
        return result;
    }

    std::size_t workspace() const noexcept {
        return factor.workspace_bytes() + columns.capacity() * sizeof(C) + gram_scratch;
    }
};

C coefficient(const Basis& basis, std::size_t row, std::size_t column, const Gains& gain, const Gains& slope) {
    C result{};
    for (std::size_t a = 0; a < basis.taps; ++a) {
        if (gain[a] == C{} && slope[a] == C{}) continue;
        const auto term = basis.at(row, column, a);
        result += ssb_affine_path({term.value, {}, term.moment, {}}, gain[a], slope[a]);
    }
    return result;
}

struct FitResult {
    Knot knot;
    std::size_t workspace = 0;
};

// The same affine law is used at every underlying FIR tap in the word's
// aperture. Moving the right endpoint changes a coefficient by
// alpha(t)*K0 - K1/(right.tick-left.tick).
std::optional<FitResult> fit_word(const Basis& basis, const Labels& labels, Knot prior, double center,
    double ridge, std::stop_token stop) {
    auto estimate = prior;
    estimate.tick = center;
    const auto elapsed = center - prior.tick;
    if (!(elapsed > 0.)) return {};
    const auto inverse_elapsed = 1. / elapsed;
    std::vector<C> y(basis.samples.begin(), basis.samples.end());
    std::array<std::vector<C>, max_taps> shapes;
    for (std::size_t a = 0; a < basis.taps; ++a) shapes[a].resize(y.size());
    for (std::size_t r = 0; r < y.size(); ++r) {
        if (r % 16 == 0 && stop.stop_requested()) return {};
        const auto alpha = (basis.tick(r) - prior.tick) * inverse_elapsed;
        for (std::size_t k = 0; k < basis.columns; ++k) if (const auto label = labels.known[k]) {
            for (std::size_t a = 0; a < basis.taps; ++a) {
                const auto term = basis.at(r, k, a);
                y[r] -= (*label * prior.gains[a]) * term.value;
                shapes[a][r] += *label * (alpha * term.value - inverse_elapsed * term.moment);
            }
        }
    }
    std::size_t workspace = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        Projection projection(y.size(), labels.count, ridge);
        const Bridge nuisance(prior, estimate);
        for (std::size_t r = 0; r < y.size(); ++r) {
            if (r % 16 == 0 && stop.stop_requested()) return {};
            const auto gain = nuisance.at(basis.tick(r));
            for (std::size_t k = 0; k < basis.columns; ++k)
                if (const auto j = labels.unknown_index(k); j >= 0)
                    projection.columns[r * labels.count + j] = coefficient(basis, r, k, gain, nuisance.slopes);
        }
        if (!projection.prepare(stop)) return {};
        const auto target = projection.eliminate(y, stop);
        if (!target) return {};
        std::array<std::vector<C>, max_taps> features;
        for (std::size_t a = 0; a < basis.taps; ++a) {
            auto projected = projection.eliminate(shapes[a], stop);
            if (!projected) return {};
            features[a] = std::move(*projected);
        }
        HermitianBand fit;
        fit.reset(basis.taps, basis.taps - 1);
        std::vector<C> correction(basis.taps);
        for (std::size_t a = 0; a < basis.taps; ++a) {
            auto lower = fit.lower_row(a);
            for (std::size_t b = 0; b <= a; ++b) lower[b] = inner(features[a], features[b]);
            correction[a] = inner(features[a], *target);
        }
        if (fit.factor(ridge / innovation_variance, stop) != HermitianBand::FactorResult::complete ||
            !fit.solve_in_place(correction, stop)) return {};
        for (std::size_t a = 0; a < basis.taps; ++a) {
            if (!finite(correction[a])) return {};
            estimate.gains[a] = prior.gains[a] + correction[a];
        }
        workspace = std::max(workspace, projection.workspace() + fit.workspace_bytes() +
            sizeof(C) * ((1 + basis.taps) * y.size() + (1 + basis.taps) * target->size()));
    }
    return FitResult{estimate, workspace + basis.workspace()};
}

struct WordScore {
    SsbQuietUnitDiagnostics::Group measures;
    C cross{};
    double error = 0.;
    bool present = false;
};

template<class Public>
std::optional<WordScore> score_word(const Basis& basis, const Labels& labels, const Bridge& bridge,
    const Public& public_word, double ridge, double noise, std::stop_token stop, std::size_t& workspace) {
    Projection projection(basis.samples.size(), labels.count, ridge);
    std::vector<C> y(basis.samples.begin(), basis.samples.end());
    std::vector<C> expected(y.size());
    for (std::size_t r = 0; r < y.size(); ++r) {
        if (r % 16 == 0 && stop.stop_requested()) return {};
        const auto gain = bridge.at(basis.tick(r));
        for (std::size_t k = 0; k < basis.columns; ++k) {
            const auto value = coefficient(basis, r, k, gain, bridge.slopes);
            if (const auto symbol = public_word(basis.first_symbol + k)) expected[r] += value * *symbol;
            else if (const auto label = labels.known[k]) y[r] -= value * *label;
            else projection.columns[r * labels.count + labels.unknown_index(k)] = value;
        }
    }
    if (!projection.prepare(stop)) return {};
    const auto observed = projection.eliminate(y, stop);
    const auto predicted = projection.eliminate(expected, stop);
    if (!observed || !predicted) return {};
    WordScore result;
    auto& m = result.measures;
    m.centers = y.size();
    m.raw_template_energy = power(expected);
    m.projected_template_energy = power(*predicted);
    m.projected_input_energy = power(*observed);
    const auto cross = result.cross = inner(*predicted, *observed);
    m.cross_squared = std::norm(cross);
    m.informative = m.projected_template_energy > 0. &&
        m.projected_template_energy >= .05 * m.raw_template_energy;
    if (m.informative && m.projected_input_energy > 0.) {
        m.squared_coherence = std::clamp(m.cross_squared /
            (m.projected_template_energy * m.projected_input_energy), 0., 1.);
        m.amplitude = std::abs(cross) / m.projected_template_energy;
        const auto observed_power = m.projected_input_energy / m.centers;
        m.strong = m.squared_coherence >= .80 && m.amplitude >= .10 && observed_power > noise;
        result.present = m.squared_coherence >= .40 && m.amplitude >= .15 && m.amplitude <= 3. &&
            observed_power > 8. * noise;
    }
    for (std::size_t i = 0; i < observed->size(); ++i)
        result.error += std::norm((*observed)[i] - (*predicted)[i]);
    workspace = std::max(workspace, projection.workspace() + basis.workspace() +
        (y.size() + expected.size() + observed->size() + predicted->size()) * sizeof(C));
    return result;
}

struct DemapResult {
    double pilot_error = 0., pilot_variance = 0.;
    std::size_t pilots = 0, workspace = 0;
};

std::optional<DemapResult> demap_group(const Basis& basis, const Labels& labels, const Bridge& bridge,
    const Frame& frame, const Group& group, const Profile& profile, const SingleCarrierSchedule& schedule,
    double ridge, double calibration, std::span<float, physical_interval_bits> output, std::stop_token stop,
    SsbDisplayPoints* display) {
    Projection projection(basis.samples.size(), labels.count, ridge);
    std::vector<C> y(basis.samples.begin(), basis.samples.end());
    for (std::size_t row = 0; row < y.size(); ++row) {
        if (row % 16 == 0 && stop.stop_requested()) return {};
        const auto gain = bridge.at(basis.tick(row));
        for (std::size_t k = 0; k < basis.columns; ++k) {
            const auto value = coefficient(basis, row, k, gain, bridge.slopes);
            if (const auto label = labels.known[k]) y[row] -= value * *label;
            else projection.columns[row * labels.count + labels.unknown_index(k)] = value;
        }
    }
    if (!projection.prepare(stop)) return {};
    const auto mean = projection.solve(y, stop);
    if (!mean || !labels.count) return {};
    std::vector<C> inverse_column(labels.count);
    DemapResult result;
    for (auto symbol = group.first; symbol < group.end; ++symbol) {
        if (symbol < basis.first_symbol || symbol - basis.first_symbol >= basis.columns) return {};
        const auto index = labels.unknown_index(symbol - basis.first_symbol);
        if (index < 0) continue;
        const auto position = schedule.position(frame.ordinal, symbol - frame.first);
        std::fill(inverse_column.begin(), inverse_column.end(), C{});
        inverse_column[index] = 1.;
        if (!projection.factor.solve_in_place(inverse_column, stop)) return {};
        const auto diagonal = ridge * inverse_column[index].real();
        if (!std::isfinite(diagonal) || diagonal <= 0. || diagonal > 1. + 1e-9) return {};
        const auto fraction = std::clamp(diagonal, 1e-5, .995);
        const auto self = 1. - fraction;
        const auto variance = fraction / self;
        const auto estimate = (*mean)[index] / self;
        if (!finite(estimate)) return {};
        if (position.kind == SingleCarrierSchedule::Kind::pilot) {
            result.pilot_error += std::norm(estimate - position.known_value);
            result.pilot_variance += variance;
            ++result.pilots;
        } else if (position.kind == SingleCarrierSchedule::Kind::data && variance < 10.) {
            std::array<double, max_local_label_bits> metrics{};
            if (schedule.bits_per_symbol > metrics.size() ||
                position.bit_offset + position.bit_count > output.size()) return {};
            if (display) display->record(position.bit_offset / schedule.bits_per_symbol, estimate);
            square_qam_soft_demodulate(profile.constellation, estimate,
                std::span(metrics).first(schedule.bits_per_symbol));
            for (unsigned bit = 0; bit < position.bit_count; ++bit)
                output[position.bit_offset + bit] = static_cast<float>(
                    std::clamp(metrics[bit] / (variance + calibration), -24., 24.));
        }
    }
    result.workspace = basis.workspace() + projection.workspace() +
        (y.capacity() + mean->capacity() + inverse_column.capacity()) * sizeof(C);
    return result;
}

bool training_pilot(std::size_t index) noexcept { return (index + 1) % 4 != 0; }

struct SavedWindow {
    std::size_t ordinal;
    std::size_t first_group;
    std::uint64_t first_center;
    std::vector<C> samples;
    Grid carried_grid;
    std::array<SsbCenterRange, clock_fit_words> fit{};
    std::array<SsbCenterRange, clock_fit_words - 1> score{};

    SsbAcquisitionView view(std::size_t stride) const { return {first_center, stride, samples}; }
};

struct ClockCapture {
    std::uint64_t wire_cycle;
    Grid base;
    std::array<std::optional<SavedWindow>, clock_context_count> windows{};
    std::optional<SavedWindow> tail{};

    bool complete() const {
        return tail && std::ranges::all_of(windows, [](const auto& item) { return item.has_value(); });
    }
};

double clock_observation_guard(const SingleCarrierSchedule& schedule, std::size_t cycle_intervals) {
    if (cycle_intervals < 8 || (schedule.data_symbols + schedule.pilot_stride - 1) / schedule.pilot_stride < 24)
        return 0.;
    const auto period = double(schedule.pilot_count + schedule.pilot_stride);
    const auto horizon = double(cycle_intervals) * schedule.interval_symbols(0) + 2. * period;
    const auto clock_slip = clock_trial_limit * horizon / (1. - maximum_clock);
    // Reserve history on both sides of the permitted rate and phase movement.
    return std::ceil((.5 * period + clock_slip) / (1. - maximum_clock)) + 2.;
}

constexpr std::array<std::size_t, 2> routing_words{3, 7};
constexpr std::size_t routing_fit_groups = 9;

struct RoutingAperture {
    SsbCenterRange all{};
    std::array<std::optional<SsbCenterRange>, routing_fit_groups> fit;
    std::array<SsbCenterRange, routing_words.size()> score;
};

std::vector<std::size_t> periodic_indices(std::size_t size) {
    std::vector<std::size_t> indices;
    for (unsigned i = 0; i < 8; ++i) {
        const auto index = i * size / 8;
        if (indices.empty() || index > indices.back()) indices.push_back(index);
    }
    return indices;
}

} // namespace

struct SsbLocalFirProposal {
    const void* owner = nullptr;
    std::uint64_t revision = 0;
    std::uint64_t ordinal = 0;
    State state;
    double calibration = .02;
};

SsbLocalFirEvidence::SsbLocalFirEvidence() = default;
SsbLocalFirEvidence::~SsbLocalFirEvidence() = default;
SsbLocalFirEvidence::SsbLocalFirEvidence(SsbLocalFirEvidence&&) noexcept = default;
SsbLocalFirEvidence& SsbLocalFirEvidence::operator=(SsbLocalFirEvidence&&) noexcept = default;

struct SsbLocalFir::Impl {
    Profile profile;
    SingleCarrierSchedule schedule;
    Geometry geometry;
    SsbPulse pulse;
    Grid nominal;
    State committed;
    std::vector<SsbAcquisitionAtom> acquisition;
    double signal_power = 0., ridge = 0., calibration = .02;
    std::size_t observation_bound = 0;
    std::size_t cycle_intervals = 0, protected_bits = 0;
    std::uint64_t revision = 0;
    std::optional<std::uint64_t> last_present;
    std::optional<ClockCapture> feedback;

    Impl(const Profile& p, const SsbFrontendGeometry& frontend, const SsbAcquisitionLock& lock,
        std::pair<double, std::size_t> support, std::stop_token stop)
        : profile(p), schedule(p),
          geometry{lock.reference_input_tick, frontend.decimation, frontend.radius(), p.symbol_rate / p.sample_rate,
              single_carrier_pulse_radius(p), lock.epoch_from_reference_ticks},
          pulse(p, frontend),
          nominal{support.first, support.second, lock.clock_ratio_error, 0., 0.,
              std::make_shared<const SsbPulseCache>(pulse.make_cache(lock.clock_ratio_error, 0., stop))},
          committed{nominal, {}}, acquisition(lock.atoms),
          cycle_intervals(fast::cycle_intervals(p)), protected_bits(capacity_coded_bits(p)) {
        if (!geometry.stride || geometry.reference < frontend.radius() ||
            (geometry.reference - frontend.radius()) % geometry.stride ||
            !std::isfinite(geometry.epoch) || std::abs(geometry.epoch) > 2. * p.sample_rate ||
            !std::isfinite(lock.diagnostics.public_holdout_nmse) || lock.diagnostics.public_holdout_nmse < 0.)
            throw std::invalid_argument("Invalid Fast SSB FIR lock coordinates");
        const auto strongest = std::ranges::max_element(acquisition, {}, [](const auto& atom) {
            return std::norm(atom.gain);
        });
        nominal.observation_delay = strongest->delay_ticks;
        committed.grid.observation_delay = nominal.observation_delay;
        for (const auto& atom : acquisition) signal_power += std::norm(atom.gain) * p.amplitude * p.amplitude;
        if (!(signal_power > 0.) || !std::isfinite(signal_power))
            throw std::invalid_argument("Invalid Fast SSB FIR signal calibration");
        const auto centers_per_symbol = 1. / (geometry.beta * geometry.stride);
        ridge = signal_power * std::max(.001, centers_per_symbol * lock.diagnostics.public_holdout_nmse);
        const auto lookahead = schedule.marker_span + schedule.pilot_stride + schedule.pilot_count;
        const auto guard = clock_observation_guard(schedule, cycle_intervals);
        const auto bound = std::ceil((schedule.interval_symbols(0) + lookahead + 2 * schedule.pilot_count + 24. +
            2. * (guard + pilot_margin)) *
            centers_per_symbol / (1. - maximum_clock)) + 2.;
        if (!std::isfinite(bound) || bound > max_interval_centers)
            throw std::length_error("Fast SSB FIR interval exceeds its native observation bound");
        observation_bound = static_cast<std::size_t>(bound);
    }

    Frame frame(std::uint64_t ordinal) const {
        if (ordinal >= std::numeric_limits<std::size_t>::max())
            throw std::length_error("Fast SSB FIR interval ordinal exceeds local geometry");
        const auto first = schedule.total_interval_symbols(static_cast<std::size_t>(ordinal));
        const auto end = schedule.total_interval_symbols(static_cast<std::size_t>(ordinal) + 1);
        if (end > std::numeric_limits<std::size_t>::max() - schedule.training_count - schedule.pilot_count)
            throw std::length_error("Fast SSB FIR interval position exceeds local geometry");
        Frame out{static_cast<std::size_t>(ordinal), schedule.training_count + first,
            schedule.training_count + end, {}};
        auto position = out.first + schedule.marker_symbols(out.ordinal);
        for (auto left = schedule.data_symbols; left;) {
            const auto n = std::min(left, schedule.pilot_stride);
            out.groups.push_back({position, position + n, position + n + schedule.pilot_count});
            left -= n;
            position = out.groups.back().end;
        }
        if (position != out.end || out.groups.empty())
            throw std::logic_error("Fast SSB FIR interval disagrees with the public schedule");
        return out;
    }

    std::optional<C> startup(std::size_t symbol) const {
        if (symbol < schedule.training_count) return schedule.training_symbol(symbol);
        if (symbol - schedule.training_count < schedule.marker_symbols(0))
            return schedule.position(0, symbol - schedule.training_count).known_value;
        return {};
    }

    std::optional<C> training(const Frame& f, const Group& group, std::size_t symbol) const {
        if (auto initial = startup(symbol)) return initial;
        if (symbol < group.pilot || symbol >= group.end || !training_pilot(symbol - group.pilot)) return {};
        return schedule.position(f.ordinal, symbol - f.first).known_value;
    }

    std::optional<C> training_in_frame(const Frame& f, std::size_t symbol) const {
        if (symbol < f.first || symbol >= f.end) return {};
        const auto it = std::ranges::upper_bound(f.groups, symbol, {}, &Group::first);
        if (it == f.groups.begin()) return {};
        const auto& group = *(it - 1);
        return training(f, group, symbol);
    }

    SsbCenterRange required(const Grid& grid, const Frame& f) const {
        const auto future = frame(f.ordinal + 1);
        const auto pre = f.ordinal ? inverse_margin : schedule.pilot_count + pilot_margin;
        auto range = geometry.range(grid, f.first - pre, future.groups.front().end + pilot_margin);
        if (cycle_intervals && f.ordinal >= cycle_intervals && f.ordinal % cycle_intervals == cycle_intervals - 1 &&
            f.groups.size() >= 24) {
            const auto guarded = recurring_tail_range(grid, f);
            range.first = std::min(range.first, guarded.first);
            range.past_last = std::max(range.past_last, guarded.past_last);
        }
        return range;
    }

    SsbCenterRange seed_range() const {
        const auto last = schedule.training_count + schedule.marker_symbols(0);
        return geometry.range(nominal, last - schedule.pilot_count - pilot_margin, last + pilot_margin);
    }

    RoutingAperture routing_aperture(const Grid& grid, const Frame& f) const {
        RoutingAperture out;
        if (f.groups.size() < routing_fit_groups) return out;
        auto common = grid;
        common.observation_delay = 0.;
        for (std::size_t i = 0; i < routing_words.size(); ++i) {
            const auto& group = f.groups[routing_words[i]];
            out.score[i] = geometry.range(common, group.pilot, group.end);
        }
        out.all = {out.score.front().first, out.score.back().past_last};
        const auto include = [&](SsbCenterRange range) {
            out.all.first = std::min(out.all.first, range.first);
            out.all.past_last = std::max(out.all.past_last, range.past_last);
        };
        if (!f.ordinal) {
            const auto seed = seed_range();
            const auto separated = ssb_disjoint_fit_centers(seed, out.score, geometry.stride, geometry.fir_radius);
            if (!separated || separated->first != seed.first || separated->past_last != seed.past_last) return {};
            include(seed);
        }
        for (std::size_t i = 0; i < out.fit.size(); ++i) {
            if (std::ranges::find(routing_words, i) != routing_words.end()) continue;
            const auto& group = f.groups[i];
            const auto raw = geometry.range(grid, group.pilot - pilot_margin, group.end + pilot_margin);
            out.fit[i] = ssb_disjoint_fit_centers(raw, out.score, geometry.stride, geometry.fir_radius);
            if (out.fit[i]) include(*out.fit[i]);
        }
        return out;
    }

    std::optional<Knot> seed(SsbAcquisitionView view, std::stop_token stop, std::size_t& workspace) const {
        const auto last = schedule.training_count + schedule.marker_symbols(0);
        const auto first = last - schedule.pilot_count;
        const auto center = geometry.tick(nominal, .5 * (first + last - 1));
        Knot prior;
        prior.tick = center - (schedule.pilot_count + schedule.pilot_stride) / geometry.beta;
        const auto step = .5 / geometry.beta;
        const auto tau = 2. * std::numbers::pi;
        for (const auto& atom : acquisition) {
            const auto index = std::llround((atom.delay_ticks - nominal.first_delay) / step);
            if (index < 0 || index >= static_cast<long long>(nominal.taps)) continue;
            prior.gains[index] += atom.gain * std::polar(1., tau * std::remainder(
                atom.frequency_hz * prior.tick / profile.sample_rate, 1.));
        }
        const auto basis = make_basis(geometry, nominal, view, seed_range(), stop);
        if (!basis) return {};
        const auto labels = make_labels(*basis, [&](std::size_t symbol) { return startup(symbol); });
        const auto result = fit_word(*basis, labels, prior, center, ridge, stop);
        if (!result) return {};
        workspace = std::max(workspace, result->workspace);
        return result->knot;
    }

    std::optional<Knot> fit_pilot(const Grid& grid, SsbAcquisitionView view, const Frame& f,
        const Group& group, Knot prior, std::stop_token stop, std::size_t& workspace,
        std::optional<SsbCenterRange> actual_rows = {}) const {
        const auto basis = make_basis(geometry, grid, view,
            actual_rows.value_or(geometry.range(grid, group.pilot - pilot_margin, group.end + pilot_margin)), stop);
        if (!basis) return {};
        const auto labels = make_labels(*basis, [&](std::size_t symbol) { return training(f, group, symbol); });
        const auto center = geometry.tick(grid, .5 * (group.pilot + group.end - 1));
        const auto result = fit_word(*basis, labels, prior, center, ridge, stop);
        if (!result) return {};
        workspace = std::max(workspace, result->workspace);
        return result->knot;
    }

    std::optional<WordScore> public_word(const Grid& grid, SsbAcquisitionView view, const Frame& f,
        std::size_t first_symbol, std::size_t last_symbol, SsbCenterRange rows, const Bridge& bridge,
        double noise, std::stop_token stop, std::size_t& workspace) const {
        const auto basis = make_basis(geometry, grid, view, rows, stop);
        if (!basis) return {};
        const auto symbol = [&](std::size_t k) -> std::optional<C> {
            if (k < first_symbol || k >= last_symbol) return {};
            return schedule.position(f.ordinal, k - f.first).known_value;
        };
        const auto known = make_labels(*basis, [&](std::size_t k) -> std::optional<C> {
            if (const auto word = symbol(k)) return word;
            return startup(k);
        });
        return score_word(*basis, known, bridge, symbol, ridge, noise, stop, workspace);
    }

    std::optional<WordScore> predict(const Grid& grid, SsbAcquisitionView view, const Frame& f,
        const Group& group, Knot prior, double noise, std::stop_token stop, std::size_t& workspace) const {
        auto same = prior;
        same.tick += 1.;
        return public_word(grid, view, f, group.pilot, group.end, geometry.range(grid, group.pilot, group.end),
            Bridge(prior, same), noise, stop, workspace);
    }

    struct ScoreSum {
        double error = 0., power = 0.;
        std::size_t present_words = 0;
        double nmse() const noexcept { return error / std::max(1e-15, power); }
    };

    std::optional<std::vector<Knot>> heldout_fit(const State& state, SsbAcquisitionView view, const Frame& f,
        std::span<const std::size_t> withheld, std::size_t last, std::stop_token stop, std::size_t& workspace) const {
        std::vector<Knot> states{state.knot};
        for (std::size_t i = 0; i < last; ++i) {
            if (std::ranges::find(withheld, i) != withheld.end()) continue;
            const auto next = fit_pilot(state.grid, view, f, f.groups[i], states.back(), stop, workspace);
            if (!next) return {};
            states.push_back(*next);
        }
        return states;
    }

    std::optional<ScoreSum> heldout_score(const Grid& grid, const Grid& raw_grid, SsbAcquisitionView view,
        const Frame& f, std::span<const Knot> states, std::span<const std::size_t> words,
        std::stop_token stop, std::size_t& workspace, std::span<const SsbCenterRange> actual_rows = {}) const {
        ScoreSum sum;
        if (!actual_rows.empty() && actual_rows.size() != words.size()) return {};
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto index = words[i];
            const auto& group = f.groups[index];
            const auto tick = geometry.tick(grid, .5 * (group.pilot + group.end - 1));
            const auto right = std::ranges::lower_bound(states, tick, {}, &Knot::tick);
            if (right == states.begin() || right == states.end()) return {};
            const auto rows = actual_rows.empty() ? geometry.range(raw_grid, group.pilot, group.end) : actual_rows[i];
            const auto score = public_word(grid, view, f, group.pilot, group.end, rows,
                Bridge(*(right - 1), *right), 0., stop, workspace);
            if (!score || !score->measures.informative) return {};
            sum.error += score->error;
            sum.power += score->measures.projected_input_energy;
            sum.present_words += score->present;
        }
        if (!std::isfinite(sum.error) || !(sum.power > 0.)) return {};
        return sum;
    }

    std::optional<State> support_trial(const State& checkpoint, SsbAcquisitionView view, const Frame& f,
        std::stop_token stop, std::size_t& workspace) const {
        const auto& grid = checkpoint.grid;
        if (grid.taps < 2 || f.groups.size() < 32 || !schedule.marker_cadence ||
            f.ordinal < 2 * schedule.marker_cadence || f.ordinal % schedule.marker_cadence)
            return {};
        const auto all = std::span(checkpoint.knot.gains).first(grid.taps);
        const auto energy = power(all);
        if (!(energy > 1e-15)) return {};
        const auto left = std::norm(all.front()) / energy, right = std::norm(all.back()) / energy;
        const int shift = left >= .10 && right <= .01 ? -1 : right >= .10 && left <= .01 ? 1 : 0;
        if (!shift) return {};
        auto translated = checkpoint;
        translated.grid.first_delay += shift * .5 / geometry.beta;
        translated.knot.gains.fill(C{});
        for (std::size_t a = 0; a < grid.taps; ++a) {
            const auto old = static_cast<int>(a) + shift;
            if (old >= 0 && old < static_cast<int>(grid.taps)) translated.knot.gains[a] = all[old];
        }
        const auto n = f.groups.size();
        const std::array<std::size_t, 4> selection{n / 16 - 1, n / 8 - 1, 3 * n / 16 - 1, n / 4 - 1};
        const std::array<std::size_t, 4> validation{3 * n / 8 - 1, n / 2 - 1, 3 * n / 4 - 1, 7 * n / 8 - 1};
        std::array<std::size_t, 8> withheld;
        std::ranges::copy(selection, withheld.begin());
        std::ranges::copy(validation, withheld.begin() + selection.size());
        const auto compare = [&](const State& state, std::size_t end, auto words) {
            const auto knots = heldout_fit(state, view, f, withheld, end, stop, workspace);
            return knots ? heldout_score(state.grid, grid, view, f, *knots, words, stop, workspace) : std::nullopt;
        };
        const auto baseline = compare(checkpoint, selection.back() + 2, std::span(selection));
        const auto candidate = compare(translated, selection.back() + 2, std::span(selection));
        if (!baseline || !candidate || candidate->nmse() >= baseline->nmse()) return {};
        const auto control = heldout_fit(checkpoint, view, f, withheld, n, stop, workspace);
        const auto moved = heldout_fit(translated, view, f, withheld, n, stop, workspace);
        if (!control || !moved) return {};
        const auto control_score = heldout_score(grid, grid, view, f, *control, validation, stop, workspace);
        const auto moved_score = heldout_score(translated.grid, grid, view, f, *moved, validation, stop, workspace);
        if (!control_score || !moved_score || moved_score->nmse() >= control_score->nmse()) return {};
        translated.knot = moved->back();
        return translated;
    }

    std::optional<std::pair<std::size_t, std::size_t>> clock_window(const Frame& f) const {
        if (cycle_intervals < 8) return {};
        const auto relative = f.ordinal % cycle_intervals;
        const std::array<std::size_t, 4> ordinals{
            cycle_intervals / 8 - 1, 3 * cycle_intervals / 8 - 1, 5 * cycle_intervals / 8 - 1, cycle_intervals - 1};
        const auto found = std::ranges::find(ordinals, relative);
        if (found == ordinals.end()) return {};
        const auto index = static_cast<std::size_t>(found - ordinals.begin());
        const auto n = f.groups.size();
        if (n < 24) return {};
        if (relative != ordinals.back()) return {{index, n / 4 - 3}};
        const auto first_bit = relative * physical_interval_bits;
        if (first_bit >= protected_bits) return {};
        const auto bits = std::min<std::size_t>(physical_interval_bits, protected_bits - first_bit);
        const auto complete = bits / (schedule.pilot_stride * schedule.bits_per_symbol);
        if (complete < 12) return {};
        auto group = 3 * complete / 4 - 1;
        if (f.ordinal >= cycle_intervals) {
            // The independently scored tail word contributes no labels to
            // the protected fits, including their extended waveform basis.
            const auto period = double(schedule.pilot_count + schedule.pilot_stride);
            const auto gap = static_cast<std::size_t>(std::ceil((pulse.support_symbols(maximum_clock) +
                .5 * max_taps + clock_observation_guard(schedule, cycle_intervals)) / period));
            const auto witness = n - 6;
            if (witness < gap + clock_fit_words) return {};
            group = std::min(group, witness - gap - clock_fit_words);
        }
        if (group + clock_fit_words > n) return {};
        return {{index, group}};
    }

    SsbCenterRange recurring_tail_range(const Grid& grid, const Frame& f) const {
        const auto margin = pilot_margin + clock_observation_guard(schedule, cycle_intervals);
        return geometry.range(grid, f.groups[f.groups.size() - clock_tail_words].pilot - margin, f.end + margin);
    }

    std::optional<SavedWindow> save(SsbAcquisitionView view, const Frame& f, std::size_t group,
        SsbCenterRange range) const {
        const auto samples = observed(view, range, geometry.stride);
        if (!samples || samples->size() > observation_bound) return {};
        return SavedWindow{f.ordinal, group, range.first, {samples->begin(), samples->end()}, committed.grid};
    }

    std::optional<SavedWindow> save_clock_context(SsbAcquisitionView view, const Frame& f, std::size_t group,
        const Grid& capture_grid) const {
        const auto range = geometry.range(capture_grid, f.groups[group].pilot,
            f.groups[group + clock_fit_words - 1].end);
        auto result = save(view, f, group, range);
        if (!result) return {};
        for (std::size_t i = 0; i < result->fit.size(); ++i) {
            const auto& word = f.groups[group + i];
            result->fit[i] = geometry.range(capture_grid, word.pilot, word.end);
            if (i) {
                const auto margin = (word.pilot - word.first) / 3;
                result->score[i - 1] = geometry.range(capture_grid, word.first + margin, word.pilot - margin);
            }
        }
        return result;
    }
};

bool SsbLocalFir::eligible(const Profile& p, const SsbFrontendGeometry& frontend, const SsbAcquisitionLock& lock) {
    if (p.channel != Channel::ssb || !p.capacity_mode || p.acoustic_ofdm ||
        p.sample_rate != frontend.input_rate || !frontend.decimation ||
        single_carrier_low_rate_decimation(p) != 1 || !delay_grid(p, lock)) return false;
    const SingleCarrierSchedule schedule(p);
    const auto centers_per_symbol = p.sample_rate / (p.symbol_rate * frontend.decimation * (1. - maximum_clock));
    const auto group = schedule.pilot_count + schedule.pilot_stride;
    const auto guard = clock_observation_guard(schedule, fast::cycle_intervals(p));
    const auto extent = schedule.interval_symbols(0) + schedule.marker_span + group + 2 * schedule.pilot_count + 24. +
        2. * (guard + pilot_margin);
    return schedule.pilot_count == 16 && schedule.pilot_stride >= 8 && schedule.pilot_stride <= 64 &&
        schedule.marker_span >= schedule.pilot_count &&
        schedule.bits_per_symbol <= max_local_label_bits &&
        (schedule.data_symbols + schedule.pilot_stride - 1) / schedule.pilot_stride >= routing_fit_groups &&
        std::ceil((group + 2 * inverse_margin) * centers_per_symbol) + 2. <= max_window_centers &&
        std::ceil(extent * centers_per_symbol) + 2. <= max_interval_centers;
}

SsbLocalFir::SsbLocalFir(const Profile& p, const SsbFrontendGeometry& frontend, const SsbAcquisitionLock& lock,
    std::stop_token stop) {
    if (!eligible(p, frontend, lock)) throw std::invalid_argument("Fast SSB FIR requires short acquired support");
    impl_ = std::make_unique<Impl>(p, frontend, lock, *delay_grid(p, lock), stop);
}
SsbLocalFir::~SsbLocalFir() = default;
SsbLocalFir::SsbLocalFir(SsbLocalFir&&) noexcept = default;
SsbLocalFir& SsbLocalFir::operator=(SsbLocalFir&&) noexcept = default;

SsbCenterRange SsbLocalFir::required_centers(std::uint64_t ordinal) const {
    return impl_->required(impl_->committed.grid, impl_->frame(ordinal));
}

SsbCenterRange SsbLocalFir::holdout_centers(std::uint64_t ordinal) const {
    return impl_->routing_aperture(impl_->committed.grid, impl_->frame(ordinal)).all;
}

std::vector<SsbCenterRange> SsbLocalFir::quiet_groups(std::uint64_t ordinal) const {
    const auto& p = *impl_;
    const auto groups = ssb_public_group_symbols(p.schedule, ordinal);
    std::vector<SsbCenterRange> result;
    for (const auto unit : ssb_quiet_units(groups.size()))
        result.push_back(p.geometry.range(p.committed.grid, groups[unit.first].first, groups[unit.end - 1].past_last));
    return result;
}

std::optional<SsbPublicPresence> SsbLocalFir::score_group(SsbAcquisitionView view, std::uint64_t ordinal,
    std::size_t group_index, std::stop_token stop, std::optional<double> noise_power,
    SsbQuietUnitDiagnostics* diagnostics) const {
    if (diagnostics) *diagnostics = {};
    if (stop.stop_requested()) return {};
    const auto& p = *impl_;
    const auto f = p.frame(ordinal);
    const auto groups = ssb_public_group_symbols(p.schedule, ordinal);
    const auto units = ssb_quiet_units(groups.size());
    if (group_index >= units.size()) throw std::out_of_range("Fast SSB FIR quiet group outside its public slot");
    const auto& grid = p.committed.grid;
    const auto unit = units[group_index];
    const auto range = p.geometry.range(grid, groups[unit.first].first, groups[unit.end - 1].past_last);
    const auto complete = observed(view, range, p.geometry.stride);
    if (!complete) return {};
    const bool exact_zero = std::ranges::all_of(*complete, [](C value) { return value == C{}; });
    const auto amplitude = std::sqrt(p.signal_power) / p.profile.amplitude;
    std::array<SsbQuietGroupProjection, ssb_quiet_maximum_groups> projections{};
    const auto marker_end = f.first + p.schedule.marker_symbols(f.ordinal);
    for (auto i = unit.first; i < unit.end; ++i) {
        const auto group = groups[i];
        const auto first = group.first < marker_end ? group.first : group.past_last - p.schedule.pilot_count;
        const auto basis = make_basis(p.geometry, grid, view,
            p.geometry.range(grid, first, group.past_last), stop);
        if (!basis) return {};
        const auto word = [&](std::size_t k) -> std::optional<C> {
            if (k < first || k >= group.past_last) return {};
            return p.schedule.position(f.ordinal, k - f.first).known_value;
        };
        const auto labels = make_labels(*basis, word);
        std::optional<WordScore> best;
        std::size_t workspace = 0;
        // Absence uses the entire bounded physical delay grid with free
        // complex correlation, independent of a stale carried gain array.
        for (std::size_t tap = 0; tap < grid.taps; ++tap) {
            Knot left{basis->first_tick, {}};
            left.gains[tap] = amplitude;
            auto right = left;
            right.tick += 1.;
            const auto score = score_word(*basis, labels, Bridge(left, right), word, p.ridge,
                noise_power.value_or(0.), stop, workspace);
            if (!score) return stop.stop_requested() ? std::nullopt :
                std::optional(SsbPublicPresence::unscored);
            if (score->measures.informative &&
                (!best || score->measures.squared_coherence > best->measures.squared_coherence))
                best = score;
        }
        if (!best) return SsbPublicPresence::unscored;
        const auto& m = best->measures;
        projections[i - unit.first] = {m.centers, m.raw_template_energy, m.projected_template_energy,
            m.projected_input_energy, best->cross};
    }
    const auto result = classify_ssb_quiet_unit(std::span(projections).first(unit.end - unit.first), exact_zero,
        noise_power, diagnostics);
    // A single matching path can establish continuing presence. Missing
    // every individual template does not establish a null for their mixture.
    if (result == SsbPublicPresence::absent && !exact_zero && grid.taps > 1)
        return SsbPublicPresence::no_detection;
    return result;
}

std::size_t SsbLocalFir::maximum_observation_centers() const noexcept { return impl_->observation_bound; }
double SsbLocalFir::reference_power() const noexcept { return impl_->signal_power; }

std::size_t SsbLocalFir::workspace_bytes() const noexcept {
    const auto& p = *impl_;
    auto bytes = sizeof(Impl) + p.pulse.workspace_bytes() + p.nominal.pulse->workspace_bytes() +
        p.acquisition.capacity() * sizeof(SsbAcquisitionAtom);
    if (p.committed.grid.pulse != p.nominal.pulse) bytes += p.committed.grid.pulse->workspace_bytes();
    if (p.feedback) {
        if (p.feedback->base.pulse != p.committed.grid.pulse && p.feedback->base.pulse != p.nominal.pulse)
            bytes += p.feedback->base.pulse->workspace_bytes();
        for (const auto& window : p.feedback->windows)
            if (window) bytes += window->samples.capacity() * sizeof(C);
        if (p.feedback->tail) bytes += p.feedback->tail->samples.capacity() * sizeof(C);
    }
    return bytes;
}

std::optional<SsbLocalFirHoldout> SsbLocalFir::public_holdout(SsbAcquisitionView view, std::uint64_t ordinal,
    std::stop_token stop) const {
    const auto& p = *impl_;
    const auto f = p.frame(ordinal);
    if (stop.stop_requested()) return {};
    std::size_t workspace = 0;
    auto state = p.committed;
    const auto aperture = p.routing_aperture(state.grid, f);
    if (!observed(view, aperture.all, p.geometry.stride)) return {};
    if (!p.last_present) {
        if (ordinal != 0) return {};
        const auto seed = p.seed(view, stop, workspace);
        if (!seed) return {};
        state.knot = *seed;
    }
    std::vector<Knot> fitted{state.knot};
    for (std::size_t i = 0; i < aperture.fit.size(); ++i) {
        if (!aperture.fit[i]) continue;
        const auto next = p.fit_pilot(state.grid, view, f, f.groups[i], fitted.back(), stop, workspace, aperture.fit[i]);
        if (!next) return {};
        fitted.push_back(*next);
    }
    const auto score = [&](std::size_t i) {
        return p.heldout_score(state.grid, state.grid, view, f, fitted, std::span(routing_words).subspan(i, 1),
            stop, workspace, std::span(aperture.score).subspan(i, 1));
    };
    const auto admission = score(0);
    const auto prospective = score(1);
    if (!admission || !prospective) return {};
    const auto pooled = (admission->error + prospective->error) / (admission->power + prospective->power);
    return SsbLocalFirHoldout{admission->nmse(), prospective->nmse(),
        workspace + fitted.capacity() * sizeof(Knot), pooled,
        admission->present_words == 1, prospective->present_words == 1};
}

std::optional<SsbLocalFirEvidence> SsbLocalFir::estimate(SsbAcquisitionView view, std::uint64_t ordinal,
    std::stop_token stop, std::optional<double> noise_power, SsbDisplayPoints* display) const {
    if (stop.stop_requested()) return {};
    if (noise_power && (!std::isfinite(*noise_power) || *noise_power < 0.))
        throw std::invalid_argument("Invalid Fast SSB FIR noise estimate");
    const auto& p = *impl_;
    const auto f = p.frame(ordinal);
    auto state = p.committed;
    const auto& grid = state.grid;
    if (!observed(view, p.required(grid, f), p.geometry.stride)) return {};
    SsbLocalFirEvidence out;
    out.ordinal = ordinal;
    const auto sampled = periodic_indices(f.groups.size());
    const auto noise = noise_power.value_or(0.);
    if (noise_power && ordinal) {
        const bool candidate = std::ranges::any_of(sampled, [&](std::size_t i) {
            const auto& g = f.groups[i];
            const auto y = observed(view, p.geometry.range(grid, g.pilot, g.end), p.geometry.stride);
            return y && power(*y) > 8. * noise * y->size();
        });
        if (!candidate) return out;
    }
    if (!p.last_present) {
        if (ordinal) return {};
        const auto seed = p.seed(view, stop, out.workspace_bytes);
        if (!seed) return {};
        state.knot = *seed;
    }
    std::vector<Knot> knots{state.knot};
    knots.reserve(f.groups.size() + 1);
    unsigned predicted = 0;
    for (std::size_t i = 0; i < f.groups.size(); ++i) {
        const auto& g = f.groups[i];
        if (std::ranges::find(sampled, i) != sampled.end()) {
            const auto score = p.predict(grid, view, f, g, knots.back(), noise, stop, out.workspace_bytes);
            if (!score) return {};
            predicted += score->present;
            out.presence.coherence += score->measures.squared_coherence / sampled.size();
            out.presence.amplitude += score->measures.amplitude / sampled.size();
            out.presence.projected_power += score->measures.projected_input_energy /
                (score->measures.centers * sampled.size());
        }
        const auto next = p.fit_pilot(grid, view, f, g, knots.back(), stop, out.workspace_bytes);
        if (!next) return {};
        knots.push_back(*next);
    }
    if (predicted < 2) return out;
    out.presence.present = true;

    const auto future = p.frame(ordinal + 1);
    const auto next = p.predict(grid, view, future, future.groups.front(), knots.back(), noise, stop,
        out.workspace_bytes);
    if (!next) return {};
    const auto previous = p.last_present && *p.last_present + 1 == ordinal ?
        std::optional(p.frame(ordinal - 1)) : std::nullopt;
    double floor = p.calibration;
    const auto floor_guard = ssb_demapper_variance_guard(p.profile.constellation);
    std::size_t pilots = 0;
    for (std::size_t i = 0; i < f.groups.size(); ++i) {
        const auto& g = f.groups[i];
        const auto basis = make_basis(p.geometry, grid, view,
            p.geometry.range(grid, g.first - inverse_margin, g.end + inverse_margin), stop);
        if (!basis) return {};
        const auto labels = make_labels(*basis, [&](std::size_t k) -> std::optional<C> {
            if (const auto known = p.startup(k)) return known;
            if (k < f.first && previous) return p.training_in_frame(*previous, k);
            if (k >= f.end && next->present) return p.training_in_frame(future, k);
            return p.training_in_frame(f, k);
        });
        const auto decoded = demap_group(*basis, labels, Bridge(knots[i], knots[i + 1]),
            f, g, p.profile, p.schedule, p.ridge, floor, out.soft_bits, stop, display);
        if (!decoded || !decoded->pilots) return {};
        pilots += decoded->pilots;
        out.pilot_error_power += decoded->pilot_error;
        out.expected_pilot_variance += decoded->pilot_variance;
        floor = std::clamp((decoded->pilot_error - decoded->pilot_variance) / decoded->pilots, floor_guard, .35);
        out.workspace_bytes = std::max(out.workspace_bytes, decoded->workspace);
    }
    out.pilot_error_power /= pilots;
    out.expected_pilot_variance /= pilots;
    auto future_state = state;
    future_state.knot = knots.back();
    if (auto trial = p.support_trial(state, view, f, stop, out.workspace_bytes))
        future_state = std::move(*trial);
    if (stop.stop_requested()) return {};
    out.tracker_proposed = true;
    out.proposal_ = std::make_unique<SsbLocalFirProposal>(
        SsbLocalFirProposal{&p, p.revision, ordinal, std::move(future_state), floor});
    out.workspace_bytes += knots.capacity() * sizeof(Knot) + sizeof(SsbLocalFirProposal);
    return out;
}

bool SsbLocalFir::commit_present(const SsbLocalFirEvidence& estimate, SsbAcquisitionView view,
    std::stop_token stop) {
    auto& p = *impl_;
    if (stop.stop_requested() || !estimate.presence.present || !estimate.proposal_ ||
        estimate.proposal_->owner != &p || estimate.proposal_->revision != p.revision ||
        estimate.proposal_->ordinal != estimate.ordinal || (p.last_present && estimate.ordinal <= *p.last_present))
        return false;
    const auto f = p.frame(estimate.ordinal);
    if (p.cycle_intervals >= 8 && f.groups.size() >= 24) {
        const auto cycle = f.ordinal / p.cycle_intervals;
        if (!p.feedback || p.feedback->wire_cycle != cycle)
            p.feedback = ClockCapture{cycle, cycle ? p.committed.grid : p.nominal};
        auto& capture = *p.feedback;
        if (const auto window = p.clock_window(f))
            capture.windows[window->first] = p.save_clock_context(view, f, window->second, capture.base);
        if (f.ordinal % p.cycle_intervals == p.cycle_intervals - 1) {
            const auto first = f.groups.size() - 4;
            if (cycle) {
                capture.tail = p.save(view, f, first, p.recurring_tail_range(capture.base, f));
            } else {
                const auto anchor = p.geometry.tick(p.nominal, p.schedule.training_count);
                const auto end = p.geometry.tick(p.nominal, f.end);
                const auto drift = std::ceil(std::abs(end - anchor) * clock_trial_limit * p.geometry.beta /
                    (1. - maximum_clock));
                const auto margin = pilot_margin + drift + 2.;
                capture.tail = p.save(view, f, first,
                    p.geometry.range(p.nominal, f.groups[first].pilot - margin, f.end + margin));
            }
        }
    }
    if (stop.stop_requested()) return false;
    p.committed = estimate.proposal_->state;
    p.calibration = estimate.proposal_->calibration;
    p.last_present = estimate.ordinal;
    ++p.revision;
    return true;
}

bool SsbLocalFir::wants_cycle_feedback(std::uint64_t ordinal) const noexcept {
    const auto& p = *impl_;
    return p.cycle_intervals && p.feedback && p.feedback->wire_cycle == ordinal / p.cycle_intervals &&
        ordinal % p.cycle_intervals == p.cycle_intervals - 1 && p.feedback->complete();
}

namespace {

struct ProtectedKnot {
    Knot center;
    Gains slopes{};
};

template<class Label>
std::optional<ProtectedKnot> fit_protected_affine(const Basis& basis, const Label& label, double tick, double scale,
    double ridge, std::stop_token stop, std::size_t& workspace) {
    const auto labels = make_labels(basis, label);
    if (labels.count) return {};
    const auto n = basis.taps, columns = 2 * n;
    HermitianBand gram;
    gram.reset(columns, columns - 1);
    std::vector<C> rhs(columns);
    std::array<C, 2 * max_taps> features{};
    std::array<std::span<C>, 2 * max_taps> lower;
    for (std::size_t i = 0; i < columns; ++i) lower[i] = gram.lower_row(i);
    for (std::size_t row = 0; row < basis.samples.size(); ++row) {
        if (row % 16 == 0 && stop.stop_requested()) return {};
        features.fill(C{});
        const auto elapsed = basis.tick(row) - tick;
        for (std::size_t k = 0; k < basis.columns; ++k) {
            const auto symbol = *labels.known[k];
            for (std::size_t a = 0; a < n; ++a) {
                const auto term = basis.at(row, k, a);
                features[a] += symbol * term.value;
                features[n + a] += (symbol / scale) * (elapsed * term.value - term.moment);
            }
        }
        for (std::size_t a = 0; a < columns; ++a) {
            const auto adjoint = std::conj(features[a]);
            rhs[a] += adjoint * basis.samples[row];
            for (std::size_t b = 0; b <= a; ++b) lower[a][b] += adjoint * features[b];
        }
    }
    if (gram.factor(ridge, stop) != HermitianBand::FactorResult::complete ||
        !gram.solve_in_place(rhs, stop)) return {};
    ProtectedKnot result{{tick, {}}};
    for (std::size_t a = 0; a < n; ++a) {
        if (!finite(rhs[a])) return {};
        result.center.gains[a] = rhs[a];
        result.slopes[a] = rhs[n + a] / scale;
    }
    workspace = std::max(workspace, basis.workspace() + gram.workspace_bytes() + rhs.capacity() * sizeof(C));
    return result;
}

template<class Label>
std::optional<std::pair<double, double>> predict_protected(const Basis& basis, const Label& label,
    const Bridge& bridge, std::stop_token stop, std::size_t& workspace) {
    const auto labels = make_labels(basis, label);
    if (labels.count) return {};
    double error = 0., signal = 0.;
    for (std::size_t row = 0; row < basis.samples.size(); ++row) {
        if (row % 16 == 0 && stop.stop_requested()) return {};
        const auto gain = bridge.at(basis.tick(row));
        C expected{};
        for (std::size_t k = 0; k < basis.columns; ++k)
            expected += *labels.known[k] * coefficient(basis, row, k, gain, bridge.slopes);
        error += std::norm(basis.samples[row] - expected);
        signal += std::norm(basis.samples[row]);
    }
    if (!std::isfinite(error) || !(signal > 0.)) return {};
    workspace = std::max(workspace, basis.workspace());
    return {{error, signal}};
}

} // namespace

bool ssb_local_fir_cycle_feedback(const ValidatedCapacityCycle& event, std::uint64_t wire_ordinal,
    std::size_t protected_bits) noexcept {
    return event.wire_ordinal == wire_ordinal && event.protected_bits.size() == protected_bits &&
        (event.validation == CapacityCycleValidation::authenticated ||
            event.validation == CapacityCycleValidation::checksummed);
}

std::optional<std::size_t> ssb_local_protected_offset(std::size_t relative_interval, std::size_t bit_offset,
    std::size_t bit_count, std::size_t protected_bits) noexcept {
    if (relative_interval > protected_bits / physical_interval_bits || !bit_count ||
        bit_count > physical_interval_bits || bit_offset > physical_interval_bits - bit_count) return {};
    const auto base = relative_interval * physical_interval_bits;
    const auto remaining = protected_bits - base;
    if (bit_offset > remaining || bit_count > remaining - bit_offset) return {};
    return base + bit_offset;
}

std::optional<double> ssb_local_coherent_delay(const SsbPulseCache& pulse, std::span<const C> gains,
    std::span<const C> slopes, double first_delay, double symbols_per_tick, std::stop_token stop) {
    if (gains.empty() || gains.size() != slopes.size() || gains.size() > max_taps ||
        !std::isfinite(first_delay) || !std::isfinite(symbols_per_tick) || !(symbols_per_tick > 0.)) return {};
    constexpr double resolution = 8.;
    const auto first = static_cast<int>(std::floor(-pulse.support_symbols() * resolution));
    const auto end = static_cast<int>(std::ceil((pulse.support_symbols() + .5 * (gains.size() - 1)) * resolution));
    double mass = 0., moment = 0.;
    std::array<SsbPulseKernelValue, max_taps> kernels;
    for (auto i = first; i <= end; ++i) {
        if ((i - first) % 64 == 0 && stop.stop_requested()) return {};
        const auto symbol = i / resolution;
        pulse.half_lattice_values(symbol, std::span(kernels).first(gains.size()));
        C coherent{};
        for (std::size_t tap = 0; tap < gains.size(); ++tap) {
            const auto& kernel = kernels[tap];
            coherent += ssb_affine_path({kernel.value, {}, kernel.moment, {}}, gains[tap], slopes[tap]);
        }
        const auto energy = std::norm(coherent);
        mass += energy;
        moment += symbol * energy;
    }
    if (!(mass > 0.) || !std::isfinite(mass) || !std::isfinite(moment)) return {};
    const auto delay = first_delay + moment / (mass * symbols_per_tick);
    return std::isfinite(delay) ? std::optional(delay) : std::nullopt;
}

bool ssb_local_phase_eligible(double current, double candidate, double heldout,
    double symbols_per_tick, double pilot_period) noexcept {
    return std::isfinite(current) && std::isfinite(candidate) && std::isfinite(heldout) &&
        std::isfinite(symbols_per_tick) && symbols_per_tick > 0. && std::isfinite(pilot_period) &&
        std::abs(candidate - current) * symbols_per_tick < .5 * pilot_period &&
        std::abs(candidate - heldout) < std::abs(current - heldout);
}

std::optional<double> ssb_local_clock_midpoint(
    const std::array<std::optional<double>, ssb_local_clock_coarse_ppm.size()>& scores,
    std::size_t coarse_winner) noexcept {
    const auto usable = [](const auto& score) { return score && std::isfinite(*score) && *score >= 0.; };
    if (coarse_winner >= scores.size() || !usable(scores[coarse_winner])) return {};
    auto runner_up = scores.size();
    for (std::size_t i = 0; i < scores.size(); ++i) {
        if (i == coarse_winner || !usable(scores[i])) continue;
        if (runner_up == scores.size() || *scores[i] < *scores[runner_up]) runner_up = i;
    }
    if (runner_up == scores.size() || std::max(runner_up, coarse_winner) - std::min(runner_up, coarse_winner) != 1)
        return {};
    return std::midpoint(ssb_local_clock_coarse_ppm[coarse_winner], ssb_local_clock_coarse_ppm[runner_up]);
}

bool ssb_local_clock_holdout_accepts(double nominal, double carried, double candidate) noexcept {
    return candidate <= .10 && candidate <= .90 * nominal && candidate <= .90 * carried;
}

SsbCycleRefinement SsbLocalFir::finish_coding_cycle(std::uint64_t wire_ordinal,
    const std::optional<ValidatedCapacityCycle>& event, std::stop_token stop) {
    auto& p = *impl_;
    SsbCycleRefinement result;
    if (!p.feedback || p.feedback->wire_cycle != wire_ordinal || stop.stop_requested()) return result;
    const auto capture = std::exchange(p.feedback, std::nullopt);
    if (!event || !ssb_local_fir_cycle_feedback(*event, wire_ordinal, p.protected_bits) || !capture->complete()) return result;
    const auto& base = capture->base;
    const auto& tail = *capture->tail;
    const auto final_frame = p.frame(tail.ordinal);
    const auto first_frame = tail.ordinal - tail.ordinal % p.cycle_intervals;
    const auto cadence = p.schedule.marker_cadence;
    const auto next_marker = (cadence - first_frame % cadence) % cadence;
    const auto anchor_frame = p.frame(first_frame + (next_marker < p.cycle_intervals ? next_marker : 0));
    const auto anchor_symbol = next_marker < p.cycle_intervals ? anchor_frame.first : anchor_frame.groups.front().pilot;
    const auto anchor = p.geometry.tick(base, anchor_symbol);
    const auto* witness = wire_ordinal ? &final_frame.groups[final_frame.groups.size() - 6] : nullptr;
    result.attempted = true;
    // The saved observations remain alive throughout calibration. They
    // are released before the receiver samples the retained workspace.
    std::size_t saved_bytes = tail.samples.capacity() * sizeof(C) + sizeof(ClockCapture);
    if (base.pulse != p.nominal.pulse) saved_bytes += base.pulse->workspace_bytes();
    for (const auto& window : capture->windows) saved_bytes += window->samples.capacity() * sizeof(C);
    const auto cache_build_bytes = [&](double clock) {
        // Bound the sum of rounded transmit and FIR half-supports plus
        // cache interpolation padding. During construction, three FFT
        // arrays, a twiddle table, and both result kernels may coexist.
        const auto half = static_cast<std::size_t>(std::ceil(
            p.pulse.support_symbols(clock) * SsbPulseCache::samples_per_symbol)) + 3;
        const auto values = 2 * half + 1;
        const auto fft = std::bit_ceil(values);
        return (4 * fft + 2 * values) * sizeof(C);
    };
    const auto trial = [&](double ppm) -> std::optional<Grid> {
        auto grid = base;
        grid.clock += ppm * 1e-6;
        grid.offset += (base.clock - grid.clock) * anchor;
        if (ppm) {
            try {
                grid.pulse = std::make_shared<const SsbPulseCache>(p.pulse.make_cache(grid.clock, 0., stop));
            } catch (const std::runtime_error&) {
                if (stop.stop_requested()) return {};
                throw;
            }
        }
        return grid;
    };
    struct ProtectedScore {
        Impl::ScoreSum observations;
        std::optional<double> delay;
        double nmse() const { return observations.nmse(); }
    };
    const auto score = [&](const Grid& grid, const SavedWindow& window,
        std::size_t& scratch_bytes, bool estimate_delay = false) -> std::optional<ProtectedScore> {
        const auto f = p.frame(window.ordinal);
        if (f.ordinal / p.cycle_intervals != wire_ordinal) return {};
        const auto label = [&](std::size_t symbol) -> std::optional<C> {
            if (symbol < f.first || symbol >= f.end) return {};
            if (witness && symbol >= witness->pilot && symbol < witness->end) return {};
            const auto position = p.schedule.position(f.ordinal, symbol - f.first);
            if (position.kind != SingleCarrierSchedule::Kind::data) return position.known_value;
            const auto offset = ssb_local_protected_offset(f.ordinal % p.cycle_intervals,
                position.bit_offset, position.bit_count, event->protected_bits.size());
            if (!offset) return {};
            unsigned index = 0;
            for (unsigned i = 0; i < position.bit_count; ++i) {
                const auto bit = event->protected_bits[*offset + i];
                if (bit > 1) return {};
                index = (index << 1) | bit;
            }
            index <<= p.schedule.bits_per_symbol - position.bit_count;
            return square_qam_point(p.profile.constellation, index);
        };
        std::array<ProtectedKnot, clock_fit_words> knots;
        const auto view = window.view(p.geometry.stride);
        const auto scale = (p.schedule.pilot_count + p.schedule.pilot_stride) / p.geometry.beta;
        for (std::size_t i = 0; i < knots.size(); ++i) {
            const auto& g = f.groups[window.first_group + i];
            const auto basis = make_basis(p.geometry, grid, view, window.fit[i], stop);
            if (!basis) return {};
            const auto fit = fit_protected_affine(*basis, label, p.geometry.tick(grid, .5 * (g.pilot + g.end - 1)),
                scale, p.ridge / innovation_variance, stop, scratch_bytes);
            if (!fit) return {};
            knots[i] = *fit;
        }
        ProtectedScore result;
        for (std::size_t i = 1; i < knots.size(); ++i) {
            const auto basis = make_basis(p.geometry, grid, view, window.score[i - 1], stop);
            if (!basis) return {};
            const auto prediction = predict_protected(*basis, label,
                Bridge(knots[i - 1].center, knots[i].center), stop, scratch_bytes);
            if (!prediction) return {};
            result.observations.error += prediction->first;
            result.observations.power += prediction->second;
        }
        if (estimate_delay) {
            std::array<double, clock_fit_words> centers{};
            for (std::size_t i = 0; i < knots.size(); ++i) {
                const auto center = ssb_local_coherent_delay(*grid.pulse,
                    std::span(knots[i].center.gains).first(grid.taps), std::span(knots[i].slopes).first(grid.taps),
                    grid.first_delay, p.geometry.beta, stop);
                if (!center) return result;
                centers[i] = *center;
            }
            std::ranges::sort(centers);
            result.delay = centers[centers.size() / 2];
        }
        return result;
    };
    double best = std::numeric_limits<double>::infinity();
    double selected = 0.;
    std::optional<Grid> winning;
    const auto rank = [&](double ppm) -> std::optional<double> {
        if (stop.stop_requested() || std::abs(base.clock + ppm * 1e-6) > maximum_clock) return {};
        if (ppm) {
            const auto retained_winner = winning && winning->pulse != base.pulse ?
                winning->pulse->workspace_bytes() : 0;
            result.workspace_bytes = std::max(result.workspace_bytes,
                retained_winner + cache_build_bytes(base.clock + ppm * 1e-6));
        }
        auto grid = trial(ppm);
        if (!grid) return {};
        const auto bank_bytes = (grid->pulse == base.pulse ? 0 : grid->pulse->workspace_bytes()) +
            (winning && winning->pulse != base.pulse ? winning->pulse->workspace_bytes() : 0);
        std::size_t scratch_bytes = 0;
        Impl::ScoreSum total;
        bool valid = true;
        for (const auto& window : std::span(capture->windows).first(3)) {
            const auto observed = score(*grid, *window, scratch_bytes);
            if (!observed) { valid = false; break; }
            total.error += observed->observations.error;
            total.power += observed->observations.power;
        }
        result.workspace_bytes = std::max(result.workspace_bytes, bank_bytes + scratch_bytes);
        if (!valid) return {};
        const auto nmse = total.nmse();
        if (nmse < best) {
            best = nmse;
            selected = ppm;
            winning = std::move(*grid);
        }
        return nmse;
    };
    std::array<std::optional<double>, ssb_local_clock_coarse_ppm.size()> scores;
    // Keep the original zero-first coarse order for equal scores.
    for (const auto index : {2U, 0U, 1U, 3U, 4U}) scores[index] = rank(ssb_local_clock_coarse_ppm[index]);
    if (winning && !stop.stop_requested()) {
        const auto winner = std::ranges::find(ssb_local_clock_coarse_ppm, selected) - ssb_local_clock_coarse_ppm.begin();
        if (const auto midpoint = ssb_local_clock_midpoint(scores, static_cast<std::size_t>(winner)))
            (void)rank(*midpoint);
    }
    std::size_t scratch_bytes = 0;
    const auto tail_view = tail.view(p.geometry.stride);
    const auto rebuild = [&](const Grid& grid, std::span<const SsbCenterRange> withheld = {}) -> std::optional<State> {
        Knot fresh;
        fresh.tick = p.geometry.tick(grid, final_frame.groups[tail.first_group].pilot) -
            (p.schedule.pilot_stride + p.schedule.pilot_count) / p.geometry.beta;
        for (auto i = tail.first_group; i < final_frame.groups.size(); ++i) {
            const auto& group = final_frame.groups[i];
            const auto rows = p.geometry.range(grid, group.pilot - pilot_margin, group.end + pilot_margin);
            const auto fit = ssb_disjoint_fit_centers(rows, withheld, p.geometry.stride, p.geometry.fir_radius);
            if (!fit) return {};
            const auto next = p.fit_pilot(grid, tail_view, final_frame, group, fresh, stop, scratch_bytes, fit);
            if (!next) return {};
            fresh = *next;
        }
        return State{grid, fresh};
    };
    const auto& last = *capture->windows.back();
    auto prospective = p.committed;
    if (selected && winning && !stop.stop_requested()) {
        const auto unchanged = score(base, last, scratch_bytes);
        const auto carried = score(last.carried_grid, last, scratch_bytes);
        const auto corrected = score(*winning, last, scratch_bytes);
        if (carried) result.baseline_holdout_nmse = carried->nmse();
        if (corrected) result.selected_holdout_nmse = corrected->nmse();
        if (unchanged && carried && corrected &&
            ssb_local_clock_holdout_accepts(unchanged->nmse(), carried->nmse(), corrected->nmse())) {
            if (auto rebuilt = rebuild(*winning)) {
                prospective = std::move(*rebuilt);
                result.updated_rates = true;
            }
        }
    }
    const auto phase = [&]() -> std::optional<State> {
        const auto& retained = prospective.grid;
        // A closing interval's ordinary tracker may already have used this
        // word. With no rate update, use the basis captured before that
        // interval when constructing the independent phase witness.
        const auto& phase_grid = result.updated_rates ? retained : tail.carried_grid;
        std::array<double, clock_context_count - 1> proposals{};
        for (std::size_t i = 0; i < proposals.size(); ++i) {
            const auto observed = score(phase_grid, *capture->windows[i], scratch_bytes, true);
            if (!observed || !observed->delay) return {};
            proposals[i] = *observed->delay;
        }
        std::ranges::sort(proposals);
        const auto candidate = proposals[proposals.size() / 2];
        const auto holdout = score(phase_grid, last, scratch_bytes, true);
        if (!holdout || !holdout->delay || !ssb_local_phase_eligible(retained.observation_delay,
            candidate, *holdout->delay, p.geometry.beta, p.schedule.pilot_count + p.schedule.pilot_stride)) return {};
        auto moved = phase_grid;
        moved.observation_delay = candidate;
        const auto old_range = p.geometry.range(retained, witness->pilot, witness->end);
        const auto moved_range = p.geometry.range(moved, witness->pilot, witness->end);
        if ((old_range.first == moved_range.first && old_range.past_last == moved_range.past_last) ||
            !observed(tail_view, moved_range, p.geometry.stride)) return {};
        const std::array withheld{moved_range};
        const auto excludes_witness = [&](SsbCenterRange rows) {
            const auto separated = ssb_disjoint_fit_centers(rows, withheld, p.geometry.stride, p.geometry.fir_radius);
            return separated && separated->first == rows.first && separated->past_last == rows.past_last;
        };
        for (const auto& window : capture->windows)
            if (!std::ranges::all_of(window->fit, excludes_witness) ||
                !std::ranges::all_of(window->score, excludes_witness)) return {};

        // Fit public words bracketing the withheld pilot on disjoint real
        // FIR input apertures. No decoded or alignment labels enter here.
        const auto count = final_frame.groups.size();
        const std::array fit_groups{count - 8, count - 7, count - 5};
        std::array<Knot, fit_groups.size()> knots;
        Knot prior;
        prior.tick = p.geometry.tick(moved, final_frame.groups[fit_groups.front()].pilot) -
            (p.schedule.pilot_count + p.schedule.pilot_stride) / p.geometry.beta;
        for (std::size_t i = 0; i < fit_groups.size(); ++i) {
            const auto& group = final_frame.groups[fit_groups[i]];
            const auto rows = p.geometry.range(moved, group.pilot - pilot_margin, group.end + pilot_margin);
            const auto fit = ssb_disjoint_fit_centers(rows, withheld, p.geometry.stride, p.geometry.fir_radius);
            if (!fit) return {};
            const auto next = p.fit_pilot(moved, tail_view, final_frame, group, prior, stop, scratch_bytes, fit);
            if (!next) return {};
            knots[i] = prior = *next;
        }
        const auto verification = p.public_word(moved, tail_view, final_frame, witness->pilot, witness->end,
            moved_range, Bridge(knots[1], knots[2]), 0., stop, scratch_bytes);
        if (!verification || !verification->present) return {};
        auto future_grid = retained;
        future_grid.observation_delay = candidate;
        return rebuild(future_grid, withheld);
    };
    if (witness && !stop.stop_requested()) {
        if (auto moved = phase()) {
            prospective = std::move(*moved);
            result.updated_observation_phase = true;
        }
    }
    if (!stop.stop_requested() && (result.updated_rates || result.updated_observation_phase)) {
        p.committed = std::move(prospective);
        ++p.revision;
        result.accepted = true;
    } else {
        result.updated_rates = result.updated_observation_phase = false;
    }
    const auto bank_bytes = winning && winning->pulse != base.pulse ? winning->pulse->workspace_bytes() : 0;
    result.workspace_bytes = std::max(result.workspace_bytes, bank_bytes + scratch_bytes);
    result.workspace_bytes += saved_bytes;
    return result;
}

} // namespace datapump::fast::detail
