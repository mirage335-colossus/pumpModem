#include "../src/gui/plot_data.hpp"
#include "../src/gui/pattern_score_view.hpp"
#include "datapump/streaming_modem.hpp"
#include <cmath>
#include <iostream>
#include <numbers>

using namespace datapump;
void check(bool condition, const char* message) { if (!condition) throw Error(message); }
namespace {
void pattern_evidence_retention() {
    using Clock = gui::PatternScoreView::Clock;
    using namespace std::chrono_literals;
    const auto now = Clock::time_point{} + 24h;
    live::Snapshot snapshot;
    snapshot.pattern_scores = {{10, 1}, {20, 2}, {30, 3}};
    snapshot.pattern_score_observations = {{1, now - 6s - 1ns, 10}, {2, now - 6s, 20}, {3, now - 1s, 30}};
    snapshot.pattern_score_observation_id = 3;
    gui::PatternScoreView view;
    check(view.scores(snapshot, now) == std::vector<gui::plots::PatternScore>{{{20, 2}, 20}, {{30, 3}, 30}},
          "Pattern evidence and its own admission threshold must remain visible at exactly six seconds and disappear only when older");
    check(view.scores(snapshot, now + 1ns) == std::vector<gui::plots::PatternScore>{{{30, 3}, 30}},
          "Pattern evidence must expire independently instead of retaining an old batch beside a newer point");
    check(view.scores(snapshot, now + 5s) == std::vector<gui::plots::PatternScore>{{{30, 3}, 30}} &&
          view.scores(snapshot, now + 5s + 1ns).empty(),
          "Polling an unchanged snapshot must not refresh the age of retained pattern evidence");
    check(snapshot.pattern_scores.size() == 3 && snapshot.pattern_score_observations.front().id == 1 &&
          snapshot.pattern_score_observations.back().observed_at == now - 1s &&
          snapshot.pattern_score_observations.back().admission_threshold == 30,
          "Display expiry must not remove or change the receiver's retained evidence");

    snapshot.pattern_scores = {{40, 4}};
    snapshot.pattern_score_observations = {{4, now, 4}};
    snapshot.pattern_score_observation_id = 7; // Other retained receiver hypotheses are currently hidden.
    view.clear_through(snapshot.pattern_score_observation_id);
    check(view.scores(snapshot, now).empty(), "Clearing the plot must hide current pattern evidence");
    snapshot.pattern_score_observations = {{6, now, 6}};
    check(view.scores(snapshot, now + 1s).empty(),
          "A retained receiver hypothesis must stay cleared if it is selected by a later snapshot");
    snapshot.pattern_scores.push_back(snapshot.pattern_scores.front());
    snapshot.pattern_score_observations.push_back({8, now + 1s, 8});
    snapshot.pattern_score_observation_id = 8;
    view.clear_through(3);
    check(view.scores(snapshot, now + 1s) == std::vector<gui::plots::PatternScore>{{{40, 4}, 8}},
          "A new observation with an identical score must keep its own threshold without reviving previously cleared IDs");
    check(snapshot.pattern_scores.size() == 2 && snapshot.pattern_score_observations.front().id == 6,
          "Clearing the plot must leave the receiver's evidence intact");

    // A long symbol first becomes a displayed observation when its full window
    // has completed, even though the window started hours before that point.
    const auto completed = now + 4h;
    snapshot.pattern_scores = {{50, 5}};
    snapshot.pattern_score_observations = {{9, completed, 16}};
    snapshot.pattern_score_observation_id = 9;
    const std::vector<gui::plots::PatternScore> long_symbol{{{50, 5}, 16}};
    check(view.scores(snapshot, completed) == long_symbol &&
          view.scores(snapshot, completed + 6s) == long_symbol &&
          view.scores(snapshot, completed + 6s + 1ns).empty(),
          "Fresh evidence from a completed long symbol must receive the same six-second display lifetime");
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
        pattern_evidence_retention();
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
