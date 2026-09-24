#pragma once
#include "datapump/types.hpp"
#include <span>

namespace datapump::boundary_sync {
// Plaintext bit-stream transform; constellation timing and encryption state
// belong entirely to their existing upstream/downstream layers.
inline constexpr std::size_t interval_bits = 2048;
inline constexpr std::size_t marker_bits = 192;
inline constexpr std::size_t maximum_slip_bits = 7;

// Storage and limits count individual 0/1 elements, not packed bytes. Each
// complete interval gets a marker, including an exact final interval.
// The input length must be byte aligned; size arithmetic is checked.
std::size_t encoded_size(std::size_t data_bits);
Bytes insert(std::span<const std::uint8_t> bits,
             std::size_t limit = default_memory_limit);

// Inspect only the fixed neighborhood of each expected marker. A unique exact
// pair restores the logical interval length by trimming surplus trailing bits
// or appending zero placeholders. It cannot reconstruct missing data. Damaged
// full marker slots are consumed at their nominal position; incomplete slots
// and the final partial data interval remain unchanged. This operation never
// recognizes a packet, starts another record, or authenticates its output.
Bytes recover(std::span<const std::uint8_t> wire_bits,
              std::size_t limit = default_memory_limit);
}
