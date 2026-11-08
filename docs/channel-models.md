# Sampled Robust channel models

The current Robust channel is the native streaming channel used with the
canonical `IndependentApskV2` waveform. Its public configuration and report are
in [`triad_modem_v3.hpp`](../include/datapump/triad_modem_v3.hpp); its streaming
implementation is [`triad_simulation_stream.cpp`](../src/triad_simulation_stream.cpp).
Python acquisition and tracking remain sequential receiver stages. These
simulation controls do not connect Robust reception to the GUI, establish an
end-to-end operating result, or qualify physical hardware.

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
