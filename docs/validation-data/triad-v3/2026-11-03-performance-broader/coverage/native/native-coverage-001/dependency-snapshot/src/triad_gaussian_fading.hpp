#pragma once

#include "datapump/types.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>
#include <vector>

namespace datapump::triad::v3::detail {
// Stationary finite-FIR Gaussian-Doppler stress teacher. The target PSD has
// sigma=B/2 Hz; its continuous covariance is exp[-(pi*B*tau)^2/2]. The actual
// discrete teacher is completely specified by its normalized finite kernel.
// It has spectral tails, not compact support or a field-calibrated spectrum.
class GaussianRayFading {
public:
    using Complex = std::complex<double>;
    // Two complex histories, kernel and exported kernel copy require less than
    // 400 KiB at this limit. Per-row work is two real-kernel complex FIRs.
    static constexpr std::size_t maximum_taps = 8193;

    static std::size_t tap_count(double width_hz, double dt_seconds) {
        if (!(std::isfinite(width_hz) && width_hz > 0 &&
              std::isfinite(dt_seconds) && dt_seconds > 0))
            throw Error("Gaussian path needs positive finite width and physical sample interval");
        const double scale = std::numbers::pi*width_hz*dt_seconds;
        if (!std::isfinite(scale) || scale <= 0)
            throw Error("Gaussian path kernel scale is outside numeric range");
        const double half = std::ceil(5/scale);
        if (!std::isfinite(half) || half > (maximum_taps-1)/2)
            throw Error("Gaussian path exceeds the explicit 8193-tap workspace/work bound; use a coarser valid sample clock");
        return 2*static_cast<std::size_t>(half)+1;
    }

    GaussianRayFading(double width_hz, double dt_seconds, std::uint64_t seed)
        : direct_engine_(seed ^ 0x4741555353444952ULL),
          delayed_engine_(seed ^ 0x474155535344454cULL) {
        const auto count = tap_count(width_hz, dt_seconds);
        coefficients_.resize(count);
        direct_history_.resize(count);
        delayed_history_.resize(count);
        const auto half = count/2;
        const double scale = std::numbers::pi*width_hz*dt_seconds;
        long double energy = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double x = scale*(static_cast<double>(i)-static_cast<double>(half));
            coefficients_[i] = std::exp(-x*x);
            energy += static_cast<long double>(coefficients_[i])*coefficients_[i];
        }
        const double normalization = 1/std::sqrt(static_cast<double>(energy));
        for (auto& coefficient : coefficients_) coefficient *= normalization;
        // Fill the complete past before sample0. Zero-filled startup would
        // violate stationary unit power and bias weak long observations.
        for (std::size_t i = 0; i < count; ++i) {
            direct_history_[i] = innovation(direct_engine_, direct_normal_);
            delayed_history_[i] = innovation(delayed_engine_, delayed_normal_);
        }
    }

    std::array<Complex,2> next() {
        std::array<Complex,2> value{};
        const auto count = coefficients_.size();
        const auto first_count = count-head_;
        // Two contiguous ring spans avoid a modulus in the FIR inner loop.
        for (std::size_t i = 0; i < first_count; ++i) {
            value[0] += coefficients_[i]*direct_history_[head_+i];
            value[1] += coefficients_[i]*delayed_history_[head_+i];
        }
        for (std::size_t i = first_count; i < count; ++i) {
            value[0] += coefficients_[i]*direct_history_[i-first_count];
            value[1] += coefficients_[i]*delayed_history_[i-first_count];
        }
        direct_history_[head_] = innovation(direct_engine_, direct_normal_);
        delayed_history_[head_] = innovation(delayed_engine_, delayed_normal_);
        if (++head_ == count) head_ = 0;
        return value;
    }

    std::span<const double> coefficients() const { return coefficients_; }

    double covariance(std::uint64_t lag_samples) const {
        if (lag_samples >= coefficients_.size()) return 0;
        const auto lag = static_cast<std::size_t>(lag_samples);
        long double value = 0;
        for (std::size_t i = 0; i < coefficients_.size()-lag; ++i)
            value += static_cast<long double>(coefficients_[i])*coefficients_[i+lag];
        return static_cast<double>(value);
    }

    std::size_t working_bytes() const {
        return sizeof(*this)+coefficients_.capacity()*sizeof(double)+
            (direct_history_.capacity()+delayed_history_.capacity())*sizeof(Complex);
    }

private:
    static Complex innovation(std::mt19937_64& engine, std::normal_distribution<double>& normal) {
        constexpr double scale = .7071067811865475244;
        const double real = normal(engine), imag = normal(engine);
        return {scale*real, scale*imag};
    }
    std::mt19937_64 direct_engine_, delayed_engine_;
    std::normal_distribution<double> direct_normal_{0,1}, delayed_normal_{0,1};
    std::vector<double> coefficients_;
    std::vector<Complex> direct_history_, delayed_history_;
    std::size_t head_ = 0;
};
} // namespace datapump::triad::v3::detail
