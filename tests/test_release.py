#!/usr/bin/env python3
"""Release identity, checksum inventory and fail-closed GitHub publication tests."""
from datetime import datetime, timezone
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import Mock, patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release', ROOT / 'tools/release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def metadata(**options):
    # Keep the existing published-schema fixture to prove backward compatibility.
    defaults = dict(source_sha='a' * 40, run_id='123', run_attempt='1',
                    now=datetime(2026, 9, 22, 7, 52, tzinfo=timezone.utc),
                    cmake_version='0.7.2', schema=1)
    defaults.update(options)
    return release.make_metadata(**defaults)


def backend_archive(path, base, backend, *, include_info=True, duplicate_info=False):
    entries = [(f'{base}/bin/pump', b'fixture executable')]
    if include_info:
        entries.append((f'{base}/share/doc/datapump/build-info.txt',
                        f'DataPump 0.7.2\nGUI: ON ({backend})\n'.encode()))
        if duplicate_info:
            entries.append(entries[-1])
    if path.name.endswith('.tar.gz'):
        with tarfile.open(path, 'w:gz') as archive:
            for name, content in entries:
                member = tarfile.TarInfo(name)
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))
    else:
        with zipfile.ZipFile(path, 'w') as archive:
            for name, content in entries:
                archive.writestr(name, content)


APT_NAMES = {'Packages', 'Packages.gz', 'Release', 'InRelease', 'Release.gpg',
             'datapump-archive-keyring.gpg', 'datapump.sources', 'apt-repository.json'} | {
                 f'datapump-{backend}_0.7.2+fixture_{arch}.deb'
                 for backend in ('fltk', 'rev') for arch in ('amd64', 'arm64')}


def apt_fixture():
    """Keep orchestrator tests separate from real signing/dpkg tests in the APT helper suite."""
    def build(directory, value, repository, key, fingerprint):
        for name in APT_NAMES:
            (directory / name).write_bytes(('signed fixture ' + name).encode())
    tool = Mock()
    tool.asset_names.return_value = APT_NAMES
    tool.build.side_effect = build
    return tool


class MetadataTests(unittest.TestCase):
    def test_new_metadata_defaults_to_both_backends_and_legacy_stays_readable(self):
        value = release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1')
        self.assertEqual(value['schema'], 3)
        self.assertEqual(value['gui_backends'], ['fltk', 'rev'])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'metadata.json'
            for schema in (1, 2, 3):
                original = metadata(schema=schema)
                release.write_json(path, original)
                self.assertEqual(release.load_metadata(path), original)
                self.assertEqual(len(release.application_targets(original)), 3 if schema == 1 else 6)
            for changes in ({'schema': 4}, {'schema': True}, {'gui_backends': ['fltk']},
                            {'gui_backends': ['rev', 'fltk']}, {'gui_backends': ['fltk', 'rev', 'rev']},
                            {'gui_backends': None}):
                release.write_json(path, {**metadata(schema=2), **changes})
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    release.load_metadata(path)

    def test_schema3_requires_apt_marker_and_valid_packaging_provenance(self):
        value = metadata(schema=3)
        self.assertEqual(value['apt_repository'], {
            'schema': 1, 'architectures': ['amd64', 'arm64'], 'gui_backends': ['fltk', 'rev']})
        self.assertEqual(value['packager_sha'], value['source_sha'])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'metadata.json'
            for change in ({'apt_repository': None}, {'apt_repository': {'schema': 1}},
                           {'packager_sha': 'main'}, {'repackaged_from': {'tag': '../bad'}}):
                release.write_json(path, {**value, **change})
                with self.subTest(change=change), self.assertRaises(ValueError):
                    release.load_metadata(path)
        provenance = {'tag': 'v001-old', 'inventory_sha256': 'b' * 64}
        with self.assertRaisesRegex(ValueError, 'must remain experiments'):
            metadata(schema=3, repackaged_from=provenance)
        with self.assertRaisesRegex(ValueError, 'schema 3'):
            metadata(schema=2, packager_sha='c' * 40)

    def test_target_helpers_bind_platform_backend_and_package_root(self):
        for schema in (1, 2, 3):
            value = metadata(schema=schema)
            for target in release.application_targets(value):
                platform = release.target_platform(value, target)
                backend = release.target_backend(value, target)
                self.assertIn(platform, release.TARGETS)
                self.assertIn(backend, release.GUI_BACKENDS)
                for base in release.package_bases(value, target):
                    self.assertTrue(base.endswith('-native' if schema == 1 else '-native-' + backend))
            invalid = 'linux-x86_64-rev' if schema == 1 else 'linux-x86_64'
            with self.assertRaisesRegex(ValueError, 'Unknown portable'):
                release.target_platform(value, invalid)

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


class MatrixTests(unittest.TestCase):
    def test_both_schemas_and_baselines_cover_every_expected_target(self):
        for schema in (1, 2, 3):
            for baseline in ('bookworm-sdk', 'ubuntu-22.04'):
                with self.subTest(schema=schema, baseline=baseline):
                    value = metadata(schema=schema, linux_baseline=baseline)
                    matrices = release.build_matrices(value)
                    linux = matrices['linux_matrix']['include']
                    windows = matrices['windows_matrix']['include']
                    compatibility = matrices['compatibility_matrix']['include']
                    backends = ('fltk',) if schema == 1 else ('fltk', 'rev')
                    expected = {f'{platform}-{backend}' if schema >= 2 else platform
                                for platform in ('linux-x86_64', 'linux-aarch64', 'windows-x86_64')
                                for backend in backends}
                    self.assertEqual({row['target'] for row in linux + windows}, expected)
                    self.assertEqual(len(linux), 2 * len(backends))
                    self.assertEqual(len(windows), len(backends))
                    self.assertEqual(len(compatibility), (10 if baseline == 'bookworm-sdk' else 11) * len(backends))
                    self.assertEqual(len({(row['target'], row['image']) for row in compatibility}), len(compatibility))
                    for row in linux + windows:
                        self.assertIn(row['backend'], backends)
                        if schema >= 2:
                            self.assertTrue(row['target'].endswith('-' + row['backend']))
                    for row in windows:
                        self.assertEqual(set(row), {'backend', 'target'})

    def test_compatibility_images_follow_each_architecture_and_abi_floor(self):
        for baseline in ('bookworm-sdk', 'ubuntu-22.04'):
            value = metadata(schema=2, linux_baseline=baseline)
            matrices = release.build_matrices(value)
            for row in matrices['linux_matrix']['include']:
                x86 = row['arch'] == 'x86_64'
                sdk = x86 and baseline == 'bookworm-sdk'
                self.assertEqual(row['sdk'], sdk)
                self.assertEqual(row['glibc'], '2.36' if sdk else '2.35')
                self.assertEqual(row['runner'], 'ubuntu-24.04' if x86 else 'ubuntu-24.04-arm')
                self.assertEqual(row['image'], 'debian:bookworm-slim' if sdk else 'ubuntu:22.04')
                expected_images = {'debian:bookworm-slim', 'debian:trixie-slim', 'ubuntu:24.04', 'ubuntu:26.04'}
                if not sdk:
                    expected_images.add('ubuntu:22.04')
                if x86:
                    expected_images.add('archlinux:base')
                copies = [copy for copy in matrices['compatibility_matrix']['include'] if copy['target'] == row['target']]
                self.assertEqual({copy['image'] for copy in copies}, expected_images)
                for copy in copies:
                    self.assertEqual({key: data for key, data in copy.items() if key != 'image'},
                                     {key: data for key, data in row.items() if key != 'image'})

    def test_cli_emits_all_matrix_outputs_and_preserves_existing_step_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'metadata.json'
            output = Path(temporary) / 'github-output'
            value = metadata(schema=2)
            release.write_json(path, value)
            output.write_text('tag=' + value['tag'] + '\n', encoding='utf-8')
            with patch('sys.stdout', new_callable=io.StringIO) as stdout:
                release.main(['matrices', '--metadata', str(path), '--github-output', str(output)])
            matrices = release.build_matrices(value)
            self.assertEqual(json.loads(stdout.getvalue()), matrices)
            rows = dict(line.split('=', 1) for line in output.read_text(encoding='utf-8').splitlines())
            self.assertEqual(rows.pop('tag'), value['tag'])
            self.assertEqual(json.loads(rows.pop('metadata_json')), value)
            self.assertEqual({key: json.loads(data) for key, data in rows.items()}, matrices)

    def test_cli_rejects_incomplete_backend_metadata_before_writing_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'metadata.json'
            output = Path(temporary) / 'github-output'
            value = metadata(schema=2)
            value['gui_backends'] = ['fltk']
            release.write_json(path, value)
            with patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit) as failure:
                release.main(['matrices', '--metadata', str(path), '--github-output', str(output)])
            self.assertEqual(failure.exception.code, 1)
            self.assertFalse(output.exists())


class ReleaseFixture:
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

    def backend_inputs(self, schema=2):
        value = metadata(schema=schema)
        release.write_json(self.metadata, value)
        # Use a separate root so no legacy fixture can accidentally satisfy it.
        self.artifacts = self.root / 'backend-inputs'
        self.artifacts.mkdir()
        for target in release.application_targets(value):
            directory = self.artifacts / target
            directory.mkdir()
            base = release.package_bases(value, target)[0]
            for extension in ('.tar.gz', '.zip'):
                backend_archive(directory / (base + extension), base, release.target_backend(value, target))
            self.sums(directory)
        return value


class InventoryTests(ReleaseFixture, unittest.TestCase):
    def test_schema3_requires_apt_assets_and_signature_verification(self):
        value = self.backend_inputs(schema=3)
        tool = apt_fixture()
        with patch.object(release, 'apt_tool', return_value=tool):
            self.assemble(repository='owner/repository', apt_signing_key=Path('key'), apt_signing_fingerprint='A' * 40)
            args = tool.build.call_args.args
            self.assertEqual(args[1:], (value, 'owner/repository', Path('key'), 'A' * 40))
            self.assertEqual(set(release.verify_release(self.output)[1]),
                             set(release.application_names(value).values()) | release.support_files(value) | APT_NAMES)
            tool.verify.assert_called_with(self.output, value)
            (self.output / 'InRelease').unlink()
            self.sums(self.output)
            with self.assertRaisesRegex(ValueError, 'missing an application target or support file'):
                release.verify_release(self.output)

    def test_apt_signature_failure_leaves_no_complete_release_output(self):
        self.backend_inputs(schema=3)
        tool = apt_fixture()
        tool.verify.side_effect = ValueError('APT signature mismatch')
        with patch.object(release, 'apt_tool', return_value=tool):
            with self.assertRaisesRegex(ValueError, 'signature mismatch'):
                self.assemble(repository='owner/repository', apt_signing_key=Path('key'), apt_signing_fingerprint='A' * 40)
        self.assertFalse(self.output.exists())

    def test_six_backend_archives_are_assembled_and_verified(self):
        value = self.backend_inputs()
        self.assertEqual(self.assemble(), value)
        verified, names = release.verify_release(self.output)
        self.assertEqual(verified, value)
        self.assertEqual(len(names), 9)
        self.assertEqual((self.output / 'warning.log').read_text(encoding='utf-8'), release.WARNING_LOG)
        for target, name in release.application_names(value).items():
            self.assertIn(target + release.application_targets(value)[target][2], name)
            original = release.native_pair(self.artifacts / target, value, target)
            self.assertEqual((self.output / name).read_bytes(), original.read_bytes())

    def test_missing_or_swapped_backend_build_info_rejects_both_formats(self):
        value = self.backend_inputs()
        target = 'linux-x86_64-fltk'
        directory = self.artifacts / target
        base = release.package_bases(value, target)[0]
        for extension in ('.tar.gz', '.zip'):
            archive = directory / (base + extension)
            for include_info, backend in ((False, 'fltk'), (True, 'rev')):
                backend_archive(archive, base, backend, include_info=include_info)
                self.sums(directory)
                with self.subTest(extension=extension, backend=backend, info=include_info):
                    with self.assertRaisesRegex(ValueError, 'backend build-info'):
                        self.assemble()
                    self.assertFalse(self.output.exists())
            backend_archive(archive, base, 'fltk')
            self.sums(directory)

    def test_new_metadata_requires_all_six_targets(self):
        self.backend_inputs()
        (self.artifacts / 'windows-x86_64-rev').rename(self.artifacts / 'missing')
        with self.assertRaisesRegex(ValueError, 'Missing or unsafe artifact directory'):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_wrong_root_and_duplicate_build_info_are_rejected(self):
        value = self.backend_inputs()
        target = 'linux-aarch64-rev'
        directory = self.artifacts / target
        base = release.package_bases(value, target)[0]
        archive = directory / (base + '.tar.gz')
        backend_archive(archive, base.replace('-rev', '-fltk'), 'rev')
        with self.assertRaisesRegex(ValueError, 'unexpected package root'):
            release.verify_archive_backend(archive, value, target)
        backend_archive(archive, base, 'rev', duplicate_info=True)
        with self.assertRaisesRegex(ValueError, 'duplicate or unsafe'):
            release.verify_archive_backend(archive, value, target)

    def test_final_inventory_rejects_backend_swap_even_with_updated_checksums(self):
        value = self.backend_inputs()
        self.assemble()
        names = release.application_names(value)
        (self.output / names['windows-x86_64-fltk']).write_bytes(
            (self.output / names['windows-x86_64-rev']).read_bytes())
        self.sums(self.output)
        with self.assertRaisesRegex(ValueError, 'unexpected package root'):
            release.verify_release(self.output)

    def test_schema2_warning_is_required_and_preserves_the_known_limitation(self):
        self.backend_inputs()
        self.assemble()
        warning = self.output / 'warning.log'
        warning.unlink()
        self.sums(self.output)
        with self.assertRaisesRegex(ValueError, 'missing an application target or support file'):
            release.verify_release(self.output)
        warning.write_text('No known warnings.\n', encoding='utf-8')
        self.sums(self.output)
        with self.assertRaisesRegex(ValueError, 'preserve the known Rev'):
            release.verify_release(self.output)

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

    def test_notes_show_experiment_warning_and_build_provenance(self):
        value = metadata(experiment=True)
        release.write_json(self.metadata, value)
        self.assemble()
        notes = (self.output / 'release-notes.md').read_text(encoding='utf-8')
        self.assertTrue(notes.startswith('> **Experimental build:**'))
        for expected in (value['tag'], value['source_sha'], value['run_id'], 'glibc 2.36',
                         'Portable release qualification notes.'):
            self.assertIn(expected, notes)

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

    def test_sdk_recipe_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'matching preserved source archive'):
            release.verify_sdk_pair({'datapump-sdk-one-linux-x86_64.tar.gz',
                                     'datapump-sdk-sources-two.tar.gz'})


class PublicationTests(ReleaseFixture, unittest.TestCase):
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
        self.assertIn('--verify-tag', create)
        self.assertEqual(create[create.index('--target') + 1], 'a' * 40)
        self.assertEqual(create[create.index('--title') + 1], metadata()['tag'])
        self.assertEqual(upload[:2], ['release', 'upload'])
        self.assertNotIn('--clobber', upload)
        self.assertEqual(len(upload[5:]), 6)
        self.assertFalse(any(call[:2] == ['release', 'edit'] for call in self.calls))
        self.assertEqual(self.calls[-3], ['api', '--method', 'POST',
            'repos/owner/repository/git/refs', '-f', f'ref=refs/tags/{metadata()["tag"]}',
            '-f', 'sha=' + 'a' * 40])

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

    def test_regular_publication_waits_for_separate_certification_to_become_latest(self):
        self.publish_fixture()
        with patch.object(release, 'gh', side_effect=self.github):
            release.publish(self.output, 'owner/repository', publish_now=True)
        self.assertIn('--latest=false', self.calls[-1])

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

    def test_concurrent_tag_creation_stops_release_creation(self):
        self.publish_fixture()
        def failure(arguments, check=True):
            if arguments[:3] == ['api', '--method', 'POST']:
                self.calls.append(arguments)
                raise subprocess.CalledProcessError(422, arguments, stderr='Reference already exists')
            return self.github(arguments, check=check)
        with patch.object(release, 'gh', side_effect=failure):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.output, 'owner/repository', publish_now=True)
        self.assertFalse(any(call[0] == 'release' for call in self.calls))

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


class AssetDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-download-test-')
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / 'asset.tar.gz'
        self.data = b'\x00\xffbinary archive\r\n'
        self.asset = {'id': 101, 'state': 'uploaded', 'size': len(self.data),
                      'digest': 'sha256:' + release.hashlib.sha256(self.data).hexdigest()}

    def stream(self, args, *, check, stdout, stderr):
        self.assertEqual(args, ['gh', 'api', 'repos/owner/repository/releases/assets/101',
                                '-H', 'Accept:application/octet-stream'])
        self.assertTrue(check)
        self.assertEqual(stderr, subprocess.PIPE)
        stdout.write(self.data)
        return subprocess.CompletedProcess(args, 0, None, b'')

    def test_download_streams_binary_bytes_by_asset_id_without_embedded_assets(self):
        with patch.object(release.subprocess, 'run', side_effect=self.stream):
            release.download_asset('owner/repository', self.asset, self.path)
        self.assertEqual(self.path.read_bytes(), self.data)

    def test_invalid_id_state_or_digest_fails_before_download(self):
        for change in ({'id': True}, {'id': -1}, {'state': 'starter'}, {'digest': None},
                       {'digest': 'sha256:' + 'f' * 63}):
            with self.subTest(change=change), patch.object(release.subprocess, 'run') as run:
                with self.assertRaises(ValueError):
                    release.download_asset('owner/repository', {**self.asset, **change}, self.path)
                run.assert_not_called()
                self.assertFalse(self.path.exists())

    def test_corruption_and_failed_request_remove_partial_file(self):
        with patch.object(release.subprocess, 'run', side_effect=self.stream):
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                release.download_asset('owner/repository', {**self.asset, 'digest': 'sha256:' + 'a' * 64}, self.path)
        self.assertFalse(self.path.exists())
        def fail(args, **options):
            options['stdout'].write(b'partial response')
            raise subprocess.CalledProcessError(1, args, stderr=b'network interrupted')
        with patch.object(release.subprocess, 'run', side_effect=fail):
            with self.assertRaises(subprocess.CalledProcessError):
                release.download_asset('owner/repository', self.asset, self.path)
        self.assertFalse(self.path.exists())

    def test_existing_file_is_never_overwritten(self):
        self.path.write_bytes(b'existing')
        with patch.object(release.subprocess, 'run') as run:
            with self.assertRaises(FileExistsError):
                release.download_asset('owner/repository', self.asset, self.path)
            run.assert_not_called()
        self.assertEqual(self.path.read_bytes(), b'existing')

    def test_legacy_digest_is_only_optional_when_explicitly_requested(self):
        with patch.object(release.subprocess, 'run', side_effect=self.stream):
            release.download_asset('owner/repository', {**self.asset, 'digest': None}, self.path,
                                   require_digest=False)
        self.assertEqual(self.path.read_bytes(), self.data)


class StagedPublicationTests(ReleaseFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.value = metadata()
        self.calls = []
        self.remote = {}
        self.exists = False
        self.reference = None
        self.info = {'id': 77, 'draft': True, 'prerelease': False,
                     'name': self.value['title'], 'tag_name': self.value['tag']}
        self.corrupt_download = {}
        self.missing_digest = None
        self.fail_upload = None
        patched = patch.object(release, 'gh', side_effect=self.github)
        patched.start()
        self.addCleanup(patched.stop)
        streaming = patch.object(release.subprocess, 'run', side_effect=self.stream_asset)
        streaming.start()
        self.addCleanup(streaming.stop)

    def github(self, args, check=True):
        self.calls.append(args)
        if args[:2] == ['api', '--paginate']:
            if '/assets?' in args[-1]:
                data = [{'id': index + 101, 'name': name, 'size': len(value), 'digest': None if self.missing_digest == name else 'sha256:' + release.hashlib.sha256(value).hexdigest(),
                         'state': 'uploaded'} for index, (name, value) in enumerate(self.remote.items())]
            else:
                data = [self.info] if self.exists else []
            return subprocess.CompletedProcess(args, 0, json.dumps(data), '')
        if args[:2] == ['api', '--include']:
            present = self.exists if '/releases/tags/' in args[-1] else self.reference is not None
            return subprocess.CompletedProcess(args, 0 if present else 1,
                'HTTP/2.0 200 OK\n\n{}' if present else 'HTTP/2.0 404 Not Found\n\n{}', '')
        if args[:3] == ['api', '--method', 'POST']:
            self.reference = next(value.removeprefix('sha=') for value in args if value.startswith('sha='))
        elif args[:1] == ['api']:
            data = {'object': {'type': 'commit', 'sha': self.reference, 'url': 'https://example.invalid/commit'}} if '/git/ref/' in args[-1] else {}
            return subprocess.CompletedProcess(args, 0, json.dumps(data), '')
        elif args[:2] == ['release', 'create']:
            self.exists = True
            self.info['name'] = args[args.index('--title') + 1]
            self.info['prerelease'] = '--prerelease' in args
        elif args[:2] == ['release', 'view']:
            return subprocess.CompletedProcess(args, 0, json.dumps(self.info), '')
        elif args[:2] == ['release', 'upload']:
            for filename in args[5:]:
                path = Path(filename)
                if path.name == self.fail_upload:
                    raise subprocess.CalledProcessError(1, args, stderr='upload failed')
                if path.name in self.remote:
                    raise subprocess.CalledProcessError(1, args, stderr='existing asset')
                self.remote[path.name] = path.read_bytes()
        elif args[:2] == ['release', 'edit']:
            self.assertIn('--latest=false', args)
            self.info['draft'] = False
        else:
            raise AssertionError(f'Unexpected gh command: {args}')
        return subprocess.CompletedProcess(args, 0, '', '')

    def stream_asset(self, args, *, check, stdout, stderr):
        self.assertTrue(check)
        self.assertEqual(stderr, subprocess.PIPE)
        self.assertEqual(args[:2], ['gh', 'api'])
        self.assertEqual(args[-2:], ['-H', 'Accept:application/octet-stream'])
        self.calls.append(args[1:])
        asset_id = int(args[2].rsplit('/', 1)[1])
        name = list(self.remote)[asset_id - 101]
        stdout.write(self.corrupt_download.get(name, self.remote[name]))
        return subprocess.CompletedProcess(args, 0, None, b'')

    def reserve(self, experiment=False):
        self.value = metadata(experiment=experiment)
        release.write_json(self.metadata, self.value)
        release.reserve(self.metadata, self.notes, 'owner/repository')

    def upload_all(self):
        for target in release.TARGETS:
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)

    def mutations(self):
        return [call for call in self.calls if call[:1] == ['release'] and call[1] in ('create', 'upload', 'edit')]

    def test_six_backend_uploads_require_complete_inventory_before_publication(self):
        self.value = self.backend_inputs()
        release.reserve(self.metadata, self.notes, 'owner/repository')
        targets = list(release.application_targets(self.value))
        for target in targets[:-1]:
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        with self.assertRaisesRegex(ValueError, 'six portable targets'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertTrue(self.info['draft'])
        self.assertNotIn('SHA256SUMS.txt', self.remote)
        target = targets[-1]
        with patch('sys.stdout', new_callable=io.StringIO):
            release.main(['upload', '--metadata', str(self.metadata), '--repo', 'owner/repository',
                          '--target', target, '--directory', str(self.artifacts / target)])
        with self.assertRaisesRegex(ValueError, 'overwrite an existing target'):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertEqual(len(self.remote), 10)
        self.assertEqual(set(self.remote), set(release.application_names(self.value).values()) |
                         release.support_files(self.value) | {'SHA256SUMS.txt'})
        self.assertEqual(self.remote['warning.log'], release.WARNING_LOG.encode())
        self.assertFalse(self.info['draft'])
        self.assertIn('--latest=false', self.mutations()[-1])
        self.assertTrue(all('--clobber' not in call for call in self.calls))

    def test_backend_mismatch_prevents_upload_before_network(self):
        value = self.backend_inputs()
        target = 'windows-x86_64-rev'
        directory = self.artifacts / target
        base = release.package_bases(value, target)[0]
        backend_archive(directory / (base + '.zip'), base, 'fltk')
        self.sums(directory)
        with self.assertRaisesRegex(ValueError, 'backend build-info'):
            release.upload(self.metadata, 'owner/repository', target, directory)
        self.assertEqual(self.calls, [])

    def test_draft_requires_original_warning_before_upload_or_finalize(self):
        self.value = self.backend_inputs()
        release.reserve(self.metadata, self.notes, 'owner/repository')
        original = self.remote.pop('warning.log')
        target = 'linux-x86_64-rev'
        with self.assertRaisesRegex(ValueError, 'missing support files'):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        self.remote['warning.log'] = b'No warnings.'
        with self.assertRaisesRegex(ValueError, 'preserve the known Rev'):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        self.remote['warning.log'] = original
        for target in release.application_targets(self.value):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        self.remote['warning.log'] = b'No warnings.'
        with self.assertRaisesRegex(ValueError, 'preserve the known Rev'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertTrue(self.info['draft'])
        self.assertNotIn('SHA256SUMS.txt', self.remote)

    def test_reserve_creates_exact_commit_draft_and_pending_notes_without_binaries(self):
        self.reserve(experiment=True)
        self.assertEqual(self.reference, self.value['source_sha'])
        self.assertEqual(set(self.remote), release.SUPPORT_FILES)
        self.assertEqual(self.info['name'], 'experiment')
        self.assertTrue(self.info['draft'])
        self.assertTrue(self.info['prerelease'])
        self.assertIn(release.CERTIFICATION_PENDING.encode(), self.remote['release-notes.md'])
        self.assertIn('--latest=false', next(call for call in self.calls if call[:2] == ['release', 'create']))

    def test_reserve_rejects_existing_draft_before_creating_a_tag(self):
        self.exists = True
        with self.assertRaisesRegex(ValueError, 'existing release or draft'):
            release.reserve(self.metadata, self.notes, 'owner/repository')
        self.assertIsNone(self.reference)
        self.assertEqual(self.mutations(), [])

    def test_upload_validates_both_archive_formats_then_uploads_one_renamed_target(self):
        self.reserve()
        target = 'linux-x86_64'
        release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        expected = release.application_names(self.value)[target]
        self.assertEqual(set(self.remote), release.SUPPORT_FILES | {expected})
        self.assertEqual(self.remote[expected], b'linux-x86_64 .tar.gz')
        self.assertTrue(all('--clobber' not in call for call in self.calls))

    def test_upload_rejects_corrupt_companion_before_network(self):
        next((self.artifacts / 'linux-x86_64').glob('*.zip')).write_bytes(b'corrupt companion')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            release.upload(self.metadata, 'owner/repository', 'linux-x86_64', self.artifacts / 'linux-x86_64')
        self.assertEqual(self.calls, [])

    def test_upload_refuses_existing_asset_and_changed_source_identity(self):
        self.reserve()
        target = 'windows-x86_64'
        release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        before = len(self.mutations())
        with self.assertRaisesRegex(ValueError, 'overwrite an existing target'):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        self.reference = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'exact source commit'):
            release.upload(self.metadata, 'owner/repository', 'linux-aarch64', self.artifacts / 'linux-aarch64')
        self.assertEqual(len(self.mutations()), before)

    def test_upload_requires_matching_workflow_metadata_and_pending_notes(self):
        self.reserve()
        original = self.remote['release-metadata.json']
        self.remote['release-metadata.json'] = json.dumps(metadata(run_id='999')).encode()
        with self.assertRaisesRegex(ValueError, 'another workflow run'):
            release.upload(self.metadata, 'owner/repository', 'linux-aarch64', self.artifacts / 'linux-aarch64')
        self.remote['release-metadata.json'] = original
        self.remote['release-notes.md'] = b'certification status removed'
        with self.assertRaisesRegex(ValueError, 'pending certification'):
            release.upload(self.metadata, 'owner/repository', 'linux-aarch64', self.artifacts / 'linux-aarch64')

    def test_published_or_mismatched_experiment_release_cannot_receive_assets(self):
        self.reserve()
        self.info['draft'] = False
        with self.assertRaisesRegex(ValueError, 'matching reserved draft'):
            release.upload(self.metadata, 'owner/repository', 'linux-aarch64', self.artifacts / 'linux-aarch64')
        self.info['draft'] = True
        self.info['prerelease'] = True
        with self.assertRaisesRegex(ValueError, 'matching reserved draft'):
            release.upload(self.metadata, 'owner/repository', 'linux-aarch64', self.artifacts / 'linux-aarch64')

    def test_finalize_requires_all_three_targets_and_rejects_extra_assets(self):
        self.reserve()
        with self.assertRaisesRegex(ValueError, 'three portable targets'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.upload_all()
        self.remote['unexpected.txt'] = b'unexpected'
        with self.assertRaisesRegex(ValueError, 'unexpected assets'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertNotIn('SHA256SUMS.txt', self.remote)
        self.assertTrue(self.info['draft'])

    def test_finalize_verifies_server_digests_before_uploading_checksum_and_publishing(self):
        self.reserve()
        self.upload_all()
        release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertEqual(set(self.remote), set(release.application_names(self.value).values()) | release.SUPPORT_FILES | {'SHA256SUMS.txt'})
        release.verify_release(self.output)
        self.assertFalse(self.info['draft'])
        self.assertEqual(self.mutations()[-2][1], 'upload')
        self.assertEqual(Path(self.mutations()[-2][-1]).name, 'SHA256SUMS.txt')
        self.assertEqual(self.mutations()[-1][1], 'edit')
        self.assertIn('--latest=false', self.mutations()[-1])

    def test_finalize_without_publish_keeps_draft_with_complete_inventory(self):
        self.reserve()
        self.upload_all()
        release.finalize(self.metadata, 'owner/repository', self.output)
        self.assertTrue(self.info['draft'])
        self.assertIn('SHA256SUMS.txt', self.remote)
        self.assertFalse(any(call[1] == 'edit' for call in self.mutations()))

    def test_corrupt_download_or_missing_server_digest_never_finalizes(self):
        self.reserve()
        self.upload_all()
        archive = release.application_names(self.value)['windows-x86_64']
        self.corrupt_download[archive] = b'corruption'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.corrupt_download.clear()
        self.missing_digest = archive
        with self.assertRaisesRegex(ValueError, 'completed SHA-256 digest'):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertFalse(self.output.exists())
        self.assertNotIn('SHA256SUMS.txt', self.remote)
        self.assertTrue(self.info['draft'])

    def test_checksum_upload_failure_never_publishes(self):
        self.reserve()
        self.upload_all()
        self.fail_upload = 'SHA256SUMS.txt'
        with self.assertRaises(subprocess.CalledProcessError):
            release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertTrue(self.info['draft'])
        self.assertFalse(any(call[1] == 'edit' for call in self.mutations()))

    def test_schema3_finalize_signs_then_uploads_complete_apt_inventory_before_publish(self):
        self.value = self.backend_inputs(schema=3)
        release.reserve(self.metadata, self.notes, 'owner/repository')
        for target in release.application_targets(self.value):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        tool = apt_fixture()
        with patch.object(release, 'apt_tool', return_value=tool):
            release.finalize(self.metadata, 'owner/repository', self.output, True,
                             apt_signing_key=Path('key'), apt_signing_fingerprint='A' * 40)
            self.assertEqual(set(self.remote), release.required_assets(self.value) | {'SHA256SUMS.txt'})
            release.verify_release(self.output)
        self.assertEqual(tool.build.call_args.args[1:],
                         (self.value, 'owner/repository', Path('key'), 'A' * 40))
        upload, edit = self.mutations()[-2:]
        self.assertEqual({Path(name).name for name in upload[5:]}, APT_NAMES | {'SHA256SUMS.txt'})
        self.assertIn('--latest=false', edit)
        self.assertFalse(self.info['draft'])

    def test_failed_apt_signing_never_finalizes_or_publishes(self):
        self.value = self.backend_inputs(schema=3)
        release.reserve(self.metadata, self.notes, 'owner/repository')
        for target in release.application_targets(self.value):
            release.upload(self.metadata, 'owner/repository', target, self.artifacts / target)
        tool = apt_fixture()
        tool.build.side_effect = ValueError('Missing signing key')
        before = len(self.mutations())
        with patch.object(release, 'apt_tool', return_value=tool):
            with self.assertRaisesRegex(ValueError, 'signing key'):
                release.finalize(self.metadata, 'owner/repository', self.output, True)
        self.assertEqual(len(self.mutations()), before)
        self.assertTrue(self.info['draft'])
        self.assertNotIn('SHA256SUMS.txt', self.remote)
        self.assertFalse(self.output.exists())

    def prepare_repackage_source(self):
        self.value = self.backend_inputs()
        self.assemble()
        self.remote = {path.name: path.read_bytes() for path in self.output.iterdir()}
        self.reference, self.exists = self.value['source_sha'], True
        self.info['tag_name'] = self.value['tag']

    def test_repackage_preserves_all_source_bytes_and_records_new_packager_and_origin(self):
        self.prepare_repackage_source()
        original = dict(self.remote)
        destination = self.root / 'repackaged'
        tool = apt_fixture()
        with patch.object(release, 'apt_tool', return_value=tool), patch.object(release, 'publish') as publish:
            value = release.repackage(self.value['tag'], 'owner/repository', destination,
                                      version='vapt', run_id='456', run_attempt='2', packager_sha='b' * 40,
                                      apt_signing_key=Path('key'), apt_signing_fingerprint='A' * 40)
            release.verify_release(destination)
        self.assertEqual(self.remote, original)
        self.assertEqual(value['source_sha'], self.value['source_sha'])
        self.assertEqual(value['packager_sha'], 'b' * 40)
        self.assertEqual(value['repackaged_from'], {'tag': self.value['tag'],
                         'inventory_sha256': release.hashlib.sha256(original['SHA256SUMS.txt']).hexdigest()})
        self.assertEqual(value['title'], 'experiment')
        self.assertTrue(value['experiment'])
        for target, name in release.application_names(value).items():
            self.assertEqual((destination / name).read_bytes(), original[release.application_names(self.value)[target]])
        publish.assert_called_once_with(destination, 'owner/repository', publish_now=False)

    def test_corrupt_repackage_source_cannot_sign_or_publish(self):
        self.prepare_repackage_source()
        archive = release.application_names(self.value)['linux-x86_64-fltk']
        self.corrupt_download[archive] = b'changed'
        tool = apt_fixture()
        with patch.object(release, 'apt_tool', return_value=tool), patch.object(release, 'publish') as publish:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                release.repackage(self.value['tag'], 'owner/repository', self.root / 'repackaged',
                                  run_id='456', packager_sha='b' * 40,
                                  apt_signing_key=Path('key'), apt_signing_fingerprint='A' * 40)
        tool.build.assert_not_called()
        publish.assert_not_called()


if __name__ == '__main__':
    unittest.main()
