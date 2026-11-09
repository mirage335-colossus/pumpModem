#!/usr/bin/env python3
"""Compare canonical Robust fine work on host CPU and explicit FP64 CUDA."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import numpy as np

from triad_abbreviated import environment, source_identity
from triad_case_capacity import PreparedCapacityCase, measurement_policy, public_work_coordinates
from triad_case_metrics import machine_resources
from triad_case_suite import native_plan, select_cases
from triad_cases import DEFAULT_CASES, ROOT, load_cases
from triad_cpu import configure_numerical_threads, resolve_cpu_budget
from triad_receiver import CompleteBitReceiver, _prepare_cuda_linalg


SCHEMA = 'triad-gpu-workload-v1'
SCORE_ATOL, SCORE_RTOL = 1e-7, 1e-10
STRATA = ('nominal_continuation', 'maximum_state_transition', 'interior_erasure')


def _source_hashes():
    return dict(source_identity(), **{'build_capacity.py': hashlib.sha256(
        Path(__file__).with_name('build_capacity.py').read_bytes()).hexdigest()})


def _write_probe_report(path, report):
    # Exclusive creation in the destination directory makes replacement
    # atomic without opening an existing sidecar or following a symlink.
    descriptor, temporary = tempfile.mkstemp(dir=path.parent, prefix=f'.{path.name}.')
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write('\n')
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def _phase_timing(began, represented, **phase):
    return dict(phase, represented_baseband_seconds=represented,
        ratio_processing_seconds_per_baseband_second=(time.perf_counter()-began)/represented)


def _sample_record(result, cache, *, states=None):
    work = {key:result[key] for key in ('consumed_samples', 'processed_rows', 'blocks',
        'segments', 'work_counts', 'additional_discarded_rows', 'requested_invalid_rows')}
    work['lanes'] = {name: dict(work_counts=lane['work_counts'],
        state_work_regime=lane['state_work_regime'],
        pruning_events=lane['timing']['pruning_events'], states_dropped=lane['timing']['states_dropped'])
        for name, lane in result['lanes'].items()}
    work['native_cache'] = cache
    if states is not None:
        work['retained_states'] = states
    scores = {name:lane['bit_scores'] for name,lane in result['lanes'].items()}
    if (set(scores) != {'adaptive', 'initial_affine'} or
            not all(np.asarray(x).shape == (2,) and np.isfinite(x).all() for x in scores.values())):
        raise ValueError('nonfinite or malformed lane scores')
    return dict(work=work, lane_scores=scores)


def _complete_record(result, endpoint):
    templates = result['native_template_work']
    if not result['complete_observation'] or not templates['representative_work']:
        raise ValueError('fine probe did not complete representative template work')
    sample = _sample_record(result, templates['cache'])
    sample['work'].update(template_calls=templates['calls'],
        template_elements=templates['returned_elements'],
        probe_endpoint=dict(status=endpoint['status'], classification=endpoint['slot_classification'],
            bit=endpoint['decision_bit'], quiet_projection_modes=endpoint['quiet']['observed_projection_modes']))
    return sample


def _continuation_sample(prepared, receiver, coordinates, synchronize):
    """One parity observation using the capacity suite's declared prefix and checkpoints."""
    policy = measurement_policy(1, 1.)['nominal_continuation']
    prefix = policy['prefix_baseband_seconds']
    ends = [prefix, *(prefix + value for value in policy['checkpoints_baseband_seconds'])]
    fs = prepared.fs
    samples = round(ends[-1]*fs)
    if samples > prepared.samples:
        raise ValueError('public observation cannot contain the continued fine probe')
    center = prepared.probe_origin('active', coordinates, required_samples=samples)
    origin = min(max(0, center-samples//2), prepared.samples-samples)
    valid = prepared.valid[origin:origin+samples]
    if not valid.all():
        raise ValueError('continued probe contains invalid input')
    address = prepared.calendar['plans'][0]
    synchronize()
    began = time.perf_counter()
    reset = prepared.basis_source.set_basis_address(address['pattern_epoch'], address['symbol_ordinal'])
    if (reset.get('template_cache_cleared') is not True or
            reset.get('basis_epoch') != address['pattern_epoch'] or
            reset.get('basis_symbol_ordinal') != address['symbol_ordinal']):
        raise ValueError('native basis address or cache reset did not match')
    fine = receiver.create_fine_session(coordinates, samples=samples, physical_origin=origin)
    start = 0
    records, phase_timing = [], []
    try:
        for end in ends:
            stop = round(end*fs)
            for at in range(start, stop, receiver.policy['chunk_samples']):
                hi = min(stop, at+receiver.policy['chunk_samples'])
                fine.push(at, prepared.raw[origin+at:origin+hi], valid[at:hi])
            synchronize()
            phase_timing.append(_phase_timing(began, (stop-start)/fs,
                phase='prefix' if start == 0 else 'continuation', probe_end_seconds=end))
            result = fine.receiver.progress()
            if result['processed_rows'] != stop or fine.template_energy <= 0:
                raise ValueError('continued checkpoint did not execute its declared work')
            states = {name:[[state.group,state.delay_index,state.rate_index] for state in lane.timing.states]
                for name,lane in (('adaptive',fine.receiver.adaptive),('initial_affine',fine.receiver.nominal))}
            records.append(_sample_record(result, fine.provider.cache_report(), states=states))
            start = stop
            synchronize()
            began = time.perf_counter()
        result = fine.finish()
        endpoint = receiver.classify(result, coordinates, all_input_valid=True)
    finally:
        fine.close()
    synchronize()
    phase_timing.append(_phase_timing(began, prepared.slot_seconds, phase='finish_classify_close',
        scope='Short endpoint work normalized by public slot stride.'))
    final = _complete_record(result, endpoint)
    final['work']['physical_first_sample'] = origin
    final['work']['basis_address'] = [address['pattern_epoch'], address['symbol_ordinal']]
    first_cache, last_cache = (records[i]['work']['native_cache'] for i in (0,-1))
    if last_cache.get('native_preparation_calls', 0) <= first_cache.get('native_preparation_calls', 0):
        raise ValueError('continued probe did not observe a new native cache fill')
    return dict(records=[*records, final], phase_timing=phase_timing)


def _branch_sample(prepared, receiver, coordinates, synchronize, *, cap):
    if cap:
        coordinates = public_work_coordinates(receiver, coordinates)
    synchronize()
    began = time.perf_counter()
    probe = prepared.fine_probe(receiver, coordinates, units=1 if cap else 2,
        work_probe=cap, interior_erasure=not cap)
    synchronize()
    timing = _phase_timing(began, probe['represented_baseband_seconds'])
    result = probe['result']
    if not cap and (result['requested_invalid_rows'] != 2 or not result['additional_discarded_rows']):
        raise ValueError('erasure probe did not expand a partially invalid DFT block')
    record = _complete_record(result, probe['endpoint'])
    record['work'].update(physical_first_sample=probe['public_first_sample'],
        coordinates=result['coordinates'])
    if cap:
        record['work']['capacity_transition_coverage'] = result['capacity_transition_coverage']
    return dict(records=[record], **timing, scope='Measured short work on the represented interval.')


def compare_work(cpu, cuda):
    """Separate work equality and likelihood closeness from timing."""
    if not cpu or len(cpu) != len(cuda):
        return dict(work_match=False, scores_match=False)
    return dict(work_match=all(a['work'] == b['work'] for a,b in zip(cpu,cuda)),
        scores_match=all(a['lane_scores'].keys() == b['lane_scores'].keys() and
            all(np.allclose(a['lane_scores'][name], b['lane_scores'][name],
                rtol=SCORE_RTOL, atol=SCORE_ATOL) for name in a['lane_scores']) for a,b in zip(cpu,cuda)))


def _case_probe(prepared, workers, devices, synchronize, result):
    def receiver(device):
        return CompleteBitReceiver(prepared.manifest, prepared.screen, prepared.basis_source,
            workers=workers, memory_limit_bytes=prepared.memory_limit_bytes, fine_device=device)

    with receiver('cpu') as instance:
        acquired = prepared.acquisition(instance)['result']
        session = acquired.pop('coarse_session', None)
        if session is not None:
            session.close()
    if acquired['status'] != 'acquired' or not acquired['coordinates']:
        raise ValueError('complete CPU acquisition did not produce fine coordinates')
    coordinates = tuple(acquired['coordinates'])
    result.update(coordinates=coordinates, public_geometry=prepared.public_geometry,
        input_sha256=hashlib.sha256(memoryview(prepared.raw).cast('B')).hexdigest(),
        validity_sha256=hashlib.sha256(memoryview(prepared.valid).cast('B')).hexdigest(),
        preparation=dict(receiver_seconds=prepared.receiver_preparation_seconds,
            simulator_seconds=prepared.simulator_generation_seconds), devices={})
    for device in devices:
        with receiver(device) as instance:
            strata = {}
            result['devices'][device] = dict(procedure_id=instance.procedure_id, strata=strata)
            strata['nominal_continuation'] = _continuation_sample(prepared, instance, coordinates, synchronize[device])
            strata['maximum_state_transition'] = _branch_sample(prepared, instance, coordinates,
                synchronize[device], cap=True)
            strata['interior_erasure'] = _branch_sample(prepared, instance, coordinates,
                synchronize[device], cap=False)
    if 'cuda' in result['devices']:
        cpu, cuda = (result['devices'][device]['strata'] for device in ('cpu','cuda'))
        result['parity'] = {name:compare_work(cpu[name]['records'],cuda[name]['records']) for name in STRATA}


def run_probe(*, case_list, case_indices, executable, workers, device, output):
    import torch

    if device not in ('cpu','cuda'):
        raise ValueError('device must be cpu or cuda')
    if device == 'cuda' and not torch.cuda.is_available():
        raise ValueError('CUDA requested but unavailable')
    parsed = load_cases(case_list)
    selected = select_cases(parsed, case_indices)
    if not 1 <= len(selected) <= 3:
        raise ValueError('declare one to three representative manual rows')
    machine = machine_resources()
    if machine['cpu'] is None or machine['memory_budget_bytes'] is None:
        raise ValueError('visible CPU and host memory bounds required')
    allocation = resolve_cpu_budget(jobs=1,workers=workers,threads=1,capacity=machine['cpu'])
    if allocation['explicit_overcommit']:
        raise ValueError('worker count exceeds the visible CPU allocation')
    runtime_limits = configure_numerical_threads(allocation)
    devices = ('cpu','cuda') if device == 'cuda' else ('cpu',)
    synchronize = dict(cpu=lambda:None,cuda=torch.cuda.synchronize)
    memory_limit = machine['memory_budget_bytes']
    cuda_linalg_preflight = None
    if device == 'cuda':
        cuda_linalg_preflight = _prepare_cuda_linalg(torch.cuda.current_device())
        memory_limit = min(memory_limit, torch.cuda.mem_get_info()[0]//2)
        torch.cuda.reset_peak_memory_stats()
    sources = _source_hashes()
    exporter_sha256 = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
    runtime = environment(device)
    runtime.pop('host', None)
    runtime_limits['pools'] = [{key:value for key,value in pool.items() if key != 'filepath'}
        for pool in runtime_limits['pools']]
    report = dict(schema=SCHEMA, status='running', cases=[], device=device, workers=workers,
        input=dict(source_sha256=parsed['source_sha256'], current_list_entries=len(parsed['cases'])),
        sources=sources, native_binary_sha256=exporter_sha256, runtime=runtime,
        runtime_limits=runtime_limits, memory_limit_bytes=memory_limit,
        cuda_linalg_preflight=cuda_linalg_preflight,
        local_cpu_service_established=False, complete_receiver_qualification=False,
        policy=dict(strata=list(STRATA), precision='fp64', score_atol=SCORE_ATOL, score_rtol=SCORE_RTOL,
            cases_maximum=3, pairs_per_case=1, order=list(devices),
            cuda_stream='The selected device default stream is shared by the controller and dedicated workers.',
            scope='Matched short fine checkpoints and work counters on one native capture per row. '
                  'Short endpoints may be incomplete or ambiguous. Full endpoint and local CPU '
                  'qualification require separate measurements.'))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.touch(exist_ok=False)
    _write_probe_report(output,report)
    for case in selected:
        row = dict(case=case)
        print(f"probing manual row {case['index']} with {workers} workers",flush=True)
        try:
            plan = native_plan(executable,case['native_args'])
            with PreparedCapacityCase(executable,case,plan,memory_limit_bytes=memory_limit) as prepared:
                _case_probe(prepared,workers,devices,synchronize,row)
        except (ValueError, RuntimeError, FloatingPointError, MemoryError, EOFError, OSError,
                subprocess.SubprocessError) as error:
            row['error'] = str(error)
        report['cases'].append(row)
        _write_probe_report(output,report)
    stable = (sources == _source_hashes() and
        hashlib.sha256(Path(executable).read_bytes()).hexdigest() == exporter_sha256 and
        hashlib.sha256(Path(case_list).read_bytes()).hexdigest() == parsed['source_sha256'])
    report['unchanged_at_completion'] = stable
    if device == 'cuda':
        report['cuda_peak_allocated_bytes'] = torch.cuda.max_memory_allocated()
    if not stable or any('error' in row for row in report['cases']):
        report['status'] = 'unresolved'
    elif device == 'cpu':
        report['status'] = 'cpu_reference_only'
    elif report['cuda_peak_allocated_bytes'] > 0 and all(
            all(check.values()) for row in report['cases'] for check in row['parity'].values()):
        report['status'] = 'short_fine_work_parity_observed'
    else:
        report['status'] = 'unresolved'
    _write_probe_report(output,report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,allow_abbrev=False)
    parser.add_argument('--case-list',type=Path,default=DEFAULT_CASES)
    parser.add_argument('--case-index',type=int,action='append',required=True)
    parser.add_argument('--exporter',type=Path,default=ROOT/'build/dev/triad_helper_export')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--workers',type=int,default=2)
    parser.add_argument('--device',choices=('cpu','cuda'),default='cuda')
    args = parser.parse_args(argv)
    try:
        result = run_probe(case_list=args.case_list,case_indices=args.case_index,
            executable=args.exporter,workers=args.workers,device=args.device,output=args.output)
    except (ValueError, RuntimeError, OSError) as error:
        parser.error(str(error))
    print(result['status'])
    return 2 if result['status'] == 'unresolved' else 0


if __name__ == '__main__':
    sys.exit(main())
