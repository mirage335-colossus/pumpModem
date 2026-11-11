#include "datapump/live.hpp"

#include "datapump/execution.hpp"
#include "datapump/received_text.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "search_fft.hpp"
#include "triad_receiver_bulk.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <numbers>
#include <utility>

namespace datapump::live {
namespace {
using namespace triad::v3;
using detail::AudioInput;
using detail::Profile;
using detail::real_pcm;
using Clock = std::chrono::steady_clock;
using KeyTag = std::array<std::uint8_t, 32>;

constexpr std::size_t audio_chunk = 4096, plot_size = 1024, history_size = 32;
// Binding can briefly hold two interval readers. Playback's native provider
// allowance covers its converter, PCM blocks, and the dated PipeWire ring.
constexpr std::size_t lazy_bit_workspace = 64 * 1024U;
constexpr std::size_t playback_provider_workspace = 2 * 1024 * 1024U;
constexpr std::size_t playback_controller_workspace = 2 * 1024 * 1024U;
constexpr auto no_transmitter = "Select Robust to start its session.";
constexpr auto preparing = "Preparing the configured Robust profile.";
constexpr auto duplex_workspace_error = "Robust full-duplex transmit workspace exceeds the receiver memory limit";

long double utc_now() {
    return std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust operation cancelled");
}

HelperBurstCoordinate coordinate(long double utc, std::uint32_t rate) {
    if (!std::isfinite(utc) || utc < 0 || utc >= 4294967296.L)
        throw Error("Robust UTC time is outside the configured calendar range");
    const auto epoch = static_cast<std::uint64_t>(utc);
    const auto phase = std::ceil((utc - epoch) * rate);
    if (phase >= rate) return {epoch + 1, 0};
    return {epoch, static_cast<std::uint32_t>(phase)};
}

long double utc_time(HelperBurstCoordinate position, std::uint32_t rate) {
    return position.epoch + static_cast<long double>(position.phase) / rate;
}

std::uint64_t add_slot(std::uint64_t first, std::uint64_t offset) {
    if (offset > UINT64_MAX - first) throw Error("Robust calendar slot overflow");
    return first + offset;
}

KeyTag tag(const Crypto& key) {
    constexpr std::string_view purpose = "datapump/robust/local-use-history/v1";
    const auto digest = key.mac(std::span(reinterpret_cast<const std::uint8_t*>(purpose.data()), purpose.size()));
    KeyTag result{};
    std::copy_n(digest.begin(), result.size(), result.begin());
    return result;
}

struct TxRequest {
    EmissionMode mode = EmissionMode::Message;
    Bytes input;
    bool force = false, raw = false;
    std::optional<PreparedTextMessage> prepared;

    std::size_t wire_bits() const { return prepared ? prepared->wire_bits() : (raw ? input.size() : 0); }
};

NativeMessageProfile message_profile(const Settings& settings) {
    return {settings.transfer.fec, settings.transfer.compression,
        std::min(settings.transfer.content_limit, native_message_editor_limit), settings.transfer.key};
}

std::size_t checked_workspace(std::size_t left, std::size_t right) {
    if (right > SIZE_MAX - left) throw Error("Robust workspace exceeds its address range");
    return left + right;
}

std::size_t retained_request_workspace(const TxRequest& send) {
    return send.prepared ? checked_workspace(send.prepared->working_bytes(), lazy_bit_workspace) :
        send.input.capacity();
}

std::size_t live_transmit_reservation(const Settings& settings, const Profile& profile) {
    const auto message = message_profile(settings);
    const auto queued = std::max(message.content_limit, transfer::short_message_bits);
    // Half-duplex may hold an accepted input while capture is stopping.
    // Compression starts after capture has joined. Full-duplex covers the
    // larger of codec preparation and the subsequent live waveform phase.
    if (!settings.full_duplex) return queued;
    const auto storage = text_message_transmit_storage(message);
    const auto playback = checked_workspace(profile.waveform.geometry().source_workspace_bytes,
        playback_provider_workspace + playback_controller_workspace + lazy_bit_workspace);
    return std::max(storage.preparation_bytes,
        checked_workspace(std::max(storage.prepared_bytes, queued), playback));
}

std::size_t live_transmit_reservation_checked(const Settings& settings, const Profile& profile) {
    const auto reserved = live_transmit_reservation(settings, profile);
    StreamReceiverOptions options;
    options.memory_limit_bytes = settings.receiver_memory_limit;
    if (reserved >= resolve_stream_receiver_resources(options).memory_limit_bytes)
        throw Error(settings.full_duplex ? duplex_workspace_error :
            "Robust queued transmit input exceeds the receiver memory limit");
    return reserved;
}

EmissionOptions emission_options(const TxRequest& request, const PublicProfile& calendar,
                                 std::uint64_t first, std::uint64_t count,
                                 std::uint64_t source_first, std::stop_token stop) {
    EmissionOptions options{request.mode, first, {}, count, source_first, stop};
    if (request.raw) options.bits = request.input;
    else if (request.prepared) {
        options.bit_at = request.prepared->bit_source(first, [&calendar](std::uint64_t index) {
            return message_address(calendar, index);
        });
    }
    return options;
}

struct HopPosition {
    std::uint64_t dwell = 0, cycle = 0;
    unsigned channel = 0;
    double shift = 0;
};

HopPosition illustrated_hop(const Profile& active, std::uint64_t sample, bool noise) {
    const auto& format = active.waveform.config().format;
    const auto rate = format.sample_rate;
    // Split the absolute UTC coordinate at a two-second integral boundary.
    // Five exact 400 ms dwells fit that interval.
    const auto dwell = format.epoch / 2 * 5 + (format.epoch % 2 * rate + sample) / (rate / 5 * 2);
    const auto count = active.hop_shifts_hz.size();
    const auto cycle = dwell / count;
    const auto key = noise ? Crypto::amateur() : active.key;
    const auto order = key.stream(StreamPurpose::Fhss, format.epoch, cycle * 8, 8);
    std::uint64_t seed = 0;
    for (const auto byte : order) seed = (seed << 8) | byte;
    auto stride = 1 + (seed >> 32) % count;
    while (std::gcd(stride, count) != 1) stride = stride % count + 1;
    const auto channel = static_cast<unsigned>((seed % count + stride * (dwell % count)) % count);
    return {dwell, cycle, channel, active.hop_shifts_hz[channel]};
}

struct PlotFrame {
    std::vector<float> waveform;
    std::vector<Complex> iq;
    std::vector<double> db;
    std::vector<std::complex<float>> spectrum;
    std::uint64_t first = 0;
    std::uint32_t rate = 0;
};

class PlotMeter {
public:
    explicit PlotMeter(std::uint32_t rate) : rate_(rate), fft_(plot_size) {}
    std::optional<PlotFrame> update(std::uint64_t first, std::span<const float> pcm,
                                    std::span<const Complex> iq) {
        // Retain a contiguous recent PCM window. The watermark is actual
        // consumed input; skipped UI frames do not change waveform coordinates.
        for (const auto sample : pcm) {
            pcm_[at_++ % plot_size] = sample;
            fill_ = std::min(fill_ + 1, plot_size);
        }
        const auto now = Clock::now();
        if (fill_ < plot_size || now < next_) return {};
        next_ = now + std::chrono::milliseconds(45);
        PlotFrame result;
        result.rate = rate_;
        const auto end = first + pcm.size();
        result.first = end >= plot_size ? end - plot_size : 0;
        result.waveform.reserve(plot_size);
        std::array<Complex, plot_size> bins{};
        for (std::size_t i = 0; i < plot_size; ++i) {
            const auto sample = pcm_[(at_ + i) % plot_size];
            result.waveform.push_back(sample);
            bins[i] = sample * (.5 - .5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / plot_size));
        }
        modem::detail::search_fft::fft(bins, false, fft_);
        for (const auto value : std::span(bins).first(plot_size / 2 + 1)) {
            const auto normalized = value * (2. / plot_size);
            result.spectrum.emplace_back(normalized);
            result.db.push_back(10 * std::log10(std::max(1e-24, std::norm(normalized))));
        }
        const auto take = std::min<std::size_t>(512, iq.size());
        result.iq.reserve(take);
        for (std::size_t i = 0; i < take; ++i)
            result.iq.push_back(iq[i * iq.size() / take]);
        return result;
    }
private:
    std::uint32_t rate_;
    modem::detail::search_fft::DoublePlan fft_;
    std::array<float, plot_size> pcm_{};
    std::size_t at_ = 0, fill_ = 0;
    Clock::time_point next_{};
};

PatternStep pattern_step(const StreamSlotReceipt& receipt) {
    const auto& s = receipt.score;
    PatternStep row;
    row.index = s.index;
    row.epoch = receipt.pattern_epoch;
    row.ordinal = receipt.symbol_ordinal;
    row.decision = s.decision == SlotDecision::data ? PatternDecision::data :
        s.decision == SlotDecision::quiet ? PatternDecision::quiet : PatternDecision::ambiguous;
    row.bit = s.bit;
    row.bit_evidence = s.bit_evidence;
    row.adaptive_bit_evidence = s.adaptive_bit_evidence;
    row.combined_bit_evidence = s.combined_bit_evidence;
    row.quiet_energy = s.quiet_energy;
    row.quiet_threshold = s.quiet_threshold;
    row.operational_log_e = s.operational_log_e;
    row.operational_threshold = s.operational_threshold;
    row.helper_seconds = s.helper_seconds;
    row.coarse_seconds = s.coarse_seconds;
    row.fine_seconds = s.fine_seconds;
    row.quiet_modes = s.noise_statistics_valid ? s.quiet_modes : 0;
    row.fine_candidates = s.fine_candidates;
    row.workers = receipt.workers;
    row.evidence_complete = s.complete && s.all_valid && s.zero_h0_verified && s.fine_candidates > 0;
    row.helper_status = s.helper_status;
    row.reason = s.reason;
    return row;
}

std::string message_id(Message& message, std::uint64_t id) {
    constexpr std::string_view prefix = "ROBST001";
    std::copy(prefix.begin(), prefix.end(), message.local_id.begin());
    for (unsigned i = 0; i < 8; ++i)
        message.local_id[8 + i] = static_cast<std::uint8_t>(id >> (56 - 8 * i));
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : message.local_id) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

StreamReceiverOptions stream_options(const Settings& settings, std::size_t external_memory) {
    StreamReceiverOptions options;
    options.memory_limit_bytes = settings.receiver_memory_limit;
    const auto resources = resolve_stream_receiver_resources(options);
    // Keep one bounded completed GUI receipt outside the stream's own result
    // queue. Capture backpressure resumes when the UI consumes that receipt.
    const auto presentation = message_profile(settings).content_limit + 64 * 1024U;
    if (external_memory > SIZE_MAX - presentation)
        throw Error("Robust presentation workspace exceeds its address range");
    external_memory += presentation;
    if (external_memory >= resources.memory_limit_bytes)
        throw Error("Robust input and channel workspace exceed the receiver memory limit");
    options.workers = resources.workers;
    options.slot_owners = resources.slot_owners;
    options.memory_limit_bytes = resources.memory_limit_bytes - external_memory;
    options.maximum_input_chunk_samples = audio_chunk;
    options.maximum_pending_messages = 1;
    return options;
}

void receiver_ok(const StreamReceiverReport& report) {
    if (report.state == StreamReceiverState::resource_limit || report.state == StreamReceiverState::failed)
        throw Error(report.reason.empty() ? "Robust receiver could not admit the configured public window" : report.reason);
}

struct CaptureChunk {
    std::vector<float> pcm;
    std::optional<audio::TimePrediction> timestamp;
    std::uint64_t first = 0;
};

struct CaptureQueue {
    execution::Mutex mutex;
    execution::StopCondition changed;
    std::deque<CaptureChunk> chunks;
    std::optional<audio::TimePrediction> timestamp;
    std::uint64_t next_sample = 0;
    std::size_t queued_samples = 0, provider_bytes = 0;
    bool done = false;
    std::string error;
    void fail(std::string text) {
        std::lock_guard lock(mutex);
        if (error.empty()) error = std::move(text);
        changed.notify_all();
    }
};

struct ReceiveDisplay {
    std::optional<std::uint64_t> pending_slot;
    std::uint64_t pending_id = 0, revision = 0;
    std::size_t observed_bits = 0, missing = 0;
};

} // namespace

struct Session::Impl {
    mutable execution::Mutex mutex;
    execution::StopCondition changed;
    execution::Task manager;
    std::stop_source generation_stop, tx_stop;
    Settings settings;
    Snapshot current;
    std::vector<SignalUpdate> signal_events;
    std::vector<transfer::Received> received_events;
    std::shared_ptr<const Profile> profile;
    std::optional<TxRequest> request;
    std::uint64_t generation = 0, next_id = 1, spectrum_revision = 0;
    std::uint64_t plot_segment = 0;
    std::uint64_t next_simulation_ordinal = 0, last_simulation_epoch = 0;
    bool started = false, suspended = false, stopping = false, stopped = false, resources_owned = false;
    std::map<KeyTag, long double> used_until;
    long double separated_after = 0;

    ~Impl() {
        manager.request_stop();
        generation_stop.request_stop();
        tx_stop.request_stop();
        changed.notify_all();
        if (manager.joinable()) manager.join();
    }

    template<class F>
    bool publish(std::uint64_t owner, std::stop_token stop, F&& update) {
        std::lock_guard lock(mutex);
        if (owner != generation || stop.stop_requested() || stopping || suspended) return false;
        update(current);
        return true;
    }

    std::uint64_t new_plot_segment() {
        std::lock_guard lock(mutex);
        return ++plot_segment;
    }

    void set_plot(std::uint64_t owner, std::stop_token stop, PlotFrame frame, bool receiver_input,
                  std::uint64_t segment) {
        publish(owner, stop, [&](Snapshot& out) {
            out.waveform = std::move(frame.waveform);
            out.waveform_sample_rate = frame.rate;
            out.spectrum_db = std::move(frame.db);
            out.spectrum_complex = std::move(frame.spectrum);
            out.spectrum_start_sample = frame.first;
            out.spectrum_segment = segment;
            out.spectrum_sample_count = plot_size;
            out.spectrum_revision = ++spectrum_revision;
            out.spectrum_bin_hz = static_cast<double>(frame.rate) / plot_size;
            if (receiver_input) {
                out.constellation = std::move(frame.iq);
                out.constellation_source = ConstellationSource::input;
            }
        });
    }

    void trace_hop(const Profile& active, std::uint64_t sample, bool noise,
                   std::uint64_t owner, std::stop_token stop) {
        if (active.hop_shifts_hz.empty()) return;
        const auto hop = illustrated_hop(active, sample, noise);
        publish(owner, stop, [&](Snapshot& out) {
            auto& trace = out.transmit_trace;
            if (trace.fhss_available && trace.fhss_hop == hop.dwell) return;
            trace.fhss = trace.fhss_available = trace.fhss_utc_calendar = true;
            trace.fhss_epoch = active.waveform.origin().epoch;
            trace.fhss_hop = hop.dwell;
            trace.fhss_cycle = hop.cycle;
            trace.fhss_channel = hop.channel;
            trace.fhss_channels = static_cast<unsigned>(active.hop_shifts_hz.size());
            trace.fhss_shift_hz = hop.shift;
            trace.fhss_dwell_seconds = active.waveform.config().format.dwell_seconds;
            ++trace.revision;
        });
    }

    void prepare_send(TxRequest& send, const Settings& configuration, std::uint64_t owner,
                      std::stop_token stop) {
        cancelled(stop);
        if (send.mode != EmissionMode::Message || send.raw) return;
        // Bind authenticated intervals lazily after selecting the actual
        // calendar slot. The encoder scratch is gone before waveform emission.
        const auto profile = message_profile(configuration);
        send.prepared.emplace(prepare_text_message(send.input, profile));
        Bytes{}.swap(send.input);
        if (send.prepared->working_bytes() > text_message_transmit_storage(profile).prepared_bytes)
            throw Error("Robust prepared text exceeded its workspace bound");
        cancelled(stop);
        publish(owner, stop, [&](Snapshot& out) {
            auto& trace = out.transmit_trace;
            trace.total_wire_bits = send.wire_bits();
            trace.short_text = !send.prepared->long_form();
            ++trace.revision;
        });
    }

    void trace_bound_bits(const EmissionOptions& emission, std::uint64_t owner, std::stop_token stop) {
        if (!emission.bit_at) return;
        cancelled(stop);
        Bytes prefix;
        const auto count = std::min<std::uint64_t>(emission.slot_count, modem::TransmitTrace::bit_limit);
        prefix.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) prefix.push_back(emission.bit_at(i));
        publish(owner, stop, [&](Snapshot& out) {
            out.transmit_trace.wire_plain_bits = std::move(prefix);
            ++out.transmit_trace.revision;
        });
    }

    Emission make_emission(const TxRequest& send, const Profile& profile, std::uint64_t first,
                           std::uint64_t count, std::uint64_t source_first, std::uint64_t owner,
                           std::stop_token stop) {
        auto options = emission_options(send, profile.waveform, first, count, source_first, stop);
        trace_bound_bits(options, owner, stop);
        return Emission(profile.waveform, profile.key, options);
    }

    void append_signal(SignalUpdate signal) {
        const auto same = std::find_if(signal_events.begin(), signal_events.end(),
            [&](const auto& old) { return old.id == signal.id; });
        if (same != signal_events.end()) {
            *same = std::move(signal);
            return;
        }
        if (signal_events.size() == history_size) signal_events.erase(signal_events.begin());
        signal_events.push_back(std::move(signal));
    }

    void deliver(CompleteStreamReceiver& receiver, ReceiveDisplay& display, const Settings& s,
                 std::uint64_t owner, std::stop_token stop) {
        auto slots = receiver.drain_recent_slots();
        if (receiver.report().pending_messages) {
            std::unique_lock lock(mutex);
            changed.wait(lock, stop, [&] {
                return owner != generation || suspended || stopping || received_events.empty();
            });
            if (stop.stop_requested() || owner != generation || suspended || stopping) return;
        }
        auto messages = receiver.drain_messages();
        const auto report = receiver.report();
        if (slots.empty() && messages.empty()) return;
        publish(owner, stop, [&](Snapshot& out) {
            for (const auto& row : slots) {
                if (out.pattern_steps.size() == history_size) out.pattern_steps.erase(out.pattern_steps.begin());
                out.pattern_steps.push_back(pattern_step(row));
            }
            for (auto& event : messages) {
                auto& message = event.message;
                if (!message.physical_complete) continue;
                const bool matched = display.pending_slot == event.first_public_slot;
                SignalUpdate signal;
                signal.id = matched ? display.pending_id : next_id++;
                signal.revision = matched ? display.revision + 1 : 1;
                signal.frequency_hz = s.transfer.modem.carrier_hz;
                signal.complete = true;
                signal.received_bits = message.observed_bits;
                signal.missing_symbols = message.missing_slots;
                // Raw presentation requires an exact physical bit sequence.
                if (message.raw_bits_complete())
                    for (const auto bit : message.received_bits) signal.raw_bits.push_back(bit ? '1' : '0');
                if (message.decoded_content) {
                    transfer::Received received;
                    received.content = std::move(*message.decoded_content);
                    signal.reception_id = message_id(received.content.message, signal.id);
                    const auto& payload = received.content.message.data;
                    signal.text = received_text(std::span(payload).first(std::min<std::size_t>(4096, payload.size())));
                    signal.validated = true;
                    signal.fec_stats = received.content.fec_stats;
                    signal.pre_fec_accuracy = received.content.pre_fec_accuracy;
                    if (message.raw_bits_complete()) received.raw_bits = std::move(message.received_bits);
                    received.observed_bits = signal.received_bits;
                    received.missing_symbols = signal.missing_symbols;
                    received.stream_complete = true;
                    received.content_validated = true;
                    received.short_text_decoded = !message.long_form;
                    received_events.push_back(std::move(received));
                } else {
                    signal.binary = true;
                    signal.text = signal.raw_bits.empty() ? "Incomplete Robust content" : signal.raw_bits;
                }
                append_signal(std::move(signal));
                if (matched) display = {};
            }
            if (!report.episode_first_public_slot || !report.observed_bits) return;
            const bool existing = display.pending_slot == report.episode_first_public_slot;
            if (existing && display.observed_bits == report.observed_bits && display.missing == report.erasures)
                return;
            if (!existing) {
                display = {};
                display.pending_slot = report.episode_first_public_slot;
                display.pending_id = next_id++;
            }
            display.observed_bits = report.observed_bits;
            display.missing = report.erasures;
            SignalUpdate signal;
            signal.id = display.pending_id;
            signal.revision = ++display.revision;
            signal.frequency_hz = s.transfer.modem.carrier_hz;
            signal.received_bits = display.observed_bits;
            signal.missing_symbols = display.missing;
            signal.text = report.recovering ? "Robust episode rejected; listening for two quiet slots" : "Receiving Robust content";
            append_signal(std::move(signal));
        });
    }

    void accept(CompleteStreamReceiver& receiver, ReceiveDisplay& display, const Settings& s,
                std::uint64_t first, std::span<const Complex> samples, std::span<const std::uint8_t> validity,
                std::uint64_t owner, std::stop_token stop) {
        while (!stop.stop_requested()) {
            receiver_ok(receiver.report());
            if (receiver.push(first, samples, validity) == StreamPushResult::accepted) break;

            // Observe the actual rejection cause after push(): it may have
            // committed work before reporting backpressure. Drain queued
            // output first, then wait for only one owner completion before
            // retrying this unchanged chunk.
            const auto rejected = receiver.report();
            receiver_ok(rejected);
            deliver(receiver, display, s, owner, stop);
            auto after = receiver.report();
            if (!rejected.output_backpressured) {
                after = receiver.wait_for_slot_completion();
                receiver_ok(after);
                deliver(receiver, display, s, owner, stop);
                after = receiver.report();
            }
            const bool output_drained = rejected.output_backpressured && !after.output_backpressured;
            if (!output_drained && after.state == rejected.state && !after.active_owners && !after.output_backpressured &&
                after.next_public_slot == rejected.next_public_slot &&
                after.uncommitted_slots == rejected.uncommitted_slots)
                throw Error("Robust input cannot advance within the configured receiver workspace");
        }
        cancelled(stop);
        receiver_ok(receiver.resume(false));
        deliver(receiver, display, s, owner, stop);
    }

    void finish_input(CompleteStreamReceiver& receiver, ReceiveDisplay& display, const Settings& s,
                      std::uint64_t owner, std::stop_token stop) {
        for (;;) {
            cancelled(stop);
            const auto progress = receiver.finish_input();
            receiver_ok(progress);
            deliver(receiver, display, s, owner, stop);
            if (progress.state != StreamReceiverState::receiving) return;
            if (!progress.output_backpressured)
                throw Error("Robust receiver did not drain its complete public windows");
        }
    }

    void invalidate_pending(ReceiveDisplay& display, const Settings& s, std::uint64_t owner) {
        if (!display.pending_slot) return;
        publish(owner, {}, [&](Snapshot&) {
            SignalUpdate signal;
            signal.id = display.pending_id;
            signal.revision = ++display.revision;
            signal.frequency_hz = s.transfer.modem.carrier_hz;
            signal.received_bits = display.observed_bits;
            signal.missing_symbols = display.missing;
            signal.text = "Robust input ended before physical completion";
            append_signal(std::move(signal));
        });
        display = {};
    }

    void receive_live(const Profile& base, const Settings& s, std::uint64_t owner, std::stop_token stop) {
        const auto rate = s.transfer.modem.sample_rate;
        const auto transmit_reservation = live_transmit_reservation_checked(s, base);
        AudioInput input(rate, base.receiver.data_if_hz, base.minimum_audio_hz, base.maximum_audio_hz);
        PlotMeter meter(rate);
        const auto segment = new_plot_segment();
        CaptureQueue queue;
        const auto queue_limit = static_cast<std::size_t>(rate) * 2;
        execution::Task capture([&](std::stop_token capture_stop) {
            try {
                audio::Options options;
                options.exclusive = s.exclusive;
                options.request_capture_timing = true;
                options.clock_timing = audio::ClockTimingPolicy{
                    base.waveform.config().audio_pair_ppm * .5e-6, .001, 0};
                options.capture_timing = [&](const auto& timestamp) {
                    std::lock_guard lock(queue.mutex);
                    queue.timestamp = timestamp;
                };
                options.capture_discontinuity = [&] { queue.fail("Robust capture lost dated sample continuity"); };
                audio::capture(rate, s.device, [&](std::span<const float> samples) {
                    std::lock_guard lock(queue.mutex);
                    if (!queue.error.empty() || stop.stop_requested()) return false;
                    if (samples.size() > queue_limit - queue.queued_samples ||
                        queue.chunks.size() >= 256) {
                        queue.error = "Robust input backlog overrun; capture stopped with missing samples";
                        queue.changed.notify_all();
                        return false;
                    }
                    if (samples.size() > UINT64_MAX - queue.next_sample) {
                        queue.error = "Robust capture sample coordinate overflow";
                        queue.changed.notify_all();
                        return false;
                    }
                    queue.chunks.push_back({{samples.begin(), samples.end()},
                                           std::exchange(queue.timestamp, {}), queue.next_sample});
                    queue.queued_samples += samples.size();
                    queue.next_sample += samples.size();
                    queue.changed.notify_all();
                    return true;
                }, capture_stop, [&](const audio::StreamFormat& format) {
                    if (format.usable_passband_hz < base.maximum_audio_hz)
                        throw Error("Robust receive band exceeds the selected device passband");
                    std::lock_guard lock(queue.mutex);
                    queue.provider_bytes = format.workspace_bytes;
                }, std::move(options));
            } catch (const std::exception& error) {
                if (!capture_stop.stop_requested() && !stop.stop_requested()) queue.fail(error.what());
            }
            {
                std::lock_guard lock(queue.mutex);
                queue.done = true;
            }
            queue.changed.notify_all();
        });
        std::stop_callback stop_capture(stop, [&] { capture.request_stop(); queue.changed.notify_all(); });
        std::optional<audio::TimePrediction> origin_stamp;
        std::int64_t capture_first = 0;
        std::uint64_t observation_first = 0;
        std::unique_ptr<CompleteStreamReceiver> receiver;
        // The stop callback that refers to receiver is installed after its
        // construction and is destroyed before the receiver itself.
        std::optional<std::stop_callback<std::function<void()>>> interrupt;
        ReceiveDisplay display;
        std::vector<Complex> iq;
        iq.reserve(audio_chunk);
        try {
            while (!stop.stop_requested()) {
                CaptureChunk chunk;
                std::size_t provider_bytes = 0;
                {
                    std::unique_lock lock(queue.mutex);
                    queue.changed.wait_for(lock, stop, std::chrono::milliseconds(30),
                        [&] { return queue.done || !queue.error.empty() || !queue.chunks.empty(); });
                    if (stop.stop_requested()) break;
                    if (!queue.error.empty()) throw Error(queue.error);
                    if (!queue.chunks.empty()) {
                        chunk = std::move(queue.chunks.front());
                        queue.chunks.pop_front();
                        queue.queued_samples -= chunk.pcm.size();
                        provider_bytes = queue.provider_bytes;
                    } else if (queue.done) {
                        if (receiver) {
                            // Only already accepted complete windows can be
                            // scored. EOF never invents absent waveforms.
                            lock.unlock();
                            finish_input(*receiver, display, s, owner, stop);
                        }
                        break;
                    }
                }
                if (chunk.pcm.empty()) {
                    if (receiver) {
                        receiver_ok(receiver->resume(false));
                        deliver(*receiver, display, s, owner, stop);
                    }
                    continue;
                }
                if (!chunk.timestamp || chunk.timestamp->quality == audio::TimingQuality::unavailable)
                    throw Error("This audio device did not provide dated capture for the configured Robust calendar");
                const auto& stamp = *chunk.timestamp;
                if (!std::isfinite(stamp.utc_seconds) || !std::isfinite(stamp.uncertainty_seconds) ||
                    !std::isfinite(stamp.seconds_per_frame) || stamp.seconds_per_frame <= 0 ||
                    !std::isfinite(stamp.rate_uncertainty_fraction) || stamp.uncertainty_seconds < 0 ||
                    stamp.rate_uncertainty_fraction < 0)
                    throw Error("Invalid Robust input timestamp");
                const auto device_scale = 1. / (rate * stamp.seconds_per_frame);
                const auto clock_envelope = base.waveform.config().audio_pair_ppm * .5e-6;
                if (std::abs(device_scale - 1.) > clock_envelope + stamp.rate_uncertainty_fraction)
                    throw Error("The dated Robust input contradicts the configured Base Oscillator rate");
                if (!origin_stamp) {
                    origin_stamp = stamp;
                    const auto age = stamp.utc_seconds - base.waveform.origin().epoch;
                    if (stamp.utc_seconds < 0 || stamp.utc_seconds >= 4294967296.L)
                        throw Error("Robust input timestamp is outside the configured calendar");
                    const auto epoch_ambiguity = std::abs(age) * base.waveform.config().audio_pair_ppm * 1e-6L;
                    const auto adjusted = detail::with_capture_uncertainty(base, stamp.uncertainty_seconds + 1. / rate,
                        static_cast<double>(epoch_ambiguity), input.halo_samples());
                    capture_first = std::llround((age + adjusted.waveform.config().capture_lead_seconds) * rate);
                    const auto valid_first = capture_first + static_cast<std::int64_t>(input.first_valid_sample());
                    const auto earliest = coordinate(std::max<long double>(stamp.utc_seconds, adjusted.waveform.origin().epoch), rate);
                    auto slot = adjusted.waveform.first_slot_not_before(earliest);
                    auto first = adjusted.waveform.slot(slot).observation.first;
                    if (valid_first > 0 && first < static_cast<std::uint64_t>(valid_first)) {
                        auto high = adjusted.waveform.last_slot();
                        while (slot < high) {
                            const auto middle = slot + (high - slot) / 2;
                            if (adjusted.waveform.slot(middle).observation.first < static_cast<std::uint64_t>(valid_first))
                                slot = middle + 1;
                            else high = middle;
                        }
                        first = adjusted.waveform.slot(slot).observation.first;
                        if (first < static_cast<std::uint64_t>(valid_first))
                            throw Error("No fully observable future slot remains in the configured Robust calendar");
                    }
                    observation_first = first;
                    constexpr auto overhead = 2 * 1024 * 1024U;
                    const auto ingress_workspace = overhead + queue_limit * sizeof(float) + input.working_bytes();
                    auto options = stream_options(s, checked_workspace(transmit_reservation,
                        checked_workspace(provider_bytes, ingress_workspace)));
                    options.first_slot = slot;
                    options.first_input_sample = first;
                    options.require_quiet_rearm = slot > 0;
                    receiver = std::make_unique<CompleteStreamReceiver>(adjusted.waveform, adjusted.receiver,
                        adjusted.key, options, message_profile(s));
                    interrupt.emplace(stop, [owned = receiver.get()] { owned->request_cancel(); });
                    publish(owner, stop, [&](Snapshot& out) {
                        out.receiver_error.clear();
                        if (!out.transmitting) out.status = stamp.quality == audio::TimingQuality::bounded
                            ? "Listening on the configured Robust calendar"
                            : "Listening on the configured Robust calendar with estimated audio dates";
                    });
                } else {
                    // A configured oscillator is an assumption, not license
                    // to glue a discontinuous timeline back onto old samples.
                    const auto seconds = static_cast<long double>(chunk.first) / rate;
                    const auto expected = origin_stamp->utc_seconds + seconds;
                    const auto allowance = origin_stamp->uncertainty_seconds + stamp.uncertainty_seconds +
                        seconds * base.waveform.config().audio_pair_ppm * .5e-6L + 2.L / rate;
                    if (std::abs(stamp.utc_seconds - expected) > allowance)
                        throw Error("Robust input timestamps no longer fit the configured oscillator and capture date");
                }
                const auto first_local = input.project(chunk.pcm, iq);
                if (auto frame = meter.update(chunk.first, chunk.pcm, iq))
                    set_plot(owner, stop, std::move(*frame), true, segment);
                if (iq.empty()) continue;
                const auto first = capture_first + static_cast<std::int64_t>(first_local);
                const auto signed_stop = first + static_cast<std::int64_t>(iq.size());
                if (signed_stop <= 0 || static_cast<std::uint64_t>(signed_stop) <= observation_first) continue;
                const auto skip = first >= 0 && static_cast<std::uint64_t>(first) >= observation_first ? 0 :
                    static_cast<std::size_t>(static_cast<std::int64_t>(observation_first) - first);
                const auto supplied = static_cast<std::uint64_t>(first + static_cast<std::int64_t>(skip));
                for (std::size_t at = skip; at < iq.size(); at += audio_chunk)
                    accept(*receiver, display, s, supplied + at - skip,
                        std::span(iq).subspan(at, std::min(audio_chunk, iq.size() - at)), {}, owner, stop);
            }
        } catch (...) {
            if (receiver) receiver->request_cancel();
            invalidate_pending(display, s, owner);
            throw;
        }
        if (receiver && stop.stop_requested()) receiver->request_cancel();
        invalidate_pending(display, s, owner);
    }

    Calendar simulation_calendar(const Settings& s) {
        const auto now = static_cast<std::uint64_t>(std::floor(utc_now()));
        const auto selected = s.calendar.value_or(Calendar{now, 0});
        return {std::max(selected.epoch, last_simulation_epoch),
                std::max(selected.initial_ordinal, next_simulation_ordinal)};
    }

    void simulate(const Profile& prepared, const Settings& s, const TxRequest& send,
                  std::uint64_t owner, std::stop_token stop) {
        const auto rate = s.transfer.modem.sample_rate;
        const auto& calendar = prepared.waveform;
        const auto noise = send.mode == EmissionMode::Noise;
        const auto wire_bits = send.wire_bits();
        const auto slots = noise ? add_slot(calendar.last_slot(), 1) :
            send.mode == EmissionMode::Message ? wire_bits : std::size_t{1};
        const auto end_slot = noise ? std::uint64_t{0} : add_slot(slots, 1);
        if (!noise && end_slot > calendar.last_slot())
            throw Error("The configured Robust calendar has no room for the draft and two quiet slots");
        auto emission = make_emission(send, prepared, 0, slots, 0, owner, stop);
        auto config = prepared.channel;
        if (!noise) config.minimum_capture_samples = calendar.slot(end_slot).observation.stop;
        const auto source = emission.source();
        const auto source_stop = source.geometry.source_samples;
        StreamingChannel channel(source, config);
        std::unique_ptr<CompleteStreamReceiver> receiver;
        if (!noise) {
            // Channel working bytes include the waveform source but exclude
            // the prepared text shared by its lazy interval reader.
            const auto options = stream_options(s, checked_workspace(channel.report().working_bytes,
                checked_workspace(retained_request_workspace(send), 2 * 1024 * 1024U)));
            receiver = std::make_unique<CompleteStreamReceiver>(calendar, prepared.receiver,
                prepared.key, options, message_profile(s));
        }
        std::stop_callback interrupt(stop, [&] { if (receiver) receiver->request_cancel(); });
        std::array<Complex, audio_chunk> iq{};
        std::array<std::uint8_t, audio_chunk> valid{};
        std::array<float, audio_chunk> pcm{};
        PlotMeter meter(rate);
        const auto segment = new_plot_segment();
        ReceiveDisplay display;
        auto tx_slot = calendar.transmit_slot(0);
        std::size_t generated_bits = 0;
        const auto began = Clock::now();
        bool scoring_tail = false;
        try {
            while (!stop.stop_requested()) {
                execution::checkpoint();
                const auto first = channel.position();
                const auto count = channel.read(iq, valid);
                if (!count) break;
                const auto samples = std::span(iq).first(count);
                for (std::size_t i = 0; i < count; ++i)
                    pcm[i] = real_pcm(samples[i], first + i, prepared.receiver.data_if_hz, rate);
                if (auto frame = meter.update(first, std::span(pcm).first(count), samples)) {
                    if (emission.report().generated_samples)
                        trace_hop(prepared, emission.report().generated_samples - 1, noise, owner, stop);
                    set_plot(owner, stop, std::move(*frame), true, segment);
                }
                if (receiver) accept(*receiver, display, s, first, samples, std::span(valid).first(count), owner, stop);
                if (send.mode == EmissionMode::Message) {
                    const auto generated = emission.report().generated_samples;
                    while (generated_bits < wire_bits &&
                           generated >= calendar.calendar().distance(calendar.origin(), tx_slot.support_end)) {
                        ++generated_bits;
                        if (generated_bits < wire_bits) tx_slot = calendar.transmit_slot(generated_bits);
                    }
                    publish(owner, stop, [&](Snapshot& out) {
                        if (out.transmit_trace.generated_bits != generated_bits) {
                            out.transmit_trace.generated_bits = generated_bits;
                            ++out.transmit_trace.revision;
                        }
                    });
                }
                if (!noise && !scoring_tail && emission.report().generated_samples >= source_stop) {
                    scoring_tail = true;
                    publish(owner, stop, [](Snapshot& out) { out.status = "Scoring Robust reception after simulated transmission"; });
                }
                if (noise) {
                    // An open-ended noise command is played at its physical rate
                    // so its source/calendar does not race ahead of the user.
                    const auto due = began + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(static_cast<double>(channel.position()) / rate));
                    while (!stop.stop_requested() && Clock::now() < due)
                        execution::sleep_until(std::min(due, Clock::now() + std::chrono::milliseconds(30)));
                }
            }
            cancelled(stop);
            if (receiver) {
                finish_input(*receiver, display, s, owner, stop);
                invalidate_pending(display, s, owner);
            }
        } catch (...) {
            if (receiver) receiver->request_cancel();
            invalidate_pending(display, s, owner);
            throw;
        }
    }

    std::uint64_t earliest_tx_slot(const Profile& active, long double minimum_lead) const {
        return active.waveform.first_slot_not_before(coordinate(utc_now() + minimum_lead,
            active.waveform.config().format.sample_rate));
    }

    bool overlap_locked(const Profile& active, std::uint64_t index, EmissionMode mode) const {
        const auto slot = active.waveform.transmit_slot(index);
        const auto rate = active.waveform.config().format.sample_rate;
        const bool helper = active.receiver.helper_enabled;
        if (mode == EmissionMode::Noise && !helper) return false;
        if (mode != EmissionMode::Noise && !helper && !active.waveform.config().format.encrypt) return false;
        const auto key = mode == EmissionMode::Noise ? Crypto::amateur() : active.key;
        const auto previous = used_until.find(tag(key));
        if (previous == used_until.end()) return false;
        const auto beginning = helper ? utc_time(slot.helper_begin, rate) : utc_time(slot.pattern_begin, rate);
        return beginning <= previous->second;
    }

    void reserve_key(const Profile& active, std::uint64_t index, EmissionMode mode) {
        const auto& calendar = active.waveform;
        const auto slot = calendar.transmit_slot(index);
        const auto rate = calendar.config().format.sample_rate;
        const bool helper = active.receiver.helper_enabled;
        const bool keyed = helper || (mode != EmissionMode::Noise && calendar.config().format.encrypt);
        if (keyed) {
            const auto key = mode == EmissionMode::Noise ? Crypto::amateur() : active.key;
            const auto identity = tag(key);
            if (used_until.size() >= 128 && !used_until.contains(identity))
                throw Error("Robust transmit history is full; start a new application session before selecting another key");
            // A first generated sample can already enter the device queue.
            // Retain that reservation through cancellation or profile changes.
            auto& used = used_until[identity];
            const auto through = helper ? slot.helper_end : slot.pattern_begin;
            used = std::max(used, utc_time(through, rate));
        }
        const auto following = add_slot(index, 3);
        separated_after = following <= calendar.last_slot()
            ? std::max(separated_after, utc_time(calendar.transmit_slot(following).helper_begin, rate))
            : 4294967296.L;
    }

    void play_live(const Profile& active, const Settings& s, const TxRequest& send,
                   std::uint64_t owner, std::stop_token stop) {
        if (!audio::utc_follow_supported())
            throw Error("The selected audio provider cannot schedule the configured Robust calendar");
        const auto rate = s.transfer.modem.sample_rate;
        const auto& calendar = active.waveform;
        const auto noise = send.mode == EmissionMode::Noise;
        const auto wire_bits = send.wire_bits();
        std::optional<Emission> emission;
        std::uint64_t first_slot = 0, last_slot = 0, source_first = 0, next_reservation = 0;
        std::size_t generated_bits = 0;
        bool reserved_final = false;
        PlotMeter meter(rate);
        const auto segment = new_plot_segment();
        std::array<Complex, audio_chunk> iq{};
        double pcm_scale = 0;
        audio::Options options;
        options.transmit_gain = s.transmit_gain;
        options.exclusive = s.exclusive;
        options.follow_system_clock = true;
        const auto lag = s.transfer.audio_timing_error_seconds;
        options.presentation_lag_seconds = lag / 2;
        options.maximum_utc_error_seconds = lag / 2;
        options.clock_timing = audio::ClockTimingPolicy{
            active.waveform.config().audio_pair_ppm * .5e-6, .001,
            s.transfer.clock_sync ? s.transfer.clock_sync->accuracy_seconds : 0
        };
        options.playback_discontinuity = [](const std::string& reason) {
            throw Error("Robust playback lost sample continuity: " + reason);
        };
        options.timing_status = [&](const auto& report) {
            publish(owner, stop, [&](Snapshot& out) { out.status = report.reason; });
        };
        audio::playback(rate, s.device, [&](std::span<float> out) {
            cancelled(stop);
            if (!emission) throw Error("Robust audio requested samples before calendar preparation");
            const auto begin = emission->report().generated_samples;
            const auto count = emission->read(std::span(iq).first(std::min(iq.size(), out.size())));
            const auto end = source_first + begin + count;
            while (count && !reserved_final) {
                const auto candidate = calendar.transmit_slot(next_reservation);
                const auto edge = calendar.calendar().distance(calendar.origin(),
                    active.receiver.helper_enabled ? candidate.helper_begin : candidate.support_begin);
                if (end <= edge) break;
                {
                    std::lock_guard lock(mutex);
                    reserve_key(active, next_reservation, send.mode);
                }
                if (next_reservation == last_slot) reserved_final = true;
                else ++next_reservation;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const auto pcm = real_pcm(iq[i], source_first + begin + i, active.receiver.data_if_hz, rate, pcm_scale);
                if (!std::isfinite(pcm) || std::abs(pcm) * s.transmit_gain > .95)
                    throw Error("Robust playback exceeded calibrated PCM headroom");
                out[i] = pcm;
            }
            if (auto frame = meter.update(source_first + begin, out.first(count), {})) {
                if (count) trace_hop(active, end - 1, noise, owner, stop);
                if (!s.full_duplex || !s.live_noise_dbfs_hz)
                    set_plot(owner, stop, std::move(*frame), false, segment);
            }
            if (send.mode == EmissionMode::Message) {
                while (generated_bits < wire_bits && end >= calendar.calendar().distance(calendar.origin(),
                        calendar.transmit_slot(first_slot + generated_bits).support_end))
                    ++generated_bits;
                publish(owner, stop, [&](Snapshot& snapshot) {
                    if (snapshot.transmit_trace.generated_bits != generated_bits) {
                        snapshot.transmit_trace.generated_bits = generated_bits;
                        ++snapshot.transmit_trace.revision;
                    }
                });
            }
            return count;
        }, stop, [&](const audio::StreamFormat& format) {
            if (format.usable_passband_hz < active.maximum_audio_hz)
                throw Error("Robust transmit band exceeds the selected device passband");
            if (s.full_duplex && s.live_noise_dbfs_hz &&
                format.workspace_bytes > playback_provider_workspace)
                throw Error("Robust playback provider exceeded the full-duplex audio workspace allowance");
            if (emission) return;
            // The provider has opened and completed timing preflight. Allow
            // bounded source preparation before the scheduled first sample.
            first_slot = earliest_tx_slot(active, audio::minimum_lead_seconds() + 3);
            {
                std::lock_guard lock(mutex);
                if (!send.force && overlap_locked(active, first_slot, send.mode))
                    throw Error("Robust transmit locked because this key has already used that address");
                const auto first = calendar.transmit_slot(first_slot);
                if (!send.force && utc_time(first.helper_begin, rate) < separated_after)
                    throw Error("Robust transmit is waiting for its two quiet public slots");
            }
            const auto slots = noise ? calendar.last_slot() - first_slot + 1 :
                send.mode == EmissionMode::Message ? wire_bits : std::size_t{1};
            last_slot = add_slot(first_slot, slots - 1);
            if (!noise && add_slot(last_slot, 2) > calendar.last_slot())
                throw Error("The configured Robust calendar has no room for two quiet slots after this draft");
            next_reservation = first_slot;
            const auto first = calendar.transmit_slot(first_slot);
            source_first = calendar.calendar().distance(calendar.origin(), first.helper_begin);
            emission.emplace(make_emission(send, active, first_slot, slots, source_first, owner, stop));
            if (s.full_duplex && s.live_noise_dbfs_hz &&
                emission->source().working_bytes > checked_workspace(
                    calendar.geometry().source_workspace_bytes, playback_controller_workspace))
                throw Error("Robust waveform exceeded its full-duplex source workspace allowance");
            const auto& report = emission->report();
            const auto helper_peak = report.helper_plan.helper_gain * report.composite_gain *
                (helper::pilot_amplitude_fraction + helper::low_pilot_amplitude_fraction + helper::auxiliary_amplitude_fraction);
            const auto peak = active.maximum_unscaled_data * report.data_gain + helper_peak;
            pcm_scale = .5 / (std::sqrt(2.) * peak);
            if (!std::isfinite(pcm_scale) || pcm_scale <= 0)
                throw Error("Unable to reserve Robust playback headroom");
            const auto start = utc_time(first.helper_begin, rate);
            if (start < utc_now() + audio::minimum_lead_seconds())
                throw Error("Robust preparation missed the configured public start; retry on a later slot");
            audio::schedule_output(static_cast<double>(start), stop);
            publish(owner, stop, [&](Snapshot& out) {
                out.status = "Robust audio scheduled for UTC second " + std::to_string(first.helper_begin.epoch);
            });
        }, s.channel_mode, std::move(options));
    }

    std::optional<std::string> basic_transmit_reason(const Settings& s) const {
#ifdef DATAPUMP_EXECUTION_FIBERS
        (void)s;
        return "Robust needs the desktop native receiver workers. Use the native application for Robust.";
#else
        if (s.simulation) return {};
        if (!s.calendar) return "Configure the shared UTC calendar before Robust live operation.";
        if (!audio::utc_follow_supported())
            return "This audio provider cannot schedule a dated Robust calendar. Simulation is available.";
        return {};
#endif
    }

    void lock_status(Snapshot& out) const {
        out.force_transmit_available = false;
        out.noise_transmit_unavailable = out.transmit_unavailable;
        if (!profile || settings.simulation || out.transmitting || suspended || stopping) return;
        try {
            const auto index = earliest_tx_slot(*profile, 5);
            const auto waits_for_quiet = utc_time(profile->waveform.transmit_slot(index).helper_begin,
                profile->waveform.config().format.sample_rate) < separated_after;
            if (overlap_locked(*profile, index, EmissionMode::Noise))
                out.noise_transmit_unavailable = "The public Amateur Helper key has already used this Robust address.";
            else if (waits_for_quiet)
                out.noise_transmit_unavailable = "Waiting for two quiet public slots before the next Robust transmission.";
            if (overlap_locked(*profile, index, EmissionMode::Message)) {
                out.force_transmit_available = true;
                out.transmit_unavailable = "Transmit locked: this key already used a future Robust waveform address.";
            } else if (waits_for_quiet) {
                out.transmit_unavailable = "Waiting for two quiet public slots before the next Robust transmission.";
            }
        } catch (const std::exception& error) {
            out.transmit_unavailable = error.what();
            out.noise_transmit_unavailable = out.transmit_unavailable;
        }
    }

    void run(std::stop_token manager_stop) {
        std::uint64_t processed = 0;
        while (!manager_stop.stop_requested()) {
            execution::checkpoint();
            Settings s;
            std::uint64_t owner = 0;
            std::stop_token stop;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, manager_stop, [&] {
                    return stopping || (started && !suspended && generation != processed);
                });
                if (manager_stop.stop_requested() || stopping) break;
                s = settings;
                owner = processed = generation;
                generation_stop = std::stop_source{};
                stop = generation_stop.get_token();
                resources_owned = true;
                current.running = true;
                current.status = preparing;
                current.transmit_unavailable = preparing;
                current.gpu_device.clear();
                current.gpu_backend.clear();
                current.gpu_selection_reason.clear();
                current.gpu_score_rows = 0;
                current.gpu_precision.clear();
                current.gpu_fp32_score_rows = 0;
                current.gpu_fp16_storage_score_rows = 0;
                current.gpu_packed_fp16_score_rows = 0;
                current.gpu_native_coarse_lag_sessions = 0;
                current.gpu_cuda_native_fine_sessions = 0;
                current.gpu_cuda_native_fine_fallback_reason.clear();
                current.gpu_vulkan_shared_workspace_limit_bytes = 0;
                current.gpu_vulkan_leased_bytes = 0;
                current.gpu_vulkan_peak_leased_bytes = 0;
                current.gpu_vulkan_peak_owner_lease_bytes = 0;
                current.gpu_vulkan_waiting_sessions = 0;
                current.gpu_native_coarse_workspace_sessions = 0;
                current.gpu_host_scored_rows = 0;
                current.gpu_statistics_blocks = 0;
                current.gpu_cuda_fp16_calls = 0;
                current.gpu_cuda_int8_calls = 0;
                current.gpu_cuda_half_fft_score_rows = 0;
                current.gpu_cuda_half_fft_fallback_summary.clear();
                current.gpu_native_lag_preference_supported = false;
            }
            std::stop_callback shutdown(manager_stop, [source = generation_stop]() mutable { source.request_stop(); });
            std::shared_ptr<const Profile> active;
            // One cache owner per settings generation, retained across every
            // window and successive simulation transmissions. Reconfiguration
            // releases it only after capture and all receiver owners drain.
            std::shared_ptr<receiver_detail::ReceiverBulkBackend> gpu_backend;
            execution::Task capture;
            auto start_capture = [&] {
                if (s.simulation || !s.live_noise_dbfs_hz) return;
                capture = execution::Task([&, active](std::stop_token capture_stop) {
                    try {
                        receive_live(*active, s, owner, capture_stop);
                        publish(owner, capture_stop, [](Snapshot& out) {
                            out.receiver_error = "Robust audio input ended.";
                            if (!out.transmitting) out.status = out.receiver_error;
                        });
                    } catch (const std::exception& error) {
                        publish(owner, capture_stop, [&](Snapshot& out) {
                            out.receiver_error = error.what();
                            if (!out.transmitting) out.status = out.receiver_error;
                        });
                    }
                });
            };
            auto close_capture = [&] {
                capture.request_stop();
                if (capture.joinable()) capture.join();
            };

            try {
                if (const auto reason = basic_transmit_reason(s)) throw Error(*reason);
                if (s.gpu) {
                    const bool fp32_minimum = s.transfer.modem.search_arithmetic == modem::SearchArithmetic::fp32;
                    gpu_backend = make_gpu_receiver_backend(GpuReceiverOptions{fp32_minimum});
                }
                const auto gpu_initial_receipt = gpu_backend ? gpu_backend->status_receipt() : receiver_detail::BulkReceipt{};
                const auto gpu_device = gpu_backend ? gpu_initial_receipt.device_name : std::string{};
                const auto gpu_backend_name = gpu_backend ? gpu_initial_receipt.device : std::string{};
                const auto gpu_selection_reason = gpu_backend ? gpu_initial_receipt.selection_reason : std::string{};
                const bool native_lag_preference = gpu_backend && gpu_backend->permits_native_exact_chirp_z();
                const auto prepare = [&] {
                    const auto anchor = s.simulation ? simulation_calendar(s) : *s.calendar;
                    auto prepared = detail::prepare_profile(s, anchor, stop);
                    prepared.receiver.bulk_backend = gpu_backend;
                    active = std::make_shared<const Profile>(std::move(prepared));
                    const auto& calendar = active->waveform;
                    const auto initial = calendar.transmit_slot(0);
                    const auto following = calendar.transmit_slot(1);
                    std::lock_guard lock(mutex);
                    if (owner != generation || stop.stop_requested()) return;
                    profile = active;
                    current.gpu_device = gpu_device;
                    current.gpu_backend = gpu_backend_name;
                    current.gpu_selection_reason = gpu_selection_reason;
                    current.gpu_native_lag_preference_supported = native_lag_preference;
                    current.bit_seconds = calendar.geometry().bit_seconds;
                    current.slot_stride_seconds = static_cast<double>(calendar.calendar().distance(
                        initial.helper_begin, following.helper_begin)) / calendar.effective_sample_rate();
                    current.search_band_hz = std::pair{active->minimum_data_hz, active->maximum_data_hz};
                    if (s.simulation) current.simulation_spectrum_gain_db = s.tx_dbm - s.path_loss_db;
                    if (active->receiver.helper_enabled)
                        current.helper_band_hz = std::pair{helper::low_pilot_frequency_hz - 100,
                            active->receiver.helper.frequency_hz + 100};
                    current.transmit_unavailable.reset();
                    if (!s.simulation && !s.live_noise_dbfs_hz)
                        current.receiver_error = "Enter the live audio noise density to enable the configured receiver.";
                    else current.receiver_error.clear();
                    if (current.status == preparing)
                        current.status = s.simulation ? "Robust simulation ready" :
                            !current.receiver_error.empty() ? current.receiver_error : "Opening dated Robust audio input";
                };
                prepare();
                cancelled(stop);
                start_capture();
                while (!stop.stop_requested()) {
                    TxRequest send;
                    std::stop_token transmission;
                    Settings transmit_settings = s;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock, stop, [&] { return request.has_value() || stopping || owner != generation; });
                        if (stop.stop_requested() || stopping || owner != generation) break;
                        send = std::move(*request);
                        request.reset();
                        transmission = tx_stop.get_token();
                        // Volume and routing can be changed for the next
                        // transmission without restarting the receive calendar.
                        transmit_settings.transmit_gain = settings.transmit_gain;
                        transmit_settings.channel_mode = settings.channel_mode;
                    }
                    std::stop_callback cancel_on_reconfigure(stop, [source = tx_stop]() mutable { source.request_stop(); });
                    bool capture_paused = false;
                    std::string failure;
                    try {
                        cancelled(transmission);
                        if (!s.simulation && s.full_duplex && s.live_noise_dbfs_hz)
                            (void)live_transmit_reservation_checked(transmit_settings, *active);
                        if (!s.simulation && !s.full_duplex) {
                            close_capture();
                            capture_paused = true;
                        }
                        prepare_send(send, transmit_settings, owner, transmission);
                        if (s.simulation) simulate(*active, transmit_settings, send, owner, transmission);
                        else play_live(*active, transmit_settings, send, owner, transmission);
                    } catch (const std::exception& error) {
                        if (!transmission.stop_requested()) failure = error.what();
                    }
                    publish(owner, stop, [&](Snapshot& out) {
                        out.transmitting = false;
                        out.transmit_trace.active = false;
                        ++out.transmit_trace.revision;
                        out.error = failure;
                        out.status = transmission.stop_requested() ? "Robust transmission cancelled" :
                            !failure.empty() ? failure :
                            s.simulation ? "Robust simulation input processed" : "Robust transmission sent";
                    });
                    if (stop.stop_requested()) break;
                    if (s.simulation) {
                        const auto& calendar = active->waveform;
                        // A new simulation is a new local source experiment.
                        // Retain a monotonically increasing address between
                        // runs without scoring the idle time between them.
                        const auto advance = send.mode == EmissionMode::Message ? send.wire_bits() + 2 : 3;
                        next_simulation_ordinal = add_slot(calendar.config().format.symbol_ordinal, advance);
                        const auto first = calendar.transmit_slot(0);
                        last_simulation_epoch = add_slot(first.support_begin.epoch, 30);
                        prepare();
                    } else if (capture_paused) start_capture();
                }
            } catch (const std::exception& error) {
                publish(owner, stop, [&](Snapshot& out) {
                    out.running = false;
                    out.transmitting = false;
                    out.status = error.what();
                    out.receiver_error = error.what();
                    out.transmit_unavailable = error.what();
                });
            }
            close_capture();
            {
                std::lock_guard lock(mutex);
                profile.reset();
            }
            // Device destruction must stay off the GUI mutex, and suspension
            // must not announce released resources before this teardown.
            active.reset();
            gpu_backend.reset();
            {
                std::lock_guard lock(mutex);
                resources_owned = false;
            }
            changed.notify_all();
        }
        {
            std::lock_guard lock(mutex);
            resources_owned = false;
            stopped = true;
            profile.reset();
        }
        changed.notify_all();
    }

    void enqueue(EmissionMode mode, std::span<const std::uint8_t> input = {}, bool force = false, bool raw = false) {
        std::lock_guard lock(mutex);
        if (stopping || suspended || !started) throw Error(no_transmitter);
        if (current.transmitting || request) throw Error("A Robust transmission is already active");
        if (!profile) throw Error(current.transmit_unavailable.value_or(preparing));
        if (mode == EmissionMode::Message && !raw &&
            (input.empty() || input.size() > message_profile(settings).content_limit))
            throw Error("Robust text is empty or exceeds its local content quota");
        if (mode == EmissionMode::HelperOnly && !profile->receiver.helper_enabled)
            throw Error("Set nonzero Helper Carrier power before transmitting Helper only");
        if (!settings.simulation) {
            const auto index = earliest_tx_slot(*profile, 5);
            const auto locked = overlap_locked(*profile, index, mode);
            if (force && !locked) throw Error("There is no current Robust key-use lock to override");
            if (!force && locked) throw Error("Robust transmit locked: this key already used a future address");
            if (!force && utc_time(profile->waveform.transmit_slot(index).helper_begin,
                    profile->waveform.config().format.sample_rate) < separated_after)
                throw Error("Waiting for two quiet public slots before the next Robust transmission");
        } else if (force) throw Error("Robust simulation has no hardware key-use lock");
        TxRequest send{mode, {input.begin(), input.end()}, force, raw, {}};
        tx_stop = std::stop_source{};
        current.transmitting = true;
        current.error.clear();
        current.status = send.mode == EmissionMode::Noise ? "Preparing Robust transmit noise" :
            send.mode == EmissionMode::HelperOnly ? "Preparing Robust Helper-only transmission" :
            "Preparing Robust transmission";
        if (settings.simulation) {
            current.pattern_steps.clear();
            current.spectrum_revision = 0;
        } else if (!settings.full_duplex) current.constellation.clear();
        auto& trace = current.transmit_trace;
        const auto revision = trace.revision + 1;
        trace = {};
        trace.revision = revision;
        trace.active = send.mode == EmissionMode::Message;
        trace.temporary_key = send.mode == EmissionMode::Noise;
        if (trace.active) {
            trace.raw = send.raw;
            trace.short_text = !send.raw && send.input.size() <= transfer::short_message_bytes;
            if (send.raw) {
                trace.total_wire_bits = send.input.size();
                const auto count = std::min(send.input.size(), modem::TransmitTrace::bit_limit);
                trace.wire_plain_bits.assign(send.input.begin(), send.input.begin() + static_cast<std::ptrdiff_t>(count));
            } else {
                const auto count = std::min(send.input.size(), modem::TransmitTrace::source_limit);
                trace.source_available = true;
                trace.source.assign(send.input.begin(), send.input.begin() + static_cast<std::ptrdiff_t>(count));
            }
        }
        request = std::move(send);
        changed.notify_all();
    }
};

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;

void Session::start(const Settings& value, bool active) {
    validate_settings(value);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (p.stopping) throw Error("Robust session is closing");
    if (p.started) return;
    p.settings = value;
    p.started = true;
    p.suspended = !active;
    p.current.simulation = value.simulation;
    p.current.status = active ? preparing : no_transmitter;
    p.current.transmit_unavailable = p.current.status;
    ++p.generation;
    try {
        p.manager = execution::Task([&p](std::stop_token stop) { p.run(stop); });
    } catch (...) {
        p.started = false;
        throw;
    }
}

void Session::configure(const Settings& value) {
    validate_settings(value);
    auto& p = *impl_;
    std::stop_source stop;
    {
        std::lock_guard lock(p.mutex);
        if (p.stopping) throw Error("Robust session is closing");
        if (p.current.transmitting) throw Error("Wait for the Robust transmission to finish before changing its settings");
        p.settings = value;
        p.profile.reset();
        p.current = {};
        p.received_events.clear();
        p.signal_events.clear();
        p.current.simulation = value.simulation;
        p.current.status = p.started && !p.suspended ? preparing : no_transmitter;
        p.current.transmit_unavailable = p.current.status;
        ++p.generation;
        stop = p.generation_stop;
    }
    stop.request_stop();
    p.changed.notify_all();
}

void Session::set_mono(bool mono) {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    p.settings.mono = mono;
    p.settings.channel_mode = audio::output_channels(mono);
}

void Session::set_channel_mode(audio::ChannelMode channels) {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    p.settings.mono = channels != audio::ChannelMode::stereo;
    p.settings.channel_mode = channels;
}

void Session::set_transmit_gain(double gain) {
    audio::Options value;
    value.transmit_gain = gain;
    audio::validate_option_values(value);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    p.settings.transmit_gain = gain;
}

void Session::set_helper_callsign(std::string callsign) {
    Settings changed;
    {
        auto& p = *impl_;
        std::lock_guard lock(p.mutex);
        changed = p.settings;
    }
    changed.helper.callsign = std::move(callsign);
    configure(changed);
}

bool Session::try_suspend_capture() {
    auto& p = *impl_;
    std::stop_source stop;
    bool owned = false;
    {
        std::lock_guard lock(p.mutex);
        if (p.current.transmitting) return false;
        owned = p.resources_owned;
        if (!p.suspended) {
            p.suspended = true;
            ++p.generation;
            p.current.running = false;
            p.current.status = no_transmitter;
            p.current.transmit_unavailable = no_transmitter;
            p.current.force_transmit_available = false;
            stop = p.generation_stop;
        }
    }
    stop.request_stop();
    p.changed.notify_all();
    return !owned;
}

void Session::resume_capture() {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.started || p.stopping || !p.suspended) return;
    p.suspended = false;
    p.current.status = preparing;
    p.current.transmit_unavailable = preparing;
    ++p.generation;
    p.changed.notify_all();
}

void Session::clear_received() {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        p.signal_events.clear();
        p.received_events.clear();
    }
    p.changed.notify_all();
}

void Session::transmit(const Message& message, bool force) {
    if (message.kind != MessageKind::text || message.data.empty() || message.data.size() > native_message_editor_limit)
        throw Error("Robust sends nonempty text within its local content quota or explicit raw bits");
    impl_->enqueue(EmissionMode::Message, message.data, force);
}

void Session::transmit_bits(std::span<const std::uint8_t> bits, bool force) {
    if (bits.empty() || bits.size() > transfer::short_message_bits ||
        std::any_of(bits.begin(), bits.end(), [](std::uint8_t bit) { return bit > 1; }))
        throw Error("Robust raw transmission requires 1 to 208 binary symbols");
    impl_->enqueue(EmissionMode::Message, bits, force, true);
}

void Session::transmit_noise() { impl_->enqueue(EmissionMode::Noise); }
void Session::transmit_helper() { impl_->enqueue(EmissionMode::HelperOnly); }

void Session::cancel_transmit() {
    auto& p = *impl_;
    std::stop_source stop;
    {
        std::lock_guard lock(p.mutex);
        if (!p.current.transmitting) return;
        stop = p.tx_stop;
        p.current.status = "Stopping Robust transmission";
    }
    stop.request_stop();
    p.changed.notify_all();
}

bool Session::resume_recovery(std::uint64_t) { return false; }
void Session::cancel_recovery(std::uint64_t) {}

Snapshot Session::snapshot() {
    auto& p = *impl_;
    Snapshot result;
    {
        std::lock_guard lock(p.mutex);
        // The receipt lock only copies counters; it never calls the device or
        // takes this mutex. Do not lend the GUI a backend shared_ptr: it could
        // become the last owner and perform device teardown on this thread.
        if (p.profile && p.profile->receiver.bulk_backend) {
            const auto receipt = p.profile->receiver.bulk_backend->status_receipt();
            p.current.gpu_score_rows = receipt.score_rows;
            p.current.gpu_precision = receipt.precision;
            p.current.gpu_fp32_score_rows = receipt.vulkan_fp32_score_rows;
            p.current.gpu_fp16_storage_score_rows = receipt.vulkan_fp16_storage_score_rows;
            p.current.gpu_packed_fp16_score_rows = receipt.vulkan_direct_fp16_score_rows;
            p.current.gpu_native_coarse_lag_sessions = receipt.native_coarse_lag_sessions;
            if (p.current.gpu_backend.starts_with("cuda:")) {
                p.current.gpu_cuda_native_fine_sessions = receipt.native_fine_sessions;
                p.current.gpu_cuda_native_fine_fallback_reason = receipt.native_fine_fallback_reason;
            } else if (p.current.gpu_backend.starts_with("vulkan:")) {
                p.current.gpu_vulkan_shared_workspace_limit_bytes = receipt.vulkan_shared_workspace_limit_bytes;
                p.current.gpu_vulkan_leased_bytes = receipt.vulkan_leased_bytes;
                p.current.gpu_vulkan_peak_leased_bytes = receipt.vulkan_peak_leased_bytes;
                p.current.gpu_vulkan_peak_owner_lease_bytes = receipt.vulkan_peak_owner_lease_bytes;
                p.current.gpu_vulkan_waiting_sessions = receipt.vulkan_waiting_sessions;
                p.current.gpu_native_coarse_workspace_sessions = receipt.native_coarse_workspace_sessions;
            }
            p.current.gpu_host_scored_rows = receipt.vulkan_host_scored_rows;
            p.current.gpu_statistics_blocks = receipt.statistics_blocks;
            p.current.gpu_cuda_fp16_calls = receipt.cuda_matrix_fp16_calls;
            p.current.gpu_cuda_int8_calls = receipt.cuda_matrix_int8_calls;
            p.current.gpu_cuda_half_fft_score_rows = receipt.cuda_half_fft_score_rows;
            p.current.gpu_cuda_half_fft_fallback_summary.clear();
            for (std::size_t i = 0; i < receipt.half_fft_fallback_counts.size(); ++i) {
                const auto count = receipt.half_fft_fallback_counts[i];
                if (!count) continue;
                if (!p.current.gpu_cuda_half_fft_fallback_summary.empty())
                    p.current.gpu_cuda_half_fft_fallback_summary += ", ";
                p.current.gpu_cuda_half_fft_fallback_summary +=
                    std::string(triad::v3::receiver_detail::bulk_half_fft_fallback_reasons[i]) + "=" +
                    std::to_string(count);
            }
        }
        result = p.current;
        result.signals.swap(p.signal_events);
        result.received.swap(p.received_events);
        p.lock_status(result);
    }
    if (!result.received.empty()) p.changed.notify_all();
    return result;
}

void Session::stop() {
    auto& p = *impl_;
    std::stop_source generation, transmission;
    {
        std::lock_guard lock(p.mutex);
        if (p.stopping) return;
        p.stopping = true;
        ++p.generation;
        p.current.running = p.current.transmitting = false;
        p.current.status = "Stopping Robust session";
        p.request.reset();
        generation = p.generation_stop;
        transmission = p.tx_stop;
    }
    generation.request_stop();
    transmission.request_stop();
    p.manager.request_stop();
    p.changed.notify_all();
}

bool Session::ready_to_close() const {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    return !p.started || p.stopped;
}

} // namespace datapump::live
