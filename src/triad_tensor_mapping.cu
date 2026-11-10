#include "triad_tensor_mapping.cuh"

#include <cuda_runtime.h>

#include <cmath>

namespace datapump::triad::v3::receiver_detail {
namespace {

constexpr unsigned threads_per_block = 128;
constexpr auto table_width = 3 + cuda_mapping_maximum_pins;
constexpr double tau = 6.2831853071795864769;

__global__ void mapped_c64(const double* maps, const std::int64_t* table, const double* offsets,
                           const c10::complex<float>* tiles, c10::complex<float>* output,
                           CudaMappingView view) {
    const auto sample = static_cast<std::int64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (sample >= view.rows) return;

    const auto* map = maps + 4 * blockIdx.y;
    const auto* lane = table + table_width * blockIdx.y;
    const auto destination = lane[2];
    if (destination < 0 || destination >= view.output_columns) return;

    // Retain the FP64 coordinate and phase operations of the ATen path.
    // This translation unit is compiled with multiply-add contraction off.
    const double offset = offsets[sample];
    const double relative = map[0] + map[1] * offset;
    const double base = ::floor(relative);
    const auto index = lane[0] + static_cast<std::int64_t>(base);
    const auto extent = lane[1];

    const float u = static_cast<float>(relative - base);
    const float less_one = u - 1;
    const float less_two = u - 2;
    const float more_one = u + 1;
    // ATen's scalar CUDA division multiplies by the rounded reciprocal.
    const float weights[]{
        -u * less_one * less_two * (1.f / 6.f),
        more_one * less_one * less_two * .5f,
        -more_one * u * less_two * .5f,
        more_one * u * less_one * (1.f / 6.f)};

    float real = 0, imag = 0;
    for (int j = 0; j < 4; ++j) {
        const auto knot = index + j - 1;
        if (knot < 0 || knot >= extent) continue;
        const auto tile = knot / cuda_mapping_tile_samples;
        if (tile >= cuda_mapping_maximum_pins) continue;
        const auto slot = lane[3 + tile];
        if (slot < 0 || slot >= view.tile_slots) continue;
        const auto value = tiles[slot * cuda_mapping_tile_samples + knot % cuda_mapping_tile_samples];
        real += value.real() * weights[j];
        imag += value.imag() * weights[j];
    }

    const double cycles = map[2] + map[3] * offset;
    const float angle = static_cast<float>((cycles - ::floor(cycles)) * tau);
    const float cosine = ::cosf(angle);
    const float sine = ::sinf(angle);
    // Match the CUDA ATen complex multiply's final rotation rounding.
    // Explicit fusion here leaves coordinate and interpolation contraction off.
    const float rotated_real = __fmaf_rn(real, cosine, -__fmul_rn(imag, sine));
    const float rotated_imag = __fmaf_rn(imag, cosine, __fmul_rn(real, sine));
    output[destination * view.output_column_stride + sample * view.output_sample_stride] =
        c10::complex<float>{rotated_real, rotated_imag};
}

} // namespace

cudaError_t launch_cuda_mapped_c64(const double* maps, const std::int64_t* table, const double* offsets,
                                  const c10::complex<float>* tiles, c10::complex<float>* output,
                                  CudaMappingView view, cudaStream_t stream) noexcept {
    if (!maps || !table || !offsets || !tiles || !output ||
        view.rows <= 0 || view.rows > 16'384 || view.lanes <= 0 || view.lanes > 32 ||
        view.tile_slots <= 0 || view.output_columns <= 0 ||
        view.output_column_stride <= 0 || view.output_sample_stride <= 0)
        return cudaErrorInvalidValue;
    const auto blocks = static_cast<unsigned>((view.rows + threads_per_block - 1) / threads_per_block);
    mapped_c64<<<dim3(blocks, static_cast<unsigned>(view.lanes)), threads_per_block, 0, stream>>>(
        maps, table, offsets, tiles, output, view);
    return cudaGetLastError();
}

} // namespace datapump::triad::v3::receiver_detail
