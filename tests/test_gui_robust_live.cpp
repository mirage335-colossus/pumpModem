#include "datapump/live.hpp"
#include "datapump/robust_profile.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;

namespace fixture {
std::mutex mutex;
std::atomic<unsigned> capture_opens{0}, capture_active{0}, playback_opens{0}, playback_active{0};
std::atomic<bool> capture_overlap{false}, hold_playback{false};
std::string capture_device, playback_device;
audio::Options capture_options, playback_options;
audio::ChannelMode playback_channels = audio::ChannelMode::stereo;
std::size_t playback_samples = 0;
bool scheduled = false;
struct CaptureInput {
    std::vector<float> samples;
    long double first_utc = 0;
};
std::shared_ptr<const CaptureInput> capture_input;
}

// Only device I/O is replaced. The Session, channel, waveform, complete native
// receiver, physical quiet decisions, short codec, and plot meter remain real.
namespace datapump::audio {
std::vector<Device> devices() { return {{"robust-fixture", "Robust fixture"}}; }

void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& consume,
             std::stop_token stop, StreamFormatCallback format, Options options) {
    if (fixture::capture_active.fetch_add(1)) fixture::capture_overlap = true;
    struct Close { ~Close() { --fixture::capture_active; } } close;
    std::shared_ptr<const fixture::CaptureInput> input;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_device = device;
        fixture::capture_options = options;
        input = fixture::capture_input;
    }
    ++fixture::capture_opens;
    if (format) format({rate, rate, rate * .49, 4096});
    if (input) {
        if (!options.capture_timing) throw Error("Native Robust did not request dated capture");
        for (std::size_t at = 0; at < input->samples.size() && !stop.stop_requested();) {
            const auto count = std::min<std::size_t>(128, input->samples.size() - at);
            TimePrediction stamp;
            stamp.utc_seconds = input->first_utc + static_cast<long double>(at) / rate;
            stamp.uncertainty_seconds = 1e-7;
            stamp.seconds_per_frame = 1. / rate;
            stamp.rate_uncertainty_fraction = 1e-12;
            stamp.quality = TimingQuality::bounded;
            options.capture_timing(stamp);
            if (!consume(std::span(input->samples).subspan(at, count))) break;
            at += count;
            std::this_thread::sleep_for(2ms);
        }
        return;
    }
    // Lifecycle checks have no PCM input. Lack of transport data is not silence
    // and must never be promoted to a completed message by the Session.
    while (!stop.stop_requested()) std::this_thread::sleep_for(2ms);
}

void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& consume,
             std::stop_token stop, StreamFormatCallback format) {
    capture(rate, device, consume, stop, std::move(format), {});
}

void playback(std::uint32_t rate, const std::string& device, const PlaybackCallback& source,
              std::stop_token stop, StreamFormatCallback format, ChannelMode channels, Options options) {
    ++fixture::playback_active;
    struct Close { ~Close() { --fixture::playback_active; } } close;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::playback_device = device;
        fixture::playback_options = options;
        fixture::playback_channels = channels;
        fixture::playback_samples = 0;
        fixture::scheduled = false;
    }
    ++fixture::playback_opens;
    detail::UtcPlaybackScope timed(options.follow_system_clock);
    if (format) format({rate, rate, rate * .49, 4096});
    {
        std::lock_guard lock(fixture::mutex);
        fixture::scheduled = detail::utc_playback_epoch.has_value();
    }
    std::array<float, 4096> block{};
    while (!stop.stop_requested()) {
        const auto count = source(block);
        if (!count) break;
        if (count > block.size()) throw Error("Robust playback exceeded the borrowed audio buffer");
        for (const auto sample : std::span(block).first(count))
            if (!std::isfinite(sample) || std::abs(sample) * options.transmit_gain > .95)
                throw Error("Robust playback violated finite PCM headroom");
        {
            std::lock_guard lock(fixture::mutex);
            fixture::playback_samples += count;
        }
        while (fixture::hold_playback && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
    }
}

void playback(std::uint32_t rate, const std::string& device, const PlaybackCallback& source,
              std::stop_token stop, StreamFormatCallback format, ChannelMode channels) {
    playback(rate, device, source, stop, std::move(format), channels, {});
}
}

namespace {
void check(bool condition, const std::string& reason) { if (!condition) throw Error(reason); }

template<class Predicate>
live::Snapshot wait(live::Session& session, Predicate ready, const char* reason,
                    std::chrono::seconds limit = 10s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    for (;;) {
        auto snapshot = session.snapshot();
        if (ready(snapshot)) return snapshot;
        check(std::chrono::steady_clock::now() < deadline,
              std::string(reason) + ": " + snapshot.status + "; " + snapshot.error + "; " + snapshot.receiver_error);
        std::this_thread::sleep_for(3ms);
    }
}

live::Settings settings(bool simulation) {
    live::Settings s;
    s.simulation = simulation;
    s.device = "robust-fixture";
    s.transfer.fec = FecMode::off;
    s.transfer.modem.sample_rate = 6000;
    s.transfer.modem.carrier_hz = 1725;
    s.transfer.modem.bandwidth_hz = 120;
    s.transfer.modem.oscillator_search = modem::OscillatorSearchConfig{.lf = {0, 0}, .rf = {0, 0}, .margin = 1};
    s.transfer.clock_sync = clock_sync::Policy{.accuracy_seconds = .001, .region_seconds = .001, .offset_seconds = 0};
    s.transfer.audio_timing_error_seconds = simulation ? 0 : .07;
    s.helper.power_w = 0;
    s.target_cn0_db_hz = 80;
    s.tx_dbm = 20;
    s.path_loss_db = 114;
    s.noise_dbm_hz = -174;
    s.receiver_memory_limit = 512U * 1024 * 1024;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    s.calendar = live::Calendar{static_cast<std::uint64_t>(now), 7000037};
    return s;
}

live::Snapshot ready(live::Session& session) {
    return wait(session, [](const auto& s) {
        check(s.error.empty(), "Native Robust preparation failed: " + s.error);
        return s.running && !s.transmit_unavailable;
    },
                "Native Robust profile did not become ready");
}

void close(live::Session& session) {
    session.stop();
    wait(session, [&](const auto&) { return session.ready_to_close(); }, "Robust session did not release workers");
}

void inactive_and_audio_ownership() {
    auto s = settings(false);
    s.live_noise_dbfs_hz = -80;
    s.exclusive = true;
    const auto opens = fixture::capture_opens.load();
    live::Session session;
    session.start(s, false);
    std::this_thread::sleep_for(40ms);
    check(fixture::capture_opens == opens && !session.snapshot().running,
          "Inactive Robust acquired audio while another modem is selected");
    session.resume_capture();
    ready(session);
    wait(session, [&](const auto&) { return fixture::capture_active == 1 && fixture::capture_opens == opens + 1; },
         "Selecting Robust did not open capture");
    {
        std::lock_guard lock(fixture::mutex);
        check(fixture::capture_device == s.device && fixture::capture_options.exclusive &&
              fixture::capture_options.request_capture_timing && fixture::capture_options.capture_timing,
              "Robust capture lost the selected device, exclusive mode, or dated PCM contract");
    }
    session.set_transmit_gain(.125);
    session.set_channel_mode(audio::ChannelMode::right_mono);
    std::this_thread::sleep_for(40ms);
    check(fixture::capture_opens == opens + 1, "Changing TX gain or channel restarted Robust capture");
    wait(session, [&](const auto&) { return session.try_suspend_capture(); }, "Robust did not suspend capture");
    check(fixture::capture_active == 0 && !fixture::capture_overlap,
          "Robust acknowledged capture handoff before releasing the device");
    session.resume_capture();
    ready(session);
    wait(session, [&](const auto&) { return fixture::capture_active == 1 && fixture::capture_opens == opens + 2; },
         "Robust did not reacquire capture after selection");
    close(session);
    const auto stopped = session.snapshot();
    check(fixture::capture_active == 0 && stopped.received.empty() && stopped.pattern_steps.empty(),
          "Closing an empty audio source fabricated physical reception or retained capture");
}

void live_transmit_and_cancel(bool duplex) {
    auto s = settings(false);
    s.channel_mode = audio::ChannelMode::right_mono;
    s.transmit_gain = .125;
    s.exclusive = true;
    s.full_duplex = duplex;
    s.live_noise_dbfs_hz = -80;
    if (duplex) s.transfer.key = Crypto::amateur();
    fixture::hold_playback = true;
    const auto captures = fixture::capture_opens.load();
    live::Session session;
    session.start(s);
    ready(session);
    wait(session, [&](const auto&) { return fixture::capture_active == 1 && fixture::capture_opens > captures; },
         "Live Robust did not acquire its receiver before TX");
    session.transmit(Message{.data = {'e'}});
    wait(session, [](const auto&) {
        std::lock_guard lock(fixture::mutex);
        return fixture::playback_active == 1 && fixture::scheduled && fixture::playback_samples > 0;
    }, "Live Robust did not produce scheduled PCM");
    check(fixture::capture_active == (duplex ? 1U : 0U),
          "Robust ignored the selected half/full duplex ownership policy");
    {
        std::lock_guard lock(fixture::mutex);
        check(fixture::playback_device == s.device && fixture::playback_channels == audio::ChannelMode::right_mono &&
              fixture::playback_options.transmit_gain == .125 && fixture::playback_options.exclusive &&
              fixture::playback_options.follow_system_clock &&
              fixture::playback_options.maximum_utc_error_seconds == .035 &&
              fixture::playback_options.presentation_lag_seconds == .035,
              "Robust live playback lost device, channel, gain, exclusive mode, or audio timing");
    }
    session.cancel_transmit();
    const auto cancelled = wait(session, [](const auto& out) { return !out.transmitting && fixture::playback_active == 0; },
                                "Cancel TX did not release Robust audio");
    check(cancelled.received.empty() && cancelled.pattern_steps.empty() &&
          std::none_of(cancelled.signals.begin(), cancelled.signals.end(), [](const auto& x) { return x.complete; }),
          "Cancelling live TX fabricated successful reception");
    wait(session, [](const auto&) { return fixture::capture_active == 1; },
         "Robust did not restore capture after Cancel TX");
    check(fixture::capture_opens == captures + (duplex ? 1U : 2U) && !fixture::capture_overlap,
          "Robust cancellation restarted duplex capture or overlapped half-duplex capture");
    if (duplex) {
        const auto locked = session.snapshot();
        check(locked.force_transmit_available && locked.transmit_unavailable,
              "Cancelled encrypted output forgot its reserved future key address");
        bool rejected = false;
        try { session.transmit(Message{.data = {'e'}}); }
        catch (const Error&) { rejected = true; }
        check(rejected, "Ordinary encrypted TX reused its reserved future key address");
        const auto opens = fixture::playback_opens.load();
        session.transmit(Message{.data = {'e'}}, true);
        wait(session, [&](const auto&) {
            std::lock_guard lock(fixture::mutex);
            return fixture::playback_opens == opens + 1 && fixture::playback_active == 1 &&
                fixture::scheduled && fixture::playback_samples > 0;
        }, "Force next transmission did not reach native scheduled encrypted output");
        session.cancel_transmit();
        wait(session, [](const auto& out) { return !out.transmitting && fixture::playback_active == 0; },
             "Forced Robust TX did not cancel");
        check(session.snapshot().force_transmit_available && fixture::capture_active == 1 &&
              fixture::capture_opens == captures + 1,
              "Forced cancellation forgot key-use history or restarted duplex capture");
    }
    close(session);
    fixture::hold_playback = false;
}

void simulation_cancel_is_not_completion() {
    const auto captures = fixture::capture_opens.load(), playbacks = fixture::playback_opens.load();
    live::Session session;
    session.start(settings(true));
    ready(session);
    session.transmit_noise();
    wait(session, [](const auto& out) { return out.transmitting && !out.waveform.empty(); },
         "Simulated noise did not produce a waveform");
    session.cancel_transmit();
    auto stopped = wait(session, [](const auto& out) { return !out.transmitting; }, "Noise cancellation did not stop");
    check(stopped.received.empty() && stopped.pattern_steps.empty() && stopped.signals.empty(),
          "Noise or its cancellation fabricated successful reception");
    ready(session);
    session.transmit(Message{.data = {'e'}});
    session.cancel_transmit();
    stopped = wait(session, [](const auto& out) { return !out.transmitting; }, "Message cancellation did not stop");
    check(stopped.received.empty() &&
          std::none_of(stopped.signals.begin(), stopped.signals.end(), [](const auto& x) { return x.complete; }),
          "Immediate simulation cancellation fabricated physical completion");
    close(session);
    check(session.snapshot().received.empty() && fixture::capture_opens == captures && fixture::playback_opens == playbacks,
          "Cancelled simulation emitted a late message or acquired audio");
}

void dated_live_data_then_eof() {
    using namespace triad::v3;
    auto s = settings(false);
    s.transfer.modem.sample_rate = 1500;
    s.transfer.modem.carrier_hz = 500;
    s.transfer.audio_timing_error_seconds = 0;
    s.live_noise_dbfs_hz = -100;
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
    const std::array<std::uint8_t, 1> bit{1};
    Emission source(profile.waveform, profile.key, {EmissionMode::Message, 0, bit});
    const auto rate = s.transfer.modem.sample_rate;
    const auto prefix = 2U * rate;
    const auto after_epoch = profile.waveform.slot(0).observation.stop + rate;
    check(after_epoch + prefix < 100000, "Dated live fixture exceeds its one-window sample budget");
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
    input->samples.resize(prefix + after_epoch);
    std::mt19937_64 rng(0x726f6275737421ULL);
    std::normal_distribution<double> noise(0, std::sqrt(rate / 2. * 1e-10));
    for (auto& sample : input->samples) sample = static_cast<float>(noise(rng));
    std::array<Complex, 512> iq{};
    std::size_t at = 0;
    for (;;) {
        const auto count = source.read(iq);
        if (!count) break;
        check(at + count <= after_epoch, "Dated live source exceeded its capture");
        for (std::size_t i = 0; i < count; ++i) {
            const auto rotation = std::polar(1., 2 * std::numbers::pi * s.transfer.modem.carrier_hz *
                static_cast<double>(at + i) / rate);
            input->samples[prefix + at + i] += static_cast<float>(std::numbers::sqrt2 * .1 * (iq[i] * rotation).real());
        }
        at += count;
    }
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = input;
    }
    live::Session session;
    session.start(s);
    ready(session);
    const auto out = wait(session, [](const auto& snapshot) {
        check(snapshot.error.empty(), "Dated Robust input failed: " + snapshot.error);
        check(snapshot.receiver_error.empty() || snapshot.receiver_error == "Robust audio input ended.",
              "Dated Robust receive failed: " + snapshot.receiver_error);
        check(snapshot.received.empty(), "Live transport EOF fabricated a decoded message");
        return snapshot.receiver_error == "Robust audio input ended.";
    }, "Dated native DATA scoring did not finish", 45s);
    check(out.pattern_steps.size() == 1 && out.pattern_steps[0].index == 0 &&
          out.pattern_steps[0].decision == live::PatternDecision::data && out.pattern_steps[0].bit == 1 &&
          out.pattern_steps[0].evidence_complete && out.pattern_steps[0].fine_candidates > 0,
          "Real dated PCM did not survive Hilbert conversion and complete native DATA scoring");
    check(!out.waveform.empty() && !out.constellation.empty() && out.constellation_source == live::ConstellationSource::input &&
          std::none_of(out.signals.begin(), out.signals.end(), [](const auto& signal) { return signal.complete; }),
          "Live input plots are absent or its unfinished DATA became a completed message");
    close(session);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
    }
}

void complete_native_simulation() {
    // One predeclared functional stratum, not a sensitivity/capacity claim:
    // public calendar, clear e=001, FEC/Helper/spread/selective/FHSS Off, exact
    // oscillators, target and actual 80 dB-Hz with positive physical noise.
    // At most five complete scored windows, 600,000 samples and 90s wall time.
    const auto captures = fixture::capture_opens.load(), playbacks = fixture::playback_opens.load();
    auto simulated = settings(true);
    // The public slot cadence is at least a minute. A supported lower logical
    // sample rate keeps the full physical calendar inside the sample budget.
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    check(prepared.bit_seconds && prepared.slot_stride_seconds && *prepared.slot_stride_seconds * 6 * 1500 < 600000,
          "Robust simulation exceeds its predeclared sample budget");
    session.transmit(Message{.data = {'e'}});
    std::vector<transfer::Received> received;
    auto completed = wait(session, [&](auto& out) {
        for (auto& event : out.received) received.push_back(std::move(event));
        check(out.error.empty() && out.receiver_error.empty(), "Native simulation failed: " + out.error + "; " + out.receiver_error);
        check(out.pattern_steps.size() <= 5, "Native simulation exceeded five public scoring windows");
        return !out.transmitting;
    }, "Native complete simulation exceeded its wall-time budget", 90s);
    check(received.size() == 1 && received[0].stream_complete && received[0].content_validated &&
          received[0].short_text_decoded && received[0].content.message.data == Bytes{'e'} &&
          received[0].raw_bits == Bytes({0, 0, 1}) && received[0].missing_symbols == 0,
          "Actual native DSP did not decode physically complete e/001");
    check(completed.pattern_steps.size() == 5, "Robust message did not score all three DATA and two quiet slots");
    for (std::size_t i = 0; i < completed.pattern_steps.size(); ++i) {
        const auto& row = completed.pattern_steps[i];
        check(row.index == i && row.ordinal == 7000037 + i && row.evidence_complete &&
              row.fine_candidates > 0 && row.workers > 0 && std::isfinite(row.operational_log_e),
              "Pattern steps lack actual ordered native evidence");
        check(row.decision == (i < 3 ? live::PatternDecision::data : live::PatternDecision::quiet),
              "Native message completion did not come from two physical quiet decisions");
        if (i < 3) check(row.bit == received[0].raw_bits[i], "Pattern DATA evidence disagrees with decoded bits");
        else check(row.quiet_modes > 0 && !row.bit, "Quiet completion lacks scored noise modes");
    }
    check(!completed.waveform.empty() && completed.waveform.size() <= 1024 && completed.waveform_sample_rate == 1500 &&
          !completed.spectrum_db.empty() && completed.spectrum_db.size() == completed.spectrum_complex.size() &&
          completed.spectrum_sample_count == 1024 && completed.spectrum_revision > 0 &&
          completed.spectrum_start_sample + completed.spectrum_sample_count < 600000 &&
          !completed.constellation.empty() && completed.constellation.size() <= 512 &&
          completed.constellation_source == live::ConstellationSource::input,
          "Native reception did not produce bounded waveform, waterfall, and input I/Q plots");
    check(std::all_of(completed.waveform.begin(), completed.waveform.end(), [](float x) { return std::isfinite(x); }) &&
          std::all_of(completed.spectrum_db.begin(), completed.spectrum_db.end(), [](double x) { return std::isfinite(x); }),
          "Native plot output contains nonfinite samples");
    check(fixture::capture_opens == captures && fixture::playback_opens == playbacks,
          "Simulation acquired a physical audio device");
    close(session);
    check(session.snapshot().received.empty(), "Stopped simulation emitted a late duplicate message");
}
}

int main() {
    try {
        inactive_and_audio_ownership();
        live_transmit_and_cancel(false);
        live_transmit_and_cancel(true);
        simulation_cancel_is_not_completion();
        std::cout << "Native Robust lifecycle, duplex routing, and cancellation passed\n" << std::flush;
        complete_native_simulation();
        std::cout << "Native Robust simulation decoded e/001 with two physical quiet slots\n" << std::flush;
        dated_live_data_then_eof();
        std::cout << "Native Robust Session lifecycle, audio options, and real e/001 completion passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
