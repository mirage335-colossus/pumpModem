#!/usr/bin/env python3
"""One-off staged native cohort generation; no estimation or receiver scoring.

Planning creates no captures. Generate frequency calibration first, then freeze
the externally fitted cutoff before generating admission calibration/validation.
The cutoff snapshot proves ordering and identity, not correctness of its fit.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import time

from diagnostic import NATIVE, ROOT, digest, source_hashes as diagnostic_hashes, write_json

STAGES = ('frequency-calibration', 'admission-calibration', 'validation')


def schedule():
    rows = []
    for phase, count, enabled in (('frequency-calibration', 99, False),
                                 ('admission-calibration', 39, False),
                                 ('signal', 32, True), ('null', 32, False)):
        for index in range(count):
            serial = len(rows)
            rows.append(dict(id=f'{phase}-{index:03}', phase=phase,
                stage=phase if phase in STAGES else 'validation', index=serial,
                seed=1200000+7919*serial, bit=index % 2, data=enabled))
    return rows


def native_arguments():
    result = NATIVE.copy()
    result[result.index('--key-seed')+1] = '2409'
    return result


def sources():
    return diagnostic_hashes() | {str(Path(__file__).resolve().relative_to(ROOT)): digest(__file__)}


def build_check(exporter):
    check = subprocess.run(['ninja', '-C', str(exporter.parent), '-n', 'triad_helper_export'],
        capture_output=True, text=True, check=True, timeout=5)
    if 'no work to do.' not in check.stdout:
        raise RuntimeError('Native exporter is stale; no implicit rebuild: '+check.stdout+check.stderr)
    return dict(command=check.args, stdout=check.stdout, stderr=check.stderr)


def plan(exporter):
    return dict(schema='triad-helper-frequency-independent-native-plan-v1',
        native_arguments=native_arguments(), trial_schedule=schedule(),
        exporter_sha256=digest(exporter), source_hashes=sources(), build=build_check(exporter),
        build_ninja_sha256=digest(exporter.parent/'build.ninja'),
        cmake_cache_sha256=digest(exporter.parent/'CMakeCache.txt'),
        template_trial='frequency-calibration-000', maximum_native_workers=2,
        phase_order=['Generate99 DATA-absent frequency-region calibration captures.',
            'Fit the frequency-region cutoff using only those99 captures and freeze its complete policy.',
            'Freeze that cutoff artifact using this runner before further capture generation.',
            'Generate39 separate DATA-absent admission calibration captures.',
            'Freeze admission gate before scoring32 independent signals and32 independent nulls.'],
        limitations=['Generation-only plan; no frequency/receiver algorithm or threshold is qualified here.',
            'Helper remains physically present under every DATA-absent null.',
            'All202 native seeds are distinct and separate from the8 diagnostic seeds; key2409 differs.',
            'No evaluator truth is read by this generator; later cutoff evaluation must record its truth use.',
            'Admission gating and no-held-out-data tuning are enforced by the later scoring driver, not this generator.',
            'Arrival remains .013s throughout this cohort, unknown to the receiver; not random-arrival reliability qualification.',
            'Ninja freshness/source/binary identities are retained; not an independent reproducible-build proof.'],
        environment=dict(platform=platform.platform(), cpu_count=os.cpu_count()))


def run(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=('plan', 'generate', 'freeze-frequency-cutoff'), default='plan')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exporter', type=Path, default=ROOT/'build/dev-cli/triad_helper_export')
    parser.add_argument('--stage', choices=STAGES)
    parser.add_argument('--cutoff-file', type=Path)
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--budget-seconds', type=float, default=600.)
    args = parser.parse_args(argv)
    started = time.perf_counter()
    if not 1 <= args.workers <= 2 or not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 600:
        parser.error('at most two native workers and600s per generation stage')
    exporter = args.exporter.resolve(strict=True)
    protocol_path = args.output/'protocol.json'
    current = plan(exporter)
    if args.action == 'plan':
        if args.output.exists():
            parser.error('plan needs a new output directory')
        args.output.mkdir(parents=True)
        write_json(protocol_path, current)
        print(json.dumps(dict(action='plan', captures_launched=0, total_planned_captures=202,
            protocol_sha256=digest(protocol_path), stages={s: sum(t['stage'] == s for t in schedule()) for s in STAGES})))
        return
    frozen = json.loads(protocol_path.read_text())
    if current != frozen:
        raise ValueError('Native source/build/runner differs from frozen plan')
    frequency_receipt = args.output/'frequency-calibration-generation.json'
    cutoff_path = args.output/'frozen-frequency-cutoff.json'
    if args.action == 'freeze-frequency-cutoff':
        if args.cutoff_file is None:
            parser.error('--cutoff-file is required')
        receipt = json.loads(frequency_receipt.read_text())
        if receipt['completed_captures'] != 99 or receipt['protocol_sha256'] != digest(protocol_path):
            raise ValueError('The complete99-capture frequency calibration stage is required')
        payload = json.loads(args.cutoff_file.read_text())
        if not isinstance(payload, dict) or not payload:
            raise ValueError('cutoff artifact must be a nonempty JSON object with the externally frozen policy')
        write_json(cutoff_path, dict(schema='triad-frequency-cutoff-snapshot-v1',
            protocol_sha256=digest(protocol_path), calibration_stage_receipt_sha256=digest(frequency_receipt),
            source_file_sha256=digest(args.cutoff_file), payload=payload,
            scope='Immutable externally supplied cutoff/policy snapshot; this generator does not fit or validate it.'))
        print(json.dumps(dict(action=args.action, cutoff_sha256=digest(cutoff_path), captures_launched=0)))
        return
    if args.stage is None:
        parser.error('--stage is required for generation')
    cutoff_sha = None
    if args.stage != 'frequency-calibration':
        cutoff = json.loads(cutoff_path.read_text())
        if (cutoff['protocol_sha256'] != digest(protocol_path) or
                cutoff['calibration_stage_receipt_sha256'] != digest(frequency_receipt)):
            raise ValueError('frequency cutoff snapshot does not match completed calibration')
        cutoff_sha = digest(cutoff_path)
    trials = [trial for trial in frozen['trial_schedule'] if trial['stage'] == args.stage]
    for trial in trials:
        if (args.output/trial['id']).exists():
            raise ValueError('existing capture prevents automatic repetition or overwrite: '+trial['id'])
    deadline = started+args.budget_seconds
    receipts = []

    def export(trial):
        mark = time.perf_counter()
        remaining = deadline-mark
        if remaining <= 0:
            raise TimeoutError('stage budget exhausted before next launch')
        folder = args.output/trial['id']
        command = [str(exporter), *frozen['native_arguments'], '--seed', str(trial['seed']),
            '--bit', str(trial['bit']), '--data', 'on' if trial['data'] else 'off', '--output', str(folder)]
        if trial['id'] == frozen['template_trial']:
            command.append('--templates')
        env = os.environ | {'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1', 'MKL_NUM_THREADS': '1'}
        with (args.output/(trial['id']+'-export.log')).open('x') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=remaining, env=env)
        m = json.loads((folder/'manifest.json').read_text())
        files = ['manifest.json', m['observation']['file'], m['validity']['file']]
        if m['templates']['enabled']:
            files.append(m['templates']['file'])
        input_hashes = {name: digest(folder/name) for name in files}
        return dict(trial=trial, command=command, native_and_input_hash_wall_seconds=time.perf_counter()-mark,
            input_hashes=input_hashes,
            capture_samples=m['calendar']['capture_samples'], sample_rate_hz=m['sample_rate_hz'])

    try:
        # Bounded batches stop launches after any failed pair; no speculative queue.
        for first in range(0, len(trials), args.workers):
            with ThreadPoolExecutor(max_workers=args.workers) as pool:
                batch = list(pool.map(export, trials[first:first+args.workers]))
            receipts.extend(batch)
            with (args.output/(args.stage+'-exports.jsonl')).open('a') as output:
                for receipt in batch:
                    output.write(json.dumps(receipt, allow_nan=False)+'\n')
            print(json.dumps(dict(stage=args.stage, completed=len(receipts), total=len(trials))), flush=True)
        if sources() != frozen['source_hashes'] or digest(exporter) != frozen['exporter_sha256']:
            raise ValueError('Sources or exporter changed during stage')
        summary = dict(stage=args.stage, completed_captures=len(receipts),
            protocol_sha256=digest(protocol_path), frequency_cutoff_sha256=cutoff_sha,
            complete_wall_seconds=time.perf_counter()-started,
            sum_native_and_input_hash_wall_seconds=sum(r['native_and_input_hash_wall_seconds'] for r in receipts),
            exports_sha256=digest(args.output/(args.stage+'-exports.jsonl')),
            receiver_scoring_performed=False, evaluator_truth_read=False)
        write_json(args.output/(args.stage+'-generation.json'), summary)
        print(json.dumps(summary))
    except Exception as error:
        write_json(args.output/f'failure-{time.time_ns()}.json', dict(stage=args.stage,
            completed_captures=len(receipts), exception=type(error).__name__, message=str(error),
            wall_seconds=time.perf_counter()-started, automatic_retry=False))
        raise


if __name__ == '__main__':
    run()
