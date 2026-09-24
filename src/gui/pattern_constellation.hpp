#pragma once
#include "datapump/modem.hpp"
#include "../constellation.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace datapump::gui::plots {
inline constexpr std::size_t pattern_observation_limit = 2048;
struct PatternPoint {
    // Differential template coefficient, in coherent quadrature-noise units.
    std::complex<double> centre;
    double radial_sigma = 1;
    double tangential_sigma = 0;
};
struct PatternConstellation {
    // Index is the actual encoded symbol value, using the modem's mapping.
    std::vector<PatternPoint> symbols;
    std::vector<std::complex<double>> observations;
    double quadrature_sigma = 0; // Amplitude units, before plotting division.
    double minimum_template_distance = 0; // Conditional coherent AWGN metric.
    std::pair<std::size_t, std::size_t> closest_symbols{};
    double symbol_seconds = 0;
    double symbol_esn0_db = 0;
    std::size_t omitted = 0;
};

// Every transmitted analytic pattern is one shared sign template times a
// differential phase/amplitude coefficient. Its complete analytic alphabet
// therefore spans exactly two real dimensions; multiplying every chip by the
// same +/-1 code preserves all pairwise distances. This projection needs no
// chip history or separate codeword bank, including for keyed or hour-long
// integrations.
//
// Coordinates use the configured AWGN model, not an inferred receive SNR. With
// E|a|^2 = 2 * nominal_signal_power, quadrature variance is E|a|^2/(2 Es/N0).
// This matches add_awgn() for C/N0 converted to sample SNR at Fs/2. Actual
// quantized integration time makes the scale independent of the sample clock.
// Exact noise-normalized distances apply to this analytic, white-complex-noise
// model. Finite real-PCM carrier windows can have nonorthogonal quadratures:
// their fitted covariance is sample-noise variance times the inverse oscillator
// Gram matrix. The receiver solves that actual Gram system; this display does
// not claim its scalar modeled sigma is an exact PCM covariance measurement.
//
// The Euclidean metric conditions on a fixed phase reference. Differential
// detection adds uncertainty from the preceding received phase. Ellipses show
// a first-order approximation: radial SD is one, tangential SD is
// sqrt(1 + (current_radius/reference_radius)^2). The weakest configured ring
// provides a conservative reference when its actual radius is unavailable.
// These ellipses are not calibrated likelihoods; drift, phase noise and a
// failed fit can move measured points outside the AWGN approximation.
inline PatternConstellation pattern_constellation(const modem::Config& config,
                                                  double cn0_db_hz,
                                                  std::span<const std::complex<double>> measured = {}) {
    modem::validate(config);
    if (!std::isfinite(cn0_db_hz)) throw Error("pattern noise model requires finite C/N0");
    PatternConstellation result;
    result.symbol_seconds = static_cast<double>(modem::symbol_sample_count(config)) / config.sample_rate;
    result.symbol_esn0_db = cn0_db_hz + 10 * std::log10(result.symbol_seconds);
    // Evaluate in the log domain, so very slow integrations and low C/N0 do
    // not overflow intermediate Es/N0 values which would cancel afterwards.
    const auto log_sigma = (std::log(modem::nominal_signal_power) -
                            result.symbol_esn0_db * (std::log(10.) / 10)) / 2;
    if (!std::isfinite(log_sigma) || log_sigma < std::log(std::numeric_limits<double>::min()) ||
        log_sigma > std::log(std::numeric_limits<double>::max()))
        throw Error("pattern noise model exceeds numeric plotting range");
    result.quadrature_sigma = std::exp(log_sigma);
    const auto count = 1U << config.constellation_bits;
    const auto reference_radius = modem::detail::radius_step(config.constellation_bits);
    result.symbols.reserve(count);
    for (unsigned value = 0; value < count; ++value) {
        const auto coefficient = modem::detail::mapped(value, config.constellation_bits, {1, 0});
        const auto centre = coefficient / result.quadrature_sigma;
        if (!std::isfinite(centre.real()) || !std::isfinite(centre.imag()))
            throw Error("pattern constellation exceeds numeric plotting range");
        result.symbols.push_back({centre, 1, std::hypot(1., std::abs(coefficient) / reference_radius)});
    }
    result.minimum_template_distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < result.symbols.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) {
            const auto distance = std::abs(result.symbols[i].centre - result.symbols[j].centre);
            if (distance < result.minimum_template_distance) {
                result.minimum_template_distance = distance;
                result.closest_symbols = {i, j};
            }
        }
    if (!(result.minimum_template_distance > 0) || !std::isfinite(result.minimum_template_distance))
        throw Error("pattern template separation exceeds numeric plotting range");
    const auto count_kept = std::min(pattern_observation_limit, measured.size());
    result.omitted = measured.size() - count_kept;
    result.observations.reserve(count_kept);
    for (const auto point : measured.last(count_kept)) {
        const auto projection = point / result.quadrature_sigma;
        if (std::isfinite(projection.real()) && std::isfinite(projection.imag())) result.observations.push_back(projection);
        else ++result.omitted;
    }
    return result;
}
}
