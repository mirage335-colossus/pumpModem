#include "datapump/crypto.hpp"
#include "datapump/serpent.hpp"
#include "datapump/execution.hpp"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <algorithm>
#include <atomic>
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
// Payload roots are immutable after construction. Only the reference count is
// mutable; distinct Crypto values can therefore share a root across workers.
// A legacy waveform-only Crypto keeps a null pointer and allocates no root.
struct Crypto::ConfidentialityMaterial {
    mutable std::atomic<std::size_t> owners{1};
    std::array<std::uint8_t,64> bytes;
    explicit ConfidentialityMaterial(std::span<const std::uint8_t,64> input) noexcept {
        std::copy(input.begin(),input.end(),bytes.begin());
    }
    ~ConfidentialityMaterial(){OPENSSL_cleanse(bytes.data(),bytes.size());}
    void retain() const noexcept {owners.fetch_add(1,std::memory_order_relaxed);}
    void release() const noexcept {
        if(owners.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;
    }
};
// Only the authenticated keyfile codec serializes complete purpose-key sets.
// The public API exposes operations, never raw secret material.
struct KeyringAccess {
    static constexpr std::size_t legacy_bytes=5*32;
    static constexpr std::size_t bytes=legacy_bytes+64;
    static void encode(const Crypto& key,std::span<std::uint8_t> output) {
        for(std::size_t i=0;i<5;++i) std::copy(key.keys_[i].begin(),key.keys_[i].end(),output.begin()+static_cast<std::ptrdiff_t>(i*32));
        root(key,std::span<std::uint8_t,64>(output.data()+legacy_bytes,64));
    }
    static Crypto decode(std::span<const std::uint8_t> input) {
        Crypto key;
        for(std::size_t i=0;i<5;++i) std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(i*32),32,key.keys_[i].begin());
        if(input.size()==bytes){
            key.confidentiality_=new Crypto::ConfidentialityMaterial(
                std::span<const std::uint8_t,64>(input.data()+legacy_bytes,64));
        }
        return key;
    }
    static void root(const Crypto& key,std::span<std::uint8_t,64> output);
    static Crypto fresh(std::span<const std::uint8_t> material);
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
constexpr std::string_view keyfile_hash_domain = "datapump/v1/keyfile-hash";
constexpr std::array<std::string_view, 5> purpose_labels{
    "datapump/v1/data", "datapump/v1/dsss", "datapump/v1/scrambler",
    "datapump/v1/fhss", "datapump/v1/mac"};
constexpr std::array<std::uint8_t, 8> keyfile_magic{'D', 'P', 'M', 'K', 'E', 'Y', '0', '1'};
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
void random_fill(std::span<std::uint8_t> data) {
    for (std::size_t offset = 0; offset < data.size();) {
        const auto count = std::min(data.size() - offset, static_cast<std::size_t>(std::numeric_limits<int>::max()));
        require(RAND_priv_bytes(data.data() + offset, static_cast<int>(count)) == 1,
                "OpenSSL random generation failed");
        offset += count;
    }
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
    constexpr std::string_view salt="datapump/cascade/v1/hkdf-sha512";
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

void serpent_cbc(std::span<std::uint8_t> data,View key,View iv,bool encrypt) {
    Serpent256 serpent(std::span<const std::uint8_t,32>(key.data(),32));
    serpent_cbc_prepared(data,serpent,iv,encrypt);
}

class Hash {
public:
    Hash() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
        require(context_ && EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) == 1,
                "OpenSSL SHA256 initialization failed");
    }
    void update(View data) {
        require(EVP_DigestUpdate(context_.get(), data.data(), data.size()) == 1,
                "OpenSSL SHA256 update failed");
    }
    Secret<32> finish() {
        Secret<32> result;
        unsigned int size = 0;
        require(EVP_DigestFinal_ex(context_.get(), result.bytes.data(), &size) == 1 && size == 32,
                "OpenSSL SHA256 finalization failed");
        return result;
    }
private:
    MdContext context_;
};

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
template<class H> void hash_pad(H& hash, const std::optional<std::filesystem::path>& pad, std::uint64_t length) {
    if (!pad) return;
    std::ifstream input(*pad, std::ios::binary);
    require(static_cast<bool>(input), "cannot open pad for reading");
    Secret<chunk_size> buffer;
    while (length != 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, buffer.bytes.size()));
        auto part = std::span(buffer.bytes).first(count);
        read_exact(input, part);
        hash.update(part);
        length -= count;
    }
    require_end(input);
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

std::array<std::uint8_t, 48> wrap_key(View key, View nonce, View prefix, View master) {
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(context && EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) == 1,
            "OpenSSL keyfile encryption initialization failed");
    int size = 0;
    require(EVP_EncryptUpdate(context.get(), nullptr, &size, prefix.data(), static_cast<int>(prefix.size())) == 1,
            "OpenSSL keyfile associated data failed");
    std::array<std::uint8_t, 48> result{};
    require(EVP_EncryptUpdate(context.get(), result.data(), &size, master.data(), static_cast<int>(master.size())) == 1 && size == 32,
            "OpenSSL keyfile encryption failed");
    int final_size = 0;
    require(EVP_EncryptFinal_ex(context.get(), result.data() + 32, &final_size) == 1 && final_size == 0,
            "OpenSSL keyfile encryption finalization failed");
    require(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, result.data() + 32) == 1,
            "OpenSSL keyfile authentication failed");
    return result;
}
Secret<32> unwrap_key(View key, View nonce, View prefix, View wrapped) {
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(context && EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) == 1,
            "OpenSSL keyfile decryption initialization failed");
    int size = 0;
    require(EVP_DecryptUpdate(context.get(), nullptr, &size, prefix.data(), static_cast<int>(prefix.size())) == 1,
            "OpenSSL keyfile associated data failed");
    Secret<32> master;
    require(EVP_DecryptUpdate(context.get(), master.bytes.data(), &size, wrapped.data(), 32) == 1 && size == 32,
            "OpenSSL keyfile decryption failed");
    require(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, 16,
                                const_cast<std::uint8_t*>(wrapped.data() + 32)) == 1,
            "OpenSSL keyfile tag initialization failed");
    std::array<std::uint8_t, 16> final{};
    require(EVP_DecryptFinal_ex(context.get(), final.data(), &size) == 1 && size == 0,
            "keyfile authentication failed: wrong pad or corrupted keyfile");
    return master;
}

void create_keyfile_impl(const std::filesystem::path& path, const testing::KeyfilePolicy& policy,
                         const std::optional<std::filesystem::path>& pad) {
    validate_policy(policy);
    const auto pad_length = pad_size(pad, policy);
    std::array<std::uint8_t, prefix_size> prefix{};
    std::copy(keyfile_magic.begin(), keyfile_magic.end(), prefix.begin());
    put_u64(std::span(prefix).subspan(8, 8), policy.header_bytes);
    put_u64(std::span(prefix).subspan(16, 8), pad_length);
    prefix[24] = pad ? 1 : 0;
    random_fill(std::span(prefix).subspan(32, 12));
    Hash hash;
    hash.update(view(keyfile_hash_domain));
    hash.update(prefix);
    auto output = exclusive_output(path);
    // Failures leave an incomplete, owner-only file. Never remove a path after
    // failure: another process may have replaced that name in the meantime.
    write_exact(output.get(), prefix);
    Secret<chunk_size> buffer;
    auto remaining = policy.header_bytes;
    while (remaining != 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.bytes.size()));
        auto part = std::span(buffer.bytes).first(count);
        random_fill(part);
        write_exact(output.get(), part);
        hash.update(part);
        remaining -= count;
    }
    hash_pad(hash, pad, pad_length);
    auto digest = hash.finish();
    auto wrapping_key = hkdf(digest.bytes, view("datapump/v1/keyfile-wrap"));
    Secret<32> master;
    random_fill(master.bytes);
    const auto wrapped = wrap_key(wrapping_key.bytes, std::span(prefix).subspan(32, 12), prefix, master.bytes);
    write_exact(output.get(), wrapped);
    flush_file(output);
}

Crypto load_keyfile_impl(const std::filesystem::path& path, const testing::KeyfilePolicy& policy,
                         const std::optional<std::filesystem::path>& pad) {
    validate_policy(policy);
    const auto length = regular_file_size(path);
    require(length == prefix_size + policy.header_bytes + 48, "invalid keyfile length or header size");
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open keyfile for reading");
    std::array<std::uint8_t, prefix_size> prefix{};
    read_exact(input, prefix);
    require(std::equal(keyfile_magic.begin(), keyfile_magic.end(), prefix.begin()), "unsupported keyfile format");
    require(get_u64(std::span(prefix).subspan(8, 8)) == policy.header_bytes, "invalid keyfile header length");
    require(prefix[24] <= 1 && std::all_of(prefix.begin() + 25, prefix.begin() + 32, [](auto b) { return b == 0; }) &&
            std::all_of(prefix.begin() + 44, prefix.end(), [](auto b) { return b == 0; }), "unsupported keyfile flags");
    const bool needs_pad = prefix[24] == 1;
    require(needs_pad == pad.has_value(), needs_pad ? "this keyfile requires a pad" : "this keyfile does not use a pad");
    const auto pad_length = pad_size(pad, policy);
    require(get_u64(std::span(prefix).subspan(16, 8)) == pad_length, "pad length does not match keyfile");
    Hash hash;
    hash.update(view(keyfile_hash_domain));
    hash.update(prefix);
    Secret<chunk_size> buffer;
    auto remaining = policy.header_bytes;
    while (remaining != 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.bytes.size()));
        auto part = std::span(buffer.bytes).first(count);
        read_exact(input, part);
        hash.update(part);
        remaining -= count;
    }
    std::array<std::uint8_t, 48> wrapped{};
    read_exact(input, wrapped);
    require_end(input);
    hash_pad(hash, pad, pad_length);
    auto digest = hash.finish();
    auto wrapping_key = hkdf(digest.bytes, view("datapump/v1/keyfile-wrap"));
    auto master = unwrap_key(wrapping_key.bytes, std::span(prefix).subspan(32, 12), prefix, wrapped);
    return Crypto(master.bytes);
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
constexpr std::array<std::uint8_t,8> ring_magic{'D','P','M','K','E','Y','0','2'};
constexpr std::array<std::uint8_t,8> cascade_ring_magic{'D','P','M','K','E','Y','0','3'};
constexpr std::string_view ring_hash_domain="datapump/v2/keyfile-hash";
constexpr std::string_view ring_wrap_domain="datapump/v2/keyfile-wrap";
constexpr std::string_view cascade_ring_hash_domain="datapump/v3/keyfile-hash-sha512";
constexpr std::string_view cascade_ring_wrap_domain="datapump/v3/keyfile-wrap-serpent-aes";

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
    Bytes info;const auto label=view("datapump/v3/keyfile-serpent-iv/");
    info.insert(info.end(),label.begin(),label.end());info.insert(info.end(),nonce.begin(),nonce.end());
    const auto derived=cascade_kdf(key,info);std::array<std::uint8_t,16> iv{};
    std::copy_n(derived.bytes.begin(),16,iv.begin());return iv;
}

Secret<64> timed_keyfile_entropy() {
    Secret<128> collections;
    system_random_fill(std::span(collections.bytes).first(64));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    // The execution abstraction parks only this task in the browser fiber
    // scheduler, preserving other live work while key generation waits.
    do {execution::sleep_until(deadline);} while(std::chrono::steady_clock::now()<deadline);
    system_random_fill(std::span(collections.bytes).last(64));
    return cascade_kdf(collections.bytes,view("datapump/v3/timed-keyfile-entropy"));
}
void create_keyring_impl(const std::filesystem::path& path,const std::vector<std::string>& names,
                        const testing::KeyfilePolicy& policy,const std::optional<std::filesystem::path>& pad) {
    validate_policy(policy);
    require(!names.empty() && names.size()<=128,"keyring must contain 1..128 named key sets");
    std::set<std::string> unique;std::size_t length=0;
    for(const auto& name:names) {
        require(valid_key_name(name),"key names must be 1..64 bytes of printable UTF-8");
        require(unique.insert(name).second,"duplicate key name");
        length+=2+name.size()+KeyringAccess::bytes;
    }
    const auto pad_length=pad_size(pad,policy);
    // Reserve the filename before entropy collection. Failed work leaves an
    // incomplete owner-only file, never an overwritten existing keyfile.
    auto output=exclusive_output(path);
    auto entropy=timed_keyfile_entropy();
    SecretBytes plaintext(length);
    std::size_t offset=0,index=0;
    for(const auto& name:names) {
        plaintext.bytes[offset++]=0;plaintext.bytes[offset++]=static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(),name.end(),plaintext.bytes.begin()+static_cast<std::ptrdiff_t>(offset));offset+=name.size();
        Bytes info;const auto label=view("datapump/v3/key-entry/");info.insert(info.end(),label.begin(),label.end());
        info.resize(info.size()+8);put_u64(std::span(info).last(8),index++);
        auto material=cascade_kdf(entropy.bytes,info);
        const auto key=KeyringAccess::fresh(material.bytes);
        KeyringAccess::encode(key,std::span(plaintext.bytes).subspan(offset,KeyringAccess::bytes));offset+=KeyringAccess::bytes;
    }
    std::array<std::uint8_t,prefix_size> prefix{};
    std::copy(cascade_ring_magic.begin(),cascade_ring_magic.end(),prefix.begin());
    put_u64(std::span(prefix).subspan(8,8),policy.header_bytes);
    put_u64(std::span(prefix).subspan(16,8),pad_length);prefix[24]=pad?1:0;
    put32(std::span(prefix).subspan(28,4),static_cast<std::uint32_t>(names.size()));
    system_random_fill(std::span(prefix).subspan(32,12));
    put32(std::span(prefix).subspan(44,4),static_cast<std::uint32_t>(length));
    Hash512 hash;hash.update(view(cascade_ring_hash_domain));hash.update(prefix);
    write_exact(output.get(),prefix);
    Secret<chunk_size> buffer;
    for(auto remaining=policy.header_bytes;remaining;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,buffer.bytes.size()));
        auto part=std::span(buffer.bytes).first(count);system_random_fill(part);hash.update(part);write_exact(output.get(),part);remaining-=count;
        execution::checkpoint();
    }
    hash_pad(hash,pad,pad_length);auto hashed=hash.finish();auto wrapping=cascade_kdf(hashed.bytes,view(cascade_ring_wrap_domain));
    auto inner_iv=keyfile_serpent_iv(std::span(wrapping.bytes).first(32),std::span(prefix).subspan(32,12));
    serpent_ctr_xor(plaintext.bytes,std::span(wrapping.bytes).first(32),inner_iv);
    const auto encrypted=wrap_payload(std::span(wrapping.bytes).last(32),std::span(prefix).subspan(32,12),prefix,plaintext.bytes);
    write_exact(output.get(),encrypted);flush_file(output);
}

std::vector<KeyEntry> load_keyring_impl(const std::filesystem::path& path,const testing::KeyfilePolicy& policy,
                                      const std::optional<std::filesystem::path>& pad) {
    validate_policy(policy);
    const auto length=regular_file_size(path);
    require(length>=prefix_size,"invalid keyfile length");
    std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"cannot open keyfile");
    std::array<std::uint8_t,prefix_size> prefix{};read_exact(input,prefix);
    if(std::equal(keyfile_magic.begin(),keyfile_magic.end(),prefix.begin()))return {{"Default",load_keyfile_impl(path,policy,pad)}};
    const bool cascade=std::equal(cascade_ring_magic.begin(),cascade_ring_magic.end(),prefix.begin());
    require(cascade || std::equal(ring_magic.begin(),ring_magic.end(),prefix.begin()),"unsupported keyfile format");
    const auto entry_bytes=cascade?KeyringAccess::bytes:KeyringAccess::legacy_bytes;
    require(get_u64(std::span(prefix).subspan(8,8))==policy.header_bytes,"invalid keyfile header size");
    require(prefix[24]<=1 && prefix[25]==0 && prefix[26]==0 && prefix[27]==0,"unsupported keyring flags");
    const auto count=get32(std::span(prefix).subspan(28,4)),payload_length=get32(std::span(prefix).subspan(44,4));
    require(count>=1 && count<=128 && payload_length>=count*(2+1+entry_bytes) && payload_length<=count*(2+64+entry_bytes),"invalid keyring dimensions");
    require(length==prefix_size+policy.header_bytes+payload_length+16,"invalid keyring length");
    require((prefix[24]!=0)==pad.has_value(),prefix[24]?"this keyfile requires an optional external pad (CLI --pad)":"this keyfile does not use a pad");
    const auto pad_length=pad_size(pad,policy);
    require(get_u64(std::span(prefix).subspan(16,8))==pad_length,"pad length does not match keyfile");
    Hash hash;Hash512 hash512;
    if(cascade){hash512.update(view(cascade_ring_hash_domain));hash512.update(prefix);}
    else {hash.update(view(ring_hash_domain));hash.update(prefix);}
    Secret<chunk_size> buffer;
    for(auto remaining=policy.header_bytes;remaining;) {
        const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,buffer.bytes.size()));
        auto part=std::span(buffer.bytes).first(size);read_exact(input,part);
        if(cascade)hash512.update(part);else hash.update(part);remaining-=size;
    }
    Bytes encrypted(payload_length+16);read_exact(input,encrypted);require_end(input);
    SecretBytes plaintext(payload_length);
    if(cascade) {
        hash_pad(hash512,pad,pad_length);auto hashed=hash512.finish();auto wrapping=cascade_kdf(hashed.bytes,view(cascade_ring_wrap_domain));
        unwrap_payload(std::span(wrapping.bytes).last(32),std::span(prefix).subspan(32,12),prefix,encrypted,plaintext.bytes);
        auto inner_iv=keyfile_serpent_iv(std::span(wrapping.bytes).first(32),std::span(prefix).subspan(32,12));
        serpent_ctr_xor(plaintext.bytes,std::span(wrapping.bytes).first(32),inner_iv);
    } else {
        hash_pad(hash,pad,pad_length);auto hashed=hash.finish();auto wrapping=hkdf(hashed.bytes,view(ring_wrap_domain));
        unwrap_payload(wrapping.bytes,std::span(prefix).subspan(32,12),prefix,encrypted,plaintext.bytes);
    }
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

void KeyringAccess::root(const Crypto& key,std::span<std::uint8_t,64> output) {
    if(key.confidentiality_)std::copy(key.confidentiality_->bytes.begin(),key.confidentiality_->bytes.end(),output.begin());
    else {
        Secret<64> legacy;
        std::copy(key.keys_[0].begin(),key.keys_[0].end(),legacy.bytes.begin());
        std::copy(key.keys_[4].begin(),key.keys_[4].end(),legacy.bytes.begin()+32);
        const auto derived=cascade_kdf(legacy.bytes,view("datapump/cascade/v1/legacy-root"));
        std::copy(derived.bytes.begin(),derived.bytes.end(),output.begin());
    }
}
Crypto KeyringAccess::fresh(std::span<const std::uint8_t> material) {
    auto modem=cascade_kdf(material,view("datapump/v3/modem-root"));
    Crypto key(std::span(modem.bytes).first(32));
    const auto root=cascade_kdf(material,view("datapump/v3/confidentiality-root"));
    key.confidentiality_=new Crypto::ConfidentialityMaterial(root.bytes);
    return key;
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
const void* Crypto::confidentiality_storage_id() const noexcept {return confidentiality_;}
std::size_t Crypto::confidentiality_storage_bytes() const noexcept {
    return confidentiality_?sizeof(ConfidentialityMaterial):0;
}
Crypto Crypto::random() {
    Secret<64> material;
    system_random_fill(material.bytes);
    return KeyringAccess::fresh(material.bytes);
}

Bytes Crypto::confidentiality_stream(std::uint64_t timestamp,std::uint64_t offset,std::size_t count) const {
    require(count==0 || count-1<=std::numeric_limits<std::uint64_t>::max()-offset,"confidentiality byte offset would overflow");
    if(!count)return {};
    Secret<64> root;KeyringAccess::root(*this,root.bytes);
    const auto label=view("datapump/cascade/v1/stream/");Bytes info(label.begin(),label.end());
    info.resize(info.size()+8);put_u64(std::span(info).last(8),timestamp);
    auto material=cascade_kdf(root.bytes,info);
    std::array<std::uint8_t,16> serpent_counter{},aes_counter{};
    const auto sc=view("data-s01"),ac=view("data-a01");
    std::copy(sc.begin(),sc.end(),serpent_counter.begin());std::copy(ac.begin(),ac.end(),aes_counter.begin());
    put_u64(std::span(serpent_counter).last(8),offset/16);put_u64(std::span(aes_counter).last(8),offset/16);
    const auto skip=static_cast<std::size_t>(offset%16);
    // A bounded leading block preserves random access without retaining a
    // duration-sized stream. AES remains a bulk EVP operation.
    require(count<=std::numeric_limits<std::size_t>::max()-skip,"confidentiality allocation would overflow");
    Bytes zeros(count+skip,0);
    auto masked=aes_cipher(zeros,std::span(material.bytes).last(32),aes_counter,EVP_aes_256_ctr(),true);
    if(skip)masked.erase(masked.begin(),masked.begin()+static_cast<std::ptrdiff_t>(skip));
    serpent_ctr_xor(masked,std::span(material.bytes).first(32),serpent_counter,skip);
    return masked;
}

Crypto Crypto::confidentiality_child(std::span<const std::uint8_t> context) const {
    SecretBytes authentication(32);authentication.bytes=mac(context);
    Crypto child(authentication.bytes);
    Secret<64> root;KeyringAccess::root(*this,root.bytes);
    const auto info=contextual_info("datapump/cascade/v1/child/",context);
    const auto material=cascade_kdf(root.bytes,info);
    child.confidentiality_=new ConfidentialityMaterial(material.bytes);
    return child;
}


struct ConfidentialityCbc::Impl {
    Serpent256 serpent;
    MacContext iv_template{nullptr,EVP_MAC_CTX_free};
    CipherContext encrypt_context{EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free};
    CipherContext decrypt_context{EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free};
    explicit Impl(View material):serpent(std::span<const std::uint8_t,32>(material.data(),32)) {
        constexpr std::string_view salt="datapump/cascade/v1/hkdf-sha512";
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
        const auto prefix=view("datapump/cascade/v1/cbc/serpent-iv/");
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
    Secret<64> root;KeyringAccess::root(*this,root.bytes);
    auto material=cascade_kdf(root.bytes,contextual_info("datapump/cascade/v1/cbc/",context,salt));
    return std::unique_ptr<ConfidentialityCbc>(new ConfidentialityCbc(material.bytes));
}
namespace {
Bytes confidentiality_cbc(const Crypto& crypto,View input,View context,View salt,View iv,bool encrypt) {
    require(salt.size()==32 && iv.size()==16 && !input.empty() && input.size()%16==0,"invalid confidentiality CBC geometry");
    Secret<64> root;KeyringAccess::root(crypto,root.bytes);
    const auto info=contextual_info("datapump/cascade/v1/cbc/",context,salt);
    auto material=cascade_kdf(root.bytes,info);
    const auto iv_label=view("datapump/cascade/v1/cbc/serpent-iv/");Bytes iv_info(iv_label.begin(),iv_label.end());iv_info.insert(iv_info.end(),iv.begin(),iv.end());
    auto serpent_iv=cascade_kdf(std::span(material.bytes).first(32),iv_info);
    if(encrypt){
        SecretBytes inner(input.size());std::copy(input.begin(),input.end(),inner.bytes.begin());
        serpent_cbc(inner.bytes,std::span(material.bytes).first(32),std::span(serpent_iv.bytes).first(16),true);
        return aes_cipher(inner.bytes,std::span(material.bytes).last(32),iv,EVP_aes_256_cbc(),true);
    }
    auto plain=aes_cipher(input,std::span(material.bytes).last(32),iv,EVP_aes_256_cbc(),false);
    serpent_cbc(plain,std::span(material.bytes).first(32),std::span(serpent_iv.bytes).first(16),false);
    return plain;
}
}
Bytes Crypto::encrypt_confidentiality_cbc(View input,View context,View salt,View iv) const {
    return confidentiality_cbc(*this,input,context,salt,iv,true);
}
Bytes Crypto::decrypt_confidentiality_cbc(View input,View context,View salt,View iv) const {
    return confidentiality_cbc(*this,input,context,salt,iv,false);
}

Bytes Crypto::stream(StreamPurpose purpose, std::uint64_t timestamp,
                      std::uint64_t offset, std::size_t count, StreamDomain domain) const {
    const auto index = static_cast<std::size_t>(purpose);
    require(index < 4, "invalid keystream purpose");
    require(domain == StreamDomain::Payload || domain == StreamDomain::Preamble || domain == StreamDomain::Suppression ||
            domain == StreamDomain::PatternZeroV2 || domain == StreamDomain::PatternOneV2 ||
            domain == StreamDomain::OuterDsss10V1 || domain == StreamDomain::OuterDsss100V1 ||
            domain == StreamDomain::OuterDsss1000V1 || domain == StreamDomain::FakeFhssV1 ||
            domain == StreamDomain::OuterDsss10RotationV2 || domain == StreamDomain::OuterDsss100RotationV2 ||
            domain == StreamDomain::OuterDsss1000RotationV2 || domain == StreamDomain::OuterDsss10PermutationV2 ||
            domain == StreamDomain::OuterDsss100PermutationV2 || domain == StreamDomain::OuterDsss1000PermutationV2 ||
            domain == StreamDomain::FhssCycleV2 || domain == StreamDomain::CoverPreambleV1 ||
            domain == StreamDomain::CoverSuppressionV1 || domain == StreamDomain::FhssUtcV3 || domain == StreamDomain::CoverWaitV1 ||
            domain == StreamDomain::SchedHopKeyV1 || domain == StreamDomain::SchedHopUtcV1 ||
            domain == StreamDomain::SymbolBurstWaitV1,
            "invalid keystream domain");
    const bool outer_v2=domain==StreamDomain::OuterDsss10RotationV2 || domain==StreamDomain::OuterDsss100RotationV2 ||
        domain==StreamDomain::OuterDsss1000RotationV2 || domain==StreamDomain::OuterDsss10PermutationV2 ||
        domain==StreamDomain::OuterDsss100PermutationV2 || domain==StreamDomain::OuterDsss1000PermutationV2;
    require(!outer_v2 || purpose==StreamPurpose::Dsss,"outer spreading requires the dedicated DSSS purpose key");
    require((domain!=StreamDomain::FhssCycleV2 && domain!=StreamDomain::FakeFhssV1 && domain!=StreamDomain::FhssUtcV3 &&
             domain!=StreamDomain::SchedHopKeyV1 && domain!=StreamDomain::SchedHopUtcV1 &&
             domain!=StreamDomain::SymbolBurstWaitV1) ||
            purpose==StreamPurpose::Fhss,"hopping requires the dedicated FHSS purpose key");
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
    if(domain == StreamDomain::Preamble) {
        constexpr std::string_view pad = "preamble";
        std::copy(pad.begin(), pad.end(), counter.begin());
    }
    if(domain == StreamDomain::Suppression) {
        constexpr std::string_view pad = "suppress";
        std::copy(pad.begin(), pad.end(), counter.begin());
    }
    if(domain == StreamDomain::PatternZeroV2 || domain == StreamDomain::PatternOneV2) {
        const std::string_view pad = domain == StreamDomain::PatternZeroV2 ? "pat-v2-0" : "pat-v2-1";
        std::copy(pad.begin(), pad.end(), counter.begin());
    }
    if(domain == StreamDomain::OuterDsss10V1 || domain == StreamDomain::OuterDsss100V1 ||
       domain == StreamDomain::OuterDsss1000V1) {
        require(purpose==StreamPurpose::Dsss,"outer spreading requires the dedicated DSSS purpose key");
        const std::string_view pad=domain==StreamDomain::OuterDsss10V1?"ds1-0010":
            domain==StreamDomain::OuterDsss100V1?"ds1-0100":"ds1-1000";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(domain == StreamDomain::FakeFhssV1) {
        require(purpose==StreamPurpose::Fhss,"fake hopping requires the dedicated FHSS purpose key");
        constexpr std::string_view pad="fh-fake1";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(domain == StreamDomain::FhssCycleV2 || domain == StreamDomain::FhssUtcV3) {
        const std::string_view pad=domain == StreamDomain::FhssUtcV3?"fh-utc03":"fh-cyc02";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(domain == StreamDomain::SchedHopKeyV1 || domain == StreamDomain::SchedHopUtcV1) {
        const std::string_view pad=domain == StreamDomain::SchedHopKeyV1?"sh-key01":"sh-utc01";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(domain == StreamDomain::SymbolBurstWaitV1) {
        constexpr std::string_view pad="sb-wai01";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(domain == StreamDomain::CoverPreambleV1 || domain == StreamDomain::CoverSuppressionV1 || domain == StreamDomain::CoverWaitV1) {
        const std::string_view pad=domain==StreamDomain::CoverPreambleV1?"cv-pre01":domain==StreamDomain::CoverWaitV1?"cv-wai01":"cv-sup01";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
    if(outer_v2) {
        const std::string_view pad=domain==StreamDomain::OuterDsss10RotationV2?"dr2-0010":
            domain==StreamDomain::OuterDsss100RotationV2?"dr2-0100":
            domain==StreamDomain::OuterDsss1000RotationV2?"dr2-1000":
            domain==StreamDomain::OuterDsss10PermutationV2?"dp2-0010":
            domain==StreamDomain::OuterDsss100PermutationV2?"dp2-0100":"dp2-1000";
        std::copy(pad.begin(),pad.end(),counter.begin());
    }
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
Bytes Crypto::xor_data(std::span<const std::uint8_t> data, std::uint64_t timestamp,
                        std::uint64_t offset) const {
    auto result = confidentiality_stream(timestamp, offset, data.size());
    for (std::size_t i = 0; i < data.size(); ++i) result[i] ^= data[i];
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
bool Crypto::verify(std::span<const std::uint8_t> message, std::span<const std::uint8_t> tag) const {
    if (tag.size() != 32) return false;
    const auto expected = mac(message);
    return CRYPTO_memcmp(expected.data(), tag.data(), expected.size()) == 0;
}

void create_keyfile(const std::filesystem::path& path, const std::optional<std::filesystem::path>& pad) {
    create_keyring_impl(path, {"Default"}, normal_policy, pad);
}
Crypto load_keyfile(const std::filesystem::path& path, const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path, normal_policy, pad).front().key;
}
void create_keyring(const std::filesystem::path& path,const std::vector<std::string>& names,const std::optional<std::filesystem::path>& pad) {create_keyring_impl(path,names,normal_policy,pad);}
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,const std::optional<std::filesystem::path>& pad) {return load_keyring_impl(path,normal_policy,pad);}
namespace testing {
void create_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    create_keyfile_impl(path, policy, pad);
}
Crypto load_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    return load_keyring_impl(path, policy, pad).front().key;
}
void create_keyring(const std::filesystem::path& path,const std::vector<std::string>& names,const KeyfilePolicy& policy,const std::optional<std::filesystem::path>& pad) {create_keyring_impl(path,names,policy,pad);}
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,const KeyfilePolicy& policy,const std::optional<std::filesystem::path>& pad) {return load_keyring_impl(path,policy,pad);}
} // namespace testing
} // namespace datapump
