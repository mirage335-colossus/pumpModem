#include "datapump/boundary_sync.hpp"
#include "datapump/transfer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace datapump;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes hex(std::string_view text) {
    auto nibble=[](char c)->std::uint8_t {
        if(c>='0' && c<='9') return static_cast<std::uint8_t>(c-'0');
        if(c>='a' && c<='f') return static_cast<std::uint8_t>(c-'a'+10);
        throw std::runtime_error("invalid test hexadecimal byte");
    };
    check(text.size()%2==0,"invalid test hexadecimal width");
    Bytes result;result.reserve(text.size()/2);
    for(std::size_t i=0;i<text.size();i+=2)
        result.push_back(static_cast<std::uint8_t>((nibble(text[i])<<4)|nibble(text[i+1])));
    return result;
}

Bytes bytes_from_bits(std::span<const std::uint8_t> bits) {
    check(bits.size()%8==0,"fixed interval is not byte aligned");
    Bytes bytes(bits.size()/8);
    for(std::size_t i=0;i<bits.size();++i) {
        check(bits[i]<=1,"nonbinary interval bit");
        bytes[i/8]|=static_cast<std::uint8_t>(bits[i]<<(7-i%8));
    }
    return bytes;
}

Bytes bits_from_bytes(std::span<const std::uint8_t> bytes) {
    Bytes bits;bits.reserve(bytes.size()*8);
    for(auto byte:bytes)for(unsigned i=0;i<8;++i)bits.push_back(static_cast<std::uint8_t>((byte>>(7-i))&1U));
    return bits;
}

transfer::Options options(std::span<const std::uint8_t,64> serpent,
                          std::span<const std::uint8_t,64> aes) {
    transfer::Options value;
    value.modem.sample_rate=8000;value.modem.bandwidth_hz=1000;
    value.modem.carrier_hz=1500;value.modem.spreading_factor=16;
    value.modem.pulse_shaping=false;value.modem.integration_seconds=1.0;
    value.timestamp=1800000000;value.search_seconds=0;value.content_limit=1024*1024;
    value.fec=FecMode::off;value.compression=false;
    value.key=Crypto::from_independent_roots(serpent,aes);
    return value;
}

transfer::Received receive(std::span<const std::uint8_t> wire,const transfer::Options& value) {
    transfer::StreamReceiver receiver(value,value.timestamp);
    modem::PatternBurst pending;pending.bits.assign(wire.begin(),wire.end());
    pending.first_stream_symbol=0;pending.first_sample=100;pending.end_sample=100+wire.size()*64;
    pending.stream_first_sample=100;pending.stream_first_symbol=0;pending.score=100;
    const auto observed=receiver.push(std::move(pending));
    check(!observed.stream_complete && !observed.content_validated && observed.content.message.data.empty(),
          "interval authentication must not release source before physical completion");
    modem::PatternBurst completed;completed.first_stream_symbol=wire.size();
    completed.first_sample=100+wire.size()*64;completed.end_sample=completed.first_sample;
    completed.stream_first_sample=100;completed.stream_first_symbol=0;completed.score=100;completed.complete=true;
    return receiver.push(std::move(completed));
}

void independent_interval_vector() {
    std::array<std::uint8_t,64> serpent{},aes{};
    for(std::size_t i=0;i<64;++i){serpent[i]=static_cast<std::uint8_t>(i);aes[i]=static_cast<std::uint8_t>(64+i);}
    auto value=options(serpent,aes);
    Message message;constexpr std::string_view input="Independent v4 MAC test";
    message.data.assign(input.begin(),input.end());

    // Independent Python hashlib/HMAC/HKDF-SHA512 oracle: roots are 00..3f
    // and 40..7f; 96 source bytes use nine-bit cells, FEC/compression are
    // off, and one-second symbols put the first coded bit at epoch
    // 1800000192, ordinal zero. The two full HMAC-SHA256 results are XORed.
    auto expected_data=hex("a4dbac965b8596dd64b2dbae920bb4d2414da0d0e4174b2dcee8");
    expected_data.resize(96);
    const auto expected_tag=hex("853e39cffd21b81da84a94cf086e21185d6095ff28076341a4d5ce25cbb2ed1b");
    const auto s_only=hex("d873359cfcd03643a62322ccd506ca192dfedf7521f80575c9086e0d93aff15a");
    const auto a_only=hex("5d4d0c5301f18e5e0e69b603dd68eb01709e4a8a09ff66346ddda028581d1c41");
    const auto bits=transfer::message_bits(message,value);
    check(bits.size()==1024,"long text did not use one fixed 128-byte interval");
    const auto coded=bytes_from_bits(bits);
    check(expected_data.size()==96 && std::equal(expected_data.begin(),expected_data.end(),coded.begin()),
          "independent nine-bit source-area vector changed");
    check(std::equal(expected_tag.begin(),expected_tag.end(),coded.begin()+96),
          "v4 interval tag differs from independent dual-lane HMAC vector");
    check(value.key->verify_confidentiality(Bytes{},value.key->confidentiality_mac(Bytes{})),
          "dual-lane verifier rejected its own full tag");

    auto changed_s=serpent;changed_s[17]^=1;
    auto changed_a=aes;changed_a[17]^=1;
    const auto s_coded=bytes_from_bits(transfer::message_bits(message,options(changed_s,aes)));
    const auto a_coded=bytes_from_bits(transfer::message_bits(message,options(serpent,changed_a)));
    check(std::equal(coded.begin(),coded.begin()+96,s_coded.begin()) &&
          std::equal(coded.begin(),coded.begin()+96,a_coded.begin()),"key change altered source bytes");
    const auto s_tag=hex("37db3b29e86d785a11c4f8b5159ff615f6999fafb358f3c2d1b44bf4894de953");
    const auto a_tag=hex("f60f7b0eabc0e74580cbbf54eab3f8d6080669dada1e0f11a3e7a0eff9851693");
    check(std::equal(s_tag.begin(),s_tag.end(),s_coded.begin()+96) &&
          std::equal(a_tag.begin(),a_tag.end(),a_coded.begin()+96),
          "each independent confidentiality root must affect interval authentication");

    const auto legitimate=transfer::message_wire_bits(message,value);
    check(legitimate.size()==192+1024,"v4 authentication changed marker or interval geometry");
    const auto accepted=receive(legitimate,value);
    check(accepted.stream_complete && accepted.content_validated && accepted.content.authenticated &&
          accepted.content.message.data==message.data,"genuine v4 interval did not authenticate after end");
    for(const auto& forged_tag:{s_only,a_only}) {
        auto forged=coded;std::copy(forged_tag.begin(),forged_tag.end(),forged.begin()+96);
        auto wire=boundary_sync::insert(bits_from_bytes(forged));
        auto mask_options=value;mask_options.content_limit=wire.size();transfer::xor_binary_bits(wire,mask_options);
        const auto rejected=receive(wire,value);
        check(rejected.stream_complete && !rejected.content_validated && !rejected.content.authenticated,
              "a single-lane forged interval must be rejected after physical completion");
    }
    auto tampered=legitimate;tampered[192+96*8+7]^=1;
    const auto rejected=receive(tampered,value);
    check(rejected.stream_complete && !rejected.content_validated && !rejected.content.authenticated,
          "tampered fixed interval tag was accepted");
}
}

int main() {
    try {independent_interval_vector();std::cout<<"v4 dual-lane interval authentication passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
