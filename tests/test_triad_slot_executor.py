"""Bounded slot ownership, admission, chronology and permit invariants."""
from pathlib import Path
from contextlib import nullcontext
from types import SimpleNamespace
import json
import os
import select
import signal
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import triad_slot_executor as module


class Pipe:
    def __init__(self):
        self.sent, self.received, self.closed = [], [], False
    def send_bytes(self, value):
        self.sent.append(value)
    def recv_bytes(self, maximum):
        value = self.received.pop(0)
        if len(value) > maximum:
            raise OSError('bounded frame exceeded')
        return value
    def close(self):
        self.closed = True


class Process:
    def __init__(self, pid, alive=True):
        self.pid, self.alive = pid, alive
        self.joined = []
    def is_alive(self):
        return self.alive
    def join(self, timeout):
        self.joined.append(timeout)
    def terminate(self):
        self.alive = False
    def kill(self):
        self.alive = False


class SlotExecutorTests(unittest.TestCase):
    def test_shared_transport_capability_failure_keeps_copy_reason_and_reservation(self):
        with mock.patch.object(module.os, 'memfd_create', side_effect=OSError('unavailable'), create=True):
            selected = module.select_slot_input_transport()
        self.assertEqual(selected['method'], 'pipe_copy')
        self.assertIn('unavailable', selected['reason'])
        self.assertEqual(module.SlotProcessBackend.input_reservation_bytes(257), 4*17*257+4*1024**2)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux sealed input')
    def test_shared_input_seals_exact_strided_arrays_and_borrows_parent_fd(self):
        import fcntl
        for count in (1, 257, 4099):
            values = (np.arange(2*count)+1j*np.arange(2*count))[::2]
            valid = (np.arange(2*count, dtype=np.uint8)%3)[::2]
            owned = module.SharedSlotInput.from_arrays(values, valid)
            self.addCleanup(owned.close)
            self.assertEqual(owned.input_bytes, 17*count)
            self.assertEqual(os.fstat(owned.fd).st_size, 17*count)
            self.assertEqual(owned.storage_bytes+4*1024**2, module.shared_input_reservation_bytes(count))
            opened = owned.open_readonly()
            try:
                self.assertEqual(opened.values.tobytes(), values.tobytes())
                self.assertEqual(opened.valid.tobytes(), valid.tobytes())
                self.assertFalse(opened.values.flags.writeable)
                self.assertFalse(opened.valid.flags.writeable)
                with self.assertRaises(ValueError):opened.values.setflags(write=True)
                with self.assertRaises(OSError):os.pwrite(owned.fd, b'x', 0)
                with self.assertRaises(OSError):os.ftruncate(owned.fd, 1)
                self.assertEqual(fcntl.fcntl(owned.fd, fcntl.F_GET_SEALS)&module._shared_seals()[1],
                                 module._shared_seals()[1])
            finally:
                opened.close()
            self.assertIsNotNone(owned.fd)
            owned.close()
            self.assertIsNone(owned.fd)
            with self.assertRaises(RuntimeError):owned.open_readonly()

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux sealed input')
    def test_shared_view_refuses_release_until_exported_numpy_view_is_gone(self):
        owned = module.SharedSlotInput.from_arrays(np.arange(4, dtype=np.complex128), np.ones(4, np.uint8))
        self.addCleanup(owned.close)
        opened = owned.open_readonly()
        borrowed = opened.values[:2]
        with self.assertRaises(BufferError):opened.close()
        self.assertIsNotNone(opened._mapping)
        del borrowed
        opened.close()

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux descriptor lifetime')
    def test_shared_owner_finalizes_abandoned_or_partially_initialized_fd(self):
        owned = module.SharedSlotInput.from_arrays(np.ones(1, np.complex128), np.ones(1, np.uint8))
        fd = owned.fd
        del owned
        with self.assertRaises(OSError):os.fstat(fd)
        partial = module.SharedSlotInput.__new__(module.SharedSlotInput)
        partial.__del__()  # No fd attribute yet.
        first, second = os.pipe()
        try:
            partial.fd = first
            partial.__del__()
            partial.__del__()
            with self.assertRaises(OSError):os.fstat(first)
        finally:os.close(second)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux exception view lifetime')
    def test_exception_frame_cleanup_breaks_chained_cycles_before_unmap(self):
        owned = module.SharedSlotInput.from_arrays(np.ones(4, np.complex128), np.ones(4, np.uint8))
        self.addCleanup(owned.close)
        opened = owned.open_readonly()
        def failing():
            retained_input = opened.values[::2]
            raise ValueError(str(retained_input.shape))
        try:failing()
        except ValueError as error:retained = error
        context = RuntimeError('context')
        retained.__cause__ = context
        context.__context__ = retained
        module.clear_slot_exception_frames(retained)
        self.assertIsNone(retained.__traceback__)
        self.assertIsNone(retained.__cause__)
        self.assertIsNone(context.__context__)
        opened.close()

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux sealed input')
    def test_received_shared_fd_rejects_wrong_extent_identity_and_missing_seals(self):
        from multiprocessing.reduction import send_handle
        owned = module.SharedSlotInput.from_arrays(np.arange(4, dtype=np.complex128), np.ones(4, np.uint8))
        self.addCleanup(owned.close)
        for update in ({'bytes': 69}, {'inode': owned.descriptor['inode']+1}, {'samples': True}):
            first, second = module.multiprocessing.Pipe()
            try:
                before = len(list(Path('/proc/self/fd').iterdir()))
                send_handle(first, owned.fd, os.getpid())
                with self.assertRaises(ValueError):
                    module._receive_shared_input(second, dict(owned.descriptor, **update), 4)
                self.assertEqual(len(list(Path('/proc/self/fd').iterdir())), before)
            finally:first.close();second.close()
        fd = os.memfd_create('triad-slot-unsealed', os.MFD_CLOEXEC|os.MFD_ALLOW_SEALING)
        os.ftruncate(fd, 68)
        stat = os.fstat(fd)
        first, second = module.multiprocessing.Pipe()
        try:
            send_handle(first, fd, os.getpid())
            descriptor = dict(owned.descriptor, device=stat.st_dev, inode=stat.st_ino)
            with self.assertRaisesRegex(ValueError, 'seals'):
                module._receive_shared_input(second, descriptor, 4)
        finally:first.close();second.close();os.close(fd)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux descriptor receipt')
    def test_shared_fd_handoff_atomically_cloexec_and_rejects_extra_or_malformed_rights(self):
        import array
        import socket
        from multiprocessing.reduction import send_handle
        owned = module.SharedSlotInput.from_arrays(np.ones(1, np.complex128), np.ones(1, np.uint8))
        self.addCleanup(owned.close)
        for token, count in ((b'\x00', 1), (b'\x02', 2), (b'\x01', 2), (b'\x01', 16), (b'\x01', 0)):
            with self.subTest(token=token, descriptor_count=count):
                first, second = module.multiprocessing.Pipe()
                try:
                    before = len(list(Path('/proc/self/fd').iterdir()))
                    with socket.fromfd(first.fileno(), socket.AF_UNIX, socket.SOCK_STREAM) as channel:
                        ancillary = ([(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [owned.fd]*count))]
                                     if count else [])
                        channel.sendmsg([token], ancillary)
                    with self.assertRaises(ValueError):module._receive_shared_fd(second)
                    self.assertEqual(len(list(Path('/proc/self/fd').iterdir())), before)
                finally:first.close();second.close()
        first, second = module.multiprocessing.Pipe()
        received = None
        try:
            send_handle(first, owned.fd, os.getpid())
            with mock.patch.object(module.os, 'set_inheritable', side_effect=AssertionError('must be atomic')):
                received = module._receive_shared_fd(second)
            self.assertFalse(os.get_inheritable(received))
        finally:
            first.close();second.close()
            if received is not None:os.close(received)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux descriptor ownership')
    def test_shared_view_import_failure_closes_owned_fd_but_not_borrowed_parent_fd(self):
        import builtins
        owned = module.SharedSlotInput.from_arrays(np.ones(1, np.complex128), np.ones(1, np.uint8))
        self.addCleanup(owned.close)
        original_import = builtins.__import__
        def importing(name, *args, **kwargs):
            if name == 'numpy':raise ImportError('fixture setup failure')
            return original_import(name, *args, **kwargs)
        duplicate = os.dup(owned.fd)
        with mock.patch.object(builtins, '__import__', side_effect=importing):
            with self.assertRaises(ImportError):
                module._SharedSlotView(duplicate, owned.descriptor, 1, own_fd=True)
            with self.assertRaises(ImportError):owned.open_readonly()
        with self.assertRaises(OSError):os.fstat(duplicate)
        self.assertEqual(os.fstat(owned.fd).st_size, 17)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux descriptor capability')
    def test_capability_probe_uses_the_validated_atomic_fd_receiver(self):
        with mock.patch.object(module, '_receive_shared_fd', side_effect=ValueError('rejected')) as receive:
            selected = module.select_slot_input_transport()
        receive.assert_called_once()
        self.assertEqual(selected['method'], 'pipe_copy')
        self.assertIn('ValueError', selected['reason'])

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux sealed input')
    def test_child_shared_success_and_scoring_error_release_before_completion(self):
        from multiprocessing.reduction import send_handle
        owned = module.SharedSlotInput.from_arrays(np.arange(257, dtype=np.complex128), np.arange(257, dtype=np.uint8))
        self.addCleanup(owned.close)
        source = SimpleNamespace(descriptor={}, process=SimpleNamespace(pid=123))
        for fail in (False, True):
            entered, release = threading.Event(), threading.Event()
            def score(source, manifest, *, values, valid, **kwargs):
                view = values[::2]  # A retained traceback must not pin this export.
                self.assertFalse(values.flags.writeable)
                self.assertEqual(values.tobytes(), np.arange(257, dtype=np.complex128).tobytes())
                self.assertEqual(valid.tobytes(), np.arange(257, dtype=np.uint8).tobytes())
                entered.set()
                self.assertTrue(release.wait(3))
                if fail:
                    try:
                        raise ValueError('nested view '+str(view.shape))
                    except ValueError as error:
                        raise MemoryError('fixture scorer retained '+str(view.shape)) from error
                return dict(row={'status': 'scored'}, bank_work_receipt={})
            parent, child = module.multiprocessing.Pipe()
            thread = threading.Thread(target=module._slot_process, args=(child, (), 1))
            try:
                with mock.patch.object(module, 'initialized_slot_source', return_value=nullcontext(source)), \
                        mock.patch.object(module.os, 'setsid'), \
                        mock.patch.object(module, '_bootstrap_rss', return_value=(1, 1)), \
                        mock.patch.object(module, '_affinity'), \
                        mock.patch.object(module, 'score_public_slot', new=score):
                    thread.start()
                    self.assertTrue(parent.poll(3))
                    self.assertEqual(json.loads(parent.recv_bytes())['kind'], 'ready')
                    header = dict(kind='slot', job_id=0, samples=257, memory_limit_bytes=65536,
                        manifest=dict(calendar=dict(capture_samples=257)), cpu_ids=[], public_address={},
                        source_identity={}, workers=1, receiver_policy=None, input_transport='sealed_memfd',
                        shared_input=owned.descriptor)
                    parent.send_bytes(json.dumps(header).encode())
                    send_handle(parent, owned.fd, os.getpid())
                    self.assertTrue(entered.wait(3))
                    self.assertFalse(parent.poll())
                    release.set()
                    self.assertTrue(parent.poll(3))
                    envelope = json.loads(parent.recv_bytes())
                    self.assertEqual(envelope['kind'], 'completed')
                    payload = json.loads(parent.recv_bytes())
                    self.assertEqual('error_type' in payload, fail)
                    if fail:self.assertEqual(payload['error_type'], 'MemoryError')
                    matches = []
                    for path in Path('/proc/self/fd').iterdir():
                        try:
                            stat = os.stat(path)
                        except FileNotFoundError:continue
                        if stat.st_ino == owned.descriptor['inode'] and stat.st_dev == owned.descriptor['device']:
                            matches.append(path)
                    self.assertEqual(len(matches), 1, 'child fd remained after completion')
                    parent.send_bytes(b'{"kind":"close"}')
                    thread.join(3)
                    self.assertFalse(thread.is_alive())
            finally:
                release.set()
                parent.close()
                thread.join(3)
                child.close()

    def test_shared_submission_failure_never_uses_copy_transport(self):
        backend = self.backend()
        backend.input_transport = 'sealed_memfd'
        job = self.job()
        with mock.patch.object(module, '_rss', return_value=100):
            with self.assertRaises(ValueError):backend.submit(0, workers=1, **job)
        self.assertFalse(backend._active)

    @unittest.skipUnless(hasattr(os, 'memfd_create'), 'Linux sealed input')
    def test_failed_fd_handoff_closes_backend_without_releasing_parent_input(self):
        from multiprocessing import reduction
        backend = self.backend()
        backend.input_transport = 'sealed_memfd'
        job = self.job()
        owned = module.SharedSlotInput.from_arrays(job.pop('values'), job.pop('valid'))
        self.addCleanup(owned.close)
        with mock.patch.object(module, '_rss', return_value=100), \
                mock.patch.object(reduction, 'send_handle', side_effect=BrokenPipeError('fixture')), \
                mock.patch.object(backend, 'close') as close:
            with self.assertRaises(BrokenPipeError):
                backend.submit(0, workers=1, shared_input=owned, **job)
            close.assert_called_once()
        self.assertEqual(len(backend._owners[0]['connection'].sent), 1)
        self.assertIsNotNone(owned.fd)
        self.assertEqual(os.fstat(owned.fd).st_size, 68)

    def test_shared_spawn_auxiliary_has_prestart_allowance_and_larger_live_rss(self):
        from multiprocessing import resource_tracker
        allowance = 32 * 1024**2
        for pid, resident, expected in (
                (None, None, allowance), (False, None, allowance),
                (123, None, allowance), (123, 12 * 1024**2, allowance),
                (123, 40 * 1024**2, 40 * 1024**2)):
            tracker = SimpleNamespace(_pid=pid)
            with mock.patch.object(resource_tracker, '_resource_tracker', tracker), \
                    mock.patch.object(module, '_rss', return_value=resident) as read_rss, \
                    mock.patch.object(resource_tracker, 'getfd', side_effect=AssertionError('must not start tracker')):
                self.assertEqual(module.spawn_auxiliary_reservation_bytes(), expected)
                self.assertEqual(read_rss.call_count, int(type(pid) is int and pid > 0))

    def fixture(self, *, reason=None, missing_bank=False, missing_row=False, clock=None):
        self.closed = []
        manifest = dict(sample_rate_hz=2, calendar=dict(capture_samples=4))
        def bank(source, supplied, **arguments):
            return dict(manifest=supplied, screen=object(), template_identity='public',
                work_receipt={} if missing_bank else dict(preparation_peak_estimate_bytes=128))
        outer = self
        class Receiver:
            def __init__(self, manifest, screen, source, **arguments):
                self.limit = arguments['memory_limit_bytes']
            def receive(self, values, valid):
                if clock is not None:
                    clock[0] = 2.
                row = dict(status='unresolved' if reason else 'scored', reason=reason,
                    slot_classification='AMBIGUOUS' if reason else 'QUIET', stages={})
                if not missing_row:
                    row['workspace'] = dict(estimated_peak_bytes=256, memory_limit_bytes=self.limit)
                return row
            def close(self):
                outer.closed.append(True)
                if clock is not None:
                    clock[0] = 7.
        return dict(source=object(), manifest=manifest, public_address=dict(pattern_epoch=1,symbol_ordinal=0),
            source_identity={}, values=np.ones(4,complex), valid=np.ones(4,np.uint8),
            workers=12, memory_limit_bytes=1024, bank_builder=bank, receiver_factory=Receiver)

    def backend(self):
        result = module.SlotProcessBackend.__new__(module.SlotProcessBackend)
        result.max_processes, result.total_workers = 2, 12
        result._closed, result._failed, result.close_timeout = False, False, 1.
        result.input_transport, result.input_transport_reason = 'pipe_copy', 'fixture'
        result._active, result._cpu_ids = {}, list(range(12))
        result._bootstrap_process_peak_bytes = result._bootstrap_native_peak_bytes = 0
        result._owners = [dict(process=Process(101+i), connection=Pipe(), ready=dict(native_workspace_bytes=64),
            resident_snapshot=200, native_pid=201+i) for i in range(2)]
        self.addCleanup(lambda: None)  # Synthetic processes must never receive real OS signals.
        return result

    def job(self):
        return dict(manifest=dict(calendar=dict(capture_samples=4)), public_address={}, source_identity={},
            values=np.arange(4,dtype=np.complex128), valid=np.ones(4,np.uint8), memory_limit_bytes=1024)

    def completion(self, backend, job_id, *, payload=None, wrong_id=None):
        owner = backend._owners[backend._active[job_id]['owner']]
        body = json.dumps(dict(result=dict(row={})) if payload is None else payload).encode()
        owner['connection'].received.extend([json.dumps(dict(kind='completed',
            job_id=job_id if wrong_id is None else wrong_id, service_start_monotonic=1.,
            service_stop_monotonic=2., payload_bytes=len(body))).encode(), body])
        return owner['connection']

    def test_shared_helper_includes_receiver_teardown_and_bank_work(self):
        clock = [0.]
        args = self.fixture(clock=clock)
        with mock.patch.object(module.time,'perf_counter',side_effect=lambda:clock[0]):
            result = module.score_public_slot(**args)
        self.assertEqual(result['service_seconds'], 7.)
        self.assertEqual(result['service_stop_monotonic'], 7.)
        self.assertEqual(result['row']['ratio_processing_seconds_per_baseband_second'], 3.5)
        self.assertTrue(result['workspace_receipts_complete'])
        self.assertTrue(result['row']['stages']['recurring_bank_preparation']['included_in_service'])
        self.assertEqual(self.closed,[True])

    def test_missing_receipts_remain_incomplete_not_capacity_pass(self):
        for bank_missing, row_missing in ((True,False),(False,True),(True,True)):
            result = module.score_public_slot(**self.fixture(missing_bank=bank_missing,missing_row=row_missing))
            self.assertFalse(result['workspace_receipts_complete'])
            self.assertEqual(self.closed,[True])

    def test_only_definite_workspace_failure_requests_memory_fallback(self):
        with self.assertRaises(MemoryError):
            module.score_public_slot(**self.fixture(reason='complete_helper_transient_memory_limit'))
        self.assertEqual(self.closed,[True])
        result = module.score_public_slot(**self.fixture(reason='parallel_coarse_combined_work_or_memory_limit'))
        self.assertEqual(result['row']['status'],'unresolved')

    def test_preparation_context_closes_on_caller_exception(self):
        with self.assertRaisesRegex(RuntimeError,'caller'):
            with module.prepared_public_slot(**self.fixture()) as prepared:
                self.assertIsNotNone(prepared.receiver)
                raise RuntimeError('caller')
        self.assertEqual(self.closed,[True])

    def test_input_and_bank_admission_precede_receiver_creation(self):
        args=self.fixture();args['memory_limit_bytes']=32
        with self.assertRaises(MemoryError):module.score_public_slot(**args)
        self.assertFalse(self.closed)
        args=self.fixture();args['values']=np.zeros(3,complex)
        with self.assertRaises(ValueError):module.score_public_slot(**args)
        self.assertFalse(self.closed)

    def test_all_worker_job_keeps_permits_until_collected(self):
        backend=self.backend()
        with mock.patch.object(module,'_rss',return_value=100):
            backend.submit(0,workers=12,**self.job())
            with self.assertRaisesRegex(RuntimeError,'shared'):
                backend.submit(1,workers=6,**self.job())
            pipe=self.completion(backend,0)
            with mock.patch.object(module,'wait_connections',return_value=[pipe]):
                self.assertEqual(backend.poll()[0]['job_id'],0)
            backend.submit(1,workers=6,**self.job())
            backend.submit(2,workers=6,**self.job())
        self.assertEqual(backend.active_workers,12)
        a,b=backend._active.values()
        self.assertFalse(set(a['cpu_ids'])&set(b['cpu_ids']))
        with self.assertRaises(RuntimeError):backend.submit(3,workers=1,**self.job())

    def test_two_immutable_snapshots_and_out_of_order_results(self):
        backend=self.backend();job=self.job()
        with mock.patch.object(module,'_rss',return_value=100):
            backend.submit(4,workers=6,**job)
            original=backend._owners[0]['connection'].sent[1]
            job['values'][:]=999
            self.assertEqual(np.frombuffer(original,np.complex128).tolist(),[0,1,2,3])
            backend.submit(5,workers=6,**self.job())
        second=self.completion(backend,5)
        with mock.patch.object(module,'wait_connections',return_value=[second]):
            result=backend.poll()[0]
        self.assertEqual(result['job_id'],5)
        self.assertIn(4,backend._active)
        self.assertEqual(result['input_reservation_bytes'],4*17*4+4*1024**2)
        self.assertGreaterEqual(result['collected_stop_monotonic'],result['parent_result_start_monotonic'])

    def test_transport_failure_is_not_workspace_fallback_or_retry(self):
        backend=self.backend()
        with mock.patch.object(module,'_rss',return_value=100):backend.submit(0,workers=6,**self.job())
        pipe=self.completion(backend,0,wrong_id=77)
        with mock.patch.object(module,'wait_connections',return_value=[pipe]):result=backend.poll()[0]
        self.assertEqual(result['error_phase'],'slot_owner_transport')
        self.assertNotEqual(result['error_type'],'MemoryError')
        with self.assertRaisesRegex(RuntimeError,'failed'):backend.submit(1,workers=6,**self.job())

    def test_close_cleans_native_group_even_after_python_leader_exits(self):
        backend=self.backend();backend._owners[0]['process'].alive=False
        def killed(pid, _signal):
            for owner in backend._owners:
                if owner['process'].pid==pid:owner['process'].alive=False
        with mock.patch.object(module.os,'killpg',side_effect=killed) as kill:
            backend.close()
        self.assertIn(mock.call(101,signal.SIGTERM),kill.call_args_list)
        self.assertIn(mock.call(101,signal.SIGKILL),kill.call_args_list)
        self.assertTrue(all(owner['connection'].closed for owner in backend._owners))
        self.assertTrue(backend._closed)
        backend.close()

    def test_unproven_shutdown_keeps_reservations_and_fails_closed(self):
        backend=self.backend();backend._active[0]=dict(owner=0,workers=12)
        with mock.patch.object(module.os,'killpg'):
            with self.assertRaisesRegex(RuntimeError,'reserved resources'):backend.close()
        self.assertIn(0,backend._active)
        self.assertTrue(backend._failed)
        with mock.patch.object(module.os,'killpg'):
            with self.assertRaisesRegex(RuntimeError,'reserved resources'):backend.close()
        for owner in backend._owners:owner['process'].alive=False
        with mock.patch.object(module.os,'killpg'):backend.close()
        self.assertFalse(backend._active)
        self.assertTrue(backend._shutdown_complete)

    def test_term_resistant_owners_share_grace_with_short_shutdown_deadline(self):
        backend = self.backend()
        backend.close_timeout = .3
        clock, killed_at = [0.], []
        for owner in backend._owners:
            process = owner['process']
            def join(timeout, process=process):
                process.joined.append(timeout)
                if process.alive:
                    clock[0] += timeout
            process.join = join
        def signal_group(pid, sig):
            if sig == signal.SIGKILL:
                killed_at.append(clock[0])
                for owner in backend._owners:
                    if owner['process'].pid == pid:
                        owner['process'].alive = False
        with mock.patch.object(module.time, 'perf_counter', side_effect=lambda: clock[0]), \
                mock.patch.object(module.os, 'killpg', side_effect=signal_group):
            backend.close()
        self.assertEqual(len(killed_at), 2)
        self.assertTrue(all(abs(value-.1) < 1e-12 for value in killed_at))
        self.assertTrue(all(owner['process'].joined[-1] >= .19 for owner in backend._owners))
        self.assertTrue(backend._shutdown_complete)

    def test_explicit_eighteen_and_twenty_four_permits_remain_supported(self):
        for total in (18, 24):
            with self.subTest(total=total):
                backend = self.backend()
                backend.total_workers, backend._cpu_ids = total, []
                with mock.patch.object(module, '_rss', return_value=100):
                    backend.submit(0, workers=total//2, **self.job())
                    backend.submit(1, workers=total//2, **self.job())
                self.assertEqual(backend.active_workers, total)
                self.assertTrue(all(not row['cpu_ids'] for row in backend._active.values()))
                with self.assertRaises(RuntimeError):
                    backend.submit(2, workers=1, **self.job())

    def test_linux_bootstrap_snapshot_parses_post_exec_peak_and_rejects_unknown_units(self):
        with mock.patch.object(module.Path,'read_text',return_value='VmRSS:\t200 kB\nVmHWM:\t250 kB\n'):
            self.assertEqual(module._bootstrap_rss(123),(200*1024,250*1024))
        for text in ('VmRSS: 200 kB\n', 'VmRSS: 200 MB\nVmHWM: 250 MB\n',
                     'VmRSS: 200 kB\nVmHWM: 100 kB\n'):
            with self.subTest(text=text),mock.patch.object(module.Path,'read_text',return_value=text):
                self.assertIsNone(module._bootstrap_rss(123))

    def test_known_bootstrap_ignores_parent_heap_but_keeps_existing_owner_rss(self):
        backend=self.backend()
        self.assertEqual(backend.prospective_owner_bytes(500,64),564)
        backend._bootstrap_process_peak_bytes=244
        backend._bootstrap_native_peak_bytes=80
        self.assertEqual(backend.prospective_owner_bytes(500,64),324)
        self.assertEqual(backend.prospective_owner_bytes(500000,128),372)
        with mock.patch.object(module,'_rss',return_value=600):
            self.assertEqual(backend.resident_bytes,2400)
        self.assertEqual(backend.prospective_owner_bytes(500000,64),324)

    def test_unavailable_bootstrap_preserves_typed_startup_resource_fallback(self):
        pipe=Pipe()
        pipe.received=[b'{"kind":"close"}']
        source=SimpleNamespace(descriptor={},process=SimpleNamespace(pid=234))
        with mock.patch.object(module,'initialized_slot_source',return_value=nullcontext(source)), \
                mock.patch.object(module.os,'setsid'), \
                mock.patch.object(module.os,'getpid',return_value=101), \
                mock.patch.object(module,'_bootstrap_rss',return_value=None):
            module._slot_process(pipe,[],12)
        reply=json.loads(pipe.sent[0])
        self.assertEqual(reply['kind'],'ready')
        self.assertIsNone(reply['process_rss_bytes'])
        self.assertIsNone(reply['native_rss_bytes'])
        self.assertTrue(pipe.closed)

        backend=self.backend()
        backend._owners=[]
        backend._source_spec=()
        backend.startup_timeout,backend.startup_seconds=1.,0.
        parent,child=Pipe(),Pipe()
        parent.received=list(pipe.sent)
        process=Process(101)
        process.start=lambda:None
        context=SimpleNamespace(Pipe=lambda:(parent,child),Process=lambda **kwargs:process)
        with mock.patch.object(module.multiprocessing,'get_context',return_value=context), \
                mock.patch.object(module,'wait_connections',return_value=[parent]), \
                mock.patch.object(backend,'close') as close:
            with self.assertRaises(MemoryError):backend.ensure_processes(1)
            close.assert_called_once()
        self.assertTrue(child.closed)

    def test_native_floor_is_owned_once_by_each_runtime_baseline(self):
        backend=self.backend()
        for owner in backend._owners:
            owner['ready']['native_workspace_bytes']=1000
        with mock.patch.object(module,'_rss',return_value=100):
            self.assertEqual(backend.resident_bytes,2200)
            self.assertEqual(backend.resident_bytes_after_retirement(1),1100)
            self.assertEqual(backend.native_workspace_bytes,2000)

    def test_active_owner_retirement_is_rejected_without_releasing_reservations(self):
        backend=self.backend()
        backend._active[0]=dict(owner=0,workers=12,cpu_ids=list(range(12)))
        with self.assertRaises(RuntimeError):backend.retire_idle(1)
        self.assertEqual(len(backend._owners),2)
        self.assertEqual(backend.active_workers,12)
        self.assertFalse(backend._closed)

    def test_three_owners_share_eighteen_permits_without_hidden_queue(self):
        backend = self.backend()
        backend.max_processes, backend.total_workers, backend._cpu_ids = 3, 18, []
        backend._owners.append(dict(process=Process(103), connection=Pipe(),
            ready=dict(native_workspace_bytes=64), resident_snapshot=200, native_pid=203))
        with mock.patch.object(module, '_rss', return_value=100):
            for index in range(3):
                backend.submit(index, workers=6, **self.job())
            self.assertEqual(backend.active_workers, 18)
            with self.assertRaises(RuntimeError):
                backend.submit(3, workers=1, **self.job())
            pipe = self.completion(backend, 2)
            with mock.patch.object(module, 'wait_connections', return_value=[pipe]):
                self.assertEqual(backend.poll()[0]['job_id'], 2)
            backend.submit(3, workers=6, **self.job())
            self.assertEqual(backend.active_workers, 18)

    def test_owner_bounds_precede_platform_admission(self):
        command = ['/native', '--stream-server', '--stream-kind', 'channel']
        with mock.patch.object(module.Path, 'is_file', return_value=False):
            for owners in (0, 25, True):
                with self.subTest(owners=owners), self.assertRaises(ValueError):
                    module.SlotProcessBackend(command, max_processes=owners, total_workers=24)
            with self.assertRaises(MemoryError):
                module.SlotProcessBackend(command, max_processes=2, total_workers=24)

    def test_native_command_and_result_bounds_are_explicit(self):
        for kind in ('channel','emitted'):
            self.assertEqual(module._source_spec(['/native','--stream-server','--stream-kind',kind]),('/native',(),kind))
        for command in ([],['/native'],['/native','--stream-server','--stream-kind','source']):
            with self.assertRaises(ValueError):module._source_spec(command)
        with mock.patch.object(module,'_RESULT_BYTES',16):
            with self.assertRaises(ValueError):module._send_completed(Pipe(),0,1.,dict(result='x'*20))

    def test_child_transport_compacts_only_diagnostics_and_bounds_decoded_ownership(self):
        full=dict(row=dict(status='scored', coverage=dict(complete_observation=True),
            tracking=dict(large_diagnostic=[1]*10000)),bank_work_receipt=dict(work=10))
        compact=module._transport_result(full)
        self.assertNotIn('tracking',compact['row'])
        self.assertIn('tracking',full['row'])
        self.assertEqual(compact['row']['coverage'],full['row']['coverage'])
        full['row']['reason']='x'*16385
        with self.assertRaises(ValueError):module._transport_result(full)

    @unittest.skipUnless(hasattr(os, 'pidfd_open') and hasattr(os, 'killpg'), 'Linux process ownership check')
    def test_real_spawned_native_owners_exit_after_dead_controller_and_blocked_quit(self):
        # Minimal native handshake only: deliberately never answers quit. No DSP
        # is replaced; this tests the actual spawn/group cancellation boundary.
        with tempfile.TemporaryDirectory(prefix='triad-slot-owner-') as directory:
            script=Path(directory)/'blocked_source.py'
            script.write_text("import json,sys,time\n"
                "identity={'pattern_revision':'independent-apsk-v2','added_amplitude_depth':.25}\n"
                "print(json.dumps({'format':identity}),flush=True)\n"
                "print(json.dumps(dict(identity,fixed_workspace_bytes=64,workspace_budget_bytes=128)),flush=True)\n"
                "while True: time.sleep(1)\n")
            backend=module.SlotProcessBackend([sys.executable,str(script),
                '--stream-server','--stream-kind','channel'],total_workers=2,
                startup_timeout=10.,close_timeout=2.)
            handles=[os.pidfd_open(owner['native_pid']) for owner in backend._owners]
            try:
                self.assertGreater(backend.resident_bytes,0)
                self.assertEqual(backend.owner_count,1)
                self.assertEqual(backend.native_workspace_bytes,128)
                backend.ensure_processes(2)
                handles.append(os.pidfd_open(backend._owners[1]['native_pid']))
                self.assertEqual(backend.owner_count,2)
                self.assertEqual(backend.native_workspace_bytes,256)
                for owner in backend._owners:
                    ready=owner['ready']
                    self.assertGreater(ready['process_rss_bytes'],0)
                    self.assertGreaterEqual(ready['process_bootstrap_peak_rss_bytes'],ready['process_rss_bytes'])
                    self.assertGreaterEqual(ready['native_bootstrap_peak_rss_bytes'],ready['native_rss_bytes'])
                bootstrap=backend.prospective_owner_bytes(2**60,128)
                self.assertEqual(bootstrap,max(o['ready']['process_bootstrap_peak_rss_bytes'] for o in backend._owners)+
                    max(max(o['ready']['native_bootstrap_peak_rss_bytes'],o['ready']['native_workspace_bytes']) for o in backend._owners))
                backend.retire_idle(1)
                self.assertEqual(backend.prospective_owner_bytes(2**60,128),bootstrap)
                self.assertEqual(backend.owner_count,1)
                retired=select.poll();retired.register(handles[1],select.POLLIN)
                self.assertTrue(retired.poll(1000),'retired native owner survived')
                os.kill(backend._owners[0]['process'].pid,signal.SIGKILL)
                backend._owners[0]['process'].join(timeout=1.)
                began=time.monotonic();backend.close()
                self.assertLess(time.monotonic()-began,2.5)
                for handle in handles:
                    poller=select.poll();poller.register(handle,select.POLLIN)
                    self.assertTrue(poller.poll(1000),'owned native process survived backend close')
                self.assertTrue(all(not owner['process'].is_alive() for owner in backend._owners))
            finally:
                backend.close()
                for handle in handles:os.close(handle)


if __name__=='__main__':unittest.main()
