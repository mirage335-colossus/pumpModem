#!/usr/bin/env python3
"""Bounded, adaptive Helper onset and frequency proposals for DATA acquisition.

The onset working statistic is an exponential-energy change point with levels
estimated in public noise-only and Helper-only intervals. Its competitive set
and padding are engineering policies, not confidence intervals. The complete
adaptive receiver needs independent signal and null calibration. No propagation
truth, payload bit, or cross-frequency pilot phase is used.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from triad_dynamic_frequency import estimate_dynamic_frequency, frequency_policy


_DEFAULT = dict(onset_step_seconds=.02, onset_score_drop=12.,
    onset_padding_seconds=.15, maximum_onset_width_seconds=1.25,
    level_guard_seconds=.10, minimum_noise_seconds=.25,
    level_observation_seconds=2., minimum_level_z=8.,
    minimum_level_ratio=1.03, maximum_samples=8_000_000)


def helper_policy(config=None):
    """Return the frozen proposal policy; configuration never changes the prior."""
    if config is not None and not isinstance(config, dict):
        raise ValueError('Helper config must be a dictionary')
    unknown = set(config or ()) - set(_DEFAULT) - {'frequency_config'}
    if unknown:
        raise ValueError('unknown Helper configuration: '+', '.join(sorted(unknown)))
    p = dict(_DEFAULT)
    p.update({k: v for k, v in (config or {}).items() if k != 'frequency_config'})
    for name, value in p.items():
        if (isinstance(value, (bool, np.bool_)) or
                not isinstance(value, (int, float, np.number)) or
                not math.isfinite(value) or value <= 0):
            raise ValueError(name+' must be finite and positive')
    if int(p['maximum_samples']) != p['maximum_samples']:
        raise ValueError('maximum_samples must be an integer')
    p['maximum_samples'] = int(p['maximum_samples'])
    fc = dict(modulation_width_allowance=0., require_data_free=True)
    supplied = (config or {}).get('frequency_config', {})
    if not isinstance(supplied, dict):
        raise ValueError('frequency_config must be a dictionary')
    fc.update(supplied)
    if fc.get('require_data_free') is not True:
        raise ValueError('onset-relative frequency observation must remain DATA-free')
    fp = frequency_policy(fc)
    return dict(schema='triad-realtime-helper-policy-v1', **p,
        frequency_config=fc, frequency_policy=fp,
        onset_statistic='exponential-energy change point with independent public level intervals',
        interval_kind='competitive working-score regions plus explicit padding; not calibrated confidence',
        specular_core_scope='Zero AM allowance follows persistent spectral cores; complete AM observations remain present and model mismatch needs calibration.',
        calibration='Onset, aperture selection, frequency selection and downstream screening require one complete independently calibrated procedure.',
        clock_narrowing=False, phase_correction=False)


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False,
                                    separators=(',', ':')).encode()).hexdigest()


def _source_hashes():
    directory = Path(__file__).resolve().parent
    return {name: hashlib.sha256((directory/name).read_bytes()).hexdigest()
            for name in ('triad_realtime_helper.py', 'triad_dynamic_frequency.py')}


def _number(value, name, *, minimum=None):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(name+' must be finite')
    try:
        v = float(value)
    except (ValueError, TypeError) as error:
        raise ValueError(name+' must be finite') from error
    if not math.isfinite(v) or (minimum is not None and v < minimum):
        raise ValueError(name+' is outside its finite range')
    return v


def estimate_helper(raw, manifest, original_frequencies, *, valid=None, config=None):
    """Return bounded onset/frequency proposals or the original frequency bank.

    The original DATA arrival prior is retained in the result and identity.
    Only the existing frequency estimator's aperture construction receives an
    explicitly conditioned copy. This function does not narrow DATA timing,
    declare phase authority, or admit a payload.
    """
    cpu0, wall0 = time.process_time(), time.perf_counter()
    policy = helper_policy(config)
    bank = np.asarray(original_frequencies, dtype=np.float64)
    if (bank.ndim != 1 or not bank.size or not np.isfinite(bank).all() or
            np.unique(bank).size != bank.size):
        raise ValueError('original frequency bank must be finite, nonempty and unique')
    if not isinstance(manifest, dict):
        raise ValueError('manifest must be a dictionary')
    result = dict(schema='triad-realtime-helper-v1', status='fallback', reason='',
        selected_frequencies=bank.tolist(), selected_count=len(bank),
        original_frequencies=bank.tolist(), original_count=len(bank),
        expanded_intervals_hz=[], onset_intervals_seconds=[],
        onset={}, frequency=None, phase_reference_usable=False,
        clock_status='unchanged', policy=policy,
        timing=dict(onset_cpu_seconds=0., onset_wall_seconds=0.,
                    frequency_cpu_seconds=0., frequency_wall_seconds=0.))
    # Exclude evaluator fields and DATA-present/control labels: H0 and H1 must
    # use the same identity. Observed onset/frequency subsets are results.
    public = {k: manifest.get(k) for k in
              ('sample_rate_hz', 'helper_enabled', 'format', 'channel',
               'calendar', 'helper_plan')}
    result['procedure_id'] = _digest(dict(policy=policy, geometry=public,
        original_frequencies=bank.tolist(), source_sha256=_source_hashes()))

    def finish(reason):
        result['reason'] = reason
        cpu, wall = time.process_time()-cpu0, time.perf_counter()-wall0
        if result['frequency'] is None:
            result['timing'].update(onset_cpu_seconds=cpu, onset_wall_seconds=wall)
        result['timing'].update(total_cpu_seconds=cpu, total_wall_seconds=wall)
        return result

    if not isinstance(manifest.get('helper_enabled', True), bool):
        raise ValueError('helper_enabled must be boolean')
    if not manifest.get('helper_enabled', True):
        return finish('helper_disabled')
    required = {'calendar': ('capture_samples', 'capture_lead_seconds',
        'arrival_prior_seconds', 'helper_start_seconds', 'helper_end_seconds',
        'data_sample_offset'), 'channel': ('sample_clock_pair_ppm',
        'endpoint_uniform_half_bound_ppm', 'spread_delay_ms',
        'delay_excursion_seconds', 'spread_margin'), 'helper_plan': ()}
    missing = [section+'.'+field for section, fields in required.items()
               for field in fields if not isinstance(manifest.get(section), dict)
               or field not in manifest[section]]
    if missing or 'sample_rate_hz' not in manifest or not isinstance(manifest.get('helper_plan'), dict):
        result['missing_fields'] = missing
        return finish('public_uncertainty_metadata_missing')
    cal, ch = manifest['calendar'], manifest['channel']
    fs = _number(manifest['sample_rate_hz'], 'sample rate', minimum=1.)
    y = np.asarray(raw)
    if (y.ndim != 1 or not np.iscomplexobj(y) or
            len(y) != int(cal['capture_samples'])):
        raise ValueError('complex raw capture must match public capture_samples')
    if len(y) > policy['maximum_samples']:
        return finish('onset_work_limit')
    if not np.isfinite(y).all():
        raise ValueError('nonfinite observed capture')
    mask = np.ones(len(y), dtype=bool) if valid is None else np.asarray(valid)
    if mask.shape != y.shape or not np.isin(mask, (0, 1)).all():
        raise ValueError('valid must be a binary capture mask')
    mask = mask.astype(bool, copy=False)
    prior = np.asarray(cal['arrival_prior_seconds'], dtype=np.float64)
    if prior.shape != (2,) or not np.isfinite(prior).all() or prior[0] > prior[1]:
        raise ValueError('finite ordered arrival prior required')
    result['original_arrival_prior_seconds'] = prior.tolist()
    if manifest['helper_plan'].get('pilot_dwell_gating', False) or manifest.get('format', {}).get('physical_rf_retuning', False):
        return finish('gated_or_retuned_pilots_unsupported')
    margin = _number(ch['spread_margin'], 'margin', minimum=0.)
    endpoint = _number(ch['endpoint_uniform_half_bound_ppm'], 'endpoint clock', minimum=0.)*margin*1e-6
    audio = _number(ch['sample_clock_pair_ppm'], 'sample clock', minimum=0.)*margin*1e-6
    if max(audio, endpoint) >= .1:
        return finish('clock_envelope_unsupported')
    lead = _number(cal['capture_lead_seconds'], 'capture lead', minimum=0.)
    hstart = _number(cal['helper_start_seconds'], 'Helper start')
    hend = _number(cal['helper_end_seconds'], 'Helper end')
    source_start = _number(cal['data_sample_offset'], 'DATA sample offset')/fs
    delay = (_number(ch['spread_delay_ms'], 'delay', minimum=0.)*.001+
             _number(ch['delay_excursion_seconds'], 'delay excursion', minimum=0.))
    settling = max(delay, _number(cal.get('fixed_capture_tail_bound_seconds', delay),
                                  'settling tail', minimum=0.))
    guard = policy['level_guard_seconds']
    # The start-time uncertainty due to endpoint clocks is covered explicitly.
    earliest = (prior[0]+hstart/(1+endpoint))*(1-endpoint)
    latest = (prior[1]+hstart/(1-endpoint))*(1+endpoint)
    before_end = earliest-guard
    after_start = latest+settling+guard
    after_limit = min((prior[0]+source_start/(1+endpoint))*(1-endpoint),
                      (prior[0]+hend/(1+endpoint))*(1-endpoint))-guard

    def indices(a, b):
        return max(0, math.ceil((a+lead)*fs)), min(len(y), math.floor((b+lead)*fs))

    n0a, n0b = indices(-lead, before_end)
    n1a, n1b = indices(after_start, min(after_limit,
                                      after_start+policy['level_observation_seconds']))
    if ((n0b-n0a)/fs < policy['minimum_noise_seconds'] or
            (n1b-n1a)/fs < policy['minimum_noise_seconds']):
        return finish('insufficient_public_level_support')
    if not mask[n0a:n0b].all() or not mask[n1a:n1b].all():
        return finish('known_erasure_in_level_intervals')
    power = np.square(y.real.astype(np.float64))+np.square(y.imag.astype(np.float64))
    if not np.isfinite(power).all():
        raise ValueError('observed energy exceeds finite arithmetic')
    v0, v1 = float(power[n0a:n0b].mean()), float(power[n1a:n1b].mean())
    if v0 <= 0 or v1 <= 0:
        return finish('unusable_level_estimate')
    tiny = np.finfo(np.float64).tiny
    ratio = v1/max(v0, tiny)
    z = (v1-v0)/math.sqrt(max(v0*v0/(n0b-n0a)+v1*v1/(n1b-n1a), tiny))
    result['onset'] = dict(noise_level=v0, helper_level=v1,
        helper_to_noise_level_ratio=ratio, working_level_z=z,
        noise_samples=n0b-n0a, helper_samples=n1b-n1a,
        noise_interval_seconds=[n0a/fs-lead, n0b/fs-lead],
        helper_level_interval_seconds=[n1a/fs-lead, n1b/fs-lead],
        level_z_scope='Working independent exponential-energy scale only; no statistical coverage claim.')
    if ratio < policy['minimum_level_ratio'] or z < policy['minimum_level_z']:
        return finish('weak_or_absent_helper_onset')

    step = max(1/fs, policy['onset_step_seconds'])
    onset_grid = np.linspace(earliest, latest, max(1, math.ceil((latest-earliest)/step))+1)
    rows = np.clip(np.rint((onset_grid+lead)*fs).astype(np.int64), 0, len(y))
    s0, s1 = max(0, int(rows.min())), min(len(y), int(rows.max()))
    if not mask[s0:s1].all():
        return finish('known_erasure_in_onset_search')
    # Constants independent of the change point cancel. A single prefix sum
    # evaluates every onset; no per-hypothesis sample scan is needed.
    energy = np.r_[0., np.cumsum(power[s0:s1], dtype=np.float64)]
    counts = rows-s0
    scores = counts*math.log(ratio)+energy[counts]*(1/v1-1/v0)
    scores -= scores.max()
    keep = scores >= -policy['onset_score_drop']
    starts = np.flatnonzero(keep & np.r_[True, ~keep[:-1]])
    ends = np.flatnonzero(keep & np.r_[~keep[1:], True])
    # Convert detected nominal Helper-onset dates back to arrival coordinates.
    clock_pad = (max(abs(earliest), abs(latest))+abs(hstart))*max(audio, endpoint)
    padding = policy['onset_padding_seconds']+step+clock_pad
    intervals = []
    for i, j in zip(starts, ends):
        lo = max(float(prior[0]), float(onset_grid[i]-hstart-padding))
        hi = min(float(prior[1]), float(onset_grid[j]-hstart+padding))
        if intervals and lo <= intervals[-1][1]:
            intervals[-1][1] = max(intervals[-1][1], hi)
        else:
            intervals.append([lo, hi])
    hull = [intervals[0][0], intervals[-1][1]]
    result['onset_intervals_seconds'] = intervals
    result['onset'].update(grid_seconds=onset_grid.tolist(),
        relative_log_scores=scores.tolist(), best_arrival_seconds=float(onset_grid[int(scores.argmax())]-hstart),
        padded_arrival_hull_seconds=hull, padding_seconds=padding,
        uncertainty_scope='Candidate score-drop set and declared guard; empirical coverage unqualified.')
    if hull[1]-hull[0] > policy['maximum_onset_width_seconds']:
        return finish('ambiguous_helper_onset')

    onset_cpu, onset_wall = time.process_time()-cpu0, time.perf_counter()-wall0
    result['timing'].update(onset_cpu_seconds=onset_cpu, onset_wall_seconds=onset_wall)
    conditioned = copy.deepcopy(manifest)
    conditioned['calendar']['arrival_prior_seconds'] = hull
    c0, w0 = time.process_time(), time.perf_counter()
    frequency = estimate_dynamic_frequency(y, conditioned, bank, valid=mask,
                                           config=policy['frequency_config'])
    result['timing'].update(frequency_cpu_seconds=time.process_time()-c0,
                           frequency_wall_seconds=time.perf_counter()-w0)
    result['frequency'] = frequency
    result['aperture_conditioning'] = dict(original_arrival_prior_seconds=prior.tolist(),
        acquired_arrival_hull_seconds=hull,
        scope='Conditioned frequency-aperture proposal only; DATA acquisition original prior is unchanged.',
        qualification='Full adaptive onset/frequency procedure requires independent calibration.')
    if frequency['status'] != 'narrowed':
        return finish('frequency_'+frequency['reason'])
    result.update(status='narrowed', selected_frequencies=frequency['selected_frequencies'],
        selected_count=frequency['selected_count'],
        expanded_intervals_hz=frequency['expanded_intervals_hz'])
    return finish('onset_relative_specular_frequency_proposal')
