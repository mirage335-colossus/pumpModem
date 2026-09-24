#include "datapump/compression.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
using namespace datapump;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) { try{action();}catch(const Error&){return;}throw std::runtime_error(message); }
Bytes bits(std::string_view text) {
    Bytes result;for(char ch:text)result.push_back(static_cast<std::uint8_t>(ch-'0'));return result;
}
void tests() {
    check(compression::encode_short_bits({},0).empty(),"empty encoding fits zero limit");
    check(compression::decode_short_bits({},0).empty(),"empty decoding fits zero limit");
    // Historical codebook vectors protect the on-air mapping independently
    // of encoder/decoder round trips, particularly the three-symbol message.
    constexpr std::string_view letters=" etaoinshrdlucmfwypbg";
    constexpr std::array<std::string_view,21> codewords={
        "000","001","010","011","100","1010","1011",
        "110000","110001","110010","110011","110100","110101","110110",
        "110111","111000","111001","111010","111011","111100","111101"};
    static_assert(letters.size()==codewords.size());
    for(std::size_t i=0;i<letters.size();++i) {
        const Bytes input{static_cast<std::uint8_t>(letters[i])};
        const auto expected=bits(codewords[i]);
        check(compression::encode_short_bits(input)==expected,"historical short codeword preserved");
        check(compression::decode_short_bits(expected)==input,"historical short codeword decoded");
        Bytes escaped=bits("11111");
        for(unsigned shift=8;shift>0;--shift)
            escaped.push_back(static_cast<std::uint8_t>((input.front()>>(shift-1))&1U));
        rejects([&]{compression::decode_short_bits(escaped);},"noncanonical escaped dictionary byte rejected");
    }
    check(compression::encode_short_bits(Bytes{'e'},3)==bits("001"),"e occupies exactly three bits without padding");
    check(compression::encode_short_bits(Bytes{0})==bits("1111100000000"),"NUL literal escape preserves leading zeros");
    check(compression::encode_short_bits(Bytes{255})==bits("1111111111111"),"high literal escape preserved");
    Bytes alphabet;
    for(unsigned byte=0;byte<256;++byte) {
        const Bytes input{static_cast<std::uint8_t>(byte)};
        alphabet.push_back(input.front());
        const auto encoded=compression::encode_short_bits(input);
        check(compression::decode_short_bits(encoded,1)==input,"all 256 bytes round trip individually");
        check(compression::encode_short_bits(input,encoded.size())==encoded,"exact encoder quota accepted");
        rejects([&]{compression::encode_short_bits(input,encoded.size()-1);},"encoder bit-storage quota enforced");
        rejects([&]{compression::decode_short_bits(encoded,0);},"decoder byte quota enforced");
        for(std::size_t cut=1;cut<encoded.size();++cut) {
            const auto truncated=std::span(encoded).first(cut);
            rejects([&]{compression::decode_short_bits(truncated);},"every incomplete token endpoint rejected");
            auto after_complete=bits("001");after_complete.insert(after_complete.end(),truncated.begin(),truncated.end());
            rejects([&]{compression::decode_short_bits(after_complete);},"truncated token after complete prefix rejected");
        }
    }
    const auto encoded=compression::encode_short_bits(alphabet);
    check(compression::decode_short_bits(encoded,alphabet.size())==alphabet,"all byte tokens concatenate without framing");
    rejects([&]{compression::decode_short_bits(encoded,alphabet.size()-1);},"concatenated output quota enforced");
    check(compression::decode_short_bits(encoded,std::numeric_limits<std::size_t>::max())==alphabet,"maximum limit remains bounded by input");
    check(compression::decode_short_bits(bits("001000"))==Bytes({'e',' '}),"trailing zero token is content, never discarded as padding");
    rejects([]{compression::decode_short_bits(bits("0010"));},"one trailing zero cannot be inferred as endpoint padding");
    for(auto invalid: {std::uint8_t{2},std::uint8_t{255}}) {
        for(std::size_t position=0;position<encoded.size();++position) {
            auto bad=encoded;bad[position]=invalid;
            rejects([&]{compression::decode_short_bits(bad);},"non-bit values rejected at every position");
        }
    }
    // Decoder table padding and local bit masks must never make malformed
    // source elements into accepted symbols, including truncated prefixes.
    for(unsigned value=2;value<256;++value) {
        const auto invalid=static_cast<std::uint8_t>(value);
        rejects([&]{compression::decode_short_bits(Bytes{invalid});},"non-bit single prefix rejected");
        rejects([&]{compression::decode_short_bits(Bytes{1,1,invalid});},"non-bit dictionary selector rejected");
        auto literal=bits("1111100000000");literal.back()=invalid;
        rejects([&]{compression::decode_short_bits(literal);},"non-bit literal must not be masked into acceptance");
    }
}
}
int main(){try{tests();std::cout<<"exact short dictionary tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
