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
        c.source_spectral_skirt_allowance_hz};
    require(std::all_of(nonnegative.begin(), nonnegative.end(), [](double x) { return x >= 0 && std::isfinite(x); }) &&
        std::isfinite(c.spread_doppler_hz) && c.data_if_hz <= 120e6 && c.clock_bound_ppm < 1e6 &&
        c.endpoint_clock_bound_ppm < 1e6, "invalid Robust public channel bounds");
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
};

StageWorkspace plan_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    const auto rows = validate(config, slot, workers);
    const auto input = checked_multiply(rows, sizeof(Complex) + sizeof(std::uint8_t));
    const auto helper = helper_workspace(config, slot);
    const auto coarse = coarse_workspace(config, slot, workers);
    const auto fine = fine_workspace(config, slot, workers);
    const auto lease = [&](std::size_t stage) { return checked_add(checked_add(input, receipts_bytes), stage); };
    // One lane runs acquisition and preparation in turn: nothing coexists.
    const bool overlappable = workers > 1 && helper && coarse_defers_frequency_grid(config, slot);
    const auto overlapped_coarse = overlappable ?
        coarse_workspace(config, slot, workers, {helper, helper_widest_wave}) : coarse;
    return {input, helper, coarse, fine, lease(std::max({helper, coarse, fine})), overlappable,
            overlapped_coarse, lease(std::max({helper, overlapped_coarse, fine}))};
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
        config.frequency_bound_hz, config.clock_bound_ppm, std::min(.01, coarse.maximum_frequency_step_hz)}, {}};
    result.coordinates.reserve(coarse.coordinates.size());
    for (const auto& proposed : coarse.coordinates) {
        const auto coordinate = snap(proposed, result.grid);
        if (std::find(result.coordinates.begin(), result.coordinates.end(), coordinate) == result.coordinates.end())
            result.coordinates.push_back(coordinate);
    }
    return result;
}

void receiver_detail::classify_fine_slot(ReceiverSlotScore& score, const AdmissionGrid& grid) {
    require(score.fine_candidates > 0, "Robust fine evidence requires a public coordinate bank");
    score.operational_threshold = receiver_operational_threshold();
    const double log_mass = std::log(static_cast<double>(score.fine_candidates)) - grid.log_count();
    score.selected_prior_mass = std::exp(log_mass);
    const auto best = std::max(score.bit_evidence[0], score.bit_evidence[1]);
    const auto gap = std::abs(score.bit_evidence[0] - score.bit_evidence[1]);
    score.operational_log_e = best + std::log1p(std::exp(-gap)) - std::numbers::ln2 + log_mass;
    require(std::isfinite(score.operational_log_e), "Robust DATA evidence is nonfinite");
    score.operational_passes_conditional_bound = score.complete && score.zero_h0_verified &&
        score.operational_log_e >= score.operational_threshold;
    if (score.operational_passes_conditional_bound && score.all_valid && score.bit_evidence[0] != score.bit_evidence[1]) {
        score.decision = SlotDecision::data;
        score.bit = static_cast<std::uint8_t>(score.bit_evidence[1] > score.bit_evidence[0]);
        score.reason = "fixed_affine_data_evidence";
    } else if (score.complete && score.zero_h0_verified && score.noise_statistics_valid &&
               score.quiet_energy <= score.quiet_threshold) {
        score.decision = SlotDecision::quiet;
        score.reason = "scored_public_quiet_aperture";
    } else {
        score.reason = score.complete ? "unresolved_bit_or_null" : "incomplete_fine_observation";
    }
}

namespace {
double elapsed(Clock::time_point began) {
    return std::chrono::duration<double>(Clock::now() - began).count();
}
} // namespace

std::size_t receiver_slot_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_workspace(config, slot, workers).total;
}

std::size_t receiver_slot_overlap_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                            unsigned workers) {
    return plan_workspace(config, slot, workers).overlapped_total;
}

namespace {
ReceiverSlotScore score_stages(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                               const Crypto& key, std::span<const Complex> iq,
                               std::span<const std::uint8_t> valid, unsigned workers,
                               std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared,
                               bool may_overlap) {
    check_cancelled(stop);
    const auto workspace = plan_workspace(config, slot, workers);
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
    if (overlap) {
        score.helper_overlapped = true;
        coarse = score_coarse(config, slot, key, iq,
            FrequencyAssistance{acquire, {workspace.helper, helper_widest_wave}},
            workers, workspace.overlapped_coarse, stop, executor);
    } else {
        const auto intervals = acquire(lanes, stop);
        began = Clock::now();
        coarse = score_coarse(config, slot, key, iq, intervals, workers, workspace.coarse, stop, executor);
    }
    score.coarse_seconds = elapsed(began);
    score.coarse_workspaces = coarse.workspaces;
    score.coarse_frequency_points = coarse.frequency_points;
    score.coarse_arrival_points = coarse.arrival_points;
    score.coarse_fft_precision = coarse.work.plan.fft_precision;
    score.coarse_precision_reason = coarse.work.plan.precision_reason;
    if (coarse.coordinates.empty()) {
        score.reason = std::move(coarse.reason);
        return score;
    }
    const auto fine = prepare_fine_coordinates(config, slot, coarse);

    began = Clock::now();
    score_fine(config, slot, key, iq, fine.coordinates, coarse.projected_sample_rate_hz,
               coarse.clock_gap_ppm, workers, score, workspace.fine, stop, executor);
    classify_fine_slot(score, fine.grid);
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
