#!/usr/bin/env python3
"""Exercise Windows dependency preservation/reuse without Windows or GitHub."""
import importlib.util
import builtins
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('windows_base', ROOT / 'tools/windows-base.py')
base = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(base)
REPO = 'owner/repository'


def raw_zip_entry(archive, filename, data):
    # ZipInfo's constructor normalizes backslashes on Windows. Assign the raw
    # filename afterward so the fixture writes the exact hostile ZIP header.
    member = zipfile.ZipInfo('placeholder')
    member.filename = filename
    member.orig_filename = filename
    archive.writestr(member, data)


class WindowsBaseTests(unittest.TestCase):
    def test_import_does_not_require_posix_only_sdk_modules(self):
        original = builtins.__import__
        def windows_import(name, *args, **kwargs):
            if name in ('fcntl', 'pwd', 'grp', 'resource'):
                raise AssertionError('Windows helper imported POSIX module: ' + name)
            return original(name, *args, **kwargs)
        spec = importlib.util.spec_from_file_location('windows_only_base', ROOT / 'tools/windows-base.py')
        with patch('builtins.__import__', side_effect=windows_import):
            loaded = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(loaded)
        self.assertEqual(loaded.RECIPE, ROOT / 'third_party/build-support/windows-base.json')

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-windows-base-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.export = self.root / 'export'
        self.sources = self.root / 'downloads'
        self.stage = self.root / 'stage'
        self.destination = self.root / 'installed dependencies'
        self.fetched = self.root / 'fetched'
        self.provenance_path = self.root / 'provenance.json'
        self.recipe = {'schema': 1, 'vcpkg_ref': '9e593bb18ea69cc5095e012465dcd675a822ed0d',
                       'triplet': 'x64-windows-static', 'ports': ['openssl', 'glew', 'freetype[core]'],
                       'toolset': 'v143', 'configurations': ['Debug', 'Release'],
                       'crt_linkage': 'static', 'library_linkage': 'static', 'lto': False}
        self.recipe_files = {'tools/windows-base.py': b'preserved helper\n',
                            'third_party/build-support/windows-base.json': json.dumps(self.recipe).encode()}
        self.provenance = {'source_sha': 'a' * 40, 'runner_image': 'windows-2022/20260921.1',
                           'toolset_version': '14.44.35207', 'compiler_version': '19.44.35224.0',
                           'linker_version': '14.44.35224.0'}
        self.provenance_path.write_text(json.dumps(self.provenance), encoding='utf-8')
        patched = patch.object(base, 'recipe_files', side_effect=lambda: self.recipe_files)
        patched.start()
        self.addCleanup(patched.stop)
        self.identity = base.recipe_identity()[1]
        self.binary, self.source, self.checksum = base.names(self.identity)
        self.export.mkdir()
        self.sources.mkdir()
        files = {base.TOOLCHAIN: b'# relocatable vcpkg toolchain\n', '.vcpkg-root': b'',
                 'installed/x64-windows-static/lib/libcrypto.lib': b'static library'}
        files.update({f'installed/x64-windows-static/share/{port}/copyright': b'license'
                      for port in ('openssl', 'glew', 'freetype')})
        files.update({f'installed/vcpkg/info/{port}_1.0_x64-windows-static.list': b'fixture export list'
                      for port in ('openssl', 'glew', 'freetype')})
        for name, data in files.items():
            path = self.export / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        with zipfile.ZipFile(self.sources / 'vcpkg-source.zip', 'w') as archive:
            archive.comment = self.recipe['vcpkg_ref'].encode()
            archive.writestr('ports/openssl/portfile.cmake', '# pinned recipe')
        (self.sources / 'openssl-source.tar.gz').write_bytes(b'preserved dependency archive')
        (self.sources / 'bootstrap.exe').write_bytes(b'excluded downloaded executable')
        (self.sources / 'tools').mkdir()
        (self.sources / 'tools' / 'ignored.zip').write_bytes(b'excluded downloaded tool')
        self.calls, self.downloads, self.remote = [], [], {}
        self.info = {'id': 77, 'tag_name': 'base', 'name': 'base', 'draft': False, 'prerelease': True}
        self.status = 200
        self.repository_error = None
        self.fail_upload = None
        for module, attribute, function in ((base.release, 'gh', self.github),
                                            (base, 'download_asset', self.download)):
            patched = patch.object(module, attribute, side_effect=function)
            patched.start()
            self.addCleanup(patched.stop)
        self.assemble()

    def assemble(self, directory=None):
        return base.assemble(self.export, self.sources, directory or self.stage, self.provenance_path)

    def github(self, args, check=True):
        self.calls.append(args)
        if args == ['api', f'repos/{REPO}']:
            if self.repository_error:
                raise subprocess.CalledProcessError(1, args, stderr='authentication failed')
            return subprocess.CompletedProcess(args, 0, '{}', '')
        if args == ['api', '--include', f'repos/{REPO}/releases/tags/base']:
            status = 404 if self.info is None else self.status
            return subprocess.CompletedProcess(args, 0 if status == 200 else 1,
                f'HTTP/2.0 {status} status\r\n\r\n{json.dumps(self.info)}', 'API error')
        if args[:2] == ['api', '--paginate']:
            if '/releases?' in args[-1]:
                rows = [] if self.info is None else [self.info]
            else:
                rows = [{'id': index + 1, 'name': name, 'state': 'uploaded', 'size': len(data),
                         'digest': 'sha256:' + base.hashlib.sha256(data).hexdigest()}
                        for index, (name, data) in enumerate(self.remote.items())]
            # Empty embedded assets and multiple dedicated pages are intentional.
            return subprocess.CompletedProcess(args, 0, json.dumps(rows[:1]) + '\n' + json.dumps(rows[1:]), '')
        if args[:3] == ['release', 'upload', 'base']:
            for filename in args[5:]:
                path = Path(filename)
                if path.name == self.fail_upload:
                    raise subprocess.CalledProcessError(1, args, stderr='upload failed')
                if path.name in self.remote:
                    raise AssertionError('Attempted to overwrite remote asset')
                self.remote[path.name] = path.read_bytes()
        elif args[:3] != ['release', 'edit', 'base']:
            raise AssertionError(f'Unexpected GitHub command: {args}')
        return subprocess.CompletedProcess(args, 0, '', '')

    def download(self, repository, asset, path):
        self.assertEqual(repository, REPO)
        self.downloads.append(asset['name'])
        path.write_bytes(self.remote[asset['name']])

    def remote_pair(self):
        self.remote.update({name: (self.stage / name).read_bytes() for name in (self.binary, self.source)})
        self.remote[self.checksum] = (self.stage / 'SHA256SUMS').read_bytes()

    def sums(self):
        (self.stage / 'SHA256SUMS').write_text(''.join(
            f'{base.release.digest(self.stage / name)}  {name}\n'
            for name in (self.binary, self.source)), encoding='utf-8')

    def rewrite(self, name, transform):
        path = self.stage / name
        root = path.stem
        with zipfile.ZipFile(path) as archive:
            entries = {member.filename[len(root) + 1:]: archive.read(member) for member in archive.infolist()}
        transform(entries)
        path.unlink()
        base.write_archive(path, root, entries)
        self.sums()

    def mutations(self):
        return [call for call in self.calls if call[:1] == ['release']]

    def test_assemble_preserves_export_recipe_and_recorded_download_inventory(self):
        manifest, sums = base.validate(self.stage, binary_only=False)
        self.assertEqual(manifest['recipe_id'], self.identity)
        self.assertEqual(manifest['provenance'], self.provenance)
        self.assertEqual(manifest['recipe'], self.recipe)
        self.assertEqual(set(sums), {self.binary, self.source})
        self.assertEqual(set(manifest['downloads']), {'downloads/vcpkg-source.zip', 'downloads/openssl-source.tar.gz'})
        self.assertIn('installed/x64-windows-static/lib/libcrypto.lib', manifest['files'])
        self.assertNotIn('installed/vcpkg/status', manifest['files'])
        self.assertEqual(self.calls, [])

    def test_recipe_and_helper_changes_change_identity_but_producer_metadata_does_not(self):
        self.provenance['compiler_version'] = '19.44.99999.0'
        self.provenance_path.write_text(json.dumps(self.provenance), encoding='utf-8')
        self.assertEqual(self.assemble(self.root / 'second')['recipe_id'], self.identity)
        self.recipe_files['tools/windows-base.py'] += b'# changed provenance logic\n'
        self.assertNotEqual(base.recipe_identity()[1], self.identity)
        self.recipe['vcpkg_ref'] = 'b' * 40
        self.recipe_files['third_party/build-support/windows-base.json'] = json.dumps(self.recipe).encode()
        self.assertNotEqual(base.recipe_identity()[1], self.identity)

    def test_assemble_rejects_missing_port_or_wrong_pinned_vcpkg_source(self):
        (self.export / 'installed/x64-windows-static/share/glew/copyright').unlink()
        with self.assertRaisesRegex(ValueError, 'installed static dependencies'):
            self.assemble(self.root / 'incomplete')
        (self.export / 'installed/x64-windows-static/share/glew/copyright').write_bytes(b'license')
        listfile = self.export / 'installed/vcpkg/info/glew_1.0_x64-windows-static.list'
        listfile.unlink()
        with self.assertRaisesRegex(ValueError, 'installed static dependencies'):
            self.assemble(self.root / 'missing-package-list')
        listfile.write_bytes(b'fixture export list')
        with zipfile.ZipFile(self.sources / 'vcpkg-source.zip', 'a') as archive:
            archive.comment = b'wrong source commit'
        with self.assertRaisesRegex(ValueError, 'pinned vcpkg commit'):
            self.assemble(self.root / 'wrong-source')

    def test_invalid_provenance_and_lto_recipe_are_rejected(self):
        for key, value in (('source_sha', 'main'), ('linker_version', 'latest'), ('runner_image', 'bad\nvalue')):
            provenance = {**self.provenance, key: value}
            with self.subTest(key=key), self.assertRaises(ValueError):
                base.validate_provenance(provenance)
        self.recipe['lto'] = True
        self.recipe_files['third_party/build-support/windows-base.json'] = json.dumps(self.recipe).encode()
        with self.assertRaisesRegex(ValueError, 'Unsupported Windows base recipe'):
            base.recipe_identity()

    def test_install_relocates_with_spaces_and_rejects_older_linker(self):
        with self.assertRaisesRegex(ValueError, 'older than'):
            base.install(self.stage, self.destination, linker_version='14.44.35223.99')
        self.assertFalse(self.destination.exists())
        result = base.install(self.stage, self.destination, linker_version='14.44.35224')
        self.assertEqual(Path(result['toolchain_file']).read_bytes(), (self.export / base.TOOLCHAIN).read_bytes())
        self.assertEqual(result['compiler_version'], self.provenance['compiler_version'])
        newer = base.install(self.stage, self.root / 'newer', linker_version='14.45.0.0')
        self.assertTrue(Path(newer['toolchain_file']).is_file())

    def test_install_and_assemble_never_replace_existing_paths(self):
        self.destination.mkdir()
        sentinel = self.destination / 'user.txt'
        sentinel.write_text('keep')
        with self.assertRaisesRegex(ValueError, 'new absolute path'):
            base.install(self.stage, self.destination)
        with self.assertRaisesRegex(ValueError, 'new absolute path'):
            base.install(self.stage, Path('relative-target'))
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            self.assemble()
        self.assertEqual(sentinel.read_text(), 'keep')

    def test_outer_and_inner_corruption_fail_before_install(self):
        original = (self.stage / self.binary).read_bytes()
        (self.stage / self.binary).write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'archive checksum mismatch'):
            base.install(self.stage, self.destination)
        (self.stage / self.binary).write_bytes(original)
        self.rewrite(self.binary, lambda entries: entries.update({base.TOOLCHAIN: b'changed'}))
        with self.assertRaisesRegex(ValueError, 'internal file checksum mismatch'):
            base.install(self.stage, self.destination)
        self.assertFalse(self.destination.exists())

    def test_traversal_absolute_and_windows_unsafe_paths_are_rejected(self):
        original = (self.stage / self.binary).read_bytes()
        for relative in ('../escape', '/absolute', 'C:/escape', 'scripts\\escape', 'scripts/CON.txt', 'scripts/trailing.'):
            (self.stage / self.binary).write_bytes(original)
            with zipfile.ZipFile(self.stage / self.binary, 'a') as archive:
                raw_zip_entry(archive, self.binary.removesuffix('.zip') + '/' + relative, b'unsafe')
            self.sums()
            with self.subTest(relative=relative), self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                base.install(self.stage, self.destination)
            self.assertFalse(self.destination.exists())

    def test_original_zip_name_is_checked_before_windows_separator_normalization(self):
        with zipfile.ZipFile(self.stage / self.binary, 'a') as archive:
            raw_zip_entry(archive, self.binary.removesuffix('.zip') + '/scripts\\escape', b'unsafe')
        self.sums()
        original_init = zipfile.ZipInfo.__init__
        def windows_init(member, *args, **kwargs):
            original_init(member, *args, **kwargs)
            member.filename = member.filename.replace('\\', '/')
        with patch.object(zipfile.ZipInfo, '__init__', windows_init):
            with zipfile.ZipFile(self.stage / self.binary) as archive:
                hostile = archive.infolist()[-1]
                self.assertIn('\\', hostile.orig_filename)
                self.assertNotIn('\\', hostile.filename)
            with self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                base.install(self.stage, self.destination)
        self.assertFalse(self.destination.exists())

    def test_duplicate_case_insensitive_paths_and_symlinks_are_rejected(self):
        original = (self.stage / self.binary).read_bytes()
        with zipfile.ZipFile(self.stage / self.binary, 'a') as archive:
            archive.writestr(self.binary.removesuffix('.zip') + '/' + base.TOOLCHAIN.upper(), b'duplicate')
        self.sums()
        with self.assertRaisesRegex(ValueError, 'Duplicate or case-insensitive'):
            base.install(self.stage, self.destination)
        (self.stage / self.binary).write_bytes(original)
        with zipfile.ZipFile(self.stage / self.binary, 'a') as archive:
            member = zipfile.ZipInfo(self.binary.removesuffix('.zip') + '/link')
            member.external_attr = 0o120777 << 16
            archive.writestr(member, '../escape')
        self.sums()
        with self.assertRaisesRegex(ValueError, 'regular files'):
            base.install(self.stage, self.destination)

    def test_preserved_source_checksums_and_matching_provenance_are_required(self):
        original = (self.stage / self.source).read_bytes()
        self.rewrite(self.source, lambda entries: entries.update({'downloads/openssl-source.tar.gz': b'changed'}))
        with self.assertRaisesRegex(ValueError, 'internal file checksum mismatch'):
            base.validate(self.stage, binary_only=False)
        (self.stage / self.source).write_bytes(original)
        def alter(entries):
            value = json.loads(entries[base.MANIFEST])
            value['provenance']['runner_image'] = 'different-image'
            entries[base.MANIFEST] = json.dumps(value).encode()
        self.rewrite(self.source, alter)
        with self.assertRaisesRegex(ValueError, 'binary/source provenance mismatch'):
            base.validate(self.stage, binary_only=False)

    def test_fetch_defaults_to_binary_only_and_can_install_it(self):
        self.remote_pair()
        result = base.fetch(REPO, self.fetched)
        self.assertTrue(result['found'])
        self.assertEqual(self.downloads, [self.binary, self.checksum])
        self.assertEqual({path.name for path in self.fetched.iterdir()}, {self.binary, 'SHA256SUMS'})
        installed = base.install(self.fetched, self.destination, linker_version=self.provenance['linker_version'])
        self.assertTrue(Path(installed['toolchain_file']).is_file())
        self.assertEqual(self.mutations(), [])

    def test_fetch_full_pair_and_existing_destination_refusal(self):
        self.remote_pair()
        base.fetch(REPO, self.fetched, with_sources=True)
        self.assertEqual(self.downloads, [self.binary, self.source, self.checksum])
        base.validate(self.fetched, binary_only=False)
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            base.fetch(REPO, self.fetched)

    def test_absent_recipe_and_visible_missing_base_are_the_only_cache_misses(self):
        self.assertFalse(base.fetch(REPO, self.fetched)['found'])
        self.info = None
        self.assertFalse(base.fetch(REPO, self.fetched)['found'])
        self.assertFalse(self.fetched.exists())
        self.assertEqual(self.mutations(), [])

    def test_auth_permission_and_api_failures_never_become_cache_misses(self):
        self.repository_error = True
        with self.assertRaises(subprocess.CalledProcessError):
            base.fetch(REPO, self.fetched)
        self.repository_error = None
        for status in (401, 403, 429, 500):
            self.status = status
            with self.subTest(status=status), self.assertRaisesRegex(RuntimeError, 'Cannot read'):
                base.fetch(REPO, self.fetched)
        self.assertFalse(self.fetched.exists())

    def test_partial_assets_or_draft_base_are_not_reused(self):
        self.remote[self.binary] = (self.stage / self.binary).read_bytes()
        with self.assertRaisesRegex(ValueError, 'Partial or duplicate'):
            base.fetch(REPO, self.fetched)
        self.remote_pair()
        self.info['draft'] = True
        with self.assertRaisesRegex(ValueError, 'published prerelease'):
            base.fetch(REPO, self.fetched)

    def test_failed_download_leaves_no_visible_partial_inventory(self):
        self.remote_pair()
        self.remote[self.binary] = b'corrupt archive'
        with self.assertRaisesRegex(ValueError, 'archive checksum mismatch'):
            base.fetch(REPO, self.fetched)
        self.assertFalse(self.fetched.exists())

    def test_publish_uploads_pair_then_checksum_never_clobbers_or_marks_latest(self):
        result = base.publish(REPO, self.stage)
        self.assertTrue(result['published'])
        mutations = self.mutations()
        self.assertEqual(len(mutations), 3)
        self.assertEqual([Path(name).name for name in mutations[0][5:]], [self.binary, self.source])
        self.assertEqual(Path(mutations[1][-1]).name, self.checksum)
        self.assertIn('--latest=false', mutations[2])
        self.assertIn('--prerelease', mutations[2])
        self.assertTrue(all('--clobber' not in call for call in mutations))
        before = len(mutations)
        result = base.publish(REPO, self.stage)
        self.assertFalse(result['published'])
        self.assertTrue(result['reused'])
        self.assertEqual(len(self.mutations()), before)

    def test_immutable_collision_and_partial_upload_require_inspection(self):
        self.remote_pair()
        self.provenance['source_sha'] = 'b' * 40
        self.provenance_path.write_text(json.dumps(self.provenance), encoding='utf-8')
        other = self.root / 'rebuild'
        self.assemble(other)
        with self.assertRaisesRegex(ValueError, 'Immutable Windows base assets differ'):
            base.publish(REPO, other)
        self.assertEqual(self.mutations(), [])
        self.remote.clear()
        self.fail_upload = self.source
        with self.assertRaises(subprocess.CalledProcessError):
            base.publish(REPO, self.stage)
        self.assertNotIn(self.checksum, self.remote)
        self.fail_upload = None
        with self.assertRaisesRegex(ValueError, 'Partial or duplicate'):
            base.publish(REPO, self.stage)

    def test_publish_does_not_create_or_repurpose_base(self):
        self.info = None
        with self.assertRaisesRegex(ValueError, 'finish base maintenance'):
            base.publish(REPO, self.stage)
        self.assertEqual(self.mutations(), [])

    def test_cli_identity_outputs_stable_values_without_network(self):
        output = self.root / 'github-output'
        with patch('sys.stdout', new_callable=io.StringIO) as stdout:
            base.main(['identity', '--github-output', str(output)])
        value = json.loads(stdout.getvalue())
        self.assertEqual(value['recipe_id'], self.identity)
        self.assertEqual(value['vcpkg_ref'], self.recipe['vcpkg_ref'])
        self.assertIn('recipe_id=' + self.identity, output.read_text(encoding='utf-8'))
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
