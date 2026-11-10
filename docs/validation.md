# Validation status

Updated 10 November 2026. The bounded native throughput/precision investigation
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
