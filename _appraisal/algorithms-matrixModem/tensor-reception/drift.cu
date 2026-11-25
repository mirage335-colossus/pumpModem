#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <exception>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef DRIFT_CPU_ONLY
#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>
#endif

namespace {

using Complex = std::complex<double>;
using Window = std::vector<Complex>;
using Clock = std::chrono::steady_clock;

constexpr double pi = 3.14159265358979323846;
constexpr int sample_rate = 3200;
constexpr int tone_samples = 256;
constexpr std::array<int, 8> pattern{-4, 3, -2, 5, 1, -5, 2, -3};
constexpr int samples = tone_samples * static_cast<int>(pattern.size());
constexpr int frame_samples = 2 * samples;
static_assert((frame_samples & (frame_samples - 1)) == 0, "FFT size must be a power of two");
constexpr double repeat_lag = static_cast<double>(samples) / sample_rate;
constexpr double frequency_step = 1.0 / repeat_lag;
constexpr double rate_step = frequency_step / repeat_lag;
constexpr int cfo_half_grid = 16;
constexpr int relative_bins = 3;
constexpr int static_bins = (2 * cfo_half_grid + 1) * relative_bins;
constexpr double pfa = 0.001;
constexpr int zero_index = 1;

Complex phase(double turns) { return std::polar(1.0, 2.0 * pi * turns); }

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

double decision_level(int hypotheses) { return std::log(hypotheses / pfa); }

struct Capture {
    Window past;
    Window recent;
    int drift_bin;
    int cfo_bin;
};

double energy(const Window& window) {
    double result = 0.0;
    for (const auto& x : window) result += std::norm(x);
    return result;
}

struct Templates {
    explicit Templates() : motif(samples) {
        // Every 80 ms tone completes an integer number of cycles. Adjacent
        // tones and repeats meet at unit phase without discontinuity.
        for (int i = 0; i < samples; ++i) {
            const int offset = i % tone_samples;
            motif[i] = phase(pattern[i / tone_samples] * offset / double(tone_samples));
        }
        for (int drift = 0; drift < relative_bins; ++drift) {
            auto& removal = rotations[drift];
            removal.resize(samples);
            for (int i = 0; i < samples; ++i) {
                const double t = i / double(sample_rate);
                removal[i] = phase(-(drift - zero_index) * frequency_step * t);
            }
        }
        for (int i = 0; i < frame_samples; ++i) {
            reversed[i] = (reversed[i / 2] / 2) + (i % 2) * (frame_samples / 2);
            for (int drift = 0; drift < relative_bins; ++drift) {
                const double t = i / double(sample_rate);
                dechirps[drift][reversed[i]] = phase(-0.5 * (drift - zero_index) * rate_step * t * t);
            }
        }
        for (int i = 0; i < frame_samples / 2; ++i) {
            fft_phases[i] = phase(-i / double(frame_samples));
        }
        for (int cfo = -cfo_half_grid; cfo <= cfo_half_grid; ++cfo) {
            for (int drift = 0; drift < relative_bins; ++drift) {
                auto& static_reference = statics[(cfo + cfo_half_grid) * relative_bins + drift];
                static_reference.resize(frame_samples);
                for (int i = 0; i < frame_samples; ++i) {
                    const double t = i / double(sample_rate);
                    const double turns = cfo * frequency_step * t
                                       + 0.5 * (drift - zero_index) * rate_step * t * t;
                    static_reference[i] = std::conj(motif[i % samples] * phase(turns));
                }
            }
        }
    }

    Window motif;
    std::array<Window, relative_bins> rotations;
    std::array<Window, static_bins> statics;
    std::array<int, frame_samples> reversed{};
    std::array<Complex, frame_samples / 2> fft_phases;
    std::array<std::array<Complex, frame_samples>, relative_bins> dechirps;
};

Capture render(const Templates& refs, unsigned seed, double cn0_dbhz, bool noise,
               bool signal, bool off_grid, double absolute_cfo_override = 0.0,
               bool phase_step = false) {
    std::mt19937_64 engine(seed);
    std::normal_distribution<double> normal;
    std::uniform_int_distribution<int> cfo_draw(-cfo_half_grid, cfo_half_grid);
    std::uniform_int_distribution<int> drift_draw(0, relative_bins - 1);
    std::uniform_real_distribution<double> phase_draw(0.0, 1.0);
    Capture out{Window(samples), Window(samples), drift_draw(engine), cfo_draw(engine)};
    double f = frequency_step * (out.cfo_bin + (off_grid ? 0.22 : 0.0));
    if (!noise) f += absolute_cfo_override;
    double a = rate_step * (out.drift_bin - zero_index);
    if (off_grid) a += 0.13 * rate_step;
    const double amplitude = signal ? std::sqrt(std::pow(10.0, cn0_dbhz / 10.0) / sample_rate) : 0.0;
    const double constant = phase_draw(engine);
    const double white_scale = 1.0 / std::sqrt(2.0);
    for (int i = 0; i < 2 * samples; ++i) {
        const double t = i / double(sample_rate);
        Complex x = amplitude * refs.motif[i % samples] * phase(constant + f * t + 0.5 * a * t * t);
        if (phase_step && i >= samples + samples / 2) x = -x;
        if (noise) x += white_scale * Complex{normal(engine), normal(engine)};
        (i < samples ? out.past[i] : out.recent[i - samples]) = x;
    }
    return out;
}

using Raw = std::array<Complex, relative_bins>;

Raw raw_reference(const Templates& refs, const Capture& capture) {
    Raw out{};
    for (int j = 0; j < relative_bins; ++j) {
        for (int i = 0; i < samples; ++i) {
            out[j] += std::conj(capture.past[i]) * (capture.recent[i] * refs.rotations[j][i]);
        }
    }
    return out;
}

Raw raw_pointwise(const Templates& refs, const Capture& capture) {
    Raw out{};
    for (int i = 0; i < samples; ++i) {
        const auto product = std::conj(capture.past[i]) * capture.recent[i];
        for (int j = 0; j < relative_bins; ++j) out[j] += product * refs.rotations[j][i];
    }
    return out;
}

struct Candidate {
    int index;
    double score;
};

Candidate best_relative(const Raw& raw, double past_energy) {
    int best = 0;
    for (int j = 1; j < relative_bins; ++j) {
        if (std::norm(raw[j]) > std::norm(raw[best])) best = j;
    }
    return {best, std::norm(raw[best]) / past_energy};
}

using StaticScores = std::array<double, static_bins>;

StaticScores static_direct(const Templates& refs, const Capture& capture) {
    StaticScores out{};
    for (int j = 0; j < static_bins; ++j) {
        Complex total{};
        for (int i = 0; i < samples; ++i) {
            total += refs.statics[j][i] * capture.past[i]
                   + refs.statics[j][i + samples] * capture.recent[i];
        }
        out[j] = std::norm(total) / frame_samples;
    }
    return out;
}

StaticScores static_factored(const Templates& refs, const Capture& capture) {
    std::array<Complex, frame_samples> mixed;
    for (int i = 0; i < samples; ++i) {
        const auto undo_tone = std::conj(refs.motif[i]);
        mixed[refs.reversed[i]] = capture.past[i] * undo_tone;
        mixed[refs.reversed[i + samples]] = capture.recent[i] * undo_tone;
    }

    StaticScores out{};
    for (int drift = 0; drift < relative_bins; ++drift) {
        std::array<Complex, frame_samples> fft;
        for (int i = 0; i < frame_samples; ++i) fft[i] = mixed[i] * refs.dechirps[drift][i];

        // Bit-reversed input permits an in-place radix-2 FFT in natural output order.
        for (int width = 2; width <= frame_samples; width *= 2) {
            const int half = width / 2;
            const int phase_step = frame_samples / width;
            for (int base = 0; base < frame_samples; base += width) {
                for (int k = 0; k < half; ++k) {
                    const Complex even = fft[base + k];
                    const Complex odd = fft[base + k + half] * refs.fft_phases[k * phase_step];
                    fft[base + k] = even + odd;
                    fft[base + k + half] = even - odd;
                }
            }
        }

        // The complete frame spans two motifs, so the tested CFO grid
        // (one cycle per motif) occupies every second FFT bin.
        for (int cfo = -cfo_half_grid; cfo <= cfo_half_grid; ++cfo) {
            const int bin = (2 * cfo + frame_samples) % frame_samples;
            out[(cfo + cfo_half_grid) * relative_bins + drift] = std::norm(fft[bin]) / frame_samples;
        }
    }
    return out;
}

Candidate best_static(const StaticScores& scores) {
    const auto best = std::max_element(scores.begin(), scores.end());
    return {static_cast<int>(best - scores.begin()), *best};
}

void check_static_parity(const StaticScores& direct, const StaticScores& factored) {
    for (int j = 0; j < static_bins; ++j) {
        require(std::abs(direct[j] - factored[j]) <= 1e-8 * (1.0 + direct[j]),
                "direct and factored static template scores disagree");
    }
    require(best_static(direct).index == best_static(factored).index,
            "direct and factored static template winners disagree");
}

#ifndef DRIFT_CPU_ONLY
void check_cuda(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}

void check_blas(cublasStatus_t status) {
    if (status != CUBLAS_STATUS_SUCCESS) throw std::runtime_error(cublasGetStatusString(status));
}

class GpuProjection {
public:
    GpuProjection() {
        try {
            check_blas(cublasCreate(&handle));
            check_blas(cublasSetMathMode(handle, static_cast<cublasMath_t>(
                CUBLAS_DEFAULT_MATH | CUBLAS_MATH_DISALLOW_REDUCED_PRECISION_REDUCTION)));
            check_cuda(cudaMalloc(&past, sizeof(host_past)));
            check_cuda(cudaMalloc(&recent, sizeof(host_recent)));
            check_cuda(cudaMalloc(&products, sizeof(host_products)));
        } catch (...) {
            release();
            throw;
        }
    }

    GpuProjection(const GpuProjection&) = delete;
    GpuProjection& operator=(const GpuProjection&) = delete;

    ~GpuProjection() { release(); }

    Raw score(const Templates& refs, const Capture& capture) {
        // Four real products recover each conjugated complex dot. Padding
        // aligns leading dimensions, while the GEMM requests only 2 x 6 outputs.
        for (int i = 0; i < samples; ++i) {
            host_past[i * pitch] = __float2half(static_cast<float>(capture.past[i].real()));
            host_past[i * pitch + 1] = __float2half(static_cast<float>(capture.past[i].imag()));
            for (int j = 0; j < relative_bins; ++j) {
                const Complex x = capture.recent[i] * refs.rotations[j][i];
                host_recent[(2 * j) * samples + i] = __float2half(static_cast<float>(x.real()));
                host_recent[(2 * j + 1) * samples + i] = __float2half(static_cast<float>(x.imag()));
            }
        }
        check_cuda(cudaMemcpy(past, host_past.data(), sizeof(host_past), cudaMemcpyHostToDevice));
        check_cuda(cudaMemcpy(recent, host_recent.data(), sizeof(host_recent), cudaMemcpyHostToDevice));
        const float alpha = 1.0f;
        const float beta = 0.0f;
        check_blas(cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, 2, 2 * relative_bins, samples,
                               &alpha, past, CUDA_R_16F, pitch, recent, CUDA_R_16F, samples,
                               &beta, products, CUDA_R_32F, pitch, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT));
        check_cuda(cudaMemcpy(host_products.data(), products, sizeof(host_products), cudaMemcpyDeviceToHost));
        Raw out{};
        for (int j = 0; j < relative_bins; ++j) {
            const float* re = host_products.data() + (2 * j) * pitch;
            const float* im = re + pitch;
            out[j] = Complex{double(re[0]) + im[1], double(im[0]) - re[1]};
        }
        return out;
    }

private:
    void release() noexcept {
        if (products) cudaFree(products);
        if (recent) cudaFree(recent);
        if (past) cudaFree(past);
        if (handle) cublasDestroy(handle);
    }

    static constexpr int pitch = 8;
    cublasHandle_t handle = nullptr;
    __half* past = nullptr;
    __half* recent = nullptr;
    float* products = nullptr;
    std::array<__half, pitch * samples> host_past{};
    std::array<__half, 2 * relative_bins * samples> host_recent{};
    std::array<float, pitch * 2 * relative_bins> host_products{};
};
#endif

double microseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - begin).count();
}

void verify(const Templates& refs) {
    for (unsigned seed = 31; seed < 47; ++seed) {
        const auto a = render(refs, seed, 28.0, false, true, false);
        // The single pair correlation removes this extra common CFO even though
        // its absolute value falls far outside the static reference bank.
        const auto b = render(refs, seed, 28.0, false, true, false, 613.25);
        const auto ref = raw_reference(refs, a);
        const auto far_cfo = raw_pointwise(refs, b);
        require(std::abs(best_relative(ref, energy(a.past)).score - energy(a.recent)) < 1e-8,
                "clean phase-corrected self-correlation lost ideal energy");
        const auto static_check = static_direct(refs, a);
        check_static_parity(static_check, static_factored(refs, a));
        const auto correct_static = best_static(static_check);
        require(correct_static.index == (a.cfo_bin + cfo_half_grid) * relative_bins + a.drift_bin,
                "clean static reference missed its exact hypothesis");
        for (int j = 0; j < relative_bins; ++j) {
            require(std::abs(std::abs(ref[j]) - std::abs(far_cfo[j])) < 1e-8,
                    "absolute CFO changed exact-lag self-correlation magnitude");
            if (j != a.drift_bin) {
                require(std::abs(ref[j]) < 1e-8, "clean off-drift projection was not orthogonal");
            }
        }
    }
    std::puts("DRIFT_VERIFY clean=16 extra_absolute_cfo_hz=613.25 PASS");
}

struct Counts {
    int accepted = 0;
    int correct_rate = 0;
    int correct_cfo = 0;
};

void print_counts(const char* detector, Counts counts, double us, int n) {
    std::printf("  %s present=%d correct_rate=%d correct_cfo=%d mean_us=%.3f\n",
                detector, counts.accepted, counts.correct_rate, counts.correct_cfo, us / n);
}

struct Group {
    const char* name;
    double cn0;
    int n;
    bool signal;
    bool off_grid;
    unsigned seed;
    bool phase_step = false;
};

void compare(const Templates& refs, bool gpu_enabled) {
    constexpr std::array groups{
        Group{"grid_weak", 10, 48, true, false, 3101},
        Group{"grid_mid", 20, 48, true, false, 4201},
        Group{"grid_strong", 28, 48, true, false, 5301},
        Group{"fractional_holdout", 28, 48, true, true, 6401},
        Group{"phase_step_holdout", 28, 48, true, false, 6801, true},
        Group{"white_noise", 0, 192, false, false, 7501},
    };
#ifndef DRIFT_CPU_ONLY
    std::vector<GpuProjection> gpu(gpu_enabled ? 1 : 0);
    if (gpu_enabled) {
        const auto initial = render(refs, 21, 28, true, true, false);
        gpu.front().score(refs, initial);
    }
#else
    require(!gpu_enabled, "GPU route unavailable in DRIFT_CPU_ONLY");
#endif
    std::printf("DRIFT geometry_samples=%d lag_s=%.6f cfo_bins=%d rate_step_hz_s=%.9f "
                "relative_step_hz=%.6f exact_lag=1 pfa=%.3f\n",
                samples, repeat_lag, 2 * cfo_half_grid + 1, rate_step, frequency_step, pfa);
    for (const auto& group : groups) {
        Counts zero_counts, cpu_counts, static_counts, gpu_counts;
        double cpu_time = 0.0, static_time = 0.0, factored_time = 0.0;
        double gpu_time = 0.0, max_relative_error = 0.0;
        int gpu_label_disagreements = 0;
        for (int trial = 0; trial < group.n; ++trial) {
            const auto iq = render(refs, group.seed + trial, group.cn0, true, group.signal,
                                   group.off_grid, 0.0, group.phase_step);
            const double ep = energy(iq.past);
            const auto start = Clock::now();
            const auto cpu = raw_pointwise(refs, iq);
            const auto cpu_best = best_relative(cpu, ep);
            const double zero_score = std::norm(cpu[zero_index]) / ep;
            cpu_time += microseconds(start, Clock::now());
            const auto before_static = Clock::now();
            const auto direct = static_direct(refs, iq);
            const auto static_best = best_static(direct);
            static_time += microseconds(before_static, Clock::now());
            const auto before_factored = Clock::now();
            const auto factored = static_factored(refs, iq);
            const auto factored_best = best_static(factored);
            factored_time += microseconds(before_factored, Clock::now());
            check_static_parity(direct, factored);
            require((static_best.score > decision_level(static_bins)) ==
                    (factored_best.score > decision_level(static_bins)),
                    "direct and factored static decisions disagree");
            const auto cpu_check = raw_reference(refs, iq);
            for (int j = 0; j < relative_bins; ++j) {
                require(std::abs(cpu_check[j] - cpu[j]) < 1e-8, "CPU pointwise and direct dot disagree");
            }

            if (zero_score > decision_level(1)) {
                ++zero_counts.accepted;
                zero_counts.correct_rate += group.signal && iq.drift_bin == zero_index;
            }
            if (cpu_best.score > decision_level(relative_bins)) {
                ++cpu_counts.accepted;
                cpu_counts.correct_rate += group.signal && cpu_best.index == iq.drift_bin;
            }
            if (static_best.score > decision_level(static_bins)) {
                ++static_counts.accepted;
                static_counts.correct_rate += group.signal && static_best.index % relative_bins == iq.drift_bin;
                static_counts.correct_cfo += group.signal && static_best.index / relative_bins - cfo_half_grid == iq.cfo_bin;
            }
#ifndef DRIFT_CPU_ONLY
            if (gpu_enabled) {
                const auto before_gpu = Clock::now();
                const auto got = gpu.front().score(refs, iq);
                const auto picked = best_relative(got, ep);
                gpu_time += microseconds(before_gpu, Clock::now());
                const double input_scale = std::sqrt(ep * energy(iq.recent));
                for (int j = 0; j < relative_bins; ++j) {
                    const double error = std::abs(got[j] - cpu_check[j]) / input_scale;
                    max_relative_error = std::max(max_relative_error, error);
                    require(error < 0.001, "GPU raw dot failed normalized error check");
                }
                const bool cpu_accept = cpu_best.score > decision_level(relative_bins);
                const bool gpu_accept = picked.score > decision_level(relative_bins);
                gpu_label_disagreements += gpu_accept != cpu_accept ||
                                           (cpu_accept && picked.index != cpu_best.index);
                if (gpu_accept) {
                    ++gpu_counts.accepted;
                    gpu_counts.correct_rate += group.signal && picked.index == iq.drift_bin;
                }
            }
#endif
        }
        std::printf("DRIFT_GROUP %s n=%d cn0_dbhz=%g signal=%d off_grid=%d phase_step=%d\n",
                    group.name, group.n, group.cn0, group.signal ? 1 : 0,
                    group.off_grid ? 1 : 0, group.phase_step ? 1 : 0);
        std::printf("  raw_zero present=%d correct_rate=%d\n", zero_counts.accepted, zero_counts.correct_rate);
        print_counts("raw_cpu", cpu_counts, cpu_time, group.n);
        print_counts("known_template_cpu", static_counts, static_time, group.n);
        print_counts("known_template_factored_cpu", static_counts, factored_time, group.n);
        if (gpu_enabled) {
            print_counts("raw_gpu_fp16", gpu_counts, gpu_time, group.n);
            std::printf("  raw_gpu_parity max_dot_error_over_norm=%.9g disagreements=%d\n",
                        max_relative_error, gpu_label_disagreements);
        }
    }
}

namespace bandwidth {

constexpr int factor = 8;
constexpr int half_filter = 80;
constexpr int taps = 2 * half_filter + 1;
constexpr int kept = samples - 2 * half_filter;
constexpr int narrow_samples = kept / factor;
constexpr int covariance_lags = (taps - 1) / factor;
constexpr double cutoff_hz = 140.0;
static_assert(kept % factor == 0, "retain a whole decimated window");

using NarrowWindow = std::array<Complex, narrow_samples>;
using CholeskyBand = std::array<double, covariance_lags + 1>;
using NarrowCapture = std::array<NarrowWindow, 2>;

struct Decimator {
    Decimator() {
        double gain = 0.0;
        for (int n = -half_filter; n <= half_filter; ++n) {
            const double angle = pi * n / half_filter;
            const double blackman = 0.42 + 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);
            const double sinc = n == 0 ? 2.0 * cutoff_hz / sample_rate
                                       : std::sin(2.0 * pi * cutoff_hz * n / sample_rate) / (pi * n);
            impulse[n + half_filter] = sinc * blackman;
            gain += impulse[n + half_filter];
        }
        for (auto& h : impulse) h /= gain;
        for (int lag = 0; lag <= covariance_lags; ++lag) {
            const int offset = lag * factor;
            for (int i = offset; i < taps; ++i) covariance[lag] += impulse[i] * impulse[i - offset];
        }
    }

    NarrowWindow apply(const Window& input, int start = 0) const {
        NarrowWindow out{};
        for (int n = 0; n < narrow_samples; ++n) {
            const auto* begin = input.data() + start + n * factor;
            for (int k = 0; k < taps; ++k) out[n] += impulse[k] * begin[k];
        }
        return out;
    }

    double response(double f) const {
        Complex gain{};
        for (int n = -half_filter; n <= half_filter; ++n)
            gain += impulse[n + half_filter] * phase(f * n / sample_rate);
        return std::abs(gain);
    }

    double equivalent_dimension() const {
        const double trace = narrow_samples * covariance[0];
        double square_trace = narrow_samples * covariance[0] * covariance[0];
        for (int lag = 1; lag <= covariance_lags; ++lag)
            square_trace += 2.0 * (narrow_samples - lag) * covariance[lag] * covariance[lag];
        return trace * trace / square_trace;
    }

    std::array<double, taps> impulse{};
    CholeskyBand covariance{};
};

struct NarrowRefs {
    explicit NarrowRefs(const Templates& full, const Decimator& fir) : weights(static_bins) {
        std::array<CholeskyBand, narrow_samples> lower{};
        for (int i = 0; i < narrow_samples; ++i) {
            for (int j = std::max(0, i - covariance_lags); j <= i; ++j) {
                double value = fir.covariance[i - j];
                for (int k = std::max(0, i - covariance_lags); k < j; ++k)
                    value -= lower[i][i - k] * lower[j][j - k];
                if (i == j) {
                    require(value > 0.0, "nonpositive filtered noise covariance");
                    lower[i][0] = std::sqrt(value);
                } else {
                    lower[i][i - j] = value / lower[j][0];
                }
            }
        }

        for (int d = 0; d < relative_bins; ++d) {
            for (int i = 0; i < narrow_samples; ++i)
                rotations[d][i] = full.rotations[d][half_filter + i * factor];
            for (int lag = 1; lag <= covariance_lags; ++lag)
                weighted_lags[d][lag] = fir.covariance[lag] * std::conj(full.rotations[d][lag * factor]);
        }

        for (int j = 0; j < static_bins; ++j) {
            Window source = full.statics[j];
            for (auto& v : source) v = std::conj(v);
            double total_energy = 0.0;
            for (int w = 0; w < 2; ++w) {
                auto precision = fir.apply(source, w * samples);
                for (int n = 0; n < narrow_samples; ++n) {
                    for (int lag = 1; lag <= std::min(n, covariance_lags); ++lag)
                        precision[n] -= lower[n][lag] * precision[n - lag];
                    precision[n] /= lower[n][0];
                    total_energy += std::norm(precision[n]);
                }
                for (int n = narrow_samples - 1; n >= 0; --n) {
                    for (int lag = 1; lag <= std::min(narrow_samples - n - 1, covariance_lags); ++lag)
                        precision[n] -= lower[n + lag][lag] * precision[n + lag];
                    precision[n] /= lower[n][0];
                    weights[j][w][n] = std::conj(precision[n]);
                }
            }
            noise_weighted_energy[j] = total_energy;
            for (auto& window : weights[j])
                for (auto& weight : window) weight /= std::sqrt(total_energy);
        }
    }

    std::array<NarrowWindow, relative_bins> rotations{};
    std::array<std::array<Complex, covariance_lags + 1>, relative_bins> weighted_lags{};
    std::vector<NarrowCapture> weights;
    std::array<double, static_bins> noise_weighted_energy{};
};

Candidate full_relative(const Capture& iq, const Templates& refs) {
    Raw out{};
    double past_energy = 0.0;
    for (int n = half_filter; n < samples - half_filter; ++n) {
        past_energy += std::norm(iq.past[n]);
        const auto product = std::conj(iq.past[n]) * iq.recent[n];
        for (int d = 0; d < relative_bins; ++d) out[d] += product * refs.rotations[d][n];
    }
    return best_relative(out, past_energy);
}

std::array<double, relative_bins> conditional_variance(const NarrowWindow& earlier, double past_energy,
                                                      const Decimator& fir, const NarrowRefs& refs) {
    std::array<double, relative_bins> variance;
    variance.fill(fir.covariance[0] * past_energy);
    // Condition on the earlier window. The guard gives each retained output
    // disjoint IID innovations across windows. Within either window the FIR
    // induces known correlations, so each drift bin has its own variance.
    for (int lag = 1; lag <= covariance_lags; ++lag) {
        Complex autocorrelation{};
        for (int n = lag; n < narrow_samples; ++n)
            autocorrelation += std::conj(earlier[n - lag]) * earlier[n];
        for (int d = 0; d < relative_bins; ++d)
            variance[d] += 2.0 * std::real(autocorrelation * refs.weighted_lags[d][lag]);
    }
    return variance;
}

Candidate narrow_relative(const NarrowCapture& iq, const Decimator& fir, const NarrowRefs& refs) {
    Raw out{};
    double past_energy = 0.0;
    for (int n = 0; n < narrow_samples; ++n) {
        past_energy += std::norm(iq[0][n]);
        const auto product = std::conj(iq[0][n]) * iq[1][n];
        for (int d = 0; d < relative_bins; ++d) out[d] += product * refs.rotations[d][n];
    }
    const auto variance = conditional_variance(iq[0], past_energy, fir, refs);
    Candidate best{0, -1.0};
    for (int d = 0; d < relative_bins; ++d) {
        const double score = std::norm(out[d]) / variance[d];
        if (score > best.score) best = {d, score};
    }
    return best;
}

double innovation_energy(const NarrowWindow& weights, const Decimator& fir) {
    // Independent check: form the coefficients of every original white
    // innovation in the output dot product instead of using the lag formula.
    std::array<Complex, samples> input_weights{};
    for (int n = 0; n < narrow_samples; ++n)
        for (int k = 0; k < taps; ++k)
            input_weights[n * factor + k] += weights[n] * fir.impulse[k];
    double energy = 0.0;
    for (auto w : input_weights) energy += std::norm(w);
    return energy;
}

Candidate full_known(Capture iq, const Templates& refs) {
    for (auto* window : {&iq.past, &iq.recent}) {
        std::fill_n(window->begin(), half_filter, Complex{});
        std::fill_n(window->end() - half_filter, half_filter, Complex{});
    }
    auto best = best_static(static_factored(refs, iq));
    best.score *= samples / double(kept);
    return best;
}

Candidate narrow_known(const NarrowCapture& iq, const NarrowRefs& refs) {
    StaticScores out{};
    for (int j = 0; j < static_bins; ++j) {
        Complex sum{};
        for (int n = 0; n < narrow_samples; ++n)
            sum += refs.weights[j][0][n] * iq[0][n] + refs.weights[j][1][n] * iq[1][n];
        out[j] = std::norm(sum);
    }
    return best_static(out);
}

void update_counts(Counts& counts, Candidate selection, const Capture& iq, bool signal, int hypotheses) {
    if (selection.score <= decision_level(hypotheses)) return;
    ++counts.accepted;
    counts.correct_rate += signal && selection.index % relative_bins == iq.drift_bin;
    counts.correct_cfo += signal && hypotheses == static_bins &&
                          selection.index / relative_bins - cfo_half_grid == iq.cfo_bin;
}

void run(const Templates& refs) {
    const Decimator fir;
    const NarrowRefs narrow(refs, fir);
    double minimum_pass = 1.0, maximum_stop = 0.0;
    for (int f = 0; f <= sample_rate / 2; ++f) {
        if (f <= 95) minimum_pass = std::min(minimum_pass, fir.response(f));
        if (f >= sample_rate / (2 * factor)) maximum_stop = std::max(maximum_stop, fir.response(f));
    }
    std::printf("DDC samples_in=%d samples_out=%d fs_in=%d fs_out=%d taps=%d guard_each_end=%d "
                "min_gain_dc_to_95hz_db=%.4f max_gain_200hz_to_nyquist_db=%.2f "
                "noise_bw_hz=%.1f equivalent_dimension=%.1f\n",
                kept, narrow_samples, sample_rate, sample_rate / factor, taps, half_filter,
                20 * std::log10(minimum_pass), 20 * std::log10(maximum_stop),
                sample_rate * fir.covariance[0], fir.equivalent_dimension());
    const auto [min_snr, max_snr] = std::minmax_element(narrow.noise_weighted_energy.begin(),
                                                       narrow.noise_weighted_energy.end());
    double min_energy = 1.0;
    double max_outside_energy = 0.0;
    for (unsigned seed = 31; seed < 47; ++seed) {
        const auto clean = render(refs, seed, 28, false, true, false);
        const auto outside = render(refs, seed, 28, false, true, false, 350.0);
        const NarrowCapture projected = {fir.apply(clean.past), fir.apply(clean.recent)};
        require(full_relative(clean, refs).index == clean.drift_bin &&
                    narrow_relative(projected, fir, narrow).index == clean.drift_bin,
                "clean bandwidth experiment chose wrong relative drift");
        const int expected = (clean.cfo_bin + cfo_half_grid) * relative_bins + clean.drift_bin;
        require(full_known(clean, refs).index == expected && narrow_known(projected, narrow).index == expected,
                "clean bandwidth experiment chose wrong known template");
        const double original = energy(clean.past) + energy(clean.recent);
        const auto projected_fraction = [&](const NarrowCapture& filtered) {
            double transformed = 0.0;
            for (const auto& window : filtered)
                for (auto sample : window) transformed += std::norm(sample);
            return transformed * samples / (narrow_samples * original);
        };
        min_energy = std::min(min_energy, projected_fraction(projected));
        max_outside_energy = std::max(max_outside_energy,
                                      projected_fraction({fir.apply(outside.past), fir.apply(outside.recent)}));
    }
    const auto noise = render(refs, 19020, 0, true, false, false);
    const auto earlier = fir.apply(noise.past);
    double earlier_energy = 0.0;
    for (auto value : earlier) earlier_energy += std::norm(value);
    const auto analytic_variance = conditional_variance(earlier, earlier_energy, fir, narrow);
    for (int d = 0; d < relative_bins; ++d) {
        NarrowWindow weights;
        for (int n = 0; n < narrow_samples; ++n)
            weights[n] = std::conj(earlier[n]) * narrow.rotations[d][n];
        const double checked = innovation_energy(weights, fir);
        require(std::abs(analytic_variance[d] - checked) < 1e-12 * checked,
                "conditional drift variance disagrees with direct innovation energy");
    }
    for (const auto& bank : narrow.weights) {
        require(std::abs(innovation_energy(bank[0], fir) + innovation_energy(bank[1], fir) - 1.0) < 1e-8,
                "known-template weights do not have unit white-innovation variance");
    }
    std::printf("DDC_VERIFY clean=16 min_signal_energy_fraction=%.5f "
                "static_snr_vs_guarded_min=%.5f static_snr_vs_guarded_max=%.5f "
                "static_snr_vs_unguarded_min=%.5f static_snr_vs_unguarded_max=%.5f "
                "max_energy_fraction_with_extra_350_hz=%.7g null_weight_checks=%d PASS\n",
                min_energy, *min_snr / (2 * kept), *max_snr / (2 * kept),
                *min_snr / frame_samples, *max_snr / frame_samples, max_outside_energy,
                relative_bins + static_bins);

    constexpr std::array groups{
        Group{"grid_weak", 10, 48, true, false, 3101},
        Group{"grid_mid", 20, 48, true, false, 4201},
        Group{"grid_strong", 28, 48, true, false, 5301},
        Group{"fractional_holdout", 28, 48, true, true, 6401},
        Group{"phase_step_holdout", 28, 48, true, false, 6801, true},
        Group{"white_noise_holdout", 0, 1024, false, false, 19021},
    };
    for (const auto& group : groups) {
        Counts full_raw_counts, narrow_raw_counts, full_known_counts, narrow_known_counts;
        double full_raw_us = 0.0, narrow_raw_us = 0.0, full_known_us = 0.0, narrow_known_us = 0.0;
        double filter_us = 0.0;
        for (int trial = 0; trial < group.n; ++trial) {
            const auto iq = render(refs, group.seed + trial, group.cn0, true, group.signal,
                                   group.off_grid, 0.0, group.phase_step);
            auto start = Clock::now();
            const auto full_raw = full_relative(iq, refs);
            full_raw_us += microseconds(start, Clock::now());
            start = Clock::now();
            const auto full_template = full_known(iq, refs);
            full_known_us += microseconds(start, Clock::now());
            start = Clock::now();
            const NarrowCapture filtered = {fir.apply(iq.past), fir.apply(iq.recent)};
            filter_us += microseconds(start, Clock::now());
            start = Clock::now();
            const auto narrow_raw = narrow_relative(filtered, fir, narrow);
            narrow_raw_us += microseconds(start, Clock::now());
            start = Clock::now();
            const auto narrow_template = narrow_known(filtered, narrow);
            narrow_known_us += microseconds(start, Clock::now());
            update_counts(full_raw_counts, full_raw, iq, group.signal, relative_bins);
            update_counts(narrow_raw_counts, narrow_raw, iq, group.signal, relative_bins);
            update_counts(full_known_counts, full_template, iq, group.signal, static_bins);
            update_counts(narrow_known_counts, narrow_template, iq, group.signal, static_bins);
        }
        std::printf("DDC_GROUP %s n=%d cn0_dbhz=%g fir_mean_us=%.3f\n",
                    group.name, group.n, group.cn0, filter_us / group.n);
        print_counts("full_raw_cpu", full_raw_counts, full_raw_us, group.n);
        print_counts("narrow_raw_cpu", narrow_raw_counts, narrow_raw_us, group.n);
        print_counts("full_known_factored_cpu", full_known_counts, full_known_us, group.n);
        print_counts("narrow_known_gls_cpu", narrow_known_counts, narrow_known_us, group.n);
    }
}

} // namespace bandwidth

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 1 || (argc == 2 &&
                    (std::string_view(argv[1]) == "--cpu" || std::string_view(argv[1]) == "--bandwidth")),
                "usage: matrix_drift [--cpu|--bandwidth]");
        const Templates refs;
        if (argc == 2 && std::string_view(argv[1]) == "--bandwidth") {
            bandwidth::run(refs);
            return 0;
        }
        verify(refs);
#ifdef DRIFT_CPU_ONLY
        compare(refs, false);
#else
        compare(refs, argc == 1);
#endif
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
    return 0;
}
