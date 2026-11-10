#!/usr/bin/env python3
"""Cheap checks for local workflow wiring and the bounded Robust CI boundary."""
from pathlib import Path
import importlib.util
import json
import os
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / '.github/workflows'
spec = importlib.util.spec_from_file_location('triad_ci_reference', ROOT / 'tools/triad-ci-reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


def workflow_run(source, name):
    """Read one literal run block without adding a YAML dependency to CI."""
    step = source.split(f'    - name: {name}\n', 1)[1]
    block = step.split('      run: |\n', 1)[1].split('\n    - ', 1)[0]
    return '\n'.join(line[8:] for line in block.splitlines() if line.strip()) + '\n'


class Workflows(unittest.TestCase):
    def test_reusable_workflows_exist(self):
        for path in WORKFLOWS.glob('*.yml'):
            for name in re.findall(r'uses:\s*\./\.github/workflows/([^\s]+)', path.read_text()):
                with self.subTest(workflow=path.name, dependency=name):
                    self.assertTrue((WORKFLOWS / name).is_file())

    def test_retired_robust_jobs_cannot_be_dispatched(self):
        obsolete = ('triad_perf_guard', 'triad_perf_complete', 'triad_perf_bootstrap',
                    'test_differential_receiver_probability', 'test_scheduled_parallel_preparation',
                    'ci-preparation-plan.py', 'ci-test-plan.py', 'test_live_profiles',
                    'DATAPUMP_CALIBRATION_SECTION')
        for path in WORKFLOWS.glob('*.yml'):
            for token in obsolete:
                with self.subTest(workflow=path.name, token=token):
                    self.assertNotIn(token, path.read_text())

    def test_abbreviated_ci_uses_native_reference_and_bounded_policy(self):
        source = (WORKFLOWS / 'triad-abbreviated.yml').read_text()
        self.assertIn('inputs.candidate_ref || github.sha', source)
        self.assertIn('tools/triad_abbreviated.py --output', source)
        self.assertIn('./build.sh test triad --cli', source)
        self.assertIn('--target triad_receiver_probe', source)
        self.assertIn('--probe build/triad-candidate/triad_receiver_probe', source)
        self.assertIn('--baseline build/triad-reference.json', source)
        self.assertIn('tools/triad-ci-reference.py', source)
        self.assertIn('relative_regression_pass:', source)
        self.assertIn('test_triad_abbreviated.py', (ROOT / 'CMakeLists.txt').read_text())
        self.assertIn('timeout-minutes: 20', source)
        self.assertNotRegex(source, r'--(?:full|sustained)|--qualify|pip install|continue-on-error')
        for caller in ('ci.yml', 'release.yml'):
            self.assertIn('./.github/workflows/triad-abbreviated.yml',
                          (WORKFLOWS / caller).read_text())

    def test_native_platforms_build_the_explicit_triad_partition(self):
        for filename in ('native-linux.yml', 'native-windows.yml'):
            source = (WORKFLOWS / filename).read_text()
            self.assertIn('        - triad\n', source)
            self.assertRegex(source, r"fast[|' ,]+legacy[|' ,]+triad")
            self.assertIn('datapump-tests-', source)
            self.assertIn('tools/run-ci-tests.py --build-dir build --scope', source)
        ci = (WORKFLOWS / 'ci.yml').read_text()
        for job in ('linux-tests', 'windows-tests'):
            # The next job starts at exactly two spaces, not a nested property.
            body = re.split(r'\n  [a-zA-Z][\w-]*:', ci.split(f'  {job}:\n', 1)[1], maxsplit=1)[0]
            self.assertIn('        - triad\n', body)

    def test_gpu_workflow_requires_native_cuda_without_cloud_provisioning(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        self.assertIn('workflow_dispatch:', source)
        self.assertNotIn('workflow_call:', source)
        self.assertIn('runs-on: [self-hosted, linux, x64, datapump-cuda]', source)
        self.assertIn('-DDATAPUMP_TRIAD_TENSOR=ON', source)
        self.assertIn('-DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON', source)
        self.assertIn('--target triad_receiver_probe test_triad_tensor_cuda', source)
        self.assertIn("-R '^triad_tensor_cuda$' --no-tests=error", source)
        self.assertIn('--backend cuda --require-accelerator on', source)
        self.assertIn('--device cuda', source)
        self.assertNotRegex(source, r'pip install|continue-on-error|--device cpu|runpodctl')

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_failed_reference_build_is_not_replaced_with_establishment(self):
        script = workflow_run((WORKFLOWS / 'triad-abbreviated.yml').read_text(),
                              'Build same-run native reference')
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            reference_dir = directory / 'triad-reference'
            reference_dir.mkdir()
            build = reference_dir / 'build.sh'
            build.write_text('#!/bin/sh\nexit 23\n')
            build.chmod(0o700)
            commands = directory / 'bin'
            commands.mkdir()
            for command in ('git', 'cmake'):
                path = commands / command
                path.write_text('#!/bin/sh\nexit 0\n')
                path.chmod(0o700)
            result = subprocess.run(['bash', '-e', '-c', script], check=False,
                                    env=os.environ | {'PATH': f'{commands}:{os.environ["PATH"]}',
                                                      'RUNNER_TEMP': str(directory), 'REFERENCE_SHA': 'fixture'},
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 23)


class NativeReference(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.git('init', '--quiet')
        self.git('config', 'user.email', 'native-ci-fixture@example.invalid')
        self.git('config', 'user.name', 'Native CI fixture')
        (self.root / 'README').write_text('pre-port fixture\n')
        self.commit('pre-port')
        self.preport = self.git('rev-parse', 'HEAD')
        self.marker = 'constexpr auto schema = "datapump.triad.native-probe.v1";\n'

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.root), *args], check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()

    def commit(self, message):
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', message)

    def introduce(self, source=None):
        target = self.root / reference.PROBE_SOURCE
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(self.marker if source is None else source)
        self.commit('native protocol')

    def select(self, **kwargs):
        with patch.object(reference, 'APPROVED_PREPORT', self.preport):
            return reference.select_reference(self.root, **kwargs)

    def test_transition_is_bound_to_the_approved_source_revision(self):
        self.assertEqual(reference.APPROVED_PREPORT, '1c58507a5178c7700cd15d0fcdd05a0091c6dba9')
        self.introduce()
        with self.assertRaisesRegex(ValueError, 'baseline unavailable'):
            reference.select_reference(self.root)
        result = self.select()
        self.assertEqual(result['mode'], 'initial_native_establishment')
        self.assertFalse(result['relative_regression_pass'])
        self.assertEqual(result['reference_sha'], self.preport)
        with self.assertRaisesRegex(ValueError, 'baseline unavailable'):
            self.select(reference_ref=self.preport)

    def test_second_native_commit_compares_and_self_reference_is_rejected(self):
        self.introduce()
        native = self.git('rev-parse', 'HEAD')
        (self.root / 'README').write_text('native candidate\n')
        self.commit('candidate')
        result = self.select()
        self.assertEqual(result['mode'], 'compare')
        self.assertEqual(result['reference_sha'], native)
        with self.assertRaisesRegex(ValueError, 'must differ'):
            self.select(reference_ref='HEAD')

    def test_missing_or_malformed_candidate_cannot_bootstrap(self):
        (self.root / 'README').write_text('still no native protocol\n')
        self.commit('missing')
        with self.assertRaisesRegex(ValueError, 'Candidate does not implement'):
            self.select()
        self.introduce('constexpr auto schema = broken;\n')
        with self.assertRaisesRegex(ValueError, 'malformed or unsupported'):
            self.select()

    def test_future_or_malformed_reference_protocol_fails_closed(self):
        self.introduce(self.marker.replace('v1', 'v2'))
        with self.assertRaisesRegex(ValueError, 'malformed or unsupported'):
            self.select(establish_baseline=True)
        self.introduce()
        with self.assertRaisesRegex(ValueError, 'malformed or unsupported'):
            self.select()

    def test_missing_later_reference_does_not_reopen_bootstrap(self):
        (self.root / 'README').write_text('intermediate pre-native commit\n')
        self.commit('not approved transition')
        self.introduce()
        with self.assertRaisesRegex(ValueError, 'baseline unavailable'):
            self.select()

    def test_explicit_manual_establishment_is_distinct_and_no_relative_pass(self):
        self.introduce()
        result = self.select(establish_baseline=True)
        self.assertEqual(result['mode'], 'manual_establishment')
        self.assertFalse(result['relative_regression_pass'])
        self.assertIsNone(result['reference_sha'])
        with self.assertRaisesRegex(ValueError, 'cannot also select'):
            self.select(establish_baseline=True, reference_ref=self.preport)

    def test_cli_preserves_error_and_does_not_emit_a_mode_for_following_steps(self):
        self.introduce()
        output = self.root / 'report.json'
        github_output = self.root / 'github-output'
        result = subprocess.run([os.sys.executable, str(ROOT / 'tools/triad-ci-reference.py'),
                                 '--repository', str(self.root), '--output', str(output),
                                 '--github-output', str(github_output)], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(json.loads(output.read_text())['mode'], 'error')
        self.assertFalse(github_output.exists())


if __name__ == '__main__':
    unittest.main()
