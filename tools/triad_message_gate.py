"""Optional bounded native functional gate for complete quiet-slot reception.

This is one fixed high-SNR integration check. It shares the production receiver
and canonical codec; it is neither a throughput benchmark nor an independent
sensitivity campaign. Simulator generation is outside receiver ``push``.
"""
from __future__ import annotations

import copy
import hashlib
from pathlib import Path
import time


_MAXIMUM_SOURCE_SAMPLES = 500_000
_MAXIMUM_WINDOW_SAMPLES = 1_000_000
_MAXIMUM_CAPTURE_SAMPLES = 3_000_000
_MAXIMUM_CANDIDATE_PLANS = 8
_PROCESSED_SLOTS = 5
_OBSERVATION_SLOTS = 6


def _argument(case, flag, default=None):
    arguments = case['native_args']
    return arguments[arguments.index(flag)+1] if flag in arguments else default


def _ordered_cases(cases):
    """Prefer the cheap current configuration without freezing its row number."""
    unique = {}
    for case in cases:
        unique.setdefault(case['configuration_hash'], case)
    def cost_order(case):
        return (_argument(case, '--helper') != 'off',
            _argument(case, '--fhss') != 'off',
            _argument(case, '--encrypt') != 'off',
            -float(_argument(case, '--rate', 0.)), case['configuration_hash'])
    return sorted(unique.values(), key=cost_order)


def _choose_case(executable, cases):
    from triad_case_suite import native_plan
    attempts = []
    for case in _ordered_cases(cases)[:_MAXIMUM_CANDIDATE_PLANS]:
        plan = native_plan(executable, case['native_args'])
        count = plan.get('geometry', {}).get('padded_source_samples')
        admitted = (plan.get('geometry_feasible') is True and type(count) is int and
                    4 <= count <= _MAXIMUM_SOURCE_SAMPLES)
        attempts.append(dict(index=case['index'], configuration_hash=case['configuration_hash'],
            raw_source_samples=count, admitted=admitted))
        if admitted:
            return case, attempts
    return None, attempts


def _public_manifest(source, first_slot):
    from triad_case_receiver import _manifest
    descriptor = source.descriptor
    # The original emission extent is evaluator metadata. Only the independent
    # public first-slot calendar enters this compatibility manifest helper.
    public_descriptor = dict(sample_rate_hz=descriptor['sample_rate_hz'],
        data_if_hz=descriptor['data_if_hz'],
        capture_lead_seconds=first_slot['capture_lead_seconds'],
        data_offset_samples=first_slot['source_begin_samples'],
        emitted_samples=first_slot['helper_end_samples'],
        helper_enabled=descriptor['helper_enabled'],
        helper_low_hz=descriptor['helper_low_hz'], helper_high_hz=descriptor['helper_high_hz'],
        epoch=first_slot['pattern_epoch'], noise_variance=descriptor['noise_variance'])
    return _manifest(source.plan, public_descriptor,
        first_slot['receiver_window_samples'], 0, 72001)


def _run_selected_case(executable, case, *, memory_limit_bytes, workers, progress):
    import json
    import subprocess
    from triad_continuous import NativeStream
    from triad_stream_receiver import CompleteStreamReceiver

    encode = subprocess.run([str(executable), '--short-encode', 'e'],
        capture_output=True, check=True, timeout=10)
    encoded = json.loads(encode.stdout)
    if (encoded.get('schema') != 'triad-short-codec-v1' or encoded.get('operation') != 'encode' or
            encoded.get('codec') != 'short-prefix' or encoded.get('bits') != '001' or
            encoded.get('encoded_bits') != 3 or encoded.get('source_bytes') != 1):
        raise ValueError('canonical integration token changed')
    reference_bits = tuple(map(int, encoded['bits']))
    arguments = [*case['native_args'], '--sample-rate', '6000', '--data-if', '1725',
        '--epoch', '1793850000', '--key-seed', '72001', '--seed', '74021', '--cn0', '80',
        '--seconds', '.001', '--power-reference', 'bounded-tiles',
        '--data-bits', encoded['bits'], '--observation-slots', str(_OBSERVATION_SLOTS),
        '--thermal-noise', 'on']
    source = None
    receiver = None
    result = dict(functional_passed=False, meaningful_failure=None)
    try:
        source = NativeStream(executable, arguments, kind='channel')
        descriptor = source.descriptor
        plans = [source.slot_plan(index) for index in (0, _PROCESSED_SLOTS-1, _OBSERVATION_SLOTS-1)]
        if (descriptor['source_samples'] > _MAXIMUM_SOURCE_SAMPLES or
                max(plan['receiver_window_samples'] for plan in plans) > _MAXIMUM_WINDOW_SAMPLES or
                descriptor['total_samples'] > _MAXIMUM_CAPTURE_SAMPLES):
            return dict(functional_passed=False, unsupported=True,
                reason='public_native_fixture_exceeds_fixed_sample_budget')
        first_slot = plans[0]
        manifest = _public_manifest(source, first_slot)
        source_identity = dict(native_binary_sha256=hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
            pattern_revision=descriptor['pattern_revision'],
            added_amplitude_depth=descriptor['added_amplitude_depth'],
            local_key_fixture_id=72001, public_case_hash=case['configuration_hash'])
        receiver = CompleteStreamReceiver(manifest, source, source_identity=source_identity,
            memory_limit_bytes=memory_limit_bytes, workers=workers,
            native_executable=executable, maximum_slots=_PROCESSED_SLOTS)
        first = checked = 0
        prefix = None
        generated_seconds = 0.
        while True:
            began = time.perf_counter()
            values, valid = source.next(16384)
            generated_seconds += max(0., time.perf_counter()-began)
            if len(values) == 0:
                receiver.finish_input()
                result['meaningful_failure'] = 'transport_ended_before_physical_gate'
                break
            if prefix is None:
                prefix = (values.copy(), valid.copy())
            row = receiver.push(first, values, valid)
            first += len(values)
            count = row['message']['slots_consumed']
            if count > checked:
                details = receiver.result()
                for slot in details['slots'][checked:]:
                    index = slot['index']
                    expected = 'DATA' if index < len(reference_bits) else 'QUIET'
                    if progress:
                        progress(f"message gate slot {index}: {slot['slot_classification']}")
                    if (slot['slot_classification'] != expected or
                            (index < len(reference_bits) and slot.get('decision_bit') != reference_bits[index])):
                        result['meaningful_failure'] = 'first_completed_slot_mismatch'
                        break
                checked = count
            if result['meaningful_failure'] or row['status'] != 'receiving':
                break
        result.update(result=receiver.result(), simulator_generation_seconds=generated_seconds,
            public_first_slot=first_slot, receiver_manifest=manifest,
            evaluator_ingested_samples=first,
            evaluator_public_capture_samples=descriptor['total_samples'])
        receiver.close()
        receiver = None
        final = result['result']
        result['expected_vs_observed'] = dict(reference_bits=list(reference_bits),
            observed_bits=final['message']['received_bits'], reference_text='e',
            observed_content=final['message']['decoded_content'])
        if not result['meaningful_failure'] and final['status'] == 'complete':
            # A real captured prefix is insufficient to score even the first
            # complete public bit window. EOF must leave this control incomplete.
            with CompleteStreamReceiver(manifest, source, source_identity=source_identity,
                    memory_limit_bytes=memory_limit_bytes, workers=workers,
                    native_executable=executable, maximum_slots=_PROCESSED_SLOTS) as control:
                control.push(0, *prefix)
                eof = control.finish_input()
                result['prefix_eof_control'] = {key: eof[key] for key in (
                    'status', 'reason', 'physical_complete', 'complete_receiver_work_covered',
                    'ingested_samples', 'decision_physical_stop_sample')}
        result['source_close'] = source.close()
        result['functional_passed'] = bool(not result['meaningful_failure'] and
            final['status'] == 'complete' and final['message']['received_bits'] == list(reference_bits) and
            final['message']['decoded_content']['filtered_text'] == 'e' and
            final['message']['trailing_quiet_slots'] == [3, 4] and
            not result['source_close']['complete'] and
            not result.get('prefix_eof_control', {}).get('physical_complete', True))
        return result
    except Exception as error:
        result.update(meaningful_failure='native_message_gate_runtime_error',
            error_type=type(error).__name__[:64])
        if receiver is not None:
            result['result'] = receiver.result()
        return result
    finally:
        if receiver is not None:
            receiver.close()
        if source is not None:
            source.close()


def run_native_message_gate(executable, memory_limit_bytes, workers=1, *,
                            case_list=None, progress=None):
    """Run the optional current-source native three-bit quiet-slot gate once.

    Call before other receiver workers start: the controller fixes one
    numerical thread per hypothesis worker. This function returns evidence;
    callers choose its temporary output path. A changed source/list/binary
    invalidates qualification even if the observed fixture decoded correctly.
    """
    from triad_cases import DEFAULT_CASES, load_cases
    from triad_case_suite import identity
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget

    parsed = load_cases(DEFAULT_CASES if case_list is None else case_list)
    if (isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or
            memory_limit_bytes < 1 or isinstance(workers, bool) or not isinstance(workers, int) or workers < 1):
        raise ValueError('positive local memory and worker bounds are required')
    executable = Path(executable).resolve()
    provenance = identity(executable)
    report = dict(schema='triad-native-message-gate-v1', status='running', passed=False,
        input={key:value for key,value in parsed.items() if key != 'cases'},
        provenance=provenance, workers=workers,
        policy=dict(purpose='One fixed high-SNR complete native short-message functional gate, not throughput or sensitivity qualification.',
            native_noise_cn0_db_hz=80., maximum_processed_slots=_PROCESSED_SLOTS,
            public_observation_slots=_OBSERVATION_SLOTS,
            maximum_candidate_plans=_MAXIMUM_CANDIDATE_PLANS,
            maximum_source_samples=_MAXIMUM_SOURCE_SAMPLES,
            maximum_window_samples=_MAXIMUM_WINDOW_SAMPLES,
            maximum_capture_samples=_MAXIMUM_CAPTURE_SAMPLES,
            stopping='First completed-slot mismatch, runtime/resource failure, or physical completion. No retry or campaign extension.',
            receiver_truth_boundary='Receiver gets public configuration, public slot plans and received samples only; expected bits/count/text/endpoint remain evaluator values.'))
    try:
        case, attempts = _choose_case(executable, parsed['cases'])
        report['selection'] = dict(candidate_plans=attempts,
            current_list_entries=len(parsed['cases']),
            scope='One bounded compatible current configuration; other manual configurations are not qualified by this gate.')
        if case is None:
            report.update(status='unsupported', reason='no_current_case_fits_fixed_functional_gate_budget')
        else:
            report['case'] = copy.deepcopy(case)
            allocation = resolve_cpu_budget(jobs=1, workers=workers, threads=1)
            if allocation['explicit_overcommit']:
                raise ValueError('message gate workers exceed the visible local CPU allocation')
            report['numerical_threads'] = configure_numerical_threads(allocation)
            report.update(_run_selected_case(executable, case,
                memory_limit_bytes=memory_limit_bytes, workers=workers, progress=progress))
            report['status'] = ('unsupported' if report.get('unsupported') else
                                'passed' if report.get('functional_passed') else 'failed')
    except Exception as error:
        report.update(status='failed', reason='message_gate_setup_failed',
                      error_type=type(error).__name__[:64])
    try:
        final = identity(executable)
        report['final_identity'] = final
        source_unchanged = (provenance['sources'] == final['sources'] and
            provenance['native_binary_sha256'] == final['native_binary_sha256'])
    except Exception as error:
        source_unchanged = False
        report['final_identity'] = dict(error_type=type(error).__name__[:64])
    try:
        input_unchanged = hashlib.sha256(Path(parsed['source_path']).read_bytes()).hexdigest() == parsed['source_sha256']
    except OSError as error:
        input_unchanged = False
        report['input']['final_read_error_type'] = type(error).__name__[:64]
    report['provenance']['unchanged_at_completion'] = source_unchanged
    report['input']['unchanged_at_completion'] = input_unchanged
    if not (source_unchanged and input_unchanged):
        report['status'] = 'invalidated'
    report['passed'] = bool(report['status'] == 'passed')
    return report
