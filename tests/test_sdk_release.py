#!/usr/bin/env python3
"""Exercise durable SDK reuse without contacting or changing GitHub."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('sdk_release', ROOT / 'tools/sdk-release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
IDENTITY = '0123456789abcdef0123'
REPO = 'owner/repository'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive(path, entries):
    with tarfile.open(path, 'w:gz') as output:
        for name, data in entries:
            member = tarfile.TarInfo(name)
            member.size = len(data)
            output.addfile(member, io.BytesIO(data))


class SdkReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-sdk-release-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.directory = self.root / 'local'
        self.destination = self.root / 'fetched'
        self.bootstrap = b'preserved bootstrap source archive'
        self.upstream = b'preserved upstream source archive'
        self.recipe_manifest = {'schema_version': 1, 'architecture': 'x86_64',
            'target': 'x86_64-buildroot-linux-gnu', 'glibc': '2.36',
            'buildroot': {'file': 'buildroot.tar.xz', 'sha256': sha(self.bootstrap)}}
        self.recipe = {
            'tools/build-sdk.py': b'# preserved build script\n',
            'third_party/build-support/source-sdk/manifest.json': json.dumps(self.recipe_manifest).encode(),
            'third_party/build-support/source-sdk/configs/datapump_defconfig': b'BR2_FIXTURE=y\n',
        }
        self.inventory = {'id': IDENTITY, 'files': {'downloads/fixture/source.tar.gz': sha(self.upstream)}}
        self.manifest = {'schema_version': 1, 'id': IDENTITY, 'recipe': self.recipe_manifest,
            'baseline': {'glibc': '2.36'},
            'target': {'triple': 'x86_64-buildroot-linux-gnu', 'processor': 'x86_64'}}
        self.calls = []
        self.info = {'id': 55, 'name': 'base', 'draft': False, 'prerelease': True, 'assets': []}
        self.remote = {}
        self.status = 200
        self.failure = None
        for target, replacement in ((release.sdk, ('recipe_id', lambda: IDENTITY)),
                                    (release, ('recipe_files', lambda: self.recipe)),
                                    (release, ('gh', self.github))):
            patched = patch.object(target, replacement[0], side_effect=replacement[1])
            patched.start()
            self.addCleanup(patched.stop)
        streaming = patch.object(release.release.subprocess, 'run', side_effect=self.stream_asset)
        streaming.start()
        self.addCleanup(streaming.stop)
        self.write_pair()

    def write_pair(self, *, manifest=None, source_inventory=None, binary_inventory=None,
                   recipe=None, upstream=None, bootstrap=None, extra_source=(), compiler=b'compiler'):
        self.directory.mkdir(exist_ok=True)
        binary, source, _ = release.names(IDENTITY)
        binary_root = binary.removesuffix('.tar.gz')
        source_root = source.removesuffix('.tar.gz')
        archive(self.directory / binary, [
            (binary_root + '/share/datapump-sdk/manifest.json', json.dumps(manifest or self.manifest).encode()),
            (binary_root + '/share/datapump-sdk/sources.json', json.dumps(binary_inventory or self.inventory).encode()),
            (binary_root + '/bin/compiler', compiler),
        ])
        source_entries = [(source_root + '/' + name, value) for name, value in (recipe or self.recipe).items()]
        cache = source_root + '/third_party/build-support/cache/source-sdk/'
        source_entries += [
            (cache + 'bootstrap/buildroot.tar.xz', bootstrap if bootstrap is not None else self.bootstrap),
            (cache + 'sources.json', json.dumps(source_inventory or self.inventory).encode()),
            (cache + 'downloads/fixture/source.tar.gz', upstream if upstream is not None else self.upstream),
        ]
        source_entries += [(source_root + '/' + name, value) for name, value in extra_source]
        archive(self.directory / source, source_entries)
        self.sums()

    def sums(self):
        (self.directory / 'SHA256SUMS').write_text(''.join(
            f'{release.sdk.digest(path)}  {path.name}\n' for path in sorted(self.directory.glob('*.tar.gz'))), encoding='utf-8')

    def add_remote_pair(self):
        binary, source, checksum = release.names(IDENTITY)
        self.remote.update({name: (self.directory / name).read_bytes() for name in (binary, source)})
        self.remote[checksum] = (self.directory / 'SHA256SUMS').read_bytes()

    def github(self, args, check=True):
        self.calls.append(args)
        if self.failure:
            self.failure(args)
        if args == ['api', f'repos/{REPO}']:
            return subprocess.CompletedProcess(args, 0, '{}', '')
        if args == ['api', '--include', f'repos/{REPO}/releases/tags/base']:
            status = self.status if self.info is not None else 404
            output = f'HTTP/2.0 {status} response\r\nContent-Type: application/json\r\n\r\n{json.dumps(self.info)}'
            return subprocess.CompletedProcess(args, 0 if status == 200 else 1, output, 'API request failed')
        if args[:2] == ['api', '--paginate']:
            if '/releases?' in args[-1]:
                rows = [] if self.info is None else [{**self.info, 'tag_name': 'base'}]
                return subprocess.CompletedProcess(args, 0, json.dumps(rows), '')
            assets = [{'id': index + 101, 'name': name, 'digest': 'sha256:' + sha(data),
                       'size': len(data), 'state': 'uploaded'}
                      for index, (name, data) in enumerate(self.remote.items())]
            # Exercise concatenated pages from gh api --paginate.
            return subprocess.CompletedProcess(args, 0, json.dumps(assets[:1]) + '\n' + json.dumps(assets[1:]), '')
        if args[:3] == ['release', 'create', 'base']:
            self.info = {'id': 55, 'name': 'base', 'draft': True, 'prerelease': True}
        elif args[:3] == ['release', 'upload', 'base']:
            for filename in args[5:]:
                path = Path(filename)
                if path.name in self.remote:
                    raise subprocess.CalledProcessError(1, args, stderr='asset already exists')
                self.remote[path.name] = path.read_bytes()
        elif args[:3] == ['release', 'edit', 'base']:
            self.info.update(name='base', draft=False, prerelease=True)
        else:
            raise AssertionError(f'Unexpected gh command: {args}')
        return subprocess.CompletedProcess(args, 0, '', '')

    def stream_asset(self, args, *, check, stdout, stderr):
        self.assertTrue(check)
        self.assertEqual(stderr, subprocess.PIPE)
        self.assertEqual(args[:2], ['gh', 'api'])
        self.assertEqual(args[-2:], ['-H', 'Accept:application/octet-stream'])
        self.calls.append(args[1:])
        if self.failure:
            self.failure(args[1:])
        asset_id = int(args[2].rsplit('/', 1)[1])
        name = list(self.remote)[asset_id - 101]
        stdout.write(self.remote[name])
        return subprocess.CompletedProcess(args, 0, None, b'')

    def mutations(self):
        return [call for call in self.calls if call[:1] == ['release'] and call[1] in ('create', 'upload', 'edit')]

    def test_exact_archive_pair_and_source_replay_validate(self):
        sums = release.validate(self.directory, IDENTITY)
        self.assertEqual(set(sums), set(release.names(IDENTITY)[:2]))

    def test_changed_outer_checksum_fails_before_any_network(self):
        (self.directory / release.names(IDENTITY)[0]).write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual(self.calls, [])

    def test_pair_inventory_rejects_extra_files_and_duplicate_checksums(self):
        extra = self.directory / 'extra.tar.gz'
        extra.write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'exact recipe pair'):
            release.validate(self.directory, IDENTITY)
        extra.unlink()
        sums = self.directory / 'SHA256SUMS'
        sums.write_text(sums.read_text() * 2)
        with self.assertRaisesRegex(ValueError, 'duplicate SDK checksum'):
            release.validate(self.directory, IDENTITY)

    def test_binary_recipe_id_and_target_are_checked_even_with_valid_outer_hashes(self):
        for changes in ({'id': 'wrong'}, {'target': {'triple': 'wrong', 'processor': 'x86_64'}},
                        {'baseline': {'glibc': '2.40'}}, {'recipe': {}}):
            with self.subTest(changes=changes):
                self.write_pair(manifest={**self.manifest, **changes})
                with self.assertRaisesRegex(ValueError, 'manifest does not match'):
                    release.validate(self.directory, IDENTITY)

    def test_preserved_recipe_must_replay_the_exact_current_files(self):
        for changes in ({'tools/build-sdk.py': b'changed'},
                        {'third_party/build-support/source-sdk/extra.mk': b'extra'}):
            with self.subTest(changes=changes):
                self.write_pair(recipe={**self.recipe, **changes})
                with self.assertRaisesRegex(ValueError, 'recipe does not match'):
                    release.validate(self.directory, IDENTITY)

    def test_download_and_bootstrap_bytes_are_checked_inside_source_archive(self):
        self.write_pair(upstream=b'changed upstream')
        with self.assertRaisesRegex(ValueError, 'contents do not match'):
            release.validate(self.directory, IDENTITY)
        self.write_pair(bootstrap=b'changed bootstrap')
        with self.assertRaisesRegex(ValueError, 'bootstrap archive checksum mismatch'):
            release.validate(self.directory, IDENTITY)

    def test_source_inventory_identity_and_pair_agreement_are_checked(self):
        self.write_pair(source_inventory={**self.inventory, 'id': 'wrong'})
        with self.assertRaisesRegex(ValueError, 'another recipe'):
            release.validate(self.directory, IDENTITY)
        self.write_pair(binary_inventory={**self.inventory, 'id': 'wrong'})
        with self.assertRaisesRegex(ValueError, 'same inventory'):
            release.validate(self.directory, IDENTITY)

    def test_duplicate_recipe_members_cannot_change_replayed_identity(self):
        self.write_pair(extra_source=[('tools/build-sdk.py', self.recipe['tools/build-sdk.py'])])
        with self.assertRaisesRegex(ValueError, 'Duplicate recipe'):
            release.validate(self.directory, IDENTITY)

    def test_missing_base_or_current_recipe_is_a_clean_cache_miss(self):
        self.info = None
        self.assertEqual(release.fetch(REPO, self.destination), {'found': False, 'recipe_id': IDENTITY})
        self.info = {'id': 55, 'draft': False}
        self.remote['datapump-sdk-old-linux-x86_64.tar.gz'] = b'old unrelated recipe'
        self.assertFalse(release.fetch(REPO, self.destination)['found'])
        self.assertFalse(self.destination.exists())
        self.assertEqual(self.mutations(), [])

    def test_partial_recipe_never_triggers_a_rebuild_or_overwrite(self):
        self.remote[release.names(IDENTITY)[0]] = b'partial'
        with self.assertRaisesRegex(ValueError, 'Partial or duplicate'):
            release.fetch(REPO, self.destination)
        with self.assertRaisesRegex(ValueError, 'Partial or duplicate'):
            release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual(self.mutations(), [])

    def test_api_errors_are_not_cache_misses(self):
        for status in (401, 403, 429, 500):
            with self.subTest(status=status):
                self.status = status
                with self.assertRaisesRegex(RuntimeError, 'Cannot read'):
                    release.fetch(REPO, self.destination)
        self.assertEqual(self.mutations(), [])

    def test_repository_visibility_is_checked_before_interpreting_release_404(self):
        def deny(args):
            if args == ['api', f'repos/{REPO}']:
                raise subprocess.CalledProcessError(1, args, stderr='Not Found')
        self.failure = deny
        with self.assertRaises(subprocess.CalledProcessError):
            release.fetch(REPO, self.destination)
        self.assertEqual(len(self.calls), 1)

    def test_fetch_normalizes_checksum_name_only_after_full_verification(self):
        self.add_remote_pair()
        self.assertTrue(release.fetch(REPO, self.destination)['found'])
        self.assertEqual({path.name for path in self.destination.iterdir()},
                         set(release.names(IDENTITY)[:2]) | {'SHA256SUMS'})
        self.assertEqual(release.validate(self.destination, IDENTITY), release.validate(self.directory, IDENTITY))
        self.assertEqual(self.mutations(), [])

    def test_binary_only_consumer_keeps_pair_checksums_but_downloads_no_sources(self):
        self.add_remote_pair()
        self.assertTrue(release.fetch(REPO, self.destination, binary_only=True)['found'])
        self.assertEqual({path.name for path in self.destination.iterdir()},
                         {release.names(IDENTITY)[0], 'SHA256SUMS'})
        release.validate(self.destination, IDENTITY, binary_only=True)
        downloads = [call[1] for call in self.calls if len(call) > 1 and '/releases/assets/' in call[1]]
        self.assertEqual(downloads, [f'repos/{REPO}/releases/assets/{asset_id}' for asset_id in (101, 103)])
        self.assertEqual((self.destination / 'SHA256SUMS').read_bytes(), (self.directory / 'SHA256SUMS').read_bytes())

    def test_binary_only_still_checks_archive_digest_and_internal_identity(self):
        self.write_pair(manifest={**self.manifest, 'id': 'wrong'})
        self.add_remote_pair()
        with self.assertRaisesRegex(ValueError, 'manifest does not match'):
            release.fetch(REPO, self.destination, binary_only=True)
        self.assertFalse(self.destination.exists())
        self.write_pair()
        self.add_remote_pair()
        self.remote[release.names(IDENTITY)[0]] = b'corrupt binary'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.fetch(REPO, self.destination, binary_only=True)

    def test_verify_command_never_calls_github(self):
        with redirect_stdout(io.StringIO()) as output:
            release.main(['verify', '--directory', str(self.directory)])
        self.assertTrue(json.loads(output.getvalue())['verified'])
        self.assertEqual(self.calls, [])

    def test_corrupt_download_leaves_no_reusable_directory(self):
        self.add_remote_pair()
        self.remote[release.names(IDENTITY)[1]] = b'corrupt source archive'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.fetch(REPO, self.destination)
        self.assertFalse(self.destination.exists())

    def test_fetch_rejects_draft_or_existing_local_archive_inventory(self):
        self.add_remote_pair()
        self.info['draft'] = True
        with self.assertRaisesRegex(ValueError, 'still a draft'):
            release.fetch(REPO, self.destination)
        self.info['draft'] = False
        self.destination.mkdir()
        (self.destination / 'keep').write_text('untouched')
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            release.fetch(REPO, self.destination)
        self.assertEqual((self.destination / 'keep').read_text(), 'untouched')

    def test_draft_hidden_by_tag_endpoint_is_not_mistaken_for_a_cache_miss(self):
        self.add_remote_pair()
        self.info['draft'] = True
        self.status = 404
        with self.assertRaisesRegex(ValueError, 'still a draft'):
            release.fetch(REPO, self.destination)
        self.remote.pop(release.names(IDENTITY)[2])
        with self.assertRaisesRegex(ValueError, 'Partial or duplicate'):
            release.fetch(REPO, self.destination)

    def test_publish_creates_draft_and_uploads_pair_before_checksum_then_publishes(self):
        self.info = None
        self.assertTrue(release.publish(REPO, self.directory, 'a' * 40)['published'])
        create, pair, sums, edit = self.mutations()
        self.assertEqual(create[:3], ['release', 'create', 'base'])
        self.assertEqual(create[create.index('--target') + 1], 'a' * 40)
        self.assertEqual(create[create.index('--title') + 1], 'base')
        for flag in ('--draft', '--prerelease', '--latest=false'):
            self.assertIn(flag, create)
        self.assertEqual({Path(path).name for path in pair[5:]}, set(release.names(IDENTITY)[:2]))
        self.assertEqual(Path(sums[5]).name, release.names(IDENTITY)[2])
        self.assertIn('--draft=false', edit)
        self.assertIn('--prerelease', edit)
        self.assertIn('--latest=false', edit)
        self.assertTrue(all('--clobber' not in command for command in self.calls))

    def test_existing_identical_recipe_is_an_idempotent_no_op(self):
        self.add_remote_pair()
        result = release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual(result, {'published': False, 'reused': True, 'recipe_id': IDENTITY})
        self.assertEqual(self.mutations(), [])

    def test_existing_other_recipe_is_preserved_when_appending(self):
        self.remote['old-recipe.txt'] = b'retained old recipe'
        release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual(self.remote['old-recipe.txt'], b'retained old recipe')
        self.assertFalse(any(command[:2] == ['release', 'create'] for command in self.calls))

    def test_existing_different_build_for_same_recipe_is_never_clobbered(self):
        self.add_remote_pair()
        self.write_pair(compiler=b'another non-bit-identical compiler build')
        with self.assertRaisesRegex(ValueError, 'immutable base recipe assets differ'):
            release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual(self.mutations(), [])

    def test_upload_failure_does_not_upload_commit_marker_or_publish(self):
        self.info = None
        def fail(args):
            if args[:2] == ['release', 'upload']:
                raise subprocess.CalledProcessError(1, args, stderr='upload failed')
        self.failure = fail
        with self.assertRaises(subprocess.CalledProcessError):
            release.publish(REPO, self.directory, 'a' * 40)
        self.assertTrue(self.info['draft'])
        self.assertEqual([call[1] for call in self.mutations()], ['create', 'upload'])
        self.assertNotIn(release.names(IDENTITY)[2], self.remote)

    def test_complete_draft_can_finish_without_reuploading_assets(self):
        self.add_remote_pair()
        self.info['draft'] = True
        release.publish(REPO, self.directory, 'a' * 40)
        self.assertEqual([call[1] for call in self.mutations()], ['edit'])
        self.assertFalse(self.info['draft'])

    def test_invalid_source_and_repository_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Source SHA'):
            release.publish(REPO, self.directory, 'main')
        with self.assertRaisesRegex(ValueError, 'OWNER/REPO'):
            release.fetch('--repo elsewhere', self.destination)
        self.assertEqual(self.calls, [])

    def test_cli_github_outputs_use_boolean_literals(self):
        self.info = None
        output = self.root / 'github-output'
        with redirect_stdout(io.StringIO()):
            release.main(['fetch', '--repo', REPO, '--directory', str(self.destination),
                          '--github-output', str(output)])
        self.assertEqual(output.read_text(), f'found=false\nrecipe_id={IDENTITY}\n')


if __name__ == '__main__':
    unittest.main()
