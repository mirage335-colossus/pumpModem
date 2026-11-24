#include "datapump/live.hpp"

#include "datapump/execution.hpp"
#include "datapump/received_text.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "live_capture_queue.hpp"
#include "live_finite_plan.hpp"
#include "search_fft.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <numbers>
#include <sstream>
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
constexpr std::size_t simulation_waveform_size = 2048;
constexpr std::size_t simulation_waterfall_windows_per_row = 32;
constexpr auto simulation_preview_period = std::chrono::milliseconds(45);
constexpr auto simulation_workspace_preflight_budget = std::chrono::milliseconds(100);
constexpr auto simulation_replay_duration = std::chrono::seconds(3);
constexpr auto simulation_replay_period = std::chrono::milliseconds(50);
constexpr std::size_t simulation_waterfall_workspace_bytes() {
    // A slow poller can hold an older set of rows while the recorder
    // replaces published rows. Charge all three sets even if they overlap.
    constexpr auto row_bytes = (plot_size / 2 + 1) * sizeof(double) +
        sizeof(std::shared_ptr<const std::vector<double>>) + 64;
    return 3 * Snapshot::SimulationWaterfall::maximum_rows * row_bytes + 64 * 1024 +
        plot_size * (sizeof(Complex) + sizeof(std::uint8_t)) +
        (plot_size / 2 + 1) * sizeof(double) + 3 * 1024 * 1024U;
}
constexpr std::uint64_t simulation_replay_rows(std::uint32_t rate) {
    return static_cast<std::uint64_t>(simulation_replay_duration.count()) * rate + simulation_waveform_size - 1;
}
// Binding can briefly hold two interval readers. Playback's native provider
// allowance covers its converter, PCM blocks, and the dated PipeWire ring.
constexpr std::size_t lazy_bit_workspace = 64 * 1024U;
constexpr std::size_t playback_provider_workspace = 2 * 1024 * 1024U;
constexpr std::size_t playback_controller_workspace = 2 * 1024 * 1024U;
constexpr auto no_transmitter = "Select Robust to start its session.";
constexpr auto preparing = "Preparing the configured Robust profile.";
constexpr auto duplex_workspace_error = "Robust full-duplex transmit workspace exceeds the receiver memory limit";
constexpr auto calibrating_live_noise = "Measuring startup audio noise. Keep the channel idle.";
constexpr auto wait_for_live_calibration = "Wait for live audio calibration before full-duplex transmission.";
// Wire address origin for automatic live operation. Each waveform uses its
// own public slot spacing and advances the ordinal through idle slots.
constexpr Calendar automatic_live_calendar{1767225600, 0}; // 2026-01-01 00:00:00 UTC

bool live_receive_enabled(const Settings& settings) {
    return settings.auto_live_noise || settings.live_noise_dbfs_hz.has_value();
}

std::string passband_error(std::string_view direction, const audio::StreamFormat& format,
                           double required_upper_hz) {
    std::ostringstream message;
    message << std::setprecision(8)
            << "Robust " << direction << " band requires an upper edge of " << required_upper_hz
            << " Hz. The selected audio device has a usable passband of "
            << format.usable_passband_hz << " Hz (logical rate " << format.logical_rate
            << " Hz, hardware rate " << format.hardware_rate
            << " Hz). Reduce Rate at both peers or select a device with a wider passband.";
    return message.str();
}

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

} // namespace

detail::DatedReceiveStart detail::plan_dated_receive_start(const Profile& base, bool automatic, long double first_utc,
                                                           double provider_uncertainty, std::size_t filter_halo) {
    const auto& waveform = base.waveform;
    const auto rate = waveform.config().format.sample_rate;
    if (first_utc < 0 || first_utc >= 4294967296.L)
        throw Error("Robust input timestamp is outside the configured calendar");
    const auto earliest = coordinate(std::max<long double>(first_utc, waveform.origin().epoch), rate);
    auto slot = waveform.first_slot_not_before(earliest);
    // Dated playback bounds emission against the current UTC slot. Keep the
    // predecessor available for adjacent-signal exclusion in the local view.
    const auto origin = automatic && slot ? waveform.transmit_slot(slot - 1).helper_begin : waveform.origin();
    const auto age = first_utc - utc_time(origin, rate);
    const auto epoch_ambiguity = std::abs(age) * waveform.config().audio_pair_ppm * 1e-6L;
    auto adjusted = with_capture_uncertainty(base, provider_uncertainty + 1. / rate,
        static_cast<double>(epoch_ambiguity), filter_halo, origin);
    const auto input_sample = std::llround((age + adjusted.waveform.config().capture_lead_seconds) * rate);
    const auto valid_first = input_sample + static_cast<std::int64_t>(filter_halo);
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
    return {std::move(adjusted), input_sample, slot, first};
}

namespace {

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
    long double requested_utc = 0;

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
    const auto playback = checked_workspace(profile.waveform.emission_source_workspace_bytes(),
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
    Bytes keystream;
};

std::optional<HopPosition> illustrated_hop(const Profile& active, std::uint64_t sample, bool noise,
                                           std::optional<std::uint64_t> previous_dwell) {
    if (active.hop_shifts_hz.empty()) return std::nullopt;
    const auto& format = active.waveform.config().format;
    const auto rate = format.sample_rate;
    // Split the absolute UTC coordinate at a two-second integral boundary.
    // Five exact 400 ms dwells fit that interval.
    const auto dwell = format.epoch / 2 * 5 + (format.epoch % 2 * rate + sample) / (rate / 5 * 2);
    if (previous_dwell == dwell) return std::nullopt;
    const auto count = active.hop_shifts_hz.size();
    const auto cycle = dwell / count;
    const auto key = noise ? Crypto::amateur() : active.key;
    auto order = key.stream(StreamPurpose::Fhss, format.epoch, cycle * 8, 8);
    std::uint64_t seed = 0;
    for (const auto byte : order) seed = (seed << 8) | byte;
    auto stride = 1 + (seed >> 32) % count;
    while (std::gcd(stride, count) != 1) stride = stride % count + 1;
    const auto channel = static_cast<unsigned>((seed % count + stride * (dwell % count)) % count);
    return HopPosition{dwell, cycle, channel, active.hop_shifts_hz[channel], std::move(order)};
}

struct EmissionTraceCursor {
    std::optional<std::uint64_t> pattern_ordinal, hop_dwell;
};

struct PlotFrame {
    std::vector<float> waveform;
    std::vector<Complex> iq;
    std::vector<double> db;
    std::vector<std::complex<float>> spectrum;
    std::uint64_t first = 0;
    std::uint32_t rate = 0;
};

PlotFrame plot_frame(std::uint64_t first, std::uint32_t rate, std::vector<float> waveform,
                     std::span<const Complex> iq, const modem::detail::search_fft::DoublePlan& fft) {
    PlotFrame result;
    result.rate = rate;
    result.first = first + waveform.size() - plot_size;
    result.waveform = std::move(waveform);
    std::array<Complex, plot_size> bins{};
    const auto fft_input = std::span(result.waveform).last(plot_size);
    for (std::size_t i = 0; i < plot_size; ++i)
        bins[i] = fft_input[i] * (.5 - .5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / plot_size));
    modem::detail::search_fft::fft(bins, false, fft);
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

PlotFrame receiver_plot_frame(std::uint64_t first, std::uint32_t rate, double if_hz,
                              std::span<const Complex> iq) {
    static const modem::detail::search_fft::DoublePlan fft(plot_size);
    std::vector<float> waveform(iq.size());
    for (std::size_t i = 0; i < waveform.size(); ++i)
        waveform[i] = real_pcm(iq[i], first + i, if_hz, rate);
    return plot_frame(first, rate, std::move(waveform), iq, fft);
}

struct SimulationPreview {
    std::uint64_t owner, segment, first;
    std::uint32_t rate;
    double if_hz;
    std::stop_token stop;
    std::optional<Snapshot::ReplayWaterfall> waterfall;
    std::vector<Complex> input;
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
        const auto end = first + pcm.size();
        std::vector<float> waveform(plot_size);
        for (std::size_t i = 0; i < plot_size; ++i)
            waveform[i] = pcm_[(at_ + i) % plot_size];
        return plot_frame(end >= plot_size ? end - plot_size : 0, rate_, std::move(waveform), iq, fft_);
    }
private:
    std::uint32_t rate_;
    modem::detail::search_fft::DoublePlan fft_;
    std::array<float, plot_size> pcm_{};
    std::size_t at_ = 0, fill_ = 0;
    Clock::time_point next_{};
};

double simulation_plot_noise_reference(const Settings& settings, std::uint32_t rate) {
    // Real projection preserves complex noise variance. The Hann transform
    // normalized by 2/N assigns white noise an effective bandwidth of 1.5 bins.
    const auto noise_bandwidth = 1.5 * rate / plot_size;
    return settings.tx_dbm - settings.path_loss_db - settings.noise_dbm_hz -
        10 * std::log10(noise_bandwidth);
}

std::size_t simulation_detail_workspace_bytes(std::uint32_t rate) {
    const auto horizon = static_cast<std::size_t>(rate) * 36 / 5;
    const auto ring_samples = horizon + plot_size;
    const auto ring_bytes = ring_samples * (sizeof(Complex) + sizeof(std::uint8_t));
    const auto detached_bytes = ring_bytes;
    const auto row_bytes = simulation_detail_rows * (plot_size / 2 + 1) *
        (sizeof(double) + sizeof(std::complex<float>));
    // Charge the cached batch and its replacement, plus the GUI histories
    // and bounded native transform scratch.
    return ring_bytes + detached_bytes + 2 * row_bytes + 3 * 1024 * 1024U;
}

class SimulationDetailCapture {
public:
    SimulationDetailCapture(std::uint64_t segment, std::uint64_t first,
                            std::uint32_t rate, double if_hz)
        : segment_(segment), end_(first), rate_(rate), if_hz_(if_hz),
          capacity_(static_cast<std::size_t>(rate) * 36 / 5 + plot_size),
          iq_(capacity_), valid_(capacity_, 0) {}

    void append(std::uint64_t first, std::span<const Complex> iq,
                std::span<const std::uint8_t> valid) {
        std::lock_guard lock(mutex_);
        if (first != end_) {
            // A discontinuity starts a clean segment rather than allowing old
            // samples to bridge a gap or a newly configured generation.
            end_ = first;
            count_ = write_ = 0;
            ++continuity_;
            cached_.reset();
        }
        if (iq.size() >= capacity_) {
            const auto skip = iq.size() - capacity_;
            std::copy(iq.begin() + static_cast<std::ptrdiff_t>(skip), iq.end(), iq_.begin());
            std::copy(valid.begin() + static_cast<std::ptrdiff_t>(skip), valid.end(), valid_.begin());
            write_ = 0;
            count_ = capacity_;
        } else {
            const auto first_count = std::min(iq.size(), capacity_ - write_);
            std::copy_n(iq.begin(), first_count, iq_.begin() + static_cast<std::ptrdiff_t>(write_));
            std::copy_n(valid.begin(), first_count, valid_.begin() + static_cast<std::ptrdiff_t>(write_));
            const auto second_count = iq.size() - first_count;
            if (second_count) {
                std::copy_n(iq.begin() + static_cast<std::ptrdiff_t>(first_count), second_count, iq_.begin());
                std::copy_n(valid.begin() + static_cast<std::ptrdiff_t>(first_count), second_count, valid_.begin());
            }
            write_ = (write_ + iq.size()) % capacity_;
            count_ = std::min(count_ + iq.size(), capacity_);
        }
        end_ = first + iq.size();
    }

    std::shared_ptr<const Snapshot::SimulationDetail> snapshot(Clock::time_point now) {
        std::vector<Complex> samples;
        std::vector<std::uint8_t> validity;
        std::uint64_t end = 0, available_first = 0, continuity = 0;
        {
            std::lock_guard lock(mutex_);
            if (cached_ && cached_->end_sample == end_) return cached_;
            if (cached_ && (building_ || now - last_build_ < std::chrono::milliseconds(100))) return cached_;
            if (building_) return cached_;
            end = end_;
            continuity = continuity_;
            available_first = end_ - count_;
            samples.resize(count_);
            validity.resize(count_);
            const auto oldest = (write_ + capacity_ - count_) % capacity_;
            const auto first_count = std::min(count_, capacity_ - oldest);
            std::copy_n(iq_.begin() + static_cast<std::ptrdiff_t>(oldest), first_count, samples.begin());
            std::copy_n(valid_.begin() + static_cast<std::ptrdiff_t>(oldest), first_count, validity.begin());
            if (first_count < count_) {
                const auto second_count = count_ - first_count;
                std::copy_n(iq_.begin(), second_count, samples.begin() + static_cast<std::ptrdiff_t>(first_count));
                std::copy_n(valid_.begin(), second_count, validity.begin() + static_cast<std::ptrdiff_t>(first_count));
            }
            building_ = true;
            last_build_ = now;
        }

        try {
            auto detail = std::make_shared<Snapshot::SimulationDetail>();
            detail->segment = segment_;
            detail->end_sample = end;
            detail->sample_rate = rate_;
            detail->fft_samples = plot_size;
            detail->rows.resize(simulation_detail_rows);
            const auto horizon = static_cast<std::uint64_t>(rate_) * 36 / 5;
            static const modem::detail::search_fft::DoublePlan fft(plot_size);
            static const auto hann = [] {
                std::array<double, plot_size> values{};
                for (std::size_t i = 0; i < values.size(); ++i)
                    values[i] = .5 - .5 * std::cos(2 * std::numbers::pi *
                        static_cast<double>(i) / plot_size);
                return values;
            }();
            std::array<Complex, plot_size> bins{};
            std::array<float, plot_size> pcm{};
            std::uint64_t previous_fft_end = 0;
            for (std::size_t row = 0; row < simulation_detail_rows; ++row) {
                const auto slots_from_end = simulation_detail_rows - row;
                const auto end_offset = horizon * (slots_from_end - 1) / simulation_detail_rows;
                const auto cell_width = horizon * slots_from_end / simulation_detail_rows - end_offset;
                auto& out = detail->rows[row];
                const auto required_width = std::max<std::uint64_t>(cell_width, plot_size);
                if (end < end_offset + required_width) continue;
                const auto row_end = end - end_offset;
                const auto considered_first = row_end - required_width;
                const auto fft_first = row_end - plot_size;
                if (considered_first < available_first) continue;
                const auto offset = static_cast<std::size_t>(fft_first - available_first);
                bool valid_window = offset + plot_size <= samples.size();
                const auto check_offset = static_cast<std::size_t>(considered_first - available_first);
                for (std::size_t i = 0; valid_window && i < required_width; ++i)
                    valid_window = validity[check_offset + i] != 0;
                if (!valid_window) continue;
                out.start_sample = fft_first;
                // Adjacent FFTs overlap at ordinary audio rates. Project shared
                // samples once while preserving the exact native PCM conversion.
                const auto overlap = fft_first < previous_fft_end ?
                    static_cast<std::size_t>(previous_fft_end - fft_first) : 0;
                std::copy(pcm.end() - overlap, pcm.end(), pcm.begin());
                for (std::size_t i = overlap; i < plot_size; ++i)
                    pcm[i] = real_pcm(samples[offset + i], fft_first + i, if_hz_, rate_);
                for (std::size_t i = 0; i < plot_size; ++i)
                    bins[i] = pcm[i] * hann[i];
                previous_fft_end = row_end;
                modem::detail::search_fft::fft(bins, false, fft);
                out.db.reserve(plot_size / 2 + 1);
                out.spectrum.reserve(plot_size / 2 + 1);
                for (const auto value : std::span(bins).first(plot_size / 2 + 1)) {
                    const auto normalized = value * (2. / plot_size);
                    out.spectrum.emplace_back(normalized);
                    out.db.push_back(10 * std::log10(std::max(1e-24, std::norm(normalized))));
                }
            }

            std::lock_guard lock(mutex_);
            building_ = false;
            if (continuity_ == continuity) cached_ = std::move(detail);
            return cached_;
        } catch (...) {
            std::lock_guard lock(mutex_);
            building_ = false;
            throw;
        }
    }

private:
    std::uint64_t segment_, end_, continuity_=0;
    std::uint32_t rate_;
    double if_hz_;
    const std::size_t capacity_;
    std::vector<Complex> iq_;
    std::vector<std::uint8_t> valid_;
    std::size_t write_=0, count_=0;
    std::mutex mutex_;
    std::shared_ptr<const Snapshot::SimulationDetail> cached_;
    Clock::time_point last_build_{};
    bool building_=false;
};

struct SimulationReplay {
    SimulationReplay(const Profile& profile, const Settings& settings, const triad::SimulationConfig& effects,
                     std::uint64_t source_sample, std::uint64_t input_total)
        : rate(settings.transfer.modem.sample_rate), data_if_hz(profile.receiver.data_if_hz),
          source_first(source_sample) {
        // Keep the same modeled mean Hann-bin noise reference as for the
        // original plot, even if the operator edits the link before replaying.
        waterfall.gain_db = simulation_plot_noise_reference(settings, rate);
        waterfall.search_band_hz = {profile.minimum_data_hz, profile.maximum_data_hz};
        if (profile.receiver.helper_enabled)
            waterfall.helper_band_hz = std::pair{helper::low_pilot_frequency_hz - 100,
                profile.receiver.helper.frequency_hz + 100};
        tx_scale = 1 + effects.tx.audio_ppm * 1e-6;
        rx_scale = 1 + effects.rx.audio_ppm * 1e-6;
        // Invert the simulated direct-path sample coordinate for presentation
        // only. No receiver acquisition or decision uses the known arrival.
        capture_lead_seconds = effects.capture_lead_seconds;
        timing_offset_seconds = effects.timing_offset_seconds;
        const auto lead = static_cast<long double>(capture_lead_seconds) + timing_offset_seconds;
        const auto received = std::ceil((source_sample / static_cast<long double>(tx_scale) +
            lead * rate) * rx_scale);
        first = static_cast<std::uint64_t>(std::clamp(received, 0.L, static_cast<long double>(input_total)));
        // One waveform window of lookahead keeps the last replay frame usable.
        limit = static_cast<std::size_t>(std::min(input_total - first, simulation_replay_rows(rate)));
    }

    std::size_t working_bytes() const {
        auto bytes = limit * (sizeof(Complex) + sizeof(std::uint8_t));
        if (waterfall_recorder) bytes = checked_workspace(bytes, simulation_waterfall_workspace_bytes());
        if (pattern_steps) {
            bytes += sizeof(*pattern_steps) + pattern_steps->capacity() * sizeof(PatternStep);
            for (const auto& step : *pattern_steps)
                bytes += step.wrong_key_admission.capacity() + step.helper_status.capacity() +
                    step.varying_status.capacity() + step.drift_status.capacity() + step.reason.capacity();
        }
        return bytes;
    }
    void allocate() {
        input = std::make_unique_for_overwrite<Complex[]>(limit);
        validity = std::make_unique_for_overwrite<std::uint8_t[]>(limit);
    }
    std::span<const Complex> samples() const { return {input.get(), filled}; }
    std::span<const std::uint8_t> valid_samples() const { return {validity.get(), filled}; }

    bool record(std::uint64_t begin, std::span<const Complex> observed,
                std::span<const std::uint8_t> valid) {
        const auto next = first + filled;
        if (filled != limit && next >= begin && next < begin + observed.size()) {
            const auto offset = static_cast<std::size_t>(next - begin);
            const auto count = std::min(observed.size() - offset, limit - filled);
            std::copy_n(observed.data() + offset, count, input.get() + filled);
            std::copy_n(valid.data() + offset, count, validity.get() + filled);
            filled += count;
        }
        if (!waterfall_recorder || !waterfall_recorder->record(begin, observed, valid)) return false;
        modeled_waterfall = waterfall_recorder->snapshot();
        return true;
    }

    void enable_waterfall(std::uint64_t segment, double bit_seconds) {
        if (!std::isfinite(bit_seconds) || bit_seconds < 60) return;
        const auto lead = static_cast<long double>(capture_lead_seconds) + timing_offset_seconds;
        const auto source_end = static_cast<long double>(source_first) + bit_seconds * rate;
        const auto mapped_end = std::ceil((source_end / tx_scale + lead * rate) * rx_scale);
        const auto end = static_cast<std::uint64_t>(std::clamp(mapped_end, 0.L,
            static_cast<long double>(UINT64_MAX)));
        if (end <= first || end - first < plot_size) return;
        waterfall_recorder = std::make_unique<WaterfallRecorder>(segment, first, end, rate,
            waterfall.gain_db, data_if_hz);
        modeled_waterfall = waterfall_recorder->snapshot();
    }

    std::shared_ptr<const Snapshot::SimulationWaterfall> waterfall_snapshot() const {
        return modeled_waterfall;
    }

    Snapshot::ReplayWaterfall waterfall;
    std::uint32_t rate;
    double data_if_hz;
    std::uint64_t source_first, first;
    double tx_scale=1, rx_scale=1;
    double capture_lead_seconds=0, timing_offset_seconds=0;
    std::shared_ptr<const std::vector<PatternStep>> pattern_steps;
private:
    std::size_t limit;
    std::size_t filled = 0;
    std::unique_ptr<Complex[]> input;
    std::unique_ptr<std::uint8_t[]> validity;
    std::shared_ptr<const Snapshot::SimulationWaterfall> modeled_waterfall;
    struct WaterfallRecorder {
        WaterfallRecorder(std::uint64_t segment, std::uint64_t first, std::uint64_t end,
                          std::uint32_t rate, double gain_db,
                          double if_hz)
            : bit_samples(end - first), row_first(first), rate(rate), if_hz(if_hz) {
            value.total_rows = std::min<std::uint64_t>(value.maximum_rows, bit_samples / plot_size);
            value.segment = segment;
            value.bin_hz = static_cast<double>(rate) / plot_size;
            value.gain_db = gain_db;
            value.seconds_per_row = static_cast<double>(bit_samples) / rate / value.total_rows;
            value.rows.reserve(value.total_rows);
        }

        bool record(std::uint64_t chunk_first, std::span<const Complex> samples,
                    std::span<const std::uint8_t> valid) {
            const auto completed_before = row;
            const auto chunk_end = chunk_first + samples.size();
            while (row_first < chunk_end) {
                const auto width = row_width();
                if (width > UINT64_MAX - row_first) break;
                const auto row_end = row_first + width;
                const auto count = std::min<std::uint64_t>(simulation_waterfall_windows_per_row, width / plot_size);
                if (!count) break;
                if (window == count) {
                    if (chunk_end < row_end) return row != completed_before;
                    finish_row();
                    row_first = row_end;
                    continue;
                }
                const auto row_offset = static_cast<std::uint64_t>(window) * width / count;
                const auto window_first = row_first + row_offset + (width / count - plot_size) / 2;
                const auto window_end = window_first + plot_size;
                if (window_end <= chunk_first) {
                    // An unavailable earlier window cannot complete this row.
                    return row != completed_before;
                }
                if (window_first >= chunk_end) return row != completed_before;
                const auto copy_first = std::max(window_first, chunk_first);
                const auto copy_end = std::min(window_end, chunk_end);
                const auto source_offset = static_cast<std::size_t>(copy_first - chunk_first);
                const auto target_offset = static_cast<std::size_t>(copy_first - window_first);
                if (!target_offset) {
                    window_valid = true;
                    window_filled = 0;
                }
                if (target_offset != window_filled) return row != completed_before;
                const auto copy_count = static_cast<std::size_t>(copy_end - copy_first);
                std::copy_n(samples.data() + source_offset, copy_count, window_samples.data() + target_offset);
                for (std::size_t i = 0; i < copy_count; ++i)
                    window_valid = window_valid && valid[source_offset + i] != 0;
                window_filled += copy_count;
                if (window_filled != plot_size) return row != completed_before;
                if (window_valid) accumulate_window(window_first);
                ++window;
                window_filled = 0;
            }
            return row != completed_before;
        }

        std::shared_ptr<const Snapshot::SimulationWaterfall> snapshot() const {
            auto copy = std::make_shared<Snapshot::SimulationWaterfall>(value);
            return copy;
        }

    private:
        std::uint64_t row_width() const {
            // Repeat the exact first-bit partition without multiplying the
            // full capture index by the bit's sample count.
            const auto rows = value.total_rows;
            const auto slot = row % rows;
            const auto remainder = bit_samples % rows;
            return bit_samples / rows + remainder * (slot + 1) / rows - remainder * slot / rows;
        }

        void accumulate_window(std::uint64_t window_first) {
            const auto frame = receiver_plot_frame(window_first, rate, if_hz, window_samples);
            if (power.empty()) power.assign(plot_size / 2 + 1, 0);
            for (std::size_t bin = 0; bin < power.size(); ++bin) {
                const auto linear = std::norm(frame.spectrum[bin]);
                if (std::isfinite(linear) && linear >= 0) power[bin] += linear;
            }
            ++valid_windows;
        }

        void finish_row() {
            auto db = std::make_shared<std::vector<double>>(plot_size / 2 + 1,
                -std::numeric_limits<double>::infinity());
            if (valid_windows) {
                for (std::size_t bin = 0; bin < db->size(); ++bin) {
                    const auto mean = power[bin] / valid_windows;
                    if (mean > 0 && std::isfinite(mean)) (*db)[bin] = 10 * std::log10(mean);
                }
            }
            if (value.rows.size() == value.total_rows) {
                value.rows.erase(value.rows.begin());
                ++value.first_row;
            }
            value.rows.push_back(std::move(db));
            power.clear();
            valid_windows = 0;
            window = 0;
            ++row;
        }

        const std::uint64_t bit_samples;
        std::uint64_t row_first, row=0;
        std::uint32_t rate;
        double if_hz;
        std::size_t window=0, window_filled=0, valid_windows=0;
        bool window_valid=false;
        std::array<Complex, plot_size> window_samples{};
        std::vector<double> power;
        Snapshot::SimulationWaterfall value;
    };
        std::unique_ptr<WaterfallRecorder> waterfall_recorder;
};

PatternStep pattern_step(const StreamSlotReceipt& receipt) {
    const auto& s = receipt.score;
    WrongKeyAdmissionEvidence admission_evidence;
    accumulate_wrong_key_admission(admission_evidence, s);
    PatternStep row;
    row.index = s.index;
    row.epoch = receipt.pattern_epoch;
    row.ordinal = receipt.symbol_ordinal;
    row.decision = s.decision == SlotDecision::data ? PatternDecision::data :
        s.decision == SlotDecision::quiet ? PatternDecision::quiet : PatternDecision::ambiguous;
    row.bit = s.bit;
    row.adaptive_diagnostics = s.adaptive_diagnostics;
    row.bit_evidence = s.bit_evidence;
    row.adaptive_bit_evidence = s.adaptive_bit_evidence;
    row.combined_bit_evidence = s.combined_bit_evidence;
    row.operational_bit_evidence = s.operational_bit_evidence;
    row.quiet_energy = s.quiet_energy;
    row.quiet_threshold = s.quiet_threshold;
    row.operational_log_e = s.operational_log_e;
    row.operational_threshold = s.operational_threshold;
    row.operational_bit_log_separation = s.operational_bit_log_separation;
    row.operational_minimum_bit_log_separation = s.operational_minimum_bit_log_separation;
    row.wrong_key_admission = wrong_key_admission_name(s.wrong_key_admission);
    if (admission_evidence.available && admission_evidence.data_bits == 1) {
        std::array<bool,wrong_key_admission_count> passing{};
        for (std::size_t i = 0; i < passing.size(); ++i)
            passing[i] = admission_evidence.passing_bits[i] == 1;
        row.browser_admission = passing;
    }
    row.wrong_key_required_log_separation = s.wrong_key_required_log_separation;
    row.wrong_key_rejected = s.wrong_key_rejected;
    row.helper_seconds = s.helper_seconds;
    row.helper_overlapped = s.helper_overlapped;
    row.coarse_seconds = s.coarse_seconds;
    row.coarse_workspaces = s.coarse_workspaces;
    row.coarse_frequency_points = s.coarse_frequency_points;
    row.coarse_arrival_points = s.coarse_arrival_points;
    row.coarse_projected_rate_hz = s.coarse_projected_rate_hz;
    row.fine_seconds = s.fine_seconds;
    row.quiet_modes = s.noise_statistics_valid ? s.quiet_modes : 0;
    row.fine_candidates = s.fine_candidates;
    row.workers = receipt.workers;
    row.evidence_complete = s.complete && s.all_valid && s.zero_h0_verified && s.fine_candidates > 0;
    row.helper_status = s.helper_status;
    row.varying_status = s.varying_status;
    row.drift_status = s.drift_status;
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

StreamReceiverOptions stream_options(const NativeMessageProfile& message, const StreamReceiverResources& resources,
                                     std::size_t external_memory) {
    StreamReceiverOptions options;
    // Keep one bounded completed GUI receipt outside the stream's own result
    // queue. Capture backpressure resumes when the UI consumes that receipt.
    const auto presentation = message.content_limit + 64 * 1024U;
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

StreamReceiverOptions stream_options(const Settings& settings, std::size_t external_memory) {
    StreamReceiverOptions options;
    options.memory_limit_bytes = settings.receiver_memory_limit;
    return stream_options(message_profile(settings), resolve_stream_receiver_resources(options), external_memory);
}

void receiver_ok(const StreamReceiverReport& report) {
    if (report.state == StreamReceiverState::resource_limit || report.state == StreamReceiverState::failed)
        throw Error(report.reason.empty() ? "Robust receiver could not admit the configured public window" : report.reason);
}

using detail::CaptureChunk;
using detail::CaptureQueue;

struct ReceiveDisplay {
    std::optional<std::uint64_t> pending_slot;
    std::uint64_t pending_id = 0, revision = 0;
    std::size_t observed_bits = 0, missing = 0;
    WrongKeyAdmissionEvidence wrong_key_evidence;
};

struct SimulationComparison {
    std::string sent;
    bool raw = false;
    std::optional<bool> match;
    std::string received;

    std::string describe(std::string_view value) const {
        if (raw) {
            if (value.size() <= 16) return std::string(value);
            return std::string(value.substr(0, 16)) + "… (" + std::to_string(value.size()) + " bits)";
        }
        if (value == "0" || value == "1") return "'" + std::string(value) + "'";
        return std::to_string(value.size()) + (value.size() == 1 ? " text byte" : " text bytes");
    }

    void observe(SignalUpdate& signal, std::string_view value) {
        match = value == sent;
        signal.simulation_input_result = value.empty() ? SimulationInputResult::incomplete :
            *match ? SimulationInputResult::match : SimulationInputResult::mismatch;
        received = value.empty() ? std::string{} : describe(value);
    }

    std::string summary() const {
        auto result = "sent " + describe(sent);
        if (!match) return result + " · no complete reception from the first DATA slot";
        if (received.empty()) return result + (raw ? " · incomplete received bits" : " · no decoded text");
        return result + " · received " + received + (*match ? " · match" : " · mismatch");
    }
};

} // namespace

struct Session::Impl {
    const GpuPolicy gpu_policy;
    mutable execution::Mutex mutex;
    execution::StopCondition changed;
    execution::Task manager;
    std::stop_source generation_stop, tx_stop;
    Settings settings;
    Snapshot current;
    // Retain the runtime discovery result across settings generations.
    bool gpu_available = false;
    std::string gpu_unavailable_reason;
    std::vector<SignalUpdate> signal_events;
    std::vector<transfer::Received> received_events;
    std::shared_ptr<const Profile> profile;
    std::optional<TxRequest> request;
    std::optional<SimulationReplay> retained_replay;
    std::shared_ptr<SimulationDetailCapture> simulation_detail_capture;
    std::optional<SimulationPreview> simulation_preview;
    std::optional<Clock::time_point> simulation_started;
    bool replay_requested = false;
    std::uint64_t generation = 0, next_id = 1, spectrum_revision = 0;
    std::uint64_t plot_segment = 0;
    std::uint64_t next_simulation_ordinal = 0, last_simulation_epoch = 0;
    bool started = false, suspended = false, stopping = false, stopped = false, resources_owned = false;
    std::map<KeyTag, long double> used_until;
    long double separated_after = 0;
    // Accessed only under mutex; the receive owner publishes committed quiet
    // receipts and the exact dated geometry to the transmit owner. This state
    // belongs to one settings generation and one finite capture stream.
    struct FiniteCapture {
        std::optional<PublicProfile> waveform;
        std::function<void(std::uint64_t,std::stop_token)> stop_at;
        std::int64_t first = 0;
        std::uint64_t halo = 0, last_quiet = 0, endpoint = 0;
        unsigned quiet = 0;
        bool ready = false, ended = false, claimed = false, planned = false;
        bool cutoff_accepted = false, exact_input_drained = false, physical_complete = false;
    } finite;

    explicit Impl(GpuPolicy policy) : gpu_policy(policy) {}

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

    void record_gpu_availability(std::string reason = {}) {
        std::lock_guard lock(mutex);
        gpu_available = reason.empty();
        gpu_unavailable_reason = std::move(reason);
    }

    std::uint64_t new_plot_segment() {
        std::lock_guard lock(mutex);
        simulation_preview.reset();
        return ++plot_segment;
    }

    void commit_plot(Snapshot& out, PlotFrame frame, bool receiver_input, std::uint64_t segment,
                     std::optional<Snapshot::ReplayWaterfall> waterfall) {
        out.replay_waterfall = std::move(waterfall);
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
    }

    void set_plot(std::uint64_t owner, std::stop_token stop, PlotFrame frame, bool receiver_input,
                  std::uint64_t segment, const SimulationReplay* replay = nullptr,
                  bool replaying = false) {
        publish(owner, stop, [&](Snapshot& out) {
            if (replaying && !out.simulation_replay) {
                out.simulation_replay = true;
                out.status = "Robust simulation processed · replaying 3 seconds";
            }
            commit_plot(out, std::move(frame), receiver_input, segment,
                        replay ? std::optional(replay->waterfall) : std::nullopt);
        });
    }

    void preview_receiver_input(std::uint64_t owner, std::stop_token stop, std::uint64_t segment,
                                std::uint64_t first, std::uint32_t rate, double if_hz,
                                std::span<const Complex> samples, const SimulationReplay* replay) {
        const auto size = std::min(simulation_waveform_size, samples.size());
        const auto tail = samples.last(size);
        SimulationPreview preview{owner, segment, first + samples.size() - size, rate, if_hz, stop,
            replay ? std::optional(replay->waterfall) : std::nullopt, {}};
        if (size >= plot_size) preview.input.assign(tail.begin(), tail.end());
        publish(owner, stop, [&](Snapshot& out) {
            if (out.simulation_progress) out.simulation_progress->input_samples = first + samples.size();
            // The GUI can miss any count of preview frames. It takes only
            // the freshest accepted input window and projects it itself.
            if (!preview.input.empty()) simulation_preview = std::move(preview);
        });
    }

    void trace_emission(const Emission& emission, const Profile& active, std::uint64_t sample, bool noise,
                        EmissionTraceCursor& cursor, std::uint64_t owner, std::stop_token stop) {
        auto pattern = emission.message_pattern_preview(cursor.pattern_ordinal);
        auto hop = illustrated_hop(active, sample, noise, cursor.hop_dwell);
        if (!pattern && !hop) return;
        publish(owner, stop, [&](Snapshot& out) {
            auto& trace = out.transmit_trace;
            if (pattern) {
                cursor.pattern_ordinal = pattern->ordinal;
                trace.pattern = std::move(pattern);
            }
            if (hop) {
                cursor.hop_dwell = hop->dwell;
                trace.fhss_available = true;
                trace.fhss_epoch = active.waveform.origin().epoch;
                trace.fhss_hop = hop->dwell;
                trace.fhss_cycle = hop->cycle;
                trace.fhss_byte_offset = hop->cycle * 8;
                trace.fhss_channel = hop->channel;
                trace.fhss_channels = static_cast<unsigned>(active.hop_shifts_hz.size());
                trace.fhss_shift_hz = hop->shift;
                trace.fhss_key = std::move(hop->keystream);
            }
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
                 std::uint64_t owner, std::stop_token stop, SimulationComparison* comparison = nullptr) {
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
            if (s.finite_duplex && !finite.planned && !finite.ended) {
                for (const auto& row : slots) {
                    const bool quiet = QuietSlotMessageReceiver::classification(row.score) == SlotDecision::quiet;
                    if (!quiet) finite.quiet = 0;
                    else if (finite.quiet && finite.last_quiet != UINT64_MAX && row.score.index == finite.last_quiet + 1)
                        finite.quiet = std::min(2U, finite.quiet + 1);
                    else finite.quiet = 1;
                    finite.last_quiet = row.score.index;
                }
                finite.ready = finite.quiet >= 2 && !report.recovering &&
                    !report.episode_first_public_slot && !report.pending_messages &&
                    report.state == StreamReceiverState::receiving;
            }
            for (const auto& row : slots) {
                if (out.pattern_steps.size() == history_size) out.pattern_steps.erase(out.pattern_steps.begin());
                out.pattern_steps.push_back(pattern_step(row));
            }
            for (auto& event : messages) {
                auto& message = event.message;
                if (!message.physical_complete) continue;
                if (s.finite_duplex && finite.planned && finite.cutoff_accepted)
                    finite.physical_complete = true;
                const bool matched = display.pending_slot == event.first_public_slot;
                SignalUpdate signal;
                signal.id = matched ? display.pending_id : next_id++;
                signal.revision = matched ? display.revision + 1 : 1;
                signal.frequency_hz = s.transfer.modem.carrier_hz;
                signal.complete = true;
                signal.robust_reception = true;
                signal.wrong_key_evidence = message.wrong_key_evidence;
                signal.received_bits = message.observed_bits;
                signal.missing_symbols = message.missing_slots;
                // Raw presentation requires an exact physical bit sequence.
                if (message.raw_bits_complete())
                    for (const auto bit : message.received_bits) signal.raw_bits.push_back(bit ? '1' : '0');
                signal.simulation_raw_input = comparison && comparison->raw;
                if (comparison && event.first_public_slot == 0) {
                    // Compare only after the native receiver has completed a
                    // real episode. The local draft never affects scoring.
                    if (comparison->raw) comparison->observe(signal, signal.raw_bits);
                    else if (message.decoded_content) {
                        const auto& data = message.decoded_content->message.data;
                        comparison->observe(signal, {reinterpret_cast<const char*>(data.data()), data.size()});
                    } else comparison->observe(signal, {});
                } else if (signal.simulation_raw_input)
                    signal.simulation_input_result = SimulationInputResult::incomplete;
                if (message.decoded_content && !signal.simulation_raw_input) {
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
            // A missing or changed episode identity means the old pending
            // episode has been abandoned. This also catches recovery and
            // rearm that both happened between UI deliveries. Never create a
            // row from receiver state alone.
            if (display.pending_slot && display.pending_slot != report.episode_first_public_slot) {
                SignalUpdate signal;
                signal.id = display.pending_id;
                signal.revision = ++display.revision;
                signal.frequency_hz = s.transfer.modem.carrier_hz;
                signal.received_bits = display.observed_bits;
                signal.missing_symbols = display.missing;
                signal.robust_reception = true;
                signal.wrong_key_evidence = display.wrong_key_evidence;
                signal.text = report.recovering
                    ? "Robust episode rejected; listening for two quiet slots"
                    : "Robust episode rejected";
                append_signal(std::move(signal));
                display = {};
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
            display.wrong_key_evidence = report.wrong_key_evidence;
            SignalUpdate signal;
            signal.id = display.pending_id;
            signal.revision = ++display.revision;
            signal.frequency_hz = s.transfer.modem.carrier_hz;
            signal.received_bits = display.observed_bits;
            signal.missing_symbols = display.missing;
            signal.robust_reception = true;
            signal.wrong_key_evidence = display.wrong_key_evidence;
            signal.text = "Receiving Robust content";
            append_signal(std::move(signal));
        });
        if (s.finite_duplex) changed.notify_all();
    }

    void accept(CompleteStreamReceiver& receiver, ReceiveDisplay& display, const Settings& s,
                std::uint64_t first, std::span<const Complex> samples, std::span<const std::uint8_t> validity,
                std::uint64_t owner, std::stop_token stop, SimulationComparison* comparison = nullptr) {
        while (!stop.stop_requested()) {
            receiver_ok(receiver.report());
            if (receiver.push(first, samples, validity) == StreamPushResult::accepted) break;

            // Observe the actual rejection cause after push(): it may have
            // committed work before reporting backpressure. Drain queued
            // output first, then wait for only one owner completion before
            // retrying this unchanged chunk.
            const auto rejected = receiver.report();
            receiver_ok(rejected);
            deliver(receiver, display, s, owner, stop, comparison);
            auto after = receiver.report();
            if (!rejected.output_backpressured) {
                after = receiver.wait_for_slot_completion();
                receiver_ok(after);
                deliver(receiver, display, s, owner, stop, comparison);
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
        deliver(receiver, display, s, owner, stop, comparison);
    }

    void finish_input(CompleteStreamReceiver& receiver, ReceiveDisplay& display, const Settings& s,
                      std::uint64_t owner, std::stop_token stop, SimulationComparison* comparison = nullptr) {
        for (;;) {
            cancelled(stop);
            const auto progress = receiver.finish_input();
            receiver_ok(progress);
            deliver(receiver, display, s, owner, stop, comparison);
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
            signal.robust_reception = true;
            signal.wrong_key_evidence = display.wrong_key_evidence;
            signal.text = "Robust input ended before physical completion";
            append_signal(std::move(signal));
        });
        display = {};
    }

    void receive_live(const Profile& base, const Settings& s, std::optional<detail::NoiseCalibration>& noise_baseline,
                      std::uint64_t owner, std::stop_token stop) {
        const auto rate = s.transfer.modem.sample_rate;
        const auto transmit_reservation = live_transmit_reservation_checked(s, base);
        AudioInput input(rate, base.receiver.data_if_hz, base.minimum_audio_hz, base.maximum_audio_hz);
        PlotMeter meter(rate);
        const auto segment = new_plot_segment();
        CaptureQueue queue;
        std::function<void(std::uint64_t,std::stop_token)> finite_stop_at;
        const auto queue_limit = static_cast<std::size_t>(rate) * 2;
        constexpr auto overhead = 2 * 1024 * 1024U;
        const auto ingress_workspace = overhead + queue_limit * sizeof(float) + input.working_bytes();
        const auto calibration_rows = s.auto_live_noise && !noise_baseline ? detail::noise_calibration_samples(rate) : 0;
        const auto calibration_deadline = Clock::now() + std::chrono::seconds(15);
        std::vector<float> calibration;
        if (calibration_rows) publish(owner, stop, [](Snapshot& out) {
            if (!out.transmitting) out.status = calibrating_live_noise;
        });
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
                    std::unique_lock lock(queue.mutex);
                    return queue.push(lock, samples, queue_limit,
                        std::min(audio_chunk, static_cast<std::size_t>(rate) / 20), stop, capture_stop);
                }, capture_stop, [&](const audio::StreamFormat& format) {
                    if (format.usable_passband_hz < base.maximum_audio_hz)
                        throw Error(passband_error("receive", format, base.maximum_audio_hz));
                    if (s.finite_duplex && (!format.capture_callback_may_block || !format.capture_stop_at))
                        throw Error("Finite Robust duplex requires a bounded, dated capture provider");
                    std::lock_guard lock(queue.mutex);
                    queue.provider_bytes = format.workspace_bytes;
                    queue.capture_callback_may_block = format.capture_callback_may_block;
                    finite_stop_at = format.capture_stop_at;
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
        std::uint64_t consumed_capture_frames = 0;
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
                std::function<void(std::uint64_t,std::stop_token)> capture_endpoint;
                {
                    std::unique_lock lock(queue.mutex);
                    queue.changed.wait_for(lock, stop, std::chrono::milliseconds(30),
                        [&] { return queue.done || !queue.error.empty() || !queue.chunks.empty(); });
                    if (stop.stop_requested()) break;
                    if (!queue.error.empty()) throw Error(queue.error);
                    if (!queue.chunks.empty()) {
                        chunk = queue.pop();
                        provider_bytes = queue.provider_bytes;
                        if (s.finite_duplex && !origin_stamp) capture_endpoint = finite_stop_at;
                    } else if (queue.done) {
                        if (receiver) {
                            // Only already accepted complete windows can be
                            // scored. EOF never invents absent waveforms.
                            lock.unlock();
                            finish_input(*receiver, display, s, owner, stop);
                        }
                        if (calibration_rows && !noise_baseline)
                            throw Error("Audio ended during Robust noise calibration. Check the input device or enter a measured noise density.");
                        break;
                    }
                }
                if (calibration_rows && !noise_baseline && Clock::now() >= calibration_deadline)
                    throw Error("Robust noise calibration did not receive enough audio. Check the input device or enter a measured noise density.");
                if (chunk.pcm.empty()) {
                    if (receiver) {
                        receiver_ok(receiver->resume(false));
                        deliver(*receiver, display, s, owner, stop);
                    }
                    continue;
                }
                if (chunk.first != consumed_capture_frames ||
                    chunk.pcm.size() > UINT64_MAX - consumed_capture_frames)
                    throw Error("Robust capture frame sequence is invalid");
                consumed_capture_frames += chunk.pcm.size();
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
                if (origin_stamp) {
                    // Check one dated timeline, including calibration input.
                    const auto seconds = static_cast<long double>(chunk.first) / rate;
                    const auto expected = origin_stamp->utc_seconds + seconds;
                    const auto allowance = origin_stamp->uncertainty_seconds + stamp.uncertainty_seconds +
                        seconds * base.waveform.config().audio_pair_ppm * .5e-6L + 2.L / rate;
                    if (std::abs(stamp.utc_seconds - expected) > allowance)
                        throw Error("Robust input timestamps no longer fit the configured oscillator and capture date");
                } else {
                    origin_stamp = stamp;
                }
                if (calibration_rows && !noise_baseline) {
                    if (calibration.empty()) {
                        const auto external = checked_workspace(transmit_reservation, checked_workspace(provider_bytes, ingress_workspace));
                        const auto available = *stream_options(s, external).memory_limit_bytes;
                        const auto required = checked_workspace(triad::v3::detail::stream_controller_footprint_bytes(),
                            detail::noise_calibration_workspace_bytes(rate));
                        if (required > available) throw Error("Robust startup noise calibration exceeds the receiver memory limit");
                        calibration.reserve(calibration_rows);
                    }
                    const auto count = std::min(calibration_rows - calibration.size(), chunk.pcm.size());
                    calibration.insert(calibration.end(), chunk.pcm.begin(), chunk.pcm.begin() + count);
                    if (calibration.size() == calibration_rows) {
                        noise_baseline = detail::calibrate_noise_dbfs_hz(calibration, rate, stop);
                        std::vector<float>{}.swap(calibration);
                        publish(owner, stop, [&](Snapshot& out) {
                            out.live_noise_baseline_dbfs_hz = noise_baseline->noise_dbfs_hz;
                        });
                    }
                    // Start the FIR on the next dated chunk. Discarded samples
                    // and partial slots cannot contribute quiet evidence.
                    continue;
                }
                if (!receiver) {
                    auto [adjusted, first_sample, slot, first] = detail::plan_dated_receive_start(base, s.auto_calendar,
                        stamp.utc_seconds, stamp.uncertainty_seconds, input.halo_samples());
                    if (noise_baseline) {
                        input.set_dc_offset(noise_baseline->dc_offset);
                        detail::apply_live_noise_model(adjusted.receiver, s.target_cn0_db_hz, noise_baseline->noise_dbfs_hz);
                    }
                    capture_first = first_sample;
                    observation_first = first;
                    auto options = stream_options(s, checked_workspace(transmit_reservation,
                        checked_workspace(provider_bytes, ingress_workspace)));
                    options.first_slot = slot;
                    options.first_input_sample = first;
                    options.require_quiet_rearm = slot > 0;
                    receiver = std::make_unique<CompleteStreamReceiver>(adjusted.waveform, adjusted.receiver,
                        adjusted.key, options, message_profile(s));
                    interrupt.emplace(stop, [owned = receiver.get()] { owned->request_cancel(); });
                    if (s.finite_duplex) {
                        if (!capture_endpoint) throw Error("Finite Robust capture endpoint is unavailable");
                        {
                            std::lock_guard lock(mutex);
                            if (owner != generation || stop.stop_requested() || stopping || finite.ended)
                                throw Error("Finite Robust capture ended before planning");
                            finite.waveform = adjusted.waveform;
                            finite.first = capture_first;
                            finite.halo = input.halo_samples();
                            finite.stop_at = std::move(capture_endpoint);
                        }
                        changed.notify_all();
                    }
                    publish(owner, stop, [&](Snapshot& out) {
                        out.receiver_error.clear();
                        out.receiver_workers = receiver->report().resources.workers;
                        if (s.auto_live_noise && s.full_duplex) out.transmit_unavailable.reset();
                        if (!out.transmitting) {
                            out.status = s.auto_calendar ? "Listening on the automatic Robust UTC calendar" :
                                "Listening on the configured Robust calendar";
                            if (stamp.quality != audio::TimingQuality::bounded) out.status += " with estimated audio dates";
                        }
                    });
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
            if (s.finite_duplex && !stop.stop_requested()) {
                std::lock_guard lock(mutex);
                if (owner == generation)
                    finite.exact_input_drained = finite.cutoff_accepted &&
                        consumed_capture_frames == finite.endpoint;
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

    std::optional<SimulationReplay> simulate(const Profile& prepared, const Settings& s,
                  const TxRequest& send, std::uint64_t owner, std::stop_token stop, SimulationComparison* comparison) {
        const auto began = Clock::now();
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
        std::unique_ptr<CompleteStreamReceiver> receiver;
        std::optional<StreamReceiverResources> resources;
        std::optional<NativeMessageProfile> message;
        std::size_t fixed_workspace = 0;
        if (!noise) {
            config.minimum_capture_samples = calendar.slot(end_slot).observation.stop;
            message = message_profile(s);
            StreamReceiverOptions limits;
            limits.memory_limit_bytes = s.receiver_memory_limit;
            resources = resolve_stream_receiver_resources(limits);
            fixed_workspace = checked_workspace(retained_request_workspace(send), checked_workspace(
                retained_replay ? retained_replay->working_bytes() : 0, 2 * 1024 * 1024U));
            if (resources->workers > 1) {
                try {
                    const auto waterfall_workspace = send.mode == EmissionMode::Message &&
                        calendar.geometry().bit_seconds >= 60 ? simulation_waterfall_workspace_bytes() : 0;
                    const auto optional_workspace = checked_workspace(StreamingChannel::maximum_working_bytes,
                        checked_workspace(simulation_replay_rows(rate) *
                                (sizeof(Complex) + sizeof(std::uint8_t)),
                            checked_workspace(waterfall_workspace, simulation_detail_workspace_bytes(rate))));
                    const auto options = stream_options(*message, *resources,
                        checked_workspace(fixed_workspace, optional_workspace));
                    if (triad::v3::detail::stream_simulation_workspace_fits(calendar, prepared.receiver,
                            options, *message, end_slot, optional_workspace,
                            Clock::now() + simulation_workspace_preflight_budget, stop)) {
                        config.lanes = {resources->workers, [&receiver, stop](std::size_t count, const auto& job) {
                            if (receiver && receiver->try_simulation_jobs(count, job)) return;
                            for (std::size_t i = 0; i < count; ++i) {
                                cancelled(stop);
                                job(i);
                            }
                        }};
                    }
                } catch (const Error&) {
                    // The optional channel bound is conservative. The direct
                    // reader still gets admission with its actual workspace.
                }
            }
        }
        const auto source = emission.source();
        const auto source_stop = source.geometry.source_samples;
        StreamingChannel channel(source, config);
        std::optional<SimulationReplay> replay;
        auto tx_slot = calendar.transmit_slot(0);
        const auto segment = new_plot_segment();
        auto detail_capture = std::make_shared<SimulationDetailCapture>(segment, 0, rate,
            prepared.receiver.data_if_hz);
        publish(owner, stop, [&](Snapshot& out) {
            simulation_detail_capture = detail_capture;
            out.simulation_waterfall.reset();
            out.simulation_detail.reset();
        });
        if (!noise) {
            // In a message preview, include DATA even when a long Helper
            // lead precedes it. Helper-only previews begin at Helper onset.
            const auto activity = send.mode == EmissionMode::HelperOnly ? tx_slot.helper_begin : tx_slot.pattern_begin;
            replay.emplace(prepared, s, config.effects, calendar.calendar().distance(calendar.origin(), activity),
                channel.total_samples());
            if (send.mode == EmissionMode::Message)
                replay->enable_waterfall(segment, calendar.geometry().bit_seconds);
            // Channel working bytes include the waveform source but exclude
            // the prepared text shared by its lazy interval reader and the
            // new presentation slice and retained excerpt beside the receiver.
            const auto options = stream_options(*message, *resources, checked_workspace(fixed_workspace,
                checked_workspace(checked_workspace(channel.report().working_bytes, replay->working_bytes()),
                    simulation_detail_workspace_bytes(rate))));
            // This uses the same public plan and route selector as native
            // stream admission. Only reject a first window whose DSP bound
            // alone exceeds the receiver budget after actual external costs.
            triad::v3::detail::check_simulation_first_window_workspace(calendar, prepared.receiver, options);
            receiver = std::make_unique<CompleteStreamReceiver>(calendar, prepared.receiver, prepared.key, options, *message);
            const auto initial_waterfall = replay->waterfall_snapshot();
            publish(owner, stop, [&](Snapshot& out) {
                simulation_started = began;
                out.simulation_progress = Snapshot::SimulationProgress{0, channel.total_samples(), rate};
                out.simulation_waterfall = initial_waterfall;
                out.status = "Simulating receiver input";
            });
            replay->allocate();
            publish(owner, stop, [&](Snapshot& out) { out.receiver_workers = receiver->report().resources.workers; });
        }
        std::stop_callback interrupt(stop, [&] { if (receiver) receiver->request_cancel(); });
        std::array<Complex, audio_chunk> iq{};
        std::array<std::uint8_t, audio_chunk> valid{};
        ReceiveDisplay display;
        std::size_t generated_bits = 0;
        EmissionTraceCursor trace_cursor;
        const auto noise_started = Clock::now();
        auto next_preview = noise_started;
        bool scoring_tail = false;
        try {
            while (!stop.stop_requested()) {
                execution::checkpoint();
                const auto first = channel.position();
                const auto generated_before = emission.report().generated_samples;
                const auto count = channel.read(iq, valid);
                if (!count) break;
                const auto generated = emission.report().generated_samples;
                if (generated != generated_before)
                    trace_emission(emission, prepared, generated - 1, noise, trace_cursor, owner, stop);
                const auto samples = std::span(iq).first(count);
                if (receiver) accept(*receiver, display, s, first, samples, std::span(valid).first(count),
                                     owner, stop, comparison);
                if (!stop.stop_requested()) detail_capture->append(first, samples, std::span(valid).first(count));
                if (replay && replay->record(first, samples, std::span(valid).first(count))) {
                    const auto waterfall = replay->waterfall_snapshot();
                    publish(owner, stop, [&](Snapshot& out) { out.simulation_waterfall = waterfall; });
                }
                const auto now = Clock::now();
                if (now >= next_preview) {
                    next_preview = now + simulation_preview_period;
                    preview_receiver_input(owner, stop, segment, first, rate,
                        prepared.receiver.data_if_hz, samples, replay ? &*replay : nullptr);
                }
                if (send.mode == EmissionMode::Message) {
                    const auto previous_bits = generated_bits;
                    while (generated_bits < wire_bits &&
                           generated >= calendar.calendar().distance(calendar.origin(), tx_slot.support_end)) {
                        ++generated_bits;
                        if (generated_bits < wire_bits) tx_slot = calendar.transmit_slot(generated_bits);
                    }
                    if (previous_bits != generated_bits)
                        publish(owner, stop, [&](Snapshot& out) {
                            out.transmit_trace.generated_bits = generated_bits;
                            ++out.transmit_trace.revision;
                        });
                }
                if (!noise && !scoring_tail && generated >= source_stop) {
                    scoring_tail = true;
                    publish(owner, stop, [](Snapshot& out) {
                        out.transmit_trace.emitting = false;
                        ++out.transmit_trace.revision;
                        out.status = "Simulating the receive tail and scoring reception";
                    });
                }
                if (noise) {
                    // An open-ended noise command is played at its physical rate
                    // so its source/calendar does not race ahead of the user.
                    const auto due = noise_started + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(static_cast<double>(channel.position()) / rate));
                    while (!stop.stop_requested() && Clock::now() < due)
                        execution::sleep_until(std::min(due, Clock::now() + std::chrono::milliseconds(30)));
                }
            }
            cancelled(stop);
            if (receiver) {
                publish(owner, stop, [&](Snapshot& out) {
                    out.simulation_progress->input_samples = channel.position();
                    out.status = "Finishing simulated receiver scoring";
                });
                finish_input(*receiver, display, s, owner, stop, comparison);
                invalidate_pending(display, s, owner);
                publish(owner, stop, [&](Snapshot& out) {
                    out.receiver_workers = 0;
                    out.simulation_progress->complete = true;
                    out.simulation_progress->elapsed_seconds =
                        std::chrono::duration<double>(Clock::now() - began).count();
                    simulation_started.reset();
                });
            }
        } catch (...) {
            if (receiver) receiver->request_cancel();
            invalidate_pending(display, s, owner);
            throw;
        }
        return replay;
    }

    void replay_simulation(const SimulationReplay& replay, std::uint64_t owner, std::stop_token stop) {
        const auto input = replay.samples();
        if (input.size() < plot_size || stop.stop_requested()) return;
        const auto rate = replay.rate;
        const auto playback_extent = std::min<std::size_t>(input.size(), static_cast<std::size_t>(rate) * 3);
        const auto input_validity = replay.valid_samples().first(playback_extent);
        const auto last = input.size() - plot_size;
        const auto segment = new_plot_segment();
        auto detail_capture = std::make_shared<SimulationDetailCapture>(segment, replay.first,
            rate, replay.data_if_hz);
        std::size_t detail_cursor = 0;
        const auto append_detail_to = [&](std::size_t target) {
            target = std::min(target, playback_extent);
            if (target <= detail_cursor) return;
            const auto count = target - detail_cursor;
            detail_capture->append(replay.first + detail_cursor, input.subspan(detail_cursor, count),
                input_validity.subspan(detail_cursor, count));
            detail_cursor = target;
        };
        const auto wait_until = [&](Clock::time_point time) {
            std::unique_lock lock(mutex);
            changed.wait_until(lock, stop, time, [] { return false; });
        };
        const auto started = Clock::now();
        constexpr auto frames = simulation_replay_duration / simulation_replay_period;
        const auto pattern_step_count = replay.pattern_steps ? replay.pattern_steps->size() : 0;
        publish(owner, stop, [&](Snapshot& out) {
            simulation_detail_capture = detail_capture;
            out.simulation_detail.reset();
            out.simulation_waterfall = replay.waterfall_snapshot();
            out.simulation_pattern_replay_steps = replay.pattern_steps;
            out.simulation_pattern_replay_active = pattern_step_count != 0;
            out.simulation_pattern_replay_visible_steps = 0;
            out.simulation_pattern_replay_elapsed_ms = 0;
        });
        for (std::chrono::milliseconds::rep tick = 0; tick < frames; ++tick) {
            wait_until(started + tick * simulation_replay_period);
            if (stop.stop_requested()) return;
            // Slow presentation consumers can skip frames. Never build a
            // backlog or let the replay delay the end of this session.
            tick = std::max(tick, (Clock::now() - started) / simulation_replay_period);
            if (tick >= frames) {
                append_detail_to(playback_extent);
                return;
            }
            const auto offset = static_cast<std::size_t>((tick * simulation_replay_period).count()) * rate / 1000;
            if (offset > last) return;
            const auto next_detail = std::min(playback_extent, offset +
                static_cast<std::size_t>(simulation_replay_period.count()) * rate / 1000);
            append_detail_to(next_detail);
            const auto first = replay.first + offset;
            const auto count = std::min(simulation_waveform_size, input.size() - offset);
            const auto samples = input.subspan(offset, count);
            const auto elapsed = static_cast<std::uint64_t>(tick * simulation_replay_period.count());
            const auto visible_steps = pattern_step_count * static_cast<std::size_t>(tick + 1) /
                static_cast<std::size_t>(frames);
            publish(owner, stop, [&](Snapshot& out) {
                out.simulation_pattern_replay_visible_steps = visible_steps;
                out.simulation_pattern_replay_elapsed_ms = elapsed;
            });
            set_plot(owner, stop, receiver_plot_frame(first, rate, replay.data_if_hz, samples),
                     true, segment, &replay, true);
        }
        wait_until(started + simulation_replay_duration);
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
        const auto beginning = helper ? utc_time(slot.helper_begin, rate) :
            static_cast<long double>(slot.pattern_begin.epoch);
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
            // The DATA key includes the whole-second epoch. A fractional
            // cadence change cannot reopen a reserved epoch in this session.
            const auto through = helper ? utc_time(slot.helper_end, rate) :
                static_cast<long double>(slot.pattern_begin.epoch);
            used = std::max(used, through);
        }
        const auto following = add_slot(index, 3);
        separated_after = following <= calendar.last_slot()
            ? std::max(separated_after, utc_time(calendar.transmit_slot(following).helper_begin, rate))
            : 4294967296.L;
    }

    void play_live(const Profile& active, const Settings& s, const TxRequest& send, bool startup_quiet,
                   std::uint64_t owner, std::stop_token stop) {
        if (!audio::utc_follow_supported())
            throw Error("The selected audio provider cannot schedule the configured Robust calendar");
        if (s.finite_duplex) {
            if (send.mode != EmissionMode::Message)
                throw Error("Finite Robust duplex permits one message transmission only");
            std::unique_lock lock(mutex);
            changed.wait(lock, stop, [&] {
                return owner != generation || stopping || suspended || finite.ended || finite.claimed ||
                    (finite.ready && finite.waveform && finite.stop_at);
            });
            if (stop.stop_requested() || owner != generation || stopping || suspended)
                throw Error("Finite Robust duplex cancelled before quiet rearm");
            if (finite.ended || finite.claimed || !finite.ready || !finite.waveform || !finite.stop_at)
                throw Error("Finite Robust capture ended or was already used before transmission");
            finite.claimed = true;
        }
        const auto rate = s.transfer.modem.sample_rate;
        const auto& calendar = active.waveform;
        const auto noise = send.mode == EmissionMode::Noise;
        const auto wire_bits = send.wire_bits();
        std::optional<Emission> emission;
        std::uint64_t first_slot = 0, last_slot = 0, source_first = 0, next_reservation = 0;
        std::size_t generated_bits = 0;
        EmissionTraceCursor trace_cursor;
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
            if (count && !begin) publish(owner, stop, [](Snapshot& snapshot) {
                snapshot.transmit_trace.emitting = true;
                ++snapshot.transmit_trace.revision;
            });
            if (count) trace_emission(*emission, active, end - 1, noise, trace_cursor, owner, stop);
            if (auto frame = meter.update(source_first + begin, out.first(count), {})) {
                if (!s.full_duplex || !live_receive_enabled(s))
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
                throw Error(passband_error("transmit", format, active.maximum_audio_hz));
            if (s.full_duplex && live_receive_enabled(s) &&
                format.workspace_bytes > playback_provider_workspace)
                throw Error("Robust playback provider exceeded the full-duplex audio workspace allowance");
            if (emission) return;
            // The provider has opened and completed timing preflight. Allow
            // bounded source preparation before the scheduled first sample.
            constexpr auto startup_margin_seconds = 3;
            const auto lead = audio::minimum_lead_seconds() + startup_margin_seconds;
            first_slot = earliest_tx_slot(active, lead);
            if (startup_quiet) {
                // Queued preparation and playback preflight already leave
                // the peer time to collect quiet audio. Count from Send,
                // with room for calibration and the full observation edge.
                const auto calibration = static_cast<long double>(detail::noise_calibration_samples(rate)) / rate;
                const auto halo = AudioInput::required_halo_samples(rate, active.receiver.data_if_hz,
                    active.minimum_audio_hz, active.maximum_audio_hz);
                const auto peer_ready = send.requested_utc + startup_margin_seconds + calibration;
                const auto peer = detail::plan_dated_receive_start(active, true, peer_ready, lag / 2, halo);
                first_slot = std::max(first_slot, add_slot(peer.first_slot, 2));
                if (first_slot > calendar.last_slot())
                    throw Error("No Robust transmission slot remains after the startup quiet interval");
            }
            {
                std::lock_guard lock(mutex);
                if (s.finite_duplex) {
                    if (finite.ended || !finite.claimed || !finite.ready)
                        throw Error("Finite Robust quiet rearm was lost before TX planning");
                    first_slot = std::max(first_slot, add_slot(finite.last_quiet, 1));
                    if (first_slot > calendar.last_slot())
                        throw Error("Finite Robust capture has no public TX slot after quiet rearm");
                }
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
            if (s.finite_duplex) {
                std::function<void(std::uint64_t,std::stop_token)> stop_at;
                std::uint64_t endpoint = 0;
                {
                    std::lock_guard lock(mutex);
                    if (owner != generation || stopping || suspended || finite.ended ||
                        !finite.claimed || !finite.waveform || !finite.stop_at)
                        throw Error("Finite Robust capture ended before the TX plan was admitted");
                    endpoint = detail::finite_capture_frames(*finite.waveform, last_slot, finite.first, finite.halo);
                    if (!endpoint) throw Error("Finite Robust capture endpoint is already past");
                    stop_at = finite.stop_at;
                }
                // The provider waits for the physical capture owner to accept
                // this exact endpoint and its configured local resource bound.
                // No private emission or playback schedule exists before ACK.
                stop_at(endpoint, stop);
                cancelled(stop);
                {
                    std::lock_guard lock(mutex);
                    if (owner != generation || stopping || suspended || finite.ended ||
                        !finite.ready || finite.last_quiet >= first_slot)
                        throw Error("Finite Robust capture ended before TX scheduling");
                    finite.endpoint = endpoint;
                    finite.cutoff_accepted = true;
                }
            }
            next_reservation = first_slot;
            const auto first = calendar.transmit_slot(first_slot);
            source_first = calendar.calendar().distance(calendar.origin(), first.helper_begin);
            emission.emplace(make_emission(send, active, first_slot, slots, source_first, owner, stop));
            if (s.full_duplex && live_receive_enabled(s) &&
                emission->source().working_bytes > checked_workspace(
                    calendar.emission_source_workspace_bytes(), playback_controller_workspace))
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
            if (s.finite_duplex) {
                std::lock_guard lock(mutex);
                if (owner != generation || stopping || suspended || finite.ended ||
                    !finite.ready || finite.last_quiet >= first_slot)
                    throw Error("Finite Robust quiet rearm changed before TX scheduling");
            }
            audio::schedule_output(static_cast<double>(start), stop);
            if (s.finite_duplex) {
                std::lock_guard lock(mutex);
                if (owner != generation || stopping || suspended || finite.ended ||
                    !finite.ready || finite.last_quiet >= first_slot)
                    throw Error("Finite Robust quiet rearm changed while TX was scheduled");
                finite.planned = true;
            }
            publish(owner, stop, [&](Snapshot& out) {
                out.status = std::string(startup_quiet ? "Leaving two quiet slots before first Robust transmission. Scheduled for UTC second " :
                    "Robust audio scheduled for UTC second ") + std::to_string(first.helper_begin.epoch);
            });
        }, s.channel_mode, std::move(options));
    }

    std::optional<std::string> basic_transmit_reason(const Settings& s) const {
#ifdef DATAPUMP_EXECUTION_FIBERS
        (void)s;
        return "Robust needs the desktop native receiver workers. Use the native application for Robust.";
#else
        if (s.simulation) return {};
        if (!s.calendar && !s.auto_calendar) return "Choose Auto or enter a shared UTC anchor in Robust Setup.";
        if (!audio::utc_follow_supported())
            return "This audio provider cannot schedule a dated Robust calendar. Simulation is available.";
        return {};
#endif
    }

    void lock_status(Snapshot& out) const {
        out.force_transmit_available = false;
        out.noise_transmit_unavailable = out.transmit_unavailable;
        if (!profile || settings.simulation || out.transmitting || suspended || stopping ||
            (settings.auto_live_noise && settings.full_duplex && !out.receiver_workers)) return;
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
                current.gpu_device_uuid.reset();
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
                current.gpu_vulkan_timed_submissions = 0;
                current.gpu_vulkan_device_command_seconds = 0;
                current.gpu_native_coarse_workspace_sessions = 0;
                current.gpu_native_coarse_precision_sessions = 0;
                current.gpu_host_scored_rows = 0;
                current.gpu_statistics_blocks = 0;
                current.gpu_cuda_fp16_calls = 0;
                current.gpu_cuda_int8_calls = 0;
                current.gpu_cuda_nvfp4_fine_modes = 0;
                current.gpu_cuda_cufft_fine_modes = 0;
                current.gpu_opencl_nvfp4_fine_modes = 0;
                current.gpu_cuda_half_fft_score_rows = 0;
                current.gpu_cuda_half_fft_fallback_summary.clear();
                current.gpu_native_lag_preference_supported = false;
                current.receiver_workers = 0;
                current.live_noise_baseline_dbfs_hz.reset();
            }
            std::stop_callback shutdown(manager_stop, [source = generation_stop]() mutable { source.request_stop(); });
            std::shared_ptr<const Profile> active;
            // One cache owner per settings generation, retained across every
            // window and successive simulation transmissions. Reconfiguration
            // releases it only after capture and all receiver owners drain.
            std::shared_ptr<receiver_detail::ReceiverBulkBackend> gpu_backend;
            execution::Task capture;
            // One owner at a time. A half-duplex pause joins the capture task
            // before a new task can reuse its estimate, avoiding TX tail bias.
            std::optional<detail::NoiseCalibration> noise_baseline;
            bool first_automatic_message = s.auto_calendar;
            auto start_capture = [&] {
                if (s.simulation || !live_receive_enabled(s)) return;
                if (s.finite_duplex) {
                    std::lock_guard lock(mutex);
                    finite = {};
                }
                capture = execution::Task([&, active](std::stop_token capture_stop) {
                    try {
                        receive_live(*active, s, noise_baseline, owner, capture_stop);
                        publish(owner, capture_stop, [&](Snapshot& out) {
                            if (s.finite_duplex && finite.exact_input_drained && finite.physical_complete) {
                                out.receiver_error.clear();
                                if (!out.transmitting) out.status = "Finite Robust capture drained after physical completion";
                            } else {
                                out.receiver_error = !s.finite_duplex ? "Robust audio input ended." :
                                    finite.physical_complete ? "Finite Robust input ended before its planned capture endpoint." :
                                    finite.exact_input_drained ? "Finite Robust capture ended without a physically complete message." :
                                    "Finite Robust input ended before physical completion.";
                                if (!out.transmitting) out.status = out.receiver_error;
                            }
                        });
                    } catch (const std::exception& error) {
                        publish(owner, capture_stop, [&](Snapshot& out) {
                            out.receiver_error = error.what();
                            if (!out.transmitting) out.status = out.receiver_error;
                        });
                    }
                    publish(owner, {}, [&](Snapshot& out) {
                        out.receiver_workers = 0;
                        if (s.auto_live_noise && s.full_duplex && !out.receiver_error.empty())
                            out.transmit_unavailable = out.receiver_error;
                    });
                    if (s.finite_duplex) {
                        {
                            std::lock_guard lock(mutex);
                            if (owner == generation) {
                                finite.ended = true;
                                finite.ready = false;
                                finite.stop_at = {};
                            }
                        }
                        changed.notify_all();
                    }
                });
            };
            auto close_capture = [&] {
                capture.request_stop();
                if (capture.joinable()) capture.join();
            };

            try {
                const auto blocked = basic_transmit_reason(s);
                const bool interactive_gpu = gpu_policy == GpuPolicy::interactive;
                if (interactive_gpu && (!s.gpu || blocked) && !gpu_available && gpu_unavailable_reason.empty()) {
                    try {
                        check_gpu_receiver_backend();
                        record_gpu_availability();
                    } catch (const GpuBackendUnavailable& error) {
                        record_gpu_availability(error.what());
                    }
                }
                if (s.gpu && !blocked && (!interactive_gpu || gpu_unavailable_reason.empty())) {
                    const bool fp32_minimum = s.transfer.modem.search_arithmetic == modem::SearchArithmetic::fp32;
                    try {
                        gpu_backend = make_gpu_receiver_backend(GpuReceiverOptions{fp32_minimum});
                        if (interactive_gpu) record_gpu_availability();
                    } catch (const GpuBackendUnavailable& error) {
                        if (!interactive_gpu) throw;
                        record_gpu_availability(error.what());
                    }
                }
                if (interactive_gpu && s.gpu && !gpu_unavailable_reason.empty()) {
                    s.gpu = false;
                    std::lock_guard lock(mutex);
                    if (owner == generation) settings.gpu = false;
                }
                if (blocked) throw Error(*blocked);
                const auto gpu_initial_receipt = gpu_backend ? gpu_backend->status_receipt() : receiver_detail::BulkReceipt{};
                const auto gpu_device = gpu_backend ? gpu_initial_receipt.device_name : std::string{};
                const auto gpu_backend_name = gpu_backend ? gpu_initial_receipt.device : std::string{};
                const auto gpu_selection_reason = gpu_backend ? gpu_initial_receipt.selection_reason : std::string{};
                const bool native_lag_preference = gpu_backend && gpu_backend->permits_native_exact_chirp_z();
                const auto prepare = [&] {
                    const auto anchor = s.simulation ? simulation_calendar(s) :
                        s.calendar.value_or(automatic_live_calendar);
                    auto prepared = detail::prepare_profile(s, anchor, stop);
                    // Optional wrong-key filtering is a reversible browser
                    // view. Retain the canonical baseline for every reception;
                    // headless receiver/probe configurations remain independent.
                    prepared.receiver.wrong_key_admission = WrongKeyAdmission::current;
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
                    current.gpu_device_uuid = gpu_initial_receipt.device_uuid;
                    current.gpu_selection_reason = gpu_selection_reason;
                    current.gpu_native_lag_preference_supported = native_lag_preference;
                    current.bit_seconds = calendar.geometry().bit_seconds;
                    current.slot_stride_seconds = static_cast<double>(calendar.calendar().distance(
                        initial.helper_begin, following.helper_begin)) / calendar.effective_sample_rate();
                    const auto [minimum_stride,maximum_stride]=calendar.calendar().stride_sample_range();
                    current.slot_stride_range_seconds=std::pair{
                        static_cast<double>(minimum_stride)/calendar.effective_sample_rate(),
                        static_cast<double>(maximum_stride)/calendar.effective_sample_rate()};
                    current.search_band_hz = std::pair{active->minimum_data_hz, active->maximum_data_hz};
                    if (s.simulation)
                        current.simulation_spectrum_gain_db = simulation_plot_noise_reference(s, s.transfer.modem.sample_rate);
                    if (active->receiver.helper_enabled)
                        current.helper_band_hz = std::pair{helper::low_pilot_frequency_hz - 100,
                            active->receiver.helper.frequency_hz + 100};
                    current.transmit_unavailable.reset();
                    if (!s.simulation && s.auto_live_noise && s.full_duplex)
                        current.transmit_unavailable = wait_for_live_calibration;
                    if (!s.simulation && !live_receive_enabled(s))
                        current.receiver_error = "Choose Auto or enter a measured noise density to enable Robust reception.";
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
                    bool replay_only;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock, stop, [&] { return request.has_value() || replay_requested ||
                            stopping || owner != generation; });
                        if (stop.stop_requested() || stopping || owner != generation) break;
                        replay_only = replay_requested;
                        if (!replay_only) {
                            send = std::move(*request);
                            request.reset();
                        }
                        transmission = tx_stop.get_token();
                        // Volume and routing can be changed for the next
                        // transmission without restarting the receive calendar.
                        transmit_settings.transmit_gain = settings.transmit_gain;
                        transmit_settings.channel_mode = settings.channel_mode;
                    }
                    std::stop_callback cancel_on_reconfigure(stop, [source = tx_stop]() mutable { source.request_stop(); });
                    bool capture_paused = false;
                    bool simulation_computed = replay_only;
                    std::string failure;
                    std::optional<SimulationComparison> comparison;
                    try {
                        cancelled(transmission);
                        if (replay_only) {
                            replay_simulation(*retained_replay, owner, transmission);
                        } else {
                            if (!s.simulation && s.full_duplex && live_receive_enabled(s))
                                (void)live_transmit_reservation_checked(transmit_settings, *active);
                            if (!s.simulation && !s.full_duplex) {
                                close_capture();
                                capture_paused = true;
                            }
                            if (s.simulation && send.mode == EmissionMode::Message &&
                                (send.raw || send.input.size() <= transfer::short_message_bytes)) {
                                comparison.emplace();
                                comparison->raw = send.raw;
                                for (const auto byte : send.input)
                                    comparison->sent.push_back(send.raw ? (byte ? '1' : '0') : static_cast<char>(byte));
                            }
                            prepare_send(send, transmit_settings, owner, transmission);
                            if (s.simulation) {
                                auto replay = simulate(*active, transmit_settings, send, owner, transmission,
                                                       comparison ? &*comparison : nullptr);
                                simulation_computed = replay.has_value();
                                if (replay && replay->samples().size() >= plot_size) {
                                    {
                                        std::lock_guard lock(mutex);
                                        replay->pattern_steps = std::make_shared<const std::vector<PatternStep>>(current.pattern_steps);
                                        retained_replay = std::move(replay);
                                    }
                                    replay_simulation(*retained_replay, owner, transmission);
                                }
                            } else {
                                const bool startup_quiet = first_automatic_message && send.mode == EmissionMode::Message;
                                play_live(*active, transmit_settings, send, startup_quiet, owner, transmission);
                                if (startup_quiet && !transmission.stop_requested()) first_automatic_message = false;
                            }
                        }
                    } catch (const std::exception& error) {
                        if (!transmission.stop_requested()) failure = error.what();
                    }
                    publish(owner, stop, [&](Snapshot& out) {
                        out.transmitting = false;
                        out.simulation_replay = false;
                        if (s.simulation) {
                            out.receiver_workers = 0;
                            simulation_preview.reset();
                            if (out.simulation_progress && simulation_started)
                                out.simulation_progress->elapsed_seconds =
                                    std::chrono::duration<double>(Clock::now() - *simulation_started).count();
                            simulation_started.reset();
                        }
                        out.simulation_pattern_replay_active = false;
                        out.simulation_pattern_replay_visible_steps = out.simulation_pattern_replay_steps
                            ? out.simulation_pattern_replay_steps->size() : 0;
                        out.simulation_pattern_replay_elapsed_ms =
                            static_cast<std::uint64_t>(simulation_replay_duration.count()) * 1000;
                        if (replay_only) replay_requested = false;
                        else {
                            out.transmit_trace.active = false;
                            out.transmit_trace.emitting = false;
                            ++out.transmit_trace.revision;
                        }
                        out.error = failure;
                        out.status = transmission.stop_requested() ? (simulation_computed ?
                            "Robust simulation input processed · replay stopped" : "Robust transmission cancelled") :
                            !failure.empty() ? failure :
                            replay_only ? "Robust simulation replay complete" :
                            s.simulation ? "Robust simulation input processed" : "Robust transmission sent";
                        if (comparison && simulation_computed && failure.empty()) {
                            out.status = "Robust simulation · " + comparison->summary();
                            if (transmission.stop_requested()) out.status += " · replay stopped";
                        }
                    });
                    if (stop.stop_requested()) break;
                    if (s.simulation && !replay_only) {
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
                    replay_requested = false;
                    out.running = false;
                    out.transmitting = false;
                    out.simulation_replay = false;
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
            retained_replay.reset();
            stopped = true;
            profile.reset();
        }
        changed.notify_all();
    }

    bool can_replay() const {
        return settings.simulation && started && !suspended && !stopping &&
            !current.transmitting && profile && retained_replay.has_value();
    }

    void enqueue(EmissionMode mode, std::span<const std::uint8_t> input = {}, bool force = false, bool raw = false) {
        std::lock_guard lock(mutex);
        if (stopping || suspended || !started) throw Error(no_transmitter);
        if (current.transmitting || request) throw Error("A Robust transmission is already active");
        if (!profile) throw Error(current.transmit_unavailable.value_or(preparing));
        if (!settings.simulation && settings.auto_live_noise && settings.full_duplex && !current.receiver_workers)
            throw Error(current.transmit_unavailable.value_or(wait_for_live_calibration));
        if (settings.finite_duplex && (!settings.calendar || !settings.live_noise_dbfs_hz))
            throw Error("Finite Robust duplex requires a calendar and input noise density");
        if (settings.finite_duplex && mode != EmissionMode::Message)
            throw Error("Finite Robust duplex permits one message transmission only");
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
        const auto requested_utc = settings.simulation ? 0 : utc_now();
        TxRequest send{mode, {input.begin(), input.end()}, force, raw, {}, requested_utc};
        tx_stop = std::stop_source{};
        current.transmitting = true;
        current.error.clear();
        current.status = send.mode == EmissionMode::Noise ? "Preparing Robust transmit noise" :
            send.mode == EmissionMode::HelperOnly ? "Preparing Robust Helper-only transmission" :
            "Preparing Robust transmission";
        if (settings.simulation) {
            simulation_preview.reset();
            simulation_started.reset();
            simulation_detail_capture.reset();
            current.simulation_detail.reset();
            current.simulation_progress.reset();
            current.pattern_steps.clear();
            current.simulation_pattern_replay_steps.reset();
            current.simulation_pattern_replay_active = false;
            current.simulation_pattern_replay_visible_steps = 0;
            current.simulation_pattern_replay_elapsed_ms = 0;
            current.waveform.clear();
            current.waveform_sample_rate = 0;
            current.spectrum_db.clear();
            current.spectrum_complex.clear();
            current.constellation.clear();
            current.spectrum_revision = 0;
        } else if (!settings.full_duplex) current.constellation.clear();
        auto& trace = current.transmit_trace;
        const auto revision = trace.revision + 1;
        trace = {};
        trace.revision = revision;
        trace.emitting = settings.simulation;
        trace.fhss = settings.fhss_profile != "off";
        trace.active = send.mode == EmissionMode::Message;
        trace.temporary_key = send.mode == EmissionMode::Noise;
        if (trace.active) {
            trace.raw = send.raw;
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

Session::Session() : Session(GpuPolicy::explicit_selection) {}
Session::Session(GpuPolicy gpu_policy) : impl_(std::make_unique<Impl>(gpu_policy)) {}
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
        if (!value.simulation || (value.receiver_memory_limit && p.retained_replay &&
            p.retained_replay->working_bytes() >= *value.receiver_memory_limit)) p.retained_replay.reset();
        auto replay_waterfall = value.simulation ? p.current.replay_waterfall : std::nullopt;
        p.profile.reset();
        p.current = {};
        p.simulation_detail_capture.reset();
        p.current.replay_waterfall = std::move(replay_waterfall);
        p.simulation_preview.reset();
        p.simulation_started.reset();
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
            p.current.receiver_workers = 0;
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

void Session::replay_simulation() {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.can_replay()) throw Error("No completed Robust simulation is available for replay");
    p.replay_requested = true;
    p.tx_stop = std::stop_source{};
    p.current.transmitting = true;
    p.current.error.clear();
    p.current.status = "Replaying the previous Robust simulation";
    p.changed.notify_all();
}

void Session::cancel_transmit() {
    auto& p = *impl_;
    std::stop_source stop;
    {
        std::lock_guard lock(p.mutex);
        if (!p.current.transmitting) return;
        stop = p.tx_stop;
        p.current.status = p.current.simulation_replay || p.replay_requested ? "Stopping Robust simulation replay" :
            "Stopping Robust transmission";
    }
    stop.request_stop();
    p.changed.notify_all();
}

bool Session::resume_recovery(std::uint64_t) { return false; }
void Session::cancel_recovery(std::uint64_t) {}

Snapshot Session::snapshot(bool want_simulation_detail) {
    auto& p = *impl_;
    Snapshot result;
    std::optional<SimulationPreview> preview;
    std::shared_ptr<SimulationDetailCapture> detail_capture;
    std::uint64_t detail_generation = 0;
    {
        std::lock_guard lock(p.mutex);
        preview.swap(p.simulation_preview);
    }
    if (preview && !preview->stop.stop_requested()) {
        auto frame = receiver_plot_frame(preview->first, preview->rate, preview->if_hz, preview->input);
        p.publish(preview->owner, preview->stop, [&](Snapshot& out) {
            if (p.plot_segment != preview->segment || !out.transmitting || out.simulation_replay ||
                (out.spectrum_revision && out.spectrum_segment == preview->segment &&
                    out.spectrum_start_sample >= frame.first)) return;
            p.commit_plot(out, std::move(frame), true, preview->segment, std::move(preview->waterfall));
        });
    }
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
                p.current.gpu_vulkan_timed_submissions = receipt.vulkan_timed_submissions;
                p.current.gpu_vulkan_device_command_seconds = receipt.vulkan_device_command_seconds;
                p.current.gpu_native_coarse_workspace_sessions = receipt.native_coarse_workspace_sessions;
                p.current.gpu_native_coarse_precision_sessions = receipt.native_coarse_precision_sessions;
            }
            p.current.gpu_host_scored_rows = receipt.vulkan_host_scored_rows;
            p.current.gpu_statistics_blocks = receipt.statistics_blocks;
            p.current.gpu_cuda_fp16_calls = receipt.cuda_matrix_fp16_calls;
            p.current.gpu_cuda_int8_calls = receipt.cuda_matrix_int8_calls;
            p.current.gpu_cuda_nvfp4_fine_modes = receipt.cuda_nvfp4_fine_modes;
            p.current.gpu_cuda_cufft_fine_modes = receipt.cuda_cufft_fine_modes;
            p.current.gpu_opencl_nvfp4_fine_modes = receipt.opencl_nvfp4_fine_modes;
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
        detail_capture = p.simulation_detail_capture;
        detail_generation = p.generation;
        result.gpu_available = p.gpu_available;
        result.gpu_unavailable_reason = p.gpu_unavailable_reason;
        if (result.simulation_progress && p.simulation_started)
            result.simulation_progress->elapsed_seconds =
                std::chrono::duration<double>(Clock::now() - *p.simulation_started).count();
        result.simulation_replay_available = p.can_replay();
        result.signals.swap(p.signal_events);
        result.received.swap(p.received_events);
        p.lock_status(result);
    }
    if (want_simulation_detail && detail_capture) {
        const auto detail = detail_capture->snapshot(Clock::now());
        std::lock_guard lock(p.mutex);
        if (detail && !p.stopping && p.generation == detail_generation &&
            p.simulation_detail_capture == detail_capture) {
            p.current.simulation_detail = detail;
            result.simulation_detail = detail;
        } else if (p.generation != detail_generation || p.simulation_detail_capture != detail_capture) {
            result.simulation_detail.reset();
        }
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
        p.current.receiver_workers = 0;
        p.simulation_preview.reset();
        p.simulation_detail_capture.reset();
        p.simulation_started.reset();
        p.current.simulation_replay = false;
        p.current.simulation_pattern_replay_active = false;
        p.current.status = "Stopping Robust session";
        p.request.reset();
        p.replay_requested = false;
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
