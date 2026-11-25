#!/usr/bin/env python3
"""Peak-limited wideband capacity of a Rayleigh channel with Gaussian Doppler
spectrum (2*sigma = spread), C = P - int ln(1 + P*S(f)) df  [nats/s], P = C/N0 [Hz]
(Viterbi 1967 constant-envelope FSK bound; Sethuraman & Hajek 2005; as restated by
the theory review).  Compared with the Monte Carlo MFSK best-T rates.
Usage: python3 -I peak_limited_bound.py results/mfsk_capacity_B2200.txt results/mfsk_capacity_B2200_low.txt"""
import math, sys
best = {}
for path in sys.argv[1:]:
    for line in open(path):
        f = line.split()
        if not f or line.startswith('#') or f[0] == 'MAT':
            continue
        best[(f[0], float(f[1]))] = float(f[2])
def bound(cn0_db, two_sigma):
    P = 10 ** (cn0_db / 10)
    s = two_sigma / 2
    # integrate ln(1+P S(f)) over f, S Gaussian with std s, unit area
    n, lim = 20001, 12 * s
    h = 2 * lim / (n - 1)
    acc = 0.0
    for i in range(n):
        f = -lim + i * h
        S = math.exp(-f * f / (2 * s * s)) / (math.sqrt(2 * math.pi) * s)
        w = 1 if i in (0, n - 1) else (4 if i % 2 else 2)
        acc += w * math.log1p(P * S)
    acc *= h / 3
    return (P - acc) / math.log(2)  # bit/s
print('| 2σ (Hz) | C/N0 (dB-Hz) | bound (bit/s) | MFSK best-T MI rate (bit/s) | MFSK / bound | bound Eb/N0 (dB) |')
print('|---:|---:|---:|---:|---:|---:|')
for sp, name in [(0.1, 'dopp-0.1Hz'), (1.0, 'dopp-1.0Hz'), (10.0, 'dopp-10.0Hz'), (30.0, 'dopp-30.0Hz')]:
    for c in [-20, -10, 0, 10]:
        b = bound(c, sp)
        r = best.get((name, float(c)))
        print(f'| {sp} | {c:+d} | {b:.4g} | {r if r else float("nan"):.4g} | {(r/b if r else float("nan")):.3f} | {c - 10*math.log10(b):.2f} |')
