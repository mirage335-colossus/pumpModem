# Maintained triad performance coverage matrix

**Before adding or expanding a row, follow the
[required efficient regression workflow](triad-performance.md#required-efficient-regression-workflow).**
The goal is comprehensive coverage of distinct code paths with minimum routine
computation. Each row must connect its cheap check to a defined full workload
and either independently validated prediction evidence or direct measurement.
Record gaps explicitly. Do not fill the matrix with equivalent parameter sweeps,
repeat unaffected qualification, or mistake a short execution witness for a
full-workload forecast. The decision thresholds and confidence semantics live
in the [performance policy](triad-performance.md#confidence-and-material-slowdown).

This inventory defines the maintained isolated v3 scope for the performance
prediction work. The machine-readable rows and stable row IDs are in
[`tools/triad-performance-coverage.json`](../tools/triad-performance-coverage.json).
`branch_row_ids` point to instrumented source-file coverage records; these are
compiler or Python control-flow observations, not semantic proof. The
`regime_row_ids` name computational regimes that must be represented by a
functional check and a complete workload or an explicit unresolved status.
Branch IDs use `native.` or `python.` followed by the exact source path, with
directory separators replaced by dots, from the retained coverage report.
Regime IDs are the maintained semantic obligations; an ID by itself does not
assert that the branch ran.

The inventory follows the active v3 implementation, not every historical
experiment. v1 and v2 are superseded and excluded. Outer campaign scheduling,
plotting, evidence rendering, and file packaging are not DSP workload rows.
Maintained likelihood, projection, channel, precision and receiver functions
remain in scope even when implemented in a study script and reused by an
adapter. The native `triad-long` CLI remains part of complete-pipeline timing.

## Fast guard versus qualification

### Random-channel and joint-receiver extension

The [random-channel study](triad-stochastic.md) adds explicit stochastic ray,
moving-delay and selective-coefficient paths. Historical performance forecasts
and green guard results do **not** qualify these additions. Keep their sampled
sensitivity campaigns out of routine CI.

| Path / regime | Cheap functional check | Measured full work / remaining gap |
| --- | --- | --- |
| Fixed physical-time Gaussian field, bounded random AM, moving-delay tails and overlapping spectra | `test_triad_v3_channel` (moments, covariance, cross-rate tone response, chunk equivalence, moving phase) | Complete native HF sampled exports; no general duration/rate runtime forecast |
| Whole-bit Helper lead, waits, UTC grid, multiday arithmetic | `test_triad_helper_burst`; occasional exporter `--whole-bit-qualification` | Actual two-bit continuous source/channel export; receiver train adapter still unsupported |
| Block-first joint DATA/pilot Gaussian likelihood, masks and public spectral guards | `test_triad_dynamic_joint.py`, `test_triad_dynamic_variants.py` | Paired complete sampled scores and explicit preparation; approximate channel model, calibrated per-capture diagnostics only |
| Parallel affine state scan, serial fallback, strict FP32/FP64 | `test_triad_dynamic_scan.py` | Same-I/Q serial/scan decisions plus synchronized CPU/GPU complete-score timings; synthetic 10,000-block timing is state-only, not a full-runtime forecast |
| Tiled timing/epoch acquisition, shared projection and weighted mixtures | `test_triad_dynamic_acquire.py` | 103 actual captures ×533 hypotheses, all preparation/scoring timed; coarse pilot-sensitive timing failed and must not become a qualified sensitivity anchor |
| Independent stochastic Helper frequency selection, per-capture subsets and shared preparation | `test_triad_dynamic_frequency.py`, `test_triad_dynamic_acquire.py`, `test_triad_dynamic_frequency_study.py` | Separate coverage/admission calibration; full-prior projection and clock bank retained. See [frequency study](validation-data/triad-v3/2026-11-05-helper-frequency/README.md); measured timings are not general hardware/runtime forecasts. |
| Calibration identity, frozen independent trials, replay without regeneration | `test_triad_dynamic_campaign.py` | Gate binds effective receiver identity and source; custom projection/precision requires new calibration, not reuse of an unrelated gate |

Run the small dynamic Python modules together through unittest discovery;
this takes seconds on the reference CPU. Native exporter qualifications are
occasional waveform checks, not required for an unchanged receiver-only edit.
These checks are functional coverage, not new branch-instrumentation or
predictive-performance qualification. The report states unavailable GPU cases
separately from executed GPU parity checks.

### New Helper composition extension

The [isolated Helper integration](triad-helper.md) adds the following distinct
paths after the historical 33/36-target qualification below. Those historical
counts and source-bound coverage receipts **do not cover this extension**.
The [Helper coverage map](triad-helper-coverage.md) separates requested
configuration axes, native planning, sampled I/Q and reception claims. The
[November 5 Helper campaign report](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
binds the current frozen cohort results and their source identities; it
must not be inferred from the planned rows below.

| Path / regime | Cheap functional check | Full workload / timing route | Remaining qualification |
| --- | --- | --- | --- |
| Composite emitted source, unchanged legacy source | Native `test_triad_v3_channel`, source byte-parity check | Complete 18 kHz HF/EME native exports; process startup, generation, propagation and writes timed directly | New native CFG coverage; no runtime forecast across sample rates or longer observation sizes |
| Opt-in centered FHSS lattice and Added/interleave mapping | Native `test_triad_v3_source`, `test_triad_configuration.py` | Complete EME 66-dwell source with preserved shaped tails | Other lattice/rate combinations remain subject to planner validation |
| Production Helper placement/power/clock-derived encrypted composition | `test_triad_helper_export.py` (eight small complete captures and rejected cases) | Same complete HF/EME exports, no template regeneration per trial | Audio/RF hardware and ordinary receiver excluded |
| Whole-active source power reference, fixed bit0 gain, legacy reproduction | Native `--power-plan-only`; normal exporter smoke checks defaults/rejections | Occasional `test_triad_helper_export.py ... --power-qualification`: seven complete-source energy checks, about49 s; exact legacy and Helper-only I/Q bridge | Historical low-rate startup-window power biases invalidate nominal-C/N0 sensitivity claims; no channel/outage normalization. Quadrature refinement is an engineering diagnostic, not a proven bound. Do not repeat full source qualification for unrelated receiver changes. |
| Independent audio/RF oscillator bounds; longer FHSS ramp; retained receiver FIR tail | `test_triad_helper_export.py`, `test_triad_helper_ramp_diagnostic.py` | Matched sampled old/new ramp captures plus complete EME source and channel tails | 6 ms DATA ramp raised Helper leakage by 40.6 dB relative to the old format; it remained 64.3 dB below measured noise in the sampled fixture. This is one channel realization, not an RF frontend guarantee. |
| Joint pilot CFO/audio-clock/RX-time-scale profile and fallback | `test_triad_helper_clock.py` | Actual 36-second, 18 kHz windows; eight paired replays plus two fresh captures, direct estimator timer | 10/10 intervals covered the sampled truth and narrowed 16,803 CFO candidates to 4–6. Only two captures are independent of the convergence correction; no interval-coverage or operational-lock calibration. |
| Manifest-derived public source/channel envelope and FIR-fit guard | `test_triad_helper_study.py`, `test_triad_helper_receiver.py`, `test_triad_helper_coverage.py` | Sampled receiver construction on emitted DATA bandwidth, modeled sideband maximum, public clock/time-scale and CFO bounds | DATA Doppler must be `<450 Hz` at 900 Hz post-DDC; the selected line/source/CFO envelope must fit the actual legacy/wide FIR pass edge. This is a necessary nominal-support check, not compact-support or sensitivity qualification. |
| Independent selective-modulation rate and exact bypass | `test_triad_helper_export.py`, `test_triad_helper_campaign.py`, `test_triad_helper_coverage.py` | Native export and sampled-channel planner probes, including explicit modulation with no spread | Rate and depth are independent public controls; `None` selects the preset-derived rate, zero is an exact bypass. Planner and unit checks do not establish receiver sensitivity. |
| Continuous FIR; legacy and wide frontends; epoch/clock/rate/timing/CFO banks | `test_triad_helper_batched_receiver.py`, `test_triad_helper_study.py`, `test_triad_helper_clock_loss.py` | Named sampled HF/EME receivers; retain per-stage FFT, preparation, filter and likelihood times | The default timing stride is one 900-Hz sample; explicit lag step four reproduces historical results. The 7-clock × 3-RX-rate HF bank is a distinct regime. Finite grid probes do not bound continuous worst-case loss. |
| Rate-aware input clocks, DATA/Helper pass/stop geometry, FIR selection, tails and erasure support | `test_triad_helper_rate_aware.py`, `test_triad_helper_study.py`, `test_triad_helper_campaign.py` | Synthetic high-band direct/delayed injections through both CPU receiver backends; native exporter rate qualification only when changing rate/power geometry | Default remains 18-kHz/900-Hz `legacy`; `wide` also stays fixed at those clocks. `rate-aware` requires matching raw-rate templates/captures, selects a divisor with public DATA pass support, and stops before declared Helper pilot/auxiliary bands. A support conflict rejects planning. The [extended sampled study](validation-data/triad-v3/2026-11-05-helper-extended/README.md) separately records high-rate reception and paired CPU/GPU timings. These unit checks do not themselves establish sensitivity or qualify a runtime forecast; physical-radio sensitivity remains outside scope. |
| Long rate-aware FIR work regime and explicit source-power fallback | `test_triad_helper_rate_aware.py`; exporter power/rate qualifications as occasional checks | Record filter length, sample count, decimation, overlap-add scratch and complete source rows; measure the explicitly requested full-export target when its regime changes | Overlap-add is selected by public shape for at least 257 taps and at least four input rows per tap; charge the declared `96*(input_rows+tap_count)` byte scratch estimate, which excludes library/runtime overhead and is not an RSS bound. Power-reference v2 uses every sampled source row for rich sources or failed 0.1% quadrature refinement, capped at 4,194,304 source samples. These are resource/power-path boundaries, not receiver performance forecasts. |
| Known ADC erasures; zero-fill and continuous FIR support dilation | `test_triad_helper_receiver.py`, `test_triad_helper_batched_receiver.py`, `test_triad_helper_broad_campaign.py`, CPU/CUDA synthetic parity controls | Matched synthetic masked/unmasked observations through full FIR and receiver workspaces; actual sampled cohorts use fixed public masks | Mask is immutable and hashed into receiver/procedure identity. Invalid FIR outputs are excluded from score/Gram/residual calculations. Synthetic parity is not Helper reception, encryption, calibrated admission, or GPU speedup qualification. |
| Resident and cold CPU/CUDA FFT path, strict FP32 bulk | `test_triad_helper_gpu_bench.py` (mocked dispatch), paired sampled GPU benchmark | Same EME I/Q and bank, synchronized resident timings and separate preparation | RTX PRO 6000 WK / 4-thread EPYC EME path: about 82× resident correlations, 8× full warm comparison, and 1.6× first comparison with cold preparation. Three measured replays do not establish a confidence-qualified regression threshold. |
| Frozen adaptive admission procedure and independent holdouts | `test_triad_helper_campaign.py`, `test_triad_helper_broad_campaign.py` | Direct campaign wall including exports, loading, hashing, cached templates, shared Helper/FIR capture preparation and all paired scores | Broad sampled results and source versions are in the frozen report linked above. Do not infer daily false-admission or population sensitivity from small per-capture gates. The full global residual-confidence statistic still needs procedure-bound calibration. |
| Reachable cache keys and shared subset capacity | `test_triad_helper_batched_receiver.py` excessive-capacity and subset tests | Same low-rate capture under original failed and corrected memory planning; requested400/effective13 cache entries | Corrected CUDA batch-32 declared array bound9.291 GiB versus183.589 GiB old projection; neither is measured RSS. No runtime extrapolation across cache regimes is asserted. |
| Rate-aware pulse/ramp/protected placement, expected-spectrum calculation and fallback | `test_triad_shaping` (about 10 s: rendered bits, erasure masks, fresh-key FFTs); exporter planner-only checks | Bounded `triad_shaping_sweep.py`; optional native `--extended` source checks before sampled qualification | Expected source energy is not a hard mask or channel-output bound. Geometry and workspace failures remain explicit. No new runtime forecast is asserted. |
| Bidirectional DATA/Helper interference and late overlapping pilot tracking | `test_triad_helper_interference.py` (small synthetic controls and identity failures) | Four matched full-waveform controls with complete tails; fixed late 36 s apertures | Reuse the [shaping evidence](triad-shaping.md); do not put the multi-capture GPU campaign into routine CI. Moving pilots changes physical channel/noise exposure even when cross-interference is negligible. |

Do not add the Monte Carlo sensitivity campaign to routine CI. Reuse its frozen
fixtures and source identities; choose a small complete estimator/acquisition
target or validate a cheaper forecast before adding a timing gate. Functional
unit tests alone are not prediction anchors. A passing historical guard does
not remove the explicit new-path coverage gaps above. The factorized Helper
catalog and its native plan probes are not reception or performance forecasts;
the broad campaign harness shares source templates and per-capture Helper/FIR
preparation across lag variants to avoid paying repeated work, while scoring
each arm separately. Its synthetic CPU/CUDA parity controls check masked
receiver arithmetic only; they are not broad sampled campaign results or GPU
acceleration measurements.

The subsequent known-ADC-gap raw path is a separate maintained experimental
regime: `test_triad_helper_raw_receiver.py` checks exact masked Grams, raw noise
variance, public timing/epoch/CFO banks, dependency identities, scope/resource
rejection and CPU precision; its CUDA case runs only where CUDA is available.
The sampled [broad study](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
adds actual CUDA parity, exact failed-capture replays and fresh calibrated
receptions. It does **not** provide a general duration/runtime forecast. Its
raw FFT workspace and missing-row policy must not inherit a filtered-900-Hz
performance model. Assisted-only execution likewise reports its actual selected
bank and full-fallback work; it does not charge or invent an unaided score.

The extension's very small estimator and structure tests are appropriate for
routine functional checks. Do not put the full 17,035-CFO × 7-clock × 3-rate
HF search or sampled admission campaigns into routine CI. They are research
qualification targets. Historical 33/36-workload coverage and performance
receipts above do not measure these new Python/CUDA paths. The new RTX PRO
6000 WK anchor freezes a 397.956 s GPU full-bank forecast before a 406.015 s
independent full target (2.02% longer), and measures a separate full resident
target after 207.302 s explicit prewarming. This validates that named forecast
and timing boundary, not a future-error distribution or regression gate. The
corresponding 8,196.9 s full CPU forecast remains unvalidated; cross-hardware
transfer and unmeasured size, arithmetic or memory regimes remain unsupported.

### Current selected measurement routes

The routine guard selects a direct complete target for the current named
workloads. Native and resident FFT screens run the full named target on every
screen; the older native-duration and FFT prediction chains remain available
only where their exact source, workload, arithmetic and hardware identities
qualify them. They are historical alternatives, not a blanket route for all
cases. The sampled-family adapters screen nine CPU or twelve CUDA workloads as
direct complete receiver pipelines, including current preparation and full
scoring. They do not currently treat a short shared-composition witness as a
validated forecast of those pipelines. Planner and resource workloads also
use named full-process targets. No route in this summary establishes scaling to
unmeasured workloads or hardware.

Confirmation uses a randomized balanced four-arm block: two complete paired
measurements in opposite source order, each in fresh source execution
contexts. The fair assignment coin selects `candidate, reference, reference,
candidate` or its reverse. The two candidate runtimes and two reference
runtimes form one statistical observation; the inner paired calls are not
independent samples. The nominal 1% CI workflow allocation is conditional on
the declared randomized-slot null and no unaccounted context carry-over. This
does not provide an unconditional guarantee against arbitrary persistent
environmental regimes. Fresh-context setup and warm-up are recorded outside
each target timer where the adapter's original target boundary excludes them.

The file-only current assessments qualify all 33 CPU and 36 CUDA selected
measurement routes, separately from the guard's performance outcome. They check
the actual complete targets and all required inventory gaps. Routine command
walls were 328.074 and 461.339 seconds; the GPU profile includes extra targets
and confirmations, so these are not comparable acceleration measurements.

The current direct CPU mixed campaign used six cycles and completed in 798.082
seconds, with warm spans of 86.2–99.65 seconds. Its maximum reported trend
ratio was 1.18054; that stability result remains under review. The current GPU
server mixed forecast check completed in 807.956 seconds with three holds. The
reference actual/forecast, candidate actual/forecast and paired forecast-ratio intervals were [0.986654, 1.006105],
[0.981751, 1.029124] and [0.982916, 1.035486], respectively. These results
belong to their recorded source/build/hardware identities; they do not change
the status of previous failed forecasts or qualify other backends. Full raw
work, seals and uncertainty are retained in the [current completion record](validation-data/triad-v3/2026-11-04-performance-completion/README.md).

The normal CI/development entry point is
[`triad_perf_guard.py`](../tools/triad_perf_guard.py). It keeps a finite full-workload
screening inventory and confirms only suspects. The existing
[`triad_perf_complete.py`](../tools/triad_perf_complete.py) remains the stronger
qualification aggregator; its historical holds and prediction failures below
are unchanged. A guard outcome cannot turn those records into qualification.

| Guard family | CPU / CUDA workload count | Full timing boundary and reuse |
| --- | ---: | --- |
| Native v3 | 9 / 9 | Direct full manifest CLI simulation/trial bundle, including maximal Added Symbols; no duration extrapolation in the current screen. |
| Resident FFT | 5 / 5 | Direct complete preparation and search bank on an exact cached fixture; worker startup and fixture creation are separately recorded. |
| Sampled state/phase/grouped/RBPF/acquisition | 9 / 12 | Direct complete receiver pipelines at full manifest geometry; confirmations regenerate input and use fresh source contexts. No abbreviated pipeline forecast is selected by the current screen. |
| Planner/statistics | 8 / 8 | Fixed native or retained-workspace bundles, complete process timer. Bounds remain CPU work on GPU hosts. |
| Native resource boundary | 2 / 2 | Full paired/sequential preparation exports at the fixed 5/4 MiB boundary, with actual branch/work and byte-parity checks. |
| Functional/coverage and hardware diagnostics | Separate outcomes | Required focused tests and matched coverage receipts; five-second arithmetic/tensor probes are advisory, not extra rejection hypotheses. |

There are 33 CPU or 36 CUDA workload hypotheses before diagnostic `--case`
selection. Unsupported optional configurations remain in the frozen inventory
with explicit unavailable status; they do not count as tested. The complete
CUDA-family inventory uses FP32; the tensor family is explicitly unavailable
under forced FP64. Functional/numerical parity and public work geometry are
checked separately from timing. Private allocation estimates may differ between
revisions where explicitly normalized, but remain recorded and stable within
each revision; changed public work or algorithmic regimes are not silently
compared as equivalent.

The [guard decision contract](triad-performance.md#fast-guard-commands-and-decision-contract)
uses a workflow-wide exact binomial test on randomized balanced-block
full-runtime ratios.
It is intentionally distinct from the existing arithmetic/geometric mean
forecasts and their qualification rules. Limited assurance, missing coverage,
model applicability and hardware warnings do not become successful coverage or
qualification when the reject-only guard exits zero. An expensive nominated
case can remain unconfirmed even for a severe slowdown if its fresh full look
does not fit the remaining budget; there is no timeout-censoring rejection rule.
The test treats one four-arm block as one observation, not each of its two
internal pairs as an independent observation.
The existing source
instrumentation below remains necessary evidence; a full timing screen or
function-entry witness cannot replace it.

The [current completion record](validation-data/triad-v3/2026-11-04-performance-completion/README.md)
records the separate statistical controls, qualification and limits. The earlier
guard and failed qualification reports remain historical evidence, unchanged.

## Reading status

The current native CLI manifest provides complete simulation cases for selected
waveform, channel, and receiver combinations. They are representative witnesses,
not a Cartesian product and not proof that every listed branch ran. The existing
resident FFT cases preserve their full selected sample and search geometry, but
measure the named offline receiver boundary rather than the entire simulator.
The [sampled-family manifest](../tools/triad-performance-families.json) now
declares state, phase, grouped, RBPF, static-frequency, ray-frequency and
CFO-batch workload IDs. Each declared sampled family has named direct full-workload timing evidence in
the routine or companion case campaigns. The latest Blackwell routine completed
all twelve sampled-family cases and configured extensions. Declaration,
functional pass, direct timing, prediction validation and hardware qualification
remain separate states. An empty or pending workload list is unresolved, never a pass.

The latest CPU routine selected direct timing for `native-vlf-low-cn0`; its
separately qualified duration model remains historical and was not used on that
route. Direct measurements apply to each named case/source/hardware and do not
generalize to unmeasured geometries. The archived full-target native confirmation and
reuse limits are in the
[performance trace record](validation-data/triad-v3/2026-11-03-performance-trace/README.md).

Historical studies remain useful context, but an old duration, sampled-capture
count, or successful reception is not automatically a performance anchor. A
timing anchor is reusable only when its source, implementation, hardware,
arithmetic, workload geometry and timing boundary match the active adapter.
The retained two-day v3 execution is a historical full-stream observation, not
a source-bound full-duration timing anchor for the new prediction model. The
current records and limits are linked from the [v3 evidence index](README.md)
and [performance guide](triad-performance.md).

## Coverage and timing boundaries

Native source line/branch instrumentation and Python arc instrumentation are
collected separately from timed builds. The current [native capture](validation-data/triad-v3/2026-11-03-performance-broader/coverage/native/native-coverage-001/README.md)
rebuilt nine translation units after its guard and matched all 413 declared
dependencies. Four focused CTests, five valid `triad-long` modes, and seven
collector tests passed. It covers source, channel, receiver, statistics and CLI;
receiver rank-zero fallback and Gamma iteration-bound exhaustion were exercised.
The four core TUs report 181/183, 558/560, 185/188 and 242/244 executed/instrumented
source lines; the CLI reports 1171/1805. All CFG counters are nonnegative. The
receipt's frozen matrix hash predates later inventory metadata edits; its source
and dependency observations remain historical and are not a claim that the
edited matrix hash matches. These are compiler CFG outcomes from a dirty
worktree, not semantic coverage or release provenance.

The current [Python capture](validation-data/triad-v3/2026-11-03-performance-broader/scientific/README.md)
ran all 17 selected test files in one invocation; all exited zero in 63.194
seconds with 43 declared dependencies matched. It includes the Gaussian
design-bound implementation. The earlier 16-file attempt-04 capture remains
available as historical evidence, including the original ray comparison failure
and its corrected test-only oracle. Python arcs exclude NumPy, SciPy, Torch,
native-extension and CUDA-kernel internals; this current capture used Torch
2.8.0+cpu, so CUDA-only execution is unavailable here. Its frozen matrix hash
also predates later inventory metadata edits.
Coverage and functional checks are separate from timing. The [complete-run wrapper](../tools/triad_perf_complete.py) checks the declared focused tests and can reuse only dependency-matched coverage receipts. It reports unresolved regimes and limitations separately. Empty `required_gaps` means the row's finite named obligation is represented; it is not a universal coverage or qualification claim. The JSON matrix distinguishes required gaps from scope limitations so that an unrequested architecture cross-product is not turned into an artificial completeness gate.

The Python coverage history keeps the original failed attempt. Attempt-02's ray-frequency assertion compared different covariance models. The test now retains the ray raw oracle at `3e-12` and compares the static result against the identical inflated model; the corrected test passed seven checks. Attempt-03 remains a split-run reconciliation; attempt-04 was the then-fresh passing capture and is now historical behind the current 17-file capture above. No DSP source changed for this test correction.

The H200 path witness completed 11 applicable declared path contracts in 7.916 seconds; it is not a full-workload timing. Four CUDA phase-family functional files passed; the tensor test included 48.936 seconds of cold JIT. The separate arithmetic/health profile completed in 60.219 seconds: eleven arithmetic cases passed and two FP8 cases were unsupported by the compiler instruction guard. These are functional/capability witnesses, not throughput qualification.

## Timing evidence and qualification limits

The latest [confidence-decision run](validation-data/triad-v3/2026-11-03-performance-decision/README.md)
completed the same 23 CPU target gates in **741.035 seconds**, all passing,
with 16 functional commands passing and applicable retained coverage receipts.
That archived run used a per-test one-sided 95% decision at the 1.30 ratio
threshold. The qualification-oriented normal profile uses 85% at 1.30; its legacy CI
profile uses 70% at 1.70, each retaining a separate 5% per-test false-regression
budget and fixed-look spending. These are not the fast guard's workflow policy.
The qualification-oriented routine target is 25 minutes. It no longer pursues a 5% runtime
half-width after that decision resolves. Historical qualification rules and
failed model evidence is unchanged. GPU lifecycle cold and mixed-campaign
qualification now exists for one RTX PRO 6000 Blackwell identity. The current
CPU direct all-nine-family campaign is measured, with its stability flag under
independent review; broad FFT future coverage is not claimed for its conditional engineering model;
that is a limitation, not an additional release-blocking gap. The normal-profile
GPU FHSS decision remains inconclusive in that historical protocol. Hosted CI
has not run. Current finite measurement-method qualification is reported above;
it does not promote those old performance/forecast decisions. The older width-driven campaigns below remain
historical evidence, not routine cost requirements.

Earlier native qualification used the duration model for `native-vlf-low-cn0`; the latest CPU routine explicitly selected `--native-direct-case native-vlf-low-cn0`. A separate stopped savings experiment measured 57.819 s for the abbreviated arm and 46.949 s for the direct arm in its first matched block. These are arm costs, not single-target runtimes; the second abbreviated guard was inconclusive, so the experiment is incomparable and supports no savings claim. Direct measurements establish their named source, binary, hardware and case timing; they do not require an unrelated extrapolation and do not generalize to unmeasured geometry. Historical full-target confirmation and reuse limits remain in the [performance trace record](validation-data/triad-v3/2026-11-03-performance-trace/README.md). Resident FFT timing measures full selected preparation and scoring geometry, not waveform/channel generation. The state adapter's outer call combines initialization, updates, transfers and finalization; its nested update timer is contained within that interval and must not be added as a separate stage.

An earlier width-driven CPU campaign measured all 23 declared targets in 2999.858 seconds. All 23 performance gates and prediction-validity outcomes passed, and all 16 functional commands passed. Eleven cases missed the 5% timing-precision target: `native-flat`, `native-vlf-low-cn0`, `native-wrong-key`, `native-frontend`, `native-ou-path`, `narrow-hf-tight`, `broad-hf-timing`, `vlf-850-day`, `gaussian-multiblock`, `grouped-multitile` and `rbpf-systematic`. Nine sampled cases consumed 2120.456 seconds. The run exited with status 2 under its original blocker set: cold-latency scope, inventory/dependency-coverage and hardware-health blockers, plus the then-current 5% warm-precision classification. The wrapper now reports those decisions separately: all 23 performance gates and prediction-validity outcomes passed, while 11 cases still missed the 5% timing-precision goal. The complete campaign archive and per-case evidence are indexed in the [routine archive](validation-data/triad-v3/2026-11-03-performance-complete/routine/README.md). This is not whole-scope qualification.

The earlier CPU `free-crystal` same-stack holdout used 33 training samples and five full target holdouts. Its frozen forecast was 20.2642514791 seconds; the five outer full-workflow measurements were 20.2028984060, 19.5922184251, 19.4917547580, 19.8281113570 and 20.4912192290 seconds. The 97.5% relative-mean interval [-4.8870%, +1.5729%] passed the conditional ±5% holdout closure check. Training half-width was 5.3046%, above the 5% target. At that initial qualification no compatible absolute baseline existed, so its comparison gate was inconclusive. The subsequent complete CPU routine reused the new anchor and passed the strict identical-implementation historical check in a 120.954-second enclosing child; its current timing precision reached 3.96%. This does not rewrite the earlier paired-protocol rejection. These are outer `full_seconds`, not the inner score-only timer. The archived FFT identity probe is conditional historical support; its future-coverage claim remains false.

GPU evidence is case- and route-specific. GPU family-qualification 002 directly measured all 12 declared cases in 645.067 seconds; every direct case passed its gate and reached the timing-precision target. It does not qualify complete scope or cold latency. The initial GPU attempt's generic-device-index error was a setup failure, preserved separately from receiver results.

Staged GPU chronology matters. `GPU-staged-001` conditionally validated `phase-graph-hour` on H200. The 19-job fresh campaign met its minimum and was +0.27% versus warm prediction. The cached 172-state campaign missed minimum receiver wall and its last/first-quarter ratio was 1.1139, so it does not qualify sustained throughput. `GPU-staged-002` conditionally validated Gaussian and phase-optimized cases; phase-reference, phase-reduction and phase-tensor remained unqualified. A later reuse run conditionally passed Gaussian and phase-optimized but failed phase-graph receiver-stage precision. The stage-aware campaign then qualified the other three phase cases, bringing the historical total to six independently qualified staged targets. The subsequent six-case routine took 225.037 seconds and accepted four; graph failed its receiver endpoint check and tensor remained uncertain at the sample cap. A separate eight-case direct complement passed in 370.251 seconds. These two campaigns are not a measured combined routine, and successful direct measurements do not retrospectively qualify failed predictions. Direct full GPU complement cases passed their named gates, but direct timings do not qualify the failed staged prediction routes or CPU forecasts.

CPU staged confirmation left all three attempted cases unqualified. For Gaussian, forecast was 1.3424857 seconds and actual mean 1.2704221 seconds: forecast error was +5.67% relative to actual (equivalently actual/forecast minus one was -5.37%). The interval extended below the -5% equivalence edge, so equivalence was not established. The phase-reference and phase-optimized receiver intervals were also inconclusive. This staged result is distinct from the earlier interrupted CPU campaign's phase-reference underprediction by 15.8% and unstable phase-optimized fit; both original outcomes remain retained. The completed CPU campaign later measured all declared targets and passed prediction-validity gates, with the listed timing-precision misses.

A single-capture phase-cost diagnostic measured a 0.647 ms late-tail block against a 179–246 ms receiver-cost gap. Its subnormal scan found no correlation with the slowdown, and no DSP precision change followed. The retained CPU AVX2 fallback probe reports ATen capability `AVX2`, while NumPy selected `X86_V3` and `baseline(X86_V2)`; requested MKL, OpenBLAS and oneDNN ceilings are not a verified complete ISA quarantine. See the archived [dispatch record](validation-data/triad-v3/2026-11-03-performance/avx2-dispatch.json.gz).

## Preserved November 3 routine outcomes

That CPU routine completed in 1684.791 seconds (28:05), 184.791 seconds
over the 25-minute engineering target. In that original run, native/FFT/sampled
warm-target decisions passed except broad FFT historical-anchor applicability;
later focused CPU/GPU checks passed one endpoint each, but the routine was not
rerun after repair.
The complete CPU campaign report remains incomplete. The CPU direct lifecycle
run selected five cycles with `--lifecycle-direct-cycles auto`; the fixed default
is six cycles. Three full reference/candidate campaign pairs had reference
measurements 104.398, 105.470 and 101.309 seconds and candidate measurements
102.029, 101.014 and 102.101 seconds. Its six warm spans were 72.515–75.680
seconds; the one-sided upper ratio was 1.0325645. A reference last/first-quarter
ratio of 1.14435 remains a stability/environment hold after independent review
accepted the raw-only classification. Three CPU five-second hardware
screens also warned at ratios 1.356 (stream), 1.314 (phase complex64) and 1.467
(phase complex128); these raw observations are not statistically confirmed
regressions. A raw-only derived replay validates
the cold plus all-nine-family direct target/work contract; independent review
verified 21 protected timed/model/proof function bodies unchanged from 5dfa0492 and 12 raw-file hashes and accepted its classification.
The original exit/report remains unchanged, and the stability/environment hold
means this is not an original-command pass or a whole-routine pass.

The latest Blackwell GPU routine completed in 1170.758 seconds (19:31), with all
twelve sampled-family cases and configured extensions passing. The normal-profile
FHSS interval [1.285225, 1.306855] straddles its 1.30 threshold and is
inconclusive. The derived CI decision at 1.70 is supported. Neither actual hosted
CI workflow has run, and the GPU run does not establish a whole-scope pass.
The final accepted lifecycle-proof replay took 0.479253 seconds and passed its
source-transfer checks without cold or mixed flags. It ran no receiver kernels
and did not revise the original report; see the [GPU routine archive](validation-data/triad-v3/2026-11-03-performance-broader/scientific-gpu-complete-002/README.md).

The broad/free FFT cache issue was an identity mismatch in manifest metadata:
the numerical arrays matched, while timer and temporary-output metadata differed.
This is not a numerical residual failure. The strict identical FFT route requires
the exact retained `--fixture-cache`; other routine routes can use the option
when needed. The legacy FFT reuse route
prefers an eligible exact-qualified arithmetic format in FP32-then-FP64 order;
qualification makes a fresh selection. The focused GPU check passed in
33.771707 seconds ([archived details](validation-data/triad-v3/2026-11-03-performance-broader/scientific-focused-broad-001/README.md)); full reference/candidate runs took 6.261369/6.251684 seconds,
and the single prediction/full-endpoint checks passed. Its conditional normal
interval [0.898242, 1.106221] includes the historical engineering allowance and
does not support a future-coverage claim. The focused CPU check also passed one
prediction/full endpoint in 206.889412 seconds, with 33 training samples,
reference/candidate times 12.944703/13.118263 seconds, and conditional interval
[0.882159, 1.148666]. Both used qualified FP32 and preserved source, fixture and
anchor identity. These component checks do not establish future coverage or rewrite the
incomplete CPU routine. Their 206.889 s CPU component plus the original
1684.791 s routine sum to 1891.681 s (31m32s); this sum includes the original
failed broad-HF pilot, which was not rerun, and is not a newly measured
complete-run wall time. No repaired complete routine has
been measured against the 25-minute target.

For strict identical free-crystal, qualification took 47.034 seconds. One
randomized pair measured 61.620 seconds direct and 24.258 seconds abbreviated,
a nominal difference of 37.362 seconds (60.63%) and about a two-use break-even
after qualification cost. This is one pair, not a repeated savings estimate.
The initial absolute-baseline gate was inconclusive as expected; the qualified
anchor remains valid for its exact identity.

## Broader current qualification

The broader archive adds three current evidence groups. The planner/statistics
adapter completed eight full direct targets in 42.635 seconds, all passing their
complete-process and nested computation gates. The resource adapter completed
two full direct paired/sequential regime targets in 14.467 seconds, with both
upper slowdown bounds below 1.30, exact work counters, and byte parity. The
older resource forecast remains inconclusive and unapplied; direct measurement
is the selected current route, so no cross-duration prediction is claimed.

On RTX PRO 6000 Blackwell Workstation 96 GB, GPU lifecycle002 conditionally
qualified the frozen F+10W mixed-campaign forecast using three full paired holds
with at least 60 warm seconds each. It also measured process-cold and private
empty-Triton-cache full-shape starts under its recorded cache contract. This is
one backend/stack identity. Seven older H200 staged proofs replayed exactly on
their matching numerical stack; that result is separate and does not transfer
H200 runtimes to Blackwell.

The historical CPU lifecycle F+6W forecast remains inconclusive: its paired
interval [0.890392, 0.938725] fell below the [0.90, 1.10] equivalence band. The
current direct campaign selected five cycles with `auto`; the fixed default is
six. It retained six warm spans from 72.515 to 75.680 seconds and a one-sided
upper ratio of 1.0325645. A reference last/first-quarter ratio of 1.14435 remains
an unresolved stability/environment hold. Independent review accepted the
raw-only derived replay after verifying 21 protected timed/model/proof function bodies unchanged from 5dfa0492 and 12 raw-file hashes;
it validates the cold and all-nine-family direct target/work contract, but the original CPU
routine remains incomplete; this does not validate the historical F+6W forecast.
The current archive is indexed in the [broader qualification
record](validation-data/triad-v3/2026-11-03-performance-broader/README.md).

## Finite gaps and limitations

The JSON inventory separates finite `required_gaps` from `limitations`. Planner and resource rows have accepted direct full-target receipts. The direct CPU lifecycle campaign now has an all-nine-family measurement; independent review accepted its raw-only classification after verifying 21 protected timed/model/proof function bodies unchanged from 5dfa0492 and 12 raw-file hashes. The original report and exit remain unchanged, and the 1.14435 stability/environment hold still prevents calling it a routine pass. The historical CPU F+6W forecast failure remains preserved and unused by direct mode. The 741.035-second decision routine measures aggregate runtime cost, not the frozen mixed-campaign sequence. Existing named full targets and preflight/tile-tail checks remain valid.

The CPU staged forecast failures and GPU graph/tensor routine endpoint outcomes remain attached to those specific prediction routes; earlier H200 staged proofs are retained separately and replayed only on the matching stack. Direct full measurements satisfy named target timing obligations without requiring an abbreviated model. Missing the 5% timing-width diagnostic remains separate from the regression decision. The grouped 6/8-to-24 CFO forecast is unqualified; its full direct measurements are valid. The RBPF direct case is one particle-count/resampling point, with no general scaling model claimed.

GPU lifecycle002 now conditionally qualifies the declared process-cold and private-empty-Triton-cache full-shape checks for the RTX PRO 6000 Blackwell Workstation, with the OS page cache and global driver caches retained. The earlier cold tensor functional JIT timing remains a functional observation, not a full-target cold measurement. This qualification does not transfer to H200, CPU, another software stack, or unmeasured cache state. No exhaustive memory tiers, allocator/spill/OOM sweep, multi-day processor wall, thread/concurrency ladder, all-channel Cartesian product, all-device transfer, or universal cross-library dispatch claim is required or established. Channel interactions outside named representative cases and physical-radio behavior remain limitations. Paired source ratios can cancel common environment slowdowns. Actual-helper synthetic controls demonstrate a hardware warning path, not universal physical detection; selected backend probes do not cover every startup/import/disk activity or all CPU stages of GPU-hosted pipelines. Keep hardware health, functional correctness and model validity separate. Direct complete measurements remain valid for their named targets even where an extrapolation route or timing-precision target is unqualified.

