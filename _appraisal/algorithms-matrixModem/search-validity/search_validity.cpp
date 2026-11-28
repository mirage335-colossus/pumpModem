// Deterministic search-geometry checks. No receiver work or random trials.
#include "../sub9-fsk-rate/search_geometry.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <stdexcept>

using Complex = std::complex<double>;
using search_geometry::pi;

constexpr double duration = 5242.88;
constexpr int tones = 512;
constexpr int grid_bins = 174;

void near(double value, double expected, double tolerance) {
    if (std::abs(value - expected) > tolerance) throw std::runtime_error("geometry check failed");
}

double loss(double power) { return -10 * std::log10(power); }

double sinc(double cycles) {
    return cycles == 0 ? 1.0 : std::sin(pi * cycles) / (pi * cycles);
}

// Independent signal: integrate a linear frequency in Hz from t=0 using
// trapezoids, exact in real arithmetic. For mixed hypotheses, report the
// power relative to complete coherent addition of the correct tone traces.
double drift_match(double mid_hz, double hertz_per_second, int bin, double row_drift) {
    const double dt = duration / tones;
    double previous_time = 0;
    double frequency = mid_hz - 0.5 * duration * hertz_per_second;
    double signal_cycles = 0;
    Complex sum{};
    for (int i = 0; i < tones; ++i) {
        const double t = (i + 0.5) * dt;
        const double next_frequency = frequency + hertz_per_second * (t - previous_time);
        signal_cycles += (t - previous_time) * (frequency + next_frequency) * 0.5;
        previous_time = t;
        frequency = next_frequency;
        const double phase = search_geometry::phase(search_geometry::midpoint(i, tones),
                                                    search_geometry::frequency(bin, grid_bins), row_drift);
        sum += std::polar(1.0, 2 * pi * signal_cycles - phase);
    }
    return std::norm(sum / static_cast<double>(tones));
}

double timing_power(double fraction_late, double next_phase) {
    constexpr int cellsamples = 512;
    const int slip = static_cast<int>(std::lround(fraction_late * cellsamples));
    // Same position over a boundary, first tone phase 0, then possibly pi.
    // After perfect frequency/slope removal the extra tail remains in the window.
    Complex sum{};
    for (int i = slip; i < cellsamples + slip; ++i)
        sum += std::polar(1.0, i < cellsamples ? 0.0 : next_phase);
    return std::norm(sum / static_cast<double>(cellsamples));
}

int main() {
    const double mid_hz = search_geometry::frequency(173, grid_bins) / duration;
    const double chirprate = 5.82 / (duration * duration);
    near(drift_match(mid_hz, chirprate, 173, search_geometry::drift(3)), 1.0, 2e-12);
    near(drift_match(-mid_hz, -chirprate, 0, search_geometry::drift(0)), 1.0, 2e-12);
    near(search_geometry::frequency(0, grid_bins), -43.25, 1e-12);
    near(search_geometry::frequency(173, grid_bins), 43.25, 1e-12);
    near(search_geometry::frequency(0, 1), 0.0, 1e-12);
    std::printf("physical drift extrema +/-5.82 bins: both on-grid matches 1.000000\n");

    const double half_frequency = drift_match(mid_hz + 0.25 / duration, chirprate, 173, 5.82);
    const double half_drift = drift_match(mid_hz, chirprate + 1.94 / (duration * duration), 173, 5.82);
    const double corner = drift_match(mid_hz + 0.25 / duration,
                                      chirprate + 1.94 / (duration * duration), 173, 5.82);
    const double old = drift_match(mid_hz, chirprate, 173, 5.82 / 2);
    std::printf("isolated frequency, isolated drift, joint corner losses: %.4f %.4f %.4f dB\n",
                loss(half_frequency), loss(half_drift), loss(corner));
    std::printf("physical +5.82 chirp, best old drift +2.91, midpoint frequency aligned: %.4f dB\n", loss(old));
    near(half_frequency, std::pow(sinc(0.25), 2), 2e-6);
    if (!(old < half_drift && corner < half_frequency && corner < half_drift)) return 1;

    // Sum samples before fine rotation. Calculate one tone directly, and
    // compare to the rectangular aperture formula at the coarse-band edges.
    constexpr int samples = 4096;
    for (int count : {174, 512, 696, 1024, 2048}) {
        const double residual = static_cast<double>(count) / (4 * tones);
        Complex sum{};
        for (int j = 0; j < samples; ++j)
            sum += std::polar(1.0, 2 * pi * residual * (j + 0.5) / samples);
        const double gain = std::abs(sum / static_cast<double>(samples));
        near(gain, std::abs(sinc(residual)), 3e-8);
        if (count == 2048) {
            near(gain, 0, 2e-12);
            std::printf("one reciprocal-of-tone residual: null below 2e-12 in amplitude\n");
        } else {
            std::printf("centred span of %d half-bin cells: edge %.3f Hz, projection loss %.4f dB\n",
                        count, residual * tones / duration, loss(gain * gain));
        }
    }

    for (int denominator : {8, 16}) {
        const double late = 1.0 / denominator;
        near(timing_power(late, 0), 1, 1e-12);
        const double shifted = timing_power(late, pi);
        near(shifted, (1 - 2 * late) * (1 - 2 * late), 1e-12);
        std::printf("window late by 1/%d tone: same phase 0 dB, opposite phase %.4f dB\n",
                    denominator, loss(shifted));
    }

    // Independent sample quadrature of the corrected simulator's complex
    // signal projection, using the full chirp, true and adjacent positions.
    const double f = 43.25, d = 5.82;
    const int tone = tones - 1;
    const double x = search_geometry::midpoint(tone, tones);
    for (int delta : {-1, 0, 1}) {
        Complex integral{};
        for (int j = 0; j < samples; ++j) {
            const double u = (j + 0.5) / samples - 0.5;
            const double t = (x + u / tones) * duration;
            const double cycles = f / duration * t + d / (2 * duration * duration) * t * t + 8 * delta * u;
            integral += std::polar(1.0, 2 * pi * cycles);
        }
        integral /= static_cast<double>(samples);
        const double cycles = 8.0 * delta + (f + d * x) / tones;
        const auto approx = std::polar(1.0, search_geometry::phase(x, f, d)) * sinc(cycles);
        const double error = std::abs(integral - approx);
        std::printf("position displacement %+d: amplitude %.6f, model complex error %.2g\n", delta, std::abs(integral), error);
        if (error > pi * d / (4 * tones * tones) + 1e-6) return 1;
    }
}
