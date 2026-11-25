// Energy needed to send k bits with one huge orthogonal "symbol" (M = 2^k
// waveforms), exact error probabilities in AWGN:
//   coherent:    Pc = int phi(x - sqrt(2E/N0)) Phi(x)^(M-1) dx
//   noncoherent: Pc = int r exp(-(r^2+a^2)/2) I0(a r) (1-exp(-r^2/2))^(M-1) dr,
//                a^2 = 2E/N0 (unknown constant phase over the whole message)
// and the outage margin needed when the message energy is collected over N_eff
// independent Rayleigh fades (time average of N_eff unit-mean exponentials is
// Gamma(N_eff, 1/N_eff)).  Also: required margin for a log-normal slow-fade
// component (sigma dB) that does not average within the message.
// Build: g++ -O2 -std=c++20 finite_energy.cpp -o finite_energy
#include "mm_common.hpp"
using namespace mm;

static double log_pow_m1(double logbase, double M) {  // (M-1)*log(base), base in (0,1)
  return (M - 1.0) * logbase;
}

static double pe_coherent(double k, double x) {
  double M = std::pow(2.0, k);
  double a = std::sqrt(2 * x);
  double s = 0, h = 0.002;
  for (double t = a - 12; t <= a + 12; t += h) {
    double lphi = std::log1p(-qfunc(t));
    double v = phi(t - a) * std::exp(log_pow_m1(lphi, M));
    s += v * h;
  }
  return 1 - s;
}

static double pe_noncoherent(double k, double x) {
  double M = std::pow(2.0, k);
  double a = std::sqrt(2 * x);
  double s = 0, h = 0.002;
  double lo = std::max(1e-6, a - 14), hi = a + 14;
  for (double r = lo; r <= hi; r += h) {
    double lp = std::log(r) - 0.5 * (r * r + a * a) + log_i0(a * r);
    double lq = std::log1p(-std::exp(-0.5 * r * r));
    s += std::exp(lp + log_pow_m1(lq, M)) * h;
  }
  return 1 - s;
}

static double solve(double k, double eps, bool coh) {
  double lo = 0.1, hi = 1e5;
  for (int i = 0; i < 80; ++i) {
    double x = std::sqrt(lo * hi);
    double pe = coh ? pe_coherent(k, x) : pe_noncoherent(k, x);
    if (pe > eps) lo = x; else hi = x;
  }
  return std::sqrt(lo * hi);
}

// regularized lower incomplete gamma P(a, x) by series / continued fraction
static double gammp(double a, double x) {
  if (x <= 0) return 0;
  double gln = std::lgamma(a);
  if (x < a + 1) {
    double ap = a, sum = 1.0 / a, del = sum;
    for (int n = 0; n < 100000; ++n) { ap += 1; del *= x / ap; sum += del; if (std::fabs(del) < std::fabs(sum) * 1e-15) break; }
    return sum * std::exp(-x + a * std::log(x) - gln);
  }
  double b = x + 1 - a, c = 1e300, d = 1 / b, h = d;
  for (int i = 1; i < 100000; ++i) {
    double an = -i * (i - a); b += 2; d = an * d + b; if (std::fabs(d) < 1e-300) d = 1e-300;
    c = b + an / c; if (std::fabs(c) < 1e-300) c = 1e-300; d = 1 / d; double del = d * c; h *= del;
    if (std::fabs(del - 1) < 1e-15) break;
  }
  return 1 - std::exp(-x + a * std::log(x) - gln) * h;
}

int main() {
  printf("## One-shot orthogonal signaling: Eb/N0 (dB) for word error eps, M = 2^k, AWGN\n\n");
  printf("| k | coherent 1e-2 | coherent 1e-3 | noncoherent 1e-2 | noncoherent 1e-3 | noncoherent penalty @1e-3 (dB) |\n|---:|---:|---:|---:|---:|---:|\n");
  for (double k : {1.0, 2.0, 4.0, 8.0, 12.0, 16.0, 20.0, 24.0, 32.0, 48.0, 64.0, 128.0}) {
    double c2 = solve(k, 1e-2, true), c3 = solve(k, 1e-3, true);
    double n2 = solve(k, 1e-2, false), n3 = solve(k, 1e-3, false);
    printf("| %.0f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", k, db(c2 / k), db(c3 / k), db(n2 / k), db(n3 / k), db(n3 / c3));
  }

  printf("\n## Fade margin (dB) when message energy is averaged over N_eff independent Rayleigh fades\n\n");
  printf("Margin = -10 log10 of the eps-quantile of Gamma(N_eff, 1/N_eff); this is the power margin over the mean that keeps outage below eps for an ideal energy-averaging code.\n\n");
  printf("| N_eff | eps=1e-1 | 1e-2 | 1e-3 | 1e-4 |\n|---:|---:|---:|---:|---:|\n");
  for (double n : {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 1024.0, 4096.0}) {
    printf("| %.0f |", n);
    for (double eps : {1e-1, 1e-2, 1e-3, 1e-4}) {
      double lo = 0, hi = 1;
      for (int i = 0; i < 200; ++i) { double m = 0.5 * (lo + hi); if (gammp(n, n * m) < eps) lo = m; else hi = m; }
      printf(" %.2f |", -db(0.5 * (lo + hi)));
    }
    printf("\n");
  }
  printf("\n## Log-normal slow fading that is constant over the message: margin = z_eps * sigma_dB\n\n| sigma (dB) | eps=1e-1 | 1e-2 | 1e-3 |\n|---:|---:|---:|---:|\n");
  for (double s : {2.0, 4.0, 6.0, 8.0, 10.0}) printf("| %.0f | %.1f | %.1f | %.1f |\n", s, qinv(1e-1) * s, qinv(1e-2) * s, qinv(1e-3) * s);
  return 0;
}
