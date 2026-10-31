# Standalone Helper frequency study for Earth–Moon–Earth budgets

See the [Helper integration guide](helper-integration.md) for current
integration constraints and the cross-study view of inexpensive approaches.
This page reports a bounded EME link-budget study, not universal Robust
receiver requirements.

This is an independent, sample-level Helper Carrier study of two illustrative
5.8 GHz link budgets. It does not run the ordinary Robust receiver, establish
that a particular Robust search width is sufficient, or qualify a radio or a
Moon-bounce link. The standalone receiver consumes complex samples and known RF
hop commands; it is not connected to the live receiver or a hardware retuner.

The [EME planner preset](link-planner.md) uses GPSDO OCXO models for both
baseband and shift oscillators and sets Doppler to zero. That is a planning
assumption, not a predicted lunar trajectory. The reduced-model study below
assumes a separately calibrated relative audio-clock error within ±0.0001 ppm
and that any deterministic Doppler trajectory has already been removed. It
models an unknown common RF frequency displacement and treats random FHSS
retune phases as independent. Unmodeled Doppler is not separately identifiable
from oscillator error. The receiver does not derive an ephemeris, verify GPS
lock, or independently prove the declared clock and residual-evolution bounds.

The [JPL Moon-bounce study](https://tda.jpl.nasa.gov/progress_report/42-169/169D.pdf)
describes distinct Doppler and delay signatures across the lunar surface. A
single flat noisy tone does not model that propagation. The two-path stress test
below is an intentionally simple perturbation, not a measured lunar scattering
spectrum, a delay-spread model, or evidence of field performance.

## Test inputs and interpretation

The examples use **total active-mean Helper transmit power**, not high-pilot
power. The actual Generator's `N0CALL` waveform and the 36-second calendar are
used to calculate the active mean; its straight high-pilot fraction is
0.64168563. At 290 K input noise reference and 10 dB noise figure, the
high-pilot C/N0 is 13.8093 dB-Hz at 15 kW/220 dB and 3.8093 dB-Hz at
1.5 kW/220 dB. Noise figure is an assumed receiver input, not a hardware
measurement. Equal C/N0 comparisons at the two transmit powers use path losses
10 dB apart and the same seeded noise realizations; they are not independent
trials.

The simulated high and low pilots are 2400 and 550 Hz, with the low pilot
3 dB weaker. Tests use 90 settled 360 ms apertures over 36 seconds, the fixed
400 ms dwell calendar, keyed random FCC-style or EU-style 5.8 GHz schedules,
independent retune phases, and actual transmitter waveform or an independent
synthetic reference. The known RF command for each hop is supplied to the
receiver; hidden oscillator truth does not center its search. A modeled
1500 Hz DATA-frequency estimate is a comparison point, not a decoded Robust
signal. The true next-dwell model error is evaluated separately during the
36–38 second held-out segment. A future received aperture has its own noise and
phase; the fitted-mean interval alone does not bound that new noisy measurement.

The narrower-audio experiment uses +0.00004 ppm transmitter and −0.00003 ppm
receiver audio errors, within the declared relative ±0.0001 ppm bound; the RF
shift uses +0.00008 ppm and the GPSDO OCXO 0.005°/√s phase-diffusion model.
A second comparison leaves the audio clock much less constrained (+37 ppm
transmit and −13 ppm receive, with a ±300 ppm search setting). These are
relative-link simulation inputs, not endpoint calibrations. Keyed hop fixtures
and their schedule-only regulatory scope are described in
[Helper measurement](helper-measurement.md#keyed-randomized-hop-fixtures).

For comparison only, an ideal high-C/N0 local-information calculation with
90 independent 0.36-second phases and both pilots gives a three-times-standard-
deviation scale of about 0.095 Hz at 15 kW/220 dB when audio scale and
frequency rate are known, or about 0.30 Hz at 1.5 kW/220 dB. Unknown frequency
rate, model mismatch, fading, acquisition, and finite-SNR behavior can make
actual performance worse. These theoretical references are not operational
confidence intervals or a proof of a physical sensitivity floor. The project's
[segmented-pilot reference](helper-carrier.md#link-planner-and-lead-time)
and [JPL frequency-estimation comparison](https://ntrs.nasa.gov/api/citations/19890011016/downloads/19890011016.pdf)
explain the different coherent and segmented information models.

## Bounded-audio frequency receiver

[`HelperFrequencyMeasurement`](../include/datapump/helper_frequency_measurement.hpp)
is a separate experimental receiver for an externally bounded relative audio
clock. It profiles per-aperture pilot phases independently and fits either a
constant common RF displacement or an additional linear rate. In constant mode,
the caller declares a bound on residual frequency rate; in linear mode it
declares a bound on residual acceleration. Both are assumptions after any
predicted Doppler correction, not measurements of the real Moon path.

`conditional_half_width_hz` combines local statistical uncertainty with the
configured audio and evolution allowances. The default usable `half_width_hz`
also encloses a separately fitted recent-half estimate and its uncertainty.
The latter is intended to respond to model disagreement; neither quantity is a
calibrated confidence interval or a proof against arbitrary oscillator or
propagation changes. A clear temporal-consistency contradiction withdraws
qualification. RF-ratio and model allowances use local influence coefficients,
not a universal deterministic error bound. Boundary-limited and work-limited
results are explicitly unavailable. The reduced model cannot be silently used
for an undisciplined sound card.

At 15 kW/220 dB, with the assumed constant residual rate bounded by
0.0005 Hz/s and zero actual residual rate, the corrected actual-transmitter
FHSS samples produced signal evidence and guarded predictions in all eight
seeds. Their guarded 3σ-style half-widths were 0.2743–0.3617 Hz; the smaller
prior-only conditional widths were 0.1003–0.1068 Hz. All eight modeled 1500 Hz
errors were covered, with a maximum absolute error of 0.0333 Hz. The model
prediction on the held-out segment had a maximum absolute error of 0.0331 Hz;
this is a truth comparison, not a coverage assertion for a new noisy aperture.
At 1.5 kW/220 dB, none of eight actual-transmitter samples passed the reduced
signal-evidence gate. A parallel synthetic-source run used a different keyed
EU-style calendar. Eight samples do not establish probability of acquisition,
coverage, or sensitivity.

With the same stated clock, noise and zero-residual assumptions, a four-seed
0.5 dB path-loss refinement found **204.5 dB for 15 kW**, and the equivalent
**194.5 dB for 1.5 kW**, as the largest tested losses at which all four actual
waveform samples and all four synthetic samples had a guarded half-width at or
below 0.1 Hz and covered the simulated 1500 Hz frequency. At 205–206 dB for
15 kW, all four synthetic samples qualified but only three of four
actual-waveform samples supplied a guarded interval; the equivalent 1.5 kW
losses are 195–196 dB. The two power series have matching C/N0 and reuse the
same seeds, so they are not independent confirmations. These are the best
observed points in a small fixed test grid, not a maximum achievable path loss
or a calibrated reliability threshold. The held-out prediction error is
reported separately and the intervals are conditional on the stated models.

For comparison, the earlier general four-parameter receiver retained the
less-constrained relative audio model. At 15 kW/220 dB, four actual-waveform
samples produced signal evidence but all four were boundary-limited and had
no valid interval; at 1.5 kW/220 dB, none of four detected. At 15 kW/203 dB
and the equal-C/N0 1.5 kW/193 dB point, four of four actual-waveform samples
had ≤0.1 Hz conditional intervals covering truth, while one of four synthetic
samples was boundary-limited. Again, this is a small comparison, not an EME
acceptance claim.

An earlier implementation made a materially different claim before the
recent-half guard. In eight seeded 15 kW/218 dB actual-waveform trials, a true
0.0007 Hz/s² curvature outside the constant-mode rate assumption produced five
apparently qualified narrow intervals, all of which missed the modeled truth;
the worst error was 0.324 Hz. With the guard, one of the eight returned a usable
interval, at 0.464 Hz half-width, and that interval covered the modeled error.
The other seven did not yield a guarded prediction. When true residual rate was
0.005 Hz/s against the assumed 0.0005 Hz/s bound, seven guarded predictions
were available and all seven covered, with none claiming ±0.1 Hz; two of seven
prior-only intervals missed. The assumed bound was violated in both stress
cases, so these few results do not guarantee detection of all mismatches.

At the same 15 kW/218 dB actual waveform, an artificial flat two-path signal
used a second voltage amplitude 0.5 relative to the first and differential
Dopplers of 0, 0.1, 0.25, and 0.5 Hz. No path delay or frequency-selective
response was modeled. For eight seeds at each separation and each constant or
linear model, guarded predictions covered the specified direct-path modeled
frequency; none met ±0.1 Hz. The prior-only intervals missed one or two seeds
at the 0.25 and 0.5 Hz separations. This is a numerical stress test, not a
validated representation of lunar echo characteristics.

At 1.5 kW/220 dB with the same 290 K input reference, lowering assumed noise
figure from 10 dB to 3 dB or 0 dB produced eight guarded detections in each
modeled case. Constant-mode guarded widths were 0.396–0.718 Hz at 3 dB NF and
0.272–0.490 Hz at 0 dB NF; neither met ±0.1 Hz. At 15 kW/220 dB and 3 dB NF,
constant-mode guarded widths were 0.134–0.151 Hz. These are modeled noise
figures and do not demonstrate that an actual EME receiver achieves them.

## Separate phase-continuous, non-FHSS experiment

[`HelperCoherentFrequency`](../include/datapump/helper_coherent_frequency.hpp)
is a separate experimental path for an unchanged RF setting whose received
pilot phases remain continuous across the supplied apertures. It must not be
used to combine independently reset random FHSS hop phases. It assumes a
separately bounded audio clock and that the predicted Doppler trajectory was
already removed. A local frequency-and-rate fit and a recent-half comparison
provide conditional uncertainty; callers must supply any additional justified
model-error allowance. The coherent hypothesis itself is a physical assumption,
not a guarantee derived from GPSDO selection.

In eight seeded **actual Helper Generator** waveform trials per power, the
flat, phase-continuous no-hop simulation used 290 K, 10 dB noise figure,
GPSDO-class relative audio and RF models, and 220 dB loss. The ungated
waveform's straight high-pilot fraction was 0.634251844. At 15 kW its high
pilot C/N0 was 13.7587 dB-Hz; all eight estimates were qualified, their
largest guarded half-width was 0.01776 Hz and the largest modeled DATA
frequency error was 0.00314 Hz. At 1.5 kW its high pilot C/N0 was 3.7587
dB-Hz; all eight were qualified, with maximum half-width 0.05949 Hz and
maximum modeled error 0.00927 Hz. The largest held-out model prediction errors
were 0.00347 Hz and 0.01022 Hz respectively; those compare against truth and
are not coverage guarantees for a new noisy sample. These small trials do not
establish a real lunar link or an acquisition probability.

For the 1.5 kW case, eight noise-only and eight independent random-phase-reset
controls produced no qualified result. With modeled residual linear rates of
0.0005 and 0.005 Hz/s, eight of eight estimates at each rate remained within
0.1 Hz and covered their modeled frequency. A centered quadratic curvature
stress of `0.0007 (t−18)²` Hz was declined in all eight cases. The same
illustrative flat two-path perturbation used a 0.5 relative voltage amplitude
and differential Doppler 0.1, 0.25 or 0.5 Hz. It qualified 4, 6 and 6 of eight
respectively; the qualified intervals covered the simulated direct-path
frequency. No path delay, measured lunar spectrum or field channel was
modeled, and the unqualified samples are not reception successes. Maximum
receiver-only CPU across the study was 0.1643 seconds for each 36-second
observation on the local test host. Two additional eight-seed phase-diffusion
checks used actual diffusion of 0.05°/√s and 0.5°/√s while declaring the
nominal 0.005°/√s; both returned eight covered intervals. Declaring the true
0.5°/√s also returned eight covered intervals. The maximum receiver-only CPU
across the study and these additional checks was 0.1669 seconds. This small
seed set and the local phase model do not establish robustness to arbitrary
phase noise or lunar propagation.

## Reproduction

Run with a compatible configured `build/dev`:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement_eme --parallel 2
build/dev/test_helper_measurement_eme --probe
build/dev/test_helper_measurement_eme --sweep
build/dev/test_helper_measurement_eme --frequency-probe
build/dev/test_helper_measurement_eme --frequency-edge
build/dev/test_helper_measurement_eme --frequency-sweep
build/dev/test_helper_measurement_eme --frequency-transition
build/dev/test_helper_measurement_eme --frequency-linear
build/dev/test_helper_measurement_eme --frequency-drift
build/dev/test_helper_measurement_eme --frequency-multipath
build/dev/test_helper_measurement_eme --noise-figure
build/dev/test_helper_measurement_eme --frequency-regression

CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_coherent_frequency --parallel 2
build/dev/test_helper_coherent_frequency --probe
build/dev/test_helper_coherent_frequency --suite
build/dev/test_helper_coherent_frequency --study
build/dev/test_helper_coherent_frequency --phase
```

`--probe` and `--frequency-probe` are two-dwell early-gate checks, not
36-second sensitivity tests. `--frequency-regression` checks a small deterministic
set including short/noise-only rejection, clean coverage and a curvature
counterexample; it does not establish a false-alarm probability. Individual
study output separates signal evidence, availability of conditional and guarded
uncertainty, ≤0.1 Hz widths, empirical coverage, point error and held-out model
error. Receiver-only CPU excludes waveform generation, truth comparisons and
printing. During the reported eight-seed 15 kW/220 dB reduced-mode runs it was
below 0.03 seconds per 36-second observation on the local test host; host and
compiler matter, and a work cap can decline other geometries.
