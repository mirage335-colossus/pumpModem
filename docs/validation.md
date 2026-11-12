# Validation status

The native CPU automatic policy uses qualified Q15 spectral products with FP32
FFTs on its supported AVX2 one-ray geometry, and FP32 or FP64 on other paths.
An earlier bounded FP32-versus-FP64 comparison improved a challenging five-slot
workload by **1.196×**; that historical comparison is not a current Q15 result.
The local Radeon Vulkan backend uses qualified packed-FP16 direct work and passes
actual-device correctness checks. Its earlier **1.269× complete coarse-phase
speedup** and narrow-case **39.6% slowdown** compare against the historical FP64
CPU path. Cold compilation remains a separate cost below.
Earlier Ryzen CPU and RTX 4090 CUDA results retain their original source and
hardware scope. No complete receiver-capacity or Tensor Core utilization claim
follows.

Updated 11 November 2026. The native history, matrix/preparation and wide-search
follow-through is complete with the scoped checks below, including the fine-tail
repair. Complete-receiver capacity and refreshed CPU forecasts remain unresolved.
The earlier
bounded native throughput/precision investigation
from `6bc9b0e3dd0ca91b3b8e3f4851301e6cf1b1371d` is complete. It supports scoped
GPU phase improvements, three component forecasts and an accepted CPU latency
tradeoff; complete-receiver capacity and nominal/conservative GPU-assisted CPU
forecasts remain unresolved. The earlier migration checkpoint remains the
baseline. Historical measurements retain their original source/runtime.
None is release certification.

The historical cleanup checkpoint was based on
`357a50970a0c6214eeaade7c5704bbc39abd4b79` and committed as
`419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. Its concise outcomes and limitations
remain below; they are not release certification or validation of changed code.

The native CPU receiver's serial steps now overlap parallel work within a slot,
and its transform visits the same butterflies in a cache-blocked order. Matched
replays are 1.4–2.0× shorter with identical receipts on the one measured host.
Earlier local CPU phase costs no longer describe this work; see the
[follow-through](#receiver-overlap-and-transform-order-follow-through).
Helper acquisition now uses the slot's lanes and, where the lease allows, runs
beside coarse preparation. Its workspace bound follows the public calendar, so
it no longer sets a Helper slot's lease; see the
[Helper follow-through](#helper-acquisition-overlap-follow-through).

## Receiver overlap and transform-order follow-through

**Question.** Does the native CPU receiver leave hardware idle behind serial
steps, per-wave barriers or fixed work shares, and does removing that waste
shorten elapsed same-stream service without changing any result?

Baseline is commit `22e92c3581b341c0c1293c24e084dc59fa493bb7`; the candidate is
this change on top of it. Both are Release builds from the same profile with
GNU 14.2.0, measured through `triad_receiver_probe` (SHA-256 `de87c65b…beb514`
and `01693f3f…d951d4`) on the Ryzen 5 PRO 5650U: six cores, twelve logical
CPUs, `H=12`, `W=24`, powersave governor. A desktop session used about 0.44
logical CPU throughout, so about 96% busy is this host's practical ceiling.

### Where the hardware was idle

Stack samples of the baseline single-owner row-5 replay found fewer than twelve
runnable threads in 11 of 70 samples. In the emptiest, 18–22 of 24 workers were
blocked behind one source-tile fill in the mapped basis, or asleep between the
fine stage's per-segment dispatches while the root predicted, pruned and
projected the next blocks. Coarse work used fixed shares: 26 lane jobs on 24
lanes, and one fixed share of each tile per lane. Helper acquisition, the
observation filter and the mapped-profile gate's controls each ran on one
thread with the rest of the pool idle.

Three measurements bounded what scheduling alone can recover:

| Observation | Result |
| --- | --- |
| Dynamic coarse jobs alone, quiet row 4, 64 tiles | Busy 88.2% → 94.1%, but elapsed only 23.9 → 23.5 s and CPU 251 → 265 s. |
| Current build, six pinned cores versus twelve logical CPUs | 6.10 versus 6.07 ms per public frequency, at 99.8% and 93.1% busy. SMT adds no coarse-search throughput on this host. |
| Observation filter split into parallel chunks, row 2, with the earlier stage-major transform | Coarse preparation CPU 16–17 → 27–33 s with no elapsed gain. Reverted; not repeated with the blocked transform. |

A profile of the quiet row-4 search then put about 47% of its time in the
transform's scalar parts: bit reversal, the first two stages and one call per
small butterfly group. Row 6 spent about 46 ms of CPU per 262,144-point
transform. The larger loss was unproductive busy time, not idle time.

### Changes

- One executor stage per owner with urgent and background leaves; see the
  [overlap contract](development.md#overlap-within-a-slot). Standalone sessions
  use a private instance, replacing their separate thread pools.
- Coarse: per-workspace bank jobs, the observation filter as one overlapped
  job, and each native advance searched as one span of dynamically claimed jobs.
- Fine: fixed-affine states as background chains, observation one dispatch
  ahead, source tiles filled about two tiles ahead, shared-locked tile lookups
  and one rank evaluation per pruned state.
- Transform: cache-blocked stage order, vectorized first stages, branch-free
  bit reversal, omitted butterflies for outputs the caller does not read, and
  correlation spectra stored in transform order.
- Helper: one shared branch-filter plan and response for all pilot branches.

### Results

The final set was fixed before it ran and alternated builds: two looks of the
12-slot row-5 replay at the default and one-owner allocations, one DATA slot
for rows 2–8, one unassisted quiet row-4 slot and one five-slot row-4 message.
Earlier exploratory runs are not counted. Elapsed seconds, baseline → current:

| Same-stream work | Baseline | Current | Ratio |
| --- | ---: | ---: | ---: |
| Row 5, 12 slots, `W=24/O=24`, 686.47 s of baseband | 25.891, 25.905 | 17.909, 17.942 | 1.44× |
| Its process CPU seconds and busy fraction | 293.2, 295.1; 94.4%, 94.9% | 206.3, 207.5; 96.0%, 96.4% | |
| Row 5, 12 slots, one owner | 27.155, 27.005 | 18.497, 18.124 | 1.48× |
| Its ready-input interval, commits 4→8, s/s; busy | 0.03783, 0.03773; 88.9%, 88.5% | 0.02544, 0.02564; 93.8%, 93.6% | 1.48× |
| Row 4 message: three DATA and two unassisted quiet slots, 446.60 s of baseband | 245.39 | 124.17 | 1.98× |
| Its DATA commits and final commit | 14.3, 22.8, 38.4; 245.4 | 7.1, 11.1, 16.6; 124.2 | |

| One slot, standalone, `W=24` | Baseline | Current | Ratio | Fine busy |
| --- | ---: | ---: | ---: | --- |
| Row 2 DATA | 12.95 | 10.75 | 1.20× | 78% → 92% |
| Row 3 DATA | 16.14 | 13.58 | 1.19× | 81% → 93% |
| Row 4 DATA | 6.44 | 4.18 | 1.54× | 91% → 97% |
| Row 5 DATA | 1.87 | 1.34 | 1.40× | 88% → 95% |
| Row 6 DATA | 35.54 | 21.21 | 1.68× | 86% → 95% |
| Row 7 DATA | 1.88 | 1.34 | 1.40× | 87% → 96% |
| Row 8 DATA | 5.05 | 3.33 | 1.52× | 89% → 93% |
| Row 4 quiet, complete 8,596-frequency search | 113.36 | 56.16 | 2.02× | 91% → 96% |

The quiet row-4 window is 71.5 s on a 90 s cadence. That one slot's elapsed
ratio fell from 1.26 to 0.62 s/s; an earlier, warmer baseline run took 138.5 s.
Row 6's coarse search fell from 13.79 to 4.32 s, and row 2's coarse preparation
from 2.23 to 1.43 s. The one-owner replay is now within 3% of the default one.

**Identity.** Every listed pair has the same input digest (row 5
`0673bd95…e12f4493`, row-4 message `f75ebde4…973ea250`). Apart from elapsed
times, the automatic memory limit and workspace quotes, the receipts are
identical field for field: decisions, bit and quiet evidence, coarse proposals
and refined coordinates, work counters, mapped-basis cache receipts and
messages. `test_search_fft` compares both transform kernels bit for bit with a
stage-major reference, including signed zeros, output prefixes and permuted
input.

**Functional checks.** Triad 33/33, contract 35/35 and GUI 29/29 passed. A
scratch ThreadSanitizer build, which is not a maintained profile, reported
nothing in the FFT, stream, fine, coarse, native-receiver and message tests or
in a five-slot, four-owner row-5 message.

### Limits and unresolved work

- One host, power- and thermally limited, with a desktop session running.
  Elapsed values moved by several percent between thermal states, so only
  alternated pairs are compared. This is not a capacity pass or an interval.
- Row 1 was not measured: its capture exceeds the declared sample budgets. The
  stream scheduler was measured on row 5 and one row-4 message only.
- **Earlier local CPU phase costs and forecasts no longer apply** to coarse,
  fine or Helper work. Refresh the affected calibration before any forecast.
- No accelerator ran. Compiled CUDA was not built; only the Vulkan plan check
  in the triad group passed. Device tiles and device fine statistics keep
  their previous order.
- The Radeon/native chirp-Z crossover constants were measured against the
  earlier transform and now understate native speed. Re-measure before retuning.
- Slot workspace quotes changed by 0–0.9%: row 2 +0.20%, rows 5 and 7 +0.14%,
  row 6 +0.88%, row 8 +0.12%, rows 3 and 4 unchanged.
- Helper acquisition was serial in this comparison: 0.34–0.36 s per Helper DATA
  slot, 8.6% of the row-4 DATA slot. The
  [next entry](#helper-acquisition-overlap-follow-through) overlaps it. FHSS
  template-bank preparation stays near 8 s at 85% busy. Its FIR transforms are half padding, because 16,384 input rows plus
  682 taps round up to 32,768; another chunk size changes rounding and needs
  arithmetic qualification. In the current quiet row-4 search the transform's
  butterflies are about 49% of thread samples; the spectral product, the
  scalar score loop and arrival selection are about 18%, 14% and 12%.

The 28 final receipts are in ignored `local-evidence/receiver-overlap/` to
support this table and the identity claim. Retire them when a refreshed local
calibration or an accepted follow-up measurement supersedes them. The baseline
binary is not retained; rebuild it from its commit.

## Helper acquisition overlap follow-through

**Question.** Helper acquisition ran on one thread before the coarse stage. Can
it use the slot's lanes and run beside coarse preparation without changing a
result, and what does that cost in memory?

Baseline is the build of the [previous entry](#receiver-overlap-and-transform-order-follow-through)
(`triad_receiver_probe` SHA-256 `01693f3f…d951d4`); the candidate is this change
on top of it (`3110f88f…d4a026`). Host, build profile and allocation are unchanged:
Ryzen 5 PRO 5650U, twelve logical CPUs, `H=12`, `W=24`.

### What the baseline did

On row 4, acquisition took 0.36 s on one thread with every other lane idle:
frequency refinement of four targets 0.14 s, four branch filters 0.08 s, two
arbitrary-length periodograms 0.075 s, pilot fits 0.054 s and the filter plan
0.011 s. Coarse preparation then ran for 0.30 s at 85% busy.

Its workspace bound was 1,024 bytes per prefix sample plus padding: 522 MB on
rows 3 and 4, which was 87% and 98.6% of those slots' whole lease. A counting
allocator measured 36.0 MB of live scratch for the same acquisition.

### Changes

- Helper: the two pilot fits, each target's refinement and the branch filters
  run as waves on the owner's lanes, with outcomes replayed in index order.
  Refinement shares one window table. The bound is the largest step of the
  public geometry plus a 16 MiB allowance for interval lists and records.
- Coarse: a session can take its intervals from an acquisition that runs while
  it prepares, and plan its grid afterwards. The slow mapped profile keeps its
  bank in the time domain until the intervals fix its route.
- Slot and stream: a [second, wider lease](development.md#overlap-within-a-slot)
  holds acquisition scratch beside preparation. Stream admission grants it only
  from memory beyond a sequential lease for every owner the cap still admits,
  so a window that becomes ready later keeps its place and its full shape.
- Executor: a closing pool no longer starts a worker for a stage that is still
  open, and a stage publishes leaves only once their workers could be started.
  The previous entry's stage rewrite had introduced both hazards.

### Results

The set was fixed before it ran and uses the final binary. Complete slots use
`regression`, one unit per fresh process, alternating the baseline, the slot
in sequence (`--helper-overlap off`) and the overlap lease. Elapsed seconds,
median and range:

| Complete DATA slot | Looks | Baseline | In sequence | Overlap lease |
| --- | ---: | ---: | ---: | ---: |
| Row 4 | 5 | 4.114 (4.054–4.196) | 3.986 (3.953–4.069) | 3.981 (3.884–4.101) |
| Its Helper, then Helper + coarse | | 0.361, 0.927 | 0.234, 0.820 | 0.453, 0.741 |
| Row 3 | 3 | 13.033 (13.024–13.160) | 12.906 (12.857–12.919) | 12.708 (12.672–12.815) |
| Its Helper, then Helper + coarse | | 0.300, 2.060 | 0.184, 1.910 | 1.054, 1.812 |

Row 3 is 1.01× shorter in sequence and 1.03× on the overlap lease, with
ordered ranges. Row 4 is 1.03× shorter either way. Its Helper and coarse
stages together fall from 0.93 s to 0.82 s in sequence and 0.74 s overlapped,
but the overlapped stage varied between 0.70 and 0.86 s, and the two slot
medians do not separate. Three earlier sets of the same design, taken before
this entry's last corrections and not counted, had the overlap lease 0.10 to
0.12 s ahead on row 4.

| Memory | Baseline | In sequence | Overlap lease |
| --- | ---: | ---: | ---: |
| Helper bound, rows 3 and 4 | 521.6 MB | 53.0 MB | 53.0 MB |
| Row 4 slot lease | 529.0 MB | 177.9 MB | 222.7 MB |
| Row 3 slot lease | 596.7 MB | 243.2 MB | 245.0 MB |
| Row 4 message, peak reserved | 3.64 GB | | 2.09 GB |

Measured live Helper scratch is 36.0 MB at these rows' 6 kHz geometry and
4.4 MB on the 512 Hz test fixture, whose bound is 21.4 MB.

The row-4 message has three DATA slots and two unassisted quiet slots at the
default allocation, with five owners running and every slot on the overlap
lease. In two looks of alternating order its DATA commits arrived at 6.8, 10.7
and 15.4 s and at 6.5, 10.4 and 14.9 s on the baseline, and at 6.3, 10.2 and
15.0 s and at 5.9, 10.0 and 14.6 s now: 0.3 to 0.6 s earlier. Its last commit,
which waits for the quiet slots' complete coarse search, came at 120.6 and
121.0 s against 122.1 and 120.2 s: unchanged.

Controls kept their leases, scores and times: row 8, whose Helper is gated,
3.14 and 3.15 s against 3.11 and 3.14 s; row 5, with no Helper, 1.27 and
1.30 s against 1.25 and 1.27 s.

The probe's `slot` phases keep the sequential order. Their Helper phase fell
from 0.362 to 0.218 s on row 4 and from 0.297 to 0.195 s on row 3, for 7% and
14% more CPU. The coarse preparation that follows did not change in those
single looks: 0.280 and 0.285 s on row 4, 1.458 and 1.371 s on row 3.

### Identity

Every alternated look has the same input digest and the same score: decision,
bit, evidence, quiet statistics, coarse and fine work counters and Helper
status, on all three variants. The `slot` receipts of rows 3 and 4, including
the frequency-grid digest, and the message receipts are identical field for
field apart from elapsed times, workspace quotes and reservations, and the new
overlap flag.

### Functional checks

Triad 33/33, contract 35/35 and GUI 29/29 passed on the final source. A
scratch ThreadSanitizer build, which is not a maintained profile, reported
nothing in the FFT, Helper, stream, fine, coarse, native-receiver and message
tests; in overlapped and in-sequence row-4 slots and an overlapped row-3 slot;
or in a five-owner Helper message stream whose slots all overlapped.

New tests compare every Helper field exactly with and without lanes, with jobs
run in reverse order, and where a pilot fit fails on one lane; pin which steps
form each wave, including branch filters split to fit the bound; and hold
measured Helper heap inside the largest bounded step. They compare a session
that acquires during preparation with the session its intervals build
directly, on the fixed frequency route and on both routes of the slow mapped
profile, and observe that preparation was released first. They score the
Helper fixture on both leases, in sequence, at three worker shares and as two
owners of one pool; and admit stream owners together and one after another,
on either side of each memory threshold.

### Limits and unresolved work

- One host. With `W=2*H` and SMT, an overlapped serial chain shares hardware
  threads with the bank: the Helper interval grew from 0.23 to 0.45 s on row 4
  and from 0.18 to 1.05 s on row 3, where the mapped-profile gate holds the
  owner and acquisition runs without waves. This bounds the gain here.
- The gain is latency inside a slot. Total message time did not change.
- Acquisition's two periodograms remain single transforms, and several onset
  apertures still run one after another.
- A compiled backend and the exact one-arrival profile keep the sequential
  order. No accelerator ran.
- When acquisition and preparation both fail, either failure can be reported.
- Not exercised by any test: more than one onset aperture, a refinement that
  fails on a lane, a worker that cannot be started while a stage posts, and
  the GUI's stage-time label.
- The probe's `slot` and `continuation` phases, and the capacity and forecast
  tools that add them, measure the sequential lease. That bounds an overlapped
  slot from above. Local CPU calibration still needs its refresh.
- The compiled ATen test that calls the Helper directly was not built here.
- Two findings predate this change and are not fixed: the forecast tool
  expects a controller scope name the probe no longer emits, and a public
  arrival window whose onset bounds cross sizes one Helper array from a
  negative difference.

The 40 receipts named `helper-*` are in ignored
`local-evidence/receiver-overlap/` beside the previous entry's, to support
these tables and the identity claim. The baseline binary is kept with them as
`helper-baseline-triad_receiver_probe`, because the previous entry's source
has no commit of its own to rebuild it from. Retire all of them when a
refreshed local calibration or an accepted follow-up measurement supersedes
them.

## NVIDIA and Intel GPU-toggle integration

The shared C++ automatic selector tries compatible CUDA first, then a compatible
physical Vulkan device; it never reports CPU-only work as GPU execution. The
bounded hardware comparison used a full RTX PRO 6000 Blackwell Server Edition
(96 GB), on a cloud AMD EPYC 9554 host with 16 explicitly allocated logical
threads (`H=16`, `W=32`), three slot owners and an 8 GiB application-memory bound.
This is cloud-hardware evidence, not a Ryzen CPU forecast. Both public geometries
used one ray, no Helper and 26 workspaces: broad search, 1,794 frequencies ×
2,111 arrivals with FFT 4,096; and long transform, 70 frequencies × one arrival
with FFT 65,536. The configured-calendar stream included a complete five-slot
message, two scored quiet slots and reacquisition. CPU/GPU input digests matched
within every pair.

The bounded method was predeclared: capability/phase/memory checks, existing
native component checks, automatic and FP32-minimum GUI Session checks, then a
five-slot pair and one 25-slot same-stream pair per geometry. Each had a
180-second bound; invalid work or unbounded reservation stopped a stratum.
Candidate seeds were 74021/74022; fresh five-slot holdouts used 74023/74024.
The ready-input backlog interval is separately measured from the commit of
public slot 8 through slot 16 (eight commits), after all interval input is ready;
its two initial uncommitted slots are below eight, so startup carryover is
excluded. Full-stream service includes startup fill/drain and bounded spool
read/verification, whose counters are also reported separately. These are
paired descriptive measurements, not capacity passes or confidence intervals.

| Geometry | 25-slot complete stream, CPU → GPU | Ready-input interval, CPU → GPU | Fresh five-slot holdout, CPU → GPU |
| --- | ---: | ---: | ---: |
| Broad | 8.2028 → 1.3200 s (**6.21× faster**) | 2.6703 → 0.36386 s (**7.34× faster**) | 1.6653 → 0.4391 s (**3.79× faster**) |
| Long | 16.3110 → 16.4030 s (**0.56% slower**) | 5.1647 → 5.2332 s (**1.33% slower**) | 3.4278 → 3.5058 s (**2.27% slower**) |

The first all-CUDA candidate exposed many small fine dispatches. It completed
the broad backlog in 8.0367 s CPU / 2.5076 s GPU, but the long backlog took
16.5915 s CPU / 25.2777 s GPU. The 11 November 2026 timing checkpoint's
qualified follow-up used native fine work only for automatic CUDA when public
maximum block rows ≤512, projected modes ≤40, one tap, AVX2 and admitted worker
share ≥32; larger blocks, multiple taps and smaller worker shares retained CUDA
fine. This bounded stage-routing candidate improves the broad measurements
while the long case remains slightly slower. Its guard is historical and was
superseded by the current 1024-row/26-worker route recorded in
[current CUDA precision and Tensor dispatch](validation.md#current-cuda-precision-and-tensor-dispatch).
The result therefore supports the earlier scoped automatic route, not a claim
that GPU selection accelerates every geometry.

The timing candidate checks bind probe hash
`b19a5e4ddb29b3d064a2bdedf2d5932c9cb3c8ed4aa1d608af8e4194231755d` and source
manifest `e6c46ae3e600ae330ae023d49f1ae04109a2ca34840f40664f572eca89548508`.
The final CMake-only CUDA architecture-override correction changed no DSP code;
the fresh-seed holdouts bind final probe hash
`cfb636c0d665622892e8ff923a07028a726455b8cc7e9abe53b139021667a0ba` and source
manifest `95eb309f65200223774215e17bbb4ba92433cdd472c2197d7c274dbea34550d7`.
The final CUDA bundle contains six cubins for SM 8.9 and SM 12.0. CMake had
initially selected only SM 12.0 despite an `89;120` cache request; explicit
architecture overrides disabled and per-target `CUDAFLAGS` passed for both
architectures. Preserve this build failure and its correction separately from
the DSP result. Subsequent native changes add Vulkan shared-workspace admission
and reporting after the CUDA source manifest above. The later shared coarse
release-before-parallel-retain change leaves CUDA implementation unchanged; its
interaction was reviewed, but not revalidated on actual CUDA hardware. The PRO
6000 results do not cover later source revisions.

Resource receipts show the tradeoff. The CUDA device workspace limit was
99,243,065,344 bytes; peak device allocator reservation was 1,782,579,200 bytes
on the broad backlog and 681,574,400 bytes on the long backlog. GPU receiver
host peaks were 2,346,828,227 and 3,282,152,345 bytes, versus CPU peaks of
2,029,184,451 and 2,788,859,865 bytes under the same 8 GiB cap. Spool residency
was the same in each pair: 37,380,096 bytes broad and 229,109,760 bytes long.
CUDA uploaded 107,512,372 bytes / downloaded 56,516,000 bytes on broad work;
the long case transferred 2,992,899,308 / 186,502,000 bytes and synchronized
29,850 times. These transfer, workspace and host-memory costs accompany the
slightly slower long result; they are not hidden by arithmetic peak claims.

Preserved integration failures and repairs: the first CPU probe run segfaulted
because the probe dereferenced an absent CPU bulk backend while collecting a
receipt; a null-safe receipt fix passed both affected probe tests. A 1 GiB GUI
test limit failed bounded captured-input admission (peak RSS 937,516 KiB); the
shared GUI Session checks passed after using a 2 GiB test limit. The default
CMake cache initially built only SM 12.0 and was repaired as described above.
After these fixes, the native triad group passed 33/33, affected GUI/CI/package
checks 6/6, and Python orchestration passed 71 tests. Local Radeon tests with
CUDA disabled passed native Vulkan and shared GUI Session automatic/FP32-minimum
paths. The full run's `local-evidence/gpu-toggle-validation/` files retain the
original failures and source-bound measurements until the open hardware checks
are resolved; remove these temporary results after distilling those outcomes.

These measurements do not establish sustained receiver capacity, Ryzen CPU
throughput, sensitivity populations, universal GPU benefit, or Intel UHD 630
compatibility. FP16 cuFFT remains unresolved and is distinct from measured
FP16/INT8 matrix contractions. No Intel test host was available. SecureCloud
and regional deployment attempts for RTX 4090 and RTX 5090 did not
produce an instance: the 4090 capacity had expired, and deploy requests for the
other available consumer card returned no instance. There is no new consumer
GPU measurement. The tested PRO 6000 pod was terminated; no pod or persistent
volume remains. No cloud identifiers or account details are part of this
record.

## Local Radeon Vulkan shared-workspace follow-up

The automatic Vulkan workspace path was exercised on the local Ryzen 5 PRO
5650U's Radeon Graphics (`RADV RENOIR`) with `H=12`, `W=24`, one numerical
thread per receiver worker and a 6 GiB application limit. The bounded
`automatic-workspace` checks planned `F=35`, then narrowed execution to `F=1`
while retaining all 156 score rows; this held the full 3.410 GB prior lease and
observed a 328 MB device peak. A 1 MiB shared FIFO exercised
cancellation, a small successor, wakeup and explicit native fallback.

The corrected full-public-slot pair used 710 Hz input, 120 Hz data IF, a fixed
240-second active interval, two rays, 78 workspaces, 26 frequency points,
two arrivals and an 88,979-row template with a 131,072-point FFT. CPU and GPU
active and noise inputs matched within each pair. Both GPU calls completed all
4,056 public scores (`26 × 78 × 2`) and exact fine work. The active stratum
detected bit 0; the noise stratum completed as quiet. The Vulkan receipt records
nine FFT calls, 20 dispatches, and 3,410,726,688 bytes actual peak/lease against
a 4.2 GB shared-workspace cap. Summed phase wall time, excluding simulator and
profile preparation, was 8.651 s CPU / 8.533 s GPU for active and 8.783 s CPU /
7.495 s GPU for noise. This is one descriptive pair per stratum, not a sustained
capacity or statistical claim.

An earlier `Fs=480` candidate was rejected by the transmit-bandwidth/sample-clock
guard before DSP. The `Fs=960` Hz request with a 335-second active interval
stopped at the plan's template-work bound; neither is a DSP failure. The
corrected 240-second full-public-slot pair and workspace checks completed. At
the pre-label-fix candidate, the triad group passed 33/33, actual Vulkan GUI
checks passed 6/6, and 72 focused Python tests passed. The final GUI group passed 29/29; the full
GUI binary built and both CI/package checks passed (2/2). An initial
strict-validator update passed 41/41 Python tests against a fixture that gave
pinned Vulkan a positive shared cap; review then found pinned receipts may
correctly report zero. The corrected fixtures and validators pass 41/41 focused
and 72/72 combined Python checks. Rebuilt automatic and pinned GPU slot probes
each completed all 146,432 scores and detected the expected DATA bit: automatic
reports `backend_shared` with an 8,996,048-byte owner allocation/lease, while
pinned reports `per_coarse_session` with zero shared counters. The final physical
Vulkan, portable host-scoring and FIFO admission checks pass 3/3. CPU-only
LibTorch parity and GPU-unavailable checks also pass; an initial unavailable
check used the wrong command-line flag and was corrected without a source change.

The final probe source manifest is
`50a53856782b7f8176828dfa94500f195ff7f426ad282723dd12ef2494f3c455`, and its binary
SHA-256 is `b3eadc0ded2dd6c2b8e10a1c4a45777dbf8774593b3cdbf00367e04f1a76e1dd`.
The recorded full-slot timings predate the receipt-label/validator correction;
it changes reporting only, so no performance rerun was needed. The original
wrong-fixture pass remains a failed qualification step, not evidence for the
final pinned/automatic distinction. Superseded cloud upload bundles and a
duplicate result copy were removed; the source-bound observations and original
failures remain temporarily in `local-evidence/gpu-toggle-validation/` for the
current integration claims and unresolved hardware checks.

## Lower-precision and workspace investigation

The current investigation starts from `eddd12719b475e36b6d503b4965e0805f6ab7736`.
At that checkpoint the qualified AVX2 FP32 FFT was already the operating CPU
frequency search, so the initial local comparisons used automatic FP32 as the
CPU control. Later Q15 auto-selection and its control are described separately.
Historical FP64 fine-state and tight-clock failures motivate retaining those
routes; they do not prove every present native geometry needs FP64. Full-band
FIR and Helper filtering remain FP64 as a conservative choice; a frequency shift
before the Helper filter alone does not isolate its input band.

The latest integrated source manifest is
`ce949002f0c6990aca9271f34c304588eaf608cadd6483df927fd742663113bc`, with
native probe `fa1f80f8c66afe11aed5848ffcebfb8f2705f6cbf6de18a76130f6f2a15ad522`
and GUI binary `ec4e5a843cff7ec8f4af4e55c2d095ef3afd135a41739e30a85e0f717a95cea8`.
The native triad group passed **33/33** and affected checks **3/3**. The initial
integrated build stopped before tests because the default ccache directory was
read-only; the same build and checks succeeded with `CCACHE_DISABLE=1`. The
actual Radeon GUI Session checks passed for automatic GPU and FP32 minimum,
each reporting 130 actual GPU score rows, and for native-lag selection, which
decoded `e`/`001` with two physical quiet slots and zero GPU scores. Earlier
measurements below retain their respective source and binary provenance.

### CPU one-ray preparation and admission

For exact-projection, single-ray native AVX2 frequency work, the public one-column
Gram ratio is 1. The candidate converts each FP64-checked template directly to
its FP32 spectrum during bank preparation. It accounts for simultaneous FP64
projection/FP32 plans and conversion transients, then releases the former before
scoring. Two-ray conditioning, mapped, lag, bulk, tight-clock, scalar and forced
FP64 paths retain their previous bounds. The reduced lease is an admitted DSP
bound, not a measurement of RSS or a reduction in the global `W=2*H` allocation.

The local Ryzen 5 PRO 5650U uses `H=12`, `W=24`; baseline and candidate probe
source manifests were `ca22d0ce467751510fd123304e4fae7bfe2c62942109f1db14833b82e356604a`
and `508a908820b662510ca42cdf880d8a9c21364c2ee1005e522d4b455cd19e9876`, and
binary hashes were `e337eb61a5243ba3f4d22682132582248fe10ee0f22b4c56ec40299b9f8fdd39`
and `86cb3b9e38b38a9857d622fe10d17538921111fe7077f60d5fa6dca699d6ed50`.
The identical build-options hash was
`63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`.
The native group passed **33/33**; focused precision scaling, cancellation,
admission and FP64 reference checks passed.

One first-use pair and three alternating complete five-slot pairs per geometry
preserved input, scored work, DATA/quiet decisions and evidence. Narrow
service medians were 354.304→338.277 ms, 1.047× (paired 1.019–1.081×); the
1,794-frequency broad medians were 1,836.094→1,842.809 ms, 0.996× (paired
0.939–1.055×). A brief metadata hash check overlapped the broad batch, so its
timings are descriptive and do not establish an improvement or regression.
Both short fixtures retain a 162,326,981-byte slot lease because fine work
dominates, regardless of the smaller coarse quote.

The fixed long noise pair retained exact proposals, work and quiet evidence at
65,536 FFT rows, 343.2-second public bit and 516,837 captured rows. Coarse
admission fell 291,691,128→252,893,816 bytes (13.30%); complete slot admission
fell 300,542,893→261,745,581 bytes (37 MiB, 12.91%). Post-preparation retained
bytes were unchanged at 14,719,312. The single fixed-order coarse timing was
1.480→1.615 seconds; unchanged simulator and fine stages also slowed, so this
pair establishes neither a causal regression nor a speedup. The initial long
fixture expected 65,536 FFT rows but actually produced 32,768: its geometry
assertion failed. The corrected 335-second request produced the reported long
pair and passed; the original mismatch is not a passed 65,536-point test.

The subsequent CPU spectral-product investigation added AVX2 SIMD FP32 products
and a scaled Q15 complex product with FP32 inverse FFT and FP64 scoring. The
Q15 path is selected automatically only for native, exact-projection,
single-ray AVX2 frequency searches outside the compact tight-clock class and
the separately selected exact chirp-Z route; explicit
FP32 and FP64 controls remain available. Each complex product uses bounded INT32
pair sums and a FP32 inverse FFT. There is no separate Q15 FFT-size cutoff; the
checked native transform/workspace bounds remain. Unsupported explicit Q15
requests fail before DSP. Other automatic geometry retains FP32 or the existing
FP64 guards. The following initial results measured sizes through 65,536;
the later integrated 131,072-row check is reported separately below.
This is a Q15 spectral product, not an integer FFT.

For four paired opposite-bit short trials, FP64 and Q15 had matching observed
crossing brackets at 0.3125 dB-Hz resolution and Q15 passed +0.9 dB
controls. One 65,536-point/139-frequency long active case had a matching
`[-10.625, -10.0]` dB-Hz bracket and an independent quiet control. These few
trials do not establish population sensitivity or a statistical loss bound.
In a three-arm five-slot 1,794-frequency comparison, SIMD FP32 improved median
service 2.0330→1.7369 seconds (1.171×, paired 1.112–1.213×) against the previous
scalar-product implementation, with identical fine evidence and full work.
Q15 had median 1.7021 seconds against SIMD FP32, but paired ratios crossed one
(0.940–1.035×), so no reliable additional throughput gain is claimed. That
experiment binds source
`f1ca4a01e18acce669d29ab82dc6a54f6a4292f7da036eb89deff5aff512274d`.

The later source `203ff175c20b92f70f6eb2e2f0f4f87f1cce7a149e1f518270b9addb513de036`,
probe `80b6d7cf75fad14f14d466e31e66f7a9c60d3ef775298e9d29937500ae4bb8dd`,
passed **33/33** native tests plus the FFT test, with matching Q15/FP32 quiet
and near-threshold evidence and work in the long 65,536×139 geometry. The
complete-slot admission bound fell 261,747,517→254,931,773 bytes (6.5 MiB,
2.60%); three paired long-slot timings were effectively neutral, with ratios
1.044, 1.013 and 0.964. This is a bound reduction, not measured RSS or a
sustained receiver-capacity gain. A later source-bound automatic-policy comparison
(`f5461cbe9f467e127f815e315bc5b57e24b46de22052db063aef9f98c98f6757`) used
one first-use and three matched pairs per shape. Automatic Q15/FP32 control
median service was 1.4610/1.4723 seconds for the 2,048×1,794 five-slot message
and 3.4199/3.3481 seconds for a 65,536×139 quiet slot; the paired ratios varied
and do not establish a general speedup. Input, work and fine evidence matched.
The native group passed 33/33 (61.68 seconds) plus four GUI/FFT checks after an
initial plan-rejection assertion mismatch was repaired. These frequency-route
measurements predate the separately selected chirp-Z route below.

The integrated source `021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8`
and probe `be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`
also exercised Q15 versus explicit FP32 at FFT 131,072, 137 frequencies,
26 workspaces and one arrival with `W=24`. Matched inputs and complete work
included 1,016,037 captured samples and 48,769,776 fine rows per slot; evidence
matched exactly. Both noise cases were quiet, and both `-13` dB-Hz weak-active
cases remained quiet despite an expected DATA decision (log-e 20.25024 versus
threshold 21.49338). This matched miss is retained and does not establish
successful reception or a sensitivity bound. The full slot admission bound
fell 488,861,997→475,230,509 bytes (13 MiB, 2.79%). One FP32/Q15 quiet pair
took 7.4166/7.0225 seconds and one weak-active pair 7.3113/6.9669 seconds;
single pairs are descriptive, not sustained throughput evidence. A deterministic
131,072-point coherent integer-product/FFT stress check also passed.

### Exact one-arrival chirp-Z follow-through

Automatic native CPU reception can now select the existing FP64 chirp-Z
frequency scan for a no-Helper/no-bulk, exact-projection, single-ray,
single-arrival profile when its predicted radix-row work is at least fourfold
smaller than the frequency route. The existing lag-group size, group and
workspace limits still apply. All public frequency scores remain evaluated.
Explicit FP32, FP64 and Q15 policies retain the frequency-route comparator;
the admission quote covers both possible routes and supports narrower permitted
intervals without undercharging. The execution receipt identifies
`exact-one-arrival-chirp-z`.

On integrated source `021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8`
and probe `be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`,
one first-use and three alternating matched fresh-process looks used 24 workers,
1,794 frequencies, 26 workspaces and one arrival. Median complete five-slot
service was 1.44610 seconds for explicit Q15 frequency, 0.375587 for automatic
CPU chirp-Z (**3.85×** versus Q15), and 0.574771 for actual packed Radeon work.
The CPU/GPU comparison is 1.53× in favor of CPU on this geometry; it is not a
universal device ranking. Whole-process medians were 2.5320, 1.2724 and 1.5955
seconds respectively. Input, public work, fine rows, evidence, physical DATA
and two quiet decisions, and EOF controls matched. Each GPU run executed 279,864
packed score rows including EOF and no FFT. A separate 9 dB-Hz holdout matched
all three plus an explicit FP64 frequency control; one holdout is not a
population sensitivity bound. The complete slot lease was unchanged because
fine work dominated. Native group **33/33** and four affected GUI/FFT checks
passed on this integrated source. Receipt summaries in ignored
`local-evidence/exact-native-chirpz/` support this current comparison; remove
them after the claim or remaining work is superseded.

The native exact one-arrival route now supports one uniform group through the
public 65,536-point maximum, when the same cost and admission rules hold; the
mapped lag route retains its existing group limit. A one-frequency native-only
case can use an FP64 singleton dot when its modeled work is at least fourfold
lower than the frequency FFT. One matched active and one matched quiet slot on
source `f14b49ac912cfad6cddd474491f2a856554f78441ede0e1301dc575e2a7684f5`,
probe `16130273e89aff6266fecd80d15aac845c433bcea3bdc563843b33f38ca1ef19`,
preserved input, work, fine evidence and decisions against explicit Q15. The
single-pair slot timings were 0.65428→0.59878 seconds active and
0.58495→0.59487 seconds quiet, with unchanged 164,489,981-byte slot bound;
these are descriptive, not a receiver speed claim.

A valid large native active fixture at 600 Hz sample rate and 620 seconds used
93,802 projected template rows, 55,037 frequencies, 78 workspaces and one
arrival. It completed all **4,292,886** public coarse rows in four tiles,
with 78 forward and 78 inverse FFTs plus setup. Coarse preparation/search/
selection took 1.792/2.554/0.196 seconds, fine work 4.412 seconds and whole
process 10.29 seconds. The expected active bit was missed: the slot scored
quiet with log-e 4.3404 below threshold 21.4934. This establishes complete
bounded work, not successful reception or a sensitivity threshold. Its source
snapshot was `063b32c95c9813dd409fed22a7df0eb84ffa003288d6f6fa74e03dde8f970b77`;
an earlier 240 Hz/360 Hz proposed fixture exceeded the transmitter Nyquist
skirt and was plan-only, not a completed simulated reception.

An independent synthetic post-channelization check evaluated four points of a
different giant geometry (126,232 template rows, 54,718 frequencies) against
scalar long-double direct dots. Maximum absolute complex-correlation difference
was `1.47e-8`; the largest `20·log10(|candidate|/|reference|)` deviation was
`2.42e-11` dB. A first wrapper used stale source attribution and was excluded;
the corrected oracle does not establish receiver sensitivity.

Single-arrival peak compaction removes the inert second peak placeholder while
retaining every public score and the existing final candidate limits. A matched
giant plan with 126,232 template rows, 54,718 frequencies, 78 workspaces and
FFT 262,144 reduced the `W=24` complete-slot bound 517,227,936→414,795,840
bytes, saving 102,432,096 bytes (97.69 MiB). The `W=2` bound fell
393,178,560→290,746,464 bytes by the same amount. These are no-DSP admission
quotes, not measured utilization, RSS, added owners or receiver throughput;
explicit owner and worker caps remain unchanged.

The shared GUI's GPU Default setting may choose this native chirp-Z route for
the qualified local Radeon under the observed cost crossover and worker-adjusted
public-geometry guards, while retaining device initialization and the full
fallback reservation. Strict headless accelerator probes and explicit GPU
precision modes still require their requested device execution. Actual routing
is exposed to the GUI; the matched source-bound CPU/Radeon result above informs
this decision, not a general GPU ranking. The crossover scales the measured
`26×1,750×1,794 = 81,627,000` direct-term anchor by increases in estimated
native radix work over `53×4,096×12 = 2,605,056` row-stages or fewer than 24
admitted workers. The separate estimated fourfold native work advantage remains
required. An admissible 126,232-row/54,718-frequency public plan implied about
538.76 billion dense direct terms versus 0.741 billion modeled CPU row-stages;
that geometry was plan-only, and its predicted crossover is not a measured GPU
latency, utilization or receiver-capacity result.

On the final `ce949002…` source, the actual-device Vulkan suite and plan test
passed. Intermediate and giant tests checked native-preferred versus strict-GPU
planning, identical public frequencies/workspaces, and an adequate union quote
for a narrower singleton GPU alternative; giant planning did not execute DSP.
In an actual strict 1,794-frequency slot, automatic mode scored 46,644 packed
rows and 81,627,000 terms with no FFT or native-lag fallback. The explicit FP32
control scored the same 46,644 rows with 151 FFT calls; decisions and fine
evidence matched. The strict 65,670-row case also matched its prior bit and
evidence while executing 6,864 packed rows and 450,758,880 terms. Test binary
SHA-256: `d49cd705ba4e30cc0f92b6a0966be9e3a7599dae8031cb1a88f147e1202bdb55`;
fixture source SHA-256:
`161c0a7e0b28ce4ffd7164dbbbe5c17018e960d8a8c474732d114745262162f7`.

### Bounded integer and Radeon screens

On the local 5650U, AVX2, FMA and F16C are exposed; F16C converts half/single
values and is not native FP16 arithmetic. This host does not expose VNNI/AMX.
A bounded real-dot discriminator used 4,096 and 65,536 terms with coherent
midpoint and seeded uncorrelated inputs, independently normalized operands,
wider accumulation and FP64 reconstruction. Q15 normalized squared-score
errors ranged `1.77e-5`–`1.22e-4`; Q7 errors ranged `0.0218`–`0.0402`, against
the existing `3e-6` direct-score diagnostic. FP32 products with FP64 sums gave
`8.0e-9`–`7.1e-8`. Exact scalar integer sums and executed AVX2 widening/multiply
instructions were verified. An unsigned-by-signed byte multiply-add saturation
control returned 32,767 for a mathematical sum of 64,516. These are bounded
dot tests, not complete receiver decisions or throughput; they do not rule out
other scaling, residual, mixed-precision or integer formulations.

On the Radeon, two temporary native Vulkan candidates were screened against the
existing `3e-6` independent direct-score diagnostic before long transforms or
timing. The FP32 baseline passed at 16 and 256 FFT rows, maximum normalized
errors `6.18e-7` and `7.63e-7`. Packed-FP16 products executed 50 scores at 16
rows, with error `1.62065e-3`; RADV ISA showed two `v_pk_mul_f16` and one
`v_pk_add_f16`, as well as four narrowing and two widening conversions.
VkFFT half storage with FP32 computation executed 50 scores at 16 rows, with
error `2.09520e-3`; the ISA showed FP32 arithmetic and half conversions, not
packed FP16 arithmetic. The latter reduced the small-fixture device-owner peak
from 6,880 to 5,024 bytes (27.0%) and uploads from 704 to 448 bytes. The
required FP32-sized VkFFT scratch remains; the experiment did not establish a
lower stream-admission bound. Its initial setup failed with
`VKFFT_ERROR_INVALID_user_tempBuffer_too_small` before any score assertions;
the subsequent scratch-size correction produced the reported results. The
diagnostic score failures are not equivalent to measured reception sensitivity.
These first screens were followed by the targeted tests below, rather than
treated as an automatic rejection of lower precision.

For longer transforms, the first half-storage and packed-product normalization
experiment underflowed at FFT 65,536, giving error 1.0 and 1.58 respectively.
The repaired power-of-two Parseval normalization gave `0.026928` and `0.001757`
respectively versus FP32's `0.000118` for amplitude extremes `2^-80` and
`2^40`. The diagnostic score differences remain visible. Four same-noise
paired trials over 181 frequencies and both bits gave identical FP32,
half-storage and packed-product decision brackets at 0.5 dB-Hz spacing; four
quiet controls, a long-transform quiet check and failed-Helper quiet also
matched. The first Helper control was invalid because its tone overlapped DATA;
the corrected 1,725 Hz Helper at 6,000 Hz sample rate produced the intended
`no_coarse_pilot` quiet result. A 181-frequency five-slot signal plus physical
quiet and EOF control matched. These are small deterministic and paired trials,
not a population sensitivity or false-alarm guarantee.

The intermediate automatic Vulkan path used FP16 **storage** with FP32 FFT
and product arithmetic only on qualified local RADV 25.0.7 PCI `1002:1638`
hardware and 2,048-row, one-ray, 26-workspace, one-arrival searches with
181–1,794 frequencies. It normalizes using exact power-of-two energy scales and
requires representable variance. Other hardware, geometry and out-of-range
normalization report FP32 storage or the appropriate existing native fallback.
The selected precision/reason and actual per-mode score rows appear in receipts;
this is not packed-FP16 FFT computation.

Two fresh 1,794-frequency paired trials had the same FP32/automatic crossing
brackets `[6.5, 7.0]` and `[5.0, 5.5]` dB-Hz, using 26 matched active pairs
and two quiet controls. A separate 9 dB-Hz five-slot message matched DATA,
two physical quiet decisions, public work and EOF. The 0.5 dB bracket spacing
and only two fresh draws limit sensitivity inference. For the fixed broad
high-SNR message, one first-use and three alternating FP32/automatic pairs gave
service medians 1.44509→1.01680 seconds (**1.421×**; paired ratios
1.295–1.457×), and whole-process medians 2.60192→2.09178 seconds (1.244×).
The first-use pair was 1.56168→1.01526 seconds. Input and full work matched,
including 279,864 actual score/frontier rows with EOF. Peak device memory per
owner fell 21,311,408→15,790,000 bytes (25.9%); the shared-RAM admission at
the same device cap did not decrease. In a separate single 16 MiB-per-owner
pressure pair both completed; half storage used 906 versus 1,350 FFT calls and
submissions and service was 0.97461 versus 1.35172 seconds. One pair is not a
sustained capacity result. The packed-product alternative was slower than FP32
in its scoped three-arm broad trial; this does not reject a different packed FFT.

These half-storage Radeon checks bind production source manifest
`203ff175c20b92f70f6eb2e2f0f4f87f1cce7a149e1f518270b9addb513de036`, probe binary
`0b8440988da49d178bcf0c909547a83450db88229f67c7c6a64e86be30b6b2c2`, and test
binary `e0c9aa5cc6060f94c29e8a0fe3103c7e81b691c288009b119cdf42e48319e6c6`.
The compact `sensitivity-final-broad-{holdout,message}.json`,
`sensitivity-final-message-timing.json` and `sensitivity-final-pressure.json`
receipts retain these current claims. The source-bound test suite and 37 wrapper
tests passed; larger sensitivity populations and other devices remain unqualified.

### Packed direct correlation follow-through

The subsequent native Vulkan path evaluates a single-arrival correlation
directly, rather than through FFT convolution, where the measured geometry and
device qualify. The automatic local route uses packed FP16 products, wider FP32
accumulation/reduction and FP64 scoring; actual `v_pk_mul_f16`/`v_pk_fma_f16`
instructions were observed. The widened selector requires the same RADV/PCI/driver
and storage features, shader FP16 and compute-subgroup support, one ray, one
arrival and no more than 131,072 unpadded projected template rows. Frequency and
workspace totals run as independent dots in bounded tiles under native plan and
device limits; not every admitted combination has direct empirical coverage.
Power-of-two normalization must be representable. Longer eligible direct work or
unrepresentable normalization retains FP32 direct arithmetic. Other unsupported
shapes retain eligible half-storage FFT, FP32 GPU work or the existing explicit
native fallback. A faster dense dot does not establish superior FFT asymptotic
scaling.

On frozen production source
`8cfc77fe05a086a5666c4c5e3924423f622fab1e375ac233efeb4f7bbfd6b53d`, probe
`59d7bebee72ecfb373065ce048a7098059f091ee2abfefd868287da07dd7c44d`, one
first-use plus three alternating pairs of the complete five-slot 1,794-frequency
message matched full work and decisions, including 279,864 score/frontier pairs
with EOF. Compared with the earlier half-storage FFT, median receiver service
fell 1.28209→0.677818 seconds (**1.891×**, pairs 1.749–2.171×) and whole
process time 2.517→1.822 seconds (1.381×). The direct case executed 489,762,000
complex dot terms, no FFT calls and 450 rather than 906 submissions. At a 1 MiB
per-owner cap, FP32 and packed direct both completed matched work; observed
device peaks were 749,968 and 399,968 bytes, while the same-cap host admission
quote was unchanged. Device bytes and admitted workspace are distinct.

In a separate matched four-arm trial, half-storage FFT, FP32 direct,
FP16-storage/FP32 direct and packed FP16 direct completed in 1.03723, 0.57812,
0.64286 and 0.57268 seconds respectively. Packed versus FP32 direct was
essentially tied in that experiment (paired 0.975–1.078×); versus the same
half-storage input with FP32 direct it was 1.122× faster. Four fresh paired
draws with both bits over 181 and 1,794 frequencies, four quiet controls, two
five-slot 10 dB-Hz messages and weak-Helper/no-pilot checks matched decisions
and complete work. These finite fixtures do not establish a population
sensitivity or false-alarm bound. Additional tests covered a one-frequency
message; a 1,799-frequency, 78-workspace, 1,750-row case with both bits and quiet;
and a 34,470-row, 65,536-point geometry with 139 frequencies, near-threshold DATA
and quiet. For the last case matching FP32/direct decision brackets spanned
`-10.625` to `-10.0` dB-Hz; the single `-10.0` active-slot service was 4.473
seconds for FFT, 3.128 for FP32 direct and 3.048 for packed direct. These are
single observations, not repeated complete-stream throughput measurements.
The wide 1,799-frequency complete message used 841,932 score/frontier pairs;
its single-case service was 4.934/1.841/1.722 seconds for those three paths.
These extra strata support the stated envelope, not unmeasured combinations.

The widened production selector was then exercised on actual Radeon hardware:
one-frequency and 1,799-frequency/78-workspace complete messages, the
34,470-row/139-frequency near-threshold active slot, and a new
1,888-frequency/78-workspace active slot with 147,264 packed score rows and
257,712,000 dot terms. A wider 5,384-frequency case had two arrivals and
correctly fell back to the FP32 FFT, executing 839,904 score rows. The latter
is a geometry fallback, not a failed packed computation. Actual-device checks
passed 2/2. This widened-source evidence binds manifest
`149d74436ea2bc27aa7c973fd8dde90005b5386407984914bbae7fa1cd8e7811`, probe
`25bba84c61f2a9c3b8c7597a4e638021c85a4587772145495be7f921ff9e011f` and test
binary `cbeed244a3af7a9a240c024cea30bcaeb6408567840c04334c13e863dc743e48`.

A subsequent bounded long discriminator at 65,670 projected rows, FFT 131,072,
26 workspaces, 264 frequencies and one arrival used seven actual-device native
calls on the same `149d744…` source. The packed/FP32 direct controls matched
input, public work, near-threshold active decisions and separate quiet; each
evaluated 6,864 scores and 450,758,880 dot terms. Single-case service was 4.908
seconds packed, 4.940 seconds FP32 direct and 8.328 seconds FFT; device-owner
peaks were 13,695,328, 26,829,328 and 203,953,584 bytes respectively. This is
not a population sensitivity or sustained throughput claim. Across three matched
direct looks, median dot time was 0.063386 seconds packed versus 0.182378 seconds
FP32 (2.88×), combining storage and arithmetic effects; no same-storage control
was measured for this longer geometry. Median complete-slot service was
4.811 versus 4.940 seconds (1.027×). The later 131,072
row selector boundary is a conservative numerical envelope, not a hard
sensitivity boundary. Observation energy and the largest nonzero bank-column
energy determine common scales; the observation and largest column have norms
in `[64,128)`. At `K≤131,072`, the L2 bound for flushing subnormal FP16 operand
components is at most `sqrt(2K)·2^-14`, no more than one unit roundoff relative
to that largest norm. Weaker columns can have a larger relative error, and
product underflow and FP32 accumulation are separate error mechanisms. Direct
empirical near-threshold work currently extends to 65,670 rows; it does not
prove the reception sensitivity of every geometry. The integrated source
`021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8` passed
actual-device checks at the 131,072/131,073 selector boundary; probe
`be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`, test
binary `c72bdf19349f27433dbd874e01b8f95b69533974347b5127cebc879c655c1421`.
On that integrated build, the 65,670-row active case retained matched input,
decisions, evidence and 6,864 score rows with 450,758,880 actual packed dot
terms, zero FFT calls and a 13,695,328-byte device-owner peak. These checks do
not establish the direct/FFT performance crossover for every admitted F×K shape.

For the earlier fused product trial, 489,762,000 complex terms counted at eight
real operations per term give 47.0/47.6/54.5 GF/s equivalent for FP32 direct,
FP32 direct with half storage and packed direct. This is a scoped operation-rate
calculation from measured stage time, not a hardware utilization measurement.
At the advertised 1.8 GHz, the conditional seven-CU Radeon model gives 1.61
TFLOP/s FP32 or 3.23 TFLOP/s packed FP16 arithmetic peaks. The mixed direct
kernel also uses conversions, FP32 accumulation, memory operations and other
work, so the ratio to either ideal peak is not a measured utilization figure.
The present data do not distinguish memory traffic, scheduling, synchronization,
instruction mix and clock effects as the principal limit; hardware counters or
equivalent profiling are needed before assigning that cause.
See the [Radeon arithmetic ceiling](triad-performance.md#radeon-arithmetic-ceilings)
and [AMD's packed-FP16 guidance](https://gpuopen.com/learn/first-steps-implementing-fp16/).

Before adopting half storage, the FP32 Radeon path was compared with the then-current automatic CPU
on the same fixed 377,637-row, five-slot, 1,794-frequency case, 4 GiB and W=24.
One first-use pair and three alternating fresh-process pairs preserved input
digest, full work, decisions and `5e-6` evidence, with actual GPU scoring and
no coarse fallback. CPU/GPU service medians were 1.68099/1.46446 seconds
(1.148×; pairs 1.124–1.157×), while full invocation medians including startup,
simulation, profile and EOF were 2.84888/2.67355 seconds (1.066×). The first
CPU/GPU service pair was 1.4340/1.4822 seconds and full invocation was
2.4396/2.7577 seconds. Each GPU process incurred five cache misses and 25 hits;
the later pairs are not cross-process warm-cache evidence. Peak device bytes per
owner were 21,311,408; peak admitted shared-RAM reservations were approximately
1.67 GB CPU and 3.18 GB GPU. This is not an APU capacity gain or sustained
backlog test. The source manifest was
`508a908820b662510ca42cdf880d8a9c21364c2ee1005e522d4b455cd19e9876`, build
hash `1402552abf5b6fbff4e950c89404fc95182ca1e59e25f11137439237b7078f55`,
and probe binary hash `b7042b90dfce4166381e3cb335c53199d562cd549b73f2fd7e1ea8907ac8e0a1`.
The restored Radeon backend passed its actual-device frequency-grid, frontier
and native-fine correctness check.

The exact production route planner identifies a configured public broad-timing
fixture at inner rate 1, two added symbols, crystal base oscillator, `cn0=0` and
five slots. With no spread and with `hf-best` it reports `lag_groups`, 2,711
projected template rows, 213 frequencies, 71 arrivals, 78 workspaces, one
frequency group and FFT 4,096; projection is 5 Hz with source step 60 and the
100.005 ppm clock span is outside the compact FP64 class. The planned coefficient
storage is 4,619,544 bytes, within the shape bound for a declared 16 GiB arena.
At `H=12`, `W=24`, the CPU coarse bounds are 54,959,672 bytes for one ray and
61,112,504 for two rays. The five-slot fixture requires 4,057,300 and 4,057,301
samples respectively. These are no-DSP geometry results: actual accelerator
capability, dynamic allocation, matrix execution, complete costs and physical
decisions remain separate gates. Planner receipt source manifest:
`02b3d8b76c2fa68c79f70a02704b9de6e41de5a2f861ae736b7f7f54a6027829`.
Two focused planner checks passed. An initial local planner build stopped before
assertions because ccache could not write its configured read-only directory;
rebuilding the same targets with `CCACHE_DIR=/tmp/pumpmodem-native-ccache`
succeeded before those checks. This was a local setup failure, not the later
cloud compilation or a numerical test failure.

On a full NVIDIA RTX PRO 6000 Blackwell Server Edition (SM 12.0, 97,887 MiB,
driver 595.91.07, CUDA 12.8.93 and LibTorch 2.9.1+cu128), the canonical native
CUDA test passed with 35 actual FP16 and 35 actual INT8 GEMMs, zero capability
fallbacks, and the retained 30/60 dB composite Helper/DATA checks. This is
component and bounded receiver evidence for the frozen source, not proof that
the planned public lag route runs on matrix arithmetic. Probe binary SHA-256:
`3152ae5fb3262b9190d420fad532a8e507758f6cdabeb7328c663f1cf39ef0be`;
CUDA test binary SHA-256:
`415ad48f4640edcf4106d8478fb37239340ea9d380aa4bd9e930981ca264fb02`;
build-options hash:
`156c8e3becf263a3e99647ce07a50fd644336e76b642a4c3c227851694371cf7`.

The configured-calendar lag fixture's CUDA FFT control failed the existing
native mapped-template projector fidelity gate before coarse search at 1,500 Hz
input sampling. A bounded 6,000 Hz discriminator retained the planned
K=2,711, F=213, A=71 and 78-workspace geometry but failed the same guard.
Both receipts report `executed=false`, zero GPU score rows and zero matrix
calls. FP16/INT8 lag comparison was therefore not admitted; these failures do
not establish matrix precision, sensitivity, performance or complete-service
behavior. The experimental pod was terminated with no remaining pod or
persistent storage.

The gate's failure was traced to cubic mapped-template interpolation near the
finite source endpoints. A bounded exact FIR edge-residual correction preserved
the existing fidelity thresholds: one representative previously failing guard's
projector error fell `0.01377818→9.828e-8`, relative template L2
`0.01384220→1.032e-7` and Gram error `0.00284682→8.747e-8`. Focused native
tests passed and CPU active/quiet controls each completed all 1,179,594 public
lag scores, with the active input identical to the failing cloud capture. The
corrected coarse lease is 55,155,704 bytes, while the complete slot remains
177,034,256 bytes because fine work dominates. The new quote includes both edge
scratch and other workspace-layout changes; do not attribute the entire delta
from the earlier planner quote to the edge correction alone.

A second bounded actual RTX PRO 6000 Blackwell run used corrected source
`8cfc77fe05a086a5666c4c5e3924423f622fab1e375ac233efeb4f7bbfd6b53d` and probe
`551bc0e91825baab9388cf5b587448a6bd9e3c376ddfa1885e26493869b17a4a`.
The native CUDA suite passed again. One active and one quiet slot each for
FFT, FP16 and INT8 completed all 1,179,594 public scores at `H=30`, `W=60`;
each matrix case executed 36 GEMMs with no capability fallback. All expected
DATA/quiet outcomes matched. FP16 active/quiet and INT8 active proposals matched
the FFT control; INT8 quiet changed proposal coordinates and added 13,500 fine
rows while retaining quiet and a `1.8e-15` log-e difference.

| Complete coarse phase, including preparation and selection | Active | Quiet |
| --- | ---: | ---: |
| FFT | 0.36352 s | 0.27627 s |
| FP16 matrix | 0.41407 s | 0.43040 s |
| INT8 matrix | 0.35463 s | 0.33786 s |

INT8 search tiles alone improved 1.459× active and 1.184× quiet, but its
preparation increased 0.274→0.289 seconds and 0.196→0.266 seconds respectively;
the complete coarse result shows no consistent advantage. Peak charged device
workspace rose from 35.722 MB for FFT to 1,070.772 MB FP16 and 1,068.216 MB
INT8; actual coarse-only allocator peaks were 27.263, 155.189 and 140.509 MB.
Each stratum has one matched pair per mode, with variable fine-stage and process
times; it is not a receiver throughput estimate or sensitivity bracket. The
predeclared stop did not expand this unfavorable affected-stage/memory result
into a five-slot CUDA campaign. This does not reject other matrix geometries or
the separately documented historical component wins. Both cloud pods were
terminated without retained volumes.

The scoped CPU comparison receipts in ignored `local-evidence/lower-precision-cpu/`,
Radeon screen/paired receipts in `local-evidence/radeon-precision-screen/`, and
sanitized native CUDA test and projector-failure receipts in
`local-evidence/lower-precision-cuda/` support this current investigation; remove
them after their findings and any remaining qualification are resolved or
superseded. No general CUDA lag speedup, sensitivity probability, complete
capacity or refreshed CPU forecast is claimed.

## Native arithmetic selection

The earlier working change from `eaa0b805beaf788973da252dce03bb565e9f85be`
selected post-filter FP32 frequency FFTs on AVX2, including live/simulated GUI
reception. Later automatic spectral-product selection is described above.
Every template Gram must have minimum/maximum eigenvalue ratio at least `1/16`.
This is an empirical qualification envelope, not a universal roundoff theorem.
Exact power-of-two block scaling preserves tiny inputs and noise-normalized
scores. Full-band filtering, coordinates, Gram/quadratic scoring and fine state
retain double precision. Unqualified CPU kernels, lag groups, compact tight-clock
geometry and ill-conditioned banks retain FP64. The original full hypotheses,
`W=2*H`, conservative memory admission and physical completion remain intact.
See [the operating policy](search-arithmetic.md).

On the Ryzen 5 PRO 5650U, Release/GCC 14.2, `H=12`, `W=24`, the frozen narrow/wide
coarse comparison used one separate first-use pair and three alternating pairs,
under a 120-second cap per geometry. Both policies use identical input fingerprint
`10788907945148184106`, complete search work, exact proposal coordinates/order
and the unchanged `3e-6` score gate. FP64 retains its original combined bank
preparation; FP32 includes scaling, conversion and its extra preparation dispatch.

| Complete coarse phase | FP64 median | Automatic FP32 median | Speedup | Paired ratio range |
| --- | ---: | ---: | ---: | ---: |
| 78 workspaces, 22 frequencies, FFT 2048 | 86.948 ms | 86.714 ms | 1.003× | 1.003–1.099× |
| 78 workspaces, 211 frequencies, FFT 4096 | 517.336 ms | 440.245 ms | 1.175× | 1.117–1.224× |

First-use FP64/FP32 times were 65.720/68.132 ms and 407.556/433.202 ms respectively;
they are not pooled with the three recurring looks. Narrow service is effectively
unchanged, and this is not a universal latency win. An earlier 1.45× wide pilot
also split FP64 preparation and could not establish the original-path gain;
the corrected comparison above supersedes that pilot.

Complete native `message` checks used the same three DATA plus two scored quiet
slots, seed 74021, 600,000-sample cap, 10-second invocation deadline, 4 GiB
allocation and 24 workers. Each geometry had one first-use pair plus three
alternating pairs. `--seconds .1 --rate 120` gives 10.4-second public bits; the
capture has 377,637 rows. The unchanged input, work, decisions, EOF negative
control and `5e-6` evidence gates passed. Timings include receiver preparation,
search, fine work, ordered completion and bounded verified spool reads; simulator
generation and one-time profile/audio preparation are reported separately.

| Complete five-slot service | FP64 median | FP32 median | Speedup | Paired ratio range |
| --- | ---: | ---: | ---: | ---: |
| Exact shift oscillator, 1 frequency | 367.740 ms | 361.300 ms | 1.018× | 0.985–1.031× |
| Crystal shift, RF 20 kHz, 181 frequencies | 535.864 ms | 542.943 ms | 0.987× | 0.948–1.032× |
| Crystal shift, RF 200 kHz, 1,794 frequencies | 2,002.082 ms | 1,673.750 ms | 1.196× | 1.184–1.244× |

After the first two neutral results (20.61 seconds total), one additional
search-dominant stratum was declared with the same gates and 120-second cap.
It finished in 23.91 seconds and all five automatic slots actually used FP32
frequency search. Its first-use pair was 1,951.291/1,604.695 ms. No further strata
were sampled. Three pairs are descriptive evidence, not a sensitivity probability,
sustained backlog test, manual-case capacity pass or refreshed CPU forecast.

Numerical checks cover independent FFT/DFT agreement, scalar/AVX2 agreement,
65,536-point round trips, unchanged full search/proposals, `2^-100` input scaling,
near-decision DATA on both sides, strong Helper and failed-Helper quiet. A fixed
1.5 ms two-ray delay with 0.024 Hz Doppler has minimum Gram ratio 0.0826204 and
passes FP32/FP64 work, exact proposal and score gates; 0.020 Hz gives 0.061346 and
selects FP64 just below the cutoff. The final expanded coarse check passes.
The native group passed **33/33** (52.13 seconds), GUI **29/29** (146.31 seconds),
then the added shared profile-policy check passed. Native FFT checks also pass.
The rebuilt actual Radeon passes its direct-grid, exact-frontier, near-decision
and quiet tests. Optional CUDA algorithms are unchanged; their native comparison
fixtures explicitly retain the FP64 oracle. No new CUDA-device run is claimed.

Radeon profiling bounded quadratic FP64 scoring to at most **0.37%** of complete
coarse time, below the predeclared 1% screen. A structured product dispatch passed
accuracy but slowed the wide warm mean from 439.63 to 502.59 ms; it and the
temporary instrumentation were removed. CPU controls also varied, so those
descriptive timings do not isolate a shader-only causal gain. The retained GPU
bulk path is FP32. This device has no accelerated INT8 dot path. Packed FP16 or
FP16 storage remain distinct unqualified alternatives: VkFFT half storage still
computes in FP32, but could independently affect memory traffic.

The original aggregate-initializer compilation failure was repaired by preserving
the `Workspace` field order. An initial ill-conditioning test retained Doppler
that kept its two templates independent; correcting the fixture exposed the
intended FP64 guard without relaxing a gate.

Final production source manifest:
`ca22d0ce467751510fd123304e4fae7bfe2c62942109f1db14833b82e356604a`.
Build-options hash:
`63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`.
Native probe binary:
`e337eb61a5243ba3f4d22682132582248fe10ee0f22b4c56ec40299b9f8fdd39`.
The final paired receipts in ignored `local-evidence/native-arithmetic/` support
this source-bound claim; remove them when it is superseded. Superseded pilots,
the experimental baseline executable, profiling captures and temporary diagnostics
have been removed. Existing build caches remain useful. The local GUI binary has
been rebuilt with the new automatic policy and optional Vulkan support.

## Shared GUI GPU selection

The working change from `81372e34a220f67d44b5d1b67ae03ae3bf1456e8` adds the
shared **GPU** checkbox immediately left of Developer, visible independently
of Developer mode. CPU remains the default. The saved `--gpu yes|no` preference
selects the existing native Vulkan backend for live reception and simulation;
requested device failures remain visible. The default/minimum desktop widths
grow by 64 pixels to retain the existing header's readable mode/status width.

The backend/cache belongs to one session settings generation. Device creation
and teardown stay on the manager, after capture/receiver owners drain; polling
copies only a bounded work receipt. Suspension/resumption clears old-generation
work counters. The full worker budget and DSP memory admission are unchanged.

CPU-only GUI checks passed 27/28 initially; the sole failure was the old exact
message-editor width assertion after widening the header. Updated geometry,
launch round-trip and native GUI policy checks then passed 3/3. The rebuilt
Vulkan-enabled GUI passed 10/10 affected shared checks, including CPU reception,
Fast/Legacy preservation and explicit missing-device failure/recovery. The
alternate-frontend group passed 8/8, including framebuffer and terminal views.
FLTK adapter and document conformance passed in a private Xvfb display. The
restricted invocation could not connect to its display; the host invocation
passed without changing the user's desktop. Visual inspection of the rebuilt
window confirmed a checked GPU control directly left of unchecked Developer
mode, with the actual Radeon device reported as ready.

On the actual `AMD Radeon Graphics (RADV RENOIR)` device,
`build/dev/test_gui_robust_live --gpu` decoded `e`/`001` through the GUI's native
Session path, with all three DATA and two physical quiet decisions and **130
actual GPU score rows**. GPU off/on and suspension/resumption passed. This
bounded functional fixture covers at most five scoring windows and 600,000
samples, with a 90-second receive limit. It is not a new speedup, capacity,
all-audio-device or release claim. The local `build/dev/datapump-gui` includes
Vulkan support; the portable build option still defaults off.

## Local Radeon Vulkan backend

Work starts from clean `9606771b10b813d6c7d64ecd6dae4d43329e5466`. The local
Ryzen 5 PRO 5650U exposes its Cezanne GPU, PCI `1002:1638`, through amdgpu and
Mesa `25.0.7-2+deb13u1` as `AMD Radeon Graphics (RADV RENOIR)`, Vulkan device API
1.4.305. The normal restricted sandbox hides the render node; authorized host
execution enumerates the actual integrated GPU without driver or permission
changes. A separate `llvmpipe` software device is excluded from GPU qualification.
The installed OpenCL loader exposes zero platforms.

The device reports FP16 and FP64 shader arithmetic, 8/16-bit buffer storage,
64-lane subgroups and four compute-only queues. Its integer-dot extension reports
no accelerated 8-bit or packed-4×8 variants. Host-visible/coherent/device-local
memory is shared physical RAM, not additional dedicated VRAM or demonstrated
zero-copy throughput. The kernel advertises a maximum 1,800 MHz graphics clock.
These are capability observations, not measured arithmetic rates.

The optional native Vulkan implementation targets filtered DATA frequency FFTs,
scoring and exact peak frontiers while retaining native FIR, Helper, template
generation, lag/tight-clock routes and fine processing. The public route chooser,
full worker budget, search coverage and scored completion contract remain in
force. See [scope and tests](triad-performance.md#local-radeon-vulkan-processing),
[precision boundaries](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics)
and [the conditional hardware model](triad-performance.md#radeon-arithmetic-ceilings).
The final cached-program revision passes the independent frequency-grid oracle
(`3e-6`), exact arrival-frontier cases and native-fine receiver comparisons
(`5e-6`), including near-decision and quiet observations. Memory checks cover
simultaneous owner charges, retained cache admission with no active owner,
one-byte-short rejection and cache reuse. Correctness and both timing cases bind
native source `902288108c1d89298cfddfbc94d8bec07d1f9e05df9923b16c7a981fef2737ba`,
build options `1402552abf5b6fbff4e950c89404fc95182ca1e59e25f11137439237b7078f55`
and fixture `cda15a9bc5449d605cefbe47aba0ea93337f194ab4b3bff2f083ea8413bad7fa`.
The runtime is GNU 14.2 Release, VkFFT 1.3.4 and glslang 15.2.0, with native
`H=12/W=24`. The resulting `test_triad_vulkan` binary SHA-256 is
`1c464d5a12c56556b4bb24de58818914ab30a62da1121cdf5ca2c6970be4bda7` and
`triad_receiver_probe` is
`221477e33705adc2f87a5dc1e34b27e932e6bdd6a91d54a2acbb58485df9e289`.
The required native CPU group passes 32/32 in 28.28 seconds. The actual Python
`NativeProbe` wrapper also completes a Vulkan regression (four dispatches,
26 score rows) and a configured-calendar message (24 dispatches, 156 score
rows), both at `W=24` with actual GPU work and cache bounds checked. These
integration checks do not extend the timing result to complete receiver capacity.

Earlier test-only failures are preserved:
the full `H=12/W=24` plan has one complete tile rather than the fixture's assumed
multiple tiles; native lag preflight conservatively retains a device quote; a
fine-memory check used a coarse-only observation window; and a simultaneous-owner
check confused shared aggregate receipt counters with per-owner allocation.
Those corrections did not change GPU arithmetic or numerical tolerances.

The initial uncached small case at source `b1b000b6868a4ea92f1996c5be5768ca68f4e2f662eead862e0d49e75dc8a27c`
was about 2.5× slower on Vulkan. Increasing batch
width to 64, reusing work buffers and using direction-specific plans reduced
wide-case submissions from 100 to 28 but did not by itself improve the complete
phase. Its actual peak grew from 31.1 to 207.7 MB. Caching serialized FFT programs
then removed repeated compilation while keeping device plans and buffers private
to each owner. The cache admits at most eight entries/8 MiB of payload plus a
64 KiB metadata reservation, charged for the backend's entire lifetime.

Both final cases retain the identical 8,565-sample input, fingerprint
`10788907945148184106`, all 78 workspaces and every public hypothesis. The wider
interval was declared separately after retaining the original result. Each case
uses one separate first-use pair followed by three alternating CPU/Vulkan pairs,
with a 120-second cap. Means below include preparation, search, selection and
owner teardown, but exclude separately measured backend creation.

| Public frequency interval | Frequencies / arrivals / FFT rows / tiles | CPU mean | Vulkan mean | CPU/Vulkan speedup; paired range |
| --- | --- | --- | --- | --- |
| `[-0.2,0.2]` Hz | 22 / 25 / 2,048 / 1 | 77.117 ms | 107.629 ms | **0.717×**; 0.701–0.737× |
| `[-2,2]` Hz | 211 / 31 / 4,096 / 9 | 524.061 ms | 412.809 ms | **1.269×**; 1.213–1.331× |

These are descriptive measurements, not confidence bounds. The first-use pairs
were CPU/Vulkan 72.320/213.012 ms for the narrow case and 405.327/610.745 ms for
the wide case; backend creation added 28–32 ms separately. Every measured warm
Vulkan look has zero program-cache misses: four hits and 327,312 payload bytes
for narrow, seven hits and 607,996 bytes for wide. Wide FFT plan initialization
averages 7.149 ms, shader compilation 11.907 ms and submitted-work wall time
269.081 ms, with 28 synchronizations and 63 shader dispatches. These nested
diagnostic timings must not be added again to complete coarse time.

Actual per-owner device peaks are 95,544,976 bytes (91.1 MiB) narrow and
207,686,448 bytes (198.1 MiB) wide. Admission conservatively charges the full
configured 256 MiB device ceiling plus host staging/metadata and the persistent
cache; the APU consumes shared host RAM. Generic allocation counters aggregate
owners, while `vulkan_peak_owner_bytes` reports the per-owner peak.

The wide result is a measured benefit for this complete coarse geometry, with
remaining CPU preparation included. It does not establish whole-receiver
speedup, sustained backlog capacity, GPU utilization, packed-FP16 arithmetic or
a CPU forecast. The narrow result and cold costs remain material limitations.
The default native CPU path and full worker budget remain unchanged; the shared
GUI now exposes the optional backend through its GPU checkbox. The
[arithmetic model](triad-performance.md#radeon-arithmetic-ceilings)
separates ideal peaks from the current measured stage bounds.

Temporary `local-evidence/amd-igpu/` receipts serve the current exact-device
qualification and source-bound speedup claim. Keep only the capability,
original failure and final measurement evidence needed for those claims; remove
transient shader copies, diagnostic builds and redundant logs when their purpose
ends. The reusable verified dependency cache serves the current optional build.

## Representative receiver hardware checks

The [target contract](triad-performance.md#representative-receiver-hardware)
covers RTX 4090 desktop, RTX 4090 Laptop, RTX 4060 Ti 16 GB, Ryzen 5 PRO 5650U
and Ryzen 7 8840U CPU/integrated-graphics systems. The PRO 6000 remains the
development preference. Hardware-specific checks must bind actual device,
memory, runtime and executed arithmetic; a target profile or compatible build
does not establish performance on that device. At this earlier checkpoint the
project had no AMD GPU backend. Its Ryzen CPU execution is CPU evidence, not
integrated-GPU qualification; the later Vulkan work has a separate entry above.

The native component mode and opt-in CUDA workflow now bind compiled
production/build/fixture identity, actual device/runtime, fixed inputs, complete
work and executed arithmetic. They distinguish matrix comparisons from full FFT
peak reduction and from relative receiver regression. The local Ryzen 5 PRO
5650U uses the reported native `avx2` FFT path with H=12/W=24. At compiled source
`99d21d1b61225bf8d988039029bd13d9a809a07e683ab9a072df130c74f0a3a6`,
the ordinary native group passes 32/32 in 30.71 s, the focused probe check passes
1/1 in 5.46 s, and optional CPU ATen passes 1/1 in 11.67 s. The wrapper's 32,
workflow's 18 and source-bundle's nine unit tests pass. A CPU-only component
request reports unavailable after one describe call and zero component
measurements. These checks do not qualify integrated graphics or receiver
capacity.

Initial Ada validation on an RTX 4090 (SM 8.9, CUDA 12.8, LibTorch 2.9.1) failed
the unchanged full-CUDA mapping oracle before component timings. It reported
zero differing unrotated and 645 differing rotated float components against
the installed ATen expression. The compiled source is `99d21d1b…` and fixture
source is `d916f8548659cf1fa0c5c09be4ed363d5d880cf3a8bc3fcd4f364fe37e8fe0f9`.
One bounded discriminator isolated final complex-product FMA association:
unrotated values, cycles, angle, sine/cosine and reconstructed reference were
bit-exact, while the installed expression associated the imaginary sum
differently from the fused mapper. The largest absolute mismatch was
`5.960464477539e-8`. This reproduces the old arithmetic-contract failure; it is
not a claim that a tolerance was met or that the installed oracle passed.

The separately reviewed repair declares
[`fp32-rn4-v1`](gpu-precision-controls.md#portable-fp32-mapping-arithmetic), with
four individually rounded products followed by rounded subtraction/addition,
for both the fused mapper and its native reference. FP64 is unchanged. At
source `ed59216a5d53cb6db6c7f05e2861e230dbdb7efbc74bf1ed9e25eb808725b22b`,
the ordinary native group passes 32/32 in 30.24 s and CPU ATen passes 1/1 in
13.24 s. The actual CUDA suite then failed the newly added composite
unit-rotation/negative-phase golden before performance measurements. A bounded
discriminator found the fixture had assumed equal rounded sine/cosine: actual
cosine was `0x3f3504f3` and sine `0x3f3504f4`; both mappers agreed on the result
`-2^-25 + i`. The corrected composite fixture uses exact zero-angle and
negative-integer wrap. Six independent multiplication goldens and the 139×17
exact mapper comparison remain unchanged. Review passed; the corrected fixture
source is `7d12a9d779d08189f909129e521d75afade872897752f4beedccfd3186c247bd`,
with production source `ed59216a…` unchanged. The corrected full CUDA suite
passes in 17.114 s, including exact mapping/arithmetic goldens, native receiver
comparisons, near-decision/quiet checks and +30/+60 dB Helper composites. Its
matrix fixtures execute 35 FP16 and 35 INT8 GEMMs with zero capability
fallbacks. The original mapper comparison and receiver tolerances remain
unchanged; the reference now follows the explicit arithmetic revision. A
separate launch attempt could not find `/usr/bin/time` and executed no test
assertions; the successful run used the shell timing facility.

The maintained component command then passes all three selected comparisons
in five native calls and 6.439 s total, including process/control overhead,
within its original 300-second budget. It uses matrix case zero and the fixed
peak shape with an exact 2,048 MiB workspace. The runtime is GNU 13.3 Release,
CUDA 12.8, cuBLAS 12.8.4 and LibTorch 2.9.1 on the actual RTX 4090, SM 8.9.
Build identity is
`05ae3890dd37ccd47a679f023b76b84d4c1502997fda2b9be44ec8a6fbd9390d`;
the source/fixture above and `fp32-rn4-v1` bind both binaries and all calls.

| Native component | Reference → candidate fresh-owner geometric mean | Candidate/reference ratio, descriptive 95% interval |
| --- | --- | --- |
| FP16 lag matrix versus FFT; K=769, F=129, A=65, W=33, two taps | 24.201 → 6.875 ms (3.52×) | 0.2841 [0.2152, 0.3751] |
| INT8 lag matrix versus FFT; same geometry and input | 24.259 → 6.939 ms (3.50×) | 0.2860 [0.2126, 0.3849] |
| Exact CUDA frontier versus full-score readback/host peak selection; K=8989, F=32, A=10553, W=78, FFT=32768 | 346.219 → 40.172 ms (8.62×) | 0.1160 [0.0336, 0.4011] |

Each comparison uses exactly three alternating pairs. Fresh-owner timing
includes preparation and first search on an initialized device, excluding
teardown. Resident lag ratios are 0.1193 for both matrix modes; peak search-only
ratio is 0.0818. Matrix batches reach 4,096 rows, retain the full score grid,
and report zero fallback, nonfinite or saturated values. Maximum normalized
score error is `4.8082e-6` for FP16 and `2.4454e-4` for INT8, within the unchanged
gates. The frontier comparison computes all 26,340,288 scores and preserves exact peak
semantics. These are same-binary component observations, not a relative-commit
regression result, a full 8,596-frequency receiver replay or sustained service.
The test records executed FP16/INT8 cuBLAS products; it did not collect
instruction-level Tensor Core evidence or utilization. It does not select a new
default, supply a CPU forecast or qualify real-time receiver capacity.

Existing PRO 6000 measurements below retain their original source and hardware
scope; they do not override either Ada failure or qualify the revised automatic
fine arithmetic. RTX 4090 Laptop, RTX 4060 Ti 16 GB
and Ryzen 7 8840U remain unmeasured, and no Tensor Core utilization or complete
capacity pass is claimed.

Temporary receipts in `local-evidence/representative-hardware/` retain both
original failures, the setup failure, source/binary/fixture identities and raw
checks supporting these current claims. Remove redundant build logs, transient
diagnostic copies and superseded outputs after cleanup; retain necessary claim
and failure evidence until it is superseded. The temporary cloud instance was
stopped and terminated; the refreshed provider view confirms no remaining
compute, storage or endpoint resources and no ongoing hourly charge. No
persistent volume was created. Provider/account records stay outside the checkout.

## Native matrix and preparation follow-through

The historical audit records dispositions for 21 technique groups:
[eleven GPU groups](triad-gpu-optimization.md#native-disposition-of-the-historical-techniques)
and [ten CPU groups](triad-performance.md#earlier-cpu-implementations-worth-reconsidering).
The canonical C++/CUDA implementation adds optional post-channelization FP16
and INT8 lag matrices, bounded workspace/arrival batching, exact
descriptor reuse, shared Gaussian transition preparation and an exact CUDA
arrival-peak frontier, plus a guarded automatic half-cuFFT arithmetic route for
qualifying broad searches. The FFT remains the search algorithm; the separately
bounded dense lag-matrix candidates do not replace it automatically while the
INT8 rule was being qualified at this earlier checkpoint; the later bounded
automatic candidate result appears in
[current CUDA precision and Tensor dispatch](validation.md#current-cuda-precision-and-tensor-dispatch).
Implementation does not
transfer historical performance or qualify complete-receiver capacity; see the
[precision contract](gpu-precision-controls.md#stage-boundaries-and-optional-matrix-processing).

The ordinary baseline triad group passed 32/32. The pre-tail-repair checkpoint passes:
triad 32/32 in 31.52 s, optional ATen CPU 1/1 in 13.75 s, and wrappers 41/41.
Its actual GPU compiled source manifest is
`bbcf0d9e0063fe71213034041d3883e292e4d9d2c9ee83f8ef4e6372fbc891b6`.
It passes both exact-frontier fixtures and the full CUDA suite, including
composite Helper/DATA at +30/+60 dB, the unchanged 128 MiB bound, and repeated
fresh fine-owner allocation stress. Independent source review passed. These
functional checks do not establish complete-receiver capacity.

The bounded CPU preparation comparison used the native production receiver,
GNU 14.2 Release, H=12/W=24/O=24, one numerical thread per worker and an 8 GiB
bound. It used seed 74021, three alternating baseline/candidate pairs per
active, quiet/noise and erasure stratum, with a 30-second per-call and
180-second aggregate cap. Exactly 18 calls completed in about 5.12 s. The
compiled source manifests were `3aaf0fbfeb9f311ebe66b640b9aa2ae3f3caf8b4c0164d99caad199d000c5847`
and `8e48e4da565ef37c0f66124b6194af25c390993d0367a076a4a23c7abe8ad668`.
This is a phase comparison, not a sustained backlog or capacity test.

| Fine service stratum | Candidate/baseline geometric mean | Paired log-ratio 95% t interval |
| --- | --- | --- |
| Active | 0.912935 | 0.641551–1.299118 |
| Quiet/noise | 1.014148 | 0.792482–1.297817 |
| Erasure | 0.883256 | 0.630811–1.236727 |

All intervals cross one; **no CPU speedup is demonstrated**. The first active
and noise pairs stopped on an exact receipt mismatch: coarse retained-memory
metadata increased by 464 bytes. Inspection classified only that fixed metadata
delta, retaining the original failure. Reusing the ten original calls and
executing only the eight remaining calls preserved the sample budget. DSP
inputs, state, scores and work then matched exactly; erasure needed no metadata
exception. Three-pair intervals are descriptive and do not establish a portable
CPU cost or complete-receiver forecast.

On a full RTX PRO 6000 Blackwell Server Edition (SM120, CUDA runtime 12.8,
cuBLAS 12.8.4), FP16 and INT8 each passed the bounded matrix fixtures with 35
actual GEMMs and no capability fallback. The post-channelization `CoarseBulk`
benchmark then measured three alternating pairs for each arithmetic and shape.
Each batch reached 4,096 matrix rows, made 19 deterministic direct-DFT checks, and
retained every public score. These are initialized-device fresh-owner costs:
owner creation, preparation and first lag calculation are included; process/CUDA
startup and owner teardown are excluded. Resident timings reuse immutable input
and do not measure fresh live reception. The table uses only the final three
pairs per arithmetic/shape bound to the `bbcf0d9e…` checkpoint and recorded
test/probe binary hashes; earlier pre-removal timings remain separate provenance.
DFT check coordinates may repeat;
their count is not a number of independent statistical trials.

| Shape (samples / frequencies / arrivals / workspaces; two taps) | Arithmetic | Fresh-owner geometric mean, FFT → candidate |
| --- | --- | --- |
| 769 / 129 / 65 / 33 | FP16 | 11.885 → 3.824 ms |
| 769 / 129 / 65 / 33 | INT8 | 11.858 → 3.681 ms |
| 2,048 / 65 / 64 / 32 | FP16 | 8.687 → 3.445 ms |
| 2,048 / 65 / 64 / 32 | INT8 | 9.068 → 3.283 ms |

The second shape initially failed 4,096-row batch admission at 1 GiB. Its scoped
2 GiB rerun completed all three pairs; this does not revise the portable memory
default or erase the original failure. Maximum normalized active-score errors
were `4.80022e-6` for FP16 and `2.44530e-4` for INT8 across these two shapes.
The traces record `cutlass_80_tensorop_i16832gemm_s8` INT8 and
`cutlass_80_wmma_tensorop_s161616gemm_f16` FP16 kernel launches.
The hardware-counter request failed with `ERR_NVGPUCTRPERM`; instruction counts,
occupancy and high utilization are not established by these receipts. Bulk cost
improvements do not establish complete-receiver speed or CPU capacity.

The new FP4 implementation was removed after failing its unchanged numerical
gates. Single-product E2M1/block-16 arithmetic failed the 1% normalized score
error gate with `0.050318` (about 5.03%). A high/residual repair using actual
decoded residuals and all HH/HL/LH/LL products improved that error but changed
the unique noise peak at K=769/F=129; a later 16× residual-scale repair still
selected 1803 instead of oracle 4536. The oracle's relative runner-up gap was
0.004041, while normalized maximum error was 0.003461: passing the aggregate
1% error screen did not preserve the required peak. Four FP32-accumulating
products with FP64 reconstruction and doubled packing were fully charged.
Three-pair active fresh-owner means were 4.482 ms versus FFT 12.183 ms for the
769-sample shape, and 3.534 ms versus 8.480 ms for the 2,048-sample shape. Those
favorable active costs do not fix the noise failure or demonstrate an advantage
over qualified INT8. There is no current FP4 option. The successful historical
NVFP4 results remain applicable to their original implementation and workload.

An intervening FP4 retry is invalid
as patched-source evidence: a timestamp-preserving bundle overlay left changed
sources older than existing Ninja objects, so a refreshed source manifest did
not prove that the changed translation units rebuilt. The
[bundle extraction guidance](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
now requires timestamp-safe extraction or a fresh source/build and verified
recompilation. Preserve both the numerical failure and stale-object retry.

The first public-slot check was unresolved. Row 5 CPU active/noise
checks passed, while CUDA produced matching expected decisions but its receipt
failed with `Missing CUDA coarse arithmetic policy`. After the receipt repair,
both active and noise CPU/CUDA pairs pass numerical and physical-decision
checks, with identical input, public acquisition and fixed work. The original
forecast gate rejected the new `cuda_arrival_peak_rows` telemetry. Review found
that this was overly conservative: CPU work remains projected from unchanged
workspace/frequency pairs, scores and retained peaks/proposals. Source-bound
offline reanalysis now passes both row-5 and both wide pairs, including strict
work comparison and exact coarse-search/selection CPU demand equality. It adds
no DSP calls or calibration and preserves the original failed summaries.
Row 4 CPU active/noise failed with
`Robust likelihood block cannot resolve DATA and Helper bands`, preventing the
paired CUDA checks; a separate original-baseline active replay reproduces the
same error, establishing that it predates these changes. An explicitly modified
wide-unassisted row 4 used the full
8,596-frequency `frequency_tiles` search: GPU-host CPU active/noise calls took
27.437/27.651 s, while each CUDA call reached its 30-second native deadline.
No same-input public lag-group pair admitted the optional matrix candidates.
These failures cannot be replaced by the favorable component batch results;
neither a complete-receiver pass nor sustained input-backlog capacity is shown.

The wide-search diagnosis found a concrete transfer/host-selection bottleneck:
F=8,596, L=10,553, 78 DSP workspaces and NFFT=32,768 require 7,075,659,864
logical scores, or 56,605,278,912 bytes of full FP64 score readback. The deadline-limited CUDA
call completed 132/269 tiles and downloaded about 27.815 GB while native
non-maximum suppression remained serial on the host. The new device frontier
retains the same full score calculation and peak semantics but returns at most
two peaks in 40 bytes per workspace/frequency: 26,819,520 bytes for the full
geometry. It preserves plateau midpoints, tie priority, independent endpoint
eligibility, separation and ordered publication; nonfinite values in any input
score still fail. Added core checks passed 1/1 in 4.28 s, and source reviews and
actual CUDA exact-frontier fixtures pass.

The bounded frontier benchmark used this arrival/workspace/FFT geometry with
32 frequencies, a 2 GiB bound, two warm-up scorings and three alternating timed
pairs (eight scorings total). All 26,340,288 scores, exact frontier entries and
16 fixed direct-DFT checks pass; each side performs the same 32 forward and
4,992 inverse FFTs. Fresh-owner geometric mean falls from 240.668 to 17.617 ms
(13.661×), including owner creation, preparation and search but excluding
process/CUDA startup and teardown. The descriptive paired-log-ratio 95% t
interval is 0.041642–0.128675 for candidate/baseline. Downloaded bytes fall from
210,722,304 to 99,840. This is measured component improvement; the separate
full-width check below establishes its own narrower result.

The subsequent four-call 8,596-frequency public-slot experiment passes both
active and noise CPU/CUDA pairs. Every call retains the original input/grid,
L=10,553 with 78 DSP workspaces, all 269 tiles and all 7,075,659,864 logical
scores; CUDA frontier counters confirm complete coverage. Total call elapsed times are 28.171 s CPU
versus 5.070 s CUDA for active and 26.111 s versus 4.781 s for noise. These are
one pair per stratum, not timing confidence intervals or sustained-stream
throughput. The earlier CUDA run was censored at its 30-second deadline, so it
does not provide an exact old/new GPU speedup ratio. Numerical/physical and
fixed-work gates pass. The initial forecast rejection on the new frontier
telemetry is retained; the corrected logical-work projection passes all four
recorded row-5/wide pairs. No complete forecast is calculated: the remaining
strata and operational challenges, source-matched current local CPU calibration,
independent holdouts and same-stream backlog checks are still required.
Quiet-slot message completion and local CPU capacity remain outside this check.

The cleaned full CUDA check also failed a 128 MiB bound during coarse-bank
preparation after the DC-policy fixtures, despite about 94 GiB of physical
device memory being free. The ATen 2.9.1 source investigation identified a
process-lifetime cuBLAS workspace cache of 8,519,680 bytes per handle/stream.
The repair retains direct small Grams, computes singleton DC in C128 even in
precise stages, and routes every remaining CUDA fine Gram through an owned C128
ZGEMM with a charged 4 MiB caller workspace. CPU `at::bmm` is unchanged. The original
failure remains; the repaired full CUDA suite passes the unchanged 128 MiB
fixture and repeated fresh fine-owner allocation stress. Independent review
passed. This resolves the tested Gram lifetime paths, not all possible backend
memory behavior or every configuration.

A separate public fine-only case subsequently reproduced a constructor failure
at a 13-sample tail. The initial shared public-extent merge repair passed its
targeted check 1/1 in 1.59 s. The repaired calendar merges a short public tail
into its preceding segment before samples are exposed, without dropping or
padding input. Workspace preflight validates the exact projection bins without
allocating them. Review of both changes passed. The new compiled source
`0b8165730edee9f6248f5c7fba1038a914ad454b10fea4d6e57a5ba52f280ab0`
passes ordinary triad 32/32 in 30.71 s and the full CUDA suite after a
timestamp-safe rebuild, including the unchanged 128 MiB, strong-Helper and
owner-lifetime checks. Optional ATen CPU also passes 1/1 in 13.75 s. The four-call
row-4 check passes both active/noise CPU/CUDA pairs with the original input/grid,
all 429,013 rows processed in 286 blocks/143 segments, a 1,513-row final block
and no discarded or invalid rows. Active call elapsed time
is 3.137 s CPU versus 5.088 s CUDA; noise is 27.934 s versus 6.729 s. Each pair
passes numerical, physical-decision and logical-work gates at H=27/W=54 with
8 GiB host/2 GiB device bounds. Active Helper acquisition narrows the search to
44 frequencies/two tiles. Noise finds no coarse pilot and retains all 8,596
frequencies/269 tiles and 7,075,659,864 scores; its coarse-tile phase takes
25.581 s CPU versus 2.251 s CUDA. These one-pair observations are not a portable
speed ranking.

One affected local row-4 active call also completes in 11.607 s at H=12/W=24,
O=24 and an 8 GiB bound (11.557 s named phases; 7.122 s fine work). It is a
timing observation, not calibration of all
strata, a fresh holdout or a capacity pass. Old fine-tail calibration is invalid
for the changed source; new complete forecasts still need applicable calibration
and holdouts. The `bbcf0d9e…` wide-slot and bulk measurements above retain their
pre-repair source binding. Unchanged component costs are not silently relabeled
as validation of the changed fine path.

Temporary receipts in `local-evidence/native-tensor-current/` bind this current
investigation's source, binaries, inputs and failed/continued checks. The findings
and remaining limits are distilled here. Cleanup removed 53 redundant temporary
files, including five retired orchestration scripts, empty captures, duplicate
command/hash copies and superseded successful timing/profile logs. Eleven private
source/binary/archive copies were removed; useful builds, caches and the ATen
installation remain. The temporary cloud instance and volumes were confirmed
absent; operational receipts remain outside the checkout. Hash-referenced
originals, failed gates and source-bound raw receipts supporting current claims
remain until the claim is superseded or its remaining question is resolved.
Changed matrix work is not mapped
onto the existing CPU FFT forecast; no current matrix or preparation result
qualifies complete-receiver nominal/conservative service or real-time capacity.

## Current CUDA precision and Tensor dispatch

The current C++ CUDA receiver adds guarded native half-cuFFT for broad
post-channelization frequency search on SM89 and SM120. Automatic selection
requires NFFT 4096–2^20, at least two arrivals, a double-precision
template-Gram minimum-eigenvalue ratio of at least 1/16, and
`NFFT × admitted_frequency_tile × min(W, 8) × taps ≥ 2^19`, plus successful
plan and full-workspace admission. This cost gate replaces earlier fixture-only
NFFT=131072/F≥32 caps. It scales and packs isolated spectra and
templates; FP64 scoring and persistent state remain. Before batch commitment,
a failed guard, plan or full-rectangle allocation admission selects the full
FP32 FFT route. After batch commitment, arithmetic-flag or CUDA failures fail
closed before publishing results; they do not retry with FP32 or retain an
unquantized host bank. A post-dispatch OOM remains an explicit failure. This is
cuFFT half arithmetic, not a Tensor Core FFT. Exact power-of-two energy scaling
is a checked input/range guard; it does not prove cuFFT's internal rounding
behavior.

The final bounded half-scope extension is source-bound to backend SHA-256
`5e72324c3a81ece99ed5aa91a7b2f2cc794b5fe2f7b413d2d0927f4f6a90cb45` and test
SHA-256 `60fca3b8e3ce69a36bee13e006203b181150f2bb60627c81f64110e7df6d3eaa`.
The compiled source manifest is
`53c40bf182aba4123762448b87e7c1aba654d72712461c966bf5a685973709c6`. The
RTX PRO 6000 probe binary is
`62ed656aaffb320cff75ff1fa89fd06ce34297d7baab99fe4e77946e1519a9b6`; the
RTX 4090 probe binary is
`05fd473e37704d92227f91a09b2f1cbfc0997d98c122c2c45877999b94252a3b`.
The wider transform/work-volume gate removes the previous fixture-only caps;
it is still subject to the Gram, plan, memory and fail-closed dispatch rules
above.

| Matched geometry (same inputs; complete scores) | Coarse active/noise FP32 → half | Charged workspace FP32 → half | Actual live peak FP32 → half | Decision |
| --- | ---: | ---: | ---: | --- |
| RTX PRO 6000; N=262144, K=169275, F=137, A=2, W=26; 7,124 scores | 1.73590/1.63881 → 1.60634/1.63448 s | 15.143 → 1.163 GB | 3.443 → 1.163 GB | Same bit 0 / quiet outcome |
| RTX PRO 6000; N=131072, F=8, A=2, W=26; 416 score rows | 1.59403/1.53190 → 1.53945/1.60851 s | 1.934 → 0.166 GB | 471.77 → 166.38 MB | Same paired decisions |

The automatic PRO 6000 receiver also exercised N=262144 active input across
7,124 half-FFT score rows and N=131072 noise input across 416 half-FFT score
rows. Both receipts had zero half-FFT fallbacks, matching decisions between
the FP32 and half CUDA routes on identical inputs, and complete work evidence.
Raw inputs, commands and receipts are retained under
`local-evidence/tensor-auto-policy/half-scope/`.
`local-evidence/tensor-auto-policy/half-scope/`.

The 262144 transform has mixed active/noise timing, while both geometries cut
the charged workspace substantially. These are paired observations, not a
throughput distribution or proof that half is faster on every case. A first
Fs=1500 Hz setup represented a 670-second configured signal. Its measured
phases totaled about 4.1 seconds before hitting the unchanged 30-million
raw-template-sample cap, before coarse processing. The corrected Fs=750 Hz
case retained the intended 250 Hz projected span and completed; the failed
setup remains recorded and is not benchmark evidence.

A dense N=2^20 component ran on both the RTX PRO 6000 and RTX 4090. The measured
relative RMS error was 0.0022467 on each, and relevant subnormal values were
preserved in that component. A conservative bound for worst additive subnormal
contribution at this maximum transform is about 0.14% in L2 norm. Neither this
component nor the analytical bound establishes near-threshold receiver
sensitivity or an allowable dB loss; receiver decisions and paired signal/noise
evidence remain the relevant gates.

Same-run CUPTI resource-callback capture identified the loaded half-cuFFT
symbols `vector_fft4096(__half)` and `regular_fft_factor256(__half)` in the
executed kernels. `cuobjdump` of those loaded SM120 cubins found HADD2/HFMA2/
HMUL2 instructions in both functions (97/178/72 in the vector function and
64/150/95 in the factor function, respectively), alongside minor FP32
instructions. These are static opcodes in captured executing functions, not
dynamic instruction counts, utilization, or a claim of Tensor Core execution.
Hardware counter collection remains unavailable under the provider's
`ERR_NVGPUCTRPERM` restriction; no permission bypass was attempted. The
evidence and exact commands are retained in
`local-evidence/tensor-auto-policy/cuda-half-instructions.txt`.

On a full RTX PRO 6000 Blackwell, a matched 63 MiB-admission broad coarse case
scored all 98,465,484 public scores and completed every physical quiet slot.
FP32-to-half coarse time was 340.82 to 213.36 ms (1.597x); charged workspace
fell from 54.05 to 35.53 MB. Actual allocator live peak increased from 8.45 to
16.05 MB because half admitted a larger batch. This distinction matters: the
workspace bound improved while realized allocation increased. In the broad
search receipts, half coarse totals on PRO 6000 were 0.3387/0.2883 s to
0.2691/0.2668 s (active/noise), and on RTX 4090 were 0.2053/0.2050 s to
0.1939/0.1893 s. Peak reserved memory fell about 63.4%; actual allocator peak
live fell about 32% in that different batch configuration. These are bounded
single paired observations, not a throughput distribution.

Long two-tap NFFT=131072, K=86072, F=70, A=2 retained all coarse work with a
candidate/baseline timing ratio of 0.979 (about 1.55 s, effectively neutral in
this one pair). Reserved workspace fell from 7.57 GB to 581.7 MB; actual live
peak fell from 1.72 GB to 581.6 MB. This qualifies substantial workspace
capacity for this geometry, not a long-transform speedup. In a matched
63 MiB-complete-message stream, automatic CUDA elapsed 1.79235 s FP32 versus
1.80531 s with half (0.72% slower); the predeclared ready eight-slot interval
improved 0.54196 to 0.48879 s (1.109x), and the fresh five-slot interval
0.47687 to 0.43092 s (1.107x). All physical decisions matched. The complete
stream result rules out claiming a universal whole-receiver improvement; the
interval-specific result and saved workspace remain relevant to backlog and
admission.

Sensitivity spot checks used four independent paired seeds per level and
verified identical input hashes, per-slot decisions, and physical completion.
At 6 dB-Hz both arithmetic paths failed all four expected-message cases; at
7.5 dB-Hz each succeeded on two of four. A separate INT8 near-threshold set at
−9 dB-Hz used four seeds on both the PRO 6000 and RTX 4090: FP32 and automatic
INT8 each completed one expected message and failed three. Every paired slot
decision matched on the identical input, complete work was valid, and the
receipt recorded zero unwitnessed IMMA calls. These bounded samples found no
differential decision failures; they are not a sensitivity-loss estimate or
evidence of zero dB loss. Longer messages, broader channels and independent
holdouts remain unqualified.

Preparation and Tensor dispatch also changed. Prepared right-hand operands and
cuBLASLt reduced the measured full-stage matrix cost relative to the earlier
matrix route and reduced reserved rectangles. For K=2711, F=213, A=71, W=78
(1,179,594 scores), one active/noise pair of full-stage totals
(`coarse_prepare` + every `coarse_tile` + `coarse_selection`) was: FP16
0.407/0.372 s baseline, 0.345/0.325 s with prepared B, and 0.328/0.308 s
with cuBLASLt; INT8 was 0.306/0.336 s, 0.270/0.276 s and 0.251/0.263 s,
respectively. Reserved peak rectangles fell from about 1.07 GB to 183 MB
(FP16) / 178 MB (INT8); actual allocator peak-live stayed about 121–126 MB.
This is one active/noise pair per stage and arithmetic, not a timing
distribution or whole-receiver result. The paired kernel trace reported
executed HMMA-compatible FP16 and IMMA-compatible INT8 cuBLASLt kernel families;
this is matrix-contraction evidence, not an FFT or complete-receiver result.
Same-run CUPTI capture of the cuBLASLt INT8 call identified its loaded
`cutlass_80_tensorop_i16832gemm_s8_128x64_64x6_tn_align16` SM120 cubin;
`cuobjdump` found 32 static IMMA opcodes in that 1,216-instruction function.
This proves packed integer matrix instructions exist in the captured function,
not their dynamic count or utilization. The capture is retained in
`local-evidence/tensor-auto-policy/cuda-int8-instructions.txt`. External
instruction/counter profiling is still denied as noted above. The current
automatic INT8 structural candidate uses padded matrix cost ≤30 paired-FFT row
stages, one complete grouping, one tap, K≤8192, F=8–256, at least 1664 main
complex rows, tile width ≥26 and A≥32, with shared cuBLASLt state. The F≥8 entry
is the current source condition; for low F the planner may choose a larger
frequency grouping. The 1664-row floor avoids packing overhead on undersized
contractions. A complete matrix rectangle lease is required before dispatch;
insufficient pre-commit admission retains the FFT. The accepted failure-
recovery repair fails closed after dispatch on invalid arithmetic flags or CUDA
faults before publishing a batch; there is no host unquantized retry.

Automatic INT8 receiver results support its use as a bounded coarse-speed
candidate but show mixed full-service costs. In the complete-message check, the
PRO 6000 took 8.243806 s with FP32 FFT and 8.247843 s with automatic INT8; the
RTX 4090 took 7.042574 s and 6.846154 s. In the 25-slot backlog, PRO 6000 total
time improved 38.145562→37.484921 s and its ready interval 12.63247→12.39032 s;
RTX 4090 total time regressed 34.698003→35.900528 s (3.47%), while its ready
interval improved 11.55376→11.47646 s. Summed coarse-stage time improved
4.454590→3.690055 s on PRO 6000 and 4.886528→4.398459 s on RTX 4090. The
IF450/F192 holdout improved 8.31166→8.237773 s and 7.687852→7.527696 s,
respectively. These brief measurements show a real coarse-stage benefit but no
uniform whole-stream gain; the 4090 backlog regression must remain visible.

Matrix workspace is a material cost, not a saving against FFT. With three
owners, reserved/live peaks were 533/364 MB versus 107/20 MB for FFT on the
PRO 6000, and 438/222 MB versus 107/20 MB on the RTX 4090. Therefore the
automatic route must obtain the complete rectangle lease before dispatch and
keep the FFT when admission fails. The current correctness run passed the
repaired route checks, but these samples do not establish sustained capacity or
population sensitivity. Separately, the
AVX2 CPU fine route now admits up to 1024 rows for one-tap, at-most-40-mode
geometry when at least 26 workers are admitted. In sampled 750- and 1023-row
fine clears, CPU was about 4–6x faster than the GPU. That measured route is
geometry-conditional and does not change the full `W=2*H` worker budget.

Final PRO 6000 CI passed six tests, recorded in
`local-evidence/tensor-auto-policy/final-pro-receipt.log`. On the RTX 4090,
an earlier six-test CUDA CI group passed before the final half-scope extension,
as recorded in `local-evidence/tensor-auto-policy/cuda-ci-4090.log`. For the
final source, two arithmetic checks (automatic and half-FFT) passed in
`half-scope-build-4090.log`, and three freshly relinked GUI checks passed in
`half-scope-gui-4090.log`. Two GUI test names overlap between those runs, so
there are five unique final-source tests. The build log's two GUI checks used
stale binaries and are not counted as final-source tests.
The separate `final-4090-receipt.log` records the dense half-FFT check and
binary hash, not those arithmetic/GUI suites. Earlier local triad, GUI, Python,
and bundle groups passed 33, 29, 42, and 10 checks. The final incremental
CPU-only LibTorch guard-source library, probe and test-executable build passed;
no runtime probe was run for this source increment. Earlier CPU-only LibTorch
and native probe functional checks passed, but the broader groups were not
rerun. These functional checks support the route and failure behavior; they do
not strengthen the limited timing or sensitivity sample counts above. Backend,
test, source-manifest and per-device binary hashes are bound above.

Both RunPod pods were fully terminated and verified at 21:43 UTC; neither left
a persistent volume. Cleanup removed obsolete local source bundles, copied
source and raw campaign outputs. Remote build/profile artifacts were deleted
with the pods; useful local build caches remain. Retain the matched receiver
receipts, declared strata, compact half/INT8 instruction captures and final CI
receipts only while they support current source/input matching or unresolved
near-threshold claims; delete them when those claims are superseded or resolved.
No cloud cleanup remains.

All GPU timings above include the affected search stages as specified in the
receipts; complete-stream timings include physical quiet-slot completion.
These are brief bounded cases, not sustained backlog/capacity qualification or
a population sensitivity result. The remaining
`local-evidence/tensor-auto-policy/` receipts serve source/input matching,
retained failures and instruction inspection. Remove them when their current
qualification or unresolved-claim purpose ends. Current NVIDIA dispatch distinctions and sources are
summarized in [hardware/library dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch).

## Native throughput and precision investigation

The completed investigation measured native same-stream CPU throughput,
GPU precision/acceleration and source-matched local CPU costs with bounded
checks. The baseline compiled source was
`c9a797431bd9d35a0bd2c17149dbcf6211a2296c02ff8c67499c90e236099226`.
Preserve the global `2*H` worker policy, full resource accounting, complete public
work and physical completion while investigating serialization and repeated work.

The authorized cloud envelope was at most eight hours and $20/hour aggregate.
The selected task used one full RTX PRO 6000 under a $2.50/hour ceiling and $25
total budget, without persistent volume. The roughly 100-minute rental ended
with confirmed deletion by 20:47:10 UTC; no pod or cloud volume remains. The
observed balance decrease was about $4.08, not a finalized invoice.
Provider/account details and operational logs stay outside the checkout.
See [the completed scope](triad-performance.md#native-throughput-and-precision-investigation--10-november-2026).

Verified current evidence remains scoped to the measured source:

| Check | Outcome and limit |
| --- | --- |
| Clean CPU baseline | Native triad group 32/32 passed, 32.67 s, before the current changes. |
| Current functional groups | GUI 28/28 passed in 106.60 s; contract 35/35 in 30.61 s; triad 32/32 in 30.62 s. The first triad run passed 31/32 because its CLI test expected a superseded argument; the corrected expectation then passed in the full group. These are functional checks, not capacity qualification. |
| Actual CUDA baseline | Exact `6bc9b0e3`/`c9a797…` baseline built the probe and passed the canonical tensor test within its 45-second bound on a full RTX PRO 6000 Blackwell Server GPU: 97,887 MiB, SM 120, driver 595.91.07. The pinned image supplied Torch 2.9.1/cu128 with C++11 ABI. |
| Native CUDA work | One-unit regression and active/noise slot checks passed expected decisions and input-identity checks, with actual device work and a 16 GiB device bound, W=4/O=1 and 32 GiB host bound. These are functional checks, not a throughput, tensor-core or capacity result. |
| Ingress completion wait | The single-completion-wait repair built and passed 3/3 focused checks in 11.93 s; the source change was independently reviewed. No measured speedup is claimed. |
| Coalescing/Helper fixture | Coalescing and the strong-Helper arithmetic fixture passed the CPU ATen check, 1/1 in 1.63 s. Six paired CUDA cases passed coalescing parity with fewer copies and mixed timings. The later fused complex64 repair and direct-Gram measurements have the narrower outcomes recorded below. |
| Shared receiver executor | Built and passed three focused checks in 10.09 s: stream 0.06 s, GUI 3.64 s and probe 6.39 s. Independent review found no remaining source flaw in the reviewed concurrency, lane or memory boundaries. The later concurrent-leaf exception and retained-idle-stack contrasts passed in one focused test (1/1). The matched-I/Q timings below do not support a gain. |
| Mixed-stream regression v2 | Current and baseline binaries each passed the same one-unit stream preflight at W=4/O=2, an 8 GiB bound, 600,000-sample cap and 12-second call bound, with 377,637 identical input rows and matched work. Both first attempts at 512 MiB failed controller admission; 8 GiB is this fixture's allocation, not a portable minimum. The completed fresh-call comparison below is inconclusive overall, with its mixed stream passing the slowdown gate. |

The current compiled C++ manifest is
`3aaf0fbfeb9f311ebe66b640b9aa2ae3f3caf8b4c0164d99caad199d000c5847`.
Its combined CUDA check passed the canonical tensor assertions and an actual
five-slot message with two running owners. The
[combined receipt](../local-evidence/native-throughput/combined-cuda-3aaf0fbf/combined-cuda.json)
binds the native binaries and device/runtime; it does not certify every row.

The CUDA setup first attempted a script before its upload completed, then found
`nvcc` absent from the SSH PATH despite being installed, and Ninja unavailable.
Those attempts reached no receiver assertions. Adding `/usr/local/cuda/bin` to
PATH and installing Ubuntu `ninja-build`/`libssl-dev` resolved the prerequisites;
the actual baseline CUDA checks above then ran. The baseline CUDA receipts have
been retrieved; operational records remain outside the checkout.

The first matched CPU comparisons did not improve elapsed service. Both replayed
12-slot I/Q with SHA-256
`0673bd95f9b4b92317dc951161859b9f8d621e0a44c714c121dce175e12f4493`.

| Comparison | Measured point | Interpretation |
| --- | --- | --- |
| O=2 primary pair | Whole service 37.63 → 38.75 s; central four commits 12.0835 → 12.7259 s; hardware busy fraction 87.70% → 88.88%. | About 2.98% slower overall and 5.32% slower centrally. Main scoring/work matched exactly, including both messages and scored-quiet completion. |
| Original default baseline, H=12/W=24/O=24, 8 GiB cap | Whole 37.0251 s; first slot 2.6940 s; first message 36.9523 s; CPU 391.82 s; busy 88.19%; reported RAM 1.655 GB. | Reference point, not a capacity qualification. Its sampled owner counter undercounts actual overlap. |
| Initial shared root priority | Whole 39.0495 s; first slot 28.5360 s; busy 85.96%; 12 scorer roots/24 pool threads; reported RAM 2.016 GB. | About 5.47% slower overall with a material head-of-line latency regression. |
| Leaf-first, narrow admitted scratch | Whole 38.2704 s; first slot 18.5478 s; busy 82.68%; ten roots/24 threads; reported RAM 1,976,414,672 bytes. | Still slower; finite drain retained only three- and two-lane scratch shapes. |
| Full-W scratch, before oldest-slot priority | Whole 36.8415 s; first slot 20.9921 s; first message 31.6315 s; busy 86.67%; reported RAM 3,176,535,312 bytes. | Wider scratch reduced the finite commit-7→11 drain from 10.2118 to 5.2063 s. This finite tail is not sustained-throughput evidence; the point alone is not a speedup qualification. |
| Current full-W scratch plus oldest-slot leaf priority | Whole 37.6654 s; first slot 7.5399 s; first message 21.7266 s; CPU 381.879 s; busy 84.49%; nine roots/24 threads; reported RAM 3,160,298,768 bytes. | All 12 reported scores/work and physical messages match the baseline. First message is earlier, first slot remains later, and whole service is about 1.73% slower with lower busy fraction. No throughput/utilization gain is claimed. |

The current [oldest-slot receipt](../local-evidence/native-throughput/row5-default-oldest-first.json)
and preceding [full-scratch receipt](../local-evidence/native-throughput/row5-default-full-scratch.json)
retain the exact matched input and configuration. All current owner scratch
shapes were W=24 under the same aggregate pool and 8 GiB memory cap. The oldest
eligible public slot receives leaf priority; queued roots use otherwise unneeded
lanes. The focused stream check passed 1/1 in 0.08 s for that candidate. This is
the accepted finite-work latency tradeoff: earlier first-message delivery,
without a throughput/utilization gain or capacity qualification.

The O=2 invocations are incomplete overall: subsequent 6- and 12-worker pilots
hit their 60-second deadline. Keep those failed additional pilots alongside the
completed primary observations. The default central interval also remains
unqualified: four central commits cannot exclude the seven uncommitted slots
already present at its start (`interval_could_be_explained_by_startup_carryover`).
The O=24 policy permits up to 47 startup-carryover slots; that theoretical bound
is not the observed count.

A final-source explicit O=2 replay retained the same input hash, all 12 reported
scores/work and both physical messages. Its valid ready-input interval, commits
4→8, covered 240 represented seconds with one carryover slot. Central service
rose from 0.050348 to 0.052515 s/s (about 4.30% slower), while busy fraction rose
87.70%→88.70%. Whole service rose 37.63→38.07 s (about 1.17% slower), and RAM
1.361→1.475 GB, with two running roots and 24 pool threads. The
[final O=2 receipt](../local-evidence/native-throughput/row5-backlog-final-o2.json)
is a complete primary comparison; the original invocation's later pilot
timeouts remain separate failures. This explicit O=2 point neither qualifies
default O=24 nor supports a throughput gain.

The old `peak_active_owners` field was sampled after commit and undercounted
scoring overlap. In particular, its O=2 value of one does not mean the baseline
was serial: raw slot timings show later baseline slots overlapping in waves.
The shared pool enables earlier/sliding overlap and records roots directly,
but these points show neither a speedup nor a capacity pass. Higher reported
utilization alone cannot reverse the elapsed-service failure.

The first real fused complex64 row-5 pair failed exact adaptive fine-work
parity: its two-second prefix evaluated four additional candidates, increasing
basis rows from 23,136,000 to 23,160,000. Equal retained-state and transition-edge
counts did not satisfy that gate. An isolated pointwise fixture had no
unrotated float-component mismatches and 975 rotated mismatches. Inspection of
the installed C10 CUDA binary found two `FFMA` instructions in the final complex
multiply, whereas the initial fused implementation used four `FMUL` and two
`FADD` instructions. The narrow explicit final-FMA repair passed independent
source review and actual RTX PRO 6000 exact pointwise, canonical full-slot and
original failed-row checks, including exact work/state at its 2/3/4/6-second
checkpoints. Six paired observations then preserved exact scores, work and state.
For this one row/seed and bounded continuation, DATA and QUIET fine times fell
36.2% and 34.1%; measured receiver-phase subtotals fell 17.1% and 15.3%. Kernel
launches fell from 58,333 to 10,615 and device peak from 96,468,992 to 90,177,536
bytes. These are scoped GPU phase improvements, not whole-stream throughput,
communication reliability or local CPU forecast qualification.
This matches the installed C10 binary's arithmetic, not a promise of bitwise
behavior across arbitrary LibTorch/compiler/device versions. The oracle must
reject a changed installed arithmetic path.

An actual-GPU FP16 Gram-shape experiment met its arithmetic check (reported
error `1.314e−4`) but took 184 µs, versus 119 µs for FP64 batched multiplication
and 33 µs for the direct FP64 experiment. That low-precision candidate is stopped;
its WMMA name supplies no instruction-level tensor-core execution proof. The
direct-FP64 Gram path was subsequently implemented and independently accepted
for its bounded CUDA shape of at most 128 modes. Six paired DATA/QUIET checks
preserved exact work/state with maximum score difference `1.86e-9`. Relative to
the repaired fused mapper, median fine-time ratios were 0.46138/0.44773 and
receiver-phase ratios 0.83629/0.79657. Allocator peak reserved memory fell from
90,177,536 to 69,206,016 bytes. The
[paired receipt](../local-evidence/native-throughput/direct-gram-row5-97021/paired-summary.json)
retains the one-row/seed limits and separate timing gates; these ratios are not
whole-stream or CPU capacity results.

The first complete GPU v2 run took 181.2 s and 91 calls, but six of nine models
failed: a first scored slot cost about 0.319 s versus 0.110 s subsequently.
The original report remains a failure. The revised policy executes every
active/noise unit as a fresh `regression --units 1` process and every mixed unit
as a fresh `message` process. It describes the first scored slot in a fresh
process, neither a fully cold GPU device nor steady state. Earlier same-process
v2 reports are incompatible; no extra looks were used to repair the mixture.
The frozen driver SHA-256 is
`73ae19dd896d4a07dbcd79786c4018a6802c3cf3a0ea682988b01a9939bf3dd7`.
Seventy-seven focused checks passed. Its fresh-call
[CPU baseline](../local-evidence/native-throughput/cpu-regression-v2-fresh-baseline.json)
established all nine models in 101.675845 s/163 calls at W=4/O=2, 8 GiB,
600,000 samples and a 300-second overall cap: nine active/noise looks and five
stream looks. The [candidate comparison](../local-evidence/native-throughput/cpu-regression-v2-fresh-candidate.json)
took 103.679875 s/163 calls, with all nine models supported. Mixed-stream means
were 0.630636/0.634083 s, and the ratio interval `[0.841389, 1.201541]` passed
the declared 1.30 slowdown gate at five looks. All eight standalone phase
comparisons remained inconclusive at nine looks; the overall result is
inconclusive, not a regression pass. No rerun was used to narrow those intervals.
The fresh-call GPU run established a baseline in 272.966 s/135 calls with all
nine models supported; active/noise/stream means were 0.32219/0.32291/0.95147 s.
It has no GPU reference comparison. Baseline establishment is not a regression
pass or capacity qualification.

Forecast accounting was repaired to charge audio frontend cost by represented
continuous PCM seconds rather than slot cadence, and to test independent
adaptive-work counters separately. Retained-state count 64 and emission count
88 must not be mixed into a drift ratio; exact affine-work equality remains
required. Fifteen focused checks passed; stable-counter reanalysis passes and
the genuine 25→64 drift still fails. CUDA compiler/cache guards separately
passed 51 unit checks.

The current stock row-5 CPU collection completed 33 calls with verified source
identity. Nominal and conservative point estimates were 0.062998 and 0.269030
s/s; logical I/Q-plus-PCM replay was 0.056741 s/s, giving point margins of
93.70%, 73.10% and 94.33%. I/Q replay took 38.709 s for 686.473 represented
seconds, with 86.94% hardware busy fraction, eight running owners and
3,168,302,352 bytes reported RAM. Messages, reacquisition and codec checks
passed. These are CPU-only points, not a full interval capacity pass.

Row-5 CUDA checks passed all five paired strata and scored quiet in automatic
precision, but its high-SNR challenge changed a losing acquisition clock
coordinate from 0 to −0.0003 ppm. Correct bits and 4% work drift did not satisfy
exact acquisition agreement. One explicit-FP64 check passed that challenge;
it qualifies only that explicit arithmetic check, not the automatic class.
The 5 dB near-decision challenge was outside the declared score band
(14.6199 versus 21.4934, tolerance ±3.224). Independent CPU training at seed
86021 placed both 5.5 and 6.5 dB below threshold, failing the predeclared bracket;
that failed construction produced no qualification report. A separate bounded
CPU-only construction reused seed 86021, with lower 6.5 and fixed upper 14.5 dB-Hz
and at most five bisection calls. It succeeded in five calls, fixing 8.0 dB-Hz
at log evidence 21.858229 versus threshold 21.493 ±3.224, without CUDA feedback.
The independent CPU cost holdout at seed 97021 remained unchanged. The final
explicit-FP64 row-5 run passed all five strata and three operational challenges
with exact work/acquisition and zero adaptive drift. CPU and GPU near-decision
log evidence both measured 21.85822942. This repairs that explicit precision
route; the automatic-class failure remains a separate result.

The [official all-eight forecast report](../local-evidence/native-throughput/forecast-row5/all-eight-fp64-forecast.json)
supports three partial row-5 fine-phase estimates:

| Component | Estimated s/s and interval | Independent CPU holdout s/s |
| --- | --- | --- |
| Cold maximum | 0.259870, `[0, 0.611954]` | 0.250479 |
| Terminal | 0.015253, `[0, 0.126661]` | 0.014189 |
| Erasure | 0.015019, `[0, 0.055098]` | 0.014460 |

Here s/s means CPU service seconds per represented baseband second.

Active and quiet cache misses/prepared-row counts differed by about 9–11%,
outside the unchanged 5% calibrated cost-regime gate despite exact CPU/GPU
paired work. Their estimates remain unresolved. Required matching same-stream
calibration/backlog and complete holdout/uncertainty gates also remain open.
Nominal and conservative GPU-assisted service and complete capacity are
unresolved; the other seven rows have geometry-only coverage. The partial
component forecasts cannot be reported as an all-row or complete-receiver pass.

Strong-Helper +30 and +60 dB checks now pass on CPU and CUDA. The original
+60 dB fixture failed placement at 1,725 Hz IF before receiver assertions;
the valid fixture uses 9,000 Hz sample rate, 2,450 Hz IF and noise variance 0.03.
Its exact 544,313,305-byte workspace lease also replaces an insufficient
512 MiB attempt. These repaired fixtures do not erase the original setup and
admission failures.

The CUDA build now requires NVCC and the CUDA SDK. The actual Blackwell build
uses `CMAKE_CUDA_ARCHITECTURES=120` and `TORCH_CUDA_ARCH_LIST=12.0`. These identify
the measured SM 120 device; they are not a portable architecture default. The
optional CPU ATen build and default application without Torch remain unchanged.

Retain the matched receipts, original failed checks and incomplete pilots in
[`local-evidence/native-throughput`](../local-evidence/native-throughput/) to
support this checkpoint's scoped claims and unresolved gates. The bounded
campaign has ended; no additional run is implied. Tensor-core execution and
complete capacity remain unqualified. Retire these raw receipts after applicable
replacement evidence resolves the gates and preserves the original failures,
source/configuration provenance and margins.

The baseline/candidate comparison no longer needs copied executables or source
archives. Retire those obsolete bundles and builds while retaining needed raw
results and hashes; never relabel old costs as new calibration. Release artifacts
and currently useful build profiles keep their separate retention contracts.

## Native codec and measurement migration

**Completed baseline checkpoint.** The checks below precede the current
throughput/precision changes and retain their original source and limits.

The migration from `1c58507a5178c7700cd15d0fcdd05a0091c6dba9` adds bounded native
long-message intervals, lazy emission,
shared native CPU/tensor measurement paths and a native real-audio frontend,
while retiring Python receiver DSP. The default application requires neither
Python nor Torch; optional compiled ATen bulk operations retain production receiver
control and state. CUDA device execution was unavailable during this local
checkpoint.
Implementation, numerical comparison and capacity qualification remain separate.

The long profile keeps the short codec through 16 bytes and uses fixed 1,216-slot
marker/codeword intervals beyond that boundary. Its private-key tag binds each
word to its actual public calendar address; it does not authenticate the whole
endpoint. Early short-prefix ambiguity and missing whole final uncompressed
intervals retain the limits in [protocol](protocol.md#completion-and-authentication-limits).
No new pass is inferred from the earlier short-profile GUI checkpoint below.

Final local checks and scoped supporting evidence are:

| Check | Outcome and scope |
| --- | --- |
| `./build.sh test triad` | 32/32 passed, 29.99 s, after the final spool/Helper rounding correction. |
| `./build.sh test contract` | 35/35 passed, 21.32 s. |
| `./build.sh test gui` | 28/28 passed, 106.67 s, including Fast and Legacy. |
| Default application | `datapump-gui` rebuilt and `gui_self_check` passed; `ldd` reports no Python or Torch dependency in this build. |
| CPU ATen integration | Current optional LibTorch check 1/1 passed, 1.33 s. An actual `tensor-cpu` regression unit completed the expected DATA decision with the common current source manifest. No CUDA execution. |
| Session/audio/Helper repair | Earlier focused `gui_robust_live`, `robust_audio_frontend` and `triad_helper_burst` 3/3 passed, 4.30 s. Long PCM/cancellation, quotas and full-duplex reservations are covered by these checks and the final groups; memory handling was independently reviewed. |
| Independent receiver parity | Four complete native slots and both full coarse routes matched the baseline exactly. Execution-witness defects were reproduced and repaired, with focused checks passing. |
| GPU tooling and build closure | 21 focused tests passed, including full synthetic forecast serialization, plus actual native CPU slot/terminal smoke checks. Requiring unavailable CUDA was correctly rejected. The later proposal bundle passed nine focused packaging tests and the current `triad_gpu_diagnostics` CTest (1/1, 0.77 s) after the full 32-test group; fresh extracted CPU LibTorch compilation, parity execution and `describe` also passed. These are not CUDA results. |
| Extended Message edit path | `gui_adapter_boundary`, `web_bridge`, `web_worker`, `web_renderer` and `web_preview_client` 5/5 passed, 2.42 s; loopback `build_preview` and `web_preview` 2/2 passed, 3.63 s in the permitted run. Native pipe/relay and client coverage only, not actual TX or Wasm runtime. |
| Native probe/spool | Earlier focused `triad_receiver_probe` and `triad_probe_spool` 2/2 passed, 5.13 s, on source manifest `2a309b945ea1a2397313156567014503441ed75e40fda91e06227c80e618b2d2`. The bounded pilot passed after backing-accounting repair; the final triad group passed after the subsequent portable rounding fix, which leaves this host's reservation unchanged. This is not manual-case calibration. |

The final compiled source manifest is
`c9a797431bd9d35a0bd2c17149dbcf6211a2296c02ff8c67499c90e236099226`;
the native probe SHA-256 is
`8b197fb56c98774bba2548f73a61e0dea6aa871de279c41be6b43e0f0b1c027b`.
These identities bind this local checkpoint, not a published release.

One bounded native CPU abbreviated run followed the final groups. Automatic
allocation resolved H=12, W=24 and owner cap 24, with one numerical thread per
worker and a 9,983,840,256-byte memory cap. All eight active/noise slot, coarse,
fine and audio models reported `abbreviated_holdout_supported`; the run status
was `baseline_established`, with comparisons `uncompared`. Active used 27 batch
receipts through nine looks, noise 15 through five looks, with only the declared
1/2/4-unit batches. This establishes a current native baseline, not a cross-revision
regression pass, real-time capacity or communication sensitivity.

The superseded v1 `regression-baseline.json` contained 559,086 bytes with SHA-256
`08380276aa9c15e53718d32dbb9e2d95000648031affdfa36f1079ea1f370905`.
This source-scoped result does not supply the current v2 comparison. Its
applicable outcome is preserved here; it has no active baseline-retention duty.

The independent clock-envelope reproduction first rejected a legal signed-arrival
case (`−1000`, zero Helper lead) in `PublicProfile`. The repaired public window
admitted it with a 46,938,976-byte workspace bound. Nominal physical draws and TX
addresses remain unchanged; only the quiet-evidence aperture narrows where needed.
This fixes the public-window bound rather than altering the simulated signal.

The frontend regression first reproduced rejection of a legal 4 MiB escaped
Message edit, then an invalid 2 MiB `<` draft overflowing snapshot JSON. The
repair raises only edit-frame allocation to 4 MiB, enforces published control
limits and preflights JSON expansion at 6 MiB within the existing 8 MiB envelope.
Decoded Message content remains limited to 1 MiB; other event values retain
1 MiB and errors 4096 bytes. The passing checks retain those rejection boundaries.

A bounded native spool fixture generated 827,637 I/Q rows, stored 14,069,829 bytes
and retained no full-capture array in RAM. Exact-input W=2/O=2 and W=1/O=1 replays
on H=12 each committed ten valid slots, two actual `e`/quiet episodes and the
predeclared ready interval from commit 3 through 6. Both reported
`peak_active_owners=1`; that after-commit counter does not establish actual peak
scoring overlap. This is not a multiowner speedup or manual-case capacity result.
Service includes separately quantified spool reads/hash; generation and spool
writes are excluded. Full logical PCM projection is a separate measured phase.
Controller wall removes measured condition-variable wait and spool I/O/hash but
may overlap DSP; controller thread CPU still includes spool work.

The fixture's tmpfs file occupied 14,073,856 charged RAM bytes. A separate
low-memory preflight planned 22,967,637 samples within sample/disk bounds, but
its 390,451,200-byte temporary backing exceeded the 256 MiB total budget. It
exited 2 before capture, after profile preparation. Verified ordinary disk has
a separate storage budget; tmpfs or unknown backing conservatively reserves
file extent against memory. These are bounded lifecycle/admission checks, not
CPU capacity or sensitivity results.

The CUDA-enabled C++ host branch passed a GCC 14.2/C++20 syntax check with
`DATAPUMP_TRIAD_TENSOR_CUDA=1`, CPU LibTorch 2.15 headers and verified NVIDIA
CUDA 13.0.2 runtime/CRT/CCCL headers (2.56 MB). The source SHA-256 was
`29e9fc8dbaa0ca8db3757636a7387c96f00a499e63d28a8ba3acde77907994b6`.
The upstream `C10_CUDA_NO_CMAKE_CONFIGURE_FILE` switch skipped the CPU package's
absent generated CUDA configuration; no substitute headers were invented. The
check exited 0 without warnings. Header provenance used the
[NVIDIA redistribution manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.0.2.json);
temporary downloads were removed. This does not test CUDA linking, device
execution, numerical fidelity, memory or performance.

The earlier bounded RunPod proposal prepared CUDA checks before provisioning.
It has been superseded by the
[current investigation](triad-performance.md#native-throughput-and-precision-investigation--10-november-2026).
Its initial archive omitted
`triad_probe_spool.hpp` and `triad_fixture_key.hpp`; an explicit allowlist repair
and recursive local-header closure test corrected that packaging failure. The
fresh extraction built the probe, exact bundled tensor parity test and Helper
exporter, with no unresolved or Python libraries and the same compiled source
manifest as the local CPU build. The corrected archive has 512 files and
1,370,721 bytes, SHA-256
`2bfe44676020d21a88023c5f57a7df5b26fb0537404f865ab299709a4da5adad`;
its internal manifest is
`dd17c1c270cc750d5c7dfd0a254cdf0fb15393fd1427e700013918962fa4a22a`.
The bundled test SHA-256 is
`4e7b734f09cf4b9270ff9b4df53ed8285f196683b5de8d82aeacdb512f13f2ee`.
The completed matched comparison superseded this archive's retention purpose;
its hashes and packaging findings remain here while the obsolete source bundle
is retired. The packaging checkpoint contained no account metadata or provider
action.

No local functional check remains pending in this checkpoint. Actual CUDA
linking/device execution/performance, Windows runtime, physical audio devices,
Wasm runtime, full manual-case capacity and multiowner throughput qualification
remain unrun. No hosted CI job, publication or release certification was performed.
The configured-calendar scope remains; unknown waveform epoch/ordinal/timing
acquisition is unqualified.

Python retirement removed 38 obsolete modem/DSP tools, 35 implementation-only
tests and two old sensitivity/confirmation scripts. Remaining Python modules
handle native orchestration, statistics, case parsing and physical topology.
Obsolete copied source, bytecode, bridge build artifacts and mixed/source
archives were removed from ignored evidence; numeric inventories, expanded
failed-check receipts, source-hash provenance and release assets were preserved.
Old numeric captures are not a dependency of the native implementation. Retain
any raw data only for a specifically identified unresolved question or release
duty, then remove it when that purpose ends.

Current native benchmark commands are documented in the
[computational guide](triad-performance.md). The bounded baseline above does not
supply a full manual-case capacity result; that requires applicable complete-work
coverage, source identity, uncertainty and independent holdout checks. Historical
Python calibration, transport applicability exceptions and old GPU forecasts
are inadmissible for that native runtime. Preserve their original failures and
margins as historical findings. The new native Fejer real-audio path replaces
an unused Python Kaiser experiment; that replacement is not a numerical-parity
claim. No heavy calibration campaign is authorized by this documentation update.

## Native GUI integration

**Earlier short-profile checkpoint.** The outcomes below predate native long
messages and the current benchmark/tensor/frontend migration. They retain their
own source and coverage; their passing counts do not qualify those later changes.


The operating architecture is now a single C++ stack linked into the GUI:
canonical source/Helper, complete slot scoring, bounded message/FEC processing,
in-process continuous scheduling and the native audio session. Python is absent
from this operating path. The old Python process scheduler and sealed-input IPC
are reference/historical machinery, not an application deployment requirement.
Live reception is scoped to an explicitly configured public calendar and keyed
ordinal, with a declared local live-noise model. Unknown-start acquisition,
external radio retuning, all-configuration sensitivity, native CPU capacity and
physical-device qualification remain outside the current evidence. These Linux
functional checks do not qualify Windows, browser/Wasm operation or a release;
release certification requires exact published source and binary identities.

The current functional checks have these outcomes. Combined coverage below
reuses unchanged passing cases after scoped corrections; it is not a claim
that every full group was rerun.

| Check | Outcome |
| --- | --- |
| `./build.sh test triad --jobs 1 --build-jobs 4` | 30/30 passed, 63.36 s; `CCACHE_DIR=/tmp/pumpmodem-native-ccache`. Includes native source/Helper/scorer/message/stream, `gui_robust_live` (3.24 s), Python references and GPU diagnostic unit checks. |
| `contract` | 32/32 passed, 28.04 s. |
| `gui` | Initial 27/28 passed in about 107 s; corrected `gui_native_policy` then passed in 0.05 s. All 28 cases have passing coverage across those runs. |
| `frontends` | Initial 7/8 passed in 21.36 s; corrected `framebuffer` then passed in 0.28 s. All eight cases have passing coverage across those runs. |
| `web` | Initial 11/13 passed in 147.59 s, including `web_live` in 137.59 s; both localhost preview setup failures passed in the permitted rerun, 3.56 s. All 13 cases have passing coverage across those runs. |
| Rev application build | Passed with Clang 19; runtime native-window checks remain separate. |
| FLTK native-window group | Final rebuilt group 3/3 passed, 93.82 s: workflow 0.14 s, adapter 93.55 s and document 0.11 s. Earlier failures and repairs are recorded below. |
| Rev native-window group | 5/5 passed, 335.29 s: workflow, adapter, platform and both coordinate-scale checks. The rebuilt final-copy layout/lifecycle probe also passed with nine painted frames and zero failures. |

Native-window checks used an isolated Xvfb display with software OpenGL. The
full Rev group and final rebuilt FLTK group used:

```sh
DISPLAY=:93 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 CCACHE_DIR=/tmp/pumpmodem-native-ccache ./build.sh test native --backend rev --jobs 1 --build-jobs 4
DISPLAY=:93 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 ctest --test-dir build/dev --output-on-failure -L '^native_gui$' -j 1
```

This validates the exercised native frontend paths, not physical audio devices
or a hardware GPU/driver combination. The rebuilt Rev
`timeout 90 build/rev/test_rev_adapter --layout-lifecycle` probe used the same
display/software-rendering environment and passed after the final shared
airtime-label change. No native runtime check remains pending in this checkpoint.

The GUI failure was an obsolete attachment-service fixture for Robust's short
profile; the framebuffer failure expected the retired `Short` label instead
of `Target SNR`. Scoped fixture/help corrections preserve received-content and
state checks and add 16/17-byte text and 208/209-bit boundaries. Rebuilt
`ctest --test-dir build/dev --output-on-failure -R '^(gui_native_policy|framebuffer)$' -j 2`
passed both affected cases. An earlier stale Fast service fixture was corrected;
`gui_fast` passed in 4.54 s and the full GUI run's Fast live case in 106.99 s.
Web preview failures were sandbox localhost setup restrictions, resolved by
rerunning those checks with permission, without a source fix. Initial failed
invocations remain failures.

The initial FLTK native group passed 2/3 cases. Adapter diagnosis separated
fixture assumptions from a product defect.
Tab press/release checks now tolerate unrelated asynchronous revisions while
preserving their actual input/state contract. A compression-label fixture's
80 ms wait preceded the 100 ms native presentation interval; waiting boundedly
for the actual visible text exposed genuine glyph clipping. The product fix
shortened only the airtime label to, for example,
`3 DATA + 2 scored quiet slots (~300 s)`. The focused `--compression-labels`
check passed in 0.44 s, covering maximum 208-bit input and actual Compression
and narrow Console glyph bounds. Updated `gui_native_policy` copy assertions
passed in 0.05 s. FLTK, Rev and native-worker applications were rebuilt; the
final full FLTK group and focused Rev check above then passed. Those measured
outcomes, rather than the fixture corrections alone, close the runtime checks.

These results are functional coverage, not a new GPU workload forecast, native
CPU calibration or capacity qualification. No all-configuration operating pass
or final release-source identity is claimed. An initial aggregate compile
stopped on a missing `pcm_scale` declaration and was repaired before runtime
validation; that failed invocation executed no runtime assertions.

`ctest --test-dir build/dev --output-on-failure -R '^gui_robust_live$'`
passed in **3.41 seconds**. Its bounded simulation decoded `e` from actual native
`001` DATA scoring and two fully scored quiet slots, with validated content and
ordered pattern records. This one configured-calendar stratum used 1500 Hz
sampling, 500 Hz IF, positive noise at modeled 80 dB-Hz, exact oscillators and
Helper/FEC/spread/selective/FHSS Off. Its declared bounds were five scored
windows, 600,000 samples and 90 seconds wall time. The first fixture used
6000 Hz with a minimum 60-second public cadence and failed its sample-budget
preflight; the corrected legal 1500 Hz/500 Hz fixture retained the same cap.
The earlier failed setup is not counted as a completed reception trial.

A separate dated real-PCM fixture passed Hilbert conversion and complete native
DATA-1 scoring, then rejected transport EOF as message completion. It retained
actual receiver I/Q and waveform observations without publishing a complete
message. The same test covers audio routing/options, AMATEUR, one-shot force,
cancellation and session lifecycle through mock device providers. These mocks
are not physical-device qualification. The actual simulation and PCM receive
paths keep native DSP, score evidence and codec processing real.

An independent memory discriminator originally measured **183,910,400 bytes**
RSS against a **167,772,160-byte** cap: a real overrun, not an accepted timing
variation. Admission had credited aggregate resident growth against pending
scorer work that had not yet allocated its full DSP. The repair retains
outstanding DSP/stack/output reservations separately from process footprint;
initialized I/Q is covered once by the greater of footprint and the
baseline-plus-owned-input floor. The corrected deferred-DSP check withholds
the third slot rather than spending the same resident-memory credit twice.
Explicit-worker resolution also preserved
`H=12, W=27, C=27` when only `W` was supplied. Windows automatic-cap source now
uses the minimum of physical, commit and virtual availability; that source
inspection is not a Windows execution result. These bounded resource checks
supply neither an OS-enforced hard memory limit nor native CPU capacity.

Bounded migration comparisons reported the following matched-input evidence:

| Component | Observed coverage and limit |
| --- | --- |
| Helper | Nine reference cases matched status/reason, endpoints within `1e-10` and levels within `3e-6`; these do not establish all Helper-on DATA joins. |
| Native basis | Cached dense one-lane parity was exact; three-lane maximum difference was `4.78e-15`. Separate interpolation gates cover their declared geometry. |
| Source/exporter | 1,050,000 I/Q and validity samples plus ten public slot receipts matched exactly across DATA/Helper and selected channel cases. Shared storage changes workspace accounting, not sample semantics. |
| Duration planner | Twenty-three reference rows matched candidate durations within `3e-12`; the model remains provisional and does not guarantee sensitivity. |
| Full slot scoring | Four bounded clear/encrypted DATA and noise-quiet cases passed alongside invalid-input/resource/cancellation checks. A 71,154-row encrypted two-ray fine comparison matched affine/adaptive/combined scores within `1.183e-7` and the quiet threshold exactly. Different traversal counters are not matched CPU work. |
| Message/FEC | Configured Off/RS20/RS60, exact raw bypass, bounded erasures, quiet completion and rearming received focused checks and independent review. A leading ambiguous/quiet/DATA chronology bug was found and repaired with a distinct RS20 regression. |

This is numerical/functional coverage, not a throughput comparison or a success
probability estimate. Independent Message/FEC, Session/profile, source and
scheduler reviews found no remaining blocking issue in their reviewed scope.
The scoped complete-receiver mathematical review resolved coarse tie ordering,
lag and shared-endpoint workspace defects; their targeted regressions are
included in the passing triad group. The reviewed source, documentation and
lifecycle scope has passing functional coverage, including the final native runtime checks above. This checkpoint is
not physical-device, sensitivity, native-capacity, Windows/Wasm or release
certification. Temporary parity captures and command logs serve those checks; remove redundant
captures when the comparison and review are resolved, retaining concise
findings and evidence required for unresolved failures. No heavy CPU-capacity
rerun is part of this integration checkpoint.

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

## Continuous receiver restructuring

**Historical runtime scope.** This section records the former Python-orchestrated
receiver with native DSP kernels and independent process owners. References to
“current” or “final” below identify that measured revision, not the new native
C++ GUI runtime. Process startup, resource trackers, copied transport and `memfd`
were part of that implementation and are superseded operationally. Retain its
failed gates, timing/memory limits and source identities; none transfers native
scheduler qualification. The [current ownership contract](development.md#receiver-cpu-allocation)
preserves `2*H`, full memory accounting and ordered physical completion.


**Final shared-input sustained observation.** One bounded 49-window run on the
final receiver used its automatic allocation: 24 software permits on 12 visible
hardware threads, with an 8,748,048,384-byte operating memory cap. Across all
417 predeclared eligible backlog intervals, it averaged **8.325 monitor-adjusted
busy hardware threads out of 12 (69.37%) for 51.898 seconds**. The longest
uninterrupted eligible stretch was **32.899 seconds at 7.921 busy threads**.
Eligibility followed input and backlog readiness, excluded owner startup and
ended at the start of the final window's assembly; it did not select CPU peaks
or a particular owner count. Thirteen services overlapped at most, and all 24
permits were allocated for 50.382 seconds of eligible coverage. The raw mean,
including monitor overhead, was 8.390 busy threads.

Owner startup occupied 21.238 seconds externally (21.183 seconds in the
receiver). The complete receiver lifecycle was 161.091 seconds, including
86.128 seconds through the final input push and 16.437 seconds from final
assembly to drain. Startup overlaps those lifecycle phases and must not be
added to them. Fixture generation took another 11.688 seconds; the supervisor
finished in 178.304 seconds under its 210-second bound. Peak sampled RSS was
5.167 GB and peak prospective admission was 8.744 GB under the 8.748 GB cap.
All 49 windows completed in order using sealed shared input, with two scored
quiet slots and continued listening; there were no retries, fallbacks or
remaining children. The full input digest matched before ingress, and source,
native artifacts and case identities matched after execution.
The declaration also required a full input digest after processing, but the
driver did not recompute it; that check is **unverified**. Source inspection
shows that ingress copies the input chunks and window assembly creates fresh
arrays, but this is not a substitute for the missing post-run digest. The input
is one DATA bit `0` followed by 48 quiet slots; its one-bit codec frame is
physically complete but invalid, not a decoded message demonstration.

The original predeclared exact historical-parity gate **failed** in 17 slots.
Independent examination found each difference was the number of dispatched
tracking update tasks, which changes with the worker partition. A separate
post-hoc comparison excluding only that allocation-dependent counter found all
other compared work, scores, coverage, addresses and decisions identical. This
does not change the original failed gate. The earlier 81.09% observation below
used the pre-memfd receiver and a larger 9.513 GB cap; the two runs do not
establish a causal speedup or slowdown. The current observation demonstrates
productive same-stream parallelism, not full saturation or all-case pipeline
capacity. Keep the raw result, failed gate and independent audit while they
support the current timing chart; replace them when an equally scoped current
measurement supersedes this claim.
[Final shared-input measurement](../local-evidence/final-backlog-cpu/result.json) ·
[Independent audit](../local-evidence/final-backlog-cpu/independent-audit.json)

**Scope: complete processing on a configured public calendar.** The retained
continuous listener preserves the configured keyed ordinal sequence. It detects
bits or nulls, confirms completion with two scored quiet slots and resets codec
state while keeping the global waveform ordinal. No semantic transmitter-identity
classifier is required. Acquisition of unknown waveform epochs, ordinals and
timing remains unqualified; configured-path estimates do not cover that search.
The full hardware-thread default
and the following qualified optimizations remain applicable.

**Current shared-input transport:** the Linux pipeline transfers sealed,
read-only memfd input instead of retaining several private copies per job.
Capability is checked once before admission; unsupported systems retain the
copied transport and its full reservations. Full DSP/native/input/result bounds
remain charged. The parent reserves one temporary assembly buffer, closes its
writable mapping before sealing, then retains only the FD and metadata until
successful release or safe retry. Children close every view and mapping before
acknowledging completion. FD-backed storage stays reserved even while unmapped;
there are no generic RSS discounts. Failed cleanup retains ownership and its
reservation. Finite and explicitly serial processing do not perform the probe.

A bounded full-size test held **three simultaneous 691,812,637-byte inputs**
for row-1 public ordinals 24–26. Canonical planning admitted **three eight-worker
jobs instead of one copied-input job** under the same 9072 MiB cap. Complete
modeled ownership, including one assembly buffer and transport overhead, was
**8,025,467,981 bytes**, leaving **1,487,213,491 bytes**. Sampled RSS peaked at
3.172 GB. Exact bytes, read-only mappings, immutable seals, malformed-descriptor
rejection and release/crash cleanup passed. This 7.440-second phase used
synthetic typed input: it establishes full-sized transport and conditional
admission, **not three-window row-1 DSP throughput**.

The native comparison used the same captured input through copied and shared
transport, with **24 total workers and three concurrent eight-worker services**.
All **839 checked input, placement, work, score, message and EOF fields matched
exactly**. Both decoded `001` to `e`, completed after two scored quiet slots,
scored a third quiet slot while listening continued, and rejected incomplete EOF
as physical completion. The shared path released all six parent descriptors.
A controlled collection boundary ensured the matched three-job wave; its elapsed
time is not a throughput estimate. Focused checks passed **63 stream tests and
35 backend tests**; the named triad group passed **22/22 in 30.30 seconds**.

The first private harness stopped after the full-size phase and copied native
control because it read transport metadata from an outdated receipt location.
The correction ran only the missing shared native look; neither completed phase
was repeated and production source remained frozen. Both supervisors confirmed
cleanup. Active execution totaled 62.561 s; the observed wall span was about
134 s within the original 240-second budget. Preserve the original harness
failure separately from the passing continuation.
[Full-size and copied-path receipt](../local-evidence/shared-input/final/result.json) ·
[Shared native continuation](../local-evidence/shared-input/final/continuation/result.json) ·
[Named checks](../local-evidence/shared-input/triad-final.log)

**Measured result before memfd transport:** automatic operation admits up to `2H`
independent owners sharing exactly `2H` software permits, subject to actual
backlog and full-DSP memory admission. `H` is detected from affinity/quota,
not fixed at twelve. Child bootstrap imports occur once before readiness;
future children are budgeted from their immutable pre-DSP RSS/high-water
measurements, without duplicating the parent's input heap. Parent and live
owner memory remain fully charged. A bounded reorder window lets completed
future slots release their owners before chronological message commitment.

That revision's **49-window** native capture sustained **9.730 monitor-adjusted busy
hardware-thread equivalents out of 12 (81.09%)** over **55.337 seconds** of
predeclared backlog coverage. Eligibility required all input accepted, the first
slot committed and fully received work still waiting; owner-startup intervals
were excluded. Neither achieved width nor measured CPU rate selected the bins.
Thirteen child services overlapped for 23.483 s and twelve for 30.176 s of that
coverage; all 24 permits were occupied for 52.363 s. Fourteen owners were
resident. Only 1.93% of eligible time reached 95% hardware occupancy, so sustained
full utilization remains unproven. Short counter bins have scan/quantization
jitter; the 55-second average is the useful measured point.

All 49 windows completed in order and the first 25 exactly matched the earlier
capture's work, scores and message result. Source, native artifacts, input and
allocation identities passed independent audit. Current owned-resource
accounting peaked at 9.463 GB; the larger final prospective admission was
9.475 GB, both below the frozen **9072 MiB (9.513 GB)** cap. Sampled aggregate
RSS peaked at **5.795 GB**. These are different quantities: reserved worst-state
DSP memory is not measured RSS. There was no fallback or workspace retry.
The full run and cleanup took **153.746 s** within 190 s; the receiver lifecycle
included **24.132 s** of one-time owner startup. This is one pre-memfd
utilization observation, not a measurement of the later shared-input revision,
a paired speedup or an all-row capacity result.
The final source passed **131 receiver checks, 23 backend checks and 22/22 named
triad checks (30.59 s)**. Temporary benchmark callers were removed after audit.
[Current backlog measurement](../local-evidence/resource-backlog-cpu/result.json) ·
[Independent audit](../local-evidence/resource-backlog-cpu/independent-audit.json) ·
[Current timing chart](receiver-parallelism.html#cycle) ·
[Final named checks](../local-evidence/parallel-routes/triad-expanded-final.log)

The preceding follow-up had twelve resident owners but only eleven overlapping
services, so its predeclared twelve-service sustained check failed. Its retained,
explicitly exploratory eleven-service analysis averaged **9.152 adjusted busy
threads** across 26.964 s. It exposed parent-heap-dependent startup admission;
the final architecture above corrected that accounting and removed the hardware
count as a software-owner ceiling. The failed check remains failed, and its
timings are not a paired control for the changed implementation.
[Preserved failed check](../local-evidence/resource-steady-cpu/result.json) ·
[Exploratory audit](../local-evidence/resource-steady-cpu/exploratory-width11-audit.json)

The current development changes preserve the full logical CPU budget and
remove repeated work from the dominant noise search and fine state updates.
The comparisons below use the same captured input, receiver work and allocation
with the optimized callback enabled or disabled. They are scoped development
comparisons; their exact source/native identities are in each receipt. They
must not be multiplied together or treated as complete receiver capacity.

The coarse CPU path shares immutable observation spectra between workspace
partitions. A strict-FP64 native rank-two kernel combines quadratic scores and
bounded peak selection inside the existing worker pool. Conservative arithmetic
ambiguity guards retain canonical peak selection for near ties, and retained
scores are recomputed with the canonical arithmetic before global selection.
Unsupported ranks, guarded Grams and larger retained-peak counts keep the
general path. The final twelve-worker row-4 noise comparison reduced median
elapsed time from **0.451572 to 0.295567 seconds (1.528× throughput)**. Busy-core
equivalents increased from **6.66 to 8.12**. A disjoint frequency holdout reduced
0.478227 to 0.326784 seconds (1.463×). All 1,248 raw peak identities, ordering
and published scores per look matched bitwise. The entire bounded qualifier
took 5.124 seconds; it did not execute the full broad frequency grid.
[Guarded coarse comparison](../local-evidence/continuous-coarse/guarded-native-discriminator.json)

Complete mapped CPU FP64 one-/two-tap pairs now use native candidate-parallel
Gaussian updates on that same pool. Gram validation is shared across coherence
models; each candidate advances the two blocks in chronological order using
PSD square-root factors and Cholesky solves. The controller publishes results
only after every tile joins. Generic, erasure and device paths retain the
general implementation. At twelve workers, row 7's continued fine time fell
from **403.174 to 334.730 ms (1.204×)** and its model phase from 97.034 to
19.366 ms. Fine busy cores increased from 3.174 to 3.262, with 14.65% less CPU
time. Row 7 at six workers improved 1.331×. Row 4 at twelve improved 1.095×,
below the declared 10% secondary gain target; that outcome is retained.
Logical work, state order and threshold outcomes matched under the declared
posterior tolerance. These four-second continued sessions are not successful
message trials or whole-receiver saturation measurements.
[Row 7 / twelve](../local-evidence/structural-fine/row-7-native-gaussian-integrated-abba.json) ·
[Row 7 / six](../local-evidence/structural-fine/row-7-6-native-gaussian-integrated-abba.json) ·
[Row 4 / twelve](../local-evidence/structural-fine/row-4-12-native-gaussian-integrated-abba.json)

The post-native row-7 profile averaged 340.674 ms without instrumentation and
346.774 ms with it (1.79% higher). Candidate waves occupied 160.470 ms and
averaged 5.50 busy-core equivalents within those waves. Controller preparation
occupied 55.141 ms, including 14.592 ms of native-cache requests/waits; adaptive
transitions occupied 37.478 ms. Native model updates now occupy only 20.470 ms.
These spans are nested where stated; callable overlap is not an OS scheduling
trace. Native rendering and basis FFT dominate worker CPU time. The profile
retained exact work, ordered states, posterior bytes, scores and endpoints.
[Post-native profile](../local-evidence/structural-fine/row-7-post-native-residual-attribution.json)

Mapped descriptor preparation now validates one bounded geometry bank and reuses
its extended-precision affine terms across both chronological blocks. Internal
staging accepts at most 1,024 rows; guarded fallback and public native calls remain
bounded to 128. Pin ownership and numerical evaluation order are unchanged, and
the shared two-block workspace explicitly reserves an additional 512 KiB.
A frozen four-variant comparison took 17.83 seconds, including fixture setup
and warmups. Mapped reuse reduced row-7 four-second continuation pushes after a
fixed two-second prefix from **329.192 to 301.838 ms (8.31%, four of four paired
wins)**; busy-core equivalents rose from **3.301 to 3.584**. This metric excludes
prefix, progress queries and finish. Row-4's cold two-segment maximum-state
branch improved from **222.853 to 213.626 ms (4.14%)**. Scores, posteriors,
state ordering, threshold sides, native fills and logical references matched
exactly. These are fine-stage gains, not complete-receiver throughput.

The same comparison rejected a shared timing-transition plan: it halved stencil
enumeration but failed its prospective standalone and incremental gain gates.
Typical elapsed time averaged 7.31% and 9.19% higher; this does not isolate that
entire difference as transition cost. The transition implementation, its tests
and altered work-count guard were restored to the baseline. No favorable rerun
was taken. The private comparison driver was removed after independent audit;
retain its compact receipt and audit while they support this mapped optimization
and the rejected alternative. Baseline variants shared the patched constructor
workspace allowance, so this is not a whole-HEAD memory comparison.
[Fine reuse comparison](../local-evidence/structural-fine/transition-mapped-plan-qualification.json) ·
[Independent audit and provenance](../local-evidence/structural-fine/transition-mapped-plan-audit.json)

A subsequent small reuse change shares repeated mapped-span pointer tables and
avoids repeated lookup of tiles already pinned in a block. Six matched pairs
preserved exact scores, posterior state, native/cache counters, tile contents
and LRU order, with unchanged measured sources and binaries. Row-7 continued
fine pushes averaged **168.448 → 166.591 ms (1.10%)**, with two of four paired
wins; row-4 capped pushes averaged **215.130 → 209.564 ms (2.59%)**, with one of
two wins. These noisy point observations support retaining the bounded removal
of duplicate work under the user's incremental-improvement policy; they do not
establish precise speedups or whole-receiver saturation. Both variants used six
workers and the same constructor/workspace formulas. The single qualifier took
4.37 seconds; independent audit accepted its exactness and provenance. Private
experiment code is retired after audit; the compact receipt remains while this
optimization is current.
[Span-directory qualifier](../local-evidence/stage-directory/qualify.json)

An additional bounded experiment compared 12, 18 and 24 software-worker permits
on this same twelve-logical-CPU machine, with one captured stream and a frozen
5120 MiB memory cap. One seven-window screen took 124.27 seconds:

| Controllers × workers | Complete lifecycle (s) | Six quiet-slot service union (s) | Busy-core equivalents |
| --- | ---: | ---: | ---: |
| 2 × 6 | 23.893 | 18.316 | 3.846 |
| 2 × 9 | 24.691 | 18.740 | 4.350 |
| 2 × 12 | 25.795 | 19.613 | 4.236 |
| 3 × 6 | 22.334 | 16.475 | 5.265 |
| 6 × 4 | 23.421 | 16.584 | 7.045 |

These are selection observations, not five independently qualified speedups.
The selected three-controller route then received the single predeclared ABBA
comparison on thirteen windows, measuring the last six quiet slots after six
quiet settling slots. Its median selected service union fell from **18.613 to
17.889 seconds (3.89%)**, below the fixed **15% adoption gate**. Complete
lifecycle fell from **41.163 to 38.409 seconds (6.69%)**, while busy-core
equivalents rose from **4.116 to 5.778**. Both paired service ratios improved,
but the gate was not relaxed or retried. The qualifier took 160.78 seconds.
All nine looks matched exposed receiver work, scores and ordered message results
exactly; source, binaries, inputs, software permits and memory remained valid.

Selected service is the union of actual child DSP and selected parent assembly,
transport, result and commit intervals. It is not the sum of overlapping owner
walls or a commit-to-commit approximation. Raw ingress, original native setup,
owner startup and bounded shutdown are charged in complete lifecycle time.
CPU activity uses the separately matched processing monitor interval and twelve
as its hardware denominator. The original 15% gate remains rejected. The user
subsequently directed that increased productive utilization with a modest
throughput gain is worthwhile, particularly for less power-limited processors.
That changed adoption criterion justified continuing the architecture beyond a
fixed three-controller policy. It is not a relabeled statistical pass or a
favorable timing retry.

An intermediate resource-driven implementation targeted `H` independent owners and
`2H` shared software permits, with `H` determined by affinity and quota. Every
mode uses `2H` when workers are omitted; explicit caps remain exact. Owners are
created only for ready complete windows that fit conservative public-geometry
DSP bounds. Each job reserves its required workspace, leaving remaining RAM
available for later input and other jobs. Completed future results can release
their owners before ordered commit, within a separate bounded reorder buffer.
Automatic operating RAM uses cgroup-aware available memory less an OS reserve;
the old 3840/5120 MiB experimental caps and half-memory rule are not operating
limits. That intermediate source passed 137 focused DSP/planning checks, 54 parent
scheduler checks and 20 process-backend checks, with independent source review.
The required native triad group passed **22/22 in 29.86 seconds**. These checks
preserve exact allocations, complete-work memory admission, ordered two-slot
completion, lazy growth, refilling behind a slow frontier and bounded cleanup.
The earlier five-window default check failed only its private
message-event expectation; its corrected continuation was not run before this
architecture superseded that plan.
[Worker screen](../local-evidence/receiver-oversubscription/screen.json) ·
[Independent ABBA qualification](../local-evidence/receiver-oversubscription/qualify.json) ·
[Predeclared rules](../local-evidence/receiver-oversubscription/predeclared.json)

That intermediate default then passed one matched **25-window** comparison
on the same configured row-7 capture, using a shared **9,118,859,674-byte** cap
derived from available RAM. This is a new scoped comparison, not a retry of
the earlier 15% gate:

| Measured quantity | Explicit two owners / 12 permits | Automatic / 24 permits |
| --- | ---: | ---: |
| Actual maximum concurrent slot owners | 2 | **12** |
| Selected quiet-slot service union, s | 71.295 | 67.552 |
| Complete lifecycle, s | 77.991 | 77.264 |
| Average busy hardware-thread equivalents | 4.208 | **8.633** |

Automatic execution held twelve overlapping child services for **36.407 s**
and refilled owners for slots 13–24 while peers were still active. The busy-thread
average covers the whole 77.083-second processing interval, including preparation,
fill and drain; it does not measure CPU use within that full-width interval.
The single pair establishes productive increased concurrency, not a precise
statistical speedup. All 25 slots and 2,473 exposed logical/score values matched
exactly, including DATA/null decisions and ordered completion after two quiet
slots. There were no workspace retries or serial fallbacks. Modeled peak ownership
was **9.094 GB** and sampled aggregate RSS **5.399 GB**, both below the shared cap.
All 66 source hashes, both native artifacts and input hashes remained unchanged.
Independent audit accepted the receipt; the supervisor completed in **164.589 s**
within 220 s and verified no surviving owned children.
[Resource-driven comparison](../local-evidence/resource-parallelism/result.json) ·
[That revision's triad checks](../local-evidence/parallel-routes/triad-final.log)

Pre-memfd all-case public planning provides a **prospective memory admission envelope**
at the separately frozen 9072 MiB operating cap. Initial/recurring ready-window
snapshots over ordinals 0–23 and 24–47 admitted widths
**1, 7, 6, 4, 13, 4, 13, 9** for rows 1–8, sharing 24 permits. The 16.294-second
pass queried public geometry and loaded one canonical child per row to measure
bootstrap overhead; it generated no IQ or templates and performed no scoring.
These widths depend on the declared retained input and runtime reservations;
actual startup RSS and ready work must be checked again. They are neither
measured throughput nor universal maximum widths. A smaller retained input
range can admit differently. Row 1's roughly
113-minute windows make input ownership particularly costly: twelve input/IPC
reservations alone exceeded 30.9 GiB under that copied transport. The shared-input
qualification above removes those repeated copies and admits three row-1 jobs;
the other rows' widths have not been requalified for that transport. More RAM can
therefore enable materially more useful concurrency on other machines. The
older H-cap envelope remains a scoped preceding observation, not a current limit.
[Pre-memfd per-case admission envelope](../local-evidence/resource-parallelism/admission-final.json)

An earlier bounded scheduling discriminator processed two adjacent quiet
windows from **one continuous native capture** with separate persistent Python
controllers and native source owners. Both arrangements shared the same
twelve-thread budget. Two concurrent six-worker jobs reduced whole-slot bank,
DSP, IPC and ordered quiet-state service from **8.913410 to 6.481362 seconds
(27.29% less elapsed; 1.375× throughput)** compared with sequential twelve-worker
jobs. Busy-core equivalents increased from **2.94 to 4.45**. All logical work,
coverage and ordered decisions matched; the largest numerical difference was
4.44e-16. This experiment excludes production ingress and pruning, so it does
not yet qualify the integrated listener or other manual rows.
[Two-controller discriminator](../local-evidence/continuous-process/process-discriminator.json)

One predeclared width extension compared four three-worker jobs with two waves
of two six-worker jobs on four adjacent windows. Four owners reduced elapsed
time by **14.39%**, below the fixed **15% incremental adoption gate**, while
raising busy cores from 4.43 to 5.99. It also increased aggregate CPU work and
peak sampled RSS to 2.354 GB. All sixteen evaluated slots preserved logical
work and ordered decisions; maximum numerical difference was 1.33e-15. No
threshold was changed or favorable retry taken. Two owners were selected at
that stage of the investigation; this historical decision is not a portable
owner cap and does not reduce the aggregate hardware-thread budget.
The combined scheduling investigation took 107.53 seconds, including an initial
memory-admission rejection before DSP and the explicitly revised two-owner
partition. The global 3840 MiB budget was unchanged.
[Width extension](../local-evidence/continuous-process-width/process-discriminator.json)

That first production integration used the bounded two-owner scheduler.
Ordinary `push` can accept input while DSP runs; `resume(wait=True)` drains
accepted work. A lone job keeps the full visible logical-thread allocation;
two ready jobs share it. Private native owners isolate the mutable keyed basis
address, and the parent commits results in public-slot order. Runtime, input,
IPC and result ownership share the local memory reservation. Memory admission
can select a latched full-budget serial fallback. Transport failures and
interruption fail closed, and child teardown is bounded. Service accounting
unions actual background execution with controller API intervals, excludes
application polling delays and records one-time owner startup separately.

Independent source review accepted the integration; 80 focused parent/lifecycle
checks and 14 backend checks passed. The final required triad group passed
**22/22 in 32.84 seconds**, including the refill and auxiliary-memory corrections
below. The native functional gate passed in 24.15 seconds:
`001` decoded to `e`, quiet slots 3 and 4 completed the message, and quiet slot
5 was scored while listening continued. Four of six slots used the split
worker budget; there was no memory fallback. A truncated-input control remained
physically incomplete. An initial stdin-based launcher failed before DSP because
spawn could not reopen `<stdin>`; the identical call then used a guarded script.
No production code changed to repair that launcher. A subsequent narrow
ingress correction makes buffer backpressure wait only until space is available,
so the producer can refill while paired jobs continue; explicit drain/EOF still
wait for all accepted work. Its discriminating regression is included in the
80 checks. The native functional receipt predates that refill correction and
does not measure the integrated scheduler's throughput or qualify all cases.
[Final triad log](../local-evidence/continuous-pipeline/triad-final.log) ·
[Native pipeline gate](../local-evidence/continuous-pipeline/functional-gate.json)

The production row-7 backlog comparison then passed its predeclared integration
gates in **83.43 seconds**, including setup and cleanup. ABBA used ordinary
16,384-sample `push` calls and `resume(wait=True)` on the same five captured
windows: DATA bit 0 followed by four quiet windows. Both automatic looks used
allocations **12, 6, 6, 6, 6**, with no fallback or retries. All 1,649,627 supplied
samples were covered once (274.9378 seconds of physical input). Independent
audit checked 1,856 exact fields and 392 numerical fields across twenty slot
results; every exposed numerical value matched exactly. Deeper candidate/cache
parity remains scoped to the earlier two-owner discriminator.

| Production metric | Serial 12 | Automatic two owners |
| --- | ---: | ---: |
| Median complete receive service | 21.6792 s | 18.9520 s |
| Controller/worker service union | 21.6779 s | 18.9514 s |
| Average busy-core equivalents | 2.909 | 3.566 |
| Listed-process sampled CPU time | 63.055 s | 67.585 s |

This is **12.58% less elapsed service (1.144× throughput)** with **22.62% more
productive CPU occupancy**, at 7.18% more listed-process CPU time. These CPU and
busy-core figures cover the sampled evaluator/controller/native processes;
they exclude the shared resource tracker identified below. Elapsed service
includes its startup. The comparison includes
one-time spawned-owner startup (1.255 and 1.292 seconds). Subtracting only that
measured startup gives an 18.45% reduction over the five-window service; this
descriptive calculation is not a separate sustained-throughput measurement.
Full twelve-thread saturation and other rows' parallel capacity remain unproven.
All 66 measured sources and both capture hashes stayed unchanged.
[Production comparison](../local-evidence/continuous-pipeline/production-qualifier.json)

The sampler observed 1.318 GB across its listed evaluator/controller/native
processes. A subsequent audit found that it omitted Python's shared spawn
resource tracker (12.76 MB in an isolated matching-runtime check). That omission
does not exhaust the observed margin to the 3840 MiB limit, but it exceeds the
old conservative reservation's 7.64 MB headroom. The corrected implementation
reserves at least 32 MiB for that shared helper before spawning and retains the
reservation after closing owners or falling back to serial execution. It does
not create or kill the process-wide tracker itself. Explicit serial execution
from the outset creates no tracker and has a different memory allowance from
automatic fallback.

A **14.71-second current-source continuation check passed** on the exact
929,627-sample prefix of the production capture. The three DATA/quiet/quiet
results, work and numerical fields matched the earlier comparison exactly;
allocation was 12, 6, 6, without fallback. Sampled aggregate RSS including the
tracker was 1.277 GB. The 12.70 MB tracker was covered by the 33.55 MB reservation,
and modeled peak ownership was 4,015,521,077 bytes within the 4,026,531,840-byte
limit. Source/binary identities and post-exit cleanup checks passed. This
qualifies the corrected admission and work paths; the original ABBA remains
the timing evidence for its recorded source, not a rerun of the corrected
budget path.
[Tracker-budget continuation](../local-evidence/continuous-pipeline/tracker-budget-gate.json)

The [historical two-owner execution chart](receiver-parallelism.html#two-owner-baseline) records
**206 actual events** from a fresh five-window run on `c54e2469`, using the same
1,649,627-sample capture (274.9378 seconds of physical input). It shows parent
ingress/startup/assembly/commit, both owners' nested preparation/search/fine
stages and a preselected early fine cycle in quiet window 3. It remains an explicitly historical comparison beneath the current
49-window CPU trace; the older `3b423f9` fine trace is also retained as history.

The plain control took **18.8923 seconds** and the traced run **18.8647 seconds**.
Their observed difference is −0.146%; this single ordered pair is neither a
speedup estimate nor a causal estimate of tracing overhead. Work, scores,
decisions and message state matched exactly. Both used allocations
**12, 6, 6, 6, 6**, with stable source, binary, capture and adapter hashes. The
whole bounded capture/check took **40.64 seconds**. No receiver code changed.

The timestamps show actual refill overlap: window 3 starts at 11.645 seconds
while window 2 runs until 13.377; window 4 starts at 13.386 while window 3 runs
until 17.455. The selected affine/adaptive cycles took 27.490/39.859 ms, each
with six basis jobs and six native Gaussian candidate jobs. They are fixed
early cycles, not a warmed average. Recorded call intervals include waits and
nested work; their overlap does not establish simultaneous OS execution or
full CPU saturation. Wall time uses the shared monotonic clock; thread CPU
values are independently sampled same-thread deltas, with small clock-sampling
differences possible. The chart does not qualify additional configurations or
blind reception.

Independent audit checked all event identities, nesting, dispatch/collection
order, physical coverage, source hashes and the twelve-worker permit ceiling.
Retain the compact trace while it supports this audited chart. The temporary
adapter, regenerated captures and redundant owner sidecars were removed after
acceptance (28.09 MB). Browser checks covered window navigation, interval
inspection, both fine-cycle views, historical disclosure and a 390-pixel layout;
no horizontal overflow or console warnings/errors were observed. Unique element IDs,
local links and JavaScript syntax also passed checks.
[Historical two-owner timing receipt](../local-evidence/current-timing/timing-trace.json) ·
[Clock semantics](https://docs.python.org/3.13/library/time.html#time.perf_counter)

Private pipeline drivers and raw captures were removed after independent
audit (about 44 MB). Keep the compact comparison, functional/admission receipts
and original setup failures while they support the current scheduler claims;
remove them once superseded by an equally scoped validation. An unqualified
general parallel-forecast prototype was also deleted rather than maintained
alongside the canonical sampler. It produced no qualified capacity result.

One guarded FP64 phasor-recurrence experiment reused each direct phase anchor
for 32 samples without changing interpolation coordinates. The frozen paired
comparison preserved numerical/work gates but saved only 7.53% and 6.91% of
continued fine time on rows 7 and 4, below its declared 10% adoption gate.
The variant was removed. An earlier attempt was invalidated by a concurrent
source edit; that failure remains recorded separately. No source or binary
changed during the single authorized replacement comparison.
[Rejected recurrence](../local-evidence/structural-fine/phase-recurrence-discriminator.json)

Two-tile preparation overlap failed the declared 10% useful-gain gate before
and after native scoring. Wider workspace FFT batches and Python-only bounded
peak selection were slower. Those variants and their private drivers were
removed. The installed SciPy 1.18.1 FFT backend uses DUCC, releases the GIL for
the transform and already caches plans. The repository's power-of-two-only
native FFT would increase a representative 19,600-point transform to 32,768;
there is no established benefit from replacing this backend.
[SciPy wrapper](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/scipy/fft/_duccfft/pyduccfft.cxx) ·
[DUCC plan cache](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/subprojects/duccfft/ducc0/fft/fftnd_impl.h)

A bounded fresh-start supervisor prototype was rejected and removed. It tried
ordinal-zero starts on the public 30-second grid while continuing one acquired
message. In a native test with independent receiver configuration, leading
noise and a legitimate restart behaved correctly, but the eighth attempt
mistook an active message's ordinal-one DATA for a new ordinal-zero message.
Its DATA-versus-noise score was about 59.5 million against a 21.49 threshold.
The test stopped at that first mismatch after 36.175 seconds, with unchanged
source, input and binary identities. A noise-only admission bound does not
decide competition between two signal explanations. Subtracting existing
different-window scores or selected likelihood lower bounds would not repair
that model error.
[Failed native gate](../local-evidence/continuous-fresh-native-gate.json)

The same prototype has an unsuitable search-cost structure for the long-bit
cases: complete windows every 30 seconds would give 225 starts per 6,750-second
row-1 slot, before any competing-message work. This is a hypothetical work
density, not a measured CPU forecast. Native DATA keys incorporate start epoch,
symbol ordinal and local tile even without payload encryption, so one
UTC-shifted correlation prefix cannot represent all those starts. Retain this
failed receipt and its predeclaration/log while they support the unresolved
blind-acquisition design question; the unused supervisor, emitter fixture and
associated tests have been deleted. This rejects the ordinal-zero supervisor,
not continuous bit/null reception. Further acquisition needs efficient address
hypotheses, potentially staged or Helper-assisted, with declared detection/error
bounds and incorrect-address checks. A semantic new-transmitter classifier is
not required. The configured-calendar estimates must keep the unimplemented
epoch/ordinal/timing search explicit.

Keep these compact receipts while they support the current implementation,
its numerical qualification and rejected alternatives. Private prototype
implementations have no continuing purpose. Replace raw receipts when a
matched current qualification supersedes their claim, preserving the concise
findings here. The earlier interactive report below remains a labeled baseline.

## Receiver restructuring

This section records the preceding source-pinned restructuring and the baseline
used by the interactive report. Later changes are recorded above.

That receiver stages immutable candidate plans, fuses mapped rendering,
DFT projection and sufficient statistics, and advances the two ready blocks
of a segment in one model job. Exact bit-pair geometry is evaluated once for
both independent DATA hypotheses. Global timing transitions and pruning retain
their original boundaries. Partial erasures and generic nonzero-null callbacks
keep the ordinary path. Worker failure drains submitted work, retains native
pointer owners and prevents publication of an incomplete incremental result.

At twelve workers, matched four-second continuation comparisons measured
**1.750× throughput on row 7** and **1.664× on row 4**, with 37.0% and 31.4%
less CPU time. The control disables the fused callback inside the same source
and uses the same capture, coordinates, allocation and native artifact. These
are fine-stage comparisons, not whole-service speedups or ratios against an
older calibration. Useful work, retained states and endpoints matched; maximum
lane-score differences were 5.68e-13 and 2.27e-13. Busy cores rose from 2.81 to
3.09 and from 3.02 to 3.44. **Whole-receiver saturation is still unproven.**
[Row 7](../local-evidence/structural-fine/row-7-12-final-overall.json) ·
[Row 4](../local-evidence/structural-fine/row-4-12-final-overall.json)

For the six-second row-7 session, Gaussian jobs fell from 144 to 72, mapped
worker submissions from 1,776 to 288, and physical cache hits from 18,083 to
236 while all 18,088 logical row/tile references remained. Exact bit-pair reuse
alone saved 18.9% of CPU time and 6.3% of elapsed time in its twelve-worker
comparison. The six-worker wall-time improvement was smaller and noisy; the
DC control established CPU-time savings but no reliable separate elapsed gain.
The full logical-thread default remains in force.

The preceding source's row-7 residual profile preserved work, states, scores and
endpoints in all five observations. Unmarked four-second continuations averaged
0.423 s and 3.16 busy cores. Instrumentation added about 2% on average. In the
marked path, mapping/statistics consumed 50% of elapsed time, model barriers
24%, and adaptive transitions 9%. These inclusive spans and worker CPU counters
must not be summed as independent elapsed costs. Fixed-affine setup, collection
and prediction together accounted for only 5.6%, an optimistic bound on savings
from persistence alone. A compiled batched state-update interface or safely
overlapped candidate lanes is a more substantial future restructuring, requiring
new numerical and transition-boundary qualification.
[Final residual profile](../local-evidence/structural-fine/row-7-final-residual-attribution.json)

A separate bounded chronological trace at source `3b423f9` supports the baseline
[interactive cycle chart](receiver-parallelism.html#cycle). One warm look and
four plain/traced ABBA looks took 4.34 s including fixture setup; no production
instrumentation was retained. The traced four-second continuation contains 16
candidate waves of twelve jobs, 16 model waves of three jobs and three native
cache fills. It reaches twelve overlapping worker job intervals. Within its
415.29 ms of instrumented push spans, 170.84 ms has no tracked worker job in
progress. That is neither an idle-CPU measurement nor a whole-receiver serial
fraction: controller work, native child work, gaps between wrappers and OS
scheduling are distinct. The traced mean push time was 6.84% higher than the
plain mean in these ABBA looks;
untraced controls averaged 379.32 ms. Work, ordered states, scores, endpoints
and channel posterior bytes at push boundaries matched exactly. These short
controls establish trace fidelity, not a new sustained capacity estimate.
The checked endpoint remained incomplete/ambiguous; this is not a successful
message trial. The displayed 417.03 ms axis additionally includes 1.74 ms of
probe checkpoint/digest gaps outside the push spans.
The receiver advances its phase/clock hypotheses using captured sample
timestamps, so CPU waiting does not age the captured waveform. Independent
observation or fixed-affine basis preparation can potentially overlap current
work, while adaptive state
prediction and posterior publication retain chronological dependencies. The
report's double-buffer schedule is an explicitly hypothetical model, with no
measured speedup claimed. Keep the [timeline receipt](../local-evidence/structural-fine/row-7-execution-timeline.json)
while it supports this current scheduling investigation; replace the snapshot
and remove the receipt when a changed scheduler supersedes it.

A bounded ready-lane overlap discriminator replayed a real row-7 segment's
prepared candidate work alongside the other lane's ordered model updates on
the same twelve-worker pool. Fresh model copies, pinned native owners and
49.22 MiB of scratch preserved statistics, posterior state and scores; raw
energy reduction differences stayed within the declared tolerance. Five warm
and twenty interleaved replays took 2.55 s internally. Six candidate jobs saved
a paired median 0.818 ms, below the predeclared 3 ms useful-gain gate; nine jobs
lost 0.924 ms. Even the positive result would save only about 1.5% of the final
fine slice before live planning/commit overhead. This does not justify a live
cross-lane scheduler change. The private driver was removed without changing
production sources or repeating the calibration.
[Overlap discriminator](../local-evidence/structural-fine/row-7-ready-lane-overlap.json)

Recurring integer-source bank preparation now leases each epoch/bit source
once, shares clock/ray plans and applies a native four-tap stencil in the worker
pool. The representative row-7 preparation fell from 6.628 s to 1.504 s with
source reuse, then to 0.490 s with the stencil. The final fresh stencil pair
was 1.434 s versus 0.490 s. Source requests fell from 1,248 to five. The final
twelve-worker observation averaged 3.51 busy cores; its six- and one-worker
observations were 0.396 s and 0.613 s. These single observations do not select
a smaller portable worker budget. All 78 workspace arrays agreed within
9.44e-16 peak-relative error, and DATA/H0 candidate identities, order,
coordinates and search work matched. A byte-equality gate failed and remains
recorded; the qualified result uses the declared numerical tolerance.
[Source reuse](../local-evidence/current-bank-lease/row7-pilot.json) ·
[Native stencil](../local-evidence/current-bank-lease/row7-native-stencil-paired.json)

A grouped NumPy projection variant increased CPU work without reducing
elapsed time and was removed. The post-reuse row-7 native source takes about
0.1 CPU-seconds, making further pulse-synthesis GPU acceleration a small target
there. A different FHSS coefficient/stencil interface could also remove
transport, but is not implemented or qualified. The earlier tensor-DFT
conclusion below remains applicable; no tensor speedup is inferred from this
CPU restructuring.

Large CPU observations also reuse the existing bounded FIR overlap-add path.
The branch depends on public sample count and backend, preserves finite tails,
global decimation phase and validity checks, and shares its actual buffer
estimate between ordinary and tiled acquisition. CUDA keeps its polyphase
filter. In a bounded row-1 public-geometry check with synthetic noise and a
tone, filtering fell from 6.715 s to 2.461 s and its modeled workspace from
2.940 GB to 0.342 GB. Projection relative error was 8.28e-16; full and tiled
search retained the same candidates and work. This is an operator/admission
check, not a native-channel communication trial or a complete capacity result.
[Filter qualification](../local-evidence/current-restructure/row1-streamed-observation-paired.json)

The fine and bank comparison receipts predate this isolated large-input FIR
routing change; their measured row-4/7 paths do not enter that branch. Keep
the scoped receipts, original failures and current integration results while
they support this qualification. Scratch implementations and copied sources
have been removed. Retire raw receipts when matched replacements supersede
the claims, retaining the concise algorithm and negative-result findings.

## Complete headless Robust and manual capacity

These estimates measure the historical Python-orchestrated complete receiver
with native kernels. They are retained with their original margins, failures
and applicability receipts, not as native GUI runtime calibration. New native
CPU work requires matched affected timings and complete same-stream accounting.


The retained complete-work measurements cover all eight manual rows on the Ryzen
5 PRO 5650U with **one 24-worker pool, 12 hardware threads and a 9072 MiB
workspace**. Every row includes complete work on the configured public calendar.
These are complete-work service estimates for that allocation, **not the
saturated ceiling of the automatic multiowner receiver**. Neither the row-7
pipeline gain nor its CPU occupancy is a conversion factor for the other rows.
Unknown epoch/ordinal/timing acquisition and physical sensitivity also remain
outside this qualification.

The audited union preserves the canonical measurements from three bounded
processes: rows 1–6 from the original run, row 7 from its deadline-limited
continuation and row 8 from one separately predeclared residual run. Completed
rows were not remeasured. Source, native artifacts, manual inputs, initial
runtime, allocation and the 72-comparison statistical family match; independent
audit verified component sums, row selection, workspace bounds and cleanup.
**Row 7 retains an explicit provenance limit:** its row was fully written before
the continuation was interrupted during row 8, but the canonical report has no
end-of-run source/runtime flags. External post-termination source/native/input
checks match; they do not manufacture the missing runtime completion flag.
[Current eight-row summary](../local-evidence/parallel-routes/all8-current-summary.json)

The shared-input revision retains these single-pool DSP and finite-controller
costs through an independently reviewed applicability receipt. Invoked globals
and initialization were checked as well as function bodies; only the two
pipeline modules changed, and the remaining 64 measured sources and native
artifacts match. Original timing reports and their source identities are intact.
The explicit forecast option `--cpu-calibration-applicability` validates the
exact report bytes, reviewed before/after source pair, native/runtime identity,
manual input, workers and workspace. It does not change `same_measured_source`
or loosen CUDA/reference/independent-holdout and measurement-quality gates.
All **26 forecast tests passed**, including actual CLI acceptance with unchanged
numerical output and rejection of altered bindings or scope.

The completed canonical reports for rows **1–6 and 8** are accepted through
that receipt. The actual interrupted row-7 report is rejected for its missing
completion flags, and the audited eight-row summary is rejected as a canonical
forecast input. Row 7 remains a reported manual estimate with the provenance
limit above; automated forecasting requires a completed canonical calibration.
This implements reuse of qualified CPU costs, not a newly qualified GPU forecast
or an automatic multiowner capacity estimate. No DSP timings were repeated.
[Reviewed applicability](../local-evidence/parallel-routes/shared-input-cpu-applicability.json) ·
[Source/initialization proof](../local-evidence/parallel-routes/shared-input-applicability-proof.json) ·
[Actual report-consumer checks](../local-evidence/parallel-routes/shared-input-applicability-consumer-check.json) ·
[Forecast test log](../local-evidence/parallel-routes/shared-input-forecast-tests.log)

Nominal work/model/interval gates pass on rows **2–8**. Conservative gates pass
on rows **3 and 5**. Row 4's conservative point is **4.111 s/s**, a numerical
real-time failure even though uncertainty prevents a stronger capacity decision.
Other unsupported models remain unresolved despite favorable numerical points.
Intervals are conditional extrapolation intervals, not communication-success
probabilities. Values below 1 s/s indicate less processing time than input time.

| Manual row | Nominal (s/s) | Nominal interval | Conservative (s/s) | Conservative interval | Conservative decision | Continued fine busy cores |
| --- | ---: | --- | ---: | --- | --- | ---: |
| 1 Sub-9kHz | 0.095588 | 0.00217337–221796 | 0.344345 | 0.288156–221796 | Unresolved | 2.263 |
| 2 Groundwave GPSDO | 0.082295 | 0.0660219–0.103034 | 0.328242 | 0.260634–0.410538 | Unresolved | 2.451 |
| 3 Groundwave Helper | 0.110476 | 0.0841488–0.169263 | 0.298940 | 0.221439–0.423049 | Conditional pass | 3.262 |
| 4 HF Helper | 0.170988 | 0.10633–0.261123 | 4.110555 | 0.697945–84.0643 | Unresolved; point fails | 3.335 |
| 5 HF encrypted GPSDO | 0.074096 | 0.0392398–0.123398 | 0.274246 | 0.222896–0.33994 | Conditional pass | 2.653 |
| 6 FHSS | 0.179261 | 0.116871–0.265807 | 0.506988 | 0.399053–0.65286 | Unresolved | 2.311 |
| 7 HF unencrypted GPSDO | 0.064076 | 0.0525413–0.080367 | 0.262868 | 0.196675–0.352477 | Unresolved | 2.842 |
| 8 EME | 0.168811 | 0.0738428–0.353889 | 0.583261 | 0.376338–1.19589 | Unresolved | 2.387 |

Row 1 stopped at the first material four-second continuation holdout drift above
30%; its enormous propagated upper endpoint is an uncertainty warning, not a
credible precise maximum. Rows 2 and 6 exhausted the bounded terminal-fine
model check; row 7 retained unsupported noise-only fine extrapolation, and row 8
retained unsupported terminal/state-ceiling models with a conservative upper
endpoint above real time. Row 4's broad acquisition remains unsupported. These
outcomes must not trigger repeated measurements to obtain favorable decisions.

Continued fine processing used **2.26–3.33 busy hardware-thread equivalents**
within the single pool; FHSS acquisition reached about 9.0–9.1. The separate
49-window measurement above establishes higher occupancy from independent
controllers, not a reduction in these per-case costs or all-row pipeline
capacity. The software-worker occupancy denominator is 24; hardware utilization
uses 12. Neither this laptop's power limits nor these single-pool observations
authorize reducing the automatic `2H` budget.

The first run completed in 813.032 s but its late per-row admission guard
rejected rows 7 and 8 because the frozen cap exceeded then-current free RAM.
Observed earlier workspace remained below the cap; allocator retention versus
external memory pressure was not separately attributed. A fresh-process
continuation preserved the same cap and completed row 7, then stopped at the
original 1,200-second deadline after 64.882 s. A separately predeclared row-8-only
run completed in 105.410 s within its 240-second bound. Total measured process
time was 983.324 s; the original deadline failure remains visible. All owned
processes were reaped. This is an audited union, not a fabricated successful
single canonical run. Preserve explicit-cap admission, unchanged sampling and
failed receipts when using this residual-process workflow.
[Original report](../local-evidence/parallel-routes/all8-cpu-capacity.json) ·
[Deadline-limited continuation](../local-evidence/parallel-routes/all8-cpu-missing-7-8.json) ·
[Residual row 8](../local-evidence/parallel-routes/all8-cpu-residual-8.json)

The final named `./build.sh test triad` passed **22/22 in 30.59 seconds**.
The canonical functional path still uses two scored quiet slots, preserves the
global public ordinal and continues receiving after message completion; EOF is
not successful physical completion. These checks do not establish blind
transmitter discovery or an all-case communication-success bound.

The preceding 12-worker/3840 MiB calibration and its conditional wrapper refresh
are historical. They remain only where they support the report's explicitly
labeled baseline and its source-applicability findings; they are not current
CPU timings. The refresh's exporter-path and H0 fixture failures remain distinct
from its accepted check. Private proof drivers, copied sources and redundant
per-row outputs were removed. Keep current raw reports and failures while they
support unresolved bounds; retire them when an equally scoped qualification
supersedes their claims.
[Historical sequential baseline](../local-evidence/continuous-receiver/calibration-all8.json) ·
[Historical refresh audit](../local-evidence/continuous-receiver/serial12-refresh-audit.json)

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

The completed campaign below **predates the receiver restructuring**. It
demonstrates the forecasting procedure, not current-source GPU qualification.
The new local CPU calibration must not be combined with these old GPU traces.
During the restructuring, a disposable RTX PRO 6000 host never reached SSH
readiness and was stopped and deleted. Replacement RTX PRO 6000 WK, RTX 5090
and B200 deployments were refused for unavailable capacity. No new full GPU
trace or GPU-derived CPU forecast is claimed from those attempts.

The current GPU filter uploads at most 2^20 input samples per staging block,
keeps each host block alive through stream synchronization, and drains an
exceptional submission before releasing its buffers. It preserves CUDA
polyphase filtering and explicitly budgets device input, full filter output,
cropping and standalone caller-owned host input. The revised row-1 filter
estimate is about 1.020 GB with its bank; this is an allocation model, not an
observed CUDA peak. The named GPU diagnostics passed **40/40** tests,
including fake-device copy/lifetime/interruption and exact-budget boundary
checks. Actual new-source CUDA execution remains unqualified until a usable
pod is available. CPU calibration does not include this GPU-only leaf.
[GPU regression log](../local-evidence/current-restructure/gpu-tests.log)

The earlier [eight-row RTX PRO 6000 trace](../local-evidence/current-parallel/gpu-current-full.json)
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

The earlier [full-list forecast](../local-evidence/current-parallel/forecast-current-all8.json)
covers all eight rows with source-matched local CPU costs and
[independent CPU traces](../local-evidence/current-parallel/holdout-current-all8.json).
The eight isolated holdouts took 539.759 seconds inside the drivers. Row 5 was
once refused admission before any receiver observation; its original log is
retained, and a fresh invocation admitted the same 4608 MiB cap and 12 workers.
No memory guard or timing threshold changed.

In that earlier campaign, **all eight nominal and conservative timing checks
passed**. Its forecast reports
`complete_point_estimates_with_unresolved_support` after two bounded local
phase refreshes described below.
All nominal guarded intervals
are below 1 s/s. Conservative points for rows 4, 6 and 8 fail real time, and
conservative capacity support remains unresolved for every row after the
forecast's engineering allowance. This is complete work accounting with explicit
uncertainty, not a saturated CPU ceiling or
all-case communication qualification.

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
CPU experiment drivers and the upload archive were removed. Retain that
campaign’s calibration, GPU references, holdouts, original failures and refresh receipts
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

This is the historical 7 November checkpoint; the native GUI migration is
recorded separately above.

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

This is the historical cleanup checkpoint before native GUI reconnection.

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
baseband and local headless receiver work. Those measurements do not qualify the new native GUI runtime. Physical-device
and release qualification remain separate. The
[abbreviated regression harness](triad-performance.md) measures relative changes
in declared work and cannot establish absolute receiver capacity. Its historical
smoke measurement withheld an unresolved Gaussian projection and made no
comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

A local unreleased GUI review compared `52ace0a` with
`2a0f3dd142c97909f206145cd4f4eecb1cc72dcf`. Both failed native text replay
with `receiver search limited | Simulation replay 0%`: an existing 16-entry
presentation limit discarded newly accepted output, while decoder arbitration
could retire a pending identity without a separate presentation notification.
The old receiver is retired; this finding does not establish the behavior of the
current receiver. Age-based eviction and treating retirement as physical
completion would violate the historical pending/physical-completion contract.
The same review reported an independently inherited ALSA clipping failure.
Its other host and SDK checks and exact historical scope are in Git history;
they do not qualify current sources or releases.

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
