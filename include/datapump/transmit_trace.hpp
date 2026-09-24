#pragma once
#include "datapump/types.hpp"
#include <cstddef>
#include <cstdint>

namespace datapump::modem {
// Bounded, immutable-by-value inspection of actual TX generation. Bit arrays
// contain individual 0/1 elements, so an incomplete final byte stays exact.
// Byte offsets in the source, wire, and pattern groups are separate domains.
struct TransmitTrace {
    static constexpr std::size_t source_limit=64, bit_limit=256, byte_limit=32;
    static constexpr std::size_t dynamic_storage_limit=source_limit+4*bit_limit+4*byte_limit;
    Bytes source;
    // Actual source encoder output before interval authentication/FEC/markers.
    // Long sources include their fixed-area source coding and zero padding.
    Bytes compressed_bits;
    // The actual input/output of Data XOR, including fixed markers and FEC.
    Bytes wire_plain_bits, wire_bits, data_key_bits;
    // Actual eight-byte circular I/Q mapper input/output sampled at the first
    // chip of each of the first four payload symbols, in symbol order. These
    // are symbol-start samples, not a contiguous prefix of all mapping bytes.
    // Private Pattern replaces the public stream; it is not a public XOR pad.
    // pattern_key repeats that private replacement input when selected.
    Bytes pattern_input, pattern_output, pattern_key, dsss_key;
    bool active=false, raw=false, short_text=false;
    bool source_available=false, compressed_available=false;
    bool data_masked=false, pattern_private=false, pattern_available=false;
    bool dsss=false, tone=false;
    std::size_t total_wire_bits=0, generated_bits=0;
    std::uint64_t generated_chips=0, revision=0;
    std::size_t working_bytes() const {
        return source.capacity()+compressed_bits.capacity()+wire_plain_bits.capacity()+
            wire_bits.capacity()+data_key_bits.capacity()+pattern_input.capacity()+
            pattern_output.capacity()+pattern_key.capacity()+dsss_key.capacity();
    }
};
}
