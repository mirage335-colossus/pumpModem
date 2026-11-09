#!/usr/bin/env python3
"""Measure canonical fine receiver work locally from a verified native replay.

The short slice skips remote capture generation and the local coarse bank.
Use the same host's separate, current complete-work calibration and real-bank
holdouts before interpreting replay timings as a complete service estimate.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import json
import math
from pathlib import Path
import sys
import time

from triad_calibration_identity import capture_calibration_identity, compare_calibration_identities
from triad_case_capacity import (continued_probe_extent, measure_continued_fine, measurement_policy,
    public_calendar, validate_signal_acquisition_work)
from triad_case_metrics import machine_resources
from triad_case_receiver import EPOCHS, _manifest
from triad_case_replay import _clean_options, public_screen_geometry
from triad_case_suite import native_plan, select_cases
from triad_cases import DEFAULT_CASES, load_cases
from triad_continuous import NativeStream
from triad_cpu import configure_numerical_threads, resolve_cpu_budget
from triad_receiver import FineReceiverContext, fine_public_grid
from triad_replay_packet import read_bounded_file, read_packet, write_new_file
from triad_replay_producer import (acquisition_procedure_sha256, canonical_digest, replay_public_metadata,
    source_identity_sha256, verify_authenticated_receipt)


SCHEMA = 'triad-local-fine-replay-measurement-v1'


def replay_geometry(source, plan, memory_limit_bytes):
    """Reconstruct public work inputs from the current native executable."""
    descriptor = source.descriptor
    screen = public_screen_geometry(source.plan, descriptor)
    calendar = public_calendar(source)
    fs = int(descriptor['sample_rate_hz'])
    samples, source_samples = screen['raw_capture_samples'], screen['raw_source_samples']
    slot_seconds = calendar['minimum_interval_seconds']
    address = calendar['plans'][0]
    manifest = _manifest(source.plan, descriptor, samples, 0, 72001)
    manifest['format']['symbol_ordinal'] = int(descriptor['symbol_ordinal'])
    if (manifest['public_epoch_seconds'] != address['pattern_epoch'] or
            manifest['format']['symbol_ordinal'] != address['symbol_ordinal']):
        raise ValueError('local native basis address differs from slot zero')
    for name in ('quiet_aperture_first_sample', 'quiet_aperture_stop_sample'):
        manifest['calendar'][name] = address[name]
    cn0 = float(plan['physics']['physical_data_cn0_db_hz'])
    noise_variance = fs*10**(-cn0/10)
    if not math.isfinite(noise_variance) or noise_variance <= 0:
        raise ValueError('injected raw noise variance is outside finite range')
    manifest['power']['complex_noise_variance'] = noise_variance
    coarse_step = 1/(4*(source_samples/fs))
    grid = fine_public_grid(manifest, EPOCHS, coarse_step)
    unit = min(fs, samples//4)
    calibration_geometry = dict(
        public_geometry=dict(sample_rate_hz=fs, search_window_samples=samples,
            search_window_seconds=samples/fs, conservative_public_slot_interval_seconds=slot_seconds,
            public_calendar=calendar, fine_unit_samples=unit, fine_unit_seconds=unit/fs,
            source_samples=source_samples),
        coarse_geometry=dict(epochs=list(EPOCHS),
            public_arrival_prior_seconds=list(source.plan['physics']['arrival_prior_seconds']),
            public_frequency_bound_hz=screen['grid'].frequency_bound,
            public_clock_bank_ppm=list(screen['clock_bank']),
            bit_template_count=len(EPOCHS)*2*len(screen['clock_bank']),
            source_sample_rate_hz=fs, projected_sample_rate_hz=screen['output_fs'],
            raw_source_samples=source_samples, raw_capture_samples=samples,
            coarse_arrival_step_seconds=1/screen['output_fs']),
        sample_rate_hz=fs, search_samples=samples, unit_samples=unit, slot_seconds=slot_seconds,
        memory_limit_bytes=memory_limit_bytes, noise_variance=noise_variance)
    return manifest, grid, screen, coarse_step, calibration_geometry


def measure_local_fine_replay(*, executable, case_list, case_index, packet, attestation,
                              acquisition_workers, workers, bank_reservation_bytes,
                              memory_limit_bytes=None, allow_conditional=False,
                              helper_prefix_fit=None):
    """Measure one current row in a single CPU receiver's actual worker pool.

    The caller obtains the attestation independently from the packet over an
    authenticated channel with a predeclared acquisition worker count. A
    reservation limits the fine allocator but cannot reproduce resident cache
    and memory pressure. It may be supplied by an applicable local calibration.
    """
    if (type(bank_reservation_bytes) is not int or bank_reservation_bytes < 1 or
            type(allow_conditional) is not bool):
        raise ValueError('a positive local bank reservation and explicit identity mode are required')
    diagnostics = attestation.get('diagnostics') if type(attestation) is dict else None
    declared = diagnostics.get('acquisition_workers') if type(diagnostics) is dict else None
    if type(acquisition_workers) is not int or type(declared) is not int or declared != acquisition_workers:
        raise ValueError('attested acquisition allocation differs from the declared remote allocation')
    parsed = load_cases(case_list)
    case = select_cases(parsed, [case_index])[0]
    machine = machine_resources()
    budget = machine['memory_budget_bytes']
    if machine['cpu'] is None or budget is None:
        raise ValueError('visible local CPU and workspace bounds required')
    limit = budget if memory_limit_bytes is None else memory_limit_bytes
    if type(limit) is not int or not bank_reservation_bytes < limit <= budget:
        raise ValueError('replay workspace cap must fit the current local budget')
    allocation = resolve_cpu_budget(jobs=1, workers=workers, threads=1, capacity=machine['cpu'])
    if allocation['explicit_overcommit']:
        raise ValueError('workers exceed the available local CPU allocation')
    configure_numerical_threads(allocation)
    executable = Path(executable).resolve()
    source_hash = source_identity_sha256()
    exporter_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    plan = native_plan(executable, case['native_args'])
    command = _clean_options(executable, case['native_args'], bit=0, seed=74021)
    report = dict(schema=SCHEMA, status='unresolved', case_index=case_index,
        configuration_sha256=case['configuration_hash'], manual_list_sha256=parsed['source_sha256'],
        native_exporter_sha256=exporter_hash, source_identity_sha256=source_hash,
        workers=workers, complete_receiver_capacity_established=False,
        bank_reservation_bytes=bank_reservation_bytes,
        bank_residency='Caller-supplied reservation for absent bank and full-window input; local resident workspace remains unvalidated.',
        metric='Prepared elapsed receiver processing seconds per received baseband second.')
    with closing(NativeStream(executable, command[1:], kind='channel')) as source:
        manifest, grid, screen, coarse_step, geometry = replay_geometry(source, plan, limit)
        public = geometry['public_geometry']
        calendar = public['public_calendar']
        if public['search_window_samples'] <= 6*public['sample_rate_hz']:
            raise ValueError('replay requires a proper interior subset of the public window')
        attested = attestation.get('acquisition', {})
        procedure = acquisition_procedure_sha256(case=case, exporter_sha256=exporter_hash,
            source_identity_sha256=source_hash, plan=plan, descriptor=source.descriptor,
            calendar=calendar, grid=grid, workers=acquisition_workers)
        expected = replay_public_metadata(case=case, list_sha256=parsed['source_sha256'],
            exporter_sha256=exporter_hash, source_identity_sha256=source_hash,
            plan=plan, descriptor=source.descriptor, calendar=calendar, grid=grid,
            acquisition_procedure_sha256=procedure, coordinates=attested.get('coordinates', ()))

        def validate_grid(coordinates):
            grid.validate_selected(coordinates)
            return True

        native_budget = max(source.descriptor['fixed_workspace_bytes'], source.descriptor['workspace_budget_bytes'])
        reserved = 17*public['search_window_samples']+bank_reservation_bytes+native_budget
        if reserved >= limit:
            raise MemoryError('full input and bank reservations exceed local replay cap')
        capture = dict(exporter=executable, configuration_hash=case['configuration_hash'],
            workers=workers, native_service=source, case_list=case_list, allow_conditional=allow_conditional)
        if helper_prefix_fit is None:
            replay = read_packet(packet, expected_public=expected, validate_grid=validate_grid,
                validate_acquisition_receipt=lambda receipt, expected, digest:
                    verify_authenticated_receipt(receipt, expected, digest, attestation))
        else:
            # This optional envelope requires a current local fit's own
            # independently captured prefix anchor. Verify before the fine
            # timer, preserving the original remote receipt and its digest.
            from triad_helper_prefix import (helper_envelope_bound, helper_prefix_sample_count,
                helper_verification_workspace, verify_helper_prefix)
            from triad_nonfine_calibration import read_local_receipt
            fit = read_local_receipt(helper_prefix_fit)
            if (fit.get('status') != 'recorded_unqualified' or fit.get('case_index') != case_index or
                    fit.get('public_geometry') != geometry or
                    fit.get('measurement', {}).get('inference_workers') != workers):
                raise ValueError('a current matching local fit is required for Helper verification')
            count = helper_prefix_sample_count(manifest, grid, geometry['search_samples'])
            helper_workspace = helper_verification_workspace(count, public['sample_rate_hz'])
            available = limit-reserved
            if helper_workspace > available:
                raise MemoryError('Helper verification exceeds the remaining local workspace')
            envelope = read_bounded_file(packet, minimum=1, maximum=helper_envelope_bound(count))
            verification_before = capture_calibration_identity(**capture)
            verification_started = time.monotonic_ns()
            replay, verification = verify_helper_prefix(envelope, expected_public=expected,
                validate_grid=validate_grid, trusted_attestation=attestation,
                local_signal_work=fit['measurement']['signal_work'],
                local_prefix_anchor=fit['measurement']['helper_prefix_anchor'],
                manifest=manifest, grid=grid, search_samples=geometry['search_samples'],
                memory_limit_bytes=available, runtime_identity=verification_before['identity'])
            verification_ended = time.monotonic_ns()
            verification_after = capture_calibration_identity(**capture)
            verification_match = compare_calibration_identities(fit['identity_before'], verification_before,
                recorded_after=fit['identity_after'], current_after=verification_after,
                allow_conditional=allow_conditional)
            if (verification_match.get('status') not in ('matched', 'conditionally_matched') or
                    not verification_before['observation']['ended_ns'] < verification_started <
                        verification_ended < verification_after['observation']['started_ns']):
                raise ValueError('local Helper verification identity or timing changed')
            report['local_helper_prefix_verification'] = verification
            report['helper_prefix_verification_observation'] = dict(
                identity_before=verification_before, identity_after=verification_after,
                identity_match=verification_match, work_started_ns=verification_started,
                work_ended_ns=verification_ended, reserved_bytes=reserved,
                additional_workspace_upper_bytes=helper_workspace,
                scope='Verification only; excluded from fine-service timing.')
            del envelope
        signal_work = attestation.get('signal_work')
        signal_hash = attestation.get('signal_work_sha256')
        if signal_work is not None or signal_hash is not None:
            if (not validate_signal_acquisition_work(signal_work) or canonical_digest(signal_work) != signal_hash or
                    signal_work['sample_count'] != public['search_window_samples'] or
                    signal_work['fine_slice_sha256'] != replay['acquisition']['input_sha256'] or
                    signal_work['acquisition']['receiver_workers'] != acquisition_workers or
                    signal_work['acquisition']['coordinates'] != replay['acquisition']['coordinates']):
                raise ValueError('authenticated acquisition signal work differs from the replay')
            report.update(signal_work=signal_work, signal_work_sha256=signal_hash)
        coordinates = tuple(tuple(row) for row in replay['acquisition']['coordinates'])
        midpoint, origin, _ = continued_probe_extent(plan, source.descriptor, coordinates)
        report.update(public=expected, acquisition=replay['acquisition'], public_calibration_geometry=geometry)
        with FineReceiverContext(manifest, source, epochs=EPOCHS, clocks=screen['clock_bank'],
                projected_sample_rate=screen['output_fs'], coarse_frequency_step=coarse_step,
                coarse_reserved_bytes=bank_reservation_bytes, coarse_procedure_id='detached_replay',
                coarse_reference_variance=None, workers=workers, memory_limit_bytes=limit) as receiver:
            receiver.input_workspace_bytes = 17*receiver.samples
            before = capture_calibration_identity(**capture)
            report['identity_before'] = before
            if before.get('status') not in ('available', 'conditional'):
                report['reason'] = 'pre_measurement_identity_unavailable'
                return report
            started = time.monotonic_ns()
            measurement = measure_continued_fine(receiver, coordinates, raw=replay['samples'],
                valid=replay['validity'], origin=origin, midpoint=midpoint, address=calendar['plans'][0],
                slot_seconds=geometry['slot_seconds'],
                family_size=measurement_policy(len(parsed['cases']), 1.)['family_size'])
            ended = time.monotonic_ns()
            after = capture_calibration_identity(**capture)
        match = compare_calibration_identities(before, before, recorded_after=after, current_after=after,
            allow_conditional=allow_conditional)
        report.update(identity_after=after, identity_match=match, work_started_ns=started,
            work_ended_ns=ended, fine=measurement)
    unchanged = (source_hash == source_identity_sha256() and
        exporter_hash == hashlib.sha256(executable.read_bytes()).hexdigest() and
        parsed['source_sha256'] == load_cases(case_list)['source_sha256'])
    if not unchanged:
        report['reason'] = 'source_or_manual_list_changed'
    elif match['status'] not in ('matched', 'conditionally_matched'):
        report['reason'] = 'measurement_identity_changed_or_unavailable'
    elif not before['observation']['ended_ns'] < started < ended < after['observation']['started_ns']:
        report['reason'] = 'actual_work_outside_identity_observations'
    elif measurement['status'] != 'measured_continuation':
        report['reason'] = measurement.get('reason') or 'fine_measurement_unresolved'
    else:
        report.update(status='measured_detached_fine', reason='complete_service_calibration_and_holdout_required')
    return report


def _read_attestation(path):
    data = read_bounded_file(path, minimum=1, maximum=65536)
    value = json.loads(data)
    if type(value) is not dict:
        raise ValueError('expected acquisition attestation object')
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--case-list', type=Path, default=DEFAULT_CASES)
    parser.add_argument('--case-index', type=int, required=True)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--packet', type=Path, required=True)
    parser.add_argument('--trusted-acquisition', type=Path, required=True,
        help='independent acquisition attestation from an authenticated channel')
    parser.add_argument('--helper-prefix-fit', type=Path,
        help='current private fit with local prefix anchor; --packet is then a bounded Helper envelope')
    parser.add_argument('--acquisition-workers', type=int, default=2,
        help='worker count declared for the remote public acquisition')
    parser.add_argument('--bank-reservation-bytes', type=int, required=True)
    parser.add_argument('--memory-limit-bytes', type=int)
    parser.add_argument('--workers', type=int, required=True)
    parser.add_argument('--allow-conditional-identity', action='store_true')
    parser.add_argument('--output', type=Path, required=True, help='local-only output; never upload')
    args = parser.parse_args(argv)
    try:
        report = measure_local_fine_replay(executable=args.exporter, case_list=args.case_list,
            case_index=args.case_index, packet=args.packet, attestation=_read_attestation(args.trusted_acquisition),
            acquisition_workers=args.acquisition_workers, workers=args.workers,
            bank_reservation_bytes=args.bank_reservation_bytes, memory_limit_bytes=args.memory_limit_bytes,
            allow_conditional=args.allow_conditional_identity, helper_prefix_fit=args.helper_prefix_fit)
        write_new_file(args.output, (json.dumps(report, indent=2, allow_nan=False)+'\n').encode())
    except (EOFError, OSError, ValueError, RuntimeError, MemoryError, TypeError, KeyError) as error:
        print('unresolved:', str(error), file=sys.stderr)
        return 2
    print(report['status'])
    return 0 if report['status'] == 'measured_detached_fine' else 2


if __name__ == '__main__':
    sys.exit(main())
