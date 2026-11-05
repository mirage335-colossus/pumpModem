#!/usr/bin/env python3
"""Frozen multi-cohort Helper study; generate once, share preparation across lag policies.

Research qualification only. Rank gates are per-capture experiments and do not
establish operational daily admission rates. Large sampled arrays are removed
after hashing/scoring; public manifests, truth, receipts and JSONL are retained.
"""
from __future__ import annotations
import argparse
from collections import Counter
import gc
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import numpy as np
import torch
from triad_helper_campaign import (ARMS, compact_result, decide, evaluator_metrics,
    freeze_gate, outcome_counts, schedule, source_hashes, write_json)
from triad_helper_statistics import calibration_condition_id
from triad_helper_study import HelperComparison, digest, public_clock_grids

ERASURES = ('none', 'every-dwell-10ms', 'beginning-dwell', 'end-dwell', 'boundary-two', 'random-three')
MANAGED = {'--output', '--seed', '--bit', '--data', '--helper', '--seconds', '--templates', '--plan-only'}


def protocol_cases(value):
    protocol = {'cohorts': value} if isinstance(value, list) else value
    if not isinstance(protocol, dict) or not isinstance(protocol.get('cohorts'), list) or not protocol['cohorts']:
        raise ValueError('protocol needs a nonempty cohorts list')
    alpha = protocol.get('alpha', .05)
    if not isinstance(alpha, (int, float)) or not math.isfinite(alpha) or not 0 < alpha < 1:
        raise ValueError('protocol alpha must be finite in (0,1)')
    names = set()
    for case in protocol['cohorts']:
        name = case.get('id', '')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,79}', name) or name in names:
            raise ValueError('cohort IDs must be unique safe directory names')
        args = case.get('export_args')
        if not isinstance(args, list) or not all(isinstance(x, str) for x in args) or MANAGED.intersection(args):
            raise ValueError(f'{name}: export_args contains managed flags or non-string arguments')
        if any(type(case[k]) is not int for k in ('seed_base', 'calibration_count', 'signal_count', 'null_count')):
            raise ValueError(f'{name}: integer seed and trial counts required')
        schedule(case['seed_base'], case['calibration_count'], case['signal_count'], case['null_count'])
        if not math.isfinite(case['seconds']) or case['seconds'] <= 0:
            raise ValueError(f'{name}: positive finite seconds required')
        steps = case.get('lag_steps', [1, 4])
        if not steps or len(set(steps)) != len(steps) or any(type(x) is not int or x < 1 for x in steps):
            raise ValueError(f'{name}: unique positive integer lag_steps required')
        arms = tuple(case.setdefault('arms', list(ARMS)))
        if arms not in (ARMS, ('assisted',)):
            raise ValueError(f'{name}: arms must be paired {ARMS} or assisted-only (assisted,)')
        for key in ('frequency_batch_size', 'max_cached_hypotheses', 'memory_limit_bytes'):
            if key in case and (type(case[key]) is not int or case[key] < 1):
                raise ValueError(f'{name}: positive integer {key} required')
        recipe = case.get('erasure', 'none')
        if recipe not in ERASURES:
            raise ValueError(f'{name}: unsupported erasure recipe')
        if case.get('templates_from') is not None and case['templates_from'] not in names:
            raise ValueError(f'{name}: templates_from must name a preceding cohort')
        names.add(name)
    return protocol


def erasure_mask(manifest, recipe, seed=78501):
    """Fixed public calendar mask, independent of bits, channel and noise roots."""
    fs, calendar, fmt = manifest['sample_rate_hz'], manifest['calendar'], manifest['format']
    count = int(calendar['capture_samples'])
    valid = np.ones(count, np.uint8)
    first = calendar['capture_lead_seconds'] + calendar['data_calendar_start_seconds']
    dwell, duration = fmt['dwell_seconds'], fmt['bit_seconds']
    tiles, tile = int(fmt['tile_count']), fmt['tile_seconds']
    gap = max(0., (duration - tiles * tile) / (tiles - 1)) if tiles > 1 else 0.
    active = [(first + i * (tile + gap), first + i * (tile + gap) + tile) for i in range(tiles)]
    dwells = int(round(tile / dwell))
    intervals = []
    if recipe != 'none' and (dwells < 4 or tiles < 1):
        raise ValueError('erasure recipes require at least four nominal dwells per active tile')
    if recipe == 'every-dwell-10ms':
        intervals = [(begin + i * dwell + dwell / 2 - .005, begin + i * dwell + dwell / 2 + .005)
                     for begin, _ in active for i in range(dwells)]
    elif recipe in ('beginning-dwell', 'end-dwell'):
        intervals = ([(active[0][0] + dwell, active[0][0] + 2 * dwell)] if recipe == 'beginning-dwell'
                     else [(active[-1][1] - 2 * dwell, active[-1][1] - dwell)])
    elif recipe == 'boundary-two':
        if tiles > 1:
            intervals = [(active[0][1] - dwell, active[0][1]), (active[1][0], active[1][0] + dwell)]
        else:
            boundary = first + (dwells // 2) * dwell
            intervals = [(boundary - dwell, boundary + dwell)]
    elif recipe == 'random-three':
        eligible = [(begin + i * dwell, begin + (i + 1) * dwell) for begin, _ in active
                    for i in range(1, dwells - 1)]
        indices = np.random.default_rng(seed).choice(len(eligible), size=3, replace=False)
        intervals = [eligible[int(i)] for i in sorted(indices)]
    for lower, upper in intervals:
        begin, end = round(lower * fs), round(upper * fs)
        if not 0 <= begin < end <= count:
            raise ValueError('nominal erasure leaves the observed capture')
        valid[begin:end] = 0
    return valid, dict(recipe=recipe, seed=seed, intervals_seconds=intervals,
        missing_rows=int((valid == 0).sum()), active_intervals_seconds=active, inferred_burst_gap_seconds=gap, coordinate='Fixed nominal capture calendar; no realization-dependent arrival adjustment.',
        endpoint_policy='Beginning/end use first/last interior active dwell; boundary-two straddles any intervening burst gap; every-dwell/random recipes use active tiles only.',
        scope='Known missing receiver samples applied after complete channel propagation; unrecognized outages are a separate impairment.')


def apply_erasure(folder, manifest, mask, info):
    raw_path, valid_path = folder / manifest['observation']['file'], folder / manifest['validity']['file']
    before = dict(observation_sha256=digest(raw_path), validity_sha256=digest(valid_path))
    valid = np.memmap(valid_path, dtype='u1', mode='r+', shape=mask.shape)
    if np.any(valid != 1):
        raise ValueError('native validity already contains holes; refusing to replace its mask')
    if np.any(mask == 0):
        raw = np.memmap(raw_path, dtype='<c16', mode='r+', shape=mask.shape)
        raw[mask == 0] = 0
        raw.flush()
        del raw
        valid[:] = mask
        valid.flush()
    del valid
    receipt = dict(info, native=before, observation_sha256=digest(raw_path), validity_sha256=digest(valid_path))
    write_json(folder / 'erasure.json', receipt)
    return receipt


def synchronize(device):
    if str(device).startswith('cuda'):
        torch.cuda.synchronize(torch.device(device))


def cohort_run(case, args, exporter, frozen_sources, templates_cache, *, comparison_factory, export_runner):
    folder = args.output / case['id']; folder.mkdir()
    arms = tuple(case.get('arms', ARMS))
    trials = schedule(case['seed_base'], case['calibration_count'], case['signal_count'], case['null_count'])
    write_json(folder / 'protocol.json', dict(case=case, executed_arms=list(arms),
        execution_policy='Only listed receiver arms are scored and calibrated.',
        trial_schedule=trials, alpha=args.alpha,
        exporter_sha256=digest(exporter), source_hashes=frozen_sources,
        read_order='Truth opened only after all lag-policy scores and admissions.',
        shared_work='One native capture, Helper estimation and continuous FIR preparation per trial.'))
    lag_steps = case.get('lag_steps', [1, 4]); comparisons = {}; calibration = {s: [] for s in lag_steps}
    rows = {s: [] for s in lag_steps}; gates = {}; template_folder = templates_cache.get(case.get('templates_from'))
    mask = mask_info = None; native_seconds = shared_seconds = score_seconds = preparation_seconds = 0.
    started = time.perf_counter(); stage = 'initialization'; trial = None
    try:
        if case.get('templates_from') is not None and template_folder is None:
            raise ValueError('requested templates_from cohort produced no usable templates')
        with (folder / 'trials.jsonl').open('x') as stream:
            for trial in trials:
                stage = 'native-export'; capture = folder / trial['id']
                command = [str(exporter), *case['export_args'], '--output', str(capture), '--seconds', str(case['seconds']),
                    '--seed', str(trial['seed']), '--bit', str(trial['bit']), '--data', 'on' if trial['data'] else 'off', '--helper', 'on']
                if template_folder is None: command += ['--templates']
                mark = time.perf_counter()
                with (folder / (trial['id'] + '-export.log')).open('x') as log:
                    export_runner(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.export_timeout_seconds)
                export_seconds = time.perf_counter() - mark; native_seconds += export_seconds
                manifest = json.loads((capture / 'manifest.json').read_text())
                stage = 'known-erasure-mask'
                candidate_mask, candidate_info = erasure_mask(manifest, case.get('erasure', 'none'), case.get('mask_seed', 78501))
                if mask is None: mask, mask_info = candidate_mask, candidate_info
                elif not np.array_equal(mask, candidate_mask): raise ValueError('fixed cohort validity mask changed')
                receipt = apply_erasure(capture, manifest, mask, mask_info)
                if template_folder is None:
                    template_folder = capture; templates_cache[case['id']] = capture
                else: templates_cache[case['id']] = template_folder
                if not comparisons:
                    stage = 'receiver-preparation'
                    clocks, scales = public_clock_grids(manifest, case.get('clock_step_ppm', 0.), case.get('doppler_scale_count', 1))
                    for step in lag_steps:
                        comparisons[step] = comparison_factory(capture, template_folder, device=args.device, precision='fp32',
                            fft_backend='batched', frontend_profile=case.get('frontend_profile', 'legacy'),
                            clock_ppm_values=clocks, doppler_scale_values=scales, lag_step=step,
                            aligned_fft_grid=case.get('aligned_fft_grid', True), frequency_batch_size=case.get('frequency_batch_size', args.frequency_batch_size),
                            max_cached_hypotheses=case.get('max_cached_hypotheses', args.max_cached_hypotheses),
                            memory_limit_bytes=case.get('memory_limit_bytes', args.memory_limit_bytes))
                    preparation_seconds = sum(c.preparation_seconds for c in comparisons.values())
                    write_json(folder / 'prepared.json', {str(s): dict(procedure_id=c.procedure_id,
                        template_sha256=c.template_hash, source_hashes=c.sources,
                        preparation_seconds=c.preparation_seconds) for s, c in comparisons.items()})
                stage = 'shared-capture-preparation'; synchronize(args.device)
                prepared = comparisons[lag_steps[0]].prepare_capture(capture)
                synchronize(args.device); shared_seconds += prepared['shared_preparation_seconds']
                variants = {}
                for step, comparison in comparisons.items():
                    stage = f'receiver-lag-{step}'; synchronize(args.device)
                    result = comparison.score_capture(
                        prepared, include_baseline='baseline' in arms)
                    synchronize(args.device)
                    score_seconds += result['timing']['score_only_seconds']
                    row = compact_result(result)
                    row.update(trial=trial, calibration_condition_id=calibration_condition_id(comparison.procedure_id, True))
                    if trial['phase'] == 'calibration': calibration[step].append(row)
                    else: row['decisions'] = {arm: decide(row, gates[step], arm) for arm in arms}
                    variants[str(step)] = row
                stage = 'post-score-evaluation'
                truth = json.loads((capture / 'evaluator-truth.json').read_text())
                for step, row in ((s, variants[str(s)]) for s in lag_steps):
                    row['evaluator'] = evaluator_metrics(truth, manifest, row)
                    rows[step].append(row)
                item = dict(trial=trial, variants=variants, export_command=command,
                    export_wall_seconds=export_seconds, native_cost=manifest.get('cost'),
                    shared_preparation_seconds=prepared['shared_preparation_seconds'], erasure=receipt,
                    manifest_sha256=digest(capture / 'manifest.json'), truth_sha256=digest(capture / 'evaluator-truth.json'),
                    input_sha256=receipt['observation_sha256'], validity_sha256=receipt['validity_sha256'], iq_retained=False)
                stream.write(json.dumps(item, allow_nan=False) + '\n'); stream.flush()
                for filename in (manifest['observation']['file'], manifest['validity']['file']): (capture / filename).unlink()
                del prepared
                if trial['phase'] == 'calibration' and len(calibration[lag_steps[0]]) == case['calibration_count']:
                    for step, comparison in comparisons.items():
                        gates[step] = freeze_gate(calibration[step], comparison.procedure_id,
                                                  args.alpha, arms=arms)
                        write_json(folder / f'gate-lag-{step}.json', gates[step])
                print(json.dumps(dict(cohort=case['id'], trial=trial['id'], score_seconds=score_seconds,
                    decisions={s: v.get('decisions') for s, v in variants.items()})), flush=True)
        summary = dict(status='completed', completed_trials=len(trials),
            protocol_sha256=digest(folder / 'protocol.json'),
            gate_sha256={str(s): digest(folder / f'gate-lag-{s}.json') for s in lag_steps},
            outcomes={str(s): {arm: outcome_counts(rows[s], arm) for arm in arms} for s in lag_steps},
            helper_status={str(s): dict(Counter(r['assistance']['status'] for r in rows[s])) for s in lag_steps},
            timing=dict(wall_seconds=time.perf_counter() - started, native_export_seconds=native_seconds,
                receiver_preparation_seconds=preparation_seconds, shared_capture_preparation_seconds=shared_seconds,
                all_lag_score_only_seconds=score_seconds,
                scope='Native generation/loading/hashing/writes included in wall; shared Helper/FIR charged once, resident arm scoring charged separately.'))
        if arms != ARMS:
            summary['executed_arms'] = list(arms)
    except Exception as error:
        summary = dict(status='failed', stage=stage, trial=trial, error_type=type(error).__name__, error=str(error),
            completed_trials={str(s): len(r) for s, r in rows.items()}, automatic_retry=False,
            wall_seconds=time.perf_counter() - started,
            uncertainty='Incomplete cohort is not a completed reception qualification; retained JSONL contains only completed scored trials.')
    if summary['status'] == 'failed' and trial is not None:
        failed_capture = folder / trial['id']
        summary['retained_array_identities'] = {}
        for path in list(failed_capture.glob('*.c128')) + list(failed_capture.glob('*.u8')):
            if path.name == 'templates.c128' and failed_capture in templates_cache.values(): continue
            summary['retained_array_identities'][path.name] = dict(bytes=path.stat().st_size, sha256=digest(path))
            path.unlink()
    write_json(folder / 'summary.json', summary)
    comparisons.clear(); gc.collect()
    return summary


def main(argv=None, *, comparison_factory=HelperComparison, export_runner=subprocess.run):
    started = time.perf_counter()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', required=True, type=Path); parser.add_argument('--protocol', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path); parser.add_argument('--device', default='cpu')
    parser.add_argument('--memory-limit-bytes', type=int, default=2 * 1024**3)
    parser.add_argument('--frequency-batch-size', type=int, default=8)
    parser.add_argument('--max-cached-hypotheses', type=int, default=1)
    parser.add_argument('--export-timeout-seconds', type=float, default=180.)
    args = parser.parse_args(argv)
    protocol = protocol_cases(json.loads(args.protocol.read_text())); args.alpha = protocol.get('alpha', .05)
    exporter = args.exporter.resolve(strict=True)
    if args.output.exists(): parser.error('output exists; refusing to overwrite frozen evidence')
    if (not math.isfinite(args.export_timeout_seconds) or
            min(args.memory_limit_bytes, args.frequency_batch_size, args.max_cached_hypotheses, args.export_timeout_seconds) <= 0):
        parser.error('positive resource limits and export timeout required')
    torch.backends.cuda.matmul.allow_tf32 = False; torch.set_float32_matmul_precision('highest')
    args.output.mkdir(parents=True); sources = source_hashes(); sources['tools/' + Path(__file__).name] = digest(__file__)
    exporter_hash = digest(exporter)
    write_json(args.output / 'protocol.json', dict(protocol, source_hashes=sources, exporter_sha256=exporter_hash,
        input_protocol_sha256=digest(args.protocol), runtime={k: str(v) if isinstance(v, Path) else v for k,v in vars(args).items()}))
    summaries = {}; templates = {}
    for case in protocol['cohorts']:
        if source_hashes() != {k:v for k,v in sources.items() if k != 'tools/' + Path(__file__).name} or digest(exporter) != exporter_hash or digest(__file__) != sources['tools/' + Path(__file__).name]:
            summaries[case['id']] = dict(status='not-run', reason='source or native binary changed after freeze')
        else:
            summaries[case['id']] = cohort_run(case, args, exporter, sources, templates,
                comparison_factory=comparison_factory, export_runner=export_runner)
        write_json(args.output / (case['id'] + '-checkpoint.json'), summaries[case['id']])
    for directory in set(templates.values()):
        manifest = json.loads((directory / 'manifest.json').read_text()); path = directory / manifest['templates']['file']
        if path.is_file(): path.unlink()
    write_json(args.output / 'summary.json', dict(schema='triad-helper-broad-summary-v1', cohorts=summaries,
        wall_seconds=time.perf_counter() - started, input_protocol_sha256=digest(args.protocol),
        limits=['Marginal per-capture gates; no daily false-admission or bit-authentication guarantee.',
                'Cohorts and lag arms share physical realizations; do not pool them as independent trials.']))
    return 0 if all(x['status'] == 'completed' for x in summaries.values()) else 2


if __name__ == '__main__':
    raise SystemExit(main())
