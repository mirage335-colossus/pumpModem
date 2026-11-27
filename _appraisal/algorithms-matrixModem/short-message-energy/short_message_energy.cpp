// short_message_energy.cpp
//
// THEORETICAL CALCULATION (research material; not modem code, links to nothing in the repository).
// Supports _ref/algorithms-matrixModem/Fable-5.2/reviews/review-coding-interleaving.md.
//
// Energy per MESSAGE (E/N0, not per bit) needed to deliver k information bits with message-error
// probability eps, using M = 2^k equal-energy orthogonal codewords and maximum-likelihood
// (largest-statistic) detection:
//
//   A. AWGN, one unknown phase per message (noncoherent), exact.
//   B. AWGN, coherent orthogonal (exact) and the Polyanskiy-Poor-Verdu 2011 converse for ANY code
//      without feedback:  1/M >= Q( sqrt(2E/N0) + Qinv(1-eps) ).
//   C. Rayleigh fading without channel knowledge: each codeword is L cells (branches) that fade
//      independently, mean branch SNR E/(L N0); the other M-1 codewords occupy disjoint cells;
//      square-law combining (exactly ML for equal-mean Rayleigh branches). Exact, L optimised.
//   D. Ratios against k = 1 and k = 10; marginal energy per added bit; where the cost turns linear.
//   E. Sectioning: s independently ML-decoded sections against one joint codebook.
//   F. Noncoherent (square-law) combining loss on AWGN against the number of looks.
//   G. Rayleigh: energy against a capped per-branch SNR (the weak, coherence-limited regime).
//   H. Margin against erasures: how many branches may be erased when every surviving branch is
//      X dB stronger than the design point, for a design with fixed branch count.
//      Also: random (binomial) erasures with a keyed interleaver, and the converse design that
//      spends the margin on extra branches.
//   I. Quiet-slot (end-of-message) test using the largest codeword score.
//   J. Cutoff rate R0 of q-ary noncoherent orthogonal symbols (sequential/Viterbi yardstick).
//
// Model limits: ideal orthogonality, independent branches (ideal interleaving), known timing and
// frequency, Gaussian noise of known level, no interference, no acquisition cost, no quantisation.
// "Square-law combining" on AWGN is the standard combiner, not the exact ML rule (sum of ln I0);
// on Rayleigh branches it is exactly ML. All numbers are "derived here".
//
// Build and run (about 8 minutes on a 12-thread laptop):
//   g++ -O2 -std=c++17 -fopenmp short_message_energy.cpp -o /tmp/short_message_energy
//   /tmp/short_message_energy > results/short_message_energy.md
//
// Standard library only.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

typedef long double ld;

static const ld LN2 = 0.693147180559945309417232121458L;

static ld dB(ld x) { return 10.0L * log10l(x); }

// Regularised incomplete gamma functions P(a,x) and Q(a,x), a > 0.
static void incgam(ld a, ld x, ld &P, ld &Q) {
    if (x <= 0) { P = 0; Q = 1; return; }
    const ld gln = lgammal(a);
    if (x < a + 1) {
        ld ap = a, sum = 1 / a, del = sum;
        for (int n = 0; n < 2000000; n++) {
            ap += 1; del *= x / ap; sum += del;
            if (fabsl(del) < fabsl(sum) * 1e-19L) break;
        }
        P = sum * expl(-x + a * logl(x) - gln);
        if (P > 1) P = 1;
        Q = 1 - P;
    } else {
        const ld FPMIN = 1e-4000L;
        ld b = x + 1 - a, c = 1 / FPMIN, d = 1 / b, h = d;
        for (int i = 1; i < 2000000; i++) {
            ld an = -(ld)i * ((ld)i - a);
            b += 2;
            d = an * d + b; if (fabsl(d) < FPMIN) d = FPMIN;
            c = b + an / c; if (fabsl(c) < FPMIN) c = FPMIN;
            d = 1 / d;
            ld del = d * c; h *= del;
            if (fabsl(del - 1) < 1e-19L) break;
        }
        Q = expl(-x + a * logl(x) - gln) * h;
        P = 1 - Q;
    }
}

// h(x) = 1 - P(L,x)^(M-1): probability that at least one of M-1 noise-only codeword statistics
// (each Gamma(L,1)) exceeds x.
struct HGrid {
    int L = 1;
    ld k = 1, dx = 0.02L;
    std::vector<ld> h;   // h[i] at x = i*dx, even number of intervals
    std::vector<ld> w;   // Simpson weights
};

static HGrid make_h(ld k, int L) {
    HGrid g; g.L = L; g.k = k;
    const ld Mm1 = powl(2.0L, k) - 1.0L;
    g.dx = 0.02L * std::max((ld)1.0L, sqrtl((ld)L) / 4.0L);
    // Upper limit: where (M-1) Q(L,x) < 1e-26.
    ld xmax = L + 10 * sqrtl((ld)L) + 20;
    for (;;) {
        ld P, Q; incgam(L, xmax, P, Q);
        if (Mm1 * Q < 1e-26L) break;
        xmax += std::max((ld)5.0L, 0.05L * xmax);
    }
    long n = (long)ceill(xmax / g.dx);
    if (n % 2) n++;
    g.h.assign(n + 1, 0);
    #pragma omp parallel for schedule(dynamic, 256)
    for (long i = 0; i <= n; i++) {
        ld x = i * g.dx, P, Q;
        if (i == 0) { g.h[i] = 1; continue; }
        incgam(L, x, P, Q);
        ld lp = (Q < 0.5L) ? log1pl(-Q) : logl(P);
        g.h[i] = -expm1l(Mm1 * lp);
    }
    g.w.assign(n + 1, 0);
    for (long i = 0; i <= n; i++)
        g.w[i] = g.dx / 3 * ((i == 0 || i == n) ? 1 : (i % 2 ? 4 : 2));
    return g;
}

static std::map<std::pair<long, int>, HGrid> g_cache;
static const HGrid &hgrid(ld k, int L) {
    std::pair<long, int> key((long)llroundl(k * 1000000), L);
    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        if (g_cache.size() > 64) g_cache.clear();
        it = g_cache.emplace(key, make_h(k, L)).first;
    }
    return it->second;
}

// Rayleigh: message error for M = 2^k, L branches, mean branch SNR gam.
static ld pe_ray(const HGrid &g, ld gam) {
    const int L = g.L; const ld s = 1 + gam, lg = lgammal((ld)L);
    const long n = (long)g.h.size();
    ld acc = 0;
    #pragma omp parallel for reduction(+:acc) schedule(static)
    for (long i = 0; i < n; i++) {
        if (g.h[i] <= 0) continue;
        ld x = i * g.dx, f;
        if (i == 0) f = (L == 1) ? 1 / s : 0;
        else { ld u = x / s; f = expl(-u + (L - 1) * logl(u) - lg) / s; }
        acc += g.w[i] * g.h[i] * f;
    }
    return acc;
}

// Smallest mean branch SNR with pe <= eps.
static ld gamma_ray(ld k, int L, ld eps) {
    const HGrid &g = hgrid(k, L);
    ld lo = logl(1e-6L), hi = logl(1e9L);
    if (pe_ray(g, expl(hi)) > eps) return INFINITY;
    for (int it = 0; it < 70; it++) {
        ld mid = 0.5L * (lo + hi);
        if (pe_ray(g, expl(mid)) > eps) lo = mid; else hi = mid;
    }
    return expl(hi);
}

struct RayOpt { int L; ld E, gam; int Llo, Lhi; };   // Llo..Lhi: range within 0.5 dB of the optimum

static RayOpt opt_ray(ld k, ld eps) {
    std::map<int, ld> ev;
    auto E = [&](int L) { auto it = ev.find(L); if (it != ev.end()) return it->second;
                          ld e = L * gamma_ray(k, L, eps); ev[L] = e; return e; };
    int best = 1; ld bestE = E(1);
    for (int L = 2; L < 100000; L += std::max(1, L / 12)) {
        ld e = E(L);
        if (e < bestE) { bestE = e; best = L; }
        if (L > 3 * best + 8) break;
    }
    int step = std::max(1, best / 12) + 1;
    for (int L = std::max(1, best - step); L <= best + step; L++) { ld e = E(L); if (e < bestE) { bestE = e; best = L; } }
    RayOpt r; r.L = best; r.E = bestE; r.gam = bestE / best;
    // range within 0.5 dB (coarse scan on the evaluated points plus geometric probes)
    r.Llo = best; r.Lhi = best;
    for (int L = best; L >= 1; L -= std::max(1, L / 16)) { if (E(L) <= bestE * 1.1220L) r.Llo = L; else break; }
    for (int L = best; L < 100000; L += std::max(1, L / 16)) { if (E(L) <= bestE * 1.1220L) r.Lhi = L; else break; }
    return r;
}

// AWGN, L looks with independent unknown phases, square-law combining.
// S ~ Gamma(L + J, 1), J ~ Poisson(lambda), lambda = total E/N0.  G[j] = E[h(S) | J = j].
struct GTab { int L; std::vector<ld> G; };

static GTab make_G(const HGrid &g) {
    GTab t; t.L = g.L;
    const long n = (long)g.h.size();
    const ld xh = (n - 1) * g.dx;
    const long J = (long)ceill(xh + 14 * sqrtl(xh + 1) + 60);
    t.G.assign(J + 1, 0);
    std::vector<ld> lgam(J + g.L + 2);
    for (long a = 1; a < (long)lgam.size(); a++) lgam[a] = lgammal((ld)a);
    const int L = g.L;
    #pragma omp parallel
    {
        std::vector<ld> loc(J + 1, 0);
        #pragma omp for schedule(dynamic, 64) nowait
        for (long i = 0; i < n; i++) {
            const ld hi = g.h[i]; if (hi <= 0) continue;
            const ld x = i * g.dx, wh = g.w[i] * hi;
            if (i == 0) { if (L == 1) loc[0] += wh; continue; }
            const ld lx = logl(x), sp = 14 * sqrtl(x + 1) + 60;
            long a0 = std::max((long)L, (long)floorl(x + 1 - sp)), a1 = std::min((long)(L + J), (long)ceill(x + 1 + sp));
            for (long a = a0; a <= a1; a++) loc[a - L] += wh * expl(-x + (a - 1) * lx - lgam[a]);
        }
        #pragma omp critical
        for (long j = 0; j <= J; j++) t.G[j] += loc[j];
    }
    return t;
}

static ld pe_awgn(const GTab &t, ld lam) {
    const long J = (long)t.G.size() - 1;
    const ld sp = 14 * sqrtl(lam + 1) + 60, ll = logl(lam);
    long j0 = std::max(0L, (long)floorl(lam - sp)), j1 = std::min(J, (long)ceill(lam + sp));
    ld acc = 0;
    for (long j = j0; j <= j1; j++) acc += expl(-lam + j * ll - lgammal((ld)j + 1)) * t.G[j];
    return acc;
}

// Smallest total E/N0 with pe <= eps on AWGN with L square-law combined looks.
static ld E_awgn(ld k, int L, ld eps) {
    const GTab t = make_G(hgrid(k, L));
    ld lo = logl(1e-3L), hi = logl(1e5L);
    for (int it = 0; it < 70; it++) {
        ld mid = 0.5L * (lo + hi);
        if (pe_awgn(t, expl(mid)) > eps) lo = mid; else hi = mid;
    }
    return expl(hi);
}

// Gaussian tail and inverse.
static ld logQ(ld x) { return logl(0.5L * erfcl(x / sqrtl(2.0L))); }
static ld Qinv_log(ld logp) {
    ld lo = -40, hi = 60;
    for (int it = 0; it < 200; it++) { ld mid = 0.5L * (lo + hi); if (logQ(mid) > logp) lo = mid; else hi = mid; }
    return 0.5L * (lo + hi);
}
// Polyanskiy-Poor-Verdu converse (no feedback, any code, coherent): E/N0 >= (Qinv(1/M) + Qinv(eps))^2 / 2.
static ld E_ppv_converse(ld k, ld eps) {
    ld a = Qinv_log(-k * LN2) + Qinv_log(logl(eps));
    return a > 0 ? 0.5L * a * a : 0;
}
// Coherent M-ary orthogonal, exact: pe = Int phi(z - sqrt(2E)) [1 - Phi(z)^(M-1)] dz.
static ld pe_coh(ld k, ld E) {
    const ld Mm1 = powl(2.0L, k) - 1, mu = sqrtl(2 * E), dz = 0.004L;
    const long n = (long)((mu + 13 + 13) / dz) / 2 * 2;
    ld acc = 0;
    #pragma omp parallel for reduction(+:acc) schedule(static)
    for (long i = 0; i <= n; i++) {
        ld z = -13 + i * dz;
        ld Qz = 0.5L * erfcl(z / sqrtl(2.0L));
        ld lp = (Qz < 0.5L) ? log1pl(-Qz) : logl(1 - Qz);
        ld h = -expm1l(Mm1 * lp);
        ld f = expl(-0.5L * (z - mu) * (z - mu)) / sqrtl(2 * M_PIl);
        acc += dz / 3 * ((i == 0 || i == n) ? 1 : (i % 2 ? 4 : 2)) * h * f;
    }
    return acc;
}
static ld E_coh(ld k, ld eps) {
    ld lo = logl(1e-3L), hi = logl(1e5L);
    for (int it = 0; it < 60; it++) { ld mid = 0.5L * (lo + hi); if (pe_coh(k, expl(mid)) > eps) lo = mid; else hi = mid; }
    return expl(hi);
}

// Binary noncoherent orthogonal FSK with L Rayleigh branches, closed form (validation).
static ld pe_bin_ray_closed(int L, ld gam) {
    ld p = 1 / (2 + gam), acc = 0, c = 1;   // c = C(L-1+j, j)
    for (int j = 0; j < L; j++) { if (j > 0) c = c * (L - 1 + j) / j; acc += c * powl(1 - p, j); }
    return powl(p, L) * acc;
}

// Scaled Bessel I0: exp(-x) I0(x).
static ld i0e(ld x) {
    if (x < 30) { ld s = 1, t = 1; for (int m = 1; m < 400; m++) { t *= (x / 2) * (x / 2) / ((ld)m * m); s += t; if (t < s * 1e-20L) break; } return s * expl(-x); }
    ld s = 1, t = 1; for (int m = 1; m < 30; m++) { t *= ((2 * m - 1) * (2 * m - 1)) / (8.0L * m * x); if (t < 1e-20L) break; s += t; }
    return s / sqrtl(2 * M_PIl * x);
}
// Bhattacharyya parameter of two orthogonal tones, noncoherent AWGN, soft (energy) outputs.
static ld D_awgn(ld lam) {
    const ld dy = 0.002L; ld acc = 0; const long n = (long)((lam + 20 * sqrtl(lam + 1) + 80) / dy) / 2 * 2;
    for (long i = 0; i <= n; i++) {
        ld y = i * dy, a = 2 * sqrtl(lam * y);
        // sqrt(f_nc f_c) = exp(-y - lam/2) sqrt(I0(a)) = exp(-y - lam/2 + a/2) sqrt(i0e(a))
        ld f = expl(-y - lam / 2 + a / 2) * sqrtl(i0e(a));
        acc += dy / 3 * ((i == 0 || i == n) ? 1 : (i % 2 ? 4 : 2)) * f;
    }
    return acc * acc;
}
static ld D_ray(ld gam) { return 4 * (1 + gam) / ((2 + gam) * (2 + gam)); }

int main() {
    const std::vector<ld> ks = {1, 5, 10, 16, 20, 30, 50, 77, 100, 200, 300};
    const std::vector<ld> epss = {1e-2L, 1e-3L, 1e-4L};

    printf("# Minimum energy per message for short messages with noncoherent orthogonal signalling\n\n");
    printf("Generated by `short_message_energy.cpp` (theoretical calculation; all values derived here).\n");
    printf("E/N0 is the received energy of the whole message over the one-sided noise density, as a plain ratio\n");
    printf("unless marked dB. M = 2^k equal-energy orthogonal codewords, maximum-statistic detection, known timing\n");
    printf("and frequency, Gaussian noise of known level, no interference. See the source header for the model limits.\n\n");

    // ---------- validation ----------
    printf("## 0. Validation\n\n");
    printf("| Check | Computed | Reference |\n|---|---:|---:|\n");
    printf("| AWGN noncoherent, k=1, eps=1e-3: E/N0 | %.4Lf | 2 ln(1/(2 eps)) = %.4Lf |\n", E_awgn(1, 1, 1e-3L), 2 * logl(500.0L));
    {
        // M = 4 closed form: pe = sum_{n=1}^{3} (-1)^{n+1} C(3,n)/(n+1) exp(-n E/(n+1))
        ld E = E_awgn(2, 1, 1e-3L);
        ld pc = 3.0L / 2 * expl(-E / 2) - 3.0L / 3 * expl(-2 * E / 3) + 1.0L / 4 * expl(-3 * E / 4);
        printf("| AWGN noncoherent, k=2: closed-form pe at the computed E/N0 = %.4Lf | %.6Le | 1e-3 |\n", E, pc);
    }
    {
        int L = 8; ld g = gamma_ray(1, L, 1e-3L);
        printf("| Rayleigh, k=1, L=8: closed-form pe at the computed branch SNR %.4Lf | %.6Le | 1e-3 |\n", g, pe_bin_ray_closed(L, g));
    }
    {
        RayOpt a = opt_ray(10, 1e-3L), b = opt_ray(100, 1e-3L), c = opt_ray(1000, 1e-3L);
        printf("| Rayleigh no-CSI, k=10, eps=1e-3: E/N0 (L*) | %.1Lf (%d) | Yang-Durisi-Polyanskiy 2016 Table I: 67 (N*=18) |\n", a.E, a.L);
        printf("| Rayleigh no-CSI, k=100, eps=1e-3: E/N0 (L*) | %.1Lf (%d) | 2.4e2 (N*=38) |\n", b.E, b.L);
        printf("| Rayleigh no-CSI, k=1000, eps=1e-3: E/N0 (L*) | %.0Lf (%d) | 1.3e3 (N*=96) |\n", c.E, c.L);
    }
    printf("| AWGN noncoherent Eb/N0, k=16, eps=1e-3 (dB) | %.2Lf | 2.97 (calc/results/finite_energy.md) |\n", dB(E_awgn(16, 1, 1e-3L) / (16 * LN2 / LN2)));
    printf("\n");

    // ---------- A/B: AWGN ----------
    std::map<std::pair<int, int>, ld> EA, ER;   // (k index, eps index)
    std::map<std::pair<int, int>, RayOpt> RO;
    printf("## 1. AWGN: energy per message, E/N0\n\n");
    printf("`nc` = noncoherent orthogonal, one unknown phase per message, exact. `coh` = coherent orthogonal, exact.\n");
    printf("`conv` = Polyanskiy-Poor-Verdu 2011 converse for any code without feedback (coherent, so also a lower\n");
    printf("bound for every noncoherent scheme). `ub` = the earlier union-bound illustration 2(k ln2 + ln(1/eps)).\n");
    printf("`exp` = orthogonal-signal exponent form: 2(k ln2 + ln(1/eps)) for k <= log2(1/eps), else (sqrt(k ln2) + sqrt(ln(1/eps)))^2.\n\n");
    for (size_t e = 0; e < epss.size(); e++) {
        ld eps = epss[e];
        printf("**eps = %.0Le** (log2(1/eps) = %.1Lf bits)\n\n", eps, -log2l(eps));
        printf("| k | nc E/N0 | nc Eb/N0 (dB) | nc / nc(k=1) | coh E/N0 | conv E/N0 | nc - conv (dB) | ub | exp |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        ld E1 = 0;
        for (size_t i = 0; i < ks.size(); i++) {
            ld k = ks[i], En = E_awgn(k, 1, eps), Ec = E_coh(k, eps), Ev = E_ppv_converse(k, eps);
            if (i == 0) E1 = En;
            EA[{(int)i, (int)e}] = En;
            ld ub = 2 * (k * LN2 + logl(1 / eps));
            ld ex = (k <= -log2l(eps)) ? ub : powl(sqrtl(k * LN2) + sqrtl(logl(1 / eps)), 2);
            printf("| %.0Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.1Lf | %.1Lf |\n", k, En, dB(En / k), En / E1, Ec, Ev, dB(En / Ev), ub, ex);
        }
        printf("\n");
    }

    // ---------- C: Rayleigh ----------
    printf("## 2. Rayleigh fading, no channel knowledge, L independent branches (L optimised)\n\n");
    printf("Each codeword spreads its energy equally over L independently fading cells; square-law combining (ML).\n");
    printf("`L*` is the energy-optimal branch count, `branch SNR` = E/(L* N0), and `L within 0.5 dB` is the range of\n");
    printf("branch counts whose required energy is within 0.5 dB of the optimum.\n\n");
    for (size_t e = 0; e < epss.size(); e++) {
        ld eps = epss[e];
        printf("**eps = %.0Le**\n\n", eps);
        printf("| k | E/N0 | Eb/N0 (dB) | L* | branch SNR | branch SNR (dB) | L within 0.5 dB | / Rayleigh k=1 | / AWGN nc same k (dB) |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        ld E1 = 0;
        for (size_t i = 0; i < ks.size(); i++) {
            ld k = ks[i]; RayOpt r = opt_ray(k, eps);
            if (i == 0) E1 = r.E;
            ER[{(int)i, (int)e}] = r.E; RO[{(int)i, (int)e}] = r;
            printf("| %.0Lf | %.1Lf | %.2Lf | %d | %.2Lf | %.2Lf | %d-%d | %.2Lf | %.2Lf |\n", k, r.E, dB(r.E / k), r.L, r.gam, dB(r.gam), r.Llo, r.Lhi, r.E / E1, dB(r.E / EA[{(int)i, (int)e}]));
        }
        printf("\n");
    }
    printf("**Fixed branch count, eps = 1e-3: E/N0 in dB** (too few branches is a drastic loss; too many is a mild one)\n\n");
    {
        const std::vector<int> Ls = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024};
        printf("| k |"); for (int L : Ls) printf(" L=%d |", L); printf("\n|---:|"); for (size_t i = 0; i < Ls.size(); i++) printf("---:|"); printf("\n");
        for (ld k : {1.0L, 10.0L, 20.0L, 50.0L, 100.0L}) {
            printf("| %.0Lf |", k);
            for (int L : Ls) printf(" %.2Lf |", dB(L * gamma_ray(k, L, 1e-3L)));
            printf("\n");
        }
        printf("\n");
    }

    // ---------- D: ratios and marginal cost ----------
    printf("## 3. The 10-bit premise: ratios and marginal cost per added bit\n\n");
    printf("| Channel | eps | E(1) | E(5) | E(10) | E(20) | E(10)/E(1) | E(20)/E(10) | E(100)/E(10) | k where E(k) = 2 E(1) |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int ch = 0; ch < 2; ch++) for (size_t e = 0; e < epss.size(); e++) {
        auto &T = ch ? ER : EA;
        ld eps = epss[e];
        // k where E(k) = 2 E(1): bisection on real k
        auto Ek = [&](ld k) { return ch ? opt_ray(k, eps).E : E_awgn(k, 1, eps); };
        ld lo = 1, hi = 60, tgt = 2 * T[{0, (int)e}];
        for (int it = 0; it < 18; it++) { ld mid = 0.5L * (lo + hi); if (Ek(mid) < tgt) lo = mid; else hi = mid; }
        printf("| %s | %.0Le | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.2Lf | %.1Lf |\n", ch ? "Rayleigh, L*" : "AWGN nc", eps,
               T[{0, (int)e}], T[{1, (int)e}], T[{2, (int)e}], T[{4, (int)e}], T[{2, (int)e}] / T[{0, (int)e}], T[{4, (int)e}] / T[{2, (int)e}], T[{8, (int)e}] / T[{2, (int)e}], 0.5L * (lo + hi));
    }
    printf("\n**Marginal energy per added bit, (E(k2) - E(k1))/(k2 - k1), in units of N0** (ln 2 = 0.693 is the capacity slope; 2 ln 2 = 1.386 is the AWGN low-rate slope)\n\n");
    printf("| Channel | eps |"); for (size_t i = 1; i < ks.size(); i++) printf(" %.0Lf-%.0Lf |", ks[i - 1], ks[i]); printf("\n|---|---:|"); for (size_t i = 1; i < ks.size(); i++) printf("---:|"); printf("\n");
    for (int ch = 0; ch < 2; ch++) for (size_t e = 0; e < epss.size(); e++) {
        auto &T = ch ? ER : EA;
        printf("| %s | %.0Le |", ch ? "Rayleigh, L*" : "AWGN nc", epss[e]);
        for (size_t i = 1; i < ks.size(); i++) printf(" %.2Lf |", (T[{(int)i, (int)e}] - T[{(int)i - 1, (int)e}]) / (ks[i] - ks[i - 1]));
        printf("\n");
    }
    printf("\n");

    // ---------- E: sectioning ----------
    printf("## 4. Sectioning: s independently ML-decoded sections against one joint codebook\n\n");
    printf("A k-bit message is split into s equal sections, each an independent 2^(k/s)-ary orthogonal codebook with its\n");
    printf("own energy; the message is correct only if every section is, so each section must meet 1-(1-eps)^(1/s).\n");
    printf("No outer code. Loss = 10 log10(s E(k/s, eps_s) / E(k, eps)). eps = 1e-3.\n\n");
    printf("| k | sections s | bits per section | AWGN nc loss (dB) | Rayleigh L* loss (dB) | Rayleigh joint E/N0 | Rayleigh sectioned E/N0 |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (ld k : {20.0L, 50.0L, 77.0L, 100.0L, 200.0L, 300.0L}) {
        ld eps = 1e-3L, Ej = E_awgn(k, 1, eps); RayOpt rj = opt_ray(k, eps);
        int prev_s = -1;
        for (ld b : {10.0L, 16.0L, 20.0L, 25.0L}) {
            int s = (int)ceill(k / b - 1e-9L); if (s < 2 || s == prev_s) continue;
            prev_s = s;
            ld ks_ = k / s, es = 1 - powl(1 - eps, 1.0L / s);
            ld Es = s * E_awgn(ks_, 1, es); RayOpt rs = opt_ray(ks_, es);
            printf("| %.0Lf | %d | %.2Lf | %.2Lf | %.2Lf | %.1Lf | %.1Lf |\n", k, s, ks_, dB(Es / Ej), dB(s * rs.E / rj.E), rj.E, s * rs.E);
        }
    }
    printf("\n");

    // ---------- F: noncoherent combining loss on AWGN ----------
    printf("## 5. AWGN: noncoherent (square-law) combining loss against the number of looks\n\n");
    printf("The message energy is split equally over L looks with independent unknown phases and the looks are\n");
    printf("combined by summing energies. Total E/N0 in dB for eps = 1e-3, the per-look SNR E/(L N0), and the loss\n");
    printf("against a single look (L = 1).\n\n");
    {
        const std::vector<int> Ls = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024, 4096};
        for (ld k : {1.0L, 10.0L, 100.0L}) {
            printf("**k = %.0Lf**\n\n| L | total E/N0 (dB) | per-look SNR (dB) | loss vs L=1 (dB) |\n|---:|---:|---:|---:|\n", k);
            ld E0 = 0;
            for (int L : Ls) {
                ld E = E_awgn(k, L, 1e-3L); if (L == 1) E0 = E;
                printf("| %d | %.2Lf | %.2Lf | %.2Lf |\n", L, dB(E), dB(E / L), dB(E / E0));
            }
            printf("\n");
        }
    }

    // ---------- G: Rayleigh with capped branch SNR ----------
    printf("## 6. Rayleigh: energy when the per-branch SNR is capped (coherence-limited looks)\n\n");
    printf("If one coherence interval can hold only a branch SNR x = (C/N0) Tc, the message needs L(x) branches.\n");
    printf("E/N0 = L(x) x, eps = 1e-3; loss is against the optimum branch SNR of section 2.\n\n");
    printf("| k | branch SNR x (dB) | branches L(x) | E/N0 (dB) | Eb/N0 (dB) | loss vs optimum (dB) |\n|---:|---:|---:|---:|---:|---:|\n");
    for (ld k : {10.0L, 100.0L}) {
        RayOpt r0 = opt_ray(k, 1e-3L);
        for (ld xdb : {-10.0L, -6.0L, -3.0L, 0.0L, 3.0L, 6.0L, 10.0L, 13.0L, 20.0L}) {
            ld x = powl(10, xdb / 10);
            // smallest L with pe <= eps at branch SNR x
            long lo = 0, hi = 1;
            while (pe_ray(hgrid(k, (int)hi), x) > 1e-3L) { lo = hi; hi *= 2; if (hi > 4000000) break; }
            while (hi - lo > 1) { long mid = (lo + hi) / 2; if (pe_ray(hgrid(k, (int)mid), x) > 1e-3L) lo = mid; else hi = mid; }
            printf("| %.0Lf | %.0Lf | %ld | %.2Lf | %.2Lf | %.2Lf |\n", k, xdb, hi, dB(hi * x), dB(hi * x / k), dB(hi * x / r0.E));
        }
    }
    printf("\n");

    // ---------- H: margin against erasures ----------
    printf("## 7. Margin against erasures with a fixed branch count\n\n");
    printf("A design closes exactly (eps = 1e-3) with L0 branches at branch SNR g0 (no margin). The link is then\n");
    printf("operated X dB stronger on every surviving branch, and a known set of branches is erased. Entries are the\n");
    printf("largest erased fraction 1 - L'/L0 that still meets eps, L' being the smallest surviving count that does.\n");
    printf("`ideal` is 1 - 10^(-X/10), which holds when every look counts linearly in energy; `quad` is\n");
    printf("1 - 10^(-2X/10), the weak-look limit in which information per look grows as the square of its SNR.\n\n");
    {
        const std::vector<ld> Xs = {3, 6, 10, 13, 16, 20};
        printf("| Design (eps 1e-3) | L0 | g0 (dB) |"); for (ld X : Xs) printf(" X=%.0Lf dB |", X); printf("\n|---|---:|---:|"); for (size_t i = 0; i < Xs.size(); i++) printf("---:|"); printf("\n");
        printf("| ideal | | |"); for (ld X : Xs) printf(" %.3Lf |", 1 - powl(10, -X / 10)); printf("\n");
        printf("| quad | | |"); for (ld X : Xs) printf(" %.4Lf |", 1 - powl(10, -2 * X / 10)); printf("\n");
        for (ld k : {10.0L, 100.0L}) {
            RayOpt r0 = opt_ray(k, 1e-3L);
            // designs: optimum, and coherence-limited branch SNRs
            std::vector<std::pair<int, ld>> designs; designs.push_back({r0.L, r0.gam});
            for (ld xdb : {0.0L, -6.0L, -10.0L}) {
                ld x = powl(10, xdb / 10); long lo = 0, hi = 1;
                while (pe_ray(hgrid(k, (int)hi), x) > 1e-3L) { lo = hi; hi *= 2; }
                while (hi - lo > 1) { long mid = (lo + hi) / 2; if (pe_ray(hgrid(k, (int)mid), x) > 1e-3L) lo = mid; else hi = mid; }
                designs.push_back({(int)hi, x});
            }
            for (auto &d : designs) {
                printf("| Rayleigh, k=%.0Lf | %d | %.1Lf |", k, d.first, dB(d.second));
                for (ld X : Xs) {
                    ld g = d.second * powl(10, X / 10); long lo = 0, hi = d.first;
                    while (hi - lo > 1) { long mid = (lo + hi) / 2; if (pe_ray(hgrid(k, (int)mid), g) > 1e-3L) lo = mid; else hi = mid; }
                    printf(" %.3Lf (L'=%ld) |", 1 - (ld)hi / d.first, hi);
                }
                printf("\n");
            }
        }
        // AWGN looks
        for (ld k : {10.0L, 100.0L}) for (int L0 : {64, 1024}) {
            ld E0 = E_awgn(k, L0, 1e-3L), g0 = E0 / L0;
            printf("| AWGN looks, k=%.0Lf | %d | %.1Lf |", k, L0, dB(g0));
            for (ld X : Xs) {
                ld g = g0 * powl(10, X / 10); long lo = 0, hi = L0;
                while (hi - lo > 1) {
                    long mid = (lo + hi) / 2;
                    GTab t = make_G(hgrid(k, (int)mid));
                    if (pe_awgn(t, mid * g) > 1e-3L) lo = mid; else hi = mid;
                }
                printf(" %.3Lf (L'=%ld) |", 1 - (ld)hi / L0, hi);
            }
            printf("\n");
        }
        printf("\n");
        printf("**The converse design: margin spent on extra branches.** L0 and g0 are the no-margin optimum; the design\n");
        printf("transmits n = ceil(L0 10^(X/10)) branches at g0, so any n - L0 known erasures are tolerated (the `ideal`\n");
        printf("fraction by construction). `path-loss tolerance` is how many dB every branch may weaken, with no erasures,\n");
        printf("before eps = 1e-3 is missed.\n\n");
        printf("| k | L0 | g0 (dB) |"); for (ld X : Xs) printf(" X=%.0Lf dB |", X); printf("\n|---:|---:|---:|"); for (size_t i = 0; i < Xs.size(); i++) printf("---:|"); printf("\n");
        for (ld k : {10.0L, 100.0L}) {
            RayOpt r0 = opt_ray(k, 1e-3L);
            printf("| %.0Lf | %d | %.1Lf |", k, r0.L, dB(r0.gam));
            for (ld X : Xs) {
                int n = (int)ceill(r0.L * powl(10, X / 10));
                ld g = gamma_ray(k, n, 1e-3L);
                printf(" n=%d: %.1Lf dB |", n, dB(r0.gam / g));
            }
            printf("\n");
        }
        printf("\n");
    }

    // ---------- H2: random (keyed-interleaver) erasures ----------
    printf("**Random erasures.** With a keyed interleaver an interferer that covers a fraction p of the plane erases\n");
    printf("each branch independently with probability p, so the surviving count fluctuates. Entries are the largest\n");
    printf("p for which the message error, averaged over the binomial number of survivors, stays at or below 1e-3,\n");
    printf("for the fixed-branch-count designs above operated X dB stronger. Erasure positions are known.\n\n");
    {
        const std::vector<ld> Xs = {3, 6, 10, 13, 16, 20};
        printf("| Design (eps 1e-3) | L0 | g0 (dB) |"); for (ld X : Xs) printf(" X=%.0Lf dB |", X); printf("\n|---|---:|---:|"); for (size_t i = 0; i < Xs.size(); i++) printf("---:|"); printf("\n");
        printf("| ideal 1 - 10^(-X/10) | | |"); for (ld X : Xs) printf(" %.3Lf |", 1 - powl(10, -X / 10)); printf("\n");
        for (ld k : {10.0L, 100.0L}) {
            RayOpt r0 = opt_ray(k, 1e-3L);
            std::vector<std::pair<int, ld>> designs; designs.push_back({r0.L, r0.gam});
            for (ld xdb : {0.0L, -6.0L}) {
                ld x = powl(10, xdb / 10); long lo = 0, hi = 1;
                while (pe_ray(hgrid(k, (int)hi), x) > 1e-3L) { lo = hi; hi *= 2; }
                while (hi - lo > 1) { long mid = (lo + hi) / 2; if (pe_ray(hgrid(k, (int)mid), x) > 1e-3L) lo = mid; else hi = mid; }
                designs.push_back({(int)hi, x});
            }
            for (auto &d : designs) {
                const int L0 = d.first;
                std::vector<std::vector<ld>> pe(Xs.size(), std::vector<ld>(L0 + 1, 0));
                for (size_t xi = 0; xi < Xs.size(); xi++) pe[xi][0] = 1;
                for (int Lp = 1; Lp <= L0; Lp++) {
                    HGrid g = make_h(k, Lp);
                    for (size_t xi = 0; xi < Xs.size(); xi++) pe[xi][Lp] = pe_ray(g, d.second * powl(10, Xs[xi] / 10));
                    if (pe[0][Lp] < 1e-13L) break;
                }
                printf("| Rayleigh, k=%.0Lf | %d | %.1Lf |", k, L0, dB(d.second));
                for (size_t xi = 0; xi < Xs.size(); xi++) {
                    auto tot = [&](ld p) {
                        if (p <= 0) return pe[xi][L0];
                        ld acc = 0, lp = logl(p), lq = log1pl(-p);
                        for (int j = 0; j <= L0; j++) {   // j erased
                            ld v = pe[xi][L0 - j]; if (v <= 0) continue;
                            acc += v * expl(lgammal((ld)L0 + 1) - lgammal((ld)j + 1) - lgammal((ld)(L0 - j) + 1) + j * lp + (L0 - j) * lq);
                        }
                        return acc;
                    };
                    ld lo = 0, hi = 1 - 1e-12L;
                    for (int it = 0; it < 50; it++) { ld mid = 0.5L * (lo + hi); if (tot(mid) > 1e-3L) hi = mid; else lo = mid; }
                    printf(" %.3Lf |", lo);
                }
                printf("\n"); fflush(stdout);
            }
        }
        printf("\n");
    }

    // ---------- quiet-slot test ----------
    printf("## 8. Quiet-slot (end-of-message) test with the maximum codeword score\n\n");
    printf("One calendar slot carries one k-bit orthogonal section over L Rayleigh branches. The receiver declares the\n");
    printf("slot occupied when the largest of the 2^k codeword scores exceeds a threshold. `false continuation` is\n");
    printf("the probability that a noise-only slot is declared occupied; `false termination` is the probability that\n");
    printf("an occupied slot is declared quiet (its own codeword score falls below the threshold; a decoding error\n");
    printf("is counted separately). L and the no-margin branch SNR are the section-2 optimum for eps = 1e-3; X is the\n");
    printf("margin by which every branch is stronger. k = 0 is a known keyed pattern (one hypothesis) with the same\n");
    printf("L and branch SNR as the k = 10 design.\n\n");
    {
        const std::vector<ld> pfas = {1e-2L, 1e-3L, 1e-4L, 1e-6L};
        printf("| k | L | X (dB) |"); for (ld pf : pfas) printf(" false termination at false continuation %.0Le |", pf); printf(" equal-error point |\n|---:|---:|---:|"); for (size_t i = 0; i <= pfas.size(); i++) printf("---:|"); printf("\n");
        RayOpt r10 = opt_ray(10, 1e-3L);
        for (ld k : {0.0L, 10.0L, 20.0L, 25.0L}) {
            RayOpt r0 = (k == 0) ? r10 : opt_ray(k, 1e-3L);
            const int L = r0.L; const ld M = powl(2.0L, k);
            auto pfa = [&](ld tau) { ld P, Q; incgam(L, tau, P, Q); ld lp = (Q < 0.5L) ? log1pl(-Q) : logl(P); return -expm1l(M * lp); };
            auto tau_for = [&](ld pf) { ld lo = 0, hi = 1e5L; for (int it = 0; it < 200; it++) { ld mid = 0.5L * (lo + hi); if (pfa(mid) > pf) lo = mid; else hi = mid; } return hi; };
            for (ld X : {0.0L, 3.0L, 6.0L, 10.0L}) {
                ld g = r0.gam * powl(10, X / 10);
                auto pmiss = [&](ld tau) { ld P, Q; incgam(L, tau / (1 + g), P, Q); return P; };
                printf("| %.0Lf | %d | %.0Lf |", k, L, X);
                for (ld pf : pfas) printf(" %.1Le |", pmiss(tau_for(pf)));
                ld lo = 0, hi = 1e5L; for (int it = 0; it < 200; it++) { ld mid = 0.5L * (lo + hi); if (pfa(mid) > pmiss(mid)) lo = mid; else hi = mid; }
                printf(" %.1Le |\n", pmiss(hi));
            }
        }
        printf("\n");
    }

    // ---------- reference energies for codebook_mc.cpp ----------
    printf("## 9. Reference energies used by `codebook_mc.cpp`\n\n");
    printf("| k | L | eps | E/N0 | E/N0 (dB) | branch SNR |\n|---:|---:|---:|---:|---:|---:|\n");
    {
        struct R { ld k; int L; ld eps; };
        for (R r : {R{10, 18, 1e-3L}, R{10, 9, 1e-3L}, R{10, 36, 1e-3L}, R{10, 72, 1e-3L}, R{16, 14, 1e-2L}, R{16, 28, 1e-2L}}) {
            ld g = gamma_ray(r.k, r.L, r.eps);
            printf("| %.0Lf | %d | %.0Le | %.2Lf | %.2Lf | %.3Lf |\n", r.k, r.L, r.eps, r.L * g, dB(r.L * g), g);
        }
        printf("\n");
    }

    // ---------- I: cutoff rate ----------
    printf("## 10. Cutoff rate R0 of q-ary noncoherent orthogonal symbols with soft (energy) outputs\n\n");
    printf("R0 = log2 q - log2(1 + (q-1) D) bit/symbol, D the Bhattacharyya parameter of two tones. Minimum Eb/N0 =\n");
    printf("min over Es/N0 of (Es/N0)/R0, with the optimum symbol SNR and the code rate R0/log2 q there. Rayleigh:\n");
    printf("one independent fade per symbol, D = 4(1+g)/(2+g)^2.\n\n");
    printf("| q | AWGN nc min Eb/N0 (dB) | Es/N0 (dB) | rate | Rayleigh min Eb/N0 (dB) | Es/N0 (dB) | rate |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int q : {2, 4, 8, 16, 32, 64, 128, 256, 1024, 4096}) {
        ld best[2] = {1e30L, 1e30L}, bs[2] = {0, 0}, br[2] = {0, 0};
        for (ld sdb = -3; sdb <= 20.001L; sdb += 0.05L) {
            ld s = powl(10, sdb / 10);
            for (int ch = 0; ch < 2; ch++) {
                ld D = ch ? D_ray(s) : D_awgn(s);
                ld R0 = log2l((ld)q) - log2l(1 + (q - 1) * D);
                if (R0 <= 0) continue;
                ld eb = s / R0;
                if (eb < best[ch]) { best[ch] = eb; bs[ch] = sdb; br[ch] = R0 / log2l((ld)q); }
            }
        }
        printf("| %d | %.2Lf | %.1Lf | %.2Lf | %.2Lf | %.1Lf | %.2Lf |\n", q, dB(best[0]), bs[0], br[0], dB(best[1]), bs[1], br[1]);
    }
    printf("\n");
    return 0;
}
