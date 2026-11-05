#!/usr/bin/env python3
"""Frozen, paired sampled Helper/DATA admission experiment (not routine CI).

Calibrate the *complete adaptive procedure* on Helper-present/DATA-absent
captures, then score independent signal and null captures without changing it.
One public template/preparation workspace is reused throughout.  The empirical
rank gate is a per-capture experiment under exchangeability, not a daily
false-admission guarantee or a proof of correct-bit authentication.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
import time

import numpy as np
from scipy.stats import beta

from triad_helper_study import HelperComparison, digest, public_clock_grids
from triad_helper_statistics import calibration_condition_id, signal_outcome_statistics

ARMS = ('baseline', 'assisted')


def schedule(seed_base, calibration_count, signal_count, null_count):
    """Disjoint fixed seeds; signal bits alternate without outcome-dependent work."""
    counts = (calibration_count, signal_count, null_count)
    if seed_base < 0 or any(n < 0 for n in counts) or calibration_count < 1:
        raise ValueError('nonnegative seeds/counts and at least one calibration required')
    if seed_base + sum(counts) > 2**64:
        raise ValueError('native seed range exceeded')
    rows = []
    for phase, count in zip(('calibration', 'signal', 'null'), counts):
        for index in range(count):
            rows.append(dict(id=f'{phase}-{index:04d}', phase=phase,
                             seed=seed_base + len(rows), bit=index % 2,
                             data=phase == 'signal'))
    return rows


def freeze_gate(calibration, procedure_id, alpha, arms=ARMS):
    if not 0 < alpha < 1 or not calibration:
        raise ValueError('nonempty calibration and 0 < alpha < 1 required')
    arms = tuple(arms)
    if not arms or len(set(arms)) != len(arms) or any(arm not in ARMS for arm in arms):
        raise ValueError('gate arms must be a nonempty unique subset of measured arms')
    if any(row['procedure_id'] != procedure_id for row in calibration):
        raise ValueError('calibration procedure identity differs')
    conditions = {row.get('calibration_condition_id') for row in calibration}
    if len(conditions) != 1 or None in conditions:
        raise ValueError('calibration condition identity differs or is missing')
    execution_policies = {row.get('execution_policy_id') for row in calibration}
    if len(execution_policies) != 1:
        raise ValueError('calibration execution policy identity differs')
    if any(arm not in row.get('executed_arms', ARMS) for row in calibration for arm in arms):
        raise ValueError('gate arm was not measured for every calibration trial')
    scores = {arm: [float(row[arm]['working_presence']) for row in calibration]
              for arm in arms}
    if not all(math.isfinite(x) for values in scores.values() for x in values):
        raise ValueError('nonfinite calibration score')
    return dict(schema='triad-helper-adaptive-rank-gate-v1', procedure_id=procedure_id,
                arms=list(arms),
                execution_policy_id=next(iter(execution_policies)),
                calibration_condition_id=next(iter(conditions)),
                alpha=alpha, scores=scores, count=len(calibration),
                minimum_p=1 / (len(calibration) + 1),
                rule='(1 + count(calibration score >= test score)) / (n + 1) <= alpha',
                scope='Separate per-arm marginal rank gate under exchangeable complete captures; '
                      'not a simultaneous two-arm, conditional fixed-gate or operational daily guarantee.')


def decide(result, gate, arm):
    measured_arms = tuple(gate.get('arms', ARMS))
    if arm not in ARMS or arm not in measured_arms:
        raise ValueError('decision arm was not calibrated')
    if arm not in result.get('executed_arms', ARMS):
        raise ValueError('decision arm was not measured')
    if (gate.get('execution_policy_id') is not None and
            result.get('execution_policy_id') != gate['execution_policy_id']):
        raise ValueError('decision execution policy identity differs')
    if result['procedure_id'] != gate['procedure_id']:
        raise ValueError('decision procedure identity differs')
    if (not result.get('calibration_condition_id') or
            result['calibration_condition_id'] != gate.get('calibration_condition_id')):
        raise ValueError('decision calibration condition identity differs')
    score = float(result[arm]['working_presence'])
    if not math.isfinite(score):
        raise ValueError('nonfinite decision score')
    calibration = gate['scores'][arm]
    p = (1 + sum(x >= score for x in calibration)) / (len(calibration) + 1)
    return dict(accepted=p <= gate['alpha'], p_value=p,
                bit=int(result[arm]['bit']), working_presence=score)


def binomial_interval(successes, trials, confidence=.95):
    """Exact two-sided Clopper-Pearson interval and one-sided upper bound."""
    if not 0 <= successes <= trials or not 0 < confidence < 1:
        raise ValueError('invalid binomial counts/confidence')
    if not trials:
        return dict(count=successes, trials=trials, fraction=None,
                    confidence=confidence, two_sided=None, one_sided_upper=None)
    tail = 1 - confidence
    low = 0. if successes == 0 else float(beta.ppf(tail / 2, successes, trials - successes + 1))
    high = 1. if successes == trials else float(beta.ppf(1 - tail / 2, successes + 1, trials - successes))
    upper = 1. if successes == trials else float(beta.ppf(confidence, successes + 1, trials - successes))
    return dict(count=successes, trials=trials, fraction=successes / trials,
                confidence=confidence, two_sided=[low, high], one_sided_upper=upper)


def outcome_counts(rows, arm):
    counts = Counter(correct_admitted=0, wrong_admitted=0, missed=0, false_admitted=0,
                     rejected_null=0, raw_bit_errors=0)
    signals = nulls = 0
    for row in rows:
        phase = row['trial']['phase']
        if phase == 'calibration':
            continue
        outcome = row['decisions'][arm]
        if phase == 'signal':
            signals += 1
            wrong = outcome['bit'] != row['evaluator']['source_bit']
            counts['raw_bit_errors'] += wrong
            counts['wrong_admitted' if wrong else 'correct_admitted'] += outcome['accepted']
            counts['missed'] += not outcome['accepted']
        elif phase == 'null':
            nulls += 1
            counts['false_admitted'] += outcome['accepted']
            counts['rejected_null'] += not outcome['accepted']
        else:
            raise ValueError('unknown trial phase')
    return dict(counts=counts, signal_trials=signals, null_trials=nulls,
                intervals={name: binomial_interval(counts[name], nulls)
                           for name in ('false_admitted', 'rejected_null')},
                signal_uncertainty=signal_outcome_statistics(rows, arm),
                uncertainty_scope='Null intervals are exact binomial conditional on the frozen gate and '
                    'representative independent draws. Fixed-bit signal strata use conservative '
                    'balanced-population bounds; intervals are not simultaneous across metrics or arms.')


def evaluator_metrics(truth, manifest, result):
    """Truth diagnostics called only after both receiver outcomes are frozen."""
    tx, rx, ch = truth['tx'], truth['rx'], manifest['channel']
    rx_scale = 1 + rx['audio_ppm'] * 1e-6
    static_cfo = ((tx['rf_ppm'] - rx['rf_ppm']) * ch['rf_hz'] +
                  (tx['audio_ppm'] - rx['audio_ppm']) * ch['nominal_conversion_if_hz']) * 1e-6 / rx_scale
    assistance = result['assistance']
    intervals = assistance.get('expanded_intervals_hz', [])
    selected = assistance['selected_frequencies']
    baseline = result.get('baseline')
    diagnostic = baseline.get('best_group_public_parameters') if baseline else None
    best_cfo = diagnostic.get('frequency_hz') if diagnostic else None
    return dict(source_bit=truth['source_bit'], arrival_seconds=truth['arrival_seconds'],
                nominal_static_cfo_hz=static_cfo,
                cfo_scope='Direct-ray static oscillator offset in nominal receiver sample time; '
                          'excludes stochastic phase evolution, delayed-ray Doppler and interpolation error.',
                static_cfo_in_expanded_interval=any(lo <= static_cfo <= hi for lo, hi in intervals),
                baseline_best_cfo_hz=best_cfo,
                baseline_best_cfo_retained=None if best_cfo is None else any(
                    math.isclose(best_cfo, x, rel_tol=0, abs_tol=1e-12) for x in selected),
                source_energy=truth['source_energy'], channel_report=truth['channel_report'])


def compact_result(result):
    """Omit repeated template/lag diagnostics, retaining decision and cost evidence."""
    keep = ('schema', 'procedure_id', 'input_sha256', 'template_sha256', 'source_hashes',
            'assistance', 'timing', 'limits')
    out = {key: result[key] for key in keep}
    arms = tuple(result.get('executed_arms', ARMS))
    out['executed_arms'] = list(arms)
    if 'execution_policy_id' in result:
        out['execution_policy_id'] = result['execution_policy_id']
    for arm in arms:
        source = result[arm]
        out[arm] = {key: source[key] for key in (
            'receiver_id', 'bit', 'working_presence', 'bit_log_likelihoods',
            'opposite_bit_margin', 'best_epoch_offset_seconds',
            'best_group_public_parameters', 'timing', 'arithmetic', 'resources')}
        out[arm]['score_reused_from_baseline'] = source.get('score_reused_from_baseline', False)
        g = source['geometry']
        out[arm]['work'] = dict(epoch_count=len(g['epoch_offsets']), lag_count=len(g['lag_samples']),
                               frequency_count=len(g['frequency_offsets_hz']),
                               samples=g['samples'], effective_group_count=g['effective_group_count'])
    return out


def write_json(path, value):
    # Exclusive creation prevents an accidental rerun from replacing a frozen gate.
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def source_hashes():
    root = Path(__file__).resolve().parents[1]
    paths = ['tools/' + name for name in (
        'triad_helper_campaign.py', 'triad_helper_statistics.py', 'triad_helper_study.py', 'triad_helper_assistance.py',
        'triad_helper_fft.py',
        'triad_helper_receiver.py', 'triad_live_receiver.py', 'triad_fft_acquire.py',
        'triad_group_acquire.py', 'triad_gaussian_window.py', 'triad_noise_likelihood.py',
        'triad_state_workspace.py', 'triad-helper-export.cpp')]
    paths += ['src/triad_modem_v3.cpp', 'src/triad_simulation_stream.cpp',
              'src/triad_shaping.cpp', 'include/datapump/triad_modem_v3.hpp',
              'include/datapump/triad_shaping.hpp']
    return {name: digest(root / name) for name in paths}


def append_export_shaping_options(command, args):
    """Freeze the selected shaping mode and any optional geometry overrides.

    The CLI defaults to auto and forwards it explicitly. Historical legacy
    campaigns must select legacy and use matching templates and manifests.
    """
    options = (
        ('shaping', '--shaping'),
        ('shaping_tail_fraction', '--shaping-tail-fraction'),
        ('rf_hz', '--rf-hz'),
        ('base_oscillator', '--base-oscillator'),
        ('shift_oscillator', '--shift-oscillator'),
        ('spread_preset', '--spread-preset'),
        ('margin', '--margin'),
        ('selective_perturbation', '--selective-perturbation'),
        ('selective_modulation_rate_hz', '--selective-modulation-rate-hz'),
        ('timing_policy', '--timing-policy'),
        ('data_power_w', '--data-power-w'),
        ('helper_power_w', '--helper-power-w'),
        ('rate', '--rate'),
        ('added_symbols', '--added-symbols'),
        ('fhss', '--fhss'),
        ('burst_wait_tiles', '--burst-wait-tiles'),
    )
    for attribute, flag in options:
        value = getattr(args, attribute, None)
        if value is not None:
            command += [flag, str(value)]
    return command


def summary(rows, preparation, elapsed, arms=ARMS):
    arms = tuple(arms)
    heldouts = [r for r in rows if r['trial']['phase'] != 'calibration']
    result = dict(schema='triad-helper-campaign-summary-v1', completed_trials=len(rows),
                outcomes={arm: outcome_counts(rows, arm) for arm in arms},
                helper_status=dict(Counter(r['assistance']['status'] for r in heldouts)),
                helper_reason=dict(Counter(r['assistance']['reason'] for r in heldouts)),
                signal_baseline_best_cfo_lost=(sum(r['trial']['phase'] == 'signal' and
                    r['evaluator']['baseline_best_cfo_retained'] is False for r in heldouts)
                    if 'baseline' in arms else None),
                narrowed_bank_sizes=[r['assistance']['selected_count'] for r in heldouts],
                timing=dict(campaign_wall_seconds=elapsed, shared_preparation_seconds=preparation,
                    native_export_wall_seconds=sum(r['export_wall_seconds'] for r in rows),
                    comparison_score_seconds=sum(r['timing']['command_score_seconds'] for r in rows),
                    helper_seconds=sum(r['timing']['helper_seconds'] for r in rows),
                    baseline_score_seconds=(sum(r['baseline']['timing']['total_score_prepared_seconds'] for r in rows)
                                            if 'baseline' in arms else None),
                    extra_assisted_score_seconds=(
                        sum(r['timing']['additional_assisted_scoring_seconds'] for r in rows)
                        if 'baseline' in arms else None),
                    scope='Campaign wall includes native subprocess startup/export, one preparation, loading, '
                          'paired scoring, hashing and result writes; excludes Python import/startup before main. '
                          'Arm scoring timers are resident-workspace latency, not cold command latency.'),
                limitations=['Small-sample per-capture calibration; daily operational rates not qualified.',
                             'Baseline and assisted decisions share observations; comparisons are paired.',
                             'Epoch alternatives searched, but the generated true epoch remains the nominal epoch.',
                             'Static-CFO interval diagnostics are evaluator-only; no calibrated coverage guarantee.',
                             'Different selected/flat, Helper on/off or C/N0 conditions require their own calibration.'])
    if arms != ARMS:
        result['executed_arms'] = list(arms)
        result['timing']['assisted_score_seconds'] = sum(
            r['timing']['assisted_score_seconds'] for r in rows)
        result['limitations'] = [x for x in result['limitations'] if 'decisions share observations' not in x]
        result['limitations'].append('Only declared arms were scored and calibrated; unmeasured arms have no result.')
    return result


def main(argv=None, *, comparison_factory=None, export_runner=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', required=True, choices=('hf', 'eme'))
    p.add_argument('--cn0', default='physical', help='physical or explicit DATA C/N0 in dB-Hz')
    p.add_argument('--templates', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--exporter', required=True, type=Path)
    p.add_argument('--calibration-count', type=int, default=19)
    p.add_argument('--signal-count', type=int, default=8)
    p.add_argument('--null-count', type=int, default=8)
    p.add_argument('--seed-base', required=True, type=int)
    p.add_argument('--seconds', type=float, default=16.)
    p.add_argument('--data-if', type=float,
                   help='override the exporter DATA IF; the emitted manifest remains procedure identity input')
    p.add_argument('--ramp-seconds', type=float,
                   help='optional native FHSS EME edge ramp; recorded in the emitted public manifest')
    p.add_argument('--shaping', choices=('legacy', 'auto'), default='auto',
                   help='waveform shaping mode (default: auto); legacy reproduces historical fixtures')
    p.add_argument('--shaping-tail-fraction', type=float,
                   help='optional auto-shaping spectral tail budget in (0, 0.005]')
    p.add_argument('--rate', type=float, help='optional inner pattern rate override in Hz')
    p.add_argument('--added-symbols', type=int, help='optional Added Symbols factor [1,1000]')
    p.add_argument('--fhss', choices=('on', 'off'), help='optional FHSS override')
    p.add_argument('--burst-wait-tiles', type=int,
                   help='optional Symbol Burst wait count [0,1000]')
    p.add_argument('--rf-hz', type=float, help='optional nominal RF shift in Hz; zero disables RF oscillator offset')
    p.add_argument('--base-oscillator', help='optional canonical baseband oscillator preset')
    p.add_argument('--shift-oscillator', help='optional canonical RF shift oscillator preset')
    p.add_argument('--spread-preset', help='optional canonical Doppler/delay spread preset')
    p.add_argument('--margin', type=float, help='optional spread margin multiplier')
    p.add_argument('--selective-perturbation',
                   choices=tuple(['off'] + [f'{kind}-{depth}' for kind in ('slow', 'fast', 'each')
                                            for depth in (1, 5, 10, 25, 65)]),
                   help='optional frequency-selective modulation preset')
    p.add_argument('--selective-modulation-rate-hz', type=float,
                   help='optional modulation rate override, independent of ray Doppler')
    p.add_argument('--timing-policy', choices=('case-default', 'bounded', 'broad'),
                   help='public arrival prior: preset default, 10 ms/1 ms/70 ms bounded, or several-second search')
    p.add_argument('--data-power-w', type=float, help='optional DATA transmit power in watts')
    p.add_argument('--helper-power-w', type=float,
                   help='optional requested Helper transmit power in watts (effective power is recorded)')
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    p.add_argument('--fft-backend', choices=('legacy', 'batched'), default='legacy')
    p.add_argument('--frequency-batch-size', type=int, default=8)
    p.add_argument('--max-cached-hypotheses', type=int, default=1)
    p.add_argument('--memory-limit-bytes', type=int, default=2 * 1024**3)
    p.add_argument('--frontend-profile', choices=('legacy', 'wide'), default='legacy')
    p.add_argument('--clock-step-ppm', type=float, default=0.,
                   help='public audio-clock search step in ppm; zero fixes the clock at nominal')
    p.add_argument('--doppler-scale-count', type=int, choices=(1, 3, 5, 7), default=1)
    p.add_argument('--aligned-fft-grid', action='store_true')
    p.add_argument('--lag-step', type=int, default=1,
                   help='arrival-lattice spacing in receiver samples (default: one sample)')
    p.add_argument('--alpha', type=float, default=.05)
    p.add_argument('--control', choices=('selected', 'flat'), default='selected')
    p.add_argument('--helper', choices=('on', 'off'), default='on')
    p.add_argument('--assisted-only', action='store_true',
                   help='score/calibrate only the Helper-selected assisted arm; default scores both arms')
    p.add_argument('--discard-iq', action='store_true')
    args = p.parse_args(argv)
    comparison_factory = comparison_factory or HelperComparison
    export_runner = export_runner or subprocess.run
    started = time.perf_counter()
    try:
        trials = schedule(args.seed_base, args.calibration_count, args.signal_count, args.null_count)
        cn0 = None if args.cn0 == 'physical' else float(args.cn0)
        if cn0 is not None and (not math.isfinite(cn0) or not -100 <= cn0 <= 200):
            raise ValueError('C/N0 outside native finite [-100, 200] dB-Hz range')
        if not math.isfinite(args.seconds) or args.seconds <= 0 or not 0 < args.alpha < 1:
            raise ValueError('positive finite duration and 0 < alpha < 1 required')
        if args.data_if is not None and not math.isfinite(args.data_if):
            raise ValueError('--data-if must be finite')
        if args.ramp_seconds is not None and (not math.isfinite(args.ramp_seconds) or args.ramp_seconds <= 0):
            raise ValueError('--ramp-seconds must be positive and finite')
        if args.shaping_tail_fraction is not None and (
                not math.isfinite(args.shaping_tail_fraction) or
                not 0 < args.shaping_tail_fraction <= .005):
            raise ValueError('--shaping-tail-fraction must be in (0, 0.005]')
        if args.shaping_tail_fraction is not None and args.shaping != 'auto':
            raise ValueError('--shaping-tail-fraction requires --shaping auto')
        if args.rate is not None and (not math.isfinite(args.rate) or not 0 < args.rate <= 30e6):
            raise ValueError('--rate must be in (0, 30000000] Hz')
        if args.added_symbols is not None and not 1 <= args.added_symbols <= 1000:
            raise ValueError('--added-symbols must be in [1,1000]')
        if args.burst_wait_tiles is not None and not 0 <= args.burst_wait_tiles <= 1000:
            raise ValueError('--burst-wait-tiles must be in [0,1000]')
        if args.rf_hz is not None and (not math.isfinite(args.rf_hz) or args.rf_hz < 0):
            raise ValueError('--rf-hz must be finite and nonnegative')
        for name in ('margin', 'data_power_w', 'helper_power_w'):
            value = getattr(args, name)
            if value is not None and (not math.isfinite(value) or value <= 0):
                raise ValueError(f'--{name.replace("_", "-")} must be positive and finite')
        if (args.selective_modulation_rate_hz is not None and
                (not math.isfinite(args.selective_modulation_rate_hz) or
                 args.selective_modulation_rate_hz < 0)):
            raise ValueError('--selective-modulation-rate-hz must be finite and nonnegative')
        if args.clock_step_ppm < 0 or not math.isfinite(args.clock_step_ppm):
            raise ValueError('--clock-step-ppm must be finite and nonnegative')
        if args.lag_step < 1:
            raise ValueError('--lag-step must be a positive integer')
        if args.frequency_batch_size < 1 or args.max_cached_hypotheses < 1 or args.memory_limit_bytes < 1:
            raise ValueError('frequency batch size, cache capacity, and memory limit must be positive')
        if args.fft_backend == 'legacy' and (args.clock_step_ppm > 0 or args.doppler_scale_count != 1):
            raise ValueError('clock/rate grids require --fft-backend batched')
        if args.output.exists():
            raise ValueError('output already exists; do not rerun or overwrite frozen evidence')
        exporter = args.exporter.resolve(strict=True)
        tm = json.loads((args.templates / 'manifest.json').read_text())
        if not tm['templates']['enabled']:
            raise ValueError('complete templates required')
        template_path = args.templates / tm['templates']['file']
    except (ValueError, OSError) as error:
        p.error(str(error))
    args.output.mkdir(parents=True)
    executed_arms = ('assisted',) if args.assisted_only else ARMS
    protocol = dict(schema='triad-helper-campaign-protocol-v1',
        configuration={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        executed_arms=list(executed_arms),
        public_overrides=dict(data_if_hz=args.data_if, ramp_seconds=args.ramp_seconds,
            shaping=args.shaping, shaping_tail_fraction=args.shaping_tail_fraction,
            rf_hz=args.rf_hz, base_oscillator=args.base_oscillator,
            shift_oscillator=args.shift_oscillator, spread_preset=args.spread_preset,
            margin=args.margin, selective_perturbation=args.selective_perturbation,
            selective_modulation_rate_hz=args.selective_modulation_rate_hz,
            timing_policy=args.timing_policy, data_power_w=args.data_power_w,
            helper_power_w=args.helper_power_w,
            rate_hz=args.rate, added_symbols=args.added_symbols, fhss=args.fhss,
            burst_wait_tiles=args.burst_wait_tiles, frontend_profile=args.frontend_profile,
            clock_step_ppm=args.clock_step_ppm, doppler_scale_count=args.doppler_scale_count,
            aligned_fft_grid=args.aligned_fft_grid, lag_step=args.lag_step,
            identity='Exporter manifest bytes and HelperComparison public procedure identity; no manifest field is rewritten.'),
        source_hashes=source_hashes(), exporter_sha256=digest(exporter),
        template_sha256=digest(template_path), template_manifest_sha256=digest(args.templates / 'manifest.json'),
        python=sys.version, platform=platform.platform(), trial_schedule=trials,
        gate_policy='Separate arm rank p-values; conservative ties; fixed alpha; no adaptive stopping or retries.',
        read_order='Truth opened only after receiver scores; decisions do not consume evaluator fields.',
        calibration_resolution=1 / (args.calibration_count + 1),
        helper_null_scope='Helper follows --helper; DATA off; selected/flat channel follows --control.')
    write_json(args.output / 'protocol.json', protocol)
    rows, calibration, comparison, gate = [], [], None, None
    current_trial = None
    try:
        for trial in trials:
            current_trial = trial
            folder = args.output / trial['id']
            command = [str(exporter), '--case', args.case, '--output', str(folder),
                       '--seconds', str(args.seconds), '--seed', str(trial['seed']),
                       '--bit', str(trial['bit']), '--data', 'on' if trial['data'] else 'off',
                       '--helper', args.helper, '--control', args.control,
                       '--key-seed', str(tm['key_seed']), '--epoch', str(tm['public_epoch_seconds']),
                       '--data-if', str(args.data_if if args.data_if is not None else
                                        tm['channel']['nominal_conversion_if_hz'])]
            if args.ramp_seconds is not None:
                command += ['--ramp-seconds', str(args.ramp_seconds)]
            append_export_shaping_options(command, args)
            if cn0 is not None:
                command += ['--cn0', str(cn0)]
            mark = time.perf_counter()
            with (args.output / f'{trial["id"]}-export.log').open('x') as log:
                export_runner(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            export_wall = time.perf_counter() - mark
            if comparison is None:
                # Public manifest geometry is enough to construct the nuisance
                # grids; receiver admission still has no evaluator truth input.
                manifest = json.loads((folder / 'manifest.json').read_text())
                clock_values, scales = public_clock_grids(
                    manifest, args.clock_step_ppm, args.doppler_scale_count)
                comparison = comparison_factory(
                    folder, args.templates, device=args.device, precision=args.precision,
                    fft_backend=args.fft_backend, frequency_batch_size=args.frequency_batch_size,
                    max_cached_hypotheses=args.max_cached_hypotheses,
                    memory_limit_bytes=args.memory_limit_bytes,
                    frontend_profile=args.frontend_profile, clock_ppm_values=clock_values,
                    doppler_scale_values=scales, aligned_fft_grid=args.aligned_fft_grid,
                    lag_step=args.lag_step)
                write_json(args.output / 'prepared.json', dict(procedure_id=comparison.procedure_id,
                    template_sha256=comparison.template_hash, source_hashes=comparison.sources,
                    public_preparation_seconds=comparison.preparation_seconds))
            result = comparison.score(folder, include_baseline='baseline' in executed_arms)
            row = compact_result(result)
            # Calibration distribution, distinct from the adaptive receiver
            # procedure: absent Helper must not silently reuse a present-Helper gate.
            row['calibration_condition_id'] = calibration_condition_id(
                comparison.procedure_id, args.helper == 'on')
            row.update(trial=trial, export_command=command, export_wall_seconds=export_wall)
            if trial['phase'] == 'calibration':
                calibration.append(row)
            else:
                row['decisions'] = {arm: decide(row, gate, arm) for arm in executed_arms}
            # Evaluator access deliberately follows all receiver work and admission.
            manifest = json.loads((folder / 'manifest.json').read_text())
            truth = json.loads((folder / 'evaluator-truth.json').read_text())
            row['evaluator'] = evaluator_metrics(truth, manifest, result)
            row['manifest_sha256'] = digest(folder / 'manifest.json')
            row['truth_sha256'] = digest(folder / 'evaluator-truth.json')
            row['validity_sha256'] = digest(folder / manifest['validity']['file'])
            row['iq_retained'] = not args.discard_iq
            write_json(folder / 'result.json', row)
            with (args.output / 'trials.jsonl').open('a') as stream:
                stream.write(json.dumps(row, allow_nan=False) + '\n')
                stream.flush()
            rows.append(row)
            if args.discard_iq:
                (folder / manifest['observation']['file']).unlink()
                (folder / manifest['validity']['file']).unlink()
            if trial['phase'] == 'calibration' and len(calibration) == args.calibration_count:
                gate = freeze_gate(calibration, comparison.procedure_id, args.alpha,
                                   arms=executed_arms)
                write_json(args.output / 'gate.json', gate)
            print(json.dumps(dict(trial=trial['id'], helper=row['assistance']['status'],
                selected=row['assistance']['selected_count'], decisions=row.get('decisions'),
                export_seconds=export_wall, score_seconds=row['timing']['command_score_seconds'])), flush=True)
        result = summary(rows, comparison.preparation_seconds,
                         time.perf_counter() - started, arms=executed_arms)
        result['procedure_id'] = comparison.procedure_id
        result['protocol_sha256'] = digest(args.output / 'protocol.json')
        result['gate_sha256'] = digest(args.output / 'gate.json')
        write_json(args.output / 'summary.json', result)
    except Exception as error:
        write_json(args.output / 'failure.json', dict(trial=current_trial, completed_trials=len(rows),
            error_type=type(error).__name__, error=str(error),
            wall_seconds=time.perf_counter() - started, automatic_retry=False))
        raise


if __name__ == '__main__':
    main()
