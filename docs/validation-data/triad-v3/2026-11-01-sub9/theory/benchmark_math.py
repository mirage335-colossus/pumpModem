"""Ideal presence-detection benchmarks; these do not model bit decisions.

All energies are total signal E/N0 across two ideal orthogonal ray channels.
The OU formulas assume stationary proper-complex Gaussian ray coefficients,
known timing, phase, ray Dopplers and stationary white Gaussian noise.
"""
import hashlib
import math
from pathlib import Path

import numpy as np
from scipy.linalg import eigh_tridiagonal

CN0_DB_HZ = -36.0
CN0 = 10 ** (CN0_DB_HZ / 10)
GATE = 21.653222759584384


def source_hashes(directory):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(Path(directory).glob("*.py"))}


def ou_logdet_one_ray(duration, coherence, cn0=CN0):
    """Exact continuous-time Fredholm log determinant of I + K for one ray."""
    z = math.sqrt(1 + cn0 * coherence)
    x = z * duration / coherence
    a = (1 + cn0 * coherence / 2) / z
    return (-duration / coherence + x
            + math.log((1 + a) / 2 + (1 - a) / 2 * math.exp(-2 * x)))


def ou_eigenvalues(duration, coherence, modes, cn0=CN0):
    """Point-sampled OU covariance eigenvalues; refine modes to check error."""
    dt = duration / modes
    rho = math.exp(-dt / coherence)
    diagonal = np.full(modes, 1 + rho * rho)
    diagonal[[0, -1]] = 1
    precision_eigenvalues = eigh_tridiagonal(
        diagonal, np.full(modes - 1, -rho), eigvals_only=True,
        lapack_driver="sterf")
    return ((cn0 / 2) * dt * (-math.expm1(-2 * dt / coherence))
            / precision_eigenvalues)


def ou_variance_h1(duration, coherence, cn0=CN0):
    return cn0 * cn0 / 2 * (
        duration * coherence
        - coherence * coherence / 2 * (-math.expm1(-2 * duration / coherence)))


def grid_count(duration):
    """Declared illustrative FFT calendar: 1024-s blocks, +/-1 microhertz."""
    nfft = 1 << (max(1, math.ceil(4 * duration / 1024)) - 1).bit_length()
    return 2 * math.ceil(1e-6 * nfft * 1024) + 1


FREQUENCY_CAVEAT = (
    "Hypothetical 0.22-dB scalar energy derating plus log(M) threshold cost. "
    "The derating is NOT a proved CFO-mismatch bound for stochastic OU "
    "covariance. The finite-mixture inequality L_mix >= L_matched/M holds "
    "only when an exactly matched component is present. This scenario is "
    "not a demonstrated or conservative continuous-CFO acquisition bound.")

PRESENCE_CAVEAT = (
    "Presence admission for the specified true-signal model only; no competing "
    "bit, timing, epoch, or model likelihood is simulated. These probabilities "
    "do not establish correct-bit reception or wrong-bit admission rates.")
