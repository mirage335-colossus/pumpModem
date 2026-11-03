#!/usr/bin/env python3
"""Focused checks for the public-bank followup contract, without simulations."""
import importlib.util
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE/filename)
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result)
    return result
prepare = module('acquisition_plan', 'prepare.py')
runner = module('acquisition_runner', 'run.py')

class PlanTests(unittest.TestCase):
    def test_grid_contains_zero_and_endpoints(self):
        for half, maximum in ((.090500000001, .0016844350714584277),
                              (7.035, .016844350714584275),
                              (.090500000001, .00016844350714584276)):
            grid, count = prepare.symmetric_grid(half, maximum)
            self.assertEqual(count % 2, 1)
            self.assertLessEqual(grid[2], maximum)
            self.assertAlmostEqual(grid[0]+((count-1)//2)*grid[2], 0, places=14)
            self.assertAlmostEqual(grid[0]+(count-1)*grid[2], grid[1], places=14)

    def test_grid_rejects_resource_overflow(self):
        with self.assertRaises(ValueError): prepare.symmetric_grid(7, 1e-6)

    def test_replace_preserves_negative_and_following_options(self):
        argv = ['bin', '--start', '-1', '1', '.1', '--fhss', '--seed', '42']
        prepare.replace(argv, '--start', -2, 2, .01)
        self.assertEqual(argv, ['bin', '--start', '-2', '2', '0.01', '--fhss', '--seed', '42'])

    def test_definition_mutation_detected(self):
        value = prepare.job('example', 'configure', ['bin'], 10, depends_on=[])
        runner.common.check_job(value)
        value['argv'].append('--unknown')
        with self.assertRaises(ValueError): runner.common.check_job(value)

    def test_recount_wrong_miss_and_null_separate(self):
        self.assertEqual(runner.outcome([10, 30], 20, 0, 'signal'),
                         dict(correct_admitted=0, wrong_admitted=1, missed=0))
        self.assertEqual(runner.outcome([10, 15], 20, 1, 'signal'),
                         dict(correct_admitted=0, wrong_admitted=0, missed=1))
        self.assertEqual(runner.outcome([30, 10], 20, 0, 'null'),
                         dict(false_admitted=1, not_admitted=0))

    def test_expired_and_unreaped_child_not_success(self):
        valid = dict(outcome='completed', exit_code=0, child_exit_code=0,
                     timed_out=False, cancelled=False, termination=dict(group_empty=True))
        self.assertTrue(runner.common.bounded_passed(valid))
        valid['termination']['group_empty'] = False
        self.assertFalse(runner.common.bounded_passed(valid))

    def test_group_export_uses_group_filenames_and_retains_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory = root/'group'
            value = dict(stage='export', workspace='{OUT}/group')
            def child(proxy, args, bindings, prior):
                self.assertEqual(proxy['stage'], 'group-export')
                directory.mkdir()
                (directory/'group_observation.c128').write_bytes(b'0')
                (directory/'received_energy.f64').write_bytes(b'0')
                return dict(status='completed', stage=proxy['stage'], output_files=[])
            with patch.object(runner.common, 'execute', side_effect=child), patch.object(runner, 'export_audit', return_value={'checked':True}):
                result = runner.execute(value, SimpleNamespace(output_root=root), {'OUT':str(root)}, {}, None)
            self.assertEqual(result['status'], 'completed')
            self.assertEqual(result['stage'], 'export')
            self.assertEqual(len(result['output_files']), 2)

if __name__ == '__main__': unittest.main()
