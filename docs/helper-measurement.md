# Independent Helper pilot measurement

For the current integration context and cross-study evidence map, see the
[Helper integration guide](helper-integration.md). This page reports one
standalone measurement receiver and its bounded results, not a universal Robust
receiver requirement.

`HelperMeasurement` is a pilot-only experimental receiver in
[`helper_measurement.hpp`](../include/datapump/helper_measurement.hpp). It is
separate from the live and ordinary Robust receivers. Its input is signed
complex baseband samples covering the complete declared high-pilot search
interval; it is not an adapter for the application's real audio input. The
caller supplies the aperture's first sample time and signed RF shift coefficient
for each known dwell. This interface does not retune hardware, establish the
FHSS calendar, decode the auxiliary channel, or apply corrections to Robust.

The per-dwell receiver locates the strongest high tone in the configured search range,
then finds the low tone near its expected separation. It fits their frequencies
and phases over each supplied settled aperture and combines accepted observations
in a dated linear model. The high-pilot assumption is the quiet-spectrum
assumption in [Helper Carrier](helper-carrier.md#quiet-spectrum-assumption),
not a general interference-identification guarantee. Search bounds and the low
pilot's required region must fit the complex capture Nyquist interval. No
simulation truth is used to center the search. The receiver stores at most 256
accepted apertures and rejects overlapping or out-of-order observations.

For nominal pilot frequency `f`, signed shift coefficient `F`, and estimates at
the snapshot time, the fitted model is

```text
observed f = a * f + b
a = 1 + audio_ppm * 1e-6
b = F * shift_ppm * 1e-6
```

The two pilots permit a relative audio-frequency scale and a common
displacement to be separated. The quantities describe the link: absent external
references, individual transmitter and receiver clocks and unmodeled Doppler
cannot be separately identified. The linear model also estimates the rates of
these two quantities, but their extrapolation assumes the same evolution
continues. The returned covariance is conditional on the fitted model and
declared common Wiener phase diffusion and measurement noise; it is not an
operational confidence guarantee. `predict` retains the full frequency and
drift covariance at a requested date and separately reports the uncertainty of
an independent future aperture's common phase process. A comparison with a new
measurement must also account for that aperture's process and thermal error;
the fitted mean's uncertainty alone is not a bound on the new residual.
For a supplied common phase diffusion amplitude `q` in radians/√s, the model's
frequency variance for a rectangular aperture of duration `T` is
`3 q² / (10 π² T)`, under a small-phase, high-SNR linearization. The requested
example diffusion levels fall within this small-phase regime, but the
configuration permits larger values for which this approximation is not
validated against nonlinear phase wrapping or the acquisition threshold. The
model does not cover independent audio-clock frequency acceleration, abrupt
oscillator changes, or unspecified propagation effects.

For a signal obeying the same model, a conceptual correction is
`y(t) = x(t/a) * exp(-i*2*pi*b*t/a)`: output samples step through the input by
`1/a`, giving an output/input sample-count ratio of `a`, and the mixer frequency
in output-time coordinates is `-b/a`. This is an example correction, not an
implemented live signal path. A hop changes `F` and can introduce a new random
phase. `helper_hop_phase_change` estimates the observed phase discontinuity at
a known boundary using the two adjoining local fits; it does not predict a
future independent phase. Its accuracy is conditional on boundary timing and
the phase evolution between the observed apertures.

## Simulation and checks

[`test_helper_measurement.cpp`](../tests/test_helper_measurement.cpp) and its
independent physical-time source
[`helper_measurement_sim.hpp`](../tests/helper_measurement_sim.hpp) generate
two simultaneous tones at 550 and 2400 Hz, with the low pilot at −3 dB power.
The fixture supplies known 400 ms dwell and hop commands and samples the central
360 ms, with random phase changes on retunes. It includes separate transmit and
receive audio and shift-clock parameters, linear drift, common Brownian phase
diffusion within an aperture, Gaussian thermal noise, and configurable transmit
power and path loss. The common phase process is advanced through the discarded
boundary intervals. A synthetic 950 Hz auxiliary at −9 dB adds representative
gating, reversal and symbol transitions; it is not the actual metadata waveform
and is not decoded. The fixture does not simulate radio filters, nonlinearities,
or physical hardware. Those omissions limit any sensitivity or operational claim.

The noise density is `10 log10(k T * 1000) + NF` dBm/Hz with temperature `T`
in kelvin, Boltzmann constant `k`, and noise figure `NF` in dB. With unit high
pilot power and linear `C/N0`, each component of complex Gaussian noise has
variance `Fs_physical / (2 C/N0)`, where the physical ADC rate includes its
clock error; changing capture rate therefore does not change the modeled noise
density. The example settings use 290 K, 10 dB noise figure, and
40 dB path loss. A 24 kHz complex capture spans −12 to +12 kHz and contains
the free-running HF prior, not merely a truth-centered narrow window. The
simulation runs independent scenarios on separate threads.

The three shift profiles use the illustrative relative-link assumptions in
[oscillator models](oscillator-models.md): free-running crystal 100 ppm and
0.5 degrees/√s, IC-7100 example 1 ppm and assumed 0.05 degrees/√s, and GPSDO
OCXO 0.0001 ppm and 0.005 degrees/√s. The IC-7100 RF assumption does not
specify its audio clock. These are models, not measured devices. The example
uses an independent crystal audio prior, +37 ppm transmit and −13 ppm receive
audio clocks, 0.015 ppm/s transmit audio drift, and shift drift of 0.00005 times
the selected shift offset per second. The configured shift offsets are 80,
0.8, and 0.00008 ppm respectively. The older example results below used five
fixed hop offsets of up to ±1.2 MHz around 30 MHz or ±2 MHz around 5.8 GHz.
Current FHSS test fixtures use the keyed randomized schedules described below;
the historical figures are not results for those newer calendars.

Build and run the focused fixture after configuring a compatible `build/dev`:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement --parallel 2
build/dev/test_helper_measurement --feasibility-only
build/dev/test_helper_measurement --suite
```

The feasibility command performs only the very short weak-signal probe. The
suite adds noiseless controls, three 36-second FHSS scenarios with three seeds
each, one 36-second non-FHSS control, observed hop measurements, and a held-out
corrected aperture. A short-probe result does
not establish a 36-second sensitivity boundary. The suite is part of the
`contract` group; the individual executable does not replace that group or the
applicable broader validation described in [building](building.md#testing-stages).

## Keyed randomized hop fixtures

The current independent tests use 400 ms dwell schedules from
[`helper_measurement_hops.hpp`](../tests/helper_measurement_hops.hpp). They use
the modem's cryptographic FHSS keystream with deterministic test keys; the test
keys make results reproducible, not public or fixed hop orders in operation.
The receiver sees the commanded RF frequency for every dwell and does not
infer the sequence from simulated oscillator truth. Changes of retune phase are
modeled independently, and no future hop phase is inferred from earlier hops.

| Fixture | Centers and schedule | Example largest jump in the first 90 dwells |
| --- | --- | ---: |
| HF | 50 centers from 28.775 to 31.225 MHz, spaced 50 kHz; a fresh keyed permutation per 50-hop cycle | 2.4 MHz |
| FCC 5.8 GHz profile | 100 centers from 5.728 to 5.8468 GHz, spaced 1.2 MHz; constrained keyed random draws | 112.8 MHz |
| EU 5.8 GHz SRD profile | 20 centers from 5.7316 to 5.8684 GHz, spaced 7.2 MHz; a fresh keyed permutation per 20-hop cycle | 122.4 MHz |

The FCC fixture chooses randomly among channels unused in the preceding 74
dwells and not yet used in the current 100-dwell block; every block visits each
channel once. Thus any recurrence is at least 75 dwells apart. Its test checks
the complete finite recurrence history and exact overlap in 30-second windows
at several fractional dwell offsets. Ten test keys passed with at most 0.4 s
per channel in each checked window. The EU fixture's maximum observed return
time was 15.6 s, within the 32 s illustrative limit, and its center span is
136.8 MHz. These are schedule and geometry checks, not measurements of RF
occupied width, emissions, antenna, transmitter power, or legal authorization.
The HF range is an experimental geometry and does not establish authorization
across that span.

The relevant FCC scheduling provisions are in
[47 CFR §15.247](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247).
The EU profile corresponds to the non-specific SRD example in
[ETSI EN 300 440 V2.2.1](https://www.etsi.org/deliver/etsi_en/300400_300499/300440/02.02.01_60/en_300440v020201p.pdf).
EU categories and national implementations differ; these profiles do not
claim full device compliance. In particular, neither schedule test makes the
15 kW link-budget example a compliant unlicensed transmitter.

The existing production `FhssSchedule` draws an independently shuffled
permutation each cycle; it does not enforce the FCC fixture's rolling revisit
constraint. In one 100-channel, three-cycle diagnostic, 221 of 300 sampled
75-dwell windows repeated a channel. The constrained schedule above is a test
fixture and is not an integration change to the production scheduler. See
[spread-spectrum controls](spread-spectrum-controls.md) for existing profile
and occupancy qualifications.

Run the schedule checks with:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement_hops --parallel 2
build/dev/test_helper_measurement_hops
```

## Earlier example results after 36 seconds

In the earlier fixed-five-offset FHSS simulation, three independent seeds per profile acquired
all 90 apertures. The high pilot was transmitted at −54 dBm, received at
−94 dBm after 40 dB path loss, and had modeled C/N0 of 69.975 dB-Hz; the
low pilot was 3 dB weaker. Effective residual-based C/(noise plus simulated
auxiliary interference) was approximately 53.62 dB-Hz for the high pilot and
50.62 dB-Hz for the low pilot. The selected frequency ranges and modeled errors
below are for these stated conditions, not general reception guarantees.
`DATA 1500` denotes the frequency predicted by the affine model at a nominal
1500 Hz tone and the center RF shift, rather than a decoded Robust signal.

| Shift oscillator example | Initial high-pilot search half-width | Conditional DATA 1500, 3σ half-width | Observed hop phase-change RMS error | Observed hop frequency-change RMSE |
| --- | ---: | ---: | ---: | ---: |
| 30 MHz free-running crystal | 9360.72 Hz | 0.003268–0.003269 Hz | 0.338–0.358° | 0.00351–0.00369 Hz |
| 30 MHz IC-7100 example | 94.32 Hz | 0.002860–0.002861 Hz | 0.070–0.094° | 0.000757–0.000909 Hz |
| 5.8 GHz GPSDO OCXO | 2.4606 Hz | 0.002861–0.002862 Hz | 0.070–0.081° | 0.000711–0.000994 Hz |

These are ranges across three seeds, and the initial widths include a 3× model
margin and the largest commanded hop shift. The conditional widths describe
the fitted instant and model; they are not demonstrated search bounds for later
reception. All ten conditional intervals across the nine FHSS runs and the
non-FHSS control covered the corresponding simulated DATA 1500 frequency, and
all ten held-out measured residuals fell within their conditional intervals.
Ten trials cannot establish calibrated coverage or a safe acquisition range.
The FHSS phase statistics are
errors for the observed adjacent hop discontinuities; they confer no knowledge
of the next random phase.

An earlier no-auxiliary run with a thermal-only covariance covered the fitted
DATA frequency in eight of nine cases; the IC-7100 seed 732 missed by
0.0003451 Hz against a 0.0003365 Hz conditional half-width. That diagnostic
exposed the need to represent common phase diffusion. It used different
simulation and estimator settings and is not part of the nine revised runs
above.

The non-FHSS control used the 30 MHz free-running profile and seed 799. All
90 apertures were accepted. The DATA 1500 estimated displacement was
+2404.4026 Hz with −0.000559294 Hz error and conditional 3σ half-width
0.00327370 Hz. The held-out correction used a −2404.2556 Hz mixer and an
output/input sample-count ratio of 1.0000499; its high-pilot residual was
−0.00275361 Hz with conditional 3σ half-width 0.0141048 Hz. The adjacent
aperture phase-change RMS error was 0.3454°, and the frequency-change RMSE
was 0.003406 Hz; these are continuous-pilot measurements, without random hops.

One reproducible example for each profile illustrates the signed correction.
The predicted DATA 1500 displacement is at the center shift. The mixer and
sample ratio below apply to the following held-out hop, 28.8 MHz for the HF
examples and 5.798 GHz for the microwave example. The phase is separately
reacquired after that hop.

| Example seed | Relative audio error | Relative shift error | DATA 1500 displacement | Held-out mixer correction | Output/input sample-count ratio | Held-out residual and conditional 3σ half-width |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Free-running, 731 | +50.027477 ppm | +80.144292 ppm | +2404.4038 Hz | −2308.0863 Hz | 1.00005 | −0.0017971 Hz; ±0.0141366 Hz |
| IC-7100, 732 | +49.948428 ppm | +0.80148543 ppm | +24.119486 Hz | −23.082105 Hz | 1.0000499 | +0.0000682 Hz; ±0.0118633 Hz |
| GPSDO, 733 | +49.74358 ppm | +0.000080449853 ppm | +0.54122452 Hz | −0.46645751 Hz | 1.0000497 | +0.0005594 Hz; ±0.0117951 Hz |

The DATA 1500 fitted frequency errors for these three seeds were +0.00068645,
+0.0004060, and +0.0005814 Hz, respectively. The held-out residuals include
model extrapolation and 32-tap windowed-sinc sample interpolation; their
conditional widths include the new aperture's process and thermal errors.

The short feasibility probe used 0.8 seconds and a high-pilot C/N0 of
29.975 dB-Hz; it acquired two of two apertures in one seed, both pilot
frequencies within 2 Hz. This small result is
neither a sensitivity limit nor evidence that the same acquisition rate persists
over 36 seconds. Ten 36-second scenarios ran with three scenario threads in
approximately 6 seconds in the reported local run; timing is specific to that
host and build.

## Earlier weaker link diagnostics

These results were obtained with the earlier fixed-five-offset hop fixture;
the current test source uses randomized keyed calendars. They remain historical
diagnostics, not measurements of the current calendar. The weaker-link fixtures
specify **total active-mean Helper power**, rather
than assigning the stated power to the high pilot alone. For each hop setting,
the high pilot's straight-tone power is calculated from the transmitter's
`pilot_power_fraction / mean_power(options, hopping).total`. The resulting
fractions are approximately 0.6376 without FHSS and 0.6451 with FHSS. The
simulation retains the 290 K thermal-noise and 10 dB noise-figure assumptions
above; the real RF chain, propagation and hardware remain unmodeled.

At 40 W total Helper power and 180 dB path loss, the 30 MHz IC-7100 model's
high-pilot C/N0 was 28.04 dB-Hz without FHSS and 28.09 dB-Hz with FHSS. In
three independent 36-second simulations for each hop setting, all 90
apertures were admitted and both fitted pilot frequencies were within 2 Hz of
their simulated values. Per-aperture high- and low-pilot frequency RMS errors
were 0.061–0.081 Hz and 0.092–0.115 Hz. The fitted DATA 1500 frequency error
ranged from −0.02269 to +0.00715 Hz against a conditional 3σ half-width of
about 0.054 Hz; relative audio-clock error ranged from −31.50 to +20.94 ppm.
Held-out corrected high-pilot residuals ranged from −0.0982 to +0.1011 Hz.
Observed adjacent phase-change RMS error was 7.16–9.66°; that includes
continuous-pilot phase change in the non-FHSS cases. These six samples do not
establish coverage or a sensitivity limit.

The initial two-dwell, 0.8-second probes also used 15 kW total Helper power,
220 dB path loss and the 5.8 GHz GPSDO model. High-pilot C/N0 was 13.78 dB-Hz
without FHSS and 13.83 dB-Hz with FHSS. The original per-dwell two-pilot
receiver produced no valid measurement in either two-dwell simulation. This
is a short-probe result, not a conclusion about a 36-second observation.

The synthetic-source probes and six-case HF diagnostic can be run with:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement --parallel 2
build/dev/test_helper_measurement --weak-probe
build/dev/test_helper_measurement --weak-36
```

A separate sample fixture,
[`test_helper_measurement_transmitter.cpp`](../tests/test_helper_measurement_transmitter.cpp),
uses the actual `helper::Generator` waveform, including its metadata auxiliary,
with independent clock, shift, hop-phase and thermal-noise models. It feeds
only the central 360 ms of each dwell to the receiver, so the receiver does not
observe the transmitter's boundary ramps in these tests. The RF-hop calendar
is imposed in receiver nominal-clock time while the generated waveform uses its
independent transmitter clock; it is a known calendar model, not a validated
hardware retuning synchronization. At the same 40 W,
180 dB, 30 MHz IC-7100-model settings, one 36-second seed per hop mode admitted
90 of 90 apertures with both pilot frequencies within 2 Hz. The high/low
frequency RMS errors were 0.0663/0.1040 Hz without FHSS and 0.07835/0.10817 Hz
with FHSS. The fitted DATA 1500 errors were −0.01457 Hz and +0.01341 Hz,
respectively. The relative audio estimates were +76.2047 ppm and +64.0929 ppm,
compared with approximately +50 ppm truth. These are two single-seed checks,
not calibrated sensitivity or hardware results.

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement_transmitter --parallel 2
build/dev/test_helper_measurement_transmitter --weak-36
```

For the weaker microwave link, `HelperPooledMeasurement` is a separate
experimental acquisition path. Its bounded pilot channels and narrow frequency
prior retain the original pilot frequencies; it combines dwell energies rather
than coherently combining independently reset hop phases. The caller provides
known RF commands, time coverage and drift bounds. It requires at least
36 seconds by default and requires a complete time watermark. It accepts
isolated missing or shortened apertures if its configured sampled-support and
maximum-gap limits remain satisfied. Raw capture time and the shorter retained
decimation time are reported separately. The
present implementation accepts a high-pilot prior no wider than 64 Hz and at
most 100 dwell apertures, so it does not cover the documented full free-running
HF prior without a separate prior-narrowing stage. It does not measure or
predict the independent hop phases. Its evidence threshold and local curvature
intervals are model-based
engineering quantities, not calibrated false-alarm or coverage guarantees.

The per-dwell fit stops frequency refinement when its bracket is at most
20 µHz wide, giving at most 10 µHz midpoint bracket error; its numerical
sigma floor is 10 µHz. This floor is an engineering uncertainty component,
not a bound on total estimator error. Both receiver
paths cache unchanged fits; advancing a pooled watermark still checks whether
the capture is sufficiently recent and complete. The default pooled admission
requires at least 36 seconds elapsed, at least 80% raw sampled exposure, and
no unobserved gap above 0.8 seconds. These are configurable engineering
admission thresholds, not claims that the oscillator remains stable across any
accepted gap. Retained modeled duration can be slightly shorter than raw
exposure because only complete decimation groups enter the likelihood.

Both receivers compare their full-history linear model with a recent-half
offset and drift fit. Their conditional covariance is enlarged to include the
recent fit's uncertainty and disagreement with the full estimate; clear
inconsistency can withdraw precision while preserving available signal evidence
or point fields. This check addresses tested model departures but does not
guarantee correctness for arbitrary oscillator motion. The pooled phase
contribution and its local curvature approximation remain uncalibrated for
general heterogeneous dwell weights. A configured search exceeding 64 million
estimated reduced-sample projection products is declined before enumeration
with `work_limited`; a separate 100,000-candidate cap also applies. This is a
bounded-work policy, not a processor-independent CPU-time guarantee.

In an earlier 24 dB-Hz mismatch diagnostic, a centered quadratic RF frequency
term `0.0007 (t−18)²` Hz caused four interior linear fits to claim a 3σ
half-width at or below 0.1 Hz even though all four errors exceeded 0.1 Hz; four
additional fits were boundary-limited. After the recent-half guard, the four
interior cases had half-widths 0.204–0.208 Hz and covered their modeled errors;
none claimed a ±0.1 Hz interval. Four 30 dB-Hz HF wide-prior cases with the same
curvature had errors up to 0.146 Hz and guarded half-widths 0.174–0.198 Hz,
also covering all four. These deliberately chosen seeded checks demonstrate
the observed failure and the behavior of this guard for that model; they do
not validate general mismatch coverage.

At 24 dB-Hz with the current default pooled support thresholds, all eight
tested cases with ten dispersed missing dwells and all eight with variable
0.28–0.36 s apertures retained signal evidence. Five consecutive missing
dwells created a 2.04 s gap and were declined in all eight cases. A small
number of noise-only controls did not qualify; this is not a measured
false-alarm probability. The dated estimate and its conditional interval do
not establish correct future correction when the model changes after the
observation.

The tracking budget is measured as CPU time consumed by a single receiver
thread during construction, every admitted aperture, the regular 400 ms
snapshot polls, final fitting and later cached polls. Simulation sample
generation, truth checks and reporting are outside that timer; elapsed wall
time is not substituted. In local Release measurements on an AMD Ryzen 5 PRO
5650U with GCC 14.2, the earlier per-dwell path's five-run median was 0.6605 s
for a 36-second observation at 24 kHz. An earlier 5 µHz refinement with the
recent-fit guard measured 0.456 s median and 0.469 s maximum in four matched
30 dB-Hz HF cases; one hop-wander mismatch case reached 0.498 s. After widening
the refinement bracket to 20 µHz, the final strong-path run used eight seeds
at each of 20, 24, 28, 30 and 35 dB-Hz on the randomized FCC profile and
eight seeds at each of 25, 30 and 35 dB-Hz on the randomized HF profile.
FCC medians were 0.401–0.434 s with a 0.465 s maximum; HF medians were
0.407–0.420 s with a 0.468 s maximum. For the narrow
GPSDO prior, eight 24 dB-Hz pooled baseline cases measured about 0.033 s
median and 0.039 s maximum. A single 64 Hz pooled-prior case took 0.116 s and
projected 55,409,760 reduced-sample products. Another supported geometry took
4.486 s before the work limit; with the limit it was declined in 0.00331 s and
reported 1,623,409,920 projected products. These are local measurements, not a
real-time guarantee on different hardware or for untested configurations. The
work limit makes the latter configuration unavailable rather than solving it
within the preferred budget.

The independent study is run with the same configured build using:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement_continuum --parallel 2
build/dev/test_helper_measurement_continuum --probe
build/dev/test_helper_measurement_continuum --smoke
build/dev/test_helper_measurement_continuum --sweep
build/dev/test_helper_measurement_continuum --mismatch
build/dev/test_helper_measurement_continuum --wide-cost
build/dev/test_helper_measurement_continuum --robustness
build/dev/test_helper_measurement_continuum --strong-cost
```

The study reports detection, boundary-limited and uncertainty availability,
absolute DATA 1500 model error, interval width and coverage separately. Its
noise, finite aperture, oscillator and hopping assumptions are test inputs;
results must not be interpreted as calibrated hardware sensitivity.

In three synthetic 15 kW/220 dB microwave seeds for each hop setting, all six
pooled observations met the signal evidence gate; none of six matched
noise-only controls did. All 90 apertures were retained in each case, and an
early 35.99-second snapshot was rejected. An initial implementation exposed
only two of six signal cases as valid when the optimum reached a configured
parameter bound; after distinguishing a detected tone from a qualified local
uncertainty, all six were reported as detections, but **four were
boundary-limited and had no valid covariance or 3σ interval**. The other two
had conditional DATA 1500 3σ half-widths of 0.272 and 0.291 Hz. Across all
six, DATA 1500 model error ranged from −0.0138 to +0.119 Hz and audio-clock
error from −117.3 to +48.1 ppm. A separate truth-blind noncoherent search on
five held-out dwells from 36 to 38 seconds found high-pilot residuals from
−0.40 to +0.20 Hz. This small diagnostic does not establish a safe search
interval, phase accuracy, false-alarm probability or hardware sensitivity.

```sh
build/dev/test_helper_measurement --weak-pooled-short
build/dev/test_helper_measurement --weak-pooled-36
```

The actual-transmitter fixture also exercised the 15 kW/220 dB microwave
case with three seeds per hop mode. All six retained 90 apertures and passed
the pooled evidence gate, but all six fits were boundary-limited with no valid
uncertainty interval. The fitted DATA 1500 errors ranged from −0.06631 to
+0.15964 Hz, and relative audio-clock errors from −136.814 to +82.010 ppm.
Separate noisy held-out fits, searched within ±2 Hz of the receiver prediction,
had residuals from −0.02831 to +0.30230 Hz. The held-out search window is an
experimental evaluation window, not a confidence interval. These six examples
do not qualify search narrowing or hop phase.

```sh
build/dev/test_helper_measurement_transmitter --pooled-36
```

## Random-hop signal-level study

The current independent study swept high-pilot C/N0 from 5 to 45 dB-Hz,
including 1 dB steps near the ±0.1 Hz transition. It used eight seeded,
cryptographically keyed hop calendars per level, a 24 kHz complex capture,
90 settled apertures, and the relative 5.8 GHz GPSDO oscillator model. Its
nominal high and low pilots are 2400 and 550 Hz; the low pilot is 3 dB weaker.
The table reports the fitted 1500 Hz DATA frequency at the observation's final
date, not a decoded Robust signal or an independently measured new dwell.
The 3σ half-widths are conditional estimates, not calibrated confidence
intervals. C/N0 is in dB-Hz; in a 2,500 Hz noise bandwidth the corresponding
high-pilot SNR is C/N0 minus approximately 33.98 dB.

| FCC randomized synthetic C/N0 (dB-Hz) | Evidence detections / 8 | Valid conditional intervals / 8 | Intervals ≤ ±0.1 Hz / 8 | Largest point error among detections |
| ---: | ---: | ---: | ---: | ---: |
| 5 | 0 | 0 | 0 | unavailable |
| 10 | 8 | 0 | 0 | 0.227 Hz |
| 20 | 8 | 1 | 0 | 0.044 Hz |
| 25 | 8 | 5 | 0 | 0.022 Hz |
| 28 | 8 | 7 | 0 | 0.017 Hz |
| 30 | 8 | 8 | 0 | 0.013 Hz |
| 31 | 8 | 8 | 8 | 0.012 Hz |
| 35 | 8 | 8 | 8 | 0.0078 Hz |
| 45 | 8 | 8 | 8 | 0.0028 Hz |

At 30 dB-Hz, all eight FCC synthetic intervals covered their simulated point
errors but their maximum half-width was 0.1017 Hz; none were at or below
0.1 Hz. At 31 dB-Hz all eight intervals were at or below 0.1 Hz and covered
their point errors. The corresponding EU randomized synthetic and no-FHSS
controls also produced eight of eight intervals at or below 0.1 Hz at 31 dB-Hz.
The FCC randomized **actual transmitter waveform** test produced seven of
eight such intervals at 31 dB-Hz, with the remaining fit boundary-limited;
32 dB-Hz was its first tested level with eight of eight. These are transition
observations for eight seeds and the declared drift and noise models, not a
physical sensitivity threshold or a reliability estimate.

The 30 MHz IC-7100-model strong path used all 50 randomized 50 kHz channels:
at 25 dB-Hz all eight points were within 0.1 Hz but no intervals were that
narrow; at 30 dB-Hz seven of eight intervals were at or below 0.1 Hz; at
35 dB-Hz all eight were. All eight conditional intervals at each of those
levels covered their simulated DATA-frequency errors. Eight pooled FCC and
eight no-FHSS noise-only controls and four strong-path HF noise-only controls
produced no qualified estimate; these small counts do not measure the false
alarm rate. A separate held-out aperture is searched around the model prediction
without using simulation truth to select the search center; its individual
noise and model errors remain distinct from the fitted mean interval.

The full sweep with the earlier 5 µHz refinement measured maximum receiver-only
CPU times of 0.0493 s for pooled cases and 0.489 s for strong-path cases,
including regular polls and the final fit on the previously identified host.
The subsequent 20 µHz refinement was evaluated in the narrower strong-path
run described above: at 30 dB-Hz all eight FCC intervals were at or below
±0.1 Hz and covered their modeled errors; seven of eight HF intervals were
that narrow and all eight covered. At 20 dB-Hz all eight FCC point estimates
were available, but only four cases admitted every individual dwell. Sixteen
noiseless sample-source dwell pairs in constant-offset and linear-drift
controls had a maximum individual tone error of 15.6891 µHz, within the
30 µHz test margin. The 10 µHz numerical sigma component is not a bound on
the complete fit error. Signal synthesis and truth comparisons were excluded
from CPU timing. The 0.5 s goal was met in these finite local runs, including
randomized wide hops, but is not a guarantee for other hardware, search priors,
sample rates, or model configurations.
