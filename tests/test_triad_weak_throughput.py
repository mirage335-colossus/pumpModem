from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_weak_throughput import run_cadence


class DeferredExecutor:
    """Deterministic saturated pool: execute only when the producer drains."""
    def __init__(self,**kwargs):pass
    def __enter__(self):return self
    def __exit__(self,*args):pass
    def submit(self,callback,*args):
        class Future:
            def result(self):return callback(*args)
        return Future()


class CadenceTests(unittest.TestCase):
    def test_queue_capacity_is_not_hidden_by_waiting_producer(self):
        seen=[]
        with patch('triad_weak_throughput.ThreadPoolExecutor',DeferredExecutor):
            report=run_cadence(list(range(5)),lambda i,c:seen.append(c),
                cadence_seconds=1e-6,workers=1,queue_capacity=1)
        self.assertEqual(seen,[0,1]);self.assertEqual(report['completed'],2)
        self.assertEqual(report['overloads'],3)
        self.assertEqual([r['status']for r in report['records']],['completed','completed','overload','overload','overload'])

    def test_parallel_private_callbacks_and_ordered_results(self):
        barrier=threading.Barrier(2)
        def worker(index,case):
            barrier.wait(timeout=2)
            return dict(index=index,value=case*3)
        report=run_cadence([5,7,11,13],worker,cadence_seconds=.001,workers=2,queue_capacity=4)
        self.assertEqual(report['completed'],4);self.assertEqual(report['errors'],0)
        self.assertEqual([r['result']['value']for r in report['records']],[15,21,33,39])
        self.assertGreaterEqual(report['total_cpu_seconds'],0)
        self.assertEqual(report['total_cpu_seconds'],report['parent_cpu_seconds']+report['waited_child_cpu_seconds'])

    def test_failure_is_reported_without_hiding_later_jobs(self):
        def worker(index,case):
            if index==1:raise ValueError('test failure')
            return case
        report=run_cadence([2,3,4],worker,cadence_seconds=1e-6,workers=1,queue_capacity=3)
        self.assertEqual(report['completed'],2);self.assertEqual(report['errors'],1)
        self.assertEqual(report['records'][1]['error_type'],'ValueError')
        self.assertEqual(report['records'][2]['result'],4)
        for key,value in (('workers',0),('workers',True),('queue_capacity',-1),('cadence_seconds',0)):
            kwargs=dict(cadence_seconds=.1);kwargs[key]=value
            with self.assertRaises(ValueError):run_cadence([1],worker,**kwargs)


if __name__=='__main__':unittest.main()
