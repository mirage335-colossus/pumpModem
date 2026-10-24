# Validation status

Updated 24 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current Helper Carrier candidate

The accepted source candidate is based on
`b2d3539919f0955cee94986269ccbd6402e37b1b`, with 42 application/test/build inputs
and overlay SHA-256
`107aa9d0ad36910fc087015b6357b006a412552092473a81560012f8086606ff`.
`local-evidence/helper-v3/final/source-inputs.json` binds those inputs;
documentation changes are excluded. Source review and the focused containment
checks below are accepted. The normal application/GUI/worker build passed for
this source, and the focused ASan/UBSan and shared GUI checks below passed. The
294-registration normal run completed with 288 passes, five setup failures and
one timeout. Exact web and native display retries passed all five setup-failed
cases with unchanged source and binaries, giving 293/294 distinct cases passed.
The current physical check observed clean transmission and same-row pending
`001` to completion, but strict individual-prefix coverage remains incomplete.
Contract preparation coverage is **failed/incomplete**:
`scheduled_parallel_preparation` timed out at 1,500.12 s.
No full matrix pass is claimed.
New binary identities are recorded in
`local-evidence/helper-v3/final/containment-application-build-result.json` and
`normal-binary-inputs.json` in the same directory; earlier binaries are not
provenance for this source. No publication or certification is claimed.

Earlier diagnostics retain their own source identities. The earlier 33-input
overlay is
`80ac7f2fba143fca05d623c6cb8cd2e5e94158a7561abb7d723390c1638d205e`, retained
in `local-evidence/helper-v3/final/pre-bootstrap-source-inputs.json`. The later
overlay is `22125d5bbf5344afa5cf11a243ba125513706df48ffde6648623544521b11027`;
its source and linked runtime identities are preserved in
`local-evidence/helper-v3/tx/receive-22125-build-binding.json`. The earlier normal
binary identities remain in `final/pre-bootstrap-normal-binary-inputs.json`
under the same evidence root. Documentation and ignored evidence are excluded
from the source overlays. These historical bindings remain separate from the
current 42-input manifest. A subsequent normal placement build and repaired
test-only fixture are identified separately below.

Neither historical diagnostic snapshot is a qualified final source. The later
HF diagnostic recovered the expected ordinary message, but receiver-health and
separately observed bit-prefix checks remain unsatisfied; the EME run was
cancelled incomplete. Full qualification of the current candidate remains
pending. Those snapshots predate
the fixed-epoch containment policy below; they do not establish final rolling-
reception assistance. No publication, promotion or certification is claimed.

Messages travel exclusively on ordinary Robust signals. Helper carries only
its tone, punctuation and optional callsign/time. Actual-length callsigns use
one to seven APSK symbols; at most 48 Q16 Unix-time bits fit three symbols/24 ms.
FF1/AES-256 encryption preserves length without an authentication tag, and CRC7
uses only existing spare bits. Metadata remains unauthenticated and grants no
UTC or payload authority. Keyed operation hides and ignores helper callsign,
even invalid inactive preference text; clear mode validates it. This
normalization/parser change passed source review and the focused shared
controller/parser checks below. Helper-only transmission remains a Developer action. See the
[reference](helper-carrier.md) and [plan](helper-carrier-plan.md).

### Current behavior and containment policy

Helper Power remains editable, with 10 kW and 15 kW presets added. High/Low
placement uses actual shaped occupied data bandwidth plus gating allowance and
the nearest power-qualified leakage guard. The former logical-sample-rate High
floor is removed. The startup failure, “Helper Carrier requires a valid data
passband at the output sample rate,” came from treating the default shaped
3.6 kHz setting as an unshaped 3.6 kHz band. The corrected occupied bandwidth
fits its existing data carrier without changing Rate. Narrow 100 Hz, SSB
3.6 kHz and wide 12 kHz settings all retain selected modem geometry; there is
no automatic Rate adjustment. Hardware FHSS output preparation also no longer
adds a duplicate 200 Hz gate allowance; the reported EME configuration now
plans its High helper at 2,250.3125 Hz. This is a placement correction, not a
passing EME reception result. Closest digital placement still does not promise
that a radio's narrow SSB filter can pass the complete data/guard/helper bands.

Mixed Live output can disable an unavailable helper and visibly report the
reason while retaining ordinary unscaled payload PCM. A helper-specific device
setup request can retry once without that request only before the first PCM
callback. An actual output gap still fails TX without retry. Helper-only output
fails when its helper cannot be generated. Hardware-rate negotiation remains
independent of the logical modem clock, with a practical 48 kHz helper floor.

The decided primary-replacement policy is restricted to a fixed configured
transmit timestamp with an unchanging epoch-admission bank, using the same
fixed-bank condition as ordinary refresh. It also requires explicit
**Complex only**, an actual native continuous/noncalendar/noncompact backend and a
structurally identical bounded normal `Error`-reset recipe. Clock sync, FHSS,
DSSS/Interleave, calendar, compact, configured drift and differential paths are
excluded from primary replacement. Auto, energy,
differential, compact/correlator, scheduled and rolling/unknown-epoch cases
retain their ordinary bank; an additional helper lane is separately eligible. Eligible large fixed banks
can use at most 257 frequency/clock alternatives per narrowed profile while
retaining all admitted key/profile/private-epoch and original start alternatives. The original clock
domain includes continuous residual uncertainty in helper-to-data transfer;
only an equivalent quarter-chip-sized domain can share a clock hypothesis.
Before qualification, bounded history attempts to retain the full original
initial acquisition aperture. Switching declines if history starts after an
original receiver origin. Precommit replay eligibility bounds the configured
physical capture aperture and its resources; it does not use received length or
a message-size quota. Full configured source quotas remain intact. Exact dormant
ordinary state and subsequent raw PCM remain available for replay until the first
bit accepted by an ordinary detector. Reference loss, failed acquisition or unavailable bounds before that
boundary restore the original state and input. The first ordinary accepted bit
commits that same full decoder and releases fallback state; the precommit bound
no longer limits accepted payload lifetime. No helper-specific message-length or
source quota applies. Accepted payloads retain ordinary limits and future
acquisition can broaden on helper loss. The additional small optional lane
remains separate. Native-backend containment and immutable cold-start/replay
grants for ordinary `Error` recovery passed source review and focused checks;
the focused sanitizer run passed, while full contract coverage remains
incomplete and broader sanitizer qualification remains pending. Both retain full long-text
and attachment framing, source quota, decompression, recovery, next-poll bit
progress and six seconds of fully scored absence. Tone/metadata never commit
payload or supply END.

Adjacent assisted reception can use a real linear bandstop FIR; its fully
observed centered windows retain the actual input offset and never insert EOF
zeros. Dormant original receivers and FIR-delayed consumers remain in processing
frontier accounting. Raw fallback is unchanged. Filtered noise is correlated;
the helper presence budget does not certify payload false-alarm behavior after
this frontend. Link Planner shows prospective conditional counts and their
required measured frequency uncertainty. Its ordinary payload SNR, probability
and CPU curves remain unchanged; those counts are not a measured speedup.

Weak-frequency existence admission combines independent fixed-grid raw-time
evidence groups with the single-window/variance allowances under the stated
AWGN model. Refined covariance/model intervals are local estimates, not exact
global 99% confidence intervals. Centered-CW bias cancellation is restricted to
a timing-qualified straight dwell region; unqualified windows that could cross
punctuation or metadata retain the conservative model bound. No uniform weak
qualification or GPSDO replacement is established. The metadata correction
projects the reversal through the actual sampled real-carrier/finite-Gram bin
model instead of a scalar envelope average, removing a carrier-phase-dependent
timing bias in clean dense metadata fits.

### Completed focused normal checks preceding the later diagnostics

Checks used the compatible `build/dev` Release GCC 14/FLTK/system OpenSSL profile
with ordered floating point and unchanged resource margins. The first nine-helper
run passed six and failed three in 127.09 seconds. Its failures are retained:
`helper_live` did not decode a strong callsign, `helper_receiver` failed its
original assertion, and `helper_sim` failed its near-Nyquist passband assertion. The
subsequent corrections and focused reruns are separate results:

| Scope | Completed result |
| --- | --- |
| `helper_live_assistance` | Passed in 109.36 s in the nine-helper run, including ordinary full-payload and fallback cases. |
| `helper_output_fallback` | Passed in 0.03 s; mock-provider checks include unchanged ordinary PCM and no retry after output begins. |
| `helper_conditioned_native` | Passed in 4.62 s; deterministic native capture/processing-frontier fixture, not physical audio hardware. |
| `helper_carrier`, `helper_metadata`, `helper_assistance` | Passed in 0.14/0.01/1.31 s respectively in the nine-helper run. |
| Corrected metadata/Live reception | `helper_live` and `helper_receiver` passed in 15.55/1.89 s, 17.45 s total. |
| Conservative receiver and simulation controls | `helper_receiver` and `helper_sim` passed 2/2 in 4.05 s. |
| Focused Link Planner helper checks | Passed after correcting the floating-point preset fixture; selected 100 Hz, 3.6 kHz and 12 kHz geometry remains unchanged. |
| CLI | Initial run passed 45/46 methods in 47.754 s. The 19-byte framed helper fixture incorrectly expected unrestricted `!` instead of the received-text replacement `_`; the corrected fixture passed separately in 3.692 s. Application source was unchanged, so the other 45 method results remain applicable; this was not a fresh complete 46-method pass. |

Detailed evidence is retained in
`local-evidence/helper-v3/final/helper-focused.log`,
`helper-focused-details.log`, `conservative-focused.log`,
`local-evidence/helper-v3/metadata-review/focused-final.log`,
`local-evidence/helper-v3/planner-tests-diagnostic.log`,
`planner-tests-corrected.log`, `local-evidence/helper-cli-full-v3.log` and
`local-evidence/helper-cli-framed-v3-corrected.log`. Original failures remain
failures rather than being relabeled by the passing reruns. These focused
results do not constitute a full normal regression pass or a new CPU benchmark.

### Accepted containment and focused evidence

The accepted containment inputs include `src/live.cpp` SHA-256
`6db6403ff86b4edfe87a0aaf9aae5d70aeb330db0f97457d751e51d38f442153`,
`src/pattern_receiver.cpp`
`4503fe72fb0ef0be4617c50d4a660016026c1d1f5a5810ff3b4a162b54e2665d` and
`src/streaming_modem.cpp`
`3ad9451439200133dc869e4df370a4ad009c76005a45753104270ceabc1c87ef`.
Focused fixtures retained their full configured 1 MiB source quotas. The
precommit resource boundary did not become a message-size or received-length
quota.

| Focused scope | Accepted result and limit |
| --- | --- |
| Finite resource query | Native and compact queries cover all 32 ordinary source candidates and the 111/112 aperture boundary. This is resource-query geometry, not a payload-length cap; compact primary replacement remains ineligible. |
| Fixed-bank exact bits | Exact `001` and scored END across all five original epochs; 4,625 oscillator alternatives became 45. |
| Full framed source | The 19-byte/1,216-bit fixture retained FEC, decompression, recovery, bit progress and scored END: 42.586 s process CPU for 790.769 s media. |
| Native-provider paths | Conditioned success and exact raw fallback passed. These use actual 48 kHz PCM through a provider fixture, not another physical-radio qualification. |
| Ordinary error recovery | Five forced ordinary-row `Error` resets retained immutable cold grant 769,730 and replay grant 1,164,226 bytes without an input-domain/provider/revision reset or drops. Later negligible-helper `001` and scored END passed over 17.28 s media. |
| Rolling admission | Primary replacement declined; helper off/on constructor grants were equal and both recovered exact `001`. CPU totals were 231.643/221.541 s; no rolling speedup is claimed. |
| Fixed strong-reference comparison | Matched ordinary control used 240.128 s process CPU versus 0.678877 s with assistance for 14.5552 s media, about 354× in this synthetic strong-reference fixture only. It does not qualify weak HF/EME reception, observer cost or general real-time CPU margin. |

Evidence is retained in `local-evidence/helper-v3/conditioning-two-grant-*`
logs, including unit2, 001, full, error3, rolling, control, native-success and
native-fallback. Earlier native-only declines and a 90 s full-framed timeout
remain retained. A compact fixture with two tracks exhausted its output queue;
the corrected fixture uses the ordinary default of 16. A bright-helper
post-error control also failed its 90 s bound. A repeated trace-path setup
failure was followed by a passing fresh-directory run; this sequence does not
establish a single cause for every earlier failure. The native fixture's unique
temporary filename changed only after these passing runs; the new normal
application build passed, but its default regression run still must complete. None of those failures is relabeled as
a pass by the accepted focused checks.

### Focused ASan/UBSan — selected paths passed

The focused run passed all 41 commands: 11 fresh instrumented compiles,
12 instrumentation checks, eight links and ten runtime commands. Twelve actual
implementations were instrumented; metadata alone reused an object after exact
source/include-closure and object-hash verification. All 219 frozen input
hashes and the current 42-input overlay matched. Every command exited 0 and
every direct child was reaped, with no timeout, retry, ASan or UBSan finding.
Scoped process verification found no remaining owned process and released the
CPU window.

Coverage includes the actual helper DSP, channel, Pattern/Streaming/Correlator
receivers, framed transfer and FIR input-origin/EOF handling, plus native-provider
three-bit Live success, original replay and ordinary-Error replay. The framed
batch check recovered the exact 19-byte/1,216-bit source and withheld completion
when six seconds were removed from the observed quiet tail. The three native
selectors used requested and observed CPUs 0–7 with unchanged 90 s internal and
300 s outer limits; other commands used CPU 0. Compilers ran sequentially on one
CPU under compiler-only 600 s/5 GiB limits. These are functional checks, not
throughput measurements.

Leak detection was disabled (`detect_leaks=0`). Full-payload Live, GUI and
production CLI entry points, physical devices/RF, throughput, the platform
matrix and full sanitizer/release qualification are outside this scope. Normal
archive dependencies, OpenSSL, LZMA and system libraries were not instrumented.
Exact commands, hashes, results and process limits are retained in
`local-evidence/helper-recenter/sanitizer/result.json`, `report.md`,
`identity.json`, `commands.jsonl` and `process-verification.json`.

### Focused shared GUI/controller/parser checks — passed

The focused GUI targets built successfully for the unchanged 42-input source.
All three runtime checks passed: controller `--helper-controls` in 0.4146 s,
the complete launch-command parser suite in 0.0081 s and planner `--helper-only`
in 0.2644 s. Coverage includes invalid inactive encrypted callsigns with clear-
mode recovery and Live validation, explicit strategy/fixed-versus-rolling gates,
unchanged Rate and the reported HF/EME planner counts. It does not establish
full shared GUI or native-adapter conformance. Source binding, commands and
results are in `local-evidence/helper-v3/final/containment-focused-gui-result.json`
and `containment-gui-*.log` in the same directory.

### Later placement build and hardware-rate output fixture

The normal application/GUI/worker build passed for 33-input overlay
`39954499171f16b28325ef6c26eb30fd06688183b9686f6f0751eb5165bc5b2b`, recorded
in `local-evidence/helper-v3/final/placement-source-inputs.json`, with build and
binary evidence in `placement-application-build.log` and
`placement-normal-binary-inputs.json`. This application build is not full
regression qualification. Subsequent repairs changed only the new test fixture;
the final `tests/test_helper_output_fallback.cpp` input SHA-256 was
`67a2dcbe412707ebdb9f96dd79313d82d83dbc1c8af4b74ea88471621d084430`.

The complete fallback suite, including the FHSS emitted-frequency check on actual
48 kHz output PCM through the mock provider, passed in 0.683 s. This checks the
output waveform against the common single-gate placement, alongside exact
ordinary fallback PCM, output discontinuity handling and helper-only failure.
It is mocked-provider PCM coverage, not an additional physical radio test.
The passing output is retained in
`local-evidence/helper-v3/final/placement-output-utc-test.log`.

The original fixture first failed its gate-allowance assertion before playback.
After that setup repair, a mock UTC-acceptance timeout reported one open, one
format callback and zero source callbacks (`1/1/0`). A matched fixture with
proper mock UTC acceptance passed. Those are test setup repairs, not production
TX failures. The failed checks and matched control remain in
`placement-output-test.log`, `placement-output-repaired-test.log`,
`placement-utc-before.log` and `placement-utc-after.log` in the same evidence
directory; they are not overwritten by the passing run.

### Earlier exact HF simulation — no payload acquisition

The actual-controller simulation of the reported encrypted crystal/IC-7100 HF
configuration, with 1.5 kW helper, 4 W data and ordinary raw bits `001`, completed
its bounded probe with exit 3 after 600.378 s wall time. The process was reaped;
this is an unsuccessful acquisition result, not a passing transmission or
physical-end test. Helper pending and received rows were observed, but no
ordinary bits or payload END were observed. No meaningful assistance benefit
for this exact case is demonstrated by the earlier `80ac7f2f` overlay.

Resolved geometry was 6,120 Hz logical DSP, 14.2222 s per payload symbol,
100 ppm sample-clock error, 30 Hz carrier offset and
0.50249 degrees/sqrt(second) phase diffusion. Estimated helper C/N0 was about
39.7 dB-Hz after the existing interleaved waveform backoff; this is a configured
budget, not measured precision. The ordinary search reported +/-7.97985 Hz
coverage against +/-90.45 Hz requested. At the deadline the probe reported
45.646 s of media, zero committed processing samples, 59 receiver instances,
1,470,007,381 bytes requested DSP buffering and 3,978,260 KiB process peak RSS.
The final reported aggregate process CPU time was 4,774.59 s. These diagnostic
resource totals do not qualify real-time margin or identify one cause for all
missed reception.

Investigation identified a deterministic precision-receiver bootstrap rejection
at a 30 Hz offset before dwell timing qualifies: the conservative untimed model
can prevent the frequency reference needed for assistance. The later diagnostic
below does not retroactively pass this failed run. Exact command, resolved
settings, progress, deadline and stderr are retained in
`local-evidence/helper-v3/current-simulation/crystal-final-command.log`,
`crystal-final.jsonl` and `crystal-final.stderr`.

### Pre-containment HF recovery and incomplete EME diagnostic

Before the fixed-epoch containment policy, the same HF simulation on overlay
`22125d5b` recovered ordinary raw bits `001`, then their fixed-dictionary text
`e`, in the same GUI row, ID 2. The
three-bit pending row was observed at 70.340 s and its completed text at
70.381 s. Ordinary physical END was observed, but the earlier prefixes `0` and
`00` were not separately observed. The search-limited health state also remained;
the probe explicitly reported `receiver_health_ok=false`,
`all_pending_prefixes_on_same_row=false` and exit 1. This demonstrates ordinary
payload recovery in that case, not complete clean progress/health qualification,
a CPU speedup, general weak-link sensitivity or GPSDO replacement. This earlier
primary-route result cannot establish assistance benefit for the current rolling
route, which retains the ordinary bank. The reported HF and EME commands select
Auto and are ineligible for current primary replacement; any benefit through the
additional lane needs its own evidence. The command
and observations are retained in `current-simulation/crystal-recenter-command.log`
and `crystal-recenter.jsonl` under `local-evidence/helper-v3/`.

The exact EME diagnostic on that same overlay was cancelled after its last
observation at 178.998 s wall time and only 1.074 s media. No ordinary bit or
payload END was observed. The ordinary search requested 9,432,739,510 bytes of
DSP buffering before a sufficiently long helper observation could condition
acquisition, and reported incomplete half-chip clock/frequency/rate coverage.
This was a cancelled/incomplete diagnostic, not a completed receive or deadline
pass. The cancellation record and original command/progress remain in
`current-simulation/eme-recenter-cancelled.json`, `eme-recenter-command.log` and
`eme-recenter.jsonl` under the same evidence directory.

### Adjacent physical transmission and cable return

The actual 40 W helper/4 W payload test used 48 kHz hardware with the nearest
helper center at 2,062.5 Hz and data center at 1,500 Hz. The public Volume 100
run completed TX and recording but returned 122,056 samples at or above the
16-bit clipping threshold, with peak 1.0 FS. It is retained as a clipped return,
not clean-signal qualification.

A separate repetition using the public Volume control at 20 completed TX and
recording with exit 0, zero clipped samples and peak 0.33835 FS. Returned helper
RMS was 0.13502 FS versus payload RMS 0.04351 FS: a 9.8358 dB ratio for the
configured 10 dB. This establishes adjacent-tone output and clean cable return
at that Volume setting. It does not qualify receiver decoding, RF power
calibration, weak-link assistance or GPSDO replacement. Both original and clean
repetition results, waveforms and analyses are retained in
`local-evidence/helper-v3/tx/gpsdo40-adjacent-final-*` and
`gpsdo40-adjacent-volume20-*`.

### Source-bound native receive completion with pending-poll gap

A separate normal-Controller physical duplex run linked the frozen `22125d5b`
libraries, using public Volume 20, 48 kHz hardware and the 2,062.5 Hz helper.
TX completed 17.4002 s media at 19.0556 s elapsed. The receiver exposed the exact
ordinary raw bit `1` as completed at 46.7336 s, row ID 2, revision 1, with no
unexpected bits. Capture continued until about 110 s, so completion occurred
during actual physical capture rather than at EOF. The observed run had no
receiver-health fault, drops, overruns, backlog or errors.

The strict wrapper nevertheless exited 3 after 110.110 s because it never
observed pending `1` in its polls; it reported no pending-to-completed same-row
coverage. This is completed physical receive evidence, not a passing full
wrapper or next-poll progress qualification. It also makes no weak-RF or GPSDO
replacement claim. The wrapper binary SHA-256 was
`88c141a7278c96d7fe590ece75a316c7678f246994dfac7a9337cb5e91d926b8`.
Command, output and failure result are retained in
`local-evidence/helper-v3/tx/gpsdo40-rx-overlay22125-volume20-command.json`,
`gpsdo40-rx-overlay22125-volume20.log` and
`gpsdo40-rx-overlay22125-volume20-result.json`.
`receive-22125-build-binding.json` records the exact source/library binding;
`receive-22125-postrun-hashes.json` confirms all linked/runtime inputs remained
unchanged. This evidence belongs to pre-containment `22125d5b`, not later
bootstrap or containment changes, and does not qualify current rolling-route
assistance.

### Current-source physical duplex — completion observed, prefix coverage incomplete

The current `107aa9d0` source and linked libraries were checked through the
actual shared Controller and native audio path, at 48 kHz, public Volume 20,
40 W helper/4 W data and the planned 2,062.5 Hz helper. All 42 source inputs
and linked binary hashes were unchanged. The hashed GUI ELF was not launched
through a toolkit in this physical check. The transmit result reported
`passed=1`, with 45.8446 s of media; the 110 s WAV recorder exited 0 and cleanup
completed. The overall strict runner nevertheless exited 3, as detailed below;
this was not a transmit failure.

Helper row ID 1/revision 2 was pending at 1.8561 s and received at 7.6607 s.
Ordinary row ID 2/revision 2 first appeared with exact `001` pending at
46.4406 s, then completed as `001`/`e` on that same row at 77.8076 s, with no
missing or unexpected bits. Capture remained open to 110.012 s, so completion
did not depend on EOF. No receiver-health failure was reported. The separate
`0` and `00` pending prefixes were never observed in the polls, so the strict
all-prefix predicate failed and full individual-prefix/next-poll coverage
remains unqualified. The observed `001` pending-to-completed transition is
verified; it is distinct from that remaining coverage gap.

The returned PCM had zero clipped samples and a peak of 0.337677 full scale.
Measured helper/data centroids were 2,062.4894/1,500.9495 Hz in disjoint bands,
with a 9.9925 dB RMS power ratio against the configured 10 dB. This establishes
clean adjacent cable output and the observed ordinary receive completion,
not weak-RF, default-Auto or GPSDO-replacement benefit. Binding, strict result,
spectrum and original output are retained under `local-evidence/helper-v3/tx/`
as `current107-physical-volume20-binding.json`,
`current107-physical-volume20-result.json`,
`current107-physical-volume20-spectrum.json`,
`current107-physical-volume20-tx.log` and the matching WAV.

### Withdrawn bootstrap proposal

The unaccepted recipe bootstrap was withdrawn from production after independent
review identified a native unprocessed-queue (`Q - R`) overrun and unreserved
simulation DSP use. Its patch is retained in ignored evidence for future
experiments; this withdrawn path is not implemented behavior and does not fix
or qualify the exact weak EME case. A separate private joint raw-bin frequency
estimator remains unaccepted, with no production integration planned for this
candidate. Payload-contamination handling and its stochastic phase prior need
a supported argument and validation before any future integration. Completed
private diagnostics do not establish production payload SNR, CPU speedup or
full-case qualification.

The safe existing assistance routes remain optional: full ordinary receiver
grants, retained history and the conditional receiver must coexist inside the
configured resources. If they cannot, helper work yields while ordinary
framing, source quota, progress and fully scored physical END remain unchanged.
The production resource contract remains unchanged: no additional RAM allowance,
source-quota change or disk/I/O contract change. The review rejection and
original diagnostic failures remain distinct from future qualification.

### Normal regression results and remaining qualification

The normal application/GUI/worker build passed with exit 0 using
`./build.sh --web-worker --build-dir build/dev --jobs 2`, bound to the current
42-input manifest by `final/containment-application-build-result.json`.
The 294-registration normal run completed and was reaped with exit 1. Original
outcomes are 288 passes, five failures and one timeout, all accounted for in
`local-evidence/helper-v3/final/normal/reconciliation.json`:

| Scope | Original normal-run outcome | Follow-up status |
| --- | --- | --- |
| Contract | 208 passed; one preparation timeout | Failed/incomplete preparation coverage |
| Shared GUI extras | 33/33 passed | Complete for this selection |
| Regular extras | 36/36 passed | Complete for this selection |
| Web | 11 passed; two socket-permission setup failures | Exact pair passed on loopback-permission retry |
| Native GUI | Three immediate display-startup failures | Exact display-only retry passed 3/3 |

The reconciliation repairs reporting of CTest output with adjacent `***` status
text; original logs and failed outcomes are unchanged. The contract's
`scheduled_parallel_preparation` timed out at 1,500.12 s, at line 3157 of
`local-evidence/helper-v3/final/normal/contract.log`. The unchanged watchdog and
this failure preclude a full contract or matrix pass. The preceding input
closure equivalent to committed `b2d3539` also timed out at 1,500.05 s; neither
result establishes passing preparation throughput. The overall run log is
`local-evidence/helper-v3/final/normal-qualification.log`.

The web retry ran only `build_preview` and `web_preview`: both passed in 7.50 s
with the same 42-input overlay verified before and after. The original
`PermissionError: [Errno 1] Operation not permitted` failures remain recorded.
Retry accounting is in
`local-evidence/helper-v3/final/normal/web-loopback-retry-reconciled-reconciliation.json`.
Original native startup failed because Xvfb could not bind its listeners and
the tests could not open the display; adapter assertions did not run then.
The exact display-only retry passed all three tests in 225.14 s, including
`gui_workflow` in 139.74 s, with unchanged source, binaries, tests and deadlines.
Its result is in
`local-evidence/helper-v3/final/normal/native-display-retry-reconciliation.json`.
Across the original run and these five strict setup retries, 293/294 distinct
cases passed. The preparation timeout remains unqualified; the retries do not
turn the original 288-pass/five-failure/one-timeout result into a passing full
matrix. Normal CPU work is complete. The current-source physical check above
observed clean output and same-row `001` pending-to-completed reception, with
strict individual-prefix coverage incomplete and runner exit 3.
`local-evidence/helper-v3/final/final-verification.json` confirms the 42-input
overlay, all five accepted application/archive hashes, complete registration
accounting and cleanup of the normal, retry and physical processes. Docs-only
changes are excluded from the source guard.

The focused sanitizer coverage does not replace the full sanitizer suite.
The pre-containment HF recovery leaves progress and health coverage unresolved;
EME needs a complete run. Current physical `0`/`00` next-poll coverage and full
duplex qualification remain unqualified despite the observed `001` transition
and physical completion.
A proposed 64-point frequency history failed known-frequency truth coverage at
24 dB-Hz in `local-evidence/helper-v3/eme-history/regression-history64.log`.
That history proposal remains unaccepted; the recipe bootstrap above is
withdrawn, and the private joint estimator is not qualified. None is a passing
weak-EME fix. Current-source qualification is limited to the results recorded
above; prior diagnostic snapshots and binaries do not qualify it. In-progress,
failed or cancelled work is not a pass.
No weak RF-link or 5.8 GHz propagation benefit is established by these fixtures.

Unknown full-band helper discovery, arbitrary UTC/private-epoch ambiguity and
every repeated-message onset are not solved by one tone. Dynamic phase/timing
assistance requires independently qualified measurements and supported geometry.
True scheduled, Symbol_Burst and deferred hopping retain ordinary fallback when
assisted acquisition is unavailable. Physical helper sidebands, colored
interference and DAC/RF dynamic range remain limitations at extreme power
ratios. A tone cannot separate RF error, Doppler and sample-clock error. Windows
runtime, real browser, prepared Wasm/SDK, alternate frontends and release/package
certification remain separate scopes.

### Historical Helper Carrier evidence — not current qualification

The previous candidate was based on
`b47c7a0ebd6a29d21ffa8748d5bc7ffbbeb7e942`, with 44-input overlay
`d9f1212860bd02830b1d9bab2e27141882a29273804f30c34508e1f017d9b0dd`, recorded
in `local-evidence/helper-live-fix/final-source-inputs.json`. That implementation
was incorporated into the current `b2d3539` base; its results do not qualify the
new placement, FIR, conditional bank, precision or metadata changes above.

Its normal contract run passed 206/207 in 3,576.19 s. The unchanged
`scheduled_parallel_preparation` watchdog expired at 1,500.05 s while
`shared.serial` was active. The 44 recorded input hashes for that run match
committed `b2d3539`; this is the exact preceding baseline timeout. A separate
earlier `helper-carrier-v2/contract.log` run timed out at 1,500.11 s in
`shared.parallel`, but its candidate inputs differ from `b2d3539` and are not an
exact baseline comparison. Neither run established a full contract pass or
preparation throughput parity. The earlier source also passed 39 shared GUI
cases, all 13 web registrations after two sandbox socket-setup failures were rerun with
authorized loopback access, 36 regular cases outside contract, nine focused audio
registrations and three native GUI cases on a private 2,400×1,800 display.
Its regular/contract union was 236/237, with the same preparation timeout.
The original desktop adapter-height failure, socket setup failures and passing
reruns remain in the retained `helper-live-fix/final-*` logs.

The earlier physical 48 kHz cable return measured a helper near **6,380 Hz**,
9.87 dB above the payload for a configured 10 dB ratio, with zero clipped
samples, and an encrypted one-bit duplex repetition passed in 50.07 s.
Separate Low/cochannel controls also returned their tones. These measurements
used the superseded distant High placement. They are historical audio evidence
only and are explicitly excluded from current adjacent-placement, current
physical transmission and sensitivity claims.

The old 52-configuration observer benchmark and focused five-suite ASan/UBSan
checks likewise precede this candidate. They do not establish current heap,
CPU margin, weak-frequency precision or sanitizer coverage. The prior sanitizer
scope excluded Live and dependency instrumentation; disabled LSAN was not leak
coverage. Earlier invalid planner, frequency, suspension, noise-tail,
helper-loss and late-private-epoch failures remain retained. The old strong
calendar fixture checked one advisory epoch; the new multi-epoch conditional
route needs its own qualification. Historical helper-only success never
established full mixed-link sensitivity or RF propagation performance.

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
