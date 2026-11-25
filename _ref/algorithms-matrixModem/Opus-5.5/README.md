# Matrix Modem research (Opus-5.5 pass)

24 November 2026 · Research before implementation

Matrix Modem is a proposed standalone, one-way, very-weak-signal modem whose
receiver should run almost entirely as low-precision matrix multiplication.
This pass studied the natural channel from benign to worst case, estimated
theoretical limits, compared existing weak-signal systems, checked what the
target hardware really provides, and asked where heavy computation can buy
sensitivity. It is the 24 November 2026 Opus-5.5 pass, based on the initial
Sol-6.2 material then available. It corrects several points in that earlier
pass and reuses the repository's HF, noise and propagation research. The
Sol-6.2 directory also contains later, separately dated follow-up research;
the directories record research passes rather than a permanent authority order.
For those later refinements, see the [Sol-6.2 overview](../Sol-6.2/README.md),
its [research-pass index](../Sol-6.2/research-pass-index.md), and the
[fading-limit and MFSK alternatives study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md).
No modem was implemented. All performance figures are model calculations or
literature values under stated assumptions; better designs may exist.

## Main conclusions

The original findings below retain this pass's values and recommendations.
Read conclusions 2–3 with the [25 November qualifications](#dated-qualifications-25-november-2026)
on the capacity expression, idealized MFSK sweeps and finite-message limits.

1. **Realistic HF noise makes the target links much weaker than earlier
   estimates.** ITU-R P.372 medians put HF receive noise at about −144 to
   −130 dBm/Hz at 7–10 MHz (quiet rural to rural), not −164. 2 mW over 150 dB is
   about −7 dB-Hz; over 180 dB about −37 dB-Hz.
   ([link budget](02-noise-and-link-budget.md))
2. **The energy received per fading coherence interval governs everything.**
   For a peak-limited transmitter in Rayleigh fading with a Gaussian Doppler
   spectrum, the capacity is C/N0 − ∫ln(1 + (C/N0)·S_H(f))df. Independent
   Monte Carlo of keyed long-symbol MFSK reaches 97–99.8% of it at low C/N0.
   Consequence: at −10 dB-Hz the minimum Eb/N0 is about −1.6 dB on a stable
   path, 5.4 dB at 0.1 Hz Doppler spread, 14 dB at 1 Hz, 24 dB at 10 Hz and
   29 dB at 30 Hz. No waveform or receiver beats this under those assumptions;
   discrete-mode or specular channels can do better.
   ([limits](03-information-limits-and-noncoherent-penalties.md))
3. **Unknown phase is nearly free; short symbols are not.** A constant unknown
   phase costs 0.1–0.5 dB for 16–128-bit messages. A fixed 160 ms symbol costs
   5–20 dB at low C/N0 in quiet and moderate channels. Symbol duration should
   follow the design C/N0 (about 6–10 dB Es/N0 per symbol), tone spacing the
   Doppler category, and bandwidth the equipment. Long symbols stay
   near-optimal in every Doppler category at low C/N0.
   ([design](05-waveform-and-parameter-design.md))
4. **Recommended starting family:** keyed, constant-envelope, one-tone-at-a-time
   long-symbol MFSK with keyed hopping for time diversity, a Doppler-subspace
   receiver bank, keyed decoy templates as live null hypotheses, and a
   keyed-noise-tile variant for observer resistance. Simultaneous multi-carrier
   (triple-Robust style), wideband DSSS, OFDM and OTFS are disfavoured at low
   C/N0 because they spread energy below the per-cell knee.
5. **A well-designed live receiver has little matrix work.** Even as dense
   GEMMs, the recommended single-stream receivers need under 0.01% of an RTX
   4090. Full GPU use comes from codeword-level joint correlation over coherent
   blocks (k ≈ 24–32 bits; expected 0.5–2 dB on coherent paths), wide blind
   acquisition, robustness banks, and above all batched Monte Carlo simulation,
   which addresses the slow-development problem directly. Earlier ~1%
   utilization is the documented failure mode of per-hypothesis and small-FFT
   work, not a hardware ceiling.
   ([algorithms and utilization](08-gemm-receiver-algorithms-and-utilization.md))
6. **Four-bit arithmetic costs about 0.1 dB; Blackwell has no INT4 Tensor
   Cores.** INT4 is native on the Ada GPUs; RTX 5090 and RTX PRO 6000 provide
   4-bit speed only through block-scaled FP4 (E2M1). The 11-level integer
   codebook {0, ±1, ±2, ±3, ±4, ±6} is exact on every 4-bit route and loses
   only 0.09 dB as an input quantizer, motivating a proposed numerical contract
   with per-device lowering. Wide accumulation and FP4 or INT8 substitution
   remain proposals requiring an explicit decision; they do not resolve or
   relax the original strict INT4 requirement.
   ([precision](07-matrix-hardware-and-precision.md))
7. **Observer resistance needs low energy per observed cell.** A radiometer is
   defeated by noise-level uncertainty at these levels, but an unkeyed observer
   who knows the tone grid can detect efficient tones (+3 to +6 dB per symbol)
   within about one message. Keyed spreading inside each tile (16–64
   dimensions) raises that to about 3–140 messages at no cost to the keyed
   receiver. ([observer](06-observer-lpi-and-interference-perception.md))
8. **Outages and slow fading set completion probability for long messages.**
   Fadeouts of 10–140 minutes, about 6 hours a day of MUF closure on a fixed
   frequency, and multi-day absorption events require erasure coding across the
   whole message; short messages on quiet paths are limited by too few
   independent fades. ([channel](01-hf-channel-moderate-to-worst.md))

## Dated qualifications (25 November 2026)

The headline `97–99.8%` is the rate ratio observed in the stored idealized
keyed-MFSK sweeps, with ideal synchronization and interleaving. It is not a
general finite-message or implemented-receiver guarantee. The comparison
converse is conditional on a hard time peak equal to average power, no channel
state information, flat Rayleigh fading, and asymptotically long coding. The
later Sol-6.2 [information-limits review](../Sol-6.2/information-limits-review.md)
and [fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md)
audit those premises and distinguish them from finite-band, finite-alphabet
results. In the separate 2.2 kHz finite-alphabet Monte Carlo, an adapted
receiver could approach the bound at low C/N0 when dwell and spacing suited the
case, while alphabet limits, symbol noncoherence, and channel mismatch remained
visible in other cases; see the study's [finite-alphabet results](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#finite-alphabet-in-a-22-khz-band).

Likewise, the original statement that long symbols stay near-optimal across
Doppler categories is supported by the pass's idealized, duration-optimized
sweeps; it is not a guarantee for every finite-band configuration or message
deadline. The later study separates large-alphabet results from finite-band
tests and reports a finite-burst approach to the bound with longer dwells, but finite alphabet, band limitation,
outage, acquisition and practical receiver costs still matter. It also records
conditions where long per-symbol noncoherent dwells have a measurable gap from
the coherent limit. These qualifications refine the scope of the original
conclusion; they do not erase the historical calculation or its cited sources.
Even within the original sweep, a fixed 2.5–10 s recommendation is too broad:
the [design chapter](05-waveform-and-parameter-design.md#recommendations-in-brief) records the
sampled spread-fading benefits and the larger AWGN/slow-Rayleigh penalties,
relative to each model's best duration at the same C/N0.

## Reading map

| Chapter | Content |
| --- | --- |
| [01 HF channel](01-hf-channel-moderate-to-worst.md) | Benign→extreme categories, Doppler shift/drift/spread, fades, outages, contradictions |
| [02 Noise and link budget](02-noise-and-link-budget.md) | P.372 noise, C/N0 tables, ideal and channel-aware airtimes, energy-optimal power |
| [03 Limits](03-information-limits-and-noncoherent-penalties.md) | Peak-limited capacity, MFSK cross-check, symbol duration, finite length, diversity, search cost, what is ruled out |
| [04 Empirical anchors](04-empirical-weak-signal-anchors.md) | WSJT-X modes, EbNaut, military HF, deep space, GNSS on one scale |
| [05 Design](05-waveform-and-parameter-design.md) | Parameter mapping, waveform families, hypothesis/null construct, coding, Helper role |
| [06 Observer](06-observer-lpi-and-interference-perception.md) | Unkeyed observer vs keyed receiver, SNR wall, measurement protocol |
| [07 Hardware and precision](07-matrix-hardware-and-precision.md) | Device routes and peaks, INT4/FP4 facts, quantization losses, numerical contract |
| [08 Algorithms and utilization](08-gemm-receiver-algorithms-and-utilization.md) | Matrix pipeline, workload sizing, where compute buys dB, kernel lessons, simulation |
| [09 Keys, spectrum, stream](09-keys-schedules-spectrum-and-stream-contract.md) | Shared keyfile, schedule derivation, wrong-key filtering, regulation pointers |
| [10 Other paths](10-other-propagation-paths.md) | Sub-9 kHz, groundwave, EME through the same lens |
| [11 Evaluation plan](11-evaluation-and-simulation-plan.md) | Staged, hierarchical experiments, strata, falsifiers, decisions needed |
| [12 Reconciliation](12-reconciliation-and-open-questions.md) | Corrections to Sol-6.2, prior modems, disagreements, open questions |

Supporting material:

- `reviews/` holds the six literature reviews made for this pass, with
  per-source access notes (full text, abstract, snippet):
  [HF channel](reviews/review-hf-channel.md),
  [noncoherent theory](reviews/review-noncoherent-theory.md),
  [weak-signal modes](reviews/review-weak-signal-modes.md),
  [GPU hardware](reviews/review-gpu-hardware.md),
  [tensor-core signal processing](reviews/review-tensor-core-dsp.md) and
  [GEMM-shaped decoding](reviews/review-gemm-decoding.md). The chapters are
  the maintained synthesis; the reviews are the source audit behind them.
  Several reviews exhausted their search budgets; their gap lists say what was
  not found.
- Calculation programs and outputs:
  [`_appraisal/algorithms-matrixModem/calc`](../../../_appraisal/algorithms-matrixModem/calc/README.md).

## Decisions for the user

1. FP4 (E2M1) instruction with integer-valued operands on Blackwell, or INT8 at
   half the rate.
2. Whether "transmit power" is a peak limit or an energy/average limit (it
   changes the energy-optimal design in disturbed channels).
3. Target message sizes and reliability (50/90/99%) for choosing one-way margins.
4. Integrity tag bits, and separation of secret-key and hopping profiles from
   amateur profiles.

## What this pass did not establish

No Matrix waveform or receiver was implemented, simulated at sample level or
run on a GPU. The capacity calculations assume Rayleigh fading with Gaussian
Doppler spectra, ideal coding, ideal interleaving and known synchronization
unless stated. Hardware numbers are vendor peaks or derived from them. Channel
statistics are conditional on their measurement geometry and estimator, and
several key distributions (fade durations, drift rates, modern high-latitude
spreads) are missing from the literature found.
