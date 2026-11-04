#!/usr/bin/env python3
"""Public-statistics likelihoods for unknown white complex noise power.

These are separate receivers, not plug-in modifications to the known-variance
baseline.  ``AngularGaussianLikelihood`` integrates out a common radial scale:
for the fixed declared covariance shape R=I+C, u=y/||y||,

    log LR_angle = -logdet(R) - n*log(u.H @ inv(R) @ u).

It is normalized under isotropic complex Gaussian noise at ANY constant power
(and, more generally, a uniform noise direction). It does not handle arbitrary
block-varying power. ``IndependentResidualGaussianLikelihood`` instead uses
independent whole-bank residual coordinates to upper-bound each constant-power
stationarity segment. Its false-admission bound is alpha_score + delta_noise,
not alpha_score alone. Both methods require fixed completed observation windows;
repeated attempts, durations and model alternatives need the supplied budget.

Receiver inputs contain no simulator signal energy, noiseless means or actual
noise variance. The declared reference variance sets a signal covariance SHAPE
and is not an oracle estimate of the actual noise. FP64 energy, rank decisions
and final evidence are retained; Gaussian state arithmetic remains explicit.

Primary mathematical references:
https://journals.pan.pl/Content/136752/mainFile.pdf (Theorem 4, r=1)
https://www.itl.nist.gov/div898/handbook/prc/section2/prc231.htm
The streaming information-form reuse and family allocation below are direct
derivations, not performance claims from those references.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

import numpy as np
from scipy.special import gammaincinv
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter


def _real_tensor(value, *, device=None, name="value"):
    if isinstance(value, torch.Tensor):
        if value.requires_grad:
            raise ValueError(f"{name} must not require gradients")
        if value.is_complex() or value.dtype == torch.bool:
            raise ValueError(f"{name} must be real")
        # Existing tensors retain their represented values; FP32 rounding at
        # the caller is not silently presented as original FP64 input.
        result = value.to(device=device, dtype=torch.float64)
    else:
        def contains_boolean(item):
            if isinstance(item, (bool, np.bool_)):
                return True
            return isinstance(item, (list, tuple)) and any(contains_boolean(x) for x in item)
        raw = np.asarray(value)
        if raw.dtype.kind not in "iuf" or contains_boolean(value):
            raise ValueError(f"{name} must be real numeric data without booleans")
        # Setting dtype at construction is essential: torch's default dtype
        # otherwise rounds Python floats/lists to FP32 before this upcast.
        result = torch.as_tensor(raw, dtype=torch.float64, device=device)
    if not bool(torch.isfinite(result).all()):
        raise ValueError(f"{name} must be finite")
    return result


def _positive_int(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or not 0 < value <= 2**53:
        raise ValueError(f"{name} must be an integer in 1..2**53")
    return value


def angular_log_likelihood(log_determinant, quadratic, received_energy,
                           complex_samples: int, *, relative_tolerance=1e-10):
    """Evaluate the normalized common-scale angular Gaussian likelihood.

    ``quadratic = y.H @ (I-inv(R)) @ y`` and energy=||y||² must use the
    SAME declared reference normalization. Their ratio removes actual amplitude.
    ``complex_samples`` counts valid complex raw rows, including coordinates
    outside the pattern subspace. It is not a fitted rank or coefficient count.
    Material inconsistencies and unresolved near-unit cancellation fail closed.
    """
    n = _positive_int(complex_samples, "complex_samples")
    if not math.isfinite(relative_tolerance) or not 0 < relative_tolerance < .01:
        raise ValueError("invalid angular numerical tolerance")
    q = _real_tensor(quadratic, name="angular quadratic")
    f = _real_tensor(log_determinant, device=q.device, name="log determinant")
    energy = _real_tensor(received_energy, device=q.device, name="received energy")
    f, q, energy = torch.broadcast_tensors(f, q, energy)
    if bool((energy <= 0).any()) or bool((f < -relative_tolerance).any()):
        raise ValueError("angular likelihood requires positive energy and nonnegative log determinant")
    if bool((q < -relative_tolerance * energy).any()):
        raise ValueError("angular quadratic is materially negative")
    q = q.clamp_min(0.)
    # Never clip a ratio at one: doing so could manufacture very large evidence.
    residual = energy-q
    if bool((residual <= 0).any()):
        raise ValueError("angular residual energy is nonpositive; use a more accurate raw reference")
    if bool((residual <= relative_tolerance*energy).any()):
        raise ValueError("angular residual subtraction is unresolved; use a stable raw residual reference")
    result = -f - n * torch.log1p(-q / energy)
    if not bool(torch.isfinite(result).all()):
        raise ValueError("angular likelihood became nonfinite")
    return result


class AngularGaussianLikelihood:
    """Streaming sufficient statistics for one fixed common-scale decision.

    update(r,G,dt,E,n) uses r=A.H@y/v_ref, G=A.H@A/v_ref, E=||y||²/v_ref.
    ``v_ref`` is fixed in advance and need not equal actual noise power. To keep
    shape R fixed, do not change v_ref between blocks. Energy must broadcast to
    the score shape, e.g. [trials,1] for r[trials,candidates,K]. The same actual
    energy and raw sample count must be used for every candidate of a capture.
    A zero-row erasure advances the channel state with zero r/G/E.
    """

    def __init__(self, prior: GaussianPrior, *, dtype=torch.complex128,
                 check_inputs=True, relative_tolerance=None):
        kwargs = dict(dtype=dtype, check_inputs=check_inputs,
                      relative_tolerance=relative_tolerance)
        self.signal = GaussianStateFilter(prior, **kwargs)
        self.design = GaussianStateFilter(prior, **kwargs)
        self.received_energy = None
        self._energy_compensation = None
        self._amplitude_normalizer = None
        self.complex_samples = 0

    def update(self, correlations, gram, dt_seconds: float,
               received_energy, valid_rows: int):
        if isinstance(valid_rows, bool) or not isinstance(valid_rows, int) or not 0 <= valid_rows <= 2**53:
            raise ValueError("valid_rows must be an integer in 0..2**53")
        if not isinstance(correlations, torch.Tensor) or not isinstance(gram, torch.Tensor):
            raise TypeError("angular observations and Grams must be Torch tensors")
        if correlations.requires_grad or gram.requires_grad:
            raise ValueError("angular inference inputs must not require gradients")
        energy = _real_tensor(received_energy, device=correlations.device, name="received energy")
        if bool((energy < 0).any()):
            raise ValueError("received energy must be nonnegative")
        try:
            expanded = torch.broadcast_to(energy, correlations.shape[:-1])
        except RuntimeError as error:
            raise ValueError("received energy does not broadcast to the score shape") from error
        if valid_rows == 0 and (bool((expanded != 0).any()) or bool((correlations != 0).any()) or bool((gram != 0).any())):
            raise ValueError("zero valid rows require exactly zero energy, observations and Gram")
        if valid_rows > 0 and bool((expanded <= 0).any()):
            raise ValueError("positive valid rows require positive actual energy")
        if self.complex_samples + valid_rows > 2**53:
            raise ValueError("angular sample count exceeds exact FP64 integer range")
        if self.received_energy is None:
            self.received_energy = torch.zeros_like(expanded)
            self._energy_compensation = torch.zeros_like(expanded)
        elif expanded.shape != self.received_energy.shape:
            raise ValueError("angular observation leading shape changed")
        if valid_rows and self._amplitude_normalizer is None:
            # Rescale y ONCE, never G or its shape prior. This is an algebraic
            # use of angular scale invariance, not a plug-in noise estimate.
            # It prevents cancellation in Q=logLR+logdet for tiny ADC units.
            self._amplitude_normalizer = math.sqrt(valid_rows)/expanded.sqrt()
        if self._amplitude_normalizer is not None:
            correlations = correlations * self._amplitude_normalizer[..., None]
            # Ordered products avoid squaring a very large/small normalizer.
            expanded = expanded * self._amplitude_normalizer * self._amplitude_normalizer
        # Compensated FP64 positive-energy accumulation over hour/day streams.
        adjusted = expanded - self._energy_compensation
        total = self.received_energy + adjusted
        self._energy_compensation = (total - self.received_energy) - adjusted
        self.received_energy = total
        self.complex_samples += valid_rows
        self.signal.update(correlations, gram, dt_seconds)
        zero = torch.zeros(gram.shape[:-1], dtype=gram.dtype, device=gram.device)
        self.design.update(zero, gram, dt_seconds)
        return None if self.complex_samples == 0 else self.scores()

    def scores(self):
        f = -self.design.scores()
        q = self.signal.scores() + f
        return angular_log_likelihood(f, q, self.received_energy,
                                      self.complex_samples,
                                      relative_tolerance=self.signal.relative_tolerance)

    def finish(self):
        self.signal.finish()
        self.design.finish()
        return self.scores()

    def diagnostics(self):
        signal, design = self.signal.diagnostics(), self.design.diagnostics()
        return {"blocks": self.signal.blocks, "complex_samples": self.complex_samples,
                "state_dtype": signal["state_dtype"], "energy_dtype": "torch.float64",
                "owned_state_bytes": signal["owned_state_bytes"] + design["owned_state_bytes"] +
                    self.received_energy.numel() * 24,
                "conditioning": "one fixed observed-amplitude rescaling of y only; declared G/covariance shape unchanged",
                "scope": "normalized fixed-window angular Gaussian shape likelihood; common isotropic scale only; not block-varying or anytime calibrated"}


@dataclass(frozen=True)
class WholeBankResidual:
    energy: torch.Tensor
    complex_dof: torch.Tensor
    rank: torch.Tensor
    projected_correlations: torch.Tensor
    projected_gram: torch.Tensor
    projected_energy: torch.Tensor
    valid_rows: int


def whole_bank_residual(correlations, gram, received_energy, valid_rows: int, *,
                        rank_rtol=1e-10, rank_atol=0., numerical_tolerance=1e-10):
    """Extract independent residual coordinates from the ENTIRE scored bank.

    All inputs are raw or normalized by the same fixed reference variance.
    Returns a frozen numerical projection. Every receiver must score submatrices
    of returned projected_r/G, including any tiny positive modes removed by the
    rank policy; scoring removed modes destroys residual independence. The bank
    must include all timing/frequency/model alternatives sharing this residual.

    E_res = E_total-r.H@G⁺@r is cancellation sensitive. This compact exporter
    path fails closed when positive residual DOF have unresolved residual energy;
    raw QR on the actual valid rows is the independent high-SNR reference.
    """
    if isinstance(valid_rows, bool) or not isinstance(valid_rows, int) or not 0 <= valid_rows <= 2**53:
        raise ValueError("valid_rows must be an integer in 0..2**53")
    n = valid_rows
    if not isinstance(correlations, torch.Tensor) or not isinstance(gram, torch.Tensor):
        raise TypeError("whole-bank observations and Grams must be Torch tensors")
    if correlations.requires_grad or gram.requires_grad:
        raise ValueError("whole-bank statistics must not require gradients")
    if correlations.device != gram.device or correlations.ndim < 1 or gram.shape[-2:] != (correlations.shape[-1],) * 2:
        raise ValueError("whole-bank observation/Gram shape or device mismatch")
    if any(not math.isfinite(x) or x < 0 for x in (rank_rtol, rank_atol)) or not 0 < numerical_tolerance < .01:
        raise ValueError("invalid whole-bank rank or numerical tolerance")
    r, G = correlations.to(torch.complex128), gram.to(torch.complex128)
    if not bool(torch.isfinite(r).all()) or not bool(torch.isfinite(G).all()):
        raise ValueError("whole-bank statistics must be finite")
    try:
        if torch.broadcast_shapes(r.shape[:-1], G.shape[:-2]) != r.shape[:-1]:
            raise ValueError("whole-bank Gram expands observation shape")
    except RuntimeError as error:
        raise ValueError("whole-bank observation and Gram do not broadcast") from error
    energy = _real_tensor(received_energy, device=r.device, name="received energy")
    try:
        energy = torch.broadcast_to(energy, r.shape[:-1])
    except RuntimeError as error:
        raise ValueError("whole-bank energy does not broadcast to observations") from error
    if bool((energy < 0).any()):
        raise ValueError("received energy must be nonnegative")
    if n == 0 and (bool((r != 0).any()) or bool((G != 0).any()) or bool((energy != 0).any())):
        raise ValueError("zero valid rows require exactly zero whole-bank statistics")
    scale = G.abs().amax(dim=(-2, -1)).clamp_min(torch.finfo(torch.float64).tiny)
    if bool(((G-G.mH).abs().amax(dim=(-2, -1)) > numerical_tolerance*scale).any()):
        raise ValueError("whole-bank Gram is not Hermitian")
    values, vectors = torch.linalg.eigh((G + G.mH) * .5)
    if bool((values[..., 0] < -numerical_tolerance*scale).any()):
        raise ValueError("whole-bank Gram is not positive semidefinite")
    threshold = torch.maximum(values[..., -1].clamp_min(0.) * rank_rtol,
                              torch.full_like(values[..., -1], rank_atol))
    keep = values > threshold[..., None]
    rank = keep.sum(-1)
    if bool((rank > n).any()):
        raise ValueError("whole-bank numerical rank exceeds valid row count")
    coefficient = (vectors.mH @ r[..., None]).squeeze(-1)
    kept_coefficient = torch.where(keep, coefficient, 0.)
    roots = torch.where(keep, values, 1.).sqrt()
    projected_energy = (kept_coefficient / roots).abs().square().sum(-1)
    projected_r = (vectors @ kept_coefficient[..., None]).squeeze(-1)
    projected_G = (vectors * torch.where(keep, values, 0.)[..., None, :]) @ vectors.mH
    if not bool(torch.isfinite(projected_energy).all()) or not bool(torch.isfinite(projected_r).all()) or not bool(torch.isfinite(projected_G).all()):
        raise ValueError("whole-bank projection overflowed; use a scaled raw QR reference")
    residual = energy - projected_energy
    roundoff = numerical_tolerance * torch.maximum(energy, projected_energy).clamp_min(torch.finfo(torch.float64).tiny)
    if bool((residual < -roundoff).any()):
        raise ValueError("whole-bank projected energy exceeds actual received energy")
    dof = n - rank
    if bool(((dof > 0) & (residual <= roundoff)).any()):
        raise ValueError("whole-bank residual subtraction is unresolved; use raw QR energy")
    residual = residual.clamp_min(0.)
    return WholeBankResidual(residual, dof, rank, projected_r, projected_G,
                             projected_energy, n)


def variance_upper_confidence(residual_energy, complex_dof, delta: float):
    """One-sided exact Gaussian variance UCB from independent residuals.

    For M independent proper-complex coordinates, E/σ²~Gamma(M,1), hence
    v_upper=E/GammaPPF(delta,M). DOF must be actual complex residual coordinates,
    never total rows after a fitted mean/rank has consumed some of them.
    """
    if not math.isfinite(delta) or not 0 < delta < 1:
        raise ValueError("variance failure probability must lie strictly between zero and one")
    energy = _real_tensor(residual_energy, name="residual energy")
    dof = _real_tensor(complex_dof, device=energy.device, name="complex residual dof")
    if bool((energy < 0).any()) or bool(((dof <= 0) | (dof != dof.floor()) | (dof > 2**53)).any()):
        raise ValueError("residual energy must be nonnegative and complex dof positive integral")
    quantile = gammaincinv(dof.cpu().numpy(), delta)
    if not np.isfinite(quantile).all() or np.any(quantile <= 0):
        raise ValueError("variance confidence quantile is unresolved")
    return energy / torch.as_tensor(quantile, dtype=torch.float64, device=energy.device)


@dataclass(frozen=True)
class FalseAdmissionBudget:
    """Bonferroni allocation for a predeclared fixed-window decision family.

    Calibration is shared across the whole scored candidate/model bank, so it
    is charged per attempt/duration/stationarity segment, not per candidate.
    """
    false_probability: float = 1e-4
    calibration_fraction: float = .5
    attempts: int = 1440
    duration_alternatives: int = 1
    model_alternatives: int = 1
    candidates: int = 2
    calibration_segments: int = 1

    def __post_init__(self):
        if not math.isfinite(self.false_probability) or not 0 < self.false_probability < 1:
            raise ValueError("false probability must lie strictly between zero and one")
        if not math.isfinite(self.calibration_fraction) or not 0 < self.calibration_fraction < 1:
            raise ValueError("calibration fraction must lie strictly between zero and one")
        for name in ("attempts", "duration_alternatives", "model_alternatives", "candidates", "calibration_segments"):
            _positive_int(getattr(self, name), name)

    @property
    def calibration_probability(self):
        return self.false_probability * self.calibration_fraction

    @property
    def score_probability(self):
        return self.false_probability - self.calibration_probability

    @property
    def delta_per_segment(self):
        allocation = self.calibration_probability / (self.attempts*self.duration_alternatives*self.calibration_segments)
        allocation = math.nextafter(allocation, 0.)
        if allocation <= 0:
            raise ValueError("calibration family budget underflow")
        return allocation

    @property
    def log_threshold(self):
        result = (math.log(self.attempts) + math.log(self.duration_alternatives) +
                  math.log(self.model_alternatives) + math.log(self.candidates) -
                  math.log(self.score_probability))
        return math.nextafter(result, math.inf)

    def diagnostics(self):
        return {"score_probability": self.score_probability,
                "calibration_failure_probability": self.calibration_probability,
                "total_false_admission_bound": self.false_probability,
                "delta_per_segment": self.delta_per_segment,
                "fixed_window_log_threshold": self.log_threshold,
                "scope": "predeclared family union bound; shared independent whole-bank calibration; no adaptive/anytime extension"}


def normalize_by_variance_upper(raw_correlations, raw_gram, variance_upper):
    """Whiten by a supplied independent UCB; never estimate scale from r itself."""
    if not isinstance(raw_correlations, torch.Tensor) or not isinstance(raw_gram, torch.Tensor):
        raise TypeError("normalization requires Torch observations and Grams")
    if raw_correlations.requires_grad or raw_gram.requires_grad:
        raise ValueError("normalization inputs must not require gradients")
    v = _real_tensor(variance_upper, device=raw_correlations.device, name="variance upper bound")
    if bool((v <= 0).any()):
        raise ValueError("variance upper bound must be positive")
    try:
        v = torch.broadcast_to(v, raw_correlations.shape[:-1])
    except RuntimeError as error:
        raise ValueError("variance upper bound does not broadcast to scores") from error
    return raw_correlations / v[..., None], raw_gram / v[..., None, None]


class IndependentResidualGaussianLikelihood:
    """Conservative Gaussian LR with independent segment-wise variance UCBs.

    Each update is one stationarity segment: noise may change between updates,
    but is white proper Gaussian with constant unknown variance WITHIN each.
    Its residual coordinates must be independent of every scored feature and
    all residual pools must be mutually independent under H0. A global pooled
    calibration cannot upper-bound a changing variance. Raw observations/Grams
    and residual energies must use the same physical units.

    On the event all UCBs cover truth, the conditional null expectation of LR
    is at most one (whitened actual covariance <= I). Union over failed UCBs
    contributes the explicitly allocated delta. Using the same scored samples
    for an ordinary plug-in variance estimate has no such guarantee.
    """

    def __init__(self, prior: GaussianPrior, budget: FalseAdmissionBudget, *,
                 dtype=torch.complex128, check_inputs=True, relative_tolerance=None):
        self.filter = GaussianStateFilter(prior, dtype=dtype, check_inputs=check_inputs,
                                          relative_tolerance=relative_tolerance)
        self.budget = budget
        self.segments = 0

    def update(self, raw_correlations, raw_gram, dt_seconds: float, *,
               residual_energy, complex_dof):
        if not isinstance(raw_correlations, torch.Tensor) or not isinstance(raw_gram, torch.Tensor):
            raise TypeError("independent-variance observations and Grams must be Torch tensors")
        dof = _real_tensor(complex_dof, device=raw_correlations.device, name="complex residual dof")
        if bool((dof == 0).all()):
            energy = _real_tensor(residual_energy, device=raw_correlations.device, name="residual energy")
            if bool((energy != 0).any()) or bool((raw_correlations != 0).any()) or bool((raw_gram != 0).any()):
                raise ValueError("zero residual dof permit only a complete zero-information erasure")
            G = torch.broadcast_to(raw_gram, (*raw_correlations.shape[:-1], *raw_gram.shape[-2:]))
            return self.filter.update(raw_correlations, G, dt_seconds)
        if self.segments >= self.budget.calibration_segments:
            raise ValueError("variance calibration segment allocation exhausted")
        upper = variance_upper_confidence(residual_energy, complex_dof,
                                          self.budget.delta_per_segment)
        # A residual is shared across the complete candidate axis. For the
        # canonical r[trials,candidates,K], accept Ecal[trials] directly.
        if raw_correlations.ndim >= 2 and tuple(upper.shape) == tuple(raw_correlations.shape[:-2]):
            upper = upper[..., None]
        r, G = normalize_by_variance_upper(raw_correlations, raw_gram, upper)
        result = self.filter.update(r, G, dt_seconds)
        self.segments += 1
        return result

    def scores(self):
        return self.filter.scores()

    def finish(self):
        return self.filter.finish()

    def diagnostics(self):
        result = self.filter.diagnostics()
        result.update(self.budget.diagnostics())
        result.update({"variance_segments": self.segments,
                       "delta_charged_per_attempt": self.segments*self.budget.delta_per_segment,
                       "scope": "conservative fixed-window Gaussian LR plus independent whole-bank residual UCB failure allocation; constant white variance per segment"})
        return result
