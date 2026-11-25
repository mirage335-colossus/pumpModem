"""Minimum received C/N0 for a 100-bit message to fit a stated airtime at the Doppler-spread bound.

Solves P - integral ln(1 + P*S(f)) df = 100*ln(2)/T for P (N0 = 1, complex baseband), with a
Gaussian Doppler spectrum of width 2*sigma, and P = 100*ln(2)/T without fading. This is the
ergodic bound: no finite-length, acquisition or receiver penalty, so every value is a floor that no
waveform or receiver can go below under the bound's assumptions (constant envelope at the peak,
Rayleigh fading, one receive branch, no channel knowledge, stationary channel, Gaussian noise).

Run: python3 -I deadline_cn0.py > results/deadline_cn0.md
"""

import math

BITS = 100
DEADLINES = [("15 min", 900.0), ("1 h", 3600.0), ("3 h", 10800.0), ("24 h", 86400.0)]
SPREADS = [0.1, 0.5, 1.0, 3.0, 10.0, 30.0]


def rate(p, two_sigma, n=20001, span=10.0):
    sigma = two_sigma / 2
    lo, hi = -span * sigma, span * sigma
    h = (hi - lo) / (n - 1)
    acc = 0.0
    for i in range(n):
        f = lo + i * h
        s = math.exp(-0.5 * (f / sigma) ** 2) / (sigma * math.sqrt(2 * math.pi))
        c = 1 if i in (0, n - 1) else (4 if i % 2 else 2)
        acc += c * math.log1p(p * s)
    return p - acc * h / 3


def solve(target, two_sigma):
    lo, hi = math.log(1e-8), math.log(1e6)
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if rate(math.exp(mid), two_sigma) < target:
            lo = mid
        else:
            hi = mid
    return math.exp(0.5 * (lo + hi))


def main():
    print(f"# Minimum C/N0 for {BITS} bits within a stated airtime, at the bound\n")
    print("Gaussian Doppler spectra (`2σ`), ergodic bound, no finite-length, acquisition or "
          "receiver penalty. Each cell is C/N0 in dB-Hz, with `x = (C/N0)/B_eff` in brackets.\n")
    head = "| airtime | no fading | " + " | ".join(f"{w:g} Hz" for w in SPREADS) + " |"
    print(head)
    print("| --- |" + " ---: |" * (len(SPREADS) + 1))
    for label, t in DEADLINES:
        target = BITS * math.log(2) / t
        cells = [f"{10 * math.log10(target):.1f}"]
        for w in SPREADS:
            p = solve(target, w)
            x_db = 10 * math.log10(p / (w * math.sqrt(math.pi)))
            cells.append(f"{10 * math.log10(p):.1f} ({x_db:+.0f})")
        print(f"| {label} | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
