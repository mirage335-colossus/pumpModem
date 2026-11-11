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


def workflow_job_run(source, job, name):
    """Read a literal run block from one job without parsing all of YAML."""
    start = source.split(f'  {job}:\n', 1)[1]
    body = re.split(r'\n  [A-Za-z][\w-]*:\n', start, maxsplit=1)[0]
    return workflow_run(body, name)


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
        self.assertIn("options: [native, '89', '120']", source)
        self.assertIn("options: ['off', fp16, int8, peaks, all]", source)
        self.assertIn("if: inputs.components != 'off'", source)
        self.assertIn('same-binary algorithm comparisons, not a relative-commit regression pass', source)

    def gpu_fixture(self, directory):
        """Record actual workflow argv/env without compiling or invoking a GPU."""
        directory = Path(directory)
        commands = directory / 'bin'
        commands.mkdir()
        reference_dir = directory / 'triad-cuda-reference'
        reference_dir.mkdir()
        libtorch = directory / 'libtorch'
        libtorch.mkdir()
        recorder = f'#!{os.sys.executable}\n' + '''import json, os, sys
from pathlib import Path
with open(os.environ['COMMAND_LOG'], 'a') as output:
    output.write(json.dumps({'name': Path(sys.argv[0]).name, 'argv': sys.argv[1:],
                             'torch_arch': os.environ.get('TORCH_CUDA_ARCH_LIST')}) + '\\n')
if Path(sys.argv[0]).name == 'cmake' and sys.argv[1:] == ['--version']:
    print('cmake version ' + os.environ.get('CMAKE_TEST_VERSION', '3.31.6'))
'''
        for path in (commands / 'cmake', commands / 'git', commands / 'python',
                     directory / 'build.sh', reference_dir / 'build.sh'):
            path.write_text(recorder)
            path.chmod(0o700)
        log = directory / 'commands.jsonl'
        env = os.environ | {'PATH': f'{commands}:{os.environ["PATH"]}',
                            'COMMAND_LOG': str(log), 'RUNNER_TEMP': str(directory),
                            'REFERENCE_SHA': 'fixture', 'LIBTORCH_PREFIX': str(libtorch),
                            'CUDA_ARCHITECTURES': 'native', 'COMPONENTS': 'off',
                            'MATRIX_CASE': '0', 'DEVICE_WORKSPACE_MIB': '2048',
                            'TORCH_CUDA_ARCH_LIST': 'stale-inherited-value'}
        return env, log

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_gpu_candidate_and_reference_use_the_same_validated_architecture(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        names = ('Validate CUDA test selection', 'Build native CUDA receiver and correctness checks',
                 'Build same-run native CUDA reference')
        scripts = [workflow_run(source, name) for name in names]
        for architecture, torch_arch in (('native', None), ('89', '8.9'), ('120', '12.0')):
            with self.subTest(architecture=architecture), tempfile.TemporaryDirectory() as directory:
                env, log = self.gpu_fixture(directory)
                env['CUDA_ARCHITECTURES'] = architecture
                for script in scripts:
                    result = subprocess.run(['bash', '-e', '-c', script], cwd=directory, env=env,
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                rows = [json.loads(line) for line in log.read_text().splitlines()]
                configurations = [row for row in rows if row['name'] == 'build.sh']
                self.assertEqual(len(configurations), 2)
                for row in configurations:
                    self.assertIn(f'-DCMAKE_CUDA_ARCHITECTURES={architecture}', row['argv'])
                    self.assertEqual(row['torch_arch'], torch_arch)
                    self.assertIn('-DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON', row['argv'])

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_gpu_invalid_selection_stops_before_build_or_measurement(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        validation = workflow_run(source, 'Validate CUDA test selection')
        candidate = workflow_run(source, 'Build native CUDA receiver and correctness checks')
        invalid = ({'CUDA_ARCHITECTURES': '86'}, {'CUDA_ARCHITECTURES': '89; echo bad'},
                   {'COMPONENTS': 'cuda'}, {'MATRIX_CASE': '2'},
                   *({'DEVICE_WORKSPACE_MIB': value} for value in ('0', '-1', '1.5', '65537', '1e3', '$(echo bad)')),
                   {'COMPONENTS': 'peaks', 'DEVICE_WORKSPACE_MIB': '4096'},
                   {'COMPONENTS': 'all', 'DEVICE_WORKSPACE_MIB': '1024'})
        for changes in invalid:
            with self.subTest(changes=changes), tempfile.TemporaryDirectory() as directory:
                env, log = self.gpu_fixture(directory)
                result = subprocess.run(['bash', '-e', '-c', validation + candidate], cwd=directory,
                                        env=env | changes, capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(log.exists())
        # Each configure step also rejects unsupported architecture values when
        # extracted independently; the reference must not even create a checkout.
        for name in ('Build native CUDA receiver and correctness checks', 'Build same-run native CUDA reference'):
            with self.subTest(step=name), tempfile.TemporaryDirectory() as directory:
                env, log = self.gpu_fixture(directory)
                result = subprocess.run(['bash', '-e', '-c', workflow_run(source, name)], cwd=directory,
                                        env=env | {'CUDA_ARCHITECTURES': 'invalid'}, capture_output=True, check=False)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(log.exists())

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_gpu_native_architecture_requires_supported_cmake_before_build(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        script = (workflow_run(source, 'Validate CUDA test selection') +
                  workflow_run(source, 'Build native CUDA receiver and correctness checks'))
        for version in ('3.21.7', '3.23.5', 'invalid'):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                env, log = self.gpu_fixture(directory)
                result = subprocess.run(['bash', '-e', '-c', script], cwd=directory,
                                        env=env | {'CMAKE_TEST_VERSION': version}, capture_output=True,
                                        text=True, check=False)
                self.assertEqual(result.returncode, 2)
                self.assertIn('requires CMake 3.24 or newer', result.stderr)
                rows = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual([(row['name'], row['argv']) for row in rows], [('cmake', ['--version'])])

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_gpu_components_use_one_bounded_native_path_and_separate_report(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        validation = workflow_run(source, 'Validate CUDA test selection')
        script = workflow_run(source, 'Measure bounded native CUDA components')
        for component, matrix_case, workspace in (('fp16', '0', '1'), ('int8', '1', '65536'),
                                                  ('peaks', '0', '2048'), ('all', '1', '2048')):
            with self.subTest(component=component), tempfile.TemporaryDirectory() as directory:
                env, log = self.gpu_fixture(directory)
                result = subprocess.run(['bash', '-e', '-c', validation + script], cwd=directory,
                                        env=env | {'COMPONENTS': component, 'MATRIX_CASE': matrix_case,
                                                   'DEVICE_WORKSPACE_MIB': workspace},
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                rows = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual([(row['name'], row['argv']) for row in rows[:-1]], [('cmake', ['--version'])])
                self.assertEqual(rows[-1]['name'], 'python')
                argv = rows[-1]['argv']
                self.assertEqual(argv[0], 'tools/triad_abbreviated.py')
                options = dict(zip(argv[1::2], argv[2::2]))
                self.assertEqual(options['--output'], 'build/triad-cuda-components.json')
                self.assertEqual(options['--probe'], 'build/triad-cuda/triad_receiver_probe')
                self.assertEqual(options['--component-probe'], 'build/triad-cuda/test_triad_tensor')
                self.assertEqual(options['--device'], 'cuda')
                self.assertEqual(options['--components'], component)
                self.assertEqual(options['--device-workspace-mib'], workspace)
                self.assertEqual(options['--benchmark-deadline-ms'], '300000')
                self.assertNotIn('--baseline', options)
                self.assertEqual(options.get('--matrix-case'), None if component == 'peaks' else matrix_case)

    @unittest.skipIf(os.name == 'nt', 'The Linux workflow shell is Bash')
    def test_gpu_component_success_cannot_supply_a_relative_regression_pass(self):
        source = (WORKFLOWS / 'triad-gpu.yml').read_text()
        script = workflow_run(source, 'Record CUDA qualification limits')
        for status, expected in (('baseline_established', 'false'),
                                 ('no_material_slowdown_for_measured_kernels', 'true')):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                build = directory / 'build'
                build.mkdir()
                (build / 'triad-cuda-candidate.json').write_text(json.dumps({'status': status}))
                (build / 'triad-cuda-components.json').write_text(json.dumps({'status': 'component_checks_passed'}))
                summary = directory / 'summary.md'
                # Execute the literal workflow's Python body without assuming a
                # separate "python" command exists on a developer's machine.
                body = script.split("python - <<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
                result = subprocess.run([os.sys.executable, '-c', body], cwd=directory,
                                        env=os.environ | {'REFERENCE_MODE': 'compare', 'COMPONENTS': 'all',
                                                          'GITHUB_STEP_SUMMARY': str(summary)},
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                output = summary.read_text()
                self.assertIn(f'Relative regression pass: **{expected}**', output)
                self.assertIn('Native component status: **component_checks_passed**', output)
                self.assertIn('not a relative-commit regression pass', output)

    def test_vulkan_workflow_separates_hosted_plan_and_opt_in_actual_gpu(self):
        path = WORKFLOWS / 'triad-vulkan.yml'
        self.assertTrue(path.is_file())
        source = path.read_text()
        self.assertIn("  workflow_dispatch:\n", source)
        self.assertRegex(source, r'run_gpu:\n[\s\S]*?type: boolean\n\s+default: false')
        self.assertIn('build-and-plan:', source)
        self.assertIn('runs-on: ubuntu-24.04', source)
        self.assertIn('actual-gpu:', source)
        self.assertIn('if: inputs.run_gpu', source)
        self.assertIn('needs: build-and-plan', source)
        self.assertIn('runs-on: [self-hosted, linux, x64, triad-vulkan]', source)
        self.assertIn('-DDATAPUMP_TRIAD_VULKAN=ON', source)
        self.assertIn('-DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=OFF', source)
        self.assertIn('-DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON', source)
        self.assertIn("-R '^triad_vulkan_plan$' --no-tests=error", source)
        self.assertIn("-R '^triad_vulkan$' --no-tests=error", source)
        self.assertIn('test_triad_vulkan --plan', source)
        self.assertIn('test_triad_vulkan --plan-wide', source)
        self.assertIn('test_triad_vulkan --benchmark', source)
        self.assertIn('test_triad_vulkan --benchmark-wide', source)
        self.assertIn('125s build/triad-vulkan/test_triad_vulkan --benchmark-wide', source)
        self.assertIn('--backend vulkan --require-accelerator on', source)
        self.assertIn('glslangValidator --version', source)
        self.assertNotRegex(source, r'runpodctl|apt-get.*(mesa|vulkan-driver)|continue-on-error|workflow_call:')
        self.assertIn('build/triad-vulkan-device.json', source)
        self.assertIn('build/triad-vulkan-benchmark.jsonl', source)
        self.assertIn('build/triad-vulkan-benchmark-wide.jsonl', source)
        self.assertIn('name: triad-native-vulkan-gpu-wide-${{ github.run_attempt }}', source)

    @unittest.skipIf(os.name == 'nt', 'The Vulkan CI runner shell is Bash')
    def test_vulkan_workflow_commands_build_plan_and_require_gpu_on_self_hosted_job(self):
        source = (WORKFLOWS / 'triad-vulkan.yml').read_text()
        plan_steps = (
            ('build-and-plan', 'Configure without requiring a GPU'),
            ('build-and-plan', 'Build Vulkan backend and CPU plan check'),
            ('build-and-plan', 'Check bounded native search geometry without device execution'),
            ('actual-gpu', 'Configure Vulkan with required actual GPU execution'),
            ('actual-gpu', 'Build native Vulkan receiver tests'),
            ('actual-gpu', 'Require numerical correctness on the actual Vulkan GPU'),
            ('actual-gpu', 'Measure bounded native full frequency search'),
            ('actual-gpu', 'Measure bounded native wide frequency search'),
        )
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            command_bin = directory / 'bin'
            command_bin.mkdir()
            for relative in ('build/triad-vulkan',):
                (directory / relative).mkdir(parents=True)
            recorder = f'#!{os.sys.executable}\n' + '''import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
argv = sys.argv[1:]
with open(os.environ['COMMAND_LOG'], 'a') as output:
    output.write(json.dumps({'name': name, 'argv': argv}) + '\\n')
if name == 'cmake' and argv == ['--version']:
    print('cmake version 3.31.6')
elif name == 'glslangValidator' and argv == ['--version']:
    print('Glslang Version: 15.2.0')
elif name == 'test_triad_vulkan':
    if argv == ['--plan']:
        print(json.dumps({'actual_gpu_work': False, 'workers': 24}))
    elif argv == ['--plan-wide']:
        print(json.dumps({'actual_gpu_work': False, 'workers': 24, 'wide': True}))
    elif argv == ['--benchmark']:
        print(json.dumps({'schema': 'datapump.triad.vulkan-benchmark.v1', 'actual_gpu_work': True}))
    elif argv == ['--benchmark-wide']:
        print(json.dumps({'schema': 'datapump.triad.vulkan-benchmark.v1', 'actual_gpu_work': True, 'wide': True}))
elif name == 'triad_receiver_probe':
    print(json.dumps({'status': 'complete', 'command': 'describe',
                      'identity': {'backend': 'vulkan', 'backend_receipt': {
                          'vulkan': True, 'executed': False, 'device_name': 'fixture AMD Vulkan device'}}}))
'''
            for name in ('cmake', 'ctest', 'glslangValidator'):
                target = command_bin / name
                target.write_text(recorder)
                target.chmod(0o700)
            for name in ('build.sh', 'build/triad-vulkan/test_triad_vulkan',
                         'build/triad-vulkan/triad_receiver_probe'):
                target = directory / name
                target.write_text(recorder)
                target.chmod(0o700)
            log = directory / 'commands.jsonl'
            environment = os.environ | {'PATH': f'{command_bin}:{os.environ["PATH"]}',
                                        'COMMAND_LOG': str(log)}
            for job, step in plan_steps:
                with self.subTest(job=job, step=step):
                    script = workflow_job_run(source, job, step)
                    result = subprocess.run(['bash', '-e', '-c', script], cwd=directory,
                                            env=environment, capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
            rows = [json.loads(line) for line in log.read_text().splitlines()]
            vulkan_configures = [row for row in rows if row['name'] == 'build.sh']
            self.assertEqual(len(vulkan_configures), 2)
            self.assertIn('-DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=OFF', vulkan_configures[0]['argv'])
            self.assertIn('-DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON', vulkan_configures[1]['argv'])
            checks = [row for row in rows if row['name'] == 'ctest']
            self.assertEqual(len(checks), 2)
            self.assertEqual(checks[0]['argv'], ['--test-dir', 'build/triad-vulkan', '--output-on-failure',
                                                '-R', '^triad_vulkan_plan$', '--no-tests=error'])
            self.assertEqual(checks[1]['argv'], ['--test-dir', 'build/triad-vulkan', '--output-on-failure',
                                                '-R', '^triad_vulkan$', '--no-tests=error'])
            plans = [row for row in rows if row['name'] == 'test_triad_vulkan']
            self.assertEqual([row['argv'] for row in plans],
                             [['--plan'], ['--plan-wide'], ['--benchmark'], ['--benchmark-wide']])
            preflight = workflow_job_run(source, 'actual-gpu', 'Confirm preinstalled Vulkan toolchain')
            self.assertIn('glslangValidator --version', preflight)
            self.assertIn('/dev/dri/renderD128', preflight)
            self.assertIn('timeout --signal=TERM --kill-after=5s 125s',
                          workflow_job_run(source, 'actual-gpu', 'Measure bounded native full frequency search'))
            wide_benchmark = workflow_job_run(source, 'actual-gpu', 'Measure bounded native wide frequency search')
            self.assertIn('timeout --signal=TERM --kill-after=5s 125s', wide_benchmark)
            self.assertIn('test_triad_vulkan --benchmark-wide > build/triad-vulkan-benchmark-wide.jsonl',
                          wide_benchmark)
            plan = workflow_job_run(source, 'build-and-plan',
                                    'Check bounded native search geometry without device execution')
            self.assertIn('test_triad_vulkan --plan-wide > build/triad-vulkan-plan-wide.json', plan)

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
