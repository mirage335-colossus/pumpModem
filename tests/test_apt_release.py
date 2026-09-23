#!/usr/bin/env python3
"""Signed repository, immutable download routing and Debian payload regressions."""
from datetime import datetime, timezone
import gzip
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
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
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('apt_release', ROOT / 'tools/apt-release.py')
apt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(apt)
release = apt.release_module()


def fixture_archive(path, metadata, target, additions=()):
    backend = release.target_backend(metadata, target)
    root = release.package_bases(metadata, target)[0]
    files = {'bin/pump': b'#!/bin/sh\nprintf "fixture CLI\\n"\n',
             'bin/datapump-gui': b'#!/bin/sh\nexit 0\n',
             'lib/example.so': b'fixture private library',
             'share/doc/datapump/build-info.txt': f'GUI: ON ({backend})\n'.encode()}
    sums = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, data in files.items())
    files['manifest.sha256'] = sums.encode()
    with tarfile.open(path, 'w:gz') as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(root + '/' + name)
            info.size = len(data)
            info.mode = 0o755 if name.startswith('bin/') else 0o644
            archive.addfile(info, io.BytesIO(data))
        for info in additions:
            archive.addfile(info)


class DesktopEntryTests(unittest.TestCase):
    def test_audio_desktop_entries_include_the_required_parent_category(self):
        # Freedesktop's registered Audio category requires AudioVideo. This
        # shared entry is installed by Debian, Arch and Gentoo packaging.
        for backend in apt.BACKENDS:
            with self.subTest(backend=backend):
                data, mode = apt.package_files({}, backend)[f'usr/share/applications/datapump-{backend}.desktop']
                fields = dict(line.split('=', 1) for line in data.decode().splitlines() if '=' in line)
                categories = set(filter(None, fields['Categories'].split(';')))
                self.assertIn('Audio', categories)
                self.assertIn('AudioVideo', categories)
                self.assertNotIn('Utility', categories)
                self.assertEqual(mode, 0o644)


@unittest.skipUnless(all(shutil.which(name) for name in ('dpkg-deb', 'gpg', 'gpgv', 'apt-get')), 'Linux Debian packaging tools required')
class AptReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='apt-release-tests-')
        cls.root = Path(cls.temp.name)
        cls.home = cls.root / 'gpg'
        cls.home.mkdir(mode=0o700)
        apt.run('gpg', '--batch', '--homedir', cls.home, '--pinentry-mode', 'loopback', '--passphrase', '',
                '--quick-generate-key', 'DataPump disposable test signer', 'ed25519', 'sign', '0')
        public = cls.root / 'public.gpg'
        public.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--export').stdout)
        cls.fingerprint = apt.fingerprint(public)
        cls.key = cls.root / 'signing.asc'
        cls.key.write_bytes(apt.run('gpg', '--batch', '--homedir', cls.home, '--armor', '--export-secret-keys').stdout)
        cls.key.chmod(0o600)
        cls.metadata = release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1',
            cmake_version='0.7.2', version='v001_00', experiment=True, schema=3,
            now=datetime.now(timezone.utc).replace(microsecond=0))
        cls.base = cls.root / 'base'
        cls.base.mkdir()
        for target, name in release.application_names(cls.metadata).items():
            if target.startswith('linux-'):
                fixture_archive(cls.base / name, cls.metadata, target)
        cls.manifest = apt.build(cls.base, cls.metadata, 'test-owner/test-repo', cls.key, cls.fingerprint)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def copy_repository(self):
        temporary = tempfile.TemporaryDirectory(prefix='apt-case-')
        self.addCleanup(temporary.cleanup)
        target = Path(temporary.name) / 'repo'
        shutil.copytree(self.base, target)
        return target

    def test_signed_repository_has_both_architectures_and_coinstallable_backends(self):
        self.assertEqual(len(apt.asset_names(self.metadata)), 12)
        manifest = apt.verify(self.base, self.metadata, 'test-owner/test-repo', self.fingerprint)
        self.assertEqual({(p['architecture'], p['backend']) for p in manifest['packages']},
                         {(arch, backend) for arch in apt.ARCHES for backend in apt.BACKENDS})
        files = []
        for backend in apt.BACKENDS:
            files.append(set(apt.deb_files(self.base / apt.package_name(self.metadata, 'amd64', backend))))
        self.assertFalse(files[0] & files[1])

    def test_schema4_and_5_sign_all_distribution_assets(self):
        for schema in (4, 5):
            value = release.make_metadata(source_sha='a' * 40, run_id='124', run_attempt='1', schema=schema,
                cmake_version='0.7.2', experiment=True, now=datetime.now(timezone.utc).replace(microsecond=0))
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                for target, name in release.application_names(value).items():
                    if target.startswith('linux-'):
                        fixture_archive(directory / name, value, target)
                for name in release.distribution_assets(value):
                    (directory / name).write_bytes(b'distribution fixture')
                manifest = apt.build(directory, value, 'test-owner/test-repo', self.key, self.fingerprint)
                self.assertEqual(set(manifest['distribution_assets']), release.distribution_assets(value))
                apt.verify(directory, value, 'test-owner/test-repo', self.fingerprint)
                (directory / sorted(release.distribution_assets(value))[0]).write_bytes(b'tampered')
                with self.assertRaisesRegex(ValueError, 'distribution recipe checksum'):
                    apt.verify(directory, value, 'test-owner/test-repo', self.fingerprint)

    def test_version_uses_utc_and_legal_debian_characters(self):
        before = dict(self.metadata, created_at='2026-11-01T06:59:00Z')
        after = dict(self.metadata, created_at='2026-11-01T07:00:00Z')
        apt.run('dpkg', '--validate-version', apt.debian_version(before))
        apt.run('dpkg', '--compare-versions', apt.debian_version(before), 'lt', apt.debian_version(after))
        self.assertNotIn('_', apt.debian_version(before))
        self.assertIn('/releases/download/', apt.sources(self.metadata, 'test-owner/test-repo'))
        self.assertIn('/releases/latest/download/', apt.sources(dict(self.metadata, experiment=False), 'test-owner/test-repo'))

    def test_secret_umask_does_not_make_installed_directories_private(self):
        directory = self.copy_repository()
        target = 'linux-x86_64-fltk'
        archive = directory / release.application_names(self.metadata)[target]
        previous = os.umask(0o077)
        try:
            apt.make_deb(directory, self.metadata, 'amd64', 'fltk', archive)
        finally:
            os.umask(previous)
        package = directory / apt.package_name(self.metadata, 'amd64', 'fltk')
        self.assertIn('usr/bin/datapump-fltk', apt.deb_files(package))
        extracted = directory.parent / 'extracted'
        apt.run('dpkg-deb', '--raw-extract', package, extracted)
        (extracted / 'opt/datapump/fltk').chmod(0o777)
        changed = directory.parent / 'bad-mode.deb'
        apt.run('dpkg-deb', '--root-owner-group', '--build', extracted, changed)
        with self.assertRaisesRegex(ValueError, 'directory permissions'):
            apt.deb_files(changed)

    def test_signing_key_and_release_identity_are_bound(self):
        with self.assertRaisesRegex(ValueError, 'Untrusted'):
            apt.verify(self.base, self.metadata, trusted_fingerprint='0' * 40)
        with self.assertRaisesRegex(ValueError, 'identity'):
            apt.verify(self.base, dict(self.metadata, source_sha='b' * 40))
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            apt.build(self.base, self.metadata, 'test-owner/test-repo', self.key, self.fingerprint)

    def test_tampered_metadata_key_packages_and_archives_fail(self):
        archive = self.manifest['packages'][0]['archive']
        package = self.manifest['packages'][0]['name']
        for name in ('Packages', 'Packages.gz', 'apt-repository.json', 'Release', 'InRelease',
                     'Release.gpg', 'datapump.sources', 'datapump-archive-keyring.gpg', package, archive):
            with self.subTest(name=name):
                directory = self.copy_repository()
                path = directory / name
                data = bytearray(path.read_bytes())
                data[len(data) // 2] ^= 0x7f
                path.write_bytes(data)
                with self.assertRaises((ValueError, OSError, EOFError, subprocess.CalledProcessError, tarfile.TarError)):
                    apt.verify(directory, self.metadata, trusted_fingerprint=self.fingerprint)

    def test_links_traversal_duplicate_and_privileged_archive_entries_fail(self):
        directory = self.copy_repository()
        target = 'linux-x86_64-fltk'
        name = release.application_names(self.metadata)[target]
        root = release.package_bases(self.metadata, target)[0]
        fixtures = []
        symlink = tarfile.TarInfo(root + '/lib/link')
        symlink.type = tarfile.SYMTYPE
        symlink.linkname = '/etc/passwd'
        fixtures.append(symlink)
        fixtures.append(tarfile.TarInfo(root + '/../escape'))
        fixtures.append(tarfile.TarInfo(root + '/bin/pump'))
        privileged = tarfile.TarInfo(root + '/bin/root')
        privileged.mode = 0o4755
        fixtures.append(privileged)
        for member in fixtures:
            with self.subTest(name=member.name):
                fixture_archive(directory / name, self.metadata, target, [member])
                with self.assertRaises(ValueError):
                    apt.archive_files(directory / name, self.metadata, target)

    def test_real_apt_update_and_download_survive_latest_moving(self):
        # Exercise APT itself, with all state confined to a temporary directory.
        # No installed packages, system sources, or signing trust are modified.
        directory = self.copy_repository()
        root = directory.parent
        tag = self.metadata['tag']
        package = apt.package_name(self.metadata, 'amd64', 'fltk')
        requests = []
        class Handler(SimpleHTTPRequestHandler):
            moved = False
            def log_message(self, *_args):
                pass
            def do_GET(self):
                requests.append(self.path)
                self.path = os.path.normpath(self.path)
                if '/releases/latest/download/' in self.path:
                    if self.moved:
                        self.send_error(404, 'Latest moved to another release')
                    else:
                        self.send_response(302)
                        self.send_header('Location', self.path.replace('/latest/download/', f'/download/{tag}/'))
                        self.end_headers()
                    return
                super().do_GET()
            def translate_path(self, path):
                # GitHub and standard HTTP servers normalize dot segments.
                normalized = os.path.normpath(unquote(path.split('?', 1)[0]))
                prefix = f'/test-owner/test-repo/releases/download/{tag}/'
                if not normalized.startswith(prefix):
                    return str(root / 'missing')
                return str(directory / normalized[len(prefix):])
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        source = root / 'apt.sources'
        source.write_text(f'Types: deb\nURIs: http://127.0.0.1:{server.server_port}/test-owner/test-repo/releases/latest/download/\n'
                          f'Suites: ./\nArchitectures: amd64\nSigned-By: {directory}/datapump-archive-keyring.gpg\n')
        (root / 'state/lists/partial').mkdir(parents=True)
        (root / 'cache/archives/partial').mkdir(parents=True)
        (root / 'status').touch()
        args = ['apt-get', '-o', f'Dir::State={root}/state', '-o', f'Dir::State::status={root}/status',
                '-o', f'Dir::Cache={root}/cache', '-o', f'Dir::Etc::sourcelist={source}',
                '-o', 'Dir::Etc::sourceparts=-', '-o', 'APT::Architecture=amd64',
                '-o', 'Acquire::Languages=none', '-o', 'APT::Get::List-Cleanup=0',
                '-o', f'APT::Sandbox::User={os.environ.get("USER", "root")}']
        result = apt.run(*args, 'update', cwd=root)
        self.assertNotIn(b'Failed to fetch', result.stderr)
        Handler.moved = True
        try:
            apt.run(*args, 'download', 'datapump-fltk=' + apt.debian_version(self.metadata), cwd=root)
        except subprocess.CalledProcessError as error:
            self.fail(f'APT download failed: {error.stdout!r} {error.stderr!r}; requests={requests!r}')
        self.assertEqual(apt.sha256(root / package), apt.sha256(directory / package))
        self.assertTrue(any(package in unquote(path) for path in requests))


if __name__ == '__main__':
    unittest.main()
