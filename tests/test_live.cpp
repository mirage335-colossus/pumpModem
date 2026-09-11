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
    // These cases isolate transport, scheduling and UI review. Oscillator
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
void test_audio_tx_accumulates_payload_constellation() {
    live::Session session;
    auto value=settings();value.simulation=false;value.device="live-test-audio";
    value.transfer.modem.spreading_mode=modem::SpreadingMode::tone;
    value.transfer.modem.spreading_factor=128;
    session.start(value);
    wait_for(session,[](const auto& snapshot){return !snapshot.waveform.empty();});
    session.transmit(message(89,1024));
    const auto active=wait_for(session,[](const auto& snapshot) {
        return snapshot.transmitting && snapshot.constellation_source==live::ConstellationSource::transmitted &&
               snapshot.constellation.size()>32;
    });
    check(active.constellation.size()<=2048,"real audio TX history remains bounded");
    check(active.constellation.size()*modem::symbol_sample_count(value.transfer.modem)>active.waveform.size()*16,
          "real audio TX displays a history of slow payload symbols beyond its short PCM preview");
    for(const auto point:active.constellation)
        check(std::min(std::abs(std::abs(point)-.35),std::abs(std::abs(point)-.7))<1e-8,
              "real audio TX shows actual mapped symbol amplitudes");
    check(active.received.empty() && !active.simulation_review,"TX history is not represented as received simulation data");
    wait_for(session,[](const auto& snapshot){return snapshot.transmission_finished;});
    const auto listening=wait_for(session,[](const auto& snapshot){return snapshot.constellation_source==live::ConstellationSource::input;});
    check(!listening.transmitting && !listening.simulation_review,"real audio returns to live input after playback");
}
void test_partial_back_to_back_and_resume() {
    live::Session session;
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
    const auto done = wait_for(session, [&](const auto& snapshot) {
        check(snapshot.samples_received >= previous_samples, "source sample time never resets between transmissions");
        previous_samples = snapshot.samples_received;
        for (const auto& signal : snapshot.signals) {
            if (!signal.validated) {
                if (pending.contains(signal.packet_id)) check(pending[signal.packet_id] == signal.id, "partial text keeps a stable acquisition identity");
                pending[signal.packet_id] = signal.id;
                pending_sequence[signal.packet_id] = signal.sequence;
                if (signal.received_bytes < signal.expected_bytes && signal.text.size() < first.data.size() &&
                    !signal.text.empty()) partial_before_completion = true;
            } else if (pending.contains(signal.packet_id)) {
                check(pending[signal.packet_id] == signal.id, "verified text replaces its provisional row");
                check(pending_sequence[signal.packet_id] < signal.sequence, "pending and validated events retain their actual processing order");
            }
        }
        for (const auto& item : snapshot.received) {
            check(item.diagnostics.bit_rate > 0 && !item.diagnostics.constellation.empty(),
                  "received message carries actual streaming receiver diagnostics");
            received.push_back(item.packet.message);
        }
        return received.size() == 2 && !snapshot.transmitting;
    }, 60s);
    check(done.transmission_fraction == 1, "completed fast transmission leaves a persistent completion marker");
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
    const auto final = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); }, 60s);
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
void test_simulation_review_and_live_constellation() {
    live::Session session;
    const auto value = settings();
    session.start(value);
    session.transmit(message(21, 96));
    const auto held = wait_for(session, [](const auto& snapshot) {
        return snapshot.transmission_finished && snapshot.transmission_fraction == 1;
    });
    check(held.simulation_review && held.transmission_id != 0, "completed simulation publishes a persistent review frame");
    check(held.simulation_sample_fraction >= .5 && held.simulation_sample_fraction < .52,
          "review waveform comes from the payload midpoint, not trailing silence");
    check(held.simulation_waterfall.size() == 24 && held.simulation_waterfall.front().size() == 257,
          "fast simulation retains a bounded spectrum history independent of GUI polling");
    check(held.simulation_waterfall_bin_hz == value.transfer.modem.sample_rate / 512.0,
          "review waterfall retains the correct frequency scale");
    check(held.constellation_retained && held.constellation.size() > 100,
          "review contains accumulated received symbol points");
    check(held.constellation_source==live::ConstellationSource::received,"simulation review identifies actual receiver observations");
    check(held.constellation.size() <= 2048, "retained constellation stays bounded");
    std::this_thread::sleep_for(100ms);
    const auto during = session.snapshot();
    check(during.simulation_review && during.sequence == held.sequence &&
          during.waveform == held.waveform && during.spectrum_db == held.spectrum_db &&
          during.constellation == held.constellation, "all review plots remain frozen while background reception continues");
    check(during.samples_received > held.samples_received, "review does not pause the continuous receiver");
    const auto resumed = wait_for(session, [](const auto& snapshot) { return !snapshot.simulation_review; }, 3s);
    check(resumed.waveform != held.waveform && resumed.spectrum_db != held.spectrum_db,
          "waveform and spectrum return to live samples after two seconds");
    check(!resumed.constellation_retained && resumed.constellation != held.constellation,
          "constellation returns to the incoming live signal after two seconds");
    check(resumed.constellation_source==live::ConstellationSource::input,"expired review returns its source label to live input");
    const auto live_again = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > resumed.sequence; });
    check(live_again.constellation != resumed.constellation,
          "new receiver points keep replacing the completed simulation");
    session.transmit(message(22, 96));
    const auto next = session.snapshot();
    check(!next.simulation_review && !next.constellation_retained,
          "a new transmission immediately releases the preceding review");
    const auto next_done = wait_for(session, [&](const auto& snapshot) {
        return snapshot.transmission_finished && snapshot.transmission_id != held.transmission_id;
    });
    check(next_done.simulation_review, "consecutive simulation receives its own review identity");
    session.cancel_transmit();
    const auto cancelled_review=session.snapshot();
    check(!cancelled_review.simulation_review && !cancelled_review.constellation_retained &&
          cancelled_review.constellation_source==live::ConstellationSource::input,
          "cancel during a completed review releases all retained constellation state");
    const auto after_cancel=wait_for(session,[&](const auto& snapshot){return snapshot.sequence>cancelled_review.sequence;});
    check(!after_cancel.constellation.empty() && after_cancel.constellation_source==live::ConstellationSource::input,
          "cancelled review continues publishing measured input points");
    session.configure(value);
    check(!session.snapshot().constellation_retained, "configuration clears the old constellation");
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
    live::Session session; session.start(value);
    const auto sent=message(24,96); session.transmit(sent);
    const auto decoded=wait_for(session,[](const auto& snapshot) { return !snapshot.received.empty(); });
    check(decoded.received.front().packet.message.data==sent.data,"default impaired channel failed its ordinary-bandwidth packet");
    const auto held=wait_for(session,[](const auto& snapshot) { return snapshot.transmission_finished; });
    check(held.simulation_review && held.constellation_retained,"impaired simulation must publish its measured review");
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
    live::Session session;
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
    const auto before = std::chrono::steady_clock::now();
    session.cancel_transmit();
    check(std::chrono::steady_clock::now() - before < 100ms, "cancel does not wait for a full capture or transmission");
    const auto cancelled = session.snapshot();
    check(!cancelled.transmitting && cancelled.transmission_finished && cancelled.transmission_cancelled,
          "cancelled transmission has an immediately observable terminal marker");
    value.simulation_seed = 23;
    value.receive_buffer_seconds = 0.001; // Obsolete duration has no DSP significance.
    session.configure(value);
    const auto sent = message(6, 700);
    session.transmit(sent);
    const auto final = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); });
    check(final.received.front().packet.message.id == sent.id && final.received.front().packet.message.data == sent.data,
          "fresh configuration recovers after cancellation without retaining an airtime-sized window");
    check(final.dsp_buffered_bytes <= value.dsp_workspace_bytes, "streaming DSP workspace stays independently bounded");
    const auto stopped = std::chrono::steady_clock::now();
    session.stop();
    check(std::chrono::steady_clock::now() - stopped < 100ms, "stop enqueues cancellation promptly");
}
void test_unrecoverable_noise_does_not_validate() {
    live::Session session;
    auto value = settings(); value.simulation_snr_db = -80;
    session.start(value); session.transmit(message(7, 10));
    wait_for(session, [&](const auto& snapshot) {
        check(snapshot.received.empty(), "noise-obscured transmission never bypasses the modem");
        check(std::none_of(snapshot.signals.begin(), snapshot.signals.end(), [](const auto& signal) { return signal.validated; }),
              "unrecoverable samples never produce verified ticker text");
        return snapshot.transmission_fraction == 1 && !snapshot.transmitting;
    });
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
        live::Session session;
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
        std::optional<transfer::Received> decoded;
        const auto result = wait_for(session, [&](const auto& snapshot) {
            if (!snapshot.received.empty()) decoded = snapshot.received.front();
            return decoded.has_value() && snapshot.transmission_finished;
        }, 60s);
        const auto elapsed = std::chrono::steady_clock::now() - wall_start;
        check(decoded->packet.message.data == sent.data, "long-tone sampled statistics decode the real packet bytes");
        check(result.transmission_seconds > 3600, "long symbols actually advance hours of virtual media");
        check(result.transmission_seconds > std::chrono::duration<double>(elapsed).count() * 100,
              "accelerated simulation is driven by DSP work rather than wall-clock airtime");
        check(result.dsp_buffered_bytes <= value.dsp_workspace_bytes, "long virtual duration does not grow DSP buffers");
        const auto content_seconds = static_cast<double>(decoded->packet.consumed_bytes) * 8 /
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
        run("actual audio TX constellation", test_audio_tx_accumulates_payload_constellation);
        run("partial back-to-back reception", test_partial_back_to_back_and_resume);
        run("encrypted automatic epoch", test_encrypted_auto_epoch);
        run("simulated epoch admission across clock jumps", test_simulated_epoch_admission_survives_clock_jumps);
        run("simulation review", test_simulation_review_and_live_constellation);
        run("default crystal", test_default_crystal_simulation);
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
