"""Preparation evidence uses actual CTest framing, never a partial green union."""
from contextlib import redirect_stdout
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ci_preparation_plan', ROOT / 'tools/ci-preparation-plan.py')
preparation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preparation)
planner = preparation.planner
CONTEXT_ENV = {'GITHUB_SHA': 'fixture-revision', 'GITHUB_RUN_ID': '42',
               'GITHUB_REPOSITORY': 'owner/repo', 'GITHUB_RUN_ATTEMPT': '2'}
CLEAN_SOURCE = {'revision': 'fixture-revision', 'working_tree_sha256': planner.digest(b''),
                'untracked_input_sha256': planner.digest({})}
CATALOG = {'schema': 1, 'parts': preparation.EXPECTED_PARTS,
           'all_cases': list(preparation.ALL_CASES)}


class Framing(unittest.TestCase):
    def stream(self, markers, *, start=True, end=True):
        return (('    Start 7: scheduled_parallel_preparation\n' if start else '') + markers +
                ('1/1 Test #7: scheduled_parallel_preparation ... Passed 0.01 sec\n' if end else '')).encode()

    def markers(self, name='arithmetic_default'):
        return f'7: PREPARATION_CASE_BEGIN: {name}\n7: PREPARATION_CASE_END: {name}\n'

    def test_live_matching_number_and_order_are_required(self):
        self.assertEqual(len(preparation.case_events(self.stream(self.markers()), 7,
                                                    ['arithmetic_default'])), 2)
        for markers in (self.markers().replace('7:', '6:'),
                        self.markers().replace('7: ', ''),
                        self.markers().replace('7:', '7: 7:'),
                        self.markers().replace('BEGIN', 'END'),
                        self.markers() + self.markers(),
                        '7: PREPARATION_CASE_BEGIN: arithmetic_default\n',
                        self.markers().replace('arithmetic_default', 'other'),
                        self.markers().replace('BEGIN:', 'BEGIN ')):
            with self.subTest(markers=markers), self.assertRaises(ValueError):
                preparation.case_events(self.stream(markers), 7, ['arithmetic_default'])

    def test_unframed_nested_metadata_and_summary_replay_are_not_evidence(self):
        fake = ('PREPARATION_CASE_BEGIN: arithmetic_default\n'
                '7: 7: PREPARATION_CASE_BEGIN: arithmetic_default\n'
                '7: Test command: PREPARATION_CASE_BEGIN: arithmetic_default\n')
        raw = self.stream(fake + self.markers()) + b'The following tests passed:\n' + self.markers().encode()
        self.assertEqual(len(preparation.case_events(raw, 7, ['arithmetic_default'])), 2)
        for raw in (self.stream(fake), self.stream('', end=True) + self.markers().encode(),
                    self.stream(self.markers(), start=False),
                    self.stream(self.markers(), end=False),
                    self.stream(self.markers()).replace(b'Passed', b'***Failed')):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                preparation.case_events(raw, 7, ['arithmetic_default'])

    def test_incomplete_marker_and_duplicate_ctest_identity_are_rejected(self):
        for raw in (self.stream(self.markers(), end=False).rstrip(b'\n'),
                    self.stream('    Start 7: scheduled_parallel_preparation\n' + self.markers()),
                    self.stream(self.markers()) + b'1/1 Test #7: scheduled_parallel_preparation ... Passed 0.01 sec\n'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                preparation.case_events(raw, 7, ['arithmetic_default'])

    def test_catalog_cannot_redefine_complete_coverage(self):
        preparation.validate_catalog(CATALOG)
        bad = []
        for mutate in (lambda c: c['all_cases'].pop(),
                       lambda c: c['parts']['3'].pop(),
                       lambda c: c['parts']['0'].append('shared'),
                       lambda c: c['parts'].update({'4': []}),
                       lambda c: c.update(schema=True),
                       lambda c: c['all_cases'].reverse()):
            catalog = copy.deepcopy(CATALOG)
            mutate(catalog)
            bad.append(catalog)
        for catalog in bad:
            with self.subTest(catalog=catalog), self.assertRaises(ValueError):
                preparation.validate_catalog(catalog)
        with self.assertRaisesRegex(ValueError, 'duplicate JSON'):
            preparation.read_json('{"schema":1,"schema":1}')


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ctest'), 'CMake/CTest needed')
class ActualCTestFixture(unittest.TestCase):
    """LANGUAGES NONE and a Python stand-in exercise the real CI input boundary.

    No modem or compiler is launched. CTest creates all discovery/JUnit/log
    inputs, and the actual runner supplies its result report. Git alone is
    mocked so the same checks work in dirty checkouts and source archives.
    """
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.ctest = shutil.which('ctest')
        cls.environment = patch.dict(os.environ, CONTEXT_ENV)
        cls.environment.start()
        cls.addClassCleanup(cls.environment.stop)
        cls.source = patch.object(planner, 'source_identity', return_value=CLEAN_SOURCE)
        cls.source.start()
        cls.addClassCleanup(cls.source.stop)
        cls.fixture = cls.root / 'source with spaces'
        cls.fixture.mkdir()
        cls.binary = cls.fixture / preparation.TARGET
        cls.binary.write_text('#!' + sys.executable + '\n'
            'import json, sys\n'
            f'catalog = {CATALOG!r}\n'
            'if sys.argv[1:] == ["--ci-inventory"]:\n'
            '    print(json.dumps(catalog)); raise SystemExit(0)\n'
            'assert len(sys.argv) == 3 and sys.argv[1] == "--ci-part"\n'
            'for name in catalog["parts"][sys.argv[2]]:\n'
            '    print("PREPARATION_CASE_BEGIN: " + name, flush=True)\n'
            '    print("PREPARATION_CASE_END: " + name, flush=True)\n')
        cls.binary.chmod(0o755)
        registration = json.dumps({'schema': 1, 'tests': [
            {'name': 'unrelated', 'targets': []},
            {'name': preparation.TEST, 'targets': [preparation.TARGET]}]})
        (cls.fixture / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(PreparationFixture LANGUAGES NONE)\n'
            'enable_testing()\n'
            'set(DATAPUMP_CI_PREPARATION_PART "all" CACHE STRING "part")\n'
            'set(DATAPUMP_SANITIZERS ON CACHE BOOL "fixture sanitizer identity")\n'
            'add_test(NAME unrelated COMMAND "${CMAKE_COMMAND}" -E true)\n'
            f'add_test(NAME {preparation.TEST} COMMAND "{cls.binary.as_posix()}" '
            '--ci-part "${DATAPUMP_CI_PREPARATION_PART}")\n'
            f'set_tests_properties({preparation.TEST} PROPERTIES LABELS "regular;contract;preparation")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/ci-test-prerequisites.json" '
            f'[=[{registration}]=])\n')
        cls.build = cls.root / 'configured output'
        cls.records = {}
        cls.paths = []
        for part in preparation.PARTS:
            subprocess.run(['cmake', '-S', str(cls.fixture), '-B', str(cls.build),
                '-DCMAKE_BUILD_TYPE=Debug', '-DDATAPUMP_CI_PREPARATION_PART=' + part],
                check=True, capture_output=True)
            args = cls.capture_args(part)
            result = subprocess.run([sys.executable, str(ROOT / 'tools/run-ci-tests.py'),
                '--build-dir', str(cls.build), '--scope', 'preparation', '--ctest', cls.ctest,
                '--jobs', '1', '--sanitizers', '--timeout', '9000', '--report', str(args.report)],
                capture_output=True, text=True)
            if result.returncode != 0:
                raise AssertionError(f'fixture runner CLI failed: {result.stdout} {result.stderr}')
            preparation.capture(args)
            cls.records[part] = json.loads(args.output.read_text())
            cls.paths.append(args.output)

    @classmethod
    def capture_args(cls, part='3', **overrides):
        return SimpleNamespace(**{'source_root': cls.fixture, 'build_dir': cls.build,
            'part': part, 'report': cls.root / ('report-' + part + '.json'),
            'output': cls.root / ('part-' + part + '.json'), 'ctest': cls.ctest,
            'config': None, **overrides})

    def aggregate(self, records=None, paths=None):
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            root = Path(directory)
            if records is not None:
                paths = []
                for index, record in enumerate(records):
                    path = root / f'input-{index}.json'
                    path.write_text(json.dumps(record))
                    paths.append(path)
            output = root / 'aggregate.json'
            preparation.aggregate(SimpleNamespace(source_root=self.fixture,
                parts=paths if paths is not None else self.paths, output=output))
            return json.loads(output.read_text())

    def reseal(self, record):
        record.pop('capture_sha256', None)
        return preparation.seal(record)

    def test_public_cli_float_timeout_is_exact_and_wrong_values_are_rejected(self):
        args = self.capture_args()
        original = args.report.read_bytes()
        report = json.loads(original)
        index = report['command'].index('--timeout') + 1
        self.assertEqual(report['command'][index], '9000.0')
        try:
            for timeout in ('9000.01', '8999', 'nan', 'inf', True):
                changed = copy.deepcopy(report)
                changed['command'][index] = timeout
                args.report.write_text(json.dumps(changed))
                with self.subTest(timeout=timeout), self.assertRaisesRegex(ValueError, 'runner timeout'):
                    preparation.capture(args)
        finally:
            args.report.write_bytes(original)

    def test_all_parts_are_partial_and_only_complete_union_qualifies(self):
        self.assertEqual({record['coverage'] for record in self.records.values()}, {'partial'})
        self.assertEqual({record['ctest_number'] for record in self.records.values()}, {2})
        result = self.aggregate()
        self.assertEqual(result['coverage'], 'complete')
        self.assertEqual(result['completed_cases'], list(preparation.ALL_CASES))
        self.assertEqual(result['completed_parts'], list(preparation.PARTS))

    def test_only_known_part_cache_key_is_normalized(self):
        records = list(self.records.values())
        self.assertEqual(len({record['config_identity']['cache_values_sha256'] for record in records}), 1)
        self.assertEqual(len({record['raw_config_identity']['cache_values_sha256'] for record in records}), 4)
        cache = self.build / 'CMakeCache.txt'
        original = cache.read_text()
        try:
            cache.write_text(original + '\nCMAKE_CXX_FLAGS:STRING=-fchanged\n')
            changed = planner.config_identity(cache, None, ignored_cache_keys=(preparation.CACHE_PART,))
            self.assertNotEqual(changed, self.records['3']['config_identity'])
        finally:
            cache.write_text(original)

    def test_missing_duplicate_failed_and_integrity_results_are_fatal(self):
        records = list(self.records.values())
        with self.assertRaisesRegex(ValueError, 'all four'):
            self.aggregate(records=records[:-1])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.aggregate(records=records + records[:1])
        for field, value in (('status', 'failed'), ('coverage', 'complete'),
                             ('capture_sha256', 'wrong')):
            bad = copy.deepcopy(records)
            bad[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.aggregate(records=bad)

    def test_current_source_and_every_workflow_context_field_must_match(self):
        for field in ('source_sha', 'run_id', 'repository', 'run_attempt'):
            bad = copy.deepcopy(list(self.records.values()))
            bad[0]['ci_context'][field] = 'different'
            self.reseal(bad[0])
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.aggregate(records=bad)
        with patch.object(planner, 'source_identity', return_value={**CLEAN_SOURCE,
                          'working_tree_sha256': 'dirty'}), self.assertRaisesRegex(ValueError, 'clean'):
            self.aggregate()
        with patch.dict(os.environ, {'GITHUB_SHA': 'other'}), self.assertRaisesRegex(ValueError, 'revision'):
            self.aggregate()

    def test_configuration_binary_registration_catalog_and_events_must_match(self):
        changes = (
            lambda r: r['config_identity'].update(cache_values_sha256='different'),
            lambda r: r['config_identity'].update(sanitizers='OFF'),
            lambda r: r.update(binary_sha256='a' * 64),
            lambda r: r['registration'].update(scheduled_parallel_preparation=[]),
            lambda r: r['catalog']['parts']['0'].append('shared'),
            lambda r: r['case_events'].pop(),
            lambda r: r['selected_command'].append('--extra'),
            lambda r: r.update(ignored_cache_keys=[preparation.CACHE_PART, 'CMAKE_CXX_FLAGS']),
            lambda r: r['raw_config_identity']['cache_values'].update(CMAKE_CXX_FLAGS='changed'),
            lambda r: r['raw_config_identity']['cache_values'].update(DATAPUMP_CI_PREPARATION_PART='2'),
            lambda r: r['config_identity']['compiler_library_inputs'].update(compiler='changed'),
            lambda r: r['configured_test_order'].reverse(),
            lambda r: r.update(ctest_number=1),
            lambda r: r['runner_report'].update(inventory_sha256='a' * 64),
        )
        for index, change in enumerate(changes):
            bad = copy.deepcopy(list(self.records.values()))
            change(bad[0])
            self.reseal(bad[0])
            with self.subTest(change=index), self.assertRaises(ValueError):
                self.aggregate(records=bad)

    def test_runner_failure_omission_and_warning_cannot_be_relabelled_successful(self):
        for change in (lambda r: r.update(ctest_returncode=1),
                       lambda r: r.update(status='passed_with_omissions'),
                       lambda r: r.update(omitted=[preparation.TEST]),
                       lambda r: r.update(coverage_limits=[{'test': preparation.TEST}]),
                       lambda r: r['tests'][0].update(workload_warnings=['budget']),
                       lambda r: r.update(selected=['other'])):
            record = copy.deepcopy(self.records['0'])
            change(record['runner_report'])
            self.reseal(record)
            with self.assertRaises(ValueError):
                preparation.validate_capture(record)

    def test_capture_rechecks_actual_sidecars_registration_and_configured_part(self):
        # The shared fixture ends on part 3. All modifications are restored
        # before the next read; nothing is built or run beyond the tiny test.
        args = self.capture_args(output=self.root / 'recapture.json')
        preparation.capture(args)
        files = [args.report.with_suffix('.log'), args.report.with_suffix('.xml'),
                 args.report.with_suffix('.inventory.json'), self.build / 'ci-test-prerequisites.json']
        for path in files:
            original = path.read_bytes()
            try:
                if path.suffix == '.log':
                    path.write_bytes(original.replace(b'2: PREPARATION_CASE_END: tiled\n', b''))
                elif path.suffix == '.xml':
                    path.write_bytes(original.replace(b'status="run"', b'status="fail"'))
                elif path.name == 'ci-test-prerequisites.json':
                    path.write_text('{"schema":1,"tests":[]}')
                else:
                    path.write_bytes(original + b' ')
                with self.subTest(path=path), self.assertRaises(ValueError):
                    preparation.capture(args)
            finally:
                path.write_bytes(original)
        with self.assertRaisesRegex(ValueError, 'configured preparation part'):
            preparation.capture(self.capture_args(part='0', output=args.output))

    def test_no_evidence_is_published_when_source_changes_during_capture(self):
        output = self.root / 'changed-source-must-not-publish.json'
        with patch.object(planner, 'source_identity', side_effect=[CLEAN_SOURCE,
                          {**CLEAN_SOURCE, 'working_tree_sha256': 'changed'}]):
            with self.assertRaisesRegex(ValueError, 'clean'):
                preparation.capture(self.capture_args(output=output))
        self.assertFalse(output.exists())

    def test_cli_aggregate_requires_all_parts_and_current_context(self):
        output = self.root / 'cli-aggregate.json'
        argv = ['aggregate', '--source-root', str(self.fixture), '--parts',
                *map(str, self.paths), '--output', str(output)]
        self.assertEqual(preparation.main(argv), 0)
        self.assertEqual(json.loads(output.read_text())['coverage'], 'complete')
        with patch.dict(os.environ, {'GITHUB_RUN_ATTEMPT': ''}), redirect_stderr(io.StringIO()):
            self.assertEqual(preparation.main(argv), 1)

    def test_combined_original_workload_budget_and_warning_boundaries(self):
        for total, warning in ((7199, False), (7200, True), (9000, True)):
            records = copy.deepcopy(list(self.records.values()))
            for record in records:
                record['runner_report']['tests'][0]['seconds'] = total / 4
                self.reseal(record)
            stream = io.StringIO()
            with redirect_stdout(stream):
                result = self.aggregate(records=records)
            self.assertEqual(result['serial_equivalent_seconds'], total)
            self.assertEqual(result['serial_equivalent_near_timeout'], warning)
            self.assertEqual('::warning' in stream.getvalue(), warning)
            self.assertEqual(result['status'], 'passed')
        records[-1]['runner_report']['tests'][0]['seconds'] += .01
        self.reseal(records[-1])
        with self.assertRaisesRegex(ValueError, 'original 9000s budget'):
            self.aggregate(records=records)

    def test_invalid_timings_and_actual_junit_timing_mismatch_are_fatal(self):
        for value in (-1, True, float('inf'), float('nan'), '1'):
            report = copy.deepcopy(self.records['0']['runner_report'])
            report['tests'][0]['seconds'] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'invalid seconds'):
                preparation.validate_runner_report(report, CONTEXT_ENV['GITHUB_SHA'])
        args = self.capture_args(output=self.root / 'timing-mismatch.json')
        original = args.report.read_bytes()
        try:
            report = json.loads(original)
            report['tests'][0]['seconds'] += 1
            args.report.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'JUnit evidence differs'):
                preparation.capture(args)
        finally:
            args.report.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
