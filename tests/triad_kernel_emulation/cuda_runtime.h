#pragma once

// Host stand-ins for the CUDA device language used by the SM120 OpenCL
// kernels. Each logical thread runs as a fiber, and warp or block operations
// rendezvous in the emulator. This checks kernel indexing, packing and host
// orchestration on a machine without the device. It is not device evidence:
// the instruction model's accumulation order and rounding are its own, and
// the library compiles without multiply-add contraction while NVCC contracts.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <math.h>

#define __global__
#define __device__
#define __host__
#define __shared__ static
#define __forceinline__ inline

struct alignas(8) float2 { float x, y; };
struct alignas(16) float4 { float x, y, z, w; };
struct alignas(16) uint4 { unsigned int x, y, z, w; };
struct alignas(16) double2 { double x, y; };
struct double4 { double x, y, z, w; };

inline float2 make_float2(float x, float y) { return {x, y}; }
inline float4 make_float4(float x, float y, float z, float w) { return {x, y, z, w}; }
inline double2 make_double2(double x, double y) { return {x, y}; }
inline double4 make_double4(double x, double y, double z, double w) { return {x, y, z, w}; }

namespace datapump_kernel_emulation {

struct Index { unsigned x = 0, y = 0, z = 0; };
struct Thread { Index thread, block, block_dimensions, grid_dimensions; };

Thread& current();
std::uint64_t shuffle_xor(std::uint64_t bits, unsigned lane_mask, unsigned width);
void synchronize();
bool synchronize_or(bool predicate);
void mxf4nvf4_m16n8k64(float4& accumulator, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                       std::uint32_t b0, std::uint32_t b1, std::uint32_t scale_a, std::uint32_t scale_b);

} // namespace datapump_kernel_emulation

#define threadIdx (datapump_kernel_emulation::current().thread)
#define blockIdx (datapump_kernel_emulation::current().block)
#define blockDim (datapump_kernel_emulation::current().block_dimensions)
#define gridDim (datapump_kernel_emulation::current().grid_dimensions)

template<class T>
T __shfl_xor_sync(unsigned, T value, unsigned lane_mask, unsigned width = 32) {
    static_assert(sizeof(T) <= sizeof(std::uint64_t));
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(T));
    bits = datapump_kernel_emulation::shuffle_xor(bits, lane_mask, width);
    std::memcpy(&value, &bits, sizeof(T));
    return value;
}

inline void __syncthreads() { datapump_kernel_emulation::synchronize(); }
inline int __syncthreads_or(int predicate) { return datapump_kernel_emulation::synchronize_or(predicate != 0); }

inline unsigned atomicOr(unsigned* address, unsigned value) {
    const auto previous = *address;
    *address = previous | value;
    return previous;
}

inline unsigned __float_as_uint(float value) {
    unsigned bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float rsqrtf(float value) { return 1.f / std::sqrt(value); }
inline void __sincosf(float angle, float* sine, float* cosine) { *sine = std::sin(angle); *cosine = std::cos(angle); }

inline double __dadd_rn(double a, double b) { return a + b; }
inline double __dsub_rn(double a, double b) { return a - b; }
inline double __dmul_rn(double a, double b) { return a * b; }
inline double __ddiv_rn(double a, double b) { return a / b; }
inline float __fadd_rn(float a, float b) { return a + b; }
inline float __fsub_rn(float a, float b) { return a - b; }
inline float __fmul_rn(float a, float b) { return a * b; }
inline float __fmaf_rn(float a, float b, float c) { return std::fma(a, b, c); }
inline float __double2float_rn(double value) { return static_cast<float>(value); }
inline long long __double_as_longlong(double value) {
    long long bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
inline unsigned long long __umul64hi(unsigned long long a, unsigned long long b) {
    return static_cast<unsigned long long>((static_cast<unsigned __int128>(a) * b) >> 64);
}
// The device function is accurate to about one unit in the last place. Its
// rounding is the device's own, so emulated values are not device evidence.
inline void sincospif(float x, float* sine, float* cosine) {
    constexpr double pi = 3.14159265358979323846;
    *sine = static_cast<float>(std::sin(pi * x));
    *cosine = static_cast<float>(std::cos(pi * x));
}

#define CUDART_INF_F (std::numeric_limits<float>::infinity())
#define CUDART_NAN_F (std::numeric_limits<float>::quiet_NaN())
#define CUDART_NAN (std::numeric_limits<double>::quiet_NaN())
