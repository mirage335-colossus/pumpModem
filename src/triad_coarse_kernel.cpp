// Ordered rank-two coarse scores and the small retained peak frontier.
// The receiver owns admission, immutable correlations, and worker allocation.
// This kernel allocates no memory and creates no threads.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifdef _WIN32
#define TRIAD_EXPORT __declspec(dllexport)
#else
#define TRIAD_EXPORT __attribute__((visibility("default")))
#endif

namespace {
constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();

// Same interior local maxima as scipy.signal.find_peaks: a flat maximum is
// represented by the midpoint rounded down. Endpoints are handled separately.
template<class Visit>
void interior_peaks(const double* score, std::size_t count, Visit visit) noexcept {
    std::size_t i = 1;
    while (i + 1 < count) {
        if (score[i - 1] < score[i]) {
            std::size_t end = i + 1;
            while (end + 1 < count && score[end] == score[i]) ++end;
            if (score[end] < score[i]) visit(i + (end - i - 1) / 2);
            i = end;
        } else {
            ++i;
        }
    }
}

bool separated(std::size_t left, std::size_t right, std::size_t distance) noexcept {
    return (left > right ? left - right : right - left) >= distance;
}

std::int64_t peak_frontier(const double* scores, std::size_t count,
                          std::size_t distance, std::size_t maximum,
                          double comparison_error, std::int64_t* output) noexcept {
    std::size_t selected[2]{absent, absent}, retained = 0;
    for (; retained < maximum; ++retained) {
        std::size_t best = absent;
        bool tie = false;
        interior_peaks(scores, count, [&](std::size_t index) {
            for (std::size_t j = 0; j < retained; ++j)
                if (!separated(index, selected[j], distance)) return;
            if (best == absent || scores[index] > scores[best]) {
                tie = best != absent && scores[index] - scores[best] <= comparison_error;
                best = index;
            } else if (scores[best] - scores[index] <= comparison_error) {
                tie = true;
            }
        });
        if (best == absent) break;
        // SciPy's equal-height priority order is deliberately not redefined.
        if (tie) return -1;
        selected[retained] = best;
    }
    bool more = false;
    if (retained == maximum) {
        interior_peaks(scores, count, [&](std::size_t index) {
            for (std::size_t j = 0; j < retained; ++j)
                if (!separated(index, selected[j], distance)) return;
            more = true;
        });
    }
    std::size_t candidates[4]{}, total = 0;
    for (std::size_t j = 0; j < retained; ++j) candidates[total++] = selected[j];
    if (scores[0] >= scores[1]) candidates[total++] = 0;
    if (scores[count - 1] >= scores[count - 2]) candidates[total++] = count - 1;
    for (std::size_t i = 0; i < total; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (std::abs(scores[candidates[i]] - scores[candidates[j]]) <= comparison_error)
                return -1;
    const bool by_position = total <= maximum && !more;
    for (std::size_t i = 1; i < total; ++i) {
        const auto value = candidates[i];
        std::size_t j = i;
        while (j && (by_position ? value < candidates[j - 1]
                                : scores[value] < scores[candidates[j - 1]])) {
            candidates[j] = candidates[j - 1];
            --j;
        }
        candidates[j] = value;
    }
    if (!by_position) {
        // For K=1/2, the tail of NumPy partition is respectively the maximum,
        // or second-largest then largest. Other K keeps the canonical fallback.
        const auto keep = std::min(maximum, total);
        for (std::size_t i = 0; i < keep; ++i)
            candidates[i] = candidates[total - keep + i];
        total = keep;
    }
    for (std::size_t i = 0; i < total; ++i)
        output[i] = static_cast<std::int64_t>(candidates[i]);
    return static_cast<std::int64_t>(total);
}

template<class Scalar>
void rank2(const char* correlations, std::size_t frequencies, std::size_t lags,
           std::size_t frequency_stride, std::size_t ray_stride,
           const double* inverse, double variance, std::size_t distance,
           std::size_t maximum, double* scores, std::int64_t* indices,
           std::int64_t* counts) noexcept {
    for (std::size_t f = 0; f < frequencies; ++f) {
        const auto* a = reinterpret_cast<const Scalar*>(correlations + f * frequency_stride);
        const auto* b = reinterpret_cast<const Scalar*>(correlations + f * frequency_stride + ray_stride);
        double* score = scores + f * lags;
        bool finite = true;
        double component_max = 0., neighbor_gap = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < lags; ++i) {
            const double ar = a[2 * i], ai = a[2 * i + 1];
            const double br = b[2 * i], bi = b[2 * i + 1];
            double value = inverse[0] * (ar * ar + ai * ai);
            value += inverse[6] * (br * br + bi * bi);
            const double real = ar * inverse[2] + ai * inverse[3];
            const double imag = ar * inverse[3] - ai * inverse[2];
            value += 2. * (real * br - imag * bi);
            score[i] = value / variance;
            finite = finite && std::isfinite(score[i]);
            component_max = std::max(component_max,
                std::max(std::max(std::abs(ar), std::abs(ai)),
                         std::max(std::abs(br), std::abs(bi))));
            if (i) neighbor_gap = std::min(neighbor_gap, std::abs(score[i] - score[i - 1]));
        }
        // NumPy may contract its complex multiplication, so equal mathematical
        // scores need not round identically to this strict ordered expression.
        // Bound all absolute terms by the largest input component, then reserve
        // 64 eps per evaluation (well above their operation counts).
        // Ambiguous local maxima, plateau edges, suppression priorities and
        // endpoint ranks must be decided by the canonical NumPy/SciPy path.
        const double coefficient_bound = std::abs(inverse[0]) + std::abs(inverse[6]) +
            2. * (std::abs(inverse[2]) + std::abs(inverse[3]));
        const double term_bound = 2. * component_max *
            (component_max * coefficient_bound) / variance;
        // Use the normal minimum as a deliberately conservative absolute floor
        // for subnormal intermediates, including their subsequent multipliers.
        // This routes underflow/cancellation ambiguity to canonical arithmetic.
        const double absolute_floor = std::numeric_limits<double>::min() *
            std::max(1., std::max({1., component_max, coefficient_bound}) / variance);
        const double comparison_error = 128. * std::max(absolute_floor,
            std::numeric_limits<double>::epsilon() * term_bound);
        counts[f] = finite && std::isfinite(comparison_error) &&
                    neighbor_gap > comparison_error && lags >= 3 && maximum <= 2
            ? peak_frontier(score, lags, distance, maximum, comparison_error, indices + 2 * f) : -1;
    }
}
}

extern "C" TRIAD_EXPORT int triad_coarse_rank2_peaks_v1(
    const void* correlations, std::size_t frequencies, std::size_t lags,
    std::size_t frequency_stride, std::size_t ray_stride, std::size_t complex_bytes,
    const double* inverse, double variance, std::size_t distance,
    std::size_t maximum, double* scores, std::int64_t* indices,
    std::int64_t* counts) noexcept {
    constexpr auto limit = std::numeric_limits<std::size_t>::max();
    if (!correlations || !inverse || !scores || !indices || !counts ||
        !frequencies || !lags || !distance || !maximum || maximum > 64 ||
        (complex_bytes != 8 && complex_bytes != 16) ||
        !std::isfinite(variance) || variance <= 0. ||
        lags > limit / complex_bytes || lags > limit / sizeof(double) ||
        frequencies > limit / (lags * sizeof(double)) ||
        frequencies > limit / (2 * sizeof(std::int64_t)) ||
        ray_stride < lags * complex_bytes || ray_stride > limit / 2 ||
        frequency_stride < 2 * ray_stride ||
        frequency_stride % complex_bytes || ray_stride % complex_bytes ||
        (frequencies - 1) > (limit - 2 * ray_stride) / frequency_stride ||
        lags > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) return 1;
    for (std::size_t i = 0; i < 8; ++i) if (!std::isfinite(inverse[i])) return 1;
    if (complex_bytes == 16)
        rank2<double>(static_cast<const char*>(correlations), frequencies, lags,
            frequency_stride, ray_stride, inverse, variance, distance, maximum,
            scores, indices, counts);
    else
        rank2<float>(static_cast<const char*>(correlations), frequencies, lags,
            frequency_stride, ray_stride, inverse, variance, distance, maximum,
            scores, indices, counts);
    return 0;
}
