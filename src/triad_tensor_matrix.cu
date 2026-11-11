#include "triad_tensor_matrix.hpp"

#include <cublas_v2.h>
#include <cuComplex.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace datapump::triad::v3::receiver_detail {
namespace {

constexpr unsigned threads = 128;
constexpr std::size_t alignment = 256;
constexpr std::size_t library_workspace = 4 * 1024 * 1024;
constexpr std::size_t complex_chunk = 512;

void cuda_check(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string("Robust matrix ") + operation + ": " + cudaGetErrorString(status));
}

void blas_check(cublasStatus_t status, const char* operation) {
    if (status != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error(std::string("Robust matrix ") + operation +
                                 " failed with cuBLAS status " + std::to_string(static_cast<int>(status)));
}

std::size_t add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::overflow_error("Robust matrix workspace addition overflow");
    return a + b;
}

std::size_t mul(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::overflow_error("Robust matrix workspace multiplication overflow");
    return a * b;
}

std::size_t rounded(std::size_t value, std::size_t multiple) {
    return mul(add(value, multiple - 1) / multiple, multiple);
}

struct Layout {
    std::size_t m, n, k;
    std::size_t a_scale, b_scale, a, b, partial, bytes;
};

Layout layout(CudaMatrixMath math, std::size_t rows, std::size_t columns, std::size_t inner) {
    if ((math != CudaMatrixMath::fp16 && math != CudaMatrixMath::int8) ||
        !rows || !columns || !inner || rows > INT_MAX || columns > INT_MAX / 2 || inner > INT_MAX)
        throw std::invalid_argument("invalid Robust matrix arithmetic or geometry");
    Layout out{};
    out.m = rounded(rows, 16);
    out.n = rounded(columns, 16);
    out.k = rounded(std::min(inner, complex_chunk), 16);
    if (out.m > INT_MAX || out.n > INT_MAX / 2)
        throw std::invalid_argument("Robust matrix padded dimensions exceed cuBLAS limits");
    const auto scalar_bytes = math == CudaMatrixMath::fp16 ? sizeof(__half) : sizeof(std::int8_t);
    std::size_t cursor = library_workspace;
    const auto reserve = [&](std::size_t bytes) {
        const auto first = cursor;
        cursor = rounded(add(cursor, bytes), alignment);
        return first;
    };
    out.a_scale = reserve(mul(out.m, sizeof(float)));
    out.b_scale = reserve(mul(out.n, sizeof(float)));
    out.a = reserve(mul(mul(out.m, 2 * out.k), scalar_bytes));
    out.b = reserve(mul(mul(2 * out.k, 2 * out.n), scalar_bytes));
    out.partial = reserve(mul(mul(out.m, 2 * out.n), sizeof(std::int32_t)));
    out.bytes = cursor;
    // Validate the output and operation counters as well as scratch storage.
    (void)mul(mul(rows, columns), sizeof(c10::complex<double>));
    (void)mul(mul(mul(out.m, 2 * out.n), 2 * out.k), (inner - 1) / complex_chunk + 1);
    return out;
}

unsigned blocks(std::size_t values) {
    const auto count = add(values, threads - 1) / threads;
    if (count > INT_MAX) throw std::invalid_argument("Robust matrix launch exceeds CUDA grid limit");
    return static_cast<unsigned>(count);
}

__device__ void positive_max(double* address, double value) {
    // All scales are finite nonnegative floats. Their positive double bit
    // representation is ordered, so an integer maximum is exact here.
    atomicMax(reinterpret_cast<unsigned long long*>(address), __double_as_longlong(value));
}

template<bool Left>
__global__ void scales(const c10::complex<float>* input, float* output,
                       std::size_t rows, std::size_t columns, std::size_t inner,
                       std::size_t first, std::size_t count, CudaMatrixDiagnostics* diagnostics) {
    __shared__ float largest[threads];
    const auto outer = static_cast<std::size_t>(blockIdx.x);
    const auto extent = Left ? rows : columns;
    float maximum = 0;
    unsigned long long invalid = 0;
    if (outer < extent) {
        for (auto k = static_cast<std::size_t>(threadIdx.x); k < count; k += blockDim.x) {
            const auto value = input[Left ? outer * inner + first + k : (first + k) * columns + outer];
            const float real = value.real(), imag = value.imag();
            if (::isfinite(real)) maximum = ::fmaxf(maximum, ::fabsf(real));
            else ++invalid;
            if (::isfinite(imag)) maximum = ::fmaxf(maximum, ::fabsf(imag));
            else ++invalid;
        }
    }
    if (invalid) atomicAdd(reinterpret_cast<unsigned long long*>(&diagnostics->nonfinite_values), invalid);
    largest[threadIdx.x] = maximum;
    __syncthreads();
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) largest[threadIdx.x] = ::fmaxf(largest[threadIdx.x], largest[threadIdx.x + width]);
        __syncthreads();
    }
    if (!threadIdx.x) {
        output[outer] = largest[0];
        positive_max(Left ? &diagnostics->maximum_a_scale : &diagnostics->maximum_b_scale, largest[0]);
    }
}

template<class T>
__device__ T quantize(float value, float maximum, CudaMatrixDiagnostics* diagnostics) {
    const float normalized = maximum > 0 && ::isfinite(value) ? value / maximum : 0;
    if constexpr (std::is_same_v<T, __half>) {
        return __float2half_rn(normalized);
    } else {
        const int rounded_value = __float2int_rn(normalized * 127.f);
        const int clipped = max(-127, min(127, rounded_value));
        if (rounded_value != clipped)
            atomicAdd(reinterpret_cast<unsigned long long*>(&diagnostics->saturated_values), 1ull);
        return static_cast<std::int8_t>(clipped);
    }
}

template<class T, bool Left>
__global__ void pack(const c10::complex<float>* input, const float* scale, T* output,
                    std::size_t rows, std::size_t columns, std::size_t inner,
                    std::size_t first, std::size_t count, std::size_t padded_rows,
                    std::size_t padded_columns, std::size_t padded_inner,
                    CudaMatrixDiagnostics* diagnostics) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto elements = Left ? padded_rows * (2 * padded_inner) : (2 * padded_inner) * (2 * padded_columns);
    if (index >= elements) return;
    float value = 0, maximum = 0;
    if constexpr (Left) {
        const auto row = index / (2 * padded_inner), lane = index % (2 * padded_inner);
        const auto k = lane % padded_inner;
        if (row < rows && k < count) {
            const auto source = input[row * inner + first + k];
            value = lane < padded_inner ? source.real() : source.imag();
            maximum = scale[row];
        }
    } else {
        // Store the right operand transposed so the cuBLAS call uses TN,
        // the ordinary-layout tensor-kernel shape for integer products.
        const auto lane = index % (2 * padded_inner), component = index / (2 * padded_inner);
        const auto k = lane % padded_inner, column = component % padded_columns;
        if (k < count && column < columns) {
            const auto source = input[(first + k) * columns + column];
            if (lane < padded_inner) value = component < padded_columns ? source.real() : source.imag();
            else value = component < padded_columns ? -source.imag() : source.real();
            maximum = scale[column];
        }
    }
    output[index] = quantize<T>(value, maximum, diagnostics);
}

template<class T>
__global__ void reconstruct(const T* partial, const float* a_scale, const float* b_scale,
                           c10::complex<double>* output, std::size_t rows,
                           std::size_t columns, std::size_t padded_columns) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= rows * columns) return;
    const auto row = index / columns, column = index % columns;
    double factor = static_cast<double>(a_scale[row]) * static_cast<double>(b_scale[column]);
    if constexpr (std::is_same_v<T, std::int32_t>) factor /= 127. * 127.;
    const auto real = static_cast<double>(partial[row * (2 * padded_columns) + column]) * factor;
    const auto imag = static_cast<double>(partial[row * (2 * padded_columns) + padded_columns + column]) * factor;
    const auto previous = output[index];
    output[index] = {previous.real() + real, previous.imag() + imag};
}


template<class T>
void enqueue_pack(const c10::complex<float>* a, const c10::complex<float>* b,
                  float* a_scale, float* b_scale, void* packed_a, void* packed_b,
                  std::size_t rows, std::size_t columns, std::size_t inner,
                  std::size_t first, std::size_t count, const Layout& plan,
                  CudaMatrixDiagnostics* diagnostics, cudaStream_t stream) {
    pack<T, true><<<blocks(plan.m * (2 * plan.k)), threads, 0, stream>>>(
        a, a_scale, static_cast<T*>(packed_a), rows, columns, inner, first, count,
        plan.m, plan.n, plan.k, diagnostics);
    cuda_check(cudaGetLastError(), "pack A");
    pack<T, false><<<blocks((2 * plan.k) * (2 * plan.n)), threads, 0, stream>>>(
        b, b_scale, static_cast<T*>(packed_b), rows, columns, inner, first, count,
        plan.m, plan.n, plan.k, diagnostics);
    cuda_check(cudaGetLastError(), "pack B");
}

} // namespace

int cuda_matrix_library_version() {
    int version = 0;
    blas_check(cublasGetVersion(nullptr, &version), "loaded library version");
    return version;
}

struct CudaComplexMatrix::Impl {
    cublasHandle_t handle = nullptr;
    int device = 0, major = 0, minor = 0, version = 0;

    ~Impl() {
        if (!handle) return;
        int prior = device;
        const bool have_prior = cudaGetDevice(&prior) == cudaSuccess;
        if (prior != device) (void)cudaSetDevice(device);
        (void)cublasDestroy(handle);
        if (have_prior && prior != device) (void)cudaSetDevice(prior);
    }
};

CudaComplexMatrix::CudaComplexMatrix() : impl_(std::make_unique<Impl>()) {
    cuda_check(cudaGetDevice(&impl_->device), "get device");
    cudaDeviceProp properties{};
    cuda_check(cudaGetDeviceProperties(&properties, impl_->device), "device properties");
    impl_->major = properties.major;
    impl_->minor = properties.minor;
    blas_check(cublasCreate(&impl_->handle), "create handle");
    blas_check(cublasGetVersion(impl_->handle, &impl_->version), "library version");
    blas_check(cublasSetPointerMode(impl_->handle, CUBLAS_POINTER_MODE_HOST), "host scalar mode");
    blas_check(cublasSetMathMode(impl_->handle, static_cast<cublasMath_t>(
        CUBLAS_TENSOR_OP_MATH | CUBLAS_MATH_DISALLOW_REDUCED_PRECISION_REDUCTION)), "tensor math mode");
    blas_check(cublasSetAtomicsMode(impl_->handle, CUBLAS_ATOMICS_NOT_ALLOWED), "deterministic accumulation mode");
}

CudaComplexMatrix::~CudaComplexMatrix() = default;

bool CudaComplexMatrix::supported(CudaMatrixMath math) const noexcept {
    const auto capability = impl_->major * 10 + impl_->minor;
    if (math == CudaMatrixMath::fp16) return capability >= 70;
    if (math == CudaMatrixMath::int8) return capability >= 72;
    return false;
}

bool CudaComplexMatrix::supported_shape(CudaMatrixMath math, std::size_t rows,
                                       std::size_t columns, std::size_t inner,
                                       void* scratch, std::size_t scratch_bytes) {
    const auto shape = layout(math, rows, columns, inner);
    if (!scratch || reinterpret_cast<std::uintptr_t>(scratch) % alignment || scratch_bytes < shape.bytes)
        throw std::invalid_argument("invalid Robust matrix workspace");
    return supported(math);
}

int CudaComplexMatrix::cublas_version() const noexcept { return impl_->version; }
int CudaComplexMatrix::device_major() const noexcept { return impl_->major; }
int CudaComplexMatrix::device_minor() const noexcept { return impl_->minor; }

std::size_t CudaComplexMatrix::workspace_bytes(CudaMatrixMath math, std::size_t rows,
                                             std::size_t columns, std::size_t inner) {
    return layout(math, rows, columns, inner).bytes;
}

std::size_t CudaComplexMatrix::gram_workspace_bytes() noexcept { return library_workspace; }

void CudaComplexMatrix::gram_c128(const c10::complex<double>* input, c10::complex<double>* output,
                                 std::size_t batches, std::size_t taps, std::size_t modes,
                                 void* scratch, std::size_t scratch_bytes, cudaStream_t stream) {
    static_assert(sizeof(c10::complex<double>) == sizeof(cuDoubleComplex));
    if (!input || !output || !scratch || !batches || batches > INT_MAX ||
        !taps || taps > 2 || !modes || modes > INT_MAX ||
        reinterpret_cast<std::uintptr_t>(input) % alignof(cuDoubleComplex) ||
        reinterpret_cast<std::uintptr_t>(output) % alignof(cuDoubleComplex) ||
        reinterpret_cast<std::uintptr_t>(scratch) % alignment || scratch_bytes < library_workspace)
        throw std::invalid_argument("invalid Robust C128 Gram geometry or storage");
    const auto input_stride = mul(taps, modes), output_stride = mul(taps, taps);
    if (input_stride > static_cast<std::uint64_t>(LLONG_MAX) ||
        output_stride > static_cast<std::uint64_t>(LLONG_MAX))
        throw std::invalid_argument("Robust C128 Gram stride exceeds cuBLAS limits");
    const auto input_bytes = mul(mul(batches, input_stride), sizeof(*input));
    const auto output_bytes = mul(mul(batches, output_stride), sizeof(*output));
    const auto address = [](const void* pointer, std::size_t bytes) {
        const auto start = reinterpret_cast<std::uintptr_t>(pointer);
        if (bytes > std::numeric_limits<std::uintptr_t>::max() - start)
            throw std::invalid_argument("Robust C128 Gram address range overflows");
        return start;
    };
    const auto in = address(input, input_bytes), out = address(output, output_bytes);
    const auto work = address(scratch, library_workspace);
    const auto overlap = [](std::uintptr_t a, std::size_t a_bytes, std::uintptr_t b, std::size_t b_bytes) {
        return a < b + b_bytes && b < a + a_bytes;
    };
    if (overlap(in, input_bytes, out, output_bytes) ||
        overlap(in, input_bytes, work, library_workspace) || overlap(out, output_bytes, work, library_workspace))
        throw std::invalid_argument("Robust C128 Gram storage overlaps");
    int current = -1;
    cuda_check(cudaGetDevice(&current), "check Gram device");
    if (current != impl_->device) throw std::invalid_argument("Robust Gram handle belongs to a different CUDA device");
    // cublasSetStream resets workspace selection. Bind the caller's charged
    // workspace afterwards; do not use ATen's process-lifetime BLAS cache.
    blas_check(cublasSetStream(impl_->handle, stream), "set Gram stream");
    blas_check(cublasSetWorkspace(impl_->handle, scratch, library_workspace), "set bounded Gram workspace");
    const cuDoubleComplex alpha{1., 0.}, beta{0., 0.};
    const auto* source = reinterpret_cast<const cuDoubleComplex*>(input);
    // Each input's row-major [tap,mode] storage is column-major [mode,tap].
    // OP_C/OP_N therefore yields conj(row_i) dot row_j in column-major G.
    // The typed ZGEMM retains FP64 products/accumulation regardless of the
    // handle's tensor-op permission for its separate lower-precision work.
    blas_check(cublasZgemmStridedBatched(impl_->handle, CUBLAS_OP_C, CUBLAS_OP_N,
        static_cast<int>(taps), static_cast<int>(taps), static_cast<int>(modes), &alpha,
        source, static_cast<int>(modes), static_cast<long long>(input_stride),
        source, static_cast<int>(modes), static_cast<long long>(input_stride), &beta,
        reinterpret_cast<cuDoubleComplex*>(output), static_cast<int>(taps),
        static_cast<long long>(output_stride), static_cast<int>(batches)), "C128 Gram GEMM");
}

CudaMatrixWork CudaComplexMatrix::launch(CudaMatrixMath math, const c10::complex<float>* a,
                                        const c10::complex<float>* b, c10::complex<double>* c,
                                        std::size_t rows, std::size_t columns, std::size_t inner,
                                        void* scratch, std::size_t scratch_bytes,
                                        CudaMatrixDiagnostics* diagnostics, cudaStream_t stream) {
    const auto plan = layout(math, rows, columns, inner);
    if (!a || !b || !c || !scratch || !diagnostics ||
        reinterpret_cast<std::uintptr_t>(scratch) % alignment || scratch_bytes < plan.bytes)
        throw std::invalid_argument("invalid Robust matrix storage or workspace");
    if (!supported_shape(math, rows, columns, inner, scratch, scratch_bytes))
        throw std::runtime_error("requested Robust matrix arithmetic or shape is unsupported on this CUDA device/library");
    int current = -1;
    cuda_check(cudaGetDevice(&current), "check current device");
    if (current != impl_->device) throw std::invalid_argument("Robust matrix handle belongs to a different CUDA device");
    // Setting a stream resets cuBLAS's workspace selection: bind the charged
    // workspace afterwards, every time, including when reusing the stream.
    blas_check(cublasSetStream(impl_->handle, stream), "set stream");
    blas_check(cublasSetWorkspace(impl_->handle, scratch, library_workspace), "set bounded workspace");
    auto* base = static_cast<unsigned char*>(scratch);
    auto* a_scale = reinterpret_cast<float*>(base + plan.a_scale);
    auto* b_scale = reinterpret_cast<float*>(base + plan.b_scale);
    auto* packed_a = base + plan.a;
    auto* packed_b = base + plan.b;
    auto* partial = base + plan.partial;
    cuda_check(cudaMemsetAsync(c, 0, mul(mul(rows, columns), sizeof(*c)), stream), "clear output");
    CudaMatrixWork work;
    for (std::size_t first = 0; first < inner; first += std::min(complex_chunk, inner - first)) {
        const auto count = std::min(complex_chunk, inner - first);
        scales<true><<<static_cast<unsigned>(plan.m), threads, 0, stream>>>(
            a, a_scale, rows, columns, inner, first, count, diagnostics);
        cuda_check(cudaGetLastError(), "A scales");
        scales<false><<<static_cast<unsigned>(plan.n), threads, 0, stream>>>(
            b, b_scale, rows, columns, inner, first, count, diagnostics);
        cuda_check(cudaGetLastError(), "B scales");
        // Row-major C=A*B is column-major C^T=B^T*A^T. The packed right
        // operand is transposed in memory to expose the TN tensor-kernel
        // layout. Complex multiplication uses [Ar Ai] * [[Br Bi],[-Bi Br]],
        // with complete padded zeros.
        if (math == CudaMatrixMath::fp16) {
            enqueue_pack<__half>(a, b, a_scale, b_scale, packed_a, packed_b,
                                rows, columns, inner, first, count, plan, diagnostics, stream);
            const float alpha = 1, beta = 0;
            blas_check(cublasGemmEx(impl_->handle, CUBLAS_OP_T, CUBLAS_OP_N,
                static_cast<int>(2 * plan.n), static_cast<int>(plan.m), static_cast<int>(2 * plan.k),
                &alpha, packed_b, CUDA_R_16F, static_cast<int>(2 * plan.k),
                packed_a, CUDA_R_16F, static_cast<int>(2 * plan.k), &beta,
                partial, CUDA_R_32F, static_cast<int>(2 * plan.n),
                CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT_TENSOR_OP), "FP16 GEMM");
            reconstruct<float><<<blocks(rows * columns), threads, 0, stream>>>(
                reinterpret_cast<float*>(partial), a_scale, b_scale, c, rows, columns, plan.n);
        } else {
            enqueue_pack<std::int8_t>(a, b, a_scale, b_scale, packed_a, packed_b,
                                     rows, columns, inner, first, count, plan, diagnostics, stream);
            const std::int32_t alpha = 1, beta = 0;
            blas_check(cublasGemmEx(impl_->handle, CUBLAS_OP_T, CUBLAS_OP_N,
                static_cast<int>(2 * plan.n), static_cast<int>(plan.m), static_cast<int>(2 * plan.k),
                &alpha, packed_b, CUDA_R_8I, static_cast<int>(2 * plan.k),
                packed_a, CUDA_R_8I, static_cast<int>(2 * plan.k), &beta,
                partial, CUDA_R_32I, static_cast<int>(2 * plan.n),
                CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT_TENSOR_OP), "INT8 GEMM");
            // Each INT32 partial has at most 1024 terms of magnitude <=127^2:
            // 16,516,096 < 2^24. Integer accumulation cannot overflow, and even
            // conversion through FP32 would be exact. We reconstruct directly
            // in FP64 and accumulate chunks there, never in INT32.
            reconstruct<std::int32_t><<<blocks(rows * columns), threads, 0, stream>>>(
                reinterpret_cast<std::int32_t*>(partial), a_scale, b_scale, c, rows, columns, plan.n);
        }
        cuda_check(cudaGetLastError(), "reconstruct complex output");
        ++work.gemm_calls;
        ++work.inner_chunks;
        work.padded_real_macs += mul(mul(plan.m, 2 * plan.n), 2 * plan.k);
        work.packed_values += add(mul(plan.m, 2 * plan.k), mul(2 * plan.k, 2 * plan.n));
    }
    return work;
}

} // namespace datapump::triad::v3::receiver_detail
