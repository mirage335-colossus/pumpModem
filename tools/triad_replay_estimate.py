"""Validate a detached fine replay against independent local complete-work receipts."""
from __future__ import annotations

import copy
import hashlib
import math
from pathlib import Path

import numpy as np

from triad_case_capacity import combine_components, measurement_policy, validate_signal_acquisition_work
from triad_case_metrics import LOOKS, machine_resources
from triad_gpu_workload import SCORE_ATOL, SCORE_RTOL
from triad_nonfine_calibration import (compare_nonfine_calibration, read_local_receipt as _read_local_receipt,
    validate_holdout_declaration)
from triad_replay_cpu import SCHEMA as REPLAY_SCHEMA
from triad_replay_producer import canonical_digest, source_identity_sha256


HOLDOUT_RELATIVE_ERROR_LIMIT = .15


def _unresolved(reason):
    return dict(status='unresolved', reason=reason,
        complete_receiver_capacity_established=False)


def _independent_work(*receipts):
    controllers, native_services, intervals = set(), set(), []
    for receipt in receipts:
        observed = receipt['identity_before']['observation']['process_pair']
        boot = observed['boot_id_sha256']
        controller = boot, tuple(observed['controller'])
        native = boot, tuple(observed['native'])
        if controller in controllers or native in native_services:
            return False
        controllers.add(controller)
        native_services.add(native)
        intervals.append((boot, receipt['work_started_ns'], receipt['work_ended_ns']))
    for i, (boot_id, first, stop) in enumerate(intervals):
        if any(other_boot == boot_id and not (stop <= other_first or other_stop <= first)
                for other_boot, other_first, other_stop in intervals[i+1:]):
            return False
    return True


def _fine_work(fine):
    """Use integer work and retained states for matching, and compare scores separately."""
    if (fine['status'] != 'measured_continuation' or
            fine['continuation_checkpoints_seconds'] != [1, 2, 4] or
            fine['prefix_baseband_seconds'] != 2. or fine['public_probe_samples'] != 36000 or
            len(fine['observations']) not in LOOKS or
            len(fine['observations']) != fine['successful_outer_observations']):
        raise ValueError('supported measured continued fine work required')
    observations = []
    for row in fine['observations']:
        if (row['status'] != 'complete_work_observation' or not row['geometry']['supported'] or
                not row['continued_cache_boundary_work']['supported'] or
                row['finish_classify_close']['complete_six_second_probe'] is not True or
                row['finish_classify_close']['endpoint'] != 'AMBIGUOUS'):
            raise ValueError('incomplete representative continued fine work')
        checkpoints = [row['prefix'], *row['checkpoints']]
        if len(checkpoints) != 4:
            raise ValueError('four actual work checkpoints required')
        work, scores = [], []
        for stage, rows in zip(checkpoints, (12000, 18000, 24000, 36000)):
            counters = copy.deepcopy(stage['counters'])
            if counters['processed_rows'] != rows or counters['consumed_samples'] != rows:
                raise ValueError('fine checkpoint row count differs')
            if (set(counters['lanes']) != {'initial_affine', 'adaptive'} or
                    set(counters['retained_states']) != set(counters['lanes']) or
                    not all(counters['retained_states'].values())):
                raise ValueError('fine checkpoint retained-state work is missing')
            counts = counters['work_counts']
            if (set(counts) != {'transition_edges','hypothesis_update_tasks','candidate_block_updates',
                    'merged_channel_components','projection_mode_updates','basis_rows'} or
                    any(type(count) is not int or count < 0 for count in counts.values()) or
                    counts['basis_rows'] < 1 or counts['candidate_block_updates'] < 1):
                raise ValueError('fine checkpoint actual work counts are missing')
            powers = counters['native_templates']
            if any(type(powers[name]) is not int or powers[name] < 1
                    for name in ('calls','returned_elements')):
                raise ValueError('fine checkpoint native template counts are missing')
            powers = counters['native_templates']
            energy = powers.pop('energy')
            if type(energy) not in (int, float) or not math.isfinite(energy) or energy <= 0:
                raise ValueError('no representative native template work')
            for name in ('adaptive', 'initial_affine'):
                values = counters['lanes'][name].pop('bit_scores')
                if np.asarray(values).shape != (2,) or not np.isfinite(values).all():
                    raise ValueError('finite fine checkpoint scores required')
                scores.extend(values)
            work.append(counters)
        observations.append((work, scores))
    work, scores = observations[0]
    if not all(w == work and np.allclose(s, scores, atol=SCORE_ATOL, rtol=SCORE_RTOL)
               for w, s in observations):
        raise ValueError('fine checkpoint state or work drift')
    return ((fine['public_origin_sample'], fine['requested_center_sample'], fine['workers'], work), scores)


def _same_fine_work(*measurements):
    reference, ref_scores = _fine_work(measurements[0])
    for measurement in measurements[1:]:
        current, scores = _fine_work(measurement)
        if current != reference or not np.allclose(scores, ref_scores, atol=SCORE_ATOL, rtol=SCORE_RTOL):
            return False
    return True


def _record_point_failures(result, **samples):
    key = 'ratio_processing_seconds_per_baseband_second'
    failures = {label:sample[key] for label, sample in samples.items()
        if sample['complete_work_covered'] and type(sample.get(key)) in (int,float) and
            math.isfinite(sample[key]) and sample[key] >= 1}
    if failures:
        result['numeric_point_criterion'] = 'fail'
        result['failed_nominal_points_processing_seconds_per_baseband_second'] = failures
    return result


def _qualified_nominal(prediction, actual):
    """Require a held-out complete nominal point and a supported stage model."""
    key = 'ratio_processing_seconds_per_baseband_second'
    points = prediction.get(key), actual.get(key)
    def reject(reason, **details):
        return _record_point_failures(dict(_unresolved(reason), **details),
            candidate_replay_nominal=prediction, independent_resident_nominal=actual)

    if (not all(row['complete_work_covered'] and row['all_component_models_supported']
                for row in (prediction, actual)) or
            not all(type(point) in (float,int) and math.isfinite(point) and point > 0 for point in points)):
        return reject('complete_nominal_service_model_unresolved')
    estimate, observed = points
    error = abs(estimate/observed - 1)
    if error > HOLDOUT_RELATIVE_ERROR_LIMIT or (estimate < 1 <= observed):
        return reject('held_out_complete_nominal_prediction_failed', holdout_relative_error=error)
    bounds_key = key+'_interval'
    estimate_bounds, observed_bounds = prediction[bounds_key], actual[bounds_key]
    if (not isinstance(estimate_bounds, list) or len(estimate_bounds) != 2 or
            not isinstance(observed_bounds, list) or len(observed_bounds) != 2 or
            not all(type(n) in (float,int) and math.isfinite(n) and n >= 0
                    for n in (*estimate_bounds,*observed_bounds)) or
            not estimate_bounds[0] <= estimate <= estimate_bounds[1] or
            not observed_bounds[0] <= observed <= observed_bounds[1]):
        return reject('complete_nominal_uncertainty_unavailable')
    allowance = max(estimate/observed, observed/estimate)
    low = min(estimate_bounds[0]/allowance, observed_bounds[0])
    high = max(estimate_bounds[1]*allowance, observed_bounds[1])
    return dict(status='validated_conditional_nominal_service',
        complete_receiver_capacity_established=False,
        nominal_service={key: estimate, bounds_key: [low, high],
            'margin_fraction':1-estimate, 'margin_interval':[1-high, 1-low],
            'numeric_point_criterion':'fail' if estimate >= 1 else 'below_one',
            'realtime_status':'fail' if estimate >= 1 else 'pass' if high < 1 else 'unresolved'},
        holdout=dict(actual_nominal_ratio=observed, relative_error=error,
            error_allowance=allowance, predeclared_relative_error_limit=HOLDOUT_RELATIVE_ERROR_LIMIT),
        separate_worst_state_capacity='unresolved_from_replay',
        scope='Exact matched nominal replay and independent locally timed abbreviated resident receiver. '
              'The interval includes observed model discrepancy and the held-out interval. '
              'This repeated timing covers the exact sampled signal realization and local allocation.')


def _current_runtime_matches(case, parsed, exporter, workers, recorded, machine, allow_conditional):
    """Check the current controller and an idle native basis process without DSP."""
    from contextlib import closing
    from triad_calibration_identity import capture_calibration_identity
    from triad_case_replay import _clean_options
    from triad_continuous import NativeStream
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget

    if type(workers) is not int or workers < 1 or machine['cpu'] is None:
        return False
    allocation = resolve_cpu_budget(jobs=1, workers=workers, threads=1, capacity=machine['cpu'])
    if allocation['explicit_overcommit']:
        return False
    configure_numerical_threads(allocation)
    executable = Path(exporter).resolve(strict=True)
    command = _clean_options(executable, case['native_args'], bit=0, seed=74021)
    with closing(NativeStream(executable, command[1:], kind='channel')) as source:
        current = capture_calibration_identity(exporter=executable,
            configuration_hash=case['configuration_hash'], workers=workers, native_service=source,
            case_list=parsed['source_path'], allow_conditional=allow_conditional)
    return (current.get('status') in ('available', 'conditional') and
        current['status'] == recorded['status'] and current['identity'] == recorded['identity'] and
        current['identity_sha256'] == recorded['identity_sha256'])


def _verify_local_helper_evidence(*, case, parsed, exporter, replay, calibration, holdout, evidence,
                                  allow_conditional=False):
    """Recompute Helper locally; a supplied verification receipt is never trusted.

    The attestation must have arrived over the same authenticated producer
    channel used for the replay. The prefix must also match the independent
    anchor captured from the local fit's owned input, not just the producer's
    claim about its full observation.
    """
    from contextlib import closing
    import time
    from triad_calibration_identity import capture_calibration_identity, compare_calibration_identities
    from triad_case_replay import _clean_options
    from triad_case_suite import native_plan
    from triad_continuous import NativeStream
    from triad_helper_prefix import (helper_envelope_bound, helper_prefix_sample_count,
        helper_verification_workspace, verify_helper_prefix)
    from triad_replay_cpu import replay_geometry
    from triad_replay_packet import read_bounded_file
    from triad_replay_producer import acquisition_procedure_sha256, replay_public_metadata

    if type(evidence) is not dict or set(evidence) != {'envelope', 'attestation'}:
        raise ValueError('authenticated prefix evidence is required')
    if (replay.get('schema') != REPLAY_SCHEMA or replay.get('status') != 'measured_detached_fine' or
            replay.get('case_index') != case['index'] or
            replay.get('configuration_sha256') != case['configuration_hash'] or
            replay.get('manual_list_sha256') != parsed['source_sha256'] or
            replay.get('native_exporter_sha256') != hashlib.sha256(Path(exporter).read_bytes()).hexdigest() or
            replay.get('source_identity_sha256') != source_identity_sha256() or
            not validate_signal_acquisition_work(replay.get('signal_work')) or
            replay.get('signal_work_sha256') != canonical_digest(replay['signal_work'])):
        raise ValueError('current source or replay identity differs')
    attestation = evidence['attestation']
    if (not isinstance(evidence['envelope'], (bytes, Path)) or type(attestation) is not dict or
            attestation.get('public') != replay['public'] or
            attestation.get('acquisition') != replay['acquisition'] or
            attestation.get('signal_work') != replay['signal_work'] or
            attestation.get('signal_work_sha256') != replay['signal_work_sha256']):
        raise ValueError('prefix evidence does not bind the measured replay')
    local_work = calibration['measurement']['signal_work']
    if ((holdout is not None and local_work != holdout['measurement']['signal_work']) or
            not validate_signal_acquisition_work(local_work)):
        raise ValueError('fit and independent holdout signal work differ')
    local_anchor = calibration['measurement'].get('helper_prefix_anchor')
    if type(local_anchor) is not dict:
        raise ValueError('local fit did not capture a Helper prefix anchor')
    executable = Path(exporter).resolve(strict=True)
    geometry = replay['public_calibration_geometry']
    limit = geometry['memory_limit_bytes']
    machine = machine_resources()
    if (type(limit) is not int or limit < 1 or machine['memory_budget_bytes'] is None or
            limit > machine['memory_budget_bytes']):
        raise ValueError('local Helper verification workspace is unavailable')
    plan = native_plan(executable, case['native_args'])
    command = _clean_options(executable, case['native_args'], bit=0, seed=74021)
    with closing(NativeStream(executable, command[1:], kind='channel')) as source:
        current = capture_calibration_identity(exporter=executable,
            configuration_hash=case['configuration_hash'], workers=replay['workers'], native_service=source,
            case_list=parsed['source_path'], allow_conditional=allow_conditional)
        recorded = replay['identity_before']
        if (current.get('status') not in ('available', 'conditional') or
                current['status'] != recorded['status'] or current['identity'] != recorded['identity'] or
                current['identity_sha256'] != recorded['identity_sha256']):
            raise ValueError('current local runtime identity differs')
        manifest, grid, _, _, current_geometry = replay_geometry(source, plan, limit)
        if current_geometry != geometry:
            raise ValueError('local public geometry changed')
        calendar = current_geometry['public_geometry']['public_calendar']
        workers = replay['signal_work']['acquisition']['receiver_workers']
        procedure = acquisition_procedure_sha256(case=case,
            exporter_sha256=replay['native_exporter_sha256'],
            source_identity_sha256=replay['source_identity_sha256'], plan=plan,
            descriptor=source.descriptor, calendar=calendar, grid=grid, workers=workers)
        expected = replay_public_metadata(case=case, list_sha256=parsed['source_sha256'],
            exporter_sha256=replay['native_exporter_sha256'],
            source_identity_sha256=replay['source_identity_sha256'], plan=plan,
            descriptor=source.descriptor, calendar=calendar, grid=grid,
            acquisition_procedure_sha256=procedure, coordinates=replay['acquisition']['coordinates'])
        if expected != replay['public']:
            raise ValueError('local public replay identity changed')

        def validate_grid(coordinates):
            grid.validate_selected(coordinates)
            return True

        native = max(source.descriptor['fixed_workspace_bytes'], source.descriptor['workspace_budget_bytes'])
        reserved = 17*geometry['search_samples'] + replay['bank_reservation_bytes'] + native
        if type(reserved) is not int or not 0 < reserved < limit:
            raise MemoryError('receiver reservations exceed the local workspace')
        count = helper_prefix_sample_count(manifest, grid, geometry['search_samples'])
        available = limit - reserved
        if helper_verification_workspace(count, geometry['sample_rate_hz']) > available:
            raise MemoryError('Helper verification exceeds the remaining local workspace')
        envelope_cap = helper_envelope_bound(count)
        envelope = evidence['envelope']
        if isinstance(envelope, Path):
            envelope = read_bounded_file(envelope, minimum=1, maximum=envelope_cap)
        if type(envelope) is not bytes or not 1 <= len(envelope) <= envelope_cap:
            raise ValueError('Helper envelope exceeds the remaining local workspace')
        started = time.monotonic_ns()
        decoded, receipt = verify_helper_prefix(envelope, expected_public=expected,
            validate_grid=validate_grid, trusted_attestation=attestation, local_signal_work=local_work,
            local_prefix_anchor=local_anchor, manifest=manifest, grid=grid,
            search_samples=geometry['search_samples'], memory_limit_bytes=available,
            runtime_identity=current['identity'])
        ended = time.monotonic_ns()
        after = capture_calibration_identity(exporter=executable,
            configuration_hash=case['configuration_hash'], workers=replay['workers'], native_service=source,
            case_list=parsed['source_path'], allow_conditional=allow_conditional)
        match = compare_calibration_identities(current, current, recorded_after=after, current_after=after,
            allow_conditional=allow_conditional)
        if (match.get('status') not in ('matched', 'conditionally_matched') or
                not current['observation']['ended_ns'] < started < ended < after['observation']['started_ns']):
            raise ValueError('local Helper verification identity or work interval changed')
    if (type(decoded) is not dict or decoded.get('acquisition') != replay['acquisition'] or
            type(receipt) is not dict or receipt.get('schema') != 'triad-local-helper-prefix-verification-v1' or
            receipt.get('status') != 'verified'):
        raise ValueError('local Helper verification did not bind the replay')
    if (source_identity_sha256() != replay['source_identity_sha256'] or
            hashlib.sha256(executable.read_bytes()).hexdigest() != replay['native_exporter_sha256'] or
            hashlib.sha256(Path(parsed['source_path']).read_bytes()).hexdigest() != parsed['source_sha256']):
        raise ValueError('source or configuration changed during local Helper verification')
    return receipt


def assess_nominal_replay(*, case, parsed, exporter, replay, calibration, holdout, allow_conditional=False,
                          helper_evidence=None):
    """Apply exact-work, workspace, identity and held-out nominal service gates.

    Supply trusted local receipt files. The independently authenticated producer
    attestation and packet are verified when the local replay is measured.
    Use a full local fit and a fresh independently predeclared nominal resident
    holdout. Never upload these
    local receipts. The complete worst-state decision remains separate.
    """
    if any(not isinstance(row, dict) for row in (replay, calibration, holdout)):
        return _unresolved('missing_replay_calibration_or_holdout')
    try:
        if replay.get('schema') != REPLAY_SCHEMA or replay.get('status') != 'measured_detached_fine':
            return _unresolved('local_replay_not_measured')
        case_index = case['index']
        if not all(row.get('case_index') == case_index for row in (replay,calibration,holdout)):
            return _unresolved('ordered_manual_row_identity_differs')
        if (replay['configuration_sha256'] != case['configuration_hash'] or
                replay['manual_list_sha256'] != parsed['source_sha256'] or
                replay['native_exporter_sha256'] != hashlib.sha256(Path(exporter).read_bytes()).hexdigest() or
                replay['source_identity_sha256'] != source_identity_sha256()):
            return _unresolved('manual_list_source_or_exporter_changed')
        geometry = replay['public_calibration_geometry']
        policy = measurement_policy(len(parsed['cases']), geometry['unit_samples']/geometry['sample_rate_hz'])
        if not _independent_work(calibration, replay, holdout):
            return _unresolved('calibration_replay_and_holdout_are_not_independent_runs')
        if not validate_holdout_declaration(calibration, replay, holdout):
            return _unresolved('holdout_was_not_predeclared_for_exact_fit_and_replay')
        machine = machine_resources()
        if machine['memory_budget_bytes'] is None:
            return _unresolved('local_machine_or_budget_unavailable')
        if not _current_runtime_matches(case, parsed, exporter, replay['workers'],
                replay['identity_before'], machine, allow_conditional):
            return _unresolved('current_local_runtime_identity_differs')

        def compare(receipt, *, nominal_holdout=False):
            return compare_nonfine_calibration(receipt, replay['identity_before'], replay['identity_after'],
                current_work_started_ns=replay['work_started_ns'], current_work_ended_ns=replay['work_ended_ns'],
                current_geometry=geometry, current_policy=policy, allow_conditional=allow_conditional,
                nominal_holdout=nominal_holdout)

        held_out = compare(holdout, nominal_holdout=True)
        if held_out['status'] != 'locally_matched_unqualified':
            return _unresolved('held_out_calibration_local_runtime_or_geometry_unmatched')
        actual = combine_components(holdout['measurement']['nominal_service']['components'],
            complete_work_covered=True)

        def unresolved_with_verified_holdout(reason):
            return _record_point_failures(_unresolved(reason), independent_resident_nominal=actual)

        signal = replay.get('signal_work')
        if (not validate_signal_acquisition_work(signal) or replay.get('signal_work_sha256') != canonical_digest(signal) or
                signal['fine_slice_sha256'] != replay['acquisition']['input_sha256'] or
                signal['acquisition']['coordinates'] != replay['acquisition']['coordinates']):
            return unresolved_with_verified_holdout('complete_actual_signal_work_unavailable')
        if calibration['measurement'].get('inference_allocation_mode', 'pilot_selected') != 'pilot_selected':
            return unresolved_with_verified_holdout('fixed_holdout_cannot_supply_fit_calibration')
        fitted = compare(calibration)
        if fitted['status'] != 'locally_matched_unqualified':
            return unresolved_with_verified_holdout('calibration_local_runtime_or_geometry_unmatched')
        local_signal = calibration['measurement'].get('signal_work')
        local_helper_verification = None
        if (helper_evidence is not None or local_signal != signal or
                holdout['measurement'].get('signal_work') != signal):
            if helper_evidence is None:
                return unresolved_with_verified_holdout('calibration_complete_actual_work_unmatched')
            try:
                local_helper_verification = _verify_local_helper_evidence(case=case, parsed=parsed,
                    exporter=exporter, replay=replay, calibration=calibration, holdout=holdout,
                    evidence=helper_evidence, allow_conditional=allow_conditional)
            except (AttributeError, EOFError, KeyError, OSError, RuntimeError, TypeError, ValueError,
                    OverflowError, MemoryError):
                return unresolved_with_verified_holdout('local_helper_prefix_verification_failed')
            if (replay.get('local_helper_prefix_verification') != local_helper_verification or
                    holdout.get('local_helper_predeclaration') != local_helper_verification):
                return unresolved_with_verified_holdout('local_helper_prefix_predeclaration_unmatched')
        for label, receipt, check in (('calibration', calibration, fitted), ('held_out_calibration', holdout, held_out)):
            measurement = receipt['measurement']
            actual_acquisition = measurement['nominal_acquisition']
            samples = actual_acquisition.get('samples', ())
            count = len(samples)
            expected_signal = local_signal if local_helper_verification is not None else signal
            if (measurement.get('signal_work') != expected_signal or count not in LOOKS or
                    actual_acquisition.get('actual_work_match_count') != count or
                    signal['sample_count'] != geometry['search_samples'] or
                    signal['acquisition']['receiver_workers'] != replay['workers']):
                return unresolved_with_verified_holdout(label+'_complete_actual_work_unmatched')
            reserved = replay['bank_reservation_bytes']
            required = check['resident_bank']['work']['fft_workspace_bytes']
            if type(reserved) is not int or type(required) is not int or not 0 < required <= reserved:
                return unresolved_with_verified_holdout(label+'_resident_bank_reservation_unmatched')
            if (check['workspace']['status'] != 'within_budget' or
                    not check['workspace'].get('observed_peak_bytes') or
                    max(check['workspace']['conservative_peak_bytes'], geometry['memory_limit_bytes']) >
                        machine['memory_budget_bytes']):
                return unresolved_with_verified_holdout(label+'_workspace_unresolved')
        try:
            matched_fine = _same_fine_work(replay['fine'], fitted['resident_fine'], held_out['resident_fine'])
        except (KeyError, TypeError, ValueError):
            matched_fine = False
        if not matched_fine:
            return unresolved_with_verified_holdout('resident_and_detached_fine_work_differ')
        prediction = combine_components(dict(fitted['components']['nominal_service'],
            complete_fine_search=replay['fine']['fine_component']), complete_work_covered=True)
        result = _qualified_nominal(prediction, actual)
        if result['status'] == 'validated_conditional_nominal_service':
            if local_helper_verification is not None:
                result['local_helper_prefix_verification'] = local_helper_verification
            holdout_measurement = holdout['measurement']
            result['holdout'].update(
                exploratory_selected_workers=holdout_measurement['selected_workers'],
                actual_inference_workers=holdout_measurement.get('inference_workers',
                    holdout_measurement['selected_workers']),
                inference_allocation_mode=holdout_measurement.get('inference_allocation_mode', 'pilot_selected'),
                scope='Independent local throughput at the same fitted allocation; no CPU ceiling claim.')
        return result
    except (AttributeError, EOFError, KeyError, OSError, RuntimeError, TypeError, ValueError, OverflowError):
        return _unresolved('malformed_or_unsupported_replay_evidence')


def estimate_current_manual_list(*, case_list, exporter, replays, calibrations, holdouts,
                                 allow_conditional=False, helper_envelopes=None, helper_attestations=None):
    """Preserve every ordered current row and explicitly report missing evidence."""
    from triad_cases import load_cases

    parsed = load_cases(case_list)
    list_hash = parsed['source_sha256']
    source_hash = source_identity_sha256()
    exporter_hash = hashlib.sha256(Path(exporter).read_bytes()).hexdigest()
    helper_envelopes = {} if helper_envelopes is None else helper_envelopes
    helper_attestations = {} if helper_attestations is None else helper_attestations
    if set(helper_envelopes) != set(helper_attestations):
        raise ValueError('each Helper envelope needs its separately authenticated attestation')
    indices = {case['index'] for case in parsed['cases']}
    if any(type(index) is not int or index not in indices
            for group in (replays, calibrations, holdouts, helper_envelopes, helper_attestations) for index in group):
        raise ValueError('receipt index must be an existing current manual row')
    rows = []
    for case in parsed['cases']:
        index = case['index']
        identity = dict(index=index, heading=case['heading'],
            configuration_sha256=case['configuration_hash'])
        try:
            evidence = [None if index not in group else _read_local_receipt(group[index])
                for group in (replays, calibrations, holdouts)]
            helper_evidence = None
            if index in helper_envelopes:
                from triad_replay_cpu import _read_attestation
                helper_evidence = dict(envelope=Path(helper_envelopes[index]),
                    attestation=_read_attestation(helper_attestations[index]))
        except (ValueError, OSError, TypeError, RecursionError):
            result = _unresolved('local_evidence_unreadable_or_invalid')
        else:
            result = assess_nominal_replay(case=case, parsed=parsed, exporter=exporter,
                replay=evidence[0], calibration=evidence[1], holdout=evidence[2],
                allow_conditional=allow_conditional, helper_evidence=helper_evidence)
        rows.append(dict(identity, **result))
    unchanged = (list_hash == hashlib.sha256(Path(case_list).read_bytes()).hexdigest() and
        source_hash == source_identity_sha256() and
        exporter_hash == hashlib.sha256(Path(exporter).read_bytes()).hexdigest())
    if not unchanged:
        rows = [dict(index=row['index'], heading=row['heading'],
            configuration_sha256=row['configuration_sha256'],
            **_unresolved('source_or_configuration_changed_during_estimation')) for row in rows]
    qualified = sum(row['status'] == 'validated_conditional_nominal_service' for row in rows)
    return dict(schema='triad-manual-replay-estimate-v1',
        status='current_list_nominal_estimates_validated' if qualified == len(rows) and rows else 'unresolved',
        validated_nominal_case_count=qualified, current_case_count=len(rows), cases=rows,
        manual_list_sha256=list_hash, source_identity_sha256=source_hash,
        native_exporter_sha256=exporter_hash, unchanged_at_completion=unchanged,
        complete_receiver_capacity_established=False,
        metric='Prepared elapsed receiver processing seconds per received baseband second.')


def _indexed_files(values):
    result = {}
    for value in values:
        index, separator, path = value.partition('=')
        if not separator or not index.isascii() or not index.isdecimal() or not path:
            raise ValueError('receipt references must be INDEX=PATH')
        row = int(index)
        if row in result:
            raise ValueError('duplicate receipt for a manual row')
        result[row] = path
    return result


def main(argv=None):
    import argparse
    import json
    import sys
    from triad_cases import DEFAULT_CASES
    from triad_replay_packet import write_new_file

    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--case-list', type=Path, default=DEFAULT_CASES)
    parser.add_argument('--exporter', type=Path, required=True)
    for name in ('replay', 'calibration', 'holdout'):
        parser.add_argument('--'+name, action='append', default=[], metavar='INDEX=PATH',
            help='trusted locally generated receipt, repeat for each current manual row')
    parser.add_argument('--helper-envelope', action='append', default=[], metavar='INDEX=PATH',
        help='bounded Helper-prefix envelope from the authenticated producer, paired with attestation')
    parser.add_argument('--helper-attestation', action='append', default=[], metavar='INDEX=PATH',
        help='separately authenticated original producer attestation, paired with envelope')
    parser.add_argument('--allow-conditional-identity', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = estimate_current_manual_list(case_list=args.case_list, exporter=args.exporter,
            replays=_indexed_files(args.replay), calibrations=_indexed_files(args.calibration),
            holdouts=_indexed_files(args.holdout), allow_conditional=args.allow_conditional_identity,
            helper_envelopes=_indexed_files(args.helper_envelope),
            helper_attestations=_indexed_files(args.helper_attestation))
        write_new_file(args.output, (json.dumps(result, indent=2, allow_nan=False)+'\n').encode())
    except (OSError, TypeError, ValueError) as error:
        print('unresolved:', str(error), file=sys.stderr)
        return 2
    print(result['status'])
    return 0 if result['status'] == 'current_list_nominal_estimates_validated' else 2


if __name__ == '__main__':
    raise SystemExit(main())
