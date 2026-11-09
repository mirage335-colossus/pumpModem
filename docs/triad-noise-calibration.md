# Noise-model research for triad likelihoods

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The retired November 1, 2026 v3 state study exposed a severe false-admission
failure when assumed noise power was too small. A fixed public-Gram diagnostic
with 1,024 noise trials and a fixed gate admitted 0/1,024 at the assumed
variance, 319/1,024 when actual variance was 1.25 times the assumption, and
1,024/1,024 at 1.5 and 2 times. This was a model diagnostic, not an operational
false-admission rate. An earlier incorrectly normalized wrapper was retracted;
the counts above came from its corrected comparison. The raw campaign is
retired; this failure prevents transferring a known-variance guarantee to an
unknown-variance receiver without an applicable model and evidence. The
[diversity results](validation-data/triad-v3/2026-11-01-diversity/README.md)
retain the corresponding model-dependent comparisons. Their alternative
receivers and calibration drivers are removed; the mathematical lessons remain.

## Common-scale angular likelihood

For a fixed completed observation, isotropic noise direction is independent of
one common radial scale. With signal covariance C, R = I + C, E = yᴴy and
Q = yᴴ(I − R⁻¹)y, the directional log likelihood is

`−log det(R) − n log(1 − Q/E)`.

This removes one common unknown isotropic scale. It does not cover different
unknown powers in separate blocks, arbitrary colored interference or adaptive
winner selection. The recorded formulation used actual received energy, never
noiseless simulator energy. Unresolved cancellation near Q/E = 1 must not become
a confident admission.

## Independent residual variance bounds

For a fixed whole scored bank A and its projection P, the residual energy
`yᴴ(I − P)y` has a Gamma law under white proper complex Gaussian noise, with
degrees of freedom determined by the retained bank rank. Residual coordinates
are independent of scored coordinates under that model. A residual chosen after
selecting the winning candidate generally lacks this independence. Zero residual
degrees of freedom is unsupported geometry, not permission to substitute truth.

An independent conservative variance bound can support a normalized likelihood
with null expectation at most one; its failure allowance must be added to the
score allowance. Pooling requires stationary power. Segment-wise bounds permit
more variation but cost information. Numerical rank, truncation and cancellation
must match the actually scored projection.

## Model and admission boundaries

Gaussian evolving-tap and finite phase/frequency mixtures explored different
alternative models. Finite windows or blocks can lose within-block variation and
cross-window covariance. A valid null statement does not guarantee sensitivity
to the physical alternative. Analog filtering, quantization, impulses and unknown
colored noise need an applicable model; a white-noise argument does not survive
them by assertion.

The [native-ADC admission note](triad-physical-admission.md) preserves a separate
conditional physical-noise argument. Neither research branch is a second
current receiver. New intensive checks must use the abbreviated method rather
than restore full noise calibration, sensitivity sweeps or standalone alternatives.
