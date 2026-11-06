#!/usr/bin/env python3
"""Oracle-initialized local affine representation of one saved source window."""
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy.optimize import minimize
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def identity(path):
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def overlap(x, y):
    ex, ey = np.vdot(x, x).real, np.vdot(y, y).real
    rho = float(abs(np.vdot(x, y))**2/(ex*ey))
    return dict(squared_overlap=rho, common_complex_gain_mismatch_loss_db=-10*math.log10(min(1., rho)),
                template_energy=float(ex), actual_warped_energy=float(ey),
                actual_over_template_energy_db=10*math.log10(ey/ex))


def run():
    wall, cpu = time.perf_counter(), time.process_time()
    base, changed = HERE/'hf-base-74003', HERE/'hf-plus-74003'
    a = json.loads((base/'manifest.json').read_text())
    b = json.loads((changed/'manifest.json').read_text())
    for key in ('seed','epoch','symbol_ordinal','format','geometry','first_sample','rows','sample_rate_hz'):
        if a[key] != b[key]:
            raise ValueError('paired identity mismatch: '+key)
    fs, n, start = a['sample_rate_hz'], a['rows'], a['first_calendar_seconds']
    trace = b['clock_follow']
    x = np.memmap(base/'composite.c128', dtype='<c16', mode='r', shape=(2,n))
    y = np.memmap(changed/'composite.c128', dtype='<c16', mode='r', shape=(2,n))
    crop = int(4*fs)
    indices = np.arange(crop, n-crop, dtype=np.int64)
    seconds = indices/fs
    mid = float(np.mean(seconds))
    centered = seconds-mid
    q = np.clip((start+seconds-trace['correction_at_calendar_seconds'])/trace['transition_duration_seconds'], 0, 1)
    exact_shift = trace['target_offset_seconds']*(q**3*(10+q*(-15+6*q)))
    slope, intercept = np.polyfit(centered, exact_shift, 1)
    initial = np.array([intercept, slope*1e6])
    # Deterministic fitting subset; report final overlap on all cropped rows.
    fit_rows = np.unique(np.linspace(0, len(indices)-1, 8192).astype(np.int64))
    bits = []
    for bit in (0,1):
        up = resample_poly(x[bit], 8, 1, window=('kaiser',8.6))
        def interpolate(physical_indices, shift):
            coordinates = (physical_indices+shift*fs)*8
            left = np.floor(coordinates).astype(np.int64)
            if np.min(left) < 0 or np.max(left)+1 >= len(up):
                raise ValueError('interpolation outside cropped source')
            fraction = coordinates-left
            return up[left]*(1-fraction)+up[left+1]*fraction
        actual = np.asarray(y[bit,indices])
        target = actual[fit_rows]
        def objective(p):
            shifted = interpolate(indices[fit_rows], p[0]+p[1]*1e-6*centered[fit_rows])
            rho = abs(np.vdot(shifted,target))**2/(np.vdot(shifted,shifted).real*np.vdot(target,target).real)
            return -float(rho)
        solutions = []
        for offset in (-.002,0.,.002):
            answer = minimize(objective, initial+np.array([offset,0.]), method='Nelder-Mead',
                options=dict(xatol=1e-8,fatol=1e-11,maxiter=250,
                             initial_simplex=np.array([initial+[offset,0.],initial+[offset+.001,0.],initial+[offset,1.]])))
            solutions.append(answer)
        answer = min(solutions, key=lambda r:r.fun)
        affine_shift = answer.x[0]+answer.x[1]*1e-6*centered
        exact_template = interpolate(indices,exact_shift)
        affine_template = interpolate(indices,affine_shift)
        old = np.asarray(x[bit,indices])
        bits.append(dict(bit=bit,
            nominal=overlap(old,actual),
            affine=overlap(affine_template,actual),
            exact_warp_interpolation=overlap(exact_template,actual),
            affine_offset_at_window_midpoint_seconds=float(answer.x[0]),
            affine_rate_ppm=float(answer.x[1]),
            exact_minus_affine_shift_rms_seconds=float(np.sqrt(np.mean((exact_shift-affine_shift)**2))),
            exact_minus_affine_shift_maximum_seconds=float(np.max(np.abs(exact_shift-affine_shift))),
            normalized_interpolation_sample_error=float(np.linalg.norm(exact_template-actual)/np.linalg.norm(actual)),
            fit_function_evaluations=sum(r.nfev for r in solutions),
            all_local_optimizations_converged=all(bool(r.success) for r in solutions)))
    report = dict(schema='triad-clock-affine-window-v1',
        scope='Oracle-assisted representational diagnostic, not acquisition. One finite800-second source window, cropped4seconds at both ends. No channel, noise, timing-search calibration, whole-bit or long-term tracking claim.',
        sample_rate_hz=fs, original_window_seconds=n/fs, measured_window_seconds=len(indices)/fs,
        physical_calendar_start_seconds=start+crop/fs, fine_rate_hz=a['fine_rate_hz'],
        crop_per_edge_seconds=4, nominal_source_format=a['format'], clock_trace=trace,
        interpolation='8x polyphase Kaiser(beta8.6) then linear fractional lookup; checked against independently native-rendered warped samples',
        fitting='Three local Nelder-Mead fits initialized by least-squares affine fit to PRIVATE exact TX trajectory;8192 deterministic fitting rows; losses evaluated on all396000 cropped rows. Constant complex gain eliminated analytically.',
        initial_trajectory_affine_offset_seconds=float(initial[0]), initial_trajectory_affine_rate_ppm=float(initial[1]),
        bits=bits, identities=[identity(p) for p in (Path(__file__),base/'manifest.json',changed/'manifest.json',base/'composite.c128',changed/'composite.c128')],
        wall_seconds=time.perf_counter()-wall, process_cpu_seconds=time.process_time()-cpu)
    output = HERE/'affine-result.json'
    with output.open('x') as stream:
        json.dump(report,stream,indent=2,allow_nan=False);stream.write('\n')
    print(json.dumps(dict(bits=bits,wall_seconds=report['wall_seconds'],process_cpu_seconds=report['process_cpu_seconds'])))


if __name__ == '__main__':
    run()
