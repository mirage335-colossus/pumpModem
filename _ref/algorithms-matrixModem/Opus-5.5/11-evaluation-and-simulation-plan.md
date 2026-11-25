# Evaluation and simulation plan

Research date: 24 November 2026. A staged plan for turning this research into
evidence, following the repository's policy of hierarchical, computationally
minimal testing ([Robust testing policy](../../../docs/robust-current.md#testing-computationally-intensive-robust-changes)):
predeclare strata, budgets and stopping rules; run cheap deterministic checks
first; never use exhaustive or multi-day runs as routine tests. Each stage has
a decision it must settle and a result that would falsify the current
recommendation. Implementation would live under
`_appraisal/algorithms_matrixModem/` and must not modify other modems.

## Stage 0 — Arithmetic and hardware facts (hours)

| Check | Decision | Falsifier |
| --- | --- | --- |
| Bit-exact 4-bit codebook GEMM vs CPU reference on each available device (adversarial magnitudes, cancellation, long-K chaining, block scales) | Which lowering each device uses | Any mismatch on a route ⇒ route disabled |
| SASS inspection: 4-bit `mma.sync` on sm_120 (expect INT8 emulation); `kind::mxf4nvf4` vs `kind::f8f6f4` vs INT8 rates | Blackwell 4-bit route | FP4 not ≈ 2× INT8 ⇒ use INT8 |
| Ada INT4 sustained rate; RDNA3 WMMA IU4 vs dot8 under HIP; Iris Xe DP4A rate | iGPU and laptop routes | — |

## Stage 1 — Projection-level simulator (days, CPU or GPU)

Generate per-symbol sufficient statistics directly from the channel law, as
`calc/mfsk_capacity.cpp` does, to compare waveform parameters at negligible
cost:

- reproduce the capacity tables, then measure **actual coded performance**
  (LDPC/OSD with 4-bit metrics) against them at 50%, 90% and 99% message
  success;
- sweep T, Δ, alphabet, tile bandwidth W and receiver subspace ranks against
  the channel categories;
- add category mismatch (designed for one spread, received at another) and
  tone-overlap leakage, which the capacity model omits.

Decision: candidate parameter presets per design C/N0 and category.
Falsifier: coded performance more than ~2 dB worse than the symbol-wise
capacity at matched settings would point to a decoder or metric problem before
any hardware work.

## Stage 2 — Sample-level native simulator with batched trials

A C++ transmitter, channel and receiver with a trial batch dimension:

- **Transmitter** renders keyed long-symbol MFSK (family A) and keyed tiles
  (family B) in independent tiles with prefix-computed phase continuity.
- **Channel** applies continuous-state multi-mode fading (Gaussian and
  measured-shape Doppler spectra, distinct mode shifts), common shift with
  heavy-tailed jumps, drift and SFD chirps (~0.15 Hz/s), slow log-normal AR(1)
  fading on ~15-minute scales, O/X polarization fading, diurnal on/off
  availability with abrupt steps, impulsive noise from captures or APD models,
  equipment frequency error (GPSDO, GPS-aided, TCXO), and an optional
  parameterized round-the-world echo labelled as a stress test.
- **Receiver** is the matrix pipeline in the
  [algorithm chapter](08-gemm-receiver-algorithms-and-utilization.md), with
  decoy and wrong-key nulls.
- **Batching**: thousands of independent realizations share templates;
  channel state stays continuous within each realization for the whole message.

Strata (each with an explicit Off/AWGN control):

1. AWGN and static unknown phase (calibration).
2. Benign: 2σ 0.01–0.1 Hz, ≤ 1 ms, drift 0.2–1 mHz/s.
3. Moderate: 0.5–2.5 Hz, 1–3 ms, drift 1–5 mHz/s, sunrise/sunset steps.
4. Severe: 5–15 Hz, 3–7 ms.
5. Extreme stress: 25–73 Hz, 7–11 ms (labelled stress, not a design point).
6. Outage overlays: 10–140 min fadeouts, ~6 h/day MUF gaps, multi-day absorption.
7. Noise: captured HF noise at representative sites, impulsive episodes, narrowband interferers.
8. Wrong-key and other-operator signals at the same and nearby rounded shifts.

Metrics: complete-message success probability at fixed transmitted energy and
duration (50/90/99%), false complete messages and false admissions per hour,
time to physical completion, required C/N0 relative to the capacity tables,
receiver work per signal-second, and the utilization of the instruction
actually executed.

Decision: whether family B (keyed tiles) costs measurably more than family A
for the keyed receiver, and the receiver bank composition. Falsifiers:
decoy-calibrated nulls failing to hold their false-alarm rate on captured
noise; long symbols failing on realistic drift even with tracking.

## Stage 3 — Joint detection and acquisition

Paired equal-energy comparisons on identical channel realizations:

- per-symbol scoring vs coherent block/codeword correlation (k = 8…28) on
  benign, moderate and severe strata;
- sync-symbol vs sync-free joint detection while sweeping search size from 2^10
  to 2^30 cells;
- Helper-off, Helper-on and failed-Helper acquisition at equal total energy.

Decision: whether codeword-level correlation earns its GPU cost (expected
0.5–2 dB on coherent paths). Falsifier: < 0.5 dB gain on benign strata.

## Stage 4 — Observer study

The protocol in the [observer chapter](06-observer-lpi-and-interference-perception.md):
captured noise, paired H0/H1, named observer classes, observer time relative to
complete-message time.

## Stage 5 — Over-the-air and capture replay

Replay of measured channel captures and limited over-the-air trials with
calibrated power, logging equipment frequency references. Simulator-only
thresholds have historically overstated on-air performance; this stage is the
only one that can validate the channel assumptions.

## Decisions needed before Stage 2

1. Whether Blackwell builds may use integer-valued E2M1 operands on the FP4
   instruction ([precision chapter](07-matrix-hardware-and-precision.md)).
2. Whether "transmit power" is a peak or an average/energy limit
   ([link budget](02-noise-and-link-budget.md)).
3. Target message sizes and the reliability level (50/90/99%) that define
   "sufficient margin" for a one-way configuration.
4. Whether profiles carry an authentication tag and how secret-key and hopping
   profiles are separated from amateur profiles
   ([keys chapter](09-keys-schedules-spectrum-and-stream-contract.md)).

## Material lifecycle

Experiment outputs belong in ignored `local-evidence/`, with purpose and
cleanup trigger recorded in the relevant guide; distilled findings return to
these chapters. The calculation programs in `calc/` are research aids for this
document set and should be removed or replaced once a native Matrix simulator
reproduces their tables.
