# Validation status

Updated 8 November 2026. The historical cleanup checkpoint below was then
uncommitted and based on `357a50970a0c6214eeaade7c5704bbc39abd4b79`, now
committed as `419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. It is not release
certification. The [source and binary inventory](../local-evidence/robust-cleanup/source-binaries.json)
records 2,904 surviving implementation, test and build files and selected binary
hashes. This compact manifest substantiates this cleanup checkpoint; retire it
when the checkpoint is superseded. Its source-inventory SHA-256 is
`2405f259c050b68d7bbabad9628eb97ff750e53c6d9f51012f47530719f7c74b`.
This is a post-validation inventory, excluding documentation/evidence, not a
before/after timing comparison. Raw run logs are temporary; remove them after
this checkpoint is accepted unless a specific active validation or release
requirement still needs them. The concise outcomes and limitations below are
the maintained record.

## Complete headless Robust and manual capacity

The current implementation is based on `cb992078` with uncommitted changes.
It connects canonical native samples through public Helper/coarse acquisition,
fine fixed-affine admission and adaptive tracking work, bit/null decisions,
scored quiet slots and the canonical short-message dictionary. Robust remains
unavailable in the GUI. No received packet length or transport EOF establishes
message completion.

On 8 November 2026, the named check
`./build.sh test triad --jobs 2 --build-jobs 3 -- -DDATAPUMP_COMPILER_CACHE=OFF`
passed **20/20** after the final v3 integration (26.73 seconds; the earlier frozen
measurement checkpoint also passed 20/20 in 26.48 seconds). This includes the complete receiver, stream,
quiet diagnostic, manual capacity, regression and native channel/source checks.
The eight-row manual run completed with unchanged source, binary and input-list
hashes. Its result is **measured with unresolved margins**, not a capacity pass.
The [JSON](../local-evidence/robust-manual-capacity-current.json) and
[consolidated report](../local-evidence/robust-manual-results-current.md) retain the complete
component ledger and same-stream comparisons with 1, 2, 4, 6 and 12 workers on
the local AMD Ryzen 5 PRO 5650U (six physical cores, twelve logical CPUs).

The dedicated manual suite rereads `_tests-RobustModem.md`; the current eight-row
SHA-256 is `b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b`.
It is an absolute complete-work CPU/workspace estimate, distinct from the
relative degradation regression. Literal `fox` is 22 dictionary bits. See the
[manual contract](robust-manual-capacity.md) for all-thread comparison, full-prior
fallback, fine-transition work, recurring preparation, quiet completion and
statistical limitations.

The [continued-stream measurements](../local-evidence/robust-manual-sustained-current.json)
also completed with all source, binary, case-list, base-report, helper and CPU
identity checks unchanged. Each observation kept one actual fine session alive
for a two-second prefix and cumulative one-, two- and four-second continuation,
including verified new native cache fills. Prefix and finalization are charged
once per slot. The results include the base run's unchanged remaining receiver
components; timing repeats are not independent reception-success trials.

| Current case | Workers | Sustained nominal service (s/s) | Nominal margin | Nominal decision |
| --- | ---: | ---: | ---: | --- |
| Sub-9kHz | 2 | 0.895 | 10.5% | Unresolved interval |
| Groundwave GPSDO | 4 | 0.904 | 9.6% | Unresolved interval |
| Groundwave Helper | 4 | 1.467 | -46.7% | Point failure; extrapolation unresolved |
| HF Helper | 2 | 1.277 | -27.7% | Point failure; extrapolation unresolved |
| HF encrypted GPSDO | 4 | 0.602 | 39.8% | Conditional nominal pass |
| HF encrypted GPSDO/FHSS | 6 | 1.050 | -5.0% | Nominal failure |
| HF unencrypted GPSDO | 4 | 0.592 | 40.8% | Conditional nominal pass |
| EME | 4 | 0.948 | 5.2% | Unresolved interval |

These are this receiver implementation's service margins on the local computer.
They are not a saturated whole-machine CPU ceiling. Actual workers execute
independent filter hypotheses, but native basis rendering, projection and global
tracking work still limit utilization. No idle-core division or hypothetical
parallel speedup is applied. The conservative cold-state envelope remains
separate and unresolved for every row; its repeated 256-state startup does not
by itself prove ordinary reception failure.

All explicit peak-workspace/RSS estimates fit the 5,196 MiB measurement budget,
with approximately 31–92% estimated headroom. Observed first-bank preparation
was 5.2–83.6 seconds; this is a readiness concern, not an established unavoidable
pre-transmission lead time. Simulator generation is separate. The base report
contains total triple-DATA and Helper C/N0, provisional model targets and the
exact native one-bit and literal-`fox` transmit calendars. Those C/N0 fields are
nominal received link-budget references, not measured powers of each sampled
fade. No complete-receiver >90% sensitivity threshold is established by these
timing measurements.

Development checks exposed and corrected three reception defects before the
current measurement: FP32 innovation arithmetic failed at very high SNR,
legacy 0.01 Hz final frequency snapping discarded long-duration coarse
resolution, and fixed-affine residual clock hypotheses introduced an unintended
IF-dependent frequency shift. Fine processing now declares FP64, uses the
public duration-dependent frequency grid with its full prior penalty, and
preserves acquired total CFO in the fixed-affine basis. The adaptive model
remains diagnostic and its full work is measured. Focused numerical, wide-time
phase, prior-decomposition and lifetime tests passed. Memory reviews also removed
retained session/cache cycles and included simultaneous profile-validation banks
and transient projection/control buffers.

The frozen-source native message gate passed: DATA 0, DATA 0, DATA 1 decoded
canonical `e`, followed by two fully scored quiet slots and physical completion
before transport EOF. Its captured-prefix EOF negative control stayed
incomplete. This one high-SNR case establishes a functional path, not all-case
sensitivity. The default invocation repeats this gate on current sources. The
earlier source-changing functional exercise is superseded by this frozen result.

The [sensitivity pilot](../local-evidence/robust-manual-sensitivity-current.json)
received both independent bits correctly at 8.113563 dB-Hz in current case 7,
each followed by two quiet slots and a complete work receipt. A fresh,
predeclared [confirmation](../local-evidence/robust-manual-confirmation-current.json)
at 11.113563 dB-Hz received its first **5/29 pairs** correctly, then stopped at
its memory-accounting gate. These were ten successful complete single-bit
endpoints, not five full `fox` messages. The incomplete confirmation establishes
no >90% probability bound. Do not pool different SNR points or count timing
repeats as additional successes.

That confirmation's resource monitor incorrectly treated the largest reaped
child's lifetime RSS as native post-exec residency. Its five child counters
exactly matched the Python workers' peaks. A separate zero-sample discriminator
reproduced this: a 125.7 MB launcher/child high-water counter accompanied only
20.5 MB combined sampled RSS in two verified native executables after exec.
The future confirmation helper now samples owned post-exec native processes,
retains conservative workspace/runtime/decoder allowances and counts persistent
worker processes across waves. Polling does not certify every transient peak.
Four focused regression fixtures pass. The original resource stop and all five
successful pairs remain unchanged. The original cost allowance did not admit
completion after diagnosis; the experiment was not restarted or extended.
After registering those fixtures, the affected `triad_cases` check passed again.

The v3 default manual invocation now uses the continued-session measurement.
The [promotion receipt](../local-evidence/robust-manual-promotion-current.json)
verifies that the promoted probe is identical and that DSP sources and the
native binary did not change. It preserves the original measurement phases:
the separate continuation phase used family size 24; the v3 default uses 64
for the current full list. The v3 wiring and accounting were tested separately,
including acquisition gating, missing-stage subtotals, slot normalization,
workspace/RSS/utilization and propagation of supported nominal failures. The
old observations are not relabeled as a full run of v3.

Retain the linked base, continuation, allocation, pilot, incomplete confirmation
and promotion JSON while they support these current estimates or unresolved
sensitivity/resource findings. Replace them when equivalent current measurements
supersede those claims. The duplicate temporary continuation/allocation runners
and earlier exploratory message/allocation outputs have been removed.

### Retained historical coarse evidence

Earlier tests used separate coarse and abbreviated tracking stages. They do
not measure complete receiver work or qualify the current endpoint. The old
nine-entry baseline is retained in [its JSON](../local-evidence/robust-cases-current.json)
only to preserve then-current configuration/provenance and unresolved sensitivity
outcomes: nominal Sub9 had insufficient successes, and the HF/FHSS fixed-point
paired check failed on pair six. Those observations must not be relabeled as
current eight-entry coverage or complete-message outcomes.

The earlier [Groundwave JSON](../local-evidence/robust-groundwave-current.json)
records resolution of the Helper handoff/search failure. A five-point bookkeeping
bank had rejected a useful continuous interval through neighbor padding; the
corrected caller uses continuous union width, preserving weak/broad-pilot
fallbacks. Exact lag-domain ZoomFFT completed the public CFO bank. At nominal
20.0206 dB-Hz DATA / 30.0206 dB-Hz Helper, both bits produced correct coarse
proposals. Its coarse-only service was 0.006148 s/s on this Ryzen 5 PRO 5650U;
that value excludes complete fine tracking and message work.

At -8.72375 dB-Hz, all 22 independent noise pairs recovered both coarse bits
using the complete 3,619-point prior after Helper fallback. The 90% one-sided
paired-event lower bound was 0.900628, conditional on one sampled channel and
the old coarse endpoint. This is neither a 95% bound nor literal-`fox` success.
The old `--paired-followup` campaign entry was removed with the superseded staged
suite. Keep these compact historical JSON records while those sensitivity
questions remain unresolved; retire them when current complete-receiver evidence
replaces the relevant claims. Their obsolete implementations are not retained.

## Test timing investigation

On 8 November 2026, the dirty working tree based on `64f7feb6` was timed with
GCC 14.2, the native Release FLTK configuration and two test jobs. `test all`
stopped before testing: `test_terminal_ui` and `test_framebuffer` failed to link
native audio symbols. Separate web and build-tool runs also had preview socket
permission failures and temporary GPG-key setup failures. Retain the three logs
in `local-evidence/test-timing-2026-11-08/` to diagnose these failed checks;
remove them when the link and environment failures are resolved or superseded
by equivalent current evidence.

## Transmit key policy checks

The uncommitted policy change based on `64f7feb6f298ba844f45ecbf1c57f02219158d16`
passed `./build.sh test contract` (26/26), `./build.sh test triad` (19/19), and
`./build.sh test gui` (27/27) on 7 November 2026. These runs used the existing
Release `build/dev` profile, two test jobs and four build jobs, with
`CCACHE_DISABLE=1` because the sandbox could not write the compiler cache.
The initial compile also exposed a missing test include, corrected before
these passing runs. Documentation link checks and `git diff --check` passed.

Coverage includes ordinary private/Amateur key parity, temporary noise DATA
keys and forced encryption, public-Amateur Helper decoding, changing callsign
ciphertext across absolute dwells, unchanged Helper vectors, independent
background noise, and the unavailable Robust boundary. Noise-mode coverage is
at the native policy/generator API; no GUI reconnection, operational noise
endpoint, physical transmission, or release certification is claimed.

## Cleanup validation

The CLI, FLTK GUI and pipe-worker applications rebuilt successfully. The FLTK
adapter and document test executables also compile and link; their native-window
runtime tests were not rerun. GUI controls remain, with Robust operation
explicitly unavailable pending connection to the canonical triad backend.

| Final affected check | Result |
| --- | --- |
| `./build.sh test contract` | 25/25 passed, 25.13 seconds |
| `./build.sh test triad` | 18/18 passed, 13.09 seconds |
| `./build.sh sanitize contract` | 25/25 passed, 37.49 seconds; ASan, UBSan and leak detection enabled |
| Final crypto/keyring fixture refresh under sanitizers | 2/2 passed, 8.69 seconds |
| Final focused crypto, keyring, Fast codec/CLI, GUI Fast, web worker/bridge | 7/7 passed, 40.40 seconds |

The triad Python aggregate ran 201 checks, with one native-dependent case
skipped there and covered by its separately registered native test. These are
small correctness checks, not full-observation or performance campaigns.
Independent source comparisons found the retained GF(256)/Reed–Solomon bodies,
Payload keystream path and prepared CBC implementation unchanged by the final
removal of unused recovery, framing and cryptographic APIs. Independent frozen
crypto vectors now exercise the retained prepared CBC API.

Earlier in the cleanup, the shared `regular` group passed 29/29 in 44.08 seconds
after removal of the two obsolete recovery/framing suites. The later crypto
change received the final affected checks above. Legacy passed 8/8 in 1.79
seconds. Web passed 13/13 in 146.47 seconds, including the retained Fast audio
case; the worker and bridge were rechecked after the final crypto change.
The initial Fast and GUI runs passed 35/37 and 26/27 respectively. Corrected
focused reruns completed every previously failing case; all 37 Fast and 27 GUI
cases have passed across those runs, but neither full group was rerun after
all cleanup changes.

The build-tool suite passed 27/32 in the sandbox; the five local socket/GPG
fixtures then passed outside it. Workflow tests passed 3/3, abbreviated-inference
tests 8/8, and certification-tool unit tests 86/86. The maintained-document
path/anchor audit and `git diff --check` pass.

Initial failures remain failed invocations: an invalid tiny channel fixture,
a CLI error exit code, obsolete GUI simulation/readiness expectations, and
missing native audio linkage for adapter test targets were corrected. The
initial sanitizer invocation could not run LeakSanitizer under the sandbox's
ptrace restrictions; the passing reruns retained leak detection outside that
restriction. No omitted check is counted as passed.

## Limits

No RunPod resources, GPU workload, physical audio device, real-browser session,
or release certification was used. The complete headless receiver and manual
capacity checks are described above; the GUI remains disconnected. The separate
[abbreviated regression harness](triad-performance.md) measures relative changes
in declared work and cannot establish absolute receiver capacity. Its historical
smoke measurement withheld an unresolved Gaussian projection and made no
comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

Useful [pattern](validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](validation-data/triad-v3/2026-11-05-stochastic/README.md)
measurements remain curated, source-bound research because their conclusions
inform the current triad. Preserve concise results, assumptions, limitations and
needed provenance; do not accumulate duplicate raw captures or parallel
implementations. The
[archive-retirement manifest](validation-data/triad-v3/archive-content-retirement.json)
records removed implementation copies and changed archive hashes; retained
triad data leaves were preserved byte-for-byte. Original source/internal hashes
remain historical provenance, not qualification of repacked archives or current
code. Consult the [evidence index](indexes/evidence.md) and Git history for
superseded results and their limits.
