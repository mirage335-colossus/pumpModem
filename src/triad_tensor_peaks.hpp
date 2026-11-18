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

// Cast one block of the original complex128 column and zero-pad in complex64.
cudaError_t cuda_segmented_column(const void* source, void* column, std::size_t rows,
                                  std::size_t fft_rows, cudaStream_t stream) noexcept;

// One block of projected complex64 observation spectra [phases, fft_rows]
// and a single complex64 column spectrum [fft_rows]. Fill the contiguous
// inverse FFT input [frequency_count, fft_rows] for the requested public bins.
cudaError_t cuda_segmented_product(const void* observation, const void* column, void* products,
                                   std::size_t fft_rows, std::size_t phases, std::size_t radius,
                                   std::size_t bin_stride, std::size_t first_frequency,
                                   std::size_t frequency_count, cudaStream_t stream) noexcept;

// Correlations are unnormalized cuFFT inverse rows [frequency_count, fft_rows].
// The kernel normalizes each complex64 sample by the power-of-two FFT length.
// The caller initializes block_best[lag] to zero for each block. A nonfinite
// update sets block_best[lag] to NaN, which the caller checks before publishing
// results. When hop is nonzero, previous and next are distinct FP32 tables
// [frequencies, arrivals]. Finish every tile before swapping the tables.
cudaError_t cuda_segmented_ranks(const void* correlations, double* block_best, const float* previous,
                                 float* next, std::size_t fft_rows, std::size_t arrivals,
                                 std::size_t frequencies, std::size_t first_frequency,
                                 std::size_t frequency_count, std::size_t hop, double energy,
                                 double noise_power, cudaStream_t stream) noexcept;

// Add the completed block's best emission to sum and/or reduce the final
// frequency state to one smooth rank per lag. A null block_best skips the
// addition, and null final_state and smooth skip the final reduction.
cudaError_t cuda_segmented_finish(const double* block_best, double* sum, const float* final_state,
                                  double* smooth, std::size_t arrivals, std::size_t frequencies,
                                  cudaStream_t stream) noexcept;

} // namespace datapump::triad::v3::receiver_detail
