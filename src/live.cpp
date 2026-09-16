#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include "datapump/channel.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/symbol_schedule.hpp"
#include "signal_view.hpp"
#include "live_pattern_scores.hpp"
#include "live_receptions.hpp"
#include "transmit_timing.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <variant>

namespace datapump::live {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t plot_size = 2048;
constexpr std::size_t maximum_events = 64;
constexpr std::size_t maximum_recovery_jobs = 8;
constexpr std::size_t recovery_workspace_limit = 16 * 1024 * 1024;
constexpr std::size_t replay_frames = 60;
constexpr std::size_t replay_wave_samples = 256;
constexpr std::size_t replay_bins = 257;
constexpr std::size_t minimum_constellation_limit = 2048;
std::size_t constellation_limit(const modem::Config& config) {
    return std::max({minimum_constellation_limit, detail::SignalWindow::constellation_capacity(config),
                     modem::StreamingTransmitter::constellation_history_capacity(config)});
}
std::size_t constellation_limit(const Settings& settings) {
    return std::max(constellation_limit(settings.transfer.modem), settings.long_message_modem ?
        constellation_limit(*settings.long_message_modem) : std::size_t{});
}
constexpr std::size_t pattern_score_limit = Snapshot::pattern_score_limit;
constexpr auto replay_duration = std::chrono::seconds(3);
constexpr std::size_t replay_text_limit = 4096;
constexpr std::size_t reception_alias_bytes = detail::ReceptionHistory::capacity * sizeof(std::uint64_t);
struct ReplayFrame {
    std::vector<float> waveform, spectrum;
    std::vector<std::complex<float>> constellation, pattern_scores;
    std::vector<PatternScoreObservation> pattern_score_observations;
    std::uint64_t pattern_score_observation_id = 0;
    ConstellationSource source = ConstellationSource::input;
    double fraction = 0;
    std::uint64_t dropped = 0;
    modem::TransmitTrace transmit_trace;
};
constexpr std::size_t trace_prefix_workspace = modem::TransmitTrace::source_limit +
    4 * modem::TransmitTrace::bit_limit + 4 * modem::TransmitTrace::byte_limit;
constexpr std::size_t replay_frame_base = sizeof(ReplayFrame) +
    (replay_wave_samples + replay_bins) * sizeof(float) +
    pattern_score_limit * (sizeof(std::complex<float>) + sizeof(PatternScoreObservation)) +
    sizeof(std::optional<SignalUpdate>) + replay_text_limit + 64 + reception_alias_bytes + trace_prefix_workspace;
// One verified stream is moved into the receive-content cache at the deadline.
// Its payload uses the content quota; its diagnostics and caption use DSP space.
constexpr std::size_t replay_result_workspace = sizeof(transfer::Received) +
    sizeof(SignalUpdate) + 2*replay_text_limit + 64 + reception_alias_bytes + minimum_constellation_limit * sizeof(std::complex<double>);
std::size_t replay_workspace(const Settings& value) {
    return value.simulation ? std::min(value.dsp_workspace_bytes / 8,
        replay_result_workspace + replay_frames * (replay_frame_base + constellation_limit(value) * sizeof(std::complex<float>))) : 0;
}
double epoch_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string reception_id(const Message& message) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : message.local_id) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
std::string display_text(const Message& message) {
    if (message.kind != MessageKind::text)
        return terminal_text(Bytes(message.filename.begin(), message.filename.end())) +
               " (" + std::to_string(message.data.size()) + " bytes)";
    const auto size = std::min<std::size_t>(message.data.size(), 4096);
    return terminal_text(std::span<const std::uint8_t>(message.data).first(size));
}
constexpr std::size_t minimum_workspace = 512 * 1024;
std::size_t plot_workspace(const Settings& value) { return plot_size * (sizeof(float) + sizeof(double)) +
                                       constellation_limit(value) * 3 * sizeof(std::complex<double>) +
                                       detail::SignalWindow::sample_capacity(value.transfer.modem) * 2 * sizeof(float) +
                                       pattern_score_limit * (4 * (sizeof(std::complex<double>) + sizeof(PatternScoreObservation)) + sizeof(modem::PatternEvidence)) +
                                       sizeof(detail::PatternScoreHistory) +
                                       sizeof(detail::SignalWindow) + modem::SampledSimulationChannel::workspace_bound +
                                       plot_size * (sizeof(float)+sizeof(std::complex<double>)) +
                                       sizeof(modem::TransmitTrace) + trace_prefix_workspace + replay_workspace(value) +
                                       maximum_events * reception_alias_bytes; }
std::size_t audio_reserve(const Settings& value) {
    return value.simulation ? 0 : std::min<std::size_t>(5 * 1024 * 1024, value.dsp_workspace_bytes / 8);
}
std::size_t bank_capacity(const Settings& value) {
    // Simulation feeds sampled audio directly; it never allocates
    // the asynchronous hardware capture queue.
    const auto capture_queue = value.simulation ? 0 : value.dsp_workspace_bytes / 8;
    const auto reserved = capture_queue + value.dsp_workspace_bytes / 4 + plot_workspace(value) + audio_reserve(value);
    if (reserved >= value.dsp_workspace_bytes) throw Error("DSP workspace cannot hold the audio and plot buffers");
    return value.dsp_workspace_bytes - reserved;
}
std::size_t source_budget(std::size_t content) {
    return transfer::source_storage_limit(content);
}
modem::ChannelConfig channel_config(const Settings& settings) {
    modem::ChannelConfig channel;
    channel.snr_db = settings.simulation_snr_db;
    channel.clock_error_ppm = settings.simulation_clock_error_ppm;
    channel.phase_noise_degrees_per_sqrt_second = settings.simulation_phase_noise_degrees_per_sqrt_second;
    channel.seed = settings.simulation_seed;
    return channel;
}
Settings normalized(Settings value) {
    if (value.content_limit == default_memory_limit) value.content_limit = value.transfer.content_limit;
    if (value.dsp_workspace_bytes == runtime::default_dsp_workspace_bytes()) value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes;
    value.transfer.content_limit = value.content_limit;
    value.transfer.dsp_workspace_bytes = value.dsp_workspace_bytes;
    if (value.transfer.modem.spreading_mode == modem::SpreadingMode::tone) {
        value.transfer.key.reset(); value.receive_keys.clear();
        value.transfer.modem.data_key.reset();
        value.transfer.modem.scramble = value.transfer.modem.dsss = false;
        value.transfer.modem.spreading_seed.fill(0); value.transfer.modem.dsss_seed.fill(0);
        if (value.transfer.automatic_receive_profiles && value.transfer.receive_pattern_mode != tuning::PatternMode::auto_tone &&
            value.transfer.receive_pattern_mode < tuning::PatternMode::tone_1)
            value.transfer.receive_pattern_mode = tuning::PatternMode::auto_tone;
    } else if (value.transfer.key) value.transfer.modem.scramble = true;
    modem::validate(value.transfer.modem);
    if (value.long_message_modem) {
        auto& longer = *value.long_message_modem;
        const auto& base = value.transfer.modem;
        if (longer.sample_rate != base.sample_rate || longer.carrier_hz != base.carrier_hz ||
            longer.bandwidth_hz != base.bandwidth_hz || longer.spreading_mode != base.spreading_mode)
            throw Error("short and long transmit profiles must share sample rate, carrier, bandwidth and spreading mode");
        if (longer.spreading_mode == modem::SpreadingMode::tone) {
            longer.data_key.reset(); longer.scramble = longer.dsss = false;
            longer.spreading_seed.fill(0); longer.dsss_seed.fill(0);
        } else if (value.transfer.key) longer.scramble = true;
        modem::validate(longer);
        if ((longer.scramble || longer.dsss) && !value.transfer.key && value.receive_keys.empty())
            throw Error("encrypted spreading requires a loaded key");
    }
    if (!value.simulation && value.transfer.modem.bandwidth_hz > 192000)
        throw Error("This bandwidth requires an SDR frontend; this build supports audio hardware and simulation. Enable simulation for the selected band.");
    if (!value.content_limit) throw Error("received content cache must have a positive capacity");
    (void)source_budget(value.content_limit);
    if (value.dsp_workspace_bytes < minimum_workspace) throw Error("streaming DSP workspace must be at least 512 KiB");
    (void)bank_capacity(value);
    if (!std::isfinite(value.simulation_snr_db) || std::abs(value.simulation_snr_db) > 200)
        throw Error("simulation sample SNR must be finite and between -200 and 200 dB");
    modem::validate_channel(value.transfer.modem, channel_config(value));
    if (value.receive_keys.size() > 128) throw Error("continuous receiver supports at most 128 loaded keys");
    if (value.transfer.search_seconds > 60) throw Error("continuous timing search exceeds 60 seconds");
    if ((value.transfer.modem.scramble || value.transfer.modem.dsss) &&
        !value.transfer.key && value.receive_keys.empty()) throw Error("encrypted spreading requires a loaded key");
    return value;
}

using Plots = detail::SignalPlots;
using detail::signal_plots;
void count_dropped(std::uint64_t& total, std::uint64_t count) {
    total += std::min(count, std::numeric_limits<std::uint64_t>::max() - total);
}
void append_points(modem::ConstellationBatch& target, modem::ConstellationBatch batch,
                   std::size_t limit) {
    count_dropped(target.dropped, batch.dropped);
    if (batch.points.size() > limit) {
        count_dropped(target.dropped, batch.points.size() - limit);
        batch.points.erase(batch.points.begin(), batch.points.end() - static_cast<std::ptrdiff_t>(limit));
    }
    const auto excess = target.points.size() + batch.points.size() > limit ?
        target.points.size() + batch.points.size() - limit : 0;
    count_dropped(target.dropped, excess);
    target.points.erase(target.points.begin(), target.points.begin() + static_cast<std::ptrdiff_t>(excess));
    target.points.reserve(limit);
    target.points.insert(target.points.end(), batch.points.begin(), batch.points.end());
}
}

struct Session::Impl {
    struct Prepared {
        modem::Config modem;
        std::unique_ptr<modem::StreamingTransmitter> transmitter;
        std::function<void(Prepared&)> prepare_hardware;
        std::optional<std::uint64_t> protected_epoch;
        bool binary = false;
        bool noise = false;
        std::uint64_t generation = 0, serial = 0;
        std::uint64_t transmitted_samples = 0;
        std::uint64_t tail_remaining = 0;
        bool tail_started = false;
        std::vector<ReplayFrame> replay;
        std::vector<std::optional<SignalUpdate>> signals;
        std::optional<SignalUpdate> verified;
        std::optional<transfer::Received> received;
        // Stamped when a physically completed result is staged, so clearing
        // history can invalidate a source-thread result before replay handoff.
        std::uint64_t recovery_clear_generation=0;
        std::size_t replay_count = 0, point_limit = 0;
        std::vector<std::complex<double>> pattern_scores;
        std::vector<PatternScoreObservation> pattern_score_observations;
        std::uint64_t pattern_score_observation_id = 0;
        std::stop_token stop;
    };
    struct AudioBlock { std::vector<float> samples; std::uint64_t revision; };
    struct RecoveryTask {
        transfer::Received result;
        std::shared_ptr<transfer::RecoveryJob> job;
        SignalUpdate event;
        std::stop_source stop;
        std::uint64_t generation=0;
        std::size_t reserved_bytes=0, retained_bytes=0;
        bool started=false, obsolete=false;
    };
    struct Receiver {
        transfer::Options options;
        Bytes key_tag;
        std::uint64_t epoch = 0;
        double admitted_at = 0;
        double last_confident_at = 0;
        std::uint64_t last_confident_end = 0;
        std::uint64_t sample_origin = 0, family = 0;
        detail::PatternScoreHistory pattern_score_history;
        struct Presentation {
            std::uint64_t candidate_id=0, signal_id=0, published_revision=0;
            bool content_reported=false;
        };
        // Candidate chunks may interleave. Keep each physical identity until
        // its actual completion, with the same bound as the content collector.
        std::map<std::pair<std::uint64_t,std::uint64_t>,Presentation> presentations;
        std::vector<modem::PatternBurst> bursts;
        std::unique_ptr<modem::StreamingReceiver> modem;
        std::unique_ptr<transfer::StreamReceiver> content;
    };
    struct Bank {
        std::shared_ptr<transfer::ReceiveStorageQuota> source_quota;
        std::vector<Receiver> receivers;
        detail::ReceptionHistory receptions;
        std::uint64_t samples = 0, next_candidate = 1;
        std::size_t working_bytes = sizeof(detail::ReceptionHistory);
        double created_at = 0;
        bool limited = false;
        std::complex<double> mixer{1,0};
    };
    static std::size_t receiver_workspace(const Receiver& receiver) {
        // Reserve bounded diagnostic growth and control storage before
        // admitting a receiver, rather than reporting only its idle allocation.
        const auto& config=receiver.options.modem;
        const auto compact=receiver.options.key && modem::symbol_sample_count(config)>=60ULL*config.sample_rate;
        const auto margin=compact?8192:65536;
        std::size_t burst_bytes=receiver.bursts.capacity()*sizeof(modem::PatternBurst);
        for(const auto& burst:receiver.bursts)burst_bytes+=burst.bits.capacity();
        return receiver.modem->working_bytes() + margin + sizeof(Receiver) + burst_bytes +
            receiver.presentations.size()*(sizeof(decltype(receiver.presentations)::value_type)+4*sizeof(void*)) +
            (receiver.content?receiver.content->working_bytes():0);
    }
    EpochClock epoch_clock;
    ReplayClock replay_clock;
    std::mutex mutex;
    std::condition_variable_any changed;
    Settings settings;
    Snapshot current;
    std::uint64_t generation = 0, decoder_generation = 0, tx_serial = 0, receive_revision = 0, next_signal = 1, next_event = 1;
    std::uint64_t last_pattern_transmit_epoch = 0;
    std::uint64_t recovery_clear_generation = 0;
    std::array<std::uint8_t,16> reception_namespace{};
    std::atomic<std::uint64_t> pattern_score_observation_id{0};
    bool tx_busy = false;
    Clock::time_point next_hardware_send{};
    using Transmission = std::variant<Message, Bytes, modem::Noise>;
    std::deque<Transmission> queued;
    std::shared_ptr<Prepared> ready;
    std::deque<AudioBlock> input;
    std::size_t input_bytes = 0, decoding_bytes = 0, received_bytes = 0, receiver_bytes = 0;
    std::size_t audio_bytes = 0;
    // At most one coordinator runs; each job owns its bounded parallel search.
    std::deque<std::shared_ptr<RecoveryTask>> recoveries;
    std::shared_ptr<RecoveryTask> active_recovery;
    modem::ConstellationBatch pending_points;
    ConstellationSource pending_source = ConstellationSource::input;
    std::vector<ReplayFrame> replay;
    std::vector<std::optional<SignalUpdate>> replay_signals;
    std::optional<SignalUpdate> replay_verified;
    std::optional<transfer::Received> replay_received;
    std::size_t replay_signal_cursor = 0, staged_received_bytes = 0;
    std::uint64_t replay_signal_id = 0;
    std::uint64_t replay_omitted = 0;
    Clock::time_point replay_started{};
    std::optional<std::size_t> delivered_replay_frame;
    std::size_t first_visible_replay_frame = 0;
    double replay_bin_hz = 0;
    std::stop_source capture_stop, tx_stop, decode_stop;
    std::jthread source, encoder, decoder, recovery_worker;

    explicit Impl(EpochClock clock, ReplayClock presentation_clock)
        : epoch_clock(clock ? std::move(clock) : EpochClock(epoch_now)),
          replay_clock(presentation_clock ? std::move(presentation_clock) : ReplayClock(Clock::now)) {
        (void)current_epoch();
        // Local cache identity only: this namespace never enters the wire.
        std::random_device random;
        for(auto& byte:reception_namespace)byte=static_cast<std::uint8_t>(random());
        source = std::jthread([this](std::stop_token stop) { source_loop(stop); });
        encoder = std::jthread([this](std::stop_token stop) { encode_loop(stop); });
        decoder = std::jthread([this](std::stop_token stop) { decode_loop(stop); });
        recovery_worker = std::jthread([this](std::stop_token stop) { recovery_loop(stop); });
    }
    ~Impl() {
        halt(); source.request_stop(); encoder.request_stop(); decoder.request_stop(); recovery_worker.request_stop(); changed.notify_all();
        source.join(); encoder.join(); decoder.join(); recovery_worker.join();
    }
    double current_epoch() const {
        const auto now = epoch_clock();
        if (!std::isfinite(now) || now < 0 ||
            static_cast<long double>(now) >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            throw Error("epoch clock must return finite nonnegative Unix seconds within uint64 range");
        return now;
    }
    std::string idle_status() const {
        return settings.simulation ? "Simulated independent radios; sampled audio, clock error and phase noise" : "Listening to audio input";
    }
    void clear_replay(bool discard_pending = true) {
        if (discard_pending && replay_signal_id)
            std::erase_if(current.signals, [this](const auto& event) {
                return !event.validated && event.id == replay_signal_id;
            });
        replay = {}; delivered_replay_frame.reset(); first_visible_replay_frame = 0;
        replay_signals = {}; replay_verified.reset(); replay_received.reset();
        replay_signal_cursor = staged_received_bytes = 0; replay_signal_id = 0;
        current.simulation_replay = false;
        current.replay_frame_index = current.replay_frame_count = 0;
        current.simulation_sample_fraction = 0;
        current.pattern_scores.clear();
        current.pattern_score_observations.clear();
    }
    void discard_staged_recovery(std::optional<transfer::Received>& received,
                                 std::optional<SignalUpdate>& completed,
                                 std::vector<std::optional<SignalUpdate>>& events) {
        const bool recovering=(received && (received->recovery || received->recovery_progress.state!=transfer::RecoveryState::none)) ||
            (completed && completed->recovery_progress.state!=transfer::RecoveryState::none);
        if(!recovering)return;
        const auto id=completed?completed->id:0;
        received.reset();completed.reset();staged_received_bytes=0;
        if(!id)return;
        for(auto& event:events)if(event && event->id==id)event.reset();
        std::erase_if(current.signals,[&](const auto& event){return event.id==id;});
        if(replay_signal_id==id)replay_signal_id=0;
    }
    // Called with mutex held, after validation. Invalid input must leave a
    // currently presented simulation and its pending result untouched.
    void enqueue(Transmission transmission) {
        if (current.transmitting_noise) throw Error("stop noise before transmitting a message");
        const bool noise = std::holds_alternative<modem::Noise>(transmission);
        if (noise && (tx_busy || !queued.empty())) throw Error("wait for transmission to finish before starting noise");
        if (queued.size() >= 8) throw Error("transmit queue contains eight pending messages");
        advance_replay(replay_clock());
        queued.push_back(std::move(transmission)); current.transmitting = true;
        current.transmission_finished = current.transmission_cancelled = false;
        if (!tx_busy) {
            current.transmitting_noise = noise;
            current.transmission_fraction = current.transmission_seconds = 0;
            current.transmit_trace = {};
            clear_replay();
        }
        pending_points = {}; replay_omitted = 0;
        current.constellation.clear(); current.constellation_source = ConstellationSource::input;
        current.constellation_dropped = 0;
        current.error.clear(); changed.notify_all();
    }
    void queue_points(modem::ConstellationBatch batch, ConstellationSource source_kind) {
        if (batch.points.empty() && !batch.dropped) return;
        if (pending_source != source_kind) pending_points = {};
        pending_source = source_kind;
        append_points(pending_points, std::move(batch), constellation_limit(settings));
    }
    void advance_replay(Clock::time_point now) {
        if (replay.empty()) return;
        const auto elapsed = std::max(Clock::duration::zero(), now - replay_started);
        const bool finished = elapsed >= replay_duration;
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::min(elapsed, Clock::duration(replay_duration))).count();
        const auto due = finished ? replay_signals.size() : std::min(replay_signals.size(),
            static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL) + 1);
        // Retain only the displayed generation frame if replay is cancelled.
        // The CPU may already have generated the entire transmission.
        const auto trace_index = finished ? replay.size() - 1 : std::min(replay.size() - 1,
            static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL));
        current.transmit_trace = replay[trace_index].transmit_trace;
        while (replay_signal_cursor < due) {
            auto& event = replay_signals[replay_signal_cursor++];
            if (event && !(finished && replay_verified && event->id==replay_verified->id))
                append_signal(std::move(*event));
            event.reset();
        }
        if (elapsed >= replay_duration) {
            if(replay_received && replay_received->recovery && replay_verified) {
                // Search starts after this simulation's physical completion
                // reaches the presentation clock, preserving replay ordering.
                queue_recovery(std::move(*replay_received),*replay_verified);
                replay_received.reset();staged_received_bytes=0;
            }
            if (replay_verified) {
                // A stalled consumer needs the final state once, rather than
                // every unpresented prefix followed by that same final state.
                const auto id=replay_verified->id;
                std::erase_if(current.signals,[&](const auto& event){return event.id==id;});
                append_signal(std::move(*replay_verified));
            }
            if (replay_received) {
                staged_received_bytes = 0;
                admit_received(replay_received->content.message.data.size(), settings.content_limit);
                current.received.push_back(std::move(*replay_received));
            }
            // A stalled UI must not extend the replay or paint old symbol
            // coordinates over live input. Account for undelivered points.
            const auto first = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            for (auto i = first; i < replay.size(); ++i) {
                count_dropped(replay_omitted, replay[i].constellation.size());
                count_dropped(replay_omitted, replay[i].dropped);
            }
            clear_replay(false); current.status = idle_status(); ++current.sequence;
            changed.notify_all();
        }
    }
    void replay_snapshot(Snapshot& result, Clock::time_point now) {
        if (replay.empty()) return;
        const auto elapsed = std::max(Clock::duration::zero(), now - replay_started);
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        const auto index = static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL);
        if (!delivered_replay_frame || index != *delivered_replay_frame) {
            first_visible_replay_frame = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            delivered_replay_frame = index;
        }
        const auto& frame = replay[index];
        result.transmit_trace = frame.transmit_trace;
        result.simulation_replay = true; result.replay_frame_index = index; result.replay_frame_count = replay.size();
        result.simulation_sample_fraction = frame.fraction;
        result.waveform = frame.waveform; result.spectrum_db.assign(frame.spectrum.begin(), frame.spectrum.end());
        result.spectrum_bin_hz = replay_bin_hz; result.constellation_source = frame.source;
        modem::ConstellationBatch visible;
        // Delayed UI polls consume intervening points together, not a long
        // rolling history. Coordinate/source changes must never be mixed.
        for (auto i = first_visible_replay_frame; i <= index; ++i) {
            if (replay[i].source != frame.source) { visible = {}; continue; }
            modem::ConstellationBatch points; points.dropped = replay[i].dropped;
            for (const auto point : replay[i].constellation) points.points.emplace_back(point.real(), point.imag());
            append_points(visible, std::move(points), constellation_limit(settings));
        }
        result.constellation = std::move(visible.points); result.constellation_dropped = visible.dropped;
        // Each frame already contains a bounded history; replacing it keeps
        // repeated polls stable and never leaks later evidence into replay.
        result.pattern_scores.assign(frame.pattern_scores.begin(), frame.pattern_scores.end());
        result.pattern_score_observations = frame.pattern_score_observations;
        result.pattern_score_observation_id = frame.pattern_score_observation_id;
    }
    void halt() {
        std::lock_guard lock(mutex);
        current.running = current.transmitting = current.transmitting_noise = false;
        current.transmission_finished = true; current.transmission_cancelled = true;
        current.status = "Stopped";
        clear_replay(); pending_points = {};
        replay_omitted = 0;
        ++generation; ++tx_serial; ++receive_revision;
        queued.clear(); ready.reset(); tx_busy = false; input.clear(); input_bytes = 0;
        const auto unavailable=unavailable_recovery_events();invalidate_recoveries();
        for(auto event:unavailable)append_signal(std::move(event));
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop(); changed.notify_all();
    }
    void configure(const Settings& requested) {
        auto value = normalized(requested);
        std::lock_guard lock(mutex);
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop();
        decode_stop = std::stop_source{};
        settings = std::move(value); ++generation; ++tx_serial; ++receive_revision;
        queued.clear(); ready.reset(); tx_busy = false; input.clear();
        input_bytes = receiver_bytes = received_bytes = audio_bytes = 0;
        auto unavailable=unavailable_recovery_events();invalidate_recoveries();
        clear_replay(); pending_points = {};
        replay_omitted = 0;
        current = {}; current.running = true; current.simulation = settings.simulation;
        for(auto& event:unavailable)append_signal(std::move(event));
        current.status = idle_status(); changed.notify_all();
    }
    void account(std::uint64_t samples, std::uint64_t version) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (samples > std::numeric_limits<std::uint64_t>::max() - current.samples_received)
            throw Error("virtual sample clock exceeds its platform range");
        current.samples_received += samples;
        current.virtual_seconds = static_cast<double>(current.samples_received) / settings.transfer.modem.sample_rate;
    }
    void audio_format(const audio::StreamFormat& format, std::uint64_t version) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (format.workspace_bytes > audio_reserve(settings))
            throw Error("sample-rate conversion exceeds the audio workspace reservation; increase DSP capacity");
        current.hardware_sample_rate = format.hardware_rate;
        current.audio_passband_hz = format.usable_passband_hz;
        audio_bytes = format.workspace_bytes;
        const auto upper_edge = settings.transfer.modem.carrier_hz + settings.transfer.modem.bandwidth_hz / 2;
        if (upper_edge > format.usable_passband_hz)
            throw Error("Selected upper band edge (" + std::to_string(upper_edge) +
                        " Hz) exceeds this audio path's usable passband (" +
                        std::to_string(format.usable_passband_hz) + " Hz). Select a narrower band, a wider audio device, or simulation.");
    }
    void publish(const detail::SignalWindow& window, const modem::Config& config, std::uint64_t version,
                 Clock::time_point& last_plot, bool force = false,
                 modem::StreamingTransmitter* transmitter = nullptr, std::uint64_t serial = 0,
                 const std::vector<std::complex<double>>* pattern_scores = nullptr,
                 const std::vector<PatternScoreObservation>* pattern_observations = nullptr,
                 std::uint64_t observation_id = 0) {
        if (!force && Clock::now() - last_plot < std::chrono::milliseconds(50)) return;
        auto measured = window.frame(config);
        auto transmitted = transmitter ? transmitter->take_payload_constellation() : modem::ConstellationBatch{};
        auto transmitted_history = transmitter ? transmitter->payload_constellation() :
            std::vector<std::complex<double>>{};
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (transmitter && tx_serial != serial) return;
        if (pattern_scores && tx_serial != serial) return;
        current.waveform = std::move(measured.waveform); current.spectrum_db = std::move(measured.spectrum);
        if (pattern_scores) {
            current.pattern_scores = *pattern_scores;
            current.pattern_score_observations = *pattern_observations;
            current.pattern_score_observation_id = observation_id;
        }
        current.constellation = std::move(measured.constellation);
        current.constellation_source = ConstellationSource::input;
        current.constellation_dropped = 0;
        if (transmitter) {
            // Keep actual emitted chips across display intervals. Narrow bands
            // can produce only one new chip in several GUI updates. Until the
            // payload begins, the measured settling waveform remains visible.
            if (!transmitted_history.empty())
                current.constellation = std::move(transmitted_history);
            current.pattern_scores.clear();
            current.pattern_score_observations.clear();
            current.constellation_source = ConstellationSource::transmitted;
            queue_points(std::move(transmitted), ConstellationSource::transmitted);
        }
        current.spectrum_bin_hz = static_cast<double>(config.sample_rate) / plot_size;
        ++current.sequence; last_plot = Clock::now();
    }
    static std::uint64_t replay_target(const Prepared& wave) {
        const auto samples = wave.transmitter->total_samples();
        const auto divisor = wave.replay_count - 1, index = wave.replay.size();
        return (samples / divisor) * index + (samples % divisor * index + divisor - 1) / divisor;
    }
    void collect_replay(Prepared& wave, const modem::Config& config, const detail::SignalWindow& window) {
        auto measured = window.frame(config);
        ReplayFrame frame;
        const auto keep = std::min(replay_wave_samples, measured.waveform.size());
        frame.waveform.assign(measured.waveform.end() - static_cast<std::ptrdiff_t>(keep), measured.waveform.end());
        frame.spectrum.resize(replay_bins);
        for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
            const auto begin = i * 4;
            const auto end = std::min(begin + 4, measured.spectrum.size());
            frame.spectrum[i] = static_cast<float>(*std::max_element(measured.spectrum.begin() + static_cast<std::ptrdiff_t>(begin),
                                            measured.spectrum.begin() + static_cast<std::ptrdiff_t>(end)));
        }
        modem::ConstellationBatch bounded;
        append_points(bounded, {std::move(measured.constellation), 0}, wave.point_limit);
        frame.constellation.reserve(bounded.points.size());
        for (const auto point : bounded.points) frame.constellation.emplace_back(static_cast<float>(point.real()), static_cast<float>(point.imag()));
        frame.pattern_scores.reserve(wave.pattern_scores.size());
        for (const auto point : wave.pattern_scores) frame.pattern_scores.emplace_back(static_cast<float>(point.real()), static_cast<float>(point.imag()));
        frame.pattern_score_observations = wave.pattern_score_observations;
        frame.pattern_score_observation_id = wave.pattern_score_observation_id;
        frame.dropped = bounded.dropped;
        frame.fraction = static_cast<double>(static_cast<long double>(wave.transmitted_samples) /
                                            wave.transmitter->total_samples());
        frame.transmit_trace = wave.transmitter->transmit_trace();
        wave.replay.push_back(std::move(frame));
    }
    void enqueue_audio(std::span<const float> samples, std::uint64_t version) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        const auto bytes = samples.size_bytes();
        const auto queue_limit = settings.dsp_workspace_bytes / 8;
        if (bytes > queue_limit) { current.error = "audio callback exceeds streaming workspace"; return; }
        if (input_bytes + decoding_bytes + bytes > queue_limit) {
            input.clear(); input_bytes = 0; ++receive_revision;
            current.status = "Audio receiver overrun; restarting acquisition";
        }
        if (decoding_bytes + bytes > queue_limit) return;
        input.push_back({std::vector<float>(samples.begin(), samples.end()), receive_revision});
        input_bytes += bytes; current.buffered_samples = input_bytes / sizeof(float);
        current.dsp_buffered_bytes = input_bytes + decoding_bytes + receiver_bytes + audio_bytes + plot_workspace(settings);
        changed.notify_all();
    }
    void discontinuity() {
        std::lock_guard lock(mutex);
        input.clear(); input_bytes = 0; ++receive_revision;
        current.buffered_samples = 0; changed.notify_all();
    }
    Bank make_bank(const Settings& value) { return make_bank(value, Bank{}); }
    Bank make_bank(const Settings& value, Bank bank) {
        bank.limited=false;
        if(!bank.source_quota)bank.source_quota=std::make_shared<transfer::ReceiveStorageQuota>(
            transfer::ReceiveStorageQuota{transfer::source_storage_limit(value.content_limit),0});
        std::vector<std::optional<Crypto>> keys;
        std::vector<Bytes> fingerprints;
        if (value.permits_plaintext()) keys.emplace_back();
        const auto add = [&](const Crypto& key) {
            const auto fingerprint = key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
            if (std::find(fingerprints.begin(), fingerprints.end(), fingerprint) == fingerprints.end()) {
                fingerprints.push_back(fingerprint); keys.emplace_back(key);
            }
        };
        if (value.transfer.key) add(*value.transfer.key);
        for (const auto& key : value.receive_keys) add(key);
        const auto now = current_epoch();
        const auto center = value.transfer.timestamp ? value.transfer.timestamp : static_cast<std::uint64_t>(now);
        for (std::size_t family=0;family<keys.size();++family) {
            const auto& key=keys[family];
            const auto profiles=value.transfer.automatic_receive_profiles?
                tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,key.has_value()):
                std::vector<modem::Config>{value.transfer.modem};
            for(const auto& profile:profiles) {
            auto epochs = key ? drift_candidates(center, value.transfer.search_seconds, true) : std::vector<std::uint64_t>{center};
            if(key && !value.transfer.timestamp && value.simulation) {
                // A listener can start after hardware settling has begun. Its
                // clock-error window still describes clock uncertainty; the
                // older transmit epochs below cover the physical prefix.
                const auto settling=static_cast<unsigned>(std::ceil(
                    (static_cast<double>(modem::training_sample_count(profile))+
                     static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate));
                for(unsigned age=value.transfer.search_seconds+1;age<=value.transfer.search_seconds+settling;++age)
                    if(center>=age)epochs.push_back(center-age);
            }
            for (const auto epoch : epochs) {
                const auto tag = key ? key->mac(Bytes{'D','P','-','R','X','-','B','A','N','K'}) : Bytes{};
                const auto existing = std::find_if(bank.receivers.begin(), bank.receivers.end(), [&](const auto& receiver) {
                    const auto& c=receiver.options.modem;
                    return receiver.key_tag == tag && (tag.empty() || receiver.epoch == epoch) &&
                        c.spreading_factor==profile.spreading_factor && c.integration_seconds==profile.integration_seconds &&
                        c.scramble==profile.scramble && c.spreading_mode==profile.spreading_mode &&
                        c.pulse_shaping==profile.pulse_shaping;
                });
                if (existing != bank.receivers.end()) continue;
                const auto capacity = bank_capacity(value);
                const auto compact=key && modem::symbol_sample_count(profile)>=60ULL*profile.sample_rate;
                const auto control_margin = (compact?8192:65536) + sizeof(Receiver);
                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {
                    bank.limited=true;continue;
                }
                Receiver receiver;
                receiver.sample_origin=bank.samples;receiver.family=family;
                receiver.options = value.transfer; receiver.options.modem=profile;receiver.options.key = key; receiver.epoch = epoch;
                receiver.admitted_at=now;
                receiver.key_tag = tag;
                const auto config = transfer::seeded_config(receiver.options, epoch);
                try {
                    modem::PatternSearch search;

                    search.compact_clock_search=compact;
                    search.search_stream_phases=key.has_value();
                    search.bit_limit=transfer::pattern_bit_limit(value.content_limit);
                    search.start_offset_seconds=static_cast<double>(epoch)-(value.transfer.timestamp?static_cast<double>(value.transfer.timestamp):now);
                    if(value.simulation || value.transfer.timestamp)
                        *search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(config))+
                            static_cast<double>(modem::pattern_pulse_padding_samples(config)))/config.sample_rate;
                    search.start_uncertainty_seconds=value.transfer.search_seconds+1.;
                    receiver.modem = std::make_unique<modem::StreamingReceiver>(config,
                        std::min(value.dsp_workspace_bytes / 2, capacity - bank.working_bytes - control_margin),search);
                } catch(const Error&) {
                    bank.limited=true;continue;
                }
                receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,bank.source_quota);
                bank.working_bytes += receiver_workspace(receiver);
                if (bank.working_bytes > bank_capacity(value))
                    throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                bank.receivers.push_back(std::move(receiver));
            }
            }
        }
        bank.created_at = now;
        if(bank.receivers.empty())throw Error("no receive profile fits the configured DSP workspace");
        return bank;
    }
    void refresh_bank(Bank& bank, const Settings& value) {
        // Receiver epoch admission is independent of transmit preparation.
        // Both hardware and simulation retain only observed acquisition state.
        if(value.transfer.timestamp && std::none_of(bank.receivers.begin(),bank.receivers.end(),[](const auto& receiver){
            return receiver.key_tag.empty() && receiver.modem->clock_windowed();
        }))return;
        const auto now = current_epoch();
        if (std::floor(now)==std::floor(bank.created_at)) return;
        std::erase_if(bank.receivers,[&](const auto& receiver){
            // Short streams can hold fewer than one output chunk throughout
            // the physical absence window. Their admitted clock must survive
            // epoch refresh even before any chunk updates last_confident_end.
            if(receiver.modem->synchronized())return false;
            const auto& config=receiver.options.modem;
            const auto symbol=static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate;
            const auto prefix=(static_cast<double>(modem::training_sample_count(config))+
                static_cast<double>(modem::pattern_pulse_padding_samples(config)))/config.sample_rate;
            const auto allowance=value.transfer.search_seconds+1.;
            // Rejecting a completed search no longer removes the only key for
            // a whole message: subsequent symbol-start seconds are independent.
            // Unconfirmed epochs need one complete symbol plus clock coverage.
            // Retire confirmed receivers only after their physical search has
            // ended the stream using the fixed complete-symbol absence rule.
            if(receiver.last_confident_end) {
                return now>receiver.last_confident_at+6.+allowance;
            }
            if(receiver.key_tag.empty())
                return receiver.modem->clock_windowed() && now>receiver.admitted_at+prefix+symbol+allowance;
            return !value.transfer.timestamp && now>static_cast<double>(receiver.epoch)+prefix+symbol+allowance;
        });
        bank.working_bytes=sizeof(detail::ReceptionHistory);for(const auto& receiver:bank.receivers)bank.working_bytes+=receiver_workspace(receiver);
        bank=make_bank(value,std::move(bank));
    }
    void append_signal(SignalUpdate event) {
        if (current.signals.size() == maximum_events) {
            const auto completed = [](const auto& item) { return item.validated || item.complete; };
            const auto pending = std::find_if(current.signals.begin(), current.signals.end(),
                                             [&](const auto& item) { return !completed(item); });
            if (pending == current.signals.end() && !completed(event)) return;
            current.signals.erase(pending == current.signals.end() ? current.signals.begin() : pending);
        }
        current.signals.push_back(std::move(event));
    }
    std::size_t recovery_bytes() const {
        const auto bytes=[](const RecoveryTask& task) {
            return task.retained_bytes+task.job->working_bytes();
        };
        std::size_t total=0;
        for(const auto& task:recoveries)total+=bytes(*task);
        if(active_recovery && std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end())
            total+=bytes(*active_recovery);
        return total;
    }
    std::vector<SignalUpdate> unavailable_recovery_events() {
        std::vector<SignalUpdate> events;
        for(const auto& task:recoveries)if(!task->obsolete) {
            auto event=task->event;event.recovery_progress=task->job->progress();
            event.recovery_progress.state=transfer::RecoveryState::unavailable;
            event.sequence=next_event++;events.push_back(std::move(event));
        }
        return events;
    }
    std::size_t recovery_reserved_bytes() const {
        std::size_t total=0;
        for(const auto& task:recoveries)total+=task->reserved_bytes;
        if(active_recovery && std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end())
            total+=active_recovery->reserved_bytes;
        return total;
    }
    void invalidate_recoveries(const SignalUpdate* replacement=nullptr) {
        const auto obsolete=[&](const auto& task) {
            return !replacement || (task->event.id==replacement->id && task->event.revision<replacement->revision) ||
                std::find(replacement->superseded_ids.begin(),replacement->superseded_ids.end(),task->event.id)!=replacement->superseded_ids.end();
        };
        for(const auto& task:recoveries)if(obsolete(task)) {
            task->obsolete=true;task->stop.request_stop();
        }
        std::erase_if(recoveries,[&](const auto& task){return task->obsolete;});
        changed.notify_all();
    }
    void queue_recovery(transfer::Received result,SignalUpdate& event) {
        if(!result.recovery || !result.stream_complete || result.content_validated)return;
        event.recovery_progress=result.recovery->progress();
        // Plot samples remain in the live diagnostic presentation. Background
        // recovery retains only the hard-bit capture and scalar diagnostics.
        result.diagnostics.waveform=std::vector<float>{};
        result.diagnostics.constellation=std::vector<std::complex<double>>{};
        const auto retained=sizeof(RecoveryTask)+4096+result.raw_bits.capacity()+result.error.capacity()+
            result.content.message.data.capacity()+result.content.message.filename.capacity()+
            result.content.message.callsign.capacity()+result.content.message.grid.capacity()+
            event.text.capacity()+event.raw_bits.capacity()+event.reception_id.capacity()+
            event.superseded_ids.capacity()*sizeof(std::uint64_t);
        const auto memory=result.recovery->workspace_bound()+retained;
        const auto used=recovery_reserved_bytes(),limit=recovery_workspace_limit;
        if(recoveries.size()+(active_recovery && std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end())>=maximum_recovery_jobs ||
           memory>limit || used>limit-memory) {
            event.recovery_progress.state=transfer::RecoveryState::unavailable;
            return;
        }
        auto task=std::make_shared<RecoveryTask>();task->job=result.recovery;
        task->reserved_bytes=memory;task->retained_bytes=retained;
        task->result=std::move(result);task->event=event;task->generation=generation;
        recoveries.push_back(std::move(task));changed.notify_all();
    }
    void publish_recovery_progress() {
        // Only thread-safe counters are read on the UI path; no search or
        // source decoding runs here, even when an audio stream is idle.
        for(const auto& task:recoveries) {
            if(task->obsolete || task!=active_recovery || task->generation!=generation)continue;
            auto progress=task->job->progress();
            // The codeword search may finish before bounded source validation.
            // Keep that CPU work pending until the coordinator publishes it.
            if(progress.state==transfer::RecoveryState::recovered)progress.state=transfer::RecoveryState::running;
            const auto& previous=task->event.recovery_progress;
            if(progress.state==previous.state && progress.attempts==previous.attempts &&
               progress.elapsed.count()/1000==previous.elapsed.count()/1000)continue;
            task->event.recovery_progress=progress;
            auto event=task->event;event.sequence=next_event++;
            append_signal(std::move(event));
        }
    }
    void recovery_loop(std::stop_token stop) {
        while(!stop.stop_requested()) {
            std::shared_ptr<RecoveryTask> task;
            transfer::Received result;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,stop,[&]{return current.running && std::any_of(recoveries.begin(),recoveries.end(),
                    [](const auto& item){return !item->started && !item->obsolete;});});
                if(stop.stop_requested())return;
                const auto next=std::find_if(recoveries.begin(),recoveries.end(),[](const auto& item){return !item->started && !item->obsolete;});
                if(next==recoveries.end())continue;
                task=*next;task->started=true;active_recovery=task;
                result=std::move(task->result);
            }
            try { result=transfer::recover_received(std::move(result),task->stop.get_token()); }
            catch(const std::exception& error) {
                result.error=error.what();result.recovery_progress=task->job->progress();
                result.recovery_progress.state=transfer::RecoveryState::unavailable;
            }
            std::lock_guard lock(mutex);
            active_recovery.reset();
            task->result=std::move(result);
            if(task->obsolete || !current.running || task->generation!=generation)continue;
            auto& recovered=task->result;
            auto event=task->event;event.recovery_progress=recovered.recovery_progress;
            if(recovered.content_validated) {
                recovered.content.message.local_id=content_identity(event.id);
                event.text=display_text(recovered.content.message);event.binary=false;event.validated=true;
                event.reception_id=reception_id(recovered.content.message);
                event.pre_fec_accuracy=recovered.content.pre_fec_accuracy;event.fec_stats=recovered.content.fec_stats;
                try {
                    admit_received(recovered.content.message.data.size(),settings.content_limit);
                    current.received.push_back(std::move(recovered));
                } catch(const Error& error) {
                    current.error=error.what();event=task->event;
                    event.recovery_progress.state=transfer::RecoveryState::unavailable;
                }
            }
            task->event=event;event.sequence=next_event++;append_signal(std::move(event));
            const auto state=task->event.recovery_progress.state;
            if(state!=transfer::RecoveryState::incomplete && state!=transfer::RecoveryState::cancelled)
                std::erase(recoveries,task);
            ++current.sequence;
        }
    }
    std::array<std::uint8_t,16> content_identity(std::uint64_t signal_id) const {
        auto id=reception_namespace;
        for(std::size_t i=0;i<8;++i)id[8+i]^=static_cast<std::uint8_t>(signal_id>>(8*i));
        return id;
    }
    void discard_obsolete(const SignalUpdate& replacement,Prepared* wave) {
        invalidate_recoveries(&replacement);
        const auto merged=[&](std::uint64_t id) {
            return std::find(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),id)!=
                replacement.superseded_ids.end();
        };
        const auto stale=[&](const SignalUpdate& event) {
            return merged(event.id) || (event.id==replacement.id && event.revision<replacement.revision);
        };
        const auto old_content=[&](const transfer::Received& result) {
            const auto& id=result.content.message.local_id;
            return id==content_identity(replacement.id) ||
                std::any_of(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),
                    [&](auto signal_id){return id==content_identity(signal_id);});
        };
        std::erase_if(current.signals,stale);
        std::erase_if(current.received,[&](const auto& result) {
            if(!old_content(result))return false;
            received_bytes-=result.content.message.data.size();return true;
        });
        // A slower profile may produce stronger evidence while an earlier
        // simulated interpretation is still queued for presentation.
        for(auto& event:replay_signals)if(event && stale(*event))event.reset();
        if(replay_verified && stale(*replay_verified))replay_verified.reset();
        if(replay_received && old_content(*replay_received)) {
            replay_received.reset();staged_received_bytes=0;
        }
        if(wave) {
            if(wave->verified && stale(*wave->verified))wave->verified.reset();
            if(wave->received && old_content(*wave->received)) {
                wave->received.reset();staged_received_bytes=0;
            }
        }
    }
    void add_signal(SignalUpdate event, Prepared* wave = nullptr) {
        event.sequence = next_event++; event.virtual_seconds = current.virtual_seconds;
        if (!wave) { append_signal(std::move(event)); return; }
        event.text = std::string(event.text.data(), std::min(event.text.size(), replay_text_limit));
        if (event.validated || event.complete) { wave->verified = std::move(event); return; }
        // One most recent pending observation per presentation interval.
        // Floor quantization keeps even a late observation visible for the last
        // interval before validation; a stalled UI never extends the deadline.
        const auto fraction = static_cast<long double>(wave->transmitted_samples) / wave->transmitter->total_samples();
        // Frame zero represents input before this transmission began; even an
        // early confident prefix belongs to a later presentation interval.
        const auto index = std::min(wave->signals.size() - 1, std::max<std::size_t>(1,
            wave->binary ? wave->replay.size() :
                static_cast<std::size_t>(fraction * static_cast<long double>(wave->signals.size()))));
        wave->signals[index] = std::move(event);
    }
    void make_receive_room(std::size_t bytes, std::size_t limit) {
        if (staged_received_bytes > limit || bytes > limit - staged_received_bytes)
            throw Error("incoming content exceeds remaining receive cache capacity");
        while (!current.received.empty() &&
               (current.received.size() >= maximum_events || received_bytes > limit - staged_received_bytes - bytes)) {
            received_bytes -= current.received.front().content.message.data.size();
            current.received.erase(current.received.begin());
        }
    }
    void admit_received(std::size_t bytes, std::size_t limit) {
        make_receive_room(bytes, limit);
        received_bytes += bytes;
    }
    static detail::ReceptionHistory::Candidate reception_candidate(
        const Receiver& receiver,const modem::PatternBurst& burst,const Receiver::Presentation& display) {
        detail::ReceptionHistory::Candidate candidate;
        candidate.id=display.candidate_id;candidate.family=receiver.family;
        candidate.signal_id=display.signal_id;
        candidate.first=receiver.sample_origin+burst.stream_first_sample;
        candidate.end=receiver.sample_origin+burst.end_sample;
        candidate.frequency=burst.frequency_hz;
        candidate.frequency_tolerance=static_cast<double>(receiver.options.modem.sample_rate)/
            static_cast<double>(modem::symbol_sample_count(receiver.options.modem));
        candidate.absence_samples=modem::pattern_absence_samples(receiver.options.modem);
        candidate.symbol_samples=modem::symbol_sample_count(receiver.options.modem);
        candidate.timing_tolerance=modem::pattern_chip_samples(receiver.options.modem);
        candidate.score=burst.support_samples;
        candidate.complete=burst.complete;
        return candidate;
    }
    template<class Feed> void feed_bank(Bank& bank, const Settings& value, std::uint64_t version,
                                        std::stop_token stop, std::size_t sample_count, Feed feed,
                                        Prepared* simulation_wave = nullptr) {
        // Admit the receiver's current clock epoch before consuming this PCM
        // block. A short burst can finish within one block after a second rolls
        // over; refreshing afterward can miss every chip of the new stream.
        refresh_bank(bank,value);
        if(sample_count>std::numeric_limits<std::uint64_t>::max()-bank.samples)
            throw Error("receive sample clock exceeds its platform range");
        const auto capacity = bank_capacity(value);
        std::vector<std::complex<double>> pattern_scores;
        std::vector<PatternScoreObservation> pattern_observations;
        // Observe every profile before publishing any completion. Otherwise a
        // weaker profile processed first could close the shared pending row.
        for(unsigned phase=0;phase<2;++phase) {
            for (auto& receiver : bank.receivers) {
                if (stop.stop_requested()) return;
                auto accounted = receiver_workspace(receiver);
                const auto update_workspace = [&] {
                    const auto actual = receiver_workspace(receiver);
                    bank.working_bytes = bank.working_bytes - accounted + actual;
                    accounted = actual;
                };
                try {
                    if(phase==0) {
                        const auto overhead = accounted - receiver.modem->working_bytes();
                        const auto other = bank.working_bytes - accounted;
                        if (other > capacity || overhead > capacity - other)
                            throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                        // Idle keys reserve their actual state. The receiver being
                        // fed can use all remaining shared space for recording and
                        // replay, while later keys see its measured growth.
                        receiver.modem->set_workspace_bytes(capacity - other - overhead);
                        feed(*receiver.modem);
                        update_workspace();
                        {
                            const auto candidates = receiver.modem->pattern_candidates(pattern_score_limit);
                            receiver.pattern_score_history.update(candidates, replay_clock(), [&] {
                                return pattern_score_observation_id.fetch_add(1, std::memory_order_relaxed) + 1;
                            });
                        }
                        receiver.bursts=receiver.modem->take_pattern_bursts();
                        update_workspace();
                        if(bank.working_bytes>capacity)throw Error("receive event storage exceeds the configured DSP workspace");
                        continue;
                    }
                    {
                        for(auto& burst:receiver.bursts) {
                            if(burst.bits.empty() && !burst.missing_slots && !burst.complete)continue;
                            const auto score=burst.score,frequency=burst.frequency_hz;
                            const auto physical_id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
                            if(burst.end_sample>receiver.last_confident_end) {
                                receiver.last_confident_end=burst.end_sample;receiver.last_confident_at=current_epoch();
                            }
                            auto presentation=receiver.presentations.find(physical_id);
                            if(presentation==receiver.presentations.end())continue;
                            auto& display=presentation->second;
                            const auto candidate=reception_candidate(receiver,burst,display);
                            auto result=receiver.content->push(std::move(burst),receiver.modem->diagnostics());
                            update_workspace();
                            if(bank.working_bytes>capacity)throw Error("receive content diagnostics exceed the configured DSP workspace");
                            std::string bits;bits.reserve(result.raw_bits.size());
                            for(auto bit:result.raw_bits)bits.push_back(bit?'1':'0');
                            std::lock_guard lock(mutex);
                            if(!current.running || generation!=version || stop.stop_requested())return;
                            const auto complete=result.stream_complete;
                            const auto decision=bank.receptions.observe(candidate,[&]{return next_signal++;});
                            bank.limited|=decision.limited;
                            display.signal_id=decision.signal_id;
                            if(decision.selected) {
                                SignalUpdate event;event.id=decision.signal_id;event.frequency_hz=frequency;
                                event.revision=decision.revision;
                                event.superseded_ids.assign(decision.superseded_ids.begin(),decision.superseded_ids.end());
                                result.content.message.local_id=content_identity(event.id);
                                event.text=std::move(bits);event.binary=true;event.complete=result.stream_complete;
                                event.recovery_progress=result.recovery_progress;
                                if(simulation_wave && result.recovery_progress.state!=transfer::RecoveryState::none)
                                    simulation_wave->recovery_clear_generation=recovery_clear_generation;
                                event.received_bits=result.observed_bits;event.pattern_score=score;event.missing_symbols=result.missing_symbols;
                                if(result.content_validated) {
                                    event.text=display_text(result.content.message);event.binary=false;event.validated=true;
                                    event.reception_id=reception_id(result.content.message);event.pre_fec_accuracy=result.content.pre_fec_accuracy;event.fec_stats=result.content.fec_stats;
                                }
                                if(result.short_text_decoded) {
                                    event.raw_bits=std::move(event.text);
                                    event.text.assign(result.content.message.data.begin(),result.content.message.data.end());
                                    event.binary=false;
                                    event.reception_id=reception_id(result.content.message);
                                }
                                if(display.published_revision!=decision.revision) {
                                    discard_obsolete(event,simulation_wave);
                                    display.published_revision=decision.revision;
                                }
                                const bool recovery=result.recovery && result.stream_complete && !result.content_validated;
                                if(recovery) {
                                    event.recovery_progress=result.recovery_progress;
                                    if(simulation_wave) {
                                        simulation_wave->received=std::move(result);staged_received_bytes=0;
                                    } else queue_recovery(std::move(result),event);
                                }
                                add_signal(std::move(event),simulation_wave);
                                if(!recovery && (result.content_validated || result.short_text_decoded) && !display.content_reported) {
                                    const auto bytes=result.content.message.data.size();
                                    if(simulation_wave) {
                                        simulation_wave->received.reset();staged_received_bytes=0;
                                        make_receive_room(bytes,value.content_limit);staged_received_bytes=bytes;
                                        simulation_wave->received=std::move(result);
                                    } else {admit_received(bytes,value.content_limit);current.received.push_back(std::move(result));}
                                    display.content_reported=true;
                                }
                            }
                            if(complete)receiver.presentations.erase(presentation);
                            update_workspace();
                            if(bank.working_bytes>capacity)throw Error("receive presentation state exceeds the configured DSP workspace");
                        }
                        receiver.bursts.clear();update_workspace();
                        continue;
                    }
                } catch (const Error& error) {
                    update_workspace();
                    if (stop.stop_requested()) return;
                    {
                        {
                            std::lock_guard lock(mutex);
                            if(current.running && generation==version)current.error=error.what();
                        }
                        bank.limited=true;
                        modem::PatternSearch search;search.bit_limit=transfer::pattern_bit_limit(value.content_limit);

                        search.compact_clock_search=receiver.options.key &&
                            modem::symbol_sample_count(receiver.options.modem)>=60ULL*receiver.options.modem.sample_rate;
                        search.search_stream_phases=receiver.options.key.has_value();
                        search.start_offset_seconds=static_cast<double>(receiver.epoch)-current_epoch();
                        if(value.simulation || value.transfer.timestamp)
                            *search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(receiver.options.modem))+
                                static_cast<double>(modem::pattern_pulse_padding_samples(receiver.options.modem)))/receiver.options.modem.sample_rate;
                        search.start_uncertainty_seconds=value.transfer.search_seconds+1.;
                        const auto other=bank.working_bytes-accounted,overhead=accounted-receiver.modem->working_bytes();
                        if(other>capacity || overhead>capacity-other)throw;
                        const auto remaining=capacity-other-overhead;
                        receiver.modem.reset();
                        receiver.modem=std::make_unique<modem::StreamingReceiver>(transfer::seeded_config(receiver.options,receiver.epoch),remaining,search);
                        receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,bank.source_quota);
                        receiver.admitted_at=current_epoch();
                        receiver.sample_origin=bank.samples+sample_count;
                        receiver.last_confident_at=0;receiver.last_confident_end=0;
                        receiver.pattern_score_history = {};
                        receiver.presentations.clear();
                        receiver.bursts.clear();
                    }
                    update_workspace();
                }
            }
            if(phase==0) {
                std::lock_guard lock(mutex);
                if(!current.running || generation!=version || stop.stop_requested())return;
                for(auto& receiver:bank.receivers) {
                    const auto before=receiver_workspace(receiver);
                    for(const auto& burst:receiver.bursts) {
                        if(burst.bits.empty() && !burst.missing_slots && !burst.complete)continue;
                        const auto physical_id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
                        auto presentation=receiver.presentations.find(physical_id);
                        if(presentation==receiver.presentations.end()) {
                            if(receiver.presentations.size()>=16){bank.limited=true;continue;}
                            presentation=receiver.presentations.emplace(physical_id,
                                Receiver::Presentation{bank.next_candidate++}).first;
                        }
                        auto candidate=reception_candidate(receiver,burst,presentation->second);
                        candidate.complete=false;
                        const auto decision=bank.receptions.observe(candidate,[&]{return next_signal++;});
                        bank.limited|=decision.limited;
                        presentation->second.signal_id=decision.signal_id;
                    }
                    bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver);
                }
            }
        }
        bank.samples+=sample_count;
        // Receiver histories are updated even while another receiver wins the
        // plot. Retained old evidence must not gain a new age or mask a fresh,
        // weaker receiver when the selected receiver changes.
        const auto now = replay_clock();
        const detail::PatternScoreHistory* selected = nullptr;
        double best_pattern_score = -1;
        for (const auto& receiver : bank.receivers) {
            const auto score = receiver.pattern_score_history.best_score(now);
            if (score > best_pattern_score) {
                best_pattern_score = score; selected = &receiver.pattern_score_history;
            }
        }
        if (selected) {
            pattern_scores.reserve(selected->entries().size());
            pattern_observations.reserve(selected->entries().size());
            for (const auto& entry : selected->entries()) {
                if (detail::PatternScoreHistory::expired(entry.observation, now)) continue;
                const auto& candidate = entry.candidate;
                pattern_scores.emplace_back(candidate.bit ? candidate.alternative_score : candidate.score,
                                            candidate.bit ? candidate.score : candidate.alternative_score);
                pattern_observations.push_back(entry.observation);
            }
        }
        const auto observation_id = pattern_score_observation_id.load(std::memory_order_relaxed);
        std::lock_guard lock(mutex);
        if (current.running && generation == version && !stop.stop_requested()) {
            if (simulation_wave) {
                simulation_wave->pattern_scores = std::move(pattern_scores);
                simulation_wave->pattern_score_observations = std::move(pattern_observations);
                simulation_wave->pattern_score_observation_id = observation_id;
            } else if (value.simulation || !tx_busy) {
                if (current.pattern_scores != pattern_scores || current.pattern_score_observations != pattern_observations) {
                    current.pattern_scores = std::move(pattern_scores);
                    current.pattern_score_observations = std::move(pattern_observations); ++current.sequence;
                }
                current.pattern_score_observation_id = observation_id;
            }
            receiver_bytes = bank.working_bytes;
            if(bank.limited)current.status="Pattern search is limited by the configured DSP workspace";
            else if(current.status=="Pattern search is limited by the configured DSP workspace")current.status=idle_status();
            current.dsp_buffered_bytes = receiver_bytes + input_bytes + decoding_bytes + audio_bytes + plot_workspace(value) + (tx_busy ? value.dsp_workspace_bytes / 4 : 0);
        }
    }
    void feed_samples(Bank& bank,std::span<const float> samples,const Settings& value,std::uint64_t version,
                      std::stop_token stop,Prepared* wave=nullptr) {
        std::array<std::complex<double>,plot_size> projected{};
        const auto rotation=std::polar(1.,-2*std::numbers::pi*value.transfer.modem.carrier_hz/value.transfer.modem.sample_rate);
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min(projected.size(),samples.size()-offset);
            for(std::size_t i=0;i<count;++i) {
                projected[i]=static_cast<double>(samples[offset+i])*bank.mixer;bank.mixer*=rotation;
            }
            bank.mixer/=std::abs(bank.mixer);
            const auto raw=samples.subspan(offset,count);const auto mixed=std::span(projected).first(count);
            feed_bank(bank,value,version,stop,count,[&](auto& receiver){return receiver.push(raw,mixed,stop);},wave);
            offset+=count;
        }
    }
    void complete_tx(Prepared& wave, const std::string& error = {}) {
        std::lock_guard lock(mutex);
        if (wave.generation != generation || wave.serial != tx_serial) return;
        if (wave.transmitter) current.transmit_trace = wave.transmitter->transmit_trace();
        if(!settings.simulation && !wave.noise)next_hardware_send=Clock::now()+std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(static_cast<double>(modem::pattern_absence_samples(wave.modem))/wave.modem.sample_rate+1.));
        tx_busy = false; current.transmitting = settings.simulation ? false : !queued.empty();
        current.transmitting_noise = false;
        current.transmission_finished = settings.simulation || queued.empty(); current.transmission_cancelled = wave.stop.stop_requested();
        current.status = idle_status();
        if (!error.empty()) { current.error = error; staged_received_bytes = 0; }
        else if (!wave.stop.stop_requested()) {
            current.transmission_fraction = 1;
            if (settings.simulation && !wave.replay.empty()) {
                if(wave.recovery_clear_generation!=recovery_clear_generation)
                    discard_staged_recovery(wave.received,wave.verified,wave.signals);
                replay = std::move(wave.replay); delivered_replay_frame.reset(); first_visible_replay_frame = 0;
                replay_signals = std::move(wave.signals); replay_signal_cursor = 0;
                replay_verified = std::move(wave.verified); replay_received = std::move(wave.received);
                if (replay_verified) replay_signal_id = replay_verified->id;
                else for (const auto& event : replay_signals) if (event) { replay_signal_id = event->id; break; }
                replay_bin_hz = static_cast<double>(wave.modem.sample_rate) / (plot_size / 4);
                replay_started = replay_clock(); current.simulation_replay = true;
                detail::rebase_pattern_score_observations(replay, replay_started, replay_duration);
                current.transmit_trace = replay.front().transmit_trace;
                current.replay_frame_count = replay.size();
                current.status = wave.binary ? "Simulation; three seconds of raw binary signal" :
                    "Simulation; three seconds of sampled signal and reception";
                ++current.sequence;
            }
        }
        changed.notify_all();
    }
    void progress(const Prepared& wave, const Settings& value) {
        std::lock_guard lock(mutex);
        if (generation != wave.generation || tx_serial != wave.serial) return;
        current.transmit_trace = wave.transmitter->transmit_trace();
        current.transmission_seconds = static_cast<double>(value.simulation ? wave.transmitted_samples : wave.transmitter->samples_emitted()) / wave.modem.sample_rate;
        current.transmission_fraction = wave.noise ? 0 : static_cast<double>(value.simulation ? wave.transmitted_samples : wave.transmitter->samples_emitted()) /
                                        static_cast<double>(wave.transmitter->total_samples());
    }
    void retain_emitted_epoch(const Prepared& wave) {
        if(!wave.protected_epoch)return;
        const auto& config=wave.modem;
        const auto prefix=modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config);
        const auto end=wave.transmitter->total_samples()-modem::pattern_pulse_padding_samples(config)-modem::suppression_sample_count(config);
        const auto emitted=std::min(end,wave.transmitter->samples_emitted());
        if(emitted<=prefix)return;
        const auto symbol=(emitted-prefix-1)/modem::symbol_sample_count(config);
        const auto address=modem::symbol_stream_address(*wave.protected_epoch,0,symbol,
            modem::symbol_sample_count(config),config.sample_rate);
        std::lock_guard lock(mutex);
        last_pattern_transmit_epoch=std::max(last_pattern_transmit_epoch,address.epoch);
    }
    void source_loop(std::stop_token stop) {
        std::uint64_t local_generation = 0;
        std::optional<Bank> simulation_bank;
        std::unique_ptr<modem::SampledSimulationChannel> simulation_channel;
        std::shared_ptr<Prepared> wave;
        detail::SignalWindow plot_window;
        unsigned idle_fraction = 0;
        auto last_plot = Clock::time_point{};
        while (!stop.stop_requested()) {
            Settings value; std::uint64_t version; std::stop_token capture_token, processing_token;
            bool new_burst = false;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && decoder_generation == generation; });
                if (stop.stop_requested()) break;
                advance_replay(replay_clock());
                value = settings; version = generation; processing_token = decode_stop.get_token();
                if (local_generation != version) {
                    local_generation = version; wave.reset(); simulation_bank.reset(); simulation_channel.reset();
                    last_plot = {};
                    plot_window = detail::SignalWindow(value.transfer.modem);
                    idle_fraction = 0;
                }
                if (wave && wave->stop.stop_requested()) {
                    wave.reset();
                }
                if (!wave && ready) {
                    wave = std::move(ready); new_burst = true; pending_points = {};
                    if (!value.simulation) plot_window.reset();
                    current.status = wave->noise ? (value.simulation ? "Simulating continuous noise; stop when finished" :
                        "Transmitting continuous noise; stop when finished") :
                        value.simulation ? "Transmitting sampled audio to an independent receiver" : "Transmitting; audio input paused";
                }
                capture_stop = std::stop_source{}; capture_token = capture_stop.get_token();
            }
            try {
                const auto& transmit_modem = wave ? wave->modem : value.transfer.modem;
                if (value.simulation) {
                    // The receiving computer runs continuously. TX control can
                    // start a waveform, but cannot reset, seed or align RX.
                    if (!simulation_bank) simulation_bank = make_bank(value);
                    if (!simulation_channel) simulation_channel = std::make_unique<modem::SampledSimulationChannel>(
                        value.transfer.modem, channel_config(value));
                    if (new_burst) simulation_channel->begin_burst();
                    std::array<float, plot_size> samples{};
                    if (wave && wave->noise) {
                        // An open-ended tuning source has no completion replay.
                        // Pace its physical samples while the independent RX
                        // retains its ordinary keys and admission rules.
                        const auto block_start = Clock::now();
                        const auto capacity = std::min<std::size_t>(samples.size(),
                            std::max<std::size_t>(1, value.transfer.modem.sample_rate / 20));
                        const auto count = simulation_channel->read(*wave->transmitter,
                            std::span(samples).first(capacity), wave->stop);
                        wave->transmitted_samples = simulation_channel->transmitted_samples();
                        if (!count) { complete_tx(*wave); wave.reset(); continue; }
                        const auto input_samples = std::span(samples).first(count);
                        account(count, version); progress(*wave, value);
                        plot_window.push(input_samples);
                        feed_samples(*simulation_bank,input_samples,value,version,wave->stop);
                        publish(plot_window,transmit_modem,version,last_plot,false,
                            wave->transmitter.get(),wave->serial);
                        const auto deadline = block_start + std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(static_cast<double>(count) / value.transfer.modem.sample_rate));
                        std::unique_lock lock(mutex);
                        changed.wait_until(lock,stop,deadline,[&] {
                            return !current.running || generation != version || wave->stop.stop_requested();
                        });
                        continue;
                    }
                    if (wave) {
                        // First replay frame records the independently running
                        // receiver before any newly transmitted samples arrive.
                        if (wave->replay.empty()) {
                            wave->pattern_score_observation_id = pattern_score_observation_id.load(std::memory_order_relaxed);
                            collect_replay(*wave, transmit_modem, plot_window);
                        }
                        std::size_t count = 0;
                        if (!wave->tail_started) {
                            const auto target = wave->replay.size() < wave->replay_count ? replay_target(*wave) :
                                wave->transmitter->total_samples();
                            const auto remaining = target > wave->transmitted_samples ? target - wave->transmitted_samples : 1;
                            const auto capacity = static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(), remaining));
                            count = simulation_channel->read(*wave->transmitter, std::span(samples).first(capacity), wave->stop);
                            wave->transmitted_samples = simulation_channel->transmitted_samples();
                            if (!count) {
                                wave->tail_started = true;
                                wave->tail_remaining = modem::pattern_absence_samples(transmit_modem)+transmit_modem.sample_rate+2*modem::pattern_pulse_padding_samples(transmit_modem);
                            }
                        }
                        if (wave->tail_started && wave->tail_remaining) {
                            count = static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(), wave->tail_remaining));
                            simulation_channel->read_noise(std::span(samples).first(count));
                            wave->tail_remaining -= count;
                        }
                        const auto input_samples = std::span(samples).first(count);
                        account(count, version); progress(*wave, value);
                        plot_window.push(input_samples);
                        feed_samples(*simulation_bank,input_samples,value,version,wave->stop,wave.get());
                        publish(plot_window, transmit_modem, version, last_plot, false,
                                nullptr, wave->serial, &wave->pattern_scores, &wave->pattern_score_observations,
                                wave->pattern_score_observation_id);
                        if (!wave->tail_started && wave->replay.size() < wave->replay_count &&
                            wave->replay.size()+1 < wave->replay_count &&
                            wave->transmitted_samples >= replay_target(*wave))
                            collect_replay(*wave, transmit_modem, plot_window);
                        if (wave->tail_started && !wave->tail_remaining) {
                            // Short pattern bursts can be scored only after
                            // trailing samples complete a receiver window.
                            // Reserve their final replay frame for that evidence.
                            if (wave->replay.size() < wave->replay_count)
                                collect_replay(*wave, transmit_modem, plot_window);
                            complete_tx(*wave); wave.reset();
                        }
                        std::this_thread::yield();
                    } else {
                        // Idle media continues during stream preparation and
                        // replay. Noise and burst audio use the same RX clock.
                        idle_fraction += value.transfer.modem.sample_rate % 20;
                        const auto count = std::min<std::size_t>(plot_size,
                            value.transfer.modem.sample_rate / 20 + idle_fraction / 20);
                        idle_fraction %= 20;
                        auto idle = std::span(samples).first(count);
                        simulation_channel->read_noise(idle);
                        account(idle.size(), version);
                        plot_window.push(idle);
                        publish(plot_window, value.transfer.modem, version, last_plot);
                        feed_samples(*simulation_bank,idle,value,version,processing_token);
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock, stop, std::chrono::milliseconds(50), [this, version] {
                            return !current.running || generation != version || ready;
                        });
                    }
                    continue;
                }
                if (wave) {
                    bool mono;
                    { std::lock_guard lock(mutex); mono = settings.mono; }
                    discontinuity();
                    audio::playback(transmit_modem.sample_rate, value.device, [&](std::span<float> output) {
                        const auto before=wave->transmitter->samples_emitted();
                        const auto count = wave->transmitter->read(output, wave->stop);
                        retain_emitted_epoch(*wave);
                        account(count, version);
                        plot_window.push(output.first(count));
                        const auto payload_start=modem::training_sample_count(transmit_modem)+
                            modem::pattern_pulse_padding_samples(transmit_modem);
                        const auto first_payload=before<=payload_start && wave->transmitter->samples_emitted()>payload_start;
                        publish(plot_window, transmit_modem, version, last_plot,
                                first_payload || wave->transmitter->finished(), wave->transmitter.get(), wave->serial);
                        // Publish the first actual payload chips before progress
                        // can describe a post-settling constellation to the UI.
                        progress(*wave, value);
                        return count;
                    }, wave->stop, [&](const auto& format) {
                        audio_format(format, version);
                        if(wave->prepare_hardware) {
                            wave->prepare_hardware(*wave);
                            wave->prepare_hardware={};
                        }
                    }, mono);
                    discontinuity(); complete_tx(*wave); wave.reset(); plot_window.reset(); continue;
                }
                {
                    std::lock_guard lock(mutex);
                    if (generation == version) { current.status = idle_status(); current.error.clear(); }
                }
                plot_window.reset();
                audio::capture(value.transfer.modem.sample_rate, value.device, [&](std::span<const float> chunk) {
                    account(chunk.size(), version); plot_window.push(chunk);
                    publish(plot_window, value.transfer.modem, version, last_plot);
                    enqueue_audio(chunk, version);
                    std::lock_guard lock(mutex);
                    return current.running && generation == version && !ready;
                }, capture_token, [&](const auto& format) { audio_format(format, version); });
            } catch (const std::exception& exception) {
                const auto cancelled_transmission = wave && wave->stop.stop_requested();
                if (!cancelled_transmission) simulation_bank.reset();
                if (wave) { complete_tx(*wave, wave->stop.stop_requested() ? "" : exception.what()); wave.reset(); }
                std::unique_lock lock(mutex);
                if (current.running && generation == version && !capture_token.stop_requested() && !processing_token.stop_requested() && !cancelled_transmission) {
                    current.error = exception.what(); current.status = value.simulation ? "Simulation paused after channel error" : "Audio input unavailable; retrying";
                    changed.wait_for(lock, stop, std::chrono::seconds(2), [this, version] {
                        return !current.running || generation != version || ready;
                    });
                }
            }
        }
    }
    void encode_loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Transmission transmission; Settings value; std::uint64_t version, serial; std::stop_token token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && !tx_busy && replay.empty() && !queued.empty(); });
                if (stop.stop_requested()) break;
                transmission = std::move(queued.front()); queued.pop_front(); value = settings;
                version = generation; serial = ++tx_serial; tx_stop = std::stop_source{}; token = tx_stop.get_token();
                current.transmission_id = serial;
                current.transmit_trace = {};
                clear_replay(); pending_points = {};
                current.constellation.clear(); current.constellation_source = ConstellationSource::input;
                current.constellation_dropped = 0;
                tx_busy = true; current.transmitting = true; current.transmission_finished = false;
                current.transmitting_noise = std::holds_alternative<modem::Noise>(transmission);
                current.transmission_fraction = current.transmission_seconds = 0;
                current.status = current.transmitting_noise ? "Preparing noise with temporary keys" :
                    std::holds_alternative<Bytes>(transmission) ? "Preparing raw binary signal" :
                    "Preparing fixed-interval stream";
            }
            try {
                if (const auto* message = std::get_if<Message>(&transmission);
                    message && value.long_message_modem && !transfer::uses_raw_message(*message))
                    value.transfer.modem = *value.long_message_modem;
                if (std::holds_alternative<modem::Noise>(transmission)) {
                    // Ephemeral streams need no saved-key epoch scheduling,
                    // source encoding, receiver-bank key or message replay.
                    auto prepared = std::make_shared<Prepared>();
                    prepared->modem = value.transfer.modem;
                    prepared->generation = version; prepared->serial = serial;
                    prepared->stop = token; prepared->noise = true;
                    prepared->transmitter = std::make_unique<modem::StreamingTransmitter>(
                        modem::Noise{}, value.transfer.modem, value.dsp_workspace_bytes / 4);
                    std::lock_guard lock(mutex);
                    if (!current.running || generation != version || tx_serial != serial || token.stop_requested()) continue;
                    ready = std::move(prepared);
                    if (!value.simulation) capture_stop.request_stop();
                    changed.notify_all();
                    continue;
                }
                // Only the transmitter reads its send epoch here. The
                // running receiver admits its own candidates independently.
                if(!value.simulation) {
                    // Both explicit epochs and automatically scheduled sends
                    // leave an observed absence interval after the last send.
                    std::unique_lock lock(mutex);
                    changed.wait_until(lock,token,next_hardware_send,[&]{return token.stop_requested();});
                    if(token.stop_requested() || stop.stop_requested())continue;
                }
                const bool scheduled_hardware=!value.simulation && !value.transfer.timestamp;
                if (!value.transfer.timestamp && !scheduled_hardware) {
                    value.transfer.timestamp=static_cast<std::uint64_t>(current_epoch());
                    if(value.transfer.key) {
                        // No transmitted nonce: wait for a fresh local time
                        // coordinate instead of repeating this device's CTR
                        // positions in two bursts in the same whole second.
                        std::uint64_t previous;
                        {std::lock_guard lock(mutex);previous=last_pattern_transmit_epoch;}
                        while(value.transfer.timestamp<=previous && !token.stop_requested() && !stop.stop_requested()) {
                            std::unique_lock lock(mutex);
                            changed.wait_for(lock,stop,std::chrono::milliseconds(50),[&]{return token.stop_requested();});
                            value.transfer.timestamp=static_cast<std::uint64_t>(current_epoch());
                        }
                        if(token.stop_requested() || stop.stop_requested())continue;
                        {std::lock_guard lock(mutex);last_pattern_transmit_epoch=std::max(last_pattern_transmit_epoch,value.transfer.timestamp);}
                    }
                }
                auto prepared = std::make_shared<Prepared>();
                prepared->modem = value.transfer.modem;
                prepared->generation = version; prepared->serial = serial; prepared->stop = token;
                prepared->binary = std::holds_alternative<Bytes>(transmission);
                if (scheduled_hardware) {
                    prepared->prepare_hardware=[this,transmission=std::move(transmission),options=value.transfer](Prepared& wave) mutable {
                        options.modem.stream_phase_samples=0;
                        std::uint64_t minimum=0;
                        if(options.key) {
                            std::lock_guard lock(mutex);
                            if(last_pattern_transmit_epoch==std::numeric_limits<std::uint64_t>::max())
                                throw Error("transmit epoch exhausted");
                            minimum=last_pattern_transmit_epoch+1;
                        }
                        auto scheduled=datapump::detail::schedule_transmission(options.modem,[&](std::uint64_t epoch) {
                            options.timestamp=epoch;
                            return wave.binary?transfer::binary_transmitter(std::get<Bytes>(transmission),options):
                                transfer::message_transmitter(std::get<Message>(transmission),options);
                        },[this]{return current_epoch();},wave.stop,minimum);
                        if(options.key) {
                            std::lock_guard lock(mutex);last_pattern_transmit_epoch=std::max(last_pattern_transmit_epoch,scheduled.epoch);
                            wave.protected_epoch=scheduled.epoch;
                        }
                        wave.transmitter=std::move(scheduled.transmitter);
                        datapump::detail::wait_for_playback(scheduled.playback_epoch,[this]{return current_epoch();},wave.stop);
                    };
                } else if (prepared->binary) {
                    const auto& bits = std::get<Bytes>(transmission);
                    prepared->transmitter = transfer::binary_transmitter(bits, value.transfer);
                } else {
                    if (token.stop_requested()) continue;
                    prepared->transmitter = transfer::message_transmitter(std::get<Message>(transmission), value.transfer);
                }
                if (value.simulation) {
                    const auto reserved = replay_workspace(value);
                    if (reserved <= replay_result_workspace) throw Error("DSP workspace cannot hold simulation results");
                    const auto budget = reserved - replay_result_workspace;
                    // Keep a complete measured constellation in each frame;
                    // reduce replay cadence when its rate-sized points need
                    // more room, instead of clipping it to the old PCM tail.
                    prepared->point_limit = std::max<std::size_t>(32, detail::SignalWindow::constellation_capacity(value.transfer.modem));
                    prepared->replay_count = std::min(replay_frames,
                        budget / (replay_frame_base + prepared->point_limit * sizeof(std::complex<float>)));
                    if (prepared->replay_count < 2) throw Error("DSP workspace cannot hold a simulation replay");
                    prepared->replay.reserve(prepared->replay_count);
                    prepared->signals.resize(prepared->replay_count);
                }
                std::lock_guard lock(mutex);
                if (!current.running || generation != version || tx_serial != serial || token.stop_requested()) continue;
                ready = std::move(prepared); if (!value.simulation) capture_stop.request_stop(); changed.notify_all();
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (generation != version || tx_serial != serial) continue;
                tx_busy = false; current.transmitting = !queued.empty(); current.transmission_finished = queued.empty();
                current.transmitting_noise = false;
                current.status = idle_status(); if (!token.stop_requested()) current.error = exception.what(); changed.notify_all();
            }
        }
    }
    void decode_loop(std::stop_token stop) {
        std::optional<Bank> bank; std::uint64_t local_generation = 0, local_revision = 0;
        while (!stop.stop_requested()) {
            Settings value; AudioBlock block; std::uint64_t version; std::stop_token token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this, &local_generation] {
                    return generation != local_generation ||
                           (current.running && !settings.simulation && !input.empty());
                });
                if (stop.stop_requested()) break;
                if (local_generation != generation) {
                    bank.reset(); local_generation = generation; local_revision = receive_revision;
                    decoding_bytes = 0; decoder_generation = generation; changed.notify_all();
                }
                if (!current.running || settings.simulation || input.empty()) continue;
                value = settings; version = generation; token = decode_stop.get_token();
                block = std::move(input.front()); input.pop_front(); input_bytes -= block.samples.size() * sizeof(float);
                decoding_bytes = block.samples.size() * sizeof(float);
                current.buffered_samples = input_bytes / sizeof(float);
                if (local_generation != version || local_revision != block.revision) {
                    local_generation = version; local_revision = block.revision; bank.reset();
                }
            }
            try {
                if (!bank) bank = make_bank(value);
                feed_samples(*bank,block.samples,value,version,token);
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (current.running && generation == version && !token.stop_requested()) current.error = exception.what();
                bank.reset();
            }
            {
                std::lock_guard lock(mutex);
                decoding_bytes = 0;
            }
        }
    }
};

Session::Session(EpochClock epoch_clock, ReplayClock replay_clock)
    : impl_(std::make_unique<Impl>(std::move(epoch_clock), std::move(replay_clock))) {}
Session::~Session() = default;
void Session::start(const Settings& settings) { impl_->configure(settings); }
void Session::configure(const Settings& settings) { impl_->configure(settings); }
void Session::set_mono(bool mono) {
    std::lock_guard lock(impl_->mutex);
    impl_->settings.mono = mono;
}
bool Session::resume_recovery(std::uint64_t signal_id) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->current.running)return false;
    const auto found=std::find_if(impl_->recoveries.begin(),impl_->recoveries.end(),[&](const auto& task) {
        return task->event.id==signal_id && !task->obsolete && task->generation==impl_->generation;
    });
    if(found==impl_->recoveries.end() || *found==impl_->active_recovery)return false;
    const auto& task=*found;
    const auto state=task->event.recovery_progress.state;
    if(state!=transfer::RecoveryState::incomplete && state!=transfer::RecoveryState::cancelled)return false;
    task->stop=std::stop_source{};task->started=false;
    task->event.recovery_progress.state=transfer::RecoveryState::ready;
    auto event=task->event;event.sequence=impl_->next_event++;
    impl_->append_signal(std::move(event));impl_->changed.notify_all();return true;
}
void Session::cancel_recovery(std::uint64_t signal_id) {
    std::lock_guard lock(impl_->mutex);
    for(const auto& task:impl_->recoveries) {
        if(task->event.id!=signal_id || task->obsolete)continue;
        task->stop.request_stop();
        if(task!=impl_->active_recovery) {
            task->started=true;task->event.recovery_progress.state=transfer::RecoveryState::cancelled;
            auto event=task->event;event.sequence=impl_->next_event++;
            impl_->append_signal(std::move(event));
        }
    }
}
void Session::clear_recoveries() {
    std::lock_guard lock(impl_->mutex);
    ++impl_->recovery_clear_generation;
    impl_->discard_staged_recovery(impl_->replay_received,impl_->replay_verified,impl_->replay_signals);
    if(impl_->ready)
        impl_->discard_staged_recovery(impl_->ready->received,impl_->ready->verified,impl_->ready->signals);
    // A finished coordinator can already have released its job while its
    // publication is still waiting for the next UI poll.
    std::erase_if(impl_->current.signals,[](const auto& event) {
        return event.recovery_progress.state!=transfer::RecoveryState::none;
    });
    std::erase_if(impl_->current.received,[&](const auto& received) {
        if(received.recovery_progress.state==transfer::RecoveryState::none)return false;
        impl_->received_bytes-=received.content.message.data.size();return true;
    });
    for(const auto& task:impl_->recoveries)
        std::erase_if(impl_->current.signals,[&](const auto& event){return event.id==task->event.id;});
    impl_->invalidate_recoveries();
}
void Session::transmit(const Message& message) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    if (message.data.size() > impl_->settings.content_limit) throw Error("message exceeds content capacity");
    impl_->enqueue(message);
}
void Session::transmit_bits(std::span<const std::uint8_t> bits) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    if (bits.empty()) throw Error("enter at least one binary bit");
    if (bits.size() > impl_->settings.content_limit) throw Error("binary input exceeds content capacity");
    if (std::any_of(bits.begin(), bits.end(), [](auto bit) { return bit > 1; }))
        throw Error("binary input must contain only 0 and 1 bits");
    impl_->enqueue(Bytes(bits.begin(), bits.end()));
}
void Session::transmit_noise() {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    impl_->enqueue(modem::Noise{});
}
void Session::cancel_transmit() {
    std::lock_guard lock(impl_->mutex);
    impl_->advance_replay(impl_->replay_clock());
    impl_->tx_stop.request_stop(); ++impl_->tx_serial; impl_->queued.clear(); impl_->ready.reset(); impl_->tx_busy = false;
    impl_->current.transmitting = impl_->current.transmitting_noise = false;
    impl_->current.transmission_finished = true; impl_->current.transmission_cancelled = true;
    impl_->clear_replay(); impl_->pending_points = {}; impl_->replay_omitted = 0;
    impl_->current.constellation.clear(); impl_->current.constellation_source = ConstellationSource::input;
    impl_->current.constellation_dropped = 0;
    impl_->current.status = impl_->idle_status(); impl_->changed.notify_all();
}
Snapshot Session::snapshot() {
    std::lock_guard lock(impl_->mutex);
    const auto now = impl_->replay_clock();
    impl_->advance_replay(now);
    impl_->publish_recovery_progress();
    impl_->current.recovery_working_bytes=impl_->recovery_bytes();
    auto signals = std::move(impl_->current.signals); auto received = std::move(impl_->current.received);
    impl_->current.signals.clear(); impl_->current.received.clear(); impl_->received_bytes = 0;
    if (!impl_->pending_points.points.empty() || impl_->pending_points.dropped) {
        // Publication owns the bounded display history. The fresh-point batch
        // only accounts for observations omitted before the UI consumed them.
        impl_->current.constellation_dropped = impl_->pending_points.dropped;
        if (impl_->current.constellation_source != impl_->pending_source)
            count_dropped(impl_->current.constellation_dropped, impl_->pending_points.points.size());
        impl_->pending_points = {}; ++impl_->current.sequence;
    }
    auto result = impl_->current; result.signals = std::move(signals); result.received = std::move(received);
    impl_->replay_snapshot(result, now);
    count_dropped(result.constellation_dropped, impl_->replay_omitted); impl_->replay_omitted = 0;
    return result;
}
void Session::stop() { impl_->halt(); }
}
