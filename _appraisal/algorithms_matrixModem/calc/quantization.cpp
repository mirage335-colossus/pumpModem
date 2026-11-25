// Four-bit (and other) quantization losses relevant to an INT4 matrix receiver.
// (a) Weak-signal correlation efficiency of a memoryless quantizer Q applied
//     to noise-dominated Gaussian samples: eta = E[xQ(x)]^2 / (E[x^2] E[Q^2]).
//     This is the classic radio-astronomy "quantization efficiency": the SNR of
//     a long correlation against a weak signal is multiplied by eta.
// (b) Template (reference tone) quantization efficiency:
//     |sum s* q|^2 / (sum|s|^2 sum|q|^2), I and Q quantized to a codebook.
// (c) Monte Carlo check: deflection of |z|^2 for a weak tone in noise when both
//     data and template are 4-bit, relative to unquantized.
// (d) Requantizing per-tone energies to 16 levels before a decision/decoding
//     GEMM: symbol-wise MI of noncoherent M-FSK (rank-1 Rayleigh) with
//     quantized cell energies (exact bin likelihoods) vs. unquantized.
// Build: g++ -O3 -march=native -fopenmp -std=c++20 quantization.cpp -o quantization
#include <omp.h>
#include <complex>
#include <functional>
#include "mm_common.hpp"
using namespace mm;

// Quantizer described by sorted output levels; thresholds at midpoints (MMSE
// would use centroids; midpoint thresholds model integer rounding).
struct Quant {
  std::vector<double> lv;  // levels in units of the scale
  double scale;
  double q(double x) const {
    double u = x / scale;
    // nearest level
    auto it = std::lower_bound(lv.begin(), lv.end(), u);
    if (it == lv.begin()) return lv.front() * scale;
    if (it == lv.end()) return lv.back() * scale;
    double hi = *it, lo = *(it - 1);
    return ((u - lo) < (hi - u) ? lo : hi) * scale;
  }
};

static double eff_gauss(const Quant& Q) {
  // numerical integration over N(0,1)
  double sxq = 0, sqq = 0, h = 1e-3;
  for (double x = -9; x <= 9; x += h) {
    double p = phi(x) * h, v = Q.q(x);
    sxq += x * v * p;
    sqq += v * v * p;
  }
  return sxq * sxq / sqq;
}

static std::pair<double, double> best_scale(std::vector<double> lv) {
  double best = 0, bs = 0;
  for (double s = 0.02; s <= 3.0; s *= 1.01) {
    Quant Q{lv, s};
    double e = eff_gauss(Q);
    if (e > best) { best = e; bs = s; }
  }
  return {best, bs};
}

static std::vector<double> range_levels(int lo, int hi, double offset = 0) {
  std::vector<double> v;
  for (int k = lo; k <= hi; ++k) v.push_back(k + offset);
  return v;
}

int main() {
  printf("## (a) Weak-signal correlation efficiency, Gaussian input, optimal scale\n\n");
  printf("| quantizer | levels | eta | loss (dB) | best step / sigma |\n|---|---:|---:|---:|---:|\n");
  struct QDef { const char* name; std::vector<double> lv; };
  std::vector<double> fp4 = {-6, -4, -3, -2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2, 3, 4, 6};
  std::vector<QDef> qs = {
      {"1-bit sign", {-0.5, 0.5}},
      {"2-bit midrise {+-1,+-3}/2", {-1.5, -0.5, 0.5, 1.5}},
      {"3-bit midrise (8 levels)", range_levels(-4, 3, 0.5)},
      {"INT3 two's complement -4..3", range_levels(-4, 3)},
      {"4-bit midrise (16 levels)", range_levels(-8, 7, 0.5)},
      {"INT4 two's complement -8..7", range_levels(-8, 7)},
      {"INT4 symmetric -7..7", range_levels(-7, 7)},
      {"FP4 E2M1 (15 values)", fp4},
      {"E2M1 integer subset {0,+-1,+-2,+-3,+-4,+-6}", {-6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6}},
      {"INT4/E2M1 common integers -4..4", range_levels(-4, 4)},
      {"INT8 symmetric -127..127", range_levels(-127, 127)},
  };
  for (auto& d : qs) {
    auto [e, s] = best_scale(d.lv);
    printf("| %s | %zu | %.4f | %.3f | %.3f |\n", d.name, d.lv.size(), e, -db(e), s);
  }

  printf("\n## (b) Tone-template quantization efficiency (I and Q quantized separately)\n\n");
  printf("| codebook | amplitude (codebook units) | efficiency | loss (dB) |\n|---|---:|---:|---:|\n");
  Rng rng(42);
  auto template_eff = [&](const std::vector<double>& lv, double amp) {
    double acc = 0;
    int trials = 400, N = 512;
    for (int t = 0; t < trials; ++t) {
      double f = rng.uniform() * 0.5, ph = rng.uniform() * 2 * kPi;
      std::complex<double> c = 0;
      double ss = 0, qq = 0;
      Quant Q{lv, 1.0};
      for (int n = 0; n < N; ++n) {
        std::complex<double> s = std::polar(1.0, 2 * kPi * f * n + ph);
        std::complex<double> q(Q.q(amp * s.real()), Q.q(amp * s.imag()));
        c += std::conj(s) * q;
        ss += std::norm(s);
        qq += std::norm(q);
      }
      acc += std::norm(c) / (ss * qq);
    }
    return acc / trials;
  };
  for (double amp : {3.5, 5.0, 6.0, 7.0, 7.5, 8.0}) {
    double e = template_eff(range_levels(-7, 7), amp);
    printf("| INT4 symmetric -7..7 | %.1f | %.4f | %.3f |\n", amp, e, -db(e));
  }
  for (double amp : {4.0, 5.0, 6.0, 7.0}) {
    double e = template_eff(fp4, amp);
    printf("| FP4 E2M1 | %.1f | %.4f | %.3f |\n", amp, e, -db(e));
  }
  for (double amp : {3.0, 4.0, 4.5}) {
    double e = template_eff(range_levels(-4, 4), amp);
    printf("| common integers -4..4 | %.1f | %.4f | %.3f |\n", amp, e, -db(e));
  }
  for (double amp : {1.0, 1.5}) {
    double e = template_eff(range_levels(-1, 1), amp);
    printf("| ternary {-1,0,1} | %.1f | %.4f | %.3f |\n", amp, e, -db(e));
  }
  {
    double e = template_eff({-1, 1}, 1.0);
    printf("| binary {-1,1} (sign of I,Q) | 1.0 | %.4f | %.3f |\n", e, -db(e));
  }

  printf("\n## (c) Monte Carlo: weak tone, N=512 samples, 4-bit data x 4-bit template, deflection of |z|^2\n\n");
  {
    const int N = 512, trials = 20000;
    const double snr = undb(-20.0);  // per complex sample
    auto [e4, s4] = best_scale(range_levels(-8, 7));
    Quant Qd{range_levels(-8, 7), s4 * std::sqrt(0.5)};  // per-component sigma = sqrt(0.5)
    Quant Qt{range_levels(-7, 7), 1.0};
    double m1[2] = {0, 0}, m0[2] = {0, 0}, v0[2] = {0, 0};
#pragma omp parallel for reduction(+ : m1[:2], m0[:2], v0[:2])
    for (int t = 0; t < trials; ++t) {
      Rng r(1000 + t);
      double f = 0.1 + 0.3 * r.uniform(), ph = 2 * kPi * r.uniform();
      for (int h = 0; h < 2; ++h) {
        std::complex<double> zi = 0, zq = 0;
        for (int n = 0; n < N; ++n) {
          std::complex<double> s = std::polar(1.0, 2 * kPi * f * n);
          std::complex<double> nz(r.normal() * std::sqrt(0.5), r.normal() * std::sqrt(0.5));
          std::complex<double> y = nz + (h ? std::sqrt(snr) * std::polar(1.0, ph) * s : 0.0);
          std::complex<double> ts(Qt.q(7.0 * s.real()), Qt.q(7.0 * s.imag()));
          std::complex<double> yq(Qd.q(y.real()), Qd.q(y.imag()));
          zi += y * std::conj(s);
          zq += yq * std::conj(ts);
        }
        double ei = std::norm(zi), eq = std::norm(zq);
        if (h) { m1[0] += ei; m1[1] += eq; }
        else { m0[0] += ei; m0[1] += eq; v0[0] += ei * ei; v0[1] += eq * eq; }
      }
    }
    double defl[2];
    for (int k = 0; k < 2; ++k) {
      double mu0 = m0[k] / trials, var0 = v0[k] / trials - mu0 * mu0;
      defl[k] = (m1[k] / trials - mu0) / std::sqrt(var0);
    }
    printf("Per-sample SNR -20 dB, coherent gain 27.1 dB -> ideal Es/N0 = %.2f dB.\n", db(snr * N));
    printf("Deflection (mean shift / null std of |z|^2): unquantized %.4f, 4-bit data & template %.4f -> ratio %.4f (%.3f dB in SNR terms); predicted from (a)x(b): %.4f\n",
           defl[0], defl[1], defl[1] / defl[0], -db(defl[1] / defl[0]), e4 * template_eff(range_levels(-7, 7), 7.0));
  }

  printf("\n## (d) Symbol-wise MI of noncoherent M-FSK (rank-1 Rayleigh) with 16-level quantized cell energies\n\n");
  printf("Energies normalized to the noise mean (e ~ Exp(1) noise-only). Quantizers: 'uniform' step d on [0,16d); 'equiprob' thresholds at noise-only quantiles; 'log' thresholds geometric. Receiver uses exact bin likelihoods.\n\n");
  printf("| M | Es/N0 (dB) | MI unquantized (bit) | uniform best | equiprob | log-spaced | MI ratio unquantized/uniform (dB) |\n|---:|---:|---:|---:|---:|---:|---:|\n");
  for (int M : {16, 64, 256}) {
    for (double esdb : {0.0, 3.0, 6.0, 10.0, 14.0}) {
      double lam = undb(esdb);
      auto mi_with = [&](const std::vector<double>& thr) {
        // thr: 15 interior thresholds (ascending); bins [0,t1),...,[t15,inf)
        int nb = (int)thr.size() + 1;
        std::vector<double> lp(nb);
        for (int b = 0; b < nb; ++b) {
          double a = b ? thr[b - 1] : 0.0, c = (b < nb - 1) ? thr[b] : INFINITY;
          double pn = std::exp(-a) - (std::isfinite(c) ? std::exp(-c) : 0.0);
          double ps = std::exp(-a / (1 + lam)) - (std::isfinite(c) ? std::exp(-c / (1 + lam)) : 0.0);
          lp[b] = std::log(std::max(ps, 1e-300)) - std::log(std::max(pn, 1e-300));
        }
        auto bin = [&](double e) { return (int)(std::upper_bound(thr.begin(), thr.end(), e) - thr.begin()); };
        const int trials = 40000;
        double acc = 0;
#pragma omp parallel for reduction(+ : acc)
        for (int t = 0; t < trials; ++t) {
          Rng r(7 + t);
          double L0 = lp[bin((1 + lam) * r.exp1())];
          double s = 0;
          for (int m = 1; m < M; ++m) s += std::exp(lp[bin(r.exp1())] - L0);
          acc += std::log1p(s);
        }
        return std::log2((double)M) - acc / trials / kLn2;
      };
      // unquantized
      double accu = 0;
      {
        const int trials = 40000;
        double w = lam / (1 + lam);
#pragma omp parallel for reduction(+ : accu)
        for (int t = 0; t < trials; ++t) {
          Rng r(7 + t);
          double L0 = w * (1 + lam) * r.exp1();
          double s = 0;
          for (int m = 1; m < M; ++m) s += std::exp(w * r.exp1() - L0);
          accu += std::log1p(s);
        }
        accu = std::log2((double)M) - accu / 40000 / kLn2;
      }
      double best_u = 0;
      for (double d = 0.1; d <= 4.0; d *= 1.15) {
        std::vector<double> thr;
        for (int k = 1; k < 16; ++k) thr.push_back(k * d);
        best_u = std::max(best_u, mi_with(thr));
      }
      std::vector<double> teq, tlog;
      for (int k = 1; k < 16; ++k) teq.push_back(-std::log(1.0 - k / 16.0));
      double best_l = 0;
      for (double top = 4; top <= 60; top *= 1.3) {
        tlog.clear();
        for (int k = 1; k < 16; ++k) tlog.push_back(0.05 * std::pow(top / 0.05, (k - 1) / 14.0));
        best_l = std::max(best_l, mi_with(tlog));
      }
      double mie = mi_with(teq);
      printf("| %d | %.0f | %.4f | %.4f | %.4f | %.4f | %.3f |\n", M, esdb, accu, best_u, mie, best_l,
             accu > 0 ? db(accu / std::max(best_u, 1e-9)) : 0.0);
    }
  }
  return 0;
}
