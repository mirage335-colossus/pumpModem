#!/usr/bin/env python3
"""Signed pacman database, native package payload and repository identity regressions."""
from datetime import datetime, timedelta, timezone
import gzip
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import threading
import unittest
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('arch_release', ROOT / 'tools/arch-release.py')
arch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(arch)
distro = arch.distro_module()
apt = distro.apt_module()
release = apt.release_module()


def fixture_archive(path, metadata, target):
    backend = release.target_backend(metadata, target)
    root = release.package_bases(metadata, target)[0]
    files = {'bin/pump': (b'#!/bin/sh\nprintf "fixture CLI\\n"\n', 0o755),
             'bin/datapump-gui': (b'#!/bin/sh\nexit 0\n', 0o755),
             'lib/example.so': (b'private binary library\x00', 0o644),
             'share/doc/datapump/LICENSE': (b'fixture original license\n', 0o444),
             'share/doc/datapump/license with spaces.txt': (b'preserve names and bytes\n', 0o644),
             'share/doc/datapump/build-info.txt': (f'GUI: ON ({backend})\n'.encode(), 0o644)}
    sums = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, (data, _) in files.items())
    files['manifest.sha256'] = (sums.encode(), 0o644)
    with tarfile.open(path, 'w:gz') as archive:
        for name, (data, mode) in files.items():
            info = tarfile.TarInfo(root + '/' + name)
            info.size, info.mode = len(data), mode
            archive.addfile(info, io.BytesIO(data))


def tar_files(path):
    with tarfile.open(path, 'r:gz') as archive:
        return {member.name: (archive.extractfile(member).read(), member.mode)
                for member in archive if member.isfile()}


def database_fields(data):
    result = {}
    for stanza in data.decode().strip().split('\n\n'):
        key, *values = stanza.splitlines()
        result[key.strip('%')] = values
    return result


@unittest.skipUnless(all(shutil.which(name) for name in ('gpg', 'gpgv')), 'GnuPG is required')
class ArchReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='pacman-release-tests-')
        cls.root = Path(cls.temporary.name)
        cls.home = cls.root / 'gpg'
        cls.home.mkdir(mode=0o700)
        apt.run('gpg', '--batch', '--homedir', cls.home, '--pinentry-mode', 'loopback', '--passphrase', '',
                '--quick-generate-key', 'DataPump disposable native package signer', 'ed25519', 'sign', '0')
        public = cls.root / 'public.gpg'
        public.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--export').stdout)
        cls.fingerprint = apt.fingerprint(public)
        cls.key = cls.root / 'signing.asc'
        cls.key.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--armor', '--export-secret-keys').stdout)
        cls.key.chmod(0o600)
        cls.repository = 'test-owner/test-repo'
        cls.metadata = release.make_metadata(source_sha='a' * 40, packager_sha='b' * 40, run_id='123',
            run_attempt='2', cmake_version='0.7.2', version='v001_00', experiment=True, schema=5,
            now=datetime.now(timezone.utc).replace(microsecond=0))
        cls.base = cls.root / 'release'
        cls.base.mkdir()
        for target, name in release.application_names(cls.metadata).items():
            if target.startswith('linux-'):
                fixture_archive(cls.base / name, cls.metadata, target)
        previous = os.umask(0o077)
        try:
            cls.manifest = arch.build(cls.base, cls.metadata, cls.repository, cls.key, cls.fingerprint)
        finally:
            os.umask(previous)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def copy_release(self):
        temporary = tempfile.TemporaryDirectory(prefix='pacman-case-')
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / 'release'
        shutil.copytree(self.base, path)
        return path

    def sign(self, directory, name):
        apt.run('gpg', '--batch', '--yes', '--homedir', self.home, '--pinentry-mode', 'loopback', '--passphrase', '',
                '--local-user', self.fingerprint, '--digest-algo', 'SHA256', '--output', directory / (name + '.sig'),
                '--detach-sign', directory / name)

    def refresh_manifest(self, directory):
        path = directory / arch.MANIFEST
        manifest = json.loads(path.read_text())
        manifest['assets'] = {name: apt.sha256(directory / name) for name in manifest['assets']}
        path.write_bytes(distro.document(manifest))
        self.sign(directory, arch.MANIFEST)

    def test_signed_native_repository_has_four_packages_and_two_databases(self):
        manifest = arch.verify(self.base, self.metadata, self.repository, self.fingerprint)
        self.assertEqual(len(arch.asset_names(self.metadata)), 21)
        self.assertEqual(set(manifest['packages']), {f'linux-{architecture}-{backend}'
                         for architecture in arch.ARCHES for backend in arch.BACKENDS})
        for architecture in arch.ARCHES:
            descs = tar_files(self.base / f'datapump-{architecture}.db')
            files_db = tar_files(self.base / f'datapump-{architecture}.files')
            self.assertEqual(len(descs), 2)
            self.assertEqual(len(files_db), 4)
            for backend in arch.BACKENDS:
                row = manifest['packages'][f'linux-{architecture}-{backend}']
                desc = database_fields(descs[f'{row["package"]}-{row["version"]}/desc'][0])
                self.assertEqual(desc['FILENAME'], [row['name']])
                self.assertNotIn('/', desc['FILENAME'][0])  # Required by libalpm, even for a Latest mirror.
                self.assertEqual(desc['SHA256SUM'], [apt.sha256(self.base / row['name'])])
                self.assertEqual(desc['ARCH'], [architecture])
                self.assertEqual(desc['DATA'], ['pkgtype=pkg'])
                self.assertIn('alsa-plugins', desc['DEPENDS'])
                self.assertIn('glibc>=2.36' if architecture == 'x86_64' else 'glibc>=2.35', desc['DEPENDS'])
                self.assertTrue(desc['PGPSIG'][0])
                listing = database_fields(files_db[f'{row["package"]}-{row["version"]}/files'][0])['FILES']
                self.assertIn(f'opt/datapump/{backend}/lib/example.so', listing)
                self.assertNotIn('.PKGINFO', listing)

    def test_native_payload_mtree_and_modes_preserve_the_portable_archive(self):
        for row in self.manifest['packages'].values():
            backend = row['backend']
            files = tar_files(self.base / row['name'])
            fields = dict(line.split(' = ', 1) for line in files['.PKGINFO'][0].decode().splitlines()
                          if not line.startswith('depend = '))
            self.assertEqual(fields['pkgname'], f'datapump-{backend}-bin')
            self.assertEqual(fields['pkgver'], distro.distro_version(self.metadata) + '-1')
            self.assertEqual(fields['xdata'], 'pkgtype=pkg')
            self.assertEqual(fields['license'], 'LicenseRef-DataPump-Bundled')
            self.assertNotIn('.BUILDINFO', files)
            self.assertEqual(files[f'opt/datapump/{backend}/lib/example.so'], (b'private binary library\x00', 0o644))
            self.assertEqual(files[f'opt/datapump/{backend}/share/doc/datapump/LICENSE'][1], 0o644)
            self.assertEqual(files[f'usr/bin/datapump-cli-{backend}'][1], 0o755)
            self.assertIn(b'AudioVideo;Audio;', files[f'usr/share/applications/datapump-{backend}.desktop'][0])
            self.assertIn(b'fixture original license', files[f'usr/share/licenses/datapump-{backend}-bin/LICENSE'][0])
            mtree = gzip.decompress(files['.MTREE'][0]).decode()
            self.assertIn('license\\040with\\040spaces.txt', mtree)
            self.assertIn(f'./opt/datapump/{backend}/lib/example.so uid=0 gid=0', mtree)
            self.assertIn('sha256digest=' + hashlib.sha256(b'private binary library\x00').hexdigest(), mtree)
            with tarfile.open(self.base / row['name']) as archive:
                self.assertTrue(all(member.mode == 0o755 for member in archive if member.isdir()))

    def test_regular_latest_and_experiment_pinned_configs_require_both_signatures(self):
        for architecture in arch.ARCHES:
            pinned = arch.config(self.metadata, self.repository, architecture).decode()
            rolling = arch.config(dict(self.metadata, experiment=False), self.repository, architecture).decode()
            self.assertIn('/releases/download/' + self.metadata['tag'], pinned)
            self.assertNotIn('/latest/', pinned)
            self.assertIn('/releases/latest/download', rolling)
            self.assertIn('SigLevel = PackageRequired DatabaseRequired', rolling)
            self.assertIn('retry pacman -Syu', rolling)
            self.assertNotIn('retry pacman -Sy.', rolling)

    def test_rejects_untrusted_key_changed_identity_and_unsafe_assets(self):
        with self.assertRaisesRegex(ValueError, 'Untrusted'):
            arch.verify(self.base, self.metadata, self.repository, '0' * 40)
        metadata = dict(self.metadata, packager_sha='c' * 40)
        with self.assertRaisesRegex(ValueError, 'identity'):
            arch.verify(self.base, metadata, self.repository, self.fingerprint)
        directory = self.copy_release()
        path = directory / 'datapump-x86_64.db'
        path.unlink()
        path.symlink_to(self.base / path.name)
        with self.assertRaisesRegex(ValueError, 'unsafe'):
            arch.verify(directory, self.metadata, self.repository, self.fingerprint)
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            arch.build(self.base, self.metadata, self.repository, self.key, self.fingerprint)

    def test_rejects_invalid_detached_signature_even_with_resigned_inventory(self):
        directory = self.copy_release()
        name = next(iter(self.manifest['packages'].values()))['name']
        (directory / (name + '.sig')).write_bytes(b'not a valid detached signature')
        self.refresh_manifest(directory)
        with self.assertRaises(subprocess.CalledProcessError):
            arch.verify(directory, self.metadata, self.repository, self.fingerprint)
        (directory / arch.MANIFEST).write_bytes(b'{}\n')
        with self.assertRaises(subprocess.CalledProcessError):
            arch.verify(directory, self.metadata, self.repository, self.fingerprint)

    def test_rejects_even_validly_signed_package_payload_tampering(self):
        directory = self.copy_release()
        row = next(iter(self.manifest['packages'].values()))
        files = tar_files(directory / row['name'])
        files[f'opt/datapump/{row["backend"]}/lib/example.so'] = (b'malicious replacement', 0o644)
        (directory / row['name']).write_bytes(arch.archive_bytes(files, row['epoch']))
        self.sign(directory, row['name'])
        self.refresh_manifest(directory)
        with self.assertRaisesRegex(ValueError, 'payload differs'):
            arch.verify(directory, self.metadata, self.repository, self.fingerprint)

    def test_rejects_even_validly_signed_database_and_config_tampering(self):
        for name in ('datapump-x86_64.db', 'datapump-pacman-x86_64.conf'):
            with self.subTest(asset=name):
                directory = self.copy_release()
                path = directory / name
                if name.endswith('.db'):
                    files = tar_files(path)
                    key = next(iter(files))
                    files[key] = (files[key][0].replace(b'%FILENAME%\n', b'%FILENAME%\n../../'), 0o644)
                    row = next(iter(self.manifest['packages'].values()))
                    path.write_bytes(arch.archive_bytes(files, row['epoch']))
                    self.sign(directory, name)
                else:
                    path.write_bytes(path.read_bytes().replace(b'DatabaseRequired', b'DatabaseNever'))
                self.refresh_manifest(directory)
                with self.assertRaisesRegex(ValueError, 'payload differs|configuration differs'):
                    arch.verify(directory, self.metadata, self.repository, self.fingerprint)

    def test_original_source_changes_and_duplicate_archive_members_are_rejected(self):
        directory = self.copy_release()
        row = next(iter(self.manifest['packages'].values()))
        (directory / row['archive']).write_bytes(b'damaged source archive')
        with self.assertRaises((ValueError, tarfile.TarError)):
            arch.verify(directory, self.metadata, self.repository, self.fingerprint)
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w:gz') as archive:
            for _ in range(2):
                member = tarfile.TarInfo('duplicate')
                member.size, member.mode, member.mtime = 1, 0o644, row['epoch']
                archive.addfile(member, io.BytesIO(b'x'))
        path = directory / 'duplicate.tar.gz'
        path.write_bytes(buffer.getvalue())
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            arch.check_archive(path, {'duplicate': (b'x', 0o644)}, row['epoch'])


@unittest.skipUnless(getattr(os, 'geteuid', lambda: -1)() == 0 and all(shutil.which(name) for name in ('pacman', 'gpg', 'gpgv')),
                     'Native pacman integration runs as root inside the disposable Arch CI container')
class NativePacmanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='pacman-native-tests-')
        cls.root = Path(cls.temporary.name)
        cls.home = cls.root / 'gpg'
        cls.home.mkdir(mode=0o700)
        apt.run('gpg', '--batch', '--homedir', cls.home, '--pinentry-mode', 'loopback', '--passphrase', '',
                '--quick-generate-key', 'DataPump disposable native update signer', 'ed25519', 'sign', '0')
        cls.public = cls.root / 'public.gpg'
        cls.public.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--export').stdout)
        cls.fingerprint = apt.fingerprint(cls.public)
        cls.key = cls.root / 'signing.asc'
        cls.key.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--armor', '--export-secret-keys').stdout)
        cls.key.chmod(0o600)
        cls.releases = {}
        cls.metadata = {}
        instant = datetime.now(timezone.utc).replace(microsecond=0) - timedelta(minutes=2)
        for index, label in enumerate(('A', 'B')):
            metadata = release.make_metadata(source_sha='a' * 40, packager_sha='b' * 40,
                run_id=str(123 + index), run_attempt='1', cmake_version='0.7.2', experiment=False,
                schema=5, now=instant + timedelta(minutes=index))
            directory = cls.root / label
            directory.mkdir()
            for target, name in release.application_names(metadata).items():
                if target.startswith('linux-'):
                    fixture_archive(directory / name, metadata, target)
            arch.build(directory, metadata, 'fixture/project', cls.key, cls.fingerprint)
            cls.releases[label], cls.metadata[label] = directory, metadata

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='pacman-isolated-root-')
        self.addCleanup(temporary.cleanup)
        self.installed = Path(temporary.name)
        for name in ('db/local', 'cache', 'gnupg', 'hooks', 'root'):
            (self.installed / name).mkdir(parents=True, mode=0o700)
        keyhome = self.installed / 'gnupg'
        apt.run('gpg', '--batch', '--homedir', keyhome, '--import', self.public)
        apt.run('gpg', '--batch', '--homedir', keyhome, '--import-ownertrust',
                input=f'{self.fingerprint}:6:\n'.encode())
        apt.run('gpg', '--batch', '--homedir', keyhome, '--check-trustdb')
        self.state = {'database': 'A', 'packages': 'A', 'failure': None, 'requests': []}
        state, releases = self.state, self.releases

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                name = unquote(urlparse(self.path).path).removeprefix('/releases/latest/download/')
                state['requests'].append(name)
                is_database = name in ('datapump-x86_64.db', 'datapump-x86_64.db.sig')
                label = state['database'] if is_database else state['packages']
                path = releases[label] / name
                failure = state['failure']
                if ('/' in name or not path.is_file()
                        or (is_database and failure == 'missing')):
                    self.send_error(404)
                    return
                data = path.read_bytes()
                if name.endswith('.db') and failure == 'tampered':
                    data = bytes([data[0] ^ 1]) + data[1:]
                elif name.endswith('.db.sig') and failure == 'wrong-signature':
                    other = 'A' if label == 'B' else 'B'
                    data = (releases[other] / name).read_bytes()
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *_args):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()

        def stop_server():
            self.server.shutdown()
            thread.join(timeout=5)
            self.server.server_close()

        self.addCleanup(stop_server)
        self.config = self.installed / 'pacman.conf'
        self.config.write_text('[options]\nArchitecture = x86_64\nSigLevel = Required\n'
            f'RootDir = {self.installed}/root/\nDBPath = {self.installed}/db/\n'
            f'CacheDir = {self.installed}/cache/\nGPGDir = {keyhome}/\n'
            f'HookDir = {self.installed}/hooks/\nLogFile = {self.installed}/pacman.log\n'
            '[datapump-x86_64]\nSigLevel = PackageRequired DatabaseRequired\n'
            f'Server = http://127.0.0.1:{self.server.server_port}/releases/latest/download\n')

    def pacman(self, *arguments, success=True):
        # Dependency bypass is confined to this isolated synthetic repository;
        # the separate live release installation verifies actual dependencies.
        command = ['pacman', '--config', str(self.config), '--noconfirm', *arguments]
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, 'Invalid repository unexpectedly succeeded:\n' + result.stdout)
        return result

    def sync(self, *packages, success=True):
        return self.pacman('-Syu', '--nodeps', '--nodeps', *packages, success=success)

    def assert_version(self, label):
        wanted = distro.distro_version(self.metadata[label]) + '-1'
        for backend in arch.BACKENDS:
            package = f'datapump-{backend}-bin'
            self.assertEqual(self.pacman('-Q', package).stdout.strip(), f'{package} {wanted}')
            self.assertTrue((self.installed / f'root/opt/datapump/{backend}/lib/example.so').is_file())

    def test_native_latest_upgrade_and_bad_database_rejection(self):
        self.sync('datapump-fltk-bin', 'datapump-rev-bin')
        self.assert_version('A')
        self.state.update(database='B', packages='B')
        self.sync()
        self.assert_version('B')
        self.pacman('-Qkk', 'datapump-fltk-bin', 'datapump-rev-bin')
        for failure in ('tampered', 'missing', 'wrong-signature'):
            with self.subTest(failure=failure):
                self.state['failure'] = failure
                self.sync(success=False)
                self.assert_version('B')
        self.state['failure'] = None
        self.sync()
        self.assert_version('B')

    def test_native_stale_latest_database_fails_then_full_refresh_installs_current_packages(self):
        self.sync()  # Save A's database in the otherwise empty isolated root.
        self.state['packages'] = 'B'
        self.pacman('-S', '--nodeps', '--nodeps', 'datapump-fltk-bin', 'datapump-rev-bin', success=False)
        for backend in arch.BACKENDS:
            self.pacman('-Q', f'datapump-{backend}-bin', success=False)
        # libalpm can stop the transaction after the first missing package.
        self.assertTrue(any(arch.package_name(self.metadata['A'], 'x86_64', backend)
                            in self.state['requests'] for backend in arch.BACKENDS))
        self.state['database'] = 'B'
        self.sync('datapump-fltk-bin', 'datapump-rev-bin')
        self.assert_version('B')


if __name__ == '__main__':
    unittest.main()
