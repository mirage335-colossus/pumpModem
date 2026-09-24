#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace live_test_audio {
std::atomic<std::uint64_t> played_samples{0};
}
// Link-time audio adapter: exercise Session's actual playback/capture branch
// without a host sound card. Simulation must never call this adapter.
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,
             std::stop_token stop,StreamFormatCallback format) {
    if(device!="live-test-audio")throw Error("unexpected audio capture in live test");
    if(format)format({rate,rate,.42*rate,4096});
    const std::vector<float> silence(std::max<std::uint32_t>(1,rate/20));
    while(!stop.stop_requested()) {
        if(!callback(silence))return;
        std::this_thread::sleep_for(2ms);
    }
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& callback,
              std::stop_token stop,StreamFormatCallback format) {
    if(device!="live-test-audio")throw Error("unexpected audio playback in live test");
    if(format)format({rate,rate,.42*rate,4096});
    std::array<float,4096> output{};
    while(!stop.stop_requested()) {
        const auto count=callback(output);
        if(count>output.size())throw Error("live playback exceeded output capacity");
        if(!count)return;
        live_test_audio::played_samples.fetch_add(count);
        std::this_thread::sleep_for(2ms);
    }
}
}
namespace {
void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
template<class Function> void rejects(Function action, const char* description) {
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error(description);
}
live::Settings settings() {
    live::Settings value;
    value.simulation = true;
    value.simulation_snr_db = 18;
    // These cases isolate transport, scheduling and UI replay. Oscillator
    // impairments have separate channel and live-default coverage.
    value.simulation_clock_error_ppm = 0;
    value.simulation_phase_noise_degrees_per_sqrt_second = 0;
    value.device = "THIS DEVICE MUST NEVER BE OPENED IN SIMULATION";
    value.transfer.modem.sample_rate = 8000;
    value.transfer.modem.bandwidth_hz = 1000;
    value.transfer.modem.carrier_hz = 1500;
    value.transfer.compression = false;
    value.transfer.fec = FecMode::off;
    return value;
}
Message message(std::uint8_t id, std::size_t size) {
    Message result;
    result.callsign = "N0CALL";
    result.id.fill(id);
    const std::string alphabet = "The continuous channel delivers this sentence in actual sampled audio. ";
    for (std::size_t i = 0; i < size; ++i) result.data.push_back(static_cast<std::uint8_t>(alphabet[i % alphabet.size()]));
    return result;
}
std::string message_id(const Message& message) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : message.id) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
void check_signal_metrics(const live::Snapshot& snapshot) {
    for (const auto& signal : snapshot.signals) {
        if (!signal.validated)
            check(!signal.pre_fec_accuracy, "pending signal text cannot claim validated pre-FEC data accuracy");
        if (signal.preamble_received_percent)
            check(std::isfinite(*signal.preamble_received_percent) && *signal.preamble_received_percent >= 0 &&
                  *signal.preamble_received_percent <= 100, "preamble reception percentage must be finite and within 0..100");
    }
    for (const auto& received : snapshot.received) {
        const auto id = message_id(received.packet.message);
        const auto signal = std::find_if(snapshot.signals.begin(), snapshot.signals.end(), [&](const auto& update) {
            return update.validated && update.packet_id == id;
        });
        check(signal != snapshot.signals.end(), "validated packet has no matching final signal-browser update");
        check(received.packet.pre_fec_accuracy.has_value() && signal->pre_fec_accuracy.has_value(),
              "validated packet and signal-browser row must carry exact pre-FEC body-bit counters");
        const auto& packet_accuracy = *received.packet.pre_fec_accuracy;
        const auto& signal_accuracy = *signal->pre_fec_accuracy;
        check(signal_accuracy.received_data_bits == packet_accuracy.received_data_bits &&
              signal_accuracy.corrected_data_bits == packet_accuracy.corrected_data_bits,
              "signal-browser accuracy differs from the packet decoder's actual validated bit counts");
        check(signal_accuracy.received_data_bits > 0 &&
              signal_accuracy.corrected_data_bits <= signal_accuracy.received_data_bits,
              "validated body-bit accuracy has an invalid denominator or correction count");
        if (received.diagnostics.preamble_reception) {
            const auto& preamble = *received.diagnostics.preamble_reception;
            check(preamble.expected_samples > 0 && preamble.observed_samples <= preamble.expected_samples &&
                  preamble.matched_samples <= preamble.observed_samples,
                  "measured preamble coverage and matches must stay within the expected training interval");
            check(signal->preamble_received_percent.has_value() &&
                  std::abs(*signal->preamble_received_percent - 100 * preamble.received_fraction()) < 1e-10,
                  "signal-browser preamble percentage differs from the selected receiver's actual measurement");
        } else {
            check(!signal->preamble_received_percent,
                  "unmeasured preamble reception cannot be presented as a numeric percentage");
        }
    }
}
template<class Predicate> live::Snapshot wait_for(live::Session& session, Predicate predicate,
                                                std::chrono::milliseconds timeout = 30s) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    std::string last_status, last_signal;
    double fraction = 0;
    do {
        auto snapshot = session.snapshot();
        last_status = snapshot.status; fraction = snapshot.transmission_fraction;
        if (!snapshot.signals.empty()) {
            const auto& signal = snapshot.signals.back();
            last_signal = std::to_string(signal.received_bytes) + "/" + std::to_string(signal.expected_bytes);
        }
        if (!snapshot.error.empty()) throw std::runtime_error("session failed: " + snapshot.error);
        if (predicate(snapshot)) return snapshot;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("continuous session timed out: " + last_status + "; TX " + std::to_string(fraction) + "; last prefix " + last_signal);
}
void test_idle_noise_and_plots() {
    live::Session session;
    auto value = settings();
    session.start(value);
    const auto first = wait_for(session, [](const auto& snapshot) { return snapshot.waveform.size() == 2048; });
    const auto later = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > first.sequence + 2; });
    check(later.running && later.simulation && !later.transmitting, "idle simulator status");
    check(later.samples_received > first.samples_received, "idle source clock advances");
    check(later.waveform != first.waveform, "idle noise is continuously regenerated");
    check(later.spectrum_db.size() == 1025 && later.spectrum_bin_hz == 8000.0 / 2048,
          "spectrum uses the full contiguous FFT and correct frequency axis");
    check(std::all_of(later.spectrum_db.begin(), later.spectrum_db.end(), [](auto n) { return std::isfinite(n); }),
          "noise FFT contains only finite bins");
    check(later.constellation.size() > 10, "idle constellation comes from measured baseband samples");
    check(later.constellation_source==live::ConstellationSource::input,"idle constellation is labeled measured input");
    check(std::any_of(later.constellation.begin(), later.constellation.end(), [](auto point) {
        return std::abs(std::abs(point) - 1) > 0.1;
    }), "raw constellation retains amplitude instead of normalizing every point onto a circle");
    check(later.received.empty() && later.signals.empty(), "noise is never promoted to a message");
    session.stop();
    check(!session.snapshot().running, "stop is immediately observable");
}
void test_audio_tx_publishes_fresh_payload_constellation() {
    live::Session session;
    auto value=settings();value.simulation=false;value.device="live-test-audio";
    value.transfer.modem.spreading_mode=modem::SpreadingMode::tone;
    value.transfer.modem.spreading_factor=128;
    session.start(value);
    wait_for(session,[](const auto& snapshot){return !snapshot.waveform.empty();});
    session.transmit(message(89,1024));
    const auto active=wait_for(session,[](const auto& snapshot) {
        return snapshot.transmitting && snapshot.constellation_source==live::ConstellationSource::transmitted &&
               !snapshot.constellation.empty();
    });
    check(active.constellation.size()<=2048,"real audio TX symbol batch remains bounded");
    for(const auto point:active.constellation)
        check(std::min(std::abs(std::abs(point)-.35),std::abs(std::abs(point)-.7))<1e-8,
              "real audio TX shows actual mapped symbol amplitudes");
    check(active.received.empty() && !active.simulation_replay,"TX symbols are not represented as received simulation data");
    wait_for(session,[](const auto& snapshot){return snapshot.transmission_finished;});
    const auto listening=wait_for(session,[](const auto& snapshot){return snapshot.constellation_source==live::ConstellationSource::input;});
    check(!listening.transmitting && !listening.simulation_replay,"real audio returns to live input after playback");
}
void test_audio_tx_empty_symbol_intervals_and_cancel() {
    live::Session session;
    auto value = settings(); value.simulation = false; value.device = "live-test-audio";
    value.transfer.modem.spreading_mode = modem::SpreadingMode::tone;
    value.transfer.modem.spreading_factor = 16384;
    session.start(value);
    wait_for(session, [](const auto& snapshot) { return !snapshot.waveform.empty(); });
    session.transmit(message(90, 2048));
    const auto symbol = wait_for(session, [](const auto& snapshot) {
        return snapshot.transmitting && snapshot.constellation_source == live::ConstellationSource::transmitted &&
               !snapshot.constellation.empty();
    });
    const auto between = wait_for(session, [&](const auto& snapshot) {
        check(snapshot.transmitting, "long-tone fixture ended before its empty symbol interval");
        check(snapshot.constellation_source == live::ConstellationSource::transmitted,
              "an active transmitter cannot label its PCM as received input between slow symbols");
        return snapshot.sequence > symbol.sequence && snapshot.constellation.empty();
    });
    check(between.transmission_fraction < 1 && !between.simulation_replay,
          "empty transmitted-symbol frame belongs to an unfinished real-audio transmission");
    session.cancel_transmit();
    const auto cancelled = session.snapshot();
    check(!cancelled.transmitting && cancelled.transmission_cancelled && cancelled.constellation.empty() &&
          cancelled.constellation_source == live::ConstellationSource::input,
          "cancellation immediately clears the slow transmitter's displayed symbols");
    const auto listening = wait_for(session, [&](const auto& snapshot) {
        check(!snapshot.transmitting && snapshot.constellation_source == live::ConstellationSource::input,
              "a cancelled playback callback republished stale transmitted symbols");
        return snapshot.sequence > cancelled.sequence && !snapshot.waveform.empty();
    });
    const auto resumed_at = std::chrono::steady_clock::now();
    wait_for(session, [&](const auto& snapshot) {
        check(!snapshot.transmitting && snapshot.constellation_source == live::ConstellationSource::input,
              "old TX points reappeared after live input resumed");
        return snapshot.samples_received > listening.samples_received &&
               std::chrono::steady_clock::now() - resumed_at >= 120ms;
    }, 3s);
}
void test_binary_audio_preserves_exact_bit_length() {
    live::Session session;
    auto value = settings(); value.simulation = false; value.device = "live-test-audio";
    value.transfer.compression = true; value.transfer.fec = FecMode::rs20;
    const Bytes bits{0, 0, 1};
    const auto expected = transfer::estimate_binary(bits, value.transfer);
    const auto symbol_samples = modem::symbol_sample_count(value.transfer.modem);
    check(expected.waveform_samples == symbol_samples &&
          std::abs(expected.total_seconds - modem::symbol_seconds(value.transfer.modem)) < 1e-12,
          "three raw bits occupy one physical symbol without packet framing, byte padding or training");
    session.start(value);
    wait_for(session, [](const auto& snapshot) { return !snapshot.waveform.empty(); });
    live_test_audio::played_samples = 0;
    session.transmit_bits(bits);
    const auto finished = wait_for(session, [](const auto& snapshot) { return snapshot.transmission_finished; });
    check(live_test_audio::played_samples.load() == expected.waveform_samples,
          "actual audio playback must preserve a leading-zero three-bit transmission's exact estimated sample count");
    check(!finished.transmitting && finished.transmission_fraction == 1 &&
          std::abs(finished.transmission_seconds - expected.total_seconds) < 1e-12,
          "raw audio completion reports the actual transmitted symbol duration");
    check(finished.received.empty() && finished.signals.empty() && !finished.simulation_replay,
          "raw playback cannot fabricate a received packet or simulation replay");
    const auto listening = wait_for(session, [](const auto& snapshot) {
        return snapshot.constellation_source == live::ConstellationSource::input;
    });
    check(!listening.transmitting, "raw audio completion resumes continuous reception");
}
void check_binary_signals(const live::Snapshot& snapshot, std::size_t expected_bits) {
    check(snapshot.received.empty(), "received binary bits cannot become downloadable packet content");
    for (const auto& signal : snapshot.signals) {
        check(signal.binary && !signal.validated && signal.packet_id.empty(),
              "raw reception must be identified as unverified binary data, without a packet identity");
        check(signal.received_bytes == 0 && signal.expected_bytes == 0 && signal.expected_bits == expected_bits &&
              signal.received_bits <= expected_bits,
              "raw reception reports meaningful bit counts instead of framed byte counts");
        check(!signal.preamble_received_percent && !signal.pre_fec_accuracy,
              "unframed binary reception cannot claim preamble evidence, FEC correction or validated bit accuracy");
        if (signal.text == "Receiving binary...") {
            check(!signal.complete && signal.received_bits == 0, "binary placeholder cannot claim recovered content");
        } else {
            check(signal.text.size() == std::min<std::size_t>(signal.received_bits, 4096) &&
                  std::all_of(signal.text.begin(), signal.text.end(), [](auto bit) { return bit == '0' || bit == '1'; }),
                  "binary signal text must contain only its actual recovered bits");
        }
        if (signal.complete)
            check(signal.received_bits == expected_bits, "completed binary reception must account for every meaningful bit");
    }
}
void test_binary_simulation_replay_validation_and_cancel() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    const Bytes empty, invalid{0, 2, 1}, bits{0, 0, 1};
    rejects([&] { session.transmit_bits(bits); }, "stopped session rejects binary transmission");
    auto value = settings();
    value.transfer.modem.spreading_mode = modem::SpreadingMode::tone;
    value.transfer.modem.spreading_factor = 128;
    value.transfer.fec = FecMode::rs60;
    value.simulation_snr_db = 40;
    value.content_limit = 16;
    session.start(value);
    const auto idle = wait_for(session, [](const auto& snapshot) { return !snapshot.waveform.empty(); });
    rejects([&] { session.transmit_bits(empty); }, "empty binary transmission is rejected synchronously");
    rejects([&] { session.transmit_bits(invalid); }, "binary transmission rejects elements other than zero and one");
    rejects([&] { session.transmit_bits(Bytes(17, 0)); }, "raw bit-element storage obeys the content budget");
    const auto unchanged = session.snapshot();
    check(!unchanged.transmitting && unchanged.transmission_id == idle.transmission_id && unchanged.error.empty(),
          "invalid binary input cannot mutate the transmit queue or poison the live session");
    const auto expected = transfer::estimate_binary(bits, value.transfer);
    session.transmit_bits(bits);
    const auto first = wait_for(session, [&](const auto& snapshot) {
        check_binary_signals(snapshot, bits.size());
        if (!snapshot.simulation_replay) check(snapshot.signals.empty(), "raw reception cannot appear during CPU computation");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(first.replay_frame_count > 1 && first.replay_frame_count <= 60 && first.replay_frame_index == 0 &&
          std::abs(first.transmission_seconds - expected.total_seconds) < 1e-12,
          "raw simulation replays its exact unframed duration in bounded chronological frames");
    std::uint64_t signal_id = 0, last_signal_sequence = 0;
    bool saw_pending_bits = false;
    const auto observe_pending = [&](const auto& snapshot) {
        check_binary_signals(snapshot, bits.size());
        for (const auto& signal : snapshot.signals) {
            check(!signal.complete, "binary completion must wait for the full three-second replay deadline");
            if (signal_id) check(signal.id == signal_id, "binary placeholder and recovered bits must retain a stable signal identity");
            check(signal.sequence > last_signal_sequence, "binary signal updates must arrive in media order without duplication");
            signal_id = signal.id; last_signal_sequence = signal.sequence;
            if (signal.received_bits) {
                check(std::string("001").starts_with(signal.text), "high-SNR pending binary content must be recovered correctly");
                saw_pending_bits = true;
            }
        }
    };
    observe_pending(first);
    rejects([&] { session.transmit_bits(empty); }, "empty input cannot replace an active raw replay");
    rejects([&] { session.transmit_bits(invalid); }, "invalid bit input cannot replace an active raw replay");
    const auto still_first = session.snapshot();
    observe_pending(still_first);
    check(still_first.simulation_replay && still_first.transmission_id == first.transmission_id &&
          still_first.waveform == first.waveform && still_first.replay_frame_index == 0,
          "rejected raw input leaves the active replay and its queue untouched");
    replay_milliseconds = 1500;
    const auto middle = session.snapshot();
    check(middle.simulation_replay && middle.simulation_sample_fraction > .4 && middle.simulation_sample_fraction < .6 &&
          !middle.waveform.empty() && !middle.spectrum_db.empty() && !middle.constellation.empty() &&
          middle.constellation_source == live::ConstellationSource::input,
          "raw replay shows measured waveform, spectrum and input constellation halfway through its three seconds");
    observe_pending(middle);
    replay_milliseconds = 2999;
    const auto last = session.snapshot();
    check(last.simulation_replay && last.simulation_sample_fraction > .99,
          "raw simulation remains visible through the full three-second presentation");
    observe_pending(last);
    check(saw_pending_bits, "sample-derived binary bits must be visible as pending before replay completion");
    replay_milliseconds = 3000;
    const auto completed = session.snapshot();
    check_binary_signals(completed, bits.size());
    check(completed.signals.size() == 1 && completed.signals.front().complete && completed.signals.front().text == "001" &&
          completed.signals.front().id == signal_id && completed.signals.front().sequence > last_signal_sequence,
          "raw simulation must deliver bits recovered from the channel at its presentation deadline");
    check(!completed.simulation_replay && completed.received.empty() &&
          completed.constellation_source == live::ConstellationSource::input,
          "raw simulation completes received bit text at exactly three seconds without fabricating a file");
    const auto completed_again = session.snapshot();
    check(completed_again.signals.empty() && completed_again.received.empty(), "binary completion is delivered exactly once");
    const auto noise = wait_for(session, [&](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "returning raw simulation to noise cannot validate content");
        return snapshot.samples_received > completed.samples_received && snapshot.waveform != last.waveform;
    });
    check(!noise.simulation_replay, "noise reception resumes after raw simulation presentation");
    session.transmit_bits(bits);
    const auto cancellable = wait_for(session, [&](const auto& snapshot) {
        return snapshot.transmission_finished && snapshot.simulation_replay && snapshot.transmission_id != first.transmission_id;
    });
    replay_milliseconds = 4500;
    session.cancel_transmit();
    const auto cancelled = session.snapshot();
    check(!cancelled.simulation_replay && cancelled.transmission_cancelled && !cancelled.transmitting &&
          cancelled.signals.empty() && cancelled.received.empty(),
          "raw replay can be cancelled immediately");
    replay_milliseconds = 6000;
    wait_for(session, [&](const auto& snapshot) {
        check(!snapshot.simulation_replay && snapshot.received.empty() && snapshot.signals.empty(),
              "cancelled raw replay cannot republish stale frames or fabricated packets");
        return snapshot.samples_received > cancellable.samples_received && snapshot.waveform != cancellable.waveform;
    });

    session.transmit_bits(bits);
    const auto replaceable = wait_for(session, [](const auto& snapshot) { return snapshot.transmission_finished && snapshot.simulation_replay; });
    check_binary_signals(replaceable, bits.size());
    check(!replaceable.signals.empty(), "raw replay starts with an explicit receiving placeholder");
    const auto replaced_id = replaceable.signals.front().id;
    replay_milliseconds = 8950;
    const Bytes replacement_bits{1, 0};
    session.transmit_bits(replacement_bits);
    const auto interrupted = session.snapshot();
    check(!interrupted.simulation_replay && interrupted.signals.empty() && interrupted.received.empty(),
          "replacement drops due but unpresented binary rows from the interrupted transmission");
    wait_for(session, [&](const auto& snapshot) {
        check_binary_signals(snapshot, replacement_bits.size());
        for (const auto& signal : snapshot.signals) check(signal.id != replaced_id, "replaced binary acquisition cannot leak into its successor");
        return snapshot.transmission_finished && snapshot.simulation_replay && snapshot.transmission_id != replaceable.transmission_id;
    });
    replay_milliseconds = 11950;
    const auto replacement = session.snapshot();
    check_binary_signals(replacement, replacement_bits.size());
    const auto recovered = std::find_if(replacement.signals.begin(), replacement.signals.end(), [](const auto& signal) { return signal.complete; });
    check(recovered != replacement.signals.end() && recovered->text == "10" && recovered->id != replaced_id,
          "replacement presents its own recovered bits at its own three-second deadline");
}
void test_keyed_binary_reception_preserves_partial_symbols() {
    const Bytes bits{0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0};
    for (const auto fec : {FecMode::rs20, FecMode::rs60}) {
        std::atomic<std::int64_t> replay_milliseconds{0};
        live::Session session({}, [&] {
            return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
        });
        auto value = settings();
        value.simulation_snr_db = 40;
        value.transfer.key.emplace(Bytes(32, 0x73));
        value.transfer.timestamp = 1500000000;
        value.transfer.search_seconds = 0;
        value.transfer.modem.scramble = true;
        value.transfer.modem.dsss = true;
        value.transfer.modem.spreading_factor = 3;
        value.transfer.modem.constellation_bits = fec == FecMode::rs20 ? 4U : 6U;
        value.transfer.fec = fec; value.transfer.compression = true;
        const auto expected = transfer::estimate_binary(bits, value.transfer);
        auto no_packet_controls = value.transfer;
        no_packet_controls.fec = FecMode::off; no_packet_controls.compression = false;
        check(expected.waveform_samples == transfer::estimate_binary(bits, no_packet_controls).waveform_samples,
              "selected Reed-Solomon and compression cannot add overhead to keyed raw bits");
        session.start(value); session.transmit_bits(bits);
        const auto first = wait_for(session, [&](const auto& snapshot) {
            check_binary_signals(snapshot, bits.size());
            if (!snapshot.simulation_replay) check(snapshot.signals.empty(), "keyed binary results cannot appear before replay starts");
            return snapshot.transmission_finished && snapshot.simulation_replay;
        });
        check(std::abs(first.transmission_seconds - expected.total_seconds) < 1e-12,
              "key masking and partial symbols preserve the exact raw transmission duration");
        replay_milliseconds = 2999;
        const auto pending = session.snapshot();
        check_binary_signals(pending, bits.size());
        check(std::any_of(pending.signals.begin(), pending.signals.end(), [](const auto& signal) {
            return !signal.complete && signal.text == "00010110010";
        }), "keyed binary reception must recover leading zeros and the final partial symbol before completion");
        replay_milliseconds = 3000;
        const auto completed = session.snapshot();
        check_binary_signals(completed, bits.size());
        check(!completed.simulation_replay && completed.signals.size() == 1 && completed.signals.front().complete &&
              completed.signals.front().text == "00010110010",
              "selected receive key must recover the actual binary channel samples without framing or FEC");
    }
}
void test_noisy_binary_reception_does_not_echo_transmission() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value = settings(); value.simulation_snr_db = -100;
    value.transfer.fec = FecMode::rs60;
    Bytes bits(256);
    std::string sent;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] = static_cast<std::uint8_t>((i * 7 + i / 3) & 1);
        sent += bits[i] ? '1' : '0';
    }
    session.start(value); session.transmit_bits(bits);
    wait_for(session, [&](const auto& snapshot) {
        check_binary_signals(snapshot, bits.size());
        if (!snapshot.simulation_replay) check(snapshot.signals.empty(), "noisy binary results must still follow the replay clock");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    replay_milliseconds = 3000;
    const auto completed = session.snapshot();
    check_binary_signals(completed, bits.size());
    const auto recovered = std::find_if(completed.signals.begin(), completed.signals.end(), [](const auto& signal) { return signal.complete; });
    check(!completed.simulation_replay && recovered != completed.signals.end() && recovered->text.size() == bits.size(),
          "unverified binary decisions remain visible even when the channel is too noisy for reliable reception");
    check(recovered->text != sent,
          "noise-obscured binary reception must come from measured samples, not an echo of transmitted bits");
}
void test_binary_long_symbol_uses_bounded_virtual_time() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value = settings();
    value.transfer.modem.bandwidth_hz = 1;
    value.transfer.modem.spreading_mode = modem::SpreadingMode::tone;
    value.transfer.modem.spreading_factor = 16384;
    value.transfer.modem.memory_limit = 1024;
    value.content_limit = 16;
    value.dsp_workspace_bytes = 1024 * 1024;
    value.simulation_snr_db = -30;
    const Bytes bits{0, 0, 1};
    const auto expected = transfer::estimate_binary(bits, value.transfer);
    session.start(value);
    const auto wall_start = std::chrono::steady_clock::now();
    session.transmit_bits(bits);
    const auto computed = wait_for(session, [&](const auto& snapshot) {
        check_binary_signals(snapshot, bits.size());
        if (!snapshot.simulation_replay) check(snapshot.signals.empty(), "long raw bit reception cannot bypass its visual timeline");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    }, 60s);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    check(computed.transmission_seconds > 3600 && computed.transmission_seconds > 100 * elapsed &&
          std::abs(computed.transmission_seconds - expected.total_seconds) < 1e-9,
          "one raw status symbol advances hours of exact media time at CPU speed without a fixed preamble");
    check(computed.dsp_buffered_bytes <= value.dsp_workspace_bytes && computed.replay_frame_count > 1 && computed.replay_frame_count <= 60,
          "hours-long raw binary transmission fits a one-MiB DSP workspace without allocating its waveform");
    replay_milliseconds = 3000;
    const auto completed = session.snapshot();
    check_binary_signals(completed, bits.size());
    const auto recovered = std::find_if(completed.signals.begin(), completed.signals.end(), [](const auto& signal) { return signal.complete; });
    check(!completed.simulation_replay && recovered != completed.signals.end() && recovered->text == "001",
          "hours-long raw simulation still presents its recovered bits after exactly three seconds");
}
void test_partial_back_to_back_and_resume() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value = settings();
    session.start(value);
    const auto first = message(1, 1200), second = message(2, 96);
    session.transmit(first);
    session.transmit(second);
    std::vector<Message> received;
    std::map<std::string, std::uint64_t> pending;
    bool partial_before_completion = false;
    std::map<std::string, std::uint64_t> pending_sequence;
    check(session.snapshot().transmitting, "queued transmission state is immediately observable");
    std::uint64_t previous_samples = 0;
    std::uint64_t replay_id = 0;
    std::int64_t replay_started_at = 0;
    std::size_t replays_started = 0;
    const auto done = wait_for(session, [&](const auto& snapshot) {
        check_signal_metrics(snapshot);
        check(snapshot.samples_received >= previous_samples, "source sample time never resets between transmissions");
        previous_samples = snapshot.samples_received;
        for (const auto& signal : snapshot.signals) {
            if (!signal.validated) {
                if (!signal.packet_id.empty()) {
                    if (pending.contains(signal.packet_id)) check(pending[signal.packet_id] == signal.id, "partial text keeps a stable acquisition identity");
                    pending[signal.packet_id] = signal.id;
                    pending_sequence[signal.packet_id] = signal.sequence;
                }
                if (signal.received_bytes < signal.expected_bytes && signal.text.size() < first.data.size() &&
                    !signal.text.empty()) partial_before_completion = true;
            } else if (pending.contains(signal.packet_id)) {
                check(pending[signal.packet_id] == signal.id, "verified text replaces its provisional row");
                check(pending_sequence[signal.packet_id] < signal.sequence, "pending and validated events retain their actual processing order");
            }
        }
        for (const auto& item : snapshot.received) {
            check(replay_milliseconds.load() >= replay_started_at + 3000,
                  "queued packet's verified result must wait for its complete three-second presentation");
            check(item.diagnostics.bit_rate > 0 && !item.diagnostics.constellation.empty(),
                  "received message carries actual streaming receiver diagnostics");
            check(item.packet.pre_fec_accuracy->corrected_data_bits == 0,
                  "validated no-FEC body has 100-percent data accuracy without counting corrected header bits");
            const auto final_signal = std::find_if(snapshot.signals.begin(), snapshot.signals.end(), [&](const auto& signal) {
                return signal.validated && signal.packet_id == message_id(item.packet.message);
            });
            check(final_signal->preamble_received_percent.has_value(),
                  "complete high-SNR preamble reception must be measured for the final signal-browser row");
            received.push_back(item.packet.message);
        }
        if (snapshot.simulation_replay && snapshot.transmission_id != replay_id) {
            check(received.size() == replays_started,
                  "queued simulation cannot replace the prior packet's unpresented final result");
            replay_id = snapshot.transmission_id; replay_started_at = replay_milliseconds.load(); ++replays_started;
            check(snapshot.replay_frame_index == 0,
                  "each queued simulation begins a separate three-second presentation");
        }
        if (snapshot.simulation_replay)
            replay_milliseconds = std::min(replay_started_at + 3000, replay_milliseconds.load() + 50);
        return received.size() == 2 && !snapshot.transmitting;
    }, 60s);
    check(done.transmission_fraction == 1, "completed fast transmission leaves a persistent completion marker");
    check(replays_started == 2, "both queued packets receive their own complete presentation timeline");
    check(partial_before_completion, "unvalidated text appears before the full waveform completes");
    check(received[0].id == first.id && received[0].data == first.data &&
          received[1].id == second.id && received[1].data == second.data, "consecutive sampled packets retain order and exact bytes");
    const auto resumed = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > done.sequence + 2; });
    check(resumed.samples_received > done.samples_received && resumed.waveform != done.waveform,
          "simulation returns to changing noise after transmission");
    check(resumed.received.empty(), "consumed waveform is not delivered repeatedly");
}
void test_encrypted_auto_epoch() {
    live::Session session;
    auto value = settings();
    value.transfer.compression = true;
    value.transfer.fec = FecMode::rs20;
    value.transfer.key.emplace(Bytes(32, 0x72));
    value.receive_keys.emplace_back(Bytes(32, 0x11));
    value.receive_keys.emplace_back(Bytes(32, 0x72));
    value.transfer.modem.scramble = true;
    value.transfer.modem.dsss = true;
    value.transfer.timestamp = 0;
    // Three epochs exercise automatic admission without making this semantic
    // test a full-bank throughput benchmark under sanitizers. The separate
    // three-key workspace test retains the default thirteen-epoch search.
    value.transfer.search_seconds = 1;
    session.start(value);
    const auto sent = message(3, 700);
    session.transmit(sent);
    const auto final = wait_for(session, [](const auto& snapshot) {
        check_signal_metrics(snapshot);
        return !snapshot.received.empty();
    }, 60s);
    check(final.received.front().packet.message.data == sent.data && final.received.front().packet.authenticated,
          "selected second named key verifies actual encrypted continuous audio");
    check(final.received.front().timestamp > 1000000000, "zero timestamp selects the current epoch automatically");
}
void test_simulated_epoch_admission_survives_clock_jumps() {
    constexpr std::uint64_t origin = 1800000000;
    std::atomic<std::uint64_t> queries{0};
    // Each clock query advances an hour. Re-reading wall time after encoding
    // or during a burst therefore cannot accidentally retain the right epoch.
    live::Session session([&] { return static_cast<double>(origin + 3600 * queries.fetch_add(1)); });
    auto value = settings();
    value.transfer.key.emplace(Bytes(32, 0x72));
    value.receive_keys.emplace_back(Bytes(32, 0x11));
    value.receive_keys.emplace_back(Bytes(32, 0x72));
    value.transfer.modem.scramble = value.transfer.modem.dsss = true;
    value.transfer.timestamp = 0;
    value.transfer.search_seconds = 1;
    value.transfer.compression = true;
    value.transfer.fec = FecMode::rs20;
    session.start(value);
    std::uint64_t previous_epoch = 0;
    for (const auto id : {25U, 26U}) {
        const auto sent = message(static_cast<std::uint8_t>(id), 96);
        session.transmit(sent);
        std::optional<transfer::Received> decoded;
        wait_for(session, [&](const auto& snapshot) {
            if (!snapshot.received.empty()) decoded = snapshot.received.front();
            return decoded.has_value() && snapshot.transmission_finished;
        }, 60s);
        check(decoded->packet.authenticated && decoded->packet.message.data == sent.data,
              "clock jumps during packet preparation or simulated reception discard the admitted key epoch");
        check(decoded->timestamp >= origin && decoded->timestamp > previous_epoch,
              "the next burst must admit a fresh receiver epoch after releasing its predecessor");
        previous_epoch = decoded->timestamp;
    }
    rejects([] { live::Session invalid([] { return -1.; }); }, "negative injected epoch rejected");
    rejects([] { live::Session invalid([] { return std::numeric_limits<double>::quiet_NaN(); }); }, "nonfinite injected epoch rejected");
    rejects([] { live::Session invalid([] { return static_cast<double>(std::numeric_limits<std::uint64_t>::max()); }); }, "out-of-range injected epoch rejected");
}
void test_simulation_replay_and_live_constellation() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    const auto value = settings();
    session.start(value);
    session.transmit(message(21, 96));
    const auto first = wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(),
              "CPU-bound simulation cannot publish reception before the visual timeline starts");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(first.simulation_replay && first.transmission_id != 0 && first.replay_frame_index == 0,
          "CPU-bound simulation finishes while its three-second replay is still on the first frame");
    check(first.replay_frame_count == 60 && first.simulation_sample_fraction < .02,
          "replay begins at preamble start and retains sixty bounded media frames");
    check(first.constellation_source == live::ConstellationSource::input,
          "initial preamble frame cannot borrow receiver lock from the completed simulation");
    check(first.dsp_buffered_bytes <= value.dsp_workspace_bytes, "replay storage is included in the configured DSP workspace");
    replay_milliseconds = 49;
    const auto before_tick = session.snapshot();
    check(before_tick.replay_frame_index == 0 && before_tick.waveform == first.waveform &&
          before_tick.constellation == first.constellation,
          "a replay frame remains visible for a complete fifty-millisecond GUI interval");
    const auto background = wait_for(session, [&](const auto& snapshot) {
        return snapshot.samples_received > first.samples_received;
    });
    check(background.simulation_replay && background.replay_frame_index == 0 && background.waveform == first.waveform,
          "background noise continues without advancing or overwriting the presentation clock");
    check(background.signals.empty() && background.received.empty(), "background reception cannot release future simulation events");

    auto previous = first;
    bool observed_lock = false, observed_fresh_symbols = false, changed_waveform = false, changed_spectrum = false;
    bool observed_pending = false;
    std::uint64_t pending_id = 0, last_pending_sequence = 0;
    std::uint64_t points_after_frame_40 = 0;
    const auto symbols = first.transmission_seconds * value.transfer.modem.sample_rate /
                         static_cast<double>(modem::symbol_sample_count(value.transfer.modem));
    const auto symbols_per_frame = static_cast<std::size_t>(std::ceil(symbols / 59)) + 3;
    const auto training_fraction = 5 / first.transmission_seconds;
    for (std::size_t index = 1; index < 60; ++index) {
        replay_milliseconds = static_cast<std::int64_t>(index * 50);
        const auto frame = session.snapshot();
        check(frame.simulation_replay && frame.replay_frame_count == 60 && frame.replay_frame_index == index,
              "replay advances at twenty frames per wall-clock second");
        check(frame.simulation_sample_fraction >= previous.simulation_sample_fraction &&
              frame.simulation_sample_fraction <= 1,
              "replay media positions are chronological and never enter decoder-tail silence");
        check(std::abs(frame.simulation_sample_fraction - static_cast<double>(index) / 59) < .02,
              "replay frames sample the whole preamble and payload uniformly");
        check(!frame.waveform.empty() && !frame.spectrum_db.empty() &&
              frame.spectrum_bin_hz > 0 && frame.dsp_buffered_bytes <= value.dsp_workspace_bytes,
              "every replay frame carries measured plots within the DSP budget");
        check(std::abs(static_cast<double>(frame.spectrum_db.size() - 1) * frame.spectrum_bin_hz -
                       value.transfer.modem.sample_rate / 2.) < frame.spectrum_bin_hz,
              "compact replay spectra preserve the original Nyquist frequency axis");
        changed_waveform = changed_waveform || frame.waveform != previous.waveform;
        changed_spectrum = changed_spectrum || frame.spectrum_db != previous.spectrum_db;
        check(frame.received.empty(), "received files and messages cannot appear before the three-second presentation deadline");
        check_signal_metrics(frame);
        if (frame.simulation_sample_fraction + .02 < training_fraction)
            check(frame.signals.empty(), "signal-browser content cannot appear while only the preamble has been presented");
        for (const auto& signal : frame.signals) {
            check(!signal.validated, "verified text cannot precede the last visual frame and completion deadline");
            check(!signal.text.empty() && signal.sequence > last_pending_sequence,
                  "pending reception needs ordered, visible signal-browser updates");
            if (pending_id) check(signal.id == pending_id, "header placeholder and partial text must retain one acquisition identity");
            pending_id = signal.id; last_pending_sequence = signal.sequence; observed_pending = true;
        }
        if (index > 40) points_after_frame_40 += frame.constellation.size() + frame.constellation_dropped;
        if (frame.constellation_source == live::ConstellationSource::received && !frame.constellation.empty()) {
            if (observed_lock) {
                check(frame.constellation.size() <= symbols_per_frame,
                      "later replay frames contain fresh symbols instead of accumulating receiver history");
                if (frame.constellation != previous.constellation) observed_fresh_symbols = true;
            }
            observed_lock = true;
        }
        const auto same_frame = session.snapshot();
        check(same_frame.replay_frame_index == frame.replay_frame_index &&
              same_frame.constellation == frame.constellation && same_frame.waveform == frame.waveform,
              "reading a frame twice does not drain symbols before they can be displayed");
        check(same_frame.signals.empty() && same_frame.received.empty(), "repeated replay snapshots cannot duplicate reception events");
        previous = frame;
    }
    check(observed_lock && observed_fresh_symbols, "successful replay shows actual lock followed by fresh measured symbol batches");
    check(observed_pending, "pending reception is visible for at least one frame before validation");
    check(changed_waveform && changed_spectrum, "replay animates measured waveform and spectrum samples across the transmission");
    check(previous.simulation_sample_fraction > .99, "last replay frame reaches the end of the complete transmission");
    double final_power = 0;
    for (const auto sample : previous.waveform) final_power += sample * sample;
    check(final_power / static_cast<double>(previous.waveform.size()) > .02,
          "last high-SNR replay waveform includes transmitted signal rather than decoder-tail noise");
    replay_milliseconds = 2999;
    const auto last = session.snapshot();
    check(last.simulation_replay && last.replay_frame_index == 59 && last.constellation == previous.constellation,
          "last payload frame remains visible until the full three-second deadline");
    check(last.received.empty() && std::none_of(last.signals.begin(), last.signals.end(), [](const auto& signal) { return signal.validated; }),
          "verified reception is withheld through 2999ms even though computation finished earlier");
    replay_milliseconds = 3000;
    const auto resumed = session.snapshot();
    check(!resumed.simulation_replay && resumed.constellation_source == live::ConstellationSource::input,
          "exactly three seconds releases replay and returns to incoming live samples");
    check(resumed.received.size() == 1 && resumed.received.front().packet.message.id == message(21, 96).id,
          "exactly three seconds delivers the completed received packet");
    check_signal_metrics(resumed);
    const auto verified = std::find_if(resumed.signals.begin(), resumed.signals.end(), [](const auto& signal) { return signal.validated; });
    check(verified != resumed.signals.end() && verified->id == pending_id && verified->sequence > last_pending_sequence,
          "final validated signal row arrives with its packet and replaces the pending identity");
    const auto completed_again = session.snapshot();
    check(completed_again.signals.empty() && completed_again.received.empty(), "completion events are delivered exactly once");
    const auto live_again = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > resumed.sequence; });
    check(live_again.waveform != last.waveform && live_again.constellation != last.constellation,
          "new receiver points keep replacing the completed simulation");

    // Repeat the same seeded transmission: the observed frame point counts
    // above provide an independent oracle for an otherwise invisible tail.
    session.transmit(message(21, 96));
    const auto stalled_start = wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "repeated simulation must defer its new reception events");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    replay_milliseconds = 5000;
    const auto before_stall = session.snapshot();
    check(before_stall.replay_frame_index == 40 && points_after_frame_40 > 0,
          "stalled-GUI fixture must leave real symbol points in the unseen final frames");
    wait_for(session, [&](const auto& snapshot) { return snapshot.samples_received > stalled_start.samples_received; });
    replay_milliseconds = 6000;
    const auto after_stall = session.snapshot();
    check(!after_stall.simulation_replay && after_stall.replay_frame_count == 0 &&
          after_stall.constellation_source == live::ConstellationSource::input,
          "a GUI that misses the final replay frames still returns to live input at exactly three seconds");
    check(after_stall.constellation_dropped >= points_after_frame_40,
          "replay expiry reports every symbol point in the frames the GUI never displayed");
    check(after_stall.waveform != before_stall.waveform && after_stall.constellation != before_stall.constellation,
          "expired replay points cannot remain over the live waveform after a GUI stall");
    check(after_stall.received.size() == 1 && after_stall.signals.size() >= 2 &&
          !after_stall.signals.front().validated && after_stall.signals.back().validated,
          "a GUI stall flushes pending reception before the final packet without extending the deadline");
    check(std::is_sorted(after_stall.signals.begin(), after_stall.signals.end(), [](const auto& a, const auto& b) {
        return a.sequence < b.sequence;
    }), "missed signal-browser updates preserve their original order");
    check_signal_metrics(after_stall);
    const auto stalled_again = session.snapshot();
    check(stalled_again.signals.empty() && stalled_again.received.empty(), "missed-frame catch-up events are not delivered twice");

    const auto next_started_at = replay_milliseconds.load();
    session.transmit(message(22, 96));
    const auto next = session.snapshot();
    check(!next.simulation_replay, "a new transmission immediately releases the preceding replay");
    const auto next_done = wait_for(session, [&](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "new simulation results remain hidden at replay start");
        return snapshot.transmission_finished && snapshot.transmission_id != first.transmission_id;
    });
    check(next_done.simulation_replay && next_done.replay_frame_index == 0,
          "consecutive simulation starts its own three-second replay timeline");
    replay_milliseconds = next_started_at + 2900;
    const auto skipped = session.snapshot();
    check(skipped.simulation_replay && skipped.replay_frame_index == 58 &&
          skipped.constellation_source == live::ConstellationSource::received &&
          (skipped.constellation.size() > symbols_per_frame || skipped.constellation_dropped > 0),
          "a delayed GUI merges fresh symbols from skipped frames or reports their bounded overflow");
    const auto skipped_again = session.snapshot();
    check(skipped_again.constellation == skipped.constellation && skipped_again.constellation_dropped == skipped.constellation_dropped,
          "merged symbols survive repeated reads within their display interval");
    session.cancel_transmit();
    const auto cancelled_replay = session.snapshot();
    check(!cancelled_replay.simulation_replay && cancelled_replay.constellation_source == live::ConstellationSource::input,
          "cancel during replay immediately releases all retained constellation state");
    replay_milliseconds = next_started_at + 3000;
    const auto after_cancel=wait_for(session,[&](const auto& snapshot){return snapshot.sequence>cancelled_replay.sequence;});
    check(!after_cancel.constellation.empty() && after_cancel.constellation_source==live::ConstellationSource::input,
          "cancelled replay continues publishing measured input points without restoring old frames");
    check(after_cancel.signals.empty() && after_cancel.received.empty(),
          "cancelling a presentation discards its unpresented validation and received packet");

    session.transmit(message(23, 32));
    const auto third = wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "interrupted-result fixture must begin with unpresented events");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    session.transmit(message(27, 32));
    check(!session.snapshot().simulation_replay, "transmit interrupts an active replay without waiting for its deadline");
    wait_for(session, [&](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "an interrupted packet cannot leak while its successor is computing");
        return snapshot.transmission_finished && snapshot.simulation_replay && snapshot.transmission_id != third.transmission_id;
    });
    replay_milliseconds = next_started_at + 6000;
    const auto replacement = session.snapshot();
    check(replacement.received.size() == 1 && replacement.received.front().packet.message.id == message(27, 32).id,
          "a new explicit transmission discards the interrupted packet and presents only its replacement");
    check_signal_metrics(replacement);
    check(std::none_of(replacement.signals.begin(), replacement.signals.end(), [&](const auto& signal) {
        return signal.packet_id == message_id(message(23, 32));
    }), "interrupted signal-browser rows cannot reappear with the replacement packet");
    session.transmit(message(28, 32));
    wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "configuration fixture must retain unpresented events");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    session.configure(value);
    check(!session.snapshot().simulation_replay, "configuration clears an active replay immediately");
    replay_milliseconds = next_started_at + 9000;
    const auto configured = wait_for(session, [](const auto& snapshot) { return !snapshot.waveform.empty(); });
    check(!configured.simulation_replay && configured.constellation_source == live::ConstellationSource::input,
          "a new configuration cannot republish frames from its predecessor");
    check(configured.signals.empty() && configured.received.empty(), "configuration drops unpresented reception events");
}
void test_default_crystal_simulation() {
    auto value=settings();
    const live::Settings defaults;
    check(defaults.simulation_clock_error_ppm==100 && defaults.simulation_phase_noise_degrees_per_sqrt_second>0,
          "live simulation defaults must include a bad crystal and phase noise");
    value.simulation_clock_error_ppm=defaults.simulation_clock_error_ppm;
    value.simulation_phase_noise_degrees_per_sqrt_second=defaults.simulation_phase_noise_degrees_per_sqrt_second;
    value.transfer.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    value.transfer.fec=FecMode::rs20;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    session.start(value);
    const auto sent=message(24,96); session.transmit(sent);
    const auto held=wait_for(session,[](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "impaired channel must defer reception until its presentation advances");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(held.replay_frame_count > 0,"impaired simulation must publish its measured replay");
    replay_milliseconds = 3000;
    const auto decoded=wait_for(session,[](const auto& snapshot) { return !snapshot.received.empty(); });
    check_signal_metrics(decoded);
    check(decoded.received.front().packet.message.data==sent.data,"default impaired channel failed its ordinary-bandwidth packet");
    check(!decoded.simulation_replay, "impaired channel releases verified reception at the same presentation deadline");
}
void test_interrupt_discards_due_unpresented_reception() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    session.start(settings());
    const auto await_replay = [&](std::uint64_t previous_id) {
        return wait_for(session, [&](const auto& snapshot) {
            check(snapshot.signals.empty() && snapshot.received.empty(),
                  "interrupted replay cannot leak due but unpresented reception into its successor");
            return snapshot.transmission_finished && snapshot.simulation_replay && snapshot.transmission_id != previous_id;
        });
    };
    session.transmit(message(29, 32));
    const auto cancelled = await_replay(0);
    // Do not snapshot here: pending text is due, but the GUI has not consumed it.
    replay_milliseconds = 2950;
    session.cancel_transmit();
    const auto after_cancel = session.snapshot();
    check(!after_cancel.simulation_replay && after_cancel.signals.empty() && after_cancel.received.empty(),
          "cancel drops due pending rows that the GUI never presented");

    session.transmit(message(30, 32));
    const auto replaced = await_replay(cancelled.transmission_id);
    replay_milliseconds = 5900;
    session.transmit(message(31, 32));
    const auto after_replace = session.snapshot();
    check(!after_replace.simulation_replay && after_replace.signals.empty() && after_replace.received.empty(),
          "explicit transmission replaces due but unpresented rows together with the old replay");
    await_replay(replaced.transmission_id);
    replay_milliseconds = 8900;
    // Once the full deadline has elapsed, an interrupt must preserve reception.
    session.cancel_transmit();
    const auto finished = session.snapshot();
    check(finished.received.size() == 1 && finished.received.front().packet.message.id == message(31, 32).id,
          "cancel after the deadline preserves the completed replacement packet");
    check_signal_metrics(finished);
    check(std::none_of(finished.signals.begin(), finished.signals.end(), [&](const auto& signal) {
        return signal.packet_id == message_id(message(29, 32)) || signal.packet_id == message_id(message(30, 32));
    }), "interrupted pending rows cannot reappear with the replacement's final reception");
    const auto again = session.snapshot();
    check(again.signals.empty() && again.received.empty(), "deadline completion retained across cancel is still delivered exactly once");
}
void test_receive_authentication_policy() {
    auto value = settings();
    check(value.permits_plaintext(), "unkeyed channel permits plain packets");
    value.receive_keys.emplace_back(Bytes(32, 0x72));
    check(value.permits_plaintext(), "unkeyed channel may additionally search loaded keys");
    value.transfer.key.emplace(Bytes(32, 0x72));
    check(!value.permits_plaintext(), "selected key forbids unkeyed fallback even with ordinary modulation");
    value.transfer.key.reset();
    value.transfer.modem.dsss = true;
    check(!value.permits_plaintext(), "key-dependent DSSS forbids unkeyed fallback");
    value.transfer.modem.dsss = false; value.transfer.modem.scramble = true;
    check(!value.permits_plaintext(), "key-dependent scrambling forbids unkeyed fallback");
}
void test_default_workspace_holds_three_long_keyed_banks() {
    live::Session session;
    auto value = settings();
    value.transfer.modem.spreading_factor = 16384;
    value.transfer.modem.scramble = true;
    value.transfer.modem.dsss = true;
    value.transfer.key.emplace(Bytes(32, 0x31));
    value.receive_keys.emplace_back(Bytes(32, 0x32));
    value.receive_keys.emplace_back(Bytes(32, 0x33));
    session.start(value);
    const auto result = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 2; });
    check(result.dsp_buffered_bytes > 0 && result.dsp_buffered_bytes <= value.dsp_workspace_bytes,
          "default DSP workspace admits three keys across thirteen epochs with long keyed patterns");
    check(result.received.empty(), "a loaded key collection never turns noise into validated content");
}
void test_encrypted_epoch_bank_refreshes_while_idle() {
    live::Session session;
    auto value = settings();
    value.transfer.key.emplace(Bytes(32, 0x39));
    value.transfer.search_seconds = 1;
    session.start(value);
    const auto began = std::chrono::steady_clock::now();
    wait_for(session, [&](const auto&) { return std::chrono::steady_clock::now() - began > 2200ms; }, 4s);
    const auto sent = message(10, 64);
    session.transmit(sent);
    const auto received = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); }, 60s);
    check(received.received.front().packet.authenticated && received.received.front().packet.message.data == sent.data,
          "an idle encrypted listener refreshes epochs after its initial timing window expires");
}
void test_cancel_reconfigure_and_bounds() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    rejects([&] { session.transmit(message(4, 1)); }, "stopped session rejects transmit");
    auto invalid = settings();
    invalid.dsp_workspace_bytes = 1;
    rejects([&] { session.start(invalid); }, "insufficient explicit DSP workspace rejected");
    invalid = settings(); invalid.simulation_snr_db = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { session.start(invalid); }, "nonfinite simulation SNR rejected");
    invalid = settings(); invalid.simulation = false;
    invalid.transfer.modem = tuning::resolve(30000000,100,tuning::PatternMode::auto_tone,false).config;
    rejects([&] { session.start(invalid); }, "30MHz plans cannot start an unusable audio upsampler");
    auto value = settings();
    session.start(value);
    session.transmit(message(5, 120000));
    wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "computation keeps pending and verified reception unpublished");
        return snapshot.transmitting && snapshot.transmission_fraction > 0 && snapshot.transmission_fraction < 1;
    });
    const auto before = std::chrono::steady_clock::now();
    session.cancel_transmit();
    check(std::chrono::steady_clock::now() - before < 100ms, "cancel does not wait for a full capture or transmission");
    const auto cancelled = session.snapshot();
    check(!cancelled.transmitting && cancelled.transmission_finished && cancelled.transmission_cancelled,
          "cancelled transmission has an immediately observable terminal marker");
    replay_milliseconds = 3000;
    const auto resumed = wait_for(session, [&](const auto& snapshot) {
        check(!snapshot.simulation_replay && snapshot.signals.empty() && snapshot.received.empty(),
              "a cancelled computation cannot publish a late presentation or reception event");
        return snapshot.sequence > cancelled.sequence;
    });
    check(!resumed.transmitting, "cancelled computation returns to idle input processing");
    value.simulation_seed = 23;
    value.receive_buffer_seconds = 0.001; // Obsolete duration has no DSP significance.
    session.configure(value);
    const auto sent = message(6, 700);
    session.transmit(sent);
    wait_for(session, [](const auto& snapshot) {
        check(snapshot.signals.empty() && snapshot.received.empty(), "reconfiguration cannot restore cancelled reception events");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    replay_milliseconds = 6000;
    const auto final = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); });
    check_signal_metrics(final);
    check(final.received.front().packet.message.id == sent.id && final.received.front().packet.message.data == sent.data,
          "fresh configuration recovers after cancellation without retaining an airtime-sized window");
    check(final.dsp_buffered_bytes <= value.dsp_workspace_bytes, "streaming DSP workspace stays independently bounded");
    const auto stopped = std::chrono::steady_clock::now();
    session.stop();
    check(std::chrono::steady_clock::now() - stopped < 100ms, "stop enqueues cancellation promptly");
}
void test_unrecoverable_noise_does_not_validate() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({}, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value = settings(); value.simulation_snr_db = -80;
    session.start(value); session.transmit(message(7, 10));
    const auto finished = wait_for(session, [&](const auto& snapshot) {
        check_signal_metrics(snapshot);
        check(snapshot.received.empty(), "noise-obscured transmission never bypasses the modem");
        check(std::none_of(snapshot.signals.begin(), snapshot.signals.end(), [](const auto& signal) { return signal.validated; }),
              "unrecoverable samples never produce verified ticker text");
        return snapshot.transmission_fraction == 1 && !snapshot.transmitting;
    });
    check(finished.simulation_replay && finished.replay_frame_count == 60,
          "failed reception still replays the measured noisy transmission");
    for (std::size_t index = 0; index < 60; ++index) {
        replay_milliseconds = static_cast<std::int64_t>(index * 50);
        const auto frame = session.snapshot();
        check_signal_metrics(frame);
        check(frame.simulation_replay && frame.constellation_source == live::ConstellationSource::input && frame.received.empty(),
              "noise-obscured replay never borrows transmitter symbols or claims receiver lock");
    }
    replay_milliseconds = 3000;
    const auto completed = session.snapshot();
    check_signal_metrics(completed);
    check(!completed.simulation_replay && completed.received.empty() &&
          std::none_of(completed.signals.begin(), completed.signals.end(), [](const auto& signal) { return signal.validated; }),
          "the presentation deadline cannot turn failed reception into a verified packet");
}
void test_weak_and_wide_modes_keep_the_channel_running() {
    live::Session session;
    auto value = settings();
    value.transfer.modem = tuning::resolve(1200, 6, tuning::PatternMode::auto_pattern, false).config;
    session.start(value);
    auto previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    auto current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(current.samples_received > previous.samples_received && current.waveform != previous.waveform,
          "long-integration weak mode still produces continuous samples and actual changing plots");
    check(current.received.empty(), "noise-only weak acquisition makes no receive claim");
    value.transfer.modem = tuning::resolve(192000, 120, tuning::PatternMode::auto_pattern, false).config;
    session.configure(value);
    previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(current.samples_received > previous.samples_received && current.waveform != previous.waveform,
          "384k sample rate retains bounded blocks while wideband noise and plots continue");
    check(current.dsp_buffered_bytes <= value.dsp_workspace_bytes, "wideband DSP workspace stays within its independent budget");
    value.transfer.modem = tuning::resolve(1, 6, tuning::PatternMode::auto_pattern, false).config;
    session.configure(value);
    previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(!current.constellation.empty() && current.constellation != previous.constellation,
          "one-hertz mode retains changing measured partial-chip constellation points");
}
void test_long_symbols_are_streamed_in_virtual_time() {
    for (const auto factor : {1024U, 16384U}) {
        std::atomic<std::int64_t> replay_milliseconds{0};
        live::Session session({}, [&] {
            return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
        });
        auto value = settings();
        value.transfer.modem.bandwidth_hz = 1;
        value.transfer.modem.spreading_mode = modem::SpreadingMode::tone;
        value.transfer.modem.spreading_factor = factor;
        // The obsolete monolithic allocation limit and ring duration cannot
        // prevent an independently bounded stream from processing this packet.
        value.transfer.modem.memory_limit = 1024;
        value.receive_buffer_seconds = 0.01;
        value.content_limit = 1024;
        value.dsp_workspace_bytes = 1024 * 1024;
        value.simulation_snr_db = -30;
        const auto sent = message(static_cast<std::uint8_t>(factor == 1024 ? 8 : 9), 96);
        session.start(value);
        const auto wall_start = std::chrono::steady_clock::now();
        session.transmit(sent);
        const auto result = wait_for(session, [](const auto& snapshot) {
            check(snapshot.signals.empty() && snapshot.received.empty(),
                  "hours-long virtual reception cannot bypass its bounded visual presentation");
            return snapshot.transmission_finished && snapshot.simulation_replay;
        }, 60s);
        const auto elapsed = std::chrono::steady_clock::now() - wall_start;
        check(result.transmission_seconds > 3600, "long symbols actually advance hours of virtual media");
        check(result.transmission_seconds > std::chrono::duration<double>(elapsed).count() * 100,
              "accelerated simulation is driven by DSP work rather than wall-clock airtime");
        check(result.dsp_buffered_bytes <= value.dsp_workspace_bytes, "long virtual duration does not grow DSP buffers");
        check(result.simulation_replay && result.replay_frame_count > 1 && result.replay_frame_count <= 60,
              "one-MiB weak-mode budget retains a bounded replay without storing hours of samples");
        replay_milliseconds = 3000;
        const auto decoded = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); });
        check_signal_metrics(decoded);
        check(!decoded.simulation_replay && decoded.received.front().packet.message.data == sent.data,
              "long-tone sampled statistics decode real bytes and present them after exactly three seconds");
        const auto content_seconds = static_cast<double>(decoded.received.front().packet.consumed_bytes) * 8 /
                                     modem::bit_rate(value.transfer.modem);
        check(std::abs(result.transmission_seconds - content_seconds - 5) < 1e-6,
              "training remains exactly five seconds even when a data symbol lasts hours");
    }
}
}
int main() {
    try {
        const auto run = [](const char* name, auto test) {
            try { test(); } catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
        };
        run("idle noise and plots", test_idle_noise_and_plots);
        run("actual audio TX constellation", test_audio_tx_publishes_fresh_payload_constellation);
        run("actual audio empty symbols and cancel", test_audio_tx_empty_symbol_intervals_and_cancel);
        run("binary audio exact bit length", test_binary_audio_preserves_exact_bit_length);
        run("binary simulation replay and cancellation", test_binary_simulation_replay_validation_and_cancel);
        run("keyed binary partial-symbol reception", test_keyed_binary_reception_preserves_partial_symbols);
        run("noisy binary is sample-derived", test_noisy_binary_reception_does_not_echo_transmission);
        run("binary long symbol bounded simulation", test_binary_long_symbol_uses_bounded_virtual_time);
        run("partial back-to-back reception", test_partial_back_to_back_and_resume);
        run("encrypted automatic epoch", test_encrypted_auto_epoch);
        run("simulated epoch admission across clock jumps", test_simulated_epoch_admission_survives_clock_jumps);
        run("simulation replay", test_simulation_replay_and_live_constellation);
        run("default crystal", test_default_crystal_simulation);
        run("unpresented replay interruption", test_interrupt_discards_due_unpresented_reception);
        run("receive authentication policy", test_receive_authentication_policy);
        run("three long keyed banks", test_default_workspace_holds_three_long_keyed_banks);
        run("idle epoch refresh", test_encrypted_epoch_bank_refreshes_while_idle);
        run("cancel, reconfigure and bounds", test_cancel_reconfigure_and_bounds);
        run("unrecoverable noise", test_unrecoverable_noise_does_not_validate);
        run("weak and wide modes", test_weak_and_wide_modes_keep_the_channel_running);
        run("long symbols in virtual time", test_long_symbols_are_streamed_in_virtual_time);
        std::cout << "Continuous receiver and simulation tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; return 1;
    }
}
