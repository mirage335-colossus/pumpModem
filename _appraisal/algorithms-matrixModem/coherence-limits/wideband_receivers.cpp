// Wideband (large-alphabet) achievable rates of practical FFT tone receivers versus the
// peak-limited noncoherent bound, for stationary fading with arbitrary Doppler spectra.
//
// Signal model (complex baseband, N0 = 1, received peak = average power P = C/N0 in Hz):
// one constant-envelope tone per symbol of dwell T, chosen from a very large orthogonal set.
// A receiver forms a quadratic statistic per tone; the per-symbol generalized mutual
// information (GMI, achievable with that receiver's metric under ideal interleaving) in the
// M -> infinity limit is
//      GMI_inf = sup_s { s E_1[L] - ln E_0[exp(s L)] }   [nat/symbol]
// where E_1 is under the transmitted tone and E_0 noise only. For L = sum_k q_k |Z_k|^2 with
// orthonormal DFT bins Z_k (noise i.i.d. CN(0,1)) this is exact and closed form:
//      GMI_inf(s) = sum_k [ s q_k (1 + lam_k) + ln(1 - s q_k) ]
// with lam_k = E|signal part of Z_k|^2 = P T Integral S(f) sinc^2((f - f_k) T) df, computed
// exactly from the fading autocorrelation. It depends only on second-order statistics, so it
// holds for Rician/specular or other non-Gaussian fading with the same autocorrelation.
//
// Receivers compared:
//   WP(model)   weighted periodogram over the dwell T, q_k = lm_k/(1+lm_k) from a model spectrum
//               (the Szego-asymptotic estimator-correlator; model = truth is "matched").
//   EC exact    exact Karhunen-Loeve estimator-correlator at dwell T (log-det of Toeplitz
//               covariance via Levinson), reference only.
//   INC(Ts,B)   FT8-like segment energies: segments of Ts, sum of |bin|^2 over bins |b|<=B,
//               incoherent across segments.
//   COH(T)      single DFT bin over the whole dwell (fully coherent).
// The tone receiver assumes timing and the composite Doppler centre are known (search cost
// is treated elsewhere). Delays shorter than the dwell only rotate path phases for a tone
// (ISI from the previous symbol is ignored; it is at most tau/T of the energy).
//
// Build: g++ -O2 -std=c++20 -fopenmp wideband_receivers.cpp -o wideband_receivers
// Run:   ./wideband_receivers > results/wideband_receivers.md
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;
static const double LN2 = 0.69314718055994530942;
static double db(double x) { return 10 * std::log10(x); }
static double undb(double x) { return std::pow(10.0, x / 10); }

// ---------------- channel spectra ----------------
struct Comp {
    enum Kind { LINE, GAUSS, FLAT, JAKES, LORENTZ } kind;
    double w;   // power fraction
    double c;   // Doppler centre (Hz)
    double s;   // GAUSS sigma, FLAT half width, JAKES fd, LORENTZ gamma
};
using Chan = std::vector<Comp>;

static cd rho(const Chan& ch, double tau) {  // E[h(t+tau) h*(t)]
    cd r = 0;
    for (auto& k : ch) {
        cd ph = std::polar(1.0, 2 * PI * k.c * tau);
        double a = 0;
        switch (k.kind) {
            case Comp::LINE: a = 1; break;
            case Comp::GAUSS: a = std::exp(-2 * PI * PI * k.s * k.s * tau * tau); break;
            case Comp::FLAT: { double x = 2 * PI * k.s * tau; a = std::fabs(x) < 1e-12 ? 1 : std::sin(x) / x; } break;
            case Comp::JAKES: a = std::cyl_bessel_j(0.0, 2 * PI * k.s * std::fabs(tau)); break;
            case Comp::LORENTZ: a = std::exp(-2 * PI * k.s * std::fabs(tau)); break;
        }
        r += k.w * a * ph;
    }
    return r;
}
static double maxfreq(const Chan& ch) {
    double m = 0;
    for (auto& k : ch) {
        double e = 0;
        switch (k.kind) {
            case Comp::LINE: e = 0; break;
            case Comp::GAUSS: e = 8 * k.s; break;
            case Comp::FLAT: case Comp::JAKES: e = k.s; break;
            case Comp::LORENTZ: e = 200 * k.s; break;
        }
        m = std::max(m, std::fabs(k.c) + e);
    }
    return m;
}

// ---------------- FFT ----------------
static void fft(std::vector<cd>& a, bool inv) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * PI / len * (inv ? 1 : -1);
        cd wl = std::polar(1.0, ang);
        for (size_t i = 0; i < n; i += len) {
            cd w = 1;
            for (size_t j = 0; j < len / 2; ++j) {
                cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Expected signal energy in DFT bins f_k = k/T (k = -K..K) of a window of length T:
// lam_k = P * Integral_{-T}^{T} (1-|tau|/T) rho(tau) exp(-j 2 pi f_k tau) dtau.
// Returned as vector indexed k+K. Uses an FFT of the windowed autocorrelation sampled at dt.
static std::vector<double> bin_energy(const Chan& ch, double P, double T, int K) {
    double fmax = std::max(maxfreq(ch), (K + 2) / T);
    // need dt < 1/(2 fmax) with margin; bins are k/T so N = T/dt; choose power of two
    int N = 1;
    while (N / T < 4 * fmax + 8 / T) N <<= 1;
    N = std::max(N, 2 * (K + 2));
    if (N < 64) N = 64;
    double dt = T / N;
    int L = 2 * N;
    std::vector<cd> c(L, 0.0);
    for (int n = -(N - 1); n <= N - 1; ++n) {
        double tau = n * dt;
        cd v = dt * (1 - std::fabs(tau) / T) * rho(ch, tau);
        c[(n + L) % L] += v;
    }
    fft(c, false);  // c[m] = sum_n v_n e^{-j 2 pi m n / L}, frequency m/(L dt) = m/(2T)
    std::vector<double> lam(2 * K + 1);
    for (int k = -K; k <= K; ++k) {
        int m = (2 * k + L) % L;
        lam[k + K] = std::max(0.0, P * c[m].real());
    }
    return lam;
}

// sup over s of sum_k [ s q_k (1+lam_k) + ln(1 - s q_k) ]   (concave in s)
static double gmi_wp(const std::vector<double>& q, const std::vector<double>& lam) {
    double qmax = 0;
    for (double x : q) qmax = std::max(qmax, x);
    if (qmax <= 0) return 0;
    auto f = [&](double s) {
        double v = 0;
        for (size_t i = 0; i < q.size(); ++i) if (q[i] > 0) v += s * q[i] * (1 + lam[i]) + std::log1p(-s * q[i]);
        return v;
    };
    double lo = 0, hi = (1 - 1e-12) / qmax;
    for (int it = 0; it < 200; ++it) {  // golden section
        double m1 = lo + (hi - lo) * 0.381966, m2 = lo + (hi - lo) * 0.618034;
        if (f(m1) < f(m2)) lo = m1; else hi = m2;
    }
    return std::max(0.0, f(0.5 * (lo + hi)));
}

// Rate (nat/s) of weighted-periodogram receiver with dwell T, model channel `mod`, truth `tru`.
static double rate_wp(const Chan& tru, const Chan& mod, double P, double T) {
    double ext = std::max(maxfreq(tru), maxfreq(mod));
    int K = (int)std::ceil((ext + 6.0 / T) * T) + 2;
    K = std::min(K, 200000);
    auto lt = bin_energy(tru, P, T, K), lm = bin_energy(mod, P, T, K);
    std::vector<double> q(lt.size());
    for (size_t i = 0; i < q.size(); ++i) q[i] = lm[i] / (1 + lm[i]);
    return gmi_wp(q, lt) / T;
}
// INC: segments Ts, bins |b|<=B, energy summed. E_c per segment; rate = (Ec - n ln(1+Ec/n))/Ts.
// The segment window is centred at the frequency offset (searched on a 1/(8 Ts) grid) that
// captures the most energy, as a receiver tracking the strongest mode would. The energy of a
// Ts-window bin at every offset on that grid comes from one zero-padded FFT.
static std::vector<double> energy_curve(const Chan& ch, double P, double T, int os) {
    double fmax = maxfreq(ch) + 8 / T;
    int N = 64;
    while (N / T < 4 * fmax) N <<= 1;
    double dt = T / N;
    int L = os * N;
    std::vector<cd> c(L, 0.0);
    for (int n = -(N - 1); n <= N - 1; ++n) {
        double tau = n * dt;
        c[(n + L) % L] += dt * (1 - std::fabs(tau) / T) * rho(ch, tau);
    }
    fft(c, false);  // frequency index m -> m/(os T)
    std::vector<double> e(L);
    for (int m = 0; m < L; ++m) e[m] = std::max(0.0, P * c[m].real());
    return e;
}
static double rate_inc(const Chan& tru, double P, double Ts, int B) {
    const int os = 8;
    auto e = energy_curve(tru, P, Ts, os);
    int L = (int)e.size(), n = 2 * B + 1;
    double ext = maxfreq(tru), best = 0;
    int J = std::min(L / 2 - 1, (int)std::ceil(ext * os * Ts) + os);
    for (int j = -J; j <= J; ++j) {
        double Ec = 0;
        for (int b = -B; b <= B; ++b) Ec += e[((j + b * os) % L + L) % L];
        best = std::max(best, (Ec - n * std::log1p(Ec / n)) / Ts);
    }
    return best;
}
static double rate_coh(const Chan& tru, double P, double T) { return rate_inc(tru, P, T, 0); }

// Exact KL estimator-correlator at dwell T: P - (1/T) ln det(I + K), K Hermitian Toeplitz on a
// fine grid (dt << coherence). Complex Levinson-Durbin for log det.
static double rate_ec_exact(const Chan& ch, double P, double T) {
    double fm = maxfreq(ch);
    double dt = std::min(T / 16, 1 / (12 * std::max(fm, 1e-3)));
    int N = (int)std::ceil(T / dt);
    if (N > 6000) return NAN;
    dt = T / N;
    std::vector<cd> r(N);
    for (int n = 0; n < N; ++n) r[n] = P * dt * rho(ch, n * dt);
    r[0] += 1.0;
    // Levinson for Hermitian Toeplitz with first column r: prediction error powers
    double logdet = std::log(r[0].real());
    std::vector<cd> a(N, 0.0), tmp(N);
    double E = r[0].real();
    a[0] = 1;
    for (int m = 1; m < N; ++m) {
        cd acc = 0;
        for (int i = 0; i < m; ++i) acc += a[i] * r[m - i];
        cd k = -acc / E;
        // a_new[i] = a[i] + k conj(a[m-i]) for i=1..m, a_new[0] = 1
        tmp[0] = 1;
        for (int i = 1; i <= m; ++i) tmp[i] = (i < m ? a[i] : cd(0)) + k * std::conj(a[m - i]);
        for (int i = 0; i <= m; ++i) a[i] = tmp[i];
        E *= (1 - std::norm(k));
        logdet += std::log(E);
    }
    return (P * T - logdet) / T;
}

// The peak-limited bound for flat fading: P - Integral ln(1+P S_ac). Gaussian/flat/line mixtures.
static double bound_rate(const Chan& ch, double P) {
    // numeric integration on a fine grid covering all components
    double lo = 1e300, hi = -1e300; bool any = false;
    for (auto& k : ch) {
        if (k.kind == Comp::LINE) continue;
        any = true;
        double e = (k.kind == Comp::GAUSS) ? 14 * k.s : (k.kind == Comp::LORENTZ ? 2e4 * k.s : k.s);
        lo = std::min(lo, k.c - e); hi = std::max(hi, k.c + e);
    }
    if (!any) return P;
    auto dens = [&](double f) {
        double s = 0;
        for (auto& k : ch) {
            double u = f - k.c;
            switch (k.kind) {
                case Comp::LINE: break;
                case Comp::GAUSS: s += k.w / (std::sqrt(2 * PI) * k.s) * std::exp(-0.5 * u * u / (k.s * k.s)); break;
                case Comp::FLAT: if (std::fabs(u) < k.s) s += k.w / (2 * k.s); break;
                case Comp::JAKES: if (std::fabs(u) < k.s) s += k.w / (PI * k.s * std::sqrt(std::max(1e-300, 1 - (u / k.s) * (u / k.s)))); break;
                case Comp::LORENTZ: s += k.w * (k.s / PI) / (k.s * k.s + u * u); break;
            }
        }
        return s;
    };
    // composite Simpson with many points; adequate for Gaussian/flat mixtures (not Jakes edges)
    double minscale = 1e300;
    for (auto& k : ch) if (k.kind != Comp::LINE) minscale = std::min(minscale, k.s);
    int n = (int)std::min(4e6, std::max(2e4, (hi - lo) / minscale * 200));
    if (n % 2) ++n;
    double h = (hi - lo) / n, sum = 0;
    for (auto& k : ch)  // Lorentzian tails beyond the grid contribute ~ P * tail mass
        if (k.kind == Comp::LORENTZ) sum += 3.0 / h * P * k.w * (1 - (2 / PI) * std::atan(2e4));
    for (int i = 0; i <= n; ++i) {
        double w = (i == 0 || i == n) ? 1 : (i % 2 ? 4 : 2);
        sum += w * std::log1p(P * dens(lo + i * h));
    }
    return P - sum * h / 3;
}

static Chan G(double two_sigma, double w = 1, double c = 0) { return {{Comp::GAUSS, w, c, two_sigma / 2}}; }
static Chan operator+(Chan a, const Chan& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static Chan scaled(Chan a, double f) { for (auto& k : a) k.w *= f; return a; }

struct NamedChan { std::string name; Chan ch; };

int main() {
    std::vector<NamedChan> chans = {
        {"Stable phase (line)", {{Comp::LINE, 1, 0, 0}}},
        {"Gaussian 2σ=0.1 Hz (F.1487 quiet)", G(0.1)},
        {"Gaussian 0.5 Hz (moderate)", G(0.5)},
        {"Gaussian 1 Hz (disturbed)", G(1)},
        {"Gaussian 3 Hz", G(3)},
        {"Gaussian 10 Hz (low-lat disturbed / high-lat moderate)", G(10)},
        {"Gaussian 30 Hz (high-lat disturbed)", G(30)},
        {"Two modes 0.5 Hz each at ±1 Hz", G(0.5, 0.5, -1) + G(0.5, 0.5, 1)},
        {"Two modes 1 Hz each at ±5 Hz", G(1, 0.5, -5) + G(1, 0.5, 5)},
        {"Rician K=1, diffuse 1 Hz", Chan{{Comp::LINE, 0.5, 0, 0}} + G(1, 0.5)},
        {"Rician K=0.3, diffuse 10 Hz", Chan{{Comp::LINE, 0.3 / 1.3, 0, 0}} + G(10, 1 / 1.3)},
        {"30% 0.05 Hz + 70% 10 Hz", G(0.05, 0.3) + G(10, 0.7)},
        {"Lorentzian (Gauss-Markov), 80% width = 10 Hz Gaussian", {{Comp::LORENTZ, 1, 0, 0.2082 * 10}}},
        {"Auroral: 50 Hz at +20 Hz offset", G(50, 1, 20)},
    };
    std::vector<double> cn0s = {-10, 0, 10};
    std::vector<double> dwells = {0.04, 0.08, 0.16, 0.32, 0.64, 1.28, 2.56, 5.12, 10.24, 20.48, 40.96, 81.92};

    printf("# Wideband achievable rates of FFT tone receivers versus the peak-limited bound\n\n");
    printf("Generated by `wideband_receivers.cpp`. Exact closed-form GMI of each quadratic receiver in the\n");
    printf("large-alphabet limit (ideal interleaving, known timing and Doppler centre). Entries are the extra\n");
    printf("Eb/N0 (dB) relative to the bound at the same received C/N0, i.e. 10·log10(R_bound/R_receiver):\n");
    printf("the factor of additional transmit time (and energy) at the same transmit power.\n\n");

    // ---- Section 1: matched WP vs dwell, and exact EC reference
    printf("## 1. Weighted periodogram (spread-matched) versus dwell T\n\n");
    printf("Gap (dB) of the matched weighted-periodogram receiver as a function of dwell; `EC` is the exact\n");
    printf("Karhunen–Loève estimator-correlator at the best listed dwell (blank where the grid is too large).\n\n");
    for (double c : cn0s) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Channel | bound Eb/N0 (dB) |", c);
        for (double T : dwells) printf(" T=%g s |", T);
        printf(" best WP | EC at best T |\n|---|---:|");
        for (size_t i = 0; i < dwells.size(); ++i) printf("---:|");
        printf("---:|---:|\n");
        for (auto& nc : chans) {
            double Rb = bound_rate(nc.ch, P);
            printf("| %s | %.2f |", nc.name.c_str(), db(P * LN2 / Rb));
            double best = 0, bestT = 0;
            for (double T : dwells) {
                double R = rate_wp(nc.ch, nc.ch, P, T);
                if (R > best) { best = R; bestT = T; }
                printf(" %.2f |", db(Rb / R));
            }
            double rec = rate_ec_exact(nc.ch, P, bestT);
            printf(" %.2f (%g s) |", db(Rb / best), bestT);
            if (std::isnan(rec)) printf("  |\n"); else printf(" %.2f |\n", db(Rb / rec));
        }
        printf("\n");
    }

    // ---- Section 2: simple receivers
    printf("## 2. Simple receivers: FT8-like single bin, multi-bin segments, fully coherent\n\n");
    printf("`INC(Ts,B)` sums |bin|² over bins |b|≤B of each Ts segment and combines segments incoherently.\n");
    printf("In the large-alphabet limit its rate does not depend on the dwell (only on Ts and B); the window is centred on\n");
    printf("the best Doppler offset (as a receiver locking to the strongest mode would). `best INC` is\n");
    printf("optimized over Ts ∈ {5 ms … 41 s} and B ∈ {0,1,2,4,8}. `COH best` optimizes the dwell of a single bin.\n\n");
    std::vector<double> tss = {0.005, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64, 1.28, 2.56, 5.12, 10.24, 20.48, 40.96};
    for (double c : cn0s) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Channel | INC(160 ms,0) FT8-like | INC(160 ms,1) | best INC (Ts, B) | COH best (T) | best WP |\n|---|---:|---:|---:|---:|---:|\n", c);
        for (auto& nc : chans) {
            double Rb = bound_rate(nc.ch, P);
            double r0 = rate_inc(nc.ch, P, 0.16, 0), r1 = rate_inc(nc.ch, P, 0.16, 1);
            double bi = 0, bts = 0; int bB = 0;
            for (double ts : tss) for (int B : {0, 1, 2, 4, 8}) { double r = rate_inc(nc.ch, P, ts, B); if (r > bi) { bi = r; bts = ts; bB = B; } }
            double bc = 0, bct = 0;
            for (double ts : tss) { double r = rate_coh(nc.ch, P, ts); if (r > bc) { bc = r; bct = ts; } }
            double bw = 0;
            for (double T : dwells) bw = std::max(bw, rate_wp(nc.ch, nc.ch, P, T));
            printf("| %s | %.2f | %.2f | %.2f (%g s, %d) | %.2f (%g s) | %.2f |\n", nc.name.c_str(), db(Rb / r0), db(Rb / r1), db(Rb / bi), bts, bB, db(Rb / bc), bct, db(Rb / bw));
        }
        printf("\n");
    }

    // ---- Section 3: mismatch of the WP model spread
    printf("## 3. Robustness of one fixed weighted-periodogram design to the actual spread\n\n");
    printf("Rows: assumed Gaussian 2σ used for the weights. Columns: actual Gaussian 2σ. Dwell fixed at\n");
    printf("T = 10.24 s; the assumed C/N0 equals the actual. Gap to the bound (dB). Diagonal = matched.\n\n");
    std::vector<double> ws = {0.1, 0.5, 1, 3, 10, 30};
    for (double c : {-10.0, 0.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| assumed \\ actual |", c);
        for (double w : ws) printf(" %g Hz |", w);
        printf("\n|---|"); for (size_t i = 0; i < ws.size(); ++i) printf("---:|"); printf("\n");
        for (double wm : ws) {
            printf("| %g Hz |", wm);
            for (double w : ws) {
                double Rb = bound_rate(G(w), P), R = rate_wp(G(w), G(wm), P, 10.24);
                printf(" %.2f |", db(Rb / R));
            }
            printf("\n");
        }
        // assumed C/N0 mismatch: weights designed at C/N0 = c+10 and c-10
        printf("| matched spread, weights for C/N0 +10 dB |");
        for (double w : ws) { double Rb = bound_rate(G(w), P); double R = rate_wp(G(w), scaled(G(w), 10.0), P, 10.24); printf(" %.2f |", db(Rb / R)); }
        printf("\n| matched spread, weights for C/N0 −10 dB |");
        for (double w : ws) { double Rb = bound_rate(G(w), P); double R = rate_wp(G(w), scaled(G(w), 0.1), P, 10.24); printf(" %.2f |", db(Rb / R)); }
        printf("\n\n");
    }

    // ---- Section 4: tone versus chirp in two-path frequency-selective fading
    printf("## 4. Tone versus linear chirp through two-path Rayleigh fading (matched receivers)\n\n");
    printf("A chirp sweeping B_c = 2200 Hz over the dwell T, after dechirping, places a path of delay τ at a\n");
    printf("frequency offset −(B_c/T)·τ. Equal-power paths, each Gaussian with the stated 2σ. The tone sees the\n");
    printf("sum of the two paths as one process (delays only rotate phase). Both receivers are matched weighted\n");
    printf("periodograms over the dwell T; entries are the gap (dB) to the flat-fading bound for the same spectrum.\n\n");
    struct TP { double tau_ms, two_sigma; const char* name; };
    std::vector<TP> tps = {{0.5, 0.1, "F.1487 quiet 0.5 ms/0.1 Hz"}, {1, 0.5, "moderate 1 ms/0.5 Hz"}, {2, 1, "disturbed 2 ms/1 Hz"},
                           {7, 1, "NVIS 7 ms/1 Hz"}, {6, 10, "low-lat dist. 6 ms/10 Hz"}, {7, 30, "high-lat dist. 7 ms/30 Hz"}};
    for (double c : {-10.0, 0.0, 10.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n| Channel | T | tone gap | chirp gap | chirp − tone (dB) |\n|---|---:|---:|---:|---:|\n", c);
        for (auto& tp : tps) {
            for (double T : {0.16, 1.28, 10.24}) {
                Chan tone = G(tp.two_sigma);
                double off = 2200.0 / T * tp.tau_ms * 1e-3;
                Chan chirp = G(tp.two_sigma, 0.5, 0) + G(tp.two_sigma, 0.5, -off);
                double Rb = bound_rate(tone, P);
                double rt = rate_wp(tone, tone, P, T), rc = rate_wp(chirp, chirp, P, T);
                printf("| %s | %g s | %.2f | %.2f | %+.2f |\n", tp.name, T, db(Rb / rt), db(Rb / rc), db(rt / rc));
            }
        }
        printf("\n");
    }
    // ---- Section 5: power-equivalent gaps
    printf("## 5. Power-equivalent penalties for selected receivers\n\n");
    printf("The gaps above are extra transmit time at the same power. Here, the C/N0 increase (dB) a receiver needs to\n");
    printf("reach the bound's rate at the reference C/N0, found by bisection. In the low-SNR regime it is about half of\n");
    printf("the time gap; at high energy per coherence interval they converge.\n\n");
    auto pe = [&](std::function<double(double)> rate, double Rtarget, double P0) {
        double lo = P0, hi = P0;
        while (rate(hi) < Rtarget) hi *= 1.25;
        for (int it = 0; it < 60; ++it) { double mid = std::sqrt(lo * hi); if (rate(mid) < Rtarget) lo = mid; else hi = mid; }
        return db(hi / P0);
    };
    printf("| C/N0 (dB-Hz) | Channel | FT8-like INC(160 ms,0): time / power | COH (best T): time / power | chirp vs tone, T=1.28 s, 2 ms 1 Hz two-path: time / power |\n|---:|---|---:|---:|---:|\n");
    for (double c : {-10.0, 0.0}) {
        double P = undb(c);
        for (double w : {0.1, 1.0, 10.0}) {
            Chan ch = G(w);
            double Rb = bound_rate(ch, P);
            auto ft8 = [&](double p) { return rate_inc(ch, p, 0.16, 0); };
            auto cohb = [&](double p) { double b = 0; for (double ts : tss) b = std::max(b, rate_coh(ch, p, ts)); return b; };
            double r0 = ft8(P), rc = cohb(P);
            printf("| %+.0f | Gaussian %g Hz | %.2f / %.2f | %.2f / %.2f |", c, w, db(Rb / r0), pe(ft8, Rb, P), db(Rb / rc), pe(cohb, Rb, P));
            if (w == 1.0) {
                double T = 1.28, off = 2200.0 / T * 2e-3;
                Chan tone = G(1), chirp = G(1, 0.5, 0) + G(1, 0.5, -off);
                double rt = rate_wp(tone, tone, P, T);
                auto rcf = [&](double p) { return rate_wp(chirp, chirp, p, T); };
                printf(" %.2f / %.2f |\n", db(rt / rcf(P)), pe(rcf, rt, P));
            } else printf("  |\n");
        }
    }
    printf("\n");
    return 0;
}
