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

## Interpretation limits

- Two-ray spread is an illustrative equal-power direct and delayed ray, not a
  measured delay or scattering spectrum. Static separation and selected
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
