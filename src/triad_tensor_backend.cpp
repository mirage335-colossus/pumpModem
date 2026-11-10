#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_tensor_mapping.hpp"

#include <ATen/ATen.h>
#include <ATen/Parallel.h>
#include <c10/core/InferenceMode.h>
#include <torch/version.h>

#if DATAPUMP_TRIAD_TENSOR_CUDA
#include "triad_tensor_mapping.cuh"

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
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <numeric>
#include <type_traits>

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
constexpr auto tau = 2 * std::numbers::pi;

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
    TRIAD_COUNT(statistics_blocks);
    TRIAD_COUNT(projected_values);
    TRIAD_COUNT(dc_reduction_values);
    TRIAD_COUNT(mapped_columns);
    TRIAD_COUNT(mapped_values);
    TRIAD_COUNT(cuda_fused_mapped_launches);
    TRIAD_COUNT(cuda_fused_mapped_values);
    TRIAD_COUNT(cuda_direct_gram_values);
    TRIAD_COUNT(cuda_bmm_gram_values);
    TRIAD_COUNT(uploaded_tiles);
    TRIAD_COUNT(tile_hits);
    TRIAD_COUNT(direct_columns);
    TRIAD_COUNT(direct_values);
#undef TRIAD_COUNT
    for (std::size_t i = 0; i < to.direct_fallback_counts.size(); ++i)
        count(to.direct_fallback_counts[i], from.direct_fallback_counts[i]);
    to.wall_seconds += from.wall_seconds;
    to.executed |= from.executed;
}

struct Arena {
    TensorReceiverOptions options;
    at::Device device{at::kCPU};
    BulkReceipt total;
    mutable std::mutex mutex;

    explicit Arena(const TensorReceiverOptions& requested) : options(requested) {
        if (options.workspace_bytes < mib || options.workspace_bytes > static_cast<std::uint64_t>(INT64_MAX))
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
        // Reserve separate driver/library headroom outside the shared tensor
        // allocation budget. The allocator cap includes its retained blocks.
        const auto headroom = std::min<std::size_t>(256 * mib, total.total_device_bytes / 20);
        total.runtime_headroom_bytes = headroom;
        if (total.initial_free_device_bytes <= headroom ||
            options.workspace_bytes > total.initial_free_device_bytes - headroom)
            throw ReceiverWorkspaceError("Robust CUDA workspace exceeds free device memory with runtime headroom");
        c10::cuda::CUDACachingAllocator::init(devices);
        // Select the native caching allocator, whose fraction and reserved
        // block statistics correspond to the admission receipt below.
        require(c10::cuda::CUDACachingAllocator::get()->name() == "native",
                "Robust CUDA requires ATen's native caching allocator");
        const auto prior = c10::cuda::CUDACachingAllocator::getDeviceStats(device.index());
        if (prior.reserved_bytes[0].current > static_cast<std::int64_t>(options.workspace_bytes))
            throw ReceiverWorkspaceError("existing CUDA allocator reservations exceed the Robust workspace");
        const auto fraction = static_cast<double>(options.workspace_bytes) / total.total_device_bytes;
        c10::cuda::CUDACachingAllocator::setMemoryFraction(std::nextafter(fraction, 0.), device.index());
        // ATen allocates the FFT work area through its caching allocator.
        // Bound the number of plan objects retained in the separate cache.
        total.fft_plan_cache_limit = 4;
        at::detail::getCUDAHooks().cuFFTSetPlanCacheMaxSize(device.index(), dim(total.fft_plan_cache_limit));
        C10_CUDA_CHECK(cudaRuntimeGetVersion(&total.cuda_runtime_version));
        C10_CUDA_CHECK(cudaDriverGetVersion(&total.cuda_driver_version));
        total.device = "cuda:" + std::to_string(options.device_index);
        total.device_name = properties.name;
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
        std::lock_guard lock(mutex);
        total.reserved_bytes -= bytes;
    }

    BulkReceipt receipt() const {
        std::lock_guard lock(mutex);
        auto out = total;
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (device.is_cuda()) {
            const auto stats = c10::cuda::CUDACachingAllocator::getDeviceStats(device.index());
            out.allocator_live_bytes = stats.allocated_bytes[0].current;
            out.allocator_reserved_bytes = stats.reserved_bytes[0].current;
            out.allocator_peak_live_bytes = stats.allocated_bytes[0].peak;
            out.allocator_peak_reserved_bytes = stats.reserved_bytes[0].peak;
            out.fft_plan_cache_count = at::detail::getCUDAHooks().cuFFTGetPlanCacheSize(device.index());
        }
#endif
        return out;
    }
};

std::shared_ptr<Arena> arena_for(const TensorReceiverOptions& options) {
    if (options.device == TensorDevice::cpu) return std::make_shared<Arena>(options);
    // CUDA's allocator limit is process/device scoped. Every receiver using
    // this device shares one charge instead of multiplying the configured cap.
    static std::mutex mutex;
    static std::map<unsigned, std::weak_ptr<Arena>> devices;
    std::lock_guard lock(mutex);
    auto& existing = devices[options.device_index];
    if (auto arena = existing.lock()) {
        require(arena->options.workspace_bytes == options.workspace_bytes,
                "active Robust CUDA receivers must agree on the device workspace");
        return arena;
    }
    auto arena = std::make_shared<Arena>(options);
    existing = arena;
    return arena;
}

class Charge {
public:
    Charge() = default;
    Charge(std::shared_ptr<Arena> arena, std::size_t bytes) : arena_(std::move(arena)), bytes_(bytes) {
        if (!arena_->reserve(bytes_))
            throw ReceiverWorkspaceError("Robust tensor operation exceeds the shared workspace");
    }
    Charge(Charge&& other) noexcept : arena_(std::move(other.arena_)), bytes_(other.bytes_) {}
    Charge& operator=(Charge&& other) noexcept {
        if (arena_) arena_->release(bytes_);
        arena_ = std::move(other.arena_);
        bytes_ = other.bytes_;
        return *this;
    }
    ~Charge() { if (arena_) arena_->release(bytes_); }

private:
    std::shared_ptr<Arena> arena_;
    std::size_t bytes_ = 0;
};

class TensorContext {
public:
    TensorContext(std::shared_ptr<Arena> arena, bool precise)
        : arena(std::move(arena)), precise(precise), type(precise ? at::kComplexDouble : at::kComplexFloat),
          real_type(precise ? at::kDouble : at::kFloat), item_bytes(precise ? 16 : 8),
          options(at::TensorOptions().device(this->arena->device)),
          complex_options(options.dtype(type)), double_options(options.dtype(at::kDouble))
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
            add_work(context_.arena->total, work);
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
        const auto transforms = static_cast<std::uint64_t>(input.numel() / input.size(-1));
        const auto total_rows = mul(transforms, rows);
        count(work.fft_calls, 1);
        count(work.fft_rows, total_rows);
        work.executed = true;
        if (coarse) {
            count(inverse ? coarse->inverse_ffts : coarse->forward_ffts, transforms);
            count(coarse->fft_rows, total_rows);
            count(coarse->fft_row_stages, mul(total_rows, std::bit_width(rows - 1)));
        }
        return result;
    }

    std::shared_ptr<Arena> arena;
    bool precise;
    at::ScalarType type, real_type;
    std::size_t item_bytes;
    at::TensorOptions options, complex_options, double_options;

private:
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
        : owner(&context), taps(count), rows(rows), resident(context.arena, mul(rows, context.item_bytes)) {}
};

class TensorCoarse final : public CoarseBulk {
public:
    TensorCoarse(std::shared_ptr<Arena> arena, bool precise) : device_(std::move(arena), precise) {}

    std::shared_ptr<const CoarseBulkFilter> prepare_filter(
        std::span<const double> taps, std::size_t fft_rows, CoarseBulkWork& work, std::stop_token stop) override {
        require(!taps.empty() && fft_rows >= taps.size(), "invalid Robust tensor FIR extent");
        TensorContext::Operation op(device_);
        cancelled(stop);
        auto filter = std::make_shared<Filter>(device_, taps.size(), fft_rows);
        Charge scratch(device_.arena, mul(fft_rows, 12 * device_.item_bytes));
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
        Charge output_charge(device_.arena, mul(output_bytes, 3));
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
        }
        require(!lag_ || covered == geometry.frequency_count, "Robust tensor lag groups do not cover the full grid");
        const auto bytes = add(mul(resident, device_.item_bytes), mul(bank.size(), 48));
        resident_ = Charge(device_.arena, bytes);
        Charge scratch(device_.arena, add(mul(largest_fft, 12 * device_.item_bytes), 2 * maximum_upload_bytes));
        geometry_ = geometry;
        count_ = bank.size();
        observation_ = device_.complex(observation, op.work);
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
            if (!group.convolution.empty()) {
                const auto impulse = device_.complex(group.convolution, op.work);
                convolution = device_.fft(impulse, group.convolution.size(), false, op.work, &work);
            }
            groups_.push_back({group.first_frequency, group.frequencies, std::move(phase), std::move(convolution)});
        }
        device_.synchronize(op.work);
        cancelled(stop);
        prepared_ = true;
    }

    void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink& sink,
        CoarseBulkWork& work, std::stop_token stop) override {
        TensorContext::Operation op(device_);
        require(prepared_ && !lag_ && !frequencies.empty() && frequencies.size() <= geometry_.frequency_count && sink,
                "invalid Robust tensor frequency tile");
        cancelled(stop);
        const auto fft_rows = geometry_.correlation_fft_rows;
        const auto spectrum_rows = mul(frequencies.size(), fft_rows);
        Charge observation_charge(device_.arena, mul(spectrum_rows, 16 * sizeof(Complex)));
        const auto position = at::arange(observation_.size(0), device_.double_options) / geometry_.projected_sample_rate_hz;
        const auto frequency = device_.upload(frequencies, at::kDouble, at::kDouble, op.work);
        auto cycles = -frequency.unsqueeze(1) * position;
        const auto angle = ((cycles - cycles.floor()) * tau).to(device_.real_type);
        const auto rotated = at::complex(angle.cos(), angle.sin()) * observation_;
        const auto spectra = device_.fft(rotated, fft_rows, false, op.work, &work);
        const auto per_workspace = add(mul(mul(spectrum_rows, geometry_.taps), 12 * device_.item_bytes),
                                       mul(mul(frequencies.size(), geometry_.lag_count), 96));
        for (std::size_t first = 0; first < count_;) {
            cancelled(stop);
            const auto [width, charge] = admit(std::min(maximum_batch, count_ - first),
                [&](std::size_t n) { return mul(n, per_workspace); });
            const auto bank = bank_.narrow(0, dim(first), dim(width)).unsqueeze(1);
            const auto product = bank * spectra.unsqueeze(0).unsqueeze(2);
            auto correlation = device_.fft(product, fft_rows, true, op.work, &work)
                .narrow(-1, 0, dim(geometry_.lag_count));
            publish(quadratic(correlation, first), first, width, frequencies.size(), sink, work, op.work);
            first += width;
        }
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
        const auto per_lag = add(mul(mul(largest_fft, geometry_.taps), 14 * device_.item_bytes),
                                 mul(geometry_.frequency_count, 112));
        const auto n = mul(geometry_.frequency_count, geometry_.lag_count);
        Charge output_charge(device_.arena, mul(n, 3 * sizeof(double)));
        auto scores = at::empty({1, dim(geometry_.frequency_count), dim(geometry_.lag_count)}, device_.double_options);
        for (std::size_t w = first_workspace; w < first_workspace + workspace_count; ++w) {
            const auto column = bank_.select(0, dim(w)).conj();
            for (std::size_t first = 0; first < geometry_.lag_count;) {
                cancelled(stop);
                const auto [width, charge] = admit(std::min<std::size_t>(32, geometry_.lag_count - first),
                    [&](std::size_t nrows) { return mul(nrows, per_lag); });
                const auto product = windows.narrow(0, dim(first), dim(width)).unsqueeze(1) * column;
                auto correlations = at::empty({dim(width), dim(geometry_.taps), dim(geometry_.frequency_count)},
                                              device_.complex_options);
                for (const auto& group : groups_) {
                    const auto input = product * group.phase;
                    auto destination = correlations.narrow(-1, dim(group.first), dim(group.count));
                    if (!group.convolution.defined()) {
                        destination.copy_(input.sum(-1, true, at::kComplexDouble));
                        count(work.singleton_dot_terms, mul(mul(width, geometry_.taps), window_rows));
                    } else {
                        const auto nfft = static_cast<std::size_t>(group.convolution.numel());
                        auto spectrum = device_.fft(input, nfft, false, op.work, &work);
                        spectrum.mul_(group.convolution);
                        destination.copy_(device_.fft(spectrum, nfft, true, op.work, &work)
                            .narrow(-1, 0, dim(group.count)));
                    }
                }
                const auto arranged = correlations.permute({2, 1, 0}).unsqueeze(0);
                scores.narrow(2, dim(first), dim(width)).copy_(quadratic(arranged, w));
                device_.synchronize(op.work);
                first += width;
            }
            publish(scores, w, 1, geometry_.frequency_count, sink, work, op.work);
        }
    }

    BulkReceipt receipt() const override { return device_.receipt(); }
    CoarseBulkCompletedWork completed_work() const override { return device_.coarse_work(); }

private:
    struct Group {
        std::size_t first, count;
        at::Tensor phase, convolution;
    };

    template<class Size>
    std::pair<std::size_t, Charge> admit(std::size_t maximum, const Size& size) const {
        for (auto width = maximum; width; width /= 2) {
            try {
                return {width, Charge(device_.arena, size(width))};
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
        require(std::all_of(host.begin(), host.end(), [](double value) { return std::isfinite(value); }),
                "Robust tensor coarse score is nonfinite");
        count(receipt.score_rows, host.size());
        count(work.score_rows, host.size());
        count(work.workspace_frequency_pairs, pairs);
        receipt.executed = true;
        sink(first, width, host);
    }

    TensorContext device_;
    CoarseBulkGeometry geometry_;
    std::size_t count_ = 0;
    bool lag_ = false, preparation_started_ = false, prepared_ = false;
    Charge resident_;
    at::Tensor observation_, bank_, inverse_a_, inverse_b_, inverse_d_;
    std::vector<Group> groups_;
};

class TensorFine final : public FineBulk {
public:
    TensorFine(std::shared_ptr<Arena> arena, bool precise) : device_(std::move(arena), precise) {}

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
        const auto direct_gram = cuda && !device_.precise && !dc && mode_count <= maximum_direct_gram_modes;
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
        const auto per_lane = add(add(rows_bytes, projection_bytes), add(map_bytes, direct_gram_bytes));
        const auto per_candidate = add(mul(columns, per_lane), result_bytes);
        const auto upload_bound = mul(maximum_pins * tile_samples, device_.item_bytes);
        Charge shared(device_.arena, add(add(mul(mode_count, 3 * sizeof(Complex)), 2 * mib), upload_bound));
        reserve_cache(per_candidate);
        at::Tensor bins;
        if (!dc) {
            std::vector<std::int64_t> indices;
            indices.reserve(mode_count);
            for (const auto index : block.projection_indices) indices.push_back(dim(index));
            bins = device_.upload(std::span<const std::int64_t>(indices), at::kLong, at::kLong, op.work);
        }
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
            const auto gram = direct_gram ? (precise.conj().resolve_conj().unsqueeze(2) * precise.unsqueeze(1)).sum(-1) :
                at::bmm(precise.conj(), precise.transpose(1, 2));
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

    BulkReceipt receipt() const override { return device_.receipt(); }
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

    std::pair<std::size_t, Charge> admit(std::size_t maximum, std::size_t per_candidate) const {
        for (auto width = maximum; width; width /= 2) {
            try {
                return {width, Charge(device_.arena, mul(width, per_candidate))};
            } catch (const ReceiverWorkspaceError&) {
                if (width == 1) throw;
            }
        }
        throw ReceiverWorkspaceError("Robust tensor fine operation cannot admit one candidate");
    }

    void reserve_cache(std::size_t one_candidate) {
        if (cache_slots_) return;
        // Price one actual candidate before choosing the resident cache.
        // Hold the resulting cache lease across every later batch admission.
        Charge minimum_batch(device_.arena, one_candidate);
        const auto tile_bytes = tile_samples * device_.item_bytes;
        auto slots = std::clamp(device_.arena->options.workspace_bytes / 8 / tile_bytes, maximum_pins, std::size_t{256});
        for (;;) {
            try {
                cache_charge_ = Charge(device_.arena, mul(slots, tile_bytes));
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
        const auto offsets = device_.upload(positions, at::kDouble, at::kDouble, work);
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
    Charge cache_charge_;
    std::size_t cache_slots_ = 0;
    at::Tensor tile_storage_;
    std::vector<Slot> slots_;
    std::uint64_t age_ = 0;
};

class TensorBackend final : public ReceiverBulkBackend {
public:
    explicit TensorBackend(const TensorReceiverOptions& options) : arena_(arena_for(options)), precision_(options.precision) {}

    std::unique_ptr<FineBulk> make_fine(double rate, double clock_span) override {
        return std::make_unique<TensorFine>(arena_, precise(rate, clock_span));
    }

    std::unique_ptr<CoarseBulk> make_coarse(double rate, double clock_span) override {
        return std::make_unique<TensorCoarse>(arena_, precise(rate, clock_span));
    }

    std::size_t fine_host_bytes(std::size_t block_rows, std::size_t taps) const override {
        const auto host = add(mul(block_rows, sizeof(std::int64_t) + sizeof(Complex)),
                              mul(16'384, 2 * taps * sizeof(Complex)));
        const auto result_staging = mul(maximum_batch * fine_result_values(taps), 2 * sizeof(Complex));
        const auto staging = add(2 * maximum_pins * tile_samples * sizeof(Complex), result_staging);
        return add(add(add(host, 2 * mib), staging),
                   arena_->device.is_cpu() ? arena_->options.workspace_bytes : 0);
    }

    std::size_t coarse_host_bytes(std::size_t maximum_fft_rows, std::size_t maximum_tile_frequencies,
                                  std::size_t arrivals) const override {
        const auto scores = mul(mul(mul(maximum_batch, maximum_tile_frequencies), arrivals), 2 * sizeof(double));
        const auto staging = add(mul(maximum_fft_rows, 3 * sizeof(Complex)), 2 * maximum_upload_bytes);
        return add(add(scores, staging), arena_->device.is_cpu() ? arena_->options.workspace_bytes : 0);
    }

    BulkReceipt receipt() const override {
        auto report = arena_->receipt();
        report.precision = precision_ == TensorPrecision::fp64 ? "fp64" : "automatic";
        return report;
    }

private:
    bool precise(double rate, double clock_span) const {
        require(std::isfinite(rate) && rate > 0 && std::isfinite(clock_span) && clock_span >= 0,
                "invalid Robust tensor public precision geometry");
        return precision_ == TensorPrecision::fp64 || (rate <= 10 && clock_span > 0 && clock_span <= .001);
    }

    std::shared_ptr<Arena> arena_;
    TensorPrecision precision_;
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

} // namespace datapump::triad::v3
