#include "appraisal/vlf_diagnostic.hpp"

#include "datapump/execution_resources.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_helper.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>

namespace appraisal {
namespace {

using namespace datapump::triad::v3;
using namespace receiver_detail;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using Clock = std::chrono::steady_clock;

struct Timer {
    Clock::time_point start = Clock::now();
    std::optional<double> cpu = detail::stream_process_cpu_seconds();

    Json elapsed() const {
        const auto wall = std::chrono::duration<double>(Clock::now() - start).count();
        const auto end = detail::stream_process_cpu_seconds();
        return object({{"wall_seconds", wall},
                       {"process_cpu_seconds", cpu && end && *end >= *cpu ? Json(*end - *cpu) : Json{}}});
    }
};

Json phase(const CoarsePhaseWork& work) {
    const auto& n = work.operations;
    return object({{"wall_seconds", work.wall_seconds},
                   {"forward_ffts", n.forward_ffts}, {"inverse_ffts", n.inverse_ffts},
                   {"fft_rows", n.fft_rows}, {"fft_row_stages", n.fft_row_stages},
                   {"source_render_rows", n.source_render_rows},
                   {"fir_input_rows", n.fir_input_rows}, {"fir_output_contributions", n.fir_output_contributions},
                   {"workspace_frequency_pairs", n.workspace_frequency_pairs}, {"score_rows", n.score_rows}});
}

Json coarse_plan(const CoarseWorkPlan& p) {
    return object({{"route", p.route == CoarseRoute::lag_groups ? "lag_groups" : "frequency_tiles"},
                   {"projected_sample_rate_hz", p.projected_sample_rate_hz},
                   {"raw_template_rows", p.raw_template_rows},
                   {"projected_template_rows", p.projected_template_rows},
                   {"projected_observation_rows", p.projected_observation_rows},
                   {"template_sampling_step", p.template_sampling_step}, {"fir_taps", p.fir_taps},
                   {"fir_fft_rows", p.fir_fft_rows}, {"correlation_fft_rows", p.correlation_fft_rows},
                   {"frequency_points", p.frequency_points}, {"arrival_points", p.arrival_points},
                   {"workspaces", p.workspaces}, {"rays", p.rays}, {"clocks", p.clocks},
                   {"tile_width", p.tile_width}, {"planned_tiles", p.total_tiles},
                   {"workspace_reserved_bytes", p.workspace_reserved_bytes},
                   {"fft_precision", p.fft_precision}, {"precision_reason", p.precision_reason}});
}

Json coarse_progress(const CoarseProgress& p) {
    return object({{"plan", coarse_plan(p.plan)}, {"completed_tiles", p.completed_tiles},
                   {"selection_completed", p.state == CoarseState::finished},
                   {"refined_coordinates", p.refined_coordinates},
                   {"preparation_seconds", p.preparation_seconds}, {"filter_setup", phase(p.filter_setup)},
                   {"mapped_fidelity", phase(p.mapped_fidelity)}, {"template_bank", phase(p.template_bank)},
                   {"observation", phase(p.observation)}, {"lag_setup", phase(p.lag_setup)},
                   {"bulk_prepare", phase(p.bulk_prepare)}, {"search", phase(p.search)},
                   {"selection", phase(p.selection)}, {"native_fallback_reason", p.native_fallback_reason}});
}

Json lane(const FineLaneWork& l) {
    return object({{"initial_states", l.initial_states}, {"peak_emission_states", l.peak_emission_states},
                   {"peak_retained_states", l.peak_retained_states}, {"transition_edges", l.transition_edges},
                   {"basis_rows", l.basis_rows}, {"positive_basis_blocks", l.positive_basis_blocks},
                   {"gaussian_updates", l.gaussian_updates}});
}

Json checkpoint(const FineProgress& p, Json timing, double elapsed_seconds, double represented_seconds) {
    auto result = object({{"elapsed_observation_seconds", elapsed_seconds},
                          {"new_observation_seconds", represented_seconds}, {"timing", std::move(timing)},
                          {"processed_rows", p.processed_rows}, {"blocks", p.blocks}, {"segments", p.segments},
                          {"observed_projection_modes", p.observed_projection_modes},
                          {"representative_work", p.representative_work},
                          {"initial_affine", lane(p.initial_affine)}, {"adaptive", lane(p.adaptive)},
                          {"native_basis_fills", p.cache.native_preparation_calls},
                          {"native_basis_fill_rows", p.cache.native_preparation_samples},
                          {"direct_basis_columns", p.cache.direct_rows},
                          {"interpolated_basis_columns", p.cache.interpolated_rows},
                          {"basis_cache_fallback_reason", p.cache.fallback_reason},
                          {"basis_cache_evictions", p.cache.evictions},
                          {"workspace_bytes", p.workspace_bytes}, {"stage_workers", p.stage_workers},
                          {"bulk_statistics_seconds_cumulative", p.times.bulk_statistics_seconds},
                          {"observation_seconds_cumulative", p.times.observation_seconds},
                          {"dispatch_seconds_cumulative", p.times.dispatch_seconds},
                          {"worker_basis_statistics_seconds_cumulative", p.times.worker_basis_statistics_seconds},
                          {"worker_gaussian_seconds_cumulative", p.times.worker_gaussian_seconds}});
    if (p.bulk) result["bulk_statistics_blocks"] = p.bulk->statistics_blocks;
    return result;
}

void stopped(std::stop_token stop) {
    if (stop.stop_requested()) throw datapump::Error("VLF component diagnostic cancelled");
}

} // namespace

void diagnose_vlf_slot(datapump::triad::probe::Json& report, const ReceiverConfig& config,
                       const ReceiverSlotPlan& slot, const datapump::Crypto& key,
                       std::span<const Complex> iq, unsigned workers, std::size_t total_memory_limit_bytes,
                       std::stop_token stop) {
    report = object({{"scope", "complete public acquisition and abbreviated fine work; no slot decision"},
                     {"phase", "preflight"}, {"complete_receiver_service", false},
                     {"capacity_qualified", false}, {"component_complete", false},
                     {"public_first_sample", slot.first_sample}, {"public_stop_sample", slot.stop_sample},
                     {"workers", workers}, {"fine_checkpoints", Json::Array{}}});
    constexpr std::array checkpoint_seconds{2, 3, 4, 6};
    if (config.helper_enabled || config.time_varying_channel || slot.index != 0 ||
        slot.format.inner_rate_hz != .1 || config.sample_rate_hz != 1920 || !workers ||
        slot.stop_sample <= slot.first_sample || iq.size() != slot.stop_sample - slot.first_sample ||
        slot.quiet_first_sample < slot.first_sample || slot.quiet_stop_sample > slot.stop_sample ||
        slot.quiet_stop_sample <= slot.quiet_first_sample ||
        slot.quiet_stop_sample - slot.quiet_first_sample < checkpoint_seconds.back() * 1920)
        throw datapump::Error("VLF diagnostic requires the complete static Helper-off reference first observation");

    // The caller has already materialized the input. Charge its current
    // process footprint, at least its span size on platforms without RSS,
    // then reserve one full stack for every possible native helper lane.
    const auto footprint = std::max(datapump::execution::process_footprint_bytes(), iq.size_bytes());
    const auto stack = datapump::execution::worker_stack_bytes();
    if (footprint >= total_memory_limit_bytes ||
        (stack && workers - 1 > (total_memory_limit_bytes - footprint) / stack))
        throw ReceiverWorkspaceError("VLF diagnostic input and worker stacks exceed the process limit");
    const auto stack_reserve = (workers - 1) * stack;
    const auto scratch = total_memory_limit_bytes - footprint - stack_reserve;
    report["process_limit_bytes"] = total_memory_limit_bytes;
    report["input_and_process_footprint_bytes"] = footprint;
    report["worker_stack_reserve_bytes"] = stack_reserve;
    report["scratch_limit_bytes"] = scratch;
    ReceiverExecutor executor(workers);

    report["phase"] = "input_validation";
    const Timer validation_timer;
    for (std::size_t i = 0; i < iq.size(); ++i) {
        if (!(i % 4096)) stopped(stop);
        if (!std::isfinite(iq[i].real()) || !std::isfinite(iq[i].imag()))
            throw datapump::Error("VLF diagnostic input must be finite");
    }
    report["input_validation"] = validation_timer.elapsed();
    report["phase"] = "helper";
    const Timer helper_timer;
    const auto helper = score_helper(config, slot, iq, {}, helper_workspace(config, slot), stop);
    report["helper"] = helper_timer.elapsed();
    report["helper"]["status"] = helper.reason;
    FrequencyInterval fallback{};
    const auto prior = public_frequency_intervals(config, helper, fallback);
    const auto plan = coarse_work_plan(config, slot, prior, workers);
    report["coarse_quote"] = coarse_plan(plan);
    if (plan.workspace_reserved_bytes > scratch)
        throw ReceiverWorkspaceError("VLF diagnostic coarse search exceeds its admitted scratch limit");

    CoarseResult selected;
    const Timer coarse_timer;
    {
        report["phase"] = "coarse_prepare";
        CoarseSession coarse(config, slot, key, iq, prior, workers, scratch, stop, &executor);
        auto progress = coarse.progress();
        const auto tiles = progress.plan.total_tiles;
        report["coarse"] = coarse_progress(progress);
        try {
            report["phase"] = "coarse_search";
            while (progress.completed_tiles < tiles) {
                stopped(stop);
                coarse.advance(64);
                const auto previous = progress.completed_tiles;
                progress = coarse.progress();
                if (progress.completed_tiles <= previous)
                    throw datapump::Error("VLF diagnostic coarse search did not advance");
            }
            report["phase"] = "coarse_selection";
            auto finished = coarse.finish();
            if (!finished || finished->coordinates.empty())
                throw datapump::Error("VLF diagnostic acquisition produced no complete public selection");
            selected = std::move(*finished);
            report["coarse"] = coarse_progress(selected.work);
        } catch (...) {
            report["coarse"] = coarse_progress(coarse.progress());
            report["coarse_elapsed"] = coarse_timer.elapsed();
            throw;
        }
    }
    report["coarse_elapsed"] = coarse_timer.elapsed();
    const auto coordinates = prepare_fine_coordinates(config, slot, selected);
    report["fine_candidates"] = coordinates.coordinates.size();

    // Center the crop on the public DATA aperture and align it with the
    // full slot's ordinary segment boundaries. No evaluator bit or realized
    // path delay selects the crop or any receiver coordinates.
    const auto fs = static_cast<std::size_t>(config.sample_rate_hz);
    const auto geometry = fine_block_geometry(config.sample_rate_hz);
    const auto rows = fs * checkpoint_seconds.back();
    const auto center = std::midpoint(slot.quiet_first_sample, slot.quiet_stop_sample) - slot.first_sample;
    if (center < rows / 2 || center > iq.size() - rows / 2)
        throw datapump::Error("VLF public DATA aperture cannot fit the declared six-second fine crop");
    const auto offset = static_cast<std::size_t>(center - rows / 2) / geometry.segment_rows * geometry.segment_rows;
    if (offset < slot.quiet_first_sample - slot.first_sample ||
        offset + rows > slot.quiet_stop_sample - slot.first_sample)
        throw datapump::Error("VLF fine crop lies outside the public DATA aperture");
    FineObservation extent{slot.first_sample + offset, rows, FineRunKind::probe, true};
    report["fine_observation"] = object({{"first_sample", extent.first_sample}, {"rows", rows},
                                         {"block_rows", geometry.block_rows}, {"segment_rows", geometry.segment_rows},
                                         {"pulse_shape", slot.format.pulse_shape == PulseShape::Rrc ? "rrc" : "tapered_rrc"}});
    if (fine_workspace(config, slot, extent, workers) > scratch)
        throw ReceiverWorkspaceError("VLF diagnostic fine crop exceeds its admitted scratch limit");
    report["phase"] = "fine_prefix";
    const Timer prefix_timer;
    FineSession fine(config, slot, key, extent, iq.subspan(offset, rows), {}, coordinates.coordinates,
                     selected.projected_sample_rate_hz, selected.clock_gap_ppm, workers, scratch, stop, &executor);
    int previous = 0;
    std::uint64_t prefix_fills = 0;
    std::uint64_t affine_work = 0, adaptive_work = 0;
    bool complete_checkpoints = true;
    for (const auto seconds : checkpoint_seconds) {
        const Timer continuation_timer;
        const auto target = fs * seconds;
        fine.advance_to(target);
        auto elapsed = (previous ? continuation_timer : prefix_timer).elapsed();
        const auto p = fine.progress();
        report["fine_checkpoints"].array().push_back(checkpoint(p, std::move(elapsed), seconds, seconds - previous));
        if (!previous) prefix_fills = p.cache.native_preparation_calls;
        complete_checkpoints &= p.processed_rows == target && p.representative_work &&
            p.initial_affine.positive_basis_blocks > affine_work && p.adaptive.positive_basis_blocks > adaptive_work;
        affine_work = p.initial_affine.positive_basis_blocks;
        adaptive_work = p.adaptive.positive_basis_blocks;
        previous = seconds;
        report["phase"] = "fine_continuation";
    }
    report["phase"] = "fine_finish";
    const Timer finish_timer;
    ReceiverSlotScore score;
    fine.finish(score);
    report["fine_finish"] = finish_timer.elapsed();
    const auto final = fine.progress();
    report["fine_finish"]["completed_crop"] = final.observation_complete;
    report["fine_finish"]["positive_work_at_all_checkpoints"] = complete_checkpoints;
    report["fine_finish"]["continued_native_basis_fill"] = final.cache.native_preparation_calls > prefix_fills;
    report["fine_finish"]["receiver_decision_eligible"] = false;
    report["fine_finish"]["tap_count"] = final.tap_count;
    if (!final.observation_complete || score.complete)
        throw datapump::Error("VLF diagnostic fine component did not finish with the required non-admitting scope");
    stopped(stop);
    report["phase"] = "completed";
    report["component_complete"] = true;
}

} // namespace appraisal
