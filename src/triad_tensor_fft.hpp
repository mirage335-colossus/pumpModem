#pragma once

#include <c10/util/complex.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace datapump::triad::v3::receiver_detail {

int cuda_fft_library_version();

class CudaHalfFftAllocationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An owned, single-stream, in-place CUDA_C_16F C2C plan. Make the plan under
// the caller's CUDA device guard. The library reports its required work area
// before the caller admits and allocates device storage.
class CudaHalfFftPlan {
public:
    static constexpr std::size_t alignment = 256;
    // An unsupported device or geometry returns nullptr before execution.
    // CUDA/library failures other than unsupported geometry throw.
    static std::unique_ptr<CudaHalfFftPlan> make(std::size_t rows, std::size_t batch, cudaStream_t);
    ~CudaHalfFftPlan();
    CudaHalfFftPlan(const CudaHalfFftPlan&) = delete;
    CudaHalfFftPlan& operator=(const CudaHalfFftPlan&) = delete;

    std::size_t data_bytes() const noexcept { return data_bytes_; }
    std::size_t workspace_bytes() const noexcept { return workspace_bytes_; }

    // Data and caller-owned scratch must be 256-byte aligned and disjoint.
    // Keep the plan and buffers alive until its stream drains, including on
    // cancellation. Neither direction applies a normalization factor.
    void enqueue(void* data, void* workspace, std::size_t workspace_bytes, bool inverse);

private:
    CudaHalfFftPlan();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::size_t data_bytes_ = 0, workspace_bytes_ = 0;
};

struct CudaHalfFftProductShape {
    std::size_t rows, workspaces, frequencies, taps;
};

enum CudaHalfFftDiagnostic : unsigned {
    cuda_half_fft_nonfinite_input = 1,
    cuda_half_fft_invalid_product = 2,
    cuda_half_fft_nonfinite_arrival = 4,
};

// All helpers use contiguous, disjoint device buffers owned by the caller.
// Zero diagnostics before use and check them after the normal synchronization
// before publishing a score. The helpers neither allocate nor synchronize.
// Packing applies an exact binary scale per row and pads to the FFT extent.
// Long rows need temporary chunk summaries. This storage can share the cuFFT
// work area on the same stream because packing ends before the transform.
std::size_t cuda_half_fft_pack_workspace_bytes(std::size_t rows, std::size_t valid, std::size_t batch) noexcept;
cudaError_t cuda_half_fft_pack(const c10::complex<float>* input, void* packed,
    std::int32_t* exponents, unsigned* diagnostics, std::size_t rows, std::size_t valid,
    std::size_t batch, void* workspace, std::size_t workspace_bytes, cudaStream_t) noexcept;

// bank[workspace,tap,bin] and observation[frequency,bin] are forward spectra
// from the packer above. Form observation * conj(bank) in FP32, then round
// once into inverse[workspace,frequency,tap,bin] for the half transform.
cudaError_t cuda_half_fft_products(const void* bank, const void* observation, void* inverse,
    unsigned* diagnostics, CudaHalfFftProductShape, cudaStream_t) noexcept;

// Restore the original scale and 1/N inverse normalization in FP64 for each
// physical arrival. Output layout matches the native quadratic score kernel.
cudaError_t cuda_half_fft_arrivals(const void* inverse, const std::int32_t* bank_exponents,
    const std::int32_t* observation_exponents, c10::complex<double>* correlations,
    unsigned* diagnostics, CudaHalfFftProductShape, std::size_t arrivals, cudaStream_t) noexcept;

} // namespace datapump::triad::v3::receiver_detail
