#!/usr/bin/env python3
"""Signed release channel updates without Portage, application, or SDK builds."""
import contextlib
import hashlib
import http.server
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
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


channel = load('gentoo_release', ROOT / 'tools/gentoo-release.py')
sync = channel.sync_module()


class CrossPlatformInventoryTests(unittest.TestCase):
    def test_inventory_can_be_loaded_without_linux_fcntl_module(self):
        with mock.patch.dict('sys.modules', {'fcntl': None}):
            self.assertIn(sync.CHANNEL, channel.asset_names({'schema': 5}))


def overlay(path, marker=b'first', unsafe=None):
    files = {'profiles/repo_name': b'datapump-bin\n', 'metadata/layout.conf': b'masters = gentoo\n',
             'README.md': marker}
    for backend in ('fltk', 'rev'):
        files[f'media-radio/datapump-{backend}-bin/datapump-{backend}-bin-0.7.2.ebuild'] = b'EAPI=8\n'
        files[f'media-radio/datapump-{backend}-bin/Manifest'] = b'fixture\n'
    with tarfile.open(path, 'w:gz') as archive:
        for name, data in files.items():
            member = tarfile.TarInfo('datapump-gentoo-overlay/' + name)
            member.size, member.mode = len(data), 0o644
            archive.addfile(member, io.BytesIO(data))
        if unsafe:
            name, kind = unsafe
            member = tarfile.TarInfo(name)
            member.mode = 0o644
            if kind == 'symlink':
                member.type, member.linkname = tarfile.SYMTYPE, '/tmp/escape'
            elif kind == 'hardlink':
                member.type, member.linkname = tarfile.LNKTYPE, 'datapump-gentoo-overlay/README.md'
            elif kind == 'mode':
                member.mode = 0o4755
            archive.addfile(member)


@unittest.skipUnless(shutil.which('gpg') and shutil.which('gpgv'), 'gpg and gpgv required')
class GentooSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory(prefix='gentoo-sync-keys-')
        cls.home = Path(cls.keys.name)
        cls.fingerprints = []
        for name in ('trusted', 'untrusted'):
            subprocess.run(['gpg', '--batch', '--homedir', str(cls.home), '--pinentry-mode', 'loopback',
                            '--passphrase', '', '--quick-generate-key', f'{name}@example.invalid',
                            'ed25519', 'sign', '0'], check=True, capture_output=True)
            listing = subprocess.run(['gpg', '--batch', '--homedir', str(cls.home), '--with-colons',
                                      '--list-keys', f'{name}@example.invalid'], check=True,
                                     capture_output=True, text=True).stdout
            fpr = next(line.split(':')[9] for line in listing.splitlines() if line.startswith('fpr:'))
            cls.fingerprints.append(fpr)
            public = subprocess.run(['gpg', '--batch', '--homedir', str(cls.home), '--export', fpr],
                                    check=True, capture_output=True).stdout
            secret = subprocess.run(['gpg', '--batch', '--homedir', str(cls.home), '--export-secret-keys', fpr],
                                    check=True, capture_output=True).stdout
            (cls.home / f'{name}.gpg').write_bytes(public)
            (cls.home / f'{name}.private').write_bytes(secret)

    @classmethod
    def tearDownClass(cls):
        cls.keys.cleanup()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='gentoo-sync-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.location = self.root / 'installed'
        self.repository = 'owner/project'
        self.fpr = self.fingerprints[0]
        self.keyring = self.home / 'trusted.gpg'
        self.routes, self.requests = {}, []
        test = self
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                test.requests.append(self.path)
                value = test.routes.get(self.path)
                if callable(value):
                    value = value()
                if value is None:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.end_headers()
                self.wfile.write(value)
            def log_message(self, *args):
                pass
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.stop_server)
        self.base = f'http://127.0.0.1:{self.server.server_port}'

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def release(self, number=1, experiment=False, unsafe=None, signer='trusted'):
        assets = self.root / f'release-{number}-{signer}'
        assets.mkdir()
        metadata = {'schema': 5, 'tag': f'v001_00-2026-09-23-120{number}CDT',
                    'created_at': f'2026-09-23T17:0{number}:00Z', 'project_version': '0.7.2',
                    'version': 'v001_00', 'run_id': str(100 + number), 'run_attempt': '1',
                    'experiment': experiment}
        overlay(assets / sync.OVERLAY, str(number).encode(), unsafe)
        fpr = self.fingerprints[signer == 'untrusted']
        manifest = channel.build(assets, metadata, self.repository, self.home / f'{signer}.private', fpr)
        shutil.copyfile(self.home / f'{signer}.gpg', assets / 'datapump-archive-keyring.gpg')
        prefix = f'/{self.repository}/releases/download/{metadata["tag"]}/'
        for path in assets.iterdir():
            self.routes[prefix + path.name] = path.read_bytes()
        self.routes[f'/{self.repository}/releases/latest/download/{sync.CHANNEL}'] = (assets / sync.CHANNEL).read_bytes()
        return assets, metadata, manifest

    def update(self, tag=None):
        return sync.sync(self.repository, self.location, self.keyring, self.fpr, tag, self.base)

    def test_signed_build_and_offline_verification_bind_client_adapter_and_metadata(self):
        assets, metadata, manifest = self.release()
        self.assertEqual(channel.verify(assets, metadata, self.repository, self.fpr), manifest)
        self.assertEqual(len(channel.asset_names(metadata)), 4)
        self.assertEqual(channel.asset_names({'schema': 4}), set())
        with self.assertRaisesRegex(ValueError, 'metadata'):
            channel.verify(assets, dict(metadata, source_sha='b' * 40), self.repository, self.fpr)
        with (assets / sync.ADAPTER_NAME).open('ab') as output:
            output.write(b'# changed\n')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            channel.verify(assets, metadata, self.repository, self.fpr)

    def test_build_preserves_existing_assets_and_rejects_dangling_symlinks(self):
        assets, metadata, _ = self.release()
        original = (assets / sync.CHANNEL).read_bytes()
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            channel.build(assets, metadata, self.repository, self.home / 'trusted.private', self.fpr)
        self.assertEqual((assets / sync.CHANNEL).read_bytes(), original)
        empty = self.root / 'dangling'
        empty.mkdir()
        (empty / sync.CHANNEL).symlink_to('missing')
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            channel.build(empty, metadata, self.repository, self.home / 'trusted.private', self.fpr)

    def test_signed_repository_mismatch_does_not_create_an_overlay(self):
        assets, metadata, _ = self.release()
        data = sync.strict_json((assets / sync.CHANNEL).read_bytes())
        data['repository'] = 'another/project'
        (assets / sync.CHANNEL).write_bytes(sync.canonical(data))
        channel.sign(assets, self.home / 'trusted.private', self.fpr)
        self.routes[f'/{self.repository}/releases/latest/download/{sync.CHANNEL}'] = (assets / sync.CHANNEL).read_bytes()
        self.routes[f'/{self.repository}/releases/download/{metadata["tag"]}/{sync.CHANNEL}.asc'] = (assets / (sync.CHANNEL + '.asc')).read_bytes()
        with self.assertRaisesRegex(ValueError, 'repository mismatch'):
            self.update()
        self.assertFalse(self.location.exists())

    def test_latest_movement_uses_immutable_signature_and_payload_then_updates_atomically(self):
        first, old, _ = self.release(1)
        second, new, _ = self.release(2)
        latest_path = f'/{self.repository}/releases/latest/download/{sync.CHANNEL}'
        def move_latest():
            self.routes[latest_path] = (second / sync.CHANNEL).read_bytes()
            return (first / sync.CHANNEL).read_bytes()
        self.routes[latest_path] = move_latest
        self.assertTrue(self.update()['changed'])
        self.assertEqual((self.location / 'README.md').read_bytes(), b'1')
        self.assertTrue(all('/latest/' not in path for path in self.requests[1:]))
        self.assertTrue(self.update()['changed'])
        self.assertEqual((self.location / 'README.md').read_bytes(), b'2')
        self.assertFalse(self.update()['changed'])
        self.assertEqual(self.update()['tag'], new['tag'])
        self.assertFalse(list(self.root.glob('.installed.staging-*')))

    def test_wrong_key_and_tampered_signature_preserve_old_overlay(self):
        _, old, _ = self.release(1)
        self.update()
        self.release(2, signer='untrusted')
        with self.assertRaisesRegex(ValueError, 'trusted key'):
            self.update()
        self.assertEqual((self.location / 'README.md').read_bytes(), b'1')
        assets, metadata, _ = self.release(3)
        latest = f'/{self.repository}/releases/latest/download/{sync.CHANNEL}'
        self.routes[latest] = (assets / sync.CHANNEL).read_bytes().replace(b'0.7.2', b'0.7.3')
        with self.assertRaisesRegex(ValueError, 'trusted key'):
            self.update()
        self.assertEqual((self.location / 'README.md').read_bytes(), b'1')

    def test_overlay_tamper_and_rollback_preserve_old_overlay(self):
        first, _, _ = self.release(1)
        second, metadata, _ = self.release(2)
        self.update()
        self.routes[f'/{self.repository}/releases/latest/download/{sync.CHANNEL}'] = (first / sync.CHANNEL).read_bytes()
        with self.assertRaisesRegex(ValueError, 'downgrade'):
            self.update()
        third, metadata, _ = self.release(3)
        self.routes[f'/{self.repository}/releases/download/{metadata["tag"]}/{sync.OVERLAY}'] = b'tamper'
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            self.update()
        self.assertEqual((self.location / 'README.md').read_bytes(), b'2')

    def test_experiment_requires_exact_explicit_tag_and_latest_never_accepts_it(self):
        _, metadata, _ = self.release(experiment=True)
        with self.assertRaisesRegex(ValueError, 'Latest'):
            self.update()
        self.assertTrue(self.update(metadata['tag'])['changed'])
        with self.assertRaisesRegex(ValueError, 'Pinned release tag'):
            manifest = self.routes[f'/{self.repository}/releases/latest/download/{sync.CHANNEL}']
            sync.validate_manifest(manifest, self.repository, self.fpr, 'wrong-tag')

    def test_signed_unsafe_archives_are_rejected_before_replacing_the_overlay(self):
        self.release(1)
        self.update()
        for index, unsafe in enumerate([
                ('datapump-gentoo-overlay/../../escape', 'file'),
                ('/absolute', 'file'), ('datapump-gentoo-overlay/link', 'symlink'),
                ('datapump-gentoo-overlay/hard', 'hardlink'),
                ('datapump-gentoo-overlay/setuid', 'mode'),
                ('datapump-gentoo-overlay/README.md', 'file')], 2):
            with self.subTest(unsafe=unsafe):
                self.release(index, unsafe=unsafe)
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    self.update()
                self.assertEqual((self.location / 'README.md').read_bytes(), b'1')
                self.assertFalse(list(self.root.glob('.installed.staging-*')))

    def test_local_modification_and_atomic_swap_failure_do_not_destroy_previous_release(self):
        self.release(1)
        self.update()
        self.release(2)
        with mock.patch.object(sync, 'exchange_directories', side_effect=OSError('fixture exchange failure')):
            with self.assertRaisesRegex(OSError, 'exchange failure'):
                self.update()
        self.assertEqual((self.location / 'README.md').read_bytes(), b'1')
        (self.location / 'README.md').write_bytes(b'local edit')
        with self.assertRaisesRegex(ValueError, 'changed since'):
            self.update()
        self.assertEqual((self.location / 'README.md').read_bytes(), b'local edit')

    def test_transport_and_manifest_validation_reject_unsafe_urls_and_duplicate_fields(self):
        for url in ('http://example.com/release', 'file:///etc/passwd', 'https://user:pass@example.com/file'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                sync.checked_url(url)
        with self.assertRaisesRegex(ValueError, 'loopback'):
            sync.release_base(self.repository, test_base_url='http://example.com')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            sync.strict_json(b'{"schema":1,"schema":1}')


if __name__ == '__main__':
    unittest.main()
