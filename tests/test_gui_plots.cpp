#include "../src/gui/plot_data.hpp"
#include <iostream>
#include <numbers>

using namespace datapump;
void check(bool condition, const char* message) { if (!condition) throw Error(message); }
int main() {
    try {
        modem::Config config;
        std::vector<float> wave(2048);
        for (std::size_t i = 0; i < wave.size(); ++i)
            wave[i] = static_cast<float>(.7 * std::cos(2 * std::numbers::pi * config.carrier_hz * static_cast<double>(i) / config.sample_rate));
        const auto view = gui::plots::waveform_window(wave, config);
        check(view.size() == 65 && view.data() == wave.data() + wave.size() - view.size(),
              "default display must show twelve carrier cycles without altering the latest samples");
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
