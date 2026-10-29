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

// Kept for consumers which distinguish metadata from tone observations. The
// current wire record always carries both the time phase and the callsign.
enum class MetadataKind { callsign, clear_time, encrypted_time };

struct Metadata {
    MetadataKind kind=MetadataKind::callsign;
    std::string callsign;
    // 14-bit phase of Unix time in the 8.125-second cycle. One step is
    // 65/131072 seconds (approximately 495.91 microseconds).
    std::uint64_t time_ticks=0;
    bool authenticated=false;
};

struct MetadataPacket {
    std::array<std::uint32_t,9> symbols{};
    std::size_t count=0;
};

inline constexpr std::uint64_t metadata_period_q16=532480; // 8.125 seconds
inline constexpr std::size_t metadata_symbol_count=9;
inline constexpr std::size_t metadata_max_characters=16;

// Fixed 126-bit record, MSB first in nine 14-bit symbols. The first symbol is
// the phase of Unix Q16 time, measured at the dwell start, in an 8.125-second
// period. Sixteen seven-bit ASCII slots follow; zero pads unused slots. NUL is
// reserved for padding and may not appear within text. No checksum or
// authentication is carried. A record never crosses into the next dwell.
//
// With a selected key the phase remains clear and the 112 character bits use
// AES-256-CTR. The independent AES key is the station key's HMAC-SHA256 of
// "datapump/helper-aux/v3/ctr-key". Its IV is big-endian absolute six-second
// bucket (64 bits), 400-ms dwell within that bucket (32 bits), block counter
// zero (32 bits). Keyed timestamps must be nominal physical dwell starts.
// Use a station-specific key and one fixed character record per absolute
// dwell: another sender or changed record in that same key/dwell reuses CTR.
// Cipher failure throws before output and never falls back to clear text.
MetadataPacket metadata_packet(std::uint64_t unix_q16_ticks,std::string_view callsign,
                               const Crypto* key=nullptr);

// Convenience wrappers for callers constructing reference geometry.
MetadataPacket callsign_packet(std::string_view);
MetadataPacket timecode_packet(std::uint64_t ticks,const Crypto* key=nullptr);

std::optional<Metadata> decode_metadata_packet(
    std::span<const std::uint32_t>,std::span<const Crypto> receive_keys={},
    // Capture-time estimate of the transmitted dwell start. The clear phase
    // resolves the absolute dwell without a search for clocks within +/-4 s.
    // Keyed decoding without this anchor fails closed. Only the first key is
    // selected; wrong keys can yield plausible unauthenticated ASCII.
    std::optional<std::uint64_t> approximate_unix_q16_ticks={});

} // namespace datapump::helper
