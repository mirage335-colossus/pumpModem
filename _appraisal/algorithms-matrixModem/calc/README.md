# Matrix Modem research calculations (Opus-5.5)

Standalone calculation programs supporting the research in
[`_ref/algorithms-matrixModem/Opus-5.5`](../../../_ref/algorithms-matrixModem/Opus-5.5/README.md).
They are analysis aids, not modem code: none is linked into the application,
and none depends on repository sources. C++ uses only the standard library
(OpenMP for threads); Python uses only the standard library and is run with
`-I`.

**Purpose and cleanup trigger.** These reproduce the tables cited in the
research chapters. Remove them (keeping the distilled tables in the chapters)
when a native Matrix simulator reproduces the capacity, quantization and
observer results, or when the chapters are superseded.

## Programs

| Program | What it computes | Output |
| --- | --- | --- |
| `link_budget.cpp` | P.372 man-made + galactic noise; C/N0 grid; Shannon and finite-k airtimes | `results/link_budget.md` |
| `finite_energy.cpp` | Exact one-shot orthogonal signaling energy (coherent / one unknown phase) for k bits; Rayleigh fade margin vs independent fades; log-normal margin | `results/finite_energy.md` |
| `mfsk_capacity.cpp` | Symbol-wise mutual information of noncoherent MFSK (AWGN unknown phase, slow Rayleigh, Gaussian-Doppler Rayleigh with estimator-correlator), maximized over symbol duration in a 2.2 kHz band; control-variate Monte Carlo validated against the analytic low-SNR expansion | `results/mfsk_capacity_B2200.txt` (−10…+34 dB-Hz), `results/mfsk_capacity_B2200_low.txt` (Doppler models, −30…−10 dB-Hz) |
| `summarize_mfsk.py` | Markdown tables from the capacity output (best T, fixed-T penalties, C/N0 for target rates) | `results/mfsk_summary.md` |
| `airtime_table.py` | 128-bit and 1 KiB airtimes at the best-T rate | `results/airtime_table.md` |
| `peak_limited_bound.py` | Peak-limited wideband capacity C/N0 − ∫ln(1+(C/N0)S) df and comparison with the Monte Carlo rates | `results/peak_limited_bound.md` |
| `quantization.cpp` | Weak-signal correlation efficiency of 1–8-bit, INT4, E2M1 and common codebooks; template quantization; 4-bit×4-bit Monte Carlo; 16-level energy requantization MI | `results/quantization.md` |
| `false_alarm.cpp` | Required Es/N0 vs number of hypotheses and noncoherent looks (Bonferroni, Marcum) | `results/false_alarm.md` |
| `lpi_observer.cpp` | Unkeyed observers (radiometer, clairvoyant LRT, noise-invariant LRT) vs keyed receiver, with tile spreading G; SNR wall | `results/lpi_observer.md` |
| `workload.py` | Dense-equivalent operation counts of candidate receiver stages vs device peaks | `results/workload.md` |

Shared helpers are in `mm_common.hpp` (RNG, log-I0, Q function, Jacobi
eigenvalues, Gaussian-Doppler Karhunen–Loève spectrum).

## Build and run

```bash
cd _appraisal/algorithms-matrixModem/calc
g++ -O3 -march=native -fopenmp -std=c++20 mfsk_capacity.cpp -o /tmp/mfsk_capacity
/tmp/mfsk_capacity 2200 1500 > results/mfsk_capacity_B2200.txt
/tmp/mfsk_capacity 2200 1000 dopp -30 -10 > results/mfsk_capacity_B2200_low.txt
python3 -I summarize_mfsk.py results/mfsk_capacity_B2200.txt > results/mfsk_summary.md
```

The stored capacity outputs keep the best-duration rows and the per-duration
`MAT` rows only for the reference durations (40 ms … 10.24 s) that the
summaries use; rerunning the program regenerates the full duration grid.

The other C++ programs take no arguments (`mfsk_capacity` takes bandwidth,
trials, an optional model-name filter and an optional C/N0 range). On a
12-thread laptop the full capacity sweep takes about 3 minutes, the observer
calculation about 3 minutes and quantization about 1 minute.

## Assumptions to keep in view

- Rayleigh fading with Gaussian Doppler spectra (F.1487 2σ convention); tone
  spacing 1/T + 4σ with no inter-tone leakage; independent fading between
  symbols (ideal interleaving); perfect timing and frequency unless stated.
- Mutual information is symbol-wise (memoryless receiver): a lower bound on
  what joint multi-symbol processing could achieve.
- Monte Carlo accuracy is about ±0.05–0.1 dB in the reported Eb/N0 values;
  the best-T maximization uses a 0.05-decade duration grid capped at 300 s
  (60 s for Doppler models).
