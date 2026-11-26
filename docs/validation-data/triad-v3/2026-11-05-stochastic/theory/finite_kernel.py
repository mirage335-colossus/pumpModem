#!/usr/bin/env python3
"""Independent deterministic covariance calculation, not sampled reception.

Reconstructs fixed-grid-gaussian-v1's finite kernel/interpolator from its
published formula. A uniform random grid origin makes a stationary mixture of
conditional Gaussian processes. Single-time marginals remain standard normal
under ideal independent normal innovations; a covariance-only Gaussian KL
formula is not thereby exact for the unconditional joint law.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

# Identity of the historical model, not the current native implementation.
HISTORICAL_SOURCE = 'src/triad_random_process.hpp'
HISTORICAL_SOURCE_SHA256 = '3ea0ab1fcc2a64f58b53751d69d707d278b5fa531b3c126baa443fa8701e7741'
HISTORICAL_SCRIPT_SHA256 = '7d80040ee47f37a351a54bd5224522fb3e674fb2e9260582561ee7eb34dc7865'
HISTORICAL_REVISION = 'a6678218dc88827fe5b58e6016cd85a393928d43'


def weights(v):
    return np.array([-.5*v+v*v-.5*v**3, 1-2.5*v*v+1.5*v**3,
                     .5*v+2*v*v-1.5*v**3, -.5*v*v+.5*v**3]).T


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='fresh report path; retained historical results are separate')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output must not already exist')
    k = np.arange(-26, 27)
    h = np.exp(-(np.pi*k/16)**2)
    h /= np.linalg.norm(h)
    covariance = np.r_[np.correlate(h, h, 'full')[52:], np.zeros(200)]
    index = np.arange(4)-1
    phases = (np.arange(512)+.5)/512
    first = weights(phases)
    local_covariance = covariance[abs(index[:, None]-index)]
    variance = np.einsum('ni,ij,nj->n', first, local_covariance, first)

    def correlation(dt):
        coordinate = phases+16*dt
        whole = np.floor(coordinate).astype(int)
        second = weights(coordinate-whole)
        second_variance = np.einsum('ni,ij,nj->n', second, local_covariance, second)
        cross = covariance[abs(whole[:, None, None]+index[None, None, :]-index[None, :, None])]
        return np.mean(np.einsum('ni,nij,nj->n', first, cross, second)/
                       np.sqrt(variance*second_variance))

    delays = np.linspace(0, 4, 2049)
    response = np.array([correlation(t) for t in delays])
    tau2 = float(2*np.trapezoid(response*response, delays))
    result = {
        'schema': 'triad-fixed-kernel-analytic-check-v1',
        'method': 'Deterministic finite-kernel covariance; 512 midpoint grid phases; trapezoid 2049 lags over [0,4/B].',
        'width_B_hz': 1,
        'interpolation_variance_min': float(variance.min()),
        'interpolation_variance_max': float(variance.max()),
        'covariance_at_quarter_width_time': float(correlation(.25)),
        'ideal_covariance_at_quarter_width_time': math.exp(-.5*(math.pi*.25)**2),
        'squared_mean_covariance_integral_seconds': tau2,
        'ideal_squared_coherence_integral_seconds': 1/math.sqrt(math.pi),
        'relative_tau2_difference': tau2*math.sqrt(math.pi)-1,
        'source': HISTORICAL_SOURCE,
        'source_sha256': HISTORICAL_SOURCE_SHA256,
        'script_sha256': HISTORICAL_SCRIPT_SHA256,
        'reproducer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'provenance_scope': 'source/script/base identify the frozen historical calculation; reproducer_sha256 identifies this portable recovery.',
        'base_revision': HISTORICAL_REVISION,
        'limitations': [
            'Not a Monte Carlo receiver result or an exhaustive spectral-tail/alias qualification.',
            'Numerical quadrature is a diagnostic, not an interval-certified integration bound.',
            'Random interpolation-grid origin makes the stationary joint law a mixture of conditional Gaussian laws; a single-time unit-normal marginal is insufficient for an exact unconditional Gaussian likelihood.',
            'A rigorous information bound can reveal the grid origin to a genie and average conditional bounds; the integral of squared mean covariance reported here is not that conditional second-moment bound.',
            'Native motion capture guard uses nominal RF+IF+source extent, omitting oscillator-error times delay-slope cross terms; the oscillator-off frozen campaign has zero such cross term.'
        ]
    }
    target = args.output
    target.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
