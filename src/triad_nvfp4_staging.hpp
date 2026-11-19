#pragma once

#include <algorithm>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DATAPUMP_NVFP4_STAGING_AVX2 1
#endif

namespace datapump::triad::v3::receiver_detail {

#if defined(DATAPUMP_NVFP4_STAGING_AVX2)
// Convert eight successive I/Q components at a time. Unsigned integer
// maximum orders the absolute FP32 values, including subnormals and nonfinite
// results, without relying on compiler fast-math reassociation.
__attribute__((target("avx2"))) inline std::size_t stage_nvfp4_samples_avx2(
    const double* input, float* output, std::size_t size, std::uint32_t& peak_bits) {
    const auto absolute = _mm256_set1_epi32(0x7fff'ffff);
    auto peak = _mm256_setzero_si256();
    std::size_t i = 0;
    for (; size - i >= 8; i += 8) {
        const auto low = _mm256_cvtpd_ps(_mm256_loadu_pd(input + i));
        const auto high = _mm256_cvtpd_ps(_mm256_loadu_pd(input + i + 4));
        const auto converted = _mm256_set_m128(high, low);
        _mm256_storeu_ps(output + i, converted);
        peak = _mm256_max_epu32(peak, _mm256_and_si256(_mm256_castps_si256(converted), absolute));
    }
    auto reduced = _mm_max_epu32(_mm256_castsi256_si128(peak), _mm256_extracti128_si256(peak, 1));
    reduced = _mm_max_epu32(reduced, _mm_shuffle_epi32(reduced, 0x4e));
    reduced = _mm_max_epu32(reduced, _mm_shuffle_epi32(reduced, 0xb1));
    peak_bits = static_cast<std::uint32_t>(_mm_cvtsi128_si32(reduced));
    return i;
}
#endif

// Copy one native mapped template into its lane-major OpenCL upload record.
// The extra complex sample holds {absolute peak of the rounded components,0}.
// Nonfinite components produce a nonfinite trailer for the host range check.
inline void stage_nvfp4_lane(std::span<const std::complex<double>> source, std::span<std::complex<float>> staged) {
    static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(std::uint32_t));
    if (staged.empty() || staged.size() - 1 != source.size())
        throw std::invalid_argument("Robust NVFP4 staged lane has an invalid extent");
    const auto* input = reinterpret_cast<const double*>(source.data());
    auto* output = reinterpret_cast<float*>(staged.data());
    const auto size = 2 * source.size();
    std::size_t i = 0;
    std::uint32_t peak_bits = 0;
#if defined(DATAPUMP_NVFP4_STAGING_AVX2)
    if (size >= 8 && __builtin_cpu_supports("avx2"))
        i = stage_nvfp4_samples_avx2(input, output, size, peak_bits);
#endif
    for (; i < size; ++i) {
        const auto value = static_cast<float>(input[i]);
        output[i] = value;
        peak_bits = std::max(peak_bits, std::bit_cast<std::uint32_t>(value) & 0x7fff'ffffU);
    }
    staged.back() = {std::bit_cast<float>(peak_bits), 0.f};
}

} // namespace datapump::triad::v3::receiver_detail

#undef DATAPUMP_NVFP4_STAGING_AVX2
