#include "datapump/triad_receiver.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
constexpr unsigned sample_rate = 497;
constexpr std::size_t workspace_limit = 256 * 1024 * 1024;
constexpr std::size_t extension_workspace = 1024 * 1024;
constexpr double omitted = -std::numeric_limits<double>::infinity();
unsigned checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    if (!(std::abs(actual - expected) <= tolerance))
        std::cerr << message << ": got " << actual << ", expected " << expected << '\n';
    check(std::abs(actual - expected) <= tolerance, message);
}
template<class Function> void rejects(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const Error&) { rejected = true; }
    check(rejected, message);
}

struct Fixture {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    std::vector<Complex> iq;
    std::vector<std::uint8_t> valid;
    Crypto key = Crypto::amateur();
};

// The bounded native waveform fixture from test_triad_native_receiver: source
// generation and mapping call the canonical C++ implementation. The explicit
// nonzero public origin/ordinal catch an extension accidentally using slot zero.
Fixture fixture(std::optional<std::uint8_t> bit = std::nullopt) {
    Fixture f;
    f.config.sample_rate_hz = sample_rate;
    f.config.data_if_hz = 1725;
    f.config.noise_variance = .25;
    auto& s = f.slot;
    s.first_sample = 20'000;
    s.index = 37;
    s.window_capture_lead_seconds = -17.7;
    s.source_start_seconds = 18.1;
    s.format.sample_rate = sample_rate;
    s.format.inner_rate_hz = 120;
    s.format.added_symbols = 1;
    s.format.bit_seconds = .001;
    s.format.pulse_shape = PulseShape::TaperedRrc;
    s.format.epoch = 1793850000;
    s.format.symbol_ordinal = 7000037;
    s.format.encrypt = true;
    const auto geometry_value = geometry(s.format);
    const auto count = geometry_value.source_samples + 403;
    check(count < 6000, "native supplement fixture exceeded its bounded sample budget");
    s.stop_sample = s.first_sample + count;
    s.quiet_first_sample = s.first_sample + 230;
    s.quiet_stop_sample = s.first_sample + geometry_value.source_samples + 180;
    f.iq.resize(count);
    f.valid.assign(count, 1);
    std::uint64_t random = 37;
    const auto uniform = [&] {
        random += 0x9e3779b97f4a7c15ULL;
        auto x = random;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return (static_cast<double>(x >> 11) + .5) / 9007199254740992.;
    };
    for (auto& value : f.iq) {
        const auto radius = std::sqrt(-f.config.noise_variance * std::log(uniform()));
        const auto phase = 2 * std::numbers::pi * uniform();
        value = std::polar(radius, phase);
    }
    if (bit) {
        PatternSource source(s.format, f.key, *bit);
        Candidate candidate;
        candidate.arrival_seconds = static_cast<double>(s.window_capture_lead_seconds + s.source_start_seconds);
        std::vector<double> offsets(count);
        std::iota(offsets.begin(), offsets.end(), 0.);
        std::vector<Complex> basis(count);
        shaped_basis_mapped(source, candidate, sample_rate, 0, offsets, basis, f.config.data_if_hz);
        for (std::size_t n = 0; n < count; ++n) f.iq[n] += .72 * basis[n];
    }
    return f;
}

SlotEvidenceSupplement supplement(std::array<double, 2> values) {
    SlotEvidenceSupplement result;
    result.fixed_weight = .25;
    result.supported = [](const auto&, const auto&) { return true; };
    result.workspace = [](const auto&, const auto&, unsigned) { return extension_workspace; };
    // These synthetic values test hook policy, NOT calibrated detection or
    // sensitivity. They never enter any operating or appraisal receiver mode.
    result.score = [values](const auto&, const auto&, const auto&, auto, const auto&,
                             unsigned, std::size_t, std::stop_token, auto&) { return values; };
    return result;
}

ReceiverSlotScore score(const Fixture& f, const SlotEvidenceSupplement* extension = nullptr,
                       std::stop_token stop = {}, ReceiverExecutor* executor = nullptr) {
    if (extension)
        return score_slot_with_supplement(f.config, f.slot, f.key, f.iq, f.valid, 2,
                                          workspace_limit, stop, executor, *extension);
    return score_slot(f.config, f.slot, f.key, f.iq, f.valid, 2, workspace_limit, stop, executor);
}

void exact_evidence(const ReceiverSlotScore& actual, const ReceiverSlotScore& expected) {
    check(actual.index == expected.index && actual.first_sample == expected.first_sample &&
          actual.stop_sample == expected.stop_sample && actual.consumed_samples == expected.consumed_samples,
          "disabled supplement changed the scored public window");
    check(actual.bit_evidence == expected.bit_evidence &&
          actual.adaptive_bit_evidence == expected.adaptive_bit_evidence &&
          actual.combined_bit_evidence == expected.combined_bit_evidence &&
          actual.operational_bit_evidence == expected.operational_bit_evidence &&
          actual.operational_log_e == expected.operational_log_e &&
          actual.quiet_energy == expected.quiet_energy && actual.quiet_threshold == expected.quiet_threshold,
          "disabled supplement changed canonical evidence arithmetic");
    check(actual.decision == expected.decision && actual.bit == expected.bit &&
          actual.reason == expected.reason && actual.complete == expected.complete &&
          actual.all_valid == expected.all_valid && actual.zero_h0_verified == expected.zero_h0_verified &&
          actual.operational_passes_conditional_bound == expected.operational_passes_conditional_bound,
          "disabled supplement changed canonical admission");
    check(actual.coarse_workspaces == expected.coarse_workspaces &&
          actual.coarse_frequency_points == expected.coarse_frequency_points &&
          actual.coarse_arrival_points == expected.coarse_arrival_points &&
          actual.fine_candidates == expected.fine_candidates &&
          actual.basis_rows == expected.basis_rows &&
          actual.adaptive_transition_edges == expected.adaptive_transition_edges &&
          actual.workspace_peak_bytes == expected.workspace_peak_bytes,
          "disabled supplement changed canonical work or workspace");
}

void workspace_and_disabled_paths(const Fixture& quiet, const ReceiverSlotScore& baseline) {
    SlotEvidenceSupplement zero;
    // Disabled callbacks deliberately fail if invoked.
    zero.supported = [](const auto&, const auto&) -> bool { throw Error("disabled support called"); };
    zero.workspace = [](const auto&, const auto&, unsigned) -> std::size_t { throw Error("disabled quote called"); };
    exact_evidence(score(quiet, &zero), baseline);
    auto unsupported = supplement({80, 80});
    unsupported.supported = [](const auto&, const auto&) { return false; };
    unsupported.workspace = zero.workspace;
    exact_evidence(score(quiet, &unsupported), baseline);
    for (const auto workers : {1U, 2U}) {
        for (const bool overlap : {false, true}) {
            const auto canonical = overlap ? receiver_slot_overlap_workspace(quiet.config, quiet.slot, workers) :
                                             receiver_slot_workspace(quiet.config, quiet.slot, workers);
            check(receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, workers, zero, overlap) == canonical &&
                  receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, workers, unsupported, overlap) == canonical,
                  "disabled extension changed a normal/overlap lane-shape quote");
        }
        auto invalid = supplement({omitted, omitted});
        invalid.workspace = [](const auto&, const auto&, unsigned) { return std::size_t{0}; };
        rejects([&] { receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, workers, invalid); },
                "supported zero workspace restored prior mass for an admitted lane shape");
    }
    for (const auto weight : {-1., 1., std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        auto invalid = supplement({omitted, omitted});
        invalid.fixed_weight = weight;
        rejects([&] { receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, 2, invalid); },
                "invalid fixed supplement weight was accepted");
    }
    auto no_support = supplement({omitted, omitted});
    no_support.supported = {};
    rejects([&] { receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, 2, no_support); },
            "positive weight accepted without lane-independent public support");
    auto no_score = supplement({omitted, omitted});
    no_score.score = {};
    rejects([&] { receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, 2, no_score); },
            "supported extension accepted without a scorer");
    auto large = supplement({omitted, omitted});
    large.workspace = [](const auto&, const auto&, unsigned) { return workspace_limit; };
    check(receiver_slot_workspace_with_supplement(quiet.config, quiet.slot, 2, large) > workspace_limit,
          "extension workspace quote omitted the canonical input/receipt lease");
    rejects([&] { score(quiet, &large); }, "over-budget extension reached receiver scoring");
}

void omitted_mass_and_preparation(const Fixture& quiet, const ReceiverSlotScore& baseline) {
    auto extension = supplement({omitted, omitted});
    ReceiverExecutor executor(2);
    unsigned calls = 0;
    std::atomic<unsigned> leaves{0};
    extension.score = [&](const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                          std::span<const Complex> iq, const FinePreparation& prepared,
                          unsigned workers, std::size_t scratch, std::stop_token stop, ReceiverExecutor& shared) {
        ++calls;
        check(&config == &quiet.config && &slot == &quiet.slot && &key == &quiet.key &&
              iq.data() == quiet.iq.data() && iq.size() == quiet.iq.size(),
              "extension did not borrow the immutable keyed public observation");
        check(&shared == &executor && workers == 2 && scratch == extension_workspace,
              "extension lost the admitted shared executor or workspace shape");
        check(!prepared.coordinates.empty() && prepared.grid.lower == 0 && prepared.grid.upper == 0 &&
              prepared.grid.frequency_radius == 0 && prepared.grid.clock_bound == 0,
              "extension did not receive snapped complete native fine preparation");
        for (const auto& coordinate : prepared.coordinates)
            check(coordinate.arrival_seconds == 0 && coordinate.frequency_hz == 0 && coordinate.clock_ppm == 0 &&
                  std::ranges::find(receiver_epoch_offsets, coordinate.epoch_offset_seconds) != receiver_epoch_offsets.end(),
                  "extension received a free-fitted coordinate outside the fixed public grid");
        shared.parallel(slot.index, 2, workers, stop,
            [&](unsigned lane, std::size_t, std::stop_token leaf_stop) {
                if (lane >= workers || leaf_stop.stop_requested()) throw Error("invalid extension executor leaf");
                ++leaves;
            });
        return std::array<double, 2>{omitted, omitted};
    };
    const auto changed = score(quiet, &extension, {}, &executor);
    check(calls == 1 && leaves == 2, "supported omitted branch did not execute once and join its shared leaves");
    check(changed.complete && changed.decision == SlotDecision::quiet && !changed.bit &&
          changed.quiet_energy == baseline.quiet_energy && changed.quiet_threshold == baseline.quiet_threshold,
          "omitted evidence changed physical quiet scoring");
    for (unsigned bit = 0; bit < 2; ++bit)
        near(changed.operational_bit_evidence[bit], baseline.operational_bit_evidence[bit] + std::log(.75),
             2e-12, "an omitted extension renormalized the reserved mass");
    near(changed.operational_log_e, baseline.operational_log_e + std::log(.75), 2e-12,
         "omitted branch changed the equal bit prior");
    check(changed.coarse_workspaces == baseline.coarse_workspaces &&
          changed.coarse_frequency_points == baseline.coarse_frequency_points &&
          changed.coarse_arrival_points == baseline.coarse_arrival_points &&
          changed.bit_evidence == baseline.bit_evidence,
          "extension changed the original acquisition coverage or affine evidence");

    auto bright = fixture(1);
    const auto bright_baseline = score(bright);
    check(bright_baseline.decision == SlotDecision::data && bright_baseline.bit == 1,
          "coherent early-admission control failed before extension comparison");
    auto skipped = supplement({80, 0});
    unsigned skipped_calls = 0;
    skipped.score = [&](const auto&, const auto&, const auto&, auto, const auto&,
                         unsigned, std::size_t, std::stop_token, auto&) {
        ++skipped_calls;
        return std::array<double, 2>{80, 0};
    };
    const auto early = score(bright, &skipped);
    check(skipped_calls == 0 && early.decision == SlotDecision::data && early.bit == 1,
          "early coherent DATA unnecessarily executed the extension");
    for (unsigned bit = 0; bit < 2; ++bit)
        near(early.operational_bit_evidence[bit], bright_baseline.operational_bit_evidence[bit] + std::log(.75),
             2e-12, "early admission spent the extension's reserved prior mass");
}

void policy_and_invalid_inputs(const Fixture& quiet) {
    auto varying = quiet;
    varying.config.time_varying_channel = true;
    auto tied = supplement({80, 81});
    const auto ambiguity = score(varying, &tied);
    check(ambiguity.complete && ambiguity.operational_passes_conditional_bound &&
          ambiguity.decision == SlotDecision::ambiguous && !ambiguity.bit &&
          ambiguity.operational_bit_log_separation < varying.config.time_varying_minimum_bit_log_separation,
          "synthetic supplemental presence bypassed canonical bit separation");

    auto separated = supplement({0, 80});
    const auto admitted = score(varying, &separated);
    check(admitted.complete && admitted.all_valid && admitted.zero_h0_verified &&
          admitted.decision == SlotDecision::data && admitted.bit == 1 &&
          admitted.reason == "bounded_supplemental_data_evidence" &&
          admitted.operational_bit_log_separation >= varying.config.time_varying_minimum_bit_log_separation,
          "synthetic separated extension did not traverse canonical admission");

    unsigned invalid_calls = 0;
    separated.score = [&](const auto&, const auto&, const auto&, auto, const auto&,
                           unsigned, std::size_t, std::stop_token, auto&) {
        ++invalid_calls;
        return std::array<double, 2>{0, 80};
    };
    auto erased = quiet;
    erased.valid[317] = 0;
    const auto gap = score(erased, &separated);
    check(invalid_calls == 0 && !gap.complete && !gap.all_valid &&
          gap.decision == SlotDecision::ambiguous && !gap.bit && gap.reason == "coarse_erasures_unsupported",
          "capture erasure reached supplement admission or became physical silence");
    auto nonfinite = quiet;
    nonfinite.iq[317] = {std::numeric_limits<double>::quiet_NaN(), 0};
    rejects([&] { score(nonfinite, &separated); }, "nonfinite IQ reached extension scoring");
    std::stop_source cancelled;
    cancelled.request_stop();
    rejects([&] { score(quiet, &separated, cancelled.get_token()); }, "cancelled slot reached extension admission");
    check(invalid_calls == 0, "invalid or cancelled input invoked the extension");
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
        auto broken = supplement({0, invalid});
        rejects([&] { score(quiet, &broken); }, "invalid supplemental log evidence was admitted");
    }
}

void retained_line_suppresses_raw_extension(double amplitude, unsigned expected_calls) {
    auto line = fixture();
    line.slot.format.encrypt = false;
    line.config.time_varying_channel = true;
    line.config.time_varying_atmospheric_hz = 90;
    line.config.time_varying_common_doppler_hz = 90;
    const auto begin = line.slot.quiet_first_sample - line.slot.first_sample;
    const auto end = line.slot.quiet_stop_sample - line.slot.first_sample;
    constexpr double frequency = 80.;
    for (auto row = begin; row < end; ++row)
        line.iq[row] += std::polar(amplitude, 2 * std::numbers::pi * frequency * row / sample_rate);
    auto extension = supplement({0, 80});
    unsigned calls = 0;
    extension.score = [&](const auto&, const auto&, const auto&, auto, const auto&,
                          unsigned, std::size_t, std::stop_token, auto&) {
        ++calls;
        return std::array<double, 2>{0, 80};
    };
    const auto result = score(line, &extension);
    check(calls == expected_calls && result.complete && result.decision == SlotDecision::ambiguous && !result.bit &&
          result.reason == "coherent_line_in_public_data_aperture" &&
          result.varying_line_log_evidence >= receiver_operational_threshold() &&
          std::abs(result.varying_line_frequency_hz - frequency) < .01,
          "retained coherent line did not suppress the raw Gaussian supplement");
    check(result.varying_quiet_modes > 0 && result.varying_quiet_energy > result.varying_quiet_threshold,
          "line-only extension path bypassed the canonical physical quiet veto");
}

void retained_line_with_wanted_data() {
    // Source and receiver both keep the encrypted epoch/ordinal/key from the
    // same fixture. A line is not a reason to veto independently valid DATA.
    auto wanted = fixture(1);
    wanted.config.time_varying_channel = true;
    wanted.config.time_varying_atmospheric_hz = 90;
    wanted.config.time_varying_common_doppler_hz = 90;
    const auto begin = wanted.slot.quiet_first_sample - wanted.slot.first_sample;
    const auto end = wanted.slot.quiet_stop_sample - wanted.slot.first_sample;
    for (auto row = begin; row < end; ++row)
        wanted.iq[row] += std::polar(.5, 2 * std::numbers::pi * 80. * row / sample_rate);
    const auto baseline = score(wanted);
    // Deliberately conflicting synthetic evidence remains test-only. An early
    // canonical admission may skip it; a retained line must suppress it.
    const auto extension = supplement({80, 0});
    const auto changed = score(wanted, &extension);
    for (const auto* result : {&baseline, &changed})
        check(result->complete && result->all_valid && result->zero_h0_verified &&
              result->operational_passes_conditional_bound &&
              result->decision == SlotDecision::data && result->bit == 1 &&
              result->operational_bit_log_separation >= wanted.config.time_varying_minimum_bit_log_separation,
              "coherent line suppressed correctly keyed wanted DATA");
    check(changed.coarse_workspaces == baseline.coarse_workspaces &&
          changed.coarse_frequency_points == baseline.coarse_frequency_points &&
          changed.coarse_arrival_points == baseline.coarse_arrival_points,
          "line-plus-DATA extension changed public acquisition coverage");
}
}

int main() {
    try {
        const auto quiet = fixture();
        const auto baseline = score(quiet);
        check(baseline.complete && baseline.decision == SlotDecision::quiet && !baseline.bit,
              "native noise control failed before supplement tests");
        workspace_and_disabled_paths(quiet, baseline);
        omitted_mass_and_preparation(quiet, baseline);
        policy_and_invalid_inputs(quiet);
        // The strong original fixture crosses an ordinary fallback first;
        // its line gate must still run although the extension was skipped.
        retained_line_suppresses_raw_extension(.5, 0);
        // This fixed weaker line keeps the original evidence below admission,
        // so synthetic supplemental evidence triggers the SAME line/quiet gate.
        retained_line_suppresses_raw_extension(.1, 1);
        retained_line_with_wanted_data();
        std::cout << "HF native evidence supplement passed " << checks << " assertions\n";
    } catch (const std::exception& error) {
        std::cerr << "HF native evidence supplement failed after " << checks << " assertions: " << error.what() << '\n';
        return 1;
    }
}
