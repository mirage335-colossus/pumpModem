// Matrix Modem FSK lattice: receiver calculations for the specification draft.
//
// Lattice (complex baseband, N0 = 1, received peak = average power P = C/N0 in Hz):
//   sample rate 3200 Hz, cell = 512 samples = 160 ms, bin = 6.25 Hz (the FT8 cell);
//   dwell = one tone held on one bin; dwell period D with an active part of nc whole cells
//   that starts 128 samples (40 ms) after the dwell start:
//     A  D = 0.4 s  (1280 samples),   nc = 2    active 0.32 s
//     B  D = 4 s    (12800 samples),  nc = 24   active 3.84 s
//     C  D = 40 s   (128000 samples), nc = 249  active 39.84 s
//   RF phase and fading are taken as independent from dwell to dwell (FHSS or keyed audio hop),
//   so only the statistics inside one dwell matter.
//
// Sections (markdown on stdout):
//   1  transmit envelope and receive window of the 0.4 s dwell (static channel, closed form)
//   2  exact large-alphabet GMI of quadratic receivers per dwell option versus the peak-limited
//      bound: per-cell single bin (FT8-like), dwell-coherent bin, spread-matched weighted
//      periodogram, and the two-stage form (cell FFT, then a slow-time transform of one or three
//      cell bins)
//   3  efficiency of quantizing the per-dwell evidence to 1..4 bits
//   4  finite alphabet with a keyed tone permutation per dwell: Monte Carlo GMI for M = 64/256,
//      tone spacing k bins, L dwells per symbol (0.4 s dwell)
//   5  the same for the 4 s dwell
//   6  unknown Doppler shift of the path: fixed wide weighting against a centre-hypothesis bank
//   7  block duration estimate from the presence threshold
//   8  sensitivity cost of reduced transforms (tone-grid bins only, one bin) and of shedding dwells;
//      not in the default set, run with the section argument "8"
//   9  extreme spreads (100-500 Hz) and other cell lengths (40 ms to 11 min); run with "9"
//   10 FHSS off: dwells without guard, coherent accumulation across dwells, few tone positions
//      (Monte Carlo of the keyed block decoded coherently) and block duration; run with "A"
//   11 FHSS dwell length with a new phase and frequency offset every dwell: block duration against
//      dwell period, block energy against the number of dwells, and an observer without the key;
//      run with "B"
//   12 a keyed phase in every cell of a dwell: timing cost to the keyed receiver, observer margins
//      with and without it, the cost of phase changes the receiver does not know; run with "C"
//   13 tone length: one cell per tone against a tone held for the hop; run with "D"
//   14 one-cell tones with keyed hopping: observer margin against hop length and channels; run with "E"
//   15 ten bits against one bit under the presence-test sizing; run with "F"
//   16 hops a block has to span to survive losing a fraction of them; run with "G"
//   19 selectable null-block budget for the one-bit and ten-bit presence calculation; run with "J"
//
// Method notes. For a metric L = sum_k q_k |Y_k|^2 over orthonormal analysis functions with
// independent CN(0,1) noise, the large-alphabet GMI is sup_s sum_k [s q_k (1+lam_k) + ln(1 - s q_k)]
// nat per dwell with lam_k = E|signal part of Y_k|^2 (exact; second-order statistics only), as in
// ../coherence-limits/wideband_receivers.cpp. The bound is P - int ln(1 + P S(f)) df nat/s.
// Gaps are 10 log10(R_bound * D / GMI): extra transmit time at the same power, guard time included.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp lattice_receiver.cpp -o lattice_receiver
// Run:   ./lattice_receiver [mc_trials] [sections, default 1234567] > results/lattice_receiver.md
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <omp.h>

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;
static const double LN2 = 0.69314718055994530942;
static const double FS = 3200.0;
static const int CELL = 512;
static double db(double x) { return 10 * std::log10(x); }
static double undb(double x) { return std::pow(10.0, x / 10); }

struct Rng {
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (int i = 0; i < 4; ++i) { seed += 0x9E3779B97F4A7C15ull; uint64_t z = seed; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; s[i] = z ^ (z >> 31); }
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() { uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17; s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45); return r; }
    double uni() { return ((next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
    cd cn() { double u = uni(), v = uni(), r = std::sqrt(-std::log(u)); return {r * std::cos(2 * PI * v), r * std::sin(2 * PI * v)}; }
};

static void fft(std::vector<cd>& a, bool inv) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * PI / len * (inv ? 1 : -1);
        cd wl = std::polar(1.0, ang);
        for (size_t i = 0; i < n; i += len) { cd w = 1; for (size_t j = 0; j < len / 2; ++j) { cd u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

// ---------- channel ----------
struct Comp { enum Kind { LINE, GAUSS } kind; double w, c, s; };  // power fraction, centre Hz, sigma Hz
using Chan = std::vector<Comp>;
struct NamedChan { std::string name; Chan ch; };
static Chan G(double two_sigma, double w = 1, double c = 0) { return {{Comp::GAUSS, w, c, two_sigma / 2}}; }
static Chan LINE(double w = 1, double c = 0) { return {{Comp::LINE, w, c, 0}}; }
static Chan operator+(Chan a, const Chan& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static double extent(const Chan& ch) { double m = 0; for (auto& k : ch) m = std::max(m, std::fabs(k.c) + (k.kind == Comp::GAUSS ? 5 * k.s : 0)); return m; }
static cd rho(const Chan& ch, double tau) {
    cd r = 0;
    for (auto& k : ch) r += k.w * (k.kind == Comp::LINE ? 1.0 : std::exp(-2 * PI * PI * k.s * k.s * tau * tau)) * std::polar(1.0, 2 * PI * k.c * tau);
    return r;
}
static double bound_rate(const Chan& ch, double P) {  // nat/s; lines carry their power at the coherent rate
    double lo = 1e300, hi = -1e300, ms = 1e300, wl = 0; bool any = false;
    for (auto& k : ch) { if (k.kind == Comp::GAUSS) { any = true; lo = std::min(lo, k.c - 14 * k.s); hi = std::max(hi, k.c + 14 * k.s); ms = std::min(ms, k.s); } else wl += k.w; }
    if (!any) return P;
    int n = (int)std::min(4e6, std::max(2e4, (hi - lo) / ms * 200)); if (n % 2) ++n;
    double h = (hi - lo) / n, sum = 0;
    for (int i = 0; i <= n; ++i) {
        double f = lo + i * h, d = 0;
        for (auto& k : ch) if (k.kind == Comp::GAUSS) d += k.w / (std::sqrt(2 * PI) * k.s) * std::exp(-0.5 * (f - k.c) * (f - k.c) / (k.s * k.s));
        sum += ((i == 0 || i == n) ? 1 : (i % 2 ? 4 : 2)) * std::log1p(P * d);
    }
    return P - sum * h / 3;
}
// expected signal energy in the DFT bins k/T (k = -K..K) of a rectangular window of length T
static std::vector<double> bin_energy(const Chan& ch, double P, double T, int K) {
    double fmax = std::max(extent(ch), (K + 2) / T);
    int N = 64; while (N / T < 4 * fmax + 8 / T) N <<= 1;
    double dt = T / N; int L = 2 * N;
    std::vector<cd> c(L, 0.0);
    for (int n = -(N - 1); n <= N - 1; ++n) c[(n + L) % L] += dt * (1 - std::fabs(n * dt) / T) * rho(ch, n * dt);
    fft(c, false);
    std::vector<double> lam(2 * K + 1);
    for (int k = -K; k <= K; ++k) lam[k + K] = std::max(0.0, P * c[(2 * k + L) % L].real());
    return lam;
}
// sup_s sum_k [ s q_k (1 + lam_k) + ln(1 - s q_k) ], nat
static double gmi_inf(const std::vector<double>& q, const std::vector<double>& lam) {
    double qmax = 0; for (double v : q) qmax = std::max(qmax, v);
    if (qmax <= 0) return 0;
    auto fp = [&](double s) { double d = 0; for (size_t k = 0; k < q.size(); ++k) if (q[k] > 0) d += q[k] * (1 + lam[k]) - q[k] / (1 - s * q[k]); return d; };
    double lo = 0, hi = (1 - 1e-12) / qmax;
    if (fp(lo) <= 0) return 0;
    for (int it = 0; it < 200; ++it) { double mid = 0.5 * (lo + hi); if (fp(mid) > 0) lo = mid; else hi = mid; }
    double s = 0.5 * (lo + hi), f = 0;
    for (size_t k = 0; k < q.size(); ++k) if (q[k] > 0) f += s * q[k] * (1 + lam[k]) + std::log1p(-s * q[k]);
    return std::max(0.0, f);
}
static double gmi_wp(const std::vector<double>& lam_true, const std::vector<double>& lam_model) {
    std::vector<double> q(lam_model.size());
    for (size_t k = 0; k < q.size(); ++k) q[k] = lam_model[k] / (1 + lam_model[k]);
    return gmi_inf(q, lam_true);
}
static int span_bins(const Chan& ch, double T) { return std::max(2, (int)std::ceil((extent(ch) + 4.0 / T) * T)); }

// two-stage receiver: cell FFT bin (offset b cell bins from the tone), then an nc-point transform
// across the dwell's cells. Analysis functions are orthonormal. Returns lam for j = 0..nc-1.
static std::vector<double> two_stage_energy(const Chan& ch, double P, int nc, int b) {
    std::vector<cd> rc(2 * nc - 1);
    for (int dlt = -(nc - 1); dlt <= nc - 1; ++dlt) {
        cd acc = 0;
        for (int u = -(CELL - 1); u <= CELL - 1; ++u)
            acc += double(CELL - std::abs(u)) * rho(ch, (dlt * CELL + u) / FS) * std::polar(1.0, -2 * PI * b * u / double(CELL));
        rc[dlt + nc - 1] = acc;
    }
    std::vector<double> lam(nc);
    double N = double(nc) * CELL;
    for (int j = 0; j < nc; ++j) {
        cd acc = 0;
        for (int dlt = -(nc - 1); dlt <= nc - 1; ++dlt) acc += double(nc - std::abs(dlt)) * rc[dlt + nc - 1] * std::polar(1.0, -2 * PI * j * dlt / double(nc));
        lam[j] = std::max(0.0, P / (FS * N) * acc.real());
    }
    return lam;
}

struct Dwell { const char* name; double period; int nc; };

// ---------- Section 1 ----------
static void section1() {
    printf("## 1. Transmit envelope and receive window of the 0.4 s dwell\n\n");
    printf("Static channel, closed form. `energy` is the coherent tone energy collected by the window relative to a\n");
    printf("tone that fills the whole 0.4 s dwell (dB; this is also the loss of signal power). `evidence` is the\n");
    printf("low-SNR evidence per dwell, which goes as energy squared (dB of transmit time).\n\n");
    const int N = 1280;
    auto rc = [](double x) { return 0.5 - 0.5 * std::cos(PI * x); };  // amplitude raised cosine 0..1
    struct Env { const char* name; int q, r; };  // quiet samples per edge, ramp samples
    std::vector<Env> envs = {
        {"no guard (tone fills the dwell; reference)", 0, 0},
        {"proposed: 20 ms quiet + 20 ms ramp per edge, 0.32 s flat", 64, 64},
        {"triad FHSS envelope: 55 ms quiet + 20 ms ramp per edge, 0.25 s flat", 176, 64},
    };
    struct Win { const char* name; int a, b; bool matched; };
    std::vector<Win> wins = {
        {"rectangular 2 cells [40, 360) ms", 128, 1152, false},
        {"rectangular 1 cell centred [120, 280) ms", 384, 896, false},
        {"matched to the envelope", 0, N, true},
    };
    printf("| Envelope | Receive window | energy (dB) | evidence (dB) |\n|---|---|---:|---:|\n");
    for (auto& e : envs) {
        std::vector<double> env(N, 0.0);
        for (int n = 0; n < N; ++n) {
            int d = std::min(n, N - 1 - n);  // distance to the nearer edge
            if (d < e.q) env[n] = 0;
            else if (d < e.q + e.r) env[n] = rc((d - e.q + 0.5) / e.r);
            else env[n] = 1;
        }
        for (auto& w : wins) {
            double num = 0, den = 0;
            for (int n = w.a; n < w.b; ++n) { double ww = w.matched ? env[n] : 1.0; num += ww * env[n]; den += ww * ww; }
            double lam = den > 0 ? num * num / den / N : 0;  // relative to P*0.4 s
            printf("| %s | %s | %.2f | %.2f |\n", e.name, w.name, db(lam), 2 * db(lam));
        }
    }
    printf("\n");
}

// ---------- Section 2 ----------
static void section2(const std::vector<NamedChan>& chans) {
    printf("## 2. Dwell options against the bound (large alphabet, exact)\n\n");
    printf("Columns: `FT8 cell` is a continuous stream of 160 ms cells with one-bin detection and no guard time\n");
    printf("(reference). `A` 0.4 s, `B` 4 s, `C` 40 s dwell periods with 2, 24 and 249 active cells. `cell` sums the\n");
    printf("one-bin energies of the dwell's cells; `coh` is one bin over the active part; `WP` is the spread-matched\n");
    printf("weighted periodogram over the active part; `2s1`/`2s3` is the two-stage form (cell FFT, then a slow-time\n");
    printf("transform of the tone's cell bin alone, or of it and both neighbours) with matched weights.\n");
    printf("Entries: extra transmit time (dB) relative to the bound at the same C/N0, guard time included.\n\n");
    std::vector<Dwell> dw = {{"A", 0.4, 2}, {"B", 4.0, 24}, {"C", 40.0, 249}};
    for (double c : {-20.0, -10.0, 0.0, 10.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n", c);
        printf("| Channel | bound Eb/N0 (dB) | FT8 cell | A cell | A coh | A WP | B cell | B WP | B 2s1 | B 2s3 | C WP | C 2s1 | C 2s3 |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        for (auto& nc : chans) {
            double Rb = bound_rate(nc.ch, P);
            printf("| %s | %.2f |", nc.name.c_str(), db(P * LN2 / Rb));
            double lc = bin_energy(nc.ch, P, 0.16, 0)[0];
            double g_ft8 = lc - std::log1p(lc);
            printf(" %.2f |", db(Rb * 0.16 / g_ft8));
            for (auto& d : dw) {
                double T = d.nc * 0.16;
                double g_cell = d.nc * g_ft8;
                int K = span_bins(nc.ch, T);
                auto lam = bin_energy(nc.ch, P, T, K);
                double l0 = lam[K], g_coh = l0 - std::log1p(l0);
                double g_wp = gmi_wp(lam, lam);
                if (d.nc == 2) { printf(" %.2f | %.2f | %.2f |", db(Rb * d.period / g_cell), db(Rb * d.period / g_coh), db(Rb * d.period / g_wp)); continue; }
                std::vector<double> l1 = two_stage_energy(nc.ch, P, d.nc, 0), l3 = l1;
                for (int b : {-1, 1}) { auto t = two_stage_energy(nc.ch, P, d.nc, b); l3.insert(l3.end(), t.begin(), t.end()); }
                double g1 = gmi_wp(l1, l1), g3 = gmi_wp(l3, l3);
                if (d.nc == 24) printf(" %.2f | %.2f | %.2f | %.2f |", db(Rb * d.period / g_cell), db(Rb * d.period / g_wp), db(Rb * d.period / g1), db(Rb * d.period / g3));
                else printf(" %.2f | %.2f | %.2f |", db(Rb * d.period / g_wp), db(Rb * d.period / g1), db(Rb * d.period / g3));
            }
            printf("\n"); fflush(stdout);
        }
        printf("\n");
    }
    // spread-hypothesis bank for the 0.4 s dwell
    printf("### Spread-hypothesis bank for the 0.4 s dwell\n\n");
    printf("Weights from an assumed Gaussian spread (rows) on the actual spread (columns), at C/N0 = -10 dB-Hz; the\n");
    printf("assumed C/N0 equals the actual. Gap to the bound (dB of time).\n\n| assumed \\ actual |");
    std::vector<double> sp = {0.1, 1, 3, 10, 30, 60};
    for (double a : sp) printf(" %g Hz |", a);
    printf("\n|---|"); for (size_t i = 0; i < sp.size(); ++i) printf("---:|"); printf("\n");
    {
        double P = undb(-10), T = 0.32; int K = span_bins(G(60), T);
        std::vector<double> rows = {0.1, 3, 10, 30, 60};
        for (double m : rows) {
            printf("| %g Hz |", m);
            auto lm = bin_energy(G(m), P, T, K);
            for (double a : sp) { auto lt = bin_energy(G(a), P, T, K); printf(" %.2f |", db(bound_rate(G(a), P) * 0.4 / gmi_wp(lt, lm))); }
            printf("\n");
        }
        printf("| one bin |");
        for (double a : sp) { auto lt = bin_energy(G(a), P, T, K); printf(" %.2f |", db(bound_rate(G(a), P) * 0.4 / (lt[K] - std::log1p(lt[K])))); }
        printf("\n\n");
    }
}

// ---------- Section 3 ----------
// efficiency (low-SNR, locally optimum sense) of a b-bit uniform quantizer g = clamp(floor(z/step),0,2^b-1)
// applied to z ~ Exp(1): Corr0(g, z)^2. Closed-form integration.
static double exp_quant_eff(int bits, double step, double* clip_prob = nullptr) {
    int L = 1 << bits;
    double Eg = 0, Eg2 = 0, Egz = 0;
    for (int i = 0; i < L; ++i) {
        double a = i * step, b = (i == L - 1) ? 1e9 : (i + 1) * step;
        double p = std::exp(-a) - std::exp(-b);
        double ez = (a + 1) * std::exp(-a) - (b < 1e8 ? (b + 1) * std::exp(-b) : 0.0);
        Eg += i * p; Eg2 += double(i) * i * p; Egz += i * ez;
    }
    if (clip_prob) *clip_prob = std::exp(-(L - 1) * step);
    double cov = Egz - Eg * 1.0, var = Eg2 - Eg * Eg;
    return var > 0 ? cov * cov / var : 0;  // Var0(z) = 1
}
// GMI (nat) of the quantized one-bin evidence in Rayleigh fading with mean per-dwell SNR g, large alphabet
static double exp_quant_gmi(int bits, double step, double g) {
    int L = 1 << bits;
    std::vector<double> p0(L), p1(L);
    for (int i = 0; i < L; ++i) {
        double a = i * step, b = (i == L - 1) ? 1e9 : (i + 1) * step;
        p0[i] = std::exp(-a) - std::exp(-b);
        p1[i] = std::exp(-a / (1 + g)) - std::exp(-b / (1 + g));
    }
    // optimum metric is the level log-likelihood ratio; GMI with that metric = D(p1||p0)
    double d = 0; for (int i = 0; i < L; ++i) if (p1[i] > 0 && p0[i] > 0) d += p1[i] * std::log(p1[i] / p0[i]);
    // integer-level metric g = i: sup_s { s E1[i] - ln E0[exp(s i)] }
    double best = 0;
    for (double s = 0.001; s < 20; s *= 1.02) {
        double e1 = 0, m0 = 0; for (int i = 0; i < L; ++i) { e1 += i * p1[i]; m0 += p0[i] * std::exp(s * i); }
        best = std::max(best, s * e1 - std::log(m0));
    }
    (void)d;
    return best;
}
static void section3() {
    printf("## 3. Quantizing the per-dwell evidence\n\n");
    printf("One-bin evidence z (noise mean 1, exponential law) quantized to `g = min(2^b - 1, floor(z / step))`, the\n");
    printf("integer g then being summed over dwells. `low-SNR loss` is `-10 log10 Corr0(g, z)^2`: the extra transmit\n");
    printf("time relative to summing unquantized energies (the power-equivalent is half). Closed form. The step is\n");
    printf("optimized for low SNR; `clip` is the noise-only probability of the top level. The other columns are the\n");
    printf("large-alphabet GMI loss (dB of time) in Rayleigh fading at the stated mean SNR per dwell with the same step.\n\n");
    printf("| bits | step | clip | low-SNR loss | -5 dB | 0 dB | +5 dB | +10 dB | +15 dB |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int bits = 1; bits <= 5; ++bits) {
        double best = 0, bs = 0;
        for (double st = 0.02; st < 4; st += 0.005) { double e = exp_quant_eff(bits, st); if (e > best) { best = e; bs = st; } }
        double cp; exp_quant_eff(bits, bs, &cp);
        printf("| %d | %.3f | %.2g | %.3f |", bits, bs, cp, -db(best));
        for (double gdb : {-5.0, 0.0, 5.0, 10.0, 15.0}) { double g = undb(gdb); printf(" %.2f |", db((g - std::log1p(g)) / exp_quant_gmi(bits, bs, g))); }
        printf("\n");
    }
    printf("\nFour bits with a fixed step of 0.5 (levels 0 to 7.5 times the noise mean, top level = clip):\n\n");
    printf("| step | clip | low-SNR loss | -5 dB | 0 dB | +5 dB | +10 dB | +15 dB |\n|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (double st : {0.25, 0.5, 0.75, 1.0}) {
        double cp, e = exp_quant_eff(4, st, &cp);
        printf("| %.2f | %.2g | %.3f |", st, cp, -db(e));
        for (double gdb : {-5.0, 0.0, 5.0, 10.0, 15.0}) { double g = undb(gdb); printf(" %.2f |", db((g - std::log1p(g)) / exp_quant_gmi(4, st, g))); }
        printf("\n");
    }
    printf("\n");
}

// ---------- Section 4 ----------
// complex Cholesky (lower) of a Hermitian PSD matrix with a tiny ridge
static std::vector<cd> cholesky(std::vector<cd> a, int n) {
    for (int i = 0; i < n; ++i) a[i * n + i] += 1e-12;
    std::vector<cd> l(n * n, 0.0);
    for (int j = 0; j < n; ++j) {
        double d = a[j * n + j].real();
        for (int k = 0; k < j; ++k) d -= std::norm(l[j * n + k]);
        d = std::sqrt(std::max(d, 1e-300));
        l[j * n + j] = d;
        for (int i = j + 1; i < n; ++i) {
            cd v = a[i * n + j];
            for (int k = 0; k < j; ++k) v -= l[i * n + k] * std::conj(l[j * n + k]);
            l[i * n + j] = v / d;
        }
    }
    return l;
}
// covariance of the signal part of the fine bins d = -Dn..Dn of an N-sample rectangular window
static std::vector<cd> bin_cov(const Chan& ch, double P, int N, int Dn) {
    int nb = 2 * Dn + 1;
    std::vector<cd> R(nb * nb, 0.0);
    Chan gauss; for (auto& k : ch) if (k.kind == Comp::GAUSS) gauss.push_back(k);
    if (gauss.empty()) return R;
    std::vector<cd> E(N); for (int t = 0; t < N; ++t) E[t] = std::polar(1.0, -2 * PI * t / double(N));
    auto ex = [&](long long t) { return E[((t % N) + N) % N]; };
    std::vector<cd> inv(nb, 0.0);  // 1/(1 - w_dd) for dd = 1..nb-1
    for (int dd = 1; dd < nb; ++dd) inv[dd] = 1.0 / (1.0 - ex(dd));
    int nth = omp_get_max_threads();
    std::vector<std::vector<cd>> part(nth, std::vector<cd>(nb * nb, 0.0));
    #pragma omp parallel for schedule(dynamic, 64)
    for (int u = -(N - 1); u <= N - 1; ++u) {
        cd r = rho(gauss, u / FS);
        if (std::abs(r) < 1e-14) continue;
        auto& Rt = part[omp_get_thread_num()];
        int m0 = std::max(0, -u), m1 = std::min(N - 1, N - 1 - u), cnt = m1 - m0 + 1;
        for (int a = 0; a < nb; ++a) {
            cd pa = r * ex((long long)(a - Dn) * u);
            for (int b = 0; b <= a; ++b) {
                int dd = a - b; cd inner;
                if (dd == 0) inner = cnt;
                else inner = (ex((long long)dd * m0) - ex((long long)dd * (m0 + cnt))) * inv[dd];
                Rt[a * nb + b] += pa * inner;
            }
        }
    }
    for (auto& Rt : part) for (int i = 0; i < nb * nb; ++i) R[i] += Rt[i];
    for (int a = 0; a < nb; ++a) for (int b = 0; b <= a; ++b) { R[a * nb + b] *= P / (FS * N); if (b < a) R[b * nb + a] = std::conj(R[a * nb + b]); }
    return R;
}
struct McOut { double gmi, se; };
// One symbol = L dwells; every dwell draws a fresh keyed placement of the M candidate tones on a
// grid of step k tone positions (352 positions of 6.25 Hz, 704 fine bins of 3.125 Hz, cyclic).
static McOut mc_keyed(const Chan& ch, double P, int M, int k, int L, bool wp, int trials, uint64_t seed, int nc = 2) {
    const int N = nc * CELL, NPOS = 352, NF = nc * NPOS;
    const int Dn = std::min(NF / 2 - 1, (int)std::ceil((extent(ch) * 0.8 + 3.0 * FS / N) * N / FS)), nb = 2 * Dn + 1;
    auto R = bin_cov(ch, P, N, Dn);
    auto Lc = cholesky(R, nb);
    std::vector<cd> linev(nb, 0.0); double linew = 0;
    for (auto& c : ch) if (c.kind == Comp::LINE) {
        linew += c.w;
        for (int a = 0; a < nb; ++a) { cd acc = 0; for (int n = 0; n < N; ++n) acc += std::polar(1.0, 2 * PI * (c.c / FS - (a - Dn) / double(N)) * n); linev[a] += std::sqrt(c.w * P / (FS * N)) * acc; }
    }
    std::vector<double> q(nb, 0.0);
    if (wp) { for (int a = 0; a < nb; ++a) { double lam = R[a * nb + a].real() + std::norm(linev[a]); q[a] = lam / (1 + lam); } double qm = *std::max_element(q.begin(), q.end()); for (auto& v : q) v = v < 1e-3 * qm ? 0 : v; }
    else q[Dn] = 1;
    const int grid = NPOS / k;
    const int NS = 24; std::vector<double> sg(NS); for (int i = 0; i < NS; ++i) sg[i] = 0.05 * std::pow(1.25, i);
    int nth = omp_get_max_threads();
    std::vector<std::vector<double>> a1(nth, std::vector<double>(NS, 0.0)), a2(nth, std::vector<double>(NS, 0.0));
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        Rng rng(seed * 1000003ull + tid * 7919ull + 29);
        std::vector<cd> Y(NF), u(nb); std::vector<double> E(NF), z(M); std::vector<int> perm(grid);
        #pragma omp for schedule(dynamic, 16)
        for (int t = 0; t < trials; ++t) {
            std::fill(z.begin(), z.end(), 0.0);
            for (int l = 0; l < L; ++l) {
                for (int i = 0; i < NF; ++i) Y[i] = rng.cn();
                for (int i = 0; i < grid; ++i) perm[i] = i;
                for (int i = 0; i < M; ++i) { int j = i + (int)(rng.next() % (uint64_t)(grid - i)); std::swap(perm[i], perm[j]); }
                int off = (int)(rng.next() % (uint64_t)k);
                int tx = nc * (off + k * perm[0]);
                for (int a = 0; a < nb; ++a) u[a] = rng.cn();
                cd ph = std::polar(1.0, 2 * PI * rng.uni());
                for (int a = 0; a < nb; ++a) { cd s = ph * linev[a]; for (int b = 0; b <= a; ++b) s += Lc[a * nb + b] * u[b]; Y[((tx + a - Dn) % NF + NF) % NF] += s; }
                for (int i = 0; i < NF; ++i) E[i] = std::norm(Y[i]);
                for (int m = 0; m < M; ++m) {
                    int c = nc * (off + k * perm[m]); double s = 0;
                    for (int a = 0; a < nb; ++a) if (q[a] > 0) s += q[a] * E[((c + a - Dn) % NF + NF) % NF];
                    z[m] += s;
                }
            }
            for (int si = 0; si < NS; ++si) {
                double mx = -1e300; for (int m = 0; m < M; ++m) mx = std::max(mx, sg[si] * (z[m] - z[0]));
                double sum = 0; for (int m = 0; m < M; ++m) sum += std::exp(sg[si] * (z[m] - z[0]) - mx);
                double f = std::log2((double)M) - (mx + std::log(sum)) / LN2;
                a1[tid][si] += f; a2[tid][si] += f * f;
            }
        }
    }
    McOut o{0, 0};
    for (int si = 0; si < NS; ++si) {
        double s1 = 0, s2 = 0; for (int t = 0; t < nth; ++t) { s1 += a1[t][si]; s2 += a2[t][si]; }
        double mean = s1 / trials, var = s2 / trials - mean * mean;
        if (mean > o.gmi) { o.gmi = mean; o.se = std::sqrt(std::max(0.0, var) / trials); }
    }
    return o;
}
static void section4(int trials) {
    printf("## 4. Finite alphabet with a keyed tone permutation per dwell (0.4 s dwell)\n\n");
    printf("Monte Carlo, %d symbol trials per point. Each dwell places the M candidate tones on a fresh keyed random\n", trials);
    printf("subset of the 6.25 Hz grid with minimum spacing `k` bins; one symbol is L dwells whose evidence is summed.\n");
    printf("`one bin` uses the tone's 3.125 Hz bin over the 0.32 s active part; `WP` the spread-matched weights\n");
    printf("(leakage into other candidates included). Entries: GMI in bits per symbol, then the extra transmit time\n");
    printf("(dB) relative to the bound, guard time included; `cap` marks GMI above 90%% of log2 M.\n\n");
    struct Cfg { int M, k; };
    std::vector<Cfg> cfgs = {{64, 1}, {64, 5}, {256, 1}};
    std::vector<NamedChan> chans = {{"Gauss 0.1 Hz", G(0.1)}, {"Gauss 1 Hz", G(1)}, {"Gauss 10 Hz", G(10)}, {"Gauss 30 Hz", G(30)}};
    struct Pt { double cn0; int L; };
    std::vector<Pt> pts = {{14, 1}, {10, 1}, {6, 1}, {6, 4}, {2, 4}, {2, 16}, {-2, 16}, {-2, 64}};
    printf("| C/N0 (dB-Hz) | L | Channel | bound Eb/N0 |");
    for (auto& c : cfgs) printf(" M=%d k=%d one bin | M=%d k=%d WP |", c.M, c.k, c.M, c.k);
    printf("\n|---:|---:|---|---:|"); for (size_t i = 0; i < cfgs.size(); ++i) printf("---:|---:|"); printf("\n");
    for (auto& p : pts) for (auto& nc : chans) {
        double P = undb(p.cn0), Rb = bound_rate(nc.ch, P) / LN2;
        printf("| %+.0f | %d | %s | %.2f |", p.cn0, p.L, nc.name.c_str(), db(P / Rb));
        int tr = std::max(2000, trials / std::max(1, p.L / 4));
        for (auto& c : cfgs) for (int wp = 0; wp < 2; ++wp) {
            McOut o = mc_keyed(nc.ch, P, c.M, c.k, p.L, wp, tr, (uint64_t)(p.cn0 * 31 + p.L * 7 + c.M + c.k * 3 + wp));
            if (o.gmi <= 3 * o.se) { printf(" n/s |"); continue; }
            double rate = o.gmi / (p.L * 0.4);
            printf(" %.3f, %.2f%s |", o.gmi, db(Rb / rate), o.gmi > 0.9 * std::log2((double)c.M) ? " cap" : "");
        }
        printf("\n"); fflush(stdout);
    }
    printf("\n");
}

static void section5(int trials) {
    printf("## 5. Finite alphabet with a keyed tone permutation per dwell (4 s dwell, 24 cells)\n\n");
    printf("As section 4 with the 3.84 s active part (fine bins of 0.26 Hz, 24 per tone position). `WP` only.\n\n");
    struct Cfg { int M, k; };
    std::vector<Cfg> cfgs = {{64, 1}, {64, 5}, {256, 1}};
    std::vector<NamedChan> chans = {{"Gauss 0.1 Hz", G(0.1)}, {"Gauss 1 Hz", G(1)}, {"Gauss 10 Hz", G(10)}, {"Gauss 30 Hz", G(30)}};
    struct Pt { double cn0; int L; };
    std::vector<Pt> pts = {{10, 1}, {4, 1}, {0, 1}, {0, 4}, {-6, 4}, {-6, 16}};
    printf("| C/N0 (dB-Hz) | L | Channel | bound Eb/N0 |");
    for (auto& c : cfgs) printf(" M=%d k=%d WP |", c.M, c.k);
    printf("\n|---:|---:|---|---:|"); for (size_t i = 0; i < cfgs.size(); ++i) printf("---:|"); printf("\n");
    for (auto& p : pts) for (auto& nc : chans) {
        double P = undb(p.cn0), Rb = bound_rate(nc.ch, P) / LN2;
        printf("| %+.0f | %d | %s | %.2f |", p.cn0, p.L, nc.name.c_str(), db(P / Rb));
        int tr = std::max(1500, trials / (4 * std::max(1, p.L / 2)));
        for (auto& c : cfgs) {
            McOut o = mc_keyed(nc.ch, P, c.M, c.k, p.L, true, tr, (uint64_t)(900 + p.cn0 * 31 + p.L * 7 + c.M + c.k * 3), 24);
            if (o.gmi <= 3 * o.se) { printf(" n/s |"); continue; }
            double rate = o.gmi / (p.L * 4.0);
            printf(" %.3f, %.2f%s |", o.gmi, db(Rb / rate), o.gmi > 0.9 * std::log2((double)c.M) ? " cap" : "");
        }
        printf("\n"); fflush(stdout);
    }
    printf("\n");
}

// ---------- Section 6 ----------
static Chan shifted(Chan ch, double c) { for (auto& k : ch) k.c += c; return ch; }
static Chan smeared(const Chan& ch, double half) {  // uniform smear of the centre over +-half Hz
    Chan out; const int n = 21;
    for (int i = 0; i < n; ++i) { double c = -half + 2 * half * i / (n - 1); for (auto k : ch) { k.c += c; k.w /= n; out.push_back(k); } }
    return out;
}
static void section6() {
    printf("## 6. Unknown Doppler shift of the path\n\n");
    printf("Sections 2 to 5 assume the Doppler centre is known. Here the true centre lies anywhere within +-1 Hz\n");
    printf("(mid-latitude shifts are of that order and wander over tens of minutes). `known` repeats section 2.\n");
    printf("`wide` uses one fixed weighting matched to the channel smeared over +-1 Hz (no tracking). `bank` tests\n");
    printf("centre hypotheses one fine bin apart and uses the nearest, which needs the shift to hold still over the\n");
    printf("evidence that selects it; for the 0.4 s dwell one fine bin is wider than the range, so `bank` is centred weights. Large alphabet, C/N0 = -10 dB-Hz; extra transmit time (dB) relative to the bound.\n\n");
    printf("| Channel | Dwell | fine bin (Hz) | known | wide | bank |\n|---|---|---:|---:|---:|---:|\n");
    std::vector<NamedChan> chans = {{"Gaussian 0.1 Hz", G(0.1)}, {"Gaussian 1 Hz", G(1)}, {"Gaussian 10 Hz", G(10)}};
    std::vector<Dwell> dw = {{"A 0.4 s", 0.4, 2}, {"B 4 s", 4.0, 24}, {"C 40 s", 40.0, 249}};
    double P = undb(-10);
    for (auto& nc : chans) for (auto& d : dw) {
        double T = d.nc * 0.16, Rb = bound_rate(nc.ch, P);
        Chan wide = smeared(nc.ch, 1.0);
        int K = span_bins(wide, T);
        auto lam0 = bin_energy(nc.ch, P, T, K), lamw = bin_energy(wide, P, T, K);
        double g_known = gmi_wp(lam0, lam0), g_wide = 0, g_bank = 0; int nw = 0, nbk = 0;
        for (double c = -0.9; c <= 0.91; c += 0.2) { g_wide += gmi_wp(bin_energy(shifted(nc.ch, c), P, T, K), lamw); ++nw; }
        // residual offset after the bank: uniform within half a fine bin, and never beyond the +-1 Hz range itself
        for (double r = -0.4; r <= 0.41; r += 0.2) { double off = std::max(-0.9, std::min(0.9, r / T)); g_bank += gmi_wp(bin_energy(shifted(nc.ch, off), P, T, K), lam0); ++nbk; }
        printf("| %s | %s | %.3f | %.2f | %.2f | %.2f |\n", nc.name.c_str(), d.name, 1 / T, db(Rb * d.period / g_known), db(Rb * d.period / (g_wide / nw)), db(Rb * d.period / (g_bank / nbk)));
        fflush(stdout);
    }
    printf("\n");
}

// ---------- Section 7 ----------
static double qinv(double p) { double lo = 0, hi = 40; for (int i = 0; i < 200; ++i) { double m = 0.5 * (lo + hi); if (0.5 * std::erfc(m / std::sqrt(2.0)) > p) lo = m; else hi = m; } return 0.5 * (lo + hi); }
static void human(double s) {
    if (s < 120) printf(" %.0f s |", s); else if (s < 7200) printf(" %.0f min |", s / 60); else if (s < 172800) printf(" %.1f h |", s / 3600); else if (s < 86400.0 * 60) printf(" %.1f d |", s / 86400); else printf(" %.1f yr |", s / (86400 * 365.25));
}
static void section7() {
    printf("## 7. Block duration estimate\n\n");
    printf("A block of k bits is accepted when its best codeword exceeds a presence threshold. With accumulated\n");
    printf("evidence that is close to Gaussian, the block needs a total deflection S = (z_fa + z_miss)^2, where\n");
    printf("z_fa = Qinv(P_fa / (A 2^k)) for A timing and frequency hypotheses and z_miss = Qinv(P_miss). The dwell\n");
    printf("count is S / d2 with d2 = 2 GMI (nat per dwell) of the spread-matched receiver of section 2, and never\n");
    printf("fewer than 32 dwells (the diversity a short block needs in fading). P_fa = 1e-6 per block, P_miss = 1e-3.\n");
    printf("Known Doppler centre. Estimate only: no coded simulation, no interference, no margin, no outages.\n\n");
    printf("| k | A | z_fa | S |\n|---:|---:|---:|---:|\n");
    for (int k : {10, 16, 24}) for (double A : {1.0, 400.0}) { double z = qinv(1e-6 / (A * std::pow(2.0, k))); printf("| %d | %.0f | %.2f | %.0f |\n", k, A, z, (z + qinv(1e-3)) * (z + qinv(1e-3))); }
    printf("\nDuration of one block with A = 400 hypotheses; `*` marks more than 0.5 of deflection per dwell, where\n");
    printf("the Gaussian estimate is rough and the 32-dwell floor usually decides.\n\n");
    std::vector<NamedChan> chans = {{"Gaussian 0.1 Hz", G(0.1)}, {"Gaussian 1 Hz", G(1)}, {"Gaussian 10 Hz", G(10)}};
    std::vector<Dwell> dw = {{"A", 0.4, 2}, {"B", 4.0, 24}, {"C", 40.0, 249}};
    printf("| C/N0 (dB-Hz) | Channel |");
    for (auto& d : dw) printf(" %s: d2 per dwell | %s: 10 bits | %s: 24 bits |", d.name, d.name, d.name);
    printf("\n|---:|---|"); for (int i = 0; i < 9; ++i) printf("---:|"); printf("\n");
    double z10 = qinv(1e-6 / (400 * 1024.0)) + qinv(1e-3), z24 = qinv(1e-6 / (400 * 16777216.0)) + qinv(1e-3);
    for (double c : {10.0, 0.0, -10.0, -20.0}) for (auto& nc : chans) {
        double P = undb(c);
        printf("| %+.0f | %s |", c, nc.name.c_str());
        for (auto& d : dw) {
            double T = d.nc * 0.16; int K = span_bins(nc.ch, T);
            auto lam = bin_energy(nc.ch, P, T, K);
            double d2 = 2 * gmi_wp(lam, lam);
            printf(" %.3g%s |", d2, d2 > 0.5 ? " *" : "");
            for (double z : {z10, z24}) human(std::max(32.0, std::ceil(z * z / d2)) * d.period);
        }
        printf("\n");
    }
    printf("\n");
}

// ---------- Section 8 ----------
static void section8() {
    printf("## 8. Reduced transforms for the 0.4 s dwell\n\n");
    printf("What a cheaper transform costs in sensitivity. `all bins` weights every 3.125 Hz fine bin (section 2).\n");
    printf("`tone grid` computes only the 6.25 Hz tone-position bins and weights those (half the bins: the case in\n");
    printf("which a matrix-product DFT is faster than an FFT). `one bin` uses the tone's own bin alone. Large\n");
    printf("alphabet, exact. First the gap to the bound (dB of time), then the loss against `all bins` as\n");
    printf("received power (half the time difference at weak signals, the full difference is printed for +10 dB-Hz).\n\n");
    std::vector<double> sp = {0.1, 1, 3, 10, 30, 60};
    for (double c : {-10.0, 0.0, 10.0}) {
        double P = undb(c), T = 0.32;
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Receiver |", c);
        for (double a : sp) printf(" %g Hz |", a);
        printf("\n|---|"); for (size_t i = 0; i < sp.size(); ++i) printf("---:|"); printf("\n");
        std::vector<double> g_all, g_grid, g_one;
        for (double a : sp) {
            Chan ch = G(a); int K = span_bins(ch, T); auto lam = bin_energy(ch, P, T, K);
            std::vector<double> lg; for (int k = -K; k <= K; ++k) if (k % 2 == 0) lg.push_back(lam[k + K]);
            double Rb = bound_rate(ch, P);
            g_all.push_back(db(Rb * 0.4 / gmi_wp(lam, lam)));
            g_grid.push_back(db(Rb * 0.4 / gmi_wp(lg, lg)));
            g_one.push_back(db(Rb * 0.4 / (lam[K] - std::log1p(lam[K]))));
        }
        printf("| all bins |"); for (double v : g_all) printf(" %.2f |", v); printf("\n");
        printf("| tone grid |"); for (double v : g_grid) printf(" %.2f |", v); printf("\n");
        printf("| one bin |"); for (double v : g_one) printf(" %.2f |", v); printf("\n");
        double f = c < 5 ? 0.5 : 1.0;
        printf("| tone grid, power loss |"); for (size_t i = 0; i < sp.size(); ++i) printf(" %.2f |", f * (g_grid[i] - g_all[i])); printf("\n");
        printf("| one bin, power loss |"); for (size_t i = 0; i < sp.size(); ++i) printf(" %.2f |", f * (g_one[i] - g_all[i])); printf("\n\n");
    }
    printf("Shedding dwells. If only a fraction f of the dwells is processed, evenly and by key, a block needs\n");
    printf("1/f times the dwells, which at weak signals is 5 log10(1/f) dB of received power:\n\n");
    printf("| dwells processed | 80%% | 63%% | 50%% | 40%% | 25%% | 10%% |\n|---|---:|---:|---:|---:|---:|---:|\n| throughput gain |");
    for (double f : {0.8, 0.63, 0.5, 0.4, 0.25, 0.1}) printf(" %.1fx |", 1 / f);
    printf("\n| power loss (dB) |"); for (double f : {0.8, 0.63, 0.5, 0.4, 0.25, 0.1}) printf(" %.2f |", 5 * std::log10(1 / f)); printf("\n\n");
}

// ---------- Section 9 ----------
static void section9(int trials) {
    printf("## 9. Is the 160 ms cell enough? Extreme spreads and other cell lengths\n\n");
    printf("### 9a. Very wide spreads with the 0.4 s dwell (large alphabet, exact)\n\n");
    printf("Gap to the bound (dB of time, guard included). `matched` uses the true spread; `bank to 60 Hz` uses the\n");
    printf("60 Hz weights (the widest hypothesis of the draft bank); `bank x2 to 500 Hz` the nearest of 120, 250, 500 Hz.\n\n");
    printf("| C/N0 (dB-Hz) | Receiver | 100 Hz | 200 Hz | 500 Hz |\n|---:|---|---:|---:|---:|\n");
    for (double c : {-10.0, 0.0, 10.0}) {
        double P = undb(c), T = 0.32; int K = span_bins(G(500), T);
        auto l60 = bin_energy(G(60), P, T, K);
        std::vector<double> ext = {120, 250, 500}, sp = {100, 200, 500};
        std::string r1 = "| ", r2 = "| ", r3 = "| "; char b[64];
        snprintf(b, sizeof b, "%+.0f | matched |", c); r1 += b; snprintf(b, sizeof b, "%+.0f | bank to 60 Hz |", c); r2 += b; snprintf(b, sizeof b, "%+.0f | bank x2 to 500 Hz |", c); r3 += b;
        for (size_t i = 0; i < sp.size(); ++i) {
            auto lt = bin_energy(G(sp[i]), P, T, K); double Rb = bound_rate(G(sp[i]), P);
            snprintf(b, sizeof b, " %.2f |", db(Rb * 0.4 / gmi_wp(lt, lt))); r1 += b;
            snprintf(b, sizeof b, " %.2f |", db(Rb * 0.4 / gmi_wp(lt, l60))); r2 += b;
            snprintf(b, sizeof b, " %.2f |", db(Rb * 0.4 / gmi_wp(lt, bin_energy(G(ext[i]), P, T, K)))); r3 += b;
        }
        printf("%s\n%s\n%s\n", r1.c_str(), r2.c_str(), r3.c_str());
    }
    printf("\n### 9b. Finite alphabet at very wide spreads (0.4 s dwell, keyed placement, matched weights)\n\n");
    printf("GMI in bits per symbol, then gap (dB of time). The spread covers 16 to 32 tone positions, so strong\n");
    printf("signals are limited by how many positions can be told apart, not by energy.\n\n");
    printf("| C/N0 (dB-Hz) | L | Channel | bound Eb/N0 | 256 tones, k=1 | 64 tones, k=5 |\n|---:|---:|---|---:|---:|---:|\n");
    struct Pt { double cn0; int L; };
    for (auto& p : std::vector<Pt>{{30, 1}, {20, 1}, {10, 1}, {10, 16}}) for (double w : {100.0, 200.0}) {
        Chan ch = G(w); double P = undb(p.cn0), Rb = bound_rate(ch, P) / LN2;
        printf("| %+.0f | %d | Gauss %g Hz | %.2f |", p.cn0, p.L, w, db(P / Rb));
        for (int cfg = 0; cfg < 2; ++cfg) {
            McOut o = mc_keyed(ch, P, cfg ? 64 : 256, cfg ? 5 : 1, p.L, true, std::max(2000, trials / (2 * p.L)), (uint64_t)(7000 + p.cn0 * 3 + w + cfg));
            if (o.gmi <= 3 * o.se) printf(" n/s |"); else printf(" %.3f, %.2f |", o.gmi, db(Rb / (o.gmi / (p.L * 0.4))));
        }
        printf("\n"); fflush(stdout);
    }
    printf("\n### 9c. Other cell lengths (large alphabet, exact, Doppler centre known)\n\n");
    printf("The same dwell shape scaled: two cells flat in a period of 2.5 cells, cell = 160 ms x 2^j, bin = 6.25 Hz / 2^j,\n");
    printf("spread-matched weights. Gap to the bound (dB of time). `line` is a phase-stable path.\n\n");
    std::vector<NamedChan> chans = {{"line", LINE()}, {"1 mHz", G(0.001)}, {"10 mHz", G(0.01)}, {"0.1 Hz", G(0.1)}, {"1 Hz", G(1)}, {"10 Hz", G(10)}, {"100 Hz", G(100)}};
    for (double c : {-30.0, -20.0, -10.0, 0.0, 10.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| cell | bin |", c);
        for (auto& nc : chans) printf(" %s |", nc.name.c_str());
        printf("\n|---:|---:|"); for (size_t i = 0; i < chans.size(); ++i) printf("---:|"); printf("\n");
        for (int j : {-2, 0, 2, 4, 6, 8, 10, 12}) {
            double cell = 0.16 * std::pow(2.0, j), T = 2 * cell, D = 2.5 * cell;
            if (cell < 1) printf("| %.0f ms |", cell * 1e3); else printf("| %.1f s |", cell);
            if (6.25 / std::pow(2.0, j) >= 0.1) printf(" %.2f Hz |", 6.25 / std::pow(2.0, j)); else printf(" %.1f mHz |", 6250 / std::pow(2.0, j));
            for (auto& nc : chans) {
                if ((extent(nc.ch) + 4 / T) * T > 3e5) { printf(" - |"); continue; }
                int K = span_bins(nc.ch, T); auto lam = bin_energy(nc.ch, P, T, K);
                printf(" %.1f |", db(bound_rate(nc.ch, P) * D / gmi_wp(lam, lam)));
            }
            printf("\n"); fflush(stdout);
        }
        printf("\n");
    }
}

// ---------- Section 10 helpers ----------
// P(Gamma(n,1) > x) for integer n: exp(-x) sum_{i<n} x^i / i!
static double gamma_upper(long n, double x) {
    double lx = std::log(x), s = 0;
    for (long i = 0; i < n; ++i) s += std::exp(i * lx - x - std::lgamma(i + 1.0));
    return s;
}
// threshold with P(sum of n unit exponentials > tau) = pfa (noise only)
static double chi_threshold(long n, double pfa) {
    if (n > 20000) return n + qinv(pfa) * std::sqrt((double)n);
    double lo = n, hi = n + 60 * std::sqrt((double)n) + 200;
    for (int i = 0; i < 80; ++i) { double m = 0.5 * (lo + hi); if (gamma_upper(n, m) > pfa) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}
// P(X < tau), X = sum over n windows of |z_w|^2, z_w ~ CN(mu_w, 1), sum |mu_w|^2 = Lam
static double chi_miss(long n, double Lam, double tau) {
    if (n > 20000) return 0.5 * std::erfc((n + Lam - tau) / std::sqrt(2.0 * (n + 2 * Lam)));
    long j0 = std::max(0L, (long)(Lam - 14 * std::sqrt(Lam + 1) - 20)), j1 = (long)(Lam + 14 * std::sqrt(Lam + 1) + 40);
    double lt = std::log(tau), q = gamma_upper(n + j0, tau), acc = 0;
    for (long j = j0; j <= j1; ++j) {
        double pj = std::exp(j * std::log(std::max(Lam, 1e-300)) - Lam - std::lgamma(j + 1.0));
        acc += pj * (1 - q);
        q += std::exp((n + j) * lt - tau - std::lgamma(n + j + 1.0));
    }
    return acc;
}
// total energy over noise density needed by a presence test on n windows combined as energies
static double energy_needed(long n, double pfa, double pmiss) {
    double tau = chi_threshold(n, pfa), lo = 0, hi = 50 + 40 * std::sqrt((double)n);
    while (chi_miss(n, hi, tau) > pmiss) hi *= 2;
    for (int i = 0; i < 70; ++i) { double m = 0.5 * (lo + hi); if (chi_miss(n, m, tau) > pmiss) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}
// message error rate of K orthogonal codewords, one phase unknown, total E/N0 = E (numerical integral)
static double orth_error(int K, double E) {
    double sum = 0, dx = 0.002;
    for (double x = dx / 2; x < E + 40 * std::sqrt(E + 1) + 80; x += dx) {
        double z = 2 * std::sqrt(x * E);
        double lf = -(x + E) + (z < 600 ? std::log(std::cyl_bessel_i(0.0, z)) : z - 0.5 * std::log(2 * PI * z));
        sum += std::exp(lf + (K - 1) * std::log1p(-std::exp(-x))) * dx;
    }
    return 1 - sum;
}
static double interp_db(const std::vector<double>& edb, const std::vector<double>& pe, double target) {
    for (size_t i = 0; i + 1 < edb.size(); ++i)
        if (pe[i] >= target && pe[i + 1] < target && pe[i + 1] > 0)
            return edb[i] + (edb[i + 1] - edb[i]) * (std::log(pe[i]) - std::log(target)) / (std::log(pe[i]) - std::log(pe[i + 1]));
    return std::nan("");
}

// ---------- Section 10 ----------
static void section10(int trials) {
    printf("## 10. FHSS off: contiguous dwells and coherent accumulation across dwells\n\n");
    printf("Without FHSS the carrier phase does not change between dwells and no quiet interval is needed. The dwell\n");
    printf("is then a whole number of cells, back to back, and every tone on the lattice has phase zero at every dwell\n");
    printf("boundary, so the transmitted phase is continuous and known.\n\n");
    printf("### 10a. Removing the guard (0.32 s dwells back to back against 0.4 s with guard), C/N0 = -10 dB-Hz\n\n");
    printf("| Channel | with guard | back to back |\n|---|---:|---:|\n");
    { double P = undb(-10), T = 0.32;
      for (double w : {0.1, 1.0, 10.0, 30.0}) { Chan ch = G(w); int K = span_bins(ch, T); auto lam = bin_energy(ch, P, T, K); double g = gmi_wp(lam, lam), Rb = bound_rate(ch, P);
          printf("| Gaussian %g Hz | %.2f | %.2f |\n", w, db(Rb * 0.4 / g), db(Rb * 0.32 / g)); } }
    printf("\n### 10b. Coherent accumulation over W dwells (large alphabet, exact)\n\n");
    printf("The receiver removes the known transmit phase and treats W consecutive 0.32 s dwells as one observation,\n");
    printf("with spread-matched weights. This needs the channel phase to be common to the tone positions used (a\n");
    printf("narrow total band on one propagation mode) and the frequency known to a fraction of 1/(W x 0.32 s).\n");
    printf("Gap to the bound (dB of time); W = 1 is the per-dwell receiver.\n\n");
    std::vector<NamedChan> chans = {{"line", LINE()}, {"1 mHz", G(0.001)}, {"10 mHz", G(0.01)}, {"0.1 Hz", G(0.1)}, {"1 Hz", G(1)}};
    std::vector<long> Ws = {1, 16, 256, 4096, 16384, 65536};
    for (double c : {-30.0, -20.0, -10.0, 0.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Channel |", c);
        for (long W : Ws) { double T = W * 0.32; if (T < 60) printf(" W=%ld (%.1f s) |", W, T); else if (T < 7200) printf(" W=%ld (%.0f min) |", W, T / 60); else printf(" W=%ld (%.1f h) |", W, T / 3600); }
        printf("\n|---|"); for (size_t i = 0; i < Ws.size(); ++i) printf("---:|"); printf("\n");
        for (auto& nc : chans) {
            printf("| %s |", nc.name.c_str());
            for (long W : Ws) { double T = W * 0.32; if ((extent(nc.ch) + 4 / T) * T > 3e5) { printf(" - |"); continue; }
                int K = span_bins(nc.ch, T); auto lam = bin_energy(nc.ch, P, T, K); printf(" %.1f |", db(bound_rate(nc.ch, P) * T / gmi_wp(lam, lam))); }
            printf("\n"); fflush(stdout);
        }
        printf("\n");
    }
    printf("### 10c. Coherent per tone position only (phase-stable path)\n\n");
    printf("If the channel phase differs between positions, the dwells at each of Np positions are summed coherently\n");
    printf("and the Np sums are combined as energies. Gap to the coherent bound (dB of time) over a window of 4096\n");
    printf("dwells (22 min) and 65536 dwells (5.8 h).\n\n| C/N0 (dB-Hz) | window | all positions together | Np = 2 | Np = 4 | Np = 16 |\n|---:|---|---:|---:|---:|---:|\n");
    for (double c : {-30.0, -20.0, -10.0}) for (long W : {4096L, 65536L}) {
        double P = undb(c), lam = P * W * 0.32;
        printf("| %+.0f | %s |", c, W == 4096 ? "22 min" : "5.8 h");
        for (int np : {1, 2, 4, 16}) printf(" %.1f |", db(lam / (np * (lam / np - std::log1p(lam / np)))));
        printf("\n");
    }
    printf("\n");
    // ---- 10d ----
    printf("### 10d. Few tone positions: the keyed block decoded coherently (Monte Carlo)\n\n");
    printf("The format of the specification with k = 10 bits: 32 symbols of 256 values, value = G_j c xor m_j, and 8\n");
    printf("dwells per symbol (256 dwells). Each dwell maps the 256 values onto M tone positions by a fresh keyed\n");
    printf("balanced map (256/M values per position; one-to-one for M = 256). Phase-stable path, one unknown phase for\n");
    printf("the block, Gaussian noise. The receiver sums complex bin values into one 256-entry table per symbol and\n");
    printf("takes the largest |sum over symbols|^2 over the 1024 messages. E/N0 is the energy of the whole block.\n");
    printf("Reference: 1024 exactly orthogonal codewords (numerical integral). Predicted loss: -10 log10(1 - 1/M).\n");
    printf("%d blocks per M; the same noise is reused across E/N0.\n\n", trials);
    {
        std::vector<double> edb; for (double e = 9.0; e <= 19.01; e += 0.5) edb.push_back(e);
        std::vector<double> pref; for (double e : edb) pref.push_back(orth_error(1024, undb(e)));
        double r2 = interp_db(edb, pref, 1e-2), r3 = interp_db(edb, pref, 1e-3);
        printf("| Positions M | E/N0 for 1%% block error (dB) | loss (dB) | E/N0 for 0.1%% (dB) | loss (dB) | predicted loss (dB) |\n|---:|---:|---:|---:|---:|---:|\n");
        printf("| orthogonal reference | %.2f | - | %.2f | - | - |\n", r2, r3);
        const int NS = 32, L = 8, KB = 10, NC = 1 << KB;
        for (int M : {256, 16, 8, 4, 2}) {
            std::vector<long> errs(edb.size(), 0);
            #pragma omp parallel
            {
                std::mt19937_64 rng(0x5eedULL * (M + 3) + 7919ULL * omp_get_thread_num());
                std::normal_distribution<double> nd(0.0, std::sqrt(0.5));
                std::vector<long> le(edb.size(), 0);
                std::vector<std::complex<double>> Tn(NS * 256), Zc(NC), noise(M);
                std::vector<int> cnt(NS * 256), Cc(NC), perm(256);
                std::vector<uint8_t> g(NS * KB), mask(NS), val(NS * NC);
                #pragma omp for schedule(dynamic, 16)
                for (int t = 0; t < trials; ++t) {
                    for (auto& x : g) x = (uint8_t)(rng() & 255);
                    for (auto& x : mask) x = (uint8_t)(rng() & 255);
                    for (int j = 0; j < NS; ++j) for (int c = 0; c < NC; ++c) { uint8_t v = mask[j]; for (int b = 0; b < KB; ++b) if (c >> b & 1) v ^= g[j * KB + b]; val[j * NC + c] = v; }
                    int c0 = (int)(rng() % NC);
                    std::fill(Tn.begin(), Tn.end(), std::complex<double>(0, 0)); std::fill(cnt.begin(), cnt.end(), 0);
                    for (int j = 0; j < NS; ++j) for (int d = 0; d < L; ++d) {
                        for (int i = 0; i < 256; ++i) perm[i] = i;
                        for (int i = 255; i > 0; --i) std::swap(perm[i], perm[rng() % (i + 1)]);
                        for (auto& x : noise) { double re = nd(rng), im = nd(rng); x = std::complex<double>(re, im); }
                        int p0 = perm[val[j * NC + c0]] % M;
                        for (int v = 0; v < 256; ++v) { int pp = perm[v] % M; Tn[j * 256 + v] += noise[pp]; cnt[j * 256 + v] += (pp == p0); }
                    }
                    for (int c = 0; c < NC; ++c) { std::complex<double> z(0, 0); int k2 = 0; for (int j = 0; j < NS; ++j) { int v = val[j * NC + c]; z += Tn[j * 256 + v]; k2 += cnt[j * 256 + v]; } Zc[c] = z; Cc[c] = k2; }
                    for (size_t e = 0; e < edb.size(); ++e) {
                        double a = std::sqrt(undb(edb[e]) / (NS * L));   // amplitude per dwell; the phase is absorbed in the noise
                        double best = -1; int bi = -1;
                        for (int c = 0; c < NC; ++c) { double m = std::norm(a * Cc[c] + Zc[c]); if (m > best) { best = m; bi = c; } }
                        if (bi != c0) ++le[e];
                    }
                }
                #pragma omp critical
                for (size_t e = 0; e < edb.size(); ++e) errs[e] += le[e];
            }
            std::vector<double> pe; for (auto x : errs) pe.push_back((double)x / trials);
            double e2 = interp_db(edb, pe, 1e-2), e3 = interp_db(edb, pe, 1e-3);
            printf("| %d | %.2f | %.2f | %.2f | %.2f | %.2f |\n", M, e2, e2 - r2, e3, e3 - r3, -db(1 - 1.0 / M)); fflush(stdout);
        }
        printf("\nThe 0.1%% column rests on about %d error events per point and is the less certain.\n\n", trials / 1000);
    }
    // ---- 10e ----
    printf("### 10e. Block duration estimate on a phase-stable path, FHSS off\n\n");
    printf("The block is accepted by a presence test on the best message: the sum over coherent windows of the\n");
    printf("squared magnitude of each window's sum. Threshold for a false-presence rate of 1e-6 over A 2^k tests with\n");
    printf("A = 4000 (ten times the hypotheses of section 7, for the window bank and its finer frequency grid); miss\n");
    printf("rate 1e-3. Exact central and noncentral chi-square tails up to 20,000 windows, Gaussian above. The energy\n");
    printf("is reduced by (1 - 1/M) for M = 16 tone positions (section 10d). Known frequency and timing, Gaussian\n");
    printf("noise, the path's phase steady over each window. Estimate only: no coded simulation, no margin.\n\n");
    printf("| k | windows per block | total E/N0 needed (dB) |\n|---:|---:|---:|\n");
    for (int k : {10, 24}) for (long n : {1L, 4L, 16L, 64L, 1024L}) printf("| %d | %ld | %.2f |\n", k, n, db(energy_needed(n, 1e-6 / (4000.0 * std::pow(2.0, k)), 1e-3)));
    printf("\nDuration of one block, 10 bits / 24 bits:\n\n");
    printf("| C/N0 (dB-Hz) | per dwell, 0.32 s | per dwell, 40 s | coherent 1 min | coherent 22 min | coherent 5.8 h | whole block coherent |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (double c : {-10.0, -20.0, -30.0, -37.0}) {
        double P = undb(c) * (1 - 1.0 / 16);
        printf("| %+.0f |", c);
        for (double Tw : {0.32, 40.0, 0.32 * 256, 0.32 * 4096, 0.32 * 65536, 0.0}) {
            std::string cell;
            for (int k : {10, 24}) {
                double pfa = 1e-6 / (4000.0 * std::pow(2.0, k)), dur;
                if (Tw == 0) dur = energy_needed(1, pfa, 1e-3) / P;
                else {
                    double lam = P * Tw; long lo = 0, hi = 1;
                    auto ok = [&](long n) { return chi_miss(n, n * lam, chi_threshold(n, pfa)) <= 1e-3; };
                    while (!ok(hi)) { lo = hi; hi *= 2; if (hi > (1L << 50)) break; }
                    while (hi - lo > 1) { long m = lo + (hi - lo) / 2; if (ok(m)) hi = m; else lo = m; }
                    dur = hi == 1 ? -1 : hi * Tw;
                }
                char b[48];
                if (dur < 0) snprintf(b, sizeof b, "-"); else if (dur < 120) snprintf(b, sizeof b, "%.0f s", dur); else if (dur < 7200) snprintf(b, sizeof b, "%.0f min", dur / 60); else if (dur < 172800) snprintf(b, sizeof b, "%.1f h", dur / 3600); else if (dur < 86400.0 * 730) snprintf(b, sizeof b, "%.1f d", dur / 86400); else snprintf(b, sizeof b, "%.0f yr", dur / (86400 * 365.25));
                cell += (k == 10 ? std::string(" ") : std::string(" / ")) + b;
            }
            printf("%s |", cell.c_str());
        }
        printf("\n"); fflush(stdout);
    }
    printf("\n`-`: one window already holds more than the block needs; see the last column.\n\n");
    // ---- 10f ----
    printf("### 10f. Hypothesis count and antenna bandwidth\n\n");
    printf("Deflection S = (z_fa + z_miss)^2 that a block needs against the number of hypotheses A (section 7 method).\n");
    printf("Block time is proportional to S in both the weak-signal and the coherent case.\n\n| k | A | S | time against A = 400 (dB) |\n|---:|---:|---:|---:|\n");
    for (int k : {10, 24}) { double s0 = 0; for (double A : {400.0, 4000.0, 40000.0, 400000.0}) { double z = qinv(1e-6 / (A * std::pow(2.0, k))) + qinv(1e-3); if (A == 400) s0 = z * z; printf("| %d | %.0f | %.1f | %.2f |\n", k, A, z * z, db(z * z / s0)); } }
    printf("\nA single-tuned transmit antenna of -3 dB bandwidth BW radiates 1 / (1 + (2 df / BW)^2) of its resonant\n");
    printf("power at an offset df, with a phase of atan(2 df / BW). Mean radiated power over M positions centred on\n");
    printf("resonance, the phase at the outer position, and the sum with the few-position loss measured in 10d\n");
    printf("(2.6, 0.8, 0.24 and 0.0 dB for 2, 4, 8 and 16 positions). Bandwidths are the four antenna cases of the\n");
    printf("earlier sub-9 kHz antenna note. Derived; no antenna was measured.\n\n");
    printf("| BW (Hz) | bin (Hz) | M | span (Hz) | mean radiated (dB) | outer phase (deg) | with few-position loss (dB) |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (double BW : {18.5, 57.1, 173.9, 458.3}) for (double bin : {6.25, 1.5625}) {
        if (bin < 6 && BW > 60) continue;
        int Ms[4] = {2, 4, 8, 16}; double fl[4] = {2.6, 0.8, 0.24, 0.0};
        for (int i = 0; i < 4; ++i) { int M = Ms[i]; double a = 0, edge = (M - 1) / 2.0 * bin;
            for (int q = 0; q < M; ++q) { double o = (q - (M - 1) / 2.0) * bin; a += 1 / (1 + 4 * o * o / (BW * BW)); }
            printf("| %.1f | %.4g | %d | %.4g | %.2f | %.0f | %.2f |\n", BW, bin, M, (M - 1) * bin, db(a / M), std::atan(2 * edge / BW) * 180 / PI, -db(a / M) + fl[i]); }
    }
    printf("\n");
}

// ---------- Section 11 ----------
static std::string hstr(double s) {
    char b[48];
    if (s < 10) snprintf(b, sizeof b, "%.1f s", s); else if (s < 120) snprintf(b, sizeof b, "%.0f s", s); else if (s < 7200) snprintf(b, sizeof b, "%.0f min", s / 60); else if (s < 172800) snprintf(b, sizeof b, "%.1f h", s / 3600); else if (s < 86400.0 * 730) snprintf(b, sizeof b, "%.1f d", s / 86400); else snprintf(b, sizeof b, "%.0f yr", s / (86400 * 365.25));
    return b;
}
static std::vector<double> boxcar(const std::vector<double>& lam, int h) {   // mean over integer bin shifts -h..h
    int n = (int)lam.size(); std::vector<double> pre(n + 1, 0), out(n);
    for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + lam[i];
    for (int k = 0; k < n; ++k) out[k] = (pre[std::min(n, k + h + 1)] - pre[std::max(0, k - h)]) / (2 * h + 1);
    return out;
}
// Log statistic of one dwell for a tone whose centre is one of K cells:
//   ln[ sum over cells of exp(a x) / (K (1 + lam)) ],  a = lam / (1 + lam),  x = |cell value|^2.
// The true cell (sig) holds a constant-amplitude tone of energy lam_true with a random phase. Noise cells
// are unit exponentials; above 32 of them the 32 largest are drawn exactly and the rest enter by their
// conditional mean and variance.
static double dwell_stat(Rng& r, double lam, double lam_true, double K, bool sig) {
    double a = lam / (1 + lam), ls = -1e300, sn = 0, rest = K - (sig ? 1 : 0);
    if (sig) ls = a * std::norm(std::sqrt(lam_true) + r.cn());
    if (rest >= 1 && rest <= 32) { for (int i = 0; i < (int)rest; ++i) sn += std::exp(-a * std::log(r.uni())); }
    else if (rest > 32) {
        const int m = 32; double g = 0, um = 0;
        for (int i = 0; i < m; ++i) { g -= std::log(r.uni()); um = g / (rest + 1); sn += std::exp(-a * std::log(um)); }
        if (um < 1) {
            double nr = rest - m, mu = (1 - std::pow(um, 1 - a)) / ((1 - a) * (1 - um));
            double m2 = std::fabs(1 - 2 * a) < 1e-9 ? -std::log(um) / (1 - um) : (1 - std::pow(um, 1 - 2 * a)) / ((1 - 2 * a) * (1 - um));
            double v = std::max(0.0, m2 - mu * mu), u1 = r.uni(), u2 = r.uni();
            sn += std::max(0.0, nr * mu + std::sqrt(nr * v) * std::sqrt(-2 * std::log(u1)) * std::cos(2 * PI * u2));
        }
    }
    double top = std::max(ls, sn > 0 ? std::log(sn) : -1e300);
    double v = top + std::log((ls > -1e299 ? std::exp(ls - top) : 0.0) + (sn > 0 ? std::exp(std::log(sn) - top) : 0.0));
    return v - std::log(K * (1 + lam));
}
// P(sum over n dwells of the statistic < tau) with the signal present
static double block_miss(int n, double lam, double lam_true, double K, double tau, int trials, uint64_t seed) {
    long miss = 0;
    #pragma omp parallel reduction(+ : miss)
    {
        Rng r(seed + 977 * (uint64_t)omp_get_thread_num());
        #pragma omp for schedule(static)
        for (int t = 0; t < trials; ++t) { double T = 0; for (int d = 0; d < n; ++d) T += dwell_stat(r, lam, lam_true, K, true); if (T < tau) ++miss; }
    }
    return (double)miss / trials;
}
// total E/N0 (dB) at which a block of n dwells, each with its own phase and one of K centres, is missed with
// probability pm; the threshold tau uses P_fa <= exp(-tau), which holds for any likelihood ratio
static double block_energy_db(int n, double K, double tau, double pm, int trials, uint64_t seed) {
    double lo = 3, hi = 45;
    for (int i = 0; i < 9; ++i) { double m = 0.5 * (lo + hi), lam = undb(m) / n; if (block_miss(n, lam, lam, K, tau, trials, seed) > pm) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}
// Does an observer whose dwells have energy lam, with the tone in one of N cells per dwell, detect a block of
// n dwells with probability 0.9 at a false-alarm rate of 1e-2? Per-dwell statistics are drawn into pools of
// 2^20 for noise only and for signal present; block sums are resampled from the pools, and the threshold is
// the empirical 99th percentile of the noise-only sums.
static bool observer_detects(int n, double lam, double N, uint64_t seed) {
    const int PN = 1 << 20, NB = 20000;
    std::vector<float> h0(PN), h1(PN);
    std::vector<double> s0(NB), s1(NB);
    #pragma omp parallel
    {
        Rng r(seed + 7919 * (uint64_t)omp_get_thread_num());
        #pragma omp for schedule(static)
        for (int i = 0; i < PN; ++i) { h0[i] = (float)dwell_stat(r, lam, lam, N, false); h1[i] = (float)dwell_stat(r, lam, lam, N, true); }
        #pragma omp for schedule(static)
        for (int b = 0; b < NB; ++b) { double a0 = 0, a1 = 0; for (int d = 0; d < n; ++d) { a0 += h0[r.next() >> 44]; a1 += h1[r.next() >> 44]; } s0[b] = a0; s1[b] = a1; }
    }
    std::nth_element(s0.begin(), s0.begin() + (int)(0.99 * NB), s0.end());
    double thr = s0[(int)(0.99 * NB)]; long hit = 0;
    for (double v : s1) if (v > thr) ++hit;
    return hit >= 0.9 * NB;
}
static void section11(int trials) {
    printf("## 11. FHSS dwell length when every dwell has a new phase and frequency offset\n\n");
    printf("With FHSS the receiver can be coherent only inside a dwell. Phase is taken as independent from dwell to\n");
    printf("dwell throughout. A dwell period D has D/0.16 - 1 flat cells (2 cells for 0.4 s) between its edges.\n\n");
    std::vector<Dwell> dw = {{"0.4 s", 0.4, 2}, {"4 s", 4.0, 24}, {"40 s", 40.0, 249}, {"400 s", 400.0, 2499}, {"4000 s", 4000.0, 24999}};
    // ---- 11a ----
    printf("### 11a. Steady path, centre known: energy per dwell and gap to the coherent bound\n\n");
    printf("Large alphabet, one bin, extra transmit time in dB. `E` is the energy of one dwell over the noise density.\n\n| C/N0 (dB-Hz) |");
    for (auto& d : dw) printf(" %s: E | gap (dB) |", d.name);
    printf("\n|---:|"); for (size_t i = 0; i < dw.size(); ++i) printf("---:|---:|"); printf("\n");
    for (double c : {10.0, 0.0, -10.0, -20.0, -30.0}) {
        printf("| %+.0f |", c);
        for (auto& d : dw) { double lam = undb(c) * d.nc * 0.16; printf(" %.3g | %.1f |", lam, db(undb(c) * d.period / (lam - std::log1p(lam)))); }
        printf("\n");
    }
    // ---- 11b ----
    printf("\n### 11b. Fading paths: block duration against dwell period, with and without a random centre per dwell\n\n");
    printf("Section 7 method: 10-bit block, A = 400, deflection S = 100, dwells = S / (2 GMI), never fewer than 32.\n");
    printf("Each cell gives three durations: centre known / centre uniform within +-0.1 Hz / within +-1 Hz, drawn\n");
    printf("afresh for every dwell. For a random centre the receiver is the better of two: (i) one fixed weighting\n");
    printf("matched to the smeared spectrum (exact GMI); (ii) a search over centres inside each dwell, credited with\n");
    printf("the centred GMI at the residual offset minus ln(number of centres), a lower bound from\n");
    printf("I(X;Y) >= I(X;Y|centre) - H(centre). `f` marks a block held at the 32-dwell floor.\n\n| C/N0 (dB-Hz) | Channel |");
    for (auto& d : dw) printf(" %s |", d.name);
    printf("\n|---:|---|"); for (size_t i = 0; i < dw.size(); ++i) printf("---|"); printf("\n");
    {
        struct Fc { const char* name; double two_sigma; };
        std::vector<Fc> chans = {{"Gaussian 10 mHz", 0.01}, {"Gaussian 0.1 Hz", 0.1}, {"Gaussian 1 Hz", 1.0}, {"Gaussian 10 Hz", 10.0}};
        double z10 = qinv(1e-6 / (400 * 1024.0)) + qinv(1e-3), S = z10 * z10;
        for (double c : {0.0, -10.0, -20.0}) for (auto& fc : chans) {
            double P = undb(c); Chan ch = G(fc.two_sigma);
            printf("| %+.0f | %s |", c, fc.name);
            for (auto& d : dw) {
                double T = d.nc * 0.16; std::string cell;
                for (double D : {0.0, 0.1, 1.0}) {
                    Chan wide = D > 0 ? smeared(ch, D) : ch; int K = span_bins(wide, T);
                    if ((extent(wide) + 4 / T) * T > 3e5) { cell += (D > 0 ? " / -" : "-"); continue; }
                    auto lam0 = bin_energy(ch, P, T, K);
                    double g;
                    if (D == 0) g = gmi_wp(lam0, lam0);
                    else {
                        auto lamw = D * T >= 2 ? boxcar(lam0, (int)std::lround(D * T)) : bin_energy(wide, P, T, K);
                        double g_smear = gmi_wp(lamw, lamw);
                        double step = std::max(1 / T, fc.two_sigma / 4), hres = step / 2;
                        auto lamr = hres * T >= 2 ? boxcar(lam0, (int)std::lround(hres * T)) : bin_energy(smeared(ch, hres), P, T, K);
                        double g_res = gmi_wp(lamr, lam0) - std::log(1 + std::ceil(2 * D / step));
                        g = std::max(g_smear, g_res);
                    }
                    double n = std::max(32.0, std::ceil(S / (2 * g)));
                    cell += std::string(D > 0 ? " / " : "") + hstr(n * d.period) + (n <= 32 ? " f" : "");
                }
                printf(" %s |", cell.c_str()); fflush(stdout);
            }
            printf("\n");
        }
    }
    // ---- 11c ----
    const double tau = std::log(4000.0 * 1024.0 / 1e-6);
    printf("\n### 11c. Steady path: energy of a 10-bit block of n dwells, each with its own phase and centre\n\n");
    printf("Constant-amplitude tone, a new phase in every dwell, and in every dwell a centre that is one of K cells\n");
    printf("(K = 1: centre known). Per-dwell statistic: ln of the mean over the K cells of exp(a x)/(1 + E_d), with\n");
    printf("a = E_d/(1 + E_d); the block sums it. Threshold tau = ln(A 2^k / 1e-6) = %.1f with A = 4000, which bounds\n", tau);
    printf("the false-presence rate for any likelihood ratio; miss rate 1e-3 by Monte Carlo (%d blocks per point).\n", trials);
    printf("`exact` is the chi-square calculation of section 10e for K = 1, as a check of the method.\n\n");
    printf("| dwells n | exact, K = 1 (dB) | K = 1 (dB) | K = 10 (dB) | K = 100 (dB) | K = 1000 (dB) | E per dwell, K = 1 / K = 1000 |\n|---:|---:|---:|---:|---:|---:|---|\n");
    std::vector<int> ns = {4, 8, 16, 32, 64, 128};
    std::vector<std::vector<double>> E(ns.size(), std::vector<double>(4));
    for (size_t i = 0; i < ns.size(); ++i) {
        int n = ns[i]; double ex = db(energy_needed(n, std::exp(-tau), 1e-3));
        printf("| %d | %.2f |", n, ex);
        int kk = 0; for (double K : {1.0, 10.0, 100.0, 1000.0}) { E[i][kk] = block_energy_db(n, K, tau, 1e-3, trials, 0xB10CULL + n); printf(" %.2f |", E[i][kk]); ++kk; fflush(stdout); }
        printf(" %.1f / %.1f |\n", undb(E[i][0]) / n, undb(E[i][3]) / n);
    }
    printf("\nFor comparison, a block that is coherent throughout (no FHSS) needs %.2f dB.\n\n", db(energy_needed(1, std::exp(-tau), 1e-3)));
    printf("The K = 1 column is 0.3 to 0.4 dB above `exact` because the threshold bound is not tight; the K > 1\n");
    printf("columns carry the same excess.\n\n");
    printf("Dwell (flat part) and block time that follow, for blocks of 32 and of 8 dwells; K = 1 / K = 100 / K = 1000:\n\n");
    printf("| C/N0 (dB-Hz) | 32 dwells: dwell | block | 8 dwells: dwell | block |\n|---:|---|---|---|---|\n");
    for (double c : {10.0, 0.0, -10.0, -20.0, -30.0}) {
        printf("| %+.0f |", c);
        for (int row : {3, 1}) {
            std::string a, b; int n = ns[row];
            for (int kk : {0, 2, 3}) { double Td = undb(E[row][kk]) / n / undb(c); a += (kk ? " / " : "") + hstr(Td); b += (kk ? " / " : "") + hstr(n * Td); }
            printf(" %s | %s |", a.c_str(), b.c_str());
        }
        printf("\n");
    }
    printf("\nBlock time on the fixed dwell ladder, steady path, centre known, exact chi-square, 10 bits, A = 4000,\n");
    printf("never fewer than 32 dwells (`f` marks the floor):\n\n| C/N0 (dB-Hz) |");
    for (auto& d : dw) printf(" %s |", d.name);
    printf(" no FHSS, coherent block |\n|---:|"); for (size_t i = 0; i <= dw.size(); ++i) printf("---|"); printf("\n");
    for (double c : {10.0, 0.0, -10.0, -20.0, -30.0}) {
        double P = undb(c), pfa = std::exp(-tau);
        printf("| %+.0f |", c);
        for (auto& d : dw) {
            double lam = P * d.nc * 0.16; long lo = 31, hi = 32;
            auto ok = [&](long n) { return chi_miss(n, n * lam, chi_threshold(n, pfa)) <= 1e-3; };
            while (!ok(hi)) { lo = hi; hi *= 2; if (hi > (1L << 50)) break; }
            while (hi - lo > 1) { long m = lo + (hi - lo) / 2; if (ok(m)) hi = m; else lo = m; }
            printf(" %s%s |", hstr(hi * d.period).c_str(), hi <= 32 ? " f" : "");
        }
        printf(" %s |\n", hstr(energy_needed(1, pfa, 1e-3) / P).c_str()); fflush(stdout);
    }
    // ---- 11d ----
    printf("\n### 11d. An observer without the key\n\n");
    printf("The link is the K = 1 block of 11c with n dwells (exact energies). The observer knows the lattice, the\n");
    printf("band and the dwell timing but not the key, so in each dwell the tone is in one of N cells (tone positions\n");
    printf("times hop channels). Its statistic is the same per-dwell log of the mean over N cells, summed over the\n");
    printf("block. `margin` is how much more C/N0 than the link the observer needs to detect the block with\n");
    printf("probability 0.9 at a false-alarm rate of 1e-2 (threshold from noise-only blocks; 20,000 resampled blocks\n");
    printf("per point). A wideband radiometer over W for the block time T with a perfectly known noise level needs\n");
    printf("C/N0 = z sqrt(W/T) with z = 3.61; its margin is 10 log10(z sqrt(W T) / E_block).\n\n");
    printf("| dwells n | block E/N0 (dB) | E per dwell | margin, N = 352 (dB) | N = 17,600 (dB) | N = 3,520,000 (dB) | radiometer, W T = 7.9e6 (dB) | W T = 3.6e9 (dB) |\n|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    {
        const double zo = qinv(1e-2) + qinv(0.1);
        for (int n : {32, 128, 512, 2048, 8192}) {
            double Eb = energy_needed(n, std::exp(-tau), 1e-3), lam = Eb / n;
            printf("| %d | %.2f | %.3g |", n, db(Eb), lam);
            for (double N : {352.0, 17600.0, 3.52e6}) {
                double glo = -6, ghi = 45;
                for (int i = 0; i < 10; ++i) { double m = 0.5 * (glo + ghi); if (!observer_detects(n, lam * undb(m), N, 0x0B5ULL + n + (uint64_t)N)) glo = m; else ghi = m; }
                printf(" %.1f |", 0.5 * (glo + ghi)); fflush(stdout);
            }
            printf(" %.1f | %.1f |\n", db(zo * std::sqrt(7.9e6) / Eb), db(zo * std::sqrt(3.6e9) / Eb));
        }
        double Sk = std::pow(qinv(std::exp(-tau)) + qinv(1e-3), 2);
        printf("| very many weak dwells | grows as sqrt(n) | -> 0 | %.1f | %.1f | %.1f | falls as 1/sqrt(n) | |\n", 0.5 * db(352 * zo * zo / Sk), 0.5 * db(17600 * zo * zo / Sk), 0.5 * db(3.52e6 * zo * zo / Sk));
    }
    printf("\nW T = 7.9e6 is 2.2 kHz for one hour; 3.6e9 is a 1 MHz hop span for one hour. The last row is the Gaussian\n");
    printf("limit 5 log10(N z^2 / S), reached only when n is much larger than N z^2.\n\n");
}

// ---------- Section 12 ----------
static void section12() {
    printf("## 12. A keyed phase in every cell of a dwell\n\n");
    printf("Each 160 ms cell of a dwell is transmitted with a keyed phase of 0, 90, 180 or 270 degrees. The receiver\n");
    printf("removes it by an exact quarter turn of each cell's values before it combines the cells of a dwell. The\n");
    printf("rotation has unit modulus, so with exact timing the tones stay orthogonal over whole cells, the noise\n");
    printf("statistics are unchanged, and every earlier figure for the keyed receiver holds.\n\n");
    const double Tc = 0.16;
    // ---- 12a ----
    printf("### 12a. What the keyed receiver pays: timing\n\n");
    printf("A receive window displaced by tau holds tau of the neighbouring cell, whose phase is unrelated, so the\n");
    printf("coherent amplitude is 1 - tau/160 ms. A plain tone is not free of this either: its window then holds tau\n");
    printf("of ramp, quiet or the next dwell, so its amplitude is 1 - tau/dwell (section 12e). An earlier version of\n");
    printf("this section said a plain tone loses nothing inside the guard and compared against a 40 ms grid; that\n");
    printf("holds only for the 0.4 s dwell.\n\n");
    printf("| timing error (ms) | 2.5 | 5 | 10 | 20 | 40 | 80 |\n|---|---:|---:|---:|---:|---:|---:|\n| loss (dB) |");
    for (double t : {2.5, 5.0, 10.0, 20.0, 40.0, 80.0}) printf(" %.2f |", -2 * db(1 - t * 1e-3 / Tc));
    printf("\n\n| timing grid step (ms) | 5 | 10 | 20 | 40 |\n|---|---:|---:|---:|---:|\n| worst loss (dB) |");
    for (double g : {5.0, 10.0, 20.0, 40.0}) printf(" %.2f |", -2 * db(1 - g * 0.5e-3 / Tc));
    printf("\n| mean loss (dB) |");
    for (double g : {5.0, 10.0, 20.0, 40.0}) { double x = g * 0.5e-3 / Tc; printf(" %.2f |", -db((1 - std::pow(1 - x, 3)) / (3 * x))); }
    printf("\n\nA propagation path delayed by d against the timing reference loses the same fraction of its own power:\n");
    printf("%.2f dB at 0.5 ms, %.2f dB at 2 ms, %.2f dB at 7 ms.\n\n", -2 * db(1 - 0.5e-3 / Tc), -2 * db(1 - 2e-3 / Tc), -2 * db(1 - 7e-3 / Tc));
    // ---- 12b ----
    const double tau = std::log(4000.0 * 1024.0 / 1e-6);
    const int n = 32; const double Eb = energy_needed(n, std::exp(-tau), 1e-3), lam = Eb / n, zo = qinv(1e-2) + qinv(0.1);
    printf("### 12b. What an observer without the key loses\n\n");
    printf("Link: the 32-dwell block of section 11 on a steady path (E/N0 = %.2f dB, %.2f per dwell). The observer\n", db(Eb), lam);
    printf("knows the lattice, the band and the cell and dwell timing. Criterion as in 11d: detect the block with\n");
    printf("probability 0.9 at a false-alarm rate of 1e-2. `margin` is the extra C/N0 it needs over the link.\n");
    printf("Observer statistics, all computed with exact chi-square tails unless marked:\n\n");
    printf("- `line`: plain tone. The largest dwell-long bin over all cells and dwells. Section 11d found the best\n");
    printf("  statistic 0.6 to 1.7 dB better than this largest-bin detector (1.6, 3.8 and 5.7 dB).\n");
    printf("- `blob`: keyed phase. Cell energies added over a dwell for each position (2m degrees of freedom for m\n");
    printf("  cells), then the largest over positions and dwells.\n");
    printf("- `energy`: keyed phase. All cell energies of the block added (Gaussian deflection).\n");
    printf("- `square`: the phase key is binary. Cell values are squared and added over the dwell, which restores a\n");
    printf("  line (Gaussian sum over cells, then the `line` requirement).\n");
    printf("- `fourth`: the phase key is quaternary. The same with fourth powers.\n\n");
    printf("`square` and `fourth` need many cells per dwell for their Gaussian sum and are not given for 2 cells.\n\n");
    for (double N : {352.0, 17600.0, 3.52e6}) {
        double pc = 0.01 / (N * n), pmiss1 = std::pow(0.1, 1.0 / n);
        auto need = [&](long m) { double t = chi_threshold(m, pc), lo = 0, hi = 50 + 40 * std::sqrt((double)m); while (chi_miss(m, hi, t) > pmiss1) hi *= 2; for (int i = 0; i < 70; ++i) { double mid = 0.5 * (lo + hi); if (chi_miss(m, mid, t) > pmiss1) lo = mid; else hi = mid; } return 0.5 * (lo + hi); };
        double l1 = need(1);
        printf("**N = %.0f cells per dwell.** Plain tone, `line`: %.1f dB at any dwell length.\n\n", N, db(l1 / lam));
        printf("| dwell | cells m | link C/N0 (dB-Hz) | blob (dB) | energy (dB) | square, binary key (dB) | fourth, quaternary key (dB) | observer needs, quaternary key (dB) |\n|---|---:|---:|---:|---:|---:|---:|---:|\n");
        for (long m : {2L, 24L, 249L, 2499L, 24999L}) {
            double gb = need(m) / lam, ge = zo * std::sqrt(N * n * m) / Eb;
            double gs = (2 * l1 + std::sqrt(4 * l1 * l1 + 2.0 * m * l1)) / lam, gf = std::pow(24.0 * m * m * m * l1, 0.25) / lam;
            const char* nm = m == 2 ? "0.4 s" : m == 24 ? "4 s" : m == 249 ? "40 s" : m == 2499 ? "400 s" : "4000 s";
            if (m < 24) printf("| %s | %ld | %+.1f | %.1f | %.1f | - | - | %.1f |\n", nm, m, db(lam / (m * Tc)), db(gb), db(ge), db(std::min(gb, ge)));
            else printf("| %s | %ld | %+.1f | %.1f | %.1f | %.1f | %.1f | %.1f |\n", nm, m, db(lam / (m * Tc)), db(gb), db(ge), db(gs), db(gf), db(std::min(gb, std::min(ge, gf))));
        }
        printf("\n"); fflush(stdout);
    }
    printf("The `energy` margin is that of an energy detector over the occupied band for the whole block, which no\n");
    printf("constant-envelope signal of that footprint can exceed. Allow about 2 dB for a statistic better than the\n");
    printf("largest-blob detector, as measured for the plain tone.\n\n");
    // ---- 12c ----
    printf("### 12c. If the receiver did not know the phase changes\n\n");
    printf("Then it is coherent only inside one cell and adds 32 m cells as energies. Block energy and time against\n");
    printf("the keyed or plain dwell (%.2f dB):\n\n| dwell | cells m | block E/N0 (dB) | more energy (dB) | block time |\n|---|---:|---:|---:|---:|\n", db(Eb));
    for (long m : {2L, 24L, 249L, 2499L}) { double e = energy_needed(n * m, std::exp(-tau), 1e-3); printf("| %s | %ld | %.2f | %.1f | %.1fx |\n", m == 2 ? "0.4 s" : m == 24 ? "4 s" : m == 249 ? "40 s" : "400 s", m, db(e), db(e / Eb), e / Eb); }
    // ---- 12d ----
    printf("\n### 12d. Few positions without FHSS: the line a plain tone leaves\n\n");
    printf("Without FHSS and without a phase key, every visit to a position continues the same carrier, so each of P\n");
    printf("positions holds a steady spectral line of 1/P^2 of the power. An observer that integrates one line\n");
    printf("coherently for the block time, with no knowledge of key or timing, needs 20 log10(P) dB more C/N0 than a\n");
    printf("receiver that is coherent over the block. A quaternary phase key leaves no line.\n\n| positions P | 2 | 4 | 8 | 16 | 352 |\n|---|---:|---:|---:|---:|---:|\n| line power (dB below the signal) |");
    for (double P : {2.0, 4.0, 8.0, 16.0, 352.0}) printf(" %.1f |", 2 * db(P));
    printf("\n\n");
    // ---- 12e ----
    printf("### 12e. Timing search: what sets the spacing, clock drift, and the phase interval\n\n");
    printf("A timing error tau costs (1 - tau/T)^2 of the power, where T is the stretch over which the receiver is\n");
    printf("coherent with one known phase: the flat part of the dwell for a plain tone, the interval between phase\n");
    printf("steps with the phase key. Hypotheses spaced T/8 lose 0.56 dB at worst and 0.27 dB on average; T/16 loses\n");
    printf("0.28 and 0.14 dB. The spacing is therefore a fraction of T, not a fixed time. All figures below use T/8.\n\n");
    printf("A tone that changes position every 160 ms with no guard (one cell per dwell) already has T = 160 ms, and\n");
    printf("a phase key adds nothing to its search.\n\n");
    struct Dw { const char* name; long m; double period; };
    std::vector<Dw> dws = {{"0.4 s", 2, 0.4}, {"4 s", 24, 4.0}, {"40 s", 249, 40.0}, {"400 s", 2499, 400.0}, {"4000 s", 24999, 4000.0}};
    printf("Spacing, and timing hypotheses per search dimension against the plain tone:\n\n");
    printf("| dwell | plain tone | phase step every cell | every 2 cells | every 4 cells | every 8 cells |\n|---|---|---|---|---|---|\n");
    for (auto& d : dws) {
        double sp = d.m * Tc / 8;
        printf("| %s | %s |", d.name, sp < 1 ? (std::to_string((int)std::lround(sp * 1000)) + " ms").c_str() : hstr(sp).c_str());
        for (int k : {1, 2, 4, 8}) { if (k > d.m / 2 && k > 1) { printf(" - |"); continue; } printf(" %d ms, %.0fx |", 20 * k, (double)d.m / k); }
        printf("\n");
    }
    printf("\nFrequency error and drift of the oscillators add nothing: the phase key is removed cell by cell before\n");
    printf("the frequency search. Sample-clock error does: a clock off by e slips e x (block time) over the block, and\n");
    printf("one timing hypothesis has to hold over the block, so the timing rate is a second search dimension with\n");
    printf("about 2 e (block time) / spacing hypotheses. With offset and rate both uncertain the factors multiply.\n\n");
    printf("Rate hypotheses for the 32-dwell block (plain tone / phase step every cell / every 4 cells):\n\n");
    printf("| dwell | block | clock 1e-9 | clock +-1 ppm | clock +-20 ppm |\n|---|---|---|---|---|\n");
    for (auto& d : dws) {
        if (d.m < 24) continue;
        double Tb = 32 * d.period;
        printf("| %s | %s |", d.name, hstr(Tb).c_str());
        for (double e : {1e-9, 1e-6, 20e-6}) {
            auto cnt = [&](double sp) { return std::max(1.0, std::ceil(2 * e * Tb / sp)); };
            printf(" %.0f / %.0f / %.0f |", cnt(d.m * Tc / 8), cnt(0.02), cnt(0.08));
        }
        printf("\n");
    }
    printf("\nA clock whose rate changes during the block adds a third dimension; not sized.\n\n");
    printf("What a longer phase interval gives back to the observer. With a phase step every k cells the observer\n");
    printf("can be coherent over k cells, so a dwell is m/k segments. Extra C/N0 the observer needs over the link,\n");
    printf("largest sum of segment energies over a dwell (as `blob` in 12b), N = 352 / 3,520,000 cells:\n\n");
    printf("| dwell | every cell | every 2 cells | every 4 cells | every 8 cells | plain tone (largest bin) |\n|---|---|---|---|---|---|\n");
    for (auto& d : dws) {
        if (d.m < 24) continue;
        printf("| %s |", d.name);
        std::string plain;
        for (int k : {1, 2, 4, 8, 0}) {
            std::string cell;
            for (double N : {352.0, 3.52e6}) {
                double pc = 0.01 / (N * n), pmiss1 = std::pow(0.1, 1.0 / n); long mm = k ? std::max(1L, d.m / k) : 1;
                double t = chi_threshold(mm, pc), lo = 0, hi = 50 + 40 * std::sqrt((double)mm);
                while (chi_miss(mm, hi, t) > pmiss1) hi *= 2;
                for (int i = 0; i < 70; ++i) { double mid = 0.5 * (lo + hi); if (chi_miss(mm, mid, t) > pmiss1) lo = mid; else hi = mid; }
                char b[32]; snprintf(b, sizeof b, "%s%.1f", cell.empty() ? "" : " / ", db(0.5 * (lo + hi) / lam)); cell += b;
            }
            printf(" %s dB |", cell.c_str());
        }
        printf("\n"); fflush(stdout);
    }
    printf("\n");
}

// ---------- Section 13 ----------
static void section13() {
    printf("## 13. Tone length: one cell per tone against a tone held for the hop\n\n");
    printf("The tone position changes every `tone` cells. With FHSS the hop holds a whole number of tones between\n");
    printf("its edges, and the receiver is taken as coherent only inside one tone (different positions do not share\n");
    printf("a path phase over a wide band). Spread-matched weights, large alphabet, centre known. `period` is the\n");
    printf("transmit time per tone, hop edges included.\n\n");
    struct Opt { const char* name; double T, period; };
    std::vector<Opt> opts = {{"1 cell, FHSS 0.4 s hop (2 tones)", 0.16, 0.2}, {"2 cells, FHSS 0.4 s hop (1 tone)", 0.32, 0.4}, {"1 cell, no FHSS", 0.16, 0.16}, {"2 cells, no FHSS", 0.32, 0.32},
                             {"1 cell, FHSS 4 s hop (24 tones)", 0.16, 4.0 / 24}, {"24 cells, FHSS 4 s hop (1 tone)", 3.84, 4.0}, {"24 cells, no FHSS", 3.84, 3.84}};
    std::vector<NamedChan> chans = {{"steady", LINE()}, {"0.1 Hz", G(0.1)}, {"1 Hz", G(1)}, {"3 Hz", G(3)}, {"10 Hz", G(10)}, {"30 Hz", G(30)}};
    printf("### 13a. Extra transmit time against the bound (dB)\n\n");
    for (double c : {10.0, 0.0, -10.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Tone |", c); for (auto& nc : chans) printf(" %s |", nc.name.c_str());
        printf("\n|---|"); for (size_t i = 0; i < chans.size(); ++i) printf("---:|"); printf("\n");
        for (auto& o : opts) { printf("| %s |", o.name); for (auto& nc : chans) { int K = span_bins(nc.ch, o.T); auto lam = bin_energy(nc.ch, P, o.T, K); printf(" %.1f |", db(bound_rate(nc.ch, P) * o.period / gmi_wp(lam, lam))); } printf("\n"); }
        printf("\n");
    }
    printf("### 13b. Block duration, 10 bits (section 7 method, A = 400, at least 32 tones; `f` marks the floor)\n\n| C/N0 (dB-Hz) | Channel |");
    for (auto& o : opts) printf(" %s |", o.name);
    printf("\n|---:|---|"); for (size_t i = 0; i < opts.size(); ++i) printf("---|"); printf("\n");
    double z10 = qinv(1e-6 / (400 * 1024.0)) + qinv(1e-3), S = z10 * z10;
    for (double c : {20.0, 10.0, 0.0, -10.0, -20.0}) for (auto& nc : chans) {
        if (nc.name == "3 Hz" || nc.name == "30 Hz") continue;
        double P = undb(c); printf("| %+.0f | %s |", c, nc.name.c_str());
        for (auto& o : opts) { int K = span_bins(nc.ch, o.T); auto lam = bin_energy(nc.ch, P, o.T, K); double n = std::max(32.0, std::ceil(S / (2 * gmi_wp(lam, lam)))); printf(" %s%s |", hstr(n * o.period).c_str(), n <= 32 ? " f" : ""); }
        printf("\n");
    }
    printf("\n");
}

// ---------- Section 14 ----------
static void section14() {
    printf("## 14. One-cell tones with keyed hopping: an observer without the key\n\n");
    printf("Link: one tone per 160 ms cell on 352 keyed positions, steady path, receiver coherent inside one tone,\n");
    printf("10-bit block sized by the exact presence test of section 10e (A = 4000). FHSS hops hold `tones per hop`\n");
    printf("tones on one of `channels` keyed hop channels; hop edges are ignored. Observer as in 11d (lattice, band,\n");
    printf("tone and hop timing known; 0.9 detection at 1e-2 false alarm; z = 3.61), Gaussian deflection throughout:\n");
    printf("per tone on the occupied channel d1^2 = (exp(l^2) - 1)^2 (1 - l^2) / (352 l^2) with l the observer's energy\n");
    printf("per tone; per hop dh^2 = (tones per hop) d1^2; over the channels the per-hop likelihood is a mixture, with\n");
    printf("deflection (exp(dh^2) - 1) / channels; the block needs hops x that = z^2. `margin` is the extra C/N0 the\n");
    printf("observer needs. `sqrt` is the square-root ideal 5 log10(352 channels z^2 / S). `~` marks dh > 1.5 or l > 0.6,\n");
    printf("where a sum is far from Gaussian and the figure is rough. `> x` means the observer would need tones\n");
    printf("with l above 0.9, where this weak-tone formula stops; the margin is at least x.\n\n");
    const double tau = std::log(4000.0 * 1024.0 / 1e-6), zo = qinv(1e-2) + qinv(0.1), Tc = 0.16;
    double Sk = std::pow(qinv(std::exp(-tau)) + qinv(1e-3), 2);
    for (double c : {0.0, -10.0, -20.0}) {
        double lam = undb(c) * Tc, lo = 32, hi = 1e12;
        for (int i = 0; i < 80; ++i) { double mid = std::sqrt(lo * hi); if (energy_needed((long)mid, std::exp(-tau), 1e-3) / mid > lam) lo = mid; else hi = mid; }
        double n = hi;
        printf("**C/N0 = %+.0f dB-Hz**: %.3g tones per block (%s), energy per tone %.3g.\n\n", c, n, hstr(n * Tc).c_str(), lam);
        printf("| hop | tones per hop | hops per block | 1 channel (dB) | 50 (dB) | 1,000 (dB) | 10,000 (dB) |\n|---|---:|---:|---:|---:|---:|---:|\n");
        struct Hp { const char* name; double m; };
        for (Hp h : {Hp{"0.4 s", 2}, Hp{"4 s", 24}, Hp{"40 s", 249}, Hp{"400 s", 2499}, Hp{"4000 s", 24999}}) {
            double H = n / h.m; if (H < 4) continue;
            printf("| %s | %.0f | %.3g |", h.name, h.m, H);
            for (double N : {1.0, 50.0, 1000.0, 10000.0}) {
                double dh2 = std::log1p(zo * zo * N / H), d12 = dh2 / h.m, a = 1e-6, b = 0.999;
                auto f = [&](double l) { double e = std::expm1(l * l); return e * e * (1 - l * l) / (352 * l * l); };
                if (f(0.9) < d12) { printf(" > %.0f |", db(0.9 / lam)); continue; }
                b = 0.9; for (int i = 0; i < 80; ++i) { double mid = 0.5 * (a + b); if (f(mid) < d12) a = mid; else b = mid; }
                printf(" %.1f%s |", db(0.5 * (a + b) / lam), (dh2 > 2.25 || 0.5 * (a + b) > 0.6) ? " ~" : "");
            }
            printf("\n");
        }
        printf("\n");
    }
    printf("| | 1 channel | 50 | 1,000 | 10,000 |\n|---|---:|---:|---:|---:|\n| sqrt (dB) |");
    for (double N : {1.0, 50.0, 1000.0, 10000.0}) printf(" %.1f |", 0.5 * db(352 * N * zo * zo / Sk));
    printf("\n\nWithout hopping an observer at the link's own C/N0 needs about the square of the one-channel margin in\n");
    printf("time, since weak-tone evidence grows with the square of the power.\n\n");
    printf("### 14b. How short the hop has to be\n\n");
    printf("In the weak-tone range the margin is proportional to dh, and dh^2 = ln(1 + x) with x = z^2 channels /\n");
    printf("(hops per block), against x for the square-root ideal. The hopping benefit given back is therefore\n");
    printf("5 log10(x / ln(1 + x)) dB:\n\n| x | 0.1 | 0.3 | 1 | 3 | 10 | 30 |\n|---|---:|---:|---:|---:|---:|---:|\n| given back (dB) |");
    for (double x : {0.1, 0.3, 1.0, 3.0, 10.0, 30.0}) printf(" %.1f |", 0.5 * db(x / std::log1p(x)));
    printf("\n\nx = 1 is %.0f hops per block for every hop channel. Longest hop that keeps x at or below 1:\n\n", zo * zo);
    printf("| C/N0 (dB-Hz) | block | 50 channels | 1,000 channels | 10,000 channels |\n|---:|---|---|---|---|\n");
    for (double c : {0.0, -10.0, -20.0}) {
        double lam = undb(c) * Tc, lo = 32, hi = 1e12;
        for (int i = 0; i < 80; ++i) { double mid = std::sqrt(lo * hi); if (energy_needed((long)mid, std::exp(-tau), 1e-3) / mid > lam) lo = mid; else hi = mid; }
        printf("| %+.0f | %s |", c, hstr(hi * Tc).c_str());
        for (double N : {50.0, 1000.0, 10000.0}) { double h = hi * Tc / (zo * zo * N); if (h < 0.4) printf(" under 0.4 s |"); else printf(" %s |", hstr(h).c_str()); }
        printf("\n");
    }
    printf("\nWhat a shorter hop costs the receiver with one-cell tones: only its edges (2 x 40 ms at 0.4 s, 2 x 80 ms\n");
    printf("above), since the receiver is coherent inside a tone and not across the hop.\n\n| hop | 0.4 s | 4 s | 40 s | 400 s | 4000 s |\n|---|---:|---:|---:|---:|---:|\n| time lost to edges (dB) |");
    for (double h : {0.4, 4.0, 40.0, 400.0, 4000.0}) printf(" %.3f |", -db(1 - (h < 1 ? 0.08 : 0.16) / h));
    printf("\n\n");
}

// ---------- Section 15 ----------
// Required weak-tone Gaussian deflection, then steady-path total E/N0 for 1, 32 and 1024 looks.
static std::array<double, 4> presence_requirements(double per_template_false, double miss) {
    const double z = qinv(per_template_false) + qinv(miss);
    return {z * z, energy_needed(1, per_template_false, miss),
            energy_needed(32, per_template_false, miss), energy_needed(1024, per_template_false, miss)};
}

static void section15() {
    printf("## 15. Ten bits against one bit at the same confidence\n\n");
    printf("A block of k bits is sized by its presence test: false-presence rate 1e-6 over A 2^k tests, miss rate\n");
    printf("1e-3. `S` is the Gaussian deflection (z_fa + z_miss)^2 of section 7, to which block time is proportional\n");
    printf("for weak tones. `E` is the exact total E/N0 (chi-square tails, steady path) for a block added as n\n");
    printf("energies. Ratios are against k = 1 in the same row group; at fixed power they are ratios of transmit time.\n\n");
    printf("| A | k | S | S ratio | E, n = 1 (dB) | ratio | E, n = 32 (dB) | ratio | E, n = 1024 (dB) | ratio |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (double A : {1.0, 400.0, 4000.0}) {
        std::array<double, 4> one_bit{};
        for (int k : {1, 10, 16, 24}) {
            const auto need = presence_requirements(1e-6 / (A * std::pow(2.0, k)), 1e-3);
            if (k == 1) one_bit = need;
            printf("| %.0f | %d | %.1f | %.2f |", A, k, need[0], need[0] / one_bit[0]);
            for (size_t i = 1; i < need.size(); ++i) printf(" %.2f | %.2f |", db(need[i]), need[i] / one_bit[i]);
            printf("\n");
        }
    }
    printf("\nFor comparison, message error alone (no presence test), ideal orthogonal signalling, from\n");
    printf("../short-message-energy: E(10)/E(1) = 2.48, 2.01 and 1.77 at error rates of 1e-2, 1e-3 and 1e-4.\n\n");
}

// ---------- Section 16 ----------
static void section16() {
    printf("## 16. Losing hops: how many hops a block has to span\n\n");
    printf("Each hop is unusable with probability p, independently (interference on some hop frequencies or during\n");
    printf("some hops), and the receiver treats an unusable hop as erased. A margin of M dB tolerates the loss of a\n");
    printf("fraction f of the block's tones (0.57, 0.77, 0.87, 0.90, 0.91, 0.92 for 3, 6, 10, 13, 16, 20 dB, from\n");
    printf("../short-message-energy). The block's tones are spread evenly over H hops. Smallest H for which more than\n");
    printf("f of the hops are lost with probability at most 1e-3 and 1e-2 (exact binomial); `-` if p is not below f.\n\n");
    const double fs[6] = {0.57, 0.77, 0.87, 0.90, 0.91, 0.92}; const int Ms[6] = {3, 6, 10, 13, 16, 20};
    for (double q : {1e-3, 1e-2}) {
        printf("**Probability at most %g**\n\n| p lost | 3 dB | 6 dB | 10 dB | 13 dB | 16 dB | 20 dB |\n|---:|---:|---:|---:|---:|---:|---:|\n", q);
        for (double p : {0.25, 0.5, 0.75}) {
            printf("| %.2f |", p);
            for (int i = 0; i < 6; ++i) {
                (void)Ms; if (p >= fs[i]) { printf(" - |"); continue; }
                int found = -1;
                for (int H = 1; H <= 20000 && found < 0; ++H) {
                    int kmax = (int)std::floor(fs[i] * H + 1e-9); double tail = 0;   // P(lost > kmax)
                    for (int k = kmax + 1; k <= H; ++k) tail += std::exp(std::lgamma(H + 1.0) - std::lgamma(k + 1.0) - std::lgamma(H - k + 1.0) + k * std::log(p) + (H - k) * std::log1p(-p));
                    if (tail <= q) { bool ok = true;   // require it to hold for the next 8 values too (the tail is not monotone in H)
                        for (int H2 = H + 1; H2 <= H + 8 && ok; ++H2) { int km = (int)std::floor(fs[i] * H2 + 1e-9); double t2 = 0; for (int k = km + 1; k <= H2; ++k) t2 += std::exp(std::lgamma(H2 + 1.0) - std::lgamma(k + 1.0) - std::lgamma(H2 - k + 1.0) + k * std::log(p) + (H2 - k) * std::log1p(-p)); if (t2 > q) ok = false; }
                        if (ok) found = H; }
                }
                if (found < 0) printf(" > 20000 |"); else printf(" %d |", found);
            }
            printf("\n");
        }
        printf("\n");
    }
    printf("Losing a fraction p of the hops costs 1/(1 - p) in usable time: 3.0 dB of time for half the hops, which\n");
    printf("is about 1.5 dB of power for weak tones. That is the sense in which lost hops act as a higher noise floor.\n\n");
}

// ---------- Section 17 ----------
// Coherent pulse attack on tones that all start their cell at the same phase, and what a keyed phase does to it.
static void section17(int trials) {
    printf("## 17. Tones without a keyed phase: the cell-rate pulse an observer can add coherently\n\n");
    printf("Every position completes whole cycles in a cell. If every tone starts its cell at the same phase, the mean\n");
    printf("of the signal over the P unknown positions is (1/P) sum_k exp(j 2 pi k n / N): a pulse at each cell start\n");
    printf("holding 1/P of the tone energy. An observer correlates each cell with that pulse and adds the M results\n");
    printf("coherently, with no knowledge of the positions. With l the energy per tone over the noise density, the sum\n");
    printf("is one complex value of mean-square M l / P over unit noise; one carrier phase is unknown, so the test is on\n");
    printf("its magnitude (exact, noncentral chi-square with one complex term). Steady phase at the observer over the\n");
    printf("whole observation; cell timing known; P = 352. The energy observer is that of section 14 with one hop\n");
    printf("channel. Detection 0.9 at 1e-2 false alarm, shared over the frequency bins searched. Theoretical.\n\n");
    const double P = 352, zo = qinv(1e-2) + qinv(0.1), Tc = 0.16;
    auto energy_obs = [&](double n) {   // tone energy the section-14 energy observer needs over n tones
        double a = 1e-9, b = 0.9;
        for (int i = 0; i < 100; ++i) { double l = std::sqrt(a * b), e = std::exp(l * l) - 1; if (n * e * e * (1 - l * l) / (P * l * l) < zo * zo) a = l; else b = l; }
        return std::sqrt(a * b);
    };
    const double lam1 = energy_needed(1, 1e-2, 0.1), lam4 = energy_needed(1, 1e-2 / 1e4, 0.1);
    printf("### 17a. Energy per tone the observer needs, against observation time\n\n");
    printf("Needed M l / P: %.2f with the carrier frequency known, %.2f when 10,000 frequency bins are searched.\n\n", lam1, lam4);
    printf("| observation | cells M | energy observer l (dB) | pulse, frequency known (dB) | pulse, 10^4 bins (dB) | advantage (dB) | with 10^4 bins (dB) |\n|---|---:|---:|---:|---:|---:|---:|\n");
    struct Ob { const char* name; double sec; };
    for (Ob o : {Ob{"10 min", 600}, Ob{"1 h", 3600}, Ob{"18 h", 64800}, Ob{"1 week", 604800}, Ob{"30 days", 2592000}}) {
        double M = o.sec / Tc, le = energy_obs(M), l1 = lam1 * P / M, l4 = lam4 * P / M;
        printf("| %s | %.3g | %.1f | %.1f | %.1f | %.1f | %.1f |\n", o.name, M, db(le), db(l1), db(l4), db(le / l1), db(le / l4));
    }
    printf("\nThe advantage grows 5 dB for every tenfold of observation time and has no limit.\n\n");
    printf("### 17b. Observer margin over one block of the link\n\n");
    printf("Link as in section 14: one-cell tones, 10-bit block sized by the presence test (A = 4000), no hopping.\n");
    printf("Margin is the extra C/N0 the observer needs over the link; negative means the observer detects the block\n");
    printf("at a lower level than the link works at.\n\n");
    printf("| link C/N0 | tones per block | block | energy observer (dB) | pulse, frequency known (dB) | pulse, 10^4 bins (dB) |\n|---|---:|---|---:|---:|---:|\n");
    const double tau = std::log(4000.0 * 1024.0 / 1e-6);
    for (double c : {0.0, -10.0, -20.0}) {
        double lam = undb(c) * Tc, lo = 32, hi = 1e12;
        for (int i = 0; i < 80; ++i) { double mid = std::sqrt(lo * hi); if (energy_needed((long)mid, std::exp(-tau), 1e-3) / mid > lam) lo = mid; else hi = mid; }
        double n = hi;
        printf("| %+.0f dB-Hz | %.3g | %s | %.1f | %.1f | %.1f |\n", c, n, hstr(n * Tc).c_str(), db(energy_obs(n) / lam), db(lam1 * P / n / lam), db(lam4 * P / n / lam));
    }
    printf("\n### 17c. Monte Carlo in the time domain: no key, two-phase key, four-phase key\n\n");
    const int N = CELL, Pn = 352, M = 1024, T = std::max(100, std::min(trials, 600));
    printf("%d cells of %d samples, a tone on one of %d positions drawn afresh each cell, complex Gaussian noise, %d\n", M, N, Pn, T);
    printf("trials per entry. `pulse` correlates the samples with the pulse above. `squared pulse` squares the samples\n");
    printf("first and correlates with the pulse of the doubled positions, the attack on a two-phase key. Thresholds are\n");
    printf("the 99th percentile of noise-only trials. The first row is at the l that 17a predicts for 0.9 detection.\n\n");
    std::vector<cd> t1(N), t2(N);
    for (int n = 0; n < N; ++n) { cd a = 0, b = 0; for (int k = 0; k < Pn; ++k) { a += std::polar(1.0, 2 * PI * k * n / N); b += std::polar(1.0, 4 * PI * k * n / N); } t1[n] = std::conj(a); t2[n] = std::conj(b); }
    auto run = [&](double l, int key, std::vector<double>& s1, std::vector<double>& s2, uint64_t seed) {   // key: 0 none, 2 two phases, 4 four phases; l < 0 means noise only
        s1.assign(T, 0); s2.assign(T, 0);
        #pragma omp parallel for schedule(dynamic)
        for (int t = 0; t < T; ++t) {
            Rng r(seed + 7919ull * t); cd z1 = 0, z2 = 0; double amp = l > 0 ? std::sqrt(l / N) : 0;
            for (int c = 0; c < M; ++c) {
                int k = (int)(r.uni() * Pn); double th = key ? 2 * PI * (int)(r.uni() * key) / key : 0;
                for (int n = 0; n < N; ++n) {
                    cd x = r.cn(); if (amp > 0) x += amp * std::polar(1.0, 2 * PI * ((k * n) % N) / N + th);
                    z1 += x * t1[n]; z2 += x * x * t2[n];
                }
            }
            s1[t] = std::norm(z1); s2[t] = std::norm(z2);
        }
    };
    std::vector<double> h1, h2, a1, a2; run(-1, 0, h1, h2, 1001);
    std::sort(h1.begin(), h1.end()); std::sort(h2.begin(), h2.end());
    double th1 = h1[(size_t)(0.99 * T)], th2 = h2[(size_t)(0.99 * T)];
    printf("| l per tone (dB) | key | pulse detects | squared pulse detects |\n|---:|---|---:|---:|\n");
    double lstar = lam1 * Pn / M;
    for (double l : {lstar, undb(10.0), undb(15.0)}) for (int key : {0, 2, 4}) {
        run(l, key, a1, a2, 2000 + (uint64_t)(db(l) * 10) + key);
        int d1 = 0, d2 = 0; for (int t = 0; t < T; ++t) { d1 += a1[t] > th1; d2 += a2[t] > th2; }
        printf("| %.1f | %s | %.2f | %.2f |\n", db(l), key == 0 ? "none" : key == 2 ? "two phases" : "four phases", (double)d1 / T, (double)d2 / T);
    }
    printf("\nA rate near 0.01 is the false-alarm rate: nothing detected.\n\n");
}

// ---------- Section 18 ----------
// Search volume for coherent reception on a phase-stable path below 9 kHz.
static void section18() {
    printf("## 18. Below 9 kHz: how many hypotheses coherent reception has to try\n\n");
    printf("Carrier 9 kHz, FHSS off, one coherent window of length T. Counted per window: frequency bins, drift-rate\n");
    printf("bins and timing offsets, each combination one block decode. The sample clock is taken from the same\n");
    printf("reference as the carrier, so its rate follows the frequency hypothesis; the last column of 18c is the\n");
    printf("extra factor if it does not. Theoretical; the oscillator classes are assumptions, not measurements.\n\n");
    printf("### 18a. Spacing rules\n\n");
    auto sinc2 = [](double x) { return x == 0 ? 1.0 : std::pow(std::sin(PI * x) / (PI * x), 2); };
    printf("Frequency: a tone off by x bins of 1/T keeps sinc^2(x).\n\n| bin spacing | worst loss (dB) | mean loss (dB) |\n|---|---:|---:|\n");
    for (double sp : {1.0, 0.5, 0.25}) { double m = 0; int K = 2000; for (int i = 0; i < K; ++i) m += sinc2(sp * 0.5 * (i + 0.5) / K); printf("| %.2g / T | %.2f | %.2f |\n", sp, -db(sinc2(sp / 2)), -db(m / K)); }
    auto chirp_keep = [](double d) {   // power kept with a residual drift of d bins of 1/T across the window, frequency refitted
        cd a = 0; int K = 4000; for (int i = 0; i < K; ++i) { double u = (i + 0.5) / K - 0.5; a += std::polar(1.0, PI * d * u * u); } return std::norm(a / (double)K);
    };
    double dstep[2]; int q = 0;
    printf("\nDrift: a frequency that moves by d bins of 1/T across the window, linearly, keeps the value below after the\nbest fixed frequency is fitted.\n\n| residual drift across the window (bins) | loss (dB) |\n|---:|---:|\n");
    for (double d : {0.5, 1.0, 2.0, 4.0}) printf("| %.1f | %.2f |\n", d, -db(chirp_keep(d)));
    for (double target : {0.56, 0.91}) { double a = 0, b = 8; for (int i = 0; i < 60; ++i) { double m = 0.5 * (a + b); if (-db(chirp_keep(m)) < target) a = m; else b = m; } dstep[q++] = 2 * a; }
    printf("\nDrift hypotheses %.2f bins apart lose at most 0.56 dB (the timing rule's worst case); %.2f apart at most 0.91 dB\n(the worst case of half-bin frequency spacing). The tables use half-bin frequency spacing and the %.2f rule.\n\n", dstep[0], dstep[1], dstep[1]);
    const double fc = 9000, ds = dstep[1];
    struct Cl { const char* name; double off_ppm, drift_ppm_h, timing_s; };
    const Cl cls[] = {
        {"GPS-disciplined, corrections logged (±0.001 ppm, no drift, ±1 ms)", 1e-3, 0, 1e-3},
        {"GPS-disciplined, corrections slewed unseen at up to 1 ppm (±1 ppm, 1 ppm/h, ±1 ms)", 1, 1, 1e-3},
        {"TCXO (±1 ppm, 0.1 ppm/h, ±0.1 s)", 1, 0.1, 0.1},
        {"Crystal (±20 ppm, 1 ppm/h, ±1 s)", 20, 1, 1},
        {"Sound-card clock (±100 ppm, 5 ppm/h, ±1 s)", 100, 5, 1},
        {"±5 Hz stated uncertainty (±556 ppm, 5 ppm/h, ±1 s)", 5 / fc * 1e6, 5, 1},
    };
    struct Wn { const char* name; double T; };
    const Wn wns[] = {{"1 min", 60}, {"22 min", 1320}, {"1 h", 3600}, {"5.8 h", 20880}};
    printf("### 18b. Frequency and drift-rate bins per window\n\n| reference at each end | ± frequency (Hz) |");
    for (auto& w : wns) printf(" %s: frequency × drift |", w.name);
    printf("\n|---|---:|"); for (size_t i = 0; i < 4; ++i) printf("---:|"); printf("\n");
    for (auto& c : cls) {
        printf("| %s | %.3g |", c.name, c.off_ppm * 1e-6 * fc);
        for (auto& w : wns) {
            double F = std::max(1.0, std::ceil(2 * c.off_ppm * 1e-6 * fc * w.T * 2)), D = std::max(1.0, std::ceil(2 * c.drift_ppm_h * 1e-6 * fc * (w.T / 3600) * w.T / ds));
            printf(" %.0f × %.0f |", F, D);
        }
        printf("\n");
    }
    printf("\n### 18c. Decodes per window against the measured decode rates\n\n");
    printf("Timing offsets are the timing uncertainty over an eighth of the phase interval. `need` is decodes per\n");
    printf("second of signal (decodes per window over T). Measured on the RTX PRO 6000 Blackwell: 8,400 full 24-bit\n");
    printf("decodes a second as a direct kernel; about 65 a second for the coherent ladder by extrapolation. `x` marks\n");
    printf("a need above 8,400 a second, `o` above 65.\n\n");
    for (double iv : {0.16, 10.0}) {
        printf("**Phase interval %.3g s (timing spacing %.3g s).**\n\n| reference at each end | timing offsets |", iv, iv / 8);
        for (auto& w : wns) printf(" %s: need (1/s) |", w.name);
        printf(" clock-rate factor if separate, 1 h |\n|---|---:|"); for (size_t i = 0; i < 5; ++i) printf("---:|"); printf("\n");
        for (auto& c : cls) {
            double To = std::max(1.0, std::ceil(2 * c.timing_s / (iv / 8)));
            printf("| %s | %.0f |", c.name, To);
            for (auto& w : wns) {
                double F = std::max(1.0, std::ceil(2 * c.off_ppm * 1e-6 * fc * w.T * 2)), D = std::max(1.0, std::ceil(2 * c.drift_ppm_h * 1e-6 * fc * (w.T / 3600) * w.T / ds));
                double need = F * D * To / w.T;
                printf(" %.3g%s |", need, need > 8400 ? " x" : need > 65 ? " o" : "");
            }
            printf(" %.0f |\n", std::max(1.0, std::ceil(2 * c.off_ppm * 1e-6 * 3600 / (iv / 8))));
        }
        printf("\n");
    }
}

static void admission_tradeoff() {
    printf("## 19. Ten-bit and one-bit presence at selectable null-block budgets\n\n");
    printf("One public block has A nuisance hypotheses per payload. Each template receives the same\n");
    printf("null budget alpha / (A × 2^k). The union bound allows dependent templates with valid marginal\n");
    printf("tails. The required miss probability on the true template stays at 0.001.\n\n");
    printf("S is the weak-tone Gaussian deflection. E1, E32 and E1024 are steady-path E/N0 from the\n");
    printf("chi-square model for the indicated count of independent, equal-noise complex looks. Each\n");
    printf("ratio divides the ten-bit requirement by the one-bit requirement at the same alpha.\n");
    printf("Each saving compares ten bits at the listed alpha with ten bits at 1e-6.\n\n");
    printf("| A | alpha | S ratio | E1 ratio | E32 ratio | E1024 ratio | S saving | E1 saving | E32 saving | E1024 saving |\n");
    printf("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int hypotheses : {1, 400, 4000}) {
        const auto reference = presence_requirements(1e-6 / (1024 * hypotheses), 1e-3);
        for (double alpha : {1e-6, 1e-4, 1e-3, 1e-2}) {
            const auto one = presence_requirements(alpha / (2 * hypotheses), 1e-3);
            const auto ten = presence_requirements(alpha / (1024 * hypotheses), 1e-3);
            printf("| %d | %.0e |", hypotheses, alpha);
            for (size_t i = 0; i < ten.size(); ++i) printf(" %.3f |", ten[i] / one[i]);
            for (size_t i = 0; i < ten.size(); ++i) printf(" %.1f%% |", 100 * (1 - ten[i] / reference[i]));
            printf("\n");
        }
    }
    printf("\n### Ten bits at a relaxed budget against one bit at 1e-6\n\n");
    printf("Ratios divide the ten-bit requirement at the listed alpha by the original one-bit requirement\n");
    printf("at 1e-6. Both modes search the same A and keep the true-template miss probability at 0.001.\n\n");
    printf("| A | ten-bit alpha | S ratio | E1 ratio | E32 ratio | E1024 ratio |\n");
    printf("|---:|---:|---:|---:|---:|---:|\n");
    for (int hypotheses : {1, 400, 4000}) {
        const auto strict_one = presence_requirements(1e-6 / (2 * hypotheses), 1e-3);
        for (double alpha : {1e-4, 1e-3}) {
            const auto ten = presence_requirements(alpha / (1024 * hypotheses), 1e-3);
            printf("| %d | %.0e |", hypotheses, alpha);
            for (size_t i = 0; i < ten.size(); ++i) printf(" %.3f |", ten[i] / strict_one[i]);
            printf("\n");
        }
    }
    printf("\nAll four ratios equal 1 at ten-bit alpha = (1024/2) × 1e-6 = 5.12e-4.\n");
    printf("At that budget the two modes allocate exactly the same null probability to each template.\n");
    printf("Above it the ten-bit presence test needs less signal under these assumptions.\n");
    printf("\nAt fixed received power and duty factor, a ratio of E gives the active-time and wall-time ratio\n");
    printf("if the same count of steady looks can scale in duration. At fixed duration and look count, its\n");
    printf("power saving in dB is 10 log10(E_old/E_new). At fixed per-look weak signal and geometry, S\n");
    printf("scales with the number of looks and unrounded duration. At fixed duration, weak-tone deflection\n");
    printf("scales with squared power, giving a power saving of 5 log10(S_old/S_new). A fixed transmitted\n");
    printf("block keeps its scheduled duration when a receiver alone changes its threshold. Discrete dwell\n");
    printf("floors, fading, wrong active-payload winners and operator recognition require separate checks.\n");
}

int main(int argc, char** argv) {
    int trials = argc > 1 ? std::atoi(argv[1]) : 20000;
    std::vector<NamedChan> chans = {
        {"Stable phase (line)", LINE()},
        {"Gaussian 0.1 Hz", G(0.1)},
        {"Gaussian 0.5 Hz", G(0.5)},
        {"Gaussian 1 Hz", G(1)},
        {"Gaussian 3 Hz", G(3)},
        {"Gaussian 10 Hz", G(10)},
        {"Gaussian 30 Hz", G(30)},
        {"Two modes 1 Hz at ±5 Hz", G(1, 0.5, -5) + G(1, 0.5, 5)},
        {"Rician K=1, diffuse 1 Hz", LINE(0.5) + G(1, 0.5)},
        {"Auroral 50 Hz at +20 Hz", G(50, 1, 20)},
    };
    printf("# Matrix Modem FSK lattice: receiver calculations\n\n");
    printf("Generated by `lattice_receiver.cpp`. Complex baseband at 3200 Hz, cell = 512 samples = 160 ms, bin = 6.25 Hz.\n");
    printf("Dwell period D with nc whole active cells starting 40 ms after the dwell start: A 0.4 s / 2 cells,\n");
    printf("B 4 s / 24 cells, C 40 s / 249 cells. Phase and fading are independent from dwell to dwell. Known timing\n");
    printf("and Doppler centre, Gaussian noise, no interference, ideal coding. Theoretical calculation.\n\n");
    std::string want = argc > 2 ? argv[2] : "1234567";
    auto on = [&](char c) { return want.find(c) != std::string::npos; };
    if (on('1')) section1();
    if (on('2')) section2(chans);
    if (on('3')) section3();
    if (on('4')) section4(trials);
    if (on('5')) section5(trials);
    if (on('6')) section6();
    if (on('7')) section7();
    if (on('8')) section8();
    if (on('9')) section9(trials);
    if (on('A')) section10(trials);
    if (on('B')) section11(trials);
    if (on('C')) section12();
    if (on('D')) section13();
    if (on('E')) section14();
    if (on('F')) section15();
    if (on('G')) section16();
    if (on('H')) section17(trials);
    if (on('I')) section18();
    if (on('J')) admission_tradeoff();
    return 0;
}
