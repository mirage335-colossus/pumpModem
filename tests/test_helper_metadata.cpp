#include "datapump/helper_metadata.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace h=datapump::helper;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f) {try {f();} catch(const datapump::Error&) {return;} throw std::runtime_error("invalid input accepted");}
std::span<const std::uint32_t> view(const h::MetadataPacket& p) {return {p.symbols.data(),p.count};}
void records() {
    for(const auto& text:{std::string{},std::string{"A"},std::string{"N0CALL"},std::string{"ABCDEFGHIJKLMNOP"}}) {
        const auto packet=h::metadata_packet(123456789,text);
        check(packet.count==9,"wrong record width");
        check(packet.symbols[0]==(123456789%532480)*2/65,"incorrect time phase");
        const auto decoded=h::decode_metadata_packet(view(packet));
        check(decoded&&decoded->callsign==text&&decoded->time_ticks==packet.symbols[0]&&!decoded->authenticated,"record round trip");
    }
    const auto vector=h::metadata_packet(0,"AB");
    check(vector.symbols[0]==0&&vector.symbols[1]==(65U<<7|66U),"wire layout");
    check(std::all_of(vector.symbols.begin()+2,vector.symbols.end(),[](auto n){return n==0;}),"padding");
    rejects([]{h::metadata_packet(0,"ABCDEFGHIJKLMNOPQ");});
    rejects([]{h::metadata_packet(0,std::string("A\0B",3));});
    rejects([]{h::metadata_packet(0,std::string(1,char(128)));});
    auto malformed=vector;malformed.symbols[2]=1;
    check(!h::decode_metadata_packet(view(malformed)),"noncanonical padding accepted");
    malformed=vector;malformed.symbols[0]=1U<<14;
    check(!h::decode_metadata_packet(view(malformed)),"out-of-range symbol accepted");
    check(!h::decode_metadata_packet(view(vector).first(8)),"truncated record accepted");
}
void time_phase() {
    check(h::metadata_packet(0,{}).symbols[0]==0,"epoch");
    check(h::metadata_packet(532479,{}).symbols[0]==16383,"last tick before wrap");
    check(h::metadata_packet(532480,{}).symbols[0]==0,"wrap");
    for(std::uint64_t t=0;t<532480;t+=13)
        check(h::metadata_packet(t,{}).symbols[0]==h::metadata_packet(t+532480,{}).symbols[0],"periodicity");
    check(8.125/16384<.0005&&8.125>8.,"time requirements");
}
void encrypted_records() {
    std::array<std::uint8_t,32> master{};
    for(unsigned i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const datapump::Crypto key(master);
    const std::array<datapump::Crypto,1> keys{key};
    struct Vector {std::uint64_t dwell,ticks;std::array<std::uint32_t,9> symbols;};
    // Independent Python hashlib/hmac HKDF and openssl enc AES-256-CTR.
    const std::array<Vector,5> vectors{{
        {0,0,{0,1491,5608,132,9865,9169,8719,2903,15422}},
        {14,367002,{11292,2927,3901,687,10437,15888,13137,1993,3797}},
        {15,393216,{12098,7022,8672,4935,11867,3688,14963,8606,7113}},
        {162,4246733,{15980,5171,2291,10937,10069,2000,4233,754,8194}},
        {4500000000ULL,117964800000000ULL,{8822,4925,4670,4484,9231,15547,2791,12941,13150}}
    }};
    for(const auto& vector:vectors) {
        const auto packet=h::metadata_packet(vector.ticks,"N0CALL",&key);
        check(packet.symbols==vector.symbols,"CTR independent vector mismatch");
        check(!h::decode_metadata_packet(view(packet),keys),"keyed record decoded without capture clock");
        const auto decoded=h::decode_metadata_packet(view(packet),keys,vector.ticks);
        check(decoded&&decoded->kind==h::MetadataKind::encrypted_time&&decoded->callsign=="N0CALL"&&
            decoded->time_ticks==packet.symbols[0]&&!decoded->authenticated,"CTR record did not decode");
    }
    // Exercise both sides of each six-second bucket and of the 8.125-second
    // phase wrap; the receiver calculates one absolute dwell, with no search.
    for(std::uint64_t dwell=10;dwell<400;++dwell) {
        const auto ticks=(dwell*131072+2)/5;
        const auto packet=h::metadata_packet(ticks,"ABCDEFGHIJKLMNOP",&key);
        for(const auto offset:{-262144LL,-262143LL,-1LL,0LL,1LL,262143LL,262144LL}) {
            const auto anchor=static_cast<std::uint64_t>(static_cast<std::int64_t>(ticks)+offset);
            const auto decoded=h::decode_metadata_packet(view(packet),keys,anchor);
            check(decoded&&decoded->callsign=="ABCDEFGHIJKLMNOP","CTR clock/bucket boundary disagreement");
        }
    }
    // Distinguish changing absolute time from the repeating clear phase.
    const auto first=h::metadata_packet(0,"N0CALL",&key);
    const auto later=h::metadata_packet(130ULL*65536,"N0CALL",&key);
    check(first.symbols[0]==later.symbols[0]&&first.symbols!=later.symbols,"CTR repeated with modulo time phase");
    check(h::metadata_packet(0,"N0CALL",&key).symbols!=h::metadata_packet(26214,"N0CALL",&key).symbols,
        "CTR pad reused across adjacent dwells");
    rejects([&]{h::metadata_packet(1,"N0CALL",&key);});
    const auto empty=h::timecode_packet(0,&key);
    const auto empty_decoded=h::decode_metadata_packet(view(empty),keys,0);
    check(empty_decoded&&empty_decoded->callsign.empty(),"encrypted empty character field");
    struct Properties {~Properties() {EVP_set_default_properties(nullptr,"");}} restore;
    check(EVP_set_default_properties(nullptr,"provider=missing")==1,"provider exclusion setup failed");
    rejects([&]{h::metadata_packet(0,"N0CALL",&key);});
    check(!h::decode_metadata_packet(view(first),keys,0),"missing cipher did not fail closed");
    check(h::decode_metadata_packet(view(h::metadata_packet(0,"N0CALL"))).has_value(),"clear codec requires AES");
}
}
int main() {
    try {records();time_phase();encrypted_records();std::cout<<"fixed Helper metadata codec passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
