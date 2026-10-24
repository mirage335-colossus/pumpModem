#pragma once

#include "datapump/crypto.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace datapump::helper {

enum class MetadataKind { callsign, clear_time, encrypted_time };

struct Metadata {
    MetadataKind kind=MetadataKind::callsign;
    std::string callsign;
    // Unsigned 48-bit Unix time: 32 whole seconds followed by 16 fractional bits.
    std::uint64_t time_ticks=0;
    // Neither encryption nor the optional error checksum authenticates metadata.
    bool authenticated=false;
};

struct MetadataPacket {
    std::array<std::uint32_t,9> symbols{};
    std::size_t count=0;
};

// Compact frames are MSB first in 21-bit symbols. The two type bits are 00 for
// callsign, 01 for clear time, 10 for encrypted time, and 11 reserved. Callsign
// content is five length bits then exactly that many seven-bit ASCII characters.
// Time content is six bits containing B-1, then B value bits, retaining Q16 Unix
// precision. Clear B is the minimum width (at least one); encrypted B is at least
// twenty for an FF1 domain of at least one million and otherwise the minimum.
//
// Symbol count is ceil(content_bits/21), including the type and length. A CRC7
// is appended only when at least seven unused bits already exist in that count;
// it never adds a symbol. CRC7/MMC uses polynomial 0x09, initial value zero,
// nonreflected MSB-first header+content bits and no final XOR. Remaining bits
// are zero. Nineteen-character callsigns use seven symbols; time uses at most
// three. The earlier fixed helper frames are deliberately superseded.
MetadataPacket callsign_packet(std::string_view);

// Encryption is NIST FF1 with radix two, ten rounds, an empty tweak and forward
// AES-256. Its separately derived key is Crypto::mac of the exact ASCII label
// "datapump/helper-time/v2/ff1-key". Integer arithmetic covers widths 20..48.
// It preserves the time field's length and adds no authentication tag. It is
// deterministic and does not conceal width. Throws on out-of-range time or
// unavailable/failed encryption; there is no clear fallback.
MetadataPacket timecode_packet(std::uint64_t ticks,const Crypto* key=nullptr);

// Rejects malformed/noncanonical lengths, symbols, padding, checksums or type.
// Encrypted time uses ONLY receive_keys.front(), the selected station key.
// There is no authentication: a wrong key can yield a plausible timestamp and
// cannot reliably be rejected or selected from a key ring. All returned metadata
// has authenticated=false. This codec changes no clocks. Missing AES fails closed.
std::optional<Metadata> decode_metadata_packet(
    std::span<const std::uint32_t>,std::span<const Crypto> receive_keys={});

} // namespace datapump::helper
