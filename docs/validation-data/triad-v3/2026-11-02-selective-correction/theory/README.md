# Corrected dispersive operator: necessary information bounds

These are analytical upper bounds for the isolated `dispersive-dft-v1`
channel, with checked finite operator calculations. They are **not reception
measurements, achievable duration estimates, or universal propagation limits**.
The [machine-readable result](dispersive_information.json) contains 36
−60 dB-Hz cases at full pre-selective spans of 850 days and ten Julian years.
It preserves the earlier [legacy calculation](../../2026-11-02-selective/theory/README.md)
and derives new constants for the corrected operator rather than editing old
results.

Under the explicit zero-mean renewing-Gaussian assumptions below, **both the
strict and relaxed targets are excluded in all 36 cases**. In particular,
Each65, Gaussian width `B=0.1 Hz`, 7-ms ray excess delay and ten-year input
support has an oracle KL upper bound of **1.34035 nats**. The necessary
information is 9.06224 nats for 99% detection at false probability `1e-4`, and
3.82058 nats for 90% at `0.01`. The old legacy upper bound of 6.58331 nats
excluded only the strict target in that cell. The new conclusion is specific
to the new operator and its stronger structural bound.

**The stable 850-day, −60 dB-Hz experiment has fixed rays and `B=0`; these
exclusions do not apply to it.** A surviving coherent component, external
channel observations, or a different long-term stochastic law changes the
problem. Neither the old nor new bound says that −60 dB-Hz is impossible in
all channels.

## Assumptions and the oracle

Condition on the transmitted keyed waveform, oscillator phase trajectory and
initial deterministic ray phase, without revealing the Gaussian ray gains.
The pre-selective signal is then zero-mean proper-complex Gaussian with
noise-normalized covariance `K`. Two independent stationary Gaussian FIR ray
gains, equal ray powers, and the stated source/delay geometry define the
covariance envelope. Six independent uniform AM phases are independent of
those ray gains. This ideal probability law is not a theorem about the finite
seeded PRNG.

Let `A=I+U` be the selective operator. The receiver is favorably given `A` and
the noise density. With white proper-complex noise `Q=CN(0,I)`,

\[
D(P_Y\|Q)\le E_A D(P_{Y|A}\|Q)
 = E_A\{\operatorname{tr}(AKA^*)-\log\det(I+AKA^*)\}.
\]

The inequality follows by revealing the independent AM trajectory under both
hypotheses and then discarding it. This upper bound does not replace the
covariance mixture by a covariance-matched Gaussian: the latter gives a
**lower** KL bound and cannot establish the exclusions here. The proper-complex
Gaussian KL formula has no additional real-Gaussian factor `1/2`.

All inputs are zero extended and all selective FIR output is allowed, including
its complete tail. For a finite input, `I` denotes its isometric embedding in
the output calendar; the operator inequalities can equivalently be written on
the common zero-extended sequence space. A shorter capture or deterministic
post-processing cannot increase the information if the corresponding noise is
treated consistently.

## Why both corrected branch stacks contract

Let `D_0=I`, and let `D_1,D_2` be the actual sampled cubic fractional delays.
Each coefficient includes the center-frequency rotation at its actual integer
lag. For fractional part `δ∈[0,1]` and
`s=sin²((ω−ω_center)/2)`, the cubic delay satisfies

\[
|D(e^{j\omega})|^2 =
1-\frac49\delta(1-\delta)(2-\delta)(1+\delta)s^2
   [3+4\delta(1-\delta)s]\le1.
\]

An integer delay is the exact `δ=0` case. Hence each delay is an ℓ²
contraction, including its complete zero-state FIR output. The three branches
are a scaled three-point DFT of these delays. DFT orthogonality gives **both**

\[
\sum_b B_b^*B_b=\tfrac13\sum_m D_m^*D_m\preceq I,
\qquad
\sum_b B_bB_b^*=\tfrac13\sum_m D_mD_m^*\preceq I.
\]

The second relation matters for the improved fourth-moment bound. It also
holds for finite zero-extended input and full-tail rectangular matrices.
These claims concern the actual sampled cubic law; exact ideal-delay spectral
periodicity is unnecessary.

Put `d=d_s+d_f`, `q=(d_s²+d_f²)/2`, and write
`U=Σ_b M_{u_b}B_b`, where `u_b=g_b−1` is the real sinusoidal excursion.
Pointwise Cauchy–Schwarz and the input stack bound give

\[
\|U\|\le\sqrt3 d,\qquad EU=0,\qquad E[U^*U]\preceq qI.
\]

For the other second moment, use the identical component rates across bands
and independent phases. For one component of depth `a` and angular rate `Ω`,

\[
E[U_aU_a^*]=\tfrac{a^2}{4}
 \left(M_{e^{j\Omega t}}\sum_bB_bB_b^*M_{e^{-j\Omega t}}
       +M_{e^{-j\Omega t}}\sum_bB_bB_b^*M_{e^{j\Omega t}}\right)
 \preceq\tfrac{a^2}{2}I.
\]

Cross-component moments vanish by phase independence. Consequently
`E[UU*]≤qI` as well. Independent modulation values at every time sample are
not assumed; one fixed set of six phases drives the entire observation.

For every fixed finite input, the ensemble energy obeys

\[
E\|Ax\|^2=\|x\|^2+q\sum_b\|B_bx\|^2,
\qquad 1+q/3\le E\|Ax\|^2/\|x\|^2\le1+q.
\]

These are ensemble ratios over phases, not guaranteed realized energy gains.
The channel performs no energy normalization.

## Two valid quadratic oracle bounds

For PSD `Z`, `tr Z−logdet(I+Z)≤min(tr Z,tr Z²/2)`. The linear bound is

\[
E\operatorname{tr}(AKA^*)\le(1+q)\operatorname{tr}K.
\]

Writing `H=A*A`, positivity gives
`tr(K H K H)≤||H|| tr(K²H)`. Therefore one quadratic factor is

\[
F_1=(1+\sqrt3d)^2(1+q).
\]

A stronger alternative uses `AKA*=K+V+W`, with `V=UK+KU*` and `W=UKU*`.
The joint phase law is symmetric under `U→−U`, so the odd terms vanish.
Put `T=tr(K²)`. In a basis diagonalizing `K`,
`λ_iλ_j≤(λ_i²+λ_j²)/2`, together with the two second moments above, yields
`E tr(KUKU*)≤qT`. The Frobenius Cauchy–Schwarz inequality also gives
`|E tr(UKUK)|≤qT`. It follows that

\[
2E\operatorname{tr}(KW)\le2qT,\quad
E\operatorname{tr}(V^2)\le4qT,\quad
E\operatorname{tr}(W^2)\le\|U\|_{\max}^2
 E\operatorname{tr}(UK^2U^*)\le3d^2qT.
\]

Thus `F_2=1+6q+3d²q` is valid, and the final upper bound is

\[
\boxed{D(P_Y\|Q)\le
 \min\{(1+q)\operatorname{tr}K,\ \tfrac12\min(F_1,F_2)\operatorname{tr}(K^2)\}.}
\]

The old branchwise factor remains a valid looser bound here, but is not needed.
It must not be transplanted as an equality or treated as measured performance.

| Control | `1+q` | Corrected `F_1` | Corrected `F_2` | Legacy bound factor |
| --- | ---: | ---: | ---: | ---: |
| Off | 1 | 1 | 1 | 1 |
| Each25 | 1.0625 | 3.699679 | **1.421875** | 2.546875 |
| Each65 | 1.4225 | 15.040565 | **5.677075** | 27.883675 |

## Numerical exclusions and their scope

The source peak envelope, `9.03500774607482`, is reused only after rehashing
the synthesis files bound by the earlier transmitter calculation. No continuous
energy integral is substituted for point-sampled energy. The actual finite
Gaussian FIR coefficients are probed afresh from C++. The earlier
`input_bounds` function is reused without modifying its source: it bounds the
16-tap delayed-source interpolation peak and computes the discrete
covariance-square sum, including finite-length edge terms.

The numeric envelope assumes zero audio-clock error, integer-aligned direct
arrival and a fixed delayed-ray interpolation phase. Sample rates are 16 Hz
for `B=0.1,1 Hz`, and 128 Hz for `B=10 Hz`. Here `B=2σ` of each ray's Gaussian
Doppler spectrum; it is distinct from deterministic ray-center separation.
Spacing and positive AM reference rate do not enter these conservative
operator constants. New clock/arrival sampling cases need a new input envelope.

KL upper bounds at −60 dB-Hz:

| Full input span | Gaussian `B` | Ray delay | Off | Each25 | Each65 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 850 days | 0.1 Hz | 5 µs | 0.0169294 | 0.0240715 | 0.0961094 |
| 850 days | 0.1 Hz | 7 ms | 0.0549444 | 0.0781240 | 0.3119233 |
| 850 days | 1 Hz | 5 µs | 0.00169294 | 0.00240715 | 0.00961094 |
| 850 days | 1 Hz | 7 ms | 0.00549444 | 0.00781240 | 0.03119233 |
| 850 days | 10 Hz | 5 µs | 0.00017054 | 0.00024249 | 0.00096818 |
| 850 days | 10 Hz | 7 ms | 0.00051345 | 0.00073007 | 0.00291491 |
| 10 Julian years | 0.1 Hz | 5 µs | 0.0727466 | 0.1034366 | 0.4129878 |
| 10 Julian years | 0.1 Hz | 7 ms | 0.2360992 | 0.3357035 | 1.3403527 |
| 10 Julian years | 1 Hz | 5 µs | 0.00727466 | 0.01034366 | 0.04129878 |
| 10 Julian years | 1 Hz | 7 ms | 0.02360992 | 0.03357035 | 0.13403527 |
| 10 Julian years | 10 Hz | 5 µs | 0.00073283 | 0.00104199 | 0.00416034 |
| 10 Julian years | 10 Hz | 7 ms | 0.00220634 | 0.00313714 | 0.01252555 |

All entries are below both target KL requirements. The larger 7-ms values
reflect a **looser interpolation peak envelope**, not demonstrated information
gain from delay spread. An upper bound that is larger does not establish a
better channel or receiver.

For Each65, `B=0.1 Hz`, 7 ms and ten years, the most favorable single-completion
correct-admission probability is at most 0.19982 under `α=1e-4`, or 0.43871
under `α=0.01`. At the 850-day input span those upper limits are 0.05785 and
0.16215. These are model-conditional necessary limits, not estimated actual
receiver probabilities. The event is given the entire daily/12-hour false
budget for its one completion; repeated attempts and duration alternatives
would require additional accounting and cannot improve this necessary test.

The JSON also reports necessary C/N0 crossings and an algebraic necessary-span
inversion. For example, under the same stationary teacher indefinitely,
Each65, `B=0.1 Hz`, 7 ms would need at least 67.61 Julian years of pre-selective
support to avoid exclusion of the strict target at −60 dB-Hz. At 5 µs the
bound is 219.43 years. Those spans exceed the simulator's ten-year active
request limit and any demonstrated long-term channel/oscillator validation;
they are **not suggested transmitter durations**.

The finite Gaussian covariance-square sum scales as `T/B` when densely sampled
and the observation is long compared with the finite teacher memory. Therefore
the favorable low-C oracle allowance scales as `C²T/B`, not coherent `CT`.
Lowering waveform rate does not remove this renewing-fading limitation.
Searching a richer likelihood may close implementation gaps inside feasible
regions; it cannot defeat this particular upper bound while its assumptions
hold. A coherent path component or valid side information changes the bound's
premise and remains an appropriate separate experiment.

## Reproduce the cheap checks

From the repository root, using Python 3 with NumPy and SciPy and a C++20
`c++` compiler (or select another compiler with `--compiler`):

```sh
PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 python3 \
  docs/validation-data/triad-v3/2026-11-02-selective-correction/theory/dispersive_information.py \
  --output /tmp/dispersive-information.json
PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 python3 \
  docs/validation-data/triad-v3/2026-11-02-selective-correction/theory/test_dispersive_information.py
```

In the original run, four focused tests passed in 2.85 seconds. They check the cubic contraction
identity, complete-output agreement with the actual C++ helper (maximum error
`6.28e-16` over 24 input/29 output rows), two-sided branch stack bounds,
second/fourth phase moments and reported exclusions. Five nodes per each of six
phases integrate the degree-four polynomial moments exactly in exact
arithmetic; the floating calculations are diagnostics, not exact KL quadrature
or certified interval arithmetic. No long waveform, receiver trial or GPU
acceleration is represented by these tests.

## Standalone recovery and model identity

The 25 November 2026 recovery restores the [calculator](dispersive_information.py),
[finite probe](dispersive_probe.cpp) and [four-test suite](test_dispersive_information.py).
The recorded report remains unchanged. All its original source hashes match
[`ea05d3a8346be7b06a2a53c0e4050e44a963b34b`](https://github.com/mirage335-colossus/pumpModem/tree/ea05d3a8346be7b06a2a53c0e4050e44a963b34b),
including the corrected
[`channel_perturbation.hpp`](https://github.com/mirage335-colossus/pumpModem/blob/ea05d3a8346be7b06a2a53c0e4050e44a963b34b/src/channel_perturbation.hpp)
SHA-256 `fae3b1b345e3dc19c8b85a5010e4434530bd0a34d595dd4de8cd55349d8d879a`.
This is distinct from the legacy all-pass model; the later `357a5097` calculator
and probe bytes are identical, but its channel sources are not the report's
complete source identity.

The probe extracts the historical cubic Lagrange coefficients, actual-tap center
rotation, DFT branch sum and seeded AM phases in their original arithmetic order.
Its 24-input/29-output diagnostic uses a finite zero-extended input vector in
place of a streaming history ring and retains the entire five-sample tail.
The original 4096-sample phase renormalization is never reached. Gaussian
coefficients and phase initialization use the small
[shared mathematical extract](../../2026-11-02-selective/theory/historical_math.hpp);
no receiver, channel runtime or historical source tree is restored.

The calculator retains the unchanged legacy `input_bounds` mathematics and all
corrected operator constants, inversion formulas and margins. It verifies the
same exact historical transmitter-bound report and synthesis identities as the
[legacy recovery](../../2026-11-02-selective/theory/README.md#standalone-recovery-and-model-identity).
Regenerated reports retain that history in `historical_model_commit` and
`historical_source_sha256`, separately from hashes of the current standalone
files in `source_sha256`. The original hash assertion now checks the retained
report against this pinned historical mapping and the actual transmitter-bound
file, instead of requiring deleted modem sources. The helper description labels
the standalone extraction. The original seven-test legacy suite, including its
full-report serialization regression and recorded initial failure, remains
available alongside these four corrected tests.

With Python 3.13, NumPy 2.5.4, SciPy 1.18.1 and one numerical thread, the four
tests passed in 0.739 s. A complete regenerated report matched all 751 numeric
fields exactly on this host, including all 36 exclusions and necessary-span
inversions. The original probe built against its pinned historical headers and
the standalone probe emitted byte-identical JSON (15,068 bytes), including all
three Gaussian kernels. Floating-point roundoff may vary elsewhere; the
independent matrix/probe check retains its `2e-12` absolute tolerance. The
temporary report is for comparison; the original JSON is not overwritten.
