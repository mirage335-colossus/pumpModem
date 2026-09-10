#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
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
constexpr std::size_t review_rows = 24;
constexpr std::size_t review_bins = 257;
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
constexpr std::size_t plot_workspace = plot_size * (sizeof(float) + sizeof(double) + 3 * sizeof(std::complex<double>)) +
                                       512 * sizeof(modem::SymbolObservation) +
                                       2 * review_rows * review_bins * sizeof(double) +
                                       2 * (plot_size * sizeof(float) + (plot_size / 2 + 1) * sizeof(double));
std::size_t audio_reserve(const Settings& value) {
    return value.simulation ? 0 : std::min<std::size_t>(5 * 1024 * 1024, value.dsp_workspace_bytes / 8);
}
std::size_t bank_capacity(const Settings& value) {
    const auto reserved = value.dsp_workspace_bytes / 8 + value.dsp_workspace_bytes / 4 + plot_workspace + audio_reserve(value);
    if (reserved >= value.dsp_workspace_bytes) throw Error("DSP workspace cannot hold the audio and plot buffers");
    return value.dsp_workspace_bytes - reserved;
}
std::size_t packet_budget(std::size_t content) {
    return transfer::packet_workspace_limit(content);
}
Settings normalized(Settings value) {
    if (value.content_limit == default_memory_limit) value.content_limit = value.transfer.content_limit;
    if (value.dsp_workspace_bytes == default_workspace) value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes;
    value.transfer.content_limit = value.content_limit;
    value.transfer.dsp_workspace_bytes = value.dsp_workspace_bytes;
    modem::validate(value.transfer.modem);
    if (!value.content_limit) throw Error("received content cache must have a positive capacity");
    (void)packet_budget(value.content_limit);
    if (value.dsp_workspace_bytes < minimum_workspace) throw Error("streaming DSP workspace must be at least 512 KiB");
    (void)bank_capacity(value);
    if (!std::isfinite(value.simulation_snr_db) || std::abs(value.simulation_snr_db) > 200)
        throw Error("simulation sample SNR must be finite and between -200 and 200 dB");
    if (value.receive_keys.size() > 128) throw Error("continuous receiver supports at most 128 loaded keys");
    if (value.transfer.search_seconds > 60) throw Error("continuous timing search exceeds 60 seconds");
    if ((value.transfer.modem.scramble || value.transfer.modem.dsss) &&
        !value.transfer.key && value.receive_keys.empty()) throw Error("encrypted spreading requires a loaded key");
    return value;
}

void fft(std::vector<std::complex<double>>& values) {
    const auto n = values.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2; length <= n; length *= 2) {
        const auto step = std::polar(1.0, -2 * std::numbers::pi / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            std::complex<double> factor{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto a = values[i + j], b = values[i + j + length / 2] * factor;
                values[i + j] = a + b; values[i + j + length / 2] = a - b; factor *= step;
            }
        }
    }
}
struct Plots {
    std::vector<float> waveform;
    std::vector<double> spectrum;
    std::vector<std::complex<double>> constellation;
};
Plots plots(std::vector<float> samples, const modem::Config& config) {
    Plots result;
    result.waveform = std::move(samples);
    std::vector<std::complex<double>> bins(plot_size);
    const auto padding = plot_size - result.waveform.size();
    for (std::size_t i = 0; i < result.waveform.size(); ++i) {
        const auto index = padding + i;
        const auto window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(index) /
                                              static_cast<double>(plot_size - 1));
        bins[index] = static_cast<double>(result.waveform[i]) * window;
    }
    fft(bins);
    for (std::size_t i = 0; i <= plot_size / 2; ++i)
        result.spectrum.push_back(20 * std::log10(std::max(1e-12, std::abs(bins[i]) * 2 / plot_size)));
    // Unsynchronized, measured baseband amplitudes. Integrating each
    // chip avoids displaying a fabricated constellation while the input is noise.
    const auto full_chip = std::max<std::size_t>(4, static_cast<std::size_t>(
        std::llround(static_cast<double>(config.sample_rate) / (config.bandwidth_hz / 2) / 4)) * 4);
    const auto chip = std::min(full_chip, std::max<std::size_t>(1, result.waveform.size() / 16));
    for (std::size_t begin = 0; begin + chip <= result.waveform.size(); begin += chip) {
        std::complex<double> point{};
        for (std::size_t j = 0; j < chip; ++j) {
            const auto phase = -2 * std::numbers::pi * config.carrier_hz * static_cast<double>(begin + j) /
                               config.sample_rate;
            point += static_cast<double>(result.waveform[begin + j]) * std::polar(1.0, phase);
        }
        result.constellation.push_back(point * (2.0 / static_cast<double>(chip)));
    }
    return result;
}
}

struct Session::Impl {
    struct Prepared {
        std::unique_ptr<modem::StreamingTransmitter> transmitter;
        std::uint64_t generation = 0, serial = 0;
        std::uint64_t tail_remaining = 0;
        bool tail_started = false;
        Plots review_sample;
        std::vector<std::vector<double>> review_waterfall;
        double review_fraction = 0;
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
        double created_at = epoch_now();
    };
    static std::size_t receiver_workspace(const Receiver& receiver) {
        // The decoder retains up to 2048 diagnostic points after locking.
        // Reserve that bounded growth and callback/control storage before
        // admitting a receiver, rather than reporting only its idle allocation.
        return receiver.modem->working_bytes() + 65536 + sizeof(Receiver);
    }
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
    std::vector<std::complex<double>> last_receiver_constellation;
    Clock::time_point review_until{};
    std::vector<std::pair<std::string, std::uint64_t>> signal_ids;
    std::stop_source capture_stop, tx_stop, decode_stop;
    std::jthread source, encoder, decoder;

    Impl() {
        source = std::jthread([this](std::stop_token stop) { source_loop(stop); });
        encoder = std::jthread([this](std::stop_token stop) { encode_loop(stop); });
        decoder = std::jthread([this](std::stop_token stop) { decode_loop(stop); });
    }
    ~Impl() {
        halt(); source.request_stop(); encoder.request_stop(); decoder.request_stop(); changed.notify_all();
        source.join(); encoder.join(); decoder.join();
    }
    std::string idle_status() const {
        return settings.simulation ? "Simulated channel; accelerated virtual time, ideal carrier timing" : "Listening to audio input";
    }
    void halt() {
        std::lock_guard lock(mutex);
        current.running = current.transmitting = false;
        current.transmission_finished = true; current.transmission_cancelled = true;
        current.status = "Stopped";
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
        last_receiver_constellation.clear();
        review_until = {};
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
    }
    void publish(std::span<const float> samples, const modem::Config& config, std::uint64_t version,
                 Clock::time_point& last_plot, bool force = false) {
        if (!force && Clock::now() - last_plot < std::chrono::milliseconds(50)) return;
        const auto tail = samples.last(std::min(samples.size(), plot_size));
        auto measured = plots(std::vector<float>(tail.begin(), tail.end()), config);
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (current.simulation_review) {
            if (Clock::now() < review_until) { last_plot = Clock::now(); return; }
            current.simulation_review = false;
        }
        current.waveform = std::move(measured.waveform); current.spectrum_db = std::move(measured.spectrum);
        if (!current.constellation_retained) current.constellation = std::move(measured.constellation);
        current.spectrum_bin_hz = static_cast<double>(config.sample_rate) / plot_size;
        ++current.sequence; last_plot = Clock::now();
    }
    // Select the review by media position, not wall-clock/UI polling. Uniform
    // observations are small enough to capture a genuine payload-midpoint
    // sample even when an entire simulation finishes between two GUI polls.
    void collect_review(Prepared& wave, const modem::Config& config, double sigma,
                        std::mt19937_64& random, std::normal_distribution<double>& normal) {
        if (wave.tail_started || wave.review_waterfall.size() == review_rows) return;
        const auto training = modem::training_sample_count(config);
        const auto total = wave.transmitter->total_samples();
        if (total <= training || wave.transmitter->samples_emitted() <= training) return;
        const auto fraction = static_cast<long double>(wave.transmitter->samples_emitted() - training) /
                              static_cast<long double>(total - training);
        const auto target = [&](std::size_t row) {
            return .25L + .25L * static_cast<long double>(row) / static_cast<long double>(review_rows - 1);
        };
        if (fraction < target(wave.review_waterfall.size())) return;
        std::vector<float> preview(plot_size);
        wave.transmitter->preview_last(preview);
        for (auto& sample : preview) sample += static_cast<float>(normal(random) * sigma);
        auto measured = plots(std::move(preview), config);
        std::vector<double> compact(review_bins);
        for (std::size_t i = 0; i < compact.size(); ++i) {
            const auto begin = i * 4;
            const auto end = std::min(begin + 4, measured.spectrum.size());
            compact[i] = *std::max_element(measured.spectrum.begin() + static_cast<std::ptrdiff_t>(begin),
                                            measured.spectrum.begin() + static_cast<std::ptrdiff_t>(end));
        }
        do { wave.review_waterfall.push_back(compact); }
        while (wave.review_waterfall.size() < review_rows && fraction >= target(wave.review_waterfall.size()));
        wave.review_fraction = static_cast<double>(fraction);
        wave.review_sample = std::move(measured);
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
        current.dsp_buffered_bytes = input_bytes + decoding_bytes + receiver_bytes + audio_bytes + plot_workspace;
        changed.notify_all();
    }
    void discontinuity() {
        std::lock_guard lock(mutex);
        input.clear(); input_bytes = 0; ++receive_revision;
        current.buffered_samples = 0; changed.notify_all();
    }
    Bank make_bank(const Settings& value) { return make_bank(value, Bank{}); }
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
        const auto center = value.transfer.timestamp ? value.transfer.timestamp : static_cast<std::uint64_t>(epoch_now());
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
                const auto preamble_bytes = expected.size();
                if (key) expected = key->xor_data(expected, epoch);
                // Every bootstrap trial uses the same stream position. Derive
                // its mask once, so blind noise acquisition does not repeat
                // HKDF and AES setup for every symbol timing hypothesis.
                const auto bootstrap_mask = key ? key->xor_data(Bytes(packet_prefix_size), epoch, preamble_bytes) : Bytes{};
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
        bank.created_at = epoch_now();
        return bank;
    }
    void refresh_bank(Bank& bank, const Settings& value) {
        if (value.transfer.timestamp || epoch_now() - bank.created_at < 1) return;
        if (std::any_of(bank.receivers.begin(), bank.receivers.end(), [](const auto& receiver) {
            return receiver.modem->synchronized();
        })) return;
        const auto bootstrap_seconds = 5 + static_cast<double>(modem::payload_symbol_count(packet_prefix_size, value.transfer.modem)) *
                                           modem::symbol_seconds(value.transfer.modem);
        // A noise-hidden start cannot supply a trustworthy epoch. Long blind
        // integrations retain a finite admitted bank for one bootstrap span;
        // they cannot admit every wall-clock epoch without unbounded state.
        if (bootstrap_seconds > 30 && epoch_now() - bank.created_at < bootstrap_seconds) return;
        const auto now = epoch_now();
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
                        add_signal({receiver.signal_id, value.transfer.modem.carrier_hz, text, false, packet_id(preview->message),
                                    receiver.modem->diagnostics().snr_db, receiver.frame.size(), *expected_size});
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
        add_signal({receiver.signal_id, value.transfer.modem.carrier_hz, display_text(packet.message), true, packet_id(packet.message),
                    diagnostics.snr_db, packet.consumed_bytes, packet.consumed_bytes});
        const auto bytes = packet.message.data.size();
        while (!current.received.empty() &&
               (current.received.size() >= maximum_events || received_bytes + bytes > value.content_limit)) {
            received_bytes -= current.received.front().packet.message.data.size();
            current.received.erase(current.received.begin());
        }
        received_bytes += bytes;
        current.constellation = diagnostics.constellation;
        last_receiver_constellation = diagnostics.constellation;
        current.received.push_back({std::move(packet), std::move(diagnostics), receiver.epoch});
        signal_ids.clear();
        return true;
    }
    template<class Feed> void feed_bank(Bank& bank, const Settings& value, std::uint64_t version,
                                        std::stop_token stop, Feed feed) {
        bool complete = false;
        const auto first = bank.active.value_or(0);
        const auto last = bank.active ? first + 1 : bank.receivers.size();
        for (std::size_t index = first; index < last; ++index) {
            auto& receiver = bank.receivers[index];
            if (stop.stop_requested()) return;
            try {
                auto wire = feed(*receiver.modem);
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
            bank = {};
            bank = make_bank(value);
        } else refresh_bank(bank, value);
        std::lock_guard lock(mutex);
        if (current.running && generation == version) {
            receiver_bytes = bank.working_bytes;
            current.dsp_buffered_bytes = receiver_bytes + input_bytes + decoding_bytes + audio_bytes + plot_workspace + (tx_busy ? value.dsp_workspace_bytes / 4 : 0);
        }
    }
    void complete_tx(const Prepared& wave, const std::string& error = {}) {
        std::lock_guard lock(mutex);
        if (wave.generation != generation || wave.serial != tx_serial) return;
        tx_busy = false; current.transmitting = !queued.empty();
        current.transmission_finished = queued.empty(); current.transmission_cancelled = wave.stop.stop_requested();
        current.status = idle_status();
        if (!error.empty()) current.error = error;
        else if (!wave.stop.stop_requested()) {
            current.transmission_fraction = 1;
            if (settings.simulation && !wave.review_sample.waveform.empty() && queued.empty()) {
                current.waveform = wave.review_sample.waveform;
                current.spectrum_db = wave.review_sample.spectrum;
                current.constellation_retained = !last_receiver_constellation.empty();
                current.constellation = current.constellation_retained ? last_receiver_constellation : wave.review_sample.constellation;
                current.spectrum_bin_hz = static_cast<double>(settings.transfer.modem.sample_rate) / plot_size;
                current.simulation_waterfall = wave.review_waterfall;
                current.simulation_waterfall_bin_hz = static_cast<double>(settings.transfer.modem.sample_rate) / (plot_size / 4);
                current.simulation_sample_fraction = wave.review_fraction;
                current.simulation_review = true;
                review_until = Clock::now() + std::chrono::seconds(2);
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
        std::mt19937_64 plot_random;
        std::normal_distribution<double> normal;
        std::normal_distribution<double> plot_normal;
        std::optional<Bank> simulation_bank;
        std::shared_ptr<Prepared> wave;
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
                    local_generation = version; wave.reset(); simulation_bank.reset();
                    random.seed(value.simulation_seed); plot_random.seed(value.simulation_seed ^ 0x504c4f5453ULL);
                    normal.reset(); plot_normal.reset(); last_plot = {};
                }
                if (wave && wave->stop.stop_requested()) wave.reset();
                if (!wave && ready) {
                    wave = std::move(ready); new_burst = true; last_receiver_constellation.clear();
                    current.status = value.simulation ? "Transmitting through simulated AWGN; accelerated virtual time" : "Transmitting; audio input paused";
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
                    if (new_burst) simulation_bank.reset();
                    if (!simulation_bank) simulation_bank = make_bank(value);
                    const auto sigma = std::sqrt(modem::nominal_signal_power / std::pow(10.0, value.simulation_snr_db / 10));
                    std::vector<float> preview(plot_size);
                    if (wave) {
                        std::vector<modem::SymbolObservation> observations;
                        observations.reserve(512);
                        std::uint64_t samples = 0;
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
                            collect_review(*wave, value.transfer.modem, sigma, plot_random, plot_normal);
                            samples += clean->sample_count;
                            observations.push_back(modem::add_awgn(*clean, value.simulation_snr_db, random));
                        }
                        account(samples, version); progress(*wave, value);
                        feed_bank(*simulation_bank, value, version, processing_token, [&](auto& receiver) {
                            return receiver.push_symbols(observations, processing_token);
                        });
                        if (Clock::now() - last_plot >= std::chrono::milliseconds(50) || wave->transmitter->finished()) {
                            if (!wave->tail_started) wave->transmitter->preview_last(preview);
                            for (auto& sample : preview) sample += static_cast<float>(plot_normal(plot_random) * sigma);
                            publish(preview, value.transfer.modem, version, last_plot, true);
                            std::lock_guard lock(mutex);
                            if (current.running && generation == version) {
                                for (const auto& receiver : simulation_bank->receivers) {
                                    const auto diagnostics = receiver.modem->diagnostics();
                                    if (receiver.modem->synchronized() && !diagnostics.constellation.empty()) {
                                        last_receiver_constellation = diagnostics.constellation; break;
                                    }
                                }
                                if (!last_receiver_constellation.empty()) current.constellation = last_receiver_constellation;
                            }
                        }
                        if (wave->tail_started && !wave->tail_remaining) { complete_tx(*wave); wave.reset(); }
                        // The media clock advances only by processed quanta;
                        // yielding here keeps control/GUI threads responsive.
                        std::this_thread::yield();
                    } else {
                        for (auto& sample : preview) sample = static_cast<float>(normal(random) * sigma);
                        account(preview.size(), version);
                        publish(preview, value.transfer.modem, version, last_plot);
                        feed_bank(*simulation_bank, value, version, processing_token, [&](auto& receiver) {
                            return receiver.push(preview, processing_token);
                        });
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock, stop, std::chrono::milliseconds(5), [this, version] {
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
                        publish(output.first(count), value.transfer.modem, version, last_plot);
                        return count;
                    }, wave->stop, [&](const auto& format) { audio_format(format, version); });
                    discontinuity(); complete_tx(*wave); wave.reset(); continue;
                }
                {
                    std::lock_guard lock(mutex);
                    if (generation == version) { current.status = idle_status(); current.error.clear(); }
                }
                audio::capture(value.transfer.modem.sample_rate, value.device, [&](std::span<const float> chunk) {
                    account(chunk.size(), version); publish(chunk, value.transfer.modem, version, last_plot);
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
                current.simulation_review = current.constellation_retained = false;
                current.simulation_waterfall.clear(); current.simulation_sample_fraction = 0;
                last_receiver_constellation.clear(); review_until = {};
                tx_busy = true; current.transmitting = true; current.transmission_finished = false;
                current.transmission_fraction = current.transmission_seconds = 0;
                current.status = "Preparing packet; no complete waveform allocation";
                if (!value.transfer.timestamp) value.transfer.timestamp = static_cast<std::uint64_t>(epoch_now());
            }
            try {
                auto wire = transfer::transmission_wire(message, value.transfer);
                if (token.stop_requested()) continue;
                const auto config = transfer::seeded_config(value.transfer, value.transfer.timestamp);
                auto prepared = std::make_shared<Prepared>();
                prepared->generation = version; prepared->serial = serial; prepared->stop = token;
                prepared->transmitter = std::make_unique<modem::StreamingTransmitter>(std::move(wire), config,
                                                                                    value.dsp_workspace_bytes / 4);
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

Session::Session() : impl_(std::make_unique<Impl>()) {}
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
    impl_->current.simulation_review = impl_->current.constellation_retained = false;
    impl_->current.simulation_waterfall.clear(); impl_->current.simulation_sample_fraction = 0;
    impl_->last_receiver_constellation.clear(); impl_->review_until = {};
    impl_->current.error.clear(); impl_->changed.notify_all();
}
void Session::cancel_transmit() {
    std::lock_guard lock(impl_->mutex);
    impl_->tx_stop.request_stop(); ++impl_->tx_serial; impl_->queued.clear(); impl_->ready.reset(); impl_->tx_busy = false;
    impl_->current.transmitting = false; impl_->current.transmission_finished = true; impl_->current.transmission_cancelled = true;
    impl_->current.simulation_review = false;
    impl_->current.status = impl_->idle_status(); impl_->changed.notify_all();
}
Snapshot Session::snapshot() {
    std::lock_guard lock(impl_->mutex);
    auto signals = std::move(impl_->current.signals); auto received = std::move(impl_->current.received);
    impl_->current.signals.clear(); impl_->current.received.clear(); impl_->received_bytes = 0;
    auto result = impl_->current; result.signals = std::move(signals); result.received = std::move(received); return result;
}
void Session::stop() { impl_->halt(); }
}
