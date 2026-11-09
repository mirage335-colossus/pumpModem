"""Transport boundaries for the conditional native fine-work replay."""
import hashlib
import ctypes
import errno
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_replay_packet as packet


def public():
    return dict(manual_list_sha256='a'*64, configuration_sha256='b'*64,
        native_exporter_sha256='c'*64, source_identity_sha256='d'*64,
        calendar_sha256='e'*64, grid_sha256='f'*64, work_identity_sha256='1'*64,
        acquisition_procedure_sha256='2'*64, case_index=7, sample_rate_hz=6000,
        sample_count=36000, calendar_slots=24, slot_index=0, window_first_sample=0,
        window_stop_sample=100000, origin_sample=6000, pattern_epoch=1793850000,
        symbol_ordinal=0)


def acquisition(payload=None):
    if payload is None:
        iq, valid = sample()
        payload = iq.tobytes()+valid.tobytes()
    return dict(schema='triad-complete-public-acquisition-receipt-v1', status='acquired',
        complete_public_search=True, work_only=False, procedure_sha256='2'*64,
        receipt_sha256='3'*64, input_sha256=hashlib.sha256(payload).hexdigest(), coordinates=[[0, -6.345, -.01, 0.0], [0, 0., 0., 0.]])


def sample():
    i = np.arange(36000, dtype=np.float64)
    return (i*1e-6+1j*(i*2e-6)).astype('<c16'), np.ones(36000, dtype='u1')


def decode(blob, expected=None, grid=None, receipt=None):
    return packet.decode_packet(blob, expected_public=public() if expected is None else expected,
        validate_grid=(lambda coords: coords == ((0, -6.345, -.01, 0.0), (0, 0., 0., 0.))) if grid is None else grid,
        validate_acquisition_receipt=(lambda proof, metadata, digest: proof['receipt_sha256'] == '3'*64 and proof['input_sha256'] == digest) if receipt is None else receipt)


def forge(metadata, payload):
    data = json.dumps(metadata, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')
    prefix = packet._HEADER_PREFIX.pack(packet._MAGIC, len(data), 36000, len(payload),
        hashlib.sha256(data).digest(), hashlib.sha256(payload).digest())
    return prefix + hashlib.sha256(prefix).digest() + data + payload


class ReplayPacketTests(unittest.TestCase):
    def setUp(self):
        self.iq, self.valid = sample()
        self.payload = self.iq.tobytes() + self.valid.tobytes()
        self.blob = packet.encode_packet(public(), acquisition(self.payload), self.payload)

    def test_roundtrip_and_verified_local_inputs_are_independent(self):
        actual = decode(self.blob)
        np.testing.assert_array_equal(actual['samples'], self.iq)
        np.testing.assert_array_equal(actual['validity'], self.valid)
        actual['samples'][0] = 123
        self.assertEqual(decode(self.blob)['samples'][0], 0)
        self.assertEqual(actual['public'], public())

    def test_corruption_truncation_oversize_and_mutable_input_rejected(self):
        for position in (0, packet._HEADER.size, len(self.blob)-1):
            with self.subTest(position=position):
                data = bytearray(self.blob); data[position] ^= 1
                with self.assertRaises(packet.ReplayPacketError): decode(bytes(data))
        for data in (self.blob[:-1], self.blob+b'\0', bytearray(self.blob),
                     b'x'*(packet._PACKET_LIMIT+1)):
            with self.subTest(kind=type(data), size=len(data)), self.assertRaises(packet.ReplayPacketError):
                decode(data)

    def test_trusted_public_mismatches_and_grid_and_receipt_are_required(self):
        for key, value in (('manual_list_sha256','9'*64), ('configuration_sha256','9'*64),
                           ('native_exporter_sha256','9'*64), ('source_identity_sha256','9'*64),
                           ('grid_sha256','9'*64), ('calendar_sha256','9'*64),
                           ('work_identity_sha256','9'*64), ('origin_sample', 6001),
                           ('pattern_epoch',1793850001), ('symbol_ordinal',1)):
            expected = public(); expected[key] = value
            with self.subTest(key=key), self.assertRaises(packet.ReplayPacketError): decode(self.blob, expected)
        for grid, receipt in ((lambda _:False,lambda *_:True), (lambda _:True,lambda *_:False),
                              (lambda _:None,lambda *_:True), (lambda _:True,lambda *_:None)):
            with self.assertRaises(packet.ReplayPacketError): decode(self.blob, grid=grid, receipt=receipt)
        def unsafe(_):
            raise ValueError('untrusted input must not escape')
        with self.assertRaises(packet.ReplayPacketError) as context:
            decode(self.blob, grid=unsafe)
        self.assertNotIn('untrusted input', str(context.exception))
        self.assertTrue(context.exception.__suppress_context__)

    def test_partial_work_only_truth_and_unsupported_metadata_rejected(self):
        cases = [dict(status='partial'), dict(complete_public_search=False), dict(work_only=True),
            dict(procedure_sha256='f'*64), dict(coordinates=[]),
            dict(coordinates=[[True, 0., 0., 0.]]), dict(coordinates=[[0, 0., 0., 1e10]]),
            dict(coordinates=[[0, 0., 0., 0.],[0, 0., 0., 0.]]),
            dict(actual_clock_ppm=0), dict(received_length=2)]
        for changes in cases:
            proof = acquisition(); proof.update(changes)
            with self.subTest(changes=changes), self.assertRaises(packet.ReplayPacketError):
                packet.encode_packet(public(), proof, self.payload)
        for key, value in (('case_index',True),('sample_rate_hz',6001),('calendar_slots',23),
                           ('origin_sample',70000),('slot_index',24),('manual_list_sha256','unknown')):
            item = public(); item[key] = value
            with self.subTest(key=key), self.assertRaises(packet.ReplayPacketError):
                packet.encode_packet(item, acquisition(), self.payload)
        item=public();item['transmitted_bit']=1
        with self.assertRaises(packet.ReplayPacketError):
            packet.encode_packet(item, acquisition(), self.payload)

    def test_invalid_iq_and_masks_rejected_both_on_encode_and_decode(self):
        iq=self.iq.copy();iq[8]=np.nan+1j
        with self.assertRaises(packet.ReplayPacketError):packet.encode_packet(public(),acquisition(),iq.tobytes()+self.valid.tobytes())
        valid=self.valid.copy();valid[8]=2
        with self.assertRaises(packet.ReplayPacketError):packet.encode_packet(public(),acquisition(),self.iq.tobytes()+valid.tobytes())
        with self.assertRaises(packet.ReplayPacketError):packet.encode_packet(public(),acquisition(),self.iq.astype('complex64').tobytes()+self.valid.tobytes())
        metadata=dict(schema=packet._SCHEMA,public=public(),acquisition=acquisition())
        with self.assertRaises(packet.ReplayPacketError):decode(forge(metadata,iq.tobytes()+self.valid.tobytes()))
        with self.assertRaises(packet.ReplayPacketError):decode(forge(metadata,self.iq.tobytes()+valid.tobytes()))

    def test_snapshot_is_immutable_and_digest_is_bound_to_trusted_receipt(self):
        with self.assertRaises(packet.ReplayPacketError):
            packet.encode_packet(public(), acquisition(), bytearray(self.payload))
        with self.assertRaises(packet.ReplayPacketError):
            packet.encode_packet(public(), acquisition(), self.iq)
        wrong = acquisition(); wrong['input_sha256'] = '0'*64
        with self.assertRaises(packet.ReplayPacketError):
            packet.encode_packet(public(), wrong, self.payload)
        self.iq[0] = 17 + 1j
        self.valid[0] = 0
        with patch.object(np, 'ascontiguousarray', side_effect=AssertionError('must not recopy arrays')):
            blob = packet.encode_packet(public(), acquisition(self.payload), self.payload)
        seen = []
        def check_receipt(proof, metadata, digest):
            seen.append((proof['input_sha256'], digest))
            proof['coordinates'][0][1] = 999
            return digest == hashlib.sha256(self.payload).hexdigest()
        result = decode(blob, receipt=check_receipt)
        self.assertEqual(result['samples'][0], 0)
        self.assertEqual(result['validity'][0], 1)
        self.assertEqual(result['acquisition']['coordinates'][0][1], -6.345)
        self.assertEqual(seen[0][0], seen[0][1])
        # A validly rehashed forged body cannot pass an independent receipt.
        changed = bytearray(self.payload); changed[0] ^= 1
        changed = bytes(changed)
        proof = acquisition(changed)
        forged = packet.encode_packet(public(), proof, changed)
        trusted_original = hashlib.sha256(self.payload).hexdigest()
        with self.assertRaises(packet.ReplayPacketError):
            decode(forged, receipt=lambda _proof, _public, digest: digest == trusted_original)

    def test_bounded_reader_rejects_symlinks_nonregular_oversized_and_swaps(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root/'sample'; source.write_bytes(self.blob)
            kwargs = dict(expected_public=public(), validate_grid=lambda _coords: True,
                validate_acquisition_receipt=lambda proof, _public, digest: proof['input_sha256'] == digest)
            actual = packet.read_packet(source, **kwargs)
            np.testing.assert_array_equal(actual['samples'], self.iq)
            link = root/'link'; link.symlink_to(source)
            with self.assertRaises(packet.ReplayPacketError): packet.read_packet(link, **kwargs)
            directory = root/'dir'; directory.mkdir()
            parent_link = root/'dirlink'; parent_link.symlink_to(directory, target_is_directory=True)
            with self.assertRaises(packet.ReplayPacketError): packet.read_packet(parent_link/'file', **kwargs)
            with self.assertRaises(packet.ReplayPacketError): packet.write_packet(parent_link/'file', self.blob)
            pipe = root/'pipe'; os.mkfifo(pipe)
            with self.assertRaises(packet.ReplayPacketError): packet.read_packet(pipe, **kwargs)
            huge = root/'huge'
            with huge.open('wb') as stream: stream.truncate(packet._PACKET_LIMIT+1)
            with self.assertRaises(packet.ReplayPacketError): packet.read_packet(huge, **kwargs)
            secret = root/'secret'; secret.write_bytes(b'untouched')
            original_read = os.read
            swapped = []
            def swap_after_open(fd, size):
                block = original_read(fd, size)
                if not swapped:
                    source.rename(root/'old-sample')
                    source.symlink_to(secret)
                    swapped.append(True)
                return block
            with patch.object(packet.os, 'read', side_effect=swap_after_open):
                with self.assertRaises(packet.ReplayPacketError): packet.read_packet(source, **kwargs)
            self.assertEqual(secret.read_bytes(), b'untouched')
            self.assertTrue(source.is_symlink())

    def test_output_atomic_no_replace_existing_file_or_symlink(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root); target=root/'existing';target.write_bytes(b'unchanged')
            out=root/'packet';packet.write_packet(out,self.blob);self.assertEqual(out.read_bytes(),self.blob)
            for dest in (out,target):
                with self.assertRaises(packet.ReplayPacketError):packet.write_packet(dest,self.blob)
            alias=root/'link';alias.symlink_to(target)
            with self.assertRaises(packet.ReplayPacketError):packet.write_packet(alias,self.blob)
            self.assertEqual(target.read_bytes(),b'unchanged')
            self.assertTrue(alias.is_symlink())
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['existing','link','packet'])

    def test_procfs_fallback_uses_same_inode_and_never_replaces(self):
        # Simulate missing CAP_DAC_READ_SEARCH while letting the real fallback
        # syscall and real procfs resolve the actual open anonymous inode.
        native = ctypes.CDLL(None, use_errno=True)
        native.linkat.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                                  ctypes.c_char_p, ctypes.c_int)
        native.linkat.restype = ctypes.c_int
        class LinkFallback:
            argtypes = restype = None
            def __init__(self): self.flags = []; self.before_link = None
            def __call__(self, oldfd, old, newfd, new, flags):
                self.flags.append(flags)
                if flags == 0x1000:
                    ctypes.set_errno(errno.ENOENT)
                    return -1
                if self.before_link is not None:
                    self.before_link()
                return native.linkat(oldfd, old, newfd, new, flags)
        class Library:
            def __init__(self):
                self.linkat = LinkFallback()
                self.fstatfs = native.fstatfs
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = Library()
            with patch.object(packet.ctypes, 'CDLL', return_value=library):
                packet.write_packet(root/'fresh', self.blob)
                self.assertEqual((root/'fresh').read_bytes(), self.blob)
                self.assertEqual(library.linkat.flags, [0x1000, 0x400])
                victim = root/'victim'; victim.write_bytes(b'untouched')
                alias = root/'alias'; alias.symlink_to(victim)
                for dest in (root/'fresh', victim, alias):
                    with self.subTest(dest=dest), self.assertRaises(packet.ReplayPacketError):
                        packet.write_packet(dest, self.blob)
                self.assertEqual(victim.read_bytes(), b'untouched')
                self.assertTrue(alias.is_symlink())
                late = root/'late'
                library.linkat.before_link = lambda: late.write_bytes(b'created concurrently')
                with self.assertRaises(packet.ReplayPacketError):
                    packet.write_packet(late, self.blob)
                self.assertEqual(late.read_bytes(), b'created concurrently')

    def test_procfs_fallback_fails_closed_on_invalid_source_and_other_errors(self):
        native = ctypes.CDLL(None, use_errno=True)
        class LinkError:
            argtypes = restype = None
            def __init__(self, error): self.error = error; self.flags = []
            def __call__(self, _oldfd, _old, _newfd, _new, flags):
                self.flags.append(flags)
                ctypes.set_errno(self.error)
                return -1
        class Library:
            def __init__(self, error):
                self.linkat = LinkError(error)
                self.fstatfs = native.fstatfs
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = Library(errno.EACCES)
            with patch.object(packet.ctypes, 'CDLL', return_value=library):
                with self.assertRaises(packet.ReplayPacketError):
                    packet.write_packet(root/'denied', self.blob)
            self.assertEqual(library.linkat.flags, [0x1000])
            self.assertFalse((root/'denied').exists())
            library = Library(errno.ENOENT)
            real_stat = os.stat
            def wrong_identity(path, *args, **kwargs):
                if kwargs.get('dir_fd') is not None and isinstance(path, str) and path.isdecimal():
                    raise OSError(errno.ENOENT, 'missing proc fd')
                return real_stat(path, *args, **kwargs)
            with patch.object(packet.ctypes, 'CDLL', return_value=library), \
                    patch.object(packet.os, 'stat', side_effect=wrong_identity):
                with self.assertRaises(packet.ReplayPacketError):
                    packet.write_packet(root/'missing', self.blob)
            self.assertEqual(library.linkat.flags, [0x1000])
            self.assertFalse((root/'missing').exists())
            library = Library(errno.ENOENT)
            def mismatched_identity(path, *args, **kwargs):
                actual = real_stat(path, *args, **kwargs)
                if kwargs.get('dir_fd') is not None and isinstance(path, str) and path.isdecimal():
                    return SimpleNamespace(st_dev=actual.st_dev, st_ino=actual.st_ino + 1)
                return actual
            with patch.object(packet.ctypes, 'CDLL', return_value=library), \
                    patch.object(packet.os, 'stat', side_effect=mismatched_identity):
                with self.assertRaises(packet.ReplayPacketError):
                    packet.write_packet(root/'wrong-inode', self.blob)
            self.assertEqual(library.linkat.flags, [0x1000])
            self.assertFalse((root/'wrong-inode').exists())
            library = Library(errno.ENOENT)
            class NotProcfs:
                argtypes = restype = None
                def __call__(self, _fd, _buffer): return 0  # zero f_type
            library.fstatfs = NotProcfs()
            with patch.object(packet.ctypes, 'CDLL', return_value=library):
                with self.assertRaises(packet.ReplayPacketError):
                    packet.write_packet(root/'wrong-fs', self.blob)
            self.assertEqual(library.linkat.flags, [0x1000])
            self.assertFalse((root/'wrong-fs').exists())


if __name__ == '__main__':
    unittest.main()
