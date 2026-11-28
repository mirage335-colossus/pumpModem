#!/usr/bin/env python3
"""Search time against block duration for the sub-9 kHz configuration, two ways of searching.

Arithmetic on measured times (28 November 2026, Fable-5.2 pass). It replaces section 3 of
search_ceiling.py for the adopted configuration: 512 tones of 10.24 s, sixteen positions in
12.5 Hz, a block of 5,242.88 s.

  per hypothesis   every frequency, drift and timing hypothesis is a full decode on the window
                   ladder (decode_bench.cpp, decode_gpu.cu); what the first count assumed
  transform        per message and timing offset the tone values are gathered once and all
                   frequency and drift hypotheses come from a transform across them
                   (search_bench.cpp with FFTW on the laptop; search_gpu.cu as one FP16 matrix
                   product on Tensor Cores)

Measured inputs are read from the result files. Everything beyond the measured case of 174
frequency bins, 4 drift hypotheses and 20 timing offsets is scaled by the stated rules and is an
estimate, not a measurement:
  - laptop: time per message = gather + timing offsets x (drift hypotheses x rung 0 transform +
    lower rungs), from the parts of the one-offset run; the measured case is the run that executed
    its 20 offsets;
  - GPU: gather time plus a time in proportion to the rows of the matrix operand (drift
    hypotheses x bins on the whole block, plus the bins each lower rung needs in each of its
    windows), at the per-row cost of the 174-bin run up to its 2,294 rows, interpolated towards
    the 696-bin run (8,520 rows) and held at that run's per-row cost beyond;
  - more than 1,024 frequency bins do not fit one transform across tone sums (they exceed half the
    tone's reciprocal): the range is cut into bands of 512 bins, each a separate pass with bin
    values taken at the band's centre (0.9 dB at a band edge);
  - 16 and 10 bits take 2^-8 and 2^-14 of the 24-bit time (measured flat for the lookup kernels,
    assumed for the transforms);
  - a single hypothesis is one plain decode on either device, and where a plain decode per
    hypothesis is the quicker way it is taken.
Run: python3 -I search_margins.py > results/search_margins.md
"""
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCK = 5242.88


def grab(path, pattern, group=1, which=0):
    found = re.findall(pattern, (HERE / path).read_text())
    if len(found) <= which:
        sys.exit(f"{path}: pattern not found: {pattern}")
    hit = found[which]
    return float(hit if isinstance(hit, str) else hit[group - 1])


# measured: laptop, FFTW, 12 threads (first section of the file)
t_gather = grab("results/search_bench.md", r"Gather of 512 tone values and positions, once per message \| ([0-9.]+) us") * 1e-6
t_rung0 = grab("results/search_bench.md", r"Rung 0, per drift hypothesis and timing offset[^|]*\| ([0-9.]+) us") * 1e-6
t_rest = grab("results/search_bench.md", r"All other rungs, per timing offset \| ([0-9.]+) us") * 1e-6
# measured with the 20 timing offsets executed and one gather per message (first such section)
t_cpu20 = grab("results/search_bench.md", r"\| 24 bits \| ([0-9.e+]+) s \| [0-9.e+]+ \|\n")
# measured: GPU
g_rows_a = grab("results/gpu/search_gpu.out", r"rungs 0 to 8 in the product \((\d+) complex rows")
g_ns_a = grab("results/gpu/search_gpu.out", r"gather \+ product \+ epilogue\s+([0-9.]+) ns per message") * 1e-9
g_gather = grab("results/gpu/search_gpu.out", r"gather to FP16 planes: ([0-9.]+) ms per batch") * 1e-3 / grab("results/gpu/search_gpu.out", r"batch (\d+) messages")
g_fft = grab("results/gpu/search_gpu.out", r"whole route, timed as one\s+([0-9.]+) ns per message") * 1e-9
g_rows_b = grab("results/gpu/search_gpu_696bins.out", r"rungs 0 to 8 in the product \((\d+) complex rows")
g_ns_b = grab("results/gpu/search_gpu_696bins.out", r"gather \+ product \+ epilogue\s+([0-9.]+) ns per message") * 1e-9
tflops = grab("results/gpu/search_gpu.out", r"FP16 x FP16 -> FP16: product [0-9.]+ ms \(([0-9.]+) nominal TFLOP/s\)")
# measured: one full-ladder decode of 512 tones, 24 bits
d_cpu = grab("results/decode_bench.md", r"\| 512 \| 9 \| ([0-9.]+) \| [0-9.]+ \| [0-9.e+]+ \| [0-9.e+]+ \| yes \|", which=2)
d_gpu = grab("results/gpu/decode_gpu.out", r"ladder\s+512 tones\s+([0-9.]+) ms/decode") * 1e-3

CLASSES = [  # name, frequency bins, drift hypotheses, timing offsets
    ("GPS-locked, corrections logged", 1, 1, 1),
    ("GPS-disciplined, slewed unseen at 1 ppm", 174, 33, 1),
    ("TCXO, 1 ppm, 0.1 ppm/h, 0.1 s", 174, 4, 20),
    ("Crystal, 20 ppm, 1 ppm/h, 1 s", 3469, 33, 200),
    ("Sound-card clock, 100 ppm, 5 ppm/h, 1 s", 17344, 163, 200),
]


def bands(f):
    return 1 if f <= 1024 else math.ceil(f / 512)


def cpu_transform(f, nd, nt):   # per message: one gather per band, then every timing offset
    return (t_gather + nt * (nd * t_rung0 + t_rest)) * bands(f)


def gpu_rows(f, nd):
    bb = f if f <= 1024 else 512
    return nd * bb + sum((1 << r) * max(1, math.ceil(bb / (1 << r))) for r in range(1, 9))


def gpu_transform(f, nd):
    rows = gpu_rows(f, nd)
    if rows <= g_rows_a:
        per = g_gather + (g_ns_a - g_gather) * rows / g_rows_a
    elif rows <= g_rows_b:
        per = g_ns_a + (g_ns_b - g_ns_a) * (rows - g_rows_a) / (g_rows_b - g_rows_a)
    else:
        per = g_ns_b * rows / g_rows_b
    return per * bands(f)


def fmt(x):
    if x >= 100:
        return f"{x:,.0f}"
    if x >= 10:
        return f"{x:.0f}"
    if x >= 1:
        return f"{x:.1f}"
    return f"{x:.2g}"


print("# Search time against block duration, sub-9 kHz configuration\n")
print("Arithmetic on measured times; the rules and what is scaled are in the head of")
print("`search_margins.py`. Block: 512 tones of 10.24 s, 5,242.88 s.\n")
print("## Measured inputs\n")
print("| Quantity | Value |\n| --- | ---: |")
print(f"| Laptop, one full-ladder decode of 2^24 messages (one hypothesis) | {d_cpu:.3f} s |")
print(f"| Laptop, transform route per message and timing: gather / rung 0 per drift / lower rungs | {t_gather*1e6:.3f} / {t_rung0*1e6:.3f} / {t_rest*1e6:.3f} us |")
print(f"| GPU, one full-ladder decode of 2^24 messages (one hypothesis) | {d_gpu:.4f} s |")
print(f"| GPU, FP16 matrix product route per message and timing, {g_rows_a:.0f} rows | {g_ns_a*1e9:.1f} ns ({tflops:.0f} nominal TFLOP/s in the product) |")
print(f"| GPU, the same with {g_rows_b:.0f} rows (696 bins) | {g_ns_b*1e9:.1f} ns |")
print(f"| GPU, cuFFT route per message and timing, 174 bins | {g_fft*1e9:.1f} ns |")
print("\n## Measured case: 174 frequency bins, 4 drift hypotheses, 20 timing offsets, 24 bits\n")
print("| Route | Search time | Block duration / search time |\n| --- | ---: | ---: |")
M = 2.0 ** 24
rows = [
    ("Laptop, a full decode per hypothesis (13,920 decodes)", 13920 * d_cpu),
    ("Laptop, FFT across tone sums (FFTW, 12 threads), 20 offsets executed", t_cpu20),
    ("GPU, a full decode per hypothesis (13,920 decodes)", 13920 * d_gpu),
    ("GPU, cuFFT across tone sums", g_fft * M * 20),
    ("GPU, FP16 matrix product across tone sums (Tensor Cores)", g_ns_a * M * 20),
]
for name, t in rows:
    print(f"| {name} | {fmt(t)} s | {fmt(BLOCK / t)} |")
print("\n## Reference classes: block duration divided by search time\n")
print("At or above 1 the device keeps up with the block. Only the TCXO row with 24 bits is measured;")
print("the rest is scaled.\n")
for k in (24, 16, 10):
    print(f"### {k}-bit block\n")
    print("| Reference at each end | Hypotheses on the whole block | Laptop, per hypothesis | Laptop, transform | GPU, per hypothesis | GPU, matrix product |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    scale = 2.0 ** (k - 24)
    for name, f, nd, nt in CLASSES:
        hyp = f * nd * nt
        old_c, old_g = hyp * d_cpu * scale, hyp * d_gpu * scale
        new_c = old_c if hyp == 1 else min(old_c, cpu_transform(f, nd, nt) * M * scale)
        new_g = old_g if hyp == 1 else min(old_g, gpu_transform(f, nd) * M * nt * scale)
        print(f"| {name} | {hyp:,} | {fmt(BLOCK/old_c)} | {fmt(BLOCK/new_c)} | {fmt(BLOCK/old_g)} | {fmt(BLOCK/new_g)} |")
    print()
print("## Not in these figures\n")
print("- Forming the bin values for each timing offset (the transform stage) and the presence test.")
print("- At rungs 1 and 2 a drifting reference moves the signal by up to 1.5 bins between windows;")
print("  summing along tilted bin paths in the epilogue covers that and was not timed.")
print("- Energies over 512 bins per band instead of 174 on the laptop (a small part of the transform time).")
print("- Tone lengths other than 10.24 s: with shorter tones the gather grows with the tone count and")
print("  the tone values would be summed into about 512 sub-blocks before the transform; not built.")
