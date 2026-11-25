"""Presence detection of one known keyed tone pattern versus decoding a message, under the
Doppler-spread bound's assumptions (constant envelope, Rayleigh fading with a Gaussian Doppler
spectrum, known timing and frequency, N0 = 1, complex baseband, received C/N0 = P in hertz).

Hypotheses for one keyed pattern of duration T:
  H0: noise only in the keyed bins, H1: the keyed tone with Doppler spectrum P*S(f) plus noise.
Both are stationary Gaussian processes, so over T >> 1/B_eff the optimum (estimator-correlator)
log-likelihood ratio is a weighted sum of bin energies with weight w = P*S/(1+P*S). Its mean
separation and variances grow linearly in T; a normal approximation then gives the time needed
for a stated false-alarm and detection probability. The divergence rate D(H1||H0) equals the
Doppler bound's rate P - integral ln(1 + P*S), which is why the bound and this test agree.

Reference rows:
  'no fading' = static channel with known phase (coherent matched filter, d^2 = 2*P*T),
  '100 bits at bound' = 100*ln 2 / (P - integral ln(1+P*S)) seconds, 'no fading' uses P nat/s.

Run: python3 -I presence_detection.py > results/presence_detection.md
"""

import math
from statistics import NormalDist

PFA = 1e-6
PD = 0.9
Z0 = NormalDist().inv_cdf(1 - PFA)   # 4.753
Z1 = NormalDist().inv_cdf(PD)        # 1.2816
D_COH = Z0 + Z1                      # deflection needed by a Gaussian-shift test


def gaussian_integrals(p, two_sigma, n=20001, span=10.0):
    """Return (a, b0, b1, rate) per second for a Gaussian spectrum of width 2*sigma."""
    sigma = two_sigma / 2
    lo, hi = -span * sigma, span * sigma
    h = (hi - lo) / (n - 1)
    a = b0 = b1 = r = 0.0
    for i in range(n):
        f = lo + i * h
        s = math.exp(-0.5 * (f / sigma) ** 2) / (sigma * math.sqrt(2 * math.pi))
        q = p * s
        w = q / (1 + q)
        c = 1 if i in (0, n - 1) else (4 if i % 2 else 2)
        a += c * q * w
        b0 += c * w * w
        b1 += c * q * q
        r += c * (q - math.log1p(q))
    k = h / 3
    return a * k, b0 * k, b1 * k, r * k


def fmt_time(t):
    for unit, size in (("yr", 365.25 * 86400), ("d", 86400), ("h", 3600), ("min", 60)):
        if t >= 2 * size:
            return f"{t / size:.3g} {unit}"
    return f"{t:.3g} s"


def main():
    spreads = [0.1, 1.0, 10.0, 30.0]
    levels = [-20, -10, 0]
    print("# Presence detection of a known keyed pattern versus a 100-bit message\n")
    print(f"P_FA = {PFA:g}, P_D = {PD:g} (z = {Z0:.3f} + {Z1:.3f}), Gaussian Doppler spectra, "
          "known timing and frequency, optimum quadratic detector, normal approximation.\n")
    print("`x` is the received energy per effective coherence interval, (C/N0)/B_eff; "
          "`looks` is T*B_eff, the number of effectively independent coherence intervals observed.\n")
    print("| C/N0 | 2σ | x | detect pattern | looks | 100 bits at bound | ratio |")
    print("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for lvl in levels:
        p = 10 ** (lvl / 10)
        t_det = D_COH ** 2 / (2 * p)
        t_msg = 100 * math.log(2) / p
        print(f"| {lvl} dB-Hz | no fading | — | {fmt_time(t_det)} | — | {fmt_time(t_msg)} "
              f"| {t_msg / t_det:.1f} |")
        for w in spreads:
            a, b0, b1, rate = gaussian_integrals(p, w)
            b_eff = w * math.sqrt(math.pi)  # 1/∫S² for a Gaussian of width 2σ = w
            x_db = 10 * math.log10(p / b_eff)
            sqrt_t = (Z0 * math.sqrt(b0) + Z1 * math.sqrt(b1)) / a
            t_det = sqrt_t ** 2
            t_msg = 100 * math.log(2) / rate
            looks = t_det * b_eff
            flag = "" if looks >= 30 else " †"
            print(f"| {lvl} dB-Hz | {w:g} Hz | {x_db:.1f} dB | {fmt_time(t_det)}{flag} | "
                  f"{looks:.3g} | {fmt_time(t_msg)} | {t_msg / t_det:.1f} |")
    print("\n† fewer than 30 effective looks: the normal approximation, and the ergodic "
          "treatment of fading, are unreliable; an outage analysis applies instead.\n")
    print("Quadratic-regime check: for x below about −10 dB the detection time is close to "
          f"`{D_COH ** 2:.1f}·B_eff/(C/N0)²`, so halving power quadruples it, exactly as for "
          "message time at the bound.")


if __name__ == "__main__":
    main()
