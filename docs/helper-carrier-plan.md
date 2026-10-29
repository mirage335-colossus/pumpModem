# Helper Carrier implementation and qualification

Updated 28 October 2026. Human checkpoint `572b051` is committed. The rebuilt
GUI is `build/dev/datapump-gui`; source-bound results and remaining limits are in
[validation](validation.md). The completed contract stage failed; qualification
remains incomplete. Current GUI `79d40073…` passes the EME Planner caution
check and headless self-check; core `2c118bf0…` is unchanged, retaining its five
focused passes on the preceding `64824037…` GUI binding. The retained HF15k
capture strictly fails four premature native ENDs among 17 identities despite
its original fixture exit 0/canonical exact eight bits. A new test-only guard
verifies four conflicts and 13 quiet controls by trace reinspection, without a
new receiver/PCM run; neither old nor new posthoc inspector is an acquisition
pass. The private calendar-END proposal is held,
unbuilt/unintegrated after bounded review: weak-chain confirmation cannot prove
a historical standalone positive. Full GUI 39/39 and six selected ordinary-Live passes
retain the preceding `0463/276e` binding, including unskipped exact-source
coverage. Full Rate-360 `21bf/913` and HF40 `85a5/d8e3`/`f37/949` passes retain
their own bindings; those full runs have not been repeated on this binary.
No full-contract/platform/hardware qualification follows.

## Transmitter and receiver scope

- The transmitter uses fixed 550 Hz and 950 Hz low pilot and auxiliary tones,
  with steady-tone powers of −3 dB and −9 dB relative to the high pilot. The
  high pilot is at least 2400 Hz and rises only for occupied bandwidth and
  separation. Helper Carrier Power is the simultaneous active composite average;
  Robust power specifies its allocation. Modulation and gating affect helper
  average power, and the ratio limit remains in force.
- Receiver acquisition, tracking and decoding updates for this waveform are
  deferred. The following receiver observations and source-bound evidence concern
  the earlier two-tone implementation and are not qualification of the new
  transmitter waveform.
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

Physical audio/RF calibration and passband margin remain necessary. Previous
headroom measurements describe their earlier two-tone source binding; they do
not establish calibration for the new waveform.

## Measured status and remaining acceptance

Checkpoint `572b051` has a rebuilt GUI and the earlier two-tone/one-bank
implementation; its findings do not qualify the new transmitter waveform. The [operating guide](helper-carrier.md#manual-reception) gives
a compact measured comparison; [validation](validation.md) retains precision,
work, CPU, support dates, exact bindings and every individual failed result.
The preceding `85a5/d8e3` supplied HF40 case passes exact eight-bit/full-wire reception, public
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
Frozen `21bf/913` Rate-360 Fake-FHSS passes full eight-bit/public pending one-row/END/replay
after bounded raw-`q=0` provenance repair; its earlier zero-owner failure remains
preserved. No estimator, split, lead or threshold changed. Current HF15k restores
collision owner 39 from zero to 56 FFT jobs, with 77/77 contained queries.
Total work is 11,711 FFT jobs/306 transforms/152.656895 CPU seconds; separate
captures supply no matched CPU gain. Four premature native ENDs remain a strict
failure. Address 74 fails while canonical address 75 scores 110.90 in the same
window and DATA continues: this is a calendar-END authority defect, not an
established precision limit. The older `2101/3b7` END-consistent capture left this
collision unexercised. Full EME, 1,024-byte reception, 54–450 MHz coverage and
hardware qualification remain open. No END repair or strict pass is claimed.
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
criterion; a modest residual RF penalty can still be useful. A whole instantaneous
hull is sufficient for that conservative design
but not proved necessary for a score; accumulated phase, FIR support and
correlations require proof before substituting or retrodating a later average. Preserve unresolved LF/UTC/key and
independent hop phase. See the [architecture/error review](helper-carrier.md#receiver-architecture-decision)
for primary references and the three short discriminators, with the compact
[reference/error-work budget](../local-evidence/helper-two-tone/architecture-review-r1/reference-budget.json).
Targeted passes do not establish general safety or full practical acceptance.
Full HF40 retains pilot/audio correlation through legacy M/`D−H` geometry; typed
DATA scalar marginalization is a separate unquantified optimization gap, not
the HF40 cost cause. No affine-parameter implementation or CPU gain is claimed.
The RF-only control has no matched-frontier cost ratio. Its full 250/900-CPU
controls remain incomplete and violate an ID's published prefix;
neither canonical eight bits/END nor the current paired History guard test
qualifies that full control. The guard is independent of Helper and preserves
legitimate timing refinement; no END/scoring/layout change or full control rerun follows.
Helper-OFF's unnotched mixture is also an unquantified fairness difference;
the fully GPSDO-disciplined control changes audio and lead. Neither gives a fair
CPU ratio. EME's roughly 1.52 dB component gating opportunity remains separate
from the unproved pilot-CFO-loss explanation. The private gated Live attempt fails its CPU budget before a
matched first score; no isolated gain follows. A bounded ordinary-cell
gate-deficit component passes exact arithmetic and a single fixed-order cost
comparison. The private deficit integration then exhausts its 190-CPU ON cap
before a canonical score/fence/raw binding; OFF scores 30.1203 below 31.7153.
It remains unpromoted, with no matched gain. The current EME backend cannot
consume the compact auxiliary timing prior; stronger markers alone do not
remove its timing alternatives.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
