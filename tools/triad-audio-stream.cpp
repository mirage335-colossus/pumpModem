#include "datapump/audio.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

namespace {
using namespace std::chrono;
using Clock = steady_clock;
constexpr std::size_t block_frames = 4096;
constexpr std::uint64_t maximum_queue_frames = 192000; // two seconds at 96 kHz

struct Config {
    std::string mode = "play", device = "default", channels = "left";
    std::string clock_mode = "auto", capture_format = "raw";
    std::uint32_t rate = 48000;
    std::uint64_t frames = 0;
    bool frames_set = false, exclusive = false;
    bool continuous_clock = false;
    datapump::audio::ContinuousClockOptions clock_options;
    datapump::background_noise::Config noise;
    double reference_watts = 0, reference_rms = 0;
    bool watts_set = false, rms_set = false;
};

std::uint64_t parse_u64(const char* text, const char* what) {
    if (!text || !*text || *text == '-' || *text == '+')
        throw std::invalid_argument(std::string("invalid ") + what);
    std::size_t used = 0;
    auto value = std::stoull(text, &used);
    if (used != std::strlen(text)) throw std::invalid_argument(std::string("invalid ") + what);
    return value;
}
double parse_double(const char* text, const char* what) {
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    if (used != std::strlen(text) || !std::isfinite(value))
        throw std::invalid_argument(std::string("invalid ") + what);
    return value;
}

Config parse(int argc, char** argv) {
    Config c;
    std::optional<std::string> selected_noise;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto next = [&]() -> const char* {
            if (++i >= argc) throw std::invalid_argument("missing value for " + key);
            return argv[i];
        };
        if (key == "--mode") c.mode = next();
        else if (key == "--device") c.device = next();
        else if (key == "--sample-rate") {
            const auto v = parse_u64(next(), "sample rate");
            if (v == 0 || v > 192000) throw std::invalid_argument("sample rate must be 1..192000 Hz");
            c.rate = static_cast<std::uint32_t>(v);
        } else if (key == "--frames") { c.frames = parse_u64(next(), "frame count"); c.frames_set = true; }
        else if (key == "--exclusive") c.exclusive = true;
        else if (key == "--clock-discipline") {
            const std::string mode = next();
            if (mode != "auto" && mode != "off" && mode != "continuous") throw std::invalid_argument("clock discipline must be auto, off or continuous");
            c.clock_mode = mode;
        }
        else if (key == "--capture-format") c.capture_format = next();
        else if (key == "--clock-max-ppm") c.clock_options.maximum_rate_correction = parse_double(next(), "clock rate") / 1e6;
        else if (key == "--clock-phase-max-ppm") c.clock_options.maximum_phase_correction = parse_double(next(), "clock phase rate") / 1e6;
        else if (key == "--clock-slew-ppm-per-second") c.clock_options.maximum_slew_per_second = parse_double(next(), "clock slew") / 1e6;
        else if (key == "--clock-phase-seconds") c.clock_options.phase_time_constant_seconds = parse_double(next(), "clock phase time constant");
        else if (key == "--channels") c.channels = next();
        else if (key == "--background-noise-bits") {
            if (selected_noise) throw std::invalid_argument("select one background-noise level");
            selected_noise = key; const auto v = parse_u64(next(), "effective bits");
            if (v > 16) throw std::invalid_argument("effective bits must be 0..16");
            c.noise.mode = datapump::background_noise::Mode::EffectiveBits; c.noise.value = static_cast<double>(v);
        } else if (key == "--background-noise-obscured-bits") {
            if (selected_noise) throw std::invalid_argument("select one background-noise level");
            selected_noise = key; const auto v = parse_u64(next(), "obscured bits");
            if (v > 16) throw std::invalid_argument("obscured bits must be 0..16");
            c.noise.mode = datapump::background_noise::Mode::ObscuredBits; c.noise.value = static_cast<double>(v);
        } else if (key == "--background-noise-db") {
            if (selected_noise) throw std::invalid_argument("select one background-noise level");
            selected_noise = key; c.noise.mode = datapump::background_noise::Mode::SuppressionDb;
            c.noise.value = parse_double(next(), "noise suppression");
            if (c.noise.value < 0) throw std::invalid_argument("noise suppression must be nonnegative");
        } else if (key == "--background-noise-watts") {
            if (selected_noise) throw std::invalid_argument("select one background-noise level");
            selected_noise = key; c.watts_set = true; c.noise.mode = datapump::background_noise::Mode::RelativeWatts;
            c.noise.value = parse_double(next(), "noise watts");
            if (c.noise.value <= 0) throw std::invalid_argument("noise watts must be positive");
        } else if (key == "--background-noise-reference-watts") {
            c.reference_watts = parse_double(next(), "reference watts"); c.watts_set = true;
        } else if (key == "--background-noise-reference-rms") {
            c.reference_rms = parse_double(next(), "reference RMS"); c.rms_set = true;
        } else if (key == "--help") {
            std::cout << "triad-audio-stream --mode play|capture|copy --frames N [--sample-rate HZ] [--device NAME] [--exclusive] [--channels left|right|stereo]\n"
                "  play: stdin mono float32le -> audio backend; capture: backend -> stdout mono float32le; copy: bounded device-free pipe check.\n"
                "  --clock-discipline auto|off|continuous: auto (default) follows UTC for play/capture; copy remains off. Dated native provider required.\n"
                "  [--clock-max-ppm 1000 --clock-phase-max-ppm 200 --clock-slew-ppm-per-second 10 --clock-phase-seconds 1000]\n"
                "  --capture-format raw|mapped-v1: mapped capture emits bounded TRIADMAP frames with exact local sample coordinates.\n"
                "  optional TX noise: --background-noise-bits N | --background-noise-obscured-bits N | --background-noise-db D | --background-noise-watts W --background-noise-reference-watts W --background-noise-reference-rms R\n";
            std::exit(0);
        } else throw std::invalid_argument("unknown option: " + key);
    }
    if (!c.frames_set) throw std::invalid_argument("--frames is required");
    if (c.mode != "play" && c.mode != "capture" && c.mode != "copy")
        throw std::invalid_argument("--mode must be play, capture or copy");
    c.continuous_clock = c.clock_mode == "continuous" || (c.clock_mode == "auto" && c.mode != "copy");
    if (c.capture_format != "raw" && c.capture_format != "mapped-v1")
        throw std::invalid_argument("capture format must be raw or mapped-v1");
    if (c.capture_format != "raw" && c.mode != "capture")
        throw std::invalid_argument("mapped-v1 output requires capture mode");
    if (c.channels != "left" && c.channels != "right" && c.channels != "stereo")
        throw std::invalid_argument("--channels must be left, right or stereo");
    if (c.mode == "capture" && c.channels != "left")
        throw std::invalid_argument("capture supports the left logical mono input only");
    if ((c.noise.mode == datapump::background_noise::Mode::RelativeWatts && (!c.watts_set || !c.rms_set)) ||
        (c.noise.mode != datapump::background_noise::Mode::RelativeWatts && (c.watts_set || c.rms_set)))
        throw std::invalid_argument("relative watts needs both fixed reference watts and RMS, and other noise modes reject references");
    if (c.noise.mode == datapump::background_noise::Mode::RelativeWatts) {
        if (c.reference_watts <= 0 || c.reference_rms <= 0)
            throw std::invalid_argument("relative noise references must be positive");
        c.noise.reference_watts = c.reference_watts;
        c.noise.reference_mean_square = c.reference_rms * c.reference_rms;
    }
    if (c.mode == "capture" && c.noise.mode != datapump::background_noise::Mode::Off)
        throw std::invalid_argument("transmit background noise is unavailable in capture mode");
    if (c.mode == "copy" && c.noise.mode != datapump::background_noise::Mode::Off)
        throw std::invalid_argument("copy mode does not model the final audio output stage; use play mode");
    if (c.mode == "copy" && c.exclusive) throw std::invalid_argument("copy mode has no audio device");
    c.clock_options.validate();
    if (c.mode == "copy" && c.continuous_clock) throw std::invalid_argument("copy mode has no dated audio device for continuous clock discipline");
    return c;
}

void set_binary_stdio() {
    if constexpr (std::endian::native != std::endian::little)
        throw std::runtime_error("float32le pipe requires a little-endian host");
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

bool read_floats(std::istream& in, float* out, std::size_t count, std::size_t& got) {
    const auto bytes = count * sizeof(float);
    in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(bytes));
    const auto read_bytes = static_cast<std::size_t>(in.gcount());
    got = read_bytes / sizeof(float);
    if (read_bytes % sizeof(float)) throw std::runtime_error("input ended inside a float32 frame");
    if (!std::all_of(out, out + got, [](float value) { return std::isfinite(value); }))
        throw std::runtime_error("nonfinite PCM input");
    return got == count;
}

datapump::audio::ChannelMode channel_mode(const std::string& value) {
    if (value == "right") return datapump::audio::ChannelMode::right_mono;
    if (value == "stereo") return datapump::audio::ChannelMode::stereo;
    return datapump::audio::ChannelMode::left_mono;
}

struct CaptureBlock {
    std::vector<float> pcm;
    std::vector<double> offsets;
    std::uint64_t first_output_frame=0, source_frame_origin=0;
    std::uint32_t native_rate=0;
};

void append_le(char*& output,std::uint64_t value,std::size_t bytes) {
    for(std::size_t i=0;i<bytes;++i)*output++=static_cast<char>((value>>(8*i))&255u);
}

void write_capture_block(std::ostream& output,const CaptureBlock& block,bool mapped) {
    if(mapped) {
        if(block.pcm.empty() || block.pcm.size()>block_frames || block.offsets.size()!=block.pcm.size() || !block.native_rate)
            throw std::runtime_error("invalid mapped capture block");
        // One bounded write per complete frame, not an ostream operation for
        // each scalar. No lifetime-growing metadata side queue or second pipe.
        std::array<char,40+block_frames*12> bytes;
        char* cursor=bytes.data();std::memcpy(cursor,"TRIADMAP",8);cursor+=8;
        append_le(cursor,1,4);append_le(cursor,block.pcm.size(),4);
        append_le(cursor,block.first_output_frame,8);append_le(cursor,block.source_frame_origin,8);
        append_le(cursor,block.native_rate,4);append_le(cursor,0,4);
        for(double offset:block.offsets) {
            if(!std::isfinite(offset) || offset<0)throw std::runtime_error("nonfinite mapped capture coordinate");
            append_le(cursor,std::bit_cast<std::uint64_t>(offset),8);
        }
        for(float sample:block.pcm) {
            if(!std::isfinite(sample))throw std::runtime_error("nonfinite mapped capture PCM");
            append_le(cursor,std::bit_cast<std::uint32_t>(sample),4);
        }
        output.write(bytes.data(),static_cast<std::streamsize>(cursor-bytes.data()));
    } else {
        output.write(reinterpret_cast<const char*>(block.pcm.data()),static_cast<std::streamsize>(block.pcm.size()*sizeof(float)));
    }
}

struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<CaptureBlock> blocks;
    std::uint64_t frames = 0, cap = maximum_queue_frames;
    bool done = false, failed = false;
    std::string error;
};

int run(const Config& c) {
    set_binary_stdio();
    const auto wall_start = Clock::now();
    const auto cpu_start = std::clock();
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> captured{0};
    std::atomic<std::uint64_t> clipped{0};
    std::optional<datapump::audio::BackgroundNoiseReport> noise_report;
    datapump::audio::Options options;
    options.exclusive = c.exclusive;
    // A recovered underrun still loses physical samples. Never certify that
    // stream as complete merely because the backend later resumes playback.
    options.playback_discontinuity = [](const std::string& reason) {
        throw std::runtime_error("playback discontinuity: " + reason);
    };
    options.background_noise = c.noise;
    options.background_noise_report = [&](const auto& report) { noise_report = report; };
    std::optional<datapump::audio::ClockDisciplineReport> clock_report;
    std::uint64_t clock_reports = 0, clock_saturated = 0, clock_holdover = 0, clock_phase_limited = 0;
    double peak_clock_error = 0, peak_clock_rate = 0;
    if (c.continuous_clock) {
        options.continuous_clock = c.clock_options;
        options.continuous_clock_report = [&](const auto& report) {
            clock_report = report; ++clock_reports;
            clock_saturated += report.saturated; clock_holdover += report.holdover;
            clock_phase_limited += report.phase_correction_limited;
            if (std::isfinite(report.phase_error_seconds)) peak_clock_error = std::max(peak_clock_error, std::abs(report.phase_error_seconds));
            peak_clock_rate = std::max(peak_clock_rate, std::abs(report.applied_correction));
        };
    }

    if (c.mode == "copy") {
        std::array<float, block_frames> input{};
        std::uint64_t left = c.frames;
        while (left) {
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(left, input.size()));
            std::size_t got = 0;
            read_floats(std::cin, input.data(), want, got);
            if (got != want) throw std::runtime_error("input ended before declared frame count");
            std::cout.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(got * sizeof(float)));
            if (!std::cout) throw std::runtime_error("output pipe failed");
            completed += got; left -= got;
        }
        std::cout.flush();
    } else if (c.mode == "play") {
        std::uint64_t supplied = 0;
        auto callback = [&](std::span<float> output) -> std::size_t {
            if (supplied == c.frames) return 0;
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(
                c.frames - supplied, output.size()));
            std::size_t got = 0;
            read_floats(std::cin, output.data(), want, got);
            for (std::size_t i = 0; i < got; ++i)
                if (std::abs(output[i]) >= .999f) clipped.fetch_add(1, std::memory_order_relaxed);
            supplied += got;
            completed.store(supplied, std::memory_order_relaxed);
            return got;
        };
        datapump::audio::playback(c.rate, c.device, callback, {}, {}, channel_mode(c.channels), options);
        if (completed != c.frames) throw std::runtime_error("audio backend completed before declared frame count");
    } else {
        Queue queue;
        queue.cap = std::min<std::uint64_t>(maximum_queue_frames,
            std::max<std::uint64_t>(block_frames, static_cast<std::uint64_t>(c.rate) * 2));
        std::optional<CaptureBlock> pending_map;
        if(c.capture_format == "mapped-v1") {
            options.capture_sample_map = [&](const datapump::audio::CaptureSampleMap& map) {
                if(pending_map || map.source_frame_offsets.empty() || map.source_frame_offsets.size()>block_frames ||
                    map.first_output_frame!=captured.load(std::memory_order_relaxed) || !map.native_rate)
                    throw std::runtime_error("capture sample map is missing, oversized or discontinuous");
                pending_map.emplace();
                pending_map->first_output_frame=map.first_output_frame;
                pending_map->source_frame_origin=map.source_frame_origin;
                pending_map->native_rate=map.native_rate;
                pending_map->offsets.assign(map.source_frame_offsets.begin(),map.source_frame_offsets.end());
            };
        }
        std::thread writer([&] {
            try {
                for (;;) {
                    CaptureBlock block;
                    {
                        std::unique_lock lock(queue.mutex);
                        queue.changed.wait(lock, [&] { return !queue.blocks.empty() || queue.done; });
                        if (queue.blocks.empty() && queue.done) break;
                        block = std::move(queue.blocks.front()); queue.blocks.pop_front();
                        queue.frames -= block.pcm.size();
                    }
                    write_capture_block(std::cout,block,c.capture_format == "mapped-v1");
                    std::cout.flush();
                    if (!std::cout) throw std::runtime_error("capture output pipe failed");
                    completed.fetch_add(block.pcm.size(), std::memory_order_relaxed);
                }
            } catch (const std::exception& e) {
                std::lock_guard lock(queue.mutex); queue.error = e.what(); queue.failed = true; queue.changed.notify_all();
            }
        });
        auto callback = [&](std::span<const float> input) {
            const auto done = captured.load(std::memory_order_relaxed);
            const auto left = done >= c.frames ? 0 : c.frames - done;
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(left, input.size()));
            if (!n) return false;
            CaptureBlock block;
            if(c.capture_format == "mapped-v1") {
                if(!pending_map || pending_map->offsets.size()!=input.size())
                    throw std::runtime_error("capture PCM has no matching exact sample map");
                block=std::move(*pending_map);pending_map.reset();block.offsets.resize(n);
            }
            block.pcm.assign(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(n));
            for (float v : block.pcm) if (std::abs(v) >= .999f) clipped.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard lock(queue.mutex);
            if (queue.failed || queue.frames + n > queue.cap) {
                queue.failed = true; queue.error = "capture pipe queue exceeded its two-second bound";
                queue.changed.notify_all(); return false;
            }
            queue.frames += n; queue.blocks.push_back(std::move(block));
            captured.fetch_add(n, std::memory_order_relaxed);
            queue.changed.notify_all();
            return done+n<c.frames;
        };
        try {
            datapump::audio::capture(c.rate, c.device, callback, {}, {}, options);
        } catch (...) {
            { std::lock_guard lock(queue.mutex); queue.done = true; queue.changed.notify_all(); }
            writer.join(); throw;
        }
        { std::lock_guard lock(queue.mutex); queue.done = true; queue.changed.notify_all(); }
        writer.join();
        std::lock_guard lock(queue.mutex);
        if (!queue.error.empty()) throw std::runtime_error(queue.error);
        if (captured != c.frames || completed != c.frames) throw std::runtime_error("capture ended before declared frame count");
    }
    const auto wall = duration<double>(Clock::now() - wall_start).count();
    const auto cpu = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    std::cerr << std::setprecision(std::numeric_limits<long double>::max_digits10)
        << "{\"schema\":\"triad-audio-stream-v1\",\"mode\":\"" << c.mode
        << "\",\"sample_rate_hz\":" << c.rate << ",\"requested_frames\":" << c.frames
        << ",\"completed_frames\":" << completed.load() << ",\"complete\":true,\"clipped_samples\":" << clipped.load()
        << ",\"process_cpu_seconds\":" << cpu << ",\"wall_seconds\":" << wall
        << ",\"queue_bound_frames\":" << (c.mode == "copy" ? 0 : std::min<std::uint64_t>(maximum_queue_frames, std::max<std::uint64_t>(block_frames, static_cast<std::uint64_t>(c.rate) * 2)))
        << ",\"capture_format\":\"" << c.capture_format << "\""
        << ",\"background_noise\":{";
    if (!noise_report || !noise_report->enabled) std::cerr << "\"enabled\":false";
    else std::cerr << "\"enabled\":true,\"sample_rate_hz\":" << noise_report->sample_rate_hz
        << ",\"active_samples\":" << noise_report->active_samples << ",\"expected_variance\":" << noise_report->expected_variance
        << ",\"measured_variance\":" << noise_report->measured_variance << ",\"peak\":" << noise_report->peak;
    std::cerr << "},\"clock_discipline\":{\"enabled\":" << (c.continuous_clock ? "true" : "false")
        << ",\"reports\":" << clock_reports << ",\"saturated_reports\":" << clock_saturated
        << ",\"holdover_reports\":" << clock_holdover << ",\"peak_absolute_error_seconds\":" << peak_clock_error
        << ",\"phase_limited_reports\":" << clock_phase_limited
        << ",\"peak_applied_ppm\":" << peak_clock_rate * 1e6;
    if (clock_report) {
        const auto& r = *clock_report;
        std::cerr << ",\"ready\":" << (r.ready ? "true" : "false")
            << ",\"observations\":" << r.observations << ",\"rejected\":" << r.rejected
            << ",\"clock_steps\":" << r.clock_steps << ",\"rate_updates\":" << r.rate_updates
            << ",\"cumulative_clock_step_seconds\":" << r.cumulative_clock_step_seconds
            << ",\"q_estimate\":" << r.q_estimate << ",\"target_correction\":" << r.target_correction
            << ",\"applied_correction\":" << r.applied_correction << ",\"native_frame\":" << r.native_frame
            << ",\"logical_seconds\":" << r.logical_seconds
            << ",\"origin_utc_seconds\":" << r.origin_utc_seconds
            << ",\"estimated_utc_seconds\":" << r.estimated_utc_seconds
            << ",\"phase_error_seconds\":" << r.phase_error_seconds << ",\"uncertainty_seconds\":";
        if (std::isfinite(r.uncertainty_seconds)) std::cerr << r.uncertainty_seconds; else std::cerr << "null";
    }
    std::cerr << ",\"scope\":\"estimated full retained audio passband; adjusted capture covariance unqualified; no RF-clock certificate\"}}\n";
    return 0;
}
}

int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& e) {
        std::cerr << "triad-audio-stream: " << e.what() << '\n';
        std::cerr << "{\"schema\":\"triad-audio-stream-v1\",\"complete\":false}\n";
        return 2;
    }
}
