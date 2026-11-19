// Compile device code offline for SM120. The NVIDIA OpenCL worker loads it
// as native kernel binaries, without loading or initializing CUDA Runtime.
#include <cuda_fp4.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <cstdint>

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
