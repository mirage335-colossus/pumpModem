#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include "datapump/runtime.hpp"
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
    const auto size = std::min<std::size_t>(message.data.size(), 65536);
    return terminal_text(std::span<const std::uint8_t>(message.data).first(size));
}
std::size_t maximum_samples(const Settings& settings) {
    // Reserve room for the circular input, its decode snapshot, the prepared
    // transmission, and acquisition's FFT working arrays. The modem also
    // performs its own exact allocation checks for each individual operation.
    return settings.transfer.modem.memory_limit / 64;
}
std::size_t buffer_capacity(const Settings& settings) {
    const auto samples = settings.receive_buffer_seconds * settings.transfer.modem.sample_rate;
    if (!std::isfinite(samples) || samples < 1)
        throw Error("continuous receive window must be finite and positive");
    return static_cast<std::size_t>(std::min(std::ceil(samples), static_cast<double>(maximum_samples(settings))));
}
void validate_settings(const Settings& settings) {
    modem::validate(settings.transfer.modem);
    if (!std::isfinite(settings.simulation_snr_db) || std::abs(settings.simulation_snr_db) > 200)
        throw Error("simulation sample SNR must be finite and between -200 and 200 dB");
    if (!std::isfinite(settings.simulation_speed) || settings.simulation_speed < 0.1 || settings.simulation_speed > 64)
        throw Error("simulation speed must be between 0.1 and 64");
    if (settings.receive_keys.size() > 128) throw Error("continuous receiver supports at most 128 loaded keys");
    if (settings.transfer.search_seconds > 60) throw Error("continuous timing search exceeds 60 seconds");
    (void)buffer_capacity(settings);
    if ((settings.transfer.modem.scramble || settings.transfer.modem.dsss) &&
        !settings.transfer.key && settings.receive_keys.empty())
        throw Error("encrypted spreading requires a loaded key");
}

class Ring {
public:
    void reset(std::size_t capacity) { data_.assign(capacity, 0); head_ = count_ = 0; total_ = 0; }
    void grow(std::size_t capacity) {
        if (capacity <= data_.size()) return;
        auto old = copy();
        data_.assign(capacity, 0); head_ = 0;
        std::copy(old.begin(), old.end(), data_.begin());
    }
    void append(std::span<const float> input) {
        for (auto value : input) {
            if (count_ < data_.size()) { data_[(head_ + count_) % data_.size()] = value; ++count_; }
            else { data_[head_] = value; head_ = (head_ + 1) % data_.size(); }
            ++total_;
        }
    }
    std::vector<float> copy(std::size_t last = std::numeric_limits<std::size_t>::max()) const {
        const auto n = std::min(count_, last);
        std::vector<float> output(n);
        for (std::size_t i = 0; i < n; ++i) output[i] = data_[(head_ + count_ - n + i) % data_.size()];
        return output;
    }
    void discard_to(std::uint64_t absolute) {
        if (absolute <= begin()) return;
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(absolute - begin(), count_));
        head_ = (head_ + n) % data_.size(); count_ -= n;
    }
    void clear() { head_ = count_ = 0; }
    std::size_t size() const { return count_; }
    std::uint64_t begin() const { return total_ - count_; }
    std::uint64_t total() const { return total_; }
private:
    std::vector<float> data_;
    std::size_t head_ = 0, count_ = 0;
    std::uint64_t total_ = 0;
};

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
    // Unsynchronized, measured differential baseband points. Integrating each
    // chip avoids displaying a fabricated constellation while the input is noise.
    const auto full_chip = std::max<std::size_t>(4, static_cast<std::size_t>(
        std::llround(static_cast<double>(config.sample_rate) / (config.bandwidth_hz / 2) / 4)) * 4);
    const auto chip = std::min(full_chip, std::max<std::size_t>(1, result.waveform.size() / 16));
    std::complex<double> previous{};
    bool have_previous = false;
    for (std::size_t begin = 0; begin + chip <= result.waveform.size(); begin += chip) {
        std::complex<double> point{};
        for (std::size_t j = 0; j < chip; ++j) {
            const auto phase = -2 * std::numbers::pi * config.carrier_hz * static_cast<double>(begin + j) /
                               config.sample_rate;
            point += static_cast<double>(result.waveform[begin + j]) * std::polar(1.0, phase);
        }
        if (have_previous) {
            const auto scale = std::abs(previous) * std::abs(point);
            if (scale > 1e-20) result.constellation.push_back(point * std::conj(previous) / scale);
        }
        previous = point; have_previous = true;
    }
    return result;
}
}

struct Session::Impl {
    struct Prepared {
        std::vector<float> samples;
        std::uint64_t generation = 0, serial = 0;
        std::stop_token stop;
    };
    std::mutex mutex;
    std::condition_variable_any changed;
    Settings settings;
    Snapshot current;
    Ring ring;
    std::uint64_t generation = 0, tx_serial = 0, next_signal = 1;
    double anchor = epoch_now();
    std::string listening_status;
    bool acquisition_supported = true;
    bool tx_busy = false;
    std::deque<Message> queued;
    std::size_t queued_bytes = 0, received_bytes = 0;
    std::shared_ptr<Prepared> ready;
    std::stop_source capture_stop, tx_stop, decode_stop;
    std::jthread source, encoder, decoder;

    Impl() {
        source = std::jthread([this](std::stop_token stop) { source_loop(stop); });
        encoder = std::jthread([this](std::stop_token stop) { encode_loop(stop); });
        decoder = std::jthread([this](std::stop_token stop) { decode_loop(stop); });
    }
    ~Impl() {
        halt();
        source.request_stop(); encoder.request_stop(); decoder.request_stop();
        changed.notify_all();
        // Join while every mutex, condition variable and shared buffer exists.
        source.join(); encoder.join(); decoder.join();
    }
    void halt() {
        std::lock_guard lock(mutex);
        current.running = false; current.transmitting = false;
        current.status = "Stopped";
        ++generation; ++tx_serial;
        queued.clear(); queued_bytes = 0; ready.reset(); tx_busy = false;
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop();
        changed.notify_all();
    }
    void configure(const Settings& value) {
        validate_settings(value);
        std::lock_guard lock(mutex);
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop();
        decode_stop = std::stop_source{};
        settings = value;
        ++generation; ++tx_serial;
        queued.clear(); queued_bytes = received_bytes = 0; ready.reset(); tx_busy = false;
        ring.reset(buffer_capacity(value)); anchor = epoch_now();
        current = {};
        current.running = true; current.simulation = value.simulation;
        listening_status = value.simulation ? "Listening to simulated channel" : "Listening to audio input";
        acquisition_supported = true;
        try {
            const auto training = modem::preamble(value.transfer.modem);
            acquisition_supported = modem::waveform_sample_count(training.size() + packet_prefix_size, value.transfer.modem) <= maximum_samples(value) &&
                                    modem::memory_supported(training.size() + packet_prefix_size, training.size(), value.transfer.modem);
        } catch (const Error&) { acquisition_supported = false; }
        if (!acquisition_supported) listening_status += "; selected mode exceeds the bounded acquisition window";
        current.status = listening_status;
        changed.notify_all();
    }
    void ingest(std::span<const float> samples, std::uint64_t version, Clock::time_point& last_plot) {
        std::vector<float> tail;
        modem::Config config;
        {
            std::lock_guard lock(mutex);
            if (!current.running || generation != version) return;
            ring.append(samples);
            current.samples_received = ring.total(); current.buffered_samples = ring.size();
            if (Clock::now() - last_plot < std::chrono::milliseconds(50)) return;
            tail = ring.copy(plot_size); config = settings.transfer.modem;
        }
        auto measured = plots(std::move(tail), config);
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        current.waveform = std::move(measured.waveform);
        current.spectrum_db = std::move(measured.spectrum);
        current.constellation = std::move(measured.constellation);
        current.spectrum_bin_hz = static_cast<double>(config.sample_rate) / plot_size;
        ++current.sequence; last_plot = Clock::now(); changed.notify_all();
    }
    void complete_tx(const Prepared& wave, const std::string& error = {}) {
        std::lock_guard lock(mutex);
        if (wave.generation != generation || wave.serial != tx_serial) return;
        tx_busy = false;
        current.transmitting = !queued.empty();
        current.status = listening_status;
        if (!error.empty()) current.error = error;
        changed.notify_all();
    }
    void source_loop(std::stop_token stop) {
        std::uint64_t local_generation = 0;
        std::mt19937_64 random;
        std::normal_distribution<double> normal;
        std::shared_ptr<Prepared> wave;
        std::size_t position = 0;
        double sigma = 0;
        auto next = Clock::now(), last_plot = Clock::time_point{};
        while (!stop.stop_requested()) {
            Settings value;
            std::uint64_t version;
            std::stop_token capture_token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running; });
                if (stop.stop_requested()) break;
                value = settings; version = generation;
                if (version != local_generation) {
                    local_generation = version; wave.reset(); position = 0;
                    random.seed(value.simulation_seed); normal.reset();
                    sigma = std::sqrt(0.245 / std::pow(10.0, value.simulation_snr_db / 10));
                    next = Clock::now(); last_plot = {};
                }
                if (wave && wave->stop.stop_requested()) { wave.reset(); position = 0; }
                if (!wave && ready) {
                    wave = std::move(ready); position = 0;
                    double power = 0;
                    for (const auto sample : wave->samples) power += static_cast<double>(sample) * sample;
                    power /= static_cast<double>(wave->samples.size());
                    sigma = std::sqrt(power / std::pow(10.0, value.simulation_snr_db / 10));
                    current.status = value.simulation ? "Transmitting into simulated channel" : "Transmitting; input paused";
                }
                capture_stop = std::stop_source{}; capture_token = capture_stop.get_token();
            }
            if (value.simulation) {
                const auto count = std::max<std::size_t>(1, value.transfer.modem.sample_rate / 20);
                std::vector<float> chunk(count);
                for (auto& sample : chunk) {
                    const auto signal = wave && position < wave->samples.size() ? wave->samples[position++] : 0;
                    sample = static_cast<float>(signal + normal(random) * sigma);
                }
                ingest(chunk, version, last_plot);
                if (wave && position == wave->samples.size()) { complete_tx(*wave); wave.reset(); }
                next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(0.05 / value.simulation_speed));
                if (next < Clock::now()) next = Clock::now();
                std::unique_lock lock(mutex);
                changed.wait_until(lock, stop, next, [this, version] { return !current.running || generation != version; });
                continue;
            }
            if (wave) {
                { std::lock_guard lock(mutex); if (generation == version) ring.clear(); }
                std::string error;
                try { audio::play(wave->samples, value.transfer.modem.sample_rate, value.device, wave->stop); }
                catch (const std::exception& exception) { if (!wave->stop.stop_requested()) error = exception.what(); }
                {
                    std::lock_guard lock(mutex);
                    if (generation == version) { ring.clear(); anchor = epoch_now() - static_cast<double>(ring.total()) / value.transfer.modem.sample_rate; }
                }
                complete_tx(*wave, error); wave.reset();
                continue;
            }
            try {
                {
                    std::lock_guard lock(mutex);
                    if (generation == version) {
                        current.status = listening_status; current.error.clear(); ring.clear();
                        anchor = epoch_now() - static_cast<double>(ring.total()) / value.transfer.modem.sample_rate;
                    }
                }
                audio::capture(value.transfer.modem.sample_rate, value.device,
                    [this, version, &last_plot](std::span<const float> chunk) {
                        ingest(chunk, version, last_plot);
                        std::lock_guard lock(mutex);
                        return current.running && generation == version && !ready;
                    }, capture_token);
            } catch (const std::exception& exception) {
                std::unique_lock lock(mutex);
                if (current.running && generation == version && !capture_token.stop_requested()) {
                    current.error = exception.what(); current.status = "Audio input unavailable; retrying";
                    changed.wait_for(lock, stop, std::chrono::seconds(2), [this, version] {
                        return !current.running || generation != version || static_cast<bool>(ready);
                    });
                }
            }
        }
    }
    void encode_loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Message message;
            Settings value;
            std::uint64_t version, serial;
            std::stop_token token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && !tx_busy && !queued.empty(); });
                if (stop.stop_requested()) break;
                message = std::move(queued.front()); queued.pop_front(); queued_bytes -= message.data.size();
                value = settings; version = generation; serial = ++tx_serial;
                tx_stop = std::stop_source{}; token = tx_stop.get_token(); tx_busy = true;
                current.transmitting = true; current.status = "Encoding transmission; input continues";
                if (!value.transfer.timestamp)
                    value.transfer.timestamp = static_cast<std::uint64_t>(anchor + static_cast<double>(ring.total()) / value.transfer.modem.sample_rate);
            }
            try {
                const auto estimate = transfer::estimate(message, value.transfer);
                const auto capacity = estimate.waveform_samples + static_cast<std::size_t>(value.transfer.modem.sample_rate) * 3;
                if (!estimate.memory_supported || capacity > maximum_samples(value))
                    throw Error("transmission is too long for the bounded continuous receiver; increase bandwidth or shorten the content");
                auto prepared = std::make_shared<Prepared>();
                prepared->generation = version; prepared->serial = serial; prepared->stop = token;
                prepared->samples = transfer::transmit(message, value.transfer, token);
                std::lock_guard lock(mutex);
                if (!current.running || generation != version || tx_serial != serial || token.stop_requested()) continue;
                ring.grow(capacity);
                ready = std::move(prepared);
                if (!value.simulation) capture_stop.request_stop();
                changed.notify_all();
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (generation != version || tx_serial != serial) continue;
                tx_busy = false; current.transmitting = !queued.empty();
                current.status = listening_status;
                if (!token.stop_requested()) current.error = exception.what();
                changed.notify_all();
            }
        }
    }

    struct Candidate { transfer::Options options; std::uint64_t timestamp; };
    std::vector<Candidate> candidates(const Settings& value, double end_epoch) {
        std::vector<Candidate> result;
        auto options = value.transfer;
        std::vector<std::optional<Crypto>> keys;
        if (value.permits_plaintext()) keys.emplace_back();
        if (options.key) keys.push_back(options.key);
        for (const auto& key : value.receive_keys) keys.emplace_back(key);
        const auto center = options.timestamp ? options.timestamp : static_cast<std::uint64_t>(
            std::max(0.0, end_epoch - options.modem.training_seconds));
        for (const auto& key : keys) {
            options.key = key;
            const auto epochs = drift_candidates(center, options.search_seconds, key.has_value());
            for (const auto epoch : epochs) result.push_back({options, epoch});
        }
        return result;
    }
    void add_signal(SignalUpdate update) {
        const auto same = std::find_if(current.signals.begin(), current.signals.end(), [&](const auto& old) { return old.id == update.id; });
        if (same != current.signals.end()) *same = std::move(update);
        else {
            if (current.signals.size() == maximum_events) current.signals.erase(current.signals.begin());
            current.signals.push_back(std::move(update));
        }
    }
    void decode_loop(std::stop_token stop) {
        std::uint64_t local_generation = 0, last_total = 0, signal_id = 0, acquired_begin = 0;
        std::optional<Candidate> acquired;
        std::vector<Candidate> search;
        std::size_t search_index = 0;
        std::string last_preview;
        auto next_decode = Clock::now();
        while (!stop.stop_requested()) {
            Settings value;
            std::vector<float> samples;
            std::uint64_t version, begin, end;
            double end_epoch;
            std::stop_token decode_token;
            {
                std::unique_lock lock(mutex);
                changed.wait_until(lock, stop, next_decode, [this, local_generation] {
                    return current.running && generation != local_generation;
                });
                if (stop.stop_requested()) break;
                next_decode = Clock::now() + std::chrono::milliseconds(100);
                if (!current.running) continue;
                value = settings; version = generation;
                if (version != local_generation) {
                    local_generation = version; last_total = 0; acquired.reset(); search.clear();
                    search_index = 0; signal_id = 0; last_preview.clear();
                }
                if (!acquisition_supported) continue;
                if (last_total == ring.total()) continue;
                try {
                    const auto training = modem::preamble(value.transfer.modem);
                    if (ring.size() < modem::waveform_sample_count(training.size(), value.transfer.modem)) continue;
                } catch (const Error&) { continue; }
                begin = ring.begin(); end = ring.total();
                if (acquired && begin > acquired_begin) { acquired.reset(); search.clear(); signal_id = 0; }
                samples = ring.copy(); last_total = end;
                decode_token = decode_stop.get_token();
                end_epoch = anchor + static_cast<double>(end) / value.transfer.modem.sample_rate;
            }
            if (search.empty() || search_index == search.size()) { search = candidates(value, end_epoch); search_index = 0; }
            auto candidate = acquired ? *acquired : search[search_index++];
            try {
                const auto config = transfer::seeded_config(candidate.options, candidate.timestamp);
                const auto training = modem::preamble(config);
                const auto expected = candidate.options.key ? candidate.options.key->xor_data(training, candidate.timestamp) : training;
                auto decoded = modem::demodulate(samples, config, expected, decode_token);
                if (stop.stop_requested()) break;
                if (decoded.bytes.size() < training.size()) continue;
                if (!acquired) {
                    acquired = candidate; acquired_begin = begin + decoded.diagnostics.sample_offset;
                    signal_id = next_signal++; last_preview.clear();
                }
                const auto plaintext = candidate.options.key ? candidate.options.key->xor_data(decoded.bytes, candidate.timestamp) : std::move(decoded.bytes);
                const Bytes frame(plaintext.begin() + static_cast<std::ptrdiff_t>(training.size()), plaintext.end());
                const auto preview = preview_packet_partial(frame, config.memory_limit);
                if (preview) {
                    auto text = display_text(preview->message);
                    if (text != last_preview) {
                        std::lock_guard lock(mutex);
                        if (current.running && generation == version) {
                            add_signal({signal_id, config.carrier_hz, text, false, packet_id(preview->message),
                                decoded.diagnostics.snr_db, frame.size(), preview->wire_size});
                            last_preview = std::move(text);
                        }
                    }
                }
                const auto size = packet_frame_size(frame, config.memory_limit);
                if (size) {
                    const auto capacity = modem::waveform_sample_count(training.size() + *size, config) +
                                          static_cast<std::size_t>(config.sample_rate) * 3;
                    std::lock_guard lock(mutex);
                    if (current.running && generation == version) {
                        if (capacity <= maximum_samples(value)) ring.grow(capacity);
                        else current.status = "Listening; incoming packet exceeds the bounded acquisition window";
                    }
                }
                if (!size || frame.size() < *size) continue;
                auto packet = decode_packet(frame, transfer::packet_options(candidate.options, candidate.timestamp), config.memory_limit);
                const auto consumed = modem::waveform_sample_count(training.size() + packet.consumed_bytes, config);
                const auto finish = begin + decoded.diagnostics.sample_offset + consumed;
                std::lock_guard lock(mutex);
                if (!current.running || generation != version) continue;
                add_signal({signal_id, config.carrier_hz, display_text(packet.message), true, packet_id(packet.message),
                            decoded.diagnostics.snr_db, packet.consumed_bytes, packet.consumed_bytes});
                const auto bytes = packet.message.data.size();
                while (!current.received.empty() &&
                       (current.received.size() >= maximum_events || received_bytes + bytes > config.memory_limit / 4)) {
                    received_bytes -= current.received.front().packet.message.data.size();
                    current.received.erase(current.received.begin());
                }
                if (bytes <= config.memory_limit / 4) {
                    received_bytes += bytes;
                    current.received.push_back({std::move(packet), std::move(decoded.diagnostics), candidate.timestamp});
                } else current.error = "validated content exceeds the continuous receive event budget";
                ring.discard_to(finish); current.buffered_samples = ring.size();
                acquired.reset(); search.clear(); signal_id = 0; last_preview.clear(); last_total = 0;
                next_decode = Clock::now();
            } catch (const Error&) {
                // Noise, an incomplete packet, or an invalid tag is never a
                // validated receive event. A corrupt completed frame releases
                // its acquisition so subsequent candidates remain searchable.
                if (acquired) { acquired.reset(); search.clear(); signal_id = 0; last_preview.clear(); }
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (current.running && generation == version) current.error = exception.what();
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
    if (impl_->queued.size() >= 8 || message.data.size() > impl_->settings.transfer.modem.memory_limit / 4 -
        std::min(impl_->queued_bytes, impl_->settings.transfer.modem.memory_limit / 4))
        throw Error("transmit queue exceeds its memory budget");
    impl_->queued.push_back(message); impl_->queued_bytes += message.data.size();
    impl_->current.transmitting = true; impl_->current.error.clear(); impl_->changed.notify_all();
}
void Session::cancel_transmit() {
    std::lock_guard lock(impl_->mutex);
    impl_->tx_stop.request_stop(); ++impl_->tx_serial;
    impl_->queued.clear(); impl_->queued_bytes = 0; impl_->ready.reset(); impl_->tx_busy = false;
    impl_->current.transmitting = false;
    impl_->current.status = impl_->listening_status;
    impl_->changed.notify_all();
}
Snapshot Session::snapshot() {
    std::lock_guard lock(impl_->mutex);
    auto signals = std::move(impl_->current.signals);
    auto received = std::move(impl_->current.received);
    impl_->current.signals.clear(); impl_->current.received.clear(); impl_->received_bytes = 0;
    auto result = impl_->current;
    result.signals = std::move(signals); result.received = std::move(received);
    return result;
}
void Session::stop() { impl_->halt(); }
}
