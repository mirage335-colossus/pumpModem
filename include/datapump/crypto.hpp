#pragma once

#include "datapump/types.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <span>

namespace datapump {

enum class StreamPurpose : std::uint8_t { Data, Dsss, Scrambler, Fhss };

// A 256-bit shared secret with independently derived stream and MAC keys.
// The timestamp is the same whole-second epoch for every modem layer;
// offset is a byte position within that epoch's stream, not wall-clock time.
class Crypto {
public:
    explicit Crypto(std::span<const std::uint8_t> master_key);
    ~Crypto();
    Crypto(const Crypto&) = default;
    Crypto& operator=(const Crypto&);

    static Crypto random();
    Bytes stream(StreamPurpose purpose, std::uint64_t timestamp,
                 std::uint64_t offset, std::size_t count) const;
    Bytes xor_data(std::span<const std::uint8_t> data, std::uint64_t timestamp,
                   std::uint64_t offset = 0) const;

    // HMAC-SHA256 over exactly message. Include framed metadata in message.
    Bytes mac(std::span<const std::uint8_t> message) const;
    bool verify(std::span<const std::uint8_t> message,
                std::span<const std::uint8_t> tag) const;

private:
    friend struct KeyringAccess;
    Crypto() = default;
    std::array<std::array<std::uint8_t, 32>, 5> keys_{};
};

inline constexpr std::uint64_t keyfile_header_bytes = 128ULL * 1024 * 1024;
inline constexpr std::uint64_t minimum_keyfile_pad_bytes = 1024ULL * 1024 * 1024 + 1;

void create_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);
Crypto load_keyfile(const std::filesystem::path& path,
                    const std::optional<std::filesystem::path>& pad = std::nullopt);

struct KeyEntry { std::string name; Crypto key; };
// Version 2 stores multiple independent named key sets in one authenticated,
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
