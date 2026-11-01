#!/usr/bin/env python3
"""Actual-workspace contract checks for the bounded phase/channel adapter."""
import contextlib
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
SPEC = importlib.util.spec_from_file_location('triad_rbpf_study', ROOT/'tools/triad-rbpf-study.py')
study = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(study)
FIX = importlib.util.spec_from_file_location('triad_rbpf_fixture', ROOT/'tests/test_triad_noise_study.py')
fixture = importlib.util.module_from_spec(FIX); FIX.loader.exec_module(fixture)
from triad_gaussian_state import GaussianPrior, GaussianStateFilter


class RBStudyTests(unittest.TestCase):
    def args(self, path, output, *extra):
        return study.parse_args([str(path), '--output', str(output), '--reference-variance', '1', '--particles', '8', *extra])

    def workspace(self, root, name='path', **kwargs):
        path = Path(root)/name; path.mkdir()
        fixture.write_workspace(path, seed=512, observation=True, block_count=4, mixed_taps=True, **kwargs)
        return path

    def test_normalized_acquisition_mixture_is_not_a_maximum(self):
        values = torch.tensor([2., 6., 1., 4.], dtype=torch.float64)
        labels = np.asarray([0, 0, 1, 1])
        actual = study.mix_candidates(values, labels)
        expected = np.asarray([[np.logaddexp(2., 6.)-math.log(2), np.logaddexp(1., 4.)-math.log(2)]])
        np.testing.assert_allclose(actual, expected, atol=1e-14)
        np.testing.assert_allclose(study.mix_candidates(values.repeat(2), np.tile(labels, 2)), expected, atol=1e-14)

    def test_zero_phase_actual_adapter_matches_existing_gaussian_filter(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            args = self.args(path, Path(temp)/'run.json', '--methods', 'rbpf', '--coherence', '13')
            report = study.run(args); self.assertEqual(report['failed_physical_capture_records'], 0)
            ws = study.BlockWorkspace(path)
            _, mapping, labels = study.candidate_mapping(ws, 2)
            state = GaussianStateFilter(GaussianPrior((.5, .5), 13))
            for r, G, _, _, dt in study.actual_blocks(ws, 1.):
                state.update(torch.from_numpy(r[mapping]), torch.from_numpy(G[mapping[:,:,None],mapping[:,None,:]]), dt)
            expected = study.mix_candidates(state.finish(), labels)
            np.testing.assert_allclose(report['captures'][0]['methods']['rbpf']['bit_log_scores'], expected[0], atol=2e-12)
            self.assertEqual(report['captures'][0]['selected_candidate_indices'], [2, 3])
            self.assertEqual(report['captures'][0]['full_bank_dimension'], 6)

    def test_ucb_reference_scaling_and_expanded_budget(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            outputs = []
            for index, reference in enumerate((1., 7.)):
                args = self.args(path, Path(temp)/f'r{index}.json', '--reference-variance', str(reference),
                    '--frequency-bound', '.05', '--phase-diffusion', '.1', '--resampling', 'systematic')
                report = study.run(args); self.assertEqual(report['failed_physical_capture_records'], 0)
                outputs.append(report)
            a, b = [r['captures'][0]['methods']['rbpf-ucb']['bit_log_scores'] for r in outputs]
            np.testing.assert_allclose(a, b, atol=2e-12, rtol=2e-12)
            self.assertAlmostEqual(outputs[0]['family']['fixed_window_log_threshold'], math.log(1440*4*11*2/(.5e-4)), places=12)
            self.assertEqual(outputs[0]['captures'][0]['calibration']['pooled_complex_dof'], 8)
            self.assertIn('joint over null noise', outputs[0]['captures'][0]['methods']['rbpf-ucb']['diagnostics']['calibration_scope'])

    def test_no_truth_leakage_and_actual_only_file_reads(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            original = np.fromfile
            def guarded(file, *args, **kwargs):
                self.assertNotIn(Path(file.name).name, ('mean.c128', 'signal_energy.f64'))
                return original(file, *args, **kwargs)
            with patch.object(study.np, 'fromfile', side_effect=guarded):
                first = study.run(self.args(path, Path(temp)/'before.json', '--phase-diffusion', '.3'))
            self.assertEqual(first['failed_physical_capture_records'], 0)
            manifest = json.loads((path/'manifest.json').read_text())
            manifest['noise_variance'] = 2345.
            manifest['signal_bit_simulation_truth'] = 1-manifest['signal_bit_simulation_truth']
            (path/'manifest.json').write_text(json.dumps(manifest))
            np.full((4, 6), complex(np.nan, np.nan), dtype='<c16').tofile(path/'mean.c128')
            np.full(4, np.nan, dtype='<f8').tofile(path/'signal_energy.f64')
            second = study.run(self.args(path, Path(temp)/'after.json', '--phase-diffusion', '.3'))
            self.assertEqual(second['failed_physical_capture_records'], 0)
            for method in study.METHODS:
                self.assertEqual(first['captures'][0]['methods'][method]['bit_log_scores'], second['captures'][0]['methods'][method]['bit_log_scores'])

    def test_partial_last_block_and_zero_evidence_erasure_keep_physical_time(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            manifest = json.loads((path/'manifest.json').read_text()); manifest['sample_count'] = 30
            (path/'manifest.json').write_text(json.dumps(manifest))
            intervals = np.asarray([[0,8],[8,16],[16,24],[24,30]],dtype='<u8'); intervals.tofile(path/'intervals.u64')
            np.asarray([8,0,8,6],dtype='<u8').tofile(path/'valid_rows.u64')
            for name, shape, dtype in [('gram.c128',(4,6,6),'<c16'),('observation.c128',(4,6),'<c16'),('received_energy.f64',(4,),'<f8')]:
                array = np.fromfile(path/name,dtype=dtype).reshape(shape); array[1] = 0.; array.tofile(path/name)
            ws = study.BlockWorkspace(path)
            blocks = list(study.actual_blocks(ws, 1.))
            np.testing.assert_allclose([b[-1] for b in blocks],[0.,.125,.125,.109375],atol=0.)
            report = study.run(self.args(path, Path(temp)/'partial.json', '--methods', 'rbpf', '--phase-diffusion', '.1'))
            self.assertEqual(report['failed_physical_capture_records'], 0)
            self.assertEqual(report['captures'][0]['methods']['rbpf']['diagnostics']['blocks'], 4)

    def test_cli_validation_and_memory_failure_are_explicit(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp); output = Path(temp)/'invalid.json'
            for invalid in [[], ['--cn0','nan'], ['--reference-variance','0'], ['--reference-variance','1','--tap-powers','1'],
                            ['--reference-variance','1','--particles','0'], ['--reference-variance','1','--horizon-seconds','1e308','--attempt-spacing','1e-300']]:
                with self.subTest(invalid=invalid), self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                    study.parse_args([str(path),'--output',str(output),*invalid])
            args = self.args(path, Path(temp)/'small.json', '--memory-gib', '.00005')
            report = study.run(args)
            self.assertEqual(report['failed_physical_capture_records'], 1)
            self.assertIn(report['captures'][0]['error_type'], ('MemoryError','ValueError'))
            summary = report['summary_by_control']['signal']['rbpf']
            self.assertEqual(summary['aggregate_counts']['missed'], 1)
            self.assertEqual(summary['completed_physical_paths'], 0)

    def test_noise_failure_and_reception_categories_are_distinct(self):
        self.assertEqual(study.classify(np.asarray([[30.,1.]]),0,20.,'signal')['correct_accepted'],1)
        self.assertEqual(study.classify(np.asarray([[30.,1.]]),1,20.,'signal')['wrong_accepted'],1)
        self.assertEqual(study.classify(np.asarray([[3.,1.]]),0,20.,'signal')['missed'],1)
        self.assertEqual(study.classify(np.asarray([[30.,1.]]),0,20.,'null')['false_admitted'],1)
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            manifest = json.loads((path/'manifest.json').read_text()); manifest['control']='null'; manifest['gaussian_projection_supported']=False
            (path/'manifest.json').write_text(json.dumps(manifest))
            report = study.run(self.args(path,Path(temp)/'bad.json'))
            self.assertEqual(report['failed_physical_capture_records'],1)
            self.assertIn('Gaussian null',report['captures'][0]['error'])
            self.assertFalse(report['summary_by_control']['null']['rbpf']['null_qualification_complete'])
            self.assertEqual(report['summary_by_control']['null']['rbpf']['aggregate_counts']['captures'],0)

    def test_wrong_key_changes_evaluation_labels_only_and_ignores_truth(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            first = study.run(self.args(path, Path(temp)/'signal.json', '--phase-diffusion', '.2'))
            self.assertEqual(first['failed_physical_capture_records'], 0)
            manifest = json.loads((path/'manifest.json').read_text())
            manifest['control'] = 'wrong-key'
            manifest['signal_bit_simulation_truth'] = 'intentionally unusable evaluation truth'
            (path/'manifest.json').write_text(json.dumps(manifest))
            np.full((4, 6), complex(np.nan, np.nan), dtype='<c16').tofile(path/'mean.c128')
            np.full(4, np.nan, dtype='<f8').tofile(path/'signal_energy.f64')
            original = np.fromfile
            def guarded(file, *args, **kwargs):
                self.assertNotIn(Path(file.name).name, ('mean.c128', 'signal_energy.f64'))
                return original(file, *args, **kwargs)
            with patch.object(study.np, 'fromfile', side_effect=guarded):
                wrong = study.run(self.args(path, Path(temp)/'wrong.json', '--phase-diffusion', '.2'))
            self.assertEqual(wrong['failed_physical_capture_records'], 0)
            self.assertEqual(set(wrong['summary_by_control']), {'wrong-key'})
            self.assertIsNone(wrong['captures'][0]['truth_bit_simulation_only'])
            for method in study.METHODS:
                before = first['captures'][0]['methods'][method]
                after = wrong['captures'][0]['methods'][method]
                self.assertEqual(before['bit_log_scores'], after['bit_log_scores'])
                self.assertEqual(after['counts']['false_admitted'], int(max(after['bit_log_scores']) >= after['threshold']))
                self.assertNotIn('correct_accepted', after['counts'])
                summary = wrong['summary_by_control']['wrong-key'][method]
                self.assertEqual(summary['aggregate_counts']['captures'], 1)
                self.assertIsNone(summary['null_qualification_complete'])
                self.assertTrue(summary['wrong_key_campaign_complete'])
                self.assertIn('not a Gaussian', summary['false_admission_scope'])
            self.assertEqual(study.classify(np.asarray([[30., 1.]]), None, 20., 'wrong-key')['false_admitted'], 1)
            self.assertEqual(study.classify(np.asarray([[3., 1.]]), None, 20., 'wrong-key')['not_admitted'], 1)

    def test_unknown_control_remains_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            manifest = json.loads((path/'manifest.json').read_text()); manifest['control'] = 'unrecognized'
            (path/'manifest.json').write_text(json.dumps(manifest))
            report = study.run(self.args(path, Path(temp)/'unknown.json'))
            self.assertEqual(report['failed_physical_capture_records'], 1)
            self.assertIn('only actual signal/null/wrong-key', report['captures'][0]['error'])
            self.assertEqual(report['captures'][0]['methods'], {})

    def test_cuda_initialization_precedes_allocator_peak_reset(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            args = self.args(path, Path(temp)/'cuda.json', '--device', 'cuda:0')
            # Stop after startup, before inference or real device allocation.
            args.inputs.append(path)
            calls = []
            with patch.object(study.torch.cuda, 'is_available', return_value=True), \
                 patch.object(study.torch.cuda, 'init', side_effect=lambda: calls.append('init')), \
                 patch.object(study.torch.cuda, 'set_device', side_effect=lambda device: calls.append(('select', device))), \
                 patch.object(study.torch.cuda, 'reset_peak_memory_stats', side_effect=lambda device: calls.append(('reset', device))):
                with self.assertRaisesRegex(study.WorkspaceError, 'duplicate physical path'):
                    study.run(args)
            self.assertEqual(calls, ['init', ('select', 'cuda:0'), ('reset', 'cuda:0')])
            self.assertFalse(study.torch.backends.cuda.matmul.allow_tf32)

    def test_fp32_bulk_parity_diagnostics_and_bounded_retention(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.workspace(temp)
            results = [study.run(self.args(path,Path(temp)/(precision+'.json'),'--precision',precision,'--frequency-bound','.1','--phase-diffusion','.1')) for precision in ('fp32','fp64')]
            for report in results: self.assertEqual(report['failed_physical_capture_records'],0)
            for method in study.METHODS:
                rows = [r['captures'][0]['methods'][method] for r in results]
                np.testing.assert_allclose(rows[0]['bit_log_scores'],rows[1]['bit_log_scores'],atol=2e-6,rtol=2e-6)
                self.assertLess(rows[0]['diagnostics']['owned_state_bytes'],10000)
                self.assertFalse(rows[0]['diagnostics']['tf32_allowed'])
            self.assertEqual(results[0]['arithmetic']['bulk_particle_means_and_products'],'fp32')
            self.assertEqual(results[0]['arithmetic']['rank_gram_covariance_factorization'],'FP64')


if __name__ == '__main__':
    torch.set_num_threads(2)
    unittest.main()
