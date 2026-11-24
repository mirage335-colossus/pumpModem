#include "datapump/live.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/crypto.hpp"
#include "../src/live_capture_queue.hpp"
#include "../src/live_finite_plan.hpp"
#include "../src/triad_receiver_fine.hpp"
#include "../src/triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <future>
#include <mutex>
#include <numbers>
#include <random>
#include <string_view>
#include <thread>
#include <utility>

using namespace datapump;
using namespace std::chrono_literals;

namespace fixture {
std::mutex mutex;
std::atomic<unsigned> capture_opens{0}, capture_active{0}, playback_opens{0}, playback_active{0};
std::atomic<unsigned> callback_started{0}, callback_finished{0};
std::atomic<bool> capture_overlap{false}, hold_playback{false}, hold_playback_source{false};
std::atomic<bool> hold_capture{false}, reject_capture_endpoint{false};
std::atomic<bool> hold_playback_format{false}, hold_endpoint_ack{false}, endpoint_started{false};
std::atomic<bool> pause_released{true}, capture_paused{false};
std::atomic<bool> loopback_ready{false};
std::atomic<std::uint64_t> capture_position{0};
std::atomic<std::uint64_t> capture_endpoint{0};
std::atomic<double> capture_passband_override{0}, playback_passband_override{0};
std::string capture_device, playback_device;
audio::Options capture_options, playback_options;
audio::ChannelMode playback_channels = audio::ChannelMode::stereo;
std::size_t playback_samples = 0;
bool scheduled = false;
std::optional<long double> scheduled_epoch;
std::optional<std::pair<long double, long double>> playback_format_window;
std::vector<float> playback_prefix;
std::vector<float> loopback_pcm;
std::optional<long double> loopback_epoch;
struct CaptureInput {
    std::vector<float> samples;
    long double first_utc = 0;
    bool callback_may_block = false, repeat = false, burst = false, finite = false, loopback = false;
    bool loopback_silence = false, short_after_cutoff = false;
    std::optional<std::size_t> loopback_playback_limit;
    std::size_t chunk_size = 128;
    std::optional<std::size_t> pause_after;
};
std::shared_ptr<const CaptureInput> capture_input;

long double utc_now() {
    return std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
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
    if (format) {
        const auto passband = fixture::capture_passband_override.load();
        StreamFormat actual{rate, rate, passband > 0 ? passband : rate * .49, 4096};
        actual.capture_callback_may_block = input && input->callback_may_block;
        if (actual.capture_callback_may_block) actual.workspace_bytes += input->samples.size() * sizeof(float);
        if (input && input->finite) actual.capture_stop_at = [input](std::uint64_t endpoint, std::stop_token stop) {
            fixture::endpoint_started = true;
            while (fixture::hold_endpoint_ack && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
            if (stop.stop_requested() || fixture::reject_capture_endpoint)
                throw Error("Fixture refused finite capture endpoint");
            if (input->loopback && (endpoint <= fixture::capture_position || endpoint > 600000))
                throw Error("Fixture rejected late or oversized finite capture endpoint");
            fixture::capture_endpoint = endpoint;
        };
        format(actual);
    }
    if (input) {
        if (!options.capture_timing) throw Error("Native Robust did not request dated capture");
        for (std::size_t at = 0; (input->repeat || at < input->samples.size()) && !stop.stop_requested();) {
            while (fixture::hold_capture && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
            if (input->pause_after && at >= *input->pause_after && !fixture::pause_released) {
                fixture::capture_paused = true;
                while (!fixture::pause_released && !(input->loopback && fixture::loopback_ready) && !stop.stop_requested())
                    std::this_thread::sleep_for(2ms);
            }
            if (stop.stop_requested()) break;
            const auto endpoint = fixture::capture_endpoint.load();
            const auto capture_end = endpoint - (endpoint && input->short_after_cutoff ? 1U : 0U);
            if (input->loopback && endpoint && at >= capture_end) break;
            const auto offset = input->repeat ? 0 : at;
            auto count = std::min(input->chunk_size, input->samples.size() - offset);
            if (input->pause_after && at < *input->pause_after)
                count = std::min(count, *input->pause_after - at);
            if (input->loopback && endpoint)
                count = std::min<std::uint64_t>(count, capture_end - at);
            TimePrediction stamp;
            stamp.utc_seconds = input->first_utc + static_cast<long double>(at) / rate;
            stamp.uncertainty_seconds = 1e-7;
            stamp.seconds_per_frame = 1. / rate;
            stamp.rate_uncertainty_fraction = 1e-12;
            stamp.quality = TimingQuality::bounded;
            options.capture_timing(stamp);
            ++fixture::callback_started;
            std::vector<float> actual;
            if (input->loopback) {
                actual.assign(count, 0);
                std::lock_guard lock(fixture::mutex);
                if (fixture::loopback_epoch && !input->loopback_silence) {
                    const auto first = std::llround((*fixture::loopback_epoch - input->first_utc) * rate);
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto position = static_cast<std::int64_t>(at + i) - first;
                        if (position >= 0 && static_cast<std::size_t>(position) < fixture::loopback_pcm.size() &&
                            (!input->loopback_playback_limit ||
                             static_cast<std::size_t>(position) < *input->loopback_playback_limit))
                            actual[i] = fixture::loopback_pcm[static_cast<std::size_t>(position)];
                    }
                }
            }
            const auto accepted = input->loopback ? consume(actual) : consume(std::span(input->samples).subspan(offset, count));
            ++fixture::callback_finished;
            if (!accepted) break;
            at += count;
            fixture::capture_position = at;
            if (!input->repeat && !input->burst) std::this_thread::sleep_for(2ms);
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
    bool loopback = false;
    {
        std::lock_guard lock(fixture::mutex);
        loopback = fixture::capture_input && fixture::capture_input->loopback;
        fixture::playback_device = device;
        fixture::playback_options = options;
        fixture::playback_channels = channels;
        fixture::playback_samples = 0;
        fixture::scheduled = false;
        fixture::scheduled_epoch.reset();
        fixture::playback_format_window.reset();
        fixture::playback_prefix.clear();
        fixture::loopback_pcm.clear();
        fixture::loopback_epoch.reset();
    }
    ++fixture::playback_opens;
    while (fixture::hold_playback_format && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
    if (stop.stop_requested()) return;
    detail::UtcPlaybackScope timed(options.follow_system_clock);
    const auto before_format = fixture::utc_now();
    if (format) {
        const auto passband = fixture::playback_passband_override.load();
        format({rate, rate, passband > 0 ? passband : rate * .49, 4096});
    }
    const auto after_format = fixture::utc_now();
    {
        std::lock_guard lock(fixture::mutex);
        fixture::scheduled = detail::utc_playback_epoch.has_value();
        fixture::scheduled_epoch = detail::utc_playback_epoch;
        fixture::playback_format_window = {before_format, after_format};
        fixture::loopback_epoch = detail::utc_playback_epoch;
    }
    while (fixture::hold_playback_source && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
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
            if (loopback) fixture::loopback_pcm.insert(fixture::loopback_pcm.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
        }
        while (fixture::hold_playback && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
    }
    if (loopback) fixture::loopback_ready = true;
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
    s.data_cadence = live::DataCadence::scheduled;
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

void data_cadence_profiles() {
    auto s = settings(true);
    const auto calendar = *s.calendar;
    const auto scheduled = live::detail::prepare_profile(s, calendar, {}, {.simulation_seed = 1});
    s.data_cadence = live::DataCadence::continuous;
    live::validate_settings(s);
    const auto simulated = live::detail::prepare_profile(s, calendar, {}, {.simulation_seed = 1});
    s.simulation = false;
    const auto live_profile = live::detail::prepare_profile(s, calendar, {});
    const auto adjusted = live::detail::with_capture_uncertainty(live_profile, .2, .1, 100);
    const auto first = simulated.waveform.transmit_slot(0);
    const auto next = simulated.waveform.transmit_slot(1);
    check(!scheduled.waveform.config().continuous_data && simulated.waveform.config().continuous_data &&
          scheduled.waveform.transmit_slot(0).pattern_end < scheduled.waveform.transmit_slot(1).pattern_begin &&
          first.pattern_end == next.pattern_begin,
          "Shared Robust DATA cadence did not pack consecutive gates");
    for (const auto* profile : {&live_profile, &adjusted}) {
        check(profile->waveform.config().continuous_data && profile->waveform.transmit_slot(0) == first &&
              profile->waveform.transmit_slot(1) == next &&
              profile->waveform.slot(1).symbol_ordinal == calendar.initial_ordinal + 1,
              "Live preparation or capture uncertainty changed the configured packed calendar");
    }
    const auto rejects = [](const auto& operation) {
        try { operation(); } catch (const Error& error) {
            return std::string_view(error.what()).starts_with("Continuous Robust DATA requires");
        }
        return false;
    };
    auto incompatible = s;
    const auto verify = [&] {
        check(rejects([&] { live::validate_settings(incompatible); }) &&
              rejects([&] { live::detail::prepare_profile(incompatible, calendar, {}); }),
              "Continuous Robust DATA accepted an incompatible waveform setting");
        incompatible = s;
    };
    incompatible.helper.power_w = 1;
    verify();
    incompatible.symbol_burst_wait = 1;
    verify();
    incompatible.symbol_burst_random = 1;
    verify();
    incompatible.fhss_profile = "fake-fcc";
    verify();
}

void scheduled_fill_profiles() {
    auto s = settings(true);
    s.symbol_burst_wait = 1;
    s.symbol_burst_random = 2;
    const auto calendar = *s.calendar;
    const auto scheduled = live::detail::prepare_profile(s, calendar, {}, {.simulation_seed = 1});
    s.data_cadence = live::DataCadence::scheduled_fill;
    live::validate_settings(s);
    const auto simulated = live::detail::prepare_profile(s, calendar, {}, {.simulation_seed = 1});
    s.simulation = false;
    const auto live_profile = live::detail::prepare_profile(s, calendar, {});
    const auto adjusted = live::detail::with_capture_uncertainty(live_profile, .2, .1, 100);
    const auto fill = simulated.waveform.gap_fill_after(0);
    const auto ingress_halo = live::detail::AudioInput::required_halo_samples(s.transfer.modem.sample_rate,
        live_profile.receiver.data_if_hz, live_profile.minimum_audio_hz, live_profile.maximum_audio_hz);
    const auto transport_halo = audio::native_roundtrip_filter_halo_samples(s.transfer.modem.sample_rate);
    check(!scheduled.waveform.config().scheduled_gap_fill && fill && fill->size(),
          "Scheduled gap-fill selection did not create an eligible source interval");
    check(simulated.waveform.config().receiver_filter_halo_samples == 0 &&
          simulated.waveform.config().gap_fill_transport_halo_samples == 0 &&
          adjusted.waveform.config().receiver_filter_halo_samples == 100 &&
          adjusted.waveform.config().gap_fill_transport_halo_samples == transport_halo,
          "Scheduled gap fill changed the actual complex simulation or live filter halo");
    for (const auto* profile : {&simulated, &live_profile, &adjusted}) {
        const auto& waveform = profile->waveform;
        const auto candidate = waveform.gap_fill_after(0);
        check(waveform.config().scheduled_gap_fill && !waveform.config().continuous_data &&
              waveform.transmit_slot(0) == scheduled.waveform.transmit_slot(0) &&
              waveform.transmit_slot(1) == scheduled.waveform.transmit_slot(1) &&
              waveform.slot(1).symbol_ordinal == calendar.initial_ordinal + 1 &&
              waveform.config().gap_fill_bounds &&
              waveform.config().gap_fill_bounds->receiver_filter_halo_samples == ingress_halo + transport_halo &&
              candidate && candidate->first == fill->first && candidate->stop == fill->stop,
              "Gap fill, live preparation or local capture uncertainty changed shared source coordinates");
    }
    const auto rejects = [](const auto& operation) {
        try { operation(); } catch (const Error& error) {
            return std::string_view(error.what()).starts_with("Scheduled Robust gap fill requires");
        }
        return false;
    };
    auto incompatible = s;
    incompatible.helper.power_w = 1;
    check(rejects([&] { live::validate_settings(incompatible); }) &&
          rejects([&] { live::detail::prepare_profile(incompatible, calendar, {}); }),
          "Scheduled gap fill accepted active Helper power");
    incompatible = s;
    incompatible.fhss_profile = "fake-fcc";
    check(rejects([&] { live::validate_settings(incompatible); }) &&
          rejects([&] { live::detail::prepare_profile(incompatible, calendar, {}); }),
          "Scheduled gap fill accepted FHSS");
}

void scheduled_bridge_profiles() {
    auto s = settings(false);
    s.symbol_burst_wait = 1;
    s.symbol_burst_random = 2;
    const auto calendar = *s.calendar;
    const auto scheduled = live::detail::prepare_profile(s, calendar, {});
    s.data_cadence = live::Settings{}.data_cadence;
    check(s.data_cadence == live::DataCadence::scheduled_bridge,
          "New native Robust settings did not select short transitions");
    live::validate_settings(s);
    const auto bridged = live::detail::prepare_profile(s, calendar, {});
    const auto reference = live::detail::with_capture_uncertainty(scheduled, .2, .1, 100);
    const auto adjusted = live::detail::with_capture_uncertainty(bridged, .2, .1, 100);
    const auto& waveform = adjusted.waveform;
    const auto expected = reference.waveform.slot(1);
    const auto actual = waveform.slot(1);
    check(!scheduled.waveform.config().scheduled_gap_bridge && waveform.config().scheduled_gap_bridge &&
          !waveform.config().scheduled_gap_fill && !waveform.config().continuous_data &&
          waveform.transmit_slot(0) == reference.waveform.transmit_slot(0) &&
          actual.tx == expected.tx && actual.symbol_ordinal == expected.symbol_ordinal &&
          actual.observation.first == expected.observation.first && actual.observation.stop == expected.observation.stop &&
          actual.quiet_aperture.first == expected.quiet_aperture.first && actual.quiet_aperture.stop == expected.quiet_aperture.stop,
          "Scheduled short transitions changed the calendar or ordinary receive geometry");
    for (const auto helper_on : {false, true}) for (const auto fhss_on : {false, true}) {
        auto configuration = s;
        configuration.added_symbols = 10;
        configuration.transfer.modem.carrier_hz = 1775;
        configuration.helper.power_w = helper_on ? .1 : 0;
        configuration.fhss_profile = fhss_on ? "fake-fcc" : "off";
        configuration.simulation = helper_on == fhss_on;
        live::validate_settings(configuration);
        const auto selected = live::detail::prepare_profile(configuration, calendar, {}, {.simulation_seed = 1});
        configuration.data_cadence = live::DataCadence::scheduled;
        const auto ordinary = live::detail::prepare_profile(configuration, calendar, {}, {.simulation_seed = 1});
        const auto& config = selected.waveform.config();
        const auto& reference = ordinary.waveform.config();
        check(config.scheduled_gap_bridge && !config.scheduled_gap_fill && !config.continuous_data &&
              config.helper_enabled == helper_on && config.helper_enabled == reference.helper_enabled &&
              config.format.fhss == fhss_on && config.format.fhss == reference.format.fhss &&
              config.format.quiet_seconds == reference.format.quiet_seconds &&
              config.format.ramp_seconds == reference.format.ramp_seconds &&
              config.format.dwell_seconds == reference.format.dwell_seconds &&
              selected.hop_shifts_hz == ordinary.hop_shifts_hz &&
              selected.waveform.transmit_slot(1) == ordinary.waveform.transmit_slot(1),
              "Short transitions changed the selected Scheduled Helper or FHSS profile");
    }
    auto bounded = s;
    bounded.receiver_memory_limit = waveform.emission_source_workspace_bytes();
    bool source_rejected = false;
    try { (void)live::detail::prepare_profile(bounded, calendar, {}); }
    catch (const Error& error) {
        source_rejected = std::string_view(error.what()) ==
            "Robust native waveform workspace exceeds the configured session memory limit";
    }
    check(*bounded.receiver_memory_limit > waveform.geometry().source_workspace_bytes && source_rejected,
          "Scheduled short transitions admitted a session limit with room for only its source caches");
}

void carrier_band_matches_profile() {
    auto s = settings(true);
    auto& modem = s.transfer.modem;
    const auto rejects = [](const auto& operation) {
        try { operation(); } catch (const Error&) { return true; }
        return false;
    };
    const auto near = [](double left, double right) { return std::abs(left - right) < 1e-9; };
    modem.bandwidth_hz = 1;
    modem.oscillator_search.reset();
    const auto low = live::detail::plan_carrier_band(s);
    low.validate(.5);
    check(near(low.waveform_bandwidth_hz, .625) && low.data_passband(.5).lower_hz > 0 &&
          low.data_passband(.5).lower_hz < 375, "A physical half-rate carrier inherited the speaker floor");
    check(rejects([&] { low.validate(.25); }) && rejects([&] { low.validate(2999.99); }),
          "Carrier preview accepted shaped DATA across DC or Nyquist");

    modem.bandwidth_hz = 3.6;
    const auto no_floor = live::detail::plan_carrier_band(s);
    s.minimum_carrier_spacing_hz = 125;
    const auto medium_floor = live::detail::plan_carrier_band(s);
    s.minimum_carrier_spacing_hz = 265;
    const auto wide_floor = live::detail::plan_carrier_band(s);
    check(near(no_floor.waveform_bandwidth_hz, 2.25) &&
          near(medium_floor.waveform_bandwidth_hz, 250.75) &&
          near(wide_floor.waveform_bandwidth_hz, 530.75) &&
          rejects([&] { wide_floor.validate(265); }),
          "Carrier placement did not refresh for the new spacing floor or accepted a DC crossing");
    s.selective_perturbation = "each-65";
    const auto spaced = live::detail::prepare_profile(s, *s.calendar, {});
    const auto& spaced_geometry = spaced.waveform.geometry();
    const auto first_slot = spaced.waveform.slot(0);
    check(near(spaced_geometry.carrier_spacing_hz, 265) &&
          near(spaced.waveform.placement().waveform_bandwidth_hz, 530.75) &&
          spaced.waveform.basis_format(first_slot).minimum_carrier_spacing_hz == 265 &&
          near(spaced.channel.effects.occupied_bandwidth_hz, 530.75) &&
          near(spaced.channel.selective_model.correlation_spacing_hz, .75),
          "Native profile did not use the wider waveform with the original selective channel scale");
    s.minimum_carrier_spacing_hz = 126;
    check(rejects([&] { live::validate_settings(s); }), "Robust accepted an unsupported spacing setting");
    s.minimum_carrier_spacing_hz = 0;

    modem.bandwidth_hz = 120;
    modem.carrier_hz = 1775;
    modem.oscillator_search = modem::OscillatorSearchConfig{
        .lf = {.1, 0}, .rf = {.01, 0}, .rf_shift_hz = 1e6, .margin = 1};
    s.added_symbols = 10;
    s.added_interleave = true;
    s.fhss_profile = "fake-fcc";
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
    s.helper.power_w = .004;
    constexpr double doppler_ratio = 1.000001;
    s.doppler_shift_hz = (modem.oscillator_search->rf_shift_hz + modem.carrier_hz) * (doppler_ratio - 1);
    const auto band = live::detail::plan_carrier_band(s, doppler_ratio);
    band.validate(modem.carrier_hz);
    check(band.helper_enabled && rejects([&] { band.validate(1200); }),
          "Enabled Helper preview did not enforce its fixed low-tone separation");
    const auto edge = band.data_passband(modem.carrier_hz);
    const auto native = live::detail::prepare_profile(s, *s.calendar, {});
    const auto& hop_format = native.waveform.config().format;
    const auto hop_envelope = [&](long double seconds) {
        return shaping_envelope(hop_format, native.waveform.geometry(), seconds);
    };
    check(hop_format.dwell_seconds == .4 && hop_format.quiet_seconds == .055 &&
          hop_envelope(.346L) == 0 && hop_envelope(.454L) == 0 &&
          hop_envelope(.344L) > 0 && hop_envelope(.456L) > 0 &&
          !native.waveform.config().scheduled_gap_bridge,
          "The default DATA cadence changed the original FHSS dwell guard or added a bridge");
    const auto& placement = native.waveform.placement();
    check(near(edge.lower_hz, native.minimum_data_hz) && near(edge.upper_hz, native.maximum_data_hz) &&
          near(band.waveform_bandwidth_hz, placement.waveform_bandwidth_hz + placement.gate_bandwidth_hz),
          "Carrier preview and native Helper placement disagree about shaped occupied DATA bandwidth");
    check(near(band.data_passband(band.minimum_carrier_hz(1400)).lower_hz, 1400),
          "Carrier planner could not solve its guarded lower edge");
    s.simulation = false;
    s.calendar->epoch = 1795081201;
    s.helper.power_w = 19.2; // Ratio 192. The nominal-envelope guard formerly admitted this odd dwell phase.
    check(rejects([&] { live::detail::plan_carrier_band(s).validate(modem.carrier_hz); }) &&
          rejects([&] { (void)live::detail::prepare_profile(s, *s.calendar, {}); }),
          "Carrier preview missed the actual odd-phase Helper leakage guard");
    --s.calendar->epoch;
    live::detail::plan_carrier_band(s).validate(modem.carrier_hz);
    (void)live::detail::prepare_profile(s, *s.calendar, {});
    s.simulation = true;
    check(rejects([&] { live::detail::plan_carrier_band(s).validate(modem.carrier_hz); }),
          "Simulation could advance to an odd epoch with a rejected Helper placement");

    s.fhss_profile = "off";
    s.helper.power_w = 0;
    s.added_symbols = 1;
    modem.bandwidth_hz = 100000;
    check(rejects([&] { (void)live::detail::plan_carrier_band(s); }),
          "Carrier preview ignored an unsupported waveform sample rate");
    modem.sample_rate = 192000;
    const auto gated = live::detail::plan_carrier_band(s);
    check(near(gated.waveform_bandwidth_hz, 62600),
          "Carrier preview dropped the canonical gate allowance when spectral certification could not narrow it");
}

void fractional_spacing_helper_profile() {
    auto s = settings(true);
    s.minimum_carrier_spacing_hz = 3.5;
    s.transfer.modem.bandwidth_hz = 3.6;
    s.transfer.modem.oscillator_search = modem::OscillatorSearchConfig{
        .lf = {.0001, .5}, .rf = {.0001, .5}, .rf_shift_hz = 30'000'000, .margin = 3};
    s.calendar = live::Calendar{1793850000, 7000037};
    s.target_cn0_db_hz = 32;
    s.tx_dbm = 3;
    s.path_loss_db = 90;
    s.noise_dbm_hz = -164;
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
    s.helper.power_w = 100;
    const auto preview = live::detail::plan_carrier_band(s);
    preview.validate(s.transfer.modem.carrier_hz);

    const auto profile = live::detail::prepare_profile(s, *s.calendar, {}, {.simulation_seed = 950101});
    const auto& waveform = profile.waveform;
    check(waveform.basis_format(waveform.slot(0)).minimum_carrier_spacing_hz == 3.5 &&
          waveform.geometry().carrier_spacing_hz == 3.5 &&
          waveform.geometry().occupied_bandwidth_hz == 7.75 && preview.waveform_bandwidth_hz == 7.75,
          "Robust fractional spacing was lost between the preview, waveform, and receiver template");
    check(std::abs(profile.receiver.helper.effective_power_w - 100) < 1e-9 &&
          profile.receiver.helper.frequency_hz == 2550 && std::abs(profile.maximum_audio_hz - 2920) < .02,
          "Robust fractional spacing changed the verified 100 W Helper placement");
}

void native_precision_profile_policy() {
    auto s = settings(true);
    const auto automatic = live::detail::prepare_profile(s, *s.calendar, {});
    s.transfer.modem.search_arithmetic = modem::SearchArithmetic::fp32;
    const auto fp32_min = live::detail::prepare_profile(s, *s.calendar, {});
    s.transfer.modem.search_arithmetic = modem::SearchArithmetic::fp64;
    const auto fp64_force = live::detail::prepare_profile(s, *s.calendar, {});
    check(automatic.receiver.native_coarse_precision == triad::v3::NativeCoarsePrecision::automatic &&
          fp32_min.receiver.native_coarse_precision == triad::v3::NativeCoarsePrecision::fp32 &&
          fp64_force.receiver.native_coarse_precision == triad::v3::NativeCoarsePrecision::fp64,
          "GUI search arithmetic did not map to the native receiver precision policy");

    s.gpu = true;
    bool rejected = false;
    try {
        live::validate_settings(s);
    } catch (const Error& error) {
        const std::string_view message(error.what());
        rejected = live::gpu_receiver_available()
            ? message.find("FP64 force cannot be combined with the GPU receiver") != std::string_view::npos
            : message.find("GPU reception is unavailable in this build (CUDA or Vulkan support required)") != std::string_view::npos;
    }
    check(rejected, "GPU plus FP64 force did not report the applicable explicit validation error");
}

void simulation_first_window_admission() {
    auto s = settings(true);
    const auto prepared = live::detail::prepare_profile(s, *s.calendar, {});
    triad::v3::StreamReceiverOptions options;
    options.workers = 1;
    options.slot_owners = 1;
    options.memory_limit_bytes = 1;
    bool rejected = false;
    try {
        triad::v3::detail::check_simulation_first_window_workspace(prepared.waveform, prepared.receiver, options);
    } catch (const Error& error) {
        const std::string_view message(error.what());
        rejected = message.find("first receive window requires") != std::string_view::npos &&
            message.find("Available receiver workspace is 0.00 MiB") != std::string_view::npos;
    }
    check(rejected, "Simulation first-window preflight admitted a DSP bound above the receiver limit");
    options.memory_limit_bytes = std::numeric_limits<std::size_t>::max();
    triad::v3::detail::check_simulation_first_window_workspace(prepared.waveform, prepared.receiver, options);

    s.data_cadence = live::DataCadence::continuous;
    s.transfer.modem.sample_rate = 100;
    s.transfer.modem.carrier_hz = 25;
    s.transfer.modem.bandwidth_hz = 36;
    const auto packed = live::detail::prepare_profile(s, *s.calendar, {});
    options.workers = 2;
    options.slot_owners = 2;
    options.memory_limit_bytes = 1;
    options.maximum_input_chunk_samples = 4096;
    check(packed.waveform.slot(1).observation.stop - packed.waveform.slot(0).observation.stop <
          options.maximum_input_chunk_samples,
          "Simulation preflight fixture needs two windows that can arrive in the same chunk");
    triad::v3::detail::check_simulation_first_window_workspace(packed.waveform, packed.receiver, options);
}

void configured_physical_carrier_context() {
    auto first_settings=settings(true);
    first_settings.transfer.modem.carrier_hz=1720;
    first_settings.transfer.modem.oscillator_search->rf_shift_hz=5;
    first_settings.added_symbols=10;
    first_settings.added_interleave=true;
    first_settings.fhss_profile="fake-list";
    first_settings.fhss_frequencies_hz={0,12};
    const auto first=live::detail::prepare_profile(first_settings,*first_settings.calendar,{});
    check(first.waveform.config().format.configured_carrier_hz==1725,
          "Robust profile omitted the configured baseband plus Shift carrier");

    // Equivalent baseband/Shift pairs denote the same configured carrier.
    auto equivalent_settings=first_settings;
    equivalent_settings.transfer.modem.carrier_hz=1723;
    equivalent_settings.transfer.modem.oscillator_search->rf_shift_hz=2;
    const auto equivalent=live::detail::prepare_profile(equivalent_settings,*equivalent_settings.calendar,{});
    auto format_a=first.waveform.config().format;
    auto format_b=equivalent.waveform.config().format;
    check(format_a.configured_carrier_hz==format_b.configured_carrier_hz,
          "equivalent configured carrier decompositions did not produce the same key context");
    format_a.encrypt=format_b.encrypt=false;
    triad::v3::PatternSource source_a(format_a,first.key,0), source_b(format_b,equivalent.key,0);
    for(std::uint64_t cell=0;cell<100;++cell)
        check(source_a.pattern_coefficient_at(cell)==source_b.pattern_coefficient_at(cell),
              "equivalent baseband/Shift settings changed the triple Robust keystream");

    // Hopping and Doppler alter operating shifts/searches but keep the base
    // configured carrier context fixed.
    auto moved_settings=first_settings;
    moved_settings.fhss_profile="fake-list";
    moved_settings.fhss_frequencies_hz={0,24};
    moved_settings.doppler_shift_hz=3;
    const auto moved=live::detail::prepare_profile(moved_settings,*moved_settings.calendar,{});
    auto format_c=moved.waveform.config().format;
    check(format_c.configured_carrier_hz==format_a.configured_carrier_hz &&
          moved.hop_shifts_hz!=first.hop_shifts_hz,
          "hop or Doppler settings replaced the configured carrier key coordinate");
    format_c.encrypt=false;
    triad::v3::PatternSource source_c(format_c,moved.key,0);
    std::uint64_t first_payload_cell=0;
    while(first_payload_cell<source_a.geometry().fine_cells &&
          source_a.pattern_coefficient_at(first_payload_cell)==std::array<triad::v3::Complex,3>{})
        ++first_payload_cell;
    check(first_payload_cell<source_a.geometry().fine_cells,
          "FHSS profile produced no active carrier-key fixture cell");
    for(std::uint64_t cell=first_payload_cell;cell<first_payload_cell+100;++cell)
        check(source_a.pattern_coefficient_at(cell)==source_c.pattern_coefficient_at(cell),
              "hop or Doppler position changed the configured-carrier keystream");
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

void robust_rate_fits_typical_converter() {
    auto low_rate = settings(true);
    low_rate.transfer.modem.bandwidth_hz = 2400;
    const auto low = live::detail::prepare_profile(low_rate, *low_rate.calendar, {}, {.simulation_seed = 1});
    auto high_rate = low_rate;
    high_rate.transfer.modem.bandwidth_hz = 3600;
    const auto high = live::detail::prepare_profile(high_rate, *high_rate.calendar, {}, {.simulation_seed = 1});
    high_rate.transfer.modem.sample_rate = 8000;
    const auto wide = live::detail::prepare_profile(high_rate, *high_rate.calendar, {}, {.simulation_seed = 1});
    check(low.maximum_audio_hz <= 2520 && high.maximum_audio_hz > 2520 && wide.maximum_audio_hz <= 3360,
          "Robust passband plan did not distinguish the 6 kHz default and 8 kHz wide profile");

    auto live_settings = settings(false);
    live_settings.transfer.modem.bandwidth_hz = 3600;
    live_settings.live_noise_dbfs_hz = -100;
    fixture::capture_passband_override = 2520;
    live::Session session;
    session.start(live_settings);
    const auto failed = wait(session, [](const auto& snapshot) { return !snapshot.receiver_error.empty(); },
                             "A narrow Robust input passband was not rejected");
    check(failed.receiver_error.find("requires an upper edge of") != std::string::npos &&
          failed.receiver_error.find("usable passband of 2520 Hz") != std::string::npos &&
          failed.receiver_error.find("logical rate 6000 Hz, hardware rate 6000 Hz") != std::string::npos &&
          failed.receiver_error.find("Reduce Rate") != std::string::npos,
          "Robust passband rejection omitted the needed edge, available passband, rates, or remedy");
    close(session);
    fixture::capture_passband_override = 0;

    live_settings.live_noise_dbfs_hz.reset();
    fixture::playback_passband_override = 2520;
    live::Session transmit;
    transmit.start(live_settings);
    ready(transmit);
    transmit.transmit_bits(std::array<std::uint8_t, 1>{1});
    const auto rejected = wait(transmit, [](const auto& snapshot) { return !snapshot.transmitting; },
                               "A narrow Robust output passband was not rejected");
    check(rejected.error.find("Robust transmit band requires an upper edge") != std::string::npos &&
          rejected.error.find("usable passband of 2520 Hz") != std::string::npos,
          "Robust transmit passband rejection omitted the device and required bands");
    close(transmit);
    fixture::playback_passband_override = 0;
}

void capture_backpressure() {
    // Lock handoff makes the test deterministic: after the producer signals
    // while owning the queue lock, the main thread can acquire that lock only
    // once the full-queue wait has released it.
    for (const bool cancel : {false, true}) {
        live::detail::CaptureQueue queue;
        queue.capture_callback_may_block = true;
        const std::array<float, 2> full{1, 2};
        const std::array<float, 1> next{3};
        {
            std::unique_lock lock(queue.mutex);
            check(queue.push(lock, full, 2, 2, {}, {}), "Could not fill bounded capture queue");
        }
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        std::promise<bool> result;
        auto result_future = result.get_future();
        std::jthread producer([&](std::stop_token stop) {
            std::unique_lock lock(queue.mutex);
            entered.set_value();
            result.set_value(queue.push(lock, next, 2, 2, {}, stop));
        });
        entered_future.wait();
        {
            std::unique_lock lock(queue.mutex);
            check(result_future.wait_for(0s) == std::future_status::timeout,
                  "Full buffered queue did not park its producer");
            if (cancel) producer.request_stop();
            else {
                const auto removed = queue.pop();
                check(removed.pcm == std::vector<float>({1, 2}), "Queue changed captured PCM");
            }
        }
        producer.join();
        check(result_future.get() == !cancel, "Blocked capture did not resume or cancel correctly");
        std::unique_lock lock(queue.mutex);
        if (!cancel) {
            const auto resumed = queue.pop();
            check(resumed.first == 2 && resumed.pcm == std::vector<float>({3}),
                  "Resumed capture changed its sample coordinate or PCM");
        }
    }
    // The fake provider offers a bounded, dated source as quickly as it can.
    // Ordinary device capture must still report the same overrun; a provider
    // that can retain samples may instead wait and must wake on cancellation.
    auto run = [](bool may_block, bool oversized) {
        auto s = settings(false);
        s.transfer.modem.sample_rate = 1500;
        s.transfer.modem.carrier_hz = 500;
        s.live_noise_dbfs_hz = -100;
        const auto count = s.transfer.modem.sample_rate / 20 + (oversized ? 1U : 0U);
        auto input = std::make_shared<fixture::CaptureInput>();
        input->samples.resize(count);
        input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
        input->callback_may_block = may_block;
        input->repeat = true;
        input->chunk_size = count;
        fixture::callback_started = fixture::callback_finished = 0;
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input = input;
        }
        live::Session session;
        session.start(s);
        if (!may_block || oversized) {
            const auto out = wait(session, [=](const auto& snapshot) {
                return snapshot.receiver_error.find(oversized ? "chunk exceeds" : "backlog overrun") != std::string::npos;
            }, "Capture did not reject an unbufferable input", 20s);
            check(out.received.empty(), "Capture overrun fabricated a completed reception");
        } else {
            wait(session, [](const auto&) { return fixture::capture_active == 1; },
                 "Buffered capture did not start");
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (std::chrono::steady_clock::now() < deadline && fixture::callback_finished < 100 &&
                   fixture::capture_active)
                std::this_thread::sleep_for(2ms);
            const bool waited = fixture::callback_finished >= 100 && fixture::capture_active;
            const auto snapshot = session.snapshot();
            check(waited, "Buffered capture did not wait for receiver capacity: " + snapshot.status + "; " +
                  snapshot.error + "; " + snapshot.receiver_error + "; callbacks " +
                  std::to_string(fixture::callback_started.load()) + "/" +
                  std::to_string(fixture::callback_finished.load()));
            check(session.snapshot().receiver_error.empty(), "Buffered capture reported an overrun");
        }
        close(session);
        check(fixture::callback_started == fixture::callback_finished && fixture::capture_active == 0,
              "Capture cancellation left a callback blocked");
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input.reset();
        }
    };
    run(false, false);
    run(true, false);
    run(true, true);
}

void finite_duplex_plan() {
    using live::detail::finite_capture_frames;
    check(finite_capture_frames(100, 20, 7) == 87, "Finite capture endpoint lost filter halo");
    check(finite_capture_frames(100, -20, 7) == 127, "Finite capture endpoint lost negative origin");
    check(finite_capture_frames(100, 110, 7) == 0, "Finite capture endpoint failed to clamp past input");
    check(finite_capture_frames(100, 105, 7) == 2, "Finite capture endpoint lost a partial halo");
    check(finite_capture_frames(0, std::numeric_limits<std::int64_t>::min(), 0) == (UINT64_C(1) << 63),
          "Finite capture endpoint mishandled the minimum signed coordinate");
    bool overflow = false;
    try { (void)finite_capture_frames(UINT64_MAX, -1, 1); } catch (const Error&) { overflow = true; }
    check(overflow, "Finite capture endpoint wrapped its coordinate");

    for (const bool reject : {false, true}) {
        auto s = settings(false);
        s.transfer.modem.sample_rate = 1500;
        s.transfer.modem.carrier_hz = 500;
        s.live_noise_dbfs_hz = -100;
        s.full_duplex = true;
        s.finite_duplex = true;
        auto input = std::make_shared<fixture::CaptureInput>();
        input->samples.resize(s.transfer.modem.sample_rate / 20);
        input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
        input->callback_may_block = input->finite = input->repeat = true;
        input->chunk_size = input->samples.size();
        const auto planning_profile = live::detail::prepare_profile(s, *s.calendar, {});
        const live::detail::AudioInput planning_converter(s.transfer.modem.sample_rate,
            planning_profile.receiver.data_if_hz, planning_profile.minimum_audio_hz, planning_profile.maximum_audio_hz);
        const auto planning_adjusted = live::detail::with_capture_uncertainty(planning_profile,
            1e-7 + 1. / s.transfer.modem.sample_rate,
            2 * planning_profile.waveform.config().audio_pair_ppm * 1e-6, planning_converter.halo_samples());
        input->pause_after = 2U * s.transfer.modem.sample_rate +
            planning_adjusted.waveform.slot(1).observation.stop + 2 * planning_converter.halo_samples();
        fixture::pause_released = false;
        fixture::capture_paused = false;
        fixture::hold_capture = true;
        fixture::hold_playback = true;
        fixture::capture_endpoint = 0;
        fixture::reject_capture_endpoint = reject;
        const auto opens = fixture::playback_opens.load();
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input = input;
        }
        live::Session session;
        session.start(s);
        ready(session);
        wait(session, [](const auto&) { return fixture::capture_active == 1; }, "Finite capture did not open");
        session.transmit_bits(std::array<std::uint8_t,1>{1});
        std::this_thread::sleep_for(80ms);
        check(fixture::playback_opens == opens && !fixture::capture_endpoint,
              "Finite TX opened playback before actual quiet rearm");
        fixture::hold_capture = false;
        if (reject) {
            const auto failed = wait(session, [](const auto& snapshot) {
                return !snapshot.transmitting && !snapshot.error.empty();
            }, "Rejected finite endpoint did not stop TX", 30s);
            check(failed.error.find("refused finite capture endpoint") != std::string::npos &&
                  !fixture::capture_endpoint, "Finite endpoint refusal was hidden");
            std::lock_guard lock(fixture::mutex);
            check(!fixture::scheduled && fixture::playback_samples == 0,
                  "Rejected finite endpoint exposed scheduled playback or samples");
        } else {
            wait(session, [](const auto&) { return fixture::capture_endpoint.load() != 0; },
                 "Finite provider did not receive endpoint", 30s);
            wait(session, [](const auto&) {
                std::lock_guard lock(fixture::mutex);
                return fixture::scheduled_epoch.has_value();
            }, "Finite playback was not scheduled", 30s);
            long double epoch;
            {
                std::lock_guard lock(fixture::mutex);
                epoch = *fixture::scheduled_epoch;
            }
            const auto base = live::detail::prepare_profile(s, *s.calendar, {});
            const auto rate = s.transfer.modem.sample_rate;
            const live::detail::AudioInput converter(rate, base.receiver.data_if_hz,
                                                     base.minimum_audio_hz, base.maximum_audio_hz);
            const auto age = input->first_utc - base.waveform.origin().epoch;
            const auto adjusted = live::detail::with_capture_uncertainty(base, 1e-7 + 1. / rate,
                static_cast<double>(std::abs(age) * base.waveform.config().audio_pair_ppm * 1e-6L),
                converter.halo_samples());
            const auto capture_first = std::llround((age + adjusted.waveform.config().capture_lead_seconds) * rate);
            const auto seconds = static_cast<std::uint64_t>(std::floor(epoch));
            const auto phase = static_cast<std::uint32_t>(std::llround((epoch - seconds) * rate));
            const auto first_slot = base.waveform.first_slot_not_before({seconds, phase});
            const auto expected = finite_capture_frames(adjusted.waveform, first_slot, capture_first,
                                                        converter.halo_samples());
            check(fixture::capture_endpoint == expected,
                  "Finite capture endpoint differs from actual adjusted receive geometry");
            session.cancel_transmit();
        }
        fixture::hold_capture = fixture::hold_playback = false;
        fixture::pause_released = true;
        close(session);
        fixture::reject_capture_endpoint = false;
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input.reset();
        }
    }
    // Cancellation before any capture data must not open playback or create
    // a physical completion. EOF before rearm has the same no-transmit rule.
    for (const bool cancel_before_input : {true, false}) {
        auto s = settings(false);
        s.transfer.modem.sample_rate = 1500;
        s.transfer.modem.carrier_hz = 500;
        s.live_noise_dbfs_hz = -100;
        s.full_duplex = s.finite_duplex = true;
        auto input = std::make_shared<fixture::CaptureInput>();
        input->callback_may_block = input->finite = true;
        input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
        if (cancel_before_input) {
            input->samples.resize(75);
            input->repeat = true;
            fixture::hold_capture = true;
        }
        const auto opens = fixture::playback_opens.load();
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input = input;
        }
        live::Session session;
        session.start(s);
        ready(session);
        if (!cancel_before_input)
            wait(session, [](const auto& out) {
                return out.receiver_error == "Finite Robust input ended before physical completion.";
            },
                 "Finite EOF was not reported");
        session.transmit_bits(std::array<std::uint8_t,1>{1});
        if (cancel_before_input) session.cancel_transmit();
        const auto result = wait(session, [](const auto& out) { return !out.transmitting; },
                                 "Finite wait did not stop");
        check(fixture::playback_opens == opens && result.received.empty() && result.signals.empty(),
              "Cancelled or truncated finite capture produced playback or completion");
        if (!cancel_before_input)
            check(result.error.find("capture ended") != std::string::npos,
                  "Finite EOF did not report incomplete capture");
        fixture::hold_capture = false;
        close(session);
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input.reset();
        }
    }

    // While the physical provider is opening or acknowledging the cutoff,
    // a new received DATA slot revokes the earlier quiet rearm. It must be
    // observed before private TX PCM can be exposed.
    for (const bool hold_ack : {false, true}) {
        using namespace triad::v3;
        auto s = settings(false);
        s.transfer.modem.sample_rate = 1500;
        s.transfer.modem.carrier_hz = 500;
        s.transfer.audio_timing_error_seconds = 0;
        s.live_noise_dbfs_hz = -100;
        s.full_duplex = s.finite_duplex = true;
        const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
        const auto rate = s.transfer.modem.sample_rate;
        const live::detail::AudioInput converter(rate, profile.receiver.data_if_hz,
                                                 profile.minimum_audio_hz, profile.maximum_audio_hz);
        const auto adjusted = live::detail::with_capture_uncertainty(profile, 1e-7 + 1. / rate,
            2 * profile.waveform.config().audio_pair_ppm * 1e-6, converter.halo_samples());
        const auto prefix = 2U * rate;
        auto input = std::make_shared<fixture::CaptureInput>();
        input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
        input->callback_may_block = input->finite = true;
        input->chunk_size = rate / 20;
        input->pause_after = prefix + adjusted.waveform.slot(1).observation.stop + 2 * converter.halo_samples();
        const auto end = prefix + adjusted.waveform.slot(3).observation.stop + rate;
        check(end < 400000 && *input->pause_after < end, "Finite interleaving fixture exceeds its budget: " +
              std::to_string(*input->pause_after) + "/" + std::to_string(end));
        input->samples.resize(end);
        const auto density = std::pow(10., *s.live_noise_dbfs_hz / 10);
        const auto gain = std::sqrt(density * std::pow(10., s.target_cn0_db_hz / 10));
        std::mt19937_64 rng(0x66696e697465ULL);
        std::normal_distribution<double> noise(0, std::sqrt(rate / 2. * density));
        for (auto& value : input->samples) value = static_cast<float>(noise(rng));
        const std::array<std::uint8_t, 1> bit{1};
        Emission emission(profile.waveform, profile.key, {EmissionMode::Message, 3, bit});
        std::array<Complex, 512> iq{};
        std::size_t at = 0;
        for (;;) {
            const auto count = emission.read(iq);
            if (!count) break;
            check(prefix + at + count <= input->samples.size(), "Finite interference source exceeds its capture");
            for (std::size_t i = 0; i < count; ++i) {
                const auto rotation = std::polar(1., 2 * std::numbers::pi * s.transfer.modem.carrier_hz *
                                                     static_cast<double>(at + i) / rate);
                input->samples[prefix + at + i] += static_cast<float>(std::numbers::sqrt2 * gain *
                                                                       (iq[i] * rotation).real());
            }
            at += count;
        }
        fixture::pause_released = false;
        fixture::capture_paused = fixture::endpoint_started = false;
        fixture::hold_playback_format = !hold_ack;
        fixture::hold_endpoint_ack = hold_ack;
        const auto opens = fixture::playback_opens.load();
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input = input;
        }
        live::Session session;
        session.start(s);
        ready(session);
        session.transmit_bits(bit);
        wait(session, [&](const auto&) {
            return fixture::capture_paused && (hold_ack ? fixture::endpoint_started.load() :
                                                           fixture::playback_opens.load() > opens);
        }, "Finite playback did not reach the intended barrier", 30s);
        fixture::pause_released = true;
        wait(session, [](const auto& out) {
            return std::any_of(out.pattern_steps.begin(), out.pattern_steps.end(), [](const auto& row) {
                return row.index == 3 && row.decision == live::PatternDecision::data;
            });
        }, "Finite interleaving DATA was not committed", 30s);
        fixture::hold_playback_format = fixture::hold_endpoint_ack = false;
        const auto failed = wait(session, [](const auto& out) { return !out.transmitting && !out.error.empty(); },
                                 "Finite TX ignored the changed receiver state", 30s);
        check(!failed.error.empty(), "Finite TX did not report the revoked quiet state");
        {
            std::lock_guard lock(fixture::mutex);
            check(!fixture::scheduled && fixture::playback_samples == 0,
                  "Finite TX exposed playback despite revoked quiet state");
        }
        close(session);
        {
            std::lock_guard lock(fixture::mutex);
            fixture::capture_input.reset();
        }
    }
}

void finite_duplex_completion_case(bool silence, bool mute_later_tx, bool short_capture) {
    auto s = settings(false);
    s.transfer.modem.sample_rate = 1500;
    s.transfer.modem.carrier_hz = 500;
    s.transfer.audio_timing_error_seconds = 0;
    s.live_noise_dbfs_hz = -100;
    s.receiver_memory_limit = 1024U * 1024 * 1024;
    s.full_duplex = s.finite_duplex = true;
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
    const auto rate = s.transfer.modem.sample_rate;
    const live::detail::AudioInput converter(rate, profile.receiver.data_if_hz,
                                             profile.minimum_audio_hz, profile.maximum_audio_hz);
    const auto adjusted = live::detail::with_capture_uncertainty(profile, 1e-7 + 1. / rate,
        2 * profile.waveform.config().audio_pair_ppm * 1e-6, converter.halo_samples());
    auto input = std::make_shared<fixture::CaptureInput>();
    input->samples.resize(rate / 20);
    input->chunk_size = input->samples.size();
    input->repeat = input->callback_may_block = input->finite = input->loopback = true;
    input->loopback_silence = silence;
    input->short_after_cutoff = short_capture;
    if (mute_later_tx) {
        const auto first = profile.waveform.transmit_slot(0);
        input->loopback_playback_limit = profile.waveform.calendar().distance(first.helper_begin, first.support_end);
    }
    input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
    input->pause_after = 2U * rate + adjusted.waveform.slot(1).observation.stop + 2 * converter.halo_samples();
    fixture::pause_released = false;
    fixture::loopback_ready = false;
    fixture::capture_endpoint = fixture::capture_position = 0;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = input;
    }
    live::Session session;
    session.start(s);
    ready(session);
    const std::array<std::uint8_t,2> bits{1,0};
    session.transmit_bits(std::span(bits).first(mute_later_tx ? 2 : 1));
    bool received = false;
    const auto drained = wait(session, [&](const auto& out) {
        for (const auto& signal : out.signals)
            if (signal.complete && signal.robust_reception && signal.raw_bits == "1") received = true;
        check(out.error.empty(), "Finite loopback TX failed: " + out.error);
        return out.status == "Finite Robust capture drained after physical completion" ||
               !out.receiver_error.empty();
    }, "Finite loopback did not finish", 45s);
    check(fixture::capture_endpoint > 0 && fixture::capture_position + (short_capture ? 1U : 0U) == fixture::capture_endpoint,
          "Finite capture fixture did not stop at its intended frame: " +
          std::to_string(fixture::capture_position.load()) + "/" + std::to_string(fixture::capture_endpoint.load()) +
          " silence=" + std::to_string(silence) + " muted=" + std::to_string(mute_later_tx) +
          " short=" + std::to_string(short_capture) + "; status=" + drained.status +
          "; receiver=" + drained.receiver_error);
    if (mute_later_tx) check(received, "Earlier physically completed message was not observed");
    if (silence) check(!received, "Silent capture unexpectedly produced a completed message");
    if (short_capture) {
        check(drained.receiver_error == (received ? "Finite Robust input ended before its planned capture endpoint." :
                                                    "Finite Robust input ended before physical completion."),
              "Short finite capture was not reported distinctly");
    } else
    if (silence)
        check(!received && drained.receiver_error == "Finite Robust capture ended without a physically complete message.",
              "An exact finite endpoint fabricated receiver completion");
    else
        check(drained.receiver_error.empty() && received &&
              drained.status == "Finite Robust capture drained after physical completion",
              "Exact finite capture did not retain its physically scored completion: " + drained.receiver_error);
    close(session);
    fixture::pause_released = true;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
        fixture::loopback_pcm.clear();
    }
}

void finite_duplex_completion() {
    finite_duplex_completion_case(false, false, false);
    finite_duplex_completion_case(true, false, false);
    finite_duplex_completion_case(false, true, false);
    finite_duplex_completion_case(false, true, true);
    finite_duplex_completion_case(true, false, true);
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
    check(stopped.received.empty() && stopped.pattern_steps.empty() && stopped.signals.empty() &&
          !stopped.transmit_trace.emitting,
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

void long_live_prefix_and_cancel(bool duplex) {
    using namespace triad::v3;
    auto s = settings(false);
    s.transfer.modem.bandwidth_hz = 3600;
    s.transfer.modem.carrier_hz = 1700; // Leave audio passband room for the HF and Doppler bounds.
    s.transfer.fec = FecMode::rs20;
    s.transfer.key = Crypto::amateur();
    s.full_duplex = duplex;
    // These settings bound the receiver's HF search. With Simulation off,
    // physical playback must still be the canonical, unperturbed emission.
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
    s.doppler_shift_hz = 8;
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
    check(trace.emitting && trace.active && !trace.raw && trace.total_wire_bits == message_interval_bits &&
          trace.source.size() == modem::TransmitTrace::source_limit &&
          trace.wire_plain_bits.size() == modem::TransmitTrace::bit_limit,
          "Long Robust TX lost its exact interval extent or bounded trace");
    check(trace.pattern && trace.pattern->inner_coefficients > 0 &&
          trace.pattern->prefixes[0].size() == 32 && trace.pattern->prefixes[1].size() == 32 &&
          trace.pattern->prefixes[0] != trace.pattern->prefixes[1],
          "Long Robust TX did not retain two distinct bounded current-frame pattern previews");
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
    check(profile.receiver.frequency_bound_hz == 8 &&
          profile.receiver.time_varying_common_doppler_hz == 38 &&
          std::abs(profile.receiver.spread_delay_seconds - .007) < 1e-12 &&
          std::abs(profile.receiver.spread_doppler_hz - 30.) < 1e-12 &&
          profile.receiver.selective_slow_depth == .65 &&
          profile.receiver.selective_fast_depth == .65,
          "Doppler / HF Worst / Each 65% did not remain configured as receive-side search bounds");
    const auto& calendar = profile.waveform;
    const auto slot = calendar.first_slot_not_before({epoch, 0});
    const auto scheduled = calendar.transmit_slot(slot);
    const auto start = scheduled.helper_begin;
    check(start == HelperBurstCoordinate{epoch,0}, "Long Robust TX began outside its configured public slot");
    const auto addressed = calendar.basis_format(calendar.slot(slot));
    check(trace.pattern->epoch == scheduled.pattern_begin.epoch &&
          trace.pattern->ordinal == calendar.config().format.symbol_ordinal + slot &&
          trace.pattern->inner_coefficients == calendar.geometry().inner_coefficients &&
          trace.pattern->prefixes == PatternSource::inner_pattern_prefixes(addressed, profile.key, 16),
          "Prepared Robust frame and displayed pattern address, complexity or prefixes disagree");
    const auto first = calendar.calendar().distance(calendar.origin(), start);
    constexpr std::array<std::uint8_t, 1> first_marker_bit{1};
    Emission expected(calendar, profile.key, {EmissionMode::Message, slot, first_marker_bit, 1, first, {}});
    std::vector<Complex> iq(played.size());
    check(expected.read(iq) == played.size(), "The reference first marker bit did not cover the PCM prefix");
    const auto scale = .5 / (std::sqrt(2.) * profile.maximum_unscaled_data * expected.report().data_gain);
    unsigned active = 0;
    for (std::size_t i = 0; i < played.size(); ++i) {
        const auto sample = live::detail::real_pcm(iq[i], first + i, s.transfer.modem.carrier_hz,
            s.transfer.modem.sample_rate, scale);
        check(std::abs(sample - played[i]) < 1e-6, "Long Robust playback emitted a different first waveform bit");
        if (std::abs(played[i]) > 1e-4) ++active;
    }
    check(active > 64, "The long Robust playback fixture observed only an idle prefix");
    session.cancel_transmit();
    const auto ended = wait(session, [](const auto& out) {
        return !out.transmitting && fixture::playback_active == 0;
    }, "Cancelling the long Robust draft did not stop playback");
    check(!ended.transmit_trace.emitting && ended.received.empty() && ended.pattern_steps.empty() &&
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

void nonstationary_channel_profile() {
    for (const auto simulation : {false, true}) {
        for (const auto duplex : {false, true}) {
            auto s = settings(simulation);
            s.full_duplex = duplex;
            s.spread_preset = "hf-worst";
            s.selective_perturbation = "each-65";
            s.doppler_shift_hz = 8;
            const auto profile = live::detail::prepare_profile(s, *s.calendar, {},
                {.simulation_seed = 37});
            check(profile.receiver.time_varying_channel &&
                  profile.receiver.time_varying_common_doppler_hz == 38 &&
                  profile.receiver.time_varying_atmospheric_hz == 30 &&
                  std::abs(profile.receiver.spread_delay_seconds - .007) < 1e-12 &&
                  std::abs(profile.receiver.spread_doppler_hz - 30.) < 1e-12 &&
                  profile.receiver.selective_slow_depth == .65 &&
                  profile.receiver.selective_fast_depth == .65,
                  "HF channel choices did not retain their receiver bounds and variation state");
            check(profile.channel.path_evolution.bounded_spread_knot_seconds > 0 &&
                  profile.channel.path_evolution.common_doppler_wander_hz == 38 &&
                  profile.channel.effects.static_doppler_hz == 0 &&
                  profile.channel.selective_model.coefficient_law == triad::v3::SelectiveCoefficientLaw::GaussianErf &&
                  profile.channel.selective_model.modulation_rate_hz ==
                      profile.receiver.selective_modulation_rate_hz / 10,
                  "HF simulation did not use the bounded evolving path and selective channel models");
        }
    }

    auto atmospheric = settings(true);
    atmospheric.spread_preset = "hf-worst";
    atmospheric.selective_perturbation = "each-65";
    atmospheric.doppler_shift_hz = 0;
    const auto varying = live::detail::prepare_profile(atmospheric, *atmospheric.calendar, {},
        {.simulation_seed = 37});
    check(varying.receiver.frequency_bound_hz == 0 &&
          varying.receiver.time_varying_atmospheric_hz == 30 &&
          varying.receiver.time_varying_common_doppler_hz == 30 &&
          varying.channel.path_evolution.common_doppler_wander_hz == 30 &&
          varying.waveform.config().cfo_bound_hz == 30,
          "Atmospheric search was lost with exact oscillators and zero explicit Doppler");

    auto off = settings(true);
    off.spread_preset = "none";
    off.selective_perturbation = "off";
    off.doppler_shift_hz = 0;
    const auto profile = live::detail::prepare_profile(off, *off.calendar, {},
        {.simulation_seed = 37});
    check(!profile.receiver.time_varying_channel &&
          profile.receiver.time_varying_common_doppler_hz == 0 &&
          profile.receiver.time_varying_atmospheric_hz == 0 &&
          profile.receiver.spread_delay_seconds == 0 && profile.receiver.spread_doppler_hz == 0 &&
          profile.receiver.selective_modulation_rate_hz == 0 &&
          profile.channel.path_evolution.bounded_spread_knot_seconds == 0 &&
          profile.channel.path_evolution.common_doppler_wander_hz == 0 &&
          profile.channel.effects.static_doppler_hz == 0 &&
          profile.channel.effects.spread_delay_ms == 0 &&
          profile.channel.effects.spread_doppler_hz == 0 &&
          profile.channel.effects.perturbation_slow_depth == 0 &&
          profile.channel.effects.perturbation_fast_depth == 0 &&
          profile.channel.selective_model.coefficient_law == triad::v3::SelectiveCoefficientLaw::GaussianErf &&
          profile.channel.selective_model.modulation_rate_hz == 0,
          "Off must bypass all dynamic path and selective channel effects");
}

void midlatitude_hf_profile_uses_nominal_gaussian_rays() {
    auto s = settings(true);
    s.spread_preset = "hf-midlatitude-moderate";
    s.selective_perturbation = "off";
    s.doppler_shift_hz = 0;
    auto oscillator = *s.transfer.modem.oscillator_search;
    oscillator.margin = 3;
    s.transfer.modem.oscillator_search = oscillator;

    const auto band = live::detail::plan_carrier_band(s);
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {}, {.simulation_seed = 37});
    const auto carrier = s.transfer.modem.carrier_hz;
    const auto guarded = band.data_passband(carrier);
    const auto slot = triad::v3::make_receiver_slot_plan(profile.waveform, profile.receiver, 0);
    check(std::abs(profile.receiver.spread_delay_seconds - .001) < 1e-12 &&
          profile.receiver.spread_doppler_hz == 0 &&
          std::abs(profile.receiver.random_doppler_width_hz - .5) < 1e-12 &&
          std::abs(profile.receiver.time_varying_atmospheric_hz - 3.) < 1e-12 &&
          profile.receiver.time_varying_common_doppler_hz == .25 &&
          profile.receiver.time_varying_channel,
          "Midlatitude HF receiver did not retain nominal 1 ms Gaussian rays and the Margin-3 public support");
    const auto drift = triad::v3::receiver_detail::drift_plan(profile.receiver, slot, 2);
    check(drift.workspace_bytes > 0 && drift.slope_radius > 0,
          "Midlatitude HF did not quote the bounded drift fallback when simulated common wander was zero");
    check(profile.waveform.config().spread_delay_ms == 3 &&
          profile.waveform.config().spread_doppler_hz == 0 &&
          std::abs(profile.waveform.config().cfo_bound_hz - 3.) < 1e-12 &&
          profile.channel.effects.spread_delay_ms == 1 &&
          profile.channel.effects.spread_doppler_hz == 0 &&
          std::abs(profile.channel.path_evolution.random_doppler_width_hz - .5) < 1e-12 &&
          profile.channel.path_evolution.bounded_spread_knot_seconds == 0 &&
          profile.channel.path_evolution.common_doppler_wander_hz == 0,
          "Margin changed the physical Gaussian rays or added a bounded echo/common wander");
    auto nominal_settings = s;
    oscillator.margin = 1;
    nominal_settings.transfer.modem.oscillator_search = oscillator;
    const auto nominal = live::detail::prepare_profile(nominal_settings, *s.calendar, {},
        {.simulation_seed = 37});
    const auto nominal_slot = triad::v3::make_receiver_slot_plan(nominal.waveform, nominal.receiver, 0);
    check(nominal.receiver.time_varying_atmospheric_hz == 0 &&
          nominal.receiver.time_varying_common_doppler_hz == 0 &&
          nominal.channel.path_evolution.common_doppler_wander_hz == 0 &&
          triad::v3::receiver_detail::drift_plan(nominal.receiver, nominal_slot, 2).workspace_bytes == 0,
          "Nominal Gaussian support added a drift proposal beyond the public center allowance");
    const auto capture_adjusted = live::detail::with_capture_uncertainty(profile, 0, 0, 100);
    check(capture_adjusted.receiver.capture_tail_bound_seconds >= .003 +
              100. / profile.waveform.effective_sample_rate(),
          "Capture settling omitted the Margin-expanded public 3 ms delay bound");
    check(std::abs(profile.minimum_data_hz - guarded.lower_hz) < 1e-9 &&
          std::abs(profile.maximum_data_hz - guarded.upper_hz) < 1e-9,
          "CarrierBand and receiver audio guard reserved different Gaussian-ray support");
    auto unspread_settings = s;
    unspread_settings.spread_preset = "none";
    const auto unspread = live::detail::plan_carrier_band(unspread_settings).data_passband(carrier);
    check(std::abs((unspread.lower_hz - guarded.lower_hz) - 4.5) < 1e-9 &&
          std::abs((guarded.upper_hz - unspread.upper_hz) - 4.5) < 1e-9 &&
          std::abs((unspread.lower_hz - profile.minimum_data_hz) - 4.5) < 1e-9 &&
          std::abs((profile.maximum_data_hz - unspread.upper_hz) - 4.5) < 1e-9,
          "Margin-3 Gaussian full skirts did not retain 3D times Margin per edge");
    auto perturbed_settings = s;
    perturbed_settings.selective_perturbation = "each-65";
    const auto perturbed = live::detail::prepare_profile(perturbed_settings, *s.calendar, {},
        {.simulation_seed = 37});
    const auto perturbed_band = live::detail::plan_carrier_band(perturbed_settings).data_passband(carrier);
    check(std::abs(perturbed.receiver.selective_modulation_rate_hz - 1.5) < 1e-12 &&
          perturbed.receiver.selective_slow_depth == .65 && perturbed.receiver.selective_fast_depth == .65 &&
          std::abs(perturbed.channel.selective_model.modulation_rate_hz.value_or(0.) - .15) < 1e-12 &&
          std::abs((guarded.lower_hz - perturbed_band.lower_hz) - 1.5) < 1e-9 &&
          std::abs((perturbed_band.upper_hz - guarded.upper_hz) - 1.5) < 1e-9,
          "Explicit selective perturbation no longer adds the Gaussian-width modulation allowance");
}

void simulation_transmit_ignores_realized_channel() {
    using namespace triad::v3;
    auto s = settings(true);
    s.calendar = live::Calendar{1800000000, 7000037};
    s.transfer.modem.bandwidth_hz = 3.6;
    const modem::OscillatorModel gpsdo_xo{.0001, .5};
    s.transfer.modem.oscillator_search =
        modem::OscillatorSearchConfig{.lf = gpsdo_xo, .rf = gpsdo_xo, .rf_shift_hz = 30e6, .margin = 3};
    s.transfer.clock_sync.reset();
    s.transfer.audio_timing_error_seconds = clock_sync::default_audio_error_seconds;
    s.minimum_carrier_spacing_hz = 265;
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
    s.target_cn0_db_hz = 32;
    s.tx_dbm = 3;
    s.path_loss_db = 90;
    s.noise_dbm_hz = -164;
    const auto first = live::detail::prepare_profile(s, *s.calendar, {}, {.simulation_seed = 6091});
    const auto second = live::detail::prepare_profile(s, *s.calendar, {}, {.simulation_seed = 6092});
    s.simulation = false;
    const auto live_profile = live::detail::prepare_profile(s, *s.calendar, {});
    for (const auto* profile : {&second, &live_profile}) {
        const auto& geometry = profile->waveform.geometry();
        check(geometry.source_samples == first.waveform.geometry().source_samples &&
              geometry.calendar_origin_sample == first.waveform.geometry().calendar_origin_sample &&
              geometry.bit_seconds == first.waveform.geometry().bit_seconds &&
              profile->waveform.transmit_slot(0) == first.waveform.transmit_slot(0) &&
              profile->waveform.transmit_slot(1) == first.waveform.transmit_slot(1),
              "A channel realization changed the public transmit geometry");
    }
    const auto& effects_a = first.channel.effects;
    const auto& effects_b = second.channel.effects;
    check(effects_a.tx.audio_ppm != effects_b.tx.audio_ppm &&
          effects_a.rx.audio_ppm != effects_b.rx.audio_ppm &&
          effects_a.timing_offset_seconds != effects_b.timing_offset_seconds,
          "The fixture did not draw different clock and arrival errors");

    const std::array<std::uint8_t, 2> bits{0, 1};
    EmissionOptions options;
    options.bits = bits;
    // The first DATA pattern starts after its leading halo. Allow its modeled
    // arrival to move within the default timing prior while keeping the test
    // to a prefix without receiver scoring.
    const auto rows = 32U * s.transfer.modem.sample_rate;
    constexpr std::size_t chunk_rows = 1000;
    std::array<Complex, chunk_rows> a{}, b{}, live_samples{};
    {
        Emission tx_a(first.waveform, first.key, options);
        Emission tx_b(second.waveform, second.key, options);
        Emission tx_live(live_profile.waveform, live_profile.key, options);
        for (std::size_t offset = 0; offset < rows; offset += chunk_rows) {
            const auto count = std::min(chunk_rows, rows - offset);
            const auto block_a = std::span(a).first(count);
            const auto block_b = std::span(b).first(count);
            const auto block_live = std::span(live_samples).first(count);
            check(tx_a.read(block_a) == count && tx_b.read(block_b) == count && tx_live.read(block_live) == count &&
                  std::ranges::equal(block_a, block_b) && std::ranges::equal(block_a, block_live),
                  "A channel realization changed the emitted DATA samples");
        }
        check(tx_a.report().data_energy > 0, "The fixture did not reach active DATA");
    }

    Emission tx_a(first.waveform, first.key, options);
    Emission tx_b(second.waveform, second.key, options);
    auto config_a = first.channel;
    auto config_b = second.channel;
    config_a.noise_seed = config_b.noise_seed = 17;
    StreamingChannel channel_a(tx_a.source(), config_a);
    StreamingChannel channel_b(tx_b.source(), config_b);
    for (std::size_t offset = 0; offset < rows; offset += chunk_rows) {
        const auto count = std::min(chunk_rows, rows - offset);
        check(channel_a.read(std::span(a).first(count)) == count &&
              channel_b.read(std::span(b).first(count)) == count, "The channel fixture ended before active DATA");
    }
    const auto& report_a = channel_a.report();
    const auto& report_b = channel_b.report();
    check(report_a.bounded_spread && report_b.bounded_spread &&
          report_a.bounded_spread->excess_delay_seconds != report_b.bounded_spread->excess_delay_seconds &&
          report_a.signal_energy > 0 && report_b.signal_energy > 0 && a != b,
          "The fixture did not realize different echo delays and received samples");
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

void automatic_noise_capture_lifecycle() {
    auto s = settings(false);
    s.transfer.modem.sample_rate = 1500;
    s.transfer.modem.carrier_hz = 500;
    s.calendar->epoch += 60;
    s.auto_live_noise = true;
    s.full_duplex = true;
    const auto rate = s.transfer.modem.sample_rate;
    const auto calibration_rows = live::detail::noise_calibration_samples(rate);
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = static_cast<long double>(s.calendar->epoch) - 8;
    input->samples.resize(calibration_rows + 3 * rate);
    input->pause_after = calibration_rows + rate / 4;
    input->chunk_size = rate / 20;
    input->callback_may_block = true;
    std::mt19937_64 rng(1793850000);
    std::normal_distribution<float> noise(0, 4e-6f);
    for (std::size_t i = 0; i < input->samples.size(); ++i) {
        const auto phase = 2 * std::numbers::pi * 73 * static_cast<double>(i) / rate;
        input->samples[i] = static_cast<float>(2e-4 + 2e-5 * std::cos(phase)) + noise(rng);
    }
    // Each rejected window consumes its last callback. Align the following
    // input to those boundaries so the clipped window requires another retry.
    const auto chunk = input->chunk_size;
    const auto rejected_frames = (calibration_rows + chunk - 1) / chunk * chunk;
    auto recovering = std::make_shared<fixture::CaptureInput>(*input);
    recovering->samples.insert(recovering->samples.begin(), rejected_frames, 1.f);
    recovering->samples.insert(recovering->samples.begin(), rejected_frames, 0.f);
    recovering->pause_after = rejected_frames;
    auto silent = std::make_shared<fixture::CaptureInput>(*input);
    std::fill(silent->samples.begin(), silent->samples.end(), 0.f);
    const auto set_input = [](const auto& source) {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = source;
    };
    set_input(recovering);
    fixture::hold_capture = true;
    fixture::pause_released = false;
    const auto startup_opens = fixture::capture_opens.load();
    live::Session session;
    session.start(s);
    wait(session, [](const auto& out) {
        return out.transmit_unavailable && out.transmit_unavailable->find("calibration") != std::string::npos;
    }, "Automatic full-duplex transmitter did not wait for its input calibration");
    const std::array<std::uint8_t, 1> bit{1};
    const auto check_transmit_blocked = [&] {
        bool blocked = false;
        try { session.transmit_bits(bit); }
        catch (const Error& error) { blocked = std::string_view(error.what()).find("calibration") != std::string_view::npos; }
        check(blocked, "Automatic full-duplex transmitter accepted a calibration-contaminating transmission");
    };
    const auto pending_calibration = [&] {
        const auto pending = wait(session, [](const auto& out) {
            if (!out.receiver_error.empty()) throw Error("Automatic calibration stopped: " + out.receiver_error);
            return out.status.find("Retrying calibration") != std::string::npos &&
                out.status.find("silent") != std::string::npos;
        }, "Automatic Robust calibration did not retry after unusable input");
        check(!pending.live_noise_baseline_dbfs_hz && !pending.receiver_workers && pending.received.empty() &&
              pending.pattern_steps.empty() && fixture::capture_active == 1,
              "Robust released capture or published receiver evidence while calibration was pending");
        return pending;
    };
    check_transmit_blocked();
    fixture::hold_capture = false;
    const auto pending = pending_calibration();
    check(pending.transmit_unavailable == pending.status, "Retry reason was hidden by the full-duplex transmit gate");
    check_transmit_blocked();
    fixture::pause_released = true;
    const auto listening = [](const auto& out) {
        return out.live_noise_baseline_dbfs_hz && out.receiver_workers && out.receiver_error.empty();
    };
    const auto first = wait(session, listening, "Automatic Robust calibration did not recover on the same capture");
    check(fixture::capture_opens == startup_opens + 1 && first.pattern_steps.empty(),
          "Automatic Robust retry reopened capture or invented public receiver evidence");

    s.full_duplex = false;
    set_input(input);
    fixture::pause_released = false;
    session.configure(s);
    wait(session, listening, "Automatic half-duplex calibration did not start receiving");
    const auto opens = fixture::capture_opens.load();
    set_input(silent);
    session.transmit_bits(bit);
    const auto resumed = wait(session, [&](const auto& out) {
        return !out.transmitting && fixture::capture_opens > opens && listening(out);
    }, "Half-duplex receive did not resume with its measured baseline");
    check(resumed.live_noise_baseline_dbfs_hz == first.live_noise_baseline_dbfs_hz && resumed.received.empty() &&
          resumed.pattern_steps.empty(), "Half-duplex resume changed its noise baseline or invented receive evidence");

    session.configure(s);
    pending_calibration();
    close(session);
    check(fixture::capture_active == 0, "Closing Robust did not release a retrying audio capture");
    fixture::pause_released = true;
    set_input(std::shared_ptr<const fixture::CaptureInput>{});
}

void live_oscilloscope_retains_environmental_window() {
    auto s = settings(false);
    s.live_noise_dbfs_hz = -100;
    constexpr std::size_t capture_samples = 8192;
    constexpr std::size_t scope_samples = 2048;
    constexpr std::size_t spectrum_samples = 1024;
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
    input->samples.resize(capture_samples);
    input->chunk_size = 128;
    input->pause_after = scope_samples;
    fixture::pause_released = false;
    fixture::capture_paused = false;
    for (std::size_t i = 0; i < input->samples.size(); ++i)
        input->samples[i] = static_cast<float>(static_cast<int>(i % 251) - 125) / 4096.f;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = input;
    }

    live::Session session;
    session.start(s);
    const auto initial = wait(session, [](const auto& snapshot) {
        if (snapshot.waveform.empty()) return false;
        check(snapshot.waveform.size() == scope_samples,
              "Live Robust oscilloscope displayed a partial startup window");
        return fixture::capture_paused.load();
    }, "Live Robust oscilloscope did not publish its full startup window");
    check(initial.waveform_sample_rate == s.transfer.modem.sample_rate &&
          initial.spectrum_sample_count == spectrum_samples &&
          initial.spectrum_start_sample == scope_samples - spectrum_samples &&
          std::equal(initial.waveform.begin(), initial.waveform.end(), input->samples.begin()),
          "Live Robust oscilloscope lost its startup PCM or shifted the FFT tail");

    fixture::pause_released = true;
    const auto wrapped = wait(session, [](const auto& snapshot) {
        return snapshot.spectrum_start_sample > scope_samples - spectrum_samples;
    }, "Live Robust oscilloscope did not publish a wrapped window");
    check(wrapped.waveform.size() == scope_samples &&
          wrapped.spectrum_start_sample <= input->samples.size() - spectrum_samples,
          "Live Robust oscilloscope lost its retained window or FFT coordinates after wrapping");
    const auto waveform_first = wrapped.spectrum_start_sample - (scope_samples - spectrum_samples);
    const auto expected = std::span(input->samples).subspan(waveform_first, scope_samples);
    check(std::equal(wrapped.waveform.begin(), wrapped.waveform.end(), expected.begin()),
          "Live Robust oscilloscope reordered or truncated its wrapped PCM window");

    close(session);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
    }
}

void automatic_live_first_contact(bool default_latency = false) {
    using namespace triad::v3;
    auto s = settings(false);
    s.calendar.reset();
    s.auto_calendar = true;
    s.data_cadence = live::DataCadence::scheduled_bridge;
    if (default_latency) {
        s.transfer.modem.bandwidth_hz = 2400;
        s.minimum_carrier_spacing_hz = 265;
        s.target_cn0_db_hz = 32;
        s.transfer.modem.oscillator_search = modem::OscillatorSearchConfig{};
        s.transfer.clock_sync.reset();
        s.transfer.key = Crypto::amateur();
        s.receiver_memory_limit.reset();
    } else {
        s.transfer.modem.sample_rate = 1500;
        s.transfer.modem.carrier_hz = 500;
        s.transfer.modem.oscillator_search->lf.accuracy_ppm = 15;
    }
    const auto rate = s.transfer.modem.sample_rate;
    // This is the public recurrence, independent of both live sessions.
    const auto profile = live::detail::prepare_profile(s, {1767225600, 0}, {});
    const auto& calendar = profile.waveform;
    const auto utc = [rate](HelperBurstCoordinate point) {
        return static_cast<long double>(point.epoch) + static_cast<long double>(point.phase) / rate;
    };

    auto tap = std::make_shared<fixture::CaptureInput>();
    tap->loopback = true;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = tap;
    }
    live::Session transmitter;
    transmitter.start(s);
    ready(transmitter);
    const auto requested = fixture::utc_now();
    constexpr std::array<std::uint8_t, 3> bits{0, 0, 1}; // Short text 'e'.
    transmitter.transmit(Message{.data = {'e'}});
    wait(transmitter, [](const auto& out) {
        check(out.error.empty(), "Automatic Robust TX failed: " + out.error);
        return !out.transmitting;
    }, "Automatic Robust TX did not finish");
    close(transmitter);

    long double start;
    std::vector<float> played;
    {
        std::lock_guard lock(fixture::mutex);
        check(fixture::scheduled_epoch && !fixture::loopback_pcm.empty(),
              "Automatic Robust did not schedule and generate dated output");
        start = *fixture::scheduled_epoch;
        played = std::move(fixture::loopback_pcm);
    }
    const auto second = static_cast<std::uint64_t>(std::floor(start));
    const auto slot = calendar.first_slot_not_before({second, 0});
    check(slot > 2 && start == utc(calendar.transmit_slot(slot).helper_begin) &&
          utc(calendar.transmit_slot(slot - 2).helper_begin) > requested,
          "Automatic Robust TX did not leave two upcoming quiet slots on the shared recurrence");

    const auto first = slot - 2;
    const auto trailing_quiet = slot + bits.size() + 1;
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = default_latency ? std::floor(requested * rate) / rate :
        utc(calendar.transmit_slot(first).helper_begin) - 8;
    input->chunk_size = rate / 20;
    const auto frames_at = [&](long double at) {
        return static_cast<std::size_t>(std::llround((at - input->first_utc) * rate));
    };
    const auto nominal_played_first = frames_at(start);
    const auto played_first = default_latency ? frames_at(start + 6.75L) : nominal_played_first;
    const auto calibration_rows = live::detail::noise_calibration_samples(rate);
    auto capture_stop = frames_at(utc(calendar.transmit_slot(trailing_quiet).helper_end) + 4);
    input->pause_after = frames_at(utc(calendar.transmit_slot(trailing_quiet).helper_begin) - 1);
    auto first_observed = first;
    if (default_latency) {
        // Inject a receiver that starts with Send. Count the same raw rows
        // that are available to the centered projector after calibration.
        const auto raw_prefix = ((calibration_rows + input->chunk_size - 1) / input->chunk_size) * input->chunk_size;
        const auto halo = live::detail::AudioInput::required_halo_samples(rate, profile.receiver.data_if_hz,
            profile.minimum_audio_hz, profile.maximum_audio_hz);
        const auto ready_utc = input->first_utc + static_cast<long double>(raw_prefix) / rate;
        const auto plan = live::detail::plan_dated_receive_start(profile, true, ready_utc, 1e-7, halo);
        first_observed = plan.first_slot;
        check(first_observed <= first && first - first_observed <= 1,
              "Compact default did not leave two fully observable startup quiet slots");
        const auto raw_stop = [&](std::uint64_t index) {
            const auto window = plan.profile.waveform.slot(index);
            check(window.previous_signal_stop <= window.observation.first &&
                  window.observation.stop <= window.next_signal_first,
                  "Compact default receive observation overlaps a neighboring signal");
            return raw_prefix + static_cast<std::size_t>(
                static_cast<std::int64_t>(window.observation.stop) - plan.first_input_sample) + halo;
        };
        for (auto index = first_observed; index < trailing_quiet - 1; ++index) (void)raw_stop(index);
        capture_stop = raw_stop(trailing_quiet);
        input->pause_after = raw_stop(trailing_quiet - 1);
        check(capture_stop > played_first + played.size(), "Compact default capture stops inside DATA");
        const auto completion_tail = static_cast<long double>(capture_stop - nominal_played_first - played.size()) / rate;
        check(start - requested <= 90 && completion_tail <= 60,
              "Compact default exceeded its first-send or confirmation latency budget");
        std::cout << "Compact default dated fixture: " << start - requested
                  << " s Send to first DATA slot, " << completion_tail << " s scheduled final support to physical cutoff\n";
    }
    check(capture_stop < (default_latency ? 1'300'000U : 800'000U) && played_first + played.size() < capture_stop,
          "Automatic Robust fixture exceeded its dated sample budget");
    input->samples.resize(capture_stop);
    input->callback_may_block = input->burst = true;
    std::mt19937_64 rng(0x6175746f757463ULL);
    std::normal_distribution<float> noise(0, .0001f);
    for (auto& sample : input->samples) sample = 2e-4f + noise(rng);
    double path_gain = 1;
    if (default_latency) {
        // One bounded, above-target functional check at 40 dB-Hz against
        // the conservative automatically estimated startup noise density.
        const auto baseline = live::detail::calibrate_noise_dbfs_hz(
            std::span(input->samples).first(calibration_rows), rate);
        const auto tx_slot = calendar.transmit_slot(slot);
        const auto begin = calendar.calendar().distance(tx_slot.helper_begin, tx_slot.pattern_begin);
        const auto rows = calendar.calendar().distance(tx_slot.pattern_begin, tx_slot.pattern_end);
        const auto center = std::span(played).subspan(begin + rows / 4, rows / 2);
        double power = 0;
        for (const auto sample : center) power += sample * sample;
        path_gain = std::sqrt(std::pow(10., (baseline.noise_dbfs_hz + 40) / 10) / (power / center.size()));
    }
    for (std::size_t i = 0; i < played.size(); ++i)
        input->samples[played_first + i] += static_cast<float>(path_gain * played[i]);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = input;
    }
    fixture::pause_released = false;
    fixture::capture_paused = false;

    s.auto_live_noise = true;
    live::Session receiver;
    const auto receive_started = std::chrono::steady_clock::now();
    receiver.start(s);
    const auto checked = [](const auto& out) {
        check(out.error.empty(), "Automatic Robust RX failed to prepare: " + out.error);
        check(out.receiver_error.empty() || out.receiver_error == "Robust audio input ended.",
              "Automatic Robust RX failed: " + out.receiver_error);
    };
    const auto before_completion = wait(receiver, [&](const auto& out) {
        checked(out);
        return fixture::capture_paused && out.pattern_steps.size() >= trailing_quiet - first_observed;
    }, "Automatic Robust RX did not score the first trailing quiet slot", 35s);
    check(before_completion.received.empty() &&
          std::none_of(before_completion.signals.begin(), before_completion.signals.end(),
                       [](const auto& signal) { return signal.complete; }),
          "Automatic Robust completed a message after only one trailing quiet slot");
    fixture::pause_released = true;
    unsigned decoded = 0;
    const auto complete = wait(receiver, [&](const auto& out) {
        checked(out);
        for (const auto& message : out.received) {
            check(message.stream_complete && message.content_validated && message.short_text_decoded &&
                  message.content.message.data == Bytes{'e'} && std::ranges::equal(message.raw_bits, bits) &&
                  message.missing_symbols == 0, "Automatic Robust RX decoded unexpected content");
            ++decoded;
        }
        return out.receiver_error == "Robust audio input ended.";
    }, "Automatic Robust RX did not complete on the second trailing quiet slot", 35s);
    check(decoded == 1 && complete.live_noise_baseline_dbfs_hz &&
          complete.pattern_steps.size() == trailing_quiet - first_observed + 1,
          "Automatic Robust RX did not retain the complete public episode");
    double quiet_scoring_seconds = 0, data_scoring_seconds = 0;
    for (std::size_t i = 0; i < complete.pattern_steps.size(); ++i) {
        const auto& row = complete.pattern_steps[i];
        const auto index = first_observed + i;
        const bool data = slot <= index && index < slot + bits.size();
        check(row.index == index && row.ordinal == index && row.evidence_complete &&
              row.epoch == calendar.transmit_slot(index).pattern_begin.epoch &&
              (data ? row.decision == live::PatternDecision::data && row.bit == bits[index - slot] :
                      row.decision == live::PatternDecision::quiet && !row.bit),
              "Automatic Robust RX scored the wrong public address or decision: " + row.reason);
        (data ? data_scoring_seconds : quiet_scoring_seconds) += row.coarse_seconds + row.fine_seconds;
    }
    if (default_latency) {
        const auto receiver_wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - receive_started);
        std::cout << "Compact default receiver processed " << complete.pattern_steps.size() << " slots in "
                  << receiver_wall.count() << " s wall over " << static_cast<double>(capture_stop) / rate
                  << " s dated input; summed coarse+fine " << quiet_scoring_seconds << " s quiet, "
                  << data_scoring_seconds << " s DATA\n";
    }
    close(receiver);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
    }
    std::cout << "Independent automatic Robust peers decoded e/001 after scored startup and completion quiet slots\n";
}

void automatic_quiet_start_covers_default_arrival_window() {
    auto s = settings(false);
    s.calendar.reset();
    s.auto_calendar = true;
    s.data_cadence = live::DataCadence::scheduled_bridge;
    s.transfer.modem.sample_rate = 1500;
    s.transfer.modem.carrier_hz = 500;
    s.transfer.modem.oscillator_search.reset();
    s.transfer.clock_sync.reset();
    s.transfer.audio_timing_error_seconds = clock_sync::default_audio_error_seconds;
    const auto rate = s.transfer.modem.sample_rate;
    const auto profile = live::detail::prepare_profile(s, {1767225600, 0}, {});
    const auto& calendar = profile.waveform;
    const auto halo = live::detail::AudioInput::required_halo_samples(rate, profile.receiver.data_if_hz,
        profile.minimum_audio_hz, profile.maximum_audio_hz);
    const auto peer_start = [&](long double utc) {
        return live::detail::plan_dated_receive_start(profile, true, utc,
            s.transfer.audio_timing_error_seconds / 2, halo);
    };

    const auto candidate = calendar.first_slot_not_before({1795500000, 0});
    const auto date = calendar.transmit_slot(candidate).helper_begin;
    check(date.phase == 0, "Default scheduled startup fixture expected a whole UTC second");
    const auto stride = static_cast<long double>(calendar.calendar().minimum_stride_samples()) / rate;
    const auto early = peer_start(static_cast<long double>(date.epoch) - std::min(20.L, stride - .5L));
    const auto late = peer_start(static_cast<long double>(date.epoch) - 1);
    const auto fully_observed = [&](const auto& start) {
        return start.first_observation_sample >=
            static_cast<std::uint64_t>(start.first_input_sample + static_cast<std::int64_t>(halo));
    };
    check(early.first_slot == candidate && late.first_slot == candidate + 1 &&
          fully_observed(early) && fully_observed(late),
          "Default arrival support did not skip the partially observable startup slot");

    live::Session transmitter;
    transmitter.start(s);
    ready(transmitter);
    constexpr std::array<std::uint8_t, 1> bit{1};
    fixture::hold_playback_source = fixture::hold_playback = true;
    const auto before_request = fixture::utc_now();
    transmitter.transmit_bits(bit);
    const auto after_request = fixture::utc_now();
    const auto queued = wait(transmitter, [](const auto& out) {
        check(out.error.empty(), "Default arrival Robust TX failed: " + out.error);
        std::lock_guard lock(fixture::mutex);
        return out.transmitting && fixture::scheduled_epoch && fixture::playback_samples == 0;
    }, "Default arrival Robust TX did not queue playback");
    constexpr std::string_view countdown = "Leaving two quiet slots · transmission starts in ";
    check(queued.status.starts_with(countdown) && queued.status.ends_with(" seconds"),
          "Scheduled Robust startup did not report its delay in seconds: " + queued.status);
    {
        std::lock_guard lock(fixture::mutex);
        const auto remaining = std::stoll(queued.status.substr(countdown.size()));
        const auto expected = std::ceil(*fixture::scheduled_epoch - fixture::utc_now());
        check(remaining > 0 && std::abs(remaining - expected) <= 1,
              "Scheduled Robust startup reported an incorrect delay");
    }
    check(queued.transmit_trace.active && !queued.transmit_trace.emitting && !queued.transmit_trace.generated_bits,
          "Robust trace marked a queued message as emitting before first playback PCM");
    fixture::hold_playback_source = false;
    wait(transmitter, [](const auto& out) {
        check(out.error.empty(), "Default arrival Robust TX failed: " + out.error);
        std::lock_guard lock(fixture::mutex);
        return out.transmitting && out.transmit_trace.emitting && fixture::playback_samples;
    }, "Robust trace did not mark first playback PCM as emitting");
    check(transmitter.snapshot().status.starts_with(countdown),
          "Prefilling Robust playback cleared the countdown before the scheduled UTC start");
    fixture::hold_playback = false;
    wait(transmitter, [](const auto& out) {
        check(out.error.empty(), "Default arrival Robust TX failed: " + out.error);
        return !out.transmitting;
    }, "Default arrival Robust TX did not finish");
    close(transmitter);
    long double start;
    std::pair<long double, long double> format_window;
    {
        std::lock_guard lock(fixture::mutex);
        check(fixture::scheduled_epoch && fixture::playback_format_window && fixture::playback_samples,
              "Default arrival Robust TX did not reach scheduled playback");
        start = *fixture::scheduled_epoch;
        format_window = *fixture::playback_format_window;
    }
    const auto startup_lead = 3 + static_cast<long double>(live::detail::noise_calibration_samples(rate)) / rate;
    const auto provider_slot = [&](long double date) {
        return calendar.first_slot_not_before({static_cast<std::uint64_t>(std::ceil(date + 5)), 0});
    };
    const auto first_possible = std::max(peer_start(before_request + startup_lead).first_slot + 2,
                                         provider_slot(format_window.first));
    const auto last_possible = std::max(peer_start(after_request + startup_lead).first_slot + 2,
                                        provider_slot(format_window.second));
    const auto chosen = calendar.first_slot_not_before({static_cast<std::uint64_t>(start), 0});
    check(first_possible <= chosen && chosen <= last_possible &&
          start == calendar.transmit_slot(chosen).helper_begin.epoch,
          "Default Robust TX did not schedule two fully observable startup quiet slots");
}

void dated_live_data_then_eof(std::optional<int> fixed_link_gain_db = {}, bool buffered = false,
                             bool automatic_noise = false) {
    using namespace triad::v3;
    auto s = settings(false);
    s.transfer.modem.sample_rate = 1500;
    s.transfer.modem.carrier_hz = 500;
    s.transfer.audio_timing_error_seconds = 0;
    s.live_noise_dbfs_hz = -100;
    if (fixed_link_gain_db) {
        s.calendar = live::Calendar{1793850000, 7000037};
        s.transfer.modem.carrier_hz = 497;
        s.spread_preset = "hf-worst";
        s.selective_perturbation = "each-65";
        s.target_cn0_db_hz = 16;
        s.live_noise_dbfs_hz = -80;
        s.receiver_memory_limit = 1024U * 1024 * 1024;
    }
    const auto noise_density = std::pow(10., *s.live_noise_dbfs_hz / 10);
    if (automatic_noise) {
        s.calendar = live::Calendar{1793850000, 7000037};
        s.target_cn0_db_hz = 32;
        s.live_noise_dbfs_hz.reset();
        s.auto_live_noise = true;
    }
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
    const std::array<std::uint8_t, 1> bit{1};
    const std::array<std::uint8_t, 3> text{0, 0, 1}; // Native short-codec representation of 'e'.
    const bool expect_message = fixed_link_gain_db || automatic_noise;
    const std::span<const std::uint8_t> wire = expect_message ? std::span<const std::uint8_t>(text) :
        std::span<const std::uint8_t>(bit);
    Emission source(profile.waveform, profile.key, {EmissionMode::Message, 0, wire});
    const auto rate = s.transfer.modem.sample_rate;
    const auto prefix = (automatic_noise ? 8U : 2U) * rate;
    const auto expected_slots = wire.size() + (expect_message ? 2 : 0);
    const auto after_epoch = profile.waveform.slot(expected_slots - 1).observation.stop + rate;
    check(after_epoch + prefix < (expect_message ? 500000 : 100000),
          "Dated live fixture exceeds its sample budget");
    const auto actual_cn0 = s.target_cn0_db_hz + fixed_link_gain_db.value_or(0);
    const auto signal_gain = std::sqrt(noise_density * std::pow(10., actual_cn0 / 10));
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = static_cast<long double>(s.calendar->epoch) - static_cast<long double>(prefix) / rate;
    input->samples.resize(prefix + after_epoch);
    input->callback_may_block = buffered;
    input->burst = buffered;
    if (buffered) input->chunk_size = rate / 20;
    std::mt19937_64 rng(0x726f6275737421ULL);
    std::normal_distribution<double> noise(0, std::sqrt(rate / 2. * noise_density));
    for (auto& sample : input->samples) sample = static_cast<float>(noise(rng));
    std::array<Complex, 512> iq{};
    std::size_t at = 0;
    float peak = 0;
    for (;;) {
        const auto count = source.read(iq);
        if (!count) break;
        check(at + count <= after_epoch, "Dated live source exceeded its capture");
        for (std::size_t i = 0; i < count; ++i) {
            const auto rotation = std::polar(1., 2 * std::numbers::pi * s.transfer.modem.carrier_hz *
                static_cast<double>(at + i) / rate);
            auto& pcm = input->samples[prefix + at + i];
            pcm += static_cast<float>(std::numbers::sqrt2 * signal_gain * (iq[i] * rotation).real());
            peak = std::max(peak, std::abs(pcm));
        }
        at += count;
    }
    check(peak < .9, "Dated live input exceeds the PCM headroom");
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input = input;
    }
    live::Session session;
    session.start(s);
    unsigned decoded_messages = 0;
    bool complete_signal = false;
    const auto out = wait(session, [&](const auto& snapshot) {
        check(snapshot.error.empty(), "Dated Robust input failed: " + snapshot.error);
        check(snapshot.receiver_error.empty() || snapshot.receiver_error == "Robust audio input ended.",
              "Dated Robust receive failed: " + snapshot.receiver_error);
        for (const auto& message : snapshot.received) {
            check(expect_message && message.stream_complete && message.content_validated &&
                  message.short_text_decoded && message.content.message.data == Bytes{'e'} &&
                  std::ranges::equal(message.raw_bits, wire) && message.missing_symbols == 0,
                  "Dated Robust did not decode the expected complete native message");
            ++decoded_messages;
        }
        for (const auto& signal : snapshot.signals) {
            if (!signal.complete) continue;
            check(expect_message && signal.robust_reception && signal.validated && signal.raw_bits == "001",
                  "Dated Robust completed an unexpected reception");
            complete_signal = true;
        }
        return snapshot.receiver_error == "Robust audio input ended.";
    }, "Dated native DATA scoring did not finish", 45s);
    if (automatic_noise)
        check(out.live_noise_baseline_dbfs_hz && *out.live_noise_baseline_dbfs_hz > -100 &&
              *out.live_noise_baseline_dbfs_hz < -90, "Automatic live noise baseline was absent or out of range");
    else check(!out.live_noise_baseline_dbfs_hz, "Manual input-noise profile unexpectedly calibrated audio");
    check(out.pattern_steps.size() == expected_slots, "Dated Robust lost a complete public aperture");
    for (std::size_t i = 0; i < expected_slots; ++i) {
        const auto& row = out.pattern_steps[i];
        check(row.index == i && row.ordinal == s.calendar->initial_ordinal + i && row.evidence_complete &&
              row.fine_candidates > 0,
              "Dated Robust lost ordered complete native scoring at slot " + std::to_string(i));
        if (i < wire.size())
            check(row.decision == live::PatternDecision::data && row.bit == wire[i],
                  "Dated Robust wanted slot " + std::to_string(i) + " failed: " + row.reason);
        else
            check(row.decision == live::PatternDecision::quiet && !row.bit,
                  "Dated Robust quiet slot " + std::to_string(i) + " failed: " + row.reason);
    }
    check(!out.waveform.empty() && !out.constellation.empty() && out.constellation_source == live::ConstellationSource::input &&
          complete_signal == expect_message && decoded_messages == (expect_message ? 1U : 0U),
          "Live input plots or expected physical completion are absent");
    close(session);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
    }
    if (expect_message)
        std::cout << "Dated Robust decoded e/001 at " << actual_cn0 << " dB-Hz with Target "
                  << s.target_cn0_db_hz << " dB-Hz, two quiet slots, " << profile.waveform.geometry().bit_seconds
                  << " s bits and peak PCM " << peak << '\n';
}

void raw_bit_session_transmission_and_ordinal() {
    auto simulated = settings(true);
    simulated.transfer.modem.sample_rate = 1500;
    // Center the widened DATA band within this fixture's real-audio passband.
    simulated.transfer.modem.carrier_hz = 375;
    simulated.minimum_carrier_spacing_hz = 265;
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    check(prepared.bit_seconds && prepared.slot_stride_seconds &&
          *prepared.slot_stride_seconds * 3 * simulated.transfer.modem.sample_rate < 600000,
          "Single-bit Robust fixture exceeds its three-window sample budget");

    const auto playback_opens = fixture::playback_opens.load();
    const auto rejects_raw = [&](std::span<const std::uint8_t> bits, const char* reason) {
        bool rejected = false;
        try { session.transmit_bits(bits); }
        catch (const Error&) { rejected = true; }
        check(rejected, reason);
        const auto state = session.snapshot();
        check(!state.transmitting && state.pattern_steps.empty() && state.signals.empty() && state.received.empty() &&
              fixture::playback_opens == playback_opens,
              "Invalid raw bits changed the Robust session or opened audio playback");
    };
    rejects_raw(std::span<const std::uint8_t>{}, "Robust accepted an empty raw-bit transmission");
    const std::array<std::uint8_t, 2> nonbinary{0, 2};
    rejects_raw(nonbinary, "Robust accepted a nonbinary raw-bit transmission");
    const std::array<std::uint8_t, transfer::short_message_bits + 1> oversized{};
    rejects_raw(oversized, "Robust accepted raw bits beyond the public short-message bound");

    const auto initial_ordinal = simulated.calendar->initial_ordinal;
    const std::array raw_messages{
        std::pair<std::vector<std::uint8_t>, std::uint64_t>{{1}, initial_ordinal},
        std::pair<std::vector<std::uint8_t>, std::uint64_t>{{0}, initial_ordinal + 3},
        std::pair<std::vector<std::uint8_t>, std::uint64_t>{{0, 1, 0}, initial_ordinal + 6}
    };
    for (const auto& [raw_bits, expected_ordinal] : raw_messages) {
        session.transmit_bits(raw_bits);
        std::string expected_bits;
        for (const auto bit : raw_bits) expected_bits.push_back(bit ? '1' : '0');
        std::vector<live::SignalUpdate> signals;
        std::size_t received_messages = 0;
        bool stopped_replay = false;
        const auto completed = wait(session, [&](auto& out) {
            signals.insert(signals.end(), out.signals.begin(), out.signals.end());
            received_messages += out.received.size();
            check(out.error.empty() && out.receiver_error.empty(),
                  "Raw-bit Robust simulation failed: " + out.error + "; " + out.receiver_error);
            check(out.pattern_steps.size() <= raw_bits.size() + 2,
                  "Raw-bit Robust scheduled more DATA and quiet windows than its input requires");
            if (expected_bits == "0" && out.simulation_replay && !stopped_replay) {
                check(out.transmitting && out.pattern_steps.size() == raw_bits.size() + 2,
                      "Raw-bit replay began before physical receive processing finished");
                session.cancel_transmit();
                stopped_replay = true;
            }
            return !out.transmitting;
        }, "Single raw-bit Robust transmission did not finish", 90s);

        const auto record = std::find_if(signals.rbegin(), signals.rend(), [](const auto& signal) {
            return signal.complete && signal.robust_reception;
        });
        check(record != signals.rend() && record->binary && record->simulation_raw_input &&
              record->raw_bits == expected_bits &&
              !record->validated && received_messages == 0 && completed.received.empty(),
              "Raw bit was text-coded, decoded as a message, or lost from the native signal record");
        check(record->simulation_input_result == live::SimulationInputResult::match &&
              completed.status.find("sent " + expected_bits + " · received " + expected_bits + " · match") != std::string::npos,
              "Raw simulation did not report the actual match for its own transmission");
        check(completed.pattern_steps.size() == raw_bits.size() + 2,
              "Raw-bit simulation did not finish from its DATA slots and two physical quiet slots");
        if (expected_bits == "0")
            check(stopped_replay && !completed.simulation_replay && completed.status.find("replay stopped") != std::string::npos,
                  "Stopping a replay did not keep its completed receive state");
        for (std::size_t i = 0; i < completed.pattern_steps.size(); ++i) {
            const auto& row = completed.pattern_steps[i];
            check(row.index == i && row.ordinal == expected_ordinal + i && row.evidence_complete &&
                  row.decision == (i < raw_bits.size() ? live::PatternDecision::data : live::PatternDecision::quiet),
                  "Raw-bit transmission changed its addressed ordinal or physical completion sequence");
            if (i < raw_bits.size()) check(row.bit == raw_bits[i], "Native raw-bit reception changed the transmitted bit");
            else check(!row.bit, "Scored quiet window was reported as DATA");
        }
    }
    close(session);
}

void modeled_simulation_waterfall() {
    // Run this under its own CTest entry. Earlier fixtures can leave allocator
    // pages charged against the receiver's 512 MiB whole-process limit.
    auto simulated = settings(true);
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
    simulated.transfer.modem.bandwidth_hz = 6;
    simulated.minimum_carrier_spacing_hz = 3.5;
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    check(prepared.bit_seconds && *prepared.bit_seconds >= 60 && *prepared.bit_seconds < 110 &&
          prepared.slot_stride_seconds && *prepared.slot_stride_seconds * 4 * 1500 < 750'000,
          "Long-waterfall fixture exceeded its intended bit or sample budget");

    session.transmit_bits(std::array<std::uint8_t, 2>{0, 1});
    std::shared_ptr<const live::Snapshot::SimulationWaterfall> first_bit_waterfall;
    std::shared_ptr<const live::Snapshot::SimulationWaterfall> replay_waterfall;
    const auto completed = wait(session, [&](const auto& out) {
        check(out.error.empty(), "Modeled waterfall simulation failed: " + out.error);
        const auto& active = out.simulation_waterfall;
        if (active && active->first_row == 0 && active->rows.size() >= active->total_rows / 2)
            first_bit_waterfall = active;
        if (out.simulation_replay && !replay_waterfall) replay_waterfall = out.simulation_waterfall;
        return !out.transmitting;
    }, "Modeled waterfall simulation did not complete", 25s);
    const auto history = completed.simulation_waterfall;
    check(history && history == replay_waterfall && history->total_rows == history->rows.size() &&
          history->total_rows <= live::Snapshot::SimulationWaterfall::maximum_rows && history->total_rows >= 50 &&
          history->first_row >= history->total_rows,
          "Modeled rows did not roll through the later DATA and quiet intervals or survive replay");
    check(std::abs(history->seconds_per_row * history->total_rows - *prepared.bit_seconds) < 0.25 &&
          std::abs(history->bin_hz - 1500. / 1024) < 1e-9,
          "Modeled waterfall lost the DATA timebase or native FFT grid");
    const auto central_bin = static_cast<std::size_t>(std::lround(500. / history->bin_hz));
    check(first_bit_waterfall && first_bit_waterfall->rows.front() != history->rows.front() &&
          first_bit_waterfall->seconds_per_row == history->seconds_per_row,
          "Scrolling did not evict old rows at the original time scale");
    const auto& first_rows = first_bit_waterfall->rows;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (std::size_t i = first_rows.size() / 4; i < first_rows.size() * 3 / 4; ++i) {
        const auto& spectrum = *first_rows[i];
        check(spectrum.size() == 513 && std::isfinite(spectrum[central_bin]),
              "Modeled waterfall did not retain actual receive-bin power");
        low = std::min(low, spectrum[central_bin]);
        high = std::max(high, spectrum[central_bin]);
    }
    check(high - low > 3., "Modeled waterfall lost carrier variation within the DATA gate");
    check(completed.pattern_steps.size() == 4 && completed.pattern_steps[0].bit == 0 &&
          completed.pattern_steps[1].bit == 1 &&
          completed.pattern_steps[2].decision == live::PatternDecision::quiet &&
          completed.pattern_steps[3].decision == live::PatternDecision::quiet,
          "Waterfall collection changed DATA or physical quiet-slot scoring");
    simulated.transfer.modem.carrier_hz += 50;
    session.configure(simulated);
    const auto adjusted = ready(session);
    check(adjusted.simulation_replay_available && !adjusted.simulation_waterfall,
          "A tuning change did not reset the active waterfall and keep the completed replay");
    session.replay_simulation();
    const auto replayed = wait(session, [](const auto& out) { return out.simulation_replay; },
                               "Modeled waterfall replay did not restart after tuning", 2s);
    check(replayed.simulation_waterfall == history,
          "Replay did not restore the original modeled waterfall after tuning");
    check(!replayed.simulation_detail, "Compact preview unexpectedly generated fullscreen FFTs");
    std::shared_ptr<const live::Snapshot::SimulationDetail> replay_detail;
    wait(session, [&](const auto& out) {
        if (!out.simulation_replay) return false;
        replay_detail = session.snapshot(true).simulation_detail;
        return replay_detail && !replay_detail->rows.empty() && !replay_detail->rows.back().db.empty();
    }, "Fullscreen replay did not receive observed FFT rows", 2s);
    check(replay_detail->sample_rate == 1500 && replay_detail->rows.size() == live::simulation_detail_rows &&
          replay_detail->rows.front().db.empty() && replay_detail->rows.back().db.size() == 513,
          "Fullscreen replay did not retain its native FFT grid and blank leading time");
    session.cancel_transmit();
    close(session);
}

void complete_native_simulation(bool gpu = false, bool gpu_fp32 = false, bool dynamic_channel = false,
                                live::DataCadence cadence = live::DataCadence::scheduled) {
    // One predeclared functional stratum, not a sensitivity/capacity claim:
    // public calendar, clear e=001, FEC/Helper/FHSS Off and exact
    // oscillators, target and actual 80 dB-Hz with positive physical noise.
    // The default case has spread/selective Off. The optional nonstationary
    // case exercises HF Worst, Each 65%, and a bounded common-shift trajectory.
    // All cases score at most five complete windows, 600,000 samples and 90s.
    const auto captures = fixture::capture_opens.load(), playbacks = fixture::playback_opens.load();
    auto simulated = settings(true);
    // A saved browser filter must not discard the native reception baseline.
    simulated.wrong_key_admission = "strict";
    simulated.gpu = gpu;
    simulated.data_cadence = cadence;
    if (gpu_fp32) simulated.transfer.modem.search_arithmetic = modem::SearchArithmetic::fp32;
    if (gpu) {
#if defined(DATAPUMP_HAVE_TRIAD_CUDA)
        // The CUDA-linked fixture reached 937,516 KiB resident on the
        // RTX PRO 6000 before adding the receiver's idle worker stack
        // reservations. Leave room for both under a fixed application cap.
        simulated.receiver_memory_limit = 2U * 1024 * 1024 * 1024;
#else
        simulated.receiver_memory_limit = 1024U * 1024 * 1024;
#endif
    }
    // The scheduled cadence is at least a minute. A supported lower logical
    // sample rate keeps either physical calendar inside the sample budget.
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
    if (dynamic_channel) {
        simulated.spread_preset = "hf-worst";
        simulated.selective_perturbation = "each-65";
        simulated.doppler_shift_hz = 8;
    }
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    check(prepared.gpu_device.empty() == !gpu && prepared.gpu_backend.empty() == !gpu &&
          prepared.gpu_score_rows == 0 && prepared.gpu_statistics_blocks == 0 &&
          prepared.gpu_cuda_native_fine_sessions == 0 && prepared.gpu_cuda_native_fine_fallback_reason.empty() &&
          prepared.gpu_vulkan_leased_bytes == 0 && prepared.gpu_vulkan_peak_leased_bytes == 0 &&
          prepared.gpu_native_coarse_workspace_sessions == 0 &&
          prepared.gpu_cuda_fp16_calls == 0 && prepared.gpu_cuda_int8_calls == 0 &&
          prepared.gpu_cuda_half_fft_score_rows == 0 && prepared.gpu_cuda_half_fft_fallback_summary.empty() &&
          prepared.gpu_fp32_score_rows == 0 && prepared.gpu_fp16_storage_score_rows == 0 &&
          prepared.gpu_packed_fp16_score_rows == 0 && prepared.gpu_native_coarse_lag_sessions == 0 &&
          (!gpu_fp32 || !prepared.gpu_native_lag_preference_supported),
          "Robust GPU readiness does not match the selected backend");
    if (prepared.gpu_backend.starts_with("vulkan:"))
        check(prepared.gpu_vulkan_shared_workspace_limit_bytes > 0,
              "Automatic Vulkan session omitted its discovered shared workspace budget");
    else
        check(prepared.gpu_vulkan_shared_workspace_limit_bytes == 0 && prepared.gpu_vulkan_leased_bytes == 0,
              "Non-Vulkan session reported Vulkan workspace admission");
    check(prepared.bit_seconds && prepared.slot_stride_seconds && *prepared.slot_stride_seconds * 6 * 1500 < 600000,
          "Robust simulation exceeds its predeclared sample budget");
    if (cadence == live::DataCadence::continuous)
        check(std::abs(*prepared.slot_stride_seconds - *prepared.bit_seconds) <= 1. / 1500,
              "Continuous simulation still leaves gaps between DATA gates");
    session.transmit(Message{.data = {'e'}});
    std::vector<transfer::Received> received;
    std::vector<live::SignalUpdate> signal_events;
    std::optional<std::chrono::steady_clock::time_point> replay_began;
    std::uint64_t replay_first = 0, last_revision = 0, last_frame = 0;
    std::uint64_t last_input = 0, preview_revision = 0;
    unsigned replay_frames = 0;
    bool processing_plot_seen = false, replay_pattern_seen = false;
    std::uint64_t replay_pattern_elapsed_ms = 0;
    const auto check_receiver_plot = [&](const live::Snapshot& out) {
        check(out.simulation_progress && out.waveform.size() >= 1024 && out.waveform.size() <= 2048 &&
              out.constellation.size() == 512 &&
              out.constellation_source == live::ConstellationSource::input &&
              out.spectrum_sample_count == 1024,
              "Simulation did not retain the full bounded receiver waveform window");
        const auto end = out.spectrum_start_sample + out.spectrum_sample_count;
        check(end >= out.waveform.size() && end <= out.simulation_progress->input_samples,
              "Simulation plotted samples outside the accepted receiver input");
        const auto first = end - out.waveform.size();
        for (std::size_t i = 0; i < out.constellation.size(); i += 7) {
            const auto at = i * out.waveform.size() / out.constellation.size();
            const auto expected = live::detail::real_pcm(out.constellation[i], first + at,
                simulated.transfer.modem.carrier_hz, out.waveform_sample_rate);
            check(std::abs(expected - out.waveform[at]) < 1e-6,
                  "Simulated waveform used different samples from receiver input I/Q");
        }
    };
    auto completed = wait(session, [&](auto& out) {
        for (auto& event : out.received) received.push_back(std::move(event));
        signal_events.insert(signal_events.end(), out.signals.begin(), out.signals.end());
        check(out.error.empty() && out.receiver_error.empty(), "Native simulation failed: " + out.error + "; " + out.receiver_error);
        check(out.pattern_steps.size() <= 5, "Native simulation exceeded five public scoring windows");
        if (out.simulation_progress) {
            const auto& progress = *out.simulation_progress;
            check(progress.input_samples >= last_input && progress.input_samples <= progress.total_samples &&
                  progress.sample_rate == simulated.transfer.modem.sample_rate,
                  "Accepted simulated input time jumped backward or past the declared extent");
            last_input = progress.input_samples;
        }
        if (out.transmitting && !out.simulation_replay && out.spectrum_revision > 0 &&
            out.spectrum_revision != preview_revision) {
            check_receiver_plot(out);
            processing_plot_seen = processing_plot_seen || out.waveform.size() == 2048;
            preview_revision = out.spectrum_revision;
        }
        if (out.simulation_replay && out.spectrum_revision != last_revision) {
            check_receiver_plot(out);
            check(out.transmitting && received.size() == 1 && out.pattern_steps.size() == 5 &&
                  out.pattern_steps.back().decision == live::PatternDecision::quiet &&
                  out.simulation_progress->complete && out.receiver_workers == 0,
                  "Simulation began replay before delivering the full result and physical quiet receipts");
            check(out.simulation_pattern_replay_active &&
                  out.simulation_pattern_replay_visible_steps <= out.pattern_steps.size() &&
                  out.simulation_pattern_replay_elapsed_ms >= replay_pattern_elapsed_ms &&
                  out.simulation_pattern_replay_elapsed_ms <= 3000,
                  "Simulation pattern evidence did not follow its bounded replay cursor");
            replay_pattern_seen = true;
            replay_pattern_elapsed_ms = out.simulation_pattern_replay_elapsed_ms;
            if (!replay_began) {
                replay_began = std::chrono::steady_clock::now();
                replay_first = out.spectrum_start_sample;
            } else {
                check(out.spectrum_start_sample > last_frame &&
                      (out.spectrum_start_sample - last_frame) % 75 == 0,
                      "Simulation replay did not follow a chronological 50 ms sample cadence");
            }
            last_frame = out.spectrum_start_sample;
            last_revision = out.spectrum_revision;
            ++replay_frames;
        }
        return !out.transmitting;
    }, "Native complete simulation exceeded its wall-time budget", 90s);
    check(replay_began && std::chrono::steady_clock::now() - *replay_began >= 2500ms &&
          replay_frames > 1 && replay_frames <= 60 && !completed.simulation_replay &&
          processing_plot_seen && replay_pattern_seen && !completed.simulation_pattern_replay_active &&
          completed.simulation_pattern_replay_visible_steps == completed.pattern_steps.size() &&
          completed.spectrum_start_sample >= replay_first &&
          completed.spectrum_start_sample - replay_first < 3 * simulated.transfer.modem.sample_rate &&
          completed.spectrum_revision >= last_revision,
          "Simulation did not finish and retain a bounded three-second activity replay");
    const bool decoded = received.size() == 1 && received[0].stream_complete && received[0].content_validated &&
        received[0].short_text_decoded && received[0].content.message.data == Bytes{'e'} &&
        received[0].raw_bits == Bytes({0, 0, 1}) && received[0].missing_symbols == 0;
    if (!decoded && (dynamic_channel || cadence != live::DataCadence::scheduled)) {
        for (const auto& row : completed.pattern_steps) {
            std::cerr << "simulation slot=" << row.index
                      << " decision=" << static_cast<unsigned>(row.decision)
                      << " bit=" << (row.bit ? std::to_string(*row.bit) : "null")
                      << " evidence=" << row.bit_evidence[0] << ',' << row.bit_evidence[1]
                      << " operational_log_e=" << row.operational_log_e
                      << " threshold=" << row.operational_threshold
                      << " helper_s=" << row.helper_seconds
                      << " coarse_s=" << row.coarse_seconds
                      << " fine_s=" << row.fine_seconds
                      << " complete=" << row.evidence_complete
                      << " reason=" << row.reason << '\n';
        }
    }
    check(decoded,
          "Actual native DSP did not decode physically complete e/001");
    const auto final_signal=std::find_if(signal_events.rbegin(),signal_events.rend(),[](const auto& signal) {
        return signal.robust_reception && signal.complete && signal.validated;
    });
    check(final_signal!=signal_events.rend() && final_signal->raw_bits=="001" &&
          final_signal->simulation_input_result==live::SimulationInputResult::match &&
          completed.status.find("sent 1 text byte · received 1 text byte · match")!=std::string::npos &&
          final_signal->wrong_key_evidence.available && final_signal->wrong_key_evidence.data_bits==3 &&
          final_signal->wrong_key_evidence.passing_bits[0]==3,
          "native GUI completion lost baseline content or reversible filter evidence");
    check(completed.pattern_steps.size() == 5, "Robust message did not score all three DATA and two quiet slots");
    for (std::size_t i = 0; i < completed.pattern_steps.size(); ++i) {
        const auto& row = completed.pattern_steps[i];
        check(row.index == i && row.ordinal == 7000037 + i && row.evidence_complete &&
              row.fine_candidates > 0 && row.workers > 0 && std::isfinite(row.operational_log_e),
              "Pattern steps lack actual ordered native evidence");
        check(row.decision == (i < 3 ? live::PatternDecision::data : live::PatternDecision::quiet),
              "Native message completion did not come from two physical quiet decisions");
        check(row.wrong_key_admission=="current" && !row.wrong_key_rejected,
              "saved browser filter changed native acquisition or decoding");
        if (i < 3) {
            check(row.bit == received[0].raw_bits[i], "Pattern DATA evidence disagrees with decoded bits");
            check(row.browser_admission && row.browser_admission->front(),
                  "Pattern DATA row omitted its reversible browser-admission ratings");
            for (std::size_t policy = 1; policy < row.browser_admission->size(); ++policy)
                check(!(*row.browser_admission)[policy] || (*row.browser_admission)[policy - 1],
                      "Nested browser-admission ratings were not monotone");
        } else {
            check(row.quiet_modes > 0 && !row.bit, "Quiet completion lacks scored noise modes");
            check(!row.browser_admission, "Quiet slot was presented as a browser-admission candidate");
        }
    }
    check(completed.waveform.size() == 2048 && completed.waveform_sample_rate == 1500 &&
          !completed.spectrum_db.empty() && completed.spectrum_db.size() == completed.spectrum_complex.size() &&
          completed.spectrum_sample_count == 1024 && completed.spectrum_revision > 0 &&
          completed.spectrum_start_sample + completed.spectrum_sample_count < 600000 &&
          !completed.constellation.empty() && completed.constellation.size() <= 512 &&
          completed.constellation_source == live::ConstellationSource::input,
          "Native reception did not produce bounded waveform, waterfall, and input I/Q plots");
    check(completed.simulation_progress && completed.simulation_progress->complete &&
          completed.simulation_progress->input_samples == completed.simulation_progress->total_samples &&
          completed.simulation_progress->elapsed_seconds > 0,
          "Completed simulation lost its measured computation progress");
    const auto& progress = *completed.simulation_progress;
    const auto modeled_seconds = static_cast<double>(progress.total_samples) / progress.sample_rate;
    check(progress.elapsed_seconds < modeled_seconds, "Finite simulation was paced against the modeled receiver clock");
    std::cout << "Simulated RX " << modeled_seconds << " s of input in " << progress.elapsed_seconds
              << " s, then replayed the three-second excerpt\n";
    check(std::all_of(completed.waveform.begin(), completed.waveform.end(), [](float x) { return std::isfinite(x); }) &&
          std::all_of(completed.spectrum_db.begin(), completed.spectrum_db.end(), [](double x) { return std::isfinite(x); }),
          "Native plot output contains nonfinite samples");
    if (!gpu && !dynamic_channel) {
        check(completed.simulation_replay_available && completed.replay_waterfall,
              "Completed simulation did not retain its calibrated activity");
        const auto original = *completed.replay_waterfall;
        simulated.transfer.modem.carrier_hz += 50;
        simulated.path_loss_db += 10;
        session.configure(simulated);
        const auto adjusted = ready(session);
        check(adjusted.simulation_replay_available && adjusted.search_band_hz != original.search_band_hz &&
              adjusted.simulation_spectrum_gain_db != original.gain_db,
              "A tuning change discarded the completed replay or did not change the active calibration");
        session.replay_simulation();
        const auto repeated = wait(session, [&](const auto& out) {
            check(out.received.empty() && out.signals.empty() && out.pattern_steps.empty() &&
                  out.transmit_trace.revision == adjusted.transmit_trace.revision,
                  "Plot-only replay generated new transmission or receiver evidence");
            return out.simulation_replay;
        }, "Requested replay did not immediately produce activity", 2s);
        check(repeated.transmitting && !repeated.simulation_replay_available && repeated.replay_waterfall &&
              repeated.simulation_pattern_replay_active &&
              repeated.simulation_pattern_replay_steps &&
              repeated.simulation_pattern_replay_steps->size() == completed.pattern_steps.size() &&
              repeated.simulation_pattern_replay_steps->front().ordinal == completed.pattern_steps.front().ordinal &&
              repeated.simulation_pattern_replay_visible_steps <= repeated.simulation_pattern_replay_steps->size() &&
              repeated.replay_waterfall->gain_db == original.gain_db &&
              repeated.replay_waterfall->search_band_hz == original.search_band_hz &&
              repeated.waveform_sample_rate == completed.waveform_sample_rate &&
              repeated.spectrum_revision > completed.spectrum_revision &&
              repeated.spectrum_start_sample <= completed.spectrum_start_sample,
              "Requested replay lost the original calibrated sample interval");
        session.cancel_transmit();
        const auto stopped = wait(session, [](const auto& out) { return !out.transmitting; },
                                   "Requested replay was not cancellable", 2s);
        check(stopped.simulation_replay_available && !stopped.simulation_replay &&
              stopped.status.find("replay stopped") != std::string::npos,
              "Cancelling manual replay discarded the retained activity");
    }
    check(fixture::capture_opens == captures && fixture::playback_opens == playbacks,
          "Simulation acquired a physical audio device");
    if (gpu) {
        check(!completed.gpu_device.empty() && !completed.gpu_backend.empty() &&
              (completed.gpu_score_rows > 0 || completed.gpu_statistics_blocks > 0),
              "GPU simulation decoded without actual accelerated receiver work");
        if (completed.gpu_backend.starts_with("cuda:")) {
            check(completed.gpu_vulkan_shared_workspace_limit_bytes == 0 &&
                  completed.gpu_vulkan_leased_bytes == 0 && completed.gpu_native_coarse_workspace_sessions == 0,
                  "CUDA session reported Vulkan workspace execution receipts");
            check(completed.gpu_fp32_score_rows == 0 && completed.gpu_fp16_storage_score_rows == 0 &&
                  completed.gpu_packed_fp16_score_rows == 0,
                  "CUDA work was misreported in Vulkan precision counters");
            if (gpu_fp32)
                check(completed.gpu_cuda_fp16_calls == 0 && completed.gpu_cuda_int8_calls == 0,
                      "CUDA FP32 minimum executed reduced-precision matrix contractions");
        } else {
            check(completed.gpu_vulkan_shared_workspace_limit_bytes > 0 &&
                  completed.gpu_vulkan_peak_leased_bytes > 0,
                  "Vulkan session omitted automatic shared workspace admission/execution evidence");
            check(completed.gpu_fp32_score_rows + completed.gpu_fp16_storage_score_rows == completed.gpu_score_rows &&
                  completed.gpu_packed_fp16_score_rows <= completed.gpu_fp16_storage_score_rows,
                  "Vulkan session precision receipts do not account for actual scored work");
            if (gpu_fp32)
                check(completed.gpu_fp32_score_rows == completed.gpu_score_rows &&
                      completed.gpu_fp16_storage_score_rows == 0 && completed.gpu_packed_fp16_score_rows == 0,
                      "Vulkan FP32 minimum did not execute solely FP32 scoring");
        }
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
        check(!resumed.gpu_device.empty() && !resumed.gpu_backend.empty() && resumed.gpu_score_rows == 0 &&
              resumed.gpu_statistics_blocks == 0 && resumed.gpu_cuda_native_fine_sessions == 0 &&
              resumed.gpu_cuda_native_fine_fallback_reason.empty() &&
              resumed.gpu_vulkan_leased_bytes == 0 && resumed.gpu_vulkan_peak_leased_bytes == 0 &&
              resumed.gpu_native_coarse_workspace_sessions == 0 &&
              resumed.gpu_cuda_fp16_calls == 0 && resumed.gpu_cuda_int8_calls == 0 &&
              resumed.gpu_cuda_half_fft_score_rows == 0 && resumed.gpu_cuda_half_fft_fallback_summary.empty(),
              "Resumed GPU generation retained stale completed-work evidence");
        if (resumed.gpu_backend.starts_with("vulkan:"))
            check(resumed.gpu_vulkan_shared_workspace_limit_bytes > 0 && resumed.gpu_vulkan_peak_leased_bytes == 0,
                  "Resumed Vulkan generation did not reset and reinitialize workspace receipts");
        else
            check(resumed.gpu_vulkan_shared_workspace_limit_bytes == 0 && resumed.gpu_vulkan_leased_bytes == 0 &&
                  resumed.gpu_native_coarse_workspace_sessions == 0,
                  "Resumed non-Vulkan generation retained stale Vulkan workspace receipts");
        check(resumed.gpu_fp32_score_rows == 0 && resumed.gpu_fp16_storage_score_rows == 0 &&
              resumed.gpu_packed_fp16_score_rows == 0 && resumed.gpu_native_coarse_lag_sessions == 0,
              "Resumed GPU generation retained stale precision evidence");
        simulated.gpu = false;
        session.configure(simulated);
        const auto cpu = ready(session);
        check(cpu.gpu_device.empty() && cpu.gpu_backend.empty() && cpu.gpu_selection_reason.empty() &&
              cpu.gpu_score_rows == 0 && cpu.gpu_precision.empty() && cpu.gpu_statistics_blocks == 0 &&
              cpu.gpu_cuda_native_fine_sessions == 0 && cpu.gpu_cuda_native_fine_fallback_reason.empty() &&
              cpu.gpu_vulkan_shared_workspace_limit_bytes == 0 && cpu.gpu_vulkan_leased_bytes == 0 &&
              cpu.gpu_vulkan_peak_leased_bytes == 0 && cpu.gpu_native_coarse_workspace_sessions == 0 &&
              cpu.gpu_host_scored_rows == 0 && cpu.gpu_cuda_fp16_calls == 0 && cpu.gpu_cuda_int8_calls == 0 &&
              cpu.gpu_cuda_half_fft_score_rows == 0 && cpu.gpu_cuda_half_fft_fallback_summary.empty() &&
              cpu.gpu_fp32_score_rows == 0 && cpu.gpu_fp16_storage_score_rows == 0 &&
              cpu.gpu_packed_fp16_score_rows == 0 && cpu.gpu_native_coarse_lag_sessions == 0 &&
              !cpu.gpu_native_lag_preference_supported,
              "Switching GPU off retained the old generation's backend or work evidence");
        simulated.gpu = true;
        session.configure(simulated);
        check(!ready(session).gpu_device.empty(), "Re-enabling GPU did not prepare a new backend");
    }
    close(session);
    check(session.snapshot().received.empty(), "Stopped simulation emitted a late duplicate message");
}

void bridge_helper_fhss_simulation() {
    auto s = settings(true);
    s.data_cadence = live::Settings{}.data_cadence;
    s.transfer.modem.carrier_hz = 1775;
    s.added_symbols = 10;
    s.added_interleave = true;
    s.helper.power_w = .1;
    s.fhss_profile = "fake-fcc";
    s.receiver_memory_limit = 3ULL * 1024 * 1024 * 1024;
    live::Session session;
    session.start(s);
    const auto prepared = ready(session);
    check(prepared.slot_stride_seconds && *prepared.slot_stride_seconds * 6 * 6000 < 3'500'000,
          "Helper and FHSS bridge fixture exceeds its finite sample budget");
    session.transmit(Message{.data = {'e'}});
    check(session.snapshot().transmit_trace.emitting,
          "Robust TX scope did not mark the simulated send as emitting");
    std::vector<transfer::Received> received;
    const auto finished = wait(session, [&](auto& out) {
        for (auto& record : out.received) received.push_back(std::move(record));
        check(out.error.empty() && out.receiver_error.empty(),
              "Helper and FHSS bridge simulation failed: " + out.error + out.receiver_error);
        return !out.transmitting && out.simulation_replay_available;
    }, "Helper and FHSS bridge simulation timed out", 75s);
    check(received.size() == 1 && received.front().stream_complete &&
          received.front().content.message.data == Bytes{'e'} &&
          finished.pattern_steps.size() == 5 &&
          finished.pattern_steps[3].decision == live::PatternDecision::quiet &&
          finished.pattern_steps[4].decision == live::PatternDecision::quiet &&
          !finished.transmit_trace.emitting,
          "Helper and FHSS bridge lost its message or physical quiet completion");
    const auto& trace = finished.transmit_trace;
    const auto key = s.transfer.key.value_or(Crypto::amateur());
    check(trace.fhss_available && trace.fhss_key.size() == 8 &&
          trace.fhss_byte_offset == trace.fhss_cycle * 8 &&
          trace.fhss_key == key.stream(StreamPurpose::Fhss, trace.fhss_epoch,
              trace.fhss_cycle * 8, 8),
          "FHSS trace did not retain the exact eight bytes used by the configured UTC cycle");
    close(session);
}

bool native_lag_simulation() {
    // One bounded actual Session fixture for the exact public one-arrival,
    // wide-frequency route. The selection counter is not completion evidence;
    // physical DATA and two scored quiet slots must also complete separately.
    const auto resources = triad::v3::resolve_stream_receiver_resources();
    if (resources.hardware_threads < 12 || resources.workers < 24) return false;
    auto simulated = settings(true);
    simulated.gpu = true;
    simulated.receiver_memory_limit = 4ULL * 1024 * 1024 * 1024;
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
    simulated.transfer.modem.oscillator_search = modem::OscillatorSearchConfig{
        .lf = {0, 0}, .rf = {100, 0}, .rf_shift_hz = 200000, .margin = 1};
    live::Session session;
    session.start(simulated);
    const auto prepared = ready(session);
    if (!prepared.gpu_native_lag_preference_supported) { close(session); return false; }
    check(prepared.bit_seconds && prepared.slot_stride_seconds &&
          *prepared.slot_stride_seconds * 6 * 1500 < 600000,
          "Native lag simulation exceeds its declared sample budget");
    session.transmit(Message{.data = {'e'}});
    std::vector<transfer::Received> received;
    auto completed = wait(session, [&](auto& out) {
        for (auto& event : out.received) received.push_back(std::move(event));
        check(out.error.empty() && out.receiver_error.empty(),
              "Native lag simulation failed: " + out.error + "; " + out.receiver_error);
        check(out.pattern_steps.size() <= 5, "Native lag simulation exceeded five public slots");
        return !out.transmitting;
    }, "Native lag simulation exceeded its wall-time budget", 90s);
    check(completed.gpu_native_coarse_lag_sessions > 0 && completed.gpu_score_rows == 0,
          "Qualified GUI session did not select native lag without claiming GPU scores");
    check(received.size() == 1 && received[0].stream_complete && received[0].content_validated &&
          received[0].content.message.data == Bytes{'e'} && completed.pattern_steps.size() == 5,
          "Native lag selection did not produce a physically complete message");
    for (std::size_t i = 0; i < completed.pattern_steps.size(); ++i)
        check(completed.pattern_steps[i].evidence_complete &&
              completed.pattern_steps[i].decision == (i < 3 ? live::PatternDecision::data : live::PatternDecision::quiet),
              "Native lag simulation lost DATA or physical quiet evidence");
    simulated.gpu = false;
    session.configure(simulated);
    const auto cpu = ready(session);
    check(cpu.gpu_device.empty() && !cpu.gpu_native_coarse_lag_sessions &&
          !cpu.gpu_native_lag_preference_supported,
          "GPU disable retained the native lag selection receipt");
    close(session);
    std::cout << "GPU Session selected native lag; physical e/001 completion passed\n";
    return true;
}

void unavailable_gpu() {
    auto requested = settings(true);
    requested.gpu = true;
    if (!live::gpu_receiver_available()) {
        bool rejected = false;
        try { live::validate_settings(requested); }
        catch (const Error& error) { rejected = std::string(error.what()).find("CUDA or Vulkan support required") != std::string::npos; }
        check(rejected, "CPU-only build accepted requested GPU processing");
        return;
    }
    // Device discovery is deliberately masked by the build's GPU-unavailable
    // test mode. Failure is asynchronous and visible; it must not claim CPU as GPU.
    live::Session session;
    session.start(requested);
    const auto failed = wait(session, [](const auto& out) { return !out.receiver_error.empty(); },
                             "Missing GPU did not report its initialization failure");
    check(!failed.running && failed.transmit_unavailable && failed.gpu_device.empty() && failed.gpu_backend.empty() &&
          failed.gpu_selection_reason.empty() && !failed.gpu_score_rows && !failed.gpu_statistics_blocks &&
          !failed.gpu_cuda_native_fine_sessions && failed.gpu_cuda_native_fine_fallback_reason.empty() &&
          !failed.gpu_vulkan_shared_workspace_limit_bytes && !failed.gpu_vulkan_leased_bytes &&
          !failed.gpu_vulkan_peak_leased_bytes && !failed.gpu_native_coarse_workspace_sessions,
          "Unavailable GPU silently fell back or reported successful work");
    requested.gpu = false;
    session.configure(requested);
    check(ready(session).receiver_error.empty(), "GPU initialization failure prevented returning to CPU");
    close(session);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--carrier-band") {
            carrier_band_matches_profile();
            fractional_spacing_helper_profile();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--passband") {
            robust_rate_fits_typical_converter();
            std::cout << "Robust startup rate and audio passband checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--simulation-admission") {
            simulation_first_window_admission();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu") {
            check(live::gpu_receiver_available(), "Actual GPU session test requires a compiled GPU backend");
            complete_native_simulation(true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-fp32") {
            check(live::gpu_receiver_available(), "Actual GPU session test requires a compiled GPU backend");
            complete_native_simulation(true, true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-native-lag") {
            check(live::gpu_receiver_available(), "Actual GPU session test requires a compiled GPU backend");
            if (!native_lag_simulation()) {
                std::cout << "Native lag Session geometry unavailable on this GPU or CPU allocation\n";
                return 77;
            }
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--nonstationary") {
            complete_native_simulation(false, false, true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--simulation-authority") {
            simulation_transmit_ignores_realized_channel();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--simulation-rx-preview") {
            complete_native_simulation();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--modeled-waterfall") {
            modeled_simulation_waterfall();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--continuous-data") {
            data_cadence_profiles();
            complete_native_simulation(false, false, false, live::DataCadence::continuous);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--scheduled-fill") {
            scheduled_fill_profiles();
            complete_native_simulation(false, false, false, live::DataCadence::scheduled_fill);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--scheduled-bridge") {
            scheduled_bridge_profiles();
            complete_native_simulation(false, false, false, live::Settings{}.data_cadence);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--bridge-helper-fhss") {
            bridge_helper_fhss_simulation();
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--fixed-receiver-link") {
            const std::string_view gain = argv[2];
            check(gain == "0" || gain == "30", "The dated fixed-link fixture supports gain 0 or 30 dB");
            dated_live_data_then_eof(gain == "30" ? 30 : 0);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--automatic-noise") {
            automatic_noise_capture_lifecycle();
            dated_live_data_then_eof({}, true, true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--oscilloscope") {
            live_oscilloscope_retains_environmental_window();
            std::cout << "Live Robust oscilloscope retains its full window and wraps in order\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--automatic-calendar") {
            automatic_live_first_contact();
            automatic_quiet_start_covers_default_arrival_window();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--automatic-calendar-latency") {
            automatic_live_first_contact(true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--gpu-unavailable") {
            unavailable_gpu();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--capture-backpressure") {
            capture_backpressure();
            std::cout << "Native capture backpressure and cancellation passed\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--buffered-live") {
            dated_live_data_then_eof(0, true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--finite-duplex") {
            finite_duplex_plan();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--finite-completion") {
            finite_duplex_completion();
            return 0;
        }
        check(argc == 1, "Unexpected Robust session test argument");
        carrier_band_matches_profile();
        robust_rate_fits_typical_converter();
        fractional_spacing_helper_profile();
        native_precision_profile_policy();
        configured_physical_carrier_context();
        if (!live::gpu_receiver_available()) unavailable_gpu();
        capture_backpressure();
        finite_duplex_plan();
        finite_duplex_completion();
        inactive_and_audio_ownership();
        live_transmit_and_cancel(false);
        live_transmit_and_cancel(true);
        long_live_prefix_and_cancel(false);
        long_live_prefix_and_cancel(true);
        full_duplex_reserves_long_preparation();
        nonstationary_channel_profile();
        midlatitude_hf_profile_uses_nominal_gaussian_rays();
        simulation_transmit_ignores_realized_channel();
        late_affine_clock_support();
        simulation_cancel_is_not_completion();
        simulation_first_window_admission();
        std::cout << "Native Robust lifecycle, duplex routing, and cancellation passed\n" << std::flush;
        raw_bit_session_transmission_and_ordinal();
        std::cout << "Native Robust single-bit raw transmission and ordinal checks passed\n" << std::flush;
        complete_native_simulation();
        std::cout << "Native Robust simulation decoded e/001 with two physical quiet slots\n" << std::flush;
        dated_live_data_then_eof();
        dated_live_data_then_eof(0, true);
        std::cout << "Native Robust Session lifecycle, audio options, and real e/001 completion passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
