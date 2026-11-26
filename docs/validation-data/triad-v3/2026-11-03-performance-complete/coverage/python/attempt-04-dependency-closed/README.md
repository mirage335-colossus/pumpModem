# Dependency-closed Python coverage capture

This is a fresh CPU branch-coverage invocation after the original ray-frequency test failure and accepted test-only correction. It is distinct from, and does not overwrite, attempt-02 or the split attempt-03 reconciliation.

## Capture result

- coverage.py 7.17.1; Python 3.13.5; Torch 2.8.0+cpu; CPUs 6 and 8.
- 16 selected test files passed, with positive check counts; 16 expected Python source rows were collected.
- Elapsed time was 53.43 seconds. CUDA was unavailable.
- Pre/post matrix SHA256: `1860b045b4078da56db5b8928cbb3b71660844a94d0c5da3ddb1eab5382e5a33`.
- Pre/post declared runtime, helper, test, and collector source identities were stable.
- The Python matrix source-row union contained 19 paths across 8 intersecting rows. The receipt contains 41 exact dependency hashes, including matrix-declared and selected test sources.
- `tools/triad_perf_complete.py`'s `check_coverage` returned `declared_dependency_status=matched`, no missing expected sources or dependencies, no changed dependency hashes, and `historical_applicability=declared_source_and_dependency_match`.

The full matrix snapshot is in `provenance/tools/triad-performance-coverage.json`. `summary.json` records matrix SHA pre/post separately from `dependency_sha256`; a later evidence/status-only matrix update therefore does not invalidate the source receipt. Hash pre/post covers the declared matrix source union, selected and matrix-named test files, the explicit noise-study helper fixture/tool, the runtime rows, and the collector itself. This proves stability of those declared files during collection, not every dynamically imported module.

## Reproduction command

The recorded capture command is historical: the
`tools/triad_perf_python_coverage.py` runner is no longer present in this
checkout. Its original source is available at the
[captured source snapshot](https://github.com/mirage335-colossus/pumpModem/blob/357a50970a0c6214eeaade7c5704bbc39abd4b79/docs/validation-data/triad-v3/2026-11-03-performance-complete/coverage/python/attempt-04-dependency-closed/sources/tools/triad_perf_python_coverage.py).
The command was:

```sh
taskset -c 6,8 /tmp/triad-weak-env/bin/python \
  tools/triad_perf_python_coverage.py \
  --coverage-matrix tools/triad-performance-coverage.json \
  --raw-dir local-evidence/triad-performance-complete/python-coverage/attempt-04-dependency-closed/raw \
  --output local-evidence/triad-performance-complete/python-coverage/attempt-04-dependency-closed/summary.json
```

The raw `.coverage` database and coverage JSON, captured test logs, and
source/dependency hash provenance are retained here. The original package also
contained 16 coverage-source snapshots, 19 matrix-declared source dependencies,
21 test-source snapshots and the collector source. These overlapping categories
covered 42 source files, which were
later removed under the material-retention policy. The test logs and provenance
JSON are receipts, not copies of the `.py` files. The
[retirement manifest](../../../../archive-content-retirement.json)
records the removed paths. The local `SHA256SUMS` is the original capture
checksum list, including its original README digest; that document digest
predates this clarification. The recorded coverage artifact hashes remain as
captured.
