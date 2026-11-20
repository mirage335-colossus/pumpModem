# Sampled channel models

The current Robust channel is the native streaming channel used with the
canonical `IndependentApskV2` waveform. Its public configuration and report are
in [`triad_modem_v3.hpp`](../include/datapump/triad_modem_v3.hpp); its streaming
implementation is [`triad_simulation_stream.cpp`](../src/triad_simulation_stream.cpp).
The GUI session uses this same native channel between emitted DATA/Helper and
the complete native receiver. Python may test or compare the implementation but
is not in that operating path. A channel connection or component parity result
does not qualify physical hardware or all receiver configurations; see
[current validation](validation.md#native-gui-integration).

Fast Modem has a separate native sampled channel in
[`fast/simulation.cpp`](../src/fast/simulation.cpp). `ChannelAssumptions` are
the local receiver declarations: they are validated in every mode and help
deterministically resolve Auto, independently of test-effect injection.
Simulation starts from a local Fast transmission and sends its resulting
waveform through the Fast receiver. Transmit power, path loss and
noise controls define the simulated link ratio in normalized receiver-input
units: transmit power and loss set the equivalent received level relative to
independent receiver noise. The samples remain normalized rather than being
scaled to a physical DAC voltage; noise is added after propagation at the
simulated receiver boundary. Common receive gain therefore does not change the
modeled ratio. Other effects include oscillator offset and phase, selected
delay/Doppler spread, and optional selective AM. The channel reuses the neutral
spread-ray and selective-perturbation kernels, but does not instantiate the
Robust receiver or Robust DATA/Helper waveform. It preserves delayed and
filter-decay tails and supplies a quiet interval for Fast completion. Simulation
runs entirely in software.

**Live loopback test** uses physical audio capture and playback with the Fast
receiver. Transmit power, path loss and modeled receiver noise are Simulation
assumptions: in Live loopback they are disabled and retained and do not alter the
playback signal or captured input. Only an explicitly started test copies the
declared oscillator, delay/Doppler-spread and selective-AM effects into the
waveform before the DAC as synthetic stress; the copied search margin is one
because margin affects receiver bounds only. Idle loopback monitoring and
ordinary Live use clean input. Playback uses the selected TX
volume and does not change the system mixer or apply an automatic gain change.
The selected nonempty Console text or file is preflighted before audio opens;
it is neither truncated nor replaced by a fixture. Capture must start within
5 seconds. The existing maxima remain 75 seconds of audio and 110 seconds
elapsed overall. After the full emitted source and canonical quiet tail, at
most 2 seconds of captured samples are allowed for receiver completion. This
grace can only report failure; it cannot synthesize physical completion.
Silence is maintained during this wait to avoid waiting indefinitely for audio
server playback drain. Progress distinguishes audio samples queued for playback
from samples captured at the receiver. The queued count includes silence and
does not confirm speaker output. A verified result becomes an ordinary Signals
or Files record with LOOPBACK TEST provenance only after physical completion,
integrity checks and exact comparison with the selected source. Public checksums
do not authenticate a sender; encrypted mode requires authentication. Live
loopback's synthetic effects describe a modeled stress overlaid on an acoustic
path, not RF propagation performance.

While Live loopback mode is selected and idle, the GUI runs ordinary receive-only
monitoring. Before a test, that session closes so the bounded duplex test can
own the device; monitoring restarts afterward. Tab switches do not stop it, and
switching to another modem releases an idle Fast capture for the shared audio
handoff. Ordinary **Live audio** also uses the selected device without the
synthetic propagation or noise model. Realized channel reports never select
receiver geometry or establish physical completion; declared assumptions can
select the deterministic local Auto profile. Profile and waveform changes
revalidate the declarations. An invalid receiver declaration reports the
required frequency range, available sampled band and oscillator/channel
allowances, and blocks listening and transmission in every mode while leaving
fields editable. Invalid injected test effects disable Simulation and explicit
loopback, but not ordinary Live. On the current automatic 231-baud profile, a
CLI-only strong-signal Gaussian-ray diagnostic (30 Hz bandwidth per ray,
2σ; 0/7 ms delay) fails acquisition while its fixed-channel control passes.
An explicit experimental 1,000-baud dense-pilot candidate passes strong digital
Gaussian, oscillator and selective-AM checks, plus a clean −6 dB reference-noise
check; Auto does not select this candidate. The matched full-stress physical
speaker/microphone trial failed acquisition. Weak Gaussian reception and full
physical stress remain unqualified. See the [scoped acoustic results](fast-acoustic-short.md#validation-scope)
and [validation record](validation.md).
See [Fast mode](fast-mode.md#fast-simulation) for the test workflow and limits.

## Baseline and channel stages

`StreamChannelConfig::effects` selects the shared synthetic impairments.
Zero-valued spread and perturbation settings with noise disabled are an
explicit channel baseline. Defaults are simulation settings, not calibrated
field conditions. Keep the complete emitted waveform as input: channel
propagation acts on the sum and does not identify or decode carrier streams.

The channel may model transmitter and receiver oscillator error, sampled
two-ray excess delay and Doppler separation, frequency-selective perturbation,
receiver noise, optional physical-path evolution, sampled frontends, and
observation erasures. Their parameters and independent seed roots are declared
in `SimulationConfig`, `PathEvolutionConfig`, `SelectiveModelConfig`, and
`SampledFrontendConfig`. The models are explicit synthetic assumptions, not
measurements of a named device or propagation path. Record the full config,
waveform format, seed roots, and source revision with results.

`StreamingChannel::read` preserves state across chunks. Noise, path, and
frontend states advance through actual channel processing even when an
observation is marked erased. The stream preserves delayed echoes and filter
decay tails; do not truncate at the emitted source endpoint. Its report
separates source length, complete output, tail, observation padding, erased
samples, converter clipping, and model diagnostics.

## Stream evaluation, workers and capture identity

A capture is identified by the SHA-256 of its samples and validity mask, and
validation records cite those hashes. The channel and the pattern source
therefore keep every floating-point operation and its order. Reordering one
sum, or replacing a library call by an identity, changes the low bits of the
samples and makes every earlier capture a different input. The changes below
left the arithmetic alone and changed where and how often it runs.
[Measurements and checks](validation.md#simulator-throughput-and-capture-identity--20-november-2026).

`StreamingChannel::read` prepares its rows in blocks of at most 128, or 1,024
with workers. For each block it generates the source history that the last
row needs, evaluates the terms that depend on a row's index alone, and then
advances the sequential states row by row in the original order. A read still
consumes the emitted source exactly as far as its own rows need, which the
GUI session relies on when it reports transmit progress after each read.

- **Independent rows.** The two sixteen-tap source interpolations and the
  fixed-time ray and selective coefficients of a row read only its index and
  the generated history, so rows can be evaluated in any order. The
  interpolations were 31–48% of the simulator's time on the matched captures
  and the selective coefficients 13% of the severe one.
- **Silence.** An interpolation whose sixteen history samples are all zero
  returns zero without evaluating its 48 sines and cosines. Every product is
  a zero, the sum starts at and stays positive zero, and the coefficient sum
  is within 1.3e-4 of one, so the quotient is that same zero.
- **Repeated values.** Coordinates and rotations that are the same for every
  row are computed once with their original operand order. A bounded curve
  keeps its two knot values for the interval it is in, and the pattern source
  keeps the coefficients of the sixteen or seventeen cells under its pulse,
  which change once in 10,000 samples at 3.6 Hz.
- **Rounding.** Floors, ceilings and integer conversions of `long double`
  coordinates use forms that return the standard values without switching
  the x87 rounding mode. Those switches are the likely explanation of the
  40% difference between binaries with equal simulator objects.
  [Mechanism and guidance](triad-performance.md#code-placement-and-x87-rounding-mode-switches).
- **Unread work.** The delayed interpolation runs only when a two-ray or
  bounded law reads it, which removes 48 of the 96 sines and cosines of a
  row on the unspread stable and five-slot message captures. Rows carry their
  echo delay into the sequential pass instead of evaluating the path again;
  a row whose coordinates failed is evaluated again, so the failure is
  reported at the same row. The selective filter updates and renormalizes
  its phases only for the sinusoidal law. Without FHSS the transmit dwell
  index is skipped when a bound found at construction proves that every
  transmit calendar coordinate converts.

`StreamChannelConfig::lanes` lends a channel the caller's workers. The rows
of a block are split among up to sixteen jobs. One further job each advances
the receiver oscillator, the thermal noise and the two bounded path
trajectories through the block. None of those states reads the signal, so
their values depend on the row alone. The next block's source history is
generated on one of two paths:

- **Split.** When the source offers `EmittedSource::readers` and neither a
  converter nor diagnostic I/Q noise reads source values, or when the signal
  is disabled, the transmit oscillator is a job of its own. It forms
  `signal_gain * polar(phase + white_pm)` for each sample with the same
  operations and random-number order. Up to eight readers evaluate disjoint
  parts of the source range at once. The owner then accounts the samples in
  order exactly as `sample_at` would: forward cursor, stop checks every 256
  samples, slot transitions and the DATA and Helper energy sums. The
  earliest failure wins, the oscillator ahead of the source at the same
  index. Each block's sequential pass runs as one more job beside the
  preparation of the read's next block. The read's last block, and a block
  with a recorded failure, is passed alone. A pass that fails inside that
  overlap discards the range evaluated beside it, so the emission report
  equals a lone reader's.
- **Fused.** Without readers, with a converter or diagnostic noise, or with
  a lone reader, one job generates the source and transmit oscillator chain
  of the next block in the original order.

`Emission` gives each reader its own DATA `PatternSource` for each slot and
its own Helper `Generator`, and the probe's composite source forwards to
every component's readers, including the interferer. `EmissionOptions::lanes`
evaluates the 32,768-node DATA power reference, the sixteen-tile
`bounded_tile_power` and the Helper reference norms in parts, each with its
own source and per-part copies bounded to 32 MiB; the sums keep node and tile
order. The receiver's `data_power_reference` stays sequential.

The reader applies the prepared terms in order with the selective filter,
front end, gain control and report sums. Samples, masks and reports are
identical for every worker count. Two things differ from a lone reader: the
source, its readers and the sequential pass can run on a worker, and a read
that fails ends the channel. The probe lends the receiver's executor, whose
workers are idle while the capture is generated. It charges their stacks to
its memory limit before it sizes its two read buffers, which start at 4,096
rows and grow to at most 65,536, so a larger read never removes a simulator
worker. Its SHA-256, copy and spool append run in 4,096-row pieces, one
piece as an extra job of each lent preparation. The workspace grows with
workers: at 16 workers `channel_workspace_bytes` rose from 314,897 to
1,640,313 on the stable capture and from 3,281,961 to 26,279,953 on the FHSS
capture, whose readers copy the pattern-source caches, all inside the 65 MiB
channel bound. The GUI simulation keeps a lone reader, because its receiver
is using those workers.

The Gaussian FIR path law, the Ornstein–Uhlenbeck ray gains, the sinusoidal
selective law and the sampled front end remain in the sequential pass. With
twelve workers on the Ryzen 5 PRO 5650U the severe slot took 0.56 s against
1.06 s before the readers and the overlapped pass, and twenty-four workers
were no faster. Which stage now bounds it was not measured.
[Measurements](validation.md#exact-receiver-and-simulator-speedups--20-november-2026).

Proposals that change sample values, measured and not adopted:

| Proposal | What changes | Measured or estimated gain |
| --- | --- | --- |
| Interpolation coefficients from one sine and one sine/cosine pair per coordinate, with angle sums for the sixteen taps | Coefficients differ by at most 3.3e-16 and their sum by 1.1e-15, so every sample with signal changes in its last bits | Measured on the 5650U: the severe slot from 2.70–2.73 s to 1.78–1.79 s with one worker and from 1.05–1.06 s to 0.89–0.90 s with twelve; the stable slot from 2.04–2.14 s to 1.15–1.16 s with one worker |
| Gaussian draws from a table method in place of the standard library's polar method | Every noise and oscillator draw, so each capture becomes another realization of the same laws | Estimated: ten draws per sample cost about 24 ns each. They are about a tenth of a lone reader's severe slot and most of the oscillator and noise jobs. Not implemented |
| A tabulated polyphase interpolation kernel | Coefficients to the table's accuracy | Not measured. It removes the remaining interpolation arithmetic, which workers already absorb |

Adopting any of them needs new capture hashes for every record that cites
one, so it belongs with a change that already invalidates them.

A change to this code is checked against the build before it, not against a
description. Run both on every path law, with and without workers and with
small and large reads, and compare samples, masks and reports bit for bit.
[`test_triad_v3_channel`](../tests/test_triad_v3_channel.cpp) keeps the parts
that need no second build: workers against the lone reader, blocks against
single rows, a source failure in the middle of a block, and the rounding
forms against the standard ones. The validation record lists the configurations that the current
code was compared on.

## Robust GUI changing-channel realization

The Robust GUI now treats the selected spread as support for a changing
synthetic realization. `HF Worst` declares 7 ms excess-delay support and
30 Hz differential Doppler before the configured margin. One echo delay is
drawn inside that support. The receiver does not use the realized delay; it
scores separately declared bounded delay bases.
Its echo/direct amplitude ratio varies smoothly between zero and one, and
its instantaneous frequency varies within the signed Doppler support.
The two path powers sum to one before their interference and selective
filtering; the direct path stays present. Effective delay spread changes with
the echo power. This is not rapid physical motion of a millisecond-delay path.

The selected spread Doppler also supplies a common atmospheric excursion
allowance; the magnitude in the explicit Doppler field adds to it. Thus
HF Worst still permits common frequency motion with exact oscillators and
the explicit Doppler at zero. The explicit field no longer selects one
constant simulated offset. Echo
and common frequency are integrated into continuous phase, with independent
seed domains and no resets across reads and public slots throughout one emitted
source. A separate simulation creates fresh channel state. Common wander and
relative echo Doppler add: at HF Worst with 3× margin and explicit Doppler zero,
the direct path can reach ±90 Hz and the echo ±180 Hz absolute shift. Capture
allowances account for this sum. Common wander, echo strength and echo frequency
use smooth independently drawn knots at `max(1, 1/max(bound, 1/3600))`
seconds. These are reproducible synthetic timescales, not a calibrated HF
law. The report distinguishes configured bounds from extrema actually visited;
a short realization need not visit the extremes.

Selected amplitude perturbations use the existing bounded Gaussian-erf
coefficient law. Slow and fast depths remain separate (Each 65% sets both to
0.65); they do not promise a total ±65% waveform envelope. The underlying fast
spectral width is one tenth of the existing selected modulation allowance,
with slow width another factor of ten smaller. This matches the channel's
ten-width-per-edge capture convention without enlarging the GUI's declared
audio passband. That convention is an allowance for spectral tails, not a
hard bandlimit. Off retains exact bypass.

Low-level `StreamingChannel` defaults retain the fixed two-ray diagnostic
control; `PathEvolutionConfig` explicitly enables bounded echo variation and
common wander. A single channel instance processes the entire emitted composite,
including Helper, silence, delayed tails and erased observations. The waveform
generator and real playback remain clean. **Simulation = No** bypasses synthetic
effects in both duplex states; **Live/Duplex** changes capture ownership.

The default two-path realization does not cover direct-path disappearance, arbitrary
diffuse scattering, all independent carrier fades or arbitrary variation rates.
Other native path laws remain available as independent stresses. Simulation
support and a receiver parameter are not themselves a reception guarantee.
See [research reconciliation](triad-stochastic.md#nonstationary-channel-research-reconciliation)
for the distinctions and contrary findings that guide qualification.

The native bounded-ray simulator also supports an explicit fixed direct-power
fraction for independent qualification. It replaces only the normalized ray
amplitudes with `sqrt(p)` and `sqrt(1-p)`, retaining the same delay, integrated
common/relative frequency, selective filtering, noise and advancing random
state. It is available only with the bounded-ray law; the default GUI law and
its arithmetic remain unchanged. Reports identify fixed direct/delayed powers
and actual echo/direct amplitude ratio, with a null ratio at zero direct
power. Both endpoint histories and tails remain present in the simulation's
support accounting. Bounded-ray reports also record simultaneous common-plus-
relative Doppler extrema in physical Hz over the full processed history,
including preroll and silence. These exclude static Doppler, oscillator,
diurnal and selective terms and precede receiver-clock scaling; a zero-power
ray still has a reported trajectory. They are not selected-bit or measured
received-carrier extrema. This enables delayed-dominant or missing-direct stress
without claiming that a selected power ratio, seeded delay or motion law is a
calibrated channel replay. See the
[measured motivation and limits](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#persistent-path-ordering-clock-age-and-noise).

### Distinguish HF stress profiles

GUI **HF Worst** and appraisal `hf-disturbed` share nominal 7 ms / 30 Hz
labels, but are different channel laws. GUI values are pre-margin support;
Margin 3 declares 21 ms / 90 Hz, and the changing realization includes common
frequency wander as well as relative echo motion (the direct ray can reach
±90 Hz and the echo ±180 Hz). The standard `hf-disturbed` case is the existing
fixed-grid Gaussian comparison: two independent zero-mean proper-complex taps
at 0 and 7 ms, each with 30 Hz full 2σ power-spectrum width. It does not add the
GUI's common-wander mechanism or define a new channel law; see the [HF model
catalog](../_appraisal/highFidelity-triple_Robust_Modem/models.md#hf-standard-and-nonstationary-controls).
ITU-R F.1487 Annex 3 calls 2σ the frequency-spread convention and lists
high-latitude disturbed at 7 ms / 30 Hz, while disturbed NVIS is 7 ms / 1 Hz
with a groundwave component added only if required [F.1487 Annex 3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf).

A separate moving-plasma two-hop realization produced about 0.17 Hz frequency
excursion and 0.325 ms delay with zero declared GUI spread and a 5 Hz receiver
allowance. Its small delay/Doppler spread does not remove its deep cancellation
or weak-reception failures, but it is not a severe-spread qualification.
Keep the channel's realized spectrum distinct from its declared
search/capture allowance: test calm realizations inside broad public bounds as
well as separate severe statistical and moving-plasma cases. Neither profile
guarantees a fixed transmission time, energy, or reception outcome relative to
AWGN.

### Noise and frequency assumptions in the 2 mW HF tests

The November 2026 HF comparisons kept the same broad receiver search for
all three physical channels. Stable controls disabled simulated path motion,
delay spread and selective modulation while retaining oscillator errors and
receiver noise. The Gaussian comparison replaced the GUI path evolution with
the independent 0/7 ms fading taps described above and retained Each 65%
modulation on the signal. The two hobbyist GPSDO XO presets provided
illustrative 0.0001 ppm accuracy and 0.5°/√s phase-diffusion parameters.
The 30 MHz translation and 1,725 Hz upper-sideband tone implied a nominal
30.001725 MHz carrier coordinate. Fake FCC applied its dwell envelope in
baseband. [Oscillator conventions](oscillator-models.md#independent-physical-quantities)
and [editable HF trials](../_tests-RobustModem-HF-simulations-2026-11-18.md).

The assumed −164 dBm/Hz equivalent receiver-input background was realized as
proper-complex Gaussian white noise. At 2 mW and 180 dB loss, the nominal
C/N₀ was −12.9897 dB-Hz. The native 6,000 Hz complex stream therefore used
normalized variance `6000 / 10^(-12.9897/10) ≈ 119432`. A site estimate
requires the actual RF band, latitude, longitude, month, hour and local noise
environment. Relating that estimate to the receiver input requires the
antenna and feeder response, bandwidth and receiver noise. Qualifying
lightning impulses, local interference or front-end overload additionally
requires a measured episode or declared amplitude, timing and equipment
model, with a stated severity percentile or other selection rule.
These effects remain unmeasured in the HF trials.
[ITU-R P.372 radio noise](https://www.itu.int/rec/R-REC-P.372-18-202609-I/en)
and [ITU-R Study Group 3 calculation inputs](https://github.com/ITU-R-Study-Group-3/ITU-R-HF/blob/master/P372/Src/AtmosPlots/README.txt).

The separate `noise-bursts` appraisal fixes its colored and impulse terms
to the white variance at +32 dB-Hz. Their combined mean power is about 1.25
times that reference variance. Within that fixture, entering −12.9897 dB-Hz
for the white term would put their combined mean about 44.02 dB below it
(`10 log10(1.25) − 12.9897 − 32`). That calculation describes the fixture's
scaling. Receiver errors still depend on burst peaks and correlation. At the
original +32 reference the five-slot noise-only trial produced five false
DATA decisions. On that same capture a diagnostic 128.735× receiver variance
gave five quiet decisions and recovered the wanted message on the paired
capture. This calibration applied to that constructed noise process.
A site-matched impulsive-noise comparison would need wanted
and silent episodes covering the same physical quiet slots.
[Noise fixture definition](../_appraisal/highFidelity-triple_Robust_Modem/models.md#noise-and-equipment-controls)
and [quiet-slot outcomes](validation.md#persistent-channel-and-calendar-age-follow-through--17-november-2026).

## Interpretation limits

- The fixed two-ray control has equal-power direct and delayed rays; the GUI
  changing realization varies their ratio. Neither is a measured delay or
  scattering spectrum. Static separation and selected
  stochastic ray evolution are distinct assumptions.
- Selective perturbation is a synthetic signal-level stress model. It is not a
  calibrated per-carrier fading prediction, and its diagnostic energy reports
  are not receiver estimates.
- Gaussian and OU path models have their stated finite implementations and
  capture assumptions. A spectral allowance is not a hard bandlimit or proof
  that a narrow sample rate captures all useful energy.
- Narrow complex-I/Q diagnostic noise is not full-rate audio/DAC noise; the
  two have different bandwidths and cannot be equated by variance alone.
- Simulation results show behavior under the recorded model and inputs. They
  do not substitute for field measurements, physical-device checks, or
  receiver-system qualification.

For concise retained experimental findings and their dated evidence, see the
[triad channel technique summaries](triad-stochastic.md) and
[validation record](validation.md#historical-provenance). The abbreviated
performance method is documented in
[`triad-performance.md`](triad-performance.md); no full or sustained campaign
is a fallback for it.
