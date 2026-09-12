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
}
int main(){try{fixed_byte_codes();roundtrips_and_prefixes();strict_validation();std::cout<<"short compression tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
