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

The current direct calibration covers all eight rows on the Ryzen 5 PRO 5650U.
It uses a fixed 4608 MiB workspace, compares 1/6/12 workers on one stream and
measures fresh inference with all 12 workers. The original run completed rows
1–6 and passed the native message gate. The then-visible workspace budget
rejected admission for rows 7 and 8. Fresh isolated measurements filled those
rows under the same workspace cap, with matching source, runtime, case list and
native artifacts. The assembled report retains both original admission failures
and binds each constituent to its hash. Summed constituent elapsed time was
25 minutes 7.69 seconds across the three runs.
[Report](../local-evidence/current-parallel/calibration-current-all8.md),
[JSON and constituent hashes](../local-evidence/current-parallel/calibration-current-all8.json),
[original run](../local-evidence/current-parallel/calibration-complete.json),
[row 7](../local-evidence/current-parallel/calibration-row-7.json),
[row 8](../local-evidence/current-parallel/calibration-row-8.json).

Every row covers the required receiver work. All eight nominal intervals are
below 1 s/s, with point margins of 60.72%–83.70%. The conservative interval
and work gates support conditional capacity on rows 2, 5 and 7. The remaining
conservative verdicts are unresolved. The conservative **points** for rows
4, 6 and 8 fail the real-time criterion. Row 4's very wide interval is driven
primarily by the unresolved largest-window noise-only acquisition model.
The complete suite therefore reports unresolved margins. These conditional
estimates apply to the observed operating conditions. Whole-receiver
saturation and communication-success probabilities remain unqualified.

| Manual row | Nominal (s/s) | Nominal interval | Conservative point (s/s) | Conservative interval | Conservative decision | Continued fine busy cores |
| --- | ---: | --- | ---: | --- | --- | ---: |
| 1 Sub-9kHz | 0.166160 | 0.146607–0.188404 | 0.803708 | 0.586333–1.093536 | Unresolved | 2.307 |
| 2 Groundwave GPSDO | 0.163044 | 0.139920–0.191862 | 0.770207 | 0.591796–0.997992 | Conditional pass | 2.380 |
| 3 Groundwave Helper | 0.256045 | 0.192917–0.332388 | 0.821223 | 0.422765–1.455372 | Unresolved | 2.435 |
| 4 HF Helper | 0.392845 | 0.316171–0.500705 | **7.488576** | 0.801072–18551.237744 | Unresolved | 3.250 |
| 5 HF encrypted GPSDO | 0.231940 | 0.203746–0.300702 | 0.726290 | 0.589419–0.957768 | Conditional pass | 2.944 |
| 6 FHSS | 0.270612 | 0.204054–0.357224 | **1.090006** | 0.873323–1.405219 | Unresolved | 2.698 |
| 7 HF unencrypted GPSDO | 0.221163 | 0.195515–0.288502 | 0.752823 | 0.675466–0.893626 | Conditional pass | 2.999 |
| 8 EME | 0.381753 | 0.298216–0.520121 | **1.359897** | 1.077178–2.605298 | Unresolved | 2.801 |

Broad FP64 fine blocks now split independent basis DFTs and finite checks
across the existing receiver pool. The controller rejoins the selected bins
in input order before the same Gram and state updates. The bounded row-7
attribution measured 0.255 s of serial basis DFT in a marked 1.082 s fine
observation. In interleaved same-stream comparisons with twelve workers,
the new dispatch reduced continued fine elapsed time by **8.3% on row 7**
and **10.1% on row 4**. Busy cores rose from 2.02 to 2.99 and from 1.99 to 3.34,
respectively. The row-7 six-worker comparison also improved, with wider
baseline scatter. Enabled row-7 elapsed time was about 0.707 s per four
baseband seconds at either allocation. Every completed pair preserved
work, retained states, scores and endpoint decisions exactly.
[Attribution](../local-evidence/current-parallel/profile.json),
[row 7 at twelve](../local-evidence/current-parallel/fft-qualification-7-12.json),
[row 4 at twelve](../local-evidence/current-parallel/fft-qualification-4-12.json),
[row 7 at six](../local-evidence/current-parallel/fft-qualification-7-6.json).

In the complete calibration, elapsed-weighted continued fine utilization
ranged from 2.31 to 3.25 busy cores across the eight rows and from 2.70 to 3.25
on broad-DFT rows 4–8. Nominal acquisition reached 8.51 on FHSS and maximum
state fine work reached 3.63 on HF Helper. Template planning, sequential
tracking transitions and small model batches still limit useful concurrency.
The full logical worker budget continues to apply regardless of the
six-worker timing comparison. [CPU allocation contract](development.md#receiver-cpu-allocation)

The named `./build.sh test triad` passed **21/21** in 29.47 seconds. All **43**
focused receiver and tracking tests passed after correcting a missing test
import path in the first invocation. The separate timed row-2 control reached
its time limit during preparation. The singleton-DC branch kept its normalized
FP64 sum, and rows 1–3 exercised that branch in the complete calibration.
[Named test log](../local-evidence/current-parallel/triad.log)

Earlier matched native row-2/3 probes measured 1.32–1.35× nominal fine
speedups from the singleton-DC sum. Direct template output and shared pinned
pointer tables reduced matched fine elapsed time by 9.2% at six workers and
4.7% at twelve. The earlier full calibration measured 1.91–2.44 busy cores
for continued fine work. These source-bound observations describe separate
optimization comparisons. A scratch 128→1024-row mapped batch ran 2–5%
slower despite fewer calls, so it was rejected.
[DC comparison](../local-evidence/robust-fine-dc-projection-ab.json),
[materialization comparison](../local-evidence/robust-fine-materialization-ab.json).

Keep the current calibration constituents, named test log and scoped fine
receipts while they support the capacity and utilization claims above.
Remove them when matched replacements supersede those claims and distill
the useful findings here.

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

The earlier receiver's
[sensitivity pilot](../local-evidence/robust-manual-sensitivity-current.json)
received both independent bits correctly at 8.113563 dB-Hz in case 7,
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

The current-source [eight-row RTX PRO 6000 trace](../local-evidence/current-parallel/gpu-current-full.json)
took 426.221 seconds internally. Allowlisted source and matching native
artifacts were only a few megabytes and transferred over authenticated SSH/SCP;
the source archive was 1.58 MB. The three paired reference rows (1, 2 and 6)
passed their precision, decision and work gates. Tight-clock
rows 1–2 now evaluate the whole coarse grid in FP64 before pruning and use
precise fine rendering; other rows retain bulk FP32. Continued fine work was
1.76–2.44× faster than the pod's matched 12-worker CPU. Native waveform
simulation and bank preparation still consumed about 80% of end-to-end time.
Do not attribute those CPU stages to GPU acceleration or use one global
GPU-to-CPU multiplier.

The [current full-list forecast](../local-evidence/current-parallel/forecast-current-all8.json)
covers all eight rows with source-matched local CPU costs and
[independent CPU traces](../local-evidence/current-parallel/holdout-current-all8.json).
The eight isolated holdouts took 539.759 seconds inside the drivers. Row 5 was
once refused admission before any receiver observation; its original log is
retained, and a fresh invocation admitted the same 4608 MiB cap and 12 workers.
No memory guard or timing threshold changed.

**All eight nominal and conservative timing checks pass.** The forecast reports
`complete_point_estimates_with_unresolved_support` after two bounded local
phase refreshes described below.
All nominal guarded intervals
are below 1 s/s. Conservative points for rows 4, 6 and 8 fail real time, and
conservative capacity support remains unresolved for every row after the
forecast's engineering allowance. This is complete work accounting with explicit
uncertainty, not a saturated CPU ceiling or
all-case communication qualification.

| Row | Nominal (s/s) | Guarded nominal interval | Conservative point (s/s) | Guarded conservative interval |
| ---: | ---: | --- | ---: | --- |
| 1 | 0.166658 | 0.127485–0.221652 | 0.802772 | 0.488611–1.366920 |
| 2 | 0.144861 | 0.108060–0.199713 | 0.768478 | 0.493163–1.247490 |
| 3 | 0.256398 | 0.167754–0.391045 | 0.826672 | 0.352304–1.819215 |
| 4 | 0.393550 | 0.274931–0.589065 | **7.784513** | 0.667560–23189.047180 |
| 5 | 0.231939 | 0.176971–0.354010 | 0.728116 | 0.487917–1.202898 |
| 6 | 0.269880 | 0.177438–0.420264 | **1.093199** | 0.725847–1.762829 |
| 7 | 0.220855 | 0.169454–0.340111 | 0.676108 | 0.430068–1.168338 |
| 8 | 0.382316 | 0.259318–0.611907 | **1.368812** | 0.897649–3.256623 |

Maximum accepted no-cancellation errors are 14.57% of nominal service and
17.02% of conservative service. The
[initial forecast](../local-evidence/current-parallel/forecast-initial-all8.json)
retains the original row-2 nominal and row-7 conservative timing failures.
One [canonical row-2 nominal refresh](../local-evidence/current-parallel/forecast-refresh-row-2-nominal.json)
took 24.770 seconds, with family size 72 and at most nine looks. It sealed its
new point and interval before a fresh receiver observation; nominal error was
−2.29%. Row 7's initial maximum-state cost contributed 15.253% error against
the 15% stage limit, so it required a separate cost-class refresh. Its
[accepted maximum-state refresh](../local-evidence/current-parallel/forecast-refresh-row-7-cap-repaired.json)
took 33.057 seconds and nine canonical looks. It rechecked the retained erasure
observation against the new bound and sealed the prediction before a new capped
receiver observation. Conservative error was +4.98%. The two accepted refreshes
took 57.827 seconds in summed measured time, excluding the failed setup attempt
below. The final report recomputes the complete list with only those two fits
and observations replaced. Other component checks remain explicitly mixed-time
verification. No measured source or precision/work/timing threshold changed.

The first row-7 maximum-state attempt used the canonical sampler, but its
temporary wrapper rejected live tuple coordinates against archived JSON lists
before sealing or obtaining an independent observation. That transient fit
had not been persisted and supplies no replacement numerical result. The
[failed-attempt disposition](../local-evidence/current-parallel/forecast-refresh-row-7-cap-disposition.json)
records that limitation and the original log. An
[offline diagnosis](../local-evidence/current-parallel/cap-refresh-format-diagnosis.json)
reproduced the representation rejection on archived data and restored the exact
original forecast through ordinary JSON normalization. One explicit format-only
harness repair then normalized and persisted the fit before verification; that
run produced the accepted receipt above. This repaired an entry-format failure
before any independent accuracy test. It did not retry a failed timing result
or relax a near-threshold limit. Both temporary wrappers were removed.

The disposable RTX PRO 6000 pod was stopped and terminated after results were
verified locally. No persistent volume was created. Temporary tensor sources,
CPU experiment drivers and the upload archive were removed. Retain the current
calibration, GPU references, holdouts, original failures and refresh receipts
while they support these claims and unresolved comparisons; retire them when
matched replacements supersede their purpose.

Earlier source-bound workflow results remain useful procedural evidence, not
current CPU calibration. A [cached same-case row-7 CLI forecast](../local-evidence/robust-v2/forecast-target-7-same-row.json)
passed both timing gates in about 47.82 seconds including target GPU work,
local verification and estimator startup, after applicable calibration and
paired references already existed. A [two-job cloud comparison](../local-evidence/robust-v2/gpu-dual-5-7.json)
ran rows 5/7 in 31.01 seconds over SSH with admitted aggregate CPU and memory
budgets and identical useful work. Independent simulations accelerate cloud
preparation; they do not demonstrate single-stream CPU capacity.

The [prior source's original forecast](../local-evidence/robust-v2/forecast-all8.json)
failed six nominal timing checks and one conservative check. Separate bounded
phase refreshes then passed its full list, but that result does not apply to
the changed CPU implementation. A [matched driver crossover](../local-evidence/robust-v2/driver-crossover.json)
found only 1.58% difference between calibration and holdout drivers and did not
reproduce the historical drift or a material garbage-collection cost. No unique
cause or retrospective clock correction is justified. The earlier tight-clock
FP32 trace rejected rows 1–2 on candidate ordering, and encrypted-5→clear-7 cost
transfer remained rejected despite supplying its missing H0 class. Keep those
failures source-bound; they do not prohibit qualified lower precision elsewhere.
See the [preferred workflow](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
for commands and applicability gates.

### Tensor-core lag-search conclusion

Two direct lag-domain Fourier transforms were implemented and tested on the
RTX PRO 6000 Blackwell using Triton 3.4: TF32x3 and explicitly rounded TF32,
both with FP32 accumulation and unchanged FP64 phase setup, Gram/rank and
quadratic scoring. Each executed 135 tensor launches across the actual row-3
DATA, noise and recurring search workload. Executed-kernel PTX and disassembled
SASS included `mma.sync.aligned.m16n8k8.row.col.f32.tf32.tf32.f32` and
`HMMA.1688.F32.TF32`. The [Triton arithmetic contract](https://github.com/triton-lang/triton/blob/v3.4.0/python/triton/language/core.py#L1858-L1890)
and [NVIDIA instruction reference](https://docs.nvidia.com/cuda/parallel-thread-execution/#warp-level-matrix-instructions-mma)
describe those operations.

Both variants passed the existing paired CPU receiver gates without relaxing
thresholds: public cells, candidate order, decisions, all five fine-work strata
and their scores matched. The worst coarse-score difference used 0.40% of the
allowed tolerance for TF32x3 and 56.76% for rounded TF32. This establishes
bounded computational agreement, not independent communication sensitivity.

| Variant | Isolated transform including coefficients, relative to ZoomFFT | Whole-work finding |
| --- | ---: | --- |
| TF32x3 | 0.78× | Already slower at the isolated transform; actual H0 searches were also slower. |
| Rounded TF32 | 1.08× | Warm ABBA process medians were 32.580 s versus 31.975 s for ZoomFFT: 1.89% slower, with both paired comparisons slower. DATA acquisition also increased from 0.935–0.939 s to 1.23–1.26 s. |

The TF32 experiment met its numerical gates but failed its predeclared useful
whole-trace improvement criterion. A separate power-of-two-scaled FP16 variant
then tested the more efficient FP16 tensor instructions, with FP32 accumulation,
explicit zero/nonfinite/range guards and scaling, packing, coefficient setup
and rescaling included in its preflight timings. At the two public row-3 matrix
geometries, the isolated transforms ran 1.88× and 1.33× faster than ZoomFFT.
Preflight PTX and SASS directly showed
`mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32` and `HMMA.16816.F32`.
Relative L2 errors were at most 2.67e-4; the tested nonzero inputs did not
underflow during packing.

The paired row-3 FP16 receiver passed the unchanged CPU precision, decision
and work gates, with exact candidate coordinates/order and all five fine-work
records. Coarse-score error consumed at most 58.39% of the existing tolerance.
However, its measured acquisition stages took 4.103 s versus 3.543 s for
ZoomFFT; the sum of common measured stages was effectively unchanged
(28.248 s versus 28.314 s, −0.24%). These sums exclude CPU-reference validation
overhead and unassigned process overhead. One bounded pair establishes no
material gain here, not a stable slowdown across workloads. The receiver
wrapper's counter was attached to temporary screen copies and did not reach
the original-screen receipt: changed scores and eligible geometry corroborate
the alternate arithmetic, but direct instruction proof is confined to the
preflight. No per-receiver FP16 launch count is claimed.

All three implementations and their switches were removed; the canonical GPU
FFT path remains. Lower precision was adequate for this tested coarse workload,
but the higher arithmetic throughput did not make this algorithm replacement a
faster forecast. This concludes the tested variants; it is not a prohibition on
different tensor algorithms or a reason to force all bulk work into FP64.

The [TF32 disposition receipt](../local-evidence/current-tensor/disposition.json)
binds actual source/native hashes, runtime, unchanged inputs, paired checks,
warm timing order and instruction evidence. Its roughly 2.3 MB of constituent
receipts and the [FP16 disposition](../local-evidence/current-tensor/fp16/disposition.json)
with its three constituent receipts support these specific numerical and
performance conclusions; remove them when the conclusions are superseded or
the summary alone suffices. No experimental driver or copied source tree is
retained.

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
