#!/usr/bin/env python3
"""Validate and summarize the fixed long-rate selective-stage tone refinement."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(rows):
    expected={(r,d,c,ray,(.25 if d==.001 else .5)*m)
              for r,d,c,ray,m in itertools.product(
                  [.001,.01],[.001,.05],[-.00005,.00005],[0,1],[1,2,4])}
    actual={(r['rate_hz'],r['doppler_hz'],r['cfo_hz'],r['ray'],r['fs_hz']) for r in rows}
    if len(rows)!=48 or actual!=expected:
        raise ValueError('expected every predeclared rate/spread/CFO/ray/Fs profile exactly once')
    scalar_keys=['finite_vs_ideal_adjacent_difference_rms',
                 'simultaneous_vs_isolated_relative_error',
                 'simultaneous_vs_ideal_relative_error','simultaneous_energy_error_db']
    array_keys=['finite_vs_ideal_complex_rms','finite_vs_ideal_amplitude_rms',
                'finite_vs_ideal_phase_rms_rad','individual_energy_error_db']
    maxima={k:max(abs(r[k]) for r in rows) for k in scalar_keys}
    for k in array_keys:
        if any(len(r[k])!=3 for r in rows):raise ValueError('expected three measured carriers')
        maxima[k]=max(abs(x) for r in rows for x in r[k])
    maxima['sideband_amplitude_abs_error']=max(
        abs(a-b) for r in rows
        for actual,ideal in zip(r['sideband_amplitude_actual'],r['sideband_amplitude_ideal'])
        for a,b in zip(actual,ideal))
    if not all(math.isfinite(v) for v in maxima.values()):raise ValueError('nonfinite response statistic')
    if any(r['slow_cycles']!=100 or r['fast_cycles']!=1000 or r['depth']!=.65 for r in rows):
        raise ValueError('unexpected measurement duration or modulation depth')
    if any(r['discard_samples']<r['tail_samples']+1000*r['fs_hz'] for r in rows):
        raise ValueError('measurement includes filter startup')
    # This line-support check applies only to these deterministic tone inputs.
    min_nyquist_margin=min(r['fs_hz']/2-abs(r['tone_triplet_offset_hz'])
                           -r['spacing_hz']-r['doppler_hz'] for r in rows)
    if min_nyquist_margin<=0:raise ValueError('tested deterministic sideband is outside Nyquist')
    ranges={k:[min(r[k] for r in rows),max(r[k] for r in rows)] for k in
            ['adjacent_complex_response_difference_rms','adjacent_amplitude_difference_rms',
             'adjacent_phase_difference_rms_rad']}
    return dict(schema='triad-long-rate-selective-refinement-v1',profiles=len(rows),
        worst_errors=maxima,differential_response_ranges=ranges,
        minimum_tested_line_nyquist_margin_hz=min_nyquist_margin,
        acceptance=dict(energy_error_limit_db=.1,
            pass_energy_error=max(maxima['individual_energy_error_db'],
                                  maxima['simultaneous_energy_error_db'])<.1,
            complex_gain_error_limit=.01,pass_complex_gain_error=maxima['finite_vs_ideal_complex_rms']<.01),
        scope='Continuous sampled selective FIR and deterministic tone/sideband response only',
        limitations=[
            'One fixed modulation seed; 48 deterministic profiles are not 48 independent physical channel trials.',
            'The selected rates and Fs make dispersive delays integral; this conclusion does not generalize to arbitrary spacing or Fs.',
            'Both Doppler-shifted triplet offsets are tested separately, with three simultaneous tones per profile; no carrier-label separation is applied by the operator.',
            'No shaped data, noise, excess-delay ray composition, stochastic oscillator, receiver compression or admission test is performed.',
            'Finite-vs-ideal energy agreement is a stage-specific interpolation result, not a sensitivity or total emitted-bandwidth certificate.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--results',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[5])
    a=p.parse_args()
    if a.output.exists():p.error('output must be fresh')
    result=summarize([json.loads(line) for line in a.results.read_text().splitlines()])
    result['results_sha256']=sha(a.results)
    result['source_sha256']={name:sha(a.root/name) for name in [
        'src/channel_perturbation.hpp',
        'docs/validation-data/triad-v3/2026-11-03-broad/sampling/long_rate_probe.cpp',
        'docs/validation-data/triad-v3/2026-11-03-broad/sampling/summarize.py']}
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'profiles':result['profiles'],'acceptance':result['acceptance']}))


if __name__=='__main__':main()
