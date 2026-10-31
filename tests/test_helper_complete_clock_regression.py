#!/usr/bin/env python3
"""Deterministic regressions for lost clock information, not sensitivity limits."""
import math
import subprocess
import sys

binary = sys.argv[1]
cases = [(1810100013, 'clean', '2'), (1810100023, 'clean', '2'),
         (1810000007, 'aligned', '2'), (1810000001, 'clean', '0.4')]
for seed, mode, delay in cases:
    command = [binary, 'full', '180', str(seed), mode, 'tone', delay, 'hf', 'tracked', 'full']
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    if result.returncode:
        raise RuntimeError(f'{command}: {result.stderr[-2000:]}')
    rows = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts:
            rows.append((parts[0], dict(item.split('=', 1) for item in parts[1:] if '=' in item)))
    summary = [v for tag, v in rows if tag == 'helper_complete_study']
    interior = [v for tag, v in rows if tag == 'helper_complete_tone' and
                v.get('scope') == 'heldout' and v.get('region') == 'interior' and v.get('selection') == 'all']
    if len(summary) != 1 or len(interior) != 1:
        raise AssertionError('missing complete-output regression measurements')
    s, p = summary[0], interior[0]
    rms = float(p['clean_phase_rms_deg'])
    if not math.isfinite(rms) or rms >= 10 or int(p['phase_samples']) < 33000:
        raise AssertionError(f'{seed}/{mode}/{delay}: changing clock error regressed: {rms} deg')
    for field in ['timestamp_failures', 'timestamp_unavailable', 'reference_mismatches', 'missing_samples']:
        if int(s[field]):
            raise AssertionError(f'{seed}: {field}={s[field]}')
    if int(s['processed_captured_samples']) != 960000 or int(s['emitted_before_eof']) == 0:
        raise AssertionError('complete stream not delivered incrementally')
    if not 959000 <= int(s['output_samples']) <= 961000:
        raise AssertionError('unexpected missing output or resampling count')
    if delay == '0.4':
        if int(s['plan_origin_joint_translated']) == 0:
            raise AssertionError('original-reference clock transport was not exercised')
    elif int(s['plan_origin_joint_point']) == 0:
        raise AssertionError('unqualified joint point correction was not exercised')
    # These cases previously failed at 34--107 degrees despite valid accumulated
    # clock information. This generous threshold preserves the causal regression
    # without turning four chosen captures into a statistical coverage claim.
    print(f'helper complete clock regression seed={seed} mode={mode} delay={delay} all_interior_rms_deg={rms:.6f}')
