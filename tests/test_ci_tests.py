#!/usr/bin/env python3
"""Coverage partition and failure-preserving timing checks for the CI runner."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ci_tests', ROOT / 'tools/run-ci-tests.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def test(name, labels=(), timeout=None):
    values = [{'name': 'LABELS', 'value': list(labels)}]
    if timeout is not None:
        values.append({'name': 'TIMEOUT', 'value': timeout})
    return {'name': name, 'properties': values}


class Selection(unittest.TestCase):
    def test_contract_overlap_and_new_tests_have_one_owner(self):
        inventory = [test('unlabelled'), test('gui', ('gui', 'contract')),
                     test('fast', ('fast', 'contract')), test('legacy', ('legacy',)),
                     test('live', ('regular', 'contract')), test('live_profiles', ('regular', 'contract')),
                     test('calibration', ('regular', 'contract', 'calibration'))]
        owners = []
        for scope in runner.SCOPES:
            selected, omitted, counts = runner.select_tests(inventory, scope)
            self.assertFalse(omitted)
            self.assertEqual(sum(counts.values()), len(inventory))
            owners += [item['name'] for item in selected]
        self.assertCountEqual(owners, [item['name'] for item in inventory])
        self.assertEqual(len(owners), len(set(owners)))

    def test_live_scope_is_exact_and_mandatory_in_every_configuration(self):
        inventory = [test(name, ('regular', 'contract'))
                     for name in ('live', 'live_profiles', 'live_resources', 'live_receptions', 'live_transmit_lock')]
        for kwargs in ({}, {'sanitizers': True}, {'sanitizers': True, 'sanitizer_realtime': True}):
            selected, omitted, _ = runner.select_tests(inventory, 'live', **kwargs)
            self.assertCountEqual([item['name'] for item in selected], runner.LIVE)
            self.assertFalse(omitted)
            core, _, _ = runner.select_tests(inventory, 'core', **kwargs)
            self.assertCountEqual([item['name'] for item in core],
                                  ['live_resources', 'live_receptions', 'live_transmit_lock'])

    def test_only_documented_debug_realtime_cases_are_omitted(self):
        inventory = [test(name, ('fast',)) for name in ('fast_session', 'gui_fast_live', 'fast_codec')]
        selected, omitted, _ = runner.select_tests(inventory, 'fast', sanitizers=True)
        self.assertEqual([item['name'] for item in selected], ['fast_codec'])
        self.assertCountEqual(omitted, runner.REALTIME)
        for kwargs in ({}, {'sanitizers': True, 'sanitizer_realtime': True}):
            selected, omitted, _ = runner.select_tests(inventory, 'fast', **kwargs)
            self.assertEqual(len(selected), 3)
            self.assertFalse(omitted)

    def test_empty_and_duplicate_inventories_fail(self):
        for inventory in ([], [test('same'), test('same')]):
            with self.assertRaises(ValueError):
                runner.select_tests(inventory, 'core')

    def test_gui_selection_includes_native_gui(self):
        selected, _, _ = runner.select_tests([test('gui', ('gui',)), test('native', ('gui', 'native_gui')),
                                              test('core')], 'gui')
        self.assertEqual([item['name'] for item in selected], ['gui', 'native'])


class Reports(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'results.xml'

    def test_near_limit_pass_warns_without_changing_result(self):
        self.path.write_text('<testsuite><testcase name="slow" status="run" time="8"/></testsuite>')
        result = runner.read_results(self.path, [test('slow', timeout=10)], 3600, .8)[0]
        self.assertEqual(result['status'], 'passed')
        self.assertTrue(result['near_timeout'])

    def test_failure_skip_and_timeout_are_never_reclassified_as_slow_passes(self):
        for child in ('<failure message="assertion"/>', '<failure message="Timeout"/>', '<skipped/>', '<error/>'):
            self.path.write_text(f'<testsuite><testcase name="bad" time="10">{child}</testcase></testsuite>')
            result = runner.read_results(self.path, [test('bad', timeout=10)], 3600, .8)[0]
            self.assertEqual(result['status'], 'incomplete_or_failed')
            self.assertFalse(result['near_timeout'])

    def test_missing_extra_or_duplicate_results_fail_closed(self):
        for contents in ('', '<testcase name="wrong"/>', '<testcase name="wanted"/><testcase name="wanted"/>'):
            self.path.write_text(f'<testsuite>{contents}</testsuite>')
            with self.assertRaises(ValueError):
                runner.read_results(self.path, [test('wanted')], 3600, .8)


@unittest.skipUnless(shutil.which('ctest'), 'CTest is needed for temporary integration fixtures')
class CTestIntegration(unittest.TestCase):
    def exercise(self, program, timeout=5):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = Path(sys.executable).as_posix()
            (root / 'CTestTestfile.cmake').write_text(
                f'add_test(sample "{executable}" "-c" "{program}")\n'
                f'set_tests_properties(sample PROPERTIES LABELS "regular;contract" TIMEOUT "{timeout}")\n')
            report = root / 'report.json'
            args = argparse.Namespace(build_dir=root, scope='core', config=None, ctest=shutil.which('ctest'),
                                      jobs=1, sanitizers=False, sanitizer_realtime=False, report=report,
                                      summary=root / 'summary.md', timeout=3600, warn_fraction=.005)
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = runner.run(args)
            return result, json.loads(report.read_text()), stdout.getvalue()

    def test_real_ctest_success_and_slow_warning(self):
        result, report, output = self.exercise('import time; time.sleep(.05)')
        self.assertEqual(result, 0)
        self.assertEqual(report['status'], 'passed')
        self.assertIn('::warning', output)
        self.assertEqual(report['selected'], ['sample'])

    def test_real_ctest_assertion_remains_fatal(self):
        result, report, _ = self.exercise('raise AssertionError(123)')
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')

    def test_real_ctest_timeout_remains_fatal(self):
        result, report, _ = self.exercise('import time; time.sleep(10)', timeout=.1)
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertFalse(report['tests'][0]['near_timeout'])

    def test_workload_warning_from_passing_ctest_output_remains_visible(self):
        marker = 'TEST_WORKLOAD_BUDGET: fixture: exceeded 30s normal budget; instrumented limit 90s (full completion remains required)'
        result, report, output = self.exercise(f'print({marker!r})')
        self.assertEqual(result, 0)
        self.assertEqual(report['tests'][0]['workload_warnings'], [marker])
        self.assertIn('::warning title=CI test workload::sample passed all assertions.', output)

    def test_workload_marker_cannot_turn_failure_into_warning(self):
        marker = 'TEST_WORKLOAD_BUDGET: fixture: exceeded 30s normal budget'
        result, report, output = self.exercise(f'print({marker!r}); raise AssertionError(123)')
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['tests'][0]['workload_warnings'], [marker])
        self.assertNotIn('::warning title=CI test workload::', output)


if __name__ == '__main__':
    unittest.main()
