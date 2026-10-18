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
                     test('calibration', ('regular', 'contract', 'calibration')),
                     test('terminal', ('frontends',)), test('framebuffer', ('frontends',)),
                     test('web_worker', ('web',)), test('wasm_live', ('web',))]
        owners = []
        for scope in runner.SCOPES:
            selected, omitted, counts = runner.select_tests(inventory, scope)
            self.assertFalse(omitted)
            self.assertEqual(sum(counts.values()), len(inventory))
            owners += [item['name'] for item in selected]
        self.assertCountEqual(owners, [item['name'] for item in inventory])
        self.assertEqual(len(owners), len(set(owners)))

    def test_frontends_are_disjoint_from_core_and_fast(self):
        inventory = [test('core'), test('terminal', ('gui', 'frontends')),
                     test('framebuffer', ('frontends',)), test('fast', ('fast',))]
        selected, omitted, _ = runner.select_tests(inventory, 'frontends')
        self.assertEqual([item['name'] for item in selected], ['terminal', 'framebuffer'])
        self.assertFalse(omitted)
        for scope in ('core', 'fast'):
            selected, _, _ = runner.select_tests(inventory, scope)
            self.assertEqual([item['name'] for item in selected], [scope])

    def test_web_scope_does_not_extend_native_core_or_frontend_jobs(self):
        inventory = [test('core'), test('terminal', ('frontends',)),
                     test('execution', ('web',)), test('web_live', ('web',)),
                     test('build_wasm_sdk', ('web', 'build')), test('wasm_live', ('web',))]
        selected, omitted, _ = runner.select_tests(inventory, 'web')
        self.assertEqual([item['name'] for item in selected],
                         ['execution', 'web_live', 'build_wasm_sdk', 'wasm_live'])
        self.assertFalse(omitted)
        self.assertEqual([item['name'] for item in runner.select_tests(inventory, 'core')[0]], ['core'])
        self.assertEqual([item['name'] for item in runner.select_tests(inventory, 'frontends')[0]], ['terminal'])

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


@unittest.skipUnless(shutil.which('cmake'), 'CMake is needed for target partition fixtures')
class BuildPartitions(unittest.TestCase):
    def test_core_compilation_does_not_build_web_prerequisites(self):
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            group_file = (ROOT / 'cmake/TestGroups.cmake').as_posix()
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(Partitions NONE)\nenable_testing()\n'
                'add_custom_target(test_ordinary COMMAND ${CMAKE_COMMAND} -E touch ordinary-built)\n'
                'add_custom_target(test_browser COMMAND ${CMAKE_COMMAND} -E touch browser-built)\n'
                'add_test(NAME ordinary COMMAND ${CMAKE_COMMAND} -E true)\n'
                'add_test(NAME browser COMMAND ${CMAKE_COMMAND} -E true)\n'
                'set_tests_properties(browser PROPERTIES LABELS "web")\n'
                f'include("{group_file}")\n')
            build = root / 'build'
            subprocess.run(['cmake', '-S', str(root), '-B', str(build)],
                           check=True, capture_output=True, text=True)
            subprocess.run(['cmake', '--build', str(build), '--target', 'datapump-tests-ci-core'],
                           check=True, capture_output=True, text=True)
            self.assertTrue((build / 'ordinary-built').is_file())
            self.assertFalse((build / 'browser-built').exists())
            subprocess.run(['cmake', '--build', str(build), '--target', 'datapump-tests-web'],
                           check=True, capture_output=True, text=True)
            self.assertTrue((build / 'browser-built').is_file())


@unittest.skipUnless(shutil.which('ctest'), 'CTest is needed for temporary integration fixtures')
class CTestIntegration(unittest.TestCase):
    def exercise(self, program, timeout=5, *, skip_return=None, companion_program=None,
                 jobs=1, stale_last_test=False, truncated_markers=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = Path(sys.executable).as_posix()
            properties = f'LABELS "regular;contract" TIMEOUT "{timeout}"'
            if skip_return is not None:
                properties += f' SKIP_RETURN_CODE "{skip_return}"'
            quote = lambda value: json.dumps(str(value))
            tests = [f'add_test(sample {quote(executable)} "-c" {quote(program)})',
                     f'set_tests_properties(sample PROPERTIES {properties})']
            if companion_program is not None:
                tests.extend((f'add_test(companion {quote(executable)} "-c" {quote(companion_program)})',
                              f'set_tests_properties(companion PROPERTIES {properties})'))
            (root / 'CTestTestfile.cmake').write_text('\n'.join(tests) + '\n')
            if stale_last_test:
                old = root / 'Testing' / 'Temporary'
                old.mkdir(parents=True)
                (old / 'LastTest.log').write_text('999: TEST_WORKLOAD_BUDGET: stale prior run\n')
            if truncated_markers:
                (root / 'CTestCustom.cmake').write_text(
                    'set(CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE 1024)\n')
            report = root / 'report.json'
            args = argparse.Namespace(build_dir=root, scope='core', config=None, ctest=shutil.which('ctest'),
                                      jobs=jobs, sanitizers=False, sanitizer_realtime=False, report=report,
                                      summary=root / 'summary.md', timeout=3600, warn_fraction=.005)
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = runner.run(args)
            contents = json.loads(report.read_text())
            contents['_summary_text'] = (root / 'summary.md').read_text()
            if truncated_markers:
                junit = root / 'report.xml'
                xml = junit.read_text()
                for marker in truncated_markers:
                    self.assertNotIn(marker, xml)
            return result, contents, stdout.getvalue()

    def test_real_ctest_collects_late_warnings_after_large_stdout_and_stderr(self):
        markers = ['TEST_WORKLOAD_BUDGET: late warning one',
                   'TEST_WORKLOAD_BUDGET: late warning two']
        program = ("import sys; print('O'*4096, flush=True); "
                   "print('E'*4096, file=sys.stderr, flush=True); "
                   f"print({markers[0]!r}, flush=True); print({markers[1]!r}, flush=True)")
        result, report, output = self.exercise(program, truncated_markers=markers)
        self.assertEqual(result, 0, output[-2000:])
        self.assertEqual(report['tests'][0]['workload_warnings'], markers)
        self.assertGreater(len(output), 1024)
        self.assertIn('::warning title=CI test workload::sample passed all assertions.', output)
        for marker in markers:
            self.assertIn(marker, report['_summary_text'])

    def test_real_ctest_ignores_stale_lasttest_and_nested_fake_prefix(self):
        program = "print('2: TEST_WORKLOAD_BUDGET: nested fake output')"
        result, report, output = self.exercise(program, stale_last_test=True)
        self.assertEqual(result, 0, output)
        self.assertEqual(report['tests'][0]['workload_warnings'], [])
        self.assertNotIn('stale prior run', output)

    def test_real_parallel_ctest_maps_nonconsecutive_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / 'parallel.py'
            fixture.write_text(
                'import sys,time\nfrom pathlib import Path\n'
                'name=sys.argv[1]\nPath("ready-"+name).touch()\n'
                'deadline=time.monotonic()+4\n'
                'while not all(Path("ready-"+n).exists() for n in ("first","second")):\n'
                ' assert time.monotonic()<deadline,"parallel peer did not start"\n'
                ' time.sleep(.005)\n'
                'for index in range(2):\n'
                ' print("TEST_WORKLOAD_BUDGET: "+name+" "+str(index),flush=True)\n'
                ' time.sleep(.02)\n')
            quote = lambda value: json.dumps(str(value).replace('\\', '/'))
            lines = []
            for name, labels in [('first', 'regular;contract'), ('excluded', 'fast'),
                                 ('second', 'regular;contract')]:
                lines.append(f'add_test({name} {quote(sys.executable)} {quote(fixture)} {name})')
                lines.append(f'set_tests_properties({name} PROPERTIES LABELS "{labels}" TIMEOUT 5)')
            (root / 'CTestTestfile.cmake').write_text('\n'.join(lines)+'\n')
            report = root / 'report.json'
            args = argparse.Namespace(build_dir=root, scope='core', config=None,
                                      ctest=shutil.which('ctest'), jobs=2, sanitizers=False,
                                      sanitizer_realtime=False, report=report,
                                      summary=root / 'summary.md', timeout=3600, warn_fraction=.8)
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = runner.run(args)
            self.assertEqual(result, 0, stdout.getvalue())
            data = json.loads(report.read_text())
            self.assertEqual(set(data['selected']), {'first', 'second'})
            self.assertEqual(data['command'][data['command'].index('--parallel')+1], '2')
            self.assertIn('Start 1: first', stdout.getvalue())
            self.assertIn('Start 3: second', stdout.getvalue())
            self.assertEqual({case['name']: case['workload_warnings'] for case in data['tests']},
                             {name: [f'TEST_WORKLOAD_BUDGET: {name} {i}' for i in range(2)]
                              for name in ('first', 'second')})

    def test_runner_drains_and_waits_before_rejecting_bad_framing(self):
        real_popen = __import__('subprocess').Popen
        observed = {}
        class Child:
            def __init__(self, command):
                self.command = command
                self.stdout = iter(['Start 1: sample\n', '1: TEST_WORKLOAD_BUDGET:'])
            def __enter__(self):
                return self
            def __exit__(self, *args):
                return False
            def wait(self):
                observed['waited'] = True
                junit = Path(self.command[self.command.index('--output-junit') + 1])
                junit.write_text('<testsuite><testcase name="sample" status="run"/></testsuite>')
                return 0
        def popen(command, *args, **kwargs):
            if '--show-only=json-v1' in command:
                return real_popen(command, *args, **kwargs)
            observed['child'] = Child(command)
            return observed['child']
        from unittest.mock import patch
        with patch.object(__import__('subprocess'), 'Popen', side_effect=popen):
            result, report, _ = self.exercise('print("ok")')
        self.assertEqual(result, 1)
        self.assertTrue(observed.get('waited'))
        self.assertEqual(report['status'], 'failed')
        self.assertIn('incomplete workload warning line', report['error'])

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
        for marker in (
                'TEST_WORKLOAD_BUDGET: fixture: exceeded 30s normal budget; instrumented limit 90s (full completion remains required)',
                'TEST_WORKLOAD_BUDGET: GUI smoke completed in 601s, above its ordinary 600s workload allowance; every workflow assertion completed.'):
            with self.subTest(marker=marker):
                result, report, output = self.exercise(f'print({marker!r})')
                self.assertEqual(result, 0)
                self.assertEqual(report['tests'][0]['workload_warnings'], [marker])
                self.assertIn('::warning title=CI test workload::sample passed all assertions.', output)

    def test_workload_warning_does_not_turn_skip_or_timeout_into_success(self):
        marker = 'TEST_WORKLOAD_BUDGET: status remains authoritative'
        result, report, output = self.exercise(f'print({marker!r}); raise SystemExit(77)', skip_return=77)
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['error'], 'No selected test passed; all selected coverage was omitted')
        self.assertEqual(report['tests'][0]['status'], 'omitted')
        self.assertEqual(report['tests'][0]['workload_warnings'], [marker])
        self.assertEqual(report['result_counts'],
                         {'passed': 0, 'omitted': 1, 'incomplete_or_failed': 0})
        self.assertEqual(report['runtime_omissions'], ['sample'])
        self.assertNotIn('::warning title=CI test workload::', output)
        self.assertNotIn('passed all assertions', output)

        result, report, output = self.exercise(
            f'import time; print({marker!r}, flush=True); time.sleep(10)', timeout=.1)
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertFalse(any('passed all assertions' in line for line in output.splitlines()))

    def test_real_ctest_mixed_pass_and_declared_skip_is_success_with_omission(self):
        result, report, output = self.exercise(
            'raise SystemExit(77)', skip_return=77, companion_program='print("passed")')
        self.assertEqual(result, 0, output[-2000:])
        self.assertEqual(report['status'], 'passed_with_omissions')
        self.assertEqual(report['result_counts'],
                         {'passed': 1, 'omitted': 1, 'incomplete_or_failed': 0})
        self.assertEqual(report['runtime_omissions'], ['sample'])
        self.assertEqual(report['omitted'], [])
        statuses = {case['name']: case['status'] for case in report['tests']}
        self.assertEqual(statuses, {'sample': 'omitted', 'companion': 'passed'})
        self.assertIn('sample', report['_summary_text'])
        self.assertIn('companion', report['_summary_text'])
        summary = report['_summary_text']
        self.assertRegex(summary, r'(?i)(?:\b1\b[^\n]*\bpassed\b|\bpassed\b[^\n]*\b1\b)')
        self.assertRegex(summary, r'(?i)(?:\b1\b[^\n]*\bomissions?\b|\bomissions?\b[^\n]*\b1\b)')
        self.assertRegex(summary, r'(?i)(?:\b0\b[^\n]*\bincomplete\b|\bincomplete\b[^\n]*\b0\b)')
        self.assertNotIn('sample passed all assertions', output)

    def test_real_ctest_fixture_preserves_python_program_backslashes(self):
        result, report, output = self.exercise(
            'raise SystemExit(77)', skip_return=77,
            companion_program=r"print(r'left\right')")
        self.assertEqual(result, 0, output[-2000:])
        self.assertEqual(report['status'], 'passed_with_omissions')
        self.assertRegex(output, r'(?m)^\d+: left\\right$')

    def test_mixed_declared_omission_never_masks_failure_or_timeout(self):
        cases = (
            ('assertion', 'raise SystemExit(77)', 'raise AssertionError("assertion")', None),
            ('timeout', 'raise SystemExit(77)', 'import time; time.sleep(10)', .1),
        )
        for label, skipped, companion, timeout in cases:
            with self.subTest(case=label):
                result, report, output = self.exercise(
                    skipped, skip_return=77, companion_program=companion,
                    timeout=timeout or 5)
                self.assertEqual(result, 1, output[-2000:])
                self.assertEqual(report['status'], 'failed')
                self.assertEqual(report['runtime_omissions'], ['sample'])
                self.assertEqual(report['result_counts']['omitted'], 1)
                self.assertEqual(report['result_counts']['incomplete_or_failed'], 1)
                self.assertNotIn('sample passed all assertions', output)

    def test_nonzero_ctest_overrules_valid_mixed_results(self):
        subprocess = __import__('subprocess')
        real_popen = subprocess.Popen
        observed = {}

        class Child:
            def __init__(self, process):
                self.process = process
                self.stdout = process.stdout
            def __enter__(self):
                self.process.__enter__()
                return self
            def __exit__(self, *args):
                return self.process.__exit__(*args)
            def wait(self):
                observed['real_returncode'] = self.process.wait()
                observed['stdout_drained'] = self.stdout.read() == ''
                return 1

        def popen(command, *args, **kwargs):
            process = real_popen(command, *args, **kwargs)
            if '--show-only=json-v1' in command:
                return process
            return Child(process)

        from unittest.mock import patch
        with patch.object(subprocess, 'Popen', side_effect=popen):
            result, report, output = self.exercise(
                'raise SystemExit(77)', skip_return=77, companion_program='print("passed")')
        self.assertEqual(observed, {'real_returncode': 0, 'stdout_drained': True})
        self.assertEqual(result, 1, output[-2000:])
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['result_counts'],
                         {'passed': 1, 'omitted': 1, 'incomplete_or_failed': 0})
        self.assertEqual(report['runtime_omissions'], ['sample'])

    def test_workload_marker_cannot_turn_failure_into_warning(self):
        marker = 'TEST_WORKLOAD_BUDGET: fixture: exceeded 30s normal budget'
        result, report, output = self.exercise(f'print({marker!r}); raise AssertionError(123)')
        self.assertEqual(result, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['tests'][0]['workload_warnings'], [marker])
        self.assertEqual(report['tests'][0]['status'], 'incomplete_or_failed')
        self.assertNotIn('::warning title=CI test workload::', output)


if __name__ == '__main__':
    unittest.main()
