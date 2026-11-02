# Selective amplitude modulation: information bounds and checked diagnostics

This study derives necessary reception conditions for the isolated three-carrier
waveform under the sampled Gaussian-ray model plus the shared additional
frequency-selective modulation stage. It does not demonstrate reception at
−60 dB-Hz, simulate ten years of samples, or establish a physical propagation
limit. The [machine-readable result](selective_information.json) contains 228
explicit cases, source hashes, assumptions and the checked helper comparison.

The main distinction is consequential: replacing a random-covariance mixture
by the Gaussian with the same covariance gives a **lower** KL divergence, not
an impossibility bound. The calculations here instead give the receiver the
selective-modulation trajectory, then upper-bound that more informative
experiment. Their exclusions survive uncertainty about that trajectory under
the stated probability law. When the upper bound is large, the calculation is
inconclusive.

## The implemented modulation law

The source contracts are [channel-models.md](../../../../channel-models.md),
`src/channel_perturbation.hpp`, `src/triad_gaussian_fading.hpp` and
`src/triad_simulation_stream.cpp`. Source fingerprints are in the result.
The selective stage follows ray propagation and precedes additive noise. It
has continuous filter state and must be allowed its output tail.

The two rotated all-pass filters are denoted A₀ and A₁. Their three complementary
branches are

\[
 B_0=(I+A_0)/2,\quad B_2=(I+A_1)/2,\quad
 B_1=I-B_0-B_2=-(A_0+A_1)/2.
\]

Each branch is an ℓ² contraction, including the middle branch; their sum is I.
This is an energy/operator statement. A conservative sample-peak or ℓ¹ bound
need not have the same constant. The implemented all-pass pole is strictly
inside the unit circle for the positive sample-rate/bandwidth geometries used
here. Zero-state finite sections are contractions too; retaining every output
tail gives the infinite causal operator used in the bound.

For branch j, at physical receiver time t,

\[
 g_j(t)=1+d_s\sin(2\pi\,0.1 f_d t+\phi_{js})
             +d_f\sin(2\pi f_d t+\phi_{jf}),\qquad
 A=\sum_{j=0}^2 D_{g_j}B_j=I+U.
\]

The six initial phases are independently uniform in the ideal probability law
analyzed here. The software uses six seeded phases and deterministic sinusoids,
not fresh independent amplitude innovations each period. This distinction
matters for long coherent accumulation.

Depth is an amplitude excursion relative to nominal gain. The checked
component domain is 0≤dₛ,d_f≤1.
For Each d the instantaneous gain is in [1−2d,1+2d]; Each50 can reach zero and
Each65 permits polarity reversal. The guide's named presets are 1, 5, 10, 25
and 65 percent; 50 percent is a useful explicit component value, not a named
GUI preset. Margin scales nominal delay and the Doppler reference once; it
does not multiply the depths. Use the resolved effective parameters. If the
Doppler reference is zero, the implementation bypasses this entire stage,
even with a nonzero selected depth; those cases require the Off bound.

The two AM rates are tied to the resolved spread Doppler reference, whereas
Gaussian B is its separately configured 2σ width. They need not equal one
another. The upper bounds below hold for any positive AM reference rate, so
they cannot distinguish Slow from Fast at equal depth. That equality is a
limitation of the bound, not a claim that practical receivers perform equally.

## The coherent component survives this AM law

Put d=dₛ+d_f and q=(dₛ²+d_f²)/2. Independence of the initial phases gives

\[
 E U=0,\quad E[U^*U]=q\sum_jB_j^*B_j\preceq3qI,
 \quad E[UU^*]\preceq3qI,\quad \|U\|\le3d.
\]

For every fixed finite-energy input x, with all filter output included,

\[
 E\|Ax\|^2=\|x\|^2+q\sum_j\|B_jx\|^2,
 \qquad E[Ax]=x.
\]

The complementary-branch identity and Cauchy–Schwarz imply
1/3≤κ=Σⱼ‖Bⱼx‖²/‖x‖²≤3. Therefore the fraction
‖E Ax‖²/E‖Ax‖²=1/(1+qκ) is between 1/(1+3q) and 1/(1+q/3).
These are ensemble ratios, not a guaranteed realization-by-realization
decoder loss or a measured sensitivity penalty.

| Each depth | Possible minimum gain | Upper mean-energy factor 1+3q | Lower coherent fraction | Quadratic bound factor F below |
|---|---:|---:|---:|---:|
| Off | 1 | 1 | 1 | 1 |
| 1% | 0.98 | 1.0003 | 0.99970 | 1.00180 |
| 5% | 0.90 | 1.0075 | 0.99256 | 1.04568 |
| 10% | 0.80 | 1.0300 | 0.97087 | 1.19080 |
| 25% | 0.50 | 1.1875 | 0.84211 | 2.54688 |
| 50% | 0 | 1.7500 | 0.57143 | 12.25000 |
| 65% | −0.30 | 2.2675 | 0.44101 | 27.88368 |

There is no average-power or per-realization normalization in this sampled
channel, and no physical-playback headroom attenuation. Adding this AM can
increase ensemble received power. Describing its entire effect as an SNR
penalty would be incorrect.

With stable complex ray gains and known timing/frequencies, the AM is a finite
linear basis: the unmodulated waveform plus six positive and six negative
frequency-shifted band outputs per ray. This suggests constrained nuisance
fitting or marginalization. It does not require committing to independent
unknown gains every AM period. A stable phase-bearing mean permits a
coherent C·T information term; AM alone does not establish a C²·T/B law.
Here C is C/N₀ in linear Hz.

For evolving Gaussian ray gains, however,
`B_j(h(t)s(t))` generally differs from `h(t)B_j(s(t))`. A block-center
factorization needs a measured commutation error, especially when Doppler
width is not small compared with the selective bank bandwidth. The present
bounds use the full operator and do not make that approximation. The existing
receiver's dimension and continuous-filter-state limits also mean that a
finite-basis suggestion is not an implemented receiver result.

## Mixture KL: the direction of each inequality

Whiten the sampled noise to Q=CN(0,I). Conditional on the selective operator A,
assume the input signal is an independent zero-mean proper complex Gaussian
vector with covariance K. Then

\[
 D(P_{Y|A}\|Q)=\operatorname{tr}(AKA^*)-
                   \log\det(I+AKA^*).
\]

The complex Gaussian formula is given in Polyanskiy and Wu's
[MIT information-theory notes, Chapter 1](https://ocw.mit.edu/courses/6-441-information-theory-spring-2016/2243edffb30f57181ed97dcb77691580_MIT6_441S16_chapter_1.pdf).
The real-Gaussian factor 1/2 does not multiply this complex formula.

The divergence chain rule gives

\[
 D(P_Y\|Q)=E_A D(P_{Y|A}\|Q)-I(A;Y)
          \le E_A D(P_{Y|A}\|Q).
\]

This is the selective-trajectory oracle bound. It conditions on the amplitude
trajectory, not on the Gaussian ray realization. Conditioning on the latter
would also reveal the random signal and yield a much looser energy bound.
The chain rule and its data-processing consequences are developed in
[Chapter 2](https://ocw.mit.edu/courses/6-441-information-theory-spring-2016/184197ca5d5418da2415d37e929860b9_MIT6_441S16_chapter_2.pdf).

Conversely, for the zero-mean Gaussian G with the same covariance as P_Y,
the quadratic log(G/Q) has the same expectation under G and P_Y. Hence

\[
 D(P_Y\|Q)=D(G\|Q)+D(P_Y\|G)\ge D(G\|Q).
\]

The Gaussian covariance proxy is a lower bound even if its covariance is
computed exactly. An explicit scalar proper-complex mixture diagnostic with
equally weighted noise-inclusive variances 1.1 and 3 gives:

| Calculation | KL nats |
|---|---:|
| Covariance-matched Gaussian lower bound | 0.33216020685 |
| Actual mixture, radial quadrature | 0.35009929621 |
| Revealed component / oracle upper bound | 0.45303876576 |

The numerical quadrature error estimate is 9.72×10⁻¹². This scalar diagnostic
checks inequality direction; it is not the modem channel.

## A bounded-operator upper bound usable without long sample arrays

For positive semidefinite Z,
`tr(Z) − logdet(I+Z) ≤ min(tr(Z), tr(Z²)/2)`.
The first bound follows from logdet≥0; the second from
z−log(1+z)≤z²/2 for each nonnegative eigenvalue.

The preceding moment identities give

\[
 E\operatorname{tr}(AKA^*)\le(1+3q)\operatorname{tr}K,
\]

\[
 E\operatorname{tr}[(AKA^*)^2]\le F\operatorname{tr}(K^2),
\quad F=\min\{(1+3d)^2(1+3q),\ 1+18q+27d^2q\}.
\]

For the first candidate F, let H=A* A. Positivity gives
`tr(K H K H) ≤ ‖H‖ tr(K² H)`, while
‖H‖≤(1+3d)² and EH≤(1+3q)I.
For the second, expand `AKA* = K+V+W`, where
`V=UK+KU*` and `W=UKU*`. The joint phase law is symmetric under U→−U,
so odd terms vanish. Cauchy–Schwarz and the two second-moment operator
bounds give
`2 E tr(KW) ≤ 6q tr(K²)`,
`E tr(V²) ≤ 12q tr(K²)` and
`E tr(W²) ≤ 27d²q tr(K²)`.

Consequently the usable mixture upper bound is

\[
 \boxed{D(P_Y\|Q)\le
 \min\{(1+3q)\operatorname{tr}K,\ (F/2)\operatorname{tr}(K^2)\}.}
\]

This is independent of carrier-spacing and AM-rate details because it uses
contractions and amplitude/moment bounds. That conservatism is intentional;
it is not a calibrated likelihood for the actual receiver.

The diagnostic checks the exact shared C++ selective output against a separate
finite operator implementation: 256 samples, maximum absolute difference
6.83×10⁻¹⁵. A 5-node quadrature in each of six independent phases exactly
integrates the polynomial moments through degree four, up to floating-point
roundoff, and checks the moment/bound identities for a small covariance.
It is not an exact quadrature of logdet or evidence about rare receptions.

## Gaussian width, delay and the −60 dB-Hz calculations

For each listed sample clock, the probe reads the actual finite Gaussian FIR
coefficients. Its autocovariance r[ℓ] is computed by discrete correlation.
The source peak-power envelope is 9.03500774607482 from the earlier
[transmitter bound](../../2026-11-02-spread/theory/transmitter_bound.json),
with its synthesis-source hashes rechecked. Only that peak bound is reused:
the earlier continuous-energy integral is **not** substituted for sampled
energy. Direct arrival is integer-aligned, clock error is zero, the delayed
ray has a fixed fractional interpolation phase, and the delayed source peak
is conservatively enlarged by the actual 16-tap interpolation coefficient
ℓ¹ envelope. Two independent equal-power Gaussian rays then satisfy

\[
 |K_{nm}|\le C\,\Delta t\,P_{\rm eff}|r[n-m]|,
\]

\[
 \operatorname{tr}K\le C\,\Delta t\,P_{\rm eff}N r[0],
\qquad
 \operatorname{tr}(K^2)\le
 (C\Delta t P_{\rm eff})^2
 \{Nr[0]^2+2\sum_{\ell=1}^{L-1}(N-\ell)_+r[\ell]^2\}.
\]

The script includes small numerical padding; these are engineering-checked
calculations, not formally certified interval arithmetic. N covers the whole
finite **pre-selective source support**, including pulse, delay and
interpolation tails. All selective output tail is allowed in the bound.
Do not reinterpret the span as active payload time followed by unlimited
additional source/filter input.

For densely sampled Gaussian Doppler, the covariance-square sum is
proportional to T/B. Thus the low-C oracle bound scales as C²T/B when the
signal is entirely zero-mean renewing Gaussian fading. At fixed B and depth,
a 10 dB reduction in C requires roughly 100 times as long to retain this
upper information allowance. This is a necessary-law scaling in the declared
model, not an achievable receiver-duration law. A coherent nonzero ray mean,
different long-term correlation law or external channel observations changes
the premise. Lowering waveform rate alone does not change it.

A decision with detection probability p and false probability α requires
`D(P_Y||Q) ≥ d_Bernoulli(p||α)`, by applying data processing to the decision.
See the binary-testing converse in
[Chapter 10](https://ocw.mit.edu/courses/6-441-information-theory-spring-2016/26fd180f40b6773bf19b659a4c5e8656_MIT6_441S16_chapter_10.pdf).
The required divergences are 9.0622364 nats for p=.99, α=10⁻⁴, and
3.8205752 nats for p=.9, α=.01. Here α is optimistically the complete
operational budget for **one completion** within the stated daily/12-hour
horizon. This does not calibrate repeated attempts, search alternatives or
correct-bit reception. A multi-year reception assigned to a completion
horizon can use this necessary test; it does not receive a new independent
false-admission allowance on every preceding day.

For C=10⁻⁶ Hz (−60 dB-Hz), a full pre-selective span of ten Julian years,
and 5 µs excess delay, the computed KL upper bounds are:

| Gaussian width B (2σ), Hz | Off | Each10 | Each25 | Each50 | Each65 |
|---:|---:|---:|---:|---:|---:|
| .001 | 7.2671 | 8.6537 | 18.5085 | 89.0224 | 202.6343 |
| .01 | .72680 | .86547 | 1.85106 | 8.90326 | 20.26577 |
| .1 | .07275 | .08663 | .18528 | .89115 | 2.02844 |
| 1 | .00727 | .00866 | .01853 | .08911 | .20284 |
| 10 | .000733 | .000873 | .001866 | .008977 | .020434 |
| 30 | .000250 | .000298 | .000638 | .003068 | .006984 |

At 5 µs, both target profiles are excluded by this bound for every listed
depth when B≥.1 Hz. At B=.001 Hz, Off through Each10 exclude the strict
profile; the relaxed profile and higher depths remain inconclusive. A larger
depth increases the upper information allowance and loosens the bound: the
table does not show that strong AM improves the receiver.

The result also covers 7 ms excess delay. Its interpolation envelope is more
conservative: at B=.1 Hz, Each65 gives 6.58331 nats, excluding strict but not
relaxed reception. Both profiles are excluded for all tested depths at both
delays when B≥1 Hz. The larger envelope is not measured extra information
or evidence that delay improves reception. Necessary C/N₀ crossings and
inverted upper detection probabilities are recorded per case in the JSON;
none are advertised as achievable sensitivity.

The results assume ideal independent Gaussian innovations and uniform phases;
they do not prove a claim about the finite PRNG or real VLF/HF/microwave
propagation. They exclude extra frontend nonlinearities, simulated AGC and
additional independent amplitude processes. Fixed deterministic phase/ray
rotations preserve the absolute covariance bound. No receiver acquisition,
bit discrimination or continuous-operation false-admission experiment was
performed by this script.

## Reproduction and validation

From the repository root with NumPy/SciPy installed:

```sh
PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 /tmp/triad-weak-env/bin/python docs/validation-data/triad-v3/2026-11-02-selective/theory/selective_information.py --output docs/validation-data/triad-v3/2026-11-02-selective/theory/selective_information.json
PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 /tmp/triad-weak-env/bin/python -m unittest discover -s docs/validation-data/triad-v3/2026-11-02-selective/theory -p test_selective_information.py -v
```

Both commands completed successfully; seven focused tests passed in 2.266 s
on this host. They check the actual helper/FIR, filter contractions and exact
bypasses, phase moments, mixture inequality direction, binary inversion,
low-signal scaling, depth limits and complete strict-JSON serialization.
The report compiles a tiny read-only C++ helper in a temporary directory. It
does not build or test the ordinary Robust receiver.

The first report attempt failed during JSON encoding because a report
comparison retained `numpy.bool_`; six earlier tests had passed without
serializing the complete report. Native scalar casts and the full-report
serialization regression repaired it without changing the physics or
thresholds. The original failure summary is retained in ignored
`local-evidence/triad-selective-theory/serialization-failure.txt`.

Useful next experiments are actual sampled-waveform joint receivers with
constrained sinusoidal selective nuisance fitting, evolving correlated ray
inference, and independent validation of filter/state and covariance
approximation errors. A covariance-only detector can be a useful baseline,
but its Gaussian proxy must not be relabeled an upper sensitivity limit.
