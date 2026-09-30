#!/usr/bin/env python3
"""Exercise offline Wasm SDK archive integrity, relocation and immutable reuse."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wasm_sdk_release', ROOT / 'tools/wasm-sdk-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class WasmSdkReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='wasm sdk release ')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sdk = self.root / 'original sdk'
        self.sdk.mkdir()
        self.recipe = copy.deepcopy(release.sdk.recipe())
        for item in self.recipe['inputs']:
            item['sha256'] = hashlib.sha256(item['name'].encode()).hexdigest()
        patched = patch.object(release.sdk, 'recipe', return_value=self.recipe)
        patched.start(); self.addCleanup(patched.stop)
        self.directory = self.root / 'archives'
        # prepared() declares a synthetic Linux x86_64 SDK; compiler and entropy
        # execution are mocked, so installation must use that fixture host too.
        for attribute, value in (('system', 'Linux'), ('machine', 'x86_64')):
            host = patch.object(release.sdk.platform, attribute, return_value=value)
            host.start()
            self.addCleanup(host.stop)

    def write(self, name, data):
        target = self.sdk / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data if isinstance(data, bytes) else data.encode())
        return target

    def prepared(self):
        for name, data in release.recipe_files().items():
            self.write(release.META + 'recipe/' + name, data)
        support = [self.recipe['entropy']['patch'], *self.recipe['entropy']['probe_sources']]
        support_hashes = {name: release.sdk.digest(release.sdk.RECIPE.parent / name) for name in support}
        manifest = {'schema_version': 1, 'recipe_id': release.identity(),
                    'recipe_sha256': release.sdk.digest(release.sdk.RECIPE),
                    'host': 'linux-x86_64', 'target': 'wasm32-emscripten',
                    'emscripten_version': self.recipe['emscripten_version'],
                    'entropy_patch_sha256': self.recipe['entropy']['patch_sha256'],
                    'entropy_capability': release.sdk.ENTROPY_CAPABILITY, 'entropy_probe': 'passed',
                    'cache_frozen': True, 'sources': self.recipe['inputs'], 'support_sha256': support_hashes,
                    'compiler': 'emsdk/upstream/emscripten/emcc',
                    'cxx_compiler': 'emsdk/upstream/emscripten/em++',
                    'toolchain': 'emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake',
                    'openssl_crypto': 'target/lib/libcrypto.a', 'openssl_include': 'target/include'}
        for name in ('compiler', 'cxx_compiler', 'toolchain', 'openssl_crypto'):
            self.write(manifest[name], name)
        manifest['openssl_crypto_sha256'] = release.sdk.digest(self.sdk / manifest['openssl_crypto'])
        for name in ('.emscripten', 'node/bin/node', 'target/include/openssl/ssl.h', 'cache/sysroot/lib/libc.a',
                     release.META + 'licenses/OpenSSL-LICENSE.txt'):
            self.write(name, name)
        for item in self.recipe['inputs']:
            self.write(release.META + 'sources/' + item['file'], item['name'])
        for name in {*release.sdk.RUNTIME_NOTICES, 'OpenSSL-LICENSE.txt', 'dlmalloc-NOTICE.txt'}:
            self.write(release.META + 'licenses/' + name, 'notice')
        for name in support:
            self.write(release.META + name, (release.sdk.RECIPE.parent / name).read_bytes())
        self.write(release.META + 'build-wasm-sdk.py', (ROOT / 'tools/build-wasm-sdk.py').read_bytes())
        self.write(release.META + 'manifest.json', json.dumps(manifest))
        self.write(release.META + 'recipe.json', json.dumps(self.recipe))
        self.write(release.META + 'preparation-provenance.json', json.dumps({
            'builder_sha256': release.sdk.digest(ROOT / 'tools/build-wasm-sdk.py'),
            'recipe_sha256': release.sdk.digest(release.sdk.RECIPE),
            'support_sha256': support_hashes, 'entropy_probe': 'passed'}))
        self.write(release.META + 'relocated-root.txt', str(self.sdk))
        return manifest

    def test_identity_matches_native_recipe_algorithm(self):
        digest = hashlib.sha256()
        for name, data in release.recipe_files().items():
            digest.update(name.encode() + b'\0'); digest.update(data)
        self.assertEqual(release.identity(), digest.hexdigest()[:20])
        self.assertIn('third_party/build-support/wasm-sdk/README.md', release.recipe_files())

    def test_roundtrip_preserves_sources_and_installs_at_a_new_location(self):
        self.prepared()
        self.write('node/bin/node-real', 'host node')
        (self.sdk / 'node/bin/node').unlink()
        (self.sdk / 'node/bin/node').symlink_to('node-real')
        release.package(self.sdk, self.directory)
        release.validate(self.directory)
        destination = self.root / 'relocated sdk'
        def qualify(em, node, target, scratch, env):
            self.assertEqual(em, destination / 'emsdk/upstream/emscripten')
            self.assertEqual(env['EM_FROZEN_CACHE'], '1')
            self.assertEqual(env['EM_CONFIG'], str(destination / '.emscripten'))
            self.assertEqual((destination / release.META / 'relocated-root.txt').read_text().strip(), str(destination))
            self.assertEqual((destination / 'node/bin/node').read_text(), 'host node')
        with patch.object(release.sdk, 'run') as run, patch.object(release.sdk, 'qualify_entropy', side_effect=qualify) as probe:
            release.install(self.directory, destination)
        run.assert_called_once(); probe.assert_called_once()
        for item in self.recipe['inputs']:
            self.assertEqual((destination / release.META / 'sources' / item['file']).read_text(), item['name'])

    def test_binary_only_install_still_checks_preserved_recipe_and_sources(self):
        self.prepared(); release.package(self.sdk, self.directory)
        (self.directory / release.names(release.identity())[1]).unlink()
        release.validate(self.directory, binary_only=True)
        with patch.object(release.sdk, 'run'), patch.object(release.sdk, 'qualify_entropy'):
            release.install(self.directory, self.root / 'binary installed')

    def test_unsupported_host_is_rejected_before_installation_side_effects(self):
        self.prepared(); release.package(self.sdk, self.directory)
        for system, machine in (('Linux', 'aarch64'), ('Darwin', 'x86_64'), ('Windows', 'AMD64')):
            with self.subTest(system=system, machine=machine):
                parent = self.root / (system + '-' + machine)
                destination = parent / 'installed sdk'
                with patch.object(release.sdk.platform, 'system', return_value=system), \
                        patch.object(release.sdk.platform, 'machine', return_value=machine), \
                        patch.object(release.sdk, 'extract') as extract, \
                        patch.object(release.sdk, 'run') as run, \
                        patch.object(release.sdk, 'qualify_entropy') as probe:
                    with self.assertRaisesRegex(ValueError, '^This pinned SDK host supports Linux x86_64 only$'):
                        release.install(self.directory, destination)
                self.assertFalse(destination.exists())
                self.assertFalse(parent.exists())
                extract.assert_not_called(); run.assert_not_called(); probe.assert_not_called()

    def test_refuses_changed_source_or_builder_provenance(self):
        self.prepared()
        path = self.sdk / release.META / 'sources' / self.recipe['inputs'][0]['file']
        path.write_text('corruption')
        with self.assertRaisesRegex(ValueError, 'source checksum'):
            release.package(self.sdk, self.directory)
        path.write_text(self.recipe['inputs'][0]['name'])
        self.write(release.META + 'preparation-provenance.json', '{}')
        with self.assertRaisesRegex(ValueError, 'provenance'):
            release.package(self.sdk, self.directory)

    def test_checksums_and_duplicate_checksum_entries_are_required(self):
        self.prepared(); release.package(self.sdk, self.directory)
        binary = self.directory / release.names(release.identity())[0]
        with binary.open('ab') as output:
            output.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.validate(self.directory)
        sums = self.directory / 'SHA256SUMS'; sums.write_text(sums.read_text() * 2)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            release.validate(self.directory)

    def test_links_traversal_special_files_and_duplicate_members_are_rejected_before_extract(self):
        for kind in ('traversal', 'link', 'special', 'duplicate'):
            path = self.root / (kind + '.tar.gz')
            with tarfile.open(path, 'w:gz') as archive:
                entry = tarfile.TarInfo('root/../escape' if kind == 'traversal' else 'root/entry')
                if kind == 'link':
                    entry.type = tarfile.SYMTYPE; entry.linkname = '../../outside'
                if kind == 'special':
                    entry.type = tarfile.FIFOTYPE
                archive.addfile(entry, io.BytesIO())
                if kind == 'duplicate': archive.addfile(entry, io.BytesIO())
            with self.assertRaises(ValueError, msg=kind):
                release.inspect_archive(path, 'root')

    def test_cyclic_links_and_linked_parent_writes_are_rejected(self):
        for kind in ('cycle', 'parent'):
            path = self.root / (kind + '.tar.gz')
            with tarfile.open(path, 'w:gz') as archive:
                link = tarfile.TarInfo('root/link')
                link.type = tarfile.SYMTYPE
                link.linkname = 'link' if kind == 'cycle' else 'real'
                archive.addfile(link)
                for name in (['root/file'] if kind == 'cycle' else ['root/real', 'root/link/child']):
                    archive.addfile(tarfile.TarInfo(name), io.BytesIO())
            with self.assertRaisesRegex(ValueError, 'Cyclic|linked directory'):
                release.inspect_archive(path, 'root')

    def test_replaced_recipe_metadata_is_rejected_even_with_new_outer_checksum(self):
        self.prepared()
        path = self.sdk / release.META / 'recipe/tools/build-wasm-sdk.py'
        path.write_text('changed recipe')
        with self.assertRaisesRegex(ValueError, 'recipe differs'):
            release.package(self.sdk, self.directory)

    def test_refuses_existing_destinations_and_escaping_prepared_links(self):
        self.prepared(); release.package(self.sdk, self.directory)
        with self.assertRaisesRegex(ValueError, 'replace'):
            release.package(self.sdk, self.directory)
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            release.install(self.directory, self.sdk)
        outside = self.root / 'outside'; outside.write_text('not part of SDK')
        (self.sdk / 'escape').symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'Escaping'):
            release.package(self.sdk, self.root / 'new archives')

    def test_failed_relocation_probe_does_not_leave_an_usable_installation(self):
        self.prepared(); release.package(self.sdk, self.directory)
        destination = self.root / 'failed'
        with patch.object(release.sdk, 'run'), patch.object(release.sdk, 'qualify_entropy', side_effect=ValueError('frozen cache missing')):
            with self.assertRaisesRegex(ValueError, 'frozen cache'):
                release.install(self.directory, destination)
        self.assertFalse(destination.exists())

    def test_missing_base_never_prepares_or_downloads_sources(self):
        with patch.object(release, 'release_info', return_value=None), patch.object(release.sdk, 'prepare') as prepare, patch.object(release, 'download_asset') as download:
            self.assertFalse(release.fetch('owner/repo', self.directory)['found'])
            with self.assertRaises(SystemExit) as result:
                release.main(['fetch', '--repo', 'owner/repo', '--directory', str(self.directory), '--require'])
            self.assertEqual(result.exception.code, 1)
            prepare.assert_not_called(); download.assert_not_called()

    def test_partial_or_draft_base_is_not_a_cache_miss(self):
        binary, source, checksum = release.names(release.identity())
        with patch.object(release, 'release_info', return_value={'assets': [{'name': binary}]}):
            with self.assertRaisesRegex(ValueError, 'Partial'):
                release.fetch('owner/repo', self.directory)
        with patch.object(release, 'release_info', return_value={'draft': True, 'assets': [{'name': n} for n in (binary, source, checksum)]}):
            with self.assertRaisesRegex(ValueError, 'draft'):
                release.fetch('owner/repo', self.directory)

    def test_binary_fetch_checks_remote_inventory_and_preserves_existing_outputs(self):
        self.prepared(); release.package(self.sdk, self.directory)
        binary, source, checksum = release.names(release.identity())
        info = {'draft': False, 'assets': [{'name': name} for name in (binary, source, checksum)]}
        received = []
        def download(repository, asset, destination):
            received.append(asset['name'])
            shutil.copy2(self.directory / ('SHA256SUMS' if asset['name'] == checksum else asset['name']), destination)
        output = self.root / 'fetched'
        with patch.object(release, 'release_info', return_value=info), patch.object(release, 'download_asset', side_effect=download):
            self.assertTrue(release.fetch('owner/repo', output, binary_only=True)['found'])
            self.assertEqual(set(received), {binary, checksum})
            release.validate(output, binary_only=True)
            with self.assertRaisesRegex(ValueError, 'replace'):
                release.fetch('owner/repo', output, binary_only=True)
        self.assertEqual({path.name for path in output.iterdir()}, {binary, 'SHA256SUMS'})

    def test_publication_never_overwrites_an_existing_recipe(self):
        info = {'assets': [{'name': name} for name in release.names(release.identity())]}
        with patch.object(release, 'validate', return_value={'archive': 'local'}), patch.object(release, 'release_info', return_value=info), patch.object(release, 'download', return_value={'archive': 'remote'}), patch.object(release, 'gh') as gh:
            with self.assertRaisesRegex(ValueError, 'refusing to overwrite'):
                release.publish('owner/repo', self.directory, 'a' * 40)
            gh.assert_not_called()


if __name__ == '__main__':
    unittest.main()
