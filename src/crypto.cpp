#include "datapump/crypto.hpp"
#include "datapump/serpent.hpp"
#include "datapump/execution.hpp"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <wasi/api.h>
#endif
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/random.h>
#endif
#endif

namespace datapump {
// The two immutable lanes are independently derived and never concatenated as
// input to any cipher-key KDF. Value copies share only storage ownership.
struct Crypto::ConfidentialityMaterial {
    mutable std::atomic<std::size_t> owners{1};
    std::array<std::uint8_t,64> serpent;
    std::array<std::uint8_t,64> aes;
    ConfidentialityMaterial(std::span<const std::uint8_t,64> s,
                            std::span<const std::uint8_t,64> a) noexcept {
        std::copy(s.begin(),s.end(),serpent.begin());
        std::copy(a.begin(),a.end(),aes.begin());
    }
    ~ConfidentialityMaterial(){OPENSSL_cleanse(serpent.data(),serpent.size());OPENSSL_cleanse(aes.data(),aes.size());}
    void retain() const noexcept {owners.fetch_add(1,std::memory_order_relaxed);}
    void release() const noexcept {
        if(owners.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;
    }
};
struct KeyringAccess {
    static constexpr std::size_t bytes=128;
    static void encode(const Crypto& key,std::span<std::uint8_t> output);
    static Crypto decode(std::span<const std::uint8_t> input);
    static std::span<const std::uint8_t,64> root(const Crypto& key,bool serpent);
    static void collect_audio(const KeyfileGenerationOptions&,std::span<std::uint8_t,64>);
};
namespace {
using View = std::span<const std::uint8_t>;
using MdContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using Kdf = std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)>;
using KdfContext = std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)>;
using Mac = std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)>;
using MacContext = std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)>;
struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

constexpr std::string_view hkdf_salt = "datapump/v1/hkdf-sha256";
constexpr std::array<std::string_view, 5> purpose_labels{
    "datapump/v1/data", "datapump/v1/dsss", "datapump/v1/scrambler",
    "datapump/v1/fhss", "datapump/v1/mac"};
constexpr std::size_t prefix_size = 48;
constexpr std::size_t chunk_size = 65536;
constexpr testing::KeyfilePolicy normal_policy{keyfile_header_bytes, minimum_keyfile_pad_bytes};

template <std::size_t N> struct Secret {
    std::array<std::uint8_t, N> bytes{};
    Secret() = default;
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    Secret(Secret&& other) noexcept : bytes(other.bytes) {
        OPENSSL_cleanse(other.bytes.data(), N);
    }
    ~Secret() { OPENSSL_cleanse(bytes.data(), N); }
};

View view(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}
void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
// Obtain bytes directly from the platform CSPRNG, bypassing OpenSSL's default
// AES-CTR DRBG for confidentiality roots and new keyfile construction.
void system_random_fill(std::span<std::uint8_t> data) {
    for(std::size_t offset=0;offset<data.size();) {
#ifdef _WIN32
        const auto count=std::min<std::size_t>(data.size()-offset,65536);
        require(BCryptGenRandom(nullptr,data.data()+offset,static_cast<ULONG>(count),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,
                "Windows system random generation failed");
#elif defined(__EMSCRIPTEN__)
        // getentropy() is restricted to 256 bytes. The same WASI random_get
        // adapter accepts a whole WebCrypto request (at most 65,536 bytes),
        // avoiding half a million crossings for a production random header.
        const auto count=std::min<std::size_t>(data.size()-offset,65536);
        require(__wasi_random_get(data.data()+offset,count)==0,"browser system random generation failed");
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
        const auto count=std::min<std::size_t>(data.size()-offset,65536);
        ssize_t result;
        do {result=::getrandom(data.data()+offset,count,0);} while(result<0 && errno==EINTR);
        require(result>0,"system random generation failed");
        offset+=static_cast<std::size_t>(result);continue;
#else
        const auto count=std::min<std::size_t>(data.size()-offset,256);
        int result;
        do {result=::getentropy(data.data()+offset,count);} while(result!=0 && errno==EINTR);
        require(result==0,"system random generation failed");
#endif
        offset+=count;
    }
}
void put_u64(std::span<std::uint8_t> target, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        target[7 - i] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8;
    }
}
std::uint64_t get_u64(View source) {
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < 8; ++i) result = (result << 8) | source[i];
    return result;
}

Secret<32> hkdf(View input, View info) {
    Kdf algorithm(EVP_KDF_fetch(nullptr, "HKDF", nullptr), EVP_KDF_free);
    require(static_cast<bool>(algorithm), "OpenSSL HKDF unavailable");
    KdfContext context(EVP_KDF_CTX_new(algorithm.get()), EVP_KDF_CTX_free);
    require(static_cast<bool>(context), "OpenSSL HKDF allocation failed");
    char digest[] = "SHA256";
    int mode = EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND;
    OSSL_PARAM params[]{
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<std::uint8_t*>(input.data()), input.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(hkdf_salt.data()), hkdf_salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<std::uint8_t*>(info.data()), info.size()),
        OSSL_PARAM_construct_end()
    };
    Secret<32> result;
    require(EVP_KDF_derive(context.get(), result.bytes.data(), result.bytes.size(), params) == 1,
            "OpenSSL HKDF derivation failed");
    return result;
}

Secret<64> cascade_kdf(View input,View info) {
    constexpr std::string_view salt="datapump/cascade/v2/hkdf-sha512";
    Kdf algorithm(EVP_KDF_fetch(nullptr,"HKDF",nullptr),EVP_KDF_free);
    require(static_cast<bool>(algorithm),"OpenSSL HKDF unavailable");
    KdfContext context(EVP_KDF_CTX_new(algorithm.get()),EVP_KDF_CTX_free);
    require(static_cast<bool>(context),"OpenSSL HKDF allocation failed");
    char digest[]="SHA512";int mode=EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND;
    OSSL_PARAM params[]{
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,digest,0),
        OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE,&mode),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,const_cast<std::uint8_t*>(input.data()),input.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,const_cast<char*>(salt.data()),salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,const_cast<std::uint8_t*>(info.data()),info.size()),
        OSSL_PARAM_construct_end()};
    Secret<64> result;
    require(EVP_KDF_derive(context.get(),result.bytes.data(),result.bytes.size(),params)==1,"OpenSSL SHA512 HKDF derivation failed");
    return result;
}

template<std::size_t N> Secret<N> hmac_digest(View key,View message,const char* name) {
    Mac algorithm(EVP_MAC_fetch(nullptr,"HMAC",nullptr),EVP_MAC_free);
    require(static_cast<bool>(algorithm),"OpenSSL HMAC unavailable");
    MacContext context(EVP_MAC_CTX_new(algorithm.get()),EVP_MAC_CTX_free);
    char digest[16]{};require(std::strlen(name)<sizeof(digest),"invalid HMAC digest");std::strcpy(digest,name);
    OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,digest,0),OSSL_PARAM_construct_end()};
    require(context && EVP_MAC_init(context.get(),key.data(),key.size(),params)==1 && EVP_MAC_update(context.get(),message.data(),message.size())==1,"OpenSSL HMAC initialization failed");
    Secret<N> result;std::size_t count=0;
    require(EVP_MAC_final(context.get(),result.bytes.data(),&count,result.bytes.size())==1 && count==result.bytes.size(),"OpenSSL HMAC finalization failed");return result;
}
Bytes hmac_sha256(View key,View message) {
    const auto tag=hmac_digest<32>(key,message,"SHA256");return Bytes(tag.bytes.begin(),tag.bytes.end());
}

Bytes keyfile_auth_input(View prefix,View outer) {
    const auto label=view("datapump/v4/keyfile-auth/");Bytes input(label.begin(),label.end());input.insert(input.end(),prefix.begin(),prefix.end());
    const auto old=input.size();input.resize(old+8);put_u64(std::span(input).subspan(old,8),outer.size());input.insert(input.end(),outer.begin(),outer.end());return input;
}

Bytes contextual_info(std::string_view domain,View context,View salt={}) {
    Bytes info(domain.begin(),domain.end());
    auto append=[&](View bytes){const auto old=info.size();info.resize(old+8);put_u64(std::span(info).subspan(old,8),bytes.size());info.insert(info.end(),bytes.begin(),bytes.end());};
    append(context);append(salt);return info;
}

void serpent_ctr_xor(std::span<std::uint8_t> bytes,View key,std::array<std::uint8_t,16> counter,
                     std::size_t skip=0) {
    Serpent256 serpent(std::span<const std::uint8_t,32>(key.data(),32));
    Secret<16> block;
    for(std::size_t offset=0;offset<bytes.size();) {
        serpent.encrypt_blocks(counter.data(),block.bytes.data(),1);
        const auto count=std::min(bytes.size()-offset,16-skip);
        for(std::size_t j=0;j<count;++j)bytes[offset+j]^=block.bytes[skip+j];
        offset+=count;skip=0;
        for(std::size_t j=counter.size();j>0;--j)if(++counter[j-1]!=0)break;
    }
    OPENSSL_cleanse(counter.data(),counter.size());
}

Bytes aes_cipher(View input,View key,View iv,const EVP_CIPHER* algorithm,bool encrypt) {
    require(input.size()<=static_cast<std::size_t>(std::numeric_limits<int>::max()),"cipher input exceeds bounded operation");
    CipherContext context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    Bytes output(input.size()+16);int count=0,tail=0;
    struct WipeOnError {Bytes& bytes;bool success=false;~WipeOnError(){if(!success)OPENSSL_cleanse(bytes.data(),bytes.size());}} guard{output};
    require(context && EVP_CipherInit_ex(context.get(),algorithm,nullptr,key.data(),iv.data(),encrypt?1:0)==1 &&
        EVP_CIPHER_CTX_set_padding(context.get(),0)==1 &&
        EVP_CipherUpdate(context.get(),output.data(),&count,input.data(),static_cast<int>(input.size()))==1 &&
        EVP_CipherFinal_ex(context.get(),output.data()+count,&tail)==1 &&
        static_cast<std::size_t>(count+tail)==input.size(),"AES cascade operation failed");
    output.resize(input.size());guard.success=true;return output;
}

void serpent_cbc_prepared(std::span<std::uint8_t> data,const Serpent256& serpent,View iv,bool encrypt) {
    Secret<16> previous,saved;
    std::copy_n(iv.begin(),16,previous.bytes.begin());
    for(std::size_t offset=0;offset<data.size();offset+=16) {
        auto* block=data.data()+offset;
        if(encrypt){for(std::size_t j=0;j<16;++j)block[j]^=previous.bytes[j];serpent.encrypt_blocks(block,block,1);std::copy_n(block,16,previous.bytes.begin());}
        else {std::copy_n(block,16,saved.bytes.begin());serpent.decrypt_blocks(block,block,1);for(std::size_t j=0;j<16;++j)block[j]^=previous.bytes[j];previous.bytes=saved.bytes;}
    }
}

class Hash512 {
public:
    Hash512():context_(EVP_MD_CTX_new(),EVP_MD_CTX_free) {
        require(context_ && EVP_DigestInit_ex(context_.get(),EVP_sha512(),nullptr)==1,"OpenSSL SHA512 initialization failed");
    }
    void update(View data){require(EVP_DigestUpdate(context_.get(),data.data(),data.size())==1,"OpenSSL SHA512 update failed");}
    Secret<64> finish(){Secret<64> result;unsigned size=0;
        require(EVP_DigestFinal_ex(context_.get(),result.bytes.data(),&size)==1 && size==64,"OpenSSL SHA512 finalization failed");return result;}
private:MdContext context_;
};

std::uint64_t regular_file_size(const std::filesystem::path& path) {
    std::error_code ec;
    require(std::filesystem::is_regular_file(path, ec) && !ec, "keyfile or pad is not a readable regular file");
    const auto size = std::filesystem::file_size(path, ec);
    require(!ec && size <= std::numeric_limits<std::uint64_t>::max(), "cannot determine keyfile or pad length");
    return static_cast<std::uint64_t>(size);
}
std::uint64_t pad_size(const std::optional<std::filesystem::path>& pad,
                       const testing::KeyfilePolicy& policy) {
    if (!pad) return 0;
    const auto size = regular_file_size(*pad);
    require(size >= policy.minimum_pad_bytes, "pad must be larger than 1 GiB (or meet the explicit test policy)");
    return size;
}
void validate_policy(const testing::KeyfilePolicy& policy) {
    require(policy.header_bytes > 0 && policy.header_bytes <= keyfile_header_bytes,
            "invalid keyfile header size policy");
    require(policy.minimum_pad_bytes > 0, "invalid keyfile pad size policy");
}
void read_exact(std::ifstream& input, std::span<std::uint8_t> buffer) {
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    require(static_cast<bool>(input), "keyfile or pad was truncated or could not be read");
}
void require_end(std::ifstream& input) {
    char extra;
    input.get(extra);
    require(input.eof() && !input.bad(), "unexpected trailing bytes or read error");
}

File exclusive_output(const std::filesystem::path& path) {
#ifdef _WIN32
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr) != 0,
            "cannot construct owner-only keyfile permissions");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    require(handle != INVALID_HANDLE_VALUE, "cannot create keyfile exclusively (file may already exist)");
    int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_BINARY | _O_WRONLY);
    if (fd == -1) {
        CloseHandle(handle);
        throw Error("cannot attach keyfile handle");
    }
    auto* output = _fdopen(fd, "wb");
    if (!output) _close(fd);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd != -1, "cannot create keyfile exclusively (file may already exist)");
    auto* output = ::fdopen(fd, "wb");
    if (!output) ::close(fd);
#endif
    require(output != nullptr, "cannot attach keyfile output stream");
    return File(output);
}
void write_exact(std::FILE* output, View bytes) {
    require(std::fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size(), "keyfile write failed");
}
void flush_file(File& output) {
    require(std::fflush(output.get()) == 0, "keyfile flush failed");
#ifdef _WIN32
    require(_commit(_fileno(output.get())) == 0, "keyfile synchronization failed");
#else
    require(::fsync(::fileno(output.get())) == 0, "keyfile synchronization failed");
#endif
    require(std::fclose(output.release()) == 0, "keyfile close failed");
}

struct SecretBytes {
    Bytes bytes;
    explicit SecretBytes(std::size_t count):bytes(count){}
    SecretBytes(const SecretBytes&)=delete;
    ~SecretBytes(){OPENSSL_cleanse(bytes.data(),bytes.size());}
};
bool valid_key_name(std::string_view name) {
    if(name.empty() || name.size()>64) return false;
    for(std::size_t i=0;i<name.size();) {
        const auto first=static_cast<std::uint8_t>(name[i++]);
        if(first<32 || first==127) return false;
        if(first<128) continue;
        unsigned count;std::uint32_t value,minimum;
        if(first>=0xc2 && first<=0xdf){count=1;value=first&31;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){count=2;value=first&15;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){count=3;value=first&7;minimum=0x10000;}
        else return false;
        if(count>name.size()-i) return false;
        while(count--){const auto next=static_cast<std::uint8_t>(name[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff) || (value>=0x80 && value<=0x9f))return false;
    }
    return true;
}
void put32(std::span<std::uint8_t> output,std::uint32_t value) {
    for(unsigned i=0;i<4;++i) output[3-i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint32_t get32(View input) {
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value=(value<<8)|input[i];return value;
}
constexpr std::array<std::uint8_t,8> cascade_ring_magic{'D','P','M','K','E','Y','0','4'};
constexpr std::string_view serpent_ring_hash_domain="datapump/v4/serpent/keyfile-hash-sha512";
constexpr std::string_view aes_ring_hash_domain="datapump/v4/aes/keyfile-hash-sha512";

void cancelled(std::stop_token stop) {require(!stop.stop_requested(),"Keyfile generation cancelled");}
void progress(const KeyfileGenerationOptions& options,std::string_view text) {
    cancelled(options.stop);if(options.progress)options.progress(text);
}
// Each byte belongs to exactly one wrapping lane. Offsets, lengths and the
// authenticated public prefix bind the partition; the optional secret pad uses
// the same independently framed partition rather than one digest split in two.
void hash_partition(Hash512& serpent,Hash512& aes,View bytes,std::uint64_t offset) {
    Secret<chunk_size> even,odd;
    std::size_t se=0,ae=0;
    for(std::size_t i=0;i<bytes.size();++i)
        if(((offset+i)&1U)==0)even.bytes[se++]=bytes[i];else odd.bytes[ae++]=bytes[i];
    serpent.update(std::span(even.bytes).first(se));aes.update(std::span(odd.bytes).first(ae));
}
void hash_partitioned_pad(Hash512& serpent,Hash512& aes,
                          const std::optional<std::filesystem::path>& pad,std::uint64_t length,
                          std::stop_token stop={}) {
    const auto marker=view("pad/");serpent.update(marker);aes.update(marker);
    std::array<std::uint8_t,8> size{};put_u64(size,length);serpent.update(size);aes.update(size);
    if(!pad)return;
    std::ifstream input(*pad,std::ios::binary);require(static_cast<bool>(input),"cannot open pad for reading");
    Secret<chunk_size> buffer;std::uint64_t offset=0;
    while(offset<length){cancelled(stop);const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(length-offset,buffer.bytes.size()));
        auto part=std::span(buffer.bytes).first(count);read_exact(input,part);hash_partition(serpent,aes,part,offset);offset+=count;execution::checkpoint();}
    require_end(input);
}
Bytes wrap_payload(View key,View nonce,View prefix,View plain) {
    CipherContext context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    require(context && EVP_EncryptInit_ex(context.get(),EVP_aes_256_gcm(),nullptr,key.data(),nonce.data())==1,"keyring encryption initialization failed");
    int count=0;
    require(EVP_EncryptUpdate(context.get(),nullptr,&count,prefix.data(),static_cast<int>(prefix.size()))==1,"keyring associated data failed");
    Bytes encrypted(plain.size()+16);
    require(EVP_EncryptUpdate(context.get(),encrypted.data(),&count,plain.data(),static_cast<int>(plain.size()))==1 && count==static_cast<int>(plain.size()),"keyring encryption failed");
    require(EVP_EncryptFinal_ex(context.get(),encrypted.data()+plain.size(),&count)==1 && count==0,"keyring encryption finalization failed");
    require(EVP_CIPHER_CTX_ctrl(context.get(),EVP_CTRL_GCM_GET_TAG,16,encrypted.data()+plain.size())==1,"keyring tag failed");
    return encrypted;
}
void unwrap_payload(View key,View nonce,View prefix,View encrypted,std::span<std::uint8_t> plain) {
    CipherContext context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    require(context && EVP_DecryptInit_ex(context.get(),EVP_aes_256_gcm(),nullptr,key.data(),nonce.data())==1,"keyring decryption initialization failed");
    int count=0;
    require(EVP_DecryptUpdate(context.get(),nullptr,&count,prefix.data(),static_cast<int>(prefix.size()))==1,"keyring associated data failed");
    require(EVP_DecryptUpdate(context.get(),plain.data(),&count,encrypted.data(),static_cast<int>(plain.size()))==1 && count==static_cast<int>(plain.size()),"keyring decryption failed");
    require(EVP_CIPHER_CTX_ctrl(context.get(),EVP_CTRL_GCM_SET_TAG,16,const_cast<std::uint8_t*>(encrypted.data()+plain.size()))==1,"keyring tag initialization failed");
    std::array<std::uint8_t,16> final{};
    require(EVP_DecryptFinal_ex(context.get(),final.data(),&count)==1 && count==0,"keyring authentication failed: wrong pad or corrupted keyfile");
}

std::array<std::uint8_t,16> keyfile_serpent_iv(View key,View nonce) {
    Bytes info;const auto label=view("datapump/v4/serpent/keyfile-iv/");
    info.insert(info.end(),label.begin(),label.end());info.insert(info.end(),nonce.begin(),nonce.end());
    const auto derived=cascade_kdf(key,info);std::array<std::uint8_t,16> iv{};
    std::copy_n(derived.bytes.begin(),16,iv.begin());return iv;
}

struct EntropyLanes {Secret<64> serpent,aes;KeyfileGenerationResult result;};
EntropyLanes timed_keyfile_entropy(const KeyfileGenerationOptions& options) {
    Secret<128> serpent_os,aes_os;
    progress(options,"Collecting independent operating-system entropy");
    system_random_fill(std::span(aes_os.bytes).first(64));
    system_random_fill(std::span(serpent_os.bytes).first(64));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    Secret<64> audio;KeyfileGenerationResult collection;
    progress(options,"Collecting three seconds of audio entropy");
    try {
        if(!options.audio)throw AudioEntropyUnavailable("no audio entropy provider is configured");
        KeyringAccess::collect_audio(options,audio.bytes);collection.audio_used=true;
    } catch(const AudioEntropyUnavailable& error) {
        cancelled(options.stop);
        if(!options.allow_audio_fallback)throw;
        collection.warning="Audio entropy unavailable; keyfile generated using separate operating-system entropy only: "+std::string(error.what());
        if(options.progress)options.progress(collection.warning);
    }
    do {cancelled(options.stop);execution::sleep_until(std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(25)));}
        while(std::chrono::steady_clock::now()<deadline);
    cancelled(options.stop);
    system_random_fill(std::span(aes_os.bytes).last(64));
    system_random_fill(std::span(serpent_os.bytes).last(64));
    // HMAC keyed by the OS-only Serpent conditioner prevents predictable or
    // adversarial audio from replacing otherwise secret OS source material.
    const auto os_serpent=cascade_kdf(serpent_os.bytes,view("datapump/v4/serpent/os-entropy"));
    Secret<128> audio_input;
    const auto label=view("datapump/v4/serpent/audio-mix/");
    std::copy(label.begin(),label.end(),audio_input.bytes.begin());
    audio_input.bytes[label.size()]=collection.audio_used?1:0;
    std::copy(audio.bytes.begin(),audio.bytes.end(),audio_input.bytes.begin()+static_cast<std::ptrdiff_t>(label.size()+1));
    EntropyLanes result;
    result.serpent.bytes=hmac_digest<64>(os_serpent.bytes,std::span(audio_input.bytes).first(label.size()+1+audio.bytes.size()),"SHA512").bytes;
    const auto ak=cascade_kdf(aes_os.bytes,view("datapump/v4/aes/keyfile-entropy"));
    result.aes.bytes=ak.bytes;result.result=std::move(collection);return result;
}
KeyfileGenerationResult create_keyring_impl(const std::filesystem::path& path,const std::vector<std::string>& names,
                        const testing::KeyfilePolicy& policy,const std::optional<std::filesystem::path>& pad,
                        const KeyfileGenerationOptions& generation) {
    validate_policy(policy);
    require(!names.empty() && names.size()<=128,"keyring must contain 1..128 named key sets");
    std::set<std::string> unique;std::size_t length=0;
    for(const auto& name:names) {
        require(valid_key_name(name),"key names must be 1..64 bytes of printable UTF-8");
        require(unique.insert(name).second,"duplicate key name");
        length+=2+name.size()+KeyringAccess::bytes;
    }
    const auto pad_length=pad_size(pad,policy);
    require(static_cast<bool>(generation.audio) || generation.allow_audio_fallback,"Keyfile generation requires fresh audio input");
    cancelled(generation.stop);
    auto output=exclusive_output(path);
    auto entropy=timed_keyfile_entropy(generation);
    SecretBytes plaintext(length);std::size_t offset=0,index=0;
    for(const auto& name:names) {
        cancelled(generation.stop);
        plaintext.bytes[offset++]=0;plaintext.bytes[offset++]=static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(),name.end(),plaintext.bytes.begin()+static_cast<std::ptrdiff_t>(offset));offset+=name.size();
        std::array<std::uint8_t,8> ordinal{};put_u64(ordinal,index++);
        const auto si=contextual_info("datapump/v4/serpent/key-entry/",ordinal);
        const auto ai=contextual_info("datapump/v4/aes/key-entry/",ordinal);
        const auto serpent=cascade_kdf(entropy.serpent.bytes,si),aes=cascade_kdf(entropy.aes.bytes,ai);
        const auto key=Crypto::from_independent_roots(serpent.bytes,aes.bytes);
        KeyringAccess::encode(key,std::span(plaintext.bytes).subspan(offset,KeyringAccess::bytes));offset+=KeyringAccess::bytes;
    }
    std::array<std::uint8_t,prefix_size> prefix{};
    std::copy(cascade_ring_magic.begin(),cascade_ring_magic.end(),prefix.begin());
    put_u64(std::span(prefix).subspan(8,8),policy.header_bytes);
    put_u64(std::span(prefix).subspan(16,8),pad_length);prefix[24]=static_cast<std::uint8_t>((pad?1:0)|(entropy.result.audio_used?2:0));
    put32(std::span(prefix).subspan(28,4),static_cast<std::uint32_t>(names.size()));
    system_random_fill(std::span(prefix).subspan(32,12));
    put32(std::span(prefix).subspan(44,4),static_cast<std::uint32_t>(length));
    Hash512 serpent_hash,aes_hash;serpent_hash.update(view(serpent_ring_hash_domain));aes_hash.update(view(aes_ring_hash_domain));
    serpent_hash.update(prefix);aes_hash.update(prefix);write_exact(output.get(),prefix);
    progress(generation,"Writing independently protected keyfile");
    Secret<chunk_size> buffer;std::uint64_t written=0;
    while(written<policy.header_bytes) {
        cancelled(generation.stop);
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(policy.header_bytes-written,buffer.bytes.size()));
        auto part=std::span(buffer.bytes).first(count);system_random_fill(part);hash_partition(serpent_hash,aes_hash,part,written);write_exact(output.get(),part);written+=count;
        execution::checkpoint();
    }
    hash_partitioned_pad(serpent_hash,aes_hash,pad,pad_length,generation.stop);
    const auto sd=serpent_hash.finish(),ad=aes_hash.finish();
    const auto sw=cascade_kdf(sd.bytes,view("datapump/v4/serpent/keyfile-wrap"));
    const auto aw=cascade_kdf(ad.bytes,view("datapump/v4/aes/keyfile-wrap"));
    auto inner_iv=keyfile_serpent_iv(std::span(sw.bytes).first(32),std::span(prefix).subspan(32,12));
    serpent_ctr_xor(plaintext.bytes,std::span(sw.bytes).first(32),inner_iv);
    const auto encrypted=wrap_payload(std::span(aw.bytes).first(32),std::span(prefix).subspan(32,12),prefix,plaintext.bytes);
    const auto auth_key=cascade_kdf(sd.bytes,view("datapump/v4/serpent/keyfile-auth"));
    const auto auth_input=keyfile_auth_input(prefix,encrypted);
    const auto tag=hmac_sha256(std::span(auth_key.bytes).first(32),auth_input);
    cancelled(generation.stop);write_exact(output.get(),encrypted);write_exact(output.get(),tag);flush_file(output);
    return entropy.result;
}
std::vector<KeyEntry> load_keyring_impl(const std::filesystem::path& path,const testing::KeyfilePolicy& policy,
                                      const std::optional<std::filesystem::path>& pad) {
    validate_policy(policy);const auto length=regular_file_size(path);require(length>=prefix_size,"invalid keyfile length");
    std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"cannot open keyfile");
    std::array<std::uint8_t,prefix_size> prefix{};read_exact(input,prefix);
    require(std::equal(cascade_ring_magic.begin(),cascade_ring_magic.end(),prefix.begin()),"unsupported keyfile format; generate a version 4 keyfile");
    constexpr auto entry_bytes=KeyringAccess::bytes;
    require(get_u64(std::span(prefix).subspan(8,8))==policy.header_bytes,"invalid keyfile header size");
    require(prefix[24]<=3 && prefix[25]==0 && prefix[26]==0 && prefix[27]==0,"unsupported keyring flags");
    const auto count=get32(std::span(prefix).subspan(28,4)),payload_length=get32(std::span(prefix).subspan(44,4));
    require(count>=1 && count<=128 && payload_length>=count*(2+1+entry_bytes) && payload_length<=count*(2+64+entry_bytes),"invalid keyring dimensions");
    require(length==prefix_size+policy.header_bytes+payload_length+48,"invalid keyring length");
    require(((prefix[24]&1U)!=0)==pad.has_value(),(prefix[24]&1U)?"this keyfile requires an optional external pad (CLI --pad)":"this keyfile does not use a pad");
    const auto pad_length=pad_size(pad,policy);require(get_u64(std::span(prefix).subspan(16,8))==pad_length,"pad length does not match keyfile");
    Hash512 serpent_hash,aes_hash;serpent_hash.update(view(serpent_ring_hash_domain));aes_hash.update(view(aes_ring_hash_domain));
    serpent_hash.update(prefix);aes_hash.update(prefix);Secret<chunk_size> buffer;
    for(std::uint64_t read=0;read<policy.header_bytes;) {
        const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(policy.header_bytes-read,buffer.bytes.size()));
        auto part=std::span(buffer.bytes).first(size);read_exact(input,part);hash_partition(serpent_hash,aes_hash,part,read);read+=size;
    }
    Bytes encrypted(payload_length+16);read_exact(input,encrypted);std::array<std::uint8_t,32> tag{};read_exact(input,tag);require_end(input);
    hash_partitioned_pad(serpent_hash,aes_hash,pad,pad_length);
    const auto sd=serpent_hash.finish(),ad=aes_hash.finish();
    const auto sw=cascade_kdf(sd.bytes,view("datapump/v4/serpent/keyfile-wrap"));
    const auto aw=cascade_kdf(ad.bytes,view("datapump/v4/aes/keyfile-wrap"));
    const auto auth_key=cascade_kdf(sd.bytes,view("datapump/v4/serpent/keyfile-auth"));
    const auto expected=hmac_sha256(std::span(auth_key.bytes).first(32),keyfile_auth_input(prefix,encrypted));
    require(CRYPTO_memcmp(expected.data(),tag.data(),tag.size())==0,"keyring authentication failed: wrong pad or corrupted keyfile");
    SecretBytes plaintext(payload_length);
    unwrap_payload(std::span(aw.bytes).first(32),std::span(prefix).subspan(32,12),prefix,encrypted,plaintext.bytes);
    auto inner_iv=keyfile_serpent_iv(std::span(sw.bytes).first(32),std::span(prefix).subspan(32,12));
    serpent_ctr_xor(plaintext.bytes,std::span(sw.bytes).first(32),inner_iv);
    std::vector<KeyEntry> entries;entries.reserve(count);std::set<std::string> names;std::size_t offset=0;
    for(std::uint32_t i=0;i<count;++i) {
        require(plaintext.bytes.size()-offset>=2,"truncated key entry");
        const std::size_t name_length=(static_cast<std::size_t>(plaintext.bytes[offset])<<8)|plaintext.bytes[offset+1];offset+=2;
        require(name_length<=64 && plaintext.bytes.size()-offset>=name_length+entry_bytes,"truncated named key set");
        const std::string name(plaintext.bytes.begin()+static_cast<std::ptrdiff_t>(offset),plaintext.bytes.begin()+static_cast<std::ptrdiff_t>(offset+name_length));offset+=name_length;
        require(valid_key_name(name) && names.insert(name).second,"invalid or duplicate key identity");
        entries.push_back({name,KeyringAccess::decode(std::span(plaintext.bytes).subspan(offset,entry_bytes))});offset+=entry_bytes;
    }
    require(offset==plaintext.bytes.size(),"trailing keyring data");return entries;
}

} // namespace

struct AudioEntropySink::Impl {
    Hash512 hash;
    std::uint32_t rate=0;
    std::uint64_t frames=0,target=0,clipped=0,repeat=0;
    std::array<std::uint32_t,32> distinct{};
    std::size_t distinct_count=0;
    std::uint32_t previous=0;
    bool finalized=false;
    std::exception_ptr failure;
    Impl(){hash.update(view("datapump/v4/serpent/physical-audio/float32-mono-be/"));}
    ~Impl(){OPENSSL_cleanse(distinct.data(),sizeof(distinct));OPENSSL_cleanse(&previous,sizeof(previous));}
    void add(std::span<const float> mono,std::uint32_t sample_rate) {
        if(failure)std::rethrow_exception(failure);
        try {
        require(!finalized,"audio entropy sink already finalized");
        if(!(sample_rate>=4000 && sample_rate<=384000))throw AudioEntropyUnavailable("audio entropy sample rate is unsupported");
        if(!rate){rate=sample_rate;target=std::uint64_t{rate}*3;
            std::array<std::uint8_t,16> format{};put_u64(std::span(format).first(8),rate);put_u64(std::span(format).last(8),1);hash.update(format);}
        if(!(rate==sample_rate))throw AudioEntropyUnavailable("audio entropy input format changed");
        Secret<1024> canonical;std::size_t used=0;
        for(const auto sample:mono) {
            if(frames==target)break;
            if(!(std::isfinite(sample) && std::abs(sample)<=1.0F))throw AudioEntropyUnavailable("audio entropy input contains invalid samples");
            const auto bits=std::bit_cast<std::uint32_t>(sample);
            if(frames && bits==previous)++repeat;else repeat=1;
            if(!(repeat<=std::max<std::uint64_t>(1,rate/10)))throw AudioEntropyUnavailable("audio entropy input is silent or constant");
            previous=bits;if(std::abs(sample)>=.999F)++clipped;
            if(distinct_count<distinct.size() && std::find(distinct.begin(),distinct.begin()+static_cast<std::ptrdiff_t>(distinct_count),bits)==distinct.begin()+static_cast<std::ptrdiff_t>(distinct_count))
                distinct[distinct_count++]=bits;
            for(unsigned byte=0;byte<4;++byte)canonical.bytes[used++]=static_cast<std::uint8_t>(bits>>(24-8*byte));
            ++frames;
            if(used==canonical.bytes.size()){hash.update(canonical.bytes);used=0;}
        }
        if(used)hash.update(std::span(canonical.bytes).first(used));
        if(frames==target){if(!(distinct_count>=distinct.size()))throw AudioEntropyUnavailable("audio entropy input has insufficient sample variation");
            if(!(clipped*100<frames*95))throw AudioEntropyUnavailable("audio entropy input is clipped");}
        }catch(...){failure=std::current_exception();throw;}
    }
    Secret<64> finish() {
        if(failure)std::rethrow_exception(failure);
        if(!(!finalized && target && frames==target))throw AudioEntropyUnavailable("fresh audio entropy capture was incomplete");
        if(!(distinct_count>=distinct.size() && clipped*100<frames*95))throw AudioEntropyUnavailable("audio entropy input is unusable");
        std::array<std::uint8_t,8> count{};put_u64(count,frames);hash.update(count);finalized=true;return hash.finish();
    }
};
AudioEntropySink::AudioEntropySink():impl_(std::make_unique<Impl>()){}
AudioEntropySink::~AudioEntropySink()=default;
void AudioEntropySink::add_samples(std::span<const float> mono,std::uint32_t sample_rate){impl_->add(mono,sample_rate);}
bool AudioEntropySink::complete() const noexcept {return !impl_->failure && impl_->target && impl_->frames==impl_->target;}
void KeyringAccess::collect_audio(const KeyfileGenerationOptions& options,std::span<std::uint8_t,64> output) {
    AudioEntropySink sink;cancelled(options.stop);options.audio(sink,options.stop);cancelled(options.stop);
    const auto digest=sink.impl_->finish();std::copy(digest.bytes.begin(),digest.bytes.end(),output.begin());
}

std::span<const std::uint8_t,64> KeyringAccess::root(const Crypto& key,bool serpent) {
    require(key.confidentiality_!=nullptr,"Confidentiality requires independently supplied Serpent and AES roots");
    return serpent?std::span<const std::uint8_t,64>(key.confidentiality_->serpent):std::span<const std::uint8_t,64>(key.confidentiality_->aes);
}
void KeyringAccess::encode(const Crypto& key,std::span<std::uint8_t> output) {
    require(output.size()==bytes,"invalid key entry extent");
    const auto s=root(key,true),a=root(key,false);
    std::copy(s.begin(),s.end(),output.begin());std::copy(a.begin(),a.end(),output.begin()+64);
}
Crypto KeyringAccess::decode(std::span<const std::uint8_t> input) {
    require(input.size()==bytes,"invalid key entry extent");
    return Crypto::from_independent_roots(input.first(64),input.last(64));
}

Crypto::Crypto(std::span<const std::uint8_t> master_key) {
    require(master_key.size() == 32, "a symmetric master key must contain exactly 32 bytes");
    try {
        for (std::size_t i = 0; i < keys_.size(); ++i) keys_[i] = hkdf(master_key, view(purpose_labels[i])).bytes;
    } catch (...) {
        OPENSSL_cleanse(keys_.data(), sizeof(keys_));
        throw;
    }
}
Crypto Crypto::amateur() {
    constexpr std::array<std::uint8_t,64> public_root{};
    return from_independent_roots(public_root,public_root);
}
Crypto Crypto::temporary() {
    Secret<64> serpent_root,aes_root;
    system_random_fill(serpent_root.bytes);
    system_random_fill(aes_root.bytes);
    return from_independent_roots(serpent_root.bytes,aes_root.bytes);
}
Crypto::Crypto(const Crypto& other) noexcept:keys_(other.keys_),confidentiality_(other.confidentiality_) {
    if(confidentiality_)confidentiality_->retain();
}
Crypto::~Crypto() {
    OPENSSL_cleanse(keys_.data(),sizeof(keys_));
    if(confidentiality_)confidentiality_->release();
}
Crypto& Crypto::operator=(const Crypto& other) {
    if (this != &other) {
        // Retain first: assigning between two aliases must not release the
        // source's last material owner before the new value has acquired it.
        const auto* replacement=other.confidentiality_;
        if(replacement)replacement->retain();
        OPENSSL_cleanse(keys_.data(), sizeof(keys_));
        keys_ = other.keys_;
        if(confidentiality_)confidentiality_->release();
        confidentiality_=replacement;
    }
    return *this;
}
Crypto Crypto::from_independent_roots(View serpent_root,View aes_root) {
    require(serpent_root.size()==64 && aes_root.size()==64,"independent cipher roots must each contain exactly 64 bytes");
    Crypto key(aes_root.first(32));
    key.confidentiality_=new ConfidentialityMaterial(
        std::span<const std::uint8_t,64>(serpent_root.data(),64),std::span<const std::uint8_t,64>(aes_root.data(),64));
    return key;
}

Bytes Crypto::confidentiality_stream(std::uint64_t timestamp,std::uint64_t offset,std::size_t count) const {
    const auto sroot=KeyringAccess::root(*this,true),aroot=KeyringAccess::root(*this,false);
    require(count==0 || count-1<=std::numeric_limits<std::uint64_t>::max()-offset,"confidentiality byte offset would overflow");
    if(!count)return {};
    auto info=[&](std::string_view label){Bytes bytes(label.begin(),label.end());bytes.resize(bytes.size()+8);put_u64(std::span(bytes).last(8),timestamp);return bytes;};
    const auto sm=cascade_kdf(sroot,info("datapump/cascade/v2/serpent/stream/"));
    const auto am=cascade_kdf(aroot,info("datapump/cascade/v2/aes/stream/"));
    std::array<std::uint8_t,16> serpent_counter{},aes_counter{};
    const auto sc=view("data-s02"),ac=view("data-a02");
    std::copy(sc.begin(),sc.end(),serpent_counter.begin());std::copy(ac.begin(),ac.end(),aes_counter.begin());
    put_u64(std::span(serpent_counter).last(8),offset/16);put_u64(std::span(aes_counter).last(8),offset/16);
    const auto skip=static_cast<std::size_t>(offset%16);
    require(count<=std::numeric_limits<std::size_t>::max()-skip,"confidentiality allocation would overflow");
    Bytes zeros(count+skip,0);
    auto masked=aes_cipher(zeros,std::span(am.bytes).first(32),aes_counter,EVP_aes_256_ctr(),true);
    if(skip)masked.erase(masked.begin(),masked.begin()+static_cast<std::ptrdiff_t>(skip));
    serpent_ctr_xor(masked,std::span(sm.bytes).first(32),serpent_counter,skip);return masked;
}
Crypto Crypto::confidentiality_child(std::span<const std::uint8_t> context) const {
    const auto sm=cascade_kdf(KeyringAccess::root(*this,true),contextual_info("datapump/cascade/v2/serpent/child/",context));
    const auto am=cascade_kdf(KeyringAccess::root(*this,false),contextual_info("datapump/cascade/v2/aes/child/",context));
    return Crypto::from_independent_roots(sm.bytes,am.bytes);
}


struct ConfidentialityCbc::Impl {
    Serpent256 serpent;
    MacContext iv_template{nullptr,EVP_MAC_CTX_free};
    CipherContext encrypt_context{EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free};
    CipherContext decrypt_context{EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free};
    explicit Impl(View material):serpent(std::span<const std::uint8_t,32>(material.data(),32)) {
        constexpr std::string_view salt="datapump/cascade/v2/hkdf-sha512";
        Kdf algorithm(EVP_KDF_fetch(nullptr,"HKDF",nullptr),EVP_KDF_free);
        require(static_cast<bool>(algorithm),"OpenSSL HKDF unavailable");
        KdfContext extract(EVP_KDF_CTX_new(algorithm.get()),EVP_KDF_CTX_free);
        require(static_cast<bool>(extract),"OpenSSL HKDF allocation failed");
        char digest[]="SHA512";int mode=EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
        OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,digest,0),
            OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE,&mode),
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,const_cast<std::uint8_t*>(material.data()),32),
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,const_cast<char*>(salt.data()),salt.size()),
            OSSL_PARAM_construct_end()};
        Secret<64> prk;
        require(EVP_KDF_derive(extract.get(),prk.bytes.data(),prk.bytes.size(),params)==1,"IV HKDF extraction failed");
        Mac hmac(EVP_MAC_fetch(nullptr,"HMAC",nullptr),EVP_MAC_free);
        require(static_cast<bool>(hmac),"OpenSSL HMAC unavailable");
        iv_template=MacContext(EVP_MAC_CTX_new(hmac.get()),EVP_MAC_CTX_free);
        OSSL_PARAM mac_params[]{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,digest,0),OSSL_PARAM_construct_end()};
        const auto prefix=view("datapump/cascade/v2/cbc/serpent-iv/");
        require(iv_template && EVP_MAC_init(iv_template.get(),prk.bytes.data(),prk.bytes.size(),mac_params)==1 &&
            EVP_MAC_update(iv_template.get(),prefix.data(),prefix.size())==1,"IV HKDF expansion initialization failed");
        std::array<std::uint8_t,16> iv{};
        require(encrypt_context && decrypt_context &&
            EVP_CipherInit_ex(encrypt_context.get(),EVP_aes_256_cbc(),nullptr,material.data()+32,iv.data(),1)==1 &&
            EVP_CIPHER_CTX_set_padding(encrypt_context.get(),0)==1 &&
            EVP_CipherInit_ex(decrypt_context.get(),EVP_aes_256_cbc(),nullptr,material.data()+32,iv.data(),0)==1 &&
            EVP_CIPHER_CTX_set_padding(decrypt_context.get(),0)==1,"prepared CBC initialization failed");
    }
    Bytes aes(View input,View iv,bool encrypt) {
        require(input.size()<=static_cast<std::size_t>(std::numeric_limits<int>::max()),"cipher input exceeds bounded operation");
        auto* context=encrypt?encrypt_context.get():decrypt_context.get();
        Bytes output(input.size()+16);int count=0,tail=0;
        struct WipeOnError {Bytes& bytes;bool success=false;~WipeOnError(){if(!success)OPENSSL_cleanse(bytes.data(),bytes.size());}} guard{output};
        require(EVP_CipherInit_ex(context,nullptr,nullptr,nullptr,iv.data(),encrypt?1:0)==1 &&
            EVP_CIPHER_CTX_set_padding(context,0)==1 &&
            EVP_CipherUpdate(context,output.data(),&count,input.data(),static_cast<int>(input.size()))==1 &&
            EVP_CipherFinal_ex(context,output.data()+count,&tail)==1 &&
            static_cast<std::size_t>(count+tail)==input.size(),"prepared AES cascade operation failed");
        output.resize(input.size());guard.success=true;return output;
    }
    Bytes crypt(View input,View iv,bool encrypt) {
        require(iv.size()==16 && !input.empty() && input.size()%16==0,"invalid confidentiality CBC geometry");
        // HKDF-Expand has exactly one SHA-512 output block: HMAC(PRK, info || 0x01).
        MacContext expansion(EVP_MAC_CTX_dup(iv_template.get()),EVP_MAC_CTX_free);
        Secret<64> serpent_iv;std::size_t size=0;const std::uint8_t one=1;
        require(expansion && EVP_MAC_update(expansion.get(),iv.data(),iv.size())==1 &&
            EVP_MAC_update(expansion.get(),&one,1)==1 &&
            EVP_MAC_final(expansion.get(),serpent_iv.bytes.data(),&size,serpent_iv.bytes.size())==1 && size==64,"IV HKDF expansion failed");
        if(encrypt) {
            SecretBytes inner(input.size());std::copy(input.begin(),input.end(),inner.bytes.begin());
            serpent_cbc_prepared(inner.bytes,serpent,std::span(serpent_iv.bytes).first(16),true);
            return aes(inner.bytes,iv,true);
        }
        auto plain=aes(input,iv,false);
        serpent_cbc_prepared(plain,serpent,std::span(serpent_iv.bytes).first(16),false);
        return plain;
    }
};
ConfidentialityCbc::ConfidentialityCbc(View material):impl_(std::make_unique<Impl>(material)) {}
ConfidentialityCbc::~ConfidentialityCbc() = default;
Bytes ConfidentialityCbc::encrypt(View input,View iv) {return impl_->crypt(input,iv,true);}
Bytes ConfidentialityCbc::decrypt(View input,View iv) {return impl_->crypt(input,iv,false);}
std::unique_ptr<ConfidentialityCbc> Crypto::prepare_confidentiality_cbc(View context,View salt) const {
    require(salt.size()==32,"invalid confidentiality CBC salt");
    const auto sm=cascade_kdf(KeyringAccess::root(*this,true),contextual_info("datapump/cascade/v2/serpent/cbc/",context,salt));
    const auto am=cascade_kdf(KeyringAccess::root(*this,false),contextual_info("datapump/cascade/v2/aes/cbc/",context,salt));
    Secret<64> material;std::copy_n(sm.bytes.begin(),32,material.bytes.begin());std::copy_n(am.bytes.begin(),32,material.bytes.begin()+32);
    return std::unique_ptr<ConfidentialityCbc>(new ConfidentialityCbc(material.bytes));
}
Bytes Crypto::stream(StreamPurpose purpose, std::uint64_t timestamp,
                      std::uint64_t offset, std::size_t count) const {
    const auto index = static_cast<std::size_t>(purpose);
    require(index < 4, "invalid keystream purpose");
    require(count == 0 || count - 1 <= std::numeric_limits<std::uint64_t>::max() - offset,
            "keystream byte offset would overflow");
    if (count == 0) return {};
    Bytes info;
    constexpr std::string_view epoch_domain = "datapump/v1/epoch/";
    info.insert(info.end(), epoch_domain.begin(), epoch_domain.end());
    info.resize(info.size() + 8);
    put_u64(std::span(info).last(8), timestamp);
    auto key = hkdf(keys_[index], info);
    std::array<std::uint8_t, 16> counter{};
    put_u64(std::span(counter).last(8), offset / 16);
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(context && EVP_EncryptInit_ex(context.get(), EVP_aes_256_ctr(), nullptr,
                                         key.bytes.data(), counter.data()) == 1,
            "OpenSSL stream initialization failed");
    const auto skip = static_cast<int>(offset % 16);
    int produced = 0;
    std::array<std::uint8_t, 16> discarded{};
    if (skip != 0) {
        require(EVP_EncryptUpdate(context.get(), discarded.data(), &produced, discarded.data(), skip) == 1 && produced == skip,
                "OpenSSL stream positioning failed");
        OPENSSL_cleanse(discarded.data(), discarded.size());
    }
    Bytes result(count, 0);
    for (std::size_t position = 0; position < count;) {
        const auto part = static_cast<int>(std::min(count - position, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        require(EVP_EncryptUpdate(context.get(), result.data() + position, &produced,
                                  result.data() + position, part) == 1 && produced == part,
                "OpenSSL stream generation failed");
        position += static_cast<std::size_t>(part);
    }
    require(EVP_EncryptFinal_ex(context.get(), discarded.data(), &produced) == 1 && produced == 0,
            "OpenSSL stream finalization failed");
    return result;
}
Bytes Crypto::mac(std::span<const std::uint8_t> message) const {
    Mac algorithm(EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
    require(static_cast<bool>(algorithm), "OpenSSL HMAC unavailable");
    MacContext context(EVP_MAC_CTX_new(algorithm.get()), EVP_MAC_CTX_free);
    require(static_cast<bool>(context), "OpenSSL HMAC allocation failed");
    char digest[] = "SHA256";
    OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0),
                        OSSL_PARAM_construct_end()};
    require(EVP_MAC_init(context.get(), keys_[4].data(), keys_[4].size(), params) == 1 &&
            EVP_MAC_update(context.get(), message.data(), message.size()) == 1,
            "OpenSSL HMAC computation failed");
    Bytes result(32);
    std::size_t size = 0;
    require(EVP_MAC_final(context.get(), result.data(), &size, result.size()) == 1 && size == result.size(),
            "OpenSSL HMAC finalization failed");
    return result;
}
Bytes Crypto::confidentiality_mac(View message) const {
    const auto sk=cascade_kdf(KeyringAccess::root(*this,true),view("datapump/cascade/v2/serpent/auth/"));
    const auto ak=cascade_kdf(KeyringAccess::root(*this,false),view("datapump/cascade/v2/aes/auth/"));
    auto result=hmac_digest<32>(std::span(sk.bytes).first(32),message,"SHA256");
    const auto other=hmac_digest<32>(std::span(ak.bytes).first(32),message,"SHA256");
    for(std::size_t i=0;i<result.bytes.size();++i)result.bytes[i]^=other.bytes[i];
    return Bytes(result.bytes.begin(),result.bytes.end());
}
KeyfileGenerationResult create_keyfile(const std::filesystem::path& path,const std::optional<std::filesystem::path>& pad,
                    const KeyfileGenerationOptions& generation) {
    return create_keyring_impl(path,{"Default"},normal_policy,pad,generation);
}
Crypto load_keyfile(const std::filesystem::path& path,const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path,normal_policy,pad).front().key;
}
KeyfileGenerationResult create_keyring(const std::filesystem::path& path,const std::vector<std::string>& names,
                    const std::optional<std::filesystem::path>& pad,const KeyfileGenerationOptions& generation) {
    return create_keyring_impl(path,names,normal_policy,pad,generation);
}
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path,normal_policy,pad);
}
namespace testing {
KeyfileGenerationResult create_keyfile(const std::filesystem::path& path,const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad,const KeyfileGenerationOptions& generation) {
    return create_keyring_impl(path,{"Default"},policy,pad,generation);
}
Crypto load_keyfile(const std::filesystem::path& path,const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path,policy,pad).front().key;
}
KeyfileGenerationResult create_keyring(const std::filesystem::path& path,const std::vector<std::string>& names,
                    const KeyfilePolicy& policy,const std::optional<std::filesystem::path>& pad,
                    const KeyfileGenerationOptions& generation) {
    return create_keyring_impl(path,names,policy,pad,generation);
}
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path,policy,pad);
}
} // namespace testing
} // namespace datapump
