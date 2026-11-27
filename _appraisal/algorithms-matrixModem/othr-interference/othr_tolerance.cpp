// Over-the-horizon-radar tolerance of the Matrix Modem FSK lattice receiver (0.4 s dwell).
//
// Time-domain simulation at 3200 complex samples/s. One scene = K consecutive 0.4 s dwells
// (1280 samples, 1024 active) that share one radar: a periodic linear-FM sweep of bandwidth B and
// repetition rate fr. The radar is generated at 12.8 kHz, passed through a steep 2.8 kHz
// SSB-like filter (255-tap Kaiser, beta 8, -6 dB at +-1400 Hz) and decimated, so each crossing of
// the passband has the ringing tails of a real receive filter. The comb of spectral lines sits at
// a random offset from the tone grid. Sweeps are coherent from sweep to sweep (a line comb) or
// have a random phase per sweep. `INR` is the radar power while in band relative to the noise
// power in 3.2 kHz. In each dwell the wanted tone sits on a fresh keyed random 6.25 Hz position
// inside the central 2.2 kHz, with a static random phase.
//
// Receiver chains (front end -> 1024-point rectangular DFT of the active part -> evidence):
//   lin/glob    linear samples, one global noise estimate, unquantized energy (naive receiver)
//   lin/bin     linear samples, per-bin median noise estimate over the scene, 4-bit evidence
//   q4/bin      4-bit samples scaled to the noise floor, saturating (a limiter): 16 mid-rise
//               levels per real component, step 0.335 sigma (range +-2.5 sigma)
//   q4e/bin     4-bit sample code with an erasure: 15 levels -7..+7 of 0.45 sigma; a sample with
//               a component beyond 3.4 sigma is erased (the sixteenth code; treated as zero)
//   ipw+q4e     inverse-power weighting first: w = min(1, 2.5 p0 / p), p the mean power of the
//               surrounding 8 samples (2.5 ms), p0 the noise floor; then q4e
//   cx+ipw+q4e  comb excision first: 4096-point (1.28 s) four-term Blackman-Harris analysis and
//               synthesis at 75% overlap; bins whose median power over the scene exceeds 6 times
//               the lower quartile of those medians are zeroed with two neighbours each side;
//               then the noise scale is estimated again and ipw+q4e follows. A wanted tone is
//               scaled by the fraction of its own analysis-window energy that survives.
// sigma comes from the 25th percentile of |Re x|, which a radar duty below about 70% does not
// move. 4-bit evidence: g = min(15, floor(e / 0.5)), e = bin energy / per-bin (or global) noise
// mean; a bin whose noise estimate is below a quarter of the global one gives g = 0.
//
// Result: the weak-signal deflection per dwell, d2 = (E1[g] - E0[g])^2 / Var0[g], with the tone
// position, radar timing and noise all random, relative to gamma^2 for an unquantized receiver
// without radar. Summed evidence over many dwells needs 1/d2 dwells, so 10 log10(gamma^2 / d2) is
// extra transmit time; half of it (printed) is the equivalent received-power margin in dB.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp othr_tolerance.cpp -o othr_tolerance
// Run:   ./othr_tolerance [scenes] > results/othr_tolerance.md
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <omp.h>

using cf = std::complex<float>;
using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;
static const double FS = 3200.0;
static const int NPER = 1280, NACT = 1024, OFF = 128;
static const int OS = 4, NTAP = 255;          // radar oversampling and receive-filter length
static const int NX = 4096, HOPX = 1024;      // excision transform and hop

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

struct Fft {
    int n; std::vector<cd> tw; std::vector<int> rev;
    explicit Fft(int n_) : n(n_), tw(n_ / 2), rev(n_) {
        for (int i = 0; i < n / 2; ++i) tw[i] = std::polar(1.0, -2 * PI * i / n);
        int lg = 0; while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i >> b & 1) r |= 1 << (lg - 1 - b); rev[i] = r; }
    }
    void run(std::vector<cd>& a) const {
        for (int i = 0; i < n; ++i) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= n; len <<= 1) {
            int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len) for (int j = 0; j < half; ++j) { cd u = a[i + j], v = a[i + j + half] * tw[j * step]; a[i + j] = u + v; a[i + j + half] = u - v; }
        }
    }
};

struct Radar { const char* name; double B, fr; bool coherent; };

static double bessel_i0(double x) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-16 * s) break; } return s; }
static double quant16(double x, double step) { double k = std::floor(x / step); k = std::min(7.0, std::max(-8.0, k)); return (k + 0.5) * step; }
static double quant15(double x, double step) { return std::min(7.0, std::max(-7.0, std::nearbyint(x / step))) * step; }
static double scale_q25(const std::vector<cd>& v) {
    std::vector<double> t; t.reserve(v.size() / 7 + 1);
    for (size_t n = 0; n < v.size(); n += 7) t.push_back(std::fabs(v[n].real()));
    std::nth_element(t.begin(), t.begin() + t.size() / 4, t.end());
    return t[t.size() / 4] / 0.31864;
}

struct Acc { double d1 = 0, e0 = 0, e02 = 0; long n = 0; };

int main(int argc, char** argv) {
    int scenes = argc > 1 ? std::atoi(argv[1]) : 160;
    const int K = 96, NCH = 6, NFE = 5, NT = K * NPER;
    const double gamma = 0.25;  // tone energy / N0 in the active part (-6 dB per dwell)
    std::vector<Radar> radars = {
        {"12 kHz at 40 sweeps/s, coherent (29B6 class; in-band duty 23%)", 12e3, 40, true},
        {"10 kHz at 50 sweeps/s, coherent (28%)", 10e3, 50, true},
        {"10 kHz at 66.67 sweeps/s, coherent (burst radar class; 28%)", 10e3, 66.6667, true},
        {"10 kHz at 50 sweeps/s, random phase per sweep (28%)", 10e3, 50, false},
        {"20 kHz at 25 sweeps/s, coherent (14%)", 20e3, 25, true},
        {"40 kHz at 10 sweeps/s, coherent (7%)", 40e3, 10, true},
        {"8 kHz at 32 sweeps/s, coherent (35%)", 8e3, 32, true},
        {"4 kHz at 25 sweeps/s, coherent (70%)", 4e3, 25, true},
        {"25.6 kHz at 1 sweep/s, coherent (coastal radar class, ungated; 11%)", 25.6e3, 1, true},
    };
    std::vector<double> inrs = {-300, 0, 10, 20, 30, 40, 60};
    const char* chn[NCH] = {"lin/glob", "lin/bin", "q4/bin", "q4e/bin", "ipw+q4e", "cx+ipw+q4e"};
    // receive filter
    std::vector<double> fir(NTAP);
    { double fc = 1400.0 / (FS * OS), sum = 0; for (int n = 0; n < NTAP; ++n) { double m = n - (NTAP - 1) / 2.0, r = 2.0 * n / (NTAP - 1) - 1; double h = m == 0 ? 2 * fc : std::sin(2 * PI * fc * m) / (PI * m); fir[n] = h * bessel_i0(8.0 * std::sqrt(std::max(0.0, 1 - r * r))) / bessel_i0(8.0); sum += fir[n]; } for (auto& v : fir) v /= sum; }
    // excision window, overlap normalization and tone kernel
    std::vector<double> bh(NX), cola(HOPX, 0.0), wk(9);
    for (int n = 0; n < NX; ++n) { double a = 2 * PI * (n + 0.5) / NX; bh[n] = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a); }
    for (int n = 0; n < NX; ++n) cola[n % HOPX] += bh[n] * bh[n];
    for (int k = -4; k <= 4; ++k) { cd a = 0; for (int n = 0; n < NX; ++n) a += bh[n] * std::polar(1.0, -2 * PI * k * (n + 0.5) / NX); wk[k + 4] = std::norm(a); }
    { double s = 0; for (double v : wk) s += v; for (auto& v : wk) v /= s; }

    printf("# OTHR tolerance of the lattice receiver (0.4 s dwell)\n\n");
    printf("Generated by `othr_tolerance.cpp`: %d scenes of %d dwells per point, test tone at %.1f dB per dwell.\n", scenes, K, 10 * std::log10(gamma));
    printf("Entries: received-power margin consumed (dB) = half of the extra transmit time, relative to an\n");
    printf("unquantized receiver without radar. `INR` is radar power while in band over noise power in 3.2 kHz.\n");
    printf("The radar passes a steep 2.8 kHz receive filter before the modem sees it. `hot bins`: tone positions\n");
    printf("whose noise estimate is more than twice the global one (linear chain). `notched`: tone positions that\n");
    printf("lose more than half their energy in the comb excision. `erased`: samples erased by the q4e chain.\n");
    printf("Simulation of a model radar; not a measurement of any specific system.\n\n");
    Fft fft(NACT), fftx(NX);
    for (auto& rd : radars) {
        printf("## %s\n\n| INR (dB) |", rd.name);
        for (int c = 0; c < NCH; ++c) printf(" %s |", chn[c]);
        printf(" hot bins | notched | erased |\n|---:|"); for (int c = 0; c < NCH + 3; ++c) printf("---:|"); printf("\n");
        for (double inr : inrs) {
            double A = inr < -200 ? 0 : std::pow(10.0, inr / 20);
            int nth = omp_get_max_threads();
            std::vector<std::vector<Acc>> acc(nth, std::vector<Acc>(NCH));
            std::vector<double> hot(nth, 0.0), ers(nth, 0.0), notched(nth, 0.0);
            #pragma omp parallel
            {
                int tid = omp_get_thread_num();
                Rng rng((uint64_t)(inr * 131 + rd.B + rd.fr * 17 + rd.coherent + 4000) * 7919ull + tid * 104729ull + 5);
                std::vector<cd> x(NT), xe(NT), buf(NACT), bx(NX), ker(NACT), chirp((size_t)OS * NT + NTAP);
                std::vector<double> w1(NT), w2(NT), pw(NT + 1), gain(NACT);
                std::vector<std::vector<float>> E(NFE, std::vector<float>((size_t)K * NACT));
                std::vector<std::vector<double>> meds(NFE, std::vector<double>(NACT)); std::vector<double> gmeans(NFE), tmp;
                std::vector<cf> spec; std::vector<char> mask(NX);
                const int NFR = NT / HOPX;
                spec.resize((size_t)NFR * NX);
                #pragma omp for schedule(dynamic, 2)
                for (int sc = 0; sc < scenes; ++sc) {
                    // ---- scene: noise plus filtered radar
                    for (int n = 0; n < NT; ++n) x[n] = rng.cn();
                    if (A > 0) {
                        double tau0 = rng.uni(), foff = rd.B > 2800 ? (rng.uni() - 0.5) * (rd.B - 2800) : 0.0, fdel = rng.uni() * rd.fr;
                        long last = -1; cd sph = 1;
                        for (long m = 0; m < (long)chirp.size(); ++m) {
                            double t = (m - NTAP / 2) / (FS * OS), ph = t * rd.fr + tau0; long sw = (long)std::floor(ph); double u = ph - sw;
                            if (sw != last) { last = sw; sph = rd.coherent ? cd(1, 0) : std::polar(1.0, 2 * PI * rng.uni()); }
                            double fi = (u - 0.5) * rd.B - foff;
                            chirp[m] = std::fabs(fi) < FS * OS / 2 ? sph * std::polar(1.0, 2 * PI * ((rd.B / rd.fr) * (u * u - u) / 2 - foff * u / rd.fr + fdel * t)) : cd(0, 0);
                        }
                        for (int n = 0; n < NT; ++n) { cd a = 0; const cd* c = &chirp[(size_t)OS * n]; for (int t = 0; t < NTAP; ++t) a += fir[t] * c[NTAP - 1 - t]; x[n] += A * a; }
                    }
                    double sig = scale_q25(x), p0 = 2 * sig * sig;
                    auto weights = [&](const std::vector<cd>& src, double floor_power, std::vector<double>& w) {
                        pw[0] = 0; for (int n = 0; n < NT; ++n) pw[n + 1] = pw[n] + std::norm(src[n]);
                        for (int n = 0; n < NT; ++n) { int a = std::max(0, n - 4), b = std::min(NT, n + 4); double p = (pw[b] - pw[a]) / (b - a); w[n] = std::min(1.0, 2.5 * floor_power / p); }
                    };
                    weights(x, p0, w1);
                    // ---- comb excision of the raw stream (circular weighted overlap-add)
                    for (int f = 0; f < NFR; ++f) {
                        for (int n = 0; n < NX; ++n) bx[n] = bh[n] * x[(f * HOPX + n) % NT];
                        fftx.run(bx);
                        for (int b = 0; b < NX; ++b) spec[(size_t)f * NX + b] = cf((float)bx[b].real(), (float)bx[b].imag());
                    }
                    {
                        std::vector<double> mb(NX), t2(NFR), inband;
                        for (int b = 0; b < NX; ++b) { for (int f = 0; f < NFR; ++f) t2[f] = std::norm(spec[(size_t)f * NX + b]); std::nth_element(t2.begin(), t2.begin() + NFR / 2, t2.end()); mb[b] = t2[NFR / 2]; }
                        for (int b = 0; b < NX; ++b) { int s = b < NX / 2 ? b : b - NX; if (std::abs(s) < 1400 * NX / 3200) inband.push_back(mb[b]); }
                        std::nth_element(inband.begin(), inband.begin() + inband.size() / 4, inband.end());
                        double g = inband[inband.size() / 4];
                        std::vector<char> raw(NX); for (int b = 0; b < NX; ++b) raw[b] = mb[b] > 6 * g;
                        for (int b = 0; b < NX; ++b) { char m = 0; for (int k = -2; k <= 2; ++k) m |= raw[(b + k + NX) % NX]; mask[b] = m; }
                    }
                    std::fill(xe.begin(), xe.end(), cd(0, 0));
                    for (int f = 0; f < NFR; ++f) {
                        for (int b = 0; b < NX; ++b) bx[b] = mask[b] ? cd(0, 0) : std::conj(cd(spec[(size_t)f * NX + b]));
                        fftx.run(bx);  // inverse via conjugation
                        for (int n = 0; n < NX; ++n) xe[(f * HOPX + n) % NT] += bh[n] * std::conj(bx[n]) / (double(NX) * cola[n % HOPX]);
                    }
                    int nn = 0;
                    for (int b = 0; b < NACT; ++b) { double g = 0; for (int k = -4; k <= 4; ++k) g += mask[(4 * b + k + NX) % NX] ? 0.0 : wk[k + 4]; gain[b] = g; }
                    for (int b = -352; b < 352; b += 2) nn += gain[(b + NACT) % NACT] < 0.5;
                    notched[tid] += nn / 352.0;
                    double sige = scale_q25(xe);
                    weights(xe, 2 * sige * sige, w2);
                    long er = 0;
                    // front ends: 0 linear, 1 q4 limiter, 2 q4e, 3 ipw+q4e, 4 cx+ipw+q4e
                    auto front = [&](int fe, cd v, int i) -> cd {
                        if (fe == 0) return v;
                        if (fe == 1) return {quant16(v.real(), 0.335 * sig), quant16(v.imag(), 0.335 * sig)};
                        if (fe == 3) v *= w1[i];
                        if (fe == 4) v *= w2[i];
                        double st = 0.45 * (fe == 4 ? sige : sig), lim = 7.5 * st;
                        if (std::fabs(v.real()) > lim || std::fabs(v.imag()) > lim) { if (fe == 2) ++er; return {0, 0}; }
                        return {quant15(v.real(), st), quant15(v.imag(), st)};
                    };
                    for (int fe = 0; fe < NFE; ++fe) for (int k = 0; k < K; ++k) {
                        for (int n = 0; n < NACT; ++n) { int i = k * NPER + OFF + n; buf[n] = front(fe, fe == 4 ? xe[i] : x[i], i); }
                        fft.run(buf);
                        for (int b = 0; b < NACT; ++b) E[fe][(size_t)k * NACT + b] = (float)(std::norm(buf[b]) / NACT);
                    }
                    ers[tid] += double(er) / (K * NACT);
                    for (int fe = 0; fe < NFE; ++fe) {
                        tmp.resize(K);
                        std::vector<double> all; all.reserve(K * NACT / 5);
                        for (int b = 0; b < NACT; ++b) { for (int k = 0; k < K; ++k) tmp[k] = E[fe][(size_t)k * NACT + b]; std::nth_element(tmp.begin(), tmp.begin() + K / 2, tmp.end()); meds[fe][b] = tmp[K / 2] / 0.6931; }
                        for (int b = -352; b < 352; ++b) for (int k = 0; k < K; k += 3) all.push_back(E[fe][(size_t)k * NACT + (b + NACT) % NACT]);
                        std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
                        gmeans[fe] = all[all.size() / 2] / 0.6931;
                    }
                    { int h = 0; for (int b = -352; b < 352; b += 2) if (meds[0][(b + NACT) % NACT] > 2 * gmeans[0]) ++h; hot[tid] += h / 352.0; }
                    // ---- wanted tone: keyed random position per dwell, weak-signal deflection
                    for (int k = 0; k < K; ++k) {
                        int b = (2 * ((int)(rng.next() % 352) - 176) + NACT) % NACT;  // central 2.2 kHz around 0 Hz
                        cd tn = std::sqrt(gamma / NACT) * std::polar(1.0, 2 * PI * rng.uni());
                        for (int n = 0; n < NACT; ++n) ker[n] = std::polar(1.0, -2 * PI * ((long)b * n % NACT) / double(NACT));
                        for (int fe = 0; fe < NFE; ++fe) {
                            cd y = 0; double tg = fe == 4 ? gain[b] : 1.0;
                            for (int n = 0; n < NACT; ++n) { int i = k * NPER + OFF + n; y += front(fe, (fe == 4 ? xe[i] : x[i]) + tg * tn * std::conj(ker[n]), i) * ker[n]; }
                            double z1 = std::norm(y) / NACT, z0 = E[fe][(size_t)k * NACT + b];
                            auto ev = [&](double z, bool perbin, bool q) { if (perbin && meds[fe][b] < 0.25 * gmeans[fe]) return 0.0; double e = z / (perbin ? meds[fe][b] : gmeans[fe]); return q ? std::min(15.0, std::floor(e / 0.5)) : e; };
                            auto add = [&](int c, bool perbin, bool q) { double g0 = ev(z0, perbin, q), g1 = ev(z1, perbin, q); Acc& a = acc[tid][c]; a.d1 += g1 - g0; a.e0 += g0; a.e02 += g0 * g0; ++a.n; };
                            if (fe == 0) { add(0, false, false); add(1, true, true); }
                            else add(fe + 1, true, true);
                        }
                    }
                }
            }
            if (inr < -200) printf("| none |"); else printf("| %.0f |", inr);
            for (int c = 0; c < NCH; ++c) {
                Acc t; for (int i = 0; i < nth; ++i) { t.d1 += acc[i][c].d1; t.e0 += acc[i][c].e0; t.e02 += acc[i][c].e02; t.n += acc[i][c].n; }
                double m1 = t.d1 / t.n, m0 = t.e0 / t.n, v0 = t.e02 / t.n - m0 * m0;
                double d2 = m1 > 0 ? m1 * m1 / v0 : 0;
                double loss = d2 > 0 ? 0.5 * 10 * std::log10(gamma * gamma / d2) : 99;
                if (loss > 30) printf(" >30 |"); else printf(" %.1f |", loss);
            }
            double h = 0, o = 0, nn = 0; for (int i = 0; i < nth; ++i) { h += hot[i]; o += ers[i]; nn += notched[i]; }
            printf(" %.0f%% | %.0f%% | %.1f%% |\n", 100 * h / scenes, 100 * nn / scenes, 100 * o / scenes);
            fflush(stdout);
        }
        printf("\n");
    }
    return 0;
}
