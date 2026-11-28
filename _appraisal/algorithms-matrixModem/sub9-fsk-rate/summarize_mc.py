#!/usr/bin/env python3
"""Condense the tables of coherent_block_mc into one row per format and channel:
the cell rung, per-tone reception, the best window and the receiver that picks a window per block.

Run: python3 -I summarize_mc.py results/coherent_block_mc_fast.md results/coherent_block_mc.md > results/coherent_block_summary.md
"""
import re, sys

def secs(c):
    if c.endswith(' min'): return float(c[:-4]) * 60
    if c.endswith(' s'): return float(c[:-2])
    return None

print("# Sub-9 kHz FSK rate and spacing: Monte Carlo summary\n")
print("Condensed from the `coherent_block_mc` outputs by `summarize_mc.py`. Block E/N0 in dB at 1% wrong decodes;")
print("every format lasts 87.4 minutes. `1/64 ladder` is the best window among those of 1/64 of the block (82 s)")
print("and longer together with per-tone reception, i.e. the specification's default ladder depth.\n")
chans = []; rows = {}
order = []
for fn in sys.argv[1:]:
    for sec in open(fn).read().split('## Format ')[1:]:
        lines = sec.split('\n'); name = lines[0].strip()
        hdr = [l for l in lines if l.startswith('| channel')]
        if not hdr: continue
        cols = [c.strip() for c in hdr[0].strip('|').split('|')]
        tone = float(re.search(r'tones of ([0-9.]+) s', sec).group(1))
        ti = [i for i, c in enumerate(cols) if secs(c) is not None and abs(secs(c) - tone) < 0.02 * tone][0]
        order.append(name)
        for l in lines:
            if l.startswith('| ') and not l.startswith('| channel'):
                f = [c.strip() for c in l.strip('|').split('|')]
                if f[0] not in chans: chans.append(f[0])
                def val(x):
                    try: return float(x)
                    except ValueError: return 99.0
                deep = [val(f[i]) for i, c in enumerate(cols) if secs(c) is not None and (secs(c) >= 81.9 or i == ti)]
                rows[(name, f[0])] = (f[1], f[ti], f[-2], f[-1], "%.1f" % min(deep) if min(deep) < 90 else ">")
for ch in chans:
    print("## %s\n" % ch)
    print("| format | one cell | per tone | best window | window picked per block | 1/64 ladder |")
    print("|---|---:|---:|---:|---:|---:|")
    for name in order:
        if (name, ch) in rows:
            r = rows[(name, ch)]; print("| %s | %s | %s | %s | %s | %s |" % (name, r[0], r[1], r[2], r[3], r[4]))
    print()
