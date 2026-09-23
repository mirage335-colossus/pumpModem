#include "datapump/audio.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace datapump;
using namespace std::chrono_literals;

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw Error(message);
}

#ifdef _WIN32
// The simulated capture yields for 1 ms after each unchanged PCM chunk.
// A coarse Windows timer can round that yield to 15.6 ms and make the fixture
// itself exceed its deadline before delivering all samples to an idle decoder.
// Scope the intended timer resolution to this test process, not the application.
struct FixtureTimerResolution {
    FixtureTimerResolution() {
        check(timeBeginPeriod(1) == TIMERR_NOERROR,
              "could not request the profile fixture's 1 ms timer resolution");
    }
    ~FixtureTimerResolution() { timeEndPeriod(1); }
    FixtureTimerResolution(const FixtureTimerResolution&) = delete;
    FixtureTimerResolution& operator=(const FixtureTimerResolution&) = delete;
};
#endif

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
    std::size_t payload_end = 0, transmit_end = 0;
    std::uint32_t rate = 0;
    std::size_t symbol_samples = 0;
    bool interval = false, binary = false, ambiguous_tone = false;
};

struct Configuration {
    double bandwidth = 3600;
    std::optional<double> carrier;
    tuning::PatternMode mode = tuning::PatternMode::auto_pattern;
    float noise_sigma = 0;
    bool keyed = false;
};

modem::Config modem_config(double target, const Configuration& configuration) {
    return tuning::resolve(configuration.bandwidth, target, configuration.mode, configuration.keyed,
                           configuration.carrier).config;
}

Waveform waveform(double target, std::string text, bool binary = false,
                  const Configuration& configuration = {}) {
    transfer::Options options;
    options.modem = modem_config(target, configuration);
    options.timestamp = epoch;
    options.search_seconds = 0;
    if (configuration.keyed) options.key.emplace(Bytes(32, 0x67));
    Message message;
    message.data.assign(text.begin(), text.end());
    auto bits = transfer::message_wire_bits(message, options);
    if (configuration.keyed) transfer::xor_binary_bits(bits, options);
    if (binary) {
        bits.clear();
        for (char digit : text) {
            check(digit == '0' || digit == '1', "invalid explicit binary fixture");
            bits.push_back(static_cast<std::uint8_t>(digit - '0'));
        }
    }
    Waveform result;
    result.text = binary && text == "001" ? "e" : text;
    result.interval = !binary && !transfer::uses_raw_message(message);
    result.binary = binary && text != "001";
    result.ambiguous_tone = tuning::tone_mode(configuration.mode);
    for (auto bit : bits) result.bits.push_back(bit ? '1' : '0');
    if (!result.interval && !binary)
        check(result.bits == (text == "e" ? "001" : "110001001110100110100100"),
              "short dictionary fixture changed its independent wire vector");
    if (binary) {
        auto transmitter = transfer::binary_transmitter(bits, options);
        result.samples.resize(transmitter->total_samples());
        for (std::size_t offset = 0; offset < result.samples.size();)
            offset += transmitter->read(std::span(result.samples).subspan(offset));
    } else result.samples = transfer::transmit(message, options);
    result.transmit_end = result.samples.size();
    result.rate = options.modem.sample_rate;
    result.symbol_samples = modem::symbol_sample_count(options.modem);
    result.payload_end = modem::training_sample_count(options.modem) +
        modem::pattern_pulse_padding_samples(options.modem) + bits.size() * modem::symbol_sample_count(options.modem);
    // The modulator already emits its suppression tail. Include complete
    // absent symbols and the FFT search lookahead, including for long symbols.
    const auto absent_symbols = (6 * result.rate + result.symbol_samples - 1) / result.symbol_samples;
    result.samples.resize(result.payload_end + absent_symbols * result.symbol_samples +
                          std::max<std::size_t>(2 * result.rate, 2 * result.symbol_samples));
    if (configuration.noise_sigma) {
        std::mt19937 random(714);
        std::normal_distribution<float> noise(0, configuration.noise_sigma);
        for (auto& sample : result.samples) sample += noise(random);
    }
    return result;
}

live::Settings settings(std::vector<double> targets, const Configuration& configuration = {}) {
    live::Settings value;
    value.simulation = false;
    value.device = "controlled profile capture";
    value.transfer.modem = modem_config(55, configuration);
    value.transfer.timestamp = epoch;
    value.transfer.search_seconds = 0;
    value.transfer.automatic_receive_profiles = true;
    value.transfer.receive_targets_db_hz = std::move(targets);
    value.transfer.receive_pattern_mode = configuration.mode;
    if (configuration.keyed) {
        value.transfer.key.emplace(Bytes(32, 0x67));
        value.receive_keys.emplace_back(Bytes(32, 0x68));
    }
    value.content_limit = value.transfer.content_limit = 1024 * 1024;
    value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes = 32 * 1024 * 1024;
    return value;
}

struct Observations {
    std::set<std::uint64_t> ids;
    std::set<std::string> prefixes;
    std::map<std::uint64_t, live::SignalUpdate> rows;
    std::map<std::uint64_t, std::uint64_t> revisions;
    std::map<std::string, transfer::Received> content;
    std::size_t complete = 0, received = 0;
    bool allow_revisions = false;

    void check_final(const Waveform& expected) const {
        check(rows.size() == 1, "one physical transmission retained multiple effective signal rows");
        const auto& signal = rows.begin()->second;
        check(!ids.empty() && signal.id == *ids.begin(),
              "stronger profile abandoned the original reception identity");
        if (expected.ambiguous_tone) {
            // Equal consecutive tones carry no observable symbol boundary.
            // Check coherent presentation, without inventing a profile flag
            // or an originating bit count absent from these waveforms.
            check(signal.complete && !signal.validated && signal.received_bits && !signal.missing_symbols,
                  "ambiguous tone reception did not retain one completed unvalidated interpretation");
            const auto& bits = signal.binary ? signal.text : signal.raw_bits;
            check(bits.size() == signal.received_bits && bits.find_first_not_of("01") == std::string::npos,
                  "tone interpretation lost its exact observed raw bits");
            check(content.size() == (signal.binary ? 0 : 1),
                  "ambiguous tone reception retained duplicate content");
            if (!content.empty()) {
                const auto& result = content.begin()->second;
                check(result.stream_complete && result.short_text_decoded && !result.content_validated &&
                      !result.content.authenticated && result.observed_bits == signal.received_bits,
                      "ambiguous tone result disagrees with its one completed row");
            }
            return;
        }
        check(signal.complete && signal.binary == expected.binary &&
              signal.validated == expected.interval && signal.text == expected.text &&
              signal.raw_bits == (expected.interval || expected.binary ? std::string{} : expected.bits) &&
              signal.received_bits == expected.bits.size() && signal.missing_symbols == 0,
              "winning profile completed with bogus text or incorrect transport metadata");
        check(content.size() == (expected.binary ? 0 : 1),
              "one transmission retained an incorrect number of effective received items");
        if (!content.empty()) check_received(content.begin()->second, expected);
    }

    static void check_received(const transfer::Received& result, const Waveform& expected) {
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

    void inspect(const live::Snapshot& snapshot, const Waveform& expected,
                 std::size_t sample_origin, bool before_absence) {
        check(snapshot.error.empty(), "live profile capture failed: " + snapshot.error);
        check(snapshot.status.find("overrun") == std::string::npos,
              "profile fixture overran the hardware decode queue");
        check(snapshot.dsp_buffered_bytes <= 32 * 1024 * 1024,
              "profile bank exceeded its DSP workspace");
        for (const auto& signal : snapshot.signals) {
            ids.insert(signal.id);
            if (!allow_revisions)
                check(ids.size() == 1, "one physical transmission created multiple live signal IDs");
            const auto prior = rows.find(signal.id);
            if (signal.revision < revisions[signal.id]) continue;
            revisions[signal.id] = signal.revision;
            for (const auto superseded : signal.superseded_ids) {
                revisions[superseded] = std::max(revisions[superseded], signal.revision);
                const auto obsolete = rows.find(superseded);
                if (obsolete != rows.end()) content.erase(obsolete->second.reception_id);
                rows.erase(superseded);
            }
            if (prior != rows.end() && signal.revision > prior->second.revision)
                content.erase(prior->second.reception_id);
            rows[signal.id] = signal;
            if (!signal.complete) {
                check(signal.binary && !signal.validated && signal.raw_bits.empty(),
                      "pending profile observation acquired completed text semantics");
                prefixes.insert(signal.text);
                continue;
            }
            if (!allow_revisions)
                check(!before_absence && snapshot.samples_received >= sample_origin + expected.payload_end + 6 * expected.rate,
                      "profile observation completed before six seconds of physical absence");
            ++complete;
            if (!allow_revisions && !expected.ambiguous_tone) check(signal.binary == expected.binary && signal.validated == expected.interval && signal.text == expected.text &&
                  signal.raw_bits == (expected.interval || expected.binary ? std::string{} : expected.bits) &&
                  signal.received_bits == expected.bits.size() && signal.missing_symbols == 0,
                  "winning profile completed with bogus text or incorrect transport metadata");
        }
        for (const auto& result : snapshot.received) {
            if (!allow_revisions) check(!before_absence, "source content was interpreted before physical absence");
            ++received;
            std::string id;
            constexpr char digits[] = "0123456789abcdef";
            for (auto byte : result.content.message.local_id) {
                id += digits[byte >> 4];
                id += digits[byte & 15];
            }
            content[id] = result;
            if (!allow_revisions && !expected.ambiguous_tone) check_received(result, expected);
        }
    }
};

void advance(live::Session& session, CaptureScript& capture, std::size_t target,
             Observations& observed, const Waveform& expected, std::size_t origin, bool before_absence) {
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    std::optional<std::chrono::steady_clock::time_point> drained_at;
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = session.snapshot();
        observed.inspect(snapshot, expected, origin, before_absence);
        // This functional arbitration fixture advances a controlled PCM clock;
        // it is not the separate fast_session real-time throughput check. The
        // old unconditional 20 ms / 1 ms producer could outrun instrumented
        // decoding and discard a valid waveform. Keep only a short burst ahead
        // and wait for the actual asynchronous queue to drain before granting
        // more input. The production queue, its overrun assertion and every
        // physical-symbol/end assertion stay unchanged.
        const auto delivered = capture.delivered.load();
        if (delivered < target && delivered >= capture.released.load() && snapshot.buffered_samples == 0) {
            const auto burst = std::max<std::size_t>(8, capture.rate / 10);
            capture.released = delivered + std::min(burst, target - delivered);
        }
        if (capture.delivered >= target && snapshot.buffered_samples == 0) {
            if (!drained_at) drained_at = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - *drained_at >= 250ms) return;
        } else drained_at.reset();
        std::this_thread::sleep_for(1ms);
    }
    throw Error("controlled profile capture did not drain its bounded decoder queue: delivered=" +
                std::to_string(capture.delivered.load()) + " target=" + std::to_string(target) +
                " buffered=" + std::to_string(session.snapshot().buffered_samples));
}

void receive_wave(live::Session& session, CaptureScript& capture, const Waveform& expected,
                  std::size_t origin, Observations& observed) {
    advance(session, capture, origin + expected.payload_end + 5 * expected.rate,
            observed, expected, origin, true);
    check(!observed.ids.empty() && (observed.allow_revisions || (observed.complete == 0 && observed.received == 0)),
          "profile bank hid pending data or completed without physical absence");
    if (expected.symbol_samples > 6 * expected.rate) {
        // More than six seconds of silence is insufficient while the absent
        // symbol itself still has not been observed through its endpoint. The
        // FFT's lookahead can consume most of that window before admitting the
        // last payload bit, so assert its prefix after that work is available.
        advance(session, capture, origin + expected.payload_end + expected.symbol_samples - 1,
                observed, expected, origin, true);
        check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete,
              "long profile completed before a full absent symbol was scored");
        for (std::size_t count = 1; count <= expected.bits.size(); ++count)
            check(observed.prefixes.contains(expected.bits.substr(0, count)),
                  "a long-profile accepted bit was hidden behind later message progress");
    }
    if (!expected.ambiguous_tone)
        check(observed.prefixes.contains(expected.bits.substr(0, 4096)), "exact transport prefix was hidden until physical completion");
    check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete &&
          (expected.ambiguous_tone || observed.rows.begin()->second.text == expected.bits.substr(0, 4096)),
          "stronger admitted profile did not replace competing hypotheses in the pending row");
    advance(session, capture, origin + expected.samples.size(), observed, expected, origin, false);
    if (!observed.allow_revisions)
        check(observed.ids.size() == 1 && observed.complete == 1 &&
              observed.received == (observed.rows.begin()->second.binary ? 0 : 1),
              "one transmission must produce exactly one completed signal and one received item when decodable");
    observed.check_final(expected);
}

void run_case(const std::vector<double>& targets, double target, const std::string& text, bool binary,
              const Configuration& configuration = {}, bool allow_revisions = false) {
    const auto expected = waveform(target, text, binary, configuration);
    CaptureScript capture;
    capture.samples = expected.samples;
    capture.rate = expected.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings(targets, configuration));
    Observations observed;
    observed.allow_revisions = allow_revisions;
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

void separated_long_and_short() {
    Configuration configuration;
    configuration.bandwidth = 16;
    configuration.carrier = 16;
    const auto first = waveform(5, "0", true, configuration);
    const auto second = waveform(55, "001", true, configuration);
    // The first receiver must finish an entire 32-second absent symbol. A
    // different sender may already start after seven seconds of actual silence;
    // its independent clock must not be absorbed by that pending absence wait.
    const auto second_origin = first.transmit_end + 7 * first.rate;
    CaptureScript capture;
    capture.samples = first.samples;
    capture.samples.resize(std::max(capture.samples.size(), second_origin + second.samples.size()));
    std::copy(second.samples.begin(), second.samples.end(), capture.samples.begin() + second_origin);
    capture.rate = first.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings({55, 5}, configuration));
    Observations observed;
    observed.allow_revisions = true;
    advance(session, capture, second_origin + second.payload_end + 5 * second.rate,
            observed, first, 0, false);
    check(observed.content.empty() &&
          std::any_of(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
              return !row.second.complete && row.second.text == "001";
          }), "independent short transmission completed before physical absence or lost its exact pending prefix");
    advance(session, capture, first.payload_end + first.symbol_samples - 1,
            observed, first, 0, false);
    const auto long_row = std::find_if(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
        return !row.second.complete && row.second.binary && row.second.text == "0" && row.second.received_bits == 1;
    });
    const auto short_row = std::find_if(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
        return row.second.complete && !row.second.binary && row.second.text == "e" && row.second.raw_bits == "001";
    });
    check(observed.rows.size() == 2 && long_row != observed.rows.end() && short_row != observed.rows.end(),
          "long pending absence merged an independent short transmission or completed before a full absent symbol");
    const auto long_id = long_row->first, short_id = short_row->first;
    advance(session, capture, capture.samples.size(), observed, first, 0, false);
    check(observed.rows.size() == 2 && observed.rows.contains(long_id) && observed.rows.contains(short_id) &&
          observed.rows.at(long_id).complete && observed.rows.at(long_id).binary &&
          observed.rows.at(long_id).text == "0" && observed.rows.at(long_id).received_bits == 1 &&
          observed.rows.at(short_id).complete && observed.rows.at(short_id).raw_bits == "001" &&
          observed.content.size() == 1,
          "separate long and short physical transmissions lost their distinct completed identities");
    Observations::check_received(observed.content.begin()->second, second);
    session.stop();
}

void continuous_long_fft_single_bit() {
    auto value = settings({});
    auto& config = value.transfer.modem;
    config.sample_rate = 64;
    config.bandwidth_hz = 8;
    config.carrier_hz = 16;
    config.spreading_factor = 64;
    config.integration_seconds = 40;
    value.transfer.automatic_receive_profiles = false;
    // A single public profile with ample workspace selects the full FFT
    // receiver. The multi-profile fixtures above prefer streamed templates
    // and therefore cannot catch delays caused by FFT input batching.
    modem::PatternSearch search;
    search.expand_clock_search = true;
    search.allow_local_clock_fallback = true;
    search.start_offset_seconds = static_cast<double>(modem::pattern_pulse_padding_samples(config)) /
                                  config.sample_rate;
    search.start_uncertainty_seconds = 1;
    modem::PatternReceiver path(config, value.dsp_workspace_bytes / 2, search);
    check(!path.clock_windowed(), "long single-bit fixture did not select the full FFT receiver");

    Waveform expected;
    expected.text = expected.bits = "0";
    expected.binary = true;
    expected.rate = config.sample_rate;
    expected.symbol_samples = modem::symbol_sample_count(config);
    expected.payload_end = modem::training_sample_count(config) +
        modem::pattern_pulse_padding_samples(config) + expected.symbol_samples;
    auto transmitter = transfer::binary_transmitter(Bytes{0}, value.transfer);
    expected.transmit_end = transmitter->total_samples();
    expected.samples.resize(expected.payload_end + 3 * expected.symbol_samples);
    for (std::size_t offset = 0; offset < expected.transmit_end;)
        offset += transmitter->read(std::span(expected.samples).subspan(offset, expected.transmit_end - offset));

    CaptureScript capture;
    capture.samples = expected.samples;
    capture.rate = expected.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(value);
    Observations observed;
    // Keep the microphone stream open. A complete, high-SNR payload symbol
    // must become pending without waiting for the rest of an arbitrary FFT
    // block, for a full absent symbol, or for an offline finish() flush.
    advance(session, capture, expected.payload_end + expected.symbol_samples / 2,
            observed, expected, 0, true);
    check(observed.rows.size() == 1 && observed.prefixes.contains("0") &&
          !observed.rows.begin()->second.complete && observed.complete == 0 && observed.received == 0,
          "continuous full FFT capture hid the one-bit pending prefix until a later input block");
    advance(session, capture, expected.payload_end + expected.symbol_samples - 1,
            observed, expected, 0, true);
    check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete,
          "continuous full FFT capture completed before observing the whole absent symbol");
    advance(session, capture, expected.samples.size(), observed, expected, 0, false);
    check(observed.ids.size() == 1 && observed.complete == 1 && observed.received == 0,
          "continuous full FFT capture changed identity or duplicated one-bit completion");
    observed.check_final(expected);
    session.stop();
}

void simulated_long_fft_single_bit(double duration = 40, double snr = 30, std::uint64_t seed = 1) {
    auto value = settings({});
    auto& config = value.transfer.modem;
    config.sample_rate = 64;
    config.bandwidth_hz = 8;
    config.carrier_hz = 16;
    config.spreading_factor = 64;
    config.integration_seconds = duration;
    value.transfer.automatic_receive_profiles = false;
    value.simulation = true;
    value.simulation_snr_db = snr;
    value.simulation_seed = seed;
    const bool require_reception = snr == 30;
    if (duration >= 400)
        check(modem::default_pattern_frequency_offsets(config).size() > 5,
              "seed fixture lost its expanded carrier and coupled-clock bank");
    // Retain the free-running sound-card model: 100 ppm clock mismatch and
    // 0.5 degrees per square-root second of independent phase diffusion.
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([] { return static_cast<double>(epoch); }, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    session.start(value);
    check(session.snapshot().simulation_compute_seconds == 0,
          "idle simulation retained a previous computation's elapsed time");
    session.transmit_bits(Bytes{0});
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    bool computed = false;
    double elapsed = 0, fraction = 0;
    std::uint64_t samples = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = session.snapshot();
        check(snapshot.error.empty(), "long sampled simulation failed: " + snapshot.error);
        check(snapshot.received.empty(), "single raw zero unexpectedly became source text");
        check(snapshot.simulation_compute_seconds >= elapsed,
              "simulation elapsed time moved backward while its presentation clock was frozen");
        if (snapshot.transmitting && elapsed > 0 && snapshot.samples_received == samples &&
            snapshot.transmission_fraction == fraction)
            check(snapshot.simulation_compute_seconds > elapsed,
                  "simulation elapsed time stopped advancing while audio/DSP progress was unchanged");
        elapsed = snapshot.simulation_compute_seconds;
        fraction = snapshot.transmission_fraction;
        samples = snapshot.samples_received;
        if (snapshot.simulation_receiving_tail)
            check(snapshot.transmitting && !snapshot.transmission_finished && fraction == 1,
                  "receiver-tail stage claimed unfinished TX audio or completed simulation work");
        if (snapshot.transmission_finished && snapshot.simulation_replay) {
            check(!snapshot.simulation_receiving_tail && elapsed > 0,
                  "completed computation kept the tail stage or lost its wall-clock elapsed time");
            computed = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    check(computed, "long sampled simulation did not finish: audio fraction " + std::to_string(fraction) +
          ", samples " + std::to_string(samples) + ", elapsed " + std::to_string(elapsed));
    std::optional<std::uint64_t> pending_id;
    std::size_t complete = 0;
    for (std::int64_t milliseconds = 0; milliseconds <= 3000; milliseconds += 10) {
        replay_milliseconds = milliseconds;
        const auto snapshot = session.snapshot();
        check(snapshot.error.empty(), "long sampled replay failed: " + snapshot.error);
        check(snapshot.simulation_compute_seconds == elapsed && !snapshot.simulation_receiving_tail,
              "presentation replay changed the completed computation's elapsed time or stage");
        check(snapshot.received.empty(), "single raw zero unexpectedly became source text during replay");
        check(snapshot.dsp_buffered_bytes <= value.dsp_workspace_bytes,
              "long sampled simulation exceeded the configured DSP workspace");
        for (const auto& signal : snapshot.signals) {
            if (!require_reception) continue;
            check(signal.binary && !signal.validated && signal.text == "0" &&
                  signal.received_bits == 1 && signal.missing_symbols == 0,
                  "long sampled simulation changed its exact one-bit reception");
            if (!signal.complete) {
                if (!pending_id) pending_id = signal.id;
                check(signal.id == *pending_id, "long sampled simulation changed its pending reception identity");
            } else {
                check(pending_id && signal.id == *pending_id,
                      "long sampled simulation withheld its pending bit or changed identity at completion");
                ++complete;
            }
        }
    }
    check(!require_reception || (pending_id && complete == 1),
          "long sampled simulation did not replay a pending bit and exactly one completed reception");
    const auto finished = session.snapshot();
    check(!finished.transmitting && finished.transmission_finished && !finished.simulation_replay,
          "sampled simulation did not leave replay after its presentation deadline");
    session.configure(value);
    check(session.snapshot().simulation_compute_seconds == 0 && !session.snapshot().simulation_receiving_tail,
          "reconfiguration retained the completed simulation's elapsed time or stage");
    session.stop();
}

void sampled_long_fft_seeds() {
    for (const auto seed : {1ULL, 7ULL, 19ULL, 73ULL}) for (const auto snr : {30., -25.}) {
        try { simulated_long_fft_single_bit(400, snr, seed); }
        catch (const Error& error) {
            throw Error("seed " + std::to_string(seed) + " / SNR " + std::to_string(snr) + ": " + error.what());
        }
    }
}
}

// These definitions deliberately replace audio.cpp in this statically linked
// test executable. The live Session still uses its real asynchronous hardware
// capture queue, receiver bank, progress presentation and content publication.
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,
             std::stop_token stop,StreamFormatCallback format,Options options) {
    validate_options(options);
    capture(rate,device,callback,stop,std::move(format));
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& callback,
              std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options) {
    validate_options(options);
    playback(rate,device,callback,stop,std::move(format),channels);
}
std::vector<Device> devices() { return {{"controlled profile capture", "deterministic test input"}}; }
void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& on_chunk,
             std::stop_token stop, StreamFormatCallback on_format) {
    check(capture_script && device == "controlled profile capture" && rate == capture_script->rate,
          "unexpected hardware capture request in profile fixture");
    auto& script = *capture_script;
    if (on_format) on_format({rate, rate, static_cast<double>(rate) / 2, 0});
    while (!stop.stop_requested()) {
        const auto begin = script.delivered.load();
        const auto end = std::min(script.released.load(), begin + std::max<std::uint32_t>(8, rate / 50));
        if (end > begin) {
            if (!on_chunk(std::span(script.samples).subspan(begin, end - begin))) return;
            script.delivered = end;
        }
        std::this_thread::sleep_for(1ms);
    }
}
void play(std::span<const float>, std::uint32_t, const std::string&, std::stop_token,
          StreamFormatCallback, ChannelMode) { throw Error("profile fixture unexpectedly played audio"); }
std::vector<float> record(double, std::uint32_t, const std::string&, std::size_t,
                          std::stop_token, StreamFormatCallback) { throw Error("profile fixture unexpectedly recorded audio"); }
void playback(std::uint32_t, const std::string&, const PlaybackCallback&, std::stop_token,
              StreamFormatCallback, ChannelMode) { throw Error("profile fixture unexpectedly played audio"); }
}

int main(int argc, char** argv) {
    std::string context;
    try {
#ifdef _WIN32
        const FixtureTimerResolution timer_resolution;
#endif
        const std::string suite = argc > 1 ? argv[1] : "all";
        check(suite == "all" || suite == "original" || suite == "matrix" || suite == "long" || suite == "long_fft" || suite == "long_seeds" || suite == "interval_queue",
              "unknown profile test suite");
        if (suite == "interval_queue") {
            context = "RX 55,32 TX 32 fixed interval queue pacing";
            run_case({55, 32}, 32, "hello fixed intervals", false);
        }
        if (suite == "all" || suite == "long_seeds") {
            context = "sampled expanded FFT simulation across strong and weak noise seeds";
            sampled_long_fft_seeds();
        }
        if (suite == "all" || suite == "long" || suite == "long_fft") {
            context = "continuous public full FFT capture of one 40-second bit";
            continuous_long_fft_single_bit();
            context = "sampled public full FFT simulation of one 40-second bit";
            simulated_long_fft_single_bit();
        }
        if (suite == "all" || suite == "original") for (const auto& targets : {std::vector<double>{55, 32}, std::vector<double>{32, 55}}) {
            for (const double target : {55., 32.}) {
                const auto profiles = "RX " + std::to_string(targets[0]) + "," + std::to_string(targets[1]) +
                    " TX " + std::to_string(target);
                context = profiles + " text e";
                run_case(targets, target, "e", false);
                context = profiles + " binary 001";
                run_case(targets, target, "001", true);
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
        if (suite == "all" || suite == "matrix") {
            for (auto targets : {std::vector<double>{55, 62, 72}, std::vector<double>{20, 26, 32}}) {
                std::set<std::uint64_t> durations;
                for (auto target : targets)
                    durations.insert(modem::symbol_sample_count(modem_config(target, {})));
                check(durations.size() == targets.size(), "profile matrix lost its distinct symbol geometries");
                // Different automatic chip floors and integration durations;
                // every permutation must choose evidence, never bank order.
                do {
                    for (double target : targets) {
                        context = "permuted RX " + std::to_string(targets[0]) + "," +
                            std::to_string(targets[1]) + "," + std::to_string(targets[2]) +
                            " TX " + std::to_string(target) + " binary 001";
                        run_case(targets, target, "001", true);
                    }
                } while (std::next_permutation(targets.begin(), targets.end()));
                for (double target : targets) {
                    context = "three-profile TX " + std::to_string(target) + " binary 0";
                    run_case(targets, target, "0", true);
                }
            }
            Configuration noisy;
            noisy.noise_sigma = .005f;
            for (double target : {55., 26., 20.}) {
                context = "mixed noisy RX 55,26,20 TX " + std::to_string(target);
                run_case({55, 26, 20}, target, "001", true, noisy);
            }
            Configuration tone;
            tone.mode = tuning::PatternMode::auto_tone;
            const auto double_tone = waveform(55, "00", true, tone);
            const auto single_tone = waveform(32, "0", true, tone);
            const auto physical_end = double_tone.payload_end + 3 * double_tone.rate;
            check(double_tone.bits == "00" && single_tone.bits == "0" &&
                  double_tone.payload_end == single_tone.payload_end &&
                  std::equal(double_tone.samples.begin(), double_tone.samples.begin() + physical_end,
                             single_tone.samples.begin()),
                  "unframed repeated-tone fixture no longer demonstrates identical PCM across symbol durations");
            for (double target : {55., 26., 20.}) {
                context = "tone RX 20,55,26 TX " + std::to_string(target) + " binary 0";
                run_case({20, 55, 26}, target, "0", true, tone);
            }
            Configuration keyed;
            keyed.keyed = true;
            for (double target : {55., 32., 26.}) {
                context = "keyed RX 32,26,55 with an unrelated receive key, TX " + std::to_string(target);
                run_case({32, 26, 55}, target, "001", true, keyed);
            }
        }
        if (suite == "all" || suite == "long") {
            Configuration long_symbols;
            long_symbols.bandwidth = 16;
            long_symbols.carrier = 16;
            check(modem_config(5, long_symbols).sample_rate == 64 &&
                  modem::symbol_seconds(modem_config(5, long_symbols)) > 6,
                  "long fixture no longer exercises a full absent symbol beyond six seconds");
            for (const auto& targets : {std::vector<double>{55, 5}, std::vector<double>{5, 55},
                                       std::vector<double>{55, 20, 5}, std::vector<double>{5, 20, 55}}) {
                context = "long RX first " + std::to_string(targets.front()) +
                    " count " + std::to_string(targets.size()) + " TX 5 binary 001";
                run_case(targets, 5, "001", true, long_symbols, true);
            }
            context = "off-clock short transmission during long-profile absence wait";
            separated_long_and_short();
        }
        std::cout << "live receive profile arbitration passed\n";
    } catch (const std::exception& error) {
        std::cerr << context << ": " << error.what() << '\n';
        return 1;
    }
}
