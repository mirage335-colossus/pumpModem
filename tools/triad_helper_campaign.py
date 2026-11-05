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

from triad_helper_study import HelperComparison, digest
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


def freeze_gate(calibration, procedure_id, alpha):
    if not 0 < alpha < 1 or not calibration:
        raise ValueError('nonempty calibration and 0 < alpha < 1 required')
    if any(row['procedure_id'] != procedure_id for row in calibration):
        raise ValueError('calibration procedure identity differs')
    conditions = {row.get('calibration_condition_id') for row in calibration}
    if len(conditions) != 1 or None in conditions:
        raise ValueError('calibration condition identity differs or is missing')
    scores = {arm: [float(row[arm]['working_presence']) for row in calibration]
              for arm in ARMS}
    if not all(math.isfinite(x) for values in scores.values() for x in values):
        raise ValueError('nonfinite calibration score')
    return dict(schema='triad-helper-adaptive-rank-gate-v1', procedure_id=procedure_id,
                calibration_condition_id=next(iter(conditions)),
                alpha=alpha, scores=scores, count=len(calibration),
                minimum_p=1 / (len(calibration) + 1),
                rule='(1 + count(calibration score >= test score)) / (n + 1) <= alpha',
                scope='Separate per-arm marginal rank gate under exchangeable complete captures; '
                      'not a simultaneous two-arm, conditional fixed-gate or operational daily guarantee.')


def decide(result, gate, arm):
    if arm not in ARMS or result['procedure_id'] != gate['procedure_id']:
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
    diagnostic = result['baseline'].get('best_group_public_parameters')
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
    for arm in ARMS:
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
        'triad_helper_receiver.py', 'triad_live_receiver.py', 'triad_fft_acquire.py',
        'triad_group_acquire.py', 'triad_gaussian_window.py', 'triad_noise_likelihood.py',
        'triad_state_workspace.py', 'triad-helper-export.cpp')]
    paths += ['src/triad_modem_v3.cpp', 'src/triad_simulation_stream.cpp',
              'include/datapump/triad_modem_v3.hpp']
    return {name: digest(root / name) for name in paths}


def summary(rows, preparation, elapsed):
    heldouts = [r for r in rows if r['trial']['phase'] != 'calibration']
    return dict(schema='triad-helper-campaign-summary-v1', completed_trials=len(rows),
                outcomes={arm: outcome_counts(rows, arm) for arm in ARMS},
                helper_status=dict(Counter(r['assistance']['status'] for r in heldouts)),
                helper_reason=dict(Counter(r['assistance']['reason'] for r in heldouts)),
                signal_baseline_best_cfo_lost=sum(r['trial']['phase'] == 'signal' and
                    r['evaluator']['baseline_best_cfo_retained'] is False for r in heldouts),
                narrowed_bank_sizes=[r['assistance']['selected_count'] for r in heldouts],
                timing=dict(campaign_wall_seconds=elapsed, shared_preparation_seconds=preparation,
                    native_export_wall_seconds=sum(r['export_wall_seconds'] for r in rows),
                    comparison_score_seconds=sum(r['timing']['command_score_seconds'] for r in rows),
                    helper_seconds=sum(r['timing']['helper_seconds'] for r in rows),
                    baseline_score_seconds=sum(r['baseline']['timing']['total_score_prepared_seconds'] for r in rows),
                    extra_assisted_score_seconds=sum(r['timing']['additional_assisted_scoring_seconds'] for r in rows),
                    scope='Campaign wall includes native subprocess startup/export, one preparation, loading, '
                          'paired scoring, hashing and result writes; excludes Python import/startup before main. '
                          'Arm scoring timers are resident-workspace latency, not cold command latency.'),
                limitations=['Small-sample per-capture calibration; daily operational rates not qualified.',
                             'Baseline and assisted decisions share observations; comparisons are paired.',
                             'Epoch alternatives searched, but the generated true epoch remains the nominal epoch.',
                             'Static-CFO interval diagnostics are evaluator-only; no calibrated coverage guarantee.',
                             'Different selected/flat, Helper on/off or C/N0 conditions require their own calibration.'])


def main():
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
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    p.add_argument('--alpha', type=float, default=.05)
    p.add_argument('--control', choices=('selected', 'flat'), default='selected')
    p.add_argument('--helper', choices=('on', 'off'), default='on')
    p.add_argument('--discard-iq', action='store_true')
    args = p.parse_args()
    started = time.perf_counter()
    try:
        trials = schedule(args.seed_base, args.calibration_count, args.signal_count, args.null_count)
        cn0 = None if args.cn0 == 'physical' else float(args.cn0)
        if cn0 is not None and (not math.isfinite(cn0) or not -100 <= cn0 <= 200):
            raise ValueError('C/N0 outside native finite [-100, 200] dB-Hz range')
        if not math.isfinite(args.seconds) or args.seconds <= 0 or not 0 < args.alpha < 1:
            raise ValueError('positive finite duration and 0 < alpha < 1 required')
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
    protocol = dict(schema='triad-helper-campaign-protocol-v1',
        configuration={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
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
                       '--data-if', str(tm['channel']['nominal_conversion_if_hz'])]
            if cn0 is not None:
                command += ['--cn0', str(cn0)]
            mark = time.perf_counter()
            with (args.output / f'{trial["id"]}-export.log').open('x') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            export_wall = time.perf_counter() - mark
            if comparison is None:
                comparison = HelperComparison(folder, args.templates, device=args.device, precision=args.precision)
                write_json(args.output / 'prepared.json', dict(procedure_id=comparison.procedure_id,
                    template_sha256=comparison.template_hash, source_hashes=comparison.sources,
                    public_preparation_seconds=comparison.preparation_seconds))
            result = comparison.score(folder)
            row = compact_result(result)
            # Calibration distribution, distinct from the adaptive receiver
            # procedure: absent Helper must not silently reuse a present-Helper gate.
            row['calibration_condition_id'] = calibration_condition_id(
                comparison.procedure_id, args.helper == 'on')
            row.update(trial=trial, export_command=command, export_wall_seconds=export_wall)
            if trial['phase'] == 'calibration':
                calibration.append(row)
            else:
                row['decisions'] = {arm: decide(row, gate, arm) for arm in ARMS}
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
                gate = freeze_gate(calibration, comparison.procedure_id, args.alpha)
                write_json(args.output / 'gate.json', gate)
            print(json.dumps(dict(trial=trial['id'], helper=row['assistance']['status'],
                selected=row['assistance']['selected_count'], decisions=row.get('decisions'),
                export_seconds=export_wall, score_seconds=row['timing']['command_score_seconds'])), flush=True)
        result = summary(rows, comparison.preparation_seconds, time.perf_counter() - started)
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
