# Standalone Helper covariance and recovery experiments

**Integration entry point: [Helper integration: start here](helper-integration.md).**
It identifies the current inexpensive composition and separates required
behavior from implementation choices. This page retains detailed experiments;
earlier failed candidates and guards are not universal architecture requirements.
Selected [machine-readable results and reproduction inputs](validation-data/helper/README.md)
are retained in the repository; they do not require ignored local evidence.

This study continues the independent Helper-only simulations described in
[frequency refinement](helper-frequency-refinement.md). It does not integrate
or test ordinary Robust reception. The compared receivers use the actual Helper
transmitter waveform, cryptographic-keystream random hop schedules and the same
generated impaired capture for each method. The HF schedule has 50 channels at
50 kHz spacing around 30 MHz; the existing FCC100 and EU20 wide-hop geometries
are available for microwave experiments. Deterministic public test keys support
reproducibility; schedule geometry alone does not certify regulatory compliance.

A full comparison observes 36 seconds of Helper, retains the frozen result at
that point, and uses the fixed 38-second lead before probes in seconds 38–40.
A later dwell can provide a fresh phase or update the current frequency model,
but cannot improve a result labeled available after 36 seconds. Each hop has an
independent common RF phase. A dwell-based correction uses approximately 0.4
seconds of buffering in the earlier experiments. The current complete-stream
path defaults to two seconds and may use later Helper observations for clock
and frequency evolution. Each dwell still uses its own fresh RF phase; it
does not borrow a neighboring hop's phase or assume RF phase continuity.

The 40 W examples specify total active-mean Helper waveform power and an
independent probe power. A simultaneous 40 W Helper and 40 W probe represents
80 W nominal combined power, not a 40 W total transmitter limit. Input
noise temperature, noise figure, path loss, audio-clock errors, shift-oscillator
errors, and phase diffusion are explicit simulation inputs. These are modeled
link conditions, not physical sensitivity or hardware-qualification claims.

## Practical path prepared for integration

The current objective is a complete, corrected, timestamped baseband stream.
Approximately ±0.1 Hz was a desired scale for frequency narrowing, not a hard
admission or integration threshold. Use the measured frequency estimate and
its honest conditional uncertainty; a useful ±0.117 Hz estimate must not block
delivery merely because it is wider than ±0.1 Hz. Preserve genuine detection,
model-consistency and reference checks. They determine what correction is
available and how it is described, not whether captured samples disappear.

The demonstrated standalone building blocks are:

| Building block | Behavior to carry into the eventual adapter | Evidence and remaining scope |
| --- | --- | --- |
| Complete-dwell delayed correction | Buffer complete captured dwells, obtain their fresh phase and available frequency/clock estimates, and extrapolate those corrections back to the beginning and forward to the end before release. | `HelperCompleteConditioner` tests verify the actual complex output before, inside and after the pilot aperture, including phase-extrapolation signs and irregular capture chunks. The v9 noisy studies exercise this in the receiver chain. |
| Continuous delivery | Release the corrected/resampled stream chronologically, including uncertain intervals. Confidence does not blank, fade, shorten or suppress output. Explicit missing-input events preserve real gaps. | All 30,720,000 captured inputs in the 32 fresh v9 clean cases were processed in both nominal and tracked arms. Phase and clock correction were applied to all 30,721,548 emitted resampled outputs. The experimental tracked arm's reported 18.45% conservative `well_supported` label is **not** the fraction corrected or delivered. |
| Retained reference and later evidence | Keep one initial relative timing/phase reference. Later clock evidence may improve unreleased samples without selecting a new reference or smoothing RF phase across hops. | Gap, reference, clock-servo and future-observation checks are implemented. Arbitrary differential channel phase and whole-cycle timing ambiguity remain unresolved. |
| Time-preserving consumer interface | Carry fractional input sample coordinates, original capture UTC/monotonic time and epoch, actual release time, relative source time, and the latest Helper observation used. Select time-dependent patterns from capture time. | Deterministic tests cover startup/drain, clock changes and missing input. Hardware capture-clock accuracy and Robust's own pattern/time interface still need integration testing. |
| Useful frequency and fresh-phase estimates | Keep the 36-second frequency snapshot separate from later per-hop phase/clock updates. Use both pilots with their shared-observation covariance, and retain acquisition/fallback. | The fresh 180 dB set had 0.0150 Hz frequency-error RMS and a median conditional half-width of ±0.1165 Hz in 31 available cases; all 31 intervals covered. This is useful measured narrowing, not a failed delivery condition or a universal coverage guarantee. |

Use the receiver's known dwell schedule as the default correction calendar.
If dwell timing is adjusted, the intended evidence is the **auxiliary-tone
phase reversal**, rather than the pilot-envelope ramp experiment. The existing
transmitter places this marker at 160 ms into each 400 ms dwell on the 950 Hz
auxiliary only: it tapers through zero over 152–168 ms and emerges inverted.
The high and low pilots do not reverse. The existing auxiliary receiver fits
the marker and checks successive-dwell cadence; auxiliary payload decoding is
not a requirement for useful pilot correction. See the
[waveform timing contract](helper-carrier.md#dwell-waveform-and-metadata).

The optional `HelperInversionTiming` adapter now publishes this receiver's
actual marker observations, and the complete-stream study can select them
with `auxiliary` boundary mode. It uses measured pilot frequencies to locate
the auxiliary, requires no payload decode, and works without pilot ramps.
The strict adapter's matched 40 W / 180 dB checks accepted no individual markers.
That is not the limit of available timing methods: the later
[`HelperInversionProfile` pooled detector](#sensitive-pooled-inversion-timing-experimental)
demonstrates useful calendar timing at that loss in the actual complete-stream
chain. Until evidence for the selected method is available and consistent,
retain schedule timing and its uncertainty.
The v9 `tracked` mode remains a selectable ramp-based experiment for the
recorded comparisons, not the recommended boundary-adjustment mechanism for
integration. A transmitted marker also does not independently measure a
receiver RF retune that occurs earlier or later than the signal's dwell date.

Accurate boundary timing prevents a sample receiving the neighboring hop's
phase correction. The useful settled-dwell estimate can then be extrapolated
through the whole assigned dwell. Finite estimation error and physical
settling/phase disturbances can still leave residual error; all such samples
remain delivered and included in complete-stream measurements. Neither
imperfect timing nor residual uncertainty is a reason to gate the entire
baseband stream. A missing fresh phase must be reported as unavailable while
other available corrections and the captured samples continue through.

The prepared path is the known-schedule, two-second complete-dwell conditioner
with current pilot/joint-clock estimates, permanent reference and explicit
capture-time mapping. The remaining integration work is the actual receiver
adapter, validation of the optional auxiliary-marker timing bridge, the downstream bandwidth and
resampling contract, and validation against Robust's time-based pattern
selection. The current linear interpolator was evaluated for the 1600 Hz tone
and 360 Hz shaped APSK band; it is not transparent across the full 24 ksample/s
Nyquist band. No ordinary Robust integration, GUI changes, hardware access or
full Robust campaign is part of this preparation.

The [v9 implementation and complete-stream results](#incremental-complete-capture-conditioner-v9)
below retain the exact comparisons, failure cases, CPU, latency and memory
measurements. Earlier failed candidates and exact ±0.1 Hz counts remain
experimental evidence; they do not replace this practical readiness criterion.

### Auxiliary phase-inversion timing accuracy

A small follow-up measured the current 160 ms reversal detector using the
existing `AuxiliaryFixture` and `Receiver`, rather than treating the existing
8 ms test tolerance as measured accuracy. Each run supplied six seconds of
the isolated, normalized production auxiliary shape with +100 ppm constant
source/capture rate error. There were no other tones, RF hops, phase noise,
capture offset or physical radio effects. The noise tests added white real
noise at a stated **steady auxiliary C/N0**, not a whole-Helper SNR. One fixed
noise seed was used; these are component smoke checks, not a sensitivity or
coverage campaign.

| Sample rate / auxiliary C/N0 | Distinct accepted markers | Timing RMS / maximum absolute error | Reported per-marker radius |
| --- | ---: | ---: | ---: |
| 20 ksample/s, noiseless | 14 | 0.0777 / 0.109 µs | ±50.0 µs |
| 24 ksample/s, noiseless | 14 | 0.0777 / 0.109 µs | ±41.7 µs |
| 24 ksample/s, 80 dB-Hz | 14 | 3.02 / 4.99 µs | ±41.7 µs |
| 24 ksample/s, 60, 40 or 18.88 dB-Hz | 0 in each run | Unavailable | No accepted marker |

The very small noiseless error measures numerical/template agreement, not
radio accuracy. Fractional-sample fitting permits sub-sample point estimates;
the implementation's reported uncertainty has a one-sample floor and is not
a calibrated weak-signal guarantee. All 14 accepted strong-signal markers were
inside their reported radii in these checks. At 24 ksample/s and 80 dB-Hz,
their 5.19948-second span gave a conditional audio-clock interval of
[83.8013, 115.8590] ppm, containing the injected +100 ppm. The existing clock
fitter retains at most 16 observations (about six seconds at full cadence),
so this is not a demonstrated 36-second timing fit.

Zero detections at the three lower C/N0 settings describe this detector and
these particular inputs; they do not establish a physical timing limit or
rule out pilot-aided marker correlation. No marker timing accuracy has yet
been established at 40 W Helper / 180 dB through random FHSS in the complete
standalone receiver. The older private likelihood experiment's 0.230 ms RMS
result used the former 240 ms marker and cannot qualify this current waveform.

The timing information can provide a measured dwell offset modulo 400 ms and,
from successive distinct, dated reversals, a relative audio-clock rate.
That can complement the two-pilot clock estimate and place correction epochs
for a future downstream receiver. Auxiliary payload decoding is unnecessary.
A constant propagation delay cancels in the rate estimate, but a changing
delay can imitate clock evolution. The periodic marker alone supplies neither
absolute UTC/whole-cycle identification nor the receiver's physical RF retune
time. Only actual dated observations count; republishing or extrapolating a
marker must not add independent clock evidence.

The diagnostic source and per-marker errors are retained in
`local-evidence/helper-continuation/marker_accuracy_probe.cpp` and
`marker-accuracy-*.log`. Reproduce the component measurement with:

```sh
g++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off -I. -Iinclude -Itests -Ithird_party/xz/src/liblzma/api local-evidence/helper-continuation/marker_accuracy_probe.cpp build/dev/libdatapump.a build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a -lcrypto -ldl -lpthread -o /tmp/helper-marker-accuracy
/tmp/helper-marker-accuracy 24000
for cn0 in 80 60 40 18.88; do /tmp/helper-marker-accuracy 24000 "$cn0"; done
```

### Standalone inversion measurement and complete-stream bridge

The side conversation's `HelperInversionTiming` implementation is reused, not
duplicated. It adapts the existing production auxiliary detector into a bounded
standalone measurement interface. Each publication contains the fractional
capture-sample date, uncertainty, generation and the complete raw FIR evidence
interval. It measures the current **160 ms auxiliary reversal**. Neither pilot
ramps nor successful auxiliary metadata decoding are prerequisites. The decoder
and marker-discovery states are separate; successful decoding does not stop
marker publication.

The complete-stream study now accepts `nominal` (unchanged default), `tracked`
(existing ramp experiment), `auxiliary`, or `combined` as its boundary method.
`auxiliary` uses measured high/low pilot frequencies to center the auxiliary
detector. An accepted marker gives a dwell-start estimate by subtracting
160 ms at the estimated source/capture clock ratio. Clock uncertainty is added
conservatively, without treating that prediction as independent timing evidence.
Only a measured point consistent with the existing ±8 ms schedule prior is
selected; otherwise the known schedule remains in force. `combined` falls back
to the ramp experiment. The marker's raw evidence date participates in the
delayed correction's latest-observation metadata. The first sample assigned to
the new hop is the ceiling of its fractional boundary date.

This bridge adjusts individual observed boundaries. It does not yet fit a
marker-derived clock or extrapolate an accepted marker through subsequent
unobserved boundaries. It never reingests the marker/pilot prediction as another
joint-clock observation, resets the permanent reference, assumes RF phase
continuity, or drops uncertain output. The physical-retune allowance remains
separate: a transmitted marker cannot determine an independently early/late
receiver retune. Drain the adapter's update queue before explicit gaps or resets;
those reset its optional local history and clear pending publications.

Eight matched 40-second HF cases used distinct verified expanded schedule keys
1911000001–1911000008 and thermal seeds equal to key seed +20,000,000. The first
case was used for feasibility before the full comparison; this is a matched
screen, not 32 fresh holdouts. Each has cryptographic random 50-channel/50-kHz
hops, independent RF/audio errors, 290 K noise temperature, 10 dB noise figure,
the unchanged 36-second frequency freeze and 38-second probe lead. The normal
180 dB case uses **40 W Helper plus a separate 40 W probe: 80 W combined** when
both transmit. The strong 120 dB `no-pilot-ramps` counterexample retains the
standard 40 W normalization and pilot amplitudes but removes their dwell ramps;
its changed total waveform energy is **not** renormalized to exactly 40 W.

| Matched result, eight cases | 180 dB normal waveform | 120 dB, pilot ramps absent |
| --- | ---: | ---: |
| Accepted inversion markers / nominal dwells | 0 / 800 | 199 / 800 |
| Accepted markers available by 36 s | 0 | 193 |
| Marker timing RMS / worst absolute error | Unavailable | 8.54 / 49.09 µs |
| Accepted errors inside reported approximately ±41.7 µs radii | Unavailable | 198 / 199 |
| Wrong-hop assignments, schedule → auxiliary | 41,719 → 41,719 | 41,592 → 30,752 |
| Complete 0–40 s synthetic clean-twin phase RMS | 8.450° → 8.450° | 7.693° → 6.490° |
| Complete output samples, either method | 7,680,388 | 7,680,384 |
| Conservatively well-supported samples, either method | 1,539,763 | 5,385,184 |
| Capture timestamp / permanent-reference mismatches | 0 / 0 | 0 / 0 |

The gain comes from correcting some previously uncertain boundary samples,
not discarding difficult samples or increasing the support mask. Worst
complete-stream errors still approach 180° where boundaries are unmeasured.
At 180 dB, all reported scientific outputs match the unchanged schedule path;
the optional measurement added cost without improving this weak-link screen.
The 36-second frequency result is also unchanged: 0.01921 Hz RMS, median
conditional half-width ±0.12070 Hz, 8/8 conditional intervals covering. This does
not establish reliable ±0.1 Hz narrowing, and the approximate target does not
gate delivery. The strong variant's marker adjustment did not measurably improve
the held-out 38–40 s probe boundaries in this screen. Its all-sample held-out
beginning/interior/end clean phase RMS remained 11.97° / 0.0565° / 1.431°.
The 180 dB counterparts were 12.22° / 2.186° / 2.912°.

One matched 360 Hz APSK probe case at each loss also showed no held-out gain:
clean-twin / noisy-composite EVM was 6.34% / 56.34% at 180 dB, and
4.94% / 4.70% in the strong ramp-free variant, for either method. These are
584 measured symbols per case, not a sensitivity campaign. The clean twin is
evaluated independently; no probe alignment or parameter selection feeds back.
The APSK evaluator reported its 4–6 non-grid endpoint samples separately;
the conditioner still released those samples.

These are **clean-twin correction errors**, using one fixed evaluator reference;
they are not noisy-probe measurement precision or an oracle bound. All uncertain
samples remain in these primary totals. Per-region tails, noisy composite
measurements, timing drift and cycle-error counters are retained in the logs.
The one 49 µs accepted error demonstrates that the detector's sample-floor
radius is not a calibrated universal guarantee. A six-second non-FHSS received
waveform check accepted 14 markers at 120 dB (9.64 µs RMS, 29.09 µs worst), and
the explicit-gap check recovered. Straight-auxiliary, absent-auxiliary and
noise-only controls selected no inversion boundaries. The specific FHSS
decline gates have not been instrumented; coherent-flank, cadence/notch and
frequency-acquisition requirements are possible causes, not established ones.

All timing below is process CPU including threads, measured separately from
simulation and metrology. At 180 dB the median optional detector cost was
0.467 s for the full **40 s capture**, with 238,140 bytes of prepaid adapter/
detector workspace plus the bridge's bounded 16-record history. Existing
complete buffering/conditioning used 1,847,632 bytes. The full-stream
conditioner plus output instrumentation cost 0.524 s/40 s; startup, aided
tracking, fallback and history/fit cost 0.0268, 0.2098, 0 and 0.0073 s over the
40 s run. Total receiver cost was 1.081 s at the 36 s capture frontier
(34 s already conditioned), and 1.237 s for 40 s plus drain. Generation and
metrology were separately 0.448 and 0.542 s/40 s. The schedule-only totals were
0.680 s at 36 s and 0.782 s through drain. These instrumented costs exceed the
preferred budget; the optional strong-signal detector remains off by default.
With a configured 2 s buffer, measured capture-to-release latency ranged from
about 2.000 to 2.800 s because complete blocks and interpolation dependencies
must become available. Captures retain their original dates through drain.
There were five 0.36 s broad apertures and 95 aided 0.36 s apertures in the
representative full run; the optional marker worker processed all 40 s.

Short 0.4, 1, 2 and 3 s delay checks selected the same two strong HF markers;
extra delay did not add marker information in those captures. Measured latency
ranges were approximately 0.4–1.2, 1.2–1.6, 2.0–2.8 and 3.2–3.6 s. Full-run
explicit-gap and system-clock-step cases preserved the original reference and
capture timestamps; the missing 9,600-sample dwell was reported as one gap.
Early/late RF-retune checks still had wrongly assigned samples, as expected
when the physical retune differs from the transmitted marker calendar.
A full physical phase-pulse check (`transient-v9held-interior-phase`) accepted
22 markers within their reported radii, preserved timestamps/reference, and
reported zero evaluator timing-cycle crossings; it does not establish that
the phase pulse itself is corrected. An initially mistyped diagnostic profile
(`transient-phase-pulse`) failed argument validation and was rerun under the
existing profile name, rather than counted as a passing impairment test.
Both four-second strong 5.8 GHz fixtures accepted nine markers, with five
wrong-hop assignments and zero timestamp/reference errors. Their worst marker
error was also 49.09 µs, so these checks do not resolve uncertainty calibration.

Fifteen focused CTest checks passed, including the original complete-stream
clock regression and new actual-chain inversion regression. The latter verifies
real boundary selection without ramp tracking, complete sample accounting,
absence controls and unchanged weak-link fallback. No ordinary Robust, GUI or
hardware tests were run. There was no weak-link timing improvement warranting
a 32-case finalist expansion or a claim of physical sensitivity limits.
Pilot-aided weak-signal marker estimation and marker-clock fusion remain open;
this adapter's availability does not rule out those approaches.

Reproduce with the existing `build/dev` profile:

```sh
./build.sh configure --build-dir build/dev --cli
cmake --build build/dev --target test_helper_inversion_timing test_helper_inversion_study test_helper_complete_study test_helper_auxiliary_timing_fixture -j 2
ctest --test-dir build/dev --output-on-failure -R 'helper_(inversion|complete_inversion|auxiliary_timing)'
build/dev/test_helper_inversion_study 120 plain 1910000001 15
build/dev/test_helper_inversion_study 120 gap 1910000001 15
build/dev/test_helper_inversion_study 120 noise 1910000001 15
for seed in $(seq 1911000001 1911000008); do
  for method in nominal auxiliary; do
    build/dev/test_helper_complete_study full 180 "$seed" clean tone 2 hf "$method"
    build/dev/test_helper_complete_study full 120 "$seed" no-pilot-ramps tone 2 hf "$method"
  done
done
```

Exact commands, binary hashes, per-marker dates, focused check output and
aggregates are retained under `local-evidence/helper-continuation/inversion-*`.
The final auxiliary comparisons include the reviewed first-new-hop-sample
rounding repair; pre-repair logs are explicitly suffixed `.preceil`.

### Measured path-loss range of the strict per-dwell inversion detector

A subsequent loss sweep kept the receiver and every threshold unchanged and
used the **standard 40 W Helper waveform**, including its usual pilot ramps.
This removes the ramp-free variant's energy caveat from the link-budget
comparison. Conditions were random HF50 hops at 50 kHz spacing around 30 MHz,
290 K and 10 dB noise figure, +37/−13 ppm independent transmitter/receiver audio
clocks, 0.8 ppm relative RF error and the existing phase-noise model. A separate
40 W tone probe occupies seconds 38–40 (80 W combined during that interval).
The receiver has the usual 36-second frequency snapshot, 38-second lead and
2-second complete-stream buffer; the table concerns **actual inversion-marker
measurements**, not general Helper pilot tracking or predicted boundaries.

Eight expanded schedule keys 1920000001–1920000008 and independent thermal
seeds +20,000,000 were reserved and checked as actual distinct 32-byte keys
before the sweep. The same impaired realizations were reused across losses.
Four-second screens preceded the following eight **40-second captures per
loss**, using the actual complete receiver chain. No estimator was tuned, and
the separately reserved validation keys remain unused.

| Loss | Captures with a marker / 8 | Markers / 800 nominal dwells | Markers available by 36 s | Accepted timing RMS / p95 / worst |
| --- | ---: | ---: | ---: | ---: |
| 120 dB | 8 | 171 (21.4%) | 162 | 7.47 / 13.51 / 29.09 µs |
| 124 dB | 8 | 57 (7.1%) | 54 | 7.84 / 15.75 / 19.33 µs |
| 126 dB | 5 | 5 (0.63%) | 4 | 13.56 / 23.80 / 23.80 µs |
| 128 dB | 1 | 1 (0.13%) | 1 | One error: 14.58 µs |
| 130 dB | 0 | 0 | 0 | Unavailable |
| 140 dB | 0 | 0 | 0 | Unavailable |
| 150 dB | 0 | 0 | 0 | Unavailable |
| 160 dB | 0 | 0 | 0 | Unavailable |
| 170 dB | 0 | 0 | 0 | Unavailable |
| 180 dB | 0 | 0 | 0 | Unavailable |

Thus the current detector supplies repeatable **occasional** timing evidence
around 120–124 dB under this model; 126–128 dB is sporadic, and no measurement
appeared at the tested losses of 130 dB or higher. It has not demonstrated
fresh timing on every hop even at 120 dB. Individual 120 dB runs yielded
12–33 markers, with a longest marker-free interval of 13.60 s; 124 dB runs
yielded 4–14, with an 18.00 s interval. These intervals use capture dates and
include capture edges, not inferred processing/publication latency. The first
accepted marker's capture date ranged from 0.56–1.36 s at 120 dB and
0.56–10.96 s at 124 dB. At 126 dB each successful capture supplied only one
marker, insufficient on its own for an independent clock-rate fit.

All 234 accepted errors in this sweep were inside their reported radii. This
does not erase the earlier 49 µs outlier or establish universal interval
coverage: observations within a capture are correlated, and the accuracy
statistics are conditional on acceptance. All 80 full captures preserved
timestamps and the permanent reference. At 124 dB, eight four-second captures
each with straight auxiliary, absent auxiliary or noise-only input selected
zero inversion boundaries. These are focused controls, not a lifetime
false-alarm calibration.

A separate six-second, non-FHSS cross-check used the existing
`test_helper_inversion_study` frontend (broad per-dwell pilot acquisition, not
the complete chain's aided tracking). Across eight captures it accepted
112/120 nominal-dwell markers at 120 dB, 78/120 at 124 dB, 3/120 at 128 dB,
and none at 130 or 140 dB. This supports the strong-signal sensitivity result;
different frontends and durations prevent treating it as a clean quantitative
isolation of FHSS alone.

The limitation belongs to this strict strong-signal auxiliary detector.
Useful pilot frequency/phase tracking at 180 dB remains demonstrated separately.
These results neither establish a physical sensitivity limit nor reject a
more sensitive pilot-aided inversion estimator. The auxiliary is 9 dB below
the high pilot: at this noise figure its approximate steady C/N0 is 78.9 dB-Hz
at 120 dB loss and 18.9 dB-Hz at 180 dB. No receiver or waveform changes were
made for this sweep, and no Robust, GUI or hardware tests were run.

Reproduce the full accuracy/availability comparison:

```sh
for loss in 120 124 126 128 130 140 150 160 170 180; do
  for seed in $(seq 1920000001 1920000008); do
    build/dev/test_helper_complete_study full "$loss" "$seed" clean tone 2 hf auxiliary
  done
done
```

Commands, binary hash and individual marker errors for all 80 full-capture
cases are retained in the repository's
[strict-detector record](validation-data/helper/2026-10-31/strict-inversion.json).
Original logs and derived aggregates remain in
`local-evidence/helper-continuation/inversion-loss/`. The unchanged complete
study binary SHA-256 is
`7f73fcaed8e9c4732136aba0db3aa09862be4acf5723414e67709fb47933771b`.
At most two simulations ran concurrently; this sweep is an accuracy study,
not a new receiver CPU benchmark.

### Sensitive pooled inversion timing (experimental)

`HelperInversionProfile` and the selectable complete-study boundary mode
`pooled-inversion` trade unnecessary microsecond precision for sensitivity.
The existing `nominal`, ramp `tracked`, and strict `auxiliary` paths remain
selectable. The transmitter waveform, pilot power split and payload format
are unchanged. This is an actual receiver/correction-chain experiment:
accepted timing changes the sample-to-hop association before buffered output
is corrected and released. It does not merely enlarge a diagnostic mask.

The receiver uses the measured pilot frequencies to mix an 80 ms window around
the existing 160 ms reversal. It projects out both neighboring pilots, forms
disjoint 250 µs complex correlation bins, and compares the known tapered
reversal with a straight auxiliary tone. It fits a separate complex auxiliary
gain in every dwell, so random hop phase is never coherently joined across
hops. A bounded three-frequency check retains prediction mismatch detection.
The timing grid has 129 points at 125 µs spacing within the existing ±8 ms
calendar prior. Received residuals supply the noise estimate; simulator SNR,
the probes and clean twins do not choose timing, thresholds or corrections.

Weak dwells retain their complete data likelihood rather than having to pass
the strict per-dwell detector. Up to 96 disjoint profiles are reexpressed under
the accumulated two-pilot relative clock and combined once. Predictions and
repeated queries do not add independent observations. Clock/model uncertainty
is shared, not divided by the number of dwells. Disjoint early/late groups can
reject a contradictory clock map when each group has sufficient evidence;
weak groups need not independently detect the marker. A strong per-dwell
result remains available before the clock history is ready. There is no new
clock feedback loop, reference reset, auxiliary decoding prerequisite, or
assumed RF phase continuity.

The result distinguishes marker evidence from conditional timing qualification,
and exposes the marker date, inferred dwell-start date, interval, original
reference and latest actual observation used. Source-clock offset intervals
and capture-time intervals have distinct units. The source-clock model before
its initial accepted phase center is a bounded extrapolation, not independent
timing evidence. The inference locates the repeated calendar modulo 400 ms;
it does not decode absolute UTC or remove whole-cycle/reference ambiguity.

The complete-stream adapter uses qualified estimates only within the original
boundary prior, preserves the ±8 ms physical-boundary uncertainty enclosure,
and never changes sample delivery based on confidence. It preserves capture
timestamps and applies each hop's own phase to both sides of its guarded
estimation aperture. An observation-provenance bug found during this work was
also corrected: the profile must report the last actual Helper support, not
the end of an entire capture block that it did not observe. Overstating that
endpoint could make the conditioner correctly refuse an otherwise available
correction. The matched tests assert phase/clock application over all clean
output, not just timestamp availability or masks.

#### Matched loss screen

Eight reserved HF50 schedules per loss used keys 1930000001–1930000008 and
independent noise seeds +20,000,000. The actual 32-byte keys were checked for
uniqueness before tuning. These are standard 40 W Helper captures at 290 K,
10 dB noise figure, independent +37/−13 ppm audio clocks, 0.8 ppm relative RF
error and the existing phase-noise model. The separate 40 W probe in seconds
38–40 makes 80 W combined during the probe. Each capture lasts 40 seconds,
with the frequency and timing snapshot frozen at exactly 36 seconds, fixed
38-second lead and a two-second complete-dwell buffer.
At 180 dB the auxiliary has approximately 19 dB-Hz C/N₀, or about −25 dB
signal-to-noise ratio per 24 kHz complex sample. Accumulating known reversal
structure is what makes this timing estimate useful below the sample noise.

| Loss | Qualified timing at 36 s | Timing error RMS / worst | Median conditional half-width | First qualified calendar, median | Complete-output clean-twin phase RMS, nominal → pooled |
| --- | ---: | ---: | ---: | ---: | ---: |
| 180 dB | 8/8 | 276 / 540 µs | ±2.169 ms | 8.8 s | 8.592° → 6.054° |
| 175 dB | 8/8 | 179 / 289 µs | ±1.265 ms | 6.4 s | 8.076° → 4.325° |
| 170 dB | 8/8 | 100 / 164 µs | ±0.748 ms | 6.4 s | 7.898° → 3.742° |
| 165 dB | 8/8 | 55 / 87 µs | ±0.550 ms | 6.4 s | 7.833° → 2.281° |
| 160 dB | 8/8 | 47 / 87 µs | ±0.356 ms | 6.4 s | 7.813° → 1.492° |

All 40 frozen timing intervals covered in this screen. These are conditional
engineering likelihood intervals, **not calibrated universal guarantees**;
the internal noise/search tail setting is not a proven false-alarm rate with
estimated noise and a conditioned clock. Point-error precision and reported
uncertainty must not be conflated. In particular this does not establish
25 µs or 125 µs reliable timing at 180 dB. At that loss no individual dwell
qualified: the sensitivity gain comes from accumulated inversion evidence,
not a claimed precise new marker measurement on every hop. The existing
±8 ms prior alone also does not count as newly measured 10 ms timing.

At 180 dB, wrong-hop sample assignments fell from 41,730 to 16,483 across
7,680,383 emitted samples in each arm. Every clean output received phase and
clock correction in both arms; no output was removed to obtain the gain.
Beginning/interior/end clean-twin phase RMS changed from
20.432° / 3.559° / 3.980° to 13.114° / 3.559° / 3.657°.
The boundary-region RMS changed from 14.717° to 9.625°. Residual errors near
hops remain substantial even with better timing. The frequency snapshot was
identical in all 40 matched pairs; this timing method supplies no additional
evidence for a narrower frequency interval.

#### Fresh 180 dB holdouts and complete output

After the screen, the unchanged estimator was evaluated on 32 fresh reserved
keys 1930100001–1930100032, again with distinct noise seeds +20,000,000.
All 32 calendar estimates qualified at 36 seconds and all intervals covered:
timing RMS **441 µs**, worst **965 µs**, median conditional half-width
**±2.050 ms** (range ±1.919–3.049 ms). Point errors met 25 µs / 125 µs /
1 ms / 10 ms in 0 / 8 / 32 / 32 cases; reported half-widths met those scales
in 0 / 0 / 0 / 32 cases. The 125 µs grid and 250 µs bins themselves contribute
a 187.5 µs discretization allowance before likelihood and clock uncertainty.
This configuration intentionally does not promise microsecond precision.

First calendar qualification occurred at 6.4–19.6 seconds, median 9.4 seconds.
There were no independently qualified individual markers at 180 dB. All
2,375 qualified rolling calendar updates covered, with worst admitted error
1.964 ms; these share history and are **not** 2,375 independent trials. A
two-second buffer cannot retroactively change samples released before this
weak-signal acquisition completes. Their original best-effort correction and
uncertainty remain in the complete-output measurements.

| Complete-output metric, 32 matched captures | Nominal calendar | Pooled inversion |
| --- | ---: | ---: |
| Wrong-hop output samples | 166,929 | 76,553 |
| All-output clean-twin phase RMS | 8.843° | 6.859° |
| Beginning / interior / end RMS | 20.074° / 4.5255° / 4.805° | 14.045° / 4.5256° / 4.575° |
| Boundary-region RMS | 14.593° | 10.443° |
| Within 0.5 ms of the physical hop, RMS | 72.076° | 62.396° |
| 0.5–2 ms from the physical hop, RMS | 68.552° | 38.734° |
| 2–8 ms from the physical hop, RMS | 18.415° | 14.358° |
| Worst complete-output phase error | approximately 180° | approximately 180° |
| Emitted samples with phase and clock applied | 30,721,545 | 30,721,545 |
| Conservative `well_supported` label | 6,069,369 | 6,069,369 |
| Timestamp / reference errors | 0 / 0 | 0 / 0 |

All captured input is processed; confidence does not remove uncertain samples.
Undefined phase at 3,451 pretransmission/zero-clean-energy samples is counted
explicitly rather than scored as zero phase error. Region counts differ by
one interpolated sample at one boundary. The unchanged large interior sample
set has effectively unchanged accuracy, so the improvement is from boundary
assignment, not discarding difficult observations. The median of each case's
boundary p95/p99 was 9.0°/78.125° for nominal versus 7.875°/20.125° for pooled;
these are **case-percentile medians**, not a fabricated pooled percentile.
The embedded v8 common-support diagnostic is available in only 2/32 cases
(42,078 samples); it is not a full direct nominal/profile intersection study.

Fresh local pilot phase RMS/worst remained 2.625°/12.713° across 3,200 dwells.
Maximum evaluated relative-clock error remained 19.45 µs, with zero observed
timing-cell slips in these clean cases. The unchanged frozen frequency result
was available and covered in 31/32 cases: RMS 0.01357 Hz, worst 0.02887 Hz,
median conditional half-width ±0.11695 Hz. Key 1930100026 was unavailable.
This does not establish generally reliable ±0.1 Hz bounds, and that approximate
goal does not gate complete-stream delivery.

The held-out tone at capture dates 38–40 seconds improved clean-twin phase RMS
5.843°→3.983°, boundary-region RMS 9.926°→6.125° and boundary fixed-lag
per-dwell residual frequency RMS 0.20287→0.07903 Hz. Interior residual
frequency RMS stayed 0.01320 Hz. The noisy composite's sample-phase RMS stayed
about 90°; it is not the clean-twin tracking measurement. A separate matched
40 W, 360 Hz shaped APSK probe check on key 1930000001 gave clean-twin EVM
5.796%→5.144% at 180 dB, while noisy composite EVM was 55.321%→55.516%.
At 160 dB, clean/noisy EVM changed 3.376%/6.276%→1.855%/5.809%.
All 584 evaluable symbols were used with one fixed evaluator reference;
the interpolation endpoint exclusions (5 and 4 samples respectively) are
reported separately. Neither probe supplies synchronization or alignment.
No oracle supplies receiver decisions, and this is not a decoding result.

#### Limits, controls and latency

Thirteen matched impairment pairs used the reserved 1930200001–1930200004
keys, including the previously unseen 23 ms phase pulse, 51 ms gain change
and 61 ms frequency-settling combinations. Their wrong-hop counts decreased
5,218→1,965, 5,248→1,581 and 5,256→2,330 respectively, without claiming that
calendar timing repaired the physical disturbances. All 896 admitted rolling
calendar intervals covered across these tests, again with correlated history.
The original 48 ms transient and whole-cycle/differential-phase counterexamples
remain in the unchanged fixtures and focused tests.
The same three transient combinations were also compared at 160 dB. Wrong-hop
counts changed 5,199→212, 5,215→200 and 5,206→490. All 300 qualified individual
intervals and all 220 qualified rolling calendar intervals covered, but worst
individual error reached 1.288 ms. Strong clean pooled precision is therefore
not a promise of equal precision for every disturbed individual dwell.

Four separately reserved keys 1930200005–1930200008 each tested a straight
auxiliary, absent auxiliary and noise-only capture at 160 and 180 dB:
**0 individual qualifications, 0 pooled qualifications and 0 selected
boundaries in all 24 full-duration controls**. This is measured finite control
coverage, not proof of a universal false-alarm probability.

Important failures and assumptions remain:

- The marker locates the transmitted calendar. An independent RF retune 2 ms
  early worsened wrong-hop assignments 1,224→4,976 and complete-stream phase
  RMS 5.40°→8.31°, despite accurate marker timing. A 2 ms late retune improved
  9,941→5,426 but retained the independent displacement. RF retune delay must
  be known, measured separately or kept uncertain; a marker is not proof of
  the physical RF transition date.
- A 9.8 ms arrival outside the ±8 ms search prior selected no boundaries. Its
  edge optimum was explicitly unqualified, not reported as covered timing.
- Curvature and whole-cycle gap cases lost pooled-clock qualification.
  Curvature, gap-cycle and frequency-step cases retained severe held-out
  phase errors (roughly 116°/119°/103°). Arbitrary differential pilot phase
  can imitate clock changes; this detector has not generally solved that
  ambiguity. No new clock evolution model was introduced in this iteration.
- The missing-ADC case retained its explicit 9,600-sample loss and released
  all other captured samples, including 78 with no available phase correction.
  UTC discontinuity tests preserved capture-time pattern lookup and reference.
  No new absolute source-time reference was silently selected.

On the single matched HF key 1930000001, delays 0.4 / 1 / 2 / 3 seconds gave
wrong-hop counts 3,226 / 3,117 / 3,029 / 2,836, and complete clean phase RMS
7.059° / 7.280° / 6.531° / 6.481°. More delay did not uniformly improve every
metric. Actual capture-to-release ranges were about 0.4–1.2 / 1.2–1.6 /
2.0–2.8 / 3.2–3.6 seconds because processing and release use complete dwells
and boundary look-ahead. The frozen 36-second timing estimate was identical
at all delays. Later evidence only changes still-buffered output, whose
original capture dates and latest observation dates remain explicit.

One matched case each of the existing 5.8 GHz FCC100 and EU20 wide-hop
geometries qualified at 36 seconds with approximately −1.213 ms timing error
inside ±2.117 ms conditional intervals. Whole-stream phase RMS changed
9.210°→7.928° and 9.261°→7.987°. This small fixture check is not broad
microwave qualification or regulatory certification. Removing pilot ramps
also retained the pooled result at 180 and 160 dB on key 1930000001; those
waveform variants retain the standard power normalization, so their changed
total energy is explicitly labeled rather than called identical 40 W links.

#### Work bounds, checks and reproduction

The detector has fixed 267,408-byte storage, at most 96 retained data profiles,
129 timing candidates, three residual-frequency candidates and 384 correlation
bins. Method stack frames were separately checked below 16 KiB in optimized
compilation. The adapter's individual fallback ring has 16 records. It borrows
the already-buffered aperture instead of copying the full stream. The existing
complete conditioner allocates 1,847,632 bytes with its 115,200-sample,
12-block maximum (4.8 seconds at 24 kHz); this is not a claim that all receiver
or evaluator memory is included in that allocation figure.

Three serialized runs of each method on key 1930100001, after all task build
and simulation jobs finished, measured total process CPU across threads.
Candidate tracking and history, **including inversion observation**, consumed
0.336–0.374 CPU seconds over exactly 36 seconds. Its median components were
0.2109 seconds for the existing measurement/history path and 0.1374 seconds
for inversion timing. Earlier runs with a background sanitizer build consumed
0.393–0.435 seconds; both sets are retained, not presented as hardware certification.
This meets the preferred 0.5-second **tracking** scale in these measurements;
it does not establish 0.5 seconds for the entire conditioning path.

At the 36-second capture frontier, 34 seconds of delayed input had actually
been conditioned: total receiver CPU including that conditioning and output
instrumentation was 0.592–0.654 seconds (median 0.616). For the complete
40-second capture plus drain, median total receiver CPU was 0.703 seconds,
versus 0.507 seconds nominal. Complete 40-second component medians were:

| Component | CPU seconds | Signal processed |
| --- | ---: | --- |
| Pilot startup / aided tracking / fallback | 0.0222 / 0.2039 / 0 | 40 s capture, existing guarded pilot apertures |
| Local fit / joint history | 0.00107 / 0.00579 | Same 40 s capture; implementation counters overlap portions of receiver total and must not all be added again |
| Inversion observation and pooling | 0.1542 | 100 windows over 40 s, 7.996083 s of actual retained aperture samples |
| Full-stream buffer/conditioning/provider/consumer | 0.3158 | All 40 s of captured input, including drain |
| Of that conditioning: profile release queries | 0.0249 | Bounded retrospective calendar queries; already included above |
| Of that conditioning: buffer push | 0.00142 | All 40 s; already included above |
| Simulation generation | 0.3752 | 40 s capture, excluded from receiver CPU |
| Extra evaluator-only synthetic probe generation | 0.0288 | Pre-probe clean twin, excluded from receiver CPU |
| Clean-twin conditioning / metrology | 0.2820 / 0.4745 | Complete evaluated capture and held-out probes, excluded from receiver CPU |

The 144 matched loss/holdout runs, 56 impairment/control runs and delay/RF/probe
variants used frozen binary SHA-256
`df88e4cfa9c93c7338ad15feed3fca6a4f561daf796c68990425d77c6c8ece98`.
Afterward, only work accounting was corrected to count apertures rejected by
the frequency witness, and exact 36-second CPU partitions were added. The
accounting build SHA is
`049d8b01b1c41df9260c8d58c5b15accd46af73d4710c1839770ae8e90b16e67`.
Every pre-existing non-CPU output field matched the frozen binary on the
complete nominal/profile comparison for key 1930100001. No estimator,
threshold, selection or correction changed after the held-out results.

The 17 focused Helper checks passed. The new component and actual-chain
regression passed again after accounting changes. The regression covers weak
pooled acquisition, stronger individual fallback, unchanged frozen frequency,
real complete-output correction, original timestamps and absent/straight-marker
controls. The original baselines and their tests remain intact.
Both focused ASan/UBSan/LeakSanitizer checks (`helper_inversion_profile` and
`helper_complete_conditioner`) also passed. The first sandboxed invocation
failed because LeakSanitizer cannot operate under the sandbox's process tracer;
the same two synthetic tests passed outside that restriction, with leak
detection retained. No audio hardware was accessed.

```sh
CCACHE_DIR=/tmp/pumpmodem-helper-ccache ./build.sh configure
CCACHE_DIR=/tmp/pumpmodem-helper-ccache cmake --build build/dev --parallel 2 \
  --target test_helper_inversion_profile test_helper_complete_study test_helper_transient_fixture
ctest --test-dir build/dev --output-on-failure --parallel 1 \
  -R '^(helper_inversion_profile|helper_complete_profile_regression|helper_transient_fixture)$'

# Single matched complete-chain capture. Both use Helper 40 W plus probe 40 W.
build/dev/test_helper_complete_study full 180 1930100001 clean tone 2 hf nominal
build/dev/test_helper_complete_study full 180 1930100001 clean tone 2 hf pooled-inversion

# Reproduce the eight-case loss screen; use 1930100001..1930100032 at 180 dB
# for the recorded 32-case holdout comparison (now regression cases).
for loss in 180 175 170 165 160; do
  for seed in $(seq 1930000001 1930000008); do
    for boundary in nominal pooled-inversion; do
      build/dev/test_helper_complete_study full "$loss" "$seed" clean tone 2 hf "$boundary"
    done
  done
done
```

Change `tone` to `apsk`, delay to `0.4`, `1` or `3`, RF geometry to
`microwave`/`microwave-eu`, or mode to `straight-auxiliary`, `no-auxiliary`,
`noise`, `adc-gap`, `clock-adjust`, `curvature`, `gap-clock-cycle`,
`differential-large`, `rf-retune-early`, `rf-retune-late`, or
`transient-inversion-held-phase23`/`gain51`/`settle61` for the corresponding
checks. The repository's
[evidence package](validation-data/helper/2026-10-31/README.md) retains verified
reservations, exact arguments, selected per-case results, failure/control cases,
CPU repeats and available provenance. Run `python3 tools/helper-evidence.py verify`
or `summary` to inspect it without rebuilding or reading ignored files.
Raw logs and omitted debug rows remain in ignored
`local-evidence/helper-continuation/inversion-profile/`; they are not required
to use the documented techniques or recompute the retained comparisons.
No ordinary Robust integration, full regression campaign, GUI change, audio
hardware access, successful decoding claim or physical sensitivity bound is
part of this work.

## Covariance for the existing four-window estimator

## Covariance for the existing four-window estimator

The original estimator derives each pilot's frequency from four sub-aperture
phase observations. The optional `compute_slope_covariance` path retains its
existing frequency points, phase points, acquisition gates and legacy marginal
errors, while adding bounded work to describe the local estimator covariance.
The default remains unchanged when this option is disabled.

The thermal covariance uses the actual sample influence functions, including
the derivatives of the adaptive guarded-power weights and the overlap between
quarter-window correlations and the common full-window center-phase fit. It
therefore retains frequency/phase and cross-pilot terms instead of assuming
independent pilot measurements. It is a first-order local white-noise model;
its plug-in weights, data-dependent selection, colored interference and
frequency-selective paths still need separate validation. The local PSD
allowance remains explicit and is not a calibrated false-alarm guarantee.

The opt-in result also supplies a separate common-oscillator process covariance.
Adaptive high- and low-pilot weighting can give different Wiener-phase influence
functions, so their process errors cannot generally be represented by a single
identical rank-one frequency error. The joint estimator accepts a validated
positive-semidefinite process block as a replacement for its legacy scalar
common-process approximation, rather than adding both. Thermal and process
uncertainty remain separate.

Adaptive high and low frequency estimates can have different effective dates.
The joint fit uses those dates for the corresponding frequency rows, while the
pilot phase difference remains dated at the shared center. The dates are also
data dependent: the opt-in interface has a declared within-aperture maximum
frequency-drift rate, and reports its duration-scaled contribution as a
separate bounded model radius. A zero bound explicitly assumes constant
frequency within the aperture apart from the separately modeled common Wiener
phase; a caller must not treat it as an unspecified or measured drift bound.
The current HF comparison declares 0.001 Hz/s and the microwave comparison
0.125 Hz/s as operating assumptions, rather than selecting a bound from the
simulation truth.

The joint fit consumes the supplied frequencies and simultaneous pilot phase
difference once, including their covariance; predictions are not additional
measurements. It keeps the unknown phase-difference intercept and one fixed
relative source-time reference. Its conditional frequency interval retains the
recent-history consistency guard and reports stochastic and bounded-model
contributions separately. It remains conditional on the supplied covariance,
clock and bounded channel-transfer models. Independent evidence is needed to
separate arbitrary differential pilot-channel phase changes from audio-clock
changes.

## Matched simulation design

The continuation comparison runs the original four-window receiver, the same
receiver with covariance enabled, a joint estimator using that covariance, and
the partitioned-Hann joint/dual-phase comparator from the earlier study. The
covariance-only variant separates the cost of producing the summary from any
change in the underlying frequency estimate. The optional dual-pilot phase
combination uses a previous-observation prediction; unavailable or inconsistent
blend evidence leaves a valid high-pilot-only phase as the fallback.

The tone and 360 Hz shaped APSK probe never determine receiver synchronization,
correction coefficients, branch selection or adaptive gain, phase or timing.
The noisy Helper determines the correction applied to both the noisy probe and
its evaluator-only clean twin. Relative operation retains one constant receiver
reference, with its corresponding simulator-clock gauge used only for evaluation.
The evaluation reports support and per-hop errors without fitting a new gauge
per hop, window or symbol. An evaluator-only oracle is timed separately and is
not fed back to the receiver.

Receiver process CPU includes all receiver threads and is reported separately
through 36 seconds and through the 40-second capture. Acquisition, tracking,
fallback, covariance/joint work and actual sample-conditioning cost must be
interpreted with their reported signal durations. Simulation generation,
clean-twin/oracle processing and metrology are separate from receiver CPU;
conditioning the held-out probe interval does not establish the cost of
conditioning an entire stream.

## Matched 40 W results

The following clean HF examples use the actual Helper waveform, a 40 W Helper
and a separately powered 40 W probe, 290 K input noise reference and 10 dB
noise figure. The modeled relative RF error is +0.8 ppm, transmit and receive
audio-clock errors are +37 and −13 ppm, and common phase diffusion is
0.05°/√s. The declared within-dwell frequency-drift bound is 0.001 Hz/s. All
methods see the same keyed-random hop schedule and impaired capture per seed.
The flat common-transfer and bounded-model assumptions above apply.

For 32 matched seeds at 180 dB, all four listed methods qualified a 36-second
frequency interval and all 32 intervals covered the simulated frequency. The
reported widths are conditional engineering intervals, not calibrated coverage
guarantees. Phase RMS is the median and worst across per-seed clean-twin RMS
values, after removing one fixed evaluator gauge; the window count retains
unavailable cases in its denominator.

| Method | 36 s frequency error RMS | Conditional half-width min / median / max | Clean-tone phase RMS median / worst | Full held-out windows |
| --- | ---: | ---: | ---: | ---: |
| Four-window baseline, relative clock | 0.01658 Hz | ±0.20048 / 0.21393 / 0.23574 Hz | 3.08° / 6.34° | 160/160 |
| Four-window, covariance, joint fit and dual phase | 0.01762 Hz | ±0.10614 / 0.12211 / 0.15166 Hz | 2.30° / 4.44° | 159/160 |
| Partitioned-Hann, joint fit and dual phase | 0.01783 Hz | ±0.11354 / 0.12520 / 0.15567 Hz | 2.29° / 4.80° | 158/160 |

In these examples the four-window covariance and joint fit narrow the reported
interval compared with the original guarded fit, while the measured point-error
RMS remains comparable. The largest qualified half-width is still above
±0.1 Hz at 180 dB. The partitioned comparison uses its separately modeled taper
covariance; neither finite result establishes a universal estimator ranking or
physical sensitivity limit. The covariance-only four-window path produced the
same point errors and interval widths as the baseline, separating measurement
cost from the later joint-fit change.

For the four-window joint/dual method, the median reported thermal frequency
sigma at 36 seconds was 0.02445 Hz; common-process sigma was about 0.000059 Hz,
the bounded-model half-width was 0.00121 Hz, and the recent-half enclosure
added 0.04553 Hz to the conditional interval. These are modeled components,
not independently calibrated coverage. Its worst admitted clean-sample phase
error was 8.09°, the largest evaluator-only time-map error after removing the
fixed gauge was 7.67 µs, and no admitted cycle slip was observed. The median
of the per-seed fresh-phase 99th percentiles was 6.61°. The largest per-seed
clean-twin frequency RMS across later supported windows was 0.03894 Hz. The
median noisy-tone frequency RMS was 0.09026 Hz with median residual-fit
standard error 0.10761 Hz. These later measurements cannot narrow the frozen
36-second interval retroactively.

For the 32 matched APSK examples at 180 dB, the four-window joint fit with
high-pilot-only phase had noisy EVM median/worst 0.55158/0.59129 and clean-twin
EVM 0.05585/0.10968. The dual-pilot phase path had noisy EVM 0.54928/0.59060
and clean-twin EVM 0.04183/0.07706; the evaluator-only oracle had noisy EVM
median/worst 0.54971/0.58815. Each joint path retained 159/160 later anchors;
individual cases contributed 265–332 supported symbols. These are fixed-gauge
waveform measurements, not decoding results. The clean twin reveals a tracking
difference while noisy EVM is close to the oracle in this modeled link.

The same joint/dual APSK path was also screened with eight seeds each at 175,
170, 165 and 160 dB. All eight frozen frequency intervals covered simulated
truth and all 40 later anchors were supported at each level; every run supplied
at least 331 supported symbols. The noisy EVM median/worst values were
0.31041/0.33210, 0.17545/0.18706, 0.09909/0.10542 and 0.05631/0.05980
respectively. Corresponding clean-twin values were 0.02483/0.04049,
0.02067/0.03271, 0.01471/0.02204 and 0.01204/0.01519. These are matched
waveform measurements under the stated model, not APSK or Robust decoding
claims.

In the final-source clean replay before the separate covariance arithmetic
optimization below, the four-window joint/dual path used median/maximum
0.13995/0.15234 seconds of process CPU through 36 seconds and 0.18421/0.19505
seconds through 40 seconds, including approximately 2.4 seconds of probe
conditioning. The covariance-only path, which retains the original point
estimator and interval, used median 0.13692 seconds through 36 seconds versus
0.06369 seconds without covariance. Through 40 seconds, median acquisition,
correlation, joint/clock/dual and conditioning costs were approximately 0.0212,
0.1268, 0.0047 and 0.0280 seconds; other receiver work is included in the total.
Median simulation generation was 0.3210 seconds and separate tone metrology
0.0030 seconds, excluded from receiver CPU. These host-specific measurements do
not establish the cost of full-stream conditioning or a whole-receiver timing
for the subsequent isolated covariance optimization.

The initial loss screen used eight matched tone seeds at each of 175, 170, 165
and 160 dB, in addition to the 180 dB study. For the four-window joint/dual
path, all eight 36-second intervals at each level covered simulated truth and
all 40 later tone windows were supported. The largest conditional half-widths
were ±0.07087, ±0.04060, ±0.02335 and ±0.01358 Hz respectively. A further
eight-seed refinement at 176, 177, 178 and 179 dB gave half-width ranges of
±0.06786–0.07920, ±0.07595–0.08851, ±0.08501–0.09910 and
±0.09518–0.11096 Hz respectively; all reported intervals covered simulated
truth. In these samples the largest width crossed ±0.1 Hz between 178 and
179 dB. These results depend on the declared 0.001 Hz/s in-dwell drift bound
and other channel/model assumptions; they do not establish calibrated reliability
or a physical sensitivity boundary.


At 5.8 GHz and 180 dB, eight seeds were run with each wide-hop fixture. The
four-window joint/dual path qualified and covered seven of eight frozen
frequency intervals for FCC100 and eight of eight for EU20; the largest
qualified half-widths were ±0.21651 and ±0.22133 Hz respectively. All 40
later tone windows were supported in each profile; later phase support does not
make an unqualified 36-second estimate available. The partitioned comparator
qualified the same counts with largest widths ±0.14451 and ±0.14834 Hz, but
its allowance does not include the same opt-in random-date model bound. The
four-window fixture declares a 0.125 Hz/s in-dwell drift bound, so those widths
are not an equal-assumption precision comparison. Neither fixture establishes
regulatory or hardware compliance.


The baseline joint/dual path was also exercised without the optional recovery
experiment on four HF seeds per condition. At 180 dB, a 1.2 Hz post-observation
frequency step caused all four joint paths to suspend, with zero of 20 later
windows qualified. Imposed curvature produced no qualified 36-second result;
a two-dwell gap retained all 20 later windows, while a longer gap retained none.
A disturbance inside the 120 ms settled aperture left 13 of 20 windows
supported, with the affected hop withheld in these samples. Noise-only inputs
produced no qualified snapshot, and no admitted timing-cycle slips were
observed in these checks. The step result establishes withdrawal, not recovery.

A bounded ±0.02 rad differential pilot-phase variation retained 20/20 windows
in these sampled conditions. A ±0.8 rad change remained undetected at 180 dB
with all 20 windows supported; the largest admitted clean-sample phase error was
17.68°. At 160 dB, the same sampled disturbance suspended all four paths and
left zero later windows qualified. A channel phase outside the declared bound
can be indistinguishable from a clock change, particularly at low SNR; these
examples do not certify that the channel bound can always be detected.
Additional four-seed drift and reversed-audio-clock examples each covered the
36-second frequency and retained all 20 later windows under their declared
models. These are finite controls, not guarantees against arbitrary changes.

## Additional bounded diagnostics

An algebra-preserving optimization of the opt-in covariance calculation was
compared on 704 matched impaired captures: 631 covariance observations were
admitted and 73 declined. The point estimates, effective dates and gates were
identical, and the maximum diagonal-normalized covariance-element difference
was 1.51×10⁻¹⁴. The numerical stress includes common phase-diffusion inputs
from zero to 10¹⁵⁰ and amplitude scales of 2⁻¹⁰⁰ to 2¹⁰⁰; those extremes are
numerical checks, not physical operating conditions. The reproducer sources
are retained in ignored `local-evidence/helper-covariance-opt/` and are not
portable version-controlled fixtures.

In five interleaved isolated component benchmark runs, each processing 100
0.36-second dwells with 99/100 admitted, median process CPU was 0.146559 seconds
before the optimization and 0.133297 seconds after it. These figures exclude
startup, sample conditioning, simulation generation and metrology. They do not
measure a whole receiver, and the earlier 36- and 40-second receiver CPU figures
are not extrapolations from this component benchmark.

A separate diagnosis examined the three step-trial seeds that did not recover
in the first 32-seed study. Each proposed an epoch, but fewer than three valid
full-covariance observations arrived before the two-second detector-only prior
expired. The prior supplied a bounded diagnostic coordinate, not correction
authority. No branch-fit failure was established in those cases; this diagnosis
does not prove that extending a prior or changing gap handling would be safe.

## Bounded change detection and recovery

`HelperFrequencyRecovery` is a bounded detector for a single declared common
frequency change. It compares fresh pilot measurements with a dated prediction
from the preceding Helper model. Its candidate correction is a provisional
acquisition coordinate and epoch request, not a phase authority, posterior
pseudo-measurement or additional observation. The detector retains a fixed
reference, requires bounded fresh evidence, checks pilot-difference consistency,
and keeps the uncertainty shared by the old prediction from shrinking as if it
were new independent noise. Missing input is distinct from evidence that the
reference was lost.

An averaged aperture near a change can contain both old and new frequencies.
The detector's safe change bracket includes the boundary apertures; a later fit
must not replay a potentially mixed frequency or phase summary as a pure
observation. The detector's additive-Hz interpretation is a declared model; it
does not by itself identify whether a physical change was additive or scaled
with the commanded RF frequency. A selected epoch must preserve the original
phase/time reference, avoid counting reused observations twice, and withhold
correction when the model or retained ambiguity is not qualified. Detection
alone is not proof of useful end-to-end recovery.


The optional joint recovery experiment retains the original raw Helper
observations and phase-difference reference. It introduces one explicitly
selected additive-common-Hz step term, with the detector's conservative change
bracket; potentially mixed transition apertures are excluded. The detector
proposal is not ingested as another data point. The original 36-second snapshot
is frozen before any post-observation recovery. The same fresh measurements may
be used to establish and fit the selected epoch only once; the conditional
interval remains subject to the selected step, clock and channel models.

For 32 seeded 180 dB trials with a modeled 1.2 Hz additive step after the
36-second observation, the recovery variant accepted an epoch in 29/32 cases
and supported 116/160 held-out tone windows: four windows in each accepted
case. The original joint path without this option supported 0/160 windows in
the matched cases. Among the recovery windows, the largest clean-twin
window-frequency error was 0.09071 Hz and the largest admitted clean-sample
phase error was 12.95°; no admitted cycle slip was observed. All 32 frozen
36-second frequency intervals covered the simulated pre-step frequency. The
step selection and its interval are conditional engineering results, not
calibrated detection or coverage probabilities. No independent probe or true
step value was used to choose the epoch.

A separate 32-seed HF test predeclared an RF-scaled step model: the step was
1.2 Hz at 30 MHz and scaled with the commanded hop frequency. All 32 frozen
intervals covered the simulated pre-step frequency; 29 cases accepted an epoch
and recovered 116/160 later windows, four per accepted case. The largest
admitted clean-window frequency error was 0.09273 Hz, the largest admitted
sample-phase error was 12.98°, and maximum relative time-map error was 7.98 µs;
no admitted cycle slips were observed. The no-recovery joint path supported
zero later windows. The RF-scaled model was chosen before the observations;
the detector did not infer it by selecting the better fit. The modeled phase
integrates the keyed hop boundaries. The synthetic step wrapper rotates the
noise with the signal, so the scaled-step and clean runs do not share identical
noise samples, although methods within each individual run use the same capture.
Median/maximum process CPU was 0.145/0.184 seconds through 36 seconds and
0.185/0.240 seconds through 40 seconds with variable partial conditioning.
These are finite model-dependent samples, not a general RF oscillator recovery
guarantee.

An eight-seed 180 dB check used the same 1.2 Hz step with the separately powered
40 W shaped APSK probe. The recovery path accepted an epoch in all eight cases,
retained 32/40 later anchors and evaluated at least 265 supported symbols per
run. Median noisy and clean-twin EVM were 0.55245 and 0.04366, with no admitted
cycle slips. The same-capture joint path without recovery supported 0/40
anchors. The ordinary nonjoint path had a different supported subset, so its EVM
is not an equal-symbol comparison. These are waveform diagnostics, not decoding
claims or evidence that all step cases will recover.

The corresponding eight-seed APSK check with a predeclared RF-scaled step also
accepted eight epochs and retained 32/40 anchors, with 265 evaluated symbols
per run and no admitted cycle slips. Median noisy EVM was 0.55249 and median
clean-twin EVM was 0.04382; the same-capture evaluator-only oracle's noisy EVM
median was 0.54911. The Helper and probe are independently specified 40 W
signals, totaling 80 W nominal power during the probe interval. The detector
itself cannot identify the physical step model; this check declares it in
advance.

A separate 32-seed clean run proposed and accepted no step epochs. It supported
159/160 later windows, retained the reference and had no admitted cycle slips;
all 32 frozen frequency intervals covered the simulated frequency. Through 36
seconds, median/maximum recovery-receiver CPU was 0.1449/0.1732 seconds. In
the additive-step trials, the corresponding values were 0.1436/0.1853 seconds;
through 40 seconds, median/maximum was 0.1828/0.2293 seconds and included only
the actual, sometimes partial, held-out conditioning. Process CPU includes the
receiver threads; simulation, clean twin, oracle and metrology are separate.

In a separate 32-seed control, a 1.2 Hz frequency pulse lasted only one dwell
before returning to the prior frequency. The recovery detector proposed and
accepted zero step epochs, and 122/160 later windows were supported. The largest
supported clean-twin window-frequency error was 0.04525 Hz and largest admitted
sample-phase error 8.19°, with no admitted slips; all frozen intervals covered
truth. The pulse wrapper rotates the noise with the signal, so clean and pulse
runs do not have identical noise samples, although the modeled circular noise
distribution is preserved. Within each case, the compared methods still use the
same capture. This is a finite pulse stress, not a guarantee against arbitrary
multiple changes or phase-selective fading.

Four-seed controls of the optional recovery path at 180 dB retained all 20
windows after a short gap but none after a long gap. Curvature produced no
qualified 36-second result or later window. The 120 ms retune stress retained
13/20 windows; the affected hop was withheld in these samples. Noise-only
input did not qualify a snapshot. Bounded differential pilot phase, modeled
drift and reversed clock errors each retained 20/20 later windows. The large
±0.8 rad differential-phase change was not detected at this loss, and all 20
windows remained supported with a worst admitted clean-sample phase error of
17.68°. Thus the declared channel-phase bound remains an assumption, not a
guaranteed detector. No false recovery epoch or admitted slip was reported in
these sampled non-step controls.

The additive-step recovery path was also tested at 180 dB with four seeds per
5.8 GHz profile. FCC100 qualified and covered three of four frozen frequency
intervals, accepted recovery in two cases and supported 8/20 later windows; the
largest supported clean-window frequency and sample-phase errors were 0.05834 Hz
and 5.61°. EU20 qualified and covered all four frozen intervals, accepted three
recoveries and supported 12/20 later windows, with corresponding maxima of
0.06236 Hz and 7.03°. No admitted slip was observed. These small tests use the
declared additive model and do not identify the physical step mechanism,
qualify actual hardware or establish regulatory compliance.

With a predeclared RF-scaled step, four seeds per microwave fixture produced
similar finite-sample availability. FCC100 qualified and covered three of four
frozen intervals, accepted two epochs and supported 8/20 windows; the largest
supported clean-window frequency error was 0.05847 Hz and sample-phase error
5.60°. EU20 qualified and covered all four, accepted three epochs and supported
12/20 windows, with corresponding maxima of 0.06161 Hz and 7.03°. Neither had
an admitted cycle slip. These are model-dependent simulations rather than
hardware, propagation or regulatory qualification.

## Opt-in gap and extended-prior experiments

Two separate experimental options extend the preceding baseline. The gap option
retains the original relative timing/phase reference across a missing interval
of at most 12 seconds, assuming audio-clock evolution within 0.2 ppm/s. The
old branch restricts the alternatives considered but cannot authorize a
prediction: at least three fresh observations spanning 0.8 seconds must qualify
the branch before correction resumes. The extended recovery option increases the
detector-only prior age from two to three seconds under a declared absolute
frequency-curvature bound of 0.002 Hz/s². That prior supplies an acquisition
coordinate, not a new measurement or correction authority; reused observations
are not counted as independent evidence. Neither option establishes its model
assumptions from the probe, and the gap and step recovery options have not been
combined into a qualified gap-plus-step solution.

At 180 dB loss in the 30 MHz HF fixture, 32 matched long-gap tone trials
supported 126/160 later windows with the gap option, versus 0/160 without it.
The 9.6-second missing interval includes the 36-second cutoff, so no frozen
36-second frequency result was available. Over supported windows the largest
clean-twin frequency error was 0.082778 Hz, largest admitted sample-phase error
9.193°, and largest timing error 11.302 µs; no admitted cycle slips were
observed. The same 32 seeded trials with a declared 1.2 Hz additive step after
36 seconds and the extended prior accepted
31/32 epochs and supported 124/160 windows, compared with 29/32 and 116/160
for the two-second prior. All 32 frozen 36-second intervals covered the
pre-step frequency; their median and maximum half-widths were 0.12211 and
0.15166 Hz. The largest later clean-twin window-frequency error was 0.09288 Hz
and sample-phase error 12.947°, with no admitted slips. These results are
conditional on the declared models and are not certified coverage rates.

The limits of the gap assumption matter. In eight trials imposing a timing
cycle jump during the gap, 30/40 windows were admitted and all eight trials
had admitted cycle slips; the largest sample-phase and timing errors were
160.37° and 545.66 µs. In eight trials with out-of-bound 0.6 ppm/s clock
evolution, 32/40 windows were admitted, with no observed slips and a maximum
timing error of 21.839 µs; the violation was not reliably detected. Arbitrary
differential pilot-phase changes or timing-cycle jumps can imitate a clock
change and cannot be reliably detected here without independent evidence. A
step during the gap yielded 0/40 supported windows in eight trials. The gap
option should therefore be used only with a defensible physical evolution and
channel model, and its inability to detect these violations must be considered.

Eight matched HF APSK trials per case separately specified 40 W Helper and
40 W probe power, 80 W combined during the probe. The gap option supported
32/40 anchors and 265–266 symbols per run; median noisy, clean-twin and oracle
noisy EVM were 0.55267, 0.045288 and 0.54911 respectively. The extended step
option supported 32/40 anchors, with median noisy and clean-twin EVM of 0.55245
and 0.043658. These probe measurements do not provide synchronization, branch
selection or adaptive alignment. Eight-seed clean and one-dwell pulse controls
had no admitted slips or false epochs.

In eight seeds per 5.8 GHz fixture, the gap option supported 32/40 windows for
each of FCC100 and EU20, versus zero without it. For the step comparison the
extended prior supported 18/40 versus 16/40 FCC100 windows, and both variants
supported 20/40 EU20 windows. These are simulated fixture outcomes, not
hardware or regulatory qualification.

With the optimized covariance calculation, median/maximum total receiver
process CPU through 36 seconds and through 40 seconds was 0.13285/0.14059 and
0.17481/0.18205 seconds for clean input with the gap option; the 40-second
figure includes 2.4 seconds of held-out sample conditioning. For long-gap
input the corresponding figures were 0.10529/0.11829 and 0.13269/0.14530
seconds, including 1.6 seconds of held-out conditioning; 9.6 seconds of input
were absent before the 36-second cutoff. For the extended-prior step case they
were 0.13526/0.14382 and 0.17556/0.18334 seconds, including 1.6 seconds of
held-out conditioning. These process-CPU figures include all receiver threads
but exclude simulation generation and metrology. Partial conditioning does not
establish full-stream conditioning cost.

In the 32 extended-step tone trials, the median and maximum fresh held-out
per-hop phase RMS were 2.79° and 5.49°; the largest per-hop phase error was
9.15°. The median noisy-tone frequency RMS was 0.09287 Hz, with median fit
standard error 0.10830 Hz. These noisy-probe diagnostics are distinct from the
clean-twin maxima above. The median process-CPU components through the
40-second capture were 0.02155 seconds startup, 0.11570 seconds correlations,
0.00943 seconds fallback, 0.00078 seconds global fit, 0.00651 seconds
clock/joint/dual work, and 0.01892 seconds conditioning for 1.6 seconds of
supported held-out input. Median simulation generation and tone metrology
were 0.32227 and 0.00243 seconds respectively and are excluded from receiver
CPU. These component timings cover the stated capture and can overlap in their
accounting; the measured total, not their sum, is the receiver CPU figure.

## Independent seeds and estimator diagnostics

The following checks use HF seeds 1–32, whose key residues are disjoint from
the earlier seeds 901–932. Fixture keys repeat every 256 seeds: distinct keys
do not prove that every finite schedule prefix is unique. In clean, paired
cases at 180 dB, all 32 frozen 36-second intervals covered truth, with median
and maximum half-width 0.12278 and 0.15425 Hz. Frequency point-error RMS was
0.01932 Hz and maximum absolute error 0.06338 Hz; 159/160 later windows were
supported with no admitted slips. Eight held-out seeds each at 175, 170, 165
and 160 dB also had all frozen intervals cover truth; their respective maximum
half-widths were 0.07333, 0.04183, 0.02396 and 0.01387 Hz. For eight held-out
seeds per microwave fixture, all frozen intervals covered and all 40/40 later
windows were supported; maximum half-width was 0.20537 Hz for FCC100 and
0.20590 Hz for EU20. These finite-sample outcomes do not certify coverage or
establish a physical sensitivity limit.

The evaluator now reports paired results only when both methods retain the
same fixed reference and gauge, and intersects their supported sample masks;
it also reports per-dwell phase and marks `conditioning_comparable`. These
comparisons do not feed the receiver. In the 32 paired HF cases at 180 dB,
high-pilot-only versus dual-pilot median per-seed phase RMS was 3.281° versus
2.924°; common-support clean-tone sample phase RMS medians were 3.091° versus
2.486°. On 265–332 common APSK symbols, median noisy EVM was 0.54340 versus
0.54189, and clean-twin EVM 0.05451 versus 0.04327. The separately specified
40 W Helper and 40 W probe imply 80 W combined. This evaluates existing
methods and is not a new receiver algorithm or a probe-aided correction.

On the final source, a separate eight-seed clean HF dual-pilot run at 180 dB
and 40 W per signal measured total receiver process CPU through 36 seconds at
0.134647/0.144234 seconds median/maximum and through 40 seconds at
0.177357/0.188834 seconds. Through 40 seconds, median component timings were
0.021875 seconds startup, 0.118160 correlations, 0.004351 fallback, 0.000840
global estimation, 0.004868 joint estimation and 0.027680 conditioning. The
conditioning covered 2.4 seconds of held-out probe signal, not the full stream;
component categories can overlap. Simulation generation and metrology were
separate, at median 0.422091 and 0.003075 seconds respectively. Fresh-hop
phase RMS had a median of 2.889° per seed, the median per-seed 99th percentile
was 6.593°, and the maximum admitted sample phase error was 7.434°. The
median noisy tone-frequency RMS was 0.09381 Hz versus 0.01201 Hz for its
evaluator-only clean twin.

An eight-seed evaluator-only pilot oracle isolated local measurement error:
94–95 aided-valid dwells per case, with median local aided phase RMS 3.281°,
known-frequency ideal 2.693°, and ideal dual phase using the true differential
2.201°. Median local aided frequency RMS was 0.08633 Hz versus 0.06914 Hz for
the ideal. The oracle subtracts exact nuisance waveforms and the per-sample
path, knows the true frequencies and differential phase, and starts its
frequency search at truth plus 0.2 Hz. The comparison is against the local
global-prediction-aided measurement, not the full receiver; the probe is absent
before 38 seconds. This is neither a realizable estimator nor an information
bound.

An isolated taper study also used truth-derived predictions rather than a
deployed receiver. Among 126/127 matched draws, training clean phase RMS was
3.17° with Hann and 2.83° with Tukey α=0.5. Under held-out 48 ms settling,
Hann was 3.47°, Tukey 3.82°, and the fallback 3.64°. The witness missed 68/127
settling cases and falsely flagged 6/127 clean cases. No taper replacement was
adopted. A separate analytic Gaussian diagnostic, rather than a waveform
simulation, reproduced the existing dual-phase estimate to 3.33×10⁻¹⁶; with
0.4 Hz frequency-gain mismatch, its frequency RMS error was 0.162 Hz versus
0.120 Hz for high-only. This diagnostic was not integrated.

An evaluator-only candidate for tighter dynamics bounds was rejected. With
an additional 0.001 Hz/s² curvature, seed 903 had 0.081335 Hz error against a
±0.078592 Hz candidate interval and seed 906 had 0.096834 Hz against
±0.084336 Hz, although the existing intervals covered; the diagnostic gates
were 2.376 and 2.322 standard deviations respectively. Its model allowances
included an additional additive curvature bound of 4×10⁻⁵ Hz/s², endpoint
offset 0.006 Hz and slope 0.0007 Hz/s; these are not general total oscillator
bounds. It used white-noise and overlapping linear covariance assumptions.
The curvature wrapper rotates the noise with the signal, so methods within a
case use the same capture but clean versus curved cases do not have identical
noise samples. These diagnostics do not establish diminishing returns. The
admitted gap-cycle slips and model violations described above remain material.

A separate, unregistered ramp-feasibility experiment asked whether Helper
envelopes could resolve the gap-cycle ambiguity. It fits the 2400, 550 and
950 Hz Helper templates with independent complex nuisance coefficients on each
side of a hop. It does not use probe templates, symbols or timing, and the
independently powered 40 W probe is unmodeled interference. Even under
optimistic assumptions—exact pre-gap reference, clock, rate and retune
calendar; only two true alternatives; five 28 ms boundaries after 38 seconds;
and an independent RF calendar for the imposed timing jump—the eight held-out
seeds 1–8 produced three false jump selections in clean cases and two missed
jumps, for both tone and APSK probes. A one-seed, 100 dB bright positive control
selected both alternatives correctly. This experiment supplies no receiver
feedback, is not a deployed detector and makes no CPU claim; the original
cycle ambiguity remains unresolved.

## Dwell-transient fixture and exploratory alternatives

The registered `helper_transient_fixture` exercises Helper-only simulations
with signal-only disturbances on dwells 25, 50, 75 and 95, retaining the same
thermal-noise realization across matched methods. Seven profiles include the
original high-pilot additive 48 ms disturbance, separate composite-phase
disturbances of 15, 48 and 80 ms, 80 ms gain disturbance, and constant and
decaying frequency settling. These fixtures do not compose with differential
phase, noise-only or audio-clock rewriting modes. A discontinuity crossing an
aperture does not have a single true aperture frequency; the evaluator oracle
is only a midpoint reference. For affected admitted samples, reported phase
error includes the physical transient itself and cannot all be attributed to
receiver estimation error.

The short probe verifies 56 distinct reserved key sets; fixture keys repeat
every 256 seeds, and this is not an exhaustive historical schedule claim. The
eight tuning seeds were 16777250, 16777251, 16777252, 16777253, 16777254,
16777255, 16777257 and 16777258. In clean HF cases at 180, 175, 170, 165 and
160 dB loss, all eight frozen 36-second intervals qualified and covered at each
loss, and all 40/40 later windows were supported. The respective maximum
conditional half-widths were 0.1290, 0.0743, 0.0425, 0.0245 and 0.0142 Hz.

An independent 32-seed clean HF tone and APSK check used distinct full noise
seeds and schedule key bytes held out from the scanned inventory. All 32
frozen intervals covered for both probes; point-error RMS was 0.0163052 Hz,
maximum absolute error 0.0327931 Hz, and median/maximum conditional half-width
0.122673/0.144864 Hz. Tone supported 160/160 windows and APSK 160/160 anchors,
with no admitted slips and maximum timing error 7.587 µs. Median per-seed
fresh-phase RMS was 2.8795° and median per-seed 99th percentile 6.948°; worst
admitted sample phase error was 8.217°. Median clean-twin tone-frequency RMS
was 0.01183 Hz versus 0.10532 Hz for the noisy probe. On the same 331 APSK
symbols per case, high-only to dual-pilot median noisy EVM changed from
0.544101 to 0.544233, while clean-twin EVM changed from 0.055171 to 0.046852.
The noisy result does not demonstrate a gain. This check did not change the
receiver algorithm.

At 180 dB the tuning transient screen showed:

| Signal disturbance | Supported later windows | Largest admitted clean-sample phase error |
| --- | ---: | ---: |
| High-pilot additive, 48 ms | 40/40 | 7.005° |
| Composite phase, 15 ms | 40/40 | 54.342° |
| Composite phase, 48 ms | 38/40 | 86.984° |
| Composite phase, 80 ms | 32/40 | 7.49° |
| Gain, 80 ms | 40/40 | 8.895° |
| Constant frequency settling, 60 ms | 1/40 | 4.586° |
| Decaying frequency settling | 40/40 | 29.695° |

The constant-frequency case recorded 32 contradictions; the decaying case
recorded none and had maximum supported residual tone frequency 0.2134 Hz.
These measurements do not establish that the admitted transient portions are
suitable for an ordinary receiver. Simulations retain the fixed 36-second
snapshot, 38-second lead and one relative reference, with 290 K thermal noise,
10 dB noise figure and the earlier oscillator assumptions. The independently
specified 40 W Helper and 40 W probe total 80 W during the probe. No GUI,
ordinary Robust receiver or audio hardware was involved.

On 32 fresh held-out HF tone seeds, the 48 ms high-pilot additive case
qualified and covered all 32 frozen intervals, supported 159/160 later windows
and had no admitted slips or model contradictions. Maximum timing error was
7.655 µs, worst admitted clean-sample phase error 8.273°, worst clean central
window frequency error 0.040660 Hz, and median noisy frequency RMS 0.10584 Hz.
The 48 ms composite-phase case likewise qualified and covered all 32 frozen
intervals, supported 152/160 windows, and recorded no admitted slips or model
contradictions. Maximum timing error was 7.913 µs, worst admitted clean-sample
phase error 91.731°, worst clean central-window frequency error 0.0364 Hz,
and median noisy frequency RMS 0.32959 Hz. The physical channel-phase pulse
is present in the probe and contributes to that admitted phase error. Median
common-sample clean phase RMS for high-only versus dual was 3.024° versus
2.654° for the additive case and 15.606° versus 15.515° for the composite
case. These are independent nuisance profiles, not tuning or proof of coverage
under arbitrary transients.

At 180 dB, eight reserved keys each for the FCC100 and EU20 5.8 GHz fixtures
gave 8/8 frozen interval coverage and 40/40 later windows in clean cases; the
maximum half-widths were 0.21119 and 0.21148 Hz. The high-pilot additive
48 ms case also covered 8/8 and supported 40/40 for both, with maximum
half-widths 0.21757 and 0.22114 Hz and largest admitted sample-phase errors
6.423° and 6.451°. The composite 48 ms pulse covered 8/8 for each and
supported 40/40 FCC100 and 39/40 EU20 windows, with maximum sample-phase
errors 85.685° and 92.023° and no admitted slips. The composite waveform pulse
is a physical perturbation, not an isolated estimator error. These fixture
geometries do not certify regulatory compliance.

Eight tuning-key controls per condition at 180 dB further expose the model
limits. Noise-only and curvature cases each qualified 0/8 frozen snapshots
and supported no later windows. Bounded differential phase covered 8/8 and
supported 40/40 windows, with maximum admitted phase error 7.115°. An
out-of-bound large differential change also covered 8/8 and supported 40/40,
with maximum admitted phase error 29.422° and no contradictions. Short gaps
covered 8/8 and supported 40/40; the retune-mid stress covered 8/8 and
supported 20/40. A step covered all frozen snapshots but the baseline
supported no later windows. In the whole-cycle gap stress, the optional gap
branch admitted 32/40 windows and all eight cases slipped, with worst phase
161.91° and timing error 545.267 µs; the baseline gap path supported no
windows. Model-contradiction checks are not reliable for arbitrary differential
phase or whole-cycle timing jumps.

Extended step recovery on the same eight tuning keys accepted 8/8 epochs and
supported 31/40 windows; all frozen intervals covered and no slips were
observed. Worst admitted clean-window frequency error was 0.1493 Hz and
sample-phase error 8.645°, so these cases do not support a post-step ±0.1 Hz
claim. Modeled drift and negative-clock controls each covered all eight frozen
intervals and supported 40/40 windows without slips; maximum admitted
sample-phase errors were 7.202° and 6.112° respectively.

Exploratory alternatives did not justify a receiver change. In six tuning
seeds and 24 affected dwells, three 120 ms calls per dwell qualified only 3/32
clean dwells versus 32/32 for the baseline. A piecewise fit used four 90 ms
segments, 31 frequency candidates and three split dates: at 48 ms it flagged
3/24 but left 16/23 admitted phase errors above 11.46°; at 80 ms it flagged
17/24 while all 19 admitted errors exceeded 11.46°, and frequency RMS rose
from 0.151 to 0.382 Hz. Its search coordinate used simulator truth plus an
offset; it was offline, outside the correction chain, and had no qualified
post-selection covariance. These tests reject these candidates under the
modeled cases, not every possible robust approach.

An offline waveform-level joint candidate on 664 common observations had
baseline/candidate frequency RMS of 0.08474/0.09281 Hz and phase RMS of
2.703°/2.714°. With a 0.4 Hz prediction-relation mismatch, candidate
frequency RMS was 0.16722 Hz and the pre-dwell gate rejected 0/666. Its
candidate covariance remains unvalidated, and no result entered the receiver
correction chain. Neither exploratory method was integrated. A separate oracle
run on the first eight reserved held-out HF keys had a median of 95 common
aided-valid dwells. Median local aided phase RMS was 3.298°, versus 2.723°
with exact known frequency and 2.250° for ideal dual phase with the true
differential. Median aided frequency RMS was 0.09559 Hz versus 0.07426 Hz for
the ideal profile. As above, the oracle knows unavailable nuisance waveforms,
the path, true frequencies and phase difference, and initializes its search at
truth plus 0.2 Hz; the local comparator uses global predictions. It supplies no
receiver feedback and is not an information bound.

On eight held-out clean HF keys at 180 dB and 40 W per signal, isolated
final-source receiver process CPU through 36 seconds was 0.132724/0.140243
seconds median/maximum, and through 40 seconds was 0.174932/0.181611 seconds.
The latter includes 2.4 seconds of held-out conditioning, not full-stream
conditioning. Through 40 seconds, median timings were 0.021638 seconds
startup, 0.117838 correlations, 0 fallback, 0.000824 global estimation,
0.004791 joint estimation and 0.027710 conditioning; fallback maximum was
0.00871 seconds. Categories may overlap, while total process CPU was directly
measured. Median simulation generation and metrology were 0.371904 and
0.003071 seconds, excluded from receiver CPU.

Another standalone timing-signature experiment changed the simulated
transmitter with a fixed coefficient; it did not change the production
waveform. Eight tuning seeds used independent hop phases and missing dwells
70–94, including a 32-second timing jump. Mean transmitter waveform power was
1.00001 times the baseline, with added tone energy 0.10000 times baseline and
maximum per-dwell power difference about 0.023%. An oracle branch scorer
knew exact source clock and frequency and compared two hypotheses. With stable
or +0.02 rad signature differential phase it made no false selections or
misses in eight cases each; with +2.717 rad differential it made eight false
and eight missed selections. These decisions were not supplied to either
actual gap tracker: both admitted cycle slips in all eight jump cases, and no
36-second snapshot was available because the gap crossed the cutoff.

For the no-jump case, actual-chain APSK clean-twin median EVM changed from
0.04992 to 0.05326 on 199 common symbols in seven of eight seeds; one seed
lacked a same-reference comparison. Supported anchors changed from 24/40 to
23/40. Fresh-phase RMS median changed from 2.953° to 3.136° on method-selected,
not common, dwells. The probe supplied no template and both Helper and
independent probe were 40 W. This is a conditional diagnostic without branch
correction, not a deployed improvement or a solution to arbitrary differential
phase ambiguity.

## Expanded-key dwell-v3 study

The simulation fixture now has an opt-in expanded schedule-key namespace;
legacy keys and schedules remain the default. The public test key combines the
24-byte prefix `HH-HELPER-FHSS-TEST-V3!!` with the full 64-bit seed and is
disjoint from the 256 legacy keys. The `hf-v3`, `microwave-v3` and
`microwave-eu-v3` case profiles select it. The short probe checks 56 new keys
against all legacy keys and prior reservations. These keys are fresh relative
to the scanned records, not necessarily every unlogged historical experiment;
distinct keys do not prove that finite generated schedule prefixes differ.
The tuning seeds were 1666973697–1666973704, held-out HF seeds
1667039233–1667039264, FCC seeds 1667104769–1667104776 and EU seeds
1667170305–1667170312.

Evaluator-only output now includes counterfactual oscillator phase before
impairment, a physical-disturbance mask enlarged by the actual resampler halo
and retained frequency-excursion phase, and each held-out dwell's supported
sample count and first/last input spans. A first/last span does not imply all
intervening samples were supported. Evaluator overhead is reported separately
from receiver CPU. None of these measurements feeds acquisition, correction
or branch selection. The six held-out disturbance profiles were reserved
before tuning in `tests/helper_transient_fixture.hpp`.

For 32 expanded-key clean HF cases at 180 dB, all frozen 36-second frequency
intervals covered; point error RMS was 0.01659 Hz, maximum absolute error
0.03238 Hz, and median/maximum conditional half-width 0.11342/0.13205 Hz.
Tone supported 160/160 windows; median per-seed fresh-phase RMS was 2.854°,
median per-seed 99th percentile 7.152°, worst admitted sample phase 7.224°,
maximum timing error 8.934 µs and no slips were observed. APSK supported
160/160 anchors with 331–332 common symbols per run. High-only versus dual
median noisy EVM was 0.55098 versus 0.54834, and clean-twin EVM 0.05923
versus 0.04391. These compare existing algorithms, not a new receiver gain.
The Helper and probe are independently specified 40 W, or 80 W combined.
The model retains 290 K noise, 10 dB noise figure, independent oscillator
errors, a 36-second frozen observation, 38-second lead and held-out probes
through 40 seconds.

On the first eight held-out HF keys, all frozen intervals covered and no slips
were observed for each of the six previously unseen disturbance profiles:

| Profile | Supported windows | Halo-affected supported samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| Additive 23 ms | 40/40 | 5,216 | 6.986° |
| Composite phase 37 ms | 38/40 | 5,928 | 43.403° |
| Composite phase 65 ms | 40/40 | 11,524 | 62.413° |
| Gain 55 ms | 38/40 | 8,520 | 7.031° |
| Frequency settling 85 ms | 5/40 | 0 | 3.276° |
| Decaying frequency settling | 39/40 | 46,343 | 52.6° |

The 65 ms phase case's largest clean central-window tone-frequency error was
0.6519 Hz; the decay case's was 0.4337 Hz. The decay count includes retained
phase from the excursion. Physical disturbances also affect the probe; these
phase errors are not solely oscillator-estimation errors. An aperture crossing
a nonstationary disturbance has no single frequency truth.

Eight tuning cases at each of 180, 175, 170, 165 and 160 dB all covered the
frozen frequency; respective maximum half-widths were 0.14461, 0.08091,
0.04559, 0.02591 and 0.01489 Hz. Supported windows were 39/40 at 180 dB and
40/40 at each other loss. At 180 dB, the tuning phase15, phase48 and decay
cases supported 39/40, 37/40 and 39/40 windows, admitted respectively 3,681,
7,512 and 52,960 halo-affected samples, and had maximum sample-phase errors
54.55°, 87.79° and 31.8°. Eight expanded-key clean cases per 5.8 GHz fixture
all covered and supported 40/40 windows without slips; FCC100 and EU20 maximum
half-widths were 0.22292 and 0.21378 Hz. The fixture does not establish hardware
or regulatory compliance.

Eight tuning-key actual-chain controls per condition at 180 dB produced no
qualified snapshots or windows for noise-only and strong curvature. Bounded
differential phase covered 8/8 and supported 40/40 windows, with maximum
admitted phase error 6.33°; large differential phase also covered 8/8 and
supported 40/40, with maximum 19.31°, and could go undetected. Drift covered
8/8 and supported 40/40 with maximum 5.45°. Short gaps covered 8/8 and
supported 39/40 with maximum 7.29°. A baseline step covered 8/8 frozen
intervals but supported 0/40 later windows. Retune-mid covered 8/8 and
supported 17/40, with maximum admitted phase error 61.06° and no slips. For a
whole-cycle gap, the optional branch admitted 32/40 windows and all eight
cases slipped, with maximum phase error 164.01°; the gap crossed the cutoff so
no 36-second snapshot was available, and the baseline supported no windows.
These tuning controls do not establish held-out guarantees.

An offline pooled actual-waveform Huber/fine-scan candidate was evaluated on
eight expanded-key tuning seeds and 32 dwells. At gate 3.5 it flagged 0/32
clean and 2/32 phase15 cases, leaving 10,800 physically disturbed input samples
in provisional, uneroded spans and a worst phase error of 56.14°. At gate 2.5
it flagged 5/32 clean cases and lost 15,120 clean samples, yet retained 8,640
phase15-disturbed samples with worst phase error 54.30°. The coordinate used
truth ±0.15 Hz, no probe or correction chain, and no qualified post-selection
covariance. This rejects the tested candidate under these conditions, not all
robust approaches.

An offline soft-joint candidate used the full Hann and four quarter-aperture
shared-sample covariance with a soft previous prior, conditional white-noise
and declared bounded-error assumptions. In eight 40-second tuning cases,
668/668 attempts fell back after the current-phase and postcurrent-time L1
checks and combined 0.5 rad guard. An independent audit declined 84/84 in
one case; its median preclock radius was 0.358 rad plus 0.345 rad from relative
clock-evolution uncertainty, not the unknown initial gauge. Artificially
zeroing clock uncertainty admitted 83/84 in a phase-only diagnostic, but is
not a real operating condition; a 0.4 Hz mismatch witness missed all short
cases. The candidate was not integrated or tested in the actual correction
chain because the guard admitted no viable phases. An earlier nominal offline
2.775° to 2.608° phase result is preliminary, not a runtime gain claim. A
future known-output-band guard cannot safely use the 980 Hz anchor factor
alone, which applies only to a predeclared 1420–1780 Hz band. It also needs
within-dwell clock evolution, conditioner-versus-joint clock mismatch,
RF/process transport and actual resampler signed-tap/operator error bounds,
with the same policy for both compared methods. The current API is insufficient
for an output guarantee; the guard is future work, not an implemented guard or
a ±0.1 Hz claim. The baseline continues to use its separate current policies.

The existing idealized oracle was also run on eight held-out v3 HF keys, with
median 94.5 common aided-valid dwells. Median local aided phase RMS was 3.249°,
versus 2.653° with known frequency and 2.103° with true differential phase;
median local aided frequency RMS was 0.09323 Hz versus the oracle profile's
0.07639 Hz. It assumes exact interfering waveforms and path, true frequency
and differential phase, and a truth-plus-0.2 Hz starting coordinate. Its
comparator is the local global-prediction-aided measurement, it supplies no
probe feedback, and it is not a proven information bound.

In an isolated final-source v3 run with eight clean held-out HF keys, 40 W
Helper and 40 W probe at 180 dB loss, dual-pilot receiver process CPU through
36 seconds was 0.144464/0.198246 seconds median/maximum and through 40 seconds
0.190034/0.251776 seconds. The latter includes 2.4 seconds of partial
conditioning, not full-stream conditioning. Median through-40 component times
were 0.023060 seconds startup, 0.124621 correlations, 0 fallback (maximum
0.009842), 0.000868 global estimation, 0.005038 joint estimation and 0.031085
conditioning; categories may overlap. Excluded from receiver CPU were median
simulation setup 0.065506 seconds, 40-second generation 0.351816, tone metrology
0.003266, phase evaluator 0.000139 and mask evaluator 0.000402. The mask
evaluation is included in the 0.029970-second median evaluator-stream total;
the phase evaluator is separate. Median
supported input span was 37.662101–39.937904 seconds; this overall span is
not necessarily contiguous, and per-dwell output provides actual counts and
spans. These measurements do not establish full-stream conditioning throughput.

No GUI, audio hardware or ordinary Robust receiver was accessed. The v3 build
and 25 selected focused checks passed.

## Actual-history dwell-v4 investigations

This follow-up evaluates practical candidate measurements without involving
the GUI, audio hardware or ordinary Robust receiver. No candidate advanced into
the correction chain. Expanded-key v4 reservations use tuning seeds
1680000001–1680000008, held-out HF seeds 1680100001–1680100032, FCC seeds
1680200001–1680200008 and EU seeds 1680300001–1680300008. The 56 keys were
checked distinct from all 256 legacy keys and the 56 v3 reservations; freshness
is relative to scanned evidence, not all possible unlogged prior work. Six
`v4held-*` fixture combinations were reserved before tuning.

In a short regression on the previously used seed 1667039233, the clean
36-second frequency error was 0.007368 Hz with a 0.119497 Hz conditional
half-width, and all five later windows were supported. With a 15 ms composite
phase pulse, the central phase error was 2.49°, but the physical pulse reached
56.26° and 460 halo-affected samples were still supported. The pulse affects
the probe itself; no evaluator truth is passed to the receiver.

In eight tuning-key clean HF cases at each of 180, 175, 170, 165 and 160 dB,
all frozen intervals covered and each loss supported 40/40 later windows.
Their maximum conditional half-widths were respectively 0.13743, 0.07824,
0.04450, 0.02540 and 0.01462 Hz. The 180 dB tuning disturbances produced:

| Profile | Supported windows | Halo-affected supported samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| High-pilot additive 48 ms | 40/40 | 1,537 | 7.335° |
| Composite phase 15 ms | 40/40 | 3,681 | 53.226° |
| Composite phase 48 ms | 37/40 | 6,261 | 91.826° |
| Composite phase 80 ms | 32/40 | 0 | 4.784° |
| Gain 80 ms | 40/40 | 16,163 | 12.834° |
| Frequency settling 60 ms | 3/40 | 0 | 2.332° |
| Decaying frequency settling | 40/40 | 52,963 | 30.957° |

These common-phase pulses physically alter the probe; the measured error is
not solely oscillator-estimation error. Trace support and withholding counts
are evaluator-only and cover only produced tone output from 38.02 to 39.98
seconds. APSK trace counts are unavailable; missing pre-initialization and
post-output samples are not counted. Withheld clean samples are not
automatically useful. As before, tests use 290 K thermal noise, 10 dB noise
figure, independent oscillator errors, a frozen 36-second observation, a
38-second lead and probe intervals through 40 seconds, with separate 40 W
Helper and 40 W probe signals (80 W combined).

On 32 fresh v4 held-out HF clean cases, 31/32 frozen 36-second intervals
qualified and all 31 covered; the remaining result was unavailable, not a
coverage miss. Among qualified cases, point error RMS was 0.018642 Hz, maximum
absolute error 0.047710 Hz, and median/maximum conditional half-width
0.114820/0.151212 Hz. Tone supported 158/160 windows, with median per-seed
fresh-phase RMS 2.876°, median per-seed 99th percentile 6.908°, worst admitted
sample phase 7.841°, maximum timing error 5.982 µs and no slips. Median later
clean-twin tone-frequency RMS was 0.011215 Hz versus 0.118032 Hz for the noisy
probe. Median supported output was 39,722 samples from approximately 2.4
seconds of conditioning; 13,938 clean samples were withheld within the
produced 38.02–39.98 second tone trace, subject to the scope and usability
limits above. APSK supported 158/160 anchors, with a median 331 common symbols.
High-only to dual-pilot median noisy EVM changed from 0.553263 to 0.552148
and clean-twin EVM from 0.056916 to 0.043587. These are comparisons of existing
methods, not a new receiver gain.

In eight held-out v4 cases per disturbance at 180 dB, all frozen intervals
covered and no slips were observed. The trace counts below are summed over the
eight cases, include the resampler halo and apply only to produced tone output
from 38.02 to 39.98 seconds; the preceding trace-scope limitations apply.

| Held-out profile | Supported windows | Disturbed supported / withheld samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| Additive 31 ms | 39/40 | 6,752 / 0 | 11.164° |
| Composite phase 12 ms | 39/40 | 3,104 / 0 | 60.667° |
| Composite phase 53 ms | 33/40 | 2,744 / 8,232 | 65.782° |
| Gain 73 ms | 38/40 | 12,964 / 1,852 | 10.506° |
| Frequency settling 70 ms | 7/40 | 0 / 66,083 | 3.631° |
| Decaying frequency settling | 39/40 | 34,560 / 12,320 | 23.187° |

The decay case had a maximum clean central-window residual tone frequency of
0.362 Hz. Phase errors include physical perturbation of the probe. In the
12 ms phase case, median per-case anchor phase RMS was 2.827° against
both the actual physical and counterfactual unperturbed phase, because the
anchor fell outside the pulse; nevertheless 3,104 halo-affected outputs were
supported and the maximum admitted sample phase error was 60.667°. Anchor
precision therefore did not detect or correct that pulse. For frequency70,
retained excursion phase means the counterfactual pre-impairment phase is not
the same as actual post-event settled phase.

Eight clean held-out cases for each of the 5.8 GHz FCC100 and EU20 fixtures covered all
frozen intervals and supported 40/40 windows without slips; their maximum
half-widths were 0.217181 and 0.214211 Hz respectively. These fixtures do not
establish regulatory compliance.

Eight v4 tuning controls per condition produced 0/8 qualified snapshots and
no later windows for noise-only and curvature. A baseline step covered 8/8
frozen intervals but supported 0/40 later windows. Bounded and large
differential phase each covered 8/8, with 40/40 and 39/40 windows and maximum
admitted phase errors of 8.07° and 21.36° respectively. Short-gap, retune-mid,
drift and negative-clock controls each covered 8/8 and supported respectively
40/40, 20/40, 40/40 and 39/40 windows; their maximum phase errors were 7.94°,
4.72°, 7.42° and 6.70°, with no admitted slips. For a whole-cycle gap, the
optional branch had no 36-second snapshot, admitted 32/40 later windows and
slipped in all eight cases, with maximum phase 164.14° and timing error
546.04 µs. The baseline supported no windows. These tuning controls do not
establish detection of arbitrary mismatch.

For the 1.2 Hz step on these tuning keys, the existing extended recovery
option covered all eight frozen intervals, accepted 8/8 epochs and supported
32/40 later tone windows with no slips. The largest clean-window frequency
error was 0.07956 Hz and largest admitted sample-phase error 8.516°. Later
recovery measurements do not alter the frozen 36-second result.

An offline differential-segment witness at threshold 1.35 flagged 6/32 clean
and 23/32 centered additive-48 tuning dwells. Own-date counterfactual phase RMS
changed from 10.60° to 7.40°, but selected intervals still overlapped 6,933
nuisance samples. It had neither halo treatment nor post-selection covariance
and was not integrated. A separate two-pilot pooled GLRT used actual previous
Helper history and current aided frequency, with overlapping 10 ms bins and
gate 3.5: it flagged 1/32 clean and 3/32 phase15 dwells but retained 10,800
physically disturbed input samples, with worst phase error 57.06°. Five ms bins
flagged 5/32 phase15, retained 10,080 disturbed samples and had worst error
54.72°. There was no per-segment threshold, and no truth or probe drove
selection; the provisional input spans were uneroded and the test was offline
and uncalibrated.

A joint split experiment on eight fresh tuning keys had 73/128 common
observations. Baseline versus split phase RMS at the second-half date was
3.995° versus 3.653°; first-half frequency RMS at each method's own date was
0.09524 versus 0.22831 Hz. It used baseline-only history and quarter gates,
without conditioner support. Only this candidate was rejected. A practical
actual-chain guard still lacks the necessary actual-history information
contracts and certified signed FIR operator; the 980 Hz band anchor factor
alone is insufficient, and waveform APSK phase is undefined near zero
amplitude. These results do not exhaust all approaches or establish a
physical sensitivity limit or diminishing returns. The v4 build and all 28
focused checks passed.

An idealized oracle run on the first eight v4 held-out keys had a median of
94.5 common aided-valid dwells. Median local aided phase RMS was 3.151°,
versus 2.709° with exact frequency and 2.208° with true differential phase;
median aided frequency RMS was 0.09228 Hz versus the oracle's 0.07044 Hz.
The oracle knows exact nuisance waveforms and path, true frequencies and
differential phase, and starts its search at truth plus 0.2 Hz. Its comparator
is the local global-prediction-aided measurement; it supplies no receiver
feedback and is not a physical bound.

In an isolated final-source run of the first eight clean v4 held-out HF keys
at 180 dB and 40 W per signal, dual-pilot receiver process CPU through 36
seconds was 0.142710/0.150445 seconds median/maximum and through 40 seconds
0.188306/0.206315 seconds. The latter includes only 2.4 seconds of partial
conditioning and does not establish full-stream throughput. Median through-40
component times were 0.022643 seconds startup, 0.126609 correlation, 0.002322
fallback, 0.000894 global estimation, 0.005309 joint estimation and 0.030606
conditioning; categories may overlap. Excluded from receiver CPU were median
simulation setup 0.062936 seconds, 40-second generation 0.425408, tone
metrology 0.003348, phase evaluator 0.000147 and evaluator stream 0.029568,
which includes mask evaluation of 0.000401. The median overall supported
input span, 37.662102–39.937905 seconds, is not necessarily continuous;
per-dwell counts and spans are provided in the output.

The standalone candidate sources are `tests/test_helper_diff_segment_v4.cpp`,
`tests/test_helper_transient_glrt_v4.cpp` and
`tests/test_helper_joint_split_v4.cpp`; their results do not feed the actual
receiver. The held-out disturbance definitions are in
`tests/helper_transient_fixture.hpp`.

## Joint-dwell and conditional-guard v5 experiments

Some v5 measurements in this section are intermediate: a later guard
correction adds an output-map phase term and the full halo distance. Results
explicitly identified as final-guard measurements were rerun on the corrected
source; other figures retain the earlier guard.

The standalone v5 chain adds an optional local current-dwell two-pilot
likelihood. The earlier aided result supplies acquisition gates, local noise
estimates and fallback; its measurement is not ingested as a second independent
likelihood row when the candidate is selected. A previous Helper prediction
provides search coordinates, not an independent prior observation. Fresh
phase and frequency estimates, covariance and model allowances are conditional
on the aided gate and a locally stationary, white-noise two-tone model; a
fallback remains available. The source implementation is experimental and is
not integrated into ordinary Robust.

Matched v5 and baseline runs both use the same conditional input-phase guard
for a declared 1420–1780 Hz target band. It budgets three stochastic standard
deviations plus model, frequency/clock transport and phase diffusion within
0.5 rad, then the conditioner erodes support using its actual interpolation
halo. This conditional input-point budget is not a guaranteed output FIR error
bound for arbitrary modulation or unmodeled physical disturbances. Each fresh
dwell requires 0.4 seconds of buffering/look-ahead. The same fixed initial
relative reference is preserved, and the independent probe supplies no
synchronization or branch selection.

The expanded-v3 key namespace was used with eight tuning keys
1690000001–1690000008, 32 held-out HF keys 1690100001–1690100032, and eight
reserved microwave keys each for FCC 1690200001–1690200008 and EU
1690300001–1690300008. Freshness is relative to the scanned key inventory,
not a proof about unlogged runs or distinct finite schedule prefixes. Thermal
noise was modeled at 290 K with 10 dB noise figure; Helper and probe powers
were separately 40 W, 80 W combined. The observation was frozen at 36 seconds,
the transmit lead was 38 seconds, and held-out tone probes ran through 40.

On the 32 held-out clean HF captures at 180 dB, a final-guard rerun reproduced
the previous signal and support results. Joint-dwell and baseline each
qualified and covered all 32 frozen frequency intervals. Joint-dwell point
error RMS was 0.01279 Hz with worst absolute error 0.02726 Hz; its conditional
half-width median/max was 0.11670/0.15975 Hz. The baseline values were
0.01302 Hz RMS, 0.02769 Hz maximum absolute error, and 0.11821/0.16260 Hz
median/max half-width. Neither method attained a conditional ±0.1 Hz interval
in these 32 cases. Later joint-dwell and baseline respectively supported
159/160 and 157/160 tone windows; worst admitted clean-sample phase errors
were 8.51° and 11.09°, maximum timing errors 4.98 and 4.46 µs, and neither
reported slips. Median clean-twin tone-frequency RMS was 0.01131 versus
0.01222 Hz; median noisy tone RMS was 0.09693 versus 0.09260 Hz. These
method-selected window counts and noisy measurements are not necessarily on
the same samples. Their fully supported window counts were 154/160 versus
140/160, with 1,252,180 versus 1,203,039 supported stream samples. When the
36-second model is frozen, later prediction frequency RMS was 0.01458 versus
0.01494 Hz, with maxima 0.03295 versus 0.03352 Hz; later samples did not alter
the frozen result. One case's supported input spans were 38.06208–38.33786,
38.46211–38.73784, 38.86208–39.13786, 39.26211–39.53784 and
39.66209–39.93786 seconds; the overall span is not contiguous.

For equal fixed-gauge, common-supported samples in these same cases, median
per-seed all-dwell phase RMS was 2.638° for joint-dwell versus 2.888° for the
baseline, and median clean-twin sample-phase RMS was 2.073° versus 2.447°.
The evaluator's common-support intersection contained 20,864–33,098 tone
samples per case, 995,341 in total; it does not feed either chain. Overall
common-dwell phase RMS was 2.623° versus 2.886°, with worst phase 9.071° versus
11.093°. The candidate accepted all
2,664 attempted local joint dwells in this clean sample. In an eight-seed
tuning screen rerun on the final guard at 180, 175, 170, 165 and 160 dB,
both methods covered all
frozen intervals. Joint-dwell maximum half-widths were respectively 0.1350,
0.0779, 0.0443, 0.0253 and 0.0146 Hz; it supported 40/40 later windows at
each loss. These finite cases do not certify uncertainty coverage under
arbitrary drift, clock, differential-phase or channel disturbances.

In eight final-guard clean HF APSK cases, joint-dwell and baseline supported
2,640 and 2,503 symbols respectively. Their method-selected mean noisy EVM
was 0.5434 versus 0.5459 and clean-twin EVM 0.0409 versus 0.0460. On the
same 2,503 common symbols, noisy EVM was 0.5450 versus 0.5459 and clean-twin
EVM 0.0404 versus 0.0459. These are waveform diagnostics, not decoded data
or a claim of broad sensitivity improvement.

In eight final-guard clean 5.8 GHz cases per fixture, both methods qualified
and covered all frozen intervals and had no slips, but joint-dwell supported
less output. FCC100 had 26 versus 38 windows with any support, zero versus nine
fully supported windows, and 78,270 versus 220,139 supported samples; EU20 had
28 versus 40 windows with any support, zero versus 14 full windows, and 89,267 versus
246,599 samples. This candidate did not improve those fixture outcomes;
the schedules do not prove regulatory compliance.

The initial eight-seed 180 dB transient screen does not establish robust
transient tracking. The phase-selection candidate supported 32/40 windows for
the high-pilot additive 48 ms case versus the guarded baseline's 37/40, with
192 versus 609 halo-affected physical-disturbance samples supported. For a
15 ms common phase pulse it supported 37/40 versus 38/40, and both retained
3,529 halo-affected samples; worst admitted clean-sample phase was 53.06°
versus 52.99°. For 60 ms frequency settling both supported only 1/40 windows
and had seven of eight frozen intervals available and covered. These
disturbances affect the probe physically as well as estimator observations,
so admitted pulse error is not solely oscillator-estimation error. Support
counts are evaluator-only within the produced trace and do not include samples
never produced; they do not authorize use of withheld samples.

On the first eight held-out HF keys, a final-guard rerun reproduced all
non-timing results from the earlier capture. The phase-selection candidate admitted no
halo-affected samples for the previously exercised 48 ms phase case versus
800 for baseline; worst admitted sample phase was 10.74° versus 81.01°, with
30 versus 29 full windows. On 205,064 common-supported samples, clean-twin
phase RMS was 2.700° versus 2.722°. For the unseen 31 ms phase case both
admitted 1,688 disturbed samples, with worst phase 64.35°. For unseen
additive 41 ms they admitted 5,184 versus 5,240 disturbed samples. For unseen
frequency settling 80 ms neither admitted disturbed samples, but both retained
only 29,288 supported samples and 4/40 windows; seven of eight frozen
intervals were available and covered. The other three profiles had all eight
frozen intervals covered, and no slips were observed. Counts refer to produced
tone output and the physical mask is evaluator-only. These held-out cases show
both a useful reduction in one modeled pulse and clear failures on others.

A separate soft-conditioned current-dwell option, using the preceding Helper
model and its declared uncertainty, accepted 604/664 attempts in the eight
clean tuning cases and fell back 60 times; it supported 38/40 tone windows,
equal to the guarded baseline in that sample. In the 15 ms phase-pulse screen
it retained 3,220 halo-affected samples and had worst admitted clean-sample
phase 52.33°. Neither the soft nor phase-selection option resolves arbitrary
differential phase, cycle ambiguity, changing channels or the physical pulse.
The earlier gap-cycle counterexamples and baseline model limits still apply.

A final-binary eight-key clean test of the separate `soft-rate-v5` option
accepted 604/664 attempts. Its pooled phase RMS was 2.679° versus 2.895° for
baseline, but clean-twin tone-frequency RMS was 0.0771 versus 0.0180 Hz,
supported tone samples 195,125 versus 240,815, and fully supported windows
16 versus 34. All eight frozen intervals covered. Mean receiver CPU through
36 seconds was 0.283 versus 0.179 seconds. In eight retune-mid cases, worst
admitted sample phase was 18.87° versus 7.85°, tone support was 108,228 versus
132,195 samples and full windows 8 versus 19. Common clean-twin frequency RMS
was 0.0576 versus 0.0203 Hz, with no slips. This implementation failed the
overall comparison; these cases do not reject every soft-prior approach.

In eight-key final-guard impairment controls, moderate curvature and noise-only
input qualified no frozen or held-out results. Short gaps supported 40/40
windows for both methods; cycle-gap cases had no frozen result or later
window. Both methods admitted all windows with an out-of-bound 0.8 rad
differential change, with maximum admitted phase errors of 22.521° and 25.039°
for joint-dwell and baseline. Retune-mid supported 21 versus 20 windows, with
worst admitted phase error 54.849° versus 10.932° and four contradictions for
each method. For a step after 36 seconds, all eight pre-step intervals covered,
but neither method supported later windows and each recorded 16 contradictions.
These controls show that the gates do not reliably reject all channel-model
violations.

In the final-guard matched held-out run, process CPU through 36 seconds was
0.321/0.409 seconds mean/maximum for joint-dwell versus 0.202/0.247 for the
guarded baseline; through 40 seconds the means were 0.405 versus 0.269 seconds.
Joint-dwell mean startup, tracking, fallback and conditioning CPU through 40
seconds was 0.0318, 0.3137, 0.00462 and 0.0459 seconds respectively; tracking
includes joint processing and component categories may overlap. These are campaign measurements, not an isolated
full-stream throughput benchmark. The 36-second calendar observation contains
90 settled 0.36-second apertures; the 40-second cost includes only 2.4 seconds
of probe conditioning. Mean 40-second simulation generation was 0.565 seconds
and metrology 0.0046 seconds, excluded from receiver CPU along with clean-twin
and evaluator work. A focused 30-test run passed before the guard correction,
and six focused tests passed again after the final build. An isolated final
binary run of one held-out seed measured joint-dwell versus baseline process
CPU of 0.2162 versus 0.1350 seconds through 36 seconds, and 0.2778 versus
0.1839 seconds through 40. Joint-dwell startup was 0.0220 seconds,
correlations 0.2153 including joint processing, fallback zero, global fit
0.000864, clock 0.00531 and conditioning 0.03383 for 2.4 seconds of signal;
component categories may overlap. Excluded costs were 0.0613 seconds setup,
0.3937 seconds generation for 40 seconds of signal, 0.00325 metrology and
0.03165 evaluator stream. This one case is not a throughput guarantee, and
full-stream conditioning was not measured.

## Piecewise and evolving-clock v6 feasibility

Two test-only options extend the standalone chain; neither is integrated with
ordinary Robust. Both preserve the existing initial relative reference and use
actual previous Helper history, never a probe-derived phase or new per-hop
reference. Expanded-key reservations use tuning seeds 1700000001–1700000008,
HF held-out 1700100001–1700100032, FCC 1700200001–1700200008 and EU
1700300001–1700300008. Freshness is relative to the scanned reservation
inventory, not all unlogged schedules. Conditions retain the 36-second frozen
observation, 38-second lead, probes through 40 seconds, 290 K thermal noise,
10 dB noise figure and independent 40 W Helper plus 40 W probe (80 W combined).

The `piecewise-v6` option scans bounded intervals of 15 ms correlation bins
for common or individual pilot phase and gain disturbances, using prior-only
frequency coordinates. It profiles linear frequency evolution, retains the
gated baseline's factor-two finite-PSD safety allowance, and accounts separately
for the factor of two between complex-correlation variance and an individual
circular-noise quadrature.
When a disturbance is detected, the contaminated full-aperture observation
is not ingested; the candidate either selects a guarded side for phase only
or withdraws phase authority. Support must also pass the shared conditional
guard and actual interpolation halo. Plugin PSD and gain, the selected model,
and declared dynamics make the nominal threshold conditional and uncalibrated;
a missed detection is not evidence of a clean dwell.

Four tuning seeds, not held-out validation, were compared at 180 dB with
matched captures. For the 15 ms phase pulse the candidate detected no events,
supported all 20 later windows and retained 1,840 halo-affected samples; the
worst admitted clean-sample phase error was 51.90°. For the 48 ms phase pulse
it detected eight events, supported 18/20 windows and retained 2,224 affected
samples, with worst phase 86.50°. The earlier phase-selection option in the
same captures supported 16/20 and retained no affected samples in that 48 ms
case; the baseline supported 19/20 and retained 2,297. For high-pilot
additive 48 ms, piecewise detected two events and retained 384 coincident
marked samples, while the earlier phase-selection option retained none. A
common phase pulse physically affects the probe; the high-pilot-only additive
fixture does not add its disturbance to the probe, so its marked samples are
coincident in time. Counts exclude output never produced, and support,
selection and phase-uncertainty calibration remain limitations. No slips were
observed in these finite tuning cases.

The separate `evolve-v6` option refines only the current correction frequency
using the preceding joint clock forecast and one fresh likelihood, with the
forecast evaluated before ingesting the current dwell. It retains the original
phase/time reference, preserves the existing overlapping-history enclosure,
uses conservative L1 uncertainty sums for unknown correlation, limits the
fresh-observation weight and falls back when the model or prediction is
inconsistent. It does not re-ingest a prediction, rewrite the clock history,
or improve the frozen 36-second result retrospectively.

One tuning-seed feasibility run covered its frozen clean frequency at a
0.10485 Hz conditional half-width, so it did not establish ±0.1 Hz narrowing.
The evolving correction and the v5 joint comparator had clean-twin tone RMS
0.0191 and 0.0185 Hz respectively; during a 15 ms physical phase pulse their
worst admitted sample phases were 49.82° and 49.74°. In a retune-mid case
they were 53.86° and 53.77°, and neither supported a later window after the
tested step. These one-seed examples do not establish a general outcome. Both
candidate implementations were rejected after the short falsification checks;
they were not advanced to held-out tests. No claim of physical sensitivity
limits or exhausted alternatives is supported.

The local joint-dwell implementation also has a default-on static window cache
with 86,400 bytes of bounded static data and a dynamic fallback. An opt-in
`cache-pair-v6` comparison runs cached and uncached methods on the same
capture. In a 32-seed held-out comparison between frozen binaries differing
only in this cache, all non-timing outputs were identical. Mean through-36
receiver process CPU changed from 0.34474 to 0.32639 seconds, with maxima
0.42509 and 0.43388; mean joint-component CPU through 40 seconds changed from
0.14457 to 0.12304, and total receiver CPU through 40 seconds from 0.43215 to
0.41388. In an eight-seed screen at 180, 175, 170, 165 and 160 dB, respective
joint-component means changed from 0.11560 to 0.10140, 0.10227 to 0.08643,
0.10146 to 0.08633, 0.10220 to 0.08860 and 0.12347 to 0.10616 seconds.
These runs were concurrent and do not establish a fixed speedup; fixed
within-case method order and allocator/cache effects may affect CPU. The
40-second receiver timing includes only partial held-out probe conditioning.
The durable same-capture selector also verified identical signal output for
one tuning seed, with cache-on/off CPU through 36 seconds 0.27858/0.28762
seconds and joint-component CPU through 40 seconds 0.10019/0.10916. This
one case is not a throughput guarantee. An additional same-binary comparison
on eight fresh held-out seeds had identical signal fields for each pair.
The initial 32 focused checks passed;
after the cache flag and selector change the five directly affected tests
passed again.

An isolated same-binary run on one held-out seed measured cache-on/off process
CPU through 36 seconds of 0.209561/0.214264 seconds and through 40 seconds of
0.266518/0.271020 seconds. For the cached method, startup was 0.021692,
correlations 0.201102 including 0.077952 joint processing, fallback 0.004446,
global fit 0.000909, clock 0.005489 and 2.4-second conditioning 0.032422
seconds. Excluded costs were 0.097374 seconds setup, 0.337163 generation for
40 seconds of signal, 0.004053 metrology and 0.029611 evaluator stream.
The cache method ran first on the shared capture, categories may overlap, and
this single case does not establish standalone full-stream throughput.

On the fresh 32-key HF held-out captures with the existing joint-dwell and
baseline methods, both covered all frozen intervals, but neither had a
conditional half-width at or below 0.1 Hz. Joint versus baseline frequency
point-error RMS was 0.02145 versus 0.02115 Hz, maximum absolute error 0.04864
versus 0.05011 Hz, and median/maximum half-width 0.11999/0.14378 versus
0.12025/0.14893 Hz. Their pooled phase RMS was 2.556° versus 2.805°; median
per-case 99th-percentile phase was 6.199° versus 6.790°, worst phase 9.114°
versus 10.262°, and later-hop phase RMS 2.218° versus 2.554°. Frozen-model
prediction frequency RMS across 160 later hops was 0.02414 versus 0.02377 Hz.
Supported samples were 1,249,893 versus 1,196,530 and fully supported windows
155 versus 138. On 991,635 common tone samples, mean clean-twin phase RMS was
2.242° versus 2.547°. Maximum timing drift was 1.160 versus 1.363 µs and no
slips were observed. Mean clean tone-frequency RMS was 0.01568 versus
0.01522 Hz, noisy-probe RMS 0.10769 versus 0.09741 Hz, and evaluator-only
oracle clean RMS 0.000274 Hz. These compare existing methods, not the
piecewise or evolving candidate, and the frozen 36-second intervals do not
support ±0.1 Hz narrowing at 180 dB in these cases.

In eight fresh HF APSK cases, cached and uncached results were identical.
Joint-dwell versus baseline supported 2,639 versus 2,494 symbols; mean noisy
EVM was 0.5468 versus 0.5495 and clean-twin EVM 0.0421 versus 0.0475. On
2,494 common symbols, noisy EVM was 0.5484 versus 0.5495 and clean-twin EVM
0.0427 versus 0.0475. For eight FCC microwave cases, cache results were again
identical and both methods covered all frozen intervals, but joint-dwell had
30 versus 39 windows with any support, zero versus 17 full windows, and
87,332 versus 211,837 supported samples; median interval half-width was
0.20992 versus 0.19624 Hz. In eight EU microwave cases, cache results were
also identical and both methods covered all frozen intervals without slips.
Joint-dwell versus baseline supported 24 versus 40 windows with any support,
zero versus ten full windows, and 61,305 versus 183,043 samples; median
half-widths were 0.21446 versus 0.20132 Hz and hop RMS 2.790° versus 2.797°.
The evaluator-only APSK true-reference clean EVM was 0.01056. These are
fixture results, not regulatory or hardware qualification.

Fresh eight-key controls per condition with joint-dwell versus baseline gave
no noise-only qualifications. A 0.02 rad differential disturbance supported
40/40 windows with worst admitted phases 6.02° versus 9.06°; an excessive
0.8 rad disturbance also supported 40/40, with worst phases 17.79° versus
17.87°. Curvature qualified no frozen or later output and produced 78
contradictions per method; a whole-cycle gap had no frozen or later output.
A post-36-second step covered all eight frozen intervals but produced no
later output, with 18 contradictions each. Retune-mid supported 14/40 windows
for each, with worst phase 9.51° versus 9.39° and six contradictions each;
its physical event is not marked by the retune mask. These results do not
guarantee detection of arbitrary channel or clock mismatch.

On the same eight keys, a held-out 22 ms physical phase pulse admitted 5,024
marked samples for each method, with worst sample phase 46.37° versus 41.43°;
112,483 versus 137,332 clean samples were withheld. A 29 ms high-pilot-only
additive event had 6,368 versus 6,369 coincident marked samples and worst
phase 6.79° versus 9.54°; the probe itself was not additively contaminated.
For a 44 ms frequency event, neither method admitted marked samples, but
54,176 marked and 322,144 versus 322,143 clean samples were withheld, with
32 contradictions per method. Counts apply only to the produced tone trace;
marked coincidence and withheld clean samples do not establish probe
contamination or usable recovered samples by themselves.

## Delayed Helper-only buffering and retrospective fit v7

This standalone experiment buffers raw baseband blocks before applying
Helper-derived conditioning; it does not involve the GUI, audio hardware or
ordinary Robust. The bounded queue preserves capture sample indices, capture
system and monotonic times, schedule slot and clock epoch separately from
release time. It releases a whole block only after its last sample is captured
and the configured monotonic delay has elapsed, with supplied timestamp and
period uncertainty included. System-clock changes are reported as capture
discontinuities rather than used as a release clock. The producer must split
blocks at hop or known clock-epoch boundaries, report missing samples and
provide a conservative actual Helper-observation frontier; wall time alone
does not establish contiguous Helper evidence.

Transmitter-time metadata is a conditional affine estimate with uncertainty
and one retained receiver-defined relative reference, including its fractional
sample identity. It is not an absolute transmitter-time acquisition. A
reference mismatch is reported rather than silently rebasing the stream.
Fractional output samples map back to the original capture-clock epoch, not
the release timestamp; capture-clock changes, missing mappings and partial
end-of-stream lookahead must be reported. These guarantees are conditional on
the caller's clock, uncertainty, epoch and observation-frontier metadata. The
simulation must generate capture timestamps using the modeled physical ADC
clock, including audio-clock error. Schedule mapping additionally depends on
an independent ±8 ms schedule-epoch bound and must report ambiguity rather
than assume an arbitrary system-clock mapping.

The retrospective smoother uses one joint estimator snapshot available at the
actual observed frontier. Its support-start, anchor and support-end estimates
must have the same history and retained reference; their uncertainties are
enclosed without treating correlated predictions as independent evidence. A
fresh qualified phase for the target dwell remains mandatory: the smoother
does not carry a common RF phase across random hops, invent observations after
the stream ends, resolve an ambiguity, or reset the reference after a dropout.
It supplies correction and bounded transport information to the same
conditional input-phase guard and actual interpolation-halo accounting
described above, not a universal FIR output-error guarantee.

The study tests configured delays of 0.4, 1, 2 and 3 seconds with 0.4-second
capture blocks. Since release waits for the complete block as well as the
configured delay, the first sample waits at least the delay plus one block,
and block-quantized processing can add further latency. The underlying model
still freezes the 36-second frequency result, uses the fixed 38-second lead
and observes held-out probes through 40 seconds. Independent 40 W Helper and
40 W probe signals mean 80 W combined; thermal noise and independent
oscillator errors retain the previous assumptions. Probe data supplies no
synchronization, branch selection or correction coefficients.

The test compares the baseline support with two wider edge choices. A channel
fixture separately removes the physical Helper and probe for the first 30 ms
or 45 ms of each dwell while preserving the thermal-noise realization. The
45 ms case is a counterexample to assuming that samples are valid merely
because the shorter erasure ended: a wider admitted edge may overlap the longer
physical interruption and the resampler halo. Delay and a more precise clock
fit alone do not identify that interruption. An earlier timestamp fixture used
nominal 24 kHz despite modeled audio-clock error; the corrected fixture uses the
modeled physical ADC clock. Preliminary results from the earlier fixture must
not be treated as corrected measurements. The earlier 34 focused Helper
checks and the delayed-buffer, smoother and study checks passed.

The following matched campaign was run before a subsequent end-of-stream
conditioner-tail fix. An initial post-fix smoke test appended 48 unsupported
outputs without changing supported counts; timestamps at or beyond capture
were unavailable. Those padding-derived outputs are unsupported and their
physical disturbance classification is unknown, not clean.

Across 32 freshly reserved held-out HF captures at 180 dB, edge30 supported
18,447,982 of 26,111,776 pre-drain conditioned outputs versus 16,177,723 for
the interior baseline. Boundary-bin support was 2,083,483 of 7,832,452 versus
zero. These totals exclude 144,000 raw unsupported startup samples per case.
On 1,051,866 common tone samples, the methods had identical mean per-case
clean-twin phase RMS of 2.321° and frequency RMS of 0.01229 Hz. Fresh-hop phase
RMS pooled over the equal-length cases was 2.626°, the median per-case 99th
percentile was 6.617°, and the worst hop was 9.743°. All 32 frozen 36-second
intervals covered their point errors: frequency-error RMS was 0.01404 Hz,
maximum absolute error 0.03451 Hz and median conditional half-width ±0.11979
Hz. None qualified ±0.1 Hz. These finite clean-model samples do not establish
coverage under arbitrary model mismatch. The separately evaluated frozen-model
frequency error over later held-out dwells had pooled RMS 0.01597 Hz; later
Helper updates are not used to revise the frozen result. No reference resets,
timestamp failures or supported schedule mismatches were observed.
For example, in seed 1710100001, 100 dwell measurements were valid and 84
had qualified clock authority; supported output spanned capture dates
6.57199–39.96286 s with gaps. The five held-out dwell supports were
38.03712–38.36286, 38.43710–38.76284, 38.86196–39.13799,
39.23711–39.56284 and 39.63709–39.96286 s, with 7,819, 7,819, 6,626,
7,819 and 7,820 supported samples respectively. Relative timing error reached
6.95 µs in that case, with a −2.41 µs change and zero counted slips; these
dates do not imply the gaps were supported.

The eight-case clean screen at 175, 170, 165 and 160 dB had median frozen
half-widths ±0.06603, ±0.03765, ±0.02156 and ±0.01251 Hz respectively, with
8/8 intervals covering in each group. These are small conditional screens,
not a physical sensitivity boundary or a general reliability guarantee.

On eight matched 180 dB tuning captures, the 0.4, 1, 2 and 3 second delay
settings supported mean boundary-bin counts of 20,158, 51,060, 67,242 and
70,190 per capture respectively; their interior counts were 231,822, 491,727,
515,606 and 511,932. Four 0.4-second cases had no supported boundary samples;
the stringent conditioner time-map gate rejected phase-eligible samples. The
longer delay did not uniformly improve phase accuracy or availability.
At the 2-second setting the observed first-sample ages were 2.400–2.800 s and
last-sample ages 2.000–2.400 s, including 0.4-second block quantization.

An isolated post-drain 180 dB run of held-out seed 1710100001 used 0.859 s
process CPU through the 36-second capture frontier, including 28 seconds of
conditioned input, and 1.020 s through the 40-second capture plus drain,
including 34 seconds conditioned and the first six seconds released raw and
unsupported. Process timing includes timestamp, schedule and trace
instrumentation; it is not an uninstrumented throughput claim. Within that
40-second run, measured startup was 0.022 s, correlation tracking 0.210 s
(including 0.004 s fallback), full 34-second instrumented conditioning 0.770
s, buffering 0.0033 s and smoothing 0.0019 s; these overlapping component
timings must not be summed as independent totals. Simulation generation was
0.363 s and the separate reported metrology including reporting was 0.276 s.
The full-stream conditioned cost is not established: the six-second startup
portion was not conditioned in that receiver run. A separate conditioner-only
benchmark did process and drain 40 seconds (960,000 input samples) with
preset independent hop anchors: recursive and scalar CPU were 0.463 and 0.538
seconds respectively in one fixed-order run. It includes output arrays and
support queries, but excludes signal generation and all tracking; the preset
anchors are not a receiver and this is not end-to-end throughput. The output
values differed by at most 5.96e-8, with identical source dates and support
masks. For the delayed configuration the bounded buffer reserved
924,864 bytes, its peak was 57,600 complex samples, scratch was 76,800 bytes,
plans 10,800 bytes, metadata 324,744 bytes and one conditioner workspace
234,784 bytes, excluding tracker history, allocator overhead and evaluator
arrays. A clean twin additionally used up to 460,800 bytes of queued samples;
the evaluator's input-date trace is separate.

The conditioner drain appended 48 padding-dependent unsupported samples in
the post-fix check. An end-of-capture phase-pulse manual check exercised that
path, and an asserting focused probe verifies unknown physical classifications
are not counted as clean or disturbed. This does not validate actual missing
ADC samples or time-varying ADC-clock drift through the full chain.

A two-case frequency-step check froze a covered 36-second result before the
1.2 Hz step at 36.8 seconds; the frozen model's later frequency error was
1.222 Hz RMS and all ten held-out windows were withheld. Two moderate
curvature cases lost clock-model consistency and did not qualify a 36-second
frequency result or held-out output. These demonstrate availability limits,
not calibrated failure probabilities. Eight noise-only cases qualified no
frequency or supported samples. A whole-cycle gap withheld all 40 held-out
windows in eight cases, while pre-gap support remained; it did not resolve
the ambiguity.

On eight matched HF tuning captures at 180 dB in that campaign, the
30 ms edge option versus the baseline supported 4,662,785 versus 4,076,130
samples over the 40-second captures; boundary-bin support was 537,939 versus
zero. Each capture had 144,000 startup raw samples without correction support,
and 34 seconds were conditioned. Across held-out intervals the edge option
admitted 20,898 early and 20,861 late samples, with worst clean-twin phase
errors of 5.34° and 7.40°. On 259,600 common samples, both methods had 2.779°
clean-twin phase RMS and 0.00807 Hz frequency RMS. All eight frozen 36-second
intervals covered, with median half-width 0.11663 Hz and minimum 0.10751 Hz;
none provided ±0.1 Hz narrowing. No slips, timestamp errors or schedule
errors were observed in these cases. In eight cases at 175 dB, median frozen
half-width was 0.0660 Hz and all eight were at or below 0.1 Hz.

A four-seed tuning comparison of recursive versus scalar conditioner modes
had non-CPU fields agree within 10⁻⁶. Mean reported edge30 process CPU through
36 seconds was 1.590 versus 1.715 seconds, and through 40 seconds 1.895 versus
2.053 seconds; mean CPU for 34 seconds of conditioning was 1.409 versus 1.502
seconds. These concurrent runs include instrumentation and do not establish a
fixed speedup or uninstrumented throughput. The first six seconds of raw
input were unsupported at startup.

In four matched 30 ms physical retune-erasure cases, the 30 ms edge option
admitted no disturbed early samples while the 20 ms option admitted 2,203;
their worst early sample-phase errors were 5.36° and 178.58°. The 30 ms
option withheld 2,950 clean early samples. The 45 ms erasure defeated the
assumption behind both wider edges: 8,640 halo-marked disturbed early samples
across eight cases were admitted, with worst phase error 179.20° among
phase-evaluable samples. These finite
channel-fixture controls do not validate wider support under arbitrary retune
behavior.

Eight unseen end-of-dwell frequency-settling cases admitted 4,252 physically
marked late samples among 19,928 supported late samples, with pooled 7.43°
late RMS and worst error 26.45°, despite the 30 ms retune-erasure assumption.
The baseline withheld all late samples; edge30 withheld 30,021 clean late
samples versus 45,697 for baseline. A four-seed 15 ms phase-pulse
control admitted 1,841 disturbed samples and reached 50.59° worst sample
phase. Eight excessive differential-phase cases remained admitted and reached
23.83° worst fresh-hop error. A bound on physical retune erasure does not bound
unmodeled in-dwell frequency settling or phase pulses. The pre-fix campaign did
not establish that every conditioned output was flushed.

On eight held-out 5.8 GHz EU20 captures, edge30 versus baseline supported
1,185,921 versus 1,190,759 samples, including 131 versus zero boundary-bin
samples; no held-out early or late edge samples were admitted. All eight
frozen frequency intervals covered, with point-error RMS 0.02415 Hz, median
half-width 0.19652 Hz and maximum 0.20628 Hz. Mean hop-phase RMS was 2.982°,
worst hop phase 12.517°, and there were no slips, timestamp errors or
supported-sample schedule errors. Instrumented concurrent mean/maximum
process CPU through 36 seconds was 1.927/1.990 seconds, with 15 unsupported
startup blocks. These are fixture results, not hardware or regulatory
qualification.

On eight pre-fix FCC100 microwave captures, edge30 versus baseline supported
968,914 versus 972,817 samples, with no supported boundary-bin samples for
either. Both covered all eight frozen intervals; mean and maximum conditional
half-width were 0.20673 and 0.22494 Hz, mean hop-phase RMS was 2.765°, and no
slips or timestamp failures were reported. These are fixture results, not
hardware or regulatory qualification.

In eight pre-fix HF APSK cases, edge30 versus baseline supported 2,818 versus
2,595 symbols. Their method-selected mean noisy EVM was 0.54194 versus 0.54344
and clean-twin EVM 0.03919 versus 0.03909. On the same 2,595 common symbols,
both noisy EVM values were 0.54344. The probe supplied no synchronization or
adaptive alignment. Symbol availability and EVM are waveform diagnostics, not
evidence that the unflushed tail was delivered.

Expanded-key reservations are tuning 1710000001–1710000008, HF held-out
1710100001–1710100032, FCC 1710200001–1710200008 and EU
1710300001–1710300008. The fixture checks their keys against scanned prior
reservations; key distinction does not prove every finite hop prefix differs
or cover unlogged history.

## Complete-dwell delayed correction v8

The selectable standalone `full` arm applies an available fresh same-hop
Helper phase across the nominal capture dwell, including outside the guarded
pilot-estimation aperture. Application is separate from the existing
conditional support decision: samples with uncertain correction still leave
the conditioner. When a fresh phase is unavailable, the previous hop's phase
multiplier is cleared and the output is explicitly unqualified. Output
segments distinguish fresh phase applied, no fresh phase, and a filter
footprint crossing a nominal hop, with schedule-boundary ambiguity recorded
independently. This does not assume RF phase continuity between hops or reset
the retained relative time reference.

The boundary estimate remains the supplied nominal schedule epoch with a
conditional ±8 ms uncertainty; it is not refined from independent Helper
evidence. The resampler's conservative 50-input-sample halo (about 2.083 ms
at 24 kHz) is considered for support, but the best-effort uncertain output
can mix samples from independent hop phases. No simulator truth or probe
samples select the correction, support, schedule or branch. The receiver
cannot infer from the modeled physical erasure alone that an ADC sample is
missing. Source, timestamp and uncertainty metadata remain separate from
release time; a transmitter-time map is relative to one retained unknown
constant time/phase reference, not an absolute clock acquisition.

This is a standalone test-consumer path, not a complete downstream streaming
API. Startup capture blocks that precede usable tracking are stored and
released raw and uncorrected with their capture metadata. A subsequent
test-only unified consumer emits them in order with the conditioned trace and
captured raw filter-edge margins, and excludes synthetic drain padding. It
runs only after end-of-capture drain: the buffer's two-second readiness is not
a demonstrated two-second consumer delivery latency. The conditioned trace
has its own resampled grid and explicit capture coordinates; monotonicity and
spacing checks are smoke checks, not proof of one-to-one sample identity or
that no information is lost. The conditioner starts with zero-padded history
rather than the preceding raw capture, so those transition outputs are
unsupported. Real missing ADC
samples and time-varying ADC drift have not yet been exercised through the
conditioner. Focused buffer tests cover caller-declared missing blocks, sample
index gaps, clock drift, caller-marked epoch changes, ordering and drain;
schedule labels and missing-data flags are caller supplied. No Robust
interoperability or decoding has been established.

The fresh matched tone campaign used frozen executable SHA-256
`31c88490588c5ae745bff377f10feb347c93e6ba0afdc8e65ecf1a9c9af96b17`,
40 W Helper and a separate 40 W probe (80 W combined), 180 dB loss, 290 K,
10 dB noise figure, random keyed HF50 hops at 50 kHz spacing, a 36-second
observation, 38-second lead and 40-second capture. It used exactly one fixed
evaluator-only relative gauge, with no per-hop or per-window alignment. The
physical tone and APSK probes exist only at 38–40 s; an additional synthetic
clean twin before 38 s is evaluator-only and begins with the conditioned
portion, not the raw startup. Nominal beginning/end bins are the first/last
60 ms of each receiver-schedule dwell, not measured physical retune dates.
Physical class `disturbed` includes signal erasure and filter halo, not a
missing ADC sample. Noisy instantaneous phase is the unfiltered Helper,
probe and noise composite. The clean-twin fixed-lag frequency diagnostic
aliases outside ±93.75 Hz.

Across 32 fresh held-out HF captures, all 960,000 captured input samples per
case were accounted for; typically 144,000 startup samples per case were
stored raw and uncorrected. In the 38–40 s nominal boundary bins, full-dwell
versus delayed interior baseline clean-twin phase RMS was **11.10° versus
74.19°**, but the full-dwell worst error was **179.93°**. The median per-case
95th and 99th percentile absolute errors were 4.75° and 29.5°; percentile
bins have 0.25° resolution. The mean per-case boundary residual-frequency
RMS was 0.227 Hz and the worst reported dwell frequency was 0.374 Hz. The full arm
classified 162,747 of 460,832 boundary outputs as supported; their RMS was
2.79° and worst error 8.28°. The 298,085 uncertain outputs included 1,536
unknown EOF-padding outputs; among the phase-evaluable uncertain outputs RMS
was 13.65°, with the 179.93° worst error. For the full arm, beginning,
interior and end all-output clean-twin phase RMS was 15.33°, 2.67° and
3.23°, respectively. On approximately 1.05 million common supported
interior samples both arms had about 2.66° phase RMS and 0.0126 Hz residual
frequency RMS; the two directional common counts differed slightly, so this
is not a claim of exactly identical selected samples. Capture-coordinate and
gauge mismatches were reported as zero in these comparisons.

The frozen 36-second frequency result was available in 31/32 cases and all 31
intervals covered: point-error RMS 0.01758 Hz, maximum absolute error
0.04348 Hz, and median conditional half-width ±0.11921 Hz. None qualified
±0.1 Hz. The pooled fresh-hop RMS was 2.58°, worst hop 9.86°, and maximum
relative timing error 16.11 µs; no slips were counted in these clean cases.
Later Helper observations used for delayed correction do not improve the
reported frozen 36-second result. Eight-case clean screens at 175, 170, 165
and 160 dB had median half-widths ±0.06680, ±0.03798, ±0.02172 and ±0.01258
Hz, with 8/8 empirical coverage in each group. These finite conditional
screens are not a sensitivity limit or general confidence guarantee.

At 5.8 GHz, eight cases per fixture showed all-output nominal-boundary RMS of
11.07° versus 88.88° baseline for the FCC geometry and 10.34° versus 70.05°
for the EU geometry; full-arm worst errors were 178.0° and 179.89°. Neither
fixture qualified any boundary output as supported. The 36-second median
conditional half-widths were ±0.2176 and ±0.2009 Hz. These fixtures do not
establish regulatory compliance. An eight-case HF APSK run on this frozen
executable used only supported symbol footprints: full versus baseline
supported 2,804 versus 2,574 symbols, and common-symbol noisy composite EVM
was 55.05% for both. It does not measure uncertain or edge-symbol EVM.

Four matched bounded/excessive differential-phase cases showed that excessive
changes can remain admitted: supported interior clean-twin phase RMS changed
from 2.68° to 5.27°, worst 12.24°, and worst fresh-hop error reached 24.71°.
The frozen frequency intervals precede that impairment. In two 45 ms physical
retune-loss controls, the 30 ms support model still admitted halo-marked
affected samples, with phase errors up to approximately 178°; ADC noise
samples were present. In two whole-cycle gap controls the 36-second result
was unavailable; later samples were passed with explicit missing-phase
provenance, and a counted cycle change in best-effort anchors does not show
recovery. A post-snapshot 1.2 Hz frequency step and moderate curvature caused
large complete-output errors and loss of support. Arbitrary differential
channel phase can imitate clock evolution and remains unresolved.

A separate bounded two-pilot boundary-profile feasibility test used five
boundaries per capture and eight fresh keys, independent per-side complex
gains, assumed audio/RF predictions and a 4 ms-centered ±8 ms prior. At 180 dB
and 3.21 ms arrival its boundary RMS error was 3.07 ms versus 0.85 ms for the
prior center; near either edge it improved modestly. A provisional threshold
admitted only 1–3 of eight signal cases per offset and no noise controls, with
the same noise seeds reused across offsets. It did not justify integration;
it does not rule out other pooled or change-point methods. Its predictions are
optimistic and no added settling was tested.

Expanded fixture-key reservations were tuning 1720000001–1720000008, HF
held-out 1720100001–1720100032, FCC 1720200001–1720200008 and EU
1720300001–1720300008. All 56 keys were checked for byte uniqueness against
256 legacy keys and 280 previously reserved expanded keys; the scoped
historical scan cannot rule out unlogged use. The separate thermal-noise seed
is the schedule seed plus 10,000,000; the seeds are distinct but paired,
not an independent factorial sweep. The newer rebuilt executable
`4caccc1d1936dfe47205a7a724bff9d38a3446086cbd61fb0c1c90247be221e8`
adds complete APSK metrology and newly predeclared impairment fixtures; its
results must not be silently mixed with the frozen campaign above. Fresh
impairment profiles were reserved after initial clean v8 exploration, but
before evaluation or tuning on those profiles.

In eight matched HF APSK runs on this newer executable, all 4,672 eligible
held-out symbols were evaluated. Mean per-case clean-twin EVM was 6.592%
for full dwell versus 50.858% for the delayed baseline, and noisy composite
EVM was 55.167% versus 74.684%. The full arm classified 3,176 symbols as
supported and 1,496 as uncertain. On 2,574 common supported symbols, both
had 5.007% clean-twin and 55.054% noisy composite EVM. All 1,312 nominal
boundary-centered symbols were uncertain: clean-twin EVM was 9.337% versus
95.332% baseline. Across each arm, 512 matched-filter footprints crossed
nominal hops, 712 were boundary-ambiguous and none were unavailable. These
are means of per-case EVM, not power-pooled values. The probe remained present
only during 38–40 seconds and provided no receiver alignment or decisions.
The existing evaluator-only oracle used true clock, frequency and hop phase;
its supported-footprint clean APSK EVM averaged 1.033%. Its unavailable
knowledge and narrower footprint make it neither a receiver comparison nor a
proven physical bound.

Two cases per newly reserved impairment profile were also run. In the
end-phase profile, the full arm classified 5,491 end-bin samples supported,
including 190 evaluator-marked disturbed samples; the combined supported set
had 8.13° phase RMS and 64.89° worst error. For interior frequency settling,
all 67,204 interior outputs were marked supported, including 8,117 marked
disturbed; their combined RMS was 8.90° with 24.68° worst phase error and
0.437 Hz worst reported within-dwell residual frequency. These errors mix
clean and disturbed samples. Only dwell 95 of the five held-out probe dwells
was impaired; the frequency-pulse phase resets at the nominal receiver dwell,
and the high-pilot additive profile does not change the probe. No ADC samples
were missing. A 36-second interval was available and covered in all eight
impairment cases, with half-widths ±0.1401–0.1539 Hz.

In eight matched tuning captures per setting, configured delays of 0.4, 1,
2 and 3 seconds gave held-out all-output boundary clean-twin phase RMS of
11.147°, 10.639°, 10.422° and 10.456° respectively; worst errors were
177.92°, 178.15°, 176.68° and 172.21°. The respective supported boundary
counts were 19,027, 34,071, 40,749 and 40,709; uncertain counts were 96,175,
81,134, 74,459 and 74,498. These supported subsets differ. Actual
first-sample release ages were 0.8–1.2, 1.6, 2.4–2.8 and 3.6 seconds;
last-sample ages were 0.4–0.8, 1.2, 2.0–2.4 and 3.2 seconds. Block
quantization and the physical sample clock produce these ranges. No tested
delay resolves a physical retune loss or random phase ambiguity.

An isolated instrumented 180 dB full-dwell case on the newer executable used
1.467 s process CPU through the 36-second capture frontier, including 28 s
conditioned, and 1.762 s through the 40-second capture plus drain, including
34 s conditioned and 6 s raw. Startup used 0.0216 s, correlations 0.2100 s
(fallback was zero for that case), instrumented conditioning 1.516 s,
buffering 0.00316 s and smoothing 0.00204 s; components overlap and should
not be summed. Generation was 0.378 s for the 40-second capture. Separate
reported metrology including output/reporting was 0.967 s, with 0.207 s of
full-stream regional metrology. The receiver timing includes metadata and
trace instrumentation and is not an uninstrumented throughput claim.

The buffer reserved 924,864 bytes and peaked at 57,600 complex samples;
additional raw scratch, plans, metadata and one conditioner workspace were
76,800, 10,800, 324,744 and 234,784 bytes respectively. The startup consumer
vector reserved 1,228,800 bytes in the clean case; a noise-only control that
never initialized the conditioner reserved 9,830,400 bytes for all raw
samples. These figures exclude tracker history, allocator overhead and
evaluator arrays. The 40-second receiver capture is not fully conditioned
because its initial startup blocks are released raw. A separate 40-second
preset-anchor conditioner-only test is described in v7; it excludes receiver
tracking and does not establish end-to-end throughput.

The later unified-consumer change was checked with clean, noise-only,
missing-Helper and clock-adjustment captures; it does not change the earlier
frozen campaign. In an isolated clean case the 960,000 capture inputs were
accounted for and the deferred consumer emitted 960,041 events: 144,000 raw
startup samples, 50 raw samples at each filter edge, and 815,941 conditioned
samples. It excluded 48 drain-padding outputs and 52 filter-edge outputs.
The event count is not an ADC sample count. Its maximum capture-coordinate
step was 1.690 samples, a spacing smoke check only. All capture timestamps
were available; transmitter-time mapping was unavailable for the 144,000 raw
startup events. Consumer delivery was after drain, not at the buffer's
two-second eligibility date. This final executable used 1.556 s receiver CPU
through the 36-second capture frontier, including 28 seconds conditioned;
through capture and drain it used 1.877 s plus 0.124 s deferred consumer CPU
including test validation, for 2.001 s combined. These figures include
instrumentation and do not measure a real-time streaming API. Generation was
0.468 s for 40 seconds; full regional metrology was 0.310 s and separately
reported total metrology including reporting was 0.831 s. The earlier
instrumented CPU results remain historical measurements of the previous
executable. The later source was checked by independent review and clean,
noise-only, missing-Helper and clock-adjustment runs; missing ADC samples
through the conditioner remain untested.

## Incremental complete-capture conditioner v9

An audit of v8 found that its support mask and the actual operation were
different: the `full` arm applied fresh phase beyond its guarded aperture and
then separately labelled the output's authority. Its raw startup and later
resampled trace were separate test-consumer paths, rather than one incremental
stream. The selectable v8 implementation remains available for matched
comparison; its support labels alone do not prove that every sample was
correctly transformed.

The standalone v9 `HelperCompleteConditioner` consumes bounded delayed
capture blocks and releases corrected events incrementally while capture
continues. It uses a two-point linear interpolator after per-input-sample
mixing, not the earlier band-limited resampler. A plan provides the estimated
boundary, phase, frequency and clock information for each hop; a support
interval labels confidence and does not limit where an available correction
is applied. Missing captured samples produce explicit gap events, with no
invented payload or interpolation across the gap. Events retain capture
sample coordinates and capture UTC/monotonic timestamps separately from the
release timestamp and an optional estimated transmitter-time map.

The source-clock servo retains a single original relative reference and
adjusts its continuous map toward later available fits. It does not silently
choose a new reference after a dropout. One earlier late-map implementation
produced a roughly 56.75° fixed-gauge error; that failed candidate is not a
validation of the revised servo. The revised path exposes the discrepancy
between its integrated clock and the fitted map rather than absorbing it
through evaluator alignment. A mismatch can prevent a sample being labelled
well supported. Unit checks exercise known and unknown phases, chunking,
explicit and implicit sample gaps, source-clock recovery, drift, original
timestamps, startup, drain and system-clock discontinuities, including an
unannounced wall-clock step. They do not establish Robust interoperability.

The initial 32-case v9 held-out comparison revealed two further large clean
interior errors, 42.2° and 34.1°, despite Helper clock errors of roughly 11
and 8 µs. The cause was a `missing_phase` smoother gate: the target's joint
observation failed qualification even though its fresh phase and accumulated
joint clock point were available. The correction path then substituted a
broader clock fallback, injecting approximately 417 and 353 µs of error. The
repair uses a valid, consistent joint clock point independently of phase
qualification. For short delays, it transports the joint curve into the
original reference with explicitly broad uncertainty; it does not reset the
reference or count the prediction as an independent observation. The frozen
36-second frequency estimate is unchanged by this correction. The continuous
clock servo applies a bounded correction of at most ±1,000 ppm; this is an
implementation bound, not a claim about the actual clock error or accuracy.

These initially examined 32 cases are now regression cases, not fresh
holdouts. On matched post-repair regressions, the two affected seeds 13 and
23 had 2.323° and 2.541° held-out interior RMS respectively; one aligned
case changed from 63.7° to 1.008°, and a 0.4-second delay case from 104° to
2.33°. Those are targeted corrections, not calibrated reliability evidence.
No event timestamp or reference errors were reported in those checks. The
earlier 80-run screen preceded this repair and used the old guarded v8
comparator; its comparisons must not be labelled final results for the
repaired candidate or the later full-dwell baseline.

The interpolation choice materially limits signal bandwidth. A one-second
tone check with 24 kHz sampling and a 100 ppm rate conversion measured about
1.94% EVM at 1,780 Hz and about 60% at 10,800 Hz. These are conditioned-tone
checks, not a guarantee for arbitrary baseband spectra. No filtering operation
creates additional information, and the two-point interpolator should not be
assumed transparent across the full Nyquist band.

The optional actual-chain boundary tracker scores 65 candidate offsets within
the supplied ±8 ms prior and retains at most 16 boundary profiles. It uses
independently correlated settled pilot gains from each side and never assumes
random-hop RF phase continuity. Adjacent profiles share noisy pilot templates;
their curvature is not treated as calibrated uncertainty. Boundary uncertainty
remains explicit and is not a
universal bound on retune or settling behavior. `nominal` and `tracked`
boundary modes remain selectable; the tracker must not use probe samples or
simulator truth to select a correction. The tracker is a new candidate, not
proof that arbitrary channel phase and clock motion are distinguishable.

One matched pre-repair 40-second clean feasibility capture at 40 W Helper and separate
40 W tone probe (80 W combined), 180 dB loss, 290 K, 10 dB noise figure and
keyed-random HF50 hops produced held-out 38–40 s interior clean-twin phase
RMS of 2.20°. Over nominal boundary bins, tracked versus nominal v9 phase RMS
was 8.64° versus 10.62°; the v8 guarded comparator was 63.45°. Worst tracked
boundary error was 164.21°, so the average gain does not establish reliable
boundary correction. The evaluator counted 771 wrong physical-hop
assignments in tracked mode versus 5,251 for nominal, with zero such
assignments labelled well supported in this particular capture. Actual
physical hop dates are evaluator-only; the receiver uses the assumed schedule
and Helper observations. These counts include the whole capture and are not a
calibrated probability or a claim that out-of-bound retunes are detected.

In that same capture, the 36-second frequency result had error 0.00527 Hz and
conditional half-width ±0.12197 Hz; it covered the error but did not qualify
±0.1 Hz. The result was frozen at 36 seconds. Retrospective correction of
earlier samples can use later captured Helper observations, and must not be
presented as additional information available at the 36-second snapshot.
The clean-trace relative timing error reached 138.7 µs and its integrated
clock differed from the conditional fit by up to 132.3 µs; no evaluator
truth-error-cell crossings were counted in this single case. This counter
is not an inference of physical branch slips and can count crossings in
noise-only cases. These quantities are not removed by per-hop or per-window
fitting.

At the default two-second configured buffer delay, whole 400 ms blocks were
released with observed capture-to-release sample ages of approximately
2.0–2.8 seconds. A clean run processed 960,000 captured inputs and emitted
960,046 resampled outputs; the number of outputs need not equal the number of
inputs. Its measured conditioner/buffer component allocated 1,847,632 bytes,
plus approximately 166 KB for the boundary tracker; evaluator traces and
pending diagnostics are separate. The single instrumented tracked run used
0.769 seconds of receiver process CPU through the 36-second capture frontier
and 0.916 seconds through capture and drain. It included output instrumentation
and was not a complete isolated throughput qualification. One case does not
establish coverage, false qualification rates, a physical sensitivity limit,
or diminishing returns.

At drain the linear interpolator emits the actual captured endpoint; it does
not add a synthetic padding sample as captured signal. The explicit missing
events and emitted samples account for the captured stream, while output
counts can differ from input counts because of the clock mapping.

Before v9 tuning, the expanded-key sets 1810000001–1810000008 (tuning),
1810100001–1810100032 (HF held-out), 1810200001–1810200008 (FCC fixture),
and 1810300001–1810300008 (EU fixture) were reserved. Their 56 schedule keys
were byte-checked against 592 prior keys with no overlap. Separately seeded
thermal noise uses the schedule seed plus 20,000,000; these deterministic
pairings are not an independent factorial sweep. Scanning recorded history
cannot establish uniqueness against unlogged runs or prove distinct finite
hop prefixes. A replacement fresh held-out range 1810400001–1810400032 and
noise range 1830400001–1830400032 was reserved before tuning the gate repair.
All 32 expanded keys were unique against 648 prior keys, with 1,282 recorded
files scanned; finite hop-prefix uniqueness and unlogged history remain
unverified. The original 1810100001–1810100032 set is regression evidence.
The following final matched campaigns use the repaired executable SHA-256
`e3e4b620825a32bb00fbe09319a26f4f4996650f8dc23efea8e02fda62578598`.
Successful execution means that a simulation completed; the accuracy and
qualification outcomes are given separately below.

The HF clean fixture uses 24 ksample/s complex capture, 290 K and 10 dB noise
figure, independent +37 ppm transmitter and −13 ppm receiver audio clocks,
0.8 ppm relative RF error, 0.05°/√s RF phase noise, and cryptographic-keystream
random selection among 50 channels spaced 50 kHz about 30 MHz. Each hop has a
fresh random RF phase. Default clock drift is zero; separate drift, curvature,
step and retune controls exercise model mismatch. Helper and probe each use
40 W, so their simultaneous power is 80 W. The probe does not contribute
measurements to the receiver.

Across 32 fresh HF captures at 40 W Helper / 180 dB, the tracked and nominal
paths each processed 30,720,000 captured input samples and emitted 30,721,548
resampled outputs. No ADC samples were missing in these clean cases. The
tracked path marked 5,668,937 outputs, or 18.45%, as conditionally supported;
25,052,611 were uncertain and still passed along. Zero timestamp failures,
unavailable timestamps, reference mismatches, supported schedule mismatches
or evaluator truth-error-cell crossings were reported. These are conditional
support labels, not a guarantee against unmodeled impairments. Capture UTC
and monotonic timestamps refer to the physical capture clock, release time is
separate, and estimated transmitter time retains its original relative
reference. The conditioner handles caller-declared ADC gaps as explicit
missing events and does not fabricate samples.
Capture timestamps are the caller's ADC-clock anchors, with an affine period
and uncertainty within each block. UTC steps must split those anchors into
clock epochs; detected unannounced steps invalidate interpolation of UTC
across the discontinuity. Release deadlines use monotonic time. The test
consumer derives time-pattern slots from capture UTC, never release time;
irregular-chunk deterministic checks preserve that lookup within 3 ns of the
supplied clock. This does not establish that a hardware ADC timestamp is
accurate, resolve absolute transmitter time, or test Robust's pattern epoch.
The queue rejects overflow explicitly rather than dropping input. Its default
capacity is 115,200 complex samples and 12 blocks; a producer with smaller
chunks must provision enough block slots or handle backpressure.

The full 0–40 s clean twin is evaluator-only: the real diagnostic probe is
present only during 38–40 s. The following phase RMS values pool evaluable
samples; all-output rows include uncertain samples, and a zero-energy twin
is excluded from phase denominators but is not missing ADC data. The boundary
row combines the first and last nominal 60 ms of each 400 ms capture dwell;
it is not an independently known physical boundary. Worst errors are included
because an RMS gain does not establish that every output is accurate.

| Scope and region | Nominal phase RMS | Tracked phase RMS | Tracked worst |
| --- | ---: | ---: | ---: |
| Synthetic twin, 0–40 s, all outputs | 8.66° | 4.76° | 180.00° |
| Synthetic twin, beginning | 20.25° | 7.98° | 180.00° |
| Synthetic twin, interior | 3.90° | 3.90° | 42.90° |
| Synthetic twin, end | 4.37° | 4.02° | 34.33° |
| Synthetic twin, boundary combined | 14.65° | 6.32° | 180.00° |
| Held-out tone clean twin, 38–40 s, beginning | 13.04° | 8.20° | 179.99° |
| Held-out tone clean twin, interior | 2.33° | 2.33° | 7.38° |
| Held-out tone clean twin, end | 2.92° | 2.53° | 7.85° |
| Held-out tone clean twin, boundary combined | 9.44° | 6.06° | 179.99° |

For the held-out tone the tracked boundary had 22,175 supported and 438,649
uncertain outputs; their RMS values were 2.05° and 6.19° respectively. Across
all 0–40 s synthetic-tone outputs, corresponding values were 2.39° and 5.15°.
These are distinct subsets, not evidence that discarding uncertain samples
improves the whole stream. The evaluator-only physical-bin check within
0.5 ms of a true boundary gave 57.01° tracked versus 62.57° nominal phase
RMS, with 1,368 versus 1,920 wrong-hop assignments among 4,224 output
positions. Whole-capture wrong-hop counts were 20,725 versus 167,170, with
none labelled supported in this clean set. True boundary dates are used only
for evaluation. For the held-out tracked beginning, interior, end and
combined boundary, local per-dwell fixed-lag residual-frequency RMS was
0.1822, 0.01477, 0.01484 and 0.09094 Hz respectively; this diagnostic is
unaliased only within ±93.75 Hz. For tracked combined boundary outputs,
the median and maximum per-case 99th-percentile phase errors were 4.625°
and 8.00°; these are not pooled percentiles.
These residual phase/frequency values use the noiseless, identically impaired
twin after the receiver's unchanged correction. The separately logged raw
noisy-tone phase includes Helper and thermal noise and is not an isolated
tone-frequency measurement. The existing idealized pilot oracle was not used
for correction or rerun as a new physical bound.

The frozen 36-second frequency result was available and covered in 31 of
32 cases; one case was unavailable, and the particular failed qualification
predicate was not isolated. Point-error RMS was 0.01504 Hz, maximum absolute
error 0.03268 Hz and median conditional half-width ±0.11653 Hz; none of the
31 intervals qualified ±0.1 Hz. Fresh-hop phase RMS was 2.51°, worst 11.50°;
the maximum relative timing error was 168.80 µs. Frozen-model held-out
frequency RMS was 0.01725 Hz. Later Helper samples used for delayed
correction did not revise the frozen result. No truth-error-cell crossings
were counted in these clean cases; this counter is not a physical slip
detector.
For example, the two-second path can correct samples captured before 36 s
using Helper through 38.38 s; held-out samples can use Helper through 39.98 s.
The event metadata records that frontier separately from the samples' dates.
Such retrospective output is not a result available at 36 s.

Eight matched HF tuning captures were screened at each loss below. Phase is
the pooled clean-twin RMS of all 38–40 s nominal-boundary outputs; interval
coverage is conditional on the modeled clean channel. Large boundary worst
errors remained at every loss, so lower median intervals alone do not
establish arbitrary-channel reliability.

| Path loss | Nominal boundary RMS | Tracked boundary RMS | Tracked worst | Median 36 s half-width | Covered |
| --- | ---: | ---: | ---: | ---: | ---: |
| 180 dB | 10.52° | 6.09° | 178.58° | ±0.11550 Hz | 8/8 |
| 175 dB | 10.30° | 4.60° | 179.83° | ±0.06608 Hz | 8/8 |
| 170 dB | 10.22° | 2.62° | 173.26° | ±0.03765 Hz | 8/8 |
| 165 dB | 10.21° | 2.52° | 174.61° | ±0.02153 Hz | 8/8 |
| 160 dB | 10.21° | 1.78° | 174.08° | ±0.01247 Hz | 8/8 |

Common-support v9/v8 comparison was available in only four of 32 fresh
cases: 82,802 samples had 2.386° versus 2.621° phase RMS. The maximum capture
date difference was 0.0456 input sample under the predeclared 0.05-sample
matching tolerance; no per-window fit or adaptive alignment was performed.
Unavailable common comparisons are not evidence of equality. The v8 full
comparator uses a separate resampled grid and a different support mask.
Its all-output held-out boundary clean-twin RMS on the same 32 captures was
10.60° versus 6.06° for v9 tracked, but that aggregate is not a common-grid
comparison. The earlier guarded baseline remains selectable separately.

The eight-case matched tracked-delay experiment gave the following held-out
boundary results. Ages include whole-block buffering and are means of the
per-case extrema, not hard bounds. All output is delivered; the supported
count identifies conditional authority rather than discarded samples.

| Requested delay | Boundary RMS | Worst | Supported boundary outputs | Capture-to-release age |
| --- | ---: | ---: | ---: | ---: |
| 0.4 s | 6.17° | 177.51° | 0 | 0.40–1.20 s |
| 1 s | 6.03° | 177.85° | 0 | 1.20–1.60 s |
| 2 s | 6.09° | 178.58° | 5,798 | 2.00–2.80 s |
| 3 s | 6.06° | 174.45° | 5,797 | 3.20–3.60 s |

The 5.8 GHz eight-case fixtures reduced held-out nominal-boundary RMS from
9.47° to 4.90° in the FCC geometry and from 10.69° to 5.75° in the EU
geometry. Neither fixture labelled boundary samples as supported, and worst
tracked errors remained approximately 179°. Their frozen 36-second median
half-widths were ±0.2076 and ±0.2090 Hz. These geometry fixtures do not
certify regulatory compliance or radio performance.

For eight HF APSK cases at 40 W Helper plus 40 W probe, the tracked path's
4,672 available symbols had a mean per-case clean-twin EVM of 5.09% and
noisy matched-filter composite EVM of 54.93%; the nominal values were 6.93%
and 55.12%. The noisy result includes Helper plus probe plus thermal noise.
The v8/v9 support masks differ, and no direct common-support APSK comparison
was established. V9 nominal and tracked modes have identical supported-symbol
counts in this clean set. Footprints that include unavailable or
unknown samples are excluded and counted. This does not establish successful
ordinary Robust decoding.

Two-case impairment checks expose important failure modes. A physical retune
2 ms early worsened tracked nominal-boundary RMS from 7.74° to 12.13° relative
to nominal scheduling. Excessive differential phase left held-out interior
RMS at 7.15° with a 13.66° worst error. A 48 ms phase disturbance produced
15.89° interior RMS and an 83.45° worst error. Frequency step, moderate
curvature and a whole-cycle gap produced very large best-effort errors and/or
unavailable frozen results. Noise-only controls did not qualify a frequency
result, but their truth-error-cell crossings are not physical slip claims.
An actual caller-marked ADC-dwell gap emitted explicit missing events and
preserved sample indices; physical RF loss with present noise is a different
fixture. These limited checks do not resolve arbitrary differential-channel
phase or guarantee retune detection.

The final focused Helper checks passed 15/15. AddressSanitizer and
UndefinedBehaviorSanitizer component checks also passed with leak detection
disabled; leaks were not tested because the execution environment's tracing
restriction prevents the leak check. No GUI, audio hardware, ordinary Robust
integration or full Robust regression was involved.

In sequential isolated single-case measurements on a Ryzen 5 PRO 5650U,
tracked two-second buffering used 0.551 seconds of receiver process CPU across
all threads through the 36-second capture frontier, processing 34 seconds of
conditioner input by that frontier. Through the full 40-second capture and
drain it used 0.627 seconds. The nominal-boundary mode used 0.450 and 0.517
seconds respectively. The full tracked receiver therefore exceeded the
preferred 0.5-second budget, although Helper aided tracking alone was below
it. These measurements are not hardware qualification over all operating
conditions.

For the complete tracked two-second run, startup was 0.0278 seconds for five
broad 0.36-second apertures, aided tracking was 0.2151 seconds for 95
0.36-second apertures, fallback was zero, global fit was 0.00124 seconds,
joint fitting was 0.00621 seconds and boundary tracking was 0.0708 seconds
for 99 boundaries. Buffer pushes used 0.00150 seconds, while release, plan,
conditioning and instrumented consumer work together used 0.3041 seconds for
the full 40-second capture. The component timers overlap the inclusive
receiver time and must not be added as independent costs. Generation used
0.4605 seconds for 40 seconds, clean-twin processing 0.2979 seconds and
metrology 0.4698 seconds; setup was 0.0692 seconds. Total experiment process
user plus system CPU was 3.993 seconds including the v8 baseline, twins and
evaluation. Whole-process peak RSS was 126,560 KiB and is not the receiver
buffer footprint: the conditioner and buffer allocated 1,847,632 bytes,
the boundary tracker 166,248 bytes, with bounded metadata additional;
evaluator trace and pending capacities of 67,108,864 and 4,980,736 bytes are
separate.

For the same isolated capture, the tracked 0.4, 1, 2 and 3 second delays used
0.554, 0.521, 0.551 and 0.522 seconds of receiver CPU through 36 seconds,
with 35.6, 34.8, 34 and 32.8 seconds of conditioner input processed by that
frontier. Their full-capture-plus-drain receiver CPU was 0.615, 0.590, 0.627
and 0.601 seconds. These isolated timing values are separate from the
eight-case accuracy and coverage aggregates above.

## Reproduction

Build the development profile and the standalone targets before running the
short check and matched cases:

```sh
CCACHE_DISABLE=1 ./build.sh --jobs 2
CCACHE_DISABLE=1 cmake --build build/dev --target test_resampler test_helper_measurement test_helper_measurement_hops test_helper_live_probe test_helper_tracking test_helper_aided_phase test_helper_relative_clock test_helper_ingest test_helper_envelope_feasibility test_helper_joint_estimator test_helper_frequency_recovery test_helper_dual_pilot_phase test_helper_joint_dwell test_helper_soft_joint_dwell test_helper_evolving_clock test_helper_piecewise_dwell test_helper_delayed_buffer test_helper_delayed_smoother test_helper_delayed_study test_helper_refinement_study test_helper_continuation_study test_helper_pilot_oracle test_helper_taper_feasibility test_helper_transient_fixture test_helper_pooled_transient_feasibility test_helper_joint_soft_feasibility test_helper_transient_glrt_v4 test_helper_diff_segment_v4 test_helper_joint_split_v4 --parallel 2
g++ -std=c++20 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math -Iinclude -Itests -Ithird_party/xz/src/liblzma/api tests/helper_full_stream_cost.cpp src/helper_stream_conditioner.cpp build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a -lcrypto -lpthread -ldl -o /tmp/helper_full_stream_cost_v7
/tmp/helper_full_stream_cost_v7 2
/tmp/helper_full_stream_cost_v7 100
build/dev/test_helper_continuation_study --probe
build/dev/test_helper_continuation_study --case 180 901 hf clean tone 40 all
build/dev/test_helper_continuation_study --case 180 901 hf clean apsk 40 all
build/dev/test_helper_continuation_study --case 180 901 microwave-eu clean tone 40 all
build/dev/test_helper_continuation_study --robustness
build/dev/test_helper_continuation_study --case 180 901 hf step tone 40 recovery
build/dev/test_helper_continuation_study --case 180 901 hf scaled-step tone 40 recovery
build/dev/test_helper_continuation_study --recovery-robustness
build/dev/test_helper_continuation_study --case 180 901 hf long-gap tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf gap-clock-cycle tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf gap-step tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf step tone 40 recovery-extended
build/dev/test_helper_continuation_study --case 180 901 hf long-gap apsk 40 gap
build/dev/test_helper_continuation_study --case 180 901 microwave-eu long-gap tone 40 gap
build/dev/test_helper_continuation_study --case 180 16777250 hf transient-high-add48 tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1667039233 hf-v3 clean tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1667039233 hf-v3 transient-heldout-phase37 tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 clean tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690000001 hf-v3 transient-phase15 tone 40 transient-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 transient-v5held-phase31 tone 40 transient-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 differential-large tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 retune-mid tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 step tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 transient-phase15 tone 40 piecewise-v6
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 clean tone 40 evolve-v6
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 clean tone 40 cache-pair-v6
build/dev/test_helper_delayed_study 180 1710000001 clean tone 2 hf
build/dev/test_helper_delayed_study 180 1710000001 retune-erasure tone 2 hf
build/dev/test_helper_delayed_study 180 1710000001 retune-erasure-45 tone 2 hf
build/dev/test_helper_delayed_study 180 1710100001 transient-end-phase tone 2 hf
ctest --test-dir build/dev --output-on-failure -R '^helper_delayed_(buffer|smoother|study)$'
build/dev/test_helper_delayed_study 180 1720000001 clean tone 2 hf full
build/dev/test_helper_delayed_study 180 1720000001 retune-erasure-45 tone 2 hf full
build/dev/test_helper_delayed_study 180 1720100001 clean apsk 2 hf full
CCACHE_DISABLE=1 ./build.sh configure --build-dir build/dev --cli
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_complete_conditioner test_helper_boundary_tracker test_helper_complete_study test_helper_delayed_buffer test_helper_delayed_smoother test_helper_delayed_study test_helper_complete_stream_metrics test_helper_complete_apsk_metrics test_helper_noise_seed test_helper_retune_offset test_helper_transient_fixture test_helper_live_probe --parallel 2
ctest --test-dir build/dev --output-on-failure -R '^(helper_live_probe|helper_transient_fixture|helper_delayed_buffer|helper_delayed_smoother|helper_delayed_study|helper_delayed_full_study|helper_complete_stream_metrics|helper_complete_apsk_metrics|helper_noise_seed|helper_retune_offset|helper_complete_conditioner|helper_boundary_tracker|helper_complete_study|helper_complete_tracked_study|helper_complete_clock_regression)$'
build/dev/test_helper_complete_study short 180 1810000001 clean tone 2 hf tracked
build/dev/test_helper_complete_study full 180 1810000001 clean tone 2 hf nominal
build/dev/test_helper_complete_study full 180 1810000001 clean tone 2 hf tracked
build/dev/test_helper_complete_study full 180 1810000001 clean apsk 2 hf tracked
build/dev/test_helper_complete_study full 180 1810000001 clean tone 2 hf tracked guarded
for delay in 0.4 1 2 3; do
  for seed in $(seq 1810000001 1810000008); do
    build/dev/test_helper_complete_study full 180 "$seed" clean tone "$delay" hf tracked full
  done
done
for loss in 180 175 170 165 160; do
  for seed in $(seq 1810000001 1810000008); do
    for boundary in nominal tracked; do
      build/dev/test_helper_complete_study full "$loss" "$seed" clean tone 2 hf "$boundary" full
    done
  done
done
for seed in $(seq 1810400001 1810400032); do
  for boundary in nominal tracked; do
    build/dev/test_helper_complete_study full 180 "$seed" clean tone 2 hf "$boundary" full
  done
done
build/dev/test_helper_delayed_study 180 1720100001 transient-v8held-end-phase tone 2 hf full
build/dev/test_helper_delayed_study 180 1720100001 transient-v8held-interior-decay tone 2 hf full
for delay in 0.4 1 2 3; do
  for seed in $(seq 1720000001 1720000008); do
    build/dev/test_helper_delayed_study 180 "$seed" clean tone "$delay" hf full
  done
done
for seed in $(seq 1720200001 1720200008); do
  build/dev/test_helper_delayed_study 180 "$seed" clean tone 2 microwave full
done
for seed in $(seq 1720300001 1720300008); do
  build/dev/test_helper_delayed_study 180 "$seed" clean tone 2 microwave-eu full
done
for seed in $(seq 1720100001 1720100032); do
  build/dev/test_helper_delayed_study 180 "$seed" clean tone 2 hf full
done
for loss in 180 175 170 165 160; do
  for seed in $(seq 1720000001 1720000008); do
    build/dev/test_helper_delayed_study "$loss" "$seed" clean tone 2 hf full
  done
done
ctest --test-dir build/dev --output-on-failure -R '^(helper_delayed_full_study|helper_complete_stream_metrics|helper_complete_apsk_metrics|helper_noise_seed)$'
for loss in 180 175 170 165 160; do
  for seed in $(seq 1710000001 1710000008); do
    build/dev/test_helper_delayed_study "$loss" "$seed" clean tone 2 hf
  done
done
for seed in $(seq 1710100001 1710100032); do
  build/dev/test_helper_delayed_study 180 "$seed" clean tone 2 hf
done
for delay in 0.4 1 2 3; do
  build/dev/test_helper_delayed_study 180 1710000001 clean tone "$delay" hf
done
build/dev/test_helper_continuation_study --case 180 1690000001 hf-v3 clean tone 40 soft-v5
build/dev/test_helper_pooled_transient_feasibility 1666973697 8 phase15 3.5
build/dev/test_helper_joint_soft_feasibility 180 1666973697 100 clean 0 0
build/dev/test_helper_diff_segment_v4 center48 1.35
build/dev/test_helper_transient_glrt_v4 phase15 3.5
build/dev/test_helper_joint_split_v4 1680000001 32
build/dev/test_helper_continuation_study --gap-robustness
build/dev/test_helper_continuation_study --extended-recovery-robustness
build/dev/test_helper_pilot_oracle 180 1 100 apsk
build/dev/test_helper_pilot_oracle 180 1667039233 100 apsk-v3
build/dev/test_helper_pilot_oracle 180 1680100001 100 apsk-v3
build/dev/test_helper_taper_feasibility short 32 901
build/dev/test_helper_taper_feasibility settle-mid 32 933
python3 tests/test_helper_joint_local_feasibility.py
ctest --test-dir build/dev --output-on-failure -R '^(resampler|helper_(measurement|measurement_hops|live_probe|tracking|tracking_robustness|aided_phase|relative_clock|ingest|envelope_feasibility|joint_estimator|frequency_recovery|dual_pilot_phase|joint_dwell|soft_joint_dwell|evolving_clock|piecewise_dwell|delayed_buffer|delayed_smoother|delayed_study|refinement_study|refinement_robustness|continuation_study|continuation_recovery|continuation_gap|continuation_extended_recovery|pilot_oracle|taper_feasibility|joint_local_feasibility|transient_fixture|pooled_transient_feasibility|joint_soft_feasibility|transient_glrt_v4|diff_segment_v4|joint_split_v4))$'
```

The case arguments specify path loss in dB, deterministic seed, RF fixture,
impairment, probe waveform, independently specified probe power in watts, and
method set. Valid argument names are determined by the executable. Use the same
seeds and channel parameters when comparing methods, and report unavailable
cases rather than selecting only successes. For example, reproduce the paired
32-seed HF tone comparisons and the eight-seed gap-cycle control with:

```sh
for seed in $(seq 901 932); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf long-gap tone 40 gap
  build/dev/test_helper_continuation_study --case 180 "$seed" hf step tone 40 recovery-extended
done
for seed in $(seq 901 908); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf gap-clock-cycle tone 40 gap
done
for seed in $(seq 1 32); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf clean tone 40 finalists
  build/dev/test_helper_continuation_study --case 180 "$seed" hf clean apsk 40 finalists
done
for seed in $(seq 1690100001 1690100032); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 joint-v5
done
```

The following loops reproduce the final v5 tuning loss screen, held-out
impairment and transient cases, clean APSK and microwave fixtures, and the
soft-rate tuning comparison. Each method pair processes the same capture:

```sh
for loss in 180 175 170 165 160; do
  for seed in $(seq 1690000001 1690000008); do
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 joint-v5
  done
done
for mode in gap gap-clock-cycle differential-large curvature noise retune-mid step; do
  for seed in $(seq 1690100001 1690100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 joint-v5
  done
done
for mode in transient-v5held-phase31 transient-v5held-add41 transient-v5held-frequency80 transient-phase48; do
  for seed in $(seq 1690100001 1690100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 transient-v5
  done
done
for seed in $(seq 1690100001 1690100008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean apsk 40 joint-v5
done
for seed in $(seq 1690200001 1690200008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-v3 clean tone 40 joint-v5
done
for seed in $(seq 1690300001 1690300008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-eu-v3 clean tone 40 joint-v5
done
for mode in clean retune-mid; do
  for seed in $(seq 1690000001 1690000008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 soft-rate-v5
  done
done
```

The v6 frozen-binary timing comparison used separate cached and uncached
binaries. The current executable can reproduce the signal cases and compare
the cache alternatives within the same capture:

```sh
for loss in 180 175 170 165 160; do
  for seed in $(seq 1700000001 1700000008); do
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 joint-v5
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 cache-pair-v6
  done
done
for seed in $(seq 1700100001 1700100032); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 joint-v5
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 cache-pair-v6
done
for mode in gap gap-clock-cycle differential differential-large curvature noise retune-mid step transient-v6held-phase22 transient-v6held-add29 transient-v6held-frequency44; do
  for seed in $(seq 1700100001 1700100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 joint-v5
  done
done
for seed in $(seq 1700100001 1700100008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean apsk 40 joint-v5
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 cache-pair-v6
done
for seed in $(seq 1700200001 1700200008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-v3 clean tone 40 joint-v5
done
for seed in $(seq 1700300001 1700300008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-eu-v3 clean tone 40 joint-v5
done
for mode in clean transient-high-add48 transient-phase15 transient-phase48; do
  for seed in $(seq 1700000001 1700000004); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 piecewise-v6
  done
done
```

Each method pair in a case uses the same impaired capture. The ordinary Robust regression
campaign is outside the scope of this standalone study. The earlier development
build and 17 focused checks passed before the opt-in follow-up. The follow-up
build (`CCACHE_DISABLE=1 ./build.sh --jobs 2`) and all 19 selected focused
checks passed; CTest took 79.69 seconds. After the independent-seed follow-up,
`CCACHE_DISABLE=1 ./build.sh --jobs 2` and all 22 selected focused checks
passed; CTest took 91.48 seconds. After the dwell-v2 fixture changes, the same
uncached main build and all 23 selected focused checks passed; CTest took
87.70 seconds. For v3 the project build and all 25 selected focused checks
passed; CTest took 95.40 seconds. The full ordinary Robust regression campaign
was not run. After the v4 changes, `CCACHE_DISABLE=1 ./build.sh --jobs 2`,
the focused target rebuild and all 28 selected checks passed; CTest took
104.09 seconds. During v6, 32 focused checks passed before the cache flag and
selector change; the five directly affected checks passed again afterward.

The evaluator-only dynamics experiment is not a registered test. One local
Linux build using this development profile and its bundled libraries is:

```sh
g++ -std=c++20 -O3 -DNDEBUG -Wno-subobject-linkage -ffp-contract=off -fno-fast-math -Iinclude -Itests -Ithird_party/xz/src/liblzma/api tests/test_helper_dynamics_benchmark.cpp build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-dynamics-benchmark
/tmp/helper-dynamics-benchmark 903 .001 18
/tmp/helper-dynamics-benchmark 906 .001 18
```

The separate ramp experiment was added after the 22-test run and is not
registered in CTest. It can be reproduced locally with:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_gap_ramp_feasibility.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-gap-ramp
/tmp/helper-gap-ramp 1 8 tone 180 0
/tmp/helper-gap-ramp 1 8 apsk 180 0
```

The waveform-level joint feasibility experiment is also unregistered. One local
Linux reproduction uses:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_joint_wave_feasibility.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-joint-wave
/tmp/helper-joint-wave 180 16777250 100 clean 0
/tmp/helper-joint-wave 180 16777250 100 clean .4
```

The unregistered piecewise diagnostic, with onset relative to the settled
aperture and four dwells per seed, can be reproduced locally with:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_piecewise_feasibility.cpp build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a -lcrypto -lpthread -ldl -o /tmp/robust_piecewise_joint
/tmp/robust_piecewise_joint 6 16777250 .14 .048 1 1.570796 .15 high 4
/tmp/robust_piecewise_joint 6 16777250 .12 .08 1 1.570796 .15 high 4
```

The separate timing-signature experiment has a local Linux reproduction:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_timing_signature_experiment.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-timing-signature-new
/tmp/helper-timing-signature-new 16777250 1 apsk 0
/tmp/helper-timing-signature-new 16777250 1 apsk .02
/tmp/helper-timing-signature-new 16777250 1 apsk 2.717052
```
