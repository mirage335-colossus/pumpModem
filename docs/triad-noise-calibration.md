# Three-carrier noise calibration and configuration diversity

This is an isolated extension of the [v3 study](triad-v3.md). It does not
integrate with the ordinary Robust receiver. The earlier
[state-likelihood campaign](validation-data/triad-v3/2026-11-01-state/README.md)
demonstrated long observations under known noise power and exposed a severe
false-admission failure when that power was underestimated. The receivers here
address that failure under explicit noise assumptions. They do not establish
reception in arbitrary interference or prove an optimal physical receiver.

The [configuration-diversity results](validation-data/triad-v3/2026-11-01-diversity/README.md)
record independent sampled captures, failures, source versions and computational
limits. Results are conditional on the declared simulator and acquisition bank.

## Receiver alternatives and assumptions

All methods retain simultaneous complex three-carrier evidence before nonlinear
combination. The keyed, shaped waveform, payload encryption, clock-derived
addresses and public candidate bank remain the same for paired comparisons.

| Method | Evidence retained | Noise requirement for its analytical gate |
|---|---|---|
| Gaussian OU reference | Complex block projections and joint channel evolution | Known white proper complex Gaussian variance |
| Angular Gaussian | The same projections, determinant and actual received total energy | One unknown common isotropic scale throughout a completed decision window |
| Residual-UCB Gaussian | Complex projections and independent whole-bank residual calibration | White Gaussian noise constant within each declared calibration segment; power may differ between segments |
| Phase/frequency reference | A normalized mixture over phase/frequency trajectories and tap amplitudes | Known white proper complex Gaussian variance |
| Residual-UCB phase/frequency | The same trajectory mixture, with independent conservative variance bounds | The same segment-wise condition as residual-UCB Gaussian |
| Static-frequency Gaussian / UCB | Whole-observation complex sums over a bounded FFT frequency bank, followed by joint Gaussian tap marginalization | Known variance, or one unknown stationary white-Gaussian variance calibrated from whole-bank residuals |

An unmodified known-variance reference with deliberately wrong noise power is
a failure control, not a calibrated receiver. Likewise, a common-scale angular
receiver applied to changing block powers is a model-mismatch experiment.
Active analog filtering, quantization, impulses and colored interference need
separate likelihoods or empirical calibration; a white-noise proof does not
automatically survive these effects.

The numerical implementation is in
[`triad_noise_likelihood.py`](../tools/triad_noise_likelihood.py).
`received_energy.f64` in a noisy block export is actual `sum |y|²` over the
same valid rows used for the projections. `signal_energy.f64` is noiseless
simulation truth and must never substitute for it. The receiver-facing
`iter_received_blocks(reference_variance=...)` requires an explicit declared
reference; it does not obtain the simulator's actual noise variance.

## Common-scale angular likelihood

Let the declared normalized signal covariance be `C`, `R = I + C`, and let
`n` count valid complex sample coordinates in a fixed completed window. For
`u = y / ||y||`, the complex angular likelihood ratio is

```
L_angle = det(R)^(-1) (uᴴ R^(-1) u)^(-n).
log L_angle = -log det R - n log1p(-Q/E),
Q = yᴴ (I - R^(-1)) y,       E = yᴴy.
```

Integrating the Gaussian radial density gives this normalized directional
density; see the complex matrix angular distribution, theorem 4 with one
column, in [Wróblewska (2025)](https://journals.pan.pl/Content/136752/mainFile.pdf).
Under an isotropic null, direction is uniform independently of radial scale,
so `E0[L_angle] = 1` at every fixed common noise power. This argument does not
extend to different unknown scales for separate blocks: their concatenated
direction is generally not uniform.

The streaming implementation runs the Gaussian information filter on the
observations and on a zero-observation design stream. Together these recover
the determinant and quadratic without storing the full time covariance.
One fixed normalization of observation amplitude improves numerical range and
cancels exactly in `Q/E`; it does not estimate or replace the noise scale.
Energy and final evidence use FP64. Unresolved cancellation near `Q/E = 1`
fails closed. Dense covariance and scalar-covariance oracles check the result.

## Independent residual variance bounds

For a fixed full scored bank `A`, let `P` project onto its retained column
space, `r = Aᴴy`, `G = AᴴA`, and `M = n - rank(A)`. Then

```
E_res = yᴴ(I-P)y = E - rᴴ G⁺ r,
E_res / sigma² ~ Gamma(M, scale=1)       under H0,
v_U = E_res / GammaQuantile(delta, M).
```

The residual coordinates and scored coordinates are independent under white
Gaussian noise. A candidate-specific residual after winner selection does not
have this property for the whole search. Numerical rank truncation must be
applied consistently: score only the projected correlations and Gram returned
by the full-bank rank calculation. A broad bank may leave `M = 0`; this is an
unsupported calibration geometry, not permission to use simulator variance.
The subtraction is checked against an independently computed raw-SVD residual
and fails closed when its cancellation is unresolved. The Gamma construction
is the complex counterpart of the standard
[chi-square variance confidence bound](https://www.itl.nist.gov/div898/handbook/prc/section2/prc231.htm).

Conditional on an independent bound `v_U >= sigma²`, a normalized likelihood
for a fixed additive signal `m` has null expectation

```
E0 exp(2 Re(yᴴm)/v_U - ||m||²/v_U)
  = exp[-(1-sigma²/v_U)||m||²/v_U] <= 1.
```

Integrating a fixed normalized signal/channel trajectory prior preserves this
inequality. It covers both Gaussian channel mixtures and the finite
phase/frequency mixture. The unconditional false-admission allowance is
`alpha_score + delta_calibration`, because calibration can fail. It does not
require the alternative signal model to be physically exact, but sensitivity
does depend on its adequacy.

Pooling residual energy across a whole observation is appropriate only if its
noise power is constant. Segment-wise calibration supports changing power but
costs information through more conservative bounds. In neither case may
calibration use observed winners, adaptive subspace selection or hidden channel
truth. Approximate numerical projection requires its own validation.

`--calibration-scope per-block|stationary` selects these policies explicitly.
The stationary implementation sums full-bank residual energies and degrees of
freedom in FP64, then uses one upper bound for the completed observation.
Its second pass must replay exactly the same observations. The serial harness
hash-checks that replay; the independent-path batch adapter retains one bounded
shard of the prepared statistics. A declared changing-noise schedule is rejected
for stationary UCB pooling, and common-scale angular inference is marked
unsupported for that schedule.

## Operational accounting and benchmark distinctions

The strict target is 99% correct accepted reception with at most `1e-4`
probability of any noise-only admission per 24 hours. The relaxed target is
90% correct with at most `0.01` per 12 hours. They are separate operating
targets; changing the gate on saved scores is not an independent experiment.

For `J` attempted windows, `D` durations and `K` separately selectable methods,
allocate half the false budget to calibration and half to scores. A normalized
mixture over acquisition hypotheses or declared priors does not add a union
factor for every mixture component. Taking the maximum instead does. A maximum
over two bit mixtures uses a factor two. Every externally selectable method
and duration must be counted, including alternatives tried after a failure.
Overlapping windows do not require independence for the union bound. These
fixed-window results do not authorize unrestricted sequential stopping.

The completed combined campaign reserves nine receiver alternatives: the five
original methods with per-block UCB calibration, the two stationary-UCB variants,
and the static-frequency reference/UCB pair. It also reserves two calibration
policies and three durations. At one completed attempt per 60 seconds, the
strict score gate is log-LR 21.1649; the relaxed 12-hour gate is 15.8666.
These are analytical family allocations under the stated null, not an empirical
demonstration of rare tails or an implemented overlapping-window scheduler.

The headline Gaussian and phase reports used `K=7`: their saved scores are
reclassified at the combined `K=9` gate, an increase of `log(9/7)`. Their variance
bound remains valid because its failure budget is shared across the full bank
and depends on attempts, duration and calibration policies, not the number of
scored methods. Relaxed classifications conservatively retain the stricter
calibration bounds. They are gate comparisons on the same captures, not fresh
validation samples. Some earlier `K=5` diagnostics reserved only one calibration
policy; raising their score gate cannot repair that smaller calibration
allocation. Their derived counts are explicitly score-only counterfactuals,
without a combined-family false-admission guarantee. Explicitly pass
`--selection-methods 9` to the serial and
batch tools when combining this whole family; their standalone defaults do not
infer other tools that were also exercised. A different operational family must
recalculate its allocation. Rate, waveform and propagation-geometry configuration
are fixed for each reported operational family. Automatically searching more
configurations requires additional mixture weights or family allocation.

The new likelihood tools evaluate completed fixed-duration observations. They
do not yet provide a validated inverse duration planner for arbitrary channel
uncertainty or a rolling scheduler that shares work across all daily attempts.
The configured `triad_long --mode plan` path rejects these new configurations
rather than applying its older planning assumptions silently. Nominal energy
bounds and completed duration trials are references, not planner qualification.

There are three different comparisons:

1. A known received waveform/channel in white noise is an optimistic energy
   benchmark. For a single known deterministic signal, the scalar detection
   reference requires `E/N0 = (z_(1-alpha) + z_PD)^2 / 2`. This is a detection
   reference, not by itself a correct-bit guarantee under unknown channels.
2. A declared uncertain Gaussian channel has a model-specific optimal
   likelihood. If its whitened signal covariance is `C`, its KL divergence
   from noise is `tr C - log det(I+C)`. At weak per-coherence-block SNR this
   is approximately `tr(C²)/2`. Independent fixed-duration coherence blocks
   can therefore lead to duration proportional to `(C/N0)^(-2)`, whereas
   coherent energy collection allows `(C/N0)^(-1)`. Neither scaling can be
   presumed for an arbitrary physical path or adaptive estimator.
3. Runtime, capture bandwidth, sample-clock drift, model-state resolution,
   finite search banks and export size impose practical limits distinct from
   those statistical bounds. Numerical agreement with a dense likelihood
   oracle establishes implementation accuracy, not near-optimal HF sensitivity.

The existing numerical implementation target remains at most 1 dB additional
required energy relative to a tractable reference with the **same** unknown
channel, phase, search and admission model, at the stated reception target.
A paired sensitivity sweep and independent held-out reception counts must
establish that target. Tiny dense-oracle and CPU/GPU score agreements test the
arithmetic, but do not measure a required-energy curve. The completed fixed
energy points and their gaps to the more favorable known-waveform reference
are recorded separately; this campaign does not close that 1 dB qualification.

Noise uncertainty must be treated as a model issue, not merely a longer-time
problem. [Tandra and Sahai](https://people.eecs.berkeley.edu/~sahai/Papers/tandra_sahai_revised_submission.jstsp.final.pdf)
derive SNR walls for specified uncertainty classes. Their assumptions and
definitions do not automatically prove a wall for this keyed receiver; the
relevant lesson is to test the uncertainty class explicitly.

## Configuration and experiment design

The requested tight baseline uses both hobbyist GPSDO profiles, clock sync
`GPS_10ms-1ms_region-0ms_offset`, 70-ms audio error, margin 1, HF-best spread,
1% slow and fast selective amplitude modulation, Added Symbols off, FHSS off,
and whole-bit Symbol Burst 3. The HF preset at 5.8 GHz is a controlled
cross-frequency test, not a validated microwave propagation prescription.

Rate 36 / Added 1 and Rate 3.6 / Added 10 both have six fine symbols per second
per carrier and nominal combined bandwidth 22.5 Hz. Their inner pattern
structures differ. Keep total-triplet C/N0 and actual per-bit energy fixed in
this comparison. Larger rate increases bandwidth, source cost and timing-grid
requirements; it does not create extra signal energy.

Two 10-ms clock accuracies, a 1-ms region width and two causal 70-ms audio-lag
ranges produce a 90.5-ms arrival half-window before sampling/drift allowances.
Keep its capture-coordinate center explicit. An integer clock-epoch boundary
can still require another keyed candidate even when the arrival interval is
short. Zero offset does not eliminate long-span sample-clock drift.

Exported candidate arrivals and selected simulation truth are in padded capture
coordinates. Add `capture_lead_seconds` to the unpadded clock-policy interval;
the new export metadata records both intervals. Older exports use
`max(0, -minimum_public_arrival) + 0.05` seconds of padding. This is separate
from the audio timestamp helper's coordinate shift.

Timing resolution must include the carrier separation. For equal-power adjacent
carriers with offsets `{-d,0,d}`, iid zero-mean fine coefficients and ideal RRC
shaping, a useful *approximation* to the shared-gain waveform correlation is

```
rho(delta) = RC(R * delta, rolloff) * [1 + 2*cos(2*pi*d*delta)] / 3.
```

Power retention is `|rho|²`. With `R=6`, `d=7.5 Hz` and rolloff 0.25, a 35-ms
timing error can lose about 12 dB of fixed-branch evidence. Longer accumulation
does not repair information discarded by that projection. `--timing-loss-db`
(default 0.5 dB) now derives a recommended timing step from this expression's
first main lobe; it is about 16.844 ms at that rate. It does not change the public
bank automatically and is not a finite-filter or physical-channel certificate.
The analogous clock-grid estimate uses the same permitted endpoint displacement.

`triad-retained-energy.py` measures full-bank and best fixed true-bit candidate
projection energy using noiseless simulation truth. It selects a candidate only
after summing its entire observation. These optimistic per-block free-channel
fits diagnose lost information; they must never feed receiver decisions or
noise calibration. `triad-reblock.py` sums globally referenced linear statistics
exactly, but a longer block constrains channel/phase evolution more strongly and
must be tested as a different receiver approximation.

RF accuracy scales with the shift: the established relative 0.0001-ppm
hobbyist GPSDO envelope contributes 0.00005 Hz at 500 kHz and 0.58 Hz at
5.8 GHz. The 100-ppm free-running profile contributes 50 Hz and 580 kHz.
These are configured model envelopes, not measured hardware performance.
Differential cancellation cannot restore a signal outside the captured band.
Whole-bit Symbol Burst 3 schedules the next useful symbol after a three-span
wait, rounded to its UTC calendar; it is not three silent tiles within one bit.

Use short screens before long runs: matched fine rates; covered and uncovered
timing/epoch banks; flat and two-ray paths; delay/Doppler mismatch; oscillator
profiles and RF shift; stationary and changing noise; 10-ms erasures; explicit
FHSS and burst alternatives. Then freeze selected receiver priors and validate
new keys, physical paths, clocks and both bits independently. Thermal repeats
on one path measure conditional noise variation and are not additional
independent physical paths. Hour/day exports must preserve channel/filter
state and complete tails without time compression.

`triad_bounded_run.py` owns cloud experiment process groups and applies bounded
TERM/KILL cleanup. A timeout, partial export or cleanup failure is recorded as
a failure. The serial harness reports completed-capture sensitivity separately
from processing errors and exits nonzero if any capture fails. The independent-
path adapter additionally counts processing failures as misses in its all-attempt
signal bound; failed null processing invalidates a whole-campaign qualification.

## Streaming inference and next receiver comparisons

`triad-noise-batch-study.py` batches actual independent sampled paths, retaining
FP64 full-bank rank, energy and variance preparation and FP64 log evidence.
Its explicit FP32 bulk mode disables reduced-input tensor products. Default
block transfers and optional resident-shard transfers execute the same model;
resident inputs trade bounded memory for fewer host/device transfers. Reports
separate preparation, packing, transfer, inference and peak CUDA memory.
File mappings are released between physical paths. These are isolated study
implementations, not GUI precision controls or a deployed continuous receiver.

`triad-static-frequency-study.py` adds a complementary static-channel model.
It sums complex whitened block observations over bounded frequency hypotheses
before forming channel-fit energies. FFTs replace repeated direct frequency
dot products; Gaussian tap coefficients are marginalized with small Cholesky
solves. Candidate and frequency alternatives are normalized mixtures, not a
maximum whose search penalty is omitted. The final partial block uses its actual
center timestamp. Direct sums and dense Gaussian likelihoods provide independent
oracles. CPU FP64 and CUDA FP32 score comparisons are recorded in the results.

This model approximates residual common phase as constant within each retained
block and assumes constant residual CFO and fixed tap coefficients across the
observation. Known tap Doppler remains in the sampled basis. It cannot infer
arbitrary oscillator wander or changing propagation. Shorter blocks reduce
within-block frequency loss but increase work; a fine FFT cannot recover
information discarded during projection. Its stationary UCB projection covers
the full scored block bank because multiplying a column by a block-constant
frequency phase does not enlarge that column space.

The phase model now exposes `--frequency-model` and `--frequency-random-walk`.
Making OU correlation time large does not disable an independently nonzero
random-walk term. A physically static frequency offset can use the explicit
static model while retaining phase diffusion; coloured oscillator cases need
their own declared dynamics and convergence tests.

The timing-loss evidence also motivates retaining separate complex carrier
correlations before imposing the shared channel relationship. A timing
refinement can then rotate those linear carrier observations before combining
them, rather than trying to recover information already cancelled in a coarse
shared-gain projection. This is an unimplemented workspace alternative. It still
needs fractional envelope timing, cross-carrier noise covariance, a normalized
joint likelihood and comparison against an unpruned sampled reference; it is
not a claim that three fine-sample carrier products avoid squaring loss.

A distinct next comparison is Rao–Blackwellized joint phase/channel inference:
each oscillator trajectory carries its own conditional Gaussian tap posterior,
rather than a single static amplitude/relative-phase quadrature. Relevant primary
precedents are [joint CFO/channel estimation](https://www.merl.com/publications/docs/TR2010-095.pdf)
and [non-pilot phase/CFO sequential Monte Carlo](https://link.springer.com/article/10.1155/2008/612929).
They support an architecture hypothesis, not a demonstrated gain at this study's
C/N0. First compare tiny exact trajectory sums, fixed delay/Doppler mixtures and
retained-energy ceilings; then measure particle depletion, likelihood-estimator
variance and runtime on independent weak captures. Correct normalized proposals
and resampling are essential to likelihood-based admission accounting; choosing
the best particle or pruning by uncalibrated evidence cannot inherit that result.
