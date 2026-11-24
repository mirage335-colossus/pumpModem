#pragma once
#include "datapump/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace datapump::modem {
struct TransmitPatternPreview {
    std::uint64_t epoch = 0, ordinal = 0, inner_coefficients = 0;
    // A bounded prefix from tile zero, indexed by input bit before masking.
    // Each big-endian word packs the amplitude permutation and the three
    // carrier phase octants, before the Added Symbols factors.
    std::array<Bytes, 2> prefixes;
};

// Bounded, immutable-by-value inspection of actual TX generation. Bit arrays
// contain individual 0/1 elements, so an incomplete final byte stays exact.
// Byte offsets in the source, wire, and pattern groups are separate domains.
struct TransmitTrace {
    static constexpr std::size_t source_limit=64, bit_limit=256, byte_limit=32;
    static constexpr std::size_t dynamic_storage_limit=source_limit+bit_limit+3*byte_limit;
    Bytes source;
    // The input to native DATA emission, including configured markers and FEC.
    Bytes wire_plain_bits;
    std::optional<TransmitPatternPreview> pattern;
    Bytes fhss_key;
    // Actual TX-owned schedule; the GUI uses this channel, never a second PRNG.
    bool fhss=false,fhss_available=false,temporary_key=false;
    std::uint64_t fhss_epoch=0,fhss_hop=0,fhss_cycle=0,fhss_byte_offset=0;
    unsigned fhss_channel=0,fhss_channels=0;
    double fhss_shift_hz=0;
    // active is message generation only; temporary_key enables noise diagnostics
    // without inventing a message, source bytes or a finite wire endpoint.
    bool active=false, raw=false;
    bool emitting=false; // Send preparation or emission, excluding capture replay.
    bool source_available=false;
    std::size_t total_wire_bits=0, generated_bits=0;
    std::uint64_t revision=0;
    std::size_t working_bytes() const {
        return source.capacity()+wire_plain_bits.capacity()+fhss_key.capacity()+
            (pattern ? pattern->prefixes[0].capacity() + pattern->prefixes[1].capacity() : 0);
    }
};
}
