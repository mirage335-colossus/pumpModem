# Validation status

Updated 7 November 2026. This records the Robust cleanup candidate, then
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

## Editable Robust case validation

### Historical nine-entry baseline

On 8 November 2026, the then-current nine-entry manual list was run on the
uncommitted suite extension based on
`c578659143f400b7a56f444d4d9773b5a813394b`. It had eight unique normalized
configurations; its entries 2 and 3 explicitly reused one measurement. This is
a historical all-entry baseline, not coverage of today's manually edited list.
The runner rereads and strictly validates the entire manual file on every
invocation; optional row selection is labeled partial. The
[historical report](../local-evidence/robust-cases-current.md) and
[JSON evidence](../local-evidence/robust-cases-current.json) contain the complete
configuration, nominal DATA/Helper C/N0, bit/`fox` calendar, service intervals,
workspace, preparation, success evidence, and limitations for every entry.
Literal `fox` uses the canonical short-message dictionary and is 22 bits.

The manual file has since changed to eight entries. Groundwave 500 kHz Encrypted
with Helper is current row 3; row numbers in the baseline table below refer only
to the old nine-entry list.

The input SHA-256 is
`03c85b3e6478836d9c2526b04d0fc1725706609d02c7e042d20114e2c2c1fee6`;
the native binary SHA-256 is
`f17a86ed10f529586a2e492aee6790650d0ca9d0669ec0a4ca456f362c1fec3a`.
The report preserves six explicitly identified measurement phases, including
their original source hashes, policies and unchanged-at-completion checks.
Later rendering and the direct FIR follow-up do not relabel earlier timings or
pool independent probability claims. This is research validation, not release,
GUI, operational-admission or complete-message qualification.

CPU measurements are local to the AMD Ryzen 5 PRO 5650U, with 12 logical CPUs
available and one numerical thread selected. Values below are prepared receiver
processing seconds per received baseband second; 1 or greater fails realtime.
The complete coarse procedure includes the streaming FIR and full public
Helper/FFT search. PracticalTracking subwindow measurements are separate and
range from 0.0545 to 0.0899 s/s; they are not an integrated receiver total.
The reused Python environment is Python 3.13.5, NumPy 2.5.4, SciPy 1.18.1,
PyTorch 2.15.0+cpu and threadpoolctl 3.8.0.

| Then-current entry in the old nine-row list | Conditional coarse-bit DATA target, dB-Hz | Complete coarse CPU, s/s | Full coarse preparation, s |
| --- | --- | --- | --- |
| 1, Sub9 | -25.02, confirmed | 0.000283 | 80.09 |
| 2/3, groundwave GPSDO | -15.02, confirmed | 0.000322 | 8.81 |
| 4, groundwave Helper | Unresolved public frequency search | Unresolved | 8.95 |
| 5, HF Helper | 8.57, confirmed | 0.04109 | 4.67 |
| 6, HF encrypted GPSDO | 7.94, separate fixed-point confirmation | 0.003078 | 4.55 |
| 7, HF FHSS | -5.50, unqualified pilot/paired evidence | 0.04406 | 75.47 |
| 8, HF unencrypted GPSDO | 7.94, confirmed | 0.003040 | 4.84 |
| 9, EME | 10.02, confirmed | 0.04881 | 10.73 |

Confirmed targets are tested adequate points, not measured minimum crossings.
They concern each bit on one sampled channel, conditional on a transmission,
with independent noise confirmation and the recorded per-case confidence
allocation. Earlier points use 36/36 successes per bit; current two-candidate
tests use 42/42. Entry 6's separate 36/36 fixed-point follow-up does not extend
the earlier failed lower-point search's confidence family. No simultaneous
cross-case or 22-bit `fox` success claim is made. C/N0 values reference nominal
received link budgets before sampled fading/selective effects, not measured
realized component powers. At Sub9's nominal -36 dB-Hz, the pilot achieved only
2/4 and 3/4 for the two bits; that nominal configuration is not qualified.

Entry 7 obtained 1/4 and 3/4 at -11.50 dB-Hz, then 4/4 for each bit at
-5.50, 0.50 and nominal 20.02 dB-Hz. The 1,105-second confirmation work forecast
exceeded the predeclared 600-second per-candidate limit, so standard 95%
confirmation stopped before starting. One separately predeclared point-specific
check planned 22 independent common-noise pairs at -5.5022 dB-Hz, counting a
pair successful only if both bits were correct. It stopped at its first failed
pair: five successful pairs, then bit 1 failed in pair six (12 receives total).
No fixed-budget probability bound is reported after this early stop, and no
second candidate or extension followed. The >90% target remains provisional.
The maintained `--paired-followup` option reproduces this separate lower-confidence
tier: all 22 pairs would yield a 90% one-sided lower bound of 0.9006280 for the
paired event and hence either bit marginal, without meeting the standard 95%
criterion or representing real-message success.

In that historical baseline, Groundwave Helper was row 4. Its low and high
pilot profiles both passed their profile checks. The failure was the handoff:
the case supplied a five-point discrete CFO bookkeeping bank, and interval-edge
neighbor padding selected four of those five points, exactly the 80% fallback
limit. That made the discrete selector report `profile_or_allowance_too_broad`
even though the continuous interval union occupied only about 40% of the public
prior. The caller consumes continuous intervals, so discrete point density was
not a meaningful measure of the proposed search width. The full FFT search then
also hit its frequency-work limit. These original failures remain part of the
historical report; they are corrected by density-independent continuous
admission at the manual-case Helper call site and an exact lag-domain
ZoomFFT search, with unchanged weak/broad-pilot fallbacks and full public
candidate coverage.

The coarse CPU estimates that complete their coverage checks have approximately
95.1–99.97% point-estimate margins, with high utilization of the allocated
thread. Worst estimated coarse-test workspace is 1.554 GB against a 5.934 GB
budget (50% of available RAM), leaving about 73.8% headroom. Conservative Helper
transients are included while the DATA bank remains live. These are capacity
estimates, not allocator or deployed-receiver certificates. Full preparation
above five seconds is prominently flagged, especially Sub9 and FHSS; no proof
of an unavoidable minimum receiver start lead is claimed. Simulator generation
and noise-factor preparation are separate from receiver readiness/service.

The short FIR extrapolation was unresolved for entries 7 and 9. A predeclared
filter-only follow-up used their actual finite extents (1,680,827 and 658,304 raw
samples), identical canonical taps/dtype/chunks/tails, an excluded warmup and
three fresh repeats under the 4M-sample/5-second caps. Direct FIR service was
0.000207 and 0.000212 s/s. The failed abbreviated evidence remains visible;
the replacement uses no duration extrapolation and did not repeat channel,
template-bank, FFT scoring or probability trials.

The exact Sub9 bank's brief native-work forecast was 29–42 minutes, so it was
not run. The S=240 collapsed profile failed; finite cubic boundaries and the
original raw FIR must be preserved. Current S=60 slow-waveform and S=3 FHSS
profiles pass fresh local rank, projector, stored-operator, wrong-bit-separation
and clean/noise winner checks; this is local validation, not a global error
bound. A matched HF timing comparison gave 0.0833–0.0927 s/s with one thread
versus 0.680–0.868 with twelve, with identical scores. The FHSS comparison gave
0.0427–0.0429 s/s for either allocation, also with identical scores. This
supports the one-thread default without dividing service time by host cores.

Native bounded FHSS template rendering now avoids the former 26-template cache
failure without raising its cap. Regression coverage also checks native Helper
calendar timing, gated-pilot metadata, finite projected-noise covariance and
boundaries, fresh-list parsing, statistical decisions, partial selection and
capacity limits. Final `./build.sh test triad --jobs 2 --build-jobs 3 --
-DDATAPUMP_COMPILER_CACHE=OFF` passed **20/20** in 11.91 seconds. Compiler-cache
use was disabled for this sandbox. No RunPod instances were needed or deployed.

Keep the old all-entry report/JSON as a historical baseline, because it contains
the other-case results and the original Groundwave failure. The current
Groundwave report/JSON is the validation checkpoint. Its JSON retains a compact
development history and neighboring regressions. Interim scalar-selection,
boundary-failure, standalone probe, and intermediate report artifacts were
removed after their findings were distilled; useful existing build caches are
retained. Reassess evidence retention when the manual configurations change.

### Current Groundwave Helper repair

The current eight-row manual file has SHA-256
`b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b`; Groundwave
Helper is row 3. The run used base revision
`24646bf65b71385085765ce29467d79ad391a5bc`, native binary SHA-256
`f17a86ed10f529586a2e492aee6790650d0ca9d0669ec0a4ca456f362c1fec3a`, and
`triad_realtime_fft.py` SHA-256
`7dd9fb00c3dd6a40912225889c10c057160dbc016a76578bc2ac1048e5552e7a`. The
manual file, source and binary were unchanged at completion. Only this row was
selected, so the other seven rows remain uncovered by this rerun.

The original failure had two causes: a five-point discrete Helper bookkeeping
bank made neighbor padding count four of five points against the 80% threshold,
despite a useful continuous interval union; then the full frequency-domain
search exceeded its work cap. The case callers now explicitly use continuous
interval admission, clipping and merging the proposal to the public prior and
measuring total union width over prior width. The 80% fallback and weak, flat,
boundary, inconsistent, gated and retuned pilot fallbacks remain. The two-pilot
profile narrowed the prior to [-0.378964, 0.168427] Hz (40.70% width). The exact
lag-domain ZoomFFT evaluated the entire admitted CFO grid rather than pruning
hypotheses.

At nominal DATA C/N0 20.0206 dB-Hz (nominal Helper C/N0 30.0206 dB-Hz), both
transmitted bit alternatives produced a correct unique coarse proposal. Each
search covered 1,474 frequency points, 13 epochs, both bit hypotheses, three
clock values, two rays and all 71 arrival points. The high-SNR/nominal gate
completed; this establishes complete coarse search for both bits on the sampled
channel, not an integrated receiver or population guarantee.

| Measured endpoint | Result |
| --- | --- |
| Complete coarse service at configured nominal SNR | 0.006148 processing seconds per received baseband second; 95% interval 0.005787–0.006611; 99.39% point-estimate service margin. Ryzen 5 PRO 5650U, one numerical thread, high utilization (0.99756–0.99959). |
| Estimated coarse-test workspace | 646.9 MiB / 5,369.8 MiB budget; 87.95% estimated headroom. Conservative accounting, not an allocator certificate. |
| Full-coarse receiver preparation | 14.21 seconds, above the five-second warning; no unavoidable minimum lead time established. |
| Short receiver readiness probe | 1.71 seconds including its excluded warmup; distinct from full-coarse preparation. |

The conditional duration model proposed -8.72375 dB-Hz for a single bit.
The native transmission calendar is 710.401 seconds for one bit and 15,830.401
seconds for literal 22-bit `fox`; these serialization times do not imply message
success. The standard 95% confirmation forecast
was 790.413 seconds, exceeding its 600-second candidate budget, so the standard
95% threshold remains unresolved. Under the separate predeclared paired
follow-up, all 22 independent common-noise pairs at -8.72375 dB-Hz decoded both
bits correctly (44 receives). Helper did not yield an admitted narrowing in
any of these receives, so all 44 outcomes used the full public ±0.672534 Hz CFO prior and
3,619 frequency points across the complete public arrival, epoch, bit and clock
bank. The 90% one-sided exact lower bound is
0.9006280202 for the paired event, hence also each bit marginal. Its 414.026
second forecast fit the paired budget. This lower-confidence bound concerns one
fixed channel and the coarse receiver conditional on a transmission; it does
not establish standard 95% confidence or literal-`fox` message success.

The 5 Hz S=60 profile passed the fresh local fidelity checks and uses 0.2-second
coarse arrival spacing; the tested 3 Hz profile failed the 1% gate and remains
excluded. Finite projected-noise boundary work was reduced from an over-limit
162 million direct-operation estimate to an exact selected-prefix/suffix sum
of about 6.56 million, without raising the 100-million operation cap. A prior
frequency-domain full search exceeded its frequency/product caps; the lag
ZoomFFT completed the complete admitted search. These bounded results do not
qualify physical-channel population performance, operational admission, full
Robust reception, DSP allocator peaks, or the GUI path. Same-source,
nominal-noise neighboring regressions also decoded both bits correctly:
Groundwave GPSDO used its complete 2-CFO public bank, and HF Helper used all 44
CFO points after a narrowed Helper proposal. Each used one seed on the same
sampled channel; these two receives per case (four total) are path regressions, not
probability or capacity requalification.

The current named `triad` checks passed 20/20 in 12.40 seconds. The case-suite
command returned its expected research-coverage status. The case suite measured
only current Groundwave Helper row 3. The separate neighboring path checks do
not requalify the other seven manual rows.

Retain the final
[current Groundwave report](../local-evidence/robust-groundwave-current.md) and
[JSON evidence](../local-evidence/robust-groundwave-current.json) as the current
checkpoint. Its JSON contains the source/configuration hashes, paired outcomes,
work accounting, neighboring regressions, compact development history, and
limitations. The superseded scalar-selection, boundary-failure, standalone
probe, and intermediate files have been removed; keep the historical nine-entry
report because it records the other configurations and the original failure.
Reassess retention when the manual configurations or checkpoint are superseded.

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
or release certification was used. The Python acquisition and tracking stages
remain separate stages, not an integrated end-to-end receiver. The bounded
[abbreviated timing harness](triad-performance.md) estimates only declared
fixed-geometry kernel work; it does not qualify physical reception or complete
receiver latency. The smoke measurement withheld an unresolved Gaussian
projection and made no comparative performance-pass claim.

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
