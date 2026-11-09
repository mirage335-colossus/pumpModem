"""Bounded Helper-prefix transport and local work-identity verification.

The original attestation is authenticated separately, and prefix membership is
checked against a local fit anchor captured from the complete observation. This
does not establish receiver success or local CPU capacity.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import struct

import numpy as np

from triad_replay_packet import _PACKET_LIMIT, decode_packet

PREFIX_SCHEMA = 'triad-helper-prefix-v1'
VERIFICATION_SCHEMA = 'triad-local-helper-prefix-verification-v1'
MAX_PREFIX_SAMPLES = 1_000_000
MAX_HELPER_WORKSPACE_BYTES = 768 * 1024**2
_MAX_ATTESTATION_BYTES = 65536
_HEADER = struct.Struct('<8sIII')
_MAGIC = b'TRDHLP01'
MAX_ENVELOPE_BYTES = _HEADER.size + _MAX_ATTESTATION_BYTES + _PACKET_LIMIT + 17*MAX_PREFIX_SAMPLES
_PREFIX_KEYS = frozenset(('schema', 'sample_count', 'sample_rate_hz', 'origin_sample',
    'iq_sha256', 'validity_sha256', 'bound_full_iq_sha256', 'bound_full_validity_sha256'))


def _digest(data):
    return hashlib.sha256(data).hexdigest()


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True,
        allow_nan=False).encode('ascii')


def _valid_hash(value):
    return type(value) is str and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def _prefix_receipt(value, count, sample_rate):
    if (type(value) is not dict or frozenset(value) != _PREFIX_KEYS or
            value.get('schema') != PREFIX_SCHEMA or type(count) is not int or
            not 1 <= count <= MAX_PREFIX_SAMPLES or type(sample_rate) is not int or
            sample_rate <= 0 or value.get('sample_count') != count or
            type(value.get('sample_count')) is not int or value.get('sample_rate_hz') != sample_rate or
            type(value.get('sample_rate_hz')) is not int or type(value.get('origin_sample')) is not int or
            value['origin_sample'] != 0 or not all(_valid_hash(value.get(key)) for key in
            ('iq_sha256', 'validity_sha256', 'bound_full_iq_sha256', 'bound_full_validity_sha256'))):
        raise ValueError('invalid Helper-prefix receipt')
    return value


def helper_prefix_sample_count(manifest, grid, search_samples):
    """Derive the receiver Helper extent from independently known geometry."""
    if type(search_samples) is not int or search_samples < 1:
        raise ValueError('invalid public search extent')
    fs = manifest['sample_rate_hz']
    if type(fs) is not int or fs <= 0:
        raise ValueError('invalid public sample rate')
    cal = manifest['calendar']
    values = (float(cal['capture_lead_seconds']), float(cal['data_sample_offset']), float(grid.arrival_upper))
    if not all(math.isfinite(value) for value in values):
        raise ValueError('nonfinite Helper geometry')
    extent = (values[0]+values[1]/fs+max(0., values[2])+1)*fs
    if not math.isfinite(extent):
        raise ValueError('nonfinite Helper extent')
    count = max(0, min(search_samples, math.ceil(extent)))
    if not 1 <= count <= MAX_PREFIX_SAMPLES:
        raise ValueError('Helper prefix exceeds bounded transport')
    return count


def helper_envelope_bound(count):
    """Maximum on-wire bytes for an independently derived prefix length."""
    if type(count) is not int or not 1 <= count <= MAX_PREFIX_SAMPLES:
        raise ValueError('invalid bounded Helper prefix length')
    return _HEADER.size + _MAX_ATTESTATION_BYTES + _PACKET_LIMIT + 17*count


def helper_verification_workspace(count, sample_rate):
    """Conservative extra bytes for bounded reading, decoding and Helper work.

    Callers reserve resident native, bank and full-observation memory separately.
    The bound includes the read bytearray and immutable copy, and the retained
    envelope, decoded slices and arrays during local Helper computation.
    """
    from triad_case_replay import helper_workspace_estimate
    if type(sample_rate) is not int or sample_rate <= 0:
        raise ValueError('invalid Helper sample rate')
    bound = helper_envelope_bound(count)
    scratch = helper_workspace_estimate(count, sample_rate)
    if scratch > MAX_HELPER_WORKSPACE_BYTES:
        raise MemoryError('Helper scratch exceeds verification cap')
    return max(3*bound + 65536, scratch + bound + 34*count + 2*_PACKET_LIMIT)


def capture_local_prefix_anchor(prepared, receiver, signal_work):
    """Hash a prefix while exclusively owning the fitted observation arrays.

    The caller keeps both inputs immutable from acquisition through snapshot;
    full hashes are verified after the snapshot against existing signal work.
    """
    from triad_case_capacity import validate_signal_acquisition_work
    from triad_case_replay import helper_workspace_estimate
    if receiver.manifest.get('helper_enabled', True) is not True:
        raise ValueError('Helper is not enabled')
    count = helper_prefix_sample_count(receiver.manifest, receiver.grid, receiver.samples)
    fs = receiver.sample_rate
    if (type(fs) is not int or fs != receiver.manifest['sample_rate_hz'] or
            helper_workspace_estimate(count, fs) > MAX_HELPER_WORKSPACE_BYTES or
            not validate_signal_acquisition_work(signal_work) or signal_work['sample_count'] != receiver.samples):
        raise ValueError('unsupported Helper geometry or signal work')
    raw, valid = prepared.raw, prepared.valid
    if (type(prepared.observation_first_sample) is not int or prepared.observation_first_sample != 0 or
            type(receiver.capture_origin) is not int or receiver.capture_origin != 0 or raw.shape != (receiver.samples,) or
            raw.dtype != np.dtype('complex128') or not raw.flags.c_contiguous or
            valid.shape != (receiver.samples,) or valid.dtype != np.dtype('uint8') or
            not valid.flags.c_contiguous):
        raise ValueError('invalid observation layout')
    # The acquisition already validated samples; scan hashes without allocating
    # another prefix buffer or timing this diagnostic as receiver processing.
    iq = memoryview(raw[:count]).cast('B')
    mask = memoryview(valid[:count]).cast('B')
    if (type(getattr(receiver, 'helper_peak_bytes', None)) is not int or receiver.helper_peak_bytes <= 0 or
            receiver.helper_peak_bytes > prepared.memory_limit_bytes or
            _digest(memoryview(raw).cast('B')) != signal_work['full_iq_sha256'] or
            _digest(memoryview(valid).cast('B')) != signal_work['full_validity_sha256']):
        raise ValueError('owned full observation changed or prefix is invalid')
    anchor = dict(schema=PREFIX_SCHEMA, sample_count=count, sample_rate_hz=fs, origin_sample=0,
        iq_sha256=_digest(iq), validity_sha256=_digest(mask),
        bound_full_iq_sha256=signal_work['full_iq_sha256'],
        bound_full_validity_sha256=signal_work['full_validity_sha256'])
    return anchor


def snapshot_helper_prefix(prepared, receiver, signal_work):
    """Copy a producer prefix after verifying the same-observation anchor."""
    anchor = capture_local_prefix_anchor(prepared, receiver, signal_work)
    count = anchor['sample_count']
    # Existing Helper peak reserve is conservative. Account the additional
    # copied prefix while both receiver and full input remain resident.
    if receiver.helper_peak_bytes + 17*count > prepared.memory_limit_bytes:
        raise MemoryError('Helper prefix snapshot exceeds workspace cap')
    iq = np.ascontiguousarray(prepared.raw[:count], dtype='<c16').tobytes()
    mask = np.ascontiguousarray(prepared.valid[:count], dtype='u1').tobytes()
    if _digest(iq) != anchor['iq_sha256'] or _digest(mask) != anchor['validity_sha256']:
        raise ValueError('owned observation changed during prefix snapshot')
    return iq, mask, anchor


def encode_helper_envelope(fine_packet, prefix_iq, prefix_validity, attestation):
    """Encode the unchanged fine packet, prefix and original attestation."""
    if type(attestation) is not dict:
        raise ValueError('invalid Helper-prefix attestation')
    meta = _canonical(attestation)
    prefix = attestation.get('helper_prefix')
    if type(prefix) is not dict:
        raise ValueError('missing Helper-prefix receipt')
    count, rate = prefix.get('sample_count'), prefix.get('sample_rate_hz')
    _prefix_receipt(prefix, count, rate)
    if (type(fine_packet) is not bytes or not 1 <= len(fine_packet) <= _PACKET_LIMIT or
            type(prefix_iq) is not bytes or len(prefix_iq) != 16*count or
            type(prefix_validity) is not bytes or len(prefix_validity) != count or
            not 1 <= len(meta) <= _MAX_ATTESTATION_BYTES or
            _digest(prefix_iq) != prefix['iq_sha256'] or _digest(prefix_validity) != prefix['validity_sha256']):
        raise ValueError('invalid Helper-prefix envelope')
    return _HEADER.pack(_MAGIC, len(meta), len(fine_packet), count)+meta+fine_packet+prefix_iq+prefix_validity


def decode_helper_envelope(envelope, *, expected_sample_count, expected_sample_rate_hz, trusted_attestation):
    """Bound transport and compare its receipt with the independent attestation."""
    if (type(envelope) is not bytes or not _HEADER.size <= len(envelope) <= MAX_ENVELOPE_BYTES or
            type(trusted_attestation) is not dict):
        raise ValueError('invalid Helper-prefix envelope')
    magic, meta_len, fine_len, count = _HEADER.unpack_from(envelope)
    if (magic != _MAGIC or not 1 <= meta_len <= _MAX_ATTESTATION_BYTES or
            not 1 <= fine_len <= _PACKET_LIMIT or type(expected_sample_count) is not int or
            count != expected_sample_count or not 1 <= count <= MAX_PREFIX_SAMPLES or
            _HEADER.size+meta_len+fine_len+17*count != len(envelope)):
        raise ValueError('invalid Helper-prefix envelope size')
    start = _HEADER.size
    # Canonical byte comparison rejects duplicate keys without parsing untrusted JSON.
    if envelope[start:start+meta_len] != _canonical(trusted_attestation):
        raise ValueError('embedded and authenticated attestations differ')
    prefix = _prefix_receipt(trusted_attestation.get('helper_prefix'), count, expected_sample_rate_hz)
    start += meta_len
    fine = envelope[start:start+fine_len]
    start += fine_len
    iq_data, mask_data = envelope[start:start+16*count], envelope[start+16*count:]
    if _digest(iq_data) != prefix['iq_sha256'] or _digest(mask_data) != prefix['validity_sha256']:
        raise ValueError('Helper prefix hash mismatch')
    iq = np.frombuffer(iq_data, dtype='<c16')
    valid = np.frombuffer(mask_data, dtype='u1')
    if not np.isfinite(iq).all() or not np.logical_or(valid == 0, valid == 1).all():
        raise ValueError('invalid Helper prefix values')
    return fine, iq.copy(), valid.copy()


def non_helper_work(signal):
    """Return exact signal work without the numerical Helper diagnostic digest."""
    if type(signal) is not dict or type(signal.get('acquisition')) is not dict:
        raise ValueError('invalid acquisition work')
    acquisition = dict(signal['acquisition'])
    if 'helper_result_sha256' not in acquisition:
        raise ValueError('missing Helper result digest')
    del acquisition['helper_result_sha256']
    return dict(signal, acquisition=acquisition)


def verify_helper_prefix(envelope, *, expected_public, validate_grid, trusted_attestation,
                         local_signal_work, local_prefix_anchor, manifest, grid,
                         search_samples, memory_limit_bytes, runtime_identity):
    """Recompute Helper on a locally anchored, same-observation prefix.

    The caller independently constructs public geometry and local runtime
    identity, authenticates the unchanged remote attestation, and supplies the
    matching local fit's signal work and its independently captured anchor. A
    standalone JSON receipt is never proof that this function ran.
    """
    from triad_case_capacity import _work_digest, validate_signal_acquisition_work
    from triad_receiver import _public_metrics
    from triad_replay_producer import canonical_digest, verify_authenticated_receipt
    from triad_weak_helper import estimate_weak_helper
    if (type(expected_public) is not dict or type(trusted_attestation) is not dict or
            type(runtime_identity) is not dict or not runtime_identity or
            type(memory_limit_bytes) is not int or memory_limit_bytes <= 0 or
            manifest.get('helper_enabled', True) is not True):
        raise ValueError('invalid local Helper verification context')
    count = helper_prefix_sample_count(manifest, grid, search_samples)
    fs = manifest['sample_rate_hz']
    _prefix_receipt(local_prefix_anchor, count, fs)
    original = trusted_attestation.get('signal_work')
    if (not validate_signal_acquisition_work(original) or
            not validate_signal_acquisition_work(local_signal_work) or
            trusted_attestation.get('signal_work_sha256') != canonical_digest(original) or
            original['sample_count'] != search_samples or local_signal_work['sample_count'] != search_samples or
            non_helper_work(original) != non_helper_work(local_signal_work) or
            trusted_attestation.get('helper_prefix') != local_prefix_anchor or
            local_prefix_anchor['bound_full_iq_sha256'] != local_signal_work['full_iq_sha256'] or
            local_prefix_anchor['bound_full_validity_sha256'] != local_signal_work['full_validity_sha256']):
        raise ValueError('Helper prefix or non-Helper work differs from the local fit')
    if helper_verification_workspace(count, fs) > memory_limit_bytes:
        raise MemoryError('Helper prefix verification exceeds workspace cap')
    fine, iq, valid = decode_helper_envelope(envelope, expected_sample_count=count,
        expected_sample_rate_hz=fs, trusted_attestation=trusted_attestation)
    replay = decode_packet(fine, expected_public=expected_public, validate_grid=validate_grid,
        validate_acquisition_receipt=lambda receipt, public, digest:
            verify_authenticated_receipt(receipt, public, digest, trusted_attestation))
    if (original['fine_slice_sha256'] != replay['acquisition']['input_sha256'] or
            original['acquisition']['coordinates'] != replay['acquisition']['coordinates']):
        raise ValueError('Helper acquisition differs from fine packet')
    local_manifest = copy.deepcopy(manifest)
    local_manifest['calendar']['capture_samples'] = count
    bound = float(grid.frequency_bound)
    if not math.isfinite(bound) or not 0 <= bound <= 16384:
        raise ValueError('invalid Helper frequency geometry')
    frequencies = np.linspace(-bound, bound, 2*math.ceil(bound/.5)+1) if bound else np.array([0.])
    helper = estimate_weak_helper(iq, local_manifest, frequencies, valid=valid,
        config={'frequency_config': dict(selection_geometry='continuous_interval',
                                         future_horizon_seconds=search_samples/fs)})
    local_helper = _work_digest(_public_metrics(helper))
    if local_helper != local_signal_work['acquisition']['helper_result_sha256']:
        raise ValueError('local canonical Helper differs from local fit')
    receipt = dict(schema=VERIFICATION_SCHEMA, status='verified',
        attestation_sha256=canonical_digest(trusted_attestation), envelope_sha256=_digest(envelope),
        public_sha256=canonical_digest(expected_public), runtime_identity_sha256=canonical_digest(runtime_identity),
        local_signal_work_sha256=canonical_digest(local_signal_work),
        non_helper_work_sha256=canonical_digest(non_helper_work(original)),
        local_prefix_anchor_sha256=canonical_digest(local_prefix_anchor),
        full_iq_sha256=local_prefix_anchor['bound_full_iq_sha256'],
        full_validity_sha256=local_prefix_anchor['bound_full_validity_sha256'],
        prefix_iq_sha256=local_prefix_anchor['iq_sha256'], prefix_validity_sha256=local_prefix_anchor['validity_sha256'],
        remote_helper_sha256=original['acquisition']['helper_result_sha256'], local_helper_sha256=local_helper,
        scope='Local Helper work identity only; no CPU capacity or message-success qualification.')
    return replay, receipt
