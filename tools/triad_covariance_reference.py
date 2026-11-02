#!/usr/bin/env python3
"""Small exact references for a proper-complex Gaussian triad channel.

This is an analysis helper, not a receiver or a model of the dispersive
selective-modulation stage.  It treats each bit's supplied sample/tap basis as
deterministic and tap gains as a zero-mean Gaussian process with

    C_ij = exp(-.5 * (pi * B * (t_i-t_j))**2),

where B is twice the RMS Doppler, matching the Gaussian prior used by
``triad_gaussian_window.py``.  The returned covariance is

    K_b = sum_k p_k diag(A_b[:,k]) C diag(A_b[:,k])^H.

It is an exact finite-matrix likelihood for this explicitly stated channel
prior and basis.  A finite seeded phase/amplitude law such as the corrected
selective stage is generally not a Gaussian channel; substituting its average
covariance here is not an exact likelihood for that law.

Dimensions are deliberately bounded to 512 samples.  Use this as a small
matched reference for checking block-centre approximations, not as a dense
multi-day receiver implementation.  No simulator truth or receiver threshold
is supplied by this module.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Literal

import numpy as np
from scipy.linalg import solve_triangular
from scipy.special import ndtr


MAX_SAMPLES = 512
Precision = Literal["fp64", "fp32-bulk"]


def _finite_real_array(value, name: str, *, ndim: int | None = None) -> np.ndarray:
    original = np.asarray(value)
    if original.dtype.kind not in "iuf":
        raise ValueError(f"{name} must contain real numeric values")
    result = np.asarray(original, dtype=np.float64)
    if ndim is not None and result.ndim != ndim:
        raise ValueError(f"{name} must have {ndim} dimensions")
    if not np.isfinite(result).all():
        raise ValueError(f"{name} must be finite")
    return result


def _basis_array(basis) -> np.ndarray:
    value = np.asarray(basis)
    if value.ndim != 3:
        raise ValueError("basis must have shape [bits, samples, taps]")
    if value.shape[0] < 2 or value.shape[1] < 1 or value.shape[2] < 1:
        raise ValueError("basis requires at least two bits, one sample and one tap")
    if value.shape[1] > MAX_SAMPLES:
        raise ValueError(f"dense covariance reference is limited to {MAX_SAMPLES} samples")
    result = np.asarray(value, dtype=np.complex128)
    if not np.isfinite(result).all():
        raise ValueError("basis must be finite")
    return result


def _precision(value: str) -> Precision:
    if value not in ("fp64", "fp32-bulk"):
        raise ValueError("precision must be 'fp64' or 'fp32-bulk'")
    return value  # type: ignore[return-value]


def gaussian_coefficient_covariance(times_seconds, doppler_width_hz: float) -> np.ndarray:
    """Return the normalized SE coefficient covariance for physical times."""
    times = _finite_real_array(times_seconds, "sample times", ndim=1)
    if times.size < 1 or times.size > MAX_SAMPLES:
        raise ValueError(f"sample count must be in 1..{MAX_SAMPLES}")
    if times.size > 1 and np.any(np.diff(times) <= 0):
        raise ValueError("sample times must increase strictly")
    if not math.isfinite(doppler_width_hz) or doppler_width_hz < 0:
        raise ValueError("Doppler width must be finite and nonnegative")
    # Use local differences: long absolute epochs must not erase small lags.
    local = times - times[0]
    delta = local[:, None] - local[None, :]
    argument = math.pi * float(doppler_width_hz) * delta
    with np.errstate(over="ignore"):
        covariance = np.exp(-0.5 * argument * argument)
    return covariance


def block_center_coefficient_covariance(times_seconds, doppler_width_hz: float,
                                        *, block_samples: int,
                                        window_samples: int) -> np.ndarray:
    """SE covariance sampled at block centres, independent between windows.

    Every sample in a block receives that block's centre coefficient.  Windows
    are independent; within each window their block-centre coefficients retain
    the same SE covariance as the exact reference.  ``window_samples`` must be
    a multiple of ``block_samples`` so no block straddles an independence edge.
    This matches the mathematical approximation used by the existing Gaussian
    window receiver while retaining its explicit independent-window premise.
    """
    times = _finite_real_array(times_seconds, "sample times", ndim=1)
    if times.size < 1 or times.size > MAX_SAMPLES:
        raise ValueError(f"sample count must be in 1..{MAX_SAMPLES}")
    if times.size > 1 and np.any(np.diff(times) <= 0):
        raise ValueError("sample times must increase strictly")
    for value, name in ((block_samples, "block_samples"),
                        (window_samples, "window_samples")):
        if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or value < 1:
            raise ValueError(f"{name} must be a positive integer")
    if window_samples % block_samples:
        raise ValueError("window_samples must be a multiple of block_samples")
    # The covariance constructor validates Doppler width and dense extent.
    if not math.isfinite(doppler_width_hz) or doppler_width_hz < 0:
        raise ValueError("Doppler width must be finite and nonnegative")
    sample_index = np.arange(times.size, dtype=np.int64)
    blocks_per_window = window_samples // block_samples
    block_id = sample_index // block_samples
    window_id = block_id // blocks_per_window
    first = block_id * block_samples
    stop = np.minimum(first + block_samples, times.size)
    # A block's physical centre uses the actual sample-time coordinates,
    # including irregular final spacing if the caller supplied it.
    centers_by_block = np.array([
        .5 * (times[a] + times[z - 1]) for a, z in
        ((int(a), int(z)) for a, z in zip(
            np.r_[0, np.flatnonzero(np.diff(block_id)) + 1],
            np.r_[np.flatnonzero(np.diff(block_id)) + 1, times.size]))
    ], dtype=np.float64)
    center = centers_by_block[block_id]
    delta = center[:, None] - center[None, :]
    argument = math.pi * float(doppler_width_hz) * delta
    with np.errstate(over="ignore"):
        covariance = np.exp(-0.5 * argument * argument)
    covariance *= (window_id[:, None] == window_id[None, :])
    return covariance


def channel_covariances(basis, sample_times_seconds, doppler_width_hz: float,
                        tap_powers, *, block_samples: int | None = None,
                        window_samples: int | None = None) -> np.ndarray:
    """Build [bits,samples,samples] signal covariance from a public basis.

    Supplying neither block argument builds the full continuous SE covariance.
    Supplying both builds the block-centre, independent-window approximation.
    Covariance construction is strict complex FP64. Preserving positive
    semidefiniteness matters to subsequent likelihood factorization, and a
    complex64 pointwise construction can introduce negative eigenvalues from
    loss of conjugate symmetry. The separate likelihood-scoring option allows
    explicit FP32 whitening against this strict reference covariance.
    """
    A = _basis_array(basis)
    bits, samples, taps = A.shape
    times = _finite_real_array(sample_times_seconds, "sample times", ndim=1)
    if times.shape != (samples,):
        raise ValueError("one sample time is required for every basis row")
    powers = _finite_real_array(tap_powers, "tap powers", ndim=1)
    if powers.shape != (taps,) or np.any(powers < 0):
        raise ValueError("tap powers must match the nonnegative basis taps")
    if (block_samples is None) != (window_samples is None):
        raise ValueError("block_samples and window_samples must be supplied together")
    if block_samples is None:
        coefficient_covariance = gaussian_coefficient_covariance(times, doppler_width_hz)
    else:
        coefficient_covariance = block_center_coefficient_covariance(
            times, doppler_width_hz, block_samples=block_samples,
            window_samples=window_samples)

    accumulated = np.zeros((bits, samples, samples), dtype=np.complex128)
    for tap, power in enumerate(powers):
        a = A[:, :, tap]
        accumulated += power * (a[:, :, None] * coefficient_covariance[None, :, :]) * a.conj()[:, None, :]
    return accumulated


@dataclass(frozen=True)
class GaussianFactor:
    """Reusable strict-FP64 factor and normalization for one covariance."""
    cholesky: np.ndarray
    log_determinant: float
    noise_variance: float

    @property
    def samples(self) -> int:
        return self.cholesky.shape[0]


def factor_covariance(signal_covariance, noise_variance: float) -> GaussianFactor:
    """Factor R=noise_variance*I+K with strict complex FP64 Cholesky."""
    K = np.asarray(signal_covariance, dtype=np.complex128)
    if K.ndim != 2 or K.shape[0] != K.shape[1] or not 1 <= K.shape[0] <= MAX_SAMPLES:
        raise ValueError(f"signal covariance must be square with extent 1..{MAX_SAMPLES}")
    if not np.isfinite(K).all():
        raise ValueError("signal covariance must be finite")
    if not math.isfinite(noise_variance) or noise_variance <= 0:
        raise ValueError("noise variance must be positive and finite")
    scale = max(float(np.max(np.abs(K))), np.finfo(np.float64).tiny)
    if float(np.max(np.abs(K - K.conj().T))) > 1e-10 * scale:
        raise ValueError("signal covariance is not Hermitian")
    K = .5 * (K + K.conj().T)
    # Since K is a sum of PSD tap terms, a material negative eigenvalue is an
    # implementation/input defect. Cholesky of R catches it without adding a
    # hidden diagonal jitter; the stricter diagnostic is useful to callers.
    eigenvalues = np.linalg.eigvalsh(K)
    if eigenvalues[0] < -1e-10 * scale:
        raise ValueError("signal covariance is not positive semidefinite")
    R = K.copy()
    R.flat[::R.shape[0] + 1] += float(noise_variance)
    if not np.isfinite(R).all():
        raise ValueError("noise plus signal covariance overflowed")
    try:
        L = np.linalg.cholesky(R)
    except np.linalg.LinAlgError as error:
        raise ValueError("noise plus signal covariance is not positive definite") from error
    logdet = float(2.0 * np.log(np.real(np.diag(L))).sum(dtype=np.float64))
    if not math.isfinite(logdet):
        raise ValueError("covariance log determinant is not finite")
    return GaussianFactor(L, logdet, float(noise_variance))


def gaussian_log_likelihood(samples, factor: GaussianFactor,
                            *, precision: str = "fp64") -> np.ndarray:
    """Score samples under one CN(0,R) law, omitting common -N log(pi).

    Samples have shape [trials,N] (or [N]) and are already represented in
    complex128. The optional FP32-bulk mode lowers only triangular whitening;
    the Cholesky/logdet preparation and quadratic/evidence reductions stay
    FP64. It casts the supplied samples and reusable factor to complex64 only
    after finite normal-range checks; overflow and nonzero subnormal inputs or
    outputs fail clearly. Scale the observation/covariance consistently before
    comparing arithmetic when its normalized components lie outside that
    range. It is an arithmetic experiment, not a qualified receiver mode.
    """
    arithmetic = _precision(precision)
    y = np.asarray(samples)
    single = y.ndim == 1
    if single:
        y = y[None, :]
    if y.ndim != 2 or y.shape[1] != factor.samples:
        raise ValueError("samples must have shape [trials,covariance samples]")
    y = np.asarray(y, dtype=np.complex128)
    if y.shape[0] < 1 or not np.isfinite(y).all():
        raise ValueError("at least one finite sample vector is required")
    if arithmetic == "fp32-bulk":
        limit = np.finfo(np.float32).max
        normal = np.finfo(np.float32).tiny
        def require_fp32_components(values, name):
            for component in (values.real, values.imag):
                absolute = np.abs(component)
                if np.any(absolute > limit):
                    raise ValueError(f"FP32-bulk {name} exceeds finite FP32 range")
                if np.any((absolute != 0) & (absolute < normal)):
                    raise ValueError(f"FP32-bulk {name} contains nonzero subnormal-range values")
        require_fp32_components(factor.cholesky, "factor")
        require_fp32_components(y, "sample")
        L = factor.cholesky.astype(np.complex64)
        rhs = y.astype(np.complex64).T
        if not np.isfinite(L).all() or not np.isfinite(rhs).all():
            raise ValueError("FP32-bulk whitening cast is nonfinite")
        with np.errstate(over="ignore", invalid="ignore", divide="ignore"):
            whitened = solve_triangular(L, rhs, lower=True, check_finite=False)
        if not np.isfinite(whitened).all():
            raise ValueError("FP32-bulk whitening exceeded finite arithmetic range")
        require_fp32_components(whitened, "whitened sample")
        if np.any(rhs != 0) and not np.any(whitened != 0):
            raise ValueError("FP32-bulk whitening underflowed all sample evidence")
        w = whitened.astype(np.complex128)
    else:
        w = solve_triangular(factor.cholesky, y.T, lower=True, check_finite=False)
    with np.errstate(over="ignore", invalid="ignore"):
        quadratic = (w.real * w.real + w.imag * w.imag).sum(axis=0, dtype=np.float64)
    if not np.isfinite(quadratic).all():
        raise ValueError("Gaussian quadratic evidence is outside finite FP64 range")
    result = -factor.log_determinant - quadratic
    if not np.isfinite(result).all():
        raise ValueError("Gaussian log likelihood is outside finite FP64 range")
    return result[0] if single else result


def covariance_log_likelihoods(samples, signal_covariances, noise_variance: float,
                               *, precision: str = "fp64") -> np.ndarray:
    """Score every supplied bit covariance against identical observations."""
    covariances = np.asarray(signal_covariances, dtype=np.complex128)
    if covariances.ndim != 3 or covariances.shape[1] != covariances.shape[2]:
        raise ValueError("signal covariances must have shape [bits,N,N]")
    if covariances.shape[0] < 2:
        raise ValueError("at least two bit covariances are required")
    factors = [factor_covariance(covariances[bit], noise_variance)
               for bit in range(covariances.shape[0])]
    return np.stack([gaussian_log_likelihood(samples, factor, precision=precision)
                     for factor in factors], axis=-1)


def _relative_covariance_difference(first_covariance, second_covariance,
                                    noise_variance):
    """Eigenvalues of L1^-1 (K0-K1) L1^-H, without subtracting noise."""
    K0 = np.asarray(first_covariance, dtype=np.complex128)
    K1 = np.asarray(second_covariance, dtype=np.complex128)
    if K0.ndim != 2 or K0.shape[0] != K0.shape[1] or K1.shape != K0.shape:
        raise ValueError("both signal covariances must be square and have equal shape")
    # Validate both physical covariances. Form their difference before adding
    # the common noise: weak-signal information is quadratic in this difference.
    factor_covariance(K0, noise_variance)
    f1 = factor_covariance(K1, noise_variance)
    difference = K0 - K1
    left = solve_triangular(f1.cholesky, difference, lower=True, check_finite=False)
    relative = solve_triangular(f1.cholesky, left.conj().T,
                                lower=True, check_finite=False).conj().T
    if not np.isfinite(relative).all():
        raise ValueError("relative covariance difference is outside finite range")
    delta = np.linalg.eigvalsh(.5 * (relative + relative.conj().T))
    if not np.isfinite(delta).all() or np.any(delta <= -1):
        raise ValueError("relative covariance is numerically unresolved; rescale or use a better-conditioned reference")
    return delta


def covariance_kl(first_covariance, second_covariance,
                  noise_variance: float) -> float:
    """D(CN(0,R0)||CN(0,R1)) in nats, stable for nearby hypotheses.

    Sum delta-log1p(delta) over relative covariance eigenvalues. A short
    convergent series avoids cancellation of the linear terms near zero.
    No precision wider than FP64 is required by this reference calculation.
    """
    delta = _relative_covariance_difference(first_covariance, second_covariance,
                                             noise_variance)
    small = np.abs(delta) < .01
    terms = np.empty_like(delta)
    x = delta[small]
    polynomial = np.zeros_like(x)
    for order in range(12, 1, -1):
        polynomial = polynomial * x + (-1.)**order / order
    terms[small] = x*x*polynomial
    terms[~small] = delta[~small] - np.log1p(delta[~small])
    result = float(terms.sum(dtype=np.float64))
    if not math.isfinite(result) or result < 0:
        raise ValueError("covariance KL is outside its finite nonnegative range")
    return result


def covariance_bhattacharyya(first_covariance, second_covariance,
                             noise_variance: float) -> float:
    """Bhattacharyya distance for two zero-mean proper-complex Gaussian laws.

    Returns ``logdet((R0+R1)/2) - (logdet(R0)+logdet(R1))/2`` in nats.
    The covariance is complex proper-normal; no extra real-Gaussian factor of
    one half is applied to this log-determinant expression.
    """
    delta = _relative_covariance_difference(first_covariance, second_covariance,
                                             noise_variance)
    # Equivalent to log1p(delta/2)-log1p(delta)/2, but with no
    # cancellation of two first-order terms. The operation order avoids
    # squaring large delta before dividing by 1+delta.
    distance = float(.5 * np.log1p((.25*delta)*(delta/(1+delta))).sum(dtype=np.float64))
    if not math.isfinite(distance) or distance < 0:
        raise ValueError("Bhattacharyya distance is outside its finite nonnegative range")
    return distance


def known_realized_channel_error_probability(first_mean, second_mean,
                                             noise_variance: float) -> float:
    """Equal-prior optimum error for two known means and CN(0,σ² I) noise.

    `P_e = Q(||s0-s1|| / sqrt(2 σ²))`. This bit-classification bound assumes
    the complete realized waveform/channel and alignment are known. It is not
    a signal-versus-noise or false-admission threshold.
    """
    s0 = np.asarray(first_mean, dtype=np.complex128)
    s1 = np.asarray(second_mean, dtype=np.complex128)
    if s0.ndim != 1 or s1.shape != s0.shape or not np.isfinite(s0).all() or not np.isfinite(s1).all():
        raise ValueError("known means must be equally sized finite complex vectors")
    if not math.isfinite(noise_variance) or noise_variance <= 0:
        raise ValueError("noise variance must be positive and finite")
    distance = float(np.linalg.norm(s0 - s1))
    return float(ndtr(-distance / math.sqrt(2.0 * noise_variance)))


def proper_complex_gaussian_samples(factor: GaussianFactor, count: int, *, seed: int) -> np.ndarray:
    """Generate deterministic diagnostic draws from one already factored law."""
    if isinstance(count, bool) or not isinstance(count, int) or count < 1:
        raise ValueError("count must be a positive integer")
    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed <= 2**64 - 1:
        raise ValueError("seed must be an unsigned 64-bit integer")
    rng = np.random.default_rng(seed)
    # Draw and multiply in chunks to bound the sample-by-dimension complex
    # temporaries independently of requested Monte Carlo count.
    output = np.empty((count, factor.samples), dtype=np.complex128)
    chunk = max(1, min(count, 2_000_000 // max(1, factor.samples)))
    for first in range(0, count, chunk):
        stop = min(first + chunk, count)
        z = (rng.standard_normal((stop-first, factor.samples)) +
             1j * rng.standard_normal((stop-first, factor.samples))) / math.sqrt(2.0)
        output[first:stop] = z @ factor.cholesky.T
    return output
