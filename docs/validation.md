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

## Sustained CPU hardware scaling

The Ryzen 5 PRO 5650U in the AC-powered ThinkPad L15 Gen 2a ran checked,
pinned AVX2 FP64 FMA work at 1, 2, 6 and 12 workers. Up to six workers occupied
distinct physical cores. Twelve used both siblings on all six cores. Each
primary run lasted 180 seconds and the table uses its final 120 seconds.

| Workers | Sustained GFLOP/s | Approximate active clock (GHz) |
| ---: | ---: | ---: |
| 1 | 51.641 | 4.04 |
| 2 | 95.691 | 3.74 |
| 6 | 214.277 | 2.81 |
| 12 | 237.934 | 2.55 |

Twelve workers delivered 11.04% more checked compute throughput than six and
2.49× the two-worker result. In the 90-second reverse-order revisit, the last
60 seconds measured 235.612 versus 216.224 GFLOP/s, an 8.97% gain. Twelve
workers averaged 11.45 busy logical CPUs in the primary run. These gains
describe register-resident FMA work. Same-stream receiver timings establish
how much of that parallel capacity the DSP can use.

**Allocation policy:** these measurements must not be used to reduce the
receiver's default worker budget. Keep all available logical hardware threads
available despite power/thermal limits or temporary/minor timing effects on
this host. Other processors and changing workloads can benefit from the
additional capacity. Low utilization calls for bottleneck analysis, not a
smaller global default; see the [CPU allocation contract](development.md#receiver-cpu-allocation).

The clock estimates use pinned-worker APERF/MPERF deltas. Boost was enabled
under `amd-pstate-epp` with the balanced profile and `balance_performance`
preference. AMD specifies 6 cores, 12 threads, 2.3 GHz base and boost up to
4.2 GHz. Linux documents that active frequency responds to workload and
operating conditions. [AMD specifications](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-5000-series/amd-ryzen-5-pro-5650u.html) · [Linux amd-pstate documentation](https://docs.kernel.org/admin-guide/pm/amd-pstate.html)

Although package-energy counters were unreadable, a separate 180-second run
with the exact same executable exposed readable AMD firmware power telemetry.
It sustained 240.037 GFLOP/s, 2.560 GHz and 11.51 busy CPUs. All 24 steady
five-second samples reported the sustained power limiter (SPL), with no
thermal limiter flags; median socket consumption was 11 W and Tctl was
58.75 °C. Across the eight receiver holdouts, 76 of 77 samples also reported
SPL, sometimes with current or fast package-power limiting. A low-load
control reported 1 W and no limiter flags. This directly establishes active
power limiting on the measured balanced profile. The configured ceiling is
unknown: sampled 11 W consumption is neither that ceiling nor AMD's specified
15 W default TDP. These observations do not supply a clock or GFLOP conversion
factor for receiver costs. The receipt preserves raw v2.2 metrics, the driver
mapping, hwmon cross-checks, timing windows and the unchanged original scaling
records. [Linux AMD telemetry](https://docs.kernel.org/gpu/amdgpu/thermal.html#gpu-metrics) · [Renoir/Cezanne firmware mapping](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/pm/swsmu/smu12/renoir_ppt.c)

Separate 20-second streaming-memory observations gave 9.362, 10.882, 9.312
and 9.012 useful GB/s at 1, 2, 6 and 12 workers. The 12-worker run averaged
8.80 busy logical CPUs while other desktop activity was present. These brief
observations do not establish an intrinsic SMT bandwidth penalty. Keep the
[scaling JSON](../local-evidence/cpu-scaling-current.json) while it supports
the worker-allocation decision and its telemetry limits. Remove the raw
report when a matched scaling check supersedes that evidence.

## Complete headless Robust and manual capacity

The current eight-row direct calibration is in the
[report](../local-evidence/robust-v2/calibration-complete.md) and
[JSON](../local-evidence/robust-v2/calibration-complete.json). It measures the
Ryzen 5 PRO 5650U with a fixed 4608 MiB workspace, compares 1/6/12 workers on
one stream and then measures fresh inference with all 12 workers. Eight
isolated row processes preserve the same source, runtime, case list and native
artifacts; validated disjoint coverage is assembled with the source-matched
native message gate. No row failed workspace admission. This is an assembled
suite, not one uninterrupted receiver run. The calibration took 22 minutes
41 seconds; independent eight-row CPU holdouts took another 8 minutes 27 seconds.

Every row includes the required receiver work. All nominal point estimates
are below 1 s/s, but row 6's nominal interval and state/precision gates remain
unresolved. Conservative **points** for rows 4, 6 and 8 fail the real-time
criterion. Only row 1 has a supported conservative interval wholly below 1;
its upper bound is close to the threshold. Other conservative verdicts remain
unresolved. An uncertain interval does not erase a numerical point failure.
These are conditional estimates under observed operating conditions, not a
saturated hardware ceiling or a reception-success qualification.

| Manual row | Nominal (s/s) | Nominal interval | Conservative point (s/s) | Conservative interval |
| --- | ---: | --- | ---: | --- |
| 1 Sub-9kHz | 0.179814 | 0.085477–0.314060 | 0.773798 | 0.594343–0.999392 |
| 2 Groundwave GPSDO | 0.163281 | 0.135513–0.196353 | 0.791430 | 0.616164–1.012843 |
| 3 Groundwave Helper | 0.240141 | 0.202022–0.283502 | 0.739433 | 0.530973–1.018359 |
| 4 HF Helper | 0.409671 | 0.292566–0.570413 | **6.924697** | 0.961851–633.189664 |
| 5 HF encrypted GPSDO | 0.249103 | 0.222474–0.308350 | 0.903178 | 0.721209–1.213210 |
| 6 FHSS | 0.267353 | 0.065798–1.562399 | **1.038936** | 0.723239–1.584566 |
| 7 HF unencrypted GPSDO | 0.209993 | 0.173206–0.289517 | 0.746100 | 0.310397–1.452913 |
| 8 EME | 0.345843 | 0.286124–0.453207 | **1.289443** | 0.916206–2.005552 |

The exact singleton-DC projection now uses a normalized FP64 sum instead of a
full FFT. Matched native row-2/3 probes ran 1.32–1.35× faster for nominal fine
work and 1.31×/1.47× faster at the capped state, with matching states, work and
endpoint decisions (maximum lane-score difference 3.98e-13). Direct template
output and shared pinned pointer tables reduced matched fine elapsed time by
9.2% at six workers and 4.7% at twelve. The measured mapped-kernel span itself
ran 21% faster at twelve than six workers. The actual full calibration still
used only 1.91–2.44 busy cores for continued fine work; acquisition reached
8.67 in FHSS. Twelve configured workers are not twelve productive cores
throughout the receiver. The default now uses the full visible logical budget,
with explicit allocation controls retained.

The final native `./build.sh test triad` passed **21/21** in 27.27 seconds;
22 focused projection/tracking checks also passed. Temporary evidence is the
[triad log](../local-evidence/robust-v2/triad.log),
[fine attribution](../local-evidence/robust-fine-attribution-current.json),
[DC comparison](../local-evidence/robust-fine-dc-projection-ab.json) and
[materialization comparison](../local-evidence/robust-fine-materialization-ab.json).
Keep these and the source-matched row constituents while they support the
current measurements and unresolved model comparisons; remove them after a
matched replacement supersedes their purpose.

The previous source's conservative points for rows 1–3 were approximately
1.149/1.170/1.180 s/s; these are historical, not paired estimates of the total
speedup. Earlier cold calibration took about 49 minutes and did not establish
capacity. A scratch 128→1024-row mapped batch reduced calls but ran 2–5%
slower, so it was rejected. Old coarse-only replay tools and redundant
campaigns were removed. Their concise negative findings remain; they do not
qualify the current complete receiver.

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

The frozen [eight-row RTX PRO 6000 trace](../local-evidence/robust-v2/gpu-full.json)
took 435.765 seconds internally and 442.33 seconds over SSH. Allowlisted
source and matching native artifacts uploaded in 23.86 seconds; dependency
setup took 42.91 seconds and overlapped transfer. The three paired reference
rows (1, 2 and 6) passed their precision, decision and work gates. Tight-clock
rows 1–2 now evaluate the whole coarse grid in FP64 before pruning and use
precise fine rendering; other rows retain bulk FP32. Continued fine work was
1.98–2.67× faster than the pod's matched 12-worker CPU. Native waveform
simulation and bank preparation still consumed about 80% of end-to-end time.
Do not attribute those CPU stages to GPU acceleration or use one global
GPU-to-CPU multiplier.

The [current scoped full-list forecast](../local-evidence/robust-v2/forecast-current-all8.json)
meets the unchanged nominal and conservative timing budgets for **all eight
rows**. Six nominal fine-cost classes and row 4's recurring acquisition were
refreshed once using the canonical sampling policy (family size 72, maximum
nine looks). Each new prediction and interval was sealed before its fresh
receiver observation. These seven bounded refreshes took 321.298 seconds in
summed measured time. Unchanged component checks are explicitly mixed-time
verification, not one uninterrupted trial. Fixed orchestration costs remain
locally calibrated. This is a current-condition engineering forecast, not a
claim of stability across power states or a statistical confidence certificate.

All nominal guarded intervals are below 1 s/s. Conservative point estimates
for rows 4, 6 and 8 fail that criterion; conservative capacity support remains
unresolved in every row. The intervals retain the v2 engineering allowance
and the underlying models' precision limits.

| Row | Nominal (s/s) | Guarded nominal interval | Conservative point (s/s) | Guarded conservative interval |
| ---: | ---: | --- | ---: | --- |
| 1 | 0.139130 | 0.103260–0.189699 | 0.777407 | 0.495286–1.249240 |
| 2 | 0.141636 | 0.102136–0.201247 | 0.792683 | 0.513470–1.266053 |
| 3 | 0.223572 | 0.139028–0.356268 | 0.741413 | 0.442477–1.272949 |
| 4 | 0.380836 | 0.239002–0.612431 | **6.762115** | 0.826119–513.087530 |
| 5 | 0.224222 | 0.152604–0.369671 | 0.908158 | 0.596759–1.523846 |
| 6 | 0.246790 | 0.163126–0.379720 | **1.041547** | 0.601149–1.980708 |
| 7 | 0.207116 | 0.150137–0.341368 | 0.751476 | 0.257956–1.820995 |
| 8 | 0.346207 | 0.248803–0.533184 | **1.288346** | 0.763505–2.506939 |

Maximum accepted no-cancellation error is 9.44% of nominal service and 10.64%
of conservative service. All input bytes, receiver decisions and required
work gates matched. The maintained estimator's `--refresh-cpu-phase` command
updates one affected class without changing the measured CPU implementation
or invalidating the other calibration stages. The actual recurring-acquisition
CLI initially rejected equivalent empty tuple/list coordinate representations;
normalizing through the existing JSON representation repaired verification.
The recorded fit, fresh observation, sealed point and interval were reused
unchanged, with no refit or repeated timing. The original attempt remains in
the evidence. Independent focused checks passed **71/71**, and 61 shared source
hashes, both native binaries and all three CUDA source leaves still match the
frozen measurements.

A single real [nominal-refresh CLI invocation](../local-evidence/robust-v2/forecast-refresh-row-2-nominal-cli.json)
also passed in 24.449 seconds, using 12 workers and nine canonical looks with
family size 72. Its sealed nominal point was 0.138829 s/s versus 0.137009 in
the fresh observation (1.33% timing error). It verified the maintained entry
point and did not replace or refit any result in the accepted eight-row
assembly. The conservative timing check passed while capacity remained
unresolved. The [guide](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
contains the copyable command.

The [original v2 forecast](../local-evidence/robust-v2/forecast-all8.json)
remains unchanged. It accepted nominal timing only for rows 7–8 and
conservative timing for seven rows. Nominal no-cancellation errors on rows
1–6 were 35.81%, 20.14%, 19.80%, 16.98%, 18.57% and 12.10% (the last failed
its 10% single-stage limit); row 4's conservative error was 22.47%. These
failures motivated the separate current-condition refresh, not looser limits.

Nominal drift was primarily continued fine processing. One predeclared
[matched row-2 crossover](../local-evidence/robust-v2/driver-crossover.json),
after a 60-second idle observation, alternated the canonical calibration and
independent trace drivers in ABBA order on one exact fixture. Four-second
continuations averaged 0.524215 and 0.532493 seconds, a 1.58% difference in
the opposite direction to the historical discrepancy. Inputs, state, work and
scores matched in all four looks. Garbage collection overlapped only 0.21 ms
of timed continuation. Both drivers now ran near the earlier fresh holdout.
This did not reproduce a driver/normalization difference or material GC cost
in the matched diagnostic. Historical per-phase clock data is unavailable, so
no unique cause or retrospective
frequency correction is justified. Keep the original failed point-accuracy
checks visible. A long calibration's operating conditions do not automatically
represent a continuously receiving application's duty cycle.

A real [cached same-case row-7 CLI forecast](../local-evidence/robust-v2/forecast-target-7-same-row.json)
reused qualified references 1/2/6. It predicted nominal 0.207116 s/s versus
0.208497 s/s in the fresh independent trace (−0.66%). Its conservative timing
check also passed, but the 0.751476 s/s conservative point retains unresolved
capacity support. Target GPU work took 22.713 seconds, local verification
23.046 seconds and the CLI process 2.06 seconds: about **47.82 seconds** after
applicable CPU calibration and GPU references already exist. This demonstrates
a fast, qualified same-case workflow; it is not a full-list accuracy result or
a cross-case transfer certificate. The separate H0-only calibration took
20.574 seconds and avoided DATA generation. Adding it to the encrypted-5→clear-7
transfer supplied the missing work class but did not repair donor timing drift;
that transfer remains rejected.

The [two-job cloud comparison](../local-evidence/robust-v2/gpu-dual-5-7.json)
ran unpaired rows 5/7 in 31.01 seconds over SSH, versus at least 39.754 seconds
of their measured serial work. It preserved input bytes and useful work,
allocated 24 workers within 27 visible, and admitted aggregate host/device
budgets before launching. Paired timing references stayed isolated. This
supports carefully bounded concurrent preparation, not independent simulations
as evidence of single-stream CPU capacity. The disposable pod was terminated
and no persistent volume was created.

Retain the current CPU/GPU receipts, qualified references, original failed
forecasts and crossover while their accuracy claims and discrepancies remain
active. Replace them only with source/runtime-matched evidence and preserve
concise negative findings here. The previous FP32 trace rejected rows 1–2 on
tight-clock ordering; its v1 row-7 hybrid also failed a relative-only H0 timing
gate. Those historical policies are not retroactively relabeled as passing.
Old coarse-only replay/calibration programs were removed. This continuation
retired 2,452,924,679 bytes across 31,564 obsolete campaign/source-copy/report
files, retaining concise findings and current unresolved evidence. See the
[preferred workflow](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
for current commands and exact applicability gates.

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
