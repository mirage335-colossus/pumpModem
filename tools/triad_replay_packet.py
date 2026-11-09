"""Bounded, non-pickle transport for one public-acquisition fine-work slice.

The hashes detect corruption; they do not authenticate a remote host or prove
its claimed acquisition. An independently verified transport and a caller-owned
trusted acquisition verifier are both required. A decoded packet is work input,
not a receiver decision, local CPU timing, or capacity qualification.
"""
from __future__ import annotations

import ctypes
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import stat
from typing import Callable

import numpy as np

_MAGIC = b'TRDPKT01'
_HEADER_PREFIX = struct.Struct('<8sIIQ32s32s')
_HEADER = struct.Struct('<8sIIQ32s32s32s')
_SAMPLE_RATE = 6000
_SAMPLE_COUNT = _SAMPLE_RATE * 6
_PAYLOAD_BYTES = _SAMPLE_COUNT * 17
_METADATA_LIMIT = 65536
_PACKET_LIMIT = _HEADER.size + _METADATA_LIMIT + _PAYLOAD_BYTES
_HASH = re.compile(r'[0-9a-f]{64}\Z')
_PUBLIC_KEYS = frozenset(('manual_list_sha256', 'configuration_sha256',
    'native_exporter_sha256', 'source_identity_sha256', 'calendar_sha256',
    'grid_sha256', 'work_identity_sha256', 'acquisition_procedure_sha256',
    'case_index', 'sample_rate_hz', 'sample_count', 'calendar_slots',
    'slot_index', 'window_first_sample', 'window_stop_sample', 'origin_sample',
    'pattern_epoch', 'symbol_ordinal'))
_HASH_KEYS = frozenset(name for name in _PUBLIC_KEYS if name.endswith('_sha256'))
_ACQUISITION_KEYS = frozenset(('schema', 'status', 'complete_public_search',
    'work_only', 'procedure_sha256', 'receipt_sha256', 'input_sha256', 'coordinates'))
_METADATA_KEYS = frozenset(('schema', 'public', 'acquisition'))
_SCHEMA = 'triad-fine-replay-packet-v1'
_ACQUISITION_SCHEMA = 'triad-complete-public-acquisition-receipt-v1'


class ReplayPacketError(ValueError):
    """Controlled error; never includes untrusted received text."""


def _fail():
    raise ReplayPacketError('invalid or unsupported replay packet') from None


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _public(public):
    if type(public) is not dict or frozenset(public) != _PUBLIC_KEYS:
        _fail()
    if any(type(public[key]) is not str or not _HASH.fullmatch(public[key])
           for key in _HASH_KEYS):
        _fail()
    if (not _integer(public['case_index'], 1, 2**31-1) or
            public['sample_rate_hz'] != _SAMPLE_RATE or type(public['sample_rate_hz']) is not int or
            public['sample_count'] != _SAMPLE_COUNT or type(public['sample_count']) is not int or
            public['calendar_slots'] != 24 or type(public['calendar_slots']) is not int or
            not _integer(public['slot_index'], 0, 23) or
            not _integer(public['pattern_epoch'], -(2**63), 2**63-1) or
            not _integer(public['symbol_ordinal'], 0, 2**64-1) or
            not all(_integer(public[name], 0, 2**63-1) for name in
                    ('window_first_sample', 'window_stop_sample', 'origin_sample'))):
        _fail()
    first, last, origin = (public[name] for name in
                           ('window_first_sample', 'window_stop_sample', 'origin_sample'))
    if not first <= origin <= last or origin + _SAMPLE_COUNT > last:
        _fail()
    return public


def _acquisition(receipt, public):
    if type(receipt) is not dict or frozenset(receipt) != _ACQUISITION_KEYS:
        _fail()
    if (receipt['schema'] != _ACQUISITION_SCHEMA or receipt['status'] != 'acquired' or
            receipt['complete_public_search'] is not True or receipt['work_only'] is not False or
            receipt['procedure_sha256'] != public['acquisition_procedure_sha256'] or
            type(receipt['receipt_sha256']) is not str or not _HASH.fullmatch(receipt['receipt_sha256']) or
            type(receipt['input_sha256']) is not str or not _HASH.fullmatch(receipt['input_sha256'])):
        _fail()
    coordinates = receipt['coordinates']
    if type(coordinates) is not list or not 1 <= len(coordinates) <= 12:
        _fail()
    seen = set()
    for row in coordinates:
        if (type(row) is not list or len(row) != 4 or not _integer(row[0], -(2**31), 2**31-1) or
                any(type(value) not in (int, float) or abs(value) > 1e9 or not math.isfinite(value)
                    for value in row[1:])):
            _fail()
        key = tuple(row)
        if key in seen:
            _fail()
        seen.add(key)
    return receipt


def _metadata(metadata):
    if (type(metadata) is not dict or frozenset(metadata) != _METADATA_KEYS or
            metadata['schema'] != _SCHEMA):
        _fail()
    public = _public(metadata['public'])
    _acquisition(metadata['acquisition'], public)
    return metadata


def _reject_constant(_):
    _fail()


def _unique_object(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            _fail()
        obj[key] = value
    return obj


def _encode_metadata(metadata):
    _metadata(metadata)
    try:
        encoded = json.dumps(metadata, sort_keys=True, ensure_ascii=True,
            separators=(',', ':'), allow_nan=False).encode('ascii')
    except (TypeError, ValueError, OverflowError):
        _fail()
    if not 1 <= len(encoded) <= _METADATA_LIMIT:
        _fail()
    try:
        _metadata(json.loads(encoded, object_pairs_hook=_unique_object, parse_constant=_reject_constant))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError, TypeError, ValueError):
        _fail()
    return encoded


def _payload_views(payload):
    if type(payload) is not bytes or len(payload) != _PAYLOAD_BYTES:
        _fail()
    iq = np.frombuffer(payload, dtype='<c16', count=_SAMPLE_COUNT)
    mask = np.frombuffer(payload, dtype='u1', count=_SAMPLE_COUNT, offset=_SAMPLE_COUNT*16)
    if not np.isfinite(iq).all() or not np.logical_or(mask == 0, mask == 1).all():
        _fail()
    return iq, mask


def encode_packet(public: dict, acquisition: dict, payload: bytes) -> bytes:
    """Encode one immutable, coherent IQ-then-validity snapshot.

    The producer must construct ``payload`` while exclusively owning or locking
    both source buffers. This codec cannot establish the producer's lock
    discipline: its caller must separately verify that the independently
    trusted acquisition/work receipt binds this exact input digest.
    """
    _payload_views(payload)
    metadata = _encode_metadata(dict(schema=_SCHEMA, public=public, acquisition=acquisition))
    payload_hash = hashlib.sha256(payload).digest()
    captured = json.loads(metadata, object_pairs_hook=_unique_object, parse_constant=_reject_constant)
    if captured['acquisition']['input_sha256'] != payload_hash.hex():
        _fail()
    prefix = _HEADER_PREFIX.pack(_MAGIC, len(metadata), _SAMPLE_COUNT, len(payload),
        hashlib.sha256(metadata).digest(), payload_hash)
    return prefix + hashlib.sha256(prefix).digest() + metadata + payload


def decode_packet(packet: bytes, *, expected_public: dict, validate_grid: Callable,
                  validate_acquisition_receipt: Callable) -> dict:
    """Verify locally rederived public fields and caller-owned acquisition proof.

    ``validate_grid(coords)`` must confirm all receiver-produced coordinates
    belong to the rederived canonical public grid. ``validate_acquisition_receipt``
    must verify the complete public-search receipt, its provenance and the
    provided input digest using a trusted source outside this packet. Both
    callbacks must explicitly return True.
    Even then, the caller must validate workload coverage and local service time.
    """
    if type(expected_public) is not dict:
        _fail()
    expected = _public(dict(expected_public))
    if not callable(validate_grid) or not callable(validate_acquisition_receipt):
        _fail()
    if type(packet) is not bytes:
        _fail()
    view = memoryview(packet)
    if view.ndim != 1 or view.itemsize != 1 or not _HEADER.size <= view.nbytes <= _PACKET_LIMIT:
        _fail()
    try:
        magic, meta_size, count, payload_size, meta_hash, payload_hash, header_hash = _HEADER.unpack_from(view)
    except (ValueError, struct.error, BufferError):
        _fail()
    if (hashlib.sha256(view[:_HEADER_PREFIX.size]).digest() != header_hash or
            magic != _MAGIC or count != _SAMPLE_COUNT or not 1 <= meta_size <= _METADATA_LIMIT or
            payload_size != _PAYLOAD_BYTES or _HEADER.size + meta_size + payload_size != view.nbytes):
        _fail()
    meta_view = view[_HEADER.size:_HEADER.size+meta_size]
    body = view[_HEADER.size+meta_size:]
    if hashlib.sha256(meta_view).digest() != meta_hash or hashlib.sha256(body).digest() != payload_hash:
        _fail()
    try:
        metadata = json.loads(meta_view.tobytes().decode('ascii'), object_pairs_hook=_unique_object,
                              parse_constant=_reject_constant)
        _metadata(metadata)
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError, TypeError, ValueError):
        _fail()
    if metadata['public'] != expected:
        _fail()
    iq_view = np.frombuffer(body[:_SAMPLE_COUNT*16], dtype='<c16', count=_SAMPLE_COUNT)
    mask_view = np.frombuffer(body[_SAMPLE_COUNT*16:], dtype='u1', count=_SAMPLE_COUNT)
    if not np.isfinite(iq_view).all() or not np.logical_or(mask_view == 0, mask_view == 1).all():
        _fail()
    receipt = metadata['acquisition']
    if receipt['input_sha256'] != payload_hash.hex():
        _fail()
    coords = tuple(tuple(row) for row in receipt['coordinates'])
    try:
        receipt_copy = json.loads(json.dumps(receipt, allow_nan=False))
        if validate_grid(coords) is not True or validate_acquisition_receipt(receipt_copy, dict(expected), payload_hash.hex()) is not True:
            _fail()
    except Exception:
        _fail()
    # Copy so a caller cannot mutate the original packet after validation.
    return dict(public=dict(expected), acquisition=receipt, samples=iq_view.copy(), validity=mask_view.copy(),
        scope='Validated short-work transport only; integrity is not authentication or throughput qualification.')


def _open_parent(path):
    """Open a caller-selected directory through no-follow component handles."""
    fd = None
    try:
        destination = Path(path)
        if (destination.name in ('', '.', '..') or '..' in destination.parts or
                not hasattr(os, 'O_NOFOLLOW') or not hasattr(os, 'O_DIRECTORY')):
            _fail()
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        fd = os.open('/' if destination.is_absolute() else '.', flags)
        parents = destination.parent.parts
        if destination.is_absolute():
            parents = parents[1:]
        for component in parents:
            if component in ('', '.'):
                continue
            next_fd = os.open(component, flags, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        result = fd, destination.name
        fd = None
        return result
    except (OSError, TypeError, ValueError):
        _fail()
    finally:
        if fd is not None:
            os.close(fd)


def _file_identity(st):
    return st.st_dev, st.st_ino, st.st_mode, st.st_nlink, st.st_size, st.st_mtime_ns, st.st_ctime_ns


def read_bounded_file(path: os.PathLike | str, *, minimum: int, maximum: int) -> bytes:
    """Read a stable regular file under the caller's byte cap without following links."""
    directory = descriptor = None
    try:
        directory, name = _open_parent(path)
        descriptor = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        before = os.fstat(descriptor)
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink < 1 or
                not minimum <= before.st_size <= maximum):
            _fail()
        if _file_identity(os.stat(name, dir_fd=directory, follow_symlinks=False)) != _file_identity(before):
            _fail()
        data = bytearray()
        while len(data) <= maximum:
            chunk = os.read(descriptor, min(65536, maximum + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
        after = os.fstat(descriptor)
        listed_after = os.stat(name, dir_fd=directory, follow_symlinks=False)
        if (len(data) != before.st_size or len(data) > maximum or
                _file_identity(after) != _file_identity(before) or
                _file_identity(listed_after) != _file_identity(before)):
            _fail()
        return bytes(data)
    except (OSError, TypeError, ValueError, AttributeError):
        _fail()
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if directory is not None:
            os.close(directory)


def read_packet(path: os.PathLike | str, *, expected_public: dict, validate_grid: Callable,
                validate_acquisition_receipt: Callable) -> dict:
    """Read and validate a bounded immutable replay from a caller-selected path."""
    data = read_bounded_file(path, minimum=_HEADER.size, maximum=_PACKET_LIMIT)
    return decode_packet(data, expected_public=expected_public, validate_grid=validate_grid,
                         validate_acquisition_receipt=validate_acquisition_receipt)


def write_new_file(path: os.PathLike | str, content: bytes) -> None:
    """Atomically install without overwriting any existing file or symlink.

    Linux O_TMPFILE and linkat anchor installation to the open anonymous inode
    and no-follow directory handles. Where AT_EMPTY_PATH is unavailable to the
    process, a verified procfs descriptor link provides the same inode anchor.
    No temporary pathname can be swapped. If these operations are unavailable,
    fail closed. Packet metadata never supplies a path.
    """
    if type(content) is not bytes:
        _fail()
    directory = descriptor = None
    try:
        if not hasattr(os, 'O_TMPFILE'):
            _fail()
        directory, name = _open_parent(path)
        descriptor = os.open('.', os.O_RDWR | os.O_TMPFILE, 0o600, dir_fd=directory)
        data = memoryview(content)
        while data:
            count = os.write(descriptor, data)
            if count <= 0:
                _fail()
            data = data[count:]
        os.fsync(descriptor)
        libc = ctypes.CDLL(None, use_errno=True)
        linkat = libc.linkat
        linkat.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int)
        linkat.restype = ctypes.c_int
        if linkat(descriptor, b'', directory, os.fsencode(name), 0x1000) != 0:
            # Linux may report ENOENT for AT_EMPTY_PATH when the process lacks
            # CAP_DAC_READ_SEARCH, despite having created the anonymous inode.
            # Do not retry destination conflicts or other errors through procfs.
            if ctypes.get_errno() != errno.ENOENT:
                _fail()
            proc_directory = None
            try:
                proc_directory = os.open('/proc/self/fd',
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
                # Only a kernel procfs fd directory supplies stable magic links;
                # a caller-controlled replacement directory is not acceptable.
                fstatfs = libc.fstatfs
                fstatfs.argtypes = (ctypes.c_int, ctypes.c_void_p)
                fstatfs.restype = ctypes.c_int
                fs_info = ctypes.create_string_buffer(256)
                if (fstatfs(proc_directory, fs_info) != 0 or
                        ctypes.c_long.from_buffer(fs_info).value != 0x9fa0):
                    _fail()
                fd_name = str(descriptor)
                expected = os.fstat(descriptor)
                resolved = os.stat(fd_name, dir_fd=proc_directory, follow_symlinks=True)
                if (not stat.S_ISREG(expected.st_mode) or expected.st_nlink != 0 or
                        (resolved.st_dev, resolved.st_ino) != (expected.st_dev, expected.st_ino)):
                    _fail()
                if linkat(proc_directory, os.fsencode(fd_name), directory,
                          os.fsencode(name), 0x400) != 0:  # AT_SYMLINK_FOLLOW
                    _fail()
            finally:
                if proc_directory is not None:
                    os.close(proc_directory)
    except (OSError, TypeError, ValueError, AttributeError):
        _fail()
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if directory is not None:
            os.close(directory)


def write_packet(path: os.PathLike | str, packet: bytes) -> None:
    """Install one already encoded replay at a new destination."""
    if type(packet) is not bytes or not _HEADER.size <= len(packet) <= _PACKET_LIMIT:
        _fail()
    write_new_file(path, packet)
