// Mathematical extracts for the two finite theoretical probes, not a channel
// implementation. See README.md#standalone-recovery-and-model-identity.
#pragma once
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace historical_math {
using Complex=std::complex<double>;
constexpr double tau=2*std::numbers::pi;

// Exact historical phase-bit mapping, shared by both model revisions.
inline std::uint64_t mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
inline auto initial_phases(std::uint64_t seed) {
    std::array<std::array<double,2>,3> result{};
    for(std::size_t band=0;band<3;++band)for(std::size_t component=0;component<2;++component) {
        const auto bits=mix(seed^mix(2*band+component+0x9072d5ULL));
        result[band][component]=tau*(static_cast<double>(bits>>11)+.5)/9007199254740992.;
    }
    return result;
}

// Coefficient construction only from GaussianRayFading at both pinned commits:
// src/triad_gaussian_fading.hpp SHA-256
// 97f26b082d7d68123952bde7ccb9f97148c5977cf741cf6c15c76e77fcc20944.
// No innovations, histories, random distributions or receiver state are needed.
inline std::vector<double> gaussian_coefficients(double width_hz,double dt_seconds) {
    if(!(std::isfinite(width_hz)&&width_hz>0&&std::isfinite(dt_seconds)&&dt_seconds>0))
        throw std::invalid_argument("positive finite Gaussian width and sample interval required");
    const double scale=std::numbers::pi*width_hz*dt_seconds;
    if(!std::isfinite(scale)||scale<=0)throw std::invalid_argument("invalid Gaussian scale");
    const double half_count=std::ceil(5/scale);
    if(!std::isfinite(half_count)||half_count>4096)
        throw std::invalid_argument("Gaussian kernel exceeds historical 8193-tap bound");
    const auto count=2*static_cast<std::size_t>(half_count)+1;
    std::vector<double> coefficients(count);
    const auto half=count/2;
    long double energy=0;
    for(std::size_t i=0;i<count;++i) {
        const double x=scale*(static_cast<double>(i)-static_cast<double>(half));
        coefficients[i]=std::exp(-x*x);
        energy+=static_cast<long double>(coefficients[i])*coefficients[i];
    }
    const double normalization=1/std::sqrt(static_cast<double>(energy));
    for(auto& coefficient:coefficients)coefficient*=normalization;
    return coefficients;
}
}
