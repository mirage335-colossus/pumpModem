#include "datapump/compression.hpp"
#include "datapump/speculation.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace datapump::compression {
namespace {
struct Code { unsigned value=0,length=0; };
constexpr std::string_view three_bit=" etao";
constexpr std::string_view four_bit="in";
constexpr std::string_view six_bit="shrdlucmfwypbg";
static_assert(six_bit.size()==14);
// Full-domain decoder tables keep transiently misselected prefix branches
// within an allocated table too. Canonical endpoints still use the checks below.
constexpr std::array<std::uint8_t,8> three_bit_decode{' ','e','t','a','o',0,0,0};
constexpr std::array<std::uint8_t,16> six_bit_decode{'s','h','r','d','l','u','c','m','f','w','y','p','b','g',0,0};
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
    std::size_t position_=0;
    unsigned read(unsigned count) {
        if(input_.empty() || count>remaining())throw Error("truncated short prefix bit token");
        datapump_speculation_barrier();
        // The index helper preserves an address dependency through the load;
        // the bit mask also bounds dictionary selectors independently of the
        // earlier whole-input validation.
        unsigned value=0;
        for(unsigned bit=0;bit<count;++bit,++position_)
            value=(value<<1)|(input_[datapump_index_nospec(position_,input_.size())]&1U);
        return value;
    }
public:
    explicit Reader(std::span<const std::uint8_t> input):input_(input) {
        if(std::any_of(input.begin(),input.end(),[](auto bit){return bit>1;}))
            throw Error("short prefix input elements must be zero or one");
        datapump_speculation_barrier();
    }
    std::size_t remaining()const{return input_.size()-position_;}
    std::optional<std::uint8_t> next() {
        const auto start=position_;
        if(remaining()<3)return {};
        const auto first=read(3);
        if(first<5)return three_bit_decode[first&7U];
        if(first==5) {
            if(!remaining()){position_=start;return {};}
            return static_cast<std::uint8_t>(four_bit[read(1)&1U]);
        }
        if(remaining()<2){position_=start;return {};}
        const auto prefix=(first<<2)|read(2);
        if(prefix!=31) {
            if(!remaining()){position_=start;return {};}
            return six_bit_decode[((prefix<<1)|read(1))&15U];
        }
        if(remaining()<8){position_=start;return {};}
        const auto literal=read(8);
        if(codes[literal].length!=13)throw Error("noncanonical escaped short prefix byte");
        return static_cast<std::uint8_t>(literal);
    }
};
}

Bytes encode_short_bits(std::span<const std::uint8_t> input,std::size_t output_limit) {
    output_limit=std::min(output_limit,Bytes{}.max_size());
    std::size_t bit_count=0;
    for(const auto byte:input) {
        if(codes[byte].length>output_limit-bit_count)
            throw Error("short prefix bit output exceeds limit");
        bit_count+=codes[byte].length;
    }
    Bytes result(bit_count);std::size_t position=0;
    for(const auto byte:input) {
        const auto code=codes[byte];
        for(unsigned bit=0;bit<code.length;++bit)
            result[position++]=static_cast<std::uint8_t>((code.value>>(code.length-bit-1))&1U);
    }
    return result;
}

Bytes decode_short_bits(std::span<const std::uint8_t> bits,std::size_t output_limit) {
    Reader reader(bits);
    output_limit=std::min(output_limit,Bytes{}.max_size());
    Bytes result;result.reserve(std::min(output_limit,bits.size()/3));
    while(reader.remaining()) {
        if(result.size()==output_limit)throw Error("short prefix decoded output exceeds limit");
        const auto byte=reader.next();
        if(!byte)throw Error("truncated short prefix bit token");
        result.push_back(*byte);
    }
    return result;
}
}
