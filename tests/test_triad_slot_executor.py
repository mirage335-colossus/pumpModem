"""Bounded slot ownership, admission, chronology and permit invariants."""
from pathlib import Path
from types import SimpleNamespace
import json
import os
import select
import signal
import sys
import tempfile
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
        result._active, result._cpu_ids = {}, list(range(12))
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
                self.assertEqual(backend.native_workspace_bytes,256)
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
