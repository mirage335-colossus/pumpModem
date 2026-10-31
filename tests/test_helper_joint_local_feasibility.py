#!/usr/bin/env python3
"""Idealized per-dwell joint-estimation feasibility, not a waveform simulation.

Each summary is a centered, locally linear pilot observation. Gaussian errors,
their covariance, the source phase gauge and the clock-prediction covariance are
known. There is no acquisition, leakage, retune, selection or qualification.
The generated samples and truth are never passed to the receiver.
"""
import math
import random


def weights(vh, vl, cross):
    den = vh + vl - 2 * cross
    return (vl - cross) / den, (vh - cross) / den


def run(difference_bias, seed=91037, trials=30000):
    rng = random.Random(seed)
    # Representative local uncertainties, not calibrated values for a link.
    sh, sl, sp, rho = .057, .081, .040, .3
    sfh, sfl, srate = .12, .17, .04
    covariance = rho * sh * sp
    vh, vl = sh * sh, sl * sl + sp * sp
    wh, wl = weights(vh, vl, covariance)
    fwh, fwl = weights(sfh * sfh, sfl * sfl + srate * srate, 0.)
    phase_squared = high_squared = freq_squared = high_freq_squared = 0.
    max_equivalence = 0.
    # Gauge constants represent a single unknown reference retained across
    # dwells. The receiver has only its noisy differential prediction.
    reference_phase, differential_gauge = .73, 1.31
    for _ in range(trials):
        h, l, independent = (rng.gauss(0., 1.) for _ in range(3))
        eh, el = sh * h, sl * l
        ep = sp * (rho * h + math.sqrt(1-rho*rho) * independent)
        # High-minus-low differential prediction includes its persistent gauge.
        high = reference_phase + eh
        low = reference_phase - differential_gauge + el
        aligned_low = low + differential_gauge + ep
        separate = wh * high + wl * aligned_low
        # Independent normal-equation implementation of the same GLS fit.
        determinant = vh * vl - covariance * covariance
        inv00, inv11, inv01 = vl/determinant, vh/determinant, -covariance/determinant
        joint = ((inv00+inv01)*high + (inv11+inv01)*aligned_low) / (inv00+inv11+2*inv01)
        max_equivalence = max(max_equivalence, abs(joint-separate))
        phase_squared += (joint-reference_phase)**2
        high_squared += (high-reference_phase)**2
        fh = sfh * rng.gauss(0., 1.)
        fl = sfl * rng.gauss(0., 1.) + srate * rng.gauss(0., 1.) + difference_bias
        freq = fwh * fh + fwl * fl
        freq_squared += freq*freq
        high_freq_squared += fh*fh
    assert max_equivalence < 1e-12
    phase_rms, high_rms = math.sqrt(phase_squared/trials), math.sqrt(high_squared/trials)
    freq_rms, high_freq_rms = math.sqrt(freq_squared/trials), math.sqrt(high_freq_squared/trials)
    print(f'bias_hz={difference_bias:.3f} trials={trials} '
          f'high_phase_rms_rad={high_rms:.6f} joint_phase_rms_rad={phase_rms:.6f} '
          f'high_frequency_rms_hz={high_freq_rms:.6f} joint_frequency_rms_hz={freq_rms:.6f} '
          f'phase_equivalence_max={max_equivalence:.3g} low_phase_weight={wl:.6f} '
          f'low_frequency_weight={fwl:.6f}')
    return freq_rms


if __name__ == '__main__':
    matched = run(0.)
    mismatched = run(.20)
    severely_mismatched = run(.40)
    assert mismatched > matched
    assert severely_mismatched > mismatched
