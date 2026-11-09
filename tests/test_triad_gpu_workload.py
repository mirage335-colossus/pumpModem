"""Guard the diagnostic workload comparison and explicit CUDA requirement."""
import copy
from contextlib import nullcontext
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import weakref

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_gpu_bank import GpuFFTScreen
from triad_gpu_workload import _write_probe_report, compare_work, run_probe
from triad_realtime_fft import FFTScreen
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

    def test_large_cuda_observation_budget_accounts_for_bounded_upload(self):
        screen = GpuFFTScreen.__new__(GpuFFTScreen)
        screen.samples = 40_652_650
        screen.crop_rows = 67_359
        screen.decimation = 600
        screen.dtype = np.complex128
        screen.taps = range(3000)
        screen.template_chunk_samples = 16384
        screen.resident_estimate = 335_946_624
        expected = screen.resident_estimate+16*(screen.samples+2*(1 << 20))
        self.assertEqual(screen._observation_peak_estimate(), expected)
        screen.memory_limit_bytes = expected-1
        with self.assertRaisesRegex(MemoryError, 'observation filter exceeds the declared workspace'):
            screen.prepare_observation(None)

    def test_cuda_upload_synchronizes_live_host_chunks_including_on_enqueue_failure(self):
        raw = (np.arange(14) + 1j*np.arange(14)[::-1])[::2]
        for interrupt in (False, True):
            with self.subTest(interrupt=interrupt):
                expected = raw.astype(np.complex64)
                storage = np.empty_like(expected)
                pending, copied_sizes = [], []
                device_raw = mock.MagicMock()

                def synchronize():
                    if pending:
                        target, reference = pending.pop()
                        host = reference()
                        self.assertIsNotNone(host)
                        np.copyto(target, host)
                        copied_sizes.append(len(host))

                stream = mock.Mock(synchronize=mock.Mock(side_effect=synchronize))

                def device_view(indices):
                    target = storage[indices]
                    def set_host(host, *, stream):
                        self.assertFalse(pending)
                        self.assertTrue(host.flags.c_contiguous)
                        self.assertEqual(host.dtype, target.dtype)
                        self.assertEqual(host.shape, target.shape)
                        pending.append((target, weakref.ref(host)))
                        if interrupt:
                            raise KeyboardInterrupt
                    return SimpleNamespace(set=set_host)

                device_raw.__getitem__.side_effect = device_view
                filtered = object()
                def filter_observation(taps, values, *, down):
                    self.assertFalse(pending)
                    self.assertIs(values, device_raw)
                    self.assertEqual(down, 2)
                    np.testing.assert_array_equal(storage, expected)
                    return filtered

                upfirdn = mock.Mock(side_effect=filter_observation)
                screen = GpuFFTScreen.__new__(GpuFFTScreen)
                screen.dtype, screen.real_dtype = np.complex64, np.float32
                screen.taps, screen.decimation = [1.], 2
                screen.gpu_work = dict(projected_input_payload_bytes=0,
                    observation_filter_pool_live_snapshot_max_bytes=0)
                screen.cuda = SimpleNamespace(active=nullcontext,
                    cp=SimpleNamespace(empty=mock.Mock(return_value=device_raw), asarray=np.asarray,
                        cuda=SimpleNamespace(get_current_stream=lambda: stream)),
                    pool=SimpleNamespace(used_bytes=lambda: 123), signal=SimpleNamespace(upfirdn=upfirdn))
                with mock.patch('triad_gpu_bank._OBSERVATION_UPLOAD_ROWS', 3):
                    if interrupt:
                        with self.assertRaises(KeyboardInterrupt):
                            screen._filter_observation(raw)
                        upfirdn.assert_not_called()
                        self.assertEqual(copied_sizes, [3])
                        np.testing.assert_array_equal(storage[:3], expected[:3])
                        stream.synchronize.assert_called_once()
                    else:
                        self.assertIs(screen._filter_observation(raw), filtered)
                        self.assertEqual(copied_sizes, [3, 3, 1])
                        self.assertEqual(stream.synchronize.call_count, 4)
                        self.assertEqual(screen.gpu_work['observation_filter_pool_live_snapshot_max_bytes'], 123)
                screen.cuda.cp.empty.assert_called_once_with(raw.shape, dtype=np.complex64)

    def test_standalone_cuda_score_reserves_raw_and_validity_before_filtering(self):
        manifest = dict(sample_rate_hz=400,
            calendar=dict(capture_samples=512, capture_lead_seconds=.2,
                data_sample_offset=0, arrival_prior_seconds=[0., .05]),
            channel=dict(spread_delay_ms=0., spread_doppler_hz=0.),
            format=dict(occupied_bandwidth_hz=20.), power=dict(complex_noise_variance=1.))
        cpu = FFTScreen(manifest, np.ones((1, 2, 128), dtype=np.complex128), (0,),
            clock_ppm=(0.,), output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90.,
            precision='fp64', workers=1)
        screen = GpuFFTScreen.__new__(GpuFFTScreen)
        screen.__dict__.update(cpu.__dict__)
        screen.prepare_observation = mock.Mock(return_value=object())
        screen._coerce_projected = mock.Mock(return_value=object())
        screen._frequency_spectra = mock.Mock(return_value=object())
        screen._frequency_workspace_rows = mock.Mock(return_value=[[], []])
        raw = np.ones(screen.samples, dtype=np.complex128)
        valid = np.ones(screen.samples, dtype=np.uint8)
        frequencies = np.array([0.])
        retained = raw.nbytes+valid.nbytes
        kernel_peak = screen._correlation_plan(frequencies, None, raw_input=True)[
            'estimated_peak_bytes_by_method']['frequency']

        screen.memory_limit_bytes = kernel_peak+retained-1
        result = screen.score(raw, frequencies, valid=valid)
        self.assertEqual(result['reason'], 'correlation_working_memory_limit')
        screen.prepare_observation.assert_not_called()

        screen.memory_limit_bytes = kernel_peak+retained
        result = screen.score(raw, frequencies, valid=valid)
        self.assertEqual(result['status'], 'proposed')
        self.assertEqual(result['work']['estimated_peak_bytes'], screen.memory_limit_bytes)
        self.assertEqual(result['work']['standalone_retained_input_bytes'], retained)
        screen.prepare_observation.assert_called_once_with(raw, valid)

        projected = screen.score_projected(object(), frequencies)
        self.assertEqual(projected['status'], 'proposed')
        self.assertNotIn('standalone_retained_input_bytes', projected['work'])

    def test_lookup_reuse_preserves_logical_template_demand(self):
        reference = copy.deepcopy(self.records)
        reference[0]['work']['native_cache'].update(cache_hits=20,row_tile_references=120)
        candidate = copy.deepcopy(reference)
        candidate[0]['work']['native_cache']['cache_hits'] = 200
        self.assertTrue(compare_work(reference,candidate)['work_match'])
        self.assertEqual(reference[0]['work']['native_cache']['cache_hits'],20)
        candidate[0]['work']['native_cache']['row_tile_references'] += 1
        self.assertFalse(compare_work(reference,candidate)['work_match'])

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
