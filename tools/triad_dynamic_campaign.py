#!/usr/bin/env python3
"""Frozen paired admission study for the isolated stochastic joint receiver.

This is an occasional sampled-waveform experiment, not a routine CI campaign.
Native captures contain the complete configured Helper auxiliary, pilots and
three-carrier DATA. Every declared receiver arm shares each observation and the
same prepared block statistics. The default experiment conditions on known
arrival and disabled oscillator errors; it is not blind acquisition evidence.

Example (the exporter must include the stochastic controls)::

  python tools/triad_dynamic_campaign.py --exporter build/dev-cli/triad_helper_export \
    --output local-evidence/dynamic-hf --seed-base 7000 \
    --export-arg='--case hf --seconds 16 --cn0 8 --channel-evolution stochastic' \
    --export-arg='--random-doppler-width-hz .05 --selective-modulation-rate-hz .05' \
    --block-seconds .25 --coherences .1 .5 2 inf

The finite-coherence mixture and static infinite-coherence comparator each get
their own frozen empirical rank gate. These are marginal per-capture diagnostic
gates under exchangeability, not simultaneous or operational daily guarantees.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import time

import numpy as np
from scipy.special import logsumexp

from triad_helper_statistics import binomial_interval, signal_outcome_statistics


ARMS = ('static', 'dynamic')
OWNED_EXPORT_FLAGS = {'--output', '--seed', '--bit', '--data', '--templates'}


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                    allow_nan=False).encode()).hexdigest()


def write_json(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def schedule(seed_base, calibration_count, signal_count, null_count):
    counts = (calibration_count, signal_count, null_count)
    if (isinstance(seed_base, bool) or not isinstance(seed_base, int) or seed_base < 0 or
            any(isinstance(n, bool) or not isinstance(n, int) or n < 0 for n in counts) or
            calibration_count < 1 or seed_base + sum(counts) > 2**64):
        raise ValueError('nonnegative integral seeds/counts, at least one calibration, and uint64 seeds required')
    rows = []
    for phase, count in zip(('calibration', 'signal', 'null'), counts):
        for index in range(count):
            rows.append(dict(id=f'{phase}-{index:04d}', phase=phase,
                             seed=seed_base + len(rows), bit=index % 2,
                             data=phase == 'signal'))
    return rows


def export_arguments(values, acquisition):
    arguments = [word for value in values for word in shlex.split(value)]
    flags = {word.split('=', 1)[0] for word in arguments if word.startswith('--')}
    conflict = flags & OWNED_EXPORT_FLAGS
    if conflict:
        raise ValueError('campaign owns native options: ' + ', '.join(sorted(conflict)))
    if '--help' in flags or '--plan-only' in flags:
        raise ValueError('campaign requires actual sampled captures')
    if '--whole-bit-count' in flags or '--whole-bit-wait-count' in flags:
        raise ValueError('multi-bit calendar needs a separately defined multi-bit admission experiment')
    if acquisition == 'conditioned':
        if flags & {'--arrival-seconds', '--oscillator-effects'}:
            raise ValueError('conditioned acquisition owns arrival/oscillator controls; use public-bank otherwise')
        arguments += ['--arrival-seconds', '0', '--oscillator-effects', 'off']
    return arguments


def public_geometry(manifest):
    """Calibration identity; deliberately excludes DATA on/off and realized truth."""
    return {key: manifest[key] for key in ('schema', 'sample_rate_hz', 'format', 'calendar',
        'helper_plan', 'channel', 'power', 'helper_enabled', 'public_epoch_seconds', 'key_seed')} | {
        'helper_component_control': manifest.get('helper_component_control', 'full')}


def calibrated_procedure_identity(receiver, *, geometry, configuration,
                                  template_sha256, sources):
    """Bind the resolved receiver, including custom factories, into its gate.

    The receiver owns its internal algorithm/projection/source identity. A
    custom factory without that identity cannot produce reusable calibrated
    gates. Historical v1 evidence remains unchanged; newly produced identities
    include this explicit v2 binding.
    """
    receiver_id = getattr(receiver, 'procedure_id', None)
    if not isinstance(receiver_id, str) or not receiver_id.strip():
        raise ValueError('receiver must expose a nonempty effective procedure_id for calibration')
    keys = ('block_seconds', 'coherences', 'pilot_use', 'acquisition', 'arrivals',
            'frequencies', 'clock_ppm', 'epoch_offsets', 'device', 'precision',
            'execution', 'projection')
    return identity(dict(schema='triad-dynamic-calibrated-procedure-v2',
        receiver_procedure_id=receiver_id, public_geometry=geometry,
        receiver_configuration={key: configuration[key] for key in keys},
        template_sha256=template_sha256, source_hashes=sources))


def template_geometry(manifest):
    return {key: manifest[key] for key in ('sample_rate_hz', 'format', 'public_epoch_seconds', 'key_seed')} | {
        'data_gain': manifest['power']['template_data_gain']}


def read_templates(directory, manifest):
    directory = Path(directory)
    tm = json.loads((directory / 'manifest.json').read_text())
    if not tm['templates']['enabled'] or template_geometry(tm) != template_geometry(manifest):
        raise ValueError('template source geometry does not match the capture')
    shape = tuple(tm['templates']['shape'])
    if len(shape) != 3 or shape[1] != 2 or min(shape) < 1:
        raise ValueError('templates require [epochs,2,samples]')
    path = directory / tm['templates']['file']
    if path.stat().st_size != math.prod(shape) * 16:
        raise ValueError('template byte count differs from public shape')
    values = np.memmap(path, dtype='<c16', mode='r', shape=shape)
    return values, tuple(tm['templates']['epoch_offsets_seconds']), dict(
        template_sha256=digest(path), template_manifest_sha256=digest(directory / 'manifest.json'))


def load_capture(directory, expected_geometry=None):
    directory = Path(directory)
    manifest = json.loads((directory / 'manifest.json').read_text())
    geometry = public_geometry(manifest)
    if expected_geometry is not None and geometry != expected_geometry:
        raise ValueError('public capture geometry changed; a new prepared procedure and calibration are required')
    shape = tuple(manifest['observation']['shape'])
    if len(shape) != 1 or shape[0] < 1:
        raise ValueError('one nonempty complex observation vector required')
    path, mask = directory / manifest['observation']['file'], directory / manifest['validity']['file']
    if path.stat().st_size != shape[0] * 16 or mask.stat().st_size != shape[0]:
        raise ValueError('capture or validity byte count differs from public shape')
    raw = np.memmap(path, dtype='<c16', mode='r', shape=shape)
    valid = np.memmap(mask, dtype='u1', mode='r', shape=shape)
    if np.any(valid > 1):
        raise ValueError('capture validity is not binary')
    return manifest, raw, valid, dict(input_sha256=digest(path), validity_sha256=digest(mask),
                                     manifest_sha256=digest(directory / 'manifest.json'))


def paired_arms(hypothesis_log_evidence, coherences):
    """Mix H0/H1 separately, then divide; input [coherences,trials,3]."""
    scores = np.asarray(hypothesis_log_evidence, dtype=np.float64)
    if (scores.ndim != 3 or scores.shape[0] != len(coherences) or scores.shape[2] != 3 or
            not np.isfinite(scores).all()):
        raise ValueError('finite [coherences,trials,H0/bit0/bit1] model evidence required')
    static = [i for i, tau in enumerate(coherences) if math.isinf(tau)]
    dynamic = [i for i, tau in enumerate(coherences) if math.isfinite(tau)]
    if len(static) != 1 or not dynamic:
        raise ValueError('one static infinite-coherence and at least one finite-coherence model required')
    static_evidence = scores[static[0]]
    dynamic_evidence = logsumexp(scores[dynamic], axis=0) - math.log(len(dynamic))
    reduced = {'static': static_evidence[:, 1:] - static_evidence[:, :1],
               'dynamic': dynamic_evidence[:, 1:] - dynamic_evidence[:, :1]}
    result = []
    for index in range(scores.shape[1]):
        row = {}
        for arm in ARMS:
            bits = reduced[arm][index]
            bit = int(np.argmax(bits))
            row[arm] = dict(bit=bit, bit_log_likelihoods=bits.tolist(),
                            working_presence=float(logsumexp(bits) - math.log(2)),
                            opposite_bit_margin=float(bits[bit] - bits[1 - bit]))
        result.append(row)
    return result


def freeze_gate(rows, procedure_id, condition_id, alpha):
    if not rows or not 0 < alpha < 1:
        raise ValueError('nonempty calibration and 0 < alpha < 1 required')
    if any(r['trial']['phase'] != 'calibration' or r['procedure_id'] != procedure_id or
           r['condition_id'] != condition_id for r in rows):
        raise ValueError('calibration procedure/condition/phase differs')
    values = {arm: [float(r[arm]['working_presence']) for r in rows] for arm in ARMS}
    if not all(math.isfinite(x) for arm in values.values() for x in arm):
        raise ValueError('nonfinite calibration score')
    if alpha < 1 / (len(rows) + 1):
        raise ValueError('calibration cannot resolve the requested rank-test alpha')
    return dict(schema='triad-dynamic-rank-gate-v1', procedure_id=procedure_id,
                condition_id=condition_id, count=len(rows), alpha=alpha, scores=values,
                minimum_p=1 / (len(rows) + 1),
                rule='(1 + count(calibration >= held-out score))/(n+1) <= alpha',
                scope='Separate marginal per-arm, per-capture exchangeable rank tests; ties are conservative. '
                      'Not simultaneous across arms or conditions, not a fixed-gate conditional false-alarm bound, '
                      'and not an operational daily guarantee.')


def decide(row, gate, arm):
    if row['procedure_id'] != gate['procedure_id'] or row['condition_id'] != gate['condition_id']:
        raise ValueError('gate identity differs')
    score = float(row[arm]['working_presence'])
    if not math.isfinite(score):
        raise ValueError('nonfinite held-out score')
    p = (1 + sum(x >= score for x in gate['scores'][arm])) / (gate['count'] + 1)
    return dict(accepted=p <= gate['alpha'], p_value=p, bit=row[arm]['bit'], working_presence=score)


def outcomes(rows, arm):
    signals = signal_outcome_statistics(rows, arm)
    nulls = [row for row in rows if row['trial']['phase'] == 'null']
    admitted = sum(row['decisions'][arm]['accepted'] for row in nulls)
    return dict(signal=signals, null=dict(false_admitted=admitted, rejected_null=len(nulls)-admitted,
                trials=len(nulls), uncertainty=binomial_interval(admitted, len(nulls))),
                uncertainty_scope='Conditional held-out uncertainty assumes independent representative seeds '
                    'within fixed source-bit strata and the frozen gate. Bounds are per metric, not simultaneous.')


def paired_outcomes(rows):
    cells = {}
    for row in rows:
        if row['trial']['phase'] == 'calibration':
            continue
        def category(arm):
            d = row['decisions'][arm]
            if row['trial']['phase'] == 'null':
                return 'false_admission' if d['accepted'] else 'rejected_null'
            return ('miss' if not d['accepted'] else
                    'correct' if d['bit'] == row['evaluator']['source_bit'] else 'wrong')
        key = category('static') + ' / ' + category('dynamic')
        cells[key] = cells.get(key, 0) + 1
    return dict(static_then_dynamic=cells,
                scope='Paired descriptive counts, not independent observations across receiver arms.')


def source_hashes():
    root = Path(__file__).resolve().parents[1]
    names = ['tools/triad_dynamic_campaign.py', 'tools/triad_dynamic_joint.py',
             'tools/triad_dynamic_variants.py', 'tools/triad_dynamic_scan.py',
             'tools/triad_gaussian_state.py', 'tools/triad_gaussian_prepared.py',
             'tools/triad_helper_statistics.py', 'tools/triad-helper-export.cpp',
             'src/triad_modem_v3.cpp', 'src/triad_simulation_stream.cpp',
             'src/channel_perturbation.hpp', 'src/triad_random_process.hpp',
             'src/channel_spread.hpp', 'src/triad_gaussian_fading.hpp',
             'src/helper_carrier.cpp', 'src/triad_shaping.cpp', 'src/crypto.cpp', 'src/tuning_presets.cpp',
             'include/datapump/triad_modem_v3.hpp', 'include/datapump/triad_helper_burst.hpp',
             'include/datapump/pattern_pulse.hpp', 'include/datapump/helper_carrier.hpp',
             'include/datapump/crypto.hpp', 'CMakeLists.txt']
    return {name: digest(root / name) for name in names}


def evaluator_metrics(directory, manifest):
    """Called only after receiver scoring and any held-out decisions."""
    path = Path(directory) / manifest['evaluator_truth_file']
    truth = json.loads(path.read_text())
    fs = manifest['sample_rate_hz']
    power = manifest['power']
    source, channel = truth['source_energy'], truth['channel_report']
    data_energy = float(source['data_energy_samples']) / fs
    return dict(source_bit=truth['source_bit'], arrival_seconds=truth['arrival_seconds'],
                emitted_data_energy_seconds=data_energy,
                emitted_helper_energy_seconds=float(source['helper_energy_samples']) / fs,
                received_composite_energy_seconds=float(channel['signal_energy_samples']) / fs,
                nominal_data_cn0_db_hz=power['data_cn0_db_hz'],
                emitted_data_energy_over_n0=data_energy * 10**(power['data_cn0_db_hz'] / 10),
                actual_bit_seconds=manifest['format']['bit_seconds'],
                capture_seconds=channel['samples'] / fs,
                source_energy=source, channel_report=channel,
                energy_scope='DATA source energy is separate from received composite energy. With Helper present, '
                    'the latter includes Helper and cross terms and is not a received DATA Eb/N0.',
                truth_sha256=digest(path))


def _jsonable(value):
    if hasattr(value, 'detach'):
        return value.detach().cpu().tolist()
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    return value


def main(argv=None, *, receiver_factory=None, export_runner=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--templates', type=Path)
    parser.add_argument('--export-arg', action='append', default=[],
                        help='native argument string; repeat as needed; shell quoting is parsed without a shell')
    parser.add_argument('--seed-base', type=int, required=True)
    parser.add_argument('--calibration-count', type=int, default=39)
    parser.add_argument('--signal-count', type=int, default=32)
    parser.add_argument('--null-count', type=int, default=32)
    parser.add_argument('--alpha', type=float, default=.05)
    parser.add_argument('--block-seconds', type=float, default=.25)
    parser.add_argument('--coherences', nargs='+', type=float, default=(.1, .5, 2., math.inf))
    parser.add_argument('--pilot-use', choices=('both', 'low', 'high', 'none'), default='both')
    parser.add_argument('--execution', choices=('serial', 'scan'), default='serial',
                        help='explicit Gaussian likelihood execution; serial preserves the previous default')
    parser.add_argument('--projection', choices=('full-envelope', 'core'), default='full-envelope',
                        help='core is a lossy fixed-bin alternative and requires --execution scan')
    parser.add_argument('--acquisition', choices=('conditioned', 'public-bank'), default='conditioned')
    parser.add_argument('--arrivals', type=float, nargs='+', default=(0.,))
    parser.add_argument('--frequencies', type=float, nargs='+', default=(0.,))
    parser.add_argument('--clock-ppm', type=float, nargs='+', default=(0.,))
    parser.add_argument('--epoch-offsets', type=int, nargs='+', default=(0,))
    parser.add_argument('--device', default='cpu')
    parser.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--batch-size', type=int, default=4)
    parser.add_argument('--export-workers', type=int, default=1)
    parser.add_argument('--export-timeout', type=float, default=600.)
    parser.add_argument('--memory-limit-bytes', type=int, default=2 * 1024**3)
    parser.add_argument('--discard-iq', action='store_true')
    args = parser.parse_args(argv)
    try:
        trials = schedule(args.seed_base, args.calibration_count, args.signal_count, args.null_count)
        if not 0 < args.alpha < 1 or args.alpha < 1 / (args.calibration_count + 1):
            raise ValueError('alpha outside (0,1) or below calibration rank resolution')
        if not math.isfinite(args.block_seconds) or args.block_seconds <= 0:
            raise ValueError('positive finite block length required')
        if any(math.isnan(t) or t <= 0 for t in args.coherences) or len(set(args.coherences)) != len(args.coherences):
            raise ValueError('distinct positive coherences required')
        if sum(math.isinf(t) for t in args.coherences) != 1 or not any(math.isfinite(t) for t in args.coherences):
            raise ValueError('include one inf static comparator and at least one finite coherence')
        if (min(args.threads, args.batch_size, args.export_workers, args.memory_limit_bytes) < 1 or
                args.export_workers > args.batch_size or not math.isfinite(args.export_timeout) or args.export_timeout <= 0):
            raise ValueError('positive resource limits required; export workers must not exceed batch size')
        for bank in (args.arrivals, args.frequencies, args.clock_ppm):
            if not bank or not all(math.isfinite(v) for v in bank) or len(set(bank)) != len(bank):
                raise ValueError('search banks must be nonempty, finite and distinct')
        if not args.epoch_offsets or len(set(args.epoch_offsets)) != len(args.epoch_offsets):
            raise ValueError('epoch offsets must be nonempty and distinct')
        if args.acquisition == 'conditioned' and (tuple(args.arrivals) != (0.,) or
                tuple(args.frequencies) != (0.,) or tuple(args.clock_ppm) != (0.,) or tuple(args.epoch_offsets) != (0,)):
            raise ValueError('conditioned experiment fixes all public acquisition coordinates to nominal')
        if args.projection == 'core' and args.execution != 'scan':
            raise ValueError('core projection currently requires --execution scan')
        export_args = export_arguments(args.export_arg, args.acquisition)
        exporter = args.exporter.resolve(strict=True)
        if args.output.exists():
            raise ValueError('output already exists; frozen experiments are never overwritten or retried')
        if args.templates is not None and not (args.templates / 'manifest.json').is_file():
            raise ValueError('template manifest missing')
    except (ValueError, OSError) as error:
        parser.error(str(error))

    started = time.perf_counter()
    args.output.mkdir(parents=True)
    configuration = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
    configuration['coherences'] = ['inf' if math.isinf(t) else t for t in args.coherences]
    protocol = dict(schema='triad-dynamic-campaign-protocol-v2', configuration=configuration,
        native_arguments=export_args, source_hashes=source_hashes(), exporter_sha256=digest(exporter),
        trial_schedule=trials, python=sys.version, platform=platform.platform(),
        score_policy='One cached projected block design; static inf and equal finite-coherence dynamic mixture; '
                     'equal source-bit mixture for presence. No learned model selection.',
        acquisition_scope=('Known arrival, epoch and oscillator coordinates, explicitly conditioned diagnostic.'
                           if args.acquisition == 'conditioned' else
                           'Declared finite public bank; no claim of coverage beyond supplied coordinates.'),
        null_policy='Native DATA off; configured complete Helper and random channel unchanged.',
        read_order='Receiver accepts public manifest/templates/IQ/mask only; evaluator truth read after scores and decisions.',
        gate_policy='Fixed marginal per-arm empirical rank gate; no sequential stopping or automatic retries.',
        native_binary_provenance='Binary hash and current source hashes retained; caller must verify their build relationship.',
        noise_policy='Fresh native noisy capture per seed; no synthetic-noise or reduced-channel substitution.')
    protocol['receiver_factory'] = ('built-in explicit execution/projection selection' if receiver_factory is None else
        'caller-supplied '+getattr(receiver_factory, '__module__', type(receiver_factory).__module__)+'.'+
        getattr(receiver_factory, '__qualname__', type(receiver_factory).__qualname__))
    protocol['calibration_identity_policy'] = ('v2 binds the constructed receiver.procedure_id, including its '
        'resolved custom-factory algorithm/projection identity; requested CLI configuration alone is insufficient.')
    write_json(args.output / 'protocol.json', protocol)
    export_runner = export_runner or subprocess.run
    if receiver_factory is None:
        import torch
        torch.set_num_threads(args.threads)
        if args.precision == 'fp32':
            torch.set_float32_matmul_precision('highest')
            torch.backends.cuda.matmul.allow_tf32 = False
        if args.execution == 'serial':
            from triad_dynamic_joint import DynamicJointReceiver
            receiver_factory = DynamicJointReceiver
        elif args.projection == 'core':
            from triad_dynamic_variants import CoreProjectionReceiver
            receiver_factory = CoreProjectionReceiver
        else:
            from triad_dynamic_variants import ScanJointReceiver
            receiver_factory = ScanJointReceiver
    rows, calibration, receiver, gate = [], [], None, None
    expected_geometry = None
    timings = dict(export_stage_wall_seconds=0., native_export_sum_seconds=0.,
                   load_hash_seconds=0., preparation_seconds=0., receiver_batch_seconds=0.)
    current = None

    def export_trial(trial, with_templates=False):
        folder = args.output / trial['id']
        command = [str(exporter), *export_args, '--output', str(folder), '--seed', str(trial['seed']),
                   '--bit', str(trial['bit']), '--data', 'on' if trial['data'] else 'off']
        if with_templates:
            command.append('--templates')
        mark = time.perf_counter()
        with (args.output / f'{trial["id"]}-export.log').open('x') as log:
            export_runner(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                          timeout=args.export_timeout)
        return trial, folder, command, time.perf_counter() - mark

    try:
        for phase, phase_trials_iterator in itertools.groupby(trials, key=lambda t: t['phase']):
            phase_trials = list(phase_trials_iterator)
            for first in range(0, len(phase_trials), args.batch_size):
                batch = phase_trials[first:first + args.batch_size]
                current = [t['id'] for t in batch]
                mark = time.perf_counter()
                if receiver is None:
                    # Generate one shared template bank, never one per trial.
                    exported = [export_trial(batch[0], with_templates=args.templates is None)]
                    remainder = batch[1:]
                else:
                    exported, remainder = [], batch
                with ThreadPoolExecutor(max_workers=args.export_workers) as pool:
                    exported += list(pool.map(export_trial, remainder))
                timings['export_stage_wall_seconds'] += time.perf_counter() - mark
                timings['native_export_sum_seconds'] += sum(item[3] for item in exported)
                mark = time.perf_counter()
                loaded = [load_capture(folder, expected_geometry) for _, folder, _, _ in exported]
                if expected_geometry is None:
                    expected_geometry = public_geometry(loaded[0][0])
                    if any(public_geometry(item[0]) != expected_geometry for item in loaded):
                        raise ValueError('initial batch public geometry differs')
                manifest = loaded[0][0]
                row_count = len(loaded[0][1])
                # Includes raw complex128 batch and uint8 validity; receiver
                # preparation has its own separately declared memory budget.
                required = len(loaded) * row_count * 17
                if required > args.memory_limit_bytes:
                    raise MemoryError(f'input batch requires {required} bytes, above declared limit')
                validity = np.asarray(loaded[0][2])
                if any(not np.array_equal(validity, item[2]) for item in loaded[1:]):
                    raise ValueError('validity differs inside batch; fixed-design campaign requires one public mask')
                raw = np.stack([item[1] for item in loaded])
                timings['load_hash_seconds'] += time.perf_counter() - mark
                if receiver is None:
                    mark = time.perf_counter()
                    template_dir = args.templates or exported[0][1]
                    templates, all_epochs, template_receipt = read_templates(template_dir, manifest)
                    try:
                        indexes = [all_epochs.index(epoch) for epoch in args.epoch_offsets]
                    except ValueError as error:
                        raise ValueError('requested epoch absent from public template bank') from error
                    receiver = receiver_factory(manifest, templates[indexes], args.epoch_offsets,
                        arrival_seconds=tuple(args.arrivals), frequencies_hz=tuple(args.frequencies),
                        clock_ppm=tuple(args.clock_ppm), block_seconds=args.block_seconds,
                        coherence_seconds=tuple(args.coherences), pilot_use=args.pilot_use,
                        device=args.device, precision=args.precision, valid=validity,
                        memory_limit_bytes=args.memory_limit_bytes)
                    timings['preparation_seconds'] = time.perf_counter() - mark
                    procedure_id = calibrated_procedure_identity(receiver,
                        geometry=expected_geometry, configuration=configuration,
                        template_sha256=template_receipt['template_sha256'], sources=protocol['source_hashes'])
                    condition_id = identity(dict(procedure_id=procedure_id, public_geometry=expected_geometry))
                    write_json(args.output / 'prepared.json', dict(procedure_id=procedure_id,
                        condition_id=condition_id, **template_receipt,
                        preparation_seconds=timings['preparation_seconds'],
                        public_geometry=expected_geometry,
                        identity_schema='triad-dynamic-calibrated-procedure-v2',
                        receiver_procedure_id=receiver.procedure_id,
                        receiver_details=_jsonable(receiver.work())))
                mark = time.perf_counter()
                if not np.array_equal(validity.astype(bool), receiver.valid):
                    raise ValueError('public validity mask changed after preparation')
                result = receiver.score(raw)
                elapsed = time.perf_counter() - mark
                timings['receiver_batch_seconds'] += elapsed
                result = _jsonable(result)
                arm_rows = paired_arms(result['hypothesis_log_evidence'], args.coherences)
                if len(arm_rows) != len(exported):
                    raise ValueError('receiver returned the wrong trial count')
                batch_receipt = dict(trials=current, wall_seconds=elapsed,
                    receiver_timing={name: result[name] for name in (
                        'correlation_seconds', 'state_seconds', 'preparation_seconds') if name in result},
                    work=result.get('work', {}))
                write_json(args.output / f'{phase}-{first:04d}-batch.json', batch_receipt)
                for index, ((trial, folder, command, export_wall), item, arm_row) in enumerate(zip(exported, loaded, arm_rows)):
                    m, _, _, receipt = item
                    row = dict(trial=trial, procedure_id=procedure_id, condition_id=condition_id,
                               **arm_row, **receipt, export_command=command, export_wall_seconds=export_wall,
                               model_bit_scores=np.asarray(result['model_bit_scores'])[:, index, :].tolist(),
                               hypothesis_log_evidence=np.asarray(result['hypothesis_log_evidence'])[:, index, :].tolist(),
                               batch_receipt=f'{phase}-{first:04d}-batch.json')
                    if phase == 'calibration':
                        calibration.append(row)
                    else:
                        row['decisions'] = {arm: decide(row, gate, arm) for arm in ARMS}
                    row['evaluator'] = evaluator_metrics(folder, m)
                    if row['evaluator']['source_bit'] != trial['bit']:
                        raise ValueError('native emitted bit differs from the frozen scheduled source bit')
                    if args.acquisition == 'conditioned' and row['evaluator']['arrival_seconds'] != 0.:
                        raise ValueError('conditioned native capture did not use the declared zero arrival')
                    row['iq_retained'] = not args.discard_iq
                    write_json(folder / 'result.json', row)
                    with (args.output / 'trials.jsonl').open('a') as stream:
                        stream.write(json.dumps(row, allow_nan=False) + '\n')
                    rows.append(row)
                    print(json.dumps(dict(trial=trial['id'], decisions=row.get('decisions'),
                                          export_seconds=export_wall)), flush=True)
                    if args.discard_iq:
                        (folder / m['observation']['file']).unlink()
                        (folder / m['validity']['file']).unlink()
                # Memmaps are scoped to this batch. No retained sample views
                # are passed to admission or evaluator logic.
                del loaded, raw
            if phase == 'calibration':
                gate = freeze_gate(calibration, procedure_id, condition_id, args.alpha)
                write_json(args.output / 'gate.json', gate)
        if source_hashes() != protocol['source_hashes'] or digest(exporter) != protocol['exporter_sha256']:
            raise ValueError('source or native binary changed during the frozen campaign')
        timings['campaign_wall_seconds'] = time.perf_counter() - started
        timings['scope'] = ('Campaign wall includes native export/startup, one preparation, reading/hashing, '
            'receiver work, evaluator diagnostics and result writes; excludes Python import before main. '
            'Native sums may exceed stage wall with concurrent exports. Batch score time is shared across arms.')
        summary = dict(schema='triad-dynamic-campaign-summary-v1', completed_trials=len(rows),
            procedure_id=procedure_id, condition_id=condition_id, outcomes={arm: outcomes(rows, arm) for arm in ARMS},
            paired=paired_outcomes(rows), timing=timings, protocol_sha256=digest(args.output / 'protocol.json'),
            gate_sha256=digest(args.output / 'gate.json'), limitations=[
                protocol['acquisition_scope'], 'Native complete-waveform stochastic channel; receiver uses an approximate block model.',
                'Empirical per-capture diagnostics do not qualify operational daily false admissions.',
                'No independent-trial claim across paired receiver arms or reused configurations.',
                'Helper-present received energy includes pilots/auxiliary; no received DATA energy is inferred from it.',
                'Single campaign wall and batch timers are measurements, not confidence-qualified performance forecasts.'])
        write_json(args.output / 'summary.json', summary)
        return summary
    except Exception as error:
        write_json(args.output / 'failure.json', dict(current_batch=current, completed_trials=len(rows),
            error_type=type(error).__name__, error=str(error), wall_seconds=time.perf_counter() - started,
            automatic_retry=False))
        raise


if __name__ == '__main__':
    main()
