#include "datapump/stream_codec.hpp"
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    try { action(); } catch(const Error&) { return; }
    throw std::runtime_error(message);
}
Bytes noise(std::size_t count,std::mt19937& random) {
    Bytes result(count);for(auto& byte:result)byte=static_cast<std::uint8_t>(random());return result;
}
IntervalOptions keyed(FecMode mode,std::uint8_t key,std::uint64_t address) {
    IntervalOptions options;options.fec=mode;
    options.authenticator=[key,address,mode](const Bytes& data) {
        Bytes canonical{'t','e','s','t',static_cast<std::uint8_t>(mode)};
        for(unsigned i=0;i<8;++i)canonical.push_back(static_cast<std::uint8_t>(address>>(56-i*8)));
        canonical.insert(canonical.end(),data.begin(),data.end());
        Bytes tag(32);unsigned size=0;
        if(!HMAC(EVP_sha256(),&key,1,canonical.data(),canonical.size(),tag.data(),&size)||size!=tag.size())
            throw std::runtime_error("test HMAC failed");
        return tag;
    };
    options.verifier=[auth=options.authenticator](const Bytes& data,const Bytes& tag) {
        const auto expected=auth(data);return tag.size()==expected.size() && !CRYPTO_memcmp(tag.data(),expected.data(),tag.size());
    };
    return options;
}
void generic_rs() {
    std::mt19937 random(0x199b7);
    for(const auto parity:{2U,4U,22U,48U,90U}) {
        const auto pristine=fec::rs_encode(noise(255-parity,random),parity);
        for(unsigned trial=0;trial<70;++trial) {
            const auto erasures=trial%(parity+1),errors=(parity-erasures)/2;
            std::vector<std::size_t> positions(pristine.size());std::iota(positions.begin(),positions.end(),0);
            std::shuffle(positions.begin(),positions.end(),random);auto damaged=pristine;
            for(std::size_t i=0;i<erasures+errors;++i)damaged[positions[i]]^=static_cast<std::uint8_t>(1+random()%255);
            const auto changed=fec::rs_correct(damaged,parity,std::span(positions).first(erasures));
            check(changed==erasures+errors && damaged==pristine,"mixed errors/erasures at bound must repair exactly");
        }
        std::vector<std::size_t> erased(parity+1);std::iota(erased.begin(),erased.end(),0);
        auto unchanged=pristine;
        rejects([&]{fec::rs_correct(unchanged,parity,erased);},"too many erasures rejected even for zero syndrome");
        check(unchanged==pristine,"invalid erasures leave codeword unchanged");
        erased={0,0};rejects([&]{fec::rs_correct(unchanged,parity,erased);},"duplicate erasures rejected");
        erased={pristine.size()};rejects([&]{fec::rs_correct(unchanged,parity,erased);},"out-of-range erasure rejected");
    }
    // A known erasure may already contain its true zero value. It still uses
    // one erasure equation, and cannot be silently removed from the budget.
    auto zero=fec::rs_encode(Bytes(106),22);std::vector<std::size_t> erased(22);std::iota(erased.begin(),erased.end(),0);
    check(fec::rs_correct(zero,22,erased)==0,"valid zero-valued erasures need no changed values");
}
void intervals() {
    std::mt19937 random(9041);
    for(auto mode:{FecMode::off,FecMode::rs20,FecMode::rs60})for(bool encrypted:{false,true}) {
        auto options=encrypted?keyed(mode,41,0x100000005):IntervalOptions{mode,{},{}};
        const auto input=noise(interval_data_bytes(mode,encrypted),random),wire=encode_interval(input,options);
        check(wire.size()==128,"fixed interval must contain exactly 128 bytes");
        auto decoded=decode_interval(wire,options);
        check(decoded.data==input && decoded.authenticated==encrypted,"public/keyed interval roundtrip");
        check(decoded.pre_fec_accuracy && !decoded.pre_fec_accuracy->corrected_data_bits,"pristine interval accuracy");
        if(encrypted) {
            rejects([&]{decode_interval(wire,keyed(mode,42,0x100000005));},"wrong key must fail");
            rejects([&]{decode_interval(wire,keyed(mode,41,0x100000006));},"wrong canonical address must fail");
        }
        const auto parity=interval_parity_bytes(mode);
        if(parity) {
            auto damaged=wire;
            for(std::size_t i=0;i<parity/2;++i)damaged[127-i]^=static_cast<std::uint8_t>(31+i);
            decoded=decode_interval(damaged,options);
            check(decoded.data==input && decoded.corrected_bytes==parity/2,"last parity positions remain correctable");
            check(decoded.pre_fec_accuracy && !decoded.pre_fec_accuracy->corrected_data_bits,"parity damage excluded from data accuracy");
            std::vector<std::size_t> erased(parity);std::iota(erased.begin(),erased.end(),0);
            damaged=wire;for(auto position:erased)damaged[position]=0;
            decoded=decode_interval(damaged,options,erased);
            check(decoded.data==input && decoded.erased_bytes==parity && !decoded.pre_fec_accuracy,"full erasure budget recovers without invented accuracy");
        } else {
            const std::array<std::size_t,1> erased{0};
            rejects([&]{decode_interval(wire,options,erased);},"no-FEC unknown occupancy cannot become a source byte");
        }
        rejects([&]{decode_interval(std::span(wire).first(127),options);},"short codewords must be explicitly completed with erased slots");
        rejects([&]{encode_interval(std::span(input).first(input.size()-1),options);},"variable source area rejected");
    }
    auto options=keyed(FecMode::off,3,4);options.verifier={};
    const auto wire=encode_interval(Bytes(96),options);
    rejects([&]{decode_interval(wire,options);},"keyed decode cannot accidentally omit verifier");
    options.verifier=[](const Bytes&,const Bytes&){return true;};options.authenticator={};
    rejects([&]{encode_interval(Bytes(96),options);},"keyed encode cannot accidentally omit authenticator");
    rejects([]{interval_parity_bytes(static_cast<FecMode>(99));},"unknown FEC profile rejected");
}
void source_cells() {
    std::mt19937 random(0x842);
    for(auto area:{128U,106U,80U,96U,74U,48U}) {
        const auto capacity=source_bytes_per_interval(area,false);
        for(std::size_t count=0;count<=capacity*2;++count) {
            auto input=noise(count,random);
            if(count)input.back()=0;
            if(count>1)input[count-2]=0;
            const auto encoded=encode_source(input,area,false);
            check(!encoded.empty() && encoded.size()%area==0,"validity source areas have fixed widths");
            check(decode_source(encoded,area,false)==input,"validity cells preserve exact bytes and zero suffix");
            if(count)rejects([&]{decode_source(encoded,area,false,count-1);},"cell output quota enforced");
        }
        auto empty=encode_source({},area,false);empty[0]=0x01;
        rejects([&]{decode_source(empty,area,false);},"absent cell requires zero payload");
        empty=encode_source({},area,false);empty[1]=0x40;
        rejects([&]{decode_source(empty,area,false);},"occupied cell following absent cell rejected");
        if((area*8)%9) {
            empty=encode_source({},area,false);empty.back()=1;
            rejects([&]{decode_source(empty,area,false);},"unused fixed area bits must be zero");
        }
        const auto first=encode_source(Bytes{0,0xff,0},area,false),second=encode_source(Bytes{0x80,0},area,false);
        auto joined=first;joined.insert(joined.end(),second.begin(),second.end());
        check(decode_source(joined,area,false)==Bytes({0,0xff,0,0x80,0}),"underfilled interval is not a stream end");
    }
    rejects([]{encode_source(Bytes(100),74,false,1);},"fixed source storage quota enforced");
}
void compressed_sources() {
    std::mt19937 random(11);
    for(auto count:{0U,1U,44U,256U,8193U}) {
        auto input=noise(count,random);if(count)input.back()=0;
        for(auto area:{106U,74U,80U,48U}) {
            const auto encoded=encode_source(input,area,true);
            check(encoded.size()%area==0 && decode_source(encoded,area,true)==input,"always compressed exact bytes roundtrip");
            if(count)rejects([&]{decode_source(encoded,area,true,count-1);},"bounded post-end decompression");
            auto extra=encoded;extra.resize(extra.size()+area);
            rejects([&]{decode_source(extra,area,true);},"extra all-padding interval rejected");
            if(encoded.size()>area)rejects([&]{decode_source(std::span(encoded).first(encoded.size()-area),area,true);},"lost last interval removes codec end");
        }
    }
}
}
int main() {
    try { generic_rs();intervals();source_cells();compressed_sources();std::cout<<"fixed stream codec passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
