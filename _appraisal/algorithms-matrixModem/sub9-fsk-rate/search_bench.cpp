// Matrix Modem FSK below 9 kHz: the whole hypothesis search of one block on a CPU, organised so
// that the frequency and drift hypotheses come from a transform across a message's tone sums and
// are not an outer loop around the decode.
//
// Measurement aid, 28 November 2026 (Fable-5.2 pass). Companion of search_gpu.cu (same block and
// definitions) and successor of the per-hypothesis counting in decode_bench.cpp.
//
// Block: 24 information bits, 32 symbols of 256 values, value_j = Ghi_j[c_hi] xor Glo_j[c_lo];
// N tones, each on one of 16 positions through a keyed balanced map redrawn per tone; a keyed
// interleaver assigns tones to symbols. For one timing hypothesis the receiver holds one complex
// 4-bit bin value per tone and position.
//
// Per message and timing hypothesis:
//   gather    u[n] = bin value of the position this message puts tone n on        (N lookups)
//   rung 0    whole block: for each drift hypothesis, de-chirp u, transform 2N points, take
//             |X[b]|^2 on the NB frequency bins half a reciprocal of the block apart
//   rung r    windows of N/2^r tones: transform each window on 2N/2^r points, add the windows'
//             energies bin by bin, take the best of the NB/2^r bins that rung needs; windows of
//             eight tones and fewer are evaluated directly on their one to three bins
// The planted message, frequency bin and drift have to come out on rung 0.
//
// The three parts are timed separately so that other reference classes (more bins, more drift
// hypotheses, more timing offsets) can be costed from them; search_ceiling.py does that.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp search_bench.cpp -o /tmp/search_bench
//        add -DUSE_FFTW -l:libfftw3f.so.3 to use FFTW's single-precision transforms (prototypes
//        are declared here because only the runtime library is installed on the host)
// Run:   /tmp/search_bench [threads] [tones] [frequency bins] [sampled blocks of 4096 messages]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <omp.h>

#ifdef USE_FFTW
extern "C" {
typedef float fftwf_complex[2];
typedef struct fftwf_plan_s* fftwf_plan;
fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex* in, fftwf_complex* out, int sign, unsigned flags);
void fftwf_execute_dft(const fftwf_plan p, fftwf_complex* in, fftwf_complex* out);
void* fftwf_malloc(size_t n);
}
#endif

static const int NSYM = 32, NV = 256, HB = 12, NH = 1 << HB, POS = 16, ND = 4, MAXR = 16;
static const double PI = 3.14159265358979323846;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// plain radix-2 transform on interleaved complex floats, used when FFTW is not linked
struct Radix2 {
  int n = 0; std::vector<int> rev; std::vector<float> wr, wi;
  void init(int n_) {
    n = n_; rev.assign(n, 0); int lg = 0; while ((1 << lg) < n) ++lg;
    for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i >> b & 1) r |= 1 << (lg - 1 - b); rev[i] = r; }
    wr.resize(n / 2); wi.resize(n / 2);
    for (int k = 0; k < n / 2; ++k) { wr[k] = (float)std::cos(-2 * PI * k / n); wi[k] = (float)std::sin(-2 * PI * k / n); }
  }
  void run(float* a) const {
    for (int i = 0; i < n; ++i) if (rev[i] > i) { std::swap(a[2 * i], a[2 * rev[i]]); std::swap(a[2 * i + 1], a[2 * rev[i] + 1]); }
    for (int len = 2; len <= n; len <<= 1) {
      const int half = len >> 1, step = n / len;
      for (int i = 0; i < n; i += len)
        for (int k = 0; k < half; ++k) {
          const float cr = wr[k * step], ci = wi[k * step];
          float* p = a + 2 * (i + k); float* q = a + 2 * (i + k + half);
          const float tr = q[0] * cr - q[1] * ci, ti = q[0] * ci + q[1] * cr;
          q[0] = p[0] - tr; q[1] = p[1] - ti; p[0] += tr; p[1] += ti;
        }
    }
  }
};

int main(int argc, char** argv) {
  const int threads = argc > 1 ? atoi(argv[1]) : omp_get_max_threads();
  const int N = argc > 2 ? atoi(argv[2]) : 512;
  const int NB0 = argc > 3 ? atoi(argv[3]) : 174;
  const int S = argc > 4 ? atoi(argv[4]) : 96;
  omp_set_num_threads(threads);
  int R = 0; while ((2 << R) <= N) ++R;                 // rungs 0..R-1, windows of N .. 2 tones
  if ((1 << R) != N || NB0 > 2 * N) { fprintf(stderr, "tones must be a power of two and bins at most 2N\n"); return 2; }
  std::vector<int> nb(R), L(R); std::vector<bool> direct(R);
  for (int r = 0; r < R; ++r) { L[r] = N >> r; nb[r] = std::max(1, (NB0 + (1 << r) - 1) >> r); direct[r] = L[r] <= 8; }

  std::mt19937_64 rng(2026);
  std::normal_distribution<double> gauss(0, 1);
  std::vector<uint8_t> ghi((size_t)NSYM * NH), glo((size_t)NSYM * NH);
  for (int j = 0; j < NSYM; ++j) {
    uint32_t g[8]; for (auto& x : g) x = (uint32_t)(rng() & 0xFFFFFF); const uint8_t m = (uint8_t)rng();
    auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); return v; };
    for (int h = 0; h < NH; ++h) { ghi[(size_t)j * NH + h] = val((uint32_t)h << HB) ^ m; glo[(size_t)j * NH + h] = val((uint32_t)h); }
  }
  auto value = [&](int j, uint32_t c) { return (uint8_t)(ghi[(size_t)j * NH + (c >> HB)] ^ glo[(size_t)j * NH + (c & (NH - 1))]); };
  std::vector<uint8_t> sym(N); for (int t = 0; t < N; ++t) sym[t] = (uint8_t)(t % NSYM);
  for (int t = N - 1; t > 0; --t) std::swap(sym[t], sym[rng() % (uint64_t)(t + 1)]);

  // planted message, frequency bin and drift; bin values per tone and value through the keyed map
  const uint32_t planted = (uint32_t)(rng() & 0xFFFFFF);
  const int f0 = -NB0 / 2 + (int)(rng() % (uint64_t)NB0), d0 = 2;
  const double gd[ND] = {-5.82, -1.94, 1.94, 5.82};      // frequency change over the block, in bins
  std::vector<int8_t> zmap((size_t)N * NV * 2);
  for (int t = 0; t < N; ++t) {
    uint8_t map[NV]; for (int v = 0; v < NV; ++v) map[v] = (uint8_t)(v % POS);
    for (int v = NV - 1; v > 0; --v) std::swap(map[v], map[rng() % (uint64_t)(v + 1)]);
    double zr[POS], zi[POS];
    for (int p = 0; p < POS; ++p) { zr[p] = 2.2 * gauss(rng); zi[p] = 2.2 * gauss(rng); }
    const double x = (double)t / N, ph = PI * f0 * x + PI * gd[d0] * x * x / 2;
    const int pp = map[value(sym[t], planted)];
    zr[pp] += 2.5 * std::cos(ph); zi[pp] += 2.5 * std::sin(ph);
    for (int v = 0; v < NV; ++v) {
      zmap[((size_t)t * NV + v) * 2] = (int8_t)std::max(-7, std::min(7, (int)std::lrint(zr[map[v]])));
      zmap[((size_t)t * NV + v) * 2 + 1] = (int8_t)std::max(-7, std::min(7, (int)std::lrint(zi[map[v]])));
    }
  }
  std::vector<float> chirp((size_t)ND * N * 2);
  for (int d = 0; d < ND; ++d) for (int t = 0; t < N; ++t) { const double x = (double)t / N, a = -PI * gd[d] * x * x / 2; chirp[((size_t)d * N + t) * 2] = (float)std::cos(a); chirp[((size_t)d * N + t) * 2 + 1] = (float)std::sin(a); }
  std::vector<std::vector<float>> rot(R);                // direct rungs: e^{-j pi b i / L}
  for (int r = 0; r < R; ++r) if (direct[r]) { rot[r].resize((size_t)nb[r] * L[r] * 2); for (int bi = 0; bi < nb[r]; ++bi) for (int i = 0; i < L[r]; ++i) { const double a = -PI * (bi - nb[r] / 2) * i / L[r]; rot[r][((size_t)bi * L[r] + i) * 2] = (float)std::cos(a); rot[r][((size_t)bi * L[r] + i) * 2 + 1] = (float)std::sin(a); } }

#ifdef USE_FFTW
  std::vector<fftwf_plan> plan(R, nullptr);
  { fftwf_complex* tmp = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * 2 * N);
    for (int r = 0; r < R; ++r) if (!direct[r]) plan[r] = fftwf_plan_dft_1d(2 * L[r], tmp, tmp, -1, 0U); }
  const char* fftname = "FFTW 3 single precision, measured plans";
#else
  std::vector<Radix2> plan(R); for (int r = 0; r < R; ++r) if (!direct[r]) plan[r].init(2 * L[r]);
  const char* fftname = "plain radix-2 transform of this program";
#endif

  // sampled blocks of 4096 messages; the first one holds the planted message
  std::vector<int> blocks(S); blocks[0] = (int)(planted >> HB);
  for (int i = 1; i < S; ++i) blocks[i] = (int)(rng() % (uint64_t)NH);

  struct Out { double secs; float best[MAXR]; uint32_t msg; int bin, drift; double sink; };
  auto pass = [&](int upto) {                            // upto: 0 gather only, 1 rung 0, 2 all rungs
    Out o{}; for (auto& b : o.best) b = -1; double sink = 0;
    const double t0 = now();
#pragma omp parallel reduction(+ : sink)
    {
#ifdef USE_FFTW
      float* buf = (float*)fftwf_malloc(sizeof(float) * 4 * N);
#else
      std::vector<float> bufv(4 * N); float* buf = bufv.data();
#endif
      std::vector<float> u(2 * N), acc(2 * N);
      float tb[MAXR]; for (auto& b : tb) b = -1; uint32_t tm = 0; int tbin = 0, tdr = 0;
#pragma omp for schedule(dynamic, 1)
      for (int s = 0; s < S; ++s) {
        const int chi = blocks[s]; uint8_t gh[NSYM]; for (int j = 0; j < NSYM; ++j) gh[j] = ghi[(size_t)j * NH + chi];
        for (int clo = 0; clo < NH; ++clo) {
          uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[(size_t)j * NH + clo];
          for (int t = 0; t < N; ++t) { const int8_t* z = &zmap[((size_t)t * NV + val[sym[t]]) * 2]; u[2 * t] = z[0]; u[2 * t + 1] = z[1]; }
          if (upto == 0) { sink += u[0] + u[2 * N - 1]; continue; }
          for (int d = 0; d < ND; ++d) {                 // rung 0
            const float* c = &chirp[(size_t)d * N * 2];
            for (int t = 0; t < N; ++t) { buf[2 * t] = u[2 * t] * c[2 * t] - u[2 * t + 1] * c[2 * t + 1]; buf[2 * t + 1] = u[2 * t] * c[2 * t + 1] + u[2 * t + 1] * c[2 * t]; }
            std::fill(buf + 2 * N, buf + 4 * N, 0.f);
#ifdef USE_FFTW
            fftwf_execute_dft(plan[0], (fftwf_complex*)buf, (fftwf_complex*)buf);
#else
            plan[0].run(buf);
#endif
            for (int bi = 0; bi < nb[0]; ++bi) { const int k = (bi - nb[0] / 2 + 2 * N) % (2 * N); const float e = buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1]; if (e > tb[0]) { tb[0] = e; tm = ((uint32_t)chi << HB) | (uint32_t)clo; tbin = bi - nb[0] / 2; tdr = d; } }
          }
          if (upto == 1) continue;
          for (int r = 1; r < R; ++r) {
            const int Lr = L[r], W = 1 << r, n2 = 2 * Lr, nbr = nb[r];
            std::fill(acc.begin(), acc.begin() + nbr, 0.f);
            if (direct[r]) {
              const float* ro = rot[r].data();
              for (int w = 0; w < W; ++w) for (int bi = 0; bi < nbr; ++bi) {
                float sr = 0, si = 0; const float* q = ro + (size_t)bi * Lr * 2; const float* x = &u[(size_t)w * Lr * 2];
                for (int i = 0; i < Lr; ++i) { sr += x[2 * i] * q[2 * i] - x[2 * i + 1] * q[2 * i + 1]; si += x[2 * i] * q[2 * i + 1] + x[2 * i + 1] * q[2 * i]; }
                acc[bi] += sr * sr + si * si;
              }
            } else {
              for (int w = 0; w < W; ++w) {
                std::copy(&u[(size_t)w * Lr * 2], &u[(size_t)(w + 1) * Lr * 2], buf); std::fill(buf + 2 * Lr, buf + 4 * Lr, 0.f);
#ifdef USE_FFTW
                fftwf_execute_dft(plan[r], (fftwf_complex*)buf, (fftwf_complex*)buf);
#else
                plan[r].run(buf);
#endif
                for (int bi = 0; bi < nbr; ++bi) { const int k = (bi - nbr / 2 + n2) % n2; acc[bi] += buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1]; }
              }
            }
            for (int bi = 0; bi < nbr; ++bi) if (acc[bi] > tb[r]) tb[r] = acc[bi];
          }
        }
      }
#pragma omp critical
      { for (int r = 0; r < R; ++r) if (tb[r] > o.best[r]) { o.best[r] = tb[r]; if (r == 0) { o.msg = tm; o.bin = tbin; o.drift = tdr; } } }
    }
    o.secs = now() - t0; o.sink = sink; return o;
  };

  pass(0);                                               // warm up threads and caches
  const Out a = pass(0), b = pass(1), c = pass(2);
  const double msgs = (double)S * NH, M24 = 16777216.0;
  const double tg = a.secs / msgs, t0 = (b.secs - a.secs) / msgs / ND, tr = (c.secs - b.secs) / msgs, tall = c.secs / msgs;
  printf("# Hypothesis search by a transform across tone sums, CPU\n\n");
  printf("threads %d, tones %d, frequency bins on the whole block %d, drift hypotheses %d, rungs %d (windows of %d to 2 tones)\n", threads, N, NB0, ND, R, N);
  printf("transform: %s\n", fftname);
  printf("timed on %d blocks of 4096 messages (%.0f messages), all threads busy\n\n", S, msgs);
  printf("planted message %06x, frequency bin %d, drift hypothesis %d; found %06x, bin %d, drift %d: %s\n\n", planted, f0, d0, c.msg, c.bin, c.drift,
         (c.msg == planted && c.bin == f0 && c.drift == d0 && b.msg == planted) ? "correct" : "WRONG");
  printf("| Part, per message and timing hypothesis | Time | Rate |\n| --- | ---: | ---: |\n");
  printf("| Gather of %d tone values | %.3f us | %.3g tone lookups/s |\n", N, tg * 1e6, N / tg);
  printf("| Rung 0, per drift hypothesis: de-chirp, transform of %d points, %d bin energies | %.3f us | %.3g transforms/s |\n", 2 * N, nb[0], t0 * 1e6, 1 / t0);
  printf("| Rungs 1 to %d together | %.3f us | |\n", R - 1, tr * 1e6);
  printf("| All of it with %d drift hypotheses | %.3f us | %.3g messages/s |\n\n", ND, tall * 1e6, 1 / tall);
  long hyp = (long)ND * nb[0]; for (int r = 1; r < R; ++r) hyp += nb[r];
  printf("frequency and drift hypotheses evaluated per message and timing: %d x %d on the whole block, %ld with the shorter rungs\n\n", nb[0], ND, hyp);
  const double D = N * 5242.88 / 512.0 * (512.0 / N) * 1.0;   // block of 87.4 minutes whatever the tone count
  printf("One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of %.0f s:\n\n", D);
  printf("| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |\n| --- | ---: | ---: | ---: |\n");
  for (int k : {24, 16, 10}) { const double one = tall * M24 / (double)(1 << (24 - k)); printf("| %d bits | %.4g s | %.4g s | %.4g |\n", k, one, 20 * one, D / (20 * one)); }
  printf("\nThe 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.\n");
  printf("\nbest energy per rung:"); for (int r = 0; r < R; ++r) printf(" %.4g", c.best[r]); printf("\n");
  return 0;
}
