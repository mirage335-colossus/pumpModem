// Bounded idealized observer screen. No modem or channel implementation.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
constexpr double kPfa = 1e-3;
constexpr double kPd = 0.9;
constexpr double kPi = 3.14159265358979323846;

double gamma_p(double a, double x) {
  if (x <= 0) return 0;
  if (x < a + 1) {
    double term = 1 / a;
    double sum = term;
    for (int n = 1; n < 10000; ++n) {
      term *= x / (a + n);
      sum += term;
      if (std::abs(term) <= std::abs(sum) * 2e-15) break;
    }
    return std::clamp(sum * std::exp(-x + a * std::log(x) - std::lgamma(a)), 0.0, 1.0);
  }

  constexpr double tiny = 1e-300;
  double b = x + 1 - a;
  double c = 1 / tiny;
  double d = 1 / b;
  double h = d;
  for (int i = 1; i < 10000; ++i) {
    const double an = -static_cast<double>(i) * (static_cast<double>(i) - a);
    b += 2;
    d = an * d + b;
    if (std::abs(d) < tiny) d = tiny;
    c = b + an / c;
    if (std::abs(c) < tiny) c = tiny;
    d = 1 / d;
    const double delta = d * c;
    h *= delta;
    if (std::abs(delta - 1) < 2e-15) break;
  }
  const double q = std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
  return std::clamp(1 - q, 0.0, 1.0);
}

double chi_square_cdf(double df, double x) {
  return gamma_p(df / 2, x / 2);
}

double noncentral_chi_square_cdf(double df, double x, double lambda) {
  const double mu = lambda / 2;
  if (mu < 1e-12) return chi_square_cdf(df, x);
  const int last = static_cast<int>(mu + 14 * std::sqrt(mu + 1) + 40);
  double total = 0;
  for (int j = 0; j <= last; ++j) {
    const double log_weight = -mu + j * std::log(mu) - std::lgamma(j + 1.0);
    total += std::exp(log_weight) * gamma_p(df / 2 + j, x / 2);
  }
  return std::clamp(total, 0.0, 1.0);
}

double central_quantile(double df, double probability) {
  double low = 0;
  double high = df + 40 * std::sqrt(df + 1) + 100;
  while (chi_square_cdf(df, high) < probability) high *= 2;
  for (int i = 0; i < 90; ++i) {
    const double mid = (low + high) / 2;
    if (chi_square_cdf(df, mid) < probability) low = mid;
    else high = mid;
  }
  return (low + high) / 2;
}

double required_energy(double df, int hypotheses) {
  const double local_cdf = std::pow(1 - kPfa, 1.0 / hypotheses);
  const double threshold = central_quantile(df, local_cdf);
  const double target_cdf = (1 - kPd) / std::pow(local_cdf, hypotheses - 1);
  double low = 0;
  double high = 1;
  while (noncentral_chi_square_cdf(df, threshold, 2 * high) > target_cdf) high *= 2;
  for (int i = 0; i < 70; ++i) {
    const double mid = (low + high) / 2;
    if (noncentral_chi_square_cdf(df, threshold, 2 * mid) > target_cdf) low = mid;
    else high = mid;
  }
  return (low + high) / 2;
}

double survival_exp(double x) { return std::exp(-x); }
}

int main() {
  std::puts("K,looks,full_search_hypotheses,sparse_search_hypotheses,wideband_radiometer_E_over_N0_dB,known_hop_E_over_N0_dB,local16_E_over_N0_dB,full_search_E_over_N0_dB");
  for (int looks : {1, 2, 4, 8, 16}) {
    for (int bins : {1, 4, 16, 64, 256, 352}) {
      const int local = std::min(bins, 16);
      const double rad = required_energy(2.0 * bins * looks, 1);
      const double known = required_energy(2.0 * looks, 1);
      const double sparse = required_energy(2.0 * looks, local);
      const double full = required_energy(2.0 * looks, bins);
      const auto db = [](double x) { return 10 * std::log10(x); };
      std::printf("%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n", bins, looks, bins, local,
                  db(rad), db(known), db(sparse), db(full));
    }
  }

  // Exact one-look, one-bin Rayleigh check. The noncentral model above assumes
  // a fixed complex signal amplitude. This separate case deliberately differs.
  const double threshold = -std::log(kPfa);
  const double rho = threshold / -std::log(kPd) - 1;
  const double achieved_pfa = survival_exp(threshold);
  const double achieved_pd = survival_exp(threshold / (1 + rho));
  std::fprintf(stderr, "Rayleigh control: rho=%.9f (%+.4f dB), Pfa=%.9g, Pd=%.9g\n",
               rho, 10 * std::log10(rho), achieved_pfa, achieved_pd);
  return 0;
}
