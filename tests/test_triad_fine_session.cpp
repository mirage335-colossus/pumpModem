#include "triad_receiver_fine.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <thread>

extern "C" int triad_gaussian_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* mean, double* covariance,
    double* scores, double* diagnostics) noexcept;

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

    explicit Fixture(unsigned sample_rate = 2400, double inner_rate = 120, bool extend_for_source = false)
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
        PatternSource source(slot.format, key, 1);
        if (extend_for_source) {
            slot.stop_sample = slot.first_sample + std::max<std::uint64_t>(
                19 * fs + 17, source.geometry().source_samples + 2 * fs);
            slot.quiet_stop_sample = slot.stop_sample - 2 * fs;
        }
        iq.resize(slot.stop_sample - slot.first_sample);
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

double test_log_add(double left, double right) {
    const auto minus_infinity = -std::numeric_limits<double>::infinity();
    if (left == minus_infinity) return right;
    if (right == minus_infinity) return left;
    return std::max(left, right) + std::log1p(std::exp(-std::abs(left - right)));
}

void slow_gain_coherence_is_bounded_and_normalized() {
    // This public geometry has one-second fine cells. Its declared .04 Hz
    // phase/frequency scale admits the slow gain alternatives. The phase
    // trajectory is deterministic and independent of the receiver model bank.
    const Fixture f(240, 6, true);
    auto samples = f.iq;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto elapsed = static_cast<double>(i) / f.fs;
        samples[i] *= std::polar(1., 2.2 * std::sin(2 * std::numbers::pi * elapsed / 400.));
    }
    const FineObservation extent{f.slot.first_sample, samples.size(), FineRunKind::probe, true};

    struct Result { FineProgress progress; ReceiverSlotScore score; };
    const auto run = [&](ReceiverConfig config, unsigned workers, bool prefix) {
        const auto memory = fine_workspace(config, f.slot, extent, workers);
        FineSession session(config, f.slot, f.key, extent, samples, {}, f.coordinates,
                            100, 12, workers, memory);
        if (prefix) session.advance_to(f.fs * 11);
        session.advance_to(extent.rows);
        Result result;
        session.finish(result.score);
        result.progress = session.progress();
        return result;
    };

    auto static_config = f.config;
    const auto static_result = run(static_config, 1, false);
    auto slow_config = f.config;
    slow_config.time_varying_channel = true;
    slow_config.time_varying_common_doppler_hz = .04;
    slow_config.time_varying_atmospheric_hz = .04;
    const auto slow_serial = run(slow_config, 1, false);
    const auto slow_parallel = run(slow_config, 2, true);
    const auto old_workspace = fine_workspace(static_config, f.slot, extent, 1);
    const auto slow_workspace = fine_workspace(slow_config, f.slot, extent, 1);

    const auto expected_tau = 1 / (2 * std::numbers::pi * .04);
    check(slow_serial.progress.slow_gain_coherence_seconds[0] == expected_tau &&
          slow_serial.progress.slow_gain_coherence_seconds[1] == 4 * expected_tau &&
          std::isfinite(slow_serial.progress.slow_gain_bit_evidence[0]) &&
          std::isfinite(slow_serial.progress.slow_gain_bit_evidence[1]),
          "eligible slow geometry must report both fixed slow-coherence alternatives and their evidence");
    check(static_result.progress.slow_gain_coherence_seconds == std::array<double, 2>{} &&
          static_result.progress.slow_gain_bit_evidence == std::array<double, 2>{} &&
          !static_config.time_varying_channel,
          "the explicit Off path must not acquire slow gain alternatives");
    check(slow_workspace > old_workspace,
          "admission must reserve the additional slow channel posterior state");
    rejects<ReceiverWorkspaceError>([&] {
        FineSession underquoted(slow_config, f.slot, f.key, extent, samples, {}, f.coordinates,
                                100, 12, 1, slow_workspace - 1);
    }, "slow channel state must be included in the admitted fine workspace");

    // The old affine likelihood keeps .75 prior mass and the two new
    // normalized OU likelihoods share .25. This checks the applied mixture
    // itself, not merely the geometry's scale-selection formula.
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto expected = test_log_add(std::log(.75) + static_result.progress.initial_affine.bit_scores[bit],
            std::log(.25) + slow_serial.progress.slow_gain_bit_evidence[bit]);
        check(std::abs(expected - slow_serial.score.bit_evidence[bit]) < 1e-10,
              "slow gain evidence must enter the existing affine likelihood with its fixed prior mass");
    }

    const auto& old_lane = static_result.progress.initial_affine;
    const auto& new_lane = slow_serial.progress.initial_affine;
    check(new_lane.basis_rows == old_lane.basis_rows && new_lane.basis_calls == old_lane.basis_calls &&
          new_lane.positive_basis_blocks == old_lane.positive_basis_blocks &&
          new_lane.gaussian_updates == old_lane.gaussian_updates +
              slow_serial.progress.blocks * old_lane.initial_states * 3 * 2 &&
          slow_serial.progress.adaptive.transition_edges == static_result.progress.adaptive.transition_edges &&
          slow_serial.progress.adaptive.basis_rows == static_result.progress.adaptive.basis_rows,
          "slow gain work must reuse affine basis statistics without expanding adaptive search");
    check(slow_parallel.progress.slow_gain_coherence_seconds == slow_serial.progress.slow_gain_coherence_seconds &&
          slow_parallel.progress.slow_gain_bit_evidence == slow_serial.progress.slow_gain_bit_evidence &&
          slow_parallel.score.bit_evidence == slow_serial.score.bit_evidence &&
          slow_parallel.progress.initial_affine.basis_rows == new_lane.basis_rows &&
          slow_parallel.progress.adaptive.transition_edges == slow_serial.progress.adaptive.transition_edges,
          "slow gain likelihood must be invariant to prefix checkpoints and worker count");

    // A larger declared allowance must retain the slower actual trajectory
    // above. Two fine cells, rather than the upper bound, set the scale floor.
    auto wide_config = slow_config;
    wide_config.time_varying_common_doppler_hz = .1;
    wide_config.time_varying_atmospheric_hz = .1;
    auto wide_off = wide_config;
    wide_off.time_varying_channel = false;
    const auto wide_reference = run(wide_off, 1, false);
    const auto wide_serial = run(wide_config, 1, false);
    const auto wide_parallel = run(wide_config, 2, true);
    check(wide_serial.progress.slow_gain_coherence_seconds == std::array<double, 2>{2., 8.} &&
          fine_workspace(wide_config, f.slot, extent, 1) == slow_workspace,
          "wide variation bounds must retain the two-cell slow models and the same state reservation");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto expected = test_log_add(std::log(.75) + wide_reference.progress.initial_affine.bit_scores[bit],
            std::log(.25) + wide_serial.progress.slow_gain_bit_evidence[bit]);
        check(std::abs(expected - wide_serial.score.bit_evidence[bit]) < 1e-10,
              "clamped slow scales must retain the fixed normalized family weights");
    }
    check(wide_serial.progress.initial_affine.basis_rows == wide_reference.progress.initial_affine.basis_rows &&
          wide_serial.progress.initial_affine.gaussian_updates == new_lane.gaussian_updates &&
          wide_serial.progress.adaptive.transition_edges == wide_reference.progress.adaptive.transition_edges &&
          wide_serial.progress.adaptive.basis_rows == wide_reference.progress.adaptive.basis_rows &&
          wide_parallel.score.bit_evidence == wide_serial.score.bit_evidence &&
          wide_parallel.progress.slow_gain_bit_evidence == wide_serial.progress.slow_gain_bit_evidence,
          "wide bounds must preserve basis/adaptive work, two-model cost and prefix/worker invariance");
}

void slow_gain_coherence_gate_preserves_fast_and_off_paths() {
    const Fixture f(240, 60); // 0.1 s per fine cell
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::probe, true};
    const auto run = [&](ReceiverConfig config) {
        const auto memory = fine_workspace(config, f.slot, extent, 1);
        FineSession session(config, f.slot, f.key, extent, f.iq, {}, f.coordinates,
                            100, 12, 1, memory);
        session.advance_to(extent.rows);
        ReceiverSlotScore score;
        session.finish(score);
        return std::pair{session.progress(), score};
    };
    auto fast = f.config;
    fast.time_varying_channel = true;
    fast.time_varying_common_doppler_hz = 12; // coherence is shorter than two cells
    const auto fast_result = run(fast);
    auto off = f.config;
    off.time_varying_common_doppler_hz = .04;
    const auto off_result = run(off);
    check(fast_result.first.slow_gain_coherence_seconds == std::array<double, 2>{} &&
          fast_result.first.slow_gain_bit_evidence == std::array<double, 2>{} &&
          off_result.first.slow_gain_coherence_seconds == std::array<double, 2>{} &&
          off_result.first.slow_gain_bit_evidence == std::array<double, 2>{},
          "fast variation and explicit Off must retain the existing fine model set");
    check(fast_result.first.initial_affine.gaussian_updates == off_result.first.initial_affine.gaussian_updates &&
          fast_result.first.initial_affine.basis_rows == off_result.first.initial_affine.basis_rows &&
          fast_result.second.bit_evidence == off_result.second.bit_evidence,
          "an ineligible slow model must not alter fast or Off likelihood work");
}

void gaussian_update_matches_dense_complex_covariance() {
    using C = std::complex<double>;
    constexpr std::size_t count = 4;
    constexpr double variance = .7;
    constexpr double gain_power = .7;
    constexpr double coherence = 3.7;
    const std::array<double, count> times{0., .25, .5, .9};
    const std::array<C, count> basis{{{1., 0.}, {}, {.8, .25}, {-.6, .4}}};
    const std::array<C, count> observation{{{.8, -.2}, {}, {-.3, .7}, {.2, .1}}};

    // Dense H1 output covariance over observed blocks {0,2,3}. Block 1 is
    // erased. Its time remains in the AR recurrence: the gap from observed
    // block 0 to block 2 is .5 s, followed by the merged .4 s terminal step.
    constexpr std::array<std::size_t, 3> observed{0, 2, 3};
    std::array<std::array<C, 3>, 3> lower{};
    const auto covariance_at = [&](std::size_t row, std::size_t col) {
        const auto i = observed[row], j = observed[col];
        const auto separation = std::abs(times[i] - times[j]);
        const auto channel = gain_power * std::exp(-separation / coherence) * basis[i] * std::conj(basis[j]);
        return channel + (row == col ? C{variance, 0.} : C{});
    };
    double log_determinant = 0;
    for (std::size_t row = 0; row < observed.size(); ++row) {
        for (std::size_t col = 0; col <= row; ++col) {
            auto value = covariance_at(row, col);
            for (std::size_t k = 0; k < col; ++k)
                value -= lower[row][k] * std::conj(lower[col][k]);
            if (row == col) {
                check(std::abs(value.imag()) < 1e-13 && value.real() > 0,
                      "dense reference covariance must be Hermitian positive definite");
                lower[row][col] = std::sqrt(value.real());
                log_determinant += 2 * std::log(lower[row][col].real());
            } else {
                lower[row][col] = value / lower[col][col].real();
            }
        }
    }
    std::array<C, 3> whitened{};
    for (std::size_t row = 0; row < observed.size(); ++row) {
        auto value = observation[observed[row]];
        for (std::size_t col = 0; col < row; ++col)
            value -= lower[row][col] * whitened[col];
        whitened[row] = value / lower[row][row].real();
    }
    double observed_energy = 0, whitened_energy = 0;
    for (const auto index : observed) observed_energy += std::norm(observation[index]) / variance;
    // Solving with the full H1 covariance already whitens the thermal noise.
    for (const auto value : whitened) whitened_energy += std::norm(value);
    const auto dense_log_likelihood_ratio = observed_energy - whitened_energy - log_determinant +
        observed.size() * std::log(variance);

    std::array<double, 1> power{gain_power};
    std::array<C, 3> means{};
    std::array<C, 3> posterior{{C{}, C{gain_power, 0.}, C{gain_power, 0.}}};
    std::array<double, 3> scores{};
    std::array<double, 2> diagnostics{std::numeric_limits<double>::infinity(), 0.};
    double last_center = times[0];
    for (std::size_t first = 0; first < count; first += 2) {
        const auto blocks = std::min<std::size_t>(2, count - first);
        std::array<C, 3 * 2> block_correlations{};
        std::array<C, 3 * 2> block_grams{};
        std::array<double, 4 * 2> transitions{};
        for (std::size_t local = 0; local < blocks; ++local) {
            const auto index = first + local;
            const auto elapsed = index ? times[index] - last_center : 0.;
            last_center = times[index];
            const auto coefficient = std::exp(-elapsed / coherence);
            const auto transition = 4 * local;
            transitions[transition] = coefficient * coefficient;
            transitions[transition + 1] = 1 - coefficient * coefficient;
            transitions[transition + 2] = coefficient;
            const auto h0 = (local * 3) * 1;
            const auto h1 = (local * 3 + 1) * 1;
            const auto h2 = (local * 3 + 2) * 1;
            // An erased block has zero projected Gram/correlation. Its
            // transition still advances the latent channel state.
            block_correlations[h0] = 0;
            block_correlations[h1] = std::conj(basis[index]) * observation[index] / variance;
            block_correlations[h2] = block_correlations[h1];
            block_grams[h0] = 0;
            block_grams[h1] = std::norm(basis[index]) / variance;
            block_grams[h2] = block_grams[h1];
        }
        const auto status = triad_gaussian_update_v1(1, 1, 1, blocks, 0, 1,
            reinterpret_cast<const double*>(block_correlations.data()),
            reinterpret_cast<const double*>(block_grams.data()), power.data(), transitions.data(),
            reinterpret_cast<double*>(means.data()), reinterpret_cast<double*>(posterior.data()),
            scores.data(), diagnostics.data());
        check(status == 0, "Gaussian update kernel must accept the fixed finite slow-gain likelihood");
    }
    check(std::abs(scores[1] - dense_log_likelihood_ratio) < 2e-12,
          "block Gaussian updates must equal the dense complex covariance likelihood across an erased block and merged tail");
}

void tracked_span_matches_signed_correlation_without_changing_evidence() {
    const Fixture f(480);
    auto config = f.config;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 12;
    config.spread_delay_seconds = .004;
    const auto plan = varying_fine_plan(config, f.slot, 2);
    const auto work = tracked_frequency_span_workspace(f.slot, plan);
    check(work > 0 && work < 512 * 1024, "tracked span must quote its bounded private workspace");
    const auto final_rows = plan.block_rows + f.iq.size() % plan.block_rows;
    check(final_rows > plan.block_rows, "tracked span fixture must merge a nonempty terminal remainder");
    const auto fft_rows = std::bit_ceil(2 * final_rows);
    const auto frequency = 2. * f.fs / fft_rows;
    const std::array<Tap, 2> paths{{{0, 0}, {config.spread_delay_seconds * .5, 0}}};
    MappedBasis source(config, f.slot, f.key, final_rows, 1);
    const MappedBasisRow row{0, 1, 0, 0};
    std::vector<double> offsets(final_rows);
    std::iota(offsets.begin(), offsets.end(), 0.);
    std::vector<Complex> rendered(4 * final_rows), observation(f.iq.size());
    Complex phase{1};
    for (std::size_t first = 0; first < observation.size();) {
        const auto rows = observation.size() - first < 2 * plan.block_rows ? final_rows : plan.block_rows;
        const auto sign = first / plan.block_rows % 2 ? -1. : 1.;
        const auto increment = std::polar(1., 2 * std::numbers::pi * sign * frequency / f.fs);
        source.render_pair(row, paths, first, std::span(offsets).first(rows),
            std::span(rendered).first(4 * rows), {});
        for (std::size_t i = 0; i < rows; ++i) {
            observation[first + i] = phase *
                (Complex{.75, .2} * rendered[2 * rows + i] + Complex{-.3, .6} * rendered[3 * rows + i]);
            phase *= increment;
        }
        first += rows;
    }

    ReceiverExecutor executor(2);
    ReceiverSlotScore ordinary, diagnostic;
    const auto score = [&](const ReceiverConfig& c, ReceiverSlotScore& result, TrackedFrequencySpan* span) {
        score_varying_fine(c, f.slot, f.key, plan, observation, f.coordinates, {},
            100, 12, 2, result, {}, executor, nullptr, span);
    };
    TrackedFrequencySpan span;
    score(config, ordinary, nullptr);
    score(config, diagnostic, &span);
    check(diagnostic.varying_bit_evidence == ordinary.varying_bit_evidence &&
          diagnostic.fixed_varying_bit_evidence == ordinary.fixed_varying_bit_evidence &&
          diagnostic.tracked_frequency_bit_evidence == ordinary.tracked_frequency_bit_evidence &&
          diagnostic.varying_frequency_states == ordinary.varying_frequency_states &&
          diagnostic.varying_basis_rows == ordinary.varying_basis_rows,
          "appraisal span must not alter ordinary tracking evidence or basis work");
    check(span.coordinates.size() == 1 && span.observation_energy.size() == f.iq.size() / plan.block_rows,
          "tracked span must retain one timing coordinate across the full public block calendar");
    double captured = 0, total = 0;
    bool found_negative = false, found_positive = false;
    for (std::size_t i = 0; i < span.observation_energy.size(); ++i) {
        const auto& result = span.coordinates[0].blocks[i][1];
        captured += result.captured_energy;
        total += span.observation_energy[i];
        if (result.verified && span.observation_energy[i] > 1e-6) {
            found_negative |= (i % 2 == 1 && result.frequency_hz == -frequency);
            found_positive |= (i % 2 == 0 && result.frequency_hz == frequency);
        }
    }
    check(total > 0 && std::abs(captured - total) < 1e-8 * total && found_negative && found_positive,
          "the tracked span must capture a known two-column signal at both signed FFT frequencies");

    const std::array<FineCoordinate, 2> nominations{{{0, 0, 0, -10}, {0, 0, 0, 10}}};
    TrackedFrequencyClockShadow shadow{nominations};
    ReceiverSlotScore original, expanded;
    score_varying_fine(config, f.slot, f.key, plan, observation, f.coordinates, {},
        100, 12, 2, original, {}, executor, nullptr, &span, &shadow);
    score_varying_fine(config, f.slot, f.key, plan, observation, f.coordinates, nominations,
        100, 12, 2, expanded, {}, executor);
    check(original.varying_bit_evidence == ordinary.varying_bit_evidence &&
          original.tracked_frequency_bit_evidence == ordinary.tracked_frequency_bit_evidence &&
          original.varying_basis_rows == ordinary.varying_basis_rows &&
          shadow.ordinary_coordinates == 1 && shadow.added_coordinates == 2 &&
          shadow.added_basis_rows == 8 * observation.size() && span.coordinates.size() == 3 &&
          expanded.varying_basis_rows == original.varying_basis_rows + shadow.added_basis_rows,
          "clock shadow must keep ordinary bit evidence exact and charge every added basis row");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        check(std::abs(shadow.tracked_bit_evidence[bit] - expanded.tracked_frequency_bit_evidence[bit]) < 1e-10 &&
              std::abs(shadow.varying_bit_evidence[bit] - expanded.varying_bit_evidence[bit]) < 1e-10 &&
              std::abs(shadow.tracked_log_prior_adjustment - expanded.tracked_frequency_log_prior_adjustment) < 1e-12,
              "clock shadow and canonical supplemental timing path must charge the same public prior");
    }
    auto coincident = config;
    coincident.spread_delay_seconds = 1e-16;
    ReceiverSlotScore coincident_score;
    score(coincident, coincident_score, &span);
    check(std::ranges::any_of(span.coordinates[0].blocks,
          [](const auto& block) { return block[1].rank_ambiguous; }),
          "a nearly coincident second column must carry a rank uncertainty bound");
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
        slow_gain_coherence_is_bounded_and_normalized();
        slow_gain_coherence_gate_preserves_fast_and_off_paths();
        gaussian_update_matches_dense_complex_covariance();
        tracked_span_matches_signed_correlation_without_changing_evidence();
        incomplete_bulk_cannot_publish_quiet();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
