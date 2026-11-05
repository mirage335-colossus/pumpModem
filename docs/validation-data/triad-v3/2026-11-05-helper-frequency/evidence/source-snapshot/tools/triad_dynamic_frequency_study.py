#!/usr/bin/env python3
"""Run a frozen paired full-bank versus Helper-frequency stochastic study.

This occasional qualification tool consumes already generated native captures.
The complete adaptive estimator, fallback and receiver is calibrated as one
procedure. Evaluator truth is not read until every receiver score and gate is
frozen. It never exports waveforms or changes the ordinary Robust receiver.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
import torch

from triad_dynamic_campaign import (decide, digest, evaluator_metrics, freeze_gate,
    load_capture, outcomes, public_geometry, read_templates, source_hashes,
    write_json)
from triad_dynamic_acquire import HelperFrequencyAcquisition, TiledAcquisition


def implementation_hashes():
    root = Path(__file__).resolve().parents[1]
    names = ('tools/triad_dynamic_frequency_study.py',
             'tools/triad_dynamic_frequency.py', 'tools/triad_dynamic_acquire.py')
    return source_hashes() | {name: digest(root / name) for name in names}


def frequency_bank(manifest, step_hz):
    if not math.isfinite(step_hz) or step_hz <= 0:
        raise ValueError('frequency step must be finite and positive')
    channel = manifest['channel']
    half = (float(channel['deterministic_cfo_bound_hz']) *
            float(channel.get('spread_margin', 1.)))
    if not math.isfinite(half) or half < 0:
        raise ValueError('invalid public CFO prior')
    cells = int(math.ceil(half / step_hz))
    if cells > 100_000:
        raise ValueError('original CFO bank exceeds 200,001-cell safety cap')
    return tuple(float(x) for x in np.linspace(-half, half, max(1, 2*cells+1))), half


def interval_contains(intervals, value):
    """Whether a diagnostic interval union contains an evaluator-only value."""
    return any(float(low) <= value <= float(high) for low, high in intervals)


def load_schedule(path):
    schedule = json.loads(Path(path).read_text())
    if not isinstance(schedule, list) or not schedule:
        raise ValueError('schedule must be a nonempty JSON array')
    ids = set()
    for trial in schedule:
        if not isinstance(trial, dict) or set(trial) != {'id', 'directory', 'phase'}:
            raise ValueError('each schedule entry needs exactly id, directory, phase')
        if not isinstance(trial['id'], str) or not trial['id'] or trial['id'] in ids:
            raise ValueError('trial ids must be nonempty and unique')
        if trial['phase'] not in ('calibration', 'signal', 'null'):
            raise ValueError('phase must be calibration, signal or null')
        ids.add(trial['id'])
    return schedule


def run(args=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schedule', type=Path, required=True)
    parser.add_argument('--templates', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frequency-step', type=float, default=.5)
    parser.add_argument('--clock-ppm', type=float, nargs='+', default=[0.])
    parser.add_argument('--arrivals', type=float, nargs='+', default=[0.])
    parser.add_argument('--epochs', type=int, nargs='+', default=[0])
    parser.add_argument('--arrival-model', choices=('continuous', 'discrete'), default='continuous')
    parser.add_argument('--device', default='cpu')
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--batch-size', type=int, default=8)
    parser.add_argument('--block-seconds', type=float, default=.25)
    parser.add_argument('--projection', choices=('core', 'full-envelope'), default='core')
    parser.add_argument('--pilot-use', choices=('none', 'low', 'high', 'both'), default='both')
    parser.add_argument('--memory-limit-bytes', type=int, default=2*1024**3)
    parser.add_argument('--alpha', type=float, default=.05)
    parser.add_argument('--include-baseline', action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument('--frequency-config', type=Path,
                        help='JSON object of explicitly frozen estimator policy controls')
    ns = parser.parse_args(args)
    if ns.threads < 1 or ns.batch_size < 1 or ns.memory_limit_bytes < 1:
        parser.error('threads, batch size and memory limit must be positive')
    if not 0 < ns.alpha < 1:
        parser.error('alpha must be between zero and one')
    if ns.output.exists():
        parser.error('output directory must not exist')
    schedule = load_schedule(ns.schedule)
    calibration_count = sum(x['phase'] == 'calibration' for x in schedule)
    if calibration_count and ns.alpha < 1 / (calibration_count + 1):
        parser.error('calibration count cannot resolve requested rank-test alpha')
    torch.set_num_threads(ns.threads)
    if torch.cuda.is_available():
        torch.backends.cuda.matmul.allow_tf32 = False
    t0 = time.perf_counter()
    frequency_config = (json.loads(ns.frequency_config.read_text())
                        if ns.frequency_config else {})
    if not isinstance(frequency_config, dict):
        parser.error('frequency config must be a JSON object')
    hashes = implementation_hashes()
    loaded = []
    common_geometry = None
    common_valid = None
    for trial in schedule:
        mark = time.perf_counter()
        manifest, raw, valid, file_hashes = load_capture(trial['directory'], common_geometry)
        if common_geometry is None:
            common_geometry = public_geometry(manifest)
            common_valid = np.asarray(valid).copy()
        elif not np.array_equal(valid, common_valid):
            raise ValueError('capture validity differs; cannot share fixed tile workspaces')
        required = raw.nbytes + valid.nbytes
        if required > ns.memory_limit_bytes:
            raise MemoryError(f'{trial["id"]} observation and mask exceed memory limit')
        loaded.append(dict(trial=trial, directory=Path(trial['directory']).resolve(),
            manifest=manifest, raw=raw, valid=valid, hashes=file_hashes,
            load_seconds=time.perf_counter()-mark))
    base = loaded[0]
    batch_bytes = sum(row['raw'].nbytes for row in loaded) + common_valid.nbytes
    if batch_bytes > ns.memory_limit_bytes:
        raise MemoryError(f'capture batch requires {batch_bytes} bytes above declared memory limit')
    template, template_epochs, template_hashes = read_templates(ns.templates, base['manifest'])
    frequencies, prior_half_width = frequency_bank(base['manifest'], ns.frequency_step)
    epochs = tuple(ns.epochs)
    if any(epoch not in template_epochs for epoch in epochs):
        raise ValueError('requested epoch absent from public templates')
    kwargs = dict(arrival_seconds=tuple(ns.arrivals), clock_ppm=tuple(ns.clock_ppm),
        selected_epochs=epochs, block_seconds=ns.block_seconds, pilot_use=ns.pilot_use,
        arrival_model=ns.arrival_model, projection=ns.projection, device=ns.device,
        precision='fp32', memory_limit_bytes=ns.memory_limit_bytes)
    adaptive = HelperFrequencyAcquisition(base['manifest'], template, template_epochs,
        frequencies_hz=frequencies, frequency_config=frequency_config, **kwargs)
    reference = TiledAcquisition(base['manifest'], template, template_epochs,
        frequencies_hz=frequencies, **kwargs) if ns.include_baseline else None
    protocol = dict(schema='triad-dynamic-helper-frequency-study-v1',
        schedule_sha256=digest(ns.schedule),
        schedule=[dict(id=x['trial']['id'], phase=x['trial']['phase'],
                       directory=str(x['directory']), hashes=x['hashes']) for x in loaded],
        source_hashes=hashes, template=template_hashes, template_epochs=template_epochs,
        public_geometry=common_geometry, public_search=dict(frequency_hz=frequencies,
            oscillator_channel_prior_half_width_hz=prior_half_width, frequency_step_requested_hz=ns.frequency_step,
            arrivals=ns.arrivals, clocks_ppm=ns.clock_ppm, epochs=epochs,
            arrival_model=ns.arrival_model, pilot_use=ns.pilot_use, projection=ns.projection),
    procedure=dict(adaptive_id=adaptive.procedure_id,
            baseline_id=reference.procedure_id if reference else None,
            adaptive_policy=adaptive.policy, alpha=ns.alpha,
            admission_status='empirically gated' if calibration_count else 'diagnostic only; no admission decisions'),
        environment=dict(python=platform.python_version(), numpy=np.__version__, torch=torch.__version__,
            device=ns.device, cpu_count=__import__('os').cpu_count(), threads=ns.threads),
        scope='Finite sampled captures, declared timing/epoch/clock banks and the bound adaptive frequency policy. '
              'Admission uses an exchangeable empirical calibration rank gate; this is not an operational false-alarm bound.')
    ns.output.mkdir(parents=True)
    write_json(ns.output/'protocol.json', protocol)
    protocol_sha = digest(ns.output/'protocol.json')

    raw_batch = np.stack([np.asarray(row['raw']) for row in loaded])
    # Release memmap references before tiled preparation allocates its reusable
    # workspace; the contiguous batch is now the sole raw observation copy.
    for row in loaded:
        row['raw'] = None
        row['valid'] = None

    def score_arm(receiver):
        t = time.perf_counter()
        result = receiver.score(raw_batch, common_valid, trial_batch_size=ns.batch_size)
        return result, time.perf_counter()-t

    # Both arrays are scored before the evaluator-only files are opened.
    adaptive_result, adaptive_score_wall = score_arm(adaptive)
    baseline_result = None
    baseline_score_wall = None
    if reference is not None:
        baseline_result, baseline_score_wall = score_arm(reference)
    adaptive_id = adaptive_result['procedure_id']
    baseline_id = baseline_result['procedure_id'] if baseline_result else None

    def arm_rows(result, arm_name, procedure_id):
        # This acquisition adapter reports one already mixed per-trial bit score;
        # presence is log-mean-exp across the two bit likelihoods.
        log_bits = np.asarray(result['bit_scores'], dtype=np.float64)
        rows = []
        for index, item in enumerate(loaded):
            bits = log_bits[index]
            rows.append(dict(trial=dict(id=item['trial']['id'], phase=item['trial']['phase']),
                procedure_id=procedure_id, condition_id='helper-frequency-stochastic',
                **{arm_name: dict(bit=int(np.argmax(bits)), bit_log_likelihoods=bits.tolist(),
                    working_presence=float(np.logaddexp(bits[0], bits[1])-math.log(2)))}))
        return rows

    adaptive_rows = arm_rows(adaptive_result, 'dynamic', adaptive_id)
    calibration_indices = [i for i, row in enumerate(schedule) if row['phase'] == 'calibration']
    # freeze_gate is shared with the static/dynamic study, so place the same
    # adaptive procedure value under both labels. Only the dynamic arm is used.
    for row in adaptive_rows:
        row['static'] = row['dynamic']
    adaptive_gate = (freeze_gate([adaptive_rows[i] for i in calibration_indices],
        adaptive_id, 'helper-frequency-stochastic', ns.alpha) if calibration_indices else None)
    baseline_rows, baseline_gate = None, None
    if baseline_result is not None:
        baseline_rows = arm_rows(baseline_result, 'dynamic', baseline_id)
        for row in baseline_rows:
            row['static'] = row['dynamic']
        baseline_gate = (freeze_gate([baseline_rows[i] for i in calibration_indices],
            baseline_id, 'helper-frequency-stochastic', ns.alpha) if calibration_indices else None)
    # Freeze all gates, then make held-out decisions, still without evaluator truth.
    for index, trial in enumerate(schedule):
        if trial['phase'] == 'calibration' or adaptive_gate is None:
            continue
        adaptive_rows[index]['decisions'] = {'dynamic': decide(adaptive_rows[index], adaptive_gate, 'dynamic')}
        if baseline_rows is not None:
            baseline_rows[index]['decisions'] = {'dynamic': decide(baseline_rows[index], baseline_gate, 'dynamic')}

    evaluator = []
    for item in loaded:
        metrics = evaluator_metrics(item['directory'], item['manifest'])
        truth_path = item['directory'] / item['manifest']['evaluator_truth_file']
        truth = json.loads(truth_path.read_text())
        tx, rx, channel = truth['tx'], truth['rx'], item['manifest']['channel']
        scale = 1 + rx['audio_ppm']*1e-6
        truth_cfo = ((tx['rf_ppm']-rx['rf_ppm'])*channel['rf_hz'] +
            (tx['audio_ppm']-rx['audio_ppm'])*channel['nominal_conversion_if_hz'])*1e-6/scale
        metrics.update(actual_cfo_hz_after_score=truth_cfo,
                       source_bit=truth['source_bit'],
                       truth_sha256=digest(truth_path),
                       evaluator_scope='Read only after receiver scoring and gates were fixed.')
        evaluator.append(metrics)
    for index, item in enumerate(loaded):
        adaptive_rows[index]['evaluator'] = evaluator[index]
        adaptive_rows[index]['frequency_assistance'] = _jsonable(
            adaptive_result['frequency_assistance'][index])
        narrowed = adaptive_rows[index]['frequency_assistance']['status'] == 'narrowed'
        adaptive_rows[index]['frequency_assistance']['evaluator_truth_covered'] = (
            interval_contains(adaptive_rows[index]['frequency_assistance'].get('expanded_intervals_hz', []),
                              evaluator[index]['actual_cfo_hz_after_score']) if narrowed else None)
        adaptive_rows[index]['phase_assistance'] = _jsonable(adaptive_result['phase_assistance'])
        adaptive_rows[index]['work'] = dict(
            candidates=int(adaptive_result['work']['candidates_per_trial'][index]),
            frequencies=int(adaptive_result['work']['frequencies_per_trial'][index]),
            selected_frequency_hz=adaptive_result['work']['selected_frequency_banks'][index])
        if baseline_rows is not None:
            baseline_rows[index]['evaluator'] = evaluator[index]

    summary = dict(schema='triad-dynamic-helper-frequency-results-v1',
        protocol_sha256=protocol_sha, procedure_ids=dict(adaptive=adaptive_id, baseline=baseline_id),
        timing=dict(capture_loading_and_hashing_seconds=sum(x['load_seconds'] for x in loaded),
            adaptive_planning_seconds=adaptive.planning_seconds,
            baseline_planning_seconds=reference.planning_seconds if reference else None,
            adaptive_frequency_estimation_seconds=adaptive_result['timing'].get('frequency_estimation_seconds'),
            adaptive_tile_preparation_seconds=adaptive_result['timing'].get('preparation_seconds'),
            adaptive_candidate_scoring_seconds=adaptive_result['timing'].get('score_wall_seconds'),
            baseline_tile_preparation_seconds=baseline_result['timing'].get('preparation_seconds')
                if baseline_result else None,
            baseline_candidate_scoring_seconds=baseline_result['timing'].get('score_wall_seconds')
                if baseline_result else None,
            adaptive_scoring_seconds=adaptive_score_wall, baseline_scoring_seconds=baseline_score_wall,
            complete_wall_seconds=time.perf_counter()-t0,
            scope='Includes observation loading/hashing and complete receiver score calls; native capture creation was external.'),
        gates=dict(adaptive=adaptive_gate, baseline=baseline_gate),
        outcomes=dict(adaptive=outcomes(adaptive_rows, 'dynamic') if adaptive_gate else None,
            baseline=outcomes(baseline_rows, 'dynamic') if baseline_rows is not None and baseline_gate else None),
        paired_comparison=dict(scope='Same captures; independent gates; descriptive outcomes only.',
            adaptive_vs_baseline=paired_counts(adaptive_rows, baseline_rows) if baseline_rows is not None and adaptive_gate and baseline_gate else None),
        admission_status='empirical calibration gate applied' if adaptive_gate else
            'diagnostic only; no reception admission claimed',
        no_helper_narrowing=sum(row['frequency_assistance']['status'] != 'narrowed' for row in adaptive_rows),
        candidate_work=dict(original_per_trial=adaptive_result['work']['original_candidates'],
            selected_per_trial=adaptive_result['work']['candidates_per_trial'],
            total_candidate_trial_evaluations=adaptive_result['work']['total_candidate_trial_evaluations']))
    write_json(ns.output/'rows.json', dict(adaptive=adaptive_rows, baseline=baseline_rows))
    write_json(ns.output/'summary.json', summary)
    if implementation_hashes() != hashes:
        raise RuntimeError('receiver sources changed during the run; evidence is unbound')
    print(json.dumps(summary, indent=2, allow_nan=False))
    return summary


def paired_counts(adaptive, baseline):
    cells = {}
    for a, b in zip(adaptive, baseline):
        if a['trial']['phase'] == 'calibration':
            continue
        def cat(row):
            decision = row['decisions']['dynamic']
            if row['trial']['phase'] == 'null':
                return 'false' if decision['accepted'] else 'reject-null'
            if not decision['accepted']:
                return 'miss'
            return 'correct' if decision['bit'] == row['evaluator']['source_bit'] else 'wrong'
        key = cat(a)+' / '+cat(b)
        cells[key] = cells.get(key, 0)+1
    return cells


def _jsonable(value):
    if hasattr(value, 'detach'):
        return value.detach().cpu().tolist()
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [_jsonable(v) for v in value]
    return value


if __name__ == '__main__':
    run()
