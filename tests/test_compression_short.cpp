#include "datapump/compression.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
template<class Function>void rejects(Function operation,const char* detail) {
    try{operation();}catch(const Error&){return;}throw std::runtime_error(detail);
}
void fixed_byte_codes() {
    check(compression::encode_short(Bytes(8,' '))==Bytes({0,0,0}),"space does not use the fixed three-bit000 code");
    check(compression::encode_short(Bytes(8,'e'))==Bytes({0x24,0x92,0x49}),"e does not use the fixed three-bit001 code");
    check(compression::encode_short(Bytes(2,'i'))==Bytes({0xaa}),"i does not use the fixed four-bit1010 code");
    check(compression::encode_short(Bytes(4,'s'))==Bytes({0xc3,0x0c,0x30}),"s does not use the fixed six-bit110000 code");
    check(compression::encode_short(Bytes{0})==Bytes({0xf8,0}),"uncommon byte does not use fixed escape plus literal");
    check(compression::encode_short(Bytes{'t','h','e',' '})==Bytes({0x58,0x90}),"short codec introduced a phrase dictionary");
    check(compression::encode_short({}).empty() && compression::decode_short({},0).empty(),"empty short stream has framing overhead");
}
void roundtrips_and_prefixes() {
    for(unsigned byte=0;byte<256;++byte) {
        const Bytes input{static_cast<std::uint8_t>(byte)};const auto encoded=compression::encode_short(input);
        check(compression::decode_short(encoded,1)==input,"short code does not represent every byte");
    }
    std::mt19937 random(419);
    for(std::size_t size=1;size<256;++size) {
        Bytes input(size);for(auto& byte:input)byte=static_cast<std::uint8_t>(random());
        const auto encoded=compression::encode_short(input);
        check(compression::decode_short(encoded,input.size())==input,"short code roundtrip failed");
        for(std::size_t prefix=0;prefix<=encoded.size();++prefix) {
            const auto preview=compression::preview_short(std::span(encoded).first(prefix),input.size(),19);
            check(preview.size()<=19 && std::equal(preview.begin(),preview.end(),input.begin()),"partial preview invents bytes or exceeds its bound");
        }
        check(compression::preview_short(encoded,input.size(),input.size())==input,"complete preview loses decoded bytes");
    }
}
void strict_validation() {
    rejects([]{compression::decode_short(Bytes{0xf8},1);},"truncated literal accepted");
    rejects([]{compression::decode_short(Bytes{0x01},1);},"nonzero final padding accepted");
    rejects([]{compression::decode_short(Bytes{0,0},1);},"extra whole byte accepted");
    rejects([]{compression::decode_short(Bytes{0},0);},"zero-length message accepted encoded data");
    rejects([]{compression::decode_short(Bytes{0xfb,0x28},1);},"noncanonical escaped e accepted");
    rejects([]{compression::preview_short(Bytes{0xfb,0x28},1,1);},"preview accepted a noncanonical escaped byte");
    check(compression::preview_short(Bytes{0xf8},1,1).empty(),"preview did not stop at incomplete literal");
    check(compression::preview_short(Bytes{0},100,0).empty(),"zero preview capacity allocates decoded output");
    rejects([]{compression::decode_short(Bytes{0},8,7);},"decoder ignored declared output bound");
    rejects([]{compression::encode_short(Bytes{0},1);},"encoder ignored its output bound");
    rejects([]{compression::decode_short({},std::numeric_limits<std::size_t>::max());},"decoder allocates an impossible declared length");
}
void exact_bit_codes() {
    constexpr std::array<std::uint8_t,5> three_bit{' ','e','t','a','o'};
    for(unsigned code=0;code<three_bit.size();++code) {
        const Bytes input{three_bit[code]};
        const Bytes expected{static_cast<std::uint8_t>((code>>2)&1U),
                             static_cast<std::uint8_t>((code>>1)&1U),
                             static_cast<std::uint8_t>(code&1U)};
        check(compression::encode_short_bits(input,3)==expected,"three-bit character gained framing or padding");
        check(compression::decode_short_bits(expected,1)==input,"three-bit character needs an original length");
    }
    check(compression::encode_short_bits({},0).empty() && compression::decode_short_bits({},0).empty(),
          "empty exact-bit stream gained overhead");
    const Bytes leading_zeros{' ',' ','e',' '};
    check(compression::decode_short_bits(compression::encode_short_bits(leading_zeros))==leading_zeros,
          "exact-bit code loses leading or trailing zero tokens");
    for(unsigned byte=0;byte<256;++byte) {
        const Bytes input{static_cast<std::uint8_t>(byte)};
        const auto bits=compression::encode_short_bits(input);
        check(compression::decode_short_bits(bits,1)==input,"exact-bit code does not represent every byte");
        const auto packed=compression::encode_short(input);
        for(std::size_t bit=0;bit<bits.size();++bit)
            check(bits[bit]==((packed[bit/8]>>(7-bit%8))&1U),"exact-bit and packed codebooks differ");
        for(std::size_t size=1;size<bits.size();++size)
            rejects([&]{compression::decode_short_bits(std::span(bits).first(size));},
                    "incomplete exact-bit token accepted");
    }
    std::mt19937 random(733);
    for(std::size_t size=1;size<256;++size) {
        Bytes input(size);for(auto& byte:input)byte=static_cast<std::uint8_t>(random());
        const auto bits=compression::encode_short_bits(input);
        check(compression::decode_short_bits(bits,size)==input,"exact-bit stream roundtrip failed");
    }
}
void exact_bit_validation() {
    rejects([]{compression::encode_short_bits(Bytes{'e'},2);},"exact-bit encoder ignored bit-element bound");
    rejects([]{compression::encode_short_bits(Bytes{0},12);},"exact-bit encoder ignored literal extent");
    check(compression::encode_short_bits(Bytes{0},13).size()==13,"literal extent includes padding");
    rejects([]{compression::decode_short_bits(Bytes{0,0,2});},"non-bit input accepted");
    rejects([]{compression::decode_short_bits(Bytes{0,0,0,255});},"non-bit tail accepted");
    rejects([]{compression::decode_short_bits(Bytes{0,0,1,0});},"partial token after valid code accepted");
    rejects([]{compression::decode_short_bits(Bytes{1,1,1,1,1,0,1,1,0,0,1,0,1});},
            "exact-bit decoder accepted noncanonical escaped e");
    rejects([]{compression::decode_short_bits(Bytes{0,0,0},0);},"zero decoder output bound ignored");
    rejects([]{compression::decode_short_bits(Bytes(9,0),2);},"decoder output bound ignored");
    check(compression::decode_short_bits(Bytes(9,0),3)==Bytes(3,' '),"exact decoder output bound loses a token");
}
}
int main(){try{fixed_byte_codes();roundtrips_and_prefixes();strict_validation();exact_bit_codes();exact_bit_validation();std::cout<<"short compression tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
