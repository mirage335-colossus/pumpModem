#include "triad_receiver_fine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

template<class Exception = Error, class Operation>
void rejects(Operation operation, const char* reason) {
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(reason);
}

struct Fixture {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    Crypto key = Crypto::amateur();
    std::vector<Complex> iq;
    std::array<FineCoordinate, 1> coordinates{{{0, 0, 0, 0}}};
    unsigned fs;

    explicit Fixture(unsigned sample_rate = 2400, double inner_rate = 120)
        : fs(sample_rate) {
        config.sample_rate_hz = fs;
        config.data_if_hz = 1725;
        config.noise_variance = .05;
        config.clock_bound_ppm = 40;
        slot.index = 17;
        slot.first_sample = 90'000'000'000ULL;
        slot.stop_sample = slot.first_sample + 19 * fs + 17;
        slot.quiet_first_sample = slot.first_sample + 2 * fs;
        slot.quiet_stop_sample = slot.stop_sample - 2 * fs;
        slot.format.sample_rate = fs;
        slot.format.inner_rate_hz = inner_rate;
        slot.format.added_symbols = 1;
        slot.format.bit_seconds = .001;
        slot.format.pulse_shape = PulseShape::TaperedRrc;
        slot.format.epoch = 1793850000;
        slot.format.symbol_ordinal = 3117;
        slot.window_capture_lead_seconds = -17;
        slot.source_start_seconds = 18;
        iq.resize(slot.stop_sample - slot.first_sample);
        PatternSource source(slot.format, key, 1);
        check(source.geometry().source_samples < iq.size() - fs, "public fine fixture does not contain DATA support");
        source.render(0, std::span(iq).subspan(fs, source.geometry().source_samples));
    }

    std::span<const Complex> samples(FineObservation observation) const {
        return std::span(iq).subspan(observation.first_sample - slot.first_sample, observation.rows);
    }
};

void production_equivalence() {
    const Fixture f;
    const auto bound = fine_workspace(f.config, f.slot, 1);
    ReceiverSlotScore direct;
    score_fine(f.config, f.slot, f.key, f.iq, f.coordinates, 100, 12, 1, direct, bound, {});
    check(direct.complete && direct.bit_evidence[1] > direct.bit_evidence[0],
          "whole-slot production wrapper must recover the reference score ordering");

    const FineObservation observation{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    const auto measured_bound = fine_workspace(f.config, f.slot, observation, 1);
    rejects<ReceiverWorkspaceError>([&] {
        FineSession too_small(f.config, f.slot, f.key, observation, f.iq, {}, f.coordinates,
                              100, 12, 1, measured_bound - 1);
    }, "one byte below the declared fine workspace must fail");
    FineSession split(f.config, f.slot, f.key, observation, f.iq, {}, f.coordinates,
                      100, 12, 1, measured_bound);
    split.advance_to(2 * f.fs - 7);
    const auto prefix = split.progress();
    check(prefix.processed_rows == 3 * f.fs / 2 && prefix.available_rows == 2 * f.fs - 7,
          "an irregular checkpoint may reveal input but cannot shorten a segment");
    rejects([&] { split.advance_to(prefix.available_rows - 1); }, "regressing a checkpoint must fail");
    split.advance_to(3 * f.fs + 13);
    check(split.progress().processed_rows == 3 * f.fs, "checkpoint must defer the incomplete segment");
    split.advance_to(f.iq.size() - 1);
    const auto near_end = split.progress();
    check(near_end.processed_rows == 19 * f.fs && !near_end.observation_complete,
          "the short terminal block must wait for its full declared extent");
    split.advance_to(f.iq.size());
    ReceiverSlotScore incremental;
    split.finish(incremental);
    const auto finished = split.progress();
    check(finished.observation_complete && finished.last_block_rows == 17 &&
          finished.finished && incremental.complete, "the true terminal block must be scored once");
    check(direct.bit_evidence == incremental.bit_evidence &&
          direct.adaptive_bit_evidence == incremental.adaptive_bit_evidence &&
          direct.combined_bit_evidence == incremental.combined_bit_evidence &&
          direct.quiet_modes == incremental.quiet_modes && direct.quiet_energy == incremental.quiet_energy &&
          direct.quiet_threshold == incremental.quiet_threshold && direct.basis_rows == incremental.basis_rows &&
          direct.adaptive_transition_edges == incremental.adaptive_transition_edges &&
          direct.affine_blocks == incremental.affine_blocks,
          "whole and checkpointed production evidence must match exactly");
    rejects([&] { split.finish(incremental); }, "finish cannot be repeated");
}

void uninterrupted_interior_probe() {
    const Fixture f;
    const FineObservation extent{f.slot.first_sample + 2 * f.fs + 23, 6 * f.fs, FineRunKind::probe, true};
    const auto bound = fine_workspace(f.config, f.slot, extent, 1);
    FineSession measured(f.config, f.slot, f.key, extent, f.samples(extent), {}, f.coordinates,
                         100, 12, 1, bound);
    measured.advance_to(2 * f.fs);
    auto previous = measured.progress();
    const auto prefix = previous;
    check(prefix.processed_rows == 2 * f.fs && prefix.initial_affine.positive_basis_blocks &&
          prefix.adaptive.positive_basis_blocks && prefix.cache.native_preparation_calls > 0 &&
          prefix.cache.fallback_reason.empty(), "cold prefix must execute both lanes and fill the actual cache");
    for (const auto seconds : {3, 4, 6}) {
        measured.advance_to(seconds * f.fs);
        const auto current = measured.progress();
        check(current.processed_rows == seconds * f.fs &&
              current.initial_affine.retained_states == prefix.initial_affine.retained_states &&
              current.cache.tile_samples == prefix.cache.tile_samples &&
              current.cache.dense_factor == prefix.cache.dense_factor &&
              current.initial_affine.projected_template_energy > previous.initial_affine.projected_template_energy &&
              current.adaptive.positive_basis_blocks > previous.adaptive.positive_basis_blocks,
              "continuation must carry the same lattice/cache and score new nonzero templates");
        previous = current;
    }
    ReceiverSlotScore ended;
    measured.finish(ended);
    const auto final = measured.progress();
    const auto minimum_scale = 1 - (f.config.clock_bound_ppm + 12. / 4) * 1e-6;
    check(4 * final.cache.dense_sample_rate_hz * minimum_scale > final.cache.tile_samples &&
          final.cache.cache_misses > prefix.cache.cache_misses &&
          final.cache.native_preparation_calls > prefix.cache.native_preparation_calls &&
          final.cache.native_preparation_samples > prefix.cache.native_preparation_samples,
          "continued physical travel must cross and actually fill a new cache tile");
    check(final.finished && final.observation_complete && final.representative_work && !ended.complete &&
          !ended.bit && ended.decision == SlotDecision::ambiguous &&
          ended.first_sample == extent.first_sample && ended.quiet_threshold > 0,
          "a fully scored diagnostic crop cannot complete a public slot");

    // A separately constructed equivalent crop must share the original source
    // clock. This known lead rebasing is independent of received values.
    auto rebased = f.slot;
    rebased.first_sample = extent.first_sample;
    rebased.stop_sample = extent.first_sample + extent.rows;
    rebased.quiet_first_sample = rebased.first_sample;
    rebased.quiet_stop_sample = rebased.stop_sample;
    rebased.window_capture_lead_seconds -= static_cast<long double>(extent.first_sample - f.slot.first_sample) / f.fs;
    FineSession single(f.config, rebased, f.key, extent, f.samples(extent), {}, f.coordinates,
                       100, 12, 1, fine_workspace(f.config, rebased, extent, 1));
    single.advance_to(extent.rows);
    ReceiverSlotScore one_step;
    single.finish(one_step);
    const auto one = single.progress();
    check(one_step.bit_evidence == ended.bit_evidence && one_step.adaptive_bit_evidence == ended.adaptive_bit_evidence &&
          one.initial_affine.retained_states == final.initial_affine.retained_states &&
          one.adaptive.retained_states == final.adaptive.retained_states &&
          one.adaptive.transition_edges == final.adaptive.transition_edges &&
          one.cache.native_preparation_calls == final.cache.native_preparation_calls &&
          one.cache.cache_hits == final.cache.cache_hits,
          "interior work and final evidence must preserve source origin and checkpoint invariance");
}

void erasure_and_failure() {
    const Fixture f;
    const FineObservation extent{f.slot.first_sample + 2 * f.fs, 2 * f.fs, FineRunKind::probe, true};
    const auto bound = fine_workspace(f.config, f.slot, extent, 1);
    std::vector<std::uint8_t> validity(extent.rows, 1);
    validity[f.fs / 4 - 1] = validity[f.fs / 4] = 0;
    FineSession erased(f.config, f.slot, f.key, extent, f.samples(extent), validity, f.coordinates,
                       100, 12, 1, bound);
    erased.advance_to(extent.rows);
    ReceiverSlotScore score;
    erased.finish(score);
    const auto work = erased.progress();
    check(work.observation_complete && work.representative_work && work.erased_blocks == 2 &&
          work.requested_invalid_rows == 2 && work.additional_discarded_rows == f.fs / 2 - 2 &&
          work.adaptive.transition_edges > 0 && work.initial_affine.gaussian_updates > 0 &&
          !score.all_valid && !score.noise_statistics_valid && !score.complete && score.quiet_threshold == 0,
          "erased DFT blocks must advance tracking while supplying no samples or quiet admission");

    std::vector<std::uint8_t> corrupt(extent.rows, 1);
    corrupt[f.fs] = 2;
    FineSession poison(f.config, f.slot, f.key, extent, f.samples(extent), corrupt, f.coordinates,
                       100, 12, 1, bound);
    poison.advance_to(f.fs / 2);
    check(poison.progress().segments == 1, "valid work must precede the malformed input");
    rejects([&] { poison.advance_to(extent.rows); }, "invalid validity byte must fail");
    rejects([&] { poison.advance_to(f.fs); }, "failed fine session must be poisoned");
    rejects([&] { (void)poison.progress(); }, "failed fine session cannot publish evidence");
    rejects([&] { poison.finish(score); }, "failed fine session cannot finish");

    std::stop_source stop;
    FineSession cancelled(f.config, f.slot, f.key, extent, f.samples(extent), {}, f.coordinates,
                          100, 12, 1, bound, stop.get_token());
    cancelled.advance_to(f.fs / 2);
    stop.request_stop();
    rejects([&] { cancelled.advance_to(extent.rows); }, "cancelled fine continuation must fail");
    rejects([&] { cancelled.finish(score); }, "cancelled fine work cannot finish");
}

void cold_maximum_states() {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample + 3 * f.fs, f.fs, FineRunKind::cold_maximum, true};
    FineSession cold(f.config, f.slot, f.key, extent, f.samples(extent), {}, f.coordinates,
                     100, 40, 1, fine_workspace(f.config, f.slot, extent, 1));
    cold.advance_to(extent.rows);
    ReceiverSlotScore score;
    cold.finish(score);
    const auto work = cold.progress();
    check(work.observation_complete && work.representative_work && work.segments == 2 &&
          work.initial_affine.initial_states == 256 && work.initial_affine.retained_states.size() == 256 &&
          work.adaptive.initial_states == 256 && work.adaptive.peak_emission_states == 256 &&
          work.adaptive.peak_retained_states == 64 && work.adaptive.transition_edges >= 64 &&
          work.adaptive.merged_channel_components > 0 && work.adaptive.retained_prior_mass < 1 &&
          work.initial_affine.gaussian_updates == work.blocks * 256 * 3 * receiver_coherence_count &&
          work.cache.native_preparation_calls > 0 && !score.complete && !score.bit,
          "cold ceiling must execute actual capped emission and posterior transitions without admission");
}

enum class IncompleteBulk { mapping_only, missing_counts, missing_statistics, nonzero_null };

class IncompleteFineBulk final : public FineBulk {
public:
    explicit IncompleteFineBulk(IncompleteBulk fault) : fault_(fault) {}

    void statistics(const FineBulkBlock& block, std::size_t candidates, const FineMappingSource& source,
                    std::span<FineBulkStatistics> output, std::stop_token) override {
        for (std::size_t i = 0; i < candidates; ++i) {
            source(i, 0, block.rows, [](auto, auto, auto) {});
            output[i].nonzero_basis = true;
            if (fault_ == IncompleteBulk::nonzero_null) {
                output[i].correlations.fill({});
                output[i].gram.fill({});
                output[i].correlations[0] = 1.;
            }
        }
        if (fault_ == IncompleteBulk::mapping_only) return;
        work_.executed = true;
        if (fault_ == IncompleteBulk::missing_counts) return;
        work_.statistics_blocks += candidates;
        work_.projected_values += 2 * block.taps * candidates * block.projection_indices.size();
    }

    BulkReceipt receipt() const override { return work_; }

private:
    IncompleteBulk fault_;
    BulkReceipt work_;
};

class IncompleteBulkBackend final : public ReceiverBulkBackend {
public:
    explicit IncompleteBulkBackend(IncompleteBulk fault) : fault_(fault) {}
    std::unique_ptr<FineBulk> make_fine(double, double) override {
        return std::make_unique<IncompleteFineBulk>(fault_);
    }
    std::unique_ptr<CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    BulkReceipt receipt() const override { return {}; }

private:
    IncompleteBulk fault_;
};

void incomplete_bulk_cannot_publish_quiet() {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    const std::vector<Complex> noise_free(f.iq.size());
    for (const auto fault : {IncompleteBulk::mapping_only, IncompleteBulk::missing_counts,
                            IncompleteBulk::missing_statistics, IncompleteBulk::nonzero_null}) {
        auto config = f.config;
        config.bulk_backend = std::make_shared<IncompleteBulkBackend>(fault);
        FineSession session(config, f.slot, f.key, extent, noise_free, {}, f.coordinates,
                            100, 12, 1, fine_workspace(config, f.slot, extent, 1));
        const auto reason = [&]() -> std::string {
            try {
                session.advance_to(f.fs / 2);
            } catch (const Error& error) {
                return error.what();
            }
            return {};
        }();
        const auto expected = fault == IncompleteBulk::nonzero_null ? "exact zero-signal null" :
            fault == IncompleteBulk::missing_statistics ? "finite complete sufficient statistics" :
            "complete public statistics work";
        check(reason.find(expected) != std::string::npos,
              "bulk mapping, receipt flags and output must cover separate required work");
        ReceiverSlotScore score;
        rejects([&] { session.finish(score); }, "failed bulk work cannot complete a slot");
        rejects([&] { (void)session.progress(); }, "failed bulk work cannot publish partial credit");
        check(!score.complete && score.decision == SlotDecision::ambiguous,
              "failed bulk work cannot provide scored quiet");
    }
}
} // namespace

int main() {
    try {
        production_equivalence();
        uninterrupted_interior_probe();
        erasure_and_failure();
        cold_maximum_states();
        incomplete_bulk_cannot_publish_quiet();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
