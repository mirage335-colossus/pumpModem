// Check the simulator's complex bin values against independent sample integration.
#define main coherent_block_mc_main
#include "coherent_block_mc.cpp"
#undef main

int main() {
    constexpr int P = 16;
    constexpr int cells = 3;
    constexpr int subdivisions = 32768;
    const uint8_t positions[] = {15, 0, 3};
    const std::vector<cd> antenna(P, cd(0.7, -0.2));
    Rng rng(11);
    GaussianRays rays(rng, 2.56);
    rays.phase.fill(cd{});
    rays.offset.fill(0.0);
    // A +1-bin edge image, an off-grid component, and a stationary component.
    rays.phase[0] = 1.0;
    rays.offset[0] = 1.0;
    rays.phase[1] = cd(-0.3, 0.7);
    rays.offset[1] = -0.375;
    rays.phase[2] = cd(0.5, -0.1);
    const auto initial = rays.phase;
    std::vector<cf> actual(cells * P);
    rays.project(positions, 1, cells, antenna, actual);
    double maximum = 0;
    for (int cell = 0; cell < cells; ++cell) {
        std::array<cd, P> expected{};
        for (int n = 0; n < subdivisions; ++n) {
            const double u = (n + 0.5) / subdivisions;
            cd gain{};
            for (int k = 0; k < 3; ++k)
                gain += initial[k] * std::polar(1.0, 2 * PI * rays.offset[k] * (cell + u));
            for (int q = 0; q < P; ++q)
                expected[q] += gain * std::polar(1.0, 2 * PI * (positions[cell] - q) * u);
        }
        for (int q = 0; q < P; ++q) {
            const cd value = expected[q] * antenna[positions[cell]] /
                             (subdivisions * std::sqrt(static_cast<double>(rays.count)));
            maximum = std::max(maximum, std::abs(value - cd(actual[cell * P + q])));
        }
    }
    std::printf("Maximum complex projection error: %.4g\n", maximum);
    return maximum < 2e-8 ? 0 : 1;
}
