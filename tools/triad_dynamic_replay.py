#!/usr/bin/env python3
"""Recalibrate a frozen joint-receiver policy on retained native campaign I/Q.

No waveform is regenerated and no existing evidence is modified. The original
calibration captures produce a new policy-specific gate, followed by the same
held-out captures. Reusing previously inspected validation data is an exploratory
paired replay, not fresh prospective qualification of a newly selected policy.

  python tools/triad_dynamic_replay.py --campaign CAPTURES --output NEW_DIRECTORY \
    --pilot-use none --device cuda --benchmark-cpu-cuda

Optional CPU/CUDA timing replays measure synchronized warm score latency at
batch1 and batch8, including sample transfer/correlation/state work. Preparation
and disk loading are measured separately. Timing replays are not new receptions.
"""
from __future__ import annotations

import argparse
import gc
from functools import partial
import itertools
import json
import math
from pathlib import Path
import platform
import statistics
import sys
import time

import numpy as np

from triad_dynamic_campaign import (ARMS, _jsonable, decide, digest, evaluator_metrics,
    freeze_gate, identity, load_capture, outcomes, paired_arms, paired_outcomes,
    public_geometry, read_templates, source_hashes, write_json)


def implementation_hashes():
    directory = Path(__file__).parent
    return source_hashes() | {'tools/triad_dynamic_replay.py': digest(__file__)} | {
        'tools/' + name: digest(directory / name) for name in
        ('triad_dynamic_variants.py', 'triad_dynamic_scan.py')}


def configuration_from(protocol, args):
    config = dict(protocol['configuration'])
    for name in ('device', 'precision', 'threads', 'batch_size', 'block_seconds', 'pilot_use',
                 'memory_limit_bytes', 'coherences', 'arrivals', 'frequencies', 'clock_ppm', 'epoch_offsets'):
        value = getattr(args, name)
        if value is not None:
            config[name] = value
    config['coherences'] = [float(value) for value in config['coherences']]
    config['execution'] = getattr(args, 'execution', None) or config.get('execution', 'serial')
    config['projection'] = getattr(args, 'projection', None) or config.get('projection', 'full-envelope')
    if (len(set(config['coherences'])) != len(config['coherences']) or
            any(math.isnan(value) or value <= 0 for value in config['coherences']) or
            sum(math.isinf(value) for value in config['coherences']) != 1 or
            not any(math.isfinite(value) for value in config['coherences'])):
        raise ValueError('distinct positive coherences including one inf and at least one finite value required')
    if not math.isfinite(config['block_seconds']) or config['block_seconds'] <= 0:
        raise ValueError('positive finite block length required')
    if min(config['threads'], config['batch_size'], config['memory_limit_bytes']) < 1:
        raise ValueError('positive thread/batch/memory limits required')
    for name in ('arrivals', 'frequencies', 'clock_ppm', 'epoch_offsets'):
        values = config[name]
        if not values or len(set(values)) != len(values) or not all(math.isfinite(v) for v in values):
            raise ValueError(f'{name} must be a finite nonempty unique public bank')
    return config


def safe_configuration(config):
    result = dict(config)
    result['coherences'] = ['inf' if math.isinf(value) else value for value in config['coherences']]
    return result


def prepare(receiver_factory, config, manifest, templates, epochs, valid, *, device=None):
    indexes = [epochs.index(epoch) for epoch in config['epoch_offsets']]
    return receiver_factory(manifest, templates[indexes], config['epoch_offsets'], valid=valid,
        arrival_seconds=tuple(config['arrivals']), frequencies_hz=tuple(config['frequencies']),
        clock_ppm=tuple(config['clock_ppm']), block_seconds=config['block_seconds'],
        coherence_seconds=tuple(config['coherences']), pilot_use=config['pilot_use'],
        device=device or config['device'], precision=config['precision'],
        memory_limit_bytes=config['memory_limit_bytes'])


def load_batch(campaign, trials, geometry, memory_limit_bytes):
    started = time.perf_counter()
    loaded = [load_capture(campaign / trial['id'], geometry) for trial in trials]
    valid = np.asarray(loaded[0][2])
    if any(not np.array_equal(valid, row[2]) for row in loaded[1:]):
        raise ValueError('replay batch validity differs; fixed prepared design required')
    required = len(loaded) * len(loaded[0][1]) * 17
    if required > memory_limit_bytes:
        raise MemoryError(f'input batch requires {required} bytes above declared limit')
    raw = np.stack([row[1] for row in loaded])
    return loaded, raw, valid, time.perf_counter() - started


def previous_comparison(rows, previous_rows, arm):
    cells = {}
    for row in rows:
        if row['trial']['phase'] == 'calibration':
            continue
        previous = previous_rows[row['trial']['id']]
        def category(item):
            d = item['decisions'][arm]
            if item['trial']['phase'] == 'null':
                return 'false_admission' if d['accepted'] else 'rejected_null'
            return ('miss' if not d['accepted'] else 'correct'
                    if d['bit'] == item['evaluator']['source_bit'] else 'wrong')
        key = category(previous) + ' / ' + category(row)
        cells[key] = cells.get(key, 0) + 1
    return dict(original_then_replay=cells, scope='Same captures; separately calibrated policies; descriptive paired cells.')


def timing_benchmark(*, receiver_factory, config, manifest, templates, epochs, valid, raw,
                     trials, repeats, torch):
    """Full score replays on identical resident host inputs; no admission changes."""
    result = dict(schema='triad-dynamic-resident-score-timing-v1', repeats=repeats,
        input_trials=[t['id'] for t in trials], actual_batch_size=len(raw), backends={},
        environment=dict(platform=platform.platform(), processor=platform.processor(),
            torch_version=torch.__version__, torch_threads=torch.get_num_threads(),
            cuda_version=torch.version.cuda,
            cuda_device_name=torch.cuda.get_device_name(0) if torch.cuda.is_available() else None),
        scope='One warmup per backend/batch; synchronized full score call includes raw transfer, correlation '
              'and state scoring. Preparation is separate; disk reads/hashing/startup/export excluded. '
              'Replays are timing observations, not independent signal trials or a confidence-qualified CI forecast.')
    scores = {}
    for device in ('cpu', 'cuda'):
        if device == 'cuda' and not torch.cuda.is_available():
            result['backends'][device] = {'status': 'unavailable'}
            continue
        mark = time.perf_counter()
        receiver = prepare(receiver_factory, config, manifest, templates, epochs, valid, device=device)
        if device == 'cuda':
            torch.cuda.synchronize()
        backend = dict(status='measured', preparation_seconds=time.perf_counter()-mark,
                       work=receiver.work(), measurements={})
        for count in sorted({1, len(raw)}):
            sample = raw[:count]
            receiver.score(sample)
            times = []
            for _ in range(repeats):
                if device == 'cuda':
                    torch.cuda.synchronize()
                mark = time.perf_counter()
                outcome = receiver.score(sample)
                if device == 'cuda':
                    torch.cuda.synchronize()
                times.append(time.perf_counter()-mark)
            scores[(device, count)] = np.asarray(outcome['hypothesis_log_evidence'])
            backend['measurements'][str(count)] = dict(wall_seconds=times,
                median_wall_seconds=statistics.median(times),
                captures_per_second=count/statistics.median(times))
        result['backends'][device] = backend
        del receiver
        gc.collect()
        if device == 'cuda':
            torch.cuda.empty_cache()
    if all(result['backends'][device]['status'] == 'measured' for device in ('cpu', 'cuda')):
        result['comparisons'] = {}
        for count in sorted({1, len(raw)}):
            cpu, gpu = scores['cpu', count], scores['cuda', count]
            cpu_arms, gpu_arms = paired_arms(cpu, config['coherences']), paired_arms(gpu, config['coherences'])
            result['comparisons'][str(count)] = dict(
                cpu_seconds_over_cuda_seconds=result['backends']['cpu']['measurements'][str(count)]['median_wall_seconds'] /
                    result['backends']['cuda']['measurements'][str(count)]['median_wall_seconds'],
                max_absolute_hypothesis_evidence_difference=float(np.max(np.abs(cpu-gpu))),
                changed_raw_bit_choices={arm: sum(a[arm]['bit'] != b[arm]['bit'] for a, b in zip(cpu_arms, gpu_arms))
                                         for arm in ARMS})
    return result


def main(argv=None):
    started = time.perf_counter()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--templates', type=Path, help='relocated public template directory, if needed')
    parser.add_argument('--device')
    parser.add_argument('--precision', choices=('fp32', 'fp64'))
    parser.add_argument('--threads', type=int)
    parser.add_argument('--batch-size', type=int)
    parser.add_argument('--block-seconds', type=float)
    parser.add_argument('--pilot-use', choices=('both', 'low', 'high', 'none'))
    parser.add_argument('--execution', choices=('serial', 'scan'))
    parser.add_argument('--projection', choices=('full-envelope', 'core'))
    parser.add_argument('--coherences', nargs='+', type=float)
    parser.add_argument('--arrivals', nargs='+', type=float)
    parser.add_argument('--frequencies', nargs='+', type=float)
    parser.add_argument('--clock-ppm', nargs='+', type=float)
    parser.add_argument('--epoch-offsets', nargs='+', type=int)
    parser.add_argument('--memory-limit-bytes', type=int)
    parser.add_argument('--benchmark-cpu-cuda', action='store_true')
    parser.add_argument('--benchmark-repeats', type=int, default=3)
    parser.add_argument('--benchmark-batch-size', type=int, default=8)
    args = parser.parse_args(argv)
    try:
        if args.output.exists():
            raise ValueError('replay output must be a new directory')
        original_protocol = json.loads((args.campaign / 'protocol.json').read_text())
        if original_protocol['schema'] != 'triad-dynamic-campaign-protocol-v1':
            raise ValueError('unsupported source campaign protocol')
        if not (args.campaign / 'summary.json').is_file():
            raise ValueError('source campaign must have completed; incomplete campaigns need explicit separate study')
        config = configuration_from(original_protocol, args)
        schedule = original_protocol['trial_schedule']
        if not schedule or schedule[0]['phase'] != 'calibration':
            raise ValueError('source campaign must begin with calibration')
        if not 1 <= args.benchmark_repeats <= 100 or args.benchmark_batch_size < 1:
            raise ValueError('bounded positive benchmark repetitions and batch size required')
        template_dir = args.templates or (Path(config['templates']) if config.get('templates') else
                                         args.campaign / schedule[0]['id'])
        first_manifest, _, first_valid, first_receipt = load_capture(args.campaign / schedule[0]['id'])
        geometry = public_geometry(first_manifest)
        templates, epochs, template_receipt = read_templates(template_dir, first_manifest)
        original_prepared = json.loads((args.campaign / 'prepared.json').read_text())
        if template_receipt['template_sha256'] != original_prepared['template_sha256']:
            raise ValueError('retained source templates differ from original campaign')
        if any(epoch not in epochs for epoch in config['epoch_offsets']):
            raise ValueError('requested epoch missing from public template bank')
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))

    args.output.mkdir(parents=True)
    hashes = implementation_hashes()
    replay_protocol = dict(schema='triad-dynamic-replay-protocol-v1',
        original_protocol_sha256=digest(args.campaign / 'protocol.json'),
        original_gate_sha256=digest(args.campaign / 'gate.json'),
        original_prepared_sha256=digest(args.campaign / 'prepared.json'),
        configuration=safe_configuration(config), source_hashes=hashes,
        template_receipt=template_receipt, first_capture_receipt=first_receipt,
        trial_schedule=schedule, benchmark=dict(enabled=args.benchmark_cpu_cuda,
            repeats=args.benchmark_repeats, batch_size=args.benchmark_batch_size),
        scope='Paired exploratory replay on previously available captures; fresh policy calibration on original '
              'calibration seeds, then original held-out seeds. No new independent validation is claimed.',
        read_order='No original result or evaluator truth is opened before the corresponding replay scores and decisions.')
    write_json(args.output / 'protocol.json', replay_protocol)
    rows, calibration, previous_rows, gate = [], [], {}, None
    current = None
    timing = dict(load_hash_seconds=0., preparation_seconds=0., receiver_batch_seconds=0.)
    try:
        import torch
        from triad_dynamic_variants import make_receiver
        receiver_factory = partial(make_receiver, execution=config['execution'],
                                   projection=config['projection'])
        torch.set_num_threads(config['threads'])
        if config['precision'] == 'fp32':
            torch.set_float32_matmul_precision('highest')
            torch.backends.cuda.matmul.allow_tf32 = False
        mark = time.perf_counter()
        receiver = prepare(receiver_factory, config, first_manifest, templates, epochs, first_valid)
        timing['preparation_seconds'] = time.perf_counter()-mark
        procedure_id = identity(dict(receiver_procedure_id=receiver.procedure_id,
            replay_policy=safe_configuration(config), source_hashes=hashes,
            template_sha256=template_receipt['template_sha256']))
        condition_id = identity(dict(procedure_id=procedure_id, public_geometry=geometry))
        write_json(args.output / 'prepared.json', dict(procedure_id=procedure_id, condition_id=condition_id,
            receiver_procedure_id=receiver.procedure_id, preparation_seconds=timing['preparation_seconds'],
            receiver_work=receiver.work(), **template_receipt))
        for phase, group in itertools.groupby(schedule, key=lambda row: row['phase']):
            phase_trials = list(group)
            for first in range(0, len(phase_trials), config['batch_size']):
                trials = phase_trials[first:first+config['batch_size']]
                current = [trial['id'] for trial in trials]
                loaded, raw, valid, elapsed = load_batch(args.campaign, trials, geometry, config['memory_limit_bytes'])
                timing['load_hash_seconds'] += elapsed
                if not np.array_equal(valid.astype(bool), receiver.valid):
                    raise ValueError('replay validity differs from prepared design')
                mark = time.perf_counter()
                result = _jsonable(receiver.score(raw))
                elapsed = time.perf_counter()-mark
                timing['receiver_batch_seconds'] += elapsed
                arms = paired_arms(result['hypothesis_log_evidence'], config['coherences'])
                if len(arms) != len(trials):
                    raise ValueError('receiver trial count differs')
                batch_file = f'{phase}-{first:04d}-batch.json'
                write_json(args.output / batch_file, dict(trials=current, wall_seconds=elapsed,
                    correlation_seconds=result.get('correlation_seconds'), state_seconds=result.get('state_seconds'),
                    work=result.get('work')))
                for index, (trial, item, scored) in enumerate(zip(trials, loaded, arms)):
                    manifest, _, _, receipt = item
                    folder = args.campaign / trial['id']
                    row = dict(trial=trial, procedure_id=procedure_id, condition_id=condition_id,
                        **scored, **receipt, batch_receipt=batch_file,
                        model_bit_scores=np.asarray(result['model_bit_scores'])[:, index, :].tolist(),
                        hypothesis_log_evidence=np.asarray(result['hypothesis_log_evidence'])[:, index, :].tolist())
                    if phase == 'calibration':
                        calibration.append(row)
                    else:
                        row['decisions'] = {arm: decide(row, gate, arm) for arm in ARMS}
                    # Only now may prior receiver/evaluator results be read.
                    previous = json.loads((folder / 'result.json').read_text())
                    for field in ('input_sha256', 'manifest_sha256', 'validity_sha256'):
                        if receipt[field] != previous[field]:
                            raise ValueError(f'retained native input differs from original receipt: {field}')
                    row['evaluator'] = evaluator_metrics(folder, manifest)
                    if row['evaluator']['truth_sha256'] != previous['evaluator']['truth_sha256']:
                        raise ValueError('retained evaluator truth differs from original receipt')
                    previous_rows[trial['id']] = previous
                    write_json(args.output / f'{trial["id"]}.json', row)
                    with (args.output / 'trials.jsonl').open('a') as stream:
                        stream.write(json.dumps(row, allow_nan=False)+'\n')
                    rows.append(row)
                    print(json.dumps(dict(trial=trial['id'], decisions=row.get('decisions'))), flush=True)
                del loaded, raw
            if phase == 'calibration':
                gate = freeze_gate(calibration, procedure_id, condition_id, config['alpha'])
                write_json(args.output / 'gate.json', gate)
        del receiver
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
        timing['replay_wall_seconds_before_benchmark'] = time.perf_counter()-started
        if args.benchmark_cpu_cuda:
            selected = [trial for trial in schedule if trial['phase'] == 'signal'][:args.benchmark_batch_size]
            if not selected:
                raise ValueError('timing benchmark requires retained signal captures')
            _, raw, valid, loading = load_batch(args.campaign, selected, geometry, config['memory_limit_bytes'])
            benchmark = timing_benchmark(receiver_factory=receiver_factory, config=config,
                manifest=first_manifest, templates=templates, epochs=epochs, valid=valid,
                raw=raw, trials=selected, repeats=args.benchmark_repeats, torch=torch)
            benchmark['input_loading_hash_seconds'] = loading
            write_json(args.output / 'benchmark.json', benchmark)
            del raw
        if implementation_hashes() != hashes:
            raise ValueError('implementation changed during frozen replay')
        timing['complete_wall_seconds'] = time.perf_counter()-started
        timing['scope'] = 'Includes preparation, loading/hashing, scoring and evidence writes; no native waveform generation.'
        summary = dict(schema='triad-dynamic-replay-summary-v1', completed_trials=len(rows),
            procedure_id=procedure_id, condition_id=condition_id,
            outcomes={arm: outcomes(rows, arm) for arm in ARMS}, paired=paired_outcomes(rows),
            compared_with_original={arm: previous_comparison(rows, previous_rows, arm) for arm in ARMS},
            timing=timing, protocol_sha256=digest(args.output / 'protocol.json'), gate_sha256=digest(args.output / 'gate.json'),
            limitations=[replay_protocol['scope'], 'Empirical marginal per-capture gates do not establish daily false admissions.',
                'Pilot ablation changes receiver use; actual native Helper waveform remains present.',
                'Receiver block/channel model is approximate; source and received energy are unchanged across replays.',
                'Any optional timing measurement is workload-specific and not an inferred full campaign speedup.'])
        write_json(args.output / 'summary.json', summary)
        return summary
    except Exception as error:
        write_json(args.output / 'failure.json', dict(current_batch=current, completed_trials=len(rows),
            error_type=type(error).__name__, error=str(error), wall_seconds=time.perf_counter()-started,
            automatic_retry=False))
        raise


if __name__ == '__main__':
    main()
