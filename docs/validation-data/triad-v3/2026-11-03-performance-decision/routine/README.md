# Prospective CPU decision routine

The [complete summary](cpu-decision-001/summary.json.gz) records one completed
CPU campaign: **741.035036 seconds**, all **23 runtime gates passed**, all
16 matrix-selected functional commands passed, and `errors: []`. The process
exited **2** because the broader declared scope remains incomplete. This is
not a retry or a sum of separately measured campaigns.

| Preserved child report | Runtime cases | Child process wall time | Outcome |
| --- | ---: | ---: | --- |
| [Native and paired FFT](cpu-decision-001/native-fft.json.gz) | 13 | 214.904526 s | All gates passed. |
| [Identical FFT](cpu-decision-001/identical-fft/free-crystal/result.json.gz) | 1 | 39.214691 s | `free-crystal` passed its historical/current absolute-runtime decision after strict runtime identity checks. No paired implementation observations were invented. |
| [Sampled families](cpu-decision-001/sampled/summary.json.gz) | 9 | 439.072257 s | All direct full-workload gates passed. Per-case results retain call receipts, work counts and raw timings. |

Child process walls include their own setup and orchestration; individual
operation timers retain their narrower recorded boundaries. The complete
741-second campaign also includes functional checks and the outer orchestration.
The identical route's measured operation is receiver preparation plus scoring
including finish; it excludes fixture generation, worker startup, identity
audit and evidence writes. Native generation and sampled-family complete work
have their separate declared timing boundaries. Do not pool these timers into
one common per-operation estimand.

The run used CPU affinity `2,4`, two Python numerical threads, one native
thread, `fp32`, and `--stopping-rule decision`. The per-test confidence contract
and historical endpoint requirements remain those described in the
[parent record](../README.md). All 23 declared warm decisions resolved.
Twenty-two cases did not meet the legacy 5% timing-width diagnostic; the
identical route's corresponding diagnostic was not requested (`null`). These
are not 23 measured width failures, and width was not a reason for exit 2.

The remaining scope blockers are cold startup/loading/first-use prediction
and three maintained inventory gaps: standalone native planner/statistics
timing, a forecast across a representative resource boundary, and a complete
mixed-family concurrency/throughput forecast. The hardware screen was observed
without a degradation flag; it is a screen, not a confidence-qualified health
guarantee. `complete_scope_qualification` remains false. Direct full sampled
measurements do not qualify an absent or failed abbreviated model.

Computational savings are **not established by this prospective run** because
no matched direct-testing campaign at comparable assurance was executed.
The separate [offline replay](replay/cpu-complete-001-decision-replay.json.gz)
is retained as a replay calculation, not as measured campaign acceleration.
No pending three-observation shadow policy is included or adopted here.

## Reproduction and retained inputs

[reproduction.json](reproduction.json) preserves the exact outer argument array,
working directory, affinity, output paths and environment qualification. The
parent environment was inherited; child numerical controls and observed
library identities are in the reports. The invocation was limited by a shared
1,800-second harness budget and an outer timeout. Reproduction needs a fresh
output directory and the retained inputs at the declared paths. It must not
overwrite this observation.

The archive retains the [outer stdout](outer-logs/stdout.log.gz) and
[stderr](outer-logs/stderr.log.gz), child/functional logs, native/FFT report,
all sampled case results, and identical-FFT
[raw observations](cpu-decision-001/identical-fft/free-crystal/observations.jsonl.gz),
[forecast](cpu-decision-001/identical-fft/free-crystal/forecast.json.gz) and
[precision receipt](cpu-decision-001/identical-fft/free-crystal/precision-qualification.json.gz).
The [original identical anchor](identical-anchor-inputs/free-crystal.identical-anchor.json.gz)
and enabled [reporting projection receipt](identical-anchor-inputs/reporting-projection.json.gz)
are exact input copies; the original proof and all unchanged runtime/input
identities remain required by that projection.
Its historical source-archive hash points to the unchanged
[identical-FFT source snapshot](../../2026-11-03-performance-complete/identical-fft/matching-source.tar.gz),
which remains in the earlier archive rather than being relabeled as current.

The summary's historical native and Python coverage receipt hashes were checked
against both the original local files and the unchanged maintained receipts:

- [Native follow-up](../../2026-11-03-performance-complete/coverage/native/followup-001/coverage.json):
  `99d34fd366bd636da2a9a6b9565233ee1297d30c8c9eec954e47c00371e740b5`.
- [Python dependency-closed receipt](../../2026-11-03-performance-complete/coverage/python/attempt-04-dependency-closed/summary.json):
  `a1903287f1a0dd36e7023151d0e268ee5257fb0e4635c0ea946e8298a2cf8a15`.

Both were accepted as source-and-dependency-matched **historical** counters.
They were not rerun by this archive operation. Their finite scope and prior
reconciliation remain in the original evidence.

[matching-source-and-tests.tar.gz](matching-source-and-tests.tar.gz) contains
124 frozen source, build-input, configuration and test files. Seventy-six
members were checked against source/protocol hashes in the completed summary;
the remaining current harness/test members have explicit archival hashes.
[Native build provenance](reference-build-provenance.json) records the compiler,
reference commit and both native binary identities; the prior
[trace proof](../../2026-11-03-performance-trace/README.md#reproduction-and-further-development)
retains the native source/build reconstruction. Binaries, IQ/statistic arrays,
NPZ caches and temporary captures are excluded. Reports retain generation
commands, seeds, geometry and input hashes for reconstruction.

## Test-log scope and archive verification

The [initial discovery log](validation-logs/triad-confidence-unit.log.gz) records
295 tests in 13.104 seconds: 294 passed and one environment-gated test skipped.
The [explicit native-fixture follow-up](validation-logs/triad-confidence-native-fixture.log.gz)
records that one test passing in 0.948 seconds. Separate final logs record
[six family tests](validation-logs/triad-confidence-family-final.log.gz) passing
in 0.042 seconds and [ten staged tests](validation-logs/triad-confidence-staged-final.log.gz)
passing in 0.094 seconds. The root reports those final checks ran after the
campaign without code edits. These are separate checks, not an asserted count
of unique tests, and no claim is made that the initial discovery covered a
later delta.

[preservation.json](preservation.json) records every original byte hash, gzip
hash, source member and coverage-reference check. Gzip and tar metadata use
zero timestamps, with no original filename in the gzip header. Run
`python3 verify.py` in this directory to check [SHA256SUMS](SHA256SUMS), exact
decompression, source bindings and the retained coverage references. Verification
runs no receivers, simulations, tests or benchmarks. Earlier archives and
their historical source snapshots remain untouched.
