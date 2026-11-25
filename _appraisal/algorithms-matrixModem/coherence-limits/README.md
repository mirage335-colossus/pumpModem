# Coherence-limit calculations (Sol-6.2 pass, 25 November 2026)

Standalone C++ programs that audit the peak-limited noncoherent fading bound and measure how
closely practical FFT tone receivers, and some alternatives, approach it. They support
[the fading-limit and MFSK-alternatives study](../../../_ref/algorithms-matrixModem/Sol-6.2/fading-limit-and-mfsk-alternatives.md).
They are analysis aids, not modem code: nothing here is linked into the application or depends on
repository sources. Standard library only (OpenMP for the Monte Carlo).

**Purpose and cleanup trigger.** These reproduce the tables cited in the study. Remove them, keeping
the distilled tables in the study, when a native Matrix Modem simulator reproduces the receiver
comparisons with acquisition, coding and physical completion, or when the study is superseded.

## Programs

| Program | What it computes | Output | Runtime (12-thread 5650U) |
| --- | --- | --- | --- |
| `bound_shapes.cpp` | Independent reproduction of the bound `P − ∫ln(1+P·S)`; universal curve in energy per coherence interval; Eb/N0 versus C/N0; time per 100 bits; Doppler-spectrum shape sensitivity including the worst spectrum for a fixed RMS spread (KKT solution); Rician and mixed narrow/wide spectra; receive branches; transmitter peak-to-average; power splitting | `results/bound_shapes.md` | < 1 s |
| `wideband_receivers.cpp` | Exact large-alphabet GMI of quadratic FFT receivers (weighted periodogram, exact estimator-correlator, FT8-like single bin, multi-bin segments, fully coherent) for arbitrary second-order channels; spread-mismatch matrix; tone versus chirp in two-path fading | `results/wideband_receivers.md` | ≈ 10 s |
| `finite_alphabet_mc.cpp` | Monte Carlo GMI in a 2.2 kHz band with M = 11–352 tones, actual leakage between tones, and four receivers including a per-symbol spread GLRT | `results/finite_alphabet_mc.md` | ≈ 21 min at 10,000 trials |
| `summarize_mc.py` | Per-configuration worst case over the Doppler-spread channels and the per-symbol GLRT excess, from the Monte Carlo output | `results/finite_alphabet_summary.md` | < 1 s |
| `finite_message.cpp` | Extra duration needed for a k-bit message to meet a failure probability, from the receiver's information density (exact Gamma law for a flat spectrum, moment-matched for Gaussian) | `results/finite_message.md` | < 1 s |
| `psk_lowsnr.py` | Low-SNR (quadratic-regime) fraction of the bound reached by differential and block-detected PSK and by a single coherent tone bin, optimized over symbol duration | `results/psk_lowsnr.md` | ≈ 6 s |
| `presence_detection.py` | Time to detect one known keyed pattern (known timing, optimum quadratic detector, normal approximation) against the time for 100 bits at the bound, with and without Gaussian Doppler spread | `results/presence_detection.md` | < 1 s |
| `deadline_cn0.py` | Minimum received C/N₀ for 100 bits within 15 min, 1 h, 3 h and 24 h at the bound, for Gaussian spreads 0.1–30 Hz and without fading | `results/deadline_cn0.md` | ≈ 10 s |
| `scenario_airtime.py` | Wall-clock transmit time for 100 bits at 2–100 mW, 150–180 dB and −144 to −130 dBm/Hz, at the bound and with a 4 dB practical-receiver and two-thirds availability allowance | `results/scenario_airtime.md` | ≈ 10 s |

## Build and run

```bash
cd _appraisal/algorithms-matrixModem/coherence-limits
g++ -O2 -std=c++20 bound_shapes.cpp -o /tmp/bound_shapes && /tmp/bound_shapes > results/bound_shapes.md
g++ -O2 -std=c++20 wideband_receivers.cpp -o /tmp/wideband_receivers && /tmp/wideband_receivers > results/wideband_receivers.md
g++ -O3 -march=native -std=c++20 -fopenmp finite_alphabet_mc.cpp -o /tmp/finite_alphabet_mc && /tmp/finite_alphabet_mc 10000 > results/finite_alphabet_mc.md
python3 -I summarize_mc.py results/finite_alphabet_mc.md > results/finite_alphabet_summary.md
g++ -O2 -std=c++20 finite_message.cpp -o /tmp/finite_message && /tmp/finite_message > results/finite_message.md
python3 -I psk_lowsnr.py > results/psk_lowsnr.md
python3 -I presence_detection.py > results/presence_detection.md
python3 -I deadline_cn0.py > results/deadline_cn0.md
python3 -I scenario_airtime.py > results/scenario_airtime.md
```

`finite_alphabet_mc` accepts `[trials] [channel-name substring] [C/N0 dB-Hz]` to rerun one stratum.

All runs were local on the 12-thread Ryzen 5 PRO 5650U; the longest, the Monte Carlo, took 21 min 27 s at 10,000 trials. RunPod was not used, because nothing in this pass needed more than local CPU time.

## Literature-check calculations

`literature-checks/` holds the small calculations that the literature reviewers of this pass wrote to check or extend published values. The documents cite them as [derived]. They were rerun from these copies on 25 November 2026 and reproduce the cited values; the Monte Carlo values agree within 0.05 dB.

| Program | What it computes | Output | Runtime |
| --- | --- | --- | --- |
| `deflection_losses.py` | Low-energy-coherence deflection losses of unweighted boxes and mis-scaled weights on Gaussian, flat and Jakes spectra | `results/deflection_losses.txt` | < 1 s |
| `quantization_losses.py` | Quantization efficiency of Gaussian input; DFT-matrix error power for FP16, INT8, INT4 and NVFP4; losses with a strong narrowband interferer before isolation | `results/quantization_losses.txt` | ≈ 1 min |
| `orthogonal_mi.cpp` | Symbol mutual information of noncoherent M-ary orthogonal signaling in AWGN and with energy split over `L` Rayleigh branches (Q65's data rate and Turbo-FSK comparisons) | `results/orthogonal_mi.txt` | ≈ 1–2 min per alphabet |
| `finite_burst.cpp` | Loss of capacity per unit energy from a finite constant-envelope burst, exact Gaussian log-determinant | `results/finite_burst.txt` | ≈ 26 s |
| `branch_power.py` | Per-branch C/N0 reduction for 2 and 4 independent or correlated receive branches | `results/branch_power.txt` | ≈ 5 s |

```bash
cd _appraisal/algorithms-matrixModem/coherence-limits/literature-checks
python3 -I deflection_losses.py > results/deflection_losses.txt
python3 -I quantization_losses.py > results/quantization_losses.txt
g++ -O2 -std=c++20 orthogonal_mi.cpp -o /tmp/orthogonal_mi && /tmp/orthogonal_mi 64 1.2381 200000 > results/orthogonal_mi.txt
/tmp/orthogonal_mi 128 1.5 100000 >> results/orthogonal_mi.txt && /tmp/orthogonal_mi 256 1.0 50000 >> results/orthogonal_mi.txt
g++ -O2 -std=c++20 finite_burst.cpp -o /tmp/finite_burst && /tmp/finite_burst > results/finite_burst.txt
python3 -I branch_power.py > results/branch_power.txt
```

The Q65 Figure 1 table in the practice review was digitized from the guide's PDF vector paths with throwaway scripts that were not kept; the table, not the extraction, is the record.

## Conventions and assumptions to keep in view

- Complex baseband, `N0 = 1`, received C/N0 `P` in hertz. Peak equals average received power
  (constant envelope) unless a table says otherwise. Doppler spreads use the F.1487 `2σ` convention.
- The **gap** reported by the receiver programs is `10·log10(R_bound/R)` at the same C/N0: the factor
  of extra transmit time, and energy, at the same transmit power. In the low-SNR (quadratic) regime
  rate scales with the square of power, so the equivalent C/N0 increase needed to restore the rate is
  about half of that dB value; at high energy per coherence interval the two coincide.
- Large-alphabet results assume ideal interleaving across symbols, known symbol timing and known
  composite Doppler centre. Delays shorter than the dwell only rotate path phases for a tone; ISI from
  the previous symbol (at most `τ/T` of the energy) is ignored. Acquisition, drift, nonstationarity,
  impulsive noise and interference are not modelled.
- The large-alphabet GMI of a quadratic receiver depends only on second-order channel statistics, so
  it holds unchanged for Rician or other non-Gaussian fading with the same autocorrelation.
- The Monte Carlo uses a control variate (identical noise with and without the signal plus an
  all-reference noise average). Reported `±` values are about two standard errors of the gap. Points
  whose GMI is not resolved are marked `n/s`; the wideband calculator covers those regimes.
- `finite_message.cpp` uses an information-density heuristic (normal/Gamma approximation of the
  random-coding error), not a converse.
