#include "datapump/triad_acceleration.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_tensor_fft.hpp"

#include <ATen/ATen.h>
#include <c10/core/InferenceMode.h>
#include <c10/cuda/CUDAException.h>
#include <c10/cuda/CUDACachingAllocator.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#include <cufft.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace {

namespace v = datapump::triad::v3;
namespace r = datapump::triad::v3::receiver_detail;
using Complex = std::complex<double>;
using C64 = c10::complex<float>;
using C128 = c10::complex<double>;
using Clock = std::chrono::steady_clock;
constexpr std::size_t benchmark_pairs = 3;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

std::int64_t dim(std::size_t count) { return static_cast<std::int64_t>(count); }

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Fixture {
    r::CudaHalfFftProductShape shape;
    std::size_t kernel, arrivals;
    std::vector<C64> bank, observation;

    Fixture(r::CudaHalfFftProductShape shape, std::size_t kernel, std::size_t arrivals)
        : shape(shape), kernel(kernel), arrivals(arrivals), bank(bank_rows() * kernel),
          observation(shape.frequencies * observation_rows()) {}

    std::size_t bank_rows() const { return shape.workspaces * shape.taps; }
    std::size_t observation_rows() const { return kernel + arrivals - 1; }
    std::size_t inverse_rows() const { return bank_rows() * shape.frequencies; }
    std::size_t result_size() const { return inverse_rows() * arrivals; }
    std::size_t index(std::size_t workspace, std::size_t frequency, std::size_t tap, std::size_t arrival) const {
        return ((workspace * shape.frequencies + frequency) * shape.taps + tap) * arrivals + arrival;
    }
};

at::Tensor upload(std::vector<C64>& host, std::size_t rows, std::size_t columns) {
    const auto options = at::TensorOptions().dtype(at::kComplexFloat);
    return at::from_blob(host.data(), {dim(rows), dim(columns)}, options.device(at::kCPU))
        .to(options.device(at::kCUDA), false, true);
}

struct Inputs {
    at::Tensor bank, observation;
    explicit Inputs(Fixture& fixture)
        : bank(upload(fixture.bank, fixture.bank_rows(), fixture.kernel)),
          observation(upload(fixture.observation, fixture.shape.frequencies, fixture.observation_rows())) {}
};

struct HalfPlans {
    std::unique_ptr<r::CudaHalfFftPlan> bank, observation, inverse;
    std::size_t scratch_bytes, fft_workspace_bytes, pack_workspace_bytes;

    HalfPlans(const Fixture& fixture, cudaStream_t stream)
        : bank(r::CudaHalfFftPlan::make(fixture.shape.rows, fixture.bank_rows(), stream)),
          observation(r::CudaHalfFftPlan::make(fixture.shape.rows, fixture.shape.frequencies, stream)),
          inverse(r::CudaHalfFftPlan::make(fixture.shape.rows, fixture.inverse_rows(), stream)) {
        check(bank && observation && inverse, "half FFT plan unavailable for a declared representative shape");
        fft_workspace_bytes = std::max({bank->workspace_bytes(), observation->workspace_bytes(), inverse->workspace_bytes()});
        pack_workspace_bytes = std::max(
            r::cuda_half_fft_pack_workspace_bytes(fixture.shape.rows, fixture.kernel, fixture.bank_rows()),
            r::cuda_half_fft_pack_workspace_bytes(fixture.shape.rows, fixture.observation_rows(), fixture.shape.frequencies));
        scratch_bytes = std::max(fft_workspace_bytes, pack_workspace_bytes);
    }
};

// Keep external cuFFT work areas and handles alive across exceptions and stop.
struct DrainOnExit {
    cudaStream_t stream;
    bool pending = true;
    ~DrainOnExit() { if (pending) (void)cudaStreamSynchronize(stream); }
};

struct Measured {
    at::Tensor arrivals;
    double ms = 0;
    std::size_t allocated_peak = 0, explicit_bytes = 0;
    unsigned diagnostics = 0;
};

std::size_t allocated(c10::DeviceIndex device, bool peak) {
    const auto bytes = c10::cuda::CUDACachingAllocator::getDeviceStats(device).allocated_bytes[0];
    return static_cast<std::size_t>(peak ? bytes.peak : bytes.current);
}

Measured half_search(const Fixture& fixture, const Inputs& inputs, const HalfPlans& plans,
                     c10::cuda::CUDAStream stream, std::stop_source* stop_after_forward = nullptr) {
    const auto start = Clock::now();
    const auto base = allocated(stream.device_index(), false);
    c10::cuda::CUDACachingAllocator::resetPeakStats(stream.device_index());
    const auto bytes = inputs.bank.options().dtype(at::kByte);
    auto bank = at::empty({dim(plans.bank->data_bytes())}, bytes);
    auto observation = at::empty({dim(plans.observation->data_bytes())}, bytes);
    auto inverse = at::empty({dim(plans.inverse->data_bytes())}, bytes);
    auto scratch = at::empty({dim(plans.scratch_bytes)}, bytes);
    auto bank_exponents = at::empty({dim(fixture.bank_rows())}, bytes.dtype(at::kInt));
    auto observation_exponents = at::empty({dim(fixture.shape.frequencies)}, bytes.dtype(at::kInt));
    auto diagnostics = at::zeros({1}, bytes.dtype(at::kInt));
    auto arrivals = at::empty({dim(fixture.result_size())}, bytes.dtype(at::kComplexDouble));
    unsigned observed = 0;
    DrainOnExit drain{stream.stream()};
    auto* flags = reinterpret_cast<unsigned*>(diagnostics.data_ptr<std::int32_t>());
    C10_CUDA_CHECK(r::cuda_half_fft_pack(inputs.bank.const_data_ptr<C64>(), bank.data_ptr(),
        bank_exponents.data_ptr<std::int32_t>(), flags, fixture.shape.rows, fixture.kernel, fixture.bank_rows(),
        scratch.data_ptr(), plans.scratch_bytes, stream.stream()));
    plans.bank->enqueue(bank.data_ptr(), scratch.data_ptr(), plans.scratch_bytes, false);
    C10_CUDA_CHECK(r::cuda_half_fft_pack(inputs.observation.const_data_ptr<C64>(), observation.data_ptr(),
        observation_exponents.data_ptr<std::int32_t>(), flags, fixture.shape.rows, fixture.observation_rows(),
        fixture.shape.frequencies, scratch.data_ptr(), plans.scratch_bytes, stream.stream()));
    plans.observation->enqueue(observation.data_ptr(), scratch.data_ptr(), plans.scratch_bytes, false);
    if (stop_after_forward) {
        stop_after_forward->request_stop();
        return {};
    }
    C10_CUDA_CHECK(r::cuda_half_fft_products(bank.const_data_ptr(), observation.const_data_ptr(), inverse.data_ptr(),
        flags, fixture.shape, stream.stream()));
    plans.inverse->enqueue(inverse.data_ptr(), scratch.data_ptr(), plans.scratch_bytes, true);
    C10_CUDA_CHECK(r::cuda_half_fft_arrivals(inverse.const_data_ptr(), bank_exponents.const_data_ptr<std::int32_t>(),
        observation_exponents.const_data_ptr<std::int32_t>(), arrivals.data_ptr<C128>(), flags,
        fixture.shape, fixture.arrivals, stream.stream()));
    C10_CUDA_CHECK(cudaMemcpyAsync(&observed, flags, sizeof(observed), cudaMemcpyDeviceToHost, stream.stream()));
    stream.synchronize();
    drain.pending = false;
    const auto explicit_bytes = bank.nbytes() + observation.nbytes() + inverse.nbytes() + scratch.nbytes() +
        bank_exponents.nbytes() + observation_exponents.nbytes() + diagnostics.nbytes() + arrivals.nbytes();
    return {std::move(arrivals), elapsed_ms(start), allocated(stream.device_index(), true) - base,
            static_cast<std::size_t>(explicit_bytes), observed};
}

Measured single_search(const Fixture& fixture, const Inputs& inputs, c10::cuda::CUDAStream stream) {
    const auto start = Clock::now();
    const auto base = allocated(stream.device_index(), false);
    c10::cuda::CUDACachingAllocator::resetPeakStats(stream.device_index());
    const auto shape = fixture.shape;
    const auto n = dim(shape.rows);
    auto bank = at::fft_fft(inputs.bank, n, -1).conj().reshape({dim(shape.workspaces), 1, dim(shape.taps), n});
    auto observation = at::fft_fft(inputs.observation, n, -1).reshape({1, dim(shape.frequencies), 1, n});
    auto product = bank * observation;
    auto arrivals = at::fft_ifft(product, n, -1).narrow(-1, 0, dim(fixture.arrivals)).to(at::kComplexDouble).flatten();
    stream.synchronize();
    return {std::move(arrivals), elapsed_ms(start), allocated(stream.device_index(), true) - base};
}

std::vector<C128> read(const at::Tensor& result) {
    auto host = result.to(at::kCPU);
    const auto* data = host.const_data_ptr<C128>();
    return {data, data + host.numel()};
}

Complex as_complex(C64 value) { return {value.real(), value.imag()}; }
Complex as_complex(C128 value) { return {value.real(), value.imag()}; }

void check_impulses(c10::cuda::CUDAStream stream) {
    Fixture fixture({4096, 2, 3, 2}, 2001, 27);
    for (std::size_t row = 0; row + 1 < fixture.bank_rows(); ++row)
        fixture.bank[row * fixture.kernel + 31 + 2 * row] = {float(row + 1), -.25f};
    for (std::size_t frequency = 0; frequency + 1 < fixture.shape.frequencies; ++frequency)
        fixture.observation[frequency * fixture.observation_rows() + 42 + frequency] = {.5f, float(frequency + 1)};
    const HalfPlans plans(fixture, stream.stream());
    Inputs inputs(fixture);

    std::stop_source stop;
    check(!half_search(fixture, inputs, plans, stream, &stop).arrivals.defined() && stop.stop_requested(),
          "half FFT cancelled fixture published a result");
    const auto result = half_search(fixture, inputs, plans, stream);
    check(!result.diagnostics, "half FFT impulse diagnostics");
    const auto actual = read(result.arrivals);
    for (std::size_t w = 0; w < fixture.shape.workspaces; ++w)
        for (std::size_t f = 0; f < fixture.shape.frequencies; ++f)
            for (std::size_t t = 0; t < fixture.shape.taps; ++t) {
                const auto row = w * fixture.shape.taps + t;
                const auto sample = 31 + 2 * row;
                const auto b = as_complex(fixture.bank[row * fixture.kernel + sample]);
                for (std::size_t lag = 0; lag < fixture.arrivals; ++lag) {
                    const auto o = as_complex(fixture.observation[f * fixture.observation_rows() + sample + lag]);
                    const auto expected = o * std::conj(b);
                    const auto got = as_complex(actual[fixture.index(w, f, t, lag)]);
                    check(std::abs(got - expected) <= .012 * std::max(1., std::abs(expected)),
                          "half FFT changed impulse sign, lag, row or scale");
                    if (!b.real() || f + 1 == fixture.shape.frequencies)
                        check(got == Complex{}, "half FFT zero spectrum produced a correlation");
                }
            }

    fixture.bank[0] = {std::numeric_limits<float>::infinity(), 0.f};
    fixture.observation[0] = {std::numeric_limits<float>::quiet_NaN(), 0.f};
    Inputs invalid(fixture);
    check(half_search(fixture, invalid, plans, stream).diagnostics == r::cuda_half_fft_nonfinite_input,
          "half FFT must flag nonfinite input before publishing");
    check(!r::CudaHalfFftPlan::make(4095, 1, stream.stream()) &&
          !r::CudaHalfFftPlan::make(4096, 1u << 20, stream.stream()),
          "half FFT accepted unsupported size or overflowing batch extent");
}

void check_coherent_extremes(c10::cuda::CUDAStream stream) {
    Fixture fixture({65536, 1, 2, 2}, 32768, 65);
    const std::array<C64, 2> bank{{{0x1p+115f, -0x1p+114f}, {0x1p-146f, 0x1p-147f}}};
    const std::array<C64, 2> observation{{{0x1p+80f, 0x1p+81f}, {0x1p-139f, -0x1p-140f}}};
    for (std::size_t i = 0; i < bank.size(); ++i)
        std::fill_n(fixture.bank.begin() + i * fixture.kernel, fixture.kernel, bank[i]);
    for (std::size_t i = 0; i < observation.size(); ++i)
        std::fill_n(fixture.observation.begin() + i * fixture.observation_rows(), fixture.observation_rows(), observation[i]);
    const Inputs inputs(fixture);
    const HalfPlans plans(fixture, stream.stream());
    if (plans.inverse->workspace_bytes()) {
        auto data = at::empty({dim(plans.inverse->data_bytes())}, inputs.bank.options().dtype(at::kByte));
        bool rejected = false;
        try {
            plans.inverse->enqueue(data.data_ptr(), nullptr, 0, true);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "half FFT accepted an underquoted work area");
    }
    const auto result = half_search(fixture, inputs, plans, stream);
    check(!result.diagnostics, "half FFT overflowed a coherently summed scaled transform");
    const auto actual = read(result.arrivals);
    for (std::size_t f = 0; f < fixture.shape.frequencies; ++f)
        for (std::size_t t = 0; t < fixture.shape.taps; ++t) {
            const auto expected = as_complex(observation[f]) * std::conj(as_complex(bank[t])) * double(fixture.kernel);
            for (std::size_t lag = 0; lag < fixture.arrivals; ++lag) {
                const auto got = as_complex(actual[fixture.index(0, f, t, lag)]);
                check(std::abs(got - expected) <= .012 * std::abs(expected),
                      "half FFT lost a coherent correlation at an extreme input scale");
            }
        }
}

void check_long_sparse(c10::cuda::CUDAStream stream) {
    Fixture fixture({262144, 1, 3, 1}, 131073, 65);
    constexpr float impulse = 1.01f;
    constexpr float sigma = 0x1p-10f;
    constexpr std::size_t low = 17, high = 42;
    fixture.bank[0] = {impulse, 0.f};
    fixture.observation[0] = {impulse, 0.f};

    const auto quiet_offset = fixture.observation_rows();
    const auto weak_offset = quiet_offset * 2;
    std::mt19937 random(74052);
    std::normal_distribution<float> normal(0.f, sigma / std::sqrt(2.f));
    for (std::size_t i = 0; i < fixture.observation_rows(); ++i) {
        const C64 sample{normal(random), normal(random)};
        fixture.observation[quiet_offset + i] = sample;
        fixture.observation[weak_offset + i] = sample;
    }
    fixture.observation[weak_offset + low] = {sigma * std::sqrt(12.4f), 0.f};
    fixture.observation[weak_offset + high] = {sigma * std::sqrt(12.6f), 0.f};
    fixture.observation[weak_offset + 1024] = {impulse, 0.f};
    const Inputs inputs(fixture);
    const HalfPlans plans(fixture, stream.stream());
    const auto result = half_search(fixture, inputs, plans, stream);
    check(!result.diagnostics, "half FFT long sparse diagnostics");
    const auto actual = read(result.arrivals);

    // The old maximum-component times row-length bound rounded every
    // product in the first spectrum pair to zero despite a finite impulse.
    const auto impulse_expected = double(impulse) * impulse;
    const auto impulse_observed = as_complex(actual.front());
    check(std::abs(impulse_observed - impulse_expected) <= .012 * impulse_expected,
          "half FFT erased the long sparse impulse correlation");
    double noise_reference = 0, noise_error = 0;
    for (std::size_t f = 1; f < fixture.shape.frequencies; ++f)
        for (std::size_t lag = 0; lag < fixture.arrivals; ++lag) {
            const auto got = as_complex(actual[fixture.index(0, f, 0, lag)]);
            const auto expected = double(impulse) * as_complex(fixture.observation[f * fixture.observation_rows() + lag]);
            noise_reference += std::norm(expected);
            noise_error += std::norm(got - expected);
        }
    const auto rms = std::sqrt(noise_error / noise_reference);
    check(std::isfinite(rms) && rms < .05, "half FFT long sparse weak correlation layout or scale differs");
    const auto normalized_score = [&](std::size_t lag) {
        return std::norm(as_complex(actual[fixture.index(0, 2, 0, lag)])) / (impulse_expected * sigma * sigma);
    };
    std::cout << "long_sparse_rows=" << fixture.shape.rows << " long_sparse_kernel=" << fixture.kernel
        << " impulse_expected=" << impulse_expected << " impulse_observed=" << impulse_observed.real()
        << " weak_and_noise_relative_rms=" << rms << " weak_low_expected=12.4 weak_low_score=" << normalized_score(low)
        << " weak_high_expected=12.6 weak_high_score=" << normalized_score(high) << '\n';
}

void fill_noise_and_weak_signals(Fixture& fixture) {
    std::mt19937 random(74051);
    std::normal_distribution<float> normal(0.f, .70710678f);
    for (auto& value : fixture.bank) value = {normal(random), normal(random)};
    for (auto& value : fixture.observation) value = {normal(random), normal(random)};
    // Keep the third frequency noise-only. Place two known correlations
    // close to a fixed component threshold to reveal score drift. This is a
    // fixture at the post-channelization boundary, not a sensitivity trial.
    const auto lag = fixture.arrivals / 2;
    for (std::size_t f = 0; f < 2; ++f) {
        const auto offset = f * fixture.observation_rows() + lag;
        double norm = 0;
        Complex correlation;
        for (std::size_t i = 0; i < fixture.kernel; ++i) {
            const auto b = as_complex(fixture.bank[i]);
            norm += std::norm(b);
            correlation += as_complex(fixture.observation[offset + i]) * std::conj(b);
        }
        const auto injection = (std::sqrt((f ? 12.6 : 12.4) * norm) - correlation) / norm;
        for (std::size_t i = 0; i < fixture.kernel; ++i) {
            const auto o = as_complex(fixture.observation[offset + i]) + injection * as_complex(fixture.bank[i]);
            fixture.observation[offset + i] = {float(o.real()), float(o.imag())};
        }
    }
}

void compare(Fixture fixture, c10::cuda::CUDAStream stream) {
    fill_noise_and_weak_signals(fixture);
    const Inputs inputs(fixture);
    const auto first_single = single_search(fixture, inputs, stream);
    const auto plan_start = Clock::now();
    const HalfPlans plans(fixture, stream.stream());
    const auto plan_ms = elapsed_ms(plan_start);
    const auto first_half = half_search(fixture, inputs, plans, stream);
    check(!first_half.diagnostics, "half FFT diagnostics in weak and noise-only input");
    const auto fp32 = read(first_single.arrivals);
    const auto half = read(first_half.arrivals);
    check(fp32.size() == fixture.result_size() && half.size() == fp32.size(), "half FFT omitted arrivals");
    double error_norm = 0, reference_norm = 0;
    for (std::size_t i = 0; i < half.size(); ++i) {
        error_norm += std::norm(as_complex(half[i]) - as_complex(fp32[i]));
        reference_norm += std::norm(as_complex(fp32[i]));
    }
    const auto relative_error = std::sqrt(error_norm / reference_norm);
    // Detect a structural layout or scaling failure. Report the measured
    // score changes separately from this component sanity check.
    check(std::isfinite(relative_error) && relative_error < .05, "half FFT correlation layout or scale differs");
    double norm = 0;
    for (std::size_t i = 0; i < fixture.kernel; ++i) norm += std::norm(as_complex(fixture.bank[i]));

    std::array<double, benchmark_pairs> single_ms{}, half_ms{};
    for (std::size_t pair = 0; pair < benchmark_pairs; ++pair) {
        const auto fp32_run = [&] { single_ms[pair] = single_search(fixture, inputs, stream).ms; };
        const auto half_run = [&] {
            const auto result = half_search(fixture, inputs, plans, stream);
            check(!result.diagnostics, "half FFT timing run diagnostics");
            half_ms[pair] = result.ms;
        };
        if (pair % 2) { half_run(); fp32_run(); }
        else { fp32_run(); half_run(); }
    }
    std::sort(single_ms.begin(), single_ms.end());
    std::sort(half_ms.begin(), half_ms.end());
    const auto low = fixture.index(0, 0, 0, fixture.arrivals / 2);
    const auto high = fixture.index(0, 1, 0, fixture.arrivals / 2);
    const auto noise = fixture.index(0, 2, 0, fixture.arrivals / 2);
    std::cout << "rows=" << fixture.shape.rows << " kernel=" << fixture.kernel << " arrivals=" << fixture.arrivals
        << " workspaces=" << fixture.shape.workspaces << " frequencies=" << fixture.shape.frequencies
        << " taps=" << fixture.shape.taps << " correlation_values=" << half.size()
        << " fp32_first_ms=" << first_single.ms << " half_plan_ms=" << plan_ms << " half_first_ms=" << first_half.ms
        << " fp32_median_ms=" << single_ms[benchmark_pairs / 2] << " half_median_ms=" << half_ms[benchmark_pairs / 2]
        << " fp32_allocator_peak_bytes=" << first_single.allocated_peak
        << " half_allocator_peak_bytes=" << first_half.allocated_peak
        << " half_explicit_bytes=" << first_half.explicit_bytes << " half_fft_workspace_bytes=" << plans.fft_workspace_bytes
        << " half_pack_workspace_bytes=" << plans.pack_workspace_bytes
        << " correlation_relative_rms=" << relative_error
        << " fp32_low_score=" << std::norm(as_complex(fp32[low])) / norm
        << " half_low_score=" << std::norm(as_complex(half[low])) / norm
        << " fp32_high_score=" << std::norm(as_complex(fp32[high])) / norm
        << " half_high_score=" << std::norm(as_complex(half[high])) / norm
        << " fp32_noise_score=" << std::norm(as_complex(fp32[noise])) / norm
        << " half_noise_score=" << std::norm(as_complex(half[noise])) / norm << '\n';
}

void check_receiver_half_route() {
    constexpr std::size_t n = 4096, kernel = 1601, arrivals = 7, workspaces = 9, taps = 2;
    constexpr std::size_t tile_width = 3, separation = 2;
    constexpr double fs = 1024, variance = .5;
    constexpr std::size_t cap = 512 * 1024 * 1024;
    constexpr std::array frequencies{-12.5, 0., 18.25, 31.};
    constexpr std::array amplitudes{1., .3, 1.7, .2, .5, .7, .1};
    const r::CoarseBulkGeometry geometry{taps, kernel, n, arrivals, frequencies.size(), fs, variance, tile_width};
    std::vector<Complex> columns(workspaces * taps * kernel), observation(kernel + arrivals - 1);
    std::array<r::CoarseBulkWorkspace, workspaces> bank{};
    for (std::size_t w = 0; w < workspaces; ++w) {
        bank[w].inverse_a = .5;
        bank[w].inverse_d = .25;
        for (std::size_t t = 0; t < taps; ++t) {
            const auto offset = 16 * w + 200 * t;
            auto column = std::span(columns).subspan((w * taps + t) * kernel, kernel);
            column[offset] = 1;
            bank[w].columns[t] = column;
            for (std::size_t lag = 0; lag < arrivals; ++lag)
                observation[offset + lag] = amplitudes[lag] * Complex(1 + .125 * w, .25 * t);
        }
    }
    const auto backend = v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, cap,
        v::TensorPrecision::automatic, v::TensorCoarseMath::fp16_fft});
    auto owner = backend->make_coarse(fs, .25);
    r::CoarseBulkWork preparation, search;
    owner->prepare(geometry, observation, bank, {}, preparation, {});
    const auto prepared = owner->receipt();
    check(owner->precision_reason() == "cuda-fp16-frequency-fft" &&
          prepared.precision == "fp16_fft_fp64_coordinates_and_reductions" && prepared.cuda_fft_version > 0,
          "explicit receiver half FFT did not reach the planned CUDA stage");
    check(prepared.cuda_half_fft_peak_bank_bytes == workspaces * taps * (4 * n + sizeof(std::int32_t)) &&
          prepared.reserved_bytes == prepared.cuda_half_fft_peak_phase_bytes &&
          prepared.cuda_half_fft_peak_phase_bytes <= cap,
          "explicit receiver half FFT did not reserve its packed bank and phase");

    const auto raw = [&](std::span<const double> tile) {
        std::size_t next = 0;
        std::vector<double> scores;
        owner->frequency_tile(tile, [&](std::size_t first, std::size_t count, std::span<const double> values) {
            check(first == next && count && values.size() == count * tile.size() * arrivals,
                  "receiver half FFT raw scores lost or reordered public rows");
            for (std::size_t w = 0; w < count; ++w) {
                for (std::size_t f = 0; f < tile.size(); ++f) {
                    const auto row = values.subspan((w * tile.size() + f) * arrivals, arrivals);
                    const auto peak = r::select_coarse_arrivals(row, separation);
                    const double scale = 1 + .125 * (first + w);
                    const double expected = std::norm(amplitudes[2] * Complex(scale, 0)) +
                        .5 * std::norm(amplitudes[2] * Complex(scale, .25));
                    check(peak.count == 2 && peak.indices[0] == 0 && peak.indices[1] == 2 &&
                          row[2] > .9 * expected && row[2] < 1.1 * expected,
                          "receiver half FFT changed the planted impulse ordering or score scale");
                }
            }
            scores.insert(scores.end(), values.begin(), values.end());
            next += count;
        }, search, {});
        check(next == workspaces, "receiver half FFT raw tile omitted its workspace tail");
        return scores;
    };
    (void)raw(std::span(frequencies).first(tile_width));
    const auto tail = std::span(frequencies).last(1);
    const auto tail_scores = raw(tail);
    std::size_t next = 0;
    const auto compressed = owner->frequency_peaks(tail, separation,
        [&](std::size_t first, std::size_t count, std::span<const r::CoarseBulkPeaks> values) {
            check(first == next && count && values.size() == count, "receiver half FFT frontier reordered public rows");
            for (std::size_t w = 0; w < count; ++w) {
                const auto row = std::span(tail_scores).subspan((first + w) * arrivals, arrivals);
                const auto expected = r::select_coarse_arrivals(row, separation);
                check(values[w].count == expected.count, "receiver half FFT changed the exact frontier count");
                for (std::size_t k = 0; k < expected.count; ++k)
                    check(values[w].lags[k] == expected.indices[k] && values[w].scores[k] == row[expected.indices[k]],
                          "receiver half FFT changed the exact frontier lag or score");
            }
            next += count;
        }, search, {});
    check(compressed && next == workspaces, "receiver half FFT frontier omitted its workspace tail");
    const auto report = owner->receipt();
    constexpr auto searched_frequencies = frequencies.size() + 1;
    constexpr auto pairs = workspaces * searched_frequencies;
    check(preparation.forward_ffts == workspaces * taps && search.forward_ffts == searched_frequencies &&
          search.inverse_ffts == pairs * taps && search.score_rows == pairs * arrivals &&
          report.score_rows == search.score_rows && report.cuda_half_fft_score_rows == search.score_rows &&
          report.cuda_half_fft_padded_ffts > 0 &&
          report.cuda_half_fft_forward_ffts + report.cuda_half_fft_inverse_ffts ==
              preparation.forward_ffts + search.forward_ffts + search.inverse_ffts + report.cuda_half_fft_padded_ffts,
          "receiver half FFT receipt did not distinguish public work from padded transforms");
    owner.reset();
    check(backend->status_receipt().reserved_bytes == 0, "receiver half FFT phase leaked its reservation");

    const auto precise = v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, cap,
        v::TensorPrecision::fp64, v::TensorCoarseMath::fp16_fft});
    owner = precise->make_coarse(fs, .25);
    preparation = {};
    owner->prepare(geometry, observation, bank, {}, preparation, {});
    const auto fallback = owner->receipt();
    check(owner->precision_reason() == "cuda-fp16-fft-fp64-stage" && fallback.precision == "fp64" &&
          fallback.half_fft_fallback_counts[1] == 1 && fallback.cuda_half_fft_forward_ffts == 0,
          "receiver half FFT overrode the requested FP64 stage or lost its fallback receipt");
    owner.reset();

    auto unsupported = geometry;
    unsupported.correlation_fft_rows = 4608;
    owner = backend->make_coarse(fs, .25);
    preparation = {};
    search = {};
    owner->prepare(unsupported, observation, bank, {}, preparation, {});
    (void)raw(std::span(frequencies).first(tile_width));
    (void)raw(tail);
    const auto unsupported_report = owner->receipt();
    check(owner->precision_reason() == "cuda-fp16-fft-unsupported-plan" &&
          unsupported_report.half_fft_fallback_counts[3] == 1 &&
          unsupported_report.cuda_half_fft_forward_ffts == 0 &&
          unsupported_report.cuda_half_fft_score_rows == 0 &&
          search.score_rows == workspaces * frequencies.size() * arrivals,
          "unsupported half FFT geometry did not score the complete public grid in FP32");
    owner.reset();

    // Use the shared GUI factory and keep a fixed cap so both the acquired
    // whole-phase lease and its release are observable on the test device.
    const auto automatic = v::make_gpu_receiver_backend();
    auto broad = geometry;
    constexpr std::size_t broad_tile_width = 16;
    std::array<double, broad_tile_width + 1> broad_frequencies{};
    for (std::size_t f = 0; f < broad_frequencies.size(); ++f) broad_frequencies[f] = double(f) - 8.;
    broad.frequency_count = broad_frequencies.size();
    broad.frequency_tile_width = broad_tile_width;
    const auto hardware = automatic->status_receipt();
    const auto capability = 10 * hardware.cuda_compute_major + hardware.cuda_compute_minor;
    const bool qualified = capability == 89 || capability == 120;
    owner = automatic->make_coarse(fs, .25);
    preparation = {};
    owner->prepare(broad, observation, bank, {}, preparation, {});
    const auto broad_report = owner->receipt();
    if (qualified) {
        check(owner->precision_reason() == "cuda-fp16-frequency-fft" &&
              broad_report.cuda_half_fft_peak_phase_bytes > 0 &&
              broad_report.reserved_bytes >= broad_report.cuda_half_fft_peak_phase_bytes &&
              broad_report.reserved_bytes <= cap,
              "automatic receiver did not admit the qualified half FFT phase");
        search = {};
        for (std::size_t first = 0; first < broad.frequency_count; first += broad_tile_width) {
            const auto tile = std::span(broad_frequencies).subspan(first,
                std::min(broad_tile_width, broad.frequency_count - first));
            std::size_t published = 0;
            const auto handled = owner->frequency_peaks(tile, separation,
                [&](std::size_t row, std::size_t count, std::span<const r::CoarseBulkPeaks> values) {
                    check(row == published && values.size() == count * tile.size(),
                          "automatic half FFT frontier changed the public workspace order");
                    published += count;
                }, search, {});
            check(handled && published == workspaces, "automatic half FFT did not finish the public frequency tile");
        }
        const auto completed = owner->receipt();
        check(search.score_rows == workspaces * broad.frequency_count * arrivals &&
              completed.cuda_half_fft_score_rows == search.score_rows,
              "automatic half FFT did not score every public frequency and arrival");
    } else {
        check(owner->precision_reason() == "cuda-fp16-fft-unqualified-hardware" &&
              broad_report.cuda_half_fft_forward_ffts == 0,
              "automatic receiver did not report the CUDA half FFT hardware guard");
    }
    owner.reset();
    check(automatic->status_receipt().reserved_bytes == 0, "automatic receiver leaked the coarse phase lease");

    if (qualified) {
        owner = automatic->make_coarse(fs, .25);
        auto small = broad;
        small.frequency_count = small.frequency_tile_width = 7;
        owner->prepare(small, observation, bank, {}, preparation, {});
        check(owner->precision_reason() == "cuda-fp16-fft-unqualified-shape" &&
              owner->receipt().cuda_half_fft_forward_ffts == 0,
              "automatic half FFT selected an inverse tile below its workload bound");
        owner.reset();
        owner = automatic->make_coarse(fs, .25);
        broad.minimum_eigenvalue_ratio = 1. / 32;
        owner->prepare(broad, observation, bank, {}, preparation, {});
        check(owner->precision_reason() == "cuda-fp16-fft-ill-conditioned-template" &&
              owner->receipt().cuda_half_fft_forward_ffts == 0,
              "automatic half FFT overrode the canonical conditioning guard");
        owner.reset();
        broad.minimum_eigenvalue_ratio = 1;
    }
    const auto minimum = v::make_gpu_receiver_backend({true});
    owner = minimum->make_coarse(fs, .25);
    owner->prepare(broad, observation, bank, {}, preparation, {});
    check(minimum->status_receipt().precision == "fp32_minimum" &&
          owner->precision_reason() == "cuda-fp16-fft-fp32-minimum" &&
          owner->receipt().cuda_half_fft_forward_ffts == 0,
          "GUI FP32 minimum did not preserve its floor on automatic CUDA");
    std::cout << "receiver_half_fft_score_rows=" << report.cuda_half_fft_score_rows
              << " padded_ffts=" << report.cuda_half_fft_padded_ffts
              << " peak_phase_bytes=" << report.cuda_half_fft_peak_phase_bytes << '\n';
}

} // namespace

int main() {
    try {
        c10::InferenceMode inference;
        c10::cuda::CUDAGuard device(0);
        const auto stream = c10::cuda::getStreamFromPool(false, 0);
        c10::cuda::CUDAStreamGuard stream_guard(stream);
        int version = 0;
        check(cufftGetVersion(&version) == CUFFT_SUCCESS, "cannot identify cuFFT version");
        cudaDeviceProp properties{};
        C10_CUDA_CHECK(cudaGetDeviceProperties(&properties, 0));
        std::cout << std::setprecision(9) << "CUDA device=" << properties.name << " SM=" << properties.major << '.'
            << properties.minor << " cuFFT=" << version << " transform=CUDA_C_16F" << '\n';
        check_impulses(stream);
        check_coherent_extremes(stream);
        check_long_sparse(stream);
        compare(Fixture({4096, 8, 32, 1}, 1845, 2111), stream);
        compare(Fixture({65536, 2, 8, 2}, 34470, 139), stream);
        compare(Fixture({131072, 2, 4, 2}, 82645, 57), stream);
        compare(Fixture({1 << 20, 1, 3, 1}, 3 * (1 << 18), 31), stream);
        check_receiver_half_route();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
