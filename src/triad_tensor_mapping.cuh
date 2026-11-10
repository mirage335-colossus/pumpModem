#pragma once

#include <c10/util/complex.h>
#include <cuda_runtime_api.h>

#include <cstdint>

namespace datapump::triad::v3::receiver_detail {

inline constexpr std::int64_t cuda_mapping_tile_samples = 8192;
inline constexpr std::int64_t cuda_mapping_maximum_pins = 32;

struct CudaMappingView {
    std::int64_t rows;
    std::int64_t lanes;
    std::int64_t tile_slots;
    std::int64_t output_columns;
    std::int64_t output_column_stride;
    std::int64_t output_sample_stride;
};

// The output pointer already includes the tensor view's storage offset.
// The caller keeps the coefficient/offset tensors and tile lease alive on
// the guarded stream. The return value reports launch errors without waiting.
cudaError_t launch_cuda_mapped_c64(const double* maps, const std::int64_t* table, const double* offsets,
                                  const c10::complex<float>* tiles, c10::complex<float>* output,
                                  CudaMappingView view, cudaStream_t stream) noexcept;

} // namespace datapump::triad::v3::receiver_detail
