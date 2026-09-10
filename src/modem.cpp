#include "datapump/modem.hpp"
#include "datapump/crypto.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <istream>
#include <initializer_list>
#include <utility>
#include <limits>
#include <numbers>
#include <ostream>
#include <random>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr double amplitude = 0.7;
constexpr std::array<Complex, 4> shifts{{{1,0}, {0,1}, {0,-1}, {-1,0}}};
void check(bool ok, const char* message) { if (!ok) throw Error(message); }
std::size_t product(std::size_t a, std::size_t b, std::size_t limit) {
    check(b == 0 || a <= limit / b, "modem memory limit exceeded");
    return a * b;
}
void budget(std::size_t limit, std::initializer_list<std::pair<std::size_t,std::size_t>> allocations) {
    for (auto [count, width] : allocations) {
        const auto bytes = product(count, width, limit);
        limit -= bytes;
    }
}
std::size_t chip_samples(const Config& c) {
    return static_cast<std::size_t>(std::llround(c.sample_rate / (c.bandwidth_hz / 2) / 4)) * 4;
}
std::size_t symbol_samples(const Config& c) { return chip_samples(c) * c.spreading_factor; }
void finite_samples(std::span<const float> samples, std::size_t limit) {
    product(samples.size(), sizeof(float), limit);
    for (float v : samples) check(std::isfinite(v), "non-finite audio sample");
}
std::vector<int> signs(std::size_t chips, const Config& c) {
    product(chips, sizeof(int) + 1, c.memory_limit);
    std::vector<int> out(chips, 1);
    if (c.scramble) {
        Crypto crypto(c.spreading_seed);
        auto stream = crypto.stream(StreamPurpose::Scrambler, 0, 0, (chips + 7) / 8);
        for (std::size_t i = 0; i < chips; ++i)
            out[i] = ((stream[i / 8] >> (i % 8)) & 1) ? -1 : 1;
    } else if (c.spreading_factor > 1) {
        constexpr std::array<int, 8> pattern{1, 1, -1, 1, -1, -1, 1, -1};
        for (std::size_t i = 0; i < chips; ++i) out[i] = pattern[(i % c.spreading_factor) % pattern.size()];
    }
    if (c.dsss) {
        Crypto crypto(c.dsss_seed);
        auto stream = crypto.stream(StreamPurpose::Dsss, 0, 0, (chips + 7) / 8);
        for (std::size_t i = 0; i < chips; ++i)
            if ((stream[i / 8] >> (i % 8)) & 1) out[i] = -out[i];
    }
    return out;
}
std::vector<Complex> phases(std::span<const std::uint8_t> bytes) {
    std::vector<Complex> out;
    out.reserve(bytes.size() * 4);
    Complex phase{1, 0};
    for (auto byte : bytes) for (int shift = 6; shift >= 0; shift -= 2) {
        phase *= shifts[(byte >> shift) & 3];
        out.push_back(phase);
    }
    return out;
}
void fft(std::vector<Complex>& data, bool inverse) {
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length *= 2) {
        const Complex root = std::polar(1.0, (inverse ? tau : -tau) / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            Complex w{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                Complex u = data[i+j], v = data[i+j+length/2] * w;
                data[i+j] = u + v; data[i+j+length/2] = u - v; w *= root;
            }
        }
        if (length == n) break;
    }
    if (inverse) for (auto& v : data) v /= static_cast<double>(n);
}
std::size_t fft_size(std::size_t minimum, std::size_t limit, std::size_t bytes_per_item) {
    std::size_t n = 1;
    while (n < minimum) { check(n <= limit / 2, "FFT size exceeds memory limit"); n *= 2; }
    product(n, bytes_per_item, limit);
    return n;
}
// Integrate over one chip, sampled four times per chip. Mixing is phase blind:
// absolute RF/audio phase is removed by differential matching below.
std::vector<Complex> baseband(std::span<const float> samples, const Config& c) {
    const auto chip = chip_samples(c), stride = chip / 4;
    if (samples.size() < chip) return {};
    std::vector<Complex> out;
    out.reserve((samples.size() - chip) / stride + 1);
    std::vector<Complex> ring(chip);
    const auto step = std::polar(1.0, -tau * c.carrier_hz / c.sample_rate);
    Complex oscillator{1,0}, sum{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto x = static_cast<double>(samples[i]) * oscillator;
        oscillator *= step;
        sum += x - ring[i % chip]; ring[i % chip] = x;
        if (i + 1 >= chip && (i + 1 - chip) % stride == 0)
            out.push_back(sum * (2.0 / static_cast<double>(chip)));
    }
    return out;
}
std::vector<Complex> differential(const std::vector<Complex>& x) {
    if (x.size() <= 4) return {};
    std::vector<Complex> out(x.size() - 4);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = x[i+4] * std::conj(x[i]);
    return out;
}
struct Acquisition { std::size_t offset; double correlation; };
Acquisition acquire(std::span<const float> samples, std::span<const float> reference, const Config& c) {
    auto signal = differential(baseband(samples, c));
    auto expected = differential(baseband(reference, c));
    check(!expected.empty() && signal.size() >= expected.size(), "capture shorter than preamble");
    const auto signal_count = signal.size(), expected_count = expected.size();
    // Two FFT buffers plus signal energy prefix, temporary baseband, and audio.
    const auto n = fft_size(signal_count + expected_count - 1, c.memory_limit, 80);
    std::vector<double> energy(signal_count + 1);
    for (std::size_t i = 0; i < signal_count; ++i) energy[i+1] = energy[i] + std::norm(signal[i]);
    double reference_energy = 0;
    for (auto x : expected) reference_energy += std::norm(x);
    signal.resize(n); expected.resize(n);
    fft(signal, false); fft(expected, false);
    for (std::size_t i = 0; i < n; ++i) signal[i] *= std::conj(expected[i]);
    fft(signal, true);
    Acquisition best{0, 0};
    for (std::size_t offset = 0; offset + expected_count <= signal_count; ++offset) {
        const double denominator = std::sqrt(reference_energy * std::max(0.0, energy[offset+expected_count]-energy[offset]));
        const double score = denominator > 1e-20 ? std::abs(signal[offset]) / denominator : 0;
        if (score > best.correlation) best = {offset * (chip_samples(c) / 4), score};
    }
    check(best.correlation > 0.35, "no matching modem preamble found");
    return best;
}
std::vector<Complex> symbols(std::span<const float> samples, std::size_t offset,
                             std::size_t count, const Config& c, const std::vector<int>& code) {
    const auto chip = chip_samples(c), duration = symbol_samples(c);
    std::vector<Complex> out(count);
    auto oscillator = std::polar(1.0, -tau * c.carrier_hz * static_cast<double>(offset) / c.sample_rate);
    const auto step = std::polar(1.0, -tau * c.carrier_hz / c.sample_rate);
    for (std::size_t j = 0; j < count; ++j) {
        Complex sum{}; std::size_t actual = 0;
        for (std::size_t k = 0; k < duration && offset < samples.size(); ++k, ++offset) {
            sum += static_cast<double>(samples[offset]) * oscillator * static_cast<double>(code[j*c.spreading_factor+k/chip]);
            oscillator *= step; ++actual;
        }
        out[j] = actual ? sum * (2.0 / static_cast<double>(actual)) : Complex{};
    }
    return out;
}
struct Fit { double score; double rotation; Complex gain; };
Fit fit(const std::vector<Complex>& received, const std::vector<Complex>& expected) {
    // Regress the unwrapped phase of known training symbols. Averaging raw
    // adjacent products biases long-preamble timing toward accidental noise.
    double sx = 0, sy = 0, sxx = 0, sxy = 0, previous = 0, unwrapped = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double phase = std::arg(received[i] * std::conj(expected[i]));
        if (i == 0) unwrapped = phase;
        else unwrapped += std::remainder(phase - previous, tau);
        previous = phase;
        const double x = static_cast<double>(i);
        sx += x; sy += unwrapped; sxx += x*x; sxy += x*unwrapped;
    }
    const double n = static_cast<double>(expected.size());
    const double denominator = n*sxx - sx*sx;
    const double angle = denominator > 0 ? (n*sxy - sx*sy) / denominator : 0;
    Complex sum{}; double energy = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        sum += received[i] * std::conj(expected[i]) * std::polar(1.0, -angle * static_cast<double>(i));
        energy += std::norm(received[i]);
    }
    return {energy > 1e-20 ? std::norm(sum) / (energy * static_cast<double>(expected.size())) : 0,
            angle, sum / static_cast<double>(expected.size())};
}
void put16(std::ostream& out, std::uint16_t value) {
    const char b[2]{static_cast<char>(value), static_cast<char>(value >> 8)}; out.write(b,2);
}
void put32(std::ostream& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value)); put16(out, static_cast<std::uint16_t>(value >> 16));
}
void exact(std::istream& in, char* destination, std::size_t size) {
    check(size <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()), "WAV read too large");
    check(static_cast<bool>(in.read(destination, static_cast<std::streamsize>(size))), "truncated WAV");
}
std::uint16_t get16(const unsigned char* b) { return static_cast<std::uint16_t>(b[0] | (b[1] << 8)); }
std::uint32_t get32(const unsigned char* b) { return get16(b) | (static_cast<std::uint32_t>(get16(b+2)) << 16); }
void discard(std::istream& in, std::size_t count) {
    std::array<char,4096> buffer{};
    while (count) { const auto n = std::min(count, buffer.size()); exact(in, buffer.data(), n); count -= n; }
}
}
void validate(const Config& c) {
    check(c.sample_rate >= 8000 && c.sample_rate <= 384000, "sample rate must be 8000..384000 Hz");
    check(std::isfinite(c.bandwidth_hz) && c.bandwidth_hz >= 1 && c.bandwidth_hz <= c.sample_rate / 2.0,
          "bandwidth must be finite and within 1 Hz..Nyquist");
    check(std::isfinite(c.carrier_hz) && c.carrier_hz >= c.bandwidth_hz / 2 &&
          c.carrier_hz + c.bandwidth_hz / 2 <= c.sample_rate / 2.0,
          "carrier and bandwidth must fit inside audio passband");
    check(std::isfinite(c.training_seconds) && c.training_seconds >= 5 && c.training_seconds <= 32768,
          "training must last 5..32768 seconds");
    check(c.spreading_factor >= 1 && c.spreading_factor <= 16384, "spreading factor must be 1..16384");
    check(c.memory_limit >= 1024, "modem memory limit must be at least 1024 bytes");
    check(chip_samples(c) >= 4, "chip sampling is too fast");
    product(chip_samples(c), c.spreading_factor, std::numeric_limits<std::size_t>::max());
}
double bit_rate(const Config& c) { validate(c); return 2.0 * c.sample_rate / static_cast<double>(symbol_samples(c)); }
Bytes preamble(const Config& c) {
    validate(c);
    const auto length = std::max<std::size_t>(12, static_cast<std::size_t>(std::ceil(c.training_seconds * bit_rate(c) / 8)));
    product(length, 1, c.memory_limit);
    constexpr std::array<std::uint8_t, 6> text{'U','3','<','Z','i','f'};
    Bytes out(length);
    for (std::size_t i = 0; i < length; ++i) out[i] = text[i % text.size()];
    return out;
}
std::vector<float> modulate(std::span<const std::uint8_t> bytes, const Config& c) {
    validate(c);
    const auto count = product(bytes.size(), 4, c.memory_limit);
    const auto chip = chip_samples(c), duration = symbol_samples(c);
    const auto sample_count = product(count, duration, c.memory_limit / sizeof(float));
    budget(c.memory_limit, {{sample_count,sizeof(float)}, {count,sizeof(Complex)},
                             {count*c.spreading_factor,sizeof(int)}, {(count*c.spreading_factor+7)/8,1}});
    const auto code = signs(count * c.spreading_factor, c);
    const auto phase = phases(bytes);
    std::vector<float> out(sample_count);
    const auto step = std::polar(1.0, tau * c.carrier_hz / c.sample_rate);
    Complex oscillator{1, 0};
    for (std::size_t i = 0; i < sample_count; ++i) {
        out[i] = static_cast<float>(amplitude * (phase[i / duration] * oscillator).real() * code[i / chip]);
        oscillator *= step;
    }
    return out;
}
DecodeResult demodulate(std::span<const float> samples, const Config& c,
                        std::span<const std::uint8_t> expected_preamble) {
    validate(c); finite_samples(samples, c.memory_limit);
    check(!expected_preamble.empty(), "expected preamble is required");
    const auto duration = symbol_samples(c), chip = chip_samples(c);
    const auto training_symbols = product(expected_preamble.size(),4,c.memory_limit);
    const auto training_samples = product(training_symbols,duration,c.memory_limit/sizeof(float));
    check(samples.size() >= training_samples, "capture shorter than preamble");
    const auto stride = chip/4;
    const auto signal_bins = (samples.size()-chip)/stride+1;
    const auto training_bins = (training_samples-chip)/stride+1;
    const auto fft_count = fft_size(signal_bins+training_bins,c.memory_limit,sizeof(Complex));
    // Bound every large allocation before materializing baseband or FFT arrays.
    // 64 bytes per FFT item covers two FFTs, energy prefixes, and temporary
    // baseband/differential copies, with input and reference audio accounted too.
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {training_samples,sizeof(float)},
                             {fft_count,64}, {chip,sizeof(Complex)}});
    const auto maximum_symbols = (samples.size()+duration/2)/duration;
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {training_samples,sizeof(float)},
                             {maximum_symbols,sizeof(Complex)}, {training_symbols,sizeof(Complex)*2},
                             {maximum_symbols*c.spreading_factor,sizeof(int)+1}});
    const auto reference = modulate(expected_preamble, c);
    const auto acquired = acquire(samples, reference, c);
    const auto expected = phases(expected_preamble);
    const auto code = signs(((samples.size() + duration / 2) / duration) * c.spreading_factor, c);
    auto radius = chip / 4;
    Fit best{0,0,{}}; std::size_t start = acquired.offset;
    // Hierarchical refinement bounds trial count even for multi-second chips.
    // Each stage evaluates at most 17 candidates, ending at single samples.
    for (;;) {
        const auto step = std::max<std::size_t>(1,radius/8);
        const auto low = start > radius ? start-radius : 0;
        const auto high = std::min(start+radius,samples.size()-reference.size());
        for (std::size_t trial = low;; trial = std::min(high,trial+step)) {
            const auto received = symbols(samples,trial,expected.size(),c,code);
            const auto candidate = fit(received,expected);
            if (candidate.score > best.score) { best = candidate; start = trial; }
            if (trial == high) break;
        }
        if (step == 1) break;
        radius = step;
    }
    check(best.score >= .60, "preamble does not validate after timing search");
    const auto count = ((samples.size() - start + chip / 2) / duration / 4) * 4;
    auto received = symbols(samples, start, count, c, code);
    DecodeResult out;
    out.bytes.resize(count / 4);
    Complex previous = best.gain;
    double error_energy = 0, signal_energy = 0;
    for (std::size_t j = 0; j < count; ++j) {
        const auto point = received[j] * std::polar(1.0, -best.rotation * static_cast<double>(j));
        const auto delta = point * std::conj(previous);
        unsigned closest = 0; double distance = -std::numeric_limits<double>::infinity();
        for (unsigned k = 0; k < 4; ++k) {
            const double score = (delta * std::conj(shifts[k])).real();
            if (score > distance) { distance = score; closest = k; }
        }
        out.bytes[j/4] |= static_cast<std::uint8_t>(closest << (6 - 2 * (j % 4)));
        if (out.diagnostics.constellation.size() < 2048)
            out.diagnostics.constellation.push_back(std::abs(delta) > 1e-20 ? delta / std::abs(delta) : Complex{});
        if (j < expected.size()) {
            signal_energy += std::norm(best.gain);
            error_energy += std::norm(point - best.gain * expected[j]);
        }
        previous = point;
    }
    out.diagnostics.sample_offset = start;
    out.diagnostics.preamble_correlation = std::sqrt(best.score);
    out.diagnostics.snr_db = 10 * std::log10(signal_energy / std::max(error_energy, 1e-20));
    out.diagnostics.bit_rate = bit_rate(c);
    const auto display_stride = std::max<std::size_t>(1, samples.size() / 2048);
    for (std::size_t i = 0; i < samples.size() && out.diagnostics.waveform.size() < 2048; i += display_stride)
        out.diagnostics.waveform.push_back(samples[i]);
    return out;
}
std::vector<float> simulate(std::span<const float> samples, const Config& c, const ChannelConfig& channel) {
    validate(c); finite_samples(samples, c.memory_limit);
    check(std::isfinite(channel.snr_db) && channel.snr_db >= -300 && channel.snr_db <= 300,
          "simulation SNR must be finite and within -300..300 dB");
    check(std::isfinite(channel.frequency_offset_hz) && std::abs(channel.frequency_offset_hz) < c.sample_rate / 2,
          "invalid simulation frequency offset");
    check(channel.delay_samples <= c.memory_limit / sizeof(float) - samples.size(), "simulation delay exceeds memory limit");
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {samples.size()+channel.delay_samples,sizeof(float)}});
    std::vector<float> out(samples.size() + channel.delay_samples);
    double power = 0;
    for (auto v : samples) power += static_cast<double>(v) * v;
    power = samples.empty() ? 0 : power / static_cast<double>(samples.size());
    if (channel.frequency_offset_hz == 0) std::copy(samples.begin(), samples.end(), out.begin() + channel.delay_samples);
    else if (!samples.empty()) {
        const auto n = fft_size(samples.size(), c.memory_limit, sizeof(Complex));
        budget(c.memory_limit, {{samples.size(),sizeof(float)}, {out.size(),sizeof(float)}, {n,sizeof(Complex)}});
        std::vector<Complex> analytic(n);
        std::copy(samples.begin(), samples.end(), analytic.begin());
        fft(analytic, false);
        for (std::size_t k = 1; k < n/2; ++k) analytic[k] *= 2;
        for (std::size_t k = n/2+1; k < n; ++k) analytic[k] = 0;
        fft(analytic, true);
        for (std::size_t i = 0; i < samples.size(); ++i)
            out[i+channel.delay_samples] = static_cast<float>((analytic[i] * std::polar(1.0, tau * channel.frequency_offset_hz * static_cast<double>(i) / c.sample_rate)).real());
    }
    const double sigma = std::sqrt(power * std::pow(10.0, -channel.snr_db / 10));
    std::mt19937_64 generator(channel.seed);
    std::normal_distribution<double> normal(0, sigma > 0 ? sigma : 1);
    if (sigma > 0) for (auto& v : out) v = static_cast<float>(v + normal(generator));
    finite_samples(out, c.memory_limit);
    return out;
}
void write_wav(std::ostream& out, std::span<const float> samples, std::uint32_t rate) {
    check(rate >= 8000 && rate <= 384000, "invalid WAV sample rate");
    check(samples.size() <= (std::numeric_limits<std::uint32_t>::max() - 36) / 2, "WAV is too large");
    finite_samples(samples, std::numeric_limits<std::size_t>::max());
    out.write("RIFF",4); put32(out, static_cast<std::uint32_t>(36+samples.size()*2)); out.write("WAVEfmt ",8);
    put32(out,16); put16(out,1); put16(out,1); put32(out,rate); put32(out,rate*2); put16(out,2); put16(out,16);
    out.write("data",4); put32(out,static_cast<std::uint32_t>(samples.size()*2));
    for (auto v : samples) {
        const auto quantized = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(v), -1.0, 32767.0/32768) * 32768));
        put16(out, static_cast<std::uint16_t>(quantized));
    }
    check(static_cast<bool>(out), "failed to write WAV");
}
Wav read_wav(std::istream& in, std::size_t limit) {
    std::array<unsigned char,16> b{};
    exact(in,reinterpret_cast<char*>(b.data()),12);
    check(std::memcmp(b.data(),"RIFF",4)==0 && std::memcmp(b.data()+8,"WAVE",4)==0, "not a RIFF WAVE file");
    auto remaining = static_cast<std::uint64_t>(get32(b.data()+4));
    check(remaining >= 4, "invalid RIFF size"); remaining -= 4;
    check(remaining <= 1024*1024 || (remaining-1024*1024+1)/2 <= limit, "WAV container exceeds memory policy");
    Wav result{}; bool have_format = false, have_data = false;
    while (remaining) {
        check(remaining >= 8, "truncated WAV chunk header");
        exact(in,reinterpret_cast<char*>(b.data()),8); remaining -= 8;
        const auto size = static_cast<std::uint64_t>(get32(b.data()+4));
        const auto padded_size = size + (size & 1);
        check(padded_size <= remaining, "WAV chunk extends beyond RIFF container");
        if (std::memcmp(b.data(),"fmt ",4)==0) {
            check(!have_format && size >= 16, "invalid or repeated WAV format chunk");
            exact(in,reinterpret_cast<char*>(b.data()),16);
            result.sample_rate = get32(b.data()+4);
            check(get16(b.data())==1 && get16(b.data()+2)==1 && get16(b.data()+14)==16 && get16(b.data()+12)==2,
                  "only PCM16 mono WAV is supported");
            check(result.sample_rate >= 8000 && result.sample_rate <= 384000 && get32(b.data()+8)==result.sample_rate*2,
                  "invalid WAV sample rate or byte rate");
            discard(in,static_cast<std::size_t>(size-16)); have_format = true;
        } else if (std::memcmp(b.data(),"data",4)==0) {
            check(have_format && !have_data && size % 2 == 0, "invalid or repeated WAV data chunk");
            product(static_cast<std::size_t>(size/2),sizeof(float),limit);
            result.samples.resize(static_cast<std::size_t>(size/2));
            for (auto& sample : result.samples) {
                exact(in,reinterpret_cast<char*>(b.data()),2);
                const auto value = get16(b.data());
                sample = static_cast<float>((value < 32768 ? static_cast<int>(value) : static_cast<int>(value)-65536) / 32768.0);
            }
            have_data = true;
        } else discard(in,static_cast<std::size_t>(size));
        if (size & 1) discard(in,1);
        remaining -= padded_size;
    }
    check(have_format && have_data, "WAV is missing format or data"); return result;
}
std::vector<float> modulate_status(std::span<const std::uint8_t> bits, const Config& c) {
    validate(c);
    const auto duration = symbol_samples(c), chip = chip_samples(c);
    const auto count = product(bits.size(),duration,c.memory_limit / (sizeof(float)+1));
    budget(c.memory_limit, {{count,sizeof(float)}, {bits.size()*c.spreading_factor,sizeof(int)+1}});
    const auto code = signs(bits.size()*c.spreading_factor,c);
    std::vector<float> out(count);
    Complex phase{1,0}, oscillator{1,0};
    const auto step = std::polar(1.0,tau*c.carrier_hz/c.sample_rate);
    for (std::size_t j=0; j<bits.size(); ++j) {
        check(bits[j] <= 1,"status bits must be 0 or 1");
        if (bits[j]) phase = -phase;
        for (std::size_t k=0; k<duration; ++k) {
            out[j*duration+k] = static_cast<float>(amplitude*(phase*oscillator).real()*code[j*c.spreading_factor+k/chip]);
            oscillator *= step;
        }
    }
    return out;
}
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> bits, const Config& c) {
    validate(c); finite_samples(samples,c.memory_limit);
    check(!bits.empty(),"known status bits are required");
    const auto expected_count = product(bits.size(),symbol_samples(c),c.memory_limit/sizeof(float));
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {expected_count,sizeof(float)},
                             {bits.size()*c.spreading_factor,sizeof(int)+1}});
    const auto reference = modulate_status(bits,c);
    check(samples.size() == reference.size(),"status detector requires exactly the known symbol duration");
    double dot=0, signal=0, expected=0;
    for (std::size_t i=0; i<samples.size(); ++i) { dot+=samples[i]*reference[i]; signal+=samples[i]*samples[i]; expected+=reference[i]*reference[i]; }
    return signal > 0 && expected > 0 ? dot/std::sqrt(signal*expected) : 0;
}
}
