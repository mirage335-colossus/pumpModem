#pragma once

#include <c10/util/complex.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace datapump::triad::v3::receiver_detail {

enum class CudaMatrixMath { fp16, int8 };

// Loaded-library identity without constructing a cuBLAS handle or executing
// DSP. NVIDIA explicitly supports a null handle for cublasGetVersion().
int cuda_matrix_library_version();

// Caller-owned device storage, initially all zero. Diagnostics accumulate
// across launches on the same stream. Inspect them after the caller's normal
// synchronization, before publishing scores. Nonfinite inputs are a failure;
// saturation is reported independently of the arithmetic error qualification.
struct CudaMatrixDiagnostics {
    std::uint64_t nonfinite_values = 0;
    std::uint64_t saturated_values = 0;
    double maximum_a_scale = 0;
    double maximum_b_scale = 0;
};

struct CudaMatrixWork {
    std::uint64_t gemm_calls = 0;
    std::uint64_t inner_chunks = 0;
    std::uint64_t padded_real_macs = 0;
    std::uint64_t packed_values = 0;
};

// One instance belongs to one serialized tensor stage. Construct it under the
// stage's CUDA device guard. The handle is reused; scratch belongs to the
// caller's bounded arena and includes the complete explicit cuBLAS workspace.
// Driver/library context allocations remain part of the backend's separately
// reserved runtime headroom. This class owns no sample or matrix allocation.
class CudaComplexMatrix {
public:
    CudaComplexMatrix();
    ~CudaComplexMatrix();
    CudaComplexMatrix(const CudaComplexMatrix&) = delete;
    CudaComplexMatrix& operator=(const CudaComplexMatrix&) = delete;

    bool supported(CudaMatrixMath) const noexcept;
    // Validate padded geometry and the caller-charged workspace, then check
    // device capability without allocating storage or executing arithmetic.
    bool supported_shape(CudaMatrixMath, std::size_t rows,
                         std::size_t columns, std::size_t inner,
                         void* scratch, std::size_t scratch_bytes);
    int cublas_version() const noexcept;
    int device_major() const noexcept;
    int device_minor() const noexcept;

    static std::size_t workspace_bytes(CudaMatrixMath, std::size_t rows,
                                       std::size_t columns, std::size_t inner);

    static std::size_t gram_workspace_bytes() noexcept;

    // Input is contiguous row-major [batches,taps,modes], with taps in 1..2.
    // Each output batch is COLUMN-major [taps,taps], holding
    // G_ij = sum_k conj(input[i,k]) * input[j,k]. A row-major tensor view must
    // transpose its last two axes to expose G. Products and accumulation stay
    // complex FP64; the fine Gaussian state's precision policy is unchanged.
    // Scratch must be 256-byte aligned and at least gram_workspace_bytes().
    // Input, output and scratch must not overlap. No matrix allocation or
    // synchronization occurs; all storage stays live until stream completion.
    void gram_c128(const c10::complex<double>* input, c10::complex<double>* output,
                   std::size_t batches, std::size_t taps, std::size_t modes,
                   void* scratch, std::size_t scratch_bytes, cudaStream_t);

    // A[rows,inner], B[inner,columns], C[rows,columns] are contiguous row-major
    // complex matrices, with no conjugation. The function overwrites C. It
    // enqueues work without synchronizing or allocating matrix storage. Keep
    // all pointers and this object alive until that stream has completed.
    // Scratch must be 256-byte aligned, must not alias A/B/C/diagnostics, and
    // must have at least workspace_bytes() bytes. Errors are exceptions;
    // unsupported arithmetic is never silently replaced by another dtype.
    CudaMatrixWork launch(CudaMatrixMath, const c10::complex<float>* a,
                         const c10::complex<float>* b, c10::complex<double>* c,
                         std::size_t rows, std::size_t columns, std::size_t inner,
                         void* scratch, std::size_t scratch_bytes,
                         CudaMatrixDiagnostics*, cudaStream_t);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3::receiver_detail
