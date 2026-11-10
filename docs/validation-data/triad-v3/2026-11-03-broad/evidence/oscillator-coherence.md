# Oscillator coherence and capture at very low C/N₀

This note separates three issues: capture of a frequency-offset signal, loss in
one untracked coherent correlation, and information available to a better phase
receiver. A bad untracked correlation is **not** a universal receiver limit.
The calculation (retired historical source) and [results](oscillator-coherence.json)
use the actual isolated CLI preset composition. Three native `configure` calls
and small scalar checks completed; no new sampled transmission was generated.

## Actual preset and endpoint semantics

The registry is [`src/tuning_presets.cpp`](../../../../../src/tuning_presets.cpp).
Its accuracy values are **effective relative bounds**, not per-radio bounds:

| Preset | Relative accuracy (ppm) | Class phase diffusion (degrees/√s) |
| --- | ---: | ---: |
| Free-running crystal | 100 | 0.5 |
| GPSDO hobbyist XO | 0.0001 | 0.5 |
| GPSDO TCXO | 0.0001 | 0.05 |
| GPSDO OCXO | 0.0001 | 0.005 |

The equal GPSDO accuracy is a selected locked-link residual assumption; its
phase-diffusion values distinguish the illustrative classes. These are not
measured PSDs or guarantees for arbitrary real GPSDOs. For context, the native
CB/SSB and IC-7100 Shift entries use effective relative bounds 40 and 1 ppm,
respectively, corresponding to their two-radio sums.

When both Baseband and Shift presets are supplied, isolated `triad_long` sets

`σ = hypot(base_phase, shift_phase) × π/180` radians/√s,

then assigns **σ/√2 to each independent TX and RX endpoint**. Their relative
Brownian variance rate is σ². Do not multiply by another √2 after this step.
The sampled model increments phase by endpoint diffusion times `sqrt(dt)` times
an independent standard normal draw. Additional white/flicker/random-walk FM
and white PM are separate controls; they are zero in this calculation.
Preset margin scales the selected spread/delay, not these oscillator truths.
The independent base/RF topology is not a model of the unsupported shared
Baseband-clock Shift selection.

## What one untracked long correlation loses

For `θ(t)-θ(s) ~ N(0,σ²|t-s|)`,

`E exp(i[θ(t)-θ(s)]) = exp(-|t-s|/τ)`, with `τ=2/σ²`.

After removing the known deterministic waveform and static frequency, a
constant-power rectangular observation has mean retained coherent energy

`F(T) = E|T⁻¹ ∫₀ᵀ exp(iθ(t))dt|² = 2(x-1+exp(-x))/x²`, `x=T/τ`.

This follows by integrating the covariance over the observation square.
It is also the Brownian term of the existing
`expected_correlation_coherence` (retired historical source)
helper. The calculation uses a small-x series and `expm1`, avoiding precision
loss without requiring higher-than-FP64 arithmetic.

Both selected oscillator classes are the same in each row below. At 850 days
and total C/N₀ = −60 dB-Hz (`c=10⁻⁶ s⁻¹`):

| Baseband + Shift | τ | Mean F(850 days) | Correlation loss | cτ | Mean projected energy cTF |
| --- | ---: | ---: | ---: | ---: | ---: |
| crystal or GPSDO XO | 3.648 hours | 0.00035754 | −34.47 dB | 0.01313 | 0.02626 |
| GPSDO TCXO | 15.198 days | 0.035121 | −14.54 dB | 1.3131 | 2.5793 |
| GPSDO OCXO | 1,519.818 days | 0.836976 | −0.773 dB | 131.3123 | 61.4675 |

Without phase error, energy is `cT=73.44`. These projected energies are
**expectations of a random statistic**, not deterministic noncentralities,
correct-reception probabilities, or sufficient-statistic information bounds.
They do not justify substituting a mean into a rare-tail calibration.
For `T≫τ`, one fixed-phase correlation's mean energy saturates at `2cτ`;
continuing that particular correlation indefinitely does not retain all new
signal energy. A constrained phase tracker, mixture/state-space likelihood,
or accumulation before carrier products may retain more. The small `cτ` of
the XO case explains why assuming a reliable phase estimate in each coherence
interval is especially questionable at −60 dB-Hz.

Changing GUI rate does not change the selected Brownian σ. A slower pattern
can reduce template and bandwidth cost while leaving this phase evolution
unchanged. Carrier comparison cancels common phase in a noiseless algebraic
model, but multiplication introduces noise products; cancellation alone is not
proof that all pre-multiplication information survives. Delayed TX phase and
frequency-selective paths also need their actual joint channel model.

The favorable known-waveform presence floors in
`triad_coverage_bounds.py` (retired historical source)
remain unchanged: 211.50 days for 99%/10⁻⁴ or 75.33 days for 90%/.01 at
−60 dB-Hz and unit useful-energy efficiency, before unknown-channel penalties.

## RF capture can dominate a millihertz waveform

For GUI Rate .001 Hz and Added Symbols ×1, the native fine rate per carrier is
`.001/6 Hz`, spacing is **0.000208333 Hz**, and nominal triplet width is
**0.000625 Hz**. The probes use 5.8 GHz RF, half-rate IF reference .0005 Hz,
margin 1, spread None/modulation Off, no explicit drift, no diffuse ray width,
and the tight 10-ms/1-ms/0-ms/70-ms timing configuration. `--nominal-if-hz` is
an oscillator-error reference, not an absolute rendered real-audio center.

Let `b` and `r` be native relative Baseband and Shift bounds in ppm, and put
`a_min=1-b×0.5e-6`, `a_max=1+b×0.5e-6`. The public CFO bound is

`f_bound=(f_RF*r + f_IF*b)×1e-6/a_min`.

The native conservative capture predicate in this specific None/Off case is

`Fs*a_min > W*a_max + 2*f_bound`.

| Both oscillator presets | Public CFO ± bound | Native Fs must exceed | Selected Fs | Bare 850-day samples |
| --- | ---: | ---: | ---: | ---: |
| GPSDO OCXO or XO | 0.580000000029 Hz | 1.160625000116 Hz | 2 Hz | 146,880,000 |
| Free-running crystal | 580,029.001450 Hz | 1,160,116.009326 Hz | 2 MHz | 146,880,000,000,000 |

The smallest integer sample rates passing that predicate are 2 and 1,160,117
Hz. The crystal probe uses 2 MHz as a convenient supported integer clock.
The native source rounds the 850-day request to 73,728,051.2 active seconds;
complete shaped source support is about 73,824,054.5 seconds at 2 Hz, or
**147,648,109 source-support samples**, before additional receiver capture
padding. That already exceeds the raw export's 128-million-row ceiling, while
bounded streaming/statistics paths have different limits. At 2 MHz the native
source-support count is **147,648,102,400,007**. Bounded RAM does not make that
sample-processing cost small.

The native whole-bit frequency-grid estimates are **342,098,159** positions
for the GPSDO cases and **342,115,263,331,198** for crystal, both far beyond the
10,000-candidate materialization cap. These are estimates for a simple uniform
coherent grid, not lower bounds on every acquisition algorithm. Coarse RF
capture/acquisition followed by correct heterodyning and decimation can reduce
fine-search work; it must first cover the signal and preserve relevant
sidebands. Differential cancellation cannot recover discarded capture energy.
The deterministic capture predicate does not bound the infinite spectral tails
of Brownian/other oscillator laws or establish analog filtering equivalence.

## Optional finite Brownian-phase information bound

**Independent mathematical review accepted this scoped derivation; it is not
a sampled-triad exclusion.** It is included to avoid treating the
Gaussian fading KL formula as though it applied to a constant-modulus random
phase signal. The main retention and capture findings above do not depend on it.

Assume a single constant-power continuous signal with known deterministic
waveform, unknown Brownian phase with variance rate σ², total C/N₀ `c`, and
independent proper-complex white noise. The divergence below is always
`D(P_signal || P_noise)`, in nats. There are no extra phase observations,
delayed rays, selective filters, nonconstant envelope or clock errors. For its
signal/noise likelihood ratio L, two independent phase paths give

`E₀[L²] = E exp(2c ∫₀ᵀ cos δ(t)dt)`,

where their phase difference has generator `σ² ∂²/∂θ²` on the circle.
Feynman–Kac therefore gives the operator
`H=σ²∂² + 2c cos θ`. Use the uniform probability measure on the circle.
For any normalized function `f=a+g`, with mean-zero g and `||g||²=u`,
Poincaré's inequality and `||cos||²=1/2` give

`<f,Hf> <= (2c-σ²)u + 2√2 c √(u(1-u))`.

Maximizing this two-dimensional quadratic form yields

`λ_max(H) <= λ_u = min(2c, [sqrt((σ²-2c)²+8c²)-(σ²-2c)]/2)`.

For a uniform initial phase, `log E₀L² <= T λ_u`. For any fixed initial phase,
first smooth for `t₀=min(T,1/σ²)`. The potential is at most 2c, so the first
weighted transition density is bounded by `exp(2ct₀)` times the heat kernel.
Its L² norm is

`||p_t₀||₂² = 1 + 2 Σ_{n≥1} exp(-2σ²n²t₀)`.

Cauchy–Schwarz and the remaining semigroup bound then give

`log E₀L² <= 2ct₀ + (T-t₀)λ_u + log ||p_t₀||₂`.

The same smoothing bound holds uniformly over the initial phase and over any
initial phase distribution: averaging initial heat kernels cannot increase
their common L²-norm upper bound. This permits an initially known phase or an
unknown initial distribution, but **does not permit side-channel observations
of the subsequent phase trajectory**.

The code bounds the positive infinite-sum tail by a Gaussian integral and
also takes the trivial `2cT` second-moment upper bound. Finally,
`D(P_signal || P_noise) <= log E₀L²`, and revealing the phase gives the separate
bound `D(P_signal || P_noise)<=cT`.
This uses the actual random-phase mixture; it never replaces it by a Gaussian
signal with the same covariance.

For the XO/crystal σ and −60 dB-Hz over 850 days, the bound is **0.97710 nats**
for uniform initial phase or **1.11018 nats** for arbitrary initial phase.
The one-completion decision necessities are 9.06224 and 3.82058 nats for the
strict and relaxed targets. Subject to the stated derivation and model, this
separate flat constant-power example cannot meet those targets. TCXO/OCXO
bounds reduce to the much larger oracle `cT=73.44` and are inconclusive.
**Do not propagate that XO result to the broad configuration census:** the
actual three-carrier shaped signal is not this constant-power scalar model,
and neither Each65 nor two-ray composition is included. A suitable actual
waveform/operator upper envelope and separate review would be needed first.

## Research context and next useful experiments

The primary [Wiener phase-noise capacity study](https://arxiv.org/html/1503.08644v3)
uses an average-power constraint and optimized input distributions; it reports
low-SNR behavior close to AWGN for that discrete-time setting. That does not
supply a near-optimal one-bit receiver for a fixed bounded, noise-like waveform.
Its model discussion also distinguishes a sampled phase approximation from
continuous filtering. [Oversampled Wiener-channel bounds](https://arxiv.org/abs/2001.07485)
compare coherent, partially coherent and noncoherent processing in their stated
regimes; those asymptotic results are not a −60-dB-Hz triad qualification.

[NIST's oscillator-noise discussion](https://tf.nist.gov/phase/Properties/nine.htm)
describes multiple power-law components. Consequently a single Wiener σ
extrapolated over 850 days is a transparent stress assumption, not a substitute
for a measured phase-noise/Allan-deviation model or device aging/holdover data.

Useful bounded followups are actual paired global versus segmented/state-space
reception at fixed source energy and physical phase law, phase-grid refinement
with retained alternatives through fades, and coarse capture/decimation checks
for large RF uncertainty. Preserve null/wrong-key outcomes and complete family
accounting. Do not require a huge fine coherent frequency grid merely because
that is the simplest estimator in `configure`.

Reproduce the cheap calculation from the repository root with a fresh output:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-broad/evidence/oscillator-coherence.py --output /tmp/oscillator-coherence-fresh.json
```

The JSON records source and executable hashes separately, commands, assumptions
and scalar/native checks. It does not certify that an arbitrary local binary
was built from the inspected source, and no reception or throughput measurement
is inferred from these native configuration calls.
The script and JSON preserve their original pre-review status text; the
independent mathematical review recorded above occurred afterward and does not
rewrite those calculation receipts.
