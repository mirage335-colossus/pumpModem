# Validation status

Updated 23 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current Helper Carrier candidate

The default-off continuation is based on
`b47c7a0ebd6a29d21ffa8748d5bc7ffbbeb7e942`. The 44 modified application, test
and build inputs have overlay SHA-256
`d9f1212860bd02830b1d9bab2e27141882a29273804f30c34508e1f017d9b0dd`.
`local-evidence/helper-live-fix/final-source-inputs.json` records sorted UTF-8
path, NUL, lowercase SHA-256 and LF identities. Documentation and ignored
evidence are excluded. No publication, promotion or certification is claimed.

Messages travel exclusively on ordinary Robust signals. Helper measurements
can guide one optional ordinary receiver, retaining long text, attachments,
framing, decompression, recovery, source quota, exact progress and six seconds
of fully scored absence for completion. Optional speculation yields to ordinary
allocation and acquisition. Once bits are accepted, that payload retains the
ordinary processing and completion path. See the [reference](helper-carrier.md)
and [plan](helper-carrier-plan.md).

Actual-length callsigns occupy one to seven APSK symbols. Q16 Unix time uses at
most 48 time bits and three symbols/24 ms. FF1/AES-256 encryption preserves bit
length and adds no authentication tag; CRC7 uses only spare bits. Metadata is
unauthenticated and grants no clock or payload authority. Keyed operation hides
and ignores helper callsign. Helper-only transmission is a Developer action.

### Reproduced failure and current local evidence

The reported 36 Hz/interleaved configuration selected an 8 kHz hardware clock
for a 6,380 Hz helper, failing before PCM output. Helper operation now requests
at least 48 kHz, independently of the unchanged modem clock, and validates the
actual provider format. ALSA, Windows and host adapters honor this floor. A
capture provider unable to supply it can retain ordinary reception with helper
observation disabled. High, Low and cochannel placement reach the physical
waveform; cochannel deliberately overlaps payload and is discouraged.

The helper amplitude now accounts for the existing outer-DSSS amplitude gain.
Actual PCM controls retain payload gain and AWGN while checking the requested
power ratio. A physical 48 kHz cable return of the first reported configuration
measured helper RMS 0.2534 FS near 6,380 Hz and payload RMS 0.0813 FS near
1,500 Hz: 9.87 dB versus the configured 10 dB, peak 0.630 FS, zero clipped
samples. This is clean physical return evidence, not a weak-link sensitivity
measurement. The preceding native source cohort also completed the exact
one-bit encrypted payload in duplex after physical absence. The final native
repetition also passed in 50.07 seconds, using 48 kHz hardware, with the exact
completed bit at 47.92 seconds and no error or unexpected bits. Its FFT work
briefly accumulated 1.98 seconds of backlog before returning to zero; this is
not a zero-backlog or latency-improvement claim. Separate four-second physical
Low/cochannel transmissions returned 550/1,500 Hz tones at 0.519 FS RMS.

Link Planner reports helper effective power, received power, C/N0, level in a
10 Hz cell, audio frequency and whether the reference qualifies. It preserves
ordinary payload SNR and probability calculations. The second reported example
has predicted helper C/N0 15.76 dB-Hz, or 5.76 dB in 10 Hz, below the required
6 dB presence gate. That configuration does not promise a usable reference.

Weak-tone acquisition uses a finite-Gram narrowband fit and a lifetime
multiple-look existence budget of at most 0.01 per reset under the stated AWGN
model. Long-baseline doubled-phase confidence retains all surviving frequency
aliases or falls back to a wide interval. Frequency and phase measurement dates
are separate. Live uses original earliest qualified evidence only to rank one
advisory private epoch. Phase qualification can activate later with continuous
correction at the committed frontier. Timing bounds remain fixed at admission.

A 52-configuration observer benchmark retained in
`local-evidence/helper-live-fix/observer/` used 48/96 kHz, ±20/100 Hz search,
tone/callsign/encrypted metadata and clean/30/24 dB-Hz conditions. Tracker plus
precision receiver cost 2.24–11.98 ms CPU per audio second. Precision-receiver
retained requested heap was 5,808 bytes unkeyed or 5,968 keyed; peak 6,000 plus
an 8-byte handle (allocator-usable 6,024). Stack, caller keys and OpenSSL direct
allocations are outside that measurement. All clean metadata controls decoded.
At 24 dB-Hz, 56 wide intervals missed truth under model contamination; none of
the eight intervals ≤0.1 Hz or 22 intervals ≤0.1758 Hz did. This does not promise
uniform weak-helper qualification or full-Live CPU headroom.

Local builds use `build/dev`, Release GCC 14, FLTK, system OpenSSL and ordered
floating point. No resource-margin environment or relaxed acceptance limits
are applied. Current normal application/worker build and focused phase and
unambiguous-onset calendar retention checks passed. The complete assisted Live
suite also passed in 68.50 seconds in the normal two-test contract run, including
both full-payload scenarios and the constrained-memory ordinary fallback.
The final normal contract run finished with 206/207 passes in 3,576.19 seconds.
All seven helper cases passed. Strict Live passed in 76.88 seconds, full
differential calibration passed all sections in 1,407.03 seconds, and the full
45-method CLI suite passed in 46.94 seconds. Complete CTest and CLI details are
retained in `local-evidence/helper-live-fix/final-contract-details.log` and
`final-cli-details.log`.

The sole failure was `scheduled_parallel_preparation`, which hit its unchanged
1,500-second watchdog at 1,500.05 seconds. Reservation and default/fp32/fp64
serial/parallel arithmetic comparisons completed without assertions;
`shared.serial` was still active. This is failed/incomplete preparation
coverage, not a passing full contract. The prior watchdog occurred later in
`shared.parallel`; these runs do not establish preparation throughput parity.
Shared GUI passed all 39 cases in 188.30 seconds, including Link Planner and
helper control visibility/state. Complete details are in `final-gui-details.log`.
All 13 web cases have passing coverage: 11 passed in 140.78 seconds, while
`build_preview` and `web_preview` failed only at sandbox-blocked socket creation
(`EPERM`) before running. The unchanged pair passed 2/2 in 6.74 seconds with the
already authorized temporary loopback listener access. Original setup failures
and full detail remain retained. This is native worker/host/Node coverage, not
real-browser or prepared Wasm execution. The 36 regular cases outside the
contract passed in 135.35 seconds. The regular/contract union therefore has
236/237 regular passes and 206/207 contract passes, with the same preparation
timeout in both inventories; all nine focused audio registrations passed.
The 201 regular/contract overlaps were not repeated. All three native GUI cases
passed in 225.08 seconds on a private 2,400×1,800 display, using the original
assertions and deadlines: workflow 139.99 seconds, adapter conformance 84.97
seconds and document conformance 0.10 seconds. The initial physical-desktop run
passed workflow and document conformance but failed the adapter visibility
check when the window manager changed client height from 1,008 to the required
1,040 pixels, moving the bottom-anchored device control by 32 pixels. The focused
unchanged fixture passed on the larger private display; its temporary diagnostic
was removed byte-for-byte before the complete native rerun. No runtime layout
change or weakened assertion was needed. Original and passing logs remain in
`final-native-details.log` and `final-native-private-display-details.log`.

### Retained failures and earlier evidence

The earlier compact-codec/full-payload cohort passed seven helper suites,
45 CLI cases, 39 shared GUI cases and nine focused audio/channel cases. Its web
suite passed 11 initially; the two socket-blocked preview cases passed with
permission for temporary loopback listeners. Those results precede the current
physical-rate, power, confidence and Live control changes and do not qualify
this overlay. The still earlier 207-case contract run had 206 passes and one
1,500.11-second preparation watchdog, in 3,494.98 seconds. Differential
calibration completed in 1,408.61 seconds. Full-contract qualification was not
established.

Current development retained the original invalid planner fixture, incorrect
negative-frequency control, misaligned two-stream suspension fixture, missing
simulation noise tail, expensive automatic compact receiver selection, lost
long-symbol acquisition on helper disappearance. These led to focused
corrections; failures are kept separately from passing reruns. The simulation now captures actual channel
noise far enough past EOF for the resident FFT acquisition window to run,
including with helper disabled, without synthesizing completion or more TX.

Adjacent-epoch selection at a late-second boundary remains a helper limitation.
The first accepted CW window has no certified maximum detection delay, and
unknown channel startup/arrival delay can itself cross a private-epoch boundary.
Both weak and strong late-start failures are retained. The calendar-lifetime
fixture explicitly uses a strong 1,500 W/0.004 W ratio, 40 dB less added noise
and an early fractional transmit date (0.10–0.30 seconds); it checks one healthy
advisory epoch across ordinary refresh, not the ambiguous late case. The
separate weak 30.02 dB-Hz fixed-epoch comparison retains the realistic waveform
and demonstrates acquisition outside the original frequency bank. Full unknown
clock-prior assistance needs concurrent epoch hypotheses with separately
accounted false-alarm, memory and CPU budgets; it is not established here.

Earlier instrumented cohorts do not count as current sanitizer coverage.
Final instrumentation rebuilt all eight actual DSP implementations, including
the correlator, and linked all five complete helper suites. Carrier, metadata,
precision reception and physical simulation passed without ASan/UBSan findings
in 0.67/0.07/9.59/8.49 seconds. These deterministic checks ran alongside the
normal prerequisite build; no throughput claim is made. Assistance ran
separately after compilation ended and passed in 1.17 seconds with its original
five-second recovery budget. All 18 compile/link/run commands succeeded and
all 247 frozen dependency hashes matched current source. The correlator compile
took 274.91 seconds and 2.73 GiB child peak RSS, within its compiler-only
600-second/5-GiB cap. Archive dependencies, OpenSSL and Live remain
outside that focused scope; disabled LSAN is not leak coverage.

### Qualification limits

The full ordinary search remains admitted. One optional compatible receiver can
use helper frequency, phase and punctuation timing, within actual surplus. No
universal CPU-ceiling or full-bank sensitivity improvement is claimed. Guards,
scheduled acquisition, Symbol_Burst and hopping combinations retain their
ordinary fallback when helper assistance is incompatible. Unknown full-band
discovery, arbitrary UTC/private-epoch ambiguity and every repeated-message
onset are not solved by one tone. Dense metadata requires a clean strong helper.
A tone cannot separate RF error, Doppler and sample-clock error.

Physical helper sidebands, colored interference and DAC/RF dynamic range remain
real limitations, especially at extreme power ratios. AWGN false-admission
controls do not establish equivalent guarantees for arbitrary interference.
Native cable tests do not qualify RF propagation or the 5.8 GHz experiment.
Windows runtime, real browser, prepared Wasm/SDK, alternate frontends and
release/package certification are separate unexecuted scopes here. Shared,
worker and adapter stubs do not replace them.

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
