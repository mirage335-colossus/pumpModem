# Matrix Modem FSK specification

One-way raw words on a configured, clock-derived public calendar. Both ends fix the profile
before transmission. There is no feedback or transmitted length field.

**Implementation status.** The native ten-bit HF reference uses complex samples, hopping off,
seeded schedules and a supplied block ordinal. Its ordinary receiver searches residual timing
and frequency using one- and five-bin energy sums and coherent windows. Wire interoperability,
unknown-epoch or ordinal acquisition, field sensitivity and sustained service remain unqualified.
[Native reference](algorithms-matrixModem/block-reception/README.md) · [Design study](../_ref/algorithms-matrixModem/fsk-design-study.md) · [Overall modem brief](matrixModem.md)

## Waveform profiles

- Send one constant-amplitude tone at full power during each flat interval. Only its position
  carries data. For integer scale `j ≥ 0`, the cell lasts `T = 0.160 × 2^j` seconds.
  Position `n` has centre
  `f₀ + n/T` for `0 ≤ n < N`, `N ≥ 2`. Hold the position for `C ≥ 1` whole cells.
- Schedule a fresh phase of 0°, 90°, 180° or 270° per tone, keyed independently of position
  and payload. Successive phases may coincide. Below 9 kHz, additional concealment
  must add no receiver processing cost.

| Setting | HF | Below 9 kHz |
| --- | --- | --- |
| Grid | `j = 0`, 160 ms cells, 6.25 Hz spacing. Default 352 positions from 450 Hz in 2.2 kHz. Up to 448 in 2.8 kHz. | `j = 3`, 1.28 s cells, 16 positions in 12.5 Hz. Alternatives `j = 2` or `4` give 16 positions in 25 or 6.25 Hz. |
| Tone length | One cell or a configured held tone | Eight cells (10.24 s at `j = 3`), alternatively one or 32 |
| Hopping | Optional. Reference hop 0.4 s, alternate 40 s for one-cell tones. | Off. Both references GPS-locked, with corrections logged or audio retimed from pulse-per-second. |
| Phase change | Step at the tone boundary | Rotate at constant angular rate over the first 80 ms, or 160 ms for a narrow antenna. Use the shorter rotation, with keyed direction for a half turn. |

- Without hopping, send tones back-to-back without amplitude ramps or guards, retaining
  phase evolution at the band origin.
  With hopping, retune in quiet. Each edge of a 0.4 s HF hop has a 20 ms raised-cosine
  ramp and 20 ms quiet, leaving two flat cells. Longer reference hops use 20 ms ramp
  and 60 ms quiet per edge.
- Size tones and repeats for ordinary recovery of intact, waterfall-visible signals after
  antenna pointing loss, at the configured spread and deadline. Apply the selected margin
  (3, 6, 10, 13, 16 or 20 dB) to the off-axis power budget. Target 26 dB out-of-band
  attenuation relative to mean in-band power using a specified transmit filter, measurement
  bandwidth and averaging interval.

## Block coding and calendar

- Encode a `k = 10, 16 or 24` bit encrypted word `c` as 32 byte symbols
  `vₛ = Gₛ c XOR mₛ` using keyed `8 × k` binary matrices over GF(2) and byte masks.
  Shuffle exactly `L ≥ 1` copies of each symbol index into the block's `32L` tones.
- Draw a keyed map per tone. For `N ≥ 256`, assign bytes to the first 256 positions of a
  permutation. Otherwise use an agreed pool of `P = 2^floor(log₂ N)` positions, exactly
  `256/P` bytes per position and `L ≥ ceil(8/log₂ P)`. Check distinguishability on the
  surviving whole-word position sequences.
- Without hopping, a block lasts `32LCT`. With hopping, `q = floor(flat cells per hop / C)`
  must be at least one. Require `L ≥ q` and reserve `ceil(32L/q)` full hop periods
  (at least 32). Leave residual cells and unused final slots idle.

## Keys and addressing

- With the shared v4 keyfile or published zero key, XOR the payload with both AES-256-CTR
  and Serpent-256-CTR keystreams before coding. Preserve the payload bit count.
- Use distinct AES-256 schedule purposes for symbol order, matrices, masks, placement,
  hops and phase. Address by epoch, ordinals and nominal shift rounded to the configured
  multiple of 10 Hz. Retunes and measured corrections preserve the address. Matrix
  serialization, rounding ties and wire test vectors remain open. [Key and stream design](../_ref/algorithms-matrixModem/Opus-5.5/09-keys-schedules-spectrum-and-stream-contract.md)

## Receiver search

- **Input.** Use fixed gain, a linear passband including guard bins, and radio blanking
  and noise reduction off. Translate and filter 48 kHz real audio before decimating to
  the reference 3,200 complex samples/s (512 per HF cell). Admit bandwidth, offsets, spread
  and filter transitions together. Use wider qualified capture when needed. Filtered
  decimation to 400 complex samples/s is optional at `j = 3`.
- **Conditioning.** Estimate component noise scale from the lower quartile of complex
  sample power under circular Gaussian noise. Round I and Q in units of 0.45 standard
  deviations. Erase the whole sample if either code is nonfinite or outside −7…+7.
  Calibrate any inverse-power weighting or sparse-line excision used.
- **Coverage.** Score every word on each scorable block, including quiet and after misses.
  Search the configured sideband, timing, offset, drift and spread directly from data,
  without waiting for pilot lock or a visible trace.
  Helper Carrier may only narrow the search independently. Reversed-spectrum, spacing-error
  and damaged-tone searches require explicit selection. When disabled, they must preserve
  ordinary sensitivity, false-word performance, workspace and maximum sustained throughput.
- **Projections.** Match rectangular apertures at the required physical frequencies,
  including intermediate bins for held tones. Follow the phase ramp. Use a coarse timing
  step of one eighth of the phase interval, or finer where coherent timing requires
  `1/(8 × band span)`. Correct signed-frequency phase and moved window boundaries, or
  qualify the mismatch. Cover accumulated clock slip and intra-window drift. Space
  coherent frequency hypotheses by `1/(2U)` for a window of `U` seconds.
- **Per-tone branch.** Choose one spread per block. Sum weighted energies for a single bin
  or Gaussian widths of 10, 30, 60, 120, 250 or 500 Hz (full two-standard-deviation
  width). Use only captured bins and calibrate truncated apertures. Normalize by time
  and frequency medians, using surrounding noise bins on small grids. Weight each hop by
  its inverse noise level, erase unusable hops and combine its tones at a common offset
  before marginalizing over that offset.
- **Word scores.** Quantize per-tone evidence to 0…15, clipping at 4–8 times the noise mean.
  Accumulate repeats by byte value, then sum 32 symbol entries per word. On the same samples,
  undo the keyed phase and sum each candidate's complex projections within windows. For each
  window length, sum squared magnitudes including the actual final partial window. The native
  reference standardizes candidate scores within each hypothesis before taking maxima.
- **Coherent windows.** Start with the block, or the active hop between retunes. Add energies
  across hops. Subdivide to the configured shortest window (HF target 1/64 block, limited by
  the hop). Below 9 kHz use cells within held tones, per-tone energy, then windows doubling
  from two tones to the block.

## Block decision and completion

- Require both branches. Unusable input or an unavailable branch yields **unscored**.
  Give each branch half the false-admission budget and its own strict cutoff on the
  maximum over all words and hypotheses. Resolve ties deterministically. The provisional
  attended ten-bit target is one false word per 10,000 physically quiet public blocks.
- If the per-tone score exceeds its cutoff, return its word even on disagreement.
  Otherwise return the coherent word if its score exceeds its cutoff, else **scored absence**.
  Freeze cutoffs before checking fresh quiet, active, interfered and wrong-schedule blocks.
  Recalibrate any changed search or decision rule.
- **Two consecutive scored absent blocks close an open message.** Unscored blocks break the
  count. Retain interior gaps as erasures if a word resumes before closure. After closure,
  the next word opens a new message at the continuing public ordinal. Payload, idle hop
  slots, EOF and resource limits cannot signal completion. The raw 24-bit stream leaves
  final-byte framing and file integrity unspecified.

## Processing limits

- Share `W = 2H` native workers across stages, with `H` the effective logical CPU capacity
  after affinity and quota. Honor explicit limits. Reuse schedules, projections and
  candidate gathers within admitted workspace. For additive word scores, compare direct
  lookup with a qualified Walsh–Hadamard transform. For oscillator searches, compare
  direct scoring on small banks with dechirping and a transform across tone sums on broad
  banks. Time qualified CPU FFT, GPU FFT and matrix-product DFT routes at actual batch sizes.
- Select routes by complete single-stream service through ordered decisions and closure,
  including unsuccessful search, transfers, recurring calibration and peak workspace.
  Measure sustained backlog over a stated interval. Use the same scheduling policy for
  HF and sub-9 kHz and record each run's timings, assignment and limiting stage per device.
  Qualify decisions, sensitivity and arithmetic before timing comparisons, and test an
  independent challenging workload before extrapolating. Preserve every configured
  hypothesis. If full-fidelity service falls behind, report overload. Identify
  any reduced-work route and use it only with recalibrated decisions and demonstrated
  sensitivity and deadline margin over its operating range.
- Target four-bit input, evidence and stored complex-bin components after conditioning.
  Size sums and squared magnitudes for the maximum block. The native reference uses a
  floating-point FFT and coherent sums. Four-bit transform arithmetic remains unqualified.
  [Receiver design review](../_ref/algorithms-matrixModem/fsk-reconciliation.md)
