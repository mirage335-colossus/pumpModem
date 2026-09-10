#include "datapump/crypto.hpp"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace datapump {
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
void hash_pad(Hash& hash, const std::optional<std::filesystem::path>& pad, std::uint64_t length) {
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
} // namespace

Crypto::Crypto(std::span<const std::uint8_t> master_key) {
    require(master_key.size() == 32, "a symmetric master key must contain exactly 32 bytes");
    try {
        for (std::size_t i = 0; i < keys_.size(); ++i) keys_[i] = hkdf(master_key, view(purpose_labels[i])).bytes;
    } catch (...) {
        OPENSSL_cleanse(keys_.data(), sizeof(keys_));
        throw;
    }
}
Crypto::~Crypto() { OPENSSL_cleanse(keys_.data(), sizeof(keys_)); }
Crypto& Crypto::operator=(const Crypto& other) {
    if (this != &other) {
        OPENSSL_cleanse(keys_.data(), sizeof(keys_));
        keys_ = other.keys_;
    }
    return *this;
}
Crypto Crypto::random() {
    Secret<32> master;
    random_fill(master.bytes);
    return Crypto(master.bytes);
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
Bytes Crypto::xor_data(std::span<const std::uint8_t> data, std::uint64_t timestamp,
                        std::uint64_t offset) const {
    auto result = stream(StreamPurpose::Data, timestamp, offset, data.size());
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
    create_keyfile_impl(path, normal_policy, pad);
}
Crypto load_keyfile(const std::filesystem::path& path, const std::optional<std::filesystem::path>& pad) {
    return load_keyfile_impl(path, normal_policy, pad);
}
namespace testing {
void create_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    create_keyfile_impl(path, policy, pad);
}
Crypto load_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad) {
    return load_keyfile_impl(path, policy, pad);
}
} // namespace testing
} // namespace datapump
