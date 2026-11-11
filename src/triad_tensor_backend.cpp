#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_tensor_mapping.hpp"

#include <ATen/ATen.h>
#include <ATen/Parallel.h>
#include <c10/core/InferenceMode.h>
#include <torch/version.h>

#if DATAPUMP_TRIAD_TENSOR_CUDA
#include "triad_tensor_fft.hpp"
#include "triad_tensor_mapping.cuh"
#include "triad_tensor_matrix.hpp"
#include "triad_tensor_peaks.hpp"

#include <ATen/detail/CUDAHooksInterface.h>
#include <c10/cuda/CUDACachingAllocator.h>
#include <c10/cuda/CUDAException.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#include <cuda_runtime_api.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <numeric>
#include <type_traits>
#include <utility>

namespace datapump::triad::v3 {
namespace receiver_detail {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t mib = 1024 * 1024;
constexpr std::size_t tile_samples = 8192;
constexpr std::size_t maximum_pins = 32;
constexpr std::size_t mapping_rows = 16'384;
constexpr std::size_t maximum_states = 512;
constexpr std::size_t maximum_batch = 8;
constexpr std::size_t maximum_direct_gram_modes = 128;
constexpr std::size_t maximum_upload_bytes = 16 * mib;
constexpr std::size_t maximum_matrix_kernel_rows = 8192;
constexpr std::size_t maximum_matrix_frequencies = 256;
constexpr std::size_t maximum_matrix_bytes = 32 * mib;
constexpr std::size_t maximum_matrix_workspaces = 32;
constexpr std::size_t maximum_matrix_arrivals = 64;
constexpr std::size_t minimum_auto_matrix_workspaces = 26;
constexpr std::size_t minimum_auto_matrix_rows = minimum_auto_matrix_workspaces * maximum_matrix_arrivals;
constexpr auto tau = 2 * std::numbers::pi;
constexpr std::string_view small_fine_fallback = "small-chronological-blocks";
enum class HalfFftFallback : std::size_t {
    cpu_device, fp64_stage, lag_group_route, unsupported_plan, workspace_limit,
    fp32_minimum, unqualified_hardware, unqualified_shape, ill_conditioned_template,
    plan_allocation, allocator_limit
};
constexpr std::array<std::string_view, bulk_half_fft_fallback_reasons.size()> half_fft_precision_reasons{
    "cuda-fp16-fft-cpu-device", "cuda-fp16-fft-fp64-stage", "cuda-fp16-fft-lag-group-route",
    "cuda-fp16-fft-unsupported-plan", "cuda-fp16-fft-workspace-limit", "cuda-fp16-fft-fp32-minimum",
    "cuda-fp16-fft-unqualified-hardware", "cuda-fp16-fft-unqualified-shape", "cuda-fp16-fft-ill-conditioned-template",
    "cuda-fp16-fft-plan-allocation", "cuda-fp16-fft-allocator-limit"
};

constexpr std::size_t fine_result_values(std::size_t taps) {
    return 2 * taps * (1 + taps) + 1; // Correlations and Gram for each bit, then the basis witness.
}

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

void cancelled(std::stop_token stop) {
    require(!stop.stop_requested(), "Robust tensor operation cancelled");
}

std::size_t add(std::size_t a, std::size_t b) {
    if (b > SIZE_MAX - a) throw ReceiverWorkspaceError("Robust tensor workspace extent overflow");
    return a + b;
}

std::size_t mul(std::size_t a, std::size_t b) {
    if (a && b > SIZE_MAX / a) throw ReceiverWorkspaceError("Robust tensor workspace extent overflow");
    return a * b;
}

std::int64_t dim(std::size_t value) {
    require(value <= static_cast<std::uint64_t>(INT64_MAX), "Robust tensor index extent overflow");
    return static_cast<std::int64_t>(value);
}

std::size_t ceil_div(std::size_t n, std::size_t d) {
    return n / d + (n % d != 0);
}

void count(std::uint64_t& destination, std::uint64_t increment) noexcept {
    destination += std::min(increment, UINT64_MAX - destination);
}

const char* coarse_math_name(TensorCoarseMath math) {
    switch (math) {
    case TensorCoarseMath::fft: return "fft";
    case TensorCoarseMath::fp16: return "fp16";
    case TensorCoarseMath::int8: return "int8";
    case TensorCoarseMath::fp16_fft: return "fp16-fft";
    }
    throw Error("invalid Robust coarse tensor arithmetic policy");
}

bool requests_matrix(TensorCoarseMath math) noexcept {
    return math == TensorCoarseMath::fp16 || math == TensorCoarseMath::int8;
}

bool automatic_half_fft_hardware(const BulkReceipt& identity) noexcept {
    const auto capability = 10 * identity.cuda_compute_major + identity.cuda_compute_minor;
    return capability == 89 || capability == 120;
}

void record_fft(std::size_t transforms, std::size_t rows, bool inverse,
                BulkReceipt& work, CoarseBulkWork* coarse, std::size_t padded_transforms = 0) {
    const auto total_rows = mul(transforms, rows);
    count(work.fft_calls, 1);
    count(work.fft_rows, total_rows);
    work.executed = true;
    if (coarse) {
        const auto public_transforms = transforms - padded_transforms;
        const auto public_rows = mul(public_transforms, rows);
        count(inverse ? coarse->inverse_ffts : coarse->forward_ffts, public_transforms);
        count(coarse->fft_rows, public_rows);
        count(coarse->fft_row_stages, mul(public_rows, std::bit_width(rows - 1)));
    }
}

void add_work(BulkReceipt& to, const BulkReceipt& from) noexcept {
#define TRIAD_COUNT(field) count(to.field, from.field)
    TRIAD_COUNT(upload_bytes);
    TRIAD_COUNT(download_bytes);
    TRIAD_COUNT(synchronizations);
    TRIAD_COUNT(fft_calls);
    TRIAD_COUNT(fft_rows);
    TRIAD_COUNT(fir_input_rows);
    TRIAD_COUNT(fir_output_contributions);
    TRIAD_COUNT(score_rows);
    TRIAD_COUNT(cuda_arrival_peak_rows);
    TRIAD_COUNT(cuda_arrival_peak_values);
    TRIAD_COUNT(cuda_half_fft_forward_ffts);
    TRIAD_COUNT(cuda_half_fft_inverse_ffts);
    TRIAD_COUNT(cuda_half_fft_padded_ffts);
    TRIAD_COUNT(cuda_half_fft_score_rows);
    TRIAD_COUNT(statistics_blocks);
    TRIAD_COUNT(native_fine_sessions);
    TRIAD_COUNT(projected_values);
    TRIAD_COUNT(dc_reduction_values);
    TRIAD_COUNT(mapped_columns);
    TRIAD_COUNT(mapped_values);
    TRIAD_COUNT(cuda_fused_mapped_launches);
    TRIAD_COUNT(cuda_fused_mapped_values);
    TRIAD_COUNT(cuda_direct_gram_values);
    TRIAD_COUNT(cuda_bmm_gram_values);
    TRIAD_COUNT(cuda_matrix_fp16_calls);
    TRIAD_COUNT(cuda_matrix_int8_calls);
    TRIAD_COUNT(cuda_matrix_lt_hmma_calls);
    TRIAD_COUNT(cuda_matrix_lt_imma_calls);
    TRIAD_COUNT(cuda_matrix_unwitnessed_calls);
    TRIAD_COUNT(cuda_matrix_inner_chunks);
    TRIAD_COUNT(cuda_matrix_padded_real_macs);
    TRIAD_COUNT(cuda_matrix_packed_values);
    TRIAD_COUNT(cuda_matrix_prepared_b_groups);
    TRIAD_COUNT(cuda_matrix_prepared_b_bytes);
    TRIAD_COUNT(cuda_matrix_prepared_b_values);
    TRIAD_COUNT(cuda_matrix_prepared_b_uses);
    TRIAD_COUNT(cuda_matrix_output_values);
    TRIAD_COUNT(cuda_matrix_nonfinite_values);
    TRIAD_COUNT(cuda_matrix_saturated_values);
    TRIAD_COUNT(projection_index_cache_hits);
    TRIAD_COUNT(projection_index_cache_uploads);
    TRIAD_COUNT(mapping_offset_cache_hits);
    TRIAD_COUNT(mapping_offset_cache_uploads);
    TRIAD_COUNT(uploaded_tiles);
    TRIAD_COUNT(tile_hits);
    TRIAD_COUNT(direct_columns);
    TRIAD_COUNT(direct_values);
#undef TRIAD_COUNT
    for (std::size_t i = 0; i < to.direct_fallback_counts.size(); ++i)
        count(to.direct_fallback_counts[i], from.direct_fallback_counts[i]);
    for (std::size_t i = 0; i < to.matrix_fft_fallback_counts.size(); ++i)
        count(to.matrix_fft_fallback_counts[i], from.matrix_fft_fallback_counts[i]);
    for (std::size_t i = 0; i < to.half_fft_fallback_counts.size(); ++i)
        count(to.half_fft_fallback_counts[i], from.half_fft_fallback_counts[i]);
    to.cuda_half_fft_peak_bank_bytes = std::max(to.cuda_half_fft_peak_bank_bytes, from.cuda_half_fft_peak_bank_bytes);
    to.cuda_half_fft_peak_workspace_bytes = std::max(to.cuda_half_fft_peak_workspace_bytes, from.cuda_half_fft_peak_workspace_bytes);
    to.cuda_half_fft_peak_phase_bytes = std::max(to.cuda_half_fft_peak_phase_bytes, from.cuda_half_fft_peak_phase_bytes);
    to.cuda_half_fft_plan_seconds += from.cuda_half_fft_plan_seconds;
    to.cuda_matrix_maximum_a_scale = std::max(to.cuda_matrix_maximum_a_scale, from.cuda_matrix_maximum_a_scale);
    to.cuda_matrix_maximum_b_scale = std::max(to.cuda_matrix_maximum_b_scale, from.cuda_matrix_maximum_b_scale);
    to.cuda_matrix_maximum_rows = std::max(to.cuda_matrix_maximum_rows, from.cuda_matrix_maximum_rows);
    to.cuda_matrix_maximum_columns = std::max(to.cuda_matrix_maximum_columns, from.cuda_matrix_maximum_columns);
    to.cuda_matrix_maximum_inner = std::max(to.cuda_matrix_maximum_inner, from.cuda_matrix_maximum_inner);
    to.wall_seconds += from.wall_seconds;
    to.executed |= from.executed;
}

struct Arena {
    TensorReceiverOptions options;
    at::Device device{at::kCPU};
    BulkReceipt total;
    mutable std::mutex mutex;
    std::condition_variable_any available;
    std::deque<const std::size_t*> pending;
    std::size_t existing_allocations = 0;
#if DATAPUMP_TRIAD_TENSOR_CUDA
    std::once_flag matrix_context_once;
    std::shared_ptr<CudaComplexMatrix::Context> matrix_context;

    std::shared_ptr<CudaComplexMatrix::Context> shared_matrix_context() {
        std::call_once(matrix_context_once, [&] { matrix_context = CudaComplexMatrix::make_context(); });
        return matrix_context;
    }
#endif

    explicit Arena(const TensorReceiverOptions& requested, bool automatic) : options(requested) {
        if (!automatic && (options.workspace_bytes < mib ||
                           options.workspace_bytes > static_cast<std::uint64_t>(INT64_MAX)))
            throw ReceiverWorkspaceError("Robust tensor backend requires an explicit workspace of at least 1 MiB");
        static std::once_flag threading;
        std::call_once(threading, [] {
            at::set_num_threads(1);
            at::set_num_interop_threads(1);
        });
        total.device = "cpu";
        total.device_name = "ATen CPU";
        total.library_version = TORCH_VERSION;
        total.workspace_limit_bytes = options.workspace_bytes;
        if (options.device == TensorDevice::cpu) {
            require(options.device_index == 0, "Robust CPU tensor device has no nonzero index");
            return;
        }
#if DATAPUMP_TRIAD_TENSOR_CUDA
        int devices = 0;
        C10_CUDA_CHECK(cudaGetDeviceCount(&devices));
        if (options.device_index >= static_cast<unsigned>(devices) ||
            options.device_index > static_cast<unsigned>(std::numeric_limits<c10::DeviceIndex>::max()))
            throw TensorBackendUnavailable("requested Robust CUDA device is unavailable");
        device = at::Device(at::kCUDA, static_cast<c10::DeviceIndex>(options.device_index));
        const c10::cuda::CUDAGuard guard(device);
        cudaDeviceProp properties{};
        C10_CUDA_CHECK(cudaGetDeviceProperties(&properties, device.index()));
        C10_CUDA_CHECK(cudaMemGetInfo(&total.initial_free_device_bytes, &total.total_device_bytes));
        c10::cuda::CUDACachingAllocator::init(devices);
        // Select the native caching allocator, whose fraction and reserved
        // block statistics correspond to the admission receipt below.
        require(c10::cuda::CUDACachingAllocator::get()->name() == "native",
                "Robust CUDA requires ATen's native caching allocator");
        const auto prior = c10::cuda::CUDACachingAllocator::getDeviceStats(device.index());
        // Automatic mode leaves separate driver/library and display headroom.
        // Reconfiguration can reuse the process allocator's retained blocks.
        // Existing active bytes still consume the new arena's capacity.
        const auto headroom = automatic
            ? std::clamp(total.total_device_bytes / 20, 256 * mib, 2048 * mib)
            : std::min<std::size_t>(256 * mib, total.total_device_bytes / 20);
        total.runtime_headroom_bytes = headroom;
        auto accessible = total.initial_free_device_bytes;
        if (automatic) {
            accessible = std::min(total.total_device_bytes, add(accessible,
                static_cast<std::size_t>(prior.reserved_bytes[0].current)));
            existing_allocations = static_cast<std::size_t>(prior.active_bytes[0].current);
            if (accessible <= headroom || accessible - headroom < add(existing_allocations, mib))
                throw ReceiverWorkspaceError("Robust CUDA has insufficient device memory with runtime headroom");
            options.workspace_bytes = accessible - headroom;
            total.reserved_bytes = total.peak_reserved_bytes = existing_allocations;
        }
        if (accessible <= headroom || options.workspace_bytes > accessible - headroom)
            throw ReceiverWorkspaceError("Robust CUDA workspace exceeds free device memory with runtime headroom");
        if (prior.reserved_bytes[0].current > static_cast<std::int64_t>(options.workspace_bytes))
            throw ReceiverWorkspaceError("existing CUDA allocator reservations exceed the Robust workspace");
        total.workspace_limit_bytes = options.workspace_bytes;
        const auto fraction = static_cast<double>(options.workspace_bytes) / total.total_device_bytes;
        c10::cuda::CUDACachingAllocator::setMemoryFraction(std::nextafter(fraction, 0.), device.index());
        // ATen allocates the FFT work area through its caching allocator.
        // Bound the number of plan objects retained in the separate cache.
        total.fft_plan_cache_limit = 4;
        at::detail::getCUDAHooks().cuFFTSetPlanCacheMaxSize(device.index(), dim(total.fft_plan_cache_limit));
        C10_CUDA_CHECK(cudaRuntimeGetVersion(&total.cuda_runtime_version));
        C10_CUDA_CHECK(cudaDriverGetVersion(&total.cuda_driver_version));
        // Bind runtime provenance before a describe-only request, rather
        // than changing identity when the first matrix stage is created.
        total.cuda_blas_version = cuda_matrix_library_version();
        total.cuda_fft_version = cuda_fft_library_version();
        total.device = "cuda:" + std::to_string(options.device_index);
        total.device_name = properties.name;
        total.cuda_compute_major = properties.major;
        total.cuda_compute_minor = properties.minor;
        total.cuda = true;
#else
        throw TensorBackendUnavailable("Robust CUDA requested with a CPU-only LibTorch backend");
#endif
    }

    bool reserve(std::size_t bytes) {
        std::lock_guard lock(mutex);
        if (bytes > options.workspace_bytes - total.reserved_bytes) return false;
        total.reserved_bytes += bytes;
        total.peak_reserved_bytes = std::max(total.peak_reserved_bytes, total.reserved_bytes);
        return true;
    }

    std::size_t admit(std::size_t minimum, std::size_t preferred, std::stop_token stop) {
        require(minimum && preferred >= minimum, "invalid Robust CUDA device lease");
        if (minimum > options.workspace_bytes - existing_allocations)
            throw ReceiverWorkspaceError("Robust CUDA cannot fit one complete device phase");
        std::unique_lock lock(mutex);
        // A waiting phase holds no device memory. Ready phases can always
        // complete within their lease, even if other owners have used VRAM.
        pending.push_back(&minimum);
        const auto remove = [&] {
            pending.erase(std::find(pending.begin(), pending.end(), &minimum));
            available.notify_all();
        };
        try {
            available.wait(lock, stop, [&] {
                return pending.front() == &minimum && minimum <= options.workspace_bytes - total.reserved_bytes;
            });
        } catch (...) {
            remove();
            throw;
        }
        remove();
        cancelled(stop);
        const auto bytes = std::min(preferred, options.workspace_bytes - total.reserved_bytes);
        total.reserved_bytes += bytes;
        total.peak_reserved_bytes = std::max(total.peak_reserved_bytes, total.reserved_bytes);
        return bytes;
    }

    void release(std::size_t bytes) noexcept {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (bytes && device.is_cuda() && std::uncaught_exceptions()) {
            // Charges can unwind before the surrounding operation guard.
            // Keep their reservation until submitted work has drained.
            try {
                const c10::cuda::CUDAGuard guard(device);
                (void)cudaDeviceSynchronize();
            } catch (...) {
                // The original CUDA error remains the operation's failure.
            }
        }
#endif
        {
            std::lock_guard lock(mutex);
            total.reserved_bytes -= bytes;
        }
        available.notify_all();
    }

    BulkReceipt receipt(bool query_runtime = true, const BulkReceipt* work = nullptr) const {
        BulkReceipt out;
        {
            std::lock_guard lock(mutex);
            out = total;
            out.device_admission_waiters = pending.size();
            if (work) add_work(out, *work);
        }
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (query_runtime && device.is_cuda()) {
            const auto stats = c10::cuda::CUDACachingAllocator::getDeviceStats(device.index());
            out.allocator_live_bytes = stats.allocated_bytes[0].current;
            out.allocator_reserved_bytes = stats.reserved_bytes[0].current;
            out.allocator_peak_live_bytes = stats.allocated_bytes[0].peak;
            out.allocator_peak_reserved_bytes = stats.reserved_bytes[0].peak;
            out.fft_plan_cache_count = at::detail::getCUDAHooks().cuFFTGetPlanCacheSize(device.index());
        }
#else
        (void)query_runtime;
#endif
        return out;
    }
};

std::shared_ptr<Arena> arena_for(const TensorReceiverOptions& options, bool automatic) {
    if (options.device == TensorDevice::cpu) return std::make_shared<Arena>(options, false);
    // CUDA's allocator limit is process/device scoped. Every receiver using
    // this device shares one charge instead of multiplying the configured cap.
    static std::mutex mutex;
    static std::map<unsigned, std::weak_ptr<Arena>> devices;
    std::lock_guard lock(mutex);
    auto& existing = devices[options.device_index];
    if (auto arena = existing.lock()) {
        require(automatic || arena->options.workspace_bytes == options.workspace_bytes,
                "active Robust CUDA receivers must agree on the device workspace");
        return arena;
    }
    auto arena = std::make_shared<Arena>(options, automatic);
    existing = arena;
    return arena;
}

class TensorWorkspace {
public:
    TensorWorkspace(std::shared_ptr<Arena> arena, bool leased) : arena_(std::move(arena)), leased_(leased) {}
    ~TensorWorkspace() { if (limit_) arena_->release(limit_); }

    bool leased() const noexcept { return leased_; }
    bool fits(std::size_t bytes) const {
        std::lock_guard lock(mutex_);
        return bytes <= limit_ - used_;
    }

    void shrink_to(std::size_t bytes) {
        std::lock_guard lock(mutex_);
        require(leased_ && used_ <= bytes, "Robust CUDA cannot shrink below its resident device workspace");
        if (limit_ > bytes) {
            arena_->release(limit_ - bytes);
            limit_ = bytes;
        }
    }

    void begin(std::size_t minimum, std::size_t preferred, std::stop_token stop) {
        if (!leased_) return;
        std::lock_guard lock(mutex_);
        require(!used_, "Robust CUDA cannot replace a lease with resident device buffers");
        if (limit_) arena_->release(std::exchange(limit_, 0));
        limit_ = arena_->admit(minimum, preferred, stop);
    }

    bool reserve(std::size_t bytes) {
        if (!leased_) return arena_->reserve(bytes);
        std::lock_guard lock(mutex_);
        if (bytes > limit_ - used_) return false;
        used_ += bytes;
        return true;
    }

    void release(std::size_t bytes) noexcept {
        if (leased_) {
            std::lock_guard lock(mutex_);
            used_ -= bytes;
        } else arena_->release(bytes);
    }

private:
    std::shared_ptr<Arena> arena_;
    const bool leased_;
    mutable std::mutex mutex_;
    std::size_t limit_ = 0, used_ = 0;
};

class Charge {
public:
    Charge() = default;
    Charge(std::shared_ptr<TensorWorkspace> workspace, std::size_t bytes)
        : workspace_(std::move(workspace)), bytes_(bytes) {
        if (!workspace_->reserve(bytes_))
            throw ReceiverWorkspaceError("Robust tensor operation exceeds the shared workspace");
    }
    Charge(Charge&& other) noexcept : workspace_(std::move(other.workspace_)), bytes_(other.bytes_) {}
    Charge& operator=(Charge&& other) noexcept {
        if (workspace_) workspace_->release(bytes_);
        workspace_ = std::move(other.workspace_);
        bytes_ = other.bytes_;
        return *this;
    }
    ~Charge() { if (workspace_) workspace_->release(bytes_); }

private:
    std::shared_ptr<TensorWorkspace> workspace_;
    std::size_t bytes_ = 0;
};

class TensorContext {
public:
    TensorContext(std::shared_ptr<Arena> arena, std::shared_ptr<BulkReceipt> backend_work,
                  bool precise, bool leased)
        : arena(std::move(arena)), workspace(std::make_shared<TensorWorkspace>(this->arena, leased)),
          precise(precise), type(precise ? at::kComplexDouble : at::kComplexFloat),
          real_type(precise ? at::kDouble : at::kFloat), item_bytes(precise ? 16 : 8),
          options(at::TensorOptions().device(this->arena->device)),
          complex_options(options.dtype(type)), double_options(options.dtype(at::kDouble)),
          backend_work(std::move(backend_work))
#if DATAPUMP_TRIAD_TENSOR_CUDA
          , stream(this->arena->device.is_cuda()
              ? std::optional(c10::cuda::getStreamFromPool(false, this->arena->device.index()))
              : std::nullopt)
#endif
    {}

    // Context methods serialize mutable tensors. Each CUDA context uses its
    // own stream. All borrowed uploads are blocking and results synchronize
    // before publication or release of their workspace charge.
    class Operation {
    public:
        explicit Operation(TensorContext& context)
            : context_(context), began_(Clock::now()), lock_(context.mutex)
#if DATAPUMP_TRIAD_TENSOR_CUDA
              , guard_(context.stream)
#endif
        {
            at::init_num_threads();
            require(at::get_num_threads() == 1 && at::get_num_interop_threads() == 1,
                    "Robust ATen work requires one numerical thread per receiver worker");
        }
        ~Operation() {
#if DATAPUMP_TRIAD_TENSOR_CUDA
            if (context_.stream && std::uncaught_exceptions()) {
                // Do not leave kernels using an operation's released charge.
                (void)cudaStreamSynchronize(context_.stream->stream());
                count(work.synchronizations, 1);
            }
#endif
            work.wall_seconds += std::chrono::duration<double>(Clock::now() - began_).count();
            add_work(context_.local, work);
            std::lock_guard lock(context_.arena->mutex);
            add_work(*context_.backend_work, work);
        }
        BulkReceipt work;
    private:
        TensorContext& context_;
        Clock::time_point began_;
        std::unique_lock<std::mutex> lock_;
        c10::InferenceMode inference_;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        c10::cuda::OptionalCUDAStreamGuard guard_;
#endif
    };

    BulkReceipt receipt() const {
        std::lock_guard lock(mutex);
        auto report = arena->receipt();
        auto identity = report;
        report = local;
        report.device = std::move(identity.device);
        report.device_name = std::move(identity.device_name);
        report.library_version = std::move(identity.library_version);
        report.cuda_runtime_version = identity.cuda_runtime_version;
        report.cuda_driver_version = identity.cuda_driver_version;
        report.cuda_blas_version = identity.cuda_blas_version;
        report.cuda_fft_version = identity.cuda_fft_version;
        report.cuda_compute_major = identity.cuda_compute_major;
        report.cuda_compute_minor = identity.cuda_compute_minor;
        report.numerical_threads = identity.numerical_threads;
        report.cuda = identity.cuda;
        report.precision = precise ? "fp64" : "fp32_bulk_fp64_coordinates_and_reductions";
        report.workspace_limit_bytes = identity.workspace_limit_bytes;
        report.reserved_bytes = identity.reserved_bytes;
        report.peak_reserved_bytes = identity.peak_reserved_bytes;
        report.allocator_live_bytes = identity.allocator_live_bytes;
        report.allocator_reserved_bytes = identity.allocator_reserved_bytes;
        report.allocator_peak_live_bytes = identity.allocator_peak_live_bytes;
        report.allocator_peak_reserved_bytes = identity.allocator_peak_reserved_bytes;
        report.total_device_bytes = identity.total_device_bytes;
        report.initial_free_device_bytes = identity.initial_free_device_bytes;
        report.runtime_headroom_bytes = identity.runtime_headroom_bytes;
        report.fft_plan_cache_count = identity.fft_plan_cache_count;
        report.fft_plan_cache_limit = identity.fft_plan_cache_limit;
        return report;
    }

    FineBulkWork fine_work() const {
        std::lock_guard lock(mutex);
        return {local.statistics_blocks, local.projected_values, local.executed};
    }

    CoarseBulkCompletedWork coarse_work() const {
        std::lock_guard lock(mutex);
        return {local.score_rows, local.executed};
    }

    template<class T>
    at::Tensor upload(std::span<const T> values, at::ScalarType source, at::ScalarType target,
                      BulkReceipt& work) const {
        const auto host_options = at::TensorOptions().device(at::kCPU).dtype(source);
        constexpr bool owns_host_tensor = std::is_same_v<T, Complex>;
        at::Tensor host;
        if constexpr (owns_host_tensor) {
            static_assert(sizeof(Complex) == sizeof(c10::complex<double>));
            static_assert(std::is_trivially_copyable_v<c10::complex<double>>);
            require(source == at::kComplexDouble, "invalid Robust complex staging scalar type");
            // ATen's complex type can require stricter alignment than
            // std::complex. Copy its two-double representation into owned,
            // allocator-aligned storage before entering tensor kernels.
            host = at::empty({dim(values.size())}, host_options);
            std::memcpy(host.mutable_data_ptr(), values.data(), values.size_bytes());
        } else {
            host = at::from_blob(const_cast<T*>(values.data()), {dim(values.size())}, host_options);
        }
        // Convert on the bounded host side before transfer. The upload cannot
        // leave pageable caller memory referenced after this function returns.
        if (arena->device.is_cpu()) return host.to(host_options.dtype(target), false, !owns_host_tensor);
        auto converted = host.to(host_options.dtype(target), false, false);
        auto copied = converted.to(options.dtype(target), false, true);
        count(work.upload_bytes, static_cast<std::uint64_t>(copied.nbytes()));
        count(work.synchronizations, 1);
        return copied;
    }

    void put_complex(at::Tensor destination, std::span<const Complex> input, BulkReceipt& work) const {
        require(destination.numel() == dim(input.size()), "Robust tensor complex upload shape mismatch");
        const auto limit = maximum_upload_bytes / sizeof(Complex);
        for (std::size_t first = 0; first < input.size();) {
            const auto rows = std::min(limit, input.size() - first);
            destination.narrow(0, dim(first), dim(rows)).copy_(
                upload(input.subspan(first, rows), at::kComplexDouble, type, work));
            first += rows;
        }
    }

    at::Tensor complex(std::span<const Complex> values, BulkReceipt& work) const {
        auto result = at::empty({dim(values.size())}, complex_options);
        put_complex(result, values, work);
        return result;
    }

    template<class T>
    void download(const at::Tensor& values, std::span<T> output, at::ScalarType destination,
                  BulkReceipt& work) const {
        require(values.numel() == dim(output.size()), "Robust tensor download shape mismatch");
        // Make the transfer and the host scalar conversion distinct so the
        // receipt describes the copied device payload for every library version.
        const auto copied = values.resolve_conj().resolve_neg().contiguous()
            .to(at::TensorOptions().device(at::kCPU).dtype(values.scalar_type()), false, false);
        if (arena->device.is_cuda()) {
            count(work.download_bytes, static_cast<std::uint64_t>(copied.nbytes()));
            count(work.synchronizations, 1);
        }
        const auto host = copied.to(destination);
        require(static_cast<std::size_t>(host.nbytes()) == output.size_bytes(),
                "Robust tensor download scalar extent mismatch");
        std::memcpy(static_cast<void*>(output.data()), host.const_data_ptr(), output.size_bytes());
    }

    void synchronize(BulkReceipt& work) const {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (stream) {
            stream->synchronize();
            count(work.synchronizations, 1);
        }
#else
        (void)work;
#endif
    }

#if DATAPUMP_TRIAD_TENSOR_CUDA
    cudaStream_t cuda_stream() const {
        require(stream.has_value(), "Robust fused mapping requires its CUDA stream");
        return stream->stream();
    }
#endif

    at::Tensor fft(const at::Tensor& input, std::size_t rows, bool inverse, BulkReceipt& work,
                   CoarseBulkWork* coarse = nullptr, bool unitary = false) const {
        auto result = inverse ? at::fft_ifft(input, dim(rows), -1) :
            at::fft_fft(input, dim(rows), -1, unitary ? std::optional<std::string_view>("ortho") : std::nullopt);
        record_fft(static_cast<std::size_t>(input.numel() / input.size(-1)), rows, inverse, work, coarse);
        return result;
    }

    std::shared_ptr<Arena> arena;
    std::shared_ptr<TensorWorkspace> workspace;
    bool precise;
    at::ScalarType type, real_type;
    std::size_t item_bytes;
    at::TensorOptions options, complex_options, double_options;

private:
    std::shared_ptr<BulkReceipt> backend_work;
    mutable std::mutex mutex;
    BulkReceipt local;
#if DATAPUMP_TRIAD_TENSOR_CUDA
    std::optional<c10::cuda::CUDAStream> stream;
#endif
};

struct Filter final : CoarseBulkFilter {
    const TensorContext* owner;
    std::size_t taps, rows;
    Charge resident;
    at::Tensor spectrum;

    Filter(const TensorContext& context, std::size_t count, std::size_t rows)
        : owner(&context), taps(count), rows(rows), resident(context.workspace, mul(rows, context.item_bytes)) {}
};

class TensorCoarse final : public CoarseBulk {
public:
    TensorCoarse(std::shared_ptr<Arena> arena, std::shared_ptr<BulkReceipt> backend_work,
                 bool precise, TensorPrecision precision, TensorCoarseMath math, bool leased)
        : device_(std::move(arena), std::move(backend_work), precise, leased), precision_(precision),
          requested_math_(math), math_(math) {}

    std::string_view precision_reason() const noexcept override {
        if (device_.workspace->leased() && lag_ && geometry_.lag_count == 1 && groups_.size() == 1)
            return geometry_.frequency_count == 1 ? "cuda-one-arrival-singleton-dot" : "cuda-one-arrival-chirp-z";
        if (!matrix_precision_reason_.empty()) return matrix_precision_reason_;
        return half_fft_precision_reason_;
    }

    void begin_fir_projection(std::size_t maximum_fft_rows, std::size_t maximum_filters,
                              std::size_t maximum_output_rows, std::stop_token stop) override {
        if (!device_.workspace->leased()) return;
        const auto spectrum = mul(maximum_fft_rows, device_.item_bytes);
        const auto resident = mul(maximum_filters, spectrum);
        const auto output = mul(maximum_output_rows, 3 * device_.item_bytes);
        const auto batch = mul(16, spectrum);
        device_.workspace->begin(add(add(resident, output), batch),
                                 add(add(resident, output), mul(4, batch)), stop);
    }

    static std::size_t maximum_frequency_tile_width(const CoarseBulkGeometry& geometry, std::size_t workspaces,
        std::size_t requested, std::size_t item_bytes, std::size_t capacity) {
        if (requested <= 1) return requested;
        const auto bank = mul(mul(workspaces, geometry.taps), geometry.correlation_fft_rows);
        const auto observation = add(geometry.kernel_rows, geometry.lag_count - 1);
        const auto resident = resident_bytes(add(add(bank, observation), mul(workspaces, 4)), workspaces, item_bytes);
        if (resident >= capacity) return 1;
        const auto one = add(observation_bytes(geometry, 1),
            frequency_batch_bytes(geometry, 1, frontier_overhead(geometry, 1), item_bytes));
        return std::clamp((capacity - resident) / one, std::size_t{1}, requested);
    }

    std::shared_ptr<const CoarseBulkFilter> prepare_filter(
        std::span<const double> taps, std::size_t fft_rows, CoarseBulkWork& work, std::stop_token stop) override {
        require(!taps.empty() && fft_rows >= taps.size(), "invalid Robust tensor FIR extent");
        TensorContext::Operation op(device_);
        cancelled(stop);
        auto filter = std::make_shared<Filter>(device_, taps.size(), fft_rows);
        Charge scratch(device_.workspace, mul(fft_rows, 12 * device_.item_bytes));
        const auto impulse = device_.upload(taps, at::kDouble, device_.real_type, op.work).to(device_.type);
        filter->spectrum = device_.fft(impulse, fft_rows, false, op.work, &work);
        device_.synchronize(op.work);
        cancelled(stop);
        return filter;
    }

    std::vector<Complex> project_fir(const CoarseBulkFilter& filter_base, const CoarseFirCrop& crop,
        const CoarseFirSource& fill, CoarseBulkWork& work, std::stop_token stop) override {
        const auto* filter = dynamic_cast<const Filter*>(&filter_base);
        require(filter && filter->owner == &device_, "Robust FIR filter belongs to a different tensor session");
        require(crop.output_stride && crop.chunk_rows && crop.input_rows &&
                crop.chunk_rows <= filter->rows - filter->taps + 1 && fill,
                "invalid Robust tensor FIR crop");
        if (!crop.output_rows) return {};
        TensorContext::Operation op(device_);
        const auto output_stop = add(crop.first_output, crop.output_rows);
        (void)add(crop.output_origin, mul(output_stop - 1, crop.output_stride));
        const auto output_bytes = mul(crop.output_rows, device_.item_bytes);
        Charge output_charge(device_.workspace, mul(output_bytes, 3));
        auto output = at::zeros({dim(crop.output_rows)}, device_.complex_options);
        struct Job {
            std::size_t first, rows, out_first, out_stop;
        };
        std::array<Job, 4> jobs{};
        std::size_t first = 0;
        const auto chunk_fft_bytes = mul(filter->rows, 16 * device_.item_bytes);
        while (first < crop.input_rows) {
            cancelled(stop);
            const auto [width, batch_charge] = admit(4, [&](std::size_t n) { return mul(n, chunk_fft_bytes); });
            std::size_t actual = 0;
            while (first < crop.input_rows && actual < width) {
                const auto rows = std::min(crop.chunk_rows, crop.input_rows - first);
                const auto tail = add(first, add(rows, filter->taps - 1));
                const auto low = first > crop.output_origin ? ceil_div(first - crop.output_origin, crop.output_stride) : 0;
                const auto high = tail > crop.output_origin ? ceil_div(tail - crop.output_origin, crop.output_stride) : 0;
                const auto lo = std::max(low, crop.first_output);
                const auto hi = std::min(high, output_stop);
                if (lo < hi) jobs[actual++] = {first, rows, lo, hi};
                first += rows;
            }
            if (!actual) continue;
            auto input = at::zeros({dim(actual), dim(filter->rows)}, device_.complex_options);
            std::vector<Complex> staging(crop.chunk_rows);
            for (std::size_t i = 0; i < actual; ++i) {
                const auto& job = jobs[i];
                fill(job.first, std::span(staging).first(job.rows));
                device_.put_complex(input.select(0, dim(i)).narrow(0, 0, dim(job.rows)),
                                    std::span(staging).first(job.rows), op.work);
                count(op.work.fir_input_rows, job.rows);
                count(work.fir_input_rows, job.rows);
            }
            auto spectrum = device_.fft(input, filter->rows, false, op.work, &work);
            spectrum.mul_(filter->spectrum);
            auto convolved = device_.fft(spectrum, filter->rows, true, op.work, &work);
            for (std::size_t i = 0; i < actual; ++i) {
                const auto& job = jobs[i];
                const auto first_sample = crop.output_origin + job.out_first * crop.output_stride - job.first;
                const auto rows = job.out_stop - job.out_first;
                const auto stop_sample = add(first_sample, add(mul(rows - 1, crop.output_stride), 1));
                output.narrow(0, dim(job.out_first - crop.first_output), dim(rows)).add_(
                    convolved.select(0, dim(i)).slice(0, dim(first_sample), dim(stop_sample), dim(crop.output_stride)));
                count(op.work.fir_output_contributions, rows);
                count(work.fir_output_contributions, rows);
            }
            // FFT work areas and tensor staging may be reused by the next
            // batch only after the current charge's work has completed.
            device_.synchronize(op.work);
        }
        std::vector<Complex> result(crop.output_rows);
        device_.download(output, std::span(result), at::kComplexDouble, op.work);
        cancelled(stop);
        return result;
    }

    void prepare(const CoarseBulkGeometry& geometry, std::span<const Complex> observation,
        std::span<const CoarseBulkWorkspace> bank, std::span<const CoarseZoomGroup> groups,
        CoarseBulkWork& work, std::stop_token stop) override {
        TensorContext::Operation op(device_);
        cancelled(stop);
        require(!preparation_started_ && !bank.empty() && geometry.taps && geometry.taps <= 2 &&
                geometry.kernel_rows && geometry.lag_count && geometry.frequency_count &&
                observation.size() == add(geometry.kernel_rows, geometry.lag_count - 1) &&
                std::isfinite(geometry.projected_sample_rate_hz) && geometry.projected_sample_rate_hz > 0 &&
                std::isfinite(geometry.variance) && geometry.variance > 0,
                "invalid Robust tensor coarse public geometry");
        preparation_started_ = true;
        lag_ = !groups.empty();
        const auto bank_rows = lag_ ? geometry.kernel_rows : geometry.correlation_fft_rows;
        require(lag_ || bank_rows >= observation.size(), "Robust tensor correlation FFT is undersized");
        const auto columns = mul(bank.size(), geometry.taps);
        auto resident = add(add(mul(columns, bank_rows), observation.size()), mul(bank.size(), 4));
        std::size_t covered = 0, largest_fft = bank_rows;
        std::size_t matrix_bytes = 0;
        for (const auto& group : groups) {
            require(group.frequencies && group.first_frequency <= covered &&
                    group.first_frequency >= (covered ? covered - 1 : 0) &&
                    group.first_frequency < geometry.frequency_count &&
                    group.frequencies <= geometry.frequency_count - group.first_frequency &&
                    group.first_frequency + group.frequencies >= covered &&
                    group.input_phase.size() == geometry.kernel_rows &&
                    (group.frequencies == 1 ? group.convolution.empty() :
                        group.convolution.size() >= add(geometry.kernel_rows, group.frequencies - 1)),
                    "invalid Robust tensor lag-group coverage");
            covered = group.first_frequency + group.frequencies;
            resident = add(resident, add(group.input_phase.size(), group.convolution.size()));
            largest_fft = std::max(largest_fft, group.convolution.size());
            matrix_bytes = add(matrix_bytes, mul(mul(geometry.kernel_rows, group.frequencies), sizeof(Complex) / 2));
        }
        require(!lag_ || covered == geometry.frequency_count, "Robust tensor lag groups do not cover the full grid");
        geometry_ = geometry;
        count_ = bank.size();
        const auto use_half = admit_half_frequency(op.work, stop);
        select_automatic_matrix(groups);
        matrix_fallback_ = matrix_policy_reason(geometry, groups, matrix_bytes);
        const auto bytes = resident_bytes(resident, bank.size(), device_.item_bytes);
        const auto preparation = add(mul(largest_fft, 12 * device_.item_bytes), 2 * maximum_upload_bytes);
        std::size_t fft_preferred = 0;
        if (!use_half && device_.workspace->leased()) {
            // The FIR owner has released its spectra. Quote the resident bank
            // together with scratch for a complete scoring batch before any
            // new upload, so no owner can wait while pinning its bank.
            std::size_t shared, batch, preferred_width;
            if (lag_) {
                shared = mul(mul(geometry.frequency_count, geometry.lag_count), 3 * sizeof(double));
                batch = lag_batch_bytes(geometry, largest_fft);
                preferred_width = std::min<std::size_t>(32, geometry.lag_count);
            } else {
                const auto width = geometry.frequency_tile_width ? geometry.frequency_tile_width
                                                                 : geometry.frequency_count;
                require(width <= geometry.frequency_count, "invalid Robust CUDA frequency tile width");
                shared = observation_bytes(geometry, width);
                batch = frequency_batch_bytes(geometry, width, frontier_overhead(geometry, width), device_.item_bytes);
                preferred_width = std::min(maximum_batch, bank.size());
            }
            const auto minimum = add(bytes, std::max(preparation, add(shared, batch)));
            fft_preferred = add(bytes, std::max(preparation, add(shared, mul(batch, preferred_width))));
            auto preferred = fft_preferred;
#if DATAPUMP_TRIAD_TENSOR_CUDA
            if (automatic_matrix_ && matrix_fallback_ == no_matrix_fallback) {
                const auto matrix = std::max(minimum, matrix_preferred_bytes(bytes, preparation));
                if (matrix <= device_.arena->options.workspace_bytes - device_.arena->existing_allocations)
                    preferred = matrix;
                else
                    matrix_fallback_ = 4;
            }
#endif
            device_.workspace->begin(minimum, preferred, stop);
            if (automatic_matrix_ && matrix_fallback_ == no_matrix_fallback && !device_.workspace->fits(preferred))
                matrix_fallback_ = 4;
        }
        initialize_matrix();
        if (automatic_matrix_ && matrix_fallback_ != no_matrix_fallback)
            device_.workspace->shrink_to(fft_preferred);
        Charge scratch;
        if (!use_half) {
            resident_ = Charge(device_.workspace, bytes);
            scratch = Charge(device_.workspace, preparation);
        }
        observation_ = device_.complex(observation, op.work);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (use_half) half_->diagnostics.zero_();
        else
#endif
            bank_ = at::empty({dim(bank.size()), dim(geometry.taps), dim(bank_rows)}, device_.complex_options);
        std::vector<double> a, d;
        std::vector<Complex> b;
        a.reserve(bank.size());
        d.reserve(bank.size());
        b.reserve(bank.size());
        for (std::size_t i = 0; i < bank.size(); ++i) {
            cancelled(stop);
            const auto& row = bank[i];
            a.push_back(row.inverse_a);
            d.push_back(row.inverse_d);
            b.push_back(row.inverse_b);
            for (std::size_t ray = 0; ray < geometry.taps; ++ray) {
                require(row.columns[ray].size() == geometry.kernel_rows, "invalid Robust tensor bank column length");
                auto source = device_.complex(row.columns[ray], op.work);
#if DATAPUMP_TRIAD_TENSOR_CUDA
                if (use_half) {
                    prepare_half_column(source, add(mul(i, geometry.taps), ray), work, op.work);
                    continue;
                }
#endif
                if (!lag_) source = device_.fft(source, bank_rows, false, op.work, &work).conj();
                bank_.select(0, dim(i)).select(0, dim(ray)).copy_(source);
            }
        }
        inverse_a_ = device_.upload(std::span<const double>(a), at::kDouble, at::kDouble, op.work);
        inverse_d_ = device_.upload(std::span<const double>(d), at::kDouble, at::kDouble, op.work);
        inverse_b_ = device_.upload(std::span<const Complex>(b), at::kComplexDouble, at::kComplexDouble, op.work);
        for (const auto& group : groups) {
            auto phase = device_.complex(group.input_phase, op.work);
            at::Tensor convolution;
            Group prepared{group.first_frequency, group.frequencies, std::move(phase), {}, {}, {}, matrix_fallback_};
            if (!group.convolution.empty()) {
                const auto impulse = device_.complex(group.convolution, op.work);
                convolution = device_.fft(impulse, group.convolution.size(), false, op.work, &work);
                if (requests_matrix(math_) && prepared.fallback == no_matrix_fallback) {
                    if (group.frequencies < 8 || group.frequencies > maximum_matrix_frequencies) {
                        prepared.fallback = 3;
                    } else {
                        try {
                            prepare_matrix(prepared, impulse, op.work);
                        } catch (const ReceiverWorkspaceError&) {
                            prepared.packed_right = at::Tensor{};
                            prepared.packed_right_charge = {};
                            prepared.fallback = 4;
                        } catch (const c10::OutOfMemoryError&) {
                            if (!automatic_matrix_) throw;
                            device_.synchronize(op.work);
                            prepared.packed_right = at::Tensor{};
                            prepared.packed_right_charge = {};
                            prepared.fallback = 4;
                            matrix_precision_reason_ = "cuda-int8-matrix-allocator-limit";
                        }
                    }
                }
            } else if (requests_matrix(math_) && prepared.fallback == no_matrix_fallback) {
                prepared.fallback = 3;
            }
            prepared.convolution = std::move(convolution);
            has_matrix_ |= prepared.packed_right.defined();
            if (automatic_matrix_ && !has_matrix_ && prepared.fallback == 4 && matrix_precision_reason_.empty())
                matrix_precision_reason_ = "cuda-int8-matrix-workspace-limit";
            groups_.push_back(std::move(prepared));
        }
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (automatic_matrix_) {
            if (has_matrix_) matrix_precision_reason_ = "cuda-int8-matrix";
            else if (matrix_fallback_ == no_matrix_fallback) {
                matrix_.reset();
                device_.workspace->shrink_to(fft_preferred);
            }
        }
#endif
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (use_half) {
            check_half_diagnostics(op.work);
            half_->bank_plan.reset();
        } else
#endif
            device_.synchronize(op.work);
        cancelled(stop);
        prepared_ = true;
    }

    void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink& sink,
        CoarseBulkWork& work, std::stop_token stop) override {
        TensorContext::Operation op(device_);
        require(static_cast<bool>(sink), "invalid Robust tensor frequency score sink");
        frequency_scores(frequencies, 0, work, op.work, stop,
            [&](const at::Tensor& scores, std::size_t first, std::size_t width) {
                publish(scores, first, width, frequencies.size(), sink, work, op.work);
            });
    }

    bool frequency_peaks(std::span<const double> frequencies, std::size_t separation, const CoarsePeakSink& sink,
        CoarseBulkWork& work, std::stop_token stop) override {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        // Unsupported backends must leave work and completion receipts
        // untouched, including Operation's timing/thread initialization.
        if (!device_.arena->device.is_cuda()) return false;
        TensorContext::Operation op(device_);
        require(separation && static_cast<bool>(sink), "invalid Robust tensor frequency peak geometry");
        // Include a possible contiguous score copy plus the fixed packed
        // output. Both remain charged through the blocking frontier readback.
        const auto extra = frontier_overhead(geometry_, frequencies.size());
        frequency_scores(frequencies, extra, work, op.work, stop,
            [&](const at::Tensor& scores, std::size_t first, std::size_t width) {
                publish_peaks(scores, first, width, frequencies.size(), separation, sink, work, op.work, stop);
            });
        return true;
#else
        (void)frequencies;
        (void)separation;
        (void)sink;
        (void)work;
        (void)stop;
        return false;
#endif
    }

    void lag_tile(std::size_t first_workspace, std::size_t workspace_count,
        const CoarseScoreSink& sink, CoarseBulkWork& work, std::stop_token stop) override {
        TensorContext::Operation op(device_);
        require(prepared_ && lag_ && workspace_count && first_workspace < count_ &&
                workspace_count <= count_ - first_workspace && sink, "invalid Robust tensor lag tile");
        const auto window_rows = geometry_.kernel_rows;
        const auto windows = observation_.unfold(0, dim(window_rows), 1);
        std::size_t largest_fft = window_rows;
        for (const auto& group : groups_) if (group.convolution.defined())
            largest_fft = std::max(largest_fft, static_cast<std::size_t>(group.convolution.numel()));
        const auto per_lag = lag_batch_bytes(geometry_, largest_fft);
        if (has_matrix_) {
            const auto matrix_per_lag = matrix_lag_bytes();
            // The resident operands determine which groups use FFT. A
            // matrix-only rectangle needs no FFT batch scratch. Mixed
            // groups retain a bound for either operation.
            const bool all_matrix = std::all_of(groups_.begin(), groups_.end(),
                [](const Group& group) { return group.packed_right.defined(); });
            const auto rectangle_per_lag = all_matrix ? matrix_per_lag : std::max(per_lag, matrix_per_lag);
            const auto completed = matrix_lag_tile(first_workspace, workspace_count, windows, rectangle_per_lag,
                                                   sink, work, op.work, stop);
            first_workspace += completed;
            workspace_count -= completed;
            if (!workspace_count) return;
        }
        const auto n = mul(geometry_.frequency_count, geometry_.lag_count);
        Charge output_charge(device_.workspace, mul(n, 3 * sizeof(double)));
        auto scores = at::empty({1, dim(geometry_.frequency_count), dim(geometry_.lag_count)}, device_.double_options);
        for (std::size_t w = first_workspace; w < first_workspace + workspace_count; ++w) {
            const auto column = bank_.select(0, dim(w)).conj();
            for (std::size_t first = 0; first < geometry_.lag_count;) {
                cancelled(stop);
                const auto [width, charge] = admit(std::min<std::size_t>(32, geometry_.lag_count - first),
                    [&](std::size_t rows) { return mul(rows, per_lag); });
                const auto product = windows.narrow(0, dim(first), dim(width)).unsqueeze(1) * column;
                auto correlations = at::empty({dim(width), dim(geometry_.taps), dim(geometry_.frequency_count)},
                                              has_matrix_ ? device_.options.dtype(at::kComplexDouble) : device_.complex_options);
                group_correlations(product, correlations, work, op.work, stop);
                const auto arranged = correlations.permute({2, 1, 0}).unsqueeze(0);
                scores.narrow(2, dim(first), dim(width)).copy_(quadratic(arranged, w));
                device_.synchronize(op.work);
                first += width;
            }
            publish(scores, w, 1, geometry_.frequency_count, sink, work, op.work);
        }
    }

    BulkReceipt receipt() const override {
        auto report = device_.receipt();
        report.requested_coarse_math = coarse_math_name(requested_math_);
        if (report.cuda_half_fft_forward_ffts) report.precision = "fp16_fft_fp64_coordinates_and_reductions";
        if (report.cuda_matrix_int8_calls) report.precision = "int8_matrix_fp64_coordinates_and_reductions";
        if (report.cuda_matrix_fp16_calls) report.precision = "fp16_matrix_fp64_coordinates_and_reductions";
        return report;
    }
    CoarseBulkCompletedWork completed_work() const override { return device_.coarse_work(); }

private:
    static constexpr std::size_t no_matrix_fallback = bulk_matrix_fallback_reasons.size();
    struct Group {
        std::size_t first, count;
        at::Tensor phase, convolution;
        // Tensor is destroyed before the reservation is released.
        Charge packed_right_charge;
        at::Tensor packed_right;
        std::size_t fallback = no_matrix_fallback;
    };

#if DATAPUMP_TRIAD_TENSOR_CUDA
    struct HalfFrequency {
        std::unique_ptr<CudaHalfFftPlan> bank_plan, observation_plan, inverse_plan;
        std::size_t frequencies = 0, batch = 0, workspace_bytes = 0, scratch_bytes = 0;
        at::Tensor bank, bank_exponents, scratch, diagnostics;
    };

    std::size_t half_bank_bytes(const HalfFrequency& plan) const {
        return mul(mul(count_, geometry_.taps), add(plan.bank_plan->data_bytes(), sizeof(std::int32_t)));
    }

    std::size_t half_phase_bytes(const HalfFrequency& plan) const {
        const auto observation_rows = add(geometry_.kernel_rows, geometry_.lag_count - 1);
        const auto gram_bytes = mul(count_, 2 * sizeof(double) + sizeof(Complex));
        const auto retained = add(add(half_bank_bytes(plan), mul(observation_rows, device_.item_bytes)),
            add(gram_bytes, add(plan.scratch_bytes, sizeof(unsigned))));
        // During a blocking upload the source and one device staging chunk
        // can coexist. The projected observation is already in retained.
        const auto upload = std::min(std::max(geometry_.kernel_rows, observation_rows),
            maximum_upload_bytes / sizeof(Complex));
        const auto prep = mul(add(geometry_.kernel_rows, upload), device_.item_bytes);
        const auto spectra = add(plan.observation_plan->data_bytes(), mul(plan.frequencies, sizeof(std::int32_t)));
        // Wrapped FP64 coordinates, angle temporaries and rotated C64 input.
        // Their lifetime ends before allocating the inverse batch.
        const auto rotation = add(mul(mul(plan.frequencies, observation_rows), 48),
            mul(add(observation_rows, plan.frequencies), 2 * sizeof(double)));
        const auto pairs = mul(plan.batch, plan.frequencies);
        const auto arrivals = mul(pairs, geometry_.lag_count);
        const auto correlations = mul(mul(arrivals, geometry_.taps), sizeof(Complex));
        const auto scores = add(mul(arrivals, 96), add(mul(plan.batch, frontier_overhead(geometry_, plan.frequencies)),
            sizeof(unsigned)));
        const auto scoring = add(plan.inverse_plan->data_bytes(), add(correlations, scores));
        // The arena also enforces the actual allocator reservation limit.
        // Allow native 512-byte rounding of the bounded live tensor set.
        return add(add(retained, std::max(prep, add(spectra, std::max(rotation, scoring)))), 32 * 512);
    }

    void record_half_fft(std::size_t transforms, std::size_t logical, bool inverse,
                         CoarseBulkWork& work, BulkReceipt& receipt) const {
        const auto padded = transforms - logical;
        record_fft(transforms, geometry_.correlation_fft_rows, inverse, receipt, &work, padded);
        count(inverse ? receipt.cuda_half_fft_inverse_ffts : receipt.cuda_half_fft_forward_ffts, transforms);
        count(receipt.cuda_half_fft_padded_ffts, padded);
    }

    unsigned* half_diagnostics() const {
        return reinterpret_cast<unsigned*>(half_->diagnostics.mutable_data_ptr<std::int32_t>());
    }

    void check_half_diagnostics(BulkReceipt& receipt) const {
        std::array<std::int32_t, 1> flags{};
        device_.download(half_->diagnostics, std::span<std::int32_t>(flags), at::kInt, receipt);
        require(!flags.front(), "Robust half FFT input, product or arrival is invalid");
    }

    void prepare_half_column(const at::Tensor& source, std::size_t column,
                             CoarseBulkWork& work, BulkReceipt& receipt) const {
        const auto row_bytes = half_->bank_plan->data_bytes();
        auto* packed = static_cast<std::byte*>(half_->bank.mutable_data_ptr()) + mul(column, row_bytes);
        const auto stream = device_.cuda_stream();
        C10_CUDA_CHECK(cuda_half_fft_pack(source.const_data_ptr<c10::complex<float>>(), packed,
            half_->bank_exponents.mutable_data_ptr<std::int32_t>() + column, half_diagnostics(),
            geometry_.correlation_fft_rows, geometry_.kernel_rows, 1,
            half_->scratch.mutable_data_ptr(), half_->scratch_bytes, stream));
        half_->bank_plan->enqueue(packed, half_->scratch.mutable_data_ptr(), half_->scratch_bytes, false);
        record_half_fft(1, 1, false, work, receipt);
    }
#endif

    bool admit_half_frequency(BulkReceipt& receipt, std::stop_token stop) {
        const auto automatic = device_.workspace->leased();
        if (!automatic && math_ != TensorCoarseMath::fp16_fft) return false;
        const auto reject = [&](HalfFftFallback reason) {
            const auto index = static_cast<std::size_t>(reason);
            half_fft_precision_reason_ = half_fft_precision_reasons[index];
            count(receipt.half_fft_fallback_counts[index], 1);
            return false;
        };
        if (!device_.arena->device.is_cuda()) return reject(HalfFftFallback::cpu_device);
        if (device_.precise) return reject(HalfFftFallback::fp64_stage);
        if (lag_) return reject(HalfFftFallback::lag_group_route);
        if (precision_ == TensorPrecision::fp32_minimum) return reject(HalfFftFallback::fp32_minimum);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        const auto rows = geometry_.correlation_fft_rows;
        const auto frequencies = geometry_.frequency_tile_width ? geometry_.frequency_tile_width : geometry_.frequency_count;
        require(frequencies <= geometry_.frequency_count, "invalid Robust half FFT tile width");
        auto batch = std::min(maximum_batch, count_);
        if (automatic) {
            if (!automatic_half_fft_hardware(device_.arena->total))
                return reject(HalfFftFallback::unqualified_hardware);
            // At 2^20 rows the absolute FP16 subnormal rounding contribution
            // to a scaled dense input row's L2 norm is about 0.14%. This
            // limits packing error, independent of cuFFT and sensitivity.
            // Prefer enough inverse work to amortize plan and pack overhead.
            // A long transform needs fewer frequencies to reach that work.
            if (rows < 4096 || rows > (std::size_t{1} << 20) || geometry_.lag_count < 2 ||
                mul(mul(rows, frequencies), mul(batch, geometry_.taps)) < (std::size_t{1} << 19))
                return reject(HalfFftFallback::unqualified_shape);
            // Match the conservative native guard until the corresponding
            // two-ray sensitivity range has its own receiver evidence.
            if (!(geometry_.minimum_eigenvalue_ratio >= 1. / 16))
                return reject(HalfFftFallback::ill_conditioned_template);
        }
        const auto plan_fft = [&](std::size_t batch) {
            const auto started = Clock::now();
            try {
                auto plan = CudaHalfFftPlan::make(rows, batch, device_.cuda_stream());
                receipt.cuda_half_fft_plan_seconds += std::chrono::duration<double>(Clock::now() - started).count();
                return plan;
            } catch (const CudaHalfFftAllocationError&) {
                receipt.cuda_half_fft_plan_seconds += std::chrono::duration<double>(Clock::now() - started).count();
                throw;
            }
        };
        auto candidate = std::make_unique<HalfFrequency>();
        candidate->frequencies = frequencies;
        cancelled(stop);
        try {
            candidate->bank_plan = plan_fft(1);
            candidate->observation_plan = plan_fft(candidate->frequencies);
        } catch (const CudaHalfFftAllocationError&) {
            return reject(HalfFftFallback::plan_allocation);
        }
        if (!candidate->bank_plan || !candidate->observation_plan ||
            (mul(count_, geometry_.taps) > 1 && candidate->bank_plan->data_bytes() % CudaHalfFftPlan::alignment))
            return reject(HalfFftFallback::unsupported_plan);
        auto failure = HalfFftFallback::unsupported_plan;
        const auto inverse_plan = [&](std::size_t batch) {
            if (candidate->inverse_plan && candidate->batch == batch) return true;
            cancelled(stop);
            candidate->inverse_plan.reset();
            try {
                candidate->inverse_plan = plan_fft(mul(mul(batch, candidate->frequencies), geometry_.taps));
            } catch (const CudaHalfFftAllocationError&) {
                failure = HalfFftFallback::plan_allocation;
                return false;
            }
            if (!candidate->inverse_plan) {
                failure = HalfFftFallback::unsupported_plan;
                return false;
            }
            candidate->batch = batch;
            candidate->workspace_bytes = std::max({candidate->bank_plan->workspace_bytes(),
                candidate->observation_plan->workspace_bytes(), candidate->inverse_plan->workspace_bytes()});
            candidate->scratch_bytes = std::max({candidate->workspace_bytes,
                cuda_half_fft_pack_workspace_bytes(rows, geometry_.kernel_rows, 1),
                cuda_half_fft_pack_workspace_bytes(rows, add(geometry_.kernel_rows, geometry_.lag_count - 1),
                    candidate->frequencies)});
            return true;
        };
        if (automatic) {
            // No half buffer or FFT work is live while replacing the FIR
            // lease. A one-workspace plan guarantees a feasible lower
            // bound without waiting for another owner's preferred batch.
            if (!inverse_plan(1)) return reject(failure);
            auto minimum = half_phase_bytes(*candidate);
            auto preferred = minimum;
            if (inverse_plan(batch)) {
                const auto full = half_phase_bytes(*candidate);
                minimum = std::min(minimum, full);
                preferred = std::max(preferred, full);
            } else {
                batch /= 2;
            }
            try {
                device_.workspace->begin(minimum, preferred, stop);
            } catch (const ReceiverWorkspaceError&) {
                return reject(HalfFftFallback::workspace_limit);
            }
        }
        const auto columns = mul(count_, geometry_.taps);
        const auto options = device_.options.dtype(at::kByte);
        for (; batch; batch /= 2) {
            if (!inverse_plan(batch)) continue;
            const auto bytes = half_phase_bytes(*candidate);
            try {
                Charge phase(device_.workspace, bytes);
                // Empty allocations submit no GPU work. A failed candidate
                // releases its tensors before the charge and can try a
                // smaller plan or the canonical FP32 route without a copy.
                auto bank = at::empty({dim(mul(columns, candidate->bank_plan->data_bytes()))}, options);
                auto exponents = at::empty({dim(columns)}, options.dtype(at::kInt));
                auto scratch = at::empty({dim(candidate->scratch_bytes)}, options);
                auto diagnostics = at::empty({1}, options.dtype(at::kInt));
                candidate->bank = std::move(bank);
                candidate->bank_exponents = std::move(exponents);
                candidate->scratch = std::move(scratch);
                candidate->diagnostics = std::move(diagnostics);
                resident_ = std::move(phase);
            } catch (const ReceiverWorkspaceError&) {
                failure = HalfFftFallback::workspace_limit;
                continue;
            } catch (const c10::OutOfMemoryError&) {
                failure = HalfFftFallback::allocator_limit;
                continue;
            }
            receipt.cuda_half_fft_peak_bank_bytes = half_bank_bytes(*candidate);
            receipt.cuda_half_fft_peak_workspace_bytes = candidate->workspace_bytes;
            receipt.cuda_half_fft_peak_phase_bytes = bytes;
            half_ = std::move(candidate);
            half_fft_precision_reason_ = "cuda-fp16-frequency-fft";
            return true;
        }
        return reject(failure);
#else
        (void)stop;
        return reject(HalfFftFallback::unsupported_plan);
#endif
    }

    std::size_t lag_batch_bytes(const CoarseBulkGeometry& geometry, std::size_t fft_rows) const {
        return add(mul(mul(fft_rows, geometry.taps), 14 * device_.item_bytes), mul(geometry.frequency_count, 112));
    }

    static std::size_t resident_bytes(std::size_t values, std::size_t workspaces, std::size_t item_bytes) {
        return add(mul(values, item_bytes), mul(workspaces, 48));
    }

    static std::size_t observation_bytes(const CoarseBulkGeometry& geometry, std::size_t frequencies) {
        return mul(mul(frequencies, geometry.correlation_fft_rows), 16 * sizeof(Complex));
    }

    static std::size_t frontier_overhead(const CoarseBulkGeometry& geometry, std::size_t frequencies) {
        return mul(frequencies, add(mul(geometry.lag_count, sizeof(double)), sizeof(CoarseBulkPeaks)));
    }

    static std::size_t frequency_batch_bytes(const CoarseBulkGeometry& geometry, std::size_t frequencies,
                                             std::size_t extra, std::size_t item_bytes) {
        const auto spectrum_rows = mul(frequencies, geometry.correlation_fft_rows);
        return add(add(mul(mul(spectrum_rows, geometry.taps), 12 * item_bytes),
                       mul(mul(frequencies, geometry.lag_count), 96)), extra);
    }

    at::Tensor rotate_observation(std::span<const double> frequencies, BulkReceipt& receipt) const {
        const auto position = at::arange(observation_.size(0), device_.double_options) / geometry_.projected_sample_rate_hz;
        const auto frequency = device_.upload(frequencies, at::kDouble, at::kDouble, receipt);
        const auto cycles = -frequency.unsqueeze(1) * position;
        const auto angle = ((cycles - cycles.floor()) * tau).to(device_.real_type);
        return at::complex(angle.cos(), angle.sin()) * observation_;
    }

#if DATAPUMP_TRIAD_TENSOR_CUDA
    void clear_fft_tail(const at::Tensor& packed, std::size_t used_bytes) const {
        const auto bytes = static_cast<std::size_t>(packed.nbytes());
        require(used_bytes <= bytes, "Robust half FFT tile exceeds its admitted storage");
        if (used_bytes != bytes)
            C10_CUDA_CHECK(cudaMemsetAsync(static_cast<std::byte*>(packed.mutable_data_ptr()) + used_bytes,
                0, bytes - used_bytes, device_.cuda_stream()));
    }

    template<class Publish>
    void half_frequency_scores(std::span<const double> frequencies, CoarseBulkWork& work,
                               BulkReceipt& receipt, std::stop_token stop, const Publish& publish_scores) {
        const auto n = geometry_.correlation_fft_rows;
        const auto f = frequencies.size();
        require(f <= half_->frequencies, "Robust half FFT frequency tile exceeds its admitted width");
        const auto stream = device_.cuda_stream();
        const auto options = device_.options.dtype(at::kByte);
        auto observation = at::empty({dim(half_->observation_plan->data_bytes())}, options);
        auto observation_exponents = at::empty({dim(f)}, options.dtype(at::kInt));
        {
            const auto rotated = rotate_observation(frequencies, receipt);
            require(rotated.is_contiguous(), "Robust half FFT requires contiguous rotated observations");
            C10_CUDA_CHECK(cuda_half_fft_pack(rotated.const_data_ptr<c10::complex<float>>(), observation.mutable_data_ptr(),
                observation_exponents.mutable_data_ptr<std::int32_t>(), half_diagnostics(), n,
                static_cast<std::size_t>(observation_.numel()), f,
                half_->scratch.mutable_data_ptr(), half_->scratch_bytes, stream));
        }
        const auto row_bytes = half_->observation_plan->data_bytes() / half_->frequencies;
        clear_fft_tail(observation, mul(f, row_bytes));
        half_->observation_plan->enqueue(observation.mutable_data_ptr(), half_->scratch.mutable_data_ptr(),
            half_->scratch_bytes, false);
        record_half_fft(half_->frequencies, f, false, work, receipt);

        auto inverse = at::empty({dim(half_->inverse_plan->data_bytes())}, options);
        const auto inverse_transforms = mul(mul(half_->batch, half_->frequencies), geometry_.taps);
        for (std::size_t first = 0; first < count_;) {
            cancelled(stop);
            const auto width = std::min(half_->batch, count_ - first);
            const auto first_column = mul(first, geometry_.taps);
            const CudaHalfFftProductShape shape{n, width, f, geometry_.taps};
            const auto* bank = static_cast<const std::byte*>(half_->bank.const_data_ptr()) + mul(first_column, row_bytes);
            C10_CUDA_CHECK(cuda_half_fft_products(bank, observation.const_data_ptr(), inverse.mutable_data_ptr(),
                half_diagnostics(), shape, stream));
            const auto transforms = mul(mul(width, f), geometry_.taps);
            clear_fft_tail(inverse, mul(transforms, row_bytes));
            half_->inverse_plan->enqueue(inverse.mutable_data_ptr(), half_->scratch.mutable_data_ptr(),
                half_->scratch_bytes, true);
            record_half_fft(inverse_transforms, transforms, true, work, receipt);

            auto correlations = at::empty({dim(width), dim(f), dim(geometry_.taps), dim(geometry_.lag_count)},
                options.dtype(at::kComplexDouble));
            C10_CUDA_CHECK(cuda_half_fft_arrivals(inverse.const_data_ptr(),
                half_->bank_exponents.const_data_ptr<std::int32_t>() + first_column,
                observation_exponents.const_data_ptr<std::int32_t>(), correlations.mutable_data_ptr<c10::complex<double>>(),
                half_diagnostics(), shape, geometry_.lag_count, stream));
            publish_scores(quadratic(correlations, first), first, width);
            count(receipt.cuda_half_fft_score_rows, mul(mul(width, f), geometry_.lag_count));
            first += width;
        }
    }
#endif

    template<class Publish>
    void frequency_scores(std::span<const double> frequencies, std::size_t extra_per_workspace,
                          CoarseBulkWork& work, BulkReceipt& receipt, std::stop_token stop, const Publish& publish_scores) {
        require(prepared_ && !lag_ && !frequencies.empty() && frequencies.size() <= geometry_.frequency_count,
                "invalid Robust tensor frequency tile");
        if (requests_matrix(math_)) count(receipt.matrix_fft_fallback_counts[2], 1);
        cancelled(stop);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (half_) {
            half_frequency_scores(frequencies, work, receipt, stop, publish_scores);
            return;
        }
#endif
        const auto fft_rows = geometry_.correlation_fft_rows;
        Charge observation_charge(device_.workspace, observation_bytes(geometry_, frequencies.size()));
        const auto rotated = rotate_observation(frequencies, receipt);
        const auto spectra = device_.fft(rotated, fft_rows, false, receipt, &work);
        const auto per_workspace = frequency_batch_bytes(geometry_, frequencies.size(), extra_per_workspace,
                                                          device_.item_bytes);
        for (std::size_t first = 0; first < count_;) {
            cancelled(stop);
            const auto [width, charge] = admit(std::min(maximum_batch, count_ - first),
                [&](std::size_t n) { return mul(n, per_workspace); });
            const auto bank = bank_.narrow(0, dim(first), dim(width)).unsqueeze(1);
            const auto product = bank * spectra.unsqueeze(0).unsqueeze(2);
            auto correlation = device_.fft(product, fft_rows, true, receipt, &work)
                .narrow(-1, 0, dim(geometry_.lag_count));
            publish_scores(quadratic(correlation, first), first, width);
            first += width;
        }
    }

    void publish_peaks(const at::Tensor& scores, std::size_t first, std::size_t width, std::size_t frequencies,
                       std::size_t separation, const CoarsePeakSink& sink, CoarseBulkWork& work,
                       BulkReceipt& receipt, std::stop_token stop) const {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        const auto pairs = mul(width, frequencies);
        const auto values = mul(pairs, geometry_.lag_count);
        const auto bytes = mul(pairs, sizeof(CoarseBulkPeaks));
        const auto packet_bytes = add(bytes, half_ ? sizeof(unsigned) : 0);
        require(scores.scalar_type() == at::kDouble && static_cast<std::size_t>(scores.numel()) == values,
                "invalid Robust tensor peak score storage");
        const auto contiguous = scores.contiguous();
        auto packed = at::empty({dim(packet_bytes)}, device_.options.dtype(at::kByte));
        C10_CUDA_CHECK(cuda_select_coarse_peaks(contiguous.const_data_ptr<double>(),
            reinterpret_cast<CoarseBulkPeaks*>(packed.mutable_data_ptr()), pairs, geometry_.lag_count,
            separation, device_.cuda_stream()));
        // Append the arithmetic witness to the existing blocking frontier
        // readback so a failed half batch cannot publish any scores.
        if (half_)
            C10_CUDA_CHECK(cudaMemcpyAsync(static_cast<std::byte*>(packed.mutable_data_ptr()) + bytes,
                half_->diagnostics.const_data_ptr(), sizeof(unsigned), cudaMemcpyDeviceToDevice, device_.cuda_stream()));
        std::vector<CoarseBulkPeaks> host(add(pairs, half_ ? 1 : 0));
        auto* host_bytes = reinterpret_cast<std::uint8_t*>(host.data());
        device_.download(packed, std::span(host_bytes, packet_bytes), at::kByte, receipt);
        if (half_) {
            unsigned flags = 0;
            std::memcpy(&flags, host_bytes + bytes, sizeof(flags));
            require(!flags, "Robust half FFT input, product or arrival is invalid");
        }
        const auto frontiers = std::span(host).first(pairs);
        // The kernel validates every input score, including rejected arrivals.
        // Validate the entire packed batch before publishing any frontier.
        for (const auto& row : frontiers) {
            require(row.count > 0 && row.count <= 2, "Robust tensor coarse score is nonfinite or peak count is invalid");
            for (std::size_t i = 0; i < row.count; ++i)
                require(row.lags[i] < geometry_.lag_count && std::isfinite(row.scores[i]) &&
                        (!i || row.lags[i] != row.lags[0]), "invalid Robust tensor coarse peak arrival or score");
        }
        cancelled(stop);
        count(receipt.score_rows, values);
        count(receipt.cuda_arrival_peak_rows, pairs);
        count(receipt.cuda_arrival_peak_values, values);
        count(work.score_rows, values);
        count(work.workspace_frequency_pairs, pairs);
        receipt.executed = true;
        sink(first, width, frontiers);
#else
        (void)scores;
        (void)first;
        (void)width;
        (void)frequencies;
        (void)separation;
        (void)sink;
        (void)work;
        (void)receipt;
        (void)stop;
        throw TensorBackendUnavailable("Robust arrival peak reduction requires CUDA");
#endif
    }

    void select_automatic_matrix(std::span<const CoarseZoomGroup> groups) {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (!device_.workspace->leased() || requested_math_ != TensorCoarseMath::fft || geometry_.lag_count <= 1 || !lag_)
            return;
        if (device_.precise) return;
        if (precision_ == TensorPrecision::fp32_minimum) {
            matrix_precision_reason_ = "cuda-int8-matrix-fp32-minimum";
            return;
        }
        const auto& identity = device_.arena->total;
        const auto capability = 10 * identity.cuda_compute_major + identity.cuda_compute_minor;
        if (capability != 89 && capability != 120) {
            matrix_precision_reason_ = "cuda-int8-matrix-unqualified-hardware";
            return;
        }
        // The broad frequency FFT and single-arrival chirp-Z have different
        // work scaling. Consider only one complete lag group and large
        // independent rows in the measured one-ray precision scope.
        matrix_precision_reason_ = "cuda-int8-matrix-unqualified-shape";
        if (groups.size() != 1 || geometry_.taps != 1 || geometry_.kernel_rows < 16 ||
            geometry_.kernel_rows > maximum_matrix_kernel_rows || geometry_.frequency_count < 8 ||
            geometry_.frequency_count > maximum_matrix_frequencies || geometry_.lag_count < 32 ||
            std::min(count_, geometry_.lag_workspace_tile_width) < minimum_auto_matrix_workspaces ||
            preferred_matrix_rows() < minimum_auto_matrix_rows || !std::has_single_bit(groups.front().convolution.size()))
            return;
        const auto fft_rows = groups.front().convolution.size();
        const auto paired_fft_row_stages = mul(mul(2, fft_rows), std::bit_width(fft_rows - 1));
        // A work crossover proxy calibrated on complete paired lag costs.
        // The ratio is real padded MACs per complex FFT row-stage.
        if (CudaComplexMatrix::padded_real_macs_per_row(geometry_.frequency_count, geometry_.kernel_rows) >
            mul(30, paired_fft_row_stages)) return;
        matrix_precision_reason_ = {};
        automatic_matrix_ = true;
        math_ = TensorCoarseMath::int8;
#else
        (void)groups;
#endif
    }

    std::size_t matrix_policy_reason(const CoarseBulkGeometry& geometry, std::span<const CoarseZoomGroup> groups,
                                     std::size_t matrix_bytes) const {
        if (!requests_matrix(math_)) return no_matrix_fallback;
        if (!device_.arena->device.is_cuda()) return 0;
        if (device_.precise) return 1;
        if (!lag_) return 2;
        if (geometry.kernel_rows < 16 || geometry.kernel_rows > maximum_matrix_kernel_rows || geometry.lag_count < 16)
            return 3;
        const auto limit = std::min(maximum_matrix_bytes, device_.arena->options.workspace_bytes / 8);
        if (matrix_bytes > limit) return 4;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        std::size_t packed_bytes = 0;
        for (const auto& group : groups)
            if (!group.convolution.empty() && group.frequencies >= 8 && group.frequencies <= maximum_matrix_frequencies)
                packed_bytes = add(packed_bytes, CudaComplexMatrix::prepared_right_bytes(matrix_math(),
                    group.frequencies, geometry.kernel_rows));
        if (packed_bytes > limit) return 4;
        const auto& identity = device_.arena->total;
        const auto capability = 10 * identity.cuda_compute_major + identity.cuda_compute_minor;
        if (capability < (matrix_math() == CudaMatrixMath::int8 ? 72 : 70)) return 5;
        return no_matrix_fallback;
#else
        (void)groups;
        return 5;
#endif
    }

    void initialize_matrix() {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (requests_matrix(math_) && matrix_fallback_ == no_matrix_fallback) {
            try {
                matrix_ = std::make_unique<CudaComplexMatrix>(device_.arena->shared_matrix_context());
                if (automatic_matrix_ && !matrix_->has_tensor_algorithm(matrix_math(), preferred_matrix_rows(),
                        geometry_.frequency_count, geometry_.kernel_rows))
                    matrix_fallback_ = 5;
            } catch (const CudaMatrixAllocationError&) {
                // A failed call_once initialization remains retryable by the
                // next owner. This owner still has its complete FFT minimum.
                if (!automatic_matrix_) {
                    matrix_ = std::make_unique<CudaComplexMatrix>();
                    return;
                }
                matrix_fallback_ = 4;
                matrix_precision_reason_ = "cuda-int8-matrix-allocator-limit";
            }
        }
        if (automatic_matrix_ && matrix_fallback_ != no_matrix_fallback) {
            if (matrix_precision_reason_.empty())
                matrix_precision_reason_ = matrix_fallback_ == 4 ? "cuda-int8-matrix-workspace-limit"
                                                                : "cuda-int8-matrix-device-capability";
            matrix_.reset();
        }
#endif
    }

    std::size_t matrix_lag_bytes() const {
        return add(mul(mul(geometry_.kernel_rows, geometry_.taps), device_.item_bytes),
            mul(geometry_.frequency_count, geometry_.taps * sizeof(Complex) + 112));
    }

    std::size_t matrix_rectangle_bytes(std::size_t workspaces, std::size_t arrivals,
                                       std::size_t per_lag, std::size_t scratch) const {
        const auto score_bytes = mul(mul(geometry_.frequency_count, geometry_.lag_count), 3 * sizeof(double));
        return add(mul(workspaces, score_bytes), add(mul(mul(workspaces, arrivals), per_lag), scratch));
    }

#if DATAPUMP_TRIAD_TENSOR_CUDA
    CudaMatrixMath matrix_math() const {
        if (math_ == TensorCoarseMath::int8) return CudaMatrixMath::int8;
        return CudaMatrixMath::fp16;
    }

    std::size_t preferred_matrix_workspaces() const {
        return std::min({maximum_matrix_workspaces, count_, geometry_.lag_workspace_tile_width});
    }

    std::size_t preferred_matrix_rows() const {
        return mul(mul(preferred_matrix_workspaces(), std::min(maximum_matrix_arrivals, geometry_.lag_count)),
                   geometry_.taps);
    }

    std::size_t matrix_group_scratch_bytes(std::size_t rows, std::size_t columns) const {
        const auto scratch = CudaComplexMatrix::workspace_bytes(matrix_math(), rows, columns, geometry_.kernel_rows);
        // The contiguous input, C128 result and diagnostics coexist with
        // the explicit library packing and accumulation workspace.
        const auto tensors = add(mul(rows, add(mul(geometry_.kernel_rows, sizeof(Complex) / 2),
                                               mul(columns, sizeof(Complex)))), sizeof(CudaMatrixDiagnostics));
        return add(scratch, tensors);
    }

    std::size_t matrix_preferred_bytes(std::size_t resident, std::size_t preparation) const {
        // Automatic admission selects exactly one complete matrix group.
        const auto columns = geometry_.frequency_count;
        const auto packed = CudaComplexMatrix::prepared_right_bytes(matrix_math(), columns, geometry_.kernel_rows);
        const auto rectangle = matrix_rectangle_bytes(preferred_matrix_workspaces(),
            std::min(maximum_matrix_arrivals, geometry_.lag_count), matrix_lag_bytes(),
            matrix_group_scratch_bytes(preferred_matrix_rows(), columns));
        return add(add(resident, packed), std::max(add(preparation, sizeof(CudaMatrixDiagnostics)), rectangle));
    }

    static void check_matrix_diagnostics(const CudaMatrixDiagnostics& observed, BulkReceipt& receipt) {
        count(receipt.cuda_matrix_nonfinite_values, observed.nonfinite_values);
        count(receipt.cuda_matrix_saturated_values, observed.saturated_values);
        receipt.cuda_matrix_maximum_a_scale = std::max(receipt.cuda_matrix_maximum_a_scale, observed.maximum_a_scale);
        receipt.cuda_matrix_maximum_b_scale = std::max(receipt.cuda_matrix_maximum_b_scale, observed.maximum_b_scale);
        require(!observed.nonfinite_values && !observed.saturated_values,
                "Robust coarse low-precision matrix input is nonfinite or saturated");
    }
#endif

    void prepare_matrix(Group& group, const at::Tensor& impulse, BulkReceipt& receipt) {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        require(static_cast<bool>(matrix_), "Robust coarse matrix handle was not prepared");
        const auto bytes = CudaComplexMatrix::prepared_right_bytes(matrix_math(), group.count, geometry_.kernel_rows);
        group.packed_right_charge = Charge(device_.workspace, bytes);
        Charge diagnostics_charge(device_.workspace, sizeof(CudaMatrixDiagnostics));
        group.packed_right = at::empty({dim(bytes)}, device_.options.dtype(at::kByte));
        auto diagnostics = at::zeros({dim(sizeof(CudaMatrixDiagnostics))}, device_.options.dtype(at::kByte));
        const auto packed = matrix_->prepare_right(matrix_math(), group.phase.const_data_ptr<c10::complex<float>>(),
            impulse.const_data_ptr<c10::complex<float>>(), static_cast<std::size_t>(impulse.numel()),
            group.count, geometry_.kernel_rows, group.packed_right.mutable_data_ptr(), bytes,
            reinterpret_cast<CudaMatrixDiagnostics*>(diagnostics.mutable_data_ptr()), device_.cuda_stream());
        CudaMatrixDiagnostics observed{};
        static_assert(std::is_trivially_copyable_v<CudaMatrixDiagnostics>);
        device_.download(diagnostics, std::span(reinterpret_cast<std::uint8_t*>(&observed), sizeof(observed)),
                         at::kByte, receipt);
        check_matrix_diagnostics(observed, receipt);
        count(receipt.cuda_matrix_prepared_b_groups, 1);
        count(receipt.cuda_matrix_prepared_b_bytes, bytes);
        count(receipt.cuda_matrix_prepared_b_values, packed);
        count(receipt.cuda_matrix_packed_values, packed);
#else
        (void)group;
        (void)impulse;
        (void)receipt;
        throw TensorBackendUnavailable("Robust coarse matrix processing requires CUDA");
#endif
    }

    std::size_t matrix_scratch_bytes(std::size_t arrivals) const {
        std::size_t bytes = 0;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        for (const auto& group : groups_) if (group.packed_right.defined()) {
            const auto rows = mul(arrivals, geometry_.taps);
            bytes = std::max(bytes, matrix_group_scratch_bytes(rows, group.count));
        }
#else
        (void)arrivals;
#endif
        return bytes;
    }

    void release_matrices(BulkReceipt& work) {
        device_.synchronize(work);
        for (auto& group : groups_) if (group.packed_right.defined()) {
            group.packed_right = at::Tensor{};
            group.packed_right_charge = {};
            group.fallback = 4;
        }
        has_matrix_ = false;
    }

    struct MatrixRectangle {
        std::size_t workspaces = 0, arrivals = 0;
        Charge charge;
    };

    MatrixRectangle admit_matrix_rectangle(std::size_t maximum_workspaces, std::size_t per_lag) const {
        for (auto workspaces = maximum_workspaces; workspaces; workspaces /= 2) {
            for (auto arrivals = std::min(maximum_matrix_arrivals, geometry_.lag_count); arrivals; arrivals /= 2) {
                const auto bytes = matrix_rectangle_bytes(workspaces, arrivals, per_lag,
                    matrix_scratch_bytes(mul(workspaces, arrivals)));
                try {
                    return {workspaces, arrivals, Charge(device_.workspace, bytes)};
                } catch (const ReceiverWorkspaceError&) {
                    // Reduce arrival width first; if even one cannot fit,
                    // retry a smaller independent-workspace batch.
                }
            }
        }
        return {};
    }

    std::size_t matrix_lag_tile(std::size_t first_workspace, std::size_t workspace_count, const at::Tensor& windows,
                               std::size_t per_lag, const CoarseScoreSink& sink, CoarseBulkWork& work,
                               BulkReceipt& receipt, std::stop_token stop) {
        std::size_t completed = 0;
        while (completed < workspace_count) {
            cancelled(stop);
            const auto maximum = automatic_matrix_ ? std::min(maximum_matrix_workspaces, geometry_.lag_workspace_tile_width)
                                                   : maximum_matrix_workspaces;
            auto rectangle = admit_matrix_rectangle(std::min(maximum, workspace_count - completed), per_lag);
            if (!rectangle.workspaces) {
                // Matrix residency can fit while packing cannot. The caller
                // resumes its serial FFT path at the first unpublished
                // workspace, retaining full coverage and publication order.
                release_matrices(receipt);
                return completed;
            }
            const auto first = first_workspace + completed;
            const auto batch = rectangle.workspaces;
            auto scores = at::empty({dim(batch), dim(geometry_.frequency_count), dim(geometry_.lag_count)},
                                    device_.double_options);
            const auto columns = bank_.narrow(0, dim(first), dim(batch)).conj().unsqueeze(1);
            for (std::size_t arrival = 0; arrival < geometry_.lag_count;) {
                cancelled(stop);
                const auto width = std::min(rectangle.arrivals, geometry_.lag_count - arrival);
                // One GEMM sees all independent workspace/arrival/tap rows.
                // The maximum rectangle is 32*64*2 = 4096 complex rows.
                const auto product = windows.narrow(0, dim(arrival), dim(width)).unsqueeze(0).unsqueeze(2) * columns;
                auto correlations = at::empty({dim(batch), dim(width), dim(geometry_.taps), dim(geometry_.frequency_count)},
                                              device_.options.dtype(at::kComplexDouble));
                group_correlations(product, correlations, work, receipt, stop);
                scores.narrow(2, dim(arrival), dim(width)).copy_(quadratic(correlations.permute({0, 3, 2, 1}), first));
                device_.synchronize(receipt);
                arrival += width;
            }
            // The public host-staging bound is eight complete workspaces.
            // Never publish arrival fragments or enlarge that host bound
            // merely because the device now computes a larger rectangle.
            for (std::size_t offset = 0; offset < batch;) {
                const auto width = std::min(maximum_batch, batch - offset);
                publish(scores.narrow(0, dim(offset), dim(width)), first + offset, width,
                        geometry_.frequency_count, sink, work, receipt);
                offset += width;
            }
            completed += batch;
        }
        return completed;
    }

    void group_correlations(const at::Tensor& product, at::Tensor correlations,
                            CoarseBulkWork& work, BulkReceipt& receipt, std::stop_token stop) {
        for (const auto& group : groups_) {
            cancelled(stop);
            auto destination = correlations.narrow(-1, dim(group.first), dim(group.count));
            if (group.packed_right.defined()) {
                matrix_correlations(product, group, destination, receipt);
                continue;
            }
            if (requests_matrix(math_))
                count(receipt.matrix_fft_fallback_counts[group.fallback], 1);
            const auto input = product * group.phase;
            if (!group.convolution.defined()) {
                destination.copy_(input.sum(-1, true, at::kComplexDouble));
                count(work.singleton_dot_terms, static_cast<std::uint64_t>(product.numel()));
            } else {
                const auto nfft = static_cast<std::size_t>(group.convolution.numel());
                auto spectrum = device_.fft(input, nfft, false, receipt, &work);
                spectrum.mul_(group.convolution);
                destination.copy_(device_.fft(spectrum, nfft, true, receipt, &work).narrow(-1, 0, dim(group.count)));
            }
        }
    }

    void matrix_correlations(const at::Tensor& product, const Group& group, at::Tensor destination,
                             BulkReceipt& receipt) {
#if DATAPUMP_TRIAD_TENSOR_CUDA
        require(matrix_ && group.packed_right.defined(), "Robust coarse matrix workspace was not prepared");
        const auto rows = static_cast<std::size_t>(product.numel()) / geometry_.kernel_rows;
        const auto scratch_bytes = CudaComplexMatrix::workspace_bytes(matrix_math(), rows, group.count, geometry_.kernel_rows);
        auto scratch = at::empty({dim(scratch_bytes)}, device_.options.dtype(at::kByte));
        auto diagnostics = at::zeros({dim(sizeof(CudaMatrixDiagnostics))}, device_.options.dtype(at::kByte));
        auto input = product.reshape({dim(rows), dim(geometry_.kernel_rows)}).contiguous();
        auto output = at::empty({dim(rows), dim(group.count)}, device_.options.dtype(at::kComplexDouble));
        const auto work = matrix_->launch(matrix_math(), input.const_data_ptr<c10::complex<float>>(),
            group.packed_right.const_data_ptr(), static_cast<std::size_t>(group.packed_right.nbytes()),
            output.mutable_data_ptr<c10::complex<double>>(),
            rows, group.count, geometry_.kernel_rows, scratch.mutable_data_ptr(), scratch_bytes,
            reinterpret_cast<CudaMatrixDiagnostics*>(diagnostics.mutable_data_ptr()), device_.cuda_stream());
        destination.copy_(output.reshape(destination.sizes()));
        CudaMatrixDiagnostics observed{};
        static_assert(std::is_trivially_copyable_v<CudaMatrixDiagnostics>);
        device_.download(diagnostics, std::span(reinterpret_cast<std::uint8_t*>(&observed), sizeof(observed)),
                         at::kByte, receipt);
        check_matrix_diagnostics(observed, receipt);
        count(math_ == TensorCoarseMath::int8 ? receipt.cuda_matrix_int8_calls : receipt.cuda_matrix_fp16_calls,
              work.gemm_calls);
        count(math_ == TensorCoarseMath::int8 ? receipt.cuda_matrix_lt_imma_calls : receipt.cuda_matrix_lt_hmma_calls,
              work.lt_tensor_calls);
        count(receipt.cuda_matrix_unwitnessed_calls, work.gemm_calls - work.lt_tensor_calls);
        count(receipt.cuda_matrix_prepared_b_uses, 1);
        count(receipt.cuda_matrix_inner_chunks, work.inner_chunks);
        count(receipt.cuda_matrix_padded_real_macs, work.padded_real_macs);
        count(receipt.cuda_matrix_packed_values, work.packed_values);
        count(receipt.cuda_matrix_output_values, mul(rows, group.count));
        receipt.cuda_matrix_maximum_rows = std::max(receipt.cuda_matrix_maximum_rows, static_cast<std::uint64_t>(rows));
        receipt.cuda_matrix_maximum_columns = std::max(receipt.cuda_matrix_maximum_columns, static_cast<std::uint64_t>(group.count));
        receipt.cuda_matrix_maximum_inner = std::max(receipt.cuda_matrix_maximum_inner, static_cast<std::uint64_t>(geometry_.kernel_rows));
#else
        (void)product;
        (void)group;
        (void)destination;
        (void)receipt;
        throw TensorBackendUnavailable("Robust coarse matrix processing requires CUDA");
#endif
    }

    template<class Size>
    std::pair<std::size_t, Charge> admit(std::size_t maximum, const Size& size) const {
        for (auto width = maximum; width; width /= 2) {
            try {
                return {width, Charge(device_.workspace, size(width))};
            } catch (const ReceiverWorkspaceError&) {
                if (width == 1) throw;
            }
        }
        throw ReceiverWorkspaceError("Robust tensor operation cannot admit one batch");
    }

    at::Tensor quadratic(const at::Tensor& correlations, std::size_t first) const {
        const auto size = correlations.size(0);
        const auto coefficients = [&](const at::Tensor& inverse) {
            return inverse.narrow(0, dim(first), size).reshape({size, 1, 1});
        };
        const auto a = correlations.select(2, 0).to(at::kComplexDouble);
        auto score = coefficients(inverse_a_) * (at::real(a).square() + at::imag(a).square());
        if (geometry_.taps == 2) {
            const auto b = correlations.select(2, 1).to(at::kComplexDouble);
            score.add_(coefficients(inverse_d_) * (at::real(b).square() + at::imag(b).square()));
            score.add_(at::real(a.conj() * coefficients(inverse_b_) * b) * 2);
        }
        return score.div_(geometry_.variance);
    }

    void publish(const at::Tensor& scores, std::size_t first, std::size_t width, std::size_t frequencies,
                 const CoarseScoreSink& sink, CoarseBulkWork& work, BulkReceipt& receipt) const {
        const auto pairs = mul(width, frequencies);
        std::vector<double> host(mul(pairs, geometry_.lag_count));
        device_.download(scores, std::span(host), at::kDouble, receipt);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (half_) check_half_diagnostics(receipt);
#endif
        require(std::all_of(host.begin(), host.end(), [](double value) { return std::isfinite(value); }),
                "Robust tensor coarse score is nonfinite");
        count(receipt.score_rows, host.size());
        count(work.score_rows, host.size());
        count(work.workspace_frequency_pairs, pairs);
        receipt.executed = true;
        sink(first, width, host);
    }

    TensorContext device_;
    TensorPrecision precision_;
    TensorCoarseMath requested_math_, math_;
    CoarseBulkGeometry geometry_;
    std::size_t count_ = 0;
    std::size_t matrix_fallback_ = no_matrix_fallback;
    std::string_view half_fft_precision_reason_, matrix_precision_reason_;
    bool automatic_matrix_ = false;
    bool lag_ = false, preparation_started_ = false, prepared_ = false, has_matrix_ = false;
    Charge resident_;
    at::Tensor observation_, bank_, inverse_a_, inverse_b_, inverse_d_;
    std::vector<Group> groups_;
#if DATAPUMP_TRIAD_TENSOR_CUDA
    std::unique_ptr<CudaComplexMatrix> matrix_;
    std::unique_ptr<HalfFrequency> half_;
#endif
};

class TensorFine final : public FineBulk {
public:
    TensorFine(std::shared_ptr<Arena> arena, std::shared_ptr<BulkReceipt> backend_work,
               bool precise, TensorCoarseMath coarse_math, bool leased)
        : device_(std::move(arena), std::move(backend_work), precise, leased), coarse_math_(coarse_math) {}

    void statistics(const FineBulkBlock& block, std::size_t candidate_count, const FineMappingSource& source,
                    std::span<FineBulkStatistics> result, std::stop_token stop) override {
        require(block.rows && block.taps && block.taps <= 2 && !block.projection_indices.empty() &&
                block.projected_y.size() == block.projection_indices.size() &&
                block.projection_indices.size() <= block.rows && candidate_count && candidate_count <= maximum_states &&
                result.size() == candidate_count && source && std::isfinite(block.variance) && block.variance > 0,
                "invalid Robust tensor fine block geometry");
        TensorContext::Operation op(device_);
        cancelled(stop);
        const auto columns = 2 * block.taps;
        const auto mode_count = block.projection_indices.size();
        const auto dc = mode_count == 1 && block.projection_indices.front() == 0;
        const auto cuda = device_.arena->device.is_cuda();
        // A singleton DC Gram is one C128 product, including in precise
        // stages. Avoid creating a persistent ATen cuBLAS workspace for it.
        const auto direct_gram = cuda && mode_count <= maximum_direct_gram_modes && (!device_.precise || dc);
        require(std::is_sorted(block.projection_indices.begin(), block.projection_indices.end()) &&
                std::adjacent_find(block.projection_indices.begin(), block.projection_indices.end()) ==
                    block.projection_indices.end() &&
                block.projection_indices.back() < block.rows,
                "invalid Robust tensor fine projection bins");
        const auto rows_bytes = mul(block.rows, dc ? 32 : 16 * device_.item_bytes);
        const auto projection_bytes = mul(mode_count, 96);
        const auto map_bytes = mul(std::min(mapping_rows, block.rows), 224);
        const auto result_values = fine_result_values(block.taps);
        const auto result_bytes = mul(result_values + 1, sizeof(Complex)); // Packed payload and converted witness.
        // Charge the materialized conjugate and the tap-by-tap complex128
        // product. This short contiguous sum needs no global partial buffer.
        const auto direct_gram_bytes = direct_gram ? mul(mode_count, (block.taps + 1) * sizeof(Complex)) : 0;
        // The owned cuBLAS path also prices a contiguous C128 input copy,
        // should a future projection produce a strided view.
        const auto owned_gram_bytes = cuda && !direct_gram ? mul(mode_count, sizeof(Complex)) : 0;
        const auto per_lane = add(add(rows_bytes, projection_bytes), add(map_bytes, add(direct_gram_bytes, owned_gram_bytes)));
        const auto per_candidate = add(mul(columns, per_lane), result_bytes);
        const auto upload_bound = mul(maximum_pins * tile_samples, device_.item_bytes);
        const auto shared_bytes = add(add(mul(mode_count, 3 * sizeof(Complex)), 2 * mib), upload_bound);
        std::size_t gram_bytes = 0;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (cuda && !direct_gram) gram_bytes = CudaComplexMatrix::gram_workspace_bytes();
#endif
        if (device_.workspace->leased()) {
            const auto shared_minimum = add(add(shared_bytes, gram_bytes), per_candidate);
            if (!device_.workspace->fits(add(shared_minimum, cache_slots_ ? 0 : upload_bound))) {
                // No previous block's kernels outlive its result download.
                // Drop optional residents before waiting for a larger lease.
                bins_.reset();
                for (auto& offsets : offsets_) offsets.reset();
                tile_storage_ = at::Tensor{};
                slots_.clear();
                cache_charge_ = {};
                cache_slots_ = 0;
                const auto desired_candidates = mul(std::min(maximum_batch, candidate_count), per_candidate);
                const auto descriptor_bytes = add(mul(mode_count, 2 * sizeof(std::int64_t)),
                                                   mul(std::min(mapping_rows, block.rows), 3 * sizeof(double)));
                const auto preferred = add(add(shared_bytes, gram_bytes),
                    add(add(desired_candidates, mul(preferred_cache_slots(), tile_samples * device_.item_bytes)),
                        descriptor_bytes));
                device_.workspace->begin(add(shared_minimum, upload_bound), preferred, stop);
            }
        }
        Charge shared(device_.workspace, shared_bytes);
        Charge gram_charge;
        at::Tensor gram_workspace;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (gram_bytes) {
            gram_charge = Charge(device_.workspace, gram_bytes);
            gram_workspace = at::empty({dim(gram_bytes)}, device_.options.dtype(at::kByte));
            if (!gram_) gram_ = std::make_unique<CudaComplexMatrix>();
        }
#endif
        reserve_cache(per_candidate);
        at::Tensor bins;
        if (!dc) bins = projection_bins(block, op.work);
        const auto y = device_.upload(block.projected_y, at::kComplexDouble, at::kComplexDouble, op.work);
        for (std::size_t first = 0; first < candidate_count;) {
            cancelled(stop);
            const auto [width, batch_charge] = admit(std::min(maximum_batch, candidate_count - first), per_candidate);
            const auto lanes = mul(width, columns);
            auto raw = at::zeros({dim(lanes), dim(block.rows)}, device_.complex_options);
            FusedMappingWork fused;
            for (std::size_t row = 0; row < block.rows;) {
                cancelled(stop);
                const auto rows = std::min(mapping_rows, block.rows - row);
                auto window = raw.narrow(1, dim(row), dim(rows));
                std::vector<Pending> pending;
                pending.reserve(lanes);
                std::vector<double> positions;
                std::array<bool, maximum_batch * 4> assigned{};
                std::vector<bool> protected_slots;
                std::size_t protected_count = 0;
                auto flush = [&] {
                    if (!pending.empty()) render(window, positions, pending, op.work, fused);
                    pending.clear();
                    std::fill(protected_slots.begin(), protected_slots.end(), false);
                    protected_count = 0;
                };
                for (std::size_t i = 0; i < width; ++i) {
                    source(first + i, row, rows,
                        [&](std::span<const double> offsets, std::span<const BulkMappedColumn> mapped,
                            std::span<const BulkDirectColumn> direct) {
                            cancelled(stop);
                            require(offsets.size() == rows, "Robust tensor mapping offset extent mismatch");
                            if (positions.empty()) {
                                for (std::size_t j = 0; j < offsets.size(); ++j)
                                    require(std::isfinite(offsets[j]) && offsets[j] >= 0 && offsets[j] <= 1'048'576 &&
                                            (!j || offsets[j] > offsets[j - 1]), "invalid Robust tensor mapping offsets");
                                positions.assign(offsets.begin(), offsets.end());
                            }
                            else require(std::equal(positions.begin(), positions.end(), offsets.begin()),
                                         "Robust tensor batch mappings require common offsets");
                            std::size_t incoming = 0;
                            for (const auto& descriptor : mapped) incoming = add(incoming, descriptor.tiles.size());
                            require(incoming <= maximum_pins, "Robust tensor mapping exceeds the native pin lease");
                            if (incoming) {
                                initialize_cache();
                                protected_slots.resize(slots_.size(), false);
                                if (incoming > slots_.size() - protected_count) flush();
                            }
                            auto mark = [&](std::size_t column) {
                                require(column < columns && !assigned[i * columns + column],
                                        "duplicate or invalid Robust tensor mapping column");
                                assigned[i * columns + column] = true;
                                return i * columns + column;
                            };
                            std::vector<TileUpload> uploads;
                            uploads.reserve(incoming);
                            try {
                                for (const auto& descriptor : mapped) {
                                    require(!descriptor.tiles.empty() && std::isfinite(descriptor.fraction) &&
                                            descriptor.fraction >= 0 && descriptor.fraction < 1 &&
                                            std::isfinite(descriptor.scale) && descriptor.scale > 0 &&
                                            std::isfinite(descriptor.phase) && std::isfinite(descriptor.phase_step),
                                            "invalid Robust tensor mapped coordinates");
                                    // Native prepared mappings bound their relative coordinate and
                                    // phase. Check the arithmetic before a signed tensor index add.
                                    const auto lo = descriptor.fraction + descriptor.scale * offsets.front();
                                    const auto hi = descriptor.fraction + descriptor.scale * offsets.back();
                                    require(std::isfinite(lo) && std::isfinite(hi) &&
                                            std::abs(lo) <= 0x1p31 && std::abs(hi) <= 0x1p31 &&
                                            std::abs(descriptor.phase + descriptor.phase_step * offsets.front()) <= 0x1p30 &&
                                            std::abs(descriptor.phase + descriptor.phase_step * offsets.back()) <= 0x1p30 &&
                                            descriptor.origin >= INT64_MIN + INT64_C(2147483651) &&
                                            descriptor.origin <= INT64_MAX - INT64_C(2147483651),
                                            "Robust tensor mapped index outside bounded geometry");
                                    auto& entry = pending.emplace_back(Pending{mark(descriptor.output_column),
                                        descriptor.origin, descriptor.fraction, descriptor.scale,
                                        descriptor.phase, descriptor.phase_step, {}});
                                    entry.slots.reserve(descriptor.tiles.size());
                                    for (const auto& tile : descriptor.tiles) {
                                        const auto slot = resident_slot(tile, protected_slots, uploads, op.work);
                                        entry.slots.push_back(dim(slot));
                                        if (!protected_slots[slot]) {
                                            protected_slots[slot] = true;
                                            ++protected_count;
                                        }
                                    }
                                }
                                upload_tiles(uploads, op.work);
                            } catch (...) {
                                for (const auto& upload : uploads) slots_[upload.destination] = {};
                                throw;
                            }
                            for (const auto& column : direct) {
                                require(column.samples.size() == rows && !column.reason.empty(),
                                        "invalid Robust tensor direct fallback column");
                                device_.put_complex(window.select(0, dim(mark(column.output_column))),
                                                    column.samples, op.work);
                                count(op.work.direct_columns, 1);
                                count(op.work.direct_values, rows);
                                const auto match = std::find(bulk_direct_fallback_reasons.begin(),
                                    bulk_direct_fallback_reasons.end(), column.reason);
                                const auto reason = std::min<std::size_t>(match - bulk_direct_fallback_reasons.begin(),
                                    bulk_direct_fallback_reasons.size() - 1);
                                count(op.work.direct_fallback_counts[reason], 1);
                            }
                        });
                }
                flush();
                row += rows;
            }

            const auto raw_nonzero = at::any(raw.ne(0).reshape({dim(width), -1}), -1);
            auto projected = dc ? raw.sum(-1, true, at::kComplexDouble) / std::sqrt(static_cast<double>(block.rows)) :
                device_.fft(raw, block.rows, false, op.work, nullptr, true).index_select(-1, bins);
            if (dc) count(op.work.dc_reduction_values, mul(lanes, block.rows));
            projected = projected.reshape({dim(2 * width), dim(block.taps), dim(mode_count)});
            const auto precise = projected.to(at::kComplexDouble);
            const auto normalized_correlation = (precise.conj() * y).sum(-1) / block.variance;
            const auto gram = gram_matrix(precise, direct_gram, gram_workspace);
            const auto normalized_gram = (gram + gram.transpose(1, 2).conj()) * (.5 / block.variance);

            // Preserve all statistics and the exact 0/1 witness in one blocking result transfer.
            const auto packed = at::cat({normalized_correlation.flatten(), normalized_gram.flatten(),
                                         raw_nonzero.to(at::kComplexDouble)});
            std::vector<Complex> host_values(mul(width, result_values));
            const auto host = std::span(host_values);
            device_.download(packed, host, at::kComplexDouble, op.work);
            const auto gram_count = columns * block.taps;
            const auto host_corr = host.first(lanes);
            const auto host_gram = host.subspan(lanes, width * gram_count);
            const auto host_nonzero = host.last(width);
            for (const auto value : host_corr)
                require(std::isfinite(value.real()) && std::isfinite(value.imag()), "Robust tensor correlation is nonfinite");
            for (const auto value : host_gram)
                require(std::isfinite(value.real()) && std::isfinite(value.imag()), "Robust tensor Gram is nonfinite");
            for (std::size_t i = 0; i < width; ++i) {
                auto& out = result[first + i];
                out = {};
                out.nonzero_basis = host_nonzero[i] != Complex{};
                std::copy_n(host_corr.begin() + i * columns, columns, out.correlations.begin() + block.taps);
                std::copy_n(host_gram.begin() + i * gram_count, gram_count,
                            out.gram.begin() + block.taps * block.taps);
            }
            count(op.work.statistics_blocks, width);
            count(op.work.projected_values, mul(lanes, mode_count));
            count(op.work.cuda_fused_mapped_launches, fused.launches);
            count(op.work.cuda_fused_mapped_values, fused.values);
            if (cuda) count(direct_gram ? op.work.cuda_direct_gram_values : op.work.cuda_bmm_gram_values,
                            mul(width, gram_count));
            op.work.executed = true;
            cancelled(stop);
            first += width;
        }
    }

    BulkReceipt receipt() const override {
        auto report = device_.receipt();
        // This is the owning backend's request, not fine-stage arithmetic.
        // CUDA arenas may be shared by backends with different policies.
        report.requested_coarse_math = coarse_math_name(coarse_math_);
        return report;
    }
    FineBulkWork completed_work() const override { return device_.fine_work(); }

private:
    struct FusedMappingWork {
        std::uint64_t launches = 0, values = 0;
    };

    struct Slot {
        std::weak_ptr<const void> owner;
        const Complex* samples = nullptr;
        std::uint64_t used = 0;
    };

    struct TileUpload {
        std::span<const Complex> samples;
        std::size_t destination;
    };

    struct Pending {
        std::size_t output;
        std::int64_t origin;
        double fraction, scale, phase, phase_step;
        std::vector<std::int64_t> slots;
    };

    at::Tensor gram_matrix(const at::Tensor& values, bool direct, const at::Tensor& workspace) {
        if (direct) return (values.conj().resolve_conj().unsqueeze(2) * values.unsqueeze(1)).sum(-1);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (device_.arena->device.is_cuda()) {
            require(gram_ && workspace.defined(), "Robust owned CUDA Gram workspace was not prepared");
            const auto input = values.contiguous();
            auto output = at::empty({input.size(0), input.size(1), input.size(1)},
                                    device_.options.dtype(at::kComplexDouble));
            gram_->gram_c128(input.const_data_ptr<c10::complex<double>>(), output.mutable_data_ptr<c10::complex<double>>(),
                static_cast<std::size_t>(input.size(0)), static_cast<std::size_t>(input.size(1)),
                static_cast<std::size_t>(input.size(2)), workspace.mutable_data_ptr(),
                static_cast<std::size_t>(workspace.nbytes()), device_.cuda_stream());
            // cuBLAS writes each Hermitian Gram column-major. A view supplies
            // the existing logical [batch,tap,tap] order without a copy.
            return output.transpose(1, 2);
        }
#else
        (void)workspace;
#endif
        return at::bmm(values.conj(), values.transpose(1, 2));
    }

    template<class T>
    struct DescriptorCache {
        // Keep reservation until both device tensor and retained host key die.
        Charge charge;
        at::Tensor tensor;
        std::vector<T> key;
        std::size_t rows = 0;
        std::uint64_t used = 0;
    };

    at::Tensor projection_bins(const FineBulkBlock& block, BulkReceipt& work) {
        if (bins_ && bins_->rows == block.rows &&
            std::equal(bins_->key.begin(), bins_->key.end(), block.projection_indices.begin(), block.projection_indices.end())) {
            count(work.projection_index_cache_hits, 1);
            return bins_->tensor;
        }
        std::vector<std::int64_t> indices;
        indices.reserve(block.projection_indices.size());
        for (const auto index : block.projection_indices) indices.push_back(dim(index));
        auto next = std::make_unique<DescriptorCache<std::size_t>>();
        try {
            next->charge = Charge(device_.workspace, mul(block.projection_indices.size(), sizeof(std::int64_t)));
        } catch (const ReceiverWorkspaceError&) {
            // The operation's shared projection bound already covers one
            // transient index tensor. Caching must not become an admission
            // prerequisite when another stage consumes arena headroom.
            count(work.projection_index_cache_uploads, 1);
            return device_.upload(std::span<const std::int64_t>(indices), at::kLong, at::kLong, work);
        }
        next->key.assign(block.projection_indices.begin(), block.projection_indices.end());
        next->rows = block.rows;
        next->tensor = device_.upload(std::span<const std::int64_t>(indices), at::kLong, at::kLong, work);
        // Replacement is transactional; the old tensor and charge stay live
        // until its successor's blocking upload has completed.
        bins_ = std::move(next);
        count(work.projection_index_cache_uploads, 1);
        return bins_->tensor;
    }

    at::Tensor mapping_offsets(std::span<const double> positions, BulkReceipt& work) {
        std::size_t replace = 0;
        for (std::size_t i = 0; i < offsets_.size(); ++i) {
            auto& cached = offsets_[i];
            if (cached && std::equal(cached->key.begin(), cached->key.end(), positions.begin(), positions.end())) {
                cached->used = ++offset_age_;
                count(work.mapping_offset_cache_hits, 1);
                return cached->tensor;
            }
            if (!cached || (offsets_[replace] && cached->used < offsets_[replace]->used)) replace = i;
        }
        auto next = std::make_unique<DescriptorCache<double>>();
        try {
            next->charge = Charge(device_.workspace, positions.size_bytes());
        } catch (const ReceiverWorkspaceError&) {
            // The admitted mapped batch already prices this transient tensor.
            count(work.mapping_offset_cache_uploads, 1);
            return device_.upload(positions, at::kDouble, at::kDouble, work);
        }
        next->key.assign(positions.begin(), positions.end());
        next->tensor = device_.upload(positions, at::kDouble, at::kDouble, work);
        next->used = ++offset_age_;
        offsets_[replace] = std::move(next);
        count(work.mapping_offset_cache_uploads, 1);
        return offsets_[replace]->tensor;
    }

    std::pair<std::size_t, Charge> admit(std::size_t maximum, std::size_t per_candidate) {
        for (auto width = maximum; width; width /= 2) {
            try {
                return {width, Charge(device_.workspace, mul(width, per_candidate))};
            } catch (const ReceiverWorkspaceError&) {
                if (width == 1) {
                    if (!bins_ && !offsets_[0] && !offsets_[1]) throw;
                    // Previous batches have completed their blocking result
                    // readback. The current bins reference is still covered
                    // by the shared transient projection reservation.
                    bins_.reset();
                    for (auto& offsets : offsets_) offsets.reset();
                    return {1, Charge(device_.workspace, per_candidate)};
                }
            }
        }
        throw ReceiverWorkspaceError("Robust tensor fine operation cannot admit one candidate");
    }

    std::size_t preferred_cache_slots() const {
        return std::clamp(device_.arena->options.workspace_bytes / 8 / (tile_samples * device_.item_bytes),
                          maximum_pins, std::size_t{256});
    }

    void reserve_cache(std::size_t one_candidate) {
        if (cache_slots_) return;
        // Price one actual candidate before choosing the resident cache.
        // Hold the resulting cache lease across every later batch admission.
        Charge minimum_batch(device_.workspace, one_candidate);
        const auto tile_bytes = tile_samples * device_.item_bytes;
        auto slots = preferred_cache_slots();
        for (;;) {
            try {
                cache_charge_ = Charge(device_.workspace, mul(slots, tile_bytes));
                break;
            } catch (const ReceiverWorkspaceError&) {
                if (slots == maximum_pins) throw;
                slots = std::max(maximum_pins, slots / 2);
            }
        }
        cache_slots_ = slots;
    }

    void initialize_cache() {
        if (!slots_.empty()) return;
        tile_storage_ = at::empty({dim(cache_slots_), dim(tile_samples)}, device_.complex_options);
        slots_.resize(cache_slots_);
    }

    std::size_t resident_slot(const BulkMappedTile& tile, const std::vector<bool>& protected_slots,
                              std::vector<TileUpload>& uploads, BulkReceipt& work) {
        require(tile.owner && tile.samples.size() == tile_samples, "invalid Robust tensor native tile lease");
        std::size_t available = slots_.size();
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& entry = slots_[i];
            if (entry.samples == tile.samples.data() && !entry.owner.owner_before(tile.owner) &&
                !tile.owner.owner_before(entry.owner)) {
                slots_[i].used = ++age_;
                count(work.tile_hits, 1);
                return i;
            }
            if (!protected_slots[i] && (available == slots_.size() || entry.used < slots_[available].used))
                available = i;
        }
        require(available != slots_.size(), "Robust tensor cache exhausted all protected slots");
        uploads.push_back({tile.samples, available});
        slots_[available] = {tile.owner, tile.samples.data(), ++age_};
        return available;
    }

    void upload_tiles(std::span<const TileUpload> uploads, BulkReceipt& work) {
        if (uploads.empty()) return;
        std::vector<Complex> host(mul(uploads.size(), tile_samples));
        std::array<std::int64_t, maximum_pins> indices{};
        for (std::size_t i = 0; i < uploads.size(); ++i) {
            std::copy(uploads[i].samples.begin(), uploads[i].samples.end(), host.begin() + i * tile_samples);
            indices[i] = dim(uploads[i].destination);
        }
        const auto values = device_.upload(std::span<const Complex>(host), at::kComplexDouble, device_.type, work)
            .reshape({dim(uploads.size()), dim(tile_samples)});
        const auto destinations = device_.upload(std::span<const std::int64_t>(indices).first(uploads.size()),
            at::kLong, at::kLong, work);
        tile_storage_.index_copy_(0, destinations, values);
        count(work.uploaded_tiles, uploads.size());
    }

    void render(at::Tensor output, std::span<const double> positions, std::span<const Pending> pending,
                BulkReceipt& work, [[maybe_unused]] FusedMappingWork& fused) {
        constexpr std::size_t descriptors = maximum_batch * 4;
        constexpr std::size_t table_width = 3 + maximum_pins;
        std::array<double, descriptors * 4> coefficients{};
        std::array<std::int64_t, descriptors * table_width> table{};
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto& item = pending[i];
            coefficients[4 * i] = item.fraction;
            coefficients[4 * i + 1] = item.scale;
            coefficients[4 * i + 2] = item.phase;
            coefficients[4 * i + 3] = item.phase_step;
            table[table_width * i] = item.origin;
            table[table_width * i + 1] = dim(item.slots.size() * tile_samples);
            table[table_width * i + 2] = dim(item.output);
            std::copy(item.slots.begin(), item.slots.end(), table.begin() + i * table_width + 3);
        }
        const auto lanes = pending.size();
        const auto maps = device_.upload(std::span<const double>(coefficients).first(lanes * 4),
            at::kDouble, at::kDouble, work).reshape({dim(lanes), 4});
        const auto indices = device_.upload(std::span<const std::int64_t>(table).first(lanes * table_width),
            at::kLong, at::kLong, work).reshape({dim(lanes), dim(table_width)});
        const auto offsets = mapping_offsets(positions, work);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (device_.arena->device.is_cuda() && !device_.precise) {
            static_assert(tile_samples == cuda_mapping_tile_samples && maximum_pins == cuda_mapping_maximum_pins);
            // Every allocation and the launch use the Operation's same guarded
            // stream, so the CUDA allocator can retire the descriptor tensors
            // in stream order after this scope. No cross-stream use is added.
            C10_CUDA_CHECK(launch_cuda_mapped_c64(maps.const_data_ptr<double>(),
                indices.const_data_ptr<std::int64_t>(), offsets.const_data_ptr<double>(),
                tile_storage_.const_data_ptr<c10::complex<float>>(), output.mutable_data_ptr<c10::complex<float>>(),
                {dim(positions.size()), dim(lanes), tile_storage_.size(0), output.size(0),
                 output.stride(0), output.stride(1)}, device_.cuda_stream()));
            count(fused.launches, 1);
            count(fused.values, mul(lanes, positions.size()));
        } else
#endif
        {
            render_aten_mapping(output, tile_storage_, maps, indices, offsets, device_.real_type,
                                dim(tile_samples), dim(maximum_pins));
        }
        count(work.mapped_columns, lanes);
        count(work.mapped_values, mul(lanes, positions.size()));
        work.executed = true;
    }

    TensorContext device_;
    TensorCoarseMath coarse_math_;
    Charge cache_charge_;
    std::size_t cache_slots_ = 0;
    at::Tensor tile_storage_;
    std::vector<Slot> slots_;
    std::uint64_t age_ = 0;
    std::unique_ptr<DescriptorCache<std::size_t>> bins_;
    std::array<std::unique_ptr<DescriptorCache<double>>, 2> offsets_;
    std::uint64_t offset_age_ = 0;
#if DATAPUMP_TRIAD_TENSOR_CUDA
    std::unique_ptr<CudaComplexMatrix> gram_;
#endif
};

class TensorBackend final : public ReceiverBulkBackend {
public:
    explicit TensorBackend(const TensorReceiverOptions& options, bool automatic = false)
        : arena_(arena_for(options, automatic)), precision_(options.precision),
          coarse_math_(options.coarse_math), automatic_(automatic) {
        (void)coarse_math_name(coarse_math_);
        require(precision_ != TensorPrecision::fp32_minimum || !requests_matrix(coarse_math_),
                "Robust FP32 minimum conflicts with requested reduced-precision coarse contractions");
    }

    bool supports_exact_device_chirp_z() const noexcept override { return automatic_; }

    std::string_view fine_fallback_reason(const FineBulkGeometry& geometry) const noexcept override {
        // Small independent transforms and sparse projections lose their
        // advantage to the synchronous per-block CUDA preparation/readback.
        // Keep the native route within the measured AVX2 worker/shape class.
        if (automatic_ && precision_ != TensorPrecision::fp64 && geometry.native_avx2 &&
            geometry.workers >= 26 && geometry.taps == 1 &&
            geometry.maximum_block_rows && geometry.maximum_block_rows <= 1024 &&
            geometry.maximum_projection_modes && geometry.maximum_projection_modes <= 40)
            return small_fine_fallback;
        return {};
    }

    void record_native_fine(std::string_view reason) override {
        require(reason == small_fine_fallback, "invalid native fine routing reason for Robust CUDA");
        std::lock_guard lock(arena_->mutex);
        count(work_->native_fine_sessions, 1);
    }

    std::unique_ptr<FineBulk> make_fine(double rate, double clock_span) override {
        return std::make_unique<TensorFine>(arena_, work_, precise(rate, clock_span), coarse_math_, automatic_);
    }

    std::unique_ptr<CoarseBulk> make_coarse(double rate, double clock_span) override {
        return std::make_unique<TensorCoarse>(arena_, work_, precise(rate, clock_span), precision_, coarse_math_, automatic_);
    }

    std::size_t maximum_frequency_tile_width(const CoarseBulkGeometry& geometry, std::size_t workspaces,
        std::size_t requested_width, double clock_span) const override {
        if (!automatic_) return requested_width;
        const auto item_bytes = precise(geometry.projected_sample_rate_hz, clock_span) ? 16 : 8;
        return TensorCoarse::maximum_frequency_tile_width(geometry, workspaces, requested_width, item_bytes,
            arena_->options.workspace_bytes - arena_->existing_allocations);
    }

    std::size_t fine_host_bytes(std::size_t block_rows, std::size_t taps) const override {
        const auto host = add(mul(block_rows, sizeof(std::int64_t) + sizeof(Complex)),
                              mul(16'384, 2 * taps * sizeof(Complex)));
        const auto result_staging = mul(maximum_batch * fine_result_values(taps), 2 * sizeof(Complex));
        const auto staging = add(2 * maximum_pins * tile_samples * sizeof(Complex), result_staging);
        // Current keys plus their transactional replacements are bounded by
        // the public maximum block geometry, never by observed contents.
        const auto descriptor_keys = add(mul(block_rows, 2 * sizeof(std::size_t)),
                                         mul(std::min(mapping_rows, block_rows), 3 * sizeof(double)));
        return add(add(add(add(host, descriptor_keys), 2 * mib), staging),
                   arena_->device.is_cpu() ? arena_->options.workspace_bytes : 0);
    }

    std::size_t coarse_host_bytes(std::size_t maximum_fft_rows, std::size_t maximum_tile_frequencies,
                                  std::size_t arrivals) const override {
        const auto rows = mul(maximum_batch, maximum_tile_frequencies);
        const auto full_scores = mul(mul(rows, arrivals), 2 * sizeof(double));
        // Preserve the full-score fallback quote. For short arrival vectors,
        // the downloaded POD tensor and copied frontier vector can be larger.
        const auto frontiers = arena_->device.is_cuda() ? mul(rows, 2 * sizeof(CoarseBulkPeaks)) : 0;
        const auto scores = std::max(full_scores, frontiers);
        const auto staging = add(mul(maximum_fft_rows, 3 * sizeof(Complex)), 2 * maximum_upload_bytes);
        // One optional cuBLAS handle and bounded shape descriptors; no host
        // coefficient matrix is retained. Device packing remains arena-charged.
        // The half route keeps at most three cuFFT plan handles and appends a
        // diagnostic word to the existing host frontier packet.
        const auto half = coarse_math_ == TensorCoarseMath::fp16_fft ||
            (automatic_ && precision_ == TensorPrecision::automatic && automatic_half_fft_hardware(arena_->total));
        const auto metadata = !arena_->device.is_cuda() ? 0 :
            half ? 6 * mib + 2 * sizeof(CoarseBulkPeaks) :
            requests_matrix(coarse_math_) ? 2 * mib : 0;
        return add(add(add(scores, staging), metadata), arena_->device.is_cpu() ? arena_->options.workspace_bytes : 0);
    }

    BulkReceipt receipt() const override { return report(true); }
    BulkReceipt status_receipt() const override { return report(false); }

private:
    BulkReceipt report(bool query_runtime) const {
        auto report = arena_->receipt(query_runtime, work_.get());
        report.precision = precision_ == TensorPrecision::fp64 ? "fp64" :
            precision_ == TensorPrecision::fp32_minimum ? "fp32_minimum" : "automatic";
        report.requested_coarse_math = coarse_math_name(coarse_math_);
        if (automatic_) report.selection_reason = "cuda-discrete-gpu";
        if (report.native_fine_sessions) report.native_fine_fallback_reason = small_fine_fallback;
        return report;
    }

    bool precise(double rate, double clock_span) const {
        require(std::isfinite(rate) && rate > 0 && std::isfinite(clock_span) && clock_span >= 0,
                "invalid Robust tensor public precision geometry");
        return precision_ == TensorPrecision::fp64 || (rate <= 10 && clock_span > 0 && clock_span <= .001);
    }

    std::shared_ptr<Arena> arena_;
    TensorPrecision precision_;
    TensorCoarseMath coarse_math_;
    bool automatic_;
    std::shared_ptr<BulkReceipt> work_ = std::make_shared<BulkReceipt>();
};

} // namespace
} // namespace receiver_detail

std::shared_ptr<receiver_detail::ReceiverBulkBackend> make_tensor_receiver_backend(const TensorReceiverOptions& options) {
    try {
        return std::make_shared<receiver_detail::TensorBackend>(options);
    } catch (const c10::OutOfMemoryError&) {
        throw ReceiverWorkspaceError("allocation failed during Robust tensor backend initialization");
    } catch (const c10::Error& error) {
        if (options.device == TensorDevice::cuda)
            throw TensorBackendUnavailable("Robust CUDA initialization failed: " +
                std::string(error.what_without_backtrace()).substr(0, 512));
        throw;
    }
}

std::shared_ptr<receiver_detail::ReceiverBulkBackend> make_automatic_cuda_receiver_backend(TensorPrecision precision) {
#if DATAPUMP_TRIAD_TENSOR_CUDA
    int count = 0;
    if (const auto status = cudaGetDeviceCount(&count); status != cudaSuccess)
        throw TensorBackendUnavailable("Robust CUDA device discovery failed: " + std::string(cudaGetErrorString(status)));

    struct Candidate {
        unsigned index;
        std::uint64_t parallel_clock;
        std::size_t memory;
    };
    std::vector<Candidate> candidates;
    for (int index = 0; index < count && index <= std::numeric_limits<c10::DeviceIndex>::max(); ++index) {
        cudaDeviceProp properties{};
        if (cudaGetDeviceProperties(&properties, index) != cudaSuccess || properties.integrated ||
            properties.computeMode == cudaComputeModeProhibited) continue;
        // Rank visible, usable devices with a simple throughput proxy and
        // then memory capacity. Receiver qualification uses elapsed service.
        candidates.push_back({static_cast<unsigned>(index),
            static_cast<std::uint64_t>(properties.multiProcessorCount) * properties.clockRate,
            properties.totalGlobalMem});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.parallel_clock != b.parallel_clock) return a.parallel_clock > b.parallel_clock;
        return a.memory > b.memory;
    });
    std::string reason;
    std::exception_ptr workspace_failure;
    for (const auto& candidate : candidates) {
        try {
            return std::make_shared<receiver_detail::TensorBackend>(
                TensorReceiverOptions{TensorDevice::cuda, candidate.index, 0, precision, TensorCoarseMath::fft}, true);
        } catch (const c10::OutOfMemoryError&) {
            workspace_failure = std::make_exception_ptr(ReceiverWorkspaceError("allocation failed during Robust CUDA initialization"));
        } catch (const ReceiverWorkspaceError&) {
            workspace_failure = std::current_exception();
        } catch (const c10::Error& error) {
            reason = std::string(error.what_without_backtrace()).substr(0, 512);
        } catch (const TensorBackendUnavailable& error) {
            reason = error.what();
        }
    }
    if (workspace_failure) std::rethrow_exception(workspace_failure);
    if (!reason.empty()) throw TensorBackendUnavailable("Robust CUDA initialization failed: " + reason);
    throw TensorBackendUnavailable("No compatible discrete CUDA device is available");
#else
    (void)precision;
    throw TensorBackendUnavailable("Robust CUDA requested with a CPU-only LibTorch backend");
#endif
}

} // namespace datapump::triad::v3
