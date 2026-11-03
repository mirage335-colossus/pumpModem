# Paired numerical repair rescore

This followup reuses the core campaign's physical workspaces and exact
scientific arguments. It does not generate a new waveform, add channel/search
hypotheses, alter duration or gates, or count the same paths as independent
validation. The original `/workspace/triad` tree and its core receipts remain
immutable; accepted numerical source fixes live in `/workspace/triad-fixed`.

Original job IDs are retained with a separate `numerical-fix` variant identity.
The new manifest binds the original plan and all receipt bytes, original native
binary/build/archive, complete workspace inventories, and patched scorer import
closure. The old receipt plan hashes are never replaced with the new manifest
hash. Original and new raw score archives are preserved for direct comparison.

The checked source closure includes `triad_phase_frequency.py`, which is imported
by `triad-noise-study.py` but was omitted from the baseline plan's individual
file list. Its original bytes are verified against the original hashed source
archive without extracting or modifying that archive. This is a provenance
completion, not a scientific receiver change.

Run the cheap recipe checks with the existing numerical interpreter:

```sh
/tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-03-broad/rescore/test_plan.py -v
```

After the numerical patch review, copy only the approved source changes and
these scripts into the patched source tree. The runner preserves the Python
virtual-environment invocation path; it must not resolve the executable symlink
to a system interpreter. The following remote preparation reads/hashes existing
artifacts but runs no receiver:

```sh
triad_py=/workspace/triad-env/bin/python
triad_old=/workspace/triad
triad_fixed=/workspace/triad-fixed
triad_repro=docs/validation-data/triad-v3/2026-11-03-broad/rescore
triad_runs="$triad_old/local-evidence/triad-broad-coverage/core-v2-runs"
triad_manifest="$triad_fixed/local-evidence/triad-broad-coverage/rescore/manifest.json"
triad_fixed_out="$triad_fixed/local-evidence/triad-broad-coverage/fixed-results"
"$triad_py" "$triad_fixed/$triad_repro/prepare.py" \
  --root "$triad_fixed" --original-root "$triad_old" \
  --baseline-plan "$triad_old/local-evidence/triad-broad-coverage/plans/core-frozen.json" \
  --original-output "$triad_old/local-evidence/triad-broad-coverage/core-v2" \
  --output-root "$triad_fixed_out" \
  --binary "$triad_old/build/dev-cli/triad_long" \
  --build-provenance "$triad_old/local-evidence/triad-broad-coverage/build-provenance.json" \
  --source-archive /tmp/triad-broad-cloud/triad-broad-source.tar.gz \
  --receipts "$triad_runs/configure/results.json" \
    "$triad_runs/pilot-export/results.json" "$triad_runs/export/results.json" \
    "$triad_runs/pilot-actual/results.json" "$triad_runs/actual/results.json" \
    "$triad_runs/conditional/results.json" "$triad_runs/benchmark/results.json" \
  --output "$triad_manifest"
```

Adjust the interpreter path to the already verified numerical environment.
The plan requires every original core job to be accounted for; unsupported,
dependency-blocked and failed jobs stay present. Native source/archive checks
and every recorded workspace/report hash must pass before preparation or any
dependent rescore stage. A separate `--scorer-manifest-only --root ... --output
...` command is available to inspect the proposed source closure before the
complete freeze, but does not authorize or replace the complete binding.

Execute each selected stage explicitly, with fresh run directories:

```sh
"$triad_py" "$triad_fixed/$triad_repro/run.py" --manifest "$triad_manifest" \
  --stage actual --python "$triad_py" --jobs 8 --global-seconds 3600 \
  --run-dir "$triad_fixed_out/actual-run"
```

Repeat with `--stage conditional` and `--stage benchmark` only when authorized,
using `conditional-run` and `benchmark-run`. The frozen plan has 356 actual,
912 conditional and 16 benchmark jobs (1,284 total); original unsupported or
missing-export cells remain explicit skips. Each child keeps its original
120-second timeout and numerical/memory settings. At most eight owned child
groups run concurrently and each stage stops dispatching before its one-hour
limit. Source, binary, baseline receipts and all physical files are rechecked
after the stage; mutations invalidate the stage receipt.

`--only-id` permits a preselected pilot. Subsequent chunks must pass all prior
variant receipts with repeated `--prior-results` options and select only new
IDs. A previously accounted ID cannot be retried implicitly. A numerical fix
rescore is explicitly a new variant, not a retry hidden inside the baseline
campaign. Stage receipts use their own schema and manifest identity.

Analyze old and new observations together after collecting the selected stage
receipts and all raw outputs:

```sh
"$triad_py" "$triad_fixed/$triad_repro/analyze.py" --manifest "$triad_manifest" \
  --receipts "$triad_fixed_out/actual-run/results.json" \
    "$triad_fixed_out/conditional-run/results.json" \
    "$triad_fixed_out/benchmark-run/results.json" \
  --output "$triad_fixed_out/paired-analysis.json"
```

The analyzer independently recounts strict/relaxed outcomes using the existing
analyzer and exact score arrays. It reports status transitions, raw score and
winner changes, admission changes, method and complete-job times, and paired
precision/backend comparisons against CPU FP64. Gates must match exactly.
The relaxed gate remains the strict log gate minus `log(200)` while keeping the
stricter residual-noise UCB; its calibration failure allocation is conservative.

Attempted processing failures remain in opportunity denominators: signal
failures count as missed opportunities and failed controls remain unresolved.
Unstarted work is separate. Completed, hash-verified conditions from partially failed jobs remain available
for paired comparison. Only missing conditions are unresolved; the enclosing
job remains failed. Failures stay in the status and denominator accounting. Same-path conditional noise repetitions
are not independent channel trials. Single shared-host latency measurements
include contention/startup and do not establish a universal GPU speedup or a
general sensitivity/duration bound.
