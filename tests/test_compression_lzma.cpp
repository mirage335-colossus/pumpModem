#include "datapump/compression.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) { try{action();}catch(const Error&){return;}throw std::runtime_error(message); }
void tests() {
    check(compression::lzma2_encoder_workspace()<=compression::long_encoder_limit,"fixed encoder scratch fits cap");
    check(compression::lzma2_decoder_workspace()<=compression::long_decoder_limit,"fixed decoder scratch fits cap");
    std::mt19937 random(19);
    for(auto count:{0U,1U,44U,256U,65537U})for(bool repetitive:{false,true}) {
        Bytes input(count,'e');if(!repetitive)for(auto& byte:input)byte=static_cast<std::uint8_t>(random());
        const auto encoded=compression::encode_lzma2(input);
        const auto result=compression::decode_lzma2(encoded,input.size());
        check(result.data==input && result.consumed_bytes==encoded.size(),"no original-length field needed for exact output");
        rejects([&]{compression::encode_lzma2(input,encoded.size()-1);},"encoder output quota enforced");
        rejects([&]{compression::decode_lzma2(std::span(encoded).first(encoded.size()-1));},"missing codec end rejected");
        if(count)rejects([&]{compression::decode_lzma2(encoded,count-1);},"decoder expansion quota enforced");
        auto padded=encoded;padded.insert(padded.end(),25,0);
        const auto consumed=compression::decode_lzma2(padded);
        check(consumed.data==input && consumed.consumed_bytes==encoded.size(),"application can check fixed final padding extent");
    }
    rejects([]{compression::decode_lzma2(Bytes{3});},"malformed raw LZMA2 control rejected");
    // Cross the fixed history boundary twice, with nearby and distant repeated
    // sequences. Verify exact bytes across dictionary wraps without assuming
    // a particular match selection or SIMD implementation in the encoder.
    Bytes history(2*compression::lzma2_dictionary_bytes+1027);
    for(std::size_t i=0;i<history.size();++i)
        history[i]=static_cast<std::uint8_t>(((i%524287)*37+(i%257)*13)^(i/4093));
    for(std::size_t i=524287;i<history.size();++i)
        if(i%65537<64000)history[i]=history[i-524287];
    const auto history_encoded=compression::encode_lzma2(history);
    const auto history_decoded=compression::decode_lzma2(history_encoded,history.size());
    check(history_decoded.data==history && history_decoded.consumed_bytes==history_encoded.size(),
        "dictionary wraps preserve exact source bytes");
    // A received property byte cannot select an unsupported literal table.
    // 0xff is outside the LZMA lc/lp/pb domain. The rest of this minimal chunk
    // must never reach probability-table initialization under that geometry.
    rejects([]{compression::decode_lzma2(Bytes{0xe0,0,0,0,0,0xff,0,0});},
        "invalid received LZMA properties rejected");
    rejects([]{compression::decode_lzma2({});},"missing codec stream rejected");
    rejects([]{compression::encode_lzma2(Bytes{1},1024,1);},"actual encoder workspace bounded");
    rejects([]{compression::decode_lzma2(Bytes{0},1024,1);},"actual decoder workspace bounded");
}
}
int main(){try{tests();std::cout<<"fixed LZMA2 tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
