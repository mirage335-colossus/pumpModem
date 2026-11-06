#!/usr/bin/env python3
"""Measure actual native pattern probes, without propagating or renormalizing them.

Window quantiles describe the exported realization, not independent statistical
trials or physical-radio AGC qualification. Use native channel/receiver studies
for sensitivity. This tool is intentionally cheap and reusable across probes.
"""
from __future__ import annotations
import argparse
import itertools
import json
import math
from pathlib import Path
import numpy as np


def window_mean(values, rows):
    values = np.asarray(values, dtype=np.float64)
    if not 1 <= rows <= len(values):
        raise ValueError('window must fit the observation')
    cumulative = np.concatenate(([0.], np.cumsum(values, dtype=np.float64)))
    return (cumulative[rows:] - cumulative[:-rows]) / rows


def envelope_metrics(power, envelope, fs, seconds, significant_step_db):
    rows = round(fs * seconds)
    if rows < 8 or rows > len(power):
        return dict(window_seconds=seconds, measured=False,
                    reason='fewer than eight samples/window or shorter recording')
    mean = window_mean(power, rows)
    duty = window_mean(np.square(envelope), rows)
    good = duty > .1
    if not np.any(good):
        return dict(window_seconds=seconds, measured=False, reason='no active DATA windows')
    # This removes only the known *measurement* duty baseline. It does not
    # modify emitted samples, restore lost energy or flatten radio fading.
    duty_corrected = mean[good] / duty[good]
    reference = float(np.median(duty_corrected))
    db = 10 * np.log10(np.maximum(duty_corrected, 1e-300) / reference)
    quantiles = np.quantile(db, [0, .01, .05, .5, .95, .99, 1])
    flat = duty[good] > 1 - 1e-12
    return dict(window_seconds=seconds, actual_window_seconds=rows/fs, measured=True, window_samples=rows,
        overlapping_windows=int(good.sum()), nominal_duty_corrected=True,
        power_median=reference, rms_deviation_db_quantiles=quantiles.tolist(),
        quantile_probabilities=[0,.01,.05,.5,.95,.99,1],
        maximum_absolute_deviation_db=float(np.abs(db).max()),
        significant_step_db=significant_step_db,
        quarter_step_fraction_within=float(np.mean(np.abs(db) <= .25*significant_step_db)),
        strict_all_windows_within_quarter_step=bool(np.all(np.abs(db) <= .25*significant_step_db)),
        flat_envelope_windows=int(flat.sum()),
        flat_envelope_max_abs_db=float(np.abs(db[flat]).max()) if flat.any() else None)


def spectrum_metrics(values, fs, bandwidth):
    if len(values) < 3:
        raise ValueError('at least three samples required for a spectral diagnostic')
    window = np.hanning(len(values))
    transform = np.fft.fft(values * window)
    power = np.abs(transform)**2
    frequency = np.fft.fftfreq(len(values), 1/fs)
    total = float(power.sum())
    if not math.isfinite(total) or total <= 0:
        raise ValueError('no finite source energy in the spectral window; select active DATA')
    order = np.argsort(np.abs(frequency))
    fraction = np.cumsum(power[order]) / total
    widths = {str(q):float(2*abs(frequency[order[min(np.searchsorted(fraction,q),len(order)-1)]]))
              for q in (.99,.999,.9999)}
    energy = np.abs(values)**2
    variance = energy-energy.mean()
    modulation = np.abs(np.fft.rfft(variance * window))**2
    modulation[0] = 0
    peak = int(np.argmax(modulation))
    return dict(method='Hann-window finite-record periodogram; no hard spectral mask',
        occupied_width_hz=widths, nominal_bandwidth_hz=bandwidth,
        energy_outside_nominal_fraction=float(power[np.abs(frequency)>bandwidth/2].sum()/total),
        data_at_helper_offsets_fraction={str(center):float(power[np.abs(frequency-center)<=5].sum()/total)
            for center in (-1175., -775., 675.) if abs(center)+5 < fs/2},
        normalized_fourth_moment=float(np.mean(energy**2)/np.mean(energy)**2),
        pseudo_covariance_ratio=float(abs(np.mean(values**2))/np.mean(energy)),
        peak_to_average_power_db=float(10*np.log10(energy.max()/energy.mean())),
        largest_power_modulation_bin_hz=float(peak*fs/len(values)),
        largest_power_modulation_bin_fraction=float(modulation[peak]/modulation.sum()) if modulation.sum() else 0.)


def coefficient_metrics(coefficients):
    power = np.sum(np.abs(coefficients)**2, axis=-1)
    valid = np.all(power > 0, axis=0)
    if not valid.any():
        return dict(cells=0, payload_cells=0)
    coefficients = coefficients[:,valid]
    amplitude = np.abs(coefficients)
    payload = np.max(np.abs(coefficients[0]-coefficients[1]),axis=-1)>1e-12
    permutations = list(itertools.permutations(range(3)))
    if not payload.any():
        return dict(cells=int(valid.sum()), payload_cells=0)
    left,right = amplitude[:,payload]
    errors = [float(np.mean((left[:,p]-right)**2)) for p in permutations]
    lp,rp=left**2,right**2
    return dict(cells=int(valid.sum()), payload_cells=int(payload.sum()),
        maximum_summed_power_error=float(np.abs(power[:,valid]-3).max()),
        smallest_carrier_radius=float(amplitude.min()),
        best_fixed_amplitude_permutation=list(permutations[int(np.argmin(errors))]),
        best_fixed_amplitude_permutation_mse=min(errors),
        amplitude_ratio_distance_squared=float(np.mean(np.sum((left/np.linalg.norm(left,axis=1)[:,None]-
             right/np.linalg.norm(right,axis=1)[:,None])**2,axis=1))),
        bit_alternative_power_correlation=float(np.corrcoef(lp.reshape(-1),rp.reshape(-1))[0,1]))


def measure(directory):
    directory=Path(directory); m=json.loads((directory/'manifest.json').read_text())
    if m.get('schema') != 'triad-pattern-probe-v1':
        raise ValueError('native triad-pattern-probe-v1 manifest required')
    def array(file, shape, dtype='<c16'):
        if (directory/file).stat().st_size != math.prod(shape)*np.dtype(dtype).itemsize:
            raise ValueError('probe file extent differs from public shape')
        return np.memmap(directory/file,mode='r',dtype=dtype,shape=tuple(shape))
    x=array(m['composite_file'],m['composite_shape'])
    carriers=array(m['carrier_file'],m['carrier_shape'])
    coefficients=array(m['coefficients_file'],m['coefficients_shape'])
    envelope=array(m['envelope_file'],m['envelope_shape'],'<f8')
    fs=m['sample_rate_hz']; spacing=m['carrier_spacing_hz']
    # The established base alphabet's smallest meaningful radius step is
    # 1 -> sqrt(1.6). Report Added step separately, since normalized composite
    # products contain smaller coincidental differences between combinations.
    step_db=10*math.log10(1.6)
    depth=m['format']['added_amplitude_depth']
    norms=np.linalg.norm(x,axis=1)
    if not np.isfinite(norms).all() or np.any(norms <= 0):
        raise ValueError('no finite source energy for both alternatives; select an active DATA window')
    normalized=x/norms[:,None]
    correlation=complex(np.vdot(normalized[0],normalized[1]))
    return dict(schema='triad-pattern-metrics-v1', source=m,
        scope='native source-only measurements; empirical overlapping-window extrema, not confidence bounds or physical AGC guarantees',
        coefficient=coefficient_metrics(coefficients),
        added_unbalanced_radius_step_db=10*math.log10((1+depth)/(1-depth)),
        normalized_complex_bit_correlation=[correlation.real,correlation.imag],
        coherent_distance_squared=float(np.sum(np.abs(normalized[0]-normalized[1])**2)),
        unknown_global_phase_distance_squared=float(2-2*abs(correlation)),
        unknown_phase_separation_penalty_relative_orthogonal_db=float(-10*np.log10(max(1-abs(correlation),1e-300))),
        locally_constant_adjacent_beat_retained={str(w):float(abs(np.sinc(spacing*w))) for w in (.1,.8)},
        bits=[dict(bit=b,mean_power=float(np.mean(abs(x[b])**2)),
            spectrum=spectrum_metrics(x[b],fs,3*spacing),
            composite_windows=[envelope_metrics(abs(x[b])**2,envelope,fs,w,step_db)
                               for w in (.05,.1,.15,.2,.4,.8)],
            summed_carrier_windows=[envelope_metrics(np.sum(abs(carriers[b])**2,axis=0),envelope,fs,w,step_db) for w in (.1,.8)])
            for b in (0,1)])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('probe',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('use a new output file')
    result=measure(a.probe)
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')


if __name__=='__main__':main()
