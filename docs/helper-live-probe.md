# Standalone Helper live-style probe

This focused experiment tests Helper-derived sample conditioning without
running an ordinary Robust receiver. The sampled simulation and conditioner
accept complex I/Q. They are separate from the application's existing real-mono
Live session, GUI and hardware retune path. The physical cable experiment below
starts with real mono audio and uses a test-local analytic adapter. Neither test
establishes a Robust acquisition, decoding or hardware qualification.

## Helper-only timing, phase and conditioning

The receiver observes the actual Helper Generator waveform, including the two
straight pilots at 550 and 2400 Hz and its 950 Hz auxiliary. It obtains a
relative audio-clock scale and common frequency displacement from the Helper
measurements. The source time and phase are not fitted from the diagnostic
probe: its known symbols and samples are used only in the final evaluation.
Auxiliary APSK decoding is not required.

The first 40 ms of Helper startup are compared with the known startup waveform
to obtain a coarse time interval. This diagnostic allows an unknown common
complex gain and compares its fit with a more flexible individual-tone-gain
model. It refuses insufficient evidence, unresolved timing aliases, excessive
work, and the tested inconsistent-channel cases. Its radius and consistency
checks are engineering witnesses, not calibrated confidence bounds or a proof
against arbitrary channel responses. In an initial stress test, unmodeled
differential pilot phase produced a confidently wrong onset about 1.21 ms away;
a first same-onset veto also failed to detect it. The revised global
independent-tone onset comparison rejected those cases. A subsequent noiseless
control revealed numerical false rejection; the final implementation accounts
for its finite search bracket and floating-point evaluation.

The difference between high- and low-pilot phases measures source time modulo
`1/(2400-550)` seconds, approximately 0.5405 ms. `HelperPhaseClock` uses the
qualified startup interval to select an initial branch, then unwraps successive
Helper phase pairs using their measured dates and a declared audio-rate range.
Unknown common RF phase cancels, including independent FHSS retune phase. It
withholds a branch after an ambiguous, too-weak or excessively separated
observation. Its phase-error radii and failure-budget calculation are
conditional on its local statistical model; they are not calibrated coverage.
The default zero differential phase allowance assumes the two pilots share the
same channel transfer. Startup consistency does not establish that assumption
after later hops; an independently justified differential allowance is required
when available.

`HelperStreamConditioner` mixes the captured input by the negative measured
common displacement, then uses paired production `audio::Resampler` instances
with input step `1/a`, where the observed pilot model is
`f_observed = a*f_nominal + b`. The NCO and input/output time maps remain
continuous through updates, and the paired resamplers check that their source
coordinates agree. A phase correction is authorized only by a captured,
same-hop pilot fit; the resampler's entire interpolation support must lie in
that authority. A new phase-clock interval must contain the conditioner's
current time map before that phase is applied. Old samples never gain phase
authority from a later anchor. The test also compares the actual resampler
source map with the Helper clock and withholds timing qualification when their
disagreement exceeds its explicit 10 ns tolerance. The experiment can buffer one 400 ms dwell,
so a fresh phase can be measured before samples from that same dwell are
released. It does not extrapolate a random phase to a future hop.

The nominal transmission has 90 dwells, or 36 seconds, of observation followed
by five 400 ms detection dwells; the diagnostic probe occupies seconds 38–40.
The `frozen` comparison uses the frequency/audio model available after 36
seconds while taking fresh, dated same-hop Helper phase and phase-clock evidence
where required. `continued` permits later Helper frequency/audio updates. Thus
the phase on a later hop is not claimed to have been available at 36 seconds.
The exact transmitter-clock convention and known symbol dates are used for
metrology, but no timing, phase, frequency or gain is estimated from the tone
or APSK probe and fed back into the correction. A simulator-truth oracle is a
separate evaluation comparison only.

## Sampled diagnostic probes

[`test_helper_live_probe.cpp`](../tests/test_helper_live_probe.cpp) drives
[`helper_live_probe_fixture.hpp`](../tests/helper_live_probe_fixture.hpp) in
bounded irregular chunks. The fixture supplies cryptographically keyed random
HF and microwave hops, separate transmit and receive audio clocks, modeled RF
offset and phase noise, independent random hop phase, and complex thermal
noise. Helper and probe are present in the same sampled mixture. The probe's
power is separately specified; it is a metrology signal rather than an ordinary
Robust transmission. The simulation derives the Helper high-pilot level from
the actual waveform's total active-mean Helper power and reports path loss,
input temperature and noise figure. It provides no evidence that the ordinary
Robust power allocation could carry the high-rate diagnostic signal.

The two probes are a straight 1600 Hz tone and 64-APSK at 300 symbols/s with
root-raised-cosine rolloff 0.2: nominal shaped bandwidth is 360 Hz. The probe
center keeps the nominal 360 Hz signal separated from the 950 Hz auxiliary.
The constellation evaluator uses a fixed matched filter at the known source
symbol dates and the known symbol amplitude. There is no independent probe
timing, phase, frequency or gain acquisition, and no per-symbol or per-dwell
adaptive alignment. With error vector `e` and fixed expected symbol `s`, RMS
EVM is `sqrt(sum(|e|^2)/sum(|s|^2))`; constellation signal-to-error ratio is
`-20 log10(EVM)`. This includes synchronization and distortion error and is not
automatically a thermal SNR measurement. Reported 360 Hz nominal probe SNR is
derived separately from the link budget.

The tone evaluator measures residual phase and frequency in fixed windows.
Its noisy phase-slope fit has a conditional fit standard error; a noise-free,
identically impaired probe twin receives the *same* Helper-derived correction
and exposes sample-conditioning errors without giving the receiver a clean
reference. If the noisy probe is too weak for its fixed-window phase fit, that
metrology is reported unavailable. A measured noisy-tone frequency is an
estimate, not exact ground truth.

## Sampled results

The results below are individual seeded examples, not measured acquisition
probabilities or calibrated interval coverage. They use 290 K input noise
reference, 10 dB noise figure, and actual Helper waveform generation. In the
strong cases Helper power and the independently powered probe are each 4 kW,
with 180 dB simulated path loss. Their high-pilot C/N0 is about 48.07 dB-Hz;
the probe's modeled C/N0 is about 50.00 dB-Hz, or 24.43 dB SNR in 360 Hz.
The 4 kW probe is a deliberately bright metrology signal and is not a Robust
power or decoding claim.

The following frequency and phase figures use the noiseless probe twin after
the identical Helper-derived correction. Frequency is RMS across the five
supported 240 ms windows, and phase is RMS against the fixed source convention.
The independent noisy tone fit is shown separately; its standard error is the
RMS of its individual fixed-window, residual-based slope-fit errors.

| Simulated case | Twin frequency RMS before → frozen Helper | Twin phase RMS before → frozen Helper | Noisy frozen frequency RMS; fit standard error | Phase support |
| --- | ---: | ---: | ---: | --- |
| 30 MHz, randomized FHSS | 24.365 → 0.00227 Hz | 129.89° → 0.636° | 0.00979 Hz; 0.01510 Hz | 5/5 probe dwells |
| 5.8 GHz, randomized FHSS | 0.5431 → 0.00192 Hz | 129.03° → 0.291° | 0.01129 Hz; 0.01546 Hz | 5/5 probe dwells |
| 30 MHz, no FHSS | 24.080 → 0.00321 Hz | 100.04° → 1.119° | 0.01303 Hz; 0.01414 Hz | 5/5 probe dwells |

The initial high-pilot search half-widths in the randomized FHSS examples were
±94.395 Hz at HF and ±2.47404 Hz at 5.8 GHz. At the 36-second snapshot, the
frozen model's conditional three-sigma half-width for a nominal 1600 Hz probe
at the center shift was about ±0.01334 Hz and ±0.01313 Hz respectively. Those
are model-derived uncertainty estimates, not validated search guarantees or
the measurements of later hop residuals. The independently evaluated clean
twin residuals over the later probe windows are the 0.00227 Hz and 0.00192 Hz
RMS values in the table; the fresh same-hop phase and phase-clock observations
used in those windows were not available at the 36-second snapshot.

For the same modeled link settings, fixed-date 64-APSK evaluation produced:

| Simulated case | Noisy EVM before → frozen Helper | Constellation signal/error ratio before → after | Oracle EVM | Symbols |
| --- | ---: | ---: | ---: | ---: |
| 30 MHz, randomized FHSS | 1.4050 → 0.05772 | −2.95 → 24.77 dB | 0.05431 | 360 |
| 5.8 GHz, randomized FHSS | 1.6056 → 0.05982 | −4.11 → 24.46 dB | 0.05903 | 360 |
| 30 MHz, no FHSS | 1.3668 → 0.06029 | −2.71 → 24.39 dB | 0.05402 | 360 |

The 40 W Helper / 4 kW probe, 180 dB HF example had a 28.07 dB-Hz high pilot.
Its startup timing branch was unavailable, so it supplied frequency-only output
and no qualified phase or APSK measurement. In this one sample the twin's
frequency RMS changed from 24.412 Hz to 0.00159 Hz; this does not establish
timing or phase recovery. The 15 kW Helper / 4 kW probe, 220 dB microwave
example had 13.81 dB-Hz high-pilot C/N0 and a −15.57 dB nominal probe SNR in
360 Hz. Startup phase and the noisy probe's fixed-window metrology were
unavailable; the twin frequency RMS changed from 0.544 Hz to 0.133 Hz. These
samples do not establish weak-link sensitivity or an operational search bound.

The test includes the originally ambiguous long-extrapolation example: at
4 kW / 180 dB HF, its direct startup-plus-audio uncertainty exceeded the
±0.270 ms timing-branch half-width. Successive **Helper** pilot phase pairs
resolved the branch under the declared common-transfer and audio-range model;
the probe was not used to choose it. An opposite-direction clock control used
−37 ppm transmit and +13 ppm receive audio clocks. The Helper estimate was
−49.482 ppm; its five probe dwells had phase support, and twin frequency RMS
changed from 23.884 to 0.000467 Hz with a final phase RMS of 0.346°. Another
control checks a changing rate:
a deliberately severe 500 ppm rate step caused up to 123.94 µs disagreement
between the commanded Helper clock and the actual resampler time map, and no
affected samples were qualified. A small 1 ppm step had a maximum 0.0015 µs
disagreement and retained 23,900 qualified samples. These are bounded controls,
not a guarantee for arbitrary oscillator changes.

On the local AMD Ryzen 5 PRO 5650U and GCC 14.2 Release build, receiver-only
CPU for the seeded examples was approximately 0.472–0.504 seconds. This includes
the 40-second Helper observation and polls, startup and phase-clock processing,
and about 2.4 seconds of streaming conditioning. It excludes simulation sample
generation, extra raw/oracle/continued comparison streams, and evaluation.
Generation took approximately 0.311–0.351 CPU seconds, while separate tone
and constellation metrology took approximately 0.008–0.055 seconds in these
runs. The user's 0.5-second preference was exceeded in some cases; these are
host- and workload-specific measurements, not hardware guarantees.

## 40 W HF random-hop sweep

The following standalone sweep uses a 40 W Helper and a separately specified
40 W tone probe, both subject to the stated path loss. The Helper power is the
total active-mean composite waveform power; it is not the power of each pilot.
During the probe interval these are independently specified powers totaling
80 W nominal average, so the example is not a demonstration of an overall
40 W transmitter limit. A system limited to 40 W total would need a separate
power-allocation study.
The RF schedule is keyed-random HF50 with 50 kHz channel spacing. It uses a
30 MHz center, +0.8 ppm relative RF shift error, +37 ppm transmit and −13 ppm
receive audio-clock error, independent common hop phases and 0.05°/√s modeled
phase diffusion. The assumed path has a common complex transfer across the
pilots and probe; there is no modeled frequency-selective multipath. The input
temperature is 290 K and noise figure 10 dB. These are simulations, not a
transceiver, antenna, propagation or regulatory qualification.

Eight seeds were run at each loss. For each seed, the receiver observes 36
seconds before freezing the frequency/audio fit, then measures fresh phases on
later hops; the table counts five held-out probe dwells per seed. A qualified
absolute phase anchor is a same-hop pilot fit accepted by the startup and
phase-clock path. The phase-corrected window count also requires the actual
conditioner and its interpolation support to qualify, so an anchor alone is not
a claim of corrected output. At each loss all 40 held-out nominal-boundary hop
changes had valid pilot fits, independent of absolute phase qualification.

| Path loss | High pilot C/N0 | Startup qualified | Absolute anchors / 40 | Phase-corrected windows / 40 | Hop RMS median; worst single hop | Largest conditional 3σ frequency half-width |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 180 dB | 28.07 dB-Hz | 0/8 | 0 | 0 | 6.99°; 19.43° | ±0.1216 Hz |
| 175 dB | 33.07 dB-Hz | 0/8 | 0 | 0 | 4.01°; 10.90° | ±0.0684 Hz |
| 170 dB | 38.07 dB-Hz | 3/8 | 15 | 15 | 2.35°; 5.96° | ±0.0387 Hz |
| 169 dB | 39.07 dB-Hz | 5/8 | 25 | 25 | 2.12°; 5.26° | ±0.0346 Hz |
| 168 dB | 40.07 dB-Hz | 8/8 | 40 | 40 | 1.91°; 4.64° | ±0.0309 Hz |
| 167 dB | 41.07 dB-Hz | 8/8 | 40 | 40 | 1.73°; 4.15° | ±0.0277 Hz |
| 165 dB | 43.07 dB-Hz | 8/8 | 40 | 40 | 1.44°; 3.41° | ±0.0223 Hz |
| 160 dB | 48.07 dB-Hz | 8/8 | 40 | 40 | 0.95°; 2.17° | ±0.0134 Hz |

For hop errors, the median is over eight per-seed RMS values, each based on
five held-out changes; the worst is the largest absolute error among those 40
changes. The independent truth reference and the Helper measurement both
extrapolate local phases and frequencies to the nominal dwell boundary. This
is not a direct measurement of the phase jump at the actual delayed retune,
and includes local extrapolation and modeled phase noise. It measures relative
hop changes even when absolute startup timing is unavailable; the separate
relative-gauge conditioning test below exercises that possibility end to end.
The frozen
three-sigma widths are conditional model outputs at the center RF and nominal
1600 Hz probe, not measured coverage guarantees or a claim that phase is known
after 36 seconds. These eight-seed samples are not reliable probabilities or
precise sensitivity thresholds. The arrival offset and oscillator parameters
are fixed across these seeds; the random schedule, hop phases and noise vary.

The corresponding frozen-correction clean-twin frequency RMS values, each over
five later probe windows, had the following median and maximum across the eight
seeds. These are measured residuals after the same correction obtained from
the noisy Helper, and the maximum is over per-seed RMS values, not individual
window errors:

| Path loss | Median frequency RMS | Maximum frequency RMS |
| ---: | ---: | ---: |
| 180 dB | 0.01027 Hz | 0.02782 Hz |
| 175 dB | 0.00584 Hz | 0.01552 Hz |
| 170 dB | 0.00335 Hz | 0.00868 Hz |
| 169 dB | 0.00299 Hz | 0.00772 Hz |
| 168 dB | 0.00268 Hz | 0.00687 Hz |
| 167 dB | 0.00239 Hz | 0.00612 Hz |
| 165 dB | 0.00191 Hz | 0.00484 Hz |
| 160 dB | 0.00108 Hz | 0.00270 Hz |

The test also has an optional relative-gauge conditioner. It chooses the
receiver-defined source coordinate `ŝ(36 s) = 36 s`, integrates the frozen
Helper audio-clock model, and uses fresh, valid high-pilot phase in each hop.
This does not require a qualified absolute startup branch. One fixed unknown
time and phase remain for ordinary acquisition; no tone or APSK samples are
used to determine them. For evaluation only, the simulator's clock truth at the
declared origin supplies one fixed expected gauge, without fitting probe
samples or feeding that value to the receiver. The following two-seed tone
examples had five of five supported phase anchors and five of five supported
windows in every case:

| Path loss | Clean-twin phase RMS, seed 901 | Clean-twin phase RMS, seed 905 |
| ---: | ---: | ---: |
| 180 dB | 16.46° | 14.05° |
| 175 dB | 9.21° | 7.84° |
| 170 dB | 5.13° | 4.36° |
| 165 dB | 2.84° | 2.41° |
| 160 dB | 1.55° | 1.31° |

The phase RMS is computed over the five supported 240 ms windows against that
fixed gauge. A small additional timing test varied arrival between 2.94 and
3.48 ms and reversed the signs of the two audio-clock errors, using seeds 901
and 905. All 12 cases at 180, 170 and 160 dB had five supported anchors and
windows; phase RMS ranges were 7.03–15.32°, 2.31–5.37° and 0.592–2.111°
respectively. These controls do not establish coverage for arbitrary clocks,
paths or phase behavior. The relative test uses the same common-transfer
assumption; the separate [tracking study](helper-tracking.md) includes APSK
evaluation. Neither test demonstrates ordinary Robust acquisition.

The separately generated 64-APSK probe was also run at 40 W with seeds 901 and
905. Each supported EVM uses 360 symbols and the same fixed-date evaluation
described above:

| Path loss | Supported cases | EVM before → frozen Helper, seed 901 | EVM before → frozen Helper, seed 905 |
| ---: | ---: | ---: | ---: |
| 180 dB | 0/2 | unavailable | unavailable |
| 170 dB | 1/2 | unavailable | 1.3832 → 0.1886 |
| 165 dB | 2/2 | 1.4037 → 0.1010 | 1.3794 → 0.1068 |
| 160 dB | 2/2 | 1.4050 → 0.0577 | 1.3792 → 0.0610 |

For the supported 170 dB case, the oracle EVM was 0.1807. The diagnostic is a
high-rate, 360 Hz shaped constellation and its EVM is not a Robust decoding
result. An unavailable result means the Helper-only absolute timing/phase path
declined this run; it is not evidence that relative phase is physically
unmeasurable.

On the same host and build as above, receiver-only process CPU in these runs
was 0.461–0.590 seconds per 40-second Helper stream, including approximately
2.4 seconds of probe conditioning and the hop-phase calculations. Generation,
comparison streams and metrology are excluded; these figures do not represent
continuous full-rate conditioning for the entire 40 seconds. Some cases exceed
the preferred 0.5-second budget. In the two-seed relative-gauge examples,
receiver CPU was 0.487–0.623 seconds. This includes the optional absolute
diagnostics as well as relative conditioning, so it is not a measurement of a
stripped-down relative-only implementation.

An exploratory `--40w-short` run generates only the first 0.4-second dwell and
estimates startup using local clock and frequency estimates from that single
noisy aperture. Its uncertainty radius conditions on those estimates; it is a
cheap feasibility check, not the 36-second result or validated interval
coverage. A 4 kW independently powered metrology probe can overwhelm the
broadband residual used for estimated pilot C/N0, suppress phase authority and
alter observed hop errors; its results should not be treated as equivalent to
the equal-power mixture. The equal-power results above avoid attributing that
specific bright-probe effect to the 40 W Helper's thermal link budget.

## Physical cable check

The connected analog loopback was exercised separately with an actual Helper
Generator waveform and a 1600 Hz tone. It was a 40-second **non-FHSS** mono
capture: 38 seconds of Helper lead followed by two seconds containing the
probe, with an intentionally generated +4 Hz common offset and +100 ppm audio
clock error. This is a physical audio-path check, not a simulated RF path loss,
FHSS retune, EME channel or ordinary Robust reception. A two-second initial
playback/capture check found both pilots and the probe without clipping.
Direct `hw:1,0` capture first failed because the analog device rejected mono;
`plughw:1,0` with ALSA channel conversion then captured the 40-second stimulus
successfully. The recorded peak was about 0.1801 and no samples were near
full scale. No mixer settings were changed.

The offline analyzer uses production 48 kHz-to-12 kHz resampling and a
test-local 129-tap Hilbert conversion of the real capture before the standalone
I/Q Helper components. It derives onset, clock and the phase at each probe dwell
from Helper only. The affine startup-to-probe timing bound alone was 0.3533 ms,
larger than the 0.2703 ms branch half-width; the successive Helper-pair phase
clock resolved a unique conditional branch, with a first-probe radius of about
9.43 µs. Its current time map was required to remain inside the contemporaneous
phase-clock interval, and phase-unsupported resampler samples were excluded.
The zero unknown differential pilot-transfer allowance remains a model assumption,
not a measurement proving the cable or every radio hop has zero differential
phase.

In this capture, the measured raw tone offset was +4.16018 Hz, with 101.48°
circular phase RMS. After Helper-only conditioning it was +0.000383 Hz, with a
conditional slope-fit standard error of 0.000999 Hz and 1.098° phase RMS across
30 fixed evaluation blocks. All 23,955 checked output samples had timing support;
phase evaluation also required interpolation-halo support. The point estimate
is not exact physical frequency knowledge. A synthetic silent-preroll control
gave +4.16095 → +0.000264 Hz and 101.46° → 0.856°. The physical offline chain
used about 0.60 seconds user CPU plus 0.01 seconds system CPU for the 40-second
capture, including mono conversion, all Helper measurement, conditioning and
independent tone metrology. It is not a receiver-only tracking figure and does
not establish the 0.5-second tracking preference.

A separate synthetic cable-source control tested the opposite clock-error
direction at −100 ppm. Helper-only measurements estimated −100.109 ppm and
+4.00024 Hz, and all 23,950 checked output samples had timing support. The tone
changed from +3.84179 Hz / 102.63° RMS to −0.0000496 Hz / 0.759° RMS, with
a conditional slope-fit standard error of 0.000694 Hz. This is a simulated
control, not another hardware capture.

The compact local evidence is
[`physical-summary.txt`](../local-evidence/helper-live-cable/physical-summary.txt).
An exact compressed copy of this run's 48 kHz mono float capture is retained at
[`capture-40s.f32.gz`](../local-evidence/helper-live-cable/capture-40s.f32.gz).
It is ignored local evidence, not a version-controlled fixture; copying the
source checkout alone does not preserve it. Its compressed SHA-256 is
`0282f0b929443b61ac582d504e8832af3fcc5403e8200162dadd55b0f17b5b0f`; the
decompressed capture SHA-256 is
`cd15730de6c0b67b2ce69f2e08e9d6c33fdef7724f5ec2a7ed074f819a56a405`.

## Reproduction

With a compatible `build/dev`, build the specific test before invoking it:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_live_probe --parallel 2
build/dev/test_helper_live_probe --probe
build/dev/test_helper_live_probe --suite
build/dev/test_helper_live_probe --40w-short 170 905
build/dev/test_helper_live_probe --40w 170 905 tone 40
build/dev/test_helper_live_probe --40w 170 905 apsk 40
build/dev/test_helper_live_probe --40w-relative 180 905 tone 40
build/dev/test_helper_live_probe --40w-relative 180 905 tone 40 0.00348 -37 13
```

`--probe` is a short early-gate/noise check. `--suite` includes separate
noiseless frequency, audio-clock and random-hop-phase controls; strong HF and
microwave FHSS and HF no-FHSS tone and APSK cases; weaker Helper examples; a
clock-drift example; and startup, missing-phase, phase-authority and model
consistency controls. The suite is part of the `contract` selection; a
focused pass does not replace full preservation or platform validation. Its
seeded I/Q noise uses the standard-library normal distribution; identical seeds
do not guarantee identical sample streams across library implementations.
Judge results against the test tolerances rather than exact log text or CPU time.
For the 40 W table, repeat the full tone command for losses 180, 175, 170, 169,
168, 167, 165 and 160 dB and seeds `901` through `908`; the APSK table uses seeds `901` and `905`
at `180 170 165 160` dB. The full command accepts an independently specified
probe power in watts; omitting it selects a 4 kW metrology probe, not 40 W.

To reproduce the *real-mono analysis path* without an audio device, build the
standalone cable tool and make simulated captures from the same Helper-plus-tone
stimulus. Use a fresh directory because generated files are exclusively created:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target helper_live_cable_tool --parallel 2
ctest --test-dir build/dev --output-on-failure -R '^helper_live_cable_sim$'
helper_sim_dir=$(mktemp -d /tmp/helper-cable-sim.XXXXXX)
build/dev/helper_live_cable_tool --generate-long "$helper_sim_dir/positive.f32"
build/dev/helper_live_cable_tool --simulate-long "$helper_sim_dir/positive.f32" "$helper_sim_dir/positive-capture.f32" \
  --preset cable-20261030 --seed 901
build/dev/helper_live_cable_tool --analyze-long "$helper_sim_dir/positive-capture.f32"
build/dev/helper_live_cable_tool --generate-long-negative "$helper_sim_dir/negative.f32"
build/dev/helper_live_cable_tool --simulate-long "$helper_sim_dir/negative.f32" "$helper_sim_dir/negative-capture.f32" \
  --preset cable-20261030 --seed 901
build/dev/helper_live_cable_tool --analyze-long "$helper_sim_dir/negative-capture.f32"
```

The CTest case is registered when Python 3 is available during CMake
configuration; it exercises the simulated capture and analyzer without an
audio device. The manual commands remain available without Python.
Both captures go through the same production resampler, test-local Hilbert
adapter, Helper timing and correction, and fixed tone metrology as the cable
recording. The positive and negative stimuli have respectively +100 and
−100 ppm source-clock error, with +4 Hz common offset. The simulated capture
adds 0.75 seconds of preroll and 1 second of postroll. The measured preset uses
a fixed gain of 0.84150, a first-order high-pass cutoff of 11.34796 Hz, an
effective additional delay of 35.0795 ms, zero assumed additional ADC clock
error, DC offset −0.0000828714, pre-quantization white noise RMS 0.0000454631,
and independent first-order correlated noise with RMS 0.000084666 and a
71.4 ms time constant.
Its gain, delay and filter approximate the recorded path using the pre-probe
Helper pilots; the noise parameters use the 0.10–0.65 s preroll only. The
measured post-ADC white residual was 0.000046309 RMS. The lower injected
white-noise level subtracts ideal signed-16-bit quantization variance under a
uniform, uncorrelated-error assumption; the simulator then applies ideal
quantization. This is a phenomenological approximation, not a measured
decomposition of analog and quantization noise.
The `flat` preset uses gain 0.84, delay 20 ms, white noise RMS 0.000085 and no
high-pass or correlated noise. Both use cubic fractional-delay interpolation
and ideal signed 16-bit quantization. An optional ADC clock error changes the
physical sample rate used for filter and correlated-noise time constants. The
noise uses a fixed integer generator
and twelve-uniform approximation. Its
seed makes repeat runs deterministic for a compatible build; floating-point
rounding and library implementations can differ across platforms. CPU timing
also depends on the host. The output is little-endian mono float32.

After `--preset`, the optional `--gain`, `--delay-ms`, `--highpass-hz`,
`--noise-rms`, `--dc`, `--colored-noise-rms`, `--colored-noise-tau-ms`,
`--adc-ppm`, `--quantization-bits` and `--seed` arguments override its
settings. Place overrides after `--preset`: selecting a preset resets earlier
options. `--quantization-bits 0` disables the quantizer.

The measured preset is a compact model of one recording, not an exact replay
or a complete physical channel. No diagnostic-probe samples were used to fit
the model: the 1600 Hz probe was held out. In this one comparison, its observed
transfer differed from the pre-probe model prediction by about 0.00142 dB in
gain and 0.00404° in phase. This is not a validation of transfer at other
frequencies or times. The simple correlated-noise model summarizes one short
preroll, not every feature or variation of the measured noise; it does not
model RF propagation, FHSS or EME.
The Helper receiver sees the simulated samples only; it is not given the
configured gain, delay, clock or frequency error to select the correction.
A successful result demonstrates the stated simulated conditions and does not
establish that another physical cable will behave identically.

With this measured preset and seed 901, the positive-clock synthetic capture
reported 90/90 Helper apertures, +100.139 ppm estimated audio error, and raw
tone +4.160188 Hz / 101.475° RMS. Helper-only conditioning produced a residual
+0.000398 Hz / 1.0999° RMS, with a conditional fit standard error of
0.001001 Hz. The physical recording reported +100.137 ppm,
+4.160176 Hz / 101.476° before conditioning, and +0.000383 Hz / 1.098° after.
The negative-clock simulation reported −100.132 ppm and +3.841981 Hz /
102.563° before versus +0.0000218 Hz / 1.107° after conditioning. These are
numerical examples, not expected sample-for-sample equality or a calibrated
hardware accuracy bound.

To replay the **same recorded cable capture** without opening a device, first
build the standalone analyzer and restore the retained local artifact into a
fresh temporary directory:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target helper_live_cable_tool --parallel 2
helper_replay_dir=$(mktemp -d /tmp/helper-cable-replay.XXXXXX)
gzip -t local-evidence/helper-live-cable/capture-40s.f32.gz
gzip -dc local-evidence/helper-live-cable/capture-40s.f32.gz > "$helper_replay_dir/capture.f32"
sha256sum "$helper_replay_dir/capture.f32"
build/dev/helper_live_cable_tool --analyze-long "$helper_replay_dir/capture.f32"
```

The `sha256sum` result should match the decompressed hash above. This is a
reanalysis of the recorded samples, not a new Live measurement; a new cable
capture can differ in hardware timing, noise, gain and phase. Numerical
rounding and CPU times can also differ with the compiler and host.

The optional [calibration tool](../tools/calibrate_helper_live_cable.py)
reproduces the measured-preset derivation from the transmitted stimulus and
recorded capture. It requires a Python environment with NumPy but not SciPy.
After the replay commands above, use a fresh stimulus path in that directory:

```sh
build/dev/helper_live_cable_tool --generate-long "$helper_replay_dir/stimulus.f32"
python3 tools/calibrate_helper_live_cable.py \
  "$helper_replay_dir/stimulus.f32" "$helper_replay_dir/capture.f32" \
  > "$helper_replay_dir/calibration.json"
```

The default calibration uses the known injected +100 ppm and +4 Hz stimulus,
two Helper tones between 2 and 37.5 seconds, a 0.70–0.90 second startup search,
and the 0.10–0.65 second silent preroll. The 1600 Hz probe is analyzed only
after fitting. The JSON includes input hashes, windows, model parameters,
held-out probe errors and limitations. The retained run is also summarized in
[`calibration-helper-only.json`](../local-evidence/helper-live-cable/calibration-helper-only.json),
which is ignored local evidence. Two pilot tones do not uniquely identify the
physical circuit. If the ignored capture is absent, the preset simulation
still runs, but this particular recording cannot be replayed or recalibrated.

The optional calibration regression can also be run with a Python environment
that has NumPy:

```sh
python3 tests/test_helper_live_cable_calibration.py \
  build/dev/helper_live_cable_tool tools/calibrate_helper_live_cable.py
```

CMake registers `helper_live_cable_calibration` when Python is available, but
marks it skipped when that interpreter lacks NumPy. A skip is not a passing
calibration test; use a NumPy-enabled interpreter for the command above.

For the physical cable experiment, these commands use the repository's production
audio capture harness and the standalone cable stimulus/analyzer. Build the
current compatible `build/dev` library first. The host device name is specific
to this run; a different connected machine can require another device. The
playback/capture step opens the named audio device and lasts approximately 42
seconds including its capture lead and tail.

```sh
c++ -std=c++20 -O2 -Iinclude tools/helper_live_cable.cpp \
  src/helper_startup_timing.cpp src/helper_stream_conditioner.cpp \
  src/helper_phase_clock.cpp build/dev/libdatapump.a \
  build/dev/third_party/xz/liblzma.a -lcrypto -ldl -pthread \
  -o /tmp/helper_live_cable_phase
c++ -std=c++20 -O2 -Iinclude tools/cable_audio_capture.cpp \
  src/audio.cpp src/audio_policy.cpp build/dev/libdatapump.a \
  build/dev/third_party/xz/liblzma.a -lcrypto -ldl -pthread \
  -o /tmp/cable_audio_capture
helper_cable_dir=$(mktemp -d /tmp/helper-live-cable.XXXXXX)
/tmp/helper_live_cable_phase --generate-long "$helper_cable_dir/stimulus.f32"
/tmp/cable_audio_capture --input "$helper_cable_dir/stimulus.f32" \
  --output "$helper_cable_dir/capture.f32" --rate 48000 --device plughw:1,0
time -p /tmp/helper_live_cable_phase --analyze-long "$helper_cable_dir/capture.f32"
```

On this run the sandbox could not access `/dev/snd`; the device inspection and
playback/capture used host-level permission. A successfully opened device alone
does not establish that its analog path matches this cable setup.
