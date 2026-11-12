#include "ssb_quiet_low_power.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::fast::detail {
namespace {

using Complex = std::complex<double>;
constexpr double absolute_power_ceiling = 2.5e-5;
constexpr double reference_power_fraction = .001;
constexpr double noise_multiplier = 3.;
constexpr double strong_coherence = .80;
constexpr double unit_coherence = .40;

std::optional<double> mean_power(std::span<const Complex> input) {
    if (input.empty()) return {};
    double sum = 0.;
    for (const auto value : input) sum += std::norm(value);
    if (!std::isfinite(sum)) return {};
    return sum / input.size();
}

struct Coherence {
    bool meaningful = false;
    bool veto = false;
};

Coherence coherent_word(const SsbQuietUnitDiagnostics& diagnostics) {
    if (diagnostics.count < ssb_quiet_minimum_groups ||
        diagnostics.count > diagnostics.groups.size()) return {};
    constexpr auto minimum_projection = 64. * std::numeric_limits<double>::epsilon();
    bool meaningful = true;
    bool fragment = false;
    double sum = 0.;
    for (const auto& group : std::span(diagnostics.groups).first(diagnostics.count)) {
        const auto raw = group.raw_template_energy;
        const auto pilot = group.projected_template_energy;
        const auto y = group.projected_input_energy;
        const auto cross = group.cross_squared;
        if (!group.centers || !std::isfinite(raw) || !std::isfinite(pilot) || !std::isfinite(y) ||
            !std::isfinite(cross) || raw <= 0. || pilot <= 0. || pilot < minimum_projection * raw ||
            y < 0. || cross < 0.) {
            meaningful = false;
            continue;
        }
        if (y == 0.) {
            meaningful &= cross == 0.;
            continue;
        }
        const auto denominator = y * pilot;
        if (!(std::isfinite(denominator) && denominator > 0.)) {
            meaningful = false;
            continue;
        }
        const auto quality = std::clamp(cross / denominator, 0., 1.);
        fragment |= quality >= strong_coherence;
        sum += quality;
    }
    return {meaningful, fragment || (meaningful && sum / diagnostics.count >= unit_coherence)};
}

} // namespace

SsbLowPowerQuiet::SsbLowPowerQuiet(std::uint32_t rate, std::size_t stride, std::size_t fir_radius)
    : stride_(stride), core_trim_(0), block_centers_(0), maximum_centers_(0), input_rate_(rate) {
    if (!stride_ || stride_ > rate / 1000 || fir_radius > rate / 20)
        throw std::invalid_argument("Invalid Fast SSB low-power quiet geometry");
    // Opposite full-unit samples can extend one FIR radius across their
    // center boundary. A core begins two radii inside its own unit.
    core_trim_ = (2 * fir_radius + stride_ - 1) / stride_;
    block_centers_ = (rate + 100 * stride_ - 1) / (100 * stride_);
    maximum_centers_ = rate / (4 * stride_);
}

std::optional<std::span<const Complex>> SsbLowPowerQuiet::samples(SsbAcquisitionView view,
                                                                  SsbCenterRange range) const {
    if (view.stride != stride_ || range.first < view.first_center_input_tick ||
        range.past_last <= range.first) return {};
    const auto offset = range.first - view.first_center_input_tick;
    const auto extent = range.past_last - range.first;
    if (offset % stride_ || extent % stride_) return {};
    const auto first = offset / stride_;
    const auto count = extent / stride_;
    if (count > maximum_centers_ || first > view.samples.size() || count > view.samples.size() - first)
        return {};
    return view.samples.subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(count));
}

std::optional<SsbLowPowerQuiet::Power> SsbLowPowerQuiet::measure(std::span<const Complex> input,
                                                                 bool need_core) const {
    if (input.size() < block_centers_) return {};
    Power result;
    double sum = 0.;
    for (std::size_t first = 0; first < input.size();) {
        const auto remaining = input.size() - first;
        const auto count = remaining < block_centers_ + (block_centers_ + 1) / 2 ?
            remaining : block_centers_;
        const auto block = mean_power(input.subspan(first, count));
        if (!block) return {};
        result.peak_block_mean = std::max(result.peak_block_mean, *block);
        sum += *block * count;
        first += count;
    }
    if (!std::isfinite(sum)) return {};
    result.mean = sum / input.size();
    if (need_core) {
        if (input.size() < 2 * core_trim_ + 2 * block_centers_) return {};
        const auto core = mean_power(input.subspan(core_trim_, input.size() - 2 * core_trim_));
        if (!core) return {};
        result.core_mean = *core;
    }
    return result;
}

bool SsbLowPowerQuiet::verified_signal(SsbAcquisitionView view, SsbCenterRange first, SsbCenterRange last) {
    interrupt();
    if (first.past_last > last.first || last.first - first.past_last < core_trim_ * stride_) return false;
    const auto first_samples = samples(view, first);
    const auto last_samples = samples(view, last);
    if (!first_samples || !last_samples || first_samples->size() < block_centers_ ||
        last_samples->size() < block_centers_) return false;
    const auto early = mean_power(*first_samples);
    const auto late = mean_power(*last_samples);
    if (!early || !late) return false;
    hard_cap_ = std::min(absolute_power_ceiling, reference_power_fraction * std::min(*early, *late));
    if (!(hard_cap_ > 0.)) return false;
    anchor_end_ = last.past_last;
    return true;
}

void SsbLowPowerQuiet::interrupt() noexcept {
    anchor_end_.reset();
    next_center_.reset();
    ceiling_.reset();
    pending_.reset();
    hard_cap_ = 0.;
    calibration_closed_ = false;
}

SsbLowPowerQuietDecision SsbLowPowerQuiet::observe(SsbAcquisitionView view, SsbCenterRange range,
        SsbPublicPresence presence, const SsbQuietUnitDiagnostics& diagnostics, std::uint64_t guard) {
    SsbLowPowerQuietDecision result;
    const SsbLowPowerQuietReceipt original{range, presence, {}};
    const auto current = samples(view, range);
    const auto coherence = current && presence != SsbPublicPresence::unscored ?
        coherent_word(diagnostics) : Coherence{};
    if (presence == SsbPublicPresence::present || coherence.veto) {
        result.receipts[0] = {range, SsbPublicPresence::present, {}};
        result.count = 1;
        result.coherent_veto = presence != SsbPublicPresence::present;
        interrupt();
        return result;
    }

    // A unit that already has a normalized null keeps that classification.
    // Low-power evidence only supplies a separate calibrated ceiling.
    const auto emit = [&](SsbLowPowerQuietReceipt receipt, std::optional<double> ceiling = {}) {
        if (receipt.presence == SsbPublicPresence::no_detection) receipt.power_ceiling = ceiling;
        result.receipts[result.count++] = receipt;
    };
    if (!anchor_end_ || range.first < guard || range.first < *anchor_end_) {
        emit(original);
        return result;
    }
    if (next_center_ && range.first != *next_center_) {
        interrupt();
        emit(original);
        return result;
    }
    const auto admissible = current && coherence.meaningful &&
        presence != SsbPublicPresence::unscored;
    const auto record = [&](const std::optional<Power>& power) {
        if (!power) return;
        result.measured_mean_power = power->mean;
        result.measured_peak_block_mean = power->peak_block_mean;
    };
    if (ceiling_) {
        if (!admissible) {
            interrupt();
            emit(original);
            return result;
        }
        next_center_ = range.past_last;
        const auto power = measure(*current, false);
        record(power);
        if (power && power->peak_block_mean <= hard_cap_ && power->mean <= *ceiling_)
            emit(original, ceiling_);
        else
            emit(original);
        return result;
    }
    if (calibration_closed_) {
        emit(original);
        return result;
    }

    const auto power = admissible ? measure(*current, true) : std::nullopt;
    record(power);
    const auto bounded = power && power->core_mean <= hard_cap_ / noise_multiplier &&
        power->peak_block_mean <= hard_cap_;
    if (!pending_) {
        if (range.first - *anchor_end_ <= input_rate_ && bounded) {
            pending_ = Pending{original, *power};
            next_center_ = range.past_last;
        } else {
            calibration_closed_ = true;
            emit(original);
        }
        return result;
    }

    const auto a = *pending_;
    pending_.reset();
    calibration_closed_ = true;
    const auto retained = samples(view, a.receipt.range);
    const auto a_ceiling = std::min(hard_cap_, noise_multiplier * a.power.core_mean);
    const auto b_ceiling = bounded ? std::min(hard_cap_, noise_multiplier * power->core_mean) : 0.;
    if (retained && bounded && a.power.mean <= b_ceiling && power->mean <= a_ceiling) {
        ceiling_ = std::min(a_ceiling, b_ceiling);
        next_center_ = range.past_last;
        emit(a.receipt, b_ceiling);
        emit(original, a_ceiling);
    } else {
        emit(retained ? a.receipt : SsbLowPowerQuietReceipt{a.receipt.range, SsbPublicPresence::unscored, {}});
        emit(original);
    }
    return result;
}

} // namespace datapump::fast::detail
