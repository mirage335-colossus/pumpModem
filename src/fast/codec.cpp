#include "datapump/fast/codec.hpp"
#include "datapump/stream_codec.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <utility>

namespace datapump::fast {
namespace {
struct CloseFile { void operator()(std::FILE* f)const {if(f)std::fclose(f);} };
using File = std::unique_ptr<std::FILE, CloseFile>;
File spool() {
    auto* f=std::tmpfile();
    if(!f) throw Error("Cannot create private fast receive spool");
    return File(f);
}
void write_file(std::FILE* f,std::span<const std::uint8_t> data) {
    if(std::fwrite(data.data(),1,data.size(),f)!=data.size()) throw Error("Fast receive spool write failed");
}
void rewind_file(std::FILE* f) {
    if(std::fflush(f)!=0 || std::fseek(f,0,SEEK_SET)!=0) throw Error("Fast receive spool seek failed");
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
    }
    throw Error("Unknown fast code rate");
}
std::size_t inner_size(std::size_t source_bytes,CodeRate rate) {
    const auto mask=puncture(rate);const auto raw=(source_bytes*8+6)*2;
    const auto whole=raw/mask.size()*static_cast<std::size_t>(std::count(mask.begin(),mask.end(),1));
    return whole+static_cast<std::size_t>(std::count(mask.begin(),mask.begin()+static_cast<std::ptrdiff_t>(raw%mask.size()),1));
}
std::size_t k_bytes(const Profile& p) { return p.robust?112:120; }
struct Keys {
    Bytes encryption,authentication;
    Keys(const Profile& p,const Crypto& crypto,std::span<const std::uint8_t> salt) {
        if(salt.size()!=32)throw Error("Invalid fixed fast salt width");
        auto base=crypto.mac(label("DataPump/fast/v1/root"));
        auto prk=hmac(salt,base);OPENSSL_cleanse(base.data(),base.size());
        auto context=profile_id(p);
        auto info=label("DataPump/fast/v1/AES-256-CBC");append(info,context);info.push_back(1);
        encryption=hmac(prk,info);
        info=label("DataPump/fast/v1/HMAC-SHA256");append(info,context);info.push_back(1);
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
    auto canonical=label("DataPump/fast/v1/group");append(canonical,profile_id(p));append(canonical,salt);
    ordinal_bytes(canonical,ordinal);append(canonical,body);return canonical;
}
Bytes seal(const Profile& p,const Keys& keys,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
           std::span<const std::uint8_t> iv,std::span<const std::uint8_t> plain) {
    if(plain.size()!=ciphertext_bytes(p))throw Error("Invalid fixed fast plaintext width");
    Bytes systematic(iv.begin(),iv.end());append(systematic,crypt(plain,keys.encryption,iv,true));
    append(systematic,hmac(keys.authentication,tag_input(p,salt,ordinal,systematic)));return systematic;
}
Bytes open(const Profile& p,const Keys& keys,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
           std::span<const std::uint8_t> systematic) {
    if(systematic.size()!=k_bytes(p)*2)throw Error("Invalid fixed fast systematic width");
    auto body=systematic.first(systematic.size()-32);
    if(!same(systematic.last(32),hmac(keys.authentication,tag_input(p,salt,ordinal,body))))
        throw Error("Fast stream integrity failed");
    return crypt(body.subspan(16),keys.encryption,body.first(16),false);
}
Bytes public_tag(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                 std::span<const std::uint8_t> plain) {
    auto canonical=label("DataPump/fast/v1/public/group");append(canonical,profile_id(p));append(canonical,salt);
    ordinal_bytes(canonical,ordinal);append(canonical,plain);return checksum(canonical);
}
Bytes public_seal(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                  std::span<const std::uint8_t> plain) {
    if(plain.size()!=source_bytes_per_group(p,false))throw Error("Invalid fixed fast public source width");
    Bytes systematic(plain.begin(),plain.end());append(systematic,public_tag(p,salt,ordinal,plain));return systematic;
}
Bytes public_open(const Profile& p,std::span<const std::uint8_t> salt,std::uint64_t ordinal,
                  std::span<const std::uint8_t> systematic) {
    if(systematic.size()!=k_bytes(p)*2)throw Error("Invalid fixed fast public systematic width");
    const auto plain=systematic.first(systematic.size()-32);
    if(!same(systematic.last(32),public_tag(p,salt,ordinal,plain)))throw Error("Fast public stream checksum failed");
    return Bytes(plain.begin(),plain.end());
}
Bytes bootstrap_tag(const Profile& p,const std::optional<Crypto>& crypto,std::span<const std::uint8_t> salt) {
    auto canonical=label(crypto?"DataPump/fast/v1/bootstrap":"DataPump/fast/v1/public/bootstrap");
    append(canonical,profile_id(p));append(canonical,salt);
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
Bytes rs_deinterleave(const Profile& p,const coding::Decoded& decoded,DecodeSnapshot& stats) {
    const std::size_t rows=p.interleave_depth*2U,k=k_bytes(p);Bytes output;output.reserve(rows*k);
    for(std::size_t row=0;row<rows;++row) {
        Bytes word(128);std::vector<std::size_t> erasures;
        for(std::size_t column=0;column<128;++column) {
            word[column]=decoded.bytes[column*rows+row];
            if(decoded.unreliable[column*rows+row])erasures.push_back(column);
        }
        // An erasure spread beyond the RS budget is a hard failure, never
        // selectively discard unknown positions to manufacture continuity.
        stats.corrected_bytes+=fec::rs_correct(word,128-k,erasures);
        stats.erased_bytes+=erasures.size();append(output,std::span(word).first(k));
    }
    return output;
}
}

std::size_t ciphertext_bytes(const Profile& p) { return k_bytes(p)*2-48; }
std::size_t source_bytes_per_group(const Profile& p,bool encrypted) {return encrypted?ciphertext_bytes(p):k_bytes(p)*2-32;}
std::size_t cycle_intervals(const Profile& p) {
    validate_codec_profile(p);
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

struct ReceivedFile::Impl {
    File file;std::uint64_t bytes=0;mutable std::mutex mutex;
    Impl():file(spool()){}
};
ReceivedFile::ReceivedFile(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
ReceivedFile::~ReceivedFile()=default;
std::uint64_t ReceivedFile::size()const{return impl_->bytes;}
Bytes ReceivedFile::preview(std::size_t maximum)const {
    std::lock_guard lock(impl_->mutex);rewind_file(impl_->file.get());
    Bytes out(static_cast<std::size_t>(std::min<std::uint64_t>(impl_->bytes,std::min<std::size_t>(maximum,4096))));
    if(std::fread(out.data(),1,out.size(),impl_->file.get())!=out.size())throw Error("Fast receive spool read failed");
    return out;
}
void ReceivedFile::save(const std::filesystem::path& path)const {
    std::lock_guard lock(impl_->mutex);rewind_file(impl_->file.get());
#ifdef _WIN32
    auto* raw=_wfopen(path.c_str(),L"wbx");
#else
    auto* raw=std::fopen(path.c_str(),"wbx");
#endif
    if(!raw)throw Error("Cannot exclusively create fast destination");
    File output(raw);
    try {
        std::array<std::uint8_t,16384> buffer{};std::uint64_t left=impl_->bytes;
        while(left) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(left,buffer.size()));
            if(std::fread(buffer.data(),1,n,impl_->file.get())!=n)throw Error("Fast receive spool read failed");
            write_file(output.get(),std::span(buffer).first(n));left-=n;
        }
        if(std::fflush(output.get())!=0)throw Error("Fast destination write failed");
        auto* closed=output.release();if(std::fclose(closed)!=0)throw Error("Fast destination close failed");
    }catch(...) {
        output.reset();std::error_code ignored;std::filesystem::remove(path,ignored);throw;
    }
}

struct StreamEncoder::Impl {
    Profile profile;std::optional<Crypto> crypto;SourceReader reader;std::function<Bytes(std::size_t)> random;Bytes salt;std::unique_ptr<Keys> keys;Bytes wire;
    std::size_t offset=0;std::uint64_t source_count=0,intervals=0,ordinal=0;
    bool bootstrap=true,source_end=false,terminal_done=false,final_cycle=false;
    std::array<std::uint8_t,16384> input{};std::size_t input_pos=0,input_size=0;
    unsigned cell=0,cell_left=0;
    Impl(Profile p,const std::optional<Crypto>& c,SourceReader r,std::function<Bytes(std::size_t)> entropy):
        profile(p),crypto(c),reader(std::move(r)),random(std::move(entropy)),salt(random(32)) {
        validate_codec_profile(p);if(!reader)throw Error("Missing fast source reader");
        if(crypto)keys=std::make_unique<Keys>(p,*crypto,salt);
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
        Bytes systematic;systematic.reserve(profile.interleave_depth*k_bytes(profile)*2);
        if(bootstrap) {
            systematic=salt;append(systematic,bootstrap_tag(profile,crypto,salt));
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
StreamEncoder::StreamEncoder(Profile p,const std::optional<Crypto>& c,SourceReader r):StreamEncoder(p,c,std::move(r),random_bytes){}
StreamEncoder::StreamEncoder(Profile p,const std::optional<Crypto>& c,SourceReader r,std::function<Bytes(std::size_t)> random):
    impl_(std::make_unique<Impl>(p,c,std::move(r),std::move(random))){}
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
    Profile profile;std::optional<Crypto> crypto;std::uint64_t quota;DecodeSnapshot stats;File plain;
    std::vector<float> soft;Bytes salt;std::unique_ptr<Keys> keys;std::uint64_t ordinal=0,cycles=0;
    bool bootstrap_received=false;
    std::shared_ptr<const ReceivedFile> received;
    Impl(Profile p,const std::optional<Crypto>& c,std::uint64_t q):profile(p),crypto(c),quota(q),plain(spool()) {
        validate_codec_profile(p);soft.reserve(cycle_intervals(p)*physical_interval_bits);
        stats.encrypted=crypto.has_value();
    }
    void fail(const std::string& why) {stats.failed=true;stats.status=why;}
    void cycle() {
        const auto actual=inner_size(profile.interleave_depth*256,profile.code_rate);
        auto decoded=coding::decode(std::span(soft).first(actual),profile.interleave_depth*256,profile.code_rate);
        auto systematic=rs_deinterleave(profile,decoded,stats);
        if(!bootstrap_received) {
            salt.assign(systematic.begin(),systematic.begin()+32);
            if(!same(std::span(systematic).subspan(32,32),bootstrap_tag(profile,crypto,salt)) ||
               std::any_of(systematic.begin()+64,systematic.end(),[](auto b){return b!=0;}))
                throw Error(crypto?"Fast bootstrap integrity failed":"Fast public bootstrap checksum failed");
            if(crypto)keys=std::make_unique<Keys>(profile,*crypto,salt);
            bootstrap_received=true;
            stats.status=crypto?"Receiving authenticated fast areas":"Receiving checksummed fast areas (not authenticated)";
        }else {
            for(unsigned group=0;group<profile.interleave_depth;++group) {
                if(ordinal==std::numeric_limits<std::uint64_t>::max())throw Error("Fast group counter exhausted");
                const auto k=k_bytes(profile)*2;
                const auto body=std::span(systematic).subspan(group*k,k);
                auto area=keys?open(profile,*keys,salt,ordinal,body):public_open(profile,salt,ordinal,body);++ordinal;
                if(area.size()>quota-stats.spool_bytes)throw Error("Fast receive spool quota exceeded");
                write_file(plain.get(),area);stats.spool_bytes+=area.size();
                if(crypto)++stats.authenticated_groups;else ++stats.checksum_groups;
                OPENSSL_cleanse(area.data(),area.size());
            }
            ++cycles;
        }
        soft.clear();
    }
    std::shared_ptr<ReceivedFile::Impl> interpret() {
        if(!bootstrap_received || !cycles || !soft.empty())throw Error("Fast stream ended within fixed coding geometry");
        rewind_file(plain.get());auto output=std::make_shared<ReceivedFile::Impl>();
        std::array<std::uint8_t,16384> buffer{},destination{};std::size_t used=0;
        bool ended=false;unsigned cell=0,cell_bits=0;std::uint64_t position=0,endpoint=0,remaining=stats.spool_bytes;
        while(remaining) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,buffer.size()));
            if(std::fread(buffer.data(),1,count,plain.get())!=count)throw Error("Fast receive spool read failed");
            remaining-=count;
            for(std::size_t b=0;b<count;++b)for(unsigned i=0;i<8;++i) {
                const auto bit=(buffer[b]>>(7-i))&1U;++position;
                if(ended) {if(bit)throw Error("Noncanonical fast source fill");continue;}
                cell=(cell<<1)|bit;
                if(++cell_bits!=9)continue;
                if(!(cell&256)) {
                    if(cell)throw Error("Invalid fast endpoint cell");
                    ended=true;endpoint=position;
                }else {
                    if(output->bytes>=quota-stats.spool_bytes)throw Error("Fast combined spool quota exceeded");
                    destination[used++]=static_cast<std::uint8_t>(cell);++output->bytes;
                    if(used==destination.size()) {write_file(output->file.get(),destination);used=0;}
                }
                cell=0;cell_bits=0;
            }
        }
        if(!ended)throw Error("Fast mandatory source endpoint missing");
        const auto cycle_bits=static_cast<std::uint64_t>(profile.interleave_depth)*source_bytes_per_group(profile,crypto.has_value())*8;
        if(endpoint<=position-cycle_bits)throw Error("Extra noncanonical fast padding cycle");
        write_file(output->file.get(),std::span(destination).first(used));rewind_file(output->file.get());
        stats.source_bytes=output->bytes;stats.spool_bytes+=output->bytes;
        return output;
    }
};
StreamDecoder::StreamDecoder(Profile p,const std::optional<Crypto>& c,std::uint64_t quota):impl_(std::make_unique<Impl>(p,c,quota)){}
StreamDecoder::~StreamDecoder()=default;
StreamDecoder::StreamDecoder(StreamDecoder&&) noexcept=default;
StreamDecoder& StreamDecoder::operator=(StreamDecoder&&) noexcept=default;
void StreamDecoder::push_interval(std::span<const float> soft_bits) {
    if(soft_bits.size()!=physical_interval_bits)throw Error("Fast interval must have exactly 2048 positions");
    if(impl_->stats.physical_end)throw Error("Fast reception already physically ended");
    if(impl_->stats.intervals==std::numeric_limits<std::uint64_t>::max())throw Error("Fast interval counter exhausted");
    ++impl_->stats.intervals;if(impl_->stats.failed)return;
    impl_->soft.insert(impl_->soft.end(),soft_bits.begin(),soft_bits.end());
    if(impl_->soft.size()==cycle_intervals(impl_->profile)*physical_interval_bits) {
        try {impl_->cycle();}catch(const std::exception& error) {impl_->soft.clear();impl_->fail(error.what());}
    }
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
        impl_->received=std::shared_ptr<const ReceivedFile>(new ReceivedFile(std::move(output)));
        impl_->stats.complete=true;impl_->stats.authenticated=impl_->crypto.has_value();
        impl_->stats.status=impl_->crypto?"Verified and physically complete":"Checksum-verified and physically complete (not authenticated)";
    }catch(const std::exception& error) {impl_->fail(error.what());}
}
DecodeSnapshot StreamDecoder::snapshot()const{return impl_->stats;}
std::shared_ptr<const ReceivedFile> StreamDecoder::result()const{return impl_->received;}

namespace testing {
StreamEncoder deterministic_encoder(Profile p,const Crypto& crypto,SourceReader source,std::uint64_t seed) {
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
    return StreamEncoder(p,crypto,std::move(source),std::move(random));
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
