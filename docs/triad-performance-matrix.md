# Maintained triad performance coverage matrix

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

## Reading status

The current native CLI manifest provides complete simulation cases for selected
waveform, channel, and receiver combinations. They are representative witnesses,
not a Cartesian product and not proof that every listed branch ran. The existing
resident FFT cases preserve their full selected sample and search geometry, but
measure the named offline receiver boundary rather than the entire simulator.
The [sampled-family manifest](../tools/triad-performance-families.json) now
declares state, phase, grouped, RBPF, static-frequency, ray-frequency and
CFO-batch workload IDs. Adapter code and focused functional checks exist for
some families; case declaration, a passing run, performance-model validation
and hardware qualification remain separate states. An empty or pending workload
list is an unresolved coverage obligation, never a pass.

Native case timing is direct for each named target except `native-vlf-low-cn0`,
which uses its separately qualified duration model. These are complete-run
measurements for the declared case/source/hardware; they do not require an
unrelated extrapolation to count as direct evidence, and they do not generalize
to unmeasured geometries. The archived full-target native confirmation and
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
collected separately from timed builds. The current native receipt is the
reviewed [follow-up capture](validation-data/triad-v3/2026-11-03-performance-complete/coverage/native/followup-001/README.md): nine translation units were built after the pre-build source/header guard; pre/post declared dependency inventories matched; focused CTest checks passed 4/4, regular focused tests passed 2/2, and five valid `triad-long` modes passed (`plan`, `export`, `export-sampled`, `export-stats`, `export-group-stats`). The capture includes source, channel, receiver, statistics and CLI. Receiver rank-zero fallback and Gamma iteration-bound exhaustion were exercised. The four core TUs report 181/183, 558/560, 185/188 and 242/244 executed/instrumented source lines; the CLI reports 1171/1805. All recorded CFG counters are nonnegative. These are compiler CFG outcomes from a dirty worktree, not semantic coverage or release provenance. The prior invalid focused capture remains raw audit history only.

The fresh [dependency-closed Python capture](validation-data/triad-v3/2026-11-03-performance-complete/coverage/python/attempt-04-dependency-closed/README.md) ran all 16 declared test files in one invocation; all exited zero with positive checks in 53.43 seconds. It covers 3573/4476 executable lines and 1088/1666 Python arcs. The wrapper matched all declared dependencies with no missing or changed items. Python arcs exclude NumPy, SciPy, Torch, native-extension and CUDA-kernel internals; this receipt used Torch 2.8.0+cpu, so CUDA-only execution is unavailable here.

Coverage and functional checks are separate from timing. The [complete-run wrapper](../tools/triad_perf_complete.py) checks the declared focused tests and can reuse only dependency-matched coverage receipts. It reports unresolved regimes and limitations separately. Empty `required_gaps` means the row's finite named obligation is represented; it is not a universal coverage or qualification claim. The JSON matrix distinguishes required gaps from scope limitations so that an unrequested architecture cross-product is not turned into an artificial completeness gate.

The Python coverage history keeps the original failed attempt. Attempt-02's ray-frequency assertion compared different covariance models. The test now retains the ray raw oracle at `3e-12` and compares the static result against the identical inflated model; the corrected test passed seven checks. Attempt-03 remains a split-run reconciliation; attempt-04 is the fresh passing single capture. No DSP source changed for this test correction.

The H200 path witness completed 11 applicable declared path contracts in 7.916 seconds; it is not a full-workload timing. Four CUDA phase-family functional files passed; the tensor test included 48.936 seconds of cold JIT. The separate arithmetic/health profile completed in 60.219 seconds: eleven arithmetic cases passed and two FP8 cases were unsupported by the compiler instruction guard. These are functional/capability witnesses, not throughput qualification.

## Timing evidence and qualification limits

Native complete CLI cases are direct measurements except `native-vlf-low-cn0`, which uses its separately qualified duration model. Direct measurements establish their named source, binary, hardware and case timing; they do not require an unrelated extrapolation and do not generalize to unmeasured geometry. Historical full-target confirmation and reuse limits remain in the [performance trace record](validation-data/triad-v3/2026-11-03-performance-trace/README.md). Resident FFT timing measures full selected preparation and scoring geometry, not waveform/channel generation. The state adapter's outer call combines initialization, updates, transfers and finalization; its nested update timer is contained within that interval and must not be added as a separate stage.

The completed CPU campaign measured all 23 declared targets in 2999.858 seconds. All 23 performance gates and prediction-validity outcomes passed, and all 16 functional commands passed. Eleven cases missed the 5% timing-precision target: `native-flat`, `native-vlf-low-cn0`, `native-wrong-key`, `native-frontend`, `native-ou-path`, `narrow-hf-tight`, `broad-hf-timing`, `vlf-850-day`, `gaussian-multiblock`, `grouped-multitile` and `rbpf-systematic`. Nine sampled cases consumed 2120.456 seconds. The run exited with status 2 under its original blocker set: cold-latency scope, inventory/dependency-coverage and hardware-health blockers, plus the then-current 5% warm-precision classification. The wrapper now reports those decisions separately: all 23 performance gates and prediction-validity outcomes passed, while 11 cases still missed the 5% timing-precision goal. The complete campaign archive and per-case evidence are indexed in the [routine archive](validation-data/triad-v3/2026-11-03-performance-complete/routine/README.md). This is not whole-scope qualification.

The independent CPU `free-crystal` same-stack holdout used 33 training samples and five full target holdouts. Its frozen forecast was 20.2642514791 seconds; the five outer full-workflow measurements were 20.2028984060, 19.5922184251, 19.4917547580, 19.8281113570 and 20.4912192290 seconds. The 97.5% relative-mean interval [-4.8870%, +1.5729%] passed the conditional ±5% holdout closure check. Training half-width was 5.3046%, above the 5% target. At that initial qualification no compatible absolute baseline existed, so its comparison gate was inconclusive. The subsequent complete CPU routine reused the new anchor and passed the strict identical-implementation historical check in a 120.954-second enclosing child; its current timing precision reached 3.96%. This does not rewrite the earlier paired-protocol rejection. These are outer `full_seconds`, not the inner score-only timer. The archived FFT identity probe is conditional historical support; its future-coverage claim remains false.

GPU evidence is case- and route-specific. GPU family-qualification 002 directly measured all 12 declared cases in 645.067 seconds; every direct case passed its gate and reached the timing-precision target. It does not qualify complete scope or cold latency. The initial GPU attempt's generic-device-index error was a setup failure, preserved separately from receiver results.

Staged GPU chronology matters. `GPU-staged-001` conditionally validated `phase-graph-hour` on H200. The 19-job fresh campaign met its minimum and was +0.27% versus warm prediction. The cached 172-state campaign missed minimum receiver wall and its last/first-quarter ratio was 1.1139, so it does not qualify sustained throughput. `GPU-staged-002` conditionally validated Gaussian and phase-optimized cases; phase-reference, phase-reduction and phase-tensor remained unqualified. A later reuse run conditionally passed Gaussian and phase-optimized but failed phase-graph receiver-stage precision. The stage-aware campaign then qualified the other three phase cases, bringing the historical total to six independently qualified staged targets. The subsequent six-case routine took 225.037 seconds and accepted four; graph failed its receiver endpoint check and tensor remained uncertain at the sample cap. A separate eight-case direct complement passed in 370.251 seconds. These two campaigns are not a measured combined routine, and successful direct measurements do not retrospectively qualify failed predictions. Direct full GPU complement cases passed their named gates, but direct timings do not qualify the failed staged prediction routes or CPU forecasts.

CPU staged confirmation left all three attempted cases unqualified. For Gaussian, forecast was 1.3424857 seconds and actual mean 1.2704221 seconds: forecast error was +5.67% relative to actual (equivalently actual/forecast minus one was -5.37%). The interval extended below the -5% equivalence edge, so equivalence was not established. The phase-reference and phase-optimized receiver intervals were also inconclusive. This staged result is distinct from the earlier interrupted CPU campaign's phase-reference underprediction by 15.8% and unstable phase-optimized fit; both original outcomes remain retained. The completed CPU campaign later measured all declared targets and passed prediction-validity gates, with the listed timing-precision misses.

A single-capture phase-cost diagnostic measured a 0.647 ms late-tail block against a 179–246 ms receiver-cost gap. Its subnormal scan found no correlation with the slowdown, and no DSP precision change followed. The retained CPU AVX2 fallback probe reports ATen capability `AVX2`, while NumPy selected `X86_V3` and `baseline(X86_V2)`; requested MKL, OpenBLAS and oneDNN ceilings are not a verified complete ISA quarantine. See the archived [dispatch record](validation-data/triad-v3/2026-11-03-performance/avx2-dispatch.json.gz).

## Finite gaps and limitations

The JSON inventory separates finite `required_gaps` from `limitations`. Remaining finite gaps are standalone planner/statistics timing, validation of a forecast across a representative implemented resource-regime boundary, and an independently checked mixed-family campaign-throughput forecast. The complete CPU run measures aggregate cost; it does not validate a frozen aggregate prediction. Existing named full targets and preflight/tile-tail checks remain valid, while extrapolation across an unqualified resource boundary is prohibited.

Failed CPU staged models and the later H200 graph/tensor reuse failures remain explicit limitations of those prediction routes. Direct full measurements satisfy the named target timing obligation without requiring an abbreviated model. Missing the 5% precision goal remains a separately reported outcome, not an inventory veto or a change to the existing regression gate. The grouped 6/8-to-24 CFO forecast is unqualified; its full direct measurements are valid. The RBPF direct case is one particle-count/resampling point, with no general scaling model claimed.

Cold full-shape GPU compile/capture costs were not requested in the warm-target contract and remain unqualified; the cold tensor functional JIT timing is not a full-target cold measurement. No exhaustive memory tiers, allocator/spill/OOM sweep, multi-day processor wall, thread/concurrency ladder, all-channel Cartesian product, all-device transfer, or universal cross-library dispatch claim is required or established. Channel interactions outside named representative cases and physical-radio behavior remain limitations. Direct complete measurements remain valid for their named targets even where an extrapolation route or timing-precision target is unqualified.

