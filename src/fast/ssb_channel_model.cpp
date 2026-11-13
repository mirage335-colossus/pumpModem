#include "ssb_channel_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {

using Complex = SsbChannelModel::Complex;
constexpr auto tau = 2. * std::numbers::pi;
constexpr double maximum_clock = .001;
constexpr double maximum_delay_seconds = .024;
constexpr double maximum_doppler_hz = 500.;
constexpr auto maximum_exact_tick_difference = double(std::uint64_t{1} << 52);

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

Complex rotation(double frequency, double time, double input_rate) {
    return std::polar(1., tau * std::remainder(frequency * time / input_rate, 1.));
}

std::optional<double> cluster_split(std::span<const SsbAcquisitionAtom> atoms, double beta) {
    const auto strongest = std::max_element(atoms.begin(), atoms.end(), [](const auto& left, const auto& right) {
        return std::norm(left.gain) < std::norm(right.gain);
    });
    const SsbAcquisitionAtom* other = nullptr;
    for (const auto& atom : atoms) {
        if (std::abs(atom.delay_ticks - strongest->delay_ticks) < 8. / beta) continue;
        if (!other || std::norm(atom.gain) > std::norm(other->gain)) other = &atom;
    }
    if (!other || std::norm(other->gain) < .01 * std::norm(strongest->gain)) return {};
    return .5 * (strongest->delay_ticks + other->delay_ticks);
}

} // namespace

SsbChannelModel::SsbChannelModel(const Profile& p, const SsbFrontendGeometry& g, const SsbAcquisitionLock& lock,
                                 std::stop_token stop)
    : reference_tick_(lock.reference_input_tick), stride_(g.decimation), maximum_support_symbols_(0),
      input_rate_(p.sample_rate), beta_(p.symbol_rate / input_rate_), radius_(single_carrier_pulse_radius(p)),
      epoch_(lock.epoch_from_reference_ticks), clock_(lock.clock_ratio_error), minimum_delay_(0.),
      maximum_delay_(0.), pulse_(p, g), atoms_(lock.atoms) {
    // The fit's reference is inside the public preamble. Below 800 baud its
    // distance from the waveform origin can exceed the original two seconds.
    const auto maximum_epoch_ticks = supports_narrow_acoustic_fit(p) ?
        (SingleCarrierSchedule(p).training_count + radius_) / beta_ / (1. - maximum_clock) : 2. * input_rate_;
    if (!stride_ || reference_tick_ < g.radius() || (reference_tick_ - g.radius()) % stride_ ||
        !std::isfinite(epoch_) || std::abs(epoch_) > maximum_epoch_ticks ||
        !std::isfinite(clock_) || std::abs(clock_) > maximum_clock || atoms_.empty() ||
        atoms_.size() > DelayDopplerFit::maximum_atoms)
        throw std::invalid_argument("Invalid Fast SSB acquired channel coordinates");
    for (const auto& a : atoms_) {
        if (!std::isfinite(a.delay_ticks) || a.delay_ticks < -double(stride_) ||
            a.delay_ticks > maximum_delay_seconds * input_rate_ + stride_ ||
            !std::isfinite(a.frequency_hz) || std::abs(a.frequency_hz) > maximum_doppler_hz || !finite(a.gain))
            throw std::invalid_argument("Invalid Fast SSB acquired path");
    }
    const auto [first, last] = std::minmax_element(atoms_.begin(), atoms_.end(), [](const auto& a, const auto& b) {
        return a.delay_ticks < b.delay_ticks;
    });
    minimum_delay_ = first->delay_ticks;
    maximum_delay_ = last->delay_ticks;
    const auto support = 2. * pulse_.support_symbols(maximum_clock) + (maximum_delay_ - minimum_delay_) * beta_;
    if (support + 2. > maximum_row_symbols) throw std::length_error("Fast SSB channel support exceeds its row bound");
    maximum_support_symbols_ = static_cast<std::size_t>(std::ceil(support)) + 1;
    split_delay_ = cluster_split(atoms_, beta_);
    caches_.reserve(atoms_.size());
    for (const auto& a : atoms_) caches_.push_back(make_path_cache(a.frequency_hz, stop));
}

double SsbChannelModel::relative_ticks(std::uint64_t center) const noexcept {
    return center >= reference_tick_ ? double(center - reference_tick_) : -double(reference_tick_ - center);
}

std::uint64_t SsbChannelModel::center_at_or_after_symbol(double symbol) const {
    const auto coordinate = (epoch_ + (symbol + radius_) / beta_ - adjustment_.clock_offset) / (1. + clock());
    if (!std::isfinite(coordinate) || std::abs(coordinate) >= maximum_exact_tick_difference)
        throw std::length_error("Fast SSB observation time exceeds the center grid");
    const auto steps = static_cast<std::int64_t>(std::ceil(coordinate / stride_));
    const auto displacement = std::uint64_t(steps < 0 ? -steps : steps) * stride_;
    if (steps < 0) {
        if (displacement > reference_tick_) throw std::length_error("Fast SSB observation precedes capture");
        return reference_tick_ - displacement;
    }
    if (displacement > std::numeric_limits<std::uint64_t>::max() - reference_tick_)
        throw std::length_error("Fast SSB observation time overflow");
    return reference_tick_ + displacement;
}

double SsbChannelModel::group_sign(double delay) const noexcept {
    if (!split_delay_) return 0.;
    return delay < *split_delay_ ? -.5 : .5;
}

double SsbChannelModel::displacement(double t, double delay) const noexcept {
    return ((1. + clock()) * t - epoch_ + adjustment_.clock_offset - delay) * beta_ - radius_;
}

std::pair<std::size_t, std::size_t> SsbChannelModel::covered_symbols(std::uint64_t center) const {
    const auto t = relative_ticks(center);
    if (std::abs(t) >= maximum_exact_tick_difference)
        throw std::length_error("Fast SSB row exceeds the center grid");
    const auto support = caches_.front().support_symbols();
    const auto first = std::max(0., std::ceil(displacement(t, maximum_delay_) - support));
    const auto last = std::floor(displacement(t, minimum_delay_) + support);
    if (last < first) return {0, 0};
    return {static_cast<std::size_t>(first), static_cast<std::size_t>(last) + 1};
}

SsbChannelRow SsbChannelModel::row(std::uint64_t center, std::span<SsbChannelTerm, maximum_row_symbols> scratch,
                                  std::optional<SsbTimingBasis> timing) const {
    const auto [first_symbol, end] = covered_symbols(center);
    const auto count = end - first_symbol;
    if (count > maximum_support_symbols_) throw std::length_error("Fast SSB row exceeded its finite support");
    const auto first = static_cast<double>(first_symbol);
    const auto t = relative_ticks(center);
    const auto support = caches_.front().support_symbols();
    auto terms = scratch.first(count);
    std::fill(terms.begin(), terms.end(), SsbChannelTerm{});
    for (std::size_t a = 0; a < atoms_.size(); ++a) {
        const auto& atom = atoms_[a];
        const auto u = displacement(t, atom.delay_ticks);
        const auto lo = static_cast<std::size_t>(std::max(0., std::ceil(u - support) - first));
        const auto limit = std::min(double(count), std::floor(u + support) - first + 1.);
        if (limit <= 0. || lo >= count) continue;
        const auto hi = static_cast<std::size_t>(limit);
        if (hi <= lo) continue;
        const auto group = group_sign(atom.delay_ticks);
        const auto gain = effective_gain(atom, t);
        const auto& cache = caches_[a];
        const auto run = cache.symbol_run(u - (first + lo), hi - lo);
        const auto track = [&](std::size_t i, Complex value, Complex slope, Complex moment_slope) {
            auto& term = terms[i];
            term.value += value;
            const auto offset = gain * (beta_ * slope);
            term.relative += group * value;
            term.offset += offset;
            term.slope += (offset * (t - timing->midpoint_ticks) -
                gain * (beta_ * moment_slope)) / timing->half_span_ticks;
        };
        if (run && !timing) {
            for (auto i = lo; i < hi; ++i) terms[i].value += gain * run->value(i - lo);
        } else if (run) {
            for (auto i = lo; i < hi; ++i) {
                const auto derivatives = run->derivatives(i - lo);
                track(i, gain * run->value(i - lo), derivatives.symbol, derivatives.tap_moment);
            }
        } else {
            // Preserve the pointwise cutoff if roundoff places an endpoint
            // outside support, without changing the remaining row geometry.
            for (auto i = lo; i < hi; ++i) {
                const auto pulse = cache.kernel(u - (first + i));
                const auto value = gain * pulse.value;
                if (timing) track(i, value, pulse.symbol_derivative, pulse.tap_moment_derivative);
                else terms[i].value += value;
            }
        }
    }
    return {first_symbol, terms};
}

SsbChannelAdjustment SsbChannelModel::adjusted(SsbTrackerStep step, SsbTimingBasis timing) const {
    auto state = adjustment_;
    state.common *= std::exp(Complex{step[0], step[1]});
    state.relative_log += Complex{step[2], step[3]};
    const auto rate = step[5] / timing.half_span_ticks;
    state.clock_offset += step[4] - rate * timing.midpoint_ticks;
    state.clock_slope += rate;
    return state;
}

bool SsbChannelModel::commit_adjustment(const SsbChannelAdjustment& state, std::stop_token stop) {
    const auto corrected = clock_ + state.clock_slope;
    if (stop.stop_requested() || !finite(state.common) || !finite(state.relative_log) ||
        !std::isfinite(state.clock_offset) || !std::isfinite(corrected) || std::abs(corrected) > maximum_clock)
        return false;
    if (corrected != clock()) {
        std::vector<SsbPulseCache> trial;
        trial.reserve(atoms_.size());
        try {
            for (const auto& a : atoms_) trial.push_back(pulse_.make_cache(corrected, a.frequency_hz, stop));
        } catch (...) {
            if (stop.stop_requested()) return false;
            throw;
        }
        if (stop.stop_requested()) return false;
        caches_.swap(trial);
    }
    adjustment_ = state;
    return true;
}

std::optional<SsbChannelModel::KnownResponse> SsbChannelModel::known_path(std::uint64_t center,
        std::size_t first_symbol, std::span<const Complex> symbols, const SsbAcquisitionAtom& path,
        const SsbPulseCache& cache, double reference_time) const {
    if (cache.clock_ratio_error() != clock() || cache.frequency_hz() != path.frequency_hz) return {};
    const auto t = relative_ticks(center);
    const auto u = displacement(t, path.delay_ticks) - first_symbol;
    const auto support = cache.support_symbols();
    const auto first = std::ceil(u - support);
    const auto last = std::floor(u + support);
    if (first < 0. || last >= double(symbols.size()) || last < first) return {};
    const auto begin = static_cast<std::size_t>(first);
    const auto count = static_cast<std::size_t>(last) - begin + 1;
    Complex shaped{}, moment{};
    if (const auto run = cache.symbol_run(u - first, count)) {
        for (std::size_t k = 0; k < count; ++k) {
            shaped += symbols[begin + k] * run->value(k);
            moment += symbols[begin + k] * run->tap_moment(k);
        }
    } else {
        for (std::size_t k = begin; k < begin + count; ++k) {
            const auto pulse = cache.kernel(u - k);
            shaped += symbols[k] * pulse.value;
            moment += symbols[k] * pulse.tap_moment;
        }
    }
    const auto elapsed = t - reference_time;
    const auto phase = rotation(path.frequency_hz, elapsed, input_rate_);
    return KnownResponse{phase * shaped, phase * Complex{0., tau / input_rate_} * (elapsed * shaped - moment)};
}

SsbPulseCache SsbChannelModel::make_path_cache(double frequency_hz, std::stop_token stop) const {
    return pulse_.make_cache(clock(), frequency_hz, stop);
}

SsbChannelModel::Complex SsbChannelModel::effective_gain(const SsbAcquisitionAtom& path, double reference_time) const {
    return adjustment_.common * std::exp(group_sign(path.delay_ticks) * adjustment_.relative_log) *
        path.gain * rotation(path.frequency_hz, reference_time, input_rate_);
}

bool SsbChannelModel::replace_paths(std::vector<SsbAcquisitionAtom> atoms, std::vector<SsbPulseCache> caches,
                                   std::stop_token stop) {
    if (stop.stop_requested() || atoms.size() != atoms_.size() || caches.size() != atoms_.size()) return false;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i].delay_ticks != atoms_[i].delay_ticks || !finite(atoms[i].gain) ||
            !std::isfinite(atoms[i].frequency_hz) || std::abs(atoms[i].frequency_hz) > maximum_doppler_hz ||
            caches[i].frequency_hz() != atoms[i].frequency_hz || caches[i].clock_ratio_error() != clock()) return false;
    }
    atoms_.swap(atoms);
    caches_.swap(caches);
    adjustment_.common = 1.;
    adjustment_.relative_log = 0.;
    split_delay_ = cluster_split(atoms_, beta_);
    return true;
}

std::size_t SsbChannelModel::workspace_bytes() const noexcept {
    auto bytes = pulse_.workspace_bytes() + atoms_.capacity() * sizeof(SsbAcquisitionAtom);
    for (const auto& cache : caches_) bytes += cache.workspace_bytes();
    return bytes;
}

} // namespace datapump::fast::detail
