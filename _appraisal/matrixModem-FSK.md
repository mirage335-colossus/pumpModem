# Matrix Modem FSK specification

Draft requirements for the FSK waveform and receiver. [Implementation status](algorithms-matrixModem/block-reception/README.md#implementation-status)

## Waveform profiles

- One full-power, constant-amplitude tone per flat interval. Only frequency position carries data.
- Cell duration `T = 0.160 × 2^j` seconds, integer `j ≥ 0`. Position `n` is
  `f₀ + n/T`, `0 ≤ n < N`, `N ≥ 2`. Hold a tone for `C ≥ 1` whole cells.
- Assign each tone a keyed phase from {0°, 90°, 180°, 270°}, independent of payload and position.
  Successive phases may coincide.

| Parameter | HF | Below 9 kHz |
| --- | --- | --- |
| Lattice scale | `j = 0` | `j = 3`, alternatives 2 or 4 |
| Frequency positions | Default 352 from 450 Hz, 6.25 Hz apart (2.2 kHz grid). Up to 448 in 2.8 kHz. | 16, spaced `1/T`, in a 12.5 Hz nominal grid at `j = 3` |
| Tone length | One or more cells | Eight cells, alternatives one or 32 |
| FHSS | Optional. 0.4 s reference hop, 40 s alternate for one-cell tones. | Off |
| Phase transition | Step at tone boundary | Linear ramp over the first 80 ms of each tone, alternatively 160 ms. Shortest rotation, keyed half-turn direction. |
| Reference | Configured oscillator uncertainty | Both ends GPS-locked, corrections logged or audio retimed from PPS |

- Without FHSS, use contiguous tones without amplitude ramps or guards. Preserve phase
  evolution at the band origin. With FHSS, retune in quiet. Each 0.4 s hop edge uses a
  20 ms raised-cosine ramp and 20 ms quiet. Longer reference hops use 20 ms ramp and 60 ms quiet.
- Target 26 dB out-of-band attenuation relative to mean in-band power, with specified
  filter, measurement bandwidth and averaging interval.

## Block coding and calendar

- Fix the profile and clock-derived public calendar at both ends. No feedback or length fields.
- Encode `k ∈ {10,16,24}` encrypted bits `c` into 32 bytes
  `vₛ = Gₛc XOR mₛ`, using keyed `8 × k` matrices over GF(2) and byte masks.
  Shuffle exactly `L ≥ 1` copies per symbol into `32L` tones.
- Redraw the position map per tone. For `N ≥ 256`, use the first 256 entries of a
  keyed permutation. Otherwise use an agreed `P = 2^floor(log₂ N)` position pool,
  `256/P` bytes per position and `L ≥ ceil(8/log₂ P)`.
- Without FHSS, block duration is `32LCT`. With FHSS,
  `q = floor(flat cells per hop / C) ≥ 1`, `L ≥ q`, and duration is
  `ceil(32L/q)` whole hop periods. Leave residual cells and final unused slots idle.

## Keys and addressing

- Before coding, XOR payload with AES-256-CTR and Serpent-256-CTR keystreams, preserving bit count.
- Separate AES-256 purposes for symbol order, matrices, masks, placement, hops and phase.
  Address by epoch, ordinals and nominal shift rounded to the configured multiple of
  10 Hz. Retunes and measured corrections preserve that address.
  [Key interface and unresolved wire details](../_ref/algorithms-matrixModem/fsk-reconciliation.md#integration-and-profile-selection)

## Receiver search

- **Input.** Fixed gain, linear guarded passband, radio blanking and noise reduction off.
  Translate/filter 48 kHz real audio to 3,200 complex samples/s. Allow wider qualified
  capture for offsets, spread and transitions, or filtered 400 samples/s at `j = 3`.
- **Conditioning.** Estimate Gaussian component noise scale from the lower quartile of
  complex power. Round I/Q at `0.45σ`. Erase the sample if either code is nonfinite
  or outside −7…+7.
- **Coverage.** On the configured sideband, search every word, timing, offset, drift and spread
  hypothesis on active and quiet blocks, including after misses. Require no pilot lock
  or visible trace. Helper may independently narrow search. Fault searches require explicit
  selection and preserve ordinary sensitivity, false-word performance and capacity when off.
- **Projections.** Rectangular matched apertures, including held-tone intermediate frequencies
  and phase ramps. Timing step is phase interval/8, reduced to `1/(8 × band span)`
  for coherent timing where finer. Correct signed-frequency phase, moved boundaries,
  clock slip and intra-window drift, or qualify mismatch. Frequency step is `1/(2U)`
  for coherent window duration `U`.
- **Per-tone evidence.** Choose one spread per block from one bin or Gaussian full-2σ
  widths 10/30/60/120/250/500 Hz. Sum weighted captured-bin energies. Normalize by time
  and frequency medians, using surrounding noise bins for small grids. Weight each hop
  by inverse noise, erase unusable hops, and combine its tones at a common offset
  before marginalizing that offset.
- **Scores.** Clip evidence at 4–8 times noise mean and quantize to 0…15. Accumulate
  repeats by byte value, then sum 32 symbol entries per word. On the same samples, remove keyed
  phase, sum each candidate's complex projections within windows and add window energies.
  Include final partial-window exposure. Reference scoring standardizes candidates within each hypothesis.
- **Windows.** Longest coherent span is the block or active hop. Add energies across
  hops. Subdivide to HF's configured shortest window (target 1/64 block, limited by hop).
  Below 9 kHz use cells within tones, per-tone energy, then two-tone doubling to the block.

## Block decision and completion

- Unusable input or an unavailable branch yields **unscored**. Give each branch half the
  false-admission budget, a strict cutoff on its searched maximum and deterministic ties.
  Provisional ten-bit target is `10⁻⁴` false admissions per quiet block.
- Return the per-tone winner if its score exceeds its cutoff, otherwise the coherent
  winner if it exceeds its cutoff, otherwise **absence**.
  Recalibrate changed conditioning, searches or decisions, including truncated apertures.
- Close after **two consecutive scored absent blocks**. Unscored blocks break the count.
  Preserve interior gaps as erasures if reception resumes before closure. Keep public
  ordinals across closure. The next accepted word opens a new message. Payload, idle hop
  slots, EOF and resource limits cannot close reception.

## Processing limits

- Shared native workers `W = 2H`, with `H` effective logical capacity after affinity/quota.
  Honor explicit limits. Reuse schedules, projections and gathers within bounded workspace.
- Use the fastest qualified complete-search route, including quiet and unsuccessful reception.
  Preserve all configured hypotheses. If full-fidelity service falls behind, report overload.
  Any reduced-work route then requires explicit identification and recalibrated decisions
  with demonstrated sensitivity and deadline margin.
- Four-bit input, evidence and complex-bin component targets. Size accumulators and squared
  magnitudes for the maximum block. [Implementation and qualification](algorithms-matrixModem/block-reception/README.md#receiver-qualification-requirements)
