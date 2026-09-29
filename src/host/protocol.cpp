#include "datapump/host/protocol.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace datapump::host::protocol {
namespace {
constexpr std::byte magic[]{std::byte{'D'},std::byte{'P'},std::byte{'W'},std::byte{'1'}};
}
std::span<const std::byte> Reader::take(std::size_t n) {
    if(n>bytes_.size())throw Error("truncated local host frame");
    const auto result=bytes_.first(n);bytes_=bytes_.subspan(n);return result;
}
std::uint32_t Reader::u32(){const auto b=take(4);std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(b[i])<<(8*i);return v;}
std::uint64_t Reader::u64(){const auto lo=u32(),hi=u32();return lo|(std::uint64_t{hi}<<32);}
float Reader::f32(){return std::bit_cast<float>(u32());}
double Reader::f64(){return std::bit_cast<double>(u64());}
std::string Reader::text(std::size_t limit){const auto n=u32();if(n>limit)throw Error("host text exceeds its bound");const auto b=take(n);return {reinterpret_cast<const char*>(b.data()),b.size()};}
void Reader::end() const {if(!bytes_.empty())throw Error("unexpected local host frame fields");}
void Writer::u32(std::uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::byte>((v>>(8*i))&255));}
void Writer::u64(std::uint64_t v){u32(static_cast<std::uint32_t>(v));u32(static_cast<std::uint32_t>(v>>32));}
void Writer::f32(float v){u32(std::bit_cast<std::uint32_t>(v));}
void Writer::f64(double v){u64(std::bit_cast<std::uint64_t>(v));}
void Writer::text(const std::string& v){if(v.size()>max_frame-4)throw Error("host text exceeds frame bound");u32(static_cast<std::uint32_t>(v.size()));const auto b=std::as_bytes(std::span(v));bytes.insert(bytes.end(),b.begin(),b.end());}
std::vector<std::byte> encode(const Frame& f) {
    if(f.payload.size()>max_frame)throw Error("host frame exceeds its bound");
    Writer out;out.bytes.insert(out.bytes.end(),std::begin(magic),std::end(magic));out.u32(static_cast<std::uint32_t>(f.type));out.u32(static_cast<std::uint32_t>(f.payload.size()));out.bytes.insert(out.bytes.end(),f.payload.begin(),f.payload.end());return std::move(out.bytes);
}
std::vector<Frame> Decoder::feed(std::span<const std::byte> bytes) {
    // Callers normally read <=64 KiB. A huge transport batch must be fragmented
    // by the caller, never admitted as an unbounded temporary allocation.
    if(bytes.size()>max_frame+12 || buffer_.size()+bytes.size()>2*(max_frame+12))throw Error("host input batch exceeds its bound");
    buffer_.insert(buffer_.end(),bytes.begin(),bytes.end());std::vector<Frame> out;std::size_t at=0;
    while(buffer_.size()-at>=12) {
        const auto begin=buffer_.begin()+static_cast<std::ptrdiff_t>(at);
        if(!std::equal(std::begin(magic),std::end(magic),begin))throw Error("invalid local host frame magic/version");
        Reader header(std::span<const std::byte>(buffer_).subspan(at+4,8));const auto type=header.u32(),length=header.u32();
        if(length>max_frame)throw Error("host frame exceeds its bound");
        if(buffer_.size()-at-12<length)break;
        out.push_back({static_cast<Type>(type),{begin+12,begin+12+length}});at+=12+length;
    }
    buffer_.erase(buffer_.begin(),buffer_.begin()+static_cast<std::ptrdiff_t>(at));return out;
}
void Decoder::finish() const {if(!buffer_.empty())throw Error("truncated local host frame at EOF");}
}
