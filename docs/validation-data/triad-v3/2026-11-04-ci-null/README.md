# CPU CI same-build null-blocking qualification

This record reports a fixed 64-workflow same-build experiment of the unchanged
CPU CI performance workflow. It estimates false blocking under the tested
allocation and software conditions; it does not measure performance-regression
detection power or qualify modem performance.

## Result

All 64 scheduled workflows completed. The unit suite and routine CPU guard
produced **zero all-cause blocks**, zero statistical rejections, zero
functional/integrity failures and zero infrastructure-incomplete outcomes.
Three workflows nominated a case for confirmation; all three confirmation
sequences stopped for futility, with no confirmed regression or exhausted
budget. Across the three workflows, four individual case episodes were
nominated. The baseline is excluded from all rates.

For the predeclared single all-cause workflow-block endpoint, 0/64 gives an
exact one-sided 95% binomial upper bound of **4.57%**, conditional on treating
the 64 allocation-cluster outcomes as independent and representative of the
relevant CPU CI allocation population. That conditional bound is below 5%.
It is not a proven future bound: physical host identity and placement were not
verified, and separate allocations may share hardware. With physical grouping
unknown, the conservative one-sided upper bound is **95%** (one possible
independent host group). The experiment therefore does not establish an
unconditional false-block rate below 5%, nor does it establish the guard's
nominal 1% whole-workflow false-regression budget as a suite-level rate.
Simultaneous diagnostic
bounds for the additional outcome categories are wider and are not the primary
whole-suite endpoint.

The 64 allocations each exposed eight usable vCPUs. The allocation mix was 49
compute-class (16 GiB), 13 general-class (32 GiB) and 2 CPU5G-class (16 GiB).
Observed processor models were
AMD EPYC 9754 (11), 9655 (4), 9B45 (6), 9655P (21), 9654 (12), 4564P (2) and
9965 (8). Anonymous placement strata were datacenter-a (31), datacenter-b
(21), datacenter-c (11) and datacenter-d (1). These are allocation and model
observations, not verified independent physical hosts. Three scheduled results
were retrieved over a proxy transport; each still corresponds to its original
single scheduled workflow.

## Workflow coverage and timing

Every allocation executed the unchanged full Python discovery command: **519
tests discovered, 516 executed, 3 skipped**. The three skips were fixture
checks requiring optional environment variables not set in the frozen CI unit
stage: two planner retained-input/native-driver checks require
`TRIAD_PLANNER_BOUNDS_WORKSPACE` and `TRIAD_PLANNER_BINARY`, and one sampled
native family-export check requires `TRIAD_PERF_NATIVE_BINARY`. The skips are
reported as unexecuted coverage, not passes. The separate routine guard ran all
**33/33 screens** and **18/18 functional commands** on all 64 workflows. Each
run retained source and binary integrity identities and the full stage logs.

The validation schedule was frozen before the 64 observations; no executed
workflow was retried or replaced. The separately collected baseline is
excluded. Observer wall time, which includes the workflow setup/identity checks but not
provisioning or build, was 224.916 / 289.391 / 461.515 seconds (minimum /
median / maximum); the sum over 64 allocation runs (up to eight concurrent)
was 19,631.387 s.
The measured unit-plus-guard stage wall was 223.573 / 287.839 / 458.327 s,
with a sum of 19,508.723 s. Within those stages, unit tests took 18.094 /
27.029 / 56.388 s (minimum / median / maximum; total 1,831.915 s), and the
guard process took 205.026 / 256.569 / 417.670 s (total 17,676.702 s).
Observer overhead beyond those measured stages was 1.200 / 1.744 / 3.188 s
(minimum / median / maximum; total 122.665 s). These totals are elapsed
processing time summed across per-allocation workflows, not CPU-seconds and
not elapsed wall-clock campaign duration.

The separately collected baseline took 285.495 seconds at the observer
boundary, including 28.8 seconds of source/stack preflight; its unit-plus-guard
stages took 256.695 seconds. It used the normal CI branch and an empty
first-run fixture cache. It is excluded from the 64 validation outcomes.
Five separate native positive-control cases took 19.107 seconds in total and
showed the expected wrapper-delay controls for general slowdown,
full-duration-only slowdown, changed work and environmental slowdown. Injected
delay was outside the inner receiver timer but inside the CLI wall-time measure;
these controls do not simulate physical kernel slowdown. They are not part of
the null-blocking denominator and do not estimate detection probability.

The routine guard reported advisories because native/Python instrumentation
receipts and a known-good hardware baseline were not supplied. Raw five-second
hardware probes did run. These advisories did not omit any guard screen and did
not change the recorded outcome. The three unit-test fixture skips remain an
explicit functional-coverage limitation.

Two pre-campaign setup allocations were abandoned before any scheduled test.
The scheduled cluster-039 allocation recovered once at startup before its
workflow began; it ran its single scheduled workflow without test retry or
replacement. Setup incidents do not enter the 64-workflow outcome denominator.

## Frozen identities and reproducibility

- Source commit: `6b0f3cc8ffd5c39351eee46ff395b95c80ef6b61`.
- Source archive SHA-256: `f3d86fb17e3a96498fd7dd3cdd7aa43859ee22470bae8318c82d1e3a2ca2a3a5`.
- Frozen specification: 519 discovered unit tests, the normal CI guard command,
  fixed 64-cluster schedule, unchanged observer, 8-vCPU allocation, and pinned
  build identities. The source-file SHA-256 map is retained with the evidence.
- Frozen observer SHA-256:
  `886395041167cbae876dc559e0b316be6dc53433100176b15e2553616c78bc48`.
- The maintained observer has a separate explicit expected-unit-count option;
  it was not used to alter or regenerate this campaign's frozen specification.
- Native binaries were built in Release with Ubuntu GCC 13.3, `-O3 -DNDEBUG`,
  `DATAPUMP_PORTABLE=OFF`, and no explicit architecture/ISA compiler flags.
  Python AVX2 library-dispatch controls do not prove all native code is
  compatible with older CPUs; direct target-CPU execution remains necessary.

The compact scientific archive
[`triad-ci-null-64-scientific.tar.xz`](triad-ci-null-64-scientific.tar.xz)
contains the frozen source map/spec/schedule, raw unit/guard outputs and
measurements for all 64 runs plus the excluded baseline, the independent
frozen-observer analysis, positive controls, build overlay, allocation metadata
and SHA-256 inventory. The 22.7 MB source archive is not duplicated; its hash
and complete file map are retained. Provider/account identifiers, connection
endpoints, operational logs and billing records are excluded. The archived
analysis keeps allocation records distinct from the SHA-256 hashes of those
records.

The archive digest is recorded in the adjacent
[`triad-ci-null-64-scientific.tar.xz.sha256`](triad-ci-null-64-scientific.tar.xz.sha256)
file. After extracting the archive, replay the frozen analyzer with the pinned
Python 3.12 / SciPy environment:

```sh
python3 frozen-inputs/triad_perf_ci_null-frozen-886395.py analyze \
  --manifest inputs/manifest.json \
  --observations validation baseline \
  --output analysis-replay.json
```

## Interpretation and use

Treat this as an occasional empirical qualification of the unchanged CI suite,
not a routine CI dependency. Do not repeat the cloud campaign for small
documentation or observer changes that do not affect the frozen unit/guard
workflow. Reuse the result only when source closure, six native binary
identities, Python/dependency stack, test count and skip conditions, guard
commands and profile, fixture/cache policy, precision/thread settings, CPU
allocation size, and relevant placement assumptions still match. Changes to
those inputs require a focused impact assessment or new qualification.

Routine CI remains the unchanged unit-plus-guard route. Its observed median
workflow was about 4.8 minutes, and the slowest of these 64 runs was about 7.7
minutes, well below the 25-minute engineering ceiling. This empirical study
supports low observed blocking on these allocations; unresolved physical host
clustering limits how confidently that rate transfers to future CI.
