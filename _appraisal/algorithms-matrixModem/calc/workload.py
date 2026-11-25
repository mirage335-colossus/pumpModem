#!/usr/bin/env python3
"""Operation counts of candidate Matrix receiver stages, expressed as dense
complex GEMM work (8 real ops per complex MAC), per second of received signal,
against dense vendor peaks of the 4-bit-capable route on each GPU.
Assumptions are explicit per row; these are sizing arithmetic, not measurements."""
fs = 2400.0          # complex samples/s after decimation (2.2 kHz usable band)
B = 2200.0
peaks = {            # dense peak, Tera-ops/s, 4-bit route (official tables; laptop/4060Ti derived)
    'RTX 4090 INT4': 1321.2, 'RTX 4060 Ti 16GB INT4': 353.0, 'RTX 5090 FP4': 1676.0,
    'RTX PRO 6000 FP4': 2015.2, 'RTX PRO 6000 INT8': 1007.6, 'Radeon 780M IU4 (HIP)': 33.2,
}
rows = []
def add(name, ops_per_s, note):
    rows.append((name, ops_per_s, note))
# W1 tone bank as dense DFT-GEMM, 160 ms symbols, all in-band bins, 4 starts per symbol
T = 0.16; N = fs * T; M = B * T
add('W1 tone bank DFT-GEMM, T=0.16 s', 8 * N * M * 4 / T, 'N=384, 352 bins, 4 window starts/symbol')
# W2 long symbols, dense DFT-GEMM on a half-bin grid, drift x Doppler-subspace hypotheses
T = 10.24; N = fs * T; bins = 2 * B * T; r = 4; D = 9
add('W2 long-symbol bank, T=10.24 s', 8 * N * bins * r * D * 4 / T, f'N={N:.0f}, {bins:.0f} half-bin freqs, rank {r} subspace, {D} drifts, 4 starts/symbol')
# W3 keyed noise-like tiles, constrained search (clock + Helper guidance)
T = 2.56; W = 25.0; Nt = 2 * W * T; Mk = 128; offs = 100 * 21 * 5
add('W3 keyed tiles, guided search', 8 * Nt * Mk * offs / T, 'W=25 Hz, 64 dims, 64 data+64 null keyed waveforms, 100 time x 21 freq x 5 drift offsets')
# W3b same, blind search (no Helper, epoch known to +-T, frequency +-50 Hz)
offs = 256 * 500 * 5
add('W3b keyed tiles, blind search', 8 * Nt * Mk * offs / T, '256 time x 500 freq x 5 drift offsets')
# W4 joint codeword correlation over a coherent block (k bits per 3-symbol block), after acquisition
for k in (20, 24, 28, 32):
    Dblk = 3 * 2.56; Nb = fs * Dblk; offs = 50
    add(f'W4 codeword-level correlation, k={k}', 8 * Nb * 2 ** k * offs / Dblk, f'{Dblk:.2f} s coherent block, {offs} residual offsets')
# W5 Monte Carlo: 10^4 independent trials of W3b per second of simulated signal
add('W5 simulation batch, 10^4 x W3b', 1e4 * 8 * Nt * Mk * 256 * 500 * 5 / 2.56, 'batch of independent trials; channel synthesis not counted')
print('| Workload | Tera-ops per signal-second | ' + ' | '.join(f'% of {k}' for k in peaks) + ' | Assumptions |')
print('|---|---:|' + '---:|' * len(peaks) + '---|')
for name, ops, note in rows:
    t = ops / 1e12
    print(f'| {name} | {t:.3g} | ' + ' | '.join(f'{100 * t / p:.2g}' for p in peaks.values()) + f' | {note} |')
