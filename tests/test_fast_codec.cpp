#include "datapump/fast/codec.hpp"
#include "datapump/stream_codec.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>

using namespace datapump;
using namespace datapump::fast;
namespace {
void check(bool condition,const char* why) {if(!condition)throw std::runtime_error(why);}
template<class F>void rejects(F&& action,const char* why) {
    try {action();}catch(const Error&) {return;}
    throw std::runtime_error(why);
}
Bytes unhex(std::string_view hex) {
    Bytes bytes;for(std::size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(i,2)),nullptr,16)));
    return bytes;
}
Crypto key(unsigned seed=0) {Bytes bytes(32);for(unsigned i=0;i<32;++i)bytes[i]=static_cast<std::uint8_t>(i+seed);return Crypto(bytes);}
SourceReader memory_source(Bytes data) {
    return [data=std::move(data),position=std::size_t{0}](std::span<std::uint8_t> out)mutable {
        const auto count=std::min(out.size(),data.size()-position);
        std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(position),count,out.begin());position+=count;return count;
    };
}
Bytes transmit(const Profile& p,const Crypto& crypto,const Bytes& bytes) {
    StreamEncoder encoder(p,crypto,memory_source(bytes));Bytes wire;std::array<std::uint8_t,physical_interval_bits> interval{};
    while(encoder.next_interval(interval))wire.insert(wire.end(),interval.begin(),interval.end());
    check(encoder.source_bytes()==bytes.size(),"TX byte counter");
    check(encoder.intervals_emitted()*physical_interval_bits==wire.size(),"TX physical cadence counter");return wire;
}
void feed(StreamDecoder& rx,std::span<const std::uint8_t> wire) {
    std::array<float,physical_interval_bits> soft{};
    check(wire.size()%physical_interval_bits==0,"fixture fixed cadence");
    for(std::size_t at=0;at<wire.size();at+=physical_interval_bits) {
        for(std::size_t b=0;b<soft.size();++b)soft[b]=wire[at+b]?12.F:-12.F;
        rx.push_interval(soft);
    }
}
std::size_t coded_size(const Profile& p) {return coding::encode(Bytes(p.interleave_depth*256),p.code_rate).size();}
Bytes systematic(const Profile& p,std::span<const std::uint8_t> cycle) {
    std::vector<float> soft(coded_size(p));for(std::size_t i=0;i<soft.size();++i)soft[i]=cycle[i]?12.F:-12.F;
    auto de=coding::decode(soft,p.interleave_depth*256,p.code_rate);Bytes out;
    const auto rows=p.interleave_depth*2U,k=p.robust?112U:120U;
    for(unsigned row=0;row<rows;++row)for(unsigned column=0;column<k;++column)out.push_back(de.bytes[column*rows+row]);
    return out;
}
Bytes code_systematic(const Profile& p,std::span<const std::uint8_t> systematic) {
    const auto rows=p.interleave_depth*2U,k=p.robust?112U:120U;Bytes words(rows*128);
    for(unsigned row=0;row<rows;++row) {
        auto area=systematic.subspan(row*k,k);auto word=fec::rs_encode(Bytes(area.begin(),area.end()),128-k);
        for(unsigned col=0;col<128;++col)words[col*rows+row]=word[col];
    }
    auto bits=coding::encode(words,p.code_rate);bits.resize(cycle_intervals(p)*physical_interval_bits);return bits;
}
void independent_vectors() {
    // Frozen outputs generated independently using a Python shift register,
    // hashlib/HMAC/HKDF and cryptography's AES implementation. Do not derive
    // these expected values from the production encoder at test runtime.
    const std::array<std::string_view,3> expected{
        "1101001111101100001101110110101001100010110000001110100110001111000101100111",
        "110011101100110101101000001100001110011011100100011",
        "11001101000101101010100100001001100110111001"};
    unsigned vector=0;
    for(auto rate:{CodeRate::half,CodeRate::three_quarters,CodeRate::seven_eighths}) {
        const Bytes source{0x80,0x5a,0,0xff};const auto wire=coding::encode(source,rate);
        std::string text;for(auto b:wire)text.push_back(static_cast<char>('0'+b));
        check(text==expected[vector++],"independent K7/puncture/tail vector");
        std::vector<float> soft;for(auto b:wire)soft.push_back(b?8.F:-8.F);
        check(coding::decode(soft,source.size(),rate).bytes==source,"independent inner vector decode");
    }
    auto p=profile(Channel::wire);Bytes salt(32),iv(16),plain(176);
    std::iota(salt.begin(),salt.end(),32);std::iota(iv.begin(),iv.end(),0);std::iota(plain.begin(),plain.end(),0);
    const auto expected_crypto=unhex(
        "000102030405060708090a0b0c0d0e0f1e03c7f5409c338d0198fd87e3fffc4691e3b226d5c20647f492e61bd1b833a5"
        "1ecfb2010b4917049caec766959b2c3d5b2eb9ab03ce8caec1f67f5dda0d4dabb5f1b1e9645fa737da7e4809f01d9165"
        "8d43c52c440eb2a6073dd344e2e30582d8f22b0c79ff98200e5230b67707c79e38920bc6cbbb907444df1f55539556db69"
        "7bda7137b963b932229faad89a5486185e22bec842ae8138a56c41d8d4bd85f828a6f048fa25d698e9c0295e75cc294965e"
        "8c7d6c8815c1f3001358aa670dcd4beda505f2f8769fb5af14116fd25b1");
    const auto crypto=key();auto group=fast::testing::seal_group(p,crypto,salt,7,iv,plain);
    check(group==expected_crypto,"independent CBC/HKDF/group HMAC vector");
    check(fast::testing::open_group(p,crypto,salt,7,group)==plain,"authenticated vector decryption");
    rejects([&]{fast::testing::open_group(p,crypto,salt,8,group);},"reordered group rejected");
    rejects([&]{fast::testing::open_group(p,key(1),salt,7,group);},"wrong key rejected");
    auto wrong=p;wrong.constellation=64;
    rejects([&]{fast::testing::open_group(wrong,crypto,salt,7,group);},"local profile bound by MAC");
    auto wrongsalt=salt;wrongsalt[0]^=1;
    rejects([&]{fast::testing::open_group(p,crypto,wrongsalt,7,group);},"salt bound by key derivation");
    for(auto index:{0U,15U,16U,191U,192U,223U}) {
        auto damaged=group;damaged[index]^=1;
        rejects([&]{fast::testing::open_group(p,crypto,salt,7,damaged);},"IV/cipher/tag tamper rejected before plaintext");
    }
    // Independently frozen full stream fingerprint, including bootstrap MAC,
    // RS polynomial/interleave column order, termination and physical fill.
    p.interleave_depth=1;
    const std::string domain="DataPump/fast/v1/bootstrap";Bytes canonical(domain.begin(),domain.end());
    const auto context=profile_id(p);canonical.insert(canonical.end(),context.begin(),context.end());
    canonical.insert(canonical.end(),salt.begin(),salt.end());
    Bytes bootstrap=salt;const auto tag=crypto.mac(canonical);bootstrap.insert(bootstrap.end(),tag.begin(),tag.end());bootstrap.resize(224);
    auto fixed_wire=code_systematic(p,bootstrap);std::fill(plain.begin(),plain.end(),0);
    const Bytes source{0,0xff,0x42};std::size_t position=0;
    for(auto byte:source)for(int bit=8;bit>=0;--bit,++position)
        plain[position/8]|=static_cast<std::uint8_t>((((256U|byte)>>bit)&1U)<<(7-position%8));
    auto coded=code_systematic(p,fast::testing::seal_group(p,crypto,salt,0,iv,plain));
    fixed_wire.insert(fixed_wire.end(),coded.begin(),coded.end());
    Bytes digest(32);unsigned digest_size=0;
    check(EVP_Digest(fixed_wire.data(),fixed_wire.size(),digest.data(),&digest_size,EVP_sha256(),nullptr)==1 && digest_size==32,"wire fingerprint hashing");
    check(digest==unhex("e52aca6df25bb57b988ca1bea41a53d530815f7f1015589dd01c7ffac04aa6b7"),"independent full fixed-cadence wire vector");
    StreamDecoder fixed_rx(p,crypto);feed(fixed_rx,fixed_wire);fixed_rx.finish(true);
    check(fixed_rx.result() && fixed_rx.result()->preview()==source,"independent full wire vector decode");
}
void roundtrips() {
    std::mt19937 random(311);auto crypto=key();
    {
        auto p=profile(Channel::wire);p.interleave_depth=1;
        auto a=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),71);
        auto b=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),71);
        auto c=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),72);
        std::array<std::uint8_t,physical_interval_bits> aa{},bb{},cc{};
        check(a.next_interval(aa)&&b.next_interval(bb)&&c.next_interval(cc),"deterministic test encoders emit intervals");
        check(aa==bb && aa!=cc,"regression entropy is repeatable and scoped per encoder");
    }
    for(auto rate:{CodeRate::half,CodeRate::three_quarters,CodeRate::seven_eighths})for(bool robust:{false,true}) {
        auto p=profile(Channel::wire);p.code_rate=rate;p.robust=robust;p.interleave_depth=1;
        const auto capacity=ciphertext_bytes(p)*8/9;
        for(auto size:{std::size_t{0},std::size_t{1},capacity-1,capacity,capacity+1,std::size_t{513}}) {
            Bytes source(size);for(auto& b:source)b=static_cast<std::uint8_t>(random());
            if(size)source.back()=0;
            auto wire=transmit(p,crypto,source);StreamDecoder decoder(p,crypto);feed(decoder,wire);
            check(!decoder.result() && !decoder.snapshot().physical_end && !decoder.snapshot().source_bytes,"never expose source before physical end");
            decoder.finish(false);
            check(!decoder.result() && !decoder.snapshot().physical_end,"EOF cannot manufacture physical completion");
            decoder.finish(true);check(decoder.snapshot().complete,"fixed-rate source roundtrip completes");
            check(decoder.result()->preview()==source,"exact source and trailing zero retained");
            if(size==capacity)check(wire.size()==3*cycle_intervals(p)*physical_interval_bits,"mandatory endpoint may occupy extra final cycle");
        }
    }
    auto p=profile(Channel::wire);p.interleave_depth=1;const auto wire=transmit(p,crypto,Bytes(123,0));
    StreamDecoder zero(p,crypto);feed(zero,wire);zero.finish(true);check(zero.result()->preview()==Bytes(123,0),"all-zero file");
    StreamDecoder wrong(p,key(1));feed(wrong,wire);wrong.finish(true);check(wrong.snapshot().failed && !wrong.result(),"wrong bootstrap key fails");
    auto missing=wire;missing.resize(missing.size()-physical_interval_bits);
    StreamDecoder truncated(p,crypto);feed(truncated,missing);truncated.finish(true);check(!truncated.result(),"partial final cycle unavailable");
    missing.assign(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(cycle_intervals(p)*physical_interval_bits));
    StreamDecoder only_bootstrap(p,crypto);feed(only_bootstrap,missing);only_bootstrap.finish(true);check(!only_bootstrap.result(),"missing entire source cycle unavailable");
    StreamDecoder quota(p,crypto,64);feed(quota,wire);quota.finish(true);check(quota.snapshot().failed && !quota.result(),"local spool quota failure cannot complete");
    StreamDecoder combined(p,crypto,176+123-1);feed(combined,wire);combined.finish(true);
    check(combined.snapshot().failed && !combined.result(),"both private source areas and final file count against quota");
    StreamDecoder exact_quota(p,crypto,176+123);feed(exact_quota,wire);exact_quota.finish(true);
    check(exact_quota.snapshot().complete && exact_quota.snapshot().spool_bytes==176+123,"exact combined-spool quota is usable");
    rejects([&]{StreamDecoder bad(p,crypto);bad.push_interval(std::array<float,7>{});},"remotely variable interval widths rejected");
    std::array<float,physical_interval_bits> nonfinite{};nonfinite.fill(std::numeric_limits<float>::quiet_NaN());
    StreamDecoder nan(p,crypto);for(std::size_t i=0;i<cycle_intervals(p);++i)nan.push_interval(nonfinite);
    nan.finish(true);check(nan.snapshot().failed,"non-finite channel evidence fails closed");
}
void canonical_sources() {
    const auto crypto=key();auto p=profile(Channel::wire);p.interleave_depth=1;
    const auto pristine=transmit(p,crypto,{});const auto width=cycle_intervals(p)*physical_interval_bits;
    const auto bootstrap=systematic(p,std::span(pristine).first(width));const auto salt=std::span(bootstrap).first(32);
    for(auto bad:{0,1,2}) {
        Bytes plain(ciphertext_bytes(p));if(bad==0)plain[1]=0x80;if(bad==1)plain.back()=1;
        if(bad==2)std::fill(plain.begin(),plain.end(),0xff);
        const auto group=fast::testing::seal_group(p,crypto,salt,0,Bytes(16),plain);
        auto wire=Bytes(pristine.begin(),pristine.begin()+static_cast<std::ptrdiff_t>(width));const auto cycle=code_systematic(p,group);
        wire.insert(wire.end(),cycle.begin(),cycle.end());StreamDecoder rx(p,crypto);feed(rx,wire);rx.finish(true);
        check(rx.snapshot().failed && !rx.result(),"authenticated malformed source/fill cannot become complete file");
    }
    auto extra=pristine;const auto padding=fast::testing::seal_group(p,crypto,salt,1,Bytes(16),Bytes(ciphertext_bytes(p)));
    const auto cycle=code_systematic(p,padding);extra.insert(extra.end(),cycle.begin(),cycle.end());
    StreamDecoder rx(p,crypto);feed(rx,extra);rx.finish(true);check(rx.snapshot().failed && !rx.result(),"whole extra padding supercycle rejected");
    const auto longwire=transmit(p,crypto,Bytes(650,4));auto interior=longwire;
    interior.erase(interior.begin()+static_cast<std::ptrdiff_t>(width),interior.begin()+static_cast<std::ptrdiff_t>(2*width));
    StreamDecoder missing(p,crypto);feed(missing,interior);missing.finish(true);check(!missing.result(),"lost interior group never joins source neighbors");
    auto final=longwire;final.resize(final.size()-width);StreamDecoder last(p,crypto);feed(last,final);last.finish(true);
    check(!last.result(),"lost whole final group cannot validate prefix");
}
void burst_and_soft() {
    auto p=profile(Channel::wire);p.interleave_depth=16;const auto crypto=key();Bytes source(2100);
    std::iota(source.begin(),source.end(),0);const auto wire=transmit(p,crypto,source);
    StreamDecoder rx(p,crypto);std::array<float,physical_interval_bits> soft{};
    const auto start=cycle_intervals(p)+3;
    for(std::size_t interval=0;interval<wire.size()/physical_interval_bits;++interval) {
        for(std::size_t b=0;b<soft.size();++b)soft[b]=interval==start?0.F:(wire[interval*soft.size()+b]?8.F:-8.F);
        rx.push_interval(soft);
    }
    rx.finish(true);check(rx.snapshot().complete && rx.result()->preview()==source,"interleaver/RS recover one fully erased inner physical interval");
    check(rx.snapshot().erased_bytes>0,"RS erasure recovery is visible");
    Bytes raw(256);std::iota(raw.begin(),raw.end(),0);auto bits=coding::encode(raw,CodeRate::half);
    std::vector<float> noisy;for(std::size_t i=0;i<bits.size();++i)noisy.push_back((bits[i]?8.F:-8.F)*(i%211==0?-0.1F:1.F));
    check(coding::decode(noisy,raw.size(),CodeRate::half).bytes==raw,"soft Viterbi corrects sparse low-confidence errors");
}
void streamed(std::size_t total) {
    auto p=profile(Channel::wire);p.interleave_depth=4;auto crypto=key();std::size_t generated=0,max_request=0;
    StreamEncoder tx(p,crypto,[&](std::span<std::uint8_t> output) {
        max_request=std::max(max_request,output.size());const auto n=std::min(output.size(),total-generated);
        for(std::size_t i=0;i<n;++i)output[i]=static_cast<std::uint8_t>((generated+i)*71+3);
        generated+=n;return n;
    });
    StreamDecoder rx(p,crypto,total*3+32768);std::array<std::uint8_t,physical_interval_bits> bits{};
    while(tx.next_interval(bits))feed(rx,bits);
    rx.finish(true);
    check(rx.snapshot().complete && rx.result()->size()==total,"large lazy file stream exact byte count");
    check(max_request<=16384,"source scratch is independent of file size");
    check(rx.snapshot().spool_bytes<total*3+32768,"combined spool usage bounded by local quota");
    const auto path=std::filesystem::temp_directory_path()/std::filesystem::path("datapump-fast-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    rx.result()->save(path);rejects([&]{rx.result()->save(path);},"save never overwrites destination");
    std::ifstream input(path,std::ios::binary);std::array<char,16384> buffer{};std::size_t offset=0;
    while(input) {
        input.read(buffer.data(),buffer.size());const auto count=static_cast<std::size_t>(input.gcount());
        for(std::size_t i=0;i<count;++i)check(static_cast<std::uint8_t>(buffer[i])==static_cast<std::uint8_t>((offset+i)*71+3),"saved streamed file exact comparison");
        offset+=count;
    }
    check(offset==total,"save exact size");input.close();std::filesystem::remove(path);
}
}
int main(int argc,char**) {
    try {
        independent_vectors();roundtrips();canonical_sources();burst_and_soft();streamed(argc>1?2*1024*1024:64*1024);
        std::cout<<"fast fixed-cadence crypto/FEC/source tests passed\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
