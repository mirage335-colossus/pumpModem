"""Synthetic registration, partition and aggregation checks for CI planning."""
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ci_test_plan', ROOT / 'tools/ci-test-plan.py')
planner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(planner)
runner = planner.runner


def test(name, labels=()):
    return {'name': name, 'properties': [{'name': 'LABELS', 'value': list(labels)}]}


class SourceFixture(unittest.TestCase):
    def setUp(self):
        # Planning binds a real CI checkout. These registration fixtures must
        # also run from source archives and on hosts without Git installed.
        real_run = subprocess.run

        def run(command, *args, **kwargs):
            if command[0] == 'git':
                value = 'fixture-revision\n' if 'rev-parse' in command else ''
                if not kwargs.get('text'):
                    value = value.encode()
                return subprocess.CompletedProcess(command, 0, stdout=value, stderr='')
            return real_run(command, *args, **kwargs)

        self.subprocess_patch = patch.object(subprocess, 'run', side_effect=run)
        self.subprocess_patch.start()
        self.addCleanup(self.subprocess_patch.stop)


class PartitionPlan(SourceFixture):
    def setUp(self):
        super().setUp()
        self.tests = [test('ordinary-a'), test('ordinary-b'), test('gui_application', ('regular', 'contract')),
                      test('gui_controller', ('regular', 'contract')), test('native_gui_case', ('native_gui', 'contract')),
                      test('live', ('regular', 'contract')), test('live_profiles', ('regular', 'contract')),
                      test('scheduled_parallel_preparation', ('regular', 'contract', 'preparation')),
                      test('calibration-a', ('regular', 'contract', 'calibration')),
                      test('differential_receiver_probability', ('regular', 'contract'))]

    def test_core_parts_are_deterministic_disjoint_and_keep_gui_pair_together(self):
        model = planner.partition_model(self.tests, 'core')
        self.assertEqual(model, planner.partition_model(list(reversed(self.tests)), 'core'))
        self.assertEqual(set(model), {'0', '1'})
        self.assertIn('gui_controller', model['0'])
        self.assertIn('gui_application', model['0'])
        self.assertNotIn('gui_controller', model['1'])
        self.assertNotIn('gui_application', model['1'])

    def test_contract_partition_union_and_special_groups_are_exact(self):
        model = planner.partition_model(self.tests, 'contract')
        self.assertEqual(set(model), {'general', 'preparation', 'live', 'calibration'})
        self.assertEqual(model['preparation'], ['scheduled_parallel_preparation'])
        self.assertEqual(model['live'], ['live', 'live_profiles'])
        self.assertEqual(model['calibration'], ['differential_receiver_probability'])
        self.assertIn('calibration-a', model['general'])
        self.assertNotIn('native_gui_case', model['general'])
        self.assertEqual(len(sum(model.values(), [])), len(set(sum(model.values(), []))))

    def test_selector_matches_existing_runner_scope_rules(self):
        expected = sorted(item['name'] for item in runner.select_tests(self.tests, 'core')[0])
        model = planner.partition_model(self.tests, 'core')
        self.assertEqual(sorted(model['0'] + model['1']), expected)

    def test_duplicate_and_unmapped_registrations_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            planner.canonical_inventory([test('same'), test('same')])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'registration.json'
            path.write_text(json.dumps({'schema': 1, 'tests': [
                {'name': 'one', 'targets': []}, {'name': 'one', 'targets': []}]}))
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                planner.load_registration(path)

    @unittest.skipUnless(shutil.which('cmake') and shutil.which('ctest'), 'CMake/CTest needed')
    def test_actual_cmake_registration_export_drives_prerequisites(self):
        # No native compiler/application is run. Exercise the real CMake
        # mapping loop and configured discovery, rather than a copied map.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(PlanFixture LANGUAGES NONE)\n'
                'enable_testing()\n'
                'foreach(name alpha beta)\n'
                'add_custom_target(test_${name})\n'
                'add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}" -E true)\n'
                'endforeach()\n'
                f'include("{(ROOT / "cmake/TestGroups.cmake").as_posix()}")\n')
            build = root / 'output with spaces'
            subprocess.run(['cmake', '-S', str(root), '-B', str(build)],
                           check=True, capture_output=True)
            planner.prepare(SimpleNamespace(build_dir=build, source_root=ROOT,
                ctest=shutil.which('ctest'), config=None, scope='core', part='0',
                extra_target=[], output=root / 'plan.json'))
            plan = json.loads((root / 'plan.json').read_text())
            self.assertEqual(plan['registration'], {'alpha': ['test_alpha'], 'beta': ['test_beta']})
            self.assertEqual(plan['build_targets'], ['test_alpha'])

    def test_fake_ctest_plan_cross_checks_inventory_and_resolves_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'build'
            build.mkdir()
            (build / 'CMakeCache.txt').write_text('CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n')
            (build / 'ci-test-prerequisites.json').write_text(json.dumps({'schema': 1, 'tests': [
                {'name': item['name'], 'targets': ['target_' + item['name']]}
                for item in self.tests]}))
            args = SimpleNamespace(build_dir=build, ctest='fixture-ctest', config='Release',
                                   scope='core', part='0', source_root=ROOT,
                                   extra_target=['datapump-apps'], output=root / 'plan.json')
            real_run = subprocess.run

            def run(command, *values, **kwargs):
                if command[0] == 'fixture-ctest':
                    return subprocess.CompletedProcess(command, 0,
                        stdout=json.dumps({'tests': self.tests}).encode())
                return real_run(command, *values, **kwargs)

            with patch.object(subprocess, 'run', side_effect=run):
                planner.prepare(args)
            plan = json.loads(args.output.read_text())
            planner.validate_plan(plan)
            self.assertEqual(plan['selected_tests'], plan['parts']['0'])
            self.assertIn('datapump-apps', plan['build_targets'])
            self.assertTrue(set(plan['selected_tests']).issubset(plan['registration']))
            with self.assertRaisesRegex(ValueError, 'registration mismatch'):
                args2 = SimpleNamespace(**{**vars(args), 'output': root / 'bad.json'})
                (build / 'ci-test-prerequisites.json').write_text(json.dumps({'schema': 1, 'tests': []}))
                with patch.object(subprocess, 'run', side_effect=run):
                    planner.prepare(args2)

    @unittest.skipUnless(shutil.which('ctest'), 'CTest is needed for runner plan integration')
    def test_runner_consumes_plan_and_reports_bound_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'build'
            build.mkdir()
            command = json.dumps(Path(sys.executable).as_posix())
            code = json.dumps('print("ok")')
            (build / 'CTestTestfile.cmake').write_text(
                f'add_test(alpha {command} -c {code})\n'
                f'add_test(beta {command} -c {code})\n'
                'set_tests_properties(alpha beta PROPERTIES LABELS "regular")\n')
            (build / 'CMakeCache.txt').write_text('CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n')
            (build / 'ci-test-prerequisites.json').write_text(json.dumps({'schema': 1, 'tests': [
                {'name': 'alpha', 'targets': ['test_alpha']}, {'name': 'beta', 'targets': ['test_beta']}]}))
            plan_path = root / 'plan.json'
            planner.prepare(SimpleNamespace(build_dir=build, ctest=shutil.which('ctest'), config=None,
                                            scope='core', part='0', source_root=ROOT,
                                            extra_target=[], output=plan_path))
            plan = json.loads(plan_path.read_text())
            args = SimpleNamespace(build_dir=build, scope='core', config=None,
                                   ctest=shutil.which('ctest'), jobs=1, sanitizers=False,
                                   sanitizer_realtime=False, report=root / 'report.json',
                                   summary=None, timeout=30, warn_fraction=.8,
                                   selection_plan=plan_path)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(runner.run(args), 0)
            report = json.loads(args.report.read_text())
            self.assertEqual(report['selected_part'], '0')
            self.assertEqual(report['expected_tests'], plan['selected_tests'])
            self.assertEqual(report['plan_sha256'], plan['plan_sha256'])
            self.assertEqual(report['canonical_inventory_sha256'], plan['canonical_inventory_sha256'])


class Aggregation(SourceFixture):
    def setUp(self):
        super().setUp()
        inventory = planner.canonical_inventory([test('a'), test('b')])
        registration = {'a': ['test_a'], 'b': ['test_b']}
        self.plan = {'schema': 1, 'scope': 'core', 'selected_part': '0',
                     'parts': {'0': ['a'], '1': ['b']}, 'expected_tests': ['a', 'b'],
                     'selected_tests': ['a'], 'build_targets': ['test_a'],
                     'source_identity': {'revision': 'rev', 'working_tree_sha256': 'src'},
                     'config_identity': {'cache_values_sha256': 'cfg', 'config': 'Release'},
                     'canonical_inventory': inventory,
                     'canonical_inventory_sha256': planner.digest(inventory),
                     'registration': registration,
                     'registration_sha256': planner.digest(registration),
                     'ci_context': {'run_id': '42', 'repository': 'example/repo'}}
        self.plan['plan_sha256'] = planner.model_digest(self.plan)

    def report(self, part, name, **overrides):
        report = {'plan_sha256': self.plan['plan_sha256'], 'source_identity': self.plan['source_identity'],
                  'config_identity': self.plan['config_identity'], 'selected_part': part,
                  'expected_tests': self.plan['parts'][part], 'selected': self.plan['parts'][part],
                  'canonical_inventory_sha256': self.plan['canonical_inventory_sha256'],
                  'ci_context': self.plan['ci_context'],
                  'tests': [{'name': name, 'status': 'passed'}],
                  'ctest_returncode': 0, 'status': 'passed'}
        report.update(overrides)
        return report

    def test_complete_parts_aggregate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan_path = root / 'plan.json'
            plan_path.write_text(json.dumps(self.plan))
            paths = []
            for part, name in [('0', 'a'), ('1', 'b')]:
                path = root / f'{part}.json'
                path.write_text(json.dumps(self.report(part, name)))
                paths.append(path)
            args = type('Args', (), {'plan': plan_path, 'parts': paths, 'output': root / 'result.json'})()
            planner.aggregate(args)
            self.assertEqual(json.loads(args.output.read_text())['expected_tests'], ['a', 'b'])

    def test_common_identity_ignores_only_part_specific_provenance(self):
        other = {**self.plan, 'selected_part': '1', 'selected_tests': ['b'],
                 'build_targets': ['test_b'], 'configured_inventory_sha256': 'other-raw'}
        self.assertEqual(planner.model_digest(other), self.plan['plan_sha256'])
        planner.validate_plan(other)
        other['expected_tests'] = ['b']
        with self.assertRaises(ValueError):
            planner.validate_plan(other)

    def test_modified_registered_deadline_changes_common_identity(self):
        import copy
        changed = copy.deepcopy(self.plan)
        changed['canonical_inventory'][0]['properties'].append({'name': 'TIMEOUT', 'value': 10})
        self.assertNotEqual(planner.model_digest(changed), self.plan['plan_sha256'])

    def test_container_site_is_incidental_but_flags_and_compiler_bytes_are_not(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compiler = root / 'compiler'
            compiler.write_bytes(b'compiler identity one')
            cache = root / 'CMakeCache.txt'

            def identity(site, flags='-O1', sanitizers='ON'):
                cache.write_text(
                    f'CMAKE_CXX_COMPILER:FILEPATH={compiler}\n'
                    f'CMAKE_CXX_FLAGS:STRING={flags}\n'
                    f'DATAPUMP_SANITIZERS:BOOL={sanitizers}\n'
                    f'SITE:STRING={site}\n')
                return planner.config_identity(cache, 'Debug')

            first = identity('container-a')
            self.assertEqual(first, identity('container-b'))
            self.assertNotEqual(first, identity('container-b', flags='-O2'))
            self.assertNotEqual(first, identity('container-b', sanitizers='OFF'))
            compiler.write_bytes(b'compiler identity two')
            self.assertNotEqual(first, identity('container-a'))

    @unittest.skipUnless(shutil.which('ctest'), 'CTest is needed for complete aggregation integration')
    def test_separate_real_plans_preserve_junit_order_and_declared_omissions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            command = json.dumps(Path(sys.executable).as_posix())
            (root / 'CTestTestfile.cmake').write_text(
                f'add_test(skip {command} -c "raise SystemExit(77)")\n'
                f'add_test(beta {command} -c "print(1)")\n'
                f'add_test(alpha {command} -c "print(1)")\n'
                'set_tests_properties(skip PROPERTIES SKIP_RETURN_CODE 77)\n')
            (root / 'CMakeCache.txt').write_text('CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n')
            (root / 'ci-test-prerequisites.json').write_text(json.dumps({'schema': 1, 'tests': [
                {'name': name, 'targets': [f'test_{name}']} for name in ('skip', 'beta', 'alpha')]}))
            reports, plans = [], []
            for part in ('0', '1'):
                plan_path = root / f'plan-{part}.json'
                planner.prepare(SimpleNamespace(build_dir=root, source_root=ROOT,
                    ctest=shutil.which('ctest'), config=None, scope='core', part=part,
                    extra_target=[], output=plan_path))
                plans.append(json.loads(plan_path.read_text()))
                path = root / f'report-{part}.json'
                args = SimpleNamespace(build_dir=root, scope='core', config=None,
                    ctest=shutil.which('ctest'), jobs=1, sanitizers=False,
                    sanitizer_realtime=False, report=path, summary=None,
                    timeout=30, warn_fraction=.8, selection_plan=plan_path)
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.run(args), 0)
                reports.append(path)
            self.assertEqual(plans[0]['plan_sha256'], plans[1]['plan_sha256'])
            planner.aggregate(SimpleNamespace(plan=root / 'plan-0.json', parts=reports,
                                               output=root / 'aggregate.json'))
            aggregate = json.loads((root / 'aggregate.json').read_text())
            self.assertEqual(aggregate['status'], 'passed_with_omissions')
            self.assertEqual(aggregate['result_counts'],
                             {'passed': 2, 'omitted': 1, 'incomplete_or_failed': 0})
            self.assertEqual(aggregate['runtime_omissions'], ['skip'])


    def test_missing_duplicate_failed_cancelled_mismatch_and_omission_rejected(self):
        bad_sets = [
            [self.report('0', 'a')],
            [self.report('0', 'a'), self.report('0', 'a')],
            [self.report('0', 'a'), self.report('1', 'b', ctest_returncode=1)],
            [self.report('0', 'a'), self.report('1', 'b', status='cancelled')],
            [self.report('0', 'a'), self.report('1', 'b', source_identity={'revision': 'different'})],
            [self.report('0', 'a'), self.report('1', 'b', tests=[{'name': 'b', 'status': 'omitted'}])],
        ]
        for bad in bad_sets:
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                plan_path = root / 'plan.json'
                plan_path.write_text(json.dumps(self.plan))
                paths = []
                for i, report in enumerate(bad):
                    path = root / f'{i}.json'
                    path.write_text(json.dumps(report))
                    paths.append(path)
                args = type('Args', (), {'plan': plan_path, 'parts': paths, 'output': root / 'result.json'})()
                with self.assertRaises(ValueError):
                    planner.aggregate(args)


if __name__ == '__main__':
    unittest.main()
