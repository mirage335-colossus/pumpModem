// Coherent search coordinates. Frequencies are cycles per window, drift is the
// end-to-end frequency change in those same units. The frequency epoch is the
// midpoint of the window. Host and CUDA use the same template convention.
#pragma once

#if defined(__CUDACC__)
#define MATRIX_SEARCH_HD __host__ __device__
#else
#define MATRIX_SEARCH_HD
#endif

namespace search_geometry {
constexpr double pi = 3.14159265358979323846;

MATRIX_SEARCH_HD constexpr double midpoint(int sample, int count) {
    return (sample + 0.5) / count - 0.5;
}

MATRIX_SEARCH_HD constexpr double frequency(int index, int count) {
    return (index - 0.5 * (count - 1)) * 0.5;
}

// FFT indices are integers on the zero-padded transform. An even number of
// symmetric half-bin templates needs this extra offset in cycles per window.
MATRIX_SEARCH_HD constexpr double fft_shift(int count) {
    return count % 2 == 0 ? 0.25 : 0.0;
}

MATRIX_SEARCH_HD constexpr double drift(int index) {
    return (index - 1.5) * 3.88;
}

MATRIX_SEARCH_HD constexpr double phase(double x, double frequency_at_midpoint, double drift_bins) {
    return 2 * pi * frequency_at_midpoint * x + pi * drift_bins * x * x;
}
} // namespace search_geometry

#undef MATRIX_SEARCH_HD
