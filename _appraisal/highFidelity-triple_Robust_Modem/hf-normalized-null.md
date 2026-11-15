# Normalized covariance and wrong-key evidence

This is a bounded diagnostic, not a receiver decision rule. It investigates an
alternative left open by the [fixed-physical-prior counterexample](hf-predictive-completion.md#rejected-directional-data-veto).
That counterexample does not invalidate every angular statistic: making the
covariance shape independent of noise variance avoids its divergence to minus
infinity as noise falls. Fixed-shape directional evidence still saturates as
signal magnitude grows, and an arbitrary structured wrong-key observation is
not a spherical null.

## Frozen model and numerical checks

The [native diagnostic](src/hf_normalized_null.cpp) applies one fixed public
orthonormal DATA-band projection to both observations and templates. It uses
the native fine-block geometry, basis generation, FFT and Gaussian kernels.
The template matrix `A` is block diagonal: each disjoint fine block has its
own one- or two-tap gain vector. Its whole-aperture rank `r` is the sum of
block ranks, not at most two. With total public template Gram trace `g`, the
fixed shape bank is

`V(c) = I + (c*r/g) A A*`, for `c = {1/16, 1/4, 1, 4, 16, 64}`.

These shapes depend on public templates, not observed energy or estimated noise
power. All six contrasts have fixed equal weight. At most twelve deduplicated
native public proposals retain their original full-lattice prior mass; their
weights are not renormalized after selection. The same projected statistics
also feed a known-white Gaussian comparison with the existing physical gain
powers. The correct-bit Gaussian comparison must explain the native positive
before a negative directional result is interpreted as a wanted-model failure.

The complex angular density follows
[Ito, Araki and Nakatani (2016), Eq. 10](https://www.eurasip.org/Proceedings/Eusipco/Eusipco2016/papers/1570256519.pdf).
Fixed-mixture validity still requires valid components for the specified null;
see [Grünwald, de Heide and Koolen (2024)](https://academic.oup.com/jrsssb/article/86/5/1091/7623686).
Neither result establishes that wrong-key HF signals have spherical directions.

The report additionally gives an optimized **isotropic-projector** diagnostic:
for dimension `d`, rank `r` and energy fraction `eta` in that span, the best
scalar contrast has log ratio zero when `eta <= r/d`, and otherwise
`d*KL(r/d || eta)`. This is not an upper bound over arbitrary unequal-eigenvalue
covariances, and its observed-direction optimization is not an operational
e-value. Near-singular rank cases remain explicitly unavailable.

Native component checks pass for noise-scale, observation phase/amplitude and
template-scale invariance; the scalar-projector formula; and violations under
non-spherical nulls. A separate two-block, two-tap test compares the actual
scorer with a direct complex 6×6 covariance factorization at every frozen
contrast and physical power. It also verifies that a single shared gain vector
across blocks would be a different model. Source review caught a conflicting
generic CLI restriction before execution; only the approved diagnostic
wrong-key mode was exempted. Unscoped wrong-key slot and diagnostic noise
commands remain refused.

## First bounded aperture pair

Both runs use the first `hf-disturbed` aperture, seed 9201, C/N0 20 dB-Hz,
reference waveform, Helper/FEC off and the native CPU receiver. They have the
same public geometry and noise seed, but distinct emitted keyed waveforms.
Each observation is first processed normally; the receiver is closed before
the diagnostic creates its executor. Full `W=24`, live memory, the copied
aperture and extra diagnostic work are reported. This repeats no persistent
receiver search in the operating application.

| Observation | Native decision | Normalized bit log ratios | Same-model known-white log ratios |
| --- | --- | --- | --- |
| Wanted | Correct DATA, native log evidence 42.563 | 53.809, −8.406 | 49.884, 2.507 |
| Wrong key | Ambiguous: native bit evidence too close | −8.194, −6.701 | 3.657, 5.642 |

The same-model positive supports relevance to the wanted signal. The wrong-key
row already abstains natively, however: this pair **does not demonstrate a
repair of false DATA**. Scores from different projection/model families are
not sensitivity gains in dB. The shared diagnostic has 13,831 complex modes
across 95 fine blocks. Its extra coarse/projection work takes approximately
0.56 and 0.44 seconds; complete aperture runs take 1.68 and 3.72 seconds.
No throughput or low-SNR qualification follows.

The selected IQ/validity hashes are
`ba7d80479ecea07c0d048d5d0621ce1038a7cb2500eb1aa28d2285d15fb16db6`
and `154875b20041a40b0223c5a83714204044959b1eacf91e1d8b3400c31a3bdb10`.
Raw receipts and command/binary hashes are in `local-evidence/hf-normalized-null/`,
bound to appraisal source
`a9d1e09dc4295d99d9f6e5b3f027cf18acef07898a8abd93b09f43faa051f4de`
and unchanged native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.

## Reproduced first-slot false DATA

The protected probe renders through the end of its first public observation,
sample 79,203 at 1,920 complex samples/s. It keeps the original RS20 `B`
source, transmitter/receiver key selection, public calendar and static
receiver configuration from the full 8,488,803-sample experiment. Both runs
use path seed `5932140317584679278`, noise seed `5932140317803442018`, complex
noise variance `0.0001`, 10.4 s actual bit duration, public epoch `1793850030`
and symbol ordinal `741`. The canonical receiver scores the complete first
slot and closes before the diagnostic starts. The probe asserts identity
between the buffered IQ/validity hash and the native slot input hash in each
run. The wanted and wrong-key waveforms have different hashes. This prefix
experiment does not assess message completion. [Original full probe](hf-protected-reassembly.md)

| Observation | Native bit and operational log evidence | Fixed normalized bit log ratios | Same-model known-white bit log ratios |
| --- | --- | --- | --- |
| Wanted | Correct DATA 0, 194,740,790 | 3,424.036, 46.786 | 194,383,989, 31,809,735 |
| Wrong key | False DATA 0, 15,934,599 | 0.859, 3.155 | 14,571,971, 15,962,178 |

Each shadow uses the same 853-mode DATA projection, 45 disjoint blocks, 37.5 Hz
retained half-band and three of 13 public epoch coordinates at their original
weights. For the strongest wrong-key directional component the rank is 43
and the template span contains 8.13% of the projected received energy. The
raw optimized isotropic-projector log ratio is 6.232 for that component, while
its strongest fixed covariance yields 7.380 before prior weighting. The
block template energies give this covariance unequal eigenvalues, outside
the scalar-projector optimization family.
The strongest wanted component contains 99.9996% of projected energy and has
the same rank. Overall projected energies are similar, about 19,439 and
19,637 for wanted and wrong key. The known-white comparison preserves the
large wrong-key evidence despite the small explained fraction. The directional
score exposes that dependence on waveform fit in this observation. The
known-white shadow prefers bit 1 in the wrong-key case, while its native
receiver declares bit 0. The two scorers use different likelihood models.
Applying the native `21.493` threshold to the normalized statistic would
require its own calibration and sensitivity study.

The full native observation is `[57590, 79203)`, 21,613 valid rows. The
common orthogonal DATA projection uses `[57600, 79141)`. The selected
observation hashes are
`499aeb7f4e92903f9dd97c1ef1efaf7e409d2ae9fb7474842e39f02c99f45989`
for wanted and
`4904b9f1a0ca21b68b38b3eada96ea2dd1d90194ab5d2cdcc05e44bfb26b4e2e`
for wrong key. The raw receipts and exact commands are preserved in
`local-evidence/hf-normalized-null/protected-*-first-slot.stdout.json` and
`protected-prefix-execution.json`. Probe binary SHA-256 is
`a1564c7b09772c009ae9a9db40489543dcaed22ba8fafbab9a0132cd30adace4`, appraisal
source SHA-256 is `c449841259afb8eee787979b2d58f063569b5ae9c66997914f3da52a005cbff8`
and native source SHA-256 is
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.

Complete prefix runs take 0.491 and 0.492 seconds of external wall time. The
extra shadow takes 0.0713 and 0.0737 seconds, including one coarse pass each.
Native execution uses 24 workers on 12 effective logical CPUs. The shadow
retains the same worker allocation, with three independent scoring lanes for
the three selected coordinates. The copy reserves 8 MiB under its hard
524,288-row limit. The shadow quotes about 99.6 MB for projection, templates,
statistics and numerical work above its process/stack reserve. These timings
describe one unusually simple, strong prefix and exclude sustained backlog.

## Limits and next discriminator

A hard directional veto still has a high-SNR failure mode. As additive noise
falls, the wanted observation approaches a fixed direction. If an ionospheric
channel puts most of that signal outside the admitted waveform subspace, the
known-white coherent evidence can grow while the directional score approaches
a finite value below an acceptance threshold. Even the optimized scalar
projector has a zero ceiling when its explained energy fraction is at most
`r/d`. The two aperture pairs establish neither a threshold nor a tradeoff
that avoids this failure. The protected probe also leaves the native false
DATA decision visible in its receipt.

The noise and front-end reference separates calibrated port PSD, event marks,
time/frequency covariance and receiver distortion. Its independent-episode
split also rules out treating multiple windows or changed noise seeds on one
path as independent disturbed days. The structured wrong-key waveform, a
receiver transient and a colored background can violate the spherical null
for different reasons. A covariance estimate trained only on quiet gaps would
require evidence that it also represents the scored slot. [Noise and equipment reference](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md) · [Covariance mismatch analysis](https://arxiv.org/abs/2012.01182)

A possible follow-up to a specified nuisance/null model would compare the recorded oblique `A` wrong-key
slot 11, which already produces false DATA at 20 dB-Hz, with the corresponding
wanted aperture and a weaker wanted control that still passes native DATA.
Bind each observation to its original digest and require correct-bit
separation in the same-model known-white positive before interpreting a
directional loss. If that family fails the desired positive as the trajectory
changes, a useful next discriminator is keyed prediction on held-out modes.
Select timing, frequency and bounded channel gains on a disjoint fitting
subset, then compare predictions of untouched modes under the configured key.
Training on the held-out sample or reusing full-IQ-selected proposals would
invalidate that check. At low signal energy and under changing channels the
fit can provide too little evidence even for the correct key. The proposed
comparison remains diagnostic until fresh disturbed-channel positive and
negative controls establish a defensible operating rule and complete-receiver
cost. [Oblique campaign](campaigns/hf-vector-oblique-boundary.json)

That transfer check is not an unexecuted acceptance gate for the current
completion/reassembly candidate. No operational normalized veto is proposed:
the demonstrated high-SNR saturation and the missing calibrated structured
null remain unresolved by additional descriptive scores. The old oblique
receipts retain configuration and digests, not reusable IQ; a transfer run
would require rendering again and extending the current first-aperture
`hf-disturbed` diagnostic admission. It remains a concrete research option
when a proposed model makes its result actionable.

Keep the normalized diagnostic and these small decisive receipts while testing
transfer of the observed separation. When superseded, preserve its scoped
findings and remove the unused implementation, tests, and redundant captures.
