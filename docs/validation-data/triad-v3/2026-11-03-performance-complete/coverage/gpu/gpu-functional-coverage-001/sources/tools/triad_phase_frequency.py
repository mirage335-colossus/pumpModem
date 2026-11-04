"""Streaming finite phase/frequency likelihood for the isolated triad study.

Inputs are noise-normalized sufficient statistics r=A^H y/sigma^2 and
G=A^H A/sigma^2 from a disjoint physical sample block.  Conditional on a
complex tap vector h, its likelihood ratio against the known white-noise null
is exp(2 Re(h^H r)-h^H G h).  This module integrates a proper, fixed finite
prior over h and oscillator paths.  It never receives simulated signal means.

The implemented oscillator is a finite Markov chain. Frequency follows a
reflecting birth/death OU or random-walk chain. Each interval applies half
phase advection at the old frequency, the frequency transition, and half
advection at the new frequency, then independent circular phase diffusion.
Positive linear interpolation is itself part of that discrete model; its
added phase variance is reported. The automatically aligned frequency grid
makes both half shifts integral at nominal_dt_seconds.

This is an exact forward likelihood for the declared finite model in exact
arithmetic, not exact inference for a continuous colored oscillator. Unknown
global amplitude and relative ray phase remain static mixture coordinates
through the complete message. Bulk arithmetic is FP32 or FP64; accumulated
log evidence uses FP64. No lower-precision matrix multiplication is allowed.
"""
from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass
import math
from typing import Sequence

import torch


@dataclass(frozen=True)
class PhaseFrequencyModel:
    phase_states: int = 256
    frequency_bound_hz: float = .02
    nominal_dt_seconds: float = 4.
    frequency_grid_hz: tuple[float, ...] | None = None
    frequency_model: str = "ou"  # ou, random_walk, static
    frequency_std_hz: float = .003
    frequency_correlation_seconds: float = 1000.
    random_walk_hz_per_sqrt_second: float = 1e-6
    phase_diffusion_rad_per_sqrt_second: float = .00872665
    amplitude_nodes: tuple[float, ...] = (.5, 1., 2.)
    amplitude_weights: tuple[float, ...] = (.25, .5, .25)
    relative_phase_states: int = 8
    ray_amplitudes: tuple[float, ...] | None = None
    initial_frequency_weights: tuple[float, ...] | None = None
    phase_kernel_tail_probability: float = 1e-12
    maximum_frequency_states: int = 257
    transition_cache_entries: int = 4


def _finite(value: float, name: str, minimum: float = 0.) -> float:
    value = float(value)
    if not math.isfinite(value) or value < minimum:
        raise ValueError(f"{name} must be finite and >= {minimum}")
    return value


def _positive_weights(values: Sequence[float], count: int, name: str) -> torch.Tensor:
    if len(values) != count:
        raise ValueError(f"{name} length differs from its state count")
    result = torch.tensor(tuple(float(x) for x in values), dtype=torch.float64)
    if not bool(torch.isfinite(result).all()) or bool((result < 0).any()) or float(result.sum()) <= 0:
        raise ValueError(f"{name} must be finite nonnegative weights with positive sum")
    result = result/result.max()
    return result/result.sum()


def _bernoulli(value: torch.Tensor) -> torch.Tensor:
    """Stable B(x)=x/(exp(x)-1), used by a detailed-balance generator."""
    result = torch.empty_like(value)
    small = value.abs() < 1e-4
    positive = value > 50
    negative = value < -50
    middle = ~(small | positive | negative)
    x = value[small]
    result[small] = 1-x/2+x.square()/12-x.pow(4)/720
    x = value[positive]
    result[positive] = x*torch.exp(-x)/(1-torch.exp(-x))
    result[negative] = -value[negative]
    result[middle] = value[middle]/torch.expm1(value[middle])
    return result


class PhaseFrequencyLikelihood:
    """Persistent forward state; update once per disjoint physical block.

    ``dt`` is the separation from the previous block center. Use zero for the
    first block when its center defines the public prior's reference time.
    Later zero-energy blocks still propagate the oscillator state. They add
    exactly zero log evidence. G may have shape [bits,K,K] or [batch,bits,K,K].

    Set validate_inputs=False only after a workspace has independently checked
    finite, Hermitian positive-semidefinite statistics; this avoids per-block
    accelerator synchronization. Structural shape/dtype checks remain active.
    """

    def __init__(self, batch_size: int, bits: int = 2, taps: int = 2,
                 model: PhaseFrequencyModel | None = None, *, device="cpu",
                 dtype=torch.float64, memory_limit_bytes: int = 4*1024**3,
                 validate_inputs: bool = True):
        self.model = model or PhaseFrequencyModel()
        if any(isinstance(x, bool) or not isinstance(x, int) for x in (batch_size, bits, taps)):
            raise ValueError("batch size, bit count and tap count must be integers")
        self.batch_size, self.bits, self.taps = batch_size, bits, taps
        if self.batch_size < 1 or self.bits < 1 or self.taps not in (1, 2):
            raise ValueError("positive batch/bits and one or two taps are required")
        self.device = torch.device(device)
        if dtype in (torch.complex64, torch.complex128):
            dtype = torch.float32 if dtype == torch.complex64 else torch.float64
        if dtype not in (torch.float32, torch.float64):
            raise ValueError("phase/frequency bulk arithmetic requires FP32 or FP64")
        self.dtype = dtype
        self.complex_dtype = torch.complex64 if dtype == torch.float32 else torch.complex128
        if self.device.type == "cuda" and dtype == torch.float32:
            if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
                raise ValueError("FP32 minimum requires TF32 disabled and highest float32 matmul precision")
        self.validate_inputs = bool(validate_inputs)
        m = self.model
        if isinstance(m.phase_states, bool) or not 2 <= m.phase_states <= 4096:
            raise ValueError("phase_states must be an integer in [2,4096]")
        if not isinstance(m.phase_states, int):
            raise ValueError("phase_states must be an integer")
        if not isinstance(m.relative_phase_states, int) or not 1 <= m.relative_phase_states <= 256:
            raise ValueError("relative_phase_states must be in [1,256]")
        if not isinstance(m.maximum_frequency_states, int) or not 1 <= m.maximum_frequency_states <= 1025:
            raise ValueError("maximum_frequency_states must be in [1,1025]")
        if not isinstance(m.transition_cache_entries, int) or not 1 <= m.transition_cache_entries <= 32:
            raise ValueError("transition_cache_entries must be in [1,32]")
        self.phase_states = m.phase_states
        self.phase_step = 2*math.pi/self.phase_states
        nominal_dt = _finite(m.nominal_dt_seconds, "nominal dt", 1e-12)
        bound = _finite(m.frequency_bound_hz, "frequency bound")
        _finite(m.frequency_std_hz, "frequency standard deviation")
        _finite(m.random_walk_hz_per_sqrt_second, "frequency random walk")
        _finite(m.phase_diffusion_rad_per_sqrt_second, "phase diffusion")
        if m.frequency_model not in ("ou", "random_walk", "static"):
            raise ValueError("frequency_model must be ou, random_walk or static")
        if m.frequency_model == "ou":
            _finite(m.frequency_correlation_seconds, "frequency correlation time", 1e-12)
        if not 0 <= m.phase_kernel_tail_probability < 1:
            raise ValueError("phase kernel tail probability must lie in [0,1)")
        if m.frequency_grid_hz is None:
            spacing = (2/self.phase_states)/nominal_dt
            if spacing <= 0 or not math.isfinite(spacing) or bound > ((m.maximum_frequency_states-1)//2)*spacing:
                raise ValueError("aligned frequency grid exceeds its explicit state bound")
            half = math.ceil(bound/spacing)
            grid = torch.arange(-half, half+1, dtype=torch.float64)*spacing
        else:
            if not m.frequency_grid_hz or len(m.frequency_grid_hz) > m.maximum_frequency_states:
                raise ValueError("frequency grid cannot be empty")
            grid = torch.tensor(m.frequency_grid_hz, dtype=torch.float64)
        if not bool(torch.isfinite(grid).all()) or grid.numel() > m.maximum_frequency_states:
            raise ValueError("frequency grid is nonfinite or exceeds its explicit state bound")
        if grid.numel() > 1:
            differences = grid[1:]-grid[:-1]
            if bool((differences <= 0).any()) or not bool(torch.allclose(differences, differences[:1], rtol=1e-10, atol=1e-15)):
                raise ValueError("frequency grid must be strictly increasing and uniform")
            self.frequency_spacing_hz = float(differences[0])
        else:
            self.frequency_spacing_hz = 0.
        self.frequency_grid_cpu = grid
        self.frequency_states = grid.numel()
        self.frequency_generator, stationary = self._frequency_generator()
        self.frequency_stationary_weights_cpu = stationary
        if m.initial_frequency_weights is not None:
            initial_frequency = _positive_weights(m.initial_frequency_weights, self.frequency_states, "initial frequency")
        elif stationary is not None:
            initial_frequency = stationary
        else:
            initial_frequency = torch.full_like(grid, 1/self.frequency_states)
        self.initial_frequency_weights_cpu = initial_frequency.clone()
        amplitudes = tuple(_finite(x, "amplitude node", 1e-15) for x in m.amplitude_nodes)
        if not amplitudes or len(amplitudes) > 64:
            raise ValueError("one to 64 amplitude nodes are required")
        amplitude_weights = _positive_weights(m.amplitude_weights, len(amplitudes), "amplitude")
        ray_amplitudes = m.ray_amplitudes
        if ray_amplitudes is None:
            ray_amplitudes = (1.,) if self.taps == 1 else (math.sqrt(.5), math.sqrt(.5))
        if len(ray_amplitudes) != self.taps:
            raise ValueError("ray_amplitudes must match the tap count")
        ray_amplitudes = tuple(_finite(x, "ray amplitude") for x in ray_amplitudes)
        self.ray_amplitudes = ray_amplitudes
        if sum(x*x for x in ray_amplitudes) <= 0:
            raise ValueError("ray amplitudes cannot all vanish")
        relative_count = m.relative_phase_states if self.taps == 2 else 1
        vectors, mixture_weights = [], []
        for index, amplitude in enumerate(amplitudes):
            for relative in range(relative_count):
                row = [complex(amplitude*ray_amplitudes[0])]
                if self.taps == 2:
                    angle = 2*math.pi*relative/relative_count
                    row.append(amplitude*ray_amplitudes[1]*complex(math.cos(angle), math.sin(angle)))
                vectors.append(row)
                mixture_weights.append(float(amplitude_weights[index])/relative_count)
        self.quadrature_states = len(vectors)
        self.gain_vectors = torch.tensor(vectors, dtype=self.complex_dtype, device=self.device)
        self.phases = torch.arange(self.phase_states, dtype=dtype, device=self.device)*self.phase_step
        self.phase_cosine, self.phase_sine = self.phases.cos(), self.phases.sin()
        elements = self.batch_size*self.bits*self.quadrature_states*self.frequency_states*self.phase_states
        # Includes simultaneous predictor, emission, log-weight, reduction and
        # transition-cache arrays. Framework/runtime/allocator cache is external.
        self.working_bytes_bound = elements*torch.tensor([], dtype=dtype).element_size()*16
        self.working_bytes_bound += m.transition_cache_entries*(self.frequency_states**2*16+
            self.frequency_states*self.phase_states*24+self.phase_states*32)+65536
        if memory_limit_bytes <= 0 or self.working_bytes_bound > memory_limit_bytes:
            raise ValueError("phase/frequency state exceeds the declared memory limit; no states were pruned")
        prior = torch.tensor(mixture_weights, dtype=dtype, device=self.device)[:, None, None]
        prior = prior*initial_frequency.to(device=self.device, dtype=dtype)[None, :, None]
        prior = prior.expand(self.quadrature_states, self.frequency_states, self.phase_states)/self.phase_states
        prior = prior/prior.sum()
        self.weights = prior[None, None].expand(self.batch_size, self.bits, -1, -1, -1).clone()
        self._scores = torch.zeros((self.batch_size, self.bits), dtype=torch.float64, device=self.device)
        self._transition_cache: OrderedDict[float, dict] = OrderedDict()
        self.updates = 0
        self.elapsed_seconds = 0.
        self.maximum_interpolation_variance_radians2 = 0.
        self.maximum_discarded_phase_kernel_mass = 0.
        self.maximum_transition_row_error = 0.

    def _frequency_generator(self):
        grid, m = self.frequency_grid_cpu, self.model
        count = grid.numel()
        generator = torch.zeros((count, count), dtype=torch.float64)
        if count == 1 or m.frequency_model == "static":
            return generator, None
        spacing = self.frequency_spacing_hz
        diffusion = .5*m.random_walk_hz_per_sqrt_second*m.random_walk_hz_per_sqrt_second
        stationary = None
        if m.frequency_model == "ou":
            diffusion += m.frequency_std_hz*m.frequency_std_hz/m.frequency_correlation_seconds
            if not math.isfinite(diffusion):
                raise ValueError("frequency diffusion exceeds the supported numeric range")
            if diffusion > 0:
                variance = diffusion*m.frequency_correlation_seconds
                potential = grid.square()/(2*variance)
                stationary = torch.softmax(-potential, dim=0)
                delta = potential[1:]-potential[:-1]
                forward = diffusion/(spacing*spacing)*_bernoulli(delta)
                backward = diffusion/(spacing*spacing)*_bernoulli(-delta)
            else:
                # Zero-diffusion OU is a finite deterministic-drift analogue.
                drift = -grid/m.frequency_correlation_seconds
                forward = drift[:-1].clamp_min(0)/spacing
                backward = (-drift[1:]).clamp_min(0)/spacing
                stationary = torch.zeros_like(grid)
                negative = torch.nonzero(grid < 0).flatten()
                positive = torch.nonzero(grid > 0).flatten()
                zero = torch.nonzero(grid == 0).flatten()
                if zero.numel():
                    stationary[int(zero[0])] = 1.
                elif not negative.numel():
                    stationary[0] = 1.
                elif not positive.numel():
                    stationary[-1] = 1.
                else:
                    left, right = int(negative[-1]), int(positive[0])
                    distance = grid[right]-grid[left]
                    stationary[left] = grid[right]/distance
                    stationary[right] = -grid[left]/distance
        else:
            forward = backward = torch.full((count-1,), diffusion/(spacing*spacing), dtype=torch.float64)
        for index in range(count-1):
            generator[index, index+1] = forward[index]
            generator[index+1, index] = backward[index]
        generator.diagonal().copy_(-generator.sum(dim=1))
        if not bool(torch.isfinite(generator).all()):
            raise ValueError("frequency generator overflow")
        return generator, stationary

    def _transition(self, dt: float) -> dict:
        cached = self._transition_cache.get(dt)
        if cached is not None:
            self._transition_cache.move_to_end(dt)
            return cached
        frequency = torch.matrix_exp(self.frequency_generator*dt)
        if not bool(torch.isfinite(frequency).all()) or float(frequency.min()) < -1e-11:
            raise ValueError("frequency transition has substantive invalid probabilities")
        frequency = frequency.clamp_min(0)
        row_sums = frequency.sum(dim=1, keepdim=True)
        self.maximum_transition_row_error = max(self.maximum_transition_row_error, float((row_sums-1).abs().max()))
        frequency = frequency/row_sums
        shift = self.frequency_grid_cpu*(dt*self.phase_states*.5)
        nearest = shift.round()
        close = (shift-nearest).abs() <= 32*torch.finfo(torch.float64).eps*torch.maximum(torch.ones_like(shift), shift.abs())
        shift = torch.where(close, nearest, shift)
        lower = shift.floor().to(torch.int64)
        fraction = shift-lower
        phase_index = torch.arange(self.phase_states, dtype=torch.int64)[None, :]
        index0 = (phase_index-lower[:, None]) % self.phase_states
        index1 = (index0-1) % self.phase_states
        added_variance = 2*float((fraction*(1-fraction)).max())*self.phase_step**2
        self.maximum_interpolation_variance_radians2 = max(self.maximum_interpolation_variance_radians2, added_variance)
        modes = torch.arange(self.phase_states//2+1, dtype=torch.float64)
        multiplier = torch.exp(-self.model.phase_diffusion_rad_per_sqrt_second**2*dt*
                               (1-torch.cos(2*math.pi*modes/self.phase_states))/self.phase_step**2)
        kernel = torch.fft.irfft(multiplier, n=self.phase_states)
        if float(kernel.min()) < -1e-12:
            raise ValueError("phase transition has substantive negative probabilities")
        kernel = kernel.clamp_min(0)
        kernel = kernel/kernel.sum()
        # A positive sparse circular kernel avoids negative FFT roundoff in
        # live probabilities. Truncation/renormalization defines the stated
        # finite model and its discarded probability is explicitly reported.
        order = sorted(range(self.phase_states), key=lambda k: min(k, self.phase_states-k))
        selected, retained = [], 0.
        for offset in order:
            selected.append(offset)
            retained += float(kernel[offset])
            if 1-retained <= self.model.phase_kernel_tail_probability:
                break
        discarded = max(0., 1-retained)
        self.maximum_discarded_phase_kernel_mass = max(self.maximum_discarded_phase_kernel_mass, discarded)
        probabilities = kernel[selected]
        probabilities = probabilities/probabilities.sum()
        result = {"frequency": frequency.to(device=self.device, dtype=self.dtype),
                  "index0": index0.to(self.device), "index1": index1.to(self.device),
                  "fraction": fraction.to(device=self.device, dtype=self.dtype),
                  "phase_offsets": tuple(selected),
                  "phase_probabilities": tuple(float(x) for x in probabilities),
                  "interpolation_variance_radians2": added_variance,
                  "discarded_phase_kernel_mass": discarded}
        self._transition_cache[dt] = result
        while len(self._transition_cache) > self.model.transition_cache_entries:
            self._transition_cache.popitem(last=False)
        return result

    def _advect(self, weights: torch.Tensor, transition: dict) -> torch.Tensor:
        shape = (1, 1, 1, self.frequency_states, self.phase_states)
        index0 = transition["index0"].view(shape).expand_as(weights)
        index1 = transition["index1"].view(shape).expand_as(weights)
        fraction = transition["fraction"].view(1, 1, 1, self.frequency_states, 1)
        return torch.gather(weights, -1, index0)*(1-fraction)+torch.gather(weights, -1, index1)*fraction

    def _predict(self, weights: torch.Tensor, dt: float) -> torch.Tensor:
        if dt == 0:
            return weights
        transition = self._transition(dt)
        predicted = self._advect(weights, transition)
        predicted = torch.einsum("...fp,fg->...gp", predicted, transition["frequency"])
        predicted = self._advect(predicted, transition)
        if len(transition["phase_offsets"]) != 1 or transition["phase_offsets"][0] != 0:
            mixed = torch.zeros_like(predicted)
            for offset, probability in zip(transition["phase_offsets"], transition["phase_probabilities"]):
                mixed.add_(torch.roll(predicted, offset, dims=-1), alpha=probability)
            predicted = mixed
        return predicted/predicted.sum(dim=(-3, -2, -1), keepdim=True)

    def _inputs(self, r: torch.Tensor, gram: torch.Tensor):
        if r.requires_grad or gram.requires_grad:
            raise ValueError("autograd inputs are unsupported by the bounded streaming likelihood")
        if r.shape != (self.batch_size, self.bits, self.taps):
            raise ValueError("r must have shape [batch,bits,taps]")
        if gram.shape == (self.bits, self.taps, self.taps):
            gram = gram[None].expand(self.batch_size, -1, -1, -1)
        elif gram.shape != (self.batch_size, self.bits, self.taps, self.taps):
            raise ValueError("G must have shape [bits,taps,taps] or [batch,bits,taps,taps]")
        if not r.is_complex() or not gram.is_complex() or r.dtype not in (torch.complex64, torch.complex128) or gram.dtype not in (torch.complex64, torch.complex128):
            raise ValueError("r and G require complex FP32 or FP64")
        r = r.to(device=self.device, dtype=self.complex_dtype)
        gram = gram.to(device=self.device, dtype=self.complex_dtype)
        if self.validate_inputs:
            if not bool(torch.isfinite(r).all()) or not bool(torch.isfinite(gram).all()):
                raise ValueError("nonfinite sufficient statistics")
            tolerance = 5e-6 if self.dtype == torch.float32 else 1e-10
            scale = gram.abs().amax(dim=(-2, -1)).clamp_min(1.)
            error = (gram-gram.conj().transpose(-1, -2)).abs().amax(dim=(-2, -1))
            if bool((error > tolerance*scale).any()):
                raise ValueError("G must be Hermitian")
            if bool((torch.linalg.eigvalsh(gram).amin(-1) < -tolerance*scale).any()):
                raise ValueError("G must be positive semidefinite")
            if bool(((gram.abs().amax(dim=(-2, -1)) == 0) & (r.abs().amax(dim=-1) != 0)).any()):
                raise ValueError("zero Gram requires zero sufficient observation")
        return r, gram

    def update(self, r: torch.Tensor, gram: torch.Tensor, dt: float = 0.) -> torch.Tensor:
        dt = _finite(dt, "block-center separation")
        r, gram = self._inputs(r, gram)
        predicted = self._predict(self.weights, dt)
        correlation = torch.einsum("qk,nbk->nbq", self.gain_vectors.conj(), r)
        energy = torch.einsum("qk,nbkl,ql->nbq", self.gain_vectors.conj(), gram, self.gain_vectors).real
        emission = 2*(correlation.real[..., None]*self.phase_cosine+
                      correlation.imag[..., None]*self.phase_sine)-energy[..., None]
        logits = predicted.log()+emission[..., None, :]
        maximum = logits.amax(dim=(-3, -2, -1), keepdim=True)
        unnormalized = (logits-maximum).exp()
        total = unnormalized.sum(dim=(-3, -2, -1), keepdim=True, dtype=torch.float64)
        increment = maximum[..., 0, 0, 0].to(torch.float64)+total[..., 0, 0, 0].log()
        posterior = unnormalized/total.to(self.dtype)
        empty = (gram.abs().amax(dim=(-2, -1)) == 0) & (r.abs().amax(dim=-1) == 0)
        self.weights = torch.where(empty[..., None, None, None], predicted, posterior)
        self._scores += torch.where(empty, torch.zeros_like(increment), increment)
        if self.validate_inputs and (not bool(torch.isfinite(self.weights).all()) or not bool(torch.isfinite(self._scores).all())):
            raise ValueError("phase/frequency inference became nonfinite")
        self.updates += 1
        self.elapsed_seconds += dt
        return self.scores()

    def scores(self) -> torch.Tensor:
        """Current per-bit log likelihood ratios; returned tensor is a copy."""
        return self._scores.clone()

    def diagnostics(self) -> dict:
        frequency_mean = float(self.initial_frequency_weights_cpu@self.frequency_grid_cpu)
        frequency_variance = float(self.initial_frequency_weights_cpu@(self.frequency_grid_cpu-frequency_mean).square())
        stationary_variance = stationary_decay = None
        if self.frequency_stationary_weights_cpu is not None:
            stationary = self.frequency_stationary_weights_cpu
            mean = float(stationary@self.frequency_grid_cpu)
            centered = self.frequency_grid_cpu-mean
            stationary_variance = float(stationary@centered.square())
            if stationary_variance > 0:
                stationary_decay = -float(stationary*centered@(self.frequency_generator@centered))/stationary_variance
        return {"algorithm": "finite_phase_frequency_static_gain_marginal_lr",
                "phase_states": self.phase_states, "frequency_states": self.frequency_states,
                "frequency_grid_hz": self.frequency_grid_cpu.tolist(),
                "frequency_spacing_hz": self.frequency_spacing_hz,
                "initial_frequency_weights": self.initial_frequency_weights_cpu.tolist(),
                "initial_frequency_mean_hz": frequency_mean,
                "initial_frequency_variance_hz2": frequency_variance,
                "generator_stationary_frequency_variance_hz2": stationary_variance,
                "generator_stationary_initial_correlation_decay_per_second": stationary_decay,
                "grid_convergence_scope": "automatic phase-grid changes also change frequency spacing; vary explicit frequency_grid_hz independently when separating the two discretization errors",
                "quadrature_states": self.quadrature_states,
                "frequency_model": self.model.frequency_model,
                "frequency_std_hz": self.model.frequency_std_hz,
                "frequency_correlation_seconds": self.model.frequency_correlation_seconds,
                "random_walk_hz_per_sqrt_second": self.model.random_walk_hz_per_sqrt_second,
                "phase_diffusion_rad_per_sqrt_second": self.model.phase_diffusion_rad_per_sqrt_second,
                "nominal_dt_seconds": self.model.nominal_dt_seconds,
                "transition_order": "half old-frequency phase advection; frequency CTMC; half new-frequency phase advection; independent phase CTMC",
                "initial_phase_prior": "uniform", "gain_prior_static_through_observation": True,
                "amplitude_nodes": list(self.model.amplitude_nodes),
                "amplitude_weights_normalized": _positive_weights(self.model.amplitude_weights, len(self.model.amplitude_nodes), "amplitude").tolist(),
                "relative_phase_states": self.model.relative_phase_states if self.taps == 2 else 1,
                "ray_amplitudes": list(self.ray_amplitudes),
                "phase_kernel_tail_probability": self.model.phase_kernel_tail_probability,
                "bulk_dtype": str(self.dtype), "log_evidence_dtype": "torch.float64",
                "tf32_allowed": False, "working_bytes_bound": self.working_bytes_bound,
                "memory_scope": "owned state/temporary/cache array estimate; excludes framework runtime and allocator cache",
                "maximum_phase_interpolation_variance_radians2_per_interval": self.maximum_interpolation_variance_radians2,
                "maximum_discarded_phase_kernel_mass": self.maximum_discarded_phase_kernel_mass,
                "maximum_frequency_transition_row_error_before_normalizing": self.maximum_transition_row_error,
                "updates": self.updates, "elapsed_center_separation_seconds": self.elapsed_seconds,
                "model_scope": "normalized finite-state likelihood with static proper gain quadrature and block-constant common phase; continuous colored oscillator and arbitrary within-block phase evolution are not exact models",
                "null_scope": "known white circular Gaussian null and fixed pre-observation priors; finite-precision validation/calibration is separate from the exact-arithmetic mean-one LR identity"}
