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

A current-source eight-row direct suite is now assembled in the human-readable
[`report`](../local-evidence/robust-cases-12-complete.md) and
[`JSON`](../local-evidence/robust-cases-12-complete.json).
It uses the unchanged 12-CPU-affinity Ryzen 5 PRO 5650U source closure and
4608 MiB workspace budget, compares 1/6/12 worker allocations, then takes fresh
12-worker measurements. The eight-row assembly combines a full-list base
attempt (six measured rows; rows 7 and 8 failed initial workspace admission)
with fresh matched replacements for those two rows. The replacement for row 7
also supplies the 12-worker estimate used in the suite. The reports retain
their source, case-list and native-binary hashes and assembly lineage. The
base run's message gate passed; the assembly adds work estimates and is not one
uninterrupted eight-row run.
After the final test group, 63 measured CPU source/native file hashes were
verified unchanged; all three GPU runtime source leaves matched the final trace.

All eight **nominal conditional estimates** are below 1 s/s. This is not a
complete-capacity pass: every conservative cold-state point is separately
reported, six are at or above 1 s/s, and all eight stronger capacity decisions
remain unresolved. Workspace estimates fit the declared budget but are not
certificates. The detailed report has conditional intervals, state/margin
gates and per-stage CPU accounting.

| Manual row | Nominal (s/s) | Conditional interval (s/s) | Conservative cold point (s/s) |
| --- | ---: | --- | ---: |
| 1 Sub-9kHz | 0.234998 | 0.198937–0.277056 | 1.148788 |
| 2 Groundwave GPSDO | 0.244695 | 0.178981–0.332112 | 1.170012 |
| 3 Groundwave Helper | 0.366213 | 0.252997–0.536509 | 1.179995 |
| 4 HF Helper | 0.456268 | 0.328338–0.645724 | 7.649702 |
| 5 HF encrypted GPSDO | 0.260899 | 0.233839–0.321704 | 0.908173 |
| 6 FHSS | 0.317366 | 0.251902–0.400944 | 1.327087 |
| 7 HF unencrypted GPSDO | 0.261014 | 0.217385–0.345319 | 0.945196 |
| 8 EME | 0.387971 | 0.310421–0.519707 | 1.447646 |

The host exposed all 12 logical CPUs across six physical cores, but this did
not demonstrate near-maximum receiver utilization. Continued fine work used
about 1.7–2.0 busy cores; nominal acquisition ranged about 1.1–8.2 busy cores.
The 12/6 fine-service comparisons ranged 0.891–1.044, so extra workers did not
establish a consistent throughput gain. The selected fine/basis bottleneck
remains a priority for optimization. See the stage observations in the JSON;
do not scale service by idle cores.

A scratch A/B increased mapped-kernel basis rows per call from 128 to 1,024
and reduced the number of calls from 148 to 48. It preserved useful work and
scores but was 4.7% slower at six workers and 2.0% slower at twelve, so the
larger batch was rejected and the scratch change removed. It did not justify a
production patch. The final `./build.sh test triad` passed **21/21** in 26.19
seconds; its temporary log is
[`robust-triad-release-check.log`](../local-evidence/robust-triad-release-check.log).

Keep the three constituent CPU reports and assembly while these current-source
nominal, conservative-state and workspace claims are active. The current
message-gate report and native binary/source hashes support the gate/provenance
claim. Remove the constituent reports and assembly together when a later
source-matched full suite supersedes these measurements; retain concise
conclusions here. This measurement does not resolve complete-message
sensitivity, receiver success probability or release qualification.

The superseded eight-row CPU runs are historical only. The earlier full cold
run took 2,958 seconds (about 49 minutes); its conservative repeated-state
measurements did not qualify capacity. The older v3 analysis also stopped on
raw continuation rates before slot normalization and omitted other service
components, so its table cannot be treated as a correctly stopped estimate.
Neither run demonstrated a saturated 12-thread ceiling, current capacity, or
sensitivity/release qualification. Their detailed CPU tables are removed here
to keep the current result unambiguous. The obsolete fit/replay/holdout/transport
and duplicate snapshot receipts were removed after distilling their findings
(56 files, 51,473,265 bytes). Distinct unresolved sensitivity and release
evidence remains.

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

The v3 default manual invocation used the continued-session measurement. The
promotion evidence below records that earlier phase and is historical; the full
v3 capacity tables were removed because current-source results supersede them.
The [promotion receipt](../local-evidence/robust-manual-promotion-current.json)
verifies that the promoted probe is identical and that DSP sources and the
native binary did not change. It preserves the original measurement phases:
the separate continuation phase used family size 24; the v3 default uses 64
for its then-current list. The v3 wiring and accounting were tested separately,
including acquisition gating, missing-stage subtotals, slot normalization,
workspace/RSS/utilization and propagation of supported nominal failures. The
old observations are not relabeled as a full run of v3. Its full run was
source-bound but is no longer the maintained CPU estimate.

Keep prior base, continuation, allocation, pilot, incomplete-confirmation and
promotion reports only for the stated unresolved sensitivity/resource
comparisons; retire each when equivalent current evidence supersedes its
purpose. The duplicate temporary continuation/allocation runners and earlier
exploratory message/allocation outputs have been removed.

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

## GPU-assisted receiver measurement

[`gpu-rtx-4608-final.json`](../local-evidence/gpu-rtx-4608-final.json) records
an eight-row RTX PRO 6000 RunPod trace (421.386 seconds, including 148.086
seconds of native capture-generation wall time). Authenticated SSH/SCP
transferred about 1.5 MB of allowlisted source and 0.8 MB of matching native artifacts; the outer
remote measurement took 427.3 seconds. Three paired CPU-reference gates (rows
3, 4 and 6) passed. CUDA coarse-data work measured 2.11–14.71x and continued
fine work 1.73–2.33x the pod's 12-worker CPU for the measured stages. This
demonstrates a fast GPU-assisted diagnostic path for future tests, not a
full-receiver GPU port or local CPU speed estimate.

The GPU report's end-to-end native bank path was only 1.01–1.32x the pod CPU,
not the much larger stage-only gain. This GPU workflow rejected forecasts for
manual rows 1 and 2; rows 3–8 have same-row nominal points. A global GPU/CPU
multiplier is unsupported. Bulk CUDA work used FP32, with small FP64
Gram/Gaussian decision work.

The follow-up reports cover same-row GPU rows 3–8, pure transfer from row 5
to 7, and hybrid transfer with row-7 noise-only fine calibration. Same-row
nominal forecasts pass for all six rows; complete-capacity verdicts remain
unresolved, and rows 1–2 are rejected for this GPU forecast. The independent
24.852-second CPU row-7 holdout used the exact 4608 MiB cap and byte-identical
input/validity. Pure transfer predicted 0.260176 s/s versus 0.238000 on that
holdout (+9.318%) and 0.261014 on the withheld full direct row (-0.321%). Its
nominal stage checks passed, but its cold ceiling had an 11.63% H0 cache-hit
discrepancy, exceeding the 5% tolerance by 6.63 percentage points. Hybrid
transfer produced 0.260176/0.911570
s/s nominal/cold versus direct 0.261014/0.945196 (-0.32%/-3.56%); H0
acquisition cost was 0.002411 versus 0.001641 s/s (+46.92%), so its overall
validation status is `cost_drift`. The same-row-7 result was 0.260085/0.946963
and had the same acquisition-stage drift. Thus the experiments demonstrate
credible nominal row-7 forecasting but not an accepted complete CPU estimator;
the differing gate outcomes must remain visible. Ten numerical forecast tests
passed. See [`robust-gpu-cpu-same-row-final.json`](../local-evidence/robust-gpu-cpu-same-row-final.json),
[`robust-gpu-cpu-transfer-7-final.json`](../local-evidence/robust-gpu-cpu-transfer-7-final.json),
[`robust-gpu-cpu-transfer-7-noise-final.json`](../local-evidence/robust-gpu-cpu-transfer-7-noise-final.json)
and [`robust-cpu-holdout-7-4608.json`](../local-evidence/robust-cpu-holdout-7-4608.json).
Retain those reports while the measured agreement, holdout and cost-drift
claims are current; remove them when a source-matched replacement supersedes
those claims. Do not compare their six-row forecast coverage/holdout workload
as a direct speedup over the distinct full-suite calibration duration.

Both disposable pods were terminated; the reported task-balanced delta was
about $2.58, not invoice reconciliation. Keep the GPU trace while it supports
the measured stage-speed and precision claims, then remove it after a
source-matched replacement. No single GPU-to-CPU factor is supported.

The earlier replay/calibration campaign was slow, sensitive to provenance and
transport changes, and failed to establish a reusable CPU estimator. Its
dedicated programs and tests were removed. The direct complete suite above is
the CPU method; row-7 nominal CPU forecast agreement has been demonstrated,
but its cold cache coverage and acquisition cost gates remain unresolved.
Historical replay receipts have been removed after preserving the relevant
limitations here. Keep distinct sensitivity and release evidence only while
their unresolved claims require it.

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
