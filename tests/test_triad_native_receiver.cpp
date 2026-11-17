#include "datapump/triad_receiver.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "../src/search_fft.hpp"
#include "../src/triad_receiver_coarse.hpp"
#include "../src/triad_receiver_bulk.hpp"
#include "../src/triad_receiver_driver.hpp"
#include "../src/triad_receiver_executor.hpp"
#include "../src/triad_receiver_fine.hpp"
#include "../src/triad_receiver_helper.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr unsigned sample_rate = 497;
constexpr std::size_t workspace_limit = 256 * 1024 * 1024;
const bool cpu_fft_fp32_qualified =
    std::string_view(modem::detail::search_fft::kernel_name()) == "avx2";

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

template<typename Exception, typename Operation>
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
    std::vector<Complex> iq;
    std::vector<std::uint8_t> valid;
    Crypto key = Crypto::amateur();
};

Fixture fixture(std::optional<std::uint8_t> bit, bool spread, bool encrypt, std::uint64_t seed = 37,
                double test_frequency_offset_hz = .05,
                double broad_frequency_bound_hz = .12,
                double nominal_carrier_hz = 0) {
    Fixture f;
    auto& c = f.config;
    c.sample_rate_hz = sample_rate;
    c.data_if_hz = 1725;
    c.noise_variance = .25;
    c.clock_bound_ppm = spread ? 12 : 0;
    c.frequency_bound_hz = spread ? broad_frequency_bound_hz : 0;
    c.spread_delay_seconds = spread ? .003 : 0;
    c.spread_doppler_hz = spread ? .2 : 0;
    auto& s = f.slot;
    s.first_sample = 20'000;  // Nonzero global window origin.
    s.index = 37;
    s.window_capture_lead_seconds = -17.7;
    s.source_start_seconds = 18.1;
    s.arrival_lower_seconds = spread ? -.015 : 0;
    s.arrival_upper_seconds = spread ? .015 : 0;
    s.format.sample_rate = sample_rate;
    s.format.inner_rate_hz = 120;
    s.format.added_symbols = 1;
    s.format.configured_carrier_hz = nominal_carrier_hz;
    s.format.bit_seconds = .001;
    s.format.pulse_shape = PulseShape::TaperedRrc;
    s.format.epoch = 1793850000;
    s.format.symbol_ordinal = 7000037;
    s.format.encrypt = encrypt;
    const auto g = geometry(s.format);
    const auto count = g.source_samples + 403;
    s.stop_sample = s.first_sample + count;
    s.quiet_first_sample = s.first_sample + 230;
    s.quiet_stop_sample = s.first_sample + g.source_samples + 180;
    check(count < 6'000, "bounded receiver fixture grew beyond its sample budget");
    f.iq.resize(count);
    f.valid.resize(count, 1);

    auto rng = seed;
    const auto uniform = [&]() {
        rng += 0x9e3779b97f4a7c15ULL;
        auto x = rng;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return (static_cast<double>(x >> 11) + .5) / 9007199254740992.;
    };
    for (auto& sample : f.iq) {
        const auto radius = std::sqrt(-c.noise_variance * std::log(uniform()));
        const auto phase = 2 * std::numbers::pi * uniform();
        sample = std::polar(radius, phase);
    }
    if (bit) {
        auto tx = s.format;
        tx.epoch += spread ? 2 : 0;
        PatternSource source(tx, f.key, *bit);
        Candidate candidate;
        candidate.clock_ppm = spread ? 6 : 0;
        const auto scale = 1 + candidate.clock_ppm * 1e-6;
        const auto arrival = spread ? .004 : 0;
        candidate.arrival_seconds = static_cast<double>(s.window_capture_lead_seconds + arrival +
            s.source_start_seconds / scale);
        candidate.frequency_hz = (spread ? test_frequency_offset_hz : 0) - c.data_if_hz * (scale - 1);
        if (spread) candidate.channel.taps.push_back({c.spread_delay_seconds, c.spread_doppler_hz});
        std::vector<double> offsets(count);
        for (std::size_t i = 0; i < count; ++i) offsets[i] = i;
        std::vector<Complex> shaped(count * candidate.channel.taps.size());
        shaped_basis_mapped(source, candidate, c.sample_rate_hz, 0, offsets, shaped, c.data_if_hz);
        for (std::size_t i = 0; i < count; ++i) {
            f.iq[i] += .72 * shaped[i];
            if (spread) f.iq[i] += Complex(.42, .28) * shaped[count + i];
        }
    }
    return f;
}

ReceiverSlotScore score(const Fixture& f, unsigned workers = 1) {
    return score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, workers, workspace_limit);
}

void strict_preserves_wanted(const ReceiverSlotScore& wanted) {
    auto filtered = wanted;
    receiver_detail::apply_wrong_key_admission(filtered, WrongKeyAdmission::strict);
    check(wanted.decision == SlotDecision::data && filtered.decision == SlotDecision::data &&
          filtered.bit == wanted.bit && !filtered.wrong_key_rejected,
          "strict wrong-key policy lost a frozen weak/disturbed wanted control");
}

void complete_data() {
    const auto plain = fixture(0, false, false);
    const auto one = score(plain);
    check(one.decision == SlotDecision::data && one.bit == 0 && one.complete &&
          one.operational_passes_conditional_bound, "plain DATA zero must be admitted");

    const auto encrypted = fixture(1, true, true);
    const auto result = score(encrypted, 2);
    check(result.decision == SlotDecision::data && result.bit == 1 && result.complete &&
          result.all_valid && result.zero_h0_verified, "encrypted DATA one must be admitted");
    check(result.index == encrypted.slot.index && result.consumed_samples == encrypted.iq.size(),
          "complete score must retain the public slot identity and physical extent");
    check(!result.adaptive_diagnostics && !result.adaptive_transition_edges &&
          !result.adaptive_peak_states && !result.adaptive_blocks && result.affine_blocks > 1 &&
          result.coarse_workspaces >= receiver_epoch_offsets.size() * 2,
          "complete score must execute the full coarse and operational affine lane without adaptive diagnostics");
    check(result.selected_prior_mass > 0 && result.selected_prior_mass < 1,
          "fine selection must retain the original public grid prior");
    const auto serial = score(encrypted);
    check(serial.bit_evidence == result.bit_evidence &&
          serial.operational_bit_evidence == result.operational_bit_evidence &&
          serial.quiet_energy == result.quiet_energy && serial.bit == result.bit,
          "same-window evidence must match under independent worker shares");
}

void configured_carrier_reception() {
    const auto encrypted = fixture(1, true, true, 37, 12, 24, 1737);
    const auto result = score(encrypted);
    check(result.complete && result.decision == SlotDecision::data && result.bit == 1,
          "matching configured carrier did not admit the offset DATA waveform");
    // This signal is configured at 1737 Hz while a neighboring 1725-Hz
    // receiver has a +/-24-Hz search that covers its actual +12-Hz offset.
    // Carrier binding must change the receiver's evidence on the same IQ.
    // A strong foreign waveform can still beat the existing white-noise H0;
    // see the configured-carrier validation record rather than treating key
    // separation as an unconditional source-identity test.
    auto adjacent_frequency = encrypted;
    adjacent_frequency.slot.format.configured_carrier_hz = 1725;
    const auto mismatched = score(adjacent_frequency);
    check(mismatched.complete && result.operational_log_e > mismatched.operational_log_e,
          "configured carrier separation did not lower evidence for the foreign waveform");

    // One prespecified lower-gain control, retaining noise seed, geometry,
    // search bounds and the exact frequency difference (no seed search).
    const auto noise = fixture(std::nullopt, true, true, 37, 12, 24, 1737);
    auto weaker = encrypted;
    for (std::size_t i = 0; i < weaker.iq.size(); ++i)
        weaker.iq[i] = noise.iq[i] + .2 * (encrypted.iq[i] - noise.iq[i]);
    const auto weak_matching = score(weaker);
    weaker.slot.format.configured_carrier_hz = 1725;
    const auto weak_mismatched = score(weaker);
    std::cout << "carrier context: strong matched=" << result.operational_log_e
              << " foreign=" << mismatched.operational_log_e
              << " foreign_decision=" << static_cast<int>(mismatched.decision)
              << "; gain0.2 matched=" << weak_matching.operational_log_e
              << " foreign=" << weak_mismatched.operational_log_e
              << " threshold=" << weak_mismatched.operational_threshold << '\n';
    check(weak_matching.complete && weak_matching.decision == SlotDecision::data && weak_matching.bit == 1,
          "lower-gain matching carrier did not admit the transmitted bit");
    check(weak_mismatched.complete && weak_mismatched.decision != SlotDecision::data,
          "lower-gain foreign carrier was admitted by the broad frequency search");

    // Preserve the original false DATA as a regression control: key binding
    // alone does not reject this strong, unrelated keyed waveform.
    check(mismatched.decision == SlotDecision::data,
          "the frozen strong foreign-carrier control no longer exercises false DATA");
    adjacent_frequency.config.wrong_key_admission = WrongKeyAdmission::strict;
    const auto rejected = score(adjacent_frequency);
    check(rejected.decision == SlotDecision::ambiguous && !rejected.bit && rejected.wrong_key_rejected &&
          rejected.operational_passes_conditional_bound,
          "strict wrong-key policy must erase strong competing foreign DATA without inventing quiet");
    check(rejected.operational_bit_evidence == mismatched.operational_bit_evidence &&
          rejected.operational_log_e == mismatched.operational_log_e &&
          rejected.quiet_energy == mismatched.quiet_energy &&
          rejected.coarse_workspaces == mismatched.coarse_workspaces &&
          rejected.coarse_frequency_points == mismatched.coarse_frequency_points &&
          rejected.coarse_arrival_points == mismatched.coarse_arrival_points &&
          rejected.basis_rows == mismatched.basis_rows &&
          rejected.adaptive_transition_edges == mismatched.adaptive_transition_edges &&
          rejected.workspace_peak_bytes == mismatched.workspace_peak_bytes,
          "wrong-key admission must reuse exactly the ordinary DSP scores, work and workspace");
    for (const auto* wanted : {&result, &weak_matching}) {
        auto filtered = *wanted;
        receiver_detail::apply_wrong_key_admission(filtered, WrongKeyAdmission::strict);
        check(filtered.decision == SlotDecision::data && filtered.bit == 1 && !filtered.wrong_key_rejected,
              "strict policy lost the frozen matched strong or lower-gain control");
    }
}

void wrong_key_policy_boundaries() {
    using receiver_detail::apply_wrong_key_admission;
    const auto threshold = receiver_operational_threshold();
    ReceiverSlotScore candidate;
    candidate.decision = SlotDecision::data;
    candidate.bit = 0;
    candidate.complete = candidate.all_valid = true;
    candidate.operational_threshold = threshold;
    candidate.operational_bit_evidence = {threshold + 25., threshold + 20.};
    candidate.operational_bit_log_separation = 5.;
    candidate.operational_log_e = threshold + 24.;
    candidate.operational_passes_conditional_bound = true;
    auto current = candidate, guarded = candidate, strict = candidate;
    apply_wrong_key_admission(current, WrongKeyAdmission::current);
    apply_wrong_key_admission(guarded, WrongKeyAdmission::guarded);
    apply_wrong_key_admission(strict, WrongKeyAdmission::strict);
    check(current.decision == candidate.decision && current.bit == candidate.bit &&
          current.operational_bit_evidence == candidate.operational_bit_evidence &&
          current.wrong_key_required_log_separation == 0., "current policy must preserve admission exactly");
    check(guarded.decision == SlotDecision::data && strict.decision == SlotDecision::ambiguous &&
          strict.wrong_key_required_log_separation > guarded.wrong_key_required_log_separation &&
          strict.operational_passes_conditional_bound && !strict.bit,
          "wrong-key strength must monotonically tighten admission without changing the noise-bound receipt");

    // The extra penalty joins continuously at the competing bit's threshold;
    // no minimum fragment count or stronger penalty merely for more samples.
    for (double offset : {-1e-6, 0., 1e-6}) {
        auto boundary = candidate;
        boundary.operational_bit_evidence[1] = threshold + offset;
        apply_wrong_key_admission(boundary, WrongKeyAdmission::strict);
        check(std::abs(boundary.wrong_key_required_log_separation - 2 * std::numbers::ln10) < 1e-6,
              "competing-evidence penalty must be continuous at the ordinary threshold");
    }
    // A modeled weak wanted signal with only one supported bit remains usable.
    auto weak = candidate;
    weak.operational_bit_evidence = {threshold + .01, -10};
    weak.operational_bit_log_separation = threshold + 10.01;
    apply_wrong_key_admission(weak, WrongKeyAdmission::strict);
    check(weak.decision == SlotDecision::data, "strict policy must not impose an arbitrary higher noise threshold");
    for (const auto decision : {SlotDecision::quiet, SlotDecision::ambiguous}) {
        auto nondata = candidate;
        nondata.decision = decision;
        nondata.bit.reset();
        apply_wrong_key_admission(nondata, WrongKeyAdmission::strict);
        check(nondata.decision == decision && !nondata.wrong_key_rejected && !nondata.bit,
              "wrong-key policy must preserve quiet and erasures");
    }
    // The extended filters share the same score and remain nested. Each
    // coefficient receives an exact boundary and both adjacent outcomes.
    double previous_required = 0;
    for (const auto mode : wrong_key_admission_policies) {
        check(parse_wrong_key_admission(wrong_key_admission_name(mode)) == mode,
              "wrong-key policy tokens must round trip");
        auto filtered = candidate;
        apply_wrong_key_admission(filtered, mode);
        check(filtered.wrong_key_required_log_separation >= previous_required &&
              wrong_key_admission_passes(candidate, mode) == (filtered.decision == SlotDecision::data),
              "scalar admission and mutating gate must agree for every nested tier");
        previous_required = filtered.wrong_key_required_log_separation;
        if (mode == WrongKeyAdmission::current) continue;
        for (const auto offset : {-1e-9, 0., 1e-9}) {
            auto boundary = candidate;
            boundary.operational_bit_log_separation = previous_required + offset;
            check(wrong_key_admission_passes(boundary, mode) == (offset >= 0),
                  "every stronger tier must preserve its inclusive scalar boundary");
        }
    }
    // Use an exactly represented excess for the coefficient check: at the
    // operational threshold, (T + 20) - T can differ from 20 by one ULP.
    auto integer_excess = candidate;
    integer_excess.operational_threshold = 20.;
    integer_excess.operational_bit_evidence = {60., 40.};
    integer_excess.operational_bit_log_separation = 20.;
    integer_excess.operational_minimum_bit_log_separation = 10.;
    constexpr std::array expected_margins{0., 12., 15., 22.5, 41.25, 60.};
    for (std::size_t i = 0; i < wrong_key_admission_count; ++i) {
        auto filtered = integer_excess;
        apply_wrong_key_admission(filtered, wrong_key_admission_policies[i]);
        check(filtered.wrong_key_required_log_separation == expected_margins[i],
              "stronger levels must scale the whole Strict margin by the frozen reference multiplier");
    }
    // A below-threshold competitor used to leave every larger rho dormant.
    // Whole-margin scaling now acts on this finite-gap dominant-bit case.
    auto below_threshold = integer_excess;
    below_threshold.operational_bit_evidence = {26., 19.};
    below_threshold.operational_bit_log_separation = 7.;
    below_threshold.operational_minimum_bit_log_separation = 6.;
    check(wrong_key_admission_passes(below_threshold, WrongKeyAdmission::strict) &&
          !wrong_key_admission_passes(below_threshold, WrongKeyAdmission::very_strict) &&
          !wrong_key_admission_passes(below_threshold, WrongKeyAdmission::extreme) &&
          !wrong_key_admission_passes(below_threshold, WrongKeyAdmission::maximum),
          "stronger full-margin levels must remain active below the competing-score threshold");

    auto one_supported_bit = candidate;
    one_supported_bit.operational_bit_evidence[1] = -std::numeric_limits<double>::infinity();
    one_supported_bit.operational_bit_log_separation = std::numeric_limits<double>::infinity();
    WrongKeyAdmissionEvidence evidence;
    accumulate_wrong_key_admission(evidence, one_supported_bit);
    check(evidence.available && evidence.data_bits == 1 &&
          std::all_of(evidence.passing_bits.begin(), evidence.passing_bits.end(), [](auto count) { return count == 1; }),
          "legal absent alternative and infinite gap must remain rated under every tier");
    accumulate_wrong_key_admission(evidence, candidate);
    check(evidence.available && evidence.data_bits == 2 && evidence.passing_bits[0] == 2 &&
          evidence.passing_bits[1] == 2 && evidence.passing_bits[2] == 1 && evidence.passing_bits.back() == 1,
          "episode evidence must retain exact per-tier baseline DATA counts");
    auto quiet = candidate;
    quiet.decision = SlotDecision::quiet;
    quiet.bit.reset();
    accumulate_wrong_key_admission(evidence, quiet);
    check(evidence.available && evidence.data_bits == 2 && !wrong_key_admission_passes(quiet, WrongKeyAdmission::current),
          "quiet is never a rated DATA bit");
    auto unrated = candidate;
    unrated.operational_bit_evidence = {};
    unrated.operational_bit_log_separation = 0;
    accumulate_wrong_key_admission(evidence, unrated);
    accumulate_wrong_key_admission(evidence, one_supported_bit);
    check(!evidence.available && evidence.data_bits == 4 && evidence.passing_bits[0] == 3,
          "missing scalar evidence must stay unavailable after later rated bits");
    WrongKeyAdmissionEvidence already_filtered;
    auto filtered_baseline = candidate;
    apply_wrong_key_admission(filtered_baseline, WrongKeyAdmission::guarded);
    accumulate_wrong_key_admission(already_filtered, filtered_baseline);
    check(!already_filtered.available && already_filtered.data_bits == 1 && already_filtered.passing_bits[0] == 0,
          "already-filtered reception must not pretend to be unfiltered history");
    auto contradictory = candidate;
    contradictory.operational_minimum_bit_log_separation = candidate.operational_bit_log_separation + .25;
    WrongKeyAdmissionEvidence contradictory_evidence;
    accumulate_wrong_key_admission(contradictory_evidence, contradictory);
    check(!contradictory_evidence.available && contradictory_evidence.data_bits == 1 &&
          contradictory_evidence.passing_bits[0] == 0,
          "DATA contradicting its baseline minimum gap must remain unrated");
    contradictory.operational_minimum_bit_log_separation = candidate.operational_bit_log_separation;
    WrongKeyAdmissionEvidence inclusive_baseline;
    accumulate_wrong_key_admission(inclusive_baseline, contradictory);
    check(inclusive_baseline.available && inclusive_baseline.data_bits == 1 &&
          inclusive_baseline.passing_bits[0] == 1,
          "DATA at the exact baseline minimum gap remains rated");
    rejects<Error>([] { (void)parse_wrong_key_admission("10^-4/day"); }, "uncalibrated rate is not a policy token");
    rejects<Error>([&] { apply_wrong_key_admission(candidate, static_cast<WrongKeyAdmission>(99)); },
                   "invalid wrong-key policy must not silently select a mode");
    rejects<Error>([&] { (void)wrong_key_admission_passes(quiet, static_cast<WrongKeyAdmission>(99)); },
                   "scalar query must reject an invalid policy even for non-DATA");
}

void bounded_short_competition() {
    const auto declare_variation = [](Fixture& input) {
        input.config.time_varying_channel = true;
        input.config.time_varying_common_doppler_hz = 8;
        input.config.time_varying_atmospheric_hz = 8;
    };
    using namespace receiver_detail;
    const auto check_data = [&](Fixture input, std::uint8_t bit) {
        const auto affine = score(input, 2);
        declare_variation(input);
        check(affine.decision == SlotDecision::data && affine.bit == bit &&
              affine.operational_log_e > affine.operational_threshold + std::log(8.) &&
              affine.operational_bit_log_separation > input.config.time_varying_minimum_bit_log_separation &&
              varying_fine_plan(input.config, input.slot, 2).block_rows &&
              drift_plan(input.config, input.slot, 2).workspace_bytes,
              "short-model competition fixture must admit strong affine DATA and plan both alternatives");
        const auto competed = score(input, 2);
        check(competed.complete && competed.all_valid && competed.zero_h0_verified &&
              competed.consumed_samples == input.iq.size() && competed.decision == SlotDecision::data &&
              competed.bit == bit && competed.varying_candidates && competed.varying_blocks &&
              competed.varying_basis_rows && !competed.drift_timing_atoms &&
              competed.drift_status == "earlier_evidence_sufficient",
              "default admitted DATA must compare the short model without forcing the longer drift search");
    };
    check_data(fixture(1, true, false), 1);
    // Apply the same receiver allowance after rendering each undisturbed
    // bit. Neither the original IQ nor the public calendar changes.
    check_data(fixture(0, false, false), 0);
    check_data(fixture(1, false, false), 1);
    for (const std::uint8_t bit : {0, 1}) {
        auto moving = fixture(bit, false, false);
        declare_variation(moving);
        for (std::size_t i = 0; i < moving.iq.size(); ++i) {
            const auto time = static_cast<double>(i) / sample_rate;
            moving.iq[i] *= std::polar(1., 1.4 * std::sin(2 * std::numbers::pi * 1.25 * time));
        }
        const auto result = score(moving, 2);
        check(moving.config.clock_bound_ppm == 0 && result.varying_frequency_states > 1 &&
              result.midpoint_timing_status == "clock_axis_collapsed" &&
              !result.midpoint_timing_lags && !result.midpoint_timing_basis_rows &&
              result.complete && result.all_valid && result.zero_h0_verified &&
              result.decision == SlotDecision::data && result.bit == bit,
              "configured-epoch short tracking must retain both bits under moving frequency with ideal timing");
    }

    auto quiet = fixture(std::nullopt, false, false);
    const auto affine_quiet = score(quiet, 2);
    declare_variation(quiet);
    const auto competed_quiet = score(quiet, 2);
    check(affine_quiet.complete && affine_quiet.decision == SlotDecision::quiet &&
          competed_quiet.complete && competed_quiet.all_valid && competed_quiet.zero_h0_verified &&
          competed_quiet.noise_statistics_valid && competed_quiet.consumed_samples == quiet.iq.size() &&
          competed_quiet.decision == SlotDecision::quiet && !competed_quiet.bit &&
          competed_quiet.quiet_first_sample == affine_quiet.quiet_first_sample &&
          competed_quiet.quiet_stop_sample == affine_quiet.quiet_stop_sample &&
          competed_quiet.varying_candidates && competed_quiet.varying_quiet_modes &&
          competed_quiet.varying_quiet_energy <= competed_quiet.varying_quiet_threshold,
          "a broad receiver allowance must preserve the complete physical quiet observation");
}

// One short DATA window per case, same observation and declared static
// search with and without the added model. Exact channel slope, offset and
// timing go only to the independent waveform generator.
void bounded_drift_recovery() {
    const auto prepare = [](std::uint64_t seed) {
        auto input = fixture(std::nullopt, true, false, seed);
        input.config.frequency_bound_hz = 8;
        input.config.spread_delay_seconds = .007;
        input.config.spread_doppler_hz = 30;
        return input;
    };
    const auto synthesize = [](Fixture& input, std::uint8_t bit, int epoch, double lag,
                               double clock, double offset, double slope, double gain, Complex echo) {
        auto tx = input.slot.format;
        tx.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(tx.epoch) + epoch);
        PatternSource source(tx, input.key, bit);
        Candidate truth;
        truth.clock_ppm = clock;
        const auto scale = 1 + clock * 1e-6;
        truth.arrival_seconds = static_cast<double>(input.slot.window_capture_lead_seconds + lag +
            input.slot.source_start_seconds / scale);
        truth.frequency_hz = offset - input.config.data_if_hz * (scale - 1);
        truth.frequency_drift_hz_per_second = slope;
        truth.channel.taps.push_back({.007, 30});
        const auto rows = input.iq.size();
        std::vector<double> offsets(rows);
        for (std::size_t row = 0; row < rows; ++row) offsets[row] = row;
        std::vector<Complex> basis(2 * rows);
        shaped_basis_mapped(source, truth, input.config.sample_rate_hz, 0, offsets, basis,
                            input.config.data_if_hz);
        for (std::size_t row = 0; row < rows; ++row)
            input.iq[row] += gain * basis[row] + echo * basis[rows + row];
    };
    const auto check_both = [](Fixture input, std::uint8_t expected) {
        const auto original = score(input, 2);
        input.config.time_varying_channel = true;
        input.config.time_varying_common_doppler_hz = 8;
        const auto changed = score(input, 2);
        const auto original_bank = receiver_detail::score_slot_without_stationary_fading(
            input.config, input.slot, input.key, input.iq, input.valid, 2, workspace_limit, {}, nullptr);
        check(original.complete && original.decision != SlotDecision::data,
              "linear-drift discriminator no longer exercises the original loss of admission");
        check(changed.decision == SlotDecision::data && changed.bit == expected && changed.complete &&
              changed.all_valid && changed.zero_h0_verified && changed.operational_passes_conditional_bound &&
              changed.drift_timing_atoms > 0 && changed.drift_slopes > changed.drift_timing_atoms &&
              changed.drift_frequency_bins > 1 && changed.drift_bit_evidence[expected] >
                  changed.drift_bit_evidence[1 - expected],
              "bounded coherent drift search did not recover the DATA alternative");
        strict_preserves_wanted(changed);
        for (const auto* varying : {&original_bank, &changed})
            check(varying->complete && varying->all_valid && varying->zero_h0_verified &&
                  varying->coarse_workspaces == original.coarse_workspaces &&
                  varying->coarse_frequency_points == original.coarse_frequency_points &&
                  varying->coarse_arrival_points == original.coarse_arrival_points &&
                  varying->fine_candidates == original.fine_candidates &&
                  varying->basis_rows == original.basis_rows &&
                  varying->selected_prior_mass == original.selected_prior_mass,
                  "varying-channel scoring changed the original public search coverage or prior mass");
        check(!original.affine_stationary_fading_gain && !original_bank.affine_stationary_fading_gain &&
              original_bank.bit_evidence == original.bit_evidence,
              "excluding only the additive family must recover the exact original affine evidence");
        check(changed.affine_stationary_fading_gain &&
              changed.slow_gain_coherence_seconds == std::array<double, 2>{.5, 2.},
              "the short two-tap receiver must execute the added direct-path gain family");
        for (std::size_t bit = 0; bit < original.bit_evidence.size(); ++bit)
            check(changed.bit_evidence[bit] >= original_bank.bit_evidence[bit] + std::log1p(-.25),
                  "the weighted additive model must preserve the original affine contribution");
    };
    auto rising = prepare(37);
    const double duration = geometry(rising.slot.format).source_samples / effective_sample_rate(rising.slot.format);
    synthesize(rising, 0, 2, .004, 6, -6 - 12 * .004 / duration, 12 / duration, .05, {.03, .02});
    check_both(std::move(rising), 0);

    auto falling = prepare(579);
    const auto slope = -10 / duration;
    const auto start = static_cast<double>(falling.slot.window_capture_lead_seconds - .003 +
        falling.slot.source_start_seconds / (1 - 4e-6));
    synthesize(falling, 1, -3, -.003, -4, 5.7 - slope * start, slope, .08, {.025, .029});
    check_both(std::move(falling), 1);

    auto noise = prepare(2026);
    noise.config.time_varying_channel = true;
    noise.config.time_varying_common_doppler_hz = 8;
    const auto quiet = score(noise, 2);
    check(quiet.drift_timing_atoms > 0 && quiet.varying_candidates > 0 &&
          quiet.decision == SlotDecision::quiet && !quiet.bit && quiet.complete &&
          quiet.zero_h0_verified && !quiet.operational_passes_conditional_bound &&
          quiet.varying_quiet_modes > 0 && quiet.varying_quiet_energy <= quiet.varying_quiet_threshold,
          "the extra channel priors changed a scored quiet window into DATA");
}

void long_drift_recovery(bool broad) {
    constexpr double fs = 6000;
    Format format;
    format.sample_rate = fs;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    format.symbol_ordinal = 7000037;
    auto tx = format;
    tx.epoch += broad ? 0 : 3;
    const std::uint8_t bit = broad ? 0 : 1;
    const std::uint64_t seed = broad ? 37 : 983;
    const auto key = Crypto::amateur();
    PatternSource source(tx, key, bit);
    const auto g = geometry(format);
    StreamChannelConfig channel_config;
    channel_config.effects.sample_rate = fs;
    channel_config.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    channel_config.effects.rf_hz = 30e6;
    channel_config.effects.nominal_conversion_if_hz = 0;
    channel_config.effects.capture_lead_seconds = 2.017;
    channel_config.effects.capture_trail_seconds = broad ? 1. : .5;
    channel_config.effects.spread_delay_ms = 7;
    channel_config.effects.spread_doppler_hz = 30;
    channel_config.effects.static_doppler_hz = broad ? -6. : 5.5;
    channel_config.effects.seed = seed;
    channel_config.path_seed = seed;
    channel_config.noise_seed = seed;
    channel_config.signal_gain = .05;
    channel_config.complex_noise_variance = .25 / sample_rate * fs;
    channel_config.tx_frequency_drift_hz_per_second = (broad ? 12. : -10.) / g.bit_seconds;
    channel_config.maximum_chunk_samples = 8192;
    StreamingChannel channel(source, channel_config);
    check(channel.total_samples() < 230'000, "long drift fixture exceeds its sample bound");
    std::vector<Complex> iq(channel.total_samples());
    for (std::size_t first = 0; first < iq.size();) {
        const auto rows = std::min(channel_config.maximum_chunk_samples, iq.size() - first);
        check(channel.read(std::span(iq).subspan(first, rows)) == rows, "long drift capture truncated");
        first += rows;
    }
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.noise_variance = channel_config.complex_noise_variance;
    config.frequency_bound_hz = broad ? .009 : 8.;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = broad ? 90. : 8.;
    config.time_varying_atmospheric_hz = broad ? 89.991 : 0.;
    ReceiverSlotPlan slot;
    slot.stop_sample = iq.size();
    slot.format = format;
    slot.arrival_lower_seconds = broad ? -.5 : -.4;
    slot.arrival_upper_seconds = broad ? .5 : .4;
    slot.window_capture_lead_seconds = 2;
    slot.quiet_first_sample = static_cast<std::uint64_t>(std::ceil((2 + g.bit_seconds + .057) * fs));
    slot.quiet_stop_sample = slot.stop_sample;
    const auto result = score_receiver_slot(config, slot, key, iq, {}, 4, workspace_limit);
    check(result.complete && result.decision == SlotDecision::data && result.bit == bit &&
          result.varying_timing_blocks > 1 && result.varying_timing_frequencies > 1 &&
          result.drift_observation_rows > 131'072 && result.drift_proposal_stride > 1 &&
          result.drift_timing_atoms > 0 && result.drift_bit_evidence[bit] > result.drift_bit_evidence[1 - bit],
          "broad timing search missed the weak long drift after exceeding the old raw FFT limit");
    strict_preserves_wanted(result);
    if (broad)
        check(result.varying_timing_status == "segmented_smooth_frequency_timing" && result.coarse_arrival_points > 512,
              "the wide public prior did not execute the smooth timing proposal");
}

void weak_diffuse_fading_recovery() {
    constexpr double width = 1.5;
    Format format;
    format.sample_rate = sample_rate;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    format.symbol_ordinal = 7000037;
    const auto g = geometry(format);
    const auto key = Crypto::amateur();
    ReceiverConfig config;
    config.sample_rate_hz = sample_rate;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.random_doppler_width_hz = width;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    ReceiverSlotPlan slot;
    slot.format = format;
    slot.arrival_lower_seconds = -.4;
    slot.arrival_upper_seconds = .4;
    slot.window_capture_lead_seconds = 2;
    const auto run = [&](double gain) {
        PatternSource source(format, key, 0);
        StreamChannelConfig channel_config;
        channel_config.effects.sample_rate = sample_rate;
        channel_config.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
        channel_config.effects.rf_hz = 30e6;
        channel_config.effects.nominal_conversion_if_hz = 0;
        channel_config.effects.capture_lead_seconds = 2;
        channel_config.effects.capture_trail_seconds = .5;
        channel_config.effects.spread_delay_ms = 7;
        channel_config.effects.spread_doppler_hz = 30;
        channel_config.effects.static_doppler_hz = -6;
        channel_config.effects.seed = 101;
        channel_config.path_seed = 101;
        channel_config.noise_seed = 101;
        channel_config.signal_gain = gain;
        channel_config.path_evolution.random_doppler_width_hz = width;
        channel_config.complex_noise_variance = config.noise_variance;
        channel_config.maximum_chunk_samples = 8192;
        StreamingChannel channel(source, channel_config);
        check(channel.total_samples() < 19'000, "diffuse fading fixture exceeds its sample bound");
        std::vector<Complex> iq(channel.total_samples());
        for (std::size_t first = 0; first < iq.size();) {
            const auto rows = std::min(channel_config.maximum_chunk_samples, iq.size() - first);
            check(channel.read(std::span(iq).subspan(first, rows)) == rows, "diffuse fading capture truncated");
            first += rows;
        }
        slot.stop_sample = iq.size();
        const auto quiet = static_cast<std::uint64_t>(std::ceil((2 + g.bit_seconds + .007 + .05) * sample_rate));
        slot.quiet_first_sample = std::min(quiet, slot.stop_sample);
        slot.quiet_stop_sample = slot.stop_sample;
        return score_receiver_slot(config, slot, key, iq, {}, 4, workspace_limit);
    };
    const auto data = run(.05);
    check(data.complete && data.decision == SlotDecision::data && data.bit == 0 &&
          data.operational_passes_conditional_bound && data.varying_timing_blocks > 1 &&
          data.coherent_gain_candidates && data.coherent_gain_blocks > 100 &&
          data.coherent_gain_frequency_states > 1 && data.coherent_gain_bit_evidence[0] > data.coherent_gain_bit_evidence[1],
          "the receiver lost weak DATA under independent diffuse Gaussian ray gains");
    strict_preserves_wanted(data);
    const auto noise = run(0);
    check(noise.complete && !noise.bit && noise.decision != SlotDecision::data &&
          !noise.operational_passes_conditional_bound && noise.coherent_gain_blocks == data.coherent_gain_blocks,
          "the diffuse fading models mistook the matched noise observation for DATA");
}

void large_aperture_supplemental_plans() {
    using namespace receiver_detail;
    constexpr std::size_t former_raw_limit = 1'048'576;
    constexpr double fs = 32'000;
    Format format;
    format.sample_rate = fs;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    const auto source_rows = geometry(format).source_samples;
    check(source_rows > former_raw_limit && source_rows < 1'200'000,
          "large aperture fixture must cross the former raw limit within its sample budget");
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    ReceiverSlotPlan slot;
    slot.format = format;
    slot.stop_sample = source_rows + 128;
    slot.quiet_first_sample = 0;
    slot.quiet_stop_sample = source_rows;
    const auto plain = drift_plan(config, slot, 1);
    const auto projected = drift_plan(config, slot, 1, true);
    check(plain.workspace_bytes && plain.raw_rows == source_rows && plain.rows <= 131'072 &&
          projected.workspace_bytes > plain.workspace_bytes && projected.raw_rows == plain.raw_rows &&
          projected.projection_blocks > 1 && varying_line_workspace(config, slot) > 0,
          "large aperture supplemental plans must charge their bounded work");

    std::vector<Complex> observation(slot.stop_sample);
    constexpr double line_frequency = 81.2345;
    const auto step = std::polar(1., 2 * std::numbers::pi * line_frequency / fs);
    auto tone = Complex{.1};
    for (std::size_t row = 0; row < source_rows; ++row) {
        observation[row] = tone;
        tone *= step;
    }
    const auto line = find_varying_line(config, slot, observation, {});
    check(line && line->first == 0 && line->stop == source_rows &&
          std::abs(line->frequency_hz - line_frequency) < .0002 &&
          line->log_weighted_evidence > receiver_operational_threshold(),
          "bounded coherent line search did not rescore the complete large aperture");

    config.sample_rate_hz = 6000;
    slot.format.sample_rate = 6000;
    slot.format.bit_seconds = 270;
    slot.stop_sample = geometry(slot.format).source_samples + 128;
    const auto longer = drift_plan(config, slot, 1);
    check(longer.workspace_bytes && longer.raw_rows > former_raw_limit && longer.ridge_segments > 64 &&
          longer.rows <= 131'072,
          "long drift must shorten the proposal segments to retain coherent curvature coverage");
}

void long_calm_drift_owner_lease(bool active = true, std::uint8_t bit = 0, bool allocation = false,
                                bool cache_only = false) {
    using namespace receiver_detail;
    constexpr double actual_cn0 = -2.3044892137827393;
    const auto prepare = [&](double target) {
        live::Settings settings;
        settings.simulation = true;
        settings.device = "native-long-calm-regression";
        settings.calendar = live::Calendar{1793404800, 0};
        settings.transfer.key = Crypto::amateur();
        settings.transfer.fec = FecMode::off;
        settings.transfer.content_limit = 16;
        settings.transfer.modem.sample_rate = 1920;
        settings.transfer.modem.carrier_hz = 500;
        settings.transfer.modem.bandwidth_hz = 120;
        settings.transfer.modem.oscillator_search = modem::OscillatorSearchConfig{
            .lf = {100, .5}, .rf = {1, .05}, .rf_shift_hz = 3.4e6, .margin = 3,
            .reference = modem::OscillatorReference::independent_audio,
            .sideband = modem::OscillatorSideband::upper};
        settings.transfer.clock_sync = clock_sync::Policy{.001, .001, 0};
        settings.transfer.audio_timing_error_seconds = 0;
        settings.added_symbols = 1;
        settings.spread_preset = "hf-worst";
        settings.selective_perturbation = "each-65";
        settings.target_cn0_db_hz = target;
        settings.tx_dbm = 30;
        settings.noise_dbm_hz = -170;
        settings.path_loss_db = 200 - actual_cn0;
        settings.helper.power_w = 0;
        settings.helper.callsign = "TEST";
        settings.helper.timecode = true;
        live::validate_settings(settings);
        return live::detail::prepare_profile(settings, *settings.calendar, {}, {.simulation_seed = 62014});
    };
    const auto profile = prepare(-3);
    const auto longer = prepare(-9);
    constexpr unsigned workers = 24;
    constexpr std::size_t memory_limit = 6ULL * 1024 * 1024 * 1024;
    for (const auto* current : {&profile, &longer}) {
        const auto& config = current->receiver;
        const auto slot = make_receiver_slot_plan(current->waveform, config, 12);
        const auto preferred = drift_plan(config, slot, workers);
        check(!preferred.workspace_bytes &&
              preferred.status == "drift_public_bandwidth_exceeds_reduced_proposal_limit",
              "long broad fixture no longer exercises the preferred proposal bandwidth boundary");
        const auto lease = receiver_slot_workspace(config, slot, workers);
        const auto overlap = receiver_slot_overlap_workspace(config, slot, workers);
        const auto expected = current == &profile ? 693098478ULL : 2669814011ULL;
        check(lease == expected && overlap == lease,
              "optional long drift must preserve the original complete full-worker owner lease");
        const auto frozen = receiver_drift_admission(config, slot, workers);
        const auto overlapping = receiver_drift_admission(config, slot, workers, true);
        check(frozen.ordinary.workspace_bytes && frozen.line.workspace_bytes &&
              frozen.ordinary.nfft == overlapping.ordinary.nfft &&
              frozen.line.nfft == overlapping.line.nfft && frozen.workspace_bytes == lease &&
              frozen.overlap_workspace_bytes == overlap,
              "quote and execution orchestration must freeze the same admitted long-drift plans");
        const auto rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
        const auto allowance = lease - rows * (sizeof(Complex) + sizeof(std::uint8_t)) - 64 * 1024;
        const auto expanded = drift_plan(config, slot, workers, false, allowance);
        const auto line = drift_plan(config, slot, workers, true, allowance);
        check(expanded.workspace_bytes && line.workspace_bytes &&
              expanded.workspace_bytes <= allowance && line.workspace_bytes <= allowance &&
              expanded.nfft == (current == &profile ? 524288ULL : 2097152ULL) &&
              expanded.sample_stride == 2 && expanded.slope_budget >= 3,
              "the long drift proposal must retain bandwidth and a fair zero/both-bit slope bank");
        const auto ordinary_only = drift_plan(config, slot, workers, false, expanded.workspace_bytes);
        const auto line_narrower = drift_plan(config, slot, workers, true, expanded.workspace_bytes);
        check(ordinary_only.workspace_bytes && line_narrower.workspace_bytes &&
              line_narrower.stage_workers < ordinary_only.stage_workers &&
              line_narrower.workspace_bytes <= expanded.workspace_bytes,
              "the line shape must choose its own largest fitting stage width");
        const auto rejected = drift_plan(config, slot, workers, false, 1);
        check(!rejected.workspace_bytes && rejected.status == "drift_extension_exceeds_preserved_owner_lease" &&
              receiver_slot_workspace(config, slot, workers) == lease,
              "an optional drift extension beyond its preserved allowance must not invalidate baseline work");
    }
    auto narrow_clock = profile.receiver;
    narrow_clock.clock_bound_ppm = narrow_clock.endpoint_clock_bound_ppm = 0;
    auto narrow_options = profile.waveform.config();
    narrow_options.clock_bound_ppm = narrow_options.audio_pair_ppm = 0;
    const PublicProfile narrow_profile(narrow_options, profile.key);
    const auto narrow_slot = make_receiver_slot_plan(narrow_profile, narrow_clock, 12);
    const auto smaller = receiver_drift_admission(narrow_clock, narrow_slot, 8);
    const auto larger = receiver_drift_admission(narrow_clock, narrow_slot, workers);
    // This reproduces the formerly omitted model at an eight-lane owner
    // shape, which automatic W24 can also admit as part of a balanced group.
    check(smaller.workspace_bytes == 276033982ULL && larger.workspace_bytes == 453996094ULL &&
          smaller.ordinary.workspace_bytes && smaller.line.workspace_bytes &&
          smaller.ordinary.stage_workers > 0 && smaller.ordinary.stage_workers < 8 &&
          smaller.line.stage_workers > 0 && smaller.line.stage_workers < 8 &&
          larger.ordinary.stage_workers == 8 && larger.line.stage_workers == 8 &&
          smaller.ordinary.nfft == larger.ordinary.nfft &&
          smaller.ordinary.slope_budget == larger.ordinary.slope_budget &&
          smaller.ordinary.frequency_radius == larger.ordinary.frequency_radius &&
          smaller.ordinary.slope_radius == larger.ordinary.slope_radius &&
          receiver_slot_workspace(narrow_clock, narrow_slot, 8) == smaller.workspace_bytes &&
          receiver_drift_admission(narrow_clock, narrow_slot, 8, true).ordinary.stage_workers == smaller.ordinary.stage_workers,
          "a smaller stage shape must preserve all drift hypotheses inside the original owner lease");
    std::cerr << "drift stage widths W8=" << smaller.ordinary.stage_workers << '/' << smaller.line.stage_workers
        << " W24=" << larger.ordinary.stage_workers << '/' << larger.line.stage_workers << '\n';
    // Exercise the actual complete-slot planner's optional exception
    // boundary through its supported backend quote interface, without DSP.
    class PlanningErrorBackend final : public ReceiverBulkBackend {
    public:
        PlanningErrorBackend(unsigned fail_at, bool allocation = false, bool cancellation = false)
            : fail_at_(fail_at), allocation_(allocation), cancellation_(cancellation) {}
        std::unique_ptr<FineBulk> make_fine(double, double) override { return {}; }
        std::unique_ptr<CoarseBulk> make_coarse(double, double) override { return {}; }
        std::size_t fine_host_bytes(std::size_t, std::size_t) const override {
            if (++calls_ == fail_at_) {
                if (allocation_) throw std::bad_alloc{};
                throw Error(cancellation_ ? "Robust reception cancelled" : "mapped basis workspace overflow");
            }
            return 0;
        }
        std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
        BulkReceipt receipt() const override { return {}; }
    private:
        unsigned fail_at_;
        bool allocation_, cancellation_;
        mutable unsigned calls_ = 0;
    };
    auto backend_config = profile.receiver;
    backend_config.bulk_backend = std::make_shared<PlanningErrorBackend>(0);
    const auto backend_lease = receiver_slot_workspace(backend_config,
        make_receiver_slot_plan(profile.waveform, backend_config, 12), workers);
    const auto backend_slot = make_receiver_slot_plan(profile.waveform, backend_config, 12);
    backend_config.bulk_backend = std::make_shared<PlanningErrorBackend>(2);
    const auto optional_error = receiver_drift_admission(backend_config, backend_slot, workers);
    check(!optional_error.ordinary.workspace_bytes &&
          optional_error.ordinary.status == "drift_extension_shape_unavailable" &&
          optional_error.workspace_bytes == backend_lease,
          "an optional backend extent error must retain the mandatory baseline lease");
    backend_config.bulk_backend = std::make_shared<PlanningErrorBackend>(1);
    rejects<Error>([&] { receiver_drift_admission(backend_config, backend_slot, workers); },
        "mandatory baseline planning errors must propagate");
    backend_config.bulk_backend = std::make_shared<PlanningErrorBackend>(2, true);
    rejects<std::bad_alloc>([&] { receiver_drift_admission(backend_config, backend_slot, workers); },
        "allocation failure must not be reclassified as an optional unsupported shape");
    backend_config.bulk_backend = std::make_shared<PlanningErrorBackend>(2, false, true);
    rejects<Error>([&] { receiver_drift_admission(backend_config, backend_slot, workers); },
        "cancellation must not be reclassified as an optional unsupported shape");
    if (!active) return;
    const auto& config = profile.receiver;
    const auto slot = make_receiver_slot_plan(profile.waveform, config, 12);
    const auto rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
    check(rows < 400000 && std::abs(profile.waveform.geometry().bit_seconds - 176.8) < 1e-10,
          "long calm integration fixture exceeds its declared geometry");
    std::vector<Complex> iq(rows);
    PatternSource source(slot.format, profile.key, bit);
    std::mt19937_64 random(83041);
    std::normal_distribution<double> normal(0, std::sqrt(config.noise_variance / 2));
    for (std::size_t i = 0; i < rows; ++i) {
        const auto t = static_cast<long double>(slot.first_sample + i) / config.sample_rate_hz -
            profile.waveform.config().capture_lead_seconds;
        const auto at = (t - slot.source_start_seconds + .0005L - config.spread_delay_seconds) * config.sample_rate_hz;
        const auto phase = std::polar(1., 2 * std::numbers::pi *
            static_cast<double>(std::remainder(-180.L * t, 1.L)));
        iq[i] = config.data_basis_gain * source.sample_at(at) * phase + Complex{normal(random), normal(random)};
    }
    if (bit == 0) {
        std::array<FineCoordinate, 9> timings;
        for (std::size_t i = 0; i < timings.size(); ++i)
            timings[i] = {static_cast<std::int64_t>(i) - 4, -.0005, 0, 0};
        const auto plan = receiver_drift_admission(config, slot, workers).ordinary;
        ReceiverExecutor executor(workers);
        ReceiverSlotScore cached, recomputed;
        score_drift_fine(config, slot, profile.key, plan, iq, timings, {}, workers, cached, {}, executor);
        score_drift_fine(config, slot, profile.key, plan, iq, timings, {}, workers, recomputed, {}, executor,
            nullptr, DriftZeroSlopeEvaluation::recompute);
        const auto difference = std::max(std::abs(cached.drift_bit_evidence[0] - recomputed.drift_bit_evidence[0]),
            std::abs(cached.drift_bit_evidence[1] - recomputed.drift_bit_evidence[1]));
        check(difference < 1e-9 && cached.drift_timing_atoms == recomputed.drift_timing_atoms &&
              cached.drift_slopes == recomputed.drift_slopes && cached.drift_coherent_timing_fft_rows > 0,
              "cached zero-slope frontiers must preserve recomputed native likelihood and selected public atoms");
        std::cerr << "cached/recomputed drift evidence max difference=" << difference << '\n';
    }
    if (cache_only) return;
    const auto score = score_receiver_slot(config, slot, profile.key, iq, {}, workers, memory_limit);
    check(score.complete && score.all_valid && score.zero_h0_verified && score.bit == bit &&
          score.decision == SlotDecision::data && score.operational_passes_conditional_bound &&
          score.drift_relative_doppler_models == 2 && score.drift_slopes >= 6 &&
          score.drift_timing_atoms >= 16 && score.drift_timing_atoms <= 20 &&
          score.drift_coherent_timing_fft_rows > 0 && score.drift_stage_workers == 8 &&
          score.drift_status == "linear_drift_ridge_proposals_within_preserved_owner_lease" &&
          score.drift_line_status == "eligible_not_executed",
          "ordinary reception lost a long calm delayed ray under conservative GUI bounds");
    if (allocation) {
        check(narrow_slot.first_sample >= slot.first_sample && narrow_slot.stop_sample <= slot.stop_sample,
              "the zero-clock diagnostic aperture must be a subspan of the same physical input");
        const auto cropped = std::span<const Complex>(iq).subspan(narrow_slot.first_sample - slot.first_sample,
            narrow_slot.stop_sample - narrow_slot.first_sample);
        const auto eight = score_receiver_slot(narrow_clock, narrow_slot, profile.key, cropped, {}, 8, memory_limit);
        const auto full = score_receiver_slot(narrow_clock, narrow_slot, profile.key, cropped, {}, workers, memory_limit);
        check(eight.decision == SlotDecision::data && eight.bit == bit &&
              full.decision == SlotDecision::data && full.bit == bit &&
              eight.drift_stage_workers == smaller.ordinary.stage_workers &&
              full.drift_stage_workers == larger.ordinary.stage_workers &&
              eight.workspace_peak_bytes == smaller.workspace_bytes && full.workspace_peak_bytes == larger.workspace_bytes &&
              std::abs(eight.operational_bit_evidence[0] - full.operational_bit_evidence[0]) < 1e-9 &&
              std::abs(eight.operational_bit_evidence[1] - full.operational_bit_evidence[1]) < 1e-9,
              "different admitted stage widths must preserve ordinary decisions, likelihoods and owner leases");
    }
}

void broad_frequency_timing_plan() {
    using namespace receiver_detail;
    ReceiverConfig config;
    config.sample_rate_hz = 6000;
    config.noise_variance = .25;
    config.frequency_bound_hz = .009;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_atmospheric_hz = 89.991;
    ReceiverSlotPlan slot;
    slot.stop_sample = 320'000;
    slot.arrival_lower_seconds = -7;
    slot.arrival_upper_seconds = 7.07;
    slot.window_capture_lead_seconds = 9;
    slot.format.sample_rate = 6000;
    slot.format.inner_rate_hz = 36;
    slot.format.added_symbols = 10;
    slot.format.bit_seconds = 30;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    const auto plan = [&] {
        const FrequencyInterval oscillator{-config.frequency_bound_hz, config.frequency_bound_hz};
        return coarse_work_plan(config, slot, std::span(&oscillator, 1), 4);
    };
    const auto gps = plan();
    const auto gps_drift = drift_plan(config, slot, 4);
    config.frequency_bound_hz = 8;
    config.time_varying_atmospheric_hz = 82;
    const auto wide_oscillator = plan();
    const auto wide_drift = drift_plan(config, slot, 4);
    config.frequency_bound_hz = 90;
    config.time_varying_atmospheric_hz = 0;
    const auto explicit_doppler = plan();
    const auto explicit_drift = drift_plan(config, slot, 4);
    const auto same_timing = [&](const CoarseWorkPlan& other) {
        return gps.arrival_points == other.arrival_points && gps.workspaces == other.workspaces &&
            gps.segmented_timing_blocks == other.segmented_timing_blocks &&
            gps.segmented_timing_frequencies == other.segmented_timing_frequencies &&
            gps.segmented_timing_hop == other.segmented_timing_hop;
    };
    const auto same_drift = [&](const DriftPlan& other) {
        return gps_drift.sample_stride == other.sample_stride && gps_drift.rows == other.rows &&
            gps_drift.nfft == other.nfft && gps_drift.slope_budget == other.slope_budget &&
            gps_drift.frequency_radius == other.frequency_radius && gps_drift.slope_radius == other.slope_radius &&
            gps_drift.ridge_segments == other.ridge_segments && gps_drift.slow_ridge_segments == other.slow_ridge_segments;
    };
    check(gps.arrival_points > 8000 && gps.workspaces == 2 * receiver_epoch_offsets.size() &&
          gps.segmented_timing_blocks > 1 && gps.segmented_timing_frequencies > 400 && gps.segmented_timing_hop == 2 &&
          gps_drift.slow_ridge_segments > 0 && same_timing(wide_oscillator) && same_timing(explicit_doppler) &&
          same_drift(wide_drift) && same_drift(explicit_drift),
          "broad timing support must depend on the public total frequency bound");
    config.frequency_bound_hz = .009;
    for (const auto bound : {30., 32., 33.}) {
        config.time_varying_atmospheric_hz = bound - config.frequency_bound_hz;
        config.time_varying_common_doppler_hz = bound;
        const auto narrower = plan();
        check(narrower.segmented_timing_blocks > 1 && narrower.segmented_timing_frequencies > 1 &&
              narrower.segmented_timing_hop > 0 && narrower.arrival_points > 7000 &&
              narrower.workspaces == gps.workspaces,
              "narrower frequency bounds removed broad public timing proposals");
    }
    config.time_varying_atmospheric_hz = 89.991;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_channel = false;
    const auto off = plan();
    check(!off.segmented_timing_blocks && !off.segmented_timing_hop &&
          gps.workspace_reserved_bytes > off.workspace_reserved_bytes,
          "broad frequency timing must reserve its concurrent proposal state");
}

void bind_varying_calendar(Fixture& input) {
    input.config.data_if_hz = 100;
    PublicProfileConfig public_config;
    public_config.format = input.slot.format;
    public_config.data_if_hz = input.config.data_if_hz;
    public_config.cfo_bound_hz = input.config.frequency_bound_hz + input.config.time_varying_atmospheric_hz;
    public_config.capture_lead_seconds = .4;
    public_config.capture_trail_seconds = .12;
    const PublicProfile profile(public_config);
    input.slot = make_receiver_slot_plan(profile, input.config, 0);
    const auto rows = input.slot.stop_sample - input.slot.first_sample;
    check(rows <= input.iq.size(), "calendar line fixture must use the same full seeded noise prefix");
    input.iq.resize(rows);
    input.valid.resize(rows);
}

void varying_quiet_band(double line_amplitude, bool half_bin, bool calendar_bound = false) {
    auto input = fixture(std::nullopt, false, false);
    input.config.time_varying_channel = true;
    input.config.time_varying_common_doppler_hz = 90;
    input.config.time_varying_atmospheric_hz = 90;
    if (calendar_bound) {
        bind_varying_calendar(input);
        const auto quiet = score(input, 2);
        check(quiet.complete && quiet.zero_h0_verified && quiet.noise_statistics_valid &&
              quiet.calendar_opportunities_per_day == 1441 && quiet.operational_threshold == std::log(1441 / 1e-4) &&
              quiet.decision == SlotDecision::quiet,
              "calendar admission must preserve real observed quiet under the lower bound");
        input.valid[0] = 0;
        const auto erased = score(input, 2);
        check(!erased.complete && !erased.all_valid && !erased.bit && erased.decision == SlotDecision::ambiguous,
              "a calendar opportunity with a missing observation must not become DATA or quiet");
        input.valid[0] = 1;
    }
    const auto begin = input.slot.quiet_first_sample - input.slot.first_sample;
    const auto end = input.slot.quiet_stop_sample - input.slot.first_sample;
    const auto coarse_fft_spacing = sample_rate / static_cast<double>(std::bit_ceil(2 * (end - begin)));
    const auto frequency = half_bin ? (std::floor(80 / coarse_fft_spacing) + .5) * coarse_fft_spacing : 80.;
    // A non-DATA signal outside the old narrow DATA band but well within
    // the declared atmospheric extension must prevent physical completion.
    for (auto row = begin; row < end; ++row)
        input.iq[row] += std::polar(line_amplitude, 2 * std::numbers::pi * frequency * row / sample_rate);
    const auto expanded = score(input, 2);
    check(expanded.complete && !expanded.bit && expanded.decision == SlotDecision::ambiguous &&
          expanded.quiet_energy <= expanded.quiet_threshold &&
          expanded.varying_quiet_energy > expanded.varying_quiet_threshold &&
          expanded.reason == "coherent_line_in_public_data_aperture" &&
          std::abs(expanded.varying_line_frequency_hz - frequency) < .01,
          "energy in the declared varying DATA band became a scored quiet slot");

    // A keyed, independently drifting DATA carrier can coexist with the
    // same off-lattice line. Suppressing every supplemental decision on
    // detecting a line would fail this control.
    PatternSource source(input.slot.format, input.key, 1);
    Candidate truth;
    truth.arrival_seconds = calendar_bound ?
        static_cast<double>(input.slot.source_start_seconds + input.slot.window_capture_lead_seconds) : .4;
    const auto duration = geometry(input.slot.format).source_samples / effective_sample_rate(input.slot.format);
    truth.frequency_drift_hz_per_second = -9 / duration;
    truth.frequency_hz = 10.5 - truth.arrival_seconds * truth.frequency_drift_hz_per_second;
    std::vector<double> offsets(input.iq.size());
    std::vector<Complex> basis(input.iq.size());
    for (std::size_t row = 0; row < offsets.size(); ++row) offsets[row] = row;
    shaped_basis_mapped(source, truth, sample_rate, 0, offsets, basis, input.config.data_if_hz);
    for (std::size_t row = 0; row < basis.size(); ++row) input.iq[row] += .14 * basis[row];
    const auto coexist = score(input, 2);
    check(coexist.complete && coexist.decision == SlotDecision::data && coexist.bit == 1 &&
          coexist.reason == "varying_data_evidence_with_coherent_line" &&
          std::abs(coexist.varying_line_frequency_hz - frequency) < .01,
          "a coherent line suppressed a separable keyed DATA alternative");
}

void calendar_opportunity_bound() {
    using namespace receiver_detail;
    check(receiver_operational_threshold() == std::log(receiver_reference_opportunities_per_day / 1e-4) &&
          ReceiverSlotPlan{}.calendar_admission.opportunities_per_day() == 0,
          "standalone and duration planning must retain the reference opportunity bound");
    PublicProfileConfig declaration;
    declaration.format.epoch = 1'793'850'000;
    declaration.format.symbol_ordinal = 309;
    declaration.format.sample_rate = 6000;
    declaration.format.bit_seconds = .001;
    declaration.format.inner_rate_hz = 120;
    declaration.format.added_symbols = 1;
    declaration.data_if_hz = 1700;
    declaration.capture_lead_seconds = .35;
    declaration.capture_trail_seconds = .2;
    declaration.arrival_low_seconds = -.05;
    declaration.arrival_high_seconds = .1;
    declaration.receiver_tail_samples = 53;
    declaration.receiver_filter_halo_samples = 17;
    ReceiverConfig config;
    config.sample_rate_hz = declaration.format.sample_rate;
    config.data_if_hz = declaration.data_if_hz;
    const PublicProfile off(declaration);
    check(off.calendar().minimum_stride_seconds() == 60 && off.maximum_slot_opportunities_per_day() == 1441,
          "the 60-second recurrence must count both endpoints of a nominal day");
    declaration.helper_enabled = config.helper_enabled = true;
    declaration.helper.power_w = 8;
    declaration.helper.data_power_w = 4;
    declaration.helper.callsign = "TRIAD";
    const PublicProfile on(declaration);
    check(on.calendar().minimum_stride_seconds() == 90 && on.maximum_slot_opportunities_per_day() == 961,
          "the Helper lead and UTC30 rounding must count the actual minimum recurrence");
    declaration.audio_pair_ppm = 200;
    declaration.clock_bound_ppm = config.clock_bound_ppm = 301;
    declaration.random_extra_bits = 3;
    declaration.wait_bits = 8;
    const PublicProfile waited(declaration);
    const auto minimum_stride = waited.calendar().minimum_stride_seconds();
    const auto count = waited.maximum_slot_opportunities_per_day();
    check(minimum_stride > 90 && count == 86'400 / minimum_stride + 1,
          "fixed waits change the proof and random waits cannot raise the opportunity count");
    for (const std::uint64_t index : {0, 1, 251, 252, 200'000}) {
        const auto planned = make_receiver_slot_plan(waited, config, index);
        const auto next = make_receiver_slot_plan(waited, config, index + 1);
        check(planned.calendar_admission.opportunities_per_day() == count &&
              next.calendar_admission.opportunities_per_day() == count &&
              planned.format.symbol_ordinal == declaration.format.symbol_ordinal + index &&
              next.stop_sample - planned.stop_sample >= minimum_stride * declaration.format.sample_rate - 1,
              "clock bounds, keyed ordinals and the random wait cycle must preserve the configured ready-time bound");
    }
    declaration.capture_lead_seconds = 1e100;
    check(PublicProfile(declaration).maximum_slot_opportunities_per_day() == 0,
          "an unboundable ready-time scale must leave admission at the reference limit");
}

void calendar_line_and_separation() {
    using namespace receiver_detail;
    auto input = fixture(std::nullopt, false, false);
    input.config.time_varying_channel = true;
    input.config.time_varying_atmospheric_hz = 90;
    input.config.time_varying_common_doppler_hz = 90;
    bind_varying_calendar(input);
    constexpr unsigned opportunities = 1441;
    const auto threshold = std::log(opportunities / 1e-4);
    const auto reference = receiver_operational_threshold();
    const auto between = (threshold + reference) / 2;

    // A coherent DC line has exact on-grid power. Use its unit-amplitude
    // score to isolate the public grid/determinant penalty, then set the
    // amplitude to exercise both line-selection thresholds deterministically.
    const auto rows = input.slot.quiet_stop_sample - input.slot.quiet_first_sample;
    const auto variance = input.config.noise_variance;
    const auto unit_power = (rows / variance) / (1 + variance / rows);
    input.iq.assign(input.iq.size(), Complex{1});
    const auto unit = find_varying_line(input.config, input.slot, input.iq, {});
    check(unit && unit->frequency_hz == 0., "line threshold fixture must lie on the exact DC grid");
    const auto amplitude = std::sqrt((between + unit_power - unit->log_weighted_evidence) / unit_power);
    input.iq.assign(input.iq.size(), Complex{amplitude});
    const auto bound_line = find_varying_line(input.config, input.slot, input.iq, {}, threshold);
    check(!find_varying_line(input.config, input.slot, input.iq, {}) && bound_line &&
          std::abs(bound_line->log_weighted_evidence - between) < 1e-8,
          "newly eligible native line evidence must pass the calendar threshold but miss the reference threshold");

    // Put a controlled DATA alternative between the two limits on the real
    // line observation. A newly eligible score must execute the nuisance
    // path and veto this raw-only alternative before the physical decision.
    SlotEvidenceSupplement probe;
    probe.fixed_weight = .125;
    probe.supported = [](const auto&, const auto&) { return true; };
    probe.workspace = [](const auto&, const auto&, unsigned) { return std::size_t{1}; };
    probe.score = [&](const auto&, const auto&, const auto&, auto, const auto&, unsigned, std::size_t,
                      std::stop_token, ReceiverExecutor&) {
        return std::array{-std::numeric_limits<double>::infinity(), between + 2 * std::numbers::ln2 - std::log(probe.fixed_weight)};
    };
    const auto full_score = [&](const ReceiverSlotPlan& slot) {
        const auto memory = receiver_slot_workspace_with_supplement(input.config, slot, 2, probe, true);
        return score_slot_with_supplement(input.config, slot, input.key, input.iq, input.valid, 2,
                                          memory, {}, nullptr, probe);
    };
    auto unbound = input.slot;
    unbound.calendar_admission = {};
    const auto old = full_score(unbound);
    const auto conditioned = full_score(input.slot);
    check(old.complete && old.all_valid && old.zero_h0_verified && old.operational_log_e >= threshold &&
          old.operational_log_e < reference && old.varying_line_log_evidence == 0. &&
          conditioned.complete && conditioned.all_valid && conditioned.zero_h0_verified && !conditioned.bit &&
          conditioned.decision == SlotDecision::ambiguous && conditioned.operational_threshold == threshold &&
          conditioned.reason == "coherent_line_in_public_data_aperture" &&
          std::abs(conditioned.varying_line_log_evidence - between) < 1e-8,
          "newly eligible bound DATA must be subject to the actual native coherent-line veto");

    ReceiverSlotScore row;
    row.complete = row.all_valid = row.zero_h0_verified = row.noise_statistics_valid = true;
    row.fine_candidates = 1;
    row.quiet_threshold = 10.;
    const AdmissionGrid singleton(0, 0, 0, 0, .01);
    const auto minimum = input.config.time_varying_minimum_bit_log_separation;
    const auto mass_penalty = singleton.log_count();
    row.bit_evidence = {between + mass_penalty, between + mass_penalty};
    classify_varying_slot(row, singleton, nullptr, nullptr, minimum, threshold);
    check(row.operational_passes_conditional_bound && row.decision == SlotDecision::ambiguous && !row.bit,
          "meeting the calendar presence threshold cannot admit near-tied bits or scored absence");
    row.bit_evidence = {between + mass_penalty + std::numbers::ln2, between + mass_penalty - 15};
    classify_varying_slot(row, singleton, nullptr, nullptr, minimum);
    check(row.decision == SlotDecision::quiet && !row.operational_passes_conditional_bound,
          "an unbound observation must miss the original presence threshold");
    classify_varying_slot(row, singleton, nullptr, nullptr, minimum, threshold);
    check(row.operational_passes_conditional_bound && row.decision == SlotDecision::data && row.bit == 0,
          "the calendar bound must admit valid separated evidence between the two presence limits");

    row.bit_evidence = {47. + mass_penalty, 35. + mass_penalty};
    classify_varying_slot(row, singleton, nullptr, nullptr, minimum);
    auto calendar = row;
    classify_varying_slot(calendar, singleton, nullptr, nullptr, minimum, threshold);
    calendar.calendar_opportunities_per_day = opportunities;
    apply_wrong_key_admission(row, WrongKeyAdmission::strict);
    apply_wrong_key_admission(calendar, WrongKeyAdmission::strict);
    check(calendar.wrong_key_required_log_separation == row.wrong_key_required_log_separation &&
          calendar.decision == row.decision,
          "calendar presence admission must preserve the wrong-key policy's reference pressure");
}

void varying_bit_separation() {
    using namespace receiver_detail;
    const AdmissionGrid grid(0, 0, 0, 0, .01);
    ReceiverSlotScore observation;
    observation.complete = observation.all_valid = observation.zero_h0_verified = observation.noise_statistics_valid = true;
    observation.fine_candidates = 1;
    observation.quiet_threshold = 10;
    observation.bit_evidence = {45., 44.};
    const auto minimum = ReceiverConfig{}.time_varying_minimum_bit_log_separation;
    classify_varying_slot(observation, grid, &grid, nullptr, minimum);
    check(observation.operational_passes_conditional_bound && observation.decision == SlotDecision::ambiguous &&
          !observation.bit && observation.operational_bit_log_separation < minimum,
          "near-tied bit evidence became DATA or scored silence");
    observation.varying_candidates = 1;
    observation.varying_bit_evidence = {40., 55.};
    classify_varying_slot(observation, grid, &grid, nullptr, minimum);
    check(observation.operational_passes_conditional_bound && observation.decision == SlotDecision::data &&
          observation.bit == 1 && observation.operational_bit_evidence[1] > observation.operational_bit_evidence[0] &&
          observation.operational_bit_log_separation >= minimum,
          "separated supplemental evidence did not resolve the earlier near tie");
}

void varying_helper_coexistence() {
    constexpr double fs = 1500, center = 475;
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = center;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    config.helper_enabled = true;
    config.helper.low_frequency_hz = 175;
    config.helper.auxiliary_frequency_hz = 1000;
    config.helper.frequency_hz = 1050;
    ReceiverSlotPlan slot;
    slot.first_sample = 20'000;
    slot.index = 37;
    slot.window_capture_lead_seconds = -2;
    slot.source_start_seconds = 4;
    slot.helper_begin_seconds = 2.5;
    slot.helper_end_seconds = 23;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.bit_seconds = .001;
    auto tx = slot.format;
    ++tx.epoch;
    const auto key = Crypto::amateur();
    const auto source_rows = geometry(tx).source_samples;
    const auto rows = static_cast<std::size_t>(3 * fs) + source_rows;
    check(rows < 23'000, "varying Helper coexistence fixture exceeds its sample bound");
    slot.stop_sample = slot.first_sample + rows;
    slot.quiet_first_sample = slot.first_sample + static_cast<std::size_t>(2 * fs);
    slot.quiet_stop_sample = slot.quiet_first_sample + source_rows;
    std::vector<Complex> observation(rows), pattern(rows);
    std::vector<double> offsets(rows);
    for (std::size_t row = 0; row < rows; ++row) offsets[row] = row;
    PatternSource source(tx, key, 1);
    Candidate truth;
    truth.arrival_seconds = 2;
    truth.frequency_drift_hz_per_second = 9 / (source_rows / fs);
    truth.frequency_hz = -4.5 - 2 * truth.frequency_drift_hz_per_second;
    shaped_basis_mapped(source, truth, fs, 0, offsets, pattern, center);
    for (std::size_t row = 0; row < rows; ++row) {
        const double t = row / fs;
        const auto tone = [t](double f) { return std::polar(1., 2 * std::numbers::pi * f * t); };
        // A deterministic off-band interferer and actual separated Helper
        // tones. The short public lead intentionally forces acquisition fallback.
        observation[row] = .45 * tone(.754877666 * fs);
        if (t > .5)
            observation[row] += .2 * (tone(175 - center) + tone(1050 - center)) + .06 * tone(1000 - center);
    }
    const auto lease = receiver_slot_overlap_workspace(config, slot, 2);
    const auto helper_only = score_receiver_slot(config, slot, key, observation, {}, 2, lease);
    check(helper_only.complete && helper_only.decision == SlotDecision::quiet &&
          helper_only.varying_frequency_states > 0 && helper_only.varying_quiet_modes > 0,
          "separated Helper tones created DATA or prevented the scored quiet control");
    for (std::size_t row = 0; row < rows; ++row) observation[row] += .04 * pattern[row];
    const auto data = score_receiver_slot(config, slot, key, observation, {}, 2, lease);
    check(data.complete && data.decision == SlotDecision::data && data.bit == 1 &&
          data.drift_timing_atoms > 0 && data.varying_frequency_states > 0 &&
          data.operational_bit_log_separation >= data.operational_minimum_bit_log_separation,
          "projected dynamic receiver did not recover the drift with Helper present");
}

void late_helper_timing_recovery(bool declared_delay) {
    using namespace receiver_detail;
    constexpr double fs = 1500, center = 475, clock_ppm = 100, auxiliary = 970;
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = center;
    config.noise_variance = .03;
    config.frequency_bound_hz = 8;
    config.clock_bound_ppm = 300;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = config.time_varying_atmospheric_hz = 36;
    // Keep the original samples and widen only the declared delay support.
    if (declared_delay) config.spread_delay_seconds = .007;
    config.helper_enabled = true;
    config.helper.low_frequency_hz = 175;
    config.helper.auxiliary_frequency_hz = auxiliary;
    config.helper.frequency_hz = 1050;
    ReceiverSlotPlan slot;
    slot.index = 12;
    slot.first_sample = 20'000;
    slot.source_start_seconds = 720;
    slot.window_capture_lead_seconds = 2.25L - slot.source_start_seconds;
    slot.helper_begin_seconds = slot.source_start_seconds - 1.5;
    slot.helper_end_seconds = slot.source_start_seconds + 20;
    slot.arrival_lower_seconds = -.0025;
    slot.arrival_upper_seconds = .0025;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.epoch = 1793405520;
    slot.format.symbol_ordinal = 12;
    slot.format.bit_seconds = .001;
    const auto source_rows = geometry(slot.format).source_samples;
    const auto rows = static_cast<std::size_t>(3 * fs) + source_rows;
    check(rows < 23'000, "late Helper comparison exceeds its fixed single-aperture budget");
    slot.stop_sample = slot.first_sample + rows;
    slot.quiet_first_sample = slot.first_sample + static_cast<std::size_t>(2 * fs);
    slot.quiet_stop_sample = slot.quiet_first_sample + source_rows;
    std::vector<Complex> noise(rows), common(rows), helper(rows), pattern(rows);
    std::vector<double> offsets(rows);
    std::mt19937_64 rng(62033);
    std::normal_distribution<double> normal(0, std::sqrt(config.noise_variance / 2));
    for (std::size_t i = 0; i < rows; ++i) {
        offsets[i] = i;
        noise[i] = {normal(rng), normal(rng)};
        const double t = i / fs;
        const auto tone = [t](double f) { return std::polar(1., 2 * std::numbers::pi * f * t); };
        common[i] = std::polar(1., 16 * std::sin(2 * std::numbers::pi * 1.7 * t) +
                                   7.2 * std::sin(2 * std::numbers::pi * .29 * t));
        if (t > .5) helper[i] = common[i] * (.2 * (tone(175 - center) + tone(1050 - center)) + .06 * tone(auxiliary - center));
    }
    const auto key = Crypto::amateur();
    auto quiet = noise;
    for (std::size_t i = 0; i < rows; ++i) quiet[i] += helper[i];
    const auto capture = [&](std::uint8_t bit) {
        Candidate truth;
        truth.clock_ppm = clock_ppm;
        const auto ratio = 1 + clock_ppm * 1e-6;
        truth.arrival_seconds = static_cast<double>(slot.window_capture_lead_seconds + .001 +
            slot.source_start_seconds / ratio);
        truth.frequency_hz = -center * (ratio - 1);
        PatternSource source(slot.format, key, bit);
        shaped_basis_mapped(source, truth, fs, 0, offsets, pattern, center);
        auto observation = noise;
        for (std::size_t i = 0; i < rows; ++i) {
            observation[i] += .55 * pattern[i] * common[i];
            observation[i] += helper[i];
        }
        return observation;
    };
    const auto planned = varying_fine_plan(config, slot, 2);
    const auto lease = receiver_slot_workspace(config, slot, 2);
    check(planned.track_frequency && !helper_workspace(config, slot),
          "late Helper fixture requires projected short tracking after unsupported acquisition");
    if (declared_delay)
        check(planned.nominal_independent_gain_candidates == 2 && lease <= workspace_limit,
              "declared delay must admit the two nominal spatial candidates with Helper enabled");
    const auto uses_midpoint = [](const ReceiverSlotScore& score) {
        return score.midpoint_timing_status == "public_midpoint_lags" && score.midpoint_timing_lags > 0 &&
            score.midpoint_timing_widened_inverses == 26 && score.midpoint_timing_candidates > 0 &&
            score.midpoint_timing_candidates <= maximum_midpoint_timing_coordinates &&
            score.midpoint_timing_basis_rows > 0 && score.varying_frequency_states > 0;
    };
    const auto uses_independent_gain = [&](const ReceiverSlotScore& score) {
        return score.complete && score.all_valid && score.zero_h0_verified &&
            score.first_sample == slot.first_sample && score.stop_sample == slot.stop_sample &&
            score.quiet_first_sample == slot.quiet_first_sample && score.quiet_stop_sample == slot.quiet_stop_sample &&
            score.consumed_samples == rows && score.workspace_peak_bytes == lease &&
            !score.helper_overlapped && score.helper_status == "insufficient_guaranteed_coarse_support" && uses_midpoint(score) &&
            score.nominal_independent_gain_candidates == 2 && score.nominal_independent_gain_frequency_states > 1 &&
            score.nominal_independent_gain_frequency_states <= 65 && score.nominal_independent_gain_basis_rows == 8 * rows &&
            std::ranges::all_of(score.nominal_independent_gain_bit_evidence,
                [](double value) { return std::isfinite(value); });
    };
    for (const std::uint8_t bit : {0, 1}) {
        const auto observation = capture(bit);
        const auto score = score_receiver_slot(config, slot, key, observation, {}, 2, lease);
        check(score.complete && score.all_valid && score.zero_h0_verified && score.decision == SlotDecision::data &&
              score.bit == bit && !score.helper_overlapped && uses_midpoint(score) &&
              score.helper_status == "insufficient_guaranteed_coarse_support",
              "late projected tracking lost DATA after unsuccessful Helper acquisition");
        if (declared_delay)
            check(uses_independent_gain(score), "late Helper DATA must score both nominal delay candidates within its lease");
    }
    const auto absent = score_receiver_slot(config, slot, key, quiet, {}, 2, lease);
    check(absent.complete && absent.all_valid && absent.zero_h0_verified && absent.noise_statistics_valid &&
          absent.decision == SlotDecision::quiet && !absent.bit && uses_midpoint(absent),
          "late midpoint tracking turned separated Helper tones and noise into DATA or ambiguity");
    if (declared_delay)
        check(uses_independent_gain(absent), "Helper-only quiet must score both nominal delay candidates within its lease");
}

void physical_quiet_and_limits() {
    auto f = fixture(std::nullopt, false, false);
    const auto bound = receiver_slot_workspace(f.config, f.slot, 1);
    check(bound > f.iq.size() * (sizeof(Complex) + 1) && bound <= workspace_limit,
          "admission must charge the entire observation and DSP bound");
    const auto quiet = score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound);
    check(quiet.decision == SlotDecision::quiet && !quiet.bit && quiet.complete &&
          quiet.zero_h0_verified && quiet.noise_statistics_valid && quiet.quiet_modes > 0 &&
          quiet.quiet_energy <= quiet.quiet_threshold, "quiet must be fully scored physical noise");
    rejects<ReceiverWorkspaceError>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound - 1);
    }, "one byte below declared workspace must fail admission");
    rejects<Error>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, std::span(f.iq).first(23), {}, 1, bound);
    }, "transport prefix cannot score a complete public slot");
    f.valid[50] = 0;
    const auto gap = score(f);
    check(gap.decision == SlotDecision::ambiguous && !gap.complete && !gap.noise_statistics_valid,
          "known capture erasure cannot supply physical quiet");
    f.valid[50] = 1;
    std::stop_source cancel;
    cancel.request_stop();
    rejects<Error>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound, cancel.get_token());
    }, "cancelled input cannot be scored");
    f.iq[50] = std::numeric_limits<double>::infinity();
    rejects<Error>([&] { (void)score(f); }, "nonfinite received samples must fail validation");
}

void helper_composite_observation() {
    // Exactly one transmitted DATA slot and one subsequent noise-only public
    // window. Pull real composite I/Q through the complete streaming channel.
    // The receiver receives neither the bit, actual offset nor source endpoint.
    constexpr unsigned fs = 6000;
    PublicProfileConfig settings;
    settings.format.sample_rate = fs;
    settings.format.inner_rate_hz = 120;
    settings.format.added_symbols = 1;
    settings.format.bit_seconds = .001;
    settings.format.pulse_shape = PulseShape::TaperedRrc;
    settings.format.encrypt = true;
    settings.format.epoch = 1793850000;
    settings.format.symbol_ordinal = 15;
    settings.helper_enabled = true;
    settings.helper.data_power_w = 4;
    settings.helper.power_w = 8;
    settings.helper.callsign = "TRIAD";
    settings.capture_lead_seconds = 2;
    settings.capture_trail_seconds = .4;
    settings.arrival_low_seconds = -.4;
    settings.arrival_high_seconds = .4;
    settings.cfo_bound_hz = 1.5;
    const auto key = Crypto::amateur();
    const PublicProfile profile(settings, key);
    const auto data = profile.slot(0), quiet = profile.slot(1);
    check(data.observation.size() < 360'000 && quiet.observation.size() < 360'000 &&
          quiet.observation.stop < 1'100'000 && data.observation.stop < quiet.observation.first,
          "Helper integration fixture exceeds its fixed sample budget");
    const std::array<std::uint8_t, 1> bits{1};
    EmissionOptions options;
    options.bits = bits;
    Emission emission(profile, key, options);
    StreamChannelConfig channel_config;
    channel_config.effects.sample_rate = fs;
    channel_config.effects.occupied_bandwidth_hz = profile.geometry().occupied_bandwidth_hz;
    channel_config.effects.nominal_conversion_if_hz = settings.data_if_hz;
    channel_config.effects.capture_lead_seconds = settings.capture_lead_seconds;
    channel_config.effects.capture_trail_seconds = settings.capture_trail_seconds;
    channel_config.effects.timing_offset_seconds = .18;
    channel_config.effects.static_doppler_hz = .357;
    channel_config.effects.seed = 37;
    channel_config.complex_noise_variance = .02;
    channel_config.minimum_capture_samples = quiet.observation.stop;
    StreamingChannel channel(emission.source(), channel_config);

    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = settings.data_if_hz;
    config.frequency_bound_hz = settings.cfo_bound_hz;
    config.noise_variance = channel_config.complex_noise_variance;
    config.data_basis_gain = 1 / std::sqrt(data_power_reference(profile.config().format, key).mean_norm);
    config.helper_enabled = true;
    config.helper = profile.helper_plan();

    struct Capture {
        ReceiverSlotPlan plan;
        std::vector<Complex> iq;
        std::vector<std::uint8_t> valid;
    };
    const auto observe = [&](const PublicSlot& slot) {
        const auto seconds = [&](HelperBurstCoordinate coordinate) {
            return profile.calendar().relative_seconds(profile.origin(), coordinate);
        };
        Capture captured;
        auto& plan = captured.plan;
        plan.index = slot.index;
        plan.first_sample = slot.observation.first;
        plan.stop_sample = slot.observation.stop;
        plan.quiet_first_sample = slot.quiet_aperture.first;
        plan.quiet_stop_sample = slot.quiet_aperture.stop;
        plan.format = profile.basis_format(slot);
        plan.arrival_lower_seconds = settings.arrival_low_seconds;
        plan.arrival_upper_seconds = settings.arrival_high_seconds;
        plan.window_capture_lead_seconds = settings.capture_lead_seconds -
            static_cast<long double>(plan.first_sample) / fs;
        plan.source_start_seconds = seconds(slot.tx.support_begin);
        plan.helper_begin_seconds = seconds(slot.tx.helper_begin);
        plan.helper_end_seconds = seconds(slot.tx.helper_end);

        std::array<Complex, 4096> discard;
        while (channel.position() < plan.first_sample) {
            const auto take = std::min<std::uint64_t>(discard.size(), plan.first_sample - channel.position());
            check(channel.read(std::span(discard).first(take)) == take, "truncated Helper capture before window");
        }
        captured.iq.resize(slot.observation.size());
        captured.valid.resize(captured.iq.size());
        for (std::size_t i = 0; i < captured.iq.size();) {
            const auto take = std::min(discard.size(), captured.iq.size() - i);
            check(channel.read(std::span(captured.iq).subspan(i, take), std::span(captured.valid).subspan(i, take)) == take,
                  "truncated Helper capture in window");
            i += take;
        }
        return captured;
    };
    const auto score_mode = [&](const Capture& captured, NativeCoarsePrecision precision) {
        auto selected = config;
        selected.native_coarse_precision = precision;
        // The exact lease that lets Helper acquisition overlap preparation.
        return score_receiver_slot(selected, captured.plan, key, captured.iq, captured.valid, 2,
                                   receiver_slot_overlap_workspace(selected, captured.plan, 2));
    };
    const auto active_capture = observe(data);
    const auto active = score_mode(active_capture, NativeCoarsePrecision::fp32);
    const auto active_fp64 = score_mode(active_capture, NativeCoarsePrecision::fp64);
    check(emission.report().helper_energy > 0 && emission.report().data_energy > 0,
          "composite capture must contain actual DATA and Helper energy");
    check(active.complete && active.decision == SlotDecision::data && active.bit == 1,
          "Helper-assisted encrypted DATA must complete with the correct bit");
    check(active.helper_status == "pilot_aware_onset_relative_frequency_proposal",
          "real Helper lead must produce a narrowed frequency proposal");
    check(active.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
          active.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
          active_fp64.coarse_fft_precision == "fp64" &&
          active_fp64.coarse_precision_reason == "explicit-fp64",
          "complete Helper/DATA receiver did not execute the selected FP32 and FP64 coarse paths");
    check(active.coarse_workspaces == active_fp64.coarse_workspaces &&
          active.coarse_frequency_points == active_fp64.coarse_frequency_points &&
          active.fine_candidates == active_fp64.fine_candidates && active.bit == active_fp64.bit &&
          active.decision == active_fp64.decision && active.helper_status == active_fp64.helper_status &&
          active.adaptive_transition_edges == active_fp64.adaptive_transition_edges &&
          active.adaptive_peak_states == active_fp64.adaptive_peak_states &&
          active.affine_blocks == active_fp64.affine_blocks && active.adaptive_blocks == active_fp64.adaptive_blocks,
          "automatic post-filter FP32 changed complete Helper/DATA work or admission");
    const auto score_close = [](double a, double b) {
        return std::abs(a - b) <= 5e-6 * std::max(1., std::abs(b));
    };
    for (std::size_t i = 0; i < active.bit_evidence.size(); ++i)
        check(score_close(active.bit_evidence[i], active_fp64.bit_evidence[i]) &&
              score_close(active.operational_bit_evidence[i], active_fp64.operational_bit_evidence[i]),
              "automatic post-filter FP32 changed complete DATA evidence beyond tolerance");
    check(score_close(active.operational_log_e, active_fp64.operational_log_e) &&
          score_close(active.operational_threshold, active_fp64.operational_threshold),
          "automatic post-filter FP32 changed the DATA decision threshold beyond tolerance");
    const auto quiet_capture = observe(quiet);
    const auto absent = score_mode(quiet_capture, NativeCoarsePrecision::fp32);
    const auto absent_fp64 = score_mode(quiet_capture, NativeCoarsePrecision::fp64);
    check(absent.complete && absent.decision == SlotDecision::quiet && absent.noise_statistics_valid,
          "Helper-enabled receiver must score actual quiet after emission stops");
    check(absent.helper_status != "pilot_aware_onset_relative_frequency_proposal" &&
          active.coarse_frequency_points < absent.coarse_frequency_points,
          "coarse search must use the selected Helper region or full noise-only fallback");
    check(absent.decision == absent_fp64.decision && absent.bit == absent_fp64.bit &&
          absent.helper_status == absent_fp64.helper_status &&
          absent.coarse_workspaces == absent_fp64.coarse_workspaces &&
          absent.coarse_frequency_points == absent_fp64.coarse_frequency_points &&
          absent.fine_candidates == absent_fp64.fine_candidates &&
          score_close(absent.quiet_energy, absent_fp64.quiet_energy) &&
          score_close(absent.quiet_threshold, absent_fp64.quiet_threshold) &&
          score_close(absent.operational_log_e, absent_fp64.operational_log_e),
          "automatic post-filter FP32 changed failed-Helper quiet/null evidence");
    check(absent.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
          absent.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
          absent_fp64.coarse_fft_precision == "fp64" &&
          absent_fp64.coarse_precision_reason == "explicit-fp64",
          "failed-Helper quiet receiver did not report the selected CPU precision paths");
    // The sequential lease runs Helper acquisition before coarse preparation.
    // A lease with room for both overlaps them. Decisions, evidence and work
    // are the same, at every worker share, for DATA and for failed-Helper quiet.
    const auto same_score = [](const ReceiverSlotScore& a, const ReceiverSlotScore& b) {
        return a.decision == b.decision && a.bit == b.bit && a.complete == b.complete && a.all_valid == b.all_valid &&
            a.zero_h0_verified == b.zero_h0_verified && a.noise_statistics_valid == b.noise_statistics_valid &&
            a.operational_passes_conditional_bound == b.operational_passes_conditional_bound &&
            a.bit_evidence == b.bit_evidence && a.adaptive_bit_evidence == b.adaptive_bit_evidence &&
            a.combined_bit_evidence == b.combined_bit_evidence && a.quiet_energy == b.quiet_energy &&
            a.quiet_threshold == b.quiet_threshold && a.operational_log_e == b.operational_log_e &&
            a.operational_threshold == b.operational_threshold && a.selected_prior_mass == b.selected_prior_mass &&
            a.quiet_modes == b.quiet_modes && a.fine_candidates == b.fine_candidates &&
            a.coarse_workspaces == b.coarse_workspaces && a.coarse_frequency_points == b.coarse_frequency_points &&
            a.coarse_arrival_points == b.coarse_arrival_points && a.coarse_fft_precision == b.coarse_fft_precision &&
            a.coarse_precision_reason == b.coarse_precision_reason && a.affine_blocks == b.affine_blocks &&
            a.adaptive_blocks == b.adaptive_blocks && a.adaptive_peak_states == b.adaptive_peak_states &&
            a.adaptive_transition_edges == b.adaptive_transition_edges && a.basis_rows == b.basis_rows &&
            a.consumed_samples == b.consumed_samples && a.helper_status == b.helper_status && a.reason == b.reason;
    };
    for (const auto* captured : {&active_capture, &quiet_capture}) {
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        const auto lease = [&](unsigned workers, std::size_t bytes) {
            return score_receiver_slot(selected, captured->plan, key, captured->iq, captured->valid, workers, bytes);
        };
        const auto minimum = receiver_slot_workspace(selected, captured->plan, 2);
        const auto wider = receiver_slot_overlap_workspace(selected, captured->plan, 2);
        check(wider > minimum && wider < 256U * 1024 * 1024,
              "Helper overlap lease must exceed the sequential lease by its acquisition scratch only");
        const auto sequential = lease(2, minimum), overlapped = lease(2, wider), short_of_overlap = lease(2, wider - 1);
        check(!sequential.helper_overlapped && sequential.workspace_peak_bytes == minimum &&
              overlapped.helper_overlapped && overlapped.workspace_peak_bytes == wider &&
              !short_of_overlap.helper_overlapped && short_of_overlap.workspace_peak_bytes == minimum,
              "slot must overlap Helper acquisition exactly when its lease covers both");
        check(same_score(sequential, overlapped) && same_score(sequential, short_of_overlap),
              "overlapping Helper acquisition with coarse preparation changed a slot's score");
        rejects<ReceiverWorkspaceError>([&] { (void)lease(2, minimum - 1); },
                                        "Helper-enabled slot was admitted below its sequential lease");
        for (const unsigned workers : {1U, 5U})
            check(same_score(sequential, lease(workers, receiver_slot_overlap_workspace(selected, captured->plan, workers))) &&
                  same_score(sequential, lease(workers, receiver_slot_workspace(selected, captured->plan, workers))),
                  "Helper-enabled score changed with its worker share");
        // One lane runs acquisition and preparation in turn: one lease.
        const auto single = receiver_slot_workspace(selected, captured->plan, 1);
        check(receiver_slot_overlap_workspace(selected, captured->plan, 1) == single &&
              !lease(1, single).helper_overlapped, "a single lane was quoted or reported an overlap");
        // The measurement control never overlaps, whatever its lease.
        const auto control = receiver_detail::score_slot_in_sequence(selected, captured->plan, key, captured->iq,
            captured->valid, 2, wider, {}, nullptr);
        check(!control.helper_overlapped && control.workspace_peak_bytes == minimum && same_score(sequential, control),
              "the sequential control overlapped or changed a slot's score");
    }
    // Two owners on one shared pool each overlap, as stream slots do, and
    // score what they score alone.
    {
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        receiver_detail::ReceiverExecutor pool(4);
        const std::array captures{&active_capture, &quiet_capture};
        std::array<std::optional<ReceiverSlotScore>, 2> scored;
        std::array<std::exception_ptr, 2> failed;
        std::atomic<unsigned> published = 0;
        for (std::size_t i = 0; i < captures.size(); ++i)
            pool.submit([&, i] {
                try {
                    const auto& captured = *captures[i];
                    scored[i] = receiver_detail::score_slot(selected, captured.plan, key, captured.iq, captured.valid, 2,
                        receiver_slot_overlap_workspace(selected, captured.plan, 2), {}, &pool);
                } catch (...) {
                    failed[i] = std::current_exception();
                }
            }, [&] { ++published; });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (published != captures.size() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(published == captures.size(), "shared-pool Helper slots did not finish");
        pool.close();
        for (std::size_t i = 0; i < captures.size(); ++i) {
            if (failed[i]) std::rethrow_exception(failed[i]);
            const auto& alone = i ? absent : active;
            check(scored[i] && scored[i]->helper_overlapped && same_score(alone, *scored[i]),
                  "a Helper slot scored differently beside another owner on a shared pool");
        }
        check(pool.peak_running_roots() == 2, "shared-pool Helper slots did not run together");
    }
    // A known erasure still ends before any coarse work, on its sequential quote.
    {
        auto erased = active_capture;
        erased.valid[erased.valid.size() - 7] = 0;
        const auto gap = score_mode(erased, NativeCoarsePrecision::fp32);
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        check(gap.reason == "coarse_erasures_unsupported" && !gap.helper_overlapped && !gap.complete &&
              gap.decision == SlotDecision::ambiguous && !gap.coarse_workspaces && gap.coarse_seconds == 0 &&
              gap.helper_status == active.helper_status &&
              gap.workspace_peak_bytes == receiver_slot_workspace(selected, erased.plan, 2),
              "erased Helper-enabled slot must stop after acquisition without coarse work");
    }
    if (cpu_fft_fp32_qualified) {
        const auto compare_packed = [&](const Capture& capture, const ReceiverSlotScore& control) {
            const auto packed = score_mode(capture, NativeCoarsePrecision::automatic);
            auto selected = config;
            selected.native_coarse_precision = NativeCoarsePrecision::automatic;
            const auto in_sequence = receiver_detail::score_slot_in_sequence(selected, capture.plan, key, capture.iq,
                capture.valid, 2, receiver_slot_overlap_workspace(selected, capture.plan, 2), {}, nullptr);
            check(packed.helper_overlapped && !in_sequence.helper_overlapped && same_score(in_sequence, packed),
                  "Q15 slot changed when Helper acquisition overlapped its fused preparation");
            check(packed.complete && packed.all_valid && packed.zero_h0_verified &&
                  packed.coarse_precision_reason == "q15-products-fp32-fft" &&
                  packed.decision == control.decision && packed.bit == control.bit &&
                  packed.helper_status == control.helper_status &&
                  packed.coarse_frequency_points == control.coarse_frequency_points &&
                  packed.coarse_workspaces == control.coarse_workspaces &&
                  packed.coarse_arrival_points == control.coarse_arrival_points,
                  "Q15 changed a Helper-assisted or failed-Helper decision or public search coverage");
        };
        compare_packed(active_capture, active);
        compare_packed(quiet_capture, absent);
    }

    // The same input still contains Helper, including its energy in the
    // receiver's projection. Only a necessarily useless pilot fit is skipped.
    auto broad = config;
    broad.time_varying_channel = true;
    broad.time_varying_common_doppler_hz = broad.frequency_bound_hz;
    const auto score_broad = [&](const Capture& capture) {
        const auto workspace = receiver_slot_workspace(broad, capture.plan, 2);
        check(workspace == receiver_slot_overlap_workspace(broad, capture.plan, 2),
              "non-narrowing Helper must not reserve overlapping acquisition scratch");
        const auto result = score_receiver_slot(broad, capture.plan, key, capture.iq, capture.valid, 2, workspace);
        const std::array full{receiver_detail::FrequencyInterval{-broad.frequency_bound_hz, broad.frequency_bound_hz}};
        const auto full_grid = receiver_detail::coarse_work_plan(broad, capture.plan, full, 2);
        check(result.complete && result.all_valid && result.zero_h0_verified && result.noise_statistics_valid &&
              result.consumed_samples == capture.iq.size() && !result.helper_overlapped && result.helper_seconds == 0 &&
              result.helper_status == "common_motion_requires_full_data_frequency_prior" &&
              result.coarse_frequency_points == full_grid.frequency_points && result.workspace_peak_bytes == workspace,
              "the full-prior receiver must skip the fit and score the complete public aperture and grid");
        return result;
    };
    const auto broad_data = score_broad(active_capture);
    const auto broad_quiet = score_broad(quiet_capture);
    check(broad_data.decision == SlotDecision::data && broad_data.bit == 1 &&
          broad_quiet.decision == SlotDecision::quiet && !broad_quiet.bit,
          "the no-narrowing optimized path must preserve DATA and quiet decisions");
    auto invalid = active_capture.valid;
    invalid.back() = 2;
    const auto workspace = receiver_slot_workspace(broad, active_capture.plan, 2);
    rejects<Error>([&] { (void)score_receiver_slot(broad, active_capture.plan, key, active_capture.iq, invalid, 2, workspace); },
                   "omitting Helper bypassed full-aperture validity validation");
}

void native_precision_near_decision_sides() {
    // Reuse the frozen native/Vulkan receiver oracle's public slot geometry
    // and DATA attenuation bracket, but exercise this source-only CPU fixture
    // with no GPU dependency or duplicate receiver implementation.
    constexpr unsigned fs = 497;
    ReceiverConfig base;
    base.sample_rate_hz = fs;
    base.data_if_hz = 1725;
    base.noise_variance = .25;
    ReceiverSlotPlan slot;
    slot.index = 37;
    slot.first_sample = 20'000;
    slot.window_capture_lead_seconds = -17.7;
    slot.source_start_seconds = 18.1;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.bit_seconds = .001;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.encrypt = true;
    const auto key = Crypto::amateur();
    PatternSource source(slot.format, key, 1);
    const auto count = source.geometry().source_samples + 403;
    check(count < 6'000, "native precision near-decision fixture exceeded its fixed sample cap");
    slot.stop_sample = slot.first_sample + count;
    slot.quiet_first_sample = slot.first_sample + 230;
    slot.quiet_stop_sample = slot.first_sample + count - 223;
    std::vector<Complex> transmitted(count);
    for (std::size_t i = 0; i < count; ++i)
        transmitted[i] = .7 * source.sample_at((static_cast<long double>(i) / fs -
            slot.window_capture_lead_seconds - slot.source_start_seconds) * fs);

    const auto score_both = [&](std::span<const Complex> input) {
        auto floating = base;
        floating.native_coarse_precision = NativeCoarsePrecision::fp32;
        const auto low = score_receiver_slot(floating, slot, key, input, {}, 1,
            receiver_slot_workspace(floating, slot, 1));
        auto reference = base;
        reference.native_coarse_precision = NativeCoarsePrecision::fp64;
        const auto full = score_receiver_slot(reference, slot, key, input, {}, 1,
            receiver_slot_workspace(reference, slot, 1));
        check(low.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") && full.coarse_fft_precision == "fp64" &&
              low.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
              full.coarse_precision_reason == "explicit-fp64",
              "near-decision receiver did not execute both selected CPU arithmetic paths");
        check(low.complete && full.complete && low.bit == full.bit && low.decision == full.decision &&
              low.coarse_frequency_points == full.coarse_frequency_points &&
              low.coarse_workspaces == full.coarse_workspaces && low.fine_candidates == full.fine_candidates &&
              low.adaptive_transition_edges == full.adaptive_transition_edges &&
              low.adaptive_peak_states == full.adaptive_peak_states && low.affine_blocks == full.affine_blocks &&
              low.adaptive_blocks == full.adaptive_blocks,
              "near-decision FP32 receiver changed its winner or complete work coverage");
        const auto close = [](double a, double b) {
            return std::abs(a - b) <= 5e-6 * std::max(1., std::abs(b));
        };
        for (std::size_t i = 0; i < low.bit_evidence.size(); ++i)
            check(close(low.bit_evidence[i], full.bit_evidence[i]) &&
                  close(low.operational_bit_evidence[i], full.operational_bit_evidence[i]),
                  "near-decision FP32 changed the complete DATA evidence beyond tolerance");
        check(close(low.operational_log_e, full.operational_log_e) &&
              close(low.operational_threshold, full.operational_threshold),
              "near-decision FP32 changed the full receiver threshold evidence");
        return std::pair{low, full};
    };

    for (const auto attenuation : {.0615, .0621}) {
        auto input = transmitted;
        for (auto& sample : input) sample *= attenuation;
        const auto [low, full] = score_both(input);
        const auto expected_pass = attenuation > .062;
        check(std::abs(full.operational_log_e - full.operational_threshold) < .7 &&
              full.operational_passes_conditional_bound == expected_pass &&
              std::abs(low.operational_log_e - low.operational_threshold) < .7 &&
              low.operational_passes_conditional_bound == expected_pass,
              "FP32 changed either original near-decision DATA side of the frozen .7 threshold bracket");
        const auto direct = score_receiver_slot(base, slot, key, input, {}, 1,
            receiver_slot_workspace(base, slot, 1));
        check(direct.complete && direct.coarse_fft_precision == "fp64" &&
              direct.coarse_precision_reason == "exact-one-arrival-singleton-dot" &&
              direct.decision == full.decision && direct.bit == full.bit &&
              direct.operational_passes_conditional_bound == expected_pass &&
              direct.coarse_workspaces == full.coarse_workspaces &&
              direct.coarse_frequency_points == full.coarse_frequency_points &&
              direct.coarse_arrival_points == full.coarse_arrival_points,
              "automatic singleton dot changed a near-threshold decision or public search geometry");
        if (expected_pass) strict_preserves_wanted(direct);
        if (cpu_fft_fp32_qualified) {
            auto packed_config = base;
            packed_config.native_coarse_precision = NativeCoarsePrecision::q15;
            const auto packed = score_receiver_slot(packed_config, slot, key, input, {}, 1,
                receiver_slot_workspace(packed_config, slot, 1));
            check(packed.complete && packed.coarse_fft_precision == "fp32" &&
                  packed.coarse_precision_reason == "q15-products-fp32-fft" &&
                  packed.decision == full.decision && packed.bit == full.bit &&
                  packed.operational_passes_conditional_bound == expected_pass &&
                  packed.coarse_workspaces == full.coarse_workspaces &&
                  packed.coarse_frequency_points == full.coarse_frequency_points &&
                  packed.coarse_arrival_points == full.coarse_arrival_points,
                  "Q15 changed a near-threshold decision or public search geometry");
        }
    }
}
void signed_relative_doppler_endpoints() {
    using namespace receiver_detail;
    const std::array first{std::log(4.), std::log(2.)};
    const std::array second{std::log(8.), std::log(3.)};
    const auto same = mix_drift_sign_evidence(first, 1, second, 1);
    const auto unequal = mix_drift_sign_evidence(first, 2, second, 5);
    const auto missing = mix_drift_sign_evidence(first, 1, {-INFINITY, -INFINITY}, 0);
    check(std::abs(std::exp(same[0]) * 2 - 6.) < 1e-12 &&
          std::abs(std::exp(unequal[1]) * 7 - 9.5) < 1e-12 &&
          std::abs(std::exp(missing[0]) - 2.) < 1e-12,
          "signed drift must retain half the prior with identical, unequal or absent timing selections");

    PublicProfileConfig settings;
    settings.format.sample_rate = 1920;
    settings.format.inner_rate_hz = 120;
    settings.format.added_symbols = 1;
    settings.format.bit_seconds = 2;
    settings.format.epoch = 1793404800;
    settings.format.encrypt = true;
    settings.data_if_hz = 500;
    settings.spread_delay_ms = 21;
    settings.spread_doppler_hz = 90;
    settings.cfo_bound_hz = 100.3505;
    settings.capture_lead_seconds = .25;
    const PublicProfile profile(settings);
    ReceiverConfig config;
    config.sample_rate_hz = 1920;
    config.data_if_hz = 500;
    config.frequency_bound_hz = 10.3505;
    config.spread_delay_seconds = .021;
    config.spread_doppler_hz = 90;
    config.time_varying_channel = true;
    config.time_varying_atmospheric_hz = 90;
    config.time_varying_common_doppler_hz = 90;
    config.noise_variance = 192;
    const auto key = Crypto::amateur();
    config.data_basis_gain = 1 / std::sqrt(data_power_reference(profile.config().format, key).mean_norm);
    const auto slot = make_receiver_slot_plan(profile, config, 0);
    const auto rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
    check(rows < 25'000, "signed Doppler endpoint fixture exceeds its finite sample budget");
    std::vector<Complex> iq(rows), noise(rows);
    std::mt19937_64 random(83041);
    std::normal_distribution<double> normal(0, std::sqrt(config.noise_variance / 2));
    for (auto& value : noise) value = {normal(random), normal(random)};
    const auto lease = receiver_slot_workspace(config, slot, 2);
    for (auto bit : {std::uint8_t{0}, std::uint8_t{1}}) {
        PatternSource source(slot.format, key, bit);
        for (auto sign : {-1, 1}) {
            for (std::size_t i = 0; i < rows; ++i) {
                const auto t = static_cast<long double>(i) / config.sample_rate_hz - slot.window_capture_lead_seconds;
                const auto at = (t - slot.source_start_seconds - config.spread_delay_seconds) * config.sample_rate_hz;
                const auto phase = std::polar(1., 2 * std::numbers::pi *
                    static_cast<double>(std::remainder(sign * 180.L * t, 1.L)));
                iq[i] = config.data_basis_gain * source.sample_at(at) * phase + noise[i];
            }
            const auto received = score_receiver_slot(config, slot, key, iq, {}, 2, lease);
            check(received.complete && received.all_valid && received.zero_h0_verified &&
                  received.decision == SlotDecision::data && received.bit == bit &&
                  received.nominal_independent_gain_frequency_states > 65 &&
                  received.nominal_independent_gain_frequency_status == "common_plus_relative_doppler" &&
                  received.drift_relative_doppler_models == 2,
                  "signed relative Doppler support lost an ordinary endpoint bit or its full frequency model");
        }
    }
    const auto quiet = score_receiver_slot(config, slot, key, noise, {}, 2, lease);
    check(quiet.complete && quiet.all_valid && quiet.zero_h0_verified && quiet.decision == SlotDecision::quiet &&
          !quiet.bit && quiet.drift_relative_doppler_models == 2,
          "signed Doppler likelihood admitted genuine noise");
    auto zero = config;
    zero.spread_doppler_hz = 0;
    const auto plan = drift_plan(zero, slot, 2);
    const std::array coordinate{FineCoordinate{0, 0, 0, 0}};
    ReceiverSlotScore single;
    ReceiverExecutor executor(2);
    score_drift_fine(zero, slot, key, plan, noise, coordinate, {}, 2, single, {}, executor);
    check(single.drift_relative_doppler_models == 1 && single.drift_timing_atoms == 1,
          "zero relative Doppler must retain the original single drift model and timing mass");
}

void signed_gain_timing_and_exact_admission() {
    using namespace receiver_detail;
    PublicProfileConfig profile_config;
    auto& format = profile_config.format;
    format.sample_rate = 1920;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 278.4;
    format.epoch = 1793404800;
    format.encrypt = true;
    profile_config.data_if_hz = 240;
    profile_config.clock_bound_ppm = 150;
    profile_config.spread_delay_ms = 7;
    profile_config.arrival_high_seconds = .01;
    profile_config.cfo_bound_hz = 90;
    profile_config.capture_lead_seconds = .25;
    ReceiverConfig config;
    config.sample_rate_hz = format.sample_rate;
    config.data_if_hz = profile_config.data_if_hz;
    config.clock_bound_ppm = profile_config.clock_bound_ppm;
    config.spread_delay_seconds = .007;
    config.time_varying_atmospheric_hz = 90;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_channel = true;
    config.random_doppler_width_hz = 30;
    config.noise_variance = .003;
    const std::array intervals{FrequencyInterval{0, 0}};
    const PublicProfile long_profile(profile_config);
    const auto long_slot = make_receiver_slot_plan(long_profile, config, 6);
    const auto long_plan = coarse_work_plan(config, long_slot, intervals, 2);
    check(long_plan.template_sampling_step == 1 &&
          long_plan.workspaces == 6 * receiver_epoch_offsets.size() && long_plan.rays == 2 &&
          long_plan.exact_raw_column_rows > 60'000'000 &&
          long_plan.exact_render_and_projected_rows > 30'000'000 &&
          long_plan.exact_preparation_fft_row_stages > (std::uint64_t{1} << 32) &&
          receiver_slot_workspace(config, long_slot, 2) > long_plan.workspace_reserved_bytes,
          "long late-clock exact work must retain every ray/clock/epoch and the full memory lease");
    const auto late_slot = make_receiver_slot_plan(long_profile, config, 5000);
    const auto late_rank = coarse_midpoint_timing_support(config, late_slot, 2);
    check(late_rank.available && late_rank.gain_status == "signed_gain_full_direct_without_fixed" &&
          late_rank.gain_sampled_blocks == 0 && late_rank.gain_direct_blocks == late_rank.gain_available_blocks &&
          late_rank.gain_direct_blocks > 0 && late_rank.gain_fft_stage_bound > (std::uint64_t{1} << 32),
          "the stationary sampling budget must not discard admissible full-aperture direct timing");

    format.bit_seconds = 1.;
    const PublicProfile short_profile(profile_config);
    const auto slot = make_receiver_slot_plan(short_profile, config, 5);
    const auto key = Crypto::amateur();
    PatternSource source(slot.format, key, 0);
    std::vector<Complex> iq(slot.stop_sample - slot.first_sample);
    constexpr long double realized_scale = 1 - 100e-6L;
    check(iq.size() < 25'000, "signed gain timing fixture must remain bounded");
    for (std::size_t i = 0; i < iq.size(); ++i) {
        const auto t = i / static_cast<long double>(format.sample_rate) - slot.window_capture_lead_seconds;
        const auto position = (t * realized_scale - slot.source_start_seconds) * format.sample_rate;
        const auto delayed = position - config.spread_delay_seconds * realized_scale * format.sample_rate;
        const auto phase = std::polar(1., .4 + .08 * std::sin(.013 * i));
        iq[i] = source.sample_at(position) + .5 * phase * source.sample_at(delayed);
    }
    const auto quote = coarse_workspace(config, slot, 2, {}, MidpointTimingEvaluation::verify);
    rejects<ReceiverWorkspaceError>([&] {
        (void)score_coarse(config, slot, key, iq, intervals, 2, quote - 1, {}, nullptr, MidpointTimingEvaluation::verify);
    }, "signed gain timing must not run one byte below its full coarse lease");
    const auto coarse = score_coarse(config, slot, key, iq, intervals, 2, quote,
                                     {}, nullptr, MidpointTimingEvaluation::verify);
    const auto& timing = *coarse.midpoint_timing;
    const auto signed_plan = coarse_midpoint_timing_support(config, slot, 2);
    check(timing.lag_count > coarse.arrival_points && timing.gain_proposal_count > 0 &&
          timing.gain_proposal_count <= receiver_signed_lag_gain_max_candidates &&
          timing.gain_fixed_proposal_count > 0 && timing.gain_fixed_proposal_count <= 2 &&
          timing.gain_fixed_proposal_count <= timing.gain_proposal_count && timing.gain_verified_correlations > 0 &&
          timing.gain_blocks <= timing.gain_sampled_blocks &&
          timing.gain_sampled_blocks <= timing.gain_available_blocks,
          "signed rank must check linear correlations and retain the fixed timing family");
    check(timing.gain_status == "signed_gain_full_direct" &&
          timing.gain_blocks <= timing.gain_direct_blocks &&
          timing.gain_direct_blocks <= signed_plan.gain_direct_blocks &&
          (timing.gain_sampled_blocks < timing.gain_available_blocks || timing.gain_direct_blocks == timing.gain_blocks) &&
          timing.gain_fft_stage_bound == signed_plan.gain_fft_stage_bound &&
          timing.gain_score_row_bound == signed_plan.gain_score_row_bound &&
          timing.gain_workspace_bytes == signed_plan.gain_workspace_bytes &&
          timing.gain_fft_row_stages <= timing.gain_fft_stage_bound &&
          timing.gain_fixed_frequency_points == signed_plan.gain_fixed_frequency_points &&
          timing.gain_frequency_points == timing.gain_fixed_frequency_points &&
          timing.gain_frequency_status == "common_frequency_only" &&
          timing.gain_score_rows == 2 * timing.lag_count *
              (timing.gain_fixed_frequency_points * timing.gain_blocks +
               timing.gain_frequency_points * timing.gain_direct_blocks) &&
          coarse.work.signed_gain_proposal.operations.fft_row_stages == timing.gain_fft_row_stages,
          "signed rank must charge real work across both fixed and full-aperture timing families");
    const auto strongest = timing.gain_proposals[0].coordinate;
    const auto lag = timing.gain_proposal_lags[0];
    check(strongest == midpoint_timing_coordinate(slot, timing.middle_seconds, config.clock_bound_ppm,
              static_cast<std::size_t>(coarse.projected_sample_rate_hz), lag, 0),
          "the retained timing atom must belong to the predeclared finite lag dictionary");
    for (const auto t : {slot.source_start_seconds, slot.source_start_seconds + short_profile.geometry().bit_seconds}) {
        const auto error = strongest.arrival_seconds + t * (1 / (1 + strongest.clock_ppm * 1e-6L) - 1 / realized_scale);
        check(std::abs(error) < .5 / short_profile.geometry().fine_chip_rate_hz,
              "receiver-derived signed timing must support both ends of the aged aperture");
    }
    const auto receiver_bound = receiver_slot_workspace(config, slot, 2);
    const auto result = score_receiver_slot(config, slot, key, iq, {}, 2, receiver_bound);
    check(result.complete && result.all_valid && result.zero_h0_verified &&
          result.decision == SlotDecision::data && result.bit == 0 &&
          result.signed_lag_gain_candidates == result.signed_lag_gain_proposal_maps &&
          result.signed_lag_gain_candidates > 0 &&
          result.signed_lag_gain_proposal_direct_scored_blocks == timing.gain_direct_blocks &&
          result.signed_lag_gain_proposal_fixed_maps == timing.gain_fixed_proposal_count &&
          result.signed_lag_gain_proposal_lags == timing.gain_proposal_lags &&
          result.signed_lag_gain_basis_rows == 4 * result.signed_lag_gain_candidates * iq.size(),
          "the ordinary aged-clock receiver must score the nominated family under both bit hypotheses");

    // At later calendar ages the short columns span only a fraction of the
    // lag FFT. Exercise two widths and both clock signs without rendering
    // a full calendar prefix. The diagnostic checks every tiled lag and
    // frequency for one block against an independent finite direct sum.
    for (auto bit : {std::uint8_t{0}, std::uint8_t{1}}) {
        auto aged_format = profile_config;
        if (bit) aged_format.format.added_symbols = 5;
        auto wide_config = config;
        wide_config.spread_doppler_hz = 90;
        const PublicProfile aged_profile(aged_format);
        const auto aged = make_receiver_slot_plan(aged_profile, wide_config, bit ? 192 : 128);
        const auto plan = coarse_midpoint_timing_support(wide_config, aged, 2);
        const auto stages_per_fft = std::uint64_t{plan.gain_fft_rows} * std::countr_zero(plan.gain_fft_rows);
        const auto old_bound = stages_per_fft * (plan.gain_sampled_blocks *
            (5 + 2 * plan.gain_fixed_frequency_points + 2 * plan.gain_frequency_points) +
            (plan.gain_available_blocks - plan.gain_sampled_blocks) * (3 + 2 * plan.gain_frequency_points));
        check(plan.available && plan.gain_sampled_blocks < plan.gain_available_blocks &&
              plan.gain_frequency_points > plan.gain_fixed_frequency_points &&
              plan.gain_frequency_status == "common_plus_relative_doppler" &&
              plan.gain_fft_stage_bound < old_bound && aged.stop_sample - aged.first_sample < 40'000,
              "aged signed-rank test must quote a bounded tiled transform reduction");
        const auto rate = 1 + (bit ? 75e-6L : -100e-6L);
        PatternSource aged_source(aged.format, key, bit);
        std::vector<Complex> aged_iq(aged.stop_sample - aged.first_sample);
        for (std::size_t i = 0; i < aged_iq.size(); ++i) {
            const auto t = i / static_cast<long double>(format.sample_rate) - aged.window_capture_lead_seconds;
            const auto position = (t * rate - aged.source_start_seconds) * format.sample_rate;
            const auto delayed = position - config.spread_delay_seconds * rate * format.sample_rate;
            aged_iq[i] = aged_source.sample_at(position) +
                std::polar(.5, .4 + .08 * std::sin(.013 * i)) * aged_source.sample_at(delayed);
        }
        const auto aged_quote = coarse_workspace(wide_config, aged, 2, {}, MidpointTimingEvaluation::verify);
        const auto tiled = score_coarse(wide_config, aged, key, aged_iq, intervals, 2, aged_quote,
                                        {}, nullptr, MidpointTimingEvaluation::verify);
        check(tiled.midpoint_timing.has_value(), "aged tiled signed gain timing did not execute");
        const auto& rank = *tiled.midpoint_timing;
        const auto score_extent = 2 * rank.lag_count * rank.gain_frequency_points;
        check(rank.gain_tiled_blocks > 0 && rank.gain_tiled_blocks + rank.gain_blocks <= rank.gain_direct_blocks &&
              rank.gain_tiled_verified_correlations == score_extent && rank.gain_proposal_count > 0 &&
              rank.gain_score_rows == 2 * rank.lag_count *
                  (rank.gain_fixed_frequency_points * rank.gain_blocks +
                   rank.gain_frequency_points * rank.gain_direct_blocks) &&
              rank.gain_fft_stage_bound == plan.gain_fft_stage_bound &&
              rank.gain_fft_row_stages <= rank.gain_fft_stage_bound &&
              rank.gain_workspace_bytes == plan.gain_workspace_bytes,
              "tiled signed gain timing must verify every lag/frequency on both bits and charge full work");
    }
}
}

int main(int argc, char** argv) {
    try {
        // Keep each deterministic channel stratum within its own bounded
        // CTest invocation; key changes can alter which fallback model runs.
        if (argc == 2) {
            const std::string_view part = argv[1];
            if (part == "carrier") configured_carrier_reception();
            else if (part == "short-drift") {
                bounded_short_competition();
                bounded_drift_recovery();
            }
            else if (part == "long-drift") long_drift_recovery(false);
            else if (part == "broad-drift") long_drift_recovery(true);
            else if (part == "fading") weak_diffuse_fading_recovery();
            else if (part == "late-helper") late_helper_timing_recovery(false);
            else if (part == "late-helper-delay") late_helper_timing_recovery(true);
            else if (part == "signed-gain-timing") signed_gain_timing_and_exact_admission();
            else if (part == "signed-doppler") signed_relative_doppler_endpoints();
            else if (part == "long-calm") long_calm_drift_owner_lease();
            else if (part == "long-calm-polarity") long_calm_drift_owner_lease(true, 1, true);
            else if (part == "long-calm-plans") long_calm_drift_owner_lease(false);
            else if (part == "long-calm-cache") long_calm_drift_owner_lease(true, 0, false, true);
            else if (part == "calendar-admission") {
                calendar_opportunity_bound();
                calendar_line_and_separation();
                varying_quiet_band(.5, false, true);
            }
            else throw std::runtime_error("unknown native receiver test stratum");
            return 0;
        }
        check(argc == 1, "unexpected native receiver test arguments");
        complete_data();
        wrong_key_policy_boundaries();
        large_aperture_supplemental_plans();
        broad_frequency_timing_plan();
        varying_quiet_band(.5, false);
        varying_quiet_band(1.5, true);
        varying_bit_separation();
        varying_helper_coexistence();
        physical_quiet_and_limits();
        helper_composite_observation();
        native_precision_near_decision_sides();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
