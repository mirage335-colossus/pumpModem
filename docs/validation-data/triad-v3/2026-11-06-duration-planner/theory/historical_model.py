"""Historical pure mathematics for the six duration-reference points.

Extracted from frozen-duration-model.py (SHA-256
7ab98c534f0c83b9f57894cda457706ec3c0b41f3f00919be168424df07fed06).
The four numerical functions below retain their original source verbatim.
The final threshold helper extracts only the fixed one-bit study allocation;
it is not the retired receiver's general duration planner.
"""
import math
import numpy as np
from scipy.special import gammainc

_nodes, _weights = np.polynomial.legendre.leggauss(64)


def _positive(value, name):
    if not math.isfinite(value) or value <= 0:
        raise ValueError(name + ' must be finite and positive')
    return float(value)



def effective_modes(seconds, width_hz, phase_diffusion, rays=1):
    """Trace-squared/trace-of-square of an ideal stationary ray covariance.

    r(t)=exp(-pi² B² t²/2 - sigma_phi² |t|/2). The integral includes
    finite observation edges and has the static-fading limit one mode/ray.
    Rays are assumed independently resolved. No carrier-label filtering occurs.
    """
    t = _positive(seconds, 'observation span')
    if min(width_hz, phase_diffusion) < 0 or not all(
            math.isfinite(x) for x in (width_hz, phase_diffusion)):
        raise ValueError('finite nonnegative covariance parameters required')
    if rays not in (1, 2):
        raise ValueError('one or two physical rays required')
    a, b = (math.pi * width_hz)**2, phase_diffusion**2
    rate = max(math.sqrt(a), b, 1/t)
    end = min(t*rate, 40.)
    x = (_nodes+1)*end/2
    integral = float(np.dot(_weights, (1-x/(rate*t)) *
        np.exp(-(a/rate/rate)*x*x-(b/rate)*x))) * end/2
    return max(float(rays), rays*t*rate/(2*integral))



def _x_minus_log1p(x):
    if x < 1e-4:
        return x*x*(.5-x/3+x*x/4-x*x*x/5)
    return x-math.log1p(x)



def gaussian_prediction(energy, modes, threshold):
    """Gamma equal-mode surrogate; its Chernoff bound is exact for this model.

    Under H1 L=lambda*Gamma(M,1)-M*log1p(lambda). Fractional M is an
    explicit moment surrogate, not actual finite-dimensional eigenvalues.
    """
    e, m = _positive(energy, 'energy'), _positive(modes, 'modes')
    lam = e/m
    mean = m*_x_minus_log1p(lam)
    delta = (mean-threshold)/e
    if delta <= 0:
        log_miss = 0.
    elif delta >= 1:
        log_miss = -math.inf
    else:
        # Avoid catastrophic cancellation in the inverse-square low-C/N0 regime.
        loss = (-delta*delta*(.5+delta/3+delta*delta/4+delta**3/5)
                if delta < 1e-4 else delta+math.log1p(-delta))
        log_miss = m*loss
    x = max(0., (threshold+m*math.log1p(lam))/lam)
    probability = float(1-gammainc(m, x))
    return dict(model='equal-mode Gaussian covariance likelihood', energy_n0=e,
        effective_modes=m, energy_per_mode=lam, mean_true_atom_log_lr=mean,
        modeled_detection_probability=probability,
        modeled_detection_lower_bound=-math.expm1(log_miss),
        log_miss_bound=None if log_miss == -math.inf else log_miss,
        tail_method='analytic Gamma lower-tail Chernoff; probability uses Gamma CDF',
        tail_scope='Bound for the equal-mode surrogate, not for the sampled channel/receiver')


def reference_threshold(native):
    """Original one-bit threshold, two durations, fixed original search mass.

    In plan_from_native, message_bits=1 makes grid_at's origin exactly 30 s
    for every candidate duration. Only the grid cardinality is needed here.
    Counting formulas are from source-current/tools/triad_operational_admission.py
    SHA-256 9cc99ce26c445c4c87d72600fc621b60ff9d0b61da9bed3e4f3174272904d0ea.
    This helper is scoped to the retained native-plan fixture, not arbitrary
    message lengths, current receiver grids or an operational admission API.
    """
    p=native['physics']
    attempts=math.ceil(86400/.4)+1
    log_e_threshold=math.log(attempts*2/1e-4)
    origin=30.
    drift=origin*float(p['sample_clock_pair_ppm'])*float(p['spread_margin'])*1e-6
    lo,hi=p['arrival_prior_seconds']
    lo,hi=lo-drift,hi+drift
    margin=float(p['spread_margin'])
    cfo_bound=float(p['deterministic_cfo_bound_hz'])*margin
    clock_bound=float(p['sample_clock_pair_ppm'])*margin
    arrival_intervals=max(1,math.ceil((hi-lo)/.001))
    frequency_radius=math.ceil(cfo_bound/.01)
    counts=(13,arrival_intervals+1 if hi>lo else 1,
            2*frequency_radius+1,9 if clock_bound else 1)
    candidate_count=math.prod(counts)
    # Two bit labels and three fixed coherence alternatives (.5, 2, infinity).
    return log_e_threshold+math.log(2*candidate_count*3)

