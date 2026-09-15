#pragma once
#include "datapump/types.hpp"
#include <span>

namespace datapump::boundary_sync {
// Plaintext bit-stream transform; constellation timing and encryption state
// belong entirely to their existing upstream/downstream layers.
inline constexpr std::size_t interval_bits = 2048;
inline constexpr std::size_t marker_bits = 192;
inline constexpr std::size_t maximum_slip_bits = 7;
inline constexpr std::size_t maximum_marker_loss_bits = 80;
inline constexpr std::size_t maximum_marker_errors = 8;
inline constexpr std::size_t marker_tail_bits = 32;
inline constexpr unsigned false_match_bits = 84;

// Packet streams begin with a marker. Each complete data interval gets another
// marker, including an exact final interval. Raw bits and short dictionary text
// bypass this transform. Storage and limits count 0/1 elements, not packed bytes.
// The input length must be byte aligned and elements must be 0/1; size arithmetic
// is checked. Transmit insertion never accepts unknown bit placeholders.
std::size_t encoded_size(std::size_t data_bits);
Bytes insert(std::span<const std::uint8_t> bits,
             std::size_t limit = default_memory_limit);

struct Recovery {
    Bytes bits;
    bool leading_marker_recognized = false;
};

// Match only the burst origin and fixed periodic neighborhoods. Besides exact
// markers, accept bounded substitutions or one contiguous marker deletion with
// an intact trailing anchor. Recovery additionally accepts element value 2 for
// an unknown timed bit slot. Unknown slots retain their position, add no marker
// evidence, and cannot satisfy an inferred endpoint's exact trailing anchor.
// Output replaces unknown data slots with zero for downstream packet FEC.
// The iid fair-bit false-match union bound on known observations across
// this call must be <= 2^-false_match_bits; this is not a channel calibration
// or authentication. Distinct plausible endpoints are rejected. Recovery trims
// or zero-fills the preceding interval; packet FEC/integrity remain downstream.
// Unrecognized complete slots retain nominal removal. A recognized leading
// marker identifies packet framing even when subsequent packet validation fails.
// leading_missing_bits comes only from the acquired stream position, never a
// trial decryption offset, and fixes the surviving initial suffix's endpoint.
// That known endpoint permits substitutions and unknown slots in its trailer.
Recovery recover_packet(std::span<const std::uint8_t> wire_bits,
                        std::size_t limit = default_memory_limit,
                        std::size_t leading_missing_bits = 0);
// Convenience wrapper for callers that only need the recovered packet bits.
Bytes recover(std::span<const std::uint8_t> wire_bits,
              std::size_t limit = default_memory_limit);
}
