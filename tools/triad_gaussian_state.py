#!/usr/bin/env python3
"""Streaming marginal likelihood for a proper complex Gaussian tap process.

The observation interface is the information form of a sampled linear model:
    r = A.H @ y / noise_variance, G = A.H @ A / noise_variance.
Under noise alone r ~ CN(0, G). G may be singular. An update integrates the
complex tap coefficients; it never estimates or divides by an observed gain.
The normalized likelihood is exact for this declared blockwise Gaussian model.
It is an approximation to constant-modulus oscillators and other physical
channels, whose sensitivity must be measured independently.
"""
from __future__ import annotations

from dataclasses import dataclass
import cmath
import math
from typing import Sequence

import torch


@dataclass(frozen=True)
class GaussianPrior:
    """Fixed physical coefficient prior, selected before observing samples.

    Each tap has stationary variance ``stationary_power[k]``. Between block
    centers, F = exp(-dt/coherence_seconds) exp(j*2*pi*frequency_hz*dt) I and
    Q = (1-exp(-2*dt/coherence_seconds)) diag(stationary_power). Infinite
    coherence gives unknown static Gaussian coefficients with common rotation.
    Tap Dopplers already present in A must not be counted a second time here.
    """

    stationary_power: tuple[float, ...] = (.5, .5)
    coherence_seconds: float = 128.
    frequency_hz: float = 0.

    def __post_init__(self):
        power = tuple(float(value) for value in self.stationary_power)
        if not power or len(power) > 16:
            raise ValueError("Gaussian tap prior needs 1..16 coefficient powers")
        if any(not math.isfinite(value) or value < 0 for value in power):
            raise ValueError("Gaussian coefficient powers must be finite and nonnegative")
        if not (self.coherence_seconds > 0) or math.isnan(self.coherence_seconds):
            raise ValueError("Gaussian coherence must be positive, or positive infinity")
        if not math.isfinite(self.frequency_hz):
            raise ValueError("Gaussian frequency must be finite")
        object.__setattr__(self, "stationary_power", power)


class GaussianStateFilter:
    """Bounded-memory forward likelihood with unknown complex coefficients.

    ``update`` accepts r[...,K], G[...,K,K], and elapsed seconds since the
    preceding block center. For the first block use dt=0; the zero-mean
    stationary initial prior is unchanged by an initial unobserved gap.
    For example r[trials,bits,K] and G[bits,K,K] share the deterministic
    covariance recursion across trials. The leading shapes must stay fixed.

    All-zero observation blocks advance the prior and add zero evidence.
    Every method/duration/acquisition alternative still needs an operational
    admission policy outside this component. No anytime admission is provided.

    check_inputs=False is only for a workspace that already validated its
    deterministic Grams and observations. ``finish`` always checks finite
    output and accumulated covariance diagnostics. State uses the selected
    complex dtype; cumulative log likelihood is always FP64.
    """

    def __init__(self, prior: GaussianPrior, *, dtype=torch.complex128,
                 check_inputs: bool = True, relative_tolerance: float | None = None):
        if dtype not in (torch.complex64, torch.complex128):
            raise ValueError("Gaussian state arithmetic must be complex64 or complex128")
        tolerance = (2e-5 if dtype == torch.complex64 else 1e-10) if relative_tolerance is None else float(relative_tolerance)
        if not math.isfinite(tolerance) or not 0 < tolerance < .01:
            raise ValueError("invalid Gaussian covariance tolerance")
        self.prior = prior
        self.dtype = dtype
        self.check_inputs = bool(check_inputs)
        self.relative_tolerance = tolerance
        self.mean = None
        self.covariance = None
        self.log_likelihood_ratio = None
        self.blocks = 0
        self._power = None
        self._identity = None
        self._r_shape = None
        self._g_shape = None
        self._device = None
        self._worst_relative_covariance_eigenvalue = None
        self._largest_relative_gram_repair = None

    @property
    def taps(self):
        return len(self.prior.stationary_power)

    def _validate_gram(self, gram):
        scale = gram.abs().amax(dim=(-2, -1)).clamp_min(1.)
        asymmetry = (gram - gram.mH).abs().amax(dim=(-2, -1))
        if bool((asymmetry > self.relative_tolerance * scale).any()):
            raise ValueError("Gaussian observation Gram is not Hermitian")
        hermitian = (gram + gram.mH) * .5
        values, vectors = torch.linalg.eigh(hermitian)
        if bool((values[..., 0] < -self.relative_tolerance * scale).any()):
            raise ValueError("Gaussian observation Gram is not positive semidefinite")
        # A roundoff-sized negative Gram eigenvalue is reported, rather than
        # being accepted as a negative observation energy in the likelihood.
        repair = (-values[..., 0]).clamp_min(0.) / scale
        self._largest_relative_gram_repair = torch.maximum(
            self._largest_relative_gram_repair, repair.amax().to(torch.float64))
        if bool((values < 0).any()):
            hermitian = (vectors * values.clamp_min(0.)[..., None, :]) @ vectors.mH
        return hermitian

    def _initialize(self, correlations, gram):
        self._r_shape = tuple(correlations.shape)
        self._g_shape = tuple(gram.shape)
        self._device = correlations.device
        power = torch.tensor(self.prior.stationary_power, dtype=correlations.real.dtype,
                             device=self._device)
        self._power = torch.diag_embed(power).to(self.dtype)
        self._identity = torch.eye(self.taps, dtype=self.dtype, device=self._device)
        self.mean = torch.zeros_like(correlations)
        self.covariance = self._power.expand(gram.shape).clone()
        self.log_likelihood_ratio = torch.zeros(correlations.shape[:-1],
                                                dtype=torch.float64, device=self._device)
        self._worst_relative_covariance_eigenvalue = torch.zeros((), dtype=torch.float64, device=self._device)
        self._largest_relative_gram_repair = torch.zeros((), dtype=torch.float64, device=self._device)

    def initialize_posterior(self, mean, covariance):
        """Start from a normalized posterior obtained from preceding data.

        The caller owns the preceding likelihood ratio.  This filter starts a
        fresh *incremental* likelihood at zero while retaining its channel
        information.  This is useful when public timing states change between
        blocks; it is not permission to fit an initial prior on the same data
        that will subsequently be scored.  Inputs are copied and validated.
        """
        if self.mean is not None or self.blocks:
            raise ValueError("Gaussian posterior can only initialize an unused filter")
        if not isinstance(mean, torch.Tensor) or not isinstance(covariance, torch.Tensor):
            raise TypeError("Gaussian posterior mean and covariance must be Torch tensors")
        if mean.requires_grad or covariance.requires_grad:
            raise ValueError("Gaussian posterior inputs must not require gradients")
        if (mean.ndim < 1 or mean.shape[-1] != self.taps or
                covariance.shape != mean.shape[:-1] + (self.taps, self.taps) or
                mean.device != covariance.device):
            raise ValueError("Gaussian posterior extents or device differ")
        mean = mean.to(self.dtype)
        covariance = covariance.to(self.dtype)
        if not bool(torch.isfinite(mean).all()) or not bool(torch.isfinite(covariance).all()):
            raise ValueError("Gaussian posterior contains nonfinite values")
        scale = covariance.abs().amax(dim=(-2, -1)).clamp_min(1.)
        if bool(((covariance-covariance.mH).abs().amax(dim=(-2, -1)) >
                 self.relative_tolerance*scale).any()):
            raise ValueError("Gaussian posterior covariance is not Hermitian")
        covariance = (covariance+covariance.mH)*.5
        eigenvalues = torch.linalg.eigvalsh(covariance)
        if bool((eigenvalues[..., 0] < -self.relative_tolerance*scale).any()):
            raise ValueError("Gaussian posterior covariance is not positive semidefinite")
        self._initialize(mean, covariance)
        self.mean = mean.clone()
        self.covariance = covariance.clone()

    def posterior(self):
        """Return owned copies of the normalized current coefficient state."""
        self.finish()
        return self.mean.clone(), self.covariance.clone()

    def update(self, correlations, gram, dt_seconds: float):
        """Consume one complete block and return its FP64 log-LR increment."""
        dt = float(dt_seconds)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("Gaussian elapsed block time must be finite and nonnegative")
        if not isinstance(correlations, torch.Tensor) or not isinstance(gram, torch.Tensor):
            raise TypeError("Gaussian observations and Grams must be Torch tensors")
        if correlations.requires_grad or gram.requires_grad:
            raise ValueError("Gaussian inference inputs must not require gradients")
        if correlations.ndim < 1 or tuple(gram.shape[-2:]) != (self.taps, self.taps) or correlations.shape[-1] != self.taps:
            raise ValueError("Gaussian observation/Gram coefficient extent mismatch")
        if correlations.device != gram.device:
            raise ValueError("Gaussian observations and Grams must be on the same device")
        try:
            broadcast = torch.broadcast_shapes(correlations.shape[:-1], gram.shape[:-2])
        except RuntimeError as error:
            raise ValueError("Gaussian observation and Gram leading shapes do not broadcast") from error
        if tuple(broadcast) != tuple(correlations.shape[:-1]):
            raise ValueError("Gaussian Gram must not expand the observation leading shape")
        correlations = correlations.to(self.dtype)
        gram = gram.to(self.dtype)
        if self.mean is None:
            self._initialize(correlations, gram)
        elif tuple(correlations.shape) != self._r_shape or tuple(gram.shape) != self._g_shape or correlations.device != self._device:
            raise ValueError("Gaussian state observation shape or device changed")
        if self.check_inputs:
            if not bool(torch.isfinite(correlations).all()) or not bool(torch.isfinite(gram).all()):
                raise ValueError("Gaussian observation or Gram contains nonfinite values")
            gram = self._validate_gram(gram)
        else:
            gram = (gram + gram.mH) * .5

        if math.isinf(self.prior.coherence_seconds):
            decay, innovation_fraction = 1., 0.
        else:
            decay = math.exp(-dt / self.prior.coherence_seconds)
            innovation_fraction = -math.expm1(-2 * dt / self.prior.coherence_seconds)
        phase_cycles = self.prior.frequency_hz * dt
        if not math.isfinite(phase_cycles):
            raise ValueError("Gaussian frequency/time product overflow")
        rotation = cmath.rect(decay, 2 * math.pi * math.remainder(phase_cycles, 1.))
        predicted_mean = self.mean * rotation
        predicted_covariance = self.covariance * (decay * decay) + self._power * innovation_fraction
        predicted_covariance = (predicted_covariance + predicted_covariance.mH) * .5

        values, vectors = torch.linalg.eigh(predicted_covariance)
        scale = predicted_covariance.abs().amax(dim=(-2, -1)).clamp_min(1.)
        relative_minimum = (values[..., 0] / scale).amin().to(torch.float64)
        self._worst_relative_covariance_eigenvalue = torch.minimum(
            self._worst_relative_covariance_eigenvalue, relative_minimum)
        if self.check_inputs and bool(relative_minimum < -self.relative_tolerance):
            raise ValueError("Gaussian predicted covariance lost positive semidefiniteness")
        factor = vectors * values.clamp_min(0.).sqrt()[..., None, :]
        innovation_information = self._identity + factor.mH @ gram @ factor
        innovation_information = (innovation_information + innovation_information.mH) * .5
        cholesky, info = torch.linalg.cholesky_ex(innovation_information, check_errors=False)
        if self.check_inputs and bool((info != 0).any()):
            raise ValueError("Gaussian innovation information is not positive definite")
        posterior_covariance = factor @ torch.cholesky_solve(factor.mH, cholesky)
        posterior_covariance = (posterior_covariance + posterior_covariance.mH) * .5
        difference = correlations - (gram @ predicted_mean[..., None]).squeeze(-1)
        adjustment = (posterior_covariance @ difference[..., None]).squeeze(-1)
        linear = 2 * (predicted_mean.conj() * correlations).sum(-1).real
        predicted_energy = (predicted_mean.conj() * (gram @ predicted_mean[..., None]).squeeze(-1)).sum(-1).real
        innovation_energy = (difference.conj() * adjustment).sum(-1).real
        log_determinant = 2 * cholesky.diagonal(dim1=-2, dim2=-1).real.log().sum(-1)
        increment = linear.to(torch.float64) - predicted_energy.to(torch.float64) + innovation_energy.to(torch.float64) - log_determinant.to(torch.float64)
        self.mean = predicted_mean + adjustment
        self.covariance = posterior_covariance
        self.log_likelihood_ratio = self.log_likelihood_ratio + increment
        self.blocks += 1
        return increment

    def update_many(self, correlations, grams, elapsed_seconds: Sequence[float]):
        """Consume a block tile without retaining its observations or history."""
        if correlations.shape[0] != grams.shape[0] or correlations.shape[0] != len(elapsed_seconds):
            raise ValueError("Gaussian block tile extents differ")
        for index, elapsed in enumerate(elapsed_seconds):
            self.update(correlations[index], grams[index], elapsed)
        return self.scores()

    def scores(self):
        if self.log_likelihood_ratio is None:
            raise ValueError("Gaussian likelihood has not observed a block")
        return self.log_likelihood_ratio

    def finish(self):
        """Check final numeric state; this does not perform an admission test."""
        result = self.scores()
        if not bool(torch.isfinite(result).all()) or not bool(torch.isfinite(self.mean).all()) or not bool(torch.isfinite(self.covariance).all()):
            raise ValueError("Gaussian likelihood state became nonfinite")
        if bool(self._worst_relative_covariance_eigenvalue < -self.relative_tolerance):
            raise ValueError("Gaussian covariance positivity diagnostic failed")
        return result

    def diagnostics(self):
        self.finish()
        owned = (self.mean, self.covariance, self.log_likelihood_ratio, self._power,
                 self._identity, self._worst_relative_covariance_eigenvalue,
                 self._largest_relative_gram_repair)
        return {"blocks": self.blocks, "taps": self.taps,
                "state_dtype": str(self.dtype), "log_likelihood_dtype": "torch.float64",
                "input_checks_each_block": self.check_inputs,
                "minimum_relative_covariance_eigenvalue": float(self._worst_relative_covariance_eigenvalue),
                "maximum_relative_gram_psd_repair": float(self._largest_relative_gram_repair),
                "owned_state_bytes": sum(value.numel() * value.element_size() for value in owned),
                "memory_scope": "owned streaming state only; excludes caller tiles, framework and temporary linear-algebra workspace",
                "likelihood_scope": "proper complex Gaussian block-constant coefficients with declared OU/static transitions; no physical-channel optimality or anytime-admission claim"}
