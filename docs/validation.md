# Validation status

Updated 23 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current Helper Carrier candidate

The Helper Carrier implementation is based on
`a858f6f31d19431d3d15b077752f67a30cd1866f`. It is experimental and defaults off.
The 37 changed application/test/build inputs have overlay SHA-256
`287b34eada8e6c12b6762cdd38feffdf3256925a81b6ea320001b1bc3fbe7fc2`;
the local `final-source-inputs.json` records each input and the digest encoding.
Documentation and ignored evidence are excluded from that overlay digest.
The prior green cohort below does not qualify these runtime changes. See the
[implementation reference](helper-carrier.md) and [revised plan](helper-carrier-plan.md)
for the deliberately restricted integrated path: High placement outside the
existing capture converter's passband, hardware-rate composition, optional
undecoded callsign metadata and receive frequency diagnostics. Equal-rate Live
simulation retains the original Robust waveform and reports helper unavailable.
Automatic search centering, phase/clock discipline, metadata decoding and
CPU-ceiling improvements are not implemented or claimed.

Local qualification uses the existing `build/dev` Release profile, GCC 14,
ordered `-ffp-contract=off -fno-fast-math`, FLTK and the native pipe worker. No
CI resource-margin environment or relaxed acceptance threshold is applied.
Ignored `local-evidence/helper-carrier/` retains original failures and focused
diagnostics. Native device, DAC/RF, real browser, Windows runtime and prepared
Wasm/SDK qualification are unavailable in this local run; shared tests and stub
compilation do not replace them. No native display server is available.
Rev, TUI/framebuffer runtime, full-application sanitizer and release/package
qualification were not run.

Initial full contract execution finished 196/202 passed in 3083.92 seconds and
is not a green final-source result. It exposed
the original Noise action label/width being changed, a case-sensitive CLI error
assertion, a GUI advisory comparison failure, the compact Live case exhausting
its strict 40-second wall-clock budget while progressing, and the unchanged
parallel-preparation fixture reaching its 1500-second process watchdog. The
differential receiver calibration also reached that watchdog: its matrix
completed (13 modeled cases, RMS error 0.024103, maximum error 0.0574951), but
shaped, null and default-window coverage did not finish. This is incomplete
calibration, not a pass. The
layout and CLI assertion were corrected without weakening their contracts.
The GUI advisory failure also reproduced in an isolated observer run, then
passed diagnostic observer runs; its cause remains unproven. Concurrent
preparation/build work is a plausible contributor to the Live deadline failure,
not an established cause. Final-source focused results and remaining calibration
limits must be assessed separately from that initial run.

The rebuilt final-source helper/audio selection passed 11/11 serially in
96.05 seconds: helper DSP and Live integration, resampler, device-rate and
provider timing, sampled channel, snapshot copy, ALSA playback/interleave,
Windows audio stub and browser host audio. The helper Live fixture covers two
logical rates (8 kHz and 2 kHz), thirty physical-rate composite cases including
PCM16, unchanged exact pending prefixes and physical completion, and helper
updates yielding the bounded event queue to payload progress. Receiver workspace
capacity is identical with helper enabled and disabled in both native and
simulation modes. Digital spectral fixtures measured worst periodic data-relative
leakage -50.9712 dB and active-duration-normalized finite leakage -37.2048 dB;
all three callsign fixtures placed over 99% of energy within 300 Hz. The 64
four-second Gaussian controls produced no false admissions. These are specific
digital fixtures, not hardware or general colored-interference qualification.

The final-source shared GUI group passed 39/39 serially in 531.15 seconds,
including Fast/Legacy isolation, controller, link planner, original Transmit
noise behavior, launch settings, boundary checks and self-check. The complete
controller case passed in 184.82 seconds. This clears those focused GUI
acceptance checks without erasing the original advisory failure or establishing
its cause. Native rendering/device coverage remains unavailable.

The complete CLI case passed on final source (41 internal tests, 63.24 seconds),
including helper WAV composition and separated payload recovery. The original
case-sensitive diagnostic assertion was corrected to compare lowercase error
text; actual unsupported-placement rejection remains required.

The browser-worker group passed 11/13 in the ordinary sandbox, including its
137.83-second live audio case. Two preview-server tests failed during setup
because loopback socket creation returned `EPERM`. Both unchanged tests passed
with sandbox escalation permitting their temporary `127.0.0.1` servers (2/2 in
9.20 seconds). Every selected case therefore has a passing result, with the
original setup failures retained; this was not one green sandboxed invocation.
It does not qualify a real browser/device or an actual Wasm SDK build.

A direct control compiled the exact pre-feature sampled channel alongside the
final helper-disabled implementation. Across 54 fixed cases, all float PCM bits
matched for 3,759,222 samples per implementation, including noise before/after
bursts, two source lengths, three impairments, three seeds and chunk sizes
1/137/4096. Source progress and channel diagnostics also matched. Reversed-order
generation totals were 5.06978 seconds for the legacy implementation and
5.06539 seconds for the current implementation; these are diagnostic timings,
not a throughput qualification.

The final helper DSP suite passed focused GCC ASan/UBSan instrumentation.
`ASAN_OPTIONS=detect_leaks=0` was required after the sandbox prevented leak
sanitizer's process inspection; no leak-coverage claim is made. This focused
check does not replace the full sanitizer/platform matrix. Final whitespace
validation passed. Full current-candidate qualification remains incomplete due
to the recorded Live failure and calibration watchdogs; no release, publication
or exact-binary certification is claimed.

The strict compact Live case also failed alone on final source: 40.533 seconds,
1,088/1,216 bits generated, no reported receiver health fault. Its wall-clock
limit was not changed. For a matched comparison, the exact pre-feature revision
was reconstructed with `git archive`
and built using its original `build.sh`, the same compiler, Release and ordered
arithmetic flags and the same read-only dependency prefix. Its unchanged test
also failed alone: 40.444 seconds, 1,080/1,216 bits, healthy receiver. This
reproduces the deadline failure before Helper Carrier; the single pair does not
establish general throughput parity or qualify the failed case. Metadata,
commands, source/test hashes and original outputs are retained locally.

A direct unisolated sampled 1.5 kW / 4 W case recovered `0011` instead of `001`.
This failure is retained and excludes that mixed equal-rate path from Live.
The passing physical-rate fixtures used the original capture converter and
verified exact `0`, `00`, `001` progress and unchanged six-second absence
completion. A new receive FIR and ineffective hypothesis reordering were
rejected rather than changing Robust's noise assumptions or search order.

## Prior source qualification

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
reports. Sched_Hop has not demonstrated the separate sustained-performance objective.

## Prior cohort CI elapsed time and remaining uncertainty

The qualified simultaneous native/SDK pair took 121m49s, with 1424.33 unweighted
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
