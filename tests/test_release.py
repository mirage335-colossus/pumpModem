#!/usr/bin/env python3
"""Release identity, checksum inventory and fail-closed GitHub publication tests."""
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release', ROOT / 'tools/release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def metadata(**options):
    defaults = dict(source_sha='a' * 40, run_id='123', run_attempt='1',
                    now=datetime(2026, 9, 22, 7, 52, tzinfo=timezone.utc),
                    cmake_version='0.7.2')
    defaults.update(options)
    return release.make_metadata(**defaults)


class MetadataTests(unittest.TestCase):
    def test_default_and_explicit_version_and_experiment(self):
        default = metadata()
        self.assertEqual(default['tag'], 'v0.7.2-2026-09-22-0252CDT')
        self.assertEqual(default['title'], default['tag'])
        custom = metadata(version='v001_00', experiment=True)
        self.assertEqual(custom['tag'], 'v001_00-2026-09-22-0252CDT')
        self.assertEqual(custom['title'], 'experiment')
        self.assertTrue(custom['experiment'])
        self.assertEqual(custom['source_sha'], 'a' * 40)

    def test_chicago_day_boundary_and_daylight_transitions(self):
        cases = [
            ('2026-01-01T03:04:00+00:00', '2025-12-31-2104CST'),
            ('2026-03-08T07:59:00+00:00', '2026-03-08-0159CST'),
            ('2026-03-08T08:00:00+00:00', '2026-03-08-0300CDT'),
            ('2026-11-01T06:59:00+00:00', '2026-11-01-0159CDT'),
            ('2026-11-01T07:00:00+00:00', '2026-11-01-0100CST'),
        ]
        for instant, expected in cases:
            with self.subTest(instant=instant):
                self.assertEqual(metadata(now=datetime.fromisoformat(instant))['build_date'], expected)
                with patch.object(release, 'ZoneInfo', side_effect=release.ZoneInfoNotFoundError):
                    self.assertEqual(metadata(now=datetime.fromisoformat(instant))['build_date'], expected)

    def test_naive_time_and_invalid_labels_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'UTC offset'):
            metadata(now=datetime(2026, 1, 1))
        for version in ('a/b', '../tag', '-flag', 'x\ninjected=true', 'a b', '$(id)',
                        '`id`', 'a' * 65, 'trailing.', 'two..dots'):
            with self.subTest(version=version), self.assertRaisesRegex(ValueError, 'safe label'):
                metadata(version=version)

    def test_source_and_run_identifiers_are_validated(self):
        for options in ({'source_sha': 'main'}, {'source_sha': 'A' * 40},
                        {'run_id': '0'}, {'run_id': '1\nx'}, {'run_attempt': '-1'},
                        {'experiment': 'false'}, {'linux_baseline': 'unknown'}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                metadata(**options)

    def test_project_version_is_read_from_cmake(self):
        self.assertRegex(release.project_version(), r'^\d+(?:\.\d+)+$')


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='datapump-release-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.artifacts = self.root / 'downloaded'
        self.artifacts.mkdir()
        self.metadata = self.root / 'release-metadata.json'
        release.write_json(self.metadata, metadata())
        self.notes = self.root / 'notes.md'
        self.notes.write_text('Portable release qualification notes.\n', encoding='utf-8')
        self.output = self.root / 'release'
        for target, (system, architectures, _) in release.TARGETS.items():
            directory = self.artifacts / target
            directory.mkdir()
            base = f'DataPump-0.7.2-{system}-{architectures[0]}-native'
            for extension in ('.tar.gz', '.zip'):
                (directory / (base + extension)).write_bytes(f'{target} {extension}'.encode())
            self.sums(directory)

    def sums(self, directory, checksum_name='SHA256SUMS.txt'):
        entries = sorted(path for path in directory.iterdir() if path.name != checksum_name)
        (directory / checksum_name).write_text(
            ''.join(f'{release.digest(path)}  {path.name}\n' for path in entries), encoding='utf-8')

    def assemble(self, **options):
        return release.assemble(self.artifacts, self.metadata, self.notes, self.output, **options)

    def test_minimal_inventory_selects_one_archive_per_target(self):
        value = self.assemble()
        expected = set(release.application_names(value).values()) | release.SUPPORT_FILES | {'SHA256SUMS.txt'}
        self.assertEqual({path.name for path in self.output.iterdir()}, expected)
        verified, assets = release.verify_release(self.output)
        self.assertEqual(verified, value)
        self.assertEqual(len(assets), 5)
        for target, name in release.application_names(value).items():
            self.assertEqual((self.output / name).read_bytes(), f'{target} {release.TARGETS[target][2]}'.encode())

    def test_corrupt_archive_and_missing_target_prevent_staging(self):
        directory = self.artifacts / 'linux-x86_64'
        archive = next(directory.glob('*.zip'))
        archive.write_bytes(b'corrupt companion even though only the TGZ is published')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            self.assemble()
        self.assertFalse(self.output.exists())
        self.sums(directory)
        (self.artifacts / 'linux-aarch64').rename(self.artifacts / 'wrong-target')
        with self.assertRaisesRegex(ValueError, 'Missing or unsafe artifact directory'):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_unlisted_file_is_not_silently_published(self):
        (self.artifacts / 'windows-x86_64' / 'unexpected.zip').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'does not match'):
            self.assemble()

    def test_wrong_platform_pair_and_missing_companion_are_rejected(self):
        directory = self.artifacts / 'linux-aarch64'
        for archive in list(directory.glob('DataPump-*')):
            archive.rename(directory / archive.name.replace('aarch64', 'x86_64'))
        self.sums(directory)
        with self.assertRaisesRegex(ValueError, 'matching native TGZ/ZIP pair'):
            self.assemble()
        next(directory.glob('*.zip')).unlink()
        self.sums(directory)
        with self.assertRaisesRegex(ValueError, 'matching native TGZ/ZIP pair'):
            self.assemble()

    def test_duplicate_and_escaping_checksum_names_are_rejected(self):
        directory = self.artifacts / 'linux-x86_64'
        sums = directory / 'SHA256SUMS.txt'
        original = sums.read_text(encoding='utf-8')
        for content in (original + original.splitlines()[0] + '\n',
                        '0' * 64 + '  ../escape.tar.gz\n',
                        '0' * 64 + ' *binary-format.tar.gz\n'):
            sums.write_text(content, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Invalid or duplicate'):
                self.assemble()

    def test_symlinks_and_subdirectories_are_rejected(self):
        directory = self.artifacts / 'linux-x86_64'
        (directory / 'unexpected').mkdir()
        with self.assertRaisesRegex(ValueError, 'only regular files'):
            self.assemble()
        (directory / 'unexpected').rmdir()
        link = directory / 'link'
        try:
            link.symlink_to(self.notes)
        except OSError:
            self.skipTest('Symlink creation is unavailable on this platform')
        with self.assertRaisesRegex(ValueError, 'only regular files'):
            self.assemble()

    def test_existing_output_is_never_replaced(self):
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            self.assemble()

    def test_tampered_metadata_is_rejected(self):
        value = metadata()
        value['tag'] = 'different-tag'
        release.write_json(self.metadata, value)
        with self.assertRaisesRegex(ValueError, 'does not match'):
            self.assemble()

    def test_partial_release_inventory_is_rejected_even_with_new_checksums(self):
        self.assemble()
        next(self.output.glob('*.zip')).unlink()
        self.sums(self.output)
        with self.assertRaisesRegex(ValueError, 'missing an application target'):
            release.verify_release(self.output)

    def test_sdk_assets_require_checksums_and_preserved_sources(self):
        sdk = self.root / 'sdk'
        sdk.mkdir()
        (sdk / 'datapump-sdk-fixture-linux-x86_64.tar.gz').write_bytes(b'toolchain')
        self.sums(sdk, 'SHA256SUMS')
        with self.assertRaisesRegex(ValueError, 'preserved source archive'):
            self.assemble(sdk_artifacts=sdk)
        (sdk / 'datapump-sdk-sources-fixture.tar.gz').write_bytes(b'sources')
        self.sums(sdk, 'SHA256SUMS')
        self.assemble(sdk_artifacts=sdk)
        self.assertEqual(len(release.verify_release(self.output)[1]), 7)


class PublicationTests(InventoryTests):
    # Reuse the on-disk fixture while keeping publication tests free of network.
    def github(self, arguments, check=True):
        self.calls.append(arguments)
        if arguments[:2] == ['api', '--include']:
            return subprocess.CompletedProcess(arguments, 1, 'HTTP/2.0 404 Not Found\r\n\r\n{}', 'gh: Not Found (HTTP 404)')
        return subprocess.CompletedProcess(arguments, 0, '{}', '')

    def publish_fixture(self, experiment=False):
        release.write_json(self.metadata, metadata(experiment=experiment))
        self.assemble()
        self.calls = []

    def test_default_creates_draft_with_exact_source_then_uploads_without_clobber(self):
        self.publish_fixture()
        with patch.object(release, 'gh', side_effect=self.github):
            release.publish(self.output, 'owner/repository')
        create, upload = self.calls[-2:]
        self.assertEqual(create[:3], ['release', 'create', metadata()['tag']])
        self.assertIn('--draft', create)
        self.assertEqual(create[create.index('--target') + 1], 'a' * 40)
        self.assertEqual(create[create.index('--title') + 1], metadata()['tag'])
        self.assertEqual(upload[:2], ['release', 'upload'])
        self.assertNotIn('--clobber', upload)
        self.assertEqual(len(upload[5:]), 6)
        self.assertFalse(any(call[:2] == ['release', 'edit'] for call in self.calls))

    def test_experiment_is_prerelease_never_latest_and_publishes_last(self):
        self.publish_fixture(experiment=True)
        with patch.object(release, 'gh', side_effect=self.github):
            release.publish(self.output, 'owner/repository', publish_now=True)
        create, upload, edit = self.calls[-3:]
        self.assertEqual(create[create.index('--title') + 1], 'experiment')
        self.assertIn('--prerelease', create)
        self.assertIn('--latest=false', create)
        self.assertEqual(upload[:2], ['release', 'upload'])
        self.assertIn('--draft=false', edit)
        self.assertIn('--latest=false', edit)

    def test_regular_publication_becomes_latest_only_after_upload(self):
        self.publish_fixture()
        with patch.object(release, 'gh', side_effect=self.github):
            release.publish(self.output, 'owner/repository', publish_now=True)
        self.assertIn('--latest=true', self.calls[-1])

    def test_existing_release_or_tag_stops_before_any_mutation(self):
        self.publish_fixture()
        for collision in ('/releases/tags/', '/git/ref/tags/'):
            self.calls = []
            def exists(arguments, check=True):
                if arguments[:2] == ['api', '--include'] and collision in arguments[-1]:
                    self.calls.append(arguments)
                    return subprocess.CompletedProcess(arguments, 0, '{}', '')
                return self.github(arguments, check=check)
            with self.subTest(collision=collision), patch.object(release, 'gh', side_effect=exists):
                with self.assertRaisesRegex(ValueError, 'Refusing to overwrite'):
                    release.publish(self.output, 'owner/repository', publish_now=True)
            self.assertFalse(any(call[0] == 'release' for call in self.calls))

    def test_network_or_permission_failure_is_not_treated_as_missing_tag(self):
        self.publish_fixture()
        for status in ('', 'HTTP/2.0 403 Forbidden', 'HTTP/2.0 500 Internal Server Error'):
            self.calls = []
            def failure(arguments, check=True):
                if arguments[:2] == ['api', '--include']:
                    self.calls.append(arguments)
                    return subprocess.CompletedProcess(arguments, 1, status, 'failed request')
                return self.github(arguments, check=check)
            with self.subTest(status=status), patch.object(release, 'gh', side_effect=failure):
                with self.assertRaisesRegex(RuntimeError, 'Cannot confirm'):
                    release.publish(self.output, 'owner/repository', publish_now=True)
            self.assertFalse(any(call[0] == 'release' for call in self.calls))

    def test_upload_failure_leaves_draft_and_never_publishes(self):
        self.publish_fixture()
        def failure(arguments, check=True):
            if arguments[:2] == ['release', 'upload']:
                self.calls.append(arguments)
                raise subprocess.CalledProcessError(1, arguments, stderr='upload failed')
            return self.github(arguments, check=check)
        with patch.object(release, 'gh', side_effect=failure):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.output, 'owner/repository', publish_now=True)
        self.assertIn('--draft', self.calls[-2])
        self.assertFalse(any(call[:2] == ['release', 'edit'] for call in self.calls))

    def test_bad_local_inventory_and_repository_never_call_github(self):
        self.publish_fixture()
        with patch.object(release, 'gh', side_effect=AssertionError('network')):
            with self.assertRaisesRegex(ValueError, 'OWNER/REPO'):
                release.publish(self.output, '--repo attacker')
            next(self.output.glob('*.zip')).write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                release.publish(self.output, 'owner/repository')

    def test_gh_wrapper_uses_argument_list_without_shell(self):
        with patch.object(release.subprocess, 'run') as run:
            release.gh(['release', 'view', 'literal'])
        run.assert_called_once_with(['gh', 'release', 'view', 'literal'], check=True,
                                    text=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
