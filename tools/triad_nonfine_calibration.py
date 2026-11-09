"""Local-only canonical non-fine measurement receipts for future replay.

This is a receipt primitive, not an estimator or capacity test. Capturing runs
one existing abbreviated complete-case measurement. Reuse requires paired
local runtime identity, actual work brackets, and separately validated replay
holdouts, state ceilings and workspace. No GPU elapsed time is admitted here.
Only trusted locally produced receipts are meaningful; hashes are identities,
not authentication. The receipt contains a hashed machine ID and paths and
must never be uploaded to another host.
"""
from __future__ import annotations

import copy
import math
import time

from triad_calibration_identity import capture_calibration_identity, compare_calibration_identities, _digest

SCHEMA = 'triad-local-nonfine-calibration-v1'
NOMINAL_HOLDOUT_RECEIPT_SCHEMA = 'triad-local-nominal-holdout-v1'
HOLDOUT_SCHEMA = 'triad-fixed-local-holdout-declaration-v1'
LOCAL_RECEIPT_LIMIT = 16*1024*1024
NONFINE_STAGES = frozenset(('recurring_bank_preparation', 'stream_controller',
    'quiet_state_and_canonical_codec', 'public_slot_planning',
    'receiver_worker_lifecycle', 'acquisition'))
ALL_STAGES = NONFINE_STAGES | {'complete_fine_search'}



def public_calibration_geometry(prepared):
    """Snapshot locally derived public work geometry; no evaluator truth.

    The consumer must independently reconstruct this from trusted public
    configuration, not accept it merely because a packet asserted it. Sample
    contents, realized channel, true transmitter position and message length
    are deliberately absent.
    """
    public = prepared.public_geometry
    coarse = public['coarse']
    geometry = dict(public_geometry={key:copy.deepcopy(public[key]) for key in (
            'sample_rate_hz', 'search_window_samples', 'search_window_seconds',
            'conservative_public_slot_interval_seconds', 'public_calendar',
            'fine_unit_samples', 'fine_unit_seconds', 'source_samples')},
        coarse_geometry={key:copy.deepcopy(coarse[key]) for key in (
            'epochs', 'public_arrival_prior_seconds', 'public_frequency_bound_hz',
            'public_clock_bank_ppm', 'bit_template_count', 'source_sample_rate_hz',
            'projected_sample_rate_hz', 'raw_source_samples', 'raw_capture_samples',
            'coarse_arrival_step_seconds')},
        sample_rate_hz=prepared.fs, search_samples=prepared.samples,
        unit_samples=prepared.unit_samples, slot_seconds=prepared.slot_seconds,
        memory_limit_bytes=prepared.memory_limit_bytes, noise_variance=prepared.noise_variance)
    # Reject NaN, infinities and non-JSON values rather than comparing them.
    _digest(geometry)
    return geometry

def _positive_int(value):
    return type(value) is int and value > 0


def read_local_receipt(path):
    import json
    from triad_replay_packet import read_bounded_file

    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate receipt field')
            result[key] = value
        return result

    def invalid_constant(_):
        raise ValueError('nonfinite receipt value')

    def finite_float(value):
        parsed = float(value)
        if not math.isfinite(parsed):
            raise ValueError('nonfinite receipt value')
        return parsed

    data = read_bounded_file(path, minimum=1, maximum=LOCAL_RECEIPT_LIMIT)
    result = json.loads(data, object_pairs_hook=unique, parse_constant=invalid_constant,
        parse_float=finite_float)
    if type(result) is not dict:
        raise ValueError('expected local receipt object')
    return result


def local_receipt_destination(path):
    """Resolve a new destination directly under the ignored evidence folder."""
    from pathlib import Path
    from triad_cases import ROOT

    path = Path(path)
    parent = ROOT / 'local-evidence'
    if path.parent.resolve(strict=True) != parent or not path.name:
        raise ValueError('output must be a new file directly inside local-evidence')
    destination = parent / path.name
    if destination.exists() or destination.is_symlink():
        raise ValueError('local receipt already exists')
    return destination


def write_local_receipt(path, receipt):
    import json
    from triad_replay_packet import write_new_file

    payload = json.dumps(receipt, separators=(',', ':'), allow_nan=False).encode() + b'\n'
    if len(payload) > LOCAL_RECEIPT_LIMIT:
        raise ValueError('local receipt exceeds the bounded read size')
    write_new_file(local_receipt_destination(path), payload)


def _boot_identity(receipt):
    return receipt['identity_before']['observation']['process_pair']['boot_id_sha256']


def declare_fixed_holdout(fit, replay, *, declared_ns, boot_id_sha256):
    """Bind one fixed holdout to exact prior trusted local receipts on this boot."""
    measured = fit['measurement']
    workers = measured.get('inference_workers', measured['selected_workers'])
    if (fit.get('schema') != SCHEMA or fit.get('status') != 'recorded_unqualified' or
            measured.get('measurement_scope') != 'full_capacity' or
            measured.get('inference_allocation_mode', 'pilot_selected') != 'pilot_selected' or
            replay.get('schema') != 'triad-local-fine-replay-measurement-v1' or
            replay.get('status') != 'measured_detached_fine' or
            fit['case_index'] != replay['case_index'] or not _positive_int(workers) or
            workers != replay['workers'] or not _positive_int(declared_ns) or
            not all(_positive_int(row[key]) for row in (fit,replay)
                for key in ('work_started_ns','work_ended_ns')) or
            not fit['work_started_ns'] < fit['work_ended_ns'] < replay['work_started_ns'] < replay['work_ended_ns'] < declared_ns or
            _boot_identity(fit) != boot_id_sha256 or _boot_identity(replay) != boot_id_sha256):
        raise ValueError('fixed holdout requires prior same-boot matched fit and replay at one allocation')
    return dict(schema=HOLDOUT_SCHEMA, case_index=fit['case_index'], workers=workers,
        fit_sha256=_digest(fit), replay_sha256=_digest(replay), boot_id_sha256=boot_id_sha256,
        declared_ns=declared_ns)


def validate_holdout_declaration(fit, replay, holdout):
    """Verify exact inputs, same-boot chronology and fresh fixed inference."""
    try:
        binding = holdout['holdout_declaration']
        expected = declare_fixed_holdout(fit, replay, declared_ns=binding['declared_ns'],
            boot_id_sha256=_boot_identity(holdout))
        measurement = holdout['measurement']
        actual = holdout['identity_before']['identity']['workers']
        return (holdout.get('schema') == NOMINAL_HOLDOUT_RECEIPT_SCHEMA and
            binding == expected and binding['case_index'] == holdout['case_index'] and
            measurement.get('measurement_scope') == 'nominal_holdout' and
            actual == binding['workers'] == measurement['inference_workers'] and
            measurement['inference_allocation_mode'] == 'fixed_holdout' and
            _positive_int(holdout['pilot_started_ns']) and
            binding['declared_ns'] < holdout['pilot_started_ns'] < holdout['work_started_ns'])
    except (KeyError, TypeError, ValueError, OverflowError):
        return False


def _record_helper_predeclaration(receipt, fit_signal, verification):
    """Record diagnostic evidence and reject a different independent signal.

    This does not verify ``verification`` or admit a result for assessment;
    the assessor must recompute the Helper from the original evidence.
    """
    if verification is not None:
        receipt['local_helper_predeclaration'] = copy.deepcopy(verification)
        if receipt.get('measurement', {}).get('signal_work') != fit_signal:
            receipt.update(status='unresolved', reason='held_out_local_signal_work_differs')
    return receipt


def _bracket(before, after, started, ended):
    try:
        return (_positive_int(started) and _positive_int(ended) and
            before['observation']['ended_ns'] < started < ended <
            after['observation']['started_ns'])
    except (KeyError, TypeError):
        return False


def _allocation_covered(measurement, identity):
    try:
        allocation = measurement['allocation']
        workers = identity['workers']
        budget = identity['cpu']['budget']
        topology = budget['topology']
        required = set(budget['receiver_worker_candidates'])
        required.update((budget['available_cpus'], min(topology['physical_core_count'], budget['available_cpus']), workers))
        samples = allocation['samples']
        if not isinstance(samples, list) or any(not _positive_int(row['workers']) for row in samples):
            return False
        actual = [row['workers'] for row in samples]
        selected = measurement['selected_workers']
        actual_inference = measurement.get('inference_workers', selected)
        mode = measurement.get('inference_allocation_mode', 'pilot_selected')
        return (_positive_int(selected) and _positive_int(allocation['selected_workers']) and
            selected == allocation['selected_workers'] and _positive_int(actual_inference) and
            actual_inference == workers and mode in ('pilot_selected', 'fixed_holdout') and
            (mode == 'fixed_holdout' or selected == actual_inference) and
            len(actual) == len(set(actual)) and set(actual) == required and
            all(not isinstance(row.get('selection_ratio'), bool) and
                isinstance(row.get('selection_ratio'), (int, float)) and
                math.isfinite(row['selection_ratio']) and row['selection_ratio'] >= 0
                for row in samples if row['workers'] in required) and
            min(samples, key=lambda row: row['selection_ratio'])['workers'] == selected and
            allocation['inference_samples_reused'] is False)
    except (KeyError, TypeError):
        return False


def _measurement_problem(measurement, identity, *, nominal_holdout=False):
    from triad_case_capacity import CEILING_ONLY_RECEIPTS, FULL_MEASUREMENT_SCOPE, NOMINAL_HOLDOUT_SCOPE

    status, scope = (('measured_nominal_holdout', NOMINAL_HOLDOUT_SCOPE) if nominal_holdout
        else ('measured_complete_work_estimate', FULL_MEASUREMENT_SCOPE))
    try:
        if measurement['status'] != status or measurement['measurement_scope'] != scope:
            return 'measurement_scope_mismatch'
        if nominal_holdout:
            if (measurement['inference_allocation_mode'] != 'fixed_holdout' or
                    measurement.get('ceiling_only_receipts_omitted') != list(CEILING_ONLY_RECEIPTS) or
                    any(key in measurement for key in ('complete_capacity', *CEILING_ONLY_RECEIPTS))):
                return 'nominal_holdout_scope_mismatch'
        elif measurement['inference_allocation_mode'] != 'pilot_selected':
            return 'fit_requires_full_pilot_selected_measurement'
        if not _allocation_covered(measurement, identity):
            return 'same_stream_allocation_coverage_missing'
        names = ('nominal_service',) if nominal_holdout else ('nominal_service', 'complete_capacity')
        for name in names:
            components = measurement[name]['components']
            if set(components) != ALL_STAGES or any(not isinstance(row, dict) for row in components.values()):
                return name + '_stages_missing'
        workspace = measurement['workspace_margin']
        if (not isinstance(workspace, dict) or workspace.get('status') not in ('within_budget', 'failure') or
                workspace.get('measurement_scope') != scope):
            return 'workspace_measurement_missing'
        for key in ('estimated_peak_bytes', 'budget_bytes', 'conservative_peak_bytes'):
            value = workspace.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                return 'workspace_measurement_missing'
        if not isinstance(measurement['fine_active'].get('observations'), list):
            return 'resident_fine_work_missing'
        work_receipts = ('nominal_acquisition', 'orchestration', 'recurring_banks')
        if not nominal_holdout:
            work_receipts += CEILING_ONLY_RECEIPTS
        for key in work_receipts:
            if not isinstance(measurement[key], dict):
                return key + '_receipt_missing'
    except (KeyError, TypeError):
        return 'measurement_receipt_incomplete'
    return None


def capture_nonfine_calibration(prepared, allocations, *, declared_workers, family_size,
                                exporter, case_list=None, allow_conditional=False,
                                progress=print, holdout_declaration=None, sustained_targets=None,
                                suite_list_sha256=None):
    """Run the bounded canonical fit or independent nominal holdout with paired identity.

    The canonical pilot compares physical and full logical allocations. The
    selected allocation is bound to the local identity before fresh inference.
    An explicit ``declared_workers`` mismatch stops after the pilot. In fixed
    holdout mode the full pilot is still recorded, and fresh inference uses
    the allocation independently predeclared from the calibration fit, through
    every nominal stage but without repeating the separate ceiling inference.
    Actual receiver preparation occurs before entry.
    This consumes and mutates the prepared signal and bank; close it after
    return and never reuse it for later receiver work. This function does not
    authenticate serialized data or qualify prediction.

    A suite caller binds its originally parsed list and keeps the canonical
    measurement even if a calibration-only prerequisite is unavailable.
    """
    from triad_case_capacity import PreparedCapacityCase, measure_complete_case, measurement_policy
    from triad_cases import DEFAULT_CASES, load_cases
    from triad_receiver import _public_metrics
    fixed_holdout = holdout_declaration is not None
    suite = suite_list_sha256 is not None
    if (type(prepared) is not PreparedCapacityCase or (fixed_holdout and sustained_targets is not None) or
            (suite and (fixed_holdout or declared_workers is not None)) or
            (declared_workers is not None and not _positive_int(declared_workers)) or
            (fixed_holdout and (type(holdout_declaration) is not dict or
                holdout_declaration.get('schema') != HOLDOUT_SCHEMA or
                not _positive_int(declared_workers) or holdout_declaration.get('workers') != declared_workers or
                holdout_declaration.get('case_index') != prepared.case['index'])) or
            not _positive_int(family_size) or type(allow_conditional) is not bool or
            not isinstance(allocations, (tuple, list)) or not allocations or
            any(not _positive_int(n) for n in allocations) or len(set(allocations)) != len(allocations)):
        return dict(schema=SCHEMA, status='unresolved', reason='invalid_measurement_arguments')

    def measure(**observation):
        return measure_complete_case(prepared, allocations, family_size=family_size,
            progress=progress, fixed_holdout_workers=declared_workers if fixed_holdout else None,
            sustained_targets=sustained_targets, **observation)

    def before_work_unresolved(reason, **details):
        receipt = dict(schema=SCHEMA, status='unresolved', reason=reason, **details)
        if suite:
            receipt['measurement'] = _public_metrics(measure())
        return receipt

    try:
        parsed = load_cases(DEFAULT_CASES if case_list is None else case_list)
        policy = measurement_policy(len(parsed['cases']), prepared.unit_samples / prepared.fs)
    except (KeyError, ValueError, TypeError, OSError, ZeroDivisionError):
        return before_work_unresolved('manual_list_or_policy_unavailable')
    if suite and suite_list_sha256 != parsed['source_sha256']:
        return before_work_unresolved('manual_list_changed_before_calibration')
    if family_size != policy['family_size']:
        return before_work_unresolved('family_size_does_not_match_full_manual_list')
    provisional = declared_workers if declared_workers is not None else max(allocations)
    kwargs = dict(exporter=exporter, configuration_hash=prepared.case['configuration_hash'],
        workers=provisional, native_service=prepared.basis_source, case_list=case_list,
        allow_conditional=allow_conditional)
    try:
        geometry = public_calibration_geometry(prepared)
        resident_bank = dict(profile=copy.deepcopy(prepared.bank.public_bank_profile),
                             work=copy.deepcopy(prepared.bank.public_bank_work))
        bank_digest = _digest(resident_bank)
    except (AttributeError, KeyError, TypeError, ValueError, OverflowError):
        return before_work_unresolved('public_geometry_unavailable')
    def usable(identity, workers):
        return (identity.get('status') in ('available', 'conditional') and
            identity.get('identity', {}).get('workers') == workers and
            identity['identity'].get('manual_list_sha256') == parsed['source_sha256'])

    before = capture_calibration_identity(**kwargs)
    if not usable(before, provisional):
        return before_work_unresolved('pre_measurement_identity_unavailable', identity_before=before)
    if fixed_holdout and (not _positive_int(holdout_declaration.get('declared_ns')) or
            holdout_declaration['declared_ns'] >= before['observation']['started_ns'] or
            holdout_declaration.get('boot_id_sha256') != _boot_identity({'identity_before':before})):
        return dict(schema=SCHEMA, status='unresolved', reason='fixed_holdout_declaration_timing_invalid',
            identity_before=before)
    started = None
    binding_problem = None

    class SelectionUnresolved(RuntimeError):
        pass

    def bind_selected(workers):
        nonlocal before, started, binding_problem
        if declared_workers is not None and workers != declared_workers:
            raise SelectionUnresolved('declared_worker_selection_changed', workers)
        if workers != provisional:
            kwargs['workers'] = workers
            selected = capture_calibration_identity(**kwargs)
            if (not usable(selected, workers) or before['status'] != selected['status'] or
                    before['identity'] != dict(selected['identity'], workers=provisional) or
                    any(before['observation'][key] != selected['observation'][key]
                        for key in ('process_pair', 'host_maps_sha256', 'native_maps_sha256')) or
                    before['observation']['ended_ns'] >= selected['observation']['started_ns']):
                if suite:
                    binding_problem = 'local_identity_changed_during_allocation_selection'
                    return
                raise SelectionUnresolved('local_identity_changed_during_allocation_selection', workers)
            before = selected
        started = time.monotonic_ns()

    pilot_started_ns = time.monotonic_ns() if fixed_holdout else None
    try:
        measurement = measure(record_signal_work=True, on_inference_allocation=bind_selected)
    except SelectionUnresolved as error:
        reason, selected_workers = error.args
        return dict(schema=SCHEMA, status='unresolved', reason=reason, selected_workers=selected_workers,
            identity_before=before)
    except Exception as error:
        if suite:
            raise
        return dict(schema=SCHEMA, status='unresolved', reason='canonical_measurement_failed',
                    error_type=type(error).__name__, identity_before=before)
    if started is None:
        return dict(schema=SCHEMA, status='unresolved',
            reason=binding_problem or measurement.get('reason', 'worker_allocation_unresolved'),
            allocation=measurement.get('allocation'),
            **({'measurement': _public_metrics(measurement)} if suite else {}))
    ended = time.monotonic_ns()
    measurement = _public_metrics(measurement)
    after = capture_calibration_identity(**kwargs)
    receipt = dict(schema=NOMINAL_HOLDOUT_RECEIPT_SCHEMA if fixed_holdout else SCHEMA,
        status='unresolved', case_index=prepared.case['index'],
        identity_before=before, identity_after=after,
        work_started_ns=started, work_ended_ns=ended, measurement=measurement,
        public_geometry=geometry, public_geometry_sha256=_digest(geometry),
        resident_bank=resident_bank, resident_bank_sha256=bank_digest,
        policy=policy, policy_sha256=_digest(policy),
        scope=('Canonical independent nominal resident work and local identity only. Separate ceiling work is omitted.'
            if fixed_holdout else 'Canonical abbreviated complete-case work and local identity only. No replay, prediction, or capacity qualification.'))
    if fixed_holdout:
        receipt.update(holdout_declaration=copy.deepcopy(holdout_declaration), pilot_started_ns=pilot_started_ns)
    match = compare_calibration_identities(before, before, recorded_after=after, current_after=after,
                                           allow_conditional=allow_conditional)
    try:
        geometry_unchanged = (public_calibration_geometry(prepared) == geometry and
            dict(profile=prepared.bank.public_bank_profile, work=prepared.bank.public_bank_work) == resident_bank)
    except (AttributeError, KeyError, TypeError, ValueError, OverflowError):
        geometry_unchanged = False
    if not geometry_unchanged:
        receipt['reason'] = 'public_geometry_changed'
    elif not _bracket(before, after, started, ended):
        receipt['reason'] = 'actual_work_outside_identity_observations'
    elif match['status'] not in ('matched', 'conditionally_matched'):
        receipt['reason'] = 'measurement_identity_changed_or_unavailable'
    else:
        problem = _measurement_problem(measurement, before['identity'], nominal_holdout=fixed_holdout)
        if problem:
            receipt['reason'] = problem
        else:
            receipt['status'] = 'recorded_unqualified'
            receipt['identity_match'] = match
    return receipt


def compare_nonfine_calibration(receipt, current_before, current_after, *,
                                current_work_started_ns, current_work_ended_ns, current_geometry, current_policy,
                                allow_conditional=False, nominal_holdout=False):
    """Match a full fit or independent nominal holdout with local replay identity.

    ``current_policy`` must be independently recomputed using the freshly
    parsed full manual list and current unit geometry. Returns non-fine
    component copies only as *unqualified inputs*. A consumer
    must still independently verify detached-vs-resident fine work and timing,
    held-out error, state/workspace bounds and stage model support. This API
    deliberately never emits a capacity pass or GPU-to-CPU timing conversion.
    """
    schema = NOMINAL_HOLDOUT_RECEIPT_SCHEMA if nominal_holdout else SCHEMA
    if not isinstance(receipt, dict) or receipt.get('schema') != schema or receipt.get('status') != 'recorded_unqualified':
        return dict(status='unresolved', reason='local_calibration_receipt_unavailable')
    try:
        before, after = receipt['identity_before'], receipt['identity_after']
        measurement = receipt['measurement']
        if (not isinstance(current_geometry, dict) or
                _digest(receipt['public_geometry']) != receipt['public_geometry_sha256'] or
                current_geometry != receipt['public_geometry'] or
                _digest(current_geometry) != receipt['public_geometry_sha256'] or
                _digest(receipt['resident_bank']) != receipt['resident_bank_sha256'] or
                _digest(receipt['policy']) != receipt['policy_sha256'] or
                not isinstance(current_policy, dict) or current_policy != receipt['policy']):
            return dict(status='unresolved', reason='public_geometry_mismatch')
        if not _bracket(before, after, receipt['work_started_ns'], receipt['work_ended_ns']):
            return dict(status='unresolved', reason='recorded_work_outside_identity_observations')
        if not _bracket(current_before, current_after, current_work_started_ns, current_work_ended_ns):
            return dict(status='unresolved', reason='current_work_outside_identity_observations')
        match = compare_calibration_identities(before, current_before, recorded_after=after,
            current_after=current_after, allow_conditional=allow_conditional)
        if match['status'] not in ('matched', 'conditionally_matched'):
            return dict(status='unresolved', reason='local_identity_mismatch', identity_match=match)
        problem = _measurement_problem(measurement, before['identity'], nominal_holdout=nominal_holdout)
        if problem:
            return dict(status='unresolved', reason=problem)
        names = ('nominal_service',) if nominal_holdout else ('nominal_service', 'complete_capacity')
        result = {name: {stage: copy.deepcopy(row) for stage, row in
                    measurement[name]['components'].items() if stage in NONFINE_STAGES}
                  for name in names}
        return dict(status='locally_matched_unqualified', identity_match=match, components=result,
            resident_fine=copy.deepcopy(measurement['fine_active']),
            workspace=copy.deepcopy(measurement['workspace_margin']),
            resident_bank=copy.deepcopy(receipt['resident_bank']),
            **({} if nominal_holdout else {'separate_ceiling':copy.deepcopy(measurement['fine_public_state_ceiling'])}),
            policy=copy.deepcopy(receipt['policy']),
            scope='No validated detached replay, held-out prediction, acquisition-realization equivalence, complete receiver capacity or memory qualification.')
    except (KeyError, TypeError, ValueError, OverflowError):
        return dict(status='unresolved', reason='local_calibration_receipt_invalid')


def main(argv=None):
    """Measure one explicit manual row using the canonical abbreviated path.

    The output is a sensitive local-only receipt, never a capacity result.
    Select a workspace cap that also fits future local replay; the caller must
    check current available memory separately. No remote receipt is accepted.
    """
    import argparse
    import json
    from pathlib import Path
    from triad_cases import DEFAULT_CASES, load_cases
    from triad_case_suite import native_plan, select_cases
    from triad_case_capacity import PreparedCapacityCase, measurement_policy, sustained_allocation_targets
    from triad_case_metrics import machine_resources
    from triad_case_replay import _clean_options
    from triad_continuous import NativeStream
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case-list', type=Path, default=DEFAULT_CASES)
    parser.add_argument('--case-index', type=int, required=True)
    parser.add_argument('--workers', type=int,
        help='require the fit pilot selection to match; omit to bind the measured selection automatically')
    parser.add_argument('--holdout-fit', type=Path,
        help='prior trusted local fit receipt used to predeclare the holdout allocation')
    parser.add_argument('--holdout-replay', type=Path,
        help='prior trusted local detached replay receipt paired with --holdout-fit')
    parser.add_argument('--helper-envelope', type=Path,
        help='bounded Helper-prefix envelope from the authenticated producer')
    parser.add_argument('--helper-attestation', type=Path,
        help='separately authenticated original acquisition attestation for the envelope')
    parser.add_argument('--sustained-allocation-probe', action='store_true',
        help='bounded exploratory continued fine work at physical and full logical allocations in a full fit')
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--workspace-mib', type=int,
        help='stable local workspace ceiling; defaults to visible budget rounded down to 256 MiB')
    parser.add_argument('--allow-conditional', action='store_true',
        help='explicitly admit conditional local executable mapping identity')
    parser.add_argument('--output', type=Path, required=True,
        help='new local-evidence filename; never upload this local identity receipt')
    args = parser.parse_args(argv)
    declared_workers = args.workers
    binding = None
    helper_predeclaration = None
    helper_pending = False
    try:
        if ((args.holdout_fit is None) != (args.holdout_replay is None) or
                (args.holdout_fit is not None and (args.workers is not None or args.sustained_allocation_probe)) or
                ((args.helper_envelope is None) != (args.helper_attestation is None)) or
                (args.helper_envelope is not None and args.holdout_fit is None)):
            raise ValueError('supply both holdout receipts together and omit fit-only options')
        output = local_receipt_destination(args.output)
        parsed = load_cases(args.case_list)
        case = select_cases(parsed, [args.case_index])[0]
        holdout_cap = None
        if args.holdout_fit is not None:
            from triad_calibration_identity import _hash_file
            from triad_case_capacity import validate_signal_acquisition_work
            from triad_case_metrics import LOOKS
            from triad_replay_producer import canonical_digest, source_identity_sha256
            fit, replay = (read_local_receipt(path) for path in (args.holdout_fit, args.holdout_replay))
            geometry = replay['public_calibration_geometry']
            policy = measurement_policy(len(parsed['cases']), geometry['unit_samples']/geometry['sample_rate_hz'])
            local_match = compare_nonfine_calibration(fit, replay['identity_before'], replay['identity_after'],
                current_work_started_ns=replay['work_started_ns'], current_work_ended_ns=replay['work_ended_ns'],
                current_geometry=geometry, current_policy=policy, allow_conditional=args.allow_conditional)
            if (local_match['status'] != 'locally_matched_unqualified' or replay['case_index'] != case['index'] or
                    replay['configuration_sha256'] != case['configuration_hash'] or
                    replay['manual_list_sha256'] != parsed['source_sha256'] or
                    replay['native_exporter_sha256'] != _hash_file(args.exporter) or
                    replay['source_identity_sha256'] != source_identity_sha256()):
                raise ValueError('holdout fit and replay do not match current case, source and local evidence')
            signal = replay.get('signal_work')
            fit_signal = fit['measurement'].get('signal_work')
            active = fit['measurement']['nominal_acquisition']
            count = len(active.get('samples', ()))
            if (not validate_signal_acquisition_work(signal) or
                    replay.get('signal_work_sha256') != canonical_digest(signal) or
                    not validate_signal_acquisition_work(fit_signal) or count not in LOOKS or
                    active.get('actual_work_match_count') != count or
                    signal['sample_count'] != geometry['search_samples'] or
                    signal['fine_slice_sha256'] != replay['acquisition']['input_sha256'] or
                    signal['acquisition']['coordinates'] != replay['acquisition']['coordinates'] or
                    signal['acquisition']['receiver_workers'] != replay['workers']):
                raise ValueError('holdout fit and replay do not cover the same actual acquired work')
            if signal != fit_signal and args.helper_envelope is None:
                raise ValueError('holdout fit and replay require verified local Helper evidence')
            # Explicit evidence requests verification even for an exact Helper
            # match, so the portable path can be exercised on a matching host.
            helper_pending = args.helper_envelope is not None
            declared_workers = fit['measurement'].get('inference_workers', fit['measurement']['selected_workers'])
            holdout_cap = geometry['memory_limit_bytes']
        machine = machine_resources()
        cpu = machine['cpu']; available = machine['memory_budget_bytes']
        if (not isinstance(cpu, dict) or not _positive_int(available) or
                (declared_workers is not None and
                 (not _positive_int(declared_workers) or declared_workers not in cpu['receiver_worker_candidates']))):
            raise ValueError('visible CPU/memory budget and a valid declared worker candidate are required')
        if args.workspace_mib is not None and not _positive_int(args.workspace_mib):
            raise ValueError('workspace MiB must be a positive integer')
        cap = (args.workspace_mib * 2**20 if args.workspace_mib is not None else
            holdout_cap if holdout_cap is not None else (available // (256*2**20)) * (256*2**20))
        if not _positive_int(cap) or cap > available or (holdout_cap is not None and cap != holdout_cap):
            raise ValueError('requested stable workspace exceeds the visible local receiver budget')
        allocations = cpu['receiver_worker_candidates']
        configure_numerical_threads(resolve_cpu_budget(jobs=1, workers=max(allocations), threads=1))
        executable = args.exporter.resolve(strict=True)
        # Verify local identity feasibility before native full-bank preparation
        # or simulated capture. This separate ready-only process never supplies
        # the measurement identity, which is paired around actual work below.
        command = _clean_options(executable, case['native_args'], bit=0, seed=74021)
        probe = NativeStream(executable, command[1:], kind='channel')
        try:
            initial_identity = capture_calibration_identity(exporter=executable,
                configuration_hash=case['configuration_hash'], workers=declared_workers or max(allocations),
                native_service=probe, case_list=args.case_list, allow_conditional=args.allow_conditional)
        finally:
            probe.close()
        if initial_identity.get('status') not in ('available', 'conditional'):
            raise ValueError('ready-only local runtime identity preflight unresolved; no full receiver measurement was started')
        if args.holdout_fit is not None and (initial_identity.get('status') != replay['identity_before']['status'] or
                initial_identity.get('identity_sha256') != replay['identity_before']['identity_sha256']):
            raise ValueError('current local runtime does not match the predeclared replay; no holdout was started')
        if args.holdout_fit is not None:
            if helper_pending:
                from triad_replay_cpu import _read_attestation
                from triad_replay_estimate import _verify_local_helper_evidence
                helper_predeclaration = _verify_local_helper_evidence(case=case, parsed=parsed,
                    exporter=executable, replay=replay, calibration=fit, holdout=None,
                    allow_conditional=args.allow_conditional,
                    evidence=dict(envelope=args.helper_envelope,
                        attestation=_read_attestation(args.helper_attestation)))
                if helper_predeclaration != replay.get('local_helper_prefix_verification'):
                    raise ValueError('live Helper verification differs from the measured replay')
            binding = declare_fixed_holdout(fit, replay, declared_ns=time.monotonic_ns(),
                boot_id_sha256=_hash_file('/proc/sys/kernel/random/boot_id'))
            if declared_workers != binding['workers']:
                raise ValueError('holdout allocation differs from the predeclared fit')
        plan = native_plan(executable, case['native_args'])
        if not plan.get('geometry_feasible'):
            raise ValueError('native public geometry is infeasible')
        policy = measurement_policy(len(parsed['cases']), 1.)
        with PreparedCapacityCase(executable, case, plan, memory_limit_bytes=cap,
                                  measurement_seconds=1.) as prepared:
            receipt = capture_nonfine_calibration(prepared, allocations,
                declared_workers=declared_workers, family_size=policy['family_size'], exporter=executable,
                case_list=args.case_list, allow_conditional=args.allow_conditional,
                holdout_declaration=binding,
                sustained_targets=sustained_allocation_targets(cpu) if args.sustained_allocation_probe else None,
                progress=lambda line: print(line, flush=True))
        if binding is not None:
            receipt['schema'] = NOMINAL_HOLDOUT_RECEIPT_SCHEMA
            receipt.setdefault('holdout_declaration', binding)
            _record_helper_predeclaration(receipt, fit_signal, helper_predeclaration)
        # Receipt includes local machine identity and must remain private. Use
        # exclusive creation, a restrictive mode and only the evidence folder.
        write_local_receipt(output, receipt)
        print(json.dumps(dict(status=receipt['status'], output=str(args.output),
                              scope='Local-only unqualified calibration receipt; no capacity prediction.')))
        return 0 if receipt['status'] == 'recorded_unqualified' else 2
    except (AttributeError, EOFError, ValueError, TypeError, KeyError, RuntimeError,
            OSError, OverflowError, RecursionError, MemoryError) as error:
        print(json.dumps(dict(status='unresolved', reason=str(error))))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
