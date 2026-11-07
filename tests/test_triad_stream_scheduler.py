from pathlib import Path
import sys
import threading
import unittest

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_stream_scheduler import StreamingWindowScheduler,WindowRequest


class SchedulerTests(unittest.TestCase):
    def test_parallel_workers_private_windows_and_bounded_drain(self):
        entered=[threading.Event(),threading.Event()];release=threading.Event()
        seen={}
        def score(index,raw,valid):
            if index<2:
                entered[index].set()
                if not release.wait(2):raise RuntimeError('test did not release worker')
            seen[index]=raw.copy()
            raw[:]=-123  # Must not corrupt the ring or another worker's copy.
            return index
        scheduler=StreamingWindowScheduler([WindowRequest(str(i),2*i,4)for i in range(3)],
            score,ring_capacity_samples=8,queue_capacity=2,workers=2)
        try:
            scheduler.append(np.arange(6).astype(np.complex64))
            self.assertTrue(all(event.wait(1)for event in entered))
            snapshot=scheduler.snapshot()
            self.assertEqual(snapshot['inflight_indices'],[0,1])
            self.assertIsNone(snapshot['inflight_index'])
            self.assertEqual(snapshot['numeric_array_bound_bytes'],(8+(2+2+1)*4)*9)
            scheduler.append(np.arange(6,8).astype(np.complex64))
            self.assertFalse(scheduler.close(timeout=0)['drained'])
        finally:release.set();report=scheduler.close(timeout=2)
        self.assertTrue(report['drained'])
        self.assertEqual([r['result']for r in report['records']],[0,1,2])
        for i,raw in seen.items():np.testing.assert_array_equal(raw,np.arange(2*i,2*i+4))
        for workers in (0,-1,True,1.5):
            with self.assertRaises(ValueError):
                StreamingWindowScheduler([WindowRequest('bad',0,1)],score,workers=workers)

    def test_overlapping_windows_completion_order_and_large_chunk(self):
        seen=[]
        def score(index,raw,valid):
            seen.append((index,raw.copy(),valid.copy()))
            return dict(sum=float(raw.real.sum()))
        requests=[WindowRequest('long',0,8),WindowRequest('short',3,4),WindowRequest('late',5,6)]
        scheduler=StreamingWindowScheduler(requests,score,ring_capacity_samples=8,queue_capacity=3)
        raw=np.arange(30,dtype=np.float32).astype(np.complex64)
        scheduler.append(raw[:4]);scheduler.append(raw[4:])
        report=scheduler.close(timeout=1)
        self.assertTrue(report['drained']);self.assertEqual([i for i,_,_ in seen],[1,0,2])
        for index,values,mask in seen:
            request=requests[index]
            np.testing.assert_array_equal(values,raw[request.start_sample:request.end_sample])
            self.assertTrue(mask.all())
        self.assertEqual(report['frontier_sample'],30)
        self.assertEqual(report['ring_highwater_samples'],8)
        self.assertTrue(all(r['status']=='scored'for r in report['records']))
        for r in report['records']:
            self.assertLessEqual(r['ready_wall_seconds'],r['submitted_wall_seconds'])
            self.assertLessEqual(r['submitted_wall_seconds'],r['started_wall_seconds'])
            self.assertLessEqual(r['started_wall_seconds'],r['finished_wall_seconds'])
            self.assertGreaterEqual(r['process_cpu_seconds'],0)
            self.assertGreaterEqual(r['completion_backlog_seconds'],r['scoring_wall_seconds'])

    def test_busy_worker_overload_does_not_block_or_corrupt_copies(self):
        entered=threading.Event();release=threading.Event();seen=[]
        def score(index,raw,valid):
            if index==0:
                entered.set()
                if not release.wait(1):raise RuntimeError('test did not release worker')
            seen.append((index,raw.copy()))
            return index
        scheduler=StreamingWindowScheduler([WindowRequest(str(i),4*i,4)for i in range(4)],
            score,ring_capacity_samples=8,queue_capacity=2)
        try:
            scheduler.append(np.arange(4).astype(np.complex64))
            self.assertTrue(entered.wait(1))
            scheduler.append(np.arange(4,40).astype(np.complex64))
            report=scheduler.snapshot()
            self.assertEqual(report['frontier_sample'],40)
            self.assertEqual(report['queue_highwater'],2)
            self.assertEqual(report['records'][3]['status'],'unresolved_overload')
            self.assertFalse(release.is_set())
        finally:release.set();report=scheduler.close(timeout=1)
        self.assertTrue(report['drained']);self.assertEqual([i for i,_ in seen],[0,1,2])
        for index,raw in seen:np.testing.assert_array_equal(raw,np.arange(4*index,4*index+4))

    def test_invalid_gap_and_incomplete_are_never_scored_absence(self):
        seen=[]
        scheduler=StreamingWindowScheduler([WindowRequest(str(i),4*i,4)for i in range(4)],
            lambda i,x,v:seen.append(i),ring_capacity_samples=4)
        scheduler.append(np.array([1,np.nan,2,3],np.complex64),[1,0,1,1])
        scheduler.gap(4)
        scheduler.append(np.ones(4,np.complex64))
        scheduler.append(np.ones(2,np.complex64))
        report=scheduler.close(timeout=1)
        self.assertEqual(seen,[2])
        self.assertEqual([r['status']for r in report['records']],
            ['unresolved_invalid_samples','unresolved_invalid_samples','scored','unresolved_incomplete'])
        self.assertEqual(report['gap_samples'],4)
        self.assertEqual(report['invalid_input_samples'],5)
        self.assertEqual(report['records'][3]['available_samples'],2)

    def test_timeout_is_explicit_and_can_later_drain(self):
        entered=threading.Event();release=threading.Event()
        def score(i,x,v):
            entered.set()
            if not release.wait(1):raise RuntimeError('test did not release worker')
            return 'done'
        scheduler=StreamingWindowScheduler([WindowRequest('test',0,1)],score)
        try:
            scheduler.append(np.ones(1,np.complex64));self.assertTrue(entered.wait(1))
            report=scheduler.close(timeout=0)
            self.assertFalse(report['drained']);self.assertEqual(report['records'][0]['status'],'running')
            with self.assertRaisesRegex(RuntimeError,'closed'):scheduler.append(np.ones(1,np.complex64))
        finally:release.set();report=scheduler.close(timeout=1)
        self.assertTrue(report['drained']);self.assertEqual(report['records'][0]['result'],'done')

    def test_callback_error_does_not_hide_later_work(self):
        def score(i,x,v):
            if i==0:raise ValueError('bounded test failure')
            return float(x.real.sum())
        scheduler=StreamingWindowScheduler([WindowRequest('bad',0,3),WindowRequest('good',3,3)],score)
        scheduler.append(np.ones(6,np.complex64));report=scheduler.close(timeout=1)
        self.assertTrue(report['drained'])
        self.assertEqual(report['records'][0]['status'],'scorer_error')
        self.assertEqual(report['records'][0]['error_type'],'ValueError')
        self.assertEqual(report['records'][1]['result'],3.)

    def test_worker_initializer_sets_thread_local_state_once(self):
        state=threading.local();calls=[]
        def initialize():
            state.ready=True;calls.append(threading.get_ident())
            return {'ready':True}
        def score(i,x,v):
            self.assertTrue(state.ready)
            return i
        scheduler=StreamingWindowScheduler([WindowRequest(str(i),i,1)for i in range(3)],
            score,workers=2,queue_capacity=3,worker_initializer=initialize)
        scheduler.append(np.ones(3,np.complex64));report=scheduler.close(timeout=1)
        self.assertTrue(report['drained']);self.assertEqual(len(calls),2)
        self.assertEqual(len(set(calls)),2)
        self.assertTrue(all(r['status']=='scored'for r in report['records']))
        self.assertTrue(all(r['status']=='ready'for r in report['worker_initializations']))

    def test_worker_initializer_error_drains_without_running_unconfigured_work(self):
        called=[]
        def initialize():raise ValueError('pool limit unavailable')
        scheduler=StreamingWindowScheduler([WindowRequest('x',0,1)],
            lambda *args:called.append(1),worker_initializer=initialize)
        scheduler.append(np.ones(1,np.complex64));report=scheduler.close(timeout=1)
        self.assertTrue(report['drained']);self.assertFalse(called)
        self.assertEqual(report['records'][0]['status'],'scorer_error')
        self.assertIn('worker initialization failed',report['records'][0]['error'])

    def test_input_boundaries_global_integer_clock_and_bounded_gap_receipts(self):
        start=2**60+3;seen=[]
        scheduler=StreamingWindowScheduler([WindowRequest('late',start+4,2)],
            lambda i,x,v:seen.append(x.copy()),start_sample=start,ring_capacity_samples=2,maximum_gap_records=1)
        try:
            for values,valid in (([1],None),([1+1j],[2]),([complex(np.nan,0)],None)):
                with self.assertRaises(ValueError):scheduler.append(values,valid)
                self.assertEqual(scheduler.frontier,start)
            with self.assertRaisesRegex(ValueError,'explicit gap'):
                scheduler.append(np.ones(1,np.complex64),start_sample=start+1)
            scheduler.gap(2);scheduler.gap(2)
            scheduler.append(np.array([2+3j,4+5j],np.complex64),start_sample=start+4)
        finally:report=scheduler.close(timeout=1)
        self.assertEqual(report['frontier_sample'],start+6)
        self.assertEqual(report['gap_events'],2);self.assertTrue(report['gap_records_truncated'])
        np.testing.assert_array_equal(seen[0],[2+3j,4+5j])
        for req,kwargs in (([],{}),([WindowRequest('x',0,0)],{}),
            ([WindowRequest('x',2,1),WindowRequest('y',1,1)],{}),
            ([WindowRequest('x',0,1),WindowRequest('x',1,1)],{}),
            ([WindowRequest('x',0,3)],dict(ring_capacity_samples=2))):
            with self.assertRaises(ValueError):StreamingWindowScheduler(req,lambda i,x,v:None,**kwargs)


if __name__=='__main__':unittest.main()
