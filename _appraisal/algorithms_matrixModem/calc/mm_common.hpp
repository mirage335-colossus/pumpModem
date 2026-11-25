// Shared numerical helpers for the Opus-5.5 Matrix Modem research calculations.
// Standard library only.  These are analysis aids, not modem code.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace mm {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLn2 = 0.69314718055994530942;

inline double db(double x) { return 10.0 * std::log10(x); }
inline double undb(double x) { return std::pow(10.0, x / 10.0); }

// xoshiro256** with splitmix64 seeding; deterministic per (seed, stream).
struct Rng {
  uint64_t s[4];
  static uint64_t splitmix(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  explicit Rng(uint64_t seed, uint64_t stream = 0) {
    uint64_t x = seed ^ (0xD1B54A32D192ED03ull * (stream + 1));
    for (auto& v : s) v = splitmix(x);
  }
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  uint64_t next() {
    const uint64_t r = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = rotl(s[3], 45);
    return r;
  }
  double uniform() { return ((next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
  double exp1() { return -std::log(uniform()); }
  double normal() {
    // Box-Muller, one value per call (simple, adequate here).
    double u1 = uniform(), u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
  }
};

// log(I0(x)) for x >= 0, Abramowitz & Stegun 9.8.1/9.8.2 (rel. error < 2e-7).
inline double log_i0(double x) {
  double ax = std::fabs(x);
  if (ax < 3.75) {
    double y = (x / 3.75) * (x / 3.75);
    double v = 1.0 + y * (3.5156229 + y * (3.0899424 + y * (1.2067492 + y * (0.2659732 +
               y * (0.0360768 + y * 0.0045813)))));
    return std::log(v);
  }
  double y = 3.75 / ax;
  double p = 0.39894228 + y * (0.01328592 + y * (0.00225319 + y * (-0.00157565 +
             y * (0.00916281 + y * (-0.02057706 + y * (0.02635537 + y * (-0.01647633 +
             y * 0.00392377)))))));
  return ax - 0.5 * std::log(ax) + std::log(p);
}

inline double qfunc(double x) { return 0.5 * std::erfc(x / std::sqrt(2.0)); }
inline double phi(double x) { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * kPi); }

// Inverse of Q by bisection (adequate for tables).
inline double qinv(double p) {
  double lo = -40, hi = 40;
  for (int i = 0; i < 200; ++i) {
    double mid = 0.5 * (lo + hi);
    if (qfunc(mid) > p) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

// Cyclic Jacobi eigenvalues of a small dense symmetric matrix (row-major n*n).
inline std::vector<double> sym_eigenvalues(std::vector<double> a, int n) {
  for (int sweep = 0; sweep < 100; ++sweep) {
    double off = 0;
    for (int p = 0; p < n; ++p)
      for (int q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
    if (off < 1e-24) break;
    for (int p = 0; p < n; ++p)
      for (int q = p + 1; q < n; ++q) {
        double apq = a[p * n + q];
        if (std::fabs(apq) < 1e-300) continue;
        double app = a[p * n + p], aqq = a[q * n + q];
        double theta = (aqq - app) / (2.0 * apq);
        double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
        for (int k = 0; k < n; ++k) {
          double akp = a[k * n + p], akq = a[k * n + q];
          a[k * n + p] = c * akp - s * akq;
          a[k * n + q] = s * akp + c * akq;
        }
        for (int k = 0; k < n; ++k) {
          double apk = a[p * n + k], aqk = a[q * n + k];
          a[p * n + k] = c * apk - s * aqk;
          a[q * n + k] = s * apk + c * aqk;
        }
      }
  }
  std::vector<double> ev(n);
  for (int i = 0; i < n; ++i) ev[i] = a[i * n + i];
  std::sort(ev.begin(), ev.end(), std::greater<double>());
  return ev;
}

// Normalized eigenvalue spectrum (sum = 1) of a unit-power complex Gaussian
// fading tap observed over duration T with a Gaussian Doppler spectrum of
// standard deviation sigma_hz (ITU-R F.1487 "frequency spread" = 2*sigma).
// Returns the eigenvalues of the K-point sampled correlation matrix / K.
inline std::vector<double> doppler_eigs(double sigma_hz, double T, double floor_rel = 1e-5) {
  if (sigma_hz <= 0) return {1.0};
  double bt = 2.0 * sigma_hz * T;
  int K = std::min(200, std::max(24, (int)std::ceil(3.0 * bt) + 24));
  std::vector<double> R(K * K);
  for (int i = 0; i < K; ++i)
    for (int j = 0; j < K; ++j) {
      double tau = (i - j) * T / K;
      R[i * K + j] = std::exp(-2.0 * kPi * kPi * sigma_hz * sigma_hz * tau * tau) / K;
    }
  auto ev = sym_eigenvalues(R, K);
  std::vector<double> out;
  double sum = 0;
  for (double v : ev) if (v > floor_rel * ev[0]) { out.push_back(v); sum += v; }
  for (double& v : out) v /= sum;  // renormalize (truncated tail is negligible)
  return out;
}

}  // namespace mm
