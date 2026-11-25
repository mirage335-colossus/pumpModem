// Peak-limited noncoherent fading bound: independent reproduction and sensitivity study.
//
// Model (Sethuraman & Hajek 2005, Prop. 3.3; Viterbi 1967): frequency-flat fading with
// unit-power stationary fading process H(t), normalized Doppler spectral measure S,
// receiver knows the law but not the realization, transmitted |x(t)|^2 <= peak.
// With received peak C/N0 = P (Hz) and average power equal to the peak,
//     R(P) = P - Integral ln(1 + P S_ac(f)) df    [nat/s]
// where S_ac is the absolutely continuous part of S. A spectral line (specular, non-fading
// component, including a constant Doppler offset) contributes no learning penalty.
// With average power Pavg <= peak Ppk the bound is Pavg * eta(Ppk), eta(P) = R(P)/P.
//
// Everything here is deterministic quadrature; no Monte Carlo. Standard library only.
// Build: g++ -O2 -std=c++20 bound_shapes.cpp -o bound_shapes
// Run:   ./bound_shapes > results/bound_shapes.md
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

static const double PI = 3.14159265358979323846;
static const double LN2 = 0.69314718055994530942;

// ---------- adaptive Simpson ----------
static double simpson_rec(const std::function<double(double)>& f, double a, double b, double fa, double fm,
                          double fb, double whole, double tol, int depth) {
    double m = 0.5 * (a + b), lm = 0.5 * (a + m), rm = 0.5 * (m + b);
    double flm = f(lm), frm = f(rm);
    double left = (m - a) / 6 * (fa + 4 * flm + fm), right = (b - m) / 6 * (fm + 4 * frm + fb);
    double diff = left + right - whole;
    if (depth <= 0 || std::fabs(diff) <= 15 * tol) return left + right + diff / 15;
    return simpson_rec(f, a, m, fa, flm, fm, left, tol / 2, depth - 1) +
           simpson_rec(f, m, b, fm, frm, fb, right, tol / 2, depth - 1);
}
static double integrate(const std::function<double(double)>& f, double a, double b, double tol = 1e-13) {
    // split into 64 panels first so narrow features are seen
    const int n = 64;
    double s = 0, h = (b - a) / n;
    for (int i = 0; i < n; ++i) {
        double x0 = a + i * h, x1 = x0 + h, xm = 0.5 * (x0 + x1);
        double f0 = f(x0), f1 = f(x1), fm = f(xm);
        s += simpson_rec(f, x0, x1, f0, fm, f1, h / 6 * (f0 + 4 * fm + f1), tol / n, 40);
    }
    return s;
}

// ---------- spectra ----------
// A spectrum = specular line weight (no penalty) + list of absolutely continuous components.
struct Comp {
    enum Kind { GAUSS, FLAT, JAKES, LORENTZ, EPAN } kind;
    double w;      // power weight
    double c;      // center frequency (Hz)
    double s;      // scale: GAUSS sigma; FLAT half-width; JAKES fd; LORENTZ gamma; EPAN half-width
};
struct Spectrum {
    std::string name;
    double line = 0;           // specular weight
    std::vector<Comp> comps;   // absolutely continuous parts
};

static double comp_density(const Comp& k, double f) {
    double u = f - k.c;
    switch (k.kind) {
        case Comp::GAUSS: return k.w / (std::sqrt(2 * PI) * k.s) * std::exp(-0.5 * u * u / (k.s * k.s));
        case Comp::FLAT: return std::fabs(u) < k.s ? k.w / (2 * k.s) : 0.0;
        case Comp::JAKES: return std::fabs(u) < k.s ? k.w / (PI * k.s * std::sqrt(1 - (u / k.s) * (u / k.s))) : 0.0;
        case Comp::LORENTZ: return k.w * (k.s / PI) / (k.s * k.s + u * u);
        case Comp::EPAN: return std::fabs(u) < k.s ? k.w * 0.75 / k.s * (1 - (u / k.s) * (u / k.s)) : 0.0;
    }
    return 0;
}

// Integral of ln(1 + P*S(f)) for a single component handled with substitutions where needed.
static double logint_single(const Comp& k, double P) {
    switch (k.kind) {
        case Comp::FLAT: return 2 * k.s * std::log1p(P * k.w / (2 * k.s));
        case Comp::JAKES: {
            // f = fd sin(th): integral fd cos(th) ln(1 + P w /(pi fd cos th)) dth
            auto g = [&](double th) {
                double c = std::cos(th);
                if (c <= 0) return 0.0;
                return k.s * c * std::log1p(P * k.w / (PI * k.s * c));
            };
            return integrate(g, -PI / 2, PI / 2, 1e-14);
        }
        case Comp::LORENTZ: {
            // f = gamma tan(th): integral gamma sec^2 ln(1 + P w cos^2 /(pi gamma)) dth
            auto g = [&](double th) {
                double c = std::cos(th);
                if (c <= 1e-300) return P * k.w / PI * 1.0 / 1.0 * 1.0;  // limit: gamma*sec^2*(Pw cos^2/(pi gamma)) = P w / pi
                return k.s / (c * c) * std::log1p(P * k.w * c * c / (PI * k.s));
            };
            return integrate(g, -PI / 2, PI / 2, 1e-14);
        }
        case Comp::GAUSS: {
            auto g = [&](double f) { return std::log1p(P * comp_density(k, f)); };
            double L = 14 * k.s;
            return integrate(g, k.c - L, k.c + L, 1e-14);
        }
        case Comp::EPAN: {
            auto g = [&](double f) { return std::log1p(P * comp_density(k, f)); };
            return integrate(g, k.c - k.s, k.c + k.s, 1e-14);
        }
    }
    return 0;
}

static double logint(const Spectrum& S, double P) {
    if (S.comps.empty()) return 0.0;
    if (S.comps.size() == 1) return logint_single(S.comps[0], P);
    // general mixture of bounded/Gaussian components: integrate over union of supports,
    // splitting at component edges so adaptive Simpson sees each feature.
    std::vector<double> br;
    for (auto& k : S.comps) {
        double L = (k.kind == Comp::GAUSS) ? 14 * k.s : k.s;
        if (k.kind == Comp::LORENTZ || k.kind == Comp::JAKES) {
            fprintf(stderr, "mixture with heavy-tail/singular component not supported\n");
            return NAN;
        }
        br.push_back(k.c - L); br.push_back(k.c + L);
        if (k.kind == Comp::GAUSS) for (int j = -6; j <= 6; ++j) br.push_back(k.c + j * k.s);
    }
    std::sort(br.begin(), br.end());
    br.erase(std::unique(br.begin(), br.end()), br.end());
    auto g = [&](double f) {
        double s = 0;
        for (auto& k : S.comps) s += comp_density(k, f);
        return std::log1p(P * s);
    };
    double tot = 0;
    for (size_t i = 0; i + 1 < br.size(); ++i) tot += integrate(g, br[i], br[i + 1], 1e-14);
    return tot;
}

static double rate_nat(const Spectrum& S, double P) { return P - logint(S, P); }  // nat/s
static double ebn0_db(const Spectrum& S, double P) { return 10 * std::log10(P * LN2 / rate_nat(S, P)); }
static double db(double x) { return 10 * std::log10(x); }
static double undb(double x) { return std::pow(10.0, x / 10); }

// Effective Doppler bandwidth 1/int S_ac^2 (for unit-integral S_ac), 2*sigma_rms, 80%-power width.
static double int_S2(const Spectrum& S) {
    double ac = 0; for (auto& k : S.comps) ac += k.w;
    auto g = [&](double f) { double s = 0; for (auto& k : S.comps) s += comp_density(k, f); return s * s; };
    double lo = 1e300, hi = -1e300;
    for (auto& k : S.comps) { double L = (k.kind == Comp::GAUSS) ? 14 * k.s : k.s; lo = std::min(lo, k.c - L); hi = std::max(hi, k.c + L); }
    return integrate(g, lo, hi, 1e-12) / (ac * ac);
}

static Spectrum gauss(double two_sigma, double w = 1, double c = 0) { return {"", 0, {{Comp::GAUSS, w, c, two_sigma / 2}}}; }

// ---------- worst-case spectrum for a fixed RMS spread (KKT) ----------
// maximize  int ln(1+P S)  s.t. int S = 1, int f^2 S = sig^2, S >= 0.
// Solution: S(f) = [1/(a + b f^2) - 1/P]^+ ; support |f| < F, F = sqrt((P - a)/b).
struct KKT { double a, b, F; };
static void kkt_moments(double a, double b, double P, double& m0, double& m2) {
    double F = std::sqrt((P - a) / b), r = std::sqrt(b / a), at = std::atan(F * r);
    double I0 = 2 * at / std::sqrt(a * b);                 // int 1/(a+bf^2)
    double I2 = (2 * F - a * I0) / b;                      // int f^2/(a+bf^2)
    m0 = I0 - 2 * F / P;
    m2 = I2 - (2 * F * F * F / 3) / P;
}
static KKT kkt_solve(double P, double sig) {
    // parametrize a = P*(1-e) with e in (0,1); for each e find b giving m0 = 1 (m0 decreasing in b),
    // then bisect e to match m2 = sig^2.
    auto b_for = [&](double a) {
        double lo = 1e-300, hi = 1e300;
        for (int it = 0; it < 400; ++it) {
            double mid = std::sqrt(lo * hi), m0, m2;
            kkt_moments(a, mid, P, m0, m2);
            if (m0 > 1) lo = mid; else hi = mid;
        }
        return std::sqrt(lo * hi);
    };
    double elo = 1e-15, ehi = 1 - 1e-15;
    for (int it = 0; it < 200; ++it) {
        double e = 0.5 * (elo + ehi), a = P * (1 - e), b = b_for(a), m0, m2;
        kkt_moments(a, b, P, m0, m2);
        // larger e (smaller a) -> taller centre -> narrower -> smaller m2
        if (m2 > sig * sig) elo = e; else ehi = e;
    }
    double e = 0.5 * (elo + ehi), a = P * (1 - e), b = b_for(a);
    return {a, b, std::sqrt((P - a) / b)};
}
static double kkt_logint(const KKT& k, double P) {
    // int_{-F}^{F} ln(P/(a+bf^2)) df
    double F = k.F, a = k.a, b = k.b;
    auto anti = [&](double f) { return f * std::log(a + b * f * f) - 2 * f + 2 * std::sqrt(a / b) * std::atan(f * std::sqrt(b / a)); };
    return 2 * F * std::log(P) - (anti(F) - anti(-F));
}

int main() {
    std::vector<double> spreads = {0.1, 0.5, 1, 3, 10, 30};
    printf("# Peak-limited noncoherent bound: reproduction and sensitivity\n\n");
    printf("Generated by `bound_shapes.cpp` (deterministic quadrature). Rates are the conditional upper bound\n");
    printf("R = P - Integral ln(1+P S_ac) with peak = average received C/N0 = P; Eb/N0 = P ln2 / R.\n\n");

    // 1. Reproduction at -10 dB-Hz
    printf("## 1. Reproduction of the quoted table (Gaussian spectrum, 2sigma convention, -10 dB-Hz)\n\n");
    printf("| 2σ (Hz) | B_eff = 1/∫S² (Hz) | rate bound (bit/s) | Eb/N0 (dB) | x = (C/N0)/B_eff (dB) |\n|---:|---:|---:|---:|---:|\n");
    for (double w : {0.1, 1.0, 10.0, 30.0}) {
        Spectrum S = gauss(w);
        double P = undb(-10), Be = 1 / int_S2(S);
        printf("| %g | %.4g | %.5g | %.2f | %.1f |\n", w, Be, rate_nat(S, P) / LN2, ebn0_db(S, P), db(P / Be));
    }

    // 2. Universal curve in x
    printf("\n## 2. One parameter: energy per effective coherence interval\n\n");
    printf("For any Gaussian Doppler spectrum the minimum Eb/N0 depends only on x = (C/N0)/B_eff, B_eff = sqrt(pi)*2σ.\n");
    printf("Loss is relative to the AWGN wideband limit of -1.59 dB.\n\n");
    printf("| x (dB) | min Eb/N0, Gaussian (dB) | loss vs AWGN (dB) | min Eb/N0, flat spectrum same B_eff (dB) | slope d(Eb/N0)/d(x) |\n|---:|---:|---:|---:|---:|\n");
    for (double xdb = -30; xdb <= 30.01; xdb += 5) {
        double P = 1.0;  // scale-free: choose B_eff from x
        double Be = P / undb(xdb);
        double two_sigma = Be / std::sqrt(PI);
        Spectrum G = gauss(two_sigma);
        Spectrum F{"", 0, {{Comp::FLAT, 1, 0, Be / 2}}};  // flat with width Be has 1/int S^2 = Be
        double e1 = ebn0_db(G, P), e2 = ebn0_db(F, P);
        double h = 0.01;
        Spectrum G2 = gauss(two_sigma / undb(h));
        double slope = (e1 - ebn0_db(G2, P)) / (-h);
        printf("| %+.0f | %.2f | %.2f | %.2f | %.2f |\n", xdb, e1, e1 + 1.592, e2, slope);
    }

    // 3. Bound vs C/N0 for Gaussian spreads
    printf("\n## 3. Minimum Eb/N0 (dB) versus received C/N0, Gaussian spectrum\n\n");
    printf("| C/N0 (dB-Hz) |");
    for (double w : spreads) printf(" 2σ=%g Hz |", w);
    printf("\n|---:|"); for (size_t i = 0; i < spreads.size(); ++i) printf("---:|"); printf("\n");
    for (double c = -30; c <= 30.01; c += 5) {
        printf("| %+.0f |", c);
        for (double w : spreads) printf(" %.1f |", ebn0_db(gauss(w), undb(c)));
        printf("\n");
    }
    printf("\nTime to deliver 100 information bits at the bound (with ideal coding, interleaving and synchronization):\n\n");
    printf("| C/N0 (dB-Hz) | AWGN |"); for (double w : spreads) printf(" 2σ=%g Hz |", w);
    printf("\n|---:|---:|"); for (size_t i = 0; i < spreads.size(); ++i) printf("---:|"); printf("\n");
    auto fmt_time = [](double s) {
        static char buf[64];
        if (s < 120) snprintf(buf, 64, "%.0f s", s);
        else if (s < 7200) snprintf(buf, 64, "%.0f min", s / 60);
        else if (s < 172800) snprintf(buf, 64, "%.1f h", s / 3600);
        else if (s < 3.15e7 * 2) snprintf(buf, 64, "%.0f d", s / 86400);
        else snprintf(buf, 64, "%.0f yr", s / 3.156e7);
        return std::string(buf);
    };
    for (double c = -30; c <= 20.01; c += 5) {
        double P = undb(c);
        printf("| %+.0f | %s |", c, fmt_time(100 / (P / LN2)).c_str());
        for (double w : spreads) printf(" %s |", fmt_time(100 / (rate_nat(gauss(w), P) / LN2)).c_str());
        printf("\n");
    }

    // 4. Spectrum shape at equal nominal spread
    printf("\n## 4. Sensitivity to Doppler spectrum shape (minimum Eb/N0, dB)\n\n");
    printf("Each shape is scaled to the same nominal spread W. 'RMS' rows set 2*sigma_rms = W. The Lorentzian\n");
    printf("(first-order Gauss-Markov fading) has infinite RMS width and is matched to the Gaussian 80%%-power width.\n");
    printf("'Worst (RMS)' is the spectrum that maximizes the learning penalty for the given RMS width at that C/N0\n");
    printf("(KKT solution S = [1/(a+bf^2) - 1/P]^+); it lower-bounds every spectrum with that RMS width.\n\n");
    for (double c : {-10.0, 0.0, 10.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n", c);
        printf("| Shape (W = nominal spread) |"); for (double w : spreads) printf(" W=%g Hz |", w);
        printf("\n|---|"); for (size_t i = 0; i < spreads.size(); ++i) printf("---:|"); printf("\n");
        struct Row { const char* name; std::function<double(double)> eb; };
        std::vector<Row> rows = {
            {"Gaussian, 2σ = W (F.1487)", [&](double W) { return ebn0_db(gauss(W), P); }},
            {"Worst case for 2σ_rms = W", [&](double W) { KKT k = kkt_solve(P, W / 2); double R = P - kkt_logint(k, P); return db(P * LN2 / R); }},
            {"Parabolic (low-SNR worst), 2σ_rms = W", [&](double W) { Spectrum S{"", 0, {{Comp::EPAN, 1, 0, std::sqrt(5.0) * W / 2}}}; return ebn0_db(S, P); }},
            {"Flat, 2σ_rms = W (width 1.73 W)", [&](double W) { Spectrum S{"", 0, {{Comp::FLAT, 1, 0, std::sqrt(3.0) * W / 2}}}; return ebn0_db(S, P); }},
            {"Flat, full width W", [&](double W) { Spectrum S{"", 0, {{Comp::FLAT, 1, 0, W / 2}}}; return ebn0_db(S, P); }},
            {"Jakes (classical U), 2σ_rms = W", [&](double W) { Spectrum S{"", 0, {{Comp::JAKES, 1, 0, W / std::sqrt(2.0)}}}; return ebn0_db(S, P); }},
            {"Lorentzian, same 80% width as Gaussian", [&](double W) { Spectrum S{"", 0, {{Comp::LORENTZ, 1, 0, 0.2082 * W}}}; return ebn0_db(S, P); }},
            {"Two Gaussians 2σ=W/4, centres ±W/2 (sum 2σ_rms≈1.03W)", [&](double W) { Spectrum S{"", 0, {{Comp::GAUSS, 0.5, -W / 2, W / 8}, {Comp::GAUSS, 0.5, W / 2, W / 8}}}; return ebn0_db(S, P); }},
        };
        for (auto& r : rows) {
            printf("| %s |", r.name);
            for (double w : spreads) printf(" %.2f |", r.eb(w));
            printf("\n");
        }
        printf("\n");
    }

    // 5. Specular (Rician) and mixed narrow+wide components
    printf("## 5. Specular and narrow components\n\n");
    printf("Rician: a line of weight K/(K+1) (unknown but fixed phase, any constant Doppler offset) plus a Gaussian\n");
    printf("diffuse part of weight 1/(K+1) with 2σ = W. Mixed: fraction a in a 0.05 Hz Gaussian, rest in 2σ = W.\n");
    printf("Values are minimum Eb/N0 (dB) at the stated C/N0.\n\n");
    for (double c : {-10.0, 0.0}) {
        double P = undb(c);
        printf("**C/N0 = %+.0f dB-Hz**\n\n", c);
        printf("| Channel |"); for (double w : spreads) printf(" W=%g Hz |", w);
        printf("\n|---|"); for (size_t i = 0; i < spreads.size(); ++i) printf("---:|"); printf("\n");
        for (double K : {0.0, 0.1, 0.3, 1.0, 3.0, 10.0}) {
            printf("| Rician K = %g (%.1f dB) |", K, K > 0 ? db(K) : -INFINITY);
            for (double w : spreads) {
                Spectrum S{"", K / (K + 1), {{Comp::GAUSS, 1 / (K + 1), 0, w / 2}}};
                printf(" %.2f |", ebn0_db(S, P));
            }
            printf("\n");
        }
        for (double a : {0.1, 0.3, 0.5}) {
            printf("| %.0f%% in 0.05 Hz + rest in W |", 100 * a);
            for (double w : spreads) {
                Spectrum S{"", 0, {{Comp::GAUSS, a, 0, 0.025}, {Comp::GAUSS, 1 - a, 0, w / 2}}};
                printf(" %.2f |", ebn0_db(S, P));
            }
            printf("\n");
        }
        printf("| 50%%/50%% two modes 2σ=W each, offset ±2W |");
        for (double w : spreads) { Spectrum S{"", 0, {{Comp::GAUSS, 0.5, -2 * w, w / 2}, {Comp::GAUSS, 0.5, 2 * w, w / 2}}}; printf(" %.2f |", ebn0_db(S, P)); }
        printf("\n| 50%% 2σ=W + 50%% 2σ=10W, same centre |");
        for (double w : spreads) { Spectrum S{"", 0, {{Comp::GAUSS, 0.5, 0, w / 2}, {Comp::GAUSS, 0.5, 0, 5 * w}}}; printf(" %.2f |", ebn0_db(S, P)); }
        printf("\n\n");
    }

    // 6. Receive branches and transmitter peakiness
    printf("## 6. Resources outside the single-antenna, constant-envelope model\n\n");
    printf("**Independent receive branches.** N_r branches, each receiving the full C/N0 with independent fading of the\n");
    printf("same spectrum: the bound scales by N_r (the information divergence adds per branch). Eb/N0 below is referred\n");
    printf("to one branch's C/N0. A dual-polarization receiver that separates two independently fading magneto-ionic\n");
    printf("modes of equal power P/2, versus one linear antenna receiving half of each (P/2 total): rate ratio shown.\n\n");
    printf("| C/N0 (dB-Hz) | 2σ (Hz) | 1 branch | 2 branches | 4 branches | dual-pol vs single linear, rate ratio |\n|---:|---:|---:|---:|---:|---:|\n");
    for (double c : {-10.0, 0.0, 10.0}) for (double w : {0.1, 1.0, 10.0}) {
        double P = undb(c); Spectrum G = gauss(w);
        double r1 = rate_nat(G, P);
        // single linear: power P/2 total in one process; dual: two independent processes each P/2
        double single = rate_nat(G, P / 2), dual = 2 * rate_nat(G, P / 2);
        printf("| %+.0f | %g | %.2f | %.2f | %.2f | %.2f |\n", c, w, db(P * LN2 / r1), db(P * LN2 / (2 * r1)), db(P * LN2 / (4 * r1)), dual / single);
    }
    printf("\n**Transmitter peak-to-average ratio.** If the amplifier's peak can exceed the average-power budget by κ, the\n");
    printf("bound becomes Pavg·η(κ·Pavg); a duty-cycled transmitter at peak gains energy efficiency. κ = 1 is the\n");
    printf("constant-envelope case of the quoted table. Values: minimum Eb/N0 (dB) referred to average C/N0.\n\n");
    printf("| average C/N0 (dB-Hz) | 2σ (Hz) | κ=1 | κ=2 | κ=4 | κ=10 | κ=100 |\n|---:|---:|---:|---:|---:|---:|---:|\n");
    for (double c : {-10.0, 0.0}) for (double w : {0.1, 1.0, 10.0, 30.0}) {
        double P = undb(c); Spectrum G = gauss(w);
        printf("| %+.0f | %g |", c, w);
        for (double kap : {1.0, 2.0, 4.0, 10.0, 100.0}) {
            double eta = rate_nat(G, kap * P) / (kap * P);
            printf(" %.2f |", db(LN2 / eta));
        }
        printf("\n");
    }
    printf("\n**Splitting peak power across simultaneous tones** (two tones of P/2 each, independent fading): rate ratio to a\n");
    printf("single tone at P. Values below 1 show that splitting a peak-limited signal costs efficiency.\n\n");
    printf("| C/N0 (dB-Hz) | 2σ=0.1 | 2σ=1 | 2σ=10 |\n|---:|---:|---:|---:|\n");
    for (double c : {-10.0, 0.0, 10.0, 20.0}) {
        double P = undb(c);
        printf("| %+.0f |", c);
        for (double w : {0.1, 1.0, 10.0}) printf(" %.3f |", 2 * rate_nat(gauss(w), P / 2) / rate_nat(gauss(w), P));
        printf("\n");
    }
    return 0;
}
