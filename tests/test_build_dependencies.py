#!/usr/bin/env python3
"""Check offline SDK integrity, ownership and repair using a tiny local .deb."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_deps', ROOT / 'tools/prepare-build-deps.py')
deps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deps)


class DependencyPrefixSelection(unittest.TestCase):
    def test_only_native_linux_amd64_uses_automatic_sdk(self):
        with tempfile.TemporaryDirectory(prefix='datapump prefix ') as work:
            root = Path(work)
            prefix = root / 'third_party/build-support/cache/sysroot/usr'
            (prefix / 'include').mkdir(parents=True)
            (prefix.parent / 'prepared.json').write_text('{}')
            script = root / 'check.cmake'
            script.write_text('cmake_minimum_required(VERSION 3.21)\n'
                f'set(CMAKE_CURRENT_SOURCE_DIR [==[{root.as_posix()}]==])\n'
                'set(DATAPUMP_COMPILER_CACHE OFF)\n'
                f'include([==[{(ROOT / "cmake/BuildDependencies.cmake").as_posix()}]==])\n'
                'if(NOT "${DATAPUMP_DEPENDENCY_PREFIX}" STREQUAL "${EXPECTED}")\n'
                '  message(FATAL_ERROR "Wrong SDK selection: ${DATAPUMP_DEPENDENCY_PREFIX}")\n'
                'endif()\n')
            for cross, system, cpu, explicit, expected in [
                ('OFF', 'Linux', 'x86_64', '', prefix.as_posix()),
                ('ON', 'Linux', 'x86_64', '', ''),
                ('OFF', 'Linux', 'aarch64', '', ''),
                ('OFF', 'Darwin', 'x86_64', '', ''),
                ('ON', 'Linux', 'aarch64', prefix.as_posix(), prefix.as_posix()),
            ]:
                with self.subTest(cross=cross, system=system, cpu=cpu, explicit=explicit):
                    result = subprocess.run(['cmake', f'-DCMAKE_CROSSCOMPILING={cross}',
                        f'-DCMAKE_SYSTEM_NAME={system}', f'-DCMAKE_SYSTEM_PROCESSOR={cpu}',
                        f'-DDATAPUMP_DEPENDENCY_PREFIX={explicit}', f'-DEXPECTED={expected}',
                        '-P', str(script)], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(shutil.which('dpkg-deb') and shutil.which('dpkg'), 'optional Debian SDK tool')
class OfflineDependencies(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='datapump SDK test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        package = self.root / 'fixture'
        (package / 'DEBIAN').mkdir(parents=True)
        (package / 'DEBIAN/control').write_text(
            'Package: datapump-sdk-fixture\nVersion: 1.0\nArchitecture: all\n'
            'Maintainer: DataPump tests <nobody@example.invalid>\nDescription: offline fixture\n')
        (package / 'usr/include').mkdir(parents=True)
        (package / 'usr/include/fixture.h').write_text('/* intact */\n')
        self.packages = self.root / 'input archives'
        self.packages.mkdir()
        self.archive = self.packages / 'fixture.deb'
        subprocess.run(['dpkg-deb', '--build', str(package), str(self.archive)],
                       check=True, capture_output=True)
        self.manifest = self.root / 'manifest.json'
        self.record = {
            'architecture': deps.command('dpkg', '--print-architecture'),
            'multiarch': 'unused', 'runtime_libraries': [],
            'packages': [{'name': 'fixture-dev', 'version': '1.0', 'file': 'fixture.deb',
                          'url': 'https://deb.debian.org/debian/fixture.deb',
                          'sha256': hashlib.sha256(self.archive.read_bytes()).hexdigest()}]}
        self.manifest.write_text(json.dumps(self.record))
        self.args = SimpleNamespace(manifest=self.manifest, packages_dir=self.packages,
                                    cache_dir=self.root / 'persistent cache', download=False)

    def test_verified_offline_restore_retains_archives_and_repairs_headers(self):
        with patch.object(deps.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            deps.prepare(self.args)
            header = self.args.cache_dir / 'sysroot/usr/include/fixture.h'
            self.assertEqual(header.read_text(), '/* intact */\n')
            self.assertTrue((self.args.cache_dir / 'packages/fixture.deb').is_file())
            header.unlink()
            shutil.rmtree(self.packages)
            self.args.packages_dir = None
            deps.prepare(self.args)
            self.assertEqual(header.read_text(), '/* intact */\n')

    def test_corrupt_archive_is_rejected_before_creating_sdk(self):
        self.archive.write_bytes(self.archive.read_bytes() + b'corruption')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            deps.prepare(self.args)
        self.assertFalse((self.args.cache_dir / 'sysroot').exists())

    def test_missing_archive_does_not_download_implicitly(self):
        self.archive.unlink()
        with patch.object(deps.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            with self.assertRaisesRegex(ValueError, 'Missing'):
                deps.prepare(self.args)

    def test_unmanaged_directory_is_not_replaced(self):
        destination = self.args.cache_dir / 'sysroot'
        destination.mkdir(parents=True)
        keep = destination / 'keep'
        keep.write_text('user files')
        with self.assertRaisesRegex(ValueError, 'unmanaged'):
            deps.prepare(self.args)
        self.assertEqual(keep.read_text(), 'user files')

    def test_symlink_destination_is_not_replaced(self):
        self.args.cache_dir.mkdir()
        (self.args.cache_dir / 'sysroot').symlink_to(self.packages, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink SDK'):
            deps.prepare(self.args)
        self.assertTrue(self.archive.exists())

    def test_runtime_version_mismatch_is_rejected(self):
        self.record['runtime_libraries'] = [dict(development='fixture-dev', package='fixture-runtime')]
        self.manifest.write_text(json.dumps(self.record))
        def query(*args):
            return self.record['architecture'] if args[0] == 'dpkg' else '2.0'
        with patch.object(deps, 'command', side_effect=query):
            with self.assertRaisesRegex(ValueError, 'installed 2.0, SDK requires 1.0'):
                deps.prepare(self.args)
        self.assertFalse(self.args.cache_dir.exists())


if __name__ == '__main__':
    unittest.main()
