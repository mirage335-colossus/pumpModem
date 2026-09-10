#include "datapump/live.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
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
                                                std::chrono::milliseconds timeout = 6s) {
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
    check(std::any_of(later.constellation.begin(), later.constellation.end(), [](auto point) {
        return std::abs(std::abs(point) - 1) > 0.1;
    }), "raw constellation retains amplitude instead of normalizing every point onto a circle");
    check(later.received.empty() && later.signals.empty(), "noise is never promoted to a message");
    session.stop();
    check(!session.snapshot().running, "stop is immediately observable");
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
    }, 10s);
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
    session.start(value);
    const auto sent = message(3, 700);
    session.transmit(sent);
    const auto final = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); }, 12s);
    check(final.received.front().packet.message.data == sent.data && final.received.front().packet.authenticated,
          "selected second named key verifies actual encrypted continuous audio");
    check(final.received.front().timestamp > 1000000000, "zero timestamp selects the current epoch automatically");
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
    const auto received = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); }, 10s);
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
        }, 15s);
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
        run("partial back-to-back reception", test_partial_back_to_back_and_resume);
        run("encrypted automatic epoch", test_encrypted_auto_epoch);
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
