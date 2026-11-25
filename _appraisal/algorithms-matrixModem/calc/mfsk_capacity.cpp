// Symbol-wise mutual information of noncoherent M-ary FSK (one tone per
// symbol, orthogonal tone hypotheses, optimal per-tone likelihood) under
//   * constant-amplitude unknown-phase AWGN ("awgn-nc"),
//   * slow flat Rayleigh fading, ergodically interleaved ("rayleigh-slow"),
//   * flat Rayleigh fading with a Gaussian Doppler spectrum whose full width
//     2*sigma follows ITU-R F.1487 ("dopp-<2sigma>Hz"); the receiver uses the
//     exact Gaussian-signal estimator-correlator (Karhunen-Loeve weights).
// For each model and received C/N0 it maximizes rate = I(T)/T over symbol
// duration T, with tone spacing Delta = 1/T + 4*sigma (so Doppler-broadened
// tones do not overlap) and M = floor(B/Delta) tones in bandwidth B.
// Assumptions: perfect timing/frequency knowledge, no inter-tone leakage,
// independent fading from symbol to symbol (ideal interleaving, memoryless
// receiver), ideal outer code achieving symbol-wise mutual information.
// Under Watterson's two-path model with zero path offsets a narrowband tone
// sees the sum of independent Gaussian processes with the same spectrum, i.e.
// the same Rayleigh/Gaussian-Doppler law, so delay spread only enters through
// symbol-edge smear (ignored here) and frequency correlation across tones.
//
// Build: g++ -O3 -march=native -fopenmp -std=c++20 mfsk_capacity.cpp -o mfsk_capacity
#include <omp.h>
#include <cstring>
#include <string>
#include "mm_common.hpp"

using namespace mm;

struct Model {
  std::string name;
  int kind;          // 0 awgn-nc, 1 rayleigh-slow, 2 doppler rayleigh
  double two_sigma;  // Hz
};

// log(1 + sum_{m!=0} exp(L_m - L_0)) for rank-1 models using exact top-K order
// statistics of the M-1 noise-cell exponentials (Renyi representation) and a
// conditional-mean remainder.  f(e) is the per-cell likelihood ratio factor.
template <class F, class CM>
double rank1_logterm(Rng& rng, long n, double L0, F logf, CM cond_mean_f, double* nmean) {
  const int K = 160;
  if (n <= 4 * K) {
    // brute force log-sum-exp
    double mx = L0;
    std::vector<double> v(n);
    double es = 0;
    for (long i = 0; i < n; ++i) { double e = rng.exp1(); es += e; v[i] = logf(e); mx = std::max(mx, v[i]); }
    *nmean = es / n;
    double s = std::exp(L0 - mx);
    double t = 0;
    for (long i = 0; i < n; ++i) t += std::exp(v[i] - mx);
    return std::log1p(t / s);
  }
  // Renyi: X_(k) = sum_{j=k}^{n} E_j / j, descending order.
  double hn = 0, h2 = 0;
  // tail sums for j = K+1..n computed analytically
  auto harm = [](double x) { return std::log(x) + 0.5772156649015329 + 1.0 / (2 * x) - 1.0 / (12 * x * x); };
  hn = harm((double)n) - harm((double)K);
  h2 = (1.0 / K - 1.0 / n);  // approx sum_{j=K+1}^{n} 1/j^2
  double G = hn + std::sqrt(h2) * rng.normal();
  if (G < 0) G = 0;
  std::vector<double> top(K);
  double x = G;
  for (int k = K; k >= 1; --k) { x += rng.exp1() / k; top[k - 1] = x; }
  double thr = top[K - 1];
  double mx = L0;
  std::vector<double> lv(K);
  for (int k = 0; k < K; ++k) { lv[k] = logf(top[k]); mx = std::max(mx, lv[k]); }
  double rem_log = std::log((double)(n - K)) + cond_mean_f(thr);  // log of remainder sum
  mx = std::max(mx, rem_log);
  double t = std::exp(rem_log - mx);
  for (int k = 0; k < K; ++k) t += std::exp(lv[k] - mx);
  double s = std::exp(L0 - mx);
  return std::log1p(t / s);
}

int main(int argc, char** argv) {
  const double B = (argc > 1) ? atof(argv[1]) : 2200.0;
  const int trials = (argc > 2) ? atoi(argv[2]) : 1500;
  std::vector<Model> models = {{"awgn-nc", 0, 0}, {"rayleigh-slow", 1, 0}};
  for (double s2 : {0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0})
    models.push_back({"dopp-" + std::to_string(s2).substr(0, std::to_string(s2).find('.') + 2) + "Hz", 2, s2});

  if (argc > 3) {  // optional model-name filter (substring)
    std::vector<Model> keep;
    for (auto& m : models) if (m.name.find(argv[3]) != std::string::npos) keep.push_back(m);
    models = keep;
  }
  std::vector<double> cn0_db;
  const double cmin = (argc > 4) ? atof(argv[4]) : -10.0, cmax = (argc > 5) ? atof(argv[5]) : 34.0;
  for (double c = cmin; c <= cmax + 0.01; c += 2) cn0_db.push_back(c);
  std::vector<double> Ts;
  for (double lt = std::log10(0.004); lt <= std::log10(300.0) + 1e-9; lt += 0.05) Ts.push_back(std::pow(10, lt));
  // exact reference durations (40 ms ... 10.24 s, powers of two times 160 ms)
  for (double t : {0.04, 0.08, 0.16, 0.32, 0.64, 1.28, 2.56, 5.12, 10.24}) Ts.push_back(t);
  std::sort(Ts.begin(), Ts.end());

  printf("# Noncoherent MFSK symbol-wise MI, B=%.0f Hz, trials=%d per (T, C/N0)\n", B, trials);
  printf("# columns: model C/N0[dB-Hz] best_rate[bit/s] T*[s] M* Es/N0*[dB] Eb/N0[dB] shannon_rate[bit/s] loss_vs_shannon[dB] MI*/log2M\n");

  for (const auto& md : models) {
    const double sigma = md.two_sigma / 2.0;
    // rate[ci][ti]
    std::vector<std::vector<double>> rate(cn0_db.size(), std::vector<double>(Ts.size(), 0.0));
    std::vector<std::vector<double>> mi(cn0_db.size(), std::vector<double>(Ts.size(), 0.0));
    std::vector<long> Ms(Ts.size());
    for (size_t ti = 0; ti < Ts.size(); ++ti) {
      const double T = Ts[ti];
      const double delta = 1.0 / T + 4.0 * sigma;
      const long M = (long)std::floor(B / delta);
      Ms[ti] = M;
      if (M < 2) continue;
      if (md.kind == 2 && 2.0 * sigma * T > 60.0) continue;  // far beyond coherence; never optimal
      if (md.kind == 2 && T > 60.0) continue;                // brute-force cost cap
      const int ntr = (md.kind == 2) ? trials : 3 * trials;
      std::vector<double> mu = (md.kind == 2) ? doppler_eigs(sigma, T) : std::vector<double>{1.0};
      const int r = (int)mu.size();
      // Control-variate accumulators per C/N0: X, C1, C2 and their products.
      const size_t NC = cn0_db.size();
      std::vector<double> acc(NC * 8, 0.0);
#pragma omp parallel
      {
        std::vector<double> lacc(NC * 8, 0.0);
        auto add = [&](size_t ci, double X, double c1, double c2) {
          double* a = &lacc[ci * 8];
          a[0] += X; a[1] += c1; a[2] += c2; a[3] += X * c1; a[4] += X * c2;
          a[5] += c1 * c1; a[6] += c2 * c2; a[7] += c1 * c2;
        };
#pragma omp for schedule(dynamic, 8)
        for (int tr = 0; tr < ntr; ++tr) {
          Rng rng(12345 + 7919 * ti, (uint64_t)tr * 131 + md.kind * 17 + (uint64_t)(md.two_sigma * 1000));
          if (md.kind == 0) {
            // constant amplitude, unknown phase: y0 = a + n, metric log I0(2 a |y|)
            double nr = rng.normal() * std::sqrt(0.5), ni = rng.normal() * std::sqrt(0.5);
            for (size_t ci = 0; ci < NC; ++ci) {
              double rho = undb(cn0_db[ci]) * T;  // Es/N0
              double a = std::sqrt(rho);
              double yr = a + nr, yi = ni;
              double e0 = yr * yr + yi * yi;
              double L0 = log_i0(2 * a * std::sqrt(e0));
              Rng r2(999 + ti, (uint64_t)tr);  // common noise cells across C/N0
              auto logf = [&](double e) { return log_i0(2 * a * std::sqrt(e)); };
              auto cmean = [&](double t) {
                const int N = 400;
                double h = t / N, ss = 0, mx = -1e300;
                std::vector<double> lv(N + 1);
                for (int k = 0; k <= N; ++k) {
                  double x = k * h;
                  lv[k] = log_i0(2 * a * std::sqrt(x)) - x;
                  mx = std::max(mx, lv[k]);
                }
                for (int k = 0; k <= N; ++k) {
                  double w = (k == 0 || k == N) ? 1 : (k % 2 ? 4 : 2);
                  ss += w * std::exp(lv[k] - mx);
                }
                ss *= h / 3.0;
                return std::log(ss) + mx - std::log1p(-std::exp(-t));
              };
              double nmean = NAN;
              double X = rank1_logterm(r2, M - 1, L0, logf, cmean, &nmean);
              add(ci, X, e0 - (1 + rho), std::isnan(nmean) ? 0.0 : rho * (nmean - 1));
            }
          } else if (md.kind == 1) {
            double e0n = rng.exp1();
            for (size_t ci = 0; ci < NC; ++ci) {
              double lam = undb(cn0_db[ci]) * T;
              double w = lam / (1 + lam);
              double L0 = w * (1 + lam) * e0n;
              Rng r2(777 + ti, (uint64_t)tr);
              auto logf = [&](double e) { return w * e; };
              auto cmean = [&](double t) {
                return std::log(-std::expm1(-(1 - w) * t) / (1 - w)) - std::log1p(-std::exp(-t));
              };
              double nmean = NAN;
              double X = rank1_logterm(r2, M - 1, L0, logf, cmean, &nmean);
              add(ci, X, w * (1 + lam) * (e0n - 1), std::isnan(nmean) ? 0.0 : w * (nmean - 1));
            }
          } else {
            // brute force over M cells with r KL coordinates each
            std::vector<double> e0(r);
            for (int i = 0; i < r; ++i) e0[i] = rng.exp1();
            std::vector<double> en((size_t)(M - 1) * r);
            for (auto& v : en) v = rng.exp1();
            std::vector<double> esum(r, 0.0);
            for (long m = 0; m < M - 1; ++m)
              for (int i = 0; i < r; ++i) esum[i] += en[(size_t)m * r + i];
            std::vector<double> w(r), lam(r), Lm(M - 1);
            for (size_t ci = 0; ci < NC; ++ci) {
              double es = undb(cn0_db[ci]) * T;
              for (int i = 0; i < r; ++i) { lam[i] = es * mu[i]; w[i] = lam[i] / (1 + lam[i]); }
              double L0 = 0, c1 = 0, c2 = 0;
              for (int i = 0; i < r; ++i) {
                L0 += w[i] * (1 + lam[i]) * e0[i];
                c1 += w[i] * (1 + lam[i]) * (e0[i] - 1);
                c2 += w[i] * (esum[i] / (M - 1) - 1);
              }
              double mx = L0;
              for (long m = 0; m < M - 1; ++m) {
                double L = 0;
                const double* p = &en[(size_t)m * r];
                for (int i = 0; i < r; ++i) L += w[i] * p[i];
                Lm[m] = L;
                mx = std::max(mx, L);
              }
              double t = 0;
              for (long m = 0; m < M - 1; ++m) t += std::exp(Lm[m] - mx);
              add(ci, std::log1p(t / std::exp(L0 - mx)), c1, c2);
            }
          }
        }
#pragma omp critical
        for (size_t k = 0; k < acc.size(); ++k) acc[k] += lacc[k];
      }
      for (size_t ci = 0; ci < NC; ++ci) {
        const double* a = &acc[ci * 8];
        const double n = ntr;
        double mX = a[0] / n, m1 = a[1] / n, m2 = a[2] / n;
        double v11 = a[5] / n - m1 * m1, v22 = a[6] / n - m2 * m2, v12 = a[7] / n - m1 * m2;
        double cx1 = a[3] / n - mX * m1, cx2 = a[4] / n - mX * m2;
        double b1 = 0, b2 = 0;
        double det = v11 * v22 - v12 * v12;
        if (v22 > 1e-30 * (v11 + 1e-300) && std::fabs(det) > 1e-30 * v11 * v22) {
          b1 = (cx1 * v22 - cx2 * v12) / det;
          b2 = (cx2 * v11 - cx1 * v12) / det;
        } else if (v11 > 0) {
          b1 = cx1 / v11;
        }
        double Xadj = mX - b1 * m1 - b2 * m2;  // control variates have zero true mean
        double I = std::log2((double)M) - Xadj / kLn2;
        if (I < 0) I = 0;
        double es = undb(cn0_db[ci]) * T, sl2 = 0;
        for (double v : mu) sl2 += (es * v) * (es * v);
        double ilow = 0.5 * (1 - 1.0 / M) * sl2 / kLn2;  // low-SNR expansion, bits
        mi[ci][ti] = I;
        rate[ci][ti] = I / T;
        printf("MAT %s T=%.5g M=%ld r=%d cn0=%.1f EsN0dB=%.2f MI=%.6g MIlow=%.6g rate=%.6g\n", md.name.c_str(), T, M, r,
               cn0_db[ci], db(es), I, ilow, I / T);
      }
    }
    for (size_t ci = 0; ci < cn0_db.size(); ++ci) {
      size_t best = 0;
      for (size_t ti = 0; ti < Ts.size(); ++ti) if (rate[ci][ti] > rate[ci][best]) best = ti;
      double R = rate[ci][best];
      double cn0 = undb(cn0_db[ci]);
      double rsh = B * std::log2(1 + cn0 / B);
      double cn0_sh = B * (std::pow(2.0, R / B) - 1);  // C/N0 Shannon needs for R
      double loss = (R > 0) ? db(cn0 / cn0_sh) : NAN;
      printf("%-14s %6.1f %12.5g %8.4g %7ld %7.2f %7.2f %12.5g %7.2f %6.3f\n", md.name.c_str(),
             cn0_db[ci], R, Ts[best], Ms[best], db(cn0 * Ts[best]), (R > 0) ? db(cn0 / R) : NAN, rsh,
             loss, (Ms[best] >= 2) ? mi[ci][best] / std::log2((double)Ms[best]) : 0.0);
    }
    fflush(stdout);
  }
  return 0;
}
