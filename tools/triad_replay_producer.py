#!/usr/bin/env python3
"""Produce a bounded native fine-work slice and an authenticated-channel receipt.

The receipt must reach the consumer independently over an authenticated channel.
Use a fresh trusted process and no concurrent source or binary replacement; file
hashes do not identify loaded runtime libraries or transient executable loads.
Neither the packet's digest nor an unauthenticated receipt establishes search
provenance. This producer runs a real, complete public acquisition; a tiled
search requiring more than its bounded initial look is reported unresolved.
Outputs are inputs to local measurements, not throughput or success evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from triad_calibration_identity import _expected_source_paths
from triad_case_capacity import (PreparedCapacityCase, continued_probe_extent,
    signal_acquisition_work, validate_signal_acquisition_work)
from triad_case_metrics import machine_resources
from triad_case_replay import public_screen_geometry
from triad_case_suite import native_plan, select_cases
from triad_cases import DEFAULT_CASES, ROOT, load_cases
from triad_cpu import configure_numerical_threads, resolve_cpu_budget
from triad_replay_packet import encode_packet, write_new_file, write_packet
from triad_receiver import _DEFAULT


ATTESTATION_SCHEMA = 'triad-authenticated-acquisition-attestation-v1'
ACQUISITION_SCHEMA = 'triad-complete-public-acquisition-receipt-v1'


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
        ensure_ascii=True, allow_nan=False).encode('ascii')).hexdigest()


def source_identity_sha256():
    """Current exact local DSP source closure, including the replay code."""
    paths = _expected_source_paths()
    files = {}
    for relative in sorted(paths):
        path = ROOT / relative
        if not path.is_file():
            raise ValueError('required source file missing')
        files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return canonical_digest(files)


def acquisition_procedure_sha256(*, case, exporter_sha256, source_identity_sha256,
                                  plan, descriptor, calendar, grid, workers):
    """Public path-independent identity for the fixed bounded acquisition.

    This pins code, binary, public plan/bank geometry, address, default policy
    and acquisition workers, not runtime or a proof that acquisition occurred.
    The verifier independently derives these inputs and trusts the authenticated
    producer process. Python numerical runtimes are not claimed identical.
    """
    if type(workers) is not int or workers < 1:
        raise ValueError('positive acquisition worker count required')
    geometry = dict(public_screen_geometry(plan, descriptor))
    geometry['grid'] = geometry['grid'].identity()
    policy = dict(_DEFAULT, operational_evidence='fixed_initial_affine')
    public_descriptor = {key: descriptor[key] for key in ('sample_rate_hz',
        'total_samples', 'source_samples', 'capture_lead_seconds', 'data_offset_samples')}
    return canonical_digest(dict(schema='triad-public-acquisition-procedure-v1',
        configuration_sha256=case['configuration_hash'], exporter_sha256=exporter_sha256,
        source_identity_sha256=source_identity_sha256, plan_format=plan['format'],
        plan_physics=plan['physics'], plan_geometry=plan['geometry'],
        descriptor=public_descriptor, calendar_sha256=canonical_digest(calendar),
        final_grid_sha256=grid.identity(), public_screen_geometry=geometry,
        policy=policy, workers=workers, coarse_unit_limit=0,
        default_numerical_threads=1))


def replay_public_metadata(*, case, list_sha256, exporter_sha256, source_identity_sha256,
                           plan, descriptor, calendar, grid,
                           acquisition_procedure_sha256, coordinates):
    """Locally rederive all packet geometry; caller supplies trusted identities.

    ``grid`` must be the actual locally constructed final receiver grid, not a
    remotely supplied object. Procedure identity must be independently approved;
    an untrusted packet or attestation alone cannot define that identity.
    """
    coordinates = tuple(tuple(row) for row in coordinates)
    grid.validate_selected(coordinates)
    if len(coordinates) < 1 or len(coordinates) > 12:
        raise ValueError('complete public acquisition needs bounded selected coordinates')
    plans = calendar['plans']
    if len(plans) != 24 or plans[0]['index'] != 0:
        raise ValueError('fixed 24-slot public calendar required')
    fs = int(descriptor['sample_rate_hz'])
    midpoint, origin, samples = continued_probe_extent(plan, descriptor, coordinates)
    if fs != 6000 or origin is None or samples != 36000:
        raise ValueError('six-second public fine slice unavailable')
    slot = plans[0]
    # PreparedCapacityCase measures the complete native capture at origin
    # zero, including any pre-roll before the stream scheduler's first window.
    # Keep the actual native slot geometry in the independently derived
    # calendar; packet bounds describe the calibrated capture.
    stop = descriptor['total_samples']
    slot_first = slot['receiver_window_first_sample']
    slot_stop = slot['receiver_window_stop_sample']
    slot_origin = slot['receiver_window_physical_origin_sample']
    slot_samples = slot['receiver_window_samples']
    if (any(type(value) is not int for value in (stop,slot_first,slot_stop,slot_origin,slot_samples)) or
            not 0 <= slot_first < slot_stop == stop or
            slot_origin != slot_first or slot_samples != slot_stop-slot_first):
        raise ValueError('slot zero window is inconsistent with the native capture')
    calendar_hash = canonical_digest(calendar)
    grid_hash = grid.identity()
    work = dict(schema='triad-public-fine-work-identity-v1', config=case['configuration_hash'],
        list=list_sha256, source=source_identity_sha256, exporter=exporter_sha256,
        calendar=calendar_hash, grid=grid_hash, procedure=acquisition_procedure_sha256,
        coordinates=[list(row) for row in coordinates], origin_sample=origin,
        midpoint_sample=midpoint, sample_count=samples, sample_rate_hz=fs,
        block='fixed two-second prefix and 1/2/4-second continuation')
    return dict(manual_list_sha256=list_sha256, configuration_sha256=case['configuration_hash'],
        native_exporter_sha256=exporter_sha256, source_identity_sha256=source_identity_sha256,
        calendar_sha256=calendar_hash, grid_sha256=grid_hash, work_identity_sha256=canonical_digest(work),
        acquisition_procedure_sha256=acquisition_procedure_sha256, case_index=case['index'],
        sample_rate_hz=fs, sample_count=samples, calendar_slots=24, slot_index=0,
        window_first_sample=0, window_stop_sample=stop, origin_sample=origin,
        pattern_epoch=int(slot['pattern_epoch']), symbol_ordinal=int(slot['symbol_ordinal']))


def verify_authenticated_receipt(receipt, public, input_digest, attestation):
    """Compare with an attestation obtained independently over authenticated transport.

    The caller must authenticate the remote endpoint, keep the attestation
    separate from the packet, locally derive ``public``, and separately approve
    the receiver procedure identity. This function does not perform transport
    authentication and cannot prove a remote host is honest.
    """
    try:
        if (type(attestation) is not dict or attestation.get('schema') != ATTESTATION_SCHEMA or
                attestation.get('status') != 'complete_public_acquisition' or
                attestation.get('source_unchanged') is not True or
                attestation.get('public') != public or
                attestation.get('acquisition') != receipt or
                receipt.get('input_sha256') != input_digest):
            return False
        unsigned = dict(receipt)
        claimed = unsigned.pop('receipt_sha256')
        return claimed == canonical_digest(unsigned)
    except (KeyError, TypeError, ValueError):
        return False


def make_packet(prepared, receiver, *, case, list_sha256, exporter_sha256, source_hash,
                capture_signal_work=False):
    """Use an exclusively owned PreparedCapacityCase after complete acquisition.

    Caller must ensure no other thread/process can mutate the prepared raw and
    validity arrays from acquisition through snapshot. The CLI creates and owns
    this instance and does not publish the arrays or invoke external callbacks.
    """
    if type(capture_signal_work) is not bool:
        raise ValueError('capture_signal_work must be explicit boolean')
    acquired = receiver.acquire(prepared.raw, prepared.valid, coarse_unit_limit=0)
    session = acquired.pop('coarse_session', None)
    if session is not None:
        session.close()
    coarse = acquired.get('coarse', {})
    if (acquired.get('status') != 'acquired' or coarse.get('status') != 'proposed' or
            not acquired.get('coordinates')):
        raise ValueError('complete bounded public acquisition was not established')
    coordinates = tuple(acquired['coordinates'])
    if receiver.policy != dict(_DEFAULT, operational_evidence='fixed_initial_affine'):
        raise ValueError('producer requires the canonical default receiver policy')
    procedure = acquisition_procedure_sha256(case=case, exporter_sha256=exporter_sha256,
        source_identity_sha256=source_hash, plan=prepared.plan, descriptor=prepared.bank.descriptor,
        calendar=prepared.calendar, grid=receiver.grid, workers=receiver.workers)
    public = replay_public_metadata(case=case, list_sha256=list_sha256,
        exporter_sha256=exporter_sha256, source_identity_sha256=source_hash,
        plan=prepared.plan, descriptor=prepared.bank.descriptor,
        calendar=prepared.calendar, grid=receiver.grid,
        acquisition_procedure_sha256=procedure, coordinates=coordinates)
    origin, count = public['origin_sample'], public['sample_count']
    raw, valid = prepared.raw[origin:origin+count], prepared.valid[origin:origin+count]
    if (raw.shape != (count,) or raw.dtype != np.dtype('complex128') or
            valid.shape != (count,) or valid.dtype != np.dtype('uint8')):
        raise ValueError('unexpected native observation layout')
    # Capture the two inputs once as immutable bytes while this producer owns
    # the only reachable observation arrays and all receiver work has returned.
    payload = np.ascontiguousarray(raw, dtype='<c16').tobytes() + np.ascontiguousarray(valid, dtype='u1').tobytes()
    acquisition = dict(schema=ACQUISITION_SCHEMA, status='acquired',
        complete_public_search=True, work_only=False, procedure_sha256=procedure,
        input_sha256=hashlib.sha256(payload).hexdigest(),
        coordinates=[list(row) for row in coordinates])
    acquisition['receipt_sha256'] = canonical_digest(acquisition)
    blob = encode_packet(public, acquisition, payload)
    if not capture_signal_work:
        return blob, public, acquisition
    signal_work = signal_acquisition_work(prepared, acquired, receiver_workers=receiver.workers)
    if (not validate_signal_acquisition_work(signal_work) or
            signal_work['fine_slice_sha256'] != acquisition['input_sha256'] or
            signal_work['acquisition']['coordinates'] != acquisition['coordinates'] or
            signal_work['acquisition']['receiver_workers'] != receiver.workers):
        raise ValueError('signal acquisition work does not match the produced fine slice')
    return blob, public, acquisition, signal_work


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--case-list', type=Path, default=DEFAULT_CASES)
    parser.add_argument('--case-index', type=int, required=True)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--memory-limit-bytes', type=int,
        help='optional local workspace cap, no greater than visible available memory')
    parser.add_argument('--helper-prefix', action='store_true',
        help='produce a bounded Helper-prefix envelope for independent local verification')
    args = parser.parse_args(argv)
    try:
        parsed = load_cases(args.case_list)
        case = select_cases(parsed, [args.case_index])[0]
        if args.helper_prefix and not case['fields']['Helper Carrier Transmit Power'] > 0:
            raise ValueError('Helper-prefix transport requires an enabled Helper')
        machine = machine_resources()
        if machine['cpu'] is None or machine['memory_budget_bytes'] is None:
            raise ValueError('visible CPU and workspace bounds required')
        memory_limit = machine['memory_budget_bytes'] if args.memory_limit_bytes is None else args.memory_limit_bytes
        if type(memory_limit) is not int or not 0 < memory_limit <= machine['memory_budget_bytes']:
            raise ValueError('workspace cap must fit visible local memory')
        allocation = resolve_cpu_budget(jobs=1, workers=args.workers, threads=1, capacity=machine['cpu'])
        if allocation['explicit_overcommit']:
            raise ValueError('worker count exceeds visible CPU allocation')
        configure_numerical_threads(allocation)
        exporter_hash = hashlib.sha256(args.exporter.read_bytes()).hexdigest()
        source_hash = source_identity_sha256()
        plan = native_plan(args.exporter, case['native_args'])
        with PreparedCapacityCase(args.exporter, case, plan,
                memory_limit_bytes=memory_limit) as prepared:
            with prepared.receiver(args.workers) as receiver:
                actual_receiver_procedure = receiver.procedure_id
                blob, public, acquisition, signal_work = make_packet(prepared, receiver, case=case,
                    list_sha256=parsed['source_sha256'], exporter_sha256=exporter_hash,
                    source_hash=source_hash, capture_signal_work=True)
                if args.helper_prefix:
                    from triad_helper_prefix import snapshot_helper_prefix
                    prefix_iq, prefix_validity, prefix_receipt = snapshot_helper_prefix(prepared, receiver, signal_work)
        unchanged = (source_hash == source_identity_sha256() and
            exporter_hash == hashlib.sha256(args.exporter.read_bytes()).hexdigest() and
            parsed['source_sha256'] == hashlib.sha256(args.case_list.read_bytes()).hexdigest())
        if not unchanged:
            raise ValueError('source or configuration changed during acquisition')
        attestation = dict(schema=ATTESTATION_SCHEMA, status='complete_public_acquisition',
            source_unchanged=True, public=public, acquisition=acquisition,
            signal_work=signal_work, signal_work_sha256=canonical_digest(signal_work),
            diagnostics=dict(path_dependent_receiver_procedure_sha256=actual_receiver_procedure,
                acquisition_workers=args.workers, python=sys.version.split()[0],
                numpy=np.__version__, memory_limit_bytes=memory_limit),
            scope='Remote short-work slice and completed bounded public acquisition only; no local CPU service or capacity conclusion.')
        if args.helper_prefix:
            from triad_helper_prefix import encode_helper_envelope
            attestation['helper_prefix'] = prefix_receipt
            write_new_file(args.output, encode_helper_envelope(blob, prefix_iq, prefix_validity, attestation))
        else:
            write_packet(args.output, blob)
        # The operator transports this exact output independently over an
        # authenticated channel. A prefix envelope also embeds it for an
        # exact comparison; the embedded copy is not authentication.
        print(json.dumps(attestation, sort_keys=True, separators=(',', ':'), allow_nan=False))
        return 0
    except (OSError, ValueError, RuntimeError, MemoryError) as exc:
        print('unresolved:', str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
