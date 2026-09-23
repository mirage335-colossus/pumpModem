#!/usr/bin/env python3
"""Exercise signed/pinned CI toolchain setup without apt, downloads or builds."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('release_rev_tools', ROOT / 'tools/install-release-rev.py')
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)


class ReleaseRevTools(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump rev tools ')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.system = self.root / 'system'
        (self.system / 'etc').mkdir(parents=True)
        (self.system / 'etc/os-release').write_text('ID=ubuntu\nVERSION_ID="22.04"\n')
        self.destination = self.root / 'installed tools'
        self.calls = []
        self.payload = b'controlled Ninja source archive'
        self.fingerprint = tools.LLVM_FINGERPRINT
        self.extra_key = False
        self.patches = [
            patch.object(tools, 'run', side_effect=self.fake_run),
            patch.object(tools, 'download', side_effect=self.download),
            patch.object(tools, 'NINJA_SHA256', hashlib.sha256(self.payload).hexdigest()),
            patch.object(tools.os, 'geteuid', return_value=0),
            patch.object(tools.os, 'cpu_count', return_value=3),
            patch.object(tools.platform, 'system', return_value='Linux'),
            patch.object(tools.platform, 'machine', return_value='x86_64'),
        ]
        for mock in self.patches:
            mock.start()
            self.addCleanup(mock.stop)

    def download(self, url, destination):
        self.calls.append(['download', url])
        Path(destination).write_bytes(self.payload)

    def fake_run(self, arguments, **kwargs):
        arguments = [str(argument) for argument in arguments]
        self.calls.append(arguments)
        output = ''
        if arguments[0] == 'gpg' and '--show-keys' in arguments:
            output = f'pub:::::::::\nfpr:::::::::{self.fingerprint}:\nsub:::::::::\nfpr:::::::::SUBKEY:\n'
            if self.extra_key:
                output += 'pub:::::::::\nfpr:::::::::UNTRUSTED:\n'
        elif arguments[0] == 'gpg' and '--dearmor' in arguments:
            Path(arguments[arguments.index('--output') + 1]).write_bytes(b'verified keyring')
        elif arguments[0] == 'cmake' and '-S' in arguments:
            self.staged = Path(next(argument.split('=', 1)[1] for argument in arguments
                                    if argument.startswith('-DCMAKE_INSTALL_PREFIX=')))
        elif arguments[0] == 'cmake' and '--install' in arguments:
            (self.staged / 'bin').mkdir(parents=True)
            (self.staged / 'bin/ninja').write_text('mock-built Ninja')
        elif arguments[0].endswith('/bin/ninja'):
            output = tools.NINJA_VERSION + '\n'
        return subprocess.CompletedProcess(arguments, 0, stdout=output, stderr='')

    def test_pinned_setup_for_both_architectures(self):
        for machine, architecture in [('x86_64', 'amd64'), ('aarch64', 'arm64')]:
            with self.subTest(machine=machine), patch.object(tools.platform, 'machine', return_value=machine):
                destination = self.destination / architecture
                tools.install(destination, self.system)
                self.assertTrue((destination / 'bin/ninja').is_file())
                source = (self.system / 'etc/apt/sources.list.d/datapump-release-llvm19.list').read_text()
                self.assertIn(f'arch={architecture}', source)
                self.assertIn('signed-by=', source)
                self.assertNotIn('trusted=yes', source)
                self.assertIn('https://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main', source)
        clang_install = next(call for call in self.calls if any(value.startswith('clang-19=') for value in call))
        self.assertIn(f'clang-19={tools.LLVM_VERSION}', clang_install)
        self.assertIn(f'clang-tools-19={tools.LLVM_VERSION}', clang_install)
        for dependency in ['libxrandr-dev', 'libgl-dev', 'libglew-dev', 'libfreetype-dev']:
            self.assertIn(dependency, clang_install)
        configure = next(call for call in self.calls if '-S' in call)
        self.assertIn('-DBUILD_TESTING=OFF', configure)
        self.assertIn('-DCMAKE_CXX_COMPILER=g++-11', configure)
        self.assertTrue(any(call[:2] == ['cmake', '--build'] and call[-2:] == ['--parallel', '3']
                            for call in self.calls))

    def test_rejects_wrong_or_additional_signing_key_before_llvm_install(self):
        for wrong, extra in [(True, False), (False, True)]:
            with self.subTest(wrong=wrong, extra=extra):
                self.fingerprint = 'WRONG' if wrong else tools.LLVM_FINGERPRINT
                self.extra_key = extra
                with self.assertRaisesRegex(ValueError, 'fingerprint'):
                    tools.install(self.destination, self.system)
                self.assertFalse((self.system / 'etc/apt/sources.list.d').exists())
                self.assertFalse(any(any(value.startswith('clang-19=') for value in call) for call in self.calls))

    def test_rejects_modified_ninja_before_extracting_or_building(self):
        with patch.object(tools, 'NINJA_SHA256', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'checksum'):
                tools.install(self.destination, self.system)
        self.assertFalse(any(call[0] in ('tar', 'cmake') for call in self.calls))
        self.assertFalse(self.destination.exists())

    def test_rejects_wrong_baseline_or_existing_destination_before_apt(self):
        (self.system / 'etc/os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        with self.assertRaisesRegex(ValueError, '22.04'):
            tools.install(self.destination, self.system)
        self.destination.mkdir()
        with self.assertRaisesRegex(ValueError, 'new directory'):
            tools.install(self.destination, self.system)
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
