#include "datapump/background_noise.hpp"
#include "datapump/audio_output.hpp"
#include "datapump/resampler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace datapump::background_noise;
namespace {

struct Options {
    fs::path input, output, metadata;
    std::string format, output_format;
    unsigned channels = 1;
    double signal_gain = 1, sample_rate = 48000, input_rate = 0;
    Config noise{};
    bool level_set = false;
};

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
double number(std::string_view s, const char* name) {
    std::string copy(s);
    std::size_t used = 0;
    double value;
    try { value = std::stod(copy, &used); }
    catch (...) { throw std::runtime_error(std::string("invalid ") + name); }
    if (used != copy.size() || !std::isfinite(value))
        throw std::runtime_error(std::string("invalid ") + name);
    return value;
}
unsigned integer(std::string_view s, const char* name) {
    unsigned value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size())
        throw std::runtime_error(std::string("invalid ") + name);
    return value;
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto next = [&]() -> std::string_view {
            if (++i >= argc) throw std::runtime_error("missing value after " + std::string(arg));
            return argv[i];
        };
        if (arg == "--input") o.input = next();
        else if (arg == "--output") o.output = next();
        else if (arg == "--metadata") o.metadata = next();
        else if (arg == "--format") o.format = next();
        else if (arg == "--output-format") o.output_format = next();
        else if (arg == "--channels") o.channels = integer(next(), "channel count");
        else if (arg == "--signal-gain") o.signal_gain = number(next(), "signal gain");
        else if (arg == "--sample-rate") o.sample_rate = number(next(), "sample rate");
        else if (arg == "--input-rate") o.input_rate = number(next(), "input rate");
        else if (arg == "--bits" || arg == "--obscured-bits" || arg == "--db" || arg == "--noise-watts") {
            if (o.level_set) throw std::runtime_error("choose only one background-noise level option");
            o.level_set = true;
            o.noise.value = number(next(), "noise level");
            if (arg == "--bits") o.noise.mode = Mode::EffectiveBits;
            else if (arg == "--obscured-bits") o.noise.mode = Mode::ObscuredBits;
            else if (arg == "--db") o.noise.mode = Mode::SuppressionDb;
            else o.noise.mode = Mode::RelativeWatts;
        } else if (arg == "--reference-watts") o.noise.reference_watts = number(next(), "reference watts");
        else if (arg == "--reference-rms") {
            const auto rms = number(next(), "reference RMS");
            if (rms <= 0) throw std::runtime_error("reference RMS must be positive");
            o.noise.reference_mean_square = rms * rms;
        } else if (arg == "--help") {
            std::cout << "background-noise --input FILE --output FILE --metadata FILE --format f32|s16 "
                         "[--output-format f32|s16] [--channels 1|2] [--signal-gain G] "
                         "[--bits 0..16 | --obscured-bits 0..16 | --db D | --noise-watts W "
                         "--reference-watts W --reference-rms R] [--input-rate HZ] [--sample-rate HZ]\n"
                         "Noise is added after full-rate audio resampling; references are final DAC units.\n";
            std::exit(0);
        } else throw std::runtime_error("unknown option: " + std::string(arg));
    }
    if (o.input.empty() || o.output.empty() || o.metadata.empty())
        throw std::runtime_error("--input, --output, and --metadata are required");
    if (o.format != "f32" && o.format != "s16") throw std::runtime_error("--format must be f32 or s16");
    if (o.output_format.empty()) o.output_format = o.format;
    if (o.output_format != "f32" && o.output_format != "s16") throw std::runtime_error("--output-format must be f32 or s16");
    if (o.channels < 1 || o.channels > 2) throw std::runtime_error("--channels must be one or two");
    if (o.input_rate == 0) o.input_rate = o.sample_rate;
    if (!std::isfinite(o.signal_gain) || o.signal_gain < .0001 || o.signal_gain > 1.75)
        throw std::runtime_error("signal gain must be within .0001..1.75");
    for (const auto rate : {o.input_rate, o.sample_rate})
        if (!std::isfinite(rate) || rate < 64 || rate > 120000000 || std::floor(rate) != rate)
            throw std::runtime_error("audio input/output rates must be integers in 64..120000000 Hz");
    if (o.noise.mode == Mode::RelativeWatts && (!o.noise.reference_watts || !o.noise.reference_mean_square))
        throw std::runtime_error("--noise-watts requires --reference-watts and --reference-rms");
    o.noise.full_scale = 1;
    return o;
}
std::string quote(const fs::path& p) {
    std::string s = p.string(), out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
class ExclusiveOutput {
public:
    explicit ExclusiveOutput(const fs::path& path) {
#ifdef _WIN32
        const int descriptor = _open(path.string().c_str(), _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL,
                                     _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) {
            file_ = _fdopen(descriptor, "wb");
            if (!file_) _close(descriptor);
        }
#else
        const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) {
            file_ = fdopen(descriptor, "wb");
            if (!file_) ::close(descriptor);
        }
#endif
        if (!file_) throw std::runtime_error("cannot create exclusive output: " + path.string());
    }
    ~ExclusiveOutput() { if (file_) std::fclose(file_); }
    ExclusiveOutput(const ExclusiveOutput&) = delete;
    ExclusiveOutput& operator=(const ExclusiveOutput&) = delete;
    void write(const void* data, std::size_t size) {
        if (size && std::fwrite(data, 1, size, file_) != size)
            throw std::runtime_error("output write failed");
    }
    void flush() { if (std::fflush(file_) != 0) throw std::runtime_error("output flush failed"); }
private:
    std::FILE* file_ = nullptr;
};
std::uint64_t samples_count(std::uintmax_t bytes, const Options& o) {
    const std::size_t width = (o.format == "f32" ? 4 : 2) * o.channels;
    require(bytes != 0 && bytes % width == 0, "input must be nonempty and aligned to complete audio frames");
    return bytes / width;
}
double sample_f32(std::ifstream& in) {
    float value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in || !std::isfinite(value)) throw std::runtime_error("truncated or nonfinite f32 input");
    return static_cast<double>(value);
}
std::int16_t sample_s16(std::ifstream& in) {
    std::uint16_t bits{};
    in.read(reinterpret_cast<char*>(&bits), sizeof(bits));
    if (!in) throw std::runtime_error("truncated S16 input");
    return std::bit_cast<std::int16_t>(bits);
}
void write_float(ExclusiveOutput& out, float value) { out.write(&value, sizeof(value)); }
void write_s16(ExclusiveOutput& out, std::int16_t value) {
    const auto bits = std::bit_cast<std::uint16_t>(value);
    out.write(&bits, sizeof(bits));
}

int run(const Options& o) {
    const auto started = std::chrono::steady_clock::now();
    const auto cpu_started = std::clock();
    const auto in_abs = fs::weakly_canonical(o.input);
    const auto out_abs = fs::absolute(o.output).lexically_normal();
    const auto meta_abs = fs::absolute(o.metadata).lexically_normal();
    require(in_abs != out_abs && in_abs != meta_abs && out_abs != meta_abs,
            "input, output, and metadata paths must be distinct");
    require(fs::is_regular_file(in_abs), "input must be a regular file");
    require(!fs::exists(out_abs) && !fs::exists(meta_abs), "output and metadata must not already exist");
    const auto count = samples_count(fs::file_size(in_abs), o);
    const auto level = resolve(o.noise);
    require(level.peak <= 1., "selected noise level cannot fit within normalized DAC full scale");

    // Validate format, identical stereo input, and source headroom before
    // creating artifacts. A resampling filter may overshoot that source peak;
    // the final-rate mixer also enforces headroom sample by sample. A failure
    // can leave partial PCM, but never a complete receipt or clipped output.
    double input_peak = 0, input_sumsq = 0;
    std::int64_t minimum_output_code = 0, maximum_output_code = 0;
    {
        std::ifstream in(in_abs, std::ios::binary);
        require(static_cast<bool>(in), "cannot open input");
        for (std::uint64_t i = 0; i < count; ++i) {
            double a, b = 0;
            if (o.format == "f32") { a = sample_f32(in); if (o.channels == 2) b = sample_f32(in); }
            else { a = static_cast<double>(sample_s16(in)) / 32768.; if (o.channels == 2) b = static_cast<double>(sample_s16(in)) / 32768.; }
            require(o.channels != 2 || a == b, "stereo fixture must contain identical mono channels");
            const double scaled = std::abs(a * o.signal_gain);
            input_peak = std::max(input_peak, scaled);
            input_sumsq += scaled * scaled;
            if (o.output_format == "s16") {
                const auto code = o.format == "s16"
                    ? static_cast<std::int64_t>(std::llround(a * o.signal_gain * 32768.))
                    : static_cast<std::int64_t>(std::clamp(static_cast<float>(a * o.signal_gain), -1.f, 1.f) * 32767.f);
                minimum_output_code = std::min(minimum_output_code, code);
                maximum_output_code = std::max(maximum_output_code, code);
            }
        }
    }
    require(input_peak + level.peak <= 1., "noise plus source peak exceeds DAC range; apply fixed source headroom");
    if (o.output_format == "s16") {
        const double code_variance = level.variance * 32767. * 32767.;
        require(code_variance == 0 || code_variance >= 0x1.0p-32,
                "noise is below supported S16 probability precision; use floating output or a higher level");
        const auto code_peak = static_cast<std::int64_t>(std::ceil(level.peak * 32767.));
        require(minimum_output_code - code_peak >= -32768 && maximum_output_code + code_peak <= 32767,
                "noise plus source peak exceeds S16 range; apply fixed source headroom");
    }

    datapump::audio::PlaybackOutput mixer(static_cast<std::uint32_t>(o.sample_rate), o.noise, o.signal_gain);
    std::ifstream in(in_abs, std::ios::binary);
    require(static_cast<bool>(in), "cannot open PCM input");
    ExclusiveOutput out(out_abs);
    std::uint64_t output_count = 0;
    std::array<std::int16_t, 8192> packed{};
    auto emit = [&](std::span<float> mono) {
        if (o.output_format == "s16") {
            auto codes = std::span(packed).first(mono.size()*o.channels);
            mixer.pack_s16(mono, codes, o.channels, datapump::audio::ChannelMode::stereo);
            out.write(codes.data(), codes.size_bytes());
        } else {
            mixer.mix_float(mono);
            for (const float x : mono) {
                const float value = static_cast<float>(x*mixer.provider_gain());
                write_float(out, value);
                if (o.channels == 2) write_float(out, value);
            }
        }
        output_count += mono.size();
    };
    // Preserve exact cached S16 codes when no rate conversion is necessary.
    // Every other path goes through the same full-rate float finalizer used by
    // providers; the noise is never passed through the source resampler.
    if (o.format == "s16" && o.output_format == "s16" && o.input_rate == o.sample_rate) {
        std::array<std::int16_t,4096> mono{};
        for (std::uint64_t first=0; first<count;) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(mono.size(),count-first));
            for (std::size_t i=0;i<n;++i) {
                mono[i]=sample_s16(in);
                if(o.channels==2)require(sample_s16(in)==mono[i],"stereo input changed after validation");
            }
            mixer.mix_s16(std::span(mono).first(n));
            for(std::size_t i=0;i<n;++i) {
                write_s16(out,mono[i]);if(o.channels==2)write_s16(out,mono[i]);
            }
            first+=n;output_count+=n;
        }
    } else {
        datapump::audio::Resampler converter(static_cast<std::uint32_t>(o.input_rate),
                                             static_cast<std::uint32_t>(o.sample_rate));
        std::array<float,4096> input{}, output{};
        std::size_t offset=0, loaded=0;std::uint64_t read=0;
        while (!converter.finished()) {
            if(offset==loaded && read<count) {
                loaded=static_cast<std::size_t>(std::min<std::uint64_t>(input.size(),count-read));offset=0;
                for(std::size_t i=0;i<loaded;++i) {
                    const double value=o.format=="f32"?sample_f32(in):static_cast<double>(sample_s16(in))/32768.;
                    if(o.channels==2) {
                        const double right=o.format=="f32"?sample_f32(in):static_cast<double>(sample_s16(in))/32768.;
                        require(value==right,"stereo input changed after validation");
                    }
                    input[i]=static_cast<float>(value);
                }
                read+=loaded;
            }
            const auto progress=converter.process(std::span(input).first(loaded).subspan(offset),output,read==count);
            offset+=progress.consumed;
            emit(std::span(output).first(progress.produced));
            require(progress.consumed || progress.produced || converter.finished(),"audio renderer made no progress");
        }
    }
    out.flush();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double cpu = static_cast<double>(std::clock() - cpu_started) / CLOCKS_PER_SEC;
    const auto report=mixer.report();
    const double actual_mean=report.measured_mean, actual_var=report.measured_variance;
    const char* mode_name = "off";
    switch (o.noise.mode) {
    case Mode::Off: mode_name = "off"; break;
    case Mode::EffectiveBits: mode_name = "effective-bits"; break;
    case Mode::ObscuredBits: mode_name = "obscured-bits"; break;
    case Mode::SuppressionDb: mode_name = "dB-below-full-scale-sine"; break;
    case Mode::RelativeWatts: mode_name = "relative-watts"; break;
    }
    std::ostringstream metadata;
    metadata << std::setprecision(17)
         << "{\n  \"schema\":\"background-noise-audio-v2\",\n  \"status\":\"complete\",\n"
         << "  \"input\":" << quote(in_abs) << ",\n  \"output\":" << quote(out_abs) << ",\n"
         << "  \"format\":\"" << o.format << "\",\n  \"output_format\":\"" << o.output_format << "\",\n"
         << "  \"channels\":" << o.channels << ",\n  \"sample_rate_hz\":" << o.sample_rate << ",\n"
         << "  \"samples\":" << output_count << ",\n  \"input_samples\":" << count << ",\n"
         << "  \"input_rate_hz\":" << o.input_rate << ",\n  \"stage\":\"shared full-rate audio finalizer after resampling\",\n"
         << "  \"source_peak\":" << input_peak << ",\n"
         << "  \"source_mean_square\":" << input_sumsq / static_cast<double>(count) << ",\n"
         << "  \"mode\":\"" << mode_name << "\",\n  \"level_value\":" << o.noise.value << ",\n"
         << "  \"reference_mean_square\":" << o.noise.reference_mean_square << ",\n"
         << "  \"reference_watts\":" << o.noise.reference_watts << ",\n  \"signal_gain\":" << o.signal_gain << ",\n"
         << "  \"expected_noise_variance\":" << level.variance << ",\n"
         << "  \"measured_noise_mean\":" << actual_mean << ",\n  \"measured_noise_variance\":" << actual_var << ",\n"
         << "  \"noise_peak_bound\":" << (o.output_format == "s16" ? std::ceil(level.peak*32767.)/32767. : level.peak)
         << ",\n  \"clipped_samples\":0,\n"
         << "  \"key_policy\":\"fresh OS-seeded AES-256-CTR key per invocation; key is ephemeral and never stored\",\n"
         << "  \"iv_policy\":\"system-clock nanoseconds in high 64 bits; counter starts at zero under the fresh key\",\n"
         << "  \"noise_scope\":\"additive independent sample noise across the represented PCM Nyquist band before DAC; downstream shaping is not compensated\",\n"
         << "  \"cpu_seconds\":" << cpu << ",\n  \"wall_seconds\":" << elapsed << "\n}\n";
    ExclusiveOutput meta(meta_abs);
    const auto metadata_text = metadata.str();
    meta.write(metadata_text.data(), metadata_text.size());
    meta.flush();
    std::cout << "samples=" << count << " expected_variance=" << level.variance
              << " measured_variance=" << actual_var << " output=" << out_abs << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& e) { std::cerr << "background-noise: " << e.what() << '\n'; return 2; }
}
