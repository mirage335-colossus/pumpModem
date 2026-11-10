// FP64 information-form Gaussian updates for the headless receiver's one-
// and two-tap banks. One job owns a disjoint candidate interval.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

#ifdef _WIN32
#define TRIAD_EXPORT __declspec(dllexport)
#else
#define TRIAD_EXPORT __attribute__((visibility("default")))
#endif

namespace {
using Complex = std::complex<double>;

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
                          std::conj(phase)*(s*high_root), c*low_root}};
        return {low, {phase*(s*high_root), c*low_root,
                      c*high_root, -std::conj(phase)*(s*low_root)}};
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

template<std::size_t K>
Status load_observation(const double* r, const double* g, Vector<K>& correlation,
                        Matrix<K>& gram, double& repair) {
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
    const auto decomposition = spectral_root<K>(gram);
    if (decomposition.smallest < -tolerance*scale) return indefinite_gram;
    repair = std::max(0., -decomposition.smallest)/scale;
    if (decomposition.smallest < 0.) gram = hermitian_product<K>(decomposition.root);
    return success;
}

template<std::size_t K>
Status advance(const Vector<K>& correlation, const Matrix<K>& gram, const double* power,
               const double* transition, Vector<K>& mean, Matrix<K>& covariance,
               double& score, double& minimum) {
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

    // Use the same positive-definite square-root information solve as the
    // general receiver. In particular, do not invert P or drop the identity
    // term for high-SNR or rank-deficient observations.
    const double pivot0 = innovation[0].real();
    if (!std::isfinite(pivot0) || pivot0 <= 0.) return invalid_innovation;
    const double l00 = std::sqrt(pivot0);
    double logdet = std::log(l00);
    Matrix<K> whitened_adjoint{};
    for (std::size_t col = 0; col < K; ++col)
        whitened_adjoint[col] = std::conj(factor[col*K])/l00;
    if constexpr (K == 2) {
        const Complex l10 = (.5*innovation[2]+.5*std::conj(innovation[1]))/l00;
        const double pivot1 = innovation[3].real()-std::norm(l10);
        if (!std::isfinite(pivot1) || pivot1 <= 0.) return invalid_innovation;
        const double l11 = std::sqrt(pivot1);
        for (std::size_t col = 0; col < K; ++col)
            whitened_adjoint[K+col] = (std::conj(factor[col*K+1])-l10*whitened_adjoint[col])/l11;
        logdet += std::log(l11);
    }
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

template<std::size_t K>
Status batch(std::size_t candidates, std::size_t models, std::size_t blocks,
             std::size_t first, std::size_t stop, const double* r, const double* g,
             const double* power, const double* transitions, double* m, double* p,
             double* scores, double* diagnostics) {
    for (std::size_t h = 0; h < 3; ++h) {
        for (std::size_t c = first; c < stop; ++c) {
            std::array<Vector<K>, 2> observations{};
            std::array<Matrix<K>, 2> grams{};
            std::array<double, 2> repairs{};
            for (std::size_t block = 0; block < blocks; ++block) {
                const std::size_t at = (block*3+h)*candidates+c;
                const auto result = load_observation<K>(r+2*at*K, g+2*at*K*K,
                    observations[block], grams[block], repairs[block]);
                if (result != success) return result;
            }
            for (std::size_t model = 0; model < models; ++model) {
                const std::size_t at = (model*3+h)*candidates+c;
                Vector<K> mean{};
                Matrix<K> covariance{};
                for (std::size_t i = 0; i < K; ++i) mean[i] = load(m, at*K+i);
                for (std::size_t i = 0; i < K*K; ++i) covariance[i] = load(p, at*K*K+i);
                double score = scores[at];
                for (std::size_t block = 0; block < blocks; ++block) {
                    const auto result = advance<K>(observations[block], grams[block], power,
                        transitions+4*(block*models+model), mean, covariance,
                        score, diagnostics[2*model]);
                    if (result != success) return result;
                    diagnostics[2*model+1] = std::max(diagnostics[2*model+1], repairs[block]);
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
extern "C" TRIAD_EXPORT int triad_gaussian_update_v1(
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
