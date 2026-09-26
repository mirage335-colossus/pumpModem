#!/usr/bin/env python3
"""Check release dependency preservation with byte fixtures and no GitHub writes."""
import base64
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_dependencies', ROOT / 'tools/release-dependencies.py')
dependencies = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dependencies)
REPO = 'owner/repository'
WINDOWS = '0123456789abcdef0123'
LINUX = 'fedcba9876543210fedc'
RECIPES = {'windows-base': WINDOWS, 'linux-sdk': LINUX}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load_tool(name):
    spec = importlib.util.spec_from_file_location('dependency_test_' + name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseDependencyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-release-dependencies-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.directory = self.root / 'release'
        self.release = dependencies.release_tool()
        self.assets, self.payloads = {}, {}
        self.groups = dependencies._groups(RECIPES, 'bookworm-sdk')
        for kind, (binary, source, checksum) in self.groups.items():
            values = {binary: b'\xff\x00 preserved compiled ' + kind.encode(),
                      source: b'\x00 preserved recipe, sources, downloads ' + kind.encode()}
            values[checksum] = ''.join(f'{sha(values[name])}  {name}\n' for name in (binary, source)).encode()
            for name, data in values.items():
                asset_id = len(self.assets) + 1
                self.assets[name] = {'id': asset_id, 'name': name, 'state': 'uploaded',
                                     'digest': 'sha256:' + sha(data), 'size': len(data)}
                self.payloads[asset_id] = data
        self.inventory = {name: asset['digest'][7:] for name, asset in self.assets.items()}
        self.api_calls, self.stream_calls = [], []
        self.base_releases = [{'tag_name': 'base', 'id': 55, 'draft': False, 'assets': []}]
        self.base_assets = list(self.assets.values())
        for target, name, replacement in ((self.release, 'api_pages', self.pages),
                                          (self.release.subprocess, 'run', self.stream)):
            patched = patch.object(target, name, side_effect=replacement)
            patched.start()
            self.addCleanup(patched.stop)

    def pages(self, endpoint):
        self.api_calls.append(endpoint)
        if endpoint == f'repos/{REPO}/releases?per_page=100':
            return self.base_releases
        if endpoint == f'repos/{REPO}/releases/55/assets?per_page=100':
            return self.base_assets
        self.fail('Unexpected API endpoint: ' + endpoint)

    def stream(self, args, **kwargs):
        self.assertEqual(args[:2], ['gh', 'api'])
        self.assertEqual(args[3:], ['-H', 'Accept:application/octet-stream'])
        self.assertEqual(kwargs['check'], True)
        self.assertNotIn('text', kwargs)
        self.assertNotIn('capture_output', kwargs)
        asset_id = int(args[2].rsplit('/', 1)[1])
        self.stream_calls.append(asset_id)
        kwargs['stdout'].write(self.payloads[asset_id])
        return subprocess.CompletedProcess(args, 0)

    def preserve(self, **kwargs):
        dependencies.preserve(REPO, self.directory, RECIPES, 'bookworm-sdk', **kwargs)

    def write_fixture(self):
        self.directory.mkdir()
        for name, asset in self.assets.items():
            (self.directory / name).write_bytes(self.payloads[asset['id']])

    def test_existing_recipe_identity_compatibility(self):
        windows = load_tool('windows-base')
        identities = dependencies.recipe_ids(ROOT, 'bookworm-sdk')
        self.assertEqual(identities['windows-base'], windows.recipe_identity()[1])
        # The Linux-only producer hashes native POSIX paths. A Windows runner
        # must still compute its Linux identity with forward slashes, rather
        # than copying that producer's unsupported Windows path behavior.
        if sys.platform != 'win32':
            with patch.dict(sys.modules, {'fcntl': types.ModuleType('fcntl')}):
                sdk = load_tool('build-sdk')
            self.assertEqual(identities['linux-sdk'], sdk.recipe_id())
        self.assertEqual(dependencies.recipe_ids(ROOT, 'ubuntu-22.04'),
                         {'windows-base': windows.recipe_identity()[1]})

    def test_asset_names_require_exact_baseline_kinds_and_identities(self):
        self.assertEqual(dependencies.asset_names(RECIPES, 'bookworm-sdk'), set(self.assets))
        self.assertEqual(len(dependencies.asset_names({'windows-base': WINDOWS}, 'ubuntu-22.04')), 3)
        for value, baseline in (({}, 'bookworm-sdk'), ({'windows-base': WINDOWS}, 'bookworm-sdk'),
                                (RECIPES, 'ubuntu-22.04'), (RECIPES, 'unexpected'),
                                ({**RECIPES, 'windows-base': '../bad'}, 'bookworm-sdk'),
                                ({**RECIPES, 'linux-sdk': 123}, 'bookworm-sdk')):
            with self.subTest(value=value, baseline=baseline), self.assertRaises(ValueError):
                dependencies.asset_names(value, baseline)

    def test_base_triplets_are_paginated_streamed_and_preserved_byte_for_byte(self):
        self.directory.mkdir()
        (self.directory / 'application.tar.gz').write_bytes(b'application stays intact')
        self.preserve()
        dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')
        self.assertEqual(self.api_calls, [f'repos/{REPO}/releases?per_page=100',
                                        f'repos/{REPO}/releases/55/assets?per_page=100'])
        self.assertEqual(len(self.stream_calls), 6)
        for name, asset in self.assets.items():
            self.assertEqual((self.directory / name).read_bytes(), self.payloads[asset['id']])
        self.assertEqual((self.directory / 'application.tar.gz').read_bytes(), b'application stays intact')

    def test_source_release_survives_deleted_base_without_any_base_lookup(self):
        self.base_releases = []
        self.preserve(source_assets=self.assets, source_inventory=self.inventory)
        self.assertEqual(self.api_calls, [])
        dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')

    def test_complete_source_triplets_do_not_require_a_main_inventory(self):
        self.preserve(source_assets=self.assets)
        self.assertEqual(self.api_calls, [])
        dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')

    def test_source_can_supply_one_kind_with_other_exact_kind_from_base(self):
        source = {name: self.assets[name] for name in self.groups['linux-sdk']}
        self.base_assets = [self.assets[name] for name in self.groups['windows-base']]
        self.preserve(source_assets=source, source_inventory=self.inventory)
        dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')

    def test_legacy_linux_pair_reconstructs_checksum_from_authenticated_inventory(self):
        source = dict(self.assets)
        checksum = self.groups['linux-sdk'][2]
        del source[checksum]
        inventory = dict(self.inventory)
        del inventory[checksum]
        self.base_releases = []
        self.preserve(source_assets=source, source_inventory=inventory)
        self.assertEqual(self.api_calls, [])
        self.assertEqual((self.directory / checksum).read_bytes(), self.payloads[self.assets[checksum]['id']])

    def test_partial_source_never_falls_back_to_base(self):
        for kind in self.groups:
            for present in (self.groups[kind][:1], self.groups[kind][1:]):
                with self.subTest(kind=kind, present=present), self.assertRaisesRegex(ValueError, 'Partial'):
                    self.preserve(source_assets={name: self.assets[name] for name in present},
                                  source_inventory=self.inventory)
        self.assertEqual(self.api_calls, [])
        self.assertEqual(self.stream_calls, [])

    def test_legacy_pair_without_authenticated_inventory_is_partial(self):
        with self.assertRaisesRegex(ValueError, 'Partial'):
            self.preserve(source_assets={name: self.assets[name] for name in self.groups['linux-sdk'][:2]})

    def test_missing_draft_or_ambiguous_base_never_triggers_build_or_publish(self):
        for releases in ([], [{'tag_name': 'base', 'id': 55, 'draft': True}], self.base_releases * 2):
            self.base_releases = releases
            with self.subTest(releases=releases), self.assertRaisesRegex(ValueError, 'published base'):
                self.preserve()
        self.assertEqual(self.stream_calls, [])

    def test_missing_partial_duplicate_base_recipes_fail_before_downloading(self):
        for assets in ([], self.base_assets[:-1], self.base_assets + [self.base_assets[0]]):
            self.base_assets = assets
            with self.subTest(assets=len(assets)), self.assertRaisesRegex(ValueError, 'partial|Duplicate'):
                self.preserve()
        self.assertEqual(self.stream_calls, [])

    def test_source_inventory_mismatch_does_not_use_base(self):
        self.inventory[self.groups['windows-base'][0]] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source inventory differs'):
            self.preserve(source_assets=self.assets, source_inventory=self.inventory)
        self.assertEqual(self.api_calls, [])
        self.assertFalse(any(self.directory.iterdir()))

    def test_corrupt_stream_is_rejected_without_installing_partial_triplets(self):
        self.payloads[1] += b'changed'
        with self.assertRaisesRegex(ValueError, 'Downloaded release asset checksum mismatch'):
            self.preserve()
        self.assertFalse(any(self.directory.iterdir()))

    def test_server_digest_is_required_for_copy(self):
        self.assets[self.groups['windows-base'][0]].pop('digest')
        with self.assertRaisesRegex(ValueError, 'completed SHA-256'):
            self.preserve()
        self.assertFalse(any(self.directory.iterdir()))

    def test_dependency_pair_checksums_are_verified_separately_from_server_digests(self):
        binary = self.groups['linux-sdk'][0]
        asset = self.assets[binary]
        data = b'wrong compiler with valid GitHub digest'
        self.payloads[asset['id']] = data
        asset.update(digest='sha256:' + sha(data), size=len(data))
        with self.assertRaisesRegex(ValueError, 'Dependency archive checksum mismatch'):
            self.preserve()
        self.assertFalse(any(self.directory.iterdir()))

    def test_verify_requires_exact_strict_checksum_pair_and_regular_files(self):
        self.write_fixture()
        checksum = self.directory / self.groups['windows-base'][2]
        original = checksum.read_bytes()
        for text in (original + original.splitlines(keepends=True)[0],
                     original.splitlines(keepends=True)[0],
                     original + b'0' * 64 + b'  unexpected.zip\n',
                     original.replace(b'  ', b' *', 1)):
            checksum.write_bytes(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')
        checksum.write_bytes(original)
        binary = self.directory / self.groups['windows-base'][0]
        binary.unlink()
        binary.mkdir()
        with self.assertRaisesRegex(ValueError, 'unsafe dependency archive'):
            dependencies.verify(self.directory, RECIPES, 'bookworm-sdk')

    def test_existing_dependency_asset_is_never_overwritten(self):
        self.directory.mkdir()
        path = self.directory / self.groups['windows-base'][0]
        path.write_bytes(b'original')
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            self.preserve()
        self.assertEqual(path.read_bytes(), b'original')
        self.assertEqual(self.api_calls, [])

    def prepared(self, schema=6):
        return {'metadata': {'schema': schema, 'linux_baseline': 'bookworm-sdk', 'dependencies': RECIPES},
                'assets': self.assets, 'inventory': self.inventory}

    def test_fetch_uses_published_release_only_and_emits_installer_checksum_name(self):
        with patch.object(dependencies.certification_tool(), 'prepare', return_value=self.prepared()) as prepare:
            result = dependencies.fetch(REPO, 'v1.0', 'windows-base', self.directory, 'a' * 64)
        self.assertEqual(result, {'found': True, 'recipe_id': WINDOWS})
        self.assertEqual(prepare.call_args.args[0:2], (REPO, 'v1.0'))
        self.assertEqual(prepare.call_args.args[3], 'a' * 64)
        self.assertEqual(self.api_calls, [])
        self.assertEqual({path.name for path in self.directory.iterdir()},
                         set(self.groups['windows-base'][:2]) | {'SHA256SUMS'})
        self.assertEqual((self.directory / 'SHA256SUMS').read_bytes(),
                         self.payloads[self.assets[self.groups['windows-base'][2]]['id']])

    def test_fetch_legacy_returns_absent_without_making_directory(self):
        with patch.object(dependencies.certification_tool(), 'prepare', return_value=self.prepared(schema=5)):
            self.assertEqual(dependencies.fetch(REPO, 'v1.0', 'linux-sdk', self.directory), {'found': False})
        self.assertFalse(self.directory.exists())
        self.assertEqual(self.stream_calls, [])

    def test_fetch_binary_only_verifies_then_keeps_installer_inputs(self):
        with patch.object(dependencies.certification_tool(), 'prepare', return_value=self.prepared()):
            result = dependencies.fetch(REPO, 'v1.0', 'windows-base', self.directory, binary_only=True)
        self.assertEqual(result, {'found': True, 'recipe_id': WINDOWS})
        self.assertEqual(len(self.stream_calls), 3)
        self.assertEqual({path.name for path in self.directory.iterdir()},
                         {self.groups['windows-base'][0], 'SHA256SUMS'})

    def test_fetch_missing_release_dependency_never_falls_back_to_base(self):
        del self.assets[self.groups['linux-sdk'][1]]
        with patch.object(dependencies.certification_tool(), 'prepare', return_value=self.prepared()):
            with self.assertRaisesRegex(ValueError, 'Missing required dependency asset'):
                dependencies.fetch(REPO, 'v1.0', 'linux-sdk', self.directory)
        self.assertFalse(self.directory.exists())
        self.assertEqual(self.api_calls, [])

    def test_cli_reports_machine_readable_fetch_outputs(self):
        output = self.root / 'github-output'
        stdout = io.StringIO()
        with patch.object(dependencies, 'fetch', return_value={'found': True, 'recipe_id': LINUX}), redirect_stdout(stdout):
            dependencies.main(['fetch', '--repo', REPO, '--tag', 'v1.0', '--kind', 'linux-sdk',
                               '--directory', str(self.directory), '--github-output', str(output)])
        self.assertEqual(json.loads(stdout.getvalue()), {'found': True, 'recipe_id': LINUX})
        self.assertEqual(output.read_text(), f'found=true\nrecipe_id={LINUX}\n')


class HistoricalRecipeTests(unittest.TestCase):
    def setUp(self):
        self.release = dependencies.release_tool()
        self.source_sha, self.tree_sha = 'a' * 40, 'b' * 40
        self.files = {
            'tools/windows-base.py': b'raise RuntimeError("historical helpers must not execute")\n',
            'third_party/build-support/windows-base.json': b'{"preserved":"windows recipe"}\n',
            'tools/build-sdk.py': b'raise RuntimeError("historical SDK must not execute")\n',
            'third_party/build-support/source-sdk/manifest.json': b'{"preserved":"SDK recipe"}\n',
            'third_party/build-support/source-sdk/configs/datapump_defconfig': b'BR2_PINNED=y\n',
            'unrelated/large-file.bin': b'not requested',
        }
        self.tree, self.blobs, self.calls = [], {}, []
        for name, data in self.files.items():
            blob_sha = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
            self.tree.append({'path': name, 'sha': blob_sha, 'type': 'blob', 'mode': '100644'})
            self.blobs[blob_sha] = {'sha': blob_sha, 'encoding': 'base64', 'size': len(data),
                                    'content': base64.b64encode(data).decode() + '\n'}
        self.truncated = False
        patched = patch.object(self.release, 'gh', side_effect=self.api)
        patched.start()
        self.addCleanup(patched.stop)

    def api(self, args):
        self.calls.append(args)
        endpoint = args[1]
        prefix = f'repos/{REPO}/git/'
        self.assertTrue(endpoint.startswith(prefix))
        endpoint = endpoint[len(prefix):]
        if endpoint == 'commits/' + self.source_sha:
            value = {'sha': self.source_sha, 'tree': {'sha': self.tree_sha}}
        elif endpoint == 'trees/' + self.tree_sha + '?recursive=1':
            value = {'sha': self.tree_sha, 'truncated': self.truncated, 'tree': self.tree}
        elif endpoint.startswith('blobs/'):
            value = self.blobs[endpoint.split('/')[1]]
        else:
            self.fail('Unexpected historical API endpoint: ' + endpoint)
        return subprocess.CompletedProcess(args, 0, stdout=json.dumps(value))

    def test_legacy_identities_come_from_exact_git_objects_without_executing_them(self):
        expected = dependencies._recipe_ids(self.files, 'bookworm-sdk')
        self.assertEqual(dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk'), expected)
        self.assertEqual(len(self.calls), 7)  # commit + tree + five relevant recipe blobs
        self.assertNotEqual(expected, dependencies.recipe_ids(ROOT, 'bookworm-sdk'))

    def test_ubuntu_legacy_identity_downloads_only_windows_recipe(self):
        self.assertEqual(dependencies.remote_recipe_ids(REPO, self.source_sha, 'ubuntu-22.04'),
                         dependencies._recipe_ids(self.files, 'ubuntu-22.04'))
        self.assertEqual(len(self.calls), 4)

    def test_missing_historical_windows_helper_fails_without_current_recipe_guess(self):
        self.tree = [item for item in self.tree if item['path'] != 'tools/windows-base.py']
        with self.assertRaisesRegex(ValueError, 'Historical Windows dependency provenance absent'):
            dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk')
        self.assertEqual(len(self.calls), 2)

    def test_truncated_tree_is_not_accepted_as_complete_recipe(self):
        self.truncated = True
        with self.assertRaisesRegex(ValueError, 'truncated'):
            dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk')

    def test_blob_bytes_are_bound_to_the_committed_git_object(self):
        blob = self.blobs[self.tree[0]['sha']]
        blob['content'] = base64.b64encode(b'replaced recipe').decode()
        blob['size'] = len(b'replaced recipe')
        with self.assertRaisesRegex(ValueError, 'blob checksum mismatch'):
            dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk')

    def test_symlink_or_duplicate_recipe_file_is_rejected(self):
        self.tree[0]['mode'] = '120000'
        with self.assertRaisesRegex(ValueError, 'unique regular Git blobs'):
            dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk')
        self.tree[0]['mode'] = '100644'
        self.tree.append(dict(self.tree[0]))
        with self.assertRaisesRegex(ValueError, 'unique regular Git blobs'):
            dependencies.remote_recipe_ids(REPO, self.source_sha, 'bookworm-sdk')


if __name__ == '__main__':
    unittest.main()
