# Three-carrier modem v3 study

**Development and integration entry point:** use the
[three-carrier development workflow](triad-development-workflow.md) for efficient
simulation recipes, evidence-backed receiver choices and scope-based validation.
Start with its small diagnostics before reproducing a full dated campaign.

The [noise-calibration and configuration-diversity extension](triad-noise-calibration.md)
documents the common-scale and independent-residual likelihoods, their explicit
noise assumptions, and the tighter clock/oscillator configuration study.

V3 is the separate streaming experiment for one-bit transmissions whose active
span can be hours, days or longer. It does not change the ordinary Robust receiver or
the earlier v1/v2 experimental formats. The implementation is in
[`triad_modem_v3.hpp`](../include/datapump/triad_modem_v3.hpp),
[`triad_statistics.hpp`](../include/datapump/triad_statistics.hpp), and their
`src/` implementations. This page describes the current software and the
assumptions needed to interpret its theoretical bounds; it is not a claim of
validated radio performance. Recorded experiments and evidence limits are in
the [initial v3 validation record](validation-data/triad-v3/2026-10-31/README.md)
and the [state-likelihood study](validation-data/triad-v3/2026-11-01-state/README.md).
The [GPU optimization protocol](triad-gpu-optimization.md) defines the comparison
gates; the [optimization campaign](validation-data/triad-v3/2026-11-01-optimization/README.md)
records its timings, numerical checks and remaining qualification gaps.
The [tensor follow-up](validation-data/triad-v3/2026-11-01-tensor/README.md)
adds optional mixed-product frequency contraction and fused phase advection.
It retains strict reference modes and reports modest gains, accumulated
two-day errors and unqualified operational tails; receiver defaults are unchanged.
The [exhaustive native FP4 campaign](validation-data/triad-v3/2026-11-01-fp4/README.md)
adds tiled candidate/timing/CFO/epoch correlations, sampled quantization diagnostics,
independent seed controls and RTX5090/PRO/MIG measurements. It preserves complete
search accounting and strict references; quantized operational admission remains
unqualified.

The [sub-9-kHz long-duration study](validation-data/triad-v3/2026-11-01-sub9/README.md)
adds low-rate representation, sampled month-long observations, evolving-ray
stress cases and conditional-channel phase-particle likelihoods. Its measured
reception probabilities apply to the declared sampled models and acquisition
conditions; the record separates these from ideal presence-detection bounds.
The [spread-boundary study](validation-data/triad-v3/2026-11-02-spread/README.md)
separates deterministic ray-center changes from stochastic Doppler width and
tests the receiver's block/window approximations under explicit duration limits.
The [selective-modulation study](validation-data/triad-v3/2026-11-02-selective/README.md)
adds paired modulation-depth/rate comparisons, sampled noisy boundary trials,
receiver-prior alternatives and explicit pre/post modulation energy accounting.

Before changing arithmetic or relating these experiments to a precision
dropdown, read the [precision-control experiment guide](gpu-precision-controls.md).
It separates bulk computation, sensitive higher-precision operations, caching
and placement, with measured tradeoffs and unresolved policy decisions.

## Format and streaming state

The configured GUI-style inner Rate maps to `inner_rate = Rate / 6` inner chips
per second per carrier. Added Symbols multiplies that to
`fine_rate = inner_rate × Added`. Three zero-centered complex carriers are
spaced by `1.25 × fine_rate`; the nominal combined occupied width is three
times that spacing. The shared pulse is a unit-energy, roll-off-0.25 root-raised
cosine truncated at eight fine-chip periods on each side. The total triplet
power is divided among the three carriers.

Each fine chip has nonzero nominal amplitude (the three levels are
approximately 0.632, 1.0 and 1.265), an 8-PSK phase, and one of six amplitude
orders. The two bit choices rotate the amplitude order across carriers. Added
Symbols repeats the inner triplet and applies per-carrier keyed QPSK rotations
before whole-cell interleaving. Thus interleave moves complete fine cells and
does not cut a pattern symbol. The sample waveform sums the shaped adjacent
carriers; pulse overlap and carrier summation can still create instantaneous
envelope zeros. This construction is noise-like, but is not a proof of Gaussian
white statistics, low probability of intercept, or equal sensitivity to the
ordinary modem.

The source planner divides the active signal into private tiles made of
400-ms dwells. A tile contains at least 20 dwells and is lengthened as needed
to retain at least 64 complete inner coefficients after pulse-support,
endpoint-margin, and Added Symbols alignment deductions. A requested active
duration is rounded up to complete tiles. `Format` retains the requested duration;
`Geometry` reports resulting duration, nominal payload-energy seconds, sample count, and a
tile-sized workspace bound. Nominal payload energy is a protected cell-count
lower bound before finite pulse shaping and channel loss, not measured
post-channel energy.

`Geometry.calendar_origin_sample` is the public sample coordinate where the
pattern and 400-ms dwell calendar begins, after the pulse-padding halo. The
sampled-channel implementation uses this origin for its TX and RX hop
calendars; acquisition candidates do not recenter it using hidden arrival
truth. `triad_long --fhss` currently sets only the transmit envelope and hop
guards. It does not turn on sampled RF retuning because the CLI does not supply
a hop calendar, so the CLI FHSS cases do not test retuning.

`PatternSource` generates cells by random-access coordinates and caches at
most two tiles. Crypto child contexts are separated by purpose and include the
format epoch, symbol ordinal, and tile index. The payload mask and inner,
outer, cover, and permutation streams use distinct purpose labels. A receiver
candidate with a different public epoch constructs its corresponding private
pattern. An operational transmitter must set the epoch from its system-clock
message time once and keep that address fixed through the bit; receiver clock
updates must not reseed an in-progress message. `--now` selects the current
epoch in the study CLI. The CLI's `--seed`/MT19937 key fixture is reproducible
simulation material, not an operational secret-key generator. Key provisioning
and the key lifetime policy remain external. There is no nonce exchange or
authentication field in this experimental waveform.

The `StreamingChannel` and `StreamingReceiver` consume sequential chunks and
keep channel history, oscillator state, source tiles, QR accumulators, and
scratch within declared bounds that do not grow with observation duration.
The receiver requires contiguous sample coordinates and explicit validity
masks for erasures. A short input remains incomplete; EOF does not turn an
unscored interval into absence. The implementation is a synchronous study
pipeline and does not yet provide a resumable transmission session, persisted
receiver checkpoint, or operator-facing progress service for a multi-day bit.
The initial validation record processed actual sequential sampled observations
up to 172,871.2 seconds (two days) with duration-bounded workspace. One separate
two-day flat-spread, colored-oscillator case processed 11,064,065 samples in
55.308 seconds wall time on the recorded host. These are specific synthetic
model results; four trials per long-duration operating point do not qualify
99% reception, and the single throughput case is not a general performance
guarantee. Multi-day checkpoint/restart and operational recovery remain
unimplemented. See the [validation record](validation-data/triad-v3/2026-10-31/README.md)
for conditions, counts, confidence limits and exclusions.

### Reciprocal sample rates and long-duration representation

The CLI accepts a requested active duration through ten Julian years
(`315,576,000 s`), and the generic planner's default maximum has the same value.
This is an implementation ceiling, not a demonstrated communication duration.
Complete-tile
rounding, pulse padding, burst waits and channel tails can make the actual
elapsed capture longer. Reports distinguish these quantities. Source/sample
addresses and tile crypto addresses use checked 64-bit coordinates; extending
the requested duration does not allocate a full-duration waveform or reseed
its epoch/key-addressed pattern. Range support alone does not establish a
reception probability. The linked sub-9-kHz study records the measured cases;
checkpoint/restart is not implemented.

`Format.sample_rate` is an unsigned 32-bit numerator and
`Format.sample_rate_denominator` is an unsigned 32-bit denominator, defaulting
to one. Supported clocks are integer Hz or reciprocal-integer Hz: a denominator
greater than one requires numerator one. For example,
`--sample-rate 1 --sample-rate-denominator 60` selects one sample every
60 seconds. The effective rate is used throughout source/channel time,
correlation bases, noise variance, capture extent and exports. Manifests retain
`sample_rate_numerator`, `sample_rate_denominator` and effective `sample_rate`.
Reciprocal-clock UTC sample steps use checked integer seconds rather than
accumulated floating-point offsets. Denominator one preserves existing default
seed behavior; matching seeds at different rates still does not couple the
physical stochastic trajectories.

The full positive uint32 denominator range is representable, subject to
waveform bandwidth, capture and coordinate limits. Geometry still
requires `Fs >= 4 × fine_rate` and `Fs > 1.1 × nominal_triplet_bandwidth`, plus
the sampled channel's oscillator/capture checks. The deterministic capture bound
includes the complete declared oscillator/public-frequency envelope, ray
Doppler, drift and diurnal excursion; a narrow payload bandwidth alone does not
make a reciprocal rate valid. An aliased declared capture is rejected. Stochastic
oscillator and OU spectral tails are not a bounded capture guarantee. The CLI's
automatic sample-rate choice is unchanged. Useful initial waveform geometries
with Added Symbols off are:

| GUI-style Rate | Fine rate per carrier | Nominal triplet bandwidth | Explicit sample rate |
| ---: | ---: | ---: | ---: |
| 3.6 Hz | 0.6 Hz | 2.25 Hz | 4 Hz |
| 0.36 Hz | 0.06 Hz | 0.225 Hz | 1 Hz |
| 0.036 Hz | 0.006 Hz | 0.0225 Hz | 1 Hz |
| 0.0036 Hz | 0.0006 Hz | 0.00225 Hz | 1 Hz |
| 0.00036 Hz | 0.00006 Hz | 0.000225 Hz | 1/60 Hz |

These rows are geometry examples, not full-channel sampling qualifications.
Protected tile construction brackets and searches the required dwell count in
logarithmic work rather than stepping through a ten-million-dwell scan limit.
The minimum 64 inner coefficients, complete-cell alignment and bounded tile
workspace still apply; lower rates cannot bypass minimum pattern complexity.

Below 64 samples/s, FHSS, millisecond dwell dropout, active sampled frontend,
slow AGC, white/flicker/random-walk FM and white PM are rejected. Constant
offsets, Wiener phase, fractional two-ray propagation, selected selective
perturbation and the optional path evolution described below remain modeled.
This is a point-sampled physical model, not an established high-rate
integration/decimation equivalent. Compare physical-time-aligned waveforms,
Grams and retained information at finer sampling rates before using it for a
sensitivity claim. Equal RNG seeds at different sample rates do not create
identical stochastic trajectories. The 400-ms source calendar and whole-cell
interleave remain in force; unsupported 10-ms erasures are not silently dropped.

[`tools/triad-sampling-refinement.py`](../tools/triad-sampling-refinement.py)
accepts integer `--sample-rates` and reciprocal `--sample-periods` in seconds.
The selected grid must be distinct and nonoverlapping; its highest effective
rate is the refinement reference. It aligns samples on their exact rational
lattices, including nonnested periods. Outage comparisons use one common
physical span and ramp across rates, increasing the ramp to at least four
samples at the slowest selected rate when necessary. Reports retain requested
and effective outage settings and any adjustment. These remain deterministic
sampled-waveform comparisons, not an analog reference or detection calibration.

The export limits remain explicit: at most 128 million samples and the selected
disk budget (8 GiB by default). They can reject an export even when the bounded
streaming geometry is representable. No public pilot or new repeating pattern
structure is added by this range extension. At very low rates, Doppler sidebands
can overlap adjacent carriers; retain the summed waveform and full Gram matrix
rather than assume independent carrier observations.

Physical channel presets and receiver tap hypotheses are separate controls.
For a matched `sub9-best`, margin-1 experiment, explicitly supply
`--basis two-ray --tap-delay-ms .005 --tap-doppler-hz .001`. Selecting
`--spread-preset sub9-best` alone does **not** replace the receiver's older
HF tap defaults. Deliberate tap mismatch is a separate experiment.

[`tools/triad-lowrate-campaign.py`](../tools/triad-lowrate-campaign.py) generates
independently seeded sampled exports with bounded jobs and cancellation. Its
nominal-only timing bank is a diagnostic, not known alignment or complete
acquisition. Declare the timing/epoch bank and all receiver alternatives used.

## Current receiver and search

For each declared candidate (bit, arrival, frequency, clock, epoch, drift and
channel basis), the receiver synthesizes the actual shaped raw-sample basis.
It appends valid observation rows to a complex QR accumulator, fits the
candidate's linear channel coefficients, and sums explained projection energy
over the scored blocks. It retains residual/input energies, rank, coefficients,
coverage and work counters. No noisy carrier products or per-row powers are
used to form the projection. The same input is evaluated across the finite
candidate bank; the candidate must clear its score threshold and beat the
strongest opposite-bit candidate by a positive margin.

The channel basis uses the declared tap delays and Dopplers. Its fitted complex
tap coefficient is shared by the summed three-carrier waveform, but it is
refitted independently in every scoring block. This is a constrained channel
subspace, not a physically smoothed phase/tap trajectory. The current receiver
does not yet implement a hidden-path estimator, forward/backward phase
smoother, pairwise differential detector, or amplitude-only detector in its C++
streaming path. Separate Python studies compare these families on exported
sampled windows; they are not runtime modes of `StreamingReceiver`. Flat and
coupled two-ray bases can be compared; arbitrary per-carrier tap fits are
explicitly unsupported because they can absorb the pattern. Search is
exhaustive only over the finite bank supplied by the caller, not continuous
timing/frequency or arbitrary channel paths.

The sampled channel starts from the shaped transmit waveform and preserves
state through reads. It supports the shared two-ray `SpreadRay` and
frequency-selective perturbation implementations, oscillator phase/frequency
impairments, clock scales, noise, and explicit erasures. The CLI `--channel hf`
selects a synthetic 1.5-ms / 0.3-Hz two-ray channel with 5% slow and 5% fast
frequency-selective modulation. These are model inputs, not calibrated HF
measurements. `--channel none` is the explicit spread/modulation bypass; other
selected impairments such as oscillator error and AWGN can remain active.
Channel output is not gated again after propagation. The CLI `--fhss` currently
shapes the source envelope only; it does not retune the simulated RF channel or
validate real FHSS hardware. Oscillator white FM, random-walk FM, finite-band
flicker FM, white PM and Wiener phase diffusion are synthetic spectra, not
measurements or a device-specific Allan-deviation model.

### Optional long-span path evolution

`StreamChannelConfig.path_evolution` adds independent stationary proper-complex
OU or Gaussian-spectrum ray gains, common sinusoidal path phase and one finite
signal outage to the isolated streaming channel. Defaults are off and preserve
the earlier sample and RNG sequence. The
[channel-model contract](channel-models.md#isolated-triad-long-span-path-evolution)
defines their units, order, seed and time coordinate. CLI controls are
`--ray-coherence-seconds`, `--ray-gaussian-width-hz`, `--diurnal-phase-radians`,
`--diurnal-period-seconds`, `--outage-seconds START END`, `--outage-gain`,
`--outage-ramp-seconds` and `--path-seed`.

These are independently selected stress models. A 0.001-Hz separation of two
fixed rays describes beating; it does not by itself set an OU coherence time
or a Gaussian Doppler width. The Gaussian width is `B = 2σ` Hz, implemented by
independent stationary finite FIR ray gains, and is mutually exclusive with
positive OU coherence. The teacher reports its actual normalized kernel and
physical RX timestep; its 8,193-tap cap is explicit. The `6B` total capture
allowance reserves six Gaussian standard deviations at each edge, not hard
spectral support. Both stochastic alternatives need sampling refinement, so the
deterministic capture check is not an analog bandwidth guarantee.

`--spread-delay-ms` and `--spread-doppler-hz` instead override nominal physical
ray geometry before one margin multiplication. They leave the public receiver
`--tap-delay-ms` and `--tap-doppler-hz` hypotheses independent; the
[channel-model contract](channel-models.md#isolated-triad-long-span-path-evolution)
also specifies legacy-channel fallback and default-off behavior.
GPSDO presets likewise remain declared
simulation inputs over a long observation, not measured device behavior.
NIST describes methods for evaluating actual GPSDO frequency/time uncertainty
([Lombardi, 2016](https://www.nist.gov/publications/evaluating-frequency-and-time-uncertainty-gps-disciplined-oscillators-and-clocks));
a preset name alone does not supply that evaluation for a month-long link.

The campaign wrapper accepts every shared selective preset: Off and Slow, Fast
or Each at 1, 5, 10, 25 and 65 percent. These are additive amplitude excursions,
without received-energy normalization; Each65 can reverse an individual band's
polarity. The temporal rates are 0.1 and 1 times the effective ray-center
separation, independently of Gaussian width B. A zero separation bypasses this
stage. These periodic excursions retain a coherent mean; their rate is not a
substitute for random phase coherence time.

Active stream reports expose `path_truth_simulation_only.selective_perturbation`:
physical RX sample rate, depths/rates, band geometry, six seeded initial phases,
tail length, processed rows, and pre/post selective signal-energy sums. Phase
origin is capture row zero before its first update. Energy sums include processed
tails and precede receiver oscillator, thermal noise, frontend and erasures;
divide by physical RX sample rate for time-integrated energy. They are diagnostic
truth, never practical receiver inputs. Inactive reports contain only
`{"enabled":false}`. This instrumentation does not normalize samples or consume
additional random numbers.

The selective study found wrong-key admissions when fine-pattern changes were
slow relative to Gaussian ray evolution, despite high correct-key reception.
The calibrated Gaussian-noise admission bound does not cover such signals.
Treat those slower-rate operating points as failed identification controls;
longer transmission or a favorable noise-only test does not establish key or
pattern distinguishability under the fitted channel model.

### Optional sampled converters and interference

`StreamChannelConfig.frontend` adds explicit sampled ADC/DAC and receiver
front-end experiments. Defaults disable these stages and preserve the existing
sample output exactly. The processing order is shaped source → DAC gain,
converter error, dither and quantization → TX oscillator/gain → shared
propagation → RX oscillator → thermal noise and receiver-coordinate
interference → optional analog-filter approximation → existing slow AGC when
selected → gain step/overload recovery → ADC gain, converter error, dither and
quantization → observed erasure mask. Channel, oscillator, converter RNG,
filter and recovery state continue across chunks. Echoes are not gated away at
hop guards; the analog filter adds its relative `1e-6` decay tail.

The isolated CLI exposes these controls:

| Controls | Sampled model |
| --- | --- |
| `--dac-bits N`, `--adc-bits N` | `0` bypasses quantization/error/dither; `2..24` selects signed component quantization, including nominal 16-bit paths. |
| `--dac-full-scale V`, `--adc-full-scale V`, `--dac-gain G`, `--adc-gain G` | Per-real-component full scale and physical input gain; the range is `[-V, V-step]`, with `step = 2V / 2^N`. |
| `--dac-enob B`, `--adc-enob B` | Add synthetic independent Gaussian converter error before quantization. `0` selects nominal resolution; otherwise `1 <= B <= N`. |
| `--dac-dither-lsb RMS`, `--adc-dither-lsb RMS` | Added TPDF dither, with the stated RMS in nominal LSBs per real component, immediately before conversion. |
| `--rx-lowpass-hz HZ` | Continuous one-pole approximation after thermal noise and interference; it also colors that noise. |
| `--interference-tone AMP HZ`, `--tone-every-dwells N` | Receiver-coordinate tone during every Nth receiver-time 400-ms dwell. |
| `--interference-impulse AMP RATE-HZ` | Random-phase, one-sample impulses with occupancy `1-exp(-RATE-HZ/Fs_RX)`, at most one event per sample. |
| `--rx-gain-step TIME FACTOR`, `--overload-recovery-seconds S` | A physical gain change and optional immediate overload attack with exponential gain recovery. |

The ENOB setting models additional component-noise variance
`(step_ENOB² - step_nominal²)/12`; it is not a measured converter ENOB or a
distortion model. Thermal noise can itself provide quantization dither; added
dither contributes further noise. DAC converter noise can occupy shaped quiet
guards while the finite transmitter source exists; an absent-transmitter null
does not inject DAC noise. Full scale and gain determine headroom and clipping.
Gains are not automatically removed at the receiver, and reported thermal
variance remains the pre-frontend input reference. Nominal converter bit depth
does not prescribe DSP arithmetic precision.

These are synthetic component models. A receiver-coordinate tone does not
represent a fixed external RF interferer moving through a retuning front end,
and CLI FHSS remains envelope-only. No physical RF selectivity, retuning or
overload immunity is established. Reports include actual DAC/ADC clipping,
overload and impulse counters and effective frontend parameters.

The current implementation conservatively disables the inherited Gaussian
projection shortcut and analytic admission claim for any active frontend.
Transmit quantization alone differs from quantizing noisy receiver input;
restoring an eligible shortcut requires accounting for the selected processing.
`--mode export` and the duration planner reject active frontend settings;
`--mode export-stats` requires `--stats-noisy` and records actual sampled
observations after the selected processing. `BlockWorkspace.iter_simulator()`
rejects these exports; use `iter_observation()` for the actual noisy projection.
Replacing receiver quantization or analog filtering with unprocessed projected
white Gaussian noise would represent a different physical experiment. The exported no-thermal pass can include
converter noise/interference and is not a Gaussian signal mean. `run` retains
uncalibrated score diagnostics through `uncalibrated_candidate_pass`, with
acceptance disabled until an appropriate admission policy is established.
Neither small numerical differences nor these software paths establish 99%
reception or an operational rare-tail rate.

## Bounds, planner and measurable targets

The default operational target is 99% correct reception and at most `1e-4`
probability of any noise-only admission in 24 hours. The explicitly relaxed
comparison is 90% correct reception and 1% per 12 hours. Both are configurable;
the relaxed target is not substituted silently when the default fails.

[`tools/triad-link-budget.py`](../tools/triad-link-budget.py) supplies a separate
radiated-power budget and known-waveform detection reference. For 1 mW radiated
power and 200 dB complete path loss, received power is `−200 dBm`; no antenna
input-power or efficiency correction is applied. At 38 days and a useful
full-power energy fraction of one, the explicit noise-density comparison is:

| Receiver noise density | Total-triplet C/N₀ | Nominal available E/N₀ |
| ---: | ---: | ---: |
| −164 dBm/Hz | −36 dB-Hz | 29.163 dB |
| −154 dBm/Hz | −46 dB-Hz | 19.163 dB |
| −144 dBm/Hz | −56 dB-Hz | 9.163 dB |

These energies use `E/N₀ = (C/N₀) × useful_seconds`; they do not predict
unknown-channel reception. The script exposes the useful-energy fraction,
noise density and family counts. Its default expanded study family is
`K=11` methods, `D=4` durations and two bit hypotheses, with one scheduled
attempt per minute and half the false-admission budget allocated to scores.
`--attempt-spacing-seconds` changes that declared schedule for both strict and
relaxed calculations; its default remains 60 seconds. A spacing at least as long
as the corresponding horizon means one scheduled attempt. Fewer attempts reduce
the family-allocation penalty only by restricting operation; they are not a
receiver sensitivity gain. The bound does not establish optimal calibration of
correlated repeated windows or arbitrary adaptive monitoring.
This differs from the earlier measured `K=9/D=3` family. Its inverse duration
assumes a completely known waveform, phase, alignment and channel; it is not
the practical decoder's duration planner. Raising the generic planner's range
does not establish retained-energy or channel-uncertainty bounds for the new
path models.

No noise density in this table is asserted to be typical below 9 kHz.
[ITU-R P.372-18](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.372-18-202609-I!!SUM-HTM-E.htm)
includes atmospheric and man-made radio noise as well as other sources.
Site, time and receive-system noise measurements remain necessary for a
physical link prediction; the current Gaussian null does not cover impulsive
or colored environmental interference merely by changing `C/N₀`.

These comparisons follow standard matched-subspace and separable least-squares
results. Scharf and Friedlander derive finite-sample GLRT distributions and
invariance results for specified Gaussian subspace models
([paper](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf)).
O'Leary and Rust describe eliminating linear parameters by variable projection
while optimizing nonlinear parameters, and discuss numerical diagnostics and
local-minimum risks ([NIST report](https://nvlpubs.nist.gov/nistpubs/ir/2012/nist.ir.7856.pdf)).
Divsalar and Simon analyze joint multiple-symbol differential detection for
uncoded MPSK ([NASA record](https://ntrs.nasa.gov/citations/19900046187));
Rogstad derives squaring loss for a cross-correlation tracking loop in
suppressed-carrier arraying ([NASA record](https://ntrs.nasa.gov/citations/19920005021)).
These sources motivate the benchmark and alternatives, but their specific
signal/channel models do not establish triad performance.

For two fully known sampled hypotheses `s0` and `s1` in complex AWGN with
`E|n[k]|² = σ²`, the equal-prior optimum binary error is

`P(error) = Q(sqrt(||s1 - s0||² / (2 σ²)))`.

The statistic helper implements this bound. Compute the distance from the
actual shaped, channel-affected sampled waveforms and state the synchronization
and channel knowledge used. This is the known-channel benchmark; it is not an
acquisition result.

For a fixed, whitened complex basis of rank `r`, the projection energy
`E = ||Qᴴ y||² / σ²` has a `Gamma(r, 1)` null law under fixed known white
Gaussian noise. Under a signal mean `μ`, it has noncentrality
`λ = ||Qᴴ μ||² / σ²`. The implementation computes blockwise QR projections;
the rank is measured from each basis after masking missing rows. A union bound
allocates the declared horizon false-admission budget across the complete
fixed candidate bank and repeated starts. The exact numerical Gamma quantile
is preferred when it converges; otherwise the code reports its conservative
concentration bound. Neither calculation alone validates Gaussian noise,
fixed pre-noise hypotheses, missingness independence, or a real channel model.

The duration planner uses `C/N₀` in dB-Hz and the total nominal triplet power
reference. In the CLI's unit-power reference, complex noise variance is
`effective_sample_rate / 10^(C/N₀/10)`. Its conditional model calculates usable time after
startup, channel outage, and retained-energy fractions; projection rank from
the number and rank of coherence blocks; a finite-family null-score upper
bound; a noncentral true-score lower bound; and a wrong-bit upper bound using
the declared maximum wrong-projection fraction. It tests complete source-tile durations on a
whole-dwell calendar, including silent burst waits. The generic API defaults
to a 400-ms quantum; the CLI supplies the actual tile quantum and offset.
Minimum duration is at least 8 seconds, 20 dwells, and 64 inner coefficients.
The planner reports elapsed duration and the corresponding active-duration
input for the transmitter separately. It also estimates operations and tiled workspace. These resource
estimates use a configured operation count per sample and hypothesis; they are
not measured throughput predictions.

The default ten-Julian-year horizon does not force a scan of every possible
duration before returning an early solution. The ordered exhaustive search
charges its cap to durations actually evaluated and permits at most 2,000,001
evaluations. If that budget is exhausted with unvisited durations remaining, it
throws `duration search exhausted bounded planner grid; unvisited durations remain`;
it does not report that no feasible duration exists. Rank changes are not assumed
to make the separation margin monotone, so no binary-search shortcut silently
skips duration alternatives.

The planner's success status is conditional on caller-supplied bounds for
retained energy, wrong-codeword projection, outage, and an equal-rank fixed
candidate bank. `--assume-model-bounds` marks those assumptions as supported
for calculation; it does not supply evidence for them. The default
wrong-projection fraction is 0.08 and is not measured. Treat a conditional plan
as a model calculation until independent channel/codeword evidence supports
each bound. Unbounded outages or unmodeled channel changes cannot have a finite
duration guarantee.

The finite-horizon false-admission budget covers the hypothesis count,
attempt spacing, and `--duration-alternatives` supplied to the planner. Count
every separately attempted duration in that option. This controls the analytic
union bound only when the declared family covers the operational schedule.
Overlapping windows need not be independent for the union bound, but empirical
calibration must recreate their shared noise and the real schedule. For a
zero-event campaign with `N` independent horizons, the one-sided 95% upper
probability bound is
`1 - 0.05^(1/N)`; a small zero-count test cannot certify a tiny operational
false-admission probability.

The generic planner rank is currently calculated as the number of coherence
blocks times `rank_per_block`. The receiver reports its actual rank per
candidate after applying each raw-sample block boundary and erasure mask;
silent rows and rank-deficient blocks can reduce that value. Real observations
can therefore differ from the planner's fixed-rank abstraction. Validate the
rank distribution and score calibration over candidate arrival offsets,
duration boundaries, and dropout patterns before treating the duration result
as a bound for the receiver.

### Separate coherent, phase-unknown, and operational bounds

The known-channel binary formula above assumes the phase of each candidate is
known. Its gap to a phase-unknown reference can be calculated for an explicit
simple case. Let the two candidate waveforms have equal energy `E σ²`, zero
normalized cross-correlation, and known alignment; let the unknown carrier
phase be constant over the observation. With known phase, coherent binary
detection has `P(error) = Q(sqrt(E))`. With unknown phase, comparing the two
orthogonal projection energies has
`P(error) = 0.5 exp(-E/2)`. These are oracle-channel binary tests, not the v3
receiver's acquired or channel-impaired performance.

| Target binary error | Known phase `E` | Unknown constant phase `E` | Intrinsic gap |
| --- | ---: | ---: | ---: |
| 1% | 5.4119 (7.33 dB) | 7.8240 (8.93 dB) | 1.60 dB |
| 10% | 1.6424 (2.16 dB) | 3.2189 (5.08 dB) | 2.92 dB |

Therefore, for this equal-energy orthogonal reference, a universal end-to-end
target of at most 1 dB above the coherent known-phase bound is unattainable
when phase is unknown, even before timing search, channel uncertainty, or
operational false-alarm control. The proposed **1 dB implementation target
belongs against the matched reference with the same unknown-phase/channel
model and rank**, not against the more favorable coherent oracle. The actual
triad codewords need not be orthogonal, and the receiver's independently
fitted block coefficients are a different nuisance model; measure its own
Gram matrix and reference curves before applying these numbers.

Binary error alone also does not set an admission threshold: it has no
noise-only family or repeated-search false-admission constraint. One
checkable known-mean presence reference uses fixed candidate `s_b` and
`y = s_b + n` under the signal hypothesis, with complex noise
`n ~ CN(0, σ² I)`. Define

`g_b = Re(s_bᴴ y) / (σ ||s_b||)`.

Under no signal, `g_b ~ N(0, 1/2)`. For `M` predeclared tests in the entire
finite operational family and horizon false-admission budget `α`, a
Bonferroni threshold

`τ = Q⁻¹(α/M) / sqrt(2)`

bounds the chance that any fixed candidate passes under noise by `α`, without
assuming independent tests. If the true known-mean candidate must pass with
probability at least `1 - β_pass`, its normalized energy must satisfy

`E >= 0.5 [Q⁻¹(α/M) + Q⁻¹(β_pass)]²`.

This is only a true-candidate pass bound. Correct admission also requires
controlling selection of a wrong-bit candidate. For fixed equal-energy
known-mean candidates, let `ρ_j` be the normalized complex inner product
between the true waveform and wrong candidate `j`. The pairwise probability
that `j` scores above the true candidate is
`Q(sqrt(E [1 - Re(ρ_j)]))`; summing these probabilities over wrong-bit
candidates gives a conservative union bound. Allocate the total miss budget
between true-candidate non-admission and wrong-bit selection. This expression
does not apply unchanged to fitted unknown-channel likelihoods; compute the
fixed-rank reference there under its own likelihood and complete search bank.

Use these explicit comparisons as acceptance targets, then report actual gaps:

1. The fixed-alignment, known-channel receiver should be within **1 dB** of the
   sampled-waveform binary benchmark at the same error probability.
2. The fixed-rank receiver should be within **1 dB** of its declared
   matched-subspace model benchmark at the same finite-family false-admission
   probability. Establish this with independent noise realizations and a
   tractable exhaustive reference bank.
3. Measure the additional loss from unknown channel, then timing/epoch/frequency
   acquisition, against those preceding references. No acquisition-loss target
   is yet demonstrated; report each stage in dB and preserve wrong accepts,
   misses, and null admissions separately.
4. Compare detections for block-first joint reception against product-first
   and amplitude-only alternatives, stating their different nuisance models
   and computational costs. The exported-window Python comparison implements
   these alternatives. The earlier multiple-symbol differential and cross-correlation
   results motivate this comparison, but do not provide a performance number
   for the triad.

Under a known coherent channel, collected energy is proportional to
`(C/N₀) T`, so a fixed error target has `T ∝ (C/N₀)^−1`. With independent
unknown channel coefficients re-fit in fixed-length blocks, the projection
rank and null fluctuation grow with block count; at weak per-block SNR, a fixed
separation target can instead require approximately
`T ∝ (C/N₀)^−2`, with additional dependence on block length and codeword
projection separation. This is a scaling argument for the stated Gaussian,
fixed-block model, not a measured v3 law. Estimate the exponent from duration
sweeps before extrapolating. Product-first energy-combination receivers can
scale worse when each segment has weak product evidence; derive that specific
statistic's mean and variance rather than assign one universal exponent.

## Exported-window likelihood experiments

The C++ `export-stats` mode creates a separate sampled-waveform fixture for
Python receiver experiments. It uses the same shaped triad, public candidate
basis, sampled channel and validity calendar as the C++ study, then writes the
full Hermitian block Gram matrix `G_raw = Aᴴ W A`, the simulator's signal
projection `m = Aᴴ W s`, and block intervals/valid-row counts. `W` selects valid sample
rows. `--stats-noisy` additionally writes `Aᴴ W y` from an independently
replayed noisy channel pass. These are sufficient-statistic experiments over
the exported linear model; they are neither modes of the C++
`StreamingReceiver` nor integrated with the ordinary Robust receiver.

The Python workspace uses `σ² = Fs / 10^(C/N₀/10)` and presents detectors with
`r = Aᴴ W y / σ²` and `G = G_raw / σ²`; under the declared white-noise model,
`r | H0 ~ CN(0,G)`. It samples noise using the **full** Gram matrix, retaining
cross-candidate and cross-tap covariance. It does not treat candidate scores as
independent. The public iterator exposes raw Grams and the observation calendar;
the detector-facing iterators supply normalized `r`, `G`, valid-row counts,
and physical elapsed time. Exported means, signal energies and selected truth
fields are simulation-only and must not be supplied as receiver metadata.
The optional actual noisy projection becomes ordinary normalized observation
input through its separate iterator. The finite Gaussian
likelihood assumes known white-noise variance; uncertainty or estimation of
`σ²`, colored noise and non-Gaussian interference are outside that likelihood.

The `gaussian` method integrates unknown proper complex channel coefficients
under a specified static or OU prior, using a bounded-memory forward Gaussian
state update. The OU correlation time, stationary tap powers and rotation are
fixed before looking at observations; taps already represented in `A` must
not be counted again in the prior. Singular public Grams are supported without
forming an inverse. The likelihood is exact for this declared Gaussian model,
not for arbitrary constant-modulus oscillator or propagation processes. It
does not implement optional stopping or an automatic anytime decision rule.
The `phase-frequency` method instead integrates a finite oscillator-state
prior, initial phase, positive global-amplitude quadrature and static relative
ray-phase quadrature. It is exact only for that finite discretized model.
`block-energy` is a separate fixed-rank Gaussian projection detector; its
nearly singular whitening and sensitive reductions use FP64. The Gaussian and
phase/frequency bulk may use FP32 or FP64, but their accumulated log evidence is
FP64; the FP32 CUDA path disables TF32 and lower-precision matrix multiply.
FP64 reference comparisons remain important near an admission threshold.

### Windowed Gaussian Doppler likelihood

[`triad_gaussian_window.py`](../tools/triad_gaussian_window.py) prepares a
Gaussian coefficient prior across physical block centers within each fixed,
nonoverlapping window. Its covariance is `exp[−(π B Δt)²/2]` times each tap's
declared stationary power; ray Doppler already represented in the sampled
basis is not added again. PSD square-root/eigenvalue preparation supports
rank-deficient Grams without inverting a nearly singular channel covariance.
Prepared factors are reused across observations and stationary residual-noise
upper bounds, then scored with batched complex matrix products.

Independent windows and block-center coefficients define the receiver's
alternative model. The sampled Gaussian FIR teacher can evolve within a block
and remain correlated across windows. Shorter-block and longer-window checks
must measure those losses; a normalized likelihood is not evidence that the
alternative matches the physical channel. This method has no additional
residual-CFO search beyond the exported candidate bank.

[`triad-conditional-sweep.py`](../tools/triad-conditional-sweep.py) compares
`static-fft-ucb`, `gaussian-ou-ucb`, `cfo-ou-ucb` and `gaussian-window-ucb` on
common observations. Conditional mode draws full-bank correlations and noisy
total energy jointly from each sampled path; repetitions vary thermal noise,
not the physical path. `--mode actual-noisy` instead reads the actual noisy
correlations and received energy, with one observed trial per path and no
C/N₀ injection or sweep. Simulation means, signal energies and selected truth
are excluded from receiver scoring. See the
[noise-calibration assumptions](triad-noise-calibration.md#gaussian-spectrum-windows-and-paired-sweeps)
for the stationary full-bank UCB and configurable `K=16/D=8` family accounting.

Gaussian-window preparation and evidence use FP64; optional FP32 applies to
the bulk correlation/transform products, with TF32 disabled. The retained
factor/cache sizes scale with observation blocks and have explicit memory
preflight. This is a reusable exported-workspace receiver, not a new bounded
C++ streaming mode, an anytime stopping rule or an automatic duration inverse.

### Conditional Gaussian channel with phase particles

[`triad_rbpf.py`](../tools/triad_rbpf.py) adds a bounded Rao–Blackwellized
phase/frequency filter. Each particle represents a common oscillator trajectory
and a conditional complex channel mean. Given its phase `φ`, the Gaussian
update uses `exp(−jφ) r` with unchanged `G`; covariance, factorization and gain
preparation are shared across particles. The channel coefficients apply to
the complete summed three-carrier basis. This retains complex accumulation
before likelihood multiplication and does not treat carrier pairs as
independent evidence.

The declared oscillator prior has a uniform initial CFO interval, optional
Wiener phase and optional Brownian frequency. Frequency/phase increments use
the jointly integrated Brownian transition. The initial CFO interval is not a
hard bound on later random-walk trajectories. A circular Gaussian tap prior
already marginalizes the constant common phase, so the particle phase starts
at zero. Tap OU coherence and powers are independent prior parameters; do not
duplicate a Doppler already represented in the public waveform basis.

With resampling off, the score is a normalized mixture over a predeclared
independent prior-path bank. With multinomial or systematic resampling, the
likelihood estimate is nonnegative and unbiased over independent algorithm
randomness under its declared model; its logarithm is not an unbiased log
likelihood. A Markov null bound for adaptive resampling is joint over noise
and algorithm randomness, not conditional on every fixed adaptive seed.
Never select the best particle or seed while keeping the mixture's admission
claim. Particle-count, block-length and sampling refinement, including
effective-sample-size depletion, remain necessary sensitivity checks.

The core also supports optional `frequency_initialization="stratified"`:
one independent uniform jitter in each equal-width CFO stratum, with equal
weights. It preserves the continuous prior in expectation and can reduce
initial-frequency integration variance. IID initialization remains the default.
[`triad-rbpf-convergence.py`](../tools/triad-rbpf-convergence.py) compares both
choices on identical observations and paired algorithm randomness. Its
replicas are numerical diagnostics, not additional independent transmissions;
it reports likelihood averages and variability without selecting a best seed
or qualifying an additional receiver family.

The optional `frequency_initialization="midpoint-grid"` instead assigns equal
mass to deterministic CFO-stratum midpoints. It declares a normalized discrete
CFO prior, not an unbiased estimate of a continuous uniform prior. The core and
adapter require zero oscillator phase diffusion, zero frequency random walk
and no resampling for this mode. Gaussian channel taps may still evolve with
the declared OU coherence; a circular tap prior marginalizes constant initial
phase. There is no implied phase-tracking capability, and grid/prior refinement
must test mismatch separately. Any additionally selected grid or prior belongs
in the predeclared receiver family.

[`triad-rbpf-study.py`](../tools/triad-rbpf-study.py) consumes actual noisy
exported correlations, public full-bank Grams, received energy and physical
intervals. `rbpf` requires a correct caller-declared white Gaussian noise
variance; `rbpf-ucb` uses a pooled upper confidence bound from residuals
orthogonal to the entire scored bank and assumes one constant unknown white
proper-complex noise variance. Hidden signal means/energies and selected
simulation truth do not enter scoring. Finite acquisition alternatives are
normalized mixtures within each bit. The default `K=11/D=4` family includes
these two methods; any further selected model, duration or receiver must be
accounted for. Neither mode is an anytime stopping rule or a duration inverse.

The adapter approximates common phase as constant inside each exported block,
then advances state by physical block-center separation through erasures and
the partial final block. It processes independent workspaces serially and
batches candidates and particles within each filter. FP32 uses complex64 bulk
particle means/products, with FP64 covariance solves, oscillator state and
evidence reductions; FP64 widens the bulk too. TF32 is disabled. Report the
memory estimate separately from framework/cache overhead and measured device
usage. [`triad-rbpf-benchmark.py`](../tools/triad-rbpf-benchmark.py) compares
identical predeclared phase banks across CPU/GPU and precisions; a kernel
timing does not establish end-to-end sampled-receiver acceleration.

[`triad-cfo-batch-study.py`](../tools/triad-cfo-batch-study.py) provides a separate
actual-observation adapter for that deterministic model, reporting the method
as `cfo-grid-ucb`. It prepares each path's full-bank residual noise upper bound,
then retains the projected correlations and Grams for a resident batch across
paths, candidates and CFO grid points. It requires actual observations and
received energies, compatible geometry and relative candidate columns, and
identical physical block calendars; it does not silently align unlike captures.
Absolute epochs and keys may differ. Simulation means, true noise variance and
selected physical truth do not enter scoring.

The batch size defaults to eight (supported range 1–256), with explicit array
memory preflight; CUDA use is capped at 75% of reported free GPU memory as well
as the requested budget. Retained batch data grows with blocks and must fit
that budget. This is distinct from the duration-bounded C++ streaming receiver.
FP32 selects complex64 bulk particle products; full-bank residual projection,
Grams, covariance preparation and sensitive phase/evidence reductions remain
FP64, and TF32 is disabled. Reports separate preparation, transfer, resident
inference, joint wall time, array bounds and device allocation. CPU/GPU code
paths are implemented; acceleration for this new batch adapter is not yet
measured or qualified here.

The adapter defaults to an expanded configurable `K=16/D=8` selection family,
not the older `K=11/D=4` campaign. Its noise bound assumes one stationary white
proper-complex variance per path and independent whole-bank residual
calibration. Wrong-key rejection is an empirical control, not covered by the
noise-only false-admission theorem. It remains a fixed-window detector, not an
anytime rule or a validated duration planner.

The conditional-channel strategy has prior communication-system research,
including [Kim, Pun and Iltis, MERL TR2010-095](https://www.merl.com/publications/docs/TR2010-095.pdf).
That work studies CFO-conditioned channel estimation in MIMO-OFDMA; its
pilot-assisted/semi-blind results are motivation, not performance evidence for
this keyed one-way triad waveform.

Likelihood mixtures and bank maxima have different null calibration. A
normalized prior mixture retains its declared probability weights; under its
specified known-variance null it is a nonnegative likelihood ratio with unit
mean, which supports a Markov threshold for a fixed test. The current scripts
allocate the family budget across scheduled attempts, duration alternatives
and receiver-selection methods, and apply the corresponding log threshold to
each bit-mixture score. `block-energy` instead takes a maximum over candidate
scores and accounts for the candidate count in its family threshold. These
analytic allocations require the deployed family and schedule to be declared;
the union bound remains valid for overlapping, dependent windows. These tests
do not empirically reproduce a whole operational schedule or validate a
mismatched noise null. Calibrate the complete operational schedule and any adaptive
method/duration choice on held-out null data. Do not tune and report a threshold
on the same holdout. Any separately selected acquisition bank or
receiver must be included in the family count.

The workspace iterators carry absolute sample intervals, block centers and
physical `dt` across tiles. Detector state is updated through missing or
zero-energy blocks so oscillator/channel evolution is not restarted at a file
or shard boundary. Independent path batches derive stable thermal-noise seeds
from the root seed, split, channel/key seeds and epoch, rather than shard
position; null and signal projections on each path are paired with that same
noise seed. Keep development, calibration and holdout seed domains separate.
Each exported path keeps several memory maps open. The path runner checks a
soft file-descriptor limit of at least `6N + 128` for `N` paths: for 300 paths,
raise it within the host hard limit before launch, for example with
`ulimit -n 4096`, or use smaller batches. This is a process-resource
requirement, not a C++ runtime setting.

Finite oscillator grids need convergence checks against the continuous
physical model. In the current phase-prior diagnostic, the default OU settings
(`σ_f = 0.003 Hz`, `τ = 1000 s`) have continuous phase correlation about
`6.57e-18` at 512 s, while a 256-state phase model with its coarse aligned
frequency grid has a spurious correlation revival of `0.10424`; 512 phase
states remove it at that evaluated lag. The slower `σ_f = 0.002 Hz`,
`τ = 4096 s` case needs 1024 states to meet the same declared lag checks.
These finite lag diagnostics do not demonstrate continuous-model convergence
or reception. Keep phase-state and frequency-grid refinement separate, and
compare detector rankings and outcomes as well as prior correlations before
using sensitivity results. A finite-grid likelihood can be computationally
exact while representing the wrong long-term oscillator behavior.

`tools/triad_gaussian_bounds.py` evaluates a complete exported design using
public Grams only. For a single specified Gaussian tap prior, it computes
the exact model log-LR mean and variance, a positive-scale Chernoff miss bound,
and Bhattacharyya bounds against wrong bit/epoch candidates. Its fixed bank
maximizes individual Gaussian likelihoods with a union-calibrated gate. It
does not certify the multi-coherence mixture or a physical channel merely
because the waveform design is shared. It reports whether that complete
duration meets the requested conditional reception bound; it does not
extrapolate duration or declare a truncated interleave tile complete. Numerical
FP64 evaluation is not interval-certified rounding. Failed conservative bounds
are not proofs that reception is impossible.

Optional `--null-calibration gaussian-chernoff` uses fixed-design fractional
Gaussian likelihood moments to tighten that individual-bank null gate. The
default remains `markov`. The tighter gate is not valid for the separate
multi-coherence mixture without a corresponding mixture calibration.

One local development check at `−24 dB-Hz` used a single two-day sampled
physical path: the Gaussian method accepted 32/32 conditionally generated
thermal-noise trials and the separately retained actual noisy projection
accepted 1/1; block-energy accepted 0/32. This is evidence for that one path
and declared Gaussian prior only, not a path-diversity estimate or a 99%
reliability result. The detailed export provenance, conditions, and limits are
recorded in the [state-study report](validation-data/triad-v3/2026-11-01-state/README.md).

These scripts have optional Python dependencies (`numpy`, `scipy`, and
`torch`); they are not dependencies of the ordinary CMake build or receiver.
The C++ exporter remains available from the isolated study build. For example,
after building that target, a short fixture and conditional receiver study can
be run locally as follows:

```sh
build/dev/triad_long --mode export-stats --seconds 16 --channel hf \
  --basis two-ray --block-seconds 4 --signal-bit 0 --seed 7101 \
  --key-seed 7101 --channel-seed 9101 --output /tmp/triad-stats-0

python3 tools/triad-state-study.py /tmp/triad-stats-0 \
  --output /tmp/triad-state-development.json --cn0 -24 \
  --trials 32 --split development --methods gaussian block-energy
```

For path-diversity work, create independent exports with unique channel and key
seeds, balance the transmitted bit, and pass the complete batch to
`tools/triad-path-study.py`. Its outcomes and confidence intervals use paths,
not conditional noise replications, as independent trials. Keep `--actual-observation`
for a separately sampled noisy projection only when each export was created
with `--stats-noisy`; it must match that export's physical C/N₀. The
`tools/triad-state-campaign.py` helper can generate provenance-tagged C++
exports and shard independent path ranges. All report claims should state
whether they use conditional noise replicas, actual noisy projections, or
independent sampled paths.

## Running the isolated study

Build and run only the isolated triad group; it does not invoke the ordinary
Robust receiver regression campaign:

```sh
CCACHE_DISABLE=1 ./build.sh test triad --jobs 2
build/dev/triad_long --help
```

The executable provides `--mode plan|run|export|export-stats`:

```sh
# Baseline duration calculation; absent certificates, status stays unsupported.
build/dev/triad_long --mode plan --cn0 8 --correct .99 --false 1e-4 \
  --horizon 86400 --attempt-spacing 60 --duration-alternatives 3 --basis flat

# Explicitly assume the input channel/codeword bounds for a conditional result.
# This switch does not validate those assumptions.
build/dev/triad_long --mode plan --cn0 8 --correct .99 --false 1e-4 \
  --horizon 86400 --attempt-spacing 60 --duration-alternatives 3 \
  --basis flat --assume-model-bounds

# A sampled 60-second run with public timing/frequency grids and two-ray basis.
build/dev/triad_long --mode run --seconds 60 --cn0 8 --channel hf \
  --basis two-ray --start -0.5 0.5 0.1 --frequency -0.2 0.2 0.05 \
  --block-seconds 1.6 --trials 8

# Export a fixed raw-waveform candidate bank and one noiseless sampled signal.
build/dev/triad_long --mode export --seconds 16 --channel none \
  --phase-diffusion 0 --output /tmp/triad-v3-basis

# Optional CUDA batched projection/calibration on the exported bank.
python3 tools/triad-gpu-study.py /tmp/triad-v3-basis \
  --device cuda:0 --trials 128 --attempts 1 --false-alarm-budget .01 \
  --benchmark --output /tmp/triad-v3-gpu.jsonl
```

`run` reports correct accepts, wrong accepts, misses, null/control admissions,
thresholds, coverage, memory, basis-value counts, CPU and wall time. The C++ API
also exposes per-candidate ranks and further work counters. `plan`
reports its model assumptions and conditional status. `export` writes candidate
bases and a sampled signal without AWGN, subject to the 128-million-sample and
8-GiB default export limits. The Python CUDA tool performs batched
matched-subspace projections of those fixed candidate columns and can compare
complex64 device results with a complex128 CPU reference. It disables TF32 and
reports ranking/admission deltas. This is an independent raw-waveform study,
not execution of `StreamingReceiver`, not a validated duration planner, and not
a demonstration of end-to-end modem acceleration. Multiple GPUs require
separate shard processes with distinct `--device`, `--shard-index`, and a
shared `--shard-count`; the script does not provision cloud resources. The
export manifest does not currently bind itself to a source revision hash;
record the exact revision and hash the exported files with any reproducible
GPU result.

Keep command lines, seeds, full search dimensions, device and
precision, independent calibration/holdout split, measured runtime and cleanup
status with any GPU results. Do not treat a GPU kernel timing as end-to-end
streaming throughput, or synthetic sampled channels as physical link
measurements. The large daily false-admission campaign qualifies only its fixed
Gaussian bank and schedule; it does not establish a general operational rate.
The state study has pointwise 300/300 two-day successes for a selected,
known-alignment HF model and known white-noise variance. It does not establish
a general 99% multi-day operating envelope, real RF retuning, device oscillator
behavior, or public LPI characteristics. A measured noise-variance mismatch
test demonstrates that the known-noise assumption is operationally material;
variance-robust calibration remains required.

Additional experimental tools deliberately preserve their narrower scopes:

- `triad-sufficient-study.py` samples the full correlated Gaussian projection
  vector for disjoint scheduled windows. It permits large daily-tail studies
  without constructing every noise sample, and reports covariance accuracy.
- `triad-detector-study.py` compares all three carrier products, block-first
  shared/per-carrier fits, amplitude-only power fits and flat/two-ray GLRTs on
  the same samples. The amplitude-only result is a separate Python experiment;
  it is not a C++ streaming mode or a Rice-likelihood implementation.
- `triad-phase-study.py` preserves complex block correlations and integrates a
  normalized finite-grid phase prior. It compares phase-grid resolutions and
  raw-noise versus sufficient-statistic simulations. The phase model assumes
  known unit gain and scalar Wiener diffusion; colored-noise mismatch requires
  an explicit flag. It is not a joint unknown-multipath smoother.
- `triad-fft-acquire.py` caches template FFTs and exhaustively scores its declared
  timing/frequency/epoch/bit bank. It checks selected, boundary and random
  points against CPU FP64 sums. Its full-window shared-gain model does not
  track stochastic phase or changing multipath.

These are separately budgeted receivers, not an automatically selected or
OR-combined collection. Selecting among their outcomes requires additional
model/duration-family accounting. See the validation record for actual
measurements and remaining benchmark gaps.

## Joint common and relative ray-frequency likelihood

[`triad-ray-frequency-study.py`](../tools/triad-ray-frequency-study.py) consumes
actual noisy block statistics with public two-ray candidate mappings for both
bits. Its retained model is `A0 exp(j 2π f t_b)` and
`A1 exp(j 2π (f + δ) t_b)`, where `t_b` is the physical block center, `f` is
common CFO and `δ` is a constant relative ray-Doppler correction. The public
tap delays remain fixed. Each candidate has two static proper-complex Gaussian
coefficients of variance 0.5; the likelihood integrates those coefficients
jointly. It does not track OU ray evolution or arbitrary intra-block phase.
Input candidate metadata must describe the intended public two-ray basis;
this adapter does not infer a physical tap model from hidden simulation truth.

Complex correlations accumulate in their original ray-column coordinates
before the Gaussian coefficient integral. The per-ray frequency transforms
precede any summed-Gram whitening that would mix differently rotating columns.
The off-diagonal Gram is also frequency dependent:
`G01(δ) = Σ_b G01,b exp(+j 2π δ t_b)`.
Its positive exponent requires negative-index FFT bins relative to the
correlation transform convention. Dropping that cross-Gram or changing its
sign changes the joint likelihood. The combined outward-rounded `f + δ` grid
must remain strictly below block-center Nyquist; this guard is additional to
the sampled-waveform capture checks.

Defaults are `--frequency-bound 1e-6`, `--ray-frequency-bound 2e-7` and maximum
common FFT-lattice spacing `1/(8T)`. Common CFO has a uniform finite-grid prior.
`--zero-ray-mass .5` places half the relative-frequency prior at `δ=0` and
spreads the remainder uniformly over the finite relative grid, including zero.
With the same common grid, coefficient model and observations, this guarantees
`logLR_joint >= logLR_zero_relative - ln(2)`. It is a likelihood inequality,
not a sensitivity penalty in dB or a comparison against a different older FFT
grid. Outward endpoints and the default spacing are recorded conventions, not
certified shaped-waveform mismatch bounds. Any selected grid, prior or model
alternative requires family accounting.

`ray-frequency` requires a correct declared white-noise variance.
`ray-frequency-ucb` uses independent full-bank residual calibration for one
stationary white proper-complex noise variance. Both use actual observations,
received energies, physical intervals and the complete bank Gram; neither uses
the hidden signal mean or true channel to score candidates. The default
selection family is `K=16/D=8`, with both candidate bits accounted for. The
noise-only bound does not qualify wrong-key signals or adaptive monitoring.

FP32 selects complex64 correlation FFTs. Cross-Gram FFTs, Gram diagonals,
residual/rank calculations, small Cholesky/triangular solves, quadratics and
mixture reductions remain FP64; TF32 is disabled. Reports separately time the
correlation FFT, FP64 Gram FFT/diagonal stage, FP64 solves/mixtures and CPU
projection preparation. Frequency pairs are tiled under an explicit memory
bound, with CUDA arrays also capped at 75% of reported free device memory;
framework overhead is separate. Seven focused [direct-oracle tests](../tests/test_triad_ray_frequency.py)
passed for the reviewed implementation. Actual GPU campaigns remain separate:
this contract makes no acceleration, sensitivity or general optimality claim.

Measured results are recorded in the [−60 dB-Hz campaign](validation-data/triad-v3/2026-11-02-minus60/README.md):
five frozen long-span holdouts, joint-ray mismatch diagnostics, actual CPU/GPU
timings, precision comparisons and retained evidence audits. Consult its exact
channel, acquisition, energy and confidence assumptions before using those
results as duration-planning evidence.
