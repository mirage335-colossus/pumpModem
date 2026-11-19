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
constexpr unsigned maximum_source_tiles = 32;
constexpr unsigned unpaired_output = 0xffffu;
constexpr unsigned candidate_lanes = 4;
constexpr unsigned bit_statistics = 5;
constexpr unsigned candidate_statistics = 2 * bit_statistics + 1;

struct DeviceMapping {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::uint16_t output, second_output;
    std::uint32_t tile_count;
    std::uint32_t tiles[maximum_source_tiles];
};
static_assert(sizeof(DeviceMapping) == 176 && alignof(DeviceMapping) == 8);
static_assert(offsetof(DeviceMapping, second_output) == 42 && offsetof(DeviceMapping, tiles) == 48);

__device__ const double2* source_tile(const double2* samples, const std::uint32_t* tiles, unsigned tile) {
    return samples + static_cast<std::uint64_t>(tiles[tile]) * source_tile_samples;
}

__device__ double2 source_neighbor(const double2* samples, const std::uint32_t* tiles,
                                   std::int64_t index, std::int64_t extent) {
    if (index < 0 || index >= extent) return {};
    return source_tile(samples, tiles, index / source_tile_samples)[index % source_tile_samples];
}

__device__ double cubic(double a, double b, double c, double d, double w0, double w1, double w2, double w3) {
    return __dadd_rn(__dadd_rn(__dadd_rn(__dmul_rn(w0, a), __dmul_rn(w1, b)), __dmul_rn(w2, c)), __dmul_rn(w3, d));
}

__device__ void render_lane_row(const double2* samples, const std::uint32_t* tiles,
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

__device__ void pack_block(float value, std::uint32_t* packed, std::uint8_t* scales, unsigned index) {
    // Each padded row is a multiple of the launch width. Bounds checks
    // discard whole blocks, so all lanes reach each warp shuffle.
    const auto lane = threadIdx.x % scale_width;
    float maximum = fabsf(value);
    for (unsigned bit = scale_width / 2; bit; bit /= 2)
        maximum = fmaxf(maximum, __shfl_xor_sync(full_warp, maximum, bit, scale_width));
    // Use the full range of the nested block scale and E2M1 value. The
    // product stage removes the shared normalized_max factors afterwards.
    const __nv_fp8_e4m3 scale(maximum * maximum_scale);
    const auto represented = static_cast<float>(scale);
    const auto normalized = represented > 0 ? value * (normalized_max / represented) : 0.f;
    const auto sibling = __shfl_xor_sync(full_warp, normalized, 1, scale_width);
    if (!lane) scales[index / scale_width] = scale.__x;
    if (!(lane % 2))
        reinterpret_cast<std::uint8_t*>(packed)[index / 2] =
            __nv_cvt_float2_to_fp4x2(make_float2(normalized, sibling), __NV_E2M1, cudaRoundNearest);
}

__device__ void multiply_tile(float4& accumulator,
    std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t b0, std::uint32_t b1,
    std::uint32_t scale_a, std::uint32_t scale_b) {
    constexpr std::uint16_t selector = 0;
    asm volatile(
        "mma.sync.aligned.m16n8k64.row.col.kind::mxf4nvf4.block_scale.scale_vec::4X.f32.e2m1.e2m1.f32.ue4m3 "
        "{%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, {%0,%1,%2,%3}, %10, {%12,%12}, %11, {%12,%12};"
        : "+f"(accumulator.x), "+f"(accumulator.y), "+f"(accumulator.z), "+f"(accumulator.w)
        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(b0), "r"(b1),
          "r"(scale_a), "r"(scale_b), "h"(selector));
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
