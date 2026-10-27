# Helper Carrier implementation and qualification

Updated 27 October 2026. The current GUI is `build/dev/datapump-gui`.
The R7 GUI/CLI rebuild and three focused Session/stage/tracker checks pass.
The later optional metadata-budget CLI edit still needs rebuilding. Full
qualification remains incomplete. [Validation](validation.md) retains actual
commands, bindings, failed results and the detailed measured limits.

## Current implementation

- Normal Helper power is 90% pure pilot/10% auxiliary. Only the auxiliary has
  shaped 240 ms punctuation and optional compact metadata. High-only placement
  is data, guard, auxiliary, guard, pure pilot: auxiliary starts 450 Hz above
  the shaped data edge, adjusted by leakage bound; pilot spacing is 400 Hz.
  The radio/audio passband and filter margin remain physical requirements.
- Full oscillator/capture-derived FFT discovery precedes ten 20 ms projections
  per independent 200 ms context. Bounded numeric fallback allows one phase gap
  for frequency only. At most 192 frequency records span 36 seconds. Pooled
  frequency has no cross-retune phase authority. Measured tracker storage is
  403,016 bytes; raw staging is separately prepaid and capped at 400 ms.
- One ordinary Robust bank narrows existing and new RF candidates while
  retaining unresolved clock/timing alternatives. Every committed payload sample
  and date remains required. Phase needs local support; later frequency cannot
  manufacture earlier phase or advance the processed frontier.
- Planner separates total/pilot C/N0, uninterrupted/segmented reference scales,
  practical prediction, baseline lead and at most 36 additional seconds. The
  ±0.1 Hz milestone does not qualify every 128-second configuration. CPU prices
  remain conditional on existing receiver work and unresolved geometry.

Two-tone headroom changes native payload calibration by −1.353 dB at 40 W/4 W
and −1.989 dB at 15 kW/4 W versus the old format at unchanged volume. Recalibrate
physical RF output to retain configured power. Simulation scales signal/noise
together; ramps, metadata and received gains are separate power effects.

## Measured status and remaining acceptance

A strong RF-only Session control passes 73 independent-phase retunes at
7/7.7/8.4/9.1 MHz, 887 qualified references with no frequency escape, and exact
`001` with one pending identity/physical END. The guard accepts +2.19 Hz and
declines +142.7 Hz while preserving the NCO. Phase-covered feeds execute 3,626
owner-job deltas; this is not whole-symbol phase coverage. DATA SNR 40 dB,
zero drift and an exact audio clock are laboratory assumptions. Independent-LF
Session work is in progress; no hardware retune adapter is qualified.

New optional phase bounds give all-sample 128-second component loss
0.0695/3.6321 dB at 30 dB-Hz continuous/random phase, and 0.00627/0.27925 dB at
40 dB-Hz. The old 30 dB-Hz gate admitted no phase fits; that was a software
limit. There are no new-bound escapes over 18,633,600 dense prediction checks.
These fixed-RF controls exclude the payload notch and ordinary 128-second
completion. Earlier interval escapes and whole-aperture losses remain retained.

HF40 remains incomplete after caching: one non-TX-epoch pending bit/no END,
while the true-epoch owner executes no FFTs. The 96.98% cache hit rate and lower
descriptive hot-feed CPU do not establish a causal speedup across fresh keyed
waveforms. HF15k passes the primary fixture with actual 127→2 owner narrowing,
but signal 4's false/truncated `01` remains a strict qualification failure.
Signal 6 is a permitted compatible-prefix correction. Rate-360 passes its
primary fixture but has an epoch ambiguity and unchanged 9→9 masks on folded
mono. Correct primary data does not establish all-row/cable acceptance.

EME R5 has zero owners/bits despite sustained pilot qualification: 53,012 modeled
CPU-admission rejections, minimum prediction 1.221164 above the 0.6 objective.
The separate actual constructor is also typed pending before PCM: its
10,092,055-byte share is short 508,071/1,270,881 bytes at 83/89 pairs. Exact-lattice
reservation saves only 38,880 bytes, remains insufficient and was rejected
without production edits. EME GPSDO control is incomplete after 2.73067 captured
seconds and no full symbol/END. None of these is a physical limit or measured
one-owner throughput. The three HF GPSDO-model controls pass exact eight bits.

The serial-row cache passes 47,462,282 exact score comparisons and nonzero
acquisition checks (2,107 jobs), using 164,008 bytes. R6 build/selected CTests
pass; full regression and throughput qualification remain pending. Full-composite
N0CALL metadata has a first 7/7 component pass; nominal-offset/random-hop tests
and Live routing remain in progress. Metadata does not qualify frequency aid.
The controller null guard passes its selected full repair tests; the preceding
failed GUI group remains retained. Supplied HF/EME completion, larger payloads,
strict cable, full contract/native/sanitizer and hardware coverage remain open.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
