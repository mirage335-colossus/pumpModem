#include "datapump/triad_receiver.hpp"

#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_helper.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <new>
#include <numbers>

namespace datapump::triad::v3 {
namespace {
using namespace receiver_detail;
using Clock = std::chrono::steady_clock;
constexpr std::size_t receipts_bytes = 64 * 1024;
constexpr std::size_t maximum_helper_intervals = 64;

void require(bool valid, const char* reason) {
    if (!valid) throw Error(reason);
}

std::size_t validate(const ReceiverConfig& c, const ReceiverSlotPlan& s, unsigned workers) {
    (void)wrong_key_admission_name(c.wrong_key_admission);
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
    require((!s.carrier_beat_first_sample && !s.carrier_beat_stop_sample) ||
        (s.first_sample <= s.carrier_beat_first_sample &&
         s.carrier_beat_first_sample < s.carrier_beat_stop_sample && s.carrier_beat_stop_sample <= s.stop_sample),
        "invalid Robust carrier beat proposal aperture");
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
    std::size_t drift_envelope, overlapped_drift_envelope;
    VaryingFinePlan varying;
    DriftPlan drift;
    VaryingFinePlan coherent_gain;
    std::size_t line;
    VaryingFinePlan line_varying;
    DriftPlan line_drift;
    VaryingFinePlan line_coherent_gain;
    std::size_t evidence_supplement = 0;
    std::string_view midpoint_timing_status;
    bool helper_transfer_full_prior;
};

bool uses_varying_coarse(const StageWorkspace& workspace) {
    return workspace.varying.block_rows || workspace.drift.workspace_bytes || workspace.coherent_gain.block_rows;
}

std::optional<StageWorkspace> grouped_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        unsigned workers, StageWorkspace ordinary) {
    // Freeze the ordinary model-family selection before repricing coarse
    // scratch. A smaller lease must not change the set of admitting models.
    if (config.helper_enabled || config.bulk_backend || slot.scheduled_gap_fill || ordinary.helper || ordinary.line ||
        uses_varying_coarse(ordinary) || ordinary.evidence_supplement) return {};
    auto search = config;
    search.time_varying_channel = false;
    if (!grouped_cpu_coarse_supported(search, slot)) return {};
    auto group = slot;
    group.memory_bounded_coarse = true;
    const auto coarse = coarse_workspace_bounds(search, group, workers, {}, MidpointTimingEvaluation::off);
    const auto scratch = std::max({coarse.host_bytes, ordinary.fine, varying_quiet_workspace(config, slot, workers)});
    const auto total = checked_add(checked_add(ordinary.input, receipts_bytes), scratch);
    if (total >= ordinary.total) return {};
    ordinary.coarse = ordinary.overlapped_coarse = coarse.host_bytes;
    ordinary.total = ordinary.overlapped_total = total;
    ordinary.drift_envelope = ordinary.overlapped_drift_envelope = total;
    return ordinary;
}

StageWorkspace plan_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                              bool quote_overlap, const SlotEvidenceSupplement* supplement = nullptr,
                              const ReceiverSlotPlan* protected_plan = nullptr,
                              const StageWorkspace* public_workspace = nullptr) {
    if (slot.memory_bounded_coarse) {
        require(!supplement && !protected_plan && !public_workspace,
                "Grouped coarse scoring requires the ordinary Robust observation");
        auto original = slot;
        original.memory_bounded_coarse = false;
        const auto bounded = grouped_workspace(config, original, workers,
            plan_workspace(config, original, workers, quote_overlap));
        if (!bounded) throw ReceiverWorkspaceError("Robust grouped coarse receiver shape is unavailable");
        return *bounded;
    }
    const auto rows = validate(config, slot, workers);
    const auto& inference = protected_plan ? *protected_plan : slot;
    const auto varying_plan = [&](bool line, bool coherent_gain) {
        if (!public_workspace) return varying_fine_plan(config, inference, workers, line, coherent_gain);
        const auto& original = line ? (coherent_gain ? public_workspace->line_coherent_gain :
                                                      public_workspace->line_varying) :
            (coherent_gain ? public_workspace->coherent_gain : public_workspace->varying);
        if (!original.block_rows) return original;
        const auto tracking_final_rows = slot.scheduled_gap_fill ?
            original.block_rows + rows % original.block_rows : 0;
        const auto planned = varying_fine_plan(config, inference, workers, line, coherent_gain, tracking_final_rows);
        if (!planned.block_rows) return planned;
        if (planned.block_rows != original.block_rows || planned.track_frequency != original.track_frequency ||
            planned.nominal_independent_gain_candidates != original.nominal_independent_gain_candidates)
            return VaryingFinePlan{0, 0, "protected_varying_model_grid_unavailable"};
        if (planned.track_frequency && !line && !tracking_final_rows) {
            // Helper-off tracking bins are tied to the power-of-two maximum
            // block transform. A changed tail must preserve that lattice.
            const auto span = inference.stop_sample - inference.first_sample;
            const auto block = original.block_rows;
            const auto multiplier = coherent_gain ? 4 : 2;
            if (std::bit_ceil(multiplier * (block + rows % block)) !=
                std::bit_ceil(multiplier * (block + span % block)))
                return VaryingFinePlan{0, 0, "protected_varying_frequency_grid_unavailable"};
        }
        return planned;
    };
    const auto drift_shape = [&](bool line, std::size_t preserved, std::size_t headroom) {
        if (!public_workspace) return drift_plan(config, inference, workers, line, preserved, headroom);
        const auto& original = line ? public_workspace->line_drift : public_workspace->drift;
        if (!original.workspace_bytes) return original;
        const auto original_envelope = public_workspace->drift_envelope - public_workspace->input - receipts_bytes;
        auto planned = drift_plan(config, inference, workers, line, original_envelope, headroom, &original);
        if (!planned.workspace_bytes) return planned;
        planned.slope_reference_seconds = original.slope_reference_seconds -
            static_cast<long double>(inference.first_sample - slot.first_sample) / config.sample_rate_hz;
        return planned;
    };
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
    // Freeze the baseline family membership and complete geometry envelope
    // before considering a larger coherent proposal. Charge actual native
    // scratch for each admitted family and keep the previous owner bound.
    bool inspect_full_overlap = quote_overlap;
    const auto build = [&](const DriftPlan& planned, std::optional<DriftPlan> line_override = std::nullopt) {
        const auto input = checked_multiply(rows, sizeof(Complex) + sizeof(std::uint8_t));
        const bool helper_transfer_full_prior = helper_transfer_requires_full_prior(config, slot);
        const auto helper = helper_transfer_full_prior ? 0 : helper_workspace(config, slot);
        auto varying = varying_plan(false, false);
        const auto coherent_gain = varying_plan(false, true);
        auto search = config;
        search.time_varying_channel = public_workspace ? uses_varying_coarse(*public_workspace) :
            varying.block_rows || planned.workspace_bytes || coherent_gain.block_rows;
        const auto midpoint_support = public_workspace ? CoarseMidpointSupport{
            .available = public_workspace->varying.midpoint_tracking, .status = public_workspace->midpoint_timing_status,
            .gain_frequency_status = {}} : (varying.track_frequency ?
            coarse_midpoint_timing_support(search, slot) : CoarseMidpointSupport{false, "short_tracking_unavailable"});
        const auto midpoint = midpoint_support.available ?
            MidpointTimingEvaluation::short_tracking : MidpointTimingEvaluation::off;
        if (midpoint_support.available && varying.track_frequency) enable_midpoint_tracking(varying);
        const auto coarse = coarse_workspace_bounds(search, slot, workers, {}, midpoint);
        const auto fine = fine_workspace(config, inference, workers);
        const auto varying_quiet = varying_quiet_workspace(config, inference, workers);
        const auto line = search.time_varying_channel && (!public_workspace || public_workspace->line) ?
            varying_line_workspace(config, inference) : 0;
        const auto line_varying = line ? varying_plan(true, false) : VaryingFinePlan{};
        const auto line_coherent_gain = line &&
                (public_workspace ? public_workspace->line_coherent_gain.block_rows : coherent_gain.block_rows) ?
            varying_plan(true, true) : VaryingFinePlan{};
        // One lane runs acquisition and preparation in turn: nothing coexists.
        const bool overlap_shape = workers > 1 && helper && coarse_defers_frequency_grid(config, slot);
        const bool overlappable = quote_overlap && overlap_shape;
        const auto overlapped_coarse = inspect_full_overlap && overlap_shape ?
            coarse_workspace_bounds(search, slot, workers, {helper, helper_widest_wave}, midpoint) : coarse;
        // Ordinary drift lanes widen only into the largest other stage of
        // both orchestration envelopes. A corrected discrete GPU quote can
        // make room for more owners without losing previously admitted drift.
        const auto drift_headroom = std::max({helper,
            std::min(coarse.drift_envelope_bytes, overlapped_coarse.drift_envelope_bytes), fine,
            varying.workspace_bytes, coherent_gain.workspace_bytes, varying_quiet, line,
            line_varying.workspace_bytes, line_coherent_gain.workspace_bytes, evidence_supplement});
        auto drift = line_override || !planned.workspace_bytes ? planned :
            drift_shape(false, 0, drift_headroom);
        auto line_drift = line_override ? *line_override :
            (line && (public_workspace ? public_workspace->line_drift.workspace_bytes : drift.workspace_bytes) ?
             drift_shape(true, 0, drift_headroom) : DriftPlan{});
        const auto supplemental = std::max({varying.workspace_bytes, drift.workspace_bytes,
            coherent_gain.workspace_bytes, varying_quiet, line, line_varying.workspace_bytes,
            line_drift.workspace_bytes, line_coherent_gain.workspace_bytes, evidence_supplement});
        // A varying device session's host bytes (its compilation and a
        // unified-memory lease) share this stage, beside the native quote,
        // and never raise it.
        varying.device_envelope_bytes = supplemental - varying.workspace_bytes;
        const auto host_stage = std::max({helper, coarse.host_bytes, fine, supplemental});
        const auto overlap_host_stage = std::max({helper, overlapped_coarse.host_bytes, fine, supplemental});
        if (coarse.drift_envelope_bytes > coarse.host_bytes) {
            // The geometry envelope preserved native scratch. Additional
            // device host buffers still need room in the actual lease.
            const auto device_host_limit = std::min(host_stage, overlap_host_stage);
            drift.device_envelope_bytes = std::min(drift.device_envelope_bytes, device_host_limit);
            line_drift.device_envelope_bytes = std::min(line_drift.device_envelope_bytes, device_host_limit);
        }
        const auto lease = [&](std::size_t stage) { return checked_add(checked_add(input, receipts_bytes), stage); };
        return StageWorkspace{input, helper, coarse.host_bytes, fine,
                lease(host_stage),
                overlappable, overlapped_coarse.host_bytes,
                lease(overlap_host_stage),
                lease(std::max({helper, coarse.drift_envelope_bytes, fine, supplemental})),
                lease(std::max({helper, overlapped_coarse.drift_envelope_bytes, fine, supplemental})),
                varying, drift, coherent_gain, line, line_varying, line_drift, line_coherent_gain,
                evidence_supplement, midpoint_support.status, helper_transfer_full_prior};
    };
    auto baseline = build(drift_shape(false, 0, 0));
    if (public_workspace || baseline.drift.status != "drift_public_bandwidth_exceeds_reduced_proposal_limit")
        return baseline;
    const auto original = baseline;
    try {
        // Quote and execution callers must choose the same optional family,
        // including when it activates a different coarse plan. The initial
        // mandatory baseline above remains intact if any optional public
        // shape cannot be represented by a particular backend.
        if (!inspect_full_overlap) {
            inspect_full_overlap = true;
            baseline = build(baseline.drift);
        }
        const auto preserved_stage = baseline.drift_envelope - baseline.input - receipts_bytes;
        const auto expanded = drift_plan(config, slot, workers, false, preserved_stage);
        if (!expanded.workspace_bytes) {
            baseline.drift.status = expanded.status;
            return baseline;
        }
        DriftPlan line;
        try {
            if (varying_line_workspace(config, slot))
                line = drift_plan(config, slot, workers, true, preserved_stage);
        } catch (const Error& error) {
            if (std::string_view(error.what()) == "Robust reception cancelled") throw;
            // An unsupported optional line shape retains zero mass in that
            // branch; it must not discard the usable ordinary likelihood.
            line.status = "drift_line_extension_shape_unavailable";
        }
        auto candidate = build(expanded, line);
        // A newly available varying family can also alter coarse or line
        // preparation. Check the complete shape, for both orchestration
        // modes, rather than just the additional scorer's scratch.
        if (candidate.drift_envelope <= baseline.drift_envelope &&
            candidate.overlapped_drift_envelope <= baseline.overlapped_drift_envelope) {
            candidate.total = std::max(candidate.total, baseline.total);
            candidate.overlapped_total = std::max(candidate.overlapped_total, baseline.overlapped_total);
            return candidate;
        }
        baseline.drift.status = "drift_extension_exceeds_preserved_owner_lease";
        return baseline;
    } catch (const Error& error) {
        if (std::string_view(error.what()) == "Robust reception cancelled") throw;
        auto preserved = original;
        preserved.drift.status = "drift_extension_shape_unavailable";
        return preserved;
    }
}

struct ProtectedScoringPlan {
    ReceiverSlotPlan aperture;
    StageWorkspace original, restricted;
    std::optional<ReceiverSlotPlan> quiet;
};

ReceiverSlotPlan protected_observation(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                       std::uint64_t first, std::uint64_t stop) {
    auto aperture = slot;
    aperture.first_sample = first;
    aperture.stop_sample = stop;
    require(slot.first_sample <= aperture.first_sample && aperture.first_sample <= aperture.stop_sample &&
            aperture.stop_sample <= slot.stop_sample, "invalid Robust protected DATA observation");
    aperture.quiet_first_sample = std::clamp(slot.quiet_first_sample, aperture.first_sample, aperture.stop_sample);
    aperture.quiet_stop_sample = std::clamp(slot.quiet_stop_sample, aperture.first_sample, aperture.stop_sample);
    aperture.window_capture_lead_seconds -=
        static_cast<long double>(aperture.first_sample - slot.first_sample) / config.sample_rate_hz;
    return aperture;
}

ReceiverSlotPlan protected_data_observation(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    if (slot.protected_data_first_sample || slot.protected_data_stop_sample)
        return protected_observation(config, slot, slot.protected_data_first_sample, slot.protected_data_stop_sample);
    return protected_observation(config, slot, slot.quiet_first_sample, slot.quiet_stop_sample);
}

ReceiverSlotPlan protected_beat_observation(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto aperture = protected_data_observation(config, slot);
    auto beat = slot;
    beat.quiet_first_sample = aperture.first_sample;
    beat.quiet_stop_sample = aperture.stop_sample;
    return beat;
}

ProtectedScoringPlan plan_protected_observation(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                                 unsigned workers, const SlotEvidenceSupplement* supplement = nullptr) {
    const auto rows = validate(config, slot, workers);
    require(!config.helper_enabled && !config.physical_rf_retuning,
            "Protected Robust scoring requires Helper off and no physical retuning");
    ProtectedScoringPlan result{protected_data_observation(config, slot), {}, {}, {}};
    if (result.aperture.first_sample == result.aperture.stop_sample) {
        result.restricted.input = checked_multiply(rows, sizeof(Complex) + sizeof(std::uint8_t));
        result.restricted.total = checked_add(result.restricted.input, receipts_bytes);
        return result;
    }
    result.original = plan_workspace(config, slot, workers, false);
    result.restricted = plan_workspace(config, slot, workers, false, supplement, &result.aperture, &result.original);
    if (slot.quiet_first_sample < slot.quiet_stop_sample &&
        (result.aperture.first_sample != slot.quiet_first_sample || result.aperture.stop_sample != slot.quiet_stop_sample)) {
        result.quiet = protected_observation(config, slot, slot.quiet_first_sample, slot.quiet_stop_sample);
        const auto scratch = std::max(packed_affine_quiet_workspace(config, *result.quiet),
                                      varying_quiet_workspace(config, *result.quiet, workers));
        result.restricted.total = std::max(result.restricted.total,
            checked_add(checked_add(result.restricted.input, receipts_bytes), scratch));
    }
    return result;
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

FineCoordinate snap_timing(FineCoordinate coordinate, const AdmissionGrid& grid) {
    coordinate = snap(coordinate, grid);
    coordinate.frequency_hz = 0;
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
        const auto coordinate = snap_timing(proposed, result.grid);
        if (std::ranges::find(destination, coordinate) == destination.end()) destination.push_back(coordinate);
    };
    for (const auto& proposed : coarse.segmented_timing_coordinates)
        append_timing(proposed, result.segmented_timing_coordinates);
    result.epoch_timing_coordinates = result.segmented_timing_coordinates;
    for (const auto& proposed : coarse.epoch_timing_coordinates)
        append_timing(proposed, result.epoch_timing_coordinates);
    return result;
}

std::vector<FineCoordinate> receiver_detail::prepare_quarter_clock_timing(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const CoarseResult& coarse, const AdmissionGrid& grid) {
    auto timing = nominate_quarter_clock_timing(config, slot, coarse);
    auto next = timing.begin();
    for (const auto proposed : timing) {
        const auto coordinate = snap_timing(proposed, grid);
        if (std::find(timing.begin(), next, coordinate) == next) *next++ = coordinate;
    }
    timing.erase(next, timing.end());
    return timing;
}

std::vector<FineCoordinate> receiver_detail::prepare_short_tracking_timing(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const CoarseResult& coarse, const AdmissionGrid& grid) {
    const auto capacity = nominal_epoch_timing_capacity(config, slot);
    if (!capacity || coarse.work.plan.segmented_timing_blocks || !coarse.segmented_timing_coordinates.empty())
        return {};
    auto timing = prepare_quarter_clock_timing(config, slot, coarse, grid);
    timing.reserve(timing.size() + capacity);
    const auto append = [&](double clock) {
        for (const auto arrival : {grid.lower, grid.upper}) {
            const auto coordinate = snap_timing({0, arrival, 0, clock}, grid);
            if (std::ranges::find(timing, coordinate) == timing.end()) timing.push_back(coordinate);
        }
    };
    if (config.clock_bound_ppm) {
        for (const auto fraction : {-.75, -.25, .25, .75}) append(fraction * config.clock_bound_ppm);
    } else {
        append(0.);
    }
    return timing;
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
                                 double minimum_separation, std::optional<double> combined = std::nullopt,
                                 double threshold = receiver_operational_threshold()) {
    score.operational_threshold = threshold;
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
        const AdmissionGrid* varying_grid, const DriftPlan* drift, double minimum_separation, double threshold) {
    const auto operational = varying_operational_scores(score, grid, varying_grid, drift);
    const auto log_mass = std::log(static_cast<double>(score.fine_candidates)) - grid.log_count();
    score.selected_prior_mass = std::exp(log_mass);
    std::optional<double> combined;
    if (!varying_grid && !drift) {
        const auto best = std::max(score.bit_evidence[0], score.bit_evidence[1]);
        const auto gap = std::abs(score.bit_evidence[0] - score.bit_evidence[1]);
        combined = best + std::log1p(std::exp(-gap)) - std::numbers::ln2 + log_mass;
    }
    classify_operational_scores(score, operational, minimum_separation, combined, threshold);
}

void receiver_detail::classify_fine_slot(ReceiverSlotScore& score, const AdmissionGrid& grid) {
    classify_varying_slot(score, grid, nullptr, nullptr, 0.);
}

namespace {
double required_wrong_key_gap(const ReceiverSlotScore& score, WrongKeyAdmission policy) {
    // An unrelated strong keyed waveform can fit fragments of BOTH bit
    // alternatives far better than white noise. A fixed additive threshold
    // cannot address that near-far case. Increase the required gap smoothly
    // with the competing bit's positive excess above the ordinary threshold.
    // The stronger levels scale the WHOLE Strict margin, including its floor:
    // scaling only the competing excess leaves dominant accidental fits with
    // a below-threshold alternative untouched. The 1.5/2.75/4 multipliers are
    // extrapolated static-HF reference choices, not calibrated probabilities
    // or fixed dB losses; see the Wrong Key Admission study in validation.md.
    double fraction = .25, multiplier = 1.;
    switch (policy) {
    case WrongKeyAdmission::current: return 0;
    case WrongKeyAdmission::guarded: fraction = .10; break;
    case WrongKeyAdmission::strict: break;
    case WrongKeyAdmission::very_strict: multiplier = 1.5; break;
    case WrongKeyAdmission::extreme: multiplier = 2.75; break;
    case WrongKeyAdmission::maximum: multiplier = 4.; break;
    default: throw Error("invalid Robust wrong-key admission policy");
    }
    const auto floor = (policy == WrongKeyAdmission::guarded ? 1. : 2.) * std::numbers::ln10;
    const auto competing = std::min(score.operational_bit_evidence[0], score.operational_bit_evidence[1]);
    const auto reference_threshold = score.calendar_opportunities_per_day ?
        receiver_operational_threshold() : score.operational_threshold;
    return multiplier * (std::max(score.operational_minimum_bit_log_separation, floor) +
        fraction * std::max(0., competing - reference_threshold));
}

bool baseline_rating_valid(const ReceiverSlotScore& score) {
    const auto& evidence = score.operational_bit_evidence;
    const auto gap = std::abs(evidence[0] - evidence[1]);
    return score.complete && score.all_valid && score.operational_passes_conditional_bound &&
        score.wrong_key_admission == WrongKeyAdmission::current && !score.wrong_key_rejected &&
        score.bit && *score.bit <= 1 && *score.bit == (evidence[1] > evidence[0]) &&
        !std::isnan(evidence[0]) && !std::isnan(evidence[1]) &&
        std::isfinite(std::max(evidence[0], evidence[1])) &&
        std::isfinite(score.operational_threshold) &&
        std::isfinite(score.operational_minimum_bit_log_separation) &&
        score.operational_minimum_bit_log_separation >= 0 && gap > 0 &&
        gap >= score.operational_minimum_bit_log_separation &&
        score.operational_bit_log_separation == gap;
}
} // namespace

bool wrong_key_admission_passes(const ReceiverSlotScore& score, WrongKeyAdmission policy) {
    (void)wrong_key_admission_name(policy);
    return score.decision == SlotDecision::data &&
        (policy == WrongKeyAdmission::current ||
         score.operational_bit_log_separation >= required_wrong_key_gap(score, policy));
}

void accumulate_wrong_key_admission(WrongKeyAdmissionEvidence& evidence, const ReceiverSlotScore& score) {
    if (score.decision != SlotDecision::data) return;
    if (evidence.data_bits == std::numeric_limits<std::uint64_t>::max())
        throw Error("Robust wrong-key evidence count overflow");
    const auto valid = baseline_rating_valid(score);
    evidence.available = valid && (!evidence.data_bits || evidence.available);
    ++evidence.data_bits;
    if (!valid) return;
    for (std::size_t i = 0; i < wrong_key_admission_count; ++i)
        if (wrong_key_admission_passes(score, wrong_key_admission_policies[i])) ++evidence.passing_bits[i];
}

void receiver_detail::apply_wrong_key_admission(ReceiverSlotScore& score, WrongKeyAdmission policy) {
    (void)wrong_key_admission_name(policy);
    score.wrong_key_admission = policy;
    score.wrong_key_required_log_separation = 0;
    score.wrong_key_rejected = false;
    if (policy == WrongKeyAdmission::current || score.decision != SlotDecision::data) return;
    score.wrong_key_required_log_separation = required_wrong_key_gap(score, policy);
    if (!(score.operational_bit_log_separation >= score.wrong_key_required_log_separation)) {
        score.decision = SlotDecision::ambiguous;
        score.bit.reset();
        score.wrong_key_rejected = true;
        score.reason = "wrong_key_competing_evidence";
    }
}

namespace {
double elapsed(Clock::time_point began) {
    return std::chrono::duration<double>(Clock::now() - began).count();
}
} // namespace

std::size_t receiver_slot_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_workspace(config, slot, workers, false).total;
}

std::optional<std::size_t> receiver_detail::grouped_receiver_slot_workspace(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    if (config.helper_enabled || config.bulk_backend || config.physical_rf_retuning || slot.scheduled_gap_fill)
        return {};
    auto original = slot;
    original.memory_bounded_coarse = false;
    const auto bounded = grouped_workspace(config, original, workers,
        plan_workspace(config, original, workers, false));
    return bounded ? std::optional(bounded->total) : std::nullopt;
}

std::size_t receiver_detail::protected_slot_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                                       unsigned workers) {
    return plan_protected_observation(config, slot, workers).restricted.total;
}

receiver_detail::ReceiverDriftAdmission receiver_detail::receiver_drift_admission(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers, bool overlap) {
    const auto plan = plan_workspace(config, slot, workers, overlap);
    return {plan.drift, plan.line_drift, plan.total, plan.overlapped_total};
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
enum class EvidenceEvaluation { bounded_competition, compare_admitted };

// Beat correlations nominate ordinary affine work. They do not supply any
// of the log evidence that decides DATA presence or the selected bit.
std::optional<ReceiverSlotScore::ProtectedCarrierBeat> find_protected_carrier_beat(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid,
        ReceiverSlotScore& score, std::uint8_t ordinary_bit, std::size_t scratch, std::stop_token stop,
        const FinePreparation* scheduled_fine, bool improve_guarded_margin) {
    std::array<FineCoordinate, 3> proposals{};
    proposals[0] = {0, std::clamp(0., slot.arrival_lower_seconds, slot.arrival_upper_seconds), 0., 0.};
    std::size_t proposal_count = 1;
    for (const auto& hint : score.affine_tracking) {
        if (!hint || std::abs(hint->clock_ppm) > config.clock_bound_ppm ||
            hint->arrival_seconds < slot.arrival_lower_seconds || hint->arrival_seconds > slot.arrival_upper_seconds)
            continue;
        // Squared I/Q cancels common carrier frequency. Only the public
        // epoch, arrival and sample-clock parameters change the comparison.
        const FineCoordinate coordinate{hint->epoch_offset_seconds, hint->arrival_seconds, 0., hint->clock_ppm};
        const auto proposed = std::span(proposals).first(proposal_count);
        if (std::ranges::find(proposed, coordinate) == proposed.end())
            proposals[proposal_count++] = coordinate;
    }
    std::optional<ReceiverSlotScore::ProtectedCarrierBeat> supported;
    const auto select = [&](const FineCoordinate& proposal, const CarrierBeatReceipt& beat, std::uint8_t bit) {
        supported.emplace(ReceiverSlotScore::ProtectedCarrierBeat{
            .epoch_offset_seconds = proposal.epoch_offset_seconds, .arrival_seconds = proposal.arrival_seconds,
            .clock_ppm = proposal.clock_ppm, .ordinary_bit = ordinary_bit, .predicted_bit = bit,
            .affine_support = std::nullopt});
        for (std::size_t i = 0; i < beat.profiles.size(); ++i) {
            supported->explained_fraction[i] = beat.profiles[i].bit_projection_fraction;
            supported->held_out_fraction[i] = beat.profiles[i].held_out_explained_fraction;
        }
    };
    double strongest = 0.;
    for (const auto& proposal : std::span(proposals).first(proposal_count)) {
        const auto beat = score_carrier_beat(config, slot, key, iq, valid, proposal, true, scratch, stop);
        const auto bit = qualified_carrier_beat_bit(beat);
        if (!bit) continue;
        if (supported && supported->predicted_bit != *bit) return std::nullopt;
        const auto strength = std::min(beat.profiles[0].bit_projection_fraction[*bit],
                                       beat.profiles[1].bit_projection_fraction[*bit]);
        if (strength <= strongest) continue;
        strongest = strength;
        select(proposal, beat, *bit);
    }
    // Scheduled changes require a scan for competing proposals even when
    // a fixed fit already qualifies.
    if (!supported || (scheduled_fine && (improve_guarded_margin || supported->predicted_bit != ordinary_bit))) {
        const auto scan = scan_carrier_beat(config, slot, key, iq, valid, scratch, stop);
        score.protected_carrier_beat_search_coordinates = scan.coordinates;
        if (scan.nominations[0] && scan.nominations[1]) return std::nullopt;
        if (supported) {
            if (scan.status != "scored" || scan.nominations[1 - supported->predicted_bit]) return std::nullopt;
            if (improve_guarded_margin && supported->predicted_bit == ordinary_bit && scan.nominations[ordinary_bit]) {
                const auto fixed = snap({supported->epoch_offset_seconds, supported->arrival_seconds,
                                         0., supported->clock_ppm}, scheduled_fine->grid);
                if (std::ranges::find(scheduled_fine->coordinates, fixed) != scheduled_fine->coordinates.end()) {
                    const auto& alternative = *scan.nominations[ordinary_bit];
                    select(alternative.coordinate, alternative.evidence, alternative.bit);
                }
            }
            return supported;
        }
        for (std::uint8_t bit = 0; bit < scan.nominations.size(); ++bit)
            if (const auto& nomination = scan.nominations[bit])
                select(nomination->coordinate, nomination->evidence, bit);
    }
    return supported;
}

SlotEvidenceSupplement make_protected_beat_supplement(bool colored) {
    const auto quote = [colored](const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned) {
        const auto beat = colored ? protected_beat_observation(config, slot) : slot;
        const auto plan = scan_carrier_beat(config, beat, Crypto::amateur(), {}, {},
                                            std::numeric_limits<std::size_t>::max(), {}, true);
        return plan.status == "quoted" ? plan.workspace_bytes : 0;
    };
    return {
        .fixed_weight = .5,
        .supported = [quote](const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
            return quote(config, slot, 0) > 0;
        },
        .workspace = quote,
        .score = [colored](const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                    std::span<const Complex> iq, const FinePreparation& fine, unsigned, std::size_t scratch,
                    std::stop_token stop, ReceiverExecutor&) {
            constexpr auto absent = -std::numeric_limits<double>::infinity();
            const std::array no_evidence{absent, absent};
            const auto beat = colored ? protected_beat_observation(config, slot) : slot;
            const auto scan = scan_carrier_beat(config, beat, key, iq, {}, scratch, stop, true);
            if (!scan.exploratory_nomination) return no_evidence;

            // One selected cell in the same finite public grid. Selection
            // can use the whole aperture because the cell keeps its original
            // mass. The white model averages conditional directions and
            // the colored model debits the fixed gain dictionary.
            const auto coordinate = snap(scan.exploratory_nomination->coordinate, fine.grid);
            const auto fit = score_carrier_beat(config, beat, key, iq, {}, coordinate, true, scratch, stop,
                                               !colored, colored);
            const auto& estimates = colored ? fit.colored_power_log_evidence : fit.white_power_log_evidence;
            if (!estimates) return no_evidence;
            auto evidence = *estimates;
            const auto log_count = fine.grid.log_count();
            for (auto& bit : evidence) bit -= log_count;
            return evidence;
        }};
}

const SlotEvidenceSupplement& protected_white_beat_supplement() {
    static const auto supplement = make_protected_beat_supplement(false);
    return supplement;
}

const SlotEvidenceSupplement& protected_colored_beat_supplement() {
    static const auto supplement = make_protected_beat_supplement(true);
    return supplement;
}

ReceiverSlotScore score_stages(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                               const Crypto& key, std::span<const Complex> iq,
                               std::span<const std::uint8_t> valid, unsigned workers,
                               std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared,
                               bool may_overlap, const SlotEvidenceSupplement* supplement = nullptr,
                               EvidenceEvaluation evaluation = EvidenceEvaluation::bounded_competition,
                               AffineGainEvaluation gain = AffineGainEvaluation::operational,
                               AffineContinuationContext* continuation = nullptr,
                               const ProtectedScoringPlan* protection = nullptr,
                               bool discover_protected_affine = false, bool carrier_beat_polarity = false) {
    check_cancelled(stop);
    using ProbePhase = CoarseProbeObservation::Phase;
    auto* probe = slot.index == 0 ? config.coarse_probe_progress.get() : nullptr;
    if (probe) probe->set_phase(ProbePhase::preparing);
    const auto workspace = protection ? protection->restricted : plan_workspace(config, slot, workers, may_overlap, supplement);
    const auto& public_workspace = protection ? protection->original : workspace;
    const auto& likelihood_slot = protection ? protection->aperture : slot;
    require(iq.size() == slot.stop_sample - slot.first_sample &&
        (valid.empty() || valid.size() == iq.size()), "Robust input differs from the full public observation");
    if (workspace.total > memory_limit_bytes)
        throw ReceiverWorkspaceError("Robust receiver slot exceeds its admitted memory lease");

    ReceiverSlotScore score;
    score.wrong_key_admission = config.wrong_key_admission;
    score.midpoint_timing_status = workspace.midpoint_timing_status;
    score.index = slot.index;
    score.first_sample = slot.first_sample;
    score.stop_sample = slot.stop_sample;
    score.quiet_first_sample = slot.quiet_first_sample;
    score.quiet_stop_sample = slot.quiet_stop_sample;
    score.sample_rate_hz = config.sample_rate_hz;
    const auto final_threshold = calendar_operational_threshold(slot.calendar_admission);
    score.calendar_opportunities_per_day = slot.calendar_admission.opportunities_per_day();
    if (score.calendar_opportunities_per_day) score.operational_threshold = final_threshold;
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
    if (likelihood_slot.first_sample == likelihood_slot.stop_sample) {
        score.reason = "protected_observation_has_no_samples";
        if (probe) probe->set_phase(ProbePhase::resolved);
        return score;
    }
    // A standalone slot owns one pool for all of its stages.
    const auto owned = shared ? nullptr : std::make_unique<ReceiverExecutor>(std::max(1U, workers));
    const auto executor = shared ? shared : owned.get();
    HelperResult helper;
    FrequencyInterval fallback;
    const auto acquire = [&](const LaneWave& wave, std::stop_token job_stop) {
        if (workspace.helper_transfer_full_prior) {
            check_cancelled(job_stop);
            helper.reason = "common_motion_requires_full_data_frequency_prior";
        } else {
            const auto began = Clock::now();
            helper = score_helper(config, slot, iq, valid, workspace.helper, job_stop, wave);
            score.helper_seconds = elapsed(began);
        }
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
        if (probe) probe->set_phase(ProbePhase::resolved);
        return score;
    }

    auto began = Clock::now();
    CoarseResult coarse;
    auto search = config;
    search.time_varying_channel = uses_varying_coarse(public_workspace);
    const auto midpoint_evaluation = public_workspace.varying.midpoint_tracking ?
        MidpointTimingEvaluation::short_tracking : MidpointTimingEvaluation::off;
    if (overlap) {
        score.helper_overlapped = true;
        coarse = score_coarse(search, slot, key, iq,
            FrequencyAssistance{acquire, {workspace.helper, helper_widest_wave}},
            workers, workspace.overlapped_coarse, stop, executor, midpoint_evaluation,
            CoarseInputCheck::already_finite);
    } else {
        const auto intervals = acquire(lanes, stop);
        began = Clock::now();
        std::optional<CoarseInputSupport> support;
        if (protection && slot.scheduled_gap_fill &&
            (likelihood_slot.first_sample != slot.first_sample || likelihood_slot.stop_sample != slot.stop_sample)) {
            // Keep the original full public grid, including segmented timing
            // and midpoint gain nominations. The known possible fill support
            // is removed before the observation filter, on every backend.
            support = CoarseInputSupport{static_cast<std::size_t>(likelihood_slot.first_sample - slot.first_sample),
                static_cast<std::size_t>(likelihood_slot.stop_sample - slot.first_sample)};
        }
        coarse = score_coarse(search, slot, key, iq, intervals, workers, workspace.coarse, stop, executor,
                              midpoint_evaluation, CoarseInputCheck::already_finite, support);
    }
    score.coarse_seconds = elapsed(began);
    score.coarse_workspaces = coarse.workspaces;
    score.coarse_frequency_points = coarse.frequency_points;
    score.coarse_arrival_points = coarse.arrival_points;
    score.coarse_projected_rate_hz = coarse.projected_sample_rate_hz;
    score.coarse_fft_precision = coarse.work.plan.fft_precision;
    score.coarse_precision_reason = coarse.work.plan.precision_reason;
    score.varying_status = workspace.varying.status;
    score.drift_status = workspace.drift.status;
    score.drift_planned_stage_workers = workspace.drift.stage_workers;
    score.drift_line_planned_stage_workers = workspace.line_drift.stage_workers;
    score.drift_line_status = workspace.line_drift.workspace_bytes ? "eligible_not_executed" :
        (workspace.line_drift.status.empty() ? "not_planned" : workspace.line_drift.status);
    score.coherent_gain_status = workspace.coherent_gain.status;
    score.varying_coarse_seconds = coarse.work.varying_proposals.wall_seconds;
    score.varying_timing_blocks = coarse.work.segmented_timing_blocks;
    score.varying_timing_frequencies = coarse.work.segmented_timing_frequencies;
    score.varying_timing_status = coarse.work.segmented_timing_status;
    score.varying_timing_proposal_count =
        std::min(coarse.segmented_timing_coordinates.size(), score.varying_timing_proposals.size());
    for (std::size_t i = 0; i < score.varying_timing_proposal_count; ++i) {
        const auto& proposal = coarse.segmented_timing_coordinates[i];
        score.varying_timing_proposals[i] = {proposal.epoch_offset_seconds, proposal.arrival_seconds,
                                             proposal.frequency_hz, proposal.clock_ppm};
    }
    if (coarse.coordinates.empty()) {
        score.reason = std::move(coarse.reason);
        if (probe) probe->set_phase(ProbePhase::resolved);
        return score;
    }
    if (probe) {
        // Leave the trace on the original coarse span during supplemental work.
        search.coarse_probe_progress.reset();
        probe->set_phase(ProbePhase::fine);
    }
    auto fine = prepare_fine_coordinates(config, slot, coarse);
    // Full-window proposal selection can favor a strong neighbor. Its values
    // contribute no likelihood. Every admitting statistic observes this
    // fixed interval only, with whole transforms on its actual samples.
    const auto likelihood_iq = iq.subspan(likelihood_slot.first_sample - slot.first_sample,
                                          likelihood_slot.stop_sample - likelihood_slot.first_sample);
    const auto& quiet_slot = protection && protection->quiet ? *protection->quiet : likelihood_slot;
    const auto quiet_iq = iq.subspan(quiet_slot.first_sample - slot.first_sample,
                                     quiet_slot.stop_sample - quiet_slot.first_sample);

    began = Clock::now();
    score_fine(config, likelihood_slot, key, likelihood_iq, fine.coordinates, coarse.projected_sample_rate_hz,
               coarse.clock_gap_ppm, workers, score, workspace.fine, stop, executor, gain);
    if (probe) probe->set_phase(ProbePhase::additional);
    if (protection) {
        score.first_sample = slot.first_sample;
        score.stop_sample = slot.stop_sample;
        score.quiet_first_sample = slot.quiet_first_sample;
        score.quiet_stop_sample = slot.quiet_stop_sample;
        // The full coarse pass consumed the entire observation. Fine
        // completion still requires scoring all of the protected aperture.
        if (score.complete && score.consumed_samples == likelihood_iq.size()) score.consumed_samples = iq.size();
        // A larger DATA crop can shift the whole-block FFT calendar.
        // Score quiet on the unchanged original blocks in that case.
        if (score.complete && protection->quiet)
            score_packed_affine_quiet(config, quiet_slot, quiet_iq, score, stop);
    }
    const bool varying = workspace.varying.block_rows != 0;
    const auto drift = workspace.drift.workspace_bytes ? &workspace.drift : nullptr;
    const auto separation = config.time_varying_channel ? config.time_varying_minimum_bit_log_separation : 0.;
    const AdmissionGrid varying_grid(fine.grid.lower, fine.grid.upper,
        config.frequency_bound_hz + config.time_varying_atmospheric_hz,
        config.clock_bound_ppm, fine.grid.frequency_step);
    std::array<FineCoordinate, maximum_midpoint_timing_coordinates> midpoint_coordinates{};
    std::optional<MidpointTrackInput> midpoint;
    if (workspace.varying.midpoint_tracking) {
        require(coarse.midpoint_timing && coarse.midpoint_timing->lag_count &&
            coarse.midpoint_timing->proposals.size() <= midpoint_coordinates.size() &&
            coarse.midpoint_timing->gain_proposal_count <= coarse.midpoint_timing->gain_proposal_lags.size(),
            "Robust coarse midpoint timing did not retain its admitted public family");
        const auto& timing = *coarse.midpoint_timing;
        std::ranges::transform(timing.proposals, midpoint_coordinates.begin(),
            [](const auto& proposal) { return proposal.coordinate; });
        const auto proposed_gain = std::span(timing.gain_proposal_lags).first(timing.gain_proposal_count);
        midpoint.emplace(std::span(midpoint_coordinates).first(timing.proposals.size()), timing.lag_count,
            varying_grid.log_count() - std::log1p(2 * varying_grid.frequency_radius),
            proposed_gain, timing.gain_fft_rows != 0, timing.first_lag, timing.middle_seconds);
        score.midpoint_timing_lags = timing.lag_count;
        score.midpoint_timing_workspace_bytes = timing.workspace_bytes;
        score.midpoint_timing_precision = timing.precision;
        score.midpoint_timing_widened_inverses = timing.widened_inverses;
        score.midpoint_timing_scored_lags = timing.scored_lags;
        score.signed_lag_gain_proposal_status = timing.gain_status;
        score.signed_lag_gain_proposal_maps = proposed_gain.size();
        score.signed_lag_gain_proposal_fixed_maps = timing.gain_fixed_proposal_count;
        score.signed_lag_gain_proposal_lags = timing.gain_proposal_lags;
        score.signed_lag_gain_proposal_fft_rows = timing.gain_fft_rows;
        score.signed_lag_gain_proposal_frequency_points = timing.gain_frequency_points;
        score.signed_lag_gain_proposal_fixed_frequency_points = timing.gain_fixed_frequency_points;
        score.signed_lag_gain_proposal_frequency_status = timing.gain_frequency_status;
        score.signed_lag_gain_proposal_available_blocks = timing.gain_available_blocks;
        score.signed_lag_gain_proposal_sampled_blocks = timing.gain_sampled_blocks;
        score.signed_lag_gain_proposal_scored_blocks = timing.gain_blocks;
        score.signed_lag_gain_proposal_direct_scored_blocks = timing.gain_direct_blocks;
        score.signed_lag_gain_proposal_projected_rows = timing.gain_projected_rows;
        score.signed_lag_gain_proposal_workspace_bytes = timing.gain_workspace_bytes;
        score.signed_lag_gain_proposal_fft_stages = timing.gain_fft_row_stages;
        score.signed_lag_gain_proposal_score_rows = timing.gain_score_rows;
        score.signed_lag_gain_proposal_fft_stage_bound = timing.gain_fft_stage_bound;
        score.signed_lag_gain_proposal_score_row_bound = timing.gain_score_row_bound;
        score.signed_lag_gain_proposal_seconds = timing.gain_seconds;
        score.signed_lag_gain_proposal_route = timing.gain_route == GainRankRoute::device ? gain_device_route :
            timing.gain_route == GainRankRoute::native ? "native" : "";
        score.signed_lag_gain_proposal_device_fft_stages = timing.gain_device_fft_row_stages;
        score.signed_lag_gain_proposal_rescore_fft_stages = timing.gain_rescore_fft_row_stages;
        score.signed_lag_gain_proposal_rescored_lags = timing.gain_rescored_lags;
        score.signed_lag_gain_proposal_top2_gap = timing.gain_top2_gap;
    }
    const auto selected = fine.varying_coordinates.empty() ? std::span(fine.coordinates) :
                                                            std::span(fine.varying_coordinates);
    const auto varying_prior = public_workspace.varying.block_rows ? &varying_grid : nullptr;
    const auto coherent_gain_prior = public_workspace.coherent_gain.block_rows ? &varying_grid : nullptr;
    const auto drift_prior = public_workspace.drift.workspace_bytes ? &public_workspace.drift : nullptr;
    const bool has_supplement = workspace.evidence_supplement != 0;
    std::array<double, 2> supplement_evidence{
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    // The weight is reserved from the first classifier onward. Skipping a
    // branch after a sufficient earlier result never restores that mass.
    const auto ordinary_scores = [&](const ReceiverSlotScore& target) {
        auto ordinary = varying_operational_scores(target, fine.grid, varying_prior, drift_prior,
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
        auto residual_score = varying_operational_scores(residual, fine.grid, varying_prior, drift_prior,
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
        if (public_workspace.line) add_line_branch(score, score, -std::numeric_limits<double>::infinity());
        else if (has_supplement || coherent_gain_prior)
            classify_operational_scores(score, ordinary_scores(score), separation);
        else classify_varying_slot(score, fine.grid, varying_prior, drift_prior, separation);
    };
    // The slot's varying batch receipts cover its ordinary, coherent-gain
    // and line-conditioned varying stages: phase seconds and batches add,
    // and the quote and blocks per batch are the largest stage's. The
    // ordinary stage scores into the slot score itself.
    const auto add_varying_batches = [&](const ReceiverSlotScore& stage) {
        score.varying_projection_seconds += stage.varying_projection_seconds;
        score.varying_statistics_seconds += stage.varying_statistics_seconds;
        score.varying_recursion_seconds += stage.varying_recursion_seconds;
        score.varying_wait_seconds += stage.varying_wait_seconds;
        score.varying_batches = checked_add(score.varying_batches, stage.varying_batches);
        score.varying_batch_blocks = std::max(score.varying_batch_blocks, stage.varying_batch_blocks);
        score.varying_workspace_bytes = std::max(score.varying_workspace_bytes, stage.varying_workspace_bytes);
    };
    const auto score_coherent_gain = [&](const VaryingFinePlan& plan, ReceiverSlotScore& target,
                                         const LineAtom* line) {
        ReceiverSlotScore coherent;
        score_varying_fine(config, likelihood_slot, key, plan, likelihood_iq, selected, fine.segmented_timing_coordinates,
            coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, coherent, stop, *executor, line);
        add_varying_batches(coherent);
        target.coherent_gain_candidates = coherent.varying_candidates;
        target.coherent_gain_bit_evidence = coherent.varying_bit_evidence;
        target.coherent_gain_blocks = coherent.varying_blocks;
        target.coherent_gain_frequency_states = coherent.varying_frequency_states;
        target.coherent_gain_seconds = coherent.varying_fine_seconds;
    };
    score.selected_prior_mass = std::exp(std::log(static_cast<double>(score.fine_candidates)) - fine.grid.log_count());
    classify_supplement();
    const bool coherent_admission = score.decision == SlotDecision::data;
    // A comparison is meaningful only where at least one alternative has
    // a native public plan. Otherwise keep the inherited affine/line path.
    const bool compare_all = evaluation == EvidenceEvaluation::compare_admitted &&
        (varying_prior || coherent_gain_prior || drift);
    // A short-block plan can compete with strong affine evidence from the
    // same observation. Longer models still run as needed for admission.
    if (varying) {
        const auto raw_timing = workspace.varying.track_frequency ?
            prepare_short_tracking_timing(config, slot, coarse, fine.grid) : std::vector<FineCoordinate>{};
        const auto timing = raw_timing.empty() ? std::span(fine.segmented_timing_coordinates) : std::span(raw_timing);
        score_varying_fine(config, likelihood_slot, key, workspace.varying, likelihood_iq, selected,
                           timing,
                           coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, score, stop, *executor,
                           nullptr, nullptr, nullptr, midpoint ? &*midpoint : nullptr);
        classify_supplement();
    }
    if (workspace.coherent_gain.block_rows && (compare_all || score.decision != SlotDecision::data)) {
        score_coherent_gain(workspace.coherent_gain, score, nullptr);
        classify_supplement();
    } else if (workspace.coherent_gain.block_rows) score.coherent_gain_status = "earlier_evidence_sufficient";
    std::optional<std::uint8_t> drift_admitted_bit;
    if (drift && (compare_all || score.decision != SlotDecision::data)) {
        const bool needs_drift = score.decision != SlotDecision::data;
        score_drift_fine(config, likelihood_slot, key, *drift, likelihood_iq, selected, fine.epoch_timing_coordinates,
                         workers, score, stop, *executor);
        classify_supplement();
        if (continuation && needs_drift && config.random_doppler_width_hz > 0 && config.spread_doppler_hz == 0 &&
            score.drift_timing_atoms && score.drift_slopes && score.decision == SlotDecision::data)
            drift_admitted_bit = score.bit;
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
    // The early comparison also checks the line-conditioned short model
    // when planned. Keep longer line models on the same fallback policy.
    // If a line is retained, discard the new likelihood permanently: its
    // raw-IQ null model is not a line-conditioned model. Both canonical
    // branches retain (1-weight), with no observation-dependent renormalizing.
    const bool compare_line_short = varying && workspace.line_varying.block_rows;
    const auto newly_passes_calendar = [&] {
        return score.calendar_opportunities_per_day && score.complete && score.zero_h0_verified &&
            !score.operational_passes_conditional_bound && score.operational_log_e >= final_threshold;
    };
    bool retained_line = false;
    if (workspace.line && (compare_all || compare_line_short || !coherent_admission) &&
            (score.operational_passes_conditional_bound || newly_passes_calendar())) {
        // Keep the original nuisance selection and residual fallback for
        // previously eligible slots. Newly eligible slots use the calendar
        // limit in both phases of line selection.
        const auto line_threshold = score.operational_passes_conditional_bound ?
            receiver_operational_threshold() : final_threshold;
        if (const auto line = find_varying_line(config, likelihood_slot, likelihood_iq, stop, line_threshold)) {
            if (has_supplement)
                supplement_evidence.fill(-std::numeric_limits<double>::infinity());
            ReceiverSlotScore residual;
            residual.fine_candidates = score.fine_candidates;
            if (workspace.line_varying.block_rows) {
                score_varying_fine(config, likelihood_slot, key, workspace.line_varying, likelihood_iq, selected,
                    fine.segmented_timing_coordinates,
                    coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, residual, stop, *executor, &*line);
                add_varying_batches(residual);
            }
            add_line_branch(score, residual, line->log_weighted_evidence);
            if (workspace.line_coherent_gain.block_rows && (compare_all || score.decision != SlotDecision::data)) {
                score_coherent_gain(workspace.line_coherent_gain, residual, &*line);
                add_line_branch(score, residual, line->log_weighted_evidence);
            }
            if (workspace.line_drift.workspace_bytes && (compare_all || score.decision != SlotDecision::data)) {
                score_drift_fine(config, likelihood_slot, key, workspace.line_drift, likelihood_iq, selected, fine.epoch_timing_coordinates,
                    workers, residual, stop, *executor, &*line);
                score.drift_line_status = workspace.line_drift.status;
                score.drift_line_stage_workers = residual.drift_stage_workers;
                score.drift_line_proposal_route = residual.drift_proposal_route;
                score.drift_line_device_atoms = residual.drift_device_atoms;
                score.drift_line_retained_digest = residual.drift_retained_digest;
                score.drift_coherent_timing_fft_rows = checked_add(score.drift_coherent_timing_fft_rows,
                    residual.drift_coherent_timing_fft_rows);
                score.drift_coherent_timing_fft_stages = checked_add(score.drift_coherent_timing_fft_stages,
                    residual.drift_coherent_timing_fft_stages);
                score.drift_render_seconds += residual.drift_render_seconds;
                score.drift_ridge_seconds += residual.drift_ridge_seconds;
                score.drift_ridge_search_seconds += residual.drift_ridge_search_seconds;
                score.drift_slope_seconds += residual.drift_slope_seconds;
                score.drift_exact_seconds += residual.drift_exact_seconds;
                add_line_branch(score, residual, line->log_weighted_evidence);
            }
            score.varying_line_frequency_hz = line->frequency_hz;
            score.varying_line_log_evidence = line->log_weighted_evidence;
            retained_line = true;
        }
    }
    // Only final admission uses the calendar limit. Earlier weak evidence
    // must still leave the same opportunity for the original fallback models.
    if (newly_passes_calendar())
        classify_operational_scores(score, score.operational_bit_evidence, separation, score.operational_log_e, final_threshold);
    else score.operational_threshold = final_threshold;
    if (retained_line) {
        if (score.decision == SlotDecision::data) score.reason = "varying_data_evidence_with_coherent_line";
        else {
            score.decision = SlotDecision::ambiguous;
            score.reason = "coherent_line_in_public_data_aperture";
            score_varying_quiet(config, quiet_slot, quiet_iq, score, stop, workers, *executor);
        }
    }
    if (config.time_varying_channel && score.decision == SlotDecision::quiet) {
        score_varying_quiet(config, quiet_slot, quiet_iq, score, stop, workers, *executor);
        if (!score.varying_quiet_modes || score.varying_quiet_energy > score.varying_quiet_threshold) {
            score.decision = SlotDecision::ambiguous;
            score.reason = score.varying_quiet_status;
        }
    }
    const auto affine_atom_log_mass = -fine.grid.log_count() -
        std::log(1. + (varying_prior != nullptr) + (drift_prior != nullptr) + (coherent_gain_prior != nullptr)) -
        (public_workspace.line ? std::numbers::ln2 : 0.) +
        (has_supplement ? std::log1p(-supplement->fixed_weight) : 0.);
    const bool scheduled_carrier_beat = !protection && slot.carrier_beat_stop_sample && !varying && !supplement &&
        evaluation == EvidenceEvaluation::bounded_competition && gain == AffineGainEvaluation::operational;
    const bool improve_guarded_margin = scheduled_carrier_beat &&
        !wrong_key_admission_passes(score, WrongKeyAdmission::guarded);
    const auto provisional_bit = score.decision == SlotDecision::data ? score.bit : std::nullopt;
    apply_wrong_key_admission(score, config.wrong_key_admission);
    if (protection && discover_protected_affine && likelihood_iq.size() < iq.size() &&
        score.decision != SlotDecision::data && score.complete && score.all_valid &&
        score.zero_h0_verified && !score.wrong_key_rejected && !retained_line) {
        // The original full-window search can nominate a strong neighbor
        // instead of the wanted signal. Search the same public geometry with
        // just the protected samples for one more set of affine proposals.
        // The masked transform only nominates: likelihoods use the native,
        // unmasked protected observation and retain their original grid mass.
        const auto discover = [&] {
            FrequencyInterval full{-search.frequency_bound_hz, search.frequency_bound_hz};
            const auto coarse_started = Clock::now();
            score.protected_discovery_attempted = true;
            const auto record_coarse_cost = [&] {
                score.protected_discovery_coarse_seconds = elapsed(coarse_started);
                score.coarse_seconds += score.protected_discovery_coarse_seconds;
            };
            CoarseResult masked;
            try {
                const auto first = static_cast<std::size_t>(likelihood_slot.first_sample - slot.first_sample);
                masked = score_coarse(search, slot, key, iq, std::span(&full, 1), workers,
                    workspace.coarse, stop, executor, MidpointTimingEvaluation::off,
                    CoarseInputCheck::already_finite, CoarseInputSupport{first, first + likelihood_iq.size()});
            } catch (...) {
                record_coarse_cost();
                throw;
            }
            record_coarse_cost();
            if (masked.coordinates.empty() || masked.projected_sample_rate_hz != coarse.projected_sample_rate_hz ||
                masked.maximum_frequency_step_hz != coarse.maximum_frequency_step_hz ||
                masked.clock_gap_ppm != coarse.clock_gap_ppm)
                return;
            auto extra = prepare_fine_coordinates(config, slot, masked).coordinates;
            std::erase_if(extra, [&](const auto& atom) {
                return std::ranges::find(fine.coordinates, atom) != fine.coordinates.end();
            });
            if (extra.empty()) return;

            ReceiverSlotScore atoms;
            score_fine(config, likelihood_slot, key, likelihood_iq, extra, coarse.projected_sample_rate_hz,
                       coarse.clock_gap_ppm, workers, atoms, workspace.fine, stop, executor, gain);
            score.protected_discovery_candidates = atoms.fine_candidates;
            score.basis_rows = checked_add(score.basis_rows, atoms.basis_rows);
            if (!atoms.complete || !atoms.all_valid || !atoms.zero_h0_verified) return;

            auto candidate = score;
            const auto log_count = std::log(static_cast<double>(atoms.fine_candidates));
            const auto log_mass = log_count + affine_atom_log_mass;
            std::array<double, 2> operational{};
            for (std::size_t bit = 0; bit < operational.size(); ++bit)
                operational[bit] = log_add(score.operational_bit_evidence[bit], atoms.bit_evidence[bit] + log_mass);
            classify_operational_scores(candidate, operational, separation, std::nullopt, final_threshold);
            if (candidate.decision != SlotDecision::data) return;
            if (workspace.line) {
                const auto line_threshold = candidate.operational_log_e >= receiver_operational_threshold() ?
                    receiver_operational_threshold() : final_threshold;
                if (find_varying_line(config, likelihood_slot, likelihood_iq, stop, line_threshold)) return;
            }
            apply_wrong_key_admission(candidate, config.wrong_key_admission);
            if (candidate.decision != SlotDecision::data) return;

            candidate.protected_discovery_admitted = true;
            candidate.reason = "protected_affine_discovery";
            retain_affine_atoms(fine, candidate, atoms, extra);
            score = std::move(candidate);
        };
        try {
            discover();
        } catch (const ReceiverWorkspaceError&) {
            // An optional proposal pass must preserve the ordinary answer.
        } catch (const std::bad_alloc&) {
            // Keep the original classification if optional allocation fails.
        }
    }
    const auto ordinary_bit = score.bit ? score.bit : provisional_bit;
    if (((protection && carrier_beat_polarity) || scheduled_carrier_beat) && ordinary_bit &&
        score.complete && score.all_valid && score.zero_h0_verified &&
        config.time_varying_channel && config.selective_modulation_rate_hz && !retained_line) {
        const auto revise = [&] {
            auto beat_slot = slot;
            if (scheduled_carrier_beat) {
                // Only the timing statistic sees this interior. The full
                // observation and public grid still control admission.
                beat_slot.quiet_first_sample = slot.carrier_beat_first_sample;
                beat_slot.quiet_stop_sample = slot.carrier_beat_stop_sample;
            } else beat_slot = protected_beat_observation(config, slot);
            // The native beat search quotes and checks its own scratch
            // against the available sequential fine-stage lease.
            score.protected_carrier_beat = find_protected_carrier_beat(config, beat_slot, key, iq, valid,
                score, *ordinary_bit, workspace.fine, stop, scheduled_carrier_beat ? &fine : nullptr,
                improve_guarded_margin);
            auto& beat = score.protected_carrier_beat;
            const bool allow_same_bit = scheduled_carrier_beat ? improve_guarded_margin : score.decision != SlotDecision::data;
            if (!beat || (beat->predicted_bit == ordinary_bit && !allow_same_bit)) return;
            const auto coordinate = snap({beat->epoch_offset_seconds, beat->arrival_seconds, 0., beat->clock_ppm},
                                         fine.grid);
            if (fine.coordinates.size() >= maximum_fine_coordinates ||
                std::ranges::find(fine.coordinates, coordinate) != fine.coordinates.end()) return;
            ReceiverSlotScore atom;
            score_fine(config, likelihood_slot, key, likelihood_iq, std::span(&coordinate, 1),
                       coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, atom,
                       workspace.fine, stop, executor, gain);
            score.basis_rows = checked_add(score.basis_rows, atom.basis_rows);
            if (!atom.complete || !atom.all_valid || !atom.zero_h0_verified) return;
            const auto log_e = atom.bit_evidence[beat->predicted_bit] + affine_atom_log_mass - std::numbers::ln2;
            if (!std::isfinite(log_e)) return;
            beat->affine_support.emplace(coordinate.arrival_seconds, coordinate.frequency_hz,
                                         coordinate.clock_ppm, log_e);

            // Add both bits at this single omitted public cell. Selecting
            // the cell with the beat statistic cannot increase its fixed
            // mixture weight. The beat is never used as a likelihood.
            auto candidate = score;
            std::array<double, 2> operational{};
            for (std::size_t bit = 0; bit < operational.size(); ++bit)
                operational[bit] = log_add(score.operational_bit_evidence[bit],
                                           atom.bit_evidence[bit] + affine_atom_log_mass);
            classify_operational_scores(candidate, operational, separation, std::nullopt, final_threshold);
            if (scheduled_carrier_beat && beat->predicted_bit == ordinary_bit &&
                !wrong_key_admission_passes(candidate, WrongKeyAdmission::guarded)) return;
            apply_wrong_key_admission(candidate, config.wrong_key_admission);
            // Scheduled proposals can repair a decision only when the
            // full-window native likelihood corroborates the bit.
            if (scheduled_carrier_beat &&
                (candidate.decision != SlotDecision::data || candidate.bit != beat->predicted_bit)) return;
            if (workspace.line) {
                const auto line_threshold = candidate.operational_log_e >= receiver_operational_threshold() ?
                    receiver_operational_threshold() : final_threshold;
                if (find_varying_line(config, likelihood_slot, likelihood_iq, stop, line_threshold)) return;
            }

            candidate.protected_carrier_beat_corrected_polarity = candidate.bit && candidate.bit != ordinary_bit;
            if (candidate.decision == SlotDecision::data)
                candidate.reason = scheduled_carrier_beat ? "carrier_beat_affine_nomination" :
                    "protected_carrier_beat_affine_nomination";
            retain_affine_atoms(fine, candidate, atom, std::span(&coordinate, 1));
            score = std::move(candidate);
        };
        try {
            revise();
        } catch (const ReceiverWorkspaceError&) {
            // Optional scoring leaves the ordinary result intact.
        } catch (const std::bad_alloc&) {
            // Optional scoring leaves the ordinary result intact.
        }
    }
    const bool refine_admitted = drift_admitted_bit && score.decision == SlotDecision::data &&
        score.bit == drift_admitted_bit && score.operational_passes_conditional_bound &&
        score.operational_log_e >= score.operational_threshold;
    if (continuation && (score.decision != SlotDecision::data || refine_admitted) && score.complete && score.all_valid &&
        score.zero_h0_verified && !score.wrong_key_rejected && !retained_line && !has_supplement &&
        fine.coordinates.size() < maximum_fine_coordinates) {
        continuation->eligible = true;
        continuation->refine_admitted = refine_admitted;
        continuation->check_line = workspace.line != 0;
        continuation->scratch_bytes = std::max(workspace.fine, workspace.line);
        continuation->ordinary_count = fine.coordinates.size();
        std::ranges::copy(fine.coordinates, continuation->ordinary.begin());
        continuation->grid = fine.grid;
        continuation->projected_sample_rate_hz = coarse.projected_sample_rate_hz;
        continuation->coarse_clock_gap_ppm = coarse.clock_gap_ppm;
        // Match the fixed family mixture and the absent-line branch of
        // ordinary admission. Preserve the full affine grid's atom mass.
        continuation->atom_log_mass = affine_atom_log_mass;
    }
    score.fine_seconds = elapsed(began) - score.protected_discovery_coarse_seconds;
    check_cancelled(stop);
    if (probe) probe->set_phase(ProbePhase::resolved);
    return score;
}
} // namespace

void receiver_detail::retain_affine_atoms(FinePreparation& fine, ReceiverSlotScore& candidate,
        const ReceiverSlotScore& added, std::span<const FineCoordinate> coordinates) {
    require(added.fine_candidates == coordinates.size() && !coordinates.empty() &&
        candidate.fine_candidates == fine.coordinates.size() && candidate.fine_candidates > 0 &&
        candidate.fine_candidates < maximum_fine_coordinates &&
        coordinates.size() <= maximum_fine_coordinates - candidate.fine_candidates,
        "invalid Robust affine atom bank");
    const auto old_count = std::log(static_cast<double>(candidate.fine_candidates));
    const auto added_count = std::log(static_cast<double>(coordinates.size()));
    fine.coordinates.insert(fine.coordinates.end(), coordinates.begin(), coordinates.end());
    candidate.fine_candidates = fine.coordinates.size();
    const auto new_count = std::log(static_cast<double>(candidate.fine_candidates));
    for (std::size_t bit = 0; bit < candidate.bit_evidence.size(); ++bit) {
        const auto old_total = candidate.bit_evidence[bit] + old_count;
        const auto added_total = added.bit_evidence[bit] + added_count;
        if (added_total > old_total || !candidate.affine_tracking[bit])
            candidate.affine_tracking[bit] = added.affine_tracking[bit];
        candidate.bit_evidence[bit] = log_add(old_total, added_total) - new_count;
    }
    candidate.selected_prior_mass = std::exp(new_count - fine.grid.log_count());
}

ReceiverSlotScore receiver_detail::score_slot(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared, true);
}

ReceiverSlotScore receiver_detail::score_protected_slot(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* executor,
        bool discover_protected_affine, bool carrier_beat_polarity, bool white_beat_evidence,
        bool colored_beat_evidence) {
    check_cancelled(stop);
    require(!white_beat_evidence || !colored_beat_evidence, "Choose one Robust qualification noise model");
    const auto* supplement = white_beat_evidence ? &protected_white_beat_supplement() :
        colored_beat_evidence ? &protected_colored_beat_supplement() : nullptr;
    const auto plan = plan_protected_observation(config, slot, workers, supplement);
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, executor, false,
                        supplement, EvidenceEvaluation::bounded_competition, AffineGainEvaluation::operational,
                        nullptr, &plan, discover_protected_affine, carrier_beat_polarity);
}

ReceiverSlotScore receiver_detail::score_protected_data_slot(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        std::span<const std::uint8_t> valid, unsigned workers, std::size_t memory_limit_bytes,
        std::stop_token stop, ReceiverExecutor* executor, AffineContinuationContext* continuation) {
    if (continuation) *continuation = {};
    const auto plan = plan_protected_observation(config, slot, workers);
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, executor, false,
                        nullptr, EvidenceEvaluation::bounded_competition, AffineGainEvaluation::operational,
                        continuation, &plan, !slot.scheduled_gap_fill, true);
}

std::size_t receiver_detail::protected_white_beat_slot_workspace(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_protected_observation(config, slot, workers, &protected_white_beat_supplement()).restricted.total;
}

std::size_t receiver_detail::protected_colored_beat_slot_workspace(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_protected_observation(config, slot, workers, &protected_colored_beat_supplement()).restricted.total;
}

ReceiverSlotScore receiver_detail::score_slot_for_continuation(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        std::span<const std::uint8_t> valid, unsigned workers, std::size_t memory_limit_bytes,
        std::stop_token stop, ReceiverExecutor* executor, AffineContinuationContext& continuation) {
    continuation = {};
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, executor, true,
                        nullptr, EvidenceEvaluation::bounded_competition, AffineGainEvaluation::operational,
                        &continuation);
}

ReceiverSlotScore receiver_detail::continue_affine_slot(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        unsigned workers, std::size_t scratch_limit_bytes, std::stop_token stop, ReceiverExecutor* executor,
        const AffineContinuationContext& context, const ReceiverSlotScore::TimingProposal& predecessor,
        ReceiverSlotScore baseline, bool protected_data) {
    check_cancelled(stop);
    const bool already_admitted = baseline.decision == SlotDecision::data;
    if (!context.eligible || scratch_limit_bytes < context.scratch_bytes ||
        (already_admitted && (!context.refine_admitted || !baseline.bit)) ||
        baseline.wrong_key_rejected || !baseline.complete || !baseline.all_valid || !baseline.zero_h0_verified)
        return baseline;
    require(context.grid && context.ordinary_count == baseline.fine_candidates &&
        context.ordinary_count < maximum_fine_coordinates && baseline.index == slot.index &&
        baseline.first_sample == slot.first_sample && baseline.stop_sample == slot.stop_sample &&
        iq.size() == slot.stop_sample - slot.first_sample, "invalid Robust affine continuation context");
    const auto& grid = *context.grid;
    const auto frequency_limit = grid.frequency_radius * grid.frequency_step;
    if (!(predecessor.arrival_seconds >= grid.lower && predecessor.arrival_seconds <= grid.upper &&
          std::abs(predecessor.frequency_hz) <= frequency_limit &&
          std::abs(predecessor.clock_ppm) <= grid.clock_bound) ||
        std::ranges::find(receiver_epoch_offsets, predecessor.epoch_offset_seconds) == receiver_epoch_offsets.end())
        return baseline;
    const auto coordinate = snap({predecessor.epoch_offset_seconds, predecessor.arrival_seconds,
        predecessor.frequency_hz, predecessor.clock_ppm}, grid);
    const auto ordinary = std::span(context.ordinary).first(context.ordinary_count);
    if (std::ranges::find(ordinary, coordinate) != ordinary.end()) return baseline;

    // Selection by a predecessor is allowed even if the raw windows overlap:
    // only the nominated index is reused. Fresh likelihood at this unique
    // cell belongs to the same fixed full-grid mixture as the ordinary bank.
    const auto began = Clock::now();
    const auto inference = protected_data ? protected_data_observation(config, slot) : slot;
    const auto observation = iq.subspan(inference.first_sample - slot.first_sample,
                                        inference.stop_sample - inference.first_sample);
    ReceiverSlotScore atom;
    score_fine(config, inference, key, observation, std::span(&coordinate, 1), context.projected_sample_rate_hz,
               context.coarse_clock_gap_ppm, workers, atom, context.scratch_bytes, stop, executor);
    baseline.affine_continuation_attempted = true;
    baseline.basis_rows = checked_add(baseline.basis_rows, atom.basis_rows);
    const auto finish = [&](ReceiverSlotScore score) {
        score.fine_seconds += elapsed(began);
        check_cancelled(stop);
        return score;
    };
    if (!atom.complete || !atom.all_valid || !atom.zero_h0_verified) return finish(std::move(baseline));
    std::array<double, 2> weighted_atom{};
    for (std::size_t bit = 0; bit < weighted_atom.size(); ++bit)
        weighted_atom[bit] = atom.bit_evidence[bit] + context.atom_log_mass;
    if (already_admitted) {
        // A drift admission keeps its bit. The fresh affine cell must
        // corroborate it independently before entering the merged bank.
        atom.calendar_opportunities_per_day = baseline.calendar_opportunities_per_day;
        classify_operational_scores(atom, weighted_atom, baseline.operational_minimum_bit_log_separation,
                                    std::nullopt, baseline.operational_threshold);
        if (atom.bit != baseline.bit || !wrong_key_admission_passes(atom, config.wrong_key_admission))
            return finish(std::move(baseline));
    }
    ReceiverSlotScore candidate = baseline;
    std::array<double, 2> operational{};
    for (std::size_t bit = 0; bit < operational.size(); ++bit)
        operational[bit] = log_add(baseline.operational_bit_evidence[bit], weighted_atom[bit]);
    classify_operational_scores(candidate, operational, baseline.operational_minimum_bit_log_separation,
                                std::nullopt, baseline.operational_threshold);
    if (candidate.decision != SlotDecision::data || (already_admitted && candidate.bit != baseline.bit))
        return finish(std::move(baseline));
    if (context.check_line) {
        // A previously weak slot need not have run nuisance selection. If
        // this nomination reveals a line, leave the canonical answer intact.
        const auto threshold = candidate.operational_log_e >= receiver_operational_threshold() ?
            receiver_operational_threshold() : baseline.operational_threshold;
        if (find_varying_line(config, inference, observation, stop, threshold)) return finish(std::move(baseline));
    }
    apply_wrong_key_admission(candidate, config.wrong_key_admission);
    if (candidate.decision != SlotDecision::data) return finish(std::move(baseline));
    const auto old_count = std::log(static_cast<double>(candidate.fine_candidates));
    const auto new_count = std::log(static_cast<double>(++candidate.fine_candidates));
    for (std::size_t bit = 0; bit < operational.size(); ++bit) {
        const auto old_total = candidate.bit_evidence[bit] + old_count;
        if (atom.bit_evidence[bit] > old_total || !candidate.affine_tracking[bit])
            candidate.affine_tracking[bit] = atom.affine_tracking[bit];
        candidate.bit_evidence[bit] = log_add(old_total, atom.bit_evidence[bit]) - new_count;
    }
    candidate.selected_prior_mass = std::exp(new_count - grid.log_count());
    candidate.affine_continuation_admitted = true;
    candidate.reason = "continued_public_affine_coordinate";
    return finish(std::move(candidate));
}

ReceiverSlotScore receiver_detail::score_slot_with_supplement(
        const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory_limit_bytes, std::stop_token stop, ReceiverExecutor* shared,
        const SlotEvidenceSupplement& supplement) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared,
                        true, &supplement);
}

ReceiverSlotScore receiver_detail::score_slot_comparing_admitted_evidence(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        std::span<const std::uint8_t> valid, unsigned workers, std::size_t memory_limit_bytes,
        std::stop_token stop, ReceiverExecutor* shared) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared,
                        true, nullptr, EvidenceEvaluation::compare_admitted);
}

ReceiverSlotScore receiver_detail::score_slot_without_stationary_fading(const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
        std::span<const std::uint8_t> valid, unsigned workers, std::size_t memory_limit_bytes,
        std::stop_token stop, ReceiverExecutor* shared) {
    return score_stages(config, slot, key, iq, valid, workers, memory_limit_bytes, stop, shared,
                        true, nullptr, EvidenceEvaluation::bounded_competition,
                        AffineGainEvaluation::without_stationary_fading);
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
