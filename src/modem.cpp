#include "datapump/modem.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
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

void check(bool ok, const char* message) { if (!ok) throw Error(message); }
void check_cancelled(const std::stop_token& stop) {
    if (stop.stop_requested()) throw Error("modem operation cancelled");
}
void periodic_cancel(std::size_t position, const std::stop_token& stop) {
    if ((position & 4095U) == 0) check_cancelled(stop);
}
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
    return static_cast<std::size_t>(std::ceil(2.*c.sample_rate/c.bandwidth_hz));
}
void finite_samples(std::span<const float> samples, std::size_t limit, std::stop_token stop = {}) {
    product(samples.size(), sizeof(float), limit);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        periodic_cancel(i, stop);
        check(std::isfinite(samples[i]), "non-finite audio sample");
    }
}
void fft(std::vector<Complex>& data, bool inverse, std::stop_token stop = {}) {
    check_cancelled(stop);
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        periodic_cancel(i, stop);
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length *= 2) {
        check_cancelled(stop);
        const Complex root = std::polar(1.0, (inverse ? tau : -tau) / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            periodic_cancel(i, stop);
            Complex w{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                periodic_cancel(i + j, stop);
                Complex u = data[i+j], v = data[i+j+length/2] * w;
                data[i+j] = u + v; data[i+j+length/2] = u - v; w *= root;
            }
        }
        if (length == n) break;
    }
    if (inverse) for (std::size_t i = 0; i < n; ++i) {
        periodic_cancel(i, stop);
        data[i] /= static_cast<double>(n);
    }
}
std::size_t fft_size(std::size_t minimum, std::size_t limit, std::size_t bytes_per_item) {
    std::size_t n = 1;
    while (n < minimum) { check(n <= limit / 2, "FFT size exceeds memory limit"); n *= 2; }
    product(n, bytes_per_item, limit);
    return n;
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
    check(c.pattern_symbols && c.constellation_bits==1,
          "APSK transport has been removed; use one-bit pattern transport");
    check(c.sample_rate >= 64 && c.sample_rate <= 120000000, "internal sample rate must be 64..120000000 Hz");
    check(c.stream_phase_samples < c.sample_rate, "symbol stream phase must be within its whole second");
    check(std::isfinite(c.bandwidth_hz) && c.bandwidth_hz >= 1 && c.bandwidth_hz <= 30000000 && c.bandwidth_hz <= c.sample_rate / 2.0,
          "bandwidth must be finite and within 1 Hz..30 MHz and internal Nyquist");
    check(std::isfinite(c.training_seconds) && c.training_seconds == 2,
          "normal training duration is fixed at 2 seconds");
    check(std::isfinite(c.integration_seconds) && c.integration_seconds>=0,"invalid integration duration");
    check(c.spreading_factor >= 1 && c.spreading_factor <= 16384, "spreading factor must be 1..16384");
    check(c.spreading_mode == SpreadingMode::pattern || c.spreading_mode == SpreadingMode::tone,"unknown spreading mode");
    const auto samples=symbol_sample_count(c);
    const auto chip=chip_samples(c);
    // Do not call pattern_pulse_enabled here: its chip helper validates this
    // same configuration. Use the identical complete-chip eligibility rule.
    const bool shaped=c.pulse_shaping && c.spreading_mode==SpreadingMode::pattern &&
        samples/chip>=2*pattern_pulse_half_span;
    // This is the intended RRC support including rolloff, not a certified
    // emission mask. Finite pulse truncation and limiting still leave tails.
    const auto half_band=shaped ? (1+pattern_pulse_rolloff)*c.sample_rate/(2.*static_cast<double>(chip)) : c.bandwidth_hz/2;
    check(std::isfinite(c.carrier_hz) && c.carrier_hz>=half_band &&
          c.carrier_hz+half_band<=c.sample_rate/2.,
          "carrier and waveform must fit above DC and below internal Nyquist; raise the carrier for short unshaped patterns or tone modes");
    check(c.spreading_mode != SpreadingMode::tone || (!c.scramble && !c.dsss && !c.data_key),
          "tone mode is unencrypted and cannot enable Data, Scrambler or DSSS keystreams");
    check(c.memory_limit >= 1024, "modem memory limit must be at least 1024 bytes");
    check(chip_samples(c) >= 4, "chip sampling is too fast");
    product(chip_samples(c), c.spreading_factor, std::numeric_limits<std::size_t>::max());
}
void validate_channel(const Config& c,const ChannelConfig& channel) {
    check(std::isfinite(channel.snr_db) && channel.snr_db>=-300 && channel.snr_db<=300,"channel SNR must be -300..300 dB");
    check(std::isfinite(channel.frequency_offset_hz) && std::abs(channel.frequency_offset_hz)<c.sample_rate/2.,"channel frequency offset must fit internal Nyquist");
    check(std::isfinite(channel.clock_error_ppm) && std::abs(channel.clock_error_ppm)<=10000,"relative clock error must be -10000..10000 ppm");
    check(std::isfinite(channel.phase_noise_degrees_per_sqrt_second) && channel.phase_noise_degrees_per_sqrt_second>=0 && channel.phase_noise_degrees_per_sqrt_second<=180,
          "phase diffusion must be 0..180 degrees per square-root second");
}
std::uint64_t symbol_sample_count(const Config& c) {
    if(c.integration_seconds>0) {
        const long double samples=std::ceil(static_cast<long double>(c.integration_seconds)*c.sample_rate);
        check(samples>=4 && samples<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),"integration duration exceeds 64-bit sample counter");
        return static_cast<std::uint64_t>(samples);
    }
    const long double samples=std::ceil(2.L*c.sample_rate*c.spreading_factor/c.bandwidth_hz);
    check(samples>=4 && samples<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),"symbol duration exceeds 64-bit sample counter");
    return static_cast<std::uint64_t>(samples);
}
std::uint64_t training_sample_count(const Config& c) {
    const auto target=static_cast<std::uint64_t>(c.sample_rate)*2;
    const auto symbol=symbol_sample_count(c);
    const auto count=target/symbol+(target%symbol>=symbol/2+symbol%2);
    check(count<=std::numeric_limits<std::uint64_t>::max()/symbol,"hardware preamble duration overflow");
    return count*symbol;
}
double symbol_seconds(const Config& c) { validate(c); return c.integration_seconds>0?c.integration_seconds:2.*c.spreading_factor/c.bandwidth_hz; }
double bit_rate(const Config& c) { return c.constellation_bits/symbol_seconds(c); }
std::size_t payload_symbol_count(std::size_t payload_bytes,const Config& c) {
    validate(c);
    return product(payload_bytes,8,std::numeric_limits<std::size_t>::max());
}
std::size_t waveform_sample_count(std::size_t wire_bytes,const Config& c) {
    validate(c);
    check(wire_bytes>0,"waveform requires at least one payload byte");
    const auto count=payload_symbol_count(wire_bytes,c);
    const auto duration=symbol_sample_count(c);
    check(duration<=std::numeric_limits<std::size_t>::max(),"symbol duration exceeds platform sample counter");
    const auto payload=product(count,static_cast<std::size_t>(duration),std::numeric_limits<std::size_t>::max());
    const auto padding=pattern_pulse_padding_samples(c);
    const auto training=training_sample_count(c);
    check(payload<=std::numeric_limits<std::size_t>::max()-training,"modem sample count overflow");
    const auto content=payload+static_cast<std::size_t>(training);
    check(padding<=(std::numeric_limits<std::size_t>::max()-content)/2,"pulse tail sample count overflow");
    return content+2*static_cast<std::size_t>(padding);
}
bool memory_supported(std::size_t wire_bytes,std::size_t preamble_bytes,const Config& c) {
    validate(c);
    try {
        check(preamble_bytes==0 && wire_bytes>0,
              "invalid estimated training length");
        const auto samples=waveform_sample_count(wire_bytes,c);
        budget(c.memory_limit,{{samples,sizeof(float)+sizeof(Complex)},{wire_bytes,2},{8*1024*1024,1}});
        return true;
    } catch(const Error&) {return false;}
}
Bytes preamble(const Config& c) {
    validate(c);
    // Hardware settling is generated directly by PatternTransmitter under
    // the same selected protections, using its separate preamble counters.
    return {};
}
std::vector<float> modulate(std::span<const std::uint8_t> bytes, const Config& c, std::stop_token stop) {
    check_cancelled(stop); validate(c);
    const auto count=waveform_sample_count(bytes.size(),c);
    budget(c.memory_limit,{{count,sizeof(float)},{bytes.size(),2},{65536,1}});
    StreamingTransmitter source(Bytes(bytes.begin(),bytes.end()),c);
    std::vector<float> result(count);
    std::size_t position=0;
    while(position<result.size())position+=source.read(std::span(result).subspan(position,std::min<std::size_t>(4096,result.size()-position)),stop);
    return result;
}
DecodeResult demodulate(std::span<const float> samples, const Config& c,
                        std::span<const std::uint8_t>, std::stop_token stop) {
    check_cancelled(stop); validate(c); finite_samples(samples,c.memory_limit,stop);
    throw Error("binary patterns require PatternReceiver blind sample acquisition");
}
std::vector<float> simulate(std::span<const float> samples, const Config& c, const ChannelConfig& channel) {
    validate(c); finite_samples(samples, c.memory_limit);
    validate_channel(c,channel);
    const auto rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
    std::mt19937_64 startup_random(channel.seed^0x8ebc6af09c88c6e3ULL);
    const auto uniform=[&]{return (static_cast<double>(startup_random()>>11)+.5)/9007199254740992.;};
    const auto initial_phase=static_cast<long double>(tau)*(uniform()-.5L);
    const auto startup=channel.delay_samples+std::floor((.05L+.25L*uniform())*c.sample_rate)+.05L+.9L*uniform();
    const auto receiver_count=std::ceil(startup+static_cast<long double>(samples.size())/rate);
    check(receiver_count<=c.memory_limit/sizeof(float),"simulation duration exceeds memory limit");
    const auto count=static_cast<std::size_t>(receiver_count);
    budget(c.memory_limit, {{samples.size(),sizeof(float)}, {count,sizeof(float)}});
    std::vector<float> out(count);
    double power = 0;
    for (auto v : samples) power += static_cast<double>(v) * v;
    power = samples.empty() ? 0 : power / static_cast<double>(samples.size());
    if (!samples.empty()) {
        const auto n = fft_size(samples.size(), c.memory_limit, sizeof(Complex));
        budget(c.memory_limit, {{samples.size(),sizeof(float)}, {out.size(),sizeof(float)}, {n,sizeof(Complex)}});
        std::vector<Complex> analytic(n);
        std::copy(samples.begin(), samples.end(), analytic.begin());
        fft(analytic, false);
        for (std::size_t k = 1; k < n/2; ++k) analytic[k] *= 2;
        for (std::size_t k = n/2+1; k < n; ++k) analytic[k] = 0;
        fft(analytic, true);
        std::mt19937_64 phase_random(channel.seed^0xa0761d6478bd642fULL);
        const auto diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(c.sample_rate);
        std::normal_distribution<double> phase_step(0,1);double phase=0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto position=(static_cast<long double>(i)-startup)*rate;
            if(position<0) {if(diffusion)phase+=phase_step(phase_random)*diffusion;continue;}
            const auto center=static_cast<std::int64_t>(std::floor(position));
            Complex value{};double weight=0;
            if(std::abs(position-center)<1e-10)value=analytic[static_cast<std::size_t>(center)];
            else for(auto tap=center-7;tap<=center+8;++tap) {
                const auto distance=static_cast<double>(position-tap);
                const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
                const auto angle=std::numbers::pi*distance;
                const auto coefficient=(std::abs(angle)<1e-8?1:std::sin(angle)/angle)*window;
                weight+=coefficient;
                if(tap>=0 && tap<static_cast<std::int64_t>(samples.size()))value+=analytic[static_cast<std::size_t>(tap)]*coefficient;
            }
            if(weight)value/=weight;
            const auto angle=std::remainder(initial_phase+static_cast<long double>(tau)*
                (channel.frequency_offset_hz*static_cast<long double>(i)+c.carrier_hz*rate*startup)/c.sample_rate+phase,static_cast<long double>(tau));
            out[i]=static_cast<float>((value*std::polar(1.,static_cast<double>(angle))).real());
            if(diffusion)phase+=phase_step(phase_random)*diffusion;
        }
    }
    const double sigma = std::sqrt(power * std::pow(10.0, -channel.snr_db / 10));
    std::mt19937_64 generator(channel.seed);
    std::normal_distribution<double> normal(0, sigma > 0 ? sigma : 1);
    if (sigma > 0) for (auto& v : out) v = static_cast<float>(v + normal(generator));
    finite_samples(out, c.memory_limit);
    return out;
}
void write_wav(std::ostream& out, std::span<const float> samples, std::uint32_t rate) {
    check(rate >= 64 && rate <= 120000000, "invalid WAV sample rate");
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
            check(result.sample_rate >= 64 && result.sample_rate <= 120000000 && get32(b.data()+8)==result.sample_rate*2,
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
    PatternTransmitter source(Bytes(bits.begin(),bits.end()),c,c.stream_epoch);
    check(source.total_samples()<=c.memory_limit/sizeof(float),"pattern waveform exceeds memory limit");
    const auto count=static_cast<std::size_t>(source.total_samples());
    budget(c.memory_limit,{{count,sizeof(float)},{source.working_bytes(),1}});
    std::vector<float> output(count);
    for(std::size_t position=0;position<output.size();)
        position+=source.read(std::span(output).subspan(position,std::min<std::size_t>(4096,output.size()-position)));
    return output;
}
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> bits, const Config& c) {
    validate(c); finite_samples(samples,c.memory_limit);
    check(!bits.empty(),"known status bits are required");
    const auto duration=symbol_sample_count(c);
    check(duration<=std::numeric_limits<std::size_t>::max(),"status symbol duration exceeds platform sample counter");
    const auto payload_count = product(bits.size(),static_cast<std::size_t>(duration),c.memory_limit/sizeof(float));
    const auto training=training_sample_count(c);
    check(training<=c.memory_limit/sizeof(float)-payload_count,"status waveform exceeds memory limit");
    const auto padding=pattern_pulse_padding_samples(c);
    const auto content_count=payload_count+static_cast<std::size_t>(training);
    check(padding<=(c.memory_limit/sizeof(float)-content_count)/2,"status pulse tails exceed memory limit");
    const auto expected_count=content_count+2*static_cast<std::size_t>(padding);
    budget(c.memory_limit,{{samples.size(),sizeof(float)},{expected_count,sizeof(float)},{bits.size(),1},{16384,1}});
    const auto reference = modulate_status(bits,c);
    check(samples.size() == reference.size(),"status detector requires exactly the known symbol duration");
    double dot=0, signal=0, expected=0;
    for (std::size_t i=0; i<samples.size(); ++i) { dot+=samples[i]*reference[i]; signal+=samples[i]*samples[i]; expected+=reference[i]*reference[i]; }
    return signal > 0 && expected > 0 ? dot/std::sqrt(signal*expected) : 0;
}
}
