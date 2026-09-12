#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include "datapump/channel.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "signal_view.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <thread>
#include <utility>

namespace datapump::live {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t plot_size = 2048;
constexpr std::size_t maximum_events = 64;
constexpr std::size_t replay_frames = 60;
constexpr std::size_t replay_wave_samples = 256;
constexpr std::size_t replay_bins = 257;
constexpr std::size_t constellation_limit = 2048;
constexpr auto replay_duration = std::chrono::seconds(3);
struct ReplayFrame {
    std::vector<float> waveform, spectrum;
    std::vector<std::complex<float>> constellation;
    ConstellationSource source = ConstellationSource::input;
    double fraction = 0;
    std::uint64_t dropped = 0;
};
constexpr std::size_t replay_frame_base = sizeof(ReplayFrame) +
    (replay_wave_samples + replay_bins) * sizeof(float);
std::size_t replay_workspace(const Settings& value) {
    return value.simulation ? std::min(value.dsp_workspace_bytes / 8,
        replay_frames * (replay_frame_base + constellation_limit * sizeof(std::complex<float>))) : 0;
}
double epoch_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string packet_id(const Message& message) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : message.id) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
std::string display_text(const Message& message) {
    if (message.kind != MessageKind::text)
        return terminal_text(Bytes(message.filename.begin(), message.filename.end())) +
               " (" + std::to_string(message.data.size()) + " bytes)";
    const auto size = std::min<std::size_t>(message.data.size(), 4096);
    return terminal_text(std::span<const std::uint8_t>(message.data).first(size));
}
constexpr std::size_t default_workspace = 64 * 1024 * 1024;
constexpr std::size_t minimum_workspace = 512 * 1024;
std::size_t plot_workspace(const Settings& value) { return plot_size * (sizeof(float) + sizeof(double) + 3 * sizeof(std::complex<double>)) +
                                       sizeof(detail::SignalWindow) + modem::SimulationChannel::workspace_bound +
                                       512 * sizeof(modem::SymbolObservation) +
                                       replay_workspace(value); }
std::size_t audio_reserve(const Settings& value) {
    return value.simulation ? 0 : std::min<std::size_t>(5 * 1024 * 1024, value.dsp_workspace_bytes / 8);
}
std::size_t bank_capacity(const Settings& value) {
    // Simulation feeds integrated observations directly; it never allocates
    // the asynchronous hardware capture queue.
    const auto capture_queue = value.simulation ? 0 : value.dsp_workspace_bytes / 8;
    const auto reserved = capture_queue + value.dsp_workspace_bytes / 4 + plot_workspace(value) + audio_reserve(value);
    if (reserved >= value.dsp_workspace_bytes) throw Error("DSP workspace cannot hold the audio and plot buffers");
    return value.dsp_workspace_bytes - reserved;
}
std::size_t packet_budget(std::size_t content) {
    return transfer::packet_workspace_limit(content);
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
    if (value.dsp_workspace_bytes == default_workspace) value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes;
    value.transfer.content_limit = value.content_limit;
    value.transfer.dsp_workspace_bytes = value.dsp_workspace_bytes;
    modem::validate(value.transfer.modem);
    if (!value.simulation && value.transfer.modem.bandwidth_hz > 192000)
        throw Error("This bandwidth requires an SDR frontend; this build supports audio hardware and simulation. Enable simulation for the selected band.");
    if (!value.content_limit) throw Error("received content cache must have a positive capacity");
    (void)packet_budget(value.content_limit);
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
                   std::size_t limit = constellation_limit) {
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
        std::unique_ptr<modem::StreamingTransmitter> transmitter;
        std::uint64_t generation = 0, serial = 0;
        std::uint64_t admission_epoch = 0;
        std::uint64_t tail_remaining = 0;
        bool tail_started = false;
        std::vector<ReplayFrame> replay;
        std::size_t replay_count = 0, point_limit = 0;
        modem::ConstellationBatch interval_points;
        bool interval_locked = false;
        std::stop_token stop;
    };
    struct AudioBlock { std::vector<float> samples; std::uint64_t revision; };
    struct Receiver {
        transfer::Options options;
        Bytes key_tag;
        std::uint64_t epoch = 0, signal_id = 0;
        std::unique_ptr<modem::StreamingReceiver> modem;
        Bytes frame;
        std::size_t wire_offset = 0, last_preview_size = 0;
        std::optional<std::size_t> expected_size;
        std::string last_preview;
    };
    struct Bank {
        std::vector<Receiver> receivers;
        std::optional<std::size_t> active;
        std::size_t working_bytes = 0;
        double created_at = 0;
        std::optional<std::uint64_t> admission_epoch;
    };
    static std::size_t receiver_workspace(const Receiver& receiver) {
        // The decoder retains up to 2048 diagnostic points after locking.
        // Reserve that bounded growth and callback/control storage before
        // admitting a receiver, rather than reporting only its idle allocation.
        return receiver.modem->working_bytes() + 65536 + sizeof(Receiver);
    }
    EpochClock epoch_clock;
    ReplayClock replay_clock;
    std::mutex mutex;
    std::condition_variable_any changed;
    Settings settings;
    Snapshot current;
    std::uint64_t generation = 0, decoder_generation = 0, tx_serial = 0, receive_revision = 0, next_signal = 1, next_event = 1;
    bool tx_busy = false;
    std::deque<Message> queued;
    std::shared_ptr<Prepared> ready;
    std::deque<AudioBlock> input;
    std::size_t input_bytes = 0, decoding_bytes = 0, received_bytes = 0, receiver_bytes = 0;
    std::size_t audio_bytes = 0;
    modem::ConstellationBatch pending_points;
    ConstellationSource pending_source = ConstellationSource::input;
    std::vector<ReplayFrame> replay;
    Clock::time_point replay_started{};
    std::optional<std::size_t> delivered_replay_frame;
    std::size_t first_visible_replay_frame = 0;
    double replay_bin_hz = 0;
    std::vector<std::pair<std::string, std::uint64_t>> signal_ids;
    std::stop_source capture_stop, tx_stop, decode_stop;
    std::jthread source, encoder, decoder;

    explicit Impl(EpochClock clock, ReplayClock presentation_clock)
        : epoch_clock(clock ? std::move(clock) : EpochClock(epoch_now)),
          replay_clock(presentation_clock ? std::move(presentation_clock) : ReplayClock(Clock::now)) {
        (void)current_epoch();
        source = std::jthread([this](std::stop_token stop) { source_loop(stop); });
        encoder = std::jthread([this](std::stop_token stop) { encode_loop(stop); });
        decoder = std::jthread([this](std::stop_token stop) { decode_loop(stop); });
    }
    ~Impl() {
        halt(); source.request_stop(); encoder.request_stop(); decoder.request_stop(); changed.notify_all();
        source.join(); encoder.join(); decoder.join();
    }
    double current_epoch() const {
        const auto now = epoch_clock();
        if (!std::isfinite(now) || now < 0 ||
            static_cast<long double>(now) >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            throw Error("epoch clock must return finite nonnegative Unix seconds within uint64 range");
        return now;
    }
    std::string idle_status() const {
        return settings.simulation ? "Simulated channel; accelerated time, clock error and phase noise" : "Listening to audio input";
    }
    void clear_replay() {
        replay = {}; delivered_replay_frame.reset(); first_visible_replay_frame = 0;
        current.simulation_replay = false;
        current.replay_frame_index = current.replay_frame_count = 0;
        current.simulation_sample_fraction = 0;
    }
    void queue_points(modem::ConstellationBatch batch, ConstellationSource source_kind) {
        if (batch.points.empty() && !batch.dropped) return;
        if (pending_source != source_kind) pending_points = {};
        pending_source = source_kind;
        append_points(pending_points, std::move(batch));
    }
    void replay_snapshot(Snapshot& result) {
        if (replay.empty()) return;
        const auto elapsed = std::max(Clock::duration::zero(), replay_clock() - replay_started);
        if (elapsed >= replay_duration) {
            // A stalled UI must not extend the replay or paint old symbol
            // coordinates over live input. Account for undelivered points.
            const auto first = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            for (auto i = first; i < replay.size(); ++i) {
                count_dropped(result.constellation_dropped, replay[i].constellation.size());
                count_dropped(result.constellation_dropped, replay[i].dropped);
            }
            clear_replay(); current.status = idle_status(); ++current.sequence;
            result.simulation_replay = false; result.replay_frame_index = result.replay_frame_count = 0;
            result.simulation_sample_fraction = 0; result.status = current.status; result.sequence = current.sequence;
            return;
        }
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        const auto index = static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL);
        if (!delivered_replay_frame || index != *delivered_replay_frame) {
            first_visible_replay_frame = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            delivered_replay_frame = index;
        }
        const auto& frame = replay[index];
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
            append_points(visible, std::move(points));
        }
        result.constellation = std::move(visible.points); result.constellation_dropped = visible.dropped;
    }
    void halt() {
        std::lock_guard lock(mutex);
        current.running = current.transmitting = false;
        current.transmission_finished = true; current.transmission_cancelled = true;
        current.status = "Stopped";
        clear_replay(); pending_points = {};
        ++generation; ++tx_serial; ++receive_revision;
        queued.clear(); ready.reset(); tx_busy = false; input.clear(); input_bytes = 0;
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
        clear_replay(); pending_points = {};
        signal_ids.clear();
        current = {}; current.running = true; current.simulation = settings.simulation;
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
                 modem::StreamingTransmitter* transmitter = nullptr, std::uint64_t serial = 0) {
        if (!force && Clock::now() - last_plot < std::chrono::milliseconds(50)) return;
        auto measured = window.frame(config);
        auto transmitted = transmitter ? transmitter->take_payload_constellation() : modem::ConstellationBatch{};
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (transmitter && tx_serial != serial) return;
        current.waveform = std::move(measured.waveform); current.spectrum_db = std::move(measured.spectrum);
        current.constellation = std::move(measured.constellation);
        current.constellation_source = ConstellationSource::input;
        current.constellation_dropped = 0;
        if (transmitter) {
            current.constellation.clear();
            current.constellation_source = ConstellationSource::transmitted;
            queue_points(std::move(transmitted), ConstellationSource::transmitted);
        }
        current.spectrum_bin_hz = static_cast<double>(config.sample_rate) / plot_size;
        ++current.sequence; last_plot = Clock::now();
    }
    static std::uint64_t replay_target(const Prepared& wave, const modem::Config& config) {
        const auto training = modem::training_sample_count(config);
        const auto payload = wave.transmitter->total_samples() - training;
        const auto divisor = wave.replay_count - 1, index = wave.replay.size();
        return training + (payload / divisor) * index + (payload % divisor * index + divisor - 1) / divisor;
    }
    void collect_replay(Prepared& wave, const modem::Config& config, const modem::SimulationChannel& channel) {
        std::vector<float> preview(plot_size);
        channel.preview_last(*wave.transmitter, preview);
        const auto sample_end = channel.preview_end_samples();
        const auto valid = static_cast<std::size_t>(std::min<std::uint64_t>(sample_end, preview.size()));
        auto measured = signal_plots(std::span(preview).last(valid), config, sample_end - valid);
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
        frame.source = wave.interval_locked ? ConstellationSource::received : ConstellationSource::input;
        if (!wave.interval_locked) wave.interval_points.points = std::move(measured.constellation);
        modem::ConstellationBatch bounded;
        append_points(bounded, std::move(wave.interval_points), wave.point_limit);
        frame.constellation.reserve(bounded.points.size());
        for (const auto point : bounded.points) frame.constellation.emplace_back(static_cast<float>(point.real()), static_cast<float>(point.imag()));
        frame.dropped = bounded.dropped;
        const auto training = modem::training_sample_count(config);
        frame.fraction = static_cast<double>(static_cast<long double>(wave.transmitter->samples_emitted() - training) /
                                            (wave.transmitter->total_samples() - training));
        wave.replay.push_back(std::move(frame)); wave.interval_points = {}; wave.interval_locked = false;
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
    Bank make_bank(const Settings& value, std::uint64_t admission_epoch) {
        Bank bank;
        bank.admission_epoch = admission_epoch;
        return make_bank(value, std::move(bank));
    }
    Bank make_bank(const Settings& value, Bank bank) {
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
        const auto now = bank.admission_epoch ? static_cast<double>(*bank.admission_epoch) : current_epoch();
        const auto center = value.transfer.timestamp ? value.transfer.timestamp : static_cast<std::uint64_t>(now);
        const auto limit = packet_budget(value.content_limit);
        for (const auto& key : keys) {
            const auto epochs = key ? drift_candidates(center, value.transfer.search_seconds, true) : std::vector<std::uint64_t>{center};
            for (const auto epoch : epochs) {
                const auto tag = key ? key->mac(Bytes{'D','P','-','R','X','-','B','A','N','K'}) : Bytes{};
                const auto existing = std::find_if(bank.receivers.begin(), bank.receivers.end(), [&](const auto& receiver) {
                    return receiver.key_tag == tag && (tag.empty() || receiver.epoch == epoch);
                });
                if (existing != bank.receivers.end()) continue;
                const auto capacity = bank_capacity(value);
                constexpr auto control_margin = sizeof(Receiver) + 4096;
                if (bank.working_bytes >= capacity || capacity - bank.working_bytes <= control_margin)
                    throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                Receiver receiver;
                receiver.options = value.transfer; receiver.options.key = key; receiver.epoch = epoch;
                receiver.key_tag = tag;
                const auto config = transfer::seeded_config(receiver.options, epoch);
                auto expected = modem::preamble(config);
                if (key) expected = key->xor_data(expected, epoch);
                // Every bootstrap trial uses the same stream position. Derive
                // its mask once, so blind noise acquisition does not repeat
                // HKDF and AES setup for every symbol timing hypothesis.
                const auto bootstrap_mask = transfer::audio_bootstrap_mask(receiver.options, epoch);
                auto validator = [bootstrap_mask, limit](const Bytes& prefix) {
                    try {
                        auto plain = prefix;
                        if (!bootstrap_mask.empty()) {
                            if (plain.size() > bootstrap_mask.size()) return false;
                            for (std::size_t i = 0; i < plain.size(); ++i) plain[i] ^= bootstrap_mask[i];
                        }
                        if (!packet_bootstrap_possible(plain, limit)) return false;
                        return packet_frame_size(plain, limit).has_value();
                    } catch (const Error&) { return false; }
                };
                receiver.modem = std::make_unique<modem::StreamingReceiver>(config, std::move(expected),
                    std::min(value.dsp_workspace_bytes / 2, capacity - bank.working_bytes - control_margin),
                    std::move(validator));
                bank.working_bytes += receiver_workspace(receiver);
                if (bank.working_bytes > bank_capacity(value))
                    throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                bank.receivers.push_back(std::move(receiver));
            }
        }
        bank.created_at = now;
        return bank;
    }
    void refresh_bank(Bank& bank, const Settings& value) {
        // A simulated burst advances media time by DSP work, independently of
        // CPU speed or wall-clock jumps. Retain its admitted key/epoch search
        // until its tail completes; ordinary/idle receivers still refresh.
        if (bank.admission_epoch || value.transfer.timestamp) return;
        const auto now = current_epoch();
        if (now - bank.created_at < 1) return;
        if (std::any_of(bank.receivers.begin(), bank.receivers.end(), [](const auto& receiver) {
            return receiver.modem->synchronized();
        })) return;
        const auto bootstrap_seconds = 5 + static_cast<double>(modem::payload_symbol_count(packet_prefix_size, value.transfer.modem)) *
                                           modem::symbol_seconds(value.transfer.modem);
        // A noise-hidden start cannot supply a trustworthy epoch. Long blind
        // integrations retain a finite admitted bank for one bootstrap span;
        // they cannot admit every wall-clock epoch without unbounded state.
        if (bootstrap_seconds > 30 && now - bank.created_at < bootstrap_seconds) return;
        const auto oldest = now - value.transfer.search_seconds - 1;
        std::erase_if(bank.receivers, [&](const auto& receiver) {
            return !receiver.key_tag.empty() && static_cast<double>(receiver.epoch) < oldest &&
                   now > static_cast<double>(receiver.epoch) + bootstrap_seconds + 1;
        });
        bank.working_bytes = 0;
        for (const auto& receiver : bank.receivers) bank.working_bytes += receiver_workspace(receiver);
        // Reuse admitted states in place. A fresh parallel bank would briefly
        // double the declared DSP allocation during every epoch refresh.
        bank = make_bank(value, std::move(bank));
    }
    void add_signal(SignalUpdate event) {
        event.sequence = next_event++; event.virtual_seconds = current.virtual_seconds;
        if (current.signals.size() == maximum_events) current.signals.erase(current.signals.begin());
        current.signals.push_back(std::move(event));
    }
    std::uint64_t signal_for(const std::string& id) {
        const auto found = std::find_if(signal_ids.begin(), signal_ids.end(), [&](const auto& item) { return item.first == id; });
        if (found != signal_ids.end()) return found->second;
        if (signal_ids.size() == maximum_events) signal_ids.erase(signal_ids.begin());
        const auto result = next_signal++; signal_ids.emplace_back(id, result); return result;
    }
    bool received_wire(Receiver& receiver, Bytes wire, const Settings& value, std::uint64_t version) {
        if (wire.empty()) return false;
        const auto training_bytes = modem::preamble(receiver.options.modem).size();
        const auto old_offset = receiver.wire_offset;
        receiver.wire_offset += wire.size();
        transfer::xor_audio_whitening(wire, old_offset);
        if (receiver.options.key) wire = receiver.options.key->xor_data(wire, receiver.epoch, old_offset);
        const auto skip = old_offset < training_bytes ? std::min(wire.size(), training_bytes - old_offset) : 0;
        const auto limit = packet_budget(value.content_limit);
        if (receiver.frame.size() + wire.size() - skip > limit) throw Error("incoming content exceeds receive cache capacity");
        receiver.frame.insert(receiver.frame.end(), wire.begin() + static_cast<std::ptrdiff_t>(skip), wire.end());
        if (receiver.frame.size() < packet_prefix_size) return false;
        if (!receiver.expected_size) receiver.expected_size = packet_frame_size(receiver.frame, limit);
        const auto expected_size = receiver.expected_size;
        if (!expected_size) return false;
        if (receiver.frame.size() - receiver.last_preview_size >= 16 || receiver.frame.size() >= *expected_size) {
            receiver.last_preview_size = receiver.frame.size();
            const auto preview = preview_packet_partial(receiver.frame, limit);
            if (preview) {
                auto text = display_text(preview->message);
                if (!text.empty() && text != receiver.last_preview) {
                    std::lock_guard lock(mutex);
                    if (current.running && generation == version) {
                        if (!receiver.signal_id) receiver.signal_id = signal_for(packet_id(preview->message));
                        const auto diagnostics=receiver.modem->diagnostics();
                        const auto preamble_percent=diagnostics.preamble_reception?
                            std::optional<double>(100*diagnostics.preamble_reception->received_fraction()):std::nullopt;
                        add_signal({receiver.signal_id, value.transfer.modem.carrier_hz, text, false, packet_id(preview->message),
                                    diagnostics.snr_db, receiver.frame.size(), *expected_size,0,0,preamble_percent,std::nullopt});
                        receiver.last_preview = std::move(text);
                    }
                }
            }
        }
        if (receiver.frame.size() < *expected_size) return false;
        auto packet = decode_packet(receiver.frame, transfer::packet_options(receiver.options, receiver.epoch), limit);
        auto diagnostics = receiver.modem->diagnostics();
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return true;
        if (packet.message.data.size() > value.content_limit) {
            current.status = "Validated packet exceeds received-content cache capacity";
            return true;
        }
        if (!receiver.signal_id) receiver.signal_id = signal_for(packet_id(packet.message));
        const auto preamble_percent=diagnostics.preamble_reception?
            std::optional<double>(100*diagnostics.preamble_reception->received_fraction()):std::nullopt;
        add_signal({receiver.signal_id, value.transfer.modem.carrier_hz, display_text(packet.message), true, packet_id(packet.message),
                    diagnostics.snr_db, packet.consumed_bytes, packet.consumed_bytes,0,0,preamble_percent,packet.pre_fec_accuracy});
        const auto bytes = packet.message.data.size();
        while (!current.received.empty() &&
               (current.received.size() >= maximum_events || received_bytes + bytes > value.content_limit)) {
            received_bytes -= current.received.front().packet.message.data.size();
            current.received.erase(current.received.begin());
        }
        received_bytes += bytes;
        current.received.push_back({std::move(packet), std::move(diagnostics), receiver.epoch});
        signal_ids.clear();
        return true;
    }
    template<class Feed> void feed_bank(Bank& bank, const Settings& value, std::uint64_t version,
                                        std::stop_token stop, Feed feed, Prepared* simulation_wave = nullptr) {
        bool complete = false;
        bool locked = false;
        modem::ConstellationBatch points;
        const auto first = bank.active.value_or(0);
        const auto last = bank.active ? first + 1 : bank.receivers.size();
        for (std::size_t index = first; index < last; ++index) {
            auto& receiver = bank.receivers[index];
            if (stop.stop_requested()) return;
            try {
                auto wire = feed(*receiver.modem);
                if (receiver.modem->synchronized()) {
                    locked = true;
                    append_points(points, receiver.modem->take_payload_constellation());
                }
                if (received_wire(receiver, std::move(wire), value, version)) { complete = true; break; }
                if (receiver.modem->synchronized()) { bank.active = index; break; }
            } catch (const Error&) {
                if (stop.stop_requested()) return;
                receiver.modem->reset(); receiver.frame.clear(); receiver.wire_offset = receiver.last_preview_size = 0;
                receiver.expected_size.reset(); receiver.signal_id = 0; receiver.last_preview.clear();
                if (bank.active) { complete = true; break; }
            }
        }
        if (complete) {
            const auto admission_epoch = bank.admission_epoch;
            bank = {};
            bank.admission_epoch = admission_epoch;
            bank = make_bank(value, std::move(bank));
        } else refresh_bank(bank, value);
        std::lock_guard lock(mutex);
        if (current.running && generation == version) {
            if (simulation_wave) {
                append_points(simulation_wave->interval_points, std::move(points));
                simulation_wave->interval_locked = simulation_wave->interval_locked || locked;
            } else queue_points(std::move(points), ConstellationSource::received);
            receiver_bytes = bank.working_bytes;
            current.dsp_buffered_bytes = receiver_bytes + input_bytes + decoding_bytes + audio_bytes + plot_workspace(value) + (tx_busy ? value.dsp_workspace_bytes / 4 : 0);
        }
    }
    void complete_tx(Prepared& wave, const std::string& error = {}) {
        std::lock_guard lock(mutex);
        if (wave.generation != generation || wave.serial != tx_serial) return;
        tx_busy = false; current.transmitting = !queued.empty();
        current.transmission_finished = queued.empty(); current.transmission_cancelled = wave.stop.stop_requested();
        current.status = idle_status();
        if (!error.empty()) current.error = error;
        else if (!wave.stop.stop_requested()) {
            current.transmission_fraction = 1;
            if (settings.simulation && !wave.replay.empty() && queued.empty()) {
                replay = std::move(wave.replay); delivered_replay_frame.reset(); first_visible_replay_frame = 0;
                replay_bin_hz = static_cast<double>(settings.transfer.modem.sample_rate) / (plot_size / 4);
                replay_started = replay_clock(); current.simulation_replay = true;
                current.replay_frame_count = replay.size();
                current.status = "Simulation replay; three seconds of measured payload plots";
                ++current.sequence;
            }
        }
        changed.notify_all();
    }
    void progress(const Prepared& wave, const Settings& value) {
        std::lock_guard lock(mutex);
        if (generation != wave.generation || tx_serial != wave.serial) return;
        current.transmission_seconds = static_cast<double>(wave.transmitter->samples_emitted()) / value.transfer.modem.sample_rate;
        current.transmission_fraction = static_cast<double>(wave.transmitter->samples_emitted()) /
                                        static_cast<double>(wave.transmitter->total_samples());
    }
    void source_loop(std::stop_token stop) {
        std::uint64_t local_generation = 0;
        std::mt19937_64 random;
        std::normal_distribution<double> normal;
        std::optional<Bank> simulation_bank;
        std::unique_ptr<modem::SimulationChannel> simulation_channel;
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
                value = settings; version = generation; processing_token = decode_stop.get_token();
                if (local_generation != version) {
                    local_generation = version; wave.reset(); simulation_bank.reset(); simulation_channel.reset();
                    random.seed(value.simulation_seed);
                    normal.reset(); last_plot = {};
                    plot_window.reset();
                    idle_fraction = 0;
                }
                if (wave && wave->stop.stop_requested()) {
                    wave.reset();
                    simulation_bank.reset(); simulation_channel.reset();
                }
                if (!wave && ready) {
                    wave = std::move(ready); new_burst = true; pending_points = {};
                    plot_window.reset();
                    current.status = value.simulation ? "Transmitting through noise and oscillator impairments; accelerated time" : "Transmitting; audio input paused";
                }
                if (value.simulation && !wave && tx_busy) {
                    // Packet preparation consumes CPU, not fictitious channel
                    // time. Its real epoch stays anchored for the whole burst.
                    changed.wait_for(lock, stop, std::chrono::milliseconds(5), [this, version] {
                        return !current.running || generation != version || ready || !tx_busy;
                    });
                    continue;
                }
                capture_stop = std::stop_source{}; capture_token = capture_stop.get_token();
            }
            try {
                if (value.simulation) {
                    // The accelerated channel explicitly assumes admitted
                    // burst timing. Admission uses only receiver settings and
                    // the current clock, never the transmitter's key or wire.
                    if (new_burst) {
                        // Release old allocations before constructing their
                        // replacements within the same DSP reservation.
                        simulation_bank.reset();
                        simulation_channel.reset();
                        simulation_bank = make_bank(value, wave->admission_epoch);
                        simulation_channel = std::make_unique<modem::SimulationChannel>(value.transfer.modem, channel_config(value));
                    }
                    if (!simulation_bank) simulation_bank = make_bank(value);
                    const auto sigma = std::sqrt(modem::nominal_signal_power / std::pow(10.0, value.simulation_snr_db / 10));
                    std::vector<float> preview(plot_size);
                    if (wave) {
                        std::vector<modem::SymbolObservation> observations;
                        observations.reserve(512);
                        std::uint64_t samples = 0;
                        bool capture_frame = false;
                        for (std::size_t i = 0; i < 512; ++i) {
                            auto clean = wave->transmitter->next_symbol(wave->stop);
                            if (!clean) {
                                if (!wave->tail_started) {
                                    wave->tail_started = true;
                                    wave->tail_remaining = modem::symbol_sample_count(value.transfer.modem);
                                }
                                if (!wave->tail_remaining) break;
                                const auto quantum = std::max<std::uint64_t>(1, modem::symbol_sample_count(value.transfer.modem) / 32);
                                const auto count = std::min(quantum, wave->tail_remaining);
                                clean = modem::SymbolObservation{{}, count}; wave->tail_remaining -= count;
                            }
                            auto received = wave->tail_started ? std::optional{simulation_channel->noise(clean->sample_count)} : simulation_channel->process(*clean);
                            if (received) { samples += received->sample_count; observations.push_back(*received); }
                            if (!wave->tail_started && wave->replay.size() < wave->replay_count &&
                                wave->transmitter->samples_emitted() >= replay_target(*wave, value.transfer.modem)) {
                                capture_frame = true; break;
                            }
                        }
                        account(samples, version); progress(*wave, value);
                        feed_bank(*simulation_bank, value, version, processing_token, [&](auto& receiver) {
                            return receiver.push_symbols(observations, processing_token);
                        }, wave.get());
                        // Decode through this exact media position before
                        // recording its lock and fresh symbol observations.
                        if (capture_frame) collect_replay(*wave, value.transfer.modem, *simulation_channel);
                        if (wave->tail_started && !wave->tail_remaining) {
                            complete_tx(*wave); wave.reset();
                            // Burst observations and idle PCM use different
                            // integration domains; begin fresh acquisition
                            // before returning to continuous idle reception.
                            simulation_bank.reset(); simulation_channel.reset();
                        }
                        // The media clock advances only by processed quanta;
                        // yielding here keeps control/GUI threads responsive.
                        std::this_thread::yield();
                    } else {
                        // Idle noise only needs to feed the display cadence.
                        // Avoid burning a core simulating hours of empty air;
                        // a queued transmission wakes this wait immediately.
                        idle_fraction += value.transfer.modem.sample_rate % 20;
                        const auto count = std::min<std::size_t>(plot_size,
                            value.transfer.modem.sample_rate / 20 + idle_fraction / 20);
                        idle_fraction %= 20;
                        auto idle = std::span(preview).first(count);
                        for (auto& sample : idle) sample = static_cast<float>(normal(random) * sigma);
                        account(idle.size(), version);
                        plot_window.push(idle);
                        publish(plot_window, value.transfer.modem, version, last_plot);
                        feed_bank(*simulation_bank, value, version, processing_token, [&](auto& receiver) {
                            return receiver.push(idle, processing_token);
                        });
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock, stop, std::chrono::milliseconds(50), [this, version] {
                            return !current.running || generation != version || ready || tx_busy;
                        });
                    }
                    continue;
                }
                if (wave) {
                    discontinuity();
                    audio::playback(value.transfer.modem.sample_rate, value.device, [&](std::span<float> output) {
                        const auto count = wave->transmitter->read(output, wave->stop);
                        account(count, version); progress(*wave, value);
                        plot_window.push(output.first(count));
                        publish(plot_window, value.transfer.modem, version, last_plot,
                                wave->transmitter->finished(), wave->transmitter.get(), wave->serial);
                        return count;
                    }, wave->stop, [&](const auto& format) { audio_format(format, version); });
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
                simulation_bank.reset();
                const auto cancelled_transmission = wave && wave->stop.stop_requested();
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
            Message message; Settings value; std::uint64_t version, serial; std::stop_token token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && !tx_busy && !queued.empty(); });
                if (stop.stop_requested()) break;
                message = std::move(queued.front()); queued.pop_front(); value = settings;
                version = generation; serial = ++tx_serial; tx_stop = std::stop_source{}; token = tx_stop.get_token();
                current.transmission_id = serial;
                clear_replay(); pending_points = {};
                current.constellation.clear(); current.constellation_source = ConstellationSource::input;
                current.constellation_dropped = 0;
                tx_busy = true; current.transmitting = true; current.transmission_finished = false;
                current.transmission_fraction = current.transmission_seconds = 0;
                current.status = "Preparing packet; no complete waveform allocation";
            }
            try {
                // Admit the receiver clock before packet preparation consumes
                // CPU time. This does not inspect the selected key or frame;
                // the normal multi-key candidate radius remains unchanged.
                const auto admission_epoch = static_cast<std::uint64_t>(current_epoch());
                if (!value.transfer.timestamp) value.transfer.timestamp = admission_epoch;
                auto wire = transfer::transmission_wire(message, value.transfer);
                if (token.stop_requested()) continue;
                const auto config = transfer::seeded_config(value.transfer, value.transfer.timestamp);
                auto prepared = std::make_shared<Prepared>();
                prepared->generation = version; prepared->serial = serial; prepared->stop = token;
                prepared->admission_epoch = admission_epoch;
                prepared->transmitter = std::make_unique<modem::StreamingTransmitter>(std::move(wire), config,
                                                                                    value.dsp_workspace_bytes / 4);
                if (value.simulation) {
                    const auto budget = replay_workspace(value);
                    prepared->replay_count = std::min(replay_frames, budget / (replay_frame_base + 32 * sizeof(std::complex<float>)));
                    if (prepared->replay_count < 2) throw Error("DSP workspace cannot hold a simulation replay");
                    prepared->point_limit = std::min(constellation_limit,
                        (budget / prepared->replay_count - replay_frame_base) / sizeof(std::complex<float>));
                    prepared->replay.reserve(prepared->replay_count);
                }
                std::lock_guard lock(mutex);
                if (!current.running || generation != version || tx_serial != serial || token.stop_requested()) continue;
                ready = std::move(prepared); if (!value.simulation) capture_stop.request_stop(); changed.notify_all();
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (generation != version || tx_serial != serial) continue;
                tx_busy = false; current.transmitting = !queued.empty(); current.transmission_finished = queued.empty();
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
                feed_bank(*bank, value, version, token, [&](auto& receiver) { return receiver.push(block.samples, token); });
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
void Session::transmit(const Message& message) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    if (message.data.size() > impl_->settings.content_limit) throw Error("message exceeds content capacity");
    if (impl_->queued.size() >= 8) throw Error("transmit queue contains eight pending messages");
    impl_->queued.push_back(message); impl_->current.transmitting = true;
    impl_->current.transmission_finished = impl_->current.transmission_cancelled = false;
    impl_->current.transmission_fraction = impl_->current.transmission_seconds = 0;
    impl_->clear_replay(); impl_->pending_points = {};
    impl_->current.constellation.clear(); impl_->current.constellation_source = ConstellationSource::input;
    impl_->current.constellation_dropped = 0;
    impl_->current.error.clear(); impl_->changed.notify_all();
}
void Session::cancel_transmit() {
    std::lock_guard lock(impl_->mutex);
    impl_->tx_stop.request_stop(); ++impl_->tx_serial; impl_->queued.clear(); impl_->ready.reset(); impl_->tx_busy = false;
    impl_->current.transmitting = false; impl_->current.transmission_finished = true; impl_->current.transmission_cancelled = true;
    impl_->clear_replay(); impl_->pending_points = {};
    impl_->current.constellation.clear(); impl_->current.constellation_source = ConstellationSource::input;
    impl_->current.constellation_dropped = 0;
    impl_->current.status = impl_->idle_status(); impl_->changed.notify_all();
}
Snapshot Session::snapshot() {
    std::lock_guard lock(impl_->mutex);
    auto signals = std::move(impl_->current.signals); auto received = std::move(impl_->current.received);
    impl_->current.signals.clear(); impl_->current.received.clear(); impl_->received_bytes = 0;
    if (!impl_->pending_points.points.empty() || impl_->pending_points.dropped) {
        impl_->current.constellation = std::move(impl_->pending_points.points);
        impl_->current.constellation_dropped = impl_->pending_points.dropped;
        impl_->current.constellation_source = impl_->pending_source;
        impl_->pending_points = {}; ++impl_->current.sequence;
    }
    auto result = impl_->current; result.signals = std::move(signals); result.received = std::move(received);
    impl_->replay_snapshot(result); return result;
}
void Session::stop() { impl_->halt(); }
}
