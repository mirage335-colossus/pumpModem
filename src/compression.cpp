#include "datapump/compression.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string_view>

namespace datapump::compression {
namespace {
struct Code { unsigned value=0,length=0; };
constexpr std::string_view three_bit=" etao";
constexpr std::string_view four_bit="in";
constexpr std::string_view six_bit="shrdlucmfwypbg";
static_assert(six_bit.size()==14);
constexpr auto codes=[] {
    std::array<Code,256> result{};
    for(unsigned byte=0;byte<result.size();++byte)result[byte]={(31U<<8)|byte,13};
    for(unsigned i=0;i<three_bit.size();++i)result[static_cast<unsigned char>(three_bit[i])]={i,3};
    for(unsigned i=0;i<four_bit.size();++i)result[static_cast<unsigned char>(four_bit[i])]={10+i,4};
    for(unsigned i=0;i<six_bit.size();++i)result[static_cast<unsigned char>(six_bit[i])]={48+i,6};
    return result;
}();

class Reader {
    std::span<const std::uint8_t> input_;
    std::size_t position_=0,bit_count_=0;
    unsigned read(unsigned count) {
        unsigned value=0;
        for(unsigned bit=0;bit<count;++bit,++position_)
            value=(value<<1)|((input_[position_/8]>>(7-position_%8))&1U);
        return value;
    }
public:
    explicit Reader(std::span<const std::uint8_t> input):input_(input) {
        if(input.size()>std::numeric_limits<std::size_t>::max()/8)throw Error("short prefix input length overflow");
        bit_count_=input.size()*8;
    }
    std::size_t remaining()const{return bit_count_-position_;}
    std::optional<std::uint8_t> next() {
        const auto start=position_;
        if(remaining()<3)return {};
        const auto first=read(3);
        if(first<5)return static_cast<std::uint8_t>(three_bit[first]);
        if(first==5) {
            if(!remaining()){position_=start;return {};}
            return static_cast<std::uint8_t>(four_bit[read(1)]);
        }
        if(remaining()<2){position_=start;return {};}
        const auto prefix=(first<<2)|read(2);
        if(prefix!=31) {
            if(!remaining()){position_=start;return {};}
            return static_cast<std::uint8_t>(six_bit[((prefix<<1)|read(1))-48]);
        }
        if(remaining()<8){position_=start;return {};}
        const auto literal=read(8);
        if(codes[literal].length!=13)throw Error("noncanonical escaped short prefix byte");
        return static_cast<std::uint8_t>(literal);
    }
    void padding() {
        if(remaining()>7)throw Error("extra data after short prefix stream");
        if(remaining() && read(static_cast<unsigned>(remaining()))!=0)
            throw Error("nonzero short prefix padding");
    }
};
}

Bytes encode_short(std::span<const std::uint8_t> input,std::size_t output_limit) {
    std::size_t bits=0;
    for(const auto byte:input) {
        if(codes[byte].length>std::numeric_limits<std::size_t>::max()-bits)
            throw Error("short prefix output length overflow");
        bits+=codes[byte].length;
    }
    const auto bytes=bits/8+(bits%8!=0);
    if(bytes>output_limit)throw Error("short prefix output exceeds limit");
    Bytes result(bytes);
    std::size_t position=0;
    for(const auto byte:input) {
        const auto code=codes[byte];
        for(unsigned bit=0;bit<code.length;++bit,++position)
            result[position/8]|=static_cast<std::uint8_t>(((code.value>>(code.length-bit-1))&1U)<<(7-position%8));
    }
    return result;
}

Bytes decode_short(std::span<const std::uint8_t> encoded,std::size_t original_size,std::size_t output_limit) {
    if(original_size>output_limit)throw Error("short prefix decoded output exceeds limit");
    Reader reader(encoded);
    if(original_size>reader.remaining()/3)throw Error("truncated short prefix stream");
    Bytes result;result.reserve(original_size);
    while(result.size()<original_size) {
        const auto byte=reader.next();
        if(!byte)throw Error("truncated short prefix byte");
        result.push_back(*byte);
    }
    reader.padding();return result;
}

Bytes preview_short(std::span<const std::uint8_t> encoded,std::size_t original_size,std::size_t maximum_output) {
    Reader reader(encoded);const auto limit=std::min(original_size,maximum_output);
    Bytes result;result.reserve(std::min(limit,reader.remaining()/3));
    while(result.size()<limit) {
        const auto byte=reader.next();if(!byte)break;
        result.push_back(*byte);
    }
    if(result.size()==original_size)reader.padding();
    return result;
}
}
