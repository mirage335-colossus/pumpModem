// Ideal steady-path sizing at the attended threshold. Each normalized
// coherent window has unit complex-Gaussian noise variance.
#include <stdexcept>

namespace steady_sizing {

constexpr double template_false_rate = 5e-5 / (4000.0 * 1024);
constexpr double miss_rate = 1e-3;

// Stable even when k and mean are nearly equal and both exceed 10^8.
double poisson_mass(long k, double mean) {
    if (k == 0) return std::exp(-mean);
    if (k < 32) return std::exp(k * std::log(mean) - mean - std::lgamma(k + 1.0));
    const double d = mean - k, inv = 1.0 / k;
    const double stirling = inv / 12 - std::pow(inv, 3) / 360 + std::pow(inv, 5) / 1260;
    return std::exp(k * std::log1p(d / k) - d - 0.5 * std::log(2 * PI * k) - stirling);
}

// Poisson probability to the indicated side of k, including k. The starting
// point is on that side of the mode; the next ratio bounds the remaining sum.
double poisson_tail(long k, double mean, bool upper) {
    double term = poisson_mass(k, mean), sum = term;
    while (term > 0) {
        const double ratio = upper ? mean / (k + 1) : k / mean;
        if (ratio < 1 && term * ratio <= sum * (1 - ratio) * 2e-14) break;
        term *= ratio;
        sum += term;
        k += upper ? 1 : -1;
    }
    return sum;
}

// Gamma(n,1) survival equals Pr(Poisson(x) < n). All threshold iterates
// exceed the null mean. A checked initial bracket also covers n=1.
double threshold(long windows) {
    double lo = windows, hi = windows + 12 * std::sqrt(double(windows)) + 100;
    if (poisson_tail(windows - 1, hi, false) > template_false_rate)
        throw std::runtime_error("quiet threshold outside bracket");
    for (int step = 0; step < 55; ++step) {
        const double mid = (lo + hi) / 2;
        if (mid == lo || mid == hi) break;
        if (poisson_tail(windows - 1, mid, false) > template_false_rate) lo = mid;
        else hi = mid;
    }
    return hi;
}

// The noncentral-Gamma CDF is Pr(X >= n+Y), for independent Poisson
// X ~ Pois(cutoff), Y ~ Pois(energy). Sum from the upper Y endpoint down,
// updating the X upper tail by a positive mass at each step.
double missed(long n, double energy, double cutoff) {
    const double radius = 14 * std::sqrt(energy + 1) + 40;
    const long high = std::ceil(energy + radius);
    const long low = std::max(0L, long(std::floor(energy - radius)));
    long k = n + high;
    double tail = k > cutoff ? poisson_tail(k, cutoff, true)
                             : 1 - poisson_tail(k - 1, cutoff, false);
    double y_mass = poisson_mass(high, energy);
    double probability = 0;
    for (long y = high; y >= low; --y, --k) {
        probability += y_mass * tail;
        if (y == low) break;
        y_mass *= y / energy;
        tail += poisson_mass(k - 1, cutoff);
    }
    return probability;
}

long rounded_tones(long minimum, long window_tones, double energy_per_tone) {
    // Endpoint with n full windows. All intervals in the tables are either
    // one tone or an integral multiple of the 32-tone block increment.
    auto qualifies = [&](long tones, long n) {
        return missed(n, tones * energy_per_tone, threshold(n)) <= miss_rate;
    };
    if (window_tones == 0) {
        const double cutoff = threshold(1);
        long lo = minimum / 32 - 1, hi = minimum / 32;
        while (missed(1, hi * 32 * energy_per_tone, cutoff) > miss_rate) hi *= 2;
        while (hi - lo > 1) {
            long mid = lo + (hi - lo) / 2;
            if (missed(1, mid * 32 * energy_per_tone, cutoff) <= miss_rate) hi = mid;
            else lo = mid;
        }
        return hi * 32;
    }
    if (qualifies(minimum, (minimum + window_tones - 1) / window_tones)) return minimum;
    if (window_tones == 1) {
        long lo = minimum / 32, hi = 2 * lo;
        while (!qualifies(hi * 32, hi * 32)) { lo = hi; hi *= 2; }
        while (hi - lo > 1) {
            long mid = lo + (hi - lo) / 2;
            if (qualifies(mid * 32, mid * 32)) hi = mid;
            else lo = mid;
        }
        return hi * 32;
    }
    // A short partial aperture adds a full noise degree of freedom. Visit
    // complete-window endpoints in order. No partial window of that count
    // can pass if the full endpoint fails its identical null threshold.
    long windows = (minimum + window_tones - 1) / window_tones;
    while (!qualifies(windows * window_tones, windows)) ++windows;
    long low_repeat = std::max(minimum / 32 - 1, (windows - 1) * window_tones / 32);
    long high_repeat = windows * window_tones / 32;
    const double cutoff = threshold(windows);
    while (high_repeat - low_repeat > 1) {
        long mid = low_repeat + (high_repeat - low_repeat) / 2;
        if (missed(windows, mid * 32 * energy_per_tone, cutoff) <= miss_rate) high_repeat = mid;
        else low_repeat = mid;
    }
    return high_repeat * 32;
}

} // namespace steady_sizing

static void attended_coherent_duration() {
    using steady_sizing::rounded_tones;
    printf("## 21. Steady-path block periods at the attended target\n\n");
    printf("One joint quiet-block budget of 1e-4 reserves 5e-5 for each of the coherent and per-tone searches.\n");
    printf("Each family in this example tests at most 4,000 nuisance hypotheses per word in total, including its\n");
    printf("rungs and offsets, across 1,024 words. The Bonferroni allowance per template is %.12g.\n", steady_sizing::template_false_rate);
    printf("Miss probability on the true template is 0.001. Noise is independent complex Gaussian across\n");
    printf("disjoint windows. Timing, frequency, noise scale and phase law within each window match the template.\n");
    printf("These are theoretical block periods, excluding two trailing quiet blocks and all acquisition or decoding errors.\n\n");
    printf("### HF held tone, FHSS on\n\n");
    printf("One tone per hop. The active aperture is 2, 24, 249, 2,499 or 24,999 cells of 0.16 s respectively.\n");
    printf("Only active time contributes signal energy. Retunes allow independent unknown phases, so the\n");
    printf("per-tone receiver adds energies. At least 32 hops, with exactly 32L transmitted tones.\n\n");
    printf("| C/N0 (dB-Hz) | 0.4 s hop | 4 s hop | 40 s hop | 400 s hop | 4,000 s hop |\n");
    printf("|---:|---:|---:|---:|---:|---:|\n");
    const Dwell hops[] = {{"", .4, 2}, {"", 4, 24}, {"", 40, 249}, {"", 400, 2499}, {"", 4000, 24999}};
    for (int cn : {10, 0, -10, -20, -30}) {
        printf("| %+d |", cn);
        for (const Dwell& hop : hops) {
            long tones = rounded_tones(32, 1, undb(cn) * hop.nc * .16);
            printf(" %.2f |", tones * hop.period);
        }
        printf("\n");
    }
    printf("\n### Sub-9 kHz, FHSS off\n\n");
    printf("Sixteen positions, 10.24 s tones. Subtracting the mean of 16 orthogonal position bins\n");
    printf("in the ideal projection retains 15/16 of the energy. The block uses 32L tones with L >= 2.\n");
    printf("Each window is normalized for its actual noise variance, including any short final window.\n");
    printf("The noncentrality uses only transmitted energy. Maximum window lengths are one tone, 128 tones\n");
    printf("(1,310.72 s), 2,048 tones (20,971.52 s), or the whole block. The two longer fixed windows\n");
    printf("assume that their boundaries match the modeled changes of phase.\n\n");
    printf("| C/N0 (dB-Hz) | Per tone | 128 tones | 2,048 tones | Whole block |\n");
    printf("|---:|---:|---:|---:|---:|\n");
    for (int cn : {-10, -20, -30, -37}) {
        printf("| %+d |", cn);
        for (long width : {1L, 128L, 2048L, 0L}) {
            long tones = rounded_tones(64, width, undb(cn) * 10.24 * (15.0 / 16));
            printf(" %.2f |", tones * 10.24);
        }
        printf("\n");
    }
    printf("\nAll table entries are elapsed seconds. The two budgeted families together still require joint\n");
    printf("testing on actual scores, interference and wrong-schedule traffic. A successful threshold\n");
    printf("crossing can return a competing word. In the ideal model the sum of normalized energies has\n");
    printf("a central or noncentral chi-square law after multiplying by two. The calculation evaluates\n");
    printf("its tails through Poisson probabilities for every row, including large window counts.\n");
    printf("For the 128- and 2,048-tone caps it checks every shorter full-window endpoint and solves\n");
    printf("inside the first eligible window count. The single-tone and hop columns use a bracketed\n");
    printf("search over complete 32-tone increments. They report feasible periods. Where the format floor\n");
    printf("does not determine the answer, the preceding legal length has miss probability above 0.001.\n");
    printf("[Integer-shape gamma identity](https://dlmf.nist.gov/8.4.E10) · ");
    printf("[Noncentral distribution](https://www.boost.org/doc/libs/latest/libs/math/doc/html/math_toolkit/dist_ref/dists/nc_chi_squared_dist.html)\n");
}
