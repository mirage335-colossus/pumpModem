# Validation status

Updated 9 November 2026. The historical cleanup checkpoint below was then
uncommitted and based on `357a50970a0c6214eeaade7c5704bbc39abd4b79`, now
committed as `419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. It is not release
certification. The [source and binary inventory](../local-evidence/robust-cleanup/source-binaries.json)
records 2,904 surviving implementation, test and build files and selected binary
hashes. This compact manifest substantiates this cleanup checkpoint; retire it
when the checkpoint is superseded. Its source-inventory SHA-256 is
`2405f259c050b68d7bbabad9628eb97ff750e53c6d9f51012f47530719f7c74b`.
This is a post-validation inventory, excluding documentation/evidence, not a
before/after timing comparison. Raw run logs are temporary; remove them after
this checkpoint is accepted unless a specific active validation or release
requirement still needs them. The concise outcomes and limitations below are
the maintained record.

## Complete headless Robust and manual capacity

The latest full manual suite read the eight-entry list, passed its then-current
short complete-message functional gate, and returned
`capacity_failures`. The [report](../local-evidence/robust-cases.md) and
[JSON](../local-evidence/robust-cases.json) record unchanged source, list and
native exporter at completion. Its source-closure SHA-256 is
`89e522e59a8bc4b00f385be3a670b0929096275d82e1d43408293392c450aa60`.
Subsequent Helper-prefix transport and verification changes mean this is
historical evidence for that source, not a qualification of the changed source.
The list SHA-256 is
`b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b` and
the exporter SHA-256 is
`29ac0b83a6c66112825919491b5c612afede6b32b5241d2212287447611237d1`.
The gate is one short high-SNR functional check, not a sensitivity certificate
for all configurations.

The following nominal estimates use prepared elapsed receiver-processing
seconds per received baseband second for one stream. Intervals are conditional
on the measured geometry and extrapolation. A point at or above 1 is a numerical
failure even when its interval crosses 1. The complete-capacity result can
remain unresolved because of unsupported worst-state extrapolation.

| Measured case | Workers | Nominal (s/s) | Conditional interval (s/s) | Nominal decision | Complete-capacity decision |
| --- | ---: | ---: | --- | --- | --- |
| Sub-9kHz | 4 | 0.9334 | 0.8008–1.0826 | Unresolved | Unresolved |
| Groundwave GPSDO | 6 | 0.9542 | 0.7575–1.1907 | Unresolved | Unresolved |
| Groundwave Helper | 6 | 1.5260 | 1.2874–1.8009 | Point failure | Failure |
| HF Helper | 2 | 1.3144 | 1.1694–1.5356 | Point failure | Failure |
| HF encrypted GPSDO | 1 | 0.5972 | 0.5129–0.7142 | Conditional nominal pass | Unresolved |
| HF encrypted GPSDO/FHSS | 4 | 1.0233 | 0.9529–1.1869 | Point failure | Failure |
| HF unencrypted GPSDO | 6 | 0.6144 | 0.5146–0.7452 | Conditional nominal pass | Unresolved |
| EME | 2 | 0.9043 | 0.7292–1.1919 | Unresolved | Unresolved |

All eight conservative cold-state point estimates exceeded 1 s/s, but the
deliberate repeated setup and unsupported extrapolation do not alone prove
ordinary reception fails; five complete-capacity conclusions remain unresolved.
The same-stream allocation candidates included the visible six physical cores
and twelve logical CPUs, and the separate six-second fine probes compared the
selected allocation with those physical/logical budgets. Their uncertainty
does not establish a saturated CPU ceiling or justify scaling elapsed service
by unused cores. The per-case workspace budget was 4 GiB, with estimated
headroom from about 12.6% to 89.6%; sampled RSS and model allowances are not
workspace certificates. Full cold execution took approximately 2,958 seconds;
that elapsed experiment time includes setup and is not the sustained service
metric above. Older v3 figures below used different worker selections and
sampling; their numerical differences are not a controlled regression.

The private per-row `local-evidence/robust-current-fit-*.json` receipts contain
machine identity and must remain local. Each records a conditionally matched
identity, not an independently validated replay prediction. Retain those and
the suite JSON/report while the source-bound replay/holdout and capacity
questions need them; remove them when equivalent evidence for the current list
and changed sources supersedes those findings. The suite itself did not qualify cloud
replay. The separate assessment below covers all eight nominal cases and
keeps the complete-capacity decisions separate.

### Earlier full v3 reevaluation

The earlier measured implementation was clean revision
`0827e8b50d558a8939c0f6daae61e5e94d15b3a2`. It connects canonical native samples
through public Helper/coarse acquisition, fine fixed-affine admission and adaptive
tracking work, bit/null decisions, scored quiet slots and the canonical short-message
dictionary. Robust remains unavailable in the GUI. No received packet length or
transport EOF establishes message completion.

On 8 November 2026, `./build.sh test triad --jobs 2 --build-jobs 3 --
-DDATAPUMP_COMPILER_CACHE=OFF` passed **20/20**. The subsequent full integrated v3
manual run covered all eight then-current entries with unchanged source, binary and
input-list hashes; its then-current native message/quiet/EOF gate passed. The
overall result is **measured with unresolved margins**, not a full capacity pass. The
[earlier report](../local-evidence/robust-manual-capacity-reevaluation.md) and
[JSON](../local-evidence/robust-manual-capacity-reevaluation.json) contain the full
component ledger and 1, 2, 4, 6 and 12-worker comparisons on the local AMD Ryzen 5 PRO
5650U, with six physical cores and twelve logical CPUs.

The dedicated suite rereads `_tests-RobustModem.md`; its eight-row SHA-256 was
`b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b`. This absolute
complete-work CPU/workspace estimate differs from relative degradation regression.
Literal `fox` is 22 dictionary bits. See the [manual
contract](robust-manual-capacity.md) for fine tracking, fallback, recurrent work and
statistical limits.

The following values are prepared elapsed processing seconds per received baseband
second for one stream. Intervals are conditional on the measured geometry and
extrapolation assumptions. A point of 1 or higher fails the numerical real-time
criterion even if the wider capacity conclusion remains unresolved.

An independent review found that the v3 fine-continuation stopping decision was
applied to the raw continuation rate before slot normalization and the other
service components were included. Rows 3 and 4 stopped after three looks on a
raw point at or above one; rows 6 and 8 stopped after five looks on a raw
interval below one, although their final nominal intervals cross one. The
table preserves the historical numerical observations, not a correctly
stopped final-service analysis. The reported points for rows 3 and 4 still
exceed one and therefore fail the numerical real-time criterion; all eight
complete-capacity conclusions remain unresolved. Those intervals cannot be
retroactively treated as results of a correctly stopped procedure; the newer
full-list run above used an updated stopping rule.

| Earlier case | Workers | Nominal service (s/s) | Conditional interval (s/s) | Point margin | Nominal decision |
| --- | ---: | ---: | --- | ---: | --- |
| Sub-9kHz | 4 | 0.905 | 0.846–0.971 | 9.5% | Conditional nominal pass |
| Groundwave GPSDO | 4 | 0.912 | 0.662–1.219 | 8.8% | Unresolved interval |
| Groundwave Helper | 2 | 1.434 | 0.019–6.922 | -43.4% | Point failure; extrapolation unresolved |
| HF Helper | 2 | 1.301 | 0.333–3.102 | -30.1% | Point failure; extrapolation unresolved |
| HF encrypted GPSDO | 1 | 0.572 | 0.511–0.666 | 42.8% | Conditional nominal pass |
| HF encrypted GPSDO/FHSS | 2 | 0.935 | 0.759–1.229 | 6.5% | Unresolved interval |
| HF unencrypted GPSDO | 6 | 0.582 | 0.521–0.673 | 41.8% | Conditional nominal pass |
| EME | 2 | 0.841 | 0.510–1.310 | 15.9% | Unresolved interval |

The conservative cold-state capacity envelope remains unresolved in every row. Its
deliberately repeated 256-state startup cannot by itself prove ordinary reception is
slower than real time. These results concern the measured receiver's local service;
worker count does not establish a saturated whole-machine CPU ceiling and unused cores
are not converted into hypothetical speedup.

The preceding [continued-stream
measurements](../local-evidence/robust-manual-sustained-current.json) gave respective
nominal points 0.895, 0.904, 1.467, 1.277, 0.602, 1.050, 0.592 and 0.948 s/s. They
used a separate v2 base and continuation phase, family size 24, and sometimes
different selected workers; the integrated v3 run uses family size 64 and reselects
allocations. These differences are descriptive, not a controlled regression or
evidence of a definite improvement. The earlier [base
data](../local-evidence/robust-manual-capacity-current.json),
[summary](../local-evidence/robust-manual-results-current.md), and [allocation
comparison](../local-evidence/robust-groundwave-sustained-allocation-current.json)
retain their original provenance and remain useful for this comparison. Retire them
when equivalent current evidence supersedes that purpose and any linked unresolved
sensitivity/resource findings are resolved.

The earlier workspace budget was 4,768.7 MiB, with approximately 25–91% estimated
headroom across cases. Initial receiver readiness is distinct from sustained service,
and simulator generation is excluded. The earlier report includes total triple-DATA
and Helper C/N0, provisional model targets and native one-bit and literal-`fox`
transmit calendars. C/N0 values are nominal received link-budget references, not
measured powers of each sampled fade. Timing measurements do not establish a
complete-receiver >90% sensitivity threshold.

Development checks exposed and corrected three reception defects before the
current measurement: FP32 innovation arithmetic failed at very high SNR,
legacy 0.01 Hz final frequency snapping discarded long-duration coarse
resolution, and fixed-affine residual clock hypotheses introduced an unintended
IF-dependent frequency shift. Fine processing now declares FP64, uses the
public duration-dependent frequency grid with its full prior penalty, and
preserves acquired total CFO in the fixed-affine basis. The adaptive model
remains diagnostic and its full work is measured. Focused numerical, wide-time
phase, prior-decomposition and lifetime tests passed. Memory reviews also removed
retained session/cache cycles and included simultaneous profile-validation banks
and transient projection/control buffers.

The frozen-source native message gate passed: DATA 0, DATA 0, DATA 1 decoded
canonical `e`, followed by two fully scored quiet slots and physical completion
before transport EOF. Its captured-prefix EOF negative control stayed
incomplete. This one high-SNR case establishes a functional path, not all-case
sensitivity. The default invocation repeats this gate on current sources. The
earlier source-changing functional exercise is superseded by this frozen result.

The [sensitivity pilot](../local-evidence/robust-manual-sensitivity-current.json)
received both independent bits correctly at 8.113563 dB-Hz in current case 7,
each followed by two quiet slots and a complete work receipt. A fresh,
predeclared [confirmation](../local-evidence/robust-manual-confirmation-current.json)
at 11.113563 dB-Hz received its first **5/29 pairs** correctly, then stopped at
its memory-accounting gate. These were ten successful complete single-bit
endpoints, not five full `fox` messages. The incomplete confirmation establishes
no >90% probability bound. Do not pool different SNR points or count timing
repeats as additional successes.

That confirmation's resource monitor incorrectly treated the largest reaped
child's lifetime RSS as native post-exec residency. Its five child counters
exactly matched the Python workers' peaks. A separate zero-sample discriminator
reproduced this: a 125.7 MB launcher/child high-water counter accompanied only
20.5 MB combined sampled RSS in two verified native executables after exec.
The future confirmation helper now samples owned post-exec native processes,
retains conservative workspace/runtime/decoder allowances and counts persistent
worker processes across waves. Polling does not certify every transient peak.
Four focused regression fixtures pass. The original resource stop and all five
successful pairs remain unchanged. The original cost allowance did not admit
completion after diagnosis; the experiment was not restarted or extended.
After registering those fixtures, the affected `triad_cases` check passed again.

The v3 default manual invocation uses the continued-session measurement. The earlier promotion evidence below documents the prior phase, not the later full v3 run above.
The [promotion receipt](../local-evidence/robust-manual-promotion-current.json)
verifies that the promoted probe is identical and that DSP sources and the
native binary did not change. It preserves the original measurement phases:
the separate continuation phase used family size 24; the v3 default uses 64
for the current full list. The v3 wiring and accounting were tested separately,
including acquisition gating, missing-stage subtotals, slot normalization,
workspace/RSS/utilization and propagation of supported nominal failures. The
old observations are not relabeled as a full run of v3; the separate v3 run above has its own source-bound report.

Retain the current reevaluation report and JSON while they support these capacity findings. Keep the prior base, continuation, allocation, pilot, incomplete confirmation and promotion JSON for the stated comparison and unresolved sensitivity/resource findings; retire each when equivalent current evidence supersedes its purpose. The duplicate temporary continuation/allocation runners
and earlier exploratory message/allocation outputs have been removed.

### Retained historical coarse evidence

Earlier tests used separate coarse and abbreviated tracking stages. They do
not measure complete receiver work or qualify the current endpoint. The old
nine-entry baseline is retained in [its JSON](../local-evidence/robust-cases-current.json)
only to preserve then-current configuration/provenance and unresolved sensitivity
outcomes: nominal Sub9 had insufficient successes, and the HF/FHSS fixed-point
paired check failed on pair six. Those observations must not be relabeled as
current eight-entry coverage or complete-message outcomes.

The earlier [Groundwave JSON](../local-evidence/robust-groundwave-current.json)
records resolution of the Helper handoff/search failure. A five-point bookkeeping
bank had rejected a useful continuous interval through neighbor padding; the
corrected caller uses continuous union width, preserving weak/broad-pilot
fallbacks. Exact lag-domain ZoomFFT completed the public CFO bank. At nominal
20.0206 dB-Hz DATA / 30.0206 dB-Hz Helper, both bits produced correct coarse
proposals. Its coarse-only service was 0.006148 s/s on this Ryzen 5 PRO 5650U;
that value excludes complete fine tracking and message work.

At -8.72375 dB-Hz, all 22 independent noise pairs recovered both coarse bits
using the complete 3,619-point prior after Helper fallback. The 90% one-sided
paired-event lower bound was 0.900628, conditional on one sampled channel and
the old coarse endpoint. This is neither a 95% bound nor literal-`fox` success.
The old `--paired-followup` campaign entry was removed with the superseded staged
suite. Keep these compact historical JSON records while those sensitivity
questions remain unresolved; retire them when current complete-receiver evidence
replaces the relevant claims. Their obsolete implementations are not retained.

## GPU fine-work investigation

A bounded A100 diagnostic compared CPU and GPU fine work for manual row 7
(HF without encryption, GPSDO), using the same native samples and public
acquisition. The [first report](../local-evidence/robust-gpu-case7-lazy-failure.json)
is unresolved: concurrent initial CUDA linear algebra failed with `lazy wrapper
should be called at most once` before GPU parity could be measured. A separate
fresh-process control reproduced the failure and passed after a serial CUDA
linear-algebra call. This is consistent with the documented PyTorch lazy
initialization race and its [serial-first-call workaround](https://github.com/pytorch/pytorch/issues/90613).

The [second report](../local-evidence/robust-gpu-case7-external-prewarm.json)
used the same frozen probe source and an external wrapper that called complex
FP64 CUDA `eigh`, `cholesky_ex` and `cholesky_solve` serially and synchronized
in the same process before starting the two-worker probe. It observed matching
work counters and lane scores for the nominal continuation, maximum-state
transition and interior-erasure strata; nominal retained-state identities also
matched at the four checkpoints. The manually edited list had eight entries
and SHA-256 `b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b`;
native exporter SHA-256 was
`29ac0b83a6c66112825919491b5c612afede6b32b5241d2212287447611237d1`.
Both reports record the per-source hashes, same input and validity hashes, and
unchanged completion checks. This result is one short matched diagnostic on a
remote host; it does not measure local CPU throughput, establish speed or
qualify a complete receiver, other rows, probability or workspace capacity.

The canonical CUDA receiver now has a serial preflight and reports its startup
separately. A [third report](../local-evidence/robust-gpu-case7-canonical-preflight.json)
tests that changed source on the same row and hardware, without the external
wrapper. Its two-worker short probe also matched all three strata and retained
nominal checkpoint identities, with unchanged source, list and native binary.
The serial preflight took approximately 0.190 seconds; subsequent timed probe
peak CUDA allocated memory was approximately 1.56 GiB. The preflight's
separate allocator and device-free snapshots are in the report. Device-free
snapshots include other device users, and peak allocated memory is not total
device consumption or a full receiver workspace bound. This run has the same
limited diagnostic scope as the externally prewarmed result.

A [separate two-row report](../local-evidence/robust-gpu-cases3-6-canonical-preflight.json)
repeated the same short comparison on current manual rows 3 (Groundwave
Helper) and 6 (HF encrypted GPSDO with FHSS), with two workers and unchanged
sources, list and exporter. Work counters and lane scores matched across the
three strata for both rows, as did nominal checkpoint state identities.
Its serial preflight took approximately 0.203 seconds and the timed probe's
peak CUDA allocation was approximately 1.56 GiB, with the same limitations
on interpreting memory. These three representative rows do not constitute
full-list reception or local-CPU calibration.

An [instrumented diagnostic](../local-evidence/robust-gpu-case7-profile.json)
of the same row 7 nominal fine work matched the uninstrumented work and scores,
with unchanged source and input hashes. It recorded 8.33 seconds elapsed and
5.25 seconds of inclusive host basis-render calls summed across threads; the
CUDA operator times in the profiler were comparatively small. This suggests
basis generation as a candidate bottleneck on that remote workload. The
inclusive thread times can overlap and are not a partition of elapsed time;
profiling perturbs execution. One instrumented observation neither establishes
a reliable speedup nor predicts this computer's CPU service.

The local calibration identity guard has also been exercised without an actual
receiver workload; on this machine strict mapping identity was unavailable
because kernel mapping and file device identifiers differed. Its explicit
conditional mode matched around ready-only native processes, subject to the
documented mapping and no-replacement assumptions. Neither result supplies
timing calibration. The old full-list CPU report did not record the runtime
identity needed for calibration reuse.

A later local case 7 non-fine calibration attempt returned unresolved: its
identity declared six workers while the measured allocation selected two, so
the allocation-coverage gate rejected it. Its
[receipt](../local-evidence/robust-nonfine-case7-calibration.json) contains
machine-specific identity data and must remain local; it cannot be reused after
source or other bound identity changes. Retain it only while diagnosing that
failed allocation gate, then remove it when superseded by an applicable
current-source result.

For a separate short-replay transport investigation, the remote file writer
failed closed because its target filesystem did not support the required
`O_TMPFILE` operation. A bounded packet and its acquisition receipt were then
successfully streamed through authenticated, host-key-pinned SSH. That transport
result does not itself establish a valid local receiver replay or throughput.

The subsequent [local replay](../local-evidence/robust-gpu-case7-local-replay.json)
used the frozen case 7 capture and public acquisition, with two remote and two
local workers. Its identity observations conditionally matched around nine
six-second local fine observations over 50.45 seconds of elapsed work. The
detached nominal fine forecast was 0.5425 processing seconds per received
baseband second, with conditional interval 0.4287–0.6763. The absent full
input and search bank were reserved by estimated bytes rather than resident as
actual working data. Comparison with the earlier resident fine point of
0.54952 remains under investigation because the worker-allocation relationship
has not been established. This is a source-bound diagnostic, not a complete
receiver capacity estimate: other stages and independent holdout validation
remain required. The report contains machine identity and must remain local;
retain it only while this replay and calibration question is unresolved, then
remove it when an applicable current-source result supersedes it.

The integrated replay estimator candidate parses every current manual row and
requires matching local calibration and an independent resident holdout for a
conditional nominal estimate. A fresh [case 7 fit](../local-evidence/robust-case7-fit-current.json)
used two selected workers and conditionally matched paired local identities.
Its total cold invocation took 187.606 seconds, including preparation; measured
work inside the identity pair took approximately 162.796 seconds. The nominal
service point was 0.57934 s/s with conditional interval 0.52330–0.66102; the
complete worst-state capacity remained unresolved. The independent
[holdout attempt](../local-evidence/robust-case7-holdout-current.json) stopped
before the calibrated work because its allocation pilot selected four workers,
while the receipt required two. Consequently there is no validated estimate
from this fit and no demonstrated warm full-list speed. Other rows, current-source
identity, resident memory effects and complete work still require their own
applicable calibration and holdout evidence. These two receipts contain
machine-specific identity and must remain local; preserve them while diagnosing
the allocation mismatch and unresolved estimator, then remove them when
equivalent current-source evidence supersedes those questions.

A later frozen-source [current-list assessment](../local-evidence/robust-manual-replay-fixed.json)
resolved conditional nominal service for case 7 only. Its local
[fit](../local-evidence/robust-case7-fit-fixed.json),
[replay](../local-evidence/robust-case7-replay-fixed.json) and independent
[resident holdout](../local-evidence/robust-case7-holdout-fixed.json) used a
predeclared fixed six-worker inference allocation and conditionally matched
local identities. The replay-based nominal point was 0.62536 s/s with interval
0.46172–0.85613; the resident holdout was 0.60092 s/s with interval
0.52388–0.70261. Their 4.07% relative point difference was within the declared
15% limit. The holdout's exploratory allocation pilot preferred four workers;
the actual held-out inference retained the declared six, so this comparison
does not establish the fastest allocation or a whole-machine CPU ceiling.
The conservative worst-state result remains unresolved, as do all seven other
manual rows. This same-IQ result does not generalize to independent bits,
channels or paths.

The resident fit and holdout also reported conservative worst-state points of
2.46241 and 2.44170 s/s respectively. Both are numerical point failures at
the real-time threshold; their extrapolated worst-state capacity conclusions
remain unresolved. The cold-state envelope deliberately repeats setup and
does not by itself prove ordinary reception fails. Observed allocated CPU
utilization was approximately 17–27%; it is not a demonstrated CPU ceiling
or a basis for assuming scaling to idle cores. Against the locally configured
4 GiB workspace budget, the fit and holdout reported approximately 88.6% and
88.0% estimated headroom respectively; these accounting estimates are not
workspace certificates.

A fresh local [Groundwave Helper case 3 fit](../local-evidence/robust-case3-fit-preroll.json)
selected two workers and conditionally matched its local identity. Its nominal
point was 1.46680 s/s, with interval 1.16548–1.83148: a numerical real-time
failure. Its conservative worst-state point was 4.46998 s/s, also a numerical
point failure, but that extrapolated component model was unsupported; the
point does not establish ordinary reception speed. Against the 4 GiB workspace
budget, estimated peak was 763,751,195 bytes and sampled observed peak was
361,394,176 bytes; the resulting estimated headroom is not a workspace
certificate.

Its separate resident-bank matched fine allocation probe observed points of
1.302, 1.330 and 1.449 s/s with 2, 6 and 12 workers respectively, with wide
intervals of 0.236–2.367, 0.907–1.752 and 0–2.985 s/s. These are actual short
fine-session elapsed work per represented six seconds, not full-slot service
or complete receiver capacity. Their uncertainty does not establish a fastest
allocation or the CPU ceiling.

A corrected remote capture packet was transported for this case, but its
Helper-result digest differed from the local fit even though the compared
signal and work matched. It does not qualify a cloud-to-local case 3 estimate.
The local fit records exact source, list, binary, configuration and machine
provenance; it contains machine identity and must remain local. Retain it while
the Helper digest discrepancy and case 3 calibration remain unresolved, then
remove it when applicable current-source, current-list evidence supersedes
those questions.

A later [current-list replay assessment](../local-evidence/robust-manual-replay-preroll-case3.json)
used the case 3 [fit](../local-evidence/robust-case3-fit-preroll.json),
[replay](../local-evidence/robust-case3-replay-preroll.json) and independent
nominal-only [resident holdout](../local-evidence/robust-case3-holdout-preroll.json),
all at two workers. Its conditional nominal estimate was 1.48346 s/s with
interval 1.14365–1.87661, a numerical point failure. The resident point was
1.47415 s/s, a 0.6314% relative difference. The earlier cross-host Helper
digest discrepancy did not reproduce in this run; strict digest matching
remained required. This comparison uses the same IQ realization and does not
establish generalization across paths or complete worst-state capacity. The
other seven list rows lack applicable receipts in this assessment.

The local fit, local replay of the cloud capture and nominal-only holdout
invocations took 381.346, 76.371 and 188.312 seconds respectively. The warm assessment took 2.668
seconds with only case 3 populated, not an eight-row throughput measurement.
The local receipts contain machine identity and must remain local; retain the
assessment and receipts while their same-IQ calibration and outstanding
capacity questions need them, then remove them when applicable current-source
and current-list evidence supersedes those findings.

A fresh local [HF encrypted GPSDO/FHSS case 6 fit](../local-evidence/robust-case6-fit-preroll.json)
used a 3,584 MiB workspace limit after an initial 4 GiB preflight failed; the
successful fit invocation took 589.696 seconds. It selected two workers and
conditionally matched its local identity. Nominal service was 0.99406 s/s,
with interval 0.78889–1.31549, so the real-time conclusion is unresolved.
Recurring bank preparation contributed 0.28980 s/s and nominal fine work
0.66721 s/s. The conservative worst-state point of 4.53072 s/s is a numerical
point failure; its extrapolated capacity remains unsupported and unresolved.

The resident matched fine probe observed 0.615, 0.647 and 0.722 s/s with 2,
6 and 12 workers respectively. Their wide overlapping intervals prevent a
fastest-allocation or CPU-ceiling conclusion. Against the 3,758,096,384-byte
workspace limit, estimated peak was 1,277,767,995 bytes and sampled observed
peak was 902,873,088 bytes. These are accounting estimates, not a workspace
certificate. The receipt binds exact source, list, binary, configuration and
local identity; it must remain local. Retain it while the case 6 capacity and
workspace questions remain unresolved and remove it once applicable
current-source and current-list evidence supersedes them.

A subsequent [combined current-list assessment](../local-evidence/robust-manual-replay-preroll.json)
has matching replay and local holdout evidence for cases 3 and 6 only. Case 3
retains the 1.48346 s/s nominal point and 1.47415 s/s resident holdout noted
above. Case 6's replay-based nominal point was 1.00446 s/s with interval
0.79355–1.46415; its independent resident nominal-only holdout was 0.99669 s/s,
a 0.779% point difference. The case 6 replay point numerically fails the
real-time threshold even though its interval crosses one. Neither same-IQ
comparison qualifies complete worst-state capacity, independent channel or
message performance, or the whole-machine CPU ceiling. The other six rows
remain unresolved for lack of applicable evidence.

The case 6 [replay](../local-evidence/robust-case6-replay-preroll.json) and
[holdout](../local-evidence/robust-case6-holdout-preroll.json) are bound to the
case 6 fit above. Its holdout invocation took 404.085 seconds. Against the
3,758,096,384-byte workspace budget, its conservative peak estimate was
1,277,767,995 bytes and sampled observed peak was 992,649,216 bytes; these are
not a workspace certificate. The warm combined assessment took 3.547 seconds
with only two populated rows, not a full-list throughput measurement. These
local receipts contain machine identity and must remain local; retain them and
the assessment while current-source, current-list calibration and capacity
questions remain unresolved, then remove them when equivalent applicable
evidence supersedes those findings.

In a later bounded cloud batch, one attempt per current row produced exact
replay-work matches for rows 2, 5, 6, 7 and 8. Rows 3 and 4 differed in the
Helper-result hash; the other compared work matched. Row 1 reached its
300-second bound without producing a packet, and the remaining orphan process
was cleaned up. This batch alone did not qualify any local replay or holdout.
A separate same-host case 3 structured diagnostic also reproduced a Helper
hash mismatch: it found no categorical or count changes but 563 differing
floating-point leaves, with maximum absolute ratio difference about
1.30×10⁻⁹. No numerical tolerance was accepted or validated, so strict matching
remains in force and these results do not establish cross-host equivalence.
Temporary cloud pod resources were terminated after the investigation; the
compact source transfer was approximately 1.58 MB.

Subsequent bounded cloud runs produced exact Sub-9 kHz and HF Helper work
matches against their existing local fits. For HF Helper, the original
authenticated receipt exceeded the replay reader's input bound because it
included a diagnostic copy of the Helper result. A verified projection
removed only that optional diagnostic. The original receipt remains available
for comparison, and the canonical Helper hash and all other acquisition
evidence were preserved and checked against the local fit.

The later Groundwave Helper run matched the original local Helper fingerprint
and the rest of the full signal and acquisition receipt exactly. Its authenticated
diagnostic transport carried an additional Helper input prefix. An independent
reviewer split out the canonical fine packet losslessly and verified its bounds,
digests and original receipt against the frozen receiver source.
The follow-up Sub-9 kHz, HF Helper and Groundwave Helper cloud pods were verified
stopped and terminated after their packet transfers.

The subsequently integrated Helper-prefix path received one targeted Groundwave
case 3 fit, remote envelope, local replay and independent nominal holdout on
source-closure SHA-256
`19110782609130fc3828cf0f00798e54cd2ec19dac9c85e11615466e8ea72ec8`.
Before that run, the named `triad` group passed **21/21**. The
[fit](../local-evidence/robust-prefix-fixed-fit-3.json) used a 3.5 GiB workspace
cap and two workers; its nominal point was 1.595902 s/s with interval
1.383274–1.852689 s/s. The [partial assessment](../local-evidence/robust-prefix-fixed-estimate.json)
reports 1.420566 s/s, interval 1.131648–1.780824 s/s, and its independent
[holdout](../local-evidence/robust-prefix-fixed-holdout-3.json) measured
1.429818 s/s, a 0.647% point difference. Its exit status 2 reflects the seven
unselected rows, not a full-list pass. The point and entire conditional
interval exceed 1 s/s; this is a nominal real-time failure, not a saturated
CPU ceiling or complete-capacity qualification.

The [replay](../local-evidence/robust-prefix-fixed-replay-3.json) verified the
bounded [envelope](../local-evidence/robust-prefix-fixed-transport/packet.bin)
and its separately authenticated
[attestation](../local-evidence/robust-prefix-fixed-transport/attestation.json)
against the locally anchored input and re-ran canonical Helper. Verification
took approximately 0.375 seconds outside the fine-service timer; its separate
workspace allowance was 627,266,290 bytes plus 161,850,139 bytes of existing
reservations. The remote and local Helper digests matched in this live run, so
the differing-digest admission branch has controlled-test coverage only. A
production RunPod writer preflight also checked tiny and 8,498,782-byte files,
mode and no-clobber behavior. All subsequent test pods were verified stopped
and terminated. These observations do not establish GPU acceleration, transfer
to other hosts or signals, or full-message success.

The earlier prefix-path targeted source beginning `81ca` and the full eight-row
source beginning `89e5` are historical; neither is relabeled as the source
above. Keep the private targeted receipts and authenticated transport only
while they support this validation and unresolved Helper-portability question;
remove them when equivalent source-matched evidence supersedes those findings.

The [case 3 mismatch attestation](../local-evidence/robust-case3-helper-mismatch-attestation.json)
and [case 3 structured diagnostic](../local-evidence/robust-case3-helper-diagnostic-attestation.json)
record the earlier host-dependent Groundwave Helper difference. The later exact
match does not establish transfer to arbitrary hosts.
The matching packets and attestations under `local-evidence/robust-current-transport/`
support the corresponding local replays and holdouts. Keep the original HF
Helper attestation and its projection together. Remove each artifact when
equivalent current-source evidence resolves its mismatch or supersedes its
replay validation.

The [source-matched eight-row assessment](../local-evidence/robust-current-estimate-complete.json)
has same-IQ local replay and independent resident nominal holdout evidence for
all eight rows on the source recorded above. These are fixed-worker conditional
nominal comparisons, not validation of the later Helper-prefix implementation.
The estimator reports complete receiver capacity as unestablished. Values
are prepared processing seconds per received baseband second.

| Case | Workers | Nominal estimate (s/s) | Conditional interval (s/s) | Resident holdout (s/s) | Relative point difference |
| --- | ---: | ---: | --- | ---: | ---: |
| Sub-9 kHz | 4 | 1.04576 | 0.66390–1.59539 | 0.91804 | 13.913% |
| Groundwave GPSDO | 6 | 0.94371 | 0.74766–1.17833 | 0.92156 | 2.403% |
| Groundwave Helper | 6 | 1.50294 | 1.31038–1.72569 | 1.50419 | 0.083% |
| HF Helper | 2 | 1.33449 | 0.98196–1.82600 | 1.28797 | 3.612% |
| HF encrypted GPSDO | 1 | 0.58267 | 0.47526–0.72633 | 0.58148 | 0.204% |
| HF encrypted GPSDO/FHSS | 4 | 1.02090 | 0.87861–1.27697 | 1.02121 | 0.031% |
| HF unencrypted GPSDO | 6 | 0.60273 | 0.48124–0.76160 | 0.59992 | 0.468% |
| EME | 2 | 0.89612 | 0.73789–1.23287 | 0.89448 | 0.184% |

The Groundwave Helper estimate and its entire interval exceed the real-time
ceiling. Sub-9 kHz, HF Helper and FHSS have nominal point failures even though their
intervals cross 1. The Sub-9 kHz estimate differs from its resident holdout by
13.913%, near the predeclared 15% limit, and the two points lie on opposite
sides of 1. Groundwave GPSDO and EME also have intervals crossing 1.
These timings cover the exact measured realizations and allocations. Path
generalization, whole-message success, worst-state capacity and the whole-machine
CPU ceiling remain unqualified.
The cloud producer used CPU acquisition on GPU-equipped instances. The
reported receiver-service values were measured on the local CPU.

The first Sub-9 kHz holdout was excluded because a separate numerical-library
import may have overlapped its timed work. One predeclared replacement kept
the same fit, replay, workspace and four-worker inference allocation. The only
reported incidental process during the replacement was a brief standard-library
read that finished over two minutes before its worker pilot. The earlier
[holdout](../local-evidence/robust-current-holdout-1.json) and
[automatic aggregate](../local-evidence/robust-current-estimate-sub9.json)
serve only as timing-conflict diagnostics. The final assessment uses the
replacement receipt. Remove the timing-conflict artifacts after the exclusion
has been reviewed.

The cached assessment took 8.401 seconds with all eight rows. The eight
accepted local replay and holdout pairs took 2,013.525 seconds, excluding
the full fit suite's 2,957.980 seconds, remote work and the discarded Sub-9 kHz
holdout. A rapid fresh eight-row workflow remains unverified. The per-row
`local-evidence/robust-current-{fit,replay,holdout}-*.json` receipts contain
machine identity and must remain local. Retain them and the assessment while
they support the recorded-list nominal validation and unresolved capacity
questions. Remove them when equivalent source-matched evidence supersedes
those findings.

An earlier single-row case 7 assessment of its already-created receipts took 2.671 seconds.
the case 7 fit, replay and holdout invocations took 189.519, 51.943 and 194.163
seconds respectively, excluding remote capture production. The four serialized
local commands total 438.296 seconds (about 7 minutes 18 seconds). The wall
span from fit command to final report was approximately 710.1 seconds (about
11 minutes 50 seconds), including remote wait, transfer and dispatch. The warm
assessment includes command startup, then-current source/binary/list checks and a
ready-only full runtime identity for case 7. This earlier evidence covered one
row. The remote producer used CPU acquisition. The earlier A100 parity and
profile observations apply only to their separately recorded source.
These receipts are bound to their exact source, current manual list and runtime.
The local fit, replay and holdout include machine identity and must stay local;
retain them and the assessment while the same-IQ estimator and unresolved
capacity questions need them, and remove them when applicable current-source
and current-list evidence supersedes those findings.

A separate-process holdout on the same IQ realization can test timing transfer
for that realization; it does not establish generalization across channel
realizations, transmitted bits or path states. The historical full-list v3
run had about 258 seconds summed across the timed prefix, continuation and
finish phases, including its early stops. Projecting each row's observed
average to nine looks gives about 371 seconds, assuming unchanged per-look cost.
These are historical summed-phase measurements and an approximate projection,
respectively; they exclude sampling bookkeeping, setup, acquisition, transfer
and other receiver work. The eight-row cached assessment above reuses measured
fits, replays and holdouts. A few-minute fresh full-list calibration remains
unverified.

A bounded CPU stage diagnostic on that earlier source replayed the case 7 signal at
six and twelve workers. It used three six-second observations each for the
baseline and instrumented runs at both allocations. The work counters and
scores matched exactly between each instrumented run and its allocation's
baseline. Mapped basis work on the controller accounted for about 78% of
timed fine wall service at six workers and 71% at twelve. During the
instrumented fine phases, utilization was about 19% of the six-worker
allocation and 11% of the twelve-worker allocation. This identifies controller
basis work as a plausible bottleneck in the measured case. The diagnosis
covers one detached fine workload with timing hooks and leaves other cases,
resident work and saturated CPU throughput unresolved. Keep the private
[stage summary](../local-evidence/robust-current-cpu-profile/summary.json)
and its paired baseline and instrumented receipts only while needed to compare
an applicable basis optimization.

Retain these GPU reports while the lazy-initialization failure and the
unresolved GPU/local-CPU calibration investigation need their original
provenance; remove them after equivalent current-source evidence supersedes
their findings and the remaining calibration questions are resolved. Cloud
resource identifiers and local machine identities do not belong in these
reports. The GPU parity and profile observations are bound to their recorded
sources and do not qualify later estimator changes.

## Test timing investigation

On 8 November 2026, the dirty working tree based on `64f7feb6` was timed with
GCC 14.2, the native Release FLTK configuration and two test jobs. `test all`
stopped before testing: `test_terminal_ui` and `test_framebuffer` failed to link
native audio symbols. Separate web and build-tool runs also had preview socket
permission failures and temporary GPG-key setup failures. Retain the three logs
in `local-evidence/test-timing-2026-11-08/` to diagnose these failed checks;
remove them when the link and environment failures are resolved or superseded
by equivalent current evidence.

## Transmit key policy checks

The uncommitted policy change based on `64f7feb6f298ba844f45ecbf1c57f02219158d16`
passed `./build.sh test contract` (26/26), `./build.sh test triad` (19/19), and
`./build.sh test gui` (27/27) on 7 November 2026. These runs used the existing
Release `build/dev` profile, two test jobs and four build jobs, with
`CCACHE_DISABLE=1` because the sandbox could not write the compiler cache.
The initial compile also exposed a missing test include, corrected before
these passing runs. Documentation link checks and `git diff --check` passed.

Coverage includes ordinary private/Amateur key parity, temporary noise DATA
keys and forced encryption, public-Amateur Helper decoding, changing callsign
ciphertext across absolute dwells, unchanged Helper vectors, independent
background noise, and the unavailable Robust boundary. Noise-mode coverage is
at the native policy/generator API; no GUI reconnection, operational noise
endpoint, physical transmission, or release certification is claimed.

## Cleanup validation

The CLI, FLTK GUI and pipe-worker applications rebuilt successfully. The FLTK
adapter and document test executables also compile and link; their native-window
runtime tests were not rerun. GUI controls remain, with Robust operation
explicitly unavailable pending connection to the canonical triad backend.

| Final affected check | Result |
| --- | --- |
| `./build.sh test contract` | 25/25 passed, 25.13 seconds |
| `./build.sh test triad` | 18/18 passed, 13.09 seconds |
| `./build.sh sanitize contract` | 25/25 passed, 37.49 seconds; ASan, UBSan and leak detection enabled |
| Final crypto/keyring fixture refresh under sanitizers | 2/2 passed, 8.69 seconds |
| Final focused crypto, keyring, Fast codec/CLI, GUI Fast, web worker/bridge | 7/7 passed, 40.40 seconds |

The triad Python aggregate ran 201 checks, with one native-dependent case
skipped there and covered by its separately registered native test. These are
small correctness checks, not full-observation or performance campaigns.
Independent source comparisons found the retained GF(256)/Reed–Solomon bodies,
Payload keystream path and prepared CBC implementation unchanged by the final
removal of unused recovery, framing and cryptographic APIs. Independent frozen
crypto vectors now exercise the retained prepared CBC API.

Earlier in the cleanup, the shared `regular` group passed 29/29 in 44.08 seconds
after removal of the two obsolete recovery/framing suites. The later crypto
change received the final affected checks above. Legacy passed 8/8 in 1.79
seconds. Web passed 13/13 in 146.47 seconds, including the retained Fast audio
case; the worker and bridge were rechecked after the final crypto change.
The initial Fast and GUI runs passed 35/37 and 26/27 respectively. Corrected
focused reruns completed every previously failing case; all 37 Fast and 27 GUI
cases have passed across those runs, but neither full group was rerun after
all cleanup changes.

The build-tool suite passed 27/32 in the sandbox; the five local socket/GPG
fixtures then passed outside it. Workflow tests passed 3/3, abbreviated-inference
tests 8/8, and certification-tool unit tests 86/86. The maintained-document
path/anchor audit and `git diff --check` pass.

Initial failures remain failed invocations: an invalid tiny channel fixture,
a CLI error exit code, obsolete GUI simulation/readiness expectations, and
missing native audio linkage for adapter test targets were corrected. The
initial sanitizer invocation could not run LeakSanitizer under the sandbox's
ptrace restrictions; the passing reruns retained leak detection outside that
restriction. No omitted check is counted as passed.

## Limits

RunPod instances supported the replay transport and the separately recorded
GPU parity investigation. The current manual capacity checks used simulated
baseband and local headless receiver work. The GUI remains disconnected.
Physical-device and release qualification are separate. The
[abbreviated regression harness](triad-performance.md) measures relative changes
in declared work and cannot establish absolute receiver capacity. Its historical
smoke measurement withheld an unresolved Gaussian projection and made no
comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

Useful [pattern](validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](validation-data/triad-v3/2026-11-05-stochastic/README.md)
measurements remain curated, source-bound research because their conclusions
inform the current triad. Preserve concise results, assumptions, limitations and
needed provenance; do not accumulate duplicate raw captures or parallel
implementations. The
[archive-retirement manifest](validation-data/triad-v3/archive-content-retirement.json)
records removed implementation copies and changed archive hashes; retained
triad data leaves were preserved byte-for-byte. Original source/internal hashes
remain historical provenance, not qualification of repacked archives or current
code. Consult the [evidence index](indexes/evidence.md) and Git history for
superseded results and their limits.
