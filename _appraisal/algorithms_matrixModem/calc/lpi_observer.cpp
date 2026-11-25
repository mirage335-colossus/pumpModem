// Unkeyed-observer detectability versus keyed reception, per symbol interval.
// Model (Rayleigh flat fading per symbol, independent across symbols):
//   The band holds M_obs time-frequency "tiles" per symbol; one tile carries the
//   symbol with mean energy Es (Es/N0 = rho).  Inside a tile the waveform is
//   spread over G complex dimensions (G = 1: a plain tone of duration T; G > 1:
//   a keyed noise-like waveform of bandwidth W = G/T).  The keyed receiver
//   despreads and sees all energy in one dimension; an unkeyed observer that
//   knows T, W and the tile grid can at best use the tile energy (G dims).
// Observer detectors over N symbols (Gaussian/CLT approximation on per-symbol
// statistics, Pfa = 1e-3, Pd = 0.9):
//   D1 radiometer: sum of all tile energies, noise level known exactly.
//   D2 exact per-symbol LRT for "one tile of M_obs is occupied", known noise
//      level and known rho (a clairvoyant observer; upper bound on D3).
//   D3 the same LRT after normalizing tile energies by their per-symbol mean,
//      which makes the null distribution independent of the noise level.
//   RX keyed presence: LRT over M_rx = 64 candidate dimensions (data unknown).
// Also: radiometer SNR wall for noise-level uncertainty +-u dB (Tandra-Sahai),
// and the symbols needed to decode 128 information bits at the keyed receiver
// (symbol-wise MI of 64-ary noncoherent orthogonal signaling, Rayleigh).
// Build: g++ -O3 -march=native -fopenmp -std=c++20 lpi_observer.cpp -o lpi_observer
#include <omp.h>
#include <string>
#include "mm_common.hpp"
using namespace mm;

// log of regularized lower incomplete gamma P(s, x) via series (s > 0).
static double log_gammp(double s, double x) {
  if (x <= 0) return -INFINITY;
  // series P(s,x) = x^s e^-x / Gamma(s+1) * sum_n x^n / ((s+1)...(s+n)), in log domain
  double lt = 0.0, mx = 0.0, acc = 1.0;  // log of current term (relative), running sum scaled by exp(mx)
  for (int n = 1; n < 20000; ++n) {
    lt += std::log(x / (s + n));
    if (lt > mx) { acc = acc * std::exp(mx - lt) + 1.0; mx = lt; }
    else acc += std::exp(lt - mx);
    if (lt < mx - 40 && n > x) break;
  }
  double lsum = mx + std::log(acc);
  double v = s * std::log(x) - x - std::lgamma(s + 1) + lsum;
  return std::min(v, 0.0);
}

// log LR of a tile energy E (noise-normalized) for Rayleigh signal of mean rho
// spread over G dims, vs noise-only Gamma(G).
static double log_lr(double E, double rho, int G) {
  double a = rho / (1 + rho);
  if (G == 1) return a * E - std::log1p(rho);
  // LR = e^{aE} P(G-1, aE) Gamma(G) / ((1+rho) (aE)^{G-1})
  double x = a * E;
  return x + log_gammp(G - 1, x) + std::lgamma((double)G) - std::log1p(rho) - (G - 1) * std::log(x);
}

static double gamma_draw(Rng& r, int G) {  // sum of G unit exponentials
  double s = 0;
  for (int i = 0; i < G; ++i) s += r.exp1();
  return s;
}

struct Mom { double m0, s0, m1, s1, d, sd; };

// Paired sampling: H0 and H1 share every noise tile except the occupied one,
// so the mean shift d = E[stat1 - stat0] is estimated with low variance.
template <class Stat>
static Mom moments(int Mobs, int G, double rho, Stat stat, int nsym, uint64_t seed) {
  double a0 = 0, b0 = 0, a1 = 0, b1 = 0, ad = 0, bd = 0;
#pragma omp parallel for reduction(+ : a0, b0, a1, b1, ad, bd)
  for (int t = 0; t < nsym; ++t) {
    Rng r(seed, t);
    std::vector<double> E(Mobs);
    for (int m = 0; m < Mobs; ++m) E[m] = gamma_draw(r, G);
    double v0 = stat(E);
    int k = (int)(r.uniform() * Mobs);
    E[k] = (1 + rho) * r.exp1() + (G > 1 ? gamma_draw(r, G - 1) : 0.0);
    double v1 = stat(E);
    a0 += v0; b0 += v0 * v0; a1 += v1; b1 += v1 * v1; ad += v1 - v0; bd += (v1 - v0) * (v1 - v0);
  }
  Mom m;
  m.m0 = a0 / nsym; m.s0 = std::sqrt(std::max(b0 / nsym - m.m0 * m.m0, 1e-300));
  m.m1 = a1 / nsym; m.s1 = std::sqrt(std::max(b1 / nsym - m.m1 * m.m1, 1e-300));
  m.d = ad / nsym; m.sd = std::sqrt(std::max(bd / nsym - m.d * m.d, 0.0) / nsym);  // std error of d
  return m;
}

static double nsym_needed(const Mom& m, double za, double zb) {
  double d = m.d;
  if (d <= 2.0 * m.sd) return INFINITY;  // shift not resolved by the simulation
  double n = (za * m.s0 + zb * m.s1) / d;
  return n * n;
}

static double mi_rayleigh(int M, double rho, int trials, uint64_t seed) {
  double acc = 0, w = rho / (1 + rho);
#pragma omp parallel for reduction(+ : acc)
  for (int t = 0; t < trials; ++t) {
    Rng r(seed, t);
    double L0 = w * (1 + rho) * r.exp1(), s = 0;
    for (int m = 1; m < M; ++m) s += std::exp(w * r.exp1() - L0);
    acc += std::log1p(s);
  }
  return std::log2((double)M) - acc / trials / kLn2;
}

int main() {
  const double za = qinv(1e-3), zb = qinv(0.1);
  const int nsym = 400000;
  const int Mrx = 64;
  const int Dtot = 352;  // 2200 Hz x 0.16 s complex dimensions per symbol interval
  printf("# Observer vs keyed receiver, Rayleigh per symbol, Pfa=1e-3, Pd=0.9 (CLT approx).\n");
  printf("# Dtot = %d dims per symbol interval (e.g. 2200 Hz x 160 ms); G dims per tile, M_obs = Dtot/G tiles.\n", Dtot);
  printf("# N = symbols of observation; 'msg128' = symbols for 128 info bits at ideal symbol-wise MI of %d-ary NC orthogonal.\n\n", Mrx);
  printf("| Es/N0 per symbol (dB) | G | M_obs | D1 radiometer N | D2 clairvoyant LRT N | D3 self-normalized LRT N | keyed presence N | msg128 N | D3 / msg128 |\n");
  printf("|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
  for (double rdb : {-10.0, -6.0, -3.0, 0.0, 3.0, 6.0, 10.0}) {
    double rho = undb(rdb);
    double I = mi_rayleigh(Mrx, rho, 40000, 99);
    double nmsg = 128.0 / I;
    for (int G : {1, 4, 16, 64, 352}) {
      int Mobs = Dtot / G;
      auto rad = [&](const std::vector<double>& E) { double s = 0; for (double e : E) s += e; return s; };
      auto lrt = [&](const std::vector<double>& E) {
        double mx = -1e300;
        std::vector<double> l(E.size());
        for (size_t i = 0; i < E.size(); ++i) { l[i] = log_lr(E[i], rho, G); mx = std::max(mx, l[i]); }
        double s = 0;
        for (double v : l) s += std::exp(v - mx);
        return mx + std::log(s / E.size());
      };
      auto lrt_norm = [&](const std::vector<double>& E) {
        double mean = 0;
        for (double e : E) mean += e;
        mean /= (E.size() * G);  // per-dimension noise estimate
        std::vector<double> En(E.size());
        for (size_t i = 0; i < E.size(); ++i) En[i] = E[i] / mean;
        return lrt(En);
      };
      Mom m1;
      m1.d = rho; m1.sd = 0; m1.s0 = std::sqrt((double)Mobs * G); m1.s1 = std::sqrt((double)Mobs * G + 2 * rho + rho * rho);
      (void)rad;
      Mom m2 = moments(Mobs, G, rho, lrt, nsym, 2);
      Mom m3 = (Mobs > 1) ? moments(Mobs, G, rho, lrt_norm, nsym, 3) : Mom{0, 1, 0, 1};
      // keyed presence: M_rx candidate single dims, Rayleigh, known noise
      auto rx = [&](const std::vector<double>& E) {
        double w = rho / (1 + rho), mx = -1e300;
        for (double e : E) mx = std::max(mx, w * e);
        double s = 0;
        for (double e : E) s += std::exp(w * e - mx);
        return mx + std::log(s / E.size());
      };
      Mom m4 = moments(Mrx, 1, rho, rx, nsym, 4);
      double n3 = (Mobs > 1) ? nsym_needed(m3, za, zb) : INFINITY;
      printf("| %+.0f | %d | %d | %.3g | %.3g | %s | %.3g | %.3g | %s |\n", rdb, G, Mobs, nsym_needed(m1, za, zb),
             nsym_needed(m2, za, zb), (Mobs > 1) ? (std::to_string(n3).substr(0, 8)).c_str() : "no position contrast",
             nsym_needed(m4, za, zb), nmsg, (Mobs > 1) ? (std::to_string(n3 / nmsg).substr(0, 7)).c_str() : "-");
    }
  }
  printf("\n## Radiometer SNR wall (Tandra-Sahai): noise level known only within +-u dB\n\n");
  printf("Band SNR below (10^{u/10} - 10^{-u/10}) cannot be detected by a radiometer at any observation time.\n\n");
  printf("| u (dB) | wall band SNR (dB) | equivalent C/N0 in 2.2 kHz (dB-Hz) | equivalent per-symbol Es/N0 at 160 ms (dB) |\n|---:|---:|---:|---:|\n");
  for (double u : {0.1, 0.25, 0.5, 1.0, 2.0}) {
    double w = undb(u) - undb(-u);
    printf("| %.2f | %.1f | %.1f | %.1f |\n", u, db(w), db(w * 2200), db(w * 2200 * 0.16));
  }
  return 0;
}
