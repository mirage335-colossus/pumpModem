// FP64 information-form Gaussian updates for the headless receiver's one-
// and two-tap banks. One job owns a disjoint candidate interval.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

#ifdef _WIN32
#define DATAPUMP_TRIAD_GAUSSIAN_EXPORT __declspec(dllexport)
#else
#define DATAPUMP_TRIAD_GAUSSIAN_EXPORT __attribute__((visibility("default")))
#endif

#include "triad_gaussian_kernel.hpp"

namespace {
using Complex = std::complex<double>;
using datapump::triad::v3::receiver_detail::append_gaussian_root_column;
using datapump::triad::v3::receiver_detail::gaussian_hermitian_determinant;

enum Status {
    success,
    invalid_extent,
    nonfinite_input,
    nonhermitian_gram,
    indefinite_gram,
    indefinite_covariance,
    invalid_innovation,
    nonfinite_state
};

constexpr double tolerance = 1e-10;

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

Complex load(const double* source, std::size_t i) {
    return {source[2*i], source[2*i+1]};
}

void store(double* destination, std::size_t i, Complex value) {
    destination[2*i] = value.real();
    destination[2*i+1] = value.imag();
}

template<std::size_t K>
using Vector = std::array<Complex, K>;

template<std::size_t K>
using Matrix = std::array<Complex, K*K>;

template<std::size_t K>
double magnitude(const Matrix<K>& matrix) {
    double result = 1.;
    for (const auto value : matrix)
        result = std::max(result, std::abs(value));
    return result;
}

template<std::size_t K>
struct Spectral {
    double smallest;
    Matrix<K> root;
    double largest = 0.;
    Matrix<K> axes{};
};

template<std::size_t K>
Spectral<K> spectral_root(const Matrix<K>& hermitian) {
    if constexpr (K == 1) {
        const double eigenvalue = hermitian[0].real();
        return {eigenvalue, {std::sqrt(std::max(0., eigenvalue))}};
    } else {
        const double a = hermitian[0].real(), d = hermitian[3].real();
        const Complex b = hermitian[1];
        const double off = std::abs(b);
        const double scale = std::max({std::abs(a), std::abs(d), off});
        double tangent = 0.;
        Complex phase = 1.;
        if (off != 0.) {
            const double x = std::abs(.5*(a/scale-d/scale)), y = off/scale;
            // The smaller Jacobi tangent avoids subtracting nearly equal
            // roots and preserves a small rotation for disparate diagonals.
            if (y != 0.)
                tangent = y/(std::hypot(x, y)+x);
            phase = b/off;
        }
        const double shift = off*tangent;
        const double high = std::max(a, d)+shift, low = std::min(a, d)-shift;
        const double c = 1./std::hypot(1., tangent), s = c*tangent;
        const double high_root = std::sqrt(std::max(0., high));
        const double low_root = std::sqrt(std::max(0., low));
        if (a >= d)
            return {low, {c*high_root, -phase*(s*low_root),
                          std::conj(phase)*(s*high_root), c*low_root},
                    high, {c, -phase*s, std::conj(phase)*s, c}};
        return {low, {phase*(s*high_root), c*low_root,
                      c*high_root, -std::conj(phase)*(s*low_root)},
                high, {phase*s, c, c, -std::conj(phase)*s}};
    }
}

template<std::size_t K>
Matrix<K> hermitian_product(const Matrix<K>& factor) {
    Matrix<K> result{};
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t col = row; col < K; ++col) {
            Complex value = 0.;
            for (std::size_t tap = 0; tap < K; ++tap)
                value += factor[row*K+tap]*std::conj(factor[col*K+tap]);
            result[row*K+col] = value;
            result[col*K+row] = std::conj(value);
        }
    }
    return result;
}

double resolved_smallest(const Matrix<2>& gram, const Spectral<2>& spectrum) {
    if (gram[1] == Complex{} || !(spectrum.largest > 0.) ||
        std::abs(spectrum.smallest) > tolerance * spectrum.largest)
        return spectrum.smallest;
    // Near rank one, recover the small represented eigenvalue from det(G)
    // / lambda_max. A power-of-two rescale is exact, and FMA preserves the
    // product tails lost from ad - |b|^2 at a tiny positive eigenvalue.
    const auto exponent = std::ilogb(spectrum.largest);
    const auto a = std::scalbn(gram[0].real(), -exponent);
    const auto d = std::scalbn(gram[3].real(), -exponent);
    if ((!a && gram[0].real()) || (!d && gram[3].real())) return spectrum.smallest;
    const Complex b{std::scalbn(gram[1].real(), -exponent), std::scalbn(gram[1].imag(), -exponent)};
    return std::scalbn(gaussian_hermitian_determinant(a,d,b) /
        std::scalbn(spectrum.largest, -exponent), exponent);
}

template<std::size_t K>
Status load_observation(const double* r, const double* g, Vector<K>& correlation,
                        Matrix<K>& gram, double& repair, Spectral<K>* modes = nullptr) {
    for (std::size_t i = 0; i < K; ++i) correlation[i] = load(r, i);
    for (std::size_t i = 0; i < K*K; ++i) gram[i] = load(g, i);
    if (!std::all_of(correlation.begin(), correlation.end(), finite) ||
        !std::all_of(gram.begin(), gram.end(), finite))
        return nonfinite_input;
    const double scale = magnitude<K>(gram);
    if (!std::isfinite(scale)) return nonfinite_input;
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t col = row; col < K; ++col) {
            const auto left = gram[row*K+col], right = std::conj(gram[col*K+row]);
            if (std::abs(left-right) > tolerance*scale) return nonhermitian_gram;
            gram[row*K+col] = .5*left+.5*right;
            gram[col*K+row] = std::conj(gram[row*K+col]);
        }
    }
    auto decomposition = spectral_root<K>(gram);
    if constexpr (K == 2) {
        if (!std::isfinite(decomposition.largest)) return nonfinite_input;
        decomposition.smallest = resolved_smallest(gram, decomposition);
    }
    if (decomposition.smallest < -tolerance*scale) return indefinite_gram;
    repair = std::max(0., -decomposition.smallest)/scale;
    if (modes) *modes = decomposition;
    else if (decomposition.smallest < 0.) gram = hermitian_product<K>(decomposition.root);
    return success;
}

template<std::size_t K>
Status advance(const Vector<K>& correlation, const Matrix<K>& gram, const double* power,
               const double* transition, Vector<K>& mean, Matrix<K>& covariance,
               double& score, double& minimum) {
    static_assert(K == 1);
    const Complex rotation(transition[2], transition[3]);
    for (std::size_t row = 0; row < K; ++row) {
        mean[row] *= rotation;
        for (std::size_t col = 0; col < K; ++col)
            covariance[row*K+col] *= transition[0];
        covariance[row*K+row] += power[row]*transition[1];
    }
    if (!std::all_of(covariance.begin(), covariance.end(), finite))
        return nonfinite_state;
    const auto decomposition = spectral_root<K>(covariance);
    const double relative_minimum = decomposition.smallest/magnitude<K>(covariance);
    if (!std::isfinite(relative_minimum) || relative_minimum < -tolerance)
        return indefinite_covariance;
    minimum = std::min(minimum, relative_minimum);
    const auto& factor = decomposition.root;

    Matrix<K> gram_factor{}, innovation{};
    for (std::size_t row = 0; row < K; ++row)
        for (std::size_t col = 0; col < K; ++col)
            for (std::size_t tap = 0; tap < K; ++tap)
                gram_factor[row*K+col] += gram[row*K+tap]*factor[tap*K+col];
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t col = 0; col < K; ++col)
            for (std::size_t tap = 0; tap < K; ++tap)
                innovation[row*K+col] += std::conj(factor[tap*K+row])*gram_factor[tap*K+col];
        innovation[row*K+row] += 1.;
    }

    // The original one-tap information solve retains the identity term
    // even at high SNR or when the observation contributes no Gram energy.
    const double pivot0 = innovation[0].real();
    if (!std::isfinite(pivot0) || pivot0 <= 0.) return invalid_innovation;
    const double l00 = std::sqrt(pivot0);
    double logdet = std::log(l00);
    Matrix<K> whitened_adjoint{};
    for (std::size_t col = 0; col < K; ++col)
        whitened_adjoint[col] = std::conj(factor[col*K])/l00;
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t col = row; col < K; ++col) {
            Complex value = 0.;
            for (std::size_t tap = 0; tap < K; ++tap)
                value += std::conj(whitened_adjoint[tap*K+row])*whitened_adjoint[tap*K+col];
            covariance[row*K+col] = value;
            covariance[col*K+row] = std::conj(value);
        }
    }

    Vector<K> gram_mean{}, residual{}, adjustment{};
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t tap = 0; tap < K; ++tap)
            gram_mean[row] += gram[row*K+tap]*mean[tap];
        residual[row] = correlation[row]-gram_mean[row];
    }
    double linear = 0., predicted_energy = 0., innovation_energy = 0.;
    for (std::size_t row = 0; row < K; ++row) {
        for (std::size_t tap = 0; tap < K; ++tap)
            adjustment[row] += covariance[row*K+tap]*residual[tap];
        linear += (std::conj(mean[row])*correlation[row]).real();
        predicted_energy += (std::conj(mean[row])*gram_mean[row]).real();
        innovation_energy += (std::conj(residual[row])*adjustment[row]).real();
    }
    score += 2.*linear-predicted_energy+innovation_energy-2.*logdet;
    for (std::size_t i = 0; i < K; ++i) mean[i] += adjustment[i];
    if (!std::isfinite(score) || !std::all_of(mean.begin(), mean.end(), finite) ||
        !std::all_of(covariance.begin(), covariance.end(), finite))
        return nonfinite_state;
    return success;
}

Status advance_two_tap(const Vector<2>& correlation, const Spectral<2>& modes, unsigned rank,
                       bool physical_rank, const double* power, const double* transition,
                       Vector<2>& mean, Matrix<2>& factor, double& score) {
    const Complex rotation{transition[2], transition[3]};
    for (auto& value : mean) value *= rotation;
    const auto decay = std::sqrt(transition[0]);
    for (auto& value : factor) value *= decay;
    if (transition[1] != 0.) {
        Matrix<2> prediction{};
        append_gaussian_root_column(prediction, factor[0], factor[2]);
        append_gaussian_root_column(prediction, factor[1], factor[3]);
        append_gaussian_root_column(prediction, std::sqrt(power[0] * transition[1]), {});
        append_gaussian_root_column(prediction, {}, std::sqrt(power[1] * transition[1]));
        factor = prediction;
    }
    const std::array eigenvalues{std::max(0., modes.largest), std::max(0., modes.smallest)};
    for (unsigned i = 0; i < rank; ++i) {
        const auto gram = eigenvalues[i];
        // r in a zero-energy direction is unobservable in native physical
        // statistics. The dense ABI also accepts a linear-only exponent.
        if (physical_rank && gram == 0.) continue;
        const auto axis0 = std::conj(modes.axes[i]);
        const auto axis1 = std::conj(modes.axes[2+i]);
        const auto r = axis0 * correlation[0] + axis1 * correlation[1];
        if (gram == 0. && r == Complex{}) continue;
        const auto observed = axis0 * mean[0] + axis1 * mean[1];
        const auto u0 = axis0 * factor[0] + axis1 * factor[2];
        const auto u1 = axis0 * factor[1] + axis1 * factor[3];
        const auto norm = std::hypot(std::abs(u0), std::abs(u1));
        const auto root_gram = std::sqrt(gram);
        const auto root_information = root_gram * norm;
        const auto scale = std::hypot(1., root_information);
        if (!std::isfinite(scale)) return invalid_innovation;
        const auto inverse_root = 1. / scale;
        const auto scaled_r = r * inverse_root;
        const auto scaled_mean = observed * inverse_root;
        const auto scaled_prediction = root_gram * scaled_mean;
        const auto logdet = root_information < 1. ? std::log1p(root_information * root_information) :
            2. * std::log(scale);
        // The normalized scalar exponent avoids subtracting two nearly
        // equal O(G) terms when the predicted mean is nonzero.
        score += std::norm(norm * scaled_r) + 2. * (std::conj(scaled_mean) * scaled_r).real()
            - std::norm(scaled_prediction) - logdet;
        if (norm == 0.) continue;
        const auto along0 = u0 / norm, along1 = u1 / norm;
        const auto adjustment = norm * inverse_root * scaled_r
            - root_information * inverse_root * scaled_prediction;
        for (unsigned row = 0; row < 2; ++row) {
            const auto a = factor[2*row], b = factor[2*row+1];
            const auto parallel = a * std::conj(along0) + b * std::conj(along1);
            mean[row] += parallel * adjustment;
            if (gram == 0.) continue;
            factor[2*row] = parallel * inverse_root;
            factor[2*row+1] = b * along0 - a * along1;
        }
    }
    if (!std::isfinite(score) || !std::all_of(mean.begin(), mean.end(), finite) ||
        !std::all_of(factor.begin(), factor.end(), finite))
        return nonfinite_state;
    return success;
}

Status advance_additive(Complex correlation, double gram, double fading_power,
                        const double* transition, Vector<2>& mean, Matrix<2>& covariance,
                        double& score, double& minimum) {
    // State [h, d] contains the observed gain h=s+d and the fading gain d.
    // Its first covariance diagonal retains the observed variance even
    // when observations are too strong to resolve it in coordinates [s,d].
    const auto decay = transition[2];
    const auto difference = -transition[1] / (1. + decay);
    const auto innovation = fading_power * transition[1];
    mean[0] += difference * mean[1];
    mean[1] *= decay;
    const auto cross = decay * (covariance[1] + difference * covariance[3]) + innovation;
    covariance[0] = covariance[0].real() +
        difference * (2. * covariance[1].real() + difference * covariance[3].real()) + innovation;
    covariance[1] = cross;
    covariance[2] = std::conj(cross);
    covariance[3] = covariance[3].real() * transition[0] + innovation;
    if (!std::all_of(covariance.begin(), covariance.end(), finite) ||
        !std::all_of(mean.begin(), mean.end(), finite))
        return nonfinite_state;
    const auto decomposition = spectral_root<2>(covariance);
    const auto relative_minimum = decomposition.smallest / magnitude<2>(covariance);
    if (!std::isfinite(relative_minimum) || relative_minimum < -tolerance)
        return indefinite_covariance;
    minimum = std::min(minimum, relative_minimum);
    if (gram == 0. && correlation == Complex{}) return success;

    // Rotate the prior spectral factor onto the measured row and shrink
    // only that axis. This rank-one update avoids the subtraction of large
    // information pivots and the loss of tiny posterior covariance entries.
    const auto& factor = decomposition.root;
    const auto variance = std::norm(factor[0]) + std::norm(factor[1]);
    const auto root_variance = std::sqrt(variance);
    const auto axis0 = root_variance > 0. ? factor[0] / root_variance : Complex{1., 0.};
    const auto axis1 = root_variance > 0. ? factor[1] / root_variance : Complex{};
    const auto parallel = factor[2] * std::conj(axis0) + factor[3] * std::conj(axis1);
    const auto orthogonal = factor[3] * axis0 - factor[2] * axis1;
    const auto gain_variance = gram * variance;
    const auto scale = 1. + gain_variance;
    if (!std::isfinite(scale) || scale <= 0.) return invalid_innovation;
    const auto inverse = 1. / scale;
    const auto inverse_root = std::sqrt(inverse);
    const auto normalized_correlation = correlation * inverse_root;
    const auto normalized_mean = mean[0] * inverse_root;
    score += variance * std::norm(normalized_correlation) +
        2. * (std::conj(normalized_mean) * normalized_correlation).real() -
        gram * std::norm(normalized_mean) - std::log1p(gain_variance);

    const auto covariance_cross = root_variance * std::conj(parallel);
    const auto residual = correlation * inverse - gram * inverse * mean[0];
    mean[0] += variance * residual;
    mean[1] += std::conj(covariance_cross) * residual;
    covariance[0] = variance * inverse;
    covariance[1] = covariance_cross * inverse;
    covariance[2] = std::conj(covariance[1]);
    covariance[3] = std::norm(orthogonal) + std::norm(parallel) * inverse;
    if (!std::isfinite(score) || !std::all_of(mean.begin(), mean.end(), finite) ||
        !std::all_of(covariance.begin(), covariance.end(), finite))
        return nonfinite_state;
    return success;
}

template<std::size_t K, bool additive=false, bool persistent_factor=false>
Status batch(std::size_t candidates, std::size_t models, std::size_t blocks,
             std::size_t first, std::size_t stop, const double* r, const double* g,
             const double* power, const double* transitions, double* m, double* p,
             double* scores, double* diagnostics, const unsigned* ranks = nullptr) {
    constexpr bool two_tap = K == 2 && !additive;
    for (std::size_t h = 0; h < 3; ++h) {
        for (std::size_t c = first; c < stop; ++c) {
            std::array<Vector<K>, 2> observations{};
            std::array<Matrix<K>, 2> grams{};
            std::array<Spectral<K>, 2> modes{};
            std::array<double, 2> repairs{};
            for (std::size_t block = 0; block < blocks; ++block) {
                const std::size_t at = (block*3+h)*candidates+c;
                Status result;
                if constexpr (additive) {
                    Vector<1> correlation{};
                    Matrix<1> gram{};
                    result = load_observation<1>(r+2*at, g+2*at, correlation, gram, repairs[block]);
                    observations[block][0] = correlation[0];
                    grams[block][0] = gram[0];
                } else {
                    result = load_observation<K>(r+2*at*K, g+2*at*K*K,
                        observations[block], grams[block], repairs[block], two_tap ? &modes[block] : nullptr);
                }
                if (result != success) return result;
            }
            for (std::size_t model = 0; model < models; ++model) {
                const std::size_t at = (model*3+h)*candidates+c;
                Vector<K> mean{};
                Matrix<K> covariance{};
                for (std::size_t i = 0; i < K; ++i) mean[i] = load(m, at*K+i);
                for (std::size_t i = 0; i < K*K; ++i) covariance[i] = load(p, at*K*K+i);
                if constexpr (two_tap && !persistent_factor) {
                    const auto spectrum = spectral_root<K>(covariance);
                    const auto relative = spectrum.smallest / magnitude<K>(covariance);
                    if (!std::isfinite(relative) || relative < -tolerance) return indefinite_covariance;
                    diagnostics[2*model] = std::min(diagnostics[2*model], relative);
                    covariance = spectrum.root;
                }
                double score = scores[at];
                for (std::size_t block = 0; block < blocks; ++block) {
                    const auto* transition = transitions+4*(block*models+model);
                    Status result;
                    if constexpr (additive)
                        result = advance_additive(observations[block][0], grams[block][0].real(), power[1],
                            transition, mean, covariance, score, diagnostics[2*model]);
                    else if constexpr (two_tap)
                        result = advance_two_tap(observations[block], modes[block], ranks ? ranks[block] : 2U,
                            ranks != nullptr, power, transition, mean, covariance, score);
                    else
                        result = advance<K>(observations[block], grams[block], power, transition,
                            mean, covariance, score, diagnostics[2*model]);
                    if (result != success) return result;
                    diagnostics[2*model+1] = std::max(diagnostics[2*model+1], repairs[block]);
                }
                // The legacy dense ABI cannot carry a thin direction below
                // roundoff in its represented entries between separate calls.
                // Native persistent states keep the factor in these slots.
                if constexpr (two_tap && !persistent_factor) {
                    covariance = hermitian_product<K>(covariance);
                    if (!std::all_of(covariance.begin(), covariance.end(), finite)) return nonfinite_state;
                }
                for (std::size_t i = 0; i < K; ++i) store(m, at*K+i, mean[i]);
                for (std::size_t i = 0; i < K*K; ++i) store(p, at*K*K+i, covariance[i]);
                scores[at] = score;
            }
        }
    }
    return success;
}
} // namespace

// Complex128 arrays are passed as interleaved real/imaginary doubles. Inputs
// are immutable; output arrays belong to an uncommitted batch. On error they
// must be discarded. Diagnostics hold [min covariance, max Gram repair] for
// each model and are private to this candidate interval.
extern "C" DATAPUMP_TRIAD_GAUSSIAN_EXPORT int triad_gaussian_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* r, const double* g,
    const double* power, const double* transitions, double* m, double* p,
    double* scores, double* diagnostics) noexcept {
    if ((taps != 1 && taps != 2) || candidates == 0 || candidates > 4096 ||
        models == 0 || models > 64 || blocks == 0 || blocks > 2 ||
        first >= stop || stop > candidates)
        return invalid_extent;
    if (taps == 1)
        return batch<1>(candidates, models, blocks, first, stop, r, g, power,
                        transitions, m, p, scores, diagnostics);
    return batch<2>(candidates, models, blocks, first, stop, r, g, power,
                    transitions, m, p, scores, diagnostics);
}

extern "C" DATAPUMP_TRIAD_GAUSSIAN_EXPORT int triad_gaussian_factor2_update_v1(
    std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* r, const double* g,
    const unsigned* ranks, const double* power, const double* transitions, double* m, double* p,
    double* scores, double* diagnostics) noexcept {
    if (candidates == 0 || candidates > 4096 || models == 0 || models > 64 ||
        blocks == 0 || blocks > 2 || first >= stop || stop > candidates ||
        !ranks || std::any_of(ranks, ranks + blocks, [](unsigned rank) { return rank > 2; }))
        return invalid_extent;
    return batch<2, false, true>(candidates, models, blocks, first, stop, r, g, power,
                                 transitions, m, p, scores, diagnostics, ranks);
}

extern "C" DATAPUMP_TRIAD_GAUSSIAN_EXPORT int triad_gaussian_additive_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* r, const double* g,
    const double* power, const double* transitions, double* m, double* p,
    double* scores, double* diagnostics) noexcept {
    if (taps != 1 || candidates == 0 || candidates > 4096 ||
        models == 0 || models > 64 || blocks == 0 || blocks > 2 ||
        first >= stop || stop > candidates)
        return invalid_extent;
    return batch<2, true>(candidates, models, blocks, first, stop, r, g, power,
                          transitions, m, p, scores, diagnostics);
}
