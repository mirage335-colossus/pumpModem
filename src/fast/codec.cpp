#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/outer_rs.hpp"
#include "datapump/stream_codec.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

namespace datapump::fast {
namespace {
// Only corrupted received data may leave a recoverable hole. Allocation,
// cryptographic-library and geometry errors remain fatal to this decoder.
struct CorruptCycle : Error { using Error::Error; };
struct CloseFile { void operator()(std::FILE* f)const {if(f)std::fclose(f);} };
using File = std::unique_ptr<std::FILE, CloseFile>;
void write_file(std::FILE* f,std::span<const std::uint8_t> data) {
    if(std::fwrite(data.data(),1,data.size(),f)!=data.size()) throw Error("Fast destination write failed");
}
void append(Bytes& a,std::span<const std::uint8_t> b) { a.insert(a.end(),b.begin(),b.end()); }
Bytes label(const char* text) {
    const auto* p=reinterpret_cast<const std::uint8_t*>(text);
    return Bytes(p,p+std::char_traits<char>::length(text));
}
void ordinal_bytes(Bytes& bytes,std::uint64_t ordinal) {
    for(unsigned i=0;i<8;++i)bytes.push_back(static_cast<std::uint8_t>(ordinal>>(56-8*i)));
}
Bytes hmac(std::span<const std::uint8_t> key,std::span<const std::uint8_t> input) {
    Bytes output(32);unsigned count=0;
    if(!HMAC(EVP_sha256(),key.data(),static_cast<int>(key.size()),input.data(),input.size(),output.data(),&count) || count!=32)
        throw Error("Fast HMAC failed");
    return output;
}
Bytes checksum(std::span<const std::uint8_t> input) {
    Bytes output(32);unsigned count=0;
    if(EVP_Digest(input.data(),input.size(),output.data(),&count,EVP_sha256(),nullptr)!=1 || count!=32)
        throw Error("Fast SHA-256 checksum failed");
    return output;
}
bool same(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b) {
    return a.size()==b.size() && CRYPTO_memcmp(a.data(),b.data(),a.size())==0;
}
Bytes random_bytes(std::size_t size) {
    Bytes value(size);
    if(RAND_bytes(value.data(),static_cast<int>(size))!=1)throw Error("Fast random generation failed");
    return value;
}
void validate_codec_profile(const Profile& p) {
    if(p.compact_convolutional||small_ldpc_frame(p.ldpc_frame_bits)) {validate(p);return;}
    if(p.capacity_mode) {
        if(p.interleave_depth<1 || p.interleave_depth>16)throw Error("Capacity interleave depth must be 1 through 16 LDPC frames");
        (void)ldpc::data_bits(p.code_rate,p.ldpc_frame_bits);return;
    }
    if(p.interleave_depth<1 || p.interleave_depth>64)throw Error("Fast interleave depth must be 1 through 64");
    switch(p.code_rate) {
    case CodeRate::half: case CodeRate::three_quarters: case CodeRate::seven_eighths: break;
    default: throw Error("Unknown fast code rate");
    }
}
std::span<const std::uint8_t> puncture(CodeRate rate) {
    static constexpr std::array<std::uint8_t,2> half{1,1};
    static constexpr std::array<std::uint8_t,6> three{1,1,1,0,0,1};
    static constexpr std::array<std::uint8_t,14> seven{1,1,1,0,1,0,1,0,0,1,0,1,0,1};
    switch(rate) {
    case CodeRate::half:return half;
    case CodeRate::three_quarters:return three;
    case CodeRate::seven_eighths:return seven;
    default: break;
    }
    throw Error("Unknown fast code rate");
}
std::size_t inner_size(std::size_t source_bytes,CodeRate rate) {
    const auto mask=puncture(rate);const auto raw=(source_bytes*8+6)*2;
    const auto whole=raw/mask.size()*static_cast<std::size_t>(std::count(mask.begin(),mask.end(),1));
    return whole+static_cast<std::size_t>(std::count(mask.begin(),mask.begin()+static_cast<std::ptrdiff_t>(raw%mask.size()),1));
}
std::size_t k_bytes(const Profile& p) { return p.robust?112:120; }
std::size_t capacity_info_bytes(const Profile& p) {
    if(p.compact_convolutional)return p.interleave_depth*(p.code_rate==CodeRate::half?126:190);
    return p.interleave_depth*(ldpc::data_bits(p.code_rate,p.ldpc_frame_bits)/8);
}
std::size_t capacity_data_bytes(const Profile& p) {return (capacity_info_bytes(p)&~std::size_t{1})-2*capacity_parity_symbols(p);}
std::size_t systematic_bytes(const Profile& p) {return p.capacity_mode?capacity_data_bytes(p):k_bytes(p)*2;}
const char* domain(const Profile& p,const char* legacy,const char* capacity) {return p.capacity_mode?capacity:legacy;}
struct Keys {
    Bytes encryption,authentication;
    Keys(const Profile& p,const Crypto& crypto,std::span<const std::uint8_t> salt) {
        if(salt.size()!=32)throw Error("Invalid fixed fast salt width");
        auto base=crypto.mac(label(domain(p,"DataPump/fast/v1/root","DataPump/fast/capacity/v2/root")));
        auto prk=hmac(salt,base);OPENSSL_cleanse(base.data(),base.size());
        auto context=profile_id(p);
        auto info=label(domain(p,"DataPump/fast/v1/AES-256-CBC","DataPump/fast/capacity/v2/AES-256-CBC"));append(info,context);info.push_back(1);
        encryption=hmac(prk,info);
        info=label(domain(p,"DataPump/fast/v1/HMAC-SHA256","DataPump/fast/capacity/v2/HMAC-SHA256"));append(info,context);info.push_back(1);
        authentication=hmac(prk,info);OPENSSL_cleanse(prk.data(),prk.size());
    }
    ~Keys() {OPENSSL_cleanse(encryption.data(),encryption.size());OPENSSL_cleanse(authentication.data(),authentication.size());}
};
Bytes crypt(std::span<const std::uint8_t> input,const Bytes& key,std::span<const std::uint8_t> iv,bool encrypt) {
    if(iv.size()!=16 || input.empty() || input.size()%16)throw Error("Invalid fixed fast cipher geometry");
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),&EVP_CIPHER_CTX_free);
    Bytes output(input.size()+16);int count=0,tail=0;
    if(!ctx || EVP_CipherInit_ex(ctx.get(),EVP_aes_256_cbc(),nullptr,key.data(),iv.data(),encrypt?1:0)!=1 ||
       EVP_CIPHER_CTX_set_padding(ctx.get(),0)!=1 ||
       EVP_CipherUpdate(ctx.get(),output.data(),&count,input.data(),static_cast<int>(input.size()))!=1 ||
       EVP_CipherFinal_ex(ctx.get(),output.data()+count,&tail)!=1 || static_cast<std::size_t>(count+tail)!=input.size())
        throw Error("Fast cipher operation failed");
    output.resize(input.size());return output;
}
Bytes tag_input(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                std::span<const std::uint8_t> body) {
    auto canonical=label(domain(p,"DataPump/fast/v1/group","DataPump/fast/capacity/v2/group"));append(canonical,profile_id(p));append(canonical,salt);
    ordinal_bytes(canonical,ordinal);append(canonical,body);return canonical;
}
Bytes seal(const Profile& p,const Keys& keys,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
           std::span<const std::uint8_t> iv,std::span<const std::uint8_t> plain) {
    if(plain.size()!=ciphertext_bytes(p))throw Error("Invalid fixed fast plaintext width");
    Bytes systematic(iv.begin(),iv.end());append(systematic,crypt(plain,keys.encryption,iv,true));
    append(systematic,hmac(keys.authentication,tag_input(p,salt,ordinal,systematic)));
    if(p.capacity_mode)systematic.resize(systematic_bytes(p));
    return systematic;
}
Bytes open(const Profile& p,const Keys& keys,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
           std::span<const std::uint8_t> systematic) {
    if(systematic.size()!=systematic_bytes(p))throw Error("Invalid fixed fast systematic width");
    const auto body_size=16+ciphertext_bytes(p);
    if(std::any_of(systematic.begin()+static_cast<std::ptrdiff_t>(body_size+32),systematic.end(),[](auto b){return b!=0;}))
        throw CorruptCycle("Noncanonical capacity cipher alignment fill");
    auto body=systematic.first(body_size);
    if(!same(systematic.subspan(body_size,32),hmac(keys.authentication,tag_input(p,salt,ordinal,body))))
        throw CorruptCycle("Fast stream integrity failed");
    return crypt(body.subspan(16),keys.encryption,body.first(16),false);
}
Bytes public_tag(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                 std::span<const std::uint8_t> plain) {
    auto canonical=label(domain(p,"DataPump/fast/v1/public/group","DataPump/fast/capacity/v2/public/group"));append(canonical,profile_id(p));append(canonical,salt);
    ordinal_bytes(canonical,ordinal);append(canonical,plain);return checksum(canonical);
}
Bytes public_seal(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                  std::span<const std::uint8_t> plain) {
    if(plain.size()!=source_bytes_per_group(p,false))throw Error("Invalid fixed fast public source width");
    Bytes systematic(plain.begin(),plain.end());append(systematic,public_tag(p,salt,ordinal,plain));return systematic;
}
Bytes public_open(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                  std::span<const std::uint8_t> systematic) {
    if(systematic.size()!=systematic_bytes(p))throw Error("Invalid fixed fast public systematic width");
    const auto plain=systematic.first(systematic.size()-32);
    if(!same(systematic.last(32),public_tag(p,salt,ordinal,plain)))throw CorruptCycle("Fast public stream checksum failed");
    return Bytes(plain.begin(),plain.end());
}
Bytes bootstrap_tag(const Profile& p,const std::optional<Crypto>& crypto,std::span<const std::uint8_t> salt,
                    SourceEncoding encoding=SourceEncoding::raw) {
    auto canonical=label(crypto?domain(p,"DataPump/fast/v1/bootstrap","DataPump/fast/capacity/v2/bootstrap"):domain(p,"DataPump/fast/v1/public/bootstrap","DataPump/fast/capacity/v2/public/bootstrap"));
    append(canonical,profile_id(p));
    if(encoding==SourceEncoding::xz)append(canonical,label("/source/xz/v1"));
    append(canonical,salt);
    return crypto?crypto->mac(canonical):checksum(canonical);
}
Bytes rs_interleave(const Profile& p,std::span<const std::uint8_t> systematic) {
    const std::size_t rows=p.interleave_depth*2U,k=k_bytes(p);
    if(systematic.size()!=rows*k)throw Error("Invalid fixed fast supercycle width");
    Bytes output(rows*128);
    for(std::size_t row=0;row<rows;++row) {
        auto first=systematic.begin()+static_cast<std::ptrdiff_t>(row*k);
        const auto word=fec::rs_encode(Bytes(first,first+static_cast<std::ptrdiff_t>(k)),128-k);
        for(std::size_t column=0;column<128;++column)output[column*rows+row]=word[column];
    }
    return output;
}
Bytes rs_deinterleave(const Profile& p,const coding::Decoded& decoded,DecodeSnapshot& stats,
                     std::vector<std::string>* group_errors=nullptr) {
    const std::size_t rows=p.interleave_depth*2U,k=k_bytes(p);Bytes output;output.reserve(rows*k);
    for(std::size_t row=0;row<rows;++row) {
        Bytes word(128);std::vector<std::size_t> erasures;
        for(std::size_t column=0;column<128;++column) {
            word[column]=decoded.bytes[column*rows+row];
            if(decoded.unreliable[column*rows+row])erasures.push_back(column);
        }
        // An erasure spread beyond the RS budget is a hard failure, never
        // selectively discard unknown positions to manufacture continuity.
        try {
            stats.corrected_bytes+=fec::rs_correct(word,128-k,erasures);
            stats.erased_bytes+=erasures.size();
        }catch(const Error& error) {
            const std::string_view why=error.what();
            if(why!="Uncorrectable Reed-Solomon interval" && why!="Too many Reed-Solomon erasures")throw;
            if(!group_errors)throw CorruptCycle(error.what());
            auto& group_error=(*group_errors)[row/2];
            if(group_error.empty())group_error=error.what();
            std::fill(word.begin(),word.end(),0);
        }
        append(output,std::span(word).first(k));
    }
    return output;
}
template<class Visitor>void whitening_bits(std::size_t count,std::uint64_t cycle,Visitor&& visit) {
    // Public, frozen spectral whitening; this supplies no secrecy. SplitMix64
    // words are consumed LSB first, with a different local seed for each cycle.
    std::uint64_t state=0x44504d2f76322f77ULL+cycle*0xd1342543de82ef95ULL;
    std::uint64_t word=0;
    for(std::size_t i=0;i<count;++i) {
        if(i%64==0) {
            auto z=(state+=0x9e3779b97f4a7c15ULL);
            z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
            z=(z^(z>>27))*0x94d049bb133111ebULL;word=z^(z>>31);
        }
        visit(i,(word>>(i%64))&1U);
    }
}
const Bytes& capacity_rotations(const Profile& p) {
    validate(p);
    if(!p.capacity_mode||p.compact_convolutional)throw Error("Capacity LDPC interleave requires an LDPC capacity profile");
    const auto depth=p.interleave_depth,bps=static_cast<unsigned>(std::countr_zero(p.constellation));
    struct Cached {std::once_flag once;Bytes rotations;};
    // The finite frame/depth/QAM grid bounds this cache (under 15 MiB).
    // Normal use initializes only the selected depth/QAM pair, before audio.
    static std::array<std::array<std::array<Cached,11>,16>,5> cache;
    const auto frame_slot=p.ldpc_frame_bits==64800?0:p.ldpc_frame_bits==16200?1:
        p.ldpc_frame_bits==648?2:p.ldpc_frame_bits==1296?3:4;
    auto& entry=cache[frame_slot][depth-1][bps/2-1];
    std::call_once(entry.once,[&] {
        std::array<std::array<unsigned,22>,16> counts{};
        entry.rotations.resize(p.ldpc_frame_bits);
        for(std::size_t column=0;column<p.ldpc_frame_bits;++column) {
            auto best=std::numeric_limits<unsigned>::max();unsigned rotation=0;
            for(unsigned offset=0;offset<depth;++offset) {
                const auto candidate=static_cast<unsigned>((column+offset)%depth);
                unsigned cost=0;
                for(unsigned frame=0;frame<depth;++frame) {
                    const auto position=column*depth+(frame+candidate)%depth;
                    const auto plane=(position%physical_interval_bits)%bps;
                    cost+=counts[frame][plane];
                }
                if(cost<best){best=cost;rotation=candidate;}
            }
            entry.rotations[column]=static_cast<std::uint8_t>(rotation);
            for(unsigned frame=0;frame<depth;++frame) {
                const auto position=column*depth+(frame+rotation)%depth;
                ++counts[frame][(position%physical_interval_bits)%bps];
            }
        }
    });
    return entry.rotations;
}
Bytes capacity_encode(const Profile& p,std::span<const std::uint8_t> systematic) {
    auto rs=outer_rs::encode(systematic,capacity_parity_symbols(p));
    if(p.compact_convolutional)return coding::encode(rs,p.code_rate);
    // Odd numbers of DVB-S2 3/4 frames have one unpaired information byte.
    // Its fixed zero fill is LDPC-protected and checked independently of RS.
    rs.resize(capacity_info_bytes(p));
    const auto block_bytes=ldpc::data_bits(p.code_rate,p.ldpc_frame_bits)/8;
    Bytes wire(p.interleave_depth*p.ldpc_frame_bits);
    const auto& rotations=capacity_rotations(p);
    for(std::size_t block=0;block<p.interleave_depth;++block) {
        const auto bits=ldpc::interleave(ldpc::encode(std::span(rs).subspan(block*block_bytes,block_bytes),p.code_rate,p.ldpc_frame_bits),p.ldpc_frame_bits);
        for(std::size_t column=0;column<p.ldpc_frame_bits;++column)wire[column*p.interleave_depth+(block+rotations[column])%p.interleave_depth]=bits[column];
    }
    return wire;
}
Bytes capacity_decode(const Profile& p,std::span<const float> wire,DecodeSnapshot& stats) {
    Bytes decoded;decoded.reserve(capacity_info_bytes(p));
    if(p.compact_convolutional) {
        auto result=coding::decode(wire.first(capacity_coded_bits(p)),capacity_info_bytes(p),p.code_rate);
        // The trellis can reconstruct neutral evidence at its original timed
        // positions. Its conservative uncertainty window is diagnostic, not
        // an instruction to discard those corrected bytes. As with LDPC,
        // outer RS plus mandatory whole-cycle SHA/HMAC verify the result.
        stats.erased_bytes+=std::count(result.unreliable.begin(),result.unreliable.end(),1);
        decoded=std::move(result.bytes);
    } else {
    const auto& rotations=capacity_rotations(p);
    const auto workers=p.acoustic_ofdm?
        std::min({4U,p.interleave_depth,std::max(1U,std::thread::hardware_concurrency())}):1U;
    if(workers==1) {
        // Preserve the cable decoder's sequential path and scratch lifetime.
        std::vector<float> block_soft(p.ldpc_frame_bits);
        for(std::size_t block=0;block<p.interleave_depth;++block) {
            for(std::size_t column=0;column<p.ldpc_frame_bits;++column)block_soft[column]=wire[column*p.interleave_depth+(block+rotations[column])%p.interleave_depth];
            // A failed LDPC syndrome can still leave only a few erroneous symbols.
            // Let the outer RS repair those; the full-cycle digest is mandatory.
            auto input=ldpc::deinterleave(block_soft,p.ldpc_frame_bits);
            auto result=ldpc::decode(input,p.code_rate,ldpc::default_iterations,p.ldpc_frame_bits);
            ++stats.ldpc_frames;stats.ldpc_iterations+=result.iterations;
            if(!result.converged)++stats.ldpc_failed_frames;
            for(std::size_t bit=0;bit<result.bytes.size()*8;++bit)
                if(input[bit]!=0 && ((input[bit]>0)!=bool((result.bytes[bit/8]>>(7-bit%8))&1U)))++stats.ldpc_changed_bits;
            append(decoded,result.bytes);
        }
    } else {
        struct Frame {
            ldpc::DecodeResult result;
            std::uint64_t changed_bits=0;
            std::exception_ptr error;
        };
        std::array<Frame,16> frames;
        std::atomic<unsigned> next{0};
        const auto work=[&] {
            for(;;) {
                const auto block=next.fetch_add(1,std::memory_order_relaxed);
                if(block>=p.interleave_depth)return;
                auto& frame=frames[block];
                try {
                    std::vector<float> block_soft(p.ldpc_frame_bits);
                    for(std::size_t column=0;column<p.ldpc_frame_bits;++column)
                        block_soft[column]=wire[column*p.interleave_depth+(block+rotations[column])%p.interleave_depth];
                    const auto input=ldpc::deinterleave(block_soft,p.ldpc_frame_bits);
                    frame.result=ldpc::decode(input,p.code_rate,ldpc::default_iterations,p.ldpc_frame_bits);
                    for(std::size_t bit=0;bit<frame.result.bytes.size()*8;++bit)
                        if(input[bit]!=0 && ((input[bit]>0)!=bool((frame.result.bytes[bit/8]>>(7-bit%8))&1U)))++frame.changed_bits;
                }catch(...) {frame.error=std::current_exception();}
            }
        };
        {
            // The caller is one worker. Join all others before touching shared
            // output/statistics, including when thread creation itself throws.
            std::array<std::jthread,3> threads;
            for(unsigned i=1;i<workers;++i)threads[i-1]=std::jthread(work);
            work();
        }
        // Input order determines output, diagnostics and the first exception;
        // scheduling never changes which prefix of frame statistics is kept.
        for(unsigned block=0;block<p.interleave_depth;++block) {
            const auto& frame=frames[block];
            if(frame.error)std::rethrow_exception(frame.error);
            ++stats.ldpc_frames;stats.ldpc_iterations+=frame.result.iterations;
            if(!frame.result.converged)++stats.ldpc_failed_frames;
            stats.ldpc_changed_bits+=frame.changed_bits;
            append(decoded,frame.result.bytes);
        }
    }
    }
    if(decoded.size()%2) {
        if(decoded.back())throw CorruptCycle("Noncanonical capacity LDPC alignment fill");
        decoded.pop_back();
    }
    auto before=decoded;
    try {(void)outer_rs::correct(decoded,capacity_parity_symbols(p));}
    catch(const Error& error) {
        const std::string_view why=error.what();
        if(why=="Uncorrectable capacity Reed-Solomon cycle" || why=="Too many capacity Reed-Solomon erasures")
            throw CorruptCycle(error.what());
        throw;
    }
    for(std::size_t i=0;i<decoded.size();++i)if(decoded[i]!=before[i])++stats.corrected_bytes;
    decoded.resize(capacity_data_bytes(p));return decoded;
}
}

std::size_t capacity_parity_symbols(const Profile& p) {
    if(!p.capacity_mode)throw Error("Capacity geometry requires capacity profile");
    validate_codec_profile(p);
    // Smallest even parity count reaching 0.3% parity/data. Two-byte symbols
    // necessarily quantize the percentage, especially at interleave depth one.
    const auto symbols=capacity_info_bytes(p)/2;
    return 2*((3*symbols+2005)/2006);
}
std::size_t ciphertext_bytes(const Profile& p) { return p.capacity_mode?((capacity_data_bytes(p)-48)/16)*16:k_bytes(p)*2-48; }
std::size_t source_bytes_per_group(const Profile& p,bool encrypted) {return encrypted?ciphertext_bytes(p):systematic_bytes(p)-32;}
std::size_t capacity_source_bytes_per_cycle(const Profile& p,bool encrypted) {
    if(!p.capacity_mode)throw Error("Capacity geometry requires capacity profile");
    return source_bytes_per_group(p,encrypted)-1;
}
std::size_t capacity_information_bytes(const Profile& p) {
    validate_codec_profile(p);
    if(!p.capacity_mode)throw Error("Capacity geometry requires capacity profile");
    return capacity_info_bytes(p);
}
std::size_t capacity_coded_bits(const Profile& p) {
    validate_codec_profile(p);
    if(!p.capacity_mode)throw Error("Capacity geometry requires capacity profile");
    return p.compact_convolutional?inner_size(capacity_info_bytes(p),p.code_rate):p.interleave_depth*p.ldpc_frame_bits;
}
std::size_t cycle_intervals(const Profile& p) {
    validate_codec_profile(p);
    if(p.capacity_mode)return (capacity_coded_bits(p)+physical_interval_bits-1)/physical_interval_bits;
    return (inner_size(p.interleave_depth*256,p.code_rate)+physical_interval_bits-1)/physical_interval_bits;
}
SourceReader file_source(const std::filesystem::path& path) {
    auto input=std::make_shared<std::ifstream>(path,std::ios::binary);
    if(!*input)throw Error("Cannot open fast source file");
    return [input](std::span<std::uint8_t> destination) {
        input->read(reinterpret_cast<char*>(destination.data()),static_cast<std::streamsize>(destination.size()));
        const auto n=input->gcount();
        if(input->bad() || (input->fail() && !input->eof()))throw Error("Fast source read failed");
        return static_cast<std::size_t>(n);
    };
}
SourceReader byte_source(Bytes bytes) {
    return [bytes=std::move(bytes),position=std::size_t{0}](std::span<std::uint8_t> out) mutable {
        const auto count=std::min(out.size(),bytes.size()-position);
        std::copy_n(bytes.begin()+static_cast<std::ptrdiff_t>(position),count,out.begin());position+=count;return count;
    };
}

namespace coding {
Bytes encode(std::span<const std::uint8_t> bytes,CodeRate rate) {
    if(bytes.size()>64*256)throw Error("Invalid bounded fast trellis geometry");
    auto mask=puncture(rate);Bytes output;output.reserve(inner_size(bytes.size(),rate));
    unsigned state=0;std::size_t phase=0;
    for(std::size_t bit=0;bit<bytes.size()*8+6;++bit) {
        const auto value=bit<bytes.size()*8?((bytes[bit/8]>>(7-bit%8))&1U):0U;
        const auto reg=(state<<1)|value;
        for(auto generator:{0171U,0133U}) {
            if(mask[phase])output.push_back(static_cast<std::uint8_t>(std::popcount(reg&generator)&1));
            phase=(phase+1)%mask.size();
        }
        state=reg&63;
    }
    return output;
}
Decoded decode(std::span<const float> soft,std::size_t source_bytes,CodeRate rate) {
    if(source_bytes>64*256 || soft.size()!=inner_size(source_bytes,rate))throw Error("Invalid bounded fast trellis geometry");
    const auto steps=source_bytes*8+6;auto mask=puncture(rate);
    constexpr float impossible=-1e30F;
    std::array<float,64> metric{},next{};metric.fill(impossible);metric[0]=0;
    static const auto outputs=[] {
        std::array<std::array<unsigned,2>,64> table{};
        for(unsigned previous=0;previous<64;++previous)for(unsigned bit=0;bit<2;++bit) {
            const auto reg=(previous<<1)|bit;
            table[previous][bit]=static_cast<unsigned>(((std::popcount(reg&0171U)&1)<<1)|(std::popcount(reg&0133U)&1));
        }
        return table;
    }();
    std::vector<std::uint64_t> decisions(steps);std::vector<std::uint8_t> uncertain(steps,0);
    std::size_t cursor=0,phase=0;
    for(std::size_t step=0;step<steps;++step) {
        std::array<float,2> evidence{};
        for(unsigned j=0;j<2;++j) {
            if(mask[phase]) {
                const auto value=soft[cursor++];
                if(!std::isfinite(value))throw Error("Non-finite fast soft evidence");
                evidence[j]=std::clamp(value,-32.0F,32.0F);
                if(value==0)uncertain[step]=1;
            }
            phase=(phase+1)%mask.size();
        }
        const std::array<float,4> branch{-evidence[0]-evidence[1],-evidence[0]+evidence[1],
                                         evidence[0]-evidence[1], evidence[0]+evidence[1]};
        std::uint64_t decision=0;float maximum=impossible;
        for(unsigned state=0;state<64;++state) {
            const unsigned a=state>>1,b=a|32U,bit=state&1U;
            const auto ma=metric[a]+branch[outputs[a][bit]],mb=metric[b]+branch[outputs[b][bit]];
            if(mb>ma) {next[state]=mb;decision|=std::uint64_t{1}<<state;} else next[state]=ma;
            maximum=std::max(maximum,next[state]);
        }
        for(unsigned s=0;s<64;++s)metric[s]=next[s]-maximum;
        decisions[step]=decision;
    }
    Decoded output{Bytes(source_bytes),Bytes(source_bytes)};unsigned state=0;
    for(std::size_t step=steps;step-->0;) {
        const auto bit=state&1U;
        if(step<source_bytes*8)output.bytes[step/8]|=static_cast<std::uint8_t>(bit<<(7-step%8));
        state=(state>>1)|(((decisions[step]>>state)&1U)?32U:0U);
    }
    // Neutral evidence contaminates a bounded K*6 window on both sides of
    // the trellis position before byte interleaving is reversed.
    for(std::size_t step=0;step<steps;++step)if(uncertain[step]) {
        const auto begin=step>42?step-42:0,end=std::min(source_bytes*8,step+43);
        if(begin<end)for(auto b=begin/8;b<=(end-1)/8;++b)output.unreliable[b]=1;
    }
    return output;
}
}

struct ReceivedFile::Impl { Bytes data;std::string filename;std::size_t prefix_bytes=0; };
ReceivedFile::ReceivedFile(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
ReceivedFile::~ReceivedFile()=default;
std::uint64_t ReceivedFile::size()const{return impl_->data.size()-impl_->prefix_bytes;}
bool ReceivedFile::is_attachment()const{return impl_->prefix_bytes!=0;}
const std::string& ReceivedFile::filename()const{return impl_->filename;}
std::span<const std::uint8_t> ReceivedFile::bytes()const{return std::span<const std::uint8_t>(impl_->data).subspan(impl_->prefix_bytes);}
void ReceivedFile::save(const std::filesystem::path& path)const {
#ifdef _WIN32
    auto* raw=_wfopen(path.c_str(),L"wbx");
#else
    auto* raw=std::fopen(path.c_str(),"wbx");
#endif
    if(!raw)throw Error("Cannot exclusively create fast destination");
    File output(raw);
    try {
        write_file(output.get(),bytes());
        if(std::fflush(output.get())!=0)throw Error("Fast destination write failed");
        auto* closed=output.release();if(std::fclose(closed)!=0)throw Error("Fast destination close failed");
    }catch(...) {
        output.reset();std::error_code ignored;std::filesystem::remove(path,ignored);throw;
    }
}

TransmitEstimate estimate_transmission(const Profile& p,bool encrypted,std::uint64_t bytes) {
    validate(p);
    if(bytes>xz_size_bound(attachment::source_limit(256ULL*1024*1024)))throw Error("Fast encoded source exceeds local memory budget");
    const auto capacity=static_cast<std::uint64_t>(p.interleave_depth)*source_bytes_per_group(p,encrypted)*8;
    const auto cycles=p.capacity_mode?2+bytes/capacity_source_bytes_per_cycle(p,encrypted):1+(9*(bytes+1)+capacity-1)/capacity;
    TransmitEstimate out;out.intervals=cycles*cycle_intervals(p);
    out.samples=transmission_samples(p,out.intervals)+end_silence_samples(p);
    out.seconds=static_cast<double>(out.samples)/p.sample_rate;
    out.source_bps=8.0*static_cast<double>(bytes)/out.seconds;
    return out;
}

struct StreamEncoder::Impl {
    Profile profile;std::optional<Crypto> crypto;SourceReader reader;std::function<Bytes(std::size_t)> random;Bytes salt;std::unique_ptr<Keys> keys;Bytes wire;
    std::size_t offset=0;std::uint64_t source_count=0,intervals=0,ordinal=0;
    bool bootstrap=true,source_end=false,terminal_done=false,final_cycle=false;SourceEncoding source_encoding;
    std::array<std::uint8_t,16384> input{};std::size_t input_pos=0,input_size=0;
    unsigned cell=0,cell_left=0;
    Impl(Profile p,const std::optional<Crypto>& c,SourceReader r,std::function<Bytes(std::size_t)> entropy,SourceEncoding encoding):
        profile(p),crypto(c),reader(std::move(r)),random(std::move(entropy)),salt(random(32)),source_encoding(encoding) {
        validate_codec_profile(p);if(!reader)throw Error("Missing fast source reader");
        if(p.capacity_mode&&!p.compact_convolutional)(void)capacity_rotations(p);
        if(crypto)keys=std::make_unique<Keys>(p,*crypto,salt);
    }
    bool next_source_byte(std::uint8_t& byte) {
        if(input_pos==input_size && !source_end) {
            input_size=reader(input);input_pos=0;
            if(input_size>input.size())throw Error("Fast source reader exceeded its buffer");
            if(!input_size)source_end=true;
        }
        if(source_end)return false;
        if(source_count==std::numeric_limits<std::uint64_t>::max())throw Error("Fast source counter exhausted");
        byte=input[input_pos++];++source_count;return true;
    }
    void next_capacity_cycle() {
        Bytes systematic;std::uint64_t wire_ordinal=0;
        if(bootstrap) {
            systematic=salt;append(systematic,bootstrap_tag(profile,crypto,salt,source_encoding));
            systematic.resize(capacity_data_bytes(profile));bootstrap=false;
        } else {
            if(ordinal==std::numeric_limits<std::uint64_t>::max())throw Error("Fast cycle counter exhausted");
            wire_ordinal=ordinal+1;
            Bytes area(source_bytes_per_group(profile,crypto.has_value()));
            for(std::size_t position=1;position<area.size();++position) {
                if(!next_source_byte(area[position])) {
                    area[0]=1;area[position]=0x80;final_cycle=true;break;
                }
            }
            systematic=keys?seal(profile,*keys,salt,ordinal,random(16),area):public_seal(profile,salt,ordinal,area);
            ++ordinal;OPENSSL_cleanse(area.data(),area.size());
        }
        wire=capacity_encode(profile,systematic);
        wire.resize(cycle_intervals(profile)*physical_interval_bits);
        whitening_bits(wire.size(),wire_ordinal,[&](std::size_t i,unsigned bit){wire[i]^=static_cast<std::uint8_t>(bit);});
        offset=0;
    }
    unsigned next_source_bit() {
        if(terminal_done)return 0;
        if(!cell_left) {
            if(input_pos==input_size && !source_end) {
                input_size=reader(input);input_pos=0;
                if(input_size>input.size())throw Error("Fast source reader exceeded its buffer");
                if(!input_size)source_end=true;
            }
            if(source_end)cell=0;
            else {
                if(source_count==std::numeric_limits<std::uint64_t>::max())throw Error("Fast source counter exhausted");
                cell=0x100U|input[input_pos++];++source_count;
            }
            cell_left=9;
        }
        const auto bit=(cell>>(--cell_left))&1U;
        if(!cell_left && source_end)terminal_done=true;
        return bit;
    }
    void next_cycle() {
        if(profile.capacity_mode){next_capacity_cycle();return;}
        Bytes systematic;systematic.reserve(profile.interleave_depth*k_bytes(profile)*2);
        if(bootstrap) {
            systematic=salt;append(systematic,bootstrap_tag(profile,crypto,salt,source_encoding));
            systematic.resize(profile.interleave_depth*k_bytes(profile)*2);bootstrap=false;
        }else {
            for(unsigned group=0;group<profile.interleave_depth;++group) {
                if(ordinal==std::numeric_limits<std::uint64_t>::max())throw Error("Fast group counter exhausted");
                Bytes plain(source_bytes_per_group(profile,crypto.has_value()));
                for(std::size_t bit=0;bit<plain.size()*8;++bit)plain[bit/8]|=static_cast<std::uint8_t>(next_source_bit()<<(7-bit%8));
                append(systematic,keys?seal(profile,*keys,salt,ordinal,random(16),plain):public_seal(profile,salt,ordinal,plain));
                ++ordinal;
                OPENSSL_cleanse(plain.data(),plain.size());
            }
            final_cycle=terminal_done;
        }
        wire=coding::encode(rs_interleave(profile,systematic),profile.code_rate);
        wire.resize(cycle_intervals(profile)*physical_interval_bits);offset=0;
    }
};
StreamEncoder::StreamEncoder(Profile p,const std::optional<Crypto>& c,SourceReader r,SourceEncoding encoding):StreamEncoder(p,c,std::move(r),random_bytes,encoding){}
StreamEncoder::StreamEncoder(Profile p,const std::optional<Crypto>& c,SourceReader r,std::function<Bytes(std::size_t)> random,SourceEncoding encoding):
    impl_(std::make_unique<Impl>(p,c,std::move(r),std::move(random),encoding)){}
StreamEncoder::~StreamEncoder()=default;
StreamEncoder::StreamEncoder(StreamEncoder&&) noexcept=default;
StreamEncoder& StreamEncoder::operator=(StreamEncoder&&) noexcept=default;
bool StreamEncoder::next_interval(std::span<std::uint8_t> bits) {
    if(bits.size()!=physical_interval_bits)throw Error("Fast interval must have exactly 2048 positions");
    if(impl_->offset==impl_->wire.size()) {
        if(impl_->final_cycle)return false;
        impl_->next_cycle();
    }
    std::copy_n(impl_->wire.begin()+static_cast<std::ptrdiff_t>(impl_->offset),bits.size(),bits.begin());
    impl_->offset+=bits.size();++impl_->intervals;return true;
}
std::uint64_t StreamEncoder::source_bytes()const{return impl_->source_count;}
std::uint64_t StreamEncoder::intervals_emitted()const{return impl_->intervals;}

struct StreamDecoder::Impl {
    Profile profile;std::optional<Crypto> crypto;std::uint64_t quota;DecodeSnapshot stats;Bytes plain,valid_areas;
    std::vector<float> soft;Bytes salt;std::unique_ptr<Keys> keys;std::uint64_t ordinal=0,cycles=0;
    bool bootstrap_received=false;SourceEncoding source_encoding;
    std::shared_ptr<const ReceivedFile> received;
    Impl(Profile p,const std::optional<Crypto>& c,std::uint64_t q,SourceEncoding encoding):profile(p),crypto(c),quota(std::min<std::uint64_t>(q,256ULL*1024*1024)),source_encoding(encoding) {
        validate_codec_profile(p);
        if(p.capacity_mode&&!p.compact_convolutional)(void)capacity_rotations(p);
        soft.reserve(cycle_intervals(p)*physical_interval_bits);
        stats.encrypted=crypto.has_value();
    }
    void fail(const std::string& why) {stats.failed=true;stats.decoding_stopped=true;stats.status=why;}
    void cycle() {
        if(stats.coding_cycles==std::numeric_limits<std::uint64_t>::max())throw Error("Fast cycle counter exhausted");
        const auto physical_cycle=stats.coding_cycles++;
        const bool bootstrap=!bootstrap_received;
        const auto groups=profile.capacity_mode?1U:profile.interleave_depth;
        const auto width=source_bytes_per_group(profile,crypto.has_value());
        const auto first_ordinal=ordinal;
        if(!bootstrap) {
            if(groups>std::numeric_limits<std::uint64_t>::max()-ordinal)throw Error("Fast group counter exhausted");
            const auto bytes=static_cast<std::uint64_t>(groups)*width;
            // Charge every local fixed position, including rejected cycles, so
            // an indefinitely corrupt stream cannot evade the receive quota.
            if(bytes>quota-stats.spool_bytes)throw Error("Fast receive memory quota exceeded");
            const auto required=plain.size()+static_cast<std::size_t>(bytes);
            if(required>plain.capacity())
                plain.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(quota,
                    std::max<std::uint64_t>(required,plain.capacity()*2))));
            plain.resize(required,0);
            stats.spool_bytes=plain.size();
            valid_areas.resize(valid_areas.size()+groups,0);
            ordinal+=groups;++cycles;
        }
        bool rejected=false;
        const auto reject=[&](const std::string& why) {
            if(!rejected) {++stats.failed_cycles;rejected=true;}
            if(!stats.failed)stats.status=why;
            stats.failed=true;
        };
        Bytes systematic;
        std::vector<std::string> group_errors(groups);
        try {
            if(profile.capacity_mode) {
                whitening_bits(soft.size(),physical_cycle,[&](std::size_t i,unsigned bit){if(bit)soft[i]=-soft[i];});
                systematic=capacity_decode(profile,soft,stats);
            } else {
                const auto actual=inner_size(profile.interleave_depth*256,profile.code_rate);
                auto decoded=coding::decode(std::span(soft).first(actual),profile.interleave_depth*256,profile.code_rate);
                systematic=rs_deinterleave(profile,decoded,stats,bootstrap?nullptr:&group_errors);
            }
            // The inner decoders validate their own evidence in frame order;
            // validate the remaining physical alignment fill as well.
            if(std::any_of(soft.begin(),soft.end(),[](float x){return !std::isfinite(x);}))
                throw Error("Non-finite fast soft evidence");
        }catch(const CorruptCycle& error) {
            if(std::any_of(soft.begin(),soft.end(),[](float x){return !std::isfinite(x);}))
                throw Error("Non-finite fast soft evidence");
            reject(error.what());
            if(bootstrap)fail(error.what());
            soft.clear();return;
        }
        if(bootstrap) {
            salt.assign(systematic.begin(),systematic.begin()+32);
            if(!same(std::span(systematic).subspan(32,32),bootstrap_tag(profile,crypto,salt,source_encoding)) ||
               std::any_of(systematic.begin()+64,systematic.end(),[](auto b){return b!=0;})) {
                const auto why=crypto?"Fast bootstrap integrity failed":"Fast public bootstrap checksum failed";
                reject(why);fail(why);soft.clear();return;
            }
            if(crypto)keys=std::make_unique<Keys>(profile,*crypto,salt);
            bootstrap_received=true;
            stats.status=crypto?"Receiving authenticated fast areas":"Receiving checksummed fast areas (not authenticated)";
        }else {
            for(unsigned group=0;group<groups;++group) {
                if(!group_errors[group].empty()) {reject(group_errors[group]);continue;}
                const auto k=systematic_bytes(profile);
                const auto body=std::span(systematic).subspan(group*k,k);
                const auto area_ordinal=first_ordinal+group;
                try {
                    auto area=keys?open(profile,*keys,salt,area_ordinal,body):public_open(profile,salt,area_ordinal,body);
                    if(area.size()!=width)throw Error("Invalid fixed fast source width");
                    std::copy(area.begin(),area.end(),plain.begin()+static_cast<std::ptrdiff_t>(area_ordinal*width));
                    valid_areas[static_cast<std::size_t>(area_ordinal)]=1;
                    stats.verified_bytes+=area.size();
                    if(crypto)++stats.authenticated_groups;else ++stats.checksum_groups;
                    OPENSSL_cleanse(area.data(),area.size());
                }catch(const CorruptCycle& error) {reject(error.what());}
            }
        }
        soft.clear();
    }
    std::shared_ptr<ReceivedFile::Impl> interpret_capacity() {
        const auto width=source_bytes_per_group(profile,crypto.has_value());
        if(plain.size()!=cycles*width)throw Error("Invalid capacity source geometry");
        std::size_t final_end=plain.size();
        for(std::size_t cycle=0;cycle<cycles;++cycle) {
            const auto begin=cycle*width;const bool last=cycle+1==cycles;
            if(plain[begin]!=(last?1:0))throw Error("Missing or noncanonical capacity final flag");
            auto end=begin+width;
            if(last) {
                while(end>begin+1 && plain[end-1]==0)--end;
                if(end==begin+1 || plain[end-1]!=0x80)throw Error("Invalid capacity final padding");
                --end;
                final_end=end;
            }
        }
        // Validate every area before changing any opaque retained bytes.
        auto output=std::make_shared<ReceivedFile::Impl>();std::size_t used=0;
        for(std::size_t cycle=0;cycle<cycles;++cycle) {
            const auto begin=cycle*width;
            const auto end=cycle+1==cycles?final_end:begin+width;
            for(std::size_t read=begin+1;read<end;++read)plain[used++]=plain[read];
        }
        plain.resize(used);output->data=std::move(plain);
        stats.source_bytes=used;return output;
    }
    std::shared_ptr<ReceivedFile::Impl> interpret() {
        if(!bootstrap_received || !cycles || !soft.empty())throw Error("Fast stream ended within fixed coding geometry");
        if(profile.capacity_mode)return interpret_capacity();
        // Interpret only after physical end. First validate the entire source
        // without modifying opaque retained areas if source syntax is invalid.
        bool ended=false;unsigned cell=0,cell_bits=0;
        std::uint64_t position=0,endpoint=0;std::size_t used=0;
        for(std::size_t b=0;b<plain.size();++b) {
            const auto source=plain[b];
            for(unsigned i=0;i<8;++i) {
                const auto bit=(source>>(7-i))&1U;++position;
                if(ended) {if(bit)throw Error("Noncanonical fast source fill");continue;}
                cell=(cell<<1)|bit;
                if(++cell_bits!=9)continue;
                if(!(cell&256)) {
                    if(cell)throw Error("Invalid fast endpoint cell");
                    ended=true;endpoint=position;
                }else ++used;
                cell=0;cell_bits=0;
            }
        }
        if(!ended)throw Error("Fast mandatory source endpoint missing");
        const auto cycle_bits=static_cast<std::uint64_t>(profile.interleave_depth)*source_bytes_per_group(profile,crypto.has_value())*8;
        if(endpoint<=position-cycle_bits)throw Error("Extra noncanonical fast padding cycle");
        auto output=std::make_shared<ReceivedFile::Impl>();
        // Nine-bit cells shrink to bytes, so these writes cannot overwrite
        // unread source bits. The endpoint cell and fill have already passed.
        for(std::size_t byte=0;byte<used;++byte) {
            unsigned value=0;
            for(unsigned i=0;i<8;++i) {
                const auto bit=byte*9+1+i;
                value=(value<<1)|((plain[bit/8]>>(7-bit%8))&1U);
            }
            plain[byte]=static_cast<std::uint8_t>(value);
        }
        plain.resize(used);output->data=std::move(plain);
        stats.source_bytes=used;
        return output;
    }
};
StreamDecoder::StreamDecoder(Profile p,const std::optional<Crypto>& c,std::uint64_t quota,SourceEncoding encoding):impl_(std::make_unique<Impl>(p,c,quota,encoding)){}
StreamDecoder::~StreamDecoder()=default;
StreamDecoder::StreamDecoder(StreamDecoder&&) noexcept=default;
StreamDecoder& StreamDecoder::operator=(StreamDecoder&&) noexcept=default;
void StreamDecoder::push_interval(std::span<const float> soft_bits) {
    if(soft_bits.size()!=physical_interval_bits)throw Error("Fast interval must have exactly 2048 positions");
    if(impl_->stats.physical_end)throw Error("Fast reception already physically ended");
    if(impl_->stats.intervals==std::numeric_limits<std::uint64_t>::max()) {
        impl_->soft.clear();impl_->fail("Fast interval counter exhausted");
        throw Error("Fast interval counter exhausted");
    }
    ++impl_->stats.intervals;if(impl_->stats.decoding_stopped)return;
    try {
        impl_->soft.insert(impl_->soft.end(),soft_bits.begin(),soft_bits.end());
        if(impl_->soft.size()==cycle_intervals(impl_->profile)*physical_interval_bits)impl_->cycle();
    }catch(const std::exception& error) {impl_->soft.clear();impl_->fail(error.what());}
}
void StreamDecoder::finish(bool physical_end) {
    if(impl_->stats.physical_end)return;
    if(!physical_end) {
        if(!impl_->stats.failed)impl_->stats.status="Incomplete: physical absence has not been observed";
        return;
    }
    impl_->stats.physical_end=true;
    if(impl_->stats.failed)return;
    try {
        auto output=impl_->interpret();
        if(impl_->source_encoding==SourceEncoding::xz) {
            impl_->stats.source_bytes=0;
            output->data=decode_xz(output->data,attachment::source_limit(impl_->quota));
            auto description=attachment::inspect(output->data);
            const auto content_bytes=output->data.size()-description.prefix_bytes;
            if(content_bytes>impl_->quota)throw Error("Fast decoded content exceeds local source quota");
            output->filename=std::move(description.filename);output->prefix_bytes=description.prefix_bytes;
            impl_->stats.source_bytes=content_bytes;
        }
        impl_->received=std::shared_ptr<const ReceivedFile>(new ReceivedFile(std::move(output)));
        impl_->stats.complete=true;impl_->stats.authenticated=impl_->crypto.has_value();
        impl_->stats.status=impl_->crypto?"Verified and physically complete":"Checksum-verified and physically complete (not authenticated)";
    }catch(const std::exception& error) {impl_->fail(error.what());}
}
DecodeSnapshot StreamDecoder::snapshot()const{return impl_->stats;}
std::shared_ptr<const ReceivedFile> StreamDecoder::result()const{return impl_->received;}

namespace testing {
std::optional<Bytes> retained_source_area(const StreamDecoder& decoder,std::uint64_t ordinal) {
    const auto& impl=*decoder.impl_;
    if(!impl.stats.physical_end || impl.stats.complete || ordinal>=impl.valid_areas.size() ||
       !impl.valid_areas[static_cast<std::size_t>(ordinal)])return std::nullopt;
    const auto width=source_bytes_per_group(impl.profile,impl.crypto.has_value());
    const auto begin=static_cast<std::size_t>(ordinal)*width;
    if(begin>impl.plain.size() || width>impl.plain.size()-begin)return std::nullopt;
    return Bytes(impl.plain.begin()+static_cast<std::ptrdiff_t>(begin),
                 impl.plain.begin()+static_cast<std::ptrdiff_t>(begin+width));
}
std::size_t capacity_interleave_rotation(const Profile& p,std::size_t column) {
    if(column>=p.ldpc_frame_bits)throw Error("Capacity interleave column outside fixed frame");
    return capacity_rotations(p)[column];
}
Bytes capacity_whitening_mask(std::size_t bits,std::uint64_t cycle) {
    if(bits>16*ldpc::coded_bits+physical_interval_bits)throw Error("Invalid bounded capacity whitening geometry");
    Bytes mask(bits);whitening_bits(bits,cycle,[&](std::size_t i,unsigned bit){mask[i]=static_cast<std::uint8_t>(bit);});return mask;
}
StreamEncoder deterministic_encoder(Profile p,const Crypto& crypto,SourceReader source,std::uint64_t seed) {
    return deterministic_encoder(p,std::optional<Crypto>{crypto},std::move(source),seed,SourceEncoding::raw);
}
StreamEncoder deterministic_encoder(Profile p,const std::optional<Crypto>& crypto,SourceReader source,std::uint64_t seed,SourceEncoding encoding) {
    // SplitMix64 is intentionally a test-only byte source, scoped to this
    // encoder. It neither seeds nor replaces OpenSSL's production RNG.
    auto random=[state=seed](std::size_t size) mutable {
        Bytes bytes(size);std::uint64_t word=0;
        for(std::size_t i=0;i<size;++i) {
            if(i%8==0) {
                auto z=(state+=0x9e3779b97f4a7c15ULL);
                z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
                z=(z^(z>>27))*0x94d049bb133111ebULL;word=z^(z>>31);
            }
            bytes[i]=static_cast<std::uint8_t>(word>>(8*(i%8)));
        }
        return bytes;
    };
    return StreamEncoder(p,crypto,std::move(source),std::move(random),encoding);
}
Bytes seal_group(const Profile& p,const Crypto& crypto,std::span<const std::uint8_t> salt,
    std::uint64_t ordinal,std::span<const std::uint8_t> iv,std::span<const std::uint8_t> plaintext) {
    validate_codec_profile(p);return seal(p,Keys(p,crypto,salt),salt,ordinal,iv,plaintext);
}
Bytes open_group(const Profile& p,const Crypto& crypto,std::span<const std::uint8_t> salt,
    std::uint64_t ordinal,std::span<const std::uint8_t> systematic) {
    validate_codec_profile(p);return open(p,Keys(p,crypto,salt),salt,ordinal,systematic);
}
}
}
