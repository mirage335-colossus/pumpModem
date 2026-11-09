#!/usr/bin/env python3
"""Measure bounded Robust work with resident CUDA bulk arithmetic."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
from statistics import fmean
import sys
import tempfile
import time

import numpy as np

from triad_abbreviated import environment
from triad_case_capacity import (PreparedCapacityCase, build_recurring_public_bank, measurement_policy,
                                measure_full_selection_metadata, noise_only_observation,
                                public_work_coordinates)
from triad_case_metrics import machine_resources
from triad_case_suite import measured_source_hashes, native_plan, select_cases
from triad_cases import DEFAULT_CASES, ROOT, load_cases
from triad_cpu import configure_numerical_threads, resolve_cpu_budget
from triad_receiver import CompleteBitReceiver
from triad_tracking_basis import mapped_kernel_path


SCHEMA = 'triad-gpu-workload-v2'
SCORE_ATOL, SCORE_RTOL = 2e-3, 2e-4
ADAPTIVE_COST_RTOL = .05
STRATA = ('nominal_continuation', 'maximum_state_transition', 'terminal',
          'interior_erasure', 'noise_only')
DISPATCH_FIELDS = ('requested_candidate_shards', 'maximum_hypothesis_tasks_per_block')


def _source_hashes(device):
    leaves = ['triad_gpu_workload.py']
    if device == 'cuda':
        leaves.extend(('triad_gpu_fine.py', 'triad_gpu_bank.py'))
    return {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
            for name in leaves}


def _cuda_jit_identity(torch):
    import triton

    try:
        output = subprocess.check_output(['nvidia-smi','--query-gpu=driver_version','--format=csv,noheader'],
                                         text=True,stderr=subprocess.DEVNULL,timeout=5)
        versions = {line.strip() for line in output.splitlines() if line.strip()}
        driver = next(iter(versions)) if len(versions)==1 else None
        if driver is not None and ('.' not in driver or not all(part.isdigit() for part in driver.split('.'))):
            driver = None
    except (OSError,subprocess.SubprocessError):
        driver = None
    return dict(triton_version=triton.__version__,driver_version=driver,
                compute_capability=list(torch.cuda.get_device_capability()))


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
    elapsed = time.perf_counter()-began
    return dict(phase, elapsed_seconds=elapsed, represented_baseband_seconds=represented,
        ratio_processing_seconds_per_baseband_second=elapsed/represented)


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


def _complete_record(result, endpoint, *, require_templates=True):
    templates = result['native_template_work']
    if not result['complete_observation'] or (require_templates and not templates['representative_work']):
        raise ValueError('fine probe did not complete representative template work')
    sample = _sample_record(result, templates['cache'])
    operational, quiet = endpoint['operational'], endpoint['quiet']
    data_margin = (operational['log_e_lower_bound']-operational['log_e_threshold']
                   if operational is not None else None)
    quiet_margin = (quiet['threshold']-quiet['projected_energy'] if quiet['threshold'] is not None else None)
    sample['work'].update(template_calls=templates['calls'],
        template_elements=templates['returned_elements'],
        probe_endpoint=dict(status=endpoint['status'], classification=endpoint['slot_classification'],
            bit=endpoint['decision_bit'], quiet_projection_modes=quiet['observed_projection_modes'],
            data_threshold_side=None if data_margin is None else data_margin >= 0,
            quiet_threshold_side=None if quiet_margin is None else quiet_margin >= 0))
    sample['diagnostic_threshold_margins'] = dict(data_log_e=data_margin, quiet_energy=quiet_margin)
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
            record = _sample_record(result, fine.provider.cache_report(), states=states)
            record['work'].update(template_calls=fine.template_calls,
                                  template_elements=fine.template_rows)
            records.append(record)
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


def _branch_sample(prepared, receiver, coordinates, synchronize, *, cap=False, terminal=False, erasure=False):
    if cap:
        coordinates = public_work_coordinates(receiver, coordinates)
    synchronize()
    began = time.perf_counter()
    probe = prepared.fine_probe(receiver, coordinates, units=2 if erasure else 1,
        stratum='terminal' if terminal else 'active', work_probe=cap, interior_erasure=erasure)
    synchronize()
    timing = _phase_timing(began, probe['represented_baseband_seconds'])
    result = probe['result']
    if erasure and (result['requested_invalid_rows'] != 2 or not result['additional_discarded_rows']):
        raise ValueError('erasure probe did not expand a partially invalid DFT block')
    record = _complete_record(result, probe['endpoint'], require_templates=not terminal)
    record['work'].update(physical_first_sample=probe['public_first_sample'],
        coordinates=result['coordinates'])
    if cap:
        record['work']['capacity_transition_coverage'] = result['capacity_transition_coverage']
    return dict(records=[record], **timing, scope='Measured short work on the represented interval.')


def _dsp_work(work):
    """Compare useful DSP while retaining device-specific dispatch in the report."""
    result = copy.deepcopy(work)
    for row in (result, *result.get('lanes', {}).values()):
        row.get('work_counts', {}).pop('hypothesis_update_tasks', None)
        for name in DISPATCH_FIELDS:
            row.get('state_work_regime', {}).pop(name, None)
    coverage = result.get('capacity_transition_coverage', {})
    for lane in ('adaptive', 'nominal'):
        coverage.get(lane+'_work', {}).pop('hypothesis_update_tasks', None)
        for name in DISPATCH_FIELDS:
            coverage.get(lane+'_state_regime', {}).pop(name, None)
    cache = result.get('native_cache', {})
    # Batched CPU planning performs one physical lookup for many logical rows;
    # CUDA may still look them up separately. Keep the actual hits in receipts,
    # and compare row_tile_references to preserve their useful template demand.
    for name in ('accelerator', 'staging_workspace_bytes', 'mapped_worker_tasks', 'maximum_mapped_workers',
                 'cache_hits'):
        cache.pop(name, None)
    return result


def compare_work(cpu, cuda):
    """Separate work equality and likelihood closeness from timing."""
    if not cpu or len(cpu) != len(cuda):
        return dict(work_match=False, scores_match=False)
    return dict(work_match=all(_dsp_work(a['work']) == _dsp_work(b['work']) for a,b in zip(cpu,cuda)),
        scores_match=all(a['lane_scores'].keys() == b['lane_scores'].keys() and
            all(np.allclose(a['lane_scores'][name], b['lane_scores'][name],
                rtol=SCORE_RTOL, atol=SCORE_ATOL) and
                np.sign(np.diff(a['lane_scores'][name])[0]) == np.sign(np.diff(b['lane_scores'][name])[0])
                for name in a['lane_scores']) for a,b in zip(cpu,cuda)))


def _compare_cost_regime(cpu, cuda):
    """Bound adaptive-work drift while retaining the exact-work parity result."""
    if not cpu or len(cpu) != len(cuda):
        return dict(accepted=False, reason='missing_matched_checkpoint')
    errors = []
    for left, right in zip(cpu, cuda):
        a, b = (_dsp_work(record['work']) for record in (left, right))
        for key in ('consumed_samples', 'processed_rows', 'blocks', 'segments',
                    'additional_discarded_rows', 'requested_invalid_rows', 'probe_endpoint',
                    'physical_first_sample', 'basis_address', 'coordinates'):
            if a.get(key) != b.get(key):
                return dict(accepted=False, reason='public_schedule_or_endpoint_changed', field=key)
        if a['lanes']['initial_affine'] != b['lanes']['initial_affine']:
            return dict(accepted=False, reason='fixed_affine_work_changed')
        if a.get('retained_states', {}).get('initial_affine') != b.get('retained_states', {}).get('initial_affine'):
            return dict(accepted=False, reason='fixed_affine_state_identity_changed')
        for key in ('method', 'tile_samples', 'dense_factor', 'cache_limit_bytes', 'fallback_reason',
                    'native_preparation_calls', 'native_preparation_samples', 'cache_misses', 'evictions',
                    'direct_rows', 'large_phase_fallback_rows', 'staging_fallback_rows'):
            if a['native_cache'].get(key) != b['native_cache'].get(key):
                return dict(accepted=False, reason='native_cache_fill_or_geometry_changed', field=key)
        callbacks = a['native_cache']['calls'], b['native_cache']['calls']
        if abs(callbacks[0]-callbacks[1]) > max(2, .10*callbacks[0]):
            return dict(accepted=False, reason='mapped_callback_count_changed')
        references = a['native_cache']['row_tile_references'], b['native_cache']['row_tile_references']
        error = abs(references[1]-references[0])/max(1, references[0])
        errors.append(error)
        if error > ADAPTIVE_COST_RTOL:
            return dict(accepted=False, reason='mapped_row_tile_demand_changed', relative_error=error)
        aa, bb = (row['lanes']['adaptive'] for row in (a,b))
        for key in ('initial_states', 'state_cap', 'proposal_cap', 'attained_state_cap',
                    'attained_proposal_cap', 'coherence_models'):
            if aa['state_work_regime'][key] != bb['state_work_regime'][key]:
                return dict(accepted=False, reason='adaptive_public_regime_changed', field=key)
        if aa['work_counts'].keys() != bb['work_counts'].keys():
            return dict(accepted=False, reason='adaptive_counter_schema_changed')
        for key in aa['work_counts']:
            before, after = aa['work_counts'][key], bb['work_counts'][key]
            error = abs(after-before)/max(1, before)
            errors.append(error)
            if error > ADAPTIVE_COST_RTOL:
                return dict(accepted=False, reason='adaptive_cost_driver_changed', field=key,
                            relative_error=error)
    return dict(accepted=True, maximum_relative_adaptive_cost_driver_error=max(errors, default=0.))


def _acquisition(prepared, instance, *, independent_cost_holdout=False):
    measured = prepared.acquisition(instance)
    result = measured.pop('result')
    session = result.pop('coarse_session', None)
    helper = result.get('helper', {})
    helper_work = {key: helper.get(key) for key in ('status', 'reason', 'procedure_id', 'original_count',
                   'selected_count', 'expanded_intervals_hz', 'onset_intervals_seconds')}
    helper_work['aperture_branches'] = [{key: branch['frequency'].get(key) for key in
        ('status', 'reason', 'observation_seconds', 'aperture_count', 'expanded_intervals_hz')}
        for branch in helper.get('aperture_conditioning', {}).get('branches', ())]
    evidence = dict(status=result['status'], coordinates=result.get('coordinates', ()),
        frequency_intervals=result.get('searched_frequency_intervals_hz'),
        candidates=result.get('coarse', {}).get('candidates', ()),
        helper=helper_work, stages=result.get('stages', {}),
        acquisition_work_class=measured.get('acquisition_work_class'),
        work=result.get('coarse', {}).get('work'), elapsed_seconds=(
            measured['ratio_processing_seconds_per_baseband_second']*prepared.slot_seconds))
    if session is not None:
        try:
            plan = session.work_plan
            evidence['full_plan'] = {key: value for key, value in plan.items() if key != 'frequencies_hz'}
            evidence['bounded_tiles'] = []
            for size in (1, 1, 2):
                if session.complete:
                    break
                evidence['bounded_tiles'].append(session.step(size))
            if session.complete:
                finished = session.finish()
                evidence['completed_bounded_grid'] = finished.get('work')
        finally:
            session.close()
        if independent_cost_holdout:
            evidence['full_selection_holdout'] = measure_full_selection_metadata(
                prepared, instance, plan, family_size=1)
    elif independent_cost_holdout and result['status']=='acquired':
        ratio = 'ratio_processing_seconds_per_baseband_second'
        samples = [measured[ratio]*prepared.slot_seconds]
        for _ in range(2 if samples[0]<=.5 else 0):
            trial = prepared.acquisition(instance)
            observed = trial['result']
            extra_session = observed.pop('coarse_session',None)
            if extra_session is not None:
                extra_session.close()
            if (extra_session is not None or trial['acquisition_work_class']!=evidence['acquisition_work_class'] or
                    any(observed.get(key)!=result.get(key) for key in
                        ('status','coordinates','searched_frequency_intervals_hz'))):
                raise ValueError('independent repeated acquisition work changed')
            samples.append(trial[ratio]*prepared.slot_seconds)
        evidence['elapsed_observations_seconds'] = samples
        evidence['elapsed_seconds'] = fmean(samples)
    return evidence


def _compare_acquisition(cpu, cuda):
    keys = ('bit', 'epoch', 'arrival', 'frequency', 'clock_ppm', 'rank')
    left, right = cpu['candidates'], cuda['candidates']
    return dict(status_match=cpu['status'] == cuda['status'],
        coordinates_match=cpu['coordinates'] == cuda['coordinates'],
        helper_work_match=cpu['helper'] == cuda['helper'],
        candidate_order_match=[tuple(row[key] for key in keys) for row in left] ==
                              [tuple(row[key] for key in keys) for row in right],
        candidate_scores_match=len(left)==len(right) and np.allclose(
            [row['score'] for row in left], [row['score'] for row in right], rtol=SCORE_RTOL, atol=SCORE_ATOL),
        bounded_tile_work_match=[(tile['completed_frequency_points'], tile['raw_candidate_rows'])
            for tile in cpu.get('bounded_tiles', ())] == [(tile['completed_frequency_points'], tile['raw_candidate_rows'])
            for tile in cuda.get('bounded_tiles', ())])


def _case_probe(prepared, workers, devices, synchronize, result, *, cuda_basis_factory=None,
                reference_screen=None):
    gpu_basis = cuda_basis_factory(prepared.screen) if cuda_basis_factory is not None else None

    def receiver(device):
        screen = reference_screen if device == 'cpu' and reference_screen is not None else prepared.screen
        options = dict(basis_factory=gpu_basis) if device == 'cuda' else {}
        return CompleteBitReceiver(prepared.manifest, screen, prepared.basis_source,
            workers=workers, memory_limit_bytes=prepared.receiver_memory_limit_bytes, **options)

    result.update(public_geometry=prepared.public_geometry,
        evaluator=dict(channel_seed=prepared.channel_seed, noise_seed=prepared.noise_seed,
            source_bit=prepared.source_bit, initial_noise_only_seed=96341, recurring_noise_only_seed=96342),
        input_sha256=hashlib.sha256(memoryview(prepared.raw).cast('B')).hexdigest(),
        validity_sha256=hashlib.sha256(memoryview(prepared.valid).cast('B')).hexdigest(),
        preparation=dict(receiver_seconds=prepared.receiver_preparation_seconds,
            simulator_seconds=prepared.simulator_generation_seconds,
            bank=prepared.bank.public_bank_work_receipt), devices={})
    for device in devices:
        with receiver(device) as instance:
            strata = {}
            acquired = _acquisition(prepared, instance, independent_cost_holdout=devices==('cpu',))
            coordinates = tuple(acquired['coordinates'])
            if acquired['status'] != 'acquired' or not coordinates:
                raise ValueError('complete DATA acquisition did not produce fine coordinates')
            result['devices'][device] = dict(procedure_id=instance.procedure_id, strata=strata,
                                              data_acquisition=acquired)
            strata['nominal_continuation'] = _continuation_sample(prepared, instance, coordinates, synchronize[device])
            if device == 'cuda':
                cache = strata['nominal_continuation']['records'][-1]['work']['native_cache']
                acceleration = cache.get('accelerator', {})
                if (not acceleration.get('mapped_values') or
                        not (acceleration.get('dft_values') or acceleration.get('dc_reduction_input_values'))):
                    raise ValueError('nominal probe did not execute CUDA mapped basis and DFT projection work')
            strata['maximum_state_transition'] = _branch_sample(prepared, instance, coordinates,
                synchronize[device], cap=True)
            strata['terminal'] = _branch_sample(prepared, instance, coordinates,
                synchronize[device], terminal=True)
            strata['interior_erasure'] = _branch_sample(prepared, instance, coordinates,
                synchronize[device], erasure=True)
    noise_only_observation(prepared)
    for device in devices:
        with receiver(device) as instance:
            noise = _acquisition(prepared, instance, independent_cost_holdout=devices==('cpu',))
            coordinates = tuple(noise['coordinates']) or public_work_coordinates(instance)
            result['devices'][device]['noise_acquisition'] = noise
            result['devices'][device]['strata']['noise_only'] = _branch_sample(
                prepared, instance, coordinates, synchronize[device])
    if hasattr(prepared.screen, 'gpu_workspace_receipt'):
        result['gpu_coarse'] = prepared.screen.gpu_workspace_receipt()
    if len(devices) == 2:
        cpu, cuda = (result['devices'][device]['strata'] for device in ('cpu','cuda'))
        result['parity'] = {name:compare_work(cpu[name]['records'],cuda[name]['records']) for name in STRATA}
        result['cost_regime'] = {name:_compare_cost_regime(cpu[name]['records'],cuda[name]['records'])
                                 for name in STRATA}
        result['acquisition_parity'] = {key: _compare_acquisition(
            result['devices']['cpu'][key], result['devices']['cuda'][key])
            for key in ('data_acquisition', 'noise_acquisition')}
        def elapsed(stratum):
            phases = stratum.get('phase_timing')
            return sum(phase['elapsed_seconds'] for phase in phases) if phases else stratum['elapsed_seconds']
        result['paired_fine_speedup'] = {name: elapsed(cpu[name])/elapsed(cuda[name]) for name in STRATA}
        def continued(stratum):
            return sum(phase['elapsed_seconds'] for phase in stratum['phase_timing']
                       if phase['phase'] == 'continuation')
        result['paired_continuation_speedup'] = continued(cpu[STRATA[0]])/continued(cuda[STRATA[0]])
        result['paired_acquisition_speedup'] = {
            key: result['devices']['cpu'][key]['elapsed_seconds']/result['devices']['cuda'][key]['elapsed_seconds']
            for key in ('data_acquisition', 'noise_acquisition')}


def _recurring_probe(prepared, workers, result, *, screen_factory=None):
    prepared.screen = prepared.bank.screen = None
    last = prepared.recurring_window['index']
    first = 2 if last == 1 else 1
    result['recurring_banks'] = []
    for index in (first, last):
        plan = prepared.calendar['plans'][index]
        options = dict(screen_factory=screen_factory) if screen_factory is not None else {}
        bank = build_recurring_public_bank(prepared, plan, workers=workers, **options)
        record = dict(public_slot_index=index,
            public_address=bank['public_address'], work=bank['work'], timing=bank['work_receipt'],
            elapsed_seconds=bank['receiver_preparation_seconds'])
        result['recurring_banks'].append(record)
        if index == last:
            noise_only_observation(prepared, seed=96342, samples=plan['receiver_window_samples'])
            manifest = copy.deepcopy(bank['manifest'])
            manifest['power']['complex_noise_variance'] = prepared.noise_variance
            with CompleteBitReceiver(manifest, bank['screen'], prepared.basis_source, workers=workers,
                                     memory_limit_bytes=prepared.receiver_memory_limit_bytes) as receiver:
                result['recurring_acquisition'] = _acquisition(prepared, receiver,
                    independent_cost_holdout=tuple(result['devices'])==('cpu',))
                result['recurring_acquisition']['public_slot_index'] = index
        if hasattr(bank['screen'], 'gpu_workspace_receipt'):
            record['gpu_coarse'] = bank['screen'].gpu_workspace_receipt()
        del bank


def run_probe(*, case_list, case_indices, executable, workers, device, output, paired_reference=False,
              workspace_mib=4096, coarse_cuda=None, reference_cases=None):
    import torch

    began = time.perf_counter()
    if device not in ('cpu','cuda'):
        raise ValueError('device must be cpu or cuda')
    if coarse_cuda is None:
        coarse_cuda = device == 'cuda'
    if device == 'cuda' and not torch.cuda.is_available():
        raise ValueError('CUDA requested but unavailable')
    parsed = load_cases(case_list)
    selected = select_cases(parsed, case_indices)
    if paired_reference and reference_cases is not None:
        raise ValueError('choose all selected rows or explicit reference rows for the paired comparison')
    reference_rows = (selected if paired_reference else select_cases(parsed, reference_cases)
                      if reference_cases is not None else ())
    reference_indices = {case['index'] for case in reference_rows}
    if (not selected or (reference_rows and (device != 'cuda' or len(reference_rows) > 3 or
            not reference_indices <= {case['index'] for case in selected}))):
        raise ValueError('paired comparison requires CUDA and at most three predeclared representative rows')
    machine = machine_resources()
    if machine['cpu'] is None or machine['memory_budget_bytes'] is None:
        raise ValueError('visible CPU and host memory bounds required')
    if workers is None:
        workers = machine['cpu']['available_cpus']
    allocation = resolve_cpu_budget(jobs=1,workers=workers,threads=1,capacity=machine['cpu'])
    if allocation['explicit_overcommit']:
        raise ValueError('worker count exceeds the visible CPU allocation')
    runtime_limits = configure_numerical_threads(allocation)
    synchronize = dict(cpu=lambda:None,cuda=torch.cuda.synchronize)
    if type(workspace_mib) is not int or workspace_mib < 1:
        raise ValueError('positive workspace MiB required')
    memory_limit = min(machine['memory_budget_bytes'], workspace_mib*1024**2)
    if device=='cpu' and memory_limit!=workspace_mib*1024**2:
        raise ValueError('requested CPU holdout workspace exceeds the visible receiver budget')
    cuda_basis_factory, screen_factory = None, None
    if device == 'cuda':
        from triad_gpu_bank import make_gpu_screen_factory, needs_precise_grid
        from triad_gpu_fine import make_cuda_basis_factory
        cuda_device = f'cuda:{torch.cuda.current_device()}'
        def cuda_basis_factory(screen):
            return make_cuda_basis_factory(cuda_device,
                precise=needs_precise_grid(screen.clocks, screen.output_fs))

        memory_limit = min(memory_limit, torch.cuda.mem_get_info()[0]//2)
        torch.cuda.reset_peak_memory_stats()
        if coarse_cuda:
            screen_factory = make_gpu_screen_factory(device=cuda_device, workspace_bytes=memory_limit)
    elif coarse_cuda:
        raise ValueError('GPU coarse work requires CUDA')
    sources, shared_sources = _source_hashes(device), measured_source_hashes()
    exporter_sha256 = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
    kernel_path = mapped_kernel_path(executable)
    kernel_sha256 = hashlib.sha256(kernel_path.read_bytes()).hexdigest()
    runtime = environment(device)
    if device == 'cuda':
        runtime.pop('host', None)
        runtime['cuda_jit'] = _cuda_jit_identity(torch)
    runtime_limits['pools'] = [{key:value for key,value in pool.items() if key != 'filepath'}
        for pool in runtime_limits['pools']]
    report = dict(schema=SCHEMA, status='running', cases=[], device=device, workers=workers,
        input=dict(source_sha256=parsed['source_sha256'], current_list_entries=len(parsed['cases']),
                   selected_indices=[case['index'] for case in selected], full_list=len(selected)==len(parsed['cases'])),
        sources=sources, shared_sources=shared_sources, native_binary_sha256=exporter_sha256,
        mapped_kernel_sha256=kernel_sha256, runtime=runtime,
        cpu_budget=machine['cpu'] if device == 'cpu' else None,
        runtime_limits=runtime_limits, memory_limit_bytes=memory_limit,
        coarse_device='cuda' if coarse_cuda else 'cpu',
        local_cpu_service_established=False, complete_receiver_qualification=False,
        policy=dict(strata=list(STRATA), precision=('CUDA complex64 bulk, complex128 for the compact '
            'tight-clock precision class, FP64 CPU state') if device == 'cuda' else 'fp64',
            score_atol=SCORE_ATOL, score_rtol=SCORE_RTOL, paired_reference_case_indices=sorted(reference_indices),
            adaptive_cost_driver_relative_limit=ADAPTIVE_COST_RTOL,
            broad_grid_maximum_frequency_tiles=4,
            independent_acquisition_cost=dict(short_first_observation_seconds=.5,
                                              short_repetitions=3,long_repetitions=1),
            dispatch_only_comparison_exclusions=[*DISPATCH_FIELDS, 'hypothesis_update_tasks'],
            scope='Bounded native DATA and H0 acquisition, fine branches and GPU work counts. '
                  'Partial broad search yields work only. Short endpoints are not message trials.'))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.touch(exist_ok=False)
    _write_probe_report(output,report)
    measured_cases = {}
    for case in selected:
        paired = case['index'] in reference_indices
        devices = ('cpu', 'cuda') if paired else (device,)
        key = case['configuration_hash'], paired
        previous = measured_cases.get(key)
        if previous is not None:
            row = copy.deepcopy(previous)
            row['reused_measurement_from'] = previous['case']['id']
            row['case'] = case
            report['cases'].append(row)
            continue
        row = dict(case=case)
        print(f"probing manual row {case['index']} with {workers} workers",flush=True)
        try:
            plan = native_plan(executable,case['native_args'])
            options = dict(screen_factory=screen_factory) if screen_factory is not None else {}
            with PreparedCapacityCase(executable,case,plan,memory_limit_bytes=memory_limit,
                    bank_workers=workers, **options) as prepared:
                if paired and coarse_cuda:
                    from triad_case_replay import PreparedReplay
                    from triad_gpu_bank import compare_gpu_bank
                    with PreparedReplay(executable, case['native_args'], memory_limit_bytes=memory_limit,
                            threads=workers, seed=prepared.channel_seed) as reference:
                        row['cpu_reference_bank_seconds'] = reference.receiver_preparation_seconds
                        started = time.perf_counter()
                        row['bank_parity'] = compare_gpu_bank(reference.screen, prepared.screen)
                        row['bank_parity']['validation_seconds'] = time.perf_counter()-started
                        row['paired_bank_speedup'] = (reference.receiver_preparation_seconds/
                                                     prepared.bank.receiver_preparation_seconds)
                        _case_probe(prepared,workers,devices,synchronize,row,
                            cuda_basis_factory=cuda_basis_factory, reference_screen=reference.screen)
                else:
                    _case_probe(prepared,workers,devices,synchronize,row,
                                cuda_basis_factory=cuda_basis_factory)
                _recurring_probe(prepared,workers,row,screen_factory=screen_factory)
        except (ValueError, RuntimeError, FloatingPointError, MemoryError, EOFError, OSError,
                subprocess.SubprocessError) as error:
            row['error'] = str(error)
        report['cases'].append(row)
        measured_cases[key] = row
        _write_probe_report(output,report)
    stable = (sources == _source_hashes(device) and shared_sources == measured_source_hashes() and
        hashlib.sha256(Path(executable).read_bytes()).hexdigest() == exporter_sha256 and
        hashlib.sha256(kernel_path.read_bytes()).hexdigest() == kernel_sha256 and
        hashlib.sha256(Path(case_list).read_bytes()).hexdigest() == parsed['source_sha256'])
    report['unchanged_at_completion'] = stable
    report['trace_observed_elapsed_seconds'] = time.perf_counter()-began
    if device == 'cuda':
        report['cuda_peak_allocated_bytes'] = torch.cuda.max_memory_allocated()
    if not stable or any('error' in row for row in report['cases']):
        report['status'] = 'unresolved'
    elif device == 'cpu':
        report['status'] = 'cpu_reference_only'
    elif report['cuda_peak_allocated_bytes'] > 0:
        if not reference_rows:
            report['status'] = 'bounded_cuda_work_observed'
        elif all(row.get('bank_parity', {}).get('accepted', True) and
                all(all(check.values()) for check in row['acquisition_parity'].values()) and
                all(check['scores_match'] for check in row['parity'].values()) and
                all(check['accepted'] for check in row['cost_regime'].values())
                for row in report['cases'] if row['case']['index'] in reference_indices):
            report['status'] = 'bounded_mixed_precision_cost_regime_observed'
        else:
            report['status'] = 'unresolved'
    else:
        report['status'] = 'unresolved'
    _write_probe_report(output,report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,allow_abbrev=False)
    parser.add_argument('--case-list',type=Path,default=DEFAULT_CASES)
    parser.add_argument('--case-index',type=int,action='append')
    parser.add_argument('--exporter',type=Path,default=ROOT/'build/dev/triad_helper_export')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--workers',type=int,help='host receiver workers, defaulting to all visible logical CPUs')
    parser.add_argument('--device',choices=('cpu','cuda'),default='cuda')
    parser.add_argument('--paired-reference',action='store_true')
    parser.add_argument('--reference-case',type=int,action='append',
                        help='include a matched CPU comparison for this selected row, at most three')
    parser.add_argument('--workspace-mib',type=int,default=4096)
    parser.add_argument('--coarse-cuda', action=argparse.BooleanOptionalAction,
                        help='use CUDA coarse acquisition by default with CUDA; opt out for a CPU-coarse diagnostic')
    args = parser.parse_args(argv)
    try:
        result = run_probe(case_list=args.case_list,case_indices=args.case_index,
            executable=args.exporter,workers=args.workers,device=args.device,output=args.output,
            paired_reference=args.paired_reference, workspace_mib=args.workspace_mib, coarse_cuda=args.coarse_cuda,
            reference_cases=args.reference_case)
    except (ValueError, RuntimeError, OSError, ImportError) as error:
        parser.error(str(error))
    print(result['status'])
    return 2 if result['status'] == 'unresolved' else 0


if __name__ == '__main__':
    sys.exit(main())
