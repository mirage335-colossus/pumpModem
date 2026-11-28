// Matrix Modem FSK below 9 kHz: tone rate and tone spacing under coherent reception with spread.
//
// Monte Carlo of the specification's keyed 10-bit block (32 symbols of 256 values, value =
// G_j c xor m_j, keyed balanced map of the 256 values onto P positions redrawn for every tone,
// keyed tone-to-symbol interleaver) for transmit formats that differ only in cell length
// (position spacing = 1/cell), tone length (cells per tone) and number of positions, all with the
// same block duration and the same total energy. The receiver scores every message on a ladder of
// coherent windows from one cell up to the whole block and adds the windows as energies:
//   windows shorter than a tone  : sub-tone rungs (cells of a held tone added over part of it)
//   window = one tone            : per-tone reception
//   windows of 2, 4, ... tones   : coherent attempts across tones
// For every rung it reports the block E/N0 at which 1% of blocks decode to a wrong message, and
// the same for a receiver that picks, per block, the rung whose best message is most significant.
//
// Channel per cell (complex gain on the transmitted tone), sampled M times per cell so that energy
// leaking into neighbouring positions is included when the spread is not small against the
// spacing:
//   steady     constant
//   ou         constant amplitude, phase an Ornstein-Uhlenbeck process (rms degrees, correlation s)
//   wander     constant amplitude, frequency a smooth random function inside +-b Hz with knots
//              1/b seconds apart (the "bounded" Doppler law of the Sub9 presets, common wander only)
//   rayleigh   Gaussian Doppler spectrum of 2-sigma width b Hz, no specular part
// Optional single-tuned transmit antenna: static amplitude and phase per position for a -3 dB
// bandwidth B centred on the positions (transients are in antenna_keying.cpp, not here).
//
// Noise is complex Gaussian, unit variance per cell and position. Signal amplitude a per cell:
// block E/N0 = a^2 * cells. Every statistic is a quadratic in a, so one set of noise draws serves
// all E/N0 values. Known timing and frequency. Theoretical calculation with a Monte Carlo part.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp coherent_block_mc.cpp -o coherent_block_mc
// Run:   ./coherent_block_mc [trials] [formats, e.g. ABCD or all] [channels, e.g. 0123 or all] [antenna Hz or 0]
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <omp.h>

using cf = std::complex<float>;
using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;

struct Rng {
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (int i = 0; i < 4; ++i) { seed += 0x9E3779B97F4A7C15ull; uint64_t z = seed; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; s[i] = z ^ (z >> 31); }
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() { uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17; s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45); return r; }
    double uni() { return ((next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
    cd cn() { double u = uni(), v = uni(), r = std::sqrt(-std::log(u)); return {r * std::cos(2 * PI * v), r * std::sin(2 * PI * v)}; }  // E|z|^2 = 1
    double gauss() { double u = uni(), v = uni(); return std::sqrt(-2 * std::log(u)) * std::cos(2 * PI * v); }
};

struct Format { char id; const char* name; int j; int nc; int P; int L; };
// cell = 0.16 s * 2^j, spacing = 6.25 Hz / 2^j, tone = nc cells, 32*L tones per block.
// Every format lasts 5242.88 s (87.4 min).
static const Format FORMATS[] = {
    {'A', "6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz)", 0, 1, 4, 1024},
    {'a', "6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz)", 0, 1, 8, 1024},
    {'I', "6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz)", 0, 16, 4, 64},
    {'B', "1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz)", 2, 1, 16, 256},
    {'b', "1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz)", 2, 1, 8, 256},
    {'C', "1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz)", 2, 4, 16, 64},
    {'D', "1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz)", 2, 16, 16, 16},
    {'E', "1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz)", 2, 64, 16, 4},
    {'F', "0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz)", 3, 1, 16, 128},
    {'f', "0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz)", 3, 8, 16, 16},
    {'G', "0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz)", 4, 1, 16, 64},
    {'H', "0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz)", 6, 1, 16, 16},
    {'J', "1.5625 Hz spacing, 0.64 s tone, 4 positions (6.25 Hz)", 2, 1, 4, 256},
    {'K', "1.5625 Hz spacing, 0.64 s tone, 2 positions (3.125 Hz)", 2, 1, 2, 256},
    {'L', "1.5625 Hz spacing, 10.24 s held tone, 4 positions (6.25 Hz)", 2, 16, 4, 16},
};
struct Channel { char id; const char* name; int type; double p1, p2; };  // type 0 steady, 1 ou, 2 wander, 3 rayleigh
static const Channel CHANNELS[] = {
    {'0', "steady", 0, 0, 0},
    {'1', "phase wander 30 deg rms, 1 h correlation (quiet night)", 1, 30, 3600},
    {'2', "phase wander 60 deg rms, 10 min correlation (disturbed night)", 1, 60, 600},
    {'3', "frequency wander within +-1 mHz (Sub9 best bound)", 2, 0.001, 0},
    {'4', "frequency wander within +-5 mHz (Sub9 moderate bound)", 2, 0.005, 0},
    {'5', "frequency wander within +-50 mHz (Sub9 worst bound)", 2, 0.05, 0},
    {'6', "Rayleigh, 1 mHz Gaussian spread", 3, 0.001, 0},
    {'7', "Rayleigh, 5 mHz Gaussian spread", 3, 0.005, 0},
    {'8', "Rayleigh, 50 mHz Gaussian spread", 3, 0.05, 0},
    {'9', "Rayleigh, 0.5 Hz Gaussian spread (outside every sub-9 kHz record)", 3, 0.5, 0},
};

static const int K = 10, NMSG = 1 << K, NSYM = 32;
static const int NAMP = 153;                       // E/N0 grid 6 .. 44 dB in 0.25 dB steps
static double amp_db(int i) { return 6.0 + 0.25 * i; }

// log of the upper tail of Gamma(n, 1) at x (n windows of unit-mean exponential energy)
static double log_gamma_tail(int n, double x) {
    if (x <= 0) return 0;
    if (n <= 600) {  // exact: e^-x sum_{i<n} x^i / i!
        double lt = -x, m = lt, acc = 0;  // work relative to the largest term
        std::vector<double> terms(n); terms[0] = lt;
        for (int i = 1; i < n; ++i) { lt += std::log(x / i); terms[i] = lt; if (lt > m) m = lt; }
        for (int i = 0; i < n; ++i) acc += std::exp(terms[i] - m);
        return m + std::log(acc);
    }
    // Wilson-Hilferty for chi-square with nu = 2n at 2x
    double nu = 2.0 * n, z = (std::cbrt(2 * x / nu) - (1 - 2 / (9 * nu))) / std::sqrt(2 / (9 * nu));
    if (z < -5) return 0;
    if (z < 6) return std::log(0.5 * std::erfc(z / std::sqrt(2.0)));
    return -0.5 * z * z - std::log(z * std::sqrt(2 * PI)) + std::log1p(-1 / (z * z));
}

struct Keyed {
    int nt, P;
    std::vector<uint8_t> code;    // [NMSG][NSYM]
    std::vector<uint8_t> symof;   // [nt]
    std::vector<uint8_t> pos;     // [NMSG][nt]
    std::vector<uint8_t> qph;     // [NMSG][nt] quarter turns of the value's phase (all zero unless phasemap)
    Keyed(const Format& f, uint64_t seed, bool phasemap) : nt(NSYM * f.L), P(f.P) {
        Rng r(seed);
        code.resize(NMSG * NSYM);
        for (int j = 0; j < NSYM; ++j) {
            uint32_t g[8]; for (auto& x : g) x = (uint32_t)(r.next() & (NMSG - 1));
            uint8_t m = (uint8_t)(r.next() & 255);
            for (int c = 0; c < NMSG; ++c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); code[c * NSYM + j] = v ^ m; }
        }
        symof.resize(nt); for (int t = 0; t < nt; ++t) symof[t] = (uint8_t)(t % NSYM);
        for (int t = nt - 1; t > 0; --t) { int u = (int)(r.next() % (uint64_t)(t + 1)); std::swap(symof[t], symof[u]); }
        pos.resize((size_t)NMSG * nt); qph.assign((size_t)NMSG * nt, 0);
        int lp = 0; while ((1 << lp) < P) ++lp;
        std::vector<uint8_t> perm(256);
        for (int t = 0; t < nt; ++t) {
            for (int v = 0; v < 256; ++v) perm[v] = (uint8_t)v;
            for (int v = 255; v > 0; --v) { int u = (int)(r.next() % (uint64_t)(v + 1)); std::swap(perm[v], perm[u]); }
            for (int c = 0; c < NMSG; ++c) { uint8_t pv = perm[code[c * NSYM + symof[t]]]; pos[(size_t)c * nt + t] = (uint8_t)(pv & (P - 1)); if (phasemap) qph[(size_t)c * nt + t] = (uint8_t)((pv >> lp) & 3); }
        }
    }
};

// channel gain at M sub-samples of every cell
struct ChanGen {
    const Channel& ch; double tc; int ncell, M;
    ChanGen(const Channel& c, double tcell, int cells, int m) : ch(c), tc(tcell), ncell(cells), M(m) {}
    void draw(Rng& r, std::vector<cd>& h) const {
        size_t n = (size_t)ncell * M; h.resize(n); double dt = tc / M;
        if (ch.type == 0) { cd g = std::polar(1.0, 2 * PI * r.uni()); for (auto& x : h) x = g; }
        else if (ch.type == 1) {
            double sig = ch.p1 * PI / 180, rho = std::exp(-dt / ch.p2), q = std::sqrt(1 - rho * rho), ph = sig * r.gauss(), p0 = 2 * PI * r.uni();
            for (size_t i = 0; i < n; ++i) { h[i] = std::polar(1.0, p0 + ph); ph = rho * ph + q * sig * r.gauss(); }
        } else if (ch.type == 2) {
            double b = ch.p1, knot = 1 / b, ph = 2 * PI * r.uni(), f0 = b * (2 * r.uni() - 1), f1 = b * (2 * r.uni() - 1), tk = knot * r.uni();
            for (size_t i = 0; i < n; ++i) {
                double u = tk / knot, w = 0.5 - 0.5 * std::cos(PI * u), f = f0 + (f1 - f0) * w;
                h[i] = std::polar(1.0, ph); ph += 2 * PI * f * dt; tk += dt;
                if (tk >= knot) { tk -= knot; f0 = f1; f1 = b * (2 * r.uni() - 1); }
            }
        } else {
            const int NS = 48; double sf = ch.p1 / 2;  // sigma of the Gaussian spectrum
            std::vector<cd> ph(NS), rot(NS);
            for (int k = 0; k < NS; ++k) { ph[k] = std::polar(1.0, 2 * PI * r.uni()); rot[k] = std::polar(1.0, 2 * PI * sf * r.gauss() * dt); }
            double sc = 1 / std::sqrt((double)NS);
            for (size_t i = 0; i < n; ++i) { cd a = 0; for (int k = 0; k < NS; ++k) { a += ph[k]; ph[k] *= rot[k]; } h[i] = a * sc; }
        }
    }
};

struct Rung { double win_s; int nwin; };

int main(int argc, char** argv) {
    int trials = argc > 1 ? atoi(argv[1]) : 2000;
    std::string fsel = argc > 2 ? argv[2] : "all", csel = argc > 3 ? argv[3] : "all";
    double bant = argc > 4 ? atof(argv[4]) : 0;
    bool phasemap = argc > 5 && atoi(argv[5]) != 0;
    printf("# Sub-9 kHz FSK rate and spacing: keyed 10-bit block on a ladder of coherent windows\n\n");
    printf("Generated by `coherent_block_mc.cpp`, %d blocks per row. Every format lasts 5,242.88 s (87.4 min) and\n", trials);
    printf("carries the same energy. Entries are the block E/N0 in dB at which 1%% of blocks decode to a wrong\n");
    printf("message, for the receiver that adds coherent windows of the stated length as energies. `>` marks a\n");
    printf("rung that does not reach 1%% by 44 dB. Reference: 1,024 exactly orthogonal codewords, coherent with\n");
    printf("one unknown phase, need 12.87 dB. Known timing and frequency, Gaussian noise. Theoretical (Monte Carlo).\n");
    if (phasemap) printf("Value-dependent phase: the keyed map sends each of the 256 values to a position and one of four quarter\nturns (the next two bits of the keyed permutation), so two messages on the same position differ in\nphase three times in four. Per-tone and sub-tone rungs do not see the phase.\n");
    if (bant > 0) printf("Transmit antenna: single-tuned, -3 dB bandwidth %.1f Hz centred on the positions, static response only.\n", bant);
    printf("\n");
    for (const Format& f : FORMATS) {
        if (fsel != "all" && fsel.find(f.id) == std::string::npos) continue;
        double tc = 0.16 * (1 << f.j), spacing = 1 / tc; int nt = NSYM * f.L, ncell = nt * f.nc, P = f.P;
        Keyed key(f, 1234 + f.id, phasemap);
        // rungs: sub-tone (1 .. nc/2 cells), tone, 2..nt tones
        std::vector<Rung> rungs; int nsub = 0;
        for (int w = 1; w < f.nc; w *= 2) { rungs.push_back({w * tc, ncell / w}); ++nsub; }
        int ntop = 0; for (int w = 1; w <= nt; w *= 2) { rungs.push_back({w * f.nc * tc, nt / w}); ++ntop; }
        int nr = (int)rungs.size();
        std::vector<cd> hant(P, cd(1, 0));
        if (bant > 0) for (int p = 0; p < P; ++p) hant[p] = 1.0 / cd(1.0, 2 * (p - 0.5 * (P - 1)) * spacing / bant);
        printf("## Format %c: %s\n\n", f.id, f.name);
        printf("%d tones of %.2f s (%d cells of %.2f s), %d tones per symbol, positions %.4f Hz apart.\n\n", nt, f.nc * tc, f.nc, tc, f.L, spacing);
        printf("| channel |"); for (auto& r : rungs) { if (r.win_s < 100) printf(" %.4g s |", r.win_s); else printf(" %.3g min |", r.win_s / 60); } printf(" best rung | rung picked per block |\n");
        printf("|---|"); for (int i = 0; i < nr + 2; ++i) printf("---:|"); printf("\n");
        for (const Channel& ch : CHANNELS) {
            if (csel != "all" && csel.find(ch.id) == std::string::npos) continue;
            double spread = ch.type == 1 ? ch.p1 * PI / 180 / (2 * PI * ch.p2) : ch.p1;
            int M = spread * tc > 0.01 ? 16 : 1;
            ChanGen gen(ch, tc, ncell, M);
            std::vector<long> err((size_t)nr * NAMP, 0), errsel(NAMP, 0);
            #pragma omp parallel
            {
                Rng rng(99991ull * (omp_get_thread_num() + 1) + 7919ull * f.id + 31ull * ch.id);
                std::vector<cd> h; std::vector<cf> sig((size_t)ncell * P), noi((size_t)ncell * P);
                std::vector<cf> zs((size_t)nt * P), zn((size_t)nt * P), as(nt), an(nt);
                std::vector<float> ess((size_t)std::max(nsub, 1) * nt * P), esn(ess.size()), enn(ess.size());
                std::vector<float> coef((size_t)NMSG * nr * 3);
                std::vector<long> lerr((size_t)nr * NAMP, 0), lsel(NAMP, 0);
                std::vector<cd> ker(M > 1 ? (size_t)M * (2 * P - 1) : 0);
                if (M > 1) for (int d = -(P - 1); d <= P - 1; ++d) for (int m = 0; m < M; ++m) ker[(size_t)(d + P - 1) * M + m] = std::polar(1.0 / M, 2 * PI * d * (m + 0.5) / M);
                #pragma omp for schedule(dynamic, 4)
                for (int tr = 0; tr < trials; ++tr) {
                    int cstar = (int)(rng.next() & (NMSG - 1));
                    gen.draw(rng, h);
                    for (auto& x : noi) { cd z = rng.cn(); x = cf((float)z.real(), (float)z.imag()); }
                    std::fill(sig.begin(), sig.end(), cf(0, 0));
                    for (int t = 0; t < nt; ++t) {
                        int p = key.pos[(size_t)cstar * nt + t];
                        for (int c = 0; c < f.nc; ++c) {
                            size_t cell = (size_t)t * f.nc + c;
                            if (M == 1) { cd v = h[cell] * hant[p]; sig[cell * P + p] = cf((float)v.real(), (float)v.imag()); }
                            else for (int q = 0; q < P; ++q) {  // bin q of a tone at position p whose gain varies inside the cell
                                cd a = 0; const cd* kk = &ker[(size_t)(p - q + P - 1) * M];
                                for (int m = 0; m < M; ++m) a += h[cell * M + m] * kk[m];
                                a *= hant[p]; sig[cell * P + q] = cf((float)a.real(), (float)a.imag());
                            }
                        }
                    }
                    // tone sums and sub-tone energies
                    for (int t = 0; t < nt; ++t) for (int q = 0; q < P; ++q) {
                        cf s = 0, n = 0; size_t base = (size_t)t * f.nc;
                        for (int c = 0; c < f.nc; ++c) { s += sig[(base + c) * P + q]; n += noi[(base + c) * P + q]; }
                        zs[(size_t)t * P + q] = s; zn[(size_t)t * P + q] = n;
                        int ri = 0;
                        for (int w = 1; w < f.nc; w *= 2, ++ri) {
                            float a = 0, b = 0, d = 0;
                            for (int c0 = 0; c0 < f.nc; c0 += w) { cf ss = 0, nn = 0; for (int c = c0; c < c0 + w; ++c) { ss += sig[(base + c) * P + q]; nn += noi[(base + c) * P + q]; } a += std::norm(ss); b += 2 * (ss.real() * nn.real() + ss.imag() * nn.imag()); d += std::norm(nn); }
                            size_t e = ((size_t)ri * nt + t) * P + q; ess[e] = a; esn[e] = b; enn[e] = d;
                        }
                    }
                    for (int c = 0; c < NMSG; ++c) {
                        const uint8_t* pc = &key.pos[(size_t)c * nt]; float* co = &coef[(size_t)c * nr * 3];
                        for (int ri = 0; ri < nsub; ++ri) {
                            float a = 0, b = 0, d = 0; const float *ea = &ess[(size_t)ri * nt * P], *eb = &esn[(size_t)ri * nt * P], *ed = &enn[(size_t)ri * nt * P];
                            for (int t = 0; t < nt; ++t) { size_t e = (size_t)t * P + pc[t]; a += ea[e]; b += eb[e]; d += ed[e]; }
                            co[ri * 3] = a; co[ri * 3 + 1] = b; co[ri * 3 + 2] = d;
                        }
                        if (!phasemap) for (int t = 0; t < nt; ++t) { size_t e = (size_t)t * P + pc[t]; as[t] = zs[e]; an[t] = zn[e]; }
                        else {
                            static const cf ROT[4] = {cf(1, 0), cf(0, 1), cf(-1, 0), cf(0, -1)};
                            const uint8_t *qc = &key.qph[(size_t)c * nt], *qs = &key.qph[(size_t)cstar * nt];
                            for (int t = 0; t < nt; ++t) { size_t e = (size_t)t * P + pc[t]; as[t] = zs[e] * ROT[(qs[t] - qc[t]) & 3]; an[t] = zn[e] * ROT[(4 - qc[t]) & 3]; }
                        }
                        int len = nt;
                        for (int ri = nsub; ri < nr; ++ri) {
                            float a = 0, b = 0, d = 0;
                            for (int i = 0; i < len; ++i) { a += std::norm(as[i]); b += 2 * (as[i].real() * an[i].real() + as[i].imag() * an[i].imag()); d += std::norm(an[i]); }
                            co[ri * 3] = a; co[ri * 3 + 1] = b; co[ri * 3 + 2] = d;
                            len /= 2; for (int i = 0; i < len; ++i) { as[i] = as[2 * i] + as[2 * i + 1]; an[i] = an[2 * i] + an[2 * i + 1]; }
                        }
                    }
                    for (int ia = 0; ia < NAMP; ++ia) {
                        double a2 = std::pow(10.0, amp_db(ia) / 10) / ncell; float a = (float)std::sqrt(a2), aa = (float)a2;
                        double bestsig = 1e300; int bestmsg = -1;
                        for (int ri = 0; ri < nr; ++ri) {
                            float top = -1e30f; int arg = 0;
                            for (int c = 0; c < NMSG; ++c) { const float* co = &coef[((size_t)c * nr + ri) * 3]; float v = aa * co[0] + a * co[1] + co[2]; if (v > top) { top = v; arg = c; } }
                            if (arg != cstar) ++lerr[(size_t)ri * NAMP + ia];
                            double cells_per_win = (double)ncell / rungs[ri].nwin;
                            double lt = log_gamma_tail(rungs[ri].nwin, top / cells_per_win);
                            if (lt < bestsig) { bestsig = lt; bestmsg = arg; }
                        }
                        if (bestmsg != cstar) ++lsel[ia];
                    }
                }
                #pragma omp critical
                { for (size_t i = 0; i < err.size(); ++i) err[i] += lerr[i]; for (int i = 0; i < NAMP; ++i) errsel[i] += lsel[i]; }
            }
            auto at1 = [&](const long* e) -> double {  // E/N0 at 1% by log-linear interpolation
                double target = 0.01 * trials;
                for (int ia = 0; ia < NAMP; ++ia) if (e[ia] <= target) {
                    if (ia == 0) return amp_db(0);
                    double y0 = std::log((double)std::max(e[ia - 1], 1L)), y1 = std::log((double)std::max(e[ia], 1L) * (e[ia] ? 1 : 0.5)), yt = std::log(target);
                    double u = y0 > y1 ? (y0 - yt) / (y0 - y1) : 1; return amp_db(ia - 1) + 0.25 * u;
                }
                return 1e9;
            };
            printf("| %s |", ch.name); double best = 1e9; int bi = -1;
            for (int ri = 0; ri < nr; ++ri) { double v = at1(&err[(size_t)ri * NAMP]); if (v < best) { best = v; bi = ri; } if (v > 1e8) printf(" > |"); else printf(" %.1f |", v); }
            double vs = at1(errsel.data());
            if (bi >= 0) { if (rungs[bi].win_s < 100) printf(" %.1f (%.4g s) |", best, rungs[bi].win_s); else printf(" %.1f (%.3g min) |", best, rungs[bi].win_s / 60); } else printf(" > |");
            if (vs > 1e8) printf(" > |\n"); else printf(" %.1f |\n", vs);
            fflush(stdout);
        }
        printf("\n");
    }
    return 0;
}
