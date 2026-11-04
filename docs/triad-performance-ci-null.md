# CPU CI null-blocking experiment

This experiment estimates how often the fixed CPU CI performance workflow blocks
when the candidate and reference are the same build. It complements the
within-workflow guard policy with repeated runs across CPU allocations. It is
not a performance qualification, and the observed suite outcome is not evidence
that a real slowdown would be detected.

## Scope and frozen inputs

The measured workflow is the unchanged full Python unit-test discovery followed
by the unchanged routine CPU guard. The observer runs outside the tested source
tree. It records each stage's command, environment, standard output/error,
wall time, exit status and outcome. Unit-test failure prevents the guard stage
and is retained as a functional/integrity failure, not retried until it passes.
The complete workflow has a fixed outer deadline. Build, dependency installation
and provisioning are setup costs, not part of this workflow timing.

Use the frozen source archive and its full regular-file SHA-256 map, source
commit and archive hash to generate the specification. The specification also
pins the six native executables used by the guard: `triad_long`,
`triad_planner_perf`, `test_triad_statistics`, `test_triad_v3_source`,
`test_triad_v3_channel` and `test_triad_v3_receiver`. Candidate and reference
must use the same frozen source and byte-identical executables, in separate
directories. The archive and map must be prepared from the stated source
revision; do not include the observer or its tests in that source closure.

Run the observer from an operator checkout outside the frozen candidate and
reference trees. `SOURCE_FILES_JSON` is the full path-to-SHA256 file map;
`SOURCE_ARCHIVE_SHA256` must match the archive used to prepare the candidate.
The exact frozen commit and archive digest are recorded in the generated
specification, not inferred from the current working tree.

```sh
python3 tools/triad_perf_ci_null.py spec \
  --source-root CANDIDATE \
  --suite-commit FROZEN_COMMIT \
  --source-manifest SOURCE_FILES_JSON \
  --source-archive-sha256 SOURCE_ARCHIVE_SHA256 \
  --expected-unit-tests EXPECTED_COUNT \
  --output OPERATOR/spec.json
```

The source manifest is JSON with `commit`, `archive_sha256` and a `files` object
mapping every included relative path to its SHA-256. The `spec` command verifies
the source closure and pins the six compiled binaries. Four executable paths
are supplied in the bindings (candidate/reference native and planner); the
specification checks all six executable identities in the fixed build tree.
Analysis requires the pinned operator environment (Python 3.12, NumPy 2.3.5,
SciPy 1.16.3 and PyTorch 2.8.0+cpu); the system Python may not include SciPy.
Keep the operator script,
manifest and generated specification outside both tested trees.

Set `--expected-unit-tests` explicitly when registering a **new** campaign so
its unit-test scope is frozen in the specification. The ongoing 64-workflow
campaign retains its original 519-test specification and archived operator
script (SHA-256
`886395041167cbae876dc559e0b316be6dc53433100176b15e2553616c78bc48`). Do not
regenerate its specification or treat the maintained observer update as a
requalification of that campaign. The maintained observer's new
`--expected-unit-tests` option is in source SHA-256
`1a454565ecd4f29daa03569b2de14fcb47198d7f878ad4f35386acbf33cfd41c` for future
campaign registration.

## Baseline and fixed schedule

First collect one baseline with the same full workflow. Its output is not a
validation observation and is excluded from the null-blocking denominator. Use
a dedicated baseline fixture cache; do not reuse it for validation runs. The
initial baseline workflow wall was 285.495 seconds; this single setup/reference
observation is excluded from validation statistics. Its unit-test and guard
stages ran on the normal CI branch with the first-run fixture-cache miss. The
overall guard report separately records advisory limitations because native
and Python instrumentation receipts and a known-good hardware baseline were
not supplied. Raw five-second hardware probes did run; they do not provide a
known-good comparison baseline. These advisories are not missing guard screens
or a statistical rejection.

```sh
python3 tools/triad_perf_ci_null.py baseline \
  --spec OPERATOR/spec.json \
  --bindings OPERATOR/baseline-bindings.json \
  --output OPERATOR/baseline-001 \
  --baseline-id baseline-001
```

Before collecting validation observations, freeze the complete schedule. The
baseline file is `OPERATOR/baseline-001/observation.json`. For example,
`--clusters 60 --runs-per-cluster 1` describes 60 allocation clusters with one
scheduled workflow each; choose and record the schedule before seeing the
validation outcomes. Repeated workflows inside one allocation belong to the
same cluster and must not be counted as independent clusters.

```sh
python3 tools/triad_perf_ci_null.py init \
  --spec OPERATOR/spec.json \
  --clusters N \
  --runs-per-cluster R \
  --baseline OPERATOR/baseline-001/observation.json \
  --output OPERATOR/manifest.json
```

Run every scheduled row once, without retries or replacement of inconvenient
outcomes. The first validation run in an allocation uses a new, empty fixture
cache; later rows in the same allocation retain their cache state. Operating
system page cache state cannot be reset or inferred by the observer. Each run
uses a new output directory:

```sh
python3 tools/triad_perf_ci_null.py run \
  --manifest OPERATOR/manifest.json \
  --cluster cluster-001 \
  --repeat 0 \
  --bindings OPERATOR/cluster-001-bindings.json \
  --output OPERATOR/observations/cluster-001-run-001
```

The binding file contains local paths for candidate/reference roots, Python,
four candidate/reference executable paths (native and planner), bounds
workspace and fixture cache. The specification verifies six executable
identities in the fixed build tree. It contains no cloud account or service
metadata. Do not alter the suite,
guard settings, retry policy or source during a frozen schedule.

## Outcomes and analysis

Keep the outcomes separate: all-cause block, statistical rejection,
functional/integrity failure and infrastructure-incomplete. A missing guard
result leaves the statistical decision unknown while remaining an all-cause
block. Hardware-health warnings are not themselves a statistical rejection.
The report also distinguishes nomination, confirmation, futility and budget
exhaustion where applicable. Analyze all scheduled observations and retain
incomplete or conflicting rows rather than silently dropping them.

```sh
python3 tools/triad_perf_ci_null.py analyze \
  --manifest OPERATOR/manifest.json \
  --observations OPERATOR/observations \
  --output OPERATOR/analysis.json
```

The analyzer reads every retained `observation.json` under the supplied
directory. A file path can be supplied instead when analyzing a selected set.

For zero all-cause blocks in `n` independent allocation clusters, a one-sided
95% Clopper–Pearson upper confidence bound is `1 - 0.05^(1/n)`. This is about
4.87% for 60 zero-event clusters, 22.1% for 12 and 31.2% for 8. These are
planning values, not results from the current experiment. The observer also
uses the prespecified all-cause cluster event as the primary whole-suite
endpoint and reports a one-sided 95% bound for that endpoint; the statistical
rejection subset is bounded separately. Simultaneous category bounds are
additional diagnostics and are not the primary whole-suite confidence
criterion. A large per-test or per-stage sample must not be mistaken for the
same number of independent suite observations.

Allocation identifiers do not establish independent physical hosts. Unless
placement is independently attested, the conservative physical-host analysis
may merge allocations into an unresolved placement group. The binomial bound
is conditional on independent clusters sampled from the intended allocation
population; it is not distribution-free evidence about unseen hosts, other
providers, future CI runners or changed software/hardware. Provider metadata
that does not prove physical placement must not be described as host
independence. Report both the allocation-level result and this limitation.

## Completed 64-workflow qualification

The fixed 64-allocation campaign completed with zero all-cause blocks, zero
statistical rejections, zero functional/integrity failures and zero
infrastructure-incomplete outcomes. Three workflows nominated cases for
confirmation; all stopped for futility. The exact one-sided 95% bound for the
predeclared all-cause endpoint is 4.57% conditional on independent,
representative allocation clusters. Physical host placement was not verified;
the conservative placement-unknown bound is 95%. Thus this experiment does not
establish an unconditional false-block rate below 5%, the guard's nominal 1%
whole-workflow false-regression budget as a suite-wide rate, or detection power.
The full results,
coverage, runtime breakdown, limitations, and reproducible raw archive are in
the [64-workflow qualification record](validation-data/triad-v3/2026-11-04-ci-null/README.md).
The scientific archive extracts into `final-staging/`; change into that
directory before running the record's analysis-replay command.

This is occasional empirical qualification, not a routine CI prerequisite.
Keep the regular unit-plus-guard route unchanged and reuse this result only for
matching source, binaries, stack, workload, cache policy and allocation
assumptions. Do not repeat the cloud campaign for documentation-only or other
unaffected changes. The CPU allocations and three retrieval proxies were not
verified as independent physical hosts.
