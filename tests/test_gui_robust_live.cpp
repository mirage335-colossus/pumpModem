#include "datapump/live.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"

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
std::optional<long double> scheduled_epoch;
std::vector<float> playback_prefix;
struct CaptureInput {
    std::vector<float> samples;
    long double first_utc = 0;
};
std::shared_ptr<const CaptureInput> capture_input;
}

// Only device I/O is replaced. The Session, channel, waveform, complete native
// receiver, physical quiet decisions, message codecs, and plot meter remain real.
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
        fixture::scheduled_epoch.reset();
        fixture::playback_prefix.clear();
    }
    ++fixture::playback_opens;
    detail::UtcPlaybackScope timed(options.follow_system_clock);
    if (format) format({rate, rate, rate * .49, 4096});
    {
        std::lock_guard lock(fixture::mutex);
        fixture::scheduled = detail::utc_playback_epoch.has_value();
        fixture::scheduled_epoch = detail::utc_playback_epoch;
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
            if (fixture::playback_prefix.empty())
                fixture::playback_prefix.assign(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
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
    s.transfer.content_limit = 1024U * 1024;
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

void long_live_prefix_and_cancel() {
    using namespace triad::v3;
    auto s = settings(false);
    s.transfer.modem.bandwidth_hz = 3600;
    s.transfer.fec = FecMode::rs20;
    s.transfer.key = Crypto::amateur();
    // One highly compressible long draft. Validate its first physical DATA
    // waveform, then cancel. Whole-interval codec and quiet completion are
    // covered by the separate native codec/short waveform gates.
    Message draft;
    draft.data.assign(97, 'e');
    fixture::hold_playback = true;
    live::Session session;
    session.start(s);
    ready(session);
    session.transmit(draft);
    const auto sending = wait(session, [](const auto& out) {
        check(out.error.empty(), "Long Robust TX failed: " + out.error);
        std::lock_guard lock(fixture::mutex);
        return fixture::playback_active == 1 && fixture::scheduled_epoch && !fixture::playback_prefix.empty();
    }, "Long Robust draft did not reach scheduled native PCM");
    const auto& trace = sending.transmit_trace;
    check(trace.active && !trace.raw && !trace.short_text && trace.total_wire_bits == message_interval_bits &&
          trace.source.size() == modem::TransmitTrace::source_limit &&
          trace.wire_plain_bits.size() == modem::TransmitTrace::bit_limit,
          "Long Robust TX lost its exact interval extent or bounded trace");
    // First 96 bits of SHA-256("DataPump/byte-boundary/v1"), twice on wire.
    constexpr std::array<std::uint8_t, 12> marker{
        0xe8,0x1c,0xac,0x57,0x5d,0xff,0x31,0xfa,0xea,0xa0,0xf8,0x40};
    for (std::size_t i = 0; i < message_marker_bits; ++i) {
        const auto at = i % 96;
        check(trace.wire_plain_bits[i] == ((marker[at / 8] >> (7 - at % 8)) & 1),
              "The long Robust session did not bind the canonical interval marker");
    }

    std::vector<float> played;
    std::uint64_t epoch = 0;
    {
        std::lock_guard lock(fixture::mutex);
        played = fixture::playback_prefix;
        epoch = static_cast<std::uint64_t>(*fixture::scheduled_epoch);
    }
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
    const auto& calendar = profile.waveform;
    const auto slot = calendar.first_slot_not_before({epoch, 0});
    const auto start = calendar.transmit_slot(slot).helper_begin;
    check(start == HelperBurstCoordinate{epoch,0}, "Long Robust TX began outside its configured public slot");
    const auto first = calendar.calendar().distance(calendar.origin(), start);
    constexpr std::array<std::uint8_t, 1> first_marker_bit{1};
    Emission expected(calendar, profile.key, {EmissionMode::Message, slot, first_marker_bit, 1, first, {}});
    std::vector<Complex> iq(played.size());
    check(expected.read(iq) == played.size(), "The reference first marker bit did not cover the PCM prefix");
    const auto scale = .5 / (std::sqrt(2.) * profile.maximum_unscaled_data * expected.report().data_gain);
    unsigned active = 0;
    for (std::size_t i = 0; i < played.size(); ++i) {
        const auto sample = live::detail::real_pcm(iq[i], first + i, profile.receiver.data_if_hz,
            s.transfer.modem.sample_rate, scale);
        check(std::abs(sample - played[i]) < 1e-6, "Long Robust playback emitted a different first waveform bit");
        if (std::abs(played[i]) > 1e-4) ++active;
    }
    check(active > 64, "The long Robust playback fixture observed only an idle prefix");
    session.cancel_transmit();
    const auto ended = wait(session, [](const auto& out) {
        return !out.transmitting && fixture::playback_active == 0;
    }, "Cancelling the long Robust draft did not stop playback");
    check(ended.received.empty() && ended.pattern_steps.empty() &&
          std::none_of(ended.signals.begin(), ended.signals.end(), [](const auto& signal) { return signal.complete; }),
          "Cancelling the long Robust draft fabricated a receive result");
    close(session);
    fixture::hold_playback = false;
}

void full_duplex_reserves_long_preparation() {
    auto s = settings(false);
    s.full_duplex = true;
    s.live_noise_dbfs_hz = -100;
    s.transfer.compression = true;
    s.receiver_memory_limit = 48U * 1024 * 1024;
    const auto captures = fixture::capture_opens.load();
    const auto playbacks = fixture::playback_opens.load();
    constexpr std::string_view failure = "Robust full-duplex transmit workspace exceeds the receiver memory limit";
    live::Session session;
    session.start(s);
    ready(session);
    wait(session, [&](const auto& out) { return out.receiver_error == failure; },
         "Full-duplex receive did not check prospective TX compression memory");
    Message draft;
    draft.data.assign(97, 'e');
    session.transmit(draft);
    const auto rejected = wait(session, [](const auto& out) { return !out.transmitting; },
        "Full-duplex TX did not reject an insufficient shared memory limit");
    check(rejected.error == failure && rejected.transmit_trace.total_wire_bits == 0 &&
          fixture::capture_opens == captures && fixture::playback_opens == playbacks,
          "Full-duplex preparation spent an unreserved workspace or opened audio");
    close(session);
}

void late_affine_clock_support() {
    using namespace triad::v3;
    const auto check_support = [](const PublicProfile& waveform, const ReceiverConfig& receiver, std::uint64_t index) {
        const auto plan = make_receiver_slot_plan(waveform, receiver, index);
        const auto& config = waveform.config();
        const auto clock = receiver.clock_bound_ppm * 1e-6L;
        const auto fs = waveform.effective_sample_rate();
        const auto earliest = fs * (config.capture_lead_seconds +
            (plan.source_start_seconds - 2 / fs) / (1 + clock) + config.arrival_low_seconds);
        const auto latest = fs * (config.capture_lead_seconds +
            (plan.source_start_seconds + (waveform.geometry().source_samples + 2) / fs) / (1 - clock) +
            config.arrival_high_seconds + receiver.spread_delay_seconds);
        check(plan.first_sample <= std::floor(earliest) && plan.stop_sample >= std::ceil(latest),
              "The public Robust window does not contain both affine scorer edges");
        return plan;
    };
    auto s = settings(true);
    s.transfer.modem.bandwidth_hz = 36;
    s.transfer.modem.oscillator_search =
        modem::OscillatorSearchConfig{.lf = {100, .5}, .rf = {1, .05}, .rf_shift_hz = 30e6, .margin = 3};
    s.transfer.key = Crypto::amateur();
    s.helper.power_w = 40;
    s.tx_dbm = 10 * std::log10(4000.);
    s.path_loss_db = 180;
    s.noise_dbm_hz = -164;
    s.target_cn0_db_hz = 32;
    s.added_symbols = 10;
    s.added_interleave = true;
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
    constexpr std::uint64_t seed = 0x726f62757374ULL;
    for (const bool broad : {false, true}) {
        s.transfer.clock_sync = broad ? clock_sync::Policy{0, 14, 0} : clock_sync::Policy{.001, .001, 0};
        s.transfer.audio_timing_error_seconds = broad ? .07 : 0;
        const auto profile = live::detail::prepare_profile(s, *s.calendar, {}, {.simulation_seed = seed});
        const auto& waveform = profile.waveform;
        const auto& effects = profile.channel.effects;
        check(waveform.config().audio_pair_ppm == 100 && profile.receiver.clock_bound_ppm > 300 &&
              std::abs(effects.tx.audio_ppm) <= 50 && std::abs(effects.rx.audio_ppm) <= 50,
              "Robust search margin changed the nominal physical oscillator model");
        auto nominal_config = waveform.config();
        nominal_config.clock_bound_ppm = 0;
        const PublicProfile nominal(nominal_config, s.transfer.key);
        for (const auto index : std::array<std::uint64_t, 5>{0, 15, 23, 100, waveform.last_slot() / 2}) {
            const auto slot = waveform.slot(index);
            const auto baseline = nominal.slot(index);
            check(slot.tx.pattern_begin == baseline.tx.pattern_begin && slot.symbol_ordinal == baseline.symbol_ordinal &&
                  slot.observation.first <= baseline.observation.first && slot.observation.stop >= baseline.observation.stop &&
                  slot.data_support.first == baseline.data_support.first && slot.data_support.stop == baseline.data_support.stop,
                  "Widening Robust receive support changed the physical source or shrank observation");
            check(slot.quiet_aperture.first == slot.quiet_aperture.stop ||
                  (slot.quiet_aperture.first >= baseline.quiet_aperture.first &&
                   slot.quiet_aperture.stop <= baseline.quiet_aperture.stop),
                  "Robust clock margin expanded the scored quiet aperture");
            const auto plan = check_support(waveform, profile.receiver, index);
            if (index <= 100) check(receiver_slot_workspace(profile.receiver, plan, 1) > 0,
                "Native DSP could not plan the late Robust observation");
        }
    }

    PublicProfileConfig signed_arrival;
    signed_arrival.format.sample_rate = 6000;
    signed_arrival.format.inner_rate_hz = 100;
    signed_arrival.format.added_symbols = 1;
    signed_arrival.format.added_interleave = false;
    signed_arrival.format.bit_seconds = 8;
    signed_arrival.audio_pair_ppm = 100;
    signed_arrival.clock_bound_ppm = 300.015;
    signed_arrival.arrival_low_seconds = signed_arrival.arrival_high_seconds = -1000;
    const PublicProfile waveform(signed_arrival);
    ReceiverConfig receiver;
    receiver.sample_rate_hz = signed_arrival.format.sample_rate;
    receiver.data_if_hz = signed_arrival.data_if_hz;
    receiver.clock_bound_ppm = signed_arrival.clock_bound_ppm;
    receiver.noise_variance = 1;
    check(receiver_slot_workspace(receiver, check_support(waveform, receiver, 100), 1) > 0,
          "Native DSP could not plan a public clock window with a negative arrival offset");
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

void complete_native_simulation(bool gpu = false) {
    // One predeclared functional stratum, not a sensitivity/capacity claim:
    // public calendar, clear e=001, FEC/Helper/spread/selective/FHSS Off, exact
    // oscillators, target and actual 80 dB-Hz with positive physical noise.
    // At most five complete scored windows, 600,000 samples and 90s wall time.
    const auto captures = fixture::capture_opens.load(), playbacks = fixture::playback_opens.load();
    auto simulated = settings(true);
    simulated.gpu = gpu;
    if (gpu) simulated.receiver_memory_limit = 1024U * 1024 * 1024;
    // The public slot cadence is at least a minute. A supported lower logical
    // sample rate keeps the full physical calendar inside the sample budget.
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    check(prepared.gpu_device.empty() == !gpu && prepared.gpu_score_rows == 0,
          "Robust GPU readiness does not match the selected backend");
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
    if (gpu) {
        check(!completed.gpu_device.empty() && completed.gpu_score_rows > 0,
              "GPU simulation decoded without actual accelerated scoring");
        const auto rows = completed.gpu_score_rows;
        // Repeated profile preparation after TX must keep the same backend
        // and its cumulative work receipt for this settings generation.
        check(ready(session).gpu_score_rows >= rows, "Simulation preparation reset GPU work evidence");
        std::cout << "GPU Session decoded e/001 on " << completed.gpu_device
                  << "; actual GPU score rows=" << rows << '\n';
        (void)session.try_suspend_capture();
        wait(session, [&](const auto&) { return session.try_suspend_capture(); },
             "GPU suspension did not release its receiver resources");
        session.resume_capture();
        const auto resumed = ready(session);
        check(!resumed.gpu_device.empty() && resumed.gpu_score_rows == 0,
              "Resumed GPU generation retained stale completed-work evidence");
        simulated.gpu = false;
        session.configure(simulated);
        const auto cpu = ready(session);
        check(cpu.gpu_device.empty() && cpu.gpu_score_rows == 0,
              "Switching GPU off retained the old generation's backend or work evidence");
        simulated.gpu = true;
        session.configure(simulated);
        check(!ready(session).gpu_device.empty(), "Re-enabling GPU did not prepare a new backend");
    }
    close(session);
    check(session.snapshot().received.empty(), "Stopped simulation emitted a late duplicate message");
}

void unavailable_gpu() {
    auto requested = settings(true);
    requested.gpu = true;
    if (!live::gpu_receiver_available()) {
        bool rejected = false;
        try { live::validate_settings(requested); }
        catch (const Error& error) { rejected = std::string(error.what()).find("Vulkan support required") != std::string::npos; }
        check(rejected, "CPU-only build accepted requested GPU processing");
        return;
    }
    // This mode is run with an intentionally unavailable Vulkan ICD. Failure
    // is asynchronous and visible; it must not report ready on a CPU backend.
    live::Session session;
    session.start(requested);
    const auto failed = wait(session, [](const auto& out) { return !out.receiver_error.empty(); },
                             "Missing GPU did not report its initialization failure");
    check(!failed.running && failed.transmit_unavailable && failed.gpu_device.empty() && !failed.gpu_score_rows,
          "Unavailable GPU silently fell back or reported successful work");
    requested.gpu = false;
    session.configure(requested);
    check(ready(session).receiver_error.empty(), "GPU initialization failure prevented returning to CPU");
    close(session);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--gpu") {
            check(live::gpu_receiver_available(), "Actual GPU session test requires Vulkan support");
            complete_native_simulation(true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-unavailable") {
            unavailable_gpu();
            return 0;
        }
        check(argc == 1, "Unexpected Robust session test argument");
        if (!live::gpu_receiver_available()) unavailable_gpu();
        inactive_and_audio_ownership();
        live_transmit_and_cancel(false);
        live_transmit_and_cancel(true);
        long_live_prefix_and_cancel();
        full_duplex_reserves_long_preparation();
        late_affine_clock_support();
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
