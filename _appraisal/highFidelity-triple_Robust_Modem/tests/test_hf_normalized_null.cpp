#include "../src/hf_normalized_null.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace appraisal::normalized_null_detail;

void check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}

bool close(double a, double b) {
    return std::abs(a - b) < 4e-12 * std::max({1., std::abs(a), std::abs(b)});
}

void dimensionless_covariance() {
    constexpr std::size_t n = 12;
    constexpr double gram = 2.4, energy = 11., contrast = 4.;
    const Statistics raw{{Complex{1.2, -.83}, {}}, {gram, {}, {}, {}}};
    const auto native = gaussian_terms(raw, 1, 1., contrast / gram);
    const auto expected_j = std::norm(raw.q[0]) / gram * contrast / (1 + contrast);
    const auto expected_log = -std::log1p(contrast) - n * std::log1p(-expected_j / energy);
    check(close(native.explained, expected_j) && close(native.determinant, std::log1p(contrast)) &&
          close(direction_score(n, energy, native), expected_log), "native Gaussian terms disagree with analytic dimensionless covariance");
    for (const double variance : {1e-6, .01, 1., 100.}) {
        const auto terms = gaussian_terms(raw, 1, variance, variance * contrast / gram);
        check(close(terms.determinant, native.determinant) && close(terms.explained * variance, native.explained) &&
              close(direction_score(n, energy / variance, terms), expected_log),
              "equivalent noise normalization changed dimensionless shape or evidence");
    }
    for (const auto scale : {Complex{-.03, .07}, Complex{0., -1}, Complex{100., 30.}}) {
        auto scaled = raw;
        scaled.q[0] *= scale;
        const auto value = gaussian_terms(scaled, 1, 1., contrast / gram);
        check(close(direction_score(n, energy * std::norm(scale), value), expected_log),
              "common observation scale/phase changed the direction likelihood");
    }
    auto scaled_template = raw;
    scaled_template.q[0] *= .03;
    scaled_template.gram[0] *= .03 * .03;
    const auto moved = gaussian_terms(scaled_template, 1, 1., contrast / scaled_template.gram[0].real());
    check(close(direction_score(n, energy, moved), expected_log), "public template Gram normalization did not remove its amplitude scale");
}

constexpr std::size_t block_rows = 3, total_rows = 2 * block_rows;
using Observation = std::array<Complex, total_rows>;
using Columns = std::array<std::array<Complex, 2>, total_rows>;

Terms dense_covariance(const Observation& y, const Columns& a, double variance, double gain, bool shared) {
    std::array<std::array<Complex, total_rows>, total_rows> lower{};
    double determinant = 0;
    for (std::size_t i = 0; i < total_rows; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            Complex value = i == j ? 1. : 0.;
            if (shared || i / block_rows == j / block_rows)
                for (std::size_t tap = 0; tap < 2; ++tap) value += gain / variance * a[i][tap] * std::conj(a[j][tap]);
            for (std::size_t k = 0; k < j; ++k) value -= lower[i][k] * std::conj(lower[j][k]);
            if (i == j) {
                check(value.real() > 0, "dense covariance control is not positive definite");
                lower[i][i] = std::sqrt(value.real());
                determinant += std::log(value.real());
            } else lower[i][j] = value / lower[j][j];
        }
    }
    Observation whitened{};
    double explained = 0;
    for (std::size_t i = 0; i < total_rows; ++i) {
        auto value = y[i];
        for (std::size_t j = 0; j < i; ++j) value -= lower[i][j] * whitened[j];
        whitened[i] = value / lower[i][i];
        explained += (std::norm(y[i]) - std::norm(whitened[i])) / variance;
    }
    return {explained, determinant};
}

void independent_gains_across_blocks() {
    constexpr Observation y{{{.4, -.9}, {-.6, 1.2}, {1.6, .5}, {.5, .3}, {-.8, -.7}, {1.1, -.2}}};
    constexpr Columns a{{
        {{{1., .3}, {.1, 1.1}}}, {{{.5, -.4}, {-.8, .3}}}, {{{.2, .7}, {.3, -.2}}},
        {{{-.1, .8}, {.4, -.2}}}, {{{-.8, -.5}, {.2, .7}}}, {{{.3, 1.2}, {-1.4, .4}}},
    }};
    std::array<BitStatistics, 2> blocks{};
    double trace = 0;
    for (std::size_t row = 0; row < total_rows; ++row) {
        auto& block = blocks[row / block_rows][0];
        for (std::size_t tap = 0; tap < 2; ++tap) {
            const auto adjoint = std::conj(a[row][tap]);
            trace += std::norm(a[row][tap]);
            block.q[tap] += adjoint * y[row];
            for (std::size_t other = 0; other < 2; ++other) block.gram[2 * tap + other] += adjoint * a[row][other];
        }
    }
    constexpr double variance = .43;
    const auto scored = score_gain_shape(blocks, 0, 2, variance, {});
    check(scored.independent_gain_blocks == 2 && scored.span.rank == 4 && scored.span.near_singular_blocks == 0 &&
          close(scored.span.trace, trace), "two full-rank tap spans did not sum to aperture rank four and the common Gram trace");
    for (std::size_t i = 0; i < contrast_bank.size(); ++i) {
        const auto gain = contrast_bank[i] * 4 / trace;
        const auto dense = dense_covariance(y, a, 1., gain, false);
        const auto native = scored.angular[i];
        check(close(native.explained, dense.explained) && close(native.determinant, dense.determinant),
              "global rank/trace normalized terms differ from independent dense block covariance");
    }
    for (std::size_t i = 0; i < physical_power.size(); ++i) {
        const auto dense = dense_covariance(y, a, variance, physical_power[i] / 2, false);
        const auto native = scored.gaussian[i];
        check(close(native.explained, dense.explained) && close(native.determinant, dense.determinant),
              "known-noise terms differ from dense independent block covariance");
    }
    const auto unit_contrast = scored.angular[2];
    const auto shared_equal_gain = dense_covariance(y, a, 1., 4 / trace, true);
    const auto shared_rank_two_normalization = dense_covariance(y, a, 1., 2 / trace, true);
    check(!close(shared_equal_gain.explained, unit_contrast.explained) &&
          !close(shared_equal_gain.determinant, unit_contrast.determinant) &&
          !close(shared_rank_two_normalization.explained, unit_contrast.explained),
          "shared aperture-wide tap gains accidentally represent the independent block model");
}

double score_projector(std::size_t modes, std::size_t rank, double fraction, double beta) {
    return direction_score(modes, 1., {fraction * beta, -static_cast<double>(rank) * std::log1p(-beta)});
}

void scalar_projector_optimum() {
    constexpr std::size_t modes = 64, rank = 3;
    const auto occupancy = double(rank) / modes;
    for (const auto fraction : {.01, occupancy, .08, .23, .82, .997}) {
        const auto optimum = fraction > occupancy ? (fraction - occupancy) / (fraction * (1 - occupancy)) : 0.;
        const auto bound = projector_bound(modes, rank, fraction);
        check(close(score_projector(modes, rank, fraction, optimum), bound), "projector KL expression is not attained at its analytic optimum");
        for (const double beta : {0., .01, .1, .5, .91, .99, .999})
            check(score_projector(modes, rank, fraction, beta) <= bound + 1e-11,
                  "scalar projector model exceeded its optimized diagnostic bound");
    }
    check(projector_bound(modes, modes, 1.) == 0. && std::isinf(projector_bound(modes, rank, 1.)) &&
          projector_bound(modes, 0, 0.) == 0., "identity and exact span projector limits differ");
}

void colored_null_still_violates_spherical_bound() {
    constexpr std::size_t modes = 12;
    constexpr double contrast = 32., threshold = 3.;
    const auto beta = contrast / (1 + contrast);
    const auto fraction = (1 - std::exp((-threshold - std::log1p(contrast)) / modes)) / beta;
    check(close(score_projector(modes, 1, fraction, beta), threshold), "rank-one directional threshold is inconsistent");
    // T=|u^H y|²/||y||²: the favored direction has an exponential energy
    // with mean v and its orthogonal complement an independent unit-scale
    // Gamma(modes-1) energy. Rotation gives a correlated proper complex null;
    // choosing one coordinate gives variance imbalance in disjoint blocks.
    const auto tail = [fraction](double v) {
        return std::pow(1 + fraction / ((1 - fraction) * v), -static_cast<double>(modes - 1));
    };
    const double budget = std::exp(-threshold);
    check(tail(1.) < budget && tail(4.) > budget && tail(16.) > budget,
          "fixed dimensionless covariance mistakenly treats non-spherical null tails as calibrated");
}
} // namespace

int main() {
    try {
        dimensionless_covariance();
        independent_gains_across_blocks();
        scalar_projector_optimum();
        colored_null_still_violates_spherical_bound();
        std::cout << "HF dimensionless covariance invariance, independent two-tap blocks, scalar projector bound and non-spherical null controls passed\n";
    } catch (const std::exception& error) {
        std::cerr << "HF normalized null component check failed: " << error.what() << '\n';
        return 1;
    }
}
