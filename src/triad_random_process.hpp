#pragma once

#include "datapump/types.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>

namespace datapump::triad::v3::detail {

inline std::uint64_t random_process_mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline double random_process_uniform(std::uint64_t seed, std::int64_t index) {
    const auto bits = random_process_mix(seed ^ random_process_mix(static_cast<std::uint64_t>(index)));
    return (static_cast<double>(bits >> 11) + .5) / 9007199254740992.;
}

// fixed-grid-gaussian-v1: a random field in physical seconds, never in receiver
// sample indices. B=2 sigma of the target Gaussian Doppler PSD. A stationary
// 53-tap Gaussian FIR on the fixed 16 B knot clock approximates that spectrum.
// Cardinal cubic interpolation is normalized by its EXACT finite-kernel
// variance, so every queried marginal is N(0,1). Random knot origin removes a
// privileged calendar phase. The finite/interpolated spectrum is not compact;
// the native capture guard uses an engineering tail allowance, not hard support.
// Counter innovations plus small caches make both random access and streaming
// deterministic; no duration-sized arrays or receiver-rate-specific draws.
class FixedGaussianProcess {
    static constexpr int radius = 26, taps = 2 * radius + 1;
    static constexpr std::size_t cache_size = 64;
    struct Cached { std::int64_t index = INT64_MIN; double value = 0; };
    std::uint64_t seed_;
    long double clock_, origin_;
    std::array<double, taps> kernel_{};
    std::array<double, 4> covariance_{};
    std::array<Cached, cache_size> innovations_{};
    std::array<Cached, 4> knots_{};

    double innovation(std::int64_t index) {
        auto& cached = innovations_[static_cast<std::uint64_t>(index) % cache_size];
        if (cached.index != index) {
            const double u = random_process_uniform(seed_ ^ 0x5354474155535331ULL, index);
            const double v = random_process_uniform(seed_ ^ 0x5354474155535332ULL, index);
            cached = {index, std::sqrt(-2 * std::log(u)) * std::cos(2 * std::numbers::pi * v)};
        }
        return cached.value;
    }
    double knot(std::int64_t index) {
        auto& cached = knots_[static_cast<std::uint64_t>(index) % knots_.size()];
        if (cached.index != index) {
            double value = 0;
            for (int k = -radius; k <= radius; ++k) value += kernel_[k + radius] * innovation(index + k);
            cached = {index, value};
        }
        return cached.value;
    }
public:
    FixedGaussianProcess(double width_hz, std::uint64_t seed)
        : seed_(seed), clock_(16.L * width_hz),
          origin_(random_process_uniform(seed ^ 0x4b4e4f544f524947ULL, 0)) {
        if (!(std::isfinite(width_hz) && width_hz > 0 && std::isfinite(clock_)))
            throw Error("fixed-time Gaussian process requires positive finite width");
        double energy = 0;
        for (int k = -radius; k <= radius; ++k) {
            const double x = std::numbers::pi * k / 16.;
            const double h = std::exp(-x * x);
            kernel_[k + radius] = h; energy += h * h;
        }
        for (auto& h : kernel_) h /= std::sqrt(energy);
        for (std::size_t lag = 0; lag < covariance_.size(); ++lag)
            for (std::size_t k = 0; k + lag < kernel_.size(); ++k)
                covariance_[lag] += kernel_[k] * kernel_[k + lag];
    }
    double value(long double seconds) {
        const auto coordinate = seconds * clock_ + origin_;
        if (!(std::isfinite(coordinate) && coordinate > INT64_MIN + 64.L && coordinate < INT64_MAX - 64.L))
            throw Error("fixed-time Gaussian process coordinate overflow");
        const auto floor = std::floor(coordinate);
        const auto index = static_cast<std::int64_t>(floor);
        const double u = static_cast<double>(coordinate - floor);
        // Catmull-Rom cardinal cubic, continuous first derivative at knots.
        const std::array<double, 4> w{-.5*u + u*u - .5*u*u*u,
            1 - 2.5*u*u + 1.5*u*u*u, .5*u + 2*u*u - 1.5*u*u*u,
            -.5*u*u + .5*u*u*u};
        double result = 0, variance = 0;
        for (int i = 0; i < 4; ++i) {
            result += w[i] * knot(index + i - 1);
            for (int j = 0; j < 4; ++j) variance += w[i] * w[j] * covariance_[std::abs(i-j)];
        }
        return result / std::sqrt(variance);
    }
    double bounded_value(long double seconds) {
        // Phi(g)*2-1: exactly uniform[-1,1] marginal for this unit Gaussian.
        return std::erf(value(seconds) / std::sqrt(2.));
    }
    double knot_rate_hz() const { return static_cast<double>(clock_); }
    const auto& coefficients() const { return kernel_; }
};

// Independent proper-complex unit-mean-power field, with the same real-process
// width convention and fixed physical clock. This is diffuse fading, not a
// deterministic sinusoidal path or a measured universal propagation model.
class FixedGaussianRay {
    FixedGaussianProcess real_, imag_;
public:
    FixedGaussianRay(double width_hz, std::uint64_t seed)
        : real_(width_hz, seed ^ 0x5245414c4741494eULL),
          imag_(width_hz, seed ^ 0x494d41474741494eULL) {}
    std::complex<double> value(long double seconds) {
        constexpr double scale = .7071067811865475244;
        return scale * std::complex<double>{real_.value(seconds), imag_.value(seconds)};
    }
};

// Bounded geometric motion: independent uniform knot values joined with C2
// smootherstep on a randomly translated physical-time grid. |tau'| <=3.75 A/T.
// The bound, not a realized trace, determines history, tails and capture guards.
class SmoothRandomDelay {
    double nominal_, amplitude_, interval_;
    std::uint64_t seed_;
    long double origin_;
public:
    static double maximum_slope(double amplitude, double interval) { return 3.75 * amplitude / interval; }
    SmoothRandomDelay(double nominal, double amplitude, double interval, std::uint64_t seed)
        : nominal_(nominal), amplitude_(amplitude), interval_(interval), seed_(seed),
          origin_(random_process_uniform(seed ^ 0x44454c41594f5247ULL, 0)) {
        if (!(std::isfinite(nominal) && nominal >= 0 && std::isfinite(amplitude) &&
              amplitude >= 0 && amplitude <= nominal && std::isfinite(interval) && interval > 0 &&
              std::isfinite(maximum_slope(amplitude, interval)) && maximum_slope(amplitude, interval) < 1))
            throw Error("random delay needs nonnegative range and maximum slope below one");
    }
    double value(long double seconds) const {
        if (amplitude_ == 0) return nominal_;
        const auto coordinate = seconds / interval_ + origin_;
        if (!(std::isfinite(coordinate) && coordinate > INT64_MIN + 2.L && coordinate < INT64_MAX - 2.L))
            throw Error("random delay coordinate overflow");
        const auto floor = std::floor(coordinate);
        const auto index = static_cast<std::int64_t>(floor);
        const double u = static_cast<double>(coordinate - floor);
        const double s = u*u*u*(10 + u*(-15 + 6*u));
        const double a = 2*random_process_uniform(seed_, index)-1;
        const double b = 2*random_process_uniform(seed_, index+1)-1;
        return nominal_ + amplitude_*(a+(b-a)*s);
    }
};
} // namespace datapump::triad::v3::detail
