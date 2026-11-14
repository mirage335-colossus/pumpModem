#include "appraisal/hf_native_chain.hpp"
#include "datapump/triad_stream.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace appraisal;
constexpr double tau = 2 * std::numbers::pi;
constexpr unsigned fs = 2000;
std::size_t checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void near(double value, double expected, double tolerance, const char* message) {
    check(std::abs(value - expected) <= tolerance, message);
}
template<class F> void rejects(F&& fn, const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
Source source(std::uint64_t count, std::function<Complex(std::uint64_t)> sample,
              double band = 50, std::shared_ptr<std::uint64_t> cursor = {}) {
    Source result;
    result.format.sample_rate = fs;
    result.geometry.source_samples = count;
    result.geometry.carrier_spacing_hz = 40;
    result.geometry.occupied_bandwidth_hz = 100;
    result.minimum_frequency_hz = -band;
    result.maximum_frequency_hz = band;
    if (!cursor) cursor = std::make_shared<std::uint64_t>();
    result.sample_at = [cursor, sample = std::move(sample)](std::uint64_t index) {
        if (index != *cursor) throw std::logic_error("nonmonotone original source read");
        ++*cursor;
        return sample(index);
    };
    return result;
}
Source tone(std::uint64_t count, double hz, double amplitude = 1) {
    return source(count, [=](std::uint64_t n) { return std::polar(amplitude, tau * hz * n / fs); }, std::abs(hz));
}
HfNativeChainConfig config(std::uint64_t source_samples) {
    HfNativeChainConfig c;
    c.endpoints.effects.sample_rate = fs;
    c.endpoints.effects.occupied_bandwidth_hz = 100;
    c.endpoints.effects.rf_hz = c.propagation.rf_hz = 1e6;
    c.endpoints.maximum_chunk_samples = 4096;
    c.tx_horizon_samples = source_samples + 128;
    c.propagation_horizon_samples = source_samples + 384;
    c.capture_horizon_samples = source_samples + 512;
    c.tx_band = c.propagation_band = {-150, 150};
    return c;
}
std::vector<Complex> run(HfNativeChain& chain, std::size_t chunk) {
    std::vector<Complex> result(static_cast<std::size_t>(chain.total_samples()));
    std::vector<std::uint8_t> validity(chunk);
    std::size_t at = 0;
    while (at < result.size()) {
        const auto count = std::min(chunk, result.size() - at);
        const auto read = chain.read(std::span(result).subspan(at, count), std::span(validity).first(count));
        check(read == count, "chain ended before its declared capture horizon");
        check(std::all_of(validity.begin(), validity.begin() + count, [](auto x) { return x == 1; }),
              "chain manufactured an erased observation");
        at += read;
    }
    Complex extra;
    check(chain.read(std::span(&extra, 1)) == 0, "chain exceeded capture horizon");
    return result;
}

void identity_and_complete_tail() {
    constexpr std::uint64_t count = 1024;
    const auto sample = [](std::uint64_t i) { return Complex{.03 * (i % 7), -.02 * (i % 5)}; };
    auto cursor = std::make_shared<std::uint64_t>();
    HfNativeChain chain(source(count, sample, 50, cursor), config(count));
    check(*cursor == 0, "chain construction consumed the source");
    const auto values = run(chain, 53);
    for (std::size_t i = 0; i < values.size(); ++i)
        check(values[i] == (i < count ? sample(i) : Complex{}), "all-off chain altered the composite or silence");
    check(*cursor == count, "chain did not consume each source sample exactly once");
    const auto receipt = chain.report();
    check(receipt.tx.samples == receipt.tx_horizon_samples &&
          receipt.propagation.samples == receipt.propagation_horizon_samples &&
          receipt.rx.samples == receipt.capture_horizon_samples,
          "chain failed to drain all stage calendars");
    check(receipt.minimum_propagation_horizon_samples <= receipt.propagation_horizon_samples &&
          receipt.tx.natural_complete_samples <= receipt.tx_horizon_samples &&
          receipt.rx.natural_complete_samples <= receipt.capture_horizon_samples,
          "chain receipt omitted natural versus declared extents");

    auto delayed_config = config(count);
    delayed_config.propagation.paths[0].delay_seconds = 128. / fs;
    HfNativeChain delayed(source(count, [](auto i) { return i == count - 1 ? Complex{1, -.5} : Complex{}; }), delayed_config);
    const auto impulse = run(delayed, 17);
    for (std::size_t i = 0; i < impulse.size(); ++i)
        near(std::abs(impulse[i] - (i == count - 1 + 128 ? Complex{1, -.5} : Complex{})), 0, 1e-10,
             "final emitted pulse or delayed drain was truncated");

    auto dated_config = config(count);
    dated_config.endpoints.effects.timing_offset_seconds = 20. / fs;
    dated_config.endpoints.effects.capture_lead_seconds = 40. / fs;
    HfNativeChain dated(source(count, [](auto i) { return i == 20 ? Complex{1, 0} : Complex{}; }), dated_config);
    const auto shifted = run(dated, 31);
    for (std::size_t i = 0; i < shifted.size(); ++i)
        near(std::abs(shifted[i] - (i == 80 ? Complex{1, 0} : Complex{})), 0, 1e-10,
             "timing offset or capture lead was applied twice or on the wrong calendar");
}

void sample_clock_and_conversion_if() {
    constexpr std::uint64_t count = 4096;
    auto c = config(count);
    c.endpoints.effects.tx.audio_ppm = 1000;
    c.endpoints.effects.rx.audio_ppm = -2000;
    c.endpoints.effects.nominal_conversion_if_hz = 250;
    HfNativeChain chain(tone(count, 40), c);
    const auto values = run(chain, 113);
    Complex product{};
    for (std::size_t n = 128; n < 3800; ++n) product += values[n] * std::conj(values[n - 1]);
    const double measured = std::arg(product) * fs / tau;
    const double expected = (40 + 250) * (1 + .001) / (1 - .002) - 250;
    near(measured, expected, .002, "actual native clocks lost whole-audio conversion-IF retiming");
    check(std::abs(measured - 40) > .5, "sample-clock test did not change the waveform");
}

void oscillator_ordering_and_rf_plane_interference() {
    constexpr std::uint64_t count = 4096;
    auto c = config(count);
    c.endpoints.effects.tx.rf_ppm = 13;
    c.endpoints.effects.rx.rf_ppm = 7;
    c.propagation.paths = {Path{.power = .5}, Path{.power = .5, .delay_seconds = 20. / fs}};
    HfNativeChain chain(tone(count, 20), c);
    const auto values = run(chain, 79);
    const auto relative = std::sqrt(.5) * (Complex{1, 0} + std::polar(1., -tau * (20 + 13) * 20 / fs));
    double error = 0;
    for (std::size_t n = 64; n < 4000; ++n) {
        const auto expected = relative * std::polar(1., tau * ((20 + 13 - 7) * static_cast<double>(n) + 13 - 7) / fs);
        error = std::max(error, std::abs(values[n] - expected));
    }
    check(error < 1e-9, "TX oscillator did not precede delayed propagation or RX oscillator did not follow it");

    auto blocker_config = config(count);
    blocker_config.endpoints.effects.signal_enabled = false;
    blocker_config.endpoints.effects.tx.rf_ppm = 13;
    blocker_config.endpoints.effects.rx.rf_ppm = 7;
    blocker_config.propagation.noise.tone_amplitude = .2;
    blocker_config.propagation.noise.tone_hz = 70;
    HfNativeChain blocker(tone(count, 20), blocker_config);
    const auto interference = run(blocker, 67);
    for (std::size_t n = 64; n < 4000; ++n)
        near(std::abs(interference[n] - std::polar(.2, tau * (63 * static_cast<double>(n) - 7) / fs)),
             0, 1e-10, "RF-plane blocker inherited TX oscillator or bypassed RX oscillator");
}

void chunk_independence_and_owned_frontends() {
    constexpr std::uint64_t count = 2048;
    auto a = config(count);
    a.endpoints.effects.tx.audio_ppm = 120;
    a.endpoints.effects.rx.audio_ppm = -75;
    a.endpoints.effects.tx.phase_noise_rad_per_sqrt_second = .01;
    a.endpoints.effects.rx.phase_noise_rad_per_sqrt_second = .02;
    a.endpoints.effects.nominal_conversion_if_hz = 250;
    a.endpoints.frontend.dac.nominal_bits = 12;
    a.endpoints.frontend.adc.nominal_bits = 16;
    a.endpoints.frontend.lowpass_hz = 100;
    a.endpoints.frontend.tone_amplitude = .01;
    a.endpoints.frontend.tone_frequency_hz = 110;
    a.endpoints.oscillator_seed = 89;
    a.endpoints.noise_seed = 901;
    a.endpoints.frontend_seed = 73;
    a.final_receiver_noise_variance = .002;
    a.propagation.paths[0].delay_seconds = 13.25 / fs;
    a.propagation.noise.tone_amplitude = .03;
    a.propagation.noise.tone_hz = 80;
    a.bridge_chunk_samples = 1;
    auto b = a;
    b.bridge_chunk_samples = 73;
    HfNativeChain one(tone(count, 30, .2), a), two(tone(count, 30, .2), b);
    check(run(one, 1) == run(two, 97), "clock/phase/noise/frontend state depends on a bridge or caller chunk");
    const auto x = one.report(), y = two.report();
    check(x.tx.signal_energy == y.tx.signal_energy && x.rx.observed_energy == y.rx.observed_energy &&
          x.propagation.output_energy == y.propagation.output_energy, "chunking changed the stage receipts");
    check(x.tx.complex_noise_variance == 0 && x.rx.complex_noise_variance == .002 &&
          x.propagation.additive_noise_energy > 0, "explicit noise/interference plane ownership changed");

    auto limited = config(count);
    limited.endpoints.frontend.dac.nominal_bits = 8;
    limited.endpoints.frontend.adc.nominal_bits = 8;
    limited.endpoints.frontend.adc.full_scale = .05;
    limited.propagation.paths[0].power = .01;
    HfNativeChain converters(tone(count, 20, 2), limited);
    run(converters, 83);
    const auto hardware = converters.report();
    check(hardware.tx.dac_clipped_components > 0 && hardware.tx.adc_clipped_components == 0 &&
          hardware.rx.dac_clipped_components == 0 && hardware.rx.adc_clipped_components > 0,
          "DAC and ADC conversion ownership crossed the physical channel");
}

void canonical_emission_source() {
    using namespace datapump::triad::v3;
    PublicProfileConfig pc;
    pc.format.sample_rate = 1920;
    pc.format.inner_rate_hz = 120;
    pc.format.added_symbols = 1;
    pc.format.bit_seconds = .001;
    pc.data_if_hz = 500;
    const PublicProfile profile(pc);
    const auto key = datapump::Crypto::amateur();
    auto actual = Emission::isolated_fixture(profile, key, 1, true);
    auto reference = Emission::isolated_fixture(profile, key, 1, true);
    const auto input = actual.source();
    auto c = config(input.geometry.source_samples);
    c.endpoints.effects.sample_rate = pc.format.sample_rate;
    c.endpoints.effects.occupied_bandwidth_hz = input.geometry.occupied_bandwidth_hz;
    c.tx_band = c.propagation_band = {input.minimum_frequency_hz, input.maximum_frequency_hz};
    HfNativeChain chain(input, c);
    const auto values = run(chain, 101);
    auto ref = reference.source();
    for (std::uint64_t i = 0; i < input.geometry.source_samples; ++i)
        check(values[i] == ref.sample_at(i), "native Emission changed through the all-off chain");
    check(actual.report().generated_samples == input.geometry.source_samples,
          "native Emission source context was reset or consumed twice");
}

void continuing_interference_without_wanted_tail_truncation() {
    constexpr std::uint64_t count = 1024;
    auto c = config(count);
    c.continuing_rf_interference = true;
    c.public_endpoint_clock_bound_ppm = 2;
    c.endpoints.effects.rx.audio_ppm = -1;
    c.propagation_signal_stop_samples = count + 192;
    c.propagation_horizon_samples = c.capture_horizon_samples + 32;
    c.propagation.noise.tone_amplitude = .3;
    c.propagation.noise.tone_hz = 100;
    HfNativeChain chain(source(count, [](auto) { return Complex{}; }), c);
    const auto output = run(chain, 51);
    check(std::abs(output.back()) > .29, "RF interference was switched off before the final observation");
    const auto report = chain.report();
    check(report.continuing_rf_interference_at_capture_end &&
          report.minimum_wanted_capture_samples <= report.capture_horizon_samples &&
          report.rx.natural_complete_samples > report.capture_horizon_samples,
          "bounded capture confused continuing RF field EOF with wanted waveform support");
    c.propagation_signal_stop_samples = c.tx_horizon_samples - 1;
    rejects([&] { HfNativeChain invalid(tone(count, 20), c); },
            "continuing interference hid an underdeclared wanted tail");
}

void rejection_before_source_consumption() {
    constexpr std::uint64_t count = 1024;
    const auto bad = [&](auto change) {
        auto c = config(count);
        change(c);
        auto cursor = std::make_shared<std::uint64_t>();
        rejects([&] { HfNativeChain chain(source(count, [](auto) { return Complex{}; }, 50, cursor), c); },
                "invalid chain declaration was accepted");
        check(*cursor == 0, "rejected chain consumed its source");
    };
    bad([](auto& c) { c.tx_horizon_samples = count; });
    bad([](auto& c) { c.propagation_horizon_samples = c.tx_horizon_samples - 1; });
    bad([](auto& c) { c.capture_horizon_samples = c.propagation_horizon_samples; });
    bad([](auto& c) { c.tx_band = {-10, 10}; });
    bad([](auto& c) { c.propagation.noise.tone_amplitude = 1; c.propagation.noise.tone_hz = 200; });
    bad([](auto& c) { c.endpoints.effects.noise_enabled = true; });
    bad([](auto& c) { c.propagation.noise.white_variance = 1; });
    bad([](auto& c) { c.propagation.frontend.adc_bits = 12; });
    bad([](auto& c) { c.endpoints.effects.timing_offset_seconds = -.01; });
    bad([](auto& c) { c.endpoints.effects.spread_delay_ms = 1; });
    bad([](auto& c) { c.maximum_workspace_bytes = 1; });
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { HfNativeChain chain(tone(count, 20), config(count), stop.get_token()); },
            "cancelled chain construction was accepted");
}
} // namespace

int main() {
    try {
        identity_and_complete_tail();
        sample_clock_and_conversion_if();
        oscillator_ordering_and_rf_plane_interference();
        chunk_independence_and_owned_frontends();
        canonical_emission_source();
        continuing_interference_without_wanted_tail_truncation();
        rejection_before_source_consumption();
        std::cout << "Native HF chain checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
