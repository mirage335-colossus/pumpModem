#pragma once

#include "triad_bulk_peaks.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>

namespace datapump::triad::v3::receiver_detail {

// Select exactly the native select_coarse_arrivals() frontier from contiguous
// FP64 scores[rows, arrivals]. Every score is validated, including values not
// retained as peaks. A row containing any nonfinite value returns count equal
// to UINT64_MAX; finite rows return one or two entries in native selection order.
// Input/output storage must not overlap and must remain live until the supplied
// stream completes. This function neither allocates nor synchronizes. Its return
// value reports invalid launch geometry or CUDA launch errors, not row validity.
cudaError_t cuda_select_coarse_peaks(const double* scores, CoarseBulkPeaks* output,
                                    std::size_t rows, std::size_t arrivals,
                                    std::size_t separation, cudaStream_t stream) noexcept;

} // namespace datapump::triad::v3::receiver_detail
