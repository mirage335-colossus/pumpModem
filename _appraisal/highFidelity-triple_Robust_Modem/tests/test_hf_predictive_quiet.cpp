#include "../src/hf_predictive_quiet.hpp"

#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "triad_random_process.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_fine.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {

using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using appraisal::HfPredictiveQuiet;

constexpr double fs = 1920;
constexpr double noise = .25;
constexpr double frequency = 39.012345;
constexpr double line_amplitude = .6;
constexpr double turn = 2 * std::numbers::pi;
constexpr std::size_t target_slot = 3;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void score_serial_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                        std::span<const Complex> iq, ReceiverSlotScore& score) {
    ReceiverExecutor executor(1);
    score_varying_quiet(config, slot, iq, score, {}, 1, executor);
}

PublicProfileConfig public_config() {
    PublicProfileConfig config;
    config.format.sample_rate = fs;
    config.format.inner_rate_hz = 120;
    config.format.added_symbols = 1;
    config.format.added_interleave = false;
    config.format.bit_seconds = 8;
    config.format.epoch = 1793850000;
    config.format.symbol_ordinal = 741;
    config.data_if_hz = 500;
    config.cfo_bound_hz = 3.25;
    config.lead_samples = 192;
    config.receiver_filter_halo_samples = 32;
    return config;
}

ReceiverConfig receiver_config() {
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = 500;
    config.noise_variance = noise;
    config.frequency_bound_hz = .25;
    config.time_varying_channel = true;
    config.time_varying_atmospheric_hz = 3;
    config.time_varying_common_doppler_hz = 3;
    return config;
}

Complex tone(std::uint64_t row, double hz = frequency, double amplitude = line_amplitude, double phase = .31) {
    return std::polar(amplitude, std::remainder(turn * hz * (row / fs) + phase, turn));
}

struct Input {
    const PublicProfile profile{public_config()};
    const ReceiverConfig config{receiver_config()};
    const ReceiverSlotPlan slot;
    std::vector<Complex> iq;
    std::vector<std::uint8_t> validity;

    Input(bool coherent_line = true, std::size_t selected_slot = target_slot)
        : slot(make_receiver_slot_plan(profile, config, selected_slot)), iq(slot.stop_sample), validity(iq.size(), 1) {
        using datapump::triad::v3::detail::random_process_uniform;
        for (std::size_t row = 0; row < iq.size(); ++row) {
            const auto amplitude = std::sqrt(-noise * std::log(random_process_uniform(63073, 2 * row)));
            const auto angle = turn * random_process_uniform(63073, 2 * row + 1);
            iq[row] = std::polar(amplitude, angle);
            if (coherent_line) iq[row] += tone(row);
        }
    }

    SampleRange guard(std::size_t index) const {
        return appraisal::predictive_quiet_detail::public_training_gap(profile.slot(index - 1), profile.slot(index), fs);
    }

    HfPredictiveQuiet trained(std::size_t chunk = 1493) const {
        HfPredictiveQuiet predictor(profile, config, slot.index + 1);
        for (std::size_t first = 0; first < slot.first_sample;) {
            const auto count = std::min(chunk, static_cast<std::size_t>(slot.first_sample - first));
            predictor.observe(first, std::span(iq).subspan(first, count),
                              std::span(validity).subspan(first, count), {});
            first += count;
        }
        return predictor;
    }

    std::vector<Complex> observation() const {
        const auto rows = std::span(iq).subspan(slot.first_sample, slot.stop_sample - slot.first_sample);
        return {rows.begin(), rows.end()};
    }

    ReceiverSlotScore ambiguous_score(std::span<const Complex> observation) const {
        auto legacy = config;
        legacy.time_varying_atmospheric_hz = 0;
        ReceiverSlotScore result;
        score_serial_quiet(legacy, slot, observation, result);
        result.quiet_modes = result.varying_quiet_modes;
        result.quiet_energy = result.varying_quiet_energy;
        result.quiet_threshold = result.varying_quiet_threshold;
        result.varying_quiet_modes = 0;
        result.index = slot.index;
        result.first_sample = slot.first_sample;
        result.stop_sample = slot.stop_sample;
        result.quiet_first_sample = slot.quiet_first_sample;
        result.quiet_stop_sample = slot.quiet_stop_sample;
        result.sample_rate_hz = fs;
        result.consumed_samples = observation.size();
        result.complete = result.all_valid = result.zero_h0_verified = result.noise_statistics_valid = true;
        result.operational_threshold = receiver_operational_threshold();
        result.operational_log_e = -6;
        result.reason = "unresolved_bit_or_null";
        return result;
    }

    auto evaluate(HfPredictiveQuiet& predictor, std::span<const Complex> observation) const {
        auto score = ambiguous_score(observation);
        predictor.consider(config, slot, observation, score, {});
        return score;
    }

    template<class Modifier>
    void alter(std::vector<Complex>& observation, Modifier update) const {
        for (auto i = slot.quiet_first_sample; i < slot.quiet_stop_sample; ++i)
            observation[i - slot.first_sample] += update(i);
    }

    double in_band_tone() const {
        return -.25 * geometry(slot.format).occupied_bandwidth_hz;
    }
};

void stationary_with_independent_noise() {
    const Input input;
    auto a = input.trained();
    auto b = input.trained(16'381);
    const auto observed = input.observation();
    const auto before = input.ambiguous_score(observed);
    const auto accepted = input.evaluate(a, observed);
    const auto differently_chunked = input.evaluate(b, observed);
    const auto& report = a.result(target_slot);
    const auto& other = b.result(target_slot);
    check(before.quiet_energy > before.quiet_threshold, "fixture must exceed the uncorrected quiet threshold");
    check(report.training_status == "validated_prior_stationary_line", "stationary line did not validate independently");
    check(report.attempted && report.accepted && accepted.decision == SlotDecision::quiet,
          "stationary line plus white noise should qualify in both residual bands");
    check(QuietSlotMessageReceiver::classification(accepted) == SlotDecision::quiet,
          "predictive score does not satisfy the unchanged physical commit validator");
    check(report.frequency_first.stop <= report.frequency_second.first &&
          report.frequency_second.stop <= report.amplitude.first && report.amplitude.stop <= report.validation.first &&
          report.validation.stop <= input.slot.first_sample,
          "candidate read current or overlapping data for nuisance training");
    check(report.frequency_first_gap_slot == 1 && report.frequency_second_gap_slot == 2 &&
          report.frequency_baseline == "adjacent_gaps" && report.frequency_middle_gap_slot == 0 &&
          report.frequency_witness_count == 2 && report.frequency_older_pair_intervals == 0 &&
          report.frequency_adjacent_pair_intervals == 1 && report.frequency_joint_intervals == 1,
          "the early slot must retain its original adjacent public frequency pair");
    check(report.frequency_first.first == report.frequency_first_proposal.first &&
          report.frequency_first_proposal.stop == report.frequency_first_confirmation.first &&
          report.frequency_first_confirmation.stop == report.frequency_first.stop &&
          report.frequency_second.first == report.frequency_second_proposal.first &&
          report.frequency_second_proposal.stop == report.frequency_second_confirmation.first &&
          report.frequency_second_confirmation.stop == report.frequency_second.stop,
          "frequency proposals were not separated from their confirmation data");
    check(std::abs(report.frequency_hz - frequency) < .001 && report.line_direction_gain_squared > .1 &&
          report.line_direction_gain_squared < 1 && report.validation_line_residual < report.coherent_threshold,
          "predictive line fit or nonzero wanted direction gain is inconsistent");
    check(report.frequency_lower_hz <= frequency && frequency <= report.frequency_upper_hz &&
          report.frequency_confidence_exclusion_probability == 1e-6 &&
          report.frequency_coherent_nodes == 9 && report.frequency_band_nodes == 9 &&
          report.band_interpolation_norm > 0 && report.coherent_interpolation_norm > 0,
          "the certified tube does not cover the fixed true tone, or omitted continuous guards");
    check(report.frequency_hz == other.frequency_hz && report.coherent_residual == other.coherent_residual &&
          report.legacy_energy == other.legacy_energy && report.expanded_energy == other.expanded_energy &&
          accepted.quiet_energy == differently_chunked.quiet_energy,
          "training or scoring depends on capture chunk boundaries");
    check(a.training_workspace_bytes() < 16u * 1024u * 1024u,
          "bounded training fixture exceeds its workspace ceiling");
}

void frequency_geometry() {
    namespace math = appraisal::predictive_quiet_detail;
    constexpr double delta = .00325;
    constexpr long double separation = 89.75L;
    const auto phase = std::remainder(static_cast<double>(frequency * separation), 1.);
    const auto edge = math::phase_intervals({frequency, frequency + delta}, phase, 1e-8, separation);
    check(edge && edge->size() == 1 && edge->front().lower <= frequency && frequency <= edge->front().upper,
          "wrapped phase interval loses a known frequency at a boundary");
    const auto aliases = math::phase_intervals({frequency - .025, frequency + .025}, phase, .001, separation);
    check(aliases && aliases->size() > 1,
          "phase inversion must expose multiple possible aliases before intersection");
    check(!math::phase_intervals({-100, 100}, phase, .001, separation),
          "overwide alias bank must refuse work rather than truncate to a false unique answer");

    const auto witness = [&](double time, double width) {
        const auto first = static_cast<std::uint64_t>(time * fs);
        const auto value = std::polar(line_amplitude, turn * std::remainder(frequency * time, 1.));
        return math::FrequencyWitness{{{first, first + 100}, frequency, value}, .001,
            {{{frequency - width, frequency + width}, {}}}, 1};
    };
    const auto narrow_recent = witness(270, .0045);
    const auto short_pair = math::frequency_pair_intervals(witness(180, .0045), narrow_recent, fs);
    const auto long_pair = math::frequency_pair_intervals(witness(0, .0045), narrow_recent, fs);
    check(short_pair && long_pair && short_pair->size() == 1 && long_pair->size() > 1 &&
          !math::unique_frequency_interval(*long_pair),
          "the designed case did not reproduce the ambiguity from using the long pair alone");
    const auto joint = math::intersect_frequency_pairs(*long_pair, *short_pair);
    const auto tube = math::unique_frequency_interval(joint);
    check(tube && tube->lower <= frequency && frequency <= tube->upper &&
          tube->upper - tube->lower < short_pair->front().upper - short_pair->front().lower,
          "three fixed witnesses did not retain a unique and narrower tube around the true frequency");

    const auto wide_recent = witness(270, .0125);
    const auto wide_short = math::frequency_pair_intervals(witness(180, .0125), wide_recent, fs);
    const auto wide_long = math::frequency_pair_intervals(witness(0, .0125), wide_recent, fs);
    check(wide_long && wide_short, "unresolved alias control could not enumerate its bounded pairs");
    const auto unresolved = math::intersect_frequency_pairs(*wide_long, *wide_short);
    check(unresolved.size() > 1 && !math::unique_frequency_interval(unresolved),
          "multiple remaining joint aliases became falsely unique");
    const std::array touching{math::FrequencyInterval{1., 2.}, math::FrequencyInterval{2., 3.}};
    const std::array spanning{math::FrequencyInterval{1.5, 2.5}};
    const auto merged = math::intersect_frequency_pairs(touching, spanning);
    check(merged.size() == 1 && merged[0].lower == 1.5 && merged[0].upper == 2.5 &&
          !math::unique_frequency_interval(merged), "touching pieces must merge without bypassing the existing width cap");
    check(!math::first_lobe_radius(fs, 10'000, .5),
          "half-magnitude information cannot certify a strictly positive first-lobe response");
    const auto lobe = math::first_lobe_radius(fs, 10'000, .75);
    check(lobe && *lobe > 0 && *lobe < fs / 10'000 &&
          std::sin(std::numbers::pi * *lobe * 10'000 / fs) > 0,
          "certified first lobe reaches a Dirichlet sign change");

    // For fixed finite M, the aligned signal's effective information is
    // proportional to N*g^2. Doubling N cannot double it when both
    // windows substantially exceed that same independent training span.
    constexpr std::size_t training = 25'100, short_rows = 134'360, long_rows = 2 * short_rows;
    const auto short_gain = math::line_direction_gain_squared(training, short_rows);
    const auto long_gain = math::line_direction_gain_squared(training, long_rows);
    const auto long_information = long_rows * long_gain;
    check(long_gain > 0 && long_gain < short_gain && long_information > short_rows * short_gain &&
          long_information < training && .9 * training < long_information,
          "independent finite-M amplitude uncertainty must retain and saturate aligned information");
}

void check_continuous_residual_envelope() {
    const Input input;
    auto trained = input.trained();
    const auto observed = input.observation();
    check(input.evaluate(trained, observed).decision == SlotDecision::quiet,
          "continuous-envelope reference requires stable candidate promotion in its synthetic fixture");
    const auto& report = trained.result(target_slot);
    const auto whole_aperture = input.slot.quiet_stop_sample - input.slot.quiet_first_sample;
    const auto offset = input.slot.quiet_first_sample - input.slot.first_sample;
    const auto sample = [](double hz, std::uint64_t at) {
        return std::polar(1., std::remainder(turn * hz * (at / fs), turn));
    };
    auto legacy_cfg = input.config;
    legacy_cfg.time_varying_atmospheric_hz = 0;
    const auto g = std::sqrt(report.line_direction_gain_squared);
    // Independent direct phasor evaluation deliberately samples the interior
    // of every interval between the nine candidate nodes.
    for (std::size_t i = 0; i < 16; ++i) {
        const auto hz = std::lerp(report.frequency_lower_hz, report.frequency_upper_hz, (i + .5) / 16.);
        Complex prior{}, now{};
        for (auto row = report.amplitude.first; row < report.amplitude.stop; ++row)
            prior += std::conj(sample(hz, row)) * input.iq[row];
        prior /= static_cast<double>(report.amplitude.size());
        for (auto row = input.slot.quiet_first_sample; row < input.slot.quiet_stop_sample; ++row)
            now += std::conj(sample(hz, row)) * observed[row - input.slot.first_sample];
        now /= static_cast<double>(whole_aperture);
        auto corrected = observed;
        for (std::uint64_t row = 0; row < whole_aperture; ++row)
            corrected[offset + row] -= sample(hz, input.slot.quiet_first_sample + row) * (prior + (1 - g) * (now - prior));
        ReceiverSlotScore legacy, expanded;
        score_serial_quiet(legacy_cfg, input.slot, corrected, legacy);
        score_serial_quiet(input.config, input.slot, corrected, expanded);
        const auto coherent = std::norm(now - prior) * whole_aperture * g * g / noise;
        check(coherent <= report.coherent_residual && legacy.varying_quiet_energy <= report.legacy_energy &&
              expanded.varying_quiet_energy <= report.expanded_energy,
              "direct off-grid residual exceeds the certified complex coherence or band-energy envelope");
    }
}

void aligned_wanted_and_mismatched_current_line() {
    const Input input;
    const auto trained = input.trained();
    auto baseline_predictor = trained;
    check(input.evaluate(baseline_predictor, input.observation()).decision == SlotDecision::quiet,
          "aligned-signal control requires a passing stationary background");
    double previous = baseline_predictor.result(target_slot).coherent_residual;
    for (double amplitude : {.06, .12, .24}) {
        auto prediction = trained;
        auto data = input.observation();
        input.alter(data, [=](auto row) { return tone(row, frequency, amplitude); });
        const auto score = input.evaluate(prediction, data);
        const auto& evidence = prediction.result(target_slot);
        check(evidence.attempted && !evidence.accepted && score.decision == SlotDecision::ambiguous &&
              evidence.coherent_residual > previous && evidence.coherent_residual > evidence.coherent_threshold,
              "increasing wanted energy aligned with the known tone must defeat a quiet decision");
        previous = evidence.coherent_residual;
    }
    for (unsigned test = 0; test < 4; ++test) {
        auto prediction = trained;
        auto data = input.observation();
        input.alter(data, [&](auto row) {
            switch (test) {
            case 0: return tone(row, frequency, line_amplitude, .31 + .4) - tone(row);
            case 1: return tone(row, frequency + .021, line_amplitude) - tone(row);
            case 2: return tone(row, input.in_band_tone(), .42);
            default:
                return row < input.slot.quiet_first_sample + 900 ? tone(row, input.in_band_tone(), 3.) : Complex{};
            }
        });
        if (test >= 2) {
            const auto stationary = input.observation();
            std::vector<Complex> contaminant(data.size());
            std::ranges::transform(data, stationary, contaminant.begin(), std::minus<Complex>{});
            const auto isolated = input.ambiguous_score(contaminant);
            check(isolated.quiet_energy > isolated.quiet_threshold,
                  "the isolated second tone or burst must be visible in the native DATA band");
        }
        const auto score = input.evaluate(prediction, data);
        const auto& evidence = prediction.result(target_slot);
        if (!evidence.attempted || evidence.accepted || score.decision != SlotDecision::ambiguous)
            std::cerr << "Mismatched current line case " << test << ", " << evidence.training_status << '/' <<
                evidence.status << ", native energy " << evidence.raw_quiet_energy << '/' << evidence.raw_quiet_threshold <<
                ", coherent " << evidence.coherent_residual << '/' << evidence.coherent_threshold <<
                ", residual legacy " << evidence.legacy_energy << '/' << evidence.legacy_threshold <<
                ", expanded " << evidence.expanded_energy << '/' << evidence.expanded_threshold << '\n';
        check(evidence.attempted && !evidence.accepted && score.decision == SlotDecision::ambiguous,
              "phase/frequency step, second tone, or scored-slot burst became quiet");
        if (test >= 2)
            check(evidence.status == "unexplained_energy_in_public_quiet_bands",
                  "orthogonal interference should reach and fail the expanded-band energy check");
    }
    // The original adverse fixture had 75 Hz nominal bandwidth. The 64.34567
    // Hz line is outside both public bands and may coexist with physical quiet.
    auto prediction = trained;
    auto data = input.observation();
    input.alter(data, [](auto row) { return tone(row, 64.34567, .42); });
    check(input.evaluate(prediction, data).decision == SlotDecision::quiet,
          "protected out-of-band interference should not prevent quiet in the public DATA bands");
}

void retained_native_line() {
    const Input input;
    const auto trained = input.trained();
    for (bool second_line : {false, true}) {
        auto prediction = trained;
        auto observed = input.observation();
        if (second_line)
            input.alter(observed, [&](auto row) { return tone(row, input.in_band_tone(), .42); });
        auto score = input.ambiguous_score(observed);
        const auto retained = find_varying_line(input.config, input.slot, observed, {});
        check(retained && retained->log_weighted_evidence > 0 && std::abs(retained->frequency_hz - frequency) < .02,
              "retained-line fixture needs the native line proposal to select the continuing background tone");
        score.varying_line_frequency_hz = retained->frequency_hz;
        score.varying_line_log_evidence = retained->log_weighted_evidence;
        score.reason = "coherent_line_in_public_data_aperture";
        score_serial_quiet(input.config, input.slot, observed, score);
        check(QuietSlotMessageReceiver::classification(score) == SlotDecision::ambiguous,
              "retained-line fixture must remain ambiguous before independent quiet scoring");
        prediction.consider(input.config, input.slot, observed, score, {});
        const auto& evidence = prediction.result(target_slot);
        check(evidence.attempted && evidence.raw_line_frequency_hz == retained->frequency_hz &&
              evidence.raw_line_log_evidence == retained->log_weighted_evidence &&
              score.varying_line_frequency_hz == retained->frequency_hz &&
              score.varying_line_log_evidence == retained->log_weighted_evidence,
              "retained current-slot line metadata hid or changed the independently predicted quiet check");
        if (second_line) {
            check(!evidence.accepted && score.decision == SlotDecision::ambiguous &&
                  evidence.status == "unexplained_energy_in_public_quiet_bands",
                  "the second in-band tone escaped the quiet band test when the strongest tone was retained");
        } else {
            check(evidence.accepted && QuietSlotMessageReceiver::classification(score) == SlotDecision::quiet,
                  "a retained native line prevented otherwise valid independently scored physical quiet");
        }
    }
}

void separated_public_baseline() {
    constexpr std::size_t index = 5;
    const Input input(true, index);
    auto trained = input.trained();
    auto stationary = trained;
    const auto original = input.observation();
    const auto score = input.evaluate(stationary, original);
    const auto& good = stationary.result(index);
    const auto first_gap = input.guard(index - 4);
    const auto middle_gap = input.guard(index - 2);
    const auto recent_gap = input.guard(index - 1);
    check(good.frequency_baseline == "separated_gaps" && good.frequency_first_gap_slot == index - 4 &&
          good.frequency_middle_gap_slot == index - 2 && good.frequency_second_gap_slot == index - 1 &&
          good.frequency_witness_count == 3 && good.frequency_interval_rule == "fixed_three_witness_intersection_v1" &&
          good.frequency_first.first == first_gap.first && good.frequency_first.stop == first_gap.stop &&
          good.frequency_middle.first == middle_gap.first && good.frequency_middle.stop == middle_gap.stop &&
          good.frequency_middle_proposal.first == middle_gap.first &&
          good.frequency_middle_proposal.stop == good.frequency_middle_confirmation.first &&
          good.frequency_middle_confirmation.stop == middle_gap.stop &&
          good.frequency_second.first == recent_gap.first && good.frequency_second.stop == recent_gap.stop,
          "long frequency pair did not retain its fixed public gap addresses");
    check(good.training_status == "validated_prior_stationary_line" && good.accepted &&
          QuietSlotMessageReceiver::classification(score) == SlotDecision::quiet &&
          good.frequency_older_pair_intervals >= 1 && good.frequency_adjacent_pair_intervals >= 1 &&
          good.frequency_joint_intervals == 1 && good.frequency_confidence_exclusion_probability == 1e-6 &&
          good.frequency_lower_hz <= frequency && frequency <= good.frequency_upper_hz,
          "stationary three-witness intersection did not predict physical quiet at its unchanged gates");

    double previous = good.coherent_residual;
    for (double amplitude : {.06, .12, .24}) {
        auto prediction = trained;
        auto observation = original;
        input.alter(observation, [amplitude](auto row) { return tone(row, frequency, amplitude); });
        const auto result = input.evaluate(prediction, observation);
        const auto& evidence = prediction.result(index);
        check(evidence.attempted && !evidence.accepted && result.decision == SlotDecision::ambiguous &&
              evidence.coherent_residual > evidence.coherent_threshold && evidence.coherent_residual > previous,
              "separated frequency fit obscured a current-slot signal aligned with the continuing tone");
        previous = evidence.coherent_residual;
    }

    for (unsigned case_index = 0; case_index < 3; ++case_index) {
        auto changed = input;
        const auto gap = changed.guard(index - 4);
        if (case_index == 0) {
            changed.validity[gap.first + 11] = 0;
        } else if (case_index == 1) {
            // Reverse phase only in the second part of confirmation. The
            // earlier proposal data and the other public gaps stay fixed.
            for (auto row = gap.first + gap.size() / 4 + (gap.size() - gap.size() / 4) / 2; row < gap.stop; ++row)
                changed.iq[row] -= 2. * tone(row);
        } else {
            for (auto row = gap.first; row < gap.stop; ++row) changed.iq[row] += tone(row, frequency + .7) - tone(row);
        }
        auto prediction = changed.trained();
        const auto result = changed.evaluate(prediction, original);
        const auto& evidence = prediction.result(index);
        check(evidence.frequency_baseline == "separated_gaps" && evidence.frequency_first_gap_slot == index - 4 &&
              !evidence.attempted && !evidence.accepted && result.decision == SlotDecision::ambiguous,
              "invalid or nonstationary selected old gap was replaced by an unplanned short baseline");
    }

    auto missing_adjacent = input;
    missing_adjacent.validity[missing_adjacent.guard(index - 2).first + 11] = 0;
    auto prediction = missing_adjacent.trained();
    const auto result = missing_adjacent.evaluate(prediction, original);
    const auto& evidence = prediction.result(index);
    check(result.decision == SlotDecision::ambiguous && evidence.frequency_middle_gap_slot == index - 2 &&
          evidence.frequency_witness_count == 3 && !evidence.attempted && !evidence.accepted &&
          evidence.training_status == "no_clean_coherent_frequency_gaps",
          "an unavailable selected middle witness must leave the canonical ambiguity");
}

void invalid_or_contaminated_training() {
    for (unsigned test = 0; test < 5; ++test) {
        Input input(test != 4);
        if (test == 0) {
            input.validity[input.guard(1).first + 11] = 0;
        } else if (test == 1) {
            const auto held = input.guard(3);
            for (auto row = held.stop - held.size() / 3; row < held.stop; ++row)
                input.iq[row] += tone(row, frequency, line_amplitude) * Complex{-.25, .45};
        } else if (test == 2) {
            const auto gap = input.guard(3);
            for (auto row = gap.first; row < gap.stop - gap.size() / 3; ++row)
                input.iq[row] += tone(row, input.in_band_tone(), .35);
        } else if (test == 3) {
            const auto gap = input.guard(2);
            for (auto row = gap.first; row < gap.stop; ++row)
                input.iq[row] += tone(row, frequency + .025) - tone(row);
        }
        auto prediction = input.trained();
        const auto observed = input.observation();
        const auto score = input.evaluate(prediction, observed);
        const auto& evidence = prediction.result(target_slot);
        check(!evidence.attempted && !evidence.accepted && score.decision == SlotDecision::ambiguous &&
              evidence.training_status != "validated_prior_stationary_line",
              "missing or contaminated past training supported a quiet decision");
    }
}

void protects_native_gates() {
    const Input input;
    const auto trained = input.trained();
    const auto observed = input.observation();
    for (unsigned control = 0; control < 5; ++control) {
        auto prediction = trained;
        auto score = input.ambiguous_score(observed);
        switch (control) {
        case 0: score.all_valid = false; break;
        case 1: score.complete = false; break;
        case 2: score.operational_passes_conditional_bound = true; break;
        case 3: score.decision = SlotDecision::data; score.bit = 1; break;
        default: score.noise_statistics_valid = false; break;
        }
        const auto original = score;
        prediction.consider(input.config, input.slot, observed, score, {});
        check(!prediction.result(target_slot).attempted && score.decision == original.decision &&
              score.bit == original.bit && score.quiet_energy == original.quiet_energy && score.reason == original.reason,
              "predictive quiet superseded existing DATA, missing input, or strong ambiguous presence");
    }
}

} // namespace

int main() {
    try {
        frequency_geometry();
        stationary_with_independent_noise();
        check_continuous_residual_envelope();
        aligned_wanted_and_mismatched_current_line();
        retained_native_line();
        separated_public_baseline();
        invalid_or_contaminated_training();
        protects_native_gates();
        std::cout << "Predictive quiet stationary, aligned wanted, drift, multi-line, burst and guard tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Predictive quiet component check failed: " << error.what() << '\n';
        return 1;
    }
}
