#include "datapump/fast/codec.hpp"
#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/outer_rs.hpp"
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
std::size_t coded_size(const Profile& p) {return p.capacity_mode?p.interleave_depth*p.ldpc_frame_bits:coding::encode(Bytes(p.interleave_depth*256),p.code_rate).size();}
Bytes systematic(const Profile& p,std::span<const std::uint8_t> cycle,std::uint64_t cycle_ordinal=0) {
    if(p.capacity_mode) {
        Bytes out;std::vector<float> soft(p.ldpc_frame_bits);
        const auto mask=fast::testing::capacity_whitening_mask(cycle.size(),cycle_ordinal);
        for(std::size_t block=0;block<p.interleave_depth;++block) {
            for(std::size_t col=0;col<soft.size();++col) {
                const auto at=col*p.interleave_depth+(block+fast::testing::capacity_interleave_rotation(p,col))%p.interleave_depth;
                soft[col]=(cycle[at]^mask[at])?12.F:-12.F;
            }
            auto decoded=ldpc::decode(ldpc::deinterleave(soft,p.ldpc_frame_bits),p.code_rate,ldpc::default_iterations,p.ldpc_frame_bits);
            check(decoded.converged,"fixture capacity codeword converges");out.insert(out.end(),decoded.bytes.begin(),decoded.bytes.end());
        }
        out.resize((out.size()&~std::size_t{1})-2*capacity_parity_symbols(p));return out;
    }
    std::vector<float> soft(coded_size(p));for(std::size_t i=0;i<soft.size();++i)soft[i]=cycle[i]?12.F:-12.F;
    auto de=coding::decode(soft,p.interleave_depth*256,p.code_rate);Bytes out;
    const auto rows=p.interleave_depth*2U,k=p.robust?112U:120U;
    for(unsigned row=0;row<rows;++row)for(unsigned column=0;column<k;++column)out.push_back(de.bytes[column*rows+row]);
    return out;
}
Bytes code_systematic(const Profile& p,std::span<const std::uint8_t> systematic,std::uint64_t cycle_ordinal=1) {
    if(p.capacity_mode) {
        auto rs=outer_rs::encode(systematic,capacity_parity_symbols(p));
        const auto k=ldpc::data_bits(p.code_rate,p.ldpc_frame_bits)/8;rs.resize(k*p.interleave_depth);Bytes wire(cycle_intervals(p)*physical_interval_bits);
        for(std::size_t block=0;block<p.interleave_depth;++block) {
            const auto bits=ldpc::interleave(ldpc::encode(std::span(rs).subspan(block*k,k),p.code_rate,p.ldpc_frame_bits),p.ldpc_frame_bits);
            for(std::size_t col=0;col<bits.size();++col)
                wire[col*p.interleave_depth+(block+fast::testing::capacity_interleave_rotation(p,col))%p.interleave_depth]=bits[col];
        }
        const auto mask=fast::testing::capacity_whitening_mask(wire.size(),cycle_ordinal);
        for(std::size_t i=0;i<wire.size();++i)wire[i]^=mask[i];
        return wire;
    }
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
    const std::string domain=p.capacity_mode?"DataPump/fast/capacity/v2/public/group":"DataPump/fast/v1/public/group";Bytes canonical(domain.begin(),domain.end());
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
    // Freeze the original local profile as well as its independent wire bytes.
    auto p=classic_profile(Channel::wire);p.constellation=16;p.code_rate=CodeRate::half;
    p.robust=true;p.interleave_depth=16;Bytes salt(32),iv(16),plain(176);
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
        auto p=classic_profile(Channel::wire);p.interleave_depth=1;
        auto a=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),71);
        auto b=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),71);
        auto c=fast::testing::deterministic_encoder(p,crypto,memory_source(Bytes{1,2,3}),72);
        std::array<std::uint8_t,physical_interval_bits> aa{},bb{},cc{};
        check(a.next_interval(aa)&&b.next_interval(bb)&&c.next_interval(cc),"deterministic test encoders emit intervals");
        check(aa==bb && aa!=cc,"regression entropy is repeatable and scoped per encoder");
    }
    for(auto rate:{CodeRate::half,CodeRate::three_quarters,CodeRate::seven_eighths})for(bool robust:{false,true}) {
        auto p=classic_profile(Channel::wire);p.code_rate=rate;p.robust=robust;p.interleave_depth=1;
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
    // Preserve the original robust-RS memory boundary independently of defaults.
    auto p=classic_profile(Channel::wire);p.robust=true;p.interleave_depth=1;const auto wire=transmit(p,crypto,Bytes(123,0));
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
        auto p=classic_profile(Channel::wire);p.code_rate=rate;p.robust=robust;p.interleave_depth=1;
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
    auto p=classic_profile(Channel::wire);p.robust=true;p.interleave_depth=1;const auto crypto=key();const auto public_wire=transmit(p,std::nullopt,utf8);
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
    // Keep the explicit source/checksum boundary offsets in the robust layout.
    auto p=classic_profile(Channel::wire);p.robust=true;p.interleave_depth=1;const auto width=cycle_intervals(p)*physical_interval_bits;
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
    const auto crypto=key();auto p=classic_profile(Channel::wire);p.interleave_depth=1;
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
    // This fixed burst fixture predates the cable throughput preset.
    auto p=classic_profile(Channel::wire);p.constellation=16;p.code_rate=CodeRate::three_quarters;
    p.robust=true;p.interleave_depth=16;const auto crypto=key();Bytes source(2100);
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
void capacity_rs() {
    // Verify the chosen polynomial's full primitive period independently of
    // the production logarithm tables, including every nonzero field element.
    std::array<bool,65536> seen{};unsigned field_value=1;
    for(unsigned i=0;i<65535;++i) {
        check(field_value && !seen[field_value],"GF65536 polynomial has no short multiplicative cycle");
        seen[field_value]=true;field_value<<=1;if(field_value&65536)field_value^=0x1100b;
    }
    check(field_value==1,"GF65536 primitive cycle closes after65535 elements");
    const std::array<std::string_view,2> masks{
        "10010110101111100111111000110010100001101001011000101101001101100011000010100010010010010101100010101110111010010010010010101110",
        "01000110011010010110101101111001110101110001000101011101010010011000100000110010001101111000111110100100100010110011000011010000"};
    for(std::size_t cycle=0;cycle<masks.size();++cycle) {
        const auto mask=fast::testing::capacity_whitening_mask(128,cycle);std::string text;
        for(auto bit:mask)text.push_back(static_cast<char>('0'+bit));
        check(text==masks[cycle],"independent public capacity whitening vector");
    }
    // Independent polynomial long division in GF(2^16), generated with direct
    // shift/XOR multiplication (not the production logarithm tables).
    const auto data=unhex("01023456789abcde");
    const auto expected=unhex("01023456789abcde1f7774cfd835429d");
    check(outer_rs::encode(data,4)==expected,"independent GF65536 shortened RS vector");
    auto pristine=expected;check(!outer_rs::correct(pristine,4),"valid RS vector has zero changes");
    auto damaged=expected;damaged[0]^=3;damaged[13]^=55;
    check(outer_rs::correct(damaged,4)==2 && damaged==expected,"GF65536 RS corrects two unknown symbols");
    damaged=expected;damaged[0]^=3;damaged[5]^=55;damaged[15]^=88;
    check(outer_rs::correct(damaged,4,std::array<std::size_t,2>{0,2})==3 && damaged==expected,"GF65536 mixed errors and erasures obey 2e+v budget");
    rejects([&]{outer_rs::correct(damaged,4,std::array<std::size_t,2>{0,0});},"duplicate RS erasures rejected");
    rejects([&]{outer_rs::correct(damaged,4,std::array<std::size_t,1>{8});},"out of range RS erasures rejected");
    rejects([&]{outer_rs::encode(Bytes(3),4);},"odd RS source bytes rejected");
    Bytes oversized(65536*2);
    rejects([&]{outer_rs::encode(oversized,4);},"oversized RS source rejected before unpacking");
    rejects([&]{outer_rs::correct(oversized,4);},"oversized RS word rejected before unpacking");
    rejects([&]{outer_rs::encode(data,256);},"RS parity beyond fixed scratch rejected");
    rejects([&]{outer_rs::correct(damaged,256);},"RS decoder parity beyond fixed scratch rejected");
    rejects([&]{outer_rs::encode(Bytes(65534*2),4);},"source and parity combined field bound checked");
    Bytes bulk(25124);std::mt19937 rng(81);for(auto& b:bulk)b=static_cast<std::uint8_t>(rng());
    const auto word=outer_rs::encode(bulk,38);damaged=word;
    for(std::size_t i=0;i<19;++i)damaged[i*1301]^=static_cast<std::uint8_t>(i+1);
    check(outer_rs::correct(damaged,38)==19 && damaged==word,"full capacity RS corrects nineteen symbol errors");
    damaged=word;
    for(std::size_t i=0;i<20;++i)damaged[i*1201]^=static_cast<std::uint8_t>(i+1);
    rejects([&]{outer_rs::correct(damaged,38);},"over-budget fixed RS fixture rejects");
    Bytes maximum_data((58320-176)*2);
    for(auto& b:maximum_data)b=static_cast<std::uint8_t>(rng());
    const auto maximum_word=outer_rs::encode(maximum_data,176);damaged=maximum_word;
    for(std::size_t i=0;i<88;++i)damaged[i*1301]^=static_cast<std::uint8_t>(i+1);
    check(outer_rs::correct(damaged,176)==88 && damaged==maximum_word,"maximum capacity RS geometry corrects88 symbol errors");
}
void capacity_interleaver_balance() {
    // QAM labels restart every2048 bits. Treating positions modulo only bps
    // misses this boundary and the former depth4/20-bit reliability alias.
    for(unsigned depth=1;depth<=16;++depth)for(unsigned bps=2;bps<=22;bps+=2) {
        auto p=capacity_profile();p.interleave_depth=depth;p.constellation=1U<<bps;
        std::array<std::array<unsigned,22>,16> counts{};std::array<unsigned,22> totals{};
        std::string_view fingerprint;
        if(depth==4&&bps==16)fingerprint="6f00534dca6961f8c7fee653d14b8fa1d0cfbf569efb722849ab609bf0bb91c6";
        if(depth==4&&bps==20)fingerprint="805c47eade3103b7e4337f20192ce4ec72c13986013f8885a4385938b4f76cf9";
        if(depth==3&&bps==14)fingerprint="284a6e6024ed6a9d8c50b25303d500267e19b8cb032c97aea1534e68be7d85d1";
        Bytes frozen;if(!fingerprint.empty())frozen.reserve(ldpc::coded_bits);
        for(std::size_t column=0;column<ldpc::coded_bits;++column) {
            const auto rotation=fast::testing::capacity_interleave_rotation(p,column);
            check(rotation<depth,"capacity column rotation bounded by local depth");
            if(!fingerprint.empty())frozen.push_back(static_cast<std::uint8_t>(rotation));
            std::array<bool,16> occupied{};
            for(unsigned frame=0;frame<depth;++frame) {
                const auto slot=(frame+rotation)%depth;
                check(!occupied[slot],"capacity rotated column is a bijection");occupied[slot]=true;
                const auto plane=((column*depth+slot)%physical_interval_bits)%bps;
                ++counts[frame][plane];++totals[plane];
            }
        }
        if(!fingerprint.empty())check(sha256(frozen)==unhex(fingerprint),"independent Python balanced-interleave schedule fingerprint");
        for(unsigned plane=0;plane<bps;++plane) {
            auto low=std::numeric_limits<unsigned>::max();unsigned high=0;
            for(unsigned frame=0;frame<depth;++frame) {
                low=std::min(low,counts[frame][plane]);high=std::max(high,counts[frame][plane]);
                const auto expected=double(totals[plane])/depth;
                check(std::abs(double(counts[frame][plane])-expected)<=expected*.002,"every LDPC frame spans every QAM plane within0.2 percent");
            }
            check(high-low<=5,"frozen balanced interleaver plane counts differ by at most five");
        }
        // Every D-bit column contains one bit per frame. Check partial-edge
        // bursts, including a modem interval boundary and a full2048-bit loss.
        for(auto start:{std::size_t{0},std::size_t{1},std::size_t{2047},std::size_t{70001}})
            for(auto length:{std::size_t{1},std::size_t{17},std::size_t{2048}}) {
                if(start+length>depth*ldpc::coded_bits)continue;
                std::array<unsigned,16> losses{};
                for(auto i=start;i<start+length;++i) {
                    const auto column=i/depth,slot=i%depth;
                    const auto rotation=fast::testing::capacity_interleave_rotation(p,column);
                    ++losses[(slot+depth-rotation)%depth];
                }
                for(unsigned frame=0;frame<depth;++frame)
                    check(losses[frame]<=(length+depth-1)/depth+1,"balanced frame interleaver preserves bounded burst spread");
            }
    }
}
void capacity_roundtrips() {
    const auto crypto=key();
    for(auto channel:{Channel::wire,Channel::acoustic_short})
    for(auto rate:{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters,CodeRate::seven_ninths,CodeRate::eight_ninths,CodeRate::nine_tenths})
    for(unsigned depth:{1U,4U})for(bool encrypted:{false,true}) {
        if(channel==Channel::wire&&depth!=1)continue;
        if(channel==Channel::acoustic_short&&rate!=CodeRate::half&&rate!=CodeRate::two_thirds&&rate!=CodeRate::three_quarters)continue;
        auto p=capacity_profile(channel);p.code_rate=rate;p.interleave_depth=depth;
        const auto c=capacity_source_bytes_per_cycle(p,encrypted);const auto width=cycle_intervals(p)*physical_interval_bits;
        for(auto size:{std::size_t{0},std::size_t{1},c-1,c,c+1}) {
            Bytes source(size);for(std::size_t i=0;i<size;++i)source[i]=static_cast<std::uint8_t>(i*17);
            if(size)source.back()=0;
            const auto keying=encrypted?std::optional<Crypto>(crypto):std::nullopt;
            const auto wire=transmit(p,keying,source);StreamDecoder rx(p,keying);feed(rx,wire);
            check(!rx.result() && !rx.snapshot().source_bytes && !rx.snapshot().physical_end,"capacity source stays opaque before physical absence");
            rx.finish(false);check(!rx.result() && !rx.snapshot().physical_end,"capacity EOF cannot complete");
            rx.finish(true);check(rx.snapshot().complete,"capacity boundary roundtrip complete");
            check(Bytes(rx.result()->bytes().begin(),rx.result()->bytes().end())==source,"capacity compact source preserves every byte and trailing zeros");
            check(rx.snapshot().authenticated==encrypted,"capacity public/keyed integrity claims distinct");
            check(wire.size()==(2+size/c)*width,"capacity exact-boundary mandatory final empty cycle");
            check(estimate_transmission(p,encrypted,size).intervals==wire.size()/physical_interval_bits,"capacity estimate equals actual fixed geometry");
        }
    }
    auto vector_profile=capacity_profile();vector_profile.interleave_depth=1;
    const auto vector_wire=transmit(vector_profile,std::nullopt,Bytes{0,0x80,0xff,0});
    const auto vector_width=cycle_intervals(vector_profile)*physical_interval_bits;
    const auto vector_area=systematic(vector_profile,std::span(vector_wire).subspan(vector_width,vector_width),1);
    const Bytes source_vector{1,0,0x80,0xff,0,0x80};
    check(std::equal(source_vector.begin(),source_vector.end(),vector_area.begin()),"independent capacity flag/eight-bit source/padding vector");
    check(std::all_of(vector_area.begin()+6,vector_area.end()-32,[](auto b){return !b;}),"capacity final source fill is canonical zeros");
    auto maximum=capacity_profile();maximum.code_rate=CodeRate::nine_tenths;maximum.interleave_depth=16;
    const auto max_wire=transmit(maximum,std::nullopt,{});StreamDecoder max_rx(maximum,std::nullopt);feed(max_rx,max_wire);max_rx.finish(true);
    check(max_rx.result() && !max_rx.result()->size() && capacity_parity_symbols(maximum)==176,"maximum bounded capacity geometry roundtrip");
    auto p=capacity_profile();p.code_rate=CodeRate::seven_ninths;
    const auto c=capacity_source_bytes_per_cycle(p,false);
    check(capacity_parity_symbols(p)==38 && c==25091,"four-frame7/9 RS/source geometry");
    const auto wire=transmit(p,std::nullopt,Bytes(c+3,0x80));StreamDecoder rx(p,std::nullopt);feed(rx,wire);rx.finish(true);
    check(rx.result() && rx.result()->size()==c+3,"four LDPC frames interleave and reassemble");
    check(rx.snapshot().ldpc_frames==12 && !rx.snapshot().ldpc_failed_frames && !rx.snapshot().ldpc_changed_bits,"clean capacity LDPC diagnostics counted independently of digest groups");
    const auto zero_wire=transmit(p,std::nullopt,Bytes(100000));
    const auto ones=std::count(zero_wire.begin(),zero_wire.end(),1);
    check(ones>static_cast<long>(zero_wire.size()*45/100) && ones<static_cast<long>(zero_wire.size()*55/100),"public all-zero source is whitened including bootstrap and final fill");
    StreamDecoder quota(p,std::nullopt,source_bytes_per_group(p,false)-1);feed(quota,wire);quota.finish(true);
    check(quota.snapshot().failed && !quota.result(),"capacity complete corrected source area obeys local memory quota");
    StreamDecoder wrong(p,key(1));feed(wrong,wire);wrong.finish(true);check(!wrong.result(),"capacity public and keyed bootstrap never autodetect");
}
void capacity_malformed(Channel channel=Channel::wire) {
    auto p=capacity_profile(channel);p.interleave_depth=1;const auto width=cycle_intervals(p)*physical_interval_bits;
    const auto pristine=transmit(p,std::nullopt,{});const auto boot=systematic(p,std::span(pristine).first(width));
    const auto salt=std::span(boot).first(32);const auto size=source_bytes_per_group(p,false);
    const auto invalid=[&](const Bytes& wire,const char* why) {
        StreamDecoder rx(p,std::nullopt);feed(rx,wire);rx.finish(true);check(rx.snapshot().failed && !rx.result(),why);
    };
    for(unsigned bad=0;bad<4;++bad) {
        Bytes area(size);area[0]=1;area[1]=0x80;
        if(bad==0)area[0]=2;
        if(bad==1)area[0]=0;
        if(bad==2)area[1]=0;
        if(bad==3)area.back()=7;
        auto wire=pristine;auto replacement=code_systematic(p,public_group(p,salt,0,area));
        std::copy(replacement.begin(),replacement.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));
        invalid(wire,"capacity checksummed bad final flag/padding rejected after physical end");
    }
    auto area=Bytes(size);area[0]=1;area[1]=0x80;
    auto group=public_group(p,salt,0,area);group.back()^=1;
    auto wire=pristine;auto replacement=code_systematic(p,group);
    std::copy(replacement.begin(),replacement.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));
    invalid(wire,"capacity digest remains mandatory after LDPC and RS success");
    const auto longwire=transmit(p,std::nullopt,Bytes(capacity_source_bytes_per_cycle(p,false)*2+7,0x80));
    wire=longwire;wire.resize(wire.size()-width);invalid(wire,"capacity missing whole final cycle cannot accept prefix");
    wire=longwire;wire.resize(wire.size()-physical_interval_bits);invalid(wire,"capacity partial final cycle cannot complete");
    wire=longwire;wire.erase(wire.begin()+static_cast<std::ptrdiff_t>(width),wire.begin()+static_cast<std::ptrdiff_t>(2*width));
    invalid(wire,"capacity missing interior cycle cannot join neighboring source");
    wire=longwire;std::swap_ranges(wire.begin()+static_cast<std::ptrdiff_t>(width),wire.begin()+static_cast<std::ptrdiff_t>(2*width),wire.begin()+static_cast<std::ptrdiff_t>(2*width));
    invalid(wire,"capacity ordinal detects reordered cycles");
    const auto other=transmit(p,std::nullopt,{});wire=pristine;
    std::copy(other.begin()+static_cast<std::ptrdiff_t>(width),other.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));
    invalid(wire,"capacity salt detects splicing from another transfer");
    wire=pristine;replacement=code_systematic(p,public_group(p,salt,1,area),2);wire.insert(wire.end(),replacement.begin(),replacement.end());
    invalid(wire,"capacity final flag cannot appear before last fixed cycle");
    const auto crypto=key();const auto keyed=transmit(p,crypto,{});StreamDecoder wrong(p,key(3));feed(wrong,keyed);wrong.finish(true);
    check(!wrong.result() && wrong.snapshot().failed,"capacity wrong key rejected");
    const auto keyedboot=systematic(p,std::span(keyed).first(width));const auto keyedsalt=std::span(keyedboot).first(32);
    Bytes keyedarea(ciphertext_bytes(p));keyedarea[0]=1;keyedarea[1]=0x80;keyedarea.back()=3;
    group=fast::testing::seal_group(p,crypto,keyedsalt,0,Bytes(16),keyedarea);replacement=code_systematic(p,group);wire=keyed;
    std::copy(replacement.begin(),replacement.end(),wire.begin()+static_cast<std::ptrdiff_t>(width));StreamDecoder malformed(p,crypto);feed(malformed,wire);
    check(!malformed.snapshot().failed,"capacity authenticated source syntax waits for physical end");
    malformed.finish(true);check(malformed.snapshot().failed && !malformed.result(),"capacity keyed malformed padding rejected after physical end");
    // A lost physical interval remains at its timed position; LDPC repairs the
    // erased evidence rather than removing it or shifting the source stream.
    auto four=capacity_profile();const Bytes source(1000,0x5a);const auto good=transmit(four,std::nullopt,source);
    StreamDecoder recovered(four,std::nullopt);std::array<float,physical_interval_bits> soft{};
    const auto erased=cycle_intervals(four)+3;
    for(std::size_t interval=0;interval<good.size()/physical_interval_bits;++interval) {
        for(std::size_t bit=0;bit<soft.size();++bit)soft[bit]=interval==erased?0.F:(good[interval*physical_interval_bits+bit]?12.F:-12.F);
        recovered.push_interval(soft);
    }
    recovered.finish(true);check(recovered.result() && Bytes(recovered.result()->bytes().begin(),recovered.result()->bytes().end())==source,"capacity LDPC/frame interleaver repairs a timed interval erasure");
}
void continue_after_damaged_cycle() {
    // A timed damaged cycle must occupy its original logical position. The
    // following groups keep their original integrity ordinals and whitening;
    // no retransmission, ordinal search, or source interpretation is involved.
    for(auto channel:{Channel::wire,Channel::acoustic,Channel::acoustic_short})
    for(bool encrypted:{false,true})for(unsigned missing:{1U,2U}) {
        const bool capacity=channel!=Channel::wire;
        auto p=capacity?capacity_profile(channel):classic_profile(Channel::wire);
        p.interleave_depth=1;p.code_rate=CodeRate::three_quarters;
        const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;
        const auto width=cycle_intervals(p)*physical_interval_bits;
        const auto area_size=source_bytes_per_group(p,encrypted);
        const auto payload=capacity?capacity_source_bytes_per_cycle(p,encrypted):area_size*8/9;
        Bytes source((missing+2)*payload+7);
        for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<std::uint8_t>(i*73+(i/payload)*19+11);
        source.back()=0;
        const auto wire=transmit(p,crypto,source);
        const auto source_cycles=missing+3U;
        check(wire.size()==(source_cycles+1)*width,"gap fixture includes bootstrap, good prefix, holes, later good area, and final");
        const auto boot=systematic(p,std::span(wire).first(width));const auto salt=std::span(boot).first(32);
        std::vector<Bytes> areas;Bytes damaged=wire;
        for(unsigned ordinal=0;ordinal<source_cycles;++ordinal) {
            auto group=systematic(p,std::span(wire).subspan((ordinal+1)*width,width),ordinal+1);
            areas.push_back(crypto?fast::testing::open_group(p,*crypto,salt,ordinal,group):Bytes(group.begin(),group.end()-32));
            if(ordinal>=1 && ordinal<=missing) {
                // Regenerate valid FEC around an invalid full digest/HMAC so
                // this regression cannot pass merely because FEC repaired it.
                group.back()^=1;
                const auto replacement=code_systematic(p,group,ordinal+1);
                std::copy(replacement.begin(),replacement.end(),damaged.begin()+static_cast<std::ptrdiff_t>((ordinal+1)*width));
            }
        }
        StreamDecoder rx(p,crypto);feed(rx,std::span(damaged).first(2*width));
        check(!rx.snapshot().failed && rx.snapshot().coding_cycles==2 && rx.snapshot().verified_bytes==area_size,
            "good source prefix is verified before the disturbance");
        for(unsigned ordinal=1;ordinal<source_cycles;++ordinal) {
            feed(rx,std::span(damaged).subspan((ordinal+1)*width,width));
            const auto snapshot=rx.snapshot();const auto gaps=std::min(ordinal,missing);
            const auto good=ordinal+1-gaps;
            check(snapshot.failed && !snapshot.decoding_stopped && snapshot.failed_cycles==gaps && snapshot.coding_cycles==ordinal+2,
                "integrity gap marks incomplete data while later fixed cycles remain decodable");
            check(snapshot.spool_bytes==(ordinal+1)*area_size && snapshot.verified_bytes==good*area_size,
                "verified areas and missing areas retain bounded distinct byte accounting");
            check((encrypted?snapshot.authenticated_groups:snapshot.checksum_groups)==good &&
                  (encrypted?snapshot.checksum_groups:snapshot.authenticated_groups)==0,
                "later groups continue independent keyed or public integrity verification");
            check(!snapshot.physical_end && !snapshot.complete && !snapshot.authenticated && !snapshot.source_bytes && !rx.result(),
                "neither verified later cycles nor a protected final flag expose incomplete source");
            check(!fast::testing::retained_source_area(rx,ordinal),"verified raw areas remain opaque before physical absence");
            rx.finish(false);
            check(!rx.snapshot().physical_end && !rx.snapshot().decoding_stopped && !rx.result(),
                "EOF after a gap cannot stop subsequent decoding or manufacture physical end");
        }
        if(capacity)check(rx.snapshot().ldpc_frames==source_cycles+1 && !rx.snapshot().ldpc_failed_frames,
            "every capacity frame after an integrity failure was decoded with clean FEC");
        rx.finish(true);
        check(rx.snapshot().physical_end && rx.snapshot().failed && !rx.snapshot().complete && !rx.snapshot().authenticated &&
              !rx.snapshot().source_bytes && !rx.result(),"missing source remains incomplete at actual physical end");
        for(unsigned ordinal=0;ordinal<source_cycles;++ordinal) {
            const auto retained=fast::testing::retained_source_area(rx,ordinal);
            if(ordinal>=1 && ordinal<=missing)check(!retained,"damaged logical source area is an explicit hole");
            else check(retained && *retained==areas[ordinal],"later verified source area remains exact at its original ordinal");
        }
        check(!fast::testing::retained_source_area(rx,source_cycles),"retention hook rejects an unreceived logical position");
    }

    // Classic cycles contain several independently protected groups. A bad
    // group must not suppress its later intact neighbors within that cycle.
    for(bool encrypted:{false,true}) {
        auto p=classic_profile(Channel::wire);p.interleave_depth=4;p.code_rate=CodeRate::three_quarters;
        const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;
        const auto width=cycle_intervals(p)*physical_interval_bits;
        const auto area_size=source_bytes_per_group(p,encrypted);
        Bytes source(area_size*p.interleave_depth*8/9+7);
        for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<std::uint8_t>(i*37+9);
        const auto wire=transmit(p,crypto,source);check(wire.size()==3*width,"classic fixture spans two source cycles");
        const auto boot=systematic(p,std::span(wire).first(width));const auto salt=std::span(boot).first(32);
        auto first=systematic(p,std::span(wire).subspan(width,width));
        const auto last=systematic(p,std::span(wire).last(width));
        const auto k=first.size()/p.interleave_depth;std::vector<Bytes> areas;
        for(unsigned ordinal=0;ordinal<8;++ordinal) {
            const auto group=std::span(ordinal<4?first:last).subspan((ordinal%4)*k,k);
            areas.push_back(crypto?fast::testing::open_group(p,*crypto,salt,ordinal,group):Bytes(group.begin(),group.end()-32));
        }
        first[2*k-1]^=1;const auto damaged=code_systematic(p,first);
        StreamDecoder rx(p,crypto);feed(rx,std::span(wire).first(width));feed(rx,damaged);
        check(rx.snapshot().failed && !rx.snapshot().decoding_stopped && rx.snapshot().failed_cycles==1 &&
              rx.snapshot().verified_bytes==3*area_size && rx.snapshot().spool_bytes==4*area_size,
            "classic integrity failure retains good groups on both sides within the same cycle");
        feed(rx,std::span(wire).last(width));
        check(rx.snapshot().coding_cycles==3 && rx.snapshot().failed_cycles==1 &&
              (encrypted?rx.snapshot().authenticated_groups:rx.snapshot().checksum_groups)==7,
            "classic group ordinal advances across one hole and into the next coding cycle");
        rx.finish(true);check(!rx.result() && !rx.snapshot().complete,"classic hole cannot become apparently contiguous source");
        for(unsigned ordinal=0;ordinal<8;++ordinal) {
            const auto retained=fast::testing::retained_source_area(rx,ordinal);
            if(ordinal==1)check(!retained,"classic damaged group is explicitly absent");
            else check(retained && *retained==areas[ordinal],"classic verified group retains its original bytes and address");
        }
    }

    auto p=capacity_profile(Channel::acoustic);p.interleave_depth=1;p.code_rate=CodeRate::three_quarters;
    const auto width=cycle_intervals(p)*physical_interval_bits;
    const auto area_size=source_bytes_per_group(p,false),payload=capacity_source_bytes_per_cycle(p,false);
    const auto wire=transmit(p,std::nullopt,Bytes(payload*3+7,0x5a));
    const auto push_erased_cycle=[&](StreamDecoder& rx) {
        const std::array<float,physical_interval_bits> erased{};
        for(std::size_t i=0;i<cycle_intervals(p);++i)rx.push_interval(erased);
    };
    StreamDecoder erased(p,std::nullopt);feed(erased,std::span(wire).first(2*width));push_erased_cycle(erased);
    check(erased.snapshot().failed && !erased.snapshot().decoding_stopped && erased.snapshot().failed_cycles==1,
        "a wholly erased timed cycle leaves a recoverable decoder state");
    feed(erased,std::span(wire).subspan(3*width));
    check(erased.snapshot().ldpc_frames==5 && erased.snapshot().checksum_groups==3 && erased.snapshot().verified_bytes==3*area_size,
        "actual missing likelihoods do not suppress decoding and checksums after the disturbance");
    erased.finish(true);
    auto final=systematic(p,std::span(wire).last(width),4);final.resize(area_size);
    check(!erased.result() && !fast::testing::retained_source_area(erased,1) &&
          fast::testing::retained_source_area(erased,3)==std::optional<Bytes>(final),
        "erased cycle cannot shift the independently verified final area's logical address");

    // Missing areas consume local logical-space quota just as good areas do;
    // otherwise an all-bad stream could grow its hole bookkeeping forever.
    StreamDecoder bounded(p,std::nullopt,2*area_size);feed(bounded,std::span(wire).first(width));
    push_erased_cycle(bounded);push_erased_cycle(bounded);
    check(!bounded.snapshot().decoding_stopped && bounded.snapshot().failed_cycles==2 &&
          bounded.snapshot().spool_bytes==2*area_size && !bounded.snapshot().verified_bytes,
        "consecutive wholly missing cycles consume the exact configured source quota");
    push_erased_cycle(bounded);
    check(bounded.snapshot().failed && bounded.snapshot().decoding_stopped && bounded.snapshot().spool_bytes<=2*area_size,
        "next missing cycle cannot overrun the bounded logical source quota");
    const auto stopped=bounded.snapshot();for(unsigned i=0;i<4;++i)push_erased_cycle(bounded);
    check(bounded.snapshot().coding_cycles==stopped.coding_cycles && bounded.snapshot().ldpc_frames==stopped.ldpc_frames &&
          bounded.snapshot().intervals==stopped.intervals+4*cycle_intervals(p),
        "fatal local quota stops decoder work while physical interval accounting continues");
    bounded.finish(false);check(!bounded.snapshot().physical_end,"quota failure is not physical completion");
    bounded.finish(true);check(!bounded.result(),"all missing source cannot produce a completed result");

    // Bootstrap damage cannot be treated as a later data hole: no established
    // salt or key context exists, and later bytes must never trigger re-keying.
    auto boot=systematic(p,std::span(wire).first(width));boot[32]^=1;
    const auto invalid_boot=code_systematic(p,boot,0);
    StreamDecoder bootstrap(p,std::nullopt);feed(bootstrap,invalid_boot);
    check(bootstrap.snapshot().failed && bootstrap.snapshot().decoding_stopped &&
          bootstrap.snapshot().failed_cycles==1 && bootstrap.snapshot().coding_cycles==1,
        "invalid bootstrap remains a fatal integrity-context failure");
    feed(bootstrap,wire);
    check(bootstrap.snapshot().coding_cycles==1 && bootstrap.snapshot().ldpc_frames==1 &&
          !bootstrap.snapshot().checksum_groups && !bootstrap.snapshot().verified_bytes,
        "later valid bootstrap cannot restart a failed reception under a fresh context");
    bootstrap.finish(true);check(!bootstrap.result() && !fast::testing::retained_source_area(bootstrap,0),
        "fatal bootstrap leaves no eligible retained source");

    StreamDecoder nonfinite(p,std::nullopt);feed(nonfinite,std::span(wire).first(2*width));
    std::array<float,physical_interval_bits> nan{};nan.fill(std::numeric_limits<float>::quiet_NaN());
    for(std::size_t i=0;i<cycle_intervals(p);++i)nonfinite.push_interval(nan);
    check(nonfinite.snapshot().failed && nonfinite.snapshot().decoding_stopped && !nonfinite.snapshot().physical_end,
        "nonfinite data evidence remains a fatal input fault after a valid bootstrap");
    const auto before=nonfinite.snapshot();feed(nonfinite,std::span(wire).subspan(3*width));
    check(nonfinite.snapshot().coding_cycles==before.coding_cycles && nonfinite.snapshot().ldpc_frames==before.ldpc_frames,
        "invalid numeric evidence cannot enable further decoder work");
    nonfinite.finish(true);check(!nonfinite.result(),"fatal numeric input cannot yield a completed file");
}
void capacity_acoustic_contracts() {
    // The codec sees fixed 2048-bit intervals behind either SC or OFDM. Use
    // identical local SC geometry for both channel identities to isolate the
    // integrity domain from any future acoustic waveform/default selection.
    for(auto rate:{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters})for(unsigned depth:{1U,4U})for(bool encrypted:{false,true}) {
        auto p=capacity_profile();p.channel=Channel::acoustic;p.constellation=16;
        p.code_rate=rate;p.interleave_depth=depth;
        const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;
        const auto width=cycle_intervals(p)*physical_interval_bits;
        const auto slots=capacity_source_bytes_per_cycle(p,encrypted);
        Bytes source(slots);for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<std::uint8_t>(73*i+11);
        source[source.size()-2]=0x80;source.back()=0;
        const auto wire=transmit(p,crypto,source);
        check(wire.size()==3*width,"acoustic exact source boundary requires bootstrap, continuation and final cycles");
        const auto boot=systematic(p,std::span(wire).first(width));
        const auto salt=std::span(boot).first(32);
        const auto body=[&](std::size_t cycle) {
            const auto group=systematic(p,std::span(wire).subspan(cycle*width,width),cycle);
            if(crypto)return fast::testing::open_group(p,*crypto,salt,cycle-1,group);
            return Bytes(group.begin(),group.end()-32);
        };
        const auto continuation=body(1),final=body(2);
        check(continuation[0]==0 && std::equal(source.begin(),source.end(),continuation.begin()+1),
            "acoustic continuation flag protects exact arbitrary source bytes");
        check(final[0]==1 && final[1]==0x80 && std::all_of(final.begin()+2,final.end(),[](auto b){return !b;}),
            "acoustic final empty cycle has independent flag/delimiter/zero-fill layout");
        StreamDecoder received(p,crypto);feed(received,wire);
        check(!received.snapshot().failed && !received.snapshot().physical_end &&
              !received.snapshot().complete && !received.snapshot().source_bytes && !received.result(),
            "acoustic valid protected final cycle cannot expose source or physical completion");
        received.finish(false);
        check(!received.snapshot().failed && !received.snapshot().physical_end && !received.result(),
            "acoustic codec EOF cannot substitute for scored physical absence");
        received.finish(true);
        check(received.snapshot().complete && received.snapshot().authenticated==encrypted &&
              Bytes(received.result()->bytes().begin(),received.result()->bytes().end())==source,
            "acoustic source becomes available only after explicit physical completion");
        check(received.snapshot().ldpc_frames==3*depth && !received.snapshot().ldpc_failed_frames,
            "acoustic half and three-quarter LDPC depth geometry");
        const auto seal_final=[&](const Bytes& area) {
            return crypto?fast::testing::seal_group(p,*crypto,salt,1,Bytes(16),area):public_group(p,salt,1,area);
        };
        for(unsigned defect=0;defect<4;++defect) {
            auto area=final;
            if(defect==0)area[0]=2;
            if(defect==1)area[0]=0;
            if(defect==2)area[1]=0;
            if(defect==3)area.back()=7;
            const auto replacement=code_systematic(p,seal_final(area),2);
            StreamDecoder malformed(p,crypto);feed(malformed,std::span(wire).first(2*width));feed(malformed,replacement);
            check(!malformed.snapshot().failed && !malformed.result(),
                "acoustic integrity-valid source syntax remains deferred");
            malformed.finish(false);
            check(!malformed.snapshot().failed && !malformed.result(),"acoustic malformed padding is not interpreted at EOF");
            malformed.finish(true);
            check(malformed.snapshot().failed && !malformed.snapshot().complete && !malformed.result(),
                "acoustic malformed final flag or padding rejected at physical end");
        }
        auto corrupted_group=seal_final(final);corrupted_group.back()^=1;
        const auto corrupted=code_systematic(p,corrupted_group,2);
        StreamDecoder bad_integrity(p,crypto);feed(bad_integrity,std::span(wire).first(2*width));feed(bad_integrity,corrupted);
        check(bad_integrity.snapshot().failed && !bad_integrity.snapshot().physical_end && !bad_integrity.result(),
            "acoustic LDPC/RS success cannot bypass corrupted full integrity check");
        feed(bad_integrity,std::span(wire).last(width));bad_integrity.finish(true);
        check(bad_integrity.snapshot().failed && !bad_integrity.result(),"acoustic integrity failure keeps whole-file completion unavailable");
        StreamDecoder truncated(p,crypto);feed(truncated,std::span(wire).first(2*width));truncated.finish(true);
        check(truncated.snapshot().failed && !truncated.result(),"acoustic lost final cycle cannot complete a protected prefix");
        auto wrong_channel=p;wrong_channel.channel=Channel::wire;
        check(profile_id(wrong_channel)!=profile_id(p),"acoustic and cable channel domains differ at identical geometry");
        StreamDecoder mismatch(wrong_channel,crypto);feed(mismatch,std::span(wire).first(width));
        check(mismatch.snapshot().failed && !mismatch.result(),"acoustic bootstrap cannot be accepted under cable identity");
    }
}
void capacity_ofdm_context() {
    auto cable=capacity_profile(),unused=cable;
    unused.ofdm_fft_size=4096;unused.ofdm_prefix_samples=2048;
    unused.ofdm_pilot_stride=16;
    unused.ofdm_low_hz=1000;unused.ofdm_high_hz=12000;
    check(profile_id(cable)==profile_id(unused),"unused OFDM settings cannot alter cable wire identity");
    for(bool encrypted:{false,true}) {
        const auto p=capacity_profile(Channel::acoustic);
        check(p.acoustic_ofdm,"explicit acoustic capacity selects OFDM");
        const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;
        const Bytes source{0,0x80,0xff,0};const auto wire=transmit(p,crypto,source);
        const auto width=cycle_intervals(p)*physical_interval_bits;
        StreamDecoder good(p,crypto);feed(good,wire);good.finish(false);
        check(!good.result() && !good.snapshot().physical_end,"OFDM codec context does not complete at source EOF");
        good.finish(true);
        check(good.snapshot().complete && Bytes(good.result()->bytes().begin(),good.result()->bytes().end())==source,
            "OFDM profile retains compact source and physical-end contract");
        std::array<Profile,6> mismatch{p,p,p,p,p,p};
        mismatch[0].acoustic_ofdm=false;
        mismatch[1].ofdm_prefix_samples=p.ofdm_prefix_samples==256?512:p.ofdm_prefix_samples/2;
        mismatch[2].ofdm_fft_size=p.ofdm_fft_size==32768?16384:2*p.ofdm_fft_size;
        mismatch[3].ofdm_low_hz+=125;
        mismatch[4].ofdm_high_hz-=125;
        mismatch[5].ofdm_pilot_stride=p.ofdm_pilot_stride==8?16:8;
        for(const auto& changed:mismatch) {
            check(profile_id(changed)!=profile_id(p),"OFDM waveform geometry is integrity-bound");
            StreamDecoder wrong(changed,crypto);feed(wrong,std::span(wire).first(width));
            check(wrong.snapshot().failed && !wrong.result(),"OFDM bootstrap rejects mismatched local waveform geometry");
        }
    }
}
void capacity_parallel_decode() {
    // Independent serial orchestration of the same public FEC primitives. The
    // receiver must retain this ordering even when later workers finish first.
    const auto serial=[](const Profile& p,std::span<const std::uint8_t> rotations,std::span<const float> wire,std::uint64_t ordinal,DecodeSnapshot& stats) {
        const auto mask=fast::testing::capacity_whitening_mask(wire.size(),ordinal);
        Bytes decoded;std::vector<float> soft(ldpc::coded_bits);
        for(unsigned frame=0;frame<p.interleave_depth;++frame) {
            for(std::size_t column=0;column<soft.size();++column) {
                const auto position=column*p.interleave_depth+
                    (frame+rotations[column])%p.interleave_depth;
                soft[column]=mask[position]?-wire[position]:wire[position];
            }
            const auto input=ldpc::deinterleave(soft);
            const auto result=ldpc::decode(input,p.code_rate);
            ++stats.ldpc_frames;stats.ldpc_iterations+=result.iterations;
            if(!result.converged)++stats.ldpc_failed_frames;
            for(std::size_t bit=0;bit<result.bytes.size()*8;++bit)
                if(input[bit]!=0 && ((input[bit]>0)!=bool((result.bytes[bit/8]>>(7-bit%8))&1U)))++stats.ldpc_changed_bits;
            decoded.insert(decoded.end(),result.bytes.begin(),result.bytes.end());
        }
        if(decoded.size()%2) {
            if(decoded.back())throw Error("Noncanonical capacity LDPC alignment fill");
            decoded.pop_back();
        }
        const auto before=decoded;
        (void)outer_rs::correct(decoded,capacity_parity_symbols(p));
        for(std::size_t i=0;i<decoded.size();++i)if(decoded[i]!=before[i])++stats.corrected_bytes;
        decoded.resize(decoded.size()-2*capacity_parity_symbols(p));return decoded;
    };
    for(unsigned scenario=0;scenario<5;++scenario) {
        auto p=capacity_profile(Channel::acoustic);p.constellation=64;
        p.code_rate=CodeRate::three_quarters;p.interleave_depth=scenario==4?16:4;
        const Bytes source{0,0x80,0xff,9,0};const auto crypto=key();
        auto encoder=fast::testing::deterministic_encoder(p,crypto,byte_source(source),0x706172616c6c656cULL);
        Bytes wire;std::array<std::uint8_t,physical_interval_bits> interval{};
        while(encoder.next_interval(interval))wire.insert(wire.end(),interval.begin(),interval.end());
        Bytes rotations(ldpc::coded_bits);
        for(std::size_t i=0;i<rotations.size();++i)rotations[i]=static_cast<std::uint8_t>(fast::testing::capacity_interleave_rotation(p,i));
        const auto width=cycle_intervals(p)*physical_interval_bits;
        std::vector<float> soft(wire.size());std::mt19937 rng(0x6c647063U+scenario);
        std::normal_distribution<float> noise(0.F,1.F);
        for(std::size_t i=0;i<soft.size();++i) {
            const auto sign=wire[i]?1.F:-1.F;
            soft[i]=scenario==1?2.F*(sign+.45F*noise(rng))/(.45F*.45F):12.F*sign;
            if(scenario==2 && i<width)soft[i]=noise(rng)*.25F;
        }
        if(scenario==3) {
            // Fail frame two while earlier frames are valid. A later worker's
            // failure may finish first but cannot change the retained prefix.
            const auto at=[&](unsigned frame,std::size_t column) {
                return column*p.interleave_depth+(frame+fast::testing::capacity_interleave_rotation(p,column))%p.interleave_depth;
            };
            soft[at(2,10)]=std::numeric_limits<float>::quiet_NaN();
            soft[at(3,0)]=std::numeric_limits<float>::infinity();
        }
        DecodeSnapshot expected;std::string error;double serial_ms=0,scheduled_ms=0;
        StreamDecoder rx(p,crypto);
        for(std::size_t cycle=0;cycle<wire.size()/width;++cycle) {
            const auto clean_systematic=systematic(p,std::span(wire).subspan(cycle*width,width),cycle);
            auto start=std::chrono::steady_clock::now();
            try {
                const auto decoded=serial(p,rotations,std::span(soft).subspan(cycle*width,width),cycle,expected);
                check(decoded==clean_systematic,
                    "serial acoustic FEC recovers exact clean/noisy coding area");
            }catch(const Error& e) {error=e.what();}
            serial_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            start=std::chrono::steady_clock::now();
            for(std::size_t i=cycle*width;i<(cycle+1)*width;i+=physical_interval_bits)
                rx.push_interval(std::span(soft).subspan(i,physical_interval_bits));
            scheduled_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto actual=rx.snapshot();
            check(actual.ldpc_frames==expected.ldpc_frames && actual.ldpc_failed_frames==expected.ldpc_failed_frames &&
                actual.ldpc_iterations==expected.ldpc_iterations && actual.ldpc_changed_bits==expected.ldpc_changed_bits &&
                actual.corrected_bytes==expected.corrected_bytes,"parallel acoustic FEC diagnostics equal serial frame order");
            if(!error.empty()) {
                check(actual.failed && actual.status==error && !rx.result(),"parallel acoustic worker/RS exception propagates and fails closed");
                break;
            }
            check(!actual.failed && !rx.result(),"parallel acoustic coding success still awaits physical completion");
        }
        if(scenario==2)check(!error.empty() && expected.ldpc_failed_frames==4 && expected.ldpc_iterations==4*ldpc::default_iterations,
            "fully corrupt acoustic cycle exercises four bounded maximum-iteration workers");
        if(scenario==3)check(error=="Nonfinite Fast LDPC likelihood" && expected.ldpc_frames==2,
            "parallel worker exceptions retain only the serial preceding frame statistics");
        rx.finish(true);
        if(scenario<2 || scenario==4)check(error.empty() && rx.result() &&
            Bytes(rx.result()->bytes().begin(),rx.result()->bytes().end())==source,"parallel acoustic output is exact at physical end");
        else check(rx.snapshot().failed && !rx.result(),"parallel acoustic decode failure remains terminal at physical end");
        std::cout<<"acoustic LDPC scheduling scenario="<<scenario<<" depth="<<p.interleave_depth
            <<" serial_ms="<<serial_ms<<" scheduled_ms="<<scheduled_ms<<" frames="<<expected.ldpc_frames
            <<" iterations="<<expected.ldpc_iterations<<" failed_frames="<<expected.ldpc_failed_frames<<'\n';
    }
}
void streamed(std::size_t total,bool encrypted=true,bool capacity=false) {
    auto p=capacity?capacity_profile():classic_profile(Channel::wire);p.interleave_depth=4;const auto crypto=encrypted?std::optional<Crypto>(key()):std::nullopt;std::size_t generated=0,max_request=0;
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
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string_view(argv[1])=="--parallel") {capacity_parallel_decode();return 0;}
        if(argc>1 && std::string_view(argv[1])=="--continuation") {
            continue_after_damaged_cycle();std::cout<<"fast damaged-cycle continuation tests passed\n";return 0;
        }
        independent_vectors();roundtrips();public_roundtrips();public_malformed();canonical_sources();burst_and_soft();
        capacity_rs();capacity_interleaver_balance();capacity_roundtrips();capacity_malformed();capacity_malformed(Channel::acoustic_short);continue_after_damaged_cycle();capacity_acoustic_contracts();capacity_ofdm_context();capacity_parallel_decode();
        streamed(argc>1?2*1024*1024:100*1024);streamed(argc>1?2*1024*1024:100*1024,false);
        streamed(1024*1024,true,true);streamed(1024*1024,false,true);
        std::cout<<"fast fixed-cadence crypto/FEC/source tests passed\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
