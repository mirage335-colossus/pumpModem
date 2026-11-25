// HF link-budget arithmetic for Matrix Modem research.
//  * ITU-R P.372 median man-made noise Fam = c - d log10(f/MHz) (city,
//    residential, rural, quiet rural) and galactic noise 52 - 23 log10(f),
//    referred to a short lossless vertical monopole over perfect ground.
//    Atmospheric noise is NOT included (it is location/season/time specific
//    and can dominate below ~10 MHz at night and in summer).
//  * N0 = Fa - 174 dBm/Hz.  C/N0 = Pt - L - N0 with L the signal loss between
//    transmitter output and the receiver reference plane (antenna gains and
//    efficiencies included in L).  When external noise dominates, receive
//    antenna efficiency scales signal and noise together; it is then not part
//    of the SNR budget.  (Here L is treated as the total signal-only loss.)
//  * Ideal times: AWGN wideband energy bound t >= k ln2 /(C/N0), and the
//    Polyanskiy-Poor-Verdu (2011) finite-k minimum-energy expansion
//    k ln2 = x - sqrt(2x) Qinv(eps) + 0.5 ln x  (x = E/N0, O(1) dropped).
// Build: g++ -O2 -std=c++20 link_budget.cpp -o link_budget
#include "mm_common.hpp"
using namespace mm;

static double ppv_energy(double k, double eps) {  // returns x = E/N0 (linear)
  double q = qinv(eps);
  double lo = 1e-3, hi = 1e7;
  for (int i = 0; i < 300; ++i) {
    double x = std::sqrt(lo * hi);
    double lhs = x - std::sqrt(2 * x) * q + 0.5 * std::log(x);
    if (lhs < k * kLn2) lo = x; else hi = x;
  }
  return std::sqrt(lo * hi);
}

// Exact word error of coherent orthogonal signaling with M = 2^k (achievability
// reference, tighter than the O(1)-truncated expansion at k = 128).
static double pe_orth(double k, double x) {
  double M = std::pow(2.0, k), a = std::sqrt(2 * x), s = 0, h = 0.002;
  for (double t = a - 12; t <= a + 12; t += h) s += phi(t - a) * std::exp((M - 1) * std::log1p(-qfunc(t))) * h;
  return 1 - s;
}
static double orth_energy(double k, double eps) {
  double lo = 0.1, hi = 1e5;
  for (int i = 0; i < 80; ++i) { double x = std::sqrt(lo * hi); if (pe_orth(k, x) > eps) lo = x; else hi = x; }
  return std::sqrt(lo * hi);
}

static const char* fmt_time(double s, char* buf) {
  if (!(s > 0) || !std::isfinite(s)) { snprintf(buf, 32, "-"); return buf; }
  if (s < 120) snprintf(buf, 32, "%.3g s", s);
  else if (s < 7200) snprintf(buf, 32, "%.3g min", s / 60);
  else if (s < 2 * 86400) snprintf(buf, 32, "%.3g h", s / 3600);
  else snprintf(buf, 32, "%.3g d", s / 86400);
  return buf;
}

int main() {
  struct Env { const char* name; double c, d; };
  Env env[] = {{"city", 76.8, 27.7}, {"residential", 72.5, 27.7}, {"rural", 67.2, 27.7}, {"quiet-rural", 53.6, 28.6}};
  double freqs[] = {1.9, 3.6, 5.3, 7.1, 10.1, 14.1, 18.1, 21.1, 24.9, 28.1};

  printf("## P.372 median external noise (man-made by environment; galactic if it reaches the ground)\n\n");
  printf("| f (MHz) | city Fa | residential | rural | quiet rural | galactic | quiet rural + galactic: Fa / N0 (dBm/Hz) | rural + galactic: N0 |\n");
  printf("|---:|---:|---:|---:|---:|---:|---:|---:|\n");
  for (double f : freqs) {
    double lf = std::log10(f);
    double fam[4];
    for (int i = 0; i < 4; ++i) fam[i] = env[i].c - env[i].d * lf;
    double gal = 52.0 - 23.0 * lf;
    double qr_g = db(undb(fam[3]) + undb(gal));
    double r_g = db(undb(fam[2]) + undb(gal));
    printf("| %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f / %.1f | %.1f |\n", f, fam[0], fam[1], fam[2], fam[3], gal,
           qr_g, qr_g - 174, r_g - 174);
  }

  // C/N0 grid at three noise densities that bracket mid-HF medians.
  double n0s[] = {-150.0, -140.0, -130.0};
  double pts_mw[] = {2.0, 10.0, 100.0};
  double losses[] = {140, 150, 160, 170, 180};
  printf("\n## C/N0 (dB-Hz) and ideal minimum airtime for k bits (eps = 1e-3)\n\n");
  printf("Noise densities: -150 dBm/Hz (quiet rural/galactic, upper HF), -140 dBm/Hz (quiet rural ~7-10 MHz or rural ~20 MHz), -130 dBm/Hz (rural ~7 MHz, residential ~14 MHz).\n\n");
  printf("| N0 (dBm/Hz) | Pt | L (dB) | C/N0 (dB-Hz) | 128 b: Shannon | 128 b: finite-k (orthogonal) | 1 KiB: Shannon | 1 KiB: finite-k | 1 KiB at +6 dB |\n");
  printf("|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
  double x128 = orth_energy(128, 1e-3), x8k = ppv_energy(8192, 1e-3);
  for (double n0 : n0s)
    for (double pm : pts_mw)
      for (double L : losses) {
        double pt = db(pm);
        double cn0db = pt - L - n0;
        double cn0 = undb(cn0db);
        char a[32], b[32], c[32], d[32], e[32];
        printf("| %.0f | %.0f mW | %.0f | %+.1f | %s | %s | %s | %s | %s |\n", n0, pm, L, cn0db,
               fmt_time(128 * kLn2 / cn0, a), fmt_time(x128 / cn0, b), fmt_time(8192 * kLn2 / cn0, c),
               fmt_time(x8k / cn0, d), fmt_time(x8k / cn0 * undb(6.0), e));
      }
  printf("\nFinite-k energy references (eps=1e-3): k=128 uses exact coherent orthogonal signaling, E/N0 = %.2f (Eb/N0 = %.2f dB; the O(1)-truncated PPV expansion gives %.2f dB); k=8192 uses the PPV expansion, Eb/N0 = %.2f dB; asymptote -1.59 dB.\n",
         x128, db(x128 / 128), db(ppv_energy(128, 1e-3) / 128), db(x8k / 8192));
  printf("\n## Finite-k minimum Eb/N0 (dB), AWGN, no bandwidth limit, no feedback (PPV expansion)\n\n| k bits | eps=1e-1 | 1e-2 | 1e-3 | 1e-4 | 1e-6 |\n|---:|---:|---:|---:|---:|---:|\n");
  for (double k : {8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0, 4096.0, 8192.0, 65536.0, 1048576.0}) {
    printf("| %.0f |", k);
    for (double eps : {1e-1, 1e-2, 1e-3, 1e-4, 1e-6}) printf(" %.2f |", db(ppv_energy(k, eps) / k));
    printf("\n");
  }
  return 0;
}
