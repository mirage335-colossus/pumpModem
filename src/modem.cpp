// Shared audio container utilities and GUI setting validation. Robust DSP lives
// in the canonical triad backend; no single-carrier transmitter/receiver remains.
#include "datapump/modem.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <istream>
#include <ostream>
#include <limits>
namespace datapump::modem {
namespace {
void check(bool ok,const char* message) {if(!ok)throw Error(message);}
std::size_t product(std::size_t count,std::size_t width,std::size_t limit) {
    check(!width||count<=limit/width,"audio memory limit exceeded");return count*width;
}
void finite_samples(std::span<const float> samples,std::size_t limit) {
    product(samples.size(),sizeof(float),limit);
    for(const auto value:samples)check(std::isfinite(value),"non-finite audio sample");
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
unsigned parse_symbol_burst_wait(std::string_view value) {
    if(value=="off")return 0;
    if(value=="1")return 1;
    if(value=="2")return 2;
    if(value=="3")return 3;
    if(value=="5")return 5;
    if(value=="10")return 10;
    throw Error("Symbol_Burst accepts off, 1, 2, 3, 5 or 10 symbols");
}
unsigned parse_symbol_burst_random(std::string_view value) {
    if(value=="0")return 0;
    if(value=="1")return 1;
    if(value=="2")return 2;
    if(value=="3")return 3;
    throw Error("Symbol_Burst random extra accepts 0, 1, 2 or 3 symbols");
}
void validate(const Config& c) {
    check(c.sample_rate>=64&&c.sample_rate<=120000000,"invalid sample rate");
    check(std::isfinite(c.bandwidth_hz)&&c.bandwidth_hz>=minimum_bandwidth_hz&&c.bandwidth_hz<=maximum_bandwidth_hz,"invalid bandwidth");
    check(std::isfinite(c.carrier_hz)&&c.carrier_hz>=0,"invalid carrier");
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
}
