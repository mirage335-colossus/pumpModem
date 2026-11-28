// Matrix Modem FSK below 9 kHz: cost of one block decode on a CPU, per-tone and on the window ladder.
//
// The specification's block: k information bits, 32 symbols of 256 values, value_j = G_j c xor m_j.
// One "decode" scores all 2^k messages for one hypothesis (timing, frequency, drift).
//
//   energy   per-tone reception: Score[c] = sum_j E_j[value_j(c)], E_j the accumulated 4-bit
//            evidence of symbol j. 32 lookups per message whatever the number of tones.
//   fwht     the same scores by scattering the 32 tables' Walsh spectra into a 2^k array and one
//            fast Walsh-Hadamard transform (k 2^k additions, no table lookups in the inner loop).
//   ladder   coherent attempts on every window from two tones up to the whole block:
//            z_t(c) = B_t[value_sym(t)(c)], the complex 4-bit bin value (key undone) at the
//            position the keyed map gives that value in tone t. Windows of 2, 4, 8, ... tones are
//            summed coherently and added as energies; one maximum per rung. One lookup per
//            message and tone, so the work follows the number of tones in the block.
//   table    the ladder restricted to windows of 1/W of the block and longer (the specification's
//            default W = 64): complex table sums per (window, symbol, value), 32 W lookups per
//            message whatever the number of tones.
//
// Messages are split as c = (c_hi, c_lo); value_j(c) = Ghi_j[c_hi] xor Glo_j[c_lo]. The inner
// loops run over up to 512 values of c_lo so that all working data stay in cache.
//
// Timing. Work is cut into tasks of one (repetition, c_hi) each and all tasks of a measurement run
// under one dynamic parallel loop, so every thread is busy whatever k and the tone count: small
// blocks are repeated (as a receiver decoding many hypotheses would), large ones are timed on a
// sample of c_hi values and scaled to a full decode. Every measurement does at least `budget`
// lookups. The planted message has to come out on top of the whole-block rung and of the energy
// decode.
//
// Measurement of a calculation kernel with synthetic tables; not a receiver.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp decode_bench.cpp -o decode_bench
// Run:   ./decode_bench [threads, default all] [budget in units of 1e9 lookups, default 15]
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <omp.h>

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Rng { uint64_t s; uint64_t next() { s += 0x9E3779B97F4A7C15ull; uint64_t z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double uni() { return ((next() >> 11) + 0.5) / 9007199254740992.0; }
    double gauss() { return std::sqrt(-2 * std::log(uni())) * std::cos(6.283185307179586 * uni()); } };

static const int NSYM = 32, CH = 512, MAXR = 17;

struct Code {
    int k, lb, hb, nlo, nhi; std::vector<uint8_t> ghi, glo;  // [NSYM][nhi], [NSYM][nlo]
    uint32_t gm[NSYM][8]; uint8_t mm[NSYM];
    Code(int k_, Rng& r) : k(k_), lb(k_ / 2), hb(k_ - k_ / 2), nlo(1 << lb), nhi(1 << hb), ghi((size_t)NSYM << hb), glo((size_t)NSYM << lb) {
        for (int j = 0; j < NSYM; ++j) {
            uint32_t g[8]; for (auto& x : g) x = (uint32_t)(r.next() & ((1u << k) - 1)); uint8_t m = (uint8_t)r.next();
            for (int b = 0; b < 8; ++b) gm[j][b] = g[b]; mm[j] = m;
            auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); return v; };
            for (int h = 0; h < nhi; ++h) ghi[(size_t)j * nhi + h] = val((uint32_t)h << lb) ^ m;
            for (int l = 0; l < nlo; ++l) glo[(size_t)j * nlo + l] = val((uint32_t)l);
        }
    }
    uint8_t value(int j, uint32_t c) const { return ghi[(size_t)j * nhi + (c >> lb)] ^ glo[(size_t)j * nlo + (c & (nlo - 1))]; }
};

struct Plan { long reps; int hs; long chunk; double decodes; };
// lookups_per_task: work of one (repetition, c_hi) task; a full decode has nhi tasks
static Plan plan(const Code& c, double lookups_per_task, double budget) {
    Plan p; double tasks = std::max(budget / lookups_per_task, 4.0 * omp_get_max_threads());
    if (tasks <= c.nhi) { p.reps = 1; p.hs = (int)std::max(1.0, std::min((double)c.nhi, std::ceil(tasks))); }
    else { p.hs = c.nhi; p.reps = (long)std::ceil(tasks / c.nhi); }
    p.chunk = std::max(1L, (long)(2e6 / lookups_per_task));
    p.decodes = (double)p.reps * p.hs / c.nhi; return p;
}

int main(int argc, char** argv) {
    if (argc > 1 && atoi(argv[1]) > 0) omp_set_num_threads(atoi(argv[1]));
    double budget = (argc > 2 ? atof(argv[2]) : 15.0) * 1e9;
    int threads = omp_get_max_threads();
    printf("# Block decode cost on a CPU: per-tone, full window ladder and table ladder\n\n");
    printf("Generated by `decode_bench.cpp` with %d threads. One decode scores all 2^k messages of one hypothesis.\n", threads);
    printf("Every measurement keeps all threads busy with tasks of one (repetition, high half) and does at least\n");
    printf("%.3g lookups; small blocks are repeated, large ones are timed on a sample of high halves and scaled\n", budget);
    printf("(`timed` gives the number of decodes actually done). Synthetic 4-bit tables with a planted message.\n\n");
    Rng r{2026};
    for (int k : {10, 16, 24}) {
        Code code(k, r); const int nlo = code.nlo, nhi = code.nhi, ch = std::min(CH, nlo);
        printf("## k = %d bits (%d messages)\n\n", k, 1 << k);
        // ---------------- energy decode
        {
            std::vector<uint16_t> E((size_t)NSYM * 256);
            for (auto& e : E) e = (uint16_t)(r.next() % 4000);
            uint32_t pl = (uint32_t)(r.next() & ((1u << k) - 1));
            for (int j = 0; j < NSYM; ++j) E[(size_t)j * 256 + code.value(j, pl)] += 3000;
            Plan p = plan(code, 32.0 * nlo, budget); p.hs = nhi; p.reps = std::max(1L, (long)std::ceil(budget / (32.0 * (1 << k)))); p.decodes = (double)p.reps;
            long best = -1; uint32_t arg = 0; long ntask = p.reps * p.hs; double t0 = now();
            #pragma omp parallel
            {
                std::vector<uint32_t> acc(ch); long lb = -1; uint32_t la = 0;
                #pragma omp for schedule(dynamic, p.chunk) nowait
                for (long task = 0; task < ntask; ++task) {
                    int h = (int)(task % p.hs);
                    for (int c0 = 0; c0 < nlo; c0 += ch) {
                        std::fill(acc.begin(), acc.end(), 0u);
                        for (int j = 0; j < NSYM; ++j) { const uint16_t* e = &E[(size_t)j * 256]; uint8_t a = code.ghi[(size_t)j * nhi + h]; const uint8_t* g = &code.glo[(size_t)j * nlo + c0]; for (int i = 0; i < ch; ++i) acc[i] += e[a ^ g[i]]; }
                        for (int i = 0; i < ch; ++i) if ((long)acc[i] > lb) { lb = acc[i]; la = ((uint32_t)h << code.lb) | (uint32_t)(c0 + i); }
                    }
                }
                #pragma omp critical
                if (lb > best) { best = lb; arg = la; }
            }
            double dt = (now() - t0) / p.decodes;
            printf("Per-tone (energy) decode: %.4g s per decode, %.4g decodes/s, %.3g lookups/s; timed %.4g decodes; planted message %s.\n\n", dt, 1 / dt, 32.0 * (1 << k) / dt, p.decodes, arg == pl ? "found" : "NOT found");
        }
        // ---------------- energy decode by fast Walsh-Hadamard transform
        {
            std::vector<uint16_t> E((size_t)NSYM * 256);
            for (auto& e : E) e = (uint16_t)(r.next() % 4000);
            uint32_t pl = (uint32_t)(r.next() & ((1u << k) - 1));
            for (int j = 0; j < NSYM; ++j) E[(size_t)j * 256 + code.value(j, pl)] += 3000;
            size_t n = (size_t)1 << k; bool inner = k > 16;  // large transforms are threaded inside, small ones across repetitions
            long reps = inner ? 5 : std::max((long)threads * 4, (long)(2e9 / ((double)k * n)));
            std::vector<size_t> args(inner ? 1 : threads, 0);
            auto scatter = [&](std::vector<int32_t>& x) {
                std::fill(x.begin(), x.end(), 0);
                for (int j = 0; j < NSYM; ++j) {
                    int32_t f[256]; for (int w = 0; w < 256; ++w) f[w] = E[(size_t)j * 256 + (w ^ code.mm[j])];
                    for (int h = 1; h < 256; h <<= 1) for (int i = 0; i < 256; i += 2 * h) for (int q = i; q < i + h; ++q) { int32_t a = f[q], b = f[q + h]; f[q] = a + b; f[q + h] = a - b; }
                    for (int u = 0; u < 256; ++u) { uint32_t sidx = 0; for (int b = 0; b < 8; ++b) if (u >> b & 1) sidx ^= code.gm[j][b]; x[sidx] += f[u]; }
                }
            };
            double t0 = now(); size_t arg = 0;
            if (inner) {
                std::vector<int32_t> x(n);
                for (long rep = 0; rep < reps; ++rep) {
                    scatter(x); size_t blk = (size_t)1 << 13;
                    #pragma omp parallel for schedule(static)
                    for (size_t b0 = 0; b0 < n; b0 += blk) { int32_t* y = &x[b0]; for (size_t h = 1; h < blk; h <<= 1) for (size_t i = 0; i < blk; i += 2 * h) for (size_t q = i; q < i + h; ++q) { int32_t a = y[q], b = y[q + h]; y[q] = a + b; y[q + h] = a - b; } }
                    for (size_t h = blk; h < n; h <<= 1) {
                        #pragma omp parallel for schedule(static)
                        for (size_t i = 0; i < n; i += 2 * h) { int32_t *a = &x[i], *b = &x[i + h]; for (size_t q = 0; q < h; ++q) { int32_t u = a[q], v = b[q]; a[q] = u + v; b[q] = u - v; } }
                    }
                    int32_t best = INT32_MIN;
                    #pragma omp parallel
                    { int32_t lb = INT32_MIN; size_t la = 0;
                      #pragma omp for nowait
                      for (size_t i = 0; i < n; ++i) if (x[i] > lb) { lb = x[i]; la = i; }
                      #pragma omp critical
                      if (lb > best) { best = lb; arg = la; } }
                }
            } else {
                #pragma omp parallel
                {
                    std::vector<int32_t> x(n); size_t la = 0;
                    #pragma omp for schedule(dynamic, 1) nowait
                    for (long rep = 0; rep < reps; ++rep) {
                        scatter(x);
                        for (size_t h = 1; h < n; h <<= 1) for (size_t i = 0; i < n; i += 2 * h) for (size_t q = i; q < i + h; ++q) { int32_t a = x[q], b = x[q + h]; x[q] = a + b; x[q + h] = a - b; }
                        int32_t lb = INT32_MIN; for (size_t i = 0; i < n; ++i) if (x[i] > lb) { lb = x[i]; la = i; }
                    }
                    args[omp_get_thread_num()] = la;
                }
                arg = args[0];
            }
            double dt = (now() - t0) / reps;
            printf("Per-tone decode by fast Walsh-Hadamard transform: %.4g s per decode, %.4g decodes/s; timed %ld decodes; planted message %s.\n\n", dt, 1 / dt, reps, arg == pl ? "found" : "NOT found");
        }
        // ---------------- full ladder over tones
        printf("| tones in the block | ladder rungs | s per decode | decodes/s | message-tones/s | timed decodes | planted on the whole-block rung |\n|---:|---:|---:|---:|---:|---:|---|\n");
        for (int nt : {128, 512, 2048, 8192, 32768}) {
            uint32_t pl = (uint32_t)(r.next() & ((1u << k) - 1));
            Plan p = plan(code, (double)nlo * nt, budget);
            if (p.reps == 1 && p.hs < nhi) pl = (pl & (uint32_t)(nlo - 1)) | ((uint32_t)(r.next() % (uint64_t)p.hs) << code.lb);  // inside the timed part
            std::vector<uint8_t> sym(nt); for (int t = 0; t < nt; ++t) sym[t] = (uint8_t)(t % NSYM);
            for (int t = nt - 1; t > 0; --t) std::swap(sym[t], sym[r.next() % (uint64_t)(t + 1)]);
            std::vector<int8_t> zmap((size_t)nt * 512);  // [t][value][re, im]
            for (auto& z : zmap) z = (int8_t)std::clamp((int)std::lrint(2.2 * r.gauss()), -7, 7);
            for (int t = 0; t < nt; ++t) { int8_t& z = zmap[((size_t)t * 256 + code.value(sym[t], pl)) * 2]; z = (int8_t)std::min(7, z + 2); }
            int R = 0; for (int w = 2; w <= nt; w *= 2) ++R;
            std::vector<double> best(R, -1); std::vector<uint32_t> arg(R, 0);
            long ntask = p.reps * p.hs; double t0 = now();
            #pragma omp parallel
            {
                std::vector<int32_t> ar((size_t)(R + 1) * ch), ai((size_t)(R + 1) * ch); std::vector<float> en((size_t)R * ch);
                std::vector<double> lb(R, -1); std::vector<uint32_t> la(R, 0);
                #pragma omp for schedule(dynamic, p.chunk) nowait
                for (long task = 0; task < ntask; ++task) {
                    int h = (int)(task % p.hs);
                    for (int c0 = 0; c0 < nlo; c0 += ch) {
                        std::fill(ar.begin(), ar.end(), 0); std::fill(ai.begin(), ai.end(), 0); std::fill(en.begin(), en.end(), 0.f);
                        for (int t = 0; t < nt; ++t) {
                            int j = sym[t]; uint8_t a = code.ghi[(size_t)j * nhi + h]; const uint8_t* g = &code.glo[(size_t)j * nlo + c0]; const int8_t* row = &zmap[(size_t)t * 512];
                            int32_t *r0 = ar.data(), *i0 = ai.data();
                            for (int i = 0; i < ch; ++i) { int idx = (a ^ g[i]) * 2; r0[i] += row[idx]; i0[i] += row[idx + 1]; }
                            int lvl = 0; for (int tt = t + 1; (tt & 1) == 0; tt >>= 1, ++lvl) {  // close windows of 2^(lvl+1) tones
                                int32_t *rs = &ar[(size_t)lvl * ch], *is = &ai[(size_t)lvl * ch], *rd = &ar[(size_t)(lvl + 1) * ch], *id = &ai[(size_t)(lvl + 1) * ch]; float* e = &en[(size_t)lvl * ch];
                                for (int i = 0; i < ch; ++i) { float x = (float)rs[i], y = (float)is[i]; e[i] += x * x + y * y; rd[i] += rs[i]; id[i] += is[i]; rs[i] = 0; is[i] = 0; }
                            }
                        }
                        for (int l = 0; l < R; ++l) { const float* e = &en[(size_t)l * ch]; for (int i = 0; i < ch; ++i) if (e[i] > lb[l]) { lb[l] = e[i]; la[l] = ((uint32_t)h << code.lb) | (uint32_t)(c0 + i); } }
                    }
                }
                #pragma omp critical
                for (int l = 0; l < R; ++l) if (lb[l] > best[l]) { best[l] = lb[l]; arg[l] = la[l]; }
            }
            double dt = (now() - t0) / p.decodes;
            printf("| %d | %d | %.4g | %.4g | %.3g | %.4g | %s |\n", nt, R, dt, 1 / dt, (double)nt * (1 << k) / dt, p.decodes, arg[R - 1] == pl ? "yes" : "NO");
            fflush(stdout);
        }
        // ---------------- table ladder, W windows
        printf("\n| windows W (table ladder) | rungs | s per decode | decodes/s | lookups/s | timed decodes | planted on the whole-block rung |\n|---:|---:|---:|---:|---:|---:|---|\n");
        for (int W : {16, 64, 256}) {
            uint32_t pl = (uint32_t)(r.next() & ((1u << k) - 1));
            Plan p = plan(code, 32.0 * W * nlo, budget);
            if (p.reps == 1 && p.hs < nhi) pl = (pl & (uint32_t)(nlo - 1)) | ((uint32_t)(r.next() % (uint64_t)p.hs) << code.lb);
            std::vector<int16_t> tab((size_t)W * NSYM * 512);
            for (auto& z : tab) z = (int16_t)std::lrint(20 * r.gauss());
            for (int w = 0; w < W; ++w) for (int j = 0; j < NSYM; ++j) tab[(((size_t)w * NSYM + j) * 256 + code.value(j, pl)) * 2] += 12;
            int R = 0; for (int w = 1; w <= W; w *= 2) ++R;
            std::vector<double> best(R, -1); std::vector<uint32_t> arg(R, 0);
            long ntask = p.reps * p.hs; double t0 = now();
            #pragma omp parallel
            {
                std::vector<int32_t> ar((size_t)(R + 1) * ch), ai((size_t)(R + 1) * ch); std::vector<float> en((size_t)R * ch);
                std::vector<double> lb(R, -1); std::vector<uint32_t> la(R, 0);
                #pragma omp for schedule(dynamic, p.chunk) nowait
                for (long task = 0; task < ntask; ++task) {
                    int h = (int)(task % p.hs);
                    for (int c0 = 0; c0 < nlo; c0 += ch) {
                        std::fill(ar.begin(), ar.end(), 0); std::fill(ai.begin(), ai.end(), 0); std::fill(en.begin(), en.end(), 0.f);
                        for (int w = 0; w < W; ++w) {
                            int32_t *r0 = ar.data(), *i0 = ai.data();
                            for (int j = 0; j < NSYM; ++j) { uint8_t a = code.ghi[(size_t)j * nhi + h]; const uint8_t* g = &code.glo[(size_t)j * nlo + c0]; const int16_t* row = &tab[((size_t)w * NSYM + j) * 512];
                                for (int i = 0; i < ch; ++i) { int idx = (a ^ g[i]) * 2; r0[i] += row[idx]; i0[i] += row[idx + 1]; } }
                            int lvl = 0; for (int ww = w + 1;; ww >>= 1, ++lvl) {  // level 0 closes every window
                                int32_t *rs = &ar[(size_t)lvl * ch], *is = &ai[(size_t)lvl * ch], *rd = &ar[(size_t)(lvl + 1) * ch], *id = &ai[(size_t)(lvl + 1) * ch]; float* e = &en[(size_t)lvl * ch];
                                for (int i = 0; i < ch; ++i) { float x = (float)rs[i], y = (float)is[i]; e[i] += x * x + y * y; rd[i] += rs[i]; id[i] += is[i]; rs[i] = 0; is[i] = 0; }
                                if ((ww & 1) != 0 || lvl + 1 >= R) break;
                            }
                        }
                        for (int l = 0; l < R; ++l) { const float* e = &en[(size_t)l * ch]; for (int i = 0; i < ch; ++i) if (e[i] > lb[l]) { lb[l] = e[i]; la[l] = ((uint32_t)h << code.lb) | (uint32_t)(c0 + i); } }
                    }
                }
                #pragma omp critical
                for (int l = 0; l < R; ++l) if (lb[l] > best[l]) { best[l] = lb[l]; arg[l] = la[l]; }
            }
            double dt = (now() - t0) / p.decodes;
            printf("| %d | %d | %.4g | %.4g | %.3g | %.4g | %s |\n", W, R, dt, 1 / dt, 32.0 * W * (1 << k) / dt, p.decodes, arg[R - 1] == pl ? "yes" : "NO");
            fflush(stdout);
        }
        printf("\n");
    }
    return 0;
}
