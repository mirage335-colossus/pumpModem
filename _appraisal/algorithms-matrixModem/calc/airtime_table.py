#!/usr/bin/env python3
"""Airtime for 128 bits and 1 KiB at the best-symbol-duration noncoherent MFSK
rate (symbol-wise MI, ideal outer code, ideal interleaving) for selected C/N0.
Usage: python3 -I airtime_table.py results/mfsk_capacity_B2200.txt results/mfsk_capacity_B2200_low.txt"""
import math, sys
best = {}
for path in sys.argv[1:]:
    for line in open(path):
        f = line.split()
        if not f or f[0] in ('#', 'MAT') or line.startswith('#'):
            continue
        best[(f[0], float(f[1]))] = float(f[2])
def rate(model, c):
    pts = sorted((cc, r) for (m, cc), r in best.items() if m == model and r > 0)
    for (c1, r1), (c2, r2) in zip(pts, pts[1:]):
        if c1 <= c <= c2:
            a = (c - c1) / (c2 - c1)
            return 10 ** (math.log10(r1) + a * (math.log10(r2) - math.log10(r1))), False
    if c < pts[0][0] and model == 'awgn-nc':  # constant Eb/N0 extrapolation (needs T >> 300 s)
        c1, r1 = pts[0]
        return r1 * 10 ** ((c - c1) / 10), True
    return None, False
def fmt(s):
    if s is None: return '-'
    if s < 120: return f'{s:.3g} s'
    if s < 7200: return f'{s/60:.3g} min'
    if s < 172800: return f'{s/3600:.3g} h'
    return f'{s/86400:.3g} d'
models = ['awgn-nc', 'dopp-0.1Hz', 'dopp-1.0Hz', 'dopp-10.0Hz']
print('| C/N0 (dB-Hz) | Shannon 1 KiB | ' + ' | '.join(f'{m} 128 b / 1 KiB' for m in models) + ' |')
print('|---:|---:|' + '---:|' * len(models))
for c in [20, 13, 10, 3, 0, -7, -10, -17, -20, -27]:
    cn0 = 10 ** (c / 10)
    cells = []
    for m in models:
        r, ex = rate(m, c)
        if r is None:
            cells.append('-')
        else:
            mark = '*' if ex else ''
            cells.append(f'{fmt(128 / r)}{mark} / {fmt(8192 / r)}{mark}')
    print(f'| {c:+d} | {fmt(8192 * math.log(2) / cn0)} | ' + ' | '.join(cells) + ' |')
print('\n\\* constant-Eb/N0 extrapolation below -10 dB-Hz; requires symbols much longer than 300 s, physically unrealistic on HF skywave.')
