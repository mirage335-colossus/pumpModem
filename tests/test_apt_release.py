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
import zlib
from urllib.parse import unquote
from web_delivery_fixture import WEB, payload as web_payload

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
             'share/doc/datapump/build-info.txt': f'Source commit: {metadata["source_sha"]}\nGUI: ON ({backend})\n'.encode()}
    files.update({f'share/man/man1/{name}.1': (ROOT / f'docs/man/{name}.1').read_bytes()
                  for name in ('pump', 'pump-fast', 'datapump-gui')})
    if metadata.get('frontends'):
        files['share/doc/datapump/build-info.txt'] += b'TUI: ON (ncurses)\nFramebuffer: ON (sdl)\n'
        for name in ('datapump-tui', 'datapump-fb'):
            files[f'bin/{name}'] = b'#!/bin/sh\nexit 0\n'
            files[f'share/man/man1/{name}.1'] = (ROOT / f'docs/man/{name}.1').read_bytes()
    if metadata.get('web'):
        files.update({name: data for name, (data, _) in web_payload(metadata).items()})
        files['share/doc/datapump/build-info.txt'] += b'Web worker: ON (inherited-pipes; no sockets)\n'
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
    def test_browser_launchers_open_only_the_coinstalled_page(self):
        payload = {'share/datapump/web/wasm/datapump-wasm.html': (b'local HTML fixture', 0o644)}
        commands = []
        for backend, command in (('fltk', 'datapump-html'), ('rev', 'datapump-html-rev')):
            files = apt.package_files(payload, backend)
            commands.append(command)
            wrapper, mode = files[f'usr/bin/{command}']
            self.assertEqual(mode, 0o755)
            self.assertEqual(wrapper.decode(), '#!/bin/sh\nexec xdg-open '
                             f'/opt/datapump/{backend}/share/datapump/web/wasm/datapump-wasm.html\n')
            desktop = files[f'usr/share/applications/{command}.desktop'][0].decode()
            self.assertIn(f'Exec={command}\n', desktop)
            self.assertIn('TryExec=xdg-open\n', desktop)
            self.assertIn('Categories=AudioVideo;Audio;\n', desktop)
            with tempfile.TemporaryDirectory(prefix='html-launcher-') as temporary:
                root = Path(temporary)
                launcher = root / command
                launcher.write_bytes(wrapper)
                opener = root / 'xdg-open'
                opener.write_text('#!/bin/sh\nprintf "%s\\n" "$#" "$@"\n')
                opener.chmod(0o755)
                opened = subprocess.run([shutil.which('sh'), str(launcher), 'https://unused.invalid'],
                    env=dict(os.environ, PATH=str(root)), capture_output=True, text=True, check=True)
                self.assertEqual(opened.stdout.splitlines(), ['1',
                    f'/opt/datapump/{backend}/share/datapump/web/wasm/datapump-wasm.html'])
        self.assertEqual(len(set(commands)), 2)
        historical = apt.package_files({}, 'fltk')
        self.assertNotIn('usr/bin/datapump-html', historical)

    def test_manual_names_and_references_follow_the_coinstallable_wrappers(self):
        source = (b'.TH PUMP 1 "September 2026" "Data Pump 001_00" "User Commands"\n'
                  b'.SH NAME\npump \\- audio modem\n'
                  b'.BR pump\\-fast (1),\n.BR datapump-gui (1)\n'
                  b'\\fBpump\\fR --help\n')
        payload = {f'share/man/man1/{name}.1': (source, 0o644)
                   for name in ('pump', 'pump-fast', 'datapump-gui')}
        for backend in apt.BACKENDS:
            files = apt.package_files(payload, backend)
            installed = f'usr/share/man/man1/datapump-cli-{backend}.1.gz'
            text = gzip.decompress(files[installed][0]).decode()
            self.assertIn(f'.TH DATAPUMP-CLI-{backend.upper()} 1', text)
            self.assertIn('"Data Pump 001_00"', text)
            self.assertIn(f'datapump-cli-{backend} \\- audio modem', text)
            self.assertIn(f'.BR datapump\\-cli\\-{backend}\\-fast (1)', text)
            self.assertIn(f'.BR datapump-{backend} (1)', text)
            self.assertIn(f'\\fBdatapump-cli-{backend}\\fR --help', text)
            self.assertEqual(files[installed][1], 0o644)
            self.assertEqual(files[f'opt/datapump/{backend}/share/man/man1/pump.1'], (source, 0o644))
            self.assertEqual(len([name for name in files if name.startswith('usr/share/man/')]), 3)

    def test_optional_frontends_get_independent_coinstallable_commands(self):
        payload = {f'share/man/man1/{name}.1': (f'.TH {name.upper()} 1\n{name}\n'.encode(), 0o644)
                   for name in ('pump', 'pump-fast', 'datapump-gui', 'datapump-tui', 'datapump-fb')}
        for name in ('datapump-tui', 'datapump-fb'):
            payload[f'bin/{name}'] = (b'frontend', 0o755)
        first, second = (apt.package_files(payload, backend) for backend in ('fltk', 'rev'))
        self.assertFalse(set(p for p in first if p.startswith('usr/bin/')) &
                         set(p for p in second if p.startswith('usr/bin/')))
        for backend, files, suffix in (('fltk', first, ''), ('rev', second, '-rev')):
            for binary, adapter in (('datapump-tui', 'ncurses'), ('datapump-fb', 'sdl')):
                command = f'{binary}-{adapter}{suffix}'
                self.assertIn(f'/opt/datapump/{backend}/bin/{binary}'.encode(), files[f'usr/bin/{command}'][0])
                manual = gzip.decompress(files[f'usr/share/man/man1/{command}.1.gz'][0])
                self.assertIn(command.encode(), manual)
        del payload['share/man/man1/datapump-tui.1']
        with self.assertRaisesRegex(ValueError, 'incomplete manual set'):
            apt.package_files(payload, 'fltk')

    def test_pipe_worker_commands_and_manuals_can_coexist(self):
        payload = {f'share/man/man1/{name}.1': (f'.TH {name.upper()} 1\n{name}\n'.encode(), 0o644)
                   for name in ('pump', 'pump-fast', 'datapump-gui', 'datapump-worker')}
        payload['bin/datapump-worker'] = (b'worker', 0o755)
        first, second = (apt.package_files(payload, backend) for backend in ('fltk', 'rev'))
        for backend, files, command in (('fltk', first, 'datapump-worker'),
                                        ('rev', second, 'datapump-worker-rev')):
            wrapper = files[f'usr/bin/{command}'][0]
            self.assertIn(f'/opt/datapump/{backend}/bin/datapump-worker'.encode(), wrapper)
            manual = gzip.decompress(files[f'usr/share/man/man1/{command}.1.gz'][0])
            self.assertIn(command.encode(), manual)
            self.assertFalse(any('systemd' in path for path in files))
        self.assertFalse({p for p in first if p.startswith('usr/bin/')} &
                         {p for p in second if p.startswith('usr/bin/')})
        del payload['share/man/man1/datapump-worker.1']
        with self.assertRaisesRegex(ValueError, 'incomplete manual set'):
            apt.package_files(payload, 'fltk')

    def test_historical_archives_remain_repackagable_but_partial_manuals_fail(self):
        self.assertEqual(apt.manual_files({}, 'fltk'), {})
        with self.assertRaisesRegex(ValueError, 'incomplete manual set'):
            apt.manual_files({'share/man/man1/pump.1': (b'manual', 0o644)}, 'fltk')

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

    def test_declared_frontends_survive_signed_debian_repository_packaging(self):
        directory = self.root / 'frontend-release'
        directory.mkdir()
        values = release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1',
            cmake_version='0.7.2', version='v001_00', experiment=True,
            frontends=['tui', 'framebuffer'],
            dependencies={'linux-sdk': '1' * 20, 'windows-base': '2' * 20},
            now=datetime.now(timezone.utc).replace(microsecond=0))
        for target, name in release.application_names(values).items():
            if target.startswith('linux-'):
                fixture_archive(directory / name, values, target)
        # Native recipe/channel semantics have their own suites; the APT
        # signature must bind their exact retained bytes too.
        for name in release.distribution_assets(values):
            (directory / name).write_bytes(('distribution fixture ' + name).encode())
        apt.build(directory, values, 'test-owner/test-repo', self.key, self.fingerprint)
        apt.verify(directory, values, 'test-owner/test-repo', self.fingerprint)
        for backend in apt.BACKENDS:
            suffix = '' if backend == 'fltk' else '-rev'
            for arch in apt.ARCHES:
                entries = apt.deb_files(directory / apt.package_name(values, arch, backend))
                for binary, adapter in (('datapump-tui', 'ncurses'), ('datapump-fb', 'sdl')):
                    command = f'{binary}-{adapter}{suffix}'
                    self.assertIn(f'opt/datapump/{backend}/bin/{binary}', entries)
                    self.assertIn(f'usr/bin/{command}', entries)
                    self.assertIn(f'usr/share/man/man1/{command}.1.gz', entries)

    def test_web_payload_and_launchers_survive_the_signed_debian_repository(self):
        directory = self.root / 'web-release'
        directory.mkdir()
        values = release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1',
            cmake_version='0.7.2', version='v001_00', experiment=True, web=WEB,
            frontends=['tui', 'framebuffer'],
            dependencies={'linux-sdk': '1' * 20, 'windows-base': '2' * 20, 'wasm-sdk': '3' * 20},
            now=datetime.now(timezone.utc).replace(microsecond=0))
        for target, name in release.application_names(values).items():
            if target.startswith('linux-'):
                fixture_archive(directory / name, values, target)
        for name in release.distribution_assets(values):
            (directory / name).write_bytes(('distribution fixture ' + name).encode())
        apt.build(directory, values, 'test-owner/test-repo', self.key, self.fingerprint)
        apt.verify(directory, values, 'test-owner/test-repo', self.fingerprint)
        for backend in apt.BACKENDS:
            suffix = '' if backend == 'fltk' else '-rev'
            for architecture in apt.ARCHES:
                package = directory / apt.package_name(values, architecture, backend)
                entries = apt.deb_files(package)
                for name, (data, mode) in web_payload(values).items():
                    self.assertEqual(entries[f'opt/datapump/{backend}/{name}'], (data, mode))
                for command in ('datapump-html', 'datapump-worker'):
                    self.assertIn(f'usr/bin/{command}{suffix}', entries)
                self.assertIn(f'usr/share/applications/datapump-html{suffix}.desktop', entries)
                depends = apt.run('dpkg-deb', '--field', package, 'Depends').stdout.decode()
                self.assertIn('xdg-utils', depends)
        self.assertNotIn('xdg-utils', apt.control(self.metadata, 'amd64', 'fltk', 1))

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
            packaged = apt.deb_files(self.base / apt.package_name(self.metadata, 'amd64', backend))
            files.append(set(packaged))
            manual = gzip.decompress(packaged[f'usr/share/man/man1/datapump-cli-{backend}.1.gz'][0])
            self.assertIn(f'datapump-cli-{backend}'.encode(), manual)
            self.assertEqual(packaged[f'opt/datapump/{backend}/share/man/man1/pump.1'][0],
                             (ROOT / 'docs/man/pump.1').read_bytes())
        self.assertFalse(files[0] & files[1])

    def test_schema4_through_6_sign_all_distribution_assets(self):
        for schema in (4, 5, 6):
            value = release.make_metadata(source_sha='a' * 40, run_id='124', run_attempt='1', schema=schema,
                cmake_version='0.7.2', experiment=True, now=datetime.now(timezone.utc).replace(microsecond=0),
                dependencies={'linux-sdk': '1' * 20, 'windows-base': '2' * 20} if schema == 6 else None)
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
                # Corrupt DEFLATE bytes may fail before gzip/tar can classify
                # the archive; rejection is still mandatory in that case.
                with self.assertRaises((ValueError, OSError, EOFError, subprocess.CalledProcessError, tarfile.TarError, zlib.error)):
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
