# Three-carrier source and historical receiver findings

The retained source is `IndependentApskV2`; its derivation is described in
[independent patterns](triad-patterns.md). Earlier v3 measurements may use the
retired cyclic-amplitude/QPSK pattern revision. Their waveforms and calibration
identities are different. No legacy revision selector remains in current code.
Use [current Robust architecture](robust-current.md) for implementation and
validation status; the historical receiver studies below are not another
supported backend.

## Format and streaming state

Three adjacent carriers carry independently keyed amplitude/phase alternatives.
Added Symbols applies independently keyed two-radius/two-phase APSK factors,
normalizes coefficient power, and interleaves complete cells. Constant coefficient
power does not guarantee a constant composite envelope after shaping, carrier
summation or propagation. In particular, low carrier spacing can yield long
power imbalance and envelope zeros. Spectral appearance does not prove Gaussian
white statistics, interception resistance, or receiver sensitivity.

Native geometry owns fine-cell timing, pulse support, 400-ms dwell calendars,
private tile addresses and active/elapsed durations. Tiles preserve complete
inner coefficients and pulse/endpoint margins. Requested duration can round up
to the tile lattice. Nominal payload-energy duration is not measured recovered
energy. Source/sample coordinates are checked integers; long coordinates do not
require a complete-duration waveform allocation.

`PatternSource` and the bounded source/basis service produce the same waveform.
`StreamingChannel` propagates the already-shaped emission with continuous state
and complete delayed tails. Echoes survive transmitter hop guards. Preserve
explicit Off baselines and the selected Doppler, delay, modulation, oscillator
and sample-clock effects in the [channel contract](channel-models.md).

## Current receiver and search

The native C++ `CompleteStreamReceiver` joins noisy Helper acquisition, full
public FFT proposals, persistent fine tracking, bit/null decisions and scored
quiet-slot completion to bounded native short/long-message and configured FEC
processing. Native shaped
bases and shared immutable input support its stages. The GUI session links this
same stack; no Python receiver or receiver IPC is in the operating path.
Reception uses a configured public calendar and keyed ordinal. Current
functional checks do not qualify unknown-start acquisition, native CPU capacity
or physical hardware; see [validation](validation.md#native-codec-and-measurement-migration).
The earlier native QR receiver and isolated v3 alternatives are retired.

Research supports sharing complex FFT proposals across keyed time addresses,
bit alternatives and clock hypotheses, then refining a bounded correlated
candidate list with joint channel likelihood. This does not prove equality to
an exhaustive Cartesian acquisition search. A fixed candidate bank, normalized
channel prior and observation endpoint must be distinguished from adaptive
proposal selection and repeated operational attempts.

## Likelihood techniques worth retaining

Joint complex evidence preserves information that amplitude-only or independent
per-carrier decisions can discard. Blockwise Gaussian channel evolution can
retain bounded sufficient statistics instead of full observations. Conditional
phase particles and bounded common/relative ray-frequency mixtures explored
less Gaussian alternatives. Their computational cost, prior support and model
mismatch are separate from the waveform.

The retired state study exposed false admission when assumed noise power was
too small. [Noise research](triad-noise-calibration.md) retains that measured
failure, the common-scale and independent-residual arguments and their limits.
The October 31 aligned HF experiment accepted 320/320 one-bit messages at
8 dB-Hz using a 23.2-second pattern; its one-sided 95% lower bound was 0.990682
for that selected model. Much larger projected-Gaussian checks covered a fixed
bank with known noise, not a sampled complete receiver. Later state-model
results likewise did not qualify unknown noise, repeated blind search or the
current architecture. Their redundant raw campaigns are removed; retain these
scope limits and the [GPU findings](triad-gpu-optimization.md).
The [broad study](validation-data/triad-v3/2026-11-03-broad/README.md),
[extension](validation-data/triad-v3/2026-11-03-extension/README.md), and
[FFT timing evidence](validation-data/triad-v3/2026-11-03-fft-timing/README.md)
retain dated results rather than current qualification.

## Development boundary

Follow [current architecture](robust-current.md), the [development workflow](triad-development-workflow.md)
and [abbreviated testing](triad-performance.md). Historical complete-capture,
full-duration, GPU sweep and standalone modem drivers have been removed. Use
small discriminating checks and applicable source-matched evidence; do not
reconstruct those drivers or introduce another implementation for a protocol break.
