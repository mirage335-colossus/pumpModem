#pragma once

#include <c10/util/complex.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace datapump::triad::v3::receiver_detail {

enum class CudaMatrixMath { fp16, int8 };

class CudaMatrixAllocationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

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
    std::uint64_t lt_tensor_calls = 0;
    std::uint64_t inner_chunks = 0;
    std::uint64_t padded_real_macs = 0;
    std::uint64_t packed_values = 0;
};

// One instance belongs to one serialized tensor stage. Construct it under the
// stage's CUDA device guard. A shared device context keeps cuBLASLt alive for
// the arena, while each owner supplies its own descriptors, stream and scratch.
// Driver/library context allocations remain part of the backend's separately
// reserved runtime headroom. This class owns no sample or matrix allocation.
class CudaComplexMatrix {
public:
    class Context;
    static std::shared_ptr<Context> make_context();
    explicit CudaComplexMatrix(std::shared_ptr<Context> = {});
    ~CudaComplexMatrix();
    CudaComplexMatrix(const CudaComplexMatrix&) = delete;
    CudaComplexMatrix& operator=(const CudaComplexMatrix&) = delete;

    bool supported(CudaMatrixMath) const noexcept;
    // Validate padded geometry and the caller-charged workspace, then check
    // device capability without allocating storage or executing arithmetic.
    bool supported_shape(CudaMatrixMath, std::size_t rows,
                         std::size_t columns, std::size_t inner,
                         void* scratch, std::size_t scratch_bytes);
    // Cache a library-declared tensor algorithm for this exact padded shape.
    bool has_tensor_algorithm(CudaMatrixMath, std::size_t rows, std::size_t columns, std::size_t inner);

    static std::size_t workspace_bytes(CudaMatrixMath, std::size_t rows,
                                       std::size_t columns, std::size_t inner);

    static std::size_t prepared_right_bytes(CudaMatrixMath, std::size_t columns, std::size_t inner);
    static std::size_t padded_real_macs_per_row(std::size_t columns, std::size_t inner);

    // Prepare the immutable right operand for B[k,f] = phase[k] *
    // convolution[(f-k) mod convolution_rows]. Store the same padded real
    // representation and column scales used by the GEMM for every inner
    // chunk. The caller owns the aligned storage and diagnostics until the
    // stream completes. Returns the number of packed real values.
    std::uint64_t prepare_right(CudaMatrixMath, const c10::complex<float>* phase,
                                const c10::complex<float>* convolution, std::size_t convolution_rows,
                                std::size_t columns, std::size_t inner,
                                void* prepared, std::size_t prepared_bytes,
                                CudaMatrixDiagnostics*, cudaStream_t);

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

    // A[rows,inner] and C[rows,columns] are contiguous row-major complex
    // matrices. B is the immutable prepared right operand for this math,
    // column count and inner count. The function overwrites C. It
    // enqueues work without synchronizing or allocating matrix storage. Keep
    // all pointers and this object alive until that stream has completed.
    // Scratch must be 256-byte aligned, must not alias A/B/C/diagnostics, and
    // must have at least workspace_bytes() bytes. Errors are exceptions;
    // unsupported arithmetic is never silently replaced by another dtype.
    CudaMatrixWork launch(CudaMatrixMath, const c10::complex<float>* a,
                         const void* prepared_right, std::size_t prepared_bytes, c10::complex<double>* c,
                         std::size_t rows, std::size_t columns, std::size_t inner,
                         void* scratch, std::size_t scratch_bytes,
                         CudaMatrixDiagnostics*, cudaStream_t);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3::receiver_detail
