#include "datapump/live.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
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
    value.simulation_speed = 8;
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
    do {
        auto snapshot = session.snapshot();
        if (!snapshot.error.empty()) throw std::runtime_error("session failed: " + snapshot.error);
        if (predicate(snapshot)) return snapshot;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("continuous session timed out");
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
    bool partial_before_completion = false, transmitting_seen = false;
    std::uint64_t previous_samples = 0;
    const auto done = wait_for(session, [&](const auto& snapshot) {
        transmitting_seen |= snapshot.transmitting;
        check(snapshot.samples_received >= previous_samples, "source sample time never resets between transmissions");
        previous_samples = snapshot.samples_received;
        for (const auto& signal : snapshot.signals) {
            if (!signal.validated) {
                if (pending.contains(signal.packet_id)) check(pending[signal.packet_id] == signal.id, "partial text keeps a stable acquisition identity");
                pending[signal.packet_id] = signal.id;
                if (signal.received_bytes < signal.expected_bytes && signal.text.size() < first.data.size() &&
                    !signal.text.empty() && snapshot.transmitting) partial_before_completion = true;
            } else if (pending.contains(signal.packet_id)) check(pending[signal.packet_id] == signal.id, "verified text replaces its provisional row");
        }
        for (const auto& item : snapshot.received) {
            check(item.diagnostics.preamble_correlation > 0.6, "received message passed actual audio acquisition");
            received.push_back(item.packet.message);
        }
        return received.size() == 2 && !snapshot.transmitting;
    }, 10s);
    check(transmitting_seen, "transmission state is observable");
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
void test_cancel_reconfigure_and_bounds() {
    live::Session session;
    rejects([&] { session.transmit(message(4, 1)); }, "stopped session rejects transmit");
    auto invalid = settings();
    invalid.receive_buffer_seconds = std::numeric_limits<double>::infinity();
    rejects([&] { session.start(invalid); }, "nonfinite receive window rejected before allocation");
    invalid = settings(); invalid.simulation_speed = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { session.start(invalid); }, "nonfinite simulation speed rejected");
    auto value = settings();
    session.start(value);
    session.transmit(message(5, 1200));
    wait_for(session, [](const auto& snapshot) { return snapshot.transmitting && snapshot.samples_received > 8000; });
    const auto before = std::chrono::steady_clock::now();
    session.cancel_transmit();
    check(std::chrono::steady_clock::now() - before < 100ms, "cancel does not wait for a full capture or transmission");
    check(!session.snapshot().transmitting, "cancelled transmission state clears immediately");
    value.simulation_seed = 23;
    value.receive_buffer_seconds = 8;
    session.configure(value);
    const auto sent = message(6, 700);
    session.transmit(sent); // Automatically expands the eight-second window.
    const auto final = wait_for(session, [](const auto& snapshot) { return !snapshot.received.empty(); });
    check(final.received.front().packet.message.id == sent.id && final.received.front().packet.message.data == sent.data,
          "fresh configuration recovers after cancellation and expands a short receive window");
    check(final.buffered_samples <= value.transfer.modem.memory_limit / 64, "rolling receive memory stays bounded");
    const auto stopped = std::chrono::steady_clock::now();
    session.stop();
    check(std::chrono::steady_clock::now() - stopped < 100ms, "stop enqueues cancellation promptly");
}
void test_unrecoverable_noise_does_not_validate() {
    live::Session session;
    auto value = settings(); value.simulation_snr_db = -80;
    session.start(value); session.transmit(message(7, 10));
    bool started = false;
    wait_for(session, [&](const auto& snapshot) {
        started |= snapshot.transmitting;
        check(snapshot.received.empty(), "noise-obscured transmission never bypasses the modem");
        check(std::none_of(snapshot.signals.begin(), snapshot.signals.end(), [](const auto& signal) { return signal.validated; }),
              "unrecoverable samples never produce verified ticker text");
        return started && !snapshot.transmitting;
    });
}
void test_infeasible_modes_keep_the_channel_running() {
    live::Session session;
    auto value = settings();
    value.transfer.modem = tuning::resolve(1200, 6, tuning::PatternMode::auto_pattern, false).config;
    session.start(value);
    auto previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    auto current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(current.samples_received > previous.samples_received && current.waveform != previous.waveform,
          "unbufferable weak mode still produces continuous samples and actual changing plots");
    check(current.received.empty(), "unbufferable acquisition makes no receive claim");
    value.transfer.modem = tuning::resolve(192000, 120, tuning::PatternMode::auto_pattern, false).config;
    session.configure(value);
    previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(current.samples_received > previous.samples_received && current.waveform != previous.waveform,
          "384k sample rate clamps its window while wideband noise and plots continue");
    check(current.buffered_samples <= value.transfer.modem.memory_limit / 64, "wideband receive window stays within budget");
    value.transfer.modem = tuning::resolve(1, 6, tuning::PatternMode::auto_pattern, false).config;
    session.configure(value);
    previous = wait_for(session, [](const auto& snapshot) { return snapshot.sequence >= 3; });
    current = wait_for(session, [&](const auto& snapshot) { return snapshot.sequence > previous.sequence + 2; });
    check(!current.constellation.empty() && current.constellation != previous.constellation,
          "one-hertz mode retains changing measured partial-chip constellation points");
}
}
int main() {
    try {
        test_idle_noise_and_plots();
        test_partial_back_to_back_and_resume();
        test_encrypted_auto_epoch();
        test_receive_authentication_policy();
        test_cancel_reconfigure_and_bounds();
        test_unrecoverable_noise_does_not_validate();
        test_infeasible_modes_keep_the_channel_running();
        std::cout << "Continuous receiver and simulation tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; return 1;
    }
}
