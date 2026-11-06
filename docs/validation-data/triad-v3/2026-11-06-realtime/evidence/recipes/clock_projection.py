#!/usr/bin/env python3
"""Cheap shaped-template clock mismatch diagnostic, not acquisition validation."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1', MKL_NUM_THREADS='1')
import hashlib
import json
import math
from pathlib import Path
import resource
import sys
import time

START_CPU, START_WALL = time.process_time(), time.perf_counter()
resource.setrlimit(resource.RLIMIT_CPU, (20, 22))
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
BASE = ROOT/'local-evidence/triad-cpu-ceiling/capture'
sys.path.insert(0, str(ROOT/'tools'))
import numpy as np
from scipy import signal
from triad_dynamic_joint import load_public, _sample_cubic
from triad_realtime_fft import FFTScreen


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def retention(exact, candidate):
    # Columns in the receiver are stored as [ray, time]. Physical channel gains
    # are arbitrary complex constants; canonical eigenvalues give the extrema
    # of ||P_candidate exact h||^2 / ||exact h||^2 over nonzero h.
    a, b = exact.T, candidate.T
    ga = a.conj().T @ a
    gb = b.conj().T @ b
    cross = b.conj().T @ a
    projected = cross.conj().T @ np.linalg.solve(gb, cross)
    values, vectors = np.linalg.eigh(ga)
    whitening = (vectors/np.sqrt(values)) @ vectors.conj().T
    retained = whitening @ projected @ whitening
    fractions = np.linalg.eigvalsh((retained+retained.conj().T)*.5)
    return dict(minimum=float(fractions[0]), maximum=float(fractions[-1]),
                equal_independent_ray_mean=float(np.trace(projected).real/np.trace(ga).real))


def main():
    frozen = json.loads((OUT/'protocol.json').read_text())
    assert all(sha(ROOT/'tools'/name) == value for name, value in frozen['sources'].items())
    m, templates, epochs, _, _ = load_public(BASE)
    selected_epochs = (0, 6)
    source = np.asarray([templates[epochs.index(epoch)] for epoch in selected_epochs])
    bound = m['channel']['sample_clock_pair_ppm']*m['channel']['spread_margin']
    clocks = (-bound, 0., bound)
    truth_clocks = (-bound, -bound/2, -75., -1., 0., 1., 75., bound/2, bound)
    screen = FFTScreen(m, source, selected_epochs, clock_ppm=clocks,
                       precision='fp64', workers=1)
    center = m['calendar']['data_sample_offset']/m['sample_rate_hz']+m['format']['bit_seconds']/2
    lo, hi = m['calendar']['arrival_prior_seconds']
    padding = math.ceil(2*bound*1e-6*center*screen.output_fs)+8
    size = screen.kernel_rows+2*padding
    time_source = screen.origin+np.arange(screen.raw_kernel_rows)/screen.fs
    rows = []
    for ei, epoch in enumerate(selected_epochs):
        for bit in (0, 1):
            coarse = {w['clock_ppm']: w['columns'] for w in screen.workspaces
                      if w['epoch'] == epoch and w['bit'] == bit}
            for truth_clock in truth_clocks:
                columns = []
                for delay, doppler in screen.rays:
                    position = ((time_source-delay)*(1+truth_clock*1e-6)-screen.offset)*screen.fs
                    values = _sample_cubic(source[ei, bit], position)
                    values *= np.exp(2j*np.pi*np.remainder(doppler*time_source, 1.))
                    columns.append(signal.oaconvolve(values, screen.taps, mode='full')[::screen.decimation])
                exact = np.zeros((len(screen.rays), size), complex)
                exact[:, padding:padding+screen.kernel_rows] = columns
                for arrival in (lo, 0., hi):
                    alternatives = []
                    for clock in clocks:
                        ideal = arrival+center/(1+truth_clock*1e-6)-center/(1+clock*1e-6)
                        # Actual FFT arrival lattice, constrained to the original
                        # public timing prior. No off-prior midpoint is permitted.
                        proposed = lo+round((ideal-lo)*screen.output_fs)/screen.output_fs
                        candidate_arrival = min(hi, max(lo, proposed))
                        offset = round((candidate_arrival-arrival)*screen.output_fs)
                        candidate = np.zeros_like(exact)
                        candidate[:, padding+offset:padding+offset+screen.kernel_rows] = coarse[clock]
                        fractions = retention(exact, candidate)
                        alternatives.append(dict(clock_ppm=clock, arrival_seconds=candidate_arrival,
                            unconstrained_midpoint_arrival_seconds=ideal,
                            midpoint_error_seconds=candidate_arrival+center/(1+clock*1e-6)-arrival-center/(1+truth_clock*1e-6),
                            retention=fractions))
                    best = max(alternatives, key=lambda row: row['retention']['minimum'])
                    rows.append(dict(epoch=epoch, bit=bit, true_clock_ppm=truth_clock,
                        true_arrival_seconds=arrival, candidates=alternatives,
                        best_minimum_retention=best['retention']['minimum'],
                        best_minimum_loss_db=-10*math.log10(max(best['retention']['minimum'], 1e-300)),
                        selected_clock_ppm=best['clock_ppm'], selected_arrival_seconds=best['arrival_seconds']))
    boundary = [r for r in rows if r['true_arrival_seconds'] != 0.]
    interior = [r for r in rows if r['true_arrival_seconds'] == 0.]
    def summary(group):
        worst = max(group, key=lambda row: row['best_minimum_loss_db'])
        return dict(cases=len(group), minimum_retention=min(r['best_minimum_retention'] for r in group),
                    maximum_loss_db=worst['best_minimum_loss_db'],
                    worst_case={k:v for k,v in worst.items() if k != 'candidates'})
    report = dict(schema='triad-realtime-clock-projection-v1',
        scope='Noiseless full-length shaped-template two-ray subspace mismatch only; not a complete acquisition, stochastic channel, false-admission or probability-of-detection test',
        source_sha256=frozen['sources'], input_sha256={name:sha(BASE/name) for name in ('manifest.json','templates.c128')},
        public_geometry=dict(coarse_clocks_ppm=clocks,true_clocks_ppm=truth_clocks,
            arrival_prior_seconds=[lo,hi], selected_epochs=selected_epochs, bits=[0,1],
            sample_rate_hz=screen.fs,output_rate_hz=screen.output_fs,
            kernel_rows=screen.kernel_rows, complete_filtered_tails=True,
            direct_delay_seconds=0.,second_ray_delay_seconds=screen.delay,second_ray_doppler_hz=screen.doppler),
        assumptions=['Exact off-grid source uses the same cubic zero-extension and actual causal full-tail FIR as the coarse receiver, with FP64 arithmetic.',
            'All three adjacent carrier patterns and their actual shaping remain in each raw encrypted template.',
            'Arrival candidate is DATA-midpoint recentered, rounded to the actual 1ms FFT lattice, then restricted to original bounds. No timing optimization beyond this public construction.',
            'Minimum canonical retained energy permits arbitrary constant complex two-ray gains; equal-independent-ray mean also reported.',
            'Common CFO is zero; random AM, random fading, oscillator phase noise, noise and wrong-pattern competition are absent. These remain in the separately run native campaign.',
            'The comparison conditions on the correct epoch and bit; it measures representation loss and cannot prove top-K candidate retention.'],
        summary=dict(all=summary(rows),boundary=summary(boundary),interior=summary(interior)),rows=rows,
        cpu_seconds=time.process_time()-START_CPU,wall_seconds=time.perf_counter()-START_WALL)
    assert all(sha(ROOT/'tools'/name) == value for name,value in frozen['sources'].items())
    (OUT/'clock_projection.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(summary=report['summary'],cpu_seconds=report['cpu_seconds'],wall_seconds=report['wall_seconds']),indent=2))


if __name__ == '__main__':
    main()
