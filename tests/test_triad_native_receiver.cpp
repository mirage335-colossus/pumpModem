#include "datapump/triad_receiver.hpp"
#include "datapump/robust_audio_frontend.hpp"
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
#include "triad_signed_gain_fixture.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
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

void protected_observation_validity() {
    using namespace receiver_detail;
    auto noise = fixture(std::nullopt, false, false);
    const auto score = [&](const ReceiverSlotPlan& slot) {
        const auto quote = protected_slot_workspace(noise.config, slot, 1);
        return score_protected_slot(noise.config, slot, noise.key, noise.iq, noise.valid,
                                    1, quote, {}, nullptr);
    };
    auto empty = noise.slot;
    empty.quiet_stop_sample = empty.quiet_first_sample;
    const auto missing = score(empty);
    check(missing.all_valid && !missing.complete && !missing.zero_h0_verified &&
          missing.decision == SlotDecision::ambiguous && !missing.coarse_workspaces,
          "an empty protected aperture must stay unresolved");
    check(noise.slot.quiet_first_sample > noise.slot.first_sample,
          "protected validity fixture requires a sample outside the aperture");
    noise.valid.front() = 0;
    const auto invalid = score(noise.slot);
    check(!invalid.all_valid && !invalid.complete && !invalid.zero_h0_verified &&
          invalid.decision == SlotDecision::ambiguous && !invalid.coarse_workspaces,
          "protected scoring must still reject an invalid sample outside the aperture");
}

void protected_discovery_near_far() {
    using namespace receiver_detail;
    auto input = fixture(std::nullopt, false, true);
    const auto wanted = fixture(0, false, true);
    const auto& config = input.config;
    auto& slot = input.slot;
    const auto geom = geometry(slot.format);
    const auto start = static_cast<std::size_t>(std::llround(
        (slot.window_capture_lead_seconds + slot.source_start_seconds) * config.sample_rate_hz));
    const auto gate = static_cast<std::size_t>(std::llround(geom.bit_seconds * config.sample_rate_hz));
    const auto first = start + geom.calendar_origin_sample + gate / 4;
    const auto stop = first + gate / 2;
    check(first < stop && stop < input.iq.size(), "near-far fixture requires nonempty protected and boundary samples");
    slot.quiet_first_sample = slot.first_sample + first;
    slot.quiet_stop_sample = slot.first_sample + stop;
    for (auto row = first; row < stop; ++row) input.iq[row] += .20 * (wanted.iq[row] - input.iq[row]);

    // Two competing epochs and both bits supply synthetic nuisance to the
    // full coarse search, entirely outside the scored protected aperture.
    const auto rows = input.iq.size();
    std::vector<Complex> nuisance(rows), basis(rows);
    std::vector<double> offsets(rows);
    for (std::size_t row = 0; row < rows; ++row) offsets[row] = static_cast<double>(row);
    Candidate mapping;
    mapping.arrival_seconds = static_cast<double>(slot.window_capture_lead_seconds + slot.source_start_seconds);
    for (auto epoch : {-4, 4}) {
        auto format = slot.format;
        format.epoch = static_cast<std::uint32_t>(static_cast<std::int64_t>(format.epoch) + epoch);
        for (auto bit : {0, 1}) {
            PatternSource source(format, input.key, static_cast<std::uint8_t>(bit));
            shaped_basis_mapped(source, mapping, config.sample_rate_hz, 0, offsets, basis, config.data_if_hz);
            for (std::size_t row = 0; row < rows; ++row)
                if (row < first || row >= stop) nuisance[row] += basis[row];
        }
    }
    auto contaminated = input.iq;
    for (std::size_t row = 0; row < rows; ++row) contaminated[row] += 32. * nuisance[row];
    check(std::ranges::equal(std::span(contaminated).subspan(first, stop - first),
                             std::span(input.iq).subspan(first, stop - first)),
          "near-far protected samples must remain identical");
    const auto quote = protected_slot_workspace(config, slot, 1);
    const auto run = [&](std::span<const Complex> samples, bool enabled) {
        return score_protected_slot(config, slot, input.key, samples, input.valid, 1, quote, {}, nullptr, enabled);
    };
    const auto clean = run(input.iq, true);
    const auto ordinary = run(contaminated, false);
    const auto augmented = run(contaminated, true);
    check(clean.decision == SlotDecision::data && clean.bit == 0 && !clean.protected_discovery_attempted &&
          ordinary.complete && ordinary.decision != SlotDecision::data && !ordinary.protected_discovery_attempted,
          "out-of-aperture nuisance must displace the otherwise admitted protected candidate");
    check(augmented.complete && augmented.all_valid && augmented.zero_h0_verified &&
          augmented.decision == SlotDecision::data && augmented.bit == 0 &&
          augmented.protected_discovery_attempted && augmented.protected_discovery_admitted &&
          augmented.protected_discovery_candidates > 0,
          "masked nominations must recover the wanted bit from the same protected samples");
    check(ordinary.fine_candidates > 0 &&
          augmented.fine_candidates == ordinary.fine_candidates + augmented.protected_discovery_candidates &&
          std::abs(augmented.selected_prior_mass -
              ordinary.selected_prior_mass / ordinary.fine_candidates * augmented.fine_candidates) < 1e-12,
          "protected discovery must retain exactly the original mass of each distinct atom");
    check(augmented.coarse_arrival_points == ordinary.coarse_arrival_points &&
          augmented.coarse_frequency_points == ordinary.coarse_frequency_points &&
          augmented.operational_threshold == ordinary.operational_threshold &&
          augmented.quiet_energy == ordinary.quiet_energy &&
          augmented.workspace_peak_bytes == ordinary.workspace_peak_bytes,
          "protected discoveries must share the original hypotheses, observations and workspace bounds");
}

void protected_carrier_beat_preserves_wanted() {
    using namespace receiver_detail;
    constexpr unsigned fs = 600;
    constexpr double lag = .004, clock = 137;
    constexpr std::int64_t epoch_offset = 2;
    for (const std::uint8_t bit : {0, 1}) {
        Fixture input;
        auto& config = input.config;
        config.sample_rate_hz = fs;
        config.data_if_hz = 150;
        config.noise_variance = .25;
        config.clock_bound_ppm = 200;
        config.time_varying_channel = true;
        config.selective_modulation_rate_hz = .6;
        config.selective_slow_depth = .65;
        config.selective_fast_depth = .26;
        auto& slot = input.slot;
        slot.index = 37;
        slot.first_sample = 20'000;
        slot.source_start_seconds = 18.1;
        slot.window_capture_lead_seconds = -17.5;
        slot.arrival_lower_seconds = -.012;
        slot.arrival_upper_seconds = .012;
        slot.format.sample_rate = fs;
        slot.format.inner_rate_hz = 96;
        slot.format.added_symbols = 1;
        slot.format.minimum_carrier_spacing_hz = 125;
        slot.format.bit_seconds = .001;
        slot.format.epoch = 1793850000;
        slot.format.symbol_ordinal = 7000037;
        const auto shape = geometry(slot.format);
        const auto rows = shape.source_samples + fs + 32;
        check(rows < 8000, "carrier beat fixture exceeds its sample budget");
        const auto active_first = 360 + shape.calendar_origin_sample;
        slot.stop_sample = slot.first_sample + rows;
        slot.quiet_first_sample = slot.first_sample + active_first + 15;
        slot.quiet_stop_sample = slot.first_sample + active_first +
            static_cast<std::uint64_t>(std::llround(shape.bit_seconds * fs)) - 15;

        auto format = slot.format;
        format.epoch += epoch_offset;
        PatternSource source(format, input.key, bit);
        std::vector<double> offsets(rows);
        for (std::size_t row = 0; row < rows; ++row) offsets[row] = static_cast<double>(row);
        input.iq.resize(rows);
        input.valid.resize(rows, 1);
        const auto render = [&](double physical_lag, double physical_clock) {
            Candidate truth;
            truth.clock_ppm = physical_clock;
            const auto scale = 1 + physical_clock * 1e-6;
            truth.arrival_seconds = static_cast<double>(slot.window_capture_lead_seconds + physical_lag +
                slot.source_start_seconds / scale);
            truth.frequency_hz = -config.data_if_hz * (scale - 1);
            shaped_basis_mapped(source, truth, config.sample_rate_hz, 0, offsets, input.iq, config.data_if_hz);
            std::mt19937_64 random(71837);
            std::normal_distribution<double> normal(0, std::sqrt(config.noise_variance / 2));
            for (auto& sample : input.iq) sample = .72 * sample + Complex{normal(random), normal(random)};
        };

        const auto quote = protected_slot_workspace(config, slot, 1);
        render(lag, clock);
        const auto observable = score_carrier_beat(config, slot, input.key, input.iq, input.valid,
            {epoch_offset, lag, 0., clock}, true, quote);
        check(observable.status == "scored" && qualified_carrier_beat_bit(observable) == bit,
              "the nonnominal fixed-coordinate carrier beats must support the keyed bit");
        const auto ordinary = score_protected_slot(config, slot, input.key, input.iq, input.valid,
            1, quote, {}, nullptr, false, false);
        const auto enabled = score_protected_slot(config, slot, input.key, input.iq, input.valid,
            1, quote, {}, nullptr, false, true);
        check(ordinary.complete && ordinary.all_valid && ordinary.decision == SlotDecision::data &&
              ordinary.bit == bit && !ordinary.protected_carrier_beat,
              "native protected fixture must admit the nonnominal wanted bit");
        check(enabled.complete && enabled.all_valid && enabled.decision == ordinary.decision &&
              enabled.bit == ordinary.bit && !enabled.protected_carrier_beat_corrected_polarity &&
              enabled.operational_bit_evidence == ordinary.operational_bit_evidence &&
              enabled.workspace_peak_bytes == ordinary.workspace_peak_bytes,
              "carrier beat polarity must preserve already-correct native DATA and presence evidence");
        // This narrow-carrier observation activates ordinary line handling.
        // The raw beat rule cannot replace the line-conditioned result.
        check(ordinary.reason == "varying_data_evidence_with_coherent_line" &&
              enabled.reason == ordinary.reason && !enabled.protected_carrier_beat &&
              enabled.protected_carrier_beat_search_coordinates == 0,
              "protected polarity must defer to the retained coherent-line model");

        render(0., 0.);
        const auto scan = scan_carrier_beat(config, slot, input.key, input.iq, input.valid, quote);
        check(scan.status == "scored" && scan.coordinates == receiver_epoch_offsets.size() &&
              !scan.nominations[1 - bit] && scan.nominations[bit] &&
              scan.nominations[bit]->coordinate.epoch_offset_seconds == epoch_offset,
              "the nominal-clock scanner must find the wanted bit at its nonnominal public epoch");
    }
}

void protected_colored_carrier_beat() {
    using namespace receiver_detail;
    using live::detail::AudioInput;
    using live::detail::real_pcm;
    constexpr unsigned fs = 600;
    constexpr double center = 150;
    constexpr double variance = .08;
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = center;
    config.noise_variance = variance;
    ReceiverSlotPlan slot;
    slot.index = 37;
    slot.first_sample = 20'000;
    slot.source_start_seconds = 18;
    slot.window_capture_lead_seconds = -17.5;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 96;
    slot.format.added_symbols = 1;
    slot.format.minimum_carrier_spacing_hz = 120;
    slot.format.bit_seconds = .001;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    const auto geometry = triad::v3::geometry(slot.format);
    const auto arrival_rows = fs / 2;
    const auto rows = geometry.source_samples + fs;
    const auto active_first = arrival_rows + geometry.calendar_origin_sample;
    slot.quiet_first_sample = slot.first_sample + active_first + 15;
    slot.quiet_stop_sample = slot.first_sample + active_first +
        static_cast<std::uint64_t>(std::llround(geometry.bit_seconds * fs)) - 15;
    slot.stop_sample = slot.first_sample + rows;
    check(rows < 8000, "colored PCM fixture exceeds its sample budget");

    const auto key = Crypto::amateur();
    std::array<std::uint8_t, 64> foreign_aes{}, foreign_serpent{};
    foreign_aes.fill(0x57);
    foreign_serpent.fill(0xc3);
    const auto foreign_key = Crypto::from_independent_roots(foreign_serpent, foreign_aes);
    const FineCoordinate coordinate{0, 0, 0, 0};
    const auto quote = score_carrier_beat(config, slot, key, {}, {}, coordinate, true,
        workspace_limit, {}, false, true);
    check(quote.status == "quoted" && quote.workspace_bytes > 0, "colored power scorer must quote its workspace");
    const auto fit = [&](std::span<const Complex> iq, std::span<const std::uint8_t> valid) {
        return score_carrier_beat(config, slot, key, iq, valid, coordinate, true,
            quote.workspace_bytes, {}, false, true);
    };
    const std::vector<Complex> zero(rows);
    const std::vector<Complex> constant(rows, Complex{.2, .3});
    const double zero_atom = -4 * std::log(19.);
    for (const auto& iq : {std::cref(zero), std::cref(constant)}) {
        const auto evidence = fit(iq.get(), {});
        check(evidence.status == "scored" && !evidence.white_power_log_evidence && evidence.colored_power_log_evidence,
              "colored power must use only the declared likelihood model");
        for (const auto bit : *evidence.colored_power_log_evidence)
            check(std::abs(bit - zero_atom) < 1e-12, "constant power must retain only the zero gain atom");
    }
    rejects<ReceiverWorkspaceError>([&] {
        (void)score_carrier_beat(config, slot, key, zero, {}, coordinate, true,
            quote.workspace_bytes - 1, {}, false, true);
    }, "colored power must honor the exact scorer workspace");

    const auto capture = [&](const Crypto& transmit_key, double gain) {
        constexpr double lower = 20, upper = 280;
        const auto halo = AudioInput(fs, center, lower, upper).halo_samples();
        AudioInput audio(fs, center, lower, upper, slot.first_sample - halo);
        PatternSource source(slot.format, transmit_key, 1);
        std::vector<float> pcm(rows + 2 * halo);
        std::mt19937_64 random(61572);
        std::normal_distribution<double> gaussian(0, std::sqrt(variance / 2));
        auto previous = gaussian(random);
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            const auto next = gaussian(random);
            // The two-tap filter has norm at most one. A preceding independent
            // draw makes the Gaussian color stationary over the entire input.
            const auto colored = .65 * next + .35 * previous;
            previous = next;
            const auto sample = static_cast<long double>(i) - halo - arrival_rows;
            pcm[i] = real_pcm(source.sample_at(sample), i, center, fs, gain) + static_cast<float>(colored);
        }
        std::vector<Complex> iq;
        std::vector<std::uint8_t> validity;
        const auto first = audio.project(pcm, iq, {}, &validity);
        check(first == slot.first_sample && iq.size() == rows &&
              std::ranges::all_of(validity, [](auto value) { return value == 1; }),
              "native colored PCM fixture must retain full projection support");
        const auto observed = fit(iq, validity);
        validity[slot.quiet_first_sample - slot.first_sample] = 0;
        check(!fit(iq, validity).colored_power_log_evidence, "an invalid protected row must prevent colored power evidence");
        check(observed.status == "scored" && observed.colored_power_log_evidence &&
              std::ranges::all_of(*observed.colored_power_log_evidence, [](double e) { return std::isfinite(e); }),
              "native PCM fixture must produce finite colored likelihoods");
        return *observed.colored_power_log_evidence;
    };
    const auto absent = capture(key, 0);
    const auto wanted = capture(key, .55);
    const auto foreign = capture(foreign_key, .55);
    check(*std::ranges::max_element(absent) < 0, "fixed stationary colored noise must fall below unit evidence");
    check(wanted[1] > 0 && wanted[1] > wanted[0] &&
          wanted[1] > *std::ranges::max_element(foreign),
          "native colored power must prefer the planted bit to its opposite and the matched foreign capture");
}

void protected_drift_lattice() {
    using namespace receiver_detail;
    auto noise = fixture(std::nullopt, false, false);
    noise.config.time_varying_channel = true;
    noise.config.time_varying_common_doppler_hz = .75;
    noise.config.time_varying_atmospheric_hz = .75;
    const auto whole = drift_plan(noise.config, noise.slot, 1);
    check(whole.workspace_bytes && whole.raw_rows >= 128, "protected drift fixture needs a full public lattice");
    auto aperture = noise.slot;
    const auto crop = whole.first + whole.raw_rows / 4;
    aperture.first_sample += crop;
    aperture.stop_sample = aperture.first_sample + whole.raw_rows / 4;
    aperture.quiet_first_sample = aperture.first_sample;
    aperture.quiet_stop_sample = aperture.stop_sample;
    aperture.window_capture_lead_seconds -= static_cast<long double>(crop) / noise.config.sample_rate_hz;

    const auto replanned = drift_plan(noise.config, aperture, 1);
    const auto retained = drift_plan(noise.config, aperture, 1, false, whole.workspace_bytes, 0, &whole);
    check(replanned.workspace_bytes && replanned.frequency_step != whole.frequency_step,
          "protected drift fixture must change the local FFT lattice when replanned");
    check(retained.workspace_bytes && retained.workspace_bytes <= whole.workspace_bytes &&
          retained.raw_rows == aperture.stop_sample - aperture.first_sample && retained.raw_rows < whole.raw_rows &&
          retained.nfft == whole.nfft && retained.sample_stride == whole.sample_stride &&
          retained.frequency_step == whole.frequency_step && retained.frequency_radius == whole.frequency_radius &&
          retained.slope_step == whole.slope_step && retained.slope_radius == whole.slope_radius &&
          retained.slope_reference_seconds == whole.slope_reference_seconds && retained.slope_budget == whole.slope_budget,
          "protected drift must quote only the cropped samples on the original public lattice");
}

void affine_continuation() {
    using namespace receiver_detail;
    const auto signal = fixture(0, false, false);
    const auto ordinary = score(signal);
    AffineContinuationContext accepted_context;
    const auto accepted = score_slot_for_continuation(signal.config, signal.slot, signal.key, signal.iq,
        signal.valid, 1, workspace_limit, {}, nullptr, accepted_context);
    check(ordinary.decision == SlotDecision::data && ordinary.bit == 0 &&
          accepted.decision == ordinary.decision && accepted.bit == ordinary.bit &&
          accepted.complete && accepted.all_valid && accepted.affine_tracking[0] && !accepted_context.eligible &&
          ordinary.bit_evidence == accepted.bit_evidence &&
          ordinary.operational_bit_evidence == accepted.operational_bit_evidence &&
          ordinary.operational_threshold == accepted.operational_threshold &&
          ordinary.coarse_workspaces == accepted.coarse_workspaces &&
          ordinary.coarse_frequency_points == accepted.coarse_frequency_points &&
          ordinary.coarse_arrival_points == accepted.coarse_arrival_points &&
          ordinary.basis_rows == accepted.basis_rows &&
          ordinary.workspace_peak_bytes == accepted.workspace_peak_bytes,
          "recording continuation state must preserve the canonical DATA decision and work");

    const auto noise = fixture(std::nullopt, false, false);
    AffineContinuationContext quiet_context;
    const auto quiet = score_slot_for_continuation(noise.config, noise.slot, noise.key, noise.iq,
        noise.valid, 1, workspace_limit, {}, nullptr, quiet_context);
    check(quiet.decision == SlotDecision::quiet && quiet.complete && quiet.all_valid && quiet_context.eligible &&
          quiet_context.grid && quiet_context.ordinary_count && !quiet_context.check_line,
          "continuation control must retain the fixed-grid description of a physically scored quiet slot");
    const auto& first = quiet_context.ordinary.front();
    const ReceiverSlotScore::TimingProposal duplicate{first.epoch_offset_seconds, first.arrival_seconds,
        first.frequency_hz, first.clock_ppm};
    auto outside = duplicate;
    outside.epoch_offset_seconds = receiver_epoch_offsets.back() + 1;
    auto unsupported_clock = duplicate;
    unsupported_clock.clock_ppm = 100;
    for (const auto& hint : {duplicate, outside, unsupported_clock}) {
        const auto unchanged = continue_affine_slot(noise.config, noise.slot, noise.key, noise.iq,
            1, quiet_context.scratch_bytes, {}, nullptr, quiet_context, hint, quiet);
        check(unchanged.decision == SlotDecision::quiet && unchanged.reason == quiet.reason &&
              unchanged.operational_bit_evidence == quiet.operational_bit_evidence &&
              unchanged.selected_prior_mass == quiet.selected_prior_mass &&
              unchanged.basis_rows == quiet.basis_rows && !unchanged.affine_continuation_attempted,
              "duplicate or out-of-prior timing state must leave quiet and its evidence unchanged");
    }

    // Exercise the continuation primitive with a deliberately restricted
    // baseline bank. Both likelihoods are actually scored on identical IQ.
    // This fixture does not assume that the production coarse search misses
    // the planted coordinate, or measure the rate of recovery at low SNR.
    auto input = signal;
    for (std::size_t i = 0; i < input.iq.size(); ++i)
        input.iq[i] = noise.iq[i] + .2 * (signal.iq[i] - noise.iq[i]);
    auto context = quiet_context;
    context.ordinary_count = 1;
    context.ordinary.front() = {receiver_epoch_offsets.front(), 0, 0, 0};
    const auto effective_prior = -context.grid->log_count();
    check(context.atom_log_mass == effective_prior,
          "the affine continuation fixture must retain the original full-grid mass");
    const auto fine = [&](const Fixture& input, FineCoordinate coordinate) {
        ReceiverSlotScore score;
        score_fine(input.config, input.slot, input.key, input.iq, std::span(&coordinate, 1),
            context.projected_sample_rate_hz, context.coarse_clock_gap_ppm, 1, score,
            context.scratch_bytes, {});
        classify_fine_slot(score, *context.grid);
        return score;
    };
    const auto predecessor = *accepted.affine_tracking[0];
    const auto baseline = fine(input, context.ordinary.front());
    const auto omitted = fine(input, {predecessor.epoch_offset_seconds, predecessor.arrival_seconds,
        predecessor.frequency_hz, predecessor.clock_ppm});
    check(baseline.complete && baseline.decision != SlotDecision::data &&
          omitted.complete && omitted.decision == SlotDecision::data && omitted.bit == 0,
          "the controlled ordinary cell must miss and the omitted cell must admit the planted DATA");
    const auto can_not_fit = continue_affine_slot(input.config, input.slot, input.key, input.iq,
        1, context.scratch_bytes - 1, {}, nullptr, context, predecessor, baseline);
    check(can_not_fit.operational_bit_evidence == baseline.operational_bit_evidence &&
          can_not_fit.basis_rows == baseline.basis_rows && !can_not_fit.affine_continuation_attempted,
          "insufficient continuation scratch must leave the ordinary decision and work intact");

    const auto quiet_baseline = fine(noise, context.ordinary.front());
    const auto continued_quiet = continue_affine_slot(noise.config, noise.slot, noise.key, noise.iq,
        1, context.scratch_bytes, {}, nullptr, context, predecessor, quiet_baseline);
    check(quiet_baseline.decision == SlotDecision::quiet && continued_quiet.decision == SlotDecision::quiet &&
          continued_quiet.operational_bit_evidence == quiet_baseline.operational_bit_evidence &&
          continued_quiet.affine_continuation_attempted && !continued_quiet.affine_continuation_admitted,
          "a strong earlier coordinate cannot turn freshly scored noise into DATA");

    // Model the late packed beat path nominating a fresh cell whose own
    // likelihood leaves the slot unresolved. Its coordinate must enter the
    // same bank as its evidence before ordered continuation sees the slot.
    auto after_beat = context;
    auto unresolved = baseline;
    const FineCoordinate beat_coordinate{receiver_epoch_offsets.back(), 0, 0, 0};
    const auto beat_atom = fine(input, beat_coordinate);
    check(beat_atom.complete && beat_atom.decision != SlotDecision::data,
          "late beat fixture must leave the ordinary slot unresolved");
    for (std::size_t bit = 0; bit < unresolved.operational_bit_evidence.size(); ++bit) {
        const auto a = unresolved.operational_bit_evidence[bit], b = beat_atom.operational_bit_evidence[bit];
        unresolved.operational_bit_evidence[bit] = std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
    }
    FinePreparation bank{*context.grid, {context.ordinary.front()}, {}, {}, {}};
    retain_affine_atoms(bank, unresolved, beat_atom, std::span(&beat_coordinate, 1));
    after_beat.ordinary_count = bank.coordinates.size();
    std::ranges::copy(bank.coordinates, after_beat.ordinary.begin());
    check(after_beat.ordinary_count == 2 && unresolved.fine_candidates == 2 &&
          after_beat.ordinary[1] == beat_coordinate &&
          std::abs(unresolved.selected_prior_mass - 2 * std::exp(effective_prior)) < 1e-12,
          "beat revision must retain its unique affine atom even without a DATA decision");
    auto packed = input.slot;
    packed.protected_data_first_sample = packed.first_sample;
    packed.protected_data_stop_sample = packed.stop_sample;
    const auto from_beat = continue_affine_slot(input.config, packed, input.key, input.iq,
        1, after_beat.scratch_bytes, {}, nullptr, after_beat,
        ReceiverSlotScore::TimingProposal{beat_coordinate.epoch_offset_seconds, 0, 0, 0}, unresolved, true);
    check(!from_beat.affine_continuation_attempted && from_beat.decision == unresolved.decision &&
          from_beat.operational_bit_evidence == unresolved.operational_bit_evidence &&
          from_beat.quiet_energy == unresolved.quiet_energy && from_beat.quiet_modes == unresolved.quiet_modes,
          "continuation must skip a retained beat coordinate and preserve the physical quiet statistic");
    const auto after_predecessor = continue_affine_slot(input.config, packed, input.key, input.iq,
        1, after_beat.scratch_bytes, {}, nullptr, after_beat, predecessor, unresolved, true);
    check(after_predecessor.affine_continuation_admitted && after_predecessor.bit == 0 &&
          after_predecessor.fine_candidates == 3 &&
          std::abs(after_predecessor.selected_prior_mass - 3 * std::exp(effective_prior)) < 1e-12,
          "packed continuation must score an omitted earlier DATA coordinate once at its original mass");

    const auto continued = continue_affine_slot(input.config, input.slot, input.key, input.iq,
        1, context.scratch_bytes, {}, nullptr, context, predecessor, baseline);
    check(continued.decision == SlotDecision::data && continued.bit == 0 && continued.all_valid &&
          continued.affine_continuation_attempted && continued.affine_continuation_admitted &&
          continued.operational_threshold == baseline.operational_threshold &&
          continued.fine_candidates == 2 && continued.basis_rows == baseline.basis_rows + omitted.basis_rows &&
          std::abs(continued.selected_prior_mass - 2 * std::exp(effective_prior)) < 1e-12,
          "ordered continuation must score one omitted affine cell with the original prior");
    for (std::size_t bit = 0; bit < continued.bit_evidence.size(); ++bit) {
        const auto a = baseline.operational_bit_evidence[bit], b = omitted.operational_bit_evidence[bit];
        const auto expected = std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
        check(std::abs(continued.operational_bit_evidence[bit] - expected) < 1e-12,
              "continuation must sum fresh per-cell likelihoods exactly once");
    }
    const auto repeated = continue_affine_slot(input.config, input.slot, input.key, input.iq,
        1, context.scratch_bytes, {}, nullptr, context, predecessor, continued);
    check(repeated.operational_bit_evidence == continued.operational_bit_evidence &&
          repeated.fine_candidates == continued.fine_candidates && repeated.basis_rows == continued.basis_rows,
          "a repeated hint must not add evidence to an already admitted DATA slot");
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

// The native ridge search sums segments outer, then each slope of a block,
// then centers. Compare it at every block size from one slope to the block
// maximum with a direct center-outer reference on synthetic FP64 power
// tables that reach the half-band edges, wrap negative bins, tie exactly
// and carry nonfinite powers. The records, the selected slopes and the
// rank must match bit for bit.
void drift_ridge_interchange_oracle() {
    using namespace receiver_detail;
    const auto same_bits = [](double a, double b) {
        return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    };
    const auto direct_record = [](const DriftPlan& plan, const DriftRidgeGrid& grid, std::span<const double> power,
                                  std::span<const double> centers, std::int64_t slope) {
        DriftRidgeRecord record;
        const auto half = static_cast<std::int64_t>(grid.fft_rows / 2);
        for (auto center = -grid.bin_radius; center <= grid.bin_radius; center += grid.bin_stride) {
            std::array<double, 2> sum{};
            for (std::size_t segment = 0; segment < grid.segments; ++segment) {
                const auto bin = center + std::llround(slope * plan.slope_step * centers[segment] / grid.frequency_step);
                if (std::abs(bin) >= half) continue;
                const auto column = static_cast<std::size_t>(bin < 0 ? bin + 2 * half : bin);
                for (std::size_t bit = 0; bit < 2; ++bit)
                    sum[bit] += power[(bit * grid.segments + segment) * grid.fft_rows + column];
            }
            for (std::size_t bit = 0; bit < 2; ++bit)
                if (sum[bit] > record.score[bit]) {
                    record.score[bit] = sum[bit];
                    record.center[bit] = center;
                }
        }
        return record;
    };
    enum class Values { uniform, ties, nonfinite };
    struct Case {
        std::size_t rows, segments, slope_budget;
        std::int64_t slope_radius, frequency_radius;
        double rate, edge_offset_bins;
        Values values;
        bool slow;
    };
    const std::array cases{
        Case{1000, 8, 3, 300, 1'000'000, 100, 60, Values::uniform, false},  // edges lie on the slope grid
        Case{1000, 8, 4, 301, 1'000'000, 100, 60, Values::ties, false},
        Case{16384, 8, 3, 40'000, 12'000, 1000, 2500, Values::nonfinite, false},  // center stride 3, whole-row skips
        Case{4000, 12, 6, 5000, 1'000'000, 200, 90, Values::uniform, true},  // wide slow-plan refinement
        Case{4000, 12, 6, 5000, 1'000'000, 200, 90, Values::ties, true},
        Case{64, 8, 3, 9, 0, 50, 3, Values::nonfinite, false},  // one center
        Case{2048, 9, 8, 700, 40, 300, 100, Values::uniform, true},  // narrow slow plan
    };
    std::mt19937_64 random(4066);
    std::uniform_real_distribution<double> uniform(0, 1);
    std::size_t compared_records = 0, skipped_cells = 0, blocked_records = 0, largest_block = 0;
    for (const auto& test : cases) {
        DriftPlan plan;
        plan.first = 485;
        plan.rows = test.rows;
        plan.sample_stride = 2;
        plan.nfft = std::bit_ceil(2 * test.rows);
        plan.frequency_step = test.rate / static_cast<double>(plan.nfft);
        plan.frequency_radius = test.frequency_radius;
        plan.slope_radius = test.slope_radius;
        plan.slow_ridge_segments = test.slow ? test.segments - 1 : 0;
        const auto duration = static_cast<double>(test.rows) / test.rate;
        plan.slope_reference_seconds = (plan.first + .5) / (test.rate * plan.sample_stride) + duration * .5;
        const auto segment_step = test.rate / static_cast<double>(std::bit_ceil(2 * ((test.rows + test.segments - 1) /
            test.segments)));
        plan.slope_step = test.edge_offset_bins * segment_step / (static_cast<double>(test.slope_radius) * duration * .5);
        std::vector<double> centers(test.segments);
        const auto grid = drift_ridge_grid(plan, test.rate, test.segments, centers);
        check(!drift_ridge_exhaustive(plan, test.slope_budget) && grid.fft_rows == std::bit_ceil(2 * ((test.rows +
              test.segments - 1) / test.segments)), "ridge oracle case lost its ranked public slope grid");
        std::vector<double> power(2 * grid.segments * grid.fft_rows);
        for (auto& value : power) {
            const auto draw = uniform(random);
            value = test.values == Values::ties ? std::floor(3 * draw) : uniform(random);
            if (test.values == Values::nonfinite && draw < .03)
                value = draw < .01 ? std::numeric_limits<double>::quiet_NaN() :
                        draw < .02 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
        }
        std::vector<std::int64_t> order{-plan.slope_radius};
        for (auto slope = -(plan.slope_radius / grid.slope_stride) * grid.slope_stride; slope <= plan.slope_radius;
             slope += grid.slope_stride)
            if (slope != -plan.slope_radius && slope != plan.slope_radius) order.push_back(slope);
        order.push_back(plan.slope_radius);
        std::size_t center_count = 0;
        for (auto center = -grid.bin_radius; center <= grid.bin_radius; center += grid.bin_stride) ++center_count;
        check(grid.slopes == order.size() && grid.centers == center_count,
              "ridge grid counts differ from the native coarse slope and center loops");
        std::vector<DriftRidgeRecord> references;
        DriftRidgeFrontier direct_frontier;
        for (std::size_t index = 0; index < grid.slopes; ++index) {
            check(drift_ridge_slope(plan, grid, index) == order[index], "ridge slope order changed");
            const auto direct = direct_record(plan, grid, power, centers, order[index]);
            references.push_back(direct);
            direct_frontier.insert(order[index], direct);
            for (std::size_t segment = 0; segment < grid.segments; ++segment) {
                const auto offset = std::llround(order[index] * plan.slope_step * centers[segment] / grid.frequency_step);
                skipped_cells += grid.bin_radius + std::abs(offset) >= static_cast<std::int64_t>(grid.fft_rows / 2);
            }
            ++compared_records;
        }
        const auto direct = direct_frontier.select(plan, grid, power, centers, test.slope_budget);
        check(!direct.slopes.empty() && direct.slopes.front() == 0 && direct.slopes.size() <= test.slope_budget,
              "the direct ridge reference lost its zero-first bounded exact slope list");
        // Slope blocks of 1, 2, 3 and 16 slopes, the cache rule's and one past
        // the slope count: each record and the frontier's selection equal the
        // direct reference's. NaN-poisoned sums expose any unreset term.
        const std::array<std::size_t, 6> blocks{1, 2, 3, 16, drift_ridge_block(grid), grid.slopes + 1};
        for (const auto block : blocks) {
            const auto width = std::min({block, grid.slopes, maximum_drift_ridge_block});
            std::vector<double> sums(2 * width * grid.centers, std::numeric_limits<double>::quiet_NaN());
            std::array<std::int64_t, maximum_drift_ridge_block> slopes{};
            std::array<DriftRidgeRecord, maximum_drift_ridge_block> records{};
            for (std::size_t first = 0; first < grid.slopes; first += width) {
                const auto count = std::min(width, grid.slopes - first);
                std::copy_n(order.begin() + static_cast<std::ptrdiff_t>(first), count, slopes.begin());
                drift_ridge_records(plan, grid, power, centers, std::span(slopes).first(count),
                                    std::span(records).first(count), sums);
                for (std::size_t j = 0; j < count; ++j, ++blocked_records)
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        check(same_bits(records[j].score[bit], references[first + j].score[bit]) &&
                              records[j].center[bit] == references[first + j].center[bit],
                              "slope-blocked ridge records differ from the direct center-major reference");
            }
            std::ranges::fill(sums, std::numeric_limits<double>::quiet_NaN());
            const auto blocked = drift_ridge_frontier(plan, grid, power, centers, block, sums, {})
                .select(plan, grid, power, centers, test.slope_budget);
            check(blocked.slopes == direct.slopes && same_bits(blocked.rank, direct.rank),
                  "slope-blocked ridge search changed the selected exact slopes or atom rank");
            largest_block = std::max(largest_block, width);
        }
        rejects<Error>([&] {
            std::vector<double> short_sums(2 * grid.centers - 1);
            std::array<DriftRidgeRecord, 1> record{};
            drift_ridge_records(plan, grid, power, centers, std::span(order).first(1), record, short_sums);
        }, "a ridge block must reject sums shorter than its slopes' centers");
    }
    std::cerr << "ridge oracle records=" << compared_records << " blocked=" << blocked_records
              << " largest block=" << largest_block << " edge segment checks=" << skipped_cells << '\n';
    check(compared_records > 900 && skipped_cells > 0, "ridge oracle no longer reaches its edge strata");
    check(blocked_records == 6 * compared_records && largest_block == maximum_drift_ridge_block,
          "ridge block oracle no longer covers every block size");
    // The cache rule: 16 slopes at the largest and the HF 984-center grids,
    // the block maximum on small grids.
    const auto rule = [](std::size_t centers) { return drift_ridge_block(DriftRidgeGrid{.centers = centers}); };
    check(rule(1025) == 16 && rule(984) == 16 && rule(513) == 31 && rule(100) == maximum_drift_ridge_block &&
          rule(1) == maximum_drift_ridge_block, "ridge slope block rule changed");
    rejects<Error>([&] { (void)rule(0); }, "a ridge block rule must reject an empty grid");
    rejects<Error>([&] { (void)rule(1026); }, "a ridge block rule must reject an unbounded grid");
    // Every public slope fits the exact budget: no ranking and zero rank.
    DriftPlan small;
    small.slope_radius = 3;
    const auto all = DriftRidgeFrontier{}.select(small, {}, {}, {}, 8);
    check(drift_ridge_exhaustive(small, 7) && !drift_ridge_exhaustive(small, 6) &&
          all.slopes == std::vector<std::int64_t>{0, -3, -2, -1, 1, 2, 3} && same_bits(all.rank, 0.),
          "an exhaustive public slope bank must keep zero first and every public slope");
    // Slope backend contract: each slope's stable top eight (equal ranks by
    // ascending signed bin, NaN and -inf excluded, padded with -inf), merged
    // in scoring order, equals the native bin-by-bin streaming frontier and
    // a direct stable sort of every scored bin.
    const auto rank_descending = [](const DriftPeak& a, const DriftPeak& b) { return a.rank > b.rank; };
    std::size_t sparse_frontiers = 0, infinite_peaks = 0, tied_peaks = 0;
    for (std::size_t trial = 0; trial < 96; ++trial) {
        const auto radius = static_cast<std::int64_t>(random() % 24);
        std::vector<std::int64_t> slopes(1 + random() % 10);
        for (std::size_t index = 0; index < slopes.size(); ++index)
            slopes[index] = static_cast<std::int64_t>(index) - static_cast<std::int64_t>(slopes.size() / 2);
        std::ranges::shuffle(slopes, random);
        DriftPeakFrontier streaming{}, merged{};
        std::vector<DriftPeak> scored;
        for (const auto slope : slopes) {
            std::vector<DriftPeak> own;
            for (auto bin = -radius; bin <= radius; ++bin) {
                const auto draw = uniform(random);
                double rank = uniform(random);
                switch (trial % 4) {
                case 1: rank = std::floor(3 * rank); break;
                case 2: rank = draw < .1 ? std::numeric_limits<double>::quiet_NaN() :
                               draw < .15 ? std::numeric_limits<double>::infinity() :
                               draw < .25 ? -std::numeric_limits<double>::infinity() : std::floor(4 * rank);
                        break;
                case 3: rank = draw < .45 ? std::numeric_limits<double>::quiet_NaN() :
                               draw < .9 ? -std::numeric_limits<double>::infinity() :
                               draw < .93 ? std::numeric_limits<double>::infinity() : std::floor(2 * rank);
                        break;
                default: break;
                }
                const DriftPeak peak{rank, slope, bin};
                merge_drift_peak(streaming, peak);
                if (rank > -std::numeric_limits<double>::infinity()) {
                    own.push_back(peak);
                    scored.push_back(peak);
                }
            }
            std::ranges::stable_sort(own, rank_descending);
            DriftPeakFrontier list{};
            std::copy_n(own.begin(), std::min(own.size(), list.size()), list.begin());
            for (const auto& peak : list) merge_drift_peak(merged, peak);
        }
        std::ranges::stable_sort(scored, rank_descending);
        DriftPeakFrontier direct{};
        std::copy_n(scored.begin(), std::min(scored.size(), direct.size()), direct.begin());
        for (std::size_t rank = 0; rank < retained_drift_per_bit; ++rank)
            for (const auto* other : {&merged, &direct})
                check(same_bits(streaming[rank].rank, (*other)[rank].rank) &&
                      streaming[rank].slope == (*other)[rank].slope && streaming[rank].center == (*other)[rank].center,
                      "merged per-slope peak lists differ from the native streaming drift frontier");
        sparse_frontiers += scored.size() < retained_drift_per_bit;
        for (std::size_t rank = 0; rank < retained_drift_per_bit; ++rank) {
            infinite_peaks += std::isinf(streaming[rank].rank) && streaming[rank].rank > 0;
            tied_peaks += rank && std::isfinite(streaming[rank].rank) && streaming[rank].rank == streaming[rank - 1].rank;
        }
    }
    std::cerr << "drift peak merge sparse=" << sparse_frontiers << " infinite=" << infinite_peaks
              << " tied=" << tied_peaks << '\n';
    check(sparse_frontiers > 0 && infinite_peaks > 0 && tied_peaks > 0,
          "drift peak merge contract no longer reaches sparse, infinite and tied frontiers");
}

// A device drift session that mirrors the native proposals on the host
// (device-free). Its ridge reproduces the native segment spectra, power
// tables and records from the staged FP32 products bit for bit; its
// slopes dechirp those staged values, which the native route cannot see,
// so their frontiers are an FP32-input mirror of the device route.
class MirrorDriftBackend final : public receiver_detail::ReceiverBulkBackend {
public:
    enum class Mode { exact_ridge, all_native, staged_slopes, declined, envelope };
    explicit MirrorDriftBackend(Mode mode, bool unified = true) : mode_(mode), unified_(unified) {}
    std::unique_ptr<receiver_detail::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<receiver_detail::CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    receiver_detail::BulkReceipt receipt() const override {
        receiver_detail::BulkReceipt receipt;
        receipt.vulkan = false;
        // An integrated device by default, whose envelope fallback names
        // unified memory; a discrete one names its workspace.
        receipt.unified_memory = unified_;
        return receipt;
    }
    std::string_view stage_decline(const receiver_detail::StageShape&, bool fp64) const noexcept override {
        return mode_ == Mode::declined || fp64 ? "mirror-declined" : "";
    }
    std::size_t stage_host_bytes(const receiver_detail::StageShape&, bool) const override {
        ++host_queries;
        return mode_ == Mode::envelope ? std::numeric_limits<std::size_t>::max() / 2 : 0;
    }
    std::unique_ptr<receiver_detail::DriftBulk> make_drift(const receiver_detail::DriftBulkShape& shape, bool,
                                                           std::stop_token) override;
    void record_stage_native(receiver_detail::StageFamily, receiver_detail::StageReason reason,
                             receiver_detail::StageNative kind) override {
        std::lock_guard lock(mutex);
        records.emplace_back(reason, kind);
    }
    Mode mode_;
    bool unified_;
    mutable std::size_t host_queries = 0;
    std::size_t factories = 0, ridge_calls = 0, slope_calls = 0, staged_lanes = 0;
    receiver_detail::DriftBulkShape shape;
    std::vector<std::pair<receiver_detail::StageReason, receiver_detail::StageNative>> records;
    std::mutex mutex;
};

class MirrorDrift final : public receiver_detail::DriftBulk {
public:
    MirrorDrift(MirrorDriftBackend& owner, const receiver_detail::DriftBulkShape& shape)
        : owner_(owner), shape_(shape), staging_(shape.lanes, std::vector<std::complex<float>>(2 * shape.taps * shape.rows)) {}
    std::span<std::complex<float>> staging(std::size_t lane) noexcept override {
        if (lane >= staging_.size()) return {};
        return staging_[lane];
    }
    bool ridge(const receiver_detail::DriftAtomRequest& request, receiver_detail::DriftAtomResult& result,
               receiver_detail::StageDeviceWork& work, std::stop_token stop) override {
        using namespace receiver_detail;
        {
            std::lock_guard lock(owner_.mutex);
            ++owner_.ridge_calls;
        }
        if (owner_.mode_ == MirrorDriftBackend::Mode::all_native) return false;
        const auto columns = 2 * shape_.taps;
        const auto& staged = staging_.at(request.lane);
        for (const auto& [geometry, records, power] : {std::tuple(&request.fast, result.fast, result.fast_power),
                                                     std::tuple(&request.slow, result.slow, result.slow_power)}) {
            const auto& g = *geometry;
            if (!g.segments) continue;
            const auto rows = g.segment_fft;
            const modem::detail::search_fft::FloatPlan plan(rows, stop);
            std::vector<double> table(2 * g.segments * rows, 0.);
            std::vector<std::complex<float>> spectrum(rows);
            for (std::size_t segment = 0; segment < g.segments; ++segment)
                for (std::size_t column = 0; column < columns; ++column) {
                    std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
                    for (auto i = g.segment_bounds[segment]; i < g.segment_bounds[segment + 1]; ++i)
                        spectrum[i - g.segment_bounds[segment]] = staged[column * shape_.rows + i];
                    modem::detail::search_fft::fft(spectrum, false, plan, stop);
                    const auto bit = column / shape_.taps, tap = column % shape_.taps;
                    if (request.energy[bit][tap] <= 0) continue;
                    for (std::size_t bin = 0; bin < rows; ++bin)
                        table[(bit * g.segments + segment) * rows + bin] +=
                            std::norm(static_cast<Complex>(spectrum[bin])) / request.energy[bit][tap];
                }
            const auto half = static_cast<std::int64_t>(rows / 2);
            for (std::size_t index = 0; index < g.slopes.size(); ++index) {
                DriftRidgeRecord record;
                for (auto center = -g.bin_radius; center <= g.bin_radius; center += g.bin_stride) {
                    std::array<double, 2> sum{};
                    for (std::size_t segment = 0; segment < g.segments; ++segment) {
                        const auto bin = center + g.offsets[index * g.segments + segment];
                        if (std::abs(bin) >= half) continue;
                        const auto column = static_cast<std::size_t>(bin < 0 ? bin + 2 * half : bin);
                        for (std::size_t bit = 0; bit < 2; ++bit) sum[bit] += table[(bit * g.segments + segment) * rows + column];
                    }
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        if (sum[bit] > record.score[bit]) {
                            record.score[bit] = sum[bit];
                            record.center[bit] = center;
                        }
                }
                records[index] = record;
            }
            if (g.return_power) std::ranges::copy(table, power.begin());
            ++work.score_rows;
        }
        return true;
    }
    bool slopes(const receiver_detail::DriftAtomRequest& request, receiver_detail::DriftAtomResult& result,
                receiver_detail::StageDeviceWork& work, std::stop_token stop) override {
        using namespace receiver_detail;
        {
            std::lock_guard lock(owner_.mutex);
            ++owner_.slope_calls;
        }
        if (owner_.mode_ != MirrorDriftBackend::Mode::staged_slopes) return false;
        const auto columns = 2 * shape_.taps, nfft = shape_.nfft;
        const auto radius = static_cast<std::int64_t>(shape_.frequency_radius);
        const auto& staged = staging_.at(request.lane);
        const modem::detail::search_fft::FloatPlan plan(nfft, stop);
        std::vector<std::complex<float>> spectra(columns * nfft);
        std::array<double, 2> nomination{-std::numeric_limits<double>::infinity(),
                                         -std::numeric_limits<double>::infinity()};
        for (std::size_t index = 0; index < request.slopes.size(); ++index) {
            const double rate = request.slopes[index] * request.rate_step;
            std::ranges::fill(spectra, std::complex<float>{});
            for (std::size_t column = 0; column < columns; ++column) {
                for (std::size_t j = 0; j < shape_.rows; ++j) {
                    const double t = request.first_t + j * request.delta;
                    spectra[column * nfft + j] = static_cast<std::complex<float>>(
                        static_cast<Complex>(staged[column * shape_.rows + j]) *
                        std::polar(1., -std::numbers::pi * rate * t * t));
                }
                modem::detail::search_fft::fft(std::span(spectra).subspan(column * nfft, nfft), false, plan, stop);
            }
            std::array<DriftPeakFrontier, 2> frontier{};
            for (auto bin = -radius; bin <= radius; ++bin) {
                const auto offset = static_cast<std::size_t>(bin < 0 ? bin + static_cast<std::int64_t>(nfft) : bin);
                for (std::size_t bit = 0; bit < 2; ++bit) {
                    const auto r0 = static_cast<Complex>(spectra[bit * shape_.taps * nfft + offset]);
                    const auto r1 = shape_.taps == 2 ?
                        static_cast<Complex>(spectra[(bit * shape_.taps + 1) * nfft + offset]) : Complex{};
                    merge_drift_peak(frontier[bit], {std::norm(r0) + std::norm(r1), 0, bin});
                    double normalized = 0;
                    if (request.energy[bit][0] > 0) normalized += std::norm(r0) / request.energy[bit][0];
                    if (shape_.taps == 2 && request.energy[bit][1] > 0) normalized += std::norm(r1) / request.energy[bit][1];
                    nomination[bit] = std::max(nomination[bit], normalized);
                }
            }
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t rank = 0; rank < drift_frontier_ranks; ++rank)
                    result.frontiers[(2 * index + bit) * drift_frontier_ranks + rank] =
                        {frontier[bit][rank].rank, frontier[bit][rank].center};
        }
        result.nomination = nomination;
        work.score_rows += request.slopes.size();
        return true;
    }
    receiver_detail::BulkReceipt receipt() const override { return owner_.receipt(); }
private:
    MirrorDriftBackend& owner_;
    receiver_detail::DriftBulkShape shape_;
    std::vector<std::vector<std::complex<float>>> staging_;
};

std::unique_ptr<receiver_detail::DriftBulk> MirrorDriftBackend::make_drift(
        const receiver_detail::DriftBulkShape& requested, bool, std::stop_token) {
    ++factories;
    shape = requested;
    if (mode_ == Mode::declined) return {};
    return std::make_unique<MirrorDrift>(*this, requested);
}

// The receiver's device drift plumbing against native scoring of the same
// observation: staging, per-call native fallbacks, policy declines and the
// unified-memory envelope leave every drift field bit-identical, and an
// exact ridge mirror changes no record, slope, frontier or likelihood. The
// staged-slope mirror stands in for a device slope route.
void drift_device_mirror() {
    using namespace receiver_detail;
    auto input = fixture(std::nullopt, true, false, 2026);
    input.config.frequency_bound_hz = 8;
    input.config.spread_delay_seconds = .007;
    input.config.spread_doppler_hz = 30;
    input.config.time_varying_channel = true;
    input.config.time_varying_common_doppler_hz = 8;
    constexpr unsigned workers = 2;
    const auto plan = drift_plan(input.config, input.slot, workers, false, 0, std::size_t{1} << 30);
    check(plan.workspace_bytes && plan.status == "linear_drift_ridge_proposals" &&
          plan.device_envelope_bytes == std::size_t{1} << 30, "the drift mirror fixture lost its ranked ridge plan");
    std::vector<FineCoordinate> timings;
    for (std::int64_t epoch = -5; epoch < 5; ++epoch) timings.push_back({epoch, .002 * (epoch % 3), 0, 0});
    ReceiverExecutor executor(workers);
    const auto run = [&](std::shared_ptr<ReceiverBulkBackend> backend,
                         DriftZeroSlopeEvaluation evaluation = DriftZeroSlopeEvaluation::reuse) {
        auto config = input.config;
        config.bulk_backend = std::move(backend);
        ReceiverSlotScore result;
        score_drift_fine(config, input.slot, input.key, plan, input.iq, timings, {}, workers, result, {}, executor,
            nullptr, evaluation);
        return result;
    };
    const auto same_bits = [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
    const auto identical = [&](const ReceiverSlotScore& a, const ReceiverSlotScore& b) {
        return same_bits(a.drift_bit_evidence[0], b.drift_bit_evidence[0]) &&
               same_bits(a.drift_bit_evidence[1], b.drift_bit_evidence[1]) && a.drift_timing_atoms == b.drift_timing_atoms &&
               a.drift_slopes == b.drift_slopes && a.drift_retained_digest == b.drift_retained_digest &&
               a.drift_coherent_timing_fft_rows == b.drift_coherent_timing_fft_rows &&
               a.drift_frequency_bins == b.drift_frequency_bins && a.drift_stage_workers == b.drift_stage_workers;
    };
    const auto native = run(nullptr);
    check(native.drift_timing_atoms > 0 && native.drift_relative_doppler_models == 2 && native.drift_proposal_route.empty() &&
          !native.drift_device_atoms && native.drift_retained_digest, "native drift lost its two signs or its digest");
    check(identical(native, run(nullptr, DriftZeroSlopeEvaluation::recompute)),
          "reusing native drift slopes must preserve both signs' exact evidence and retained proposals");
    using Mode = MirrorDriftBackend::Mode;
    const auto exact = std::make_shared<MirrorDriftBackend>(Mode::exact_ridge);
    const auto ridged = run(exact);
    check(identical(ridged, native) && exact->factories == 1 && exact->ridge_calls > 0 && exact->slope_calls > 0 &&
          ridged.drift_proposal_route == "device_fp32_fft_fp64_ridge_with_native_atoms" && ridged.drift_device_atoms &&
          exact->shape.lanes == std::min<std::size_t>(plan.stage_workers, timings.size()) &&
          exact->shape.rows == plan.rows && exact->shape.slope_budget == plan.slope_budget && exact->records.empty(),
          "an exact device ridge with native slopes must reproduce native drift bit for bit");
    // Every evaluation of the exact ridge mirror published a device ridge
    // before its slopes fell back: each counts as a device atom, and the
    // route names its native fallbacks. A ridge that also falls back
    // leaves no device atom.
    const auto fallback = std::make_shared<MirrorDriftBackend>(Mode::all_native);
    const auto fallen = run(fallback);
    check(identical(fallen, native) && fallback->ridge_calls > 0 && !fallback->slope_calls && !fallen.drift_device_atoms &&
          fallen.drift_proposal_route == "device_fp32_fft_fp64_ridge_with_native_atoms",
          "per-call native fallbacks must reproduce native drift bit for bit");
    const auto declined = std::make_shared<MirrorDriftBackend>(Mode::declined);
    const auto kept = run(declined);
    check(identical(kept, native) && declined->factories == 1 && kept.drift_proposal_route.empty(),
          "a declined drift session must leave native drift unchanged");
    const auto envelope = std::make_shared<MirrorDriftBackend>(Mode::envelope);
    check(identical(run(envelope), native) && !envelope->factories &&
          envelope->host_queries == static_cast<std::size_t>(std::bit_width(plan.slope_budget)) &&
          envelope->records.size() == 1 && envelope->records[0].first == StageReason::unified_memory_envelope &&
          envelope->records[0].second == StageNative::fallback,
          "a unified-memory lease outside the drift envelope must halve its batch, then record one fallback");
    const auto discrete = std::make_shared<MirrorDriftBackend>(Mode::envelope, false);
    check(identical(run(discrete), native) && !discrete->factories && discrete->records.size() == 1 &&
          discrete->records[0].first == StageReason::workspace && discrete->records[0].second == StageNative::fallback,
          "discrete-device host bytes outside the drift envelope must record a workspace fallback");
    // A plan whose stage envelope holds no slack keeps the route native.
    auto tight = plan;
    tight.device_envelope_bytes = tight.workspace_bytes;
    const auto starved = std::make_shared<MirrorDriftBackend>(Mode::exact_ridge);
    {
        auto config = input.config;
        config.bulk_backend = starved;
        ReceiverSlotScore result;
        score_drift_fine(config, input.slot, input.key, tight, input.iq, timings, {}, workers, result, {}, executor);
        check(identical(result, native) && !starved->factories && starved->records.size() == 1 &&
              starved->records[0].first == StageReason::workspace, "drift device tables need their own envelope slack");
    }
    const auto staged = std::make_shared<MirrorDriftBackend>(Mode::staged_slopes);
    const auto mirrored = run(staged);
    const auto difference = std::max(std::abs(mirrored.drift_bit_evidence[0] - native.drift_bit_evidence[0]),
                                     std::abs(mirrored.drift_bit_evidence[1] - native.drift_bit_evidence[1]));
    check(mirrored.drift_timing_atoms == native.drift_timing_atoms && mirrored.drift_slopes == native.drift_slopes &&
          mirrored.drift_device_atoms > 0 && mirrored.drift_proposal_route == "device_fp32_fft_fp64_ridge" &&
          std::isfinite(difference), "a staged-input slope route must keep the drift atoms and slopes");
    check(ridged.drift_device_atoms == mirrored.drift_device_atoms,
          "an evaluation whose device ridge published must count as a device atom after a slope fallback");
    // A drift-only FP64 reference on the same timing atoms (its own plan of
    // the same geometry): how far native FP32 proposals and the staged-slope
    // mirror sit from it. Reported, not gated: the qualified question is the
    // device being no worse than native FP32 against FP64.
    auto precise = input.config;
    precise.native_coarse_precision = NativeCoarsePrecision::fp64;
    const auto precise_plan = drift_plan(precise, input.slot, workers, false, 0, std::size_t{1} << 30);
    check(precise_plan.workspace_bytes && precise_plan.rows == plan.rows && precise_plan.nfft == plan.nfft &&
          precise_plan.slope_budget == plan.slope_budget && precise_plan.ridge_segments == plan.ridge_segments &&
          precise_plan.slow_ridge_segments == plan.slow_ridge_segments,
          "the FP64 drift reference must keep the FP32 plan's geometry");
    ReceiverSlotScore reference;
    score_drift_fine(precise, input.slot, input.key, precise_plan, input.iq, timings, {}, workers, reference, {}, executor);
    const auto from_reference = [&](const ReceiverSlotScore& score) {
        return std::max(std::abs(score.drift_bit_evidence[0] - reference.drift_bit_evidence[0]),
                        std::abs(score.drift_bit_evidence[1] - reference.drift_bit_evidence[1]));
    };
    check(reference.drift_timing_atoms == native.drift_timing_atoms && reference.drift_slopes == native.drift_slopes &&
          std::isfinite(from_reference(native)) && std::isfinite(from_reference(mirrored)),
          "the FP64 drift reference must score the same atoms and slopes");
    // A retained coherent line: the projected ranking copy feeds the same
    // staging, so an exact ridge mirror stays bit-identical to native and
    // the staged-slope mirror keeps the atoms and slopes.
    const auto line_plan = drift_plan(input.config, input.slot, workers, true, 0, std::size_t{1} << 30);
    check(line_plan.workspace_bytes && line_plan.project_data, "the drift mirror fixture lost its line-conditioned plan");
    const LineAtom line{0, input.iq.size(), 3.25, 0};
    const auto run_line = [&](std::shared_ptr<ReceiverBulkBackend> backend) {
        auto config = input.config;
        config.bulk_backend = std::move(backend);
        ReceiverSlotScore result;
        score_drift_fine(config, input.slot, input.key, line_plan, input.iq, timings, {}, workers, result, {}, executor,
                         &line);
        return result;
    };
    const auto line_native = run_line(nullptr);
    const auto line_exact = std::make_shared<MirrorDriftBackend>(Mode::exact_ridge);
    const auto line_ridged = run_line(line_exact);
    const auto line_staged = std::make_shared<MirrorDriftBackend>(Mode::staged_slopes);
    const auto line_mirrored = run_line(line_staged);
    check(line_native.drift_timing_atoms > 0 && line_native.drift_proposal_route.empty() &&
          identical(line_ridged, line_native) && line_exact->ridge_calls > 0 &&
          line_ridged.drift_proposal_route == "device_fp32_fft_fp64_ridge_with_native_atoms" &&
          line_ridged.drift_device_atoms == line_mirrored.drift_device_atoms &&
          line_mirrored.drift_timing_atoms == line_native.drift_timing_atoms &&
          line_mirrored.drift_slopes == line_native.drift_slopes && line_mirrored.drift_device_atoms > 0 &&
          line_mirrored.drift_proposal_route == "device_fp32_fft_fp64_ridge" && line_mirrored.drift_retained_digest,
          "a line-conditioned drift stage must keep its device route, atoms and slopes");
    const auto line_difference = std::max(std::abs(line_mirrored.drift_bit_evidence[0] - line_native.drift_bit_evidence[0]),
                                          std::abs(line_mirrored.drift_bit_evidence[1] - line_native.drift_bit_evidence[1]));
    std::cerr << "drift device mirror atoms=" << mirrored.drift_device_atoms << " digest "
              << (mirrored.drift_retained_digest == native.drift_retained_digest ? "equal" : "differs")
              << " |delta evidence|=" << difference << " ridge segments=" << staged->shape.segments
              << " slow segments=" << staged->shape.slow_segments
              << "; fp64 reference: native digest "
              << (native.drift_retained_digest == reference.drift_retained_digest ? "equal" : "differs")
              << " |delta|=" << from_reference(native) << ", staged digest "
              << (mirrored.drift_retained_digest == reference.drift_retained_digest ? "equal" : "differs")
              << " |delta|=" << from_reference(mirrored)
              << "; line: atoms=" << line_mirrored.drift_device_atoms << " digest "
              << (line_mirrored.drift_retained_digest == line_native.drift_retained_digest ? "equal" : "differs")
              << " |delta evidence|=" << line_difference << '\n';
}

// An ordinary (unextended) drift plan widens past its former eight lanes
// only into the largest other stage of its owner lease. At W10 the short
// drift slot is drift-led and keeps the eight-lane width and lease; at W24
// other stages leave headroom and it widens without changing its lease.
void ordinary_drift_width_keeps_owner_lease() {
    using namespace receiver_detail;
    constexpr auto unlimited = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, 2> shapes{};
    auto input = fixture(std::nullopt, true, false, 2026);
    input.config.frequency_bound_hz = 8;
    input.config.spread_delay_seconds = .007;
    input.config.spread_doppler_hz = 30;
    input.config.time_varying_channel = true;
    input.config.time_varying_common_doppler_hz = 8;
    const auto input_bytes = input.iq.size() * (sizeof(Complex) + sizeof(std::uint8_t)) + 64 * 1024;
    for (const unsigned workers : {10U, 24U}) {
        const auto floor = drift_plan(input.config, input.slot, workers);
        const auto floor_line = drift_plan(input.config, input.slot, workers, true);
        const auto unbounded = drift_plan(input.config, input.slot, workers, false, 0, unlimited);
        const auto admitted = receiver_drift_admission(input.config, input.slot, workers);
        const auto lease = receiver_slot_workspace(input.config, input.slot, workers);
        const auto stage = lease - input_bytes;
        const auto drift_stage = std::max(floor.workspace_bytes, admitted.line.workspace_bytes ?
            floor_line.workspace_bytes : std::size_t{0});
        check(floor.workspace_bytes && floor.stage_workers == 8 && unbounded.stage_workers == workers &&
              unbounded.workspace_bytes > floor.workspace_bytes && unbounded.nfft == floor.nfft &&
              unbounded.status == floor.status && admitted.ordinary.status == floor.status &&
              admitted.workspace_bytes == lease &&
              admitted.overlap_workspace_bytes == receiver_slot_overlap_workspace(input.config, input.slot, workers),
              "short drift fixture no longer plans an ordinary eight-lane floor below the worker budget");
        std::cerr << "ordinary drift W" << workers << " lease=" << lease << " stage=" << stage
                  << " floor=" << floor.workspace_bytes << '/' << floor.stage_workers
                  << " admitted=" << admitted.ordinary.workspace_bytes << '/' << admitted.ordinary.stage_workers
                  << " line=" << admitted.line.workspace_bytes << '/' << admitted.line.stage_workers
                  << " unbounded=" << unbounded.workspace_bytes << '\n';
        ++shapes[drift_stage != stage];
        if (drift_stage == stage) {
            // Drift sets the lease: no other stage leaves headroom.
            check(admitted.ordinary.stage_workers == floor.stage_workers &&
                  admitted.ordinary.workspace_bytes == floor.workspace_bytes && unbounded.workspace_bytes > stage,
                  "a drift-led owner lease must keep the former drift width and quote");
        } else {
            const auto wider = drift_plan(input.config, input.slot, admitted.ordinary.stage_workers + 1, false, 0,
                                          unlimited);
            // Unbounded widening would make drift lead and raise this lease.
            check(drift_stage < stage && unbounded.workspace_bytes > stage &&
                  admitted.ordinary.stage_workers > floor.stage_workers &&
                  admitted.ordinary.workspace_bytes <= stage && admitted.line.workspace_bytes <= stage &&
                  (admitted.ordinary.stage_workers == workers || wider.workspace_bytes > stage),
                  "ordinary drift must take the widest width inside other-stage headroom");
        }
        const auto scored = score_receiver_slot(input.config, input.slot, input.key, input.iq, input.valid, workers,
                                                workspace_limit);
        check(scored.complete && scored.workspace_peak_bytes == lease &&
              scored.drift_planned_stage_workers == admitted.ordinary.stage_workers &&
              scored.drift_line_planned_stage_workers == admitted.line.stage_workers &&
              scored.drift_timing_atoms && scored.drift_stage_workers &&
              scored.drift_stage_workers <= admitted.ordinary.stage_workers,
              "a drift slot must execute its admitted width inside the unchanged lease");
    }
    check(shapes[0] && shapes[1], "ordinary drift width fixtures no longer cover drift-led and headroom leases");
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
        settings.data_cadence = live::DataCadence::scheduled;
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
    // W24 widens past the former eight-lane drift cap only inside the same
    // preserved owner lease.
    check(smaller.workspace_bytes == 276033982ULL && larger.workspace_bytes == 453996094ULL &&
          smaller.ordinary.workspace_bytes && smaller.line.workspace_bytes &&
          smaller.ordinary.stage_workers > 0 && smaller.ordinary.stage_workers < 8 &&
          smaller.line.stage_workers > 0 && smaller.line.stage_workers < 8 &&
          larger.ordinary.stage_workers == 9 && larger.line.stage_workers == 9 &&
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
    class FrequencyWorkspaceBackend final : public ReceiverBulkBackend {
    public:
        explicit FrequencyWorkspaceBackend(bool device_local) : device_local_(device_local) {}
        std::unique_ptr<FineBulk> make_fine(double, double) override { return {}; }
        std::unique_ptr<CoarseBulk> make_coarse(double, double) override { return {}; }
        std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
        std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
        std::optional<CoarseFrequencyPlan> frequency_plan(const CoarseBulkGeometry& geometry,
                std::size_t workspaces, std::size_t) const override {
            constexpr std::size_t device_lease = std::size_t{2} << 30;
            return CoarseFrequencyPlan{geometry.frequency_tile_width, workspaces, device_lease,
                device_local_ ? 0 : device_lease, {}, device_local_ ? device_lease : 0};
        }
        BulkReceipt receipt() const override { return {}; }
    private:
        bool device_local_;
    };
    backend_config.bulk_backend = std::make_shared<FrequencyWorkspaceBackend>(false);
    const auto shared_memory = receiver_drift_admission(backend_config, backend_slot, workers);
    backend_config.bulk_backend = std::make_shared<FrequencyWorkspaceBackend>(true);
    const auto device_memory = receiver_drift_admission(backend_config, backend_slot, workers);
    const auto same_drift_geometry = [](const DriftPlan& a, const DriftPlan& b) {
        return a.workspace_bytes && a.workspace_bytes == b.workspace_bytes && a.stage_workers == b.stage_workers &&
            a.sample_stride == b.sample_stride && a.nfft == b.nfft && a.slope_budget == b.slope_budget &&
            a.frequency_radius == b.frequency_radius && a.slope_radius == b.slope_radius && a.status == b.status;
    };
    check(same_drift_geometry(shared_memory.ordinary, device_memory.ordinary) &&
          same_drift_geometry(shared_memory.line, device_memory.line) &&
          device_memory.workspace_bytes < shared_memory.workspace_bytes &&
          device_memory.workspace_bytes > device_memory.ordinary.workspace_bytes &&
          device_memory.workspace_bytes > device_memory.line.workspace_bytes &&
          device_memory.ordinary.device_envelope_bytes <= device_memory.workspace_bytes &&
          device_memory.line.device_envelope_bytes <= device_memory.workspace_bytes,
          "device-local accounting reduced admissible drift or recharged the device lease to host memory");
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
              cached.drift_slopes == recomputed.drift_slopes && cached.drift_retained_digest == recomputed.drift_retained_digest &&
              cached.drift_coherent_timing_fft_rows > 0 &&
              cached.drift_coherent_timing_fft_rows == recomputed.drift_coherent_timing_fft_rows,
              "cached drift rank work must preserve recomputed native likelihood, proposals and selected public atoms");
        check(cached.drift_render_seconds > 0 && cached.drift_ridge_seconds > 0 &&
              cached.drift_ridge_search_seconds > 0 && cached.drift_slope_seconds > 0 &&
              cached.drift_exact_seconds > 0, "every executed drift phase must report its lane-summed time");
        std::cerr << "cached/recomputed drift evidence max difference=" << difference << '\n';
    }
    if (cache_only) return;
    const auto score = score_receiver_slot(config, slot, profile.key, iq, {}, workers, memory_limit);
    check(score.complete && score.all_valid && score.zero_h0_verified && score.bit == bit &&
          score.decision == SlotDecision::data && score.operational_passes_conditional_bound &&
          score.drift_relative_doppler_models == 2 && score.drift_slopes >= 6 &&
          score.drift_timing_atoms >= 16 && score.drift_timing_atoms <= 20 &&
          score.drift_coherent_timing_fft_rows > 0 && score.drift_stage_workers == 15 &&
          score.drift_planned_stage_workers == 15 && score.drift_line_planned_stage_workers == 14 &&
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
    // Quiet-aperture blocks project on any number of lanes. Their mode terms
    // are summed in public order, so every lane count reports the same bits.
    if (!calendar_bound) {
        std::array<ReceiverSlotScore, 3> lanes;
        for (std::size_t i = 0; i < lanes.size(); ++i) {
            const unsigned workers = i ? 3 * static_cast<unsigned>(i) : 1;
            receiver_detail::ReceiverExecutor executor(workers);
            receiver_detail::score_varying_quiet(input.config, input.slot, input.iq, lanes[i], {}, workers, executor);
            check(lanes[i].varying_quiet_modes == expanded.varying_quiet_modes &&
                  std::bit_cast<std::uint64_t>(lanes[i].varying_quiet_energy) ==
                      std::bit_cast<std::uint64_t>(expanded.varying_quiet_energy) &&
                  lanes[i].varying_quiet_threshold == expanded.varying_quiet_threshold &&
                  lanes[i].varying_quiet_status == expanded.varying_quiet_status,
                  "varying quiet energy changed with its projection lanes");
        }
    }

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

// The signed-gain fixtures (triad_signed_gain_fixture.hpp), shared with the
// Vulkan gain-rank checks.
using namespace signed_gain_fixtures;

void scheduled_fill_preserves_midpoint_search() {
    using namespace receiver_detail;
    auto input = signed_gain_fixture(-1);
    auto& slot = input.slot;
    slot.scheduled_gap_fill = true;
    slot.protected_data_first_sample = slot.first_sample + 17;
    slot.protected_data_stop_sample = slot.stop_sample - 29;
    slot.quiet_first_sample = std::max(slot.quiet_first_sample, slot.protected_data_first_sample);
    slot.quiet_stop_sample = std::min(slot.quiet_stop_sample, slot.protected_data_stop_sample);
    const auto lease = protected_slot_workspace(input.config, slot, 2);
    const auto count = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
    const auto changed = [&] {
        auto iq = input.iq;
        for (auto i : {std::size_t{0}, std::size_t{16}, count - 29, count - 1})
            iq[i] += Complex{100, 200};
        return iq;
    }();
    const auto score = [&](std::span<const Complex> iq) {
        return score_protected_data_slot(input.config, slot, Crypto::amateur(), iq, {}, 2,
            lease, {}, nullptr, nullptr);
    };
    const auto baseline = score(input.iq), filled = score(changed);
    check(baseline.complete && baseline.decision == SlotDecision::data && baseline.bit == 0 &&
          baseline.midpoint_timing_lags > 0 && baseline.varying_frequency_states > 0,
          "scheduled fill must keep the full midpoint and varying receiver families");
    check(filled.complete && filled.bit == baseline.bit &&
          filled.operational_bit_evidence == baseline.operational_bit_evidence &&
          filled.quiet_energy == baseline.quiet_energy &&
          filled.midpoint_timing_scored_lags == baseline.midpoint_timing_scored_lags &&
          filled.coarse_frequency_points == baseline.coarse_frequency_points &&
          filled.coarse_arrival_points == baseline.coarse_arrival_points &&
          !filled.protected_discovery_attempted,
          "possible fill must be masked before all proposal families with one full public search");
}

void scheduled_fill_preserves_large_tracking_tail() {
    using namespace receiver_detail;
    constexpr unsigned rate = 96'000;
    constexpr std::size_t block = 12'000, trim = 100;
    ReceiverConfig config;
    config.sample_rate_hz = rate;
    config.data_if_hz = 24'000;
    config.noise_variance = 1;
    config.frequency_bound_hz = 4;
    config.spread_delay_seconds = .001;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 4;
    ReceiverSlotPlan full;
    full.first_sample = rate;
    full.stop_sample = full.first_sample + 2 * block;
    full.window_capture_lead_seconds = -2;
    full.source_start_seconds = .5;
    full.format.sample_rate = rate;
    full.format.inner_rate_hz = 96;
    full.format.added_symbols = 1;
    full.format.bit_seconds = .001;
    auto cropped = full;
    cropped.first_sample += trim;
    cropped.window_capture_lead_seconds -= static_cast<long double>(trim) / rate;
    const auto original = varying_fine_plan(config, full, 1);
    const auto unpreserved = varying_fine_plan(config, cropped, 1);
    const auto preserved = varying_fine_plan(config, cropped, 1, false, false, block);
    const auto originally_untracked = varying_fine_plan(config, full, 1, false, false, 2 * block - trim);
    check(original.block_rows == block && original.track_frequency &&
          unpreserved.block_rows == block && !unpreserved.track_frequency &&
          preserved.block_rows == block && preserved.track_frequency &&
          preserved.nominal_independent_gain_candidates == original.nominal_independent_gain_candidates,
          "a merged protected tail must keep the public varying frequency and gain families");
    check(originally_untracked.block_rows == block && !originally_untracked.track_frequency,
          "a shorter protected tail must keep the public fixed-frequency varying family");
    const auto full_line = varying_fine_plan(config, full, 1, true);
    const auto protected_line = varying_fine_plan(config, cropped, 1, true, false, block);
    check(full_line.track_frequency && protected_line.track_frequency,
          "a merged protected tail must keep the line-conditioned tracker");

    const auto key = Crypto::amateur();
    PatternSource source(full.format, key, 0);
    std::vector<Complex> iq(2 * block);
    source.render(rate + rate / 2, iq);
    const std::array timing{FineCoordinate{}};
    ReceiverExecutor executor(1);
    const auto score = [&](const ReceiverSlotPlan& slot, const VaryingFinePlan& plan, std::span<const Complex> samples) {
        ReceiverSlotScore result;
        score_varying_fine(config, slot, key, plan, samples, timing, {}, rate, 0, 1, result, {}, executor);
        return result;
    };
    const auto public_score = score(full, original, iq);
    const auto protected_score = score(cropped, preserved, std::span(iq).subspan(trim));
    check(public_score.varying_blocks == 2 && protected_score.varying_blocks == 1 &&
          public_score.varying_frequency_states > 1 &&
          protected_score.varying_frequency_states == public_score.varying_frequency_states &&
          protected_score.tracked_frequency_log_prior_adjustment == public_score.tracked_frequency_log_prior_adjustment &&
          protected_score.nominal_independent_gain_frequency_states == public_score.nominal_independent_gain_frequency_states &&
          std::ranges::all_of(protected_score.tracked_frequency_bit_evidence,
              [](double evidence) { return std::isfinite(evidence); }),
          "merged protected tracking must score the full tail on its original lattice");
}

void signed_gain_timing_and_exact_admission() {
    using namespace receiver_detail;
    const auto profile_config = signed_gain_profile_config();
    const auto config = signed_gain_receiver_config(profile_config);
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

    const auto short_fixture = signed_gain_fixture(-1);
    const PublicProfile short_profile(short_fixture.profile_config);
    const auto& slot = short_fixture.slot;
    const auto& iq = short_fixture.iq;
    const auto key = Crypto::amateur();
    const auto realized_scale = short_fixture.rate;
    check(iq.size() < 25'000, "signed gain timing fixture must remain bounded");
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
    for (const int bit : {0, 1}) {
        const auto fixture = signed_gain_fixture(bit);
        const auto& wide_config = fixture.config;
        const auto& aged = fixture.slot;
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
        const auto& aged_iq = fixture.iq;
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

// The per-block shift expression used before exact shift runs, kept here as an
// independent reference.
std::int64_t reference_gain_shift(const receiver_detail::GainShiftGeometry& g,
                                  const receiver_detail::FineCoordinate& coordinate, std::size_t block) {
    const auto first = block * g.block_rows;
    const auto rows = std::min(g.block_rows, g.kernel_rows - first);
    const auto time = g.origin + (first + (rows - 1.L) / 2) / g.output_fs - (g.taps - 1.L) / (2 * g.fs);
    const auto epsilon = coordinate.clock_ppm * 1e-6L;
    return std::llround(g.output_fs *
        (coordinate.arrival_seconds - g.arrival_lower_seconds - time * epsilon / (1 + epsilon)));
}

void reference_gain_shifts(const receiver_detail::GainShiftMap& map,
                           std::span<const receiver_detail::FineCoordinate> coordinates, std::size_t block,
                           std::span<std::int64_t> shifts) {
    for (std::size_t lag = 0; lag < shifts.size(); ++lag)
        shifts[lag] = reference_gain_shift(map.geometry(), coordinates[lag], block);
}

// Visits every block in the rank's lane order with the production cursor and
// compares its vector and extremes with the per-block reference at every
// re-evaluation and at every `stride`-th block (all blocks when 1). Returns
// the shift vectors evaluated.
std::uint64_t check_gain_shift_lanes(const receiver_detail::GainShiftMap& map,
                                     std::span<const receiver_detail::FineCoordinate> coordinates,
                                     std::span<const std::size_t> samples, unsigned workers, bool full_direct,
                                     const char* reason, std::size_t stride = 1) {
    using namespace receiver_detail;
    const auto blocks = map.blocks();
    std::vector<std::uint8_t> sampled(blocks);
    for (const auto block : samples) sampled[block] = 1;
    const auto fixed_lanes = std::min<std::size_t>(workers, samples.size());
    const auto rank_lanes = full_direct ? std::min<std::size_t>(workers, blocks) : fixed_lanes;
    std::vector<std::int64_t> shifts(map.lags()), reference(map.lags());
    std::uint64_t vectors = 0;
    std::size_t visits = 0;
    const auto visit = [&](GainShiftCursor& cursor, std::size_t block, std::size_t horizon) {
        const auto evaluated = cursor.seek(map, block, horizon, shifts);
        ++visits;
        if (!evaluated && block % stride && block + 1 != blocks) return;
        reference_gain_shifts(map, coordinates, block, reference);
        const auto [low, high] = std::ranges::minmax(reference);
        check(shifts == reference && cursor.first_shift == low && cursor.last_shift == high &&
              cursor.begin <= block && block < cursor.end, reason);
    };
    for (std::size_t lane = 0; lane < rank_lanes; ++lane) {
        GainShiftCursor cursor;
        if (lane < fixed_lanes)
            for (auto sample = lane; sample < samples.size(); sample += fixed_lanes)
                visit(cursor, samples[sample], sample + fixed_lanes < samples.size() ?
                      samples[sample + fixed_lanes] : blocks);
        if (full_direct && samples.size() < blocks)
            for (auto block = lane; block < blocks; block += rank_lanes) {
                if (sampled[block]) continue;
                auto next = block + rank_lanes;
                while (next < blocks && sampled[next]) next += rank_lanes;
                visit(cursor, block, std::min(next, blocks));
            }
        vectors += cursor.vectors;
    }
    check(visits == (full_direct ? blocks : samples.size()) && vectors <= visits, reason);
    return vectors;
}

// Exact run enumeration: every block of a run has the run's vector, adjacent
// runs differ, and the run lookup agrees. Returns the run count.
std::size_t check_gain_shift_runs(const receiver_detail::GainShiftMap& map,
                                  std::span<const receiver_detail::FineCoordinate> coordinates, const char* reason) {
    using namespace receiver_detail;
    const auto runs = gain_shift_runs(map, map.blocks());
    const auto lags = map.lags();
    check(!runs.overflow && !runs.range && !runs.first_blocks.empty() && runs.first_blocks.front() == 0 &&
          runs.count == runs.first_blocks.size() && runs.relative.size() == runs.first_blocks.size() * lags, reason);
    std::vector<std::int64_t> reference(lags), previous;
    for (std::size_t run = 0; run < runs.first_blocks.size(); ++run) {
        const auto end = run + 1 < runs.first_blocks.size() ? runs.first_blocks[run + 1] : map.blocks();
        std::vector<std::int64_t> vector(lags);
        for (std::size_t lag = 0; lag < lags; ++lag)
            vector[lag] = runs.first_shift[run] + runs.relative[run * lags + lag];
        check(vector != previous && std::ranges::min(vector) == runs.first_shift[run] &&
              std::ranges::max(vector) == runs.last_shift[run], reason);
        for (auto block = runs.first_blocks[run]; block < end; ++block) {
            reference_gain_shifts(map, coordinates, block, reference);
            check(reference == vector && runs.run(block) == run, reason);
        }
        previous = std::move(vector);
    }
    if (runs.first_blocks.size() > 1) {
        const auto capped = gain_shift_runs(map, runs.first_blocks.size() - 1);
        check(capped.overflow && !capped.range && capped.first_blocks.empty() && capped.relative.empty() &&
              capped.count == runs.first_blocks.size(),
              "signed gain shift runs must report overflow, with the overflowing count, instead of truncating");
    }
    return runs.first_blocks.size();
}

void signed_gain_shift_runs() {
    using namespace receiver_detail;
    // Receiver fixtures: the short aged slot and both aged later slots.
    for (const int aged : {-1, 0, 1}) {
        const auto fixture = signed_gain_fixture(aged);
        const auto setup = coarse_gain_shift_setup(fixture.config, fixture.slot, 2);
        const GainShiftMap map(setup.geometry, setup.coordinates);
        check(map.blocks() == setup.blocks && map.lags() == setup.coordinates.size(),
              "signed gain shift setup must describe the planned rank");
        for (const unsigned workers : {1u, 2u, 3u, 7u})
            check_gain_shift_lanes(map, setup.coordinates, setup.sampled, workers, setup.full_direct,
                                   "signed gain shift runs must reproduce every per-block shift");
        check(check_gain_shift_runs(map, setup.coordinates, "signed gain run enumeration must be exact") > 1,
              "the 150 ppm signed gain fixtures must exercise shift run boundaries");
    }
    // Synthetic 72 Hz/5x shape: 3028 32-row blocks at 750 Hz and 10553 lags,
    // with a disciplined clock (one run) and a 150 ppm clock (a run per block).
    GainShiftGeometry geometry{750, 9000, 513, 32, 3028 * 32 - 5, -(3028 * 32 - 5) / 1500.L, .0125};
    for (const double bound : {.01, 150.}) {
        std::vector<FineCoordinate> coordinates(10553);
        for (std::size_t lag = 0; lag < coordinates.size(); ++lag)
            coordinates[lag] = {0, .0125 + (lag + .25) / 750., 0, bound * std::sin(.37 * lag)};
        const GainShiftMap map(geometry, coordinates);
        std::vector<std::size_t> samples;
        for (std::size_t sample = 0; sample < 256; ++sample) samples.push_back(sample * map.blocks() / 256 + sample % 7);
        const auto vectors = check_gain_shift_lanes(map, coordinates, samples, 24, true,
                                                    "synthetic signed gain shift runs must stay exact", 16);
        // The lane check above already compared every block with the reference.
        if (bound < 1)
            check(vectors <= 2 * 24 && !gain_shift_runs(map, 1).overflow,
                  "a disciplined clock must form one run, evaluated at most once per lane pass");
        else
            check(gain_shift_runs(map, 64).overflow && gain_shift_runs(map, 64).count == 65,
                  "a 150 ppm 72 Hz/5x map must exceed the device run limit");
    }
    // Relative shifts beyond the int32 table range are reported apart from the run cap.
    const std::array wide{FineCoordinate{0, 0, 0, 0}, FineCoordinate{0, 3e6, 0, 0}};
    const auto range = gain_shift_runs(GainShiftMap(geometry, wide), 64);
    check(range.overflow && range.range && range.count == 1 && range.first_blocks.empty(),
          "a shift span beyond the int32 table must overflow as a range error");
}

void signed_gain_certification_contract() {
    using namespace receiver_detail;
    constexpr std::size_t lags = 40, audits = 4, unsampled = 1000;
    constexpr std::size_t cells = 2 * lags;
    // Native totals N = S + U with one clear winner per bit; the rescoring
    // returns U exactly, the device U plus an error.
    std::vector<double> native(cells), sampled(cells), exact(cells), device(cells);
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::size_t lag = 0; lag < lags; ++lag) {
            const auto i = bit * lags + lag;
            native[i] = 1000 + 40 * std::cos(.3 * lag + bit) + (lag == 7 + 9 * bit ? 100 : 0);
            sampled[i] = .1 * native[i];
            exact[i] = native[i] - sampled[i];
        }
    std::vector<double> audit_native(audits * cells);
    for (std::size_t i = 0; i < audit_native.size(); ++i) audit_native[i] = 1 + 20 * std::sin(.7 * i);
    const auto hash = [](std::size_t i) {
        const auto x = std::sin(12.9898 * static_cast<double>(i + 1)) * 43758.5453;
        return x - std::floor(x) - .5;
    };
    struct Case {
        double device_error = 1e-6, audit_error = 1e-6, spike = 0;
        std::size_t audits = 4;
    };
    const auto run = [&](const Case& c, const GainCertificationLimits& limits) {
        for (std::size_t i = 0; i < cells; ++i) device[i] = exact[i] + c.device_error * hash(i);
        device[7] += c.spike;
        std::vector<float> audit_device(c.audits * cells);
        for (std::size_t i = 0; i < audit_device.size(); ++i)
            audit_device[i] = static_cast<float>(audit_native[i] + c.audit_error * hash(7 * i + 3));
        const auto rescore = [&](std::array<std::span<const std::size_t>, 2> frontier,
                                 std::array<std::span<double>, 2> sums, std::array<std::span<double>, 2> magnitudes) {
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t j = 0; j < frontier[bit].size(); ++j) {
                    sums[bit][j] = exact[bit * lags + frontier[bit][j]];
                    magnitudes[bit][j] = std::abs(sums[bit][j]);
                }
        };
        return certify_gain_direct_rank({lags, unsampled, unsampled + c.audits, sampled, device,
            std::span<const double>(audit_native).first(c.audits * cells), audit_device}, limits, rescore);
    };
    const GainCertificationLimits limits;
    const auto clean = run({}, limits);
    check(clean.fallback.empty() && clean.bits[0].best_lag == 7 && clean.bits[1].best_lag == 16 &&
          clean.rescored_lags == clean.frontier_lags && clean.frontier_lags == 2 * limits.frontier_minimum &&
          clean.bits[0].band >= clean.bits[0].floor_band && clean.bits[0].band >= clean.bits[0].audit_band,
          "certification must publish the native argmax of a clean device rank");
    for (std::size_t i = 0; i < cells; ++i)
        check(std::isinf(clean.nomination[i]) || std::abs(clean.nomination[i] - native[i]) <= 1e-12 * native[i],
              "certified nominations must use FP64 rescored totals on the frontier only");
    // Larger audited errors widen the band and grow the frontier, never the lags.
    auto previous = clean.frontier_lags;
    for (const double error : {1e-2, 1e-1, .5}) {
        const auto wider = run({error, error}, limits);
        check(wider.fallback.empty() && wider.frontier_lags >= previous && wider.bits[0].best_lag == 7 &&
              wider.bits[1].best_lag == 16 && wider.bits[0].band_source == "audit",
              "a wider audit band must grow the frontier while publishing native lags");
        previous = wider.frontier_lags;
    }
    check(previous > clean.frontier_lags, "the audit band must be able to grow the frontier");
    // A device error on a frontier lag beyond the audited band.
    const auto violated = run({.spike = 10 * clean.bits[0].band + 1}, limits);
    check(violated.fallback == gain_certification_band && violated.bits[0].maximum_error > violated.bits[0].band,
          "a device total outside its band on the frontier must request native");
    auto narrow = limits;
    narrow.frontier_maximum = 12;
    const auto overflow = run({1, 2}, narrow);
    check(overflow.fallback == gain_certification_overflow && overflow.bits[0].frontier > 12,
          "a frontier beyond its predeclared size must request native");
    // Two native totals closer than the roundoff threshold.
    exact[3] = native[7] - 1e-9 - sampled[3];
    const auto tie = run({}, limits);
    check(tie.fallback == gain_certification_tie && tie.bits[0].top2_gap < tie.bits[0].tie_threshold,
          "a native near-tie must request native");
    exact[3] = native[3] - sampled[3];
    auto strict = limits;
    strict.near_tie = 1;
    check(run({}, strict).fallback == gain_certification_tie, "a forced near-tie threshold must request native");
    check(run({.audits = 0}, limits).fallback == gain_certification_no_audit,
          "certification without audit blocks must request native");
    check(run({.device_error = std::numeric_limits<double>::quiet_NaN()}, limits).fallback == gain_certification_nonfinite,
          "nonfinite device totals must request native");
    check(run({.audit_error = std::numeric_limits<double>::infinity()}, limits).fallback == gain_certification_nonfinite,
          "nonfinite device audit values must request native");
}

// Clean per-block mirror values of one fixture's request, shared by its trials.
struct MirrorGainValues {
    receiver_detail::GainRankShape shape{};
    std::vector<float> values;                    // [block][2][lags]
};

// C++ stand-in for the unfused device kernels behind the backend seam:
// FP64 native FFTs of the device tile, then the FP32 emission
// g^2|r|^2 - logdet and two-pass FP32 log-mean-exp per block, and FP64
// block-order sums. Optional perturbations exercise the certification. Like
// the Vulkan factory, every factory call counts a planned call; a declined
// or fallen-back session, or a refused call, records the backend's own
// reason and counter in its receipt, and the receiver's own reasons arrive
// through record_stage_native.
struct MirrorGainBackend final : receiver_detail::ReceiverBulkBackend {
    double noise = 0, audit_noise = 0, spike = 0;
    std::size_t spike_lag = 0;
    // Backend reasons; empty accepts. A session fallback is the factory's
    // planned fallback, such as a refused lease.
    std::string_view session_decline, session_fallback, call_decline;
    bool silent = false;                              // decline the session without a reason
    std::size_t sessions = 0, calls = 0, releases = 0;
    bool released_at_certification = false;
    receiver_detail::GainRankShape shape{};
    std::size_t blocks = 0, audits = 0, observation_rows = 0, max_outputs = 0;
    std::shared_ptr<MirrorGainValues> cache;
    receiver_detail::BulkReceipt work;
    struct Native {
        receiver_detail::StageFamily family;
        receiver_detail::StageReason reason;
        receiver_detail::StageNative kind;
    };
    std::vector<Native> natives;                      // the receiver's own records, in order

    struct Session final : receiver_detail::GainRankBulk {
        MirrorGainBackend& backend;
        receiver_detail::GainRankShape shape;
        Session(MirrorGainBackend& owner, const receiver_detail::GainRankShape& planned)
            : backend(owner), shape(planned) {}
        bool direct_ranks(const receiver_detail::GainRankRequest& request, std::span<double> totals,
                          std::span<float> audit, receiver_detail::StageDeviceWork&, std::stop_token stop) override {
            return backend.direct_ranks(shape, request, totals, audit, stop);
        }
        receiver_detail::BulkReceipt receipt() const override { return backend.work; }
        void release() override { ++backend.releases; }
        void record_certification(const receiver_detail::GainRankCertificationRecord&) override {
            backend.released_at_certification = backend.releases == backend.calls;
        }
    };
    std::unique_ptr<receiver_detail::GainRankBulk> make_gain_rank(const receiver_detail::GainRankShape& planned,
                                                                  bool fp64_forced, std::stop_token) override {
        ++sessions;
        shape = planned;
        check(!fp64_forced, "the signed-gain fixtures request no FP64 coarse search");
        auto& stage = work.vulkan_gain_rank;
        ++stage.planned_calls;
        if (silent) return nullptr;
        if (!session_decline.empty()) {
            ++stage.declined_calls;
            if (stage.decline_reason.empty()) stage.decline_reason = session_decline;
            return nullptr;
        }
        if (!session_fallback.empty()) {
            ++stage.native_fallbacks;
            if (stage.fallback_reason.empty()) stage.fallback_reason = session_fallback;
            return nullptr;
        }
        return std::make_unique<Session>(*this, planned);
    }
    // The mirror's policy accepts every shape; its factory and calls decline.
    std::string_view stage_decline(const receiver_detail::StageShape&, bool) const noexcept override { return {}; }
    void record_stage_native(receiver_detail::StageFamily family, receiver_detail::StageReason reason,
                             receiver_detail::StageNative kind) override {
        natives.push_back({family, reason, kind});
    }
    std::unique_ptr<receiver_detail::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<receiver_detail::CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    receiver_detail::BulkReceipt receipt() const override { return work; }

    bool direct_ranks(const receiver_detail::GainRankShape& s, const receiver_detail::GainRankRequest& request,
                      std::span<double> totals, std::span<float> audit, std::stop_token stop) {
        using namespace receiver_detail;
        ++calls;
        blocks = request.blocks.size();
        audits = audit.size() / (2 * s.lags);
        observation_rows = request.observation.size();
        max_outputs = 0;
        for (const auto& block : request.blocks) max_outputs = std::max<std::size_t>(max_outputs, block.outputs);
        if (!call_decline.empty()) {
            ++work.vulkan_gain_rank.native_fallbacks;
            if (work.vulkan_gain_rank.fallback_reason.empty()) work.vulkan_gain_rank.fallback_reason = call_decline;
            return false;
        }
        const auto tile = s.tile_fft_rows, lags = s.lags, frequencies = s.direct_frequencies;
        const auto capacity = tile - s.block_rows + 1;
        const auto unit_bin = static_cast<std::int64_t>(s.frequency_stride * (tile / (2 * s.block_rows)));
        const modem::detail::search_fft::DoublePlan plan(tile, stop);
        const auto unsampled = request.blocks.size() - audits;
        std::vector<double> sums(2 * lags);
        std::vector<float> values(2 * lags), emissions(2 * lags * frequencies);
        std::array<std::vector<Complex>, 2> spectra{std::vector<Complex>(tile), std::vector<Complex>(tile)};
        std::vector<Complex> y(tile), product(tile), correlations;
        const auto transform = [&](std::span<const Complex> input, std::span<Complex> output, bool inverse) {
            std::ranges::copy(input, output.begin());
            std::fill(output.begin() + input.size(), output.end(), Complex{});
            modem::detail::search_fft::fft(output, inverse, plan, stop);
        };
        const auto hash = [](std::size_t i) {
            const auto x = std::sin(12.9898 * static_cast<double>(i % 1000003 + 1)) * 43758.5453;
            return x - std::floor(x) - .5;
        };
        const bool cached = cache && cache->shape == s && cache->values.size() == request.blocks.size() * 2 * lags;
        if (cache && !cached) {
            cache->shape = s;
            cache->values.assign(request.blocks.size() * 2 * lags, 0.f);
        }
        for (std::size_t b = 0; b < request.blocks.size(); ++b) {
            const auto cell_of = [&](std::size_t bit, std::size_t lag) { return (b * 2 + bit) * lags + lag; };
            if (cached) {
                for (std::size_t bit = 0; bit < 2; ++bit)
                    for (std::size_t lag = 0; lag < lags; ++lag) {
                        const auto cell = cell_of(bit, lag);
                        values[bit * lags + lag] = cache->values[cell] +
                            static_cast<float>((b < unsampled ? noise : noise + audit_noise) * hash(cell));
                    }
                if (b < unsampled) {
                    for (std::size_t i = 0; i < sums.size(); ++i) sums[i] += values[i];
                } else {
                    std::ranges::copy(values, audit.begin() + static_cast<std::ptrdiff_t>((b - unsampled) * 2 * lags));
                }
                continue;
            }
            const auto& block = request.blocks[b];
            const auto relative = request.run_shifts.subspan(block.run * lags, lags);
            const auto input = request.observation.subspan(static_cast<std::size_t>(block.earliest),
                                                           block.outputs + block.rows - 1);
            correlations.assign(2 * frequencies * block.outputs, {});
            for (std::size_t bit = 0; bit < 2; ++bit)
                transform(request.direct_columns[bit].subspan(block.first, block.rows), spectra[bit], false);
            for (std::size_t start = 0; start < block.outputs; start += capacity) {
                const auto outputs = std::min(capacity, block.outputs - start);
                transform(input.subspan(start, outputs + block.rows - 1), y, false);
                for (std::size_t bit = 0; bit < 2; ++bit)
                    for (std::size_t f = 0; f < frequencies; ++f) {
                        const auto bin = (static_cast<std::int64_t>(f) - static_cast<std::int64_t>(s.direct_radius)) * unit_bin;
                        for (std::size_t k = 0; k < tile; ++k)
                            product[k] = y[(k + static_cast<std::size_t>(bin)) & (tile - 1)] * std::conj(spectra[bit][k]);
                        modem::detail::search_fft::fft(product, true, plan, stop);
                        std::copy_n(product.begin(), outputs,
                                    correlations.begin() + static_cast<std::ptrdiff_t>((bit * frequencies + f) * block.outputs + start));
                    }
            }
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const auto g = static_cast<float>(request.correlation_scale / block.scale[bit]);
                const auto logdet = static_cast<float>(block.logdet[bit]);
                for (std::size_t lag = 0; lag < lags; ++lag) {
                    const auto at = static_cast<std::size_t>(relative[lag]);
                    float maximum = -std::numeric_limits<float>::infinity();
                    auto* e = &emissions[(bit * lags + lag) * frequencies];
                    for (std::size_t f = 0; f < frequencies; ++f) {
                        const auto r = correlations[(bit * frequencies + f) * block.outputs + at];
                        const auto re = static_cast<float>(r.real()), im = static_cast<float>(r.imag());
                        e[f] = g * g * (re * re + im * im) - logdet;
                        maximum = std::max(maximum, e[f]);
                    }
                    float total = 0;
                    for (std::size_t f = 0; f < frequencies; ++f) total += std::exp(e[f] - maximum);
                    auto value = maximum + std::log(total / static_cast<float>(frequencies));
                    const auto cell = cell_of(bit, lag);
                    if (cache) cache->values[cell] = value;
                    value += static_cast<float>((b < unsampled ? noise : noise + audit_noise) * hash(cell));
                    values[bit * lags + lag] = value;
                }
            }
            if (b < unsampled) {
                for (std::size_t i = 0; i < sums.size(); ++i) sums[i] += values[i];
            } else {
                std::ranges::copy(values, audit.begin() + static_cast<std::ptrdiff_t>((b - unsampled) * 2 * lags));
            }
        }
        sums[spike_lag] += spike;
        std::ranges::copy(sums, totals.begin());
        return true;
    }
};

bool same_bits(std::span<const double> a, std::span<const double> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) {
        return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
    });
}

// The native rank, bit for bit: totals, nominations, scores and work.
void same_native_rank(const receiver_detail::CoarseGainRankTrial& native,
                      const receiver_detail::CoarseGainRankTrial& other, const char* reason) {
    const auto& a = *native.coarse.midpoint_timing;
    const auto& b = *other.coarse.midpoint_timing;
    check(same_bits(native.fixed_totals, other.fixed_totals) && same_bits(native.direct_totals, other.direct_totals) &&
          a.gain_proposal_count == b.gain_proposal_count && a.gain_fixed_proposal_count == b.gain_fixed_proposal_count &&
          a.gain_proposal_lags == b.gain_proposal_lags && a.gain_fft_row_stages == b.gain_fft_row_stages &&
          a.gain_score_rows == b.gain_score_rows && a.gain_blocks == b.gain_blocks &&
          a.gain_direct_blocks == b.gain_direct_blocks && a.gain_tiled_blocks == b.gain_tiled_blocks &&
          a.gain_projected_rows == b.gain_projected_rows && a.gain_status == b.gain_status &&
          other.report.route == "native" && !other.report.reason.empty() && other.report.device_blocks == 0, reason);
    for (std::size_t i = 0; i < a.gain_proposal_count; ++i)
        check(a.gain_proposals[i].coordinate == b.gain_proposals[i].coordinate &&
              std::bit_cast<std::uint64_t>(a.gain_proposals[i].score) ==
                  std::bit_cast<std::uint64_t>(b.gain_proposals[i].score), reason);
}

// A backend present but declined before any device work: recorded reason,
// no session, the native rank bit for bit.
void same_declined_rank(const receiver_detail::CoarseGainRankTrial& native,
                        const receiver_detail::CoarseGainRankTrial& other, const MirrorGainBackend& backend,
                        std::string_view expected, const char* reason) {
    using namespace receiver_detail;
    const auto kind = expected == gain_direct_workspace ? StageNative::fallback : StageNative::decline;
    check(backend.sessions == 0 && backend.calls == 0 && other.report.reason == expected && !other.report.fallback &&
          !other.certification && other.device_totals.empty() &&
          !other.coarse.midpoint_timing->gain_device_fft_row_stages &&
          !other.coarse.midpoint_timing->gain_rescore_fft_row_stages && backend.natives.size() == 1 &&
          backend.natives[0].family == StageFamily::gain_rank && backend.natives[0].kind == kind &&
          stage_reason_suffix(backend.natives[0].reason) == expected, reason);
    same_native_rank(native, other, reason);
}

void signed_gain_device_certification() {
    using namespace receiver_detail;
    const std::array intervals{FrequencyInterval{0, 0}};
    const auto key = Crypto::amateur();
    // The 150 ppm fixtures exceed the device run limit (checked below); lift
    // it so that the mirror exercises the certification on these lags.
    GainCertificationLimits all_runs;
    all_runs.maximum_shift_runs = std::numeric_limits<std::size_t>::max();
    // Every block of the short fixture is sampled, so no unsampled direct
    // work exists; verification keeps every rank native.
    {
        const auto fixture = signed_gain_fixture(-1);
        for (const auto evaluation : {MidpointTimingEvaluation::short_tracking, MidpointTimingEvaluation::verify}) {
            const auto quote = coarse_workspace(fixture.config, fixture.slot, 2, {}, evaluation);
            const auto native = coarse_gain_rank_trial(fixture.config, fixture.slot, key, fixture.iq, intervals, 2,
                                                       quote, evaluation, nullptr, all_runs);
            check(native.report.reason.empty() && native.native_quote == quote &&
                  native.coarse.midpoint_timing->gain_sampled_blocks ==
                      native.coarse.midpoint_timing->gain_available_blocks,
                  "the short signed-gain fixture must sample every block natively");
            MirrorGainBackend unused;
            const auto declined = coarse_gain_rank_trial(fixture.config, fixture.slot, key, fixture.iq, intervals, 2,
                quote + native.device_host_bytes, evaluation, &unused, all_runs);
            same_declined_rank(native, declined, unused, evaluation == MidpointTimingEvaluation::verify ?
                gain_direct_verify_mode : gain_direct_no_unsampled,
                "a backend must decline verification and fully sampled ranks with their reasons");
        }
    }
    // Both aged fixtures publish; the first one also takes every fallback.
    for (const int aged : {0, 1}) {
        const auto fixture = signed_gain_fixture(aged);
        const auto workers = aged ? 3u : 2u;
        const auto quote = coarse_workspace(fixture.config, fixture.slot, workers, {},
                                            MidpointTimingEvaluation::short_tracking);
        const auto run = [&](ReceiverBulkBackend* backend, const GainCertificationLimits& limits, std::size_t limit) {
            return coarse_gain_rank_trial(fixture.config, fixture.slot, key, fixture.iq, intervals, workers, limit,
                                          MidpointTimingEvaluation::short_tracking, backend, limits);
        };
        const auto native = run(nullptr, all_runs, quote);
        const auto& rank = *native.coarse.midpoint_timing;
        check(rank.gain_status == "signed_gain_full_direct" && rank.gain_sampled_blocks < rank.gain_available_blocks &&
              rank.gain_tiled_blocks > 0 && native.report.route == "native" && native.report.reason.empty() &&
              !native.certification && native.native_quote == quote && native.device_host_bytes > 0 &&
              !rank.gain_device_fft_row_stages && !rank.gain_rescore_fft_row_stages &&
              native.report.shift_vectors > 0 && native.report.shift_vectors <= rank.gain_available_blocks,
              "the aged signed-gain rank must run natively with shift runs and no backend");
        // The session admits its native quote; the device host buffers need the rest.
        const auto admitted = quote + native.device_host_bytes;
        const auto trial = [&](ReceiverBulkBackend* backend, const GainCertificationLimits& limits) {
            return run(backend, limits, admitted);
        };

        const auto values = std::make_shared<MirrorGainValues>();
        MirrorGainBackend mirror;
        mirror.cache = values;
        const auto device = trial(&mirror, all_runs);
        check(device.certification.has_value(), "a mirror device must reach certification");
        const auto& certified = *device.coarse.midpoint_timing;
        const auto& certification = *device.certification;
        const auto& report = device.report;
        const auto unsampled = mirror.blocks - mirror.audits;
        // The FP64 rescoring takes one forward 2B-point transform per
        // unsampled block and rescored lag, counted apart from native work.
        const auto period = 2 * mirror.shape.block_rows;
        const auto rescore_stages = [&](std::size_t rescored) {
            return static_cast<std::uint64_t>(rescored) * unsampled * period *
                static_cast<std::uint64_t>(std::countr_zero(period));
        };
        check(mirror.sessions == 1 && mirror.calls == 1 && certification.fallback.empty() && report.route == "device" &&
              report.reason.empty() && !report.fallback && mirror.natives.empty() &&
              mirror.releases == 1 && mirror.released_at_certification &&
              report.band_ratio > 0 && report.band_ratio <= 1 &&
              std::has_single_bit(mirror.shape.tile_fft_rows) && mirror.shape.tile_fft_rows < rank.gain_fft_rows &&
              mirror.shape.tile_fft_rows >= 2 * mirror.shape.block_rows && mirror.shape.lags == rank.lag_count &&
              mirror.shape.shift_runs == std::min(all_runs.maximum_shift_runs, rank.gain_available_blocks) &&
              report.shift_runs <= mirror.shape.shift_runs && mirror.shape.observation_rows == mirror.observation_rows &&
              mirror.shape.audit_blocks == rank.gain_sampled_blocks && mirror.audits == rank.gain_blocks &&
              mirror.max_outputs <= mirror.shape.maximum_outputs && report.device_blocks == unsampled &&
              certified.gain_direct_blocks == rank.gain_direct_blocks && certified.gain_score_rows == rank.gain_score_rows &&
              certified.gain_score_rows <= certified.gain_score_row_bound &&
              certified.gain_fft_row_stages <= certified.gain_fft_stage_bound &&
              certified.gain_fft_row_stages < rank.gain_fft_row_stages && certified.gain_blocks == rank.gain_blocks &&
              report.rescored_lags == certification.rescored_lags &&
              report.frontier_lags == certification.frontier_lags && report.rescore_seconds > 0 &&
              certification.rescored_lags > 0 && certified.gain_rescored_lags == certification.rescored_lags &&
              certified.gain_rescore_fft_row_stages == rescore_stages(certification.rescored_lags) &&
              same_bits(native.fixed_totals, device.fixed_totals),
              "a clean mirror must publish with full work credit and the native fixed rank");
        check(report.device_host_bytes == native.device_host_bytes && certification.host_bytes > 0 &&
              report.device_host_held > certification.host_bytes && report.device_host_held <= report.device_host_bytes,
              "every device host buffer must be held within its admitted quote");
        check(certified.gain_proposal_count == rank.gain_proposal_count &&
              certified.gain_fixed_proposal_count == rank.gain_fixed_proposal_count &&
              certified.gain_proposal_lags == rank.gain_proposal_lags,
              "certified device nominations must equal the native lags and order");
        for (std::size_t i = 0; i < rank.gain_proposal_count; ++i)
            check(certified.gain_proposals[i].coordinate == rank.gain_proposals[i].coordinate &&
                  std::abs(certified.gain_proposals[i].score - rank.gain_proposals[i].score) <=
                      1e-9 * (1 + std::abs(rank.gain_proposals[i].score)),
                  "certified nominations must carry FP64 scores at roundoff from native");
        // The FP64 magnitude-identity rescoring reproduces the native totals,
        // and the audit captures exactly the sampled blocks' direct values.
        const auto cells = 2 * rank.lag_count;
        for (std::size_t i = 0; i < cells; ++i) {
            const auto value = certification.nomination[i];
            check(std::isinf(value) || std::abs(value - native.direct_totals[i]) <=
                      1e-10 * (1 + std::abs(native.direct_totals[i])),
                  "FP64 frontier rescoring must agree with the native direct totals");
        }
        double audit_error = 0;
        for (std::size_t i = 0; i < device.audit_native.size(); ++i) {
            check(std::isfinite(device.audit_native[i]), "every audit block must capture its native direct values");
            audit_error = std::max(audit_error, std::abs(device.audit_device[i] - device.audit_native[i]) /
                                                    (1 + std::abs(device.audit_native[i])));
        }
        for (std::size_t i = 0; i < cells; ++i) {
            double sum = 0, magnitude = 0;
            for (std::size_t a = 0; a < mirror.audits; ++a) {
                sum += device.audit_native[a * cells + i];
                magnitude += std::abs(device.audit_native[a * cells + i]);
            }
            check(std::abs(sum - device.direct_totals[i]) <= 1e-12 * (1 + magnitude),
                  "captured audit values must sum to the fixed lanes' direct totals");
        }
        check(audit_error < 1e-5 && certification.bits[0].band > 0 && certification.bits[1].band > 0,
              "the FP32 mirror must agree with its native audit to FP32 roundoff");
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto totals = std::span(certification.totals).subspan(bit * rank.lag_count, rank.lag_count);
            check(std::isfinite(certification.bits[bit].excluded) &&
                  certification.bits[bit].excluded < std::ranges::max(totals) - 2 * certification.bits[bit].band,
                  "lags outside the frontier must stay below its boundary");
        }
        if (aged) continue;

        // Perturbed per-block values widen the band and the frontier; the
        // published lags stay native. The predeclared noise puts 2 band
        // midway between the native distances from the best total to the
        // 9th and the 64th lag of either bit. For uniform noise on A audit
        // blocks the band is about N (8 + 3.5) 0.29 noise / sqrt(A): k
        // standard errors and the largest lag's sampled mean error, about
        // 3.5 standard errors over these lags.
        double ninth = std::numeric_limits<double>::infinity(), last = ninth;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            std::vector<double> totals(native.direct_totals.begin() + static_cast<std::ptrdiff_t>(bit * rank.lag_count),
                                       native.direct_totals.begin() + static_cast<std::ptrdiff_t>((bit + 1) * rank.lag_count));
            std::ranges::sort(totals, std::greater<>{});
            ninth = std::min(ninth, totals[0] - totals[8]);
            last = std::min(last, totals[0] - totals[63]);
        }
        check(1.5 * ninth < last, "the frontier growth fixture must separate its 9th and 64th native lags");
        MirrorGainBackend noisy;
        noisy.cache = values;
        noisy.noise = std::sqrt(ninth * last) * std::sqrt(12. * static_cast<double>(mirror.audits)) /
                      (2 * (8 + 3.5) * static_cast<double>(unsampled));
        const auto widened = trial(&noisy, all_runs);
        check(widened.certification && widened.certification->fallback.empty() && widened.report.route == "device" &&
              widened.certification->frontier_lags > certification.frontier_lags &&
              widened.coarse.midpoint_timing->gain_proposal_lags == rank.gain_proposal_lags,
              "perturbed device values must grow the published frontier and keep native lags");
        // Every fallback after a device call reruns the native unsampled loop:
        // native bit for bit, with the certification's reason.
        // The session's planned call is already counted, so the receiver
        // records a session fallback.
        const auto fell_back = [&](const MirrorGainBackend& backend, const CoarseGainRankTrial& other,
                                   std::string_view expected, const char* reason) {
            check(other.certification && other.certification->fallback == expected &&
                  other.report.reason == expected && other.report.fallback && backend.natives.size() == 1 &&
                  backend.natives[0].kind == StageNative::session_fallback &&
                  stage_reason_suffix(backend.natives[0].reason) == expected &&
                  backend.releases == 1 && backend.released_at_certification &&
                  !other.coarse.midpoint_timing->gain_rescored_lags && other.coarse.midpoint_timing->gain_top2_gap == 0 &&
                  // Rescoring runs before the band and tie checks, not after an overflow.
                  (expected == gain_certification_overflow) == !other.certification->rescored_lags &&
                  other.coarse.midpoint_timing->gain_rescore_fft_row_stages ==
                      rescore_stages(other.certification->rescored_lags),
                  reason);
            same_native_rank(native, other, reason);
        };
        MirrorGainBackend spiked;
        spiked.cache = values;
        spiked.spike_lag = certification.bits[0].best_lag;
        spiked.spike = 1e3 * (1 + certification.bits[0].band);
        fell_back(spiked, trial(&spiked, all_runs), gain_certification_band,
                  "an unaudited device error must fall back as band-violated");
        MirrorGainBackend scattered;
        scattered.cache = values;
        scattered.audit_noise = 10 * std::max(certification.bits[0].audit.magnitude, certification.bits[1].audit.magnitude);
        fell_back(scattered, trial(&scattered, all_runs), gain_certification_overflow,
                  "an exploded audit band must fall back as frontier-overflow");
        auto tie_limits = all_runs;
        tie_limits.near_tie = 1e6;
        MirrorGainBackend tied;
        tied.cache = values;
        fell_back(tied, trial(&tied, tie_limits), gain_certification_tie,
                  "a forced near-tie threshold must fall back as near-tie");
        // The backend's own decline (no session), its factory's fallback (a
        // refused lease, no session) and a session fallback (a refused call)
        // carry its reason; one without a reason is a contract error.
        MirrorGainBackend refusing;
        refusing.session_decline = "mirror-gain-rank-unqualified-device";
        const auto refused = trial(&refusing, all_runs);
        check(refusing.sessions == 1 && refusing.calls == 0 && !refused.certification && !refused.report.fallback &&
              refused.report.reason == refusing.session_decline && refusing.natives.empty() &&
              !refused.coarse.midpoint_timing->gain_rescore_fft_row_stages,
              "a backend decline must carry the backend's reason");
        same_native_rank(native, refused, "a backend decline must reproduce the native rank");
        // An earlier call's decline reason stays in the receipt; the counter
        // that grew names this one.
        MirrorGainBackend unadmitting;
        unadmitting.session_fallback = "mirror-gain-rank-workspace";
        unadmitting.work.vulkan_gain_rank.planned_calls = unadmitting.work.vulkan_gain_rank.declined_calls = 1;
        unadmitting.work.vulkan_gain_rank.decline_reason = refusing.session_decline;
        const auto unadmitted_session = trial(&unadmitting, all_runs);
        const auto& refused_lease = unadmitting.work.vulkan_gain_rank;
        check(unadmitting.sessions == 1 && unadmitting.calls == 0 && !unadmitted_session.certification &&
              !unadmitted_session.report.fallback && unadmitted_session.report.reason == unadmitting.session_fallback &&
              unadmitting.natives.empty() && refused_lease.planned_calls == 2 && refused_lease.declined_calls == 1 &&
              refused_lease.native_fallbacks == 1 && !unadmitted_session.coarse.midpoint_timing->gain_rescored_lags,
              "a factory fallback must carry the backend's fallback reason and run natively");
        same_native_rank(native, unadmitted_session, "a factory fallback must reproduce the native rank");
        MirrorGainBackend declining;
        declining.call_decline = "mirror-gain-rank-nonfinite-input";
        const auto declined = trial(&declining, all_runs);
        check(declining.sessions == 1 && declining.calls == 1 && !declined.certification && declined.report.fallback &&
              declined.report.reason == declining.call_decline && declining.natives.empty() && declining.releases == 1 &&
              !declined.coarse.midpoint_timing->gain_device_fft_row_stages &&
              !declined.coarse.midpoint_timing->gain_rescore_fft_row_stages,
              "a backend fallback must carry the backend's reason");
        same_native_rank(native, declined, "a backend fallback must reproduce the native rank");
        MirrorGainBackend silent;
        silent.silent = true;
        rejects<Error>([&] { (void)trial(&silent, all_runs); }, "a backend decline without a reason must be rejected");
        // Declines before any device work: the host quote must fit its
        // admission, and the default run limit holds the fixture's runs.
        MirrorGainBackend unadmitted;
        same_declined_rank(native, run(&unadmitted, all_runs, admitted - 1), unadmitted, gain_direct_workspace,
                           "device host buffers one byte beyond their admission must decline as workspace");
        MirrorGainBackend unused;
        const auto runs = trial(&unused, {});
        same_declined_rank(native, runs, unused, gain_direct_shift_runs,
                           "more shift runs than the device limit must decline before device work");
        check(runs.report.shift_runs == GainCertificationLimits{}.maximum_shift_runs + 1 &&
              report.shift_runs > GainCertificationLimits{}.maximum_shift_runs,
              "a shift-run decline must report the overflowing run count");
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
            else if (part == "affine-continuation") affine_continuation();
            else if (part == "protected-discovery") protected_discovery_near_far();
            else if (part == "protected-carrier-beat") {
                protected_carrier_beat_preserves_wanted();
                protected_colored_carrier_beat();
            }
            else if (part == "protected-observation") {
                protected_observation_validity();
                protected_drift_lattice();
                scheduled_fill_preserves_midpoint_search();
                scheduled_fill_preserves_large_tracking_tail();
            }
            else if (part == "short-drift") {
                drift_ridge_interchange_oracle();
                drift_device_mirror();
                ordinary_drift_width_keeps_owner_lease();
                bounded_short_competition();
                bounded_drift_recovery();
            }
            else if (part == "long-drift") long_drift_recovery(false);
            else if (part == "broad-drift") long_drift_recovery(true);
            else if (part == "fading") weak_diffuse_fading_recovery();
            else if (part == "late-helper") late_helper_timing_recovery(false);
            else if (part == "late-helper-delay") late_helper_timing_recovery(true);
            else if (part == "signed-gain-timing") signed_gain_timing_and_exact_admission();
            else if (part == "signed-gain-certification") {
                signed_gain_shift_runs();
                signed_gain_certification_contract();
                signed_gain_device_certification();
            }
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
        affine_continuation();
        protected_observation_validity();
        protected_discovery_near_far();
        protected_carrier_beat_preserves_wanted();
        protected_colored_carrier_beat();
        protected_drift_lattice();
        scheduled_fill_preserves_midpoint_search();
        scheduled_fill_preserves_large_tracking_tail();
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
