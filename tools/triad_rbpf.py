#!/usr/bin/env python3
"""Bounded Rao--Blackwellized common-phase / Gaussian-channel likelihood.

Inputs are the joint sampled-waveform statistics r=A.H@y/v and G=A.H@A/v.
Conditional on a phase trajectory, exp(-j*phase)*r and unchanged G give a
proper-complex Gaussian tap likelihood. Covariance preparation is shared across
particles; each particle retains its own conditional mean and oscillator state.

Without resampling, the result is the normalized finite prior-path mixture,
conditional on that path bank. With standard resampling it is a nonnegative,
unbiased likelihood estimator over independent algorithm randomness. The latter
does NOT give a null bound conditional on every fixed adaptive-resampling seed.
Log evidence itself is biased. Average likelihoods, never select a best seed or
particle. External noise calibration and fixed-window family accounting remain
the caller's responsibility. This module performs no admission or stopping.
"""
from __future__ import annotations

from dataclasses import dataclass
import cmath
import math

import torch

from triad_gaussian_state import GaussianPrior


@dataclass(frozen=True)
class RBPhasePrior:
    """Common oscillator law in physical seconds, independent of the tap prior.

    Initial frequency is uniform on +/-frequency_bound_hz. Frequency can then
    follow a Brownian random walk. Phase diffusion is an independent Wiener
    process. The frequency/phase increment uses the exact integrated Brownian
    transition, not an endpoint-frequency rectangle. Initial phase is zero:
    a constant common phase is already integrated by the circular tap prior.
    Neither frequency nor trajectories are clipped to the initial bound.
    """
    frequency_bound_hz: float = 0.
    phase_diffusion_rad_sqrt_second: float = 0.
    frequency_random_walk_hz_sqrt_second: float = 0.

    def __post_init__(self):
        for name in ("frequency_bound_hz", "phase_diffusion_rad_sqrt_second",
                     "frequency_random_walk_hz_sqrt_second"):
            value = getattr(self, name)
            if isinstance(value, bool) or not math.isfinite(value) or value < 0:
                raise ValueError(name + " must be finite and nonnegative")


class RBPhaseFilter:
    """Streaming likelihood, with no retained observation or trajectory history.

    update(r[...,K], G[...,K,K], dt) accepts any fixed leading shape; a Gram may
    broadcast within that shape. Particles are an internal additional dimension.
    GaussianPrior.frequency_hz is a separate deterministic tap rotation: do not
    duplicate a Doppler already in A or a common CFO in RBPhasePrior.

    FP32 mode uses complex64 particle means/products. Covariance/eigensolves,
    phase/frequency, scalar products' reductions and log evidence remain FP64.
    FP64 mode also widens those bulk products. CUDA TF32 products are disabled.

    frequency_initialization='iid' preserves independent uniform CFO draws.
    'stratified' uses one independent jitter in each equal-width CFO stratum,
    with the same normalized 1/N starting weights. The average empirical
    measure over jitter randomness equals the continuous uniform CFO prior;
    individual particle frequencies need not each have that full prior.

    phase_increment_radians is a no-resampling oracle/finite-bank interface. It
    replaces the oscillator transition for that update and must supply exactly
    [...,particles] increments chosen independently of received data. It cannot
    be combined with nonzero frequency-prior/dynamic parameters. The filter
    records its use; it cannot certify the caller's trajectory construction.
    """
    def __init__(self, tap_prior: GaussianPrior, phase_prior: RBPhasePrior | None = None,
                 *, particles: int = 128, seed: int = 1, dtype=torch.complex128,
                 resampling: str = "none", ess_fraction: float = .5,
                 frequency_initialization: str = "iid",
                 check_inputs: bool = True, relative_tolerance: float = 1e-10,
                 memory_limit_bytes: int = 256*1024*1024):
        if not isinstance(tap_prior, GaussianPrior):
            raise TypeError("tap_prior must be GaussianPrior")
        if phase_prior is not None and not isinstance(phase_prior, RBPhasePrior):
            raise TypeError("phase_prior must be RBPhasePrior")
        if isinstance(particles, bool) or not isinstance(particles, int) or not 1 <= particles <= 1_048_576:
            raise ValueError("particles must be an integer in 1..1048576")
        if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed < 2**63:
            raise ValueError("seed must be an integer in 0..2**63-1")
        if dtype not in (torch.complex64, torch.complex128):
            raise ValueError("RB bulk arithmetic must be complex64 or complex128")
        if resampling not in ("none", "multinomial", "systematic"):
            raise ValueError("resampling must be none, multinomial, or systematic")
        if frequency_initialization not in ("iid", "stratified"):
            raise ValueError("frequency_initialization must be iid or stratified")
        if not math.isfinite(ess_fraction) or not 0 < ess_fraction <= 1:
            raise ValueError("ESS fraction must be in (0,1]")
        if not math.isfinite(relative_tolerance) or not 0 < relative_tolerance < .01:
            raise ValueError("invalid covariance tolerance")
        if isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or memory_limit_bytes < 4096:
            raise ValueError("memory limit must be an integer of at least 4096 bytes")
        self.tap_prior, self.phase_prior = tap_prior, phase_prior or RBPhasePrior()
        self.particles, self.seed, self.dtype = particles, seed, dtype
        self.resampling, self.ess_fraction = resampling, float(ess_fraction)
        self.frequency_initialization = frequency_initialization
        self.check_inputs, self.relative_tolerance = bool(check_inputs), relative_tolerance
        self.memory_limit_bytes = memory_limit_bytes
        self.blocks = 0
        self.mean = self.covariance = self.log_weights = self.phase = self.frequency = None
        self.log_likelihood_ratio = None
        self._r_shape = self._g_shape = self._leading_shape = self._device = None
        self._generator = self._power = self._identity = None
        self._minimum_ess = self._minimum_weight = self._maximum_weight = self._resampling_counts = None
        self._worst_covariance = self._largest_gram_repair = None
        self._override_used = False

    @property
    def taps(self):
        return len(self.tap_prior.stationary_power)

    def _initialize(self, r, G):
        self._r_shape, self._g_shape = tuple(r.shape), tuple(G.shape)
        self._leading_shape, self._device = tuple(r.shape[:-1]), r.device
        count = math.prod(self._leading_shape)
        if count < 1:
            raise ValueError("empty path/candidate dimension")
        # Six NxK arrays coexist during likelihood evaluation: old mean,
        # predicted mean, rotated r, predicted observation, difference and
        # adjustment. Inner products/gathers need another array; reserve two
        # extra for product/conjugation temporaries, also without resampling.
        # Scalar particle states, likelihood temporaries, indices and resampling
        # work use a separate 24-array FP64/int64 allowance. The small-matrix
        # allowance covers covariance preparation. Framework/solver allocator
        # workspaces and caller-owned input statistics remain outside this cap.
        bulk_bytes = 8 if self.dtype == torch.complex64 else 16
        self.estimated_array_peak_bytes = count*(self.particles*(8*self.taps*bulk_bytes+24*8)
                                                 +16*16*self.taps*self.taps+128)
        if self.estimated_array_peak_bytes > self.memory_limit_bytes:
            raise ValueError("RB state/temporary array estimate exceeds memory limit")
        self._generator = torch.Generator(device=r.device).manual_seed(self.seed)
        self._power = torch.diag(torch.tensor(self.tap_prior.stationary_power,
                                              dtype=torch.float64, device=r.device)).to(torch.complex128)
        self._identity = torch.eye(self.taps, dtype=torch.complex128, device=r.device)
        self.mean = torch.zeros((count, self.particles, self.taps), dtype=self.dtype, device=r.device)
        self.covariance = self._power.expand(count, self.taps, self.taps).clone()
        self.phase = torch.zeros((count, self.particles), dtype=torch.float64, device=r.device)
        if self.phase_prior.frequency_bound_hz:
            if self.frequency_initialization == "iid":
                self.frequency = (2*torch.rand(self.phase.shape, dtype=torch.float64, device=r.device,
                                               generator=self._generator)-1)*self.phase_prior.frequency_bound_hz
            else:
                # Exactly the IID branch's number/order of uniform draws; no
                # permutation draws, so subsequent oscillator RNG is paired.
                jitter = torch.rand(self.phase.shape, dtype=torch.float64,
                                    device=r.device, generator=self._generator)
                strata = torch.arange(self.particles, dtype=torch.float64, device=r.device)
                self.frequency = (2*(strata+jitter)/self.particles-1)*self.phase_prior.frequency_bound_hz
        else:
            self.frequency = torch.zeros_like(self.phase)
        self.log_weights = torch.full_like(self.phase, -math.log(self.particles))
        self.log_likelihood_ratio = torch.zeros(count, dtype=torch.float64, device=r.device)
        self._minimum_ess = torch.full_like(self.log_likelihood_ratio, float(self.particles))
        self._minimum_weight = torch.full_like(self.log_likelihood_ratio, 1/self.particles)
        self._maximum_weight = torch.full_like(self.log_likelihood_ratio, 1/self.particles)
        self._resampling_counts = torch.zeros(count, dtype=torch.int64, device=r.device)
        self._worst_covariance = torch.zeros((), dtype=torch.float64, device=r.device)
        self._largest_gram_repair = torch.zeros_like(self._worst_covariance)
        if r.device.type == "cuda" and self.dtype == torch.complex64:
            torch.backends.cuda.matmul.allow_tf32 = False

    def _gram(self, G):
        G = G.to(torch.complex128).expand(*self._leading_shape, self.taps, self.taps)
        G = G.reshape(-1, self.taps, self.taps)
        scale = G.abs().amax((-2, -1)).clamp_min(1.)
        if self.check_inputs and bool(((G-G.mH).abs().amax((-2, -1)) > self.relative_tolerance*scale).any()):
            raise ValueError("RB Gram is not Hermitian")
        G = (G+G.mH)*.5
        if self.check_inputs:
            values, vectors = torch.linalg.eigh(G)
            if bool((values[..., 0] < -self.relative_tolerance*scale).any()):
                raise ValueError("RB Gram is not positive semidefinite")
            self._largest_gram_repair = torch.maximum(self._largest_gram_repair,
                ((-values[..., 0]).clamp_min(0.)/scale).amax())
            if bool((values < 0).any()):
                G = (vectors*values.clamp_min(0.)[..., None, :])@vectors.mH
        return G

    def _advance_phase(self, dt, override):
        if override is not None:
            p = self.phase_prior
            if self.resampling != "none" or p.frequency_bound_hz or p.phase_diffusion_rad_sqrt_second or p.frequency_random_walk_hz_sqrt_second:
                raise ValueError("explicit phase increments require a zero oscillator prior and no resampling")
            if not isinstance(override, torch.Tensor) or override.is_complex() or override.requires_grad:
                raise ValueError("explicit phase increments must be a real tensor without gradients")
            if tuple(override.shape) != (*self._leading_shape, self.particles) or override.device != self._device:
                raise ValueError("explicit phase increment shape/device mismatch")
            increment = override.to(torch.float64).reshape_as(self.phase)
            if not bool(torch.isfinite(increment).all()):
                raise ValueError("nonfinite explicit phase increment")
            self._override_used = True
        else:
            p = self.phase_prior
            increment = 2*math.pi*self.frequency*dt
            if dt and p.frequency_random_walk_hz_sqrt_second:
                z = torch.randn((*self.phase.shape, 2), dtype=torch.float64,
                                device=self._device, generator=self._generator)
                sigma_root_dt = p.frequency_random_walk_hz_sqrt_second*math.sqrt(dt)
                increment = increment + 2*math.pi*sigma_root_dt*dt*(.5*z[..., 0]+z[..., 1]/math.sqrt(12.))
                self.frequency = self.frequency + sigma_root_dt*z[..., 0]
            if dt and p.phase_diffusion_rad_sqrt_second:
                increment = increment + p.phase_diffusion_rad_sqrt_second*math.sqrt(dt)*torch.randn(
                    self.phase.shape, dtype=torch.float64, device=self._device, generator=self._generator)
        self.phase = torch.remainder(self.phase+increment+math.pi, 2*math.pi)-math.pi

    @staticmethod
    def _real_inner(a, b):
        # Products obey the bulk policy; cancellation-sensitive sums use FP64.
        return (a.conj()*b).sum(-1, dtype=torch.complex128).real

    def _resample(self, ess):
        if self.resampling == "none":
            return
        # Equal weights can produce ESS=N minus a few FP64 ulps. An empty
        # observation must not spuriously trigger resampling at fraction=1.
        slack = 16*torch.finfo(torch.float64).eps*self.particles
        selected = ess < self.ess_fraction*self.particles-slack
        # No host branch or scalar synchronization: unused rows retain their
        # original particle ordering while consuming independent RNG draws.
        weights = self.log_weights.exp()
        if self.resampling == "multinomial":
            ancestors = torch.multinomial(weights, self.particles, replacement=True, generator=self._generator)
        else:
            positions = (torch.rand((len(weights), 1), dtype=torch.float64, device=self._device,
                                    generator=self._generator)+torch.arange(self.particles,
                                    dtype=torch.float64, device=self._device))/self.particles
            cumulative = weights.cumsum(-1)
            cumulative[:, -1] = 1.
            ancestors = torch.searchsorted(cumulative.contiguous(), positions.contiguous()).clamp_max(self.particles-1)
        identity = torch.arange(self.particles, device=self._device).expand_as(ancestors)
        ancestors = torch.where(selected[:, None], ancestors, identity)
        self.mean = torch.gather(self.mean, 1, ancestors[..., None].expand_as(self.mean))
        self.phase = torch.gather(self.phase, 1, ancestors)
        self.frequency = torch.gather(self.frequency, 1, ancestors)
        self.log_weights = torch.where(selected[:, None], -math.log(self.particles), self.log_weights)
        self._resampling_counts = self._resampling_counts + selected.to(torch.int64)

    def update(self, correlations, gram, dt_seconds: float, *, phase_increment_radians=None):
        """Return this complete block's log likelihood-estimate increment."""
        dt = float(dt_seconds)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("RB elapsed time must be finite and nonnegative")
        if not isinstance(correlations, torch.Tensor) or not isinstance(gram, torch.Tensor):
            raise TypeError("RB observations and Grams must be Torch tensors")
        if correlations.requires_grad or gram.requires_grad:
            raise ValueError("RB inputs must not require gradients")
        if correlations.ndim < 1 or correlations.shape[-1] != self.taps or gram.shape[-2:] != (self.taps, self.taps):
            raise ValueError("RB coefficient extent mismatch")
        if correlations.device != gram.device:
            raise ValueError("RB observation and Gram device mismatch")
        try:
            shape = torch.broadcast_shapes(correlations.shape[:-1], gram.shape[:-2])
        except RuntimeError as error:
            raise ValueError("RB Gram does not broadcast to observations") from error
        if tuple(shape) != tuple(correlations.shape[:-1]):
            raise ValueError("RB Gram expands observation shape")
        if self.check_inputs and (not bool(torch.isfinite(correlations).all()) or not bool(torch.isfinite(gram).all())):
            raise ValueError("RB input contains nonfinite values")
        if self.mean is None:
            self._initialize(correlations, gram)
        elif tuple(correlations.shape) != self._r_shape or tuple(gram.shape) != self._g_shape or correlations.device != self._device:
            raise ValueError("RB observation shape or device changed")
        G = self._gram(gram)
        r = correlations.to(self.dtype).reshape(-1, self.taps)
        self._advance_phase(dt, phase_increment_radians)
        prior = self.tap_prior
        decay = 1. if math.isinf(prior.coherence_seconds) else math.exp(-dt/prior.coherence_seconds)
        fraction = 0. if math.isinf(prior.coherence_seconds) else -math.expm1(-2*dt/prior.coherence_seconds)
        cycles = prior.frequency_hz*dt
        if not math.isfinite(cycles):
            raise ValueError("RB tap frequency/time product overflow")
        predicted_mean = self.mean*cmath.rect(decay, 2*math.pi*math.remainder(cycles, 1.))
        predicted_covariance = self.covariance*(decay*decay)+self._power*fraction
        predicted_covariance = (predicted_covariance+predicted_covariance.mH)*.5
        eigenvalues, vectors = torch.linalg.eigh(predicted_covariance)
        scale = predicted_covariance.abs().amax((-2, -1)).clamp_min(1.)
        minimum = (eigenvalues[..., 0]/scale).amin()
        self._worst_covariance = torch.minimum(self._worst_covariance, minimum)
        if self.check_inputs and bool(minimum < -self.relative_tolerance):
            raise ValueError("RB covariance lost positive semidefiniteness")
        factor = vectors*eigenvalues.clamp_min(0.).sqrt()[..., None, :]
        information = self._identity+factor.mH@G@factor
        information = (information+information.mH)*.5
        chol, info = torch.linalg.cholesky_ex(information, check_errors=False)
        if self.check_inputs and bool((info != 0).any()):
            raise ValueError("RB innovation information is not positive definite")
        P = factor@torch.cholesky_solve(factor.mH, chol)
        P = (P+P.mH)*.5
        rotated_r = torch.polar(torch.ones_like(self.phase), -self.phase).to(self.dtype)[..., None]*r[:, None, :]
        Gbulk, Pbulk = G.to(self.dtype), P.to(self.dtype)
        predicted_observation = torch.matmul(Gbulk[:, None], predicted_mean[..., None]).squeeze(-1)
        difference = rotated_r-predicted_observation
        adjustment = torch.matmul(Pbulk[:, None], difference[..., None]).squeeze(-1)
        determinant = 2*chol.diagonal(dim1=-2, dim2=-1).real.log().sum(-1)
        increments = (2*self._real_inner(predicted_mean, rotated_r)
                      -self._real_inner(predicted_mean, predicted_observation)
                      +self._real_inner(difference, adjustment)-determinant[:, None])
        updated = self.log_weights+increments
        normalizer = torch.logsumexp(updated, -1)
        self.log_weights = updated-normalizer[:, None]
        self.log_likelihood_ratio = self.log_likelihood_ratio+normalizer
        self.mean, self.covariance = predicted_mean+adjustment, P
        ess = torch.exp(-torch.logsumexp(2*self.log_weights, -1))
        self._minimum_ess = torch.minimum(self._minimum_ess, ess)
        self._minimum_weight = torch.minimum(self._minimum_weight, self.log_weights.min(-1).values.exp())
        self._maximum_weight = torch.maximum(self._maximum_weight, self.log_weights.max(-1).values.exp())
        self._resample(ess)
        self.blocks += 1
        return normalizer.reshape(self._leading_shape)

    def scores(self):
        if self.log_likelihood_ratio is None:
            raise ValueError("RB likelihood has not observed a block")
        return self.log_likelihood_ratio.reshape(self._leading_shape)

    def finish(self):
        result = self.scores()
        for value in (result, self.mean, self.covariance, self.phase, self.frequency, self.log_weights):
            if not bool(torch.isfinite(value).all()):
                raise ValueError("RB state became nonfinite")
        if bool(self._worst_covariance < -self.relative_tolerance):
            raise ValueError("RB covariance positivity diagnostic failed")
        if bool(torch.logsumexp(self.log_weights, -1).abs().max() > 1e-9):
            raise ValueError("RB particle weights lost normalization")
        return result

    def diagnostics(self):
        self.finish()
        owned = (self.mean, self.covariance, self.phase, self.frequency, self.log_weights,
                 self.log_likelihood_ratio, self._power, self._identity, self._minimum_ess,
                 self._minimum_weight, self._maximum_weight, self._resampling_counts, self._worst_covariance,
                 self._largest_gram_repair)
        return {"blocks": self.blocks, "particles": self.particles, "taps": self.taps,
                "leading_shape": self._leading_shape, "bulk_dtype": str(self.dtype),
                "covariance_dtype": "torch.complex128", "phase_frequency_and_evidence_dtype": "torch.float64",
                "tf32_allowed": False, "resampling": self.resampling,
                "frequency_initialization": self.frequency_initialization,
                "ess_fraction": self.ess_fraction, "minimum_effective_particles": float(self._minimum_ess.min()),
                "minimum_particle_weight": float(self._minimum_weight.min()),
                "maximum_particle_weight": float(self._maximum_weight.max()),
                "resampling_events_total": int(self._resampling_counts.sum()),
                "resampling_events_maximum_per_item": int(self._resampling_counts.max()),
                "minimum_relative_covariance_eigenvalue": float(self._worst_covariance),
                "maximum_relative_gram_psd_repair": float(self._largest_gram_repair),
                "phase_increment_override_used": self._override_used,
                "owned_state_bytes": sum(x.numel()*x.element_size() for x in owned),
                "estimated_array_peak_bytes": self.estimated_array_peak_bytes,
                "memory_limit_bytes": self.memory_limit_bytes,
                "memory_scope": "owned bounded state; excludes caller statistics, RNG implementation, framework and temporary workspace",
                "calibration_scope": ("conditional on a predeclared prior-path bank independent of observations"
                    if self.resampling == "none" else "joint over null noise and independent algorithm randomness; not every fixed adaptive seed"),
                "likelihood_scope": "block-constant common phase, proper Gaussian tap dynamics, exact conditional tap integration; no admission, anytime or physical optimality claim"}
