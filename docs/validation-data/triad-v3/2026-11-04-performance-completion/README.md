# Three-carrier performance suite: finite qualification and efficient CI

This record supersedes the routine procedure in the [earlier guard study](../2026-11-04-performance-guard/README.md), while preserving its measurements and failures. Ordinary Robust integration is excluded. The [maintained matrix](../../../triad-performance-matrix.md) defines the finite supported workload inventory; the [performance guide](../../../triad-performance.md) defines commands and timing boundaries.

The completed routines measured all **33 CPU and 36 CUDA targets**, with 18 functional receipts each. External command walls were **328.074 seconds CPU** and **461.339 seconds GPU**. Neither run found a confirmed regression or functional/integrity failure. This is a reject-only guard, not affirmative certification of an acceptable mean runtime. Separate file-only qualification checks bind each selected measurement route to its actual full-workload receipts, instrumentation, cold/sustained evidence and source identities.

## Routine evidence and cost

| Measurement | CPU `cpu-guard-002` | GPU `gpu-guard-001` |
| --- | ---: | ---: |
| Hardware | Ryzen 5 PRO 5650U, CPUs 2/4 | RTX PRO 6000 Blackwell Server 96 GB, EPYC 9575F host |
| Library/native thread allocation | 2 / 1 | 2 / 1 |
| External command wall | 328.073893 s | 461.339101 s |
| Internal command wall | 326.665264 s | 460.697916 s |
| Declared budget | 600 s | 900 s |
| Complete workload screens | 33/33 | 36/36 |
| Functional receipts / elapsed | 18 / 57.509 s | 18 / 71.441 s |
| Hardware diagnostics elapsed | 50.478 s | 57.342 s |
| Fresh confirmation blocks | 0 | 15 |
| Functional/integrity errors; confirmed regressions | 0; 0 | 0; 0 |
| Budget exhausted | No | No |

The GPU development profile has a lower regression threshold, three additional GPU workloads, and confirmations absent from this CPU run. These overall command walls are **not** a CPU/GPU acceleration comparison. Dependency installation, compilation and the separate harness unit tests are outside these command timers. Hosted GitHub Actions has not run.

The GPU nominated resource-sequential, native FHSS/selective/burst, ray-frequency tiles, static FFT and optimized phase-hour. Each stopped by valid non-rejection/futility. The 15 blocks contain 30 fresh paired periods, or 60 arms; their inner pairs are not independent statistical observations. No statistical retry sought a pass.

All five GPU FFT cases selected eligible FP32. CPU broad-HF timing selected eligible FP64; its other four cases, including **free-crystal**, selected FP32. Free-crystal is therefore measured at its complete named geometry in both routines; this does not qualify arbitrary frequency envelopes. CPU hardware probes covered ten arithmetic/tensor cases. GPU probes completed eleven cases; both FP8 cases remained unsupported by the instruction-verification guard. Mean GPU busy was 93.51% across 286 samples, not a measurement of SM occupancy or verified tensor instruction utilization. Neither run had a matching known-good hardware baseline, so hardware health remains unqualified rather than silently passed.

## Measurement routes and prediction qualification

The current route deliberately prefers a direct complete target when model fitting and validation cost more than the work saved:

| Family | Selected current route | Retained limits |
| --- | --- | --- |
| Nine native simulations | Complete CLI simulations, including generation, channel, receiver and process completion | The qualified native VLF duration model remains a separate exact-identity alternative. |
| Five FFT acquisitions | Complete resident preparation and full search bank on cached, identified fixtures | Fixture generation/startup are separate campaign costs. Historical frequency-bank forecasts remain bounded by their validated regimes. |
| Nine CPU / twelve GPU state, phase, grouped, RBPF and frequency families | Complete generation/export, load, preparation, transfers, receiver and IPC pipeline | Worker creation is separately charged to the campaign. No unvalidated short-witness extrapolation is selected. |
| Eight planner/statistics bundles | Direct complete process | Fixed named geometry; no inference across arbitrary state/window dimensions. |
| Two native resource cases | Direct full preparation at the declared 5/4 MiB boundary | Public work, selected memory branch and output bytes are checked. |

Direct family screening uses 18 CPU source pipelines/22 exports and 24 GPU pipelines/28 exports. CPU generation and complete pipeline sums were 49.949/80.223 seconds; GPU sums were 38.528/58.446 seconds. Startup, verification and cleanup are additional measured campaign costs. The earlier shared-stage screen had no independently qualified complete-pipeline forecast. Making it direct added 24.879 seconds to the observed CPU routine, close to the prior 28-second accounting estimate, without spending several minutes qualifying a marginal saving. This before/after difference is not a controlled causal estimate.

The file-only `assess-methods` operation separately verifies full raw work, numerical/source/input identities, timing boundaries, all maintained required-gap IDs, functional receipts and dependency-matched instrumentation. It does not turn a performance non-rejection into mean-runtime accuracy, nor convert a failed historical forecast into a pass. Unknown future required gaps fail qualification. The final assessments qualified all 33/36 selected methods in **3.177/3.053 seconds** of file-only work. Current assessment results and exact source transfers are in the evidence archives below.

### Sustained CPU processing

The CPU all-nine-family campaign used six warm cycles, three fresh paired outer observations, and complete direct timing. It cost **798.082 seconds** and had warm spans **86.237–99.647 seconds**. The paired arithmetic runtime-ratio interval was **[0.701351, 1.115878]**. A reference quarter-trend ratio of **1.180543** exceeded the 1.10 stability screen and remains a review item. Direct timings include this observed variation; the failed earlier CPU `F + 6W` prediction is not newly qualified. A long simulated observation is not being substituted for sustained processor activity.

### Independently validated GPU campaign forecast

For this Server GPU/software identity, three short paired outer training observations froze a model **`F + 10W`** before three independent full paired campaign holds. `F` includes fresh-process/import, shared full input generation, device/worker setup, first calls and teardown costs; `W` is one complete warm mixed-family cycle. The full boundary includes IPC and scratch cleanup, but excludes controller initialization, fixture hashing and evidence serialization. Filesystem page cache is retained, as explicitly recorded.

Full targets took approximately 96 seconds per source, with warm spans **79.779–81.294 seconds**. At the first declared three-hold look, the 97.5% per-look arithmetic intervals were:

| Actual/frozen forecast quantity | Interval |
| --- | ---: |
| Reference total | [0.986654, 1.006105] |
| Candidate total | [0.981751, 1.029124] |
| Candidate/reference forecast ratio | [0.982916, 1.035486] |

All required metrics met the predeclared ±10% equivalence range. These form an intersection-union qualification; they are not a simultaneous confidence claim for every interval or a future error guarantee. No extra precision run followed qualification. Three short paired outer measurements cost **145.802 seconds**, compared with **578.878 seconds** for three full paired holds: **74.8% less measured work** at that outer-observation count. This is not a measured saving at matched regression-decision assurance or power; that counterfactual campaign has not run. Full one-time mixed-forecast qualification cost **807.956 seconds**, separate from cold qualification and preserved failed attempts. That cost is not free and is not repeated in routine CI.

Cold CUDA persistent-cache and private compiled-cache targets also have direct full-run receipts. Those passed in the earlier lifecycle attempt even though its overall report remained incomplete. They are reused only after raw/static/source validation. The new forecast does not transfer to the Workstation GPU, Ryzen, an i9, other arithmetic, changed work regimes or arbitrary durations without evidence.

## CI blocking and decision controls

Version 3 assigns a fresh fair source-order coin to each balanced block, either C,R,R,C or R,C,C,R. Every paired period creates fresh source contexts and performs full-case warmup outside the target timer; all setup/warmup/cleanup costs still count toward the campaign. Native/planner/resource targets already include fresh process startup and exit. Two candidate times and two reference times form **one** observation.

The CI guard spends a **1% workflow statistical rejection budget** across all frozen hypotheses, jobs and looks, at a **1.70× slowdown threshold**. GPU development uses 5% at 1.30×. The exact one-sided sign/binomial argument requires the conditional probability of a positive block contrast to be at most one half under the declared null. Random source reassignment permits source-independent serial slot variation; it does not prove that arbitrary source-dependent carry-over, thermal behavior or marginal runtime equality satisfies that null. Diagnostic replay of source-order coins disables statistical blocking.

| Decision control | Result and scope |
| --- | --- |
| Synthetic CI slot-period null, 10,000 whole workflows | 95 blocked workflows, 0.95%; exact 95% interval [0.769%, 1.160%] |
| Other tested unchanged fixed-slot/drift/general CI nulls | 0/10,000; upper exact 95% bound 0.0369% for each tested scenario |
| Synthetic CI effect 1.71×, immediately above threshold | 219/10,000 detections; **97.81% missed** in this control |
| Synthetic general/full-duration-only 2× effects | 10,000/10,000 detected in the tested models |
| Synthetic GPU development general null | 169/10,000 blocked workflows, 1.69%; interval [1.447%, 1.962%] |
| Actual native general and full-duration-only delay | Both rejected after 12 balanced blocks; the short probe missed the duration-only injection |
| Actual native neutral and equal-environment delay | Neither rejected |
| Actual changed work | Functional integrity failure, distinct from timing |

The synthetic campaign used 13 scenarios, 10,000 workflows each, and 52 real aggregator replays in 24.602 seconds. It retains the earlier persistent-source failure and compares the same latent effects after source reassignment. The five actual native controls cost 25.647 seconds. Their disclosed wrapper delay is outside the inner receiver timer but inside the full native target, validating that timer boundary. A separate outside-target preparation control remains deliberately undetectable by a resident-only timer; campaign/cold measurements address a different boundary.

An actual GPU A/A context experiment cost 193.246 seconds. Four blocks each for FFT and RBPF used distinct PIDs/contexts and verified cleanup. Replaying disclosed 190/10-ms logical slot delays over the same observations gave persistent FFT ratios 2.058–2.077 versus balanced 0.9967–1.0089; RBPF changed from 1.0538–1.0606 to 0.9968–1.0029. This validates the mechanism and plumbing, **not** a measured physical whole-suite false-block probability or a physical 19× worker slowdown.

Poor-confidence timing, prediction-model eligibility, hardware warnings and sustained trends do not each become additional unbudgeted statistical CI rejection gates. Genuine functional, source/work integrity and process failures still block. The nominal 1% allowance does not bound those failures. Expensive suspects can remain unconfirmed when the work budget is exhausted; no universal detection guarantee is claimed.

## Preserved failures and repairs

* The first GPU lifecycle attempt cost 122.069 seconds and failed paired regime validation because the numerical-library inventory arrived in a different order. A 38.494-second diagnostic confirmed identical work and scores. The repair canonicalizes only that unordered multiset; duplicates, paths, versions, threads and all other fields remain checked. Exact source-transfer proofs show the timed/model function bodies unchanged. The original failure remains retained.
* A forecast-export command rejected the then-new guard schema. Its mistakenly launched dependent CPU hold campaign was interrupted after 58.545 seconds and remains incomplete. The later point freeze is diagnostic only; no hold was promoted to qualification.
* GPU method assessment initially rejected five missing nested futility seals. The original outer summary and individually sealed sibling receipts were intact. Replay reconstructs only from those exact validated originals and requires identical bodies; it neither reseals invented data nor alters timings. Future reports preserve the original nested seals. The original failed assessment remains archived.
* Full unit discovery initially reported 514 tests with one error, one failure and three skips: an outdated 300-second workflow expectation, and a synthetic historical-source fixture assuming an unreviewed current dependency hash. Test-only fixes expect the deliberate 600-second workflow budget and retain refusal of unreviewed historical transitions. No failed historical model or runtime tolerance was relaxed.

## Reuse, limits and future testing

Run the [routine guard command](../../../triad-performance.md#fast-guard-commands-and-decision-contract) with compatible existing builds, fixtures and coverage receipts. Hosted CPU CI allows 600 seconds; the standalone CLI defaults to 300 and should receive an explicit suitable budget for this full inventory. GPU development can use 600–900 seconds, or up to the 1,500-second engineering target when suspect confirmation warrants it. Do not append old research or qualification campaigns to every correction cycle.

Use `triad_perf_complete.py assess-methods` for file-only qualification of retained full targets. Use `triad_perf_lifecycle.py` only when cold/sustained source or execution regimes need new qualification. Use the native/FFT/staged qualification tools only for a selected forecast whose validated savings justify their cost. The guide lists focused commands and source-transfer rules. Changes to work geometry, algorithms, precision, memory regimes, timed source, hardware or relevant libraries invalidate the affected anchor until checked; unchanged stages can be reused under exact dependency identity or an explicitly reviewed narrow transfer.

The finite selected measurement methods have broader evidence than execution witnesses alone. They do not constitute exhaustive NumPy/SciPy/Torch/CUDA-kernel branch coverage, qualification of every receiver configuration, physical-channel validation, release certification, or confidence-qualified predictions for unmeasured architectures. Historical failed staged/CPU forecasts remain failed. The CPU sustained trend, unavailable known-good hardware baseline, unsupported FP8 modes and unrun hosted workflow remain explicit limits.

Source-bound native and Python DSP coverage is reused across **457 unchanged source/dependency hashes**. Fresh instrumented harness discovery passed **519 tests in 36.106 seconds, with three optional native checks skipped**. Those three checks were then run with their existing native binary/workspace inputs and passed in **4.243 seconds**. The ten changed runtime/qualification modules reached **3,406/4,814 statements and 1,221/1,900 branches**; this is explicitly partial coverage. Across all loaded performance modules the counts were 8,854/12,515 statements and 3,090/4,956 branches. Python instrumentation does not cover subprocess/native, numerical-library or GPU-kernel internals. It is separate from DSP receipts and performance timing. Raw reports, commands, seeds, counters, source/build/input hashes, initial failures and reviewed transfer evidence are retained below; disposable compiled caches and regenerable I/Q arrays are excluded.

## Evidence files

| Artifact | Contents |
| --- | --- |
| [CPU routines](cpu-routines.tar.gz) | Final direct run, earlier shared-stage run, and stopped dependent hold attempt; raw events and external command records. |
| [CPU lifecycle](cpu-lifecycle.tar.gz) | Cold and fixed six-cycle direct campaign, including retained stability flag. |
| [GPU evidence](gpu-evidence.tar.gz) | Full guard, original lifecycle failure/cold proofs, independent mixed forecast training/holds/anchors, actual context control and DSP coverage receipts. |
| [Method assessment](method-assessment.tar.gz) | Final qualified CPU/GPU assessments, initial failed GPU assessment, exact original sources and narrow transfer proofs. |
| [Decision controls](decision-controls.tar.gz) | Synthetic whole-workflow controls, real native controls and the injected-control driver. |
| [Tests and coverage](unit-and-coverage.tar.gz) | Original unit failures, passing final and optional checks, partial harness coverage and 457-file DSP coverage reuse. |
| [Suite sources](suite-sources.tar.gz) | Exact isolated tools/manifests/tests and performance workflow. |
| [Source identities](source-identities.json) | Source and binary hashes and base revision; timing reports retain their own exact identities. |
| [SHA-256 index](SHA256SUMS) | Integrity checks for this record and all artifacts. |

Extract into a scratch evidence directory; original paths in receipts identify their provenance and are not instructions to overwrite current files. Exact commands and seeds are retained in routine/lifecycle records; regenerate excluded sampled arrays with those commands if needed. The [packaging script](package-evidence.py) records the selected paths and exclusions, rather than introducing another test framework. Original inputs remain tied to their recorded hashes. Fresh source-order coins are normal for a new guard; replaying old coins is explicitly diagnostic.

Operational account, credential and billing records are intentionally outside this repository. The temporary GPU Pod and its ephemeral storage were deleted after evidence retrieval; the provider showed **zero current spend**. No persistent volume was created. Cleanup is separate from scientific qualification.
