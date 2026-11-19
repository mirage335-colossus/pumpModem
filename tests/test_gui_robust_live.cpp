#include "datapump/live.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "../src/live_capture_queue.hpp"
#include "../src/live_finite_plan.hpp"

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
std::atomic<bool> capture_overlap{false}, hold_playback{false};
std::atomic<bool> hold_capture{false}, reject_capture_endpoint{false};
std::atomic<bool> hold_playback_format{false}, hold_endpoint_ack{false}, endpoint_started{false};
std::atomic<bool> pause_released{true}, capture_paused{false};
std::atomic<bool> loopback_ready{false};
std::atomic<std::uint64_t> capture_position{0};
std::atomic<std::uint64_t> capture_endpoint{0};
std::string capture_device, playback_device;
audio::Options capture_options, playback_options;
audio::ChannelMode playback_channels = audio::ChannelMode::stereo;
std::size_t playback_samples = 0;
bool scheduled = false;
std::optional<long double> scheduled_epoch;
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
        StreamFormat actual{rate, rate, rate * .49, 4096};
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
        fixture::playback_prefix.clear();
        fixture::loopback_pcm.clear();
        fixture::loopback_epoch.reset();
    }
    ++fixture::playback_opens;
    while (fixture::hold_playback_format && !stop.stop_requested()) std::this_thread::sleep_for(2ms);
    if (stop.stop_requested()) return;
    detail::UtcPlaybackScope timed(options.follow_system_clock);
    if (format) format({rate, rate, rate * .49, 4096});
    {
        std::lock_guard lock(fixture::mutex);
        fixture::scheduled = detail::utc_playback_epoch.has_value();
        fixture::scheduled_epoch = detail::utc_playback_epoch;
        fixture::loopback_epoch = detail::utc_playback_epoch;
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

void long_live_prefix_and_cancel(bool duplex) {
    using namespace triad::v3;
    auto s = settings(false);
    s.transfer.modem.bandwidth_hz = 3600;
    s.transfer.fec = FecMode::rs20;
    s.transfer.key = Crypto::amateur();
    s.full_duplex = duplex;
    // These settings bound the receiver's HF search. With Simulation off,
    // physical playback must still be the canonical, unperturbed emission.
    s.spread_preset = "hf-worst";
    s.selective_perturbation = "each-65";
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
    check(std::abs(profile.receiver.spread_delay_seconds - .007) < 1e-12 &&
          std::abs(profile.receiver.spread_doppler_hz - 30.) < 1e-12 &&
          profile.receiver.selective_slow_depth == .65 &&
          profile.receiver.selective_fast_depth == .65,
          "HF Worst / Each 65% did not remain configured as receive-side search bounds");
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

void dated_live_data_then_eof(std::optional<int> fixed_link_gain_db = {}, bool buffered = false) {
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
    const auto profile = live::detail::prepare_profile(s, *s.calendar, {});
    const std::array<std::uint8_t, 1> bit{1};
    const std::array<std::uint8_t, 3> text{0, 0, 1}; // Native short-codec representation of 'e'.
    const std::span<const std::uint8_t> wire = fixed_link_gain_db ? std::span<const std::uint8_t>(text) :
        std::span<const std::uint8_t>(bit);
    Emission source(profile.waveform, profile.key, {EmissionMode::Message, 0, wire});
    const auto rate = s.transfer.modem.sample_rate;
    const auto prefix = 2U * rate;
    const auto expected_slots = wire.size() + (fixed_link_gain_db ? 2 : 0);
    const auto after_epoch = profile.waveform.slot(expected_slots - 1).observation.stop + rate;
    check(after_epoch + prefix < (fixed_link_gain_db ? 500000 : 100000),
          "Dated live fixture exceeds its sample budget");
    const auto noise_density = std::pow(10., *s.live_noise_dbfs_hz / 10);
    const auto actual_cn0 = s.target_cn0_db_hz + fixed_link_gain_db.value_or(0);
    const auto signal_gain = std::sqrt(noise_density * std::pow(10., actual_cn0 / 10));
    auto input = std::make_shared<fixture::CaptureInput>();
    input->first_utc = static_cast<long double>(s.calendar->epoch) - 2;
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
            check(fixed_link_gain_db && message.stream_complete && message.content_validated &&
                  message.short_text_decoded && message.content.message.data == Bytes{'e'} &&
                  std::ranges::equal(message.raw_bits, wire) && message.missing_symbols == 0,
                  "Dated Robust did not decode the expected complete native message");
            ++decoded_messages;
        }
        for (const auto& signal : snapshot.signals) {
            if (!signal.complete) continue;
            check(fixed_link_gain_db && signal.robust_reception && signal.validated && signal.raw_bits == "001",
                  "Dated Robust completed an unexpected reception");
            complete_signal = true;
        }
        return snapshot.receiver_error == "Robust audio input ended.";
    }, "Dated native DATA scoring did not finish", 45s);
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
          complete_signal == fixed_link_gain_db.has_value() && decoded_messages == (fixed_link_gain_db ? 1U : 0U),
          "Live input plots or expected physical completion are absent");
    close(session);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_input.reset();
    }
    if (fixed_link_gain_db)
        std::cout << "Dated Robust decoded e/001 at " << actual_cn0 << " dB-Hz with Target "
                  << s.target_cn0_db_hz << " dB-Hz, two quiet slots, " << profile.waveform.geometry().bit_seconds
                  << " s bits and peak PCM " << peak << '\n';
}

void raw_bit_session_transmission_and_ordinal() {
    auto simulated = settings(true);
    simulated.transfer.modem.sample_rate = 1500;
    simulated.transfer.modem.carrier_hz = 500;
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

void complete_native_simulation(bool gpu = false, bool gpu_fp32 = false, bool dynamic_channel = false) {
    // One predeclared functional stratum, not a sensitivity/capacity claim:
    // public calendar, clear e=001, FEC/Helper/FHSS Off and exact
    // oscillators, target and actual 80 dB-Hz with positive physical noise.
    // The default case has spread/selective Off. The optional nonstationary
    // case exercises HF Worst, Each 65%, and a bounded common-shift trajectory.
    // Both score at most five complete windows, 600,000 samples and 90s.
    const auto captures = fixture::capture_opens.load(), playbacks = fixture::playback_opens.load();
    auto simulated = settings(true);
    // A saved browser filter must not discard the native reception baseline.
    simulated.wrong_key_admission = "strict";
    simulated.gpu = gpu;
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
    // The public slot cadence is at least a minute. A supported lower logical
    // sample rate keeps the full physical calendar inside the sample budget.
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
    if (!decoded && dynamic_channel) {
        for (const auto& row : completed.pattern_steps) {
            std::cerr << "nonstationary slot=" << row.index
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
        if (argc == 2 && std::string_view(argv[1]) == "--simulation-rx-preview") {
            complete_native_simulation();
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--fixed-receiver-link") {
            const std::string_view gain = argv[2];
            check(gain == "0" || gain == "30", "The dated fixed-link fixture supports gain 0 or 30 dB");
            dated_live_data_then_eof(gain == "30" ? 30 : 0);
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
        late_affine_clock_support();
        simulation_cancel_is_not_completion();
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
