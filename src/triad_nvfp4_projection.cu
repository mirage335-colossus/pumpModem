#include "triad_nvfp4_projection.hpp"

#include <cublasLt.h>
#include <cuda_fp4.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace datapump::triad::v3::receiver_detail {
namespace {

constexpr unsigned threads = 128;
constexpr unsigned block_values = 16;
constexpr std::size_t alignment = 256;
constexpr std::size_t library_workspace = 4 * 1024 * 1024;
constexpr float fp4_max = 6;
constexpr float scale_max = 448;
constexpr float normalized_max = fp4_max * scale_max;

void cuda_check(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string("Robust NVFP4 projection ") + operation + ": " + cudaGetErrorString(status));
}

void blas_check(cublasStatus_t status, const char* operation) {
    if (status != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error(std::string("Robust NVFP4 projection ") + operation + ": " +
                                 cublasLtGetStatusString(status));
}

bool unsupported(cublasStatus_t status) {
    return status == CUBLAS_STATUS_NOT_SUPPORTED || status == CUBLAS_STATUS_ARCH_MISMATCH;
}

std::size_t add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::overflow_error("Robust NVFP4 projection workspace addition overflow");
    return a + b;
}

std::size_t multiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::overflow_error("Robust NVFP4 projection workspace multiplication overflow");
    return a * b;
}

std::size_t round_up(std::size_t value, std::size_t multiple) {
    return multiply(add(value, multiple - 1) / multiple, multiple);
}

std::size_t append(std::size_t& cursor, std::size_t bytes) {
    const auto offset = cursor;
    cursor = round_up(add(cursor, bytes), alignment);
    return offset;
}

unsigned blocks(std::size_t jobs) {
    const auto grid = add(jobs, threads - 1) / threads;
    if (grid > std::numeric_limits<int>::max())
        throw std::invalid_argument("Robust NVFP4 projection CUDA grid extent is too large");
    return static_cast<unsigned>(grid);
}

struct Packed {
    std::size_t outer, inner, groups, storage_bytes, scale_bytes;
    std::size_t values, scales;

    Packed(std::size_t outer_, std::size_t inner_, std::size_t& cursor)
        : outer(outer_), inner(inner_), groups(inner / block_values),
          storage_bytes(multiply(outer, inner / 2)), scale_bytes(multiply(round_up(outer, 128), groups)),
          values(append(cursor, storage_bytes)), scales(append(cursor, scale_bytes)) {
        (void)blocks(multiply(outer, inner));
    }
};

struct Shape {
    std::size_t rows, modes, batch;
    std::size_t k, m, n;

    Shape(std::size_t rows_, std::size_t modes_, std::size_t batch_)
        : rows(rows_), modes(modes_), batch(batch_), k(multiply(2, round_up(rows, 64))),
          m(round_up(multiply(2, modes), 32)), n(round_up(batch, 16)) {
        const auto max = static_cast<std::size_t>(std::numeric_limits<int>::max());
        if (!rows || !modes || modes > rows || !batch || k > max || m > max || n > max)
            throw std::invalid_argument("invalid Robust NVFP4 projection geometry");
        (void)blocks(multiply(batch, modes));
    }
};

struct Coefficients {
    std::size_t bytes = 0;
    Packed packed;

    explicit Coefficients(const Shape& shape) : packed(shape.m, shape.k, bytes) {}
};

struct Scratch {
    std::size_t bytes = library_workspace;
    Packed input;
    std::size_t amplitudes, partial;

    explicit Scratch(const Shape& shape)
        : input(shape.n, shape.k, bytes), amplitudes(append(bytes, multiply(shape.n, sizeof(float)))),
          partial(append(bytes, multiply(multiply(shape.m, shape.n), sizeof(float)))) {}
};

unsigned char* checked_bytes(void* pointer, std::size_t bytes, std::size_t required) {
    if (!pointer || reinterpret_cast<std::uintptr_t>(pointer) % alignment || bytes < required)
        throw std::invalid_argument("Robust NVFP4 projection workspace is too small or misaligned");
    return static_cast<unsigned char*>(pointer);
}

__device__ std::size_t scale_index(std::size_t outer, std::size_t group, std::size_t groups) {
    // cuBLASLt VEC16_UE4M3. A scale tile covers 128 outer rows and 64
    // scalars along K, even when a matrix dimension ends mid-tile.
    const auto tile = ((outer / 128) * groups + (group / 4) * 4) * 128;
    return tile + (outer % 32) * 16 + ((outer % 128) / 32) * 4 + group % 4;
}

__device__ void pack_block(float value, std::size_t row, std::size_t inner,
                           const Packed& layout, unsigned char* storage) {
    const auto lane = threadIdx.x % block_values;
    // Each packed row is a multiple of the launch width. Bounds checks
    // discard only whole blocks, so all lanes reconverge at each shuffle.
    constexpr unsigned mask = 0xffffffffu;
    float peak = fabsf(value);
    for (unsigned bit = block_values / 2; bit; bit /= 2)
        peak = fmaxf(peak, __shfl_xor_sync(mask, peak, bit, block_values));

    // Fill the UE4M3 range as well as E2M1. Very small blocks can otherwise
    // lose all amplitude when their scale is rounded to zero.
    const __nv_fp8_e4m3 scale(peak * scale_max);
    const auto represented = static_cast<float>(scale);
    const auto normalized = represented > 0 ? value * (normalized_max / represented) : 0.f;
    const auto sibling = __shfl_xor_sync(mask, normalized, 1, block_values);
    if (!lane)
        storage[layout.scales + scale_index(row, inner / block_values, layout.groups)] = scale.__x;
    if (!(lane % 2))
        storage[layout.values + (row * layout.inner + inner) / 2] =
            __nv_cvt_float2_to_fp4x2(make_float2(normalized, sibling), __NV_E2M1, cudaRoundNearest);
}

__global__ void pack_coefficients(const std::int64_t* bins, unsigned char* storage, Shape shape, Packed layout) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto mode_row = index / layout.inner;
    if (mode_row >= layout.outer) return;
    const auto inner = index % layout.inner;
    const auto sample = inner % (shape.k / 2);
    float value = 0;
    if (mode_row / 2 < shape.modes && sample < shape.rows) {
        // Integer reduction preserves the phase of long/high-bin DFTs
        // before the only approximate operation, coefficient quantization.
        const auto turn = (static_cast<std::uint64_t>(bins[mode_row / 2]) * sample) % shape.rows;
        constexpr double tau = 6.283185307179586476925286766559;
        double sin_angle, cos_angle;
        sincos(-tau * static_cast<double>(turn) / shape.rows, &sin_angle, &cos_angle);
        const auto imaginary_input = inner >= shape.k / 2;
        if (mode_row % 2) value = static_cast<float>(imaginary_input ? cos_angle : sin_angle);
        else value = static_cast<float>(imaginary_input ? -sin_angle : cos_angle);
    }
    pack_block(value, mode_row, inner, layout, storage);
}

__global__ void input_amplitudes(const float2* input, float* amplitudes, std::size_t rows, std::size_t lanes) {
    __shared__ float maxima[threads];
    const auto lane = static_cast<std::size_t>(blockIdx.x);
    float maximum = 0;
    bool invalid = false;
    if (lane < lanes) {
        for (std::size_t i = threadIdx.x; i < rows; i += blockDim.x) {
            const auto value = input[lane * rows + i];
            invalid |= !isfinite(value.x) || !isfinite(value.y);
            maximum = fmaxf(maximum, fmaxf(fabsf(value.x), fabsf(value.y)));
        }
    }
    maxima[threadIdx.x] = maximum;
    const auto any_invalid = __syncthreads_or(invalid);
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) maxima[threadIdx.x] = fmaxf(maxima[threadIdx.x], maxima[threadIdx.x + width]);
        __syncthreads();
    }
    if (!threadIdx.x) amplitudes[lane] = any_invalid ? nanf("") : maxima[0];
}

__global__ void pack_input(const float2* input, const float* amplitudes, unsigned char* storage,
                           Shape shape, Packed layout, std::size_t lanes) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto input_row = index / layout.inner;
    if (input_row >= layout.outer) return;
    const auto inner = index % layout.inner;
    const auto sample = inner % (shape.k / 2);
    const auto peak = amplitudes[input_row];
    float value = 0;
    if (input_row < lanes && sample < shape.rows && peak > 0) {
        const auto source = input[input_row * shape.rows + sample];
        value = (inner < shape.k / 2 ? source.x : source.y) / peak;
    }
    pack_block(value, input_row, inner, layout, storage);
}

__global__ void unpack_output(const float* partial, const float* amplitudes, float2* output,
                              Shape shape, std::size_t lanes, float normalization) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto input_row = index / shape.modes;
    if (input_row >= lanes) return;
    const auto offset = input_row * shape.m + 2 * (index % shape.modes);
    output[index] = make_float2(amplitudes[input_row] * (partial[offset] * normalization),
                                amplitudes[input_row] * (partial[offset + 1] * normalization));
}

} // namespace

struct CudaNvfp4Projection::Impl {
    Shape shape;
    Coefficients coefficients;
    Scratch scratch;
    unsigned char* prepared = nullptr;
    cublasLtHandle_t handle = nullptr;
    cublasLtMatmulDesc_t operation = nullptr;
    cublasLtMatrixLayout_t basis = nullptr, input = nullptr, output = nullptr;
    std::array<cublasLtMatmulAlgo_t, 8> algorithms{};
    std::size_t algorithm_count = 0, next_algorithm = 0;
    bool tried_algorithms = false;

    Impl(std::size_t rows, std::size_t modes, std::size_t batch_capacity)
        : shape(rows, modes, batch_capacity), coefficients(shape), scratch(shape) {}

    ~Impl() {
        if (output) (void)cublasLtMatrixLayoutDestroy(output);
        if (input) (void)cublasLtMatrixLayoutDestroy(input);
        if (basis) (void)cublasLtMatrixLayoutDestroy(basis);
        if (operation) (void)cublasLtMatmulDescDestroy(operation);
        if (handle) (void)cublasLtDestroy(handle);
    }

    void create_descriptors() {
        blas_check(cublasLtCreate(&handle), "create cuBLASLt handle");
        blas_check(cublasLtMatmulDescCreate(&operation, CUBLAS_COMPUTE_32F, CUDA_R_32F), "create FP32 accumulation descriptor");
        const auto transpose = CUBLAS_OP_T;
        blas_check(cublasLtMatmulDescSetAttribute(operation, CUBLASLT_MATMUL_DESC_TRANSA,
                                                  &transpose, sizeof(transpose)), "set TN operand layout");
        const auto scale = CUBLASLT_MATMUL_MATRIX_SCALE_VEC16_UE4M3;
        blas_check(cublasLtMatmulDescSetAttribute(operation, CUBLASLT_MATMUL_DESC_A_SCALE_MODE,
                                                  &scale, sizeof(scale)), "set basis UE4M3 scale mode");
        blas_check(cublasLtMatmulDescSetAttribute(operation, CUBLASLT_MATMUL_DESC_B_SCALE_MODE,
                                                  &scale, sizeof(scale)), "set input UE4M3 scale mode");
        blas_check(cublasLtMatrixLayoutCreate(&basis, CUDA_R_4F_E2M1, shape.k, shape.m, shape.k),
                   "set basis NVFP4 layout");
        blas_check(cublasLtMatrixLayoutCreate(&input, CUDA_R_4F_E2M1, shape.k, shape.n, shape.k),
                   "set input NVFP4 layout");
        blas_check(cublasLtMatrixLayoutCreate(&output, CUDA_R_32F, shape.m, shape.n, shape.m), "set FP32 result layout");
    }

    bool select_algorithms() {
        if (tried_algorithms) return next_algorithm < algorithm_count;
        cublasLtMatmulPreference_t preference = nullptr;
        blas_check(cublasLtMatmulPreferenceCreate(&preference), "create FP4 selection preferences");
        const std::unique_ptr<std::remove_pointer_t<cublasLtMatmulPreference_t>, decltype(&cublasLtMatmulPreferenceDestroy)>
            owner(preference, cublasLtMatmulPreferenceDestroy);
        const std::uint64_t workspace_limit = library_workspace;
        blas_check(cublasLtMatmulPreferenceSetAttribute(preference, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES,
                                                      &workspace_limit, sizeof(workspace_limit)), "bound FP4 workspace");
        std::array<cublasLtMatmulHeuristicResult_t, 8> candidates{};
        int count = 0;
        const auto status = cublasLtMatmulAlgoGetHeuristic(handle, operation, basis, input, output, output,
            preference, static_cast<int>(candidates.size()), candidates.data(), &count);
        if (!unsupported(status)) {
            blas_check(status, "select FP4 algorithm");
            for (int i = 0; i < count; ++i) {
                const auto& candidate = candidates[static_cast<std::size_t>(i)];
                if (candidate.state != CUBLAS_STATUS_SUCCESS || candidate.workspaceSize > workspace_limit) continue;
                // cuBLASLt advertises this E2M1/UE4M3 operation only as
                // native block-scaled Tensor Core arithmetic on Blackwell.
                algorithms[algorithm_count++] = candidate.algo;
            }
        }
        tried_algorithms = true;
        return next_algorithm < algorithm_count;
    }
};

CudaNvfp4Projection::CudaNvfp4Projection(std::size_t rows, std::size_t modes, std::size_t batch_capacity)
    : impl_(std::make_unique<Impl>(rows, modes, batch_capacity)) {
    impl_->create_descriptors();
}

CudaNvfp4Projection::~CudaNvfp4Projection() = default;

bool CudaNvfp4Projection::device_supported(int device) {
    // cuBLAS 12.8.4, CUDA 12.8 Update 1, introduced block-scaled
    // GeForce-class Blackwell products. Also check the loaded library.
    if (device < 0 || cublasLtGetVersion() < 120804) return false;
    cudaDeviceProp properties{};
    if (cudaGetDeviceProperties(&properties, device) != cudaSuccess) return false;
    // The packing kernels are compiled for the exact RTX Blackwell target.
    return properties.major == 12 && properties.minor == 0;
}

std::size_t CudaNvfp4Projection::coefficient_bytes(std::size_t rows, std::size_t modes) {
    return Coefficients(Shape(rows, modes, 1)).bytes;
}

std::size_t CudaNvfp4Projection::scratch_bytes(std::size_t rows, std::size_t modes, std::size_t batch_capacity) {
    return Scratch(Shape(rows, modes, batch_capacity)).bytes;
}

void CudaNvfp4Projection::prepare(const std::int64_t* ordered_device_bins, void* coefficients,
                                  std::size_t bytes, cudaStream_t stream) {
    auto& plan = *impl_;
    if (!ordered_device_bins) throw std::invalid_argument("Robust NVFP4 projection bins are missing");
    const auto& layout = plan.coefficients.packed;
    auto* storage = checked_bytes(coefficients, bytes, plan.coefficients.bytes);
    cuda_check(cudaMemsetAsync(storage + layout.scales, 0, layout.scale_bytes, stream), "clear basis scale padding");
    pack_coefficients<<<blocks(layout.outer * layout.inner), threads, 0, stream>>>(ordered_device_bins,
                                                                                 storage, plan.shape, layout);
    cuda_check(cudaGetLastError(), "pack fine DFT basis");
    plan.prepared = storage;
}

bool CudaNvfp4Projection::project(const void* input_complex_fp32, void* output_complex_fp32, std::size_t lanes,
                                  void* scratch, std::size_t bytes, cudaStream_t stream) {
    auto& plan = *impl_;
    if (!plan.prepared || !input_complex_fp32 || !output_complex_fp32 || !lanes || lanes > plan.shape.batch)
        throw std::invalid_argument("Robust NVFP4 projection batch does not match its prepared plan");
    if (reinterpret_cast<std::uintptr_t>(input_complex_fp32) % alignof(float2) ||
        reinterpret_cast<std::uintptr_t>(output_complex_fp32) % alignof(float2))
        throw std::invalid_argument("Robust NVFP4 projection complex rows are misaligned");
    auto* storage = checked_bytes(scratch, bytes, plan.scratch.bytes);
    const auto& basis = plan.coefficients.packed;
    const auto& input = plan.scratch.input;
    const void* basis_scales = plan.prepared + basis.scales;
    const void* input_scales = storage + input.scales;
    blas_check(cublasLtMatmulDescSetAttribute(plan.operation, CUBLASLT_MATMUL_DESC_A_SCALE_POINTER,
                                             &basis_scales, sizeof(basis_scales)), "set basis scale pointer");
    blas_check(cublasLtMatmulDescSetAttribute(plan.operation, CUBLASLT_MATMUL_DESC_B_SCALE_POINTER,
                                             &input_scales, sizeof(input_scales)), "set input scale pointer");
    if (!plan.select_algorithms()) return false;

    auto* amplitudes = reinterpret_cast<float*>(storage + plan.scratch.amplitudes);
    auto* partial = reinterpret_cast<float*>(storage + plan.scratch.partial);
    const auto* samples = static_cast<const float2*>(input_complex_fp32);
    input_amplitudes<<<static_cast<unsigned>(plan.shape.n), threads, 0, stream>>>(samples, amplitudes,
                                                                                plan.shape.rows, lanes);
    cuda_check(cudaGetLastError(), "reduce template amplitudes");
    cuda_check(cudaMemsetAsync(storage + input.scales, 0, input.scale_bytes, stream), "clear input scale padding");
    pack_input<<<blocks(input.outer * input.inner), threads, 0, stream>>>(samples, amplitudes, storage,
                                                                         plan.shape, input, lanes);
    cuda_check(cudaGetLastError(), "pack fine DATA templates");

    constexpr float alpha = 1, beta = 0;
    for (; plan.next_algorithm < plan.algorithm_count; ++plan.next_algorithm) {
        const auto& algorithm = plan.algorithms[plan.next_algorithm];
        const auto status = cublasLtMatmul(plan.handle, plan.operation, &alpha, plan.prepared + basis.values, plan.basis,
                                         storage + input.values, plan.input, &beta, partial, plan.output, partial,
                                         plan.output, &algorithm, storage, library_workspace, stream);
        if (unsupported(status)) continue;
        blas_check(status, "execute block-scaled FP4 Tensor Core projection");
        break;
    }
    if (plan.next_algorithm == plan.algorithm_count) return false;
    const auto norm = 1.f / (normalized_max * normalized_max * std::sqrt(static_cast<float>(plan.shape.rows)));
    unpack_output<<<blocks(lanes * plan.shape.modes), threads, 0, stream>>>(partial, amplitudes,
        static_cast<float2*>(output_complex_fp32), plan.shape, lanes, norm);
    cuda_check(cudaGetLastError(), "extract complex FP32 projection");
    return true;
}

} // namespace datapump::triad::v3::receiver_detail
