# Stochastic-spread information limits

This is analytical evidence for specified Gaussian observation models. No modem
or channel simulation is run by these files. The [calculation](spread_frontier.py)
produces [182 spectrum/SNR cases](spread_frontier.json), covering total-triplet
C/N₀ from −60 to 0 dB-Hz and observations of at most ten Julian years.
The [eight focused tests](test_spread_frontier.py) check normalizations, finite
covariance bounds, numerical refinement, spectral limits and bit identifiability.

For unit white proper-complex Gaussian noise and zero-mean Gaussian signal
covariance `K`, the signal/noise divergence is

`D = tr(K) − logdet(I+K)`.

If any detector admits the true bit with probability at least `p`, while its
noise-only admission probability is at most `α`, data processing requires

`D ≥ d(p || α) = p log(p/α) + (1−p) log((1−p)/(1−α))`.

This is a necessary condition for **every detector on that observation model**,
rather than a performance bound on one chosen likelihood gate. It is not
sufficient for reception. The decision-event specialization follows relative
entropy data processing as developed in the authors' [information-theory
text](https://people.lids.mit.edu/yp/homepage/data/itbook-2022.pdf).

For one completed decision using the entire horizon allowance, the required
divergence is **9.06224 nats** for 99% correct/10⁻⁴ false admission per day, and
**3.82058 nats** for 90% correct/1% per 12 hours. A separately labeled comparison
uses the current 16-method, eight-duration, two-bit, minute-attempt family and
half-alpha score allocation. Dividing alpha by that family is an imposed
component allocation; it is not necessary for every possible family-wise test.

## Width, center and waveform assumptions

The Gaussian PSD integrates to one and has standard deviation `σ`; reported
spread is **2σ Hz**. A deterministic center frequency is a separate parameter.
This follows the distinction used by [ITU-R F.1487](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)
for HF Gaussian scatter components. That recommendation is not treated as
field validation of sub-9-kHz propagation.

The OU comparison has a Lorentzian PSD matched to the Gaussian **half-width at
half maximum**. Lorentzian RMS width is infinite. These comparisons therefore
do not match RMS Doppler or correlation time. The long-time Gaussian spectral
integral is reported separately from finite-time bounds; the [Gaussian
entropy-rate literature](https://arxiv.org/abs/1103.5602) does not justify silently
substituting a rate times duration for an exact finite-time divergence.

Two separated ray observations are not automatically more informative than a
single noisy sum: overlap can concentrate the covariance. The report includes
both separated and fully overlapping extremes. For equal-power independent
rays, identical stochastic shapes and constant deterministic envelopes, stable
center offsets give divergence between these extremes. The overlapping value
is consequently the relevant upper bound for exclusions independent of center
separation within that model.

Actual shaped patterns can have varying instantaneous power. A separate bound
allows arbitrary deterministic envelope phases and stable centers, provided
each delayed envelope has energy at most `T` and squared amplitude at most a
declared PAPR. The example PAPR values 1, 3 and 10 are **assumptions, not measured
transmitter bounds**. Delays, shaping, guards, oscillator noise, outages and
interference are not otherwise modeled here.

## Selected results

At **−60 dB-Hz**, Gaussian stochastic spread **2σ = 0.001 Hz**, and ten years,
the constant-envelope summed-ray divergence is at most **0.0890223 nats**.
Binary-KL inversion bounds any detector's admission probability by **2.0517%**
at α = 10⁻⁴ and **7.5346%** at α = 0.01. Even the separate hypothetical
PAPR ≤ 10 envelope bound is below both target divergences. These exclusions
are conditional on the specified channel, noise and power assumptions.

At the same Gaussian width, the necessary duration lower bounds are:

| Total C/N₀ | Strict target | Relaxed target |
|---|---:|---:|
| −60 dB-Hz | More than ten years | More than ten years |
| −50 dB-Hz | More than ten years | At least 1,567.55 days |
| −40 dB-Hz | At least 37.18 days | At least 15.68 days |

These are **lower bounds, not achievable duration plans**. They do not predict
99% or 90% reception when the configuration is not excluded. The OU model,
Gaussian spectrum and hypothetical PAPR cases have distinct rows in the JSON.

A completely known, fixed deterministic waveform at −60 dB-Hz needs 211.50 days
for the strict one-completion Gaussian presence reference, or 75.33 days for
the relaxed reference. With the separate minute-family allocation these become
432.07 and 261.20 days. This known-channel calculation does not include random
fades or unknown-channel acquisition and is not a universal minimum bit time.

Presence and bit identification remain separate. The tiny covariance examples
show that global-phase alternatives are indistinguishable under a circular
Gaussian gain, and independent per-cell gains erase phase-only distinctions.
Amplitude differences or temporal correlation can retain identification
information. Actual keyed triad patterns need their own sampled-waveform check.

OU finite-time divergence is evaluated from a closed-form Fredholm determinant.
Gaussian finite-time divergence is bounded using covariance traces and an
operator-norm bound. Selected expressions use 70-digit intermediate arithmetic;
root searches, quadrature, binary-KL inversion and final inequalities use FP64.
No interval-arithmetic certificate, Monte Carlo reception rate or empirical
rare-tail calibration is claimed. Jakes spectra are outside this bounded study.

## Connection to the implemented transmitter

The separate [actual-table calculation](transmitter_bound.py), its
[C++ helper probe](pulse_bound_probe.cpp), [results](transmitter_bound.json) and
[five checks](test_transmitter_bound.py) bind an envelope bound to the inspected
v3 source. The probe calls the shared `pattern_pulse` implementation rather than
substituting another RRC formula.

Every chip permutes the amplitudes `sqrt(.4), 1, sqrt(1.6)` across the three
carriers. Let their sum be `A` and the actual carrier gain be `g`. The source's
common ramp envelope stays between zero and one, so

`|x(t)| ≤ g A max_u Σ_k |p(u−k)|`.

The shared pulse is linear between knots spaced by 1/256 chip. The absolute
shifted-pulse sum is convex on each such interval, making its maximum occur at
a knot endpoint; finite-support endpoints are included explicitly. The measured
table maximum is **1.7968897973**, giving a padded **absolute peak-power bound
9.0350077461 relative to nominal power one**. This is not a PAPR claim: the
waveform's exact mean power has not been assumed.

For continuous-time energy, a Gram-matrix absolute-row-sum bound includes all
integer chip lags and carrier differences 0, 1.25 and 2.5 cycles/chip. Integrals
use the actual piecewise-linear table. The resulting synthesis operator bound
is **1.0199961389**. For actual active elapsed duration `T` and fine-symbol rate
`R`, including source rounding, the bound is

`∫|x(t)|² dt ≤ 1.0199961389 × ceil(T R)/R`.

The factor from the actual stored chip amplitudes and carrier gain is one to
displayed precision and is retained in the calculation. Ramps and silence can
only reduce this continuous energy. This avoids assuming exact unit-energy
normalization: the interpolated pulse itself has energy **0.9999913383**.

Substituting both the absolute peak and energy upper bounds into the Gaussian
covariance argument gives a ten-year, −60 dB-Hz, 2σ = 0.001 Hz upper KL of
**0.8204–0.8207 nats** across the four rates 0.036 through 0.000036 Hz. The
one-completion strict/relaxed admission upper bounds are approximately
**13.14% / 31.14%**. This is a continuous source/scatter/noise model result;
neither the pulse-Gram integral nor that conclusion automatically transfers to
point-sampled observations.

The discrete diagnostic instead probes the **actual Gaussian fading FIR** at
physical sample intervals 4, 16 and 64 seconds, uses its exact finite-kernel
autocovariance, and bounds sample energy by peak power times sample count.
It includes the stream interpolator's absolute gain for an integer-aligned
direct arrival and its 5 μs delayed ray, plus both eight-sample interpolation
tails. Under zero sample-clock error and with extra gain, perturbation and
frontend stages off, the resulting upper KL is **7.2671 nats** and strict
admission is at most approximately **83.73%**. This weaker bound excludes the
strict target for those stated discrete cases; it **does not exclude the
relaxed target**. Arbitrary fractional arrival or clock drift needs a matching
interpolation bound and is not covered by these particular discrete examples.

These calculations bound the deterministic source before stochastic fading;
received Rayleigh gains remain unbounded. The continuous and discrete reports
retain separate assumptions, current source hashes and numerical padding.
The 1e−9 padding is an engineering allowance, not a formal certificate for all
compiler, libm, extreme-coordinate or analog reconstruction behavior.

Reproduce from the repository root with the scientific Python environment:

```sh
OPENBLAS_NUM_THREADS=1 python docs/validation-data/triad-v3/2026-11-02-spread/theory/spread_frontier.py --output docs/validation-data/triad-v3/2026-11-02-spread/theory/spread_frontier.json
OPENBLAS_NUM_THREADS=1 python -m unittest discover -s docs/validation-data/triad-v3/2026-11-02-spread/theory -p test_spread_frontier.py
OPENBLAS_NUM_THREADS=1 python docs/validation-data/triad-v3/2026-11-02-spread/theory/transmitter_bound.py --output docs/validation-data/triad-v3/2026-11-02-spread/theory/transmitter_bound.json
OPENBLAS_NUM_THREADS=1 python -m unittest discover -s docs/validation-data/triad-v3/2026-11-02-spread/theory -p test_transmitter_bound.py
```
