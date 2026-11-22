import contextlib
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import types
import unittest
from unittest.mock import patch

import deploy


class DeployTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.known = self.root / 'known_hosts'
        self.known.write_text('example ssh-ed25519 AAAA\n')
        self.calls = []

    def bundle(self, root, dest):
        dest.write_bytes(b'test archive')

    def runner(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        return types.SimpleNamespace(returncode=0)

    def test_commands_are_fixed_authenticated_and_hash_bound(self):
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle):
            result = deploy.deploy('pod.example.com', 'root', 2244, self.known,
                                   mode='cuda', runner=self.runner)
        self.assertEqual(result, '/workspace/pump-lab/build/lab/datapump-worker')
        self.assertEqual(len(self.calls), 1)
        command = self.calls[0][0]
        self.assertIn('StrictHostKeyChecking=yes', command)
        self.assertIn('UserKnownHostsFile=' + str(self.known), command)
        self.assertIn('BatchMode=yes', command)
        self.assertEqual(command[1:3], ['-F', '/dev/null'])
        digest = hashlib.sha256(b'test archive').hexdigest()
        remote = shlex.split(command[-1])
        self.assertEqual(remote[:2], ['python3', '-c'])
        self.assertEqual(remote[-4:], [digest, 'cuda', 'simulation', '12'])
        self.assertIn(deploy.REMOTE_DEPLOY, remote[2])
        self.assertEqual(self.calls[0][1]['input'], b'test archive')

    def test_live_profile_command_and_path(self):
        digest = hashlib.sha256(b'test archive').hexdigest()
        for mode in ('cuda', 'vulkan'):
            with self.subTest(mode=mode), patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle):
                result = deploy.deploy('pod.example.com', 'root', 22, self.known,
                                       mode=mode, profile='live', runner=self.runner)
                self.assertEqual(result, '/workspace/pump-lab-live/build/lab/datapump-lab-worker')
                self.assertEqual(shlex.split(self.calls[-1][0][-1])[-4:], [digest, mode, 'live', '12'])

    def test_invalid_connection_and_mode_never_execute(self):
        for host in ('-oProxyCommand=evil', 'bad;touch', 'a/b', 'bad:22', 'foo\nbar'):
            with self.subTest(host=host), self.assertRaises(ValueError):
                deploy.deploy(host, 'root', 22, self.known, runner=self.runner)
        for user in ('root;evil', '-x', 'u@h', 'a b'):
            with self.subTest(user=user), self.assertRaises(ValueError):
                deploy.deploy('127.0.0.1', user, 22, self.known, runner=self.runner)
        for port in (0, 65536, True, '22'):
            with self.subTest(port=port), self.assertRaises(ValueError):
                deploy.deploy('127.0.0.1', 'root', port, self.known, runner=self.runner)
        with self.assertRaises(ValueError):
            deploy.deploy('127.0.0.1', 'root', 22, self.known, mode='cuda;bad', runner=self.runner)
        with self.assertRaises(ValueError):
            deploy.deploy('127.0.0.1', 'root', 22, self.known, profile='live;bad', runner=self.runner)
        token_path = self.root / 'known%h'
        token_path.write_text('irrelevant')
        with self.assertRaises(ValueError):
            deploy.deploy('example.com', 'root', 22, token_path, runner=self.runner)
        dollar_path = self.root / 'known${USER}'
        dollar_path.write_text('irrelevant')
        with self.assertRaises(ValueError):
            deploy.deploy('example.com', 'root', 22, dollar_path, runner=self.runner)
        self.assertEqual(self.calls, [])

    def test_bounded_process_drains_output_and_times_out(self):
        code, tail = deploy._bounded_process([sys.executable, '-c',
            "import sys; data=sys.stdin.buffer.read(); sys.stdout.buffer.write(b'x'*2000000+b'END'+str(len(data)).encode())"],
            b'z'*100000, 10)
        self.assertEqual(code, 0)
        self.assertLessEqual(len(tail), 4096)
        self.assertTrue(tail.endswith(b'END100000'))
        with self.assertRaises(deploy.DeployError) as broken:
            deploy._bounded_process([sys.executable, '-c', 'pass'], b'z' * 2000000, 10)
        self.assertTrue(broken.exception.uncertain)
        code, tail = deploy._bounded_process([sys.executable, '-c',
            "import sys; print('Deployment is already running'); sys.exit(1)"], b'z' * 2000000, 10)
        self.assertEqual(code, 1)
        self.assertIn(b'Deployment is already running', tail)
        with self.assertRaises(subprocess.TimeoutExpired):
            deploy._bounded_process([sys.executable, '-c', 'import time; time.sleep(5)'], None, .1)

    def test_progress_allowlist_rejects_partial_and_arbitrary_output(self):
        statuses = []
        read_progress = deploy._stage_reader(statuses.append)
        read_progress(b'PUMP_LAB_STAGE=up')
        read_progress(b'load\r\nPUMP_LAB_STAGE=upload\nPUMP_LAB_STAGE=private value\n')
        read_progress(b'x' * 1024)
        read_progress(b'PUMP_LAB_STAGE=compile\nPUMP_LAB_STAGE=configure\nPUMP_LAB_STAGE=check')
        self.assertEqual(statuses, [deploy._STAGE_MESSAGES[b'upload'], deploy._STAGE_MESSAGES[b'configure']])

        statuses.clear()
        read_progress = deploy._stage_reader(statuses.append)
        read_progress(b'PUMP_LAB_BUILD_JOBS=')
        read_progress(b'16\nPUMP_LAB_STAGE=com')
        read_progress(b'pile\n')
        self.assertEqual(statuses, [deploy._STAGE_MESSAGES[b'compile'] + ' (16 parallel jobs)'])
        statuses.clear()
        read_progress = deploy._stage_reader(statuses.append)
        read_progress(b'PUMP_LAB_BUILD_JOBS=99999\nPUMP_LAB_STAGE=compile\n')
        self.assertEqual(statuses, [deploy._STAGE_MESSAGES[b'compile']])

    def test_progress_is_delivered_before_remote_build_finishes(self):
        destination = self.root / 'managed'
        script = deploy.REMOTE_DEPLOY.replace("directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'",
                                             'directory = ' + repr(str(destination)))
        build = '''set -eu
echo PUMP_LAB_STAGE=compile
python3 -c "import time; from pathlib import Path; gate=Path('ready'); exec('while not gate.exists(): time.sleep(.01)')"
mkdir -p build/lab
printf '#!/bin/sh\nexit 0\n' > build/lab/datapump-worker
chmod +x build/lab/datapump-worker
echo PUMP_LAB_MODE=cpu
'''
        code = 'BUILD = ' + repr(build) + "\nRESOURCES = ''\n" + script
        data = b'archive'
        digest = hashlib.sha256(data).hexdigest()
        stages = []

        def progress(message):
            stages.append(message)
            if message == deploy._STAGE_MESSAGES[b'compile']:
                (destination / 'ready').touch()

        status, tail = deploy._bounded_process(
            [sys.executable, '-c', code, digest, 'cpu', 'simulation', str(len(data))],
            data, 4, on_progress=progress)
        self.assertEqual(status, 0, tail)
        self.assertEqual(stages, [deploy._STAGE_MESSAGES[b'upload'], deploy._STAGE_MESSAGES[b'compile']])

    def test_deployment_cancelled_before_ssh_and_during_remote_build(self):
        cancelled = threading.Event()
        cancelled.set()
        with patch.object(deploy.bundle, 'create_bundle') as bundle:
            with self.assertRaisesRegex(deploy.DeployError, 'cancelled before starting SSH'):
                deploy.deploy('pod.example.com', 'root', 22, self.known, runner=self.runner,
                              cancelled=cancelled)
        bundle.assert_not_called()
        self.assertEqual(self.calls, [])
        cancelled.clear()

        def cancel_on_compile(message):
            self.assertEqual(message, deploy._STAGE_MESSAGES[b'compile'])
            cancelled.set()

        with self.assertRaises(deploy.DeployError) as stopped:
            deploy._bounded_process([sys.executable, '-u', '-c',
                                    "import time; print('PUMP_LAB_STAGE=compile'); time.sleep(10)"],
                                    None, 3, on_progress=cancel_on_compile, cancelled=cancelled)
        self.assertTrue(stopped.exception.uncertain)

    def test_timeout_and_disconnect_are_uncertain(self):
        def timeout(*args, **kwargs):
            raise subprocess.TimeoutExpired(args[0], 1)
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle):
            with self.assertRaises(deploy.DeployError) as caught:
                deploy.deploy('example.com', 'root', 22, self.known, runner=timeout)
            self.assertTrue(caught.exception.uncertain)
            with self.assertRaises(deploy.DeployError) as caught:
                deploy.deploy('example.com', 'root', 22, self.known,
                              runner=lambda *a, **kw: types.SimpleNamespace(returncode=255))
            self.assertTrue(caught.exception.uncertain)

    def test_deployment_stages_share_one_deadline(self):
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle), \
             patch.object(deploy.time, 'monotonic', side_effect=[100, 101]):
            deploy.deploy('pod.example.com', 'root', 22, self.known, runner=self.runner, timeout=10)
        self.assertEqual([call[1]['timeout'] for call in self.calls], [9])

        self.calls.clear()
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle), \
             patch.object(deploy.time, 'monotonic', side_effect=[100, 110]), \
             self.assertRaisesRegex(deploy.DeployError, 'deadline'):
            deploy.deploy('pod.example.com', 'root', 22, self.known, runner=self.runner, timeout=10)
        self.assertEqual(len(self.calls), 0)

    def test_remote_build_modes_execute_correct_flags(self):
        # Exercise the actual POSIX script with fake CMake and fake Torch. This
        # checks branching/arguments, not CUDA availability or a real build.
        cases = itertools.product(('cpu', 'cuda', 'vulkan', 'gpu'), ('simulation', 'live'),
                                  ('ninja', 'make', 'existing-make'))
        for requested, profile, generator_case in (*cases, ('cuda-unavailable', 'simulation', 'ninja'),
                                                    ('gpu-no-icd', 'simulation', 'ninja')):
            cuda_unavailable = requested == 'cuda-unavailable'
            no_nvidia_icd = requested == 'gpu-no-icd'
            mode = 'cuda' if cuda_unavailable else 'gpu' if no_nvidia_icd else requested
            with self.subTest(mode=requested, profile=profile, generator=generator_case):
                home = self.root / (requested + '-' + profile + '-' + generator_case)
                home.mkdir(mode=0o700)
                archive = home / 'source.tar.gz'
                sizing = (b"import sys\nassert sys.argv[1] == '--runpod'\n"
                          b"print({'cpu': 16, 'vulkan': 12, 'cuda': 8}[sys.argv[2]])\n")
                with tarfile.open(archive, 'w:gz') as tar:
                    info = tarfile.TarInfo('source-file')
                    info.size = 1
                    tar.addfile(info, io.BytesIO(b'x'))
                    info = tarfile.TarInfo('tools/build_capacity.py')
                    info.size = len(sizing)
                    tar.addfile(info, io.BytesIO(sizing))
                digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                bin_dir = home / 'bin'
                bin_dir.mkdir()
                cmake = bin_dir / 'cmake'
                cmake.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$CMAKE_LOG"
if [ "$1" = -S ]; then
  mkdir -p build/lab
  generator="Unix Makefiles"
  previous=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' build/lab/CMakeCache.txt 2>/dev/null | sed -n '1p')
  [ -z "$previous" ] || generator=$previous
  while [ "$#" -gt 0 ]; do
    if [ "$1" = -G ]; then
      shift
      generator=$1
    fi
    shift
  done
  printf 'CMAKE_GENERATOR:INTERNAL=%s\\nCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\\n' "$generator" > build/lab/CMakeCache.txt
  if [ "$FAKE_GENERATOR" = make ]; then
    printf 'Compiler: \\nC++ launcher: \\n' > build/lab/build-info.txt
  else
    printf 'Compiler: GNU 14.2.0\\nC++ launcher: /usr/bin/ccache\\n' > build/lab/build-info.txt
  fi
fi
if [ "$1" = --build ]; then
  mkdir -p build/lab
  for arg do worker=$arg; done
  printf '#!/bin/sh\\necho no-socket-boundary\\n' > "build/lab/$worker"
  chmod +x "build/lab/$worker"
fi
''')
                cmake.chmod(0o755)
                (bin_dir / 'ninja').write_text('#!/bin/sh\\nexit 0\\n')
                (bin_dir / 'ninja').chmod(0o755)
                torch = home / 'torch'
                torch.mkdir()
                (torch / '__init__.py').write_text('''__version__ = "fake"
class cuda:
 @staticmethod
 def is_available(): return True
 @staticmethod
 def get_device_name(): return "fake GPU"
 @staticmethod
 def get_device_properties(index):
  class properties:
   total_memory = 12345678
   uuid = "MIG-fake-uuid"
  return properties()
 @staticmethod
 def get_device_capability(): return (12, 0)
class version: cuda = "fake"
def compiled_with_cxx11_abi(): return True
''')
                (torch / 'utils.py').write_text('cmake_prefix_path = "/fake/torch"\n')
                log = home / 'cmake.log'
                for name in ('sh', 'tar', 'gzip', 'sha256sum', 'chmod', 'mkdir', 'dirname', 'readlink', 'timeout', 'sed', 'echo'):
                    (bin_dir / name).symlink_to(shutil.which(name))
                (bin_dir / 'python3').symlink_to(sys.executable)
                # A retry's prior CMake cache survives extraction. Seed it
                # immediately after the mocked source unpack, matching that
                # remote lifecycle rather than creating it before upload.
                tar = bin_dir / 'tar'
                tar.unlink()
                tar.write_text('''#!/bin/sh
/bin/tar "$@"
if [ "${FAKE_GENERATOR:-}" = existing-make ]; then
  mkdir -p build/lab
  printf 'CMAKE_GENERATOR:INTERNAL=Unix Makefiles\\n' > build/lab/CMakeCache.txt
fi
''')
                tar.chmod(0o755)
                if generator_case == 'make':
                    (bin_dir / 'ninja').unlink()
                env = {**os.environ, 'PATH': str(bin_dir),
                       'PYTHONPATH': str(home), 'CMAKE_LOG': str(log),
                       'FAKE_GENERATOR': generator_case}
                toolkit = home / 'toolkit'
                nvcc = toolkit / 'bin' / 'nvcc'
                for key in ('CUDACXX', 'CUDA_HOME', 'CUDA_PATH', 'CUDAToolkit_ROOT'):
                    env.pop(key, None)
                # Explicit Vulkan also works without a CUDA toolkit. The
                # remaining Vulkan cases make CUDA discovery viable.
                if mode != 'cpu' and not (mode == 'vulkan' and generator_case == 'make'):
                    nvcc.parent.mkdir(parents=True)
                    nvcc.touch(mode=0o755)
                    if profile == 'live':
                        env['CUDACXX'] = str(nvcc)
                cuda_probe_unavailable = mode == 'gpu' and profile == 'simulation'
                vulkan = mode == 'vulkan' or cuda_probe_unavailable
                if mode != 'cpu':
                    # Stand in for the compiled CUDA probe. Its discovery result
                    # decides the branch; building the worker is still simulated.
                    compiler = bin_dir / 'c++'
                    compiler.write_text("#!/bin/sh\nfor arg do output=$arg; done\n"
                                        "printf '#!/bin/sh\\nexit %d\\n' \"$PROBE_STATUS\" > \"$output\"\n"
                                        'chmod +x "$output"\n')
                    compiler.chmod(0o755)
                    env['PROBE_STATUS'] = '2' if cuda_probe_unavailable or cuda_unavailable else '0'
                if vulkan:
                    source = home / 'nvidia_icd.json'
                    alternate_source = home / 'nvidia_icd_alternate.json'
                    if not no_nvidia_icd:
                        source.write_text(json.dumps({'ICD': {'library_path': 'libGLX_nvidia.so.0',
                                                                'api_version': '1.4.312'}}))
                    prepare = (Path(deploy.__file__).parent / 'prepare_vulkan.sh').read_text()
                    prepare = prepare.replace('/etc/os-release', '/dev/null')
                    prepare = prepare.replace(
                        "sources = (Path('/etc/vulkan/icd.d/nvidia_icd.json'), Path('/usr/share/vulkan/icd.d/nvidia_icd.json'))",
                        f'sources = (Path({str(source)!r}), Path({str(alternate_source)!r}))')
                    (home / '_zLab').mkdir()
                    (home / '_zLab/prepare_vulkan.sh').write_text(prepare)
                    loader = home / 'libOpenCL.so.1'
                    if generator_case != 'existing-make':
                        loader.touch()
                    dpkg_query = bin_dir / 'dpkg-query'
                    dpkg_query.write_text('''#!/bin/sh
for arg do package=$arg; done
[ "$package" = ocl-icd-libopencl1:amd64 ] && [ "$FAKE_GENERATOR" != make ] && printf installed
''')
                    dpkg_query.chmod(0o755)
                script = deploy.REMOTE_BUILD.replace('cd "$directory"', 'directory=' + shlex.quote(str(home)) + '\ncd "$directory"', 1)
                # Model the image layout in the test's private directory.
                script = script.replace('/usr/local/cuda/bin/nvcc', shlex.quote(str(nvcc)))
                if vulkan:
                    script = script.replace('/usr/lib/x86_64-linux-gnu/libOpenCL.so.1', shlex.quote(str(loader)))
                (home / '.deployment.json').write_text(json.dumps(
                    {'source': digest, 'mode': mode, 'profile': profile,
                     'builder': hashlib.sha256(script.encode()).hexdigest()}))
                wrapper = deploy.REMOTE_DEPLOY.replace(
                    "directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'",
                    'directory = ' + repr(str(home)))
                code = 'BUILD = ' + repr(script) + '\nRESOURCES = ' + repr(deploy.RESOURCE_RECEIPT) + '\n' + wrapper
                data = archive.read_bytes()
                run = subprocess.run([sys.executable, '-c', code, digest, mode, profile, str(len(data))],
                                     input=data, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=20)
                output = run.stdout.decode()
                if cuda_unavailable:
                    self.assertNotEqual(run.returncode, 0, output)
                    self.assertIn("CUDA device discovery failed inside the worker's no-socket boundary", output)
                    self.assertNotIn('PUMP_LAB_STAGE=configure', output)
                    self.assertFalse(log.exists())
                    self.assertFalse((home / '.worker-receipt.json').exists())
                    continue
                self.assertEqual(run.returncode, 0, output)
                expected_stages = ['upload', 'unpack', 'environment']
                if vulkan:
                    expected_stages.append('dependencies')
                expected_stages.extend(('configure', 'build-plan', 'compile', 'check'))
                self.assertEqual([line.partition('=')[2] for line in output.splitlines()
                                  if line.startswith('PUMP_LAB_STAGE=')], expected_stages)
                flags = log.read_text()
                backend = 'vulkan' if vulkan else 'cuda' if mode == 'gpu' else mode
                jobs = {'cpu': 16, 'vulkan': 12, 'cuda': 8}[backend]
                selected_generator = 'Unix Makefiles' if generator_case != 'ninja' else 'Ninja'
                self.assertIn(f'-G {selected_generator}', flags)
                self.assertIn(f'--build build/lab --parallel {jobs} --target ', flags)
                self.assertIn('-DDATAPUMP_COMPILER_CACHE=ON', flags)
                self.assertIn(f'PUMP_LAB_BUILD_JOBS={jobs}\n', output)
                self.assertIn(f'PUMP_LAB_BUILD_GENERATOR={selected_generator}', output)
                self.assertIn('PUMP_LAB_CXX_COMPILER=/usr/bin/c++', output)
                if generator_case == 'make':
                    self.assertIn('PUMP_LAB_CXX_COMPILER_ID=unknown', output)
                    self.assertIn('PUMP_LAB_CXX_COMPILER_VERSION=unknown', output)
                    self.assertIn('PUMP_LAB_COMPILER_CACHE=unavailable-or-disabled', output)
                else:
                    self.assertIn('PUMP_LAB_CXX_COMPILER_ID=GNU', output)
                    self.assertIn('PUMP_LAB_CXX_COMPILER_VERSION=14.2.0', output)
                    self.assertIn('PUMP_LAB_COMPILER_CACHE=ccache', output)
                for stage in ('configure', 'compile'):
                    metric = next(line for line in output.splitlines()
                                  if line.startswith(f'PUMP_LAB_MEASURE={stage} '))
                    self.assertRegex(metric, r'wall_seconds=[0-9.]+ child_user_cpu_seconds=[0-9.]+ '
                                     r'child_system_cpu_seconds=[0-9.]+ child_peak_rss_bytes=[0-9]+')
                receipt = json.loads((home / '.worker-receipt.json').read_text())
                self.assertEqual(receipt['request']['mode'], mode)
                self.assertEqual(receipt['backend'], backend)
                if mode == 'vulkan':
                    self.assertFalse((home / 'build/lab/probe-cuda-worker').exists())
                self.assertIn(f'PUMP_LAB_BUILD_JOBS={jobs}', receipt['text'].splitlines())
                self.assertIn(f'PUMP_LAB_BUILD_GENERATOR={selected_generator}', receipt['text'].splitlines())
                self.assertTrue(any(line.startswith('PUMP_LAB_MEASURE=configure ')
                                    for line in receipt['text'].splitlines()))
                self.assertIn('PUMP_LAB_MODE=' + backend, output)
                self.assertIn('PUMP_LAB_PROFILE=' + profile, output)
                self.assertIn('PUMP_LAB_RECEIPT_SCOPE=deployment SSH process; worker execution not established', output)
                self.assertIn('PUMP_LAB_CPU_MODEL=', output)
                self.assertIn('PUMP_LAB_CPU_MODEL_SCOPE=first reported processor', output)
                self.assertRegex(output, r'PUMP_LAB_KERNEL_MEMORY_TOTAL_BYTES=(?:[0-9]+|unavailable)')
                self.assertIn('PUMP_LAB_CGROUP_SCOPE=visible ancestors only; hidden limits may be tighter', output)
                self.assertRegex(output, r'PUMP_LAB_CPU_AFFINITY_LOGICAL=(?:[0-9]+|unavailable)')
                self.assertRegex(output, r'PUMP_LAB_CGROUP_MEMORY_LIMIT_BYTES=(?:[0-9]+|unavailable|unlimited)')
                self.assertRegex(output, r'PUMP_LAB_CGROUP_CPU_QUOTA_CORES=(?:[0-9/]+|unavailable|unlimited)')
                if profile == 'live':
                    self.assertIn('-DCMAKE_PROJECT_DataPump_INCLUDE=' + str(home) + '/_zLab/audio/inject.cmake', flags)
                    self.assertIn('--target datapump-lab-worker', flags)
                else:
                    self.assertNotIn('CMAKE_PROJECT_DataPump_INCLUDE', flags)
                    self.assertIn('--target datapump-worker', flags)
                self.assertRegex(output, r'PUMP_LAB_WORKER_SHA256=[0-9a-f]{64}')
                if backend == 'cuda':
                    self.assertIn('PUMP_LAB_DEVICE=fake GPU', output)
                    self.assertIn('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=12345678', output)
                    self.assertIn('PUMP_LAB_GPU_VISIBLE_UUID=MIG-fake-uuid', output)
                    self.assertIn('PUMP_LAB_GPU_PARTITION_STATUS=MIG indicated by UUID', output)
                    self.assertIn('PUMP_LAB_GPU_INVENTORY=', output)
                    self.assertIn('PUMP_LAB_TORCH=fake', output)
                    self.assertIn('PUMP_LAB_CUDA=fake', output)
                    self.assertIn('-DDATAPUMP_TRIAD_TENSOR=ON', flags)
                    self.assertIn('-DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON', flags)
                    self.assertIn('-DCMAKE_CUDA_ARCHITECTURES=120', flags)
                    self.assertIn('-DCMAKE_CUDA_COMPILER=' + str(nvcc), flags)
                    self.assertIn('-DCUDA_TOOLKIT_ROOT_DIR=' + str(toolkit), flags)
                    self.assertIn('-DCUDAToolkit_ROOT=' + str(toolkit), flags)
                elif backend == 'vulkan':
                    self.assertIn('-DDATAPUMP_TRIAD_VULKAN=ON', flags)
                    self.assertIn('-DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON', flags)
                    self.assertIn('-DDATAPUMP_TRIAD_TENSOR=OFF', flags)
                    expected_loader = str(loader) if generator_case == 'ninja' else ''
                    self.assertIn('-DDATAPUMP_TRIAD_OPENCL_LOADER_PATH=' + expected_loader + ' ', flags)
                    manifest = home / 'build/lab/nvidia-headless-icd.json'
                    vendors = manifest.parent / 'nvidia-opencl-vendors'
                    if no_nvidia_icd:
                        self.assertFalse(manifest.exists())
                        self.assertFalse(vendors.exists())
                        strict = subprocess.run(['sh', '_zLab/prepare_vulkan.sh'], cwd=home, env=env,
                                                 capture_output=True, timeout=5)
                        self.assertNotEqual(strict.returncode, 0)
                        self.assertIn(b'Missing NVIDIA Vulkan ICD manifest', strict.stderr)
                        optional_env = dict(env, PUMP_LAB_OPTIONAL_NVIDIA_ICD='1')
                        optional = subprocess.run(['sh', '_zLab/prepare_vulkan.sh'], cwd=home,
                                                  env=optional_env, capture_output=True, timeout=5)
                        self.assertEqual(optional.returncode, 0, optional.stderr)
                    else:
                        self.assertEqual(manifest.stat().st_mode & 0o777, 0o600)
                        self.assertEqual(json.loads(manifest.read_text())['ICD'],
                                         {'library_path': 'libEGL_nvidia.so.0', 'api_version': '1.4.312'})
                        self.assertEqual(vendors.stat().st_mode & 0o777, 0o700)
                        self.assertEqual((vendors / 'nvidia.icd').read_text(), 'libnvidia-opencl.so.1\n')
                        self.assertEqual((vendors / 'nvidia.icd').stat().st_mode & 0o777, 0o600)
                        original = manifest.read_bytes()
                        resumed = subprocess.run(['sh', '_zLab/prepare_vulkan.sh'], cwd=home, env=env,
                                                 capture_output=True, timeout=5)
                        self.assertEqual(resumed.returncode, 0, resumed.stderr)
                        self.assertEqual(manifest.read_bytes(), original)
                        source.write_text(json.dumps({'ICD': {'library_path': 'libGLX_nvidia.so.0',
                                                                'api_version': '1.4.313'}}))
                        incompatible = subprocess.run(['sh', '_zLab/prepare_vulkan.sh'], cwd=home, env=env,
                                                      capture_output=True, timeout=5)
                        self.assertNotEqual(incompatible.returncode, 0)
                        self.assertIn(b'manifest does not match', incompatible.stderr)
                        self.assertEqual(manifest.read_bytes(), original)
                        self.assertEqual(list(manifest.parent.glob('.icd-*')), [])
                else:
                    self.assertIn('PUMP_LAB_DEVICE=CPU (processor not identified)', output)
                    self.assertIn('PUMP_LAB_GPU_SCOPE=not requested', output)
                    self.assertIn('PUMP_LAB_TORCH=not requested', output)
                    self.assertIn('-DDATAPUMP_TRIAD_TENSOR=OFF', flags)
                    self.assertNotIn('REQUIRE_CUDA', flags)

    def test_runtime_cgroup_v2_hierarchical_limits(self):
        namespace = {'__name__': 'receipt_test'}
        exec(deploy.RESOURCE_RECEIPT, namespace)
        proc = self.root / 'proc'
        (proc / 'self').mkdir(parents=True)
        (proc / 'meminfo').write_text('MemTotal: 12345 kB\n')
        self.assertEqual(namespace['memory_total'](proc), str(12345 * 1024))
        group = self.root / 'cgroup with space'
        child = group / 'parent' / 'child'
        child.mkdir(parents=True)
        mount = str(group).replace(' ', r'\040')
        (proc / 'self/cgroup').write_text('0::/parent/child\n')
        (proc / 'self/mountinfo').write_text(
            f'1 0 0:1 / {mount} rw - cgroup2 cgroup rw\n')
        for directory, mem, cpu in ((group, '1000', '200000 100000'),
                                     (group / 'parent', '400', 'max 100000'),
                                     (child, '700', '75000 100000')):
            (directory / 'memory.max').write_text(mem)
            (directory / 'cpu.max').write_text(cpu)
        self.assertEqual(namespace['cgroup_limit'](proc, 'memory'), '400')
        self.assertEqual(namespace['cgroup_limit'](proc, 'cpu'), '3/4')
        (group / 'parent' / 'memory.max').unlink()
        self.assertEqual(namespace['cgroup_limit'](proc, 'memory'), 'unavailable')
        for directory in (group, group / 'parent', child):
            (directory / 'memory.max').write_text('max')
        self.assertEqual(namespace['cgroup_limit'](proc, 'memory'), 'unlimited')

    def test_vulkan_system_icd_lookup_follows_regular_symlinks_and_skips_nonregular_paths(self):
        original = (Path(deploy.__file__).parent / 'prepare_vulkan.sh').read_text()
        source_pair = "sources = (Path('/etc/vulkan/icd.d/nvidia_icd.json'), Path('/usr/share/vulkan/icd.d/nvidia_icd.json'))"
        optional_env = dict(os.environ, PUMP_LAB_OPTIONAL_NVIDIA_ICD='1')
        content = json.dumps({'ICD': {'library_path': 'libGLX_nvidia.so.0',
                                      'api_version': '1.4.312'}})

        for name in ('regular-symlink', 'nonregular-alternate', 'only-nonregular'):
            with self.subTest(case=name):
                root = self.root / name
                root.mkdir()
                first, second = root / 'system-first.json', root / 'system-second.json'
                source = original.replace('/etc/os-release', '/dev/null')
                source = source.replace(source_pair,
                                        f'sources = (Path({str(first)!r}), Path({str(second)!r}))')
                script = root / 'prepare_vulkan.sh'
                script.write_text(source)
                if name == 'regular-symlink':
                    target = root / 'installed-driver-manifest.json'
                    target.write_text(content)
                    first.symlink_to(target)
                else:
                    first.mkdir()
                    if name == 'nonregular-alternate':
                        second.write_text(content)

                result = subprocess.run(['sh', str(script)], cwd=root, env=optional_env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
                if name == 'only-nonregular':
                    self.assertNotEqual(result.returncode, 0, result.stdout.decode())
                    self.assertIn(b'Unsafe or unreadable NVIDIA Vulkan ICD manifest', result.stdout)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout.decode())
                    manifest = root / 'build/lab/nvidia-headless-icd.json'
                    self.assertEqual(json.loads(manifest.read_text())['ICD']['api_version'], '1.4.312')

    def test_vulkan_packages_use_signed_indices_with_base_universe_as_fallback(self):
        os_release = self.root / 'os-release'
        os_release.write_text('ID=ubuntu\nVERSION_CODENAME=noble\n')
        icd = self.root / 'nvidia_icd.json'
        icd.write_text(json.dumps({'ICD': {'library_path': 'libGLX_nvidia.so.0',
                                          'api_version': '1.4.312'}}))
        bin_dir = self.root / 'bin'
        bin_dir.mkdir()
        for command, body in (
            ('dpkg', 'printf \'%s\\n\' "${LAB_TEST_ARCH:-amd64}"\n'),
            ('dpkg-query', '''for arg do package=$arg; done
[ "$package" = ocl-icd-libopencl1:amd64 ] && [ "$LAB_TEST_APT_SCENARIO" = opencl-present ] && printf installed
'''),
            ('apt-get', r'''
set -eu
while [ "$1" = -o ]; do
    case "$2" in
        Dir::Etc::sourcelist=*) source=${2#*=} ;;
        Dir::State::lists=*) lists=${2#*=} ;;
    esac
    shift 2
done
printf '%s\n' "$*"
cat "$source"
case "$LAB_TEST_APT_SCENARIO:$1" in
    fallback*:--simulate|ninja-fallback:--simulate|update-fails:update) exit 100 ;;
esac
case "$LAB_TEST_APT_SCENARIO:$1:$(cat "$source")" in
    mirror-fallback:update:*https://archive.ubuntu.com/ubuntu*) exit 100 ;;
esac
if [ "$1" = update ]; then
    if [ -e "$lists/test-updated" ]; then
        if [ "$LAB_TEST_APT_SCENARIO" = fallback-update-fails ]; then exit 100; fi
        echo 'Reusing downloaded lists'
    fi
    touch "$lists/test-updated"
fi
'''),
        ):
            helper = bin_dir / command
            helper.write_text('#!/bin/sh\n' + body)
            helper.chmod(0o755)
        for command in ('sh', 'mktemp', 'chmod', 'mkdir', 'rm', 'cat', 'touch', 'timeout'):
            (bin_dir / command).symlink_to(shutil.which(command))
        (bin_dir / 'python3').symlink_to(sys.executable)
        script = (Path(deploy.__file__).parent / 'prepare_vulkan.sh').read_text()
        script = script.replace('/etc/os-release', shlex.quote(str(os_release)))
        script = script.replace(
            "sources = (Path('/etc/vulkan/icd.d/nvidia_icd.json'), Path('/usr/share/vulkan/icd.d/nvidia_icd.json'))",
            f'sources = (Path({str(icd)!r}), Path({str(self.root / "alternate_nvidia_icd.json")!r}))')
        signed_source = ('deb [arch=amd64 target=Packages signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] '
                         'https://archive.ubuntu.com/ubuntu ')
        fast_sources = [signed_source + suite for suite in
                        ('noble main', 'noble-updates main universe', 'noble-security main universe')]
        full_sources = [*fast_sources, signed_source + 'noble universe']
        security_sources = [line.replace('archive.ubuntu.com', 'security.ubuntu.com') for line in fast_sources]
        mirror_retry = 'PUMP_LAB_STAGE=dependency-mirror'
        packages = 'libvulkan-dev glslang-dev glslang-tools spirv-tools'
        packages_with_ninja = packages + ' ninja-build'
        packages_with_opencl = packages + ' opencl-c-headers'
        update = 'update --error-on=any'
        simulate = f'--simulate install --no-install-recommends --no-remove {packages}'
        install = f'install --no-install-recommends --no-remove -y {packages}'
        for scenario, exit_code, expected in (
            ('fast', 0, [update, *fast_sources, simulate, *fast_sources, install, *fast_sources]),
            ('fallback', 0, [update, *fast_sources, simulate, *fast_sources, update, *full_sources,
                             'Reusing downloaded lists', install, *full_sources]),
            ('update-fails', 100, [update, *fast_sources, mirror_retry, update, *security_sources]),
            ('mirror-fallback', 0, [update, *fast_sources, mirror_retry, update, *security_sources,
                                    simulate, *security_sources, install, *security_sources]),
            ('fallback-update-fails', 100, [update, *fast_sources, simulate, *fast_sources,
                                           update, *full_sources]),
            ('ninja-fallback', 0, [update, *fast_sources,
                                   f'--simulate install --no-install-recommends --no-remove {packages_with_ninja}',
                                   *fast_sources, update, *full_sources, 'Reusing downloaded lists',
                                   f'install --no-install-recommends --no-remove -y {packages_with_ninja}',
                                   *full_sources]),
            ('opencl-present', 0, [update, *fast_sources,
                                   f'--simulate install --no-install-recommends --no-remove {packages_with_opencl}',
                                   *fast_sources, f'install --no-install-recommends --no-remove -y {packages_with_opencl}',
                                   *fast_sources]),
            ('opencl-missing', 0, [update, *fast_sources,
                                   f'--simulate install --no-install-recommends --no-remove {packages_with_opencl} ocl-icd-libopencl1:amd64',
                                   *fast_sources,
                                   f'install --no-install-recommends --no-remove -y {packages_with_opencl} ocl-icd-libopencl1:amd64',
                                   *fast_sources]),
        ):
            with self.subTest(scenario=scenario):
                ninja = bin_dir / 'ninja'
                if scenario == 'ninja-fallback':
                    ninja.unlink(missing_ok=True)
                else:
                    ninja.write_text('#!/bin/sh\nexit 0\n')
                    ninja.chmod(0o755)
                nvcc = bin_dir / 'nvcc'
                if scenario.startswith('opencl-'):
                    nvcc.write_text('#!/bin/sh\nexit 0\n')
                    nvcc.chmod(0o755)
                else:
                    nvcc.unlink(missing_ok=True)
                run = subprocess.run(['sh', '-s'], input=script, text=True, cwd=self.root,
                                     env={**os.environ, 'PATH': str(bin_dir),
                                          'LAB_TEST_APT_SCENARIO': scenario},
                                     capture_output=True, timeout=5)
                self.assertEqual(run.returncode, exit_code, run.stderr)
                self.assertEqual(run.stdout.splitlines(), expected)

        # The package resolver is intentionally limited to Ubuntu Noble amd64.
        for codename, architecture in (('focal', 'amd64'), ('noble', 'arm64')):
            os_release.write_text(f'ID=ubuntu\nVERSION_CODENAME={codename}\n')
            run = subprocess.run(['sh', '-s'], input=script, text=True, cwd=self.root,
                                 env={**os.environ, 'PATH': str(bin_dir), 'LAB_TEST_ARCH': architecture,
                                      'LAB_TEST_APT_SCENARIO': 'unsupported-image'},
                                 capture_output=True, timeout=5)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout, '')

    def test_remote_wrapper_reuses_and_resumes_only_matching_deployments(self):
        destination = self.root / 'managed'
        script = deploy.REMOTE_DEPLOY.replace("directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'",
                                             'directory = ' + repr(str(destination)))
        build = '''set -eu
echo build >> attempts
test ! -f fail-build
mkdir -p build/lab
printf '#!/bin/sh\nexit 0\n' > build/lab/datapump-worker
chmod +x build/lab/datapump-worker
echo PUMP_LAB_PROFILE=simulation
echo PUMP_LAB_MODE=cpu
echo PUMP_LAB_DEVICE=CPU
echo PUMP_LAB_TORCH=not-requested
echo PUMP_LAB_CUDA=not-requested
printf 'PUMP_LAB_WORKER_SHA256='
sha256sum build/lab/datapump-worker | cut -d ' ' -f 1
'''
        code = 'BUILD = ' + repr(build) + '\nRESOURCES = ' + repr("print('fresh resources')") + '\n' + script
        data = b'bounded archive'
        digest = hashlib.sha256(data).hexdigest()

        def invoke(payload=data, checksum=digest, mode='cpu'):
            return subprocess.run([sys.executable, '-c', code, checksum, mode, 'simulation', str(len(data))],
                                  input=payload, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)

        first = invoke()
        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual((destination / 'attempts').read_text(), 'build\n')
        second = invoke()
        self.assertEqual(second.returncode, 0, second.stdout)
        self.assertIn(b'fresh resources', second.stdout)
        self.assertEqual((destination / 'attempts').read_text(), 'build\n')

        # A precisely identified prior builder may migrate only the same
        # source/mode/profile request so an interrupted retry can reuse its
        # existing CMake cache and selected generator.
        deployment = destination / '.deployment.json'
        current_request = json.loads(deployment.read_text())
        for builder in ('7c4ac0b50ceca91390fc881c428d3838310dac6c247dd561f2ec12163c901cd5',
                        '200bce2484affd29702fe15275135bdbe5b196e7458830204c05604505eacdfc'):
            deployment.write_text(json.dumps({**current_request, 'builder': builder}))
            migrated = invoke()
            self.assertEqual(migrated.returncode, 0, migrated.stdout)
            self.assertEqual(json.loads(deployment.read_text()), current_request)
        self.assertEqual((destination / 'attempts').read_text(), 'build\n')

        self.assertNotEqual(invoke(mode='gpu').returncode, 0)
        self.assertNotEqual(invoke(mode='vulkan').returncode, 0)
        self.assertNotEqual(invoke(checksum='0' * 64).returncode, 0)
        self.assertEqual((destination / 'attempts').read_text(), 'build\n')

        # A missing receipt represents interruption after extraction/build.
        (destination / '.worker-receipt.json').unlink()
        (destination / 'fail-build').touch()
        failed = invoke()
        self.assertNotEqual(failed.returncode, 0)
        self.assertFalse((destination / '.worker-receipt.json').exists())
        (destination / 'fail-build').unlink()
        resumed = invoke()
        self.assertEqual(resumed.returncode, 0, resumed.stdout)
        self.assertEqual((destination / 'attempts').read_text(), 'build\nbuild\nbuild\n')

        # Truncated retries preserve the validated archive and completed worker.
        self.assertNotEqual(invoke(payload=b'partial').returncode, 0)
        self.assertEqual((destination / 'source.tar.gz').read_bytes(), data)
        self.assertEqual(list(destination.glob('.upload-*')), [])
        (destination / '.deployment.json').unlink()
        self.assertNotEqual(invoke().returncode, 0)
        self.assertEqual((destination / 'attempts').read_text(), 'build\nbuild\nbuild\n')

    def test_remote_wrapper_keeps_running_build_locked(self):
        import fcntl
        destination = self.root / 'managed'
        destination.mkdir(mode=0o700)
        script = deploy.REMOTE_DEPLOY.replace("directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'",
                                             'directory = ' + repr(str(destination)))
        code = "BUILD = ''\nRESOURCES = ''\n" + script
        with (destination / '.deploy.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = subprocess.run([sys.executable, '-c', code, '0' * 64, 'cpu', 'simulation', '1'],
                                    input=b'x', stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'already running', result.stdout)
        self.assertFalse((destination / 'source.tar.gz').exists())

    def test_remote_wrapper_drains_output_before_large_script_is_consumed(self):
        destination = self.root / 'managed'
        script = deploy.REMOTE_DEPLOY.replace("directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'",
                                             'directory = ' + repr(str(destination)))
        # Force small stdout pipes, and emit more than their capacity before
        # the shell consumes the rest of its script. A synchronous stdin pipe
        # write would deadlock against this early output.
        script = script.replace('pass_fds=(lock,)', 'pass_fds=(lock,), pipesize=4096')
        build = "python3 -c 'print(\"x\" * 65536)'\n" + '# padding\n' * 2000 + '''
mkdir -p build/lab
printf '#!/bin/sh\nexit 0\n' > build/lab/datapump-worker
chmod +x build/lab/datapump-worker
echo PUMP_LAB_MODE=cpu
'''
        code = 'BUILD = ' + repr(build) + "\nRESOURCES = ''\n" + script
        data = b'archive'
        digest = hashlib.sha256(data).hexdigest()
        result = subprocess.run([sys.executable, '-c', code, digest, 'cpu', 'simulation', str(len(data))],
                                input=data, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout[-1000:])
        self.assertIn(b'PUMP_LAB_MODE=cpu', result.stdout)
        self.assertTrue((destination / '.worker-receipt.json').is_file())

    def test_gpu_receipt_fallbacks_and_no_full_device_claim(self):
        namespace = {'__name__': 'receipt_test'}
        exec(deploy.RESOURCE_RECEIPT, namespace)
        fake_torch = types.SimpleNamespace(cuda=types.SimpleNamespace(
            get_device_properties=lambda index: types.SimpleNamespace(total_memory=16000, uuid='GPU-123')))
        out = io.StringIO()
        with patch.dict(os.environ, {'PUMP_LAB_RECEIPT_MODE': 'cuda'}), \
             patch.dict(sys.modules, {'torch': fake_torch}), \
             patch.object(namespace['shutil'], 'which', return_value=None), \
             contextlib.redirect_stdout(out):
            namespace['gpu_receipts']()
        self.assertIn('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=16000', out.getvalue())
        self.assertIn('PUMP_LAB_GPU_VISIBLE_UUID=GPU-123', out.getvalue())
        self.assertIn('PUMP_LAB_GPU_PARTITION_STATUS=unverified', out.getvalue())
        self.assertIn('PUMP_LAB_GPU_INVENTORY=unavailable', out.getvalue())
        fake_torch.cuda.get_device_properties = lambda index: (_ for _ in ()).throw(RuntimeError())
        out = io.StringIO()
        with patch.dict(os.environ, {'PUMP_LAB_RECEIPT_MODE': 'cuda'}), \
             patch.dict(sys.modules, {'torch': fake_torch}), \
             patch.object(namespace['shutil'], 'which', return_value=None), \
             contextlib.redirect_stdout(out):
            namespace['gpu_receipts']()
        self.assertIn('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=unavailable', out.getvalue())
        self.assertIn('PUMP_LAB_GPU_VISIBLE_UUID=unavailable', out.getvalue())

    def test_runtime_cgroup_v1_limits(self):
        namespace = {'__name__': 'receipt_test'}
        exec(deploy.RESOURCE_RECEIPT, namespace)
        proc = self.root / 'proc'
        (proc / 'self').mkdir(parents=True)
        group = self.root / 'memory'
        child = group / 'batch'
        child.mkdir(parents=True)
        (proc / 'self/cgroup').write_text('8:memory:/batch\n')
        (proc / 'self/mountinfo').write_text(f'1 0 0:1 / {group} rw - cgroup cgroup rw,memory\n')
        (group / 'memory.limit_in_bytes').write_text('9223372036854771712')
        (child / 'memory.limit_in_bytes').write_text('2048')
        self.assertEqual(namespace['cgroup_limit'](proc, 'memory'), '2048')
        self.assertEqual(namespace['cgroup_limit'](proc, 'cpu'), 'unavailable')

    def test_hash_mismatch_aborts_before_extraction_or_build(self):
        (self.root / 'source.tar.gz').write_bytes(b'not the bundle')
        script = deploy.REMOTE_BUILD.replace('cd "$directory"', 'directory=' + shlex.quote(str(self.root)) + '\ncd "$directory"', 1)
        result = subprocess.run(['sh', '-s', '--', '0' * 64, 'cpu', 'simulation'], input=script, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'build').exists())


if __name__ == '__main__':
    unittest.main()
