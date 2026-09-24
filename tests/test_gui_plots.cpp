#include "../src/gui/plot_data.hpp"
#include "../src/gui/pattern_constellation.hpp"
#include "../src/constellation.hpp"
#include "datapump/streaming_modem.hpp"
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

using namespace datapump;
void check(bool condition, const char* message) { if (!condition) throw Error(message); }
namespace {
void pattern_constellation_geometry() {
    using namespace gui::plots;
    modem::Config config;
    config.sample_rate = 6000; config.bandwidth_hz = 1200; config.carrier_hz = 1500;
    config.spreading_factor = 16;
    for (unsigned bits = 2; bits <= 6; ++bits) {
        config.constellation_bits = bits;
        const auto model = pattern_constellation(config, 40);
        check(model.symbols.size() == (1U << bits), "pattern symbols must represent the configured APSK alphabet, not all chip bit strings");
        check(model.observations.empty(), "theoretical pattern centres must not invent received measurements");
        double closest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < model.symbols.size(); ++i) {
            const auto actual = modem::detail::mapped(static_cast<unsigned>(i), bits, {1, 0});
            check(std::abs(model.symbols[i].centre * model.quadrature_sigma - actual) < 1e-14,
                  "pattern symbol labels must preserve the modulator's phase and amplitude mapping");
            check(model.symbols[i].radial_sigma == 1 && model.symbols[i].tangential_sigma >= std::sqrt(2.),
                  "differential noise must include uncertainty in the preceding received phase");
            for (std::size_t j = 0; j < i; ++j) {
                const auto other = modem::detail::mapped(static_cast<unsigned>(j), bits, {1, 0});
                // Every possible shared spreading sign is an isometry. Compare
                // the integrated complete-template metric, not a second bank
                // of invented independently selected chip sequences.
                double template_distance = 0;
                for (unsigned chip = 0; chip < config.spreading_factor; ++chip) {
                    const auto sign = (chip * 13U + 7U) % 5U < 2U ? -1. : 1.;
                    template_distance += std::norm(sign * actual - sign * other);
                }
                template_distance /= config.spreading_factor * model.quadrature_sigma * model.quadrature_sigma;
                const auto distance = std::abs(model.symbols[i].centre - model.symbols[j].centre);
                check(std::abs(distance * distance - template_distance) < 1e-10 * template_distance,
                      "pattern projection must preserve complete-template statistical distance");
                closest = std::min(closest, distance);
            }
        }
        check(std::abs(closest - model.minimum_template_distance) < 1e-12, "nearest template separation must be measured in the displayed noise units");
        check(std::abs(model.symbols[model.closest_symbols.first].centre - model.symbols[model.closest_symbols.second].centre) == model.minimum_template_distance,
              "nearest-template connector must identify the actual minimum pair");
        auto alternative = config;
        alternative.sample_rate *= 2;
        const auto doubled_clock = pattern_constellation(alternative, 40);
        check(doubled_clock.symbols.front().centre == model.symbols.front().centre,
              "pattern statistical distance must depend on integration time, not the hardware or internal sample clock");
        alternative = config; alternative.spreading_factor *= 4;
        const auto longer = pattern_constellation(alternative, 40);
        check(std::abs(longer.minimum_template_distance / model.minimum_template_distance - 2.) < 1e-12,
              "four times the integration must double noise-normalized template separation");
        for (const auto mode : {modem::SpreadingMode::pattern, modem::SpreadingMode::tone}) {
            alternative = config; alternative.spreading_mode = mode;
            alternative.scramble = mode == modem::SpreadingMode::pattern;
            alternative.dsss = true; alternative.spreading_seed[0] = 93; alternative.dsss_seed[0] = 81;
            const auto shared_code = pattern_constellation(alternative, 40);
            check(shared_code.symbols.front().centre == model.symbols.front().centre,
                  "shared keyed, fixed and tone templates must not invent different template distances");
        }
    }
    config.constellation_bits = 6;
    const auto high_snr = pattern_constellation(config, 80);
    const auto value = modem::detail::mapped(31, config.constellation_bits, {1, 0});
    const auto radial = value / std::abs(value), tangent = radial * std::complex<double>{0, 1};
    const auto reference = modem::detail::radius_step(config.constellation_bits);
    std::mt19937_64 random(81273);
    const double sample_snr = 80 - 10 * std::log10(config.sample_rate / 2.);
    double radial_variance = 0, tangent_variance = 0;
    constexpr std::size_t trials = 16000;
    for (std::size_t i = 0; i < trials; ++i) {
        const auto previous = modem::add_awgn({{reference, 0}, modem::symbol_sample_count(config)}, sample_snr, random).value;
        const auto received = modem::add_awgn({value, modem::symbol_sample_count(config)}, sample_snr, random).value;
        const auto differential = received * std::conj(previous) / std::abs(previous);
        const auto error = (differential - value) / high_snr.quadrature_sigma;
        radial_variance += std::pow((error * std::conj(radial)).real(), 2);
        tangent_variance += std::pow((error * std::conj(tangent)).real(), 2);
    }
    radial_variance /= trials; tangent_variance /= trials;
    check(std::abs(radial_variance - 1) < .04, "AWGN simulation must agree with the pattern plot's radial noise normalization");
    check(std::abs(tangent_variance / std::pow(high_snr.symbols[31].tangential_sigma, 2) - 1) < .04,
          "weakest-reference ellipse must include the actual differential phase noise variance");

    std::vector<std::complex<double>> measurements(2100, {.137, -.291});
    measurements[2098] = {std::numeric_limits<double>::quiet_NaN(), 0};
    const auto observed = pattern_constellation(config, 40, measurements);
    check(observed.observations.size() == 2047 && observed.omitted == 53,
          "pattern observations must be bounded and account for displaced and nonfinite points");
    check(std::abs(observed.observations.back() * observed.quadrature_sigma - measurements.back()) < 1e-14,
          "pattern observations must remain measured coordinates, without nearest-symbol snapping");
    config.spreading_factor = 16384; config.integration_seconds = 1e8;
    const auto slow = pattern_constellation(config, -60);
    check(slow.symbols.size() == 64 && std::isfinite(slow.minimum_template_distance),
          "hour-long or large spreading patterns must not allocate chip histories or codeword banks");
    config.sample_rate = 120000000; config.bandwidth_hz = 30000000; config.carrier_hz = 22500000;
    config.integration_seconds = 0; config.spreading_factor = 1;
    const auto wide = pattern_constellation(config, 120);
    check(std::isfinite(wide.quadrature_sigma) && wide.quadrature_sigma > 0,
          "wideband pattern geometry must remain finite at the supported SDR bandwidth");
    bool rejected = false;
    try { (void)pattern_constellation(config, std::numeric_limits<double>::infinity()); }
    catch (const Error&) { rejected = true; }
    check(rejected, "nonfinite noise models must be rejected before drawing");
}
void sampled_waveform_reconstruction() {
    constexpr auto radius = gui::plots::waveform_kernel_radius;
    constexpr std::size_t first = 100, count = 65, width = 400;
    std::vector<float> source(256);
    for (const auto frequency : {.03125, .1875, .25, .3125, .4}) {
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<float>(.7 * std::cos(2 * std::numbers::pi * frequency * static_cast<double>(i) + .31));
        const auto trace = gui::plots::waveform_reconstruction(source, first, count, width);
        check(trace.size() > count && trace.size() <= 2 * width + 1, "reconstruction must follow display resolution within a fixed bound");
        const auto subdivisions = (trace.size() - 1) / (count - 1);
        double largest_error = 0;
        for (std::size_t i = 0; i < trace.size(); ++i) {
            const auto time = static_cast<double>(first) + static_cast<double>(i) / static_cast<double>(subdivisions);
            const auto expected = .7 * std::cos(2 * std::numbers::pi * frequency * time + .31);
            largest_error = std::max(largest_error, std::abs(trace[i] - expected));
        }
        check(largest_error < .0001, "display reconstruction distorted a captured band-limited sinusoid");
        for (std::size_t i = 0; i < count; ++i)
            check(trace[i * subdivisions] == source[first + i], "reconstruction moved an actual sample knot");
    }
    modem::Config config;
    config.sample_rate = 6000; config.carrier_hz = 1500;
    const auto guarded = gui::plots::waveform_window(source, config, 1, radius);
    check(guarded.size() == 17 && guarded.data() >= source.data() + radius && guarded.data() + guarded.size() == source.data() + source.size() - radius,
          "visible reconstruction must use measured guard samples, without extrapolating its endpoints");
    check(gui::plots::waveform_window(source, config, 100, radius).size() == source.size(),
          "overview zoom must retain the full capture instead of cropping it for reconstruction");
    std::fill(source.begin(), source.end(), .37f);
    const auto dc = gui::plots::waveform_reconstruction(source, first, count, width);
    for (const auto value : dc) check(std::abs(value - .37f) < 1e-7, "reconstruction changed a DC level");
    for (const auto window_start : {std::size_t{0}, source.size() - count})
        for (const auto value : gui::plots::waveform_reconstruction(source, window_start, count, width))
            check(std::abs(value - .37f) < 1e-7, "partial capture boundary changed the trace gain");
    const std::array<float, 2> partial{-.7f, .2f};
    const auto startup = gui::plots::waveform_reconstruction(partial, 0, partial.size(), width);
    check(startup.front() == partial.front() && startup.back() == partial.back(), "startup trace changed measured endpoints");
    for (const auto value : startup) check(std::isfinite(value) && std::abs(value) <= .7f, "partial capture produced an invalid or unbounded trace");
    std::fill(source.begin(), source.end(), 0);
    source[first + count / 2] = 1;
    const auto impulse = gui::plots::waveform_reconstruction(source, first, count, width);
    check(*std::min_element(impulse.begin(), impulse.end()) < -.1, "sampled impulse lost its band-limited sidelobes");
    const auto subdivisions = (impulse.size() - 1) / (count - 1);
    for (std::size_t i = 0; i < count; ++i)
        check(impulse[i * subdivisions] == source[first + i], "impulse sample was replaced by a manufactured carrier");
    std::vector<float> other(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) other[i] = static_cast<float>(static_cast<int>((i * 37) % 101) - 50) / 64;
    const auto noise = gui::plots::waveform_reconstruction(other, first, count, width);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] += other[i];
    const auto combined = gui::plots::waveform_reconstruction(source, first, count, width);
    for (std::size_t i = 0; i < combined.size(); ++i)
        check(std::abs(combined[i] - impulse[i] - noise[i]) < 2e-7, "display reconstruction must preserve arbitrary captured signals linearly");
    check(gui::plots::waveform_reconstruction(source, first, count, 16).empty(), "overview must retain raw sample envelopes instead of reconstructing subpixel cycles");
    check(gui::plots::waveform_reconstruction(source, first, 2, 1000000).size() <= 8193, "extreme zoom exceeded the reconstruction allocation limit");
    check(gui::plots::waveform_reconstruction({}, 0, 0, 100).empty(), "empty reconstruction must be harmless");
    check(gui::plots::waveform_reconstruction(source, 0, 1, 100).empty(), "a single sample cannot define a reconstructed trace");
    check(gui::plots::waveform_reconstruction(source, first, count, 0).empty(), "zero-width reconstruction must be harmless");
}
}
int main() {
    try {
        pattern_constellation_geometry();
        sampled_waveform_reconstruction();
        modem::Config config;
        config.sample_rate = 4800; config.carrier_hz = 900;
        std::vector<float> wave(2048);
        for (std::size_t i = 0; i < wave.size(); ++i)
            wave[i] = static_cast<float>(.7 * std::cos(2 * std::numbers::pi * config.carrier_hz * static_cast<double>(i) / config.sample_rate));
        const auto view = gui::plots::waveform_window(wave, config);
        check(view.size() == 23 && view.data() == wave.data() + wave.size() - view.size(),
              "default display must show four carrier cycles without altering the latest samples");
        check(gui::plots::waveform_window(wave, config, 3).size() == 65,
              "timebase zoom must continue to expose the twelve-cycle sparse-sample fixture");
        check(gui::plots::waveform_window(wave, config, 100).size() == wave.size(), "zoom out must expose the full capture");
        config.sample_rate = 120000000; config.carrier_hz = 22500000;
        check(gui::plots::waveform_window(wave, config).size() == view.size(), "waveform timebase must follow bandwidth, not a fixed audio clock");
        std::vector<float> spikes(1024);
        spikes[31] = .9f; spikes[37] = -.8f;
        const auto columns = gui::plots::waveform_columns(spikes, 16);
        check(columns.size() == 16 && columns[0].high == .9f && columns[0].low == -.8f,
              "overview must preserve both peaks instead of aliasing or skipping samples");
        check(gui::plots::waveform_columns({}, 10).empty(), "empty waveform must be harmless");
        gui::plots::SpectrumHistory history;
        std::vector<double> bins(1025, -100);
        for (const auto position : {0U, 1U, 2U, 3U, 511U, 1023U, 1024U}) {
            bins[position] = -12;
            history.push(bins, 4800. / 2048);
            const auto& row = history.rows().back();
            check(*std::max_element(row.begin(), row.end()) == -12, "waterfall discarded an FFT bin containing a carrier");
            bins[position] = -100;
        }
        const auto intensity = history.intensity(-12);
        std::fill(bins.begin(), bins.end(), -45); history.push(bins, 4800. / 2048);
        check(history.intensity(-12) == intensity && history.upper_db() == 0 && history.lower_db() == -100,
              "changing row noise floor must not change the meaning of its colors");
        std::fill(bins.begin(), bins.end(), 38); history.push(bins, 4800. / 2048);
        check(history.upper_db() == 40 && history.rows().front()[0] == -12,
              "large simulated noise requires one common rescale without modifying past measurements");
        history.push(bins, 9600. / 2048);
        check(history.rows().size() == 1 && history.max_hz() == 4800, "frequency-axis changes must clear incompatible history");
        for (int i = 0; i < 200; ++i) history.push(bins, 9600. / 2048);
        check(history.rows().size() == 160, "waterfall history must remain bounded");
        std::cout << "GUI plot projection tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
