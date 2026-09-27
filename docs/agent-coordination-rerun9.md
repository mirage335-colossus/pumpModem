# Ninth coordination rerun, 2026-09-27

Five fresh blinded repair workers preserved the seed and all five ledger entries.
There were **zero observed lost edits, zero sampled overlapping output claims,
and 0/11 sampled claim changes with unchanged Updated fields**. All five reported
independent primary-source web lookups and completed their configured normal
build-tool regressions, with internal skips disclosed below.

Run9 showed improvements in some execution details compared with the
[eighth run](agent-coordination-rerun8.md): job launch/handle records became common,
publication timestamps closely tracked observed file replacement, and reported
source/tool-output truncation fell from five workers to one. Results were mixed:
semantic status errors persisted, publisher/checker adoption fell to four workers,
and the two measurable ledger holds were longer. One worker continued dependent
shell commands after a rejected claim update, creating an unclaimed temporary
directory. Thus this is **not a clean protocol pass or evidence of a reduced
collision rate**. The previous run already had zero observed lost edits.

## Study design and controls

The clean main baseline was `b221b766ee4275f13683e785bb78b6219d564dca`. Current
AGENTS/workflow/lifecycle/recipe files and both coordination helpers were overlaid
onto complete anonymous parent trees for the same five historical tasks:

| Case | Historical fix, withheld from worker | Problem | Parent tracked entries |
| --- | --- | --- | ---: |
| A | `0afc74b` | Concurrent release publication and SDK recipe pairing | 3,830 |
| B | `ba80ff6` | Windows archive spelling and recipe portability | 3,856 |
| C | `1719955` | Complete release inventories and binary downloads | 3,840 |
| D | `53c54c3` | Application, packaging and tag provenance | 3,879 |
| E | `0a2a738` | Physical working-directory identity in fixtures | 3,914 |

Paths, modes, symlink targets and every tracked blob were verified before creating
anonymous commits, including tracked ignored vendor inputs. All five CMake
configuration preflights passed. Original held-out hashes, baseline/fixed controls,
guidance/helper hashes, prompts and scoring criteria were recorded before dispatch.
A–D controls reproduced the relevant historical differences. E's ordinary copied
post-fix suite passes both revisions, so separate own-revision alias controls were
required: baseline fails, historical fix passes, and the distinct-directory control
still rejects. A's explicitly requested multiple-binary SDK extension exceeds the
original historical fix, which rejects it; that extension is scored separately.

Behavioral and research instructions matched run8 except paths and whitespace.
Workers had to independently use web tools, then do focused tests and normal
`./build.sh test build --cli` coverage in isolated claimed build directories.
No answers, historical solution IDs, patches, private helper names or held-out
failures were given to workers. Guidance was unchanged during this study. The
additional A publication probes introduced after run8's freeze were frozen before
this dispatch. Later C supplements are explicitly labeled below.

There were five fresh repair workers, no replacements, and at most three concurrent
repair workers. A/B started first; three C spawn attempts were rejected by the
harness's thread limit. C then started while A/B remained active, but after the
initial relay had released. D followed B and E followed A. A completed prior-study
reviewer performed the bounded dispatch check and later read-only evaluator review;
it was not a repair worker. Exact dispatched text and hashes are retained.

Blinding was instruction-based, not filesystem-enforced. No worker reported answer,
parent-history or peer-implementation exposure. Required claims/handoff reads expose
ownership paths; A also saw B's brief existing ledger summary. C/E note searches
returned selected sibling/evaluator filenames without opening their content. The
prompt's broad prohibition on evaluator files caused E to avoid the available
environment note. That ambiguity belongs to exercise setup, not normal guidance.

Source repairs were isolated; integration files shared one board. This does not
exercise actual conflicting source saves, shared Git mutations, physical devices,
real abandoned jobs, different vendors' harnesses or distributed filesystems.
No matched no-coordination control or complete worker tool transcript was captured.
Research and read-behavior evidence below includes explicit post-run self-report;
it cannot establish unchanged normal browsing prevalence or negligible distraction.

## Collision handling and overhead

| Measure | Eighth run | Ninth run |
| --- | --- | --- |
| Lost ledger edits / sampled overlapping output claims | 0 / 0 | 0 / 0 |
| Unchanged Updated on sampled claim changes | 0/13 | 0/11 |
| Actual release owner acknowledged | 5/5 | 5/5 |
| Terminal retained claim respected | Both tested workers | Both tested workers |
| Shared file released before separate final evidence | 5/5 | 5/5 |
| Bounded board reader used, self-report | 5/5 | 5/5 |
| Publisher/checker used for claim changes, self-report | 5/5 | 4/5; C used custom atomic publication |
| Startup output reported truncated | 0/5 | 0/5 |
| Later source/build/web output reported truncated | 5/5 | 1/5; D narrowed subsequent reads |
| Independent primary web lookup, self-report | 5/5 | 5/5 |

The evaluator narrowed a covering shared-directory claim to a guard file. A relay
then an unchanged-byte owner acquired/released the ledger. Obsolete relay notices
were delivered; workers reconciled later provenance. B handled its separate file's
obsolete reply ID while retaining the actual pending request `b-shared-2`, without
waiting for a corrected reply. The guard and B-only header/entry were preserved.

The final chain was **quiet-owner → B → A → C → retained fixture → E → D**.
After C's release, a synthetic 61-day-old terminal record retained the ledger.
D/E requested the designated recovery and did not clear the claim based on age or
terminal status. The evaluator recovered it using construction evidence that no
independent worker or jobs ever existed, preserving the original record. D then
aborted an acquisition before publication/write when its expected hash differed
because E had appended; D inspected E's release and acquired afresh. This is direct
self-reported evidence of a useful collision-prevention check, with the resulting
preserved ledger independently observed. Run8 also had stopped stale-baseline attempts.

There were no repair hints or added protocol reminders. Normal release/recovery
notices and two harness notifications that an inbox message was available were sent.
The recovery delay was evaluator-controlled; do not attribute it to coding overhead.

The unchanged coordination primitives passed 26 publisher tests and 36 reader/checker
tests, including concurrent publication and injected failures. These deterministic
checks supplement sampling; they do not excuse worker-level protocol deviations.

The observer completed 2,253 scans, 122 events and 37 output-change observations.
It slept 250 ms between traversals; the longest traversal was 421 ms. Sampling is
not write interception. C's append was first seen without a contemporaneous owner;
its acquire/release was too brief to capture at both ends. This does not prove an
unclaimed write. No malformed sampled registry records or ambiguous parsed claims
were found. Files outside watched roots, including A's temporary directory and
worker artifact logs, were not covered by the output sampler.

Only A/B holds had both endpoints sampled: **15.17 s / 24.30 s**, versus approximately
0.55 s / 0.47 s in run8. C/D/E durations remain unknown, not zero. All five final
evidence files were written after ledger release, approximately 32/38/47/39/42 s
later for A/B/C/D/E. This confirms release before unrelated reporting; it does not
establish less coordination work. Coordination-only time and call count were not
measured. All five still reported broad combined startup reads despite the
one-document-per-result advice; absence of truncation is not proof of low burden.

## Status and failure-path findings

- **Dependent commands continued after failed publication.** A proposed a future
  progress timestamp. The publisher rejected the checkpoint, but the shell still
  launched regression in an already-claimed build tree and created a new, not-yet-
  claimed `/tmp` directory. A disclosed and reconciled the job/claim immediately,
  then used fail-fast chaining for its final launch. No competing owner was
  reported; this remains a real claim-before-write deviation, not erased by repair.
- **Jobs were more visible, but launch ordering remained imperfect.** All five
  published launch/handle information. C left a yielded job as launch-pending through
  another inspection and omitted a separate launch checkpoint for a later rerun.
  E's first focused test yielded before launch intent was published. These are
  narrower failures than treating every job as `none`, not full compliance.
- **Publication freshness improved; event timestamps did not consistently improve.**
  Across 42 sampled worker records, file mtime followed Updated by at most 84 ms,
  versus run8's observed A candidate roughly 34 s old at publication. File mtime is
  only a proxy. A/B helpers sampled times before inbox processing; C/E sometimes
  used checkpoint time rather than the actual event time. A fresh Updated cannot
  validate Last inbox check or Last meaningful progress.
- **Contradictory current text persisted.** B's closed handoff still says “final
  evidence next.” D published recovery-pending text despite reading the recovery
  notice. E retained recovery-pending progress after its handoff recorded release;
  closure corrected E's text. C's earlier-owner baseline language also survived
  later ownership changes. Structured completion and narrative status can disagree.
- **Final output can outlive released ownership.** B redirected its closure command's
  stdout into its own artifact file. The publisher can finish writing that output
  after its record releases the artifact claim. This was disclosed and is supported
  by its command/helper structure; no actual competing write was observed.
- **Custom publication still needs the full protocol.** C used staged hard links
  and atomic replacement, but no candidate checker/publisher and incomplete lock
  owner identity. E used unvalidated atomic progress-only replacements and sometimes
  incomplete lock metadata. No partial message or registry-candidate leakage was
  reported or sampled. Optional helpers are not mandatory, but equivalent safety
  requirements still apply. B's automated full-claims scan was parsed for overlaps
  rather than individually exposed to the model; that distinction is preserved.

No worker reported a closure validation retry. A had a rejected relative-board
message call and the failed timestamp proposal; D had two invalid read-only checker
invocations. These and minor path/command guesses were corrected and disclosed.

## Repair quality, research and validation

| Case | Worker focused evidence | Normal build-tool group | Independent candidate evaluation |
| --- | --- | --- | --- |
| A | 40 passed | 8/8 | SDK matching/multiple accepted; mismatch/orphan/extra rejected; 3 publication probes + 5 expanded publication tests pass |
| B | 24 passed | 12/12 | 22 unchanged historical tests pass |
| C | 39 release + 27 SDK + 19 certification passed | 10/10 | 87 adapted high-level tests + 5 public download probes pass; supplements and historical-equivalence gap below |
| D | 66 release + 43 certification passed | 13/13 after environment retry | 43 unchanged certification tests and 69 adapted release tests pass |
| E | 14 passed | 22/22 after environment retry | All 14 pass under aliased temporary root; actual generated fixture accepts alias, rejects different directory; lexical negative control rejects both |

A's raw historical 26-test run has one unrelated release-notes assertion failure
and five private-helper/mock-identity errors. Public probes verify actual created
release ID, competing tag/release rejection, upload inventory, first/late upload
failure, draft retention and ordinary/experiment Latest behavior. They do not
mutate GitHub. D's raw release suite has one wording mismatch, a private selector
name difference and one unrelated later CLI diagnostic test requiring `release.os`.
The adapter changes selector name/error wording and excludes that unrelated test;
certification remains unchanged, including required schema-3 provenance fields.

C's raw historical suites are retained, not declared passing. Adapters route binary
mock output through the real downloader, allow equivalent argument ordering/header
whitespace and size-before-digest failure wording, map the imported module name,
and use complete release metadata. Five private-helper tests were excluded from
the adapted count. An independent read-only review found that the replacement
probes did not cover all guards in those excluded tests.

**C historical-equivalence gap:** post-freeze high-level probes confirm SDK fetch
accepts `state=starter` and an absent server digest, while certification preparation
accepts starter assets. The historical fix rejects those inputs. Missing server
digests in certification are intentionally accepted by both. These stricter guards
were neither explicit in the frozen prompt nor present in the same form in its
parent SDK/certification implementation. Therefore this is an additional historical-
fix-equivalence gap, **not a retroactively invented declared-contract failure**.
The candidate's declared checks pass; it is not demonstrated equivalent to every
historical guard. No findings were sent back for candidate repair.

Additional post-freeze C tests exercise leading empty pages, multi-page asset
inventories and a release on a later page after an unrelated release; all three
consumer suites pass. Initial evaluator fixtures mistakenly mocked away the
historical certification downloader and later paginated a nonpaginated Git-ref
response. Both invalid attempts and their narrow corrections are retained. They
are evaluator defects, not worker failures. Original/adapted/supplemental results
remain separately identified; raw test counts are not directly comparable scores.

Research self-reports and notes distinguish actual lookups: A searched and opened
four GitHub/CLI primary pages; B queried Microsoft/Python ZIP and filename sources;
C queried GitHub/CLI pagination and binary download sources; D queried Git's
revision-resolution documentation; E searched Python directory identity, opened
`pathlib` and used find. B/C/D relied on primary search-result content rather than
separate page opens. All recorded applicability and limits. This is evidence of
requested independent research alongside coordination, not a matched study of
normal web-search frequency or a guarantee of research effectiveness.

Every normal group has three internal source-SDK ELF fixture skips because
`patchelf` is absent. E additionally has two native Arch cases omitted because
`pacman` is absent. D's first group was 12/13 and E's 18/22: GPG setup failed before
relevant signing assertions. Short socket probes distinguished sandbox EPERM from
long AF_UNIX paths; approved outside-sandbox full reruns passed. E also retained an
initial three-case test-argument setup failure before correction. No skipped or
setup-blocked scope is counted as passed. These are build-tool checks, not native
Windows, application/GUI runtime, device, live-service or release certification.

## Targeted improvements supported by this run

Keep ordinary guidance short. The evidence supports improving executable examples
and focused checks rather than adding another mandatory narrative checklist:

1. Show fail-closed command chaining around claim publication: no dependent mkdir,
   writer, generator or job launch may proceed after validation/publication fails.
   Add an injected rejection case that verifies downstream writes never start.
2. Make closure examples send publisher output to the tool response, not a file
   whose claim that same publication releases. Resolve all output writers first.
3. Preserve actual inbox/progress event times separately from publication time,
   and replace jobs, blockers and next action together. Demonstrate a completed
   nonempty inbox and release clearing obsolete “pending/next” language. Existing
   prose already requires this; repeating it at greater length is unlikely to help.
4. Keep an equivalent custom-publication example visibly complete, including lock
   identity and candidate validation. Favor reusing the existing safe publisher
   without making helper choice override harness policy or research/tool use.
5. In evaluation guidance, audit excluded private tests against each consumer's
   public behavior before calling replacements sufficient. Declare stricter
   historical guards in the next frozen prompt if they are intended acceptance
   requirements. Explicitly allow only the designated environment note, while
   keeping answer-bearing evaluator artifacts barred.

The first two address concrete failure paths. The third/fourth target recurring
execution gaps already described by the guidance. The fifth improves measurement.
None requires curtailing searches, tools or useful coding. This run cannot promise
that further exercises will uncover no new issue.

## Evidence and disposition

Ignored evidence is under `.agent-work/artifacts/coord-rerun9-20260927/`:
`evaluation/` contains tree/control/prompt manifests, observer JSONL, score and
status metrics, candidate hashes/patches, raw/adapted/supplemental tests, survey
summaries and final verification. `board/notes/` and `board/artifacts/` retain
worker research, failures, workarounds and final evidence. The raw observer is a
study artifact, not routine startup reading.

All five repair candidates remain frozen, uncommitted and unintegrated. Main source,
index and guidance are unchanged; this report is the only tracked-tree addition.
The observer stopped. Worker and synthetic claims are released; evaluator claims
are released after final checks. Evidence follows normal closed-session 30-day
retention, with no new archive. The durable findings are in this report.
