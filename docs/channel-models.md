# Doppler/delay spread and frequency-selective amplitude modulation

Use these models when building simulations that need Doppler/delay spread or
frequency-selective amplitude changes. They are explicit channel impairments;
the presets do not claim calibrated field measurements. Apply them on the
sampled audio path. The analytical receive-probability and planner estimates do
not include either effect and are not impairment-decoder predictions.

## Controls and presets

The GUI exposes **Doppler/delay spread** and **Add. freq-selective** beside the
oscillator controls. Spread is channel dispersion. The separate **Doppler**
control is static radial vehicle motion and must not be substituted for spread.
The `tuning::SpreadPreset` values are nominal inputs; margin scales delay and
spread Doppler once. It does not scale modulation depths. EME's reference is
1296 MHz. The public preset type and parser are declared in
[`include/datapump/tuning.hpp`](../include/datapump/tuning.hpp).

| GUI preset | ID (`--spread-preset`) | Excess delay (ms) | Doppler separation (Hz) | New-preset modulation default |
| --- | --- | ---: | ---: | --- |
| None | `none` | 0 | 0 | Off |
| Sub-9 kHz worldwide — Best | `sub9-best` | 0.005 | 0.001 | Off |
| Sub-9 kHz worldwide — Moderate | `sub9-moderate` | 0.05 | 0.005 | Off |
| Sub-9 kHz worldwide — Worst | `sub9-worst` | 5 | 0.05 | Off |
| HF worldwide — Best | `hf-best` | 0.5 | 0.1 | Each 65% |
| HF worldwide — Moderate | `hf-moderate` | 6 | 10 | Each 65% |
| HF worldwide — Worst | `hf-worst` | 7 | 30 | Each 65% |
| Aurora worldwide — Best | `aurora-best` | 3 | 10 | Each 65% |
| Aurora worldwide — Worst | `aurora-worst` | 11 | 55 | Each 65% |
| EME worldwide — Focused | `eme-focused` | 0.5 | 6 | Each 65% |

The fresh GUI state and the omitted CLI options select `none` and `off`.
Choosing a spread preset also resets modulation to its family default; after
that choice, modulation is independent. The `tuning::SpreadPreset` table in
[`src/tuning.cpp`](../src/tuning.cpp) is the canonical source for preset values.
The modulation IDs are `off`, `slow-N`, `fast-N`, and `each-N`, where `N` is
1, 5, 10, 25, or 65 percent depth. Slow and Fast enable only their named
component; Each sets both components to that depth.

## Model meaning and implementation

The propagation model is an illustrative equal-power two-ray channel. The
direct ray and a delayed ray each have complex amplitude `1/sqrt(2)`. Delay is
the delayed ray's excess delay; Doppler is the frequency separation between
the rays. It is not a calibrated RMS delay or scattering spectrum. See the
implementation contract in [`src/channel_spread.hpp`](../src/channel_spread.hpp).

The additional perturbation splits the occupied waveform band into low,
middle, and high ranges using two all-pass crossovers. Each range gets its own
slow and fast sinusoidal gain components. The gain is additive around nominal:
`1 + slow_depth*sin(slow_phase) + fast_depth*sin(fast_phase)`. Depths are
fractions (for example, 65% is `0.65`); each component is relative to nominal
amplitude. `Each 65%` adds both components, so the sum can reverse polarity
(minimum gain `-0.30`). This is intentional stress, not power normalization.
The implementation and state lifetime are in
[`src/channel_perturbation.hpp`](../src/channel_perturbation.hpp).

Slow and fast rates are respectively `0.1 * spread_doppler_hz` and
`1 * spread_doppler_hz`, after margin has been applied to the channel field.
The three band centers are the occupied-band center and one-third-bandwidth
below and above it. Phases are seeded independently for each band and each
component, then continue across reads/processing blocks for the lifetime of
that channel instance. A new channel instance starts fresh state from its seed.
Keep the same channel instance and filter state across blocks and hops.
When spread Doppler is zero, no modulation bank is constructed even if depths
remain selected: gain is unity, with no random static gain or hidden minimum
modulation rate. A delay-only channel still runs its delayed ray.

Off and zero spread-Doppler bypass the perturbation filter bank. `none` plus
`off` leaves these added stages inactive, with no added channel filter or
physical attenuation, and retains the existing path unchanged. A retained
nonzero modulation depth at zero spread-Doppler has the same no-added-stage
bypass. This bypass applies only to these added spread/modulation stages;
other selected sampled-channel effects such as startup, noise, and oscillator
impairments still apply. Nonzero delay with zero Doppler is not the full
bypass; it remains delay-only propagation.

## Configure and choose a processing path

The shared configuration is `datapump::modem::ChannelConfig`, declared in
[`include/datapump/modem.hpp`](../include/datapump/modem.hpp):

```cpp
ChannelConfig channel;
channel.spread_delay_ms = preset.delay_ms * margin;
channel.spread_doppler_hz = preset.doppler_hz * margin;
channel.perturbation_slow_depth = 0.65; // slow or Each 65%
channel.perturbation_fast_depth = 0.65; // fast or Each 65%
channel.seed = seed;
```

Use zero for a disabled component. Do not apply margin again in a channel
implementation. The GUI's conversion from preset and selector to these fields
is in [`src/gui/controller.cpp`](../src/gui/controller.cpp); the CLI conversion
for `pump simulate` and `pump listen` is `apply_spread_effects` in
[`src/main.cpp`](../src/main.cpp). Their public options are `--spread-preset`,
`--selective-perturbation`, and optional `--simulation-trace PATH`.

Use one of the sampled/bulk/physical implementations below:

| Use | Implementation | Notes |
| --- | --- | --- |
| Streaming simulated reception / Live | [`modem::SampledSimulationChannel`](../include/datapump/channel.hpp) | Samples flow through the seeded channel and then to the receiver. Its bounded channel workspace is `workspace_bound`; preserve read cancellation and source/tail handling. This API can compose its explicit Helper-aware path. |
| Bulk PCM input | `modem::simulate(samples, config, channel)` in [`include/datapump/modem.hpp`](../include/datapump/modem.hpp) | Pass only already-shaped DATA samples. The function has no Helper classification or separation; keep Helper composition separate or use the Helper-aware sampled channel. It adds channel startup, spread, modulation, carrier effects and noise, without building a second transmitter/FHSS envelope. Bulk output storage is constrained by the configured memory limit. |
| Physical outgoing Robust DATA audio | [`modem::HardwareTestChannel`](../include/datapump/channel.hpp) | Wrap the streaming source at playback. Live owns the DATA tail and Helper mixing. Processing workspace is statically bounded by `workspace_bound`. |

The accelerated symbol-only [`modem::SimulationChannel`](../include/datapump/channel.hpp) cannot represent
frequency-selective samples and rejects nonzero spread configuration. Do not
silently skip an impairment or approximate it in symbol observations: route
the case through sampled PCM or report that the requested channel is
unsupported.

## FHSS, envelopes, and physical output

For FHSS-shaped Robust DATA, the source is generated first with its existing
ramps and quiet intervals. Propagation then reads both the direct source and a
delayed preview at that sample's emission-time coordinate, so the delayed ray
uses the envelope that was emitted earlier. Additional frequency-selective
modulation follows propagation, then the resulting DATA channel waveform is
written. No second/current-hop gate is applied after propagation. Echoes and
filter decay can therefore extend into a transmitter quiet interval or across
a hop boundary. Do not move the channel before transmitter shaping or gate its
output by the arrival-time hop. Preserve the original source bits, transmitter
endpoint and hop schedule; the channel output may extend beyond them.
`HardwareTestChannel::read` and the bulk/sample
implementations follow this ordering; see
[`src/channel.cpp`](../src/channel.cpp) and
[`src/modem.cpp`](../src/modem.cpp).

For **Simulation → Yes**, this is the modeled receiver input. For
**Simulation → No**, selected effects apply to outgoing physical DATA audio;
there is no separate Audio test gate. The channel processes Robust DATA only.
Helper has its own envelope and is mixed separately; propagation tails do not
extend Helper's own endpoint. Live drains until DATA channel tail and original
Helper output have finished. Capture is not channel-processed a second time,
and physical output does not add the simulation's noise, oscillator error, or
path-loss model. Standalone tuning-noise and Helper-only actions do not enter
this DATA channel. See the Live playback caller in
[`src/live.cpp`](../src/live.cpp).

The physical channel uses a single static headroom gain for the complete
channel configuration. Let `B` be the sum of absolute fractional-delay
interpolation coefficients (or `1` for integer delay), `S` indicate that a
spread ray exists, and `d = slow_depth + fast_depth` when perturbation exists
(otherwise `0`). Its gain is
`1 / (((S ? (1 + B)/sqrt(2) : 1) * (1 + 5*d)))`.
This bounds the two-ray interpolation and crossover excursion without
clipping or per-block normalization and scales the shared physical output,
including Helper, preserving their nominal ratio. Sampled and bulk simulation
do not apply this physical playback gain; keep their actual unclipped samples
and report clipping if a downstream format/device clips them. None/off uses
gain `1` and bypasses this processing stage.

Physical channel output bypasses the later hardware post-resampling quiet
enforcement when active, so that enforcement cannot erase echoes. The exact
None/off path keeps the historical hardware route, including its device-side
guard. A legacy `--hardware-channel-test yes|no` GUI launch argument is
accepted for compatibility, ignored, and no longer exported. It does not
control whether explicitly selected channel effects run.

## Reproducibility, tails, and interpretation

Use a fixed `channel.seed` and preserve the actual processed values. Channel
traces record the actual seed, effective delay/Doppler, two-ray initial phase,
modulation depths, actual slow/fast rates, per-band nominal width
(`band_width_hz`, occupied width divided by three), three band centers, and
each seeded initial phase. The current event identifiers are
`simulation_two_ray_spread`, `simulation_frequency_selective_perturbation`,
`hardware_channel_test`, and `hardware_channel_test_transmission`. They are
active-stage events: a None/no-stage baseline emits no spread/modulation event
or zero-valued stage metadata. For CLI runs, write JSONL to a new
`--simulation-trace PATH`; GUI/live runs can use `--verbose` or
`--verbose-file`. Preserve the selected preset ID, margin, selected modulation
control, resulting channel fields, seed, sample rate, modem geometry, source
waveform/FHSS settings, and trace events with a reproducer. The trace describes
the channel instance; it does not prove decoder success or hardware behavior.

The sampled channel keeps bounded streaming history and supports stop-token
cancellation. For a graceful stop of an open-ended source, call
[`StreamingTransmitter::request_finish()`](../include/datapump/streaming_modem.hpp);
it preserves emitted pulse
contributions and finishes protected cover and finite source tails. Continue
reading the channel through that endpoint and its delayed-data tail. A requested
stop token is an immediate abort, not a graceful drain. The
frequency-selective stage is IIR, so implementations append zero-input samples
until relative pole state has decayed by a factor of `1e6`; this is not an
absolute silence threshold. Preserve that tail when converting to arrays,
files, or playback. Physical `HardwareTestChannel::tail_samples()` reports the
extra `ceil(delay_in_samples)` plus filter decay beyond the source extent;
`source_end_sample()` reports the full channel-output endpoint once known.
Do not end receiver processing at the last source sample: physical reception
completion remains a separate receiver decision based on scored absence.

GUI Fake FHSS models an audio envelope and keyed hop metadata. It does not
retune physical RF, implement receiver selectivity, or add a multipath
equalizer. A valid channel waveform may still be undecodable because the
receiver's timing/template support does not cover echoes across a hop. The
ordinary GUI Fake FHSS option does not enable the separate laboratory RF
retune/settling model.

## Isolated triad long-span path evolution

The separate v3 streaming study optionally extends the shared sampled path
through `datapump::triad::v3::StreamChannelConfig::path_evolution`; it does not add these
controls to ordinary Robust playback. See
[`triad_modem_v3.hpp`](../include/datapump/triad_modem_v3.hpp) and
[`triad_simulation_stream.cpp`](../src/triad_simulation_stream.cpp).
The default-off path preserves the earlier samples and RNG sequence.

| Isolated `triad_long` controls | Meaning |
| --- | --- |
| `--ray-coherence-seconds T` | `0` disables OU evolution; positive `T` multiplies the direct and delayed inputs to `SpreadRay` by independent stationary proper-complex OU gains. Each gain has zero mean, unit mean power and correlation `exp(−abs(dt)/T)`. The existing `1/sqrt(2)` two-ray normalization, delay and Doppler remain. |
| `--ray-gaussian-width-hz B` | Alternative stationary proper-complex ray gains with a Gaussian target Doppler spectrum of width `B = 2σ` Hz, independent of the ray-center separation. `0` disables this model; positive width and positive OU coherence are mutually exclusive. |
| `--diurnal-phase-radians A`, `--diurnal-period-seconds P` | Add common path phase `A sin(2πt/P + phase0)` after ray composition. `A=0` disables it; `P` defaults to 86,400 seconds. Maximum instantaneous frequency excursion is `A/P` Hz and enters the deterministic capture check. |
| `--outage-seconds START END`, `--outage-gain G`, `--outage-ramp-seconds R` | Attenuate the propagated signal on `[START,END)`, with cosine descent/ascent inside that interval. `G` is an amplitude factor in `[0,1]`; `R` must fit within half the interval. Thermal noise is added later and remains present. Both endpoints `−1` disable the outage. |
| `--path-seed N` | Independent OU/Gaussian-ray and initial-diurnal-phase seed root; an unset root inherits the channel seed. Selected phase is simulation-only metadata. |

Here `t = n/(Fs × RX_clock_scale) − capture_lead_seconds`, not elapsed payload
time after pulse padding. OU state advances at each physical RX sample using
its exact sampled transition; Gaussian FIR history likewise advances through
source silence, outages and read boundaries. Diurnal
phase and outage attenuation precede the existing frequency-selective stage;
its filter state and tails continue, and no second hop-output gate is added.
An outage plateau can therefore retain downstream filter decay.

The Gaussian model is a finite sampled teacher, implemented in
[`triad_gaussian_fading.hpp`](../src/triad_gaussian_fading.hpp). Its two independent
innovation histories are initialized at stationarity. For physical sample
interval `dt = 1/(Fs × RX_clock_scale)`, the symmetric kernel is proportional
to `exp[−(π B dt k)²]` for `|k| <= ceil(5/(π B dt))` and normalized so
`sum h[k]² = 1`. The target continuous covariance is
`exp[−(π B τ)²/2]`; the actual finite covariance at integer lag `m` is
`sum h[k] h[k+m]`. Reports retain the actual kernel and physical timestep as
simulation-only diagnostics. The 8,193-tap workspace/work cap rejects excessive
kernel lengths rather than silently shortening them.

Capture preflight adds `6B` to the required total bandwidth: `3B = 6σ` at
each spectral edge. This is an explicit tail allowance, not compact spectral
support or a guarantee against aliasing. Sampling refinement must check the
finite teacher and waveform together. With both stochastic ray controls off,
the ray gains remain fixed at unity; the legacy samples and RNG sequence are
preserved.

These are explicit stress laws, not calibrated long-wave propagation. Fixed
two-ray separation of 0.001 Hz gives deterministic beating; it does not imply
a 1,000-second random coherence time. OU coherence and Gaussian spectral width
are separately selected alternatives, both with unbounded target spectral tails.
A low sample rate requires physical sampling-refinement evidence; the exact OU
endpoint law or a Gaussian capture allowance alone does not prove that a narrow
capture retains all informative energy.

The isolated source permits integer rates down to 1 sample/s and supported
reciprocal-integer rates below it, with shaped-band and capture guards. Below
64 samples/s the channel rejects FHSS, millisecond
dropout, slow AGC, active sampled frontend, white/flicker/random-walk FM and
white PM. It preserves eligible constant offsets, Wiener phase, fractional
spread and selected selective perturbation. These restrictions avoid silently
omitting requested fast effects; they do not change the ordinary channel's
supported operating range. See the [v3 range and receiver scope](triad-v3.md#reciprocal-sample-rates-and-long-duration-representation).

The isolated `--spread-delay-ms` and `--spread-doppler-hz` controls override
their own nominal physical dimensions before margin is applied once. The
selected preset supplies any dimension not overridden. Without a preset, the
legacy `--channel` values supply the base; with neither preset nor override,
those legacy values retain their already-effective interpretation. The
`spread_inputs` metadata records overrides, resolved values and applied margin.
These controls do not alter receiver hypotheses or scale the separately selected
Gaussian width.

Physical presets do not choose receiver tap hypotheses in `triad_long`. For
the public margin-1 `sub9-best` hypothesis, explicitly use
`--tap-delay-ms .005 --tap-doppler-hz .001` with the coupled two-ray basis.
Preserve different tap values when deliberately testing model mismatch.
The [spread study](validation-data/triad-v3/2026-11-02-spread/README.md) keeps
stable ray separation, stochastic width, delay mismatch and receiver compression
as distinct experimental dimensions.

The isolated stream's optional selective report records pre/post perturbation
signal energy, the actual physical RX clock, component depths/rates, band
geometry, six initial phases and complete processed-row count. It appears only
under `path_truth_simulation_only`; receiver likelihoods must not consume it.
The sums precede receiver oscillator, thermal noise, frontend and erasures and
include processed tails. Active amplitude modulation is not power normalized.
The [selective study](validation-data/triad-v3/2026-11-02-selective/README.md)
separates these energy changes from receiver projection loss and admission.

## Focused evidence and limits

Use the focused selectors that match the changed path:

| Question | Existing evidence |
| --- | --- |
| Preset IDs, units, and parser values | `tuning` ([`tests/test_tuning.cpp`](../tests/test_tuning.cpp)) |
| GUI initial Off/None state, family defaults, margin, selector persistence | `gui_controller` ([`tests/test_gui_controller.cpp`](../tests/test_gui_controller.cpp), focused `--spread-controls` and `--channel-controls` runs) |
| Sampled, bulk, channel chunking, filter rates/phases, delay and FHSS ordering | `channel` ([`tests/test_channel.cpp`](../tests/test_channel.cpp)); `channel_fhss` is its focused FHSS-only selector |
| Live sampled path and actual selected effects | `live_simulation_spread` ([`tests/test_live.cpp`](../tests/test_live.cpp), `--simulation-spread`) |
| Physical output controls, static gain, and DATA/Helper mixing | `live_hardware_channel` ([`tests/test_live_profiles.cpp`](../tests/test_live_profiles.cpp)) and `helper_hardware_channel` ([`tests/test_helper_output_fallback.cpp`](../tests/test_helper_output_fallback.cpp)) |
| Mild spread/modulation through ordinary duplex receiver | `live_duplex_spread` ([`tests/test_live_profiles.cpp`](../tests/test_live_profiles.cpp)); the software loopback decodes all mild cases |
| Production FHSS quiet-interval echoes without a second gate | `live_hardware_fhss` ([`tests/test_live_profiles.cpp`](../tests/test_live_profiles.cpp)) and `helper_hardware_fhss` ([`tests/test_helper_output_fallback.cpp`](../tests/test_helper_output_fallback.cpp)) |

These are software waveform/path checks. They do not establish propagation
calibration, successful receive probability, a radio's analog response, actual
speaker/microphone behavior, or full device throughput. Keep the planner's
probability and compute estimates described as planner estimates; they are not
predictions for these impairments. Separate software evidence from device
measurements in reports.
