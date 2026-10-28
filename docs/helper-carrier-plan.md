# Helper Carrier implementation and qualification

Updated 28 October 2026. Human checkpoint `61e66b3` is committed. The rebuilt
GUI is `build/dev/datapump-gui`; source-bound results and remaining limits are in
[validation](validation.md). The completed contract stage failed; qualification
remains incomplete. Current core `85a53d89…`/GUI `d8e3be8d…` passes targeted
actual-RF/continuity/loss, self-checks and full HF40 public completion. The prior
`f37/949` HF40 pass keeps its own binding.

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

Checkpoint `61e66b3` has a rebuilt GUI and the two-tone/one-bank implementation
summarized above. The [operating guide](helper-carrier.md#manual-reception) gives
a compact measured comparison; [validation](validation.md) retains precision,
work, CPU, support dates, exact bindings and every individual failed result.
The current supplied HF40 case passes exact eight-bit/full-wire reception, public
pending identity, scored END and replay: 194.956 source-audio seconds and
869.479448 process CPU/114.150555 wall seconds on six physical cores/2 GiB,
103,663 FFT jobs/82 transforms. This restores scoring but increases cost over
the `f37/949` pass (66,192/39, 562.536933 CPU seconds). Independent audit assigns
94.40% of extra FFT work to 31 restored owners and the rest to wider dynamic
masks. It is not CPU savings, complete latency/backlog or hardware real-time
qualification.
The valid matched R2→R4 whole-score comparison reduces 35,672→26,264 FFT jobs
and 65.756964→49.005478 CPU seconds (25.47%), with all nine unresolved LF
alternatives retained, but only two of eight bits/no END. Separate
bitwise-preserving filter/Hilbert changes reduce another prefix's CPU by 41.15%.
These source-bound prefix results do not establish full reception or general
throughput. The earlier 109-pair mask lacks whole-score support and remains
historical evidence, not a valid R4 baseline. The earlier R4 4/5 focused result
and zero-FFT provenance failure remain preserved; current continuity/loss passes
after minimum provenance was funded through optional-refinement reclamation.

The actual-RF loss case demonstrates 30 prior-only frequency corrections,
including five after loss, with exact `001`/ID 2/scored END and no witness
renewal/new phase. FFT work rises 8.16% and CPU does not improve. The strong
engineered phase-gap case exercises independent frequency records but publishes
only a wrong-epoch pending one-bit suffix, without canonical payload/END proof.

Whole-score provenance is funded and additive constructor headroom restores all
four early owners in the targeted actual-RF case, with unchanged measured U and
all three LF alternatives retained. Its 6,195 witnessed FFT jobs include restored
work; the old 609-FFT result remains a cold-lockout failure, not an RF-saving proof.
Current Rate-360 Fake-FHSS fails with no owners despite qualified reference;
folded-mono provenance admission is the next focused repair. HF15k physical-END
authority, full EME, 1,024-byte reception, 54–450 MHz coverage and hardware
qualification remain open. The HF15k alias fully scores absent address 74 while
true address 75 is strong in the same window: this is an END-authority defect,
not insufficient scored duration, with no receiver fix or strict pass.
Estimator escapes remain part of qualification. The full contract run is
**FAILED**, with two timeouts and nine assertions, and has not been rerun on the
current app; qualification is incomplete.
Private gate and terminal-join prototypes remain unpromoted. Ordinary
wire/framing, progress, identity and END requirements are unchanged.

## Bounded architecture decision

Freeze Q, lag/history, uncertainty multipliers and additional lead. Preserve the
funded minimum provenance and validated additive constructor headroom before more
estimator tuning. Accept measured HF40 reference quality as useful on the stated
six-physical-core/2 GiB simulator, without a GPSDO-specification or CRLB target
criterion. A whole instantaneous hull is sufficient for that conservative design
but not proved necessary for a score; accumulated phase, FIR support and
correlations require proof before substituting a later average. Preserve unresolved LF/UTC/key and
independent hop phase. See the [architecture/error review](helper-carrier.md#receiver-architecture-decision)
for primary references and the three short discriminators, with the compact
[reference/error-work budget](../local-evidence/helper-two-tone/architecture-review-r1/reference-budget.json).
Targeted passes do not establish general safety or full practical acceptance.
The RF-only control has no matched-frontier cost ratio; EME gating/template/cost
remains separate from the unproved pilot-CFO-loss explanation.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
