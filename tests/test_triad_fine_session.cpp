#include "triad_receiver_fine.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_gaussian_kernel.hpp"
#include "triad_mapped_kernel.hpp"

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
#include <random>
#include <stdexcept>
#include <span>
#include <thread>
#include <tuple>

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

    explicit Fixture(unsigned sample_rate = 2400, double inner_rate = 120, bool extend_for_source = false,
                     double bit_seconds = .001)
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
        slot.format.bit_seconds = bit_seconds;
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
    const FineObservation extent{slot.first_sample, zero.size(), FineRunKind::operational, true, FineLanes::fixed_and_adaptive};
    const auto workspace = fine_workspace(config, slot, extent, 1);
    FineSession session(config, slot, Crypto::amateur(), extent, zero, {}, coordinates,
                        100, 12, 1, workspace);
    check(session.progress().processed_rows == 0,
          "public Helper tail geometry must construct without processing input");

    auto short_slot = slot;
    short_slot.stop_sample = short_slot.first_sample + 13;
    short_slot.quiet_first_sample = short_slot.first_sample;
    short_slot.quiet_stop_sample = short_slot.stop_sample;
    const FineObservation short_extent{short_slot.first_sample, 13, FineRunKind::operational, true, FineLanes::fixed_and_adaptive};
    rejects_with([&] { (void)fine_workspace(config, short_slot, short_extent, 1); },
                 "cannot resolve DATA and Helper bands",
                 "workspace admission must reject an unresolvable real DFT before constructing a receiver");
    rejects_with([&] {
        FineSession unresolved(config, short_slot, Crypto::amateur(), short_extent,
                               std::span(zero).first(13), {}, coordinates, 100, 12, 1, workspace);
    }, "cannot resolve DATA and Helper bands",
       "a whole observation too short to resolve Helper exclusions must still fail");
}

void helper_dc_projection() {
    // This clean, stationary 3.6-Hz DATA band occupies only the DC bin of a
    // quarter-second block. The distant Helper is between DFT bins.
    constexpr std::size_t fs = 6000, block_rows = fs / 4, blocks = 2;
    constexpr double pilot_hz = 2400, carrier_hz = 1725;
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = carrier_hz;
    config.noise_variance = .05;
    config.helper_enabled = true;
    config.helper.low_frequency_hz = 550;
    config.helper.auxiliary_frequency_hz = 950;
    config.helper.frequency_hz = pilot_hz;
    ReceiverSlotPlan slot;
    slot.stop_sample = slot.quiet_stop_sample = 4 * fs;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 3.6;
    slot.format.added_symbols = 1;
    slot.format.bit_seconds = 160.4;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850073;
    slot.format.symbol_ordinal = 7000037;
    const FineObservation extent{2 * fs, blocks * block_rows, FineRunKind::probe};
    const std::array<FineCoordinate, 1> coordinates{{{0, 0, 0, 0}}};
    const auto key = Crypto::amateur();
    const auto score = [&](const ReceiverConfig& receiver, const ReceiverSlotPlan& plan,
                           FineObservation observation, std::span<const Complex> samples) {
        const auto bytes = fine_workspace(receiver, plan, observation, 1);
        FineSession session(receiver, plan, key, observation, samples, {}, coordinates, 3, 0, 1, bytes);
        session.advance_to(samples.size());
        return session.progress();
    };
    const auto observe = [&](const ReceiverConfig& receiver, std::span<const Complex> samples) {
        const auto progress = score(receiver, slot, extent, samples);
        check(progress.observed_projection_modes == blocks && progress.quiet_modes == blocks,
              "the Helper taper must preserve the number of observed noise modes");
        return progress.quiet_energy * receiver.noise_variance;
    };

    const double angle = 2 * std::numbers::pi * (pilot_hz - carrier_hz) / fs;
    std::vector<Complex> samples(extent.rows);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = std::polar(1., angle * i);
    const double rectangular = blocks * std::pow(std::sin(block_rows * angle / 2) / std::sin(angle / 2), 2)
                               / block_rows;
    const double tapered = observe(config, samples);
    check(tapered > 0 && tapered < 1e-4 * rectangular,
          "the native tapered DATA projection must suppress the off-bin Helper");
    auto gated = config;
    gated.helper.pilot_dwell_gating = true;
    check(std::abs(observe(gated, samples) / rectangular - 1) < 1e-9,
          "gated Helper geometry must retain the rectangular observation");
    check(fine_workspace(config, slot, extent, 1) - fine_workspace(gated, slot, extent, 1)
              == block_rows * sizeof(double),
          "workspace admission must include the one distinct tapered mode");

    std::ranges::fill(samples, Complex{1, 0});
    // The symmetric half-sample cosine edges of length R each contribute R/2
    // to the sum and 3R/8 to the squared norm. Check this closed-form DC energy.
    constexpr auto edge_rows = block_rows / 10;
    constexpr auto weight_sum = block_rows - edge_rows;
    constexpr auto weight_square_sum = block_rows - 1.25 * edge_rows;
    constexpr auto dc_energy = blocks * weight_sum * weight_sum / weight_square_sum;
    check(std::abs(observe(config, samples) / dc_energy - 1) < 1e-10,
          "the tapered observation must use a unit-norm DC mode");

    auto cell_slot = slot;
    cell_slot.stop_sample = cell_slot.quiet_stop_sample = 40'000;
    const auto cell_shape = fine_block_geometry(config, cell_slot);
    check(cell_shape.block_rows == 10'000 && cell_shape.segment_rows == 20'000,
          "sufficiently separated Helper and DATA must admit the whole-cell projection");
    const FineObservation cell_extent{0, 40'000, FineRunKind::probe};
    samples.resize(cell_extent.rows);
    const double off_bin = 2 * std::numbers::pi * (pilot_hz - carrier_hz + .137) / fs;
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = std::polar(1., off_bin * i);
    const auto cell_progress = score(config, cell_slot, cell_extent, samples);
    check(cell_progress.quiet_modes >= 12 && cell_progress.quiet_energy * config.noise_variance < 1e-8,
          "the tapered cell must retain multiple DATA modes and suppress an off-bin Helper");
}

void static_low_rate_projection() {
    const Fixture f(240, 3.6, true, 160.4);
    auto config = f.config;
    config.clock_bound_ppm = 0;
    config.noise_variance = .001;
    const auto shape = fine_block_geometry(config, f.slot);
    check(shape.block_rows == 400 && shape.segment_rows == 800,
          "a static slow DATA cell must retain more than the quarter-second DC mode");
    const auto ordinary = [&](ReceiverConfig value, ReceiverSlotPlan slot) {
        const auto shape = fine_block_geometry(value, slot);
        return shape.block_rows == 60 && shape.segment_rows == 120;
    };
    auto moving = config;
    moving.time_varying_channel = true;
    check(ordinary(moving, f.slot), "changing channels must retain their admitted short observation geometry");
    auto helper = config;
    helper.helper_enabled = true;
    helper.helper.pilot_dwell_gating = true;
    check(ordinary(helper, f.slot), "dwell-gated Helper must keep its separately qualified geometry");
    auto short_quiet = f.slot;
    short_quiet.quiet_stop_sample = short_quiet.quiet_first_sample + 1199;
    check(ordinary(config, short_quiet), "an enlarged projection must fit the declared physical quiet aperture");

    const auto run = [&](const Crypto& key, std::span<const Complex> input) {
        const FineObservation extent{f.slot.first_sample, input.size(), FineRunKind::operational};
        const auto bytes = fine_workspace(config, f.slot, extent, 1);
        FineSession session(config, f.slot, key, extent, input, {}, f.coordinates, 100, 0, 1, bytes);
        session.advance_to(shape.segment_rows - 1);
        check(!session.progress().observation_complete && session.progress().processed_rows == 0,
              "a partial enlarged segment cannot publish complete DATA or quiet evidence");
        session.advance_to(input.size());
        ReceiverSlotScore score;
        session.finish(score);
        classify_fine_slot(score, AdmissionGrid(0, 0, 0, 0, .01));
        return score;
    };
    const auto wanted = run(f.key, f.iq);
    check(wanted.complete && wanted.all_valid && wanted.zero_h0_verified && wanted.bit == 1,
          "one-cell projection must recover a strong low-rate wanted waveform");
    check(wrong_key_admission_passes(wanted, WrongKeyAdmission::maximum),
          "strong low-rate DATA must retain its discriminating shape under the strongest browser filter");
    const std::array<std::uint8_t, 64> serpent_root{17}, aes_root{37};
    const auto foreign = run(Crypto::from_independent_roots(serpent_root, aes_root), f.iq);
    check(!wrong_key_admission_passes(foreign, WrongKeyAdmission::guarded),
          "preserving low-rate wanted evidence must not admit the independent receiver key");
    const std::vector<Complex> zero(f.iq.size());
    const auto quiet = run(f.key, zero);
    check(quiet.complete && quiet.zero_h0_verified && quiet.noise_statistics_valid &&
          quiet.decision == SlotDecision::quiet && !quiet.bit && quiet.quiet_energy == 0 &&
          quiet.quiet_modes > 0 && quiet.quiet_threshold > 0,
          "the longer observation must still score actual quiet modes for physical completion");
}

void fp32_projection_workspace_and_scores() {
    const Fixture f(6000, 3.6, true, 160.4);
    auto automatic = f.config;
    automatic.spread_delay_seconds = .021;
    automatic.spread_doppler_hz = 90;
    automatic.time_varying_channel = true;
    auto full_precision = automatic;
    full_precision.native_coarse_precision = NativeCoarsePrecision::fp64;
    const FineObservation extent{f.slot.first_sample + 8 * f.fs, 2 * f.fs, FineRunKind::probe, true};
    const auto run = [&](const auto& config) {
        const auto bytes = fine_workspace(config, f.slot, extent, 1);
        FineSession session(config, f.slot, f.key, extent, f.samples(extent), {}, f.coordinates,
                            500, 0, 1, bytes);
        session.advance_to(extent.rows);
        ReceiverSlotScore score;
        session.finish(score);
        return session.progress();
    };
    const auto fp32 = run(automatic), fp64 = run(full_precision);
    const bool vector_cpu = std::string_view(datapump::modem::detail::search_fft::kernel_name()) == "avx2";
    check(vector_cpu ? fp32.workspace_bytes > fp64.workspace_bytes : fp32.workspace_bytes == fp64.workspace_bytes,
          "the selected projection and its worker scratch must be charged before allocation");
    check(fp32.quiet_modes == 8 * 47 && fp32.quiet_modes == fp64.quiet_modes &&
          std::abs(fp32.quiet_energy - fp64.quiet_energy) <= 1e-5 * std::max(1., fp64.quiet_energy),
          "the FP32 projection must preserve the physical quiet aperture and normalization");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto score = fp64.initial_affine.bit_scores[bit];
        check(std::isfinite(score) && std::abs(fp32.initial_affine.bit_scores[bit] - score) <=
              1e-5 * std::max(1., std::abs(score)), "FP32 projection changed the bounded likelihood fixture");
    }
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
    const FineObservation extent{f.slot.first_sample + 2 * f.fs, 3 * f.fs + 17, FineRunKind::probe, true, FineLanes::fixed_and_adaptive};
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
        const FineObservation extent{slot.first_sample, test.rows, FineRunKind::probe, true, FineLanes::fixed_and_adaptive};
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
    check(direct.complete && !direct.adaptive_diagnostics &&
          direct.bit_evidence[1] > direct.bit_evidence[0],
          "whole-slot production wrapper must recover the reference score ordering");

    const FineObservation observation{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true, FineLanes::fixed_and_adaptive};
    const auto measured_bound = fine_workspace(f.config, f.slot, observation, 1);
    ReceiverSlotScore full;
    score_fine(f.config, f.slot, f.key, f.iq, f.coordinates, 100, 12, 1, full, measured_bound, {},
               nullptr, AffineGainEvaluation::operational, FineLanes::fixed_and_adaptive);
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
    check(full.adaptive_diagnostics && incremental.adaptive_diagnostics &&
          full.bit_evidence == incremental.bit_evidence &&
          full.adaptive_bit_evidence == incremental.adaptive_bit_evidence &&
          full.combined_bit_evidence == incremental.combined_bit_evidence &&
          full.quiet_modes == incremental.quiet_modes && full.quiet_energy == incremental.quiet_energy &&
          full.quiet_threshold == incremental.quiet_threshold && full.basis_rows == incremental.basis_rows &&
          full.adaptive_transition_edges == incremental.adaptive_transition_edges &&
          full.affine_blocks == incremental.affine_blocks,
          "whole and checkpointed diagnostic evidence must match exactly");
    check(direct.bit_evidence == full.bit_evidence && direct.zero_h0_verified == full.zero_h0_verified &&
          direct.quiet_energy == full.quiet_energy && direct.quiet_threshold == full.quiet_threshold &&
          direct.quiet_modes == full.quiet_modes && direct.affine_blocks == full.affine_blocks &&
          direct.consumed_samples == full.consumed_samples && direct.basis_rows == finished.initial_affine.basis_rows &&
          direct.basis_rows < full.basis_rows && !direct.adaptive_blocks && !direct.adaptive_transition_edges &&
          !direct.adaptive_peak_states && bound < measured_bound,
          "fixed-only fine work must retain the same operational evidence and lease no adaptive work");
    rejects([&] { split.finish(incremental); }, "finish cannot be repeated");
}

void uninterrupted_interior_probe() {
    const Fixture f;
    const FineObservation extent{f.slot.first_sample + 2 * f.fs + 23, 6 * f.fs, FineRunKind::probe, true, FineLanes::fixed_and_adaptive};
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
    const FineObservation extent{f.slot.first_sample + 2 * f.fs, 2 * f.fs, FineRunKind::probe, true, FineLanes::fixed_and_adaptive};
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

    const FineObservation whole{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    auto whole_validity = validity;
    whole_validity.resize(whole.rows, 1);
    const auto whole_bound = fine_workspace(f.config, f.slot, whole, 1);
    FineSession fixed(f.config, f.slot, f.key, whole, f.iq, whole_validity, f.coordinates,
                      100, 12, 1, whole_bound);
    fixed.advance_to(whole.rows);
    ReceiverSlotScore fixed_score;
    fixed.finish(fixed_score);
    const auto fixed_work = fixed.progress();
    check(fixed_score.complete && fixed_score.consumed_samples == whole.rows &&
          fixed_score.zero_h0_verified && !fixed_score.all_valid && !fixed_score.noise_statistics_valid &&
          !fixed_score.adaptive_diagnostics && fixed_score.quiet_threshold == 0 &&
          fixed_work.erased_blocks == 2 && fixed_work.requested_invalid_rows == 2 &&
          fixed_work.additional_discarded_rows == f.fs / 2 - 2 &&
          fixed_work.initial_affine.gaussian_updates > 0 && !fixed_work.adaptive.initial_states,
          "fixed-only fine must finish its physical extent while denying quiet credit to erased input");

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
    const FineObservation extent{f.slot.first_sample + 3 * f.fs, f.fs, FineRunKind::cold_maximum, true, FineLanes::fixed_and_adaptive};
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
    // Production common nulls start at zero and stay inert, so every one is
    // skipped and the receipt keeps the logical and executed counts apart.
    check(work.initial_affine.gaussian_null_skips == work.blocks * 256 * receiver_coherence_count &&
          work.adaptive.gaussian_updates > 0 &&
          3 * work.adaptive.gaussian_null_skips == work.adaptive.gaussian_updates,
          "fine receipts must report the skipped inert common-null updates");

    auto fixed_extent = extent;
    fixed_extent.lanes = FineLanes::fixed_affine;
    const auto fixed_bound = fine_workspace(f.config, f.slot, fixed_extent, 1);
    FineSession fixed(f.config, f.slot, f.key, fixed_extent, f.samples(extent), {}, f.coordinates,
                      100, 40, 1, fixed_bound);
    fixed.advance_to(extent.rows);
    ReceiverSlotScore fixed_score;
    fixed.finish(fixed_score);
    const auto fixed_work = fixed.progress();
    check(fixed_work.observation_complete && fixed_work.representative_work && fixed_work.segments == 2 &&
          fixed_work.initial_affine.initial_states == 256 && fixed_work.initial_affine.peak_emission_states == 256 &&
          fixed_work.initial_affine.gaussian_updates == work.initial_affine.gaussian_updates &&
          fixed_work.initial_affine.basis_rows == work.initial_affine.basis_rows &&
          fixed_work.initial_affine.bit_scores == work.initial_affine.bit_scores &&
          !fixed_work.adaptive.initial_states && !fixed_work.adaptive.transition_edges &&
          !fixed_score.adaptive_diagnostics && !fixed_score.complete && !fixed_score.bit,
          "fixed-only cold probe must execute all 256 affine states in both segments without admission");
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
    Fixture f(240, 6, true);
    // Compare gain laws on the same short projection geometry. A wider quiet
    // aperture would admit the one-cell projection only for the Off control.
    f.slot.quiet_stop_sample = f.slot.quiet_first_sample + 2 * f.fs;
    auto samples = f.iq;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto elapsed = static_cast<double>(i) / f.fs;
        samples[i] *= std::polar(1., 2.2 * std::sin(2 * std::numbers::pi * elapsed / 400.));
    }
    const FineObservation extent{f.slot.first_sample, samples.size(), FineRunKind::probe, true, FineLanes::fixed_and_adaptive};

    struct Result { FineProgress progress; ReceiverSlotScore score; };
    const auto run = [&](ReceiverConfig config, unsigned workers, bool prefix,
                         AffineGainEvaluation evaluation = AffineGainEvaluation::operational) {
        const auto memory = fine_workspace(config, f.slot, extent, workers);
        FineSession session(config, f.slot, f.key, extent, samples, {}, f.coordinates,
                            100, 12, workers, memory, {}, nullptr, evaluation);
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
    const auto slow_comparator = run(slow_config, 1, false, AffineGainEvaluation::without_stationary_fading);
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
          3 * old_lane.gaussian_null_skips == old_lane.gaussian_updates &&
          // The additive stationary-fading pair still runs its null.
          new_lane.gaussian_null_skips == old_lane.gaussian_null_skips +
              (slow_serial.score.affine_stationary_fading_gain ? 0 :
                  slow_serial.progress.blocks * old_lane.initial_states * 2) &&
          3 * slow_comparator.progress.initial_affine.gaussian_null_skips ==
              slow_comparator.progress.initial_affine.gaussian_updates &&
          slow_serial.progress.adaptive.transition_edges == static_result.progress.adaptive.transition_edges &&
          slow_serial.progress.adaptive.basis_rows == static_result.progress.adaptive.basis_rows,
          "slow gain work must reuse affine basis statistics without expanding adaptive search");
    check(slow_parallel.progress.slow_gain_coherence_seconds == slow_serial.progress.slow_gain_coherence_seconds &&
          slow_parallel.progress.slow_gain_bit_evidence == slow_serial.progress.slow_gain_bit_evidence &&
          slow_parallel.score.bit_evidence == slow_serial.score.bit_evidence &&
          slow_parallel.progress.initial_affine.basis_rows == new_lane.basis_rows &&
          slow_parallel.progress.adaptive.transition_edges == slow_serial.progress.adaptive.transition_edges,
          "slow gain likelihood must be invariant to prefix checkpoints and worker count");
    check(slow_comparator.score.bit_evidence == slow_serial.score.bit_evidence &&
          slow_comparator.progress.slow_gain_bit_evidence == slow_serial.progress.slow_gain_bit_evidence &&
          slow_comparator.progress.initial_affine.gaussian_updates == new_lane.gaussian_updates,
          "the stationary-fading comparison must retain the existing slow gain family");

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

void direct_gain_packs_two_tap_statistics() {
    std::array<Complex, 6> direct_r{}, direct_g{};
    std::array<Complex, 12> correlations{};
    std::array<Complex, 24> gram{};
    for (std::size_t h = 0; h < direct_r.size(); ++h) {
        if (h % 3 == 0) continue; // exact common null per block
        direct_r[h] = {static_cast<double>(h) - 2.5, .6 * h - .4};
        direct_g[h] = 1. + .2 * h;
        correlations[2 * h] = direct_r[h];
        correlations[2 * h + 1] = {-.8 * h, .3 + h};
        gram[4 * h] = direct_g[h];
        gram[4 * h + 1] = {.2 * h, .3};
        gram[4 * h + 2] = std::conj(gram[4 * h + 1]);
        gram[4 * h + 3] = 2.5 - .3 * h;
    }
    for (const auto ranks : {std::array{2u, 1u}, std::array{0u, 2u}, std::array{2u, 0u}}) {
        auto packed_r = correlations;
        auto packed_g = gram;
        pack_direct_gain_statistics(packed_r, packed_g, ranks);
        for (std::size_t h = 0; h < direct_r.size(); ++h) {
            const bool observed = ranks[h / 3] != 0;
            check(packed_r[h] == (observed ? direct_r[h] : Complex{}) &&
                  packed_g[h] == (observed ? direct_g[h] : Complex{}),
                  "additive packing must retain both direct blocks and bits, excluding echo, cross terms and rank-zero residuals");
        }
    }
}

void stationary_fading_gain_covers_direct_paths() {
    Fixture clean(240, 60, true, 16.);
    const auto duration = geometry(clean.slot.format).bit_seconds;
    check(duration >= 10. && duration < 32.,
          "the stationary-fading check must use a complete DATA aperture below the former duration cutoff");
    auto varying = clean.config;
    varying.time_varying_channel = true;
    varying.time_varying_common_doppler_hz = 12;
    varying.time_varying_atmospheric_hz = 12;
    auto mixed = clean, noise = clean;
    const auto stationary = std::polar(std::sqrt(.5), -.63);
    std::mt19937_64 rng(761);
    std::normal_distribution<double> gaussian(0.,std::sqrt(varying.noise_variance / 2.));
    for (std::size_t i = 0; i < clean.iq.size(); ++i) {
        const auto t = static_cast<double>(i) / clean.fs;
        const auto phase = .9 * std::sin(2 * std::numbers::pi * t / 7.) +
                           .3 * std::sin(2 * std::numbers::pi * t / 13.);
        noise.iq[i] = {gaussian(rng), gaussian(rng)};
        mixed.iq[i] = (stationary + std::polar(std::sqrt(.5), phase)) * clean.iq[i] + noise.iq[i];
        clean.iq[i] += noise.iq[i];
    }
    struct Result {
        FineProgress work;
        ReceiverSlotScore score;
        double wall_seconds;
    };
    const auto run = [](const Fixture& f, ReceiverConfig config, unsigned workers = 1, bool prefix = false,
                        AffineGainEvaluation evaluation = AffineGainEvaluation::operational) {
        const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true, FineLanes::fixed_and_adaptive};
        const auto memory = fine_workspace(config, f.slot, extent, workers);
        const auto start = std::chrono::steady_clock::now();
        FineSession session(config, f.slot, f.key, extent, f.iq, {}, f.coordinates,
                            100, 12, workers, memory, {}, nullptr, evaluation);
        if (prefix) session.advance_to(f.fs * 11);
        session.advance_to(extent.rows);
        ReceiverSlotScore score;
        session.finish(score);
        classify_fine_slot(score, AdmissionGrid(f.slot.arrival_lower_seconds,f.slot.arrival_upper_seconds,
            config.frequency_bound_hz,config.clock_bound_ppm,receiver_coarse_frequency_step(f.slot.format,f.fs)));
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return Result{session.progress(),score,seconds};
    };
    constexpr auto existing = AffineGainEvaluation::without_stationary_fading;
    const auto off = run(mixed, clean.config);
    const auto base = run(mixed, varying, 1, false, existing);
    const auto extended = run(mixed, varying);
    const auto split = run(mixed, varying, 2, true);
    check(!base.score.affine_stationary_fading_gain && !off.score.affine_stationary_fading_gain &&
          base.work.slow_gain_coherence_seconds == std::array<double, 2>{} &&
          base.score.bit_evidence == off.score.bit_evidence &&
          base.work.initial_affine.gaussian_updates == off.work.initial_affine.gaussian_updates,
          "the comparison must omit the additive bank and restore the original affine weights");
    check(extended.score.affine_stationary_fading_gain && split.score.affine_stationary_fading_gain &&
          extended.work.slow_gain_coherence_seconds == std::array<double, 2>{.5, 2.} &&
          extended.work.slow_gain_bit_evidence[1] > extended.work.slow_gain_bit_evidence[0] &&
          split.work.slow_gain_bit_evidence == extended.work.slow_gain_bit_evidence &&
          split.score.bit_evidence == extended.score.bit_evidence &&
          extended.score.decision == SlotDecision::data && extended.score.bit == 1,
          "short one-template DATA must score stationary and fading gains with checkpoint and worker parity");
    check(extended.work.workspace_bytes > off.work.workspace_bytes &&
          extended.work.initial_affine.basis_rows == base.work.initial_affine.basis_rows &&
          extended.work.initial_affine.gaussian_updates == base.work.initial_affine.gaussian_updates +
              extended.work.blocks * base.work.initial_affine.initial_states * 3 * 2 &&
          extended.work.adaptive.transition_edges == base.work.adaptive.transition_edges &&
          extended.work.adaptive.basis_rows == base.work.adaptive.basis_rows,
          "the additive gain must be charged and reuse the original basis without expanding adaptive work");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto expected = test_log_add(std::log(.75) + base.score.bit_evidence[bit],
            std::log(.25) + extended.work.slow_gain_bit_evidence[bit]);
        check(std::abs(expected - extended.score.bit_evidence[bit]) < 1e-10,
              "the additive gain alternatives must retain the fixed normalized affine mixture");
    }
    const auto clean_base = run(clean, varying, 1, false, existing);
    const auto clean_extended = run(clean, varying);
    const auto quiet_base = run(noise, varying, 1, false, existing);
    const auto quiet_extended = run(noise, varying);
    check(clean_base.score.decision == SlotDecision::data && clean_extended.score.decision == SlotDecision::data &&
          clean_base.score.bit == 1 && clean_extended.score.bit == 1,
          "the same public changing-channel bounds must preserve constant-channel DATA");
    check(quiet_base.score.decision == SlotDecision::quiet && quiet_extended.score.decision == SlotDecision::quiet &&
          quiet_base.score.noise_statistics_valid && quiet_extended.score.noise_statistics_valid &&
          quiet_base.score.quiet_energy == quiet_extended.score.quiet_energy,
          "the matched short noise observation must preserve scored physical quiet");

    auto two_taps = varying;
    two_taps.spread_delay_seconds = .007;
    auto two_taps_off = two_taps;
    two_taps_off.time_varying_channel = false;
    const auto wide_off = run(mixed, two_taps_off, 2, true);
    const auto wide_original = run(mixed, two_taps, 1, false, existing);
    const auto wide_extended = run(mixed, two_taps, 2, true);
    const auto wide_noise = run(noise, two_taps);
    check(wide_extended.work.tap_count == 2 && wide_extended.score.affine_stationary_fading_gain &&
          wide_extended.score.zero_h0_verified && wide_extended.work.slow_gain_bit_evidence == extended.work.slow_gain_bit_evidence,
          "both two-tap direct statistics must give the same additive evidence as the one-tap reference");
    check(!wide_off.score.affine_stationary_fading_gain && !wide_original.score.affine_stationary_fading_gain &&
          wide_off.score.bit_evidence == wide_original.score.bit_evidence &&
          wide_off.work.initial_affine.gaussian_updates == wide_original.work.initial_affine.gaussian_updates,
          "the fixed two-tap original bank must agree with the explicit excluded-family comparator");
    check(wide_extended.work.workspace_bytes > wide_off.work.workspace_bytes &&
          wide_extended.work.initial_affine.basis_rows == wide_original.work.initial_affine.basis_rows &&
          wide_extended.work.initial_affine.basis_calls == wide_original.work.initial_affine.basis_calls &&
          wide_extended.work.initial_affine.gaussian_updates == wide_original.work.initial_affine.gaussian_updates +
              wide_extended.work.blocks * wide_extended.work.initial_affine.initial_states * 3 * 2 &&
          wide_extended.work.adaptive.transition_edges == wide_original.work.adaptive.transition_edges &&
          wide_extended.work.adaptive.basis_rows == wide_original.work.adaptive.basis_rows,
          "two-tap additive gain must reserve its posterior state and charge its updates without extra basis work");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto expected = test_log_add(std::log(.75) + wide_original.score.bit_evidence[bit],
            std::log(.25) + extended.work.slow_gain_bit_evidence[bit]);
        check(std::abs(expected - wide_extended.score.bit_evidence[bit]) < 1e-10,
              "the original two-tap bank must retain its fixed weight beside the direct-template additive model");
    }
    check(wide_noise.score.decision == SlotDecision::quiet && wide_noise.score.noise_statistics_valid &&
          wide_noise.score.quiet_energy == quiet_extended.score.quiet_energy,
          "the direct additive family in a two-tap aperture must preserve observed quiet");
    const auto full_score = [&](const ReceiverConfig& config) {
        constexpr unsigned workers = 2;
        return score_receiver_slot(config,mixed.slot,mixed.key,mixed.iq,{},workers,
            receiver_slot_overlap_workspace(config,mixed.slot,workers));
    };
    const auto full_single = full_score(varying);
    const auto full_double = full_score(two_taps);
    check(full_single.complete && full_double.complete && full_single.all_valid && full_double.all_valid &&
          full_single.affine_stationary_fading_gain && full_double.affine_stationary_fading_gain &&
          full_single.decision == SlotDecision::data && full_double.decision == SlotDecision::data &&
          full_single.bit == 1 && full_double.bit == 1,
          "the same short changing single-path DATA must pass when the public geometry declares a possible echo");
    std::cout << "short stationary fading: DATA " << duration << " s; mixed log-e "
              << base.score.operational_log_e << " -> " << extended.score.operational_log_e
              << "; clean " << clean_base.score.operational_log_e << " -> " << clean_extended.score.operational_log_e
              << "; quiet " << quiet_base.score.operational_log_e << " -> " << quiet_extended.score.operational_log_e
              << "; mixed fine seconds " << base.wall_seconds << " -> " << extended.wall_seconds
              << "; Gaussian updates " << base.work.initial_affine.gaussian_updates << " -> "
              << extended.work.initial_affine.gaussian_updates
              << "; added admitted bytes " << extended.work.workspace_bytes - off.work.workspace_bytes
              << "; complete scores without/with declared 7 ms spread "
              << full_single.operational_log_e << " / " << full_double.operational_log_e << '\n';
}

void gaussian_update_matches_dense_complex_covariance(double coherence = 3.7, bool stationary_fading = false) {
    using C = std::complex<double>;
    constexpr std::size_t count = 4;
    constexpr double variance = .7;
    const double gain_power = stationary_fading ? 1. : .7;
    const std::array<double, count> times{0., .25, .5, .9};
    const std::array<C, count> basis{{{1., 0.}, {}, {.8, .25}, {-.6, .4}}};
    std::array<C, count> observation{{{.8, -.2}, {}, {-.3, .7}, {.2, .1}}};
    if (stationary_fading)
        for (std::size_t i = 0; i < count; ++i)
            observation[i] += basis[i] * (C{.45, -.3} + std::polar(.65, .38 + .7 * times[i]));

    // Dense H1 output covariance over observed blocks {0,2,3}. Block 1 is
    // erased. Its time remains in the AR recurrence: the gap from observed
    // block 0 to block 2 is .5 s, followed by the merged .4 s terminal step.
    constexpr std::array<std::size_t, 3> observed{0, 2, 3};
    std::array<std::array<C, 3>, 3> lower{};
    const auto covariance_at = [&](std::size_t row, std::size_t col) {
        const auto i = observed[row], j = observed[col];
        const auto separation = std::abs(times[i] - times[j]);
        const auto fading = std::exp(-separation / coherence);
        const auto correlation = stationary_fading ? .5 + .5 * fading : fading;
        const auto channel = gain_power * correlation * basis[i] * std::conj(basis[j]);
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

    const auto dimensions = stationary_fading ? 2U : 1U;
    const auto covariance_size = dimensions * dimensions;
    const std::array<double, 2> power{gain_power / dimensions, gain_power / dimensions};
    std::array<C, 3 * 2> means{};
    std::array<C, 3 * 4> posterior{};
    for (std::size_t hypothesis = 0; hypothesis < 3; ++hypothesis) {
        const auto start = hypothesis * covariance_size;
        for (std::size_t component = 0; component < dimensions; ++component)
            posterior[start + component * (dimensions + 1)] = power[component];
        if (stationary_fading) {
            posterior[start] += power[1];
            posterior[start + 1] = posterior[start + 2] = power[1];
        }
    }
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
        const auto update = stationary_fading ? triad_gaussian_additive_update_v1 : triad_gaussian_update_v1;
        const auto status = update(1, 1, 1, blocks, 0, 1,
            reinterpret_cast<const double*>(block_correlations.data()),
            reinterpret_cast<const double*>(block_grams.data()), power.data(), transitions.data(),
            reinterpret_cast<double*>(means.data()), reinterpret_cast<double*>(posterior.data()),
            scores.data(), diagnostics.data());
        check(status == 0, "Gaussian update kernel must accept the fixed finite gain likelihood");
    }
    check(scores[0] == 0. && scores[1] == scores[2] &&
          std::abs(scores[1] - dense_log_likelihood_ratio) < 2e-12,
          "block Gaussian updates must equal the dense complex covariance likelihood across an erased block and merged tail");
    if (stationary_fading)
        check(std::abs(posterior[5]) > .01 && posterior[5] == std::conj(posterior[6]),
              "additive gain prediction and observation must retain the correlated posterior");
}

void additive_gain_preserves_large_gram_precision() {
    constexpr double gram = 1e18, coherence = .5, tiny_elapsed = 1e-19;
    const Complex measured_gain{1., 1.};
    const std::array<double, 2> power{.5, .5};
    std::array<Complex, 3 * 2> means{};
    std::array<Complex, 3 * 4> posterior{1., .5, .5, .5, 1., .5, .5, .5, 1., .5, .5, .5};
    std::array<double, 3> scores{};
    std::array<double, 2> diagnostics{std::numeric_limits<double>::infinity(), 0.};
    const auto update = [&](double elapsed, bool observed) {
        const auto exponent = -elapsed / coherence;
        const auto decay = std::exp(exponent);
        const std::array transition{decay * decay, -std::expm1(2 * exponent), decay, 0.};
        const auto energy = observed ? gram : 0.;
        const std::array<Complex, 3> r{0., energy * measured_gain, 0.};
        const std::array<Complex, 3> g{0., energy, energy};
        const auto status = triad_gaussian_additive_update_v1(1, 1, 1, 1, 0, 1,
            reinterpret_cast<const double*>(r.data()), reinterpret_cast<const double*>(g.data()),
            power.data(), transition.data(), reinterpret_cast<double*>(means.data()),
            reinterpret_cast<double*>(posterior.data()), scores.data(), diagnostics.data());
        check(status == 0 && scores[0] == 0.,
              "large rank-one observations must preserve a finite likelihood and exact zero null");
    };

    // Both observations measure the same latent gain. The scalar posterior
    // therefore has total Gram 2G without a transition between observations.
    update(0., true);
    update(0., true);
    const auto summed_gram = 2 * gram;
    const auto expected_variance = 1. / (1. + summed_gram);
    const auto expected_mean = (summed_gram * expected_variance) * measured_gain;
    const auto expected_evidence = summed_gram * std::norm(measured_gain) *
        (summed_gram * expected_variance) - std::log1p(summed_gram);
    check(std::abs(means[2] - expected_mean) < 1e-14 &&
          std::abs(means[3] - .5 * expected_mean) < 1e-14 &&
          std::abs(scores[1] / expected_evidence - 1.) < 1e-14 &&
          std::abs(scores[2] + std::log1p(summed_gram)) < 1e-12 &&
          std::abs(posterior[4].real() / expected_variance - 1.) < 1e-14 &&
          std::abs(2. * posterior[5].real() / expected_variance - 1.) < 1e-14,
          "repeated large-Gram observations must preserve means, small covariance and the log determinant");

    // An erased block still evolves the gain. The new innovation is
    // measurable against the small posterior even though rho rounds to one.
    const auto preserved_scores = scores;
    update(tiny_elapsed, false);
    const auto difference = std::expm1(-tiny_elapsed / coherence);
    const auto innovation = -.5 * std::expm1(-2 * tiny_elapsed / coherence);
    const auto predicted_variance = expected_variance + difference * expected_variance +
        difference * difference * (.25 + .25 * expected_variance) + innovation;
    check(scores == preserved_scores &&
          std::abs(posterior[8].real() / predicted_variance - 1.) < 1e-14,
          "a tiny positive erased interval must advance the observable covariance without evidence");
    update(0., true);
    const auto increment = -std::log1p(gram * predicted_variance);
    const auto final_variance = predicted_variance / (1. + gram * predicted_variance);
    check(std::abs(scores[2] - (preserved_scores[2] + increment)) < 1e-12 &&
          std::abs(posterior[8].real() / final_variance - 1.) < 1e-14 &&
          posterior[8].real() > 0. && posterior[11].real() > 0.,
          "small positive predicted variance must affect later normalized evidence and posterior");
}

void two_tap_factor_preserves_large_gram_precision() {
    const auto gram = std::ldexp(1., 60);
    const double root_prior = std::sqrt(.5);
    const std::array<double, 2> power{.5, .5};
    const std::array<double, 4> no_elapsed{1., 0., 1., 0.};
    for (const auto phase : {Complex{1.}, Complex{0.,1.}}) {
        std::array<Complex, 6> means{};
        std::array<Complex, 12> factors{};
        for (const auto start : {0,4,8}) factors[start] = factors[start+3] = root_prior;
        const auto update = [&](Complex correlation, const std::array<double, 4>& transition,
                                unsigned rank, double echo = 0.) {
            std::array<Complex, 6> r{};
            std::array<Complex, 12> g{};
            std::array<double, 3> scores{};
            std::array<double, 2> diagnostic{};
            if (rank) {
                r[2] = correlation;
                r[3] = std::conj(phase) * correlation;
                g[4] = gram; g[5] = gram * phase; g[6] = std::conj(g[5]);
                g[7] = echo ? echo : gram;
            }
            const auto status = triad_gaussian_factor2_update_v1(1,1,1,0,1,
                reinterpret_cast<const double*>(r.data()), reinterpret_cast<const double*>(g.data()), &rank,
                power.data(), transition.data(), reinterpret_cast<double*>(means.data()),
                reinterpret_cast<double*>(factors.data()), scores.data(), diagnostic.data());
            check(status == 0 && scores[0] == 0. && scores[2] == 0.,
                  "high-Gram two-tap update must complete and preserve the exact common null");
            return scores[1];
        };
        const auto observed_variance = [&] {
            return std::norm(factors[4] + phase * factors[6]) +
                   std::norm(factors[5] + phase * factors[7]);
        };
        const auto first = update({}, no_elapsed, 2);
        const auto second = update({}, no_elapsed, 2);
        check(std::abs(first + std::log1p(gram)) < 1e-12 &&
              std::abs(second + std::log(2.)) < 1e-12 &&
              std::abs(observed_variance() * (1. + 2. * gram) - 1.) < 1e-12,
              "persistent factors must retain the thin direction across a split high-Gram update");
        const auto unobserved = std::norm(factors[4] - phase * factors[6]) +
                                std::norm(factors[5] - phase * factors[7]);
        check(std::abs(unobserved - 1.) < 1e-12, "rank-one observations must preserve the orthogonal gain");

        const auto variance = observed_variance();
        constexpr auto tiny = 1e-19;
        const std::array<double, 4> elapsed{1., -std::expm1(-2. * tiny), 1., 0.};
        check(update({}, elapsed, 0) == 0., "an erased block must predict the factor without adding evidence");
        const auto predicted = variance + elapsed[1];
        check(std::abs(update({}, no_elapsed, 2) + std::log1p(gram * predicted)) < 1e-12,
              "the preserved thin factor and tiny process innovation must both affect the next evidence");

        factors.fill({});
        for (const auto start : {0,4,8}) factors[start] = factors[start+3] = root_prior;
        means.fill({});
        const Complex wanted{1., 1.};
        const auto fitted = update(gram * wanted, no_elapsed, 2);
        check(std::abs(means[2] - .5 * wanted) < 1e-13 &&
              std::abs(means[3] - .5 * std::conj(phase) * wanted) < 1e-13 &&
              std::abs(fitted / (gram * std::norm(wanted)) - 1.) < 1e-13,
              "large correlations must retain the two posterior means and normalized wanted evidence");
        const std::array<double, 4> refreshed{.25, .75, .5, 0.};
        const auto refreshed_variance = .75 + .25 / (1. + gram);
        const auto expected = -std::log1p(gram * refreshed_variance)
            - (gram / (1. + gram * refreshed_variance)) * std::norm(.5 * wanted);
        check(std::abs(update({}, refreshed, 2) - expected) < 1e-12,
              "a strong nonzero predicted mean must not lose the cancellation-free innovation exponent");

        const auto near_collinear = std::nextafter(gram, std::numeric_limits<double>::infinity());
        const auto near_score = [&](unsigned physical_rank) {
            factors.fill({}); means.fill({});
            for (const auto start : {0,4,8}) factors[start] = factors[start+3] = root_prior;
            return update({}, no_elapsed, physical_rank, near_collinear);
        };
        const auto true_det = 1. + .5 * (gram + near_collinear) + .25 * gram * (near_collinear - gram);
        check(std::abs(near_score(2) + std::log(true_det)) < 1e-12 &&
              std::abs(near_score(1) + std::log1p(gram)) < 1e-12,
              "a small represented positive mode must be retained unless projection physically limits rank");
    }
    const auto naive = 1. + gram - std::norm(Complex{gram / std::sqrt(1. + gram)});
    check(naive == 0. && std::abs(gaussian_positive_second_pivot(gram, gram, gram, 1.) - 2.) < 1e-14,
          "the existing subtractive stateless pivot is zero while the positive formula retains both identity terms");
}

void projected_rank_one_preserves_phased_orthogonality() {
    const auto gram = std::ldexp(1., 60);
    const auto echo = std::nextafter(1., 2.);
    const auto logdet = std::log1p(2. * gram);
    // Independent scalar one-bin model: project (1, echo) onto (1,1)/sqrt(2)
    // before integrating the unit-power Gaussian gain.
    const long double parallel = 1.L + static_cast<long double>(echo);
    const auto expected = static_cast<double>(parallel * parallel * gram * gram /
        (2.L * (1.L + 2.L * gram)) - logdet);
    const auto ulp = std::nextafter(expected, std::numeric_limits<double>::infinity()) - expected;
    for (const auto phase : {Complex{1.}, Complex{0.,1.}}) {
        const auto one_ulp = gaussian_rank_one_log_likelihood(1.,1.,phase,
            Complex{1.}, std::conj(phase) * echo, gram, gram);
        const auto orthogonal = gaussian_rank_one_log_likelihood(1.,1.,phase,
            Complex{1.}, -std::conj(phase), gram, gram);
        check(std::abs(one_ulp - expected) < 8. * ulp && std::abs(orthogonal + logdet) < 1e-12,
              "the known projected rank must ignore even amplified correlation orthogonal to the phased template");
    }
    check(gaussian_rank_one_log_likelihood(0.,0.,{}, {}, {}, gram, gram) == 0.,
          "an empty physical template span must preserve exact zero evidence");
}

void two_tap_factor_matches_moderate_dense_likelihood() {
    using C = Complex;
    const std::array<C, 6> r{{{}, {}, {.4,-1.}, {-.2,.5}, {}, {}}};
    const C cross{.3,.7};
    const std::array<C, 12> g{{{}, {}, {}, {}, 2., cross, std::conj(cross), 1.5, {}, {}, {}, {}}};
    const std::array<double, 2> power{.5,.5};
    const std::array<double, 4> transition{1.,0.,1.,0.};
    const auto determinant = 4. * 3.5 - std::norm(cross);
    const auto m0 = (3.5 * r[2] - cross * r[3]) / determinant;
    const auto m1 = (4. * r[3] - std::conj(cross) * r[2]) / determinant;
    const auto expected = (std::conj(r[2]) * m0 + std::conj(r[3]) * m1).real()
        - std::log(determinant / 4.);
    for (const auto native_factor : {false,true}) {
        std::array<C, 6> means{};
        std::array<C, 12> state{};
        std::array<double, 3> scores{};
        std::array<double, 2> diagnostic{};
        const auto diagonal = native_factor ? std::sqrt(.5) : .5;
        for (const auto start : {0,4,8}) state[start] = state[start+3] = diagonal;
        const auto input_r = reinterpret_cast<const double*>(r.data());
        const auto input_g = reinterpret_cast<const double*>(g.data());
        const auto output_m = reinterpret_cast<double*>(means.data());
        const auto output_p = reinterpret_cast<double*>(state.data());
        const unsigned rank = 2;
        const auto status = native_factor ? triad_gaussian_factor2_update_v1(1,1,1,0,1,input_r,input_g,
            &rank,power.data(),transition.data(),output_m,output_p,scores.data(),diagnostic.data()) :
            triad_gaussian_update_v1(2,1,1,1,0,1,input_r,input_g,
                power.data(),transition.data(),output_m,output_p,scores.data(),diagnostic.data());
        check(status == 0 && scores[0] == 0. && scores[2] == 0. &&
              std::abs(scores[1] - expected) < 1e-13 &&
              std::abs(means[2] - m0) < 1e-13 && std::abs(means[3] - m1) < 1e-13,
              "native factor and legacy dense interfaces must agree with a separate full-rank proper-complex solve");
    }
}

// The receiver's entries skip a provably inert common null. Signal
// hypotheses, statuses and the zero null evidence must equal the exported
// entries bit for bit, and any input outside the proof runs the null.
// Inside the proof the null must actually be skipped: its mean and prior
// stay as passed while the exported entry still predicts them.
void inert_null_skip_matches_exported_updates() {
    using C = Complex;
    std::mt19937_64 random(20261120);
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> unit(0., 1.);
    const auto same = [](const auto& a, const auto& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](double x, double y) {
            return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
        });
    };
    const auto values = [](const std::vector<C>& data, std::size_t first, std::size_t count) {
        return std::span(reinterpret_cast<const double*>(data.data() + first), 2 * count);
    };
    std::size_t skipped_trials = 0, predicted_nulls = 0;
    for (std::size_t trial = 0; trial < 400; ++trial) {
        const bool factor = trial % 2;
        const std::size_t taps = factor ? 2 : 1, models = 1 + trial % 5, blocks = 1 + trial / 2 % 2;
        // Every fourth trial perturbs one input outside the inert proof.
        const auto outside = trial % 4 == 3 ? trial / 4 % 7 : 7;
        std::vector<C> r(blocks * 3 * taps), g(blocks * 3 * taps * taps);
        for (std::size_t block = 0; block < blocks; ++block) {
            for (std::size_t h = 1; h < 3; ++h) {
                std::vector<C> basis(2 * taps);
                for (auto& value : basis) value = {normal(random), normal(random)};
                for (std::size_t row = 0; row < taps; ++row) {
                    r[(block * 3 + h) * taps + row] = {normal(random) * 3, normal(random) * 3};
                    for (std::size_t col = 0; col < taps; ++col)
                        g[((block * 3 + h) * taps + row) * taps + col] = (trial % 8 == 5 && row != col) ? C{} :
                            std::conj(basis[2 * row]) * basis[2 * col] +
                            std::conj(basis[2 * row + 1]) * basis[2 * col + 1];
                }
            }
        }
        std::vector<double> transitions(4 * blocks * models);
        for (std::size_t i = 0; i < blocks * models; ++i) {
            const auto decay = trial % 16 == 1 ? 1. : std::exp(-unit(random) * 3);
            transitions[4 * i] = decay * decay;
            transitions[4 * i + 1] = -std::expm1(2 * std::log(decay));
            transitions[4 * i + 2] = decay;
        }
        std::array<double, 2> power{1. / static_cast<double>(taps), 1. / static_cast<double>(taps)};
        std::vector<C> means(models * 3 * taps), state(models * 3 * taps * taps);
        std::vector<double> scores(models * 3), diagnostics(2 * models);
        for (std::size_t model = 0; model < models; ++model) {
            for (std::size_t h = 0; h < 3; ++h) {
                for (std::size_t tap = 0; tap < taps; ++tap) {
                    state[(model * 3 + h) * taps * taps + tap * taps + tap] =
                        taps == 2 ? std::sqrt(power[tap]) : power[tap];
                    if (h) means[(model * 3 + h) * taps + tap] = {normal(random), normal(random)};
                }
                if (h) scores[model * 3 + h] = normal(random);
            }
        }
        switch (outside) {
        case 0: r[taps - 1] = {1e-300, 0.}; break;
        case 1: g[0] = -1e-300; break;
        case 2: means[0] = {0., 1e-300}; break;
        case 3: transitions[1] = 1. + 1e-15; break;
        case 4: transitions[2] = std::numeric_limits<double>::infinity(); break;
        case 5: scores[0] = -0.; break;
        case 6: state[0] = taps == 2 ? C{1e200, 0.} : C{-1e-300, 0.}; break;
        default: break;
        }
        std::vector<unsigned> ranks(blocks);
        for (auto& rank : ranks) rank = static_cast<unsigned>(trial % 3);
        const auto run = [&](bool receiver) {
            auto m = means, p = state;
            auto score = scores, diagnostic = diagnostics;
            bool skipped = false;
            const auto in_r = reinterpret_cast<const double*>(r.data());
            const auto in_g = reinterpret_cast<const double*>(g.data());
            const auto out_m = reinterpret_cast<double*>(m.data());
            const auto out_p = reinterpret_cast<double*>(p.data());
            int status = 0;
            if (factor) {
                status = receiver ?
                    gaussian_signal_factor2_update(1, models, blocks, 0, 1, in_r, in_g, ranks.data(), power.data(),
                        transitions.data(), out_m, out_p, score.data(), diagnostic.data(), skipped) :
                    triad_gaussian_factor2_update_v1(1, models, blocks, 0, 1, in_r, in_g, ranks.data(), power.data(),
                        transitions.data(), out_m, out_p, score.data(), diagnostic.data());
            } else {
                status = receiver ?
                    gaussian_signal_update(1, 1, models, blocks, 0, 1, in_r, in_g, power.data(), transitions.data(),
                        out_m, out_p, score.data(), diagnostic.data(), skipped) :
                    triad_gaussian_update_v1(1, 1, models, blocks, 0, 1, in_r, in_g, power.data(), transitions.data(),
                        out_m, out_p, score.data(), diagnostic.data());
            }
            return std::tuple{status, m, p, score, diagnostic, skipped};
        };
        const auto [status, m, p, score, diagnostic, exported_skipped] = run(false);
        const auto [skip_status, skip_m, skip_p, skip_score, skip_diagnostic, skipped] = run(true);
        check(status == skip_status && !exported_skipped, "inert-null entries changed a Gaussian update status");
        if (status) continue;
        // Every perturbation lies outside the proof; every other trial inside.
        check(skipped == (outside == 7), "the inert-null proof accepted or rejected the wrong inputs");
        skipped_trials += skipped;
        for (std::size_t model = 0; model < models; ++model) {
            if (skipped) {
                const auto at = model * 3;
                check(same(values(skip_m, at * taps, taps), values(means, at * taps, taps)) &&
                      same(values(skip_p, at * taps * taps, taps * taps), values(state, at * taps * taps, taps * taps)),
                      "a skipped common null must leave its mean and prior as passed");
                predicted_nulls += !same(values(p, at * taps * taps, taps * taps),
                                         values(state, at * taps * taps, taps * taps));
            }
            for (std::size_t h = outside == 7 ? 1 : 0; h < 3; ++h) {
                const auto at = model * 3 + h;
                check(same(values(m, at * taps, taps), values(skip_m, at * taps, taps)) &&
                      same(values(p, at * taps * taps, taps * taps), values(skip_p, at * taps * taps, taps * taps)) &&
                      same(std::span(score).subspan(at, 1), std::span(skip_score).subspan(at, 1)),
                      "inert-null entries changed a hypothesis posterior or its evidence");
            }
            check(outside != 7 || (score[model * 3] == 0. && skip_score[model * 3] == 0. &&
                                   !std::signbit(skip_score[model * 3])),
                  "an inert common null must keep exactly zero evidence");
        }
        if (outside != 7) check(same(diagnostic, skip_diagnostic), "a null outside the proof must run in full");
    }
    // The exported entry advances these nulls, so equal signal results
    // above are not an artefact of an unexercised skip.
    check(skipped_trials > 0 && predicted_nulls > 0, "the inert-null skip was not exercised");
}

void two_tap_native_dc_projection_preserves_complete_scores() {
    const Fixture f(120, 3, true);
    auto config = f.config;
    config.spread_delay_seconds = .013;
    config.noise_variance = std::ldexp(1., -62);
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = .04;
    const FineObservation extent{f.slot.first_sample,f.iq.size(),FineRunKind::operational,true, FineLanes::fixed_and_adaptive};
    const auto run = [&](unsigned workers, bool prefix) {
        const auto memory = fine_workspace(config,f.slot,extent,workers);
        FineSession session(config,f.slot,f.key,extent,f.iq,{},f.coordinates,100,12,workers,memory);
        if (prefix) session.advance_to(f.fs * 2);
        session.advance_to(extent.rows);
        ReceiverSlotScore score;
        session.finish(score);
        const auto progress = session.progress();
        check(score.complete && score.all_valid && score.zero_h0_verified &&
              progress.tap_count == 2 && progress.observed_projection_modes == progress.blocks &&
              progress.adaptive.transition_edges > 0 && progress.slow_gain_coherence_seconds[0] > 0 &&
              std::ranges::all_of(score.bit_evidence, [](double value) { return std::isfinite(value); }),
              "the actual two-tap receiver must retain finite fixed/adaptive/slow gain evidence in a DC-only projection");
        return score.bit_evidence;
    };
    check(run(1,false) == run(2,true),
          "native two-tap DC-only evidence must preserve prefix and worker partitioning");
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

void independent_gain_keeps_full_public_prior_and_bounded_work() {
    const Fixture f(480);
    auto config = f.config;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 12;
    config.spread_delay_seconds = .004;
    config.spread_doppler_hz = 12;
    config.clock_bound_ppm = 0;
    auto slot = f.slot;
    slot.arrival_lower_seconds = -.004;
    slot.arrival_upper_seconds = .004;
    const auto plan = varying_fine_plan(config, slot, 2);
    const auto narrow = std::array{f.coordinates[0], FineCoordinate{2, .001, 0, 0}, FineCoordinate{-2, -.001, 0, 0}};
    check(plan.track_frequency && plan.nominal_independent_gain_candidates == 2 &&
          config.random_doppler_width_hz == 0,
          "nominal spatial gain must support public relative-spread declarations without a diffuse-width field");

    ReceiverExecutor executor(2);
    const auto run = [&](const ReceiverSlotPlan& slot, const VaryingFinePlan& shape, std::span<const Complex> iq,
                         double gain_multiplier = 1.) {
        auto scaled = config;
        scaled.data_basis_gain *= gain_multiplier;
        ReceiverSlotScore result;
        score_varying_fine(scaled, slot, f.key, shape, iq, narrow, {}, 100, 12, 2, result, {}, executor);
        return result;
    };
    auto original = plan;
    original.nominal_independent_gain_candidates = 0;
    const auto old = run(slot, original, f.iq);
    const auto added = run(slot, plan, f.iq);
    const auto raised = run(slot, plan, f.iq, 16.);
    check(added.nominal_independent_gain_candidates == 2 &&
          added.nominal_independent_gain_frequency_states > 1 && added.nominal_independent_gain_frequency_states <= 65 &&
          added.nominal_independent_gain_basis_rows == 8 * f.iq.size() &&
          added.varying_basis_rows == old.varying_basis_rows + added.nominal_independent_gain_basis_rows &&
          added.varying_candidates == old.varying_candidates &&
          added.varying_blocks > 1 &&
          old.independent_gain_emission_cells == 0 &&
          added.independent_gain_emission_cells == 2 * 4 * 2 * added.nominal_independent_gain_frequency_states * added.varying_blocks &&
          raised.independent_gain_emission_cells == added.independent_gain_emission_cells &&
          raised.varying_basis_rows == added.varying_basis_rows &&
          added.fixed_varying_bit_evidence == old.fixed_varying_bit_evidence &&
          added.tracked_frequency_bit_evidence == old.tracked_frequency_bit_evidence,
          "the nominal spatial family preserves the previous evidence and charges exactly two mapped pairs");
    const auto single = [&](double arrival) {
        auto collapsed = slot;
        collapsed.arrival_lower_seconds = collapsed.arrival_upper_seconds = arrival;
        const auto shape = varying_fine_plan(config, collapsed, 2);
        check(shape.nominal_independent_gain_candidates == 1 && shape.workspace_bytes < plan.workspace_bytes,
              "a collapsed arrival axis must quote and score exactly one independent atom");
        return run(collapsed, shape, f.iq);
    };
    const auto first = single(slot.arrival_lower_seconds);
    const auto second = single(slot.arrival_upper_seconds);
    const auto log_add = [](double a, double b) { return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b))); };
    const AdmissionGrid grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds, 12, 0, .01);
    const double caller_prior = std::log(static_cast<double>(narrow.size())) - grid.log_count();
    check(added.nominal_independent_gain_log_prior_adjustment == std::log1p(2 * grid.frequency_radius),
          "the separate finite frequency family restores only the original frequency divisor");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto& by_power = added.nominal_independent_gain_power_bit_evidence;
        const auto power_ceiling = std::max({by_power[0][bit], by_power[1][bit], by_power[2][bit], by_power[3][bit]});
        const auto expected_power = power_ceiling + std::log(.75 * std::exp(by_power[0][bit] - power_ceiling) +
            (std::exp(by_power[1][bit] - power_ceiling) + std::exp(by_power[2][bit] - power_ceiling) +
             std::exp(by_power[3][bit] - power_ceiling)) / 12.);
        check(std::abs(by_power[0][bit] - by_power[3][bit]) > .01 &&
              std::abs(added.nominal_independent_gain_bit_evidence[bit] - expected_power) < 2e-10 &&
              std::abs(raised.nominal_independent_gain_power_bit_evidence[2][bit] - by_power[0][bit]) < 2e-10,
              "each power must be fixed across the aperture; a 16x reference amplitude and 1/256 power cancel");
        const double left = first.nominal_independent_gain_bit_evidence[bit] + caller_prior;
        const double right = second.nominal_independent_gain_bit_evidence[bit] + caller_prior;
        const double retained = old.varying_bit_evidence[bit] + caller_prior;
        const double reference = std::max({left, right, retained});
        const double expected = reference + std::log(.875 * std::exp(retained - reference) +
            .125 * (std::exp(left - reference) + std::exp(right - reference)));
        check(std::abs(added.nominal_independent_gain_bit_evidence[bit] + caller_prior - log_add(left, right)) < 2e-10 &&
              std::abs(added.nominal_independent_gain_bit_evidence[bit] - log_add(
                  added.nominal_independent_fixed_centroid_bit_evidence[bit],
                  added.nominal_independent_moving_centroid_bit_evidence[bit]) + std::numbers::ln2) < 2e-10 &&
              std::abs(added.varying_bit_evidence[bit] + caller_prior - expected) < 2e-10,
              "each new public atom must retain its full-grid mass and the old model its fixed retained weight");
    }
    const std::vector<Complex> zero(f.iq.size());
    const auto null = run(slot, plan, zero);
    const auto null_adjustment = null.nominal_independent_gain_log_prior_adjustment + std::log(2. / narrow.size());
    check(std::ranges::all_of(null.nominal_independent_gain_bit_evidence,
          [&](double value) { return value < null_adjustment; }),
          "independent gain must charge the Gaussian determinant under zero input, with no fitted noise power");
}

void signed_gain_retains_full_unselected_mass() {
    const Fixture f(480);
    auto config = f.config;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 12;
    config.spread_delay_seconds = .004;
    config.spread_doppler_hz = 12;
    // On zero IQ a very large receiver variance makes every component
    // likelihood unity to working precision, isolating the prior weights.
    config.noise_variance = 1e200;
    auto slot = f.slot;
    slot.arrival_lower_seconds = -.01;
    slot.arrival_upper_seconds = .01;
    auto plan = varying_fine_plan(config, slot, 2);
    check(plan.track_frequency && plan.nominal_independent_gain_candidates == 2,
          "signed timing fixture requires the native independent-gain route");
    enable_midpoint_tracking(plan);
    const std::vector<Complex> zero(f.iq.size());
    ReceiverExecutor executor(2);
    constexpr std::size_t lags = 3;
    constexpr long double midpoint = 26.5L;
    const AdmissionGrid grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds,
        config.time_varying_atmospheric_hz, config.clock_bound_ppm,
        std::min(.01, receiver_coarse_frequency_step(slot.format, config.sample_rate_hz)));
    const auto timing_log_count = grid.log_count() - std::log1p(2 * grid.frequency_radius);
    const auto run = [&](bool admit_gain, std::span<const std::int64_t> choices) {
        ReceiverSlotScore result;
        const MidpointTrackInput public_lags{{}, lags, timing_log_count, choices, admit_gain, 0, midpoint};
        score_varying_fine(config, slot, f.key, plan, zero, f.coordinates, {}, 100, 12, 2,
                          result, {}, executor, nullptr, nullptr, nullptr, &public_lags);
        return result;
    };
    const std::array<std::int64_t, 2> choices{0, 2};
    const auto joint = run(true, choices);
    const auto unavailable = run(false, {});
    const auto unselected = run(true, {});
    check(joint.signed_lag_gain_candidates == 2 &&
          joint.independent_gain_emission_cells == 2 * unselected.independent_gain_emission_cells &&
          joint.signed_lag_gain_basis_rows == 8 * zero.size() &&
          joint.nominal_independent_gain_bit_evidence == unselected.nominal_independent_gain_bit_evidence &&
          joint.nominal_independent_gain_bit_evidence == unavailable.nominal_independent_gain_bit_evidence &&
          !unselected.signed_lag_gain_candidates &&
          unselected.varying_bit_evidence == unavailable.varying_bit_evidence &&
          joint.varying_basis_rows == unselected.varying_basis_rows + joint.signed_lag_gain_basis_rows,
          "retained lag atoms must retain and charge the original and subselected families");
    const auto nominal_mass = 2 * std::exp(-timing_log_count);
    const auto remaining = 1 - nominal_mass;
    const auto caller = -grid.log_count();
    for (std::size_t bit = 0; bit < 2; ++bit) {
        check(std::abs(std::exp(joint.nominal_independent_gain_bit_evidence[bit] + caller) -
                       nominal_mass) < 2e-12 &&
              std::abs(std::exp(joint.signed_lag_gain_bit_evidence[bit] + caller) -
                       remaining * choices.size() / (receiver_epoch_offsets.size() * lags)) < 2e-12,
              "retained timing atoms must spend only their complete public finite-grid weights");
    }
    const std::array<std::int64_t, 2> repeated{0, 0};
    rejects([&] { (void)run(true, repeated); }, "duplicate signed lag identifiers must fail before scoring");
}

void projected_tracking_grid_preserves_the_rounded_edge() {
    const Fixture f(1920, 6. * 2 * 1920 / 181);
    auto config = f.config;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 509.17127071823205;
    config.spread_delay_seconds = .004;
    config.clock_bound_ppm = 0;
    const auto plan = varying_fine_plan(config, f.slot, 2);
    check(plan.block_rows == 181 && plan.track_frequency && plan.nominal_independent_gain_candidates == 1,
          "fractional projected tracking grid fixture must execute a nominal model over the complete aperture");
    const double bin_hz = f.fs / (2. * plan.block_rows);
    check(std::ceil(config.time_varying_atmospheric_hz / (32 * bin_hz)) == 3 &&
          std::ceil(config.time_varying_atmospheric_hz / (3 * bin_hz)) == 33,
          "projected tracking fixture must cross the original rounded state limit");
    ReceiverExecutor executor(2);
    ReceiverSlotScore result;
    score_varying_fine(config, f.slot, f.key, plan, f.iq, f.coordinates, {}, 100, 12, 2, result, {}, executor);
    const auto next_spacing = 4 * bin_hz;
    check(24 * next_spacing < config.time_varying_atmospheric_hz &&
          25 * next_spacing >= config.time_varying_atmospheric_hz &&
          result.nominal_independent_gain_frequency_states == 51 &&
          result.nominal_independent_gain_frequency_states <= 65 &&
          std::ranges::all_of(result.nominal_independent_gain_bit_evidence, [](double value) { return std::isfinite(value); }),
          "coarsened projection must score the declared frequency edge within the fixed state budget");
}

void added_epoch_track_keeps_prior_mass_and_work() {
    const Fixture f(480);
    auto config = f.config;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 12;
    config.spread_delay_seconds = .004;
    auto slot = f.slot;
    slot.arrival_lower_seconds = -.004;
    slot.arrival_upper_seconds = .004;
    const auto plan = varying_fine_plan(config, slot, 2);
    auto exact_clock = config;
    exact_clock.clock_bound_ppm = 0;
    const auto exact_plan = varying_fine_plan(exact_clock, slot, 2);
    check(plan.track_frequency && exact_plan.track_frequency &&
          plan.workspace_bytes > exact_plan.workspace_bytes &&
          nominal_epoch_timing_capacity(config, slot) == 8 && nominal_epoch_timing_capacity(exact_clock, slot) == 2,
          "raw track must reserve scratch for its public unresolved clock and arrival axes");
    std::vector<FineCoordinate> original, epoch;
    for (const auto clock : {-30., -10., 10., 30.})
        for (const auto arrival : {slot.arrival_lower_seconds, slot.arrival_upper_seconds}) {
            original.push_back({2, arrival, 0, clock});
            epoch.push_back({0, arrival, 0, clock});
        }
    const auto base = f.coordinates;
    auto all = original;
    all.insert(all.end(), epoch.begin(), epoch.end());
    ReceiverExecutor executor(2);
    const auto run = [&](std::span<const FineCoordinate> candidates, std::span<const FineCoordinate> timing) {
        ReceiverSlotScore score;
        score_varying_fine(config, slot, f.key, plan, f.iq, candidates, timing,
            100, 12, 2, score, {}, executor);
        return score;
    };
    const auto ordinary = run(base, original);
    const auto added = run(epoch, {});
    const auto complete = run(base, all);
    const auto old_count = static_cast<double>(base.size() + original.size());
    const auto extra_count = static_cast<double>(epoch.size());
    const auto total_count = old_count + extra_count;
    const auto log_add = [](double a, double b) { return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b))); };
    check(ordinary.fixed_varying_bit_evidence == complete.fixed_varying_bit_evidence &&
          complete.varying_basis_rows == ordinary.varying_basis_rows + 4 * epoch.size() * f.iq.size() &&
          complete.varying_candidates == ordinary.varying_candidates &&
          complete.varying_frequency_states == ordinary.varying_frequency_states,
          "new epoch tracking must preserve the original fixed branch and charge only its added raw basis rows");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto expected = log_add(ordinary.tracked_frequency_bit_evidence[bit] + std::log(old_count),
            added.tracked_frequency_bit_evidence[bit] + std::log(extra_count)) - std::log(total_count);
        check(std::abs(complete.tracked_frequency_bit_evidence[bit] - expected) < 2e-10 &&
              std::abs(complete.tracked_frequency_log_prior_adjustment - ordinary.tracked_frequency_log_prior_adjustment -
                  std::log(total_count / old_count)) < 1e-12,
              "expanded raw track may add original public atom weights without diluting older weights");
    }

    auto midpoint_plan = plan;
    enable_midpoint_tracking(midpoint_plan);
    check(midpoint_plan.workspace_bytes > plan.workspace_bytes,
          "midpoint production shape must charge its extra candidate state before inference");
    constexpr std::size_t public_lags = 282;
    const AdmissionGrid grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds,
        config.frequency_bound_hz + config.time_varying_atmospheric_hz, config.clock_bound_ppm,
        std::min(.01, receiver_coarse_frequency_step(slot.format, config.sample_rate_hz)));
    const auto log_original_timing = grid.log_count() - std::log1p(2 * grid.frequency_radius);
    const auto measure_midpoint = [&](std::span<const FineCoordinate> nominations) {
        ReceiverSlotScore observed;
        const MidpointTrackInput family{nominations, public_lags, log_original_timing};
        score_varying_fine(config, slot, f.key, midpoint_plan, f.iq, base, original,
            100, 12, 2, observed, {}, executor, nullptr, nullptr, nullptr, &family);
        return observed;
    };
    const std::array nominations{base.front(), epoch.front(), epoch.front()};
    const auto mixed = measure_midpoint(nominations);
    const auto distinct = measure_midpoint(std::span(nominations).first(2));
    const auto empty = measure_midpoint({});
    const auto shared_atom = run(std::span(nominations).first(1), {});
    const auto new_atom = run(std::span(nominations).subspan(1, 1), {});
    check(mixed.midpoint_timing_candidates == 2 && mixed.midpoint_timing_basis_rows == 4 * f.iq.size() &&
          mixed.varying_basis_rows == ordinary.varying_basis_rows + mixed.midpoint_timing_basis_rows &&
          mixed.varying_bit_evidence == distinct.varying_bit_evidence &&
          mixed.tracked_frequency_bit_evidence == ordinary.tracked_frequency_bit_evidence &&
          empty.midpoint_timing_candidates == 0 && empty.varying_basis_rows == ordinary.varying_basis_rows,
          "midpoint shares rendered coordinates across families and retains the exact original track evidence");
    const double caller = std::log(static_cast<double>(base.size())) - grid.log_count();
    const double full_midpoint_cells = static_cast<double>(receiver_epoch_offsets.size() * public_lags);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const double old_lr = ordinary.tracked_frequency_bit_evidence[bit] + std::log(old_count) - log_original_timing;
        const double shared_lr = shared_atom.tracked_frequency_bit_evidence[bit];
        const double new_lr = new_atom.tracked_frequency_bit_evidence[bit];
        const double fixed_lr = ordinary.fixed_varying_bit_evidence[bit] + caller;
        const double independent_lr = ordinary.nominal_independent_gain_bit_evidence[bit] + caller;
        const double scale = std::max({old_lr, shared_lr, new_lr, fixed_lr, independent_lr});
        const double old = std::exp(old_lr - scale);
        const double middle = (std::exp(shared_lr - scale) + std::exp(new_lr - scale)) / full_midpoint_cells;
        const double fixed = std::exp(fixed_lr - scale);
        const double independent = std::exp(independent_lr - scale);
        const double expected = scale + std::log(.875 * .5 * (fixed + .875 * old + .125 * middle) + .125 * independent);
        const double missing = scale + std::log(.875 * .5 * (fixed + .875 * old) + .125 * independent);
        check(std::abs(mixed.varying_bit_evidence[bit] + caller - expected) < 2e-10 &&
              std::abs(empty.varying_bit_evidence[bit] + caller - missing) < 2e-10,
              "receiver evidence must keep the whole public timing mass, including omitted midpoint candidates");
    }
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

void incomplete_bulk_cannot_publish_quiet(FineLanes lanes) {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true, lanes};
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

enum class ResidentAction {
    unavailable, execute, wrong_order, missing_mapping, missing_statistics, missing_work, fail_second
};

struct HostProjectionWitness {
    std::vector<std::size_t> batch_lanes;
    std::vector<std::size_t> resident_batch_blocks;
    std::size_t device_blocks = 0, native_blocks = 0, completed_blocks = 0;
    bool fail_second_batch = false;
    ResidentAction resident = ResidentAction::unavailable;
    // Public blocks of one resident call. More than two selects windows.
    std::size_t resident_capacity = 2;
};

class HostProjectionBulk final : public FineBulk {
public:
    static constexpr std::size_t capacity = 8;

    explicit HostProjectionBulk(std::shared_ptr<HostProjectionWitness> witness)
        : witness_(std::move(witness)) {}

    void statistics(const FineBulkBlock&, std::size_t, const FineMappingSource&,
                    std::span<FineBulkStatistics>, std::stop_token) override {
        throw Error("host projector must use the native template scheduler");
    }

    std::size_t host_projection_capacity() const noexcept override { return capacity; }
    bool host_projection_stages_peaks() const noexcept override { return true; }

    bool supports_resident_statistics(const FineBulkBlock& block) const noexcept override {
        return witness_->resident != ResidentAction::unavailable && block.rows == 60;
    }
    std::size_t resident_block_capacity() const noexcept override { return witness_->resident_capacity; }

    bool resident_statistics(std::span<const FineResidentBlock> blocks, std::size_t candidates,
                             std::stop_token) override {
        const auto action = witness_->resident;
        witness_->resident_batch_blocks.push_back(blocks.size());
        std::size_t modes = 0;
        check(blocks.size() <= witness_->resident_capacity, "a resident call exceeded its public block capacity");
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const auto& [block, source, output, tiles] = blocks[index];
            check(supports_resident_statistics(block) && output.size() == candidates && !tiles,
                  "resident packets must contain compatible public blocks");
            if (index == 1 && action == ResidentAction::fail_second)
                throw Error("injected second resident block failure");
            if (index == 1 && action == ResidentAction::wrong_order)
                source(1, 0, block.rows, [](auto, auto, auto) {});
            for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
                if (index == 1 && candidate + 1 == candidates && action == ResidentAction::missing_mapping) {
                    output[candidate] = {};
                    continue;
                }
                const auto statistics = mapped_statistics(block, source, candidate);
                if (index == 0 || candidate + 1 != candidates || action != ResidentAction::missing_statistics)
                    output[candidate] = statistics;
            }
            modes += candidates * 2 * block.taps * block.projection_indices.size();
        }
        witness_->completed_blocks += blocks.size();
        work_.executed = true;
        work_.statistics_blocks += (blocks.size() - (action == ResidentAction::missing_work)) * candidates;
        work_.projected_values += modes;
        return true;
    }

    bool begin_host_projection(const FineBulkBlock& block, std::stop_token) override {
        rows_ = block.rows;
        bins_ = block.projection_indices;
        if (rows_ == 60) {
            staging_.resize(capacity * (rows_ + 1));
            ++witness_->device_blocks;
            return true;
        }
        ++witness_->native_blocks;
        return false;
    }

    std::span<std::complex<float>> host_projection_staging(std::size_t samples) noexcept override {
        return std::span(staging_).first(samples);
    }

    void project_host_templates(std::span<const std::complex<float>> input,
                                std::span<std::complex<float>> output, std::stop_token) override {
        const auto stride = rows_ + 1;
        const auto lanes = input.size() / stride;
        check(lanes && lanes <= capacity && input.size() == lanes * stride &&
              output.size() == lanes * bins_.size(), "each host batch must contain complete templates and projections");
        check(input.data() == staging_.data(), "template workers must fill the owner-held staging directly");
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            float peak = 0;
            for (const auto value : input.subspan(lane * stride, rows_))
                peak = std::max({peak, std::abs(value.real()), std::abs(value.imag())});
            check(input[lane * stride + rows_] == std::complex<float>(peak, 0),
                  "template staging must include the actual magnitude peak");
        }
        witness_->batch_lanes.push_back(lanes);
        if (witness_->fail_second_batch && witness_->batch_lanes.size() == 2)
            throw Error("injected host projection failure");
        project(input, output, rows_, stride, lanes, bins_);
    }

    void complete_host_statistics(std::size_t candidates, std::size_t projected) override {
        ++witness_->completed_blocks;
        work_.executed = true;
        work_.statistics_blocks += candidates;
        work_.projected_values += projected;
    }

    BulkReceipt receipt() const override { return work_; }

private:
    static void project(std::span<const std::complex<float>> input, std::span<std::complex<float>> output,
                        std::size_t rows, std::size_t stride, std::size_t lanes,
                        std::span<const std::size_t> bins) {
        const auto normalizer = 1 / std::sqrt(static_cast<double>(rows));
        for (std::size_t bin = 0; bin < bins.size(); ++bin) {
            const auto step = std::polar(1., -2 * std::numbers::pi * bins[bin] / rows);
            for (std::size_t lane = 0; lane < lanes; ++lane) {
                Complex sum{}, phase = 1;
                for (const auto value : input.subspan(lane * stride, rows)) {
                    sum += Complex(value) * phase;
                    phase *= step;
                }
                output[lane * bins.size() + bin] = std::complex<float>(sum * normalizer);
            }
        }
    }

    static FineBulkStatistics mapped_statistics(const FineBulkBlock& block, const FineMappingSource& source,
                                                std::size_t candidate) {
        const auto lanes = 2 * block.taps;
        std::vector<Complex> rendered(lanes * block.rows);
        source(candidate, 0, block.rows, [&](auto offsets, auto mapped, auto direct) {
            for (const auto& column : mapped) {
                std::vector<const double*> tiles;
                for (const auto& tile : column.tiles)
                    tiles.push_back(reinterpret_cast<const double*>(tile.samples.data()));
                const TriadMappedRow map{column.fraction, column.scale, column.phase, column.phase_step,
                    column.origin, tiles.size(), tiles.data(),
                    reinterpret_cast<double*>(rendered.data() + column.output_column * block.rows)};
                triad_render_mapped_v1(&map, 1, offsets.data(), offsets.size(), column.tiles.front().samples.size());
            }
            for (const auto& column : direct)
                std::ranges::copy(column.samples, rendered.begin() + column.output_column * block.rows);
        });
        FineBulkStatistics output;
        output.nonzero_basis = std::ranges::any_of(rendered, [](Complex value) { return value != Complex{}; });
        const std::vector<std::complex<float>> samples(rendered.begin(), rendered.end());
        const auto modes = block.projection_indices.size();
        std::vector<std::complex<float>> projected(lanes * modes);
        project(samples, projected, block.rows, block.rows, lanes, block.projection_indices);
        for (std::size_t bit = 0; bit < 2; ++bit) {
            for (std::size_t tap = 0; tap < block.taps; ++tap) {
                const auto a = std::span(projected).subspan((bit * block.taps + tap) * modes, modes);
                auto& corr = output.correlations[(bit + 1) * block.taps + tap];
                for (std::size_t mode = 0; mode < modes; ++mode)
                    corr += std::conj(Complex(a[mode])) * block.projected_y[mode];
                corr /= block.variance;
                for (std::size_t other = 0; other < block.taps; ++other) {
                    const auto b = std::span(projected).subspan((bit * block.taps + other) * modes, modes);
                    auto& gram = output.gram[((bit + 1) * block.taps + tap) * block.taps + other];
                    for (std::size_t mode = 0; mode < modes; ++mode)
                        gram += std::conj(Complex(a[mode])) * Complex(b[mode]);
                    gram /= block.variance;
                }
            }
        }
        return output;
    }
    std::shared_ptr<HostProjectionWitness> witness_;
    std::vector<std::complex<float>> staging_;
    std::span<const std::size_t> bins_;
    std::size_t rows_ = 0;
    BulkReceipt work_;
};

class HostProjectionBackend final : public ReceiverBulkBackend {
public:
    explicit HostProjectionBackend(std::shared_ptr<HostProjectionWitness> witness)
        : witness_(std::move(witness)) {}

    bool fine_maps_on_host() const noexcept override { return true; }
    std::size_t fine_resident_blocks(const FineBulkGeometry&) const noexcept override {
        return witness_->resident_capacity;
    }
    std::unique_ptr<FineBulk> make_fine(double, double) override {
        return std::make_unique<HostProjectionBulk>(witness_);
    }
    std::unique_ptr<CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override {
        return HostProjectionBulk::capacity * (1500 + 47) * sizeof(std::complex<float>);
    }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    BulkReceipt receipt() const override { return {}; }

private:
    std::shared_ptr<HostProjectionWitness> witness_;
};

void host_mapped_projection_scheduler(bool resident) {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    constexpr unsigned workers = 3;
    auto config = f.config;
    config.spread_delay_seconds = .021;
    FineSession native(config, f.slot, f.key, extent, f.iq, {}, f.coordinates, 100, 0, workers,
                       fine_workspace(config, f.slot, extent, workers));
    native.advance_to(extent.rows);
    ReceiverSlotScore expected;
    native.finish(expected);
    const auto expected_work = native.progress();

    const auto witness = std::make_shared<HostProjectionWitness>();
    if (resident) witness->resident = ResidentAction::execute;
    config.bulk_backend = std::make_shared<HostProjectionBackend>(witness);
    FineSession mixed(config, f.slot, f.key, extent, f.iq, {}, f.coordinates, 100, 0, workers,
                      fine_workspace(config, f.slot, extent, workers));
    mixed.advance_to(f.fs);
    mixed.advance_to(extent.rows);
    ReceiverSlotScore actual;
    mixed.finish(actual);
    const auto work = mixed.progress();
    check(witness->native_blocks == 1 && witness->completed_blocks == work.blocks,
          "regular dispatches and the enlarged native terminal block must be scored once");
    if (resident) {
        const auto& batches = witness->resident_batch_blocks;
        check(batches.size() > 1 && batches.back() == 1 &&
              std::ranges::all_of(std::span(batches).first(batches.size() - 1),
                  [](auto count) { return count == 2; }) &&
              std::accumulate(batches.begin(), batches.end(), std::size_t{1}) == work.blocks &&
              work.cache.prepared_calls == (work.blocks - 1) * work.initial_affine.initial_states &&
              work.cache.prepared_mapped_columns > 0 &&
              witness->batch_lanes.empty(),
              "regular blocks must be paired and the final eligible block dispatched alone before the native tail");
    } else {
        check(witness->device_blocks > 1 && witness->batch_lanes.size() == 2 * witness->device_blocks,
              "the host projector must process every regular public block");
        for (std::size_t i = 0; i < witness->batch_lanes.size(); i += 2)
            check(witness->batch_lanes[i] == 8 && witness->batch_lanes[i + 1] == 4,
                  "three two-tap candidates must use one full batch and one partial batch");
    }
    check(actual.complete && expected.complete && actual.consumed_samples == extent.rows &&
          actual.quiet_modes == expected.quiet_modes && actual.quiet_energy == expected.quiet_energy &&
          actual.zero_h0_verified && actual.basis_rows == expected.basis_rows &&
          work.initial_affine.retained_states == expected_work.initial_affine.retained_states &&
          work.initial_affine.projection_modes == expected_work.initial_affine.projection_modes,
          "host scheduling must preserve physical completion, quiet statistics, and all public work");
    check(work.bulk && work.bulk->executed &&
          work.bulk->statistics_blocks == work.blocks * work.initial_affine.initial_states &&
          work.bulk->projected_values == expected_work.initial_affine.projection_modes,
          "one owner must credit every completed device and native projection exactly once");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto score = expected.bit_evidence[bit];
        check(std::abs(actual.bit_evidence[bit] - score) <= 1e-5 * std::max(1., std::abs(score)),
              "the bounded float templates must preserve the native reference likelihood");
    }

    if (resident) return;
    const auto failure = std::make_shared<HostProjectionWitness>();
    failure->fail_second_batch = true;
    config.bulk_backend = std::make_shared<HostProjectionBackend>(failure);
    FineSession failed(config, f.slot, f.key, extent, f.iq, {}, f.coordinates, 100, 0, workers,
                       fine_workspace(config, f.slot, extent, workers));
    rejects_with([&] { failed.advance_to(extent.rows); }, "injected host projection failure",
                 "failure in a partial batch must retain the initiating error");
    check(failure->batch_lanes.size() == 2 && !failure->completed_blocks,
          "the first successful device batch cannot credit a partially scored block");
    rejects([&] { (void)failed.progress(); }, "partial host projection must not publish progress");
    ReceiverSlotScore incomplete;
    rejects([&] { failed.finish(incomplete); }, "partial host projection must not finish a slot");
    check(!incomplete.complete, "host projection failure must not credit physical completion");
}

void incomplete_resident_pair_cannot_publish_quiet() {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    const std::vector<Complex> noise_free(extent.rows);
    for (const auto [action, message] : std::array{
        std::pair{ResidentAction::wrong_order, "requested an invalid public block"},
        std::pair{ResidentAction::missing_mapping, "did not map its complete public block"},
        std::pair{ResidentAction::missing_statistics, "finite complete sufficient statistics"},
        std::pair{ResidentAction::missing_work, "did not execute its complete public statistics work"},
        std::pair{ResidentAction::fail_second, "injected second resident block failure"}}) {
        auto config = f.config;
        config.spread_delay_seconds = .021;
        auto witness = std::make_shared<HostProjectionWitness>();
        witness->resident = action;
        config.bulk_backend = std::make_shared<HostProjectionBackend>(witness);
        FineSession session(config, f.slot, f.key, extent, noise_free, {}, f.coordinates,
                            100, 0, 1, fine_workspace(config, f.slot, extent, 1));
        rejects_with([&] { session.advance_to(f.fs / 2); }, message,
                     "a partial resident pair must retain its initiating error");
        check(witness->resident_batch_blocks == std::vector<std::size_t>{2} && witness->batch_lanes.empty(),
              "a failed pair must never retry the partial work through host projection");
        rejects([&] { (void)session.progress(); }, "a failed resident pair must not publish progress");
        ReceiverSlotScore score;
        rejects([&] { session.finish(score); }, "a failed resident pair must not finish a slot");
        check(!score.complete && score.decision == SlotDecision::ambiguous,
              "a failed resident pair must not provide scored quiet");
    }
}
// A window of dispatches must leave exactly the state that single
// dispatches leave: the same statistics reach each state in public order.
void resident_windows_preserve_public_order() {
    const Fixture f(240, 60);
    const FineObservation extent{f.slot.first_sample, f.iq.size(), FineRunKind::operational, true};
    constexpr unsigned workers = 3;
    auto config = f.config;
    config.spread_delay_seconds = .021;
    struct Outcome {
        ReceiverSlotScore score;
        FineProgress work;
        std::vector<std::size_t> batches;
        std::size_t native_blocks;
    };
    const auto run = [&](std::size_t capacity, bool checkpoints, std::span<const std::uint8_t> valid) {
        const auto witness = std::make_shared<HostProjectionWitness>();
        witness->resident = ResidentAction::execute;
        witness->resident_capacity = capacity;
        auto routed = config;
        routed.bulk_backend = std::make_shared<HostProjectionBackend>(witness);
        FineSession session(routed, f.slot, f.key, extent, f.iq, valid, f.coordinates, 100, 0, workers,
                            fine_workspace(routed, f.slot, extent, workers));
        // The second checkpoint ends inside a window of every capacity.
        if (checkpoints)
            for (const auto rows : {std::size_t{f.fs}, std::size_t{5 * f.fs + 120}}) session.advance_to(rows);
        session.advance_to(extent.rows);
        Outcome outcome;
        session.finish(outcome.score);
        outcome.work = session.progress();
        outcome.batches = witness->resident_batch_blocks;
        outcome.native_blocks = witness->native_blocks;
        check(witness->batch_lanes.empty(), "a resident window must not repeat its blocks through host projection");
        return outcome;
    };
    // One erased block inside a window keeps its place without statistics.
    std::vector<std::uint8_t> erased(extent.rows, 1);
    erased[7 * 60 + 11] = 0;
    for (const auto& valid : {std::span<const std::uint8_t>{}, std::span<const std::uint8_t>(erased)}) {
        const auto single = run(2, false, valid);
        const auto blocks = std::accumulate(single.batches.begin(), single.batches.end(), std::size_t{0});
        check(blocks + single.native_blocks + single.work.erased_blocks == single.work.blocks &&
              single.work.erased_blocks == (valid.empty() ? 0u : 1u),
              "single dispatches must score every observed public block once");
        for (const std::size_t capacity : {4, 6, 16, 64}) {
            for (const bool checkpoints : {false, true}) {
                const auto window = run(capacity, checkpoints, valid);
                const auto& a = window.score;
                const auto& b = single.score;
                check(a.bit_evidence == b.bit_evidence && a.quiet_energy == b.quiet_energy &&
                      a.quiet_modes == b.quiet_modes && a.quiet_threshold == b.quiet_threshold &&
                      a.complete == b.complete && a.all_valid == b.all_valid && a.basis_rows == b.basis_rows &&
                      a.consumed_samples == b.consumed_samples && a.affine_blocks == b.affine_blocks,
                      "a resident window changed the fixed-affine evidence or completion");
                const auto& lane = window.work.initial_affine;
                const auto& expected = single.work.initial_affine;
                check(lane.retained_states == expected.retained_states && lane.bit_scores == expected.bit_scores &&
                      lane.basis_calls == expected.basis_calls && lane.basis_rows == expected.basis_rows &&
                      lane.positive_basis_blocks == expected.positive_basis_blocks &&
                      lane.projection_modes == expected.projection_modes &&
                      lane.gaussian_updates == expected.gaussian_updates &&
                      lane.projected_template_energy == expected.projected_template_energy &&
                      window.work.blocks == single.work.blocks && window.work.segments == single.work.segments &&
                      window.work.erased_blocks == single.work.erased_blocks &&
                      window.work.slow_gain_bit_evidence == single.work.slow_gain_bit_evidence,
                      "a resident window changed the public work receipt");
                check(window.work.bulk && single.work.bulk &&
                      window.work.bulk->statistics_blocks == single.work.bulk->statistics_blocks &&
                      window.work.bulk->projected_values == single.work.bulk->projected_values &&
                      window.work.cache.prepared_calls == single.work.cache.prepared_calls &&
                      window.work.cache.prepared_mapped_columns == single.work.cache.prepared_mapped_columns &&
                      window.native_blocks == single.native_blocks,
                      "a resident window changed the mapped or native block count");
                check(std::accumulate(window.batches.begin(), window.batches.end(), std::size_t{0}) == blocks &&
                      std::ranges::all_of(window.batches, [&](auto count) { return count <= capacity; }) &&
                      *std::ranges::max_element(window.batches) > 2 && window.batches.size() < single.batches.size(),
                      "a resident window must gather more than one dispatch and score each block once");
                if (!checkpoints && valid.empty())
                    check(window.batches.front() == std::min(capacity, blocks),
                          "an uninterrupted window must fill its public block capacity");
            }
        }
    }

    // A failure inside a window keeps its cause and publishes nothing.
    const std::vector<Complex> noise_free(extent.rows);
    for (const auto [action, message] : std::array{
        std::pair{ResidentAction::wrong_order, "requested an invalid public block"},
        std::pair{ResidentAction::missing_mapping, "did not map its complete public block"},
        std::pair{ResidentAction::missing_statistics, "finite complete sufficient statistics"},
        std::pair{ResidentAction::missing_work, "did not execute its complete public statistics work"},
        std::pair{ResidentAction::fail_second, "injected second resident block failure"}}) {
        const auto witness = std::make_shared<HostProjectionWitness>();
        witness->resident = action;
        witness->resident_capacity = 6;
        auto routed = config;
        routed.bulk_backend = std::make_shared<HostProjectionBackend>(witness);
        FineSession session(routed, f.slot, f.key, extent, noise_free, {}, f.coordinates,
                            100, 0, workers, fine_workspace(routed, f.slot, extent, workers));
        rejects_with([&] { session.advance_to(3 * f.fs / 2); }, message,
                     "a partial resident window must retain its initiating error");
        check(witness->resident_batch_blocks == std::vector<std::size_t>{6} && witness->batch_lanes.empty(),
              "a failed window must never retry its blocks through host projection");
        rejects([&] { (void)session.progress(); }, "a failed resident window must not publish progress");
        ReceiverSlotScore score;
        rejects([&] { session.finish(score); }, "a failed resident window must not finish a slot");
        check(!score.complete && score.decision == SlotDecision::ambiguous,
              "a failed resident window must not provide scored quiet");
    }
}
} // namespace

int main() {
    try {
        helper_terminal_geometry();
        helper_dc_projection();
        static_low_rate_projection();
        fp32_projection_workspace_and_scores();
        terminal_projection_boundaries();
        compiled_observation_overlap();
        production_equivalence();
        uninterrupted_interior_probe();
        erasure_and_failure();
        cold_maximum_states();
        slow_gain_coherence_is_bounded_and_normalized();
        direct_gain_packs_two_tap_statistics();
        stationary_fading_gain_covers_direct_paths();
        gaussian_update_matches_dense_complex_covariance();
        gaussian_update_matches_dense_complex_covariance(.5, true);
        gaussian_update_matches_dense_complex_covariance(2., true);
        additive_gain_preserves_large_gram_precision();
        two_tap_factor_preserves_large_gram_precision();
        projected_rank_one_preserves_phased_orthogonality();
        two_tap_factor_matches_moderate_dense_likelihood();
        inert_null_skip_matches_exported_updates();
        two_tap_native_dc_projection_preserves_complete_scores();
        tracked_span_matches_signed_correlation_without_changing_evidence();
        independent_gain_keeps_full_public_prior_and_bounded_work();
        signed_gain_retains_full_unselected_mass();
        projected_tracking_grid_preserves_the_rounded_edge();
        added_epoch_track_keeps_prior_mass_and_work();
        incomplete_bulk_cannot_publish_quiet(FineLanes::fixed_affine);
        incomplete_bulk_cannot_publish_quiet(FineLanes::fixed_and_adaptive);
        host_mapped_projection_scheduler(false);
        host_mapped_projection_scheduler(true);
        incomplete_resident_pair_cannot_publish_quiet();
        resident_windows_preserve_public_order();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
