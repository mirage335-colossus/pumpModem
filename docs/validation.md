# Local validation record — version 0.7.2

The application and portable runtime are native C++. Python is optional test
tooling for FLTK/CLI builds and required to embed Rev resources at build time;
it is not installed with the application.

## Relative LPI observation against one receiver bit — 17 September 2026

The advisory now compares total observer bit durations with the receiver's
one-symbol design reference. Both listeners use the same received C/N0,
normalized so one sampled symbol has the existing 18 dB Es/N0 planning
reference. It is explicitly uncalibrated. Simulation/live link power and
oscillator presets no longer enter the LPI estimator. TX targets affect the
ratio only through selected waveform geometry. GUI details and CLI JSON
distinguish total N:1 observation, N-1 additional durations, normalized C/N0,
and the supplemental current-draft exposure comparison. The encryption-off
hypothetical warning remains, including for tone and unavailable numbers.
No waveform, wire format, receiver admission, physical completion or pending
progress behavior changed; the tuning reference retains its existing value.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). GCC `cli` passed in
30.84 seconds. The final estimator and eight shared GUI checks passed in
11.71 seconds: application, inspection, inspection page, layout, contract,
link boundary, adapter boundary and boundary regression. The seven selected
Rev checks passed in 1.58 seconds: estimator, application, inspection,
inspection page, layout, contract and link boundary. An initial fixture for
equal time-bandwidth products used an explicit decimal duration that rounded
up one sample; the corrected fixture uses exact spreading geometry. The separate
sample-rounding regression remains. Fixed numerical references, duration and
bandwidth scaling, unchanged keyless/keyed ratios, invalid input and numerical
limits passed. CLI assertions compare identical LPI objects under different
channel power, noise figure, oscillator and RX search settings.

The remaining 17 non-controller development-contract suites passed in 179.24
seconds. The isolated controller suite passed in 71.68 seconds, including
identical LPI summaries and details across every simulation and oscillator
preset, geometry-dependent TX changes, fixed-geometry target changes,
encryption/tone transitions and exact 001/1,216-bit draft endpoints. Together
the 28 selected GCC suites include all 21 development-contract suites.
`git diff --check` passed.

Native window rendering and physical reception were not tested. This remains
a relative Gaussian energy-detector model, not a measured reception threshold,
interference measurement or guaranteed hidden-traffic allowance. See
[the model and its limitations](lpi-estimates.md).

## Hypothetical LPI estimates without encryption — 17 September 2026

The advisory now computes the encrypted private-pattern scenario without a
selected key, including tone experiments. It preserves the current sample,
chip and symbol timing and C/N0; tone experiments use the corresponding
private pattern's modeled bandwidth. The GUI prominently labels encryption-off
results as hypothetical, and CLI JSON exposes `hypothetical_encryption` and a
warning. The warning persists when the existing strong-signal or numerical
limits prevent a number. Current draft exposure stays a duration comparison;
the advisory neither selects a different automatic profile nor re-encodes
authentication overhead. No key generation, encryption enablement, waveform
change or receiver change occurs.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). GCC `lpi_estimate` and
`cli` passed in 27.91 seconds, followed by eight shared GUI checks in 10.36
seconds: application, inspection, inspection page, layout, contract, link
boundary, adapter boundary and boundary regression. The corresponding seven
selected Rev checks passed in 1.57 seconds: estimator, application, inspection,
inspection page, layout, contract and link boundary. Added assertions compare
keyless and keyed numerical results at identical geometry, cover tone and
model-limit warnings, and preserve the independent exact `001` wire endpoint
and actual transmission settings. `git diff --check` passed.

The remaining 17 non-controller development-contract suites passed in 174.60
seconds, and the controller suite passed separately in 68.88 seconds. The
controller assertions cover encryption off/on/off/on/tone transitions, numerical
availability, warning withdrawal/restoration and exact draft bits. Together the
28 selected GCC suites include all 21 development-contract suites.

Native window rendering and physical reception were not tested; adapters and
desktop geometry are unchanged. The hypothetical result describes private
patterns under the existing model, not the actual public or tone waveform.

## LPI energy-detection advisory — 17 September 2026

Added a shared GUI advisory and `lpi` JSON results to CLI `estimate` and
`analyze-link`. The weak-signal radiometer model reports the listening time and
equivalent wire symbols for 90% detection and 1% false alarm per known window,
assuming equal received C/N0, a known band and known stationary noise power.
Simulation supplies its link C/N0; otherwise the TX target is explicitly an
assumption. Inspection also shows the assumed bandwidth, noise rise and
whole-burst exposure, with settling/filter/suppression as an equal-power
approximation. See [the model](lpi-estimates.md) for equations and limits.

The model uses sample-quantized geometry, accounts for shaped bandwidth, and
does not multiply the gain for the independent DSSS layer. Eligibility follows
transfer's existing automatic private scrambling for every keyed non-tone
transmission, including manual CLI profiles. Strong signals and numeric limits
have no numerical protection interval. Sub-symbol detection is explicit;
neither the count nor the exposure ratio is a safe traffic quota.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). The 11 focused GCC
checks passed in 27.86 seconds: `lpi_estimate`, `cli`, `gui_layout`,
`gui_contract`, `gui_link_boundary`, `gui_inspection`, `gui_inspection_page`,
`gui_application`, `gui_self_check`, `gui_adapter_boundary` and
`gui_boundary_regression`. The remaining 17 non-controller development-contract
checks passed in 184.32 seconds. The controller suite passed separately in
67.78 seconds, covering key loading, actual versus assumed C/N0, stale-estimate
withdrawal, short/long target selection, and existing pending reception behavior.
Together these cover all 21 required development-contract suites and eight
additional suites. Final wording was rebuilt and its two affected inspection
and application suites passed again. After the final numerical range guard,
the estimator, controller and CLI suites passed together in 97.05 seconds.

The eight selected Rev checks passed in 35.41 seconds: `lpi_estimate`,
`gui_layout`, `gui_contract`, `gui_application`, `gui_inspection`,
`gui_inspection_page`, `gui_link_boundary` and `gui_self_check`. Final wording
was rebuilt with the affected two suites passing again; the final numerical
range guard was rebuilt with `lpi_estimate` passing again. Tests include fixed
numerical references, signal-strength and symbol-duration scaling, no duplicate
DSSS gain, private manual CLI profiles, fractional-symbol estimates, numerical
limits, exact three-bit short/raw equivalence, and unchanged 1,216-bit interval
geometry. `git diff --check` passed.

No transport, waveform, receiver-admission, physical-end or pending-bit logic
changed. Native windows were not rendered: this environment has no display or
Xvfb. Shared layout tests and both backend builds do not establish native pixel
conformance. No physical-link or adversarial-detector measurement was performed;
the new probabilities remain an idealized model rather than calibration data.

## 1.2 kHz feasibility and tracking cost — 17 September 2026

The fixed-laptop compute estimate now includes serial continuation of one
desired stream per matching FFT profile, through nominal complete-symbol
absence. That component remains in both CPU and hypothetical GPU totals and
is exposed as `tracking_seconds` / `tracking_symbol_windows` in `analyze-link`.
No hardware benchmarking, receiver algorithm, confidence probability, wire
format or pending/completion behavior changed. Added regressions cover exact
one-/three-/ten-bit workload scaling, whole-symbol absence, empty drafts,
matching profiles, unrelated keys and correlator accounting.

The 1.2 kHz / -10 dB-Hz target / +3 dBm/-170 dB / crystal case reproduces
1,895.899 seconds of TX waveform and 2,527.892 seconds of simulated media.
Its corrected i9 estimate is 1,461.826 seconds, including 331.056 seconds of
serial tracking; the hypothetical GPU total is 448.252 seconds. A seed-1
sampled probe with an explicit matching RX target and 1 GiB DSP workspace
was stopped at its 240-second verification limit without output. Reception
remains unverified; the limit is not a failure result or calibration datum.

Bounded 100,000-trial reference experiments examined day-long -200 dB and
-230 dB cases. The 2 dB noise-figure / GPSDO-TCXO / -200 dB candidate has
99,936 correct reference detections, conditional on acquired timing/clock,
zero residual frequency and prescribed orthogonal templates. Its current
receiver workspace remains unsupported and requested-work CPU estimate is
about 41.3 hours. The [case study](1200hz-weak-link-planning.md) records the
commands, oscillator/energy limitations and distinction from actual reception.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). The 24 selected GCC
checks covered the full development-contract filter plus `correlation_experiment`,
`gui_contract` and `gui_layout`; 23 passed in the concurrent 672.30-second run,
and `gui_controller` passed an isolated rerun in 77.46 seconds. The seven
selected Clang checks covered the estimator, statistical experiment, CLI and
shared GUI application/controller/contract/layout; six passed in the initial
83.98-second run, and the final controller rerun passed in 90.54 seconds.

The convenience-message GUI test initially exceeded its fixed 15-second wait,
including in an isolated Clang run. A temporary instrumented Clang test observed
the exact message and finished transmission after 15.9868 seconds; all original
byte, metadata and composer-state assertions passed. Its test-only deadline
was therefore increased to a bounded 60 seconds, retaining 10 ms polling and
every assertion. The Clang rerun above uses that correction. No application
timeout or completion behavior changed. Native display workflows, physical
audio and Windows were not rerun. `git diff --check` and case-study numbers
and local documentation links passed their checks.

## Corrected transmit power and oscillator selection — 16 September 2026

The two mistaken -3 dBm presets were removed; the existing +3 dBm/-200 dB and
+3 dBm/-230 dB entries are the corrected choices. Their actual C/N0 values
are -33 and -63 dB-Hz under the unchanged noise model. Custom negative transmit
power remains supported by the analysis command. The planning examples and
statistical API defaults now use the corrected +3 dBm case. Seven 10,000-trial
reference cases were rerun at that power; current results are recorded in
[fast weak-link planning](weak-link-planning.md).

The shared GUI now offers free-running crystal, hobbyist GPSDO/XO without an
oven, GPSDO/TCXO without an oven, and GPSDO/OCXO models. Numeric residual clock
and phase values are visible. Both GUI backends use the same declarations and
configuration path, with one extra 48-pixel row preserving existing content
allocations. The selector changes both sampled channel settings and the
estimate, follows the transmission busy lock, preserves draft wire geometry,
and invalidates estimates made with the previous oscillator. Other modem
edits retain the choice. The original 100 ppm / 0.5 degrees/sqrt(second)
crystal scenario remains the default.

The same `--oscillator` profiles apply to `simulate`, `listen` and
`analyze-link`. Explicit clock/phase flags override their respective values.
New CLI tests compare sampled WAV output byte-for-byte between GPSDO profiles
and equivalent explicit channel settings, and compare analytical outputs for
all four profiles. They also check corrected link arithmetic, unknown profile
rejection, override metadata and command scope. These checks validate the
configuration mapping, not a physical GPSDO's performance.

At +3 dBm/-200 dB and 125,892.541-second symbols, the 10,000-trial coherent
reference model gives 2.45% for crystal and hobbyist XO, 99.65% for TCXO, and
99.73% for OCXO. GPSDO profiles bring the modeled carrier into the existing
search, but its modeled FFT workspace remains unsupported for this case.
Production RX confidence therefore remains unavailable. The profiles are
illustrative relative link impairments, not device specifications or models
of GPS servo dynamics, warm-up, holdover, or oscillator aging. The
[oscillator documentation](oscillator-models.md) records the primary sources
and why long-term GPS accuracy does not imply short-term phase coherence.

Both Release GUI executables and CLIs rebuilt. All 66 headless tests passed
(280.15 seconds), retaining the independent short-wire vectors, sampled weak
reception, physical-end and pending-bit regressions. Rev's eight focused
configuration, layout, controller, application and CLI suites passed
(68.28 seconds). Native Rev self-check, adapter/platform conformance and
1x/2x coordinate checks passed (five tests, 71.91 seconds). FLTK self-check
and document conformance passed; adapter conformance passed in 40.36 seconds
after a test-only timing correction. Its expanded-bitmap caption test had
raced the separate 40 ms source and 100 ms presentation polls with a fixed
130 ms delay. It now waits at most two seconds for the same caption,
visibility and exact tone predicate. No runtime behavior or assertion was
weakened. Both actual native windows were also captured on private displays
and visually inspected; the oscillator row and existing controls fit.

```sh
ctest --test-dir build --output-on-failure -j 2 \
  -E '^(gui_self_check|gui_workflow|gui_adapter_conformance|gui_document_conformance)$'
ctest --test-dir build-rev --output-on-failure -j 2 \
  -R '^(tuning|simulation_estimate|correlation_experiment|gui_layout|gui_contract|gui_application|gui_controller|cli)$'
```

## Bounded extreme-link planning — 16 September 2026

`pump analyze-link` now runs three matched-correlation reference experiments
without PCM generation or production receiver execution. Its statistics have
constant storage and work proportional to the Monte Carlo trial count, even
for represented symbols lasting years. The output retains exact draft wire
size and airtime, current receiver coverage limits, and the fixed i9-13900H /
RTX 4090 Laptop GPU compute estimates. The GPU estimate remains hypothetical;
no hardware benchmark or new receive backend was added. The initially added
-3 dBm/-200 dB and -3 dBm/-230 dB presets were subsequently removed after the
user corrected transmit power to +3 dBm; the existing positive-power entries
already cover those links.

The statistical unit suite compares reduced draws against an independent
explicit complex-segment implementation for one and four segments, including
correlated alternatives. It checks moments, detection probabilities, exact
integer-shape Gamma noise tails, search penalties, joint phase/frequency
coherence by numerical quadrature, the long-duration Wiener energy asymptote,
full-duration segment coverage, deterministic seeds and invalid inputs.
The extreme-geometry case represents 1e12 segments over 1e18 seconds without
iterating over them. These are statistical-model checks, not physical modem
calibration.

New CLI tests preserve exact `0`, `001` and `a` wire lengths and existing
airtime estimates; separate channel power from the TX design target; preserve
explicit independent RX profiles; test both custom and preset links; reject
misplaced/invalid options; and analyze a billion-second symbol under a
10-second subprocess timeout. Analysis outputs contain no received bits,
decoded source or physical-completion events. The four new CLI tests also
pass with the Rev/Clang-built CLI (0.31 seconds combined).

Eleven bounded 10,000-trial scenario runs covered -3 dBm at -200, -210 and
-220 dB attenuation, durations from 501,187 seconds to 1e10 seconds, 3,600-
and 20,000-second coherent segments, 0.5 and 0.05 degrees/sqrt(second) phase
diffusion, and 1e6 or 1e8 prescribed search alternatives. Their computed
examples were recorded in the original planning document; that document now
contains rerun +3 dBm examples following the power correction.
They expose the strong loss from phase diffusion and the growing time cost
of segmented energy accumulation. They do not establish actual reception
at -200 dB, whole-message probabilities, real-world confidence intervals or
an interception bound. Sampled receiver behavior is unchanged by this work.

Both Release GUI executables and CLIs build successfully. Rev's focused
`correlation_experiment`, `tuning`, `simulation_estimate`, `gui_controller`
and `gui_application` suites pass (five tests, 62.43 seconds). Native adapter
code was unchanged; native display conformance was not repeated for this
planning-only addition.

The complete focused development-contract run plus the new statistical suite
passes: 22 tests in 222.87 seconds, including sampled `weak_signal`, whole-symbol
physical-end tests, short wire vectors, fixed intervals, pending GUI updates,
live profile arbitration and the full CLI suite. No existing assertions were
relaxed. Commands used after both Release builds:

```sh
ctest --test-dir build --output-on-failure -j 2 \
  -R '^(live_profiles|live_receptions|live|live_resources|compression_short|transfer|stream_codec|stream_receive|recovery|attachment|pattern_correlator|pattern_receiver|pattern_search|tuning|simulation_estimate|correlation_experiment|weak_signal|gui_application|gui_controller|gui_inspection|gui_binary_editor|cli)$'
ctest --test-dir build-rev --output-on-failure -j 2 \
  -R '^(correlation_experiment|tuning|simulation_estimate|gui_controller|gui_application)$'
python3 tests/test_cli.py build-rev/pump -k link_analysis
```

## Sub-Hz planning and weak-signal clock search — 16 September 2026

The application now accepts 0.01 Hz through 30 MHz. Sub-Hz tests preserve exact
`a=011` and raw-bit lengths, fixed message framing, bounded waveform generation
and whole-symbol absence. An independently sampled 0.01 Hz control at Fs=64 Hz,
carrier=16 Hz and zero clock/phase drift recovers `a` using 12,800-second symbols.
That control exercises long coordinates efficiently; it does not establish
unrestricted oscillator tolerance at 0.01 Hz.

For pattern symbols of at least 16 seconds, application receive paths request
a bounded carrier lattice at spacing 0.25/T, targeting ±200 ppm with at most
4097 distinct offsets. Each has nominal and carrier-coupled timing alternatives.
FFT templates and next-symbol predictions use the hypothesized clock rate;
the longest hypothesis must be observed before comparison. Projection bins
retain the expanded offsets. Additional trials increase evidence penalties.
Matching-time carrier alternatives compete before admission, preventing the
existing bit-mask harmonics from producing wrong-label alias receptions.
Weak candidates cannot migrate onto a confirmed stream and publish duplicate
suffixes. No waveform bits, short dictionary codes, FEC or framing changed.

Wide FFT banks can generate templates in bounded scratch instead of retaining
all transformed rows. Expanded live banks sharing RAM stream those rows so
early key/epoch caches cannot crowd out the remaining banks. Private expanded
searches also use FFT competition. If its core cannot fit, application callers
may retain the original local five-bin, nominal-clock correlator and live status
reports the narrower coverage. Explicit expanded searches reject insufficient
workspace by default; expanded per-lane correlator admission is never used.
The existing local correlator and its four-hour physical progress tests retain
their original path. The model uses the requested wide search geometry
and regeneration work while keeping the fixed i9-13900H/RTX 4090 Laptop reference
budgets. It withholds percentages outside modeled carrier or FFT-workspace
coverage. No hardware benchmark or GPU execution backend was introduced.

Actual sampled PCM tests use the +3 dBm/-170 dB preset (-3 dB-Hz actual C/N0),
the default 1,500 Hz carrier, 100 ppm crystal error, 0.5 degrees/sqrt(second)
phase diffusion and the exact three-bit message `a`. The receiver gets no
channel offset, channel seed, expected text or payload length:

| Rate and design target | Symbol duration | Sampled results |
| --- | ---: | --- |
| 1 Hz, target -6 | 256 seconds | Seeds 1, 2 and 3 each recover exact `011`/`a`, one stream and one physical completion |
| 100 Hz, target -6 | 327.68 seconds | Seeds 1 and 2 each recover exact `011`/`a`, one stream and one physical completion, with a 64 MiB DSP limit |
| 100 Hz, target -3 | 163.84 seconds | Seed 1 has insufficient acquisition margin in the full blind search and does not recover the complete message |
| 1 Hz, target 32, +3 dBm/-120 dB | 128 seconds | Original strong-link failure now recovers exact `a` once; former alias duplicates are rejected |

Matched-duration noise-only controls produce no reception at 1 Hz or 100 Hz.
The 100 Hz streamed search retained about 9.45 MB idle state within its 64 MiB
ceiling. The finite seed checks establish these cases, not calibrated 99.9%
population reliability, practical -200/-230 dB links or an interception bound.
The permanent `weak_signal` suite retains the three 1 Hz seeds, a separate
noise-only control and the 100 Hz seed-1 sampled path.

Cached and streamed FFT tests compare exact candidates, scores and every
progress poll across serial and parallel execution. Coupled-clock tests cover
±200 ppm and ±8000 ppm stress cases, nine exact bits, duplicate carrier/timing
alternatives, bounded memory, immediate pending drainage, EOF/partial-silence
rejection and fully observed physical completion. Both compact preference
settings retain FFT competition; unsupported direct-correlator requests reject.

Both Release GUI executables were rebuilt. Native FLTK self-check, adapter and
document conformance passed (three checks, 60.68 seconds). Native Rev self-check,
adapter/platform conformance and 1x/2x coordinates passed (five checks,
95.33 seconds), each on a private virtual display. Rev's focused tuning,
search-geometry, FFT-batch, estimator and controller checks passed
(five suites, 70.54 seconds). Reference-hardware benchmarking was not performed.

Follow-up memory regressions preserve the original local five-bin candidate
scores, every progress poll and physical completion when an application opts
into fallback. Explicit frequency/clock requests and missing clock windows
remain strict. Shared expanded banks retain their full search while releasing
template-cache space. The original live and live-resource assertions pass.
An added sampled CLI case also receives exact `a` at a valid shaped passband
edge; implicit local searches retain their feasible offsets, including a
center-only bank, while explicit invalid offsets are rejected.

After the final receiver changes, native self-checks passed again for FLTK
(16.29 seconds) and Rev (20.12 seconds). The revised per-bank memory estimate
also passed on Rev (13.70 seconds), including the half-budget ceiling used by
live reception. Both GUI executables were rebuilt with these changes.

The final full headless run passed all 65 suites (256.45 seconds), including
the development-contract coverage, all 23 shared GUI suites, the unchanged
live/resource checks and the new `weak_signal` suite (142.37 seconds). Command:
`ctest --test-dir build --output-on-failure -j2 -E '^gui_(self_check|workflow|adapter_conformance|document_conformance)$'`.
`git diff --check` also passed. These are software and sampled-channel checks;
physical audio, RF links and interception performance were not measured.

## TX design target versus simulation channel strength — 16 September 2026

The 100 Hz comparison was checked with public auto-pattern, text `a`, default
1,500 Hz carrier, 100 ppm clock error and 0.5 degrees/sqrt(second) phase noise.
The +3 dBm/-170 dB preset supplies -3 dB-Hz C/N0; target -61 selects about
79,432,823 seconds per bit and falls outside carrier-search coverage. The
+3 dBm/-120 dB preset supplies +47 dB-Hz C/N0; target 140 reaches the applicable
64-chip floor at 1.28 seconds per bit. Its sampled-channel regression receives
exact `011` and decoded `a` with physical completion. The numerical target is
a design input for duration, not a required minimum received C/N0. No reversed
units or change to simulated power was found.

Shared help now explains the two independent settings and the drift limitation.
New model/controller regressions cover both reported 100 Hz cases and verify
that changing the target cannot change channel SNR. The local Rev executable
still contained the estimator from before the carrier-search fix; both default
FLTK and Rev builds were refreshed and checked for the corrected explanation
and target help. Six focused suites passed in the default Release build
(57.01 seconds): estimator, tuning, controller, application and both adapter
boundary guards. Rev's estimator/tuning/controller suites passed (60.97 seconds),
as did all five native checks on a private software-GL display: self-check,
adapter/platform conformance and 1x/2x coordinates (67.82 seconds).
These sampled checks validate the specific cases, not a
calibrated population-wide 99.9% success rate. No full multi-year symbol or
reference-hardware benchmark was run.

## Carrier-search coverage in simulation confidence — 16 September 2026

The reported public auto-pattern case (1 Hz, 32 dB-Hz target, +3 dBm/-120 dB,
text `a`) reproduced the misleading >99.9% estimate. The exact wire bits remain
`011`. At the default 1,500 Hz carrier, 100 ppm clock error shifts the carrier
by 0.15 Hz; the 128-second symbol's five frequency hypotheses span only
±0.00390625 Hz. A sampled-channel reproduction retained a maximum score of
12.52 against an admission threshold of 39.67 and admitted no reception. With
zero clock error, the same source/channel strength produced `011` and `a`
after the fully sampled absence tail.

The estimator now marks numeric confidence unavailable outside its modeled
carrier-search span, including either sign of explicit frequency offset plus
carrier clock drift. The GUI shows **Carrier outside RX search** and retains
both reference compute estimates. This deliberately makes no zero-probability
claim: the actual normalized energy fits can sometimes admit signals outside
the bank, but the previous attenuation-only approximation cannot predict that
regime. Receiver search, sampled channel, wire format and completion rules are
unchanged.

The Release build and all 22 selected contract, estimator and shared GUI suites
passed (172.71 seconds). The estimator regression includes the actual sampled
failure and successful zero-drift control, a separate EOF-without-absence
negative control, both frequency-bank edges, sample-quantized durations, and
stronger-SNR exclusion. GUI checks verify the exact reported settings, preserved
CPU/GPU values and restoration of numeric confidence when coverage returns.
The explanation was visually inspected in native FLTK windows at default and
minimum size, including a stronger 1 Hz preset. `git diff --check` passed.
Rev native conformance was not rerun for this shared text/model change; no
adapter code or geometry changed. No reference-hardware benchmark was run.

## Simulation probability and reference compute estimates — 16 September 2026

The persistent Simulation row now shows a modeled whole-draft reception
probability, an Intel Core i9-13900H CPU computation estimate and a projected
RTX 4090 Laptop GPU computation estimate. A bounded analytical model consumes
the existing encoded draft estimate and independently configured receiver
profiles. It preserves exact short/raw bit lengths, applicable interval FEC,
sample-quantized symbol timing and fully scored absence tails. It runs no local
benchmark and does not inspect processor/GPU identity. Fixed throughput budgets
and probability assumptions are documented in [simulation estimates](simulation-estimates.md);
these checks do not calibrate the model or establish GPU execution support.

The shared controller invalidates displayed estimates on draft/settings changes,
rejects stale preparation results, and explicitly presents off, invalid and
unavailable states. FLTK and Rev use the same declarations and geometry. The
default/minimum desktop heights increased by 43 logical pixels to retain the
existing content space while making room beside the dropdown.

Release builds succeeded. All 23 selected suites passed in 174.69 seconds:
the complete focused development-contract set, the new `simulation_estimate`
suite, shared layout/contract/link-boundary tests and adapter-boundary guards.
The new checks cover SNR and length response, unprotected short/raw input,
fixed-interval FEC, unmatched profiles, independent bank cost, impairment losses,
four-hour absence accounting, equivalent dictionary/raw draft estimates and
stale/off/invalid GUI states. No transport regression assertions were relaxed.

FLTK self-check and native adapter/document conformance passed. Rev self-check,
native adapter/platform conformance and 1x/2x coordinates all passed (five tests,
74.49 seconds). Default
1180 by 952 and minimum 1030 by 872 windows were captured and visually inspected
in both FLTK and Rev; the three estimate labels were readable without overlap.
The initial sandboxed native attempt could not create Xvfb's local display
socket; successful native retries used private displays outside that sandbox.
No reference laptop benchmark, physical-link calibration, Windows execution or
GPU execution was performed.

## Narrow-band simulation scheduling — 16 September 2026

A reported 37% CPU utilization in a public 1 Hz simulation exposed a scheduling
limit in the numerical correlator. The live receiver uses the default single
clock-rate hypothesis, not the three-rate bank in the earlier standalone
benchmark. At a 6 kHz sample clock, a two-second chip and seven-second start
uncertainty produce 15 origins across five frequencies: 75 lanes. Fixed groups
of 16 gave the executor only five runnable jobs, regardless of its 11-worker
setting. Correlation now chooses smaller groups for small banks, retaining the
previous maximum grain for large banks. No hypothesis or arithmetic changes.

A bounded actual `live::Session` reproduction used 1 Hz, the default 1,500 Hz
carrier, a -30 dB-Hz TX target, public/no-key operation, RX targets `-30,55`, a
six-second epoch search setting, 2,600 MiB workspace, and the +3 dBm/-170 dB link
preset. The harness waited for at least 30 seconds of sampled media, then
measured ten seconds of processing and cancelled. The two versions linked the
same library except for the correlator batch object; the second pair reversed
their order. Both selected 11 workers from 12 available logical CPUs.

| Actual simulation processing | Fixed grain of 16 | Adaptive grain |
| --- | ---: | ---: |
| First pair, simulated seconds per wall second | 10.6367 | 14.8282 |
| Reversed pair, simulated seconds per wall second | 10.7666 | 14.7070 |
| Mean | 10.7017 | 14.7676 |
| Mean busy logical CPUs | 3.8780 | 6.6286 |
| Mean CPU utilization across 12 logical CPUs | 32.3% | 55.2% |

This is approximately 38.0% higher throughput, or 27.5% less time for the same
sampled-media workload. Neither run reported errors, and reported DSP workspace
was unchanged at 684,584,464 bytes. An additional instrumented run attributed
9.11 of 10.01 wall seconds to correlation batch execution and 0.51 seconds to
the sampled channel, including 0.34 seconds of transmitter rendering. FFT
scoring did not run in that measurement window. The numerical backend still
has synchronization and worker-idle time; this change does not establish full
CPU utilization or a general speedup for other configurations.

The -30 target has approximately 17.5-hour symbols. These bounded measurements
cover partial-symbol simulation throughput, not a completed decode or detection
probability. A new 75-lane regression compares exact one/eleven-worker and
seven-lane tiled results with active and future origins. The existing wire,
physical-end, progress and memory checks remain intact.

The Release build, including `build/datapump-gui`, and all 20 selected contract,
executor and numerical batch suites passed; the suites took 214.99 seconds.
The updated correlator batch suite also passed AddressSanitizer/UBSan and
ThreadSanitizer with the test, backend, executor, PatternCode and crypto objects
instrumented. Other archive/external dependencies were uninstrumented and
AddressSanitizer leak detection was disabled. `git diff --check` passed. Native
window rendering and a complete 17.5-hour-symbol decode were not exercised.

## Numerical search batches for GPU preparation — 16 September 2026

FFT acquisition and long-symbol fit accumulation now have typed numerical batch
interfaces with CPU reference backends. Logical work scales independently of CPU
worker scratch. FFT batches can cover the available search bank; long-symbol
batches cover up to 64 original blocks and 65,536 lanes, reduced as necessary by
workspace and the next possible symbol completion. Host admission, trial order,
physical completion and next-poll publication remain in their original order.
No GPU runtime or kernel is enabled; [search-compute](search-compute.md) records
the interfaces and remaining device work.

The full Release build passed, including the native FLTK executable. All 20
selected headless suites passed in 187.71 seconds: the 17 suites listed in the
[development contract](development.md), plus `search_parallel`,
`pattern_fft_batch` and `pattern_correlator_batch`. After rebuilding, all four
final focused suites (`pattern_receiver`, both batch suites and
`search_parallel`) passed in 57.31 seconds, covering the optional FFT capacity
guard and completed batch fixtures. Independent wire vectors and existing memory assertions were not
changed. No native adapter or message presentation code changed.

New coverage includes 100,003 executor jobs and overflow-safe sparse ranges
spanning `SIZE_MAX`, 16,387 FFT jobs with varied tiling and reversed execution
order, and 10,019 correlator lanes with exact one/multiple-worker results.
Additional cases exercise phase alternatives, private templates, pulse shaping,
tones, malformed extents, cancellation and workspace reuse. Twelve receiver
cases compare every progress poll around symbol and physical-absence boundaries
with roomy and tight workspace. An independent executable built from the prior
committed correlator (`a8978b3`) matched all 336 per-poll digests from the updated
implementation byte-for-byte, including scores, coordinates, bit events and
constellation output, with cropped negative and future origins.

Focused AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer runs
passed for the executor, both numerical backends, FFT exact-progress and
physical-absence cases, and the new correlator coordinator boundary cases.
Relevant test, backend, coordinator, executor, PatternCode and crypto source
objects were instrumented; other archive dependencies and external libraries
were not. AddressSanitizer leak detection was disabled in this environment.
These checks establish sampled CPU behavior and shared GUI policy, not native
window rendering, physical weak-signal detection or device numerical accuracy.

Paired Release timings used the new `benchmark_correlator` tool on the Ryzen 5
PRO 5650U host (6 physical cores, 12 logical CPUs). Each run used the same 11,265
hypotheses, approximately 631-second symbols and 2,048 deterministic noise
samples: `1200 -10 11 2048 .25 1`. The baseline replaced the search/receiver
objects with the committed versions and linked the same remaining library and
benchmark objects. The run order was reversed for the second pair.

| Partial-symbol CPU workload | Committed, 11 workers | Batched, 11 workers |
| --- | ---: | ---: |
| First pair | 2.6663 s | 2.5038 s |
| Reversed pair | 2.4219 s | 2.3056 s |
| Mean | 2.5441 s | 2.4047 s |

The updated path used about 10.6 logical CPUs on average and retained the same
5,425,312 idle bytes. Two updated one-worker runs took 9.7087 and 8.3535 seconds
(9.0311-second mean), about 3.76 times the parallel mean for this workload. The
batch refactor itself reduced the paired mean by about 5.5%; it is preparation
for wider device execution, not a claim of GPU-like CPU scaling. FFT timing
smoke checks had substantial host variance and support no reliable additional
speedup claim. These partial-symbol generated-noise timings do not measure
successful reception or detection probability. No GPU was available for tests.

## Exhaustive post-reception hard-bit recovery — 16 September 2026

Unresolved interval receptions now retain a separate bounded hard-bit capture
and, only after physical completion, may search missing-bit assignments and
fixed-size alignment hypotheses. The default run lasts at most five minutes
plus its final bounded worker batches, using up to the available CPU cores.
Incomplete and cancelled searches retain their exact search position for an
explicit resume. Candidates remain private until the competing alignments are
resolved; keyed candidates still require the original HMAC verifier. Source
interpretation then follows the existing bounded path. No encoder, RS primitive,
cryptographic primitive, waveform or symbol-admission algorithm changes.

The extra capture stores four hard decisions per byte, including explicit
missing values, and releases storage if its configured retention is exceeded.
This preserves the existing receiver working-memory assertion without relaxing
its limit. The 4,096-bit diagnostic prefix and next-poll pending progress remain
independent. Live recovery has a separate bounded queue and coordinator; later
receptions continue while it searches. The shared Console and Compression menus
provide resume/cancel actions, with physical completion and recovery status
shown separately in the original row. CLI receive commands expose the local
budget, workers, retained slots and error reserve, plus recovery JSON counters.

New regression fixtures cover sparse missing bits beyond RS byte-erasure
capacity, additional known byte errors, exact public and authenticated source
reconstruction, and deterministic one/multiple-worker coverage. One keyed case
exhausts all 8,192 assignments for 57 missing bits in separate bytes and preserves
all 57 as missing in diagnostics. Further cases cover deadlines and cancellation
with resumption, wrong authentication coordinates, competing public alignments,
missing middle intervals, memory and integer limits, and uneven receive chunks
crossing the packed retention boundaries. Live and shared GUI regressions cover
same-row updates, stale result rejection, independent later reception, and
clearing both staged and already-completed recovery publications.

The Release build and all 22 selected suites passed in 182.53 seconds:
`live_profiles`, `live_receptions`, `live`, `live_resources`, `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `recovery`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `cli`, `gui_layout`, `gui_contract`,
`gui_link_boundary`, `gui_adapter_boundary` and `gui_native_policy`.
`git diff --check` passed. The independent wire vectors, sampled hours-long
physical-end cases and original memory assertions remain intact. The Compression
geometry check now uses the same grouped menu controls as the native adapters,
while checking that every menu entry agrees on its shared rectangle.
These are synthetic hard-bit, sampled-audio and headless shared GUI checks;
physical weak-signal/LPI performance and native window rendering were not
exercised. No display server was available, and neither native adapter changed.

## RX target combinations and revised interpretations — 15 September 2026

The receive-bank fix now covers differing automatic chip floors, integration
durations, keyed patterns, target ordering and late long-symbol observations.
This supersedes the completed-prefix limitation in the earlier entry below.
Stronger accepted observations revise the original reception identity, including
completed-to-pending transitions, and retire any overlapping fragment identities.
Obsolete cached content is removed before replacement publication. Shared GUI
consumers withdraw copy/save eligibility and reject stale revisions and retired
IDs. Local content identity remains stable across revisions and never enters the
wire. Neither native GUI adapter changes.
Live CLI JSON also exposes the revision and retired identities on raw-bit and
completed-content events, with `reception_update` events for decoded rows.
Plaintext output and explicitly saved files remain append-only/local outputs;
they cannot retroactively retract bytes already consumed outside the receiver.

Native log-evidence values are not comparable across exact-sample, compact-bin
and long correlator paths. Clean sampled probes found a short false prefix with
score 893 competing with a correct full stream scoring 293. Arbitration now caps
each accepted symbol's evidence at its known media duration, accumulates that
support independently of output chunking, and excludes missing positions. Capping
each symbol also prevents excess confidence in an initial exact fit from lending
support to a later partial fit. The native diagnostic scores, admission gates,
physical-absence rule, source interpretation timing and transmitted bits stay
unchanged. This is a bounded selection heuristic, not a false-alarm guarantee.

Pending disjoint hypotheses must follow the longer symbol clock within the
earlier observation's absence window. Completed history still requires actual
sample overlap. Weak observations cannot enlarge the winning span or join two
stronger independent receptions. History, retired aliases, replay storage and
GUI rows remain bounded; the history and event storage count toward DSP quotas.

The expanded sampled test matrix exercises every ordering of `55,62,72` and
`20,26,32`, all three transmit profiles, one-bit and three-bit inputs, noisy mixed
profiles, keyed reception with an unrelated receive key, tone-mode one-bit input,
and reversed pairs/triples containing 32-second symbols. Existing `55,32`
short-text, exact raw-bit, fixed-interval and consecutive-transmission fixtures
remain. The helper and GUI regressions additionally cover multi-fragment merges,
stale events, obsolete attachments, copy/save revocation, independent frequencies
and keys, independent later transmissions, alias limits and hours-long progress.
There are 76 live scenarios containing 81 physical transmissions, plus two
generated waveforms for the tone identity check. The additional separation case
starts a short transmission after seven seconds of silence while the previous
32-second profile still awaits its full absent symbol; both receptions retain
their own exact bits and identity. Native support regressions vary PCM and
decision chunk sizes and confirm that gaps and terminal events add no support.
GUI cache ownership survives the shorter row history, so revisions can also
withdraw obsolete attachments after their displayed rows have been evicted.
The initial metadata implementation exceeded one existing long-search workspace
fixture. Reusing already stored committed scores and endpoints removed the
redundant state; the original 1/16/32 MiB checks pass with their full search
coverage and budgets unchanged.

Tone mode has a wire ambiguity: two consecutive zero tones at target `55` and
one zero tone at `32` produce identical sampled PCM. A regression preserves that
independent observation; no target-selection rule can recover the originating bit
count from identical waveforms. A separate exploratory `001` tone probe also
found the existing single-profile receiver can abandon its first timing track
for a later independently timed start without completing the earlier track.
The multi-profile regression uses a single tone bit; this native tone tracking
limitation is not fixed by the arbitration change.

The Release build passed. All 16 focused suites passed in 190.22 seconds:
`live_profiles`, `live_receptions`, `live`, `live_resources`, `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `gui_application`, `gui_controller`, `gui_inspection`,
`gui_binary_editor` and `cli`. The separate native GUI policy test also passed.
The full rebuild reported the existing `std::filesystem::u8path` deprecation in
unchanged `src/gui/application.cpp`. `git diff --check` passed. These are headless
sampled-audio and shared GUI checks; physical audio links and native window
rendering were not exercised.

## Competing RX target profiles — 15 September 2026

RX targets `55,32` and `32,55` now arbitrate overlapping pattern observations
in the live receiver bank. Public patterns of different lengths share chip
prefixes, so a mismatched duration can admit real correlation evidence and
previously publish its own bogus message. A bounded history now assigns one
pending identity to competing observations of the same signal and selects the
strongest cumulative pattern evidence. All receivers consume each PCM block
before any completion is published; a weaker profile cannot complete the
selected pending row or publish extra content. Hardware capture and simulation
use this same policy.

Receiver creation and recovery retain their origin on the bank's sample clock.
The 64-entry arbitration history and drained event storage count toward the DSP
budget. Completed history suppresses late weaker copies without extending its
sample span into later independent transmissions. Wire bits, dictionary codes,
interval geometry, admission thresholds and fully scored physical-end rules are
unchanged. Source validity is not a selection criterion.

`live_profiles` feeds deterministic PCM through the real asynchronous capture
queue with stubbed audio I/O. Both target orders and both transmit profiles
cover short `e`, explicit `001`, `hello`, fixed-interval text and consecutive
transmissions switching profiles. It checks one signal ID, exact pending bits,
one completed result, independent short-message vectors, and no completion at
five seconds of silence. The test fails against the original library with
multiple live signal IDs. `live_receptions` covers evidence replacement,
losing completion, immediate per-observation progress, distinct frequencies and
keys, large sample coordinates, bounded history and later independent messages.

There remains an ambiguity when a much longer symbol first becomes admissible
after a short prefix has already completed. A clean sampled probe with targets
`55,5` demonstrated a short-prefix completion at about six seconds and stronger
first-symbol evidence at about twenty seconds. Stronger late evidence receives
a fresh pending identity instead of being discarded or reopening a completed
row; the earlier interpretation can therefore remain visible. The helper
regression preserves those newly accepted long-symbol bits. No additional wire
framing or wait for every configured long profile was introduced.

The Release build passed without compiler warnings. All 15 focused suites passed
in 123.98 seconds: `live_profiles`, `live_receptions`, `live`, `live_resources`,
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection` and `gui_binary_editor`. This includes the existing sampled
four-hour symbols, exact short endpoints and pending GUI prefixes. These are
headless sampled-audio and shared GUI checks, not physical-link measurements or
native-window rendering checks; neither GUI adapter was changed.

## Pattern steps beside evidence — 15 September 2026

The Console's scrollable **Pattern steps** text list now sits at the right edge
beside **Pattern evidence**, with the same top and height as the plot row.
Received-message history starts at the left margin again. Plot widths and the
reference's 280-pixel width are preserved; showing or hiding the generation
scope resizes the reference with the plots. This changes shared layout and
declaration order only.

The FLTK Release build passed. All 31 focused shared GUI and compatibility
tests passed in 92.06 seconds, including layout at supported window sizes,
hidden-scope reflow, independent short-message vectors, fixed intervals,
physical completion, four-hour sampled symbols and pending prefixes.
FLTK windows were captured at 1180 by 866 and 1030 by 786 with the generation
scope hidden and visible. The reference aligns with the plots, and wheel
scrolling reaches the final longer-integration row at minimum height.
FLTK document conformance passed; adapter conformance reproduced the existing
minimum-size Compression dictionary-label clipping failure. Its assertion was
retained.

The ordinary Rev Release build hit an existing Clang 19 error in
`src/live_pattern_scores.hpp`: `key()` has a deduced return type and is used
before its definition. For native layout validation only, a temporary Clang
virtual-file overlay supplied the equivalent explicit return type; repository
source was unchanged. That build passed, as did Rev adapter, platform and
1x/2x coordinate conformance (49.75 seconds total). Rev's default and minimum
windows with the scope hidden, plus its minimum window with the scope visible,
also show the reference aligned at the right, with lower rows reachable by
scrolling. This is not a clean unmodified Rev build result. Native transmission
workflows and physical audio links were not rerun for this layout change.
`git diff --check` passed.

## Default target C/N0 — 15 September 2026

CLI and GUI target C/N0 now default to 32 dB-Hz (32 dB/1Hz). Initial receive
targets and invalid-list resets use the same value, and the GUI preset list
includes 32. Help and current documentation reflect the new default, including
the GUI's approximately 1.89 kbit/s Shannon-Hartley limit at 3.6 kHz.

The Release build passed. All 14 focused tests passed in 78.99 seconds:
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_profile_reference`, `tuning` and `cli`.
The default CLI estimate also matched an explicit `--target-snr 32`.
`git diff --check` passed. Native window rendering and physical audio links were
not exercised for this shared default-setting change.

## Automatic profile reference — 15 September 2026

The Console now has a compact **Pattern steps** list on the left of reception
history and scope plots. Rows show the C/N0 changeover, complex chips per raw
bit, symbol duration and gross bitrate; the active profile is bold. Boundaries
come from the existing tuner with the selected rate, carrier, clock and key
geometry. Forced modes show their fixed profile, and extended integration shows
the active duration. Thresholds are rounded for display and are not measured
receive-confidence limits. The transmit-generation scope retains its full width.

Both FLTK and Rev Release builds passed. The 13 focused compatibility and GUI
tests passed in 70.86 seconds, including independent dictionary vectors, fixed
intervals, physical completion, sampled four-hour symbols and pending prefixes.
After the final presentation and forced-target fix, all 24 shared GUI checks
passed in 65.38 seconds. New coverage pins the default 29.480625, 26.470325 and
23.460025 dB-Hz transitions, actual clock/carrier/key floors, forced targets below
the receive-list range, extended integration, active-row updates and minimum-size
sidebar geometry. No transport or automatic selection rules changed.

Native windows were captured and inspected at 1180 by 866 and 1030 by 786. FLTK
was checked with the generation scope hidden and visible; Rev was inspected
with it hidden, with both layouts covered by shared geometry tests. Compact row
text fits beside the native scrollbar; plot headings and waveform action buttons
remain visible. Rev adapter
conformance and FLTK document conformance passed. FLTK adapter conformance reached
the previously documented minimum-size Compression dictionary-label clipping
failure; the unrelated label and its assertion were retained. Full native
transmission workflows and physical audio links were not rerun for this display
change. `git diff --check` passed.

## Continuous tuning transmission — 15 September 2026

Both shared compose pages now provide **Transmit noise** and **Stop noise**.
Noise uses the regular encrypted pattern transmitter with fresh temporary
Data/Scrambler/DSSS key material and a bounded source of Data-masked dummy bits.
The ordinary pattern alternatives, epoch/ordinal schedule, settling, shaping,
carrier and signal level remain in use. No user content, keyfile entry or
receive-bank key is created. Normal message framing, exact short/raw endpoints,
physical completion and pending presentation remain unchanged.

FLTK and Rev Release builds passed. `streaming_modem` and `noise_receive`
passed in both builds. Independent lazy-source/ordinary-source comparisons
check sampled waveform and chip equality through partial-chip symbols,
multiple epochs, shaping on/off and both surrounding noise sections. Other
new checks cover fresh starts, non-looping symbol/cache blocks, preview/read
continuity, ordinary power and sidelobes, bounded storage, four-hour symbols,
coordinate exhaustion and cancellation. The receiver comparison admits zero
bits for tuning noise, unknown-key encrypted transmission and Gaussian
background across 16/128/512-chip public/private/tone receiver scenarios;
actual-message positive controls must decode exactly. These are finite sampled
regressions, not a lifetime false-detection or thermal-indistinguishability proof.

All 15 focused compatibility/live/GUI suites passed in 119.65 seconds, including
the independent dictionary vectors, fixed intervals, physical absence,
four-hour sampled symbols and exact pending prefixes. All 23 shared GUI tests
passed in 58.63 seconds after the final presentation change. Noise-specific GUI
coverage includes empty/invalid drafts, attachments, loaded/failed keys,
repeated start/stop, unchanged draft/inspection/history/settings, immediate
busy state, elapsed status, and automatic/manual capture presentation.

FLTK native document conformance passed. Its adapter conformance reached the
already documented minimum-size Compression dictionary-label clipping failure;
that assertion was retained. The updated FLTK native workflow passed in 200.16
seconds, including keyed noise start/stop before its existing message and
attachment tests. Rev adapter, platform, 1x and 2x coordinate conformance passed.
Its concurrent workflow missed a late replay frame (maximum observed fraction
0.847458 rather than the required 0.9); the isolated rerun passed in 260.88
seconds. No assertion was changed. Both private virtual displays were stopped
after validation.
Hardware audio/RF operation and Windows runtime behavior were not exercised.
No radio was keyed by these tests. `git diff --check` passed.

## Transmission scope space reclamation — 15 September 2026

Hidden scopes now collapse their entire 206-pixel area. The signal and file
browsers move upward and gain 108 pixels, fitting two more full signal rows;
all four plots gain the remaining 98 pixels. The display choice stays available.
Showing Hex/Bits or starting transmission in auto-hide restores the scope;
completion and None return the space to reception and plots on the same
presentation update. Both native adapters consume the shared geometry.

Both FLTK and Rev Release builds passed. All 23 shared GUI checks passed in
58.29 seconds, including minimum/default/larger window bounds, exact space
reclamation, dropdown transitions, retained control geometry, reception progress
and adapter boundaries. Rev adapter conformance passed in 37.07 seconds after
its expected-geometry helper was routed through the current application layout;
the platform and both coordinate-scale checks also passed. The same helper
correction preserves every FLTK geometry assertion. An isolated FLTK rerun
passed the layout and QR focus checks, then reached the previously recorded
Compression-page dictionary clipping failure at minimum size. Its earlier
concurrent run had missed the QR focus assertion. Neither assertion was relaxed.

Native FLTK and Rev windows were inspected at default and minimum sizes, with
Hex, None and auto-hide transitions. The expanded browsers and plots reclaim
the full area while preserving the dropdown. Actual simulated replay showed
the scope in both adapters, then completion restored the expanded layout with
the same received `e` row. Both GUI processes and the private display closed
cleanly. `git diff --check` passed.

## Transmission scope auto-hide — 15 September 2026

The Console scope dropdown now offers **None**, **Hex, auto-hide**, **Hex** and
**Bits**. Hex, auto-hide is the default: the scope and caption appear during actual
transmission snapshots and simulation replay, then hide when activity ends.
None hides them throughout transmission. Manual Hex and Bits retain access to
the capture while idle. The dropdown remains available and is wide enough for
the new label; transport, capture data and surrounding control positions are
unchanged.

Both FLTK and Rev Release builds passed. All 23 shared GUI checks passed in
57.73 seconds, covering the default and manual choices, visibility during
generation/replay and after completion, retained captures, exact pending bits
and shared layout/binding boundaries. `git diff --check` passed. Native display
workflows were not rerun for this shared visibility change.

## Transmission generation scope — 15 September 2026

The first Console tab now presents the requested nine diagnostic stages plus
the actual framed Wire Plaintext input. Input, Data keystream and ciphertext
are adjacent; the pattern input, DSSS keystream and mapper result are adjacent.
Hex fits all 32 byte columns at minimum width. Bits adds aligned hexadecimal
and exact binary digits with horizontal scrolling. Partial bytes remain exact,
including the three-bit `001` dictionary encoding of `e`.

The capture comes from the actual source encoder, Data XOR and waveform
generator, independently of draft estimates. Data rows publish begun payload
symbols. Pattern rows sample eight actual mapper bytes at the first chip of
each of the first four symbols, exposing public repetition and changing private
streams. Preview reconstruction and filter lookahead cannot publish future
symbol starts. Each simulation replay frame owns its bounded capture; completion
retains the final capture, cancellation freezes it, and a new transmission clears
it. Transmitter admission and replay accounting include the diagnostic storage.

Private Pattern replaces the public template, so the Pattern XOR row is null;
the replacement bytes remain visible in Pattern Bitstream. Mapper inputs are
labeled separately from subsequent payload-dependent I/Q selection, pulse
shaping and carrier modulation. FHSS is not applied and is shown as unused.
All interval markers, MAC and FEC bits remain inside Data encryption. No wire
fields, padding, short-dictionary codes, receiver completion or pending-message
rules changed.

Validation:

- Release builds passed for FLTK (`build`) and Rev (`build-rev`).
- All 13 focused transport/live suites passed in 70.29 seconds, including
  independent dictionary vectors, exact short/raw endpoints, fixed framing,
  physical absence, sampled four-hour symbols and bounded live resources.
- After refining pattern sampling, `transfer`, `pattern_code` and `live`
  passed again, 3/3 in 13.74 seconds. New regressions independently check every
  encrypted marker/interval bit, actual Data and DSSS XOR operands, public
  repetition, private symbol addresses, exact traced/untraced PCM equality,
  preview nonpublication, retention bounds and workspace admission.
- The final shared GUI suite passed, 23/23 in 57.49 seconds. It includes
  first-tab declarations, Hex/Bits layout, actual per-poll transmission data,
  raw/escaped input, retained captures, pending reception and adapter boundaries.
- FLTK native workflow and document conformance passed. Adapter conformance
  reproduces the previously recorded Compression-page dictionary clipping at
  minimum size; that reference and its regression are unchanged.
- Rev native adapter, platform and 1x/2x coordinate conformance passed. Its
  concurrent workflow run missed the final replay-fraction threshold
  (`0.898306` against `0.9`); the unchanged workflow passed when run separately
  in 249.58 seconds.
- Final native FLTK screenshots were inspected at minimum and default sizes:
  all ten stages, all 32 Hex columns, exact `001`, three repeated public
  symbol-start groups, unused fourth-group placeholders and the full Dark
  choice are visible. Bits retains the same rows with horizontal scrolling.
- The final Rev native screenshot was inspected at default size with the same
  captured source, exact partial bits and repeated public pattern groups.
- `git diff --check` passed.

These checks exercise generated samples and native presentation. They do not
establish sound-card emission, physical-link operation or intercept probability.

## Mono transmit channel routing — 15 September 2026

The shared console now has a persistent **Mono** checkbox below **Audio device**,
enabled by default. ALSA and WinMM try stereo output, with mono fallback at each
existing sample-rate candidate. Enabled stereo output contains silent left PCM
and unchanged right PCM; disabling Mono duplicates the signal to both channels.
Mono-only endpoints use their sole channel in either mode. CLI hardware playback
uses the same default, with `--no-mono` to enable both stereo channels.

Changing the checkbox updates the next playback without restarting the receiver,
discarding pending reception, clearing plots or recalculating the draft. Channel
interleaving happens after resampling; modem bits, framing, source codecs,
physical-end detection, receive PCM and exported mono WAVs are unchanged.

New ALSA/WinMM stub regressions cover stereo preference, stereo-only and mono-only
devices, exact signed/clipped PCM on each channel, streaming frame counts,
partial ALSA writes, rate conversion and Windows double buffering. Existing
capture and error/cancellation tests remain. Shared GUI checks cover the default,
toggle propagation, busy/closing guards, all-page visibility and minimum-width
layout. Live/GUI regressions protect the receive clock, plots, prepared estimate
and ongoing hours-long symbol processing while output routing changes.

Validation:

- Release builds passed for FLTK (`build`) and Rev (`build-rev`).
- The full headless suite passed, 55/55 tests in 137.67 seconds, including the
  independent short-message/fixed-interval/physical-end/pending-progress tests,
  CLI integration and both audio backend contracts.
- After refining Mono updates to preserve reception, rebuilt both backends and
  reran `live`, `live_resources`, `gui_controller`, `gui_application` and
  `gui_layout`: 5/5 passed in 57.12 seconds.
- Rev self-check and native adapter/platform/coordinate conformance passed,
  5/5 tests on a private Xvfb display; the final rebuilt adapter conformance
  passed again. The FLTK default window was also captured
  and inspected: the checked Mono control fits below Audio device alongside
  the diagnostics.
- FLTK native document conformance passed. Adapter conformance reports that the
  existing Compression dictionary reference clips at minimum window size. Its
  text, Courier 12 font and 489-by-166-pixel frame match the pre-change source;
  the Mono layout does not alter that reference.
- `git diff --check` passed.

Windows audio was tested through the compiled WinMM stub on Linux. Physical
sound-card routing, including an IC-7100, was not tested.

## Message preservation contract and regressions — 15 September 2026

This change updates documentation and tests only. Runtime sources, protocol
constants, GUI implementation and build configuration are unchanged.
[Development requirements](development.md), also linked from root `AGENTS.md`,
now explicitly protect 1–16-byte dictionary text, exact binary messages, fixed
intervals, physical-absence completion and per-bit pending progress when each
bit may take hours. Current summaries now distinguish short dictionary content
from interval sources, describe all four GUI tabs and use the fixed format's
18.75% marker overhead relative to coded bits.

New regressions cover:

- `stream_receive`: malformed validity cells and LZMA2 chunk lengths remain
  uninterpreted through successive fixed intervals until physical completion,
  under a small local content quota. Underfilled intervals permit continuation
  and preserve leading/trailing zero bytes. Both checks cover all FEC profiles.
- `gui_application`: every pending prefix through dictionary, byte and interval
  boundaries updates one stable row without an expected length, premature
  decoding or completed-message actions. Missing-slot placeholders retain their
  visible notice.
- `gui_controller`: actual simulation progress snapshots match the displayed
  pending row on the same poll, across raw, dictionary and interval messages;
  copy/paste actions remain disabled while pending.

Existing regressions also ran for independent historical dictionary vectors,
the inclusive 16/17-byte split across key/FEC/compression settings, and generated
PCM containing three four-hour symbols. The latter verifies individual bit
drains and requires a complete absent four-hour symbol to finish reception.

Validation:

- `cmake --build build --parallel 2`: Release build passed, including FLTK.
- `ctest --test-dir build --output-on-failure -LE native_gui -j 2`: initially
  54/55 passed in 136.02 seconds, including all 23 headless GUI checks. The sole
  failure reproduced in the unchanged `attachment` test: its interval-quota
  fixture still used 16-byte text, which now correctly takes the short path.
- That fixture now uses 17-byte text and explicitly checks interval selection;
  its original quota rejection and nonpublication assertions are unchanged.
  Rebuilt `test_attachment`; focused CTest passed (1/1). All 55 headless tests
  therefore passed across the full run and this corrected-fixture rerun.
- `git diff --check` and links in the new contributor documents passed.

No native-window conformance or workflow run was performed: this environment has
no Xvfb/display server. Shared GUI tests and the native executable's headless
self-check passed; they do not establish native rendering or a physical link.

## Short-message GUI and inclusive 16-byte limit — 15 September 2026

The short dictionary now includes 16 source bytes. Shared transfer constants
set the transmit/receive/GUI limit to 16 bytes and at most 208 exact bits; text
starts using fixed coding intervals at 17 bytes. `quick brown` sends 70 bits;
`quick brown fox `, including its trailing space, sends 98. Explicit `010` sends
three bits and is interpreted as `t` after physical completion, with its exact
bits retained. Two-bit inputs remain raw without inferred characters or padding.

The Compression page now mirrors full short-text dictionary bits and accepts
up to 208 exact bits with an expected-text preview, replacing its four-bit entry
limit. Console byte-prefix editing retains its separate 128-bit limit.

The code-path audit found no separate current GUI encoder: FLTK and Rev share
the same controller, inspection model and transfer library. A stale
`build-native/datapump-gui` predated dictionary restoration despite showing the
same version number. The user reported normally launching `build/datapump-gui`,
could no longer reproduce the oversized layout, and suspected an older running
instance. Neither a stale process nor the stale alternative executable was
confirmed as the original cause. All three current GUI build targets were
rebuilt; a running instance must be closed and relaunched to load the new code.

Regression coverage now types `quick brown` character by character through the
shared application's production defaults (3.6 kHz rate, 1500 Hz carrier,
80 dB-Hz transmit/receive targets). It checks the actual Transmission document,
including replacing an older long draft while its estimate is running, clearing
pending details, page changes, raw `010` edits and returning to the same visible
text. Explicit Repeatable text remains visible and counts toward source size;
turning it off restores the exact short draft. End-to-end GUI simulations cover
70-bit text, two-bit raw reception, `010` interpreted as `t`, and copied/reused
dictionary bits. The full 208-bit input limit and rejected over-limit/invalid
edits are checked independently from Console's 128-bit limit.

Validation passed: Release builds in `build` (FLTK), `build-native` (FLTK) and
`build-rev` (Rev), plus the main FLTK and Rev headless self-checks; transfer and
stream-receive suites; all 12 CLI integration cases; six focused GUI suites
(layout, inspection, binary editor, application, adapter boundary, controller);
and the full shared GUI smoke workflow in about 96 seconds under its existing
300-second timeout. Targeted ASan/UBSan runs passed for transfer, stream receive
and short compression without diagnostics. `git diff --check` is clean.
GUI geometry and shared application paths were checked headlessly; no physical
audio link or native-window rendering was measured in this environment.

## Earlier fixed short dictionary restoration — 15 September 2026

The newer inclusive 16-byte limit and expanded Compression editor above supersede
this entry's original 15-byte limit.

Text of 1–15 source bytes again uses the original fixed dictionary. The exact-bit
encoder and decoder are restored without the former packed/length APIs or packet
parser. Text `e` sends `001`; an explicit Binary draft still transmits exactly
the entered bits. No marker, padding, length, FEC or MAC is added to either path.

Dictionary interpretation requires physical completion, complete canonical
tokens, no missing symbols, no recognized interval marker, at most 195 received
bits and at most 15 decoded bytes under the content quota. Pending observations
remain exact bits. CLI and GUI retain those bits alongside the completed text,
with no interval-validation or authentication claim and no received attachment.

Regression coverage pins the historical codebook and all 256 source bytes,
every token truncation boundary, canonical escapes, quotas, the 15/16-byte
threshold, optional encryption, exact airtime and the unchanged physical end
gate. The packed convenience API rejects partial bytes; it cannot silently
pad a dictionary stream or index beyond its packed buffer.

Validation passed:

- Release CLI and native GUI builds.
- All 14 affected headless suites: short compression, transfer, stream receive,
  regressions, CLI, attachment, pattern transfer, live, live resources, audio
  rates, GUI inspection, GUI controller, GUI application and native policy.
- The full shared GUI workflow, run serially, in about 108 seconds under its
  existing 300-second timeout.
- ASan/UBSan runs for short compression, transfer and stream receive, without
  diagnostics; `git diff --check` is clean.

The GUI checks exercised shared headless workflows, not native window rendering
or a physical speaker/microphone link.

## Earlier raw-message restoration — 15 September 2026

This entry describes the intermediate literal-byte behavior in `a495328`;
the dictionary restoration above supersedes that encoding choice.

Commit `4dd23c5` removed the automatic under-16-byte text bypass along with the
old packet/dictionary codecs. Explicit Binary/status transmission survived, but
the drainable receiver buffered fewer than 1,024 accepted bits until stream end.
That delayed pending presentation for precisely the few-bit, very slow use case.

Nonempty text of 1–15 source bytes then sent its exact MSB-first byte bits,
without markers, compression, padding, FEC or MAC. The dictionary was still
removed: text `e` was eight bits, while explicit Binary `001` was three bits.
Text of at least 16 bytes and all attachments retain fixed coding intervals.
The threshold affects transmission only and never ends a reception.

Receiver drains now expose accepted symbols below the chunk capacity. Regression
coverage includes:

- 1/3/15/16-byte thresholds across FEC, compression and keyed/public settings,
  including source quotas and matching raw waveform estimates.
- Pending `0` → `00` → `001`, stable row identity, no byte padding or premature
  completion, exact copied bytes and restored longer-message FEC selection.
- Actual generated PCM for three four-hour symbols at a 64 Hz sample clock,
  consumed in 4,096-sample blocks. Each accepted bit appears at its own endpoint
  within a 1 MiB receiver workspace. A subsequent wholly absent four-hour symbol
  completes reception; six seconds inside that symbol does not preempt it.
- Incremental chunks collected by physical stream identity, including interleaved
  hypotheses, timed gaps, carrier labeling, echo suppression and EOF flushes.

These generated-sample checks exercise long symbol coordinates and bounded
processing; they do not establish a measured acoustic or radio sensitivity.

Validation passed: the Release build, all 54 headless tests across the suite and
affected reruns, and the full shared GUI workflow with its existing 300-second
timeout. The physical suites include 29 receiver cases and 23 correlator cases.
Targeted ASan/UBSan runs passed for transfer/source reception, four-hour symbols,
incremental gaps, clock-rate hypotheses, carrier reporting and physical end.
Three tests requiring a native graphical display were not run; the native
executable built and its headless self-check passed.

## Reception, echo suppression and attachments — September 2026

The follow-up changes retain the fixed 128-byte interval format and the sole
six-second physical absence rule. Deterministic regressions now cover:

- A first symbol shifted by −28.125 Hz followed by a 1500 Hz stream: exact bits
  finish labeled 1500 Hz. A genuinely shifted stream remains labeled 1471.875 Hz.
- Duplicate overlapping carrier hypotheses in the clock correlator, with one
  physical stream emitted. Marginal isolated evidence admitted by the former
  `1e-8` significance is rejected by the new `1e-10` default. The stricter
  `1e-12` trial rejected short-pattern source fixtures and was not adopted.
- Exactly two seconds of independently generated suppression noise after the
  payload/filter tail. A weaker two-second delayed copy produces extra bits in
  the no-suppression control and exact bits with suppression. This is one
  deterministic channel fixture, not a bound on real-world echo delays.
- Actual sampled PCM with erased marker, data and final-parity symbols, including
  public shaped SF16/RS20 and keyed cases. Exact source recovery reports separate
  data/parity repairs and known versus missing bit coverage. A noisy keyed source
  also requires measurable RS recovery before successful source decoding.
- Short keyed streams surviving an epoch refresh before their first output chunk.
  An admitted receiver and a correlator with a drained bit buffer remain
  active until their physical search ends; an empty output buffer cannot make
  receiver retirement discard the six-second wait. Unconfirmed noise searches
  still retire within the receiver workspace budget.
- The explicit attachment prefix, exact file bytes and filename restoration,
  Repeatable disabled, ordinary binary/text excluded from the file list,
  post-end interpretation, bounded names and no released source on quota failure.

The Release build and all 54 headless tests pass, including a targeted rerun of
the application presentation fixture updated for the new gap/repair label.
Targeted ASan/UBSan checks passed for the codec, carrier recovery/correlator and
suppression waveform paths. Three native-window tests remain excluded because
no graphical display is available; the native executable and its self-check build
and pass. The full shared GUI workflow passes with the same 300-second budget as the
native workflow. It covers generated production keys, the formerly lost short
keyed raw message, attachment filename and exact saved bytes, ordinary message
copying, replay replacement/cancellation and fixed FEC for short source text.

## Earlier fixed-interval migration — September 2026

This entry records the initial migration; the short-dictionary restoration above
supersedes its removal of short-text encoding. Longer messages and attachments
use the fixed-interval format in [protocol.md](protocol.md).
Its only end rule is six seconds covered by consecutive fully scored failed
symbols; one failed symbol suffices at durations of six seconds or longer.
The migration removed the old packet parser, early completion callbacks,
transmitted lengths and short-text dictionary. FEC/MAC outcomes and codec ends cannot release
content before physical completion. EOF and resource limits remain interruptions.

Migration regression coverage includes fixed geometry and local metadata, RS errors
and erasures in data and parity, missing final bits, keyed address/phase recovery,
marker loss and attempt budgets, bounded source spooling, shared quotas, real
quiet capture tails, exact binary source recovery and cancellation. The receiver,
marker and codec suites have also been exercised with ASan/UBSan. Final migration results on 15 September 2026:

| Check | Result |
|---|---|
| Release build, including native FLTK executable | Passed |
| `ctest --test-dir build --output-on-failure -LE native_gui -j 2` | 52/53 passed initially; the remaining GUI application suite passed after migrating its obsolete under-16-byte FEC fixtures |
| Final headless suite status | All 53 passed, including the new stream receive and six-case live resource suites |
| Full shared GUI workflow, run serially | Passed: replay, keyed raw bits, exact file save, overwrite refusal, clipboard retention, cancellation and fixed FEC for tiny sources |
| RS/source codec, marker collector, FFT receiver, correlator and wrapper | ASan/UBSan runs passed without diagnostics; final fixed-six-second API cleanup also passed Release receiver suites |
| `git diff --check` and removed API scan | Passed; no packet parser, short dictionary codec, packet completion callback or configurable gap timeout remains in production code |

The graphical display could not be opened (`DISPLAY=:0`), so three native-window
conformance tests were excluded. The native executable built and its self-check
passed. The shared smoke initially missed a keyed raw reception while competing
with the full suite; its prescribed serial run passed. These checks do not
establish real-time reception throughput under arbitrary competing CPU load.

## Historical validation entries

The following dated entries describe earlier checkouts and their then-current
packet/dictionary behavior. They are retained as history, not the current wire
format, end policy or acceptance tests.

## Weak-symbol timeout and payload memory — 14 September 2026

The existing timeout remains six seconds of consecutive unconfirmed symbols,
evaluated at complete symbol boundaries with a minimum of two failed symbols.
A short gap preserves the admitted clock; exceeding that limit ends the active
span and trims its unconfirmed tail. Packet length does not extend the timeout.

The FFT receiver already releases expired tracks. The clock-window correlator
now also releases terminated spans' bit-vector allocations, including at EOF,
instead of retaining their empty capacity in the acquisition hypotheses.
Fixed acquisition scratch and bounded completed-message output remain available.
Regressions verify the default six-second boundary in both paths, no storage
growth or repeated completion during continued silence, and exact restoration
of the correlator's baseline workspace after draining completion. Independent
later reception retains its correct stream position.

Both full receiver suites passed in Release, and the full correlator suite
passed ASan/UBSan with no diagnostics (`detect_leaks=0`, halt on errors).
Focused live checks passed for long-symbol reception after idle, gap recovery
upgrading earlier short content, and long-packet processing/cancellation. The
application rebuilt successfully.

## Timed gaps before Reed–Solomon correction — 14 September 2026

Live reception, capture decoding and transfer simulation now preserve unknown
interior symbol slots on an admitted clock. Later independent evidence must
confirm their extent; unknowns add no score, cannot choose a private schedule,
and are trimmed from unconfirmed tails. Known bits retain their original Data
stream addresses. Packet recovery fills unknown plaintext slots with zero and
requires a recognized leading marker plus complete integrity validation.
CLI output and GUI signal rows disclose the number of inferred zero slots.

Twelve deterministic blanked-PCM cases cover public and encrypted compact
17-byte packets, with gaps in the marker, header and body. All eight RS20/RS60
cases recover the exact content with nonzero corrections; all four equivalent
FEC-off cases fail integrity. Low-level checks cover chunk independence,
gap expiry, bit/workspace limits, unsupported weak tails, independent later
starts and ambiguous stream phases. A complete SHA/MAC-validated packet closes
before a new gap; tests preserve two consecutive packets on the same clock,
including a first packet already repaired after a missing symbol. Completion
probes also check truncation, wrong keys, partial leading markers and highly
compressed packets under the actual configured content budget.

A live audio-adapter regression pauses capture inside a marker gap, observes
the completed raw span and an early short-text interpretation, then resumes
capture. The same signal row upgrades to the fully validated original packet
with three missing slots and nonzero RS corrections. A cancellation regression
waits for fresh measured plot points, since receiver evidence can advance the
snapshot sequence before the next plot publication.

Marker recovery now charges only observed bits to its `2^-84` ideal fair-bit
false-match budget and permits up to 80 contiguous deleted marker bits. Tests
cover distributed timed erasures, unknown trailing anchors, slot-trial costs,
the 102-known-bit versus 101-known-bit threshold on short inputs, and rejection
of competing marker endpoints. These are analytic-model checks, not empirical
measurements of extremely rare false matches. Marker bytes and cadence,
packet encoding, crypto streams and symbol evidence thresholds are unchanged.

All **53 tests that do not require a display** passed across the Release batch
and targeted runs, including the full live suite (121.66 seconds alone), all
27 CLI cases, both receiver paths and GUI-model checks. The three native GUI
interaction suites require an unavailable display. The native application
built successfully and its display-free self-check passed.

The four core suites (`boundary_sync`, `pattern_receiver`, `pattern_correlator`,
`pattern_transfer`) passed ASan/UBSan. Focused runs against the rebuilt
sanitized library also passed the packet-completion callback in both receiver
paths and consecutive-packet regressions. After the final live changes, the
audio-gap content-upgrade regression and complete correlator suite also passed
against the rebuilt sanitized library. Both sanitizers halted on errors;
leak detection was disabled. No sanitizer diagnostics were reported.

## Pulse shaping at unchanged payload rate — 14 September 2026

Pattern profiles with at least 16 complete chip times now apply a 25% RRC
pulse spanning 16 chips. The nominal 1,200 Hz profile retains its 600 chips/s
and original symbol durations; complete filter tails add 160 samples at 6 kHz
(26.7 ms) per burst. Shorter manual patterns and tone modes retain their
previous pulses. Both peers must use matching pulse-shaping settings.

A private 65,536-chip capture measured 742–746 Hz at 26 dB below the spectral
peak using Hann windows of 8,192–32,768 samples. Radial limiting reduced power
by 0.083 dB relative to the unlimited linear waveform, with 0.109% relative
mean-squared error. A capture including protected settling measured 744.9 Hz
with an 8,192-sample window; prefix-only and payload-only widths were 741.9 Hz
and 745.6 Hz. These finite software measurements do not certify an RF mask or
an adversary's detection time. Regular chip timing and burst edges remain
observable features. See [pulse shaping](modem.md#pulse-shaping).

Frozen pre-change Data ciphertext and fourteen private complex chip values
still match. Toggling pulse shaping also preserves the complete encrypted wire
bits for a fixed message ID. AES/HKDF, purpose keys, counter domains, XOR mixing,
bit alternatives and chip addresses are unchanged. The filter adds no chips or
public acquisition marker. Both receive paths fit shaped candidate patterns
against the original sample/bin observations, retaining their confidence
thresholds and timing-search rules. An independent raw-sample Gram calculation
checks both bit alternatives, including partial chips and rate error; unknown
neighbor-symbol tails and limiter distortion remain residual error.

Paired deterministic AWGN tests normalize both waveforms to the same received
C/N0 and use unchanged acceptance thresholds. They recover all 86 payload bits
per waveform across +26, +20, +6 and -6 dB-Hz scenarios, including fractional
sample origins and +/-5,000 ppm clock errors. Aggregate shaped/rectangular
evidence ratios are respectively 1.003 (exact timing, +26), 1.101 (+26,
+5,000 ppm), 1.122 (+20, -5,000 ppm), 1.238 (+6, 20-second symbols), and 1.088
(-6, 320-second symbols). Fractional timing and linear channel interpolation
can favor the smoother waveform. These fixtures guard against a substantial
confidence regression; they do not establish equal field error rates or
false-alarm calibration in correlated HF noise.

The fast FFT receiver's disjoint sample bins introduce a small additional
loss. An eight-seed +26 dB-Hz private comparison found an aggregate shaped/
rectangular score ratio of 0.949875 over all 48 payload symbols, including
unconfirmed candidates. In one marginal final symbol, the shaped score was 37.680 against an
unchanged 37.803 acceptance threshold, versus 40.091 for rectangular pulses;
the shaped result correctly left that last bit unconfirmed. The raw-sample
correlator recovered both complete bursts in that case, with aggregate scores
within 0.3%. Exact bin-averaged templates offered no consistent improvement
over the existing midpoint templates, so the implementation keeps their
bounded cost. All emitted bits were correct, with complete bursts in seven of
eight shaped cases and all eight rectangular cases. A permanent regression
checks the score ratio without forcing marginal bits past the confidence
threshold. No threshold was weakened to hide this measured difference.

All **53 tests that do not require a display** passed across the full Release
run and targeted reruns, including the live suite (122.49 seconds alone), the
26-test CLI suite, audio-rate conversion, crypto, receiver and GUI-model tests.
The three native GUI interaction suites could not run because this environment
has no display or Xvfb. The native GUI built and its display-free self-check
passed. Inspection accounts for pulse-tail airtime separately and identifies
its chip-space plots as input-chip design illustrations before shaping.

The four rebuilt ASan/UBSan suites (`pattern_code`, `pattern_receiver`,
`pattern_correlator`, `pulse_shaping`) passed in 339.25 seconds with both
sanitizers configured to halt on errors. LeakSanitizer was disabled for this
sandbox's ptrace restriction; no address or undefined-behavior diagnostics
were reported. The added fast-receiver confidence regression then passed a
separate rebuilt `pulse_shaping` ASan/UBSan run with the same settings.

## Public I/Q patterns and transmit history — 14 September 2026

Unkeyed public patterns now use circular I/Q values with varying amplitude and
phase, replacing the real-sign waveform. Their two complete bit templates still
repeat each symbol, so short public patterns retain a finite, potentially sparse
cloud. Private stream addressing and tone waveforms are unchanged.

The audio transmit plot now shows the transmitter's last 2,048 actual payload
chip values across GUI polls. It no longer replaces them with only the fresh
batch or measured samples between chip boundaries. The live regression verifies
exact emitted values, stability inside a chip, growth across updates, bounded
retention, and omission counts when the GUI stalls. Settling and independent
receiver-input simulation retain their measured views.

Short public symbols use sample-resolution FFT timing with an exact real
carrier-basis Gram fit. Evidence stays capped at the previous half-chip scale;
private timing and scoring are unchanged. Ninety cases cover 3/4/6/8/12/16-chip
public patterns, five sample offsets and three phases, preserving every bit and
the exact final endpoint. Initial admission may precede the true start by up to
two samples. Tests also cover shared projections with an arbitrary phase,
300-symbol wrong-key/noise captures, and weak pending tails before a later burst.
Disabling the pending-tail protection breaks both replacement interference
fixtures, confirming that they still exercise that protection.

All **51 configured CTest suites** passed across the final run and an isolated
live-suite rerun. The concurrent run passed 50 suites but exceeded the large live
transfer's wall-clock timeout while other build/verification work ran; the full
live suite then passed alone in **111.13 seconds**. All **26 CLI tests** passed.
The receiver and streaming-modem ASan/UBSan suites passed in **41.87 seconds**
with halt-on-error enabled and LeakSanitizer disabled for this sandbox's ptrace
restriction. The rebuilt native FLTK GUI passed its display-free self-check.

## Protected pattern-only waveform — 14 September 2026

The APSK transmitter/receiver, fixed training bytes, repeating spread template,
legacy whitening and post-encryption symbol padding have been removed. Every
selected non-tone key enables private pattern templates and Data encryption.
Tone clears the key and all private spreading at GUI, CLI, transfer and live
boundaries, including stale key lists and keyfile reloads.

Private templates now map eight mixed keystream bytes per chip to capped
circular I/Q noise with varying amplitude and phase. Settling combines Data
and all enabled private streams before the same mapping and chip cadence, in
separate preamble counter positions. FFT acquisition uses actual template
energy; the clock-window correlator retains its existing full Gram fit.
Pattern evidence remains the only acquisition and time/key alignment source.
Both peers must use this updated waveform.

The Release build passed all **51 configured CTest tests**, including sampled
channel/sample-rate, crypto/packet, pattern receiver/correlator, live session,
and GUI controller/inspection coverage. The independent CLI suite passed all
**26 tests**. After the final seeded-estimator memory-accounting correction
and obsolete-header cleanup, the three affected `pattern_transfer`, `tuning`
and `regressions` suites also passed. The native FLTK GUI and receiver benchmark
built successfully, and `datapump-gui --self-check` passed without a display.
Four focused ASan/UBSan suites (`pattern_code`, `pattern_receiver`,
`pattern_correlator`, `pattern_transfer`) passed in 33.33 seconds with both
sanitizers configured to halt on errors. LeakSanitizer was disabled because
this sandbox's ptrace environment prevents its shutdown inspection; no address
or undefined-behavior findings were reported.

The standalone extended controller smoke previously reached its 100-second
deadline during a later replay scenario; the default controller suite, including
production-key tone regressions, passed. This is not a completed interactive
hardware GUI certification.

Repeating the original `/tmp` waveform comparison with different messages and
keys (Data, Scrambler and DSSS enabled, prefix omitted, 1,920 samples at 6 kHz)
now changes every PCM sample. Maximum absolute squared-sample difference is
approximately **0.95946**, compared with **exactly zero** before the correction.
Permanent tests cover circular quadratures, variable amplitude, private stream
addressing/cache boundaries, each independent private layer, protected-prefix
mixing, exact bit counts and noise-only/wrong-key rejection. This validates
removal of that particular invariant; bandwidth, chip timing, capped amplitudes
and burst edges remain, and no measured interception probability is established.

## Partial byte-boundary marker recognition — September 2026

This section records the original 64-bit-loss, `2^-100` implementation and its
validation. The current [marker evidence threshold](protocol.md#marker-evidence-threshold)
permits up to 80 missing marker bits with a `2^-84` bound; the test results below
predate that adjustment.

Marker recognition now permits up to eight changed bits in a complete 192-bit
marker. Partial recognition permits one contiguous loss of 1 through 64 bits,
including a missing prefix, while preserving an exact final 32-bit anchor.
The acquired leading stream-symbol index can identify a lost initial prefix
after ordinary decryption; unkeyed input can also infer that prefix through the
bounded deletion search. Different passing endpoints reject recovery, while
equivalent paths sharing an endpoint use the strongest evidence. Starts remain
restricted to offsets 0 through 7 initially and -7 through +7 periodically.
Recovery still precedes deinterleaving, Reed–Solomon correction and packet
integrity checks. Recognized marker evidence suppresses dictionary fallback
for an invalid packet, and the acquired raw bits remain available.
When an acquired index fixes the surviving suffix's endpoint, its mismatch
budget also permits errors in the final 32 bits; inferred endpoints require
the exact trailing anchor.

The [analytic acceptance model](protocol.md#marker-evidence-threshold) charges
all searched slots, starts, deletion runs and mismatch patterns to a `2^-100`
false-match bound per recovery call under independent fair input bits. The
allowed mismatches decrease as surviving markers shorten or inputs grow.
This is a calculated model bound; software tests do not measure events at that
probability or establish a channel error rate. Distributed missing marker bits,
lost marker trailers, lost crypto alignment and whole missing data blocks
remain outside this recovery model. Packet integrity and authentication retain
their existing checks, and the wire marker, cadence and overhead are unchanged.

The validation results in the older byte-boundary section below predate this
partial-marker implementation and do not establish its test status.

For this implementation, the rebuilt Release `boundary_sync`,
`pattern_transfer`, `transfer`, and `gui_inspection` suites passed. Coverage
includes missing leading/interior marker runs, flipped bits, confidence-budget
rejection, text/file identification, and payload-byte errors with FEC off,
RS20, and RS60. Clear and keyed PCM captures with the first marker symbol
removed were acquired and decoded; the keyed receiver independently retained
the correct stream-symbol index. The boundary suite also passed ASan/UBSan
with halt-on-error enabled and leak detection disabled for the sandbox.
All 26 CLI tests passed. The Release executable and 183 source/document files
passed the runtime-marker storage scan at every bit phase; `git diff --check`
also passed.

Older entries below record their earlier implementation and test state; their
legacy waveform behavior and timing benchmarks do not describe this build.

## Periodic byte-boundary recovery — September 2026

Compact-packet pattern transport now inserts two copies of a runtime-derived
96-bit word before the encoded packet and after every complete 256 encoded
bytes, then encrypts the complete wire sequence, including markers, when a key
is selected. Recovery runs after
pattern acquisition and the unchanged whole-stream Data decryption. It matches
plaintext markers, normalizes plaintext intervals and removes markers before
deinterleaving, FEC and whole-packet integrity. The initial marker search uses
offsets 0 through 7 from the burst origin; a damaged initial marker consumes
its nominal 192-bit slot when available. Each periodic marker search is
restricted to seven bits either side of its expected slot, and normalization
retains a damaged interval's prefix while trimming or zero-filling its tail.
A recovered candidate permits one packet
at the existing burst origin, with exact extent and no inner-packet search or
retry of unstripped bytes or older packets without the initial marker. Exact
raw bits, text below 16 original bytes and byte packet APIs retain their
existing formats. Text of at least 16 bytes and every attachment use the initial
marker, including when the encoded packet is shorter than 256 bytes. Marker
overhead for `N` encoded bytes is `24 * (1 + floor(N / 256))` bytes.

Relevant checks are the 15/16-byte text threshold, short attachments, encoded
packets below 256 bytes and an exact final 256-byte block;
FEC-off and keyed/unkeyed round trips; inserted, deleted and changed plaintext
bits; damaged markers followed by intact markers; and rejection of nested or
trailing packet candidates. Source/executable scanning checks that even one
96-bit marker word is absent at every bit offset. Such a scan cannot guarantee its absence
from arbitrary transferred data or runtime memory dumps.

Before the initial marker was added, the six focused Release suites passed:
`boundary_sync`, `crypto`, `pattern_transfer`, `transfer`, `live`, and
`gui_inspection`. `boundary_sync`,
`crypto`, and `pattern_transfer` also passed with ASan/UBSan; LeakSanitizer was
disabled for that run. The pattern suite includes clear and encrypted PCM
round trips crossing a marker through the unchanged constellation decoder.
The optimized Release `pump` executable and 180 source/document files passed
the runtime-derived marker scan at all eight bit phases. `git diff --check`
also passed. These results describe the focused suites, not a full-suite run.

With the initial marker added, the five focused Release suites passed:
`boundary_sync`, `pattern_transfer`, `transfer`, `audio_rates`, and
`gui_inspection`. They cover the leading marker, bounded leading-bit recovery,
damaged aligned markers, encrypted packets, short dictionary/raw-bit bypass,
transmission estimates and audio sample-rate conversion.
All 26 CLI tests passed. The Release executable and 183 source/document files
also passed the runtime-marker storage scan at every bit phase, and
`git diff --check` passed.

These are transport-bit and software integration checks. They do not establish
recovery from whole missing blocks, an unknown absolute stream offset, lost
Data-stream/Scrambler/DSSS alignment or a slip in a tail with no later intact
marker. Pattern constellation decoding remains the sole authority for timing
and keystream alignment; recovery never trials offsets, resets counters or
reseeds streams. An intact marker never substitutes for packet SHA-256/HMAC
validation.

## Shared preamble keys and CTR pad — September 2026

The preamble now uses the same Data key and the same enabled Scrambler/DSSS
waveform keys and transmission epoch as the payload. Its only cryptographic
distinction is the fixed eight-byte ASCII `preamble` pad in the high half of
the AES-CTR counter; the low half contains the ordinary block offset. All
preamble-specific HMAC key derivations have been removed. Those derivations
were deterministic, and neither version generates random preamble keys.

Frozen Crypto vectors check unchanged payload output and the preamble counter
format at the beginning and end of the 64-bit byte-address range. All four
stream purposes have domain, epoch, chunk and random-access coverage. The
waveform checks recover the actual encrypted phase words against the selected
Data key, then verify each spreading layer against its existing payload key
with the preamble counter pad. Payload samples and positions remain unchanged;
the existing prefix-only rejection, full-prefix and lost-prefix cases remain
part of receiver validation.

All 48 Release CTest suites passed in 173.81 seconds. The four focused
ASan/UBSan suites (`crypto`, `pattern_code`,
`pattern_receiver`, `pattern_transfer`) passed in 19.61 seconds with leak
detection disabled.

## Preamble Data encryption — September 2026

Preamble noise bytes now pass through bytewise Data-purpose AES-CTR encryption
before I/Q mapping, followed by every enabled Scrambler and DSSS layer. The
selected key derives a dedicated hardware Data key; the prefix consumes no
payload keystream positions. At most four fixed 512-byte caches are allocated,
and the extra Data cache is included in the transmitter memory ceiling.

An analytic-waveform test recovers the transmitted phase words and verifies
their bytewise XOR against the dedicated Data stream across cache boundaries.
Chunked output with all three private layers preserves the waveform and PCM
headroom. A transmitter-only tone check verifies half-chip noise refreshes;
it adds no tone reception test and reserves no payload constellation points.
The receiver matrix retains its 32 existing cases and adds Data-only and
Data-plus-Scrambler-plus-DSSS preambles. These cases clear the preamble Data
seed at the receiver, reject prefix-only captures, and recover exact `001`
with the prefix present or entirely removed.

All 48 Release CTest suites passed in 165.38 seconds. The three focused
ASan/UBSan suites (`pattern_code`, `pattern_receiver`, `pattern_transfer`)
passed in 20.15 seconds with leak detection disabled. These software checks
do not measure physical low probability of intercept or external AGC behavior.

## Private hardware-noise preamble — September 2026

Hardware settling now uses independent circular Gaussian-derived I/Q noise,
with normalized mean power and bounded peaks. Any selected key supplies a
private noise seed, including Data-only configurations. Every enabled Scrambler
and DSSS layer also applies through a separate hardware derivation domain;
both layers affect the prefix when both are selected. At most three fixed
512-byte caches hold these streams, without consuming payload stream positions.

PCM checks vary the key, epoch, noise seed and each spreading seed independently.
They check both noise quadratures, peak headroom, mean power, chunk invariance,
payload independence and unchanged payload samples after the prefix. Thirty-two
receiver cases combine 64/128 chips, 100/1200 Hz, two epochs and all four
spreading-layer combinations. Prefix-only captures produce no acquired burst;
full and entirely removed prefixes both recover exact `001` under a 2 MiB
receiver ceiling.

Two nuisance-waveform regressions also prevent an admitted weak hypothesis or
its unconfirmed tail from blocking a much stronger later pattern. The captured
100 Hz CLI failure now recovers all 696 payload bits at sample 30,720 and
validates `independent clock`, using only pattern evidence to choose timing.
The final Release run passed all 48 CTest suites in 90.92 seconds. The three
focused ASan/UBSan suites passed in 21.09 seconds with leak detection
disabled. These finite software checks do not establish physical low
probability of intercept. The noise's rectangular half-chip updates have a
wider first-null spectrum than full-chip payload pulses; no enforced spectral
mask or physical AGC measurement is claimed.

## Binary pattern transport — September 2026

The hardware-settling follow-up rounds the five-second target to whole
sample-quantized payload-symbol durations, with ties upward. Duration checks
cover subsecond symbols, the ten-second boundary and hour-long symbols. Exact
three-bit PCM checks cover reception with the full prefix and with the entire
prefix removed, public and keyed spreading, and a listener whose local clock
starts after the keyed prefix with zero additional epoch-search radius.
Inspection and airtime estimates count settling separately from payload bits.
These are software checks; physical gain-control and muting behavior has not
been measured.

The follow-up also fixes weak candidates borrowing confidence from a later
individually strong symbol. An unadmitted prefix is discarded when that strong
symbol establishes a new burst; already admitted chains retain their pending
continuations. A retained failing 512-byte file recording previously produced
4,075 raw bits starting at sample 28,150. It now produces exactly 4,072 bits
starting at the actual payload boundary, sample 30,080, and validates the
original file. Both receiver paths have deterministic weak-prefix regressions.
No preamble recognition or packet validity enters the timing decision.

The consecutive text/file smoke workflow also exposed identical public
templates in plaintext and keyed Auto Pattern receive banks. The file's raw
symbol count was correct, but another bank could apply the wrong Data mask
while reporting the same pattern evidence. Auto Pattern and forced pattern
lengths now use private pattern fragments whenever a key is selected. The
key hypothesis is distinguished at pattern acquisition, without using packet
validity to select a key or adding any transmitted fields.

The completed follow-up Release run passed **48/48 CTest suites** in 95.69
seconds. The strict shared GUI smoke workflow also passed, including
consecutive plaintext text/file reception with receive keys loaded and exact
encrypted raw bits. Its existing deadline and acceptance checks were retained.
All six affected suites have passing ASan/UBSan coverage: pattern generation,
both correlation paths, transfer integration, tuning and live sessions. The
live coverage combines the full-run prefix with focused completion of the new
multiple-key case and the remaining cases. That new case initially reached
61% of a transmission before its 30-second test allowance expired under
instrumentation; it passed with a dedicated 90-second allowance and unchanged
content and memory assertions. Leak detection was disabled. The strengthened
long-correlator fixture was additionally linked against a
temporary copy of the pre-fix implementation and failed at its boundary
assertion, confirming that the regression exercises the corrected behavior.

The initial automatic transport used pattern evidence for
acquisition and exact burst endpoints. It preserves explicit legacy APSK
fixtures separately. That initial Release build passed **48/48 CTest suites**
in 78.05 seconds, including the CLI, shared GUI controller, live session,
resampling, packet, crypto and new pattern suites.

The initial **six focused ASan/UBSan suites** also passed (11.30 seconds): short
compression, pattern generation, both correlation paths, transfer integration,
and the exact-bit editor. Leak detection was disabled for this execution
environment; this is not a leak-sanitizer result.

New actual-PCM checks cover all eight possible three-bit messages without a
supplied bit count; dictionary text `e` occupying exactly three bits; unknown
start and carrier phase; wrong keys and noise-only captures; cropped keyed
streams; weak chips at -16 dB; and 1,536 received bits with 100 ppm clock error
and phase noise. Independent sound-card conversions preserve public and keyed
`001` and a downstream packet. A two-seed weak-channel regression recovers
three bits with the longer selected integration and rejects the shorter
integration in that same sampled channel. Packet integrity cannot discard
otherwise acquired raw bits or select their timing.

A deterministic live regression advances the receiver's injected clock from
`E + 0.90` to `E + 1.01` with a zero-second epoch-search radius and transmits
exactly `001`. It verifies that new epochs are admitted before the next PCM
block, without waiting a full elapsed second. This fixes an intermittent
keyed-burst failure exposed by the shared GUI smoke workflow. Previous epochs
remain available through their bounded symbol-completion interval.

The final toolkit-independent controller plus strict GUI smoke workflow also
passed, including key generation/loading, sampled encrypted text and files,
exact encrypted 32-bit raw recovery, replay replacement/cancellation, and
return to live plots. The smoke sequence now observes resumed live plots before
reconfiguring the key, which otherwise clears the completed replay's ID.

Long-symbol checks generate bounded prefixes and verify constant state during
a four-hour symbol, exact three-bit/12-hour transmitter estimates, cancellation,
and rejection of unaffordable clock windows. They do **not** run a complete
12-hour radio experiment. See the measured per-window memory and CPU costs in
[pattern constellation](pattern-constellation.md#streaming-long-symbol-correlation).
The scalar fallback is not real-time-capable for every admitted window on this
host. Carrier/rate coverage is finite, and physical oscillator coherence and
false-alarm behavior in colored interference remain uncalibrated.

The native FLTK application could not be rebuilt in this environment because
X11 development headers/libraries are missing (`Window` is undefined in the
FLTK platform headers). The toolkit-independent GUI application and controller
build and run. Earlier native GUI results below are historical and are not a
claim that this new waveform was verified through native windows here.

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

After replacing the forced-tone fixtures, all eight affected C++ suites passed:
`tuning`, `regressions`, `streaming_modem`, `transfer`, `live`, `audio_rates`,
`gui_inspection` and `gui_pattern_space`. All 23 CLI tests also passed, including
the 1,024-chip keyed simulation whose full PCM exceeds its batch memory limit.
Dense PCM checks retain exact pre-FEC bytes and require frequent measured phase
and amplitude changes with both fixed and seeded chip patterns.

The weak-channel fixture uses a seeded pattern planned at 24 dB-Hz, 6 dB of
acquisition margin, two channel seeds, ideal clocks and RS60; its shorter
integration control must fail in the same channel. This checks integration
behavior, not calibrated sensitivity at the planner's target. Long live-pattern
fixtures use a bounded 2 MiB DSP budget for their larger timing search bank.

## Synchronized message and binary editors

The Message/Binary source selector has been replaced by two views of the same
message payload. The binary editor updates the first sixteen bytes and preserves
the suffix. Tests cover byte order, whitespace, partial drafts, arbitrary byte
values, UTF-8 boundaries, escaped text and decoded payload limits. The controller
test also sends binary-edited `00 FF` through simulation and verifies that exact
payload after reception.

All 22 shared GUI tests and both FLTK/Rev native adapter suites pass. Native
keyboard checks exercise Ctrl+C/Ctrl+V, selection replacement and rejected
pastes. A real Rev keyboard exercise confirmed synchronization, suffix retention
and the sixteen-byte layout at default and minimum window sizes. FLTK now keeps
the selection until paste validation completes; Rev suppresses text events
generated by clipboard shortcuts.

The full shared smoke workflow stops at phase 8 with `Pending replay signal was
not presented`, before the binary-edit phase. An independent build of the exact
pre-change GUI sources at `feafb16b6f3e56e7ba685cbe22ec21fadd522ac4`, linked to the
same modem library, reproduces the identical failure.

## GUI abstraction audit and completion

The review traced native entry points and helpers, the public facade, control
and document declarations, layout, editor/record input, bitmap transfers,
platform-service dispatch, and CMake/CI dependencies. Three parallel reviews
covered controls, documents/layout, and boundary enforcement, followed by an
integration review and native regressions.

Remaining gaps were corrected: exported modem include paths; stale declared
callbacks after visibility, page or shutdown changes; duplicated UTF-8 edit and
record extent policy; Rev horizontal record scrolling and stale native glyph
measurement; duplicated document action identity/eligibility; unstable focus
when repeated document actions move; and zero/exhausted control allocations.
Native adapters now translate these shared decisions into toolkit operations.
The window title also comes from the shared declaration module.

The architecture check now runs in production builds, recursively checks
alternate source extensions and helper aliases, and rejects hidden toolkit or
backend-specific dependencies in shared code. Regression mutations cover both
forbidden dependencies and valid constructs that must remain accepted. All 17
public headers compiled individually as C++20 without modem/toolkit include
paths. A separate linked contract canary verifies that the application target
does not export modem include directories. A fresh `BUILD_TESTING=OFF` FLTK
configuration also passed its production boundary target.

Earlier Linux Release FLTK validation passed all 20 GUI tests, including the
simulated workflow, native adapter and document suites. The broader 45-test run passed 44 initially;
its live-simulation timeout passed when rerun alone (31.8 seconds). Native package
relocation and corruption checks passed. ASan/UBSan passed all 19 GUI checks other
than the separately validated workflow, with leak detection disabled for the
native toolkit environment. These include the actual FLTK widget/document tests.

Earlier Linux Release Rev validation passed all 22 GUI tests, including production
workflow, native adapter workflow, clipboard/platform checks and 1x/2x coordinates. The exact
profile was software OpenGL, `LP_NUM_THREADS=2`, and Xvfb at 2400x1800x24 with
96 DPI. The native probes also verify zero-area document rectangles, retained
action resizing and framebuffer extents from physical pixel endpoints. Rev
record measurements and geometry now avoid invalidating unchanged rows.
The default llvmpipe thread configuration repeatedly missed the final 50 ms
raw-replay symbol observation on this host; the bounded profile passed the same
unchanged assertions and is now used in the GUI-contract CI matrix. See
[Rev software rendering](rev-backend.md#software-rendering-and-validation).

The follow-up interface audit found and corrected additional gaps: repeated
ordinary declarations could overlap in relative rows; Rev could miss changes to
initially absent labels or shared geometry; typography and caption layers could
remain stale; service editors used different hardcoded limits; and FLTK dialogs
borrowed titles from temporary request storage. Layout identity and complete
geometry results are shared, caption layering comes from `ControlLayout`, and
service requests now declare input limits validated by the common queue. The
Rev file dialog no longer duplicates the controller's no-overwrite promise.
Both native suites use the same new layout lifecycle fixture and exercise
atomic prompt edits with a custom input limit.

Native conformance now runs separately from the production `gui_workflow` in
both backends. Rev previously repeated the entire shared workflow after its
widget probes; that second run repeatedly missed the short terminal raw-symbol
snapshot on this software-rendering host. A later production run reproduced the
same sampling sensitivity. All native probes remain, with the complete shared
workflow executed once through the production binary. The smoke's raw replay
check now accepts an unvisited tail only when completion returns live input,
reports dropped points, and delivers a completed raw signal. Packet replay still
requires observed received symbols, and timing, changing plots, pending reception
and exact raw-bit assertions remain. Deterministic `live` tests additionally
require received symbols in the terminal raw frame and verify skipped-terminal
point accounting at exactly three seconds. This does not establish a guarantee
that a slow native renderer displays every replay frame.

Final follow-up Linux Release validation passed **20/20 FLTK GUI tests** in
25.57 seconds and **22/22 Rev GUI tests** in 52.27 seconds. Both include the
production workflow and dedicated native conformance; Rev also includes actual
platform services and coordinates at 1x/2x. Builds and GUI workflows ran without
overlapping workloads, using the software-GL/Xvfb profile above. Both fresh
temporary directories were empty after success. The strengthened deterministic
`live` suite passed in 31.51 seconds. The final independent source review found
no remaining concrete application-ID decisions in either adapter, and the
production boundary guard and diff whitespace check passed.

The follow-up CLI-only contract/layout/application checks passed 9/9. Focused
ASan/UBSan checks passed 4/4, including FLTK native service title lifetimes and
input validation, with native-toolkit leak detection disabled. Successful smoke
runs now remove automatically created fixtures; explicit output directories and
failed-run evidence are retained. This prevents repeated GUI validation from
exhausting a temporary filesystem with large generated key fixtures.

The subsequent consolidation moved the remaining shared presentation
bookkeeping into `BindingState`, `RecordReconciliation` and
`DocumentPresentation`. Both adapters now consume common option retention,
record changes and document traversal/placement. Shared widget roles and chrome
preferences also cover disabled/focus/hover/selection colors, dialog content and
geometry, popup widths, tooltip timing, checkbox geometry, tab placement and
document content widths. Native glyph measurement, widget ownership, popup
screen fitting and file-browser controls remain toolkit responsibilities.

An independent source audit was performed after this consolidation and before
builds or regression execution. It caught missing bitmap-source and popup-
direction cache dependencies, dialog text measurement inferred from font size,
and native style precedence that could bypass shared disabled colors. These
were corrected and added to the shared/native conformance coverage. The boundary
guard additionally rejects new native RGB literals outside shared palette
conversion.

Final consolidation Release validation passed **24/24 FLTK GUI tests** in
27.10 seconds and **26/26 Rev GUI tests** in 62.28 seconds. Both include the
production workflow and native conformance; Rev also covers platform services
and 1x/2x coordinates. All **21 public headers** compile independently as C++20
without toolkit or modem include paths. Builds and native workflows ran
sequentially with the software-rendering profile above, and successful smoke
directories were empty afterward.

The native runs exposed two integration details. FLTK retained an unnecessary
hidden label for menus; it is now omitted, and the unchanged filtered-menu test
passes. Rev's probes assumed immediate event pumping delivered a paint. On X11,
the queued frame request may not have arrived yet, leaving the old geometry in
place. Tests now await the existing native paint counter, preserving their frame
counts and geometry/input assertions. Palette probes likewise inspect glyphs,
caret and selection after actual layout in both monochrome and color modes.
No production scheduling change was needed.

Visual comparison also caught a Rev checkbox adapter consuming its observable
change flag before the native checkbox could update, and missing disabled
styles on dropdown children. The adapter now observes without consuming that
flag, and maps disabled dropdown text, arrow and field colors explicitly.
Native tests assert unchecked/checked/unchecked paint and active/disabled/
re-enabled dropdown states in both palette modes. Both desktops use the shared
surface background role. Fresh isolated production-window captures were
visually checked for control state, disabled choices, layout, captions and
bitmap areas. The added FLTK surface assertion also passed in a final focused
native run; its production code was unchanged after the 24-test run.

The CLI-only consolidation checks passed **13/13** in 7.44 seconds. Focused
ASan/UBSan checks passed **15/15** in 8.46 seconds, including the new shared
retention/layout/chrome checks and actual FLTK widgets/documents; leak detection
was disabled for the native toolkit environment.

The maintenance scope remains features expressed with existing primitives.
New native primitive types, toolkit repairs and platform services still need
adapter implementations. These Linux runs do not establish Windows runtime
conformance or hardware audio validation.

## Rev backend and shared bitmap extraction

The optional Rev backend pins upstream `clean` at
`d73faa7759b5cfd30d592057790ab458568b569b`. Linux builds used Clang 19, Ninja,
and Mesa 25.0.7 llvmpipe (LLVM 19.1.7, OpenGL 4.5) on a private X11 display.
Controller/declaration, pixel format/tile replay, Unicode boundary and display-free
self-check suites passed. The shared controller smoke covered UTF-8 text, files,
exact leading-zero bits, stale drafts, pending reception and retained saves.
Rev rendered the same workflow, switched inspection pages and checked control
layout, UTF-8 editing, failed-paste selection preservation and modal focus.
Native clipboard tests covered UTF-8, empty versus failed reads, overlaps, stale
responses, incremental long selections and stalled-transfer limits/expiry.

The existing FLTK smoke and focused GUI/model suites passed after its plots were
moved to the shared producers. Bitmap tests also passed under ASan/UBSan. FLTK
plot output was inspected at ordinary and 200% display scale; Rev text wrapping,
Unicode glyphs, QR output and control layout were inspected visually.

A relocated Rev package passed dependency closure, inventory, CLI and full GUI
checks with its checkout, build trees, original installation, development
libraries and system fonts hidden. The path contained spaces and the environment
had empty `PATH` and `LD_LIBRARY_PATH`. Tampered and unrecorded files were rejected.
Host graphics drivers remain external dependencies.

Software OpenGL works but remains CPU-intensive: a 31-second smoke-plus-hold run
used 74 seconds user CPU after paint optimizations and a 10 Hz presentation cap,
versus 211 seconds before. Modem events and plot history remain polled at 25 Hz.
These measurements establish no low-power performance guarantee.
See [Rev backend](rev-backend.md) for build instructions and measurement details.
Windows, GCC 15+, hardware audio and the older Linux ABI release floor were not
validated by this Rev run; macOS/Metal is not integrated.

## Dark QR startup default

The preview now starts in Dark mode before its first draw, and the dropdown reads
that initial state. Every launch uses Dark red (Dark gray in monochrome), including
with an empty message. The dark background level increased slightly from 24 to 32;
Dim remains 64. The Release GUI rebuilt, and the QR suite plus all five focused
GUI/model suites passed.

## QR preview brightness

A dropdown above the QR preview selects Normal, Dim red, Dark red, or Off.
Monochrome output uses gray dimming levels. The selection survives message edits
and affects only the preview; Normal retains the original black-on-white rendering.
The Release GUI rebuilt, and the QR suite plus all five focused GUI/model suites
passed.

An ASan/UBSan rendering harness (with `detect_leaks=0`) exercised the actual App
dropdown callbacks at minimum, default, and enlarged window sizes. On a 24-bit
display, Normal matched the previous QR rendering pixel for pixel; dimming
preserved modules and quiet-zone geometry, and empty/error states stayed dim.
Off remained entirely black while editing. The adjacent waterfall was
unchanged in both color and monochrome. The minimum-size layout was inspected
visually.

On an 8-bit display without a suitable RGB visual, the actual App rendered only
grayscale, with gray dropdown labels and working brightness callbacks. Strict
waterfall pixel comparison was limited to 24-bit displays because colormap
allocation changes quantization on the 8-bit display.

## Muted color presentation

Color output now uses subdued cyan data text and traces, softer neutral labels,
and muted waterfall hues ending in off-white. The monochrome roles and waterfall
intensity/rendering path remain the same as the pre-color commit `924bd03`.

The Release GUI rebuilt and all five focused GUI/model suites passed. Both
`--color` and `--monochrome` passed the display-free self-check. An ASan/UBSan
rendering harness (with the existing `detect_leaks=0` setting) verified 108,500
clipped waterfall pixels across 218 intensity levels against the new palette,
neutral labels capped at 208, and the uniform RGB (144, 192, 184) data tint.
Its full 760-by-540 monochrome image was pixel-identical to the previous grayscale
render. On an 8-bit display without a suitable RGB visual, a color request still
produced entirely grayscale output. The muted rendering was inspected visually.

## Multihue waterfall

The waterfall now uses blue, cyan, green, yellow, orange and red between black
and white. Only the shared 256-entry lookup table and its descriptions changed;
the existing RGB row conversion, grayscale intensities, and control interface
remain unchanged.

All five Release GUI/model suites passed. An ASan/UBSan rendering harness
verified 89,050 clipped waterfall pixels across 213 intensity levels, distinct
blue/green/red regions, unchanged grayscale pixels against the previous render,
and unchanged uniformly tinted constellation points. The multihue output was
also inspected visually.

## Optional color presentation

Color is now enabled by default when an RGB visual is available. `--monochrome`
explicitly selects grayscale; `--color` restores color preference, with the last
switch taking precedence. The existing automatic grayscale fallback is unchanged.
The default/override update rebuilt successfully and passed the five Release GUI
model/self-check suites plus command-line override acceptance checks.

Release and ASan/UBSan builds succeeded, and all five focused GUI/model suites
passed in both configurations. `--color` is documented by `--help` and accepted
alongside the display-free self-check.

An isolated rendering harness exercised the actual FLTK fields, waveform,
constellation, and waterfall widgets under ASan/UBSan with the CI leak-detection
setting. Pixel comparison verified 89,050 clipped waterfall pixels over 213
intensity levels against the shared palette, and identical constellation-point
positions with one uniform tint. The initial cyan/mint palette had nondecreasing
channels; the later multihue palette above intentionally varies hue instead.
Both presentations were visually inspected. On an isolated 8-bit display without
a suitable RGB visual, a color request produced an entirely grayscale image.
The waterfall retains its Gray8 buffer and converts color rows only when enabled.
These checks exercise rendering and GUI policy; no physical-display or Windows
performance measurements were made.

## Single-backend configuration and monochrome GUI

The GUI rebuilt in Release and ASan/UBSan configurations. All five focused GUI
model/self-check suites passed in each configuration; all four toolkit-independent
GUI suites also built and passed in a fresh CLI-only configuration.

Fresh configuration accepted the default and explicit `fltk` selection and
rejected empty, multiple, and unimplemented selections. GUI-off configuration
skipped FLTK even with an unavailable backend value. The CLI-only executable
built and ran `--help` with shared OpenSSL: this host's separate static OpenSSL
installation lacks its required zstd link dependency without the existing local
development support setup. The dependency policy was not changed.

The Release GUI workflow passed on a private 1440-by-1100 virtual display,
covering keyfile operations, transmission, exact binary reception, clipboard/save,
replay, three tabs, resizing and complete diagram scrolling. Console and modem-flow
layouts were inspected visually. Dynamic dependencies contained no GL/EGL,
GTK/GLib, Cairo, or Pango library. `--help` and `--version` reported `fltk`.

The sanitizer workflow passed its behavior checks. An initial run with leak
detection enabled reported 41,319 bytes of Fontconfig allocations at shutdown;
the model/self-check suites passed with leak detection enabled outside the
sandbox. The GUI workflow was repeated using the existing CI setting
`ASAN_OPTIONS=detect_leaks=0`, retaining address and undefined-behavior checks.
After restoring scrolling for overflowing signal text, a later sanitizer run
timed out during the file-transfer stage despite the earlier completed workflows.
An unchanged-binary retry completed with exit status zero. The cause of the
intermittent timeout was not established. The final Release model/self-check
rerun passed all five suites; those checks do not cover this workflow failure.

This validates the current FLTK implementation, not a second adapter or MCU
deployment. The controller/declaration/bitmap extraction remains proposed work.
Windows, physical audio, and representative slow hardware were not exercised.

## Version 0.7.2 static full-pattern inspection

The standalone Constellations tab is removed. Modem flow now displays all legal
full-pattern symbols as phase/amplitude chip rows, a full-vector distance map,
and matched/off-pattern energy for legal, shifted, unused and mean-noise cases.
Every code position is inspectable through pagination; metrics always use the
entire actual symbol, including repeated periods and partial chips.

The new model tests first failed for the missing implementation. They then
compared every displayed chip against the previously compiled transmitter's
analytic preview and actual PCM, before rebuilding the modem with its extracted
shared code generator. This checks that sharing the generator preserves the
existing on-air sequences. Coverage includes every 2–6-bit APSK alphabet,
fixed/tone/keyed/DSSS modes, exact time-weighted distances, truncated and repeated
periods, antipodal timing ambiguity, public keyed illustrations, 16,384-chip
codes, long integrations and 30 MHz configurations.

The strengthened real-PCM acquisition test uses a 1,024-chip keyed pattern,
4APSK, -15 dB sample SNR, erased training and a 17-sample delay that changes the
carrier reference by 90 degrees. It measures per-chip Es/N0 below -7 dB, recovers
the complete authenticated packet without Reed-Solomon, and rejects the same
capture with a wrong code and a noise-only capture through finish.
This verifies acquisition within the receiver's bounded timing bank, without
hard chip decisions or a clean chip constellation. It does not establish
arbitrary clock/frequency tracking; accelerated matched-symbol simulation is
not used as evidence of blind chip acquisition.

* All **29 native CTest suites passed** across the full run and focused GUI
  rerun. The new keyed inspection fixture initially omitted its required key;
  the corrected fixture retains the production key requirement.
* All five GUI/model suites passed in Release (0.30 seconds) and under
  ASan/UBSan (2.28 seconds). The below-chip-noise PCM regression also passed
  under ASan/UBSan.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.2.
* The Release and ASan/UBSan GUI workflows passed with three tabs, current static-pattern model
  binding, unchanged live plots, three-second packet/raw replay and complete
  scrolling at minimum and expanded window sizes.
* Native visual checks covered fixed16, tone16, keyed128, all 64 APSK values,
  last-page access for 16,384 chips and a weak-signal example with -37.8 dB
  nominal chip Es/N0 and +10 dB integrated Es/N0. All four navigation buttons
  and static redraw invariance passed. The colour scale preserves distinct
  outer amplitude rings. Strict GUI and harness warning checks passed.

No wire format, keyfile or runtime dependency changed. Physical audio devices,
battery-state comparisons and hosted Windows/CI execution were not tested.
LeakSanitizer is disabled for this host's tracing environment; ASan and UBSan
remain enabled.

## Recorded 0.7.1 pattern constellation checks

The new Constellations tab shows the existing phase/amplitude observations beside
the full analytic pattern projection in modeled noise units. The new numerical
tests first failed because the projection did not exist, then passed with the
bounded model. They cover every supported alphabet, pairwise template distances,
fixed/keyed/tone equivalence, quantized integration time, sample-clock invariance,
unsnapped observations, invalid numeric inputs, hour-long integrations and 30 MHz
plans. A deterministic 16,000-trial Monte Carlo check uses the modem's actual AWGN
generator to verify the modeled radial and differential tangential variance.

* Native GUI Release: all **28 CTest suites passed** (54.72 seconds).
* After the final layout adjustment, all four focused GUI/model suites passed
  in Release (0.26 seconds) and under ASan/UBSan (1.93 seconds).
  The complete GUI source also passed strict warning checks with `-Werror`.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.1.
* The complete Release and ASan/UBSan GUI workflows passed on a private virtual
  display, including all four tabs during
  continuous reception and three-second replay, minimum/expanded resizing,
  exact observation projection, exclusion of unmatched I/Q, retained fresh-point
  batches and clearing replay points on return to live reception.
* The plot was visually inspected at the actual minimum 479×383 panel size
  for 8- and 64-symbol alphabets, using real `add_awgn` differential observations
  at moderate SNR and ideal-only high-SNR cases. Pixel margins preserve outer
  markers and labels, and the closest-pair labels remain separate in dense views.

The axes represent the analytic AWGN model, not a measured receiver likelihood.
The differential-noise ellipses use the weakest configured reference ring;
finite real-PCM quadrature covariance, clock drift and phase noise can differ
from that approximation. No modem, packet, keyfile or replay format changed;
0.7.1 remains compatible with 0.7.0. No runtime dependency was added.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this visualization release. LeakSanitizer is disabled for this
host's tracing environment; ASan and UBSan remain enabled.

## Recorded 0.7.0 framing checks

The new tests first reproduced mandatory RS on a tiny message and receiver
synchronization before rejection of a damaged short frame. The format removes
the magic byte and fixed-width body length, uses a variable compact header,
automatically disables all RS below 16 original bytes, and carries header and
body in one continuous symbol stream. Bounded provisional decoders verify
complete short frames before synchronization; initial phase hypotheses allow
reception without relying on header RS to repair the first symbol.

* Native GUI Release: all **28 CTest suites passed** (67.93 seconds).
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (49.38 seconds).
* Final Debug ASan/UBSan with `-O1`: all **27 CTest suites passed**
  (424.14 seconds), including the instrumented vendored codec, full streaming
  acquisition, transfer, live sessions, CLI, audio contracts and GUI models.
  The initial unoptimized nine-suite sanitizer run and the separate broader
  compact-bootstrap acquisition matrix also passed.
* Short provisional-reception tests passed under ASan/UBSan: corrupted complete
  packets never establish lock, subsequent valid packets recover, noise alone
  does not synchronize, and extremely long observations remain bounded.
* The complete Release and ASan/UBSan GUI workflows passed on the private virtual display,
  including automatic FEC Off below 16 bytes, restoration of the longer-message
  FEC choice, all three tabs, pending previews and exactly three-second replay.
  The compact transmission diagram and processing flow were visually inspected.
* Native ZIP and TGZ bundles passed fresh extraction into directories containing
  spaces, manifest and dependency checks, short/long compression simulations,
  and complete GUI workflows with package-search paths cleared. Their 42 ELF
  files have no GTK/GLib dependency; the local glibc requirement is at most 2.38.

The packet tests cover original-size boundaries 0/1/15/16/255/256, canonical
variable lengths, maximum metadata, every content kind, header correction up to
the actual parity budget, and malformed frames with otherwise valid integrity.
Receiver and transfer tests span two through six bits per symbol, keyed and
plain frames, missing training, continuous partial-bit boundaries, noisy gain
hypotheses, and recovery after a corrupted short body. Live tests preserve the
fresh-symbol limit across timing/gain changes and fit hours of simulated airtime
within a 1 MiB workspace.

A deterministic same-wire oracle confirmed that the old marginal-SNR CLI fixture
contains one to three actual bit errors in its now-uncoded six-byte message.
Those packets correctly fail integrity. The lifecycle fixture now uses the
healthy 3 dBm / -90 dB preset, retaining default crystal drift and phase noise;
noisy packet rejection is tested separately. Replay-start checks permit only a
new, empty, unverified frame-zero status and still forbid cancelled content.

Profiling the same PCM cases before and after nonthrowing acquisition probes
reduced two/three/four/five/six-bit trial times from
0.415/1.038/2.115/3.547/5.082 seconds to
0.179/0.211/0.278/0.531/0.732 seconds, with identical header decisions. Fixed-size
probe caches and conservative RS shape checks avoid repeated rejection work;
these measurements are local observations, not throughput guarantees.

The unoptimized Debug sanitizer live test reached its existing 60-second
computation deadline midway through multi-key reception. Sanitizer compilation
now uses `-O1`, retaining debug symbols, assertions, ASan/UBSan and frame pointers.
This also covers the already-instrumented vendored codec and standalone audio
tests. Normal Release, portable and MSVC compilation flags are unaffected.
The final full live suite passes with its original deadlines and assertions.

Both peers must use 0.7.0 framing. Raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.6.0 checks

* All **28 native/core CTest suites passed** across the full run and focused CLI
  rerun. The original CLI fixture assumed 65 KiB of repetition could not fit the
  repeat airtime policy; it now checks both compressed acceptance and explicit
  uncompressed rejection. The final CLI suite passed in 20.50 seconds.
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (37.70 seconds).
* All **nine focused ASan/UBSan suites passed** (254.87 seconds): packet,
  compact format, both compression codecs, transfer, inspection, plots, GUI
  policy and GUI self-check. The vendored liblzma C code was instrumented too.
* The compact-bootstrap acquisition suite passed under ASan/UBSan, covering
  two-ring gain aliases, noisy one-byte packets, supported constellation widths,
  missing-ring hypotheses, exact symbol padding and PCM boundaries.
* The complete GUI workflow passed in Release and ASan/UBSan on the isolated
  display. It covers text/file/raw reception, key generation, pending results,
  three-second replay, clipboard, file saves, tab switching and diagram layout.
  The final inspection also requires LZMA2 for its long text example. The flow
  and compact-bootstrap transmission views were visually inspected.

New tests first demonstrated the 72-byte bootstrap and short-window two-ring
acquisition failures, then passed with the 16-byte format and alternate gain
hypotheses. Packet tests cover four damaged header bytes, actual compression,
incompressible fallback, bounded original sizes, malformed lengths with valid
CRC/RS/SHA, and no transmitted dictionary identifier. Short-code tests cover
all bytes, canonical tokens/padding and bounded truncated previews. LZMA2 tests
include an independently generated `xz --format=raw --lzma2=preset=9e,dict=4KiB`
vector, strict stream endings, invalid controls, expansion limits, previews and
an enforcing scratch allocator. Vendored source hashes are checked by CTest.

The new compression dependency is pinned source compiled statically. No Python
or destination compression package is needed. Packet wire compatibility with
0.5.x is deliberately removed; raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.5.7 checks

* Native GUI Release: all **24 CTest suites passed** (28.92 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.86 seconds).
* All six focused ASan/UBSan suites passed: packet, transfer, inspection model,
  GUI plots, GUI policy and GUI self-check (128.84 seconds). LeakSanitizer was
  disabled for this host's tracing environment.
* The four Release GUI suites passed again after the final receiver-description
  correction. The complete GUI workflow passed in both Release and ASan/UBSan
  on the isolated virtual display, using simulated input.
* GUI workflow checks cover all three tabs during idle reception and replay,
  asynchronous RS20/RS60 and raw/packet model changes, invalid-input clearing,
  retained composition and receiver state, 1030×786 and 1400×1000 window sizes,
  and scrolling each diagram to its end and back. Packet and raw reception,
  clipboard handling, file saves and three-second replay remain covered.

The inspection tests were written before the model implementation. They verify
encoder-derived compression, keyed integrity, mandatory bootstrap parity with
body FEC Off, independent five-bit symbol padding, full and shortened RS20/RS60
blocks, physical byte totals, and omission of actual payload, metadata values
and key material. Receiver steps distinguish incoming-header selection from
outgoing settings. Raw diagrams add no packet overhead; hour-long and 30 MHz
plans remain bounded without generating waveforms. Strict C++20 warning checks
passed for the model and native diagram code.

The flow, transmission sequence and chosen constellation panels were visually
inspected in the running GUI. Packet, keyfile and waveform formats are unchanged.
No Python, GUI toolkit or other runtime dependency was added. Physical audio,
battery comparisons and hosted Windows/CI execution were not tested for this
GUI inspection release.

## Recorded 0.5.6 checks

* Native GUI Release: all **23 CTest suites passed** (28.21 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.58 seconds).
* New modem/transfer raw-reception cases passed under ASan/UBSan, including
  partial symbols, measured constellation points and arbitrary-bit-offset
  encryption. LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions (91.30 seconds), GUI
  plots, GUI policy and GUI self-check. The final GUI suites also passed in Release.
* The new end-to-end requirement first failed against the unchanged 0.5.5
  library: raw simulation produced no received bits at the presentation deadline.
  The corrected Release GUI workflow recovered encrypted `001`, displayed the
  pending and completed raw rows, and copied exactly those bits.
* The final GUI workflow also passed under ASan/UBSan, including a visible
  read-only FEC Off value in Binary mode and restoration of the saved packet
  setting. The completed `001` row and controls were visually inspected on the
  private virtual display, without using physical audio or the desktop clipboard.

Receiver tests span all supported constellation widths and one through seventeen
bits, preserve leading zeros, and check that altered/noisy observations change
decisions. Live tests cover pending-to-complete presentation at exactly 3000 ms,
no early computation results, keyed partial symbols with packet RS controls
selected but no on-air overhead, no fabricated packet/accuracy metadata,
cancellation, replacement, and an hours-long symbol within a 1 MiB DSP budget.

Raw simulation supplies nominal start timing, length and a carrier reference;
it does not implement blind real-audio raw discovery. Received raw bits have no
FEC or integrity check and can contain channel errors. Packet and raw transmitted
waveforms remain compatible with 0.5.5. Physical audio, battery comparisons and
hosted Windows/CI execution were not tested for this release.

## Recorded 0.5.5 checks

* Native GUI Release: all **23 CTest suites passed** (28.35 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.62 seconds).
* Focused raw streaming-modem and transfer tests passed under ASan/UBSan.
  LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 96.34 seconds.
* Native Release GUI workflow passed on the isolated virtual display, including
  exact encrypted three-bit transmission, explicit source selection and return
  to live plots. No physical audio device was used.
* The complete GUI workflow also passed under ASan/UBSan. The new binary editor,
  three-bit count and millisecond airtime were visually inspected on that display.

Physical audio, battery comparisons and hosted Windows/CI execution were not
tested for this release. Existing packet waveforms and keyfile formats are
unchanged; the GUI raw binary format is separate from legacy CLI DBPSK status.

## Binary transmission coverage

New regressions check leading-zero binary input, invalid input and explicit
source selection; exact unframed airtime for one through seventeen bits across
all supported constellation widths; partial final symbols that vary both phase
and amplitude; PCM/integrated agreement; and stream-key masking without extra
bits. The GUI workflow includes selected-key three-bit transmission beside a
retained message and attachment, with irrelevant packet controls disabled.

Live-session tests exercise the actual playback branch through a link-time audio
adapter, check its exact sample count, and simulate a one-symbol raw signal with
an injected presentation clock. They cover the three-second deadline, measured
input plots, no invented packet results, invalid-input nonmutation, cancellation,
return to live noise, and an hours-long symbol with a 1 MiB DSP workspace.

## Recorded 0.5.4 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 92.63 seconds. LeakSanitizer was
  disabled for this host's tracing environment.
* The new GUI workflow reproduced early verified reception against the previous
  backend, then passed with the timed event implementation. Text and file rows
  remain pending across earlier GUI polls; verified content and accuracy appear
  only when the three-second presentation completes.
* The workflow also replaces a replay after pending reception appears, cancels
  its replacement, and checks through the original deadlines that neither
  interrupted packet enters the inbox. A running pending row was visually
  inspected on the private Xvfb display.
* The complete timed GUI workflow also passed with ASan/UBSan. GUI checks used
  simulated input on the isolated display, with no physical audio device.

Deterministic presentation-clock tests cover the entire fixed-training/packet
timeline, no early browser or receipt events during computation, incremental
pending text, paired final signal and packet delivery at exactly 3,000 ms, and
duplicate-free snapshot reads. Delayed polling flushes due events in order
without extending the deadline. Two queued simulations each receive their own
three-second timeline. Cancellation, replacement and configuration discard
future results, including pending events made due before the next GUI poll.
A cancellation after the deadline preserves an already completed reception.

CPU cancellation is exercised during actual streaming progress. Failed decoding
never produces verified content. The hours-long-symbol simulation still uses
bounded CPU work and fits the 1 MiB DSP fixture, with fewer presentation frames
to accommodate preview text and result diagnostics. Packet content uses the
separate receive-content quota. Physical audio, battery comparisons and hosted
Windows/CI execution were not tested for this release.

## Recorded 0.5.3 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All five focused ASan/UBSan suites passed: packet, live sessions, GUI plots,
  GUI policy and GUI self-check. The Debug live suite took 77.75 seconds.
  LeakSanitizer was disabled for this host's tracing environment.
* The native Release GUI workflow passed on an isolated Xvfb display. Both text
  and file rows carried reception percentages, with exact data counters matching
  their verified packets. The labels were also visually inspected in the running
  window alongside the scrolling messages.
* The instrumented native GUI workflow also passed with ASan/UBSan. All GUI
  workflows used the isolated display and simulated input, without opening a
  physical audio device.
* The full standalone streaming suite passed. Targeted preamble cases passed
  under ASan, UBSan and float-cast-overflow instrumentation. All linked core
  translation units used the current diagnostics layout.
* Exact packet-bit accuracy tests passed in Release and ASan/UBSan, covering
  no-FEC packets, header/parity-only repairs, known bit flips in full and ragged
  interleaved blocks, compressed content, authenticated ciphertext corruption,
  and exclusion of trailing bytes. Failed validation never supplies accuracy.

Preamble fixtures cover clean training, half replaced by silence, missing training
replaced by silence or noise, 18 dB noise, capture beginning halfway through
training, leading silence, PCM/integrated agreement, and training retained across
a bootstrap using hour-long symbols. Coarse observations remain unknown. Very
large finite inputs exercise the numeric overflow guards. Every valid fixture
also checks that blind acquisition still returns the correct packet.

The new independent scanner has **11,520 bytes** of fixed storage per receiver
on this x86_64 build. Receiver admission and reported workspace include it.
Neither scanner storage nor runtime on a coarse integrated observation scales
with an hour-long symbol. Recognition remains a conservative diagnostic with
documented phase/amplitude thresholds, not a calibrated radio sensitivity test.

After the test workers finished, the generated-noise benchmark with thirteen
keyed epochs, three bits per symbol and a 6 kHz internal clock processed 2.66 times
real time. The preceding 0.5.2 comparison processed 2.32 times real time. These
short measurements on a shared host do not establish a speed improvement or
real-time guarantees for other profiles and machines.

Packet, keyfile and waveform formats remain compatible with 0.5.2. Physical audio,
battery comparisons and hosted Windows/CI execution have not been tested here.

## Recorded 0.5.2 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* Final live-session and GUI projection/policy/self-check suites passed again
  after adding slow-symbol labels and the cancelled-transmission serial guard.
* ASan/UBSan passed the streaming-modem suite with the new symbol-drain API,
  and all four final live/GUI suites. The standalone streaming translation unit
  used `-O1` and linked the instrumented core library; the final live suite used
  the normal Debug build and took 72.93 seconds. LeakSanitizer is disabled on
  this ptrace host.
* The native GUI workflow passed in Release and ASan/UBSan: production keyfile
  generation, text/file reception, clipboard and exclusive saves, chronological
  replay, replacement by a new transmission, Stop replay, and return to live
  waveform, waterfall and input constellation.

A separate injected monotonic presentation clock tests every 50 ms frame of a
60-frame replay and its exact three-second deadline. Repeated reads retain the
same frame. The tests verify early payload acquisition, fresh measured symbol
batches after lock, changing waveform/spectrum, and an end frame containing the
transmission rather than decoder-tail noise. A GUI that skips frames receives
their pending compatible symbol points together. A GUI that misses the deadline
returns directly to live input and reports the unseen points as omitted.

The new symbol-drain tests cover rotated received signals, unsnapped off-grid
measurements, differential coordinates, acquisition, repeated drains, ring
overflow counts, resets, and agreement between PCM and integrated transmission.
The live audio adapter checks empty intervals between slow symbols and that
cancelled TX points cannot return after live input resumes. Long simulated
symbols remain CPU-bounded and fit their existing 1 MiB test budget, reducing
replay capacity when necessary.

Packet/keyfile formats and transmitted waveforms remain compatible with 0.5.1.
These presentation changes do not add carrier tracking or remove the recorded
CPU limits below. Physical audio and battery-state comparisons have not been
performed for this release. Hosted Windows/CI execution remains unverified here.

## Recorded 0.5.1 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* All **22 ASan/UBSan CTest suites passed**, including the separate dense PCM
  boundary stress suite (302.93 seconds in Debug). LeakSanitizer is disabled
  because this host's ptrace environment prevents it from starting.
* The native GUI workflow passed in both Release and ASan/UBSan builds on a
  private Xvfb display. It covered production keyfile generation and selection,
  overwrite refusal, idle input, verified text/file reception, clipboard copying,
  exclusive saves, held simulation diagnostics and all plots returning live.

## Evidence for the 0.5.1 changes

Automatic audio uses `carrier = max(1500, 0.75 * bandwidth)` Hz and
`Fs = max(6000, ceil(4 * bandwidth))`. Nominal symbol timing still depends on
bandwidth; hardware clocks remain separately negotiated. Tests cover planning
from 1 Hz through 30 MHz, fractional bandwidths, manual CLI overrides and actual
PCM packets at the new carrier/rate combinations. A 100 Hz packet also crosses
four cascaded 300 Hz high-pass sections and separate 48/44.1 kHz conversions.

The received-tone regression exercises the actual capture converter at both
44.1 and 48 kHz with irregular input blocks. It checks sample values and fitted
I/Q against a known 1573 Hz tone at a 6 kHz logical rate, within 1e-5. The old
fractional-cycle I/Q fit fails this test. PCM acquisition now accumulates the
carrier projections over each candidate's exact chip and symbol boundaries,
then solves their Gram system. Dense 4/5/6-bit tone and pattern fixtures inspect
wire bytes before packet error correction, including fractional-cycle chips and
delayed, fragmented input. This focused fixture validates against the known
bootstrap to separate integration accuracy from blind-acquisition startup
aliases; normal transfer/live tests retain the real protected-bootstrap search.
Undelayed tones compare every bit. Delayed/patterned cases exempt only the first
symbol's unknown differential phase; its amplitude and all following bits must
match before FEC.
End-of-capture flushing remains bounded for hour-long
symbols. Simulation receivers reset before switching back to idle PCM.

The waveform now defaults to four carrier cycles, using captured guard samples
and a bounded 64-tap Blackman-windowed sinc for the line between measured sample
dots. Tests cover exact sample knots, DC gain, impulse response, linearity,
allocation bounds, and reconstruction error below 1e-4 through 0.4 times the
sample rate. The production-size plot was visually inspected on an isolated
display. Dense overviews continue to show raw extrema.

These are generated-signal, converter and software-driver tests. A physical
audio link and an AC-versus-battery comparison have not been measured here.

With other test/build workers stopped, the 6 kHz receiver bank with thirteen
keyed epochs processed generated noise at 2.94x real time for the automatic
three-bit profile and 8.33x for six bits. A continuous 1500 Hz carrier at amplitude
0.35 measured 8.33x and 0.68x respectively. The dense six-bit carrier case is a
known CPU limit: its ambiguous amplitude lattice keeps blind bootstrap searches
busy. These single-host measurements do not establish battery-state performance
or real-time operation for every signal/key-bank configuration.

## Recorded 0.5 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* Standalone GUI plot-projection tests and the actual-transmitter signal-view
  regression passed. Release whitening, FEC and transmit-history tests passed
  as part of the integrated suites.
* All eight focused AddressSanitizer/UndefinedBehaviorSanitizer suites passed:
  transfer, streaming modem, live sessions, signal view, GUI plot projection,
  GUI state policy, GUI self-check and the CLI integration suite. LeakSanitizer
  is disabled because this host's ptrace environment prevents it from starting.
* The Release GUI workflow generated and loaded a production 128 MiB keyfile
  with named keys while reception continued. It checked every selected
  key against its matching MAC, including names containing menu punctuation
  and a key named `None`, then completed text/file loopback, clipboard copying,
  exclusive saves, held simulation plots and live resumption. Final GUI policy
  and self-check suites passed again after the menu fix.
* The final GUI workflow also passed under ASan/UBSan. Its focused fixture uses
  two generated keys (`A|B` and `None`) and one admitted epoch; all ten menu-name
  edge cases remain in the real FLTK self-check. Drift-window acquisition is
  covered by the separate transfer/live sanitizer suites. The broader initial
  GUI fixture passed in Release but exceeded its sanitizer deadlines. The GUI
  test supports `--smoke-timeout 300`, also configured for CI.
* The local installed bundle passed an audit of all 42 ELF paths: no GTK/GLib
  dependency and a maximum required glibc version of 2.38.

Final TGZ and ZIP artifacts use `tools/verify-native-archives.cmake` to check
extracted inventories, native dependencies, isolated CLI/GUI commands and ABI
requirements. Historical measurements below apply to 0.4.

## Evidence for the 0.5 changes

The default 4.8 kHz internal clock and 900 Hz carrier produce **384 cycles in a
2,048-sample frame**. Drawing that entire frame into roughly 300 pixels aliases
a clean sine wave into apparent blocks. A numeric probe found maximum error of
3.3e-9 against the expected sampled sine, 2.1e-13 between contiguous and fragmented
TX reads, 8.9e-8 after conversion through a 48 kHz audio clock, and 6.4e-14 between
ideal accelerated preview and actual PCM. The carrier FFT level matched its
expected amplitude. The regression checks continuous frame phase, primary FFT
power and suppression of aliased square-wave harmonics by more than 70 dB.

The 0.5 waveform view defaulted to twelve carrier cycles, retained actual sample
values, and uses extrema when zoomed out. Waterfall tests check peak preservation,
a shared color scale, bounded history and clearing on frequency-axis changes.
Independent enumeration confirmed that all 1,025 original FFT-bin positions map
to the same display columns in live and compact review paths.

Public audio whitening reduces data-dependent constellation bias. In a structured
16APSK probe, empirical symbol-occupancy entropy increased from **3.640 to 3.956
bits/symbol**; its maximum is 4. A 64APSK regression with a 4 KiB zero-filled
payload visits every symbol and exceeds **5.97 bits/symbol**, for both keyed and
plain audio. These are finite-frame occupancy measurements, not added payload
entropy or a capacity measurement. Tests also cover the fixed protocol vector,
chunk/offset invariance, reversibility, unchanged training and raw packet formats,
and FEC correction/authentication across all five constellation sizes.

Transmit diagnostics retain a bounded chronological payload-symbol history.
Tests exercise ring overwrite, exclude training, compare PCM and accelerated
histories, and verify that partial observations of hour-long symbols do not
create duplicate points.

Audio peers require matching **0.5 modem settings**. Packet and keyfile formats
remain unchanged. Whitening is public and reversible; it does not conceal
repeated frames or guarantee low probability of intercept. Rectangular pulse
sidelobes remain. See [modem.md](modem.md) and [protocol.md](protocol.md).

## Recorded 0.4 baseline

These measurements were made on Linux x86_64 with GCC 14.2, CMake 3.31,
OpenSSL 3.5.7 and vendored FLTK 1.4.5. Version 0.4 passed 22 native Release,
19 Python-disabled CLI and 21 ASan/UBSan suites across integrated and focused
runs. The final live sanitizer suite took 97.27 seconds. LeakSanitizer was disabled
because this host's ptrace environment prevented startup.

The retained regression coverage includes 1 Hz–30 MHz bandwidth planning;
`max(64, ceil(4 * bandwidth))` internal clocks; bounded multistage resampling;
independent 48/44.1 kHz packet paths; default 100 ppm crystal error and 0.5 degrees
RMS phase diffusion per square root second; erased-training acquisition; adaptive
4/8/16/32/64-APSK; and exact byte, bootstrap and FEC boundaries. Conversion from
120 MHz to 64 Hz stays below 5 MiB. An ideal-clock packet with hour-long symbols
decodes, while its bad-crystal counterpart fails validation rather than assuming
impossible carrier coherence.

Continuous tests cover idle plots, authentication, multiple keys/epochs,
cancellation, consecutive messages and the two-second simulation review. All
three plots return to live reception afterward. A clock that jumps an hour per
query verifies that simulation preserves its admitted epochs through preparation
and decoding, then admits fresh epochs for the next burst. ALSA/WinMM fixtures
cover default discovery, partial I/O, rate conversion, cancellation and buffer
lifetimes. Cryptographic vectors, production keyfiles, QR, Unicode, content
bounds and exclusive saves remain covered.

The 0.4 native GUI workflow passed normally and under ASan/UBSan: provisional to
verified text, consecutive transmissions, exact UTF-8 clipboard copy, files-only
save listing, exclusive binary save, cache clearing and held/live plots. Its
layout was inspected on an isolated 1400×1100 display. This did not exercise a
physical audio link or compare AC and battery power states.

### 0.4 audio configuration and CPU measurements

A zero-PCM default-device probe negotiated these formats:

| Logical rate | Hardware rate | Converter passband | Playback workspace |
| ---: | ---: | ---: | ---: |
| 64 Hz | 48 kHz | 26.88 Hz | 118,348 B |
| 4.8 kHz | 48 kHz | 2,016 Hz | 119,296 B |
| 9.6 kHz | 48 kHz | 4,032 Hz | 120,256 B |
| 96 kHz | 48 kHz | 20,160 Hz | 233,792 B |

No sound was emitted and nothing was recorded. Passbands are converter
calculations, not measured analog response. Unsupported bands are rejected
before expensive audio processing; a high internal rate does not create an SDR
frontend or overcome a sound card's physical passband.

One synthetic 48 kHz → 4.8 kHz conversion processed one second in 15.42 ms,
with 647,624 bytes of workspace and 5.81 ms setup. Eight input chunks produced
exactly 4,800 samples. A separate generated-noise receiver benchmark measured
7.77× real time for automatic three-bit modulation and 13.74× for forced six-bit
modulation, using a 4.8 kHz clock and thirteen keyed epochs. Other build/test
workers were stopped. These single-host CPU measurements are not end-to-end
audio, battery-state or cross-machine performance guarantees.

### 0.4 delivery baseline and continuing limits

Relocation checks hide the original installation and exercise a copy in a path
with spaces, empty `PATH`/`LD_LIBRARY_PATH`, and invalid Python paths. They verify
dependency closure and reject modified or unrecorded files. Archive/ABI fixtures
cover TGZ/ZIP extraction, checksums, accidental GTK/GLib linkage and excessive
glibc requirements. The 0.4 package audit covered 42 ELF paths, found no GTK/GLib
dependency, and measured a maximum glibc requirement of 2.38.

GitHub Actions jobs have not run here. Windows binaries and the configured glibc
2.35 compatibility floor require a successful hosted workflow run. Local builds
on this newer host do not inherit that compatibility floor.

The accelerated channel remains a bounded matched-chip model. It has no
chip-clock recovery or carrier-tracking loop and does not demonstrate calibrated
sensitivity, arbitrary long encrypted-pattern acquisition, near-capacity
throughput, physical SDR operation, low probability of intercept, or Windows
driver reliability. See [offline-installation.md](offline-installation.md) for
bundle compatibility and copying requirements.

### Unsynchronized sampled simulation (2026-09-13)

Production simulation now sends receiver-clock PCM to the same blind acquisition
path as hardware audio. Earlier matched-observation sensitivity and constant-work
simulation measurements above describe the previous model. The matched-channel
API remains a low-level analytical test helper, outside the simulation transport.

The new channel tests verify arbitrary seeded carrier phase and fractional start
timing, positive and negative clock error, sample-identical results with one-sample
and 4096-sample reads, continuous oscillator/noise state during idle and later
bursts, and equivalence between analytic source PCM and hardware transmitter PCM
through fixed training and keyed spreading. Memory is bounded; CPU work now scales
with sample count. Long-symbol cases exercise cancellation instead of asserting
instant completion of hours of audio.

Transfer tests exercise independent receive epochs inside and outside the search
window, actual spreading correlation at weak sample SNR, and failed acquisition
under carrier incoherence. Live tests cover reception after idle noise and across
consecutive transmissions without a TX-triggered receiver reset. Raw-waveform
replays produce no timing/length-assisted received bits; blind raw discovery
remains unavailable. Very short packets may verify without a provisional browser
row, because transmission alone no longer creates a receiving event.

Release channel, modem, streaming-modem, transfer, regression and live tests
passed, as did the 22 CLI tests, GUI inspection/controller checks, controller
smoke workflow and native GUI self-check. These checks do not establish physical
hardware sensitivity, continuous clock tracking, fading or multipath performance.

AddressSanitizer and UndefinedBehaviorSanitizer checks passed for the final
channel suite and all live cases (the full-run prefix plus focused epoch and
remaining-case runs). LeakSanitizer is unavailable under this host's ptrace
sandbox, so those checks used `detect_leaks=0`. Automatic-epoch and idle-refresh
fixtures now use controlled local clocks: they test timestamp selection and an
explicit idle epoch advance without making sanitizer CPU throughput determine
key admission. Separate tests still require mismatched/out-of-window epochs to
fail. Production clock admission and retention are unchanged by these test fixes.

### GUI rate and carrier defaults (2026-09-14)

The GUI now defaults to Rate 3,600 Hz and Carrier 1,500 Hz. Carrier selection
participates in both transmit and receive planning before the existing pattern
confidence floor is evaluated. Rate changes restore that rate's recommended
carrier; other edits preserve an explicit choice. CLI defaults, cipher streams,
chip mapping, pulse shaping and acquisition thresholds remain unchanged.

The Release build passed all 53 non-interactive suites, including crypto/key
vectors, pattern acquisition, live transfer, CLI and GUI controller coverage.
New packet fixtures recover exact public and authenticated private payloads at
40 and 80 dB-Hz with unknown carrier phase/fractional start, an independently
offset receive epoch, ±100 ppm clock error and phase diffusion. The 80 dB-Hz
cases exercise the actual default 16-chip profile. New public/private PCM
round trips also pass through independent 44.1/48 kHz audio-card clocks.

Both FLTK and Rev passed the full simulated GUI workflow and native adapter
checks. FLTK document checks and Rev platform/clipboard plus 1x/2x coordinate
checks passed. The final Rate/Carrier row was visually inspected at 1180×866
and 1030×786 in both backends, with the shared layout/application checks rerun
after reserving full label widths. FLTK's 2x bitmap probe required an Xft-enabled
test build; the initial build without Xft could not apply the requested scale.

At equal received C/N0, translating the same 3,600 Hz rate waveform from a
2,700 Hz carrier to 1,500 Hz produced aggregate pattern-evidence ratios of
0.9911 public and 1.0206 private across the tested starts and ±100 ppm clocks.
These software-channel checks preserve the existing planner's confidence
expectations; they do not measure a physical radio/speaker passband or establish
field sensitivity, BER or probability of intercept.

### Shannon-Hartley capacity display (2026-09-15)

Shared GUI diagnostics now place the ideal Shannon-Hartley channel capacity
beside the gross modem bitrate. The calculation converts the accepted TX C/N0
in dB-Hz to linear SNR in the selected nominal bandwidth. CLI `estimate` exposes
the same calculation as `shannon_capacity_bps`. Very weak SNR uses `log1p` to
preserve precision, and unrepresentable capacity displays as `Unavailable`
(JSON `null`). Capacity is informational; modem planning, framing, payload
airtime and physical completion are unchanged.

The Release build and all 15 selected suites passed: `tuning`, `cli`,
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_contract` and `gui_layout`.
Independent numerical cases cover C/N0 conversion, bandwidth changes, weak and
strong targets, and numeric overflow. Shared GUI checks cover setting updates,
RX/simulation independence and preservation of the last accepted value during
invalid edits. The 3,600 Hz / 60 dB-Hz CLI estimate reports 29,242.698314280708
bit/s while short text `e` retains its three transmitted bits.

This was a headless run; native window rendering and physical-link throughput
were not measured. Both adapters consume the shared diagnostic presentation.

### Pattern evidence reset and expiry (2026-09-15)

Clicking the Console Pattern evidence plot now clears retained observations.
Stable observation IDs prevent later polls or replay frames from restoring
cleared evidence; new observations can still appear with identical scores.
Each point expires when its presentation age exceeds six seconds, including
during idle input. Evidence receives its timestamp after the complete pattern
window is scored. Expired strong candidates cannot hide fresh evidence from
another receiver. Replay preserves IDs and uses first scheduled frame times.
This diagnostic bookkeeping is bounded and does not alter modem acquisition,
wire formats, physical completion or pending reception updates.

The Release build and all 20 selected suites passed: `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `live`, `live_resources`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_plots`, `gui_bitmaps`,
`gui_interactions`, `gui_contract`, `gui_adapter_boundary`,
`gui_boundary_regression` and `gui_bindings`. Deterministic new cases cover
the exact six-second boundary, separate observation ages, repeated polls,
clear persistence, fresh identical scores, completed long-symbol evidence,
receiver selection, replay timestamp preservation and bounded history.

Validation was headless; native pointer/rendering checks and physical audio
were not run. Both adapters use the existing shared bitmap click mechanism.

### Threshold-relative pattern evidence scale (2026-09-15)

The Console Pattern evidence plot now uses exponential axes relative to each
observation's actual single-symbol admission threshold. The threshold occupies
half of each axis with solid guides; twice-threshold occupies three quarters
with dashed guides. Strong outliers approach the outer edges without shrinking
this region. Both receiver engines attach the reference to diagnostic history;
live snapshots and replay retain it with the original observation identity and
age. Acquisition decisions, physical completion and pending-bit updates are
unchanged. Chain evidence and competing-pattern margins still participate in
admission, so the guides alone do not imply acceptance.

The Release build and all 18 selected suites passed: `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `live`, `live_resources`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_plots`, `gui_bitmaps`, `gui_contract`,
`gui_adapter_boundary` and `gui_boundary_regression`. Checks cover independent
threshold/twice-threshold pixel landmarks, per-candidate thresholds, outlier
stability, invalid and extreme numeric inputs, color/monochrome, sample aspect,
tiled repaint, reference capture, replay, expiry and clearing. A 241-by-221
pixel output from the actual shared renderer was also visually inspected.

Validation was headless; native windows and physical audio were not exercised.
Both GUI adapters consume the same shared renderer and captions.

### Log-evidence scale correction and sampled clicks (2026-09-15)

The initial threshold-relative scale above incorrectly divided scores that
were already logarithms. The display now derives relative inverse model
noise-tail evidence as `exp(score - threshold)` before compressing it onto the
axis. The threshold remains at 50%; the 75% guide now correctly means twice
its evidence (`threshold + ln(2)`), rather than twice its log score. Receiver
scoring, admission, observation retention and physical completion are unchanged.

A sampled damped carrier click in the streaming correlator produced score
18.2393 against threshold 25.5108, with no accepted bits. The initial display
placed this at 39.08% of the axis, despite only 0.000695 times the threshold's
inverse model noise-tail evidence. It now renders at the origin at the tested
pixel resolution. The FFT receiver fixture also retains weak click evidence
without accepting bits. Both fixtures separately recover the exact legal
payload `01`, whose evidence remains visibly separated from the clicks.

The Release build and the same 18 suites listed above passed. `gui_bitmaps`
now tests actual sampled clicks and valid patterns through both receiver
implementations and the shared renderer, along with additive log-evidence
landmarks, a ten-log-unit deficit, outlier stability and numerical extremes.
Validation was headless and does not calibrate physical click recordings or
establish universal impulse rejection. These remain model evidence scores;
competing-pattern margins and chain history also affect actual bit admission.

### Visible noise and signal score distributions (2026-09-15)

The preceding exponential relative-evidence transform is superseded: its
saturation collapsed ordinary noise and strong signals onto the origin and
outer endpoints. The plot now preserves native log-score variation below the
threshold and uses logarithmic interpolation above it. The threshold remains
at 50%, twice the log score at 75%, and stronger retained points share an
adaptive upper range with 5% headroom. Only scores above `2T` change position
when the upper range changes. Labels explicitly identify diagnostic log
scores; `2T` does not mean twice the probability or evidence. Receiver decisions,
framing, physical completion, pending bits and display retention are unchanged.

The Release build and all 18 suites listed above passed. The final expanded
`gui_bitmaps` suite also passed after strengthening its cloud-spread assertions.
Sampled Gaussian noise and repeated damped clicks remain unadmitted and occupy
separated interior positions below the reference. Twelve-bit signals decode
exactly at three noise levels in both receiver implementations; their retained
candidates span multiple separated display positions instead of three saturated
points. Independent synthetic cases cover zero, `T`, `2T`, multiple strong
scores, a shared upper range, outliers, invalid metadata and extreme finite
values. Existing monochrome, color, aspect and tiled repaint checks remain.

Actual shared-renderer output was visually inspected at 241-by-221 pixels for
noise/clicks, signals at varied noise levels and combined clouds from both
receivers. These are headless sampled fixtures, not native windows or physical
audio measurements.

### Recovery after late marker acquisition (2026-09-16)

An operator's live-audio RS60 capture contained 1,067 bits: the exact final
44 bits of the first alignment marker and 1,023 coded bits. It lacked 148
leading marker bits and the last parity bit. Independent RS60 checking found
no errors in the observed coded bits and restored the final bit to one.
Post-end raw LZMA2 decoding recovered the exact 55-byte source
`the quick brown fox jumps over the lazy dog lorem ipsum`.

A receiver probe compiled against the prior `transfer.cpp` left this exact
capture undecoded even after a supplied physical-end event. The same probe
against the updated implementation recovered the source. The initial bounded
fallback required a unique short marker suffix backed by sufficient fixed RS
evidence, after physical completion only; the broader search below supersedes
that suffix requirement. It preserves the configured source,
FEC and key settings, actual symbol addresses, diagnostic bit prefix, storage
quotas and ordinary marker detector. The protocol documents the combined
false-match bound; no source-format success supplies alignment evidence.

The independent capture regression checks every pending bit and its stable
identity, the exact recovered source and final-parity repair statistics.
Additional cases cover known data/parity errors, insufficient parity evidence,
timed unknown slots, unmarked input, incompatible local profiles and keyed
authentication at the correct and incorrect acquired coordinates. An
18-byte-error case is accepted; a 19-byte-error case remains algebraically
correctable but fails the stricter alignment evidence requirement.

The Release build and all 18 selected suites passed: `live_profiles`,
`live_receptions`, `live`, `live_resources`, `compression_short`, `transfer`,
`stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `gui_application`, `gui_controller`, `gui_inspection`,
`gui_binary_editor`, `cli`, `boundary_sync` and `boundary_marker_storage`.
The final expanded `stream_receive` suite also passed independently.

Validation uses the supplied received bits and generated fixtures. The original
audio recording was unavailable, so these checks do not establish why physical
acquisition missed the leading marker or reproduce a new hardware audio test.

### Comprehensive RS-assisted alignment and marker benefit (2026-09-16)

The post-end fallback now tests every leading coded start allowed by one marker
plus the seven-bit slip neighborhood, including an entirely absent marker.
Matching marker suffix bits add evidence without gating RS attempts. A unique
winner must meet the 144-bit candidate threshold, including correction and
erasure penalties; 38,600 charged hypotheses retain the fallback's below
`2^-128` random-input bound. Later retained starts can veto a winner but cannot
be accepted, preventing the search boundary from hiding shifted codewords.
The accepted decoded interval enters the existing statistics/quota/spool path
without a second RS decode. Physical completion and source interpretation
remain separate gates, and no transmitted bits were added or removed.

Generated variants recover the original capture with all 44 surviving marker
bits removed, a damaged marker tail, and a completely damaged 192-bit marker
at all eight supported leading bit phases. Tests reject ambiguous all-zero
words, including a 1,223-bit case where only one candidate falls inside the
accepted-start range, and preserve correct keyed addresses, pending prefixes,
unknown-slot rejection, local source settings and the capture-size bound.

Paired checks establish why the transmitted marker remains useful. With all
48 parity bytes absent but all 80 source-area bytes observed, the full marker
still permits recovery of the exact 55-byte text. Removing the marker leaves
RS with no parity evidence to establish alignment. A separate paired 18-error
case also passes with its 44 marker bits and fails without them under the
current conservative evidence calculation; that precise error cutoff is not
an information-theoretic RS limit.

Temporary Release probes performed five end-to-end receiver calls per variant.
The original, markerless and 18-error captures averaged approximately 8–10 ms
per call on this host. Both intact-codeword variants decoded in every call;
the 18-error variant decoded only with its marker evidence. The all-parity-
absent pair likewise decoded only with its full marker. These timings include
post-end source handling and are illustrative host measurements, not hardware
audio performance claims.

The Release build and all 18 compatibility suites listed in the preceding
entry passed with the comprehensive search and the final paired marker-benefit
regressions. Validation was headless; no new physical audio test was performed.

### Separate short and long GUI SNR targets (2026-09-16)

The shared GUI now has editable short and long target dropdowns, defaulting to
32 and 55 dB-Hz. Short text remains 1–16 source bytes inclusive; exact raw bits
use that same target. Longer text, empty byte sources and every attachment use
the long target. RX starts with both targets. Draft transitions update estimates,
inspection and diagnostics without replacing the running receiver bank.
Actual transmission selects its locally configured waveform before generation;
framing, short dictionary endpoints and observed-absence completion are unchanged.

The Release build and all 37 selected compatibility/shared GUI suites passed,
including every suite in the development contract. The final controller run also
passed added UTF-8, escaped-byte, incomplete-draft, raw-bit, 16/17-byte and tiny
attachment cases. A sampled live regression sends both profiles sequentially
through one independent receiver bank and checks exact wire bits, airtime,
decoded content and the uninterrupted sample clock. Final layout, application,
contract and adapter-boundary checks passed after presentation adjustments.

FLTK adapter/document conformance and Rev adapter/platform/1x/2x coordinate
conformance passed on private X displays. Rev screenshots at default and minimum
sizes verified complete target labels and dropdown buttons. The shared layout
adds a settings row while preserving the composition/history/plot areas; the
dictionary reference wraps into shorter rows. Rev's Clang build also required
moving an existing deduced-return helper above its first use, with no behavior
change.

The standard 300-second full native workflow timed out in phase 15 on both
backends. A diagnostic run showed steadily advancing generated samples; an
original single-target 32 dB-Hz comparison also progressed slowly. No workflow
assertions or repository timeout settings were relaxed. The production workflow
was rerun with its supported 600-second command-line allowance.
FLTK completed the full unchanged workflow successfully in 473.511 seconds,
including keys, text, files, binary editing, cancellation, retained saves, live
plots and page switching.
Rev's extended run failed after 200.830 seconds in phase 11: the last observed
replay fraction was 0.830509, below the existing 0.9 assertion, despite 11 changing
frames and pending reception. This matches the native late-poll/replay-frame
sensitivity already described in the Rev backend notes; review found no selected
transmit-configuration mismatch. The log alone does not distinguish rendering
delay from host contention. Native workflow coverage is therefore not a clean
pass for Rev. No new physical audio or Windows validation was performed.

### Carrier centers, QR preview size and waterfall resizing (2026-09-16)

Every Rate now offers its half-rate center carrier alongside the unchanged
recommended default, including 1.8 kHz for 3.6 kHz. Shared controller/application
coverage checks all ten presets, custom rates, selection on every page and
continued manual overrides.

The QR preview spans the composition and action rows, growing from 78 to 115
logical pixels at the default size and from 50 to 87 at the minimum. Width-aware
growth preserves airtime space in tall, narrow windows. Existing editor heights,
generation rows, received history and plot geometry remain unchanged. Layout
checks cover minimum/default, tall/narrow and short/wide windows and prevent
overlap with controls or captions; QR expansion and integer module rendering
remain unchanged.

Live waterfall rows previously occupied one backing pixel each even when the
plot exceeded the 160-row retention bound. Taller plots now scale that bounded
history to the available height, with missing startup history still blank and
bottom-aligned. Smaller plots retain the recent-row behavior. Pixel regressions
cover full/partial/empty histories, odd heights, horizontal scaling, shrinking
back and the unchanged overview mode; the enlarged-history case failed before
the renderer fix. No modem framing, symbol timing, source interpretation or
pending-reception behavior changed.

Release builds succeeded for FLTK and Rev. All development-contract suites
passed, and the final shared GUI run passed all 24 tests, including the QR,
layout, bitmap, overlay and application regressions. An earlier application
test used an intermediate layout build; rebuilding the final layout resolved
its button-width assertion without changing that assertion.

Native screenshots on a private 96-DPI X display checked both backends at
1180×909, 1030×829, 1030×1200 and 1920×1440, then restored the default size and
expanded the QR. The final minimum-width airtime and action labels fit, the
larger QR remained square, and the waterfall retained and scaled its observed
history. Rev's carrier popup visibly offered 1.5 kHz and 1.8 kHz. FLTK adapter
and document conformance passed, as did Rev adapter, platform and 1×/2×
coordinate conformance. Full native transmission
workflows, physical audio and Windows were not rerun for these shared UI fixes.

### Parallel iterative pattern search (2026-09-16)

FFT hypothesis scoring and clock-window fit accumulation now share persistent
workers, with all but one available CPU selected by default (11 on this host).
Mutable pattern caches are private to workers; trial counting, peak ties,
admission, missing slots and physical completion retain the original order.
Scratch is charged to the configured workspace only while processing. Exact
idle-memory comparisons also cover cache eviction, so scratch cannot displace
other key/epoch searches from the live receiver bank. No transmit format,
source interpretation or pending-bit presentation rule changed.

New serial/parallel comparisons check exact scores, candidate order, thresholds,
every pending/provisional event, diagnostics and constellation points across
public/private patterns, clock phases, rate/frequency banks, ties, noise, missing
slots, physical absence and workspace reductions. The existing template-cache
assertions and compact four-hour bounds remain unchanged. Executor coverage
checks every index exactly once, simultaneous workers including the automatic
default, reuse, nested/concurrent callers, and joining all work before reporting
the earliest indexed exception. Additional executor stress passed 60 runs across
one-, two- and twelve-CPU affinity masks.

The Release build, including FLTK, succeeded. All 17 development-contract suites
plus `search_parallel` passed with `ctest --test-dir build --output-on-failure
-j 2` and the contract's test-name filter extended for `search_parallel`
(18/18, 194.42 seconds). ThreadSanitizer passed the executor
and both receivers' focused parallel comparisons with executor, scorers,
PatternCode, Crypto and test sources instrumented; unrelated archive objects and
external dependencies were not instrumented. ASan/UBSan passed the correlator's
eight exact-comparison configurations and compact-memory regression. Leak checks
were disabled for that run because sandbox ptrace prevents LeakSanitizer from
operating. Native display workflows, physical audio and Windows were not rerun.

Final five-second generated-noise benchmarks measured 1.77–1.88x speedup over
one-worker scoring on the 12-logical-CPU Ryzen 5 PRO 5650U host. At 12 kHz
bandwidth and an 80 dB-Hz target, thirteen keyed epochs improved from 0.71x to
1.25x real-time processing. The one-epoch case improved from 8.81x to 16.55x.
Commands and the additional 1.2 kHz measurement are recorded in
[throughput](throughput.md#cpu-and-live-throughput).

### Search scheduling and simulation processing (2026-09-16)

FFT scoring now queues several hypotheses per worker, sharing each worker's
private transform and pattern state while retaining separate ordered results.
Immutable FFT stage constants replace repeated identical calculations. Serial
tracking reuses exact template and carrier phase values in existing acquisition
buffers. No search hypotheses, arithmetic reductions, admission order, idle
receiver footprint, wire format or physical-completion rules changed.

An independent temporary probe linked the previous committed receiver and the
final receiver against the same remaining library objects. All 5,933,472 bytes
of its per-poll traces matched exactly across three fixtures at one, three and
automatic workers. Fixtures cover split stream phases without optional caches,
short private nonorthogonal sample fits with phase/carrier hypotheses, and
midstream optional-cache eviction. Comparisons include pending prefixes, bursts,
candidate evidence, thresholds, status, idle memory, diagnostics, constellation
samples and repeated EOF handling. A separate old/new FFT probe found exact
equality for power-of-two transforms from 4 through 65,536 in both directions.
Executor tests also passed under one-, two-, four- and twelve-CPU affinity masks.

The new `benchmark_simulation` target exercises the actual live session,
including sampled channel generation, receiver-bank processing and replay
preparation. It verifies exact bits, zero missing symbols and observed-absence
completion, and excludes only the fixed three-second presentation replay.
Instrumentation attributed about 99% of the original simulation's processing
time to reception and under 1% to waveform/channel generation. The worker limit
therefore does not imply full CPU utilization; ordered tracking and admission
remain serial, and short scoring jobs incur synchronization overhead.

The Release build, including FLTK and both benchmark targets, succeeded. All
17 development-contract suites plus `search_parallel` passed (18/18, 162.98
seconds). ThreadSanitizer passed exact parallel progress, physical absence and
cache/workspace equivalence with the executor, both scorers, PatternCode,
Crypto and tests instrumented; remaining archive objects and dependencies were
uninstrumented. ASan/UBSan passed exact progress, physical absence, cache
equivalence and shared-projection/workspace checks with the FFT receiver and
tests instrumented; other objects were uninstrumented and leak checks were
disabled for the sandbox restriction. The new simulation benchmark additionally
verified exact one-bit and three-bit receptions with one and four CPUs available,
selecting one and three workers respectively. Native display workflows, physical
audio and Windows were not rerun.

Final uncontended simulation pairs, with run order reversed, measured
17.5172/17.3937 seconds for the previous committed receiver and
14.6487/14.6905 seconds for the updated receiver. This is about 16% less processing
time at 12 kHz, an 80 dB-Hz target, thirteen keyed epochs and 64 exact raw bits.
The corresponding generated-noise benchmark improved from 1.25429x to 1.57008x
real time, about 25% greater throughput. Both receiver versions used 11 workers
and the same remaining library objects. Commands, CPU-utilization observations
and measurement limits are in [throughput](throughput.md#cpu-and-live-throughput).

### Long-symbol live acquisition and progress (2026-09-17)

A generated-PCM reproduction of a public raw `0` at Rate 3,600 Hz, carrier
1,500 Hz and target -8 dB-Hz confirmed the reported simulation failure. The
internal clock is 14,400 Hz and each bit occupies 5,732,744 samples (398.107
seconds). The original full FFT path buffered 1,165.084 seconds before its first
acquisition pass. Feeding all 800.428 seconds of sampled transmission and
observed-absence tail from the +3 dBm/-170 dB, 100 ppm, 0.5 degrees/sqrt(second)
channel produced no candidates or bits. This reproduction used the full 1,914
carrier/clock alternatives with sufficient memory, not the local fallback.
The live GUI simulation does not call the offline receiver's EOF flush.

Long-symbol acquisition now uses a smaller FFT when its overlap leaves useful
coverage, caps the range of new starts per batch, and scores an initial range
after a complete symbol plus at most one second of new starts. Established
tracks score complete available symbols at input progress boundaries instead
of waiting for another acquisition batch. Separate tracking scratch keeps the
smaller FFT buffers safe. Transmission, hypothesis coverage, confidence gates,
exact bit prefixes and fully observed physical absence remain unchanged.

Entire acquisition batches may be skipped only when one retained accepted span
already excludes every start and carrier under the existing admission rules,
and no ready continuation can rotate that span out before admission. Their
trial penalties remain charged; rejected-overlap diagnostic records are not
generated. An independent temporary build with this optimization disabled
produced 336 byte-identical emitted events across long-symbol progress and
coupled-clock fixtures. Instrumentation confirmed 524 skipped batches covering
145,016 start positions. Bits, poll positions, identities, sample endpoints,
scores, carrier estimates, missing slots and completion flags matched.

Long-symbol continuation also distributes independent carrier fits across CPU
workers when their bounded private pattern caches fit the workspace. Timing
selection and the ordered score reduction remain serial. A new exact comparison
passes for one worker versus three and automatic workers, including public and
private patterns, changing private stream phases, nominal/coupled clock fits,
duplicate-carrier ties, pending prefixes, diagnostics, idle memory and physical
completion without `finish()`. The compute estimate retains a conservative
serial-rate tracking allowance rather than promising this parallel speedup.
The parallel continuation and continuous FFT progress fixtures also passed
ASan/UBSan with the complete library instrumented; leak detection was disabled
for the sandbox restriction.

New regression coverage uses continuous PCM without `finish()`: public `001`
appears as `0`, `00`, then `001`, including delayed starts, different chunk
sizes and shared carrier projections. Noise-only input stays unadmitted and
partial silence cannot complete a message. Separate asynchronous live capture
and sampled live simulation tests expose a pending one-bit row and complete
that same row only after a whole absent symbol. The new capture regression
failed before the fix and passed afterward. ASan/UBSan passed the new continuous
FFT progress, coupled-clock progress/absence and streamed-template regressions;
leak detection was disabled for the sandbox restriction.

The initial complete Release build and 20 selected development-contract suites
passed (344.08 seconds); the FFT receiver suite also passed independently.
After the final continuation change, the complete Release build and all 21
development-contract suites plus `search_parallel` passed (22/22, 662.52
seconds while sharing the host with the exact-profile probe). FLTK and Rev GUI
binaries were rebuilt successfully. `git diff --check` passed.
These are software checks, not a new physical microphone/speaker validation.

The first exact-profile test of the updated scheduler entered acquisition at
about 399.1 seconds of media but did not finish the full search within a
600-second wall-time limit. It used automatic workers, restricted to four CPUs
after about 95 seconds to share the host with regressions, and consumed 44m14s
of CPU at a stable 1,202,164 KiB RSS. Removing the input-buffering delay does not
establish real-time throughput for this large search. The simulation compute
estimate reflects the new geometry and cadence but remains an engineering
model, not a benchmark or runtime guarantee.

The public exact-profile first pass still requires 7,657 transforms of
2,097,152 complex points: one input transform plus a generated-template forward
transform and inverse transform for each bit and each of 1,914 hypotheses.
That is about 169 billion radix-2 butterflies, excluding template generation.
Parallel continuation cannot reduce this acquisition cost. Keeping both
transformed templates for the whole bank would take about 120 GiB, so the
bounded receiver streams them instead.

A longer probe of a frozen scheduler/pruning build, before continuation was
parallelized, confirmed exact acquisition with the full public-pattern bank.
It published pending raw `0` at media time 399.217778 seconds, after 1,469.328724
wall seconds (24m29s), with score 120.519907. It used the same +3 dBm/-170 dB
sampled channel, 100 ppm clock mismatch and phase diffusion, without a narrowed
carrier bank or EOF flush. Covered-batch skipping then let input advance to the
absent-symbol decision at media time 796.405 seconds. RSS fell from 1,202,216
KiB during acquisition to 312,896 KiB during serial continuation. The host was
also running the final regression checks during acquisition, so this is an
observed shared-host time rather than an uncontended throughput benchmark.
The probe reached its original 1,800-second wall limit (exit 124) while scoring
that absent symbol; it emitted no completion event. Total CPU time was
205m12.060s. This proves exact pending acquisition, not end-to-end completion
at this profile. The subsequently added parallel continuation is covered by
the exact serial/parallel and physical-end regressions above, but was not
rerun through this full-size acquisition. Physical audio and real-time
throughput at this setting remain unverified.

### Simulation computation stalls and progress (2026-09-17)

The reported stall at a varying near-final audio percentage occurs before
replay. `source_loop()` publishes generated-audio progress before a synchronous
receiver push; a large acquisition or continuation pass can therefore leave
that percentage unchanged. The finite tail loop and receiver search coordinates
advance, and the code audit found no unbounded completion loop. Weak input can
add work: without an admitted bit and its retained span, later overlapping
full-bank acquisition batches cannot be skipped. This is a computation-cost
explanation, not evidence that every reported wait is a confirmed decode failure.

Finite simulations now report audio percentage separately from elapsed
steady-clock wall time. Snapshot polls advance the elapsed display during DSP
work even if no additional audio has been processed. Tail scoring has its own
"Checking reception after transmission" stage. Completion/cancellation freezes
elapsed time, reconfiguration/new work resets it, and replay uses its separate
presentation clock. The noise-model success label and help explain that the
probability assumes completed computation and is not an empirical success rate
or deadline prediction. No probability penalty was invented from an unfinished
run, and no timeout or UI state can complete reception.

A bounded diagnostic used public raw `0`, 400-second symbols, 64 Hz sampling,
8 Hz bandwidth, 16 Hz carrier, 100 ppm mismatch and 0.5-degree phase diffusion.
All four seeds (1, 7, 19, 73) at -3 dB-Hz C/N0 and at a 50 dB stronger channel
produced exact pending `0` and one physical completion without `finish()`.
The weak scores were 132.79--192.88 against threshold 47.11; all eight cases
took about 1.03 seconds. This 26-alternative search has different timing and
trial penalties from the user's 1,914-alternative profile and does not establish
its reliability or throughput. A separate live-session regression covers four
strong and four weaker seeds, finite computation and replay completion, exact
strong receptions, elapsed-time updates during unchanged audio progress,
reconfiguration and cancellation.

Streamed long public-pattern FFT searches now optionally retain one exact pair
of unmodulated nominal-clock waveforms, then apply each carrier rotation in the
original arithmetic order. The cache is push-scoped, included in measured
workspace accounting, and eligible only when it preserves the affordable
physical worker count and has more reusable jobs than workers. Private and
time-scaled templates retain their generation path. An existing cache can be
reused during continuation; tracking does not allocate another copy.

A temporary 40-second public-pattern microbenchmark preserved the 14,400 Hz
sample rate, 3,600 Hz bandwidth, 1,500 Hz carrier and projection geometry. With
64 mixed nominal/coupled jobs and four workers, it measured 3.062497 seconds
uncached versus 2.460236 seconds cached, including 0.076760 seconds to build the
4,608,928-byte cache: about 20% less wall time. All 230,400 two-bit score outputs
matched byte for byte. This is a reduced local microbenchmark, not a measured
speedup or end-to-end completion at the user's full 1,914-alternative setting.
The prototype is `/tmp/pumpmodem-nominal-cache/bench40.cpp`, run as
`timeout 60 /tmp/pumpmodem-nominal-cache/bench40`.

New batch regressions compare exact cached/uncached scores for shaped and
unshaped public patterns, sample fits, partial final chips, extended observation
windows, different stream indices and mixed clock alternatives. Poisoned cache
values cannot affect time-scaled alternatives. Invalid geometry and cancelled
construction/scoring are rejected. The continuous long-symbol comparison also
exercises streamed public rows, serial versus parallel progress, idle workspace,
pending prefixes and observed physical completion.

The actual rebuilt library repeated the 40-second microbenchmark in both run
orders, also with byte-identical scores. Baseline-first measured 2.595165 seconds
uncached versus 1.997540 seconds cached including construction; cached-first
measured 2.502675 versus 1.938406 seconds. Both are about 23% less wall time.
Commands were `timeout 60 /tmp/pumpmodem-nominal-cache/bench_actual40` and the
same command with `reverse`. These runs followed the regression suite, without
another heavy workload dispatched in this task.

The complete Release build succeeded, both FLTK and Rev GUI binaries were
rebuilt, and all 21 development-contract suites plus `pattern_fft_batch` and
`search_parallel` passed (23/23, 225.81 seconds). Full-library ASan/UBSan builds
passed the FFT batch suite and the streamed public/private exact parallel
long-symbol progress fixture; leak checks were disabled for the sandbox
restriction. `git diff --check` passed. Native display workflows and physical
speaker/microphone reception were not rerun.

The final full-profile probe also completed successfully within its original
1,800-second limit, using a frozen copy of the rebuilt library and no other
heavy workload dispatched in this task. It sent public raw `0` with 3 dBm /
-170 dB, 3,600 Hz Rate, 1,500 Hz carrier, -8 dB/Hz target, 14,400 Hz internal
sampling, 100 ppm clock mismatch, 0.5-degree phase diffusion and noise seed 1.
The receiver retained all 1,914 alternatives under a 1,200 MiB workspace budget,
with automatic worker selection and no narrower fallback. One symbol was
398.107170553 seconds. It accepted exact pending `0` at 399.217777778 seconds
of input and 1,381.07810026 seconds of wall time (23:01), with evidence
120.519906936, identical to the earlier full-profile result. It then observed
physical completion at 796.547708333 seconds of input and 1,719.18909057 seconds
of wall time (28:39). The final 800.428263889-second input finished at
1,719.19319546 seconds, with one accepted bit and exactly one completion;
neither `finish()` nor an oracle supplied completion.

The largest sampled RSS was 1,195,220 KiB (about 1.14 GiB), falling to
312,948 KiB during continuation; CPU time was 15,872.917 seconds. The command
was `timeout 1800s /tmp/pump-exact-cache-parallel-053542`, with source, frozen
archive and output alongside it as `.cpp`, `.a` and `.log`. The archive SHA-256
was `c91e87b42f43a94b7eec2bcfbc0563342b864fee693d212d20b064edc1665f5b`.
This establishes successful pending and physical completion for this one public
seed, not the displayed model's population success rate, private-key behavior,
real-time throughput or a physical audio link. End-to-end computation remains
long at this setting; the roughly 23% reduced-benchmark saving is not a measured
full-profile speedup.

### Shared Link planner (2026-09-18)

The new native document tab shares the existing tuning, transfer, LPI and
receiver-workspace models across FLTK and Rev. Preview targets and link-budget
inputs remain separate from live settings until the operator applies a short-
or long-message target. Draft planning retains the existing exact wire count;
completion timing adds fully scored absence. No transport, framing, receiver
progress or physical-end behavior changed.

The dedicated `gui_link_planner` suite checks independent numerical anchors at
−8 and −23 dB-Hz, sample quantization, the one-second and one-day transitions,
fixed modes, bounded curves and duration overflow. Controller/application cases
cover preview isolation, explicit apply, prompt cancellation and invalid input,
exact short/raw/interval draft counts, stale-estimate withdrawal and asynchronous
estimate failure/recovery. Native document checks cover narrow and wide layouts,
actions, one general LPI warning, and identical full/tiled chart damage in RGB,
grayscale and monochrome. The existing application regression explicitly hides
the current-draft LPI advisory on this new tab while retaining it on every
pre-existing page.

Release builds succeeded with GCC/FLTK and Clang/Rev. All 21 development-contract
suites passed; the initial 29-suite run required the new-tab advisory expectation
above, then `gui_application` passed on rerun. Final focused shared GUI checks
passed 9/9 for FLTK and 8/8 for Rev, including the planner, application, document
layout and adapter-boundary checks. No physical radio/audio link was tested.

On an isolated Xvfb display, FLTK `gui_adapter_conformance` and
`gui_document_conformance` passed. The full `gui_workflow` initially reached its
unchanged 300-second smoke limit while other builds/tests ran; an isolated rerun
passed in 225.21 seconds without changing the workflow, receiver or timeout.
The compact planner was also inspected in native FLTK and Rev windows: headline
times, both charts, all four milestones and Apply actions fit the default view.
After final wording corrections, both Release builds and both
`gui_link_planner`/`gui_application` pairs passed again (2/2 per backend).

Rev's full native `gui_workflow` did not pass: under software OpenGL on Xvfb it
reached the unchanged 300-second smoke limit in phase 17 while transmitting
sampled audio to the independent receiver. Its screen was still reporting
simulation progress. This run does not establish a completed Rev workflow or
whether the timeout differs from the baseline; no timeout or assertion was
relaxed to claim success.
Rev's `gui_adapter_conformance`, `gui_platform_conformance`,
`gui_coordinates_1x` and `gui_coordinates_2x` all passed on the same private
display (4/4). `git diff --check` passed.

### Planner order, units and clock/RAM limit (2026-09-18)

Link planner now follows Console. Frequency labels use plain decimal Hz/kHz/MHz,
and the corrected LPI example selects +23 dB-Hz: 0.568889 seconds per bit,
5.853333 seconds sending one bit, and a modeled observer/receiver ratio of
4.33307. The −23 case remains independent long-duration regression coverage.

The clock milestone now requires both carrier-search coverage and the existing
simulation estimator's wide-search workspace support. It uses the selected DSP
byte allowance, reports its 25/50/75% metadata, and identifies Clock or RAM as the
limiting condition. Complete native projection-bin candidates avoid arbitrary
one-sample memory gaps; every returned target is independently rechecked. The
controller preserves full floating-point precision through prompting/applying
these sample-sensitive targets. No sampled receiver or wire behavior changed.

New regression cases cover 512 MiB versus 1 GiB at −8, fitting returned
milestones, 75% selection propagation, exact prompt/apply round trips, second-tab
order, +23 numeric anchors, and `4.375–10.625 kHz` endpoint formatting. Both
Release backends rebuilt successfully. Rev's eight focused shared GUI suites
passed. The updated FLTK window was visually checked on a private Xvfb display;
the navigation, controls, milestones and graphs fit the default window.
All 29 GCC/FLTK headless checks passed in 242.03 seconds, including every one of
the 21 development-contract suites. `git diff --check` passed. Native end-to-end
workflows and physical audio were not rerun for this planner-only correction;
the earlier Rev workflow timeout remains an unclosed validation limit.
