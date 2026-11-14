#include "datapump/triad_receiver.hpp"
#include "triad_random_process.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace {

using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;

constexpr unsigned workers = 2;
constexpr unsigned sample_rate = 497;
constexpr double phase_turn = 2 * std::numbers::pi;
unsigned checks = 0;

void check(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

template<class Exception, class Function>
void rejects(Function&& function, const char* reason) {
    try {
        function();
    } catch (const Exception&) {
        ++checks;
        return;
    }
    throw std::runtime_error(reason);
}

// A complete, nonzero-ordinal observation in the same small native waveform
// geometry used by the existing receiver line test. The source mapping and
// seed mixer are native. The test alone knows the bit, origin and physical line.
struct Observation {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    Crypto key = Crypto::amateur();
    std::vector<Complex> iq;
    std::vector<std::uint8_t> valid;

    Observation() {
        config.sample_rate_hz = sample_rate;
        config.data_if_hz = 1725;
        config.noise_variance = .25;
        slot.index = 37;
        slot.first_sample = 20'000;
        slot.window_capture_lead_seconds = -17.7;
        slot.source_start_seconds = 18.1;
        auto& format = slot.format;
        format.sample_rate = sample_rate;
        format.inner_rate_hz = 120;
        format.added_symbols = 1;
        format.bit_seconds = .001;
        format.pulse_shape = PulseShape::TaperedRrc;
        format.epoch = 1793850000;
        format.symbol_ordinal = 7000037;
        const auto extent = geometry(format).source_samples;
        check(extent + 403 < 6000, "component observation grew beyond its sample cap");
        slot.stop_sample = slot.first_sample + extent + 403;
        slot.quiet_first_sample = slot.first_sample + 230;
        slot.quiet_stop_sample = slot.first_sample + extent + 180;
        iq.resize(extent + 403);
        valid.resize(iq.size(), 1);
        for (std::size_t row = 0; row < iq.size(); ++row) {
            const auto u = datapump::triad::v3::detail::random_process_uniform(37, 2 * row);
            const auto v = datapump::triad::v3::detail::random_process_uniform(37, 2 * row + 1);
            iq[row] = std::polar(std::sqrt(-config.noise_variance * std::log(u)), phase_turn * v);
        }
    }

    void add_data(std::uint8_t bit, double gain, double frequency = 0, double drift = 0) {
        PatternSource source(slot.format, key, bit);
        Candidate physical;
        physical.arrival_seconds = static_cast<double>(slot.window_capture_lead_seconds + slot.source_start_seconds);
        physical.frequency_hz = frequency - physical.arrival_seconds * drift;
        physical.frequency_drift_hz_per_second = drift;
        std::vector<double> offsets(iq.size());
        std::iota(offsets.begin(), offsets.end(), 0.);
        std::vector<Complex> basis(iq.size());
        shaped_basis_mapped(source, physical, sample_rate, 0, offsets, basis, config.data_if_hz);
        for (std::size_t row = 0; row < iq.size(); ++row) iq[row] += gain * basis[row];
    }

    double add_test_line() {
        const auto first = slot.quiet_first_sample - slot.first_sample;
        const auto stop = slot.quiet_stop_sample - slot.first_sample;
        const auto spacing = sample_rate / static_cast<double>(std::bit_ceil(2 * (stop - first)));
        // Halfway between coarse bins, inside atmospheric but outside the
        // nominal DATA band. Do not hand this line's frequency to the scorer.
        const auto frequency = (std::floor(80 / spacing) + .5) * spacing;
        for (auto row = first; row < stop; ++row)
            iq[row] += std::polar(1.5, phase_turn * frequency * row / sample_rate);
        return frequency;
    }

    std::size_t workspace() const {
        return receiver_slot_workspace(config, slot, workers);
    }

    ReceiverSlotScore compare(std::size_t bytes, std::stop_token stop = {},
                              ReceiverExecutor* executor = nullptr) const {
        return score_slot_comparing_admitted_evidence(config, slot, key, iq, valid, workers,
            bytes, stop, executor);
    }

    ReceiverSlotScore baseline() const {
        return score_receiver_slot(config, slot, key, iq, valid, workers, workspace());
    }
};

void off_preserves_ordinary_results() {
    Observation observation;
    for (auto bit : {false, true}) {
        if (bit) observation.add_data(1, .72);
        const auto expected = observation.baseline();
        const auto compared = observation.compare(observation.workspace());
        check(compared.complete && compared.all_valid && compared.consumed_samples == observation.iq.size(),
              "Off comparator lost the complete observation");
        check(bit ? (expected.decision == SlotDecision::data && expected.bit == 1) :
                    (expected.decision == SlotDecision::quiet && !expected.bit),
              "Off positive/quiet native control failed");
        check(compared.decision == expected.decision && compared.bit == expected.bit &&
              compared.reason == expected.reason &&
              compared.operational_bit_evidence == expected.operational_bit_evidence &&
              compared.operational_log_e == expected.operational_log_e &&
              compared.quiet_energy == expected.quiet_energy &&
              compared.basis_rows == expected.basis_rows &&
              !compared.varying_candidates && !compared.coherent_gain_candidates && !compared.drift_timing_atoms,
              "comparison changed Off decisions, evidence, or scored branches");
    }
}

void narrow_alternatives() {
    Observation observation;
    observation.config.time_varying_channel = true;
    observation.config.frequency_bound_hz = .23;
    observation.config.time_varying_common_doppler_hz = 3;
    observation.config.time_varying_atmospheric_hz = 3;
    const auto bytes = observation.workspace();
    check(bytes > 0 && bytes <= 256 * 1024 * 1024 &&
          varying_fine_plan(observation.config, observation.slot, workers).block_rows &&
          varying_fine_plan(observation.config, observation.slot, workers, false, true).block_rows &&
          drift_plan(observation.config, observation.slot, workers).workspace_bytes,
          "component geometry must admit short varying, coherent gain, and drift");

    ReceiverExecutor executor(workers);
    const auto noise = observation.compare(bytes, {}, &executor);
    check(noise.complete && noise.decision == SlotDecision::quiet && !noise.bit &&
          !noise.operational_passes_conditional_bound && noise.noise_statistics_valid,
          "comparing every admitted likelihood mistook seeded noise for DATA");

    observation.add_data(1, .72);
    const auto original = observation.baseline();
    const auto data = observation.compare(bytes, {}, &executor);
    check(original.decision == SlotDecision::data && original.bit == 1 &&
          original.reason == "fixed_affine_data_evidence" && !original.varying_candidates &&
          !original.coherent_gain_candidates && !original.drift_timing_atoms,
          "ordinary native entry point no longer exits on the strong affine control");
    check(data.complete && data.all_valid && data.index == observation.slot.index &&
          data.decision == SlotDecision::data && data.bit == 1 && data.operational_passes_conditional_bound &&
          data.operational_threshold == original.operational_threshold,
          "complete comparison must keep the strong bit and native presence threshold");
    check(data.coarse_frequency_points == original.coarse_frequency_points &&
          data.coarse_workspaces == original.coarse_workspaces && data.basis_rows == original.basis_rows &&
          data.bit_evidence == original.bit_evidence && data.workspace_peak_bytes == bytes,
          "comparison changed the inherited acquisition, affine scorer, or sequential workspace quote");
    check(data.varying_candidates && data.varying_basis_rows &&
          data.coherent_gain_candidates && data.coherent_gain_blocks > 1 &&
          data.coherent_gain_frequency_states && data.drift_timing_atoms && data.drift_slopes,
          "strong affine evidence still bypassed a publicly admitted model");
    check(data.operational_bit_evidence[1] >= original.operational_bit_evidence[1],
          "ordinary model evidence addition lost retained likelihood mass");

    rejects<ReceiverWorkspaceError>([&] { (void)observation.compare(bytes - 1, {}, &executor); },
        "one byte below the canonical workspace bound must fail admission");
    std::stop_source cancellation;
    cancellation.request_stop();
    rejects<Error>([&] { (void)observation.compare(bytes, cancellation.get_token(), &executor); },
        "pre-cancelled comparison returned a scored outcome");
    observation.valid[50] = 0;
    const auto missing = observation.compare(bytes, {}, &executor);
    check(missing.decision == SlotDecision::ambiguous && !missing.bit && !missing.complete &&
          !missing.all_valid && !missing.noise_statistics_valid && !missing.varying_candidates &&
          !missing.coherent_gain_candidates && !missing.drift_timing_atoms,
          "a known invalid sample supplied DATA or physical quiet evidence");
}

void coherent_line_is_independent_of_data() {
    Observation observation;
    observation.config.time_varying_channel = true;
    observation.config.time_varying_common_doppler_hz = 90;
    observation.config.time_varying_atmospheric_hz = 90;
    const auto frequency = observation.add_test_line();
    const auto bytes = observation.workspace();
    check(bytes <= 256 * 1024 * 1024, "line test exceeds its workspace cap");
    const auto line = observation.compare(bytes);
    check(line.complete && line.decision == SlotDecision::ambiguous && !line.bit &&
          line.reason == "coherent_line_in_public_data_aperture" &&
          line.varying_quiet_energy > line.varying_quiet_threshold &&
          std::abs(line.varying_line_frequency_hz - frequency) < .02,
          "a retained spectral line became DATA or physical quiet");

    const auto duration = geometry(observation.slot.format).source_samples /
        effective_sample_rate(observation.slot.format);
    observation.add_data(1, .14, 10.5, -9 / duration);
    const auto coexist = observation.compare(bytes);
    check(coexist.complete && coexist.decision == SlotDecision::data && coexist.bit == 1 &&
          coexist.reason == "varying_data_evidence_with_coherent_line" &&
          std::abs(coexist.varying_line_frequency_hz - frequency) < .02,
          "comparing residual models must preserve separable keyed DATA beside a coherent line");
}

} // namespace

int main() {
    try {
        off_preserves_ordinary_results();
        narrow_alternatives();
        coherent_line_is_independent_of_data();
        std::cout << checks << " native admitted-evidence checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
