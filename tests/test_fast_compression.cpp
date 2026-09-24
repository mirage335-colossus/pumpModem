#include "datapump/fast/compression.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>

using namespace datapump;
using namespace datapump::fast;
namespace {
void check(bool value,const char* why){if(!value)throw Error(why);}
template<class F>void rejects(F&& f,const char* why){try{f();}catch(const Error&){return;}throw Error(why);}
Bytes collect(SourceReader reader) {
    Bytes result;std::array<std::uint8_t,137> chunk{};
    while(const auto n=reader(chunk))result.insert(result.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(n));
    return result;
}
Bytes unhex(std::string_view text) {
    Bytes result;for(std::size_t i=0;i<text.size();i+=2)
        result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return result;
}
void formats_and_bounds() {
    // Frozen independently with Python's XZ encoder, CRC32, preset 6.
    const auto reference=unhex("fd377a585a0000016922de360200210116000000742fe5a301000b4661737420585a00ff0080000068413f240001200ca2ddb4bc9042990d010000000001595a");
    const Bytes original{'F','a','s','t',' ','X','Z',0,255,0,128,0};
    check(decode_xz(reference,original.size())==original,"Independent XZ reference did not preserve bytes");
    // Valid block-header CRC, but a hostile 4 GiB dictionary request.
    const auto oversized_dictionary=unhex("fd377a585a0000016922de360200210128000000e6a011b301000b4661737420585a00ff0080000068413f240001200ca2ddb4bc9042990d010000000001595a");
    bool memory_limited=false;
    try{decode_xz(oversized_dictionary,1024);}catch(const Error& error){
        memory_limited=std::string(error.what()).find("memory limit")!=std::string::npos;
    }
    check(memory_limited,"Received XZ dictionary escaped fixed decoder memory cap");
    for(const auto size:{0U,1U,16384U,65536U,100000U}) {
        Bytes source(size);std::mt19937 rng(417);
        for(auto& b:source)b=static_cast<std::uint8_t>(rng());
        std::size_t position=0,max_request=0;
        const auto encoded=collect(xz_source([&](std::span<std::uint8_t> output) {
            max_request=std::max(max_request,output.size());
            const auto n=std::min({output.size(),source.size()-position,std::size_t{11}});
            std::copy_n(source.begin()+static_cast<std::ptrdiff_t>(position),n,output.begin());position+=n;return n;
        },size));
        check(std::equal(encoded.begin(),encoded.begin()+6,reference.begin()),"Production source is not an XZ container");
        check(max_request<=16384&&encoded.size()<=xz_size_bound(size),"XZ streaming request/size bound exceeded");
        check(decode_xz(encoded,size)==source,"Fragmented XZ stream changed arbitrary source bytes");
        const auto prepared=prepare_xz_source(byte_source(source),size);
        check(prepared.source_bytes==source.size()&&prepared.encoded.size()<=xz_size_bound(size)&&
            prepared.encoded.capacity()<=xz_size_bound(size),"Prepared XZ source exceeded bounded storage");
        check(decode_xz(prepared.encoded,size)==source,"Prepared XZ source changed file bytes");
        if(size)rejects([&]{decode_xz(encoded,size-1);},"Expansion escaped local output quota");
    }
    auto invalid=reference;invalid.pop_back();
    rejects([&]{decode_xz(invalid,1024);},"Truncated XZ source accepted");
    invalid=reference;invalid.push_back(0);
    rejects([&]{decode_xz(invalid,1024);},"Trailing XZ bytes accepted");
    invalid=reference;invalid.insert(invalid.end(),reference.begin(),reference.end());
    rejects([&]{decode_xz(invalid,1024);},"Concatenated XZ streams accepted");
    invalid=reference;invalid[35]^=1;
    rejects([&]{decode_xz(invalid,1024);},"Corrupt XZ data/check accepted");
    rejects([&]{collect(xz_source(byte_source(Bytes(65,0)),64));},"Source quota escaped compression");
    rejects([&]{prepare_xz_source(byte_source(Bytes(65,0)),64);},"Prepared source escaped raw-byte quota");
    rejects([&]{collect(xz_source([](std::span<std::uint8_t> output){return output.size()+1;}));},"Oversized source reader result accepted");
    std::stop_source stop;stop.request_stop();
    rejects([&]{collect(xz_source(byte_source(original),1024,stop.get_token()));},"Cancelled compression continued");
    rejects([&]{prepare_xz_source(byte_source(original),1024,stop.get_token());},"Cancelled source preparation continued");
}
void large_incompressible_preparation() {
    // At this size streaming LZMA2 chunk boundaries exceed the library
    // single-call encoder bound. Use a fixed independent PRNG source.
    Bytes source(32U*1024*1024);std::uint32_t state=417;
    for(auto& byte:source) {state^=state<<13;state^=state>>17;state^=state<<5;byte=static_cast<std::uint8_t>(state);}
    const auto prepared=prepare_xz_source(byte_source(source),source.size());
    const auto old_single_call_bound=source.size()+3*(source.size()/65536)+144;
    check(prepared.encoded.size()>old_single_call_bound,
        "Incompressible fixture must exercise expansion beyond the old single-call bound");
    check(prepared.source_bytes==source.size()&&prepared.encoded.size()<=xz_size_bound(source.size())&&
        prepared.encoded.capacity()<=xz_size_bound(source.size()),"Prepared large incompressible source escaped its streaming bound");
    check(decode_xz(prepared.encoded,source.size())==source,"Large incompressible prepared source is not exact");
}
void source_domain_isolation() {
    // An old literal file can itself be a valid XZ container. The new source
    // mode must reject it rather than silently turn it into a different file.
    const auto compressed=collect(xz_source(byte_source(Bytes{0,1,2,255,0})));
    for(bool capacity:{false,true})for(bool encrypted:{false,true})for(bool tx_xz:{false,true}) {
        auto profile=capacity?capacity_profile(Channel::wire):classic_profile(Channel::wire);
        profile.interleave_depth=1;
        const auto key=encrypted?std::optional<Crypto>(Crypto(Bytes(32,47))):std::nullopt;
        StreamEncoder encoder(profile,key,byte_source(compressed),tx_xz?SourceEncoding::xz:SourceEncoding::raw);
        StreamDecoder decoder(profile,key,1024*1024,tx_xz?SourceEncoding::raw:SourceEncoding::xz);
        std::array<std::uint8_t,physical_interval_bits> bits{};
        std::array<float,physical_interval_bits> soft{};
        while(encoder.next_interval(bits)) {
            for(std::size_t i=0;i<bits.size();++i)soft[i]=bits[i]?12.f:-12.f;
            decoder.push_interval(soft);
        }
        check(decoder.snapshot().failed&&decoder.snapshot().decoding_stopped&&!decoder.snapshot().physical_end,
            "Mismatched raw/XZ source mode escaped bootstrap integrity");
        decoder.finish(true);
        check(!decoder.result()&&!decoder.snapshot().complete&&!decoder.snapshot().source_bytes,
            "Mismatched raw/XZ source became a different completed file");
    }
}
void physical_completion() {
    const Bytes source(65536,'x');
    const auto compressed=collect(xz_source(byte_source(source)));
    check(compressed.size()<1024,"Repeated source did not benefit from XZ");
    for(bool capacity:{false,true})for(bool encrypted:{false,true})for(unsigned corruption=0;corruption<3;++corruption) {
        auto profile=capacity?capacity_profile(Channel::wire):classic_profile(Channel::wire);
        profile.interleave_depth=1;
        const auto key=encrypted?std::optional<Crypto>(Crypto(Bytes(32,41))):std::nullopt;
        auto content=compressed;
        if(corruption==1)content.pop_back();
        if(corruption==2)content.push_back(0);
        StreamEncoder encoder(profile,key,byte_source(content),SourceEncoding::xz);
        StreamDecoder decoder(profile,key,1024*1024,SourceEncoding::xz);
        StreamDecoder quota(profile,key,32768,SourceEncoding::xz);
        std::array<std::uint8_t,physical_interval_bits> bits{};
        std::array<float,physical_interval_bits> soft{};
        while(encoder.next_interval(bits)) {
            for(std::size_t i=0;i<bits.size();++i)soft[i]=bits[i]?12.f:-12.f;
            decoder.push_interval(soft);quota.push_interval(soft);
            check(!decoder.result()&&!decoder.snapshot().source_bytes&&!decoder.snapshot().complete,
                "XZ content escaped before physical completion");
        }
        decoder.finish(false);quota.finish(false);
        check(!decoder.snapshot().failed&&!quota.snapshot().failed&&!decoder.result()&&!quota.result(),
            "XZ syntax or expansion was inspected before physical absence");
        decoder.finish(true);quota.finish(true);
        check(quota.snapshot().failed&&!quota.result()&&!quota.snapshot().source_bytes,
            "XZ decoded output escaped bounded source quota");
        if(corruption)check(decoder.snapshot().failed&&!decoder.result()&&!decoder.snapshot().source_bytes,
            "Invalid integrity-protected XZ source became complete");
        else {
            check(decoder.snapshot().complete&&decoder.result()&&decoder.result()->size()==source.size()&&
                Bytes(decoder.result()->bytes().begin(),decoder.result()->bytes().end())==source,
                "Physically completed XZ file is not exact");
            const auto exact=estimate_xz_transmission(profile,encrypted,byte_source(source));
            check(exact.intervals==encoder.intervals_emitted(),"XZ airtime estimate ignored compressed source");
            check(exact.source_bps==8.*source.size()/exact.seconds,
                "XZ goodput counted encoded bytes instead of original source");
            check(exact.intervals<estimate_transmission(profile,encrypted,source.size()).intervals,
                "XZ airtime did not benefit from source compression");
        }
    }
}
}
int main(){try{formats_and_bounds();large_incompressible_preparation();source_domain_isolation();physical_completion();std::cout<<"Fast XZ streaming, bounded decoding and physical-end tests passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
