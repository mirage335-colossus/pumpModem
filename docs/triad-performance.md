# Isolated three-carrier performance regression

Start with the [maintained coverage and prediction matrix](triad-performance-matrix.md).
It distinguishes implemented adapters, measured functional paths, qualified
forecasts and unresolved regimes. The routine aggregator is
[`triad_perf_complete.py`](../tools/triad_perf_complete.py); the older runner
below remains its native/FFT component. Neither a passing component nor a green
CI status certifies the whole matrix. Ordinary Robust integration is excluded.
The [current acceptance record](validation-data/triad-v3/2026-11-03-performance-complete/README.md)
preserves the successful full-workload forecasts, failed CPU relationships,
GPU follow-up decisions and measured cost of the expanded scope. The complete
suite is not yet qualified for routine release assurance.

Use [`tools/triad-performance.py`](../tools/triad-performance.py) to measure the
experimental three-carrier receiver without integrating it into ordinary
Robust. Its evidence chain now covers complete native CLI simulations,
full-geometry resident FFT acquisition, sampled execution witnesses for named
off-line receiver paths, and a smaller Gaussian-window helper measurement.
The native and FFT duration forecasts have separate identities, timing
boundaries, anchors, and validation rules. A path witness records execution;
it is not a timing prediction or exhaustive source-line coverage.

The suite does not qualify the ordinary Robust receiver, radio sensitivity,
physical radios, unmeasured receiver families, or every numerical state. The
Python-path adapters describe their implemented scope in the
[computational-family manifest](../tools/triad-performance-paths.json); the
[native scenario manifest](../tools/triad-performance-native-cases.json)
declares the complete CLI simulation cases. Read the
[triad development workflow](triad-development-workflow.md) for functional and
sensitivity checks, and the [GPU precision guide](gpu-precision-controls.md)
before changing arithmetic. Historical validation is context, not a substitute
for evidence matching the source, build, workload, hardware, and target timing
boundary.

## Run it

For the combined suite, reuse source-matched historical anchors and compatible
builds. Qualification is a separate operation; the aggregator never silently
starts it. For example (replace the uppercase paths with existing artifacts):

```sh
python3 tools/triad_perf_complete.py \
  --reference-root REFERENCE_SOURCE --binary build/dev-cli/triad_long \
  --reference-binary REFERENCE_BINARY --anchor-directory LEGACY_ANCHORS \
  --family-route staged --staged-anchor-directory STAGED_ANCHORS \
  --fixture-cache FIXTURE_CACHE --hardware-baseline HARDWARE_PROFILE_JSON \
  --functional-defaults \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --device cpu --threads 2 --native-threads 1 --budget-seconds 600 \
  --output local-evidence/triad-performance-complete/routine-001
```

Missing anchors, unsupported hardware and unresolved inventory rows remain
explicit. This command is not a claim that all current CPU models qualify or
that complete coverage fits in 600 seconds. Use the matrix's actual results.
Do not repeatedly launch qualification to turn an inconclusive result green.

The sampled-family route measures native waveform/channel generation and file
completion, then receiver loading, preparation, transfers, processing and result
IPC. The staged route measures generation separately and complete receivers on
cached sampled inputs. It shares generation measurements only for identical
generation profiles and retains their uncertainty once. Arithmetic stage means
are additive; geometric means are not. Both whole-pipeline and individual-stage
predictions must survive independent full-run validation. The observed CPU
phase-prefix failure and CPU cache-state discrepancy prohibit treating these
models as interchangeable. Worker startup and cold JIT/graph capture remain
separate, currently unqualified timing boundaries.

The staged routine checks both individual-stage and pipeline precision. Once
a stage reaches its final permitted sample count, an interval too wide to
satisfy the existing conditional guard cannot be repaired by sampling another
stage. The routine then freezes that unresolved result and retains one full
endpoint check. It does not widen the guard, discard an outlier, or qualify a
model through early stopping. Qualification and historical proof replay retain
their original schedules. The narrow compatibility mappings for the two
recorded earlier drivers require unchanged measured code and policy constants;
they do not transfer arbitrary implementation changes.

For a named affected case, explicitly qualify on the intended backend:

```sh
python3 tools/triad_perf_staged.py \
  --reference-root REFERENCE_SOURCE --candidate-root . \
  --reference-binary REFERENCE_BINARY --binary build/dev-cli/triad_long \
  --case phase-graph-hour --device cuda --precision fp32 --mode qualify \
  --anchor-directory STAGED_ANCHORS --maximum-holds 9 \
  --budget-seconds 600 --output local-evidence/triad-performance-complete/qual-001
```

`--mode direct` measures the full target without a duration forecast (the
aggregator exposes this as `--family-mode direct`). Prefer it
when a model fails or costs more than direct measurement. The
[`sampled-family manifest`](../tools/triad-performance-families.json) declares
the concrete full workload, work dimensions and dependencies for each case.

The optional [`triad_perf_identical_fft.py`](../tools/triad_perf_identical_fft.py)
route uses one timing stream only when both workers independently report equal
loaded DSP helpers, numerical/native libraries, execution controls, inputs and
geometry. It still checks absolute full-runtime history and a full-bank
sentinel; it never invents a paired implementation ratio. It times resident
preparation and scoring, excluding generation, startup, audit and report writes.
Select only qualified cases in the aggregator with repeated
`--identical-fft-case ID`, `--identical-anchor-directory PATH` and
`--fixture-cache PATH`. Ineligible deduplication is unresolved, not a silent
fallback or a newly qualified model.

Anchor reuse is identity-specific: affected source, workload, build, arithmetic,
numerical runtime, hardware, environment, thread count, adapter, timing policy
or measured geometry changes invalidate the corresponding relationship. An
unrelated documentation edit does not invalidate DSP source hashes. Identity
deduplication is an explicitly checked narrow reuse policy, not permission to
transfer forecasts across implementations or processor architectures.

Reuse compatible builds or configure both the candidate and its intended
reference. The reference binary is needed to make a native performance
comparison; if omitted, native cases still measure a prediction but explicitly
warn that implementation comparison is unavailable.

```sh
./build.sh configure --cli --build-dir build/dev-cli -- \
  -DBUILD_TESTING=ON -DDATAPUMP_COMPILER_CACHE=OFF
cmake --build build/dev-cli --config Release --target \
  triad_long test_triad_statistics test_triad_v3_source \
  test_triad_v3_channel test_triad_v3_receiver --parallel 2
python3 tools/triad-performance.py \
  --binary build/dev-cli/triad_long \
  --reference-binary build/previous/triad_long \
  --output local-evidence/triad-performance/run-001.json \
  --device cpu --threads 2 --native-threads 1 \
  --precision-target .05 --budget-seconds 1800
```

Use Python with NumPy, SciPy and PyTorch. The
[dedicated CI workflow](../.github/workflows/triad-performance.yml) pins its
numerical stack and builds reference and candidate binaries from immutable
source revisions. `--budget-seconds` defaults to 1,800 seconds per backend and
is capped at 28,800 seconds. The preferred routine budget is five minutes; a
slow but necessary first qualification can take longer. Native build and Python
dependency setup are outside the per-backend budget; fixture creation, hardware
checks, native models, receiver measurements, witnesses and optional sustained
checks are inside it. A process-group deadline covers blocked descendants.
Incomplete or timed-out evidence never counts as a performance pass.

| Option | Meaning |
| --- | --- |
| `--profile ci` | Default required native, FFT, Gaussian-helper, and applicable path-witness cases. |
| `--profile quick` | Small smoke profile; `complete_required_coverage` is false. It cannot pass the full regression suite. |
| `--profile sustain` | Extended native/FFT scenario set; defaults to one 60-second sustained check when eligible. |
| `--precision-target .05` | Default maximum relative half-width for the family-adjusted timing intervals. |
| `--precision-target .01` | Optional tighter timing target. It may require many more independent observations; stop at the declared maximum if that target remains unmet. |
| `--looks 5,9,17,33` | Predeclared cumulative timing sample counts. All looks are included in multiplicity adjustment. |
| `--validation-mode auto` | Reuse an applicable historical proof with a fresh full paired endpoint sentinel; otherwise qualify this case. |
| `--validation-mode qualify` | Force a new frozen forecast and independent full-workload qualification where that adapter has a duration model. |
| `--validation-mode reuse` | Reuse an applicable historical proof if available; do not silently spend a new qualification campaign. A missing/incompatible anchor leaves the model unqualified. |
| `--case ID` | Focus qualification on a named case; repeat to select several. Retains the full profile’s confidence multiplicity, skips other timing/fixture work, and cannot claim complete CI coverage. |
| `--anchor-directory PATH` | Persist and find source/case/environment-bound forecast records. Default is the reusable `triad-performance-anchors` directory beside each output. |
| `--sustained-seconds N` | Request one predeclared sustained consistency check per eligible native/FFT family; accepts 0–3,600 seconds and defaults to zero, or 60 for `--profile sustain`. It is not a new CI or model qualification. |
| `--reference-binary FILE` | Binary for the paired comparison. Omitting it reuses the candidate binary and means native results cannot detect a change against a prior native implementation. |
| `--device cpu`, `cuda`, or `auto` | Measure CPU, CUDA, or available backends. Each backend receives its own budget. |
| `--hardware-baseline FILE` | Compare raw hardware probes with a compatible saved hardware profile. Missing/incompatible baselines remain unavailable, not passed. |
| `--target-profile FILE` | Retain a target profile for investigation. It does not authorize an architecture-transfer prediction. |

Select a new output path for each run; reports are not overwritten. The report
retains harness/application/binary identities, inputs, timings, selected
precision, work and geometry counters, predictions, anchors, intervals, and
unsupported/unresolved cases. Large raw reports belong in ignored
`local-evidence/`; maintained benchmark contracts and compact results belong in
`docs/`.

## What each timing prediction means

### Whole native CLI simulation

The native adapter launches the actual `triad_long` CLI for each timed sibling.
Each timed invocation includes process startup, the complete selected transmit
and sampled-channel path, natural output/filter tails, the full declared
candidate/basis work, final scoring, and output. It retains internal receiver
time, generation/setup/other pipeline time, and external startup/output time as
separate additive stages. The prediction target is external complete-test wall
time for the frozen `target_seconds`, trial count, seed protocol, complete input
rows, and fixed bank. This adapter uses the native receiver's current FP64
implementation regardless of the Python FFT precision policy.

For duration-model cases, the adapter runs two full-CLI duration siblings,
checks that they stay in the same measured work regime, fits stage rates against
actual row/basis counters, and limits extrapolation to at most eight times the
larger sibling's rows. Each independent timing observation is a block of one or
more complete CLI invocations; a discarded pilot chooses block size only. The
forecast is frozen before independent full-target validation. An invalid fit,
changed geometry, failed required witness, or unresolved prediction blocks a
supported pass. Cases without duration siblings run the full target directly.

The current profiles include the following representative durations. These
durations are simulation calendars requested of the modem, not CPU wall time.
The second duration used for training is a complete
simulation sibling, not a cropped sample array.

| Case | Training durations | Intermediate holdout | Full target | Main execution witness |
| --- | ---: | ---: | ---: | --- |
| `native-flat` | Direct | — | 262.4 s | Encrypted flat-basis simulation, complete stream, sampled white-noise path |
| `native-fhss-selective-burst` | Direct | — | 140.8 s | Two-ray HF, Each-65% perturbation, actual FHSS erasures, internal silent burst tiles |
| `native-vlf-low-cn0` | 2,304,001.6 s, 18,432,012.8 s | 36,864,025.6 s | 73,728,051.2 s | −60 dB-Hz, 0.001 Hz, 1/128 Hz point sampling; about 853 source days at target |
| `native-null` | Direct | — | 32.8 s | Complete no-transmitter/absent-candidate path |
| `native-wrong-key` | Direct | — | 32.8 s | Wrong-key candidate path |
| `native-frontend` | Direct | — | 32.8 s | Sampled quantizers, interference, continuous filter, overload recovery |
| `native-ou-path` | Direct | — | 32.8 s | Evolving OU rays, diurnal phase, outage |
| `native-gaussian-path` | Direct | — | 32.8 s | Finite Gaussian Doppler FIR path |
| `native-added-max` | Direct | — | 8 s | Maximal Added Symbols case; complete minimum private tile |

Durations come from the checked-in native manifest, with common CLI settings
(including epoch, chunk/block, and memory limits) retained in that manifest.
Small direct cases have no abbreviated prediction; they time their full target
repetitions directly. The very long VLF rows are low-rate point-sampled
simulations. Their source calendar does not imply equivalently long CPU
execution, and does not establish arbitrary multi-day path physics. These named
cases are broad witnesses, not a Cartesian sweep of all rate, shift, oscillator,
channel, modulation, burst, and spreading combinations.

Initial native model qualification uses independent full-target CLI executions on new
seeds. A frozen fit can be accepted only when the corrected equivalence
interval for mean log prediction error lies within the declared ±5% band for
reference, candidate, and their paired ratio, with matching full geometry.
Confirmation accumulates at predeclared counts of 3, 5, 9 and 17 full-target
executions per source, stopping at a supported decision or resource limit.
Every look uses a 97.5% two-sided interval and the same frozen forecast.
The per-execution relative-error check and stage comparisons remain diagnostic;
qualification concerns the mean log error, not every future execution.
An inconclusive result does not qualify the forecast. This separate confirmation
ledger is explained below; it is not the timing-family confidence ledger.

### Resident Python FFT acquisition

The FFT adapter times complete preparation plus the wrapped full scoring call
on a resident sampled fixture. Every call retains the fixture's full samples,
FFT length, lag/timing lattice, masks, preparation, templates/Gram data, rank
handling, and final score construction. It abbreviates only the number of CFO
alternatives. It predicts that same full-bank resident receiver workload; it
does **not** predict waveform/channel generation, Python interpreter startup,
a cold fixture load, the complete native simulation, or radio latency. Its
persisted geometry includes sample count/rate, FFT size, lag interval and step,
group tile, precision, and device.

A static work-count rule chooses direct full-bank measurement when training
plus validation work would consume at least 80% of the full bank. A discarded
pilot also selects direct measurement when preparation-inclusive abbreviated
work would cost at least 80% of its estimated full-call cost, or the estimated
full call is below one second. Otherwise it
fits a preparation/fixed/per-CFO cost model from short bank counts, with an
at-most-8× extrapolation bound. A fresh qualification freezes that forecast
before cumulative confirmation looks at 3, 5, 9 and 17 independent
full-bank measurements on the same complete fixture. Qualification uses the
same corrected mean log-error equivalence test as the native duration
model: the reference mean, candidate mean, and paired-ratio mean must each fit
the ±5% relative band. Individual holds are recorded, but every hold is not
required to fall within 5%. Routine reuse instead uses the conditional historical-proof procedure below,
including one fresh full endpoint pair. A direct full-bank case has no bank-size extrapolation and is tested at the full
receiver workload.

The retained Gaussian-window helper adapter measures its declared
preparation/scoring path and precision controls on its algebraic multi-block
fixture. Its family contributes a timing observation; it is not a whole
simulation or duration predictor.

### Executed path witnesses

`triad_perf_paths.py` runs separately from timing and emits function/branch
witnesses for the named computational families in the path manifest: Gaussian
state, reference/optimized phase-frequency likelihoods, CUDA graph and FP64
reduction paths, supported tensor phase/frequency paths, RBPF, grouped
acquisition, static-frequency and ray-frequency studies, and CFO batching.
The witness exercises declared branches such as empty/partial blocks, masks and
rank deficiency, tile tails, UCB, advection, reset/resampling and device paths.
Only branches available on the selected backend are applicable. A successful
witness means that the required named call ran; it is neither line/branch
coverage for every numerical state nor a duration/throughput forecast for those
algorithms. The native CLI adapter separately witnesses end-to-end channel,
waveform, encryption, erasure, burst and finalization paths in its manifest.

## Timing precision, model accuracy, and the regression gate

Keep four uncertainties distinct: (1) uncertainty in the mean runtime; (2)
variation in a future runtime; (3) error in the abbreviated-to-full prediction;
and (4) drift or transfer to another source/build/host. More timing repetitions
can shrink the first term; they do not erase the other three. Under independent
normal log timings, mean uncertainty scales as `s / sqrt(n)`, whereas a single
future observation retains the `s * sqrt(1 + 1/n)` term. The
[NIST prediction-limit formula](https://www.itl.nist.gov/div898/software/dataplot/refman1/auxillar/predlimi.htm)
makes this distinction explicit. This motivates separating mean-model error
from the individual-run scatter used by the sentinel; it does not prove
stationarity or normality for a particular machine.

The default 5% target means the maximum multiplicative half-width of the
family-adjusted confidence intervals for the reference runtime, candidate
runtime, and paired slowdown. The optional 1% target asks for a much tighter
timing estimate and can be expensive. The predeclared cumulative looks default
to 5, 9, 17, and 33 samples. At each look the suite stops when all three timing
intervals reach the target, the confirmed regression boundary is crossed, or
the final look/resource budget is reached. Reports explicitly say when timing
precision remains unresolved; they never relabel a missed target as 5% precision.

Times are analyzed on a log scale, so widths and ratios are multiplicative.
For each case family and each predeclared look, the familywise timing ledger
splits total α=0.05 across three intervals: reference, candidate and paired
candidate/reference ratio. The paired ratio interval is reused for the 30%
slowdown decision; it does not spend a second unstated α. The ledger protects
the stated families and looks under the sampling assumptions. It does not adjust
the separate per-case model-equivalence checks, claim distribution-free
coverage, or guarantee an interval on a different host. The model-equivalence
check and the family timing ledger answer different questions and must be shown
separately.

Under the current implementation, outer samples are paired timed observations
made within persistent Python worker processes for FFT workloads. They estimate
resident-call timing in that regime; they do not estimate cold-process startup
variation. Native outer samples launch complete CLI runs. Both rely on
approximately independent stationary outer log runtimes for their Student-t
intervals. The current implementation records order, work geometry, and native
stage/quarter timing; it does not robustly remove arbitrary thermal drift or
scheduler contamination. If telemetry or the run sequence shows a changing
power/thermal regime, report the issue and withhold a precision-qualified
performance result rather than treating many correlated calls as independent
samples.

A precision goal is not a validity certificate for the extrapolation. The native
and FFT adapters also retain independent validation residuals and invalidate or
withhold unvalidated models. Native and FFT model residual qualification use a
finite-look equivalence test: the confidence intervals for the reference
mean log error, candidate mean log error, and paired mean log error must each
lie wholly inside the per-case ±5% relative-equivalence band. At each of the
four predeclared confirmation looks, the two-sided interval has 97.5%
confidence: α=0.10/4=0.025. For a truly out-of-band mean, false equivalence
requires crossing its relevant one-sided boundary, with probability at most
0.0125 per look under the stated assumptions. The union across four looks
bounds false qualification at 5% per case. Requiring all three metrics to pass
is an intersection–union test; this does not claim simultaneous interval
coverage across all metrics or all cases. Every forecast remains frozen,
every look is retained, and a resource stop stays unresolved. Individual holds
are not each required to be within 5%. This is not a future-run interval or proof every
residual lies within ±5%. Observed maximum residuals are useful scenarios, not
future coverage bounds.

The 30% regression threshold is an engineering decision boundary. A
**supported pass** requires valid model qualification, direct full timing, or
an explicitly conditional historical model with its widened error allowance, and
a multiplicity-adjusted upper paired slowdown bound no larger than 1.30. A
**regression** is supported when the lower bound exceeds 1.30. If the interval
crosses the threshold, the result is **inconclusive**. If the 5% timing target
is unresolved by the last look, that fact is reported even when the slowdown
gate itself is separated from its threshold. Missing coverage, numerical
mismatch, changed geometry, invalid model, or timeout is a failure/unqualified
result, never a successful performance pass. The suite stops after its maximum
look instead of spending arbitrarily for a preferred interval. A separate
**`severe_endpoint_prediction_mismatch`** blocks CI when a failed full endpoint
underpredicts runtime by more than 30% even after subtracting its frozen
historical observation allowance and current timing uncertainty in log space.
This is an engineering **calibration failure**, not proof of a regression in
mean runtime. Its false-failure rate is unqualified: historical scatter is not
a proven future tail bound. Smaller violations, overlaps and absent history
remain visible unresolved warnings. The guard never enlarges an allowance or
retries the endpoint to obtain a pass.

## Historical proof, current speed, and the routine shortcut

Initial qualification and routine regression answer different questions.
Qualification validates the abbreviation's cost relationship with independent
full runs. Routine measurements estimate **current** stage coefficients; they
never reuse yesterday's processing speed as today's prediction.

Both adapters retain complete historical source/binary, harness and environment
hashes, frozen forecasts and raw independent full-run observations. Validators
reconstruct their predictions, errors, look order and decisions. Applicability
uses a separate explicit measurement contract. It still requires exact native
binaries or Python receiver module hashes, case/fixture/geometry, arithmetic,
timing boundary and recorded environment. Reporting-only harness changes can
reuse evidence under that reviewed contract. The specifically audited CPU002
v3 hash pairs have an explicit legacy mapping; unknown old protocols are rejected.
Original records are never rewritten or silently upgraded. Hashes provide
integrity/provenance, not cryptographic authentication of a trustworthy experiment.

A routine modeled case freezes the following before new full work:

1. Current abbreviated timing samples and the full-target forecast.
2. Verified historical full-run evidence and its applicability key.
3. Separate reference, candidate and paired **mean-model** log-error envelopes:
   the larger of a 10% engineering floor and historical mean-error interval
   endpoints. Mean-model plus current mean-timing uncertainty above 20% disables
   conditional support. Individual-run scatter does not become mean-model bias.
4. A separate **single-observation sentinel** envelope also retains the historical
   maximum absolute residual. Exactly one independent full-target paired endpoint
   runs with work and numerical checks. Its discrepancy and training uncertainty
   must fit this frozen envelope; the envelope cannot expand after seeing it.

The resulting status is **`historically_supported_conditional`**, distinct from
fresh mean-equivalence qualification. The conditional regression gate widens the
current paired timing interval once by the historical paired envelope. Absolute
full-time estimates have corresponding separate engineering intervals. One
endpoint pair does not establish a new 5% model-bias confidence bound or coverage
of future runs. These intervals rely on the recorded cost relationship remaining
applicable; the sentinel is a practical contradiction check. An inconsistent or
uncertain sentinel withholds a supported pass and requests focused qualification.
There is no automatic retry until the check passes. A single sentinel cannot
guarantee detection of every 30% full-size-only slowdown; timing noise can mask
one observation. Negative controls measure its demonstrated behavior, not
universal detection power.

This permits cheap repeated checks after reporting/instrumentation changes
outside the timed receiver. Actual timed-receiver, binary, workload, precision,
or environment changes still require qualification of affected modeled cases.
It does **not** establish arbitrary cross-revision or cross-host transfer. Full
endpoint work is intentionally retained to catch cost changes that appear only
at the full workload. Cheap cases use direct timing and need no model anchor.

`auto` reuses an applicable proof or performs first-use qualification where
budget allows. `reuse` abstains when no applicable proof exists. `qualify`
explicitly runs fresh full holds. Keep the anchor directory across runs; changing
the output filename does not require new calibration. Use `--validation-mode qualify --case free-crystal` (or another
reported case ID) for affected-case qualification; a focused run is never a
complete CI pass. CI restores anchor and
sampled-fixture caches before measurement, and only trusted default-branch runs
save reusable caches. All restored records still pass normal identity/raw-proof
validation. A hosted runner with different hardware/runtime can legitimately
miss the cache's applicability checks; a cache hit alone never authorizes reuse.

The `--sustained-seconds` consistency run uses one qualified or conditionally supported forecast
per eligible family, selected by a predeclared rule: the eligible native case
with the longest full source duration and the eligible FFT case with the
largest full CFO bank. It
commits to the expected work, identity, seed, and complete geometry before the
single run. For native, it repeats complete full-target CLI trials until the
predicted native-pipeline work reaches the requested minimum; for FFT, it
repeats complete resident prepare/full-bank-score calls. It measures the actual
receiver/pipeline span and start/end block behavior. This check has no new
confidence interval and creates no qualification anchor; it is a sustained
consistency observation. It is disabled in normal CI (`0`), defaults to 60
seconds in the `sustain` profile, and runs only for an eligible exact forecast.
If its observed span misses the planned minimum or is much slower than predicted,
the retained outcome flags the mismatch for review. It does not trigger an
automatic retry or update the anchor.

## Hardware and arithmetic qualification

The hardware layer gives each warmed kernel a five-second target and records
first-use costs separately. CPU families include FP32/FP64 GEMM,
complex64/complex128 FFT, copying, phase products/reductions, small batched SVD,
and separate FP16/BF16 dispatch probes. CUDA arithmetic and explicit tensor
formats are measured separately; dtype alone does not prove specialized
instructions ran. The receiver chooses only between implemented FP32 and FP64
after comparison with a fixed FP64 reference. Strict FP32 does not silently
enable TF32, and no tensor FFT reception claim follows from hardware tensor
probes or off-line path witnesses.

Record completed CUDA work, not just asynchronous host dispatch time. Retain
CPU process time, wall time, device event time, shape, array bytes and
throughput. CPU process time per wall second is a utilization clue rather than
an instruction-set efficiency measure. A kernel result describes its measured
shape/cache regime; it is not a peak tensor/DRAM utilization guarantee. Missing
hardware counters stay missing. The optional `nvidia-smi` sampler provides
system-wide device/memory busy, clock, power and temperature observations; it is
not SM occupancy or per-process utilization.

CI additionally restores an immutable first-observed trusted hardware profile.
Its cache key identifies physical resource allocation and requested dispatch,
but deliberately excludes kernel, numerical-library and receiver-source versions.
This lets the raw hardware screen reveal environmental slowdowns shared by both
receiver implementations. A current comparison cannot automatically replace that
reference. Different CPU allocations abstain. A five-second block is a useful
throughput/utilization screen, not an independently replicated confidence bound
or certification that the original host represented ideal performance.

On the tested RTX PRO 6000 Blackwell Server with Torch 2.8/Triton, requested
strict FP8 probes emitted FP16 tensor instructions and are reported as
unsupported, without fallback-format throughput. This is a tested
compiler/runtime result, not a claim that the GPU lacks FP8 capability.

The dedicated hosted CPU workflow requests AVX2 fallback dispatch in ATen,
OpenBLAS, MKL, oneDNN, and NumPy; it verifies physical AVX2/FMA capability and
observed ATen/NumPy dispatch. This is fallback-dispatch coverage, not complete
ISA isolation, cache/clock emulation, or binary portability certification.
The local AVX2 preflight was checked; hosted Actions results remain a separate
actual run. Library controls have build/version-specific limits: [ATen dispatch](https://github.com/pytorch/pytorch/blob/v2.8.0/aten/src/ATen/native/DispatchStub.cpp),
[OpenBLAS runtime selection](https://www.openmathlib.org/OpenBLAS/docs/runtime_variables/),
[oneDNN ISA control](https://uxlfoundation.github.io/oneDNN/v3.7/dev_guide_cpu_dispatcher_control.html),
and [NumPy dispatch inspection](https://numpy.org/doc/2.3/reference/generated/numpy.lib.introspect.opt_func_info.html).

## Routine CI, qualification, and transfer

Normal CI runs the dedicated performance job unless `devfast` is explicitly
selected; the job is separate from sanitizer/matrix repetitions. New direct
release builds depend on the exact-source performance job. A noisy or
unresolved component warning remains visible separately from a regression
failure. The combined wrapper exits nonzero when complete-scope obligations
are unresolved; component warnings cannot turn that result into a qualified
release pass. Green CI alone does not mean every model is validated or every
requested interval is narrow. Existing
ordinary-release certification remains separate. Source-release and existing
APT repackaging routes that reuse older verified binaries have not measured the
current triad study and report it as unevaluated.

The suite's routine role is to execute the declared code-path witnesses,
qualify current numerical precision/score behavior, test duration/FFT work
models against applicable historical proof and current full endpoint sentinels, measure all named performance families
to an explicit precision or budget ceiling, and sample a longer sustained span
when requested. The initial qualification campaign has a different purpose:
run expensive complete-duration and full-bank anchors on relevant host classes,
freeze forecasts before validation, and retain independent full-workload
residuals that establish how much the abbreviation misses. Historical full tests
can serve as anchors only if they preserve source/binary and harness hashes,
configuration/seed, workload counters/geometry, timing boundary, selected
precision and sufficiently comparable hardware. If that metadata is missing,
label the run historical/contextual evidence; do not calibrate with it silently.

No unmeasured architecture has a qualified runtime forecast. The current suite
explicitly marks cross-architecture transfer unqualified; a GPU GEMM rate or
hardware profile alone cannot predict the whole pipeline. Qualification on a
new host requires matched stage/work shapes, independent full-workload
validation, and a declared extrapolation envelope while retaining transfer,
serial, setup, memory/cache, and startup costs.

## Evidence and reproducible claims

The previous [2026-11-03 study archive](validation-data/triad-v3/2026-11-03-performance/README.md)
records the pre-native-duration suite, its measurements, rejected scaling model,
source snapshots and cleanup. Its 10-minute/20% policy, FFT/Gaussian adapters,
and measured host wall times are historical v1 evidence; they do not qualify the
current duration-model, adaptive-look or sustained-test implementation. Current
profile results must be retained separately with fresh source/binary hashes,
case identities, frozen prediction and full holdout checks. The separate
[traceability study](validation-data/triad-v3/2026-11-03-performance-trace/README.md)
records complete-simulation forecasts, independent confirmations, sustained
observations, failed precision targets and subsequent protocol changes.
Implementation tests alone do not establish predictive accuracy.

The suite uses paired randomized/interleaved executions because environmental
variation can dominate small timing differences. The statistical treatment
follows the benchmark hierarchy discussed by [Kalibera and Jones, *Quantifying
Performance Changes with Effect Size Confidence Intervals*](https://www.cs.kent.ac.uk/pubs/2012/3233/content.pdf)
and their [*Rigorous Benchmarking in Reasonable Time*](https://kar.kent.ac.uk/33611/): distinguish repeated measurements within a process from independent execution/process effects and allocate replication where variance and cost justify it. [Lan and DeMets' alpha-spending method](https://doi.org/10.1093/biomet/70.3.659) is a principled alternative when a variable schedule of repeated looks is needed; this implementation instead uses a fixed look schedule with conservative multiplicity adjustment. [Howard et al.'s confidence sequences](https://doi.org/10.1214/20-AOS1991) provide anytime-valid alternatives under their stated sub-Gaussian, Bernstein, self-normalized or other conditions; the suite does not claim those assumptions for arbitrary machine timing.

[Google Benchmark interleaving guidance](https://github.com/google/benchmark/blob/main/docs/random_interleaving.md)
supports spreading paired measurements across changing conditions. The current
alternating order is a bounded choice; it does not prove statistical independence
or remove thermal drift. [PyTorch's CUDA timing guidance](https://docs.pytorch.org/docs/stable/notes/cuda.html#asynchronous-execution)
requires synchronization or device events for asynchronous work. Keep timing
uncertainty, prediction residuals, future-run variation, hardware health and
architecture-transfer limits visible in every maintained conclusion.
