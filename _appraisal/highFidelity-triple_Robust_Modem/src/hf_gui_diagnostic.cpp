#include "appraisal/hf_gui_diagnostic.hpp"

#include "datapump/execution_resources.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_helper.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace appraisal {
namespace {

using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using datapump::triad::probe::Json;
using Clock = std::chrono::steady_clock;

constexpr std::size_t maximum_coarse_tiles = 4096;
constexpr std::size_t maximum_coarse_pairs = 1 << 20;
constexpr std::size_t maximum_varying_rows = 32 << 20;
constexpr std::size_t maximum_scratch_bytes = 512 << 20;
constexpr std::size_t metadata_bytes = 256 << 10;
constexpr std::size_t span_summary_metadata_bytes = 2 << 20;

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

Json coordinate_json(const FineCoordinate& coordinate) {
    return Json::Object{
        {"epoch_offset_seconds", coordinate.epoch_offset_seconds},
        {"arrival_seconds", coordinate.arrival_seconds},
        {"frequency_hz", coordinate.frequency_hz},
        {"clock_ppm", coordinate.clock_ppm}
    };
}

Json coordinate_list(std::span<const FineCoordinate> coordinates) {
    Json::Array values;
    for (const auto& coordinate : coordinates) values.push_back(coordinate_json(coordinate));
    return values;
}

Json bit_values(const std::array<double, 2>& values) {
    require(std::isfinite(values[0]) && std::isfinite(values[1]),
        "HF GUI diagnostic branch evidence is nonfinite");
    return Json::Array{values[0], values[1]};
}

Json branch_json(const std::array<double, 2>& raw, const std::array<double, 2>& corrected,
                 const AdmissionGrid& grid, std::size_t candidates, std::uint64_t basis_rows,
                 std::size_t workspace, double seconds) {
    return Json::Object{
        {"raw_bit_log_evidence", bit_values(raw)},
        {"prior_corrected_bit_log_evidence", bit_values(corrected)},
        {"public_grid_log_count", grid.log_count()},
        {"public_frequency_cells", 1 + 2 * grid.frequency_radius},
        {"selected_log_prior_mass", std::log(static_cast<double>(candidates)) - grid.log_count()},
        {"candidates", candidates}, {"basis_rows", basis_rows}, {"workspace_bytes", workspace},
        {"seconds", seconds}
    };
}

Json tracked_frequency_span_json(const TrackedFrequencySpan& span, std::size_t count, std::size_t rows,
                                 std::size_t block_rows, std::size_t states, std::size_t scratch_bytes) {
    require(count && count <= span.coordinates.size() && !span.observation_energy.empty() && states > 0,
        "HF GUI tracked span requires complete timing and frequency geometry");
    const auto coordinates = std::span(span.coordinates).first(count);
    const auto energy = std::accumulate(span.observation_energy.begin(), span.observation_energy.end(), 0.);
    Json::Array bits;
    std::size_t verified_maxima = 0;
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const TrackedFrequencySpanCoordinate* best = nullptr;
        double best_capture = 0, rank_upper = 0;
        for (const auto& coordinate : coordinates) {
            double capture = 0, upper = 0;
            for (std::size_t i = 0; i < coordinate.blocks.size(); ++i) {
                const auto& block = coordinate.blocks[i][bit];
                verified_maxima += block.verified;
                capture += block.captured_energy;
                upper += block.rank_ambiguous ? span.observation_energy[i] : block.captured_energy;
            }
            // One timing coordinate for the whole slot. Only frequency can
            // change per block; every such path has strictly positive prior.
            if (!best || capture > best_capture) {
                best = &coordinate;
                best_capture = capture;
            }
            rank_upper = std::max(rank_upper, upper);
        }
        Json::Array blocks;
        for (std::size_t i = 0; i < best->blocks.size(); ++i) {
            const auto& block = best->blocks[i][bit];
            const auto first = i * block_rows;
            blocks.emplace_back(Json::Object{
                {"first_row", first},
                {"rows", i + 1 == best->blocks.size() ? rows - first : block_rows},
                {"frequency_hz", block.frequency_hz}, {"captured_energy", block.captured_energy},
                {"observation_energy", span.observation_energy[i]}, {"rank_ambiguous", block.rank_ambiguous}
            });
        }
        bits.emplace_back(Json::Object{
            {"bit", bit}, {"coordinate", coordinate_json(best->coordinate)},
            {"captured_energy", best_capture}, {"rank_upper_energy", rank_upper},
            {"residual_energy", energy - best_capture}, {"blocks", std::move(blocks)}
        });
    }
    return Json::Object{
        {"used_for_receiver_decisions", false}, {"key_role", "receiver"},
        {"scope", "raw short-track spans on clean IQ and the frozen noisy-selected timing bank"},
        {"energy_units", "sum_of_squared_complex_samples"},
        {"observation_energy", energy}, {"timing_coordinates", coordinates.size()},
        {"state_projections", verified_maxima * states}, {"verified_maxima", verified_maxima},
        {"state_projection_bound", 2 * span.observation_energy.size() * coordinates.size() * states},
        {"frequency_states", states}, {"scratch_bound_bytes", scratch_bytes},
        {"bits", std::move(bits)}
    };
}

} // namespace

Json diagnose_hf_gui_varying(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
    const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> validity,
    unsigned workers, std::size_t total_memory_limit_bytes, std::stop_token stop, const HfGuiCleanInput* clean) {
    check_cancelled(stop);
    require(workers && slot.stop_sample > slot.first_sample &&
        iq.size() == slot.stop_sample - slot.first_sample && validity.size() == iq.size(),
        "HF GUI diagnostic requires one complete public observation");
    require(!clean || clean->iq.size() == iq.size(), "HF GUI clean capture differs from the public observation extent");
    if (iq.size() > hf_gui_varying_maximum_aperture_rows)
        throw ReceiverWorkspaceError("HF GUI diagnostic aperture exceeds its declared row bound");
    if (config.helper_enabled || config.bulk_backend || !config.time_varying_channel)
        throw HfGuiDiagnosticUnavailable("HF GUI diagnostic requires a varying-channel Helper-off CPU profile");
    if (!std::ranges::all_of(validity, [](auto valid) { return valid == 1; }))
        throw HfGuiDiagnosticUnavailable("HF GUI diagnostic requires an all-valid observation");

    const auto varying = varying_fine_plan(config, slot, workers);
    const auto drift = drift_plan(config, slot, workers);
    const auto coherent_gain = varying_fine_plan(config, slot, workers, false, true);
    if (!varying.block_rows)
        throw HfGuiDiagnosticUnavailable("HF GUI short varying branch unavailable: " + std::string(varying.status));
    if (coherent_gain.block_rows)
        throw HfGuiDiagnosticUnavailable("HF GUI diagnostic does not compare profiles with an available coherent-gain prior");

    auto search = config;
    search.time_varying_channel = true;
    FrequencyInterval fallback;
    const auto intervals = public_frequency_intervals(config, HelperResult{}, fallback);
    require(intervals.size() == 1 && intervals[0].lower_hz == -config.frequency_bound_hz &&
        intervals[0].upper_hz == config.frequency_bound_hz,
        "HF GUI diagnostic requires the entire public frequency prior");
    const auto coarse_quote = coarse_work_plan(search, slot, intervals, workers);
    if (coarse_quote.total_tiles > maximum_coarse_tiles ||
        checked_multiply(coarse_quote.workspaces, coarse_quote.frequency_points) > maximum_coarse_pairs)
        throw ReceiverWorkspaceError("HF GUI coarse plan exceeds its declared tile or pair bound");

    const auto coarse_bytes = coarse_workspace(search, slot, workers);
    const auto affine_bytes = fine_workspace(config, slot, workers);
    const auto ordinary_span_bytes = clean && clean->tracked_frequency_span ?
        tracked_frequency_span_workspace(slot, varying) : 0;
    const auto expanded_span_bytes = ordinary_span_bytes && clean->quarter_clock_shadow ?
        tracked_frequency_span_workspace(slot, varying, true) : ordinary_span_bytes;
    const auto scratch = std::max({coarse_bytes, affine_bytes, checked_add(varying.workspace_bytes, expanded_span_bytes)});
    const auto footprint = std::max(execution::process_footprint_bytes(),
        checked_add(checked_add(iq.size_bytes(), validity.size_bytes()), clean ? clean->iq.size_bytes() : 0));
    const auto stacks = checked_multiply(workers - 1, execution::worker_stack_bytes());
    // Each optional summary holds two bits of per-block maps. Initializer
    // list copies of those trees can briefly coexist with the retained JSON.
    const std::size_t span_summaries = (ordinary_span_bytes != 0) + (expanded_span_bytes > ordinary_span_bytes);
    const auto receipt_bytes = checked_add(metadata_bytes,
        checked_multiply(span_summaries, span_summary_metadata_bytes));
    const auto base = checked_add(checked_add(footprint, stacks), receipt_bytes);
    if (scratch > maximum_scratch_bytes || base >= total_memory_limit_bytes ||
        scratch > total_memory_limit_bytes - base)
        throw ReceiverWorkspaceError("HF GUI diagnostic exceeds its scratch or total memory bound");
    check_cancelled(stop);
    ReceiverExecutor executor(workers);
    const auto acquire = [&](const Crypto& candidate_key) {
        const auto start = Clock::now();
        auto coarse = score_coarse(search, slot, candidate_key, iq, intervals, workers, coarse_bytes, stop, &executor);
        const auto seconds = elapsed(start);
        check_cancelled(stop);
        require(coarse.work.state == CoarseState::finished &&
            coarse.work.completed_tiles == coarse.work.plan.total_tiles &&
            coarse.workspace_peak_bytes <= coarse_bytes,
            "HF GUI diagnostic coarse work did not finish within its bound");
        if (coarse.coordinates.empty())
            throw HfGuiDiagnosticUnavailable("HF GUI diagnostic has no canonical coarse coordinates");
        auto fine = prepare_fine_coordinates(config, slot, coarse);
        return std::tuple{std::move(coarse), std::move(fine), seconds};
    };
    const auto selected_coordinates = [](const FinePreparation& fine) {
        return fine.varying_coordinates.empty() ? std::span(fine.coordinates) : std::span(fine.varying_coordinates);
    };
    const auto render_quote = [&](const FinePreparation& fine) {
        const auto selected = selected_coordinates(fine);
        // Up to three public geometries, two taps and two bit bases per
        // fixed coordinate. Tracking adds at most one coordinate per selected
        // or segmented timing proposal, with at most two taps per bit.
        const auto fixed_columns = checked_multiply(12, selected.size());
        const auto tracking_columns = varying.track_frequency ?
            checked_multiply(4, checked_add(selected.size(), fine.segmented_timing_coordinates.size())) : 0;
        return checked_multiply(iq.size(), checked_add(fixed_columns, tracking_columns));
    };
    const auto drift_prior = drift.workspace_bytes ? &drift : nullptr;
    const double separation = config.time_varying_minimum_bit_log_separation;
    const bool line_reserved = varying_line_workspace(config, slot) != 0;
    const double ordinary_weight = line_reserved ? .5 : 1.;
    const auto score_pair = [&](const Crypto& candidate_key, std::span<const Complex> observation,
                               const CoarseResult& coarse, const FinePreparation& fine,
                               std::size_t render_rows, bool diagnose_span = false,
                               std::span<const FineCoordinate> nominations = {}) {
        const auto selected = selected_coordinates(fine);
        ReceiverSlotScore score;
        score.varying_status = varying.status;
        const auto affine_start = Clock::now();
        score_fine(config, slot, candidate_key, observation, fine.coordinates, coarse.projected_sample_rate_hz,
            coarse.clock_gap_ppm, workers, score, affine_bytes, stop, &executor);
        const auto affine_seconds = elapsed(affine_start);
        require(score.complete && score.all_valid && score.zero_h0_verified && score.fine_candidates,
            "HF GUI diagnostic requires complete canonical affine evidence");
        const AdmissionGrid varying_grid(fine.grid.lower, fine.grid.upper,
            config.frequency_bound_hz + config.time_varying_atmospheric_hz,
            config.clock_bound_ppm, fine.grid.frequency_step);
        const double log_ordinary_weight = std::log(ordinary_weight);
        const auto classify_ordinary = [&](ReceiverSlotScore evidence) {
            // The canonical outer line mixture gives half weight to ordinary
            // evidence when the line branch is available, even if it is omitted.
            for (auto& value : evidence.bit_evidence) value += log_ordinary_weight;
            for (auto& value : evidence.varying_bit_evidence) value += log_ordinary_weight;
            classify_varying_slot(evidence, fine.grid, &varying_grid, drift_prior, separation);
            return evidence;
        };
        const auto affine_only = classify_ordinary(score);
        auto affine = branch_json(score.bit_evidence, affine_only.operational_bit_evidence, fine.grid,
            score.fine_candidates, score.basis_rows, affine_bytes, affine_seconds);
        affine["log_presence"] = affine_only.operational_log_e;

        TrackedFrequencySpan span;
        TrackedFrequencyClockShadow shadow{nominations};
        score_varying_fine(config, slot, candidate_key, varying, observation, selected, fine.segmented_timing_coordinates,
            coarse.projected_sample_rate_hz, coarse.clock_gap_ppm, workers, score, stop, executor,
            nullptr, diagnose_span ? &span : nullptr, nominations.empty() ? nullptr : &shadow);
        check_cancelled(stop);
        if (!score.varying_candidates)
            throw HfGuiDiagnosticUnavailable("HF GUI diagnostic has no short varying evidence: " + score.varying_status);
        require(score.varying_basis_rows <= render_rows, "HF GUI diagnostic varying work exceeded its quote");
        const auto combined = classify_ordinary(score);
        // Evaluate this branch alone using the same canonical mixture. The
        // affine and unscored drift weights remain reserved.
        auto varying_only = score;
        varying_only.bit_evidence.fill(-std::numeric_limits<double>::infinity());
        varying_only = classify_ordinary(std::move(varying_only));
        auto short_branch = branch_json(score.varying_bit_evidence, varying_only.operational_bit_evidence, varying_grid,
            score.varying_candidates, score.varying_basis_rows, varying.workspace_bytes, score.varying_fine_seconds);
        short_branch["blocks"] = score.varying_blocks;
        short_branch["block_rows"] = score.varying_block_rows;
        short_branch["frequency_states"] = score.varying_frequency_states;
        short_branch["fixed_gain_bit_log_evidence"] = bit_values(score.fixed_varying_bit_evidence);
        short_branch["tracked_frequency_bit_log_evidence"] = bit_values(score.tracked_frequency_bit_evidence);
        short_branch["tracked_frequency_log_prior_adjustment"] = score.tracked_frequency_log_prior_adjustment;
        short_branch["status"] = score.varying_status;
        short_branch["log_presence"] = varying_only.operational_log_e;
        short_branch["diagnostic_admits_data"] = varying_only.decision == SlotDecision::data;
        short_branch["diagnostic_data_bit"] = varying_only.bit ? Json(*varying_only.bit) : Json{};

        Json pair = Json::Object{
            {"affine", std::move(affine)}, {"varying", std::move(short_branch)},
            {"combined", Json::Object{
                {"prior_corrected_bit_log_evidence", bit_values(combined.operational_bit_evidence)},
                {"log_presence", combined.operational_log_e}
            }}
        };
        if (diagnose_span)
            pair["tracked_frequency_span"] = tracked_frequency_span_json(
                span, nominations.empty() ? span.coordinates.size() : shadow.ordinary_coordinates,
                iq.size(), varying.block_rows, score.varying_frequency_states, ordinary_span_bytes);
        if (!nominations.empty()) {
            auto expanded = score;
            expanded.varying_bit_evidence = shadow.varying_bit_evidence;
            expanded = classify_ordinary(std::move(expanded));
            auto detail = Json::Object{
                {"added_timing_coordinates", shadow.added_coordinates},
                {"ordinary_timing_coordinates", shadow.ordinary_coordinates},
                {"added_basis_rows", shadow.added_basis_rows},
                {"raw_varying_bit_log_evidence", bit_values(shadow.varying_bit_evidence)},
                {"tracked_frequency_bit_log_evidence", bit_values(shadow.tracked_bit_evidence)},
                {"tracked_frequency_log_prior_adjustment", shadow.tracked_log_prior_adjustment},
                {"prior_corrected_bit_log_evidence", bit_values(expanded.operational_bit_evidence)},
                {"log_presence", expanded.operational_log_e}
            };
            if (diagnose_span)
                detail["tracked_frequency_span"] = tracked_frequency_span_json(
                    span, span.coordinates.size(), iq.size(), varying.block_rows,
                    score.varying_frequency_states, expanded_span_bytes);
            pair["quarter_clock_shadow"] = std::move(detail);
        }
        return std::pair{std::move(pair), score.varying_basis_rows};
    };
    const auto coarse_json = [&](const CoarseResult& coarse, const FinePreparation& fine, double seconds) {
        Json::Array proposals;
        for (const auto& item : std::span(coarse.proposals).first(coarse.proposal_count)) {
            proposals.emplace_back(Json::Object{
                {"coordinate", coordinate_json(item.coordinate)}, {"score", item.score},
                {"bit", item.bit}, {"rank", item.rank}
            });
        }
        const auto& work = coarse.work;
        return Json::Object{
            {"frequency_points", coarse.frequency_points}, {"arrival_points", coarse.arrival_points},
            {"workspaces", coarse.workspaces}, {"total_tiles", work.plan.total_tiles},
            {"completed_tiles", work.completed_tiles}, {"workspace_bytes", coarse.workspace_peak_bytes},
            {"fft_precision", work.plan.fft_precision}, {"seconds", seconds},
            {"proposals", std::move(proposals)}, {"affine_coordinates", coordinate_list(fine.coordinates)},
            {"varying_coordinates", coordinate_list(selected_coordinates(fine))},
            {"segmented_timing_coordinates", coordinate_list(fine.segmented_timing_coordinates)},
            {"epoch_timing_coordinates", coordinate_list(fine.epoch_timing_coordinates)}
        };
    };

    const auto [coarse, fine, coarse_seconds] = acquire(key);
    const auto nominations = clean && clean->quarter_clock_shadow && varying.track_frequency ?
        prepare_quarter_clock_timing(config, slot, coarse, fine.grid) : std::vector<FineCoordinate>{};
    const auto render_rows = render_quote(fine);
    const auto shadow_rows = checked_multiply(checked_multiply(4, nominations.size()), iq.size());
    const auto shadow_total = checked_multiply(2, shadow_rows);
    auto total_render = checked_multiply(clean ? 2 : 1, render_rows);
    if (checked_add(total_render, shadow_total) > maximum_varying_rows)
        throw ReceiverWorkspaceError("HF GUI short varying branch exceeds its declared render bound");
    auto [primary, rendered] = score_pair(key, iq, coarse, fine, render_rows, false, nominations);
    Json report = Json::Object{
        {"component_complete", true}, {"all_valid", true}, {"used_for_receiver_decisions", false},
        {"public_slot", slot.index}, {"observation_first_sample", slot.first_sample},
        {"observation_stop_sample", slot.stop_sample}, {"observation_samples", iq.size()},
        {"affine", std::move(primary["affine"])}, {"varying", std::move(primary["varying"])},
        {"combined", std::move(primary["combined"])},
        {"prior", Json::Object{
            {"branch_weight", 1. / (2 + (drift_prior != nullptr))}, {"drift_reserved", drift_prior != nullptr},
            {"drift_scored", false}, {"coherent_gain_status", coherent_gain.status},
            {"coherent_gain_available", false}, {"line_scored", false},
            {"line_reserved", line_reserved}, {"ordinary_weight", ordinary_weight},
            {"noise_variance", config.noise_variance}, {"clock_bound_ppm", config.clock_bound_ppm},
            {"frequency_lower_hz", fallback.lower_hz}, {"frequency_upper_hz", fallback.upper_hz},
            {"atmospheric_allowance_hz", config.time_varying_atmospheric_hz},
            {"arrival_lower_seconds", fine.grid.lower}, {"arrival_upper_seconds", fine.grid.upper},
            {"operational_threshold", receiver_operational_threshold()},
            {"minimum_bit_log_separation", separation}
        }},
        {"coarse", coarse_json(coarse, fine, coarse_seconds)},
        {"resources", Json::Object{
            {"workers", workers}, {"scratch_bound_bytes", scratch}, {"worker_stack_bound_bytes", stacks},
            {"metadata_bound_bytes", receipt_bytes}, {"render_row_bound", render_rows}
        }}
    };
    if (clean) {
        Json::Array comparisons, render_bounds{render_rows};
        auto [rx_clean, rx_rows] = score_pair(
            key, clean->iq, coarse, fine, render_rows, clean->tracked_frequency_span, nominations);
        rendered += rx_rows;
        if (clean->quarter_clock_shadow) {
            Json shadow = Json::Object{
                {"used_for_receiver_decisions", false},
                {"status", nominations.empty() ? "no_public_quarter_clock_nominations" : "scored"},
                {"nominations", coordinate_list(nominations)}, {"added_render_row_bound", shadow_total},
                {"ordinary_segmented_timing_blocks", coarse.work.plan.segmented_timing_blocks},
                {"source_midpoint_seconds", static_cast<double>(slot.source_start_seconds +
                    .5L * coarse.work.plan.source_rows / coarse.work.plan.input_sample_rate_hz)}
            };
            if (!nominations.empty()) {
                auto& noisy_clock = primary["quarter_clock_shadow"];
                auto clean_clock = std::get<Json::Object>(rx_clean.value).extract("quarter_clock_shadow");
                const auto noisy_rows = std::get<std::uint64_t>(noisy_clock["added_basis_rows"].value);
                const auto clean_rows = std::get<std::uint64_t>(clean_clock.mapped()["added_basis_rows"].value);
                require(noisy_rows == clean_rows && checked_add(noisy_rows, clean_rows) <= shadow_total,
                    "HF GUI clock shadow clean/noisy work differs on one aperture");
                shadow["noisy"] = std::move(noisy_clock);
                shadow["clean"] = std::move(clean_clock.mapped());
            }
            report["quarter_clock_shadow"] = std::move(shadow);
        }
        comparisons.emplace_back(Json::Object{
            {"role", "receiver"}, {"coarse", Json{}}, {"clean", std::move(rx_clean)}, {"noisy", Json{}}
        });
        if (clean->transmitter_key) {
            const auto& tx_key = *clean->transmitter_key;
            const auto [tx_coarse, tx_fine, tx_seconds] = acquire(tx_key);
            const auto tx_bound = render_quote(tx_fine);
            total_render = checked_add(total_render, checked_multiply(2, tx_bound));
            if (checked_add(total_render, shadow_total) > maximum_varying_rows)
                throw ReceiverWorkspaceError("HF GUI clean comparison exceeds its aggregate short-varying render bound");
            auto [tx_noisy, tx_noisy_rows] = score_pair(tx_key, iq, tx_coarse, tx_fine, tx_bound);
            auto [tx_clean, tx_clean_rows] = score_pair(tx_key, clean->iq, tx_coarse, tx_fine, tx_bound);
            rendered += tx_noisy_rows + tx_clean_rows;
            render_bounds.emplace_back(tx_bound);
            comparisons.emplace_back(Json::Object{
                {"role", "transmitter"}, {"coarse", coarse_json(tx_coarse, tx_fine, tx_seconds)},
                {"clean", std::move(tx_clean)}, {"noisy", std::move(tx_noisy)}
            });
        }
        require(rendered <= total_render, "HF GUI clean comparison exceeded its render quote");
        report["clean_comparison"] = Json::Object{
            {"schema_version", 1}, {"used_for_receiver_decisions", false},
            {"transmitter_key_evaluator_only", true}, {"public_acquisition_input", "noisy_observation"},
            {"within_key_same_coordinates_for_clean_noisy", true}, {"coarse_searches", comparisons.size()},
            {"resources", Json::Object{
                {"coarse_workspace_bound_bytes", coarse_bytes}, {"fine_pair_count", 2 * comparisons.size()},
                {"key_render_row_bounds", std::move(render_bounds)}, {"varying_render_row_bound", total_render},
                {"varying_render_rows", rendered}
            }}, {"keys", std::move(comparisons)}
        };
    }
    return report;
}

} // namespace appraisal
