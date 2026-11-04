// Opt-in, isolated duplex PCM capture used by the triad live cable study.
// The API delivers logical mono float PCM. Optional raw output retains the
// provider's pre-resampling mono capture stream (currently S16 converted to
// float by the native ALSA backend), not analog ADC integer words.
#include "datapump/audio.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
using namespace datapump::audio;
constexpr std::uint32_t kMinRate = 44100;
constexpr std::uint32_t kMaxRate = 192000;
constexpr double kHardMaxSeconds = 2800.0;
constexpr std::uint64_t kMaxInputBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t kMaxWorkingBytes = 1536ull * 1024 * 1024;
constexpr std::uint32_t kMaxProviderRate = 192000;

struct Config {
    std::filesystem::path input, output, metadata, raw_output;
    std::string capture_device, playback_device;
    std::uint32_t rate = 48000;
    double pre = .75, post = 1.0, max_seconds = 30.0;
    bool live = false, exclusive = false, rate_set = false;
};

struct FormatRecord {
    bool seen = false;
    StreamFormat value{};
};

struct RunState {
    std::atomic<std::uint64_t> logical_samples{0};
    std::atomic<std::uint64_t> raw_samples{0};
    std::atomic<std::uint32_t> raw_rate{0};
    std::atomic<std::uint64_t> stop_after{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<bool> capture_done{false}, timed_out{false}, buffer_full{false};
    std::atomic<bool> capture_gap{false}, playback_gap{false};
    std::mutex format_mutex, timing_mutex;
    FormatRecord capture_format, playback_format;
    std::optional<TimePrediction> first_timing, last_timing;
    std::uint64_t first_timing_sample = 0, last_timing_sample = 0;
    std::uint64_t timing_count = 0;
    std::string timing_status, capture_error, gap_reason;
    std::vector<float> logical_capture, raw_capture;
    std::vector<float> input;
    std::uint64_t playback_start_capture_sample = 0;
    std::uint64_t playback_return_capture_sample = 0;
    double playback_call_start_seconds = 0, playback_call_return_seconds = 0;
};

std::string json_string(std::string_view s) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

std::uint64_t parse_uint(std::string_view s, std::string_view name) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || result.ec != std::errc{} || result.ptr != s.data() + s.size())
        throw std::runtime_error("invalid integer for " + std::string(name));
    return value;
}

double parse_number(std::string_view s, std::string_view name) {
    std::string copy(s);
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (copy.empty() || end != copy.c_str() + copy.size() || !std::isfinite(value))
        throw std::runtime_error("invalid finite number for " + std::string(name));
    return value;
}

void usage(std::ostream& out) {
    out << "Usage: triad_audio_capture --live --input mono-f32le --output new-mono-f32le "
           "--metadata new-json --rate 48000 --capture-device hw:1,0 "
           "--playback-device hw:1,0 --exclusive [options]\n"
           "Options: --raw-output new-provider-rate-f32le --preroll-seconds .75 "
           "--postroll-seconds 1 --max-seconds 30 --list-devices\n"
           "The named audio devices must be explicit ALSA hw endpoints.\n";
}

Config parse_args(int argc, char **argv, bool& help, bool& list) {
    Config c;
    help = false;
    list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--help" || key == "-h") { help = true; continue; }
        if (key == "--live") { c.live = true; continue; }
        if (key == "--exclusive") { c.exclusive = true; continue; }
        if (key == "--list-devices") { list = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + std::string(key));
        const std::string_view value(argv[++i]);
        if (key == "--input") c.input = value;
        else if (key == "--output") c.output = value;
        else if (key == "--metadata") c.metadata = value;
        else if (key == "--raw-output") c.raw_output = value;
        else if (key == "--capture-device") c.capture_device = value;
        else if (key == "--playback-device") c.playback_device = value;
        else if (key == "--rate") {
            const auto v = parse_uint(value, key);
            if (v > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("rate out of range");
            c.rate = static_cast<std::uint32_t>(v);
            c.rate_set = true;
        } else if (key == "--preroll-seconds") c.pre = parse_number(value, key);
        else if (key == "--postroll-seconds") c.post = parse_number(value, key);
        else if (key == "--max-seconds") c.max_seconds = parse_number(value, key);
        else throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (help || list) {
        if (help && list) throw std::runtime_error("--help and --list-devices cannot be combined");
        if (argc != (help ? 2 : 2)) throw std::runtime_error("--help/--list-devices must be used alone");
        return c;
    }
    if (!c.live) throw std::runtime_error("opening audio requires explicit --live");
    if (c.input.empty() || c.output.empty() || c.metadata.empty() || c.capture_device.empty() || c.playback_device.empty())
        throw std::runtime_error("input, output, metadata, capture-device, and playback-device are required");
    if (!c.rate_set) throw std::runtime_error("an explicit --rate is required");
    if (!c.exclusive || !exclusive_supported()) throw std::runtime_error("direct exclusive ALSA audio is required for this measurement");
    if (!c.capture_device.starts_with("hw:") || !c.playback_device.starts_with("hw:"))
        throw std::runtime_error("capture and playback must name explicit hw:C,D endpoints; monitor/default aliases are rejected");
    if (c.rate < kMinRate || c.rate > kMaxRate) throw std::runtime_error("logical rate must be 44100..192000 Hz");
    if (!std::isfinite(c.pre) || c.pre < .1 || c.pre > 10 || !std::isfinite(c.post) || c.post < .1 || c.post > 10)
        throw std::runtime_error("pre/post roll must be finite and 0.1..10 seconds");
    if (!std::isfinite(c.max_seconds) || c.max_seconds < 2 || c.max_seconds > kHardMaxSeconds)
        throw std::runtime_error("max-seconds must be finite and 2..2800 seconds");
    return c;
}

std::filesystem::path absolute_normal(const std::filesystem::path& p) {
    return std::filesystem::absolute(p).lexically_normal();
}

struct ReservedFile {
    std::filesystem::path path;
    std::FILE *file = nullptr;
    ReservedFile() = default;
    explicit ReservedFile(std::filesystem::path p) : path(std::move(p)) {
        file = std::fopen(path.string().c_str(), "wbx");
        if (!file) throw std::runtime_error("refusing existing/uncreatable output: " + path.string());
    }
    ReservedFile(const ReservedFile&) = delete;
    ReservedFile& operator=(const ReservedFile&) = delete;
    ReservedFile(ReservedFile&& other) noexcept : path(std::move(other.path)), file(std::exchange(other.file, nullptr)) {}
    ~ReservedFile() { if (file) std::fclose(file); }
    void write_bytes(const void *data, std::size_t bytes) {
        if (!file || std::fwrite(data, 1, bytes, file) != bytes || std::fflush(file) != 0)
            throw std::runtime_error("failed writing reserved output: " + path.string());
    }
    void replace_bytes(const void *data, std::size_t bytes) {
        if (!file) throw std::runtime_error("output reservation is closed");
#ifdef _WIN32
        if (_chsize_s(_fileno(file), 0) != 0 || _fseeki64(file, 0, SEEK_SET) != 0)
#else
        if (ftruncate(fileno(file), 0) != 0 || std::fseek(file, 0, SEEK_SET) != 0)
#endif
            throw std::runtime_error("could not replace reserved output: " + path.string());
        write_bytes(data, bytes);
    }
    void close() { if (file) { const int rc = std::fclose(file); file = nullptr; if (rc != 0) throw std::runtime_error("close failed: " + path.string()); } }
};

std::vector<float> read_input(const Config& c) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    if constexpr (std::endian::native != std::endian::little)
        throw std::runtime_error("float32le input requires a little-endian host");
    const auto bytes = std::filesystem::file_size(c.input);
    const auto max_source_bytes = static_cast<std::uint64_t>(
        std::floor(static_cast<long double>(c.rate) * sizeof(float) * c.max_seconds));
    if (!bytes || bytes % sizeof(float) || bytes > kMaxInputBytes || bytes > max_source_bytes)
        throw std::runtime_error("input must be nonempty aligned float32le mono PCM within the byte/duration bound");
    std::vector<float> x(static_cast<std::size_t>(bytes / sizeof(float)));
    std::ifstream f(c.input, std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(x.data()), static_cast<std::streamsize>(bytes))) throw std::runtime_error("could not read complete input PCM");
    double peak = 0;
    for (float v : x) {
        if (!std::isfinite(v)) throw std::runtime_error("input contains nonfinite PCM");
        peak = std::max(peak, std::abs(static_cast<double>(v)));
    }
    if (peak > .98) throw std::runtime_error("input peak exceeds the fixed 0.98 full-scale safety bound");
    return x;
}

void write_float_file(ReservedFile& f, const std::vector<float>& values) {
    if constexpr (std::endian::native == std::endian::little) {
        f.write_bytes(values.data(), values.size() * sizeof(float));
    } else {
        std::vector<std::uint32_t> words;
        words.reserve(values.size());
        for (float x : values) {
            auto word = std::bit_cast<std::uint32_t>(x);
            word = ((word & 0x000000ffu) << 24) | ((word & 0x0000ff00u) << 8) |
                   ((word & 0x00ff0000u) >> 8) | ((word & 0xff000000u) >> 24);
            words.push_back(word);
        }
        f.write_bytes(words.data(), words.size() * sizeof(std::uint32_t));
    }
}

const char *transport_kind(CaptureTransportKind kind) {
    switch (kind) {
    case CaptureTransportKind::fixed_nominal_audio: return "fixed_nominal_audio";
    case CaptureTransportKind::sampled_physical_halfband: return "sampled_physical_halfband";
    default: return "unknown";
    }
}

void append_format_json(std::ostream& o, const FormatRecord& r) {
    if (!r.seen) { o << "null"; return; }
    const auto& f = r.value;
    o << "{\"logical_rate\":" << f.logical_rate << ",\"hardware_rate\":" << f.hardware_rate
      << ",\"usable_passband_hz\":" << std::setprecision(12) << f.usable_passband_hz
      << ",\"workspace_bytes\":" << f.workspace_bytes
      << ",\"timing_quality\":" << json_string(f.timing_quality == TimingQuality::unavailable ? "unavailable" : f.timing_quality == TimingQuality::estimated ? "estimated" : "bounded")
      << ",\"timing_uncertainty_seconds\":" << f.timing_uncertainty_seconds
      << ",\"capture_transport\":{\"kind\":" << json_string(transport_kind(f.capture_transport.kind))
      << ",\"raw_rate\":" << f.capture_transport.raw_rate
      << ",\"logical_rate\":" << f.capture_transport.logical_rate
      << ",\"raw_zero\":" << f.capture_transport.raw_zero
      << ",\"logical_zero\":" << f.capture_transport.logical_zero << "}}";
}

std::string make_metadata(const Config& c, const RunState& s, bool success, std::string_view failure,
                          std::uint64_t stop_after, double elapsed, std::uint64_t budget_samples) {
    const auto n = s.logical_samples.load();
    const auto raw_n = s.raw_samples.load();
    std::ostringstream o;
    o << std::setprecision(12) << "{\"schema\":\"triad-audio-capture-v1\",\"status\":" << json_string(success ? "complete" : "incomplete")
      << ",\"failure\":" << json_string(failure)
      << ",\"capture_error\":" << json_string(s.capture_error)
      << ",\"requested\":{\"rate\":" << c.rate
      << ",\"capture_device\":" << json_string(c.capture_device) << ",\"playback_device\":" << json_string(c.playback_device)
      << ",\"exclusive\":true,\"preroll_seconds\":" << c.pre << ",\"postroll_seconds\":" << c.post
      << ",\"max_seconds\":" << c.max_seconds << ",\"input_peak\":";
    double input_peak = 0;
    for (float v : s.input) input_peak = std::max(input_peak, std::abs(static_cast<double>(v)));
    o << input_peak << "},\"playback\":{\"call_start_capture_sample\":" << s.playback_start_capture_sample
      << ",\"call_return_capture_sample\":" << s.playback_return_capture_sample
      << ",\"call_start_seconds\":" << s.playback_call_start_seconds
      << ",\"call_return_seconds\":" << s.playback_call_return_seconds
      << ",\"marker_note\":\"call boundary and API drain marker; not calibrated analog latency\"}"
      << ",\"capture\":{\"logical_samples\":" << n << ",\"logical_rate\":" << c.rate
      << ",\"logical_seconds\":" << static_cast<double>(n) / c.rate
      << ",\"required_postroll_samples\":" << stop_after
      << ",\"maximum_logical_samples\":" << budget_samples
      << ",\"logical_peak\":";
    double logical_peak = 0;
    for (float v : s.logical_capture) logical_peak = std::max(logical_peak, std::abs(static_cast<double>(v)));
    o << logical_peak << ",\"raw_samples\":" << raw_n << ",\"raw_rate\":" << s.raw_rate.load()
      << ",\"raw_seconds\":" << (s.raw_rate.load() ? static_cast<double>(raw_n) / s.raw_rate.load() : 0)
      << ",\"raw_semantics\":\"optional mono provider capture float samples before modem-rate resampling; native ALSA currently converts S16 PCM; not raw ADC codes\",\"capture_timing_count\":" << s.timing_count;
    auto add_prediction = [&](std::string_view name, const std::optional<TimePrediction>& p, std::uint64_t sample) {
        o << "," << json_string(name) << ":";
        if (!p) { o << "null"; return; }
        o << "{\"logical_sample_index\":" << sample
          << ",\"utc_seconds\":" << static_cast<double>(p->utc_seconds)
          << ",\"uncertainty_seconds\":" << p->uncertainty_seconds
          << ",\"seconds_per_frame\":" << p->seconds_per_frame
          << ",\"rate_uncertainty_fraction\":" << p->rate_uncertainty_fraction
          << ",\"clock_extrapolation_seconds\":" << p->clock_extrapolation_seconds
          << ",\"quality\":" << json_string(p->quality == TimingQuality::estimated ? "estimated" : p->quality == TimingQuality::bounded ? "bounded" : "unavailable") << "}";
    };
    add_prediction("first_capture_prediction", s.first_timing, s.first_timing_sample);
    add_prediction("last_capture_prediction", s.last_timing, s.last_timing_sample);
    o << "},\"audio_format\":{\"capture\":"; append_format_json(o, s.capture_format);
    o << ",\"playback\":"; append_format_json(o, s.playback_format);
    o << "},\"discontinuity\":{\"capture\":" << (s.capture_gap.load() ? "true" : "false")
      << ",\"playback\":" << (s.playback_gap.load() ? "true" : "false")
      << ",\"reason\":" << json_string(s.gap_reason) << "},\"timing_status\":" << json_string(s.timing_status)
      << ",\"elapsed_seconds\":" << elapsed << ",\"capture_timing_note\":\"native device timestamps are observational estimates, not calibrated DAC-to-ADC or radio latency\"}\n";
    return o.str();
}

int list_devices() {
    std::cout << "[";
    const auto ds = devices();
    for (std::size_t i = 0; i < ds.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"id\":" << json_string(ds[i].id) << ",\"description\":" << json_string(ds[i].description) << "}";
    }
    std::cout << "]\n";
    return 0;
}

int run(const Config& c) {
    const auto started = Clock::now();
    RunState s;
    s.input = read_input(c);
    const double source_seconds = static_cast<double>(s.input.size()) / c.rate;
    if (source_seconds + c.pre + c.post + 3.0 > c.max_seconds)
        throw std::runtime_error("source plus pre/post roll and startup margin exceed --max-seconds");
    const std::uint64_t max_samples = static_cast<std::uint64_t>(std::ceil(c.max_seconds * c.rate));
    const std::uint64_t raw_cap = static_cast<std::uint64_t>(std::ceil(c.max_seconds * kMaxProviderRate));
    const std::uint64_t bytes = s.input.size() * sizeof(float) + max_samples * sizeof(float) +
        (c.raw_output.empty() ? 0 : raw_cap * sizeof(float));
    if (bytes > kMaxWorkingBytes) throw std::runtime_error("bounded input/capture buffers exceed the 1536-MiB memory ceiling");
    if (c.output == c.metadata || (!c.raw_output.empty() && (c.raw_output == c.output || c.raw_output == c.metadata)))
        throw std::runtime_error("output paths must be distinct");
    const auto input_abs = absolute_normal(c.input);
    for (const auto *out : {&c.output, &c.metadata})
        if (absolute_normal(*out) == input_abs) throw std::runtime_error("input and output paths must be distinct");
    if (!c.raw_output.empty() && absolute_normal(c.raw_output) == input_abs)
        throw std::runtime_error("input and raw-output paths must be distinct");

    // Claim every output atomically before opening devices. The open handles
    // remain owned through finalization, so an existing file is never replaced.
    ReservedFile logical_file(c.output);
    ReservedFile metadata_file(c.metadata);
    std::optional<ReservedFile> raw_file;
    if (!c.raw_output.empty()) raw_file.emplace(c.raw_output);
    s.logical_capture.reserve(static_cast<std::size_t>(max_samples));
    if (!c.raw_output.empty()) s.raw_capture.reserve(static_cast<std::size_t>(raw_cap));

    std::stop_source stop;
    const auto deadline = started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(c.max_seconds));
    std::mutex watch_mutex;
    std::condition_variable watch_cv;
    bool finished = false;
    std::thread watchdog([&] {
        std::unique_lock lock(watch_mutex);
        if (!watch_cv.wait_until(lock, deadline, [&] { return finished; })) {
            s.timed_out.store(true);
            stop.request_stop();
        }
    });

    const auto pre_samples = static_cast<std::uint64_t>(std::ceil(c.pre * c.rate));
    std::thread capture_thread([&] {
        try {
            Options options;
            options.exclusive = c.exclusive;
            options.request_capture_timing = true;
            options.capture_timing = [&](const TimePrediction& p) {
                std::lock_guard lock(s.timing_mutex);
                const auto sample = s.logical_samples.load();
                if (!s.first_timing) { s.first_timing = p; s.first_timing_sample = sample; }
                s.last_timing = p;
                s.last_timing_sample = sample;
                ++s.timing_count;
            };
            options.timing_status = [&](const TimingStatus& t) {
                std::lock_guard lock(s.timing_mutex);
                s.timing_status = t.reason;
            };
            options.capture_discontinuity = [&] {
                s.capture_gap.store(true);
                std::lock_guard lock(s.timing_mutex);
                if (s.gap_reason.empty()) s.gap_reason = "capture discontinuity reported by audio provider";
            };
            if (!c.raw_output.empty()) options.capture_monitor = [&](std::span<const float> chunk, std::uint32_t rate) {
                const auto prior = s.raw_samples.load();
                if (!prior) s.raw_rate.store(rate);
                const auto count = std::min<std::uint64_t>(chunk.size(), raw_cap - std::min(prior, raw_cap));
                s.raw_capture.insert(s.raw_capture.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
                s.raw_samples.store(prior + count);
                if (count != chunk.size()) s.buffer_full.store(true);
            };

            capture(c.rate, c.capture_device, [&](std::span<const float> chunk) {
                const auto prior = s.logical_samples.load();
                const auto limit = s.stop_after.load();
                const auto cap = std::min<std::uint64_t>(max_samples, limit);
                const auto count = std::min<std::uint64_t>(chunk.size(), cap - std::min(prior, cap));
                s.logical_capture.insert(s.logical_capture.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
                s.logical_samples.store(prior + count);
                if (count != chunk.size() && prior + count < limit) s.buffer_full.store(true);
                return prior + count < limit && !stop.stop_requested();
            }, stop.get_token(), [&](const StreamFormat& f) {
                std::lock_guard lock(s.format_mutex);
                s.capture_format = {true, f};
            }, options);
        } catch (const std::exception& e) {
            s.capture_error = e.what();
            stop.request_stop();
        }
        s.capture_done.store(true);
    });

    std::string run_error;
    std::uint64_t required_capture_samples = 0;
    try {
        const auto startup_deadline = std::min(deadline, started + std::chrono::seconds(15));
        while (s.logical_samples.load() < pre_samples) {
            if (s.capture_done.load()) throw std::runtime_error("capture ended before preroll completed");
            if (s.timed_out.load() || Clock::now() >= startup_deadline) throw std::runtime_error("capture preroll exceeded startup deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        s.playback_start_capture_sample = s.logical_samples.load();
        s.playback_call_start_seconds = std::chrono::duration<double>(Clock::now() - started).count();
        Options playback_options;
        playback_options.exclusive = c.exclusive;
        playback_options.playback_discontinuity = [&](const std::string& reason) {
            s.playback_gap.store(true);
            std::lock_guard lock(s.timing_mutex);
            if (s.gap_reason.empty()) s.gap_reason = reason;
        };
        play(s.input, c.rate, c.playback_device, stop.get_token(), [&](const StreamFormat& f) {
            std::lock_guard lock(s.format_mutex);
            s.playback_format = {true, f};
        }, ChannelMode::stereo, playback_options);
        s.playback_call_return_seconds = std::chrono::duration<double>(Clock::now() - started).count();
        s.playback_return_capture_sample = s.logical_samples.load();
        required_capture_samples = s.playback_return_capture_sample + static_cast<std::uint64_t>(std::ceil(c.post * c.rate));
        if (required_capture_samples > max_samples) throw std::runtime_error("postroll endpoint exceeds preallocated capture bound");
        s.stop_after.store(required_capture_samples);
        while (s.logical_samples.load() < required_capture_samples) {
            if (s.capture_done.load()) throw std::runtime_error("capture ended before required postroll");
            if (s.timed_out.load()) throw std::runtime_error("duplex capture exceeded wall deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (const std::exception& e) {
        run_error = e.what();
        stop.request_stop();
    }
    capture_thread.join();
    {
        std::lock_guard lock(watch_mutex);
        finished = true;
    }
    watch_cv.notify_all();
    watchdog.join();
    const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
    if (s.timed_out.load() && run_error.empty()) run_error = "duplex capture exceeded wall deadline";
    if (!s.capture_error.empty() && run_error.empty()) run_error = s.capture_error;
    if (s.buffer_full.load() && run_error.empty()) run_error = "capture buffer reached its bound before the requested endpoint";
    if (s.capture_gap.load() && run_error.empty()) run_error = "capture discontinuity invalidated continuous waveform";
    if (s.playback_gap.load() && run_error.empty()) run_error = "playback discontinuity invalidated continuous waveform";
    if (!s.capture_format.seen && run_error.empty()) run_error = "capture did not report a negotiated stream format";
    if (!s.playback_format.seen && run_error.empty()) run_error = "playback did not report a negotiated stream format";
    if (required_capture_samples && s.logical_samples.load() < required_capture_samples && run_error.empty())
        run_error = "capture ended short of the requested sample endpoint";
    auto append_failure = [&](std::string message) {
        if (!run_error.empty()) run_error += "; ";
        run_error += std::move(message);
    };
    try {
        write_float_file(logical_file, s.logical_capture);
        if (raw_file) write_float_file(*raw_file, s.raw_capture);
    } catch (const std::exception& e) { append_failure(e.what()); }
    // Close both PCM artifacts before finalizing metadata so a close/flush
    // failure is reflected in the capture's status instead of leaving a
    // misleading complete receipt beside a broken waveform.
    try { logical_file.close(); }
    catch (const std::exception& e) { append_failure(e.what()); }
    if (raw_file) {
        try { raw_file->close(); }
        catch (const std::exception& e) { append_failure(e.what()); }
    }
    const bool success = run_error.empty();
    const auto metadata = make_metadata(c, s, success, run_error, required_capture_samples, elapsed, max_samples);
    try { metadata_file.write_bytes(metadata.data(), metadata.size()); }
    catch (const std::exception& e) { append_failure(std::string("metadata write failed: ") + e.what()); }
    try { metadata_file.close(); }
    catch (const std::exception& e) { append_failure(std::string("metadata close failed: ") + e.what()); }
    if (!run_error.empty()) {
        std::cerr << "triad audio capture incomplete: " << run_error << '\n';
        return 1;
    }
    std::cout << "{\"status\":\"complete\",\"logical_samples\":" << s.logical_samples.load()
              << ",\"raw_samples\":" << s.raw_samples.load() << ",\"elapsed_seconds\":" << elapsed << "}\n";
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool help = false, list = false;
        auto c = parse_args(argc, argv, help, list);
        if (help) { usage(std::cout); return 0; }
        if (list) return list_devices();
        return run(c);
    } catch (const std::exception& e) {
        std::cerr << "triad_audio_capture: " << e.what() << '\n';
        return 2;
    }
}
