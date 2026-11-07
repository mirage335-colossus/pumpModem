#pragma once

#include "datapump/types.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <memory>
#include <span>

namespace datapump {

enum class StreamPurpose : std::uint8_t { Data, Dsss, Scrambler, Fhss };
enum class StreamDomain : std::uint8_t {
    Payload, Preamble, Suppression, PatternZeroV2, PatternOneV2,
    OuterDsss10V1, OuterDsss100V1, OuterDsss1000V1, FakeFhssV1,
    OuterDsss10RotationV2, OuterDsss100RotationV2, OuterDsss1000RotationV2,
    OuterDsss10PermutationV2, OuterDsss100PermutationV2, OuterDsss1000PermutationV2, FhssCycleV2, CoverPreambleV1, CoverSuppressionV1, FhssUtcV3, CoverWaitV1,
    SchedHopKeyV1, SchedHopUtcV1, SymbolBurstWaitV1
};

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
    explicit Crypto(std::span<const std::uint8_t> master_key);
    ~Crypto();
    Crypto(const Crypto&) noexcept;
    Crypto& operator=(const Crypto&);

    static Crypto random();
    // Domains share the existing purpose/epoch key. The high counter bytes
    // are zero for Payload, ASCII "preamble" for Preamble, and ASCII
    // "suppress" for Suppression, "pat-v2-0" / "pat-v2-1" for the two
    // independent private pattern candidates. The low bytes
    // encode the block offset. Byte offsets cannot carry into the domain.
    // Outer V2 rotations use "dr2-0010"/"dr2-0100"/"dr2-1000";
    // symbol permutation seeds use "dp2-0010"/"dp2-0100"/"dp2-1000".
    // These six domains require the separate DSSS purpose key.
    // FhssCycleV2 uses "fh-cyc02" and requires the separate FHSS purpose key;
    // FhssUtcV3 uses "fh-utc03" under epoch zero for the canonical UTC calendar.
    // FakeFhssV1 is retained only for legacy diagnostic vectors.
    // CoverPreambleV1/ CoverSuppressionV1 use "cv-pre01"/"cv-sup01" to
    // derive child cover secrets; CoverWaitV1 uses "cv-wai01" for calendar waits.
    // No payload or FHSS byte range is reused.
    // SchedHopKeyV1 ("sh-key01") derives a geometry-specific child root using
    // epoch=group hops and offset=32*time multiplier. SchedHopUtcV1 ("sh-utc01")
    // uses that child root's FHSS purpose and epoch=UTC frame index. Both require
    // FHSS purpose; neither reuses channel-hop, DSSS, pattern or Data addresses.
    // SymbolBurstWaitV1 ("sb-wai01") uses FHSS purpose at the canonical payload
    // epoch and a separate 32-byte block at 32*sample-phase for each symbol.
    Bytes stream(StreamPurpose purpose, std::uint64_t timestamp,
                 std::uint64_t offset, std::size_t count,
                 StreamDomain domain = StreamDomain::Payload) const;
    Bytes xor_data(std::span<const std::uint8_t> data, std::uint64_t timestamp,
                   std::uint64_t offset = 0) const;
    // Successive Serpent-256-CTR and AES-256-CTR encryption layers. Modem
    // waveform generators continue to use the AES-only stream() API above.
    Bytes confidentiality_stream(std::uint64_t timestamp, std::uint64_t offset,
                                 std::size_t count) const;
    Crypto confidentiality_child(std::span<const std::uint8_t> context) const;
    // Fixed-width Serpent-256-CBC inner / AES-256-CBC outer cascade. The
    // caller authenticates IV and ciphertext before calling decrypt.
    std::unique_ptr<ConfidentialityCbc> prepare_confidentiality_cbc(
        std::span<const std::uint8_t> context, std::span<const std::uint8_t> salt) const;
    Bytes encrypt_confidentiality_cbc(std::span<const std::uint8_t> plaintext,
        std::span<const std::uint8_t> context, std::span<const std::uint8_t> salt,
        std::span<const std::uint8_t> wire_iv) const;
    Bytes decrypt_confidentiality_cbc(std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> context, std::span<const std::uint8_t> salt,
        std::span<const std::uint8_t> wire_iv) const;
    // Immutable confidentiality storage is shared by value copies. Workspace
    // owners charge each non-null identity once, in addition to sizeof(Crypto).
    // These values disclose allocation identity/size, never key bytes.
    const void* confidentiality_storage_id() const noexcept;
    std::size_t confidentiality_storage_bytes() const noexcept;

    // HMAC-SHA256 over exactly message. Include framed metadata in message.
    Bytes mac(std::span<const std::uint8_t> message) const;
    bool verify(std::span<const std::uint8_t> message,
                std::span<const std::uint8_t> tag) const;

private:
    friend struct KeyringAccess;
    Crypto() = default;
    struct ConfidentialityMaterial;
    std::array<std::array<std::uint8_t, 32>, 5> keys_{};
    const ConfidentialityMaterial* confidentiality_ = nullptr;
};

inline constexpr std::uint64_t keyfile_header_bytes = 128ULL * 1024 * 1024;
inline constexpr std::uint64_t minimum_keyfile_pad_bytes = 1024ULL * 1024 * 1024 + 1;

void create_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
Crypto load_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);

struct KeyEntry { std::string name; Crypto key; };
// Version 3 stores multiple independent named key sets in one authenticated,
// encrypted payload behind the same 128 MiB random header. Legacy v1 files
// load as one entry named "Default". No plaintext key export is exposed.
void create_keyring(const std::filesystem::path& path, const std::vector<std::string>& names,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);

namespace testing {
// Explicit test fixture helpers. Production create/load never accept reduced
// header or pad sizes. There is deliberately no command-line option for these.
struct KeyfilePolicy {
    std::uint64_t header_bytes;
    std::uint64_t minimum_pad_bytes;
};
void create_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
Crypto load_keyfile(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
void create_keyring(const std::filesystem::path& path, const std::vector<std::string>& names,
                    const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
std::vector<KeyEntry> load_keyring(const std::filesystem::path& path, const KeyfilePolicy& policy,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
} // namespace testing

} // namespace datapump
