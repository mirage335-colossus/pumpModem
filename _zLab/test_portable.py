#!/usr/bin/env python3
"""Check the ordinary package bytes through the authenticated installer boundary."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
import zipfile
from io import BytesIO
from dataclasses import replace

import portable
from deploy import DeployError


class PortableSimulation(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.distribution = self.base / 'distribution'
        self.remote = self.base / 'compute' / 'portable'
        self.known = self.base / 'known_hosts'
        self.known.touch()
        self.files = {
            portable.WORKER: Path('/bin/true').read_bytes() if sys.platform.startswith('linux') else b'\x7fELFfixture',
            portable.HOSTED + 'web-manifest.json': json.dumps(portable.PROFILE).encode(),
            portable.DOC + 'build-info.txt': b'System: Linux x86_64\nPortable: ON\nWeb worker: ON (inherited-pipes; no sockets)\n',
            portable.DOC + 'LICENSE': b'DataPump license',
            portable.DOC + 'runtime-info.txt': b'Runtime inventory',
            portable.DOC + 'runtime-notices/worker-LICENSE.txt': b'Library notice',
            'lib/libfuture-runtime.so.1': b'Future dynamically loaded runtime',
            'bin/datapump-gui': b'Desktop only',
            'extra/private.txt': b'Private local file',
        }
        for name in ('hosted.mjs', 'renderer.mjs', 'protocol.mjs', 'audio_worklet.js', 'browser_audio.mjs', 'style.css'):
            self.files[portable.HOSTED + name] = ('Asset ' + name).encode()
        self.write_distribution()
        self.upload_sizes = []

    def write_distribution(self):
        manifest = []
        for name, data in self.files.items():
            p = self.distribution / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            manifest.append(hashlib.sha256(data).hexdigest() + '  ' + name + '\n')
        (self.distribution / portable.WORKER).chmod(0o755)
        (self.distribution / 'manifest.sha256').write_text(''.join(manifest))

    def ssh(self, argv, **kwargs):
        self.assertIn('StrictHostKeyChecking=yes', argv)
        command = shlex.split(argv[-1])
        self.assertEqual(command[:2], ['python3', '-c'])
        command[0] = sys.executable
        command[2] = command[2].replace("Path('/workspace/pump-lab-portable')", f'Path({str(self.remote)!r})')
        self.upload_sizes.append(len(kwargs.get('input') or b''))
        return subprocess.run(command, **kwargs)

    def deploy(self, package):
        return portable.deploy_package(package, 'localhost', 'lab', 22, self.known, runner=self.ssh)

    def test_captures_one_verified_package(self):
        package = portable.load_package(self.distribution)
        self.assertEqual(package.worker_sha256, hashlib.sha256(self.files[portable.WORKER]).hexdigest())
        self.assertEqual(package.web_assets['style.css'], self.files[portable.HOSTED + 'style.css'])
        with self.assertRaises(TypeError):
            package.web_assets['style.css'] = b'changed'
        with zipfile.ZipFile(BytesIO(package.payload)) as archive:
            for name in (portable.WORKER, 'lib/libfuture-runtime.so.1', portable.DOC + 'runtime-notices/worker-LICENSE.txt'):
                self.assertEqual(archive.read(name), self.files[name])
            self.assertNotIn('extra/private.txt', archive.namelist())
            self.assertNotIn('bin/datapump-gui', archive.namelist())
        self.assertEqual(portable.load_package(self.distribution).payload_sha256, package.payload_sha256)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Local remote-host fixture executes a Linux binary')
    def test_installs_and_reuses_correct_generation_without_upload(self):
        first = portable.load_package(self.distribution)
        self.assertEqual(self.deploy(first), first.remote_worker)
        self.assertEqual(self.upload_sizes, [0, len(first.payload)])
        self.assertEqual(self.deploy(first), first.remote_worker)
        self.assertEqual(self.upload_sizes[-1], 0)
        self.files[portable.HOSTED + 'style.css'] = b'New browser release'
        self.files[portable.WORKER] += b'Next release'
        self.write_distribution()
        next_package = portable.load_package(self.distribution)
        self.assertNotEqual(self.deploy(next_package), first.remote_worker)
        for package in (first, next_package):
            binary = self.remote / package.payload_sha256 / portable.WORKER
            self.assertEqual(hashlib.sha256(binary.read_bytes()).hexdigest(), package.worker_sha256)
        # Warm reuse rejects even a damaged library or a new unrecorded runtime.
        (self.remote / next_package.payload_sha256 / 'lib/libfuture-runtime.so.1').write_bytes(b'changed')
        with self.assertRaisesRegex(DeployError, 'content mismatch'):
            self.deploy(next_package)
        foreign = self.remote / first.payload_sha256 / 'lib/libforeign.so'
        foreign.write_bytes(b'unknown')
        foreign.chmod(0o600)
        with self.assertRaisesRegex(DeployError, 'content mismatch'):
            self.deploy(first)

    def test_rejects_mismatched_browser_binary_and_unsafe_runtime_inputs(self):
        for name in (portable.HOSTED + 'renderer.mjs', portable.WORKER, 'lib/libfuture-runtime.so.1'):
            path = self.distribution / name
            path.write_bytes(b'changed')
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'checksum'):
                portable.load_package(self.distribution)
            path.write_bytes(self.files[name])
        path = self.distribution / 'lib/libfuture-runtime.so.1'
        path.unlink()
        path.symlink_to(self.distribution / 'extra/private.txt')
        with self.assertRaises(OSError):
            portable.load_package(self.distribution)
        path.unlink()
        self.files[portable.HOSTED + 'web-manifest.json'] = b'{"target":"other"}'
        self.write_distribution()
        with self.assertRaisesRegex(ValueError, 'unsupported hosted worker'):
            portable.load_package(self.distribution)

    def test_rejects_redirected_package_asset_directory(self):
        web = self.distribution / 'share/datapump/web'
        outside = self.base / 'outside'
        web.rename(outside)
        web.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            portable.load_package(self.distribution)

    def test_older_package_reports_upgrade_before_deployment(self):
        old_profile = dict(portable.PROFILE)
        del old_profile['simulation_startup_version']
        self.files[portable.HOSTED + 'web-manifest.json'] = json.dumps(old_profile).encode()
        self.write_distribution()
        with self.assertRaisesRegex(ValueError, 'Select a current Linux portable package'):
            portable.load_package(self.distribution)

    def test_rejects_duplicate_manifest_path_and_non_executable_worker(self):
        manifest = self.distribution / 'manifest.sha256'
        payload = manifest.read_bytes()
        manifest.write_bytes(payload + payload.splitlines()[0] + b'\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            portable.load_package(self.distribution)
        manifest.write_bytes(payload)
        (self.distribution / portable.WORKER).chmod(0o600)
        with self.assertRaisesRegex(ValueError, 'Invalid portable package file'):
            portable.load_package(self.distribution)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Local remote-host fixture executes a Linux binary')
    def test_incomplete_upload_and_failed_self_check_never_publish(self):
        package = portable.load_package(self.distribution)
        damaged = replace(package, payload=package.payload[:-1] + bytes([package.payload[-1] ^ 1]))
        with self.assertRaisesRegex(DeployError, 'upload checksum mismatch'):
            self.deploy(damaged)
        self.assertFalse((self.remote / package.payload_sha256).exists())
        self.files[portable.WORKER] = Path('/bin/false').read_bytes()
        self.write_distribution()
        broken_worker = portable.load_package(self.distribution)
        with self.assertRaises(DeployError):
            self.deploy(broken_worker)
        self.assertFalse((self.remote / broken_worker.payload_sha256).exists())
        self.assertEqual(list(self.remote.glob('.install-*')), [])
        self.assertEqual(self.deploy(package), package.remote_worker)


if __name__ == '__main__':
    unittest.main()
