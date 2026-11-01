# Three-carrier modem v3 study

V3 is the separate streaming experiment for one-bit transmissions whose active
span can be hours or days. It does not change the ordinary Robust receiver or
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
The maintained study has processed actual sequential sampled observations up
to 172,871.2 seconds (two days) with duration-bounded workspace. One separate
two-day flat-spread, colored-oscillator case processed 11,064,065 samples in
55.308 seconds wall time on the recorded host. These are specific synthetic
model results; four trials per long-duration operating point do not qualify
99% reception, and the single throughput case is not a general performance
guarantee. Multi-day checkpoint/restart and operational recovery remain
unimplemented. See the [validation record](validation-data/triad-v3/2026-10-31/README.md)
for conditions, counts, confidence limits and exclusions.

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
`sample_rate / 10^(C/N₀/10)`. Its conditional model calculates usable time after
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
