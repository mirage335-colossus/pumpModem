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

## Build and run

```bash
cd _appraisal/algorithms-matrixModem/coherence-limits
g++ -O2 -std=c++20 bound_shapes.cpp -o /tmp/bound_shapes && /tmp/bound_shapes > results/bound_shapes.md
g++ -O2 -std=c++20 wideband_receivers.cpp -o /tmp/wideband_receivers && /tmp/wideband_receivers > results/wideband_receivers.md
g++ -O3 -march=native -std=c++20 -fopenmp finite_alphabet_mc.cpp -o /tmp/finite_alphabet_mc && /tmp/finite_alphabet_mc 10000 > results/finite_alphabet_mc.md
python3 -I summarize_mc.py results/finite_alphabet_mc.md > results/finite_alphabet_summary.md
g++ -O2 -std=c++20 finite_message.cpp -o /tmp/finite_message && /tmp/finite_message > results/finite_message.md
python3 -I psk_lowsnr.py > results/psk_lowsnr.md
```

`finite_alphabet_mc` accepts `[trials] [channel-name substring] [C/N0 dB-Hz]` to rerun one stratum.

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
