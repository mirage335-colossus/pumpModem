#!/usr/bin/env python3
"""Explicit, bounded EME first-bit AWGN qualification; never a broad test run."""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
STAGES = (64, 128, 256, 512, 1024)
CELLS = ((128, 36.020599913279625 - 220 + 164 + 10 * math.log10(128)),
         (128, 17.), (128, 18.), (128, 19.), (256, 17.), (256, 18.), (256, 19.))
ALPHA = .05 / (len(CELLS) * (len(STAGES) + 1))
TOLERANCE = .15
INPUTS = ('src/eme_probability.cpp', 'src/eme_probability.hpp', 'src/simulation_estimate.cpp',
          'src/receiver_probability.cpp', 'src/pattern_receiver.cpp', 'src/pattern_fft_batch.cpp',
          'src/pattern_drift.hpp', 'src/pattern_search.cpp', 'src/channel.cpp', 'src/pattern_code.cpp',
          'src/receiver_strategy.cpp', 'include/datapump/modem.hpp',
          'tests/test_frequency_axis_probability.cpp', 'tools/qualify-eme-probability.py')

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def cdf(k, n, p):
    if k < 0:
        return 0.
    if k >= n or p <= 0:
        return 1.
    if p >= 1:
        return 0.
    logp, logq = math.log(p), math.log1p(-p)
    terms = [math.lgamma(n + 1) - math.lgamma(i + 1) - math.lgamma(n - i + 1)
             + i * logp + (n - i) * logq for i in range(k + 1)]
    top = max(terms)
    return min(1., math.exp(top) * math.fsum(math.exp(t - top) for t in terms))

def inverse_cdf(k, n, target):
    low, high = 0., 1.
    for _ in range(60):
        middle = (low + high) / 2
        if cdf(k, n, middle) > target:
            low = middle
        else:
            high = middle
    return (low + high) / 2

def clopper_pearson(k, n, alpha=ALPHA):
    return (0. if k == 0 else inverse_cdf(k - 1, n, 1 - alpha / 2),
            1. if k == n else inverse_cdf(k, n, alpha / 2))

def assess(k, n, p, draws):
    if not math.isfinite(p) or not 0 <= p <= 1 or draws <= 0:
        raise ValueError('invalid model probability or trial count')
    observed = clopper_pearson(k, n)
    epsilon = math.sqrt(math.log(2 / ALPHA) / (2 * draws))
    model = (max(0., p - epsilon), min(1., p + epsilon))
    difference = (observed[0] - model[1], observed[1] - model[0])
    return {'successes': k, 'captures': n, 'observed_interval': observed,
            'model_probability': p, 'model_trials': draws, 'model_interval': model,
            'difference_interval': difference,
            'qualified': difference[0] >= -TOLERANCE and difference[1] <= TOLERANCE}

def self_test():
    # Independently calculated exact-binomial endpoints and closed-form extremes.
    alpha = 1 / 240
    lo, hi = clopper_pearson(128, 256, alpha)
    assert abs(lo - .409350592394884) < 1e-10 and abs(hi - .590649407604812) < 1e-10
    assert abs(clopper_pearson(0, 64)[1] - (1 - (ALPHA / 2) ** (1 / 64))) < 1e-12
    assert abs(clopper_pearson(64, 64)[0] - (ALPHA / 2) ** (1 / 64)) < 1e-12
    assert assess(128, 256, .5, 4096)['qualified']  # identical center fits the coarse bound
    assert not assess(128, 256, .8, 4096)['qualified']
    for invalid in (float('nan'), float('inf'), -.1, 1.1):
        try:
            assess(128, 256, invalid, 4096)
        except ValueError:
            continue
        raise AssertionError('invalid probability accepted')
    print('EME qualification interval checks passed')

def parse_fields(line):
    return dict(word.split('=', 1) for word in line.split()[1:] if '=' in word)

def atomic_json(path, value):
    candidate = path.with_name(path.name + '.candidate')
    candidate.write_text(json.dumps(value, indent=2) + '\n')
    candidate.replace(path)

import re


def model_value(fields, seconds, es):
    p, draws = float(fields['p']), int(fields['trials'])
    if (fields.get('hardware') != '1' or int(fields['seconds']) != seconds or
            abs(float(fields['es']) - es) > 1e-9 or not math.isfinite(p) or not 0 <= p <= 1 or draws != 4096):
        raise RuntimeError('prediction identity, probability or trial count mismatch')
    return p, draws


def checked_capture(capture, seconds, es, seed):
    if (int(capture['seed']) != seed or capture['early'] != '0' or capture['hardware'] != '1' or
            capture['bits'] != '1' or capture['noise'] != '0' or capture['paired'] != '0' or
            int(capture['seconds']) != seconds or abs(float(capture['es']) - es) > 1e-9 or
            int(capture['axis']) <= 0 or
            any(capture[f] not in ('0', '1') for f in ('first', 'correct', 'complete')) or
            (capture['correct'] == '1' and
             (capture['first'] != '1' or capture['complete'] != '1' or capture['observed'] != '1'))):
        raise RuntimeError('capture failed seed, physical-end, power or scope check')
    return capture


def read_capture_log(path, seconds, es, first, count, partial=False):
    data = path.read_text()
    if 'frequency-axis probability failed:' in data:
        raise RuntimeError(f'receiver assertion in {path}; never replace a failed trial')
    lines = data.splitlines()
    truncated = bool(data and not data.endswith('\n'))
    fragment = lines.pop() if truncated else None
    predictions = [parse_fields(line) for line in lines if line.startswith('prediction ')]
    captures = [parse_fields(line) for line in lines if line.startswith('capture ')]
    if len(predictions) > 1 or len(captures) > count or (not partial and (truncated or len(captures) != count)):
        raise RuntimeError(f'incomplete or excessive capture inventory in {path}')
    if captures and not predictions:
        raise RuntimeError(f'capture without model identity in {path}')
    model = model_value(predictions[0], seconds, es) if predictions else None
    for i, capture in enumerate(captures):
        checked_capture(capture, seconds, es, first + i)
    if not partial and model is None:
        raise RuntimeError(f'missing prediction in {path}')
    return model, captures, fragment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/dev/test_frequency_axis_probability')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--chunk', type=int, default=16)
    parser.add_argument('--seed-base', type=int, default=10000)
    parser.add_argument('--resume-from', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--check-plan', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not 1 <= args.workers <= 8 or not 1 <= args.chunk <= 64 or args.seed_base < 10000:
        parser.error('use1..8 workers,1..64 captures/chunk and held-out seed base>=10000')
    binary = args.binary.resolve()
    identities = {p: sha(ROOT / p) for p in INPUTS}
    identity = {'binary': str(binary), 'binary_sha256': sha(binary), 'sources': identities}
    plan = {'protocol': 'eme-first-bit-awgn-staged-v1-parallel-resume1', 'cells': CELLS, 'stages': STAGES,
            'alpha_total': .05, 'alpha_per_bound': ALPHA, 'absolute_error': TOLERANCE,
            'model_trials': 4096, 'seed_base': args.seed_base, 'seed_stride_per_cell': 100000,
            'workers': args.workers, 'chunk': args.chunk, 'identity': identity,
            'model_allowance': 'fixed displayed prediction plus conservative Monte Carlo allowance; observed intervals establish agreement to that fixed prediction',
            'source_manifest': 'selected project inputs; executable SHA256 freezes built execution',
            'power': 'measured useful-symbol phase-averaged transmitted power',
            'scope': 'public128/256s EME, preset0.58Hz and0.0001ppm offsets/diffusion; exact one-bit reception and physical completion',
            'look_policy': 'only exact original seed prefix at each declared endpoint; completion order never chooses samples'}
    if args.check_plan:
        print(json.dumps(plan, indent=2))
        return 0
    if args.output is None:
        parser.error('--output required')
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('output must be absent or empty; retain earlier output')
    output.mkdir(parents=True, exist_ok=True)
    lock, stop = threading.Lock(), threading.Event()
    state = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'status': 'running', 'cells': {}, 'jobs': {}, 'reused_logs': [], 'capture_counts': {}, 'validated_chunks': {}}
    records = [dict() for _ in CELLS]
    models = [None for _ in CELLS]

    def publish():
        atomic_json(output / 'state.json', state)

    def stable_inputs():
        if sha(binary) != identity['binary_sha256'] or any(sha(ROOT / p) != digest for p, digest in identities.items()):
            raise RuntimeError('frozen binary or source changed during qualification')

    def insert(index, model, captures, provenance):
        with lock:
            if model is not None:
                if models[index] is not None and models[index] != model:
                    raise RuntimeError('changed fixed displayed model prediction')
                models[index] = model
            for capture in captures:
                seed = int(capture['seed'])
                if seed in records[index] and records[index][seed] != capture:
                    raise RuntimeError('conflicting duplicate seed outcome')
                records[index][seed] = capture
            state['capture_counts'][str(index)] = len(records[index])
            if provenance:
                state['reused_logs'].append(provenance)
            if publish_enabled:
                publish()

    publish_enabled = False
    if args.resume_from:
        previous = args.resume_from.resolve()
        old_plan = json.loads((previous / 'frozen-plan.json').read_text())
        old_state = json.loads((previous / 'state.json').read_text())
        if (old_state['status'] not in ('qualified_at_tested_cells', 'not_qualified', 'validated_completed_stage_snapshot') or
                old_state['jobs'] or old_state.get('errors')):
            raise RuntimeError('reuse only fully validated completed stages; interrupted/failed/unknown child outcomes require explicit reconciliation')
        for key in ('cells', 'stages', 'alpha_total', 'alpha_per_bound', 'absolute_error',
                    'model_trials', 'seed_base', 'seed_stride_per_cell'):
            if old_plan[key] != json.loads(json.dumps(plan[key])):
                raise RuntimeError(f'original statistical plan changed: {key}')
        if old_plan['identity']['binary_sha256'] != identity['binary_sha256']:
            raise RuntimeError('resume executable identity changed')
        for path, digest in old_plan['identity']['sources'].items():
            if path != 'tools/qualify-eme-probability.py' and identities.get(path) != digest:
                raise RuntimeError(f'resume model/source identity changed: {path}')
        plan['resumed_from'] = {'directory': str(previous), 'plan_sha256': sha(previous / 'frozen-plan.json'),
                                'state_sha256': sha(previous / 'state.json'), 'status': old_state['status'],
                                'errors': old_state.get('errors', {}),
                                'orchestration_change': 'bounded chunk scheduling only; all complete seed outcomes reused exactly once'}
        plan['resumed_from']['previous_driver_sha256'] = old_plan['identity']['sources']['tools/qualify-eme-probability.py']
        paths = {str(path.resolve()): path for path in previous.glob('cell-*.log')}
        for provenance in old_state.get('reused_logs', []):
            path = Path(provenance['path'])
            if sha(path) != provenance['sha256']:
                raise RuntimeError('predecessor reused capture log changed')
            paths[str(path.resolve())] = path
        for path in sorted(paths.values()):
            old = re.fullmatch(r'cell-(\d+)-(\d+)-n(\d+)\.log', path.name)
            chunk = re.fullmatch(r'cell-(\d+)-seed-(\d+)-count-(\d+)\.log', path.name)
            if old:
                index, seconds, endpoint = map(int, old.groups())
                if index >= len(CELLS) or seconds != CELLS[index][0] or endpoint not in STAGES:
                    raise RuntimeError('resume log outside original cell/stage inventory')
                start = 0 if endpoint == STAGES[0] else STAGES[STAGES.index(endpoint) - 1]
                first, count = args.seed_base + index * 100000 + start, endpoint - start
            elif chunk:
                index, first, count = map(int, chunk.groups())
                if index >= len(CELLS) or count < 1:
                    raise RuntimeError('resume chunk outside cell inventory')
                seconds = CELLS[index][0]
                base = args.seed_base + index * 100000
                if first < base or first + count > base + STAGES[-1]:
                    raise RuntimeError('resume chunk outside original seed inventory')
            else:
                raise RuntimeError('unknown resume log identity')
            model, captures, fragment = read_capture_log(path, seconds, CELLS[index][1], first, count)
            insert(index, model, captures, {'path': str(path), 'sha256': sha(path),
                                          'complete_captures': len(captures), 'unfinished_fragment': fragment})
        reproduced = []
        historical = list(old_state['cells'].items())
        for index, completed in old_state.get('results', {}).items():
            historical.extend((f'cell-{index}-{CELLS[int(index)][0]}', look) for look in completed.get('history', []))
        seen_looks = set()
        for name, earlier in historical:
            index = int(name.split('-')[1])
            base, endpoint = args.seed_base + index * 100000, earlier['captures']
            if endpoint not in STAGES or models[index] is None or any(seed not in records[index] for seed in range(base, base + endpoint)):
                raise RuntimeError('missing predecessor completed-look outcomes')
            if 'stage_log' in earlier and sha(Path(earlier['stage_log'])) != earlier['log_sha256']:
                raise RuntimeError('predecessor recorded stage hash changed')
            k = sum(int(records[index][seed]['correct']) for seed in range(base, base + endpoint))
            assessment = assess(k, endpoint, *models[index])
            if any(json.loads(json.dumps(value)) != earlier[key] for key, value in assessment.items()):
                raise RuntimeError('predecessor completed assessment changed')
            if (name, endpoint) not in seen_looks:
                reproduced.append({'cell': name, 'captures': endpoint, 'qualified': assessment['qualified']})
                seen_looks.add((name, endpoint))
        # Reconstruct every available complete prefix, including earlier looks
        # of a cell that was still active when the validated snapshot was made.
        for index, outcomes in enumerate(records):
            base = args.seed_base + index * 100000
            for endpoint in STAGES:
                if not all(seed in outcomes for seed in range(base, base + endpoint)):
                    break
                value = assess(sum(int(outcomes[seed]['correct']) for seed in range(base, base + endpoint)), endpoint, *models[index])
                if value['qualified'] and any(seed >= base + endpoint for seed in outcomes):
                    raise RuntimeError('predecessor skipped an earlier passing stopping point')
        plan['resumed_from']['reproduced_looks'] = reproduced
    publish_enabled = True
    atomic_json(output / 'frozen-plan.json', plan)

    def join_child(process, timeout):
        deadline = time.monotonic() + timeout
        try:
            while True:
                if stop.is_set():
                    raise RuntimeError('capture stopped after interruption or another technical failure')
                if time.monotonic() >= deadline:
                    raise RuntimeError('capture chunk exceeded bounded workload time')
                try:
                    return process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def run_chunk(index, first, count):
        if stop.is_set():
            raise RuntimeError('qualification stopped before chunk launch')
        stable_inputs()
        seconds, es = CELLS[index]
        name = f'cell-{index}-seed-{first}-count-{count}'
        path = output / f'{name}.log'
        cmd = [str(binary), '--batch', str(seconds), repr(es), str(first), str(count), '0', '1', '1']
        try:
            with path.open('x') as stream:
                process = subprocess.Popen(cmd, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
                with lock:
                    state['jobs'][name] = {'pid': process.pid, 'command': cmd, 'log': str(path), 'count': count}
                    publish()
                try:
                    code = join_child(process, max(300, 30 * count))
                finally:
                    with lock:
                        state['jobs'].pop(name, None)
                        publish()
            if code:
                raise RuntimeError(f'{name} exited{code}; retained {path}')
            stable_inputs()
            model, captures, _ = read_capture_log(path, seconds, es, first, count)
            insert(index, model, captures, None)
            with lock:
                state['validated_chunks'][name] = {'path': str(path), 'sha256': sha(path), 'exit_code': 0, 'captures': count}
                publish()
        except BaseException:
            stop.set()
            raise

    capture_pool = concurrent.futures.ThreadPoolExecutor(max_workers=args.workers)
    def run_cell(index, cell):
        seconds, es = cell
        history = []
        start_time = time.monotonic()
        base = args.seed_base + index * 100000
        for endpoint in STAGES:
            if stop.is_set():
                return {'status': 'stopped_after_other_failure', 'history': history}
            # Complete exactly the planned seed-ID set, never the first n jobs
            # to finish: reception outcome can change processing duration.
            missing = [seed for seed in range(base, base + endpoint) if seed not in records[index]]
            futures = []
            runs = []
            for seed in missing:
                if runs and len(runs[-1]) < args.chunk and runs[-1][-1] + 1 == seed:
                    runs[-1].append(seed)
                else:
                    runs.append([seed])
            for seeds in runs:
                futures.append(capture_pool.submit(run_chunk, index, seeds[0], len(seeds)))
            for future in concurrent.futures.as_completed(futures):
                future.result()
            if any(seed not in records[index] for seed in range(base, base + endpoint)) or models[index] is None:
                raise RuntimeError('incomplete original stage seed set')
            p, draws = models[index]
            k = sum(int(records[index][seed]['correct']) for seed in range(base, base + endpoint))
            result = assess(k, endpoint, p, draws)
            result.update({'seconds': seconds, 'es': es, 'elapsed_seconds': time.monotonic() - start_time,
                           'first_seed': base, 'last_seed': base + endpoint - 1})
            history.append(result)
            with lock:
                state['cells'][f'cell-{index}-{seconds}'] = result
                publish()
                print(json.dumps({'cell': index, **result}), flush=True)
            if result['qualified']:
                return {'status': 'qualified_at_tested_cell', 'result': result, 'history': history}
        return {'status': 'unqualified', 'result': history[-1], 'history': history}

    with lock:
        publish()
    results, errors = {}, {}
    cell_pool = concurrent.futures.ThreadPoolExecutor(max_workers=len(CELLS))
    try:
        futures = {cell_pool.submit(run_cell, i, cell): i for i, cell in enumerate(CELLS)}
        for future in concurrent.futures.as_completed(futures):
            index = futures[future]
            try:
                results[str(index)] = future.result()
            except Exception as error:
                stop.set()
                errors[str(index)] = str(error)
    except KeyboardInterrupt:
        stop.set()
        errors['interrupted'] = 'qualification interrupted; unfinished stages are not statistical results'
    finally:
        cell_pool.shutdown(wait=True, cancel_futures=True)
        capture_pool.shutdown(wait=True, cancel_futures=True)
    try:
        stable_inputs()
    except Exception as error:
        errors['final_identity'] = str(error)
    success = not errors and len(results) == len(CELLS) and all(r['status'] == 'qualified_at_tested_cell' for r in results.values())
    state.update({'finished_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  'status': 'qualified_at_tested_cells' if success else 'not_qualified', 'results': results, 'errors': errors})
    with lock:
        publish()
    atomic_json(output / 'captures.json', [{str(k): v for k, v in sorted(cell.items())} for cell in records])
    atomic_json(output / 'result.json', state)
    return 0 if success else 1

if __name__ == '__main__':
    raise SystemExit(main())
