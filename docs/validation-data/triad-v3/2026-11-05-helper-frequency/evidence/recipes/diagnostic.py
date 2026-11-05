#!/usr/bin/env python3
"""Bounded native capture preparation; deliberately contains no estimator.

The eight-seed diagnostic schedule is frozen before generation. Only the
explicitly requested indices are exported; the default is the initial pair.
Truth summaries are evaluator-only artifacts, never receiver inputs.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ['--case', 'hf', '--rate', '36', '--added-symbols', '10', '--seconds', '16',
    '--data-power-w', '4', '--helper-power-w', '40', '--cn0', '8', '--rf-hz', '30000000',
    '--base-oscillator', 'crystal', '--shift-oscillator', 'ic-7100', '--oscillator-effects', 'on',
    '--channel-evolution', 'stochastic', '--random-doppler-width-hz', '.5',
    '--selective-modulation-rate-hz', '.05', '--selective-perturbation', 'each-65',
    '--spread-preset', 'hf-worst', '--margin', '3', '--sample-rate', '6000',
    '--data-if', '1725', '--timing-policy', 'bounded', '--arrival-seconds', '.013',
    '--helper', 'on', '--helper-components', 'full', '--fhss', 'off', '--shaping', 'auto',
    '--key-seed', '2403', '--epoch', '1793850000']
SCHEDULE = [dict(id=f'diagnostic-{i:02}', seed=1080000+7919*i, bit=i % 2, data=True)
            for i in range(8)]
NATIVE_SOURCES = ['tools/triad-helper-export.cpp', 'src/triad_modem_v3.cpp',
    'src/triad_simulation_stream.cpp', 'src/channel_perturbation.hpp',
    'src/triad_random_process.hpp', 'src/channel_spread.hpp', 'src/triad_gaussian_fading.hpp',
    'src/helper_carrier.cpp', 'src/triad_shaping.cpp', 'src/crypto.cpp', 'src/tuning_presets.cpp',
    'include/datapump/triad_modem_v3.hpp', 'include/datapump/triad_helper_burst.hpp',
    'include/datapump/pattern_pulse.hpp', 'include/datapump/helper_carrier.hpp',
    'include/datapump/crypto.hpp', 'include/datapump/triad_simulation.hpp', 'CMakeLists.txt']


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024*1024), b''):
            value.update(block)
    return value.hexdigest()


def write_json(path, value):
    with Path(path).open('x') as target:
        json.dump(value, target, indent=2, sort_keys=True, allow_nan=False)
        target.write('\n')


def source_hashes():
    return {name: digest(ROOT/name) for name in NATIVE_SOURCES} | {
        str(Path(__file__).resolve().relative_to(ROOT)): digest(__file__)}


def run(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, default=ROOT/'build/dev-cli/triad_helper_export')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--indices', type=int, nargs='+', default=[0, 1])
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--budget-seconds', type=float, default=30.)
    args = parser.parse_args(argv)
    started = time.perf_counter()
    if (not 1 <= args.workers <= 2 or not 0 < args.budget_seconds <= 30 or
            len(set(args.indices)) != len(args.indices) or any(i not in range(8) for i in args.indices)):
        parser.error('at most two workers, 30 seconds and distinct diagnostic indices 0..7')
    exporter = args.exporter.resolve(strict=True)
    check = subprocess.run(['ninja', '-C', str(exporter.parent), '-n', 'triad_helper_export'],
        text=True, capture_output=True, check=True, timeout=5)
    if 'no work to do.' not in check.stdout:
        raise RuntimeError('Native target is stale; no implicit rebuild: '+check.stdout+check.stderr)
    hashes = source_hashes()
    protocol = dict(schema='triad-helper-stochastic-frequency-diagnostic-v1',
        native_arguments=NATIVE, trial_schedule=SCHEDULE,
        source_hashes=hashes, exporter_sha256=digest(exporter),
        build=dict(up_to_date_command=check.args, stdout=check.stdout, stderr=check.stderr,
            cmake_cache_sha256=digest(exporter.parent/'CMakeCache.txt'),
            ninja_file_sha256=digest(exporter.parent/'build.ninja'),
            verification_scope='Ninja dependency graph reports no pending work; binary and relevant native sources retained, not an independent reproducible-build proof.'),
        controls=dict(maximum_native_workers=2, first_requested_indices=[0, 1],
            maximum_invocation_wall_seconds=30, template_trial=SCHEDULE[0]['id']),
        scope=['Eight predeclared diagnostic seeds, not independent calibration or reception validation.',
            'Actual native oscillator/channel/noise draws; no receiver or estimator is called.',
            'Arrival .013 seconds is evaluator-known but must remain unknown to a receiver.',
            'Physical4W/180dB/-164dBmHz C/N0 differs from the explicit8dB-Hz injected override.',
            'Shared templates contain all13 keyed epochs and two bits, without channel or arrival truth.',
            'All source, Helper auxiliary, random channel and oscillator effects remain enabled.'],
        environment=dict(platform=platform.platform(), cpu_count=os.cpu_count()))
    protocol_path = args.output/'protocol.json'
    if args.output.exists():
        if json.loads(protocol_path.read_text()) != protocol:
            raise ValueError('frozen protocol/source/build changed; use a new output')
        if 0 not in args.indices and not (args.output/SCHEDULE[0]['id']/'manifest.json').exists():
            raise ValueError('shared template capture must exist first')
    else:
        if 0 not in args.indices:
            raise ValueError('first invocation must export template trial0')
        args.output.mkdir(parents=True)
        write_json(protocol_path, protocol)
    for index in args.indices:
        if (args.output/SCHEDULE[index]['id']).exists():
            raise ValueError('no overwrite or automatic retry of existing capture')
    deadline = started+args.budget_seconds

    def export(index):
        trial = SCHEDULE[index]
        folder = args.output/trial['id']
        command = [str(exporter), *NATIVE, '--seed', str(trial['seed']), '--bit', str(trial['bit']),
            '--data', 'on' if trial['data'] else 'off', '--output', str(folder)]
        if index == 0:
            command.append('--templates')
        mark = time.perf_counter()
        remaining = deadline-mark
        if remaining <= 0:
            raise TimeoutError('capture preparation wall budget exhausted before launch')
        env = os.environ | {'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1', 'MKL_NUM_THREADS': '1'}
        with (args.output/(trial['id']+'-export.log')).open('x') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=remaining, env=env)
        return dict(trial_id=trial['id'], command=command, native_wall_seconds=time.perf_counter()-mark)

    receipts = []
    try:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            receipts = list(pool.map(export, args.indices))
        export_complete = time.perf_counter()
        public, evaluators = [], []
        for index, receipt in zip(args.indices, receipts):
            trial = SCHEDULE[index]
            folder = args.output/trial['id']
            m = json.loads((folder/'manifest.json').read_text())
            ch, cal = m['channel'], m['calendar']
            public.append(dict(trial_id=trial['id'], **{k: m[k] for k in
                ('sample_rate_hz', 'format', 'calendar', 'channel', 'power', 'helper_plan', 'templates')},
                capture_seconds=cal['capture_samples']/m['sample_rate_hz'],
                original_margin_cfo_prior_half_width_hz=ch['deterministic_cfo_bound_hz']*ch['spread_margin'],
                input_hashes={name: digest(folder/name) for name in
                    ('manifest.json', m['observation']['file'], m['validity']['file'])},
                template_sha256=digest(folder/m['templates']['file']) if m['templates']['enabled'] else None))
            truth_path = folder/m['evaluator_truth_file']
            truth = json.loads(truth_path.read_text())
            tx, rx = truth['tx'], truth['rx']
            rx_scale = 1+rx['audio_ppm']*1e-6
            cfo = ((tx['rf_ppm']-rx['rf_ppm'])*ch['rf_hz']+
                (tx['audio_ppm']-rx['audio_ppm'])*ch['nominal_conversion_if_hz'])*1e-6/rx_scale
            evaluators.append(dict(trial_id=trial['id'], truth_sha256=digest(truth_path),
                tx=tx, rx=rx, arrival_seconds=truth['arrival_seconds'],
                source_bit=truth['source_bit'], nominal_static_cfo_hz=cfo,
                relative_audio_clock_ppm=((1+tx['audio_ppm']*1e-6)/rx_scale-1)*1e6,
                scope='EVALUATOR ONLY: static endpoint draws exclude random phase/channel evolution; not estimator input.'))
        if source_hashes() != hashes or digest(exporter) != protocol['exporter_sha256']:
            raise ValueError('native sources or binary changed during preparation')
        suffix = '-'.join(map(str, args.indices))
        write_json(args.output/f'public-summary-{suffix}.json', public)
        write_json(args.output/f'evaluator-only-summary-{suffix}.json', evaluators)
        result = dict(indices=args.indices, receipts=receipts, protocol_sha256=digest(protocol_path),
            export_stage_wall_seconds=export_complete-started,
            complete_wall_seconds=time.perf_counter()-started,
            receiver_scoring_performed=False, completed_captures=len(receipts),
            public_summary_file=f'public-summary-{suffix}.json',
            evaluator_summary_file=f'evaluator-only-summary-{suffix}.json')
        write_json(args.output/f'preparation-{suffix}.json', result)
        print(json.dumps(result, allow_nan=False))
        return result
    except Exception as error:
        write_json(args.output/f'failure-{time.time_ns()}.json', dict(
            exception=type(error).__name__, message=str(error),
            wall_seconds=time.perf_counter()-started, automatic_retry=False, receipts=receipts))
        raise


if __name__ == '__main__':
    run()
