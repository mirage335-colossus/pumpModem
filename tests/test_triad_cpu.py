"""CPU allocation policy checks; no numerical work or hardware benchmarks."""
from pathlib import Path
import sys
import tempfile
import threading
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_capacity
import triad_cpu


class TriadCpuTests(unittest.TestCase):
    def setUp(self):
        state = patch.multiple(triad_cpu, _NUMERICAL_LIMITER=None,
                               _NUMERICAL_CONTROLLER=None, _CONFIGURED_LINEAR_THREADS=None,
                               _NUMERICAL_CONFIGURATION=None, _WORKER_NUMERICAL=threading.local())
        state.start()
        self.addCleanup(state.stop)

    def fixture(self, kind='v2'):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        proc, mount = root / 'proc', root / 'cgroup space'
        (proc / 'self').mkdir(parents=True)
        (mount / 'job').mkdir(parents=True)
        escaped = str(mount).replace(' ', r'\040')
        member, tail = ('0::/tenant/job', 'cgroup2 cgroup rw') if kind == 'v2' else (
            '2:cpu,memory:/tenant/job', 'cgroup cgroup rw,cpu,memory')
        (proc / 'self/cgroup').write_text(member + '\n')
        (proc / 'self/mountinfo').write_text(f'1 0 0:1 /tenant {escaped} rw - {tail}\n')
        return proc, mount

    def test_automatic_uses_all_budget_without_workspace_explosion(self):
        for cpus, jobs, expected in [(1, 8, (1, 1)), (12, 1, (1, 12)),
                                     (12, 8, (2, 6)), (3, 8, (1, 3)),
                                     (128, 200, (2, 64))]:
            result = triad_cpu.resolve_cpu_budget(jobs=jobs, capacity=cpus)
            self.assertEqual((result['workers'], result['threads']), expected)
            self.assertEqual(result['total_thread_budget'], cpus)
            self.assertEqual(result['linear_algebra_threads'], expected[1])
            self.assertFalse(result['explicit_overcommit'])

    def test_explicit_settings_are_preserved(self):
        for kwargs, expected in [({'workers': 3}, (3, 4)),
                                 ({'threads': 2}, (2, 2)),
                                 ({'workers': 1, 'threads': 2}, (1, 2)),
                                 ({'workers': 20}, (20, 1)),
                                 ({'threads': 20}, (1, 20)),
                                 ({'workers': 2, 'threads': 9}, (2, 9))]:
            result = triad_cpu.resolve_cpu_budget(jobs=8, capacity=12, **kwargs)
            self.assertEqual((result['workers'], result['threads']), expected)
            self.assertEqual(result['explicit_overcommit'], expected[0]*expected[1] > 12)
            self.assertEqual(result['linear_algebra_threads'], expected[1])
        self.assertEqual(triad_cpu.resolve_cpu_budget(jobs=1, threads=2, capacity=12)['workers'], 1)
        self.assertEqual(triad_cpu.resolve_cpu_budget(threads=6, capacity=12,
                         linear_algebra_limit=2)['linear_algebra_threads'], 6)

    def test_invalid_counts_and_capacity_fail(self):
        for name in ('jobs', 'workers', 'threads', 'capacity', 'linear_algebra_limit'):
            for value in (True, 0, -1, 1.5, '2'):
                with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                    triad_cpu.resolve_cpu_budget(**{name: value})
        with self.assertRaises(ValueError):
            triad_cpu.resolve_cpu_budget(capacity={})

    def test_affinity_and_portable_fallback(self):
        with patch.object(triad_cpu.sys, 'platform', 'darwin'), \
             patch.object(build_capacity.os, 'cpu_count', return_value=64), \
             patch.object(build_capacity.os, 'sched_getaffinity', return_value={2, 4, 6}, create=True):
            result = triad_cpu.detect_cpu_budget()
            self.assertEqual(result['available_cpus'], 3)
            self.assertEqual(result['quota_status'], 'not_applicable')
        with patch.object(triad_cpu.sys, 'platform', 'darwin'), \
             patch.object(build_capacity.os, 'cpu_count', return_value=None), \
             patch.object(build_capacity.os, 'sched_getaffinity', side_effect=OSError, create=True):
            self.assertEqual(triad_cpu.detect_cpu_budget()['available_cpus'], 1)

    def test_v2_parent_quota_and_irrelevant_bad_memory(self):
        proc, mount = self.fixture()
        (mount / 'cpu.max').write_text('350000 100000')
        (mount / 'job/cpu.max').write_text('max 100000')
        (mount / 'memory.max').write_text('invalid')
        with patch.object(triad_cpu, 'usable_cpus', return_value=12), \
             patch.object(triad_cpu.sys, 'platform', 'linux'):
            result = triad_cpu.detect_cpu_budget(proc)
        self.assertEqual(result['available_cpus'], 3)
        self.assertEqual(result['quota_status'], 'visible')
        self.assertEqual(result['quota_limits'][0]['quota'], 350000)
        self.assertEqual(result['warnings'], [])

    def test_v1_parent_and_sub_cpu_quota(self):
        proc, mount = self.fixture('v1')
        (mount / 'cpu.cfs_quota_us').write_text('800000')
        (mount / 'cpu.cfs_period_us').write_text('100000')
        (mount / 'job/cpu.cfs_quota_us').write_text('50000')
        (mount / 'job/cpu.cfs_period_us').write_text('100000')
        with patch.object(triad_cpu, 'usable_cpus', return_value=12), \
             patch.object(triad_cpu.sys, 'platform', 'linux'):
            result = triad_cpu.detect_cpu_budget(proc)
        self.assertEqual(result['available_cpus'], 1)
        self.assertEqual([x['cpus'] for x in result['quota_limits']], [1, 8])

    def test_unknown_quota_is_explicit_and_known_parent_remains(self):
        proc, mount = self.fixture()
        (mount / 'cpu.max').write_text('400000 100000')
        (mount / 'job/cpu.max').write_text('broken 0')
        with patch.object(triad_cpu, 'usable_cpus', return_value=12), \
             patch.object(triad_cpu.sys, 'platform', 'linux'):
            result = triad_cpu.detect_cpu_budget(proc)
            unavailable = triad_cpu.detect_cpu_budget(proc / 'missing')
        self.assertEqual(result['available_cpus'], 4)
        self.assertEqual(result['quota_status'], 'partial')
        self.assertTrue(result['warnings'])
        self.assertEqual(unavailable['available_cpus'], 12)
        self.assertEqual(unavailable['quota_status'], 'unknown')
        self.assertTrue(unavailable['warnings'])

    def test_detection_is_used_only_when_capacity_unspecified(self):
        receipt = dict(available_cpus=6, quota_status='visible')
        with patch.object(triad_cpu, 'detect_cpu_budget', return_value=receipt) as detect:
            result = triad_cpu.resolve_cpu_budget(jobs=4)
            self.assertEqual((result['workers'], result['threads']), (2, 3))
            self.assertEqual(result['capacity'], receipt)
            triad_cpu.resolve_cpu_budget(capacity=2)
            self.assertEqual(detect.call_count, 1)

    def test_numerical_configuration_limits_loaded_pools(self):
        import threading
        from unittest.mock import Mock
        torch = types.ModuleType('torch')
        torch.set_num_threads = Mock()
        torch.get_num_threads = Mock(return_value=6)
        torch.get_num_interop_threads = Mock(side_effect=[2, 1])
        torch.set_num_interop_threads = Mock()
        pools = types.ModuleType('threadpoolctl')
        controller = Mock()
        controller.limit.return_value = object()
        controller.info.return_value = [dict(internal_api='openblas', num_threads=6)]
        pools.ThreadpoolController = Mock(return_value=controller)
        scipy = types.ModuleType('scipy')
        scipy.linalg = types.ModuleType('scipy.linalg')
        with patch.dict(sys.modules, {'torch': torch, 'threadpoolctl': pools,
                                      'numpy': types.ModuleType('numpy'), 'scipy': scipy,
                                      'scipy.linalg': scipy.linalg}), \
             patch.dict(triad_cpu.os.environ, {}, clear=True), \
             patch.object(triad_cpu, '_NUMERICAL_LIMITER', None):
            receipt = triad_cpu.configure_numerical_threads(dict(threads=6))
            self.assertEqual(triad_cpu.os.environ['OPENBLAS_NUM_THREADS'], '6')
            self.assertEqual(receipt['pools'][0]['num_threads'], 6)
            torch.set_num_threads.assert_called_once_with(6)
            torch.set_num_interop_threads.assert_called_once_with(1)
            controller.limit.assert_called_once_with(limits=6)
            self.assertEqual(receipt['warnings'], [])
        with patch.object(threading, 'current_thread', return_value=object()):
            with self.assertRaisesRegex(RuntimeError, 'main controller'):
                triad_cpu.configure_numerical_threads(dict(threads=6))

    def test_initialized_interop_pool_is_reported(self):
        from unittest.mock import Mock
        torch = types.ModuleType('torch')
        torch.set_num_threads = Mock()
        torch.get_num_threads = Mock(return_value=2)
        torch.get_num_interop_threads = Mock(return_value=2)
        torch.set_num_interop_threads = Mock(side_effect=RuntimeError('already running'))
        pools = types.ModuleType('threadpoolctl')
        controller = Mock()
        controller.limit.return_value = object()
        controller.info.return_value = []
        pools.ThreadpoolController = Mock(return_value=controller)
        scipy = types.ModuleType('scipy'); scipy.linalg = types.ModuleType('scipy.linalg')
        with patch.dict(sys.modules, {'torch': torch, 'threadpoolctl': pools,
                                      'numpy': types.ModuleType('numpy'), 'scipy': scipy,
                                      'scipy.linalg': scipy.linalg}), \
             patch.dict(triad_cpu.os.environ, {}, clear=True), \
             patch.object(triad_cpu, '_NUMERICAL_LIMITER', None):
            receipt = triad_cpu.configure_numerical_threads(dict(threads=2))
            self.assertEqual(receipt['torch_interop_threads'], 2)
            self.assertIn('already initialized', receipt['warnings'][0])

    def test_automatic_fft_and_linear_algebra_budgets_are_separate(self):
        from unittest.mock import Mock
        torch = types.ModuleType('torch')
        torch.set_num_threads = Mock()
        torch.get_num_threads = Mock(return_value=2)
        torch.get_num_interop_threads = Mock(return_value=1)
        torch.set_num_interop_threads = Mock()
        pools = types.ModuleType('threadpoolctl')
        controller = Mock()
        controller.limit.return_value = object()
        controller.info.return_value = []
        pools.ThreadpoolController = Mock(return_value=controller)
        scipy = types.ModuleType('scipy'); scipy.linalg = types.ModuleType('scipy.linalg')
        with patch.dict(sys.modules, {'torch': torch, 'threadpoolctl': pools,
                                      'numpy': types.ModuleType('numpy'), 'scipy': scipy,
                                      'scipy.linalg': scipy.linalg}), \
             patch.dict(triad_cpu.os.environ, {}, clear=True), \
             patch.object(triad_cpu, '_NUMERICAL_LIMITER', None):
            allocation = triad_cpu.resolve_cpu_budget(capacity=12, linear_algebra_limit=2)
            receipt = triad_cpu.configure_numerical_threads(allocation)
            self.assertEqual(receipt['fft_thread_budget'], 12)
            self.assertEqual(receipt['linear_algebra_threads'], 2)
            self.assertEqual(triad_cpu.os.environ['OMP_NUM_THREADS'], '2')
            torch.set_num_threads.assert_called_once_with(2)
            torch.set_num_interop_threads.assert_not_called()
            controller.limit.assert_called_once_with(limits=2)

    def test_linear_algebra_limit_is_opt_in_to_the_receiver_family(self):
        full = triad_cpu.resolve_cpu_budget(capacity=12)
        self.assertEqual((full['threads'], full['linear_algebra_threads']), (12, 12))
        for jobs, threads in [(1, 12), (8, 6)]:
            limited = triad_cpu.resolve_cpu_budget(jobs=jobs, capacity=12, linear_algebra_limit=2)
            self.assertEqual((limited['threads'], limited['linear_algebra_threads']), (threads, 2))
            self.assertEqual(limited['requested']['linear_algebra_limit'], 2)
        self.assertIsNone(full['requested']['linear_algebra_limit'])

    def test_worker_initialization_applies_thread_local_limit_once(self):
        from unittest.mock import Mock
        local = threading.local()
        calls = []
        class LocalController:
            def limit(self, *, limits):
                calls.append((threading.get_ident(), limits))
                local.threads = limits
                return object()
            def info(self):
                return [dict(internal_api='openmp', num_threads=getattr(local, 'threads', 99))]
        controller = LocalController()
        torch = types.ModuleType('torch')
        torch.set_num_threads = Mock()
        torch.get_num_threads = Mock(return_value=2)
        torch.get_num_interop_threads = Mock(return_value=1)
        torch.set_num_interop_threads = Mock()
        pools = types.ModuleType('threadpoolctl')
        pools.ThreadpoolController = Mock(return_value=controller)
        scipy = types.ModuleType('scipy'); scipy.linalg = types.ModuleType('scipy.linalg')
        allocation = triad_cpu.resolve_cpu_budget(capacity=12, linear_algebra_limit=2)
        results = []
        with patch.dict(sys.modules, {'torch': torch, 'threadpoolctl': pools,
                                      'numpy': types.ModuleType('numpy'), 'scipy': scipy,
                                      'scipy.linalg': scipy.linalg}), \
             patch.dict(triad_cpu.os.environ, {}, clear=True):
            configured = triad_cpu.configure_numerical_threads(allocation)
            self.assertEqual(configured['pools'][0]['num_threads'], 2)
            def worker():
                results.append(controller.info()[0]['num_threads'])
                results.append(triad_cpu.initialize_numerical_worker(allocation))
                results.append(triad_cpu.initialize_numerical_worker(allocation))
                try:
                    triad_cpu.initialize_numerical_worker(dict(threads=3))
                except ValueError:
                    results.append('different allocation rejected')
            thread = threading.Thread(target=worker)
            thread.start(); thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertEqual(results[0], 99)  # Main-thread setting was not inherited.
            self.assertEqual(results[1]['pools'][0]['num_threads'], 2)
            self.assertEqual(results[1], results[2])
            self.assertEqual(results[3], 'different allocation rejected')
            self.assertEqual(len(calls), 2)  # Once on main, once on worker.
            torch.set_num_threads.assert_called_once_with(2)
            torch.set_num_interop_threads.assert_not_called()
            pools.ThreadpoolController.assert_called_once_with()

    def test_worker_initialization_requires_controller_configuration(self):
        with self.assertRaisesRegex(RuntimeError, 'before initializing'):
            triad_cpu.initialize_numerical_worker(dict(threads=2))


if __name__ == '__main__':
    unittest.main()
