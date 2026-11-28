// Matrix Modem FSK: hypotheses whose evidence differs per hypothesis (spread bank, timing offsets
// of per-tone reception, fine timing steps of a coherent attempt) scored in one pass over the
// messages with vector table entries, against one pass over the messages per hypothesis.
//
// Measurement aid, 28 November 2026 (Fable-5.2 pass). It tests rule 2 of the specification's
// section 13.1 for the hypothesis dimensions that are not a transform.
//
// Block: 24 information bits, 32 symbols of 256 values, value_j = Ghi_j[c_hi] xor Glo_j[c_lo].
// Every hypothesis h has its own table of one entry per symbol and value.
//
//   per hypothesis   for each hypothesis: enumerate the messages, form the 32 symbol values of each,
//                    look 32 entries up in that hypothesis's table, keep the best message (the
//                    per-tone kernel of decode_bench.cpp, with its loop order)
//   one pass         enumerate the messages once; the entries of all hypotheses for a symbol value
//                    lie together, so a lookup returns a vector over the hypotheses and the 32
//                    additions are vector additions; keep the best message for every hypothesis
//
// Both sides run symbols in the outer loop and a chunk of consecutive messages inside, which is the
// faster order for both. Real entries (per-tone evidence, sums compared directly) and complex
// entries (coherent sums, energies compared). The planted message has to be the best one for every hypothesis.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp hypothesis_pass_bench.cpp -o /tmp/hypothesis_pass_bench
// Run:   /tmp/hypothesis_pass_bench [threads] > results/hypothesis_pass_bench.md

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <omp.h>

static const int NSYM = 32, NV = 256, HB = 12, NH = 1 << HB;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

template <class T> struct Aligned {
  T* p = nullptr; size_t n = 0;
  explicit Aligned(size_t n_) : n(n_) { p = (T*)std::aligned_alloc(64, ((n * sizeof(T) + 63) / 64) * 64); std::fill(p, p + n, T(0)); }
  ~Aligned() { std::free(p); }
  T& operator[](size_t i) { return p[i]; }
  const T& operator[](size_t i) const { return p[i]; }
};

int main(int argc, char** argv) {
  const int threads = argc > 1 ? atoi(argv[1]) : omp_get_max_threads();
  omp_set_num_threads(threads);
  std::mt19937_64 rng(2026);
  std::vector<uint8_t> ghi((size_t)NSYM * NH), glo((size_t)NSYM * NH);
  for (int j = 0; j < NSYM; ++j) {
    uint32_t g[8]; for (auto& x : g) x = (uint32_t)(rng() & 0xFFFFFF); const uint8_t m = (uint8_t)rng();
    auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); return v; };
    for (int h = 0; h < NH; ++h) { ghi[(size_t)j * NH + h] = val((uint32_t)h << HB) ^ m; glo[(size_t)j * NH + h] = val((uint32_t)h); }
  }
  auto value = [&](int j, uint32_t c) { return (uint8_t)(ghi[(size_t)j * NH + (c >> HB)] ^ glo[(size_t)j * NH + (c & (NH - 1))]); };
  const uint32_t planted = (uint32_t)(rng() & 0xFFFFFF);
  const double M = 16777216.0;

  printf("# One pass over the messages for many hypotheses, CPU\n\n");
  printf("threads %d; 24-bit block, all 2^24 messages enumerated in every timed pass; synthetic tables with a\n", threads);
  printf("planted message that has to win for every hypothesis. \"Per hypothesis\" is one enumeration of the\n");
  printf("messages for each hypothesis, timed on 8 hypotheses and multiplied; \"one pass\" enumerates them once.\n\n");

  for (int cplx = 0; cplx < 2; ++cplx) {
    // ---- baseline: one enumeration of the messages per hypothesis, 8 tables. Loop order as in
    // decode_bench.cpp: symbols outside, a chunk of consecutive messages inside.
    const int HBASE = 8, CH = 256;
    std::vector<int16_t> tb((size_t)HBASE * NSYM * NV * (cplx ? 2 : 1));
    for (auto& x : tb) x = (int16_t)(rng() % 400) - (cplx ? 200 : 0);
    for (int h = 0; h < HBASE; ++h) for (int j = 0; j < NSYM; ++j) tb[(((size_t)h * NSYM + j) * NV + value(j, planted)) * (cplx ? 2 : 1)] += 300;
    int okb = 0; double tbase = 0;
    for (int rep = 0; rep < 3; ++rep) {
      okb = 0; const double t0 = now();
      for (int h = 0; h < HBASE; ++h) {
        long bestv = -1; uint32_t bestm = 0;
#pragma omp parallel
        {
          long tv = -1; uint32_t tm = 0; std::vector<int> ar(CH), ai(CH);
#pragma omp for schedule(dynamic, 16)
          for (int chi = 0; chi < NH; ++chi) {
            const int16_t* t = &tb[(size_t)h * NSYM * NV * (cplx ? 2 : 1)];
            for (int c0 = 0; c0 < NH; c0 += CH) {
              std::fill(ar.begin(), ar.end(), 0); std::fill(ai.begin(), ai.end(), 0);
              for (int j = 0; j < NSYM; ++j) {
                const uint8_t a = ghi[(size_t)j * NH + chi]; const uint8_t* g = &glo[(size_t)j * NH + c0];
                if (!cplx) { const int16_t* e = t + (size_t)j * NV; for (int i = 0; i < CH; ++i) ar[i] += e[a ^ g[i]]; }
                else { const int16_t* e = t + (size_t)j * NV * 2; for (int i = 0; i < CH; ++i) { const int16_t* z = e + (size_t)(a ^ g[i]) * 2; ar[i] += z[0]; ai[i] += z[1]; } }
              }
              for (int i = 0; i < CH; ++i) { const long sc = cplx ? (long)ar[i] * ar[i] + (long)ai[i] * ai[i] : (long)ar[i]; if (sc > tv) { tv = sc; tm = ((uint32_t)chi << HB) | (uint32_t)(c0 + i); } }
            }
          }
#pragma omp critical
          if (tv > bestv) { bestv = tv; bestm = tm; }
        }
        okb += bestm == planted;
      }
      tbase = (now() - t0) / HBASE;                     // seconds per hypothesis, all messages
    }
    printf("## %s entries\n\n", cplx ? "Complex" : "Real");
    printf("Per hypothesis: %.4f s for all messages (%.2f ns per message and hypothesis); planted found in %d of %d.\n\n", tbase, tbase / M * 1e9, okb, HBASE);
    printf("| Hypotheses | One pass, all messages | Per message and hypothesis | Per hypothesis would take | One pass is faster by | Planted found for |\n");
    printf("| ---: | ---: | ---: | ---: | ---: | --- |\n");
    for (int H : {7, 56, 350}) {
      const int Hp = (H + 7) / 8 * 8;                   // padded to whole vectors of eight
      const int planes = cplx ? 2 : 1;
      const int CM = std::max(8, std::min(256, 24000 / (Hp * 2 * planes)) / 8 * 8);   // messages per chunk
      Aligned<int16_t> tv((size_t)planes * NSYM * NV * Hp);
      for (int pl = 0; pl < planes; ++pl) for (int j = 0; j < NSYM; ++j) for (int v = 0; v < NV; ++v) for (int h = 0; h < H; ++h)
        tv[(((size_t)pl * NSYM + j) * NV + v) * Hp + h] = (int16_t)((int)(rng() % 400) - (cplx ? 200 : 0));
      for (int j = 0; j < NSYM; ++j) for (int h = 0; h < H; ++h) tv[((size_t)j * NV + value(j, planted)) * Hp + h] += 300;
      std::vector<int> bestv(H); std::vector<uint32_t> bestm(H); double secs = 0;
      for (int rep = 0; rep < 3; ++rep) {
        std::fill(bestv.begin(), bestv.end(), -1);
        const double t0 = now();
#pragma omp parallel
        {
          Aligned<int16_t> ar((size_t)CM * Hp), ai((size_t)CM * Hp); Aligned<int> tb_(Hp); std::vector<uint32_t> tm(Hp, 0);
          for (int h = 0; h < Hp; ++h) tb_[h] = -1;
#pragma omp for schedule(dynamic, 16)
          for (int chi = 0; chi < NH; ++chi) {
            for (int c0 = 0; c0 < NH; c0 += CM) {
              const int cm = std::min(CM, NH - c0);
              std::fill(ar.p, ar.p + (size_t)cm * Hp, (int16_t)0); if (cplx) std::fill(ai.p, ai.p + (size_t)cm * Hp, (int16_t)0);
              for (int j = 0; j < NSYM; ++j) {
                const uint8_t a0 = ghi[(size_t)j * NH + chi]; const uint8_t* g = &glo[(size_t)j * NH + c0];
                for (int i = 0; i < cm; ++i) {
                  const size_t row = (size_t)j * NV + (a0 ^ g[i]);
                  const int16_t* x = &tv.p[row * Hp]; int16_t* a = ar.p + (size_t)i * Hp;
#pragma omp simd
                  for (int h = 0; h < Hp; ++h) a[h] += x[h];
                  if (cplx) { const int16_t* y = &tv.p[((size_t)NSYM * NV + row) * Hp]; int16_t* b = ai.p + (size_t)i * Hp;
#pragma omp simd
                    for (int h = 0; h < Hp; ++h) b[h] += y[h]; }
                }
              }
              int* tbp = tb_.p;
              for (int i = 0; i < cm; ++i) {
                const int16_t* a = ar.p + (size_t)i * Hp; const int16_t* b = ai.p + (size_t)i * Hp; int any = 0;
                if (cplx) {
#pragma omp simd reduction(| : any)
                  for (int h = 0; h < Hp; ++h) any |= ((int)a[h] * a[h] + (int)b[h] * b[h]) > tbp[h];
                } else {
#pragma omp simd reduction(| : any)
                  for (int h = 0; h < Hp; ++h) any |= (int)a[h] > tbp[h];
                }
                if (any) for (int h = 0; h < H; ++h) { const int e = cplx ? (int)a[h] * a[h] + (int)b[h] * b[h] : (int)a[h]; if (e > tbp[h]) { tbp[h] = e; tm[h] = ((uint32_t)chi << HB) | (uint32_t)(c0 + i); } }
              }
            }
          }
#pragma omp critical
          for (int h = 0; h < H; ++h) if (tb_[h] > bestv[h]) { bestv[h] = tb_[h]; bestm[h] = tm[h]; }
        }
        secs = now() - t0;
      }
      int ok = 0; for (int h = 0; h < H; ++h) ok += bestm[h] == planted;
      printf("| %d | %.3f s | %.2f ns | %.2f s | %.1f | %d of %d |\n", H, secs, secs / M / H * 1e9, tbase * H, tbase * H / secs, ok, H);
      fflush(stdout);
    }
    printf("\n");
  }
  printf("Table size in the one pass: 32 symbols x 256 values x hypotheses x 2 bytes (x 2 for complex):\n");
  printf("0.1 MB for 7 real hypotheses, 0.9 MB for 56, 5.7 MB for 350, twice that for complex entries.\n");
  return 0;
}
