#pragma once
#include "datapump/fast/profile.hpp"
#include <span>

namespace datapump::fast::ldpc {
// DVB-S2/S2X normal frames. No BCH, shortening, puncturing or implicit padding.
inline constexpr std::size_t coded_bits = 64800;
inline constexpr unsigned default_iterations = 50;
inline constexpr unsigned maximum_iterations = 100;

std::size_t data_bits(CodeRate rate);
Bytes encode(std::span<const std::uint8_t> bytes, CodeRate rate);
struct DecodeResult {
    Bytes bytes;
    bool converged = false;
    unsigned iterations = 0;
};
// Positive soft values mean bit 1: log(P(bit=1)/P(bit=0)). Exactly one frame.
// NaN and infinity are rejected; finite inputs are clipped to +/-50.
// A valid syndrome is error-correction evidence, not payload integrity.
DecodeResult decode(std::span<const float> soft, CodeRate rate,
                    unsigned iterations = default_iterations);
bool valid_codeword(std::span<const std::uint8_t> bits, CodeRate rate);

// Frozen, rate-independent permutation of all 64800 code bits. It mixes the
// unequal-reliability constellation bit positions; it adds no transmitted bits.
// interleave(bits)[i] = bits[interleave_index(i)]. Decode takes deinterleaved LLRs.
std::size_t interleave_index(std::size_t transmitted_bit);
Bytes interleave(std::span<const std::uint8_t> bits);
std::vector<float> deinterleave(std::span<const float> soft);
}
