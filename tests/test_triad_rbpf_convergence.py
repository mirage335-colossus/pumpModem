#!/usr/bin/env python3
"""Finite-replica convergence diagnostic contract checks."""
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
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('triad_rbpf_convergence',ROOT/'tools/triad-rbpf-convergence.py')
study=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(study)
FIX=importlib.util.spec_from_file_location('triad_convergence_fixture',ROOT/'tests/test_triad_noise_study.py')
fixture=importlib.util.module_from_spec(FIX);FIX.loader.exec_module(fixture)


class ConvergenceTests(unittest.TestCase):
    def workspace(self,temp):
        path=Path(temp)/'path';path.mkdir()
        fixture.write_workspace(path,seed=191,observation=True,block_count=3,mixed_taps=True)
        return path

    def args(self,path,output,*extra):
        return study.parse_args([str(path),'--output',str(output),'--reference-variance','1','--particle-counts','8','--algorithm-replicates','1',*extra])

    def test_single_replica_matches_actual_adapter_with_same_predeclared_seed(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            extra=['--frequency-bound','.2','--phase-diffusion','.3','--seed','19']
            expected=study.adapter.run(study.adapter.parse_args([str(path),'--output',str(Path(temp)/'adapter.json'),'--reference-variance','1','--particles','8',*extra]))
            actual=study.run(self.args(path,Path(temp)/'convergence.json',*extra))
            self.assertEqual(expected['failed_physical_capture_records'],0)
            for method in study.adapter.METHODS:
                row=actual['methods'][method][0]
                np.testing.assert_allclose(row['bit_log_scores_per_algorithm_replica'][0],expected['captures'][0]['methods'][method]['bit_log_scores'],atol=2e-12)
                np.testing.assert_allclose(row['log_mean_likelihood_by_bit'],row['bit_log_scores_per_algorithm_replica'][0],atol=0)
                self.assertIsNone(row['likelihood_relative_standard_error_by_bit'])
                self.assertEqual(np.shape(row['minimum_effective_particles_by_replica_candidate']),(1,2))

    def test_identical_zero_phase_replicas_are_not_independent_physical_trials(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            report=study.run(self.args(path,Path(temp)/'replicas.json','--algorithm-replicates','4','--particle-counts','4','16'))
            for method in study.adapter.METHODS:
                rows=report['methods'][method]
                for row in rows:
                    scores=np.asarray(row['bit_log_scores_per_algorithm_replica'])
                    np.testing.assert_allclose(scores,np.repeat(scores[:1],4,axis=0),atol=0)
                    np.testing.assert_allclose(row['log_mean_likelihood_by_bit'],scores[0],atol=1e-14)
                    np.testing.assert_allclose(row['likelihood_relative_standard_error_by_bit'],[0,0],atol=0)
                    self.assertNotIn('accepted',row)
                np.testing.assert_allclose(rows[0]['log_mean_likelihood_by_bit'],rows[1]['log_mean_likelihood_by_bit'],atol=1e-14)
            self.assertTrue(any('not independent physical validation' in v for v in report['limitations']))
            self.assertNotIn('summary_by_control',report)

    def test_likelihood_mean_and_variation_are_not_log_mean_or_maximum(self):
        scores=torch.tensor([[0.,2.],[4.,6.]],dtype=torch.float64)
        actual=study.summarize_scores(scores,np.asarray([0,1]))
        expected=np.logaddexp([0.,2.],[4.,6.])-math.log(2)
        np.testing.assert_allclose(actual['log_mean_likelihood_by_bit'],expected,atol=1e-14)
        self.assertFalse(np.allclose(expected,[2.,4.]))
        self.assertFalse(np.allclose(expected,[4.,6.]))
        self.assertGreater(actual['likelihood_relative_standard_error_by_bit'][0],.9)

    def test_memory_preflight_precedes_duration_sized_preparation(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            args=self.args(path,Path(temp)/'small.json','--memory-gib','.0002','--particle-counts','8192','--algorithm-replicates','8')
            with patch.object(study,'prepare',side_effect=AssertionError('allocated before preflight')):
                with self.assertRaisesRegex(MemoryError,'exceed limit'):study.run(args)
            self.assertFalse(args.output.exists())
            a=study.memory_preflight(3,2,2,6,2,16,8,'fp32')
            b=study.memory_preflight(6,2,2,6,2,16,8,'fp32')
            self.assertEqual(a['core_array_bound_bytes'],b['core_array_bound_bytes'])
            self.assertEqual(2*a['retained_cpu_arrays_bytes'],b['retained_cpu_arrays_bytes'])

    def test_preflight_covers_current_core_for_both_precisions_and_replica_counts(self):
        for precision, dtype in [('fp32', torch.complex64), ('fp64', torch.complex128)]:
            for replicas in (1, 8):
                for taps, candidates in ((1, 2), (2, 6)):
                    with self.subTest(precision=precision, replicas=replicas, taps=taps):
                        particles = 16
                        bound = study.memory_preflight(3, candidates, taps, 2*candidates, 2, particles, replicas, precision)
                        prior = study.adapter.GaussianPrior((1/taps,)*taps, math.inf)
                        state = study.RBPhaseFilter(prior, particles=particles, dtype=dtype)
                        r = torch.zeros((replicas, candidates, taps), dtype=dtype)
                        G = torch.zeros((1, candidates, taps, taps), dtype=torch.complex128)
                        state.update(r, G, 0.)
                        self.assertGreaterEqual(bound['core_array_bound_bytes'], state.estimated_array_peak_bytes)
                        self.assertGreaterEqual(bound['total_array_bound_bytes'], state.estimated_array_peak_bytes +
                            bound['cache_and_conversion_bound_bytes'] + bound['full_bank_preparation_bound_bytes'])

    def test_calibration_once_and_truth_does_not_enter_decisions(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            args=self.args(path,Path(temp)/'before.json','--particle-counts','4','8','--algorithm-replicates','2','--phase-diffusion','.2')
            with patch.object(study.adapter,'calibrate',wraps=study.adapter.calibrate) as calibrator:
                first=study.run(args)
                self.assertEqual(calibrator.call_count,1)
            manifest=json.loads((path/'manifest.json').read_text());manifest['noise_variance']=222.;manifest['signal_bit_simulation_truth']=0
            (path/'manifest.json').write_text(json.dumps(manifest))
            np.full((3,6),complex(np.nan,np.nan),dtype='<c16').tofile(path/'mean.c128')
            second=study.run(self.args(path,Path(temp)/'after.json','--particle-counts','4','8','--algorithm-replicates','2','--phase-diffusion','.2'))
            for method in study.adapter.METHODS:
                for a,b in zip(first['methods'][method],second['methods'][method]):
                    self.assertEqual(a['bit_log_scores_per_algorithm_replica'],b['bit_log_scores_per_algorithm_replica'])

    def test_paired_initializations_keep_iid_compatibility_and_frozen_replica_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            extra=['--frequency-bound','.4','--phase-diffusion','.2','--algorithm-replicates','3','--seed','41']
            iid=study.run(self.args(path,Path(temp)/'iid.json',*extra))
            paired=study.run(self.args(path,Path(temp)/'paired.json',*extra,'--frequency-initializations','stratified','iid'))
            self.assertEqual(paired['methods_compatibility_initialization'],'iid')
            for method in study.adapter.METHODS:
                a=iid['methods'][method][0]
                b=paired['methods'][method][0]
                c=paired['frequency_initialization_results']['stratified'][method][0]
                self.assertEqual(a['bit_log_scores_per_algorithm_replica'],b['bit_log_scores_per_algorithm_replica'])
                self.assertEqual(b['master_seed'],c['master_seed'])
                comparison=paired['paired_initialization_comparisons']['stratified'][method][0]
                left=np.asarray(b['bit_log_scores_per_algorithm_replica'])
                right=np.asarray(c['bit_log_scores_per_algorithm_replica'])
                np.testing.assert_allclose(comparison['paired_log_score_differences_by_replica_bit'],right-left,atol=0)
                scale=np.maximum(left.max(axis=0),right.max(axis=0))
                np.testing.assert_allclose(comparison['mean_scaled_likelihood_difference_by_bit'],(np.exp(right-scale)-np.exp(left-scale)).mean(axis=0),atol=1e-15)
                self.assertEqual(c['diagnostics']['frequency_initialization'],'stratified')
                for j,decision in enumerate(c['diagnostic_decisions_per_algorithm_replica']):
                    self.assertEqual(decision['replica_index'],j)
                    self.assertEqual(decision['winning_bit'],int(right[j].argmax()))
                    self.assertEqual(decision['exceeds_reference_gate'],bool(right[j].max()>=c['diagnostic_reference_log_gate']))
            self.assertNotIn('selected_best_initialization',paired)

    def test_zero_frequency_pair_and_likelihood_scale_overflow_representation(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            result=study.run(self.args(path,Path(temp)/'zero.json','--algorithm-replicates','2','--frequency-initializations','iid','stratified'))
            for method in study.adapter.METHODS:
                comparison=result['paired_initialization_comparisons']['stratified'][method][0]
                np.testing.assert_allclose(comparison['paired_log_score_differences_by_replica_bit'],0,atol=0)
                self.assertEqual(comparison['reference_gate_decision_disagreements'],0)
            scores=torch.tensor([[1000.,1.],[999.,2.]],dtype=torch.float64)
            summary=study.summarize_scores(scores,np.asarray([0,1]))
            self.assertIsNone(summary['mean_likelihood_by_bit'][0])
            self.assertAlmostEqual(summary['mean_likelihood_by_bit'][1],(math.exp(1)+math.exp(2))/2)
            np.testing.assert_allclose(np.log(summary['likelihood_mean_scaled_by_bit'])+summary['likelihood_scale_log_by_bit'],summary['log_mean_likelihood_by_bit'],atol=1e-13)
            np.testing.assert_allclose(summary['log_score_population_variance_by_bit'],[.25,.25],atol=0)

    def test_rejected_resampling_replica_counts_and_duplicate_particle_counts(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            for extra in [('--resampling','systematic'),('--algorithm-replicates','0'),('--particle-counts','8','8'),('--frequency-initializations','iid','iid')]:
                with self.subTest(extra=extra),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                    self.args(path,Path(temp)/'bad.json',*extra)


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()
