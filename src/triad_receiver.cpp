#include "datapump/triad_receiver.hpp"

#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_helper.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace datapump::triad::v3 {
namespace {
using namespace receiver_detail;
using Clock = std::chrono::steady_clock;
constexpr std::size_t receipts_bytes = 64 * 1024;
constexpr std::size_t maximum_fine_coordinates = 64;
constexpr std::size_t maximum_helper_intervals = 64;

void require(bool valid, const char* reason) {
    if (!valid) throw Error(reason);
}

std::size_t validate(const ReceiverConfig& c, const ReceiverSlotPlan& s, unsigned workers) {
    require(workers > 0, "Robust slot requires an explicit positive worker share");
    require(std::isfinite(c.sample_rate_hz) && c.sample_rate_hz >= 1 && c.sample_rate_hz <= 120e6 &&
        std::floor(c.sample_rate_hz) == c.sample_rate_hz, "invalid Robust receiver sample rate");
    require(std::isfinite(c.noise_variance) && c.noise_variance > 0 &&
        std::isfinite(c.data_basis_gain) && c.data_basis_gain > 0, "invalid Robust receiver gain/noise model");
    const std::array nonnegative{
        c.data_if_hz, c.frequency_bound_hz, c.clock_bound_ppm, c.endpoint_clock_bound_ppm,
        c.spread_delay_seconds, c.random_doppler_width_hz, c.delay_motion_doppler_bound_hz,
        c.delay_excursion_seconds, c.capture_tail_bound_seconds, c.relative_phase_diffusion_rad_sqrt_s,
        c.selective_modulation_rate_hz, c.selective_slow_depth, c.selective_fast_depth,
        c.source_spectral_skirt_allowance_hz, c.time_varying_common_doppler_hz, c.time_varying_atmospheric_hz,
        c.time_varying_minimum_bit_log_separation};
    require(std::all_of(nonnegative.begin(), nonnegative.end(), [](double x) { return x >= 0 && std::isfinite(x); }) &&
        std::isfinite(c.spread_doppler_hz) && c.data_if_hz <= 120e6 && c.clock_bound_ppm < 1e6 &&
        c.endpoint_clock_bound_ppm < 1e6 && std::isfinite(c.frequency_bound_hz + c.time_varying_atmospheric_hz) &&
        c.time_varying_common_doppler_hz <= c.frequency_bound_hz + c.time_varying_atmospheric_hz,
        "invalid Robust public channel bounds");
    if (c.helper_enabled) {
        const std::array frequencies{c.helper.low_frequency_hz, c.helper.frequency_hz,
                                    c.helper.auxiliary_frequency_hz};
        require(std::all_of(frequencies.begin(), frequencies.end(),
            [](double x) { return x > 0 && std::isfinite(x); }), "invalid Robust Helper placement");
    }
    require(s.first_sample < s.stop_sample && s.stop_sample - s.first_sample <= SIZE_MAX &&
        s.first_sample <= s.quiet_first_sample && s.quiet_first_sample <= s.quiet_stop_sample &&
        s.quiet_stop_sample <= s.stop_sample, "invalid Robust public receiver window");
    require(s.format.epoch >= 6 && s.format.epoch <= UINT32_MAX - 6, "invalid Robust waveform epoch");
    require(std::isfinite(s.arrival_lower_seconds) && std::isfinite(s.arrival_upper_seconds) &&
        s.arrival_lower_seconds <= s.arrival_upper_seconds && std::isfinite(s.window_capture_lead_seconds) &&
        std::isfinite(s.source_start_seconds) && s.source_start_seconds >= 0,
        "invalid Robust public arrival calendar");
    if (c.helper_enabled) {
        require(std::isfinite(s.helper_begin_seconds) && std::isfinite(s.helper_end_seconds) &&
            0 <= s.helper_begin_seconds && s.helper_begin_seconds <= s.source_start_seconds &&
            s.source_start_seconds < s.helper_end_seconds, "invalid Robust public Helper calendar");
    }
    (void)geometry(s.format);
    return static_cast<std::size_t>(s.stop_sample - s.first_sample);
}

// The sequential lease runs Helper acquisition, the coarse search and fine
// tracking one after another. The overlapped lease also holds acquisition
// scratch beside coarse preparation. The two are equal when nothing overlaps
// or another stage already needs more.
struct StageWorkspace {
    std::size_t input, helper, coarse, fine, total;
    bool overlappable;
    std::size_t overlapped_coarse, overlapped_total;
    VaryingFinePlan varying;
    DriftPlan drift;
    VaryingFinePlan coherent_gain;
    std::size_t line;
    VaryingFinePlan line_varying;
    DriftPlan line_drift;
    VaryingFinePlan line_coherent_gain;
    std::size_t evidence_supplement = 0;
};

StageWorkspace plan_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                              bool quote_overlap, const SlotEvidenceSupplement* supplement = nullptr) {
    const auto rows = validate(config, slot, workers);
    std::size_t evidence_supplement = 0;
    if (supplement) {
        require(std::isfinite(supplement->fixed_weight) && supplement->fixed_weight >= 0 &&
            supplement->fixed_weight < 1, "invalid Robust evidence supplement weight");
        if (supplement->fixed_weight > 0) {
            require(static_cast<bool>(supplement->supported),
                    "Robust evidence supplement requires a public support callback");
            if (supplement->supported(config, slot)) {
                require(supplement->workspace && supplement->score,
                        "Robust evidence supplement requires workspace and score callbacks");
                evidence_supplement = supplement->workspace(config, slot, workers);
                require(evidence_supplement > 0,
                        "Robust supported evidence supplement requires a positive workspace quote");
            }
        }
    }
    const auto input = checked_multiply(rows, sizeof(Complex) + sizeof(std::uint8_t));
    const auto helper = helper_workspace(config, slot);
    const auto varying = varying_fine_plan(config, slot, workers);
    const auto drift = drift_plan(config, slot, workers);
    const auto coherent_gain = varying_fine_plan(config, slot, workers, false, true);
    auto search = config;
    search.time_varying_channel = varying.block_rows || drift.workspace_bytes || coherent_gain.block_rows;
    const auto coarse = coarse_workspace(search, slot, workers);
    const auto fine = fine_workspace(config, slot, workers);
    const auto varying_quiet = varying_quiet_workspace(config, slot);
    const auto line = search.time_varying_channel ? varying_line_workspace(config, slot) : 0;
    const auto line_varying = line ? varying_fine_plan(config, slot, workers, true) : VaryingFinePlan{};
    const auto line_drift = line && drift.workspace_bytes ? drift_plan(config, slot, workers, true) : DriftPlan{};
    const auto line_coherent_gain = line && coherent_gain.block_rows ?
        varying_fine_plan(config, slot, workers, true, true) : VaryingFinePlan{};
    const auto supplemental = std::max({varying.workspace_bytes, drift.workspace_bytes,
        coherent_gain.workspace_bytes, varying_quiet, line, line_varying.workspace_bytes,
        line_drift.workspace_bytes, line_coherent_gain.workspace_bytes, evidence_supplement});
    const auto lease = [&](std::size_t stage) { return checked_add(checked_add(input, receipts_bytes), stage); };
    // One lane runs acquisition and preparation in turn: nothing coexists.
    const bool overlappable = quote_overlap && workers > 1 && helper && coarse_defers_frequency_grid(config, slot);
    const auto overlapped_coarse = overlappable ?
        coarse_workspace(search, slot, workers, {helper, helper_widest_wave}) : coarse;
    return {input, helper, coarse, fine,
            lease(std::max({helper, coarse, fine, supplemental})),
            overlappable, overlapped_coarse,
            lease(std::max({helper, overlapped_coarse, fine, supplemental})),
            varying, drift, coherent_gain, line, line_varying, line_drift, line_coherent_gain,
            evidence_supplement};
}

double round_even(double value) {
    const auto floor = std::floor(value), fraction = value - floor;
    return floor + (fraction > .5 || (fraction == .5 && std::abs(std::fmod(floor, 2)) == 1.));
}

FineCoordinate snap(FineCoordinate coordinate, const AdmissionGrid& grid) {
    const auto epoch = coordinate.epoch_offset_seconds;
    require(std::find(receiver_epoch_offsets.begin(), receiver_epoch_offsets.end(), epoch) !=
        receiver_epoch_offsets.end() && std::isfinite(coordinate.arrival_seconds) &&
        std::isfinite(coordinate.frequency_hz) && std::isfinite(coordinate.clock_ppm),
        "Robust coarse proposal is outside its declared public grid");
    if (grid.arrival_intervals) {
        const auto step = (grid.upper - grid.lower) / grid.arrival_intervals;
        const auto index = std::clamp(round_even((coordinate.arrival_seconds - grid.lower) / step),
                                      0., grid.arrival_intervals);
        coordinate.arrival_seconds = grid.lower + index * step;
    } else {
        coordinate.arrival_seconds = grid.lower;
    }
    const auto frequency_index = std::clamp(round_even(coordinate.frequency_hz / grid.frequency_step),
                                            -grid.frequency_radius, grid.frequency_radius);
    coordinate.frequency_hz = frequency_index * grid.frequency_step;
    if (grid.clock_bound) {
        const auto index = std::clamp(round_even((coordinate.clock_ppm + grid.clock_bound) * 8 /
                                                (2 * grid.clock_bound)), 0., 8.);
        coordinate.clock_ppm = -grid.clock_bound + index * (2 * grid.clock_bound / 8);
    } else {
        coordinate.clock_ppm = 0;
    }
    return coordinate;
}

} // namespace

std::span<const FrequencyInterval> receiver_detail::public_frequency_intervals(
        const ReceiverConfig& config, const HelperResult& helper, FrequencyInterval& fallback) {
    require(helper.intervals.size() <= maximum_helper_intervals &&
        helper.narrowed == !helper.intervals.empty(), "invalid Robust Helper frequency assistance");
    for (const auto& interval : helper.intervals)
        require(std::isfinite(interval.lower_hz) && std::isfinite(interval.upper_hz) &&
            -config.frequency_bound_hz <= interval.lower_hz && interval.lower_hz <= interval.upper_hz &&
            interval.upper_hz <= config.frequency_bound_hz, "Robust Helper interval exceeds the public frequency prior");
    fallback = {-config.frequency_bound_hz, config.frequency_bound_hz};
    return helper.narrowed ? std::span<const FrequencyInterval>(helper.intervals) :
        std::span<const FrequencyInterval>(&fallback, 1);
}

FinePreparation receiver_detail::prepare_fine_coordinates(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const CoarseResult& coarse) {
    const auto& receipt = coarse.work;
    require(receipt.state == CoarseState::finished && receipt.plan.total_tiles > 0 &&
            receipt.completed_tiles == receipt.plan.total_tiles && receipt.plan.public_slot == slot.index &&
            receipt.plan.pattern_epoch == slot.format.epoch && receipt.plan.symbol_ordinal == slot.format.symbol_ordinal &&
            receipt.plan.first_sample == slot.first_sample && receipt.plan.stop_sample == slot.stop_sample &&
            receipt.refined_coordinates == coarse.coordinates.size(),
            "Robust fine preparation requires a finished coarse search for the same public slot");
    require(!coarse.coordinates.empty() && coarse.coordinates.size() <= maximum_fine_coordinates,
            "Robust coarse refinements require a complete bounded fine bank");
    require(std::isfinite(coarse.maximum_frequency_step_hz) && coarse.maximum_frequency_step_hz > 0,
            "invalid Robust coarse frequency resolution");
    FinePreparation result{{slot.arrival_lower_seconds, slot.arrival_upper_seconds,
        config.frequency_bound_hz, config.clock_bound_ppm, std::min(.01, coarse.maximum_frequency_step_hz)}, {}, {}, {}, {}};
    result.coordinates.reserve(coarse.coordinates.size());
    for (const auto& proposed : coarse.coordinates) {
        const auto coordinate = snap(proposed, result.grid);
        if (std::find(result.coordinates.begin(), result.coordinates.end(), coordinate) == result.coordinates.end())
            result.coordinates.push_back(coordinate);
    }
    if (!coarse.varying_coordinates.empty()) {
        require(coarse.varying_coordinates.size() <= 12 && result.coordinates.size() <= 12,
                "Robust varying proposals exceed the public coordinate cap");
        result.varying_coordinates = result.coordinates;
        const AdmissionGrid varying_grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds,
            config.frequency_bound_hz + config.time_varying_atmospheric_hz, config.clock_bound_ppm,
            result.grid.frequency_step);
        for (const auto& proposed : coarse.varying_coordinates) {
            const auto coordinate = snap(proposed, varying_grid);
            if (std::ranges::find(result.varying_coordinates, coordinate) == result.varying_coordinates.end())
                result.varying_coordinates.push_back(coordinate);
        }
    }
    require(coarse.epoch_timing_coordinates.size() <= 2 * receiver_epoch_offsets.size() &&
            coarse.segmented_timing_coordinates.size() <= 8,
            "Robust epoch timing proposals exceed the public cap");
    const auto append_timing = [&](const auto& proposed, auto& destination) {
        auto coordinate = snap(proposed, result.grid);
        coordinate.frequency_hz = 0;
        if (std::ranges::find(destination, coordinate) == destination.end()) destination.push_back(coordinate);
    };
    for (const auto& proposed : coarse.segmented_timing_coordinates)
        append_timing(proposed, result.segmented_timing_coordinates);
    result.epoch_timing_coordinates = result.segmented_timing_coordinates;
    for (const auto& proposed : coarse.epoch_timing_coordinates)
        append_timing(proposed, result.epoch_timing_coordinates);
    return result;
}

namespace {
double log_add(double first, double second) {
    if (first == -std::numeric_limits<double>::infinity()) return second;
    if (second == -std::numeric_limits<double>::infinity()) return first;
    return std::max(first, second) + std::log1p(std::exp(-std::abs(first - second)));
}

// The branch weights are fixed before observing this slot. Coordinates
// selected from this same observation retain their original public prior.
// A skipped branch contributes zero e-value, never renormalized weight.
std::array<double, 2> varying_operational_scores(const ReceiverSlotScore& score, const AdmissionGrid& grid,
        const AdmissionGrid* varying_grid, const DriftPlan* drift, bool include_affine = true,
        const AdmissionGrid* coherent_gain_grid = nullptr) {
    require(score.fine_candidates > 0, "Robust fine evidence requires a public coordinate bank");
    const double log_mass = std::log(static_cast<double>(score.fine_candidates)) - grid.log_count();
    auto operational = score.bit_evidence;
    if (!include_affine) operational.fill(-std::numeric_limits<double>::infinity());
    if (!varying_grid && !drift && !coherent_gain_grid) {
        for (auto& bit : operational) bit += log_mass;
    } else {
        const auto varying_log_mass = varying_grid && score.varying_candidates ?
            std::log(static_cast<double>(score.varying_candidates)) - varying_grid->log_count() :
            -std::numeric_limits<double>::infinity();
        const double timing_log_count = grid.log_count() - std::log1p(2 * grid.frequency_radius);
        const auto drift_log_mass = drift && score.drift_timing_atoms ?
            std::log(static_cast<double>(score.drift_timing_atoms)) - timing_log_count :
            -std::numeric_limits<double>::infinity();
        const auto coherent_gain_log_mass = coherent_gain_grid && score.coherent_gain_candidates ?
            std::log(static_cast<double>(score.coherent_gain_candidates)) - coherent_gain_grid->log_count() :
            -std::numeric_limits<double>::infinity();
        const auto branch_penalty = std::log(1. + (varying_grid != nullptr) + (drift != nullptr) +
                                               (coherent_gain_grid != nullptr));
        for (std::size_t bit = 0; bit < 2; ++bit) {
            operational[bit] = log_add(operational[bit] + log_mass,
                score.varying_bit_evidence[bit] + varying_log_mass);
            operational[bit] = log_add(operational[bit], score.drift_bit_evidence[bit] + drift_log_mass);
            operational[bit] = log_add(operational[bit], score.coherent_gain_bit_evidence[bit] +
                coherent_gain_log_mass) - branch_penalty;
        }
    }
    return operational;
}

void classify_operational_scores(ReceiverSlotScore& score, const std::array<double, 2>& operational,
                                 double minimum_separation, std::optional<double> combined = std::nullopt) {
    score.operational_threshold = receiver_operational_threshold();
    score.operational_log_e = combined.value_or(log_add(operational[0], operational[1]) - std::numbers::ln2);
    score.operational_bit_evidence = operational;
    score.operational_bit_log_separation = std::abs(operational[0] - operational[1]);
    score.operational_minimum_bit_log_separation = minimum_separation;
    require(std::isfinite(score.operational_log_e), "Robust DATA evidence is nonfinite");
    score.operational_passes_conditional_bound = score.complete && score.zero_h0_verified &&
        score.operational_log_e >= score.operational_threshold;
    if (score.operational_passes_conditional_bound && score.all_valid &&
        score.operational_bit_log_separation > 0 &&
        score.operational_bit_log_separation >= minimum_separation) {
        score.decision = SlotDecision::data;
        score.bit = static_cast<std::uint8_t>(operational[1] > operational[0]);
        score.reason = score.varying_candidates || score.drift_timing_atoms || score.coherent_gain_candidates ?
            "bounded_varying_channel_data_evidence" : "fixed_affine_data_evidence";
    } else if (minimum_separation > 0 && score.operational_passes_conditional_bound) {
        score.bit.reset();
        score.decision = SlotDecision::ambiguous;
        score.reason = "varying_channel_bit_evidence_too_close";
    } else if (score.complete && score.zero_h0_verified && score.noise_statistics_valid &&
               score.quiet_energy <= score.quiet_threshold) {
        score.bit.reset();
        score.decision = SlotDecision::quiet;
        score.reason = "scored_public_quiet_aperture";
    } else {
        score.bit.reset();
        score.decision = SlotDecision::ambiguous;
        score.reason = score.complete ? "unresolved_bit_or_null" : "incomplete_fine_observation";
    }
}
} // namespace

void receiver_detail::classify_varying_slot(ReceiverSlotScore& score, const AdmissionGrid& grid,
        const AdmissionGrid* varying_grid, const DriftPlan* drift, double minimum_separation) {
    const auto operational = varying_operational_scores(score, grid, varying_grid, drift);
    const auto log_mass = std::log(static_cast<double>(score.fine_candidates)) - grid.log_count();
    score.selected_prior_mass = std::exp(log_mass);
    std::optional<double> combined;
    if (!varying_grid && !drift) {
        const auto best = std::max(score.bit_evidence[0], score.bit_evidence[1]);
        const auto gap = std::abs(score.bit_evidence[0] - score.bit_evidence[1]);
        combined = best + std::log1p(std::exp(-gap)) - std::numbers::ln2 + log_mass;
    }
    classify_operational_scores(score, operational, minimum_separation, combined);
}

void receiver_detail::classify_fine_slot(ReceiverSlotScore& score, const AdmissionGrid& grid) {
    classify_varying_slot(score, grid, nullptr, nullptr, 0.);
}

namespace {
double elapsed(Clock::time_point began) {
    return std::chrono::duration<double>(Clock::now() - began).count();
}
} // namespace

std::size_t receiver_slot_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_workspace(config, slot, workers, false).total;
}

std::size_t receiver_slot_overlap_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                            unsigned workers) {
    return plan_workspace(config, slot, workers, true).overlapped_total;
}

std::size_t receiver_detail::receiver_slot_workspace_with_supplement(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
        const SlotEvidenceSupplement& supplement, bool overlap) {
    const auto workspace = plan_workspace(config, slot, workers, overlap, &supplement);
    return overlap ? workspace.overlapped_total : workspace.total;
}

namespace {
ReceiverSlotScore score_stages(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                               const Crypto& key, std::span<const Complex> iq,
                               std::span<const std::uint8_t> valid, unsigned workers,
                               std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared,
                               bool may_overlap, const SlotEvidenceSupplement* supplement = nullptr) {
    check_cancelled(stop);
    const auto workspace = plan_workspace(config, slot, workers, may_overlap, supplement);
    require(iq.size() == slot.stop_sample - slot.first_sample &&
        (valid.empty() || valid.size() == iq.size()), "Robust input differs from the full public observation");
    if (workspace.total > memory_limit_bytes)
        throw ReceiverWorkspaceError("Robust receiver slot exceeds its admitted memory lease");

    ReceiverSlotScore score;
    score.index = slot.index;
    score.first_sample = slot.first_sample;
    score.stop_sample = slot.stop_sample;
    score.quiet_first_sample = slot.quiet_first_sample;
    score.quiet_stop_sample = slot.quiet_stop_sample;
    score.sample_rate_hz = config.sample_rate_hz;
    score.all_valid = true;
    for (std::size_t i = 0; i < iq.size(); ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        const auto mask = valid.empty() ? 1 : valid[i];
        require(mask <= 1 && std::isfinite(iq[i].real()) && std::isfinite(iq[i].imag()),
                "Robust input must be finite with a binary validity mask");
        score.all_valid &= mask != 0;
    }

    // A wholly valid observation on a lease with room for it acquires beside
    // coarse preparation. Any other slot keeps its sequential bound.
    const bool overlap = may_overlap && score.all_valid && workspace.overlappable &&
                         workspace.overlapped_total <= memory_limit_bytes;
    score.workspace_peak_bytes = overlap ? workspace.overlapped_total : workspace.total;
    // A standalone slot owns one pool for all of its stages.
    const auto owned = shared ? nullptr : std::make_unique<ReceiverExecutor>(std::max(1U, workers));
    const auto executor = shared ? shared : owned.get();
    HelperResult helper;
    FrequencyInterval fallback;
    const auto acquire = [&](const LaneWave& wave, std::stop_token job_stop) {
        const auto began = Clock::now();
        helper = score_helper(config, slot, iq, valid, workspace.helper, job_stop, wave);
        score.helper_seconds = elapsed(began);
        score.helper_status = helper.reason;
        return public_frequency_intervals(config, helper, fallback);
    };
    // Acquisition alone still fans its independent steps out on these lanes.
    const auto lanes = lane_wave(*executor, slot.index, workers, stop);
    // The canonical full coarse screen requires a wholly valid observation.
    // Known erasures cannot become observed silence or complete a message.
    if (!score.all_valid) {
        acquire(lanes, stop);
        score.reason = "coarse_erasures_unsupported";
        return score;
    }

    auto began = Clock::now();
    CoarseResult coarse;
    auto search = config;
    search.time_varying_channel = workspace.varying.block_rows || workspace.drift.workspace_bytes ||
        workspace.coherent_gain.block_rows;
    if (overlap) {
        score.helper_overlapped = true;
        coarse = score_coarse(search, slot, key, iq,
            FrequencyAssistance{acquire, {workspace.helper, helper_widest_wave}},
            workers, workspace.overlapped_coarse, stop, executor);
    } else {
        const auto intervals = acquire(lanes, stop);
        began = Clock::now();
        coarse = score_coarse(search, slot, key, iq, intervals, workers, workspace.coarse, stop, executor);
    }
    score.coarse_seconds = elapsed(began);
    score.coarse_workspaces = coarse.workspaces;
    score.coarse_frequency_points = coarse.frequency_points;
    score.coarse_arrival_points = coarse.arrival_points;
    score.coarse_fft_precision = coarse.work.plan.fft_precision;
    score.coarse_precision_reason = coarse.work.plan.precision_reason;
    score.varying_status = workspace.varying.status;
    score.drift_status = workspace.drift.status;
    score.coherent_gain_status = workspace.coherent_gain.status;
    score.varying_coarse_seconds = coarse.work.varying_proposals.wall_seconds;
    score.varying_timing_blocks = coarse.work.segmented_timing_blocks;
    score.varying_timing_frequencies = coarse.work.segmented_timing_frequencies;
    score.varying_timing_status = coarse.work.segmented_timing_status;
    if (coarse.coordinates.empty()) {
        score.reason = std::move(coarse.reason);
        return score;
    }
    const auto fine = prepare_fine_coordinates(config, slot, coarse);

    began = Clock::now();
    score_fine(config, slot, key, iq, fine.coordinates, coarse.projected_sample_rate_hz,
               coarse.clock_gap_ppm, workers, score, workspace.fine, stop, executor);
    const bool varying = workspace.varying.block_rows != 0;
    const auto drift = workspace.drift.workspace_bytes ? &workspace.drift : nullptr;
    const auto separation = config.time_varying_channel ? config.time_varying_minimum_bit_log_separation : 0.;
    const AdmissionGrid varying_grid(fine.grid.lower, fine.grid.upper,
        config.frequency_bound_hz + config.time_varying_atmospheric_hz,
        config.clock_bound_ppm, fine.grid.frequency_step);
    const auto selected = fine.varying_coordinates.empty() ? std::span(fine.coordinates) :
                                                            std::span(fine.varying_coordinates);
    const auto varying_prior = varying ? &varying_grid : nullptr;
    const auto coherent_gain_prior = workspace.coherent_gain.block_rows ? &varying_grid : nullptr;
    const bool has_supplement = workspace.evidence_supplement != 0;
    std::array<double, 2> supplement_evidence{
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    // The weight is reserved from the first classifier onward. Skipping a
    // branch after a sufficient earlier result never restores that mass.
    const auto ordinary_scores = [&](const ReceiverSlotScore& target) {
        auto ordinary = varying_operational_scores(target, fine.grid, varying_prior, drift,
                                                   true, coherent_gain_prior);
        if (has_supplement) {
            const auto old_weight = std::log1p(-supplement->fixed_weight);
            const auto new_weight = std::log(supplement->fixed_weight);
            for (std::size_t bit = 0; bit < 2; ++bit)
                ordinary[bit] = log_add(ordinary[bit] + old_weight,
                                       supplement_evidence[bit] + new_weight);
        }
        return ordinary;
    };
    const auto add_line_branch = [&](ReceiverSlotScore& target, const ReceiverSlotScore& residual,
                                     double line_log_evidence) {
        const auto ordinary = ordinary_scores(target);
        auto residual_score = varying_operational_scores(residual, fine.grid, varying_prior, drift,
                                                          false, coherent_gain_prior);
        // The extension is not qualified under a retained coherent-line
        // nuisance. Its reserved mass stays absent, on both old branches.
        if (has_supplement)
            for (auto& bit : residual_score) bit += std::log1p(-supplement->fixed_weight);
        std::array<double, 2> combined{};
        // A predeclared equal mixture of ordinary and line-plus-DATA
        // likelihoods. Dividing by a quantity at least one preserves the
        // white-noise bound under observation-dependent model retention.
        const auto denominator = std::numbers::ln2 + std::log1p(std::exp(-std::abs(line_log_evidence)));
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto numerator = line_log_evidence > 0 ?
                log_add(ordinary[bit] - line_log_evidence, residual_score[bit]) :
                log_add(ordinary[bit], residual_score[bit] + line_log_evidence);
            combined[bit] = numerator - denominator;
        }
        classify_operational_scores(target, combined, separation);
    };
    const auto classify_supplement = [&] {
        if (workspace.line) add_line_branch(score, score, -std::numeric_limits<double>::infinity());
        else if (has_supplement || coherent_gain_prior)
            classify_operational_scores(score, ordinary_scores(score), separation);
        else classify_varying_slot(score, fine.grid, varying_prior, drift, separation);
    };
    const auto score_coherent_gain = [&](const VaryingFinePlan& plan, ReceiverSlotScore& target,
                                         const LineAtom* line) {
        ReceiverSlotScore coherent;
        score_varying_fine(config, slot, key, plan, iq, selected, fine.segmented_timing_coordinates,
            coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, coherent, stop, *executor, line);
        target.coherent_gain_candidates = coherent.varying_candidates;
        target.coherent_gain_bit_evidence = coherent.varying_bit_evidence;
        target.coherent_gain_blocks = coherent.varying_blocks;
        target.coherent_gain_frequency_states = coherent.varying_frequency_states;
        target.coherent_gain_seconds = coherent.varying_fine_seconds;
    };
    score.selected_prior_mass = std::exp(std::log(static_cast<double>(score.fine_candidates)) - fine.grid.log_count());
    classify_supplement();
    const bool coherent_admission = score.decision == SlotDecision::data;
    // With no retained line the nuisance term has zero prior mass. The
    // ordinary early decision needs no extra FFT on a coherent DATA slot.
    if (varying && score.decision != SlotDecision::data) {
        score_varying_fine(config, slot, key, workspace.varying, iq, selected,
                           fine.segmented_timing_coordinates,
                           coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, score, stop, *executor);
        classify_supplement();
    } else if (varying) score.varying_status = "coherent_evidence_sufficient";
    if (coherent_gain_prior && score.decision != SlotDecision::data) {
        score_coherent_gain(workspace.coherent_gain, score, nullptr);
        classify_supplement();
    } else if (coherent_gain_prior) score.coherent_gain_status = "earlier_evidence_sufficient";
    if (drift && score.decision != SlotDecision::data) {
        score_drift_fine(config, slot, key, *drift, iq, selected, fine.epoch_timing_coordinates,
                         workers, score, stop, *executor);
        classify_supplement();
    } else if (drift) score.drift_status = "earlier_evidence_sufficient";
    if (has_supplement && score.decision != SlotDecision::data) {
        check_cancelled(stop);
        supplement_evidence = supplement->score(config, slot, key, iq, fine, workers,
            workspace.evidence_supplement, stop, *executor);
        check_cancelled(stop);
        for (const auto value : supplement_evidence)
            require(std::isfinite(value) || value == -std::numeric_limits<double>::infinity(),
                    "Robust evidence supplement returned an invalid log e-value");
        classify_supplement();
        if (score.decision == SlotDecision::data)
            score.reason = "bounded_supplemental_data_evidence";
    }
    // A supplemental threshold crossing takes the ordinary line gate.
    // If a line is retained, discard the new likelihood permanently: its
    // raw-IQ null model is not a line-conditioned model. Both canonical
    // branches retain (1-weight), with no observation-dependent renormalizing.
    if (workspace.line && !coherent_admission && score.operational_passes_conditional_bound) {
        if (const auto line = find_varying_line(config, slot, iq, stop)) {
            if (has_supplement)
                supplement_evidence.fill(-std::numeric_limits<double>::infinity());
            ReceiverSlotScore residual;
            residual.fine_candidates = score.fine_candidates;
            if (workspace.line_varying.block_rows)
                score_varying_fine(config, slot, key, workspace.line_varying, iq, selected,
                    fine.segmented_timing_coordinates,
                    coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, residual, stop, *executor, &*line);
            add_line_branch(score, residual, line->log_weighted_evidence);
            if (workspace.line_coherent_gain.block_rows && score.decision != SlotDecision::data) {
                score_coherent_gain(workspace.line_coherent_gain, residual, &*line);
                add_line_branch(score, residual, line->log_weighted_evidence);
            }
            if (workspace.line_drift.workspace_bytes && score.decision != SlotDecision::data) {
                score_drift_fine(config, slot, key, workspace.line_drift, iq, selected, fine.epoch_timing_coordinates,
                    workers, residual, stop, *executor, &*line);
                add_line_branch(score, residual, line->log_weighted_evidence);
            }
            score.varying_line_frequency_hz = line->frequency_hz;
            score.varying_line_log_evidence = line->log_weighted_evidence;
            if (score.decision == SlotDecision::data) score.reason = "varying_data_evidence_with_coherent_line";
            else {
                score.decision = SlotDecision::ambiguous;
                score.reason = "coherent_line_in_public_data_aperture";
                score_varying_quiet(config, slot, iq, score, stop);
            }
        }
    }
    if (config.time_varying_channel && score.decision == SlotDecision::quiet) {
        score_varying_quiet(config, slot, iq, score, stop);
        if (!score.varying_quiet_modes || score.varying_quiet_energy > score.varying_quiet_threshold) {
            score.decision = SlotDecision::ambiguous;
            score.reason = score.varying_quiet_status;
        }
    }
    score.fine_seconds = elapsed(began);
    check_cancelled(stop);
    return score;
}
} // namespace

ReceiverSlotScore receiver_detail::score_slot(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared, true);
}

ReceiverSlotScore receiver_detail::score_slot_with_supplement(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared,
        const SlotEvidenceSupplement& supplement) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared,
                        true, &supplement);
}

ReceiverSlotScore receiver_detail::score_slot_in_sequence(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        std::span<const std::uint8_t> valid, unsigned workers, std::size_t memory_limit_bytes,
        std::stop_token stop, ReceiverExecutor* shared) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared, false);
}

ReceiverSlotScore score_receiver_slot(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                     const Crypto& key, std::span<const Complex> iq,
                                     std::span<const std::uint8_t> valid, unsigned workers,
                                     std::size_t memory_limit_bytes, std::stop_token stop) {
    return receiver_detail::score_slot(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, nullptr);
}

} // namespace datapump::triad::v3
