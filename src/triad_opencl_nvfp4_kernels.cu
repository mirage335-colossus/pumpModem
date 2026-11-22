// Compile device code offline for SM120. The NVIDIA OpenCL worker loads it
// as native kernel binaries, without loading or initializing CUDA Runtime.
#include <cuda_fp4.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>
#include <math_constants.h>

#include <cstddef>
#include <cstdint>

#include "triad_opencl_fine_fft.cuh"

namespace {

constexpr unsigned threads = 128;
constexpr unsigned warp_size = 32;
constexpr unsigned scale_width = 16;
constexpr unsigned tile_rows = 16;
constexpr unsigned tile_columns = 8;
constexpr unsigned tile_inner = 64;
constexpr unsigned full_warp = 0xffffffffu;
constexpr float maximum_scale = 448.f;
constexpr float normalized_max = 6.f * maximum_scale;
constexpr unsigned source_tile_samples = 8192;
constexpr unsigned maximum_source_tiles = 16;
constexpr unsigned unpaired_output = 0xffffu;
constexpr unsigned candidate_lanes = 4;
constexpr unsigned bit_statistics = 5;
constexpr unsigned candidate_statistics = 2 * bit_statistics + 1;

// One mapped column, or the two bit columns of one tap that share a map.
// Tiles are cache slots: the first column's, then the second's.
struct DeviceMapping {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::uint16_t output, second_output;
    std::uint32_t tile_count;
    std::uint8_t tiles[maximum_source_tiles];
};
static_assert(sizeof(DeviceMapping) == 64 && alignof(DeviceMapping) == 8);
static_assert(offsetof(DeviceMapping, second_output) == 42 && offsetof(DeviceMapping, tiles) == 48);

// One dense source tile to render: its first dense sample, its keyed lane
// and the cache slot that receives it.
struct TileRequest {
    std::uint64_t first;
    std::uint32_t lane, slot;
};
static_assert(sizeof(TileRequest) == 16 && alignof(TileRequest) == 8);

__device__ const double2* source_tile(const double2* samples, const std::uint8_t* tiles, unsigned tile) {
    return samples + static_cast<std::uint64_t>(tiles[tile]) * source_tile_samples;
}

__device__ double2 source_neighbor(const double2* samples, const std::uint8_t* tiles,
                                   std::int64_t index, std::int64_t extent) {
    if (index < 0 || index >= extent) return {};
    return source_tile(samples, tiles, index / source_tile_samples)[index % source_tile_samples];
}

__device__ double cubic(double a, double b, double c, double d, double w0, double w1, double w2, double w3) {
    return __dadd_rn(__dadd_rn(__dadd_rn(__dmul_rn(w0, a), __dmul_rn(w1, b)), __dmul_rn(w2, c)), __dmul_rn(w3, d));
}

__device__ void render_lane_row(const double2* samples, const std::uint8_t* tiles,
                                std::int64_t left, std::int64_t extent, const double4 weights,
                                double sine, double cosine, float2* lane, unsigned row, unsigned rows) {
    double2 a, b, c, d;
    const auto local = static_cast<unsigned>(left) % source_tile_samples;
    if (left >= 0 && local + 3 < source_tile_samples) {
        const auto* tile = source_tile(samples, tiles, left / source_tile_samples) + local;
        a = tile[0];
        b = tile[1];
        c = tile[2];
        d = tile[3];
    } else {
        a = source_neighbor(samples, tiles, left, extent);
        b = source_neighbor(samples, tiles, left + 1, extent);
        c = source_neighbor(samples, tiles, left + 2, extent);
        d = source_neighbor(samples, tiles, left + 3, extent);
    }
    const auto real = cubic(a.x, b.x, c.x, d.x, weights.x, weights.y, weights.z, weights.w);
    const auto imag = cubic(a.y, b.y, c.y, d.y, weights.x, weights.y, weights.z, weights.w);
    const auto rotated_real = __dsub_rn(__dmul_rn(real, cosine), __dmul_rn(imag, sine));
    const auto rotated_imag = __dadd_rn(__dmul_rn(real, sine), __dmul_rn(imag, cosine));
    const auto narrowed = make_float2(rotated_real, rotated_imag);
    lane[row] = narrowed;
    // The native bulk path checks the original C128 template for nonzero
    // support. Record that witness only when narrowing would erase it.
    if (narrowed.x == 0 && narrowed.y == 0 && (rotated_real != 0 || rotated_imag != 0))
        atomicOr(reinterpret_cast<unsigned*>(&lane[rows].y), __float_as_uint(1.f));
}

// The block scale of this thread's sixteen elements, and the E2M1 pair that
// an even lane and its neighbor form.
struct PackedElement {
    std::uint8_t scale, pair;
};

__device__ PackedElement pack_element(float value) {
    // Each padded row is a whole number of warps and of sixteen-element
    // scale blocks, and bounds checks discard whole warps, so every lane of
    // a scale block reaches each shuffle.
    float maximum = fabsf(value);
    for (unsigned bit = scale_width / 2; bit; bit /= 2)
        maximum = fmaxf(maximum, __shfl_xor_sync(full_warp, maximum, bit, scale_width));
    // Use the full range of the nested block scale and E2M1 value. The
    // product stage removes the shared normalized_max factors afterwards.
    const __nv_fp8_e4m3 scale(maximum * maximum_scale);
    const auto represented = static_cast<float>(scale);
    const auto normalized = represented > 0 ? value * (normalized_max / represented) : 0.f;
    const auto sibling = __shfl_xor_sync(full_warp, normalized, 1, scale_width);
    return {static_cast<std::uint8_t>(scale.__x),
            __nv_cvt_float2_to_fp4x2(make_float2(normalized, sibling), __NV_E2M1, cudaRoundNearest)};
}

__device__ void pack_block(float value, std::uint32_t* packed, std::uint8_t* scales, unsigned index) {
    const auto element = pack_element(value);
    const auto lane = threadIdx.x % scale_width;
    if (!lane) scales[index / scale_width] = element.scale;
    if (!(lane % 2)) reinterpret_cast<std::uint8_t*>(packed)[index / 2] = element.pair;
}

__device__ void multiply_tile(float4& accumulator,
    std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t b0, std::uint32_t b1,
    std::uint32_t scale_a, std::uint32_t scale_b) {
#ifdef DATAPUMP_TRIAD_KERNEL_EMULATION
    // Host tests model the warp instruction with the same register layout.
    // That checks indexing and packing. It is not device arithmetic evidence.
    datapump_kernel_emulation::mxf4nvf4_m16n8k64(accumulator, a0, a1, a2, a3, b0, b1, scale_a, scale_b);
#else
    constexpr std::uint16_t selector = 0;
    asm volatile(
        "mma.sync.aligned.m16n8k64.row.col.kind::mxf4nvf4.block_scale.scale_vec::4X.f32.e2m1.e2m1.f32.ue4m3 "
        "{%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, {%0,%1,%2,%3}, %10, {%12,%12}, %11, {%12,%12};"
        : "+f"(accumulator.x), "+f"(accumulator.y), "+f"(accumulator.z), "+f"(accumulator.w)
        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(b0), "r"(b1),
          "r"(scale_a), "r"(scale_b), "h"(selector));
#endif
}

__device__ const std::uint32_t* scale_row(const std::uint8_t* scales, unsigned row, unsigned inner) {
    // Each scale vector holds the four consecutive block-16 scales for a
    // K=64 tile, least significant byte first. Buffers and rows are aligned.
    return reinterpret_cast<const std::uint32_t*>(scales) + row * (inner / tile_inner);
}

} // namespace

extern "C" __global__ void resident_fine_map(const double2* samples, const DeviceMapping* descriptors,
    float2* mapped, std::uint32_t rows, std::uint32_t mappings) {
    using datapump::triad::v3::receiver_detail::opencl_fine_fft::fine_rows;
    if (rows != fine_rows) return;
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto descriptor = index / fine_rows;
    if (descriptor >= mappings) return;
    const auto row = index % fine_rows;
    const auto& mapping = descriptors[descriptor];
    const auto extent = static_cast<std::int64_t>(mapping.tile_count) * source_tile_samples;
    const auto lane = [&](unsigned output) { return mapped + static_cast<std::uint64_t>(output) * (fine_rows + 1); };
    auto* first = lane(mapping.output);
    const auto second = mapping.second_output;

    // Use the same separate FP64 operations as the canonical native mapper.
    // A fused affine calculation could select a different cubic stencil.
    const auto offset = static_cast<double>(row);
    const auto relative = __dadd_rn(__dmul_rn(offset, mapping.scale), mapping.fraction);
    const auto integral = floor(relative);
    const auto left = mapping.origin + static_cast<std::int64_t>(integral) - 1;
    if (left < -3 || left >= extent || !extent) {
        first[row] = {};
        if (second != unpaired_output) lane(second)[row] = {};
        return;
    }

    const auto t = relative - integral;
    const auto weights = make_double4(-t * (t - 1) * (t - 2) / 6, (t + 1) * (t - 1) * (t - 2) / 2,
                                      -(t + 1) * t * (t - 2) / 2, (t + 1) * t * (t - 1) / 6);
    constexpr double tau = 6.2831853071795864769;
    const auto phase = __dadd_rn(__dmul_rn(mapping.phase_step, offset), mapping.phase);
    double sine, cosine;
    sincos(tau * (phase - floor(phase)), &sine, &cosine);
    render_lane_row(samples, mapping.tiles, left, extent, weights, sine, cosine, first, row, rows);
    if (second != unpaired_output)
        render_lane_row(samples, mapping.tiles + mapping.tile_count, left, extent, weights,
                        sine, cosine, lane(second), row, rows);
}

namespace {

// The magnitude of a finite value below 2^63 in 64.64 fixed point: its
// integer part, and its fraction in units of 2^-64. Bits below 2^-64 are
// dropped. Only integer operations read the FP64 encoding.
struct Fixed {
    std::uint64_t whole, fraction;
};

__device__ Fixed fixed_point(double value) {
    const auto bits = static_cast<std::uint64_t>(__double_as_longlong(value));
    const auto exponent = static_cast<int>(bits >> 52 & 0x7ff);
    if (!exponent) return {0, 0};
    const auto mantissa = (bits & 0xfffffffffffffull) | 0x10000000000000ull;
    // value * 2^64 = mantissa * 2^shift.
    const auto shift = exponent - 1011;
    if (shift >= 64) return {mantissa << (shift - 64), 0};
    if (shift > 0) return {mantissa >> (64 - shift), mantissa << shift};
    if (shift > -64) return {0, mantissa >> -shift};
    return {0, 0};
}

// A finite value in turns, as a 64-bit fraction of a turn: whole turns vanish
// exactly, and the bits below 2^-64 of a turn are dropped.
__device__ std::uint64_t fixed_turns(double value) {
    const auto magnitude = fixed_point(value).fraction;
    return __double_as_longlong(value) < 0 ? 0 - magnitude : magnitude;
}

// A descriptor's stencil scale and fraction in 64.64 fixed point, and its
// phase and phase step in turns, decoded once for the rows of a block.
struct FixedMapping {
    std::uint64_t scale_whole, scale_fraction, base_whole, base_fraction, phase, step;
};

__device__ FixedMapping fixed_mapping(const DeviceMapping& mapping) {
    const auto scale = fixed_point(mapping.scale), base = fixed_point(mapping.fraction);
    return {scale.whole, scale.fraction, base.whole, base.fraction, fixed_turns(mapping.phase),
            fixed_turns(mapping.phase_step)};
}

__device__ float2 widen_to_float(double2 value) {
    return make_float2(static_cast<float>(value.x), static_cast<float>(value.y));
}

// The four-point interpolation of one lane in FP32 from its FP64 tile
// samples, and the fp32-rn4-v1 rotation of the CUDA mapper. The witness
// records a lane that reads a nonzero sample under a nonzero weight while
// its FP32 value is zero.
__device__ void render_lane_row_fp32(const double2* samples, const std::uint8_t* tiles,
                                     std::int64_t left, std::int64_t extent, const float (&weights)[4],
                                     float sine, float cosine, float2* lane, unsigned row, unsigned rows) {
    double2 stencil[4];
    const auto local = static_cast<unsigned>(left) % source_tile_samples;
    if (left >= 0 && local + 3 < source_tile_samples) {
        const auto* tile = source_tile(samples, tiles, left / source_tile_samples) + local;
        for (unsigned i = 0; i < 4; ++i) stencil[i] = tile[i];
    } else {
        for (unsigned i = 0; i < 4; ++i) stencil[i] = source_neighbor(samples, tiles, left + i, extent);
    }
    float real = 0, imag = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto value = widen_to_float(stencil[i]);
        real = i ? __fmaf_rn(weights[i], value.x, real) : __fmul_rn(weights[i], value.x);
        imag = i ? __fmaf_rn(weights[i], value.y, imag) : __fmul_rn(weights[i], value.y);
    }
    const auto rotated = make_float2(__fsub_rn(__fmul_rn(real, cosine), __fmul_rn(imag, sine)),
                                     __fadd_rn(__fmul_rn(real, sine), __fmul_rn(imag, cosine)));
    lane[row] = rotated;
    if (rotated.x != 0 || rotated.y != 0) return;
    for (unsigned i = 0; i < 4; ++i)
        if (weights[i] != 0 && (stencil[i].x != 0 || stencil[i].y != 0)) {
            atomicOr(reinterpret_cast<unsigned*>(&lane[rows].y), __float_as_uint(1.f));
            return;
        }
}

} // namespace

// The FP32 mapper. Each row's coordinate and phase are reduced exactly in
// fixed point from the FP64 descriptor: the stencil coordinate as 64.64 and
// the phase as a 64-bit fraction of a turn, both with errors below 2^-53 of
// a sample or a turn over the block. A block decodes each of its at most
// two descriptors once. Weights, interpolation, the sine and cosine of the
// reduced angle and the rotation are FP32: no FP64 arithmetic remains per
// row, only the narrowing of the FP64 tile samples it reads and three
// 64-bit integer conversions. The FP64 resident_fine_map is the matched
// control in the same module.
extern "C" __global__ void resident_fine_map_fp32(const double2* samples, const DeviceMapping* descriptors,
    float2* mapped, std::uint32_t rows, std::uint32_t mappings) {
    using datapump::triad::v3::receiver_detail::opencl_fine_fft::fine_rows;
    // Both conditions hold for the whole block, so every thread returns or
    // reaches the barrier.
    if (rows != fine_rows || blockDim.x > fine_rows) return;
    __shared__ FixedMapping decoded[2];
    const auto block_first = blockIdx.x * blockDim.x;
    const auto first_descriptor = block_first / fine_rows;
    if (threadIdx.x < 2 && first_descriptor + threadIdx.x < mappings)
        decoded[threadIdx.x] = fixed_mapping(descriptors[first_descriptor + threadIdx.x]);
    __syncthreads();
    const auto index = block_first + threadIdx.x;
    const auto descriptor = index / fine_rows;
    if (descriptor >= mappings) return;
    const auto row = index % fine_rows;
    const auto& mapping = descriptors[descriptor];
    const auto& fixed = decoded[descriptor - first_descriptor];
    const auto extent = static_cast<std::int64_t>(mapping.tile_count) * source_tile_samples;
    const auto lane = [&](unsigned output) { return mapped + static_cast<std::uint64_t>(output) * (fine_rows + 1); };
    auto* first = lane(mapping.output);
    const auto second = mapping.second_output;

    // fraction + row * scale, exactly but for the dropped bits of the two
    // operands. The session admits fractions in [0, 1], nonnegative scales
    // and coordinates up to 2^31.
    const std::uint64_t offset = row;
    const auto product = offset * fixed.scale_fraction;
    const auto fraction = product + fixed.base_fraction;
    const auto whole = offset * fixed.scale_whole + __umul64hi(offset, fixed.scale_fraction) + fixed.base_whole +
                       (fraction < product);
    const auto left = mapping.origin + static_cast<std::int64_t>(whole) - 1;
    if (left < -3 || left >= extent || !extent) {
        first[row] = {};
        if (second != unpaired_output) lane(second)[row] = {};
        return;
    }

    // The stencil fraction t and its complement v = 1 - t, each to FP32
    // relative precision, so the weights that vanish at either end of the
    // stencil keep their own relative precision.
    const auto t = static_cast<float>(fraction) * 0x1p-64f;
    const auto v = fraction ? static_cast<float>(0 - fraction) * 0x1p-64f : 1.f;
    const float more_t = 1 + t, more_v = 1 + v;
    const float weights[4] = {-t * v * more_v * (1.f / 6.f), more_t * v * more_v * .5f,
                              more_t * t * more_v * .5f, -more_t * t * v * (1.f / 6.f)};
    // phase + row * phase_step modulo one turn, then the signed half turns
    // of the reduced angle.
    const auto turns = fixed.phase + offset * fixed.step;
    float sine, cosine;
    sincospif(static_cast<float>(static_cast<std::int64_t>(turns)) * 0x1p-63f, &sine, &cosine);
    // One call site, so the compiler inlines it and keeps the weights in
    // registers.
    const unsigned outputs = second != unpaired_output ? 2 : 1;
    for (unsigned output = 0; output < outputs; ++output)
        render_lane_row_fp32(samples, mapping.tiles + output * mapping.tile_count, left, extent, weights,
                             sine, cosine, output ? lane(second) : first, row, rows);
}

// Resets each lane's trailer before a batch is mapped: the peak, and the
// presence witness that the mapper or a direct upload sets.
extern "C" __global__ void resident_fine_clear(float2* mapped, std::uint32_t rows, std::uint32_t lanes) {
    const auto lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane < lanes) mapped[static_cast<std::uint64_t>(lane) * (rows + 1) + rows] = make_float2(0.f, 0.f);
}

// Zeroes the samples of every lane that received no column. Mapped and
// uploaded lanes are written in full, so no other storage needs clearing.
// One block owns one lane.
extern "C" __global__ void resident_fine_absent(float2* mapped, const std::uint8_t* present, std::uint32_t rows,
    std::uint32_t lanes) {
    const auto lane = blockIdx.x;
    if (lane >= lanes || present[lane]) return;
    auto* samples = mapped + static_cast<std::uint64_t>(lane) * (rows + 1);
    for (auto row = threadIdx.x; row < rows; row += threads) samples[row] = make_float2(0.f, 0.f);
}

extern "C" __global__ void resident_fine_peaks(float2* mapped, std::uint32_t rows, std::uint32_t lanes) {
    const auto lane = blockIdx.x;
    if (lane >= lanes) return;
    auto* samples = mapped + static_cast<std::uint64_t>(lane) * (rows + 1);
    __shared__ float warp_peak[threads / warp_size];
    float peak = 0;
    for (auto row = threadIdx.x; row < rows; row += threads) {
        const auto value = samples[row];
        peak = fmaxf(peak, isfinite(value.x) && isfinite(value.y)
            ? fmaxf(fabsf(value.x), fabsf(value.y)) : CUDART_INF_F);
    }
    for (unsigned distance = warp_size / 2; distance; distance /= 2)
        peak = fmaxf(peak, __shfl_xor_sync(full_warp, peak, distance));
    if (threadIdx.x % warp_size == 0) warp_peak[threadIdx.x / warp_size] = peak;
    __syncthreads();
    if (threadIdx.x == 0) {
        for (unsigned warp = 1; warp < threads / warp_size; ++warp) peak = fmaxf(peak, warp_peak[warp]);
        samples[rows].x = isfinite(peak) ? peak : CUDART_NAN_F;
    }
}

extern "C" __global__ void resident_fine_stats(const float2* modes, const float2* mapped,
    const double2* observed, double2* stats, std::uint32_t rows, std::uint32_t mode_count,
    std::uint32_t lanes_per_block, std::uint32_t blocks) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto statistic = index % candidate_statistics;
    const auto first_lane = index / candidate_statistics * candidate_lanes;
    const auto block = first_lane / lanes_per_block;
    if (block >= blocks) return;
    if (statistic == candidate_statistics - 1) {
        // The receiver also needs to distinguish an empty template from a
        // valid template whose projection happened to be zero.
        double nonzero = 0;
        for (unsigned lane = 0; lane < candidate_lanes; ++lane) {
            const auto trailer = mapped[static_cast<std::uint64_t>(first_lane + lane) * (rows + 1) + rows];
            if (!isfinite(trailer.x)) nonzero = CUDART_NAN;
            else if (nonzero == 0 && (trailer.x > 0 || trailer.y != 0)) nonzero = 1;
        }
        stats[index] = make_double2(nonzero, 0);
        return;
    }

    // Each bit contributes two correlations and three independent Gram
    // entries. Preserve the native conjugation and ascending mode order.
    const auto field = statistic % bit_statistics;
    const auto bit_lane = first_lane + 2 * (statistic / bit_statistics);
    const auto* a = modes + static_cast<std::uint64_t>(bit_lane + (field == 1 || field == 4)) * mode_count;
    const auto* gram = modes + static_cast<std::uint64_t>(bit_lane + (field >= 3)) * mode_count;
    const auto* observation = observed + static_cast<std::uint64_t>(block) * mode_count;
    double real = 0, imag = 0;
    for (unsigned mode = 0; mode < mode_count; ++mode) {
        const auto av = a[mode];
        const auto bv = field < 2 ? observation[mode] : make_double2(gram[mode].x, gram[mode].y);
        real = __dadd_rn(real, __dadd_rn(__dmul_rn(av.x, bv.x), __dmul_rn(av.y, bv.y)));
        imag = __dadd_rn(imag, __dsub_rn(__dmul_rn(av.x, bv.y), __dmul_rn(av.y, bv.x)));
    }
    stats[index] = make_double2(real, imag);
}

extern "C" __global__ void nvfp4_pack_basis(const std::int64_t* bins, std::uint32_t* packed, std::uint8_t* scales,
    std::uint32_t rows, std::uint32_t modes, std::uint32_t k, std::uint32_t basis_rows) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto basis_row = index / k;
    if (basis_row >= basis_rows) return;
    const auto inner = index % k;
    const auto sample = inner % (k / 2);
    float value = 0;
    if (basis_row / 2 < modes && sample < rows) {
        // Reduce the exact integer product before converting to radians.
        // The bounded angle fits the native sine/cosine range, while FP4
        // coefficient quantization dominates the intrinsic's FP32 error.
        const auto turn = (static_cast<std::uint64_t>(bins[basis_row / 2]) * sample) % rows;
        constexpr float tau = 6.2831853071795864769f;
        float sine, cosine;
        __sincosf(-tau * (static_cast<float>(turn) / static_cast<float>(rows)), &sine, &cosine);
        // Stack [real input, imag input] and pair the real/imag DFT rows.
        const bool imaginary_input = inner >= k / 2;
        if (basis_row % 2) value = imaginary_input ? cosine : sine;
        else value = imaginary_input ? -sine : cosine;
    }
    pack_block(value, packed, scales, index);
}

extern "C" __global__ void nvfp4_pack_input(const float2* mapped, std::uint32_t* packed, std::uint8_t* scales,
    std::uint32_t rows, std::uint32_t lanes, std::uint32_t k, std::uint32_t lane_capacity) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto row = index / k;
    if (row >= lane_capacity) return;
    const auto inner = index % k;
    const auto sample = inner % (k / 2);
    float value = 0;
    if (row < lanes && sample < rows) {
        // The mapper measured the exact peak while converting this lane to
        // C64. Its trailer traveled with the samples in the same upload.
        const auto* samples = mapped + row * (rows + 1);
        const auto peak = samples[rows].x;
        if (peak > 0) {
            const auto input = samples[sample];
            value = (inner < k / 2 ? input.x : input.y) / peak;
        }
    }
    pack_block(value, packed, scales, index);
}

extern "C" __global__ void nvfp4_project(const std::uint32_t* packed_input, const std::uint8_t* input_scales,
    const std::uint32_t* packed_basis, const std::uint8_t* basis_scales, const float2* mapped, float2* output,
    std::uint32_t rows, std::uint32_t modes, std::uint32_t lanes, std::uint32_t k) {
    // One block of four warps owns a 16x8 real result tile. Each warp takes
    // independent K=64 slices, then reduces the four FP32 partial tiles.
    __shared__ float4 partial[threads];
    const auto warp = threadIdx.x / warp_size;
    const auto lane = threadIdx.x % warp_size;
    const auto quad = lane / 4;
    const auto slot = lane % 4;
    const auto mode_tiles = (modes + tile_columns / 2 - 1) / (tile_columns / 2);
    const auto basis_first = blockIdx.x % mode_tiles * tile_columns;
    const auto mode = basis_first / 2 + slot;
    const auto row_a0 = blockIdx.x / mode_tiles * tile_rows + quad;
    const auto row_a1 = row_a0 + tile_rows / 2;
    const auto basis_row = basis_first + quad;
    const auto* input_a0 = packed_input + row_a0 * (k / 8);
    const auto* input_a1 = packed_input + row_a1 * (k / 8);
    const auto* input_b = packed_basis + basis_row * (k / 8);
    // With thread-id-a=0, lanes 0 and 1 of each quad provide scales for
    // its two A rows. With thread-id-b=0, lane 0 provides the B scales.
    const auto* scale_a = scale_row(input_scales, slot % 2 ? row_a1 : row_a0, k);
    const auto* scale_b = scale_row(basis_scales, basis_row, k);

    float4 accumulator{};
    for (auto tile = warp; tile < k / tile_inner; tile += threads / warp_size) {
        // PTX m16n8k64 maps each quad to rows quad and quad+8 in A,
        // and column quad in B. The two inner fragments start 32 apart.
        const auto packed_at = tile * (tile_inner / 8) + slot;
        constexpr unsigned second_half = tile_inner / 16;
        multiply_tile(accumulator, input_a0[packed_at], input_a1[packed_at],
            input_a0[packed_at + second_half], input_a1[packed_at + second_half],
            input_b[packed_at], input_b[packed_at + second_half], scale_a[tile], scale_b[tile]);
    }
    partial[threadIdx.x] = accumulator;
    __syncthreads();
    if (warp || mode >= modes) return;
    for (unsigned part = 1; part < threads / warp_size; ++part) {
        const auto other = partial[part * warp_size + lane];
        accumulator.x += other.x;
        accumulator.y += other.y;
        accumulator.z += other.z;
        accumulator.w += other.w;
    }
    const auto scale = rsqrtf(static_cast<float>(rows)) / (normalized_max * normalized_max);
    if (row_a0 < lanes) {
        const auto peak = mapped[row_a0 * (rows + 1) + rows].x;
        output[row_a0 * modes + mode] = make_float2(peak * (accumulator.x * scale), peak * (accumulator.y * scale));
    }
    if (row_a1 < lanes) {
        const auto peak = mapped[row_a1 * (rows + 1) + rows].x;
        output[row_a1 * modes + mode] = make_float2(peak * (accumulator.z * scale), peak * (accumulator.w * scale));
    }
}

// Dense coarse frequency search. One real contraction replaces the forward
// and inverse transforms of the frequency-tile route for one tile:
//
//   scores[frequency][workspace][arrival] from
//   A[(frequency, arrival)][k] = rotated observation, sliding by arrival
//   B[(workspace, ray, part)][k] = template column, as [Re, Im] or [-Im, Re]
//
// Both operands interleave the real and imaginary parts of one sample, so an
// arrival is one byte of packed E2M1 input. Block scales cover eight samples.
// Each frequency therefore keeps eight packed observations whose scale blocks
// start at samples 0..7, and an arrival reads the one matching it modulo 8.
namespace {

constexpr double tau64 = 6.283185307179586476925286766559;
constexpr double carrier_gain = .57735026918962576451;
constexpr unsigned coarse_variants = 8;
constexpr unsigned coarse_groups = 8;
constexpr unsigned coarse_warp_arrivals = tile_rows * coarse_groups;
constexpr unsigned coarse_block_arrivals = coarse_warp_arrivals * (threads / warp_size);
// A thread's fragments are alternate words of its observation: group g of a
// K=64 slice takes four of them from the 2g-th on, and the next slice starts
// four later. Each thread slot therefore has its own stream of those words,
// and a ring of five quads holds one slice while a single load brings the
// quad the next slice adds. Block scales advance the same way in pairs of
// ready scale vectors, and a bank record holds a slice's two operand words
// and its scale vector. The inner extent is a whole number of ring turns.
constexpr unsigned coarse_period = 5;
constexpr unsigned coarse_ring = 4 * coarse_period;
constexpr unsigned coarse_scale_ring = 2 * coarse_period;
// Streams end with the quads that the last slices load ahead and never use.
constexpr unsigned coarse_stream_slack = 4;
static_assert(coarse_ring >= 2 * coarse_groups + 2 && coarse_scale_ring >= coarse_groups);
constexpr unsigned pulse_resolution = 256;
constexpr unsigned pulse_samples = 8 * pulse_resolution;
// The chips of one sample's pulse support: 16 chip periods hold at most 17.
constexpr unsigned render_support = 17;
constexpr auto no_peak = ~std::uint64_t{0};
// A block filters one tile of 128 output rows of a column. Its static stage
// (47 KiB) holds a window of taps, two to a pair, and then the raw span
// that the tile's rows read through those taps.
constexpr unsigned fir_stage_pairs = 6016;
constexpr unsigned fir_chunk = 64;

constexpr unsigned coarse_rescore_interior_candidates = 8;
constexpr unsigned coarse_compact_interior_candidates = 4;
constexpr unsigned coarse_rescore_legacy_candidates = 2;
constexpr unsigned coarse_rescore_endpoints = 2;
constexpr unsigned coarse_rescore_candidates = coarse_rescore_interior_candidates +
    coarse_rescore_legacy_candidates + coarse_rescore_endpoints;
constexpr unsigned coarse_rescore_radius = 2;
constexpr unsigned coarse_rescore_scores = (2 * coarse_rescore_radius + 1) * coarse_rescore_candidates;
constexpr unsigned coarse_compact_rescore_scores = coarse_compact_interior_candidates +
    coarse_rescore_legacy_candidates + coarse_rescore_endpoints;
struct CoarseCandidateRow {
    std::uint64_t lags[coarse_rescore_scores];
    double scores[coarse_rescore_scores];
    std::uint64_t centers;
    std::uint64_t count;
};
static_assert(sizeof(CoarseCandidateRow) == 976 && alignof(CoarseCandidateRow) == 8);

struct CoarsePublishedRow {
    double scores[2];
    std::uint64_t lags[2];
    std::uint64_t count;
    std::uint64_t rescored;
};
static_assert(sizeof(CoarsePublishedRow) == 48 && alignof(CoarsePublishedRow) == 8);

__device__ float quadratic_score(const double* inverse, float a_real, float a_imag, float b_real, float b_imag) {
    // The host folds both operand scales and the noise variance into the
    // four inverse-Gram coefficients of this workspace.
    const double ar = a_real, ai = a_imag, br = b_real, bi = b_imag;
    const auto real_product = ar * inverse[2] + ai * inverse[3];
    const auto imag_product = ar * inverse[3] - ai * inverse[2];
    return static_cast<float>(inverse[0] * (ar * ar + ai * ai) + inverse[1] * (br * br + bi * bi) +
                              2 * (real_product * br - imag_product * bi));
}

// A one-ray workspace's score: its scalar inverse Gram, with the operand
// scales and the noise variance folded in, times the correlation's energy.
__device__ float ray_score(const double* inverse, float real, float imag) {
    const double r = real, i = imag;
    return static_cast<float>(inverse[0] * (r * r + i * i));
}

__device__ bool better_peak(const float* scores, std::uint64_t a, std::uint64_t b) {
    if (a == no_peak) return false;
    if (b == no_peak) return true;
    return scores[a] == scores[b] ? a > b : scores[a] > scores[b];
}

__device__ bool separated(std::uint64_t a, std::uint64_t b, std::uint32_t distance) {
    return (a > b ? a - b : b - a) >= distance;
}

__device__ std::uint64_t interior_peak(const float* scores, std::uint32_t start, std::uint32_t arrivals) {
    if (!start || start + 1 >= arrivals || !(scores[start - 1] < scores[start])) return no_peak;
    auto end = start + 1;
    while (end + 1 < arrivals && scores[end] == scores[start]) ++end;
    if (!(scores[end] < scores[start])) return no_peak;
    return start + (end - start - 1) / 2;
}

__device__ std::uint64_t reduce_peak(const float* scores, std::uint64_t local, std::uint64_t* candidates) {
    candidates[threadIdx.x] = local;
    __syncthreads();
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) {
            const auto other = candidates[threadIdx.x + width];
            if (better_peak(scores, other, candidates[threadIdx.x])) candidates[threadIdx.x] = other;
        }
        __syncthreads();
    }
    const auto result = candidates[0];
    __syncthreads();
    return result;
}

__device__ double smooth_step(double x) {
    x = fmin(fmax(x, 0.), 1.);
    return x * x * x * (10 + x * (-15 + 6 * x));
}

// The public envelope of triad_shaping.cpp. Parameters follow the host
// layout in OpenClNvfp4Coarse: see source_parameter there.
__device__ double source_envelope(const double* source, double t) {
    const auto bit = source[8], edge = source[9];
    if (t < 0 || t >= bit) return 0;
    auto result = smooth_step(t / edge) * smooth_step((bit - t) / edge);
    if (source[10] != 0) {
        const auto tile_seconds = source[11];
        const auto id = floor(t / tile_seconds);
        const auto period = source[10];
        const auto ordinal = floor(id / period);
        if (id - ordinal * period != 0 || ordinal >= source[12]) return 0;
        const auto within = t - id * tile_seconds;
        result *= smooth_step(within / edge) * smooth_step((tile_seconds - within) / edge);
    }
    if (source[13] != 0) {
        const auto dwell = source[13];
        const auto within = t - floor(t / dwell) * dwell;
        result *= smooth_step((fmin(within, dwell - within) - source[14]) / source[15]);
    }
    return result;
}

__device__ float pulse_at(const float* pulse, double chip_offset) {
    const auto position = fabs(chip_offset) * pulse_resolution;
    if (!(position <= pulse_samples)) return 0;
    const auto index = static_cast<std::uint32_t>(position);
    if (index == pulse_samples) return pulse[pulse_samples];
    const auto fraction = static_cast<float>(position - index);
    return pulse[index] + fraction * (pulse[index + 1] - pulse[index]);
}

} // namespace

// Renders the keyed source of whole lanes on the integer receive lattice,
// as PatternSource::sample_at does for the native exact route. One thread
// renders one sample of every lane of the group. Its time, envelope, chip
// support, pulse weights and carrier rotation depend on the sample alone, so
// they are evaluated once; each lane keeps the FP32 operations and order of
// a lane rendered by itself, and the raster is unchanged.
extern "C" __global__ void coarse_render_lanes(const float2* chips, const float* pulse, const double* source,
    float2* raster, std::uint32_t source_rows, std::uint32_t lane_count, std::uint32_t cells,
    std::uint32_t first_lane) {
    const auto row = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (row >= source_rows) return;
    const auto position = static_cast<double>(row) * source[0];
    bool supported = false;
    std::uint32_t first = 0, support = 0;
    float weights[render_support];
    float sine = 0, cosine = 0;
    if (position < source[2]) {
        const auto t = (position - source[1]) / source[3];
        const auto envelope = source_envelope(source, t);
        const auto chip_time = t * source[4];
        const auto offset = source[5];
        const auto first_real = fmax(0., ceil(chip_time - (8 + offset)));
        const auto last_real = fmin(source[6] - 1, floor(chip_time + (8 - offset)));
        if (envelope != 0 && last_real >= first_real) {
            supported = true;
            first = static_cast<std::uint32_t>(first_real);
            // A support of at most 16 chip periods holds at most 17 chips.
            support = static_cast<std::uint32_t>(last_real) - first + 1;
            if (support > render_support) support = render_support;
            const auto gain = static_cast<float>(carrier_gain * envelope);
            // Whole unrolled loops keep the weights in registers.
#pragma unroll
            for (std::uint32_t chip = 0; chip < render_support; ++chip)
                if (chip < support) weights[chip] = gain * pulse_at(pulse, chip_time - ((first + chip) + offset));
            // The outer carriers rotate by -/+ one carrier spacing.
            const auto cycles = source[7] * t;
            __sincosf(static_cast<float>((cycles - floor(cycles)) * tau64), &sine, &cosine);
        }
    }
    for (std::uint32_t lane_index = 0; lane_index < lane_count; ++lane_index) {
        auto value = make_float2(0.f, 0.f);
        if (supported) {
            const auto* lane = chips + (static_cast<std::uint64_t>(first_lane) + lane_index) * cells * 3;
            float2 carriers[3] = {make_float2(0.f, 0.f), make_float2(0.f, 0.f), make_float2(0.f, 0.f)};
            // A constant trip count with predicated chips, rather than an
            // early exit, lets the compiler unroll it and index the weights
            // as registers.
#pragma unroll
            for (std::uint32_t chip_index = 0; chip_index < render_support; ++chip_index) {
                if (chip_index < support) {
                    const auto weight = weights[chip_index];
                    const auto* chip = lane + (static_cast<std::uint64_t>(first) + chip_index) * 3;
                    for (unsigned carrier = 0; carrier < 3; ++carrier) {
                        carriers[carrier].x += chip[carrier].x * weight;
                        carriers[carrier].y += chip[carrier].y * weight;
                    }
                }
            }
            value.x = carriers[1].x + (carriers[0].x + carriers[2].x) * cosine + (carriers[0].y - carriers[2].y) * sine;
            value.y = carriers[1].y + (carriers[0].y + carriers[2].y) * cosine + (carriers[2].x - carriers[0].x) * sine;
        }
        raster[static_cast<std::uint64_t>(lane_index) * source_rows + row] = value;
    }
}

namespace {

// The native tapered pulse: its table, and std::lerp between two entries.
__device__ double pulse_value(const double* pulse, double chip_offset) {
    const auto position = fabs(chip_offset) * pulse_resolution;
    if (!(position <= pulse_samples)) return 0;
    const auto index = static_cast<std::uint32_t>(position);
    if (index == pulse_samples) return pulse[pulse_samples];
    const auto a = pulse[index], b = pulse[index + 1];
    const auto t = position - index;
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) return __dadd_rn(__dmul_rn(t, b), __dmul_rn(1 - t, a));
    const auto x = __dadd_rn(a, __dmul_rn(t, __dsub_rn(b, a)));
    return b > a ? (b > x ? x : b) : (b < x ? x : b);
}

} // namespace

// Renders dense source tiles of the fine basis into their cache slots, in
// FP64 and in the operation order of PatternSource::render_bank. Dense
// sample d of a lane is the keyed waveform at source sample d * source[0],
// scaled by source[16], and zero from dense sample source[17] on. The other
// parameters follow coarse_render_lanes. Chips are [lane][cell][carrier].
extern "C" __global__ void resident_fine_render_tiles(const double2* chips, const double* pulse,
    const double* source, const TileRequest* requests, double2* tiles, std::uint32_t cells,
    std::uint32_t request_count) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto request_index = index / source_tile_samples;
    if (request_index >= request_count) return;
    const auto& request = requests[request_index];
    const auto within = index % source_tile_samples;
    const auto dense = static_cast<double>(request.first + within);
    auto value = make_double2(0., 0.);
    // Separate operations keep the chip support of every sample identical
    // to the native renderer's. A fused product could move its edges.
    const auto position = __dmul_rn(dense, source[0]);
    if (dense < source[17] && position < source[2]) {
        const auto t = __ddiv_rn(__dsub_rn(position, source[1]), source[3]);
        const auto envelope = source_envelope(source, t);
        const auto chip_time = __dmul_rn(t, source[4]);
        const auto offset = source[5];
        const auto first_real = fmax(0., ceil(__dsub_rn(chip_time, 8 + offset)));
        const auto last_real = fmin(source[6] - 1, floor(__dadd_rn(chip_time, 8 - offset)));
        if (envelope != 0 && last_real >= first_real) {
            const auto first = static_cast<std::uint32_t>(first_real), last = static_cast<std::uint32_t>(last_real);
            const auto* lane = chips + static_cast<std::uint64_t>(request.lane) * cells * 3;
            double2 carriers[3] = {make_double2(0., 0.), make_double2(0., 0.), make_double2(0., 0.)};
            for (auto fine = first; fine <= last; ++fine) {
                const auto shape = pulse_value(pulse, __dsub_rn(chip_time, fine + offset));
                const auto weight = __dmul_rn(__dmul_rn(carrier_gain, shape), envelope);
                const auto* chip = lane + static_cast<std::uint64_t>(fine) * 3;
                for (unsigned carrier = 0; carrier < 3; ++carrier) {
                    carriers[carrier].x = __dadd_rn(carriers[carrier].x, __dmul_rn(chip[carrier].x, weight));
                    carriers[carrier].y = __dadd_rn(carriers[carrier].y, __dmul_rn(chip[carrier].y, weight));
                }
            }
            // The outer carriers rotate by -/+ one carrier spacing and the
            // middle one by exactly one.
            double sine, cosine;
            sincos(tau64 * remainder(__dmul_rn(source[7], t), 1.), &sine, &cosine);
            const double rotation[3][2] = {{cosine, -sine}, {1., 0.}, {cosine, sine}};
            for (unsigned carrier = 0; carrier < 3; ++carrier) {
                const auto real = rotation[carrier][0], imag = rotation[carrier][1];
                value.x = __dadd_rn(value.x, __dsub_rn(__dmul_rn(carriers[carrier].x, real),
                                                       __dmul_rn(carriers[carrier].y, imag)));
                value.y = __dadd_rn(value.y, __dadd_rn(__dmul_rn(carriers[carrier].x, imag),
                                                       __dmul_rn(carriers[carrier].y, real)));
            }
            value = make_double2(__dmul_rn(value.x, source[16]), __dmul_rn(value.y, source[16]));
        }
    }
    tiles[static_cast<std::uint64_t>(request.slot) * source_tile_samples + within] = value;
}

namespace {

// The tapered pulse in FP32 at a chip offset inside its support, which
// keeps the table index below its last entry. The table holds the FP64
// pulse rounded to FP32.
__device__ float pulse_inside(const float* pulse, float chip_offset) {
    const auto position = fabsf(chip_offset) * pulse_resolution;
    const auto index = static_cast<std::uint32_t>(position);
    const auto a = pulse[index], b = pulse[index + 1];
    return __fmaf_rn(position - static_cast<float>(index), b - a, a);
}

// The same at a support edge, where the pulse table ends. The FP64 offset
// decides the support exactly as pulse_value does.
__device__ float pulse_edge(const float* pulse, double chip_offset) {
    const auto position = fabs(chip_offset) * pulse_resolution;
    if (!(position <= pulse_samples)) return 0;
    const auto index = static_cast<std::uint32_t>(position);
    if (index == pulse_samples) return pulse[pulse_samples];
    const auto a = pulse[index], b = pulse[index + 1];
    return __fmaf_rn(static_cast<float>(position - index), b - a, a);
}

} // namespace

// The FP32 renderer of the same tiles. Each sample's time, envelope, chip
// support and edge offsets keep the FP64 operations of
// resident_fine_render_tiles, so the support and gating are the same. The
// chip sums, pulse interpolation, sine and cosine of the reduced carrier
// phase and the rotation are FP32. The keyed chips and the pulse table are
// the FP64 ones rounded to FP32 on the host, as the FP32 arithmetic would
// round them on use. Interior chip offsets step from the first chip's
// offset by whole FP32 chips, exactly except after a first chip clamped to
// the source start, where an offset rounds by at most 2^-22 chip. Values
// are stored as FP64 tiles.
extern "C" __global__ void resident_fine_render_tiles_fp32(const float2* chips, const float* pulse,
    const double* source, const TileRequest* requests, double2* tiles, std::uint32_t cells,
    std::uint32_t request_count) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto request_index = index / source_tile_samples;
    if (request_index >= request_count) return;
    const auto& request = requests[request_index];
    const auto within = index % source_tile_samples;
    const auto dense = static_cast<double>(request.first + within);
    auto value = make_double2(0., 0.);
    const auto position = __dmul_rn(dense, source[0]);
    if (dense < source[17] && position < source[2]) {
        const auto t = __ddiv_rn(__dsub_rn(position, source[1]), source[3]);
        const auto envelope = source_envelope(source, t);
        const auto chip_time = __dmul_rn(t, source[4]);
        const auto offset = source[5];
        const auto first_real = fmax(0., ceil(__dsub_rn(chip_time, 8 + offset)));
        const auto last_real = fmin(source[6] - 1, floor(__dadd_rn(chip_time, 8 - offset)));
        if (envelope != 0 && last_real >= first_real) {
            const auto first = static_cast<std::uint32_t>(first_real), last = static_cast<std::uint32_t>(last_real);
            const auto* lane = chips + static_cast<std::uint64_t>(request.lane) * cells * 3;
            const auto gain = static_cast<float>(__dmul_rn(carrier_gain, envelope));
            const auto lead = __dsub_rn(chip_time, first + offset);
            const auto lead_fp32 = static_cast<float>(lead);
            float2 carriers[3] = {make_float2(0.f, 0.f), make_float2(0.f, 0.f), make_float2(0.f, 0.f)};
            for (auto fine = first; fine <= last; ++fine) {
                const auto shape = fine == first ? pulse_edge(pulse, lead)
                    : fine == last ? pulse_edge(pulse, __dsub_rn(chip_time, fine + offset))
                    : pulse_inside(pulse, lead_fp32 - static_cast<float>(fine - first));
                const auto weight = __fmul_rn(gain, shape);
                const auto* chip = lane + static_cast<std::uint64_t>(fine) * 3;
                for (unsigned carrier = 0; carrier < 3; ++carrier) {
                    carriers[carrier].x = __fmaf_rn(chip[carrier].x, weight, carriers[carrier].x);
                    carriers[carrier].y = __fmaf_rn(chip[carrier].y, weight, carriers[carrier].y);
                }
            }
            // The outer carriers rotate by -/+ one carrier spacing. The
            // phase is reduced to half a turn exactly in FP64, as
            // remainder(cycles, 1) without its general algorithm, then FP32.
            const auto cycles = __dmul_rn(source[7], t);
            float sine, cosine;
            sincospif(static_cast<float>(2 * __dsub_rn(cycles, rint(cycles))), &sine, &cosine);
            const auto scale = static_cast<float>(source[16]);
            const auto real = __fmaf_rn(__fsub_rn(carriers[0].y, carriers[2].y), sine,
                __fmaf_rn(__fadd_rn(carriers[0].x, carriers[2].x), cosine, carriers[1].x));
            const auto imag = __fmaf_rn(__fsub_rn(carriers[2].x, carriers[0].x), sine,
                __fmaf_rn(__fadd_rn(carriers[0].y, carriers[2].y), cosine, carriers[1].y));
            value = make_double2(__fmul_rn(real, scale), __fmul_rn(imag, scale));
        }
    }
    tiles[static_cast<std::uint64_t>(request.slot) * source_tile_samples + within] = value;
}

namespace {

// The FP64 position and echo terms of raw template row `row` under one
// column map: the stencil center of the native four-point interpolation,
// its fraction and the echo angle. A column descriptor holds first position,
// clock scale, first echo cycles, echo cycle step and lane, and the terms
// depend on the first four only. A row outside the lane has no center.
struct WarpRow {
    std::int32_t center;
    float u, angle;
};
constexpr std::int32_t outside_lane = -0x7fffffff - 1;
// A thread's rows of the widest stage span, and the columns of one set
// that share a map. Sets end early at no_column.
constexpr unsigned warp_stash_rows = (fir_stage_pairs + threads - 1) / threads;
constexpr unsigned coarse_set_columns = 4;
constexpr std::uint32_t no_column = 0xffffffffu;

__device__ WarpRow warp_row(const double* map, double row, std::uint32_t source_rows) {
    const auto position = map[0] + row * map[1];
    WarpRow result{outside_lane, 0.f, 0.f};
    if (position > -2 && position < static_cast<double>(source_rows) + 1) {
        const auto integral = floor(position);
        result.center = static_cast<std::int32_t>(integral);
        result.u = static_cast<float>(position - integral);
        if (map[2] != 0 || map[3] != 0) {
            const auto echo = map[2] + row * map[3];
            result.angle = static_cast<float>((echo - floor(echo)) * tau64);
        }
    }
    return result;
}

// The warped sample of one lane at those terms: the interpolation of its
// resident rendering and, for a map with an echo, the echo rotation. Every
// warped sample, stored or staged, comes from these two functions.
__device__ float2 warp_apply(const float2* lane, const WarpRow& warp, bool echo, std::uint32_t source_rows) {
    auto value = make_float2(0.f, 0.f);
    if (warp.center == outside_lane) return value;
    const auto u = warp.u;
    const float weights[4] = {-u * (u - 1) * (u - 2) / 6, (u + 1) * (u - 1) * (u - 2) / 2,
                              -(u + 1) * u * (u - 2) / 2, (u + 1) * u * (u - 1) / 6};
    const auto center = static_cast<std::int64_t>(warp.center);
    for (int i = 0; i < 4; ++i) {
        const auto sample = center + i - 1;
        if (sample < 0 || sample >= static_cast<std::int64_t>(source_rows)) continue;
        value.x += weights[i] * lane[sample].x;
        value.y += weights[i] * lane[sample].y;
    }
    if (echo) {
        float sine, cosine;
        __sincosf(warp.angle, &sine, &cosine);
        value = make_float2(value.x * cosine - value.y * sine, value.y * cosine + value.x * sine);
    }
    return value;
}

__device__ bool has_echo(const double* map) {
    return map[2] != 0 || map[3] != 0;
}

// Stages raw rows [low, low + count) of a stored column: thread i of the
// block the rows i, i + threads, ...
struct RawStage {
    const float2* samples;
    __device__ void operator()(float2* span, std::uint64_t low, std::uint32_t count) const {
        for (auto i = threadIdx.x; i < count; i += threads) span[i] = samples[low + i];
    }
};

// Stages the same rows of a lane warped at the FP64 terms of a set's map.
// The first column of the set evaluates them into the thread's stash, and
// the later ones reuse it. A stage of one window spans at most
// fir_stage_pairs rows, so the stash covers the thread's rows. The loop has
// a constant trip count and constant stash indices, and it and filter_tile
// are inlined, so the compiler can unroll it and keep the stash in
// registers rather than in local memory.
struct StashStage {
    WarpRow* stash;
    const double* map;
    const float2* lane;
    bool echo, evaluate;
    std::uint32_t source_rows;
    __device__ __forceinline__ void operator()(float2* span, std::uint64_t low, std::uint32_t count) const {
#pragma unroll
        for (unsigned k = 0; k < warp_stash_rows; ++k) {
            const auto i = threadIdx.x + k * threads;
            if (i < count) {
                if (evaluate) stash[k] = warp_row(map, static_cast<double>(low + i), source_rows);
                span[i] = warp_apply(lane, stash[k], echo, source_rows);
            }
        }
    }
};

// Taps per stage window: half a pair each, beside a raw span of the tile's
// 127 row strides and the window. Zero when one tap would not fit.
__device__ std::uint32_t fir_window(std::uint32_t decimation, std::uint32_t tap_count) {
    const auto spread = static_cast<std::uint64_t>(threads - 1) * decimation;
    if (spread >= fir_stage_pairs) return 0;
    const auto window = static_cast<std::uint32_t>(2 * (fir_stage_pairs - spread) / 3);
    return window < tap_count ? window : tap_count;
}

// The canonical decimating FIR of one tile of a column: output row n is the
// full convolution at n * decimation, as FP32 partial sums of 64 taps from
// the row's first tap, each added in FP64. The block stages the windows of
// its taps in order, and every thread carries its row's sums across them,
// so each row performs the same operations in the same order as it would
// reading its raw rows in place. `stage_span(span, low, count)` stages the
// raw rows of one window. The window must not be zero.
template<class Stage>
__device__ __forceinline__ void filter_tile(const Stage& stage_span, const float* taps, float2* output, float2* stage,
    std::uint32_t raw_rows, std::uint32_t tap_count, std::uint32_t decimation, std::uint32_t rows,
    std::uint32_t tile, std::uint32_t window) {
    const auto first_row = static_cast<std::uint64_t>(tile) * threads;
    const auto last_row = (first_row + threads < rows ? first_row + threads : rows) - 1;
    const auto row = first_row + threadIdx.x;
    const bool active = row < rows;
    const auto first_newest = first_row * decimation, last_newest = last_row * decimation;
    // The first tap of the tile's first row, and the tap stop of its last.
    const auto tile_tap = static_cast<std::uint32_t>(first_newest >= raw_rows ? first_newest - (raw_rows - 1) : 0);
    const auto tile_stop = static_cast<std::uint32_t>(last_newest + 1 < tap_count ? last_newest + 1 : tap_count);
    std::uint64_t newest = 0;
    std::uint32_t tap = 0, tap_stop = 0, chunk_stop = 0;
    if (active) {
        newest = row * decimation;
        tap = static_cast<std::uint32_t>(newest >= raw_rows ? newest - (raw_rows - 1) : 0);
        tap_stop = static_cast<std::uint32_t>(newest + 1 < tap_count ? newest + 1 : tap_count);
        chunk_stop = tap + fir_chunk < tap_stop ? tap + fir_chunk : tap_stop;
    }
    auto* weights = reinterpret_cast<float*>(stage);
    auto* span = stage + (window + 1) / 2;
    double real = 0, imag = 0;
    float partial_real = 0, partial_imag = 0;
    for (auto first = tile_tap; first < tile_stop; first += window) {
        const auto stop = tile_stop - first > window ? first + window : tile_stop;
        // Raw rows newest - tap of the tile's rows for taps in [first, stop).
        const auto low = first_newest + 1 > stop ? first_newest + 1 - stop : 0;
        const auto high = last_newest - first < raw_rows - 1 ? last_newest - first : raw_rows - 1;
        const auto count = static_cast<std::uint32_t>(high + 1 - low);
        for (auto i = threadIdx.x; i < stop - first; i += threads) weights[i] = taps[first + i];
        stage_span(span, low, count);
        __syncthreads();
        const auto end = tap_stop < stop ? tap_stop : stop;
        const auto base = static_cast<std::uint32_t>(newest - low);
        while (tap < end) {
            const auto chunk_end = chunk_stop < end ? chunk_stop : end;
            for (; tap < chunk_end; ++tap) {
                const auto weight = weights[tap - first];
                const auto value = span[base - tap];
                partial_real += weight * value.x;
                partial_imag += weight * value.y;
            }
            if (tap == chunk_stop) {
                real += partial_real;
                imag += partial_imag;
                partial_real = partial_imag = 0;
                chunk_stop = chunk_stop + fir_chunk < tap_stop ? chunk_stop + fir_chunk : tap_stop;
            }
        }
        __syncthreads();
    }
    if (active) output[row] = make_float2(static_cast<float>(real), static_cast<float>(imag));
}

__device__ void nonfinite_tile(float2* output, std::uint32_t rows, std::uint32_t tile) {
    const auto row = static_cast<std::uint64_t>(tile) * threads + threadIdx.x;
    if (row < rows) output[row] = make_float2(CUDART_NAN_F, CUDART_NAN_F);
}

} // namespace

// Stores raw template columns for coarse_fir_tiles or coarse_fir_decimate,
// for geometries whose taps need more than one stage window of a tile. A
// set holds up to four columns of one map, which differ only in their lanes,
// and a thread evaluates its row's FP64 terms once for all of them. Raw
// columns are numbered from the batch's first column.
extern "C" __global__ void coarse_warp_columns(const float2* raster, const double* columns, const std::uint32_t* sets,
    float2* raw, std::uint32_t source_rows, std::uint32_t raw_rows, std::uint32_t set_count, std::uint32_t first_set,
    std::uint32_t first_column, std::uint32_t first_lane) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto set = index / raw_rows;
    if (set >= set_count) return;
    const auto row = index % raw_rows;
    const auto* members = sets + (static_cast<std::uint64_t>(first_set) + set) * coarse_set_columns;
    const auto* map = columns + static_cast<std::uint64_t>(members[0]) * 5;
    const auto warp = warp_row(map, static_cast<double>(row), source_rows);
    const bool echo = has_echo(map);
    for (unsigned member = 0; member < coarse_set_columns && members[member] != no_column; ++member) {
        const auto column = members[member];
        const auto* lane = raster + (static_cast<std::uint64_t>(columns[static_cast<std::uint64_t>(column) * 5 + 4]) -
                                     first_lane) * source_rows;
        raw[static_cast<std::uint64_t>(column - first_column) * raw_rows + row] =
            warp_apply(lane, warp, echo, source_rows);
    }
}

// Filters stored raw columns into the resident bank, one tile of output rows
// per block, in stage windows of taps.
extern "C" __global__ void coarse_fir_tiles(const float2* raw, const float* taps, float2* bank,
    std::uint32_t raw_rows, std::uint32_t tap_count, std::uint32_t decimation, std::uint32_t rows,
    std::uint32_t column_count, std::uint32_t first_column) {
    __shared__ float2 stage[fir_stage_pairs];
    const auto tiles = (rows + threads - 1) / threads;
    const auto column = blockIdx.x / tiles, tile = blockIdx.x % tiles;
    if (column >= column_count) return;
    const RawStage input{raw + static_cast<std::uint64_t>(column) * raw_rows};
    auto* output = bank + (static_cast<std::uint64_t>(first_column) + column) * rows;
    const auto window = fir_window(decimation, tap_count);
    if (window) return filter_tile(input, taps, output, stage, raw_rows, tap_count, decimation, rows, tile, window);
    // No stage fits: a nonfinite bank, which the host rejects.
    nonfinite_tile(output, rows, tile);
}

// The canonical decimating FIR of stored raw columns, reading them in place:
// output row n is the full convolution at n * decimation. Bounded FP32
// partial sums are added in FP64. It serves row strides too wide for a stage
// window and keeps the full occupancy of a kernel without shared memory.
extern "C" __global__ void coarse_fir_decimate(const float2* raw, const float* taps, float2* bank,
    std::uint32_t raw_rows, std::uint32_t tap_count, std::uint32_t decimation, std::uint32_t rows,
    std::uint32_t column_count, std::uint32_t first_column) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto column = index / rows;
    if (column >= column_count) return;
    const auto row = index % rows;
    const auto newest = row * decimation;
    const auto* input = raw + column * raw_rows;
    auto tap = newest >= raw_rows ? newest - (raw_rows - 1) : 0;
    const auto tap_stop = newest + 1 < tap_count ? newest + 1 : tap_count;
    double real = 0, imag = 0;
    while (tap < tap_stop) {
        const auto stop = tap + 64 < tap_stop ? tap + 64 : tap_stop;
        float partial_real = 0, partial_imag = 0;
        for (; tap < stop; ++tap) {
            const auto value = input[newest - tap];
            partial_real += taps[tap] * value.x;
            partial_imag += taps[tap] * value.y;
        }
        real += partial_real;
        imag += partial_imag;
    }
    bank[(first_column + column) * rows + row] = make_float2(static_cast<float>(real), static_cast<float>(imag));
}

// Warps and filters raw columns without storing them, when the taps fit one
// stage window, as the host arranges. One block filters one tile of output
// rows of every column of a set, whose columns share a map and differ only
// in their lanes. Each thread evaluates the FP64 terms of its staged rows
// once, keeps them, and stages each column's span from them; the columns
// then filter in turn as coarse_fir_tiles does. Each warped sample and each
// output row keeps the operations and order of a column warped and
// filtered alone.
extern "C" __global__ void coarse_warp_fir(const float2* raster, const double* columns, const std::uint32_t* sets,
    const float* taps, float2* bank, std::uint32_t source_rows, std::uint32_t raw_rows, std::uint32_t tap_count,
    std::uint32_t decimation, std::uint32_t rows, std::uint32_t set_count, std::uint32_t first_set,
    std::uint32_t first_lane) {
    __shared__ float2 stage[fir_stage_pairs];
    const auto tiles = (rows + threads - 1) / threads;
    const auto set = blockIdx.x / tiles, tile = blockIdx.x % tiles;
    if (set >= set_count) return;
    const auto* members = sets + (static_cast<std::uint64_t>(first_set) + set) * coarse_set_columns;
    const auto* map = columns + static_cast<std::uint64_t>(members[0]) * 5;
    // Several windows would restage the span. The host never dispatches
    // them here; a nonfinite bank, which the host rejects, if it did.
    const bool whole = fir_window(decimation, tap_count) == tap_count;
    WarpRow stash[warp_stash_rows];
    for (unsigned member = 0; member < coarse_set_columns && members[member] != no_column; ++member) {
        const auto* column_map = columns + static_cast<std::uint64_t>(members[member]) * 5;
        auto* output = bank + static_cast<std::uint64_t>(members[member]) * rows;
        if (!whole) {
            nonfinite_tile(output, rows, tile);
            continue;
        }
        const auto* lane = raster + (static_cast<std::uint64_t>(column_map[4]) - first_lane) * source_rows;
        const StashStage input{stash, map, lane, has_echo(map), member == 0, source_rows};
        filter_tile(input, taps, output, stage, raw_rows, tap_count, decimation, rows, tile, tap_count);
    }
}

// Per workspace: |x|^2, |y|^2 and conj(x) y of its one or two columns.
extern "C" __global__ void coarse_bank_gram(const float2* bank, double* gram, std::uint32_t rows,
    std::uint32_t workspaces, std::uint32_t taps) {
    __shared__ double partial[threads][4];
    const auto workspace = blockIdx.x;
    if (workspace >= workspaces) return;
    const auto* first = bank + static_cast<std::uint64_t>(workspace) * taps * rows;
    double sums[4] = {0, 0, 0, 0};
    for (auto row = threadIdx.x; row < rows; row += threads) {
        const double xr = first[row].x, xi = first[row].y;
        sums[0] += xr * xr + xi * xi;
        if (taps == 2) {
            const double yr = first[rows + row].x, yi = first[rows + row].y;
            sums[1] += yr * yr + yi * yi;
            sums[2] += xr * yr + xi * yi;
            sums[3] += xr * yi - xi * yr;
        }
    }
    for (unsigned i = 0; i < 4; ++i) partial[threadIdx.x][i] = sums[i];
    __syncthreads();
    if (threadIdx.x) return;
    for (unsigned thread = 1; thread < threads; ++thread)
        for (unsigned i = 0; i < 4; ++i) sums[i] += partial[thread][i];
    for (unsigned i = 0; i < 4; ++i) gram[static_cast<std::uint64_t>(workspace) * 4 + i] = sums[i];
}

// The largest real or imaginary magnitude of each column, or NaN.
extern "C" __global__ void coarse_column_peaks(const float2* bank, float* peaks, std::uint32_t rows,
    std::uint32_t columns) {
    __shared__ float warp_peak[threads / warp_size];
    const auto column = blockIdx.x;
    if (column >= columns) return;
    const auto* samples = bank + static_cast<std::uint64_t>(column) * rows;
    float peak = 0;
    for (auto row = threadIdx.x; row < rows; row += threads) {
        const auto value = samples[row];
        peak = fmaxf(peak, isfinite(value.x) && isfinite(value.y)
            ? fmaxf(fabsf(value.x), fabsf(value.y)) : CUDART_INF_F);
    }
    for (unsigned distance = warp_size / 2; distance; distance /= 2)
        peak = fmaxf(peak, __shfl_xor_sync(full_warp, peak, distance));
    if (threadIdx.x % warp_size == 0) warp_peak[threadIdx.x / warp_size] = peak;
    __syncthreads();
    if (threadIdx.x == 0) {
        for (unsigned warp = 1; warp < threads / warp_size; ++warp) peak = fmaxf(peak, warp_peak[warp]);
        peaks[column] = isfinite(peak) ? peak : CUDART_NAN_F;
    }
}

// Packed row 2c holds [Re, Im] pairs of column c. Row 2c+1 holds [-Im, Re],
// which yields the imaginary part of conj(template) * observation.
extern "C" __global__ void coarse_pack_bank(const float2* bank, const float* peaks, std::uint32_t* packed,
    std::uint32_t rows, std::uint32_t k, std::uint32_t columns, std::uint32_t packed_rows) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto packed_row = index / k;
    if (packed_row >= packed_rows) return;
    const auto inner = index % k;
    const auto sample = inner / 2, column = packed_row / 2;
    float value = 0;
    if (column < columns && sample < rows) {
        const auto peak = peaks[column];
        if (peak > 0) {
            const auto input = bank[static_cast<std::uint64_t>(column) * rows + sample];
            const bool second = inner % 2;
            value = (packed_row % 2 ? (second ? input.x : -input.y) : (second ? input.y : input.x)) / peak;
        }
    }
    const auto element = pack_element(value);
    const auto lane = threadIdx.x % scale_width;
    // Sixteen bytes per slice and thread slot: the slot's word of each half
    // of the slice, then the slice's four block scales.
    const auto tiles = k / tile_inner;
    auto* slice = reinterpret_cast<std::uint8_t*>(packed) +
        (static_cast<std::uint64_t>(packed_row) * tiles + inner / tile_inner) * 64;
    const auto within = inner % tile_inner;
    if (!(lane % 2)) {
        const auto byte = within / 2, word = byte / 4;
        slice[(word % 4) * 16 + (word / 4) * 4 + byte % 4] = element.pair;
    }
    if (!lane)
        for (unsigned slot = 0; slot < 4; ++slot) slice[slot * 16 + 8 + within / scale_width] = element.scale;
}

// Parameters hold one frequency per tile entry, then the projected rate
// and the reciprocal observation magnitude. Stream f * 8 + v starts at
// sample v, so its scale blocks serve the arrivals congruent to v.
extern "C" __global__ void coarse_pack_observation(const float2* observation, const double* parameters,
    std::uint32_t* packed, std::uint32_t* scales, std::uint32_t samples, std::uint32_t variant_samples,
    std::uint32_t frequencies) {
    const auto elements = 2 * variant_samples;
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    const auto stream = index / elements;
    if (stream >= frequencies * coarse_variants) return;
    const auto inner = index % elements;
    const auto sample = stream % coarse_variants + inner / 2;
    float value = 0;
    if (sample < samples) {
        const auto cycles = -parameters[stream / coarse_variants] * (static_cast<double>(sample) / parameters[frequencies]);
        float sine, cosine;
        __sincosf(static_cast<float>((cycles - floor(cycles)) * tau64), &sine, &cosine);
        const auto input = observation[sample];
        const auto rotated = inner % 2 ? input.y * cosine + input.x * sine : input.x * cosine - input.y * sine;
        value = rotated * static_cast<float>(parameters[frequencies + 1]);
    }
    const auto element = pack_element(value);
    const auto lane = threadIdx.x % scale_width;
    const auto quads = static_cast<std::uint64_t>(variant_samples / 32 + coarse_stream_slack);
    if (!(lane % 2)) {
        // Thread slot s reads words s, s + 2, ... of this variant.
        const auto byte = inner / 2, word = byte / 4;
        auto* streams = reinterpret_cast<std::uint8_t*>(packed) + stream * 4 * quads * 16;
        for (unsigned slot = word % 2; slot < 4 && slot <= word; slot += 2)
            streams[(slot * quads * 4 + (word - slot) / 2) * 4 + byte % 4] = element.pair;
    }
    if (!lane) {
        // Vector m of parity r holds the scales of blocks 2m + r onwards.
        const auto block = inner / scale_width;
        auto* vectors = reinterpret_cast<std::uint8_t*>(scales) + stream * 2 * quads * 8;
        for (unsigned parity = 0; parity < 2 && parity <= block; ++parity) {
            const auto offset = block - parity;
            for (unsigned position = offset % 2; position < 4 && position <= offset; position += 2)
                vectors[(parity * quads * 2 + (offset - position) / 2) * 4 + position] = element.scale;
        }
    }
}

// One block scores 512 arrivals of one bank tile at one frequency: the four
// template columns of two workspaces of two rays, or of four workspaces of
// one ray. Each warp owns 128 arrivals as eight 16x8 result tiles and walks
// the whole inner extent with three loads per slice: operand loads, not
// products, bound it.
extern "C" __global__ void coarse_fp4_scores(const uint4* packed_observation,
    const unsigned long long* observation_scales, const uint4* packed_bank, const double* coefficients,
    float* scores, std::uint32_t k, std::uint32_t stream_quads, std::uint32_t arrivals, std::uint32_t workspaces,
    std::uint32_t frequencies, std::uint32_t taps) {
    const auto warp = threadIdx.x / warp_size;
    const auto lane = threadIdx.x % warp_size;
    const auto quad = lane / 4, slot = lane % 4;
    const auto arrival_blocks = (arrivals + coarse_block_arrivals - 1) / coarse_block_arrivals;
    const auto bank_tiles = (workspaces * taps + 3) / 4;
    const auto bank_tile = blockIdx.x / arrival_blocks % bank_tiles;
    const auto frequency = blockIdx.x / arrival_blocks / bank_tiles;
    if (frequency >= frequencies) return;
    const auto first_arrival = blockIdx.x % arrival_blocks * coarse_block_arrivals + warp * coarse_warp_arrivals;
    // Rows quad and quad + 8 of every group are congruent to quad modulo 8.
    // Lanes 0 and 1 of a quad supply the scales of those two rows.
    const auto stream = static_cast<std::uint64_t>(frequency) * coarse_variants + quad;
    const auto* observation = packed_observation + (stream * 4 + slot) * stream_quads + first_arrival / 32;
    const auto* observation_scale = observation_scales + (stream * 2 + slot % 2) * stream_quads + first_arrival / 32;
    const auto tiles = k / tile_inner;
    const auto* bank = packed_bank + (static_cast<std::uint64_t>(bank_tile) * tile_columns + quad) * tiles * 4 + slot;

    float4 accumulator[coarse_groups];
    for (unsigned group = 0; group < coarse_groups; ++group) accumulator[group] = make_float4(0.f, 0.f, 0.f, 0.f);
    std::uint32_t ring[coarse_ring], scale_ring[coarse_scale_ring];
#pragma unroll
    for (unsigned i = 0; i < coarse_period; ++i) {
        const auto words = observation[i];
        const auto scales = observation_scale[i];
        ring[4 * i] = words.x;
        ring[4 * i + 1] = words.y;
        ring[4 * i + 2] = words.z;
        ring[4 * i + 3] = words.w;
        scale_ring[2 * i] = static_cast<std::uint32_t>(scales);
        scale_ring[2 * i + 1] = static_cast<std::uint32_t>(scales >> 32);
    }
    for (std::uint32_t tile = 0; tile < tiles; tile += coarse_period) {
#pragma unroll
        for (unsigned phase = 0; phase < coarse_period; ++phase) {
            // The slice at this ring position needs its first quad until
            // its products are issued. Load its replacement meanwhile.
            const auto next = observation[tile + phase + coarse_period];
            const auto next_scales = observation_scale[tile + phase + coarse_period];
            const auto record = bank[static_cast<std::uint64_t>(tile + phase) * 4];
#pragma unroll
            for (unsigned group = 0; group < coarse_groups; ++group) {
                const auto at = 4 * phase + 2 * group;
                multiply_tile(accumulator[group], ring[at % coarse_ring], ring[(at + 1) % coarse_ring],
                              ring[(at + 2) % coarse_ring], ring[(at + 3) % coarse_ring], record.x, record.y,
                              scale_ring[(2 * phase + group) % coarse_scale_ring], record.z);
            }
            ring[4 * phase] = next.x;
            ring[4 * phase + 1] = next.y;
            ring[4 * phase + 2] = next.z;
            ring[4 * phase + 3] = next.w;
            scale_ring[2 * phase] = static_cast<std::uint32_t>(next_scales);
            scale_ring[2 * phase + 1] = static_cast<std::uint32_t>(next_scales >> 32);
        }
    }

    // Slot s holds result columns 2s and 2s + 1: the real and imaginary
    // correlations of the tile's template column s.
    if (taps == 1) {
        // Each slot holds a whole workspace.
        const auto workspace = bank_tile * 4 + slot;
        if (workspace >= workspaces) return;
        const auto* inverse = coefficients + static_cast<std::uint64_t>(workspace) * 4;
        auto* output = scores + (static_cast<std::uint64_t>(frequency) * workspaces + workspace) * arrivals;
        // Constant accumulator indices keep the accumulators in registers.
#pragma unroll
        for (unsigned group = 0; group < coarse_groups; ++group) {
            const auto own = accumulator[group];
            const auto arrival = first_arrival + group * tile_rows + quad;
            if (arrival < arrivals) output[arrival] = ray_score(inverse, own.x, own.y);
            if (arrival + tile_rows / 2 < arrivals)
                output[arrival + tile_rows / 2] = ray_score(inverse, own.z, own.w);
        }
        return;
    }
    // Even slots hold a workspace's first ray and their neighbor its second.
    const auto workspace = bank_tile * 2 + slot / 2;
    const auto* inverse = coefficients + static_cast<std::uint64_t>(workspace < workspaces ? workspace : 0) * 4;
    auto* output = scores + (static_cast<std::uint64_t>(frequency) * workspaces + workspace) * arrivals;
    for (unsigned group = 0; group < coarse_groups; ++group) {
        const auto own = accumulator[group];
        const auto other = make_float4(__shfl_xor_sync(full_warp, own.x, 1), __shfl_xor_sync(full_warp, own.y, 1),
                                       __shfl_xor_sync(full_warp, own.z, 1), __shfl_xor_sync(full_warp, own.w, 1));
        if (slot % 2 || workspace >= workspaces) continue;
        const auto arrival = first_arrival + group * tile_rows + quad;
        if (arrival < arrivals) output[arrival] = quadratic_score(inverse, own.x, own.y, other.x, other.y);
        if (arrival + tile_rows / 2 < arrivals)
            output[arrival + tile_rows / 2] = quadratic_score(inverse, own.z, own.w, other.z, other.w);
    }
}

// Retain distinct FP4 interior maxima without minimum-distance suppression,
// then include the legacy separated pair and both endpoints. Exact rescoring
// checks each proposal's radius-two neighborhood before final selection.
extern "C" __global__ void coarse_select_peaks(const float* input, CoarseCandidateRow* output,
    std::uint32_t arrivals, std::uint32_t separation, std::uint32_t compact) {
    __shared__ std::uint64_t candidates[threads];
    const auto row_index = static_cast<std::uint64_t>(blockIdx.x);
    const auto* scores = input + row_index * arrivals;
    auto& result = output[row_index];
    bool invalid = false;
    for (auto i = threadIdx.x; i < arrivals; i += threads) invalid |= !isfinite(scores[i]);
    if (__syncthreads_or(invalid)) {
        if (!threadIdx.x) {
            result.centers = 0;
            result.count = no_peak;
        }
        return;
    }
    if (threadIdx.x == 0) {
        result.centers = 0;
        result.count = 0;
    }
    __syncthreads();
    if (arrivals == 1) {
        if (threadIdx.x == 0) {
            result.lags[result.count++] = 0;
            result.centers = 1;
        }
        return;
    }
    const auto interior_limit = compact ? coarse_compact_interior_candidates : coarse_rescore_interior_candidates;
    for (unsigned selected = 0; selected < interior_limit; ++selected) {
        std::uint64_t local = no_peak;
        for (std::uint32_t lag = threadIdx.x + 1; lag + 1 < arrivals; lag += threads) {
            const auto peak = interior_peak(scores, lag, arrivals);
            if (peak == no_peak) continue;
            bool available = true;
            for (unsigned prior = 0; prior < result.centers; ++prior)
                if (peak == result.lags[prior]) available = false;
            if (available && better_peak(scores, peak, local)) local = peak;
        }
        const auto best = reduce_peak(scores, local, candidates);
        if (best == no_peak) break;
        if (threadIdx.x == 0) result.lags[result.centers++] = best;
        __syncthreads();
    }
    // The first shortlist entry is also the first separated pick. Reuse it
    // and search only for the second pick outside its exclusion distance.
    if (result.centers) {
        const auto first = result.lags[0];
        std::uint64_t local = no_peak;
        for (std::uint32_t lag = threadIdx.x + 1; lag + 1 < arrivals; lag += threads) {
            const auto peak = interior_peak(scores, lag, arrivals);
            if (peak != no_peak && separated(peak, first, separation) && better_peak(scores, peak, local))
                local = peak;
        }
        const auto second = reduce_peak(scores, local, candidates);
        if (second != no_peak && threadIdx.x == 0) {
            bool present = false;
            for (unsigned prior = 0; prior < result.centers; ++prior)
                if (result.lags[prior] == second) present = true;
            if (!present && result.centers < coarse_rescore_candidates) result.lags[result.centers++] = second;
        }
    }
    if (threadIdx.x == 0) {
        if (compact) {
            if (scores[0] >= scores[1] && result.centers < coarse_rescore_candidates)
                result.lags[result.centers++] = 0;
            if (scores[arrivals - 1] >= scores[arrivals - 2] && result.centers < coarse_rescore_candidates)
                result.lags[result.centers++] = arrivals - 1;
        } else {
            if (result.centers < coarse_rescore_candidates) result.lags[result.centers++] = 0;
            if (result.centers < coarse_rescore_candidates) result.lags[result.centers++] = arrivals - 1;
        }
    }
    __syncthreads();
    // Center proposals precede their unique neighboring score samples. The
    // fixed bound is five samples for each interior or endpoint proposal.
    if (threadIdx.x == 0) {
        result.count = result.centers;
        if (!compact) {
            for (unsigned center = 0; center < result.centers; ++center) {
                const auto lag = result.lags[center];
                const auto first = lag > coarse_rescore_radius ? lag - coarse_rescore_radius : 0;
                const auto last = lag + coarse_rescore_radius < arrivals ? lag + coarse_rescore_radius : arrivals - 1;
                for (auto neighbor = first; neighbor <= last; ++neighbor) {
                    bool present = false;
                    for (unsigned i = 0; i < result.count; ++i)
                        if (result.lags[i] == neighbor) present = true;
                    if (!present && result.count < coarse_rescore_scores) result.lags[result.count++] = neighbor;
                }
            }
        }
    }
}

// The FP32 observation rotated to each frequency of the tile, as
// [frequency][sample], for coarse_rescore_peaks. Parameters follow it.
extern "C" __global__ void coarse_rotate_observation(const float2* observation, const double* parameters,
    float2* rotated, std::uint32_t samples, std::uint32_t frequencies) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto frequency = index / samples;
    if (frequency >= frequencies) return;
    const auto sample = index % samples;
    const auto cycles = -parameters[frequency] * (static_cast<double>(sample) / parameters[frequencies]);
    float sine, cosine;
    __sincosf(static_cast<float>((cycles - floor(cycles)) * tau64), &sine, &cosine);
    const auto input = observation[sample];
    rotated[index] = make_float2(input.x * cosine - input.y * sine, input.y * cosine + input.x * sine);
}

namespace {

// The quadratic score of one retained peak from its two correlations, with
// the FP64 inverse Gram of its workspace, divided by the variance, as
// coarse_rescore_peaks forms it.
__device__ double rescored(const double (&sums)[4], const double* c, double variance) {
    const auto real_product = sums[0] * c[2] + sums[1] * c[3];
    const auto imag_product = sums[0] * c[3] - sums[1] * c[2];
    const auto score = c[0] * (sums[0] * sums[0] + sums[1] * sums[1]) + c[1] * (sums[2] * sums[2] + sums[3] * sums[3]) +
                       2 * (real_product * sums[2] - imag_product * sums[3]);
    return score / variance;
}

__device__ bool exact_candidate_score(const CoarseCandidateRow& row, std::uint64_t lag, double& score) {
    for (unsigned i = 0; i < row.count; ++i) {
        if (row.lags[i] == lag) {
            score = row.scores[i];
            return true;
        }
    }
    return false;
}

__device__ bool better_exact_peak(const CoarseCandidateRow& row, std::uint64_t a, std::uint64_t b) {
    if (a == no_peak) return false;
    if (b == no_peak) return true;
    double a_score = 0, b_score = 0;
    if (!exact_candidate_score(row, a, a_score) || !exact_candidate_score(row, b, b_score)) return false;
    return a_score == b_score ? a > b : a_score > b_score;
}

// Finish the native exact-score selection policy on the wider rescored set.
// The public device frontier remains the existing two-arrival structure.
extern "C" __global__ void coarse_finalize_peaks(const CoarseCandidateRow* candidates, CoarsePublishedRow* output,
    std::uint32_t arrivals, std::uint32_t separation, std::uint32_t compact) {
    const auto row_index = static_cast<std::uint64_t>(blockIdx.x);
    const auto& row = candidates[row_index];
    auto& result = output[row_index];
    if (threadIdx.x) return;
    result.scores[0] = result.scores[1] = 0;
    result.lags[0] = result.lags[1] = 0;
    result.count = 0;
    result.rescored = row.count;
    const auto limit = compact ? coarse_compact_rescore_scores : coarse_rescore_scores;
    if (!row.count || row.count > limit) {
        result.count = no_peak;
        result.rescored = no_peak;
        return;
    }
    for (unsigned i = 0; i < row.count; ++i) {
        if (!isfinite(row.scores[i])) {
            result.count = no_peak;
            result.rescored = no_peak;
            return;
        }
    }
    if (arrivals == 1) {
        double score = 0;
        if (!exact_candidate_score(row, 0, score)) result.count = no_peak;
        else {
            result.scores[0] = score;
            result.count = 1;
        }
        return;
    }

    const auto is_eligible_peak = [&](std::uint64_t lag) {
        if (!lag || lag + 1 >= arrivals) return false;
        if (compact) return true;
        double score = 0;
        if (!exact_candidate_score(row, lag, score)) return false;
        auto first = lag;
        auto last = lag;
        double neighbor = 0;
        while (first && exact_candidate_score(row, first - 1, neighbor) && neighbor == score) --first;
        if (!first && exact_candidate_score(row, 0, neighbor) && neighbor == score) return false;
        if (first && !exact_candidate_score(row, first - 1, neighbor)) return false;
        if (!(neighbor < score)) return false;
        while (last + 1 < arrivals && exact_candidate_score(row, last + 1, neighbor) && neighbor == score) ++last;
        if (last + 1 >= arrivals || !exact_candidate_score(row, last + 1, neighbor) || !(neighbor < score)) return false;
        return lag == first + (last - first) / 2;
    };

    std::uint64_t interiors[2] = {no_peak, no_peak};
    unsigned interior_count = 0;
    for (unsigned i = 0; i < row.count; ++i) {
        const auto lag = row.lags[i];
        if (is_eligible_peak(lag) && better_exact_peak(row, lag, interiors[0])) interiors[0] = lag;
    }
    if (interiors[0] != no_peak) {
        interior_count = 1;
        for (unsigned i = 0; i < row.count; ++i) {
            const auto lag = row.lags[i];
            if (is_eligible_peak(lag) && separated(lag, interiors[0], separation) &&
                better_exact_peak(row, lag, interiors[1])) interiors[1] = lag;
        }
        if (interiors[1] != no_peak) interior_count = 2;
    }

    std::uint64_t choices[4] = {interiors[0], interiors[1], no_peak, no_peak};
    unsigned used = interior_count;
    if (compact) {
        double score = 0;
        if (exact_candidate_score(row, 0, score)) choices[used++] = 0;
        if (exact_candidate_score(row, arrivals - 1, score)) choices[used++] = arrivals - 1;
    } else {
        double left = 0, left_neighbor = 0, right = 0, right_neighbor = 0;
        if (!exact_candidate_score(row, 0, left) || !exact_candidate_score(row, 1, left_neighbor) ||
            !exact_candidate_score(row, arrivals - 1, right) ||
            !exact_candidate_score(row, arrivals - 2, right_neighbor)) {
            result.count = no_peak;
            return;
        }
        if (left >= left_neighbor) choices[used++] = 0;
        if (right >= right_neighbor) choices[used++] = arrivals - 1;
    }
    bool clipped = used > 2;
    if (used == 2 && interior_count == 2) {
        for (unsigned i = 0; i < row.count; ++i) {
            const auto lag = row.lags[i];
            if (!separated(lag, interiors[0], separation) || !separated(lag, interiors[1], separation)) continue;
            if (is_eligible_peak(lag)) {
                clipped = true;
                break;
            }
        }
    }
    if (clipped) {
        for (unsigned i = 1; i < used; ++i)
            for (auto j = i; j && better_exact_peak(row, choices[j], choices[j - 1]); --j) {
                const auto temporary = choices[j];
                choices[j] = choices[j - 1];
                choices[j - 1] = temporary;
            }
        result.count = 2;
        result.lags[0] = choices[1];
        result.lags[1] = choices[0];
    } else {
        for (unsigned i = 1; i < used; ++i)
            for (auto j = i; j && choices[j] < choices[j - 1]; --j) {
                const auto temporary = choices[j];
                choices[j] = choices[j - 1];
                choices[j - 1] = temporary;
            }
        result.count = used;
        for (unsigned i = 0; i < used; ++i) result.lags[i] = choices[i];
    }
    if (!result.count) {
        // The exact peak can move beyond the immediate neighborhood of every
        // FP4 proposal. Preserve a finite, deterministic candidate in that
        // case instead of silently publishing no arrival.
        std::uint64_t best = no_peak;
        for (unsigned i = 0; i < row.count; ++i)
            if (better_exact_peak(row, row.lags[i], best)) best = row.lags[i];
        if (best == no_peak || !exact_candidate_score(row, best, result.scores[0])) result.count = no_peak;
        else {
            result.lags[0] = best;
            result.count = 1;
        }
    }
    for (unsigned i = 0; i < result.count; ++i)
        if (!exact_candidate_score(row, result.lags[i], result.scores[i])) {
            result.count = no_peak;
            return;
        }
}

} // namespace

// Replaces the retained frontier scores with FP32 products accumulated in
// FP64 from the unquantized resident bank and the rotated observation. One
// block owns one retained peak. Parameters follow coarse_pack_observation,
// with the variance in the place of the observation magnitude. This is the
// matched control of coarse_rescore_peaks_fp32.
extern "C" __global__ void coarse_rescore_peaks(const float2* bank, const float2* rotated,
    const double* parameters, const double* inverse, CoarseCandidateRow* peaks, std::uint32_t rows,
    std::uint32_t samples, std::uint32_t workspaces, std::uint32_t frequencies, std::uint32_t taps,
    std::uint32_t stride) {
    __shared__ double partial[threads][4];
    const auto row_index = blockIdx.x / stride, entry = blockIdx.x % stride;
    const auto frequency = row_index / workspaces, workspace = row_index % workspaces;
    if (frequency >= frequencies) return;
    const auto count = peaks[row_index].count;
    if (count == no_peak || count > stride || entry >= count) return;
    const auto arrival = peaks[row_index].lags[entry];
    const auto* first = bank + static_cast<std::uint64_t>(workspace) * taps * rows;
    const auto* observation = rotated + static_cast<std::uint64_t>(frequency) * samples;
    double sums[4] = {0, 0, 0, 0};
    for (std::uint64_t row = threadIdx.x; row < rows && row + arrival < samples; row += threads) {
        const auto value = observation[row + arrival];
        const double real = value.x, imag = value.y;
        const double xr = first[row].x, xi = first[row].y;
        sums[0] += xr * real + xi * imag;
        sums[1] += xr * imag - xi * real;
        if (taps == 2) {
            const double yr = first[rows + row].x, yi = first[rows + row].y;
            sums[2] += yr * real + yi * imag;
            sums[3] += yr * imag - yi * real;
        }
    }
    for (unsigned i = 0; i < 4; ++i) partial[threadIdx.x][i] = sums[i];
    __syncthreads();
    if (threadIdx.x) return;
    for (unsigned thread = 1; thread < threads; ++thread)
        for (unsigned i = 0; i < 4; ++i) sums[i] += partial[thread][i];
    const auto* c = inverse + static_cast<std::uint64_t>(workspace) * 4;
    const auto real_product = sums[0] * c[2] + sums[1] * c[3];
    const auto imag_product = sums[0] * c[3] - sums[1] * c[2];
    const auto score = c[0] * (sums[0] * sums[0] + sums[1] * sums[1]) + c[1] * (sums[2] * sums[2] + sums[3] * sums[3]) +
                       2 * (real_product * sums[2] - imag_product * sums[3]);
    peaks[row_index].scores[entry] = score / parameters[frequencies + 1];
}

// The same scores with the FIR's partial-sum scheme: each thread sums the
// FP32 products of 64 of its rows in FP32 with single-rounding multiply-adds
// and adds each partial sum in FP64, and the block adds its threads' sums as
// an FP64 tree. The correlations, Gram and score stay FP64.
extern "C" __global__ void coarse_rescore_peaks_fp32(const float2* bank, const float2* rotated,
    const double* parameters, const double* inverse, CoarseCandidateRow* peaks, std::uint32_t rows,
    std::uint32_t samples, std::uint32_t workspaces, std::uint32_t frequencies, std::uint32_t taps,
    std::uint32_t stride) {
    __shared__ double partial[threads][4];
    const auto row_index = blockIdx.x / stride, entry = blockIdx.x % stride;
    const auto frequency = row_index / workspaces, workspace = row_index % workspaces;
    if (frequency >= frequencies) return;
    const auto count = peaks[row_index].count;
    if (count == no_peak || count > stride || entry >= count) return;
    const auto arrival = peaks[row_index].lags[entry];
    const auto* first = bank + static_cast<std::uint64_t>(workspace) * taps * rows;
    const auto* observation = rotated + static_cast<std::uint64_t>(frequency) * samples;
    double sums[4] = {0, 0, 0, 0};
    float chunk[4] = {0, 0, 0, 0};
    unsigned products = 0;
    for (std::uint64_t row = threadIdx.x; row < rows && row + arrival < samples; row += threads) {
        const auto value = observation[row + arrival];
        const auto x = first[row];
        chunk[0] = __fmaf_rn(x.y, value.y, __fmaf_rn(x.x, value.x, chunk[0]));
        chunk[1] = __fmaf_rn(-x.y, value.x, __fmaf_rn(x.x, value.y, chunk[1]));
        if (taps == 2) {
            const auto y = first[rows + row];
            chunk[2] = __fmaf_rn(y.y, value.y, __fmaf_rn(y.x, value.x, chunk[2]));
            chunk[3] = __fmaf_rn(-y.y, value.x, __fmaf_rn(y.x, value.y, chunk[3]));
        }
        if (++products == fir_chunk) {
            for (unsigned i = 0; i < 4; ++i) {
                sums[i] += chunk[i];
                chunk[i] = 0;
            }
            products = 0;
        }
    }
    for (unsigned i = 0; i < 4; ++i) partial[threadIdx.x][i] = sums[i] + chunk[i];
    __syncthreads();
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width)
            for (unsigned i = 0; i < 4; ++i) partial[threadIdx.x][i] += partial[threadIdx.x + width][i];
        __syncthreads();
    }
    if (threadIdx.x) return;
    const double total[4] = {partial[0][0], partial[0][1], partial[0][2], partial[0][3]};
    peaks[row_index].scores[entry] = rescored(total, inverse + static_cast<std::uint64_t>(workspace) * 4,
                                              parameters[frequencies + 1]);
}
