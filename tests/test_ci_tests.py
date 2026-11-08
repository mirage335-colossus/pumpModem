#!/usr/bin/env python3
"""Coverage partition and failure-preserving timing checks for the CI runner."""
import argparse
from collections import Counter
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


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
    def test_scopes_are_disjoint_and_cover_the_inventory(self):
        inventory = [test('shared', ('regular', 'contract'))] + [
            test(label, (label, 'contract')) for label in runner.SCOPES if label not in ('core','native')]
        inventory.append(test('adapter', ('gui', 'native_gui')))
        actual=[]
        for scope in runner.SCOPES:
            selected, omitted, counts=runner.select_tests(inventory, scope)
            self.assertFalse(omitted)
            self.assertEqual(sum(counts.values()),len(inventory))
            actual += [item['name'] for item in selected]
        self.assertCountEqual(actual,[item['name'] for item in inventory])
        self.assertEqual(len(actual),len(set(actual)))
        contract, _, _ = runner.select_tests(inventory, 'contract')
        self.assertNotIn('adapter',[item['name'] for item in contract])

    def test_retained_scopes_have_no_retired_receivers(self):
        self.assertFalse({'live','preparation','calibration'} & set(runner.SCOPES))

    def test_only_instrumented_realtime_cases_are_omitted(self):
        inventory=[test(name, ('fast',)) for name in ('fast_session','gui_fast_live','fast_codec')]
        selected, omitted, _ = runner.select_tests(inventory,'fast',sanitizers=True)
        self.assertEqual([item['name'] for item in selected],['fast_codec'])
        self.assertCountEqual(omitted,runner.REALTIME)
        selected, omitted, _=runner.select_tests(inventory,'fast',sanitizers=True,sanitizer_realtime=True)
        self.assertEqual(len(selected),3)
        self.assertFalse(omitted)

    def test_duplicate_and_empty_inventories_fail(self):
        for inventory in ([],[test('same'),test('same')]):
            with self.assertRaises(ValueError):runner.select_tests(inventory,'core')

class WorkloadCollector(unittest.TestCase):
    def test_parallel_interleaving_uses_ctest_numbers_not_selected_positions(self):
        selected = [test('first'), test('second')]
        collector = runner.WorkloadWarningCollector(selected)
        collector.feed('Start 3: second\n')
        collector.feed('Start 8: first\n')
        collector.feed('8: TEST_WORKLOAD_BUDGET: first late warning\n')
        collector.feed('3: TEST_WORKLOAD_BUDGET: second late warning\n')
        collector.feed('8: TEST_WORKLOAD_BUDGET: first late warning\n')
        self.assertEqual(collector.finish(), {
            'first': ['TEST_WORKLOAD_BUDGET: first late warning'],
            'second': ['TEST_WORKLOAD_BUDGET: second late warning']})

    def test_horizontal_padding_preserves_one_two_and_three_digit_identities(self):
        collector = runner.WorkloadWarningCollector(
            [test('one'), test('twelve'), test('three hundred eight')])
        collector.feed('      Start   1: one\n')
        collector.feed('  1: TEST_WORKLOAD_BUDGET: one completed with progress\n')
        collector.feed(' 1/3 Test   #1: one ............ Passed 0.01 sec\n')
        collector.feed('      Start  12: twelve\n')
        collector.feed(' 12: TEST_WORKLOAD_BUDGET: twelve completed with progress\n')
        collector.feed(' 2/3 Test  #12: twelve .......... Passed 0.02 sec\n')
        collector.feed('      Start 308: three hundred eight\n')
        collector.feed('308: TEST_WORKLOAD_BUDGET: three hundred eight completed with progress\n')
        collector.feed(' 3/3 Test #308: three hundred eight ... Passed 0.03 sec\n')
        self.assertEqual(collector.finish(), {
            'one': ['TEST_WORKLOAD_BUDGET: one completed with progress'],
            'twelve': ['TEST_WORKLOAD_BUDGET: twelve completed with progress'],
            'three hundred eight': ['TEST_WORKLOAD_BUDGET: three hundred eight completed with progress']})

    def test_padded_malformed_duplicate_and_conflicting_identities_fail_closed(self):
        cases = (
            ['      Start   1: sample\n', '      Start  12: sample\n'],
            ['      Start  12: sample\n', ' 1/1 Test   #9: sample ........ Passed 0.01 sec\n'],
            ['      Start  12: sample\n', ' 1/1 Test   #12: other ........ Passed 0.01 sec\n'],
            ['      Start  12: sample\n', ' 1/1 Test   #12:\n'],
            ['      Start  12: sample\n', ' 12: TEST_WORKLOAD_BUDGET:   \n'],
        )
        for lines in cases:
            with self.subTest(lines=lines):
                collector = runner.WorkloadWarningCollector([test('sample')])
                for line in lines:
                    collector.feed(line)
                with self.assertRaises(ValueError):
                    collector.finish()

    def test_nested_fake_prefix_is_not_misattributed(self):
        collector = runner.WorkloadWarningCollector([test('sample')])
        collector.feed('Start 1: sample\n')
        collector.feed('1: 9: TEST_WORKLOAD_BUDGET: nested child text\n')
        self.assertEqual(collector.finish(), {'sample': []})

    def test_command_metadata_and_output_replay_are_ignored(self):
        marker = 'TEST_WORKLOAD_BUDGET: live record'
        collector = runner.WorkloadWarningCollector([test('sample')])
        collector.feed('Start 1: sample\n')
        collector.feed('1: Test command: /tmp/probe "' + marker + '"\n')
        collector.feed('1: ' + marker + '\n')
        collector.feed('1/1 Test #1: sample ............ Failed  0.01 sec\n')
        collector.feed('1: ' + marker + '\n')
        collector.feed('The following tests FAILED:\n')
        collector.feed(marker + '\n')
        self.assertEqual(collector.finish(), {'sample': [marker]})

    def test_unknown_repeated_conflicting_and_incomplete_framing_fail(self):
        cases = (
            ['Start 1: sample\n', '9: TEST_WORKLOAD_BUDGET: unknown id\n'],
            ['Start 1: sample\n', 'Start 1: sample\n'],
            ['Start 1: sample\n', 'Start 2: sample\n'],
            ['Start 1: sample\n', '1: TEST_WORKLOAD_BUDGET:'],
            ['1: TEST_WORKLOAD_BUDGET: before start\n', 'Start 1: sample\n'],
            ['Start 1: sample'],
            ['Start 1: other\n'],
        )
        for lines in cases:
            with self.subTest(lines=lines):
                collector = runner.WorkloadWarningCollector([test('sample')])
                for line in lines:
                    collector.feed(line)
                with self.assertRaises(ValueError):
                    collector.finish()

    def test_completion_requires_complete_matching_started_identity(self):
        for completion in (
                '1/1 Test #9: bogus ........ Passed 0.01 sec\n',
                '1/1 Test #1: sample ........ Passed 0.01 sec',
                '1/1 Test #1:\n',
                '1/1 Test #1\n',
                '1/1 Test #1: other ........ Passed 0.01 sec\n',
                '1/1 Test #1: sample extra ........ Passed 0.01 sec\n'):
            with self.subTest(completion=completion):
                collector = runner.WorkloadWarningCollector([test('sample')])
                collector.feed('Start 1: sample\n')
                collector.feed(completion)
                with self.assertRaises(ValueError):
                    collector.finish()
        unknown = runner.WorkloadWarningCollector([test('sample')])
        unknown.feed('Start 1: sample\n')
        unknown.feed('1/1 Test #9: bogus ........ Passed 0.01 sec\n')
        unknown.feed('9: TEST_WORKLOAD_BUDGET: unknown\n')
        with self.assertRaises(ValueError):
            unknown.finish()
        collector = runner.WorkloadWarningCollector([test('sample')])
        collector.feed('Start 1: sample\n')
        for _ in range(2):
            collector.feed('1/1 Test #1: sample ........ Passed 0.01 sec\n')
        with self.assertRaisesRegex(ValueError, 'repeated CTest completion'):
            collector.finish()

    def test_completion_distinguishes_prefix_names_without_interpreting_status(self):
        collector = runner.WorkloadWarningCollector([test('foo'), test('foo bar')])
        collector.feed('Start 1: foo\n')
        collector.feed('Start 3: foo bar\n')
        collector.feed('1: TEST_WORKLOAD_BUDGET: first\n')
        collector.feed('3: TEST_WORKLOAD_BUDGET: second\n')
        collector.feed('1/2 Test #1: foo ........ Custom CTest status\n')
        collector.feed('2/2 Test #3: foo bar ....***Custom status\n')
        self.assertEqual(collector.finish(), {
            'foo': ['TEST_WORKLOAD_BUDGET: first'],
            'foo bar': ['TEST_WORKLOAD_BUDGET: second']})
        wrong = runner.WorkloadWarningCollector([test('foo'), test('foo bar')])
        wrong.feed('Start 1: foo\n')
        wrong.feed('Start 3: foo bar\n')
        wrong.feed('1/2 Test #1: foo bar .... Passed 0.01 sec\n')
        with self.assertRaisesRegex(ValueError, 'conflicts with CTest Start'):
            wrong.finish()


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

    def test_supplied_complete_warnings_replace_truncated_junit_markers_and_deduplicate(self):
        marker = 'TEST_WORKLOAD_BUDGET: late complete warning'
        partial = 'TEST_WORKLOAD_BUDGET: truncated'
        self.path.write_text('<testsuite><testcase name="wanted" status="run">'
                             '<system-out>' + partial + '</system-out></testcase></testsuite>')
        result = runner.read_results(self.path, [test('wanted')], 3600, .8,
                                     workload_warnings={'wanted': [marker, marker]})[0]
        self.assertEqual(result['workload_warnings'], [marker])

    def test_failure_skip_and_timeout_stay_failed_with_collected_warnings(self):
        marker = 'TEST_WORKLOAD_BUDGET: warning does not change status'
        for child in ('<failure message="assertion"/>', '<skipped/>', '<error message="timeout"/>'):
            with self.subTest(child=child):
                self.path.write_text(f'<testsuite><testcase name="bad" status="run" time="10">{child}</testcase></testsuite>')
                result = runner.read_results(self.path, [test('bad', timeout=10)], 3600, .8,
                                             workload_warnings={'bad': [marker]})[0]
                self.assertEqual(result['status'], 'incomplete_or_failed')
                self.assertEqual(result['workload_warnings'], [marker])
                self.assertFalse(result['near_timeout'])

    def test_declared_skip_requires_exact_ctest_metadata_and_shape(self):
        marker = 'TEST_WORKLOAD_BUDGET: retained for omitted result'
        selected = test('declared', timeout=10)
        selected['properties'].append({'name': 'SKIP_RETURN_CODE', 'value': 77})
        xml = ('<testsuite><testcase name="declared" status="notrun" time="10">'
               '<skipped message="SKIP_RETURN_CODE=77"/></testcase></testsuite>')
        self.path.write_text(xml)
        result = runner.read_results(self.path, [selected], 3600, .8,
                                     workload_warnings={'declared': [marker]})[0]
        self.assertEqual(result['status'], 'omitted')
        self.assertEqual(result['omission_reason'], 'declared_skip_return_code')
        self.assertEqual(result['skip_return_code'], 77)
        self.assertFalse(result['near_timeout'])
        self.assertEqual(result['workload_warnings'], [marker])
        selected['properties'].append({'name': 'DISABLED', 'value': False})
        self.assertEqual(runner.read_results(self.path, [selected], 3600, .8)[0]['status'],
                         'omitted')
        selected['properties'] = [item for item in selected['properties']
                                  if item['name'] != 'DISABLED']

        invalid_metadata = (
            ('undeclared skip', None, False),
            ('string return code', {'name': 'SKIP_RETURN_CODE', 'value': '77'}, False),
            ('boolean return code', {'name': 'SKIP_RETURN_CODE', 'value': True}, False),
            ('float return code', {'name': 'SKIP_RETURN_CODE', 'value': 77.0}, False),
            ('other return code', {'name': 'SKIP_RETURN_CODE', 'value': 78}, False),
            ('disabled true', {'name': 'SKIP_RETURN_CODE', 'value': 77}, True),
        )
        for label, metadata, is_disabled in invalid_metadata:
            with self.subTest(metadata=label):
                props = [item for item in selected['properties']
                         if item['name'] not in ('SKIP_RETURN_CODE', 'DISABLED')]
                if metadata is not None:
                    props.append(metadata)
                if is_disabled:
                    props.append({'name': 'DISABLED', 'value': True})
                invalid = {'name': 'declared', 'properties': props}
                self.path.write_text(xml)
                result = runner.read_results(self.path, [invalid], 3600, .8)[0]
                self.assertEqual(result['status'], 'incomplete_or_failed')
                self.assertFalse(result['near_timeout'])
                self.assertNotIn('omission_reason', result)

        malformed_cases = (
            ('wrong status', '<testcase name="declared" status="run"><skipped message="SKIP_RETURN_CODE=77"/></testcase>'),
            ('missing skipped child', '<testcase name="declared" status="notrun"/>'),
            ('missing status', '<testcase name="declared"><skipped message="SKIP_RETURN_CODE=77"/></testcase>'),
            ('wrong reason', '<testcase name="declared" status="notrun"><skipped message="other reason"/></testcase>'),
            ('duplicate skipped', '<testcase name="declared" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/><skipped message="SKIP_RETURN_CODE=77"/></testcase>'),
            ('failure plus skip', '<testcase name="declared" status="notrun"><failure message="assertion"/><skipped message="SKIP_RETURN_CODE=77"/></testcase>'),
            ('error plus skip', '<testcase name="declared" status="notrun"><error message="error"/><skipped message="SKIP_RETURN_CODE=77"/></testcase>'),
        )
        for label, case in malformed_cases:
            with self.subTest(case=label):
                self.path.write_text(f'<testsuite>{case}</testsuite>')
                result = runner.read_results(self.path, [selected], 3600, .8)[0]
                self.assertEqual(result['status'], 'incomplete_or_failed')
                self.assertFalse(result['near_timeout'])
                self.assertNotIn('omission_reason', result)

    def test_missing_extra_or_duplicate_results_fail_closed(self):
        for contents in ('', '<testcase name="wrong"/>', '<testcase name="wanted"/><testcase name="wanted"/>'):
            self.path.write_text(f'<testsuite>{contents}</testsuite>')
            with self.assertRaises(ValueError):
                runner.read_results(self.path, [test('wanted')], 3600, .8)


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ctest'), 'CMake/CTest required')
class CTestIntegration(unittest.TestCase):
    def test_pass_failure_and_missing_inventory_are_reported(self):
        import subprocess
        for fails in (False,True):
            with self.subTest(fails=fails), tempfile.TemporaryDirectory() as directory:
                root=Path(directory); build=root/'build'
                (root/'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.21)\nproject(CITest NONE)\nenable_testing()\n'
                    'add_test(NAME shared COMMAND "${CMAKE_COMMAND}" -E '+('false' if fails else 'true')+')\n'
                    'set_tests_properties(shared PROPERTIES LABELS "regular;contract")\n')
                subprocess.run(['cmake','-S',str(root),'-B',str(build)],check=True,capture_output=True)
                report=root/'report.json'
                with redirect_stdout(io.StringIO()):
                    code=runner.main(['--build-dir',str(build),'--scope','core','--report',str(report),'--jobs','1'])
                data=json.loads(report.read_text())
                self.assertEqual(code,int(fails))
                self.assertEqual(data['selected'],['shared'])
                self.assertEqual(data['status'],'failed' if fails else 'passed')
                self.assertEqual(data['result_counts']['incomplete_or_failed'],int(fails))

if __name__ == '__main__':
    unittest.main()
