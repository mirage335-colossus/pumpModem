// Cost of search breadth (number of hypotheses K) and of noncoherent
// integration (L looks combined by energy) for detecting one candidate.
// Null: sum of L unit exponentials (Gamma(L)); signal: constant amplitude,
// unknown independent phase per look, total energy rho = sum Es/N0 (AWGN).
// Threshold: Bonferroni, per-hypothesis Pfa = alpha / K (family-wise <= alpha).
// Output: total Es/N0 (dB) for Pd = 0.5 and 0.9.
// Build: g++ -O2 -std=c++20 false_alarm.cpp -o false_alarm
#include "mm_common.hpp"
using namespace mm;

// regularized upper incomplete gamma Q(a,x), log-domain safe for tails
static double log_gammq(double a, double x) {
  if (x <= 0) return 0.0;
  double gln = std::lgamma(a);
  if (x < a + 1) {  // series for P, then Q = 1 - P
    double ap = a, sum = 1.0 / a, del = sum;
    for (int n = 0; n < 100000; ++n) { ap += 1; del *= x / ap; sum += del; if (std::fabs(del) < std::fabs(sum) * 1e-16) break; }
    double p = sum * std::exp(-x + a * std::log(x) - gln);
    return std::log1p(-std::min(p, 1 - 1e-300));
  }
  double b = x + 1 - a, c = 1e300, d = 1 / b, h = d;
  for (int i = 1; i < 100000; ++i) {
    double an = -i * (i - a); b += 2; d = an * d + b; if (std::fabs(d) < 1e-300) d = 1e-300;
    c = b + an / c; if (std::fabs(c) < 1e-300) c = 1e-300; d = 1 / d; double del = d * c; h *= del;
    if (std::fabs(del - 1) < 1e-16) break;
  }
  return -x + a * std::log(x) - gln + std::log(h);
}

static double threshold(int L, double pfa) {
  double lo = 0, hi = 2000;
  for (int i = 0; i < 300; ++i) {
    double m = 0.5 * (lo + hi);
    if (log_gammq(L, m) > std::log(pfa)) lo = m; else hi = m;
  }
  return 0.5 * (lo + hi);
}

// P(sum > tau) for noncentral (Poisson mixture of Gamma(L+k))
static double pdet(int L, double rho, double tau) {
  double s = 0;
  int kmax = (int)(rho + 12 * std::sqrt(rho + 1) + 30);
  for (int k = 0; k <= kmax; ++k) {
    double lw = -rho + k * std::log(std::max(rho, 1e-300)) - std::lgamma(k + 1.0);
    if (rho == 0 && k > 0) break;
    s += std::exp(lw + log_gammq(L + k, tau));
  }
  return s;
}

static double solve_rho(int L, double tau, double pd) {
  double lo = 1e-3, hi = 1e5;
  for (int i = 0; i < 200; ++i) {
    double m = std::sqrt(lo * hi);
    if (pdet(L, m, tau) < pd) lo = m; else hi = m;
  }
  return std::sqrt(lo * hi);
}

int main() {
  const double alpha = 1e-2;
  printf("## Required total Es/N0 (dB) to detect one candidate among K, family-wise Pfa <= %.0e (Bonferroni)\n\n", alpha);
  printf("Entries: Pd=0.5 / Pd=0.9. L = number of noncoherently (energy) combined looks.\n\n");
  int Ls[] = {1, 4, 16, 64, 256, 1024};
  double Ks[] = {1, 1e3, 1e6, 1e9, 1e12, 1e15};
  printf("| K |");
  for (int L : Ls) printf(" L=%d |", L);
  printf("\n|---:|");
  for (int i = 0; i < 6; ++i) printf("---:|");
  printf("\n");
  for (double K : Ks) {
    printf("| %.0e |", K);
    for (int L : Ls) {
      double tau = threshold(L, alpha / K);
      printf(" %.1f / %.1f |", db(solve_rho(L, tau, 0.5)), db(solve_rho(L, tau, 0.9)));
    }
    printf("\n");
  }
  printf("\nSearch-breadth cost: difference between rows; noncoherent-integration loss: growth across columns relative to the coherent case (L=1 with the same total energy).\n");
  return 0;
}
