# Isolated three-carrier Live/Duplex cable suite

This suite measures the combined DAC, cable, input stage and ADC path, then
checks whether a model identified from dedicated probes predicts held-out
three-carrier waveforms. It is separate from the ordinary Robust receiver and
from the [CPU/GPU performance regression suite](triad-performance.md). Start
with the [three-carrier development workflow](triad-development-workflow.md)
and retain the waveform and propagation contracts in the
[v3 guide](triad-v3.md) and [channel-model guide](channel-models.md).

The prepared routine has a less-than-50-minute engineering budget. This is a
budget for a hierarchy of direct measurements and qualified predictions, not a
claim that every requested rate, radio or multi-day observation can be tested
physically in 50 minutes. Build/dependency setup is separate. A run must retain
actual preparation, capture, analysis and total wall time; an incomplete run is
not complete coverage.

The [2026-11-04 cable qualification record](validation-data/triad-v3/2026-11-04-live-duplex/README.md)
contains **24 completed physical captures**, retained failures and focused
independent model validation. The original campaign completed 18 captures before
the ordinary GUI acquired the device during analysis. The operator stopped the
GUI; the suite did not suspend another application automatically. Continuation
verified and reused those captures before completing the remaining six. Its
active planning/preparation/capture/analysis total was 1,847.20 seconds
(30.79 minutes), with a separately reported 681.76-second human pause. This is
not evidence of one uninterrupted sub-50-minute session.

The later [physical noise and margin follow-up](validation-data/triad-v3/2026-11-04-live-margin-bounds/README.md)
finds an important boundary: the original baseline DDC admits audio-DC noise
outside the triplet, contaminating its whole-workspace noise estimate at low
absolute levels. A fixed narrow frontend brings independent physical/model
admissions into much closer agreement. Start with that record for marginal
cable reception; transmitted-noise-dominated agreement alone does not qualify
hardware-floor behavior. Its narrow mean-loss proxy intervals are conditional
engineering estimates, not future-pass or full-receiver BER guarantees.

For short live **admission** checks with clock following enabled, use the
[native-ADC profile path](triad-physical-admission.md). It separates the actual
ADC noise envelope from covariance introduced by ASRC, keeps the clock pipeline
active, and requires independent noise validation. Its conditional admission
claim is narrower than physical certification of a daily false-admission rate.

## What each result establishes

| Evidence class | What was exercised | What it does not establish |
|---|---|---|
| Direct complete-pattern capture | Native complete keyed waveform through the actual audio cable, using declared hardware and mixer settings | Blind operational acquisition, a low false-admission rate, or independent radio behavior |
| Actual-rate component/window capture | A genuine source interval or physical probe at its original rate, without speeding up its time coordinate | Complete-bit reception or the unobserved preceding/following propagation state |
| Hardware replay of simulated propagation | The shared sampled channel's complete composite output passed through the audio path | A measured RF Doppler, multipath, fading or retuning environment |
| Held-out model prediction | A profile frozen from a separate training probe predicts an independent capture within its measured domain | An unconditional bound outside the tested rate, level, bandwidth or observation interval |
| Unsupported or unidentified | A native geometry, real-audio image, bandwidth, measurement-time or identifiability limit was found | A receiver sensitivity failure; these cases must not enter a successful-decode denominator |

Record direct outcomes, model errors and unsupported cases separately. A native
configuration census supplies eligibility, not reception evidence. Passing a
software unit test checks the harness, not the cable. Use the source-bound
[evidence matrix](validation-data/triad-v3/2026-11-04-live-duplex/evidence-matrix.json)
for actual captures and held-out errors; the matrix distinguishes the original
campaign from later retained-capture diagnostics and new physical validation.

## Why some low-rate and high-spreading cases need different tests

For GUI Rate \(R\) and Added Symbols factor \(A\), the native waveform has

\[
R_{\rm fine}=RA/6,\qquad
\Delta f=1.25R_{\rm fine},\qquad B_{\rm nominal}=3.75R_{\rm fine}=0.625RA.
\]

The planner retains at least 64 **inner** coefficients; Added Symbols does not
remove this requirement. Useful pattern duration is at least \(384/R\) seconds,
before pulse padding, dwell protection and tile rounding:

| GUI Rate | Lower bound before overhead | Appropriate routine evidence |
|---:|---:|---|
| 0.001 Hz | 384,000 s, about 4.44 days | Actual-rate windows and low-frequency component measurements |
| 0.01 Hz | 38,400 s, about 10.67 hours | Actual-rate windows and low-frequency component measurements |
| 0.1 Hz | 3,840 s, 64 minutes | Actual-rate windows; a complete bit exceeds the whole routine budget |
| 1 Hz | 384 s, 6.4 minutes | Selected complete-pattern capture is feasible |
| 3.6 Hz | 106.67 s | Selected complete-pattern capture is feasible |
| 10 Hz | 38.4 s | Selected complete-pattern capture is feasible |

Use `triad_long --mode export-source-window` for the first three rows. It renders
an absolute-coordinate interval of the native `PatternSource` and exports both
public bit alternatives. Its distinct `triad-source-window-v1` manifest records
the full geometry, selected coordinates and partial status. This operation is
source-only: it rejects propagation, oscillator, frontend and erasure selections
whose preceding state has not been simulated. Never reset a `StreamingChannel`
at a late source coordinate and represent that output as a continuous historical
path.

The mono audio bridge requires separated conjugate images. Its nominal checks
require both \(f_c-B/2\) above the declared DC transition and
\(f_c+B/2\) plus transition below audio Nyquist. The default transition allowance
is 5% of nominal bandwidth. Finite shaped pulses have spectral tails, so this
is an eligibility test followed by measurement, not a brick-wall spectral claim.
For example, at Rate 0.01/Added ×1, a 0.005-Hz center has separated nominal
images but may be far below the hardware high-pass cutoff. At Added ×100 or
×1000, that same center has overlapping images and cannot carry the complete
complex triplet through one real mono cable. An independent I/Q hardware path
would be a different interface requiring qualification.

At 48-kHz audio, Rate 24-kHz/Added ×1 has 15-kHz nominal width and requires a
higher center than 1.725 kHz. Increasing Added to ×100/×1000 at high GUI rates
can exceed the audio Nyquist limit entirely. Report these exclusions instead of
lowering the rate or spreading silently.

## Measurement and simulation chain

1. **Preflight and geometry census.** Record source/binary identities, explicit
   physical capture/playback endpoints, negotiated formats, mixer settings,
   audio rate, stimulus peak and resource/deadline limits. Reject a desktop
   monitor source or a default-device substitution. Query each distinct native
   rate/Added/FHSS geometry once, then factor carrier placement checks over it.
2. **Dedicated training.** Periodic multisine probes at several fixed levels,
   omitted Fourier bins, quiet intervals and independent public sync markers
   identify the combined path. The default probe is 57.6 seconds. First/last
   periods are excluded from the transfer fit; repeated periods are distinct
   observations, not a count of independent Fourier bins.
3. **Concentrated low-frequency training and model freeze.** The current full
   plan adds a 140.6-second low-frequency training probe after the 57.6-second
   broad probe. Fit its explicitly selected fourth-order stable
   second-order-section (SOS) extension from training data only, then freeze the
   combined profile before the independent
   85.6-second low-frequency validation probe and all other holdouts. Retain
   training waveform hashes, the causal section and residual FIR, complex
   transfer response, coherence, level dependence, quiet-noise PSD and
   identification limits. Do not refit the profile to improve each held-out
   modem result. Diagnostic smoke plans and historical plans without the added
   probe remain explicitly broad-only models.
4. **Held-out verification.** Use a different probe seed/level and the selected
   native waveforms. Compare actual and predicted energy, fixed-model residuals,
   subspace representation and conditional receiver-ranking curves. Preserve
   failures and their denominators.
5. **Low-frequency and boundary probes.** Spend real time where the hardware
   cannot be identified by a short wideband probe. Report below-noise responses
   as bounds/unidentified, and distinguish incomplete cycles from complete
   observed response. Retain endpoint/tail and quiet-gap evidence.

The revised full plan has 26 physical stimuli totaling 1,737.836 seconds. Its
planned envelope is 2,545.836 seconds including capture overhead and a
600-second analysis/finalization reserve, below the 2,900-second limit. These
are plan figures, not measured completion times for the revised routine.

The default cable backend is the isolated
[`triad_live_alsa.py`](../tools/triad_live_alsa.py) adapter. It runs `arecord` and
`aplay` against explicit `hw:C,D` endpoints with two-channel `S16_LE`,
`--fatal-errors`, verbose negotiated-format output, and ALSA resampling, channel,
format and software-volume conversion disabled. The full captured stereo PCM
is retained; the diagnostic uses fixed channel 0 rather than selecting the
better channel after seeing results. The parser verifies the actual hardware
format, channel count and exact rate. This path has no provider timestamps:
public markers measure relative latency/rate, while subprocess/file boundaries
are not calibrated analog times.

The standalone native capture executable is
[`tools/triad_audio_capture.cpp`](../tools/triad_audio_capture.cpp). It uses the
existing native audio API with explicit exclusive `hw:C,D` endpoints, a finite
wall deadline, bounded buffers, timestamp observations and separate playback
and capture discontinuity reporting. An xrun invalidates continuous-waveform
evidence. Requested sample rate and sample container size do not prove the
negotiated device rate or effective ADC resolution. Optional provider-rate
output contains floating-point PCM before modem resampling, not literal ADC
codes.

The first direct-native attempt on the present Realtek ALC257 fixture stopped
before playback because the backend requested mono from a stereo-required raw
capture endpoint. The [production-backend correction and physical follow-up](validation-data/triad-v3/2026-11-04-live-duplex/native-stereo-followup.md)
now qualify native capture on this fixture at 48 kHz. ALSA capture prefers mono,
then accepts stereo on the same endpoint/rate and extracts fixed channel zero
before the mono monitor/resampler. Format metadata distinguishes hardware and
logical channels. Capture also sets its automatic-start threshold to one frame;
the former full-buffer threshold prevented the bounded native reads from starting.
Playback buffering and channel routing retain their existing behavior.

The follow-up exercised an 11.48-second complete waveform through the production
API, with no reported discontinuities or clipping and a −47.58-dB payload-model
residual. This single-case native qualification does not relabel the original
24 ALSA-utils captures as native-backend observations. The ALSA-utils backend
still retains both ADC channels; optional native provider output retains only
the selected mono channel at provider rate.

[`triad_live_model.py`](../tools/triad_live_model.py) aligns captures using public
measurement markers independent of the bit. The affine time map is measured
over that capture only. Two markers identify an offset/rate ratio but provide no
residual degrees of freedom for a rate-confidence estimate. A codec sharing DAC
and ADC clocks can conceal common clock error. The model deliberately retains
a fixed 256/48000-second causal alignment guard; subsequent consumers must not
remove it a second time.

The production native playback conversion is included explicitly: truncate
`clamp(x) * 32767` to signed 16-bit PCM, then express those DAC units on the
`/32768` scale. Fit against that quantized **complete composite** waveform when
using `input_pcm="s16-native"`. `apply_audio_profile` remains a linear operator
for public candidate columns; quantizing each fitted channel-basis column
independently would violate superposition. The full-waveform simulator may apply
the playback quantizer once before the fixed path. Any resulting candidate-bank
approximation remains visible in held-out residuals.

The measured quiet PSD describes post-ADC samples and already includes their
quantization effects. The default simulator adds a finite-span, stationary
colored-Gaussian surrogate with that PSD and does **not** quantize the output a
second time. Gaussianity, stationarity below the measured spectral resolution
and long-term noise are assumptions, not results of a PSD estimate. Optional
output-lattice rounding is a separate approximation. No calibrated voltage,
absolute receiver noise figure or effective number of ADC bits is inferred.

Apply the hardware operator to the whole real composite waveform. Its streaming
state continues through guards and burst gaps, with explicit tail drainage.
For simulated multipath and additional selective modulation, first use the
shared sampled channel, including its overlapping dispersive passbands and
complex sidebands; do not substitute per-carrier gains or gate the received
output during transmitter silence. Low-rate dispersive models can have long
history/tails, which must be included or declared outside the run budget.

## Receiver margins and their scope

[`triad_live_waveforms.py`](../tools/triad_live_waveforms.py) converts the native
zero-centered complex triplet to real audio using
\(\sqrt{2}\,g\operatorname{Re}\{x(t)e^{j2\pi f_ct}\}\), with fixed declared gain
\(g\), then performs complex direct conversion and fixed filtering/resampling.
There is no payload-fitted amplitude normalization. The native `--nominal-if-hz`
field models oscillator error; it does not perform this physical upconversion.

The receiver comparison retains a **nominal-alignment fixed-bank diagnostic**
and supplements it with a bounded public timing search where supported. Public
measurement markers remove audio transport offset/rate; they do not remove a
separately simulated source-arrival offset. The bit label is used only to score
the result. SVD projections compare the declared public candidate bank,
including legitimate channel columns, and report observable ranks and
cross-bit subspace overlap. A partial window with fewer than enough changing
cells may make the two bit alternatives identical up to unknown complex gain;
such a bank is marked unidentifiable and receives no sensitivity curve.

The Fake-FHSS capture exposed an omitted ±90-ms public arrival prior in the
original nominal bank: both real and predicted waveforms selected the wrong
nominal bit despite a −42.53-dB waveform-model residual. The retained-capture
follow-up scored all 693 public timing alternatives, including four fractional
sample phases and exact endpoints, without circularly wrapping gaps or tails.
Both selected the correct bit at −47.395833 ms and retained 0.977963/0.978019 of
the observation energy. Each search took about 0.393 seconds; the complete
follow-up also required 35.42 seconds of reusable audio/model/DDC preparation.
The original failure remains recorded. This corrects a diagnostic-bank omission,
not a cable sensitivity loss or a calibrated acquisition claim. The bounded
helper rejects unsupported multi-column or oversized searches, does not search
frequency/clock/epoch, and leaves the approximately 2.2% representation loss
unresolved.

For cheap margin curves, the same proper complex white-noise draw is added to
the real and predicted retained correlations. Its joint covariance is the
candidate Gram matrix, preserving dependence between candidates. A few explicit
sampled-noise least-squares calculations verify this linear compression. Both
sides use the same declared reference power and `C/N0`; hardware attenuation is
not divided away. These draws are noise added **after** the measured capture/DDC
path, not repeated physical weak-signal captures. The original hardware
residual remains fixed.

Assess waveform agreement on the **payload interval**. Public markers can
dominate the energy of a very weak or near-DC stimulus and make a whole-record
residual look misleadingly small. Wrapped-stimulus metrics remain available but
are labeled separately. Partial source windows also report an interior metric
after five-second boundary guards; that finite guard is not a settling proof,
and a sub-cycle window does not measure steady-state transfer.

Report pointwise Wilson intervals for these conditional noise draws, not a
confidence bound for independent hardware observations or the crossing of an
entire searched curve. These nominal-bank noise curves do not calibrate the
larger timing-search maximum. The diagnostics do not qualify colored-noise
whitening, operational admissions, misses, authentication, blind search or
continuous-operation false admissions. It must not spoof a native Gaussian
capture manifest to activate those receivers' admission gates.

## Actual receiver admissions on cable captures

[`triad_live_margins.py`](../tools/triad_live_margins.py) adds a separate
physical reception experiment. It reuses the maintained FFT acquisition and
Gaussian-window likelihood calculations through
[`triad_live_receiver.py`](../tools/triad_live_receiver.py), with both bit
patterns and every declared timing/frequency alternative. The physical adapter
does **not** assert a Gaussian simulation manifest. It discards the kernel's
analytic admission decisions and uses its likelihood as a working statistic.

Freeze the public templates, noise condition, scaling, audio profile, search,
source and arithmetic before calibration. The empirical gate ranks the maximum
bit-mixture likelihood against independent noise-only calibration records:
`p = (1 + count(calibration_score >= test_score)) / (n + 1)`.
Ties are conservative. The initial experimental gate uses `p <= 0.05` per
capture. Its marginal interpretation requires exchangeable null records; it is
not the operational `1e-4/day` guarantee or a conditional guarantee for the
particular frozen threshold. Fresh null validation must report its uncertainty,
including when no admissions occur. Null calibration cannot guarantee correct
bits or reject structured wrong-key signals.

Two complementary measurements distinguish hardware effects:

- Mix independently seeded noise with the keyed signal **before the DAC**,
  using a fixed noise level and declared signal gains. Feed the identical full
  quantized composite to the frozen audio model. This is transmitted noise
  through the cable filter, not independent receiver-input AWGN.
- Reduce signal level with no added noise, calibrating separately on physical
  silence. This tests DAC truncation as well as the combined analog/ADC noise
  floor. A nominal nonzero signal can become entirely zero in S16 PCM.

Public measurement chirps condition transport timing and relative clock. The
receiver still searches the declared residual timing/CFO bank; epoch and clock
hypotheses are fixed. No bit-dependent alignment or payload amplitude fitting
is permitted. Report this as **marker-conditioned offline reception of actual
cable PCM**, not blind acquisition or real-time backlog qualification. The
deterministic model and physical path remain continuous across playlist records;
the model's independently drawn finite-record residual remains a stationarity
assumption. Finite template tails carry an explicit discarded-energy bound.

Use `prepare --phase calibration` with at least 39 null records, record its
playlist with `triad_audio_capture`, then `analyze` it. Reuse those exact fixtures
with `prepare --phase validation --fixtures CALIBRATION/fixtures`, and pass
`--calibration CALIBRATION_ANALYSIS` to the independent validation analysis.
`--phase pilot` is exploratory and emits no calibrated decisions by itself.
`--phase floor` makes a separate silence-calibrated level experiment. All tools
require fresh output directories. The default prepared cases are deliberately
small; specify `--cases baseline,edge,spreading,stress` for the four distinct
current regimes. Read the retained experiment before repeating long hardware
playlists, and never count synthetic harness tests as physical trials.

The [2026-11-04 actual reception study](validation-data/triad-v3/2026-11-04-live-reception/README.md)
records the physical margin curves, matched simulation failures and the measured
upper-edge model correction. Both no-added-noise floor attempts failed playback
continuity and are excluded. Diagnose the underrun before repeating that long
playlist; successful marker-conditioned offline decoding does not qualify
real-time endpoint continuity or a complete under-50-minute campaign.

Additional hardware-free checks (NumPy, SciPy and CPU PyTorch):

```sh
python tests/test_triad_live_receiver.py
python tests/test_triad_live_margins.py
```

For the flat baseline/edge regimes, `triad_live_frontend_compare.py` compares
the old DDC with the explicit causal filter in `triad_live_narrowband.py`, using
the same raw capture and separate physical-null gates. `triad_live_level_probe.py`
prepares repeated absolute-level inputs; `triad_live_level_analysis.py` separates
repeat variation from repeatable model residuals without payload normalization.
These tools require fresh output paths and source-bound, complete capture
receipts. Their fast checks are included by
`python3 -m unittest discover -s tests -p 'test_triad_live_*.py'`.

Do not reuse a nominal-width filter for unqualified oscillator offsets, Doppler
spread or modulation sidebands: derive the required passband/capture envelope,
retain complete state/tails and verify template energy first. Current narrow
admission qualification is limited to the flat baseline/edge cases and ±0.06-Hz
conditioned CFO bank. New preprocessing or noise conditions require new gates.

## Maintained coverage inventory

The plan should select representative boundaries rather than repeat equivalent
Cartesian combinations. The following maps the requested regimes to the
evidence they can receive; measured status belongs in the source-bound run
record, not in a nominal configuration name.

| Regime | Required observation or calculation | Evidence/remaining limit |
|---|---|---|
| Rates 0.001, 0.01, 0.1 Hz | Native actual-rate windows at low and audio-passband centers; full-calendar calculation | Component only within routine; complete-bit hardware behavior untested |
| Rates 1, 3.6, 10 Hz | Selected full patterns, including Added ×100/×1000 where audio geometry permits | Direct complete-pattern examples measured; not all Cartesian combinations |
| Rates 3.6, 12, 18, 24 kHz | Full patterns at 1.725-kHz or raised centers; near-upper-band boundary | Direct examples measured where images fit; the 24-kHz-rate/16-kHz-center case remains excluded |
| Center `Rate/2` | Nominal image check plus low-frequency probes | Unsupported if images overlap; analog attenuation otherwise measured or bounded |
| Center 1.725 kHz and adaptive higher centers | Transfer/phase measurements over complete occupied band | No claim outside measured coherent support |
| Added ×100 and ×1000 | Geometry census and selected complete/partial keyed waveforms | Added factor retained; unsupported bandwidths are not downscaled |
| Symbol Burst | Realized internal quiet gaps, preserved source and hardware tails | One exported bit with no realized gap is not burst coverage; backlog scheduling remains separate |
| Fake FHSS | Protected dwell geometry, ramp/guard waveform and a multi-tile capture | Envelope-only; no actual RF hopping or retuning |
| High Doppler/delay and Each25/Each65 | Shared-channel waveform replay with paired clean/native references | Synthetic propagation plus real audio path; not measured RF propagation |
| Low level, level changes and nonlinearity | Fixed-level training/holdouts, omitted-bin residuals and peak/xrun checks | Combined path only; no deliberate heavy clipping requirement |
| Streaming/tails | Chunk-equivalence software checks plus physical gaps/endpoints | Native channel history and hardware state must not reset between chunks |
| Radios, GPSDOs and independent oscillators | Separate hardware fixtures and longer observations | Unidentified by one shared-clock cable setup |
| Multi-day reception and rare errors | Long-term independent physical campaign | Unqualified by abbreviated conditional simulations |

## Routine and focused development commands

Prepare the supported native build once, then build only the isolated tools:

```sh
./build.sh configure --cli --build-dir build/dev-cli -- -DBUILD_TESTING=ON
cmake --build build/dev-cli --target triad_long triad_audio_capture --parallel 2
```

The model/bridge checks need NumPy and SciPy in the selected Python environment.
They open no audio devices:

```sh
python tests/test_triad_live_source.py build/dev-cli/triad_long
TRIAD_AUDIO_CAPTURE_BINARY=build/dev-cli/triad_audio_capture python tests/test_triad_audio_capture.py
python tests/test_triad_live_model.py
python tests/test_triad_live_waveforms.py
python tests/test_triad_live_alsa.py
python tests/test_triad_live_duplex.py
```

Use the [campaign runner](../tools/triad_live_duplex.py) to freeze a plan, prepare
the inputs, then perform the opt-in hardware run. Every output directory must
be new. The runner records a clock-derived epoch at planning time; retained
fixture seeds are public and are not operational secret keys.

```sh
python tools/triad_live_duplex.py plan --output local-evidence/live-plan
python tools/triad_live_duplex.py prepare \
  --plan local-evidence/live-plan/plan.json --output local-evidence/live-input
python tools/triad_live_duplex.py run \
  --prepared local-evidence/live-input --output local-evidence/live-capture --live
```

The default whole-command deadline is 2,900 seconds. Record the sum of planning,
preparation and live-run wall times when assessing the complete routine against
the 50-minute target; separate command deadlines do not by themselves establish
that target. Preflight must leave analysis/finalization reserve after stimulus
time. `plan --profile smoke --skip-census` selects an explicitly abbreviated
route diagnostic, not complete suite qualification.

The live command verifies the active physical desktop route before opening the
explicit hardware endpoint. Use `--capture-source`, `--playback-sink`,
`--capture-device` and `--playback-device` to override discovery with verified
names. `--capture-backend alsa-tools` is the default; `--capture-backend native`
selects the separate standalone native adapter. Retain the actual backend
identity because one execution path does not qualify the other.

If another application takes the device, retain the failure and resolve device
ownership explicitly. Resume into a fresh directory without replaying or
reanalyzing already verified complete cases:

```sh
python tools/triad_live_duplex.py run \
  --prepared local-evidence/live-input --resume-capture local-evidence/live-capture \
  --output local-evidence/live-continuation --live
```

Continuation verifies the same prepared inputs, sealed capture receipts and
original frozen profile. It carries prior measured active wall time into the
remaining budget and reports the human pause separately. The original failure
and measurements remain intact. This prevents needless repeated work without
representing a paused campaign as an uninterrupted timing demonstration.

Reanalyze retained captures without opening hardware or repeating playback:

```sh
python tools/triad_live_duplex.py analyze \
  --prepared local-evidence/live-input --capture local-evidence/live-capture \
  --output local-evidence/live-analysis
```

A source-only low-rate interval can be generated without first rendering days
of samples. Use a fresh output directory:

```sh
build/dev-cli/triad_long --mode export-source-window --rate .001 --added 1 --seconds 8 --now \
  --source-window-seconds 16 --source-window-center --phase-diffusion 0 \
  --output local-evidence/triad-live-window
```

Hardware capture is always opt-in. The executable's `--help` describes required
paths and bounds; substitute verified physical device IDs, never assume that
`hw:1,0` names the same hardware on another host. Do not run concurrent device
owners or make unrecorded mixer changes. A failed/incomplete capture is retained
for diagnosis; repeat only after identifying a changed condition, not until it
passes.

## Interpretation and reuse

Reuse a frozen profile only for the same identified route, negotiated rate,
mixer/input-gain state and tested signal-level domain. A changed device, audio
server/provider, converter mode, rate, filter, level behavior or timing transport
requires an appropriate new probe/holdout. An unchanged native receiver does not
make a stale physical profile valid. Conversely, a receiver-only change can
reuse retained I/Q and public templates when their format and diagnostic
assumptions are unchanged; it need not replay the cable merely to recompute
linear statistics.

Offline analysis verifies the profile-freeze receipt, training stimulus/capture
identities, and capture hashes and negotiated-format logs. Legacy inputs without
sealed hashes receive consistency-only verification; a missing chronological
freeze receipt is explicitly post hoc, not independent frozen-model validation.

The initial one-pole approximation matched the principal audio band well but
failed to predict the measured 1-Hz gain and phase. A training-only three-pole
extension subsequently passed ten fresh validation tones and failed the two
below 1 Hz (10/12 overall). Those failures remain part of the qualification
record. A fourth-order model, using two real poles plus a biquad in the same
two-SOS/four-state budget, was fitted to the same training data and frozen
before a fresh physical validation. All 12 resolved tones from **0.6875 to
79.6875 Hz** met the recorded engineering envelope; maximum absolute gain and
phase errors were **0.672 dB and 0.0446 rad**. The independent payload, including
burst/step behavior, had +0.00216-dB energy error and −41.06-dB fixed-model
residual. This is one capture containing 12 tones, not 12 independent hardware
trials or a simultaneous statistical confidence bound.

The new full plan explicitly selects that fourth-order model. Training begins
at 0.5 Hz, while independent validation begins at 0.6875 Hz; the lower region
is not independently qualified. The 0.005- and 0.05-Hz components were below
the usable residual floor. Neither the fitted curve nor favorable 1.725-kHz
carrier results qualify near-DC extrapolation. Main-band predictions were
preserved in two retained-capture checks with the final profile; this does not
relabel the entire original campaign as a new physical run of that profile.

At the tested upper-band boundary (Rate 24 kHz, 15.5-kHz audio center), the
original frozen model's payload-only energy error was +0.048 dB and residual
was −28.72 dB. The deliberate very-low-level case used only PCM codes −4 to +4,
lost 2.64 dB to playback quantization, and had capture noise dominate its
full-band payload energy. These measured limits require explicit headroom and
noise accounting even when a coherent bit-ranking diagnostic selects the
transmitted bit.

The [ALSA PCM documentation](https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html)
describes stream state and xrun handling. Farina's
[audio measurement methods](https://www.angelofarina.it/Public/papers/226-AES122.pdf)
provide background on sound-card measurement and clock mismatch. These sources
motivate instrumentation; the concrete hardware validity of this suite comes
from its own retained training and independent held-out observations.
