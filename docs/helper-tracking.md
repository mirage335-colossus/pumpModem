# Standalone Helper tracking experiments

See the [Helper integration guide](helper-integration.md) for current
integration constraints and the cross-study view of inexpensive approaches.
This page reports specific tracking algorithms and experiments, not universal
Robust receiver requirements.

This page describes pilot-only receiver components and simulation experiments.
They do not integrate the ordinary Robust receiver or claim Robust acquisition
or decoding. The fixed lead is 36 seconds of Helper observation and another two
seconds of detection before the probe at 38–40 seconds. The 36-second
frequency estimate and the fresh phase measurement for each later hop are
different outputs: a randomly chosen future hop has no predicted phase.

The Helper and diagnostic probe powers are specified independently. For
example, a 40 W Helper and an active 40 W probe represent 80 W nominal combined
power during the probe interval; they are not a 40 W total transmitter case.
Path loss and thermal noise apply to both. The flat common-transfer model used
by the principal cases does not describe arbitrary frequency-selective channels,
retune delays, hardware limits, antennas or a physical propagation limit.

## Frequency-aided phase

`HelperAidedPhase` uses the accumulated Helper frequency model to place the two
pilot correlations within each settled aperture. Each aperture estimates its
own phases; the frequency prediction is not a phase prediction or an additional
independent measurement. The center-dated complex correlations use a Hann
window. Four sub-apertures check residual frequency and phase evolution, and
matched-window local sidebands estimate an inflated noise level. A consistency
failure requests the existing full-range Helper acquisition; no payload or
probe is involved in selecting that fallback. Work is bounded by the configured
maximum number of input samples.

The local-noise allowance, fit scores and residual limits are engineering
guards under a local tone and noise model. They are not calibrated probabilities
of false qualification in arbitrary colored interference. Matched tapers are
important: a narrower noise estimate paired with an incompatible rectangular
fit can conceal sidelobe leakage from a bright probe. The frequency model's
measurement intake validates dates, geometry and numerical arithmetic, but the
caller remains responsible for the validity of the detection and uncertainty
it supplies.

## Relative time and phase

`HelperRelativeClock` uses the difference between simultaneously dated high-
and low-pilot phases. Under the declared common-transfer model, the arbitrary
common RF phase of each hop cancels. The phase difference observes source time
modulo `1 / (2400 - 550)` seconds, approximately 0.5405 ms. The first accepted
reference fixes one arbitrary source-time coordinate; the unknown constant time
and phase remain for a later acquisition process to resolve. The receiver must
not pick a new reference at every hop or silently reseed after a gap.

The fit retains at most the configured number of observations and alternatives,
and limits extrapolation age. Its radius includes a conservative influence
bound for correlated observations and a declared differential-channel phase
allowance. A constant differential phase is absorbed in the unknown reference;
changes in differential phase and changes in audio time are not separately
observable from these two pilots without further information. If ambiguity,
model contradiction or the work bound prevents a unique supported result, the
component reports that limitation rather than asserting an absolute time.
Support is conditional on the declared clock and channel models and is not a
calibrated coverage guarantee.

The relative tone and APSK metrology use a single fixed gauge computed from
simulator clock truth only for evaluation. Neither probe is used to select a
branch, set a correction, fit gain, timing or phase, or re-align individual
hops or symbols. The clean twin is subjected to the correction obtained from
the noisy Helper and is evaluated separately from the noisy probe. The
conditioner can qualify only output whose phase and interpolation support is
available; an accepted pilot anchor is not automatically a fully supported
output window.

The earlier standalone relative-reference path has been extended to evaluate
the 360 Hz 64-APSK probe under that same fixed-gauge convention. With 40 W
Helper and 40 W probe at 180 dB, seeds 901 and 905 each supported five of five
probe hops and 360 evaluated symbols. Noisy EVM changed from 1.466 to 0.621
and from 1.466 to 0.628; the corresponding clean-twin EVM values were 0.292
and 0.251. For comparison, the noisy oracle EVM was 0.536 and 0.570. These are
two seeded examples using the earlier relative path, not measurements of the
new two-pilot clock tracker or Robust decoding. At this link condition, noise
and tracking error both contribute to EVM.

The matched comparison also exercises the newer joint-clock path with the
40 W 64-APSK probe at 180 dB. In seeds 901 and 905, its noisy EVM was 0.550
and 0.570, and its clean-twin EVM was 0.110 and 0.062. The corresponding
broad-acquisition path gave noisy EVM 0.654 and 0.586 and clean-twin EVM 0.349
and 0.143. The individual paths retain their declared fixed receiver reference;
no probe fit picks or changes it. Each result uses 330–333 supported known-date
symbols. These are two matched simulated realizations, not evidence of decoding
success or a calibrated performance distribution.

The guarded impairment checks also demonstrate limits of the current model. A
noise-only control produced no valid 36-second snapshot. A two-dwell
observation gap retained the joint reference and five held-out
windows in the tested case. A 24-dwell gap made the clock unavailable at 36
seconds, but later restored four of five held-out windows using the original
reference. Their clean-twin frequency RMS was 0.0287 Hz. A 1.2 Hz
post-observation frequency step caused three fallback acquisitions and two
frequency-model resets. Two of five held-out windows recovered, with a
clean-twin frequency RMS of 0.189 Hz, exceeding 0.1 Hz. A late settling phase change inside a
settled aperture caused the aided and joint paths to withhold the affected
dwell; the broad path admitted it with a 67.64° maximum clean sample error.
Neither this finite set of controls nor the fallback establishes recovery for
arbitrary oscillator or retune changes.

In particular, the 0.02 rad differential-channel allowance is an assumption,
not a detector guaranteed to recognize a violation. A changing 0.8 rad
low-pilot phase at 180 dB was admitted by the joint path in the sampled test,
with 13.62° maximum clean sample phase error. An unknown changing differential
transfer can imitate audio timing; this check must not be treated as validation
of the declared channel bound. At 160 dB in the sampled run, the joint path
withheld the two affected dwells and supported three later windows with its
original reference. That result does not establish universal detection.

## Matched 40 W simulation results

The following HF cases use the actual Helper waveform, keyed-random 50-channel
hops, 40 W Helper and 40 W separately powered tone probe, 290 K input
temperature and 10 dB noise figure. Relative RF error is +0.8 ppm; transmit
and receive audio errors are +37 and −13 ppm, and the common oscillator phase
diffusion is 0.05°/√s. The shared-transfer and flat-channel assumptions above
apply. Each result uses the same impaired capture for the compared methods.

The table describes the aided-phase plus relative-clock path. After the
36-second frequency snapshot, it continues to use Helper measurements and a
fresh phase from each held-out hop. Each seed provides five 240 ms tone
windows. `Phase RMS` is the median and maximum across per-seed RMS values;
the last column is the largest absolute error among supported clean-twin
samples. Simulator clock truth supplies one fixed evaluator gauge per path and
does not enter tracking or choose a branch.

| Loss | Seeds | Supported windows | Clean-twin phase RMS median; maximum | Worst supported sample phase |
| ---: | ---: | ---: | ---: | ---: |
| 180 dB | 8 | 40/40 | 3.18°; 6.34° | 12.31° |
| 175 dB | 4 | 20/20 | 2.54°; 3.56° | 6.89° |
| 174 dB | 4 | 20/20 | 2.26°; 3.17° | 6.13° |
| 173 dB | 4 | 20/20 | 2.02°; 2.82° | 5.46° |
| 172 dB | 4 | 20/20 | 1.80°; 2.51° | 4.86° |
| 171 dB | 4 | 20/20 | 1.61°; 2.24° | 4.33° |
| 170.5 dB | 4 | 20/20 | 1.52°; 2.11° | 4.08° |
| 170 dB | 4 | 20/20 | 1.43°; 2.00° | 3.85° |
| 165 dB | 4 | 20/20 | 0.81°; 1.12° | 2.15° |
| 160 dB | 4 | 20/20 | 0.45°; 0.63° | 1.19° |

At 180 dB, the broad-only, aided-only and aided-plus-clock paths had median
clean-twin phase RMS of 6.54°, 10.55° and 3.18° respectively, across the
same eight seeds. Fresh held-out high-pilot phase measurements for the aided
path had median per-seed RMS 3.15°, with worst individual measured phase
error 11.60°. The joint path therefore improves the fixed-reference output in
these cases, not every individual pilot phase estimate. Each joint run had
five phase anchors and 39,720–39,723 supported samples in approximately
57,554 output samples. No admitted timing-cycle slip was observed. The
maximum absolute evaluator-only time-map error after removing the one constant
gauge was 5.09 µs; the change between the first and last admitted anchors was
between −1.02 and +0.77 µs.

The latest 36-second joint-correction frequency envelope is reported separately
from these later phase observations. Its conservative bound sums the global
high-pilot three-sigma term and the relative-clock support term without assuming
their errors are independent. The largest conditional half-width among the
sampled HF runs was ±0.29585 Hz at 180 dB, ±0.16452 Hz at 175 dB, ±0.10336 Hz
at 171 dB, ±0.09755 Hz at 170.5 dB and ±0.09207 Hz at 170 dB. The sampled
transition depends on this model and is not a physical sensitivity limit or
calibrated coverage guarantee. At 180 dB the aided global frequency fit alone
had a largest conditional three-sigma half-width of ±0.22348 Hz; the broad-only
fit had ±0.12347 Hz. Neither is the joint-correction envelope.

The maximum per-seed clean-twin frequency RMS across the five held-out windows
was 0.02635 Hz at 180 dB for the joint path. The noisy-tone RMS values were
0.073–0.134 Hz, with individual residual-fit standard errors summarized as
0.096–0.122 Hz; these are noisy measurements, not exact frequency truth.
The sampled 5.8 GHz FCC100 and EU20 wide-hop fixtures each had two seeds and
all ten joint tone windows supported at 180 dB. Their largest per-seed phase
RMS values were 3.97° and 4.30°, and their largest conditional 36-second joint
half-widths were ±0.31411 Hz and ±0.31322 Hz. These fixtures model randomized
schedules; they do not establish regulatory or hardware compliance.

On the local Ryzen 5 PRO 5650U Release build, the eight 180 dB cases used
0.060–0.069 seconds of process CPU for the joint receiver through 36 seconds
and 0.095–0.107 seconds through 40 seconds, including approximately 2.4
seconds of actual probe conditioning. On the same captures, the broad-only
path used 0.392–0.437 and 0.469–0.520 seconds respectively. For the joint
path, the 40-second acquisition, aided tracking, relative-clock and
conditioning CPU components were approximately 0.021–0.031, 0.041–0.050,
0.0007–0.0011 and 0.030–0.037 seconds respectively; fit and other receiver
work are included in the totals. Simulation generation used 0.381–0.423 seconds
and separate tone metrology 0.007–0.012 seconds, excluded from receiver CPU.
These process-CPU observations include receiver threads, are specific to
this host and workload, and do not measure conditioning every sample over the
full 40 seconds.

## Later ramp feasibility

The actual FHSS Helper waveform has known pilot ramps at dwell boundaries. A
standalone feasibility experiment profiles independent complex gains for the
pilots and auxiliary on each side of each boundary and combines scores rather
than assuming phase continuity across hops or decoding auxiliary data. It uses
keyed-random HF hops and the actual Generator waveform.

In one exploratory 89-boundary sample at 40 W and 180 dB, the reported timing
profile spanned 500 µs, comparable to the 540.5 µs pilot-phase ambiguity period;
the noise-only control was rejected. A 500 µs modeled retune perturbation gave
the same span and a 3 ppm audio-model mismatch gave 600 µs. At 160 dB the span
was one 100 µs grid cell. These are heuristic profile spans, not calibrated
intervals or evidence that an absolute branch is resolved. The experiment
assumes known noise power and RF offset, an almost exact audio-rate model, a
bounded 0–6 ms onset search and a flat transfer; it omits oscillator phase
diffusion and does not establish practical CPU cost. In particular, low-pilot
and auxiliary energy are difficult to distinguish within a 4 ms edge, so
auxiliary energy is fitted as a nuisance rather than decoded.

```sh
build/dev/test_helper_envelope_feasibility --short 901
build/dev/test_helper_envelope_feasibility --study 901
```

## Reproduction and measurement scope

The standalone tracker comparison uses keyed-random 50-channel HF hops at
50 kHz spacing and can select the existing 5.8 GHz wide-hop fixture. It feeds
matched simulated input to broad-acquisition and aided alternatives. The
fixture has explicit missing-observation, frequency-change, phase-diffusion,
retune and differential-pilot-phase controls. It uses the actual Helper
transmitter, an independently powered probe, thermal noise, and independent
audio and RF clock errors. Simulation truth is used to construct the channel
and evaluate residuals, never to acquire or update the receiver.

```sh
build/dev/test_helper_tracking --probe
build/dev/test_helper_tracking --case 180 901 hf clean
build/dev/test_helper_tracking --case 180 901 hf gap
build/dev/test_helper_tracking --case 180 901 hf differential-large
build/dev/test_helper_tracking --case 180 901 microwave clean
build/dev/test_helper_tracking --case 180 901 microwave-eu clean
build/dev/test_helper_tracking --case 180 901 hf clean apsk 40
build/dev/test_helper_tracking --robustness
build/dev/test_helper_live_probe --40w-relative 180 901 apsk 40
```

Measured receiver process CPU must include all receiver work and threads, and
be reported separately for the 36-second observation and the entire stated
stream. Startup/acquisition, steady tracking, fallback and actual sample
conditioning should be identifiable; simulation generation and independent
metrology must not be included as receiver CPU. A 2.4-second held-out
conditioning measurement does not establish the cost of conditioning a
complete 40-second stream. Host and build settings must accompany timing
comparisons.

The aided estimator uses direct narrowband correlations at the input sample
rate. A separate filter-and-decimate receiver has not been implemented or
benchmarked here; no claim is made that direct correlation is the minimum
possible cost. The later-ramp study is likewise exploratory and is not part of
the tracking receiver's qualification or CPU figure.
