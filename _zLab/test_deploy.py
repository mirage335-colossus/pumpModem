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
        self.assertEqual(len(self.calls), 3)
        mkdir, scp, build = (item[0] for item in self.calls)
        for command in (mkdir, scp, build):
            self.assertIn('StrictHostKeyChecking=yes', command)
            self.assertIn('UserKnownHostsFile=' + str(self.known), command)
            self.assertIn('BatchMode=yes', command)
            self.assertEqual(command[1:3], ['-F', '/dev/null'])
        self.assertEqual(mkdir[-5:], ['mkdir', '-m', '700', '--', deploy.REMOTE_DIR])
        self.assertEqual(scp[-1], 'root@pod.example.com:/workspace/pump-lab/source.tar.gz')
        digest = hashlib.sha256(b'test archive').hexdigest()
        self.assertEqual(build[-1], f'sh -s -- {digest} cuda simulation')
        self.assertEqual(self.calls[-1][1]['input'], deploy.REMOTE_BUILD.encode())

    def test_live_profile_command_and_path(self):
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle):
            result = deploy.deploy('pod.example.com', 'root', 22, self.known,
                                   mode='cuda', profile='live', runner=self.runner)
        self.assertEqual(result, '/workspace/pump-lab-live/build/lab/datapump-lab-worker')
        digest = hashlib.sha256(b'test archive').hexdigest()
        self.assertEqual(self.calls[-1][0][-1], f'sh -s -- {digest} cuda live')
        self.assertEqual(self.calls[0][0][-1], '/workspace/pump-lab-live')
        self.assertEqual(self.calls[1][0][-1], 'root@pod.example.com:/workspace/pump-lab-live/source.tar.gz')

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
        with self.assertRaises(subprocess.TimeoutExpired):
            deploy._bounded_process([sys.executable, '-c', 'import time; time.sleep(5)'], None, .1)

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
             patch.object(deploy.time, 'monotonic', side_effect=[100, 101, 103, 109]):
            deploy.deploy('pod.example.com', 'root', 22, self.known, runner=self.runner, timeout=10)
        self.assertEqual([call[1]['timeout'] for call in self.calls], [9, 7, 1])

        self.calls.clear()
        with patch.object(deploy.bundle, 'create_bundle', side_effect=self.bundle), \
             patch.object(deploy.time, 'monotonic', side_effect=[100, 102, 110]), \
             self.assertRaisesRegex(deploy.DeployError, 'deadline'):
            deploy.deploy('pod.example.com', 'root', 22, self.known, runner=self.runner, timeout=10)
        self.assertEqual(len(self.calls), 1)

    def test_remote_cpu_and_cuda_branches_execute_correct_flags(self):
        # Exercise the actual POSIX script with fake CMake and fake Torch. This
        # checks branching/arguments, not CUDA availability or a real build.
        for mode, profile in itertools.product(('cpu', 'cuda', 'gpu'), ('simulation', 'live')):
            with self.subTest(mode=mode, profile=profile):
                home = self.root / (mode + '-' + profile)
                home.mkdir()
                archive = home / 'source.tar.gz'
                with tarfile.open(archive, 'w:gz') as tar:
                    info = tarfile.TarInfo('source-file')
                    info.size = 1
                    tar.addfile(info, io.BytesIO(b'x'))
                digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                bin_dir = home / 'bin'
                bin_dir.mkdir()
                cmake = bin_dir / 'cmake'
                cmake.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$CMAKE_LOG"
if [ "$1" = --build ]; then
  mkdir -p build/lab
  for arg do worker=$arg; done
  printf '#!/bin/sh\\necho no-socket-boundary\\n' > "build/lab/$worker"
  chmod +x "build/lab/$worker"
fi
''')
                cmake.chmod(0o755)
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
                for name in ('sh', 'tar', 'gzip', 'sha256sum', 'chmod', 'mkdir', 'dirname', 'readlink', 'timeout'):
                    (bin_dir / name).symlink_to(shutil.which(name))
                (bin_dir / 'python3').symlink_to(sys.executable)
                env = {**os.environ, 'PATH': str(bin_dir),
                       'PYTHONPATH': str(home), 'CMAKE_LOG': str(log)}
                toolkit = home / 'toolkit'
                nvcc = toolkit / 'bin' / 'nvcc'
                for key in ('CUDACXX', 'CUDA_HOME', 'CUDA_PATH', 'CUDAToolkit_ROOT'):
                    env.pop(key, None)
                if mode != 'cpu':
                    nvcc.parent.mkdir(parents=True)
                    nvcc.touch(mode=0o755)
                    if profile == 'live':
                        env['CUDACXX'] = str(nvcc)
                vulkan = mode == 'gpu' and profile == 'simulation'
                if mode == 'gpu':
                    # Stand in for the compiled CUDA probe. Its discovery result
                    # decides the branch; building the worker is still simulated.
                    compiler = bin_dir / 'c++'
                    compiler.write_text("#!/bin/sh\nfor arg do output=$arg; done\n"
                                        "printf '#!/bin/sh\\nexit %d\\n' \"$PROBE_STATUS\" > \"$output\"\n"
                                        'chmod +x "$output"\n')
                    compiler.chmod(0o755)
                    env['PROBE_STATUS'] = '2' if vulkan else '0'
                if vulkan:
                    source = home / 'nvidia_icd.json'
                    source.write_text(json.dumps({'ICD': {'library_path': 'libGLX_nvidia.so.0',
                                                            'api_version': '1.4.312'}}))
                    prepare = (Path(deploy.__file__).parent / 'prepare_vulkan.sh').read_text()
                    prepare = prepare.replace('/etc/os-release', '/dev/null')
                    prepare = prepare.replace('/etc/vulkan/icd.d/nvidia_icd.json', str(source))
                    (home / '_zLab').mkdir()
                    (home / '_zLab/prepare_vulkan.sh').write_text(prepare)
                script = deploy.REMOTE_BUILD.replace('cd "$directory"', 'directory=' + shlex.quote(str(home)) + '\ncd "$directory"', 1)
                # Model the image layout in the test's private directory.
                script = script.replace('/usr/local/cuda/bin/nvcc', shlex.quote(str(nvcc)))
                result = subprocess.run(['sh', '-s', '--', digest, mode, profile], input=script, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout)
                flags = log.read_text()
                backend = 'vulkan' if vulkan else 'cuda' if mode == 'gpu' else mode
                self.assertIn('PUMP_LAB_MODE=' + backend, result.stdout)
                self.assertIn('PUMP_LAB_PROFILE=' + profile, result.stdout)
                self.assertIn('PUMP_LAB_RECEIPT_SCOPE=deployment SSH process; worker execution not established', result.stdout)
                self.assertIn('PUMP_LAB_CPU_MODEL=', result.stdout)
                self.assertIn('PUMP_LAB_CPU_MODEL_SCOPE=first reported processor', result.stdout)
                self.assertRegex(result.stdout, r'PUMP_LAB_KERNEL_MEMORY_TOTAL_BYTES=(?:[0-9]+|unavailable)')
                self.assertIn('PUMP_LAB_CGROUP_SCOPE=visible ancestors only; hidden limits may be tighter', result.stdout)
                self.assertRegex(result.stdout, r'PUMP_LAB_CPU_AFFINITY_LOGICAL=(?:[0-9]+|unavailable)')
                self.assertRegex(result.stdout, r'PUMP_LAB_CGROUP_MEMORY_LIMIT_BYTES=(?:[0-9]+|unavailable|unlimited)')
                self.assertRegex(result.stdout, r'PUMP_LAB_CGROUP_CPU_QUOTA_CORES=(?:[0-9/]+|unavailable|unlimited)')
                if profile == 'live':
                    self.assertIn('-DCMAKE_PROJECT_DataPump_INCLUDE=' + str(home) + '/_zLab/audio/inject.cmake', flags)
                    self.assertIn('--target datapump-lab-worker', flags)
                else:
                    self.assertNotIn('CMAKE_PROJECT_DataPump_INCLUDE', flags)
                    self.assertIn('--target datapump-worker', flags)
                self.assertRegex(result.stdout, r'PUMP_LAB_WORKER_SHA256=[0-9a-f]{64}')
                if backend == 'cuda':
                    self.assertIn('PUMP_LAB_DEVICE=fake GPU', result.stdout)
                    self.assertIn('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=12345678', result.stdout)
                    self.assertIn('PUMP_LAB_GPU_VISIBLE_UUID=MIG-fake-uuid', result.stdout)
                    self.assertIn('PUMP_LAB_GPU_PARTITION_STATUS=MIG indicated by UUID', result.stdout)
                    self.assertIn('PUMP_LAB_GPU_INVENTORY=', result.stdout)
                    self.assertIn('PUMP_LAB_TORCH=fake', result.stdout)
                    self.assertIn('PUMP_LAB_CUDA=fake', result.stdout)
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
                    manifest = home / 'build/lab/nvidia-headless-icd.json'
                    self.assertEqual(manifest.stat().st_mode & 0o777, 0o600)
                    self.assertEqual(json.loads(manifest.read_text())['ICD'],
                                     {'library_path': 'libEGL_nvidia.so.0', 'api_version': '1.4.312'})
                else:
                    self.assertIn('PUMP_LAB_DEVICE=CPU (processor not identified)', result.stdout)
                    self.assertIn('PUMP_LAB_GPU_SCOPE=not requested', result.stdout)
                    self.assertIn('PUMP_LAB_TORCH=not requested', result.stdout)
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
