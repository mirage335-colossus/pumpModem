# Blinded coordination exercises, 2026-09-27

This is an empirical check of [the coordination workflow](agent-coordination.md),
not application or release qualification. Five independent repair agents received
problems drawn from five different repository commits. The evaluator could see
the fixes; the workers received pre-fix source snapshots and problem statements,
without commit identifiers, solution diffs, or post-fix regression tests.

## Method and boundaries

The primary checkout started clean at
`7a0800d2c1c58e2bbfbc16d78b07ef0851766c14`. Each case used the selected commit's
first parent, with a new one-commit local Git repository. Current `AGENTS.md` and
`docs/agent-coordination.md` were overlaid to test the current protocol. The
snapshots included source, tools, tests, maintained documentation, CMake files
and dependency recipes, but not the large vendored dependencies. Workers could
not inspect the parent checkout, sibling cases, evaluator files or original Git
history. These were instructed boundaries on a shared filesystem, not enforced
security isolation. Workers were spawned without conversation history.

All workers used the same absolute, ignored `.agent-work/` board. Their historical
source snapshots were isolated because they represent incompatible points in
history. The shared integration ledger provided a deliberately contested file:
the evaluator initially claimed it, workers requested release using board
messages, then acquired it individually to append results. This tests file
ownership and handoff, not simultaneous merging of production code. The evaluator
provided no repair hints after dispatch. One missing pre-fix LICENSE fixture was
supplied after a worker found that snapshot omission.

Three worker slots were available. Five distinct agents ran in overlapping waves,
not five at once. They were Codex workers, not a test of Anthropic Desktop or
OpenRouter harnesses. Every worker was required to conduct its own authoritative
web research, record temporary knowledge, implement a repair and run regression
checks. Publication and remote mutations were excluded.

| Case | Historical fix (revealed after dispatch) | Problem supplied |
| --- | --- | --- |
| A | `0afc74b` | Tag creation race and mismatched SDK binary/source inventory |
| B | `ba80ff6` | ZIP member spelling lost before validation; CRLF changes recipe identity |
| C | `1719955` | Missing release assets, older CLI fields and multi-page inventories |
| D | `53c54c3` | Repackaging provenance differs from application source identity |
| E | `0a2a738` | Windows native-test working-directory identity failure |

The evaluator reserved post-fix test files before reading worker implementations.
It ran them against the original source and the historical repaired source as
negative and positive controls. Historical tests sometimes encode helper names
and CLI argument shape; failures caused solely by those implementation choices
must be distinguished from behavioral failures. Case E changes a test fixture,
so running the historical test against the old runner would not expose the bug.
Its actual original test file was instead run with `TMPDIR` pointing through a
filesystem alias, reproducing the spelling mismatch on Linux; the historical
fixed test passed under that same environment. This does not qualify Windows.

## Evidence and limitations

Live records, knowledge notes, snapshots and raw logs are ignored under
`.agent-work/`. Exercise artifacts are under
`.agent-work/artifacts/coord-exercise-20260927/`; held-out tests and control logs
are in its `evaluation/` subdirectory. They are local evidence, subject to the
workflow's retention rules, not a permanent dependency of this report.

The required root instructions and coordination guide totaled 5,628 words at
baseline (1,457 plus 4,171). Worker estimates of overhead are observational:
there was no randomized, matched run without coordination, so this exercise
cannot establish equal speed, token cost, research quality or coding effectiveness
relative to uncoordinated agents. Passing tasks and preserved edits can establish
that useful work occurred alongside coordination; they cannot establish parity.

## Results

Three cases met the evaluated behavior; two were partial relative to the historical
fix. Every worker produced code/tests and a research note. No solution was sent
back to a worker to repair an evaluator-discovered miss. These findings describe
the exercise snapshots, **not defects newly discovered in current production**.

| Case | Worker checks | Evaluator result |
| --- | --- | --- |
| A | Release 42 passed; source SDK 24 passed, 3 prerequisite skips | Tag race and mismatched pairing repaired. Public assembly probes still accept an orphan source archive or an additional unrelated source archive; the historical fix rejects both. Partial historical coverage. |
| B | Windows-base 24 and release 58 passed | All 22 held-out historical Windows-base tests passed. Worker independently chose canonical recipe input bytes rather than the historical `.gitattributes` change; its tests cover LF/CRLF identity, identical archive bytes and tamper rejection. Native Windows remains untested. |
| C | Release 44, SDK release 28, certification 21, dependency/wrapper 44 and source SDK 24 passed; 3 prerequisite skips | 87 adapted historical high-level checks and 5 independent public-download checks passed. Research, pagination, byte integrity and failure cleanup demonstrated with mocks, not a live GitHub service. |
| D | Release 68, certification 44, APT 7 and SDK release 27 passed | Tag selection and preparation repaired, but emitted schema-3 certification evidence lacks `packager_sha` (also lacks historical `tag_sha` and `repackaged_from` fields). Held-out certification suite: 42 passed, 1 error. Partial. |
| E | Native-runner fixture suite 13 passed | All 13 also passed with the evaluator's symlinked `TMPDIR`. Before repair the original 11-test suite had 4 failures and 1 error there. Includes a wrong-directory rejection test. Native Windows remains untested. |

All seven historical positive-control suites for A–D passed: 253 tests total.
The historical E fixture passed its 11 tests in the alias environment. Before-fix
controls exposed the targeted problems, but some raw failures were test/API
coupling rather than independent behavior evidence.

A's raw historical suite had 24 passes, one missing helper-name error and one
out-of-scope release-note failure. Public `assemble()` probes independently
verified matched-pair acceptance and mismatched-pair rejection, then exposed the
orphan/extra-source gap. D's raw release suite had 66 passes and four differences:
a helper name, two diagnostic strings, and unrelated CLI error-reporting behavior.
Its certification-report failure is a substantive missing output field, not an
interface adaptation.

C's raw historical suites did not pass unchanged. The evaluator adapted header
whitespace, the private downloader mock name/default and equivalent error text,
and selected the 38 historical high-level release tests instead of five tests
coupled to the historical low-level helper API. Together with 27 SDK and 22
certification tests these gave 87 passes. Five separate public-interface tests
then checked exact binary content, required digest preflight, size/digest
corruption cleanup, interrupted-download cleanup and refusal to overwrite an
existing file. Original logs and adapted tests were retained separately; these
are explicitly **adapted evaluation**, not an unchanged historical-suite pass.

An evaluator harness initially symlinked candidate tools into the test tree,
causing B's path-location assertion to fail. Copying the finished candidate tools
into a stable evaluator tree corrected that harness defect. Missing LICENSE
fixtures affected A, C and D and were supplied from their exact pre-fix parents.
Those infrastructure failures were not scored as worker repair failures. D's APT
suite initially could not start its disposable GPG agent in the sandbox; the
worker reports passing the same full suite after approved local escalation.

## Coordination and research observations

All five workers registered, held separate snapshot claims, used the registry
mutex, preserved the ledger's prior entries and closed with empty claims. A
observed B holding the ledger and sent B a handoff request without editing it;
B released ownership before A appended. The first three also respected the
evaluator's initial claim. Later workers found the ledger unclaimed and acquired
it through the same protocol. Five final entries remain; the main checkout's
source and index were unchanged. This supports cooperative conflict avoidance
in the exercised paths, not a claim that the prose protocol prevents arbitrary
or noncooperating filesystem writes.

Release notifications were delivered through files. A reported waiting after a
notification existed and received a reminder to inspect its inbox. B also received
a reminder, but reports having already read the acknowledgment at its checkpoint;
that is not evidence B required intervention. D explicitly released/reacquired
its LICENSE exception during fixture coordination. C reports initially writing
its uniquely named note without a separate explicit claim, then correcting the
claim before its final update; no competing writer was observed. There was no
observed data loss, stale-claim theft or unrelated Git mutation. No expired
30-day session was present, so deletion/recovery was not exercised.

Research notes contain independently chosen queries, attributed official URLs,
local reproduction evidence and limits. A and D consulted GitHub reference and
release semantics; B consulted CPython ZIP implementation and Git line-ending
rules; C consulted GitHub asset/CLI pagination documentation; E consulted Python
filesystem identity and CTest working directories. Relevant primary references
include [GitHub references](https://docs.github.com/en/rest/git/refs),
[GitHub release assets](https://docs.github.com/en/rest/releases/assets),
[CPython ZIP implementation](https://raw.githubusercontent.com/python/cpython/v3.12.0/Lib/zipfile/__init__.py),
[Git attributes](https://git-scm.com/docs/gitattributes), and
[Python filesystem identity](https://docs.python.org/3/library/pathlib.html#pathlib.Path.samefile).
Workers distinguished local/mock verification from native-platform and live-service
claims. The evaluator did not supply these references at dispatch.

Board overhead was not instrumented end to end. Reported read-volume estimates
were A 50–80 KiB, B 30–45 KiB, C about 90 KiB, D 45–65 KiB and E about 12 KiB;
estimates differ in scope and exclude or separately count initial instructions.
Reported measured claim-scan samples were A 10,098 bytes, B 9,369/10,247 bytes
and D 14,196 bytes. Final worker records were 1,394–2,037 bytes. These measurements show bounded individual
records, but repeated scans still have a cost. Self-estimated minutes are not a
reliable timing benchmark and are not used to calculate a slowdown percentage.

## Implications and next experiment

The documentation is usable for this cooperative exercise, with minor protocol
lapses, but this sample does **not** prove equivalent effectiveness or negligible
distraction. Three evaluated successes and two incomplete repairs also do not
show that coordination caused the incompleteness. The task statements described
symptoms and desired behavior; this was not unaided discovery of unknown bugs.
Most tasks were small Python tooling changes, not long-running C++/GUI debugging.

Keep exercise reports out of required startup reading. A useful next revision
would provide a compact routine-checkpoint checklist, including inbox polling
before declaring a handoff blocked and explicit note claims, while making the
long recovery/retention procedures available when their triggers occur. Those
procedures remain safety requirements; this report does not waive them. A small
cross-platform helper for metadata/claim extraction could reduce repeated prose
scans, but its locking semantics would need separate testing.

A stronger comparison should randomize matched tasks across coordinated and
isolated-control workers, counterbalance task/model ordering, instrument actual
read bytes/tokens and wall time, and use behavior-based held-out tests fixed
before dispatch. Include independent vendor harnesses, deliberately overlapping
source edits with an integrator, interrupted jobs, malformed records, overdue
owners and cleanup candidates. Require the same external-research and coding
rubric in both arms. This run supplies initial observations, not those unperformed
comparisons or cross-vendor qualifications.
