#include "datapump/audio.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw Error(message);
}

// Supply the ordinary hardware capture/decoder queue with deterministic PCM.
// Keeping capture open at a checkpoint also proves that neither an audio EOF
// nor the end of a source token is used to manufacture physical completion.
struct CaptureScript {
    std::vector<float> samples;
    std::uint32_t rate = 0;
    std::atomic<std::size_t> released{0}, delivered{0};
};
CaptureScript* capture_script = nullptr;
constexpr std::uint64_t epoch = 1800000000;

struct Waveform {
    std::vector<float> samples;
    std::string text, bits;
    std::size_t payload_end = 0;
    std::uint32_t rate = 0;
    bool interval = false;
};

Waveform waveform(double target, std::string text, bool binary = false) {
    transfer::Options options;
    options.modem = tuning::resolve(3600, target, tuning::PatternMode::auto_pattern, false).config;
    options.timestamp = epoch;
    options.search_seconds = 0;
    Message message;
    message.data.assign(text.begin(), text.end());
    const auto bits = transfer::message_wire_bits(message, options);
    Waveform result;
    result.text = text;
    result.interval = !transfer::uses_raw_message(message);
    for (auto bit : bits) result.bits.push_back(bit ? '1' : '0');
    if (!result.interval)
        check(result.bits == (text == "e" ? "001" : "110001001110100110100100"),
              "short dictionary fixture changed its independent wire vector");
    if (binary) {
        auto transmitter = transfer::binary_transmitter(bits, options);
        result.samples.resize(transmitter->total_samples());
        for (std::size_t offset = 0; offset < result.samples.size();)
            offset += transmitter->read(std::span(result.samples).subspan(offset));
    } else result.samples = transfer::transmit(message, options);
    result.rate = options.modem.sample_rate;
    result.payload_end = modem::training_sample_count(options.modem) +
        modem::pattern_pulse_padding_samples(options.modem) + bits.size() * modem::symbol_sample_count(options.modem);
    // The modulator already emits its suppression tail. Pad to eight seconds
    // after payload so every fully scored six-second absence can be reported.
    result.samples.resize(result.payload_end + 8 * result.rate);
    return result;
}

live::Settings settings(std::vector<double> targets) {
    live::Settings value;
    value.simulation = false;
    value.device = "controlled profile capture";
    value.transfer.modem = tuning::resolve(3600, 55, tuning::PatternMode::auto_pattern, false).config;
    value.transfer.timestamp = epoch;
    value.transfer.search_seconds = 0;
    value.transfer.automatic_receive_profiles = true;
    value.transfer.receive_targets_db_hz = std::move(targets);
    value.transfer.receive_pattern_mode = tuning::PatternMode::auto_pattern;
    value.content_limit = value.transfer.content_limit = 1024 * 1024;
    value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes = 32 * 1024 * 1024;
    return value;
}

struct Observations {
    std::set<std::uint64_t> ids;
    std::set<std::string> prefixes;
    std::size_t complete = 0, received = 0;

    void inspect(const live::Snapshot& snapshot, const Waveform& expected,
                 std::size_t sample_origin, bool before_absence) {
        check(snapshot.error.empty(), "live profile capture failed: " + snapshot.error);
        check(snapshot.status.find("overrun") == std::string::npos,
              "profile fixture overran the hardware decode queue");
        check(snapshot.dsp_buffered_bytes <= 32 * 1024 * 1024,
              "profile bank exceeded its DSP workspace");
        for (const auto& signal : snapshot.signals) {
            ids.insert(signal.id);
            check(ids.size() == 1, "one physical transmission created multiple live signal IDs");
            if (!signal.complete) {
                check(signal.binary && !signal.validated && signal.raw_bits.empty(),
                      "pending profile observation acquired completed text semantics");
                prefixes.insert(signal.text);
                continue;
            }
            check(!before_absence && snapshot.samples_received >= sample_origin + expected.payload_end + 6 * expected.rate,
                  "profile observation completed before six seconds of physical absence");
            ++complete;
            check(!signal.binary && signal.validated == expected.interval && signal.text == expected.text &&
                  signal.raw_bits == (expected.interval ? std::string{} : expected.bits) &&
                  signal.received_bits == expected.bits.size() && signal.missing_symbols == 0,
                  "winning profile completed with bogus text or incorrect transport metadata");
        }
        for (const auto& result : snapshot.received) {
            check(!before_absence, "source content was interpreted before physical absence");
            ++received;
            check(result.stream_complete && result.short_text_decoded == !expected.interval &&
                  result.content_validated == expected.interval &&
                  !result.content.authenticated && result.observed_bits == expected.bits.size() && result.missing_symbols == 0,
                  "completed profile result lost its source validation or public-stream semantics");
            check(std::string(result.content.message.data.begin(), result.content.message.data.end()) == expected.text,
                  "a secondary profile published bogus received content");
            std::string raw;
            for (auto bit : result.raw_bits) raw.push_back(bit ? '1' : '0');
            check(raw == expected.bits.substr(0, 4096), "completed profile result changed exact transport bits");
        }
    }
};

void advance(live::Session& session, CaptureScript& capture, std::size_t target,
             Observations& observed, const Waveform& expected, std::size_t origin, bool before_absence) {
    capture.released = target;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    std::optional<std::chrono::steady_clock::time_point> drained_at;
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = session.snapshot();
        observed.inspect(snapshot, expected, origin, before_absence);
        if (capture.delivered >= target && snapshot.buffered_samples == 0) {
            if (!drained_at) drained_at = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - *drained_at >= 250ms) return;
        } else drained_at.reset();
        std::this_thread::sleep_for(1ms);
    }
    throw Error("controlled profile capture did not drain its bounded decoder queue");
}

void receive_wave(live::Session& session, CaptureScript& capture, const Waveform& expected,
                  std::size_t origin, Observations& observed) {
    advance(session, capture, origin + expected.payload_end + 5 * expected.rate,
            observed, expected, origin, true);
    check(!observed.ids.empty() && observed.complete == 0 && observed.received == 0,
          "profile bank hid pending data or completed without physical absence");
    check(observed.prefixes.contains(expected.bits.substr(0, 4096)), "exact transport prefix was hidden until physical completion");
    advance(session, capture, origin + expected.samples.size(), observed, expected, origin, false);
    check(observed.ids.size() == 1 && observed.complete == 1 && observed.received == 1,
          "one transmission must produce exactly one completed signal and one received item");
}

void run_case(const std::vector<double>& targets, double target, const std::string& text, bool binary) {
    const auto expected = waveform(target, text, binary);
    CaptureScript capture;
    capture.samples = expected.samples;
    capture.rate = expected.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings(targets));
    Observations observed;
    receive_wave(session, capture, expected, 0, observed);
    session.stop();
}

void sequential_profiles(const std::vector<double>& targets, double first_target, double second_target) {
    const auto first = waveform(first_target, "e"), second = waveform(second_target, "hello");
    CaptureScript capture;
    capture.samples = first.samples;
    capture.samples.insert(capture.samples.end(), second.samples.begin(), second.samples.end());
    capture.rate = first.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings(targets));
    Observations first_observed, second_observed;
    receive_wave(session, capture, first, 0, first_observed);
    receive_wave(session, capture, second, first.samples.size(), second_observed);
    check(first_observed.ids != second_observed.ids, "profile arbitration merged separate physical transmissions");
    session.stop();
}
}

// These definitions deliberately replace audio.cpp in this statically linked
// test executable. The live Session still uses its real asynchronous hardware
// capture queue, receiver bank, progress presentation and content publication.
namespace datapump::audio {
std::vector<Device> devices() { return {{"controlled profile capture", "deterministic test input"}}; }
void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& on_chunk,
             std::stop_token stop, StreamFormatCallback on_format) {
    check(capture_script && device == "controlled profile capture" && rate == capture_script->rate,
          "unexpected hardware capture request in profile fixture");
    auto& script = *capture_script;
    if (on_format) on_format({rate, rate, static_cast<double>(rate) / 2, 0});
    while (!stop.stop_requested()) {
        const auto begin = script.delivered.load();
        const auto end = std::min(script.released.load(), begin + std::max<std::uint32_t>(1, rate / 100));
        if (end > begin) {
            if (!on_chunk(std::span(script.samples).subspan(begin, end - begin))) return;
            script.delivered = end;
        }
        std::this_thread::sleep_for(1ms);
    }
}
void play(std::span<const float>, std::uint32_t, const std::string&, std::stop_token,
          StreamFormatCallback, bool) { throw Error("profile fixture unexpectedly played audio"); }
std::vector<float> record(double, std::uint32_t, const std::string&, std::size_t,
                          std::stop_token, StreamFormatCallback) { throw Error("profile fixture unexpectedly recorded audio"); }
void playback(std::uint32_t, const std::string&, const PlaybackCallback&, std::stop_token,
              StreamFormatCallback, bool) { throw Error("profile fixture unexpectedly played audio"); }
}

int main() {
    std::string context;
    try {
        for (const auto& targets : {std::vector<double>{55, 32}, std::vector<double>{32, 55}}) {
            for (const double target : {55., 32.}) {
                const auto profiles = "RX " + std::to_string(targets[0]) + "," + std::to_string(targets[1]) +
                    " TX " + std::to_string(target);
                context = profiles + " text e";
                run_case(targets, target, "e", false);
                context = profiles + " binary 001";
                run_case(targets, target, "e", true);
                context = profiles + " text hello";
                run_case(targets, target, "hello", false);
                context = profiles + " fixed interval text";
                run_case(targets, target, "hello fixed intervals", false);
            }
            context = "sequential TX 55 then 32, first RX " + std::to_string(targets[0]);
            sequential_profiles(targets, 55, 32);
            context = "sequential TX 32 then 55, first RX " + std::to_string(targets[0]);
            sequential_profiles(targets, 32, 55);
        }
        std::cout << "live receive profile arbitration passed\n";
    } catch (const std::exception& error) {
        std::cerr << context << ": " << error.what() << '\n';
        return 1;
    }
}
