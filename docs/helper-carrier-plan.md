# Helper Carrier implementation and qualification

Updated 28 October 2026. Human checkpoint `2466865` is committed. The rebuilt
GUI is `build/dev/datapump-gui`; source-bound results and remaining limits are in
[validation](validation.md). The completed contract stage failed; qualification
remains incomplete.

## Current implementation

- Normal Helper power is 90% pure pilot/10% auxiliary. Only the auxiliary has
  shaped 240 ms punctuation and optional compact metadata. High-only placement
  is data, guard, auxiliary, guard, pure pilot: auxiliary starts 450 Hz above
  the shaped data edge, adjusted by leakage bound; pilot spacing is 400 Hz.
  The radio/audio passband and filter margin remain physical requirements.
- Full oscillator/capture-derived discovery precedes local refinement. Earlier
  200 ms contexts are software apertures, not a continuous-tone physical limit.
  Settled 384 ms fits and dated continuous numeric support remain bounded.
  Proper-Q component coverage improves while startup escapes remain; no
  calibrated operational-confidence guarantee follows. Numeric retention is
  separate from the 36-second extra-lead cap.
  Unknown retune phases remain independent; raw staging stays bounded at 400 ms.
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
physical RF output to retain configured power. Headroom/oversampling compensation
scales signal/noise together; Interleaved V2 retains its separate −6.0206 dB
sampled DATA gain relative to nominal RF budget. Ramps/metadata/channel gains
remain separate.

## Measured status and remaining acceptance

Checkpoint `2466865` has a rebuilt GUI and the two-tone/one-bank implementation
summarized above. The [operating guide](helper-carrier.md#manual-reception) gives
a compact measured comparison; [validation](validation.md) retains precision,
work, CPU, support dates, exact bindings and every individual failed result.
Fixed-lag refinement narrows the matched HF cohort from 125 to 109 pairs and
12,250 to 10,682 FFT jobs, with 9.17% lower process CPU. Separate bitwise-preserving
filter/Hilbert changes reduce another prefix's CPU by 41.15%. These source-bound
prefix results do not establish full reception or general throughput. Private
matched R4 whole-score refinement reduces R2's 35,672→26,264 FFT jobs and CPU
by 25.47%, with all nine unresolved LF alternatives retained, but only two of
eight bits/no END. The earlier 109-pair mask lacks whole-score support and is
not a valid R4 baseline. R4 is integrated in source; main build/checks are pending.

The actual-RF loss case demonstrates 30 prior-only frequency corrections,
including five after loss, with exact `001`/ID 2/scored END and no witness
renewal/new phase. FFT work rises 8.16% and CPU does not improve. The strong
engineered phase-gap case exercises independent frequency records but publishes
only a wrong-epoch pending one-bit suffix, without canonical payload/END proof.

The last validated app's masks lack whole-score frequency-support certification;
R4 validation is pending and the DATA-specific lease remains private. Current strict HF40 public completion,
HF15k physical-END authority, full EME, 1,024-byte reception and hardware
qualification remain open. The HF15k alias fully scores absent address 74 while
true address 75 is strong in the same window: this is an END-authority defect,
not insufficient scored duration, with no receiver fix or strict pass.
Estimator escapes remain part of qualification. The full contract run is
**FAILED**, with two timeouts and nine assertions; qualification is incomplete.
Private gate and terminal-join prototypes remain unpromoted. Ordinary
wire/framing, progress, identity and END requirements are unchanged.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
