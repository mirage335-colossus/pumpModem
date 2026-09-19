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
Bytes transmit(const Profile& p,const std::optional<Crypto>& crypto,const Bytes& bytes) {
    StreamEncoder encoder(p,crypto,byte_source(bytes));Bytes wire;std::array<std::uint8_t,physical_interval_bits> interval{};
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
Bytes sha256(const Bytes& bytes) {
    Bytes digest(32);unsigned count=0;
    check(EVP_Digest(bytes.data(),bytes.size(),digest.data(),&count,EVP_sha256(),nullptr)==1 && count==32,"SHA-256 fixture");
    return digest;
}
Bytes public_group(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,const Bytes& plain) {
    const std::string domain="DataPump/fast/v1/public/group";Bytes canonical(domain.begin(),domain.end());
    const auto context=profile_id(p);canonical.insert(canonical.end(),context.begin(),context.end());canonical.insert(canonical.end(),salt.begin(),salt.end());
    for(unsigned i=0;i<8;++i)canonical.push_back(static_cast<std::uint8_t>(ordinal>>(56-i*8)));
    canonical.insert(canonical.end(),plain.begin(),plain.end());const auto digest=sha256(canonical);
    auto group=plain;group.insert(group.end(),digest.begin(),digest.end());return group;
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
    check(fixed_rx.result() && Bytes(fixed_rx.result()->bytes().begin(),fixed_rx.result()->bytes().end())==source,"independent full wire vector decode");

    // Public checksum mode has its own independent full-wire vector. It is
    // deliberately not an authentication construction: anyone can recreate it.
    const std::string public_domain="DataPump/fast/v1/public/bootstrap";
    Bytes public_canonical(public_domain.begin(),public_domain.end());
    public_canonical.insert(public_canonical.end(),context.begin(),context.end());
    public_canonical.insert(public_canonical.end(),salt.begin(),salt.end());
    const auto public_digest=sha256(public_canonical);
    check(public_digest==unhex("a57327e7e8d74665e8e5237f1849c5fe59682c9775b3df820267e45379353eee"),"independent public bootstrap checksum");
    Bytes public_bootstrap=salt;public_bootstrap.insert(public_bootstrap.end(),public_digest.begin(),public_digest.end());public_bootstrap.resize(224);
    Bytes public_plain(source_bytes_per_group(p,false));position=0;
    for(auto byte:source)for(int bit=8;bit>=0;--bit,++position)
        public_plain[position/8]|=static_cast<std::uint8_t>((((256U|byte)>>bit)&1U)<<(7-position%8));
    const auto public_systematic=public_group(p,salt,0,public_plain);
    check(Bytes(public_systematic.end()-32,public_systematic.end())==unhex("0473a8d3bfa5c2cb7d69d1b328a9b8b7190140fc0e5d89fa2494f7313d03164e"),"independent public group checksum");
    auto public_wire=code_systematic(p,public_bootstrap);coded=code_systematic(p,public_systematic);
    public_wire.insert(public_wire.end(),coded.begin(),coded.end());
    check(sha256(public_wire)==unhex("82a64d0004b9588298479e8901f8bcd83008ec16644219d6ca6e11a09999c9fa"),"independent public complete wire vector");
    StreamDecoder public_fixed_rx(p,std::nullopt);feed(public_fixed_rx,public_wire);public_fixed_rx.finish(true);
    check(public_fixed_rx.result() && Bytes(public_fixed_rx.result()->bytes().begin(),public_fixed_rx.result()->bytes().end())==source && !public_fixed_rx.snapshot().authenticated,"independent public wire decodes without authentication claim");
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
            check(decoder.snapshot().encrypted && !decoder.snapshot().authenticated,"keyed pending reception is not whole-file authentication");
            decoder.finish(false);
            check(!decoder.result() && !decoder.snapshot().physical_end,"EOF cannot manufacture physical completion");
            decoder.finish(true);check(decoder.snapshot().complete,"fixed-rate source roundtrip completes");
            check(decoder.snapshot().authenticated && !decoder.snapshot().checksum_groups,"keyed completion reports authentication only");
            check(Bytes(decoder.result()->bytes().begin(),decoder.result()->bytes().end())==source,"exact source and trailing zero retained");
            if(size==capacity)check(wire.size()==3*cycle_intervals(p)*physical_interval_bits,"mandatory endpoint may occupy extra final cycle");
        }
    }
    auto p=profile(Channel::wire);p.interleave_depth=1;const auto wire=transmit(p,crypto,Bytes(123,0));
    StreamDecoder zero(p,crypto);feed(zero,wire);zero.finish(true);check(Bytes(zero.result()->bytes().begin(),zero.result()->bytes().end())==Bytes(123,0),"all-zero file");
    StreamDecoder wrong(p,key(1));feed(wrong,wire);wrong.finish(true);check(wrong.snapshot().failed && !wrong.result(),"wrong bootstrap key fails");
    auto missing=wire;missing.resize(missing.size()-physical_interval_bits);
    StreamDecoder truncated(p,crypto);feed(truncated,missing);truncated.finish(true);check(!truncated.result(),"partial final cycle unavailable");
    missing.assign(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(cycle_intervals(p)*physical_interval_bits));
    StreamDecoder only_bootstrap(p,crypto);feed(only_bootstrap,missing);only_bootstrap.finish(true);check(!only_bootstrap.result(),"missing entire source cycle unavailable");
    StreamDecoder quota(p,crypto,64);feed(quota,wire);quota.finish(true);check(quota.snapshot().failed && !quota.result(),"local spool quota failure cannot complete");
    StreamDecoder combined(p,crypto,176-1);feed(combined,wire);combined.finish(true);
    check(combined.snapshot().failed && !combined.result(),"encoded receive area cannot exceed memory quota");
    StreamDecoder exact_quota(p,crypto,176);feed(exact_quota,wire);exact_quota.finish(true);
    check(exact_quota.snapshot().complete && exact_quota.snapshot().spool_bytes==176,"exact in-place memory quota is usable");
    rejects([&]{StreamDecoder bad(p,crypto);bad.push_interval(std::array<float,7>{});},"remotely variable interval widths rejected");
    std::array<float,physical_interval_bits> nonfinite{};nonfinite.fill(std::numeric_limits<float>::quiet_NaN());
    StreamDecoder nan(p,crypto);for(std::size_t i=0;i<cycle_intervals(p);++i)nan.push_interval(nonfinite);
    nan.finish(true);check(nan.snapshot().failed,"non-finite channel evidence fails closed");
}
void public_roundtrips() {
    Bytes original{0,0xff,0xc3,0xa9,0xe2,0x98,0x83,0};auto reader=byte_source(original);original.assign(original.size(),77);
    std::array<std::uint8_t,6> scratch{};scratch.fill(42);Bytes read;
    while(const auto n=reader(std::span(scratch).first(3))) {
        check(n<=3 && scratch[3]==42,"owned byte reader respects every requested span");
        read.insert(read.end(),scratch.begin(),scratch.begin()+static_cast<std::ptrdiff_t>(n));
    }
    const Bytes utf8{0,0xff,0xc3,0xa9,0xe2,0x98,0x83,0};check(read==utf8,"byte source owns exact UTF-8/binary bytes");
    for(auto rate:{CodeRate::half,CodeRate::three_quarters,CodeRate::seven_eighths})for(bool robust:{false,true}) {
        auto p=profile(Channel::wire);p.code_rate=rate;p.robust=robust;p.interleave_depth=1;
        check(source_bytes_per_group(p,true)==ciphertext_bytes(p) && source_bytes_per_group(p,false)==(robust?192U:208U),"mode geometry is a fixed local choice");
        const auto capacity=source_bytes_per_group(p,false)*8/9;
        for(const auto& source:std::vector<Bytes>{Bytes{},Bytes{0},utf8,Bytes(capacity-1),Bytes(capacity),Bytes(capacity+1),Bytes(513,0xff)}) {
            const auto wire=transmit(p,std::nullopt,source);StreamDecoder rx(p,std::nullopt);feed(rx,wire);
            const auto pending=rx.snapshot();check(!pending.encrypted && !pending.authenticated && !pending.authenticated_groups && pending.checksum_groups,"public checksums never claim keyed authentication");
            check(!rx.result() && !pending.source_bytes,"public source stays pending until physical end");
            rx.finish(false);check(!rx.result() && !rx.snapshot().physical_end,"public EOF is not physical completion");
            rx.finish(true);const auto final=rx.snapshot();
            check(final.complete && !final.encrypted && !final.authenticated && !final.authenticated_groups,"public completion stays explicitly unauthenticated");
            check(final.status.find("not authenticated")!=std::string::npos && Bytes(rx.result()->bytes().begin(),rx.result()->bytes().end())==source,"public status and exact source match");
        }
    }
    auto p=profile(Channel::wire);p.interleave_depth=1;const auto crypto=key();const auto public_wire=transmit(p,std::nullopt,utf8);
    StreamDecoder keyed_rx(p,crypto);feed(keyed_rx,public_wire);keyed_rx.finish(true);
    check(keyed_rx.snapshot().failed && !keyed_rx.result(),"keyed receiver never falls back to public mode");
    StreamDecoder public_rx(p,std::nullopt);feed(public_rx,transmit(p,crypto,utf8));public_rx.finish(true);
    check(public_rx.snapshot().failed && !public_rx.result(),"public receiver never autodetects encrypted mode");
    const auto wire=transmit(p,std::nullopt,Bytes(123));
    StreamDecoder limit(p,std::nullopt,192-1);feed(limit,wire);limit.finish(true);
    check(limit.snapshot().failed && !limit.result(),"public source stays within memory quota");
    StreamDecoder exact(p,std::nullopt,192);feed(exact,wire);exact.finish(true);
    check(exact.snapshot().complete && exact.snapshot().spool_bytes==192,"public exact in-place memory quota");
}
void public_malformed() {
    auto p=profile(Channel::wire);p.interleave_depth=1;const auto width=cycle_intervals(p)*physical_interval_bits;
    const auto pristine=transmit(p,std::nullopt,{});const auto bootstrap=systematic(p,std::span(pristine).first(width));
    const auto salt=std::span(bootstrap).first(32);const auto good_group=systematic(p,std::span(pristine).subspan(width,width));
    const auto invalid=[&](const Bytes& wire,const char* why) {
        StreamDecoder rx(p,std::nullopt);feed(rx,wire);rx.finish(true);check(rx.snapshot().failed && !rx.snapshot().authenticated && !rx.result(),why);
    };
    for(const auto position:{0U,191U,192U,223U}) {
        auto corrupt=good_group;corrupt[position]^=1;auto wire=pristine;
        const auto replacement=code_systematic(p,corrupt);std::copy(replacement.begin(),replacement.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));
        invalid(wire,"public data/digest corruption is rejected after FEC");
    }
    auto damaged_boot=bootstrap;damaged_boot[0]^=1;auto damaged=pristine;auto replacement=code_systematic(p,damaged_boot);
    std::copy(replacement.begin(),replacement.end(),damaged.begin());invalid(damaged,"public transfer salt is covered by bootstrap checksum");
    for(const auto bad:{0,1,2}) {
        Bytes plain(source_bytes_per_group(p,false));if(bad==0)plain[1]=0x80;if(bad==1)plain.back()=1;if(bad==2)std::fill(plain.begin(),plain.end(),0xff);
        auto wire=pristine;replacement=code_systematic(p,public_group(p,salt,0,plain));
        std::copy(replacement.begin(),replacement.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));
        invalid(wire,"public checksum success cannot bypass canonical endpoint/fill");
    }
    auto extra=pristine;replacement=code_systematic(p,public_group(p,salt,1,Bytes(source_bytes_per_group(p,false))));
    extra.insert(extra.end(),replacement.begin(),replacement.end());invalid(extra,"public extra padding supercycle is rejected");
    auto partial=pristine;partial.resize(partial.size()-physical_interval_bits);invalid(partial,"public partial final cycle cannot complete");
    auto bootstrap_only=pristine;bootstrap_only.resize(width);invalid(bootstrap_only,"public missing source cycle cannot complete");
    auto longwire=transmit(p,std::nullopt,Bytes(650,4));
    auto missing=longwire;missing.erase(missing.begin()+static_cast<std::ptrdiff_t>(width),missing.begin()+static_cast<std::ptrdiff_t>(width*2));
    invalid(missing,"public missing interior cycle cannot join neighboring source");
    auto final_missing=longwire;final_missing.resize(final_missing.size()-width);invalid(final_missing,"public lost final cycle cannot validate a prefix");
    auto reordered=longwire;std::swap_ranges(reordered.begin()+static_cast<std::ptrdiff_t>(width),reordered.begin()+static_cast<std::ptrdiff_t>(width*2),reordered.begin()+static_cast<std::ptrdiff_t>(width*2));
    invalid(reordered,"public group ordinal detects reordered cycles");
    const auto other=transmit(p,std::nullopt,{});auto spliced=pristine;
    std::copy(other.begin()+static_cast<std::ptrdiff_t>(width),other.end(),spliced.begin()+static_cast<std::ptrdiff_t>(width));
    invalid(spliced,"public checksum binds groups to their transfer salt");
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
    rx.finish(true);check(rx.snapshot().complete && Bytes(rx.result()->bytes().begin(),rx.result()->bytes().end())==source,"interleaver/RS recover one fully erased inner physical interval");
    check(rx.snapshot().erased_bytes>0,"RS erasure recovery is visible");
    Bytes raw(256);std::iota(raw.begin(),raw.end(),0);auto bits=coding::encode(raw,CodeRate::half);
    std::vector<float> noisy;for(std::size_t i=0;i<bits.size();++i)noisy.push_back((bits[i]?8.F:-8.F)*(i%211==0?-0.1F:1.F));
    check(coding::decode(noisy,raw.size(),CodeRate::half).bytes==raw,"soft Viterbi corrects sparse low-confidence errors");
}
void streamed(std::size_t total,bool encrypted=true) {
    auto p=profile(Channel::wire);p.interleave_depth=4;const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;std::size_t generated=0,max_request=0;
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
    check(rx.snapshot().spool_bytes<total*3+32768,"in-place memory usage bounded by local quota");
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
        independent_vectors();roundtrips();public_roundtrips();public_malformed();canonical_sources();burst_and_soft();
        streamed(argc>1?2*1024*1024:64*1024);streamed(argc>1?2*1024*1024:64*1024,false);
        std::cout<<"fast fixed-cadence crypto/FEC/source tests passed\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
