#include "triad_tensor_matrix.hpp"

#include <cublasLt.h>
#include <cublas_v2.h>
#include <cuComplex.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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
    if (status == CUBLAS_STATUS_SUCCESS) return;
    const auto message = std::string("Robust matrix ") + operation +
        " failed with cuBLAS status " + std::to_string(static_cast<int>(status));
    if (status == CUBLAS_STATUS_ALLOC_FAILED) throw CudaMatrixAllocationError(message);
    throw std::runtime_error(message);
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
    std::size_t a_scale, a, partial, bytes;
};

struct MatrixTypes {
    cudaDataType_t input, output;
    cublasComputeType_t compute;
    std::uint64_t tensor_flags;
};

MatrixTypes matrix_types(CudaMatrixMath math) {
    if (math == CudaMatrixMath::fp16)
        return {CUDA_R_16F, CUDA_R_32F, CUBLAS_COMPUTE_32F,
                CUBLASLT_NUMERICAL_IMPL_FLAGS_HMMA | CUBLASLT_NUMERICAL_IMPL_FLAGS_INPUT_16F |
                CUBLASLT_NUMERICAL_IMPL_FLAGS_ACCUMULATOR_32F};
    return {CUDA_R_8I, CUDA_R_32I, CUBLAS_COMPUTE_32I,
            CUBLASLT_NUMERICAL_IMPL_FLAGS_IMMA | CUBLASLT_NUMERICAL_IMPL_FLAGS_INPUT_8I |
            CUBLASLT_NUMERICAL_IMPL_FLAGS_ACCUMULATOR_32I};
}

bool lt_unavailable(cublasStatus_t status) {
    return status == CUBLAS_STATUS_NOT_SUPPORTED || status == CUBLAS_STATUS_ARCH_MISMATCH;
}

struct LtMatrixPlan {
    CudaMatrixMath math{};
    std::size_t m = 0, n = 0, k = 0;
    cublasLtMatmulDesc_t operation = nullptr;
    cublasLtMatrixLayout_t right = nullptr, left = nullptr, output = nullptr;
    cublasLtMatmulAlgo_t algorithm{};
    bool selected = false;

    ~LtMatrixPlan() {
        if (output) (void)cublasLtMatrixLayoutDestroy(output);
        if (left) (void)cublasLtMatrixLayoutDestroy(left);
        if (right) (void)cublasLtMatrixLayoutDestroy(right);
        if (operation) (void)cublasLtMatmulDescDestroy(operation);
    }

    bool matches(CudaMatrixMath wanted, const Layout& shape) const {
        return math == wanted && m == shape.m && n == shape.n && k == shape.k;
    }

    void prepare(cublasLtHandle_t handle, CudaMatrixMath wanted, const Layout& shape, const MatrixTypes& types) {
        math = wanted;
        m = shape.m;
        n = shape.n;
        k = shape.k;
        blas_check(cublasLtMatmulDescCreate(&operation, types.compute, types.output), "create Lt operation");
        const cublasOperation_t transpose = CUBLAS_OP_T;
        blas_check(cublasLtMatmulDescSetAttribute(operation, CUBLASLT_MATMUL_DESC_TRANSA,
            &transpose, sizeof(transpose)), "set Lt transpose");
        blas_check(cublasLtMatrixLayoutCreate(&right, types.input, 2 * k, 2 * n, 2 * k), "create Lt right layout");
        blas_check(cublasLtMatrixLayoutCreate(&left, types.input, 2 * k, m, 2 * k), "create Lt left layout");
        blas_check(cublasLtMatrixLayoutCreate(&output, types.output, 2 * n, m, 2 * n), "create Lt output layout");

        cublasLtMatmulPreference_t preference = nullptr;
        blas_check(cublasLtMatmulPreferenceCreate(&preference), "create Lt preferences");
        const std::unique_ptr<std::remove_pointer_t<cublasLtMatmulPreference_t>, decltype(&cublasLtMatmulPreferenceDestroy)>
            owner(preference, cublasLtMatmulPreferenceDestroy);
        const std::uint64_t bytes = library_workspace;
        blas_check(cublasLtMatmulPreferenceSetAttribute(preference, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES,
            &bytes, sizeof(bytes)), "bound Lt workspace");
        constexpr std::uint64_t checked_flags = CUBLASLT_NUMERICAL_IMPL_FLAGS_OP_TYPE_MASK |
            CUBLASLT_NUMERICAL_IMPL_FLAGS_OP_INPUT_TYPE_MASK | CUBLASLT_NUMERICAL_IMPL_FLAGS_ACCUMULATOR_TYPE_MASK;
        const auto allowed_flags = ~checked_flags | types.tensor_flags;
        blas_check(cublasLtMatmulPreferenceSetAttribute(preference, CUBLASLT_MATMUL_PREF_IMPL_MASK,
            &allowed_flags, sizeof(allowed_flags)), "request Lt tensor implementation");

        std::array<cublasLtMatmulHeuristicResult_t, 8> candidates{};
        int found = 0;
        const auto status = cublasLtMatmulAlgoGetHeuristic(handle, operation, right, left, output, output,
            preference, static_cast<int>(candidates.size()), candidates.data(), &found);
        if (lt_unavailable(status)) return;
        blas_check(status, "select Lt algorithm");
        for (int i = 0; i < found; ++i) {
            const auto& candidate = candidates[static_cast<std::size_t>(i)];
            if (candidate.state != CUBLAS_STATUS_SUCCESS || candidate.workspaceSize > bytes) continue;
            std::uint64_t flags = 0;
            blas_check(cublasLtMatmulAlgoCapGetAttribute(&candidate.algo, CUBLASLT_ALGO_CAP_NUMERICAL_IMPL_FLAGS,
                &flags, sizeof(flags), nullptr), "read Lt numerical implementation");
            if ((flags & checked_flags) != types.tensor_flags) continue;
            algorithm = candidate.algo;
            selected = true;
            break;
        }
    }

    bool enqueue(cublasLtHandle_t handle, const void* alpha, const void* beta, const void* packed_right,
                 const void* packed_left, void* partial, void* scratch, cudaStream_t stream) {
        if (!selected) return false;
        const auto status = cublasLtMatmul(handle, operation, alpha, packed_right, right, packed_left, left,
            beta, partial, output, partial, output, &algorithm, scratch, library_workspace, stream);
        if (lt_unavailable(status)) {
            selected = false;
            return false;
        }
        blas_check(status, "execute Lt tensor product");
        return true;
    }
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
    out.a = reserve(mul(mul(out.m, 2 * out.k), scalar_bytes));
    out.partial = reserve(mul(mul(out.m, 2 * out.n), sizeof(std::int32_t)));
    out.bytes = cursor;
    // Validate the output and operation counters as well as scratch storage.
    (void)mul(mul(rows, columns), sizeof(c10::complex<double>));
    (void)mul(mul(mul(out.m, 2 * out.n), 2 * out.k), (inner - 1) / complex_chunk + 1);
    return out;
}

struct RightLayout {
    std::size_t n, k, chunks, values, packed_offset, chunk_bytes, bytes;
};

RightLayout right_layout(CudaMatrixMath math, std::size_t columns, std::size_t inner) {
    const auto dims = layout(math, 1, columns, inner);
    const auto chunks = (inner - 1) / complex_chunk + 1;
    const auto values = mul(2 * dims.k, 2 * dims.n);
    const auto packed = rounded(mul(dims.n, sizeof(float)), alignment);
    const auto scalar_bytes = math == CudaMatrixMath::fp16 ? sizeof(__half) : sizeof(std::int8_t);
    const auto chunk_bytes = rounded(add(packed, mul(values, scalar_bytes)), alignment);
    (void)mul(values, chunks);
    return {dims.n, dims.k, chunks, values, packed, chunk_bytes, mul(chunks, chunk_bytes)};
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

__global__ void left_scales(const c10::complex<float>* input, float* output,
                       std::size_t rows, std::size_t inner,
                       std::size_t first, std::size_t count, CudaMatrixDiagnostics* diagnostics) {
    __shared__ float largest[threads];
    const auto outer = static_cast<std::size_t>(blockIdx.x);
    float maximum = 0;
    unsigned long long invalid = 0;
    if (outer < rows) {
        for (auto k = static_cast<std::size_t>(threadIdx.x); k < count; k += blockDim.x) {
            const auto value = input[outer * inner + first + k];
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
        positive_max(&diagnostics->maximum_a_scale, largest[0]);
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

template<class T>
__global__ void pack_left(const c10::complex<float>* input, const float* scale, T* output,
                    std::size_t rows, std::size_t inner,
                    std::size_t first, std::size_t count, std::size_t padded_rows,
                    std::size_t padded_inner,
                    CudaMatrixDiagnostics* diagnostics) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= padded_rows * (2 * padded_inner)) return;
    float value = 0, maximum = 0;
    const auto row = index / (2 * padded_inner), lane = index % (2 * padded_inner);
    const auto k = lane % padded_inner;
    if (row < rows && k < count) {
        const auto source = input[row * inner + first + k];
        value = lane < padded_inner ? source.real() : source.imag();
        maximum = scale[row];
    }
    output[index] = quantize<T>(value, maximum, diagnostics);
}

template<class T>
__global__ void pack_right(const c10::complex<float>* phase, const c10::complex<float>* convolution,
                          std::size_t convolution_rows, std::size_t columns, std::size_t inner,
                          unsigned char* output, RightLayout plan, CudaMatrixDiagnostics* diagnostics) {
    __shared__ float largest[threads];
    const auto column = static_cast<std::size_t>(blockIdx.x);
    const auto chunk = static_cast<std::size_t>(blockIdx.y);
    const auto first = chunk * complex_chunk;
    auto* base = output + chunk * plan.chunk_bytes;
    auto* packed = reinterpret_cast<T*>(base + plan.packed_offset);
    float real[complex_chunk / threads]{}, imag[complex_chunk / threads]{};
    float maximum = 0;
    unsigned long long invalid = 0;
    for (unsigned i = 0; i < complex_chunk / threads; ++i) {
        const auto row = first + threadIdx.x + i * threads;
        if (column < columns && row < inner) {
            const auto index = column >= row ? column - row : convolution_rows - (row - column);
            const auto a = phase[row], b = convolution[index];
            real[i] = a.real() * b.real() - a.imag() * b.imag();
            imag[i] = a.real() * b.imag() + a.imag() * b.real();
            if (::isfinite(real[i])) maximum = ::fmaxf(maximum, ::fabsf(real[i]));
            else ++invalid;
            if (::isfinite(imag[i])) maximum = ::fmaxf(maximum, ::fabsf(imag[i]));
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
    maximum = largest[0];
    if (!threadIdx.x) {
        reinterpret_cast<float*>(base)[column] = maximum;
        positive_max(&diagnostics->maximum_b_scale, maximum);
    }
    // Columns of B^T are laid out as [Br,-Bi] and [Bi,Br]. Neighboring
    // threads write adjacent k positions within the two packed columns.
    auto* real_column = packed + column * (2 * plan.k);
    auto* imag_column = packed + (column + plan.n) * (2 * plan.k);
    for (unsigned i = 0; i < complex_chunk / threads; ++i) {
        const auto k = static_cast<std::size_t>(threadIdx.x + i * threads);
        if (k >= plan.k) break;
        const auto qr = quantize<T>(real[i], maximum, diagnostics);
        real_column[k] = qr;
        real_column[plan.k + k] = quantize<T>(-imag[i], maximum, diagnostics);
        imag_column[k] = quantize<T>(imag[i], maximum, diagnostics);
        imag_column[plan.k + k] = qr;
    }
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


} // namespace

int cuda_matrix_library_version() {
    int version = 0;
    blas_check(cublasGetVersion(nullptr, &version), "loaded library version");
    return version;
}

class CudaComplexMatrix::Context {
public:
    Context() {
        cuda_check(cudaGetDevice(&device), "get Lt device");
        cublasLtHandle_t candidate = nullptr;
        const auto status = cublasLtCreate(&candidate);
        if (lt_unavailable(status)) return;
        blas_check(status, "create Lt context");
        handle = candidate;
    }

    ~Context() {
        if (!handle) return;
        int prior = device;
        const bool have_prior = cudaGetDevice(&prior) == cudaSuccess;
        if (prior != device) (void)cudaSetDevice(device);
        // cuBLASLt destruction synchronizes the device. Only the arena's
        // final context release reaches this call, outside slot retirement.
        (void)cublasLtDestroy(handle);
        if (have_prior && prior != device) (void)cudaSetDevice(prior);
    }

    int device = 0;
    cublasLtHandle_t handle = nullptr;
};

struct CudaComplexMatrix::Impl {
    cublasHandle_t handle = nullptr;
    int device = 0, capability = 0;
    std::shared_ptr<Context> context;
    std::array<std::unique_ptr<LtMatrixPlan>, 8> lt_plans;
    std::size_t next_lt_plan = 0;

    cublasHandle_t legacy_handle() {
        if (handle) return handle;
        blas_check(cublasCreate(&handle), "create legacy handle");
        blas_check(cublasSetPointerMode(handle, CUBLAS_POINTER_MODE_HOST), "host scalar mode");
        blas_check(cublasSetMathMode(handle, static_cast<cublasMath_t>(
            CUBLAS_TENSOR_OP_MATH | CUBLAS_MATH_DISALLOW_REDUCED_PRECISION_REDUCTION)), "tensor math mode");
        blas_check(cublasSetAtomicsMode(handle, CUBLAS_ATOMICS_NOT_ALLOWED), "deterministic accumulation mode");
        return handle;
    }

    LtMatrixPlan* lt_plan(CudaMatrixMath math, const Layout& shape, const MatrixTypes& types) {
        if (!context || !context->handle) return nullptr;
        for (const auto& plan : lt_plans)
            if (plan && plan->matches(math, shape)) return plan.get();
        auto& slot = lt_plans[next_lt_plan];
        auto plan = std::make_unique<LtMatrixPlan>();
        plan->prepare(context->handle, math, shape, types);
        slot = std::move(plan);
        next_lt_plan = (next_lt_plan + 1) % lt_plans.size();
        return slot.get();
    }

    ~Impl() {
        if (!handle) return;
        int prior = device;
        const bool have_prior = cudaGetDevice(&prior) == cudaSuccess;
        if (prior != device) (void)cudaSetDevice(device);
        (void)cublasDestroy(handle);
        if (have_prior && prior != device) (void)cudaSetDevice(prior);
    }
};

std::shared_ptr<CudaComplexMatrix::Context> CudaComplexMatrix::make_context() {
    return std::make_shared<Context>();
}

CudaComplexMatrix::CudaComplexMatrix(std::shared_ptr<Context> context) : impl_(std::make_unique<Impl>()) {
    cuda_check(cudaGetDevice(&impl_->device), "get device");
    if (context && context->device != impl_->device)
        throw std::invalid_argument("Robust matrix context belongs to a different CUDA device");
    impl_->context = std::move(context);
    cudaDeviceProp properties{};
    cuda_check(cudaGetDeviceProperties(&properties, impl_->device), "device properties");
    impl_->capability = properties.major * 10 + properties.minor;
}

CudaComplexMatrix::~CudaComplexMatrix() = default;

bool CudaComplexMatrix::supported(CudaMatrixMath math) const noexcept {
    if (math == CudaMatrixMath::fp16) return impl_->capability >= 70;
    if (math == CudaMatrixMath::int8) return impl_->capability >= 72;
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

bool CudaComplexMatrix::has_tensor_algorithm(CudaMatrixMath math, std::size_t rows, std::size_t columns,
                                             std::size_t inner) {
    if (!supported(math)) return false;
    int current = -1;
    cuda_check(cudaGetDevice(&current), "check tensor algorithm device");
    if (current != impl_->device) throw std::invalid_argument("Robust matrix handle belongs to a different CUDA device");
    const auto* plan = impl_->lt_plan(math, layout(math, rows, columns, inner), matrix_types(math));
    return plan && plan->selected;
}

std::size_t CudaComplexMatrix::workspace_bytes(CudaMatrixMath math, std::size_t rows,
                                             std::size_t columns, std::size_t inner) {
    return layout(math, rows, columns, inner).bytes;
}

std::size_t CudaComplexMatrix::prepared_right_bytes(CudaMatrixMath math, std::size_t columns, std::size_t inner) {
    return right_layout(math, columns, inner).bytes;
}

std::size_t CudaComplexMatrix::padded_real_macs_per_row(std::size_t columns, std::size_t inner) {
    const auto right = right_layout(CudaMatrixMath::int8, columns, inner);
    return mul(right.values, right.chunks);
}

std::uint64_t CudaComplexMatrix::prepare_right(CudaMatrixMath math, const c10::complex<float>* phase,
                                             const c10::complex<float>* convolution, std::size_t convolution_rows,
                                             std::size_t columns, std::size_t inner,
                                             void* prepared, std::size_t prepared_bytes,
                                             CudaMatrixDiagnostics* diagnostics, cudaStream_t stream) {
    const auto plan = right_layout(math, columns, inner);
    if (!phase || !convolution || !prepared || !diagnostics ||
        convolution_rows < add(inner, columns - 1) || plan.chunks > 65535 ||
        reinterpret_cast<std::uintptr_t>(prepared) % alignment || prepared_bytes < plan.bytes || !supported(math))
        throw std::invalid_argument("invalid Robust prepared matrix geometry or storage");
    int current = -1;
    cuda_check(cudaGetDevice(&current), "check prepared matrix device");
    if (current != impl_->device) throw std::invalid_argument("Robust matrix handle belongs to a different CUDA device");
    const dim3 grid(static_cast<unsigned>(plan.n), static_cast<unsigned>(plan.chunks));
    auto* output = static_cast<unsigned char*>(prepared);
    if (math == CudaMatrixMath::fp16)
        pack_right<__half><<<grid, threads, 0, stream>>>(phase, convolution, convolution_rows, columns,
            inner, output, plan, diagnostics);
    else
        pack_right<std::int8_t><<<grid, threads, 0, stream>>>(phase, convolution, convolution_rows, columns,
            inner, output, plan, diagnostics);
    cuda_check(cudaGetLastError(), "prepare packed right matrix");
    return mul(plan.values, plan.chunks);
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
    const auto handle = impl_->legacy_handle();
    blas_check(cublasSetStream(handle, stream), "set Gram stream");
    blas_check(cublasSetWorkspace(handle, scratch, library_workspace), "set bounded Gram workspace");
    const cuDoubleComplex alpha{1., 0.}, beta{0., 0.};
    const auto* source = reinterpret_cast<const cuDoubleComplex*>(input);
    // Each input's row-major [tap,mode] storage is column-major [mode,tap].
    // OP_C/OP_N therefore yields conj(row_i) dot row_j in column-major G.
    // The typed ZGEMM retains FP64 products/accumulation regardless of the
    // handle's tensor-op permission for its separate lower-precision work.
    blas_check(cublasZgemmStridedBatched(handle, CUBLAS_OP_C, CUBLAS_OP_N,
        static_cast<int>(taps), static_cast<int>(taps), static_cast<int>(modes), &alpha,
        source, static_cast<int>(modes), static_cast<long long>(input_stride),
        source, static_cast<int>(modes), static_cast<long long>(input_stride), &beta,
        reinterpret_cast<cuDoubleComplex*>(output), static_cast<int>(taps),
        static_cast<long long>(output_stride), static_cast<int>(batches)), "C128 Gram GEMM");
}

CudaMatrixWork CudaComplexMatrix::launch(CudaMatrixMath math, const c10::complex<float>* a,
                                        const void* prepared_right, std::size_t prepared_bytes,
                                        c10::complex<double>* c,
                                        std::size_t rows, std::size_t columns, std::size_t inner,
                                        void* scratch, std::size_t scratch_bytes,
                                        CudaMatrixDiagnostics* diagnostics, cudaStream_t stream) {
    const auto plan = layout(math, rows, columns, inner);
    const auto right = right_layout(math, columns, inner);
    if (!a || !prepared_right || !c || !scratch || !diagnostics ||
        reinterpret_cast<std::uintptr_t>(scratch) % alignment || scratch_bytes < plan.bytes ||
        reinterpret_cast<std::uintptr_t>(prepared_right) % alignment || prepared_bytes < right.bytes)
        throw std::invalid_argument("invalid Robust matrix storage or workspace");
    if (!supported_shape(math, rows, columns, inner, scratch, scratch_bytes))
        throw std::runtime_error("requested Robust matrix arithmetic or shape is unsupported on this CUDA device/library");
    int current = -1;
    cuda_check(cudaGetDevice(&current), "check current device");
    if (current != impl_->device) throw std::invalid_argument("Robust matrix handle belongs to a different CUDA device");
    const auto types = matrix_types(math);
    LtMatrixPlan* lt = nullptr;
    try {
        lt = impl_->lt_plan(math, plan, types);
    } catch (const CudaMatrixAllocationError&) {
        // The plan was not cached. This shape can retry later and use the
        // same-arithmetic legacy GEMM for the current rectangle.
    }
    cublasHandle_t legacy = nullptr;
    const bool half = math == CudaMatrixMath::fp16;
    const float float_alpha = 1, float_beta = 0;
    const std::int32_t int_alpha = 1, int_beta = 0;
    const void* alpha = half ? static_cast<const void*>(&float_alpha) : &int_alpha;
    const void* beta = half ? static_cast<const void*>(&float_beta) : &int_beta;
    auto* base = static_cast<unsigned char*>(scratch);
    auto* a_scale = reinterpret_cast<float*>(base + plan.a_scale);
    auto* packed_a = base + plan.a;
    auto* partial = base + plan.partial;
    const auto* packed = static_cast<const unsigned char*>(prepared_right);
    cuda_check(cudaMemsetAsync(c, 0, mul(mul(rows, columns), sizeof(*c)), stream), "clear output");
    CudaMatrixWork work;
    for (std::size_t chunk = 0; chunk < right.chunks; ++chunk) {
        const auto first = chunk * complex_chunk;
        const auto count = std::min(complex_chunk, inner - first);
        left_scales<<<static_cast<unsigned>(plan.m), threads, 0, stream>>>(
            a, a_scale, rows, inner, first, count, diagnostics);
        cuda_check(cudaGetLastError(), "A scales");
        const auto* right_chunk = packed + chunk * right.chunk_bytes;
        const auto* b_scale = reinterpret_cast<const float*>(right_chunk);
        const auto* packed_b = right_chunk + right.packed_offset;
        // Row-major C=A*B is column-major C^T=B^T*A^T. The packed right
        // operand is transposed in memory to expose the TN tensor-kernel
        // layout. Complex multiplication uses [Ar Ai] * [[Br Bi],[-Bi Br]],
        // with complete padded zeros.
        if (half)
            pack_left<__half><<<blocks(plan.m * (2 * plan.k)), threads, 0, stream>>>(
                a, a_scale, reinterpret_cast<__half*>(packed_a), rows, inner, first, count, plan.m, plan.k, diagnostics);
        else
            pack_left<std::int8_t><<<blocks(plan.m * (2 * plan.k)), threads, 0, stream>>>(
                a, a_scale, reinterpret_cast<std::int8_t*>(packed_a), rows, inner, first, count, plan.m, plan.k, diagnostics);
        cuda_check(cudaGetLastError(), "pack A");
        if (lt && lt->enqueue(impl_->context->handle, alpha, beta, packed_b, packed_a, partial, scratch, stream)) {
            ++work.lt_tensor_calls;
        } else {
            if (!legacy) {
                legacy = impl_->legacy_handle();
                // Setting a stream resets the legacy workspace selection.
                blas_check(cublasSetStream(legacy, stream), "set stream");
                blas_check(cublasSetWorkspace(legacy, scratch, library_workspace), "set bounded workspace");
            }
            blas_check(cublasGemmEx(legacy, CUBLAS_OP_T, CUBLAS_OP_N,
                static_cast<int>(2 * plan.n), static_cast<int>(plan.m), static_cast<int>(2 * plan.k),
                alpha, packed_b, types.input, static_cast<int>(2 * plan.k),
                packed_a, types.input, static_cast<int>(2 * plan.k), beta,
                partial, types.output, static_cast<int>(2 * plan.n),
                types.compute, CUBLAS_GEMM_DEFAULT_TENSOR_OP), "legacy GEMM");
        }
        if (half) {
            reconstruct<float><<<blocks(rows * columns), threads, 0, stream>>>(
                reinterpret_cast<float*>(partial), a_scale, b_scale, c, rows, columns, plan.n);
        } else {
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
        work.packed_values += mul(plan.m, 2 * plan.k);
    }
    return work;
}

} // namespace datapump::triad::v3::receiver_detail
