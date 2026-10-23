# Validation status

Updated 23 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current branch qualification

Tested source: `e9763dc0ab84b31d3933a54f3547690a2a7fbbde`.
Documentation/data commit `0751ac847096de8017d7028d14f22037abce445e` has identical
application, test and workflow inputs. Both full runs used standard runners and
`devfast=false`; required inventories and source identities were independently read.

| Scope | Exact run and result |
| --- | --- |
| Full native | [39899129455](https://github.com/mirage335-colossus/pumpModem/actions/runs/39899129455): 48 required jobs successful; 121m48s. |
| GNU 11 Debug ASan/UBSan core | 268 configured cases: 251 passed, 17 declared return-77 omissions, zero failed or incomplete. |
| Linux / Windows Release core | Linux 254 passed / 17 declared omissions; Windows 233 passed / 33 omissions. |
| Full SDK | [39899129083](https://github.com/mirage335-colossus/pumpModem/actions/runs/39899129083): 15 required jobs successful; 58m25s; exact 200-test contract union and eight copied-archive smokes passed. |
| Physical / GUI coverage | Original assertions passed. Debug EU stop 8.48064s < 10s; source advance 3.18854s <= 3.26806s allowance; protected tail 3.14681s passed. |
| Preparation / calibration | Complete original cases, fixed seeds, serial/parallel comparisons and combined statistical gates passed. |

Live compact coverage completed in all three native configurations in this cohort.
Existing instrumented `fast_session` / `gui_fast_live` omissions and opt-in
sanitizer desktop omission remain explicit; omitted checks are not passes. Existing
ordinary five-second stop, delayed-arrival, completed CPU-work and Rev cadence
warnings remain visible. Physical GPM and device audio were not exercised by these
simulations. SDK results do not replace native GNU 11 sanitizer coverage.

The 25% CI margin applies only to explicit runtime and absolute-footprint
benchmarks. Local thresholds, ordinary functional assertions, numerical results,
configured allocation limits, six seconds of fully scored physical absence and
ten-second graceful stop remain strict. See the
[precise margin policy](building.md#resource-benchmark-margins).

## Symbol_Burst and supplemental corrections

The prior reconciliation is complete, not pending because an older narrative
said a run was still in flight. Runtime `83975bd8054f336a5aaa01205b8f54db6d5b13f9`
was qualified through source `67db103c2b59f72b97e07447c27c6d453aac68de` by
[native 39742621396](https://github.com/mirage335-colossus/pumpModem/actions/runs/39742621396)
and [SDK 39742625503](https://github.com/mirage335-colossus/pumpModem/actions/runs/39742625503).

[Paced backlog qualification 39737595013](https://github.com/mirage335-colossus/pumpModem/actions/runs/39737595013)
passed all nine plaintext/keyed, Burst Off/On and held/unheld cases, the original
no-argument Burst test and three storage tests. Exact bits, complete input,
physical END, final frontier/queues and cleanup passed. Held-On cases cleared 14
plaintext and eight keyed gaps; Off controls made no rescue claim. Maximum callback
was 0.906497ms and start lateness 1.323429ms; original 20ms start/completion guards
passed. Generated/prefetched samples are not transmitted progress. Reported PCM/FNV
identities matched; raw PCM was not retained for independent rehashing.

Later runtime `c51183182c6c68b5a260fba4712bb9819f943996` corrected receiver
acquisition completion and ncurses readable-prefix handling. A matched complete
PCM fixture recovered all 32 Help bits where baseline retained a 17-bit suffix;
this does not prove a unique cause for every historical GUI failure.
[Strict GUI 39859510712](https://github.com/mirage335-colossus/pumpModem/actions/runs/39859510712)
passed the original pair 2/2 serially and 2/2 in parallel, with EU stops
8.5334/8.2779s and unchanged tail/idle assertions. Its inputs match the current
runtime; this evidence remains bound to its actual source. Full candidate
`cefdac53617105aa120e425f1b270a9035777493` passed
[native 39880745813](https://github.com/mirage335-colossus/pumpModem/actions/runs/39880745813)
and [SDK 39880749070](https://github.com/mirage335-colossus/pumpModem/actions/runs/39880749070)
before the current resource-margin qualification.

Original failures remain failures: copied Help, strict compact Live compute,
SDK paste-send at its 20-second deadline, Release Burst at 51.83s, timing advice
beyond three seconds, and cancelled APT setup. Passing later results neither erase
those failures nor identify a sole cause. Earlier compact-Live incomplete coverage
is historical, not a pass and not the result of the current completed cohort.
Detailed original records remain in the pre-cleanup Git history and retained local
reports. Sched_Hop's separate sustained-performance objective remains unfinished;
see [future work](future-work.md).

## CI elapsed time and remaining uncertainty

The current simultaneous native/SDK pair took 121m49s, with 1424.33 unweighted
runner-minutes and 365775353 artifact bytes. The preceding `cefdac5` pair took
113m11s, versus 173m46s for the earlier `67db103` pair. These are observed complete
qualification times across different source epochs and runner cohorts, not matched
causal estimates. Queueing is included. No persistent compiler cache was deployed.

Adding the boundary test changed 245 existing round-robin assignments. Prior
per-test wall sums shift about 882s toward Debug core part 0, which became the
current final path. These sums do not predict job elapsed time or separate build
cost, runner variation and contention. The latest 8m38s pair increase alone does
not establish a persistent regression. Reuse the existing plans/timings before
considering a bounded rebalance; do not repeat full baselines just for comparison.

The confirmed 20-job account limit can queue the full native/SDK fan-out.
Independent package producers and copied checks proceed without waiting for core,
calibration or optional instrumented GUI scopes. Complete-release elapsed time
has not been measured on the latest branch. The bounded
[resource snapshot](ci-resource-results.json) distinguishes CPU, wall and workspace,
with unavailable fields null; Git retains its previous versions.

## Publication and exact-binary certification

A read-only GitHub check on 23 October 2026 still identified
[`v001_00-2026-09-30-0648CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-30-0648CDT)
as ordinary Latest, source `dce9138d27a2e6941b4304af2cf035c4ac2a6c9b`.
Its certification run 36704181844 attempt 1 failed; attempt 2 passed with warnings.
The exact Windows WGL exclusion and other recorded limitations remain; see the
release's hash-bound reports. The newer branch's green CI has not published or
certified replacement binaries.

For an authorized delivery, follow [RELEASE](../RELEASE) and
[the release procedure](releases.md): pin the candidate, complete required native
and SDK qualification, produce and check packages, publish, certify the exact
source and delivered binary hashes, then verify GitHub Latest. This cleanup does
not authorize publication or paid runners. Preserve prior release assets/reports.

## Automatic regression signal policy


Automatic regression checks must not force `tone-N`, `auto-tone`, or
`SpreadingMode::tone` fixtures or require successful tone simulation/reception.
Use changing-sign fixed or seeded pseudorandom patterns to check differential
phase and amplitude measurements, acquisition, streaming memory and timing.
Keep measurable transitions in signal fixtures; changing a mode name while
leaving a constant one-chip code does not add pattern-transition coverage.
Mode-name parsing can still cover the complete supported option catalog.
Independent sine-wave checks of resampler/filter mathematics are not modem
tone-pattern reception tests.

Tone operation depends on narrower physical conditions, such as GNSS timing
synchronization, low frequencies, high symbol rates and suitable hardware.
Successful differential measurements with pseudorandom patterns exercise the
measurements needed for tone shifts under those conditions; automatic tone
loopbacks are not a requirement or evidence of general tone reliability.
Historical tone results below are retained as records, not current regression
requirements or hardware validation.

## Historical evidence

The former run-by-run ledger is recoverable at
[0751ac8:docs/validation.md](https://github.com/mirage335-colossus/pumpModem/blob/0751ac847096de8017d7028d14f22037abce445e/docs/validation.md).
Read the relevant dated section when investigating a regression; it is not a
startup reading requirement. [Release history](release-history/2026-09-30/README.md),
active independent fixtures remain intact. Retired numerical captures remain
available through pinned Git history. Historical
binary hashes identify former executions; removed local binaries must be rebuilt
from their matching source/configuration before a new experiment.

Ignored `local-evidence/` retains the latest qualification reports, unaccepted
patches and selected receiver/CI inputs needed by unfinished work. It is not a
build-history archive. Completed study data and superseded tracked narratives
are available through [pinned Git references](indexes/evidence.md); old local
artifact paths may have been retired. Current development needs current source
and focused evidence, not reconstruction of former build environments.
