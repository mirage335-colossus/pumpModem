"""Executed-family contracts, not performance thresholds or sensitivity tests."""
import importlib
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import triad_perf_paths as paths


class PathCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = paths.collect_paths(ROOT, threads=2, deadline=time.monotonic()+60)

    def test_every_applicable_family_has_executed_source_witnesses(self):
        self.assertTrue(self.report['complete_applicable_coverage'], self.report['failed_ids'])
        contract = json.loads(paths.CONTRACT.read_text())
        self.assertEqual({r['id'] for r in self.report['records']}, {s['id'] for s in contract['families']})
        for row in self.report['records']:
            if row['applicable']:
                self.assertEqual(row['status'], 'complete', row)
                for required in row['required_witnesses']:
                    self.assertGreater(row['execution_witnesses'].get(required, 0), 0)
                for filename, digest in row['source_sha256'].items():
                    self.assertEqual(digest, paths._sha(ROOT/'tools'/filename))
                self.assertGreater(row['unprofiled_diagnostic_seconds'], 0.)

    def test_cuda_only_paths_are_explicit_not_substituted(self):
        self.assertEqual(set(self.report['unsupported_ids']), {'phase-graph','phase-reduction','phase-tensor'})
        for row in self.report['records']:
            if not row['applicable']:
                self.assertEqual(row['status'], 'unsupported')
                self.assertNotIn('unprofiled_diagnostic_seconds', row)
        self.assertFalse(self.report['full_runtime_prediction_qualified'])

    def test_real_effect_witnesses_and_oracles_are_retained(self):
        rows = {row['id']: row for row in self.report['records']}
        self.assertGreater(rows['rbpf']['result']['stochastic']['resampling_events_total'], 0)
        self.assertLess(rows['rbpf']['result']['midpoint_mixture_error'], 2e-11)
        self.assertEqual(rows['cfo-batch']['result']['geometry']['paths'], 2)
        self.assertTrue(rows['static-frequency']['result']['geometry']['partial_last_block'])
        self.assertEqual(rows['ray-frequency']['result']['geometry']['evaluated_frequency_pairs_per_candidate'], 15)
        self.assertTrue(rows['gaussian-state']['result']['empty_block_evidence_unchanged'])

    def test_missing_execution_witness_is_a_failure(self):
        contract = json.loads(paths.CONTRACT.read_text())
        contract['families'] = [contract['families'][0]]
        contract['families'][0]['required_witnesses'].append('triad_gaussian_state.py:does_not_exist')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'contract.json'; path.write_text(json.dumps(contract))
            with patch.object(paths, 'CONTRACT', path):
                report = paths.collect_paths(ROOT, threads=2, deadline=time.monotonic()+10)
        self.assertFalse(report['complete_applicable_coverage'])
        self.assertEqual(report['failed_ids'], ['gaussian-state'])
        self.assertIn('required execution witnesses missing', report['records'][0]['error'])

    def test_external_profiler_and_expired_deadline_are_not_overridden(self):
        def external(frame, event, arg):
            return
        sys.setprofile(external)
        try:
            with self.assertRaisesRegex(paths.PathCoverageError, 'existing profiler'):
                paths.collect_paths(ROOT)
            self.assertIs(sys.getprofile(), external)
        finally:
            sys.setprofile(None)
        with self.assertRaises(TimeoutError):
            paths.collect_paths(ROOT, deadline=time.monotonic()-1)
        self.assertIsNone(sys.getprofile())

    def test_report_is_strict_json_and_module_root_is_bound(self):
        json.dumps(self.report, allow_nan=False)
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp)/'tools').mkdir()
            with self.assertRaisesRegex(paths.PathCoverageError, 'another source root'):
                paths._Modules(tmp).get('triad_gaussian_state.py')


if __name__ == '__main__':
    unittest.main()
