#include "../src/gui/pattern_space.hpp"
#include "datapump/tuning.hpp"
#include "../src/constellation.hpp"
#include "datapump/streaming_modem.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>

using namespace datapump;
using gui::inspection::PatternSpace;
using gui::inspection::inspect_pattern_space;
namespace {
void check(bool value, const char* message) { if (!value) throw Error(message); }
void near(double value, double expected, const char* message, double tolerance = 1e-11) {
    check(std::abs(value - expected) <= tolerance * std::max(1., std::abs(expected)), message);
}
modem::RawBits symbol_bits(unsigned value, unsigned width) {
    modem::RawBits result;
    for (unsigned i = width; i; --i) result.bits.push_back(static_cast<std::uint8_t>((value >> (i - 1)) & 1U));
    return result;
}
void exact_transmitter_templates() {
    modem::Config config;
    config.bandwidth_hz = 1100; // Nonintegral chip clock: last chip is partial.
    for (const auto factor : {3U, 4U, 6U, 8U, 12U, 16U}) {
        config.spreading_factor = factor;
        for (unsigned mode = 0; mode != 3; ++mode) {
            config.spreading_mode = modem::SpreadingMode::pattern;
            config.scramble = mode == 1;
            config.dsss = mode == 2;
            config.spreading_seed[7] = 71; config.dsss_seed[3] = 19;
            for (unsigned width = 2; width <= 6; ++width) {
                config.constellation_bits = width;
                const auto model = inspect_pattern_space(config, 40);
                check(model.code.size() == factor && model.coefficients.size() == (1U << width),
                      "inspection must retain the entire configured code and every symbol");
                check(std::accumulate(model.chip_weights.begin(), model.chip_weights.end(), std::uint64_t{}) == model.symbol_samples,
                      "all actual symbol samples must have exactly one chip weight");
                for (unsigned value = 0; value < model.coefficients.size(); ++value) {
                    modem::StreamingTransmitter transmitter(symbol_bits(value, width), config);
                    std::vector<float> pcm(static_cast<std::size_t>(transmitter.total_samples()));
                    check(transmitter.read(pcm) == pcm.size() && transmitter.finished(), "fixture must emit precisely one complete raw symbol");
                    std::vector<std::complex<double>> analytic(pcm.size());
                    transmitter.preview_last_analytic(analytic);
                    for (std::size_t sample = 0; sample < analytic.size(); ++sample) {
                        const auto chip = static_cast<std::size_t>((sample / model.chip_samples) % model.code.size());
                        const auto oscillator = std::polar(1., 2 * std::numbers::pi * config.carrier_hz * static_cast<double>(sample) / config.sample_rate);
                        const auto expected = model.coefficients[value] * static_cast<double>(model.code[chip]) * oscillator;
                        check(std::abs(analytic[sample] - expected) < 1e-11,
                              "every inspected phase/amplitude chip must match the real transmitter's analytic carrier");
                        near(pcm[sample], expected.real(), "inspected templates must match transmitted real PCM", 4e-8);
                    }
                }
                for (std::size_t i = 0; i < model.coefficients.size(); ++i)
                    for (std::size_t j = 0; j < i; ++j) {
                        double integrated_distance = 0;
                        for (std::size_t k = 0; k < model.code.size(); ++k) {
                            const auto a = model.coefficients[i] * static_cast<double>(model.code[k]);
                            const auto b = model.coefficients[j] * static_cast<double>(model.code[k]);
                            integrated_distance += std::norm(a - b) * static_cast<double>(model.chip_weights[k]) / config.sample_rate;
                        }
                        near(model.squared_distance(i, j), integrated_distance, "whole-pattern distances must equal full weighted complex-vector distances");
                        near(model.noise_squared_distance(i, j), integrated_distance * 10000 / modem::nominal_signal_power,
                             "statistical distance must use the whole-symbol AWGN model");
                    }
                near(model.minimum_squared_distance, model.squared_distance(model.nearest_symbols.first, model.nearest_symbols.second),
                     "precomputed nearest pair must retain the complete-pattern metric");
                near(model.minimum_noise_squared_distance, model.noise_squared_distance(model.nearest_symbols.first, model.nearest_symbols.second),
                     "display-ready nearest distance must use the same noise model");
            }
        }
    }
}
void weighted_evidence(const PatternSpace& model) {
    auto examine = [&](const auto& evidence) {
        check(evidence.code.size() == model.code.size(), "comparison must retain every code chip");
        long double dot = 0, residual = 0;
        for (std::size_t i = 0; i < model.code.size(); ++i)
            dot += static_cast<long double>(model.chip_weights[i]) * model.code[i] * evidence.code[i];
        const auto correlation = static_cast<double>(dot / model.symbol_samples);
        for (std::size_t i = 0; i < model.code.size(); ++i) {
            const auto error = evidence.code[i] - correlation * model.code[i];
            residual += static_cast<long double>(model.chip_weights[i]) * error * error;
        }
        near(evidence.correlation, correlation, "matched filter correlation must use exact partial/repeated chip weights");
        near(evidence.residual_fraction, static_cast<double>(residual / model.symbol_samples),
             "unused-pattern residual must fit out an arbitrary complex scalar, including global sign");
        near(evidence.squared_distance, 2 * model.symbol_seconds * (1 - std::abs(correlation)),
             "comparison distance must use equal unit amplitude and the best common phase");
    };
    examine(model.one_chip_shift);
    if (model.unused_pattern) {
        examine(*model.unused_pattern);
        check(model.unused_pattern->residual_fraction > 0,
              "an unused example must not be the valid code multiplied by any complex scalar");
    }
}
void durations_and_modes() {
    modem::Config config;
    config.spreading_factor = 3; config.integration_seconds = 77.25 / config.sample_rate;
    auto model = inspect_pattern_space(config, 0);
    check(model.symbol_samples == 78 && model.chip_samples == 10 && model.complete_periods == 2 && model.tail_samples == 18,
          "quantized duration must include whole periods and a partial final chip");
    check(model.chip_weights == std::vector<std::uint64_t>{30, 28, 20}, "weights must account for the partial final repetition exactly");
    weighted_evidence(model);
    near(model.processing_gain_db, 10 * std::log10(7.8), "integration gain includes fractional chips");
    near(model.symbol_esn0_db - model.chip_esn0_db, model.processing_gain_db, "chip and symbol SNR must share the same C/N0");
    config.integration_seconds = 4.25 / config.sample_rate;
    model = inspect_pattern_space(config, 0);
    check(model.chip_weights == std::vector<std::uint64_t>{5, 0, 0} && !model.unused_pattern,
          "untransmitted code positions must not invent observable off-code directions");
    check(!model.code_selective && !model.timing_selective, "a single effective chip cannot provide chip-code timing evidence");
    weighted_evidence(model);

    for (const auto factor : {1U, 2U, 3U, 4U, 8U, 32U, 128U, 1024U, 4096U, 16384U}) {
        config.spreading_factor = factor; config.integration_seconds = 0;
        model = inspect_pattern_space(config, -10);
        check(model.unused_pattern.has_value() == (factor > 1), "multichip patterns retain off-code comparison directions");
        near(model.processing_gain_db, 10 * std::log10(factor), "full-pattern coherent integration gain follows its duration");
        weighted_evidence(model);
    }
    config.spreading_mode = modem::SpreadingMode::pattern; config.spreading_factor = 1;
    model = inspect_pattern_space(config, 0);
    check(!model.unused_pattern && !model.code_selective && !model.timing_selective,
          "a one-chip pattern must reveal its degenerate code geometry");
    config.spreading_factor = 16;
    model = inspect_pattern_space(config, 0);
    check(model.code_selective && model.timing_selective && model.unused_pattern,
          "a full changing-sign pattern must expose off-code and timing residual evidence");
    weighted_evidence(model);
    auto alternating = config; alternating.spreading_factor = 2; alternating.scramble = true;
    bool found_alternating = false;
    for (unsigned seed = 0; seed < 256 && !found_alternating; ++seed) {
        alternating.spreading_seed[0] = static_cast<std::uint8_t>(seed);
        const auto candidate = inspect_pattern_space(alternating, 0);
        if (!candidate.code_selective) continue;
        found_alternating = true;
        near(candidate.one_chip_shift.correlation, -1, "an antipodal code shift is the same legal complex subspace");
        check(!candidate.timing_selective && candidate.unused_pattern,
              "global sign ambiguity must remove one-chip timing evidence without removing true off-code examples");
        weighted_evidence(candidate);
    }
    check(found_alternating, "deterministic fixture must find a two-chip alternating keyed pattern");
    auto other = config; other.sample_rate *= 2;
    const auto same_time = inspect_pattern_space(other, 0);
    near(same_time.squared_distance(0, 1), model.squared_distance(0, 1), "physical distance must not depend on hardware/sample clock");
    near(same_time.noise_squared_distance(0, 1), model.noise_squared_distance(0, 1), "noise distance must not depend on sample clock");
    other.integration_seconds = model.symbol_seconds * 4;
    const auto slow = inspect_pattern_space(other, 0);
    near(slow.noise_squared_distance(0, 1), slow.symbol_seconds / model.symbol_seconds * model.noise_squared_distance(0, 1),
         "coherent time integration must increase squared noise distance linearly with actual quantized duration");
}
void bounded_and_public_illustration() {
    modem::Config config; config.spreading_factor = 16384; config.constellation_bits = 6;
    config.integration_seconds = 1e8; config.scramble = true; config.dsss = true;
    config.spreading_seed[1] = 71; config.dsss_seed[11] = 34;
    const auto private_model = inspect_pattern_space(config, -60);
    const auto public_model = inspect_pattern_space(config, -60, true);
    check(!private_model.representative_keyed && public_model.representative_keyed,
          "public illustrations must be explicitly distinguished from the caller's real configured seeds");
    check(public_model.code != private_model.code && public_model.code.size() == 16384 && public_model.coefficients.size() == 64,
          "very slow large patterns must retain only one bounded code period and the small symbol alphabet");
    config.spreading_seed.fill(197); config.dsss_seed.fill(21);
    const auto another = inspect_pattern_space(config, -60, true);
    check(public_model.code == another.code, "representative public code must not depend on private configuration seeds");
    weighted_evidence(public_model);
    config.integration_seconds = 0;
    const auto weak_chip = inspect_pattern_space(config, 10, true);
    check(weak_chip.chip_esn0_db < 0 && weak_chip.symbol_esn0_db > 10,
          "matched full-pattern evidence must be modeled above noise even when individual phase/amplitude chips are below noise");
    config.scramble = false; config.dsss = false;
    check(!inspect_pattern_space(config, -60, true).representative_keyed, "plain fixed code must not be mislabeled as a keyed illustration");
    config.sample_rate = 120000000; config.bandwidth_hz = 30000000; config.carrier_hz = 22500000;
    config.integration_seconds = 0;
    check(std::isfinite(inspect_pattern_space(config, 120).noise_squared_distance(0, 1)), "30 MHz model must remain bounded and finite");
    bool rejected = false;
    try { (void)inspect_pattern_space(config, std::numeric_limits<double>::quiet_NaN()); } catch (const Error&) { rejected = true; }
    check(rejected, "nonfinite C/N0 must be rejected before display");
}
void binary_pattern_preview() {
    for(const auto mode:{tuning::PatternMode::auto_pattern,tuning::PatternMode::auto_keystream,tuning::PatternMode::auto_tone}) {
        auto config=tuning::resolve(1200,40,mode,true).config;
        const auto model=inspect_pattern_space(config,40,true);
        check(model.bounded_pattern_preview && model.coefficients.size()==2 && model.codewords.size()==2,
              "binary pattern inspection must preserve two independent codeword rows");
        check(model.squared_distance(0,1)>0 && model.squared_distance(0,0)==0,
              "binary pattern rows must have measured nonzero distance despite equal scalar coefficients");
        modem::PatternCode expected(config,config.stream_epoch);
        if(!config.scramble)for(std::size_t i=0;i<model.code.size();++i)
            check(std::abs(model.chip_value(1,i)-std::sqrt(2*modem::nominal_signal_power)*expected.value(i,1,.5))<1e-10,
                  "pattern preview must render the actual selected codeword");
        config.integration_seconds=3600;
        const auto long_model=inspect_pattern_space(config,0,true);
        check(long_model.truncated && long_model.code.size()==16384 && long_model.symbol_samples<long_model.full_symbol_samples,
              "hour-long pattern inspection must retain only a bounded explicitly labeled prefix");
    }
}
}
int main() {
    try {
        exact_transmitter_templates(); durations_and_modes(); bounded_and_public_illustration();binary_pattern_preview();
        std::cout << "Static full-pattern space tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
