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
constexpr auto no_peak = ~std::uint64_t{0};

struct CoarsePeakRow {
    double scores[2];
    std::uint64_t lags[2];
    std::uint64_t count;
};
static_assert(sizeof(CoarsePeakRow) == 40 && alignof(CoarsePeakRow) == 8);

__device__ float quadratic_score(const double* inverse, float a_real, float a_imag, float b_real, float b_imag) {
    // The host folds both operand scales and the noise variance into the
    // four inverse-Gram coefficients of this workspace.
    const double ar = a_real, ai = a_imag, br = b_real, bi = b_imag;
    const auto real_product = ar * inverse[2] + ai * inverse[3];
    const auto imag_product = ar * inverse[3] - ai * inverse[2];
    return static_cast<float>(inverse[0] * (ar * ar + ai * ai) + inverse[1] * (br * br + bi * bi) +
                              2 * (real_product * br - imag_product * bi));
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
// as PatternSource::sample_at does for the native exact route.
extern "C" __global__ void coarse_render_lanes(const float2* chips, const float* pulse, const double* source,
    float2* raster, std::uint32_t source_rows, std::uint32_t lane_count, std::uint32_t cells,
    std::uint32_t first_lane) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto lane_index = index / source_rows;
    if (lane_index >= lane_count) return;
    const auto position = static_cast<double>(index % source_rows) * source[0];
    auto value = make_float2(0.f, 0.f);
    if (position < source[2]) {
        const auto t = (position - source[1]) / source[3];
        const auto envelope = source_envelope(source, t);
        const auto chip_time = t * source[4];
        const auto offset = source[5];
        const auto first_real = fmax(0., ceil(chip_time - (8 + offset)));
        const auto last_real = fmin(source[6] - 1, floor(chip_time + (8 - offset)));
        if (envelope != 0 && last_real >= first_real) {
            const auto first = static_cast<std::uint32_t>(first_real), last = static_cast<std::uint32_t>(last_real);
            const auto* lane = chips + (first_lane + lane_index) * cells * 3;
            const auto gain = static_cast<float>(carrier_gain * envelope);
            float2 carriers[3] = {make_float2(0.f, 0.f), make_float2(0.f, 0.f), make_float2(0.f, 0.f)};
            for (auto fine = first; fine <= last; ++fine) {
                const auto weight = gain * pulse_at(pulse, chip_time - (fine + offset));
                const auto* chip = lane + static_cast<std::uint64_t>(fine) * 3;
                for (unsigned carrier = 0; carrier < 3; ++carrier) {
                    carriers[carrier].x += chip[carrier].x * weight;
                    carriers[carrier].y += chip[carrier].y * weight;
                }
            }
            // The outer carriers rotate by -/+ one carrier spacing.
            const auto cycles = source[7] * t;
            float sine, cosine;
            __sincosf(static_cast<float>((cycles - floor(cycles)) * tau64), &sine, &cosine);
            value.x = carriers[1].x + (carriers[0].x + carriers[2].x) * cosine + (carriers[0].y - carriers[2].y) * sine;
            value.y = carriers[1].y + (carriers[0].y + carriers[2].y) * cosine + (carriers[2].x - carriers[0].x) * sine;
        }
    }
    raster[index] = value;
}

// Warps resident lane renderings into raw template columns with the native
// four-point interpolation and echo rotation. A column descriptor holds
// first position, clock scale, first echo cycles, echo cycle step and lane.
extern "C" __global__ void coarse_warp_columns(const float2* raster, const double* columns, float2* raw,
    std::uint32_t source_rows, std::uint32_t raw_rows, std::uint32_t column_count, std::uint32_t first_column,
    std::uint32_t first_lane) {
    const auto index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto column = index / raw_rows;
    if (column >= column_count) return;
    const auto row = static_cast<double>(index % raw_rows);
    const auto* map = columns + (first_column + column) * 5;
    const auto* lane = raster + (static_cast<std::uint64_t>(map[4]) - first_lane) * source_rows;
    const auto position = map[0] + row * map[1];
    auto value = make_float2(0.f, 0.f);
    if (position > -2 && position < static_cast<double>(source_rows) + 1) {
        const auto integral = floor(position);
        const auto u = static_cast<float>(position - integral);
        const float weights[4] = {-u * (u - 1) * (u - 2) / 6, (u + 1) * (u - 1) * (u - 2) / 2,
                                  -(u + 1) * u * (u - 2) / 2, (u + 1) * u * (u - 1) / 6};
        const auto center = static_cast<std::int64_t>(integral);
        for (int i = 0; i < 4; ++i) {
            const auto sample = center + i - 1;
            if (sample < 0 || sample >= static_cast<std::int64_t>(source_rows)) continue;
            value.x += weights[i] * lane[sample].x;
            value.y += weights[i] * lane[sample].y;
        }
        if (map[2] != 0 || map[3] != 0) {
            const auto echo = map[2] + row * map[3];
            float sine, cosine;
            __sincosf(static_cast<float>((echo - floor(echo)) * tau64), &sine, &cosine);
            value = make_float2(value.x * cosine - value.y * sine, value.y * cosine + value.x * sine);
        }
    }
    raw[index] = value;
}

// The canonical decimating FIR: output row n is the full convolution at
// n * decimation. Bounded FP32 partial sums are added in FP64.
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

// One block scores 512 arrivals of two workspaces at one frequency. Each warp
// owns 128 arrivals as eight 16x8 result tiles and walks the whole inner
// extent with three loads per slice: operand loads, not products, bound it.
extern "C" __global__ void coarse_fp4_scores(const uint4* packed_observation,
    const unsigned long long* observation_scales, const uint4* packed_bank, const double* coefficients,
    float* scores, std::uint32_t k, std::uint32_t stream_quads, std::uint32_t arrivals, std::uint32_t workspaces,
    std::uint32_t frequencies) {
    const auto warp = threadIdx.x / warp_size;
    const auto lane = threadIdx.x % warp_size;
    const auto quad = lane / 4, slot = lane % 4;
    const auto arrival_blocks = (arrivals + coarse_block_arrivals - 1) / coarse_block_arrivals;
    const auto pairs = (workspaces + 1) / 2;
    const auto pair = blockIdx.x / arrival_blocks % pairs;
    const auto frequency = blockIdx.x / arrival_blocks / pairs;
    if (frequency >= frequencies) return;
    const auto first_arrival = blockIdx.x % arrival_blocks * coarse_block_arrivals + warp * coarse_warp_arrivals;
    // Rows quad and quad + 8 of every group are congruent to quad modulo 8.
    // Lanes 0 and 1 of a quad supply the scales of those two rows.
    const auto stream = static_cast<std::uint64_t>(frequency) * coarse_variants + quad;
    const auto* observation = packed_observation + (stream * 4 + slot) * stream_quads + first_arrival / 32;
    const auto* observation_scale = observation_scales + (stream * 2 + slot % 2) * stream_quads + first_arrival / 32;
    const auto tiles = k / tile_inner;
    const auto* bank = packed_bank + (static_cast<std::uint64_t>(pair) * tile_columns + quad) * tiles * 4 + slot;

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

    // Even slots hold a workspace's first ray and their neighbor its second.
    const auto workspace = pair * 2 + slot / 2;
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

// The exact bounded arrival frontier of triad_tensor_peaks.cu, over FP32
// scores. One block owns one [frequency][workspace] row.
extern "C" __global__ void coarse_select_peaks(const float* input, CoarsePeakRow* output,
    std::uint32_t arrivals, std::uint32_t separation) {
    __shared__ std::uint64_t candidates[threads];
    const auto row_index = static_cast<std::uint64_t>(blockIdx.x);
    const auto* scores = input + row_index * arrivals;
    auto& result = output[row_index];

    std::uint64_t local = no_peak;
    bool invalid = false;
    for (auto i = threadIdx.x; i < arrivals; i += threads) {
        invalid |= !isfinite(scores[i]);
        const auto peak = interior_peak(scores, i, arrivals);
        if (better_peak(scores, peak, local)) local = peak;
    }
    if (__syncthreads_or(invalid)) {
        if (!threadIdx.x) {
            result.scores[0] = result.scores[1] = 0;
            result.lags[0] = result.lags[1] = 0;
            result.count = no_peak;
        }
        return;
    }
    if (arrivals == 1) {
        if (!threadIdx.x) {
            result.scores[0] = scores[0];
            result.scores[1] = 0;
            result.lags[0] = result.lags[1] = 0;
            result.count = 1;
        }
        return;
    }
    const auto first = reduce_peak(scores, local, candidates);
    std::uint64_t second = no_peak;
    if (first != no_peak) {
        local = no_peak;
        for (auto i = threadIdx.x + 1; i + 1 < arrivals; i += threads) {
            const auto peak = interior_peak(scores, i, arrivals);
            if (peak != no_peak && separated(peak, first, separation) && better_peak(scores, peak, local))
                local = peak;
        }
        second = reduce_peak(scores, local, candidates);
    }
    const bool left = scores[0] >= scores[1];
    const bool right = scores[arrivals - 1] >= scores[arrivals - 2];
    const unsigned interiors = static_cast<unsigned>(first != no_peak) + static_cast<unsigned>(second != no_peak);
    const unsigned count = interiors + static_cast<unsigned>(left) + static_cast<unsigned>(right);
    bool clipped = count > 2;
    if (count == 2 && interiors == 2) {
        bool third = false;
        for (auto i = threadIdx.x + 1; i + 1 < arrivals; i += threads) {
            const auto peak = interior_peak(scores, i, arrivals);
            if (peak != no_peak && separated(peak, first, separation) && separated(peak, second, separation)) {
                third = true;
                break;
            }
        }
        clipped = __syncthreads_or(third);
    }
    if (threadIdx.x) return;

    std::uint64_t choices[4] = {0, 0, 0, 0};
    unsigned used = 0;
    if (first != no_peak) choices[used++] = first;
    if (second != no_peak) choices[used++] = second;
    if (left) choices[used++] = 0;
    if (right) choices[used++] = arrivals - 1;
    for (unsigned i = 1; i < used; ++i) {
        for (auto j = i; j && (clipped ? better_peak(scores, choices[j], choices[j - 1]) :
                                       choices[j] < choices[j - 1]); --j) {
            const auto temporary = choices[j];
            choices[j] = choices[j - 1];
            choices[j - 1] = temporary;
        }
    }
    result.scores[0] = result.scores[1] = 0;
    result.lags[0] = result.lags[1] = 0;
    if (clipped) {
        result.count = 2;
        result.lags[0] = choices[1];
        result.lags[1] = choices[0];
    } else {
        result.count = used;
        for (unsigned i = 0; i < used; ++i) result.lags[i] = choices[i];
    }
    for (unsigned i = 0; i < result.count; ++i) result.scores[i] = scores[result.lags[i]];
}

// Replaces the retained frontier scores with FP32 products accumulated in
// FP64 from the unquantized resident bank. One block owns one retained peak.
// Parameters follow coarse_pack_observation, with the variance in the place
// of the observation magnitude.
extern "C" __global__ void coarse_rescore_peaks(const float2* bank, const float2* observation,
    const double* parameters, const double* inverse, CoarsePeakRow* peaks, std::uint32_t rows,
    std::uint32_t samples, std::uint32_t workspaces, std::uint32_t frequencies, std::uint32_t taps) {
    __shared__ double partial[threads][4];
    const auto row_index = blockIdx.x / 2, entry = blockIdx.x % 2;
    const auto frequency = row_index / workspaces, workspace = row_index % workspaces;
    if (frequency >= frequencies) return;
    const auto count = peaks[row_index].count;
    if (count > 2 || entry >= count) return;
    const auto arrival = peaks[row_index].lags[entry];
    const auto* first = bank + static_cast<std::uint64_t>(workspace) * taps * rows;
    double sums[4] = {0, 0, 0, 0};
    for (std::uint64_t row = threadIdx.x; row < rows && row + arrival < samples; row += threads) {
        const auto sample = row + arrival;
        const auto cycles = -parameters[frequency] * (static_cast<double>(sample) / parameters[frequencies]);
        float sine, cosine;
        __sincosf(static_cast<float>((cycles - floor(cycles)) * tau64), &sine, &cosine);
        const auto input = observation[sample];
        const double real = input.x * cosine - input.y * sine, imag = input.y * cosine + input.x * sine;
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
