#pragma once

#include "cuda_runtime.h"

namespace datapump_kernel_emulation {

// Positive OCP E4M3 values, which the UE4M3 block scale shares.
inline float decode_e4m3(unsigned char code) {
    const unsigned exponent = (code >> 3) & 15u, mantissa = code & 7u;
    if (!exponent) return std::ldexp(static_cast<float>(mantissa), -9);
    return std::ldexp(static_cast<float>(8 + mantissa), static_cast<int>(exponent) - 10);
}

inline unsigned char encode_e4m3(float value) {
    constexpr unsigned char largest = 0x7e;
    if (!(value > 0)) return 0;
    if (!(value < decode_e4m3(largest))) return largest;
    unsigned char lower = 0;
    while (decode_e4m3(static_cast<unsigned char>(lower + 1)) <= value) ++lower;
    const auto below = value - decode_e4m3(lower);
    const auto above = decode_e4m3(static_cast<unsigned char>(lower + 1)) - value;
    if (below != above) return below < above ? lower : static_cast<unsigned char>(lower + 1);
    return lower % 2 ? static_cast<unsigned char>(lower + 1) : lower;
}

} // namespace datapump_kernel_emulation

struct __nv_fp8_e4m3 {
    unsigned char __x;
    explicit __nv_fp8_e4m3(float value) : __x(datapump_kernel_emulation::encode_e4m3(value)) {}
    explicit operator float() const { return datapump_kernel_emulation::decode_e4m3(__x); }
};
