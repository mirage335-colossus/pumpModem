"""Guard the diagnostic workload comparison and explicit CUDA requirement."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_gpu_workload import _write_probe_report, compare_work, run_probe
from triad_receiver import _prepare_cuda_linalg, _require_default_cuda_stream


class WorkloadComparisonTests(unittest.TestCase):
    def setUp(self):
        self.records = [dict(work=dict(consumed_samples=12000, native_cache=dict(cache_misses=2)),
            lane_scores=dict(initial_affine=[.1, -.2], adaptive=[.2, -.3]))]

    def test_close_scores_require_identical_work(self):
        candidate = copy.deepcopy(self.records)
        candidate[0]['lane_scores']['adaptive'][0] += 1e-9
        self.assertEqual(compare_work(self.records, candidate), dict(work_match=True, scores_match=True))
        candidate[0]['work']['native_cache']['cache_misses'] += 1
        self.assertEqual(compare_work(self.records, candidate), dict(work_match=False, scores_match=True))
        candidate[0]['lane_scores']['adaptive'][0] += 2e-2
        self.assertEqual(compare_work(self.records, candidate), dict(work_match=False, scores_match=False))

    def test_close_scores_cannot_reverse_the_bit_order(self):
        reference = copy.deepcopy(self.records)
        candidate = copy.deepcopy(self.records)
        reference[0]['lane_scores']['adaptive'] = [1., 1.0001]
        candidate[0]['lane_scores']['adaptive'] = [1.0001, 1.]
        self.assertEqual(compare_work(reference, candidate), dict(work_match=True, scores_match=False))

    def test_missing_checkpoints_cannot_match(self):
        for candidate in ([], self.records * 2):
            self.assertEqual(compare_work(self.records, candidate), dict(work_match=False, scores_match=False))

    def test_device_dispatch_can_differ_but_candidate_work_cannot(self):
        reference = copy.deepcopy(self.records)
        reference[0]['work']['lanes'] = dict(initial_affine=dict(
            work_counts=dict(hypothesis_update_tasks=12, candidate_block_updates=36),
            state_work_regime=dict(initial_states=12, requested_candidate_shards=4,
                maximum_hypothesis_tasks_per_block=12)))
        reference[0]['work']['capacity_transition_coverage'] = dict(
            adaptive_work=dict(hypothesis_update_tasks=12, candidate_block_updates=36),
            adaptive_state_regime=dict(requested_candidate_shards=4, state_cap=256))
        candidate = copy.deepcopy(reference)
        lane = candidate[0]['work']['lanes']['initial_affine']
        lane['work_counts']['hypothesis_update_tasks'] = 3
        lane['state_work_regime'].update(requested_candidate_shards=1,
            maximum_hypothesis_tasks_per_block=3)
        coverage = candidate[0]['work']['capacity_transition_coverage']
        coverage['adaptive_work']['hypothesis_update_tasks'] = 3
        coverage['adaptive_state_regime']['requested_candidate_shards'] = 1
        self.assertTrue(compare_work(reference, candidate)['work_match'])
        self.assertEqual(reference[0]['work']['lanes']['initial_affine']['work_counts']['hypothesis_update_tasks'], 12)
        lane['work_counts']['candidate_block_updates'] += 1
        self.assertFalse(compare_work(reference, candidate)['work_match'])
        lane['work_counts']['candidate_block_updates'] -= 1
        coverage['adaptive_state_regime']['state_cap'] = 128
        self.assertFalse(compare_work(reference, candidate)['work_match'])

    def test_unavailable_cuda_never_runs_cpu_fallback(self):
        with tempfile.TemporaryDirectory() as path, mock.patch('torch.cuda.is_available', return_value=False):
            output = Path(path) / 'probe.json'
            with self.assertRaisesRegex(ValueError, 'CUDA requested but unavailable'):
                run_probe(case_list='unread', case_indices=[7], executable='unrun', workers=2,
                    device='cuda', output=output)
            self.assertFalse(output.exists())

    def test_cuda_requires_selected_device_and_default_stream(self):
        default, other = object(), object()
        with mock.patch('torch.cuda.current_device', return_value=0), \
                mock.patch('torch.cuda.default_stream', return_value=default), \
                mock.patch('torch.cuda.current_stream', return_value=default) as stream:
            _require_default_cuda_stream(0)
            with self.assertRaisesRegex(ValueError, 'selected device'):
                _require_default_cuda_stream(1)
            stream.return_value = other
            with self.assertRaisesRegex(ValueError, 'default stream'):
                _require_default_cuda_stream(0)

    def test_linalg_preflight_only_publishes_successful_initialization(self):
        import torch

        cpu_eye = torch.eye
        def cpu_fixture(*args, **kwargs):
            self.assertEqual(kwargs['device'], 'cuda:0')
            self.assertEqual(kwargs['dtype'], torch.complex128)
            return cpu_eye(*args, **{**kwargs, 'device': 'cpu'})

        with mock.patch('triad_receiver._CUDA_LINALG_READY', set()) as ready, \
                mock.patch('triad_receiver._require_default_cuda_stream'), \
                mock.patch.multiple(torch.cuda, mem_get_info=mock.Mock(return_value=(1000, 2000)),
                    memory_allocated=mock.Mock(return_value=0), memory_reserved=mock.Mock(return_value=0),
                    synchronize=mock.DEFAULT) as cuda, \
                mock.patch('torch.eye', side_effect=cpu_fixture) as fixture:
            with mock.patch('torch.linalg.eigh', side_effect=RuntimeError('simulated linalg failure')):
                with self.assertRaisesRegex(RuntimeError, 'simulated linalg failure'):
                    _prepare_cuda_linalg(0)
            self.assertEqual(ready, set())
            receipt = _prepare_cuda_linalg(0)
            self.assertTrue(receipt['initialized_here'])
            self.assertIn('startup_wall_seconds', receipt)
            self.assertEqual(receipt['device_total_bytes'], 2000)
            cuda['synchronize'].assert_called_once_with(0)
            self.assertEqual(_prepare_cuda_linalg(0), {'device': 0, 'initialized_here': False})
            self.assertEqual(fixture.call_count, 2)

    def test_exclusive_staging_preserves_existing_sidecar_symlink_target(self):
        with tempfile.TemporaryDirectory() as path:
            directory = Path(path)
            output, target = directory / 'probe.json', directory / 'sentinel'
            target.write_text('preserve this')
            sidecar = output.with_suffix('.json.tmp')
            sidecar.symlink_to(target)
            _write_probe_report(output, {'status': 'running'})
            _write_probe_report(output, {'status': 'unresolved'})
            self.assertEqual(target.read_text(), 'preserve this')
            self.assertTrue(sidecar.is_symlink())
            self.assertEqual(json.loads(output.read_text()), {'status': 'unresolved'})


if __name__ == '__main__':
    unittest.main()
