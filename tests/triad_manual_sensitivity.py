#!/usr/bin/env python3
"""Bounded complete-receiver sensitivity pilot, separate from CPU capacity.

This non-discovery helper tests one current manual configuration. Independent
transmissions are reception trials, never an estimate of parallel CPU
throughput. A pilot pair cannot establish a greater-than-90% success bound.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))

_BUDGET_SECONDS = 600.
_SEEDS = ((984000, 984001), (984002, 984003))
_SOURCE_CAP = 500_000
_WINDOW_CAP = 1_000_000
_CAPTURE_CAP = 3_000_000


class PrerequisiteError(ValueError):
    """A controlled diagnostic, containing no received content."""


def _require(condition, code):
    if not condition:
        raise PrerequisiteError(code)


def _finite(value, *, positive=False):
    return (not isinstance(value, bool) and isinstance(value, (int, float)) and
            math.isfinite(value) and (not positive or value > 0))


def _same_identity(first, second):
    return all(first.get(key) == second.get(key)
               for key in ('revision', 'sources', 'native_binary_sha256'))


def _sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')
    temporary.replace(path)


def _output_collides(output, protected):
    path = Path(output)
    write_paths = {path.resolve(), path.with_suffix(path.suffix+'.tmp').resolve()}
    return bool(write_paths.intersection(Path(item).resolve() for item in protected))


def _matched_target(item, case, executable):
    """Require the current normal receiver admission model, not coarse-only LR."""
    from triad_case_receiver import EPOCHS
    from triad_case_suite import native_plan, target_snr
    from triad_operational_admission import DEFAULT_COHERENCES, operational_threshold, public_grid
    from triad_receiver import _DEFAULT

    target = item.get('target_snr', {})
    receipt = target.get('complete_admission_receipt', {})
    _require(target.get('status') == 'provisional_model_target' and
        receipt.get('schema') == 'triad-complete-admission-model-v1',
        'matched_complete_admission_receipt_required')
    plan = native_plan(executable, case['native_args'])
    _require(plan == item.get('native_plan'), 'capacity_native_plan_differs_from_current_case')
    source_samples = plan.get('geometry', {}).get('padded_source_samples')
    _require(type(source_samples) is int and 4 <= source_samples <= _SOURCE_CAP,
             'single_bit_native_geometry_exceeds_pilot_budget')
    grid = public_grid(plan['physics'], plan['physics']['arrival_prior_seconds'], EPOCHS)
    grid = replace(grid, frequency_step=min(.01, 1/(4*(source_samples/6000))))
    serialized_grid = json.loads(json.dumps(dict(grid.__dict__), allow_nan=False))
    _require(receipt.get('full_grid') == serialized_grid and
        receipt.get('full_grid_identity') == grid.identity() and
        receipt.get('full_grid_count') == grid.candidate_count,
        'complete_admission_public_grid_mismatch')
    _require(receipt.get('operational_evidence') == 'fixed_initial_affine' and
        receipt.get('operational_budget') == operational_threshold() and
        receipt.get('affine_lane_weight') == 1. and
        receipt.get('model_count') == len(DEFAULT_COHERENCES),
        'complete_admission_operational_policy_mismatch')
    geometry = item.get('public_geometry', {})
    coarse = geometry.get('coarse', {})
    rate = coarse.get('projected_sample_rate_hz')
    _require(_finite(rate, positive=True), 'current_coarse_projection_receipt_required')
    residual = (_DEFAULT['fine_delay_subdivisions']+1)*(3 if grid.clock_bound else 1)
    _require(receipt.get('residual_state_count') == residual and
        _finite(receipt.get('fine_delay_step_seconds'), positive=True) and
        math.isclose(receipt['fine_delay_step_seconds'],
            1/(rate*_DEFAULT['fine_delay_subdivisions']), rel_tol=1e-12) and
        _finite(receipt.get('residual_rate_half_width_ppm')) and
        math.isclose(receipt['residual_rate_half_width_ppm'], grid.clock_bound/4, abs_tol=1e-12),
        'complete_admission_residual_lattice_mismatch')
    public_plans = geometry.get('public_calendar', {}).get('plans', ())
    _require(receipt.get('public_slot_indices') == [row['index'] for row in public_plans] and
        len(public_plans) >= 3 and public_plans[2]['index'] == 2,
        'matched_public_slot_calendar_required')
    _require(math.isclose(receipt.get('source_duration_seconds', -1), source_samples/6000,
                     rel_tol=1e-12), 'single_bit_native_geometry_exceeds_pilot_budget')
    expected = target_snr(plan, complete_admission=receipt)
    current = expected.get('single_bit', {}).get('target_cn0_db_hz')
    recorded = target.get('single_bit', {}).get('target_cn0_db_hz')
    _require(_finite(current) and _finite(recorded) and
        math.isclose(current, recorded, rel_tol=0., abs_tol=1e-9),
        'recorded_target_does_not_match_complete_admission_model')
    return float(current), plan, public_plans


def select_case(capacity, parsed, executable):
    """Choose one affordable current configuration, preferring the actual gate."""
    gate = capacity['message_gate']
    gate_hash = gate.get('case', {}).get('configuration_hash')
    current = {case['configuration_hash']: case for case in parsed['cases']}
    candidates = sorted(capacity.get('cases', ()), key=lambda item: (
        item.get('case', {}).get('configuration_hash') != gate_hash,
        item.get('native_plan', {}).get('geometry', {}).get('padded_source_samples', math.inf)))
    failures = []
    for item in candidates:
        key = item.get('case', {}).get('configuration_hash')
        if key not in current:
            continue
        try:
            cn0, plan, public_plans = _matched_target(item, current[key], executable)
        except PrerequisiteError as error:
            failures.append(dict(configuration_hash=key, reason=str(error)))
            continue
        fs = item['public_geometry']['sample_rate_hz']
        stop = public_plans[2]['receiver_window_stop_sample']
        _require(type(fs) is int and fs == 6000 and type(stop) is int and
            1 <= stop <= _CAPTURE_CAP and
            max(row['receiver_window_samples'] for row in public_plans[:3]) <= _WINDOW_CAP,
            'three_slot_public_capture_exceeds_pilot_budget')
        measurement = item.get('measurement', {})
        if key == gate_hash:
            observed = gate.get('result', {})
            ratio = observed.get('observed_ratio_processing_seconds_per_baseband_second')
            represented = observed.get('represented_baseband_seconds')
            generated = gate.get('simulator_generation_seconds', 0.)
            _require(_finite(ratio, positive=True) and _finite(represented, positive=True) and
                _finite(generated) and generated >= 0, 'matched_message_gate_cost_receipt_required')
            workers = gate.get('workers')
            # Twofold cache/scheduling allowance. This is a conditional work
            # forecast, not a statistical CPU or runtime guarantee.
            forecast = 2*(stop/fs)*(ratio+generated/represented)
            forecast_source = 'Matched native message gate, scaled by exact public three-slot exposure with2x allowance; includes its observed simulator generation separately.'
        else:
            estimate = measurement.get('complete_capacity', {})
            interval = estimate.get('ratio_processing_seconds_per_baseband_second_interval')
            _require(isinstance(interval, (list, tuple)) and len(interval) == 2 and
                _finite(interval[1], positive=True), 'same_case_complete_cost_upper_receipt_required')
            workers = measurement.get('selected_workers')
            forecast = 2*(stop/fs)*interval[1]
            forecast_source = 'Same-case complete-work upper estimate, exact public three-slot exposure and2x allowance; actual total elapsed updates admission after every call.'
        _require(type(workers) is int and workers > 0 and _finite(forecast, positive=True),
                 'valid_measured_worker_allocation_required')
        if 2*forecast > _BUDGET_SECONDS:
            failures.append(dict(configuration_hash=key, reason='initial_pair_cost_forecast_exceeds_budget'))
            continue
        return dict(case=current[key], capacity_item=item, cn0_db_hz=cn0,
            native_plan=plan, public_capture_stop_sample=stop, workers=workers,
            initial_receive_forecast_seconds=forecast, forecast_source=forecast_source,
            rejected_candidates=failures)
    raise PrerequisiteError('no_current_case_has_matched_affordable_target_and_gate_cost')


def run_stream_trial(executable, case, *, bit, cn0_db_hz, seed,
                     memory_limit_bytes, workers, progress=None):
    """One independent native bit + two quiet slots; no IQ is retained."""
    from triad_continuous import NativeStream
    from triad_message_gate import _public_manifest
    from triad_stream_receiver import CompleteStreamReceiver
    _require(type(bit) is int and bit in (0, 1) and _finite(cn0_db_hz) and
        type(seed) is int and 0 <= seed < 2**64, 'invalid_predeclared_trial_input')
    began = time.perf_counter()
    source = receiver = None
    outcome = dict(source_bit=bit, cn0_db_hz=cn0_db_hz, native_seed=seed,
        independent_native_seed=True, success=False, supported=True)
    arguments = [*case['native_args'], '--sample-rate', '6000', '--data-if', '1725',
        '--epoch', '1793850000', '--key-seed', '72001', '--seed', str(seed),
        '--cn0', format(cn0_db_hz, '.17g'), '--seconds', '.001',
        '--power-reference', 'bounded-tiles', '--data-bits', str(bit),
        '--observation-slots', '3', '--thermal-noise', 'on']
    try:
        source = NativeStream(executable, arguments, kind='channel')
        descriptor = source.descriptor
        plans = [source.slot_plan(index) for index in range(3)]
        _require(descriptor['source_samples'] <= _SOURCE_CAP and
            descriptor['total_samples'] <= _CAPTURE_CAP and
            max(row['receiver_window_samples'] for row in plans) <= _WINDOW_CAP,
            'actual_trial_geometry_exceeds_fixed_budget')
        manifest = _public_manifest(source, plans[0])
        identity = dict(native_binary_sha256=_sha(executable),
            pattern_revision=descriptor['pattern_revision'],
            added_amplitude_depth=descriptor['added_amplitude_depth'],
            local_key_fixture_id=72001, public_case_hash=case['configuration_hash'])
        receiver = CompleteStreamReceiver(manifest, source, source_identity=identity,
            memory_limit_bytes=memory_limit_bytes, workers=workers,
            native_executable=executable, maximum_slots=3)
        first = checked = 0
        mismatch = None
        while True:
            values, valid = source.next(16384)  # Actual simulator work, outside push.
            if not len(values):
                receiver.finish_input()
                break
            row = receiver.push(first, values, valid)
            first += len(values)
            if row['message']['slots_consumed'] > checked:
                details = receiver.result()
                for slot in details['slots'][checked:]:
                    index = slot['index']
                    expected = 'DATA' if index == 0 else 'QUIET'
                    if (slot['slot_classification'] != expected or
                            (index == 0 and slot.get('decision_bit') != bit)):
                        mismatch = 'first_completed_slot_mismatch'
                        break
                checked = row['message']['slots_consumed']
            if mismatch or row['status'] != 'receiving':
                break
        result = receiver.result()
        receiver.close()
        receiver = None
        message = result['message']
        outcome.update(success=bool(not mismatch and result['physical_complete'] and
            result['complete_receiver_work_covered'] and
            message['received_bits'] == [bit] and message['trailing_quiet_slots'] == [1, 2] and
            not message['erasure_slot_indices']), mismatch=mismatch,
            status=result['status'], physical_complete=result['physical_complete'],
            complete_receiver_work_covered=result['complete_receiver_work_covered'],
            received_bits=message['received_bits'], erasure_slot_indices=message['erasure_slot_indices'],
            trailing_quiet_slots=message['trailing_quiet_slots'],
            codec_status=message.get('decoded_content'),
            receiver_diagnostic=result.get('diagnostic'), slots=result['slots'],
            represented_baseband_seconds=result['represented_baseband_seconds'],
            observed_receiver_ratio=result['observed_ratio_processing_seconds_per_baseband_second'],
            processing_ratio_scope='Diagnostic only for experiment work admission. Independent transmissions are not a CPU-capacity measurement.',
            source_close=source.close())
        outcome['supported'] = result['status'] not in ('failed', 'resource_limit')
        ratio, seconds = outcome['observed_receiver_ratio'], outcome['represented_baseband_seconds']
        outcome['experiment_receiver_service_seconds'] = ratio*seconds if _finite(ratio) and _finite(seconds) else None
    except Exception as error:
        outcome.update(supported=False, error_type=type(error).__name__[:64],
                       reason=str(error) if isinstance(error, PrerequisiteError) else 'trial_runtime_failure')
        if receiver is not None:
            outcome['receiver_diagnostic'] = receiver.result().get('diagnostic')
    finally:
        if receiver is not None:
            receiver.close()
        if source is not None:
            source.close()
    outcome['experiment_elapsed_seconds'] = max(0., time.perf_counter()-began)
    if progress:
        progress(f"pilot {cn0_db_hz:.3f} dB-Hz bit {bit}: {'success' if outcome['success'] else 'failure'}")
    return outcome


def run_pilot(capacity_report, executable, output):
    experiment_began = time.perf_counter()
    from triad_cases import load_cases
    from triad_case_suite import identity
    from triad_case_metrics import machine_resources
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget

    capacity_report, executable = Path(capacity_report).resolve(), Path(executable).resolve()
    protected = {capacity_report, executable, Path(__file__).resolve(),
                 (ROOT/'_tests-RobustModem.md').resolve()}
    _require(not _output_collides(output, protected), 'pilot_output_must_not_replace_an_input')
    capacity_sha, helper_sha = _sha(capacity_report), _sha(__file__)
    capacity = json.loads(capacity_report.read_text())
    _require(capacity.get('schema') in ('triad-manual-complete-capacity-v2',
        'triad-manual-complete-capacity-v3') and
        capacity.get('status') != 'running', 'completed_capacity_report_required')
    case_path = Path(capacity.get('input', {}).get('source_path', ROOT/'_tests-RobustModem.md'))
    _require(not _output_collides(output, {*protected, case_path}),
        'pilot_output_must_not_replace_an_input')
    parsed = load_cases(case_path)
    before = identity(executable)
    _require(capacity.get('input', {}).get('source_sha256') == parsed['source_sha256'] and
        capacity['input'].get('unchanged_at_completion') is True and
        capacity.get('provenance', {}).get('unchanged_at_completion') is True and
        _same_identity(capacity['provenance'], before), 'capacity_report_source_binary_or_list_is_stale')
    gate = capacity.get('message_gate', {})
    _require(gate.get('passed') is True and gate.get('status') == 'passed' and
        gate.get('input', {}).get('source_sha256') == parsed['source_sha256'] and
        gate['input'].get('unchanged_at_completion') is True and
        gate.get('provenance', {}).get('unchanged_at_completion') is True and
        _same_identity(gate['provenance'], before), 'current_passed_complete_message_gate_required')
    selected = select_case(capacity, parsed, executable)
    resources = machine_resources()
    memory = min(resources['memory_budget_bytes'], capacity['machine']['memory_budget_bytes'])
    allocation = resolve_cpu_budget(jobs=1, workers=selected['workers'], threads=1)
    _require(not allocation['explicit_overcommit'], 'recorded_workers_exceed_current_cpu_allocation')
    numerical = configure_numerical_threads(allocation)
    report = dict(schema='triad-complete-bit-sensitivity-pilot-v1', status='running',
        capacity_report=str(capacity_report), capacity_report_sha256=capacity_sha,
        helper_sha256=helper_sha, provenance=before,
        input={key:value for key,value in parsed.items() if key != 'cases'},
        case=selected['case'], native_plan=selected['native_plan'],
        complete_admission_receipt=selected['capacity_item']['target_snr']['complete_admission_receipt'],
        selected_workers=selected['workers'], numerical_threads=numerical,
        local_memory_limit_bytes=memory, pilot_points=[],
        policy=dict(channel_seed_schedule=[list(row) for row in _SEEDS],
            pilot_points_db=[selected['cn0_db_hz'], selected['cn0_db_hz']+6],
            maximum_stream_receives=4, public_observation_slots=3, local_receiver_slot_quota=3,
            experiment_total_elapsed_budget_seconds=_BUDGET_SECONDS,
            initial_receive_forecast_seconds=selected['initial_receive_forecast_seconds'],
            forecast_source=selected['forecast_source'],
            stopping='One independent bit0/bit1 pair at the matched model target. If either reception fails, one pair at+6dB then stop. Stop immediately on runtime/resource failure, source/list/binary change or cost non-admission; no confirmation is automatic.',
            endpoint='Correct admitted single bit plus two fully scored physical quiet slots; a one-bit short-codec rejection is expected and is not a whole-message success criterion.',
            probability_scope='Exploratory only: fixed key/epoch and one selected configuration, independently seeded synthetic channel/noise per receive. No physical-channel population, simultaneous-case or literalfox90% success qualification.',
            cpu_scope='Independent receives supply sensitivity observations only. Cost counters gate this experiment; no throughput, speedup or CPU-capacity inference is made.'),
        adequate_90_percent_threshold_established=False,
        literal_fox_90_percent_threshold_established=False)
    _write(output, report)
    started = experiment_began
    report['preflight_elapsed_seconds'] = time.perf_counter()-started
    observed_costs = []
    stop = False
    for round_index, seeds in enumerate(_SEEDS):
        point = selected['cn0_db_hz']+6*round_index
        forecast = max(selected['initial_receive_forecast_seconds'],
                       2*max(observed_costs, default=0.))
        if time.perf_counter()-started+2*forecast > _BUDGET_SECONDS:
            report.update(status='unresolved_budget', reason='next_pair_cost_not_admitted')
            break
        pair = dict(cn0_db_hz=point, predeclared_noise_and_channel_seeds=list(seeds), outcomes=[])
        report['pilot_points'].append(pair)
        for bit, seed in enumerate(seeds):
            forecast = max(selected['initial_receive_forecast_seconds'],
                           2*max(observed_costs, default=0.))
            if time.perf_counter()-started+forecast > _BUDGET_SECONDS:
                report.update(status='unresolved_budget', reason='next_receive_cost_not_admitted')
                stop = True
                break
            outcome = run_stream_trial(executable, selected['case'], bit=bit,
                cn0_db_hz=point, seed=seed, memory_limit_bytes=memory,
                workers=selected['workers'], progress=lambda text: print(text, flush=True))
            pair['outcomes'].append(outcome)
            observed_costs.append(outcome['experiment_elapsed_seconds'])
            try:
                current = identity(executable)
                unchanged = (_same_identity(before, current) and _sha(__file__) == helper_sha and
                    _sha(capacity_report) == capacity_sha and
                    _sha(parsed['source_path']) == parsed['source_sha256'])
            except Exception as error:
                unchanged = False
                outcome['identity_check_error_type'] = type(error).__name__[:64]
            outcome['source_binary_list_and_plan_unchanged'] = unchanged
            if not unchanged:
                report.update(status='invalidated', reason='experiment_inputs_changed')
                stop = True
            elif not outcome['supported']:
                report.update(status='unsupported', reason='runtime_or_resource_coverage_failure')
                stop = True
            elif time.perf_counter()-started > _BUDGET_SECONDS:
                report.update(status='unresolved_budget', reason='observed_experiment_budget_exhausted')
                stop = True
            _write(output, report)
            if stop:
                break
        pair['both_bits_and_physical_end_correct'] = bool(len(pair['outcomes']) == 2 and
            all(row['success'] for row in pair['outcomes']))
        if stop:
            break
        if pair['both_bits_and_physical_end_correct']:
            report.update(status='exploratory_pair_all_successful',
                lowest_all_correct_pilot_cn0_db_hz=point)
            break
        report['status'] = 'exploratory_pilot_failed'
    report['experiment_total_elapsed_seconds'] = time.perf_counter()-started
    try:
        report['final_identity'] = identity(executable)
        stable = (_same_identity(before, report['final_identity']) and _sha(__file__) == helper_sha and
            _sha(capacity_report) == capacity_sha and _sha(parsed['source_path']) == parsed['source_sha256'])
    except Exception as error:
        stable = False
        report['final_identity'] = dict(error_type=type(error).__name__[:64])
    report['provenance']['unchanged_at_completion'] = stable
    report['input']['unchanged_at_completion'] = stable
    if not stable:
        report['status'] = 'invalidated'
    _write(output, report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
    parser.add_argument('--capacity-report', type=Path, required=True)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    protected = {args.capacity_report, args.exporter, Path(__file__), ROOT/'_tests-RobustModem.md'}
    if _output_collides(args.output, protected):
        print(json.dumps(dict(status='rejected', reason='pilot_output_must_not_replace_an_input',
            threshold_90_percent_established=False)), flush=True)
        return 2
    try:
        report = run_pilot(args.capacity_report, args.exporter, args.output)
    except Exception as error:
        report = dict(schema='triad-complete-bit-sensitivity-pilot-v1', status='rejected',
            reason=str(error) if isinstance(error, PrerequisiteError) else 'pilot_setup_failed',
            error_type=type(error).__name__[:64], adequate_90_percent_threshold_established=False,
            literal_fox_90_percent_threshold_established=False)
        if not (isinstance(error, PrerequisiteError) and
                str(error) == 'pilot_output_must_not_replace_an_input'):
            _write(args.output, report)
    print(json.dumps(dict(status=report['status'], output=str(args.output),
        threshold_90_percent_established=False)), flush=True)
    return 0 if report['status'] == 'exploratory_pair_all_successful' else 2


if __name__ == '__main__':
    raise SystemExit(main())
