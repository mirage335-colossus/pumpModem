#include "datapump/helper_metadata.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace datapump::helper {
namespace {
constexpr std::string_view key_label="datapump/helper-aux/v3/ctr-key";
using Cipher=std::unique_ptr<EVP_CIPHER,decltype(&EVP_CIPHER_free)>;
using Context=std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)>;

template<std::size_t N> struct Secret {
    std::array<std::uint8_t,N> bytes{};
    ~Secret() {OPENSSL_cleanse(bytes.data(),bytes.size());}
};
struct CleanBytes {
    Bytes& bytes;
    ~CleanBytes() {OPENSSL_cleanse(bytes.data(),bytes.size());}
};
std::uint32_t time_phase(std::uint64_t ticks) {
    return static_cast<std::uint32_t>((ticks%metadata_period_q16)*2/65);
}
std::uint64_t nearest_dwell(std::uint64_t ticks) {
    // round(ticks * 5 / 131072), without overflowing a uint64 Q16 clock.
    return (ticks/131072)*5+((ticks%131072)*5+65536)/131072;
}
std::optional<std::uint64_t> dwell_ticks(std::uint64_t dwell) {
    const auto whole=dwell/5;
    const auto fraction=((dwell%5)*131072+2)/5;
    if(whole>(UINT64_MAX-fraction)/131072)return std::nullopt;
    return whole*131072+fraction;
}
std::optional<std::uint64_t> absolute_dwell(std::uint32_t phase,std::uint64_t anchor) {
    // A phase cell is 32.5 Q16 ticks wide. Reconstruct its center nearest the
    // capture clock, then snap to the public rational 400-ms dwell calendar.
    // Four times the coordinate retains quarter-tick centers exactly.
    constexpr auto period4=static_cast<std::int64_t>(metadata_period_q16*4);
    auto delta=static_cast<std::int64_t>(65*(2*phase+1))-
        static_cast<std::int64_t>((anchor%metadata_period_q16)*4);
    if(delta>period4/2)delta-=period4;
    else if(delta< -period4/2)delta+=period4;
    const auto correction=delta>=0?(delta+2)/4:-((-delta+2)/4);
    std::uint64_t reconstructed=anchor;
    if(correction<0) {
        if(anchor<static_cast<std::uint64_t>(-correction))return std::nullopt;
        reconstructed-=static_cast<std::uint64_t>(-correction);
    } else {
        if(anchor>UINT64_MAX-static_cast<std::uint64_t>(correction))return std::nullopt;
        reconstructed+=static_cast<std::uint64_t>(correction);
    }
    const auto dwell=nearest_dwell(reconstructed);
    const auto nominal=dwell_ticks(dwell);
    if(!nominal||time_phase(*nominal)!=phase)return std::nullopt;
    return dwell;
}
void crypt_characters(MetadataPacket& packet,const Crypto& crypto,std::uint64_t dwell) {
    Cipher cipher(EVP_CIPHER_fetch(nullptr,"AES-256-CTR",nullptr),EVP_CIPHER_free);
    if(!cipher||EVP_CIPHER_get_key_length(cipher.get())!=32||EVP_CIPHER_get_iv_length(cipher.get())!=16)
        throw Error("Helper Carrier encrypted metadata requires AES-256-CTR support");
    Secret<32> key;
    {
        const auto label=std::span(reinterpret_cast<const std::uint8_t*>(key_label.data()),key_label.size());
        auto derived=crypto.mac(label);
        const CleanBytes clean{derived};
        if(derived.size()!=key.bytes.size())throw Error("Helper Carrier CTR key derivation failed");
        std::copy(derived.begin(),derived.end(),key.bytes.begin());
    }
    std::array<std::uint8_t,16> iv{};
    const auto bucket=dwell/15;
    for(unsigned i=0;i<8;++i)iv[7-i]=static_cast<std::uint8_t>(bucket>>(8*i));
    iv[11]=static_cast<std::uint8_t>(dwell%15);
    Secret<14> input,output;
    for(unsigned bit=0;bit<112;++bit)
        input.bytes[bit/8]|=static_cast<std::uint8_t>(
            ((packet.symbols[1+bit/14]>>(13-bit%14))&1U)<<(7-bit%8));
    Context context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    if(!context||EVP_EncryptInit_ex2(context.get(),cipher.get(),key.bytes.data(),iv.data(),nullptr)!=1)
        throw Error("Helper Carrier CTR cipher initialization failed");
    int written=0,final_size=0;
    Secret<16> final;
    if(EVP_EncryptUpdate(context.get(),output.bytes.data(),&written,input.bytes.data(),input.bytes.size())!=1||
       written!=static_cast<int>(output.bytes.size())||
       EVP_EncryptFinal_ex(context.get(),final.bytes.data(),&final_size)!=1||final_size!=0)
        throw Error("Helper Carrier CTR cipher operation failed");
    std::fill(packet.symbols.begin()+1,packet.symbols.end(),0);
    for(unsigned bit=0;bit<112;++bit)
        packet.symbols[1+bit/14]|=((output.bytes[bit/8]>>(7-bit%8))&1U)<<(13-bit%14);
}
} // namespace

MetadataPacket metadata_packet(std::uint64_t ticks,std::string_view callsign,const Crypto* key) {
    if(callsign.size()>metadata_max_characters)
        throw Error("Helper Carrier callsign must contain at most 16 seven-bit ASCII characters");
    MetadataPacket packet;
    packet.count=metadata_symbol_count;
    packet.symbols[0]=time_phase(ticks);
    for(std::size_t i=0;i<callsign.size();++i) {
        const auto c=static_cast<unsigned char>(callsign[i]);
        if(c==0||c>127)throw Error("Helper Carrier callsign must contain non-NUL seven-bit ASCII");
        packet.symbols[1+i/2]|=static_cast<std::uint32_t>(c)<<(i%2?0:7);
    }
    if(key) {
        const auto dwell=nearest_dwell(ticks);
        if(dwell_ticks(dwell)!=ticks)throw Error("Helper Carrier encrypted metadata requires a nominal dwell start");
        crypt_characters(packet,*key,dwell);
    }
    return packet;
}

MetadataPacket callsign_packet(std::string_view callsign) {
    return metadata_packet(0,callsign);
}

MetadataPacket timecode_packet(std::uint64_t ticks,const Crypto* key) {
    return metadata_packet(ticks,{},key);
}

std::optional<Metadata> decode_metadata_packet(std::span<const std::uint32_t> symbols,
                                               std::span<const Crypto> receive_keys,
                                               std::optional<std::uint64_t> anchor) {
    // Encryption mode is local configuration; it is never inferred from data.
    if(symbols.size()!=metadata_symbol_count||
       std::any_of(symbols.begin(),symbols.end(),[](auto symbol){return symbol>=(1U<<14);}))
        return std::nullopt;
    MetadataPacket packet;
    std::copy(symbols.begin(),symbols.end(),packet.symbols.begin());
    if(!receive_keys.empty()) {
        if(!anchor)return std::nullopt;
        const auto dwell=absolute_dwell(symbols[0],*anchor);
        if(!dwell)return std::nullopt;
        try {crypt_characters(packet,receive_keys.front(),*dwell);}
        catch(const Error&) {return std::nullopt;}
    }
    Metadata result;
    result.time_ticks=symbols[0];
    if(!receive_keys.empty())result.kind=MetadataKind::encrypted_time;
    bool padding=false;
    for(std::size_t i=0;i<metadata_max_characters;++i) {
        const auto c=(packet.symbols[1+i/2]>>(i%2?0:7))&127U;
        if(c==0)padding=true;
        else if(padding)return std::nullopt;
        else result.callsign+=static_cast<char>(c);
    }
    return result;
}

} // namespace datapump::helper
