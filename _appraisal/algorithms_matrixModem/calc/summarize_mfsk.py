#!/usr/bin/env python3
"""Summarize mfsk_capacity output into markdown tables (standard library only).

Usage: python3 -I summarize_mfsk.py results/mfsk_capacity_B2200.txt > results/mfsk_summary.md
"""
import math
import sys

path = sys.argv[1]
best = {}   # (model, cn0) -> (rate, T, M)
mat = {}    # (model, T, cn0) -> rate
order = []
for line in open(path):
    if line.startswith('#'):
        continue
    f = line.split()
    if not f:
        continue
    if f[0] == 'MAT':
        model = f[1]
        T = float(f[2].split('=')[1])
        cn0 = float(f[5].split('=')[1])
        rate = float(f[-1].split('=')[1])
        mat[(model, round(T, 5), cn0)] = rate
    else:
        model, cn0 = f[0], float(f[1])
        best[(model, cn0)] = (float(f[2]), float(f[3]), int(f[4]), float(f[8]))
        if model not in order:
            order.append(model)

def ebn0(cn0, r):
    return cn0 - 10 * math.log10(r) if r > 0 else float('nan')

cn0s = [-10.0, -6.0, -2.0, 2.0, 6.0, 10.0, 14.0, 20.0]
print('## Required Eb/N0 (dB) at the best symbol duration, and that duration\n')
print('Entry: Eb/N0 [loss vs Shannon at the same rate] (T* s). Shannon Eb/N0 at these low rates is about -1.59 dB.\n')
print('| model | ' + ' | '.join(f'{c:+.0f} dB-Hz' for c in cn0s) + ' |')
print('|---|' + '---:|' * len(cn0s))
for m in order:
    cells = []
    for c in cn0s:
        r, T, M, loss = best[(m, c)]
        cells.append(f'{ebn0(c, r):.1f} [{loss:.1f}] ({T:.3g})')
    print(f'| {m} | ' + ' | '.join(cells) + ' |')

for Tfix in (0.04, 0.16, 0.64, 2.56, 10.24):
    print(f'\n## Eb/N0 (dB) with a fixed symbol duration T = {Tfix} s (tone spacing 1/T + 4 sigma), and penalty vs best T\n')
    print('| model | ' + ' | '.join(f'{c:+.0f} dB-Hz' for c in cn0s) + ' |')
    print('|---|' + '---:|' * len(cn0s))
    for m in order:
        cells = []
        for c in cn0s:
            r = mat.get((m, round(Tfix, 5), c))
            rb = best[(m, c)][0]
            if r is None or r <= 0:
                cells.append('-')
            else:
                cells.append(f'{ebn0(c, r):.1f} (+{10*math.log10(rb/r):.1f})')
        print(f'| {m} | ' + ' | '.join(cells) + ' |')

# Required C/N0 for target rates (log interpolation over the best-rate curve)
print('\n## C/N0 (dB-Hz) needed for a target information rate at the best symbol duration\n')
targets = [0.001, 0.01, 0.1, 1.0, 10.0]
print('| model | ' + ' | '.join(f'{t:g} bit/s' for t in targets) + ' |')
print('|---|' + '---:|' * len(targets))
allc = sorted({c for (m, c) in best})
for m in order:
    pts = [(c, best[(m, c)][0]) for c in allc if best[(m, c)][0] > 0]
    cells = []
    for t in targets:
        val = None
        for (c1, r1), (c2, r2) in zip(pts, pts[1:]):
            if r1 <= t <= r2:
                a = (math.log10(t) - math.log10(r1)) / (math.log10(r2) - math.log10(r1))
                val = c1 + a * (c2 - c1)
                break
        cells.append(f'{val:.1f}' if val is not None else ('< ' + f'{pts[0][0]:.0f}' if t < pts[0][1] else '> range'))
    print(f'| {m} | ' + ' | '.join(cells) + ' |')
sh = []
for t in targets:
    # Shannon in 2200 Hz: C/N0 = B(2^(R/B)-1)
    sh.append(f'{10*math.log10(2200*(2**(t/2200)-1)):.1f}')
print('| Shannon AWGN (2.2 kHz) | ' + ' | '.join(sh) + ' |')
