#include "datapump/helper_metadata.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <bit>
#include <memory>

namespace datapump::helper {
namespace {
constexpr unsigned clear_type=1,encrypted_type=2;
constexpr std::string_view key_label="datapump/helper-time/v2/ff1-key";
using Cipher=std::unique_ptr<EVP_CIPHER,decltype(&EVP_CIPHER_free)>;
using Context=std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)>;

template<std::size_t N> struct Secret {
    std::array<std::uint8_t,N> bytes{};
    Secret()=default;
    Secret(const Secret&)=delete;
    Secret& operator=(const Secret&)=delete;
    ~Secret() {OPENSSL_cleanse(bytes.data(),bytes.size());}
};
struct CleanBytes {
    Bytes& bytes;
    ~CleanBytes() {OPENSSL_cleanse(bytes.data(),bytes.size());}
};

// NIST SP 800-38G FF1, specialized to radix 2, 20..48 digits, empty tweak.
// u,v<=24; b=ceil(v/8)<=3 and d=4*ceil(b/4)+4=8. Thus P and Q are one block
// each, and S is exactly the first eight bytes of CBC-MAC(P||Q). This is the
// standard ten-round FF1 permutation, not a truncated AES/CTR construction.
std::uint64_t ff1(std::uint64_t value,unsigned bits,const Crypto& crypto,bool decrypt) {
    if(bits<20||bits>48||value>=(std::uint64_t{1}<<bits))
        throw Error("invalid Helper Carrier FF1 time domain");
    Cipher cipher(EVP_CIPHER_fetch(nullptr,"AES-256-ECB",nullptr),EVP_CIPHER_free);
    if(!cipher||EVP_CIPHER_get_key_length(cipher.get())!=32||EVP_CIPHER_get_block_size(cipher.get())!=16)
        throw Error("Helper Carrier encrypted time requires AES-256 support");
    Secret<32> key;
    {
        const auto label=std::span(reinterpret_cast<const std::uint8_t*>(key_label.data()),key_label.size());
        auto derived=crypto.mac(label);
        const CleanBytes clean{derived};
        if(derived.size()!=key.bytes.size())throw Error("Helper Carrier FF1 key derivation failed");
        std::copy(derived.begin(),derived.end(),key.bytes.begin());
    }
    Context context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    if(!context||EVP_EncryptInit_ex2(context.get(),cipher.get(),key.bytes.data(),nullptr,nullptr)!=1||
       EVP_CIPHER_CTX_set_padding(context.get(),0)!=1)
        throw Error("Helper Carrier FF1 cipher initialization failed");
    const auto encrypt_block=[&](const std::array<std::uint8_t,16>& input,
                                 std::array<std::uint8_t,16>& output) {
        int written=0;
        if(EVP_EncryptUpdate(context.get(),output.data(),&written,input.data(),16)!=1||written!=16)
            throw Error("Helper Carrier FF1 cipher operation failed");
    };
    const unsigned u=bits/2,v=bits-u,b=(v+7)/8;
    // P=[1]1||[2]1||[1]1||[radix]3||[10]1||[u]1||[n]4||[t=0]4.
    const std::array<std::uint8_t,16> p{1,2,1,0,0,2,10,static_cast<std::uint8_t>(u),
                                      0,0,0,static_cast<std::uint8_t>(bits),0,0,0,0};
    Secret<16> first_mac,input,result;
    encrypt_block(p,first_mac.bytes);
    struct State {
        std::uint64_t a,b,y=0;
        ~State() {OPENSSL_cleanse(this,sizeof(*this));}
    } state{value>>v,value&((std::uint64_t{1}<<v)-1)};
    for(unsigned step=0;step<10;++step) {
        const auto round=decrypt?9-step:step;
        const auto half=decrypt?state.a:state.b;
        input.bytes=first_mac.bytes;
        // Q=zero(15-b) || [round]1 || [NUM2(half)]b.
        input.bytes[15-b]^=static_cast<std::uint8_t>(round);
        for(unsigned i=0;i<b;++i)
            input.bytes[15-i]^=static_cast<std::uint8_t>(half>>(8*i));
        encrypt_block(input.bytes,result.bytes);
        state.y=0;
        for(unsigned i=0;i<8;++i)state.y=(state.y<<8)|result.bytes[i];
        const auto m=(round%2)?v:u;
        const auto mask=(std::uint64_t{1}<<m)-1;
        if(decrypt) {
            const auto c=(state.b-state.y)&mask;
            state.b=state.a;state.a=c;
        } else {
            const auto c=(state.a+state.y)&mask;
            state.a=state.b;state.b=c;
        }
    }
    int final_size=0;
    if(EVP_EncryptFinal_ex(context.get(),result.bytes.data(),&final_size)!=1||final_size!=0)
        throw Error("Helper Carrier FF1 cipher finalization failed");
    return (state.a<<v)|state.b;
}

std::uint8_t crc_bit(std::uint8_t crc,unsigned bit) {
    const auto feedback=((crc>>6)^bit)&1U;
    return static_cast<std::uint8_t>(((crc<<1)&0x7fU)^(feedback?0x09U:0U));
}
unsigned width(std::uint64_t value) {
    return std::max(1U,static_cast<unsigned>(std::bit_width(value)));
}
struct Writer {
    MetadataPacket packet;
    std::size_t position=0;
    std::uint8_t checksum=0;
    void append(std::uint64_t value,unsigned bits) {
        while(bits) {
            const auto bit=static_cast<unsigned>((value>>--bits)&1U);
            packet.symbols[position/21]|=bit<<(20-position%21);
            checksum=crc_bit(checksum,bit);
            ++position;
        }
    }
    MetadataPacket finish() {
        packet.count=(position+20)/21;
        if(packet.count*21-position>=7)append(checksum,7);
        return packet;
    }
};
struct Reader {
    std::span<const std::uint32_t> symbols;
    std::size_t position=0;
    std::uint8_t checksum=0;
    std::uint64_t read(unsigned bits) {
        std::uint64_t value=0;
        while(bits--) {
            const auto bit=(symbols[position/21]>>(20-position%21))&1U;
            value=(value<<1)|bit;
            checksum=crc_bit(checksum,bit);
            ++position;
        }
        return value;
    }
    bool complete() {
        if(symbols.size()!=(position+20)/21)return false;
        if(symbols.size()*21-position>=7) {
            const auto expected=checksum;
            if(read(7)!=expected)return false;
        }
        while(position<symbols.size()*21)if(read(1))return false;
        return true;
    }
};
} // namespace

MetadataPacket callsign_packet(std::string_view callsign) {
    if(callsign.size()>19)throw Error("Helper Carrier callsign must contain at most 19 seven-bit ASCII characters");
    Writer writer;
    writer.append(0,2);
    writer.append(callsign.size(),5);
    for(const auto c:callsign) {
        const auto byte=static_cast<unsigned char>(c);
        if(byte>127)throw Error("Helper Carrier callsign must be seven-bit ASCII");
        writer.append(byte,7);
    }
    return writer.finish();
}

MetadataPacket timecode_packet(std::uint64_t ticks,const Crypto* crypto) {
    if(ticks>=(std::uint64_t{1}<<48))throw Error("Helper Carrier time must fit 48 bits");
    const auto bits=std::max(crypto?20U:1U,width(ticks));
    Writer writer;
    writer.append(crypto?encrypted_type:clear_type,2);
    writer.append(bits-1,6);
    writer.append(crypto?ff1(ticks,bits,*crypto,false):ticks,bits);
    return writer.finish();
}

std::optional<Metadata> decode_metadata_packet(std::span<const std::uint32_t> symbols,
                                               std::span<const Crypto> receive_keys) {
    if(symbols.empty()||symbols.size()>7||
       std::any_of(symbols.begin(),symbols.end(),[](auto symbol){return symbol>=(1U<<21);}))
        return std::nullopt;
    Reader reader{symbols};
    const auto type=reader.read(2);
    if(type==0) {
        const auto count=static_cast<std::size_t>(reader.read(5));
        if(count>19||7+count*7>symbols.size()*21)return std::nullopt;
        Metadata result;
        for(std::size_t i=0;i<count;++i)result.callsign+=static_cast<char>(reader.read(7));
        if(!reader.complete())return std::nullopt;
        return result;
    }
    if(type!=clear_type&&type!=encrypted_type)return std::nullopt;
    const auto bits=static_cast<unsigned>(reader.read(6))+1;
    if(bits>48||8+bits>symbols.size()*21||(type==encrypted_type&&(bits<20||receive_keys.empty())))
        return std::nullopt;
    auto ticks=reader.read(bits);
    if(!reader.complete())return std::nullopt;
    if(type==encrypted_type) {
        try {ticks=ff1(ticks,bits,receive_keys.front(),true);}
        catch(const Error&) {return std::nullopt;}
    }
    if(bits!=std::max(type==encrypted_type?20U:1U,width(ticks)))return std::nullopt;
    return Metadata{type==clear_type?MetadataKind::clear_time:MetadataKind::encrypted_time,{},ticks,false};
}

} // namespace datapump::helper
