#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

#ifdef DATAPUMP_TRIAD_GAUSSIAN_EXPORT
#define DATAPUMP_TRIAD_GAUSSIAN_DECL DATAPUMP_TRIAD_GAUSSIAN_EXPORT
#else
#define DATAPUMP_TRIAD_GAUSSIAN_DECL
#endif

namespace datapump::triad::v3::receiver_detail {

using GaussianRoot2 = std::array<std::complex<double>, 4>;

// Inputs have already been rescaled by a power of two so these products
// have a safe range. Subtract the largest off-diagonal product first.
inline double gaussian_hermitian_determinant(double a, double d, std::complex<double> cross) {
    auto b = std::abs(cross.real()), c = std::abs(cross.imag());
    if (b < c) std::swap(b, c);
    const auto ad = a*d, bb = b*b, cc = c*c;
    const auto high = (ad - bb) - cc;
    const auto tail = std::fma(a,d,-ad) - std::fma(b,b,-bb) - std::fma(c,c,-cc);
    return high + tail;
}

inline double gaussian_positive_second_pivot(double direct, double echo,
        std::complex<double> cross, double scale) {
    const auto pivot0 = 1. + direct * scale;
    const auto echo_information = echo * scale;
    if (direct == 0.) return 1. + echo_information / pivot0;
    const auto exponent = std::ilogb(std::max({direct,echo,std::abs(cross)}));
    const auto a = std::scalbn(direct, -exponent), d = std::scalbn(echo, -exponent);
    const std::complex<double> b{std::scalbn(cross.real(), -exponent),
                                 std::scalbn(cross.imag(), -exponent)};
    const auto determinant = gaussian_hermitian_determinant(a,d,b);
    if (determinant < -1e-10 * std::max({a*a,d*d,std::norm(b)}))
        return -1.;
    const auto unexplained = std::scalbn(std::max(0., determinant) / a, exponent);
    return 1. + echo_information / pivot0 + (direct * scale / pivot0) * unexplained * scale;
}

// Rank is known from the physical projection here. A rounded, impossible
// correlation in the orthogonal direction cannot contribute evidence.
inline double gaussian_rank_one_log_likelihood(double direct, double echo,
        std::complex<double> cross, std::complex<double> r0, std::complex<double> r1,
        double scale, double inverse_noise) {
    const auto trace_root = std::hypot(std::sqrt(direct), std::sqrt(echo));
    if (trace_root == 0.) return 0.;
    const auto root_scale = std::sqrt(scale);
    const auto root_information = root_scale * trace_root;
    const auto root_denominator = std::hypot(1., root_information);
    const auto normalize = (root_scale / root_denominator) * std::sqrt(inverse_noise);
    const auto gram_ratio = direct >= echo ? cross / direct : std::conj(cross) / echo;
    const auto axis = direct >= echo ?
        normalize * r0 + gram_ratio * (normalize * r1) :
        gram_ratio * (normalize * r0) + normalize * r1;
    const auto logdet = root_information < 1. ? std::log1p(root_information * root_information) :
        2. * std::log(root_denominator);
    return std::norm(axis) / (1. + std::norm(gram_ratio)) - logdet;
}

// Fold a new covariance column into an initially empty or lower triangular
// root by a right unitary rotation. The small residual survives even when
// forming and factoring the dense covariance would erase it.
inline void append_gaussian_root_column(GaussianRoot2& root,
        std::complex<double> first, std::complex<double> second) {
    if (first == std::complex<double>{}) {
        root[3] = std::hypot(root[3].real(), std::abs(second));
        return;
    }
    const auto diagonal = root[0].real();
    const auto magnitude = std::hypot(diagonal, std::abs(first));
    const auto along = diagonal / magnitude;
    const auto across = first / magnitude;
    const auto residual = along * second - across * root[2];
    root[2] = along * root[2] + std::conj(across) * second;
    root[0] = magnitude;
    root[3] = std::hypot(root[3].real(), std::abs(residual));
}

} // namespace datapump::triad::v3::receiver_detail

extern "C" DATAPUMP_TRIAD_GAUSSIAN_DECL int triad_gaussian_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* mean, double* covariance,
    double* scores, double* diagnostics) noexcept;

// Native persistent two-tap route. The state stores a row-major factor F
// with P=F F^H, including between calls. Per-block ranks cap the Gram by
// the number of available physical projection modes, not by conditioning.
extern "C" DATAPUMP_TRIAD_GAUSSIAN_DECL int triad_gaussian_factor2_update_v1(
    std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const unsigned* ranks, const double* power, const double* transitions, double* mean, double* factors,
    double* scores, double* diagnostics) noexcept;

// One observed template with a stationary and a fading gain. The two
// latent coordinates are their sum h and the fading gain d. The sufficient
// statistics r and G use the one-template layout and observe only h.
extern "C" DATAPUMP_TRIAD_GAUSSIAN_DECL int triad_gaussian_additive_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* mean, double* covariance,
    double* scores, double* diagnostics) noexcept;

#undef DATAPUMP_TRIAD_GAUSSIAN_DECL
