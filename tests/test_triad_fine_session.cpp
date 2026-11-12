#include "triad_receiver_fine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>

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

template<class Operation>
void rejects_with(Operation operation, const char* expected, const char* reason) {
    try {
        operation();
    } catch (const Error& error) {
        check(std::string(error.what()).find(expected) != std::string::npos, reason);
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

void helper_terminal_geometry() {
    // Public row-4 geometry: a 13-sample remainder used to create a DFT
    // whose 461.5-Hz bins could not separate otherwise disjoint DATA/Helper
    // bands. Construction alone reproduces the failure; no capture is needed.
    ReceiverConfig config;
    config.sample_rate_hz = 6000;
    config.data_if_hz = 1725;
    config.frequency_bound_hz = 90.522026101305059;
    config.clock_bound_ppm = 300.01500075003753;
    config.spread_doppler_hz = 90;
    config.spread_delay_seconds = .021;
    config.noise_variance = .05;
    config.helper_enabled = true;
    config.helper.low_frequency_hz = 550;
    config.helper.auxiliary_frequency_hz = 950;
    config.helper.frequency_hz = 2400;
    ReceiverSlotPlan slot;
    slot.first_sample = 89966;
    slot.stop_sample = 518979;
    slot.quiet_first_sample = 361431;
    slot.quiet_stop_sample = 517384;
    slot.format.sample_rate = 6000;
    slot.format.inner_rate_hz = 36;
    slot.format.added_symbols = 10;
    slot.format.bit_seconds = 11.6;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    const std::vector<Complex> zero(slot.stop_sample - slot.first_sample);
    const std::array<FineCoordinate, 1> coordinates{{{0, 0, 0, 0}}};
    const FineObservation extent{slot.first_sample, zero.size(), FineRunKind::operational, true};
    const auto workspace = fine_workspace(config, slot, extent, 1);
    FineSession session(config, slot, Crypto::amateur(), extent, zero, {}, coordinates,
                        100, 12, 1, workspace);
    check(session.progress().processed_rows == 0,
          "public Helper tail geometry must construct without processing input");

    auto short_slot = slot;
    short_slot.stop_sample = short_slot.first_sample + 13;
    short_slot.quiet_first_sample = short_slot.first_sample;
    short_slot.quiet_stop_sample = short_slot.stop_sample;
    const FineObservation short_extent{short_slot.first_sample, 13, FineRunKind::operational, true};
    rejects_with([&] { (void)fine_workspace(config, short_slot, short_extent, 1); },
                 "cannot resolve DATA and Helper bands",
                 "workspace admission must reject an unresolvable real DFT before constructing a receiver");
    rejects_with([&] {
        FineSession unresolved(config, short_slot, Crypto::amateur(), short_extent,
                               std::span(zero).first(13), {}, coordinates, 100, 12, 1, workspace);
    }, "cannot resolve DATA and Helper bands",
       "a whole observation too short to resolve Helper exclusions must still fail");
}

struct ProjectionWitness {
    std::span<const Complex> input;
    std::size_t cursor = 0, quiet_stop = 0, quiet_modes = 0;
    double variance = 0, quiet_energy = 0;
    std::vector<std::size_t> lengths;
    std::function<void(std::stop_token)> before_statistics;
};

// This fixture observes the production observation projection, independently
// checking every output with a direct unitary DFT. It does not supply a modem
// oracle or claim valid receiver admission from its zero template statistics.
class ProjectionWitnessBulk final : public FineBulk {
public:
    explicit ProjectionWitnessBulk(std::shared_ptr<ProjectionWitness> witness) : witness_(std::move(witness)) {}

    void statistics(const FineBulkBlock& block, std::size_t candidates, const FineMappingSource& source,
                    std::span<FineBulkStatistics> output, std::stop_token stop) override {
        auto& witness = *witness_;
        if (witness.before_statistics) witness.before_statistics(stop);
        check(block.rows <= witness.input.size() - witness.cursor, "projection must stay inside the real input");
        check(block.projected_y.size() == block.projection_indices.size(), "projected mode dimensions must agree");
        long double expected_energy = 0;
        for (std::size_t mode = 0; mode < block.projection_indices.size(); ++mode) {
            const auto index = block.projection_indices[mode];
            std::complex<long double> expected{};
            for (std::size_t sample = 0; sample < block.rows; ++sample) {
                const auto angle = -2 * std::numbers::pi_v<long double> * index * sample / block.rows;
                const auto value = witness.input[witness.cursor + sample];
                expected += std::complex<long double>(value.real(), value.imag()) *
                            std::complex<long double>(std::cos(angle), std::sin(angle));
            }
            expected /= std::sqrt(static_cast<long double>(block.rows));
            check(std::abs(std::complex<long double>(block.projected_y[mode].real(), block.projected_y[mode].imag()) -
                           expected) <= 1e-9L * std::max(1.L, std::abs(expected)),
                  "every real terminal sample must participate in the actual-length unitary DFT");
            expected_energy += std::norm(expected);
        }
        if (witness.cursor + block.rows <= witness.quiet_stop) {
            witness.quiet_modes += block.projection_indices.size();
            witness.quiet_energy += static_cast<double>(expected_energy) / witness.variance;
        }
        witness.cursor += block.rows;
        witness.lengths.push_back(block.rows);
        for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
            source(candidate, 0, block.rows, [](auto, auto, auto) {});
            output[candidate] = {};
        }
        work_.executed = true;
        work_.statistics_blocks += candidates;
        work_.projected_values += 2 * block.taps * candidates * block.projection_indices.size();
    }

    BulkReceipt receipt() const override { return work_; }

private:
    std::shared_ptr<ProjectionWitness> witness_;
    BulkReceipt work_;
};

class ProjectionWitnessBackend final : public ReceiverBulkBackend {
public:
    explicit ProjectionWitnessBackend(std::shared_ptr<ProjectionWitness> witness) : witness_(std::move(witness)) {}
    std::unique_ptr<FineBulk> make_fine(double, double) override {
        return std::make_unique<ProjectionWitnessBulk>(witness_);
    }
    std::unique_ptr<CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    BulkReceipt receipt() const override { return {}; }

private:
    std::shared_ptr<ProjectionWitness> witness_;
};

enum class OverlapAction { finish, owner_failure, observation_failure, cancel };

// No sleeps or performance thresholds: the fake backend holds its first call
// until the second observation reaches its completion witness. Failure paths
// hold one participant until the effective stage token cancels it. Timeouts
// only bound a broken scheduler's test duration.
struct FineOverlapGate {
    std::mutex mutex;
    std::condition_variable_any changed;
    const std::thread::id owner = std::this_thread::get_id();
    const bool parallel;
    const OverlapAction action;
    std::stop_source external;
    std::vector<std::size_t> observed_firsts;
    std::size_t calls = 0;
    bool bulk_entered = false, ahead_ready = false, ahead_drained = false;
    bool observed_cancel = false, bulk_cancel = false;

    explicit FineOverlapGate(bool parallel, OverlapAction action = OverlapAction::finish)
        : parallel(parallel), action(action) {}

    template<class Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate predicate, const char* reason) {
        check(changed.wait_for(lock, std::chrono::seconds(5), predicate), reason);
    }

    void observed(std::size_t first, std::stop_token stop) {
        std::unique_lock lock(mutex);
        observed_firsts.push_back(first);
        if (observed_firsts.size() != 2) return;
        check(stop.stop_possible(), "ahead observation must receive an effective stage stop token");
        if (!parallel) {
            check(std::this_thread::get_id() == owner && calls > 0,
                  "W1 must observe the next dispatch serially on its owner");
            ahead_ready = ahead_drained = true;
            return;
        }
        check(std::this_thread::get_id() != owner, "compiled lookahead must use a spare CPU lane");
        wait(lock, [&] { return bulk_entered; }, "owner must enter the backend while observation is ahead");
        ahead_ready = true;
        changed.notify_all();
        if (action == OverlapAction::observation_failure) {
            ahead_drained = true;
            throw Error("injected observation failure");
        }
        if (action == OverlapAction::owner_failure || action == OverlapAction::cancel) {
            if (action == OverlapAction::cancel) {
                lock.unlock();
                external.request_stop();
                lock.lock();
            }
            changed.wait_for(lock, stop, std::chrono::seconds(5), [] { return false; });
            check(stop.stop_requested(), "ahead observation must drain on stage cancellation");
            observed_cancel = true;
        }
        ahead_drained = true;
        changed.notify_all();
    }

    void statistics(std::stop_token stop) {
        std::unique_lock lock(mutex);
        check(std::this_thread::get_id() == owner, "every compiled backend call must stay on its owner");
        if (++calls != 1) return;
        bulk_entered = true;
        changed.notify_all();
        if (!parallel) {
            check(observed_firsts.size() == 1, "W1 must not observe ahead of the owner's first bulk call");
            return;
        }
        wait(lock, [&] { return ahead_ready; }, "CPU observation must progress while the bulk call is held");
        if (action == OverlapAction::owner_failure) throw Error("injected bulk failure");
        if (action == OverlapAction::observation_failure || action == OverlapAction::cancel) {
            changed.wait_for(lock, stop, std::chrono::seconds(5), [] { return false; });
            check(stop.stop_requested(), "bulk work must receive peer or external cancellation");
            bulk_cancel = true;
            throw Error("injected bulk cancellation");
        }
        wait(lock, [&] { return ahead_drained; }, "ahead observation must finish before the held bulk call returns");
    }
};

void compiled_observation_overlap() {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample + 2 * f.fs, 3 * f.fs + 17, FineRunKind::probe, true};
    struct Result {
        ReceiverSlotScore score;
        FineProgress progress;
        std::vector<std::size_t> lengths, firsts;
    };
    const auto run = [&](unsigned workers, bool erase) {
        auto config = f.config;
        auto witness = std::make_shared<ProjectionWitness>();
        std::vector<Complex> observed_input(f.samples(extent).begin(), f.samples(extent).end());
        std::vector<std::uint8_t> valid(extent.rows, 1);
        if (erase) {
            valid[f.fs + f.fs / 4 - 1] = 0;
            observed_input.erase(observed_input.begin() + f.fs, observed_input.begin() + f.fs + f.fs / 4);
        }
        witness->input = observed_input;
        witness->variance = config.noise_variance;
        config.bulk_backend = std::make_shared<ProjectionWitnessBackend>(witness);
        FineOverlapGate gate(workers > 1);
        witness->before_statistics = [&](std::stop_token stop) { gate.statistics(stop); };
        auto observed = extent;
        observed.observed = [&](auto first, auto stop) { gate.observed(first, stop); };
        FineSession session(config, f.slot, f.key, observed, f.samples(extent), valid, f.coordinates,
                            100, 12, workers, fine_workspace(config, f.slot, observed, workers));
        // Two advances exercise nonzero call_serial and reuse of both ring
        // entries, including the enlarged final calendar block.
        session.advance_to(f.fs);
        session.advance_to(extent.rows);
        Result result;
        session.finish(result.score);
        result.progress = session.progress();
        result.lengths = witness->lengths;
        result.firsts = gate.observed_firsts;
        check(gate.ahead_ready && gate.ahead_drained && witness->cursor == observed_input.size() &&
              result.progress.erased_blocks == (erase ? 1U : 0U),
              "all projected input and held observation must finish before publishing progress");
        return result;
    };
    const auto same_lane = [](const FineLaneWork& a, const FineLaneWork& b) {
        return a.retained_states == b.retained_states && a.bit_scores == b.bit_scores &&
            a.transition_edges == b.transition_edges && a.merged_channel_components == b.merged_channel_components &&
            a.basis_calls == b.basis_calls && a.basis_rows == b.basis_rows &&
            a.gaussian_updates == b.gaussian_updates && a.projection_modes == b.projection_modes &&
            a.positive_basis_blocks == b.positive_basis_blocks && a.projected_template_energy == b.projected_template_energy;
    };
    for (const bool erase : {false, true}) {
        const auto serial = run(1, erase), parallel = run(2, erase);
        check(serial.lengths == parallel.lengths && serial.firsts == parallel.firsts &&
          serial.score.bit_evidence == parallel.score.bit_evidence &&
          serial.score.adaptive_bit_evidence == parallel.score.adaptive_bit_evidence &&
          serial.score.combined_bit_evidence == parallel.score.combined_bit_evidence &&
          serial.score.quiet_energy == parallel.score.quiet_energy &&
          serial.score.quiet_modes == parallel.score.quiet_modes &&
          serial.score.basis_rows == parallel.score.basis_rows &&
          same_lane(serial.progress.initial_affine, parallel.progress.initial_affine) &&
          same_lane(serial.progress.adaptive, parallel.progress.adaptive) &&
          serial.progress.bulk->statistics_blocks == parallel.progress.bulk->statistics_blocks &&
          serial.progress.bulk->projected_values == parallel.progress.bulk->projected_values,
          "compiled lookahead must preserve serial projection, mapping, state work and final evidence");
        check(!parallel.score.complete && parallel.score.decision == SlotDecision::ambiguous,
              "a synthetic-statistics diagnostic must never qualify physical slot completion");
    }

    for (const auto action : {OverlapAction::owner_failure, OverlapAction::observation_failure, OverlapAction::cancel}) {
        auto config = f.config;
        auto witness = std::make_shared<ProjectionWitness>();
        witness->input = f.samples(extent);
        witness->variance = config.noise_variance;
        config.bulk_backend = std::make_shared<ProjectionWitnessBackend>(witness);
        FineOverlapGate gate(true, action);
        witness->before_statistics = [&](std::stop_token stop) { gate.statistics(stop); };
        auto observed = extent;
        observed.observed = [&](auto first, auto stop) { gate.observed(first, stop); };
        FineSession session(config, f.slot, f.key, observed, witness->input, {}, f.coordinates,
                            100, 12, 2, fine_workspace(config, f.slot, observed, 2), gate.external.get_token());
        const auto expected = action == OverlapAction::owner_failure ? "injected bulk failure" :
            action == OverlapAction::observation_failure ? "injected observation failure" : "Robust reception cancelled";
        rejects_with([&] { session.advance_to(extent.rows); }, expected,
                     "overlapped fine work must preserve the initiating failure or external cancellation");
        check(gate.ahead_ready && gate.ahead_drained &&
              (action == OverlapAction::observation_failure ? gate.bulk_cancel : gate.observed_cancel),
              "failure or cancellation must drain the peer before advance returns");
        rejects([&] { (void)session.progress(); }, "failed overlap cannot publish partial evidence");
        ReceiverSlotScore score;
        rejects([&] { session.finish(score); }, "failed overlap cannot finish a slot");
    }
}

void terminal_projection_boundaries() {
    struct Case {
        unsigned fs;
        std::size_t rows, deferred, segments;
        std::vector<std::size_t> lengths;
    };
    const std::array cases{
        Case{6000, 6000, 3000, 2, {1500, 1500, 1500, 1500}},
        Case{6000, 6001, 3000, 2, {1500, 1500, 1500, 1501}},
        Case{6000, 7499, 3000, 2, {1500, 1500, 1500, 2999}},
        Case{6000, 7500, 6000, 3, {1500, 1500, 1500, 1500, 1500}},
        Case{6000, 8999, 6000, 3, {1500, 1500, 1500, 1500, 2999}},
        Case{6002, 7501, 3001, 2, {1500, 1501, 1500, 1500, 1500}},
        Case{6000, 1499, 0, 1, {1499}},
        Case{6000, 13, 0, 1, {13}},
    };
    for (const auto& test : cases) {
        ReceiverConfig config;
        config.sample_rate_hz = test.fs;
        config.noise_variance = .125;
        ReceiverSlotPlan slot;
        slot.first_sample = 1000;
        slot.stop_sample = slot.first_sample + test.rows;
        slot.quiet_first_sample = slot.first_sample;
        // An aperture ending one sample early must exclude the complete last
        // projection block, even when most of that block lies in quiet time.
        slot.quiet_stop_sample = slot.stop_sample - 1;
        slot.format.sample_rate = test.fs;
        slot.format.inner_rate_hz = 120;
        slot.format.added_symbols = 1;
        slot.format.bit_seconds = .001;
        slot.format.pulse_shape = PulseShape::TaperedRrc;
        slot.format.epoch = 1793850000;
        slot.format.symbol_ordinal = 3117;
        std::vector<Complex> input(test.rows);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = {.3 * std::sin(.731 * i) + .17 * std::cos(.193 * i), .2 * std::sin(.557 * i)};
        input.back() += Complex{2, -3};
        const std::array<FineCoordinate, 1> coordinates{{{0, 0, 0, 0}}};
        auto witness = std::make_shared<ProjectionWitness>();
        witness->input = input;
        witness->quiet_stop = test.rows - 1;
        witness->variance = config.noise_variance;
        config.bulk_backend = std::make_shared<ProjectionWitnessBackend>(witness);
        const FineObservation extent{slot.first_sample, test.rows, FineRunKind::probe, true};
        const auto workspace = fine_workspace(config, slot, extent, 1);
        rejects<ReceiverWorkspaceError>([&] {
            FineSession undersized(config, slot, Crypto::amateur(), extent, input, {}, coordinates,
                                   100, 0, 1, workspace - 1);
        }, "terminal geometry must enforce its exact workspace quote");
        FineSession session(config, slot, Crypto::amateur(), extent, input, {}, coordinates,
                            100, 0, 1, workspace);
        session.advance_to(test.rows - 1);
        check(session.progress().processed_rows == test.deferred && witness->cursor == test.deferred,
              "checkpoint exposure must not split the immutable terminal segment");
        session.advance_to(test.rows);
        const auto progress = session.progress();
        check(progress.observation_complete && progress.processed_rows == test.rows &&
              progress.segments == test.segments && progress.blocks == test.lengths.size() &&
              witness->cursor == test.rows && witness->lengths == test.lengths,
              "terminal calendar must project all real samples exactly once");
        check(progress.quiet_modes == witness->quiet_modes &&
              std::abs(progress.quiet_energy - witness->quiet_energy) <= 1e-9 * std::max(1., witness->quiet_energy),
              "actual-length unitary projections must preserve noise variance and whole-block quiet eligibility");
    }

    ReceiverConfig oversized;
    oversized.sample_rate_hz = 2'097'152;
    oversized.noise_variance = 1;
    ReceiverSlotPlan slot;
    slot.stop_sample = 1'048'577;
    slot.quiet_stop_sample = slot.stop_sample;
    rejects_with([&] { (void)fine_workspace(oversized, slot, 1); }, "segment exceeds 1048576 samples",
                 "the actual merged segment must obey the public maximum before allocation");
}

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
    check(near_end.processed_rows == 19 * f.fs - f.fs / 2 && !near_end.observation_complete,
          "the merged terminal segment must wait for its full declared extent");
    split.advance_to(f.iq.size());
    ReceiverSlotScore incremental;
    split.finish(incremental);
    const auto finished = split.progress();
    check(finished.observation_complete && finished.last_block_rows == f.fs / 4 + 17 &&
          finished.finished && incremental.complete, "the enlarged terminal block must be scored once");
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
        helper_terminal_geometry();
        terminal_projection_boundaries();
        compiled_observation_overlap();
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
