#!/usr/bin/env python3
"""Occasional independent native qualification of the isolated weak receiver.

This is not a routine regression prerequisite. First freeze a protocol with
``--prepare-protocol``, then execute it with ``--run``. No receiver result is
used to choose seeds, arrivals, epochs, channel laws or an admission threshold.
Scores are written exclusively before the evaluator truth file is opened.
The conditional Gaussian admission construction is not a physical-null claim.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor
import copy
import hashlib
import json
import math
import multiprocessing
import os
from pathlib import Path
import platform
import random
import resource
import subprocess
import sys
import time
import traceback


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT/'local-evidence/triad-weak-stream/independent'
SOURCE_NAMES = (
    'triad_weak_qualify.py', 'triad_weak_receiver.py', 'triad_weak_helper.py',
    'triad_segmented_fft.py', 'triad_operational_admission.py',
    'triad_realtime_receiver.py', 'triad_realtime_fft.py',
    'triad_dynamic_joint.py', 'triad_dynamic_frequency.py',
    'triad_dynamic_variants.py', 'triad_dynamic_scan.py',
    'triad_gaussian_prepared.py', 'triad_gaussian_state.py',
    'triad_helper_receiver.py', 'triad_dynamic_campaign.py',
    'triad_helper_statistics.py')
REGIMES = (
    dict(name='persistent', random_doppler_width_hz=0., delay_excursion_ms=0., delay_knot_seconds=20.),
    dict(name='renewing', random_doppler_width_hz=.5, delay_excursion_ms=0., delay_knot_seconds=20.),
    dict(name='moving', random_doppler_width_hz=.5, delay_excursion_ms=.0002, delay_knot_seconds=20.),
)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True,
        separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def write_json(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def schedule(seed_base=2026111700):
    """Independent public input draws within fixed bit/channel strata.

    Arrivals and key epochs are iid draws from the stated finite test mixture;
    they are not tuned to cover an observed successful region. The finite
    mixture is a diagnostic population, not arbitrary operating conditions.
    """
    if isinstance(seed_base, bool) or not isinstance(seed_base, int) or seed_base < 0 or seed_base+39 >= 2**64:
        raise ValueError('seed base must support39 uint64 trial seeds')
    result = []
    for regime in REGIMES:
        for phase, count in (('signal', 6), ('helper-null', 6), ('noise', 1)):
            for index in range(count):
                seed = seed_base+len(result)
                draws = random.Random(seed ^ 0x5055424c49434d49)
                result.append(dict(id=f"{regime['name']}-{phase}-{index:02d}",
                    regime=regime['name'], phase=phase, seed=seed, bit=index % 2,
                    arrival_seconds=draws.choice(( -6.9, 0., 6.9)),
                    epoch_offset_seconds=draws.choice((-3, 0, 2))))
    return result


def native_arguments(base, regime, *, row=None):
    args = ['--case', 'hf', '--seconds', str(base['format']['requested_bit_seconds']),
        '--sample-rate', str(base['sample_rate_hz']), '--key-seed', str(base['key_seed']),
        '--base-oscillator', 'crystal', '--shift-oscillator', 'ic-7100',
        '--channel-evolution', 'stochastic', '--random-doppler-width-hz', str(regime['random_doppler_width_hz']),
        '--delay-excursion-ms', str(regime['delay_excursion_ms']),
        '--delay-knot-seconds', str(regime['delay_knot_seconds']),
        '--timing-policy', 'broad', '--cn0', '8', '--data-if', '1725',
        '--rf-hz', '30000000', '--spread-preset', 'hf-worst', '--margin', '3',
        '--selective-perturbation', 'each-65', '--data-power-w', '4', '--helper-power-w', '40',
        '--rate', '36', '--added-symbols', '10', '--fhss', 'off', '--shaping', 'auto']
    if row is None:
        return args+['--epoch', str(base['public_epoch_seconds'])]
    return args+['--epoch', str(base['public_epoch_seconds']+row['epoch_offset_seconds']),
        '--seed', str(row['seed']), '--bit', str(row['bit']),
        '--arrival-seconds', str(row['arrival_seconds']),
        '--data', 'on' if row['phase']=='signal' else 'off',
        '--helper', 'off' if row['phase']=='noise' else 'on']


def sources():
    return {str(Path('tools')/name): digest(ROOT/'tools'/name) for name in SOURCE_NAMES}


def prepare(output, base_directory, exporter, seed_base):
    output, base_directory, exporter = map(lambda p: Path(p).resolve(), (output, base_directory, exporter))
    output.mkdir(parents=True, exist_ok=True)
    protocol_path = output/'protocol.json'
    if protocol_path.exists():
        raise ValueError('protocol already exists; never overwrite a frozen study')
    base = json.loads((base_directory/'manifest.json').read_text())
    if base['sample_rate_hz'] != 6000 or base['public_epoch_seconds'] != 1793850000 or base['key_seed'] != 2417:
        raise ValueError('study requires the declared base public key/epoch/6000-Hz geometry')
    if base['calendar']['arrival_prior_seconds'] != [-7.035, 7.035] or not base.get('helper_enabled'):
        raise ValueError('study requires full broad timing prior and declared Helper')
    if base['templates']['epoch_offsets_seconds'] != list(range(-6, 7)):
        raise ValueError('complete13-epoch bank required')
    plans = {}
    # Plan-only output is public deterministic geometry, not sampled trial
    # data. Freeze it before every sampled generation and DATA score.
    for regime in REGIMES:
        argv = [str(exporter), *native_arguments(base, regime), '--plan-only']
        completed = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, check=True)
        plan = json.loads(completed.stdout)
        plans[regime['name']] = dict(argv=argv, public_plan=plan)
    template_path = base_directory/base['templates']['file']
    protocol = dict(schema='triad-weak-independent-protocol-v1', created_unix_seconds=time.time(),
        source_hashes=sources(), exporter=dict(path=str(exporter), sha256=digest(exporter),
            provenance_scope='Reused binary identity; this harness does not assert a fresh source/binary certification.'),
        base=dict(directory=str(base_directory), manifest_sha256=digest(base_directory/'manifest.json'),
            template_sha256=digest(template_path), public_manifest=base),
        public_plans=plans, regimes=REGIMES, rows=schedule(seed_base),
        receiver=dict(precision='fp32', screen_mode='auto', block_seconds=.5,
            noise_inflation=1., top_per_bit=2, coherences=['0.5', '2.0', 'inf'],
            memory_limit_bytes=2*1024**3, threads_per_process=2),
        trials=39, data_cn0_db_hz=8., helper_cn0_db_hz=18.,
        population='Each channel-law stratum: independent native seeds; balanced fixed signal bits; independent uniform arrival{-6.9,0,6.9} and epoch{-3,0,2} draws. Not all operating conditions.',
        receiver_input_policy='Base public epoch/key/templates/Helper-enabled/control unchanged. Only declared public channel-law metadata and common capture length/tail may vary. Noise variance=Fs/10^(8/10). On/off controls, realized epoch/arrival/bit and evaluator truth are never passed to receiver construction.',
        score_read_order='Exclusive result JSON write and source verification before opening evaluator truth.',
        admission_scope='Fixed original-bank conditional Gaussian e-value. Physical Helper-present and non-Gaussian null remain unqualified regardless of small empirical null counts.',
        runtime_scope='Per trial: generation wall/child CPU, public input loading, receiver construction, scoring, finalization and total worker process CPU/wall separately. Campaign wall includes process startup and shared scheduling; no multiplication of wall time by thread count.',
        environment=dict(python=sys.version, platform=platform.platform(), machine=platform.machine(),
            cpu_model=next((s.split(':',1)[1].strip() for s in Path('/proc/cpuinfo').read_text().splitlines() if s.startswith('model name')), 'unknown')))
    protocol['protocol_id'] = identity(protocol)
    write_json(protocol_path, protocol)
    return protocol


def verify(protocol, *, full=False):
    given = dict(protocol); saved = given.pop('protocol_id')
    if identity(given) != saved:
        raise ValueError('frozen protocol content changed')
    if sources() != protocol['source_hashes']:
        raise ValueError('receiver or qualification source changed after protocol freeze')
    if digest(protocol['exporter']['path']) != protocol['exporter']['sha256']:
        raise ValueError('native exporter changed after protocol freeze')
    if full:
        base = protocol['base']; directory = Path(base['directory'])
        if digest(directory/'manifest.json') != base['manifest_sha256'] or digest(directory/base['public_manifest']['templates']['file']) != base['template_sha256']:
            raise ValueError('base manifest or templates changed after protocol freeze')


def receiver_manifest(base, capture, regime):
    """Do not import actual source epoch or signal/null control into the RX."""
    if capture['sample_rate_hz'] != base['sample_rate_hz'] or capture['format'] != base['format']:
        raise ValueError('native capture changed public source geometry')
    for field in ('data_sample_offset', 'data_calendar_start_seconds', 'helper_start_seconds',
                  'helper_end_seconds', 'capture_lead_seconds', 'capture_trail_seconds', 'arrival_prior_seconds'):
        if capture['calendar'][field] != base['calendar'][field]:
            raise ValueError('native capture changed common calendar '+field)
    ch = capture['channel']
    for field in ('rf_hz', 'nominal_conversion_if_hz', 'base_oscillator', 'shift_oscillator',
                  'spread_margin', 'spread_delay_ms', 'spread_doppler_hz',
                  'selective_slow_depth', 'selective_fast_depth', 'selective_modulation_rate_hz',
                  'deterministic_cfo_bound_hz', 'sample_clock_pair_ppm'):
        if ch[field] != base['channel'][field]:
            raise ValueError('native capture changed undeclared physical setting '+field)
    if (ch['random_doppler_width_hz'] != regime['random_doppler_width_hz'] or
        not math.isclose(ch['delay_excursion_seconds'], regime['delay_excursion_ms']*.001, abs_tol=1e-18) or
        ch['delay_knot_seconds'] != regime['delay_knot_seconds']):
        raise ValueError('native channel-law metadata differs from frozen controls')
    m = copy.deepcopy(base)
    m['channel'] = copy.deepcopy(ch)
    for field in ('capture_samples', 'fixed_capture_tail_bound_seconds'):
        m['calendar'][field] = capture['calendar'][field]
    m['power'].update(data_cn0_db_hz=8., effective_helper_cn0_db_hz=18., cn0_override=True,
        complex_noise_variance=m['sample_rate_hz']/10**.8)
    return m


def _cpu(usage):
    return usage.ru_utime+usage.ru_stime


def trial(payload):
    protocol, row, output = payload
    output = Path(output); trial_dir = output/'trials'/row['id']
    trial_dir.mkdir(parents=True, exist_ok=False)
    started_c, started_w = time.process_time(), time.perf_counter()
    receipt = dict(id=row['id'], protocol_id=protocol['protocol_id'], source_hashes=protocol['source_hashes'], stages={})
    try:
        verify(protocol)
        for name in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
            os.environ[name] = str(protocol['receiver']['threads_per_process'])
        # These imports happen inside each process, after thread configuration.
        import numpy as np
        import torch
        from triad_dynamic_joint import load_public
        from triad_weak_receiver import WeakReceiver
        torch.set_num_threads(protocol['receiver']['threads_per_process'])
        try:
            torch.set_num_interop_threads(1)
        except RuntimeError:
            pass  # A reused pool worker already fixed this before its first score.
        torch.set_float32_matmul_precision('highest')
        base = protocol['base']['public_manifest']
        regime = next(r for r in protocol['regimes'] if r['name']==row['regime'])
        capture_dir = trial_dir/'capture'
        argv = [protocol['exporter']['path'], *native_arguments(base, regime, row=row), '--output', str(capture_dir)]
        w, u = time.perf_counter(), resource.getrusage(resource.RUSAGE_CHILDREN)
        with (trial_dir/'generation.log').open('x') as log:
            completed = subprocess.run(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=False)
        receipt['stages']['generation'] = dict(wall_seconds=time.perf_counter()-w,
            cpu_seconds=_cpu(resource.getrusage(resource.RUSAGE_CHILDREN))-_cpu(u),
            exit_code=completed.returncode, argv=argv)
        if completed.returncode:
            raise RuntimeError('native generation failed; retained generation.log')
        c, w = time.process_time(), time.perf_counter()
        cm, templates, epochs, raw, valid = load_public(capture_dir, protocol['base']['directory'])
        manifest = receiver_manifest(base, cm, regime)
        receipt['stages']['public_input_loading'] = dict(cpu_seconds=time.process_time()-c, wall_seconds=time.perf_counter()-w)
        receipt['input_hashes'] = {name:digest(capture_dir/name) for name in ('manifest.json', cm['observation']['file'], cm['validity']['file'])}
        receipt['receiver_public_manifest'] = manifest
        c, w = time.process_time(), time.perf_counter()
        config = {k:v for k,v in protocol['receiver'].items() if k not in ('threads_per_process', 'coherences')}
        config['coherences'] = tuple(map(float, protocol['receiver']['coherences']))
        rx = WeakReceiver(manifest, templates, epochs, **config)
        receipt['stages']['construction'] = dict(cpu_seconds=time.process_time()-c, wall_seconds=time.perf_counter()-w)
        result = rx.score(raw, valid)
        receipt['stages']['receiver_scoring'] = result['timing']['total']
        c, w = time.process_time(), time.perf_counter()
        verify(protocol)
        write_json(trial_dir/'receiver-result.json', result)
        receipt['receiver_result_sha256'] = digest(trial_dir/'receiver-result.json')
        # Evaluator data is deliberately first opened AFTER immutable score.
        truth_path = capture_dir/cm['evaluator_truth_file']
        truth = json.loads(truth_path.read_text())
        passed = bool(result.get('operational', {}).get('passes_conditional_bound', False))
        bit = result.get('bit'); source_bit = truth['source_bit']
        outcome = dict(id=row['id'], regime=row['regime'], phase=row['phase'],
            source_bit=source_bit, returned_bit=bit, conditional_admission=passed,
            correct_admitted=bool(row['phase']=='signal' and passed and bit==source_bit),
            wrong_admitted=bool(row['phase']=='signal' and passed and bit!=source_bit),
            missed=bool(row['phase']=='signal' and not passed),
            raw_bit_error=bool(row['phase']=='signal' and bit is not None and bit!=source_bit),
            unresolved=result['status']!='scored', null_admitted=bool(row['phase']!='signal' and passed),
            reason=result['reason'], score=result.get('score'), margin=result.get('margin'),
            physical_null_qualified=False, evaluator_truth_sha256=digest(truth_path),
            evaluator=dict(arrival_seconds=truth['arrival_seconds'], source_epoch_offset_seconds=cm['public_epoch_seconds']-base['public_epoch_seconds'],
                actual_to_public_template_amplitude_ratio=cm['power']['template_data_gain']/base['power']['template_data_gain'],
                channel_report=truth.get('channel_report')),
            receive_observation_seconds=len(raw)/manifest['sample_rate_hz'])
        write_json(trial_dir/'outcome.json', outcome)
        receipt['stages']['finalization'] = dict(cpu_seconds=time.process_time()-c, wall_seconds=time.perf_counter()-w)
        receipt.update(status='completed', worker_cpu_seconds=time.process_time()-started_c,
            worker_wall_seconds=time.perf_counter()-started_w)
        write_json(trial_dir/'receipt.json', receipt)
        return outcome
    except Exception as error:
        receipt.update(status='error', error_type=type(error).__name__, error=str(error), traceback=traceback.format_exc(),
            worker_cpu_seconds=time.process_time()-started_c, worker_wall_seconds=time.perf_counter()-started_w)
        write_json(trial_dir/'error.json', receipt)
        return dict(id=row['id'], regime=row['regime'], phase=row['phase'], status='error', error=str(error))


def summarize(outcomes, protocol, wall_seconds, workers, output=None):
    from triad_helper_statistics import balanced_signal_statistics, binomial_interval
    groups = {}
    for regime in protocol['regimes']:
        rows = [r for r in outcomes if r['regime']==regime['name']]
        signals = [r for r in rows if r['phase']=='signal' and r.get('status')!='error']
        per_bit = {b:[r for r in signals if r['source_bit']==b] for b in (0,1)}
        counts = {b:{name:sum(r[key] for r in values) for name,key in
            (('correct_admitted','correct_admitted'), ('wrong_admitted','wrong_admitted'),
             ('missed','missed'), ('raw_bit_errors','raw_bit_error'))} for b,values in per_bit.items()}
        nulls = {}
        for phase in ('helper-null','noise'):
            values = [r for r in rows if r['phase']==phase and r.get('status')!='error']
            nulls[phase] = dict(false_admissions=binomial_interval(sum(r['null_admitted'] for r in values),len(values)),
                unresolved=sum(r['unresolved'] for r in values))
        groups[regime['name']] = dict(signal=balanced_signal_statistics(counts,{b:len(v) for b,v in per_bit.items()}),
            unresolved_signals=sum(r['unresolved'] for r in signals), nulls=nulls,
            errors=[r for r in rows if r.get('status')=='error'])
    receipts=[]
    if output is not None:
        for row in outcomes:
            directory=Path(output)/'trials'/row['id']
            path=directory/('error.json' if row.get('status')=='error' else 'receipt.json')
            receipts.append(json.loads(path.read_text()))
    costs=dict(worker_cpu_seconds=sum(r['worker_cpu_seconds'] for r in receipts),
        generation_child_cpu_seconds=sum(r['stages'].get('generation',{}).get('cpu_seconds',0.) for r in receipts),
        receiver_construction_cpu_seconds=sum(r['stages'].get('construction',{}).get('cpu_seconds',0.) for r in receipts),
        receiver_scoring_cpu_seconds=sum(r['stages'].get('receiver_scoring',{}).get('cpu_seconds',0.) for r in receipts),
        scope='Worker CPU includes imports, loading, hashing, preparation, receiver and finalization; generation CPU is separate child work. Concurrent wall times must not be summed as campaign latency.')
    return dict(schema='triad-weak-independent-summary-v1', protocol_id=protocol['protocol_id'],
        completed=len(outcomes), expected=protocol['trials'], workers=workers, campaign_wall_seconds=wall_seconds,
        costs=costs, groups=groups, outcomes=outcomes, physical_null_qualified=False,
        uncertainty_scope='Exact within-stratum intervals and Bonferroni balanced-bit bounds under the frozen finite input mixture. Not simultaneous across all groups/categories, not99% reliability certification, and not an empirical10^-4/day demonstration. Setup errors are reported separately and preclude complete qualification.')


def run(output, workers):
    output = Path(output).resolve()
    protocol = json.loads((output/'protocol.json').read_text())
    verify(protocol, full=True)
    if workers not in (1,2):
        raise ValueError('one or two independent worker processes required')
    if (output/'trials').exists() or (output/'summary.json').exists():
        raise ValueError('existing trial results: never automatically rerun or overwrite evidence')
    start = time.perf_counter()
    context = multiprocessing.get_context('spawn')
    with ProcessPoolExecutor(max_workers=workers, mp_context=context) as pool:
        outcomes = list(pool.map(trial, [(protocol,r,str(output)) for r in protocol['rows']]))
    verify(protocol, full=True)
    result = summarize(outcomes, protocol, time.perf_counter()-start, workers, output)
    write_json(output/'summary.json', result)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    action=parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--prepare-protocol', action='store_true')
    action.add_argument('--run', action='store_true')
    action.add_argument('--check-contracts', action='store_true', help='cheap pure-Python schedule/public-input checks, no receiver or capture')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--base', type=Path, default=ROOT/'local-evidence/triad-cpu-ceiling/capture')
    parser.add_argument('--exporter', type=Path, default=ROOT/'build/dev-cli/triad_helper_export')
    parser.add_argument('--seed-base', type=int, default=2026111700)
    parser.add_argument('--workers', type=int, choices=(1,2), default=2)
    args=parser.parse_args()
    if args.check_contracts:
        rows=schedule();assert len(rows)==39 and len({r['seed'] for r in rows})==39
        assert all(sum(r['phase']=='signal' and r['bit']==b and r['regime']==g['name'] for r in rows)==3 for b in (0,1) for g in REGIMES)
        base=json.loads((args.base/'manifest.json').read_text())
        capture=copy.deepcopy(base);capture['data_enabled']=False;capture['helper_enabled']=False;capture['public_epoch_seconds']+=2
        capture['channel']['delay_knot_seconds']=20.
        m=receiver_manifest(base,capture,REGIMES[0]);assert m['public_epoch_seconds']==base['public_epoch_seconds'] and m['helper_enabled']
        assert m['data_enabled']==base['data_enabled'] and math.isclose(m['power']['complex_noise_variance'],6000/10**.8)
        capture['format']['added_symbols']=1
        try:receiver_manifest(base,capture,REGIMES[0])
        except ValueError:pass
        else:raise AssertionError('changed source geometry was accepted')
        print('schedule, balanced strata, public-epoch/control isolation, noise units and geometry guards passed; no sampled or timing work run')
        return
    result=prepare(args.output,args.base,args.exporter,args.seed_base) if args.prepare_protocol else run(args.output,args.workers)
    print(json.dumps({k:result[k] for k in ('schema','protocol_id')},sort_keys=True))


if __name__=='__main__':
    main()
