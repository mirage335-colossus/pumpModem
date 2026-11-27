// codebook_mc.cpp
//
// THEORETICAL / MONTE CARLO CALCULATION (research material; not modem code, links to nothing in the
// repository). Supports _ref/algorithms-matrixModem/Fable-5.2/reviews/review-coding-interleaving.md.
//
// Part 1. A keyed random q-ary codebook ("bank") of 2^k codewords over n symbol slots, decoded by
//         exhaustive maximum likelihood on Rayleigh-faded tone energies, against the exact fully
//         orthogonal reference of short_message_energy.cpp (same n = L, same E/N0).
// Part 2. Information per q-ary symbol when the same symbol is repeated over L independently
//         Rayleigh-faded looks and the looks are square-law combined, against (a) L times the
//         single-look information (what an ideal lower-rate code over L distinct symbols would
//         collect) and (b) one look carrying all the energy (a longer coherent dwell).
// Part 3. The same bank decoder under interference that raises the noise in part of the
//         time-frequency plane, for several per-cell metrics that need no side information,
//         against genie metrics that know which cells were hit.
//
// Model: one independent Rayleigh fade per symbol slot (ideal interleaving), ideal energy cells
// (exponentially distributed), Gaussian interference, known timing and frequency, no quantisation.
// Noise-only cell energy has mean 1. All numbers are "derived here" Monte Carlo estimates; the
// printed +- values are one binomial standard error.
//
// Static scheduling with one generator per thread: results repeat exactly for the same thread
// count (12 threads for the stored output) and differ within the stated standard errors otherwise.
//
// Build and run (about 22 minutes on a 12-thread laptop):
//   g++ -O3 -march=native -std=c++17 -fopenmp codebook_mc.cpp -o /tmp/codebook_mc
//   /tmp/codebook_mc > results/codebook_mc.md
//
// Standard library only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
#include <omp.h>

struct Rng {
    uint64_t s[4];
    explicit Rng(uint64_t seed) { for (int i = 0; i < 4; i++) { seed += 0x9E3779B97F4A7C15ULL; uint64_t z = seed; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; s[i] = z ^ (z >> 31); } }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() { uint64_t r = rotl(s[0] + s[3], 23) + s[0], t = s[1] << 17; s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45); return r; }
    double uni() { return ((next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
    float expo() { return (float)-std::log(uni()); }
};

// ---------- Part 1 ----------
// Returns error count over trials for a random q-ary bank.
// q1 = 0: every codeword symbol is drawn independently and uniformly. q1 > 0: product-structured
// bank; the message is (u1, u2) with k/2 and k - k/2 bits, the tone index in slot i is
// a_i(u1) * (q/q1) + b_i(u2) with independent random maps a_i (to q1 values) and b_i (to q/q1 values).
// Its score matrix over (u1, u2) is a matrix product of a one-hot [2^k1 x n q1] matrix with a
// gathered [n q1 x 2^k2] metric matrix.
static void fill_codebook(std::vector<uint8_t> &C, Rng &r, int k, int q, int n, int q1) {
    const long M = 1L << k;
    if (q1 <= 0) { for (auto &c : C) c = (uint8_t)(r.next() % q); return; }
    const int k1 = k / 2, k2 = k - k1, q2 = q / q1; const long M1 = 1L << k1, M2 = 1L << k2;
    std::vector<uint8_t> a((size_t)M1 * n), b((size_t)M2 * n);
    for (auto &x : a) x = (uint8_t)(r.next() % q1);
    for (auto &x : b) x = (uint8_t)(r.next() % q2);
    for (long u1 = 0; u1 < M1; u1++) for (long u2 = 0; u2 < M2; u2++) for (int i = 0; i < n; i++)
        C[((u1 << k2) | u2) * n + i] = (uint8_t)(a[u1 * n + i] * q2 + b[u2 * n + i]);
    (void)M;
}

static void bank_run(int k, int q, int n, int q1, double gam, long trials, uint64_t seed, long &errs) {
    const long M = 1L << k; long e = 0;
    #pragma omp parallel reduction(+:e)
    {
        Rng r(seed * 1315423911ULL + 7919ULL * omp_get_thread_num());
        std::vector<uint8_t> C(M * n); std::vector<float> Y((size_t)n * q); long cnt = 0;
        #pragma omp for schedule(static)
        for (long t = 0; t < trials; t++) {
            if (cnt++ % 32 == 0) fill_codebook(C, r, k, q, n, q1);
            for (auto &y : Y) y = r.expo();
            long m = (long)(r.next() % M);
            for (int i = 0; i < n; i++) Y[(size_t)i * q + C[m * n + i]] *= (float)(1 + gam);
            float sm = 0; for (int i = 0; i < n; i++) sm += Y[(size_t)i * q + C[m * n + i]];
            bool bad = false;
            for (long j = 0; j < M && !bad; j++) {
                if (j == m) continue;
                const uint8_t *c = &C[j * n]; float s = 0;
                for (int i = 0; i < n; i++) s += Y[(size_t)i * q + c[i]];
                if (s >= sm) bad = true;
            }
            e += bad;
        }
    }
    errs = e;
}

// ---------- Part 2 ----------
static void mi_run(int q, int L, double gam, long trials, uint64_t seed, double &mean, double &se) {
    double s1 = 0, s2 = 0; const double a = gam / (1 + gam);
    #pragma omp parallel reduction(+:s1, s2)
    {
        std::mt19937_64 g(seed * 2654435761ULL + 104729ULL * omp_get_thread_num());
        std::gamma_distribution<double> gd((double)L, 1.0);
        #pragma omp for schedule(static)
        for (long t = 0; t < trials; t++) {
            double S1 = (1 + gam) * gd(g), acc = 0;
            for (int j = 1; j < q; j++) acc += std::exp(a * (gd(g) - S1));
            double v = std::log2((double)q) - std::log2(1 + acc);
            s1 += v; s2 += v * v;
        }
    }
    mean = s1 / trials; se = std::sqrt(std::max(0.0, s2 / trials - mean * mean) / trials);
}

// ---------- Part 3 ----------
enum { MET_LINEAR, MET_CLIP, MET_NORM, MET_NORMCLIP, MET_NORMCLIPW, MET_LOG, MET_RANK, MET_GENIE_ML, MET_GENIE_ERASE, NMET };
static const char *metname[NMET] = {"linear (square-law sum)", "clip at 8 x known quiet noise", "slot-median normalised", "slot-median normalised, clip at 8", "slot-median normalised, clip at 8, weight floor/median", "log(1 + y/median)", "rank within slot", "genie ML (knows hits, J, SNR)", "genie erasure (knows hits)"};

// type 0: slot hits (all q cells of a slot get noise 1+J); type 1: independent cell hits.
static void interf_run(int k, int q, int n, double gam, int type, double rho, double J, long trials, uint64_t seed, long errs[NMET]) {
    const long M = 1L << k; long e[NMET] = {0};
    #pragma omp parallel
    {
        Rng r(seed * 2246822519ULL + 3266489917ULL * omp_get_thread_num());
        std::vector<uint8_t> C(M * n); std::vector<float> Y((size_t)n * q), T((size_t)NMET * n * q), tmp(q), meds(n), msort(n); std::vector<uint8_t> hit((size_t)n * q);
        long le[NMET] = {0}, cnt = 0;
        #pragma omp for schedule(static)
        for (long t = 0; t < trials; t++) {
            if (cnt++ % 32 == 0) for (auto &c : C) c = (uint8_t)(r.next() % q);
            long m = (long)(r.next() % M);
            for (int i = 0; i < n; i++) {
                bool sh = (type == 0) && (r.uni() < rho);
                for (int c = 0; c < q; c++) {
                    bool h = (type == 0) ? sh : (r.uni() < rho);
                    hit[(size_t)i * q + c] = h;
                    double mean = 1 + (h ? J : 0) + ((c == C[m * n + i]) ? gam : 0);
                    Y[(size_t)i * q + c] = (float)(mean * -std::log(r.uni()));
                }
            }
            // Per-slot noise estimate: median of the slot's q cell energies over ln 2 (the mean of an
            // exponential with that median). The quiet floor is the lower quartile of the slot estimates.
            for (int i = 0; i < n; i++) {
                const float *y = &Y[(size_t)i * q];
                std::copy(y, y + q, tmp.begin());
                std::nth_element(tmp.begin(), tmp.begin() + q / 2, tmp.end());
                meds[i] = std::max(tmp[q / 2] / 0.6931472f, 1e-6f);
            }
            std::copy(meds.begin(), meds.end(), msort.begin());
            std::nth_element(msort.begin(), msort.begin() + n / 4, msort.end());
            const float floorv = msort[n / 4];
            for (int i = 0; i < n; i++) {
                const float *y = &Y[(size_t)i * q];
                const float med = meds[i], wslot = std::min(1.0f, floorv / med);
                for (int c = 0; c < q; c++) {
                    size_t o = (size_t)i * q + c; float v = y[c];
                    T[MET_LINEAR * (size_t)n * q + o] = v;
                    T[MET_CLIP * (size_t)n * q + o] = std::min(v, 8.0f);
                    T[MET_NORM * (size_t)n * q + o] = v / med;
                    T[MET_NORMCLIP * (size_t)n * q + o] = std::min(v / med, 8.0f);
                    T[MET_NORMCLIPW * (size_t)n * q + o] = std::min(v / med, 8.0f) * wslot;
                    T[MET_LOG * (size_t)n * q + o] = std::log1p(v / med);
                    int rk = 0; for (int d = 0; d < q; d++) rk += (y[d] < v);
                    T[MET_RANK * (size_t)n * q + o] = (float)rk;
                    double N = 1 + (hit[o] ? J : 0);
                    T[MET_GENIE_ML * (size_t)n * q + o] = (float)(v * gam / (N * (N + gam)) - std::log1p(gam / N));
                    T[MET_GENIE_ERASE * (size_t)n * q + o] = hit[o] ? 0.0f : v;
                }
            }
            for (int mt = 0; mt < NMET; mt++) {
                const float *Tm = &T[mt * (size_t)n * q];
                float sm = 0; for (int i = 0; i < n; i++) sm += Tm[(size_t)i * q + C[m * n + i]];
                bool bad = false;
                for (long j = 0; j < M && !bad; j++) {
                    if (j == m) continue;
                    const uint8_t *c = &C[j * n]; float s = 0;
                    for (int i = 0; i < n; i++) s += Tm[(size_t)i * q + c[i]];
                    if (s >= sm) bad = true;
                }
                le[mt] += bad;
            }
        }
        #pragma omp critical
        for (int mt = 0; mt < NMET; mt++) e[mt] += le[mt];
    }
    for (int mt = 0; mt < NMET; mt++) errs[mt] = e[mt];
}

int main() {
    printf("# Keyed random bank, repetition efficiency and robust metrics (Monte Carlo)\n\n");
    printf("Generated by `codebook_mc.cpp` (Monte Carlo; all values derived here). One independent Rayleigh fade per\n");
    printf("symbol slot, ideal energy cells, noise-only cell mean 1, known timing and frequency. See the source header.\n\n");

    // ---- Part 1 ----
    printf("## 1. Keyed random q-ary bank with exhaustive ML against the fully orthogonal reference\n\n");
    printf("The reference energy E_ref is the exact requirement of 2^k fully orthogonal codewords with the same number\n");
    printf("of branches n (from `short_message_energy.md`), where the message error is `target`. The bank reuses the\n");
    printf("same q tones in every slot, so two codewords collide in a slot with probability 1/q. A fresh random\n");
    printf("codebook is drawn every 32 trials (ensemble average).\n\n");
    printf("`structure` 8x8 is the product-structured bank (tone index = 8 a_i(u1) + b_i(u2)); see the source.\n\n");
    printf("| k | q | n | structure | target | E_ref/N0 | pe at E_ref | pe at +0.25 dB | pe at +0.5 dB | pe at +1 dB | pe at +2 dB | excess energy for target (dB) |\n|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    struct P1 { int k, q, n, q1; double target, Eref; long trials; };
    const std::vector<P1> p1 = {
        {10, 64, 18, 0, 1e-3, 66.29, 2000000}, {10, 64, 18, 8, 1e-3, 66.29, 2000000}, {10, 16, 18, 0, 1e-3, 66.29, 2000000}, {10, 8, 18, 0, 1e-3, 66.29, 2000000},
        {10, 4, 18, 0, 1e-3, 66.29, 2000000}, {10, 64, 9, 0, 1e-3, 73.72, 2000000}, {10, 64, 36, 0, 1e-3, 70.64, 1000000},
        {16, 64, 14, 0, 1e-2, 61.37, 200000}, {16, 64, 14, 8, 1e-2, 61.37, 200000}, {16, 64, 28, 0, 1e-2, 65.36, 100000},
        {20, 64, 15, 0, 1e-2, 69.70, 10000}, {20, 256, 15, 0, 1e-2, 69.70, 10000},
    };
    const double offs[5] = {0, 0.25, 0.5, 1.0, 2.0};
    for (const auto &c : p1) {
        double pe[5];
        for (int o = 0; o < 5; o++) { long e; double E = c.Eref * std::pow(10.0, offs[o] / 10); bank_run(c.k, c.q, c.n, c.q1, E / c.n, c.trials, 1000 + o + 17 * c.q + 131 * c.k + c.n + 7 * c.q1, e); pe[o] = (double)e / c.trials; }
        // excess: log-linear interpolation of pe against dB
        double ex = NAN;
        if (pe[0] <= c.target) ex = 0;
        for (int o = 1; o < 5 && std::isnan(ex); o++) if (pe[o] <= c.target && pe[o - 1] > c.target && pe[o] > 0)
            ex = offs[o - 1] + (offs[o] - offs[o - 1]) * (std::log(pe[o - 1]) - std::log(c.target)) / (std::log(pe[o - 1]) - std::log(pe[o]));
        printf("| %d | %d | %d | %s | %.0e | %.2f | %.2e +- %.1e | %.2e | %.2e | %.2e | %.2e | ", c.k, c.q, c.n, c.q1 ? "8x8" : "random", c.target, c.Eref, pe[0], std::sqrt(pe[0] / c.trials), pe[1], pe[2], pe[3], pe[4]);
        if (std::isnan(ex)) printf("> 2 |\n"); else printf("%.2f |\n", ex);
        fflush(stdout);
    }
    printf("\nE_ref values are those of section 9 of `short_message_energy.md` (n = 9, 18 and 36 for k = 10; n = 14 and 28 for k = 16) and of its section 2 (k = 20, eps = 1e-2: 69.70 at L = 15).\n\n");

    // ---- Part 2 ----
    printf("## 2. Repetition with square-law combining: information per q-ary symbol\n\n");
    printf("`I_L` is the mutual information (bits) of one q-ary symbol sent on L independently Rayleigh-faded looks at\n");
    printf("per-look SNR g, square-law combined (ML for Rayleigh). `efficiency` = I_L / (L I_1): 1 means repetition\n");
    printf("collects as much as an ideal lower-rate code over L distinct symbols at the same per-look SNR.\n");
    printf("`one look` = I_1 at SNR L g: all the energy in a single coherent look (no diversity inside the symbol).\n\n");
    for (int q : {64, 1024}) {
        printf("**q = %d** (log2 q = %.0f bits)\n\n| per-look SNR (dB) | L | I_L (bits) | efficiency | one look at L g (bits) | I_L / log2 q |\n|---:|---:|---:|---:|---:|---:|\n", q, std::log2((double)q));
        for (double gdb : {-10.0, -5.0, 0.0, 5.0}) {
            double g = std::pow(10.0, gdb / 10), I1 = 0, se1 = 0;
            long base = (q == 64) ? 4000000 : 400000;
            for (int L : {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024}) {
                double m, se, m1, s1;
                long tr = (L == 1) ? base * ((gdb < -1) ? 4 : 1) : base / 4;
                mi_run(q, L, g, tr, 77 + L + (int)(gdb * 10) + q, m, se);
                if (L == 1) { I1 = m; se1 = se; }
                mi_run(q, 1, g * L, base / 4, 991 + L + (int)(gdb * 10) + q, m1, s1);
                printf("| %.0f | %d | %.4f +- %.4f | %.3f | %.4f | %.3f |\n", gdb, L, m, se, m / (L * I1), m1, m / std::log2((double)q));
                fflush(stdout);
                if (m > 0.995 * std::log2((double)q)) break;
            }
            (void)se1;
        }
        printf("\n");
    }

    // ---- Part 3 ----
    printf("## 3. Bank decoding under interference: metrics without side information\n\n");
    printf("k = 10, q = 64, random bank, exhaustive decoding with the summed per-cell metric. Both designs carry 6 dB\n");
    printf("more energy than the no-margin requirement (E/N0 = 264): design A keeps n = 18 slots and makes each 6 dB\n");
    printf("stronger; design B uses n = 72 slots at the no-margin slot SNR. `slot hits`: with probability rho a slot\n");
    printf("has its noise raised to 1 + J in all q cells (pulsed or broadband interference). `cell hits`: each cell\n");
    printf("independently has its noise raised to 1 + J with probability rho (narrowband or multitone interference\n");
    printf("after keyed tone permutation). Entries are message error probabilities; 0 means no error in the trials.\n");
    printf("The slot noise estimate is the median of the slot's q cell energies divided by ln 2; the `floor` is the\n");
    printf("lower quartile of the n slot estimates of the same message. No metric except the two genie rows and the\n");
    printf("`known quiet noise` clip uses anything but the received energies.\n\n");
    struct D { const char *name; int n; double gam; long trials; };
    const std::vector<D> ds = {{"A: n = 18, slot SNR 11.7 dB", 18, 3.683 * 3.981, 200000}, {"B: n = 72, slot SNR 5.7 dB", 72, 3.683, 100000}};
    for (int type = 0; type < 2; type++) for (double Jdb : {20.0, 40.0}) for (const auto &d : ds) {
        printf("**%s, J = %.0f dB, design %s** (%ld trials per column)\n\n| metric |", type ? "cell hits" : "slot hits", Jdb, d.name, d.trials);
        const std::vector<double> rhos = type ? std::vector<double>{0, 0.01, 0.03, 0.1, 0.2, 0.3} : std::vector<double>{0, 0.1, 0.25, 0.5, 0.75, 0.9};
        for (double rho : rhos) printf(" rho=%.2f |", rho);
        printf("\n|---|"); for (size_t i = 0; i < rhos.size(); i++) printf("---:|"); printf("\n");
        std::vector<std::vector<long>> res(NMET, std::vector<long>(rhos.size()));
        for (size_t ri = 0; ri < rhos.size(); ri++) { long e[NMET]; interf_run(10, 64, d.n, d.gam, type, rhos[ri], std::pow(10.0, Jdb / 10), d.trials, 5000 + ri + 10 * type + (int)Jdb + d.n, e); for (int mt = 0; mt < NMET; mt++) res[mt][ri] = e[mt]; }
        for (int mt = 0; mt < NMET; mt++) { printf("| %s |", metname[mt]); for (size_t ri = 0; ri < rhos.size(); ri++) printf(" %.1e |", (double)res[mt][ri] / d.trials); printf("\n"); }
        printf("\n"); fflush(stdout);
    }
    return 0;
}
