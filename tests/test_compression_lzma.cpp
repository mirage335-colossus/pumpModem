#include "datapump/compression.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value, const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F&& function, const char* message) {
    try { function(); } catch(const Error&) { return; }
    throw std::runtime_error(message);
}
Bytes fixture() {
    const std::string line="portable offline data pump\n";
    Bytes result;
    for(unsigned i=0;i<40;++i)result.insert(result.end(),line.begin(),line.end());
    return result;
}
void independent_vector() {
    // xz 5.8.1 --format=raw --lzma2=preset=9e,dict=4KiB, with fixture() as
    // stdin. This is a raw LZMA2 stream, without .xz framing or filter IDs.
    const Bytes encoded{0xe0,0x04,0x37,0x00,0x27,0x5d,0x00,0x38,0x1b,0xca,
        0xac,0x20,0xbb,0xa6,0x25,0x12,0xa5,0x0c,0x97,0xb3,0xed,0x40,0xd1,
        0xd4,0xaf,0x12,0x0c,0xe2,0xff,0x35,0xc8,0x40,0xd4,0xb9,0xbf,0xfa,
        0xab,0x0a,0xaf,0x2c,0xbb,0x64,0x1a,0x87,0x5b,0x00,0x00};
    const auto original=fixture();
    check(compression::decode_long(encoded,original.size())==original,"external raw LZMA2 vector");
    const auto result=compression::encode_long(original);
    check(result&&*result==encoded,"fixed 9e profile emits only the expected raw stream");
    for(std::size_t size=0;size<encoded.size();++size) {
        rejects([&]{compression::decode_long(std::span(encoded).first(size),original.size());},"truncated long stream accepted");
        const auto prefix=compression::preview_long(std::span(encoded).first(size),original.size(),128);
        check(prefix.size()<=128&&std::equal(prefix.begin(),prefix.end(),original.begin()),"truncated preview is not an exact prefix");
    }
    for(const auto limit:{0U,1U,19U,128U,1080U,2000U}) {
        const auto prefix=compression::preview_long(encoded,original.size(),limit);
        check(prefix.size()==std::min<std::size_t>(limit,original.size())&&
            std::equal(prefix.begin(),prefix.end(),original.begin()),"bounded long prefix");
    }
    auto trailing=encoded;trailing.push_back(0);
    rejects([&]{compression::decode_long(trailing,original.size());},"trailing zero accepted");
    trailing=encoded;trailing.insert(trailing.end(),encoded.begin(),encoded.end());
    rejects([&]{compression::decode_long(trailing,original.size());},"concatenated streams accepted");
    rejects([&]{compression::decode_long(encoded,original.size()-1);},"decoded expansion past declared length accepted");
    rejects([&]{compression::decode_long(encoded,original.size()+1);},"short decoded content accepted");
    rejects([&]{compression::decode_long(encoded,original.size(),original.size()-1);},"output limit ignored");
    rejects([&]{compression::decode_long(encoded,std::numeric_limits<std::size_t>::max());},"hostile original length allocated");
    rejects([&]{compression::preview_long(Bytes{0x03},original.size(),128);},"malformed raw control returned a successful prefix");
    check(compression::decode_long(Bytes{1,0,2,'a','b','c',0},3)==Bytes({'a','b','c'}),"raw uncompressed LZMA2 chunk");
}
void budgets_and_binary() {
    constexpr std::size_t mib=1024U*1024U;
    check(compression::long_encoder_workspace(256)<8*mib,"small input allocated preset9 default64MiB history");
    check(compression::long_decoder_workspace(256)<mib,"small decoder history not capped");
    check(compression::long_encoder_workspace(64*mib)>600*mib&&
        compression::long_encoder_workspace(64*mib)<compression::long_encoder_limit,"preset9e maximum encoder workspace");
    check(compression::long_decoder_workspace(64*mib)>64*mib&&
        compression::long_decoder_workspace(64*mib)<compression::long_decoder_limit,"maximum decoder workspace");
    check(compression::long_encoder_workspace(std::numeric_limits<std::size_t>::max())==
        compression::long_encoder_workspace(64*mib),"history exceeds fixed64MiB maximum");
    const auto original=fixture();
    rejects([&]{compression::encode_long(original,0);},"encoder workspace ignored");
    rejects([&]{compression::encode_long(original,compression::long_encoder_workspace(original.size())-1);},"encoder workspace preflight ignored");
    const auto encoded=compression::encode_long(original,compression::long_encoder_workspace(original.size()));
    check(encoded.has_value(),"reported encoder budget is insufficient");
    rejects([&]{compression::decode_long(*encoded,original.size(),original.size(),0);},"decoder workspace ignored");
    rejects([&]{compression::preview_long(*encoded,original.size(),128,0);},"preview workspace ignored");
    check(compression::decode_long(*encoded,original.size(),original.size(),
        compression::long_decoder_workspace(original.size()))==original,"reported decoder budget is insufficient");
    for(const auto size:{256U,4095U,4096U,4097U,16384U,131073U}) {
        Bytes binary(size);
        for(std::size_t i=0;i<binary.size();++i)binary[i]=static_cast<std::uint8_t>(i%256);
        const auto compressed=compression::encode_long(binary);
        if(compressed)check(compression::decode_long(*compressed,binary.size())==binary,"arbitrary-byte long round trip");
        else check(size==256,"repetitive binary was not compressed");
    }
    std::mt19937 random(0x5a4d4132);
    Bytes noise(8192);for(auto& byte:noise)byte=static_cast<std::uint8_t>(random());
    check(!compression::encode_long(noise),"incompressible payload should use raw fallback");
    check(!compression::encode_long({})&&!compression::encode_long(Bytes{0}),"tiny input expands");
}
}
int main() {
    try { independent_vector();budgets_and_binary();std::cout<<"long compression tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
