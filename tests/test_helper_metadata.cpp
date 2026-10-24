#include "datapump/helper_metadata.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace h=datapump::helper;
namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class F>void rejects(F operation,const char* message) {
    try {operation();}catch(const datapump::Error&) {return;}
    throw std::runtime_error(message);
}
auto view(const h::MetadataPacket& packet) {
    return std::span<const std::uint32_t>(packet.symbols).first(packet.count);
}
void flip(h::MetadataPacket& packet,std::size_t bit) {
    packet.symbols[bit/21]^=1U<<(20-bit%21);
}
unsigned width(std::uint64_t ticks) {
    return std::max(1U,static_cast<unsigned>(std::bit_width(ticks)));
}
// Independent bit-string framing fixture, including the CRC7/MMC bit recurrence.
h::MetadataPacket fixture(unsigned type,unsigned length,std::uint64_t payload,unsigned bits) {
    std::string frame;
    const auto append=[&](std::uint64_t value,unsigned n) {
        for(unsigned i=n;i--;)frame+=((value>>i)&1U)?'1':'0';
    };
    append(type,2);append(length,6);append(payload,bits);
    const auto count=(frame.size()+20)/21;
    if(count*21-frame.size()>=7) {
        unsigned crc=0;
        for(const auto c:frame) {
            const bool top=(crc&0x40)!=0;
            crc=(crc<<1)&0x7f;
            if(top!=(c=='1'))crc^=0x09;
        }
        append(crc,7);
    }
    frame.resize(count*21,'0');
    h::MetadataPacket packet;packet.count=count;
    for(std::size_t i=0;i<frame.size();++i)packet.symbols[i/21]=(packet.symbols[i/21]<<1)|(frame[i]=='1');
    return packet;
}
void invalid_lengths_and_symbols(const h::MetadataPacket& packet,std::span<const datapump::Crypto> keys={}) {
    for(std::size_t count=0;count<packet.count;++count)
        check(!h::decode_metadata_packet(view(packet).first(count),keys),"truncated metadata accepted");
    auto longer=packet;
    ++longer.count;
    check(!h::decode_metadata_packet(view(longer),keys),"noncanonical extra metadata symbol accepted");
    for(std::size_t i=0;i<packet.count;++i) {
        auto altered=packet;
        altered.symbols[i]|=1U<<21;
        check(!h::decode_metadata_packet(view(altered),keys),"out-of-range APSK symbol accepted");
    }
    auto reserved=packet;
    reserved.symbols[0]|=3U<<19;
    check(!h::decode_metadata_packet(view(reserved),keys),"reserved metadata type accepted");
}
void checksum_and_padding(const h::MetadataPacket& packet,std::size_t content_bits,
                          std::size_t header_bits,std::span<const datapump::Crypto> keys={}) {
    const bool checksum=packet.count*21-content_bits>=7;
    // A checksum is error detection, not authentication. Only packets with its
    // spare-space checksum can promise detection of each content-bit error.
    for(std::size_t bit=checksum?header_bits:content_bits;bit<packet.count*21;++bit) {
        auto altered=packet;
        flip(altered,bit);
        check(!h::decode_metadata_packet(view(altered),keys),"corrupt content/checksum/padding accepted");
    }
}
void callsigns() {
    struct Vector {std::string_view text;std::size_t count;std::array<std::uint32_t,7> symbols;};
    // Independent Python bit-string + CRC7 reference, not the codec under test.
    constexpr std::array vectors{
        Vector{"",1,{0}},Vector{"X",1,{0x006c74}},Vector{"XY",1,{0x00ac59}},
        Vector{"N0CALL",3,{0x01a730,0x10e0cc,0x133f00}},
        Vector{"ABCDEFGHIJKLMNOPQRS",7,{0x04e0c2,0x10e245,0x11a3c8,0x12654b,0x1326ce,0x13e851,0x14a99f}}};
    for(const auto& vector:vectors) {
        const auto packet=h::callsign_packet(vector.text);
        check(packet.count==vector.count&&std::equal(vector.symbols.begin(),vector.symbols.end(),packet.symbols.begin()),
              "compact callsign reference vector changed");
        const auto decoded=h::decode_metadata_packet(view(packet));
        check(decoded&&decoded->kind==h::MetadataKind::callsign&&decoded->callsign==vector.text&&
              decoded->time_ticks==0&&!decoded->authenticated,"compact callsign did not decode");
        invalid_lengths_and_symbols(packet);
        checksum_and_padding(packet,7+7*vector.text.size(),7);
    }
    for(std::size_t count=0;count<=19;++count) {
        const auto packet=h::callsign_packet(std::string(count,'X'));
        check(packet.count==(7+7*count+20)/21,"callsign checksum needlessly added a symbol");
        const auto decoded=h::decode_metadata_packet(view(packet));
        check(decoded&&decoded->callsign==std::string(count,'X'),"valid callsign length rejected");
    }
    for(unsigned first=0;first<128;first+=19) {
        std::string callsign;
        for(unsigned c=first;c<std::min(first+19,128U);++c)callsign+=static_cast<char>(c);
        const auto decoded=h::decode_metadata_packet(view(h::callsign_packet(callsign)));
        check(decoded&&decoded->callsign==callsign,"seven-bit callsign changed");
    }
    auto no_checksum=h::callsign_packet("XY");
    flip(no_checksum,20);
    const auto altered=h::decode_metadata_packet(view(no_checksum));
    check(altered&&altered->callsign=="XX"&&!altered->authenticated,
          "checksum-free minimal callsign incorrectly claimed integrity");
    for(unsigned length=20;length<32;++length) {
        auto invalid=h::callsign_packet("ABCDEFGHIJKLMNOPQRS");
        invalid.symbols[0]=(invalid.symbols[0]&~(31U<<14))|(length<<14);
        check(!h::decode_metadata_packet(view(invalid)),"overlong wire callsign accepted");
    }
    rejects([]{h::callsign_packet("ABCDEFGHIJKLMNOPQRST");},"overlong callsign accepted");
    for(unsigned byte=128;byte<256;++byte)
        rejects([&]{h::callsign_packet(std::string(1,static_cast<char>(byte)));},"non-ASCII callsign accepted");
}

void timecodes() {
    std::array<std::uint8_t,32> master{};
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const datapump::Crypto key(master);
    master[0]^=1;
    const datapump::Crypto wrong(master);
    const std::array keys{key};
    const std::array wrong_keys{wrong};
    const std::array wrong_first{wrong,key};
    struct Vector {std::uint64_t ticks;bool encrypted;std::size_t count;std::array<std::uint32_t,3> symbols;};
    // Independently generated integer FF1 reference using cryptography's AES.
    // That reference passes all nine official NIST FF1 AES128/192/256 examples
    // and the published noble-ciphers AES256 binary example in both directions.
    // Binary widths include odd/even 20/21,31/32,47/48, not just round trips.
    constexpr std::array vectors{
        Vector{0x000000000000ULL,false,1,{0x080820}},
        Vector{0x000000000000ULL,true,2,{0x126fb8,0x180a00}},
        Vector{0x000000000001ULL,false,1,{0x081900}},
        Vector{0x000000000001ULL,true,2,{0x127140,0x088f00}},
        Vector{0x000000081234ULL,false,2,{0x0a7024,0x0d1580}},
        Vector{0x000000081234ULL,true,2,{0x127cb4,0x1b6e80}},
        Vector{0x000000101234ULL,false,2,{0x0a9012,0x068980}},
        Vector{0x000000101234ULL,true,2,{0x12952a,0x0a9440}},
        Vector{0x000040001234ULL,false,2,{0x0bd000,0x0091a0}},
        Vector{0x000040001234ULL,true,2,{0x13dfb3,0x163690}},
        Vector{0x000080001234ULL,false,2,{0x0bf000,0x0048d0}},
        Vector{0x000080001234ULL,true,2,{0x13f753,0x00aff8}},
        Vector{0x400000001234ULL,false,3,{0x0dd000,0x000000,0x123452}},
        Vector{0x400000001234ULL,true,3,{0x15d064,0x062680,0x039c28}},
        Vector{0x800000001234ULL,false,3,{0x0df000,0x000000,0x091a59}},
        Vector{0x800000001234ULL,true,3,{0x15e249,0x0959c7,0x06cf1e}},
        Vector{0xffffffffffffULL,false,3,{0x0dffff,0x1fffff,0x1fffd1}},
        Vector{0xffffffffffffULL,true,3,{0x15e2bb,0x11a672,0x153f5f}}};
    for(const auto& vector:vectors) {
        const auto packet=h::timecode_packet(vector.ticks,vector.encrypted?&key:nullptr);
        check(packet.count==vector.count&&std::equal(vector.symbols.begin(),vector.symbols.end(),packet.symbols.begin()),
              "compact clear/FF1 time reference vector changed");
        const auto decoded=h::decode_metadata_packet(view(packet),keys);
        check(decoded&&decoded->kind==(vector.encrypted?h::MetadataKind::encrypted_time:h::MetadataKind::clear_time)&&
              decoded->time_ticks==vector.ticks&&decoded->callsign.empty()&&!decoded->authenticated,
              "compact time reference did not decode without authentication");
        const auto bits=std::max(vector.encrypted?20U:1U,width(vector.ticks));
        check(packet.count==(8+bits+20)/21&&packet.count<=3,"time checksum added a symbol or exceeded 24ms");
        check(h::timecode_packet(vector.ticks,vector.encrypted?&key:nullptr).symbols==packet.symbols,
              "compact time encryption is not deterministic");
        invalid_lengths_and_symbols(packet,keys);
        checksum_and_padding(packet,8+bits,8,keys);
        if(vector.encrypted)check(!h::decode_metadata_packet(view(packet)),"encrypted time decoded without a selected key");
    }
    for(unsigned bits=1;bits<=48;++bits) {
        const auto ticks=(std::uint64_t{1}<<bits)-1;
        for(const auto encrypted:{false,true}) {
            const auto packet=h::timecode_packet(ticks,encrypted?&key:nullptr);
            const auto decoded=h::decode_metadata_packet(view(packet),keys);
            check(decoded&&decoded->time_ticks==ticks&&!decoded->authenticated,"time bit-width boundary failed");
        }
    }
    const auto encrypted_zero=h::timecode_packet(0,&key);
    const auto wrong_decoded=h::decode_metadata_packet(view(encrypted_zero),wrong_keys);
    const auto selected=h::decode_metadata_packet(view(encrypted_zero),wrong_first);
    check(wrong_decoded&&selected&&wrong_decoded->time_ticks==selected->time_ticks&&
          selected->time_ticks!=0&&!selected->authenticated,
          "unauthenticated time guessed a key-ring identity or claimed wrong-key rejection");
    auto forged=fixture(2,19,0x7dc61,20); // Valid public CRC, modified FF1 ciphertext.
    const auto changed=h::decode_metadata_packet(view(forged),keys);
    check(changed&&changed->time_ticks!=0&&!changed->authenticated,"public checksum incorrectly treated as authentication");
    for(const auto malformed:std::array{
        fixture(1,48,0,49),fixture(1,63,0,64),fixture(1,1,0,2),fixture(2,18,0,19)})
        check(!h::decode_metadata_packet(view(malformed),keys),"invalid/noncanonical time width accepted");
    for(const auto ticks:{std::uint64_t{1}<<48,std::numeric_limits<std::uint64_t>::max()}) {
        rejects([&]{h::timecode_packet(ticks);},"out-of-range clear time accepted");
        rejects([&]{h::timecode_packet(ticks,&key);},"out-of-range encrypted time accepted");
    }
    struct RestoreProperties {
        ~RestoreProperties() {EVP_set_default_properties(nullptr,"");ERR_clear_error();}
    } restore;
    check(EVP_set_default_properties(nullptr,"provider=helper_metadata_test_missing")==1,
          "could not set unavailable-cipher fixture");
    rejects([&]{h::timecode_packet(0,&key);},"missing AES provider silently transmitted clear time");
    check(!h::decode_metadata_packet(view(encrypted_zero),keys),"missing AES provider decoded encrypted time");
    check(h::decode_metadata_packet(view(h::timecode_packet(0))).has_value(),"clear time requires an AES provider");
}
} // namespace

int main() {
    try {
        callsigns();timecodes();
        std::cout<<"compact helper metadata codec passed\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
