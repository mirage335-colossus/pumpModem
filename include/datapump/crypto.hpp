#pragma once

#include "datapump/types.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <memory>
#include <span>
#include <functional>
#include <stop_token>
#include <string_view>

namespace datapump {

// Numeric purpose slots and their HKDF labels are fixed by the current wire
// format, including slots not presently used by the three-carrier source.
enum class StreamPurpose : std::uint8_t { Data=0, Dsss=1, Scrambler=2, Fhss=3 };

// Per-transfer CBC setup. Not shared concurrently; each call resets IV/chain state.
class ConfidentialityCbc final {
public:
    ~ConfidentialityCbc();
    ConfidentialityCbc(const ConfidentialityCbc&) = delete;
    ConfidentialityCbc& operator=(const ConfidentialityCbc&) = delete;
    Bytes encrypt(std::span<const std::uint8_t> plaintext, std::span<const std::uint8_t> wire_iv);
    Bytes decrypt(std::span<const std::uint8_t> ciphertext, std::span<const std::uint8_t> wire_iv);
private:
    friend class Crypto;
    explicit ConfidentialityCbc(std::span<const std::uint8_t> material);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Purpose-separated modem/MAC keys and separate confidentiality key material.
// The timestamp is the same whole-second epoch for every modem layer;
// offset is a byte position within that epoch's stream, not wall-clock time.
class Crypto {
public:
    // AES-only waveform/MAC seed. This constructor cannot encrypt user data.
    explicit Crypto(std::span<const std::uint8_t> master_key);
    ~Crypto();
    Crypto(const Crypto&) noexcept;
    Crypto& operator=(const Crypto&);

    // Explicit, independently supplied 64-byte roots. Serpent material never
    // enters an AES cipher, waveform or mac() derivation and AES material never
    // enters a Serpent derivation. AES root bytes 0..31 seed the existing five
    // waveform/MAC purpose keys. Deterministic callers must provide both roots.
    static Crypto from_independent_roots(std::span<const std::uint8_t> serpent_root,
                                        std::span<const std::uint8_t> aes_root);
    // Purpose/epoch-separated AES-256-CTR. The high eight counter bytes are
    // zero; the low eight encode offset/16. Byte offsets retain their exact
    // position within the epoch and cannot wrap.
    Bytes stream(StreamPurpose purpose, std::uint64_t timestamp,
                 std::uint64_t offset, std::size_t count) const;
    // Successive Serpent-256-CTR and AES-256-CTR encryption layers. Modem
    // waveform generators continue to use the AES-only stream() API above.
    Bytes confidentiality_stream(std::uint64_t timestamp, std::uint64_t offset,
                                 std::size_t count) const;
    Crypto confidentiality_child(std::span<const std::uint8_t> context) const;
    // Fixed-width Serpent-256-CBC inner / AES-256-CBC outer cascade. The
    // caller authenticates IV and ciphertext before calling decrypt.
    std::unique_ptr<ConfidentialityCbc> prepare_confidentiality_cbc(
        std::span<const std::uint8_t> context, std::span<const std::uint8_t> salt) const;
    // HMAC-SHA256 over exactly message. Include framed metadata in message.
    Bytes mac(std::span<const std::uint8_t> message) const;
    // Authentication for confidential user data: independently keyed full
    // HMAC-SHA256 tags from both lanes are XORed. This result is never used
    // to derive an AES waveform or confidentiality key.
    Bytes confidentiality_mac(std::span<const std::uint8_t> message) const;

private:
    friend struct KeyringAccess;
    Crypto() = default;
    struct ConfidentialityMaterial;
    std::array<std::array<std::uint8_t, 32>, 5> keys_{};
    const ConfidentialityMaterial* confidentiality_ = nullptr;
};

inline constexpr std::uint64_t keyfile_header_bytes = 128ULL * 1024 * 1024;
inline constexpr std::uint64_t minimum_keyfile_pad_bytes = 1024ULL * 1024 * 1024 + 1;

// A fresh physical-audio provider streams selected mono samples at provider
// rate until complete(). Calls must be externally serialized; the provider
// must unsubscribe and drain its callback before returning. The sink keeps only
// bounded transient scratch and health samples, cleansed at teardown; it makes
// no recording. This is supplementary entropy,
// not an entropy estimate or a claim about device noise.
class AudioEntropySink final {
public:
    ~AudioEntropySink();
    AudioEntropySink(const AudioEntropySink&) = delete;
    AudioEntropySink& operator=(const AudioEntropySink&) = delete;
    void add_samples(std::span<const float> mono, std::uint32_t sample_rate);
    bool complete() const noexcept;
private:
    friend struct KeyringAccess;
    AudioEntropySink();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
using KeyfileAudioSource = std::function<void(AudioEntropySink&, std::stop_token)>;
class AudioEntropyUnavailable : public Error {
public:
    using Error::Error;
};
struct KeyfileGenerationOptions {
    KeyfileAudioSource audio;
    std::stop_token stop;
    std::function<void(std::string_view)> progress;
    bool allow_audio_fallback=false;
};
struct KeyfileGenerationResult {
    bool audio_used=false;
    std::string warning;
};

[[nodiscard]] KeyfileGenerationResult create_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt,
                    const KeyfileGenerationOptions& generation = {});
Crypto load_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);

struct KeyEntry { std::string name; Crypto key; };
// Version 4 stores independently derived cipher roots in an authenticated
// cascade behind the 128 MiB random header. Older formats are rejected.
// No plaintext key export is exposed.
[[nodiscard]] KeyfileGenerationResult create_keyring(const std::filesystem::path& path, const std::vector<std::string>& names,
                    const std::optional<std::filesystem::path>& pad = std::nullopt,
                    const KeyfileGenerationOptions& generation = {});
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);

namespace testing {
// Explicit test fixture helpers. Production create/load never accept reduced
// header or pad sizes. There is deliberately no command-line option for these.
struct KeyfilePolicy {
    std::uint64_t header_bytes;
    std::uint64_t minimum_pad_bytes;
};
[[nodiscard]] KeyfileGenerationResult create_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt,
                    const KeyfileGenerationOptions& generation = {});
Crypto load_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
[[nodiscard]] KeyfileGenerationResult create_keyring(const std::filesystem::path& path, const std::vector<std::string>& names,
                    const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt,
                    const KeyfileGenerationOptions& generation = {});
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
} // namespace testing

} // namespace datapump
