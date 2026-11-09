#!/usr/bin/env python3
"""Confirm one pilot-derived operating point using fresh paired receptions."""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
import math
import multiprocessing
import os
from pathlib import Path
import sys
from tempfile import NamedTemporaryFile
from threading import Event, Thread
import time

import triad_manual_sensitivity as pilot_helper
from triad_manual_sensitivity import (
    PrerequisiteError, _finite, _output_collides, _require, _same_identity, _sha,
)

_SCHEMA = 'triad-complete-bit-fixed-confirmation-v2'
_PAIR_COUNT = 29
_SEED_FIRST = 985000
_POINT_OFFSET_DB = 3.
_MAX_JOBS = 6
_WORKERS = 2
_TOTAL_SECONDS = 1800.
_CLOSE_RESERVE_SECONDS = 20.
_WAVE_COST_ALLOWANCE = 1.25
_PARENT_RESERVE_BYTES = 384 * 1024**2
_PARENT_EXTRA_BYTES = 32 * 1024**2
_CHILD_RESERVE_BYTES = 448 * 1024**2
_NATIVE_RUNTIME_ALLOWANCE_BYTES = 32 * 1024**2
_NATIVE_POLL_SECONDS = .25


def _pair_seeds(index):
    return [_SEED_FIRST+2*index, _SEED_FIRST+2*index+1]


def _peak_rss_bytes(who):
    import resource

    return int(resource.getrusage(who).ru_maxrss)*1024


def _postexec_native_rss(parent_pid, executable_identity):
    """Sample direct children of the worker's launch thread after native exec."""
    children = Path(f'/proc/{parent_pid}/task/{parent_pid}/children')
    with children.open('rb') as stream:
        pids = stream.read(256)
    if len(pids) == 256:
        raise ValueError('native child list exceeds monitor bound')
    values = []
    for pid in pids.split():
        process = Path('/proc')/str(int(pid))
        try:
            binary = (process/'exe').stat()
            if (binary.st_dev, binary.st_ino) != executable_identity:
                continue
            with (process/'status').open('rb') as stream:
                fields = dict(line.split(b':', 1) for line in stream.read(16384).splitlines()
                              if b':' in line)
            binary = (process/'exe').stat()
            rss = fields.get(b'VmRSS', b'').split()
            if ((binary.st_dev, binary.st_ino) == executable_identity and
                    fields.get(b'PPid', b'').strip() == str(parent_pid).encode() and
                    len(rss) == 2 and rss[1] == b'kB'):
                values.append(int(rss[0])*1024)
        except (OSError, ValueError):
            # A short child may exit between enumeration and either read.
            continue
    return values


class _NativeRssMonitor:
    def __init__(self, executable):
        binary = Path(executable).stat()
        self.executable_identity = binary.st_dev, binary.st_ino
        self.parent_pid = os.getpid()
        self.stop = Event()
        self.thread = Thread(target=self._sample, daemon=True)
        self.samples = self.read_failures = self.maximum_children = self.peak = 0

    def _sample(self):
        while not self.stop.is_set():
            try:
                values = _postexec_native_rss(self.parent_pid, self.executable_identity)
            except (OSError, ValueError):
                self.read_failures += 1
                values = []
            if values and sum(values) > 0:
                self.samples += 1
                self.maximum_children = max(self.maximum_children, len(values))
                self.peak = max(self.peak, sum(values))
            self.stop.wait(_NATIVE_POLL_SECONDS)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exception):
        self.stop.set()
        self.thread.join()

    def receipt(self):
        return dict(positive_samples=self.samples, scan_failures=self.read_failures,
            maximum_concurrent_children_observed=self.maximum_children,
            sampled_peak_combined_rss_bytes=self.peak, poll_interval_seconds=_NATIVE_POLL_SECONDS)


def _child_numerical_setup():
    from triad_cpu import configure_numerical_threads

    configure_numerical_threads(dict(threads=1, linear_algebra_threads=1))


def _slot_geometry(slot):
    coverage = slot.get('coverage', {})
    admission = slot.get('operational', {})
    return dict(index=slot.get('index'), address=slot.get('public_address'),
        first=coverage.get('physical_first_sample'), stop=coverage.get('physical_stop_sample'),
        samples=coverage.get('consumed_samples'), template_identity=slot.get('template_identity'),
        admission={key:admission.get(key) for key in (
            'grid_identity', 'full_grid_candidates', 'log_e_threshold', 'evidence_source')})


def _valid_digest(value):
    return (isinstance(value, str) and len(value) == 64 and
            all(digit in '0123456789abcdef' for digit in value))


def _successful_endpoint(outcome, bit, point, seed, slot_geometry):
    slots = outcome.get('slots', ())
    return bool(outcome.get('supported') is True and outcome.get('success') is True and
        outcome.get('source_bit') == bit and outcome.get('native_seed') == seed and
        outcome.get('cn0_db_hz') == point and outcome.get('independent_native_seed') is True and
        outcome.get('physical_complete') is True and
        outcome.get('complete_receiver_work_covered') is True and
        outcome.get('received_bits') == [bit] and outcome.get('trailing_quiet_slots') == [1, 2] and
        outcome.get('erasure_slot_indices') == [] and
        [_slot_geometry(slot) for slot in slots] == slot_geometry and
        all(_valid_digest(slot.get('procedure_id')) for slot in slots) and
        [slot.get('slot_classification') for slot in slots] == ['DATA', 'QUIET', 'QUIET'] and
        slots[0].get('decision_bit') == bit and
        all(slot.get('coverage', {}).get('all_input_valid') is True and
            slot.get('coverage', {}).get('complete_observation') is True for slot in slots))


def _run_pair(index, executable, case, point, memory, slot_geometry):
    import resource

    began = time.perf_counter()
    outcomes = []
    for bit, seed in enumerate(_pair_seeds(index)):
        native = None
        try:
            with _NativeRssMonitor(executable) as native:
                row = pilot_helper.run_stream_trial(executable, case, bit=bit,
                    cn0_db_hz=point, seed=seed, memory_limit_bytes=memory, workers=_WORKERS)
            matched = _successful_endpoint(row, bit, point, seed, slot_geometry)
            outcomes.append(dict(bit=bit, seed=seed, matched_success=matched,
                supported=row.get('supported'), receiver_status=row.get('status'),
                received_bits=row.get('received_bits'), physical_complete=row.get('physical_complete'),
                complete_receiver_work_covered=row.get('complete_receiver_work_covered'),
                trailing_quiet_slots=row.get('trailing_quiet_slots'),
                erasure_slot_indices=row.get('erasure_slot_indices'),
                represented_baseband_seconds=row.get('represented_baseband_seconds'),
                reason=('public_endpoint_or_admission_mismatch' if row.get('success') and not matched
                        else row.get('mismatch') or row.get('reason')),
                error_type=row.get('error_type'), diagnostic=row.get('receiver_diagnostic'),
                slots=[dict(geometry=_slot_geometry(slot),
                    classification=slot.get('slot_classification'),
                    procedure_id=slot.get('procedure_id'),
                    estimated_peak_bytes=slot.get('workspace', {}).get('estimated_peak_bytes'))
                    for slot in row.get('slots', ())],
                elapsed_seconds_for_admission=row.get('experiment_elapsed_seconds')))
        except Exception as error:
            outcomes.append(dict(bit=bit, seed=seed, matched_success=False,
                supported=False, reason='trial_runtime_failure', error_type=type(error).__name__[:64]))
        if native is not None:
            outcomes[-1]['native_rss'] = native.receipt()
    return dict(index=index, outcomes=outcomes, success=all(row['matched_success'] for row in outcomes),
        elapsed_seconds_for_admission=time.perf_counter()-began, worker_pid=os.getpid(),
        python_peak_rss_bytes=_peak_rss_bytes(resource.RUSAGE_SELF))


def _matched_pilot(capacity, pilot, parsed, source, executable, capacity_sha, helper_sha):
    _require(capacity.get('schema') in ('triad-manual-complete-capacity-v2',
                                      'triad-manual-complete-capacity-v3') and
        capacity.get('status') != 'running' and
        pilot.get('schema') == 'triad-complete-bit-sensitivity-pilot-v1' and
        pilot.get('status') == 'exploratory_pair_all_successful' and
        pilot.get('capacity_report_sha256') == capacity_sha and
        pilot.get('helper_sha256') == helper_sha, 'matched_finished_capacity_and_pilot_required')
    gate = capacity.get('message_gate', {})
    _require(gate.get('passed') is True and gate.get('status') == 'passed',
             'current_passed_complete_message_gate_required')
    for receipt in (capacity, gate, pilot):
        case_input = receipt.get('input', {})
        provenance = receipt.get('provenance', {})
        _require(case_input.get('source_sha256') == parsed['source_sha256'] and
            case_input.get('unchanged_at_completion') is True and
            provenance.get('unchanged_at_completion') is True and _same_identity(provenance, source),
            'capacity_gate_or_pilot_source_binary_or_list_is_stale')
    selected = pilot_helper.select_case(capacity, parsed, executable)
    case, base = selected['case'], selected['cn0_db_hz']
    _require(selected['workers'] == pilot.get('selected_workers') == _WORKERS and
        pilot.get('case') == case and pilot.get('native_plan') == selected['native_plan'] and
        pilot.get('complete_admission_receipt') ==
            selected['capacity_item']['target_snr']['complete_admission_receipt'] and
        pilot.get('lowest_all_correct_pilot_cn0_db_hz') == base,
        'pilot_does_not_match_current_case_and_admission')
    points = pilot.get('pilot_points', ())
    _require(len(points) == 1 and points[0].get('cn0_db_hz') == base and
        points[0].get('both_bits_and_physical_end_correct') is True and
        points[0].get('predeclared_noise_and_channel_seeds') == [984000, 984001],
        'successful_model_point_pair_required')
    outcomes = points[0].get('outcomes', ())
    _require(len(outcomes) == 2, 'two_completed_pilot_receives_required')
    geometry = [_slot_geometry(slot) for slot in outcomes[0].get('slots', ())]
    pilot_procedures = [slot.get('procedure_id') for slot in outcomes[0].get('slots', ())]
    public_plans = selected['capacity_item']['public_geometry']['public_calendar']['plans'][:3]
    _require(len(geometry) == 3 and len(public_plans) == 3, 'three_pilot_slots_required')
    for slot, plan in zip(geometry, public_plans):
        _require(slot['index'] == plan['index'] and
            slot['first'] == plan['receiver_window_first_sample'] and
            slot['stop'] == plan['receiver_window_stop_sample'] and
            slot['samples'] == plan['receiver_window_samples'] and
            slot['address'] == dict(pattern_epoch=plan['pattern_epoch'], symbol_ordinal=plan['symbol_ordinal']) and
            _valid_digest(slot['template_identity']) and
            _valid_digest(slot['admission']['grid_identity']) and
            type(slot['admission']['full_grid_candidates']) is int and
            _finite(slot['admission']['log_e_threshold']) and
            slot['admission']['evidence_source'] == 'fixed_initial_affine',
            'pilot_slot_procedure_or_public_geometry_mismatch')
    for bit, row in enumerate(outcomes):
        _require(_successful_endpoint(row, bit, base, 984000+bit, geometry) and
            row.get('source_binary_list_and_plan_unchanged') is True and
            [slot['procedure_id'] for slot in row['slots']] == pilot_procedures and
            _finite(row.get('experiment_elapsed_seconds'), positive=True) and
            row.get('represented_baseband_seconds') == selected['public_capture_stop_sample']/6000,
            'matched_pilot_receive_or_cost_receipt_required')
    peaks = [slot.get('workspace', {}).get('estimated_peak_bytes')
             for row in outcomes for slot in row['slots']]
    _require(all(type(value) is int and value > 0 for value in peaks), 'pilot_dsp_workspace_receipts_required')
    native_budgets = [slot['workspace'].get('native_reserved_workspace_bytes')
                      for row in outcomes for slot in row['slots']]
    _require(all(type(value) is int and value > 0 for value in native_budgets),
             'pilot_native_workspace_reservation_required')
    capacity_peak = selected['capacity_item'].get('measurement', {}).get('workspace_margin', {}).get(
        'conservative_peak_bytes')
    _require(_finite(capacity_peak, positive=True), 'same_case_capacity_workspace_receipt_required')
    overlap_bytes = 17*(max(slot['samples'] for slot in geometry)+2*16384)
    stream_overhead = overlap_bytes+3*16384
    minimum_dsp = math.ceil(max(1.05*(max(peaks)+stream_overhead), capacity_peak+stream_overhead))
    pilot_pair_cost = sum(row['experiment_elapsed_seconds'] for row in outcomes)
    return case, base, geometry, minimum_dsp, pilot_pair_cost, max(native_budgets)


def _allocation(resources, capacity, pilot, minimum_dsp, parent_preflight_peak, native_workspace):
    cpu = resources.get('cpu') or {}
    logical = cpu.get('available_cpus')
    physical = (cpu.get('topology') or {}).get('physical_core_count')
    _require(type(logical) is int and logical >= _WORKERS and
        type(physical) is int and physical > 0, 'current_local_cpu_topology_required')
    candidates = [resources.get('memory_budget_bytes'),
        capacity.get('machine', {}).get('memory_budget_bytes'), pilot.get('local_memory_limit_bytes')]
    _require(all(type(value) is int and value > 0 for value in candidates),
             'bounded_current_and_recorded_memory_headroom_required')
    budget = min(candidates)
    parent_reserve = max(_PARENT_RESERVE_BYTES, math.ceil(1.25*parent_preflight_peak)+_PARENT_EXTRA_BYTES)
    jobs = min(_MAX_JOBS, logical//_WORKERS,
               (budget-parent_reserve)//(_CHILD_RESERVE_BYTES+minimum_dsp))
    _require(jobs >= 1, 'no_receive_pair_fits_conservative_memory_headroom')
    return dict(jobs=jobs, receiver_workers_per_job=_WORKERS, numerical_threads_per_worker=1,
        aggregate_receiver_worker_budget=jobs*_WORKERS, available_logical_cpus=logical,
        available_physical_cores=physical, aggregate_memory_budget_bytes=budget,
        parent_preflight_peak_rss_bytes=parent_preflight_peak,
        parent_memory_reserve_bytes=parent_reserve,
        per_job_runtime_reserve_bytes=_CHILD_RESERVE_BYTES,
        per_job_native_stream_workspace_bytes=native_workspace,
        per_native_process_runtime_allowance_bytes=_NATIVE_RUNTIME_ALLOWANCE_BYTES,
        per_job_minimum_dsp_workspace_bytes=minimum_dsp,
        per_job_dsp_memory_limit_bytes=(budget-parent_reserve)//jobs-_CHILD_RESERVE_BYTES)


def _write_report(output, report):
    with NamedTemporaryFile(mode='w', dir=output.parent, prefix=output.name+'.', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write('\n')
            stream.flush()
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)


def _stable_inputs(executable, before, input_hashes):
    from triad_case_suite import identity

    try:
        return (_same_identity(before, identity(executable)) and
                all(_sha(path) == digest for path, digest in input_hashes.items()))
    except Exception:
        return False


def _remaining_forecast_seconds(completed, jobs, wave_costs, initial):
    if not wave_costs:
        return initial
    return math.ceil((_PAIR_COUNT-completed)/jobs)*_WAVE_COST_ALLOWANCE*max(wave_costs)


def _point_procedures(pairs):
    # The ID binds noise variance. Compare all observed slots at the new
    # point, including unsuccessful and partial receives, within each address.
    procedures = [None, None, None]
    for pair in pairs:
        for outcome in pair['outcomes']:
            for slot in outcome.get('slots', ()):
                index, fingerprint = slot['geometry']['index'], slot.get('procedure_id')
                if (type(index) is not int or not 0 <= index < len(procedures) or
                        not _valid_digest(fingerprint) or procedures[index] not in (None, fingerprint)):
                    return None
                procedures[index] = fingerprint
    return procedures


def _worker_memory_estimates(pairs, allocation):
    # One live channel server owns the bounded native workspace and serves
    # basis queries. The short decoder may briefly overlap it. Preserve both
    # allowances even if polling misses that overlap.
    channel_workspace = allocation['per_job_native_stream_workspace_bytes']
    runtime = allocation['per_native_process_runtime_allowance_bytes']
    peaks = {}
    for pair in pairs:
        if 'worker_pid' not in pair:
            continue
        sampled = [row.get('native_rss', {}) for row in pair['outcomes']]
        for row, receipt in zip(pair['outcomes'], sampled):
            if row['supported'] is True and (receipt.get('positive_samples', 0) == 0 or
                    receipt.get('scan_failures', 0) or
                    receipt.get('maximum_concurrent_children_observed', 0) > 2):
                return None
        observed = max((row.get('sampled_peak_combined_rss_bytes', 0) for row in sampled), default=0)
        native_allowance = max(channel_workspace+2*runtime, observed+runtime)
        pid = pair['worker_pid']
        peaks[pid] = max(peaks.get(pid, 0), pair['python_peak_rss_bytes']+native_allowance)
    return peaks


def _run_waves(report, output, executable, case, before, input_hashes, started):
    import resource

    allocation = report['allocation']
    jobs = allocation['jobs']
    memory = allocation['per_job_dsp_memory_limit_bytes']
    point = report['point_cn0_db_hz']
    geometry = report['public_slot_procedure']
    initial = report['policy']['initial_wave_forecast_seconds']
    if time.perf_counter()-started+initial+_CLOSE_RESERVE_SECONDS > _TOTAL_SECONDS:
        return 'unresolved_budget', 'initial_wave_not_admitted'
    pairs, waves = report['pairs'], report['waves']
    with ProcessPoolExecutor(max_workers=jobs, mp_context=multiprocessing.get_context('spawn'),
                             initializer=_child_numerical_setup) as pool:
        while len(pairs) < _PAIR_COUNT:
            forecast = _remaining_forecast_seconds(len(pairs), jobs,
                [wave['elapsed_seconds_for_admission'] for wave in waves], initial)
            if time.perf_counter()-started+forecast+_CLOSE_RESERVE_SECONDS > _TOTAL_SECONDS:
                return 'unresolved_budget', 'remaining_fixed_sample_not_admitted'
            if not _stable_inputs(executable, before, input_hashes):
                return 'invalidated', 'experiment_inputs_changed'
            start = time.perf_counter()
            indices = range(len(pairs), min(_PAIR_COUNT, len(pairs)+jobs))
            futures, submit_failure = [], None
            for index in indices:
                try:
                    future = pool.submit(_run_pair, index, executable, case, point, memory, geometry)
                except Exception as error:
                    submit_failure = dict(index=index, outcomes=[], success=False,
                        error_type=type(error).__name__[:64], reason='worker_process_submit_failure')
                    break
                futures.append((index, future))
            wave_pairs = []
            for index, future in futures:
                try:
                    pair = future.result()
                except Exception as error:
                    pair = dict(index=index, outcomes=[], success=False,
                                error_type=type(error).__name__[:64], reason='worker_process_failure')
                wave_pairs.append(pair)
            if submit_failure:
                wave_pairs.append(submit_failure)
            pairs.extend(wave_pairs)
            parent_rss = _peak_rss_bytes(resource.RUSAGE_SELF)
            worker_memory = _worker_memory_estimates(pairs, allocation)
            combined_memory = None if worker_memory is None else parent_rss+sum(worker_memory.values())
            waves.append(dict(pair_indices=[pair['index'] for pair in wave_pairs], cost_forecast_seconds=forecast,
                elapsed_seconds_for_admission=time.perf_counter()-start,
                controller_peak_rss_bytes=parent_rss, estimated_process_memory_bytes=combined_memory,
                worker_memory_estimates_bytes=None if worker_memory is None else
                    {str(pid):size for pid, size in worker_memory.items()},
                rss_scope='Controller and Python worker lifetime RSS peaks plus each worker native allowance. Native VmRSS samples are post-exec and may miss transients; the workspace and decoder allowance remain in the estimate.'))
            _write_report(output, report)
            print(f"confirmation pairs {len(pairs)}/{_PAIR_COUNT}, "
                  f"{sum(pair['success'] for pair in pairs)} successful", flush=True)
            if not _stable_inputs(executable, before, input_hashes):
                return 'invalidated', 'experiment_inputs_changed'
            if worker_memory is None:
                return 'unresolved_memory', 'postexec_native_rss_observation_incomplete'
            if (parent_rss > allocation['parent_memory_reserve_bytes'] or
                    any(size > memory+_CHILD_RESERVE_BYTES for size in worker_memory.values()) or
                    combined_memory > allocation['aggregate_memory_budget_bytes']):
                return 'unresolved_memory', 'process_memory_estimate_exceeded_predeclared_headroom'
            procedures = _point_procedures(pairs)
            if procedures is None:
                return 'unsupported', 'confirmation_receiver_procedure_changed'
            report['fixed_point_procedure_ids'] = procedures
            if any(len(pair['outcomes']) != 2 or
                   any(row['supported'] is not True or
                       row['reason'] == 'public_endpoint_or_admission_mismatch'
                       for row in pair['outcomes']) for pair in wave_pairs):
                return 'unsupported', 'runtime_or_resource_coverage_failure'
            if any(not pair['success'] for pair in wave_pairs):
                return 'failed_at_point', 'at_least_one_predeclared_pair_failed'
            if time.perf_counter()-started > _TOTAL_SECONDS:
                return 'unresolved_budget', 'admitted_wave_exceeded_elapsed_budget'
    return 'confirmed_at_point', None


def run_confirmation(capacity_path, pilot_path, executable, output):
    began = time.perf_counter()
    from triad_cases import DEFAULT_CASES, load_cases
    from triad_case_metrics import machine_resources
    from triad_case_suite import identity

    _require(sys.platform.startswith('linux'), 'linux_process_and_child_rss_required')
    import resource

    capacity_path, pilot_path, executable = (Path(item).resolve()
                                              for item in (capacity_path, pilot_path, executable))
    output = Path(output)
    helper = Path(pilot_helper.__file__).resolve()
    driver = Path(__file__).resolve()
    protected = {capacity_path, pilot_path, executable, driver, helper, DEFAULT_CASES}
    _require(not _output_collides(output, protected), 'confirmation_output_must_not_replace_an_input')
    capacity_bytes, pilot_bytes = capacity_path.read_bytes(), pilot_path.read_bytes()
    capacity, pilot = json.loads(capacity_bytes), json.loads(pilot_bytes)
    capacity_sha = hashlib.sha256(capacity_bytes).hexdigest()
    pilot_sha = hashlib.sha256(pilot_bytes).hexdigest()
    del capacity_bytes, pilot_bytes
    case_path = Path(capacity.get('input', {}).get('source_path', DEFAULT_CASES)).resolve()
    _require(not _output_collides(output, {*protected, case_path}),
             'confirmation_output_must_not_replace_an_input')
    _require(not output.exists() and not output.is_symlink(), 'fresh_confirmation_output_required')
    parsed = load_cases(case_path)
    before = identity(executable)
    helper_sha = _sha(helper)
    case, base, geometry, minimum_dsp, pilot_cost, native_workspace = _matched_pilot(
        capacity, pilot, parsed, before, executable, capacity_sha, helper_sha)
    input_hashes = {capacity_path:capacity_sha, pilot_path:pilot_sha,
        case_path:parsed['source_sha256'], helper:helper_sha, driver:_sha(driver)}
    _require(_stable_inputs(executable, before, input_hashes), 'experiment_inputs_changed_during_preflight')
    resources = machine_resources()
    allocation = _allocation(resources, capacity, pilot, minimum_dsp,
                             _peak_rss_bytes(resource.RUSAGE_SELF), native_workspace)
    del capacity, pilot, parsed
    initial = 2*pilot_cost*max(1., allocation['aggregate_receiver_worker_budget']/
                              allocation['available_physical_cores'])
    report = dict(schema=_SCHEMA, status='running', provenance=before, case=case,
        inputs={str(path):digest for path, digest in input_hashes.items()},
        pilot_point_cn0_db_hz=base, point_cn0_db_hz=base+_POINT_OFFSET_DB,
        public_slot_procedure=geometry, allocation=allocation, pairs=[], waves=[],
        policy=dict(pair_count=_PAIR_COUNT, maximum_stream_receives=2*_PAIR_COUNT,
            seed_schedule=[_pair_seeds(index) for index in range(_PAIR_COUNT)],
            pilot_offset_db=_POINT_OFFSET_DB, nominal_elapsed_budget_seconds=_TOTAL_SECONDS,
            close_reserve_seconds=_CLOSE_RESERVE_SECONDS, wave_cost_allowance=_WAVE_COST_ALLOWANCE,
            initial_wave_forecast_seconds=initial,
            memory_scope='Per-job DSP admission uses the greater of the same-case conservative capacity peak and the pilot workspace peak with a 5% allowance, including stream overlap. Later waves also require positive owned post-exec native RSS samples and headroom for Python peaks plus native workspace and runtime allowances.',
            stopping='Drain every admitted wave. Stop admitting pairs after any failed pair, changed input, or failed cost or memory gate.',
            endpoint='One pair requires independently seeded bit 0 and bit 1 receptions, each correct and followed by two scored physical quiet slots with complete receiver work.',
            inference='Fixed operating point for this case and synthetic population with fixed key and epoch. Independent pairs are the statistical units. No minimum SNR, literal fox, other configuration, or CPU throughput inference.'),
        fixed_point_probability_qualified=False, one_sided_95_percent_lower_bound=None)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.touch(exist_ok=False)
    _write_report(output, report)
    try:
        status, reason = _run_waves(report, output, executable, case, before, input_hashes, began)
    except Exception as error:
        status, reason = 'unsupported', 'confirmation_runtime_failure'
        report['runtime_error_type'] = type(error).__name__[:64]
    stable = _stable_inputs(executable, before, input_hashes)
    report['provenance']['unchanged_at_completion'] = stable
    elapsed = time.perf_counter()-began
    if not stable:
        status, reason = 'invalidated', 'experiment_inputs_changed'
    elif status == 'confirmed_at_point' and elapsed > _TOTAL_SECONDS:
        status, reason = 'unresolved_budget', 'confirmation_exceeded_elapsed_budget'
    qualified = status == 'confirmed_at_point'
    report.update(status=status, reason=reason, elapsed_seconds_for_admission=elapsed,
        fixed_point_probability_qualified=qualified,
        one_sided_95_percent_lower_bound=.05**(1/_PAIR_COUNT) if qualified else None)
    _write_report(output, report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
    parser.add_argument('--capacity-report', required=True, type=Path)
    parser.add_argument('--pilot-report', required=True, type=Path)
    parser.add_argument('--exporter', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        report = run_confirmation(args.capacity_report, args.pilot_report, args.exporter, args.output)
    except Exception as error:
        report = dict(status='rejected', error_type=type(error).__name__[:64],
            reason=str(error) if isinstance(error, PrerequisiteError) else 'confirmation_setup_failed',
            fixed_point_probability_qualified=False)
    print(json.dumps({key:report.get(key) for key in
        ('status', 'reason', 'error_type', 'fixed_point_probability_qualified')}), flush=True)
    return 0 if report['fixed_point_probability_qualified'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
