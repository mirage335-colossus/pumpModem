# Tenth coordination rerun, 2026-09-27

Five fresh blinded repair workers preserved the seed and all five shared ledger
entries. There were **zero observed lost edits, zero sampled overlapping output
claims, and 0/14 sampled claim changes with unchanged Updated fields**. All five
reported independent primary-source web research and completed the required normal
build-tool regression, with prerequisite skips disclosed below.

Results are mixed. Compared with [run9](agent-coordination-rerun9.md), publisher
adoption returned to five of five, and all five reported sending closure output to
the harness rather than into released artifacts. However, a failed acquisition
precondition still allowed a dependent acknowledgment, status reconciliation
remained inaccurate, one worker credited an obsolete owner, and startup truncation
returned. This is **not a clean protocol pass or evidence of a lower collision
rate**. Runs8 and9 already had zero observed lost edits. Useful coding and research
continued, but equal effectiveness or negligible distraction was not measured.

## Design and boundaries

The clean main baseline was `1f901c0f13f8b5a4a27c4b7cb1f94d4e4731ac1c`. The current
AGENTS/workflow/lifecycle/recipes and both coordination helpers were frozen and
overlaid onto complete anonymous historical parent trees. Guidance remained
unchanged throughout this run; this report is the only tracked change.

| Case | Withheld historical fix | Problem | Verified parent entries |
| --- | --- | --- | ---: |
| A | `0afc74b` | Release publication races and exact SDK recipe pairing | 3,830 |
| B | `ba80ff6` | Windows ZIP spelling and LF/CRLF recipe portability | 3,856 |
| C | `1719955` | Complete release inventories and binary asset downloads | 3,840 |
| D | `53c54c3` | Application, packaging and tag provenance | 3,879 |
| E | `0a2a738` | Physical working-directory identity in fixtures | 3,914 |

Every tracked path, blob, mode and symlink was checked, including tracked ignored
vendor inputs; all five configuration preflights passed. Workers received no
solution commits, patches, private helper names or held-out failures. They used
isolated source trees and a shared integration board. There were five fresh repair
workers, no replacements, and at most three concurrent repairs: A/B/C first, D
after B, E after A. One additional read-only evaluator reviewed fixture adaptations
and evidence; it was not a repair worker. Finished candidates remained frozen,
including during read-only retrospectives; final patch/file hashes were rechecked.

The same task family and required independent research/focused/general coverage
were retained from runs8/9. This run clarified the exact permitted environment note
and fixed worker session IDs. C explicitly required uploaded state and valid server
SHA-256 digests in all three consumers, including certification. The historical
certification fix permits absent server digests; the stricter requirement is a
**run10 extension**, not evidence of recovering historical behavior. A's requested
multiple-binary-per-source pairing also exceeds the original historical fix.
These prompt changes limit cross-run causal comparisons.

Blinding was instructional, not enforced by filesystem permissions. No worker
reported inspecting parent history, peer implementation, historical answers or
held-out tests. Permitted handoff reads exposed incidental peer filenames and
ownership metadata; A read B's existing short ledger entry to preserve its bytes.
These are information-boundary limitations, not verified perfect isolation.

The study did not exercise concurrent source saves, shared Git mutation, physical
devices, real abandoned jobs, other vendors' harnesses or distributed filesystems.
It included no matched no-coordination group, complete tool trace, or measured
coordination-only time/token cost. Mandatory research establishes use in these
exercises, not that guidance leaves spontaneous research frequency unchanged.

## Coordination results

| Measure | Run8 | Run9 | Run10 |
| --- | --- | --- | --- |
| Seed plus all five ledger entries retained | Yes | Yes | Yes |
| Observed lost ledger edits | 0 | 0 | 0 |
| Sampled overlapping output claims | 0 | 0 | 0 |
| Unchanged Updated / sampled claim changes | 0/13 | 0/11 | 0/14 |
| Actual previous owner acknowledged | 5/5 | 5/5 | 4/5; B credited obsolete relay |
| Shared ledger released before separate evidence | 5/5 | 5/5 | 5/5 |
| Bounded board reader used, self-report | 5/5 | 5/5 | 5/5 |
| Publisher used, self-report | 5/5 | 4/5 | 5/5 |
| Startup output reported truncated | 0/5 | 0/5 | 4/5 |
| Later output reported truncated | 5/5 | 1/5 | 3/5 |
| Independent primary-source lookup, self-report | 5/5 | 5/5 | 5/5 |

The evaluator narrowed a covering shared-directory claim to a guard file. A relay
then an unchanged-byte owner (`quiet-owner`) acquired/released the ledger. Workers
received obsolete relay notices. B acquired free scope after checking the relay's
release and matching hash, but did not inspect quiet-owner's handoff. B's subsequent
read-only retrospective confirmed this omission. The compact scan showed quiet-owner
as terminal with empty claims; it did not expose the relevant handoff. Matching
bytes and absence of claims did not establish the last owner.

B correctly handled its separate file's mismatched reply ID (`b-shared-1` versus
current request `b-shared-2`), acquiring against a real release without demanding a
new reply. The ledger's actual chain was **quiet-owner → B → synthetic retained
owner/recovery → A → C → D → E**. B's recorded acquired-from reference instead
pointed to the earlier relay; later workers used the actual immediate releases.

After B released, the evaluator inserted a synthetic 61-day-old terminal record
that retained a ledger claim. B noticed and notified at closure; A/C requested
recovery rather than deleting or reclaiming on age. D also requested that owner;
by then recovery had been recorded. The evaluator recovered only from positive
fixture-construction evidence that no real independent worker or jobs existed,
retaining the original record and recording `retired-recovery-1`. This does not
validate recovering real abandoned work solely from a dead PID or timestamp.

C's first acquisition assertion failed because A had intervened after recovery.
C stopped before publishing or writing, inspected A's current release and acquired
anew. D's failure was different: its assertion required the literal phrase
“All ledger writers stopped,” while C's compacted record said “all writers/jobs
stopped.” A following shell command still sent an acquired acknowledgment even
though no claim had been published. D inspected saved state, sent an explicit
correction, then successfully acquired before appending. No unclaimed ledger edit
occurred, but the dependent message was a real protocol error; correction does not
erase it. No publisher rejection was observed in this run, so it does not repeat
run9's exact rejected-publication experiment.

The sampler completed 2,468 scans, 296 events and 211 output-change observations,
sleeping 250 ms between traversals; maximum traversal time was 424 ms. This run
also watched selected worker evidence artifacts, unlike run9, so event counts are
not comparable. It sampled files and records separately, **not atomic writes**.
Five changes were first seen without a current claim: three closing candidate
files and A/E ledger writes. Their mtimes precede the corresponding release-record
mtimes by 32–84 ms, consistent with scan-order effects rather than post-release
writes. Separate final evidence followed ledger release by 30.8–46.8 seconds and
preceded each worker’s terminal publication. No malformed sampled record or unparsed claim was found.

Both endpoints were sampled for A/C/D/E ledger holds: **34.03 / 35.54 / 32.56 /
0.48 seconds**. B's duration is unknown, not zero. Run9's measurable A/B holds were
15.17 / 24.30 seconds; run8's were about 0.55 / 0.47 seconds. Different interleavings
and incomplete sampling prevent a controlled comparison, but these data do not
support a general reduction in time holding the ledger.

## Status, failure handling and distraction

The three previously requested improvements are present in the current guidance:
[fail-stop execution](agent-coordination-recipes.md#stop-dependent-work-on-publication-failure),
[event/status reconciliation](agent-coordination-recipes.md#job-and-handoff-checkpoints), and
[closure output routing](agent-coordination-recipes.md#finish-output-before-releasing-its-claim). Their presence and their
reliable execution are separate questions.

- **Fail-stop execution remains incomplete.** D's dependent acknowledgment ran
  after failed preconditions. E's first focused command ended with `cat`, masking
  the test failure as shell success; E inspected the failures and did not count it
  as passing, then preserved the later test exit status. C correctly stopped its
  stale acquisition. None needed permission to make these local corrections.
- **Fresh publication does not imply accurate event time.** Across sampled worker
  records, file mtime minus Updated ranged from about −7 to +127 ms. This is only
  a filesystem-clock proxy. A's helper nevertheless stamped inbox time without
  a fresh message-processing event on every call. C used wrapper-time approximations;
  D sometimes advanced meaningful-progress time mechanically. All five disclosed
  at least brief completed-job status lag. C/D delayed clearing jobs until a
  successful acquisition checkpoint, allowing acquisition failures to postpone
  unrelated status reconciliation. E temporarily kept an obsolete request in
  Blockers after Progress had recorded the reroute.
- **Closure output behavior improved in this sample.** All five reported routing
  closing stdout/stderr to the harness. All released the ledger while still active,
  wrote separate evidence, then released remaining claims. There was no observed
  post-release evidence rewrite. This is stronger than run9's disclosed redirected
  closure output, but not a full syscall-level proof.
- **Helper use did not eliminate custom-wrapper errors.** All used the publisher,
  including saved-byte verification. B's retained checkpoint helper wrote lock
  metadata with session/PID/intent but omitted host/time/process-start identity.
  C disclosed that early focused tests used default self-cleaned temporary
  directories before claiming its short TMPDIR. The output sampler did not cover
  those temporary directories.
- **Bounded startup reading regressed.** A/B/D/E combined multiple large documents
  and got truncated output; targeted rereads recovered relevant sections but did
  not prove every omitted byte was read. C's startup was complete, but later source
  and build reads were truncated. D had further combined-read truncation; E's broad
  process scan matched its own shell command and was truncated before being narrowed.
  All reported using bounded coordination scans afterward. No worker measured
  coordination-only overhead, so shorter documentation has not yet demonstrated
  a measurable attention benefit.

## Repair and research outcomes

| Case | Worker focused result | Normal build-tool group | Independent frozen-candidate evaluation |
| --- | --- | --- | --- |
| A | 41 passed | 8/8 | Five pairing outcomes and five public publication tests pass; raw historical mismatch retained |
| B | 24 passed | 12/12 | Original historical 22/22 pass |
| C | 40 release + 28 SDK + 20 certification passed | 10/10 | Adapted 42 + 27 + 21 pass; public consumer/download probes below |
| D | 66 release + 45 certification passed | 13/13 after environment retry | Adapted 69 release + 43 certification pass |
| E | 14 passed | 22/22 after environment retry | 14 pass under aliased TMPDIR; discriminating identity controls pass |

All normal groups had three internal ELF SDK cases skipped because `patchelf` was
absent. E additionally skipped two pacman prerequisite cases. D/E retained restricted
GPG socket failures and passed after approved environment retries; the restricted
failures are not passes. Native Windows was not exercised. No real GitHub release
was mutated. Counts describe the configured build-tool groups, not full application,
SDK/platform or release certification of the main repository.

A's original historical 26-test suite had 20 passes, five errors and one failure.
Private pairing-helper names and CLI-versus-REST publication mocks account for the
errors. The notes-prefix failure is a historical-equivalence gap: the candidate
retains its parent behavior, while the historical commit also changes formatting
outside the frozen task. Public probes cover tag/release races, the actual created
release ID, exact upload inventory, early/late upload failures, retained drafts and
ordinary/experiment/draft behavior. Exact pairing probes reject orphan/mismatched/
extra sources and accept both single and multiple matching binaries. Some
supplemental mocks were corrected after dispatch; failed initial fixtures and their
corrections remain retained. The historical fix is not a positive control for the
stronger ID-pinned publication contract or multiple-binary extension.

C's raw historical suites failed because of private module bindings, mock argument
order/stream keyword differences, diagnostic wording, omitted mock `tag_name` and
legacy optional-digest behavior. Adaptations preserve raw results and diffs, route
binary mocks to actual candidate downloaders, and keep behavioral assertions.
One release optional-digest test and one certification legacy-without-digests test
are excluded from adapted counts because the explicit run10 contract requires
server digests. A certification corruption test retains original valid digests
while mutating downloaded bytes, so it still checks checksum failure before
extraction. It is a semantic fixture adaptation, not unchanged historical coverage.

Separately, six public release-download tests pass: exact binary bytes, invalid
required digest rejection before request, starter-state rejection, corruption
cleanup, network-failure cleanup and no overwrite. Release finalization accepts a
leading empty page, rejects starter/missing/malformed digests, retains the draft
and does not upload the checksum inventory on rejection. SDK fetch and certification
prepare each accept valid and leading-empty-page inventories and reject all three
invalid state/digest cases while exercising the real binary downloader. These
additional post-dispatch probes are **not preregistered tests**; their behaviors
were specified before dispatch. They address the prior C consumer gaps, but changed
prompts prevent attributing success to coordination guidance.

D's original release suite had two private/diagnostic errors and two wording
failures; certification had two wording failures. The adapter maps the private tag
selector and error text and excludes one unrelated later CLI diagnostic test that
requires a module import absent from the historical parent. Required schema-3
source, packager, tag and provenance outputs remain checked. E's ordinary copied
historical suite was nondiscriminating in controls, so its own generated fixture
was also tested: the candidate accepts an alias of the same physical directory and
rejects a different directory; substituting lexical equality rejects both. Candidate
source was not modified for any evaluator adaptation or control.

A–D original baseline/fixed controls and all preflight manifests were retained.
E's own-revision alias control fails on baseline, passes on historical fix, and
keeps different-directory rejection. Original raw failures, excluded tests,
extensions and mock corrections must not be collapsed into “all historical tests
passed.”

All five documented independent web queries and primary-source applicability.
A searched and opened GitHub Git refs/releases/assets and `gh api` documentation;
B searched Microsoft filename rules and Python ZIP documentation/implementation;
C searched `gh api` pagination and GitHub release assets; D searched/opened GitHub
release tag-target behavior; E searched/opened Python `samefile` documentation.
A/D/E reported page opens and subsequent `find` calls. B/C used returned search
excerpts without separate opens. These are worker retrospectives and retained
research notes, not a full independently captured browsing trace or an equal-cost
research benchmark.

## Targeted improvements supported by this run

The current guidance already states the core rules. More blanket reminders or a
larger startup checklist would add reading without addressing the demonstrated
execution failures. The next changes worth testing are small and concrete:

1. **Provide bounded last-owner discovery.** Add an exact-scope handoff lookup that
   finds relevant closed-owner records without ingesting the entire board. Retain
   semantic provenance checks under the mutex; a hash, empty claims or newest
   timestamp must never select the owner. Test an unchanged-byte intervening owner
   whose release was not in the recipient's inbox.
2. **Make the entire dependent chain fail closed.** The checked acquisition recipe
   should explicitly include acknowledgments/notifications and read-only-looking
   commands that assert ownership, not just file writes and job launches. Test a
   pre-publication assertion failure followed by a message command, as well as
   publication failure. Verify no dependent acknowledgment, output or job occurs until saved
   acquisition is verified. Avoid exact prose-string tests for stopped writers;
   accept documented semantic evidence or report uncertainty.
3. **Preserve event times and reconcile status without waiting for acquisition.**
   A small checkpoint wrapper should preserve inbox/progress timestamps unless
   supplied with actual new events, update completed jobs and current request/
   blocker/next-action text together, and not manufacture completion times.
   A failed claim attempt must not leave unrelated observed job completion pending.
   Regression cases should check both structured and narrative fields.
4. **Make bounded reading and complete lock identity easier to use.** Put an
   executable one-document/paged-read example on the short entry path, and reuse
   a filled lock/checkpoint wrapper rather than retyping incomplete metadata.
   Preserve ordinary web/tool/test choices. Measure startup calls/truncation and
   useful-task outcomes in the next study rather than adding a research quota to
   production guidance.

Keep the closure-output recipe: this run supports it. Keep distinct per-session
results and a brief shared-summary claim. Do not turn corrections into mandatory
approval round trips or periodic whole-board/history ingestion. A future matched
control and narrow command trace would be needed to assess collision probability
and distraction more convincingly; another zero-loss five-worker run alone cannot
establish either.

## Evidence and disposition

Ignored evidence is under `.agent-work/artifacts/coord-rerun10-20260927/`:

- `evaluation/protocol.json`, `comparison-plan.md`, snapshot/inventory/held-out
  manifests, controls, exact assignments/dispatch hashes and candidate patch hashes;
- `evaluation/write-audit.jsonl`, observer identity/completion, `score.json`,
  `status-metrics.json`, fixture construction/recovery and read-only retrospectives;
- raw candidate logs, adapted fixture diffs/results, public consumer probes and
  explicit supplemental-control limitations;
- `board/sessions/`, immutable messages, claimed research notes and each worker's
  separate `final-evidence.md`, plus the preserved shared files.

These local artifacts are intentionally ignored and subject to the existing
30-day eligible-closure cleanup policy; this tracked report preserves the study's
conclusions and limits. Five repair sessions are closed with no claims or jobs;
no candidate was integrated into the application. The evaluator stopped and joined
the sampler before final scoring. Main runtime behavior and guidance are unchanged.

Evaluator-only final checks also exposed a wrong scan-root argument and an overly
broad truncated display. Both were corrected before release; the legacy-schema
records were reviewed manually without migration or deletion. These are recorded
in `evaluation/evaluator-limits.md` and excluded from repair-worker metrics.
