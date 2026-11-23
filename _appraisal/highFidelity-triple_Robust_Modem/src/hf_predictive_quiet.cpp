#include "hf_predictive_quiet.hpp"

#include "datapump/triad_stream_receiver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_fine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <numbers>

namespace appraisal {
namespace predictive_quiet_detail {

std::optional<std::vector<FrequencyInterval>> phase_intervals(FrequencyInterval bounds, double phase_turns,
                                                              double radius_turns, long double separation) {
    constexpr std::size_t maximum_alias_intervals = 32;
    constexpr long double outward_hz = 1e-9L;
    if (!std::isfinite(bounds.lower) || !std::isfinite(bounds.upper) || bounds.lower > bounds.upper ||
        !std::isfinite(phase_turns) || !(radius_turns >= 0 && radius_turns < .5) ||
        !(std::isfinite(separation) && separation > 0))
        return {};
    const long double low_turn = (bounds.lower - outward_hz) * separation;
    const long double high_turn = (bounds.upper + outward_hz) * separation;
    const auto first = std::ceil(low_turn - phase_turns - radius_turns);
    const auto last = std::floor(high_turn - phase_turns + radius_turns);
    if (!(last - first < maximum_alias_intervals) || first <= -0x1p50L || last >= 0x1p50L)
        return {};
    std::vector<FrequencyInterval> intervals;
    if (last < first) return intervals;
    intervals.reserve(static_cast<std::size_t>(last - first + 1));
    for (auto k = static_cast<std::int64_t>(first); k <= static_cast<std::int64_t>(last); ++k) {
        const auto low = std::max(static_cast<long double>(bounds.lower),
            (k + static_cast<long double>(phase_turns) - radius_turns) / separation - outward_hz);
        const auto high = std::min(static_cast<long double>(bounds.upper),
            (k + static_cast<long double>(phase_turns) + radius_turns) / separation + outward_hz);
        if (low <= high) intervals.push_back({std::nextafter(static_cast<double>(low), -INFINITY),
                                              std::nextafter(static_cast<double>(high), INFINITY)});
    }
    return intervals;
}

std::optional<double> first_lobe_radius(double fs, std::size_t rows, double fraction) {
    if (!(std::isfinite(fs) && fs > 0 && rows > 1 && fraction > .5 && fraction <= 1)) return {};
    // |D_N(f-f0)| <= fs/(2*N*|f-f0|) over the wrapped Nyquist interval.
    // A lower bound > 1/2 excludes every sidelobe and fixes the sign of
    // the centred Dirichlet factor in the whole window and both halves.
    return std::nextafter(fs / (2 * rows * fraction), INFINITY);
}

double line_direction_gain_squared(std::size_t training_rows, std::size_t observation_rows) {
    if (!training_rows) return 0;
    return 1. / (1. + static_cast<double>(observation_rows) / training_rows);
}

} // namespace predictive_quiet_detail

namespace {

using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using predictive_quiet_detail::FrequencyWitness;
using predictive_quiet_detail::Prediction;
using predictive_quiet_detail::ToneMean;
using predictive_quiet_detail::frequency_nodes;
using PredictiveInterval = predictive_quiet_detail::FrequencyInterval;
using Clock = std::chrono::steady_clock;

constexpr std::size_t maximum_gap_rows = 65'536;
constexpr std::size_t maximum_slot_rows = 524'288;
constexpr std::size_t maximum_slots = 256;
constexpr std::size_t fixed_scratch = 64 * 1024;
constexpr double minimum_gap_seconds = 4;
constexpr double maximum_gap_separation_seconds = 360;
constexpr double maximum_frequency_tube_hz = .002;
constexpr double maximum_prediction_seconds = 720;
constexpr long double tau = 2 * std::numbers::pi_v<long double>;

bool supported(const ReceiverConfig& config) {
    return !config.helper_enabled && !config.physical_rf_retuning && config.time_varying_channel &&
        std::isfinite(config.sample_rate_hz) && config.sample_rate_hz > 0 &&
        config.sample_rate_hz * minimum_gap_seconds <= maximum_gap_rows &&
        std::isfinite(config.noise_variance) && config.noise_variance > 0;
}

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

long double centre(SampleRange range) {
    return static_cast<long double>(range.first) + .5L * (range.size() - 1);
}

Complex unit_phase(long double turns) {
    return std::polar(1., static_cast<double>(tau * std::remainder(turns, 1.L)));
}

template<class Sample>
void for_tone(std::size_t rows, double frequency_hz, double fs, std::stop_token stop, Sample sample) {
    const auto step = static_cast<long double>(frequency_hz) / fs;
    const auto midpoint = .5L * (rows - 1);
    const auto increment = unit_phase(step);
    Complex phase;
    for (std::size_t row = 0; row < rows; ++row) {
        if (!(row % 4096)) {
            check_cancelled(stop);
            phase = unit_phase((row - midpoint) * step);
        }
        sample(row, phase);
        phase *= increment;
    }
}

ToneMean tone_mean(SampleRange range, std::span<const Complex> input, double frequency_hz,
                   double fs, std::stop_token stop) {
    Complex sum{};
    for_tone(input.size(), frequency_hz, fs, stop, [&](std::size_t row, Complex phase) {
        sum += std::conj(phase) * input[row];
    });
    return {range, frequency_hz, sum / static_cast<double>(input.size())};
}

struct TrainingRanges {
    SampleRange proposal, confirmation;
};

TrainingRanges frequency_training_ranges(SampleRange gap) {
    const auto split = gap.first + gap.size() / 4;
    return {{gap.first, split}, {split, gap.stop}};
}

double confidence_tail() {
    // Five events per independent discovery/confirmation sequence: three
    // complex projection errors, one norm lower tail, one real cross-term.
    // Three fixed witnesses use at most 15 events. Both phase comparisons
    // share the same recent witness, so its events are charged only once.
    return std::log(15. / predictive_quiet_detail::frequency_exclusion_probability);
}

std::optional<double> phase_radius(const ToneMean& mean, double noise_variance, double b) {
    const double radial_bound = std::sqrt(noise_variance * b / mean.range.size());
    const double fraction = radial_bound / std::abs(mean.amplitude);
    if (!(fraction >= 0 && fraction < 1)) return {};
    return std::nextafter(static_cast<double>(std::asin(fraction) / tau), INFINITY);
}

ReceiverSlotPlan gap_plan(ReceiverSlotPlan plan, SampleRange range) {
    plan.first_sample = plan.quiet_first_sample = range.first;
    plan.stop_sample = plan.quiet_stop_sample = range.stop;
    return plan;
}

ReceiverConfig legacy_quiet_band(ReceiverConfig config) {
    // In Helper-off geometry this uses exactly the original legacy DATA bins
    // and block calendar through the native quiet-only scorer.
    config.time_varying_atmospheric_hz = 0;
    config.selective_modulation_rate_hz = 0;
    config.source_spectral_skirt_allowance_hz = 0;
    return config;
}

bool band_quiet(const ReceiverSlotScore& evidence) {
    return evidence.varying_quiet_modes && evidence.varying_quiet_energy <= evidence.varying_quiet_threshold;
}

void score_serial_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                       std::span<const Complex> input, ReceiverSlotScore& score, std::stop_token stop) {
    ReceiverExecutor executor(1);
    score_varying_quiet(config, slot, input, score, stop, 1, executor);
}

bool gap_band_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& format, const ToneMean& mean,
                    std::span<const Complex> input, std::stop_token stop) {
    std::vector<Complex> residual(input.size());
    for_tone(input.size(), mean.frequency_hz, config.sample_rate_hz, stop, [&](std::size_t row, Complex phase) {
        residual[row] = input[row] - phase * mean.amplitude;
    });
    ReceiverSlotScore quiet;
    score_serial_quiet(config, gap_plan(format, mean.range), residual, quiet, stop);
    return band_quiet(quiet);
}

std::optional<FrequencyWitness> calibrate_frequency(const ReceiverConfig& config, const ReceiverSlotPlan& format,
                                                    SampleRange gap, std::span<const Complex> input,
                                                    std::stop_token stop) {
    const auto ranges = frequency_training_ranges(gap);
    const auto proposed_samples = input.first(static_cast<std::size_t>(ranges.proposal.size()));
    const auto proposal = find_varying_line(config, gap_plan(format, ranges.proposal), proposed_samples, stop);
    if (!proposal) return {};
    const auto proposed_mean = tone_mean(ranges.proposal, proposed_samples, proposal->frequency_hz,
                                         config.sample_rate_hz, stop);
    if (!gap_band_quiet(config, format, proposed_mean, proposed_samples, stop)) return {};

    const auto frequency = proposal->frequency_hz;
    const auto confirmed = input.last(static_cast<std::size_t>(ranges.confirmation.size()));
    const auto mean = tone_mean(ranges.confirmation, confirmed, frequency, config.sample_rate_hz, stop);
    double norm = 0;
    for (const auto sample : confirmed) norm += std::norm(sample);
    const double rows = static_cast<double>(confirmed.size());
    const auto b = confidence_tail();
    const auto variance = config.noise_variance;
    const double radial_bound = std::sqrt(variance * b / rows);
    // For h = rows*|a|^2/variance, the Gaussian norm lower tail and the
    // signal/noise cross-term imply R >= rows - sqrt(2*rows*b) + h - 2*sqrt(h*b).
    const auto discriminant = b + norm / variance - rows + std::sqrt(2 * rows * b);
    if (!(std::isfinite(discriminant) && discriminant >= 0)) return {};
    const auto amplitude_upper = (std::sqrt(b) + std::sqrt(discriminant)) * std::sqrt(variance / rows);
    const auto ratio = std::nextafter((std::abs(mean.amplitude) - radial_bound) / amplitude_upper, -INFINITY);
    const auto lobe = predictive_quiet_detail::first_lobe_radius(config.sample_rate_hz, confirmed.size(), ratio);
    if (!lobe || std::abs(frequency) + *lobe >= config.sample_rate_hz / 2) return {};

    const auto split = confirmed.size() / 2;
    const auto middle = ranges.confirmation.first + split;
    const auto first = tone_mean({ranges.confirmation.first, middle}, confirmed.first(split), frequency,
                                 config.sample_rate_hz, stop);
    const auto last = tone_mean({middle, ranges.confirmation.stop}, confirmed.subspan(split), frequency,
                                config.sample_rate_hz, stop);
    const auto first_radius = phase_radius(first, variance, b);
    const auto last_radius = phase_radius(last, variance, b);
    const auto mean_radius = phase_radius(mean, variance, b);
    if (!first_radius || !last_radius || !mean_radius) return {};

    // Each centred Dirichlet response is positive in the certified lobe,
    // so a correlation phase refers to the corresponding window centre.
    // Keep all compatible aliases until the long-baseline intersection.
    const auto phase = static_cast<double>(std::arg(last.amplitude * std::conj(first.amplitude)) / tau);
    const auto separation = (centre(last.range) - centre(first.range)) / config.sample_rate_hz;
    const auto aliases = predictive_quiet_detail::phase_intervals(
        {std::nextafter(frequency - *lobe, -INFINITY), std::nextafter(frequency + *lobe, INFINITY)},
        phase, *first_radius + *last_radius, separation);
    FrequencyWitness result;
    if (!aliases || aliases->empty() || aliases->size() > result.intervals.size()) return {};
    result.mean = mean;
    result.phase_radius_turns = *mean_radius;
    result.interval_count = aliases->size();
    std::ranges::copy(*aliases, result.intervals.begin());
    return result;
}

struct ResidualTransform {
    const ToneMean observation;
    const Complex predicted;
    const double retained_gain;
    const double coherent_residual;

    void write(std::span<const Complex> input, std::span<Complex> output,
               double fs, std::stop_token stop) const {
        // Subtract past-gap mean, then whiten the additional rank-one
        // prediction variance. The correction retains the measured current
        // coefficient with nonzero gain. It does not fit a new nuisance mean.
        const auto subtraction = predicted + (1 - retained_gain) * (observation.amplitude - predicted);
        for_tone(input.size(), observation.frequency_hz, fs, stop, [&](std::size_t row, Complex phase) {
            output[row] = input[row] - subtraction * phase;
        });
    }
};

ResidualTransform predict_residual(const ReceiverConfig& config, const ToneMean& trained,
                                   SampleRange range, std::span<const Complex> input, std::stop_token stop) {
    const auto observed = tone_mean(range, input, trained.frequency_hz, config.sample_rate_hz, stop);
    const auto elapsed = (centre(range) - centre(trained.range)) / config.sample_rate_hz;
    const auto predicted = trained.amplitude * unit_phase(elapsed * trained.frequency_hz);
    const double gain_squared = predictive_quiet_detail::line_direction_gain_squared(trained.range.size(), range.size());
    return {observed, predicted, std::sqrt(gain_squared),
            std::norm(observed.amplitude - predicted) * range.size() * gain_squared / config.noise_variance};
}

} // namespace

SampleRange predictive_quiet_detail::public_training_gap(const PublicSlot& preceding, const PublicSlot& current,
                                                          double fs) {
    const auto begin = std::max(preceding.observation.stop, current.previous_signal_stop);
    const auto end = std::min(current.observation.first, preceding.next_signal_first);
    if (end <= begin || end - begin < fs * minimum_gap_seconds) return {};
    return {std::max(begin, end - std::min<std::uint64_t>(end, maximum_gap_rows)), end};
}

std::optional<std::vector<PredictiveInterval>> predictive_quiet_detail::frequency_pair_intervals(
    const FrequencyWitness& older, const FrequencyWitness& recent, double fs) {
    const auto separation = (centre(recent.mean.range) - centre(older.mean.range)) / fs;
    if (!(separation > 0 && separation <= maximum_gap_separation_seconds)) return {};
    const auto beat_phase = static_cast<double>(std::arg(recent.mean.amplitude * std::conj(older.mean.amplitude)) / tau);
    const auto radius = older.phase_radius_turns + recent.phase_radius_turns;
    std::vector<PredictiveInterval> candidates;
    for (const auto a : std::span(older.intervals).first(older.interval_count)) {
        for (const auto b : std::span(recent.intervals).first(recent.interval_count)) {
            const PredictiveInterval overlap{std::max(a.lower, b.lower), std::min(a.upper, b.upper)};
            if (overlap.lower > overlap.upper) continue;
            auto feasible = predictive_quiet_detail::phase_intervals(overlap, beat_phase, radius, separation);
            if (!feasible) return {};
            candidates.insert(candidates.end(), feasible->begin(), feasible->end());
        }
    }
    std::ranges::sort(candidates, {}, &PredictiveInterval::lower);
    std::size_t merged = 0;
    for (const auto next : candidates) {
        if (!merged || next.lower > candidates[merged - 1].upper) candidates[merged++] = next;
        else candidates[merged - 1].upper = std::max(candidates[merged - 1].upper, next.upper);
    }
    candidates.resize(merged);
    return candidates;
}

std::vector<PredictiveInterval> predictive_quiet_detail::intersect_frequency_pairs(
    std::span<const PredictiveInterval> first, std::span<const PredictiveInterval> second) {
    std::vector<PredictiveInterval> overlap;
    while (!first.empty() && !second.empty()) {
        const auto a = first.front(), b = second.front();
        const PredictiveInterval intersection{std::max(a.lower, b.lower), std::min(a.upper, b.upper)};
        if (intersection.lower <= intersection.upper) {
            if (overlap.empty() || overlap.back().upper < intersection.lower) overlap.push_back(intersection);
            else overlap.back().upper = std::max(overlap.back().upper, intersection.upper);
        }
        if (a.upper <= b.upper) first = first.subspan(1);
        else second = second.subspan(1);
    }
    return overlap;
}

std::optional<PredictiveInterval> predictive_quiet_detail::unique_frequency_interval(
    std::span<const PredictiveInterval> intervals) {
    if (intervals.size() != 1) return {};
    const auto tube = intervals.front();
    if (!(tube.upper > tube.lower && tube.upper - tube.lower <= maximum_frequency_tube_hz)) return {};
    return tube;
}

namespace {

bool separated_gaps_available(SampleRange older, SampleRange recent, double fs) {
    if (!older.size() || !recent.size()) return false;
    const auto separation = (centre(frequency_training_ranges(recent).confirmation) -
        centre(frequency_training_ranges(older).confirmation)) / fs;
    return separation > 0 && separation <= maximum_gap_separation_seconds;
}

std::size_t quiet_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    if (!supported(config) || slot.stop_sample <= slot.first_sample) return 0;
    const auto rows = slot.stop_sample - slot.first_sample;
    if (rows > maximum_slot_rows) return 0;
    const auto input_and_copy = checked_multiply(static_cast<std::size_t>(rows), 2 * sizeof(Complex) + 1);
    return checked_add(checked_add(input_and_copy, varying_quiet_workspace(config, slot, 1)), fixed_scratch);
}

} // namespace

HfPredictiveQuiet::HfPredictiveQuiet(const PublicProfile& profile, const ReceiverConfig& config,
                                   std::uint64_t slot_limit) : training_config_(config) {
    if (!slot_limit || slot_limit > maximum_slots) throw datapump::Error("invalid predictive quiet slot limit");
    entries_.resize(static_cast<std::size_t>(slot_limit));
    training_bytes_ = checked_add(sizeof(*this), checked_multiply(entries_.capacity(), sizeof(Entry)));
    if (!supported(config)) return;
    training_slot_ = make_receiver_slot_plan(profile, config, 0);
    auto preceding = profile.slot(0);
    std::size_t largest_gap = 0, largest_stage = 0;
    for (std::size_t index = 1; index < entries_.size(); ++index) {
        const auto current = profile.slot(index);
        const auto gap = predictive_quiet_detail::public_training_gap(preceding, current, config.sample_rate_hz);
        entries_[index].gap = gap;
        if (gap.size()) {
            largest_gap = std::max(largest_gap, static_cast<std::size_t>(gap.size()));
            const auto whole = gap_plan(training_slot_, gap);
            const auto proposal = gap_plan(training_slot_, frequency_training_ranges(gap).proposal);
            const auto split = gap.stop - gap.size() / 3;
            largest_stage = std::max({largest_stage, varying_line_workspace(config, proposal),
                varying_quiet_workspace(config, proposal, 1),
                varying_quiet_workspace(config, whole, 1),
                varying_quiet_workspace(config, gap_plan(training_slot_, {gap.first, split}), 1),
                varying_quiet_workspace(config, gap_plan(training_slot_, {split, gap.stop}), 1)});
        }
        preceding = current;
    }
    // Select two or three witnesses from the immutable public geometry.
    // A calibration failure can never change this choice.
    for (std::size_t index = 3; index < entries_.size(); ++index) {
        auto& result = entries_[index].result;
        const bool separated = index >= 5 && separated_gaps_available(entries_[index - 4].gap,
            entries_[index - 1].gap, config.sample_rate_hz);
        result.frequency_first_gap_slot = index - (separated ? 4 : 2);
        result.frequency_second_gap_slot = index - 1;
        result.frequency_witness_count = separated ? 3 : 2;
        result.frequency_baseline = separated ? "separated_gaps" : "adjacent_gaps";
        result.frequency_first = entries_[result.frequency_first_gap_slot].gap;
        result.frequency_second = entries_[result.frequency_second_gap_slot].gap;
        const auto first = frequency_training_ranges(result.frequency_first);
        const auto second = frequency_training_ranges(result.frequency_second);
        result.frequency_first_proposal = first.proposal;
        result.frequency_first_confirmation = first.confirmation;
        result.frequency_second_proposal = second.proposal;
        result.frequency_second_confirmation = second.confirmation;
        if (separated) {
            result.frequency_middle_gap_slot = index - 2;
            result.frequency_middle = entries_[result.frequency_middle_gap_slot].gap;
            const auto middle = frequency_training_ranges(result.frequency_middle);
            result.frequency_middle_proposal = middle.proposal;
            result.frequency_middle_confirmation = middle.confirmation;
        }
    }
    if (largest_gap) {
        gap_samples_.reserve(largest_gap);
        const auto sample_bytes = checked_multiply(checked_add(gap_samples_.capacity(), largest_gap), sizeof(Complex));
        // The training FFT and residual checks run in sequence. Both
        // staging vectors remain charged through their possible overlap.
        training_bytes_ = checked_add(training_bytes_, checked_add(fixed_scratch,
            checked_add(sample_bytes, largest_stage)));
    }
}

void HfPredictiveQuiet::observe(std::uint64_t first, std::span<const Complex> input,
                               std::span<const std::uint8_t> valid, std::stop_token stop) {
    check_cancelled(stop);
    if (first != input_stop_ || input.size() > UINT64_MAX - first ||
        (!valid.empty() && input.size() != valid.size()))
        throw datapump::Error("predictive quiet input must be contiguous with its validity mask");
    const auto begin = Clock::now();
    const auto stop_sample = first + input.size();
    while (next_gap_ < entries_.size()) {
        const auto gap = entries_[next_gap_].gap;
        if (!gap.size()) {
            ++next_gap_;
            continue;
        }
        if (gap.first >= stop_sample) break;
        const auto from = std::max(gap.first, first);
        const auto to = std::min(gap.stop, stop_sample);
        if (to > from) {
            if (from != gap.first + gap_samples_.size())
                throw datapump::Error("predictive quiet incomplete public training interval");
            const auto count = static_cast<std::size_t>(to - from);
            const auto offset = static_cast<std::size_t>(from - first);
            const auto rows = input.subspan(offset, count);
            if (!valid.empty() && !std::ranges::all_of(valid.subspan(offset, count), [](auto x) { return x == 1; }))
                gap_valid_ = false;
            if (!std::ranges::all_of(rows, finite)) gap_valid_ = false;
            gap_samples_.insert(gap_samples_.end(), rows.begin(), rows.end());
        }
        if (gap.stop > stop_sample) break;
        train(next_gap_, stop);
        ++next_gap_;
        gap_samples_.clear();
        gap_valid_ = true;
    }
    input_stop_ = stop_sample;
    training_seconds_ += std::chrono::duration<double>(Clock::now() - begin).count();
}

void HfPredictiveQuiet::train(std::size_t index, std::stop_token stop) {
    auto& entry = entries_[index];
    auto& result = entry.result;
    if (!gap_valid_ || gap_samples_.size() != entry.gap.size()) {
        result.training_status = "invalid_public_training_gap";
        return;
    }
    const auto input = std::span<const Complex>(gap_samples_);
    entry.frequency_fit = calibrate_frequency(training_config_, training_slot_, entry.gap, input, stop);
    if (index < 3) return;
    const auto& older = entries_[result.frequency_first_gap_slot].frequency_fit;
    const auto& recent = entries_[result.frequency_second_gap_slot].frequency_fit;
    const bool separated = result.frequency_witness_count == 3;
    const auto& adjacent = separated ? entries_[result.frequency_middle_gap_slot].frequency_fit : older;
    if (!older || !recent || !adjacent) {
        result.training_status = "no_clean_coherent_frequency_gaps";
        return;
    }
    const auto fs = training_config_.sample_rate_hz;
    const auto short_intervals = predictive_quiet_detail::frequency_pair_intervals(*adjacent, *recent, fs);
    if (short_intervals) result.frequency_adjacent_pair_intervals = short_intervals->size();
    std::optional<PredictiveInterval> tube;
    if (separated) {
        const auto long_intervals = predictive_quiet_detail::frequency_pair_intervals(*older, *recent, fs);
        if (long_intervals) result.frequency_older_pair_intervals = long_intervals->size();
        if (long_intervals && short_intervals) {
            // Neither pair is required to be unique. Under the shared
            // stationary line model both sets contain the same frequency.
            const auto joint = predictive_quiet_detail::intersect_frequency_pairs(*long_intervals, *short_intervals);
            result.frequency_joint_intervals = joint.size();
            tube = predictive_quiet_detail::unique_frequency_interval(joint);
        }
    } else if (short_intervals) {
        result.frequency_joint_intervals = short_intervals->size();
        tube = predictive_quiet_detail::unique_frequency_interval(*short_intervals);
    }
    if (!tube) {
        result.training_status = "frequency_gaps_inconsistent_aliased_or_wide";
        return;
    }
    const auto validation_rows = input.size() / 3;
    const auto training_rows = input.size() - validation_rows;
    result.amplitude = {entry.gap.first, entry.gap.first + training_rows};
    result.validation = {result.amplitude.stop, entry.gap.stop};
    result.frequency_lower_hz = tube->lower;
    result.frequency_upper_hz = tube->upper;
    result.frequency_confidence_exclusion_probability = predictive_quiet_detail::frequency_exclusion_probability;
    result.frequency_hz = std::midpoint(tube->lower, tube->upper);
    const auto train_input = input.first(training_rows);
    const auto fit = tone_mean(result.amplitude, train_input, result.frequency_hz, training_config_.sample_rate_hz, stop);
    result.amplitude_magnitude = std::abs(fit.amplitude);
    const auto threshold = receiver_operational_threshold();
    if (!(std::norm(fit.amplitude) * training_rows / training_config_.noise_variance >= threshold) ||
        !gap_band_quiet(training_config_, training_slot_, fit, train_input, stop)) {
        result.training_status = "amplitude_gap_not_clean_coherent_line";
        return;
    }
    const auto validation = input.last(validation_rows);
    const auto transform = predict_residual(training_config_, fit, result.validation, validation, stop);
    result.validation_line_residual = transform.coherent_residual;
    if (!(result.validation_line_residual <= threshold)) {
        result.training_status = "held_gap_coherent_prediction_failed";
        return;
    }
    std::vector<Complex> whitened(validation_rows);
    transform.write(validation, whitened, training_config_.sample_rate_hz, stop);
    ReceiverSlotScore quiet;
    score_serial_quiet(training_config_, gap_plan(training_slot_, result.validation), whitened, quiet, stop);
    result.validation_energy = quiet.varying_quiet_energy;
    result.validation_threshold = quiet.varying_quiet_threshold;
    if (!band_quiet(quiet)) {
        result.training_status = "held_gap_has_unexplained_band_energy";
        return;
    }
    Prediction predictions;
    for (std::size_t i = 0; i < predictions.nodes.size(); ++i) {
        const auto frequency = std::lerp(tube->lower, tube->upper, static_cast<double>(i) / (predictions.nodes.size() - 1));
        predictions.nodes[i] = tone_mean(result.amplitude, train_input, frequency, training_config_.sample_rate_hz, stop);
    }
    for (const auto sample : train_input) predictions.training_mean_magnitude += std::abs(sample);
    predictions.training_mean_magnitude /= train_input.size();
    if (!std::isfinite(predictions.training_mean_magnitude)) {
        result.training_status = "nonfinite_training_norm";
        return;
    }
    entry.prediction = predictions;
    result.training_status = "validated_prior_stationary_line";
}

std::size_t HfPredictiveQuiet::workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                        unsigned workers, bool overlap) const {
    const auto native = overlap ? receiver_slot_overlap_workspace(config, slot, workers) :
                                  receiver_slot_workspace(config, slot, workers);
    return std::max(native, quiet_workspace(config, slot));
}

ReceiverSlotScore HfPredictiveQuiet::score(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                          const Crypto& key, std::span<const Complex> input,
                                          std::span<const std::uint8_t> valid, unsigned workers,
                                          std::size_t lease, std::stop_token stop, ReceiverExecutor* executor) {
    const auto quiet_bytes = quiet_workspace(config, slot);
    if (quiet_bytes > lease) throw ReceiverWorkspaceError("predictive quiet exceeds its admitted memory lease");
    auto result = score_slot(config, slot, key, input, valid, workers, lease, stop, executor);
    consider(config, slot, input, result, stop);
    if (entries_.at(static_cast<std::size_t>(slot.index)).result.attempted)
        result.workspace_peak_bytes = std::max(result.workspace_peak_bytes, quiet_bytes);
    return result;
}

void HfPredictiveQuiet::consider(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                std::span<const Complex> input, ReceiverSlotScore& score, std::stop_token stop) {
    check_cancelled(stop);
    auto& entry = entries_.at(static_cast<std::size_t>(slot.index));
    auto& evidence = entry.result;
    evidence.raw_quiet_energy = score.quiet_energy;
    evidence.raw_quiet_threshold = score.quiet_threshold;
    evidence.raw_line_frequency_hz = score.varying_line_frequency_hz;
    evidence.raw_line_log_evidence = score.varying_line_log_evidence;
    const bool excess_energy = score.quiet_energy > score.quiet_threshold ||
        (score.varying_quiet_modes && score.varying_quiet_energy > score.varying_quiet_threshold);
    // Any native current-slot line conditioning has already been included in
    // the DATA score. The independent quiet test still receives the raw IQ.
    if (!supported(config) || config.sample_rate_hz != training_config_.sample_rate_hz ||
        config.noise_variance != training_config_.noise_variance || !entry.prediction ||
        score.decision != SlotDecision::ambiguous || !score.complete || !score.all_valid ||
        !score.zero_h0_verified || !score.noise_statistics_valid || !score.quiet_modes ||
        score.operational_passes_conditional_bound || !(score.operational_log_e < score.operational_threshold) ||
        !excess_energy)
        return;
    const SampleRange aperture{slot.quiet_first_sample, slot.quiet_stop_sample};
    const auto& prediction = *entry.prediction;
    const auto trained = prediction.nodes.front().range;
    if (slot.stop_sample <= slot.first_sample || input.size() != slot.stop_sample - slot.first_sample ||
        input.size() > maximum_slot_rows || aperture.first < slot.first_sample || aperture.stop > slot.stop_sample ||
        aperture.stop <= aperture.first || trained.stop > slot.first_sample ||
        (aperture.stop - trained.first) / config.sample_rate_hz > maximum_prediction_seconds)
        return;
    const auto begin = Clock::now();
    const auto finish = [&] { evidence.extra_seconds = std::chrono::duration<double>(Clock::now() - begin).count(); };
    evidence.attempted = true;
    const auto offset = static_cast<std::size_t>(aperture.first - slot.first_sample);
    const auto aperture_input = input.subspan(offset, static_cast<std::size_t>(aperture.size()));
    evidence.line_direction_gain_squared = predictive_quiet_detail::line_direction_gain_squared(trained.size(), aperture.size());
    const auto gain = std::sqrt(evidence.line_direction_gain_squared);

    double max_frequency_step = 0;
    for (std::size_t i = 1; i < prediction.nodes.size(); ++i)
        max_frequency_step = std::max(max_frequency_step, prediction.nodes[i].frequency_hz - prediction.nodes[i - 1].frequency_hz);
    double input_mean_magnitude = 0;
    for (const auto sample : aperture_input) input_mean_magnitude += std::abs(sample);
    input_mean_magnitude /= aperture_input.size();
    const auto time_span = static_cast<long double>(aperture.size() - 1) / config.sample_rate_hz;
    const auto cross_span = static_cast<long double>(aperture.stop - 1 - trained.first) / config.sample_rate_hz;
    const auto centre_span = (centre(aperture) - trained.first) / config.sample_rate_hz;
    const auto step_radians = tau * max_frequency_step;
    const auto factor = step_radians * step_radians / 8 * std::sqrt(aperture.size() / config.noise_variance);
    // Chord interpolation error is h^2/8 times a bound on the second
    // derivative. For r(f) = y - (1-g) P_f y - g u_f A_train(f), the
    // time differences in P_f span the aperture, and the independent
    // predictor spans from its oldest amplitude sample to the aperture end.
    // Every fixed native band projection has operator norm at most one.
    evidence.band_interpolation_norm = static_cast<double>(factor *
        ((1 - gain) * input_mean_magnitude * time_span * time_span +
         gain * prediction.training_mean_magnitude * cross_span * cross_span));
    // The coherent component is centred at the current aperture, rather
    // than evaluated at every current time. Its two derivative spans shrink
    // accordingly, while the independent earlier amplitude still varies in f.
    evidence.coherent_interpolation_norm = static_cast<double>(factor * gain *
        (input_mean_magnitude * time_span * time_span / 4 +
         prediction.training_mean_magnitude * centre_span * centre_span));
    if (!std::isfinite(evidence.band_interpolation_norm) || !std::isfinite(evidence.coherent_interpolation_norm)) {
        evidence.status = "nonfinite_frequency_tube_remainder";
        finish();
        return;
    }

    std::vector<ResidualTransform> transforms;
    transforms.reserve(frequency_nodes);
    double largest_coherent_norm = 0;
    for (const auto& node : prediction.nodes) {
        transforms.push_back(predict_residual(config, node, aperture, aperture_input, stop));
        if (!std::isfinite(transforms.back().coherent_residual)) {
            evidence.status = "nonfinite_frequency_tube_residual";
            finish();
            return;
        }
        largest_coherent_norm = std::max(largest_coherent_norm, std::sqrt(transforms.back().coherent_residual));
        ++evidence.frequency_coherent_nodes;
    }
    const auto envelope_energy = [](double node_norm, double remainder) {
        const auto upper_norm = node_norm + remainder + 1e-9 * (1 + node_norm);
        return upper_norm * upper_norm;
    };
    evidence.coherent_threshold = receiver_operational_threshold();
    evidence.coherent_residual = envelope_energy(largest_coherent_norm, evidence.coherent_interpolation_norm);
    if (!(evidence.coherent_residual <= evidence.coherent_threshold)) {
        evidence.status = "coherent_prediction_residual_exceeds_threshold";
        finish();
        return;
    }

    std::vector<Complex> whitened(input.begin(), input.end());
    double largest_legacy_norm = 0, largest_expanded_norm = 0;
    const auto legacy_config = legacy_quiet_band(config);
    for (const auto& transform : transforms) {
        transform.write(aperture_input, std::span<Complex>(whitened).subspan(offset, aperture_input.size()),
                        config.sample_rate_hz, stop);
        ReceiverSlotScore legacy, expanded;
        score_serial_quiet(legacy_config, slot, whitened, legacy, stop);
        score_serial_quiet(config, slot, whitened, expanded, stop);
        evidence.legacy_modes = legacy.varying_quiet_modes;
        evidence.legacy_threshold = legacy.varying_quiet_threshold;
        evidence.expanded_modes = expanded.varying_quiet_modes;
        evidence.expanded_threshold = expanded.varying_quiet_threshold;
        if (evidence.legacy_modes != score.quiet_modes || !evidence.expanded_modes ||
            evidence.legacy_threshold != score.quiet_threshold ||
            (score.varying_quiet_modes && evidence.expanded_modes != score.varying_quiet_modes)) {
            evidence.status = "unavailable_public_quiet_band_modes";
            finish();
            return;
        }
        largest_legacy_norm = std::max(largest_legacy_norm, std::sqrt(legacy.varying_quiet_energy));
        largest_expanded_norm = std::max(largest_expanded_norm, std::sqrt(expanded.varying_quiet_energy));
        ++evidence.frequency_band_nodes;
    }
    evidence.legacy_energy = envelope_energy(largest_legacy_norm, evidence.band_interpolation_norm);
    evidence.expanded_energy = envelope_energy(largest_expanded_norm, evidence.band_interpolation_norm);
    if (!(evidence.legacy_energy <= evidence.legacy_threshold && evidence.expanded_energy <= evidence.expanded_threshold)) {
        evidence.status = "unexplained_energy_in_public_quiet_bands";
        finish();
        return;
    }

    // The real stream still independently validates the complete score and
    // commits the same public two-quiet-slot sequence. Keep raw evidence in
    // the appraisal receipt and change only the measured quiet statistic.
    score.decision = SlotDecision::quiet;
    score.reason = "scored_public_quiet_with_independent_line_prediction";
    score.quiet_energy = evidence.legacy_energy;
    score.quiet_threshold = evidence.legacy_threshold;
    score.varying_quiet_modes = evidence.expanded_modes;
    score.varying_quiet_energy = evidence.expanded_energy;
    score.varying_quiet_threshold = evidence.expanded_threshold;
    score.varying_quiet_status = "scored_public_varying_quiet_with_independent_line_prediction";
    evidence.status = "predictive_quiet_passed_both_bands";
    evidence.accepted = true;
    finish();
}

const HfPredictiveQuietResult& HfPredictiveQuiet::result(std::uint64_t index) const {
    return entries_.at(static_cast<std::size_t>(index)).result;
}

} // namespace appraisal
