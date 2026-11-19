#pragma once

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace datapump::triad::v3::receiver_detail {

// Unitary forward DFT at the retained fine-projection bins. Both real
// operands use NVFP4 with UE4M3 block-16 scales. FP32 accumulation produces
// complex FP32 modes for the existing higher precision likelihood stage.
class CudaNvfp4Projection {
public:
    CudaNvfp4Projection(std::size_t rows, std::size_t modes, std::size_t batch_capacity);
    ~CudaNvfp4Projection();
    CudaNvfp4Projection(const CudaNvfp4Projection&) = delete;
    CudaNvfp4Projection& operator=(const CudaNvfp4Projection&) = delete;

    static bool device_supported(int device);
    static std::size_t coefficient_bytes(std::size_t rows, std::size_t modes);
    static std::size_t scratch_bytes(std::size_t rows, std::size_t modes, std::size_t batch_capacity);

    // Create the immutable packed coefficients in 256-byte-aligned device
    // storage owned by the caller. Bins must be increasing within [0, rows).
    // Keep the device active and storage alive across queued calls. Use one
    // stream per owner, or explicitly order calls across streams. Call again
    // after changing any bin or releasing the coefficient storage.
    void prepare(const std::int64_t* ordered_device_bins, void* coefficients,
                 std::size_t bytes, cudaStream_t);

    // Row-major device arrays of interleaved real/imaginary floats with shapes
    // [lanes, rows] and [lanes, modes], both aligned to 8 bytes. Input, output,
    // coefficients and aligned scratch must not alias. Returns false if none
    // of the viable cuBLASLt heuristics accepts the packed FP4 operation.
    // Nonfinite input rows emit NaN instead of truncated modes.
    // Calls enqueue work without synchronizing. Serialize access to this owner
    // and keep all storage alive until the stream completes.
    bool project(const void* input_complex_fp32, void* output_complex_fp32,
                 std::size_t lanes, void* scratch, std::size_t bytes, cudaStream_t);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3::receiver_detail
