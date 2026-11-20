#pragma once

#include "cuda_runtime.h"

enum __nv_fp4_interpretation_t { __NV_E2M1 };
enum cudaRoundMode { cudaRoundNearest };

namespace datapump_kernel_emulation {

inline float decode_e2m1(unsigned nibble) {
    constexpr float magnitudes[8] = {0.f, .5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f};
    const auto value = magnitudes[nibble & 7u];
    return nibble & 8u ? -value : value;
}

inline unsigned encode_e2m1(float value) {
    const unsigned sign = std::signbit(value) ? 8u : 0u;
    const auto magnitude = std::fabs(value);
    if (!(magnitude < 6.f)) return sign | (magnitude == magnitude ? 7u : 0u);
    unsigned lower = 0;
    while (decode_e2m1(lower + 1) <= magnitude) ++lower;
    const auto below = magnitude - decode_e2m1(lower), above = decode_e2m1(lower + 1) - magnitude;
    if (below != above) return sign | (below < above ? lower : lower + 1);
    return sign | (lower % 2 ? lower + 1 : lower);
}

} // namespace datapump_kernel_emulation

// The first value occupies the low four bits, as element 2 * byte of a row.
inline unsigned char __nv_cvt_float2_to_fp4x2(float2 value, __nv_fp4_interpretation_t, cudaRoundMode) {
    return static_cast<unsigned char>(datapump_kernel_emulation::encode_e2m1(value.x) |
                                      (datapump_kernel_emulation::encode_e2m1(value.y) << 4));
}
