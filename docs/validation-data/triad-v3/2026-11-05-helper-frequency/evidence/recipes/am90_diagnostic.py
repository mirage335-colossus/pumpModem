#!/usr/bin/env python3
"""Predeclared AM90 native diagnostics; default action writes a plan only.

These16 DATA-present captures will compare frequency allowance1 and opt-in0
on identical observations. No receiver or estimator executes in this runner.
No AM90 sensitivity/coverage conclusion follows from the earlier AM0.05 gate.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import time

from diagnostic import NATIVE, ROOT, digest, source_hashes, write_json
from cohort import build_check


def schedule():
    return [dict(id=f'{"bounded" if i < 8 else "broad"}-{i % 8:02}',
        seed=1500000+7919*i, bit=i % 2, data=True,
        timing_policy='bounded' if i < 8 else 'broad') for i in range(16)]


def arguments(trial):
    values = NATIVE.copy()
    for name, value in (('--key-seed', '2413'), ('--selective-modulation-rate-hz', '90'),
                        ('--timing-policy', trial['timing_policy'])):
        values[values.index(name)+1] = value
    return values


def protocol(exporter):
    return dict(schema='triad-helper-am90-frequency-diagnostic-plan-v1',
        schedule=schedule(), native_arguments_by_geometry={name: arguments(dict(timing_policy=name))
            for name in ('bounded', 'broad')},
        source_hashes=source_hashes() | {str(Path(__file__).resolve().relative_to(ROOT)): digest(__file__),
            'local-evidence/triad-helper-frequency/cohort.py': digest(Path(__file__).with_name('cohort.py'))},
        exporter_sha256=digest(exporter), build=build_check(exporter),
        estimator_source_sha256=digest(ROOT/'tools/triad_dynamic_frequency.py'),
        frequency_policies={
            'default_modulation_allowance': dict(competitive_score_drop=18., data_frequency_aperture_seconds=.25,
                                               modulation_width_allowance=1.),
            'opt_in_zero_modulation_allowance': dict(competitive_score_drop=18., data_frequency_aperture_seconds=.25,
                                                   modulation_width_allowance=0.)},
        policy_resolution='Other settings are the source-bound frequency_policy defaults; both policies are fixed before any AM90 capture.',
        template_trial='bounded-00', maximum_workers=2,
        scope=['16 diagnostics only, eight per timing prior; no admission or operational false-alarm claim.',
            'AM width90Hz changes physical random modulation; the opt-in zero allowance changes only later frequency-region planning.',
            'Both later frequency policies must use identical captures. No DATA receiver scoring is performed here.',
            'Actual arrival .013s remains hidden from estimation; broad prior does not randomize that actual arrival.',
            'The original99-capture AM0.05 frequency calibration is not AM90 calibration.',
            'Templates are shared only after checking source format, sample clock, key, epoch and public gain.',
            'Native full Helper, random rays, oscillator impairments and DATA remain enabled.'])


def run(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=('plan', 'generate'), default='plan')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exporter', type=Path, default=ROOT/'build/dev-cli/triad_helper_export')
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--budget-seconds', type=float, default=120.)
    args = parser.parse_args(argv)
    if not 1 <= args.workers <= 2 or not 0 < args.budget_seconds <= 120:
        parser.error('at most two workers and120s generation wall budget')
    start = time.perf_counter()
    exporter = args.exporter.resolve(strict=True)
    frozen_path = args.output/'protocol.json'
    current = protocol(exporter)
    if args.action == 'plan':
        if args.output.exists():
            parser.error('plan needs a new output directory')
        args.output.mkdir(parents=True)
        write_json(frozen_path, current)
        print(json.dumps(dict(captures_launched=0, planned_captures=16, protocol_sha256=digest(frozen_path))))
        return
    frozen = json.loads(frozen_path.read_text())
    if frozen != current:
        raise ValueError('source/binary/estimator identity changed from the frozen plan')
    for trial in frozen['schedule']:
        if (args.output/trial['id']).exists():
            raise ValueError('capture already exists; no overwrite or retry')
    deadline, receipts = start+args.budget_seconds, []

    def export(trial):
        mark = time.perf_counter()
        remaining = deadline-mark
        if remaining <= 0:
            raise TimeoutError('AM90 diagnostic budget exhausted')
        folder = args.output/trial['id']
        command = [str(exporter), *arguments(trial), '--seed', str(trial['seed']),
            '--bit', str(trial['bit']), '--data', 'on', '--output', str(folder)]
        if trial['id'] == frozen['template_trial']:
            command.append('--templates')
        with (args.output/(trial['id']+'-export.log')).open('x') as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=remaining,
                env=os.environ | {'OMP_NUM_THREADS':'1', 'OPENBLAS_NUM_THREADS':'1', 'MKL_NUM_THREADS':'1'})
        m = json.loads((folder/'manifest.json').read_text())
        files = ['manifest.json', m['observation']['file'], m['validity']['file']]
        if m['templates']['enabled']:
            files.append(m['templates']['file'])
        hashes = {name: digest(folder/name) for name in files}
        return dict(trial=trial, command=command, input_hashes=hashes,
            capture_seconds=m['calendar']['capture_samples']/m['sample_rate_hz'],
            source_geometry=dict(format=m['format'], sample_rate_hz=m['sample_rate_hz'],
                key_seed=m['key_seed'], public_epoch_seconds=m['public_epoch_seconds'],
                template_data_gain=m['power']['template_data_gain']),
            wall_seconds=time.perf_counter()-mark)

    try:
        for first in range(0, 16, args.workers):
            with ThreadPoolExecutor(max_workers=args.workers) as pool:
                batch = list(pool.map(export, frozen['schedule'][first:first+args.workers]))
            for receipt in batch:
                if receipts and receipt['source_geometry'] != receipts[0]['source_geometry']:
                    raise ValueError('shared template source geometry differs')
                receipts.append(receipt)
                with (args.output/'exports.jsonl').open('a') as stream:
                    stream.write(json.dumps(receipt, allow_nan=False)+'\n')
            print(json.dumps(dict(completed=len(receipts), total=16)), flush=True)
        if protocol(exporter) != frozen:
            raise ValueError('source/binary/estimator changed during capture generation')
        result = dict(completed=16, wall_seconds=time.perf_counter()-start,
            protocol_sha256=digest(frozen_path), exports_sha256=digest(args.output/'exports.jsonl'),
            receiver_scoring_performed=False, frequency_estimation_performed=False, evaluator_truth_read=False)
        write_json(args.output/'generation.json', result)
        print(json.dumps(result))
    except Exception as error:
        write_json(args.output/f'failure-{time.time_ns()}.json', dict(completed=len(receipts),
            error=type(error).__name__, message=str(error), wall_seconds=time.perf_counter()-start,
            automatic_retry=False))
        raise


if __name__ == '__main__':
    run()
