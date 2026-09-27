#!/usr/bin/env python3
"""Published-byte identity, archive extraction and certification promotion tests."""
import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest
from unittest.mock import Mock, patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('certify', ROOT / 'tools/certify-release.py')
certify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(certify)


def digest(data):
    return hashlib.sha256(data).hexdigest()


SMOKE_DIAGNOSTIC = ('INCOMPLETE GUI_SMOKE_BUDGET: phase=17 elapsed=600.018200 budget=600.000000 '
                    'tx_id=11 fraction=0.225619 media_seconds=30.392361 samples=441901 '
                    'tail=0 progress_age=2.343160 result=incomplete')


class CertificationFixture:
    schema = 1

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-certify-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.metadata = certify.release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1',
            now=datetime(2026, 9, 22, 7, 52, tzinfo=timezone.utc), cmake_version='0.7.2', schema=self.schema)
        if self.schema == 1:
            self.metadata['schema'] = 1
            self.metadata.pop('gui_backends', None)
        self.tag = self.metadata['tag']
        self.repository = 'owner/project'
        self.commit = self.metadata['source_sha']
        self.files = {'release-notes.md': b'Original release notes\n'}
        if self.schema >= 2:
            self.files['warning.log'] = b'Display cadence warnings are advisory; other checks remain mandatory.\n'
        for target, name in certify.release.application_names(self.metadata).items():
            self.files[name] = self.archive(target)
        self.published = {'id': 456, 'tag_name': self.tag, 'draft': False, 'prerelease': False, 'name': self.tag,
                          'html_url': f'https://github.com/{self.repository}/releases/tag/{self.tag}',
                          'body': 'Original release notes', 'assets': []}
        self.refresh_metadata()
        self.calls, self.uploads, self.edits, self.downloads = [], {}, [], []
        for module in (certify, certify.release):
            patched = patch.object(module, 'gh', side_effect=self.gh)
            patched.start()
            self.addCleanup(patched.stop)
        patched = patch.object(certify.release, 'download_asset', side_effect=self.download_asset)
        patched.start()
        self.addCleanup(patched.stop)

    def archive(self, target, extra=None):
        system, _, extension = certify.release.application_targets(self.metadata)[target]
        root = sorted(certify.release.package_bases(self.metadata, target))[0]
        executable = '.exe' if system == 'Windows' else ''
        files = {f'{root}/manifest.sha256': b'inventory',
                 f'{root}/bin/pump{executable}': b'CLI', f'{root}/bin/datapump-gui{executable}': b'GUI'}
        if self.metadata['schema'] >= 2:
            backend = certify.release.target_backend(self.metadata, target)
            files[f'{root}/share/doc/datapump/build-info.txt'] = f'DataPump 0.7.2\nGUI: ON ({backend})\n'.encode()
        if extra:
            files.update(extra)
        buffer = io.BytesIO()
        if extension == '.zip':
            with zipfile.ZipFile(buffer, 'w') as archive:
                for name, data in files.items():
                    archive.writestr(name, data)
        else:
            with tarfile.open(fileobj=buffer, mode='w:gz') as archive:
                for name, data in files.items():
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.mode = len(data), 0o755
                    archive.addfile(entry, io.BytesIO(data))
        return buffer.getvalue()

    def refresh_metadata(self):
        self.files['release-metadata.json'] = (json.dumps(self.metadata) + '\n').encode()
        self.files['SHA256SUMS.txt'] = ''.join(
            f'{digest(data)}  {name}\n' for name, data in sorted(self.files.items())
            if name != 'SHA256SUMS.txt').encode()
        self.refresh_assets()

    def refresh_assets(self):
        self.assets = [{'id': i, 'name': name, 'state': 'uploaded', 'size': len(data),
                        'digest': 'sha256:' + digest(data)}
                       for i, (name, data) in enumerate(self.files.items(), 1)]

    def download_asset(self, repository, asset, path, *, require_digest=True):
        self.assertEqual(repository, self.repository)
        self.assertFalse(require_digest)
        self.assertIsInstance(asset['id'], int)
        self.assertFalse(path.exists())
        self.downloads.append(asset['name'])
        with path.open('xb') as output:
            output.write(self.files[asset['name']])

    def gh(self, args, **_):
        self.calls.append(args)
        if args[0] == 'api':
            endpoint = args[-1]
            if '/releases/tags/' in endpoint:
                value = copy.deepcopy(self.published)
            elif endpoint == f'repos/{self.repository}/releases/456/assets?per_page=100':
                self.assertIn('--paginate', args)
                # GitHub CLI emits adjacent JSON page arrays without --slurp.
                pages = json.dumps(self.assets[:3]) + '\n' + json.dumps(self.assets[3:])
                return subprocess.CompletedProcess(args, 0, pages, '')
            elif '/git/ref/tags/' in endpoint:
                value = {'object': {'type': 'commit', 'sha': self.commit}}
            else:
                self.fail(f'Unexpected API endpoint: {endpoint}')
            return subprocess.CompletedProcess(args, 0, json.dumps(value), '')
        if args[:2] == ['release', 'upload']:
            self.assertNotIn('--clobber', args)
            for name in args[5:]:
                path = Path(name)
                self.uploads[path.name] = path.read_bytes()
        elif args[:2] == ['release', 'edit']:
            notes = Path(args[args.index('--notes-file') + 1]).read_text()
            self.edits.append((args, notes))
        else:
            self.fail(f'Unexpected gh command: {args}')
        return subprocess.CompletedProcess(args, 0, '', '')

    def prepare(self, **options):
        return certify.prepare(self.repository, self.tag, self.root / 'download', **options)

    def results(self, jobs=None, **extra):
        value = {'source_sha': self.metadata['source_sha'], 'inventory_sha256': digest(self.files['SHA256SUMS.txt']),
                 'jobs': dict.fromkeys(certify.required_jobs(self.metadata), 'success') if jobs is None else jobs}
        if self.metadata['schema'] >= 2:
            value['tested_targets'] = list(certify.release.application_targets(self.metadata))
        value.update(extra)
        path = self.root / 'results.json'
        path.write_text(json.dumps(value))
        return path

    def record(self, **options):
        return certify.record(self.repository, self.tag, '789', self.results(**options), '2')

    def smoke_warning(self, **options):
        value = {'code': 'gui-smoke-workload-incomplete',
                 'target': next(iter(certify.release.application_targets(self.metadata))),
                 'scope': 'source/native', 'diagnostic': SMOKE_DIAGNOSTIC,
                 'source_sha': self.metadata['source_sha'],
                 'inventory_sha256': digest(self.files['SHA256SUMS.txt']),
                 'run_id': '789', 'run_attempt': '2'}
        value.update(options)
        return value


class SmokeWarningCollectionTests(CertificationFixture, unittest.TestCase):
    def artifact(self, job='source-linux-x86_64-rev', attempt='1', status='incomplete'):
        directory = self.root / 'artifacts' / f'certification-smoke-{job}-attempt-{attempt}'
        directory.mkdir(parents=True)
        identity = {'run_id': '789', 'run_attempt': attempt}
        (directory / 'job-coverage.json').write_text(json.dumps(identity))
        if status is not None:
            report = dict(identity, status=status, source_sha=self.metadata['source_sha'])
            if status == 'incomplete':
                report['warning'] = self.smoke_warning(run_attempt=attempt)
            (directory / 'gui_workflow.json').write_text(json.dumps(report))
        return directory

    def collect(self, attempt='2'):
        return certify.collect_smoke_warnings(self.root / 'artifacts', '789', attempt)

    def test_partial_rerun_retains_earlier_successful_job_warning(self):
        self.artifact()
        self.artifact('copy-0', '2', 'passed')
        warnings = self.collect()
        self.assertEqual(warnings, [self.smoke_warning(run_attempt='1')])
        evidence = self.record(smoke_warnings=warnings)
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertEqual(evidence['smoke_warnings'][0]['run_attempt'], '1')

    def test_full_rerun_pass_or_no_smoke_replaces_earlier_warning(self):
        self.artifact()
        latest = self.artifact(attempt='2', status='passed')
        self.assertEqual(self.collect(), [])
        (latest / 'gui_workflow.json').unlink()
        # An always-written manifest clears old smoke evidence for a WGL omission.
        self.assertEqual(self.collect(), [])

    def test_newest_attempt_is_numeric(self):
        self.artifact(attempt='2')
        self.artifact(attempt='10', status='passed')
        self.assertEqual(self.collect('10'), [])

    def test_future_and_malformed_artifact_names_are_rejected(self):
        original = self.artifact()
        for name in ('certification-smoke-source-attempt-3', 'certification-smoke-source-attempt-0',
                     'certification-smoke-source-attempt-01', 'certification-smoke-source-attempt-x',
                     'unexpected-artifact'):
            changed = original.with_name(name)
            original.rename(changed)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'artifact'):
                self.collect()
            changed.rename(original)

    def test_selected_manifest_must_have_exact_string_identity(self):
        directory = self.artifact()
        manifest = directory / 'job-coverage.json'
        manifest.unlink()
        with self.assertRaisesRegex(ValueError, 'manifest'):
            self.collect()
        for value in (None, {}, {'run_id': '788', 'run_attempt': '1'},
                      {'run_id': '789', 'run_attempt': '2'}, {'run_id': '789', 'run_attempt': 1},
                      {'run_id': '789', 'run_attempt': '1', 'extra': 'not allowed'}):
            manifest.write_text(json.dumps(value))
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'manifest identity'):
                self.collect()

    def test_all_report_identities_match_the_selected_artifact(self):
        directory = self.artifact()
        path = directory / 'gui_workflow.json'
        incomplete = json.loads(path.read_text())
        for status in ('passed', 'failed', 'incomplete'):
            for change in ({'run_id': '788'}, {'run_attempt': '2'}, {'run_attempt': 1}):
                report = dict(incomplete, status=status, **change)
                if status != 'incomplete':
                    report.pop('warning')
                path.write_text(json.dumps(report))
                with self.subTest(status=status, change=change), self.assertRaisesRegex(ValueError, 'report identity'):
                    self.collect()
        for change in ({'run_id': '788'}, {'run_attempt': '2'}, {'source_sha': 'b' * 40}):
            report = dict(incomplete, warning=dict(incomplete['warning'], **change))
            path.write_text(json.dumps(report))
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'warning identity'):
                self.collect()

    def test_only_known_aggregate_may_omit_identity(self):
        directory = self.artifact(status=None)
        path = directory / 'native-tests.json'
        path.write_text(json.dumps({'status': 'passed_with_warnings', 'tests': []}))
        self.assertEqual(self.collect(), [])
        path.write_text(json.dumps({'status': 'incomplete', 'warning': self.smoke_warning()}))
        with self.assertRaisesRegex(ValueError, 'report identity'):
            self.collect()

    def test_collection_bounds_warning_count(self):
        directory = self.artifact()
        report = (directory / 'gui_workflow.json').read_text()
        for i in range(64):
            (directory / f'package-{i}.json').write_text(report)
        with self.assertRaisesRegex(ValueError, 'more than 64'):
            self.collect()


class CertificationWorkflowTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-certify-workflow-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def run_workflow_python(self, marker, **environment):
        workflow = (ROOT / '.github/workflows/certify.yml').read_text()
        # Execute the workflow's programs, rather than duplicating their logic or
        # treating a matching command string as evidence of required coverage.
        pattern = rf"(?m)^([ ]*)python3 - <<'{marker}'\n(.*?)^\1{marker}$"
        programs = list(re.finditer(pattern, workflow, re.DOTALL))
        self.assertEqual(len(programs), 1, f'Expected one {marker} workflow program')
        output = self.root / 'github-output'
        output.unlink(missing_ok=True)
        result = subprocess.run([sys.executable, '-c', textwrap.dedent(programs[0][2])],
            cwd=self.root, env=dict(os.environ, GITHUB_OUTPUT=str(output), **environment),
            capture_output=True, text=True, timeout=20)
        values = dict(line.split('=', 1) for line in output.read_text().splitlines()) if output.exists() else {}
        return result, {name: json.loads(value) for name, value in values.items()}

    def test_linux_matrix_requires_both_scopes_without_changing_release_rows(self):
        (self.root / 'tools').mkdir()
        for name in ('release.py', 'release-dependencies.py'):
            shutil.copyfile(ROOT / 'tools' / name, self.root / 'tools' / name)
        (self.root / 'release-info').mkdir()
        for schema in (1, 2, 6):
            for baseline in ('bookworm-sdk', 'ubuntu-22.04'):
                metadata = certify.release.make_metadata(source_sha='a' * 40, run_id='123',
                    run_attempt='1', cmake_version='1.0.0', schema=schema, linux_baseline=baseline)
                (self.root / 'release-info/release-metadata.json').write_text(json.dumps(metadata))
                original = certify.release.build_matrices(metadata)
                with self.subTest(schema=schema, baseline=baseline):
                    result, matrices = self.run_workflow_python('PYMATRIX')
                    self.assertEqual(result.returncode, 0, result.stderr)
                    expected = [dict(row, scope=scope) for row in original['linux_matrix']['include']
                                for scope in ('main', 'calibration')]
                    self.assertCountEqual(matrices['linux_matrix']['include'], expected)
                    self.assertEqual(matrices['windows_matrix'], original['windows_matrix'])
                    self.assertEqual(matrices['compatibility_matrix'], original['compatibility_matrix'])
                    self.assertEqual(matrices['metadata_json'], metadata)

    def windows_jobs(self):
        needs = {name: {'result': 'success', 'outputs': {}} for name in
                 ('windows-fltk', 'windows-rev', 'windows-fltk-calibration', 'windows-rev-calibration')}
        needs['windows-rev']['outputs']['warnings'] = json.dumps([certify.windows_certification.warning_record()])
        return needs

    def collect_windows(self, needs, targets=None):
        if targets is None:
            targets = ['windows-x86_64-fltk', 'windows-x86_64-rev']
        return self.run_workflow_python('PYWINDOWS', JOB_RESULTS=json.dumps(needs), TARGETS=json.dumps(targets))

    def test_windows_main_warning_survives_empty_or_unrelated_calibration_outputs(self):
        for output in ({}, {'warnings': '[]'}, {'warnings': '[{"code":"not-the-main-probe"}]'}):
            needs = self.windows_jobs()
            needs['windows-rev-calibration']['outputs'] = output
            with self.subTest(output=output):
                result, values = self.collect_windows(needs)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(values['warnings'], [certify.windows_certification.warning_record()])

    def test_every_required_windows_main_and_calibration_child_must_succeed(self):
        for name in self.windows_jobs():
            for status in ('failure', 'cancelled', 'skipped', None):
                needs = self.windows_jobs()
                if status is None:
                    needs.pop(name)
                else:
                    needs[name]['result'] = status
                with self.subTest(name=name, status=status):
                    result, values = self.collect_windows(needs)
                    self.assertNotEqual(result.returncode, 0)
                    if 'windows-rev' in needs:
                        self.assertEqual(values['warnings'], [certify.windows_certification.warning_record()])

    def test_legacy_fltk_release_does_not_require_unpublished_rev_children(self):
        needs = self.windows_jobs()
        for name in ('windows-rev', 'windows-rev-calibration'):
            needs[name] = {'result': 'skipped', 'outputs': {}}
        result, values = self.collect_windows(needs, ['windows-x86_64'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(values['warnings'], [])


class CertificationTests(CertificationFixture, unittest.TestCase):
    def test_prepare_pins_metadata_inventory_and_source_without_archive_download(self):
        state = self.prepare()
        self.assertEqual(state['metadata'], self.metadata)
        self.assertEqual(self.published['assets'], [])
        self.assertEqual(set(state['assets']), set(self.files))
        self.assertEqual(state['inventory_sha256'], digest(self.files['SHA256SUMS.txt']))
        self.assertEqual(sorted(path.name for path in state['directory'].iterdir()),
                         ['SHA256SUMS.txt', 'release-metadata.json'])
        output = self.root / 'outputs'
        certify.output_values(state, output)
        self.assertIn('source_sha=' + self.commit, output.read_text())
        self.assertIn('baseline=bookworm-sdk', output.read_text())
        values = certify.output_values(state, None)
        self.assertEqual(values['schema'], '1')
        self.assertEqual(json.loads(values['gui_backends']), ['fltk'])
        self.assertEqual(set(json.loads(values['application_targets'])), set(certify.release.TARGETS))

    def test_wrong_source_tag_and_draft_are_rejected(self):
        self.commit = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'source commit or tag'):
            self.prepare()
        self.published['draft'] = True
        with self.assertRaisesRegex(ValueError, 'non-draft'):
            certify.prepare(self.repository, self.tag, self.root / 'other')

    def test_asset_pagination_rejects_duplicates_and_never_uses_stale_embedded_inventory(self):
        self.assets.append(copy.deepcopy(self.assets[0]))
        with self.assertRaisesRegex(ValueError, 'duplicate asset names'):
            self.prepare()
        self.assets.pop()
        self.published['assets'] = copy.deepcopy(self.assets)
        self.assets.clear()
        with self.assertRaisesRegex(ValueError, 'Missing published asset'):
            certify.prepare(self.repository, self.tag, self.root / 'stale')
        self.assertFalse(self.downloads)

    def test_missing_or_invalid_rest_release_id_is_rejected(self):
        for value in (None, '456', True, 0, -1):
            self.published['id'] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'REST release ID'):
                certify.prepare(self.repository, self.tag, self.root / str(value))
        self.assertFalse(self.downloads)

    def test_asset_download_failure_cannot_issue_or_promote_a_certificate(self):
        with patch.object(certify.release, 'download_asset', side_effect=subprocess.CalledProcessError(1, ['gh', 'api'])):
            with self.assertRaises(subprocess.CalledProcessError):
                self.record()
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_missing_target_is_rejected(self):
        del self.files[certify.release.application_names(self.metadata)['linux-aarch64']]
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application'):
            self.prepare()

    def test_unrecognized_inventory_asset_and_duplicate_checksums_are_rejected(self):
        self.files['surprise.bin'] = b'Unexpected application'
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'SDK assets'):
            self.prepare()
        del self.files['surprise.bin']
        self.refresh_metadata()
        self.files['SHA256SUMS.txt'] += self.files['SHA256SUMS.txt'].splitlines(keepends=True)[0]
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            certify.prepare(self.repository, self.tag, self.root / 'duplicate')

    def test_metadata_checksum_and_replaced_asset_are_rejected(self):
        self.files['release-metadata.json'] += b' '
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.prepare()

    def test_changed_inventory_since_prepare_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'inventory changed'):
            self.prepare(expected_inventory='f' * 64)

    def test_download_checks_one_archive_and_extracts_expected_root(self):
        for target in certify.release.TARGETS:
            with self.subTest(target=target):
                state = certify.download(self.repository, self.tag, target, self.root / target,
                                         digest(self.files['SHA256SUMS.txt']))
                self.assertEqual(state['archive'].name, certify.release.application_names(self.metadata)[target])
                self.assertTrue((state['package_root'] / 'manifest.sha256').is_file())
                self.assertEqual(state['package_root'].parent.name, 'offline destination with spaces')
                app_downloads = [name for name in self.downloads if name.startswith('DataPump-')]
                self.assertEqual(len(app_downloads), list(certify.release.TARGETS).index(target) + 1)

    def test_checksum_mismatch_is_rejected_before_extraction(self):
        name = certify.release.application_names(self.metadata)['linux-x86_64']
        self.files[name] += b'changed'
        # Simulate a server without asset digests; local hashing is still mandatory.
        for asset in self.assets:
            asset.pop('digest')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            certify.download(self.repository, self.tag, 'linux-x86_64', self.root / 'download',
                             digest(self.files['SHA256SUMS.txt']))
        self.assertFalse((self.root / 'download/offline destination with spaces').exists())

    def test_archive_traversal_wrong_architecture_and_links_are_rejected(self):
        for member in ('../escape', 'DataPump-0.7.2-Linux-aarch64-native/wrong',
                       'DataPump-0.7.2-Linux-x86_64-native/../../escape',
                       'DataPump-0.7.2-Linux-x86_64-native/C:/escape'):
            archive = self.root / 'bad.tar.gz'
            archive.write_bytes(self.archive('linux-x86_64', {member: b'bad'}))
            with self.subTest(member=member), self.assertRaisesRegex(ValueError, 'Unsafe'):
                certify.extract_archive(archive, self.root, self.metadata, 'linux-x86_64')
        with tarfile.open(archive, 'w:gz') as output:
            member = tarfile.TarInfo('DataPump-0.7.2-Linux-x86_64-native/link')
            member.type, member.linkname = tarfile.SYMTYPE, '/etc/passwd'
            output.addfile(member)
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            certify.extract_archive(archive, self.root, self.metadata, 'linux-x86_64')

    def test_passed_legacy_report_uploads_only_evidence_and_preserves_apt_latest(self):
        evidence = self.record()
        self.assertEqual(evidence['status'], 'passed')
        self.assertEqual(set(evidence['assets']), set(certify.release.application_names(self.metadata).values()))
        self.assertEqual(set(self.uploads), {'certification-789-attempt-2.json', 'certification-789-attempt-2.md'})
        self.assertIn('--latest=false', self.edits[0][0])
        self.assertFalse(evidence['latest_eligible'])
        self.assertIn('cannot become Latest', self.edits[0][1])
        self.assertTrue(self.edits[0][1].startswith('Original release notes'))
        upload = next(i for i, args in enumerate(self.calls) if args[:2] == ['release', 'upload'])
        edit = next(i for i, args in enumerate(self.calls) if args[:2] == ['release', 'edit'])
        self.assertLess(upload, edit)

    def test_failed_attempt_then_successful_retry_preserves_evidence_and_identity(self):
        original_files = dict(self.files)
        for experiment in (False, True):
            with self.subTest(experiment=experiment):
                self.files = dict(original_files)
                self.metadata.update(experiment=experiment, title='experiment' if experiment else self.tag)
                self.published.update(prerelease=experiment, name=self.metadata['title'],
                                      body=certify.release.CERTIFICATION_PENDING + '\n\nOriginal release notes')
                self.refresh_metadata()
                original_assets = dict(self.files)
                self.calls.clear()
                self.uploads.clear()
                self.edits.clear()

                # Persist uploads and edits like GitHub, so the retry discovers
                # the first attempt's real asset inventory and release body.
                def persist(args, **options):
                    result = self.gh(args, **options)
                    if args[:2] == ['release', 'upload']:
                        self.files.update(self.uploads)
                        self.refresh_assets()
                    elif args[:2] == ['release', 'edit']:
                        self.published['body'] = self.edits[-1][1]
                    return result

                failed_jobs = dict.fromkeys(certify.REQUIRED_JOBS, 'success')
                failed_jobs['windows-tests'] = 'failure'
                with patch.object(certify, 'gh', side_effect=persist):
                    failed = certify.record(self.repository, self.tag, '789',
                                            self.results(jobs=failed_jobs), '1')
                    first_reports = dict(self.uploads)
                    passed = certify.record(self.repository, self.tag, '789', self.results(), '2')

                self.assertEqual((failed['status'], passed['status']), ('failed', 'passed'))
                for field in ('source_sha', 'inventory_sha256', 'assets'):
                    self.assertEqual(failed[field], passed[field])
                self.assertEqual(set(self.uploads), {
                    f'certification-789-attempt-{attempt}.{extension}'
                    for attempt in (1, 2) for extension in ('json', 'md')})
                for name, data in {**original_assets, **first_reports}.items():
                    self.assertEqual(self.files[name], data)
                first_json = json.loads(self.files['certification-789-attempt-1.json'])
                self.assertEqual(first_json['status'], 'failed')
                self.assertEqual(first_json['jobs']['windows-tests'], 'failure')
                body = self.published['body']
                self.assertIn('**Certification passed.**', body)
                self.assertIn('Original release notes', body)
                self.assertIn('attempt 1', body)
                self.assertIn('attempt 2', body)
                self.assertIn('**failed**', body)
                self.assertIn('**passed**', body)
                self.assertIn('--latest=false', self.edits[0][0])
                retry_flags = self.edits[1][0]
                if experiment:
                    self.assertIn('--latest=false', retry_flags)
                    self.assertNotIn('--latest=true', retry_flags)
                    self.assertIn('--prerelease', retry_flags)
                    self.assertEqual(retry_flags[retry_flags.index('--title') + 1], 'experiment')
                else:
                    self.assertIn('--latest=false', retry_flags)
                    self.assertNotIn('--latest=true', retry_flags)

    def test_status_replaces_pending_notice_and_preserves_prose_and_history(self):
        history = 'Certification run 100: **passed** ([report](https://example.test/old.md)).'
        self.published['body'] = certify.release.CERTIFICATION_PENDING + '\n\nOriginal details\n\n' + history
        self.record()
        body = self.edits[-1][1]
        self.assertNotIn(certify.release.CERTIFICATION_PENDING, body)
        self.assertNotIn('has not completed', body)
        self.assertEqual(body.count('**Certification passed.**'), 1)
        self.assertIn('Original details', body)
        self.assertIn(history, body)
        # A later failed attempt updates the current status, preserving the
        # previous successful evidence and the newly appended failure report.
        self.published['body'] = body
        self.record(jobs={'linux-tests': 'failure'})
        failed = self.edits[-1][1]
        self.assertIn('**Certification failed.**', failed)
        self.assertNotIn('**Certification passed.**', failed)
        self.assertIn(history, failed)
        self.assertIn('**passed**', failed)
        self.assertIn('--latest=false', self.edits[-1][0])

    def test_legacy_pending_marker_is_replaced_only_once(self):
        self.published['body'] = '**Certification pending.** Original details\n\nQuoted **Certification pending.**'
        self.record()
        self.assertTrue(self.edits[-1][1].startswith('**Certification passed.** Original details'))
        self.assertIn('Quoted **Certification pending.**', self.edits[-1][1])

    def test_evidence_identifies_run_and_required_source_and_archive_coverage(self):
        for baseline in ('bookworm-sdk', 'ubuntu-22.04'):
            with self.subTest(baseline=baseline):
                self.metadata['linux_baseline'] = baseline
                self.refresh_metadata()
                evidence = self.record()
                expected_url = f'https://github.com/{self.repository}/actions/runs/789'
                self.assertEqual(evidence['run_url'], expected_url)
                scope = evidence['required_coverage']
                self.assertEqual(set(scope['published_archives']), set(certify.release.TARGETS))
                x86 = scope['published_archives']['linux-x86_64']
                self.assertEqual('Ubuntu 22.04' in x86['environments'], baseline == 'ubuntu-22.04')
                self.assertIn('Arch Linux', x86['environments'])
                arm = scope['published_archives']['linux-aarch64']
                self.assertIn('Ubuntu 22.04', arm['environments'])
                self.assertNotIn('Arch Linux', arm['environments'])
                self.assertEqual(arm['glibc_max'], '2.35')
                self.assertEqual(scope['source_tests']['windows-x86_64']['environment'],
                                 'Selected Windows x64 hosted runner')
                self.assertEqual(scope['published_archives']['windows-x86_64']['environments'],
                                 ['Selected Windows x64 hosted runner'])
                report = self.uploads['certification-789-attempt-2.md'].decode()
                self.assertNotIn('Windows Server 2022', report)
                self.assertIn('image recorded in the workflow logs', report)
                self.assertIn('Windows 10/11 client installations are not qualified', report)
                self.assertIn(expected_url + '/attempts/2', report)
                self.assertIn('Source suites rebuild the recorded release commit', report)
                self.assertIn('Archive checks run the published bytes', report)
                self.assertIn('Hosted certification succeeds with status **passed** or **passed_with_warnings**', report)
                self.assertIn('Documented exclusions remain untested', report)

    def test_missing_failed_cancelled_or_skipped_job_never_grants_passed(self):
        for result in (None, 'failure', 'cancelled', 'skipped'):
            jobs = dict.fromkeys(certify.REQUIRED_JOBS, 'success')
            if result is None:
                jobs.pop('compatibility')
            else:
                jobs['compatibility'] = result
            with self.subTest(result=result):
                self.assertEqual(self.record(jobs=jobs)['status'], 'failed')
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertNotIn('--latest=true', self.edits[-1][0])

    def test_experiment_stays_exact_title_prerelease_and_never_latest(self):
        self.metadata.update(experiment=True, title='experiment')
        self.published.update(prerelease=True, name='experiment')
        self.refresh_metadata()
        self.assertEqual(self.record()['status'], 'passed')
        args = self.edits[0][0]
        self.assertIn('--latest=false', args)
        self.assertIn('--prerelease', args)
        self.assertEqual(args[args.index('--title') + 1], 'experiment')

    def test_changed_experiment_designation_is_rejected(self):
        self.metadata.update(experiment=True, title='experiment')
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'Experimental release'):
            self.record()
        self.assertFalse(self.uploads)

    def test_changed_source_or_inventory_prevents_report_upload(self):
        for extra in ({'source_sha': 'b' * 40}, {'inventory_sha256': 'b' * 64}):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.record(**extra)
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_existing_certificate_or_upload_failure_cannot_promote(self):
        self.assets.append({'name': 'certification-789-attempt-2.json'})
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.record()
        self.assertFalse(self.edits)
        self.assets.pop()
        original = self.gh
        def fail_upload(args, **options):
            if args[:2] == ['release', 'upload']:
                raise subprocess.CalledProcessError(1, args)
            return original(args, **options)
        with patch.object(certify, 'gh', side_effect=fail_upload), self.assertRaises(subprocess.CalledProcessError):
            self.record()
        self.assertFalse(self.edits)

    def test_without_server_digests_record_rechecks_all_published_application_bytes(self):
        for asset in self.assets:
            asset.pop('digest')
        name = certify.release.application_names(self.metadata)['windows-x86_64']
        self.files[name] += b'changed'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.record()
        self.assertFalse(self.uploads)


class DependencyCertificationTests(CertificationFixture, unittest.TestCase):
    schema = 6

    def setUp(self):
        super().setUp()
        for name in certify.release.required_assets(self.metadata) - self.files.keys():
            self.files[name] = ('preserved fixture ' + name).encode()
        self.refresh_metadata()

    def test_certification_pins_complete_dependency_inventory(self):
        state = self.prepare()
        self.assertEqual(state['metadata']['dependencies'], self.metadata['dependencies'])
        self.assertTrue(certify.release.dependency_assets(self.metadata) <= state['inventory'].keys())
        # Preparation pins the global inventory without pulling large SDKs in
        # every compatibility job. The source jobs verify their retained copies.
        self.assertEqual(set(self.downloads), {'SHA256SUMS.txt', 'release-metadata.json'})

    def test_missing_preserved_source_blocks_certification_even_with_new_global_sums(self):
        name = next(name for name in certify.release.dependency_assets(self.metadata) if '-sources-' in name)
        self.files.pop(name)
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application or support'):
            self.prepare()

    def test_wrong_recipe_cannot_substitute_for_recorded_dependencies(self):
        name = next(name for name in certify.release.dependency_assets(self.metadata) if '-sources-' in name)
        self.files[name.replace(self.metadata['dependencies']['windows-base'], '0' * 20)
                   .replace(self.metadata['dependencies']['linux-sdk'], '0' * 20)] = self.files.pop(name)
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application or support'):
            self.prepare()

    def test_certificate_records_recipes_and_rechecks_dependencies_without_server_digests(self):
        names = certify.release.dependency_assets(self.metadata)
        for asset in self.assets:
            if asset['name'] in names:
                asset.pop('digest')
        evidence = self.record()
        self.assertEqual(evidence['status'], 'passed')
        self.assertEqual(evidence['dependencies'], self.metadata['dependencies'])
        self.assertEqual(evidence['dependency_assets'], {name: digest(self.files[name]) for name in names})
        self.assertTrue(names <= set(self.downloads))

    def test_changed_dependency_with_missing_server_digest_cannot_be_certified(self):
        name = next(iter(certify.release.dependency_assets(self.metadata)))
        self.files[name] += b'changed after source tests'
        for asset in self.assets:
            if asset['name'] == name:
                asset.pop('digest')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.record()
        self.assertEqual(self.uploads, {})
        self.assertEqual(self.edits, [])


class BackendCertificationTests(CertificationFixture, unittest.TestCase):
    schema = 2

    def test_hosted_warning_does_not_bypass_legacy_release_schema_gate(self):
        for options in ({'warnings': [certify.windows_certification.warning_record()]},
                        {'smoke_warnings': [self.smoke_warning()]}):
            with self.subTest(options=options):
                evidence = self.record(**options)
                self.assertEqual(evidence['status'], 'passed_with_warnings')
                self.assertFalse(evidence['latest_eligible'])
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertNotIn('--latest=true', self.edits[-1][0])
                for report in (self.edits[-1][1], self.uploads['certification-789-attempt-2.md'].decode()):
                    self.assertIn('This older release lacks the metadata and delivery channels required for Latest', report)
                    self.assertNotIn('This ordinary release remains eligible for Latest', report)

    def test_prepare_emits_all_six_backend_aware_targets(self):
        state = self.prepare()
        values = certify.output_values(state, None)
        expected = {platform + '-' + backend for platform in certify.release.TARGETS
                    for backend in ('fltk', 'rev')}
        self.assertEqual(values['schema'], '2')
        self.assertEqual(json.loads(values['gui_backends']), ['fltk', 'rev'])
        self.assertEqual(set(json.loads(values['application_targets'])), expected)
        self.assertEqual(len(certify.release.application_names(self.metadata)), 6)
        self.assertEqual(self.downloads, ['SHA256SUMS.txt', 'release-metadata.json'])

    def test_prepare_requires_every_rev_asset_even_when_fltk_is_complete(self):
        del self.files[certify.release.application_names(self.metadata)['linux-x86_64-rev']]
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application'):
            self.prepare()

    def test_prepare_requires_published_warning_log(self):
        del self.files['warning.log']
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application or support'):
            self.prepare()

    def test_download_verifies_and_extracts_each_platform_backend(self):
        for target in certify.release.application_targets(self.metadata):
            with self.subTest(target=target):
                state = certify.download(self.repository, self.tag, target, self.root / target,
                                         digest(self.files['SHA256SUMS.txt']))
                backend = certify.release.target_backend(self.metadata, target)
                self.assertTrue(state['package_root'].name.endswith('-native-' + backend))
                self.assertEqual(state['package_root'].parent.name, 'offline destination with spaces')
                self.assertIn('GUI: ON (' + backend + ')',
                              (state['package_root'] / 'share/doc/datapump/build-info.txt').read_text())
                self.assertEqual(state['archive'].name, certify.release.application_names(self.metadata)[target])
        self.assertEqual(len([name for name in self.downloads if name.startswith('DataPump-')]), 6)

    def test_wrong_backend_build_info_or_package_root_cannot_satisfy_rev(self):
        target = 'linux-x86_64-rev'
        root = sorted(certify.release.package_bases(self.metadata, target))[0]
        wrong_info = {f'{root}/share/doc/datapump/build-info.txt': b'GUI: ON (fltk)\n'}
        for label, data in (('info', self.archive(target, wrong_info)),
                            ('root', self.archive('linux-x86_64-fltk'))):
            with self.subTest(case=label):
                name = certify.release.application_names(self.metadata)[target]
                self.files[name] = data
                self.refresh_metadata()
                with self.assertRaises(ValueError):
                    certify.download(self.repository, self.tag, target, self.root / label,
                                     digest(self.files['SHA256SUMS.txt']))
                self.assertFalse((self.root / label / 'offline destination with spaces').exists())

    def test_download_rejects_metadata_incompatible_and_unsafe_targets(self):
        for number, target in enumerate(('linux-x86_64', 'linux-x86_64-qt', '../linux-x86_64-rev')):
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, 'Unknown application target'):
                certify.download(self.repository, self.tag, target, self.root / f'unknown-{number}',
                                 digest(self.files['SHA256SUMS.txt']))
        self.assertFalse([name for name in self.downloads if name.startswith('DataPump-')])

    def test_success_requires_all_six_targets_and_records_backend_scope_and_hashes(self):
        for baseline in ('bookworm-sdk', 'ubuntu-22.04'):
            with self.subTest(baseline=baseline):
                self.metadata['linux_baseline'] = baseline
                self.refresh_metadata()
                evidence = self.record()
                names = certify.release.application_names(self.metadata)
                self.assertEqual(evidence['schema'], 2)
                self.assertEqual(evidence['status'], 'passed')
                self.assertEqual(evidence['gui_backends'], ['fltk', 'rev'])
                self.assertEqual(evidence['known_warning_asset'], 'warning.log')
                self.assertEqual(evidence['warning_sha256'], digest(self.files['warning.log']))
                self.assertEqual(evidence['warning_policy'], certify.DISPLAY_WARNING_POLICY)
                self.assertEqual(set(evidence['tested_targets']), set(names))
                self.assertEqual(set(evidence['application_targets']), set(names))
                self.assertEqual(set(evidence['assets']), set(names.values()))
                report = self.uploads['certification-789-attempt-2.md'].decode()
                self.assertIn(certify.DISPLAY_WARNING_POLICY, report)
                self.assertIn('/warning.log)', report)
                for target, name in names.items():
                    identity = evidence['application_targets'][target]
                    self.assertEqual(identity['asset'], name)
                    self.assertEqual(identity['sha256'], digest(self.files[name]))
                    self.assertEqual(identity['gui_backend'], target.rsplit('-', 1)[1])
                    for section in ('source_tests', 'published_archives'):
                        item = evidence['required_coverage'][section][target]
                        self.assertEqual(item['gui_backend'], identity['gui_backend'])
                        self.assertEqual(item['platform'], identity['platform'])
                        if identity['platform'] == 'windows-x86_64':
                            if section == 'source_tests':
                                self.assertEqual(item['environment'], 'Selected Windows x64 hosted runner')
                            else:
                                self.assertEqual(item['environments'], ['Selected Windows x64 hosted runner'])
                    self.assertIn('Source `' + target + '`', report)
                    self.assertIn('Published `' + target + '`', report)
                for backend in ('fltk', 'rev'):
                    x86 = evidence['required_coverage']['published_archives']['linux-x86_64-' + backend]
                    self.assertEqual('Ubuntu 22.04' in x86['environments'], baseline == 'ubuntu-22.04')
                    self.assertIn('Arch Linux', x86['environments'])
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertFalse(evidence['latest_eligible'])

    def test_missing_or_fltk_only_coverage_attaches_failed_report_without_promotion(self):
        cases = [None, [], [target for target in certify.release.application_targets(self.metadata)
                           if target.endswith('-fltk')]]
        for index, targets in enumerate(cases):
            results = self.results(tested_targets=targets)
            if targets is None:
                value = json.loads(results.read_text())
                value.pop('tested_targets')
                results.write_text(json.dumps(value))
            with self.subTest(targets=targets):
                evidence = certify.record(self.repository, self.tag, '789', results, str(index + 1))
                self.assertEqual(evidence['status'], 'failed')
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertNotIn('--latest=true', self.edits[-1][0])
                report = self.uploads[f'certification-789-attempt-{index + 1}.md'].decode()
                self.assertIn('Missing targets:', report)
                self.assertIn('`windows-x86_64-rev`', report)

    def test_duplicate_wrong_or_non_list_coverage_is_rejected_without_evidence(self):
        targets = list(certify.release.application_targets(self.metadata))
        for value in (targets + [targets[0]], targets + ['linux-x86_64'],
                      ['linux-x86_64-qt'], 'all', [None], [[]], None):
            with self.subTest(targets=value), self.assertRaisesRegex(ValueError, 'Tested targets'):
                self.record(tested_targets=value)
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_all_targets_do_not_override_failed_jobs_or_experiment_policy(self):
        jobs = dict.fromkeys(certify.REQUIRED_JOBS, 'success')
        jobs['compatibility'] = 'failure'
        self.assertEqual(self.record(jobs=jobs)['status'], 'failed')
        self.assertIn('--latest=false', self.edits[-1][0])
        self.metadata.update(experiment=True, title='experiment')
        self.published.update(prerelease=True, name='experiment')
        self.refresh_metadata()
        self.assertEqual(self.record()['status'], 'passed')
        flags = self.edits[-1][0]
        self.assertIn('--latest=false', flags)
        self.assertIn('--prerelease', flags)
        self.assertEqual(flags[flags.index('--title') + 1], 'experiment')

    def test_warning_bytes_are_rechecked_without_server_digest(self):
        for asset in self.assets:
            if asset['name'] == 'warning.log':
                asset.pop('digest')
        self.files['warning.log'] += b'changed'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.record()
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_schema2_certificate_is_immutable_and_never_uploads_binaries(self):
        self.record()
        self.assertEqual(set(self.uploads), {'certification-789-attempt-2.json', 'certification-789-attempt-2.md'})
        self.assets.append({'name': 'certification-789-attempt-2.json'})
        edit_count = len(self.edits)
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.record()
        self.assertEqual(len(self.edits), edit_count)


class AptCertificationTests(CertificationFixture, unittest.TestCase):
    schema = 3

    def setUp(self):
        super().setUp()
        self.apt_names = {'Packages', 'Packages.gz', 'Release', 'InRelease', 'Release.gpg',
                          'datapump-archive-keyring.gpg', 'datapump.sources', 'apt-repository.json'} | {
                              f'datapump-{backend}_0.7.2+20260922_{arch}.deb'
                              for backend in ('fltk', 'rev') for arch in ('amd64', 'arm64')}
        self.apt_tool = Mock()
        self.apt_tool.asset_names.return_value = self.apt_names
        patched = patch.object(certify.release, 'apt_tool', return_value=self.apt_tool)
        patched.start()
        self.addCleanup(patched.stop)
        self.files.update({name: ('apt fixture ' + name).encode() for name in self.apt_names})
        self.refresh_metadata()

    def test_prepare_requires_all_signed_apt_files_and_emits_feature_marker(self):
        state = self.prepare()
        self.assertEqual(certify.output_values(state, None)['apt_repository'], 'true')
        self.assertEqual(self.downloads, ['SHA256SUMS.txt', 'release-metadata.json'])
        for index, name in enumerate(sorted(self.apt_names)):
            data = self.files.pop(name)
            self.refresh_metadata()
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'missing application or support'):
                certify.prepare(self.repository, self.tag, self.root / f'missing-{index}')
            self.files[name] = data
        self.apt_tool.verify.assert_not_called()

    def test_repackaged_tag_pins_packager_while_certification_pins_application_source(self):
        self.metadata.update(packager_sha='b' * 40, experiment=True, title='experiment',
                             repackaged_from={'tag': 'original', 'inventory_sha256': 'c' * 64})
        self.published.update(prerelease=True, name='experiment')
        self.commit = self.metadata['packager_sha']
        self.refresh_metadata()
        state = self.prepare()
        self.assertEqual(certify.output_values(state, None)['source_sha'], 'a' * 40)
        evidence = self.record()
        self.assertEqual(evidence['source_sha'], 'a' * 40)
        self.assertEqual(evidence['packager_sha'], 'b' * 40)
        self.assertEqual(evidence['tag_sha'], 'b' * 40)
        self.assertEqual(evidence['repackaged_from'], self.metadata['repackaged_from'])
        self.assertEqual(evidence['status'], 'passed')
        self.assertIn('--latest=false', self.edits[-1][0])
        self.commit = self.metadata['source_sha']
        with self.assertRaisesRegex(ValueError, 'source commit or tag'):
            certify.prepare(self.repository, self.tag, self.root / 'wrong-tag')

    def test_download_apt_verifies_pinned_assets_and_original_linux_payloads(self):
        state = certify.download_apt(self.repository, self.tag, self.root / 'apt',
                                     digest(self.files['SHA256SUMS.txt']), 'A' * 40)
        expected = self.apt_names | getattr(self, 'distro_names', set()) | getattr(self, 'channel_names', set()) | {'SHA256SUMS.txt', 'release-metadata.json'} | {
            name for target, name in certify.release.application_names(self.metadata).items()
            if target.startswith('linux-')}
        self.assertEqual(set(self.downloads), expected)
        self.assertFalse(any('windows' in name for name in self.downloads))
        self.apt_tool.verify.assert_called_once_with(state['directory'], self.metadata,
                                                     repository=self.repository, trusted_fingerprint='A' * 40)

    def test_corrupt_apt_asset_cannot_reach_signature_or_payload_verification(self):
        for asset in self.assets:
            asset.pop('digest')
        self.files['InRelease'] += b'tampered'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            certify.download_apt(self.repository, self.tag, self.root / 'apt',
                                 digest(self.files['SHA256SUMS.txt']))
        self.apt_tool.verify.assert_not_called()

    def test_apt_verification_failure_propagates_to_the_job(self):
        self.apt_tool.verify.side_effect = ValueError('APT signature mismatch')
        with self.assertRaisesRegex(ValueError, 'signature mismatch'):
            certify.download_apt(self.repository, self.tag, self.root / 'apt',
                                 digest(self.files['SHA256SUMS.txt']))

    def test_passed_record_binds_all_apt_hashes_and_demands_apt_job(self):
        evidence = self.record()
        self.assertEqual(evidence['schema'], self.schema)
        self.assertEqual(evidence['status'], 'passed')
        self.assertEqual(evidence['apt_assets'], {name: digest(self.files[name]) for name in self.apt_names})
        self.assertIn('apt-repository', evidence['required_jobs'])
        self.assertIn('--latest=true' if self.schema >= 5 else '--latest=false', self.edits[-1][0])
        report = self.uploads['certification-789-attempt-2.md'].decode()
        self.assertIn('Signed APT repository', report)
        self.assertIn('Published APT asset SHA-256 values', report)
        for name in self.apt_names:
            self.assertIn(name, report)

    def test_missing_failed_or_skipped_apt_job_cannot_promote(self):
        for result in (None, 'failure', 'skipped', 'cancelled', 'pending'):
            jobs = dict.fromkeys(certify.REQUIRED_JOBS, 'success')
            if result is not None:
                jobs['apt-repository'] = result
            with self.subTest(result=result):
                evidence = self.record(jobs=jobs)
                self.assertEqual(evidence['status'], 'failed')
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertNotIn('--latest=true', self.edits[-1][0])

    def test_no_server_digest_requires_record_to_recheck_apt_bytes(self):
        for asset in self.assets:
            if asset['name'] in self.apt_names:
                asset.pop('digest')
        self.files['Packages.gz'] += b'tampered'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.record()
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)


class DistroCertificationTests(AptCertificationTests):
    schema = 4

    def setUp(self):
        super().setUp()
        self.distro_names = {'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz', 'distro-packages.json'}
        self.distro_tool = Mock()
        self.distro_tool.asset_names.return_value = self.distro_names
        patched = patch.object(certify.release, 'distro_tool', return_value=self.distro_tool)
        patched.start()
        self.addCleanup(patched.stop)
        self.files.update({name: ('distro fixture ' + name).encode() for name in self.distro_names})
        self.refresh_metadata()

    def test_distro_download_verifies_signed_recipe_inventory_and_original_archives(self):
        state = certify.download_distro(self.repository, self.tag, self.root / 'distro',
                                        digest(self.files['SHA256SUMS.txt']), 'A' * 40)
        self.assertTrue(self.distro_names <= set(self.downloads))
        self.assertEqual(certify.output_values(state, None)['distro_recipes'], 'true')
        self.apt_tool.verify.assert_called_once()
        self.distro_tool.verify.assert_called_once_with(state['directory'], self.metadata, repository=self.repository)
        self.distro_tool.verify.side_effect = ValueError('Recipe contents mismatch')
        with self.assertRaisesRegex(ValueError, 'Recipe contents'):
            certify.download_distro(self.repository, self.tag, self.root / 'invalid-distro',
                                     digest(self.files['SHA256SUMS.txt']), 'A' * 40)

    def test_distribution_job_is_required_and_evidence_binds_all_recipe_hashes(self):
        evidence = self.record()
        self.assertIn('distro-recipes', evidence['required_jobs'])
        self.assertEqual(evidence['distribution_assets'], {name: digest(self.files[name]) for name in self.distro_names | getattr(self, 'channel_names', set())})
        self.assertIn('--latest=true' if self.schema >= 5 else '--latest=false', self.edits[-1][0])
        for result in (None, 'failure', 'skipped'):
            jobs = dict.fromkeys(certify.required_jobs(self.metadata) - {'distro-recipes'}, 'success')
            if result is not None:
                jobs['distro-recipes'] = result
            with self.subTest(result=result):
                evidence = self.record(jobs=jobs)
                self.assertEqual(evidence['status'], 'failed')
                self.assertIn('--latest=false', self.edits[-1][0])

    def test_missing_or_corrupt_recipe_cannot_reach_installation(self):
        name = sorted(self.distro_names)[0]
        self.files.pop(name)
        self.refresh_metadata()
        with self.assertRaisesRegex(ValueError, 'missing application or support'):
            self.prepare()
        self.distro_tool.verify.assert_not_called()


class ChannelCertificationTests(DistroCertificationTests):
    schema = 5

    def setUp(self):
        # Parent setup refreshes schema-5 inventory, so install channel stubs first.
        self.channels = {}
        for kind in ('arch', 'gentoo'):
            tool = Mock()
            tool.asset_names.return_value = {f'{kind}-channel.fixture'}
            self.channels[kind] = tool
        patched = patch.object(certify.release, 'channel_tool', side_effect=self.channels.__getitem__)
        patched.start()
        self.addCleanup(patched.stop)
        super().setUp()
        self.channel_names = set().union(*(tool.asset_names.return_value for tool in self.channels.values()))
        self.files.update({name: ('channel fixture ' + name).encode() for name in self.channel_names})
        self.refresh_metadata()

    def test_channel_verification_is_required_before_installation(self):
        state = certify.download_distro(self.repository, self.tag, self.root / 'channels',
                                        digest(self.files['SHA256SUMS.txt']), 'A' * 40)
        self.assertTrue(self.channel_names <= set(self.downloads))
        self.assertEqual(certify.output_values(state, None)['distro_channels'], 'true')
        for tool in self.channels.values():
            tool.verify.assert_called_once_with(state['directory'], self.metadata,
                                                 repository=self.repository, expected_fingerprint='A' * 40)
        self.channels['gentoo'].verify.side_effect = ValueError('channel signature mismatch')
        with self.assertRaisesRegex(ValueError, 'channel signature'):
            certify.download_distro(self.repository, self.tag, self.root / 'bad-channel',
                                     digest(self.files['SHA256SUMS.txt']), 'A' * 40)

    def test_signed_update_channels_are_required_to_promote_latest(self):
        evidence = self.record()
        self.assertEqual(evidence['status'], 'passed')
        self.assertIn('--latest=true', self.edits[-1][0])
        self.assertTrue(self.channel_names <= set(evidence['distribution_assets']))

    def test_exact_windows_graphics_warning_keeps_ordinary_release_latest_eligible(self):
        history = 'Certification run 100: **failed** ([report](https://example.test/old.md)).'
        self.published['body'] = certify.release.CERTIFICATION_PENDING + '\nOriginal notes\n' + history
        warnings = [certify.windows_certification.warning_record()]
        evidence = self.record(warnings=warnings)
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertTrue(evidence['latest_eligible'])
        self.assertEqual(evidence['warnings'], warnings)
        self.assertEqual(evidence['coverage_exclusions']['windows-x86_64-rev'],
                         warnings[0]['omitted_checks'])
        self.assertNotIn('source:gui_platform_conformance', warnings[0]['omitted_checks'])
        self.assertNotIn('latest_blockers', evidence)
        self.assertEqual(evidence['hosted_warning_policy'], certify.HOSTED_WARNING_POLICY)
        self.assertIn('--latest=true', self.edits[-1][0])
        self.assertIn('--prerelease=false', self.edits[-1][0])
        self.assertNotIn('--latest=false', self.edits[-1][0])
        self.assertEqual(json.loads(self.uploads['certification-789-attempt-2.json']), evidence)
        log_name = 'certification-789-attempt-2-warning.log'
        self.assertEqual(evidence['warning_report_asset'], log_name)
        self.assertEqual(evidence['warning_report_sha256'], digest(self.uploads[log_name]))
        self.assertIn(b'cannot open the Rev GUI', self.uploads[log_name])
        self.assertIn('native graphics remain unqualified', self.edits[-1][1])
        self.assertIn(history, self.edits[-1][1])
        self.assertIn('Original notes', self.edits[-1][1])
        for report in (self.edits[-1][1], self.uploads['certification-789-attempt-2.md'].decode()):
            self.assertIn('Hosted certification passed with documented exclusions', report)
            self.assertIn('This ordinary release remains eligible for Latest', report)
            self.assertIn('listed graphics checks remain untested', report)
        upload = next(i for i, args in enumerate(self.calls) if args[:2] == ['release', 'upload'])
        edit = next(i for i, args in enumerate(self.calls) if args[:2] == ['release', 'edit'])
        self.assertLess(upload, edit)

    def test_graphics_warning_does_not_promote_an_experiment(self):
        self.metadata.update(experiment=True, title='experiment')
        self.published.update(prerelease=True, name='experiment')
        self.refresh_metadata()
        evidence = self.record(warnings=[certify.windows_certification.warning_record()])
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertFalse(evidence['latest_eligible'])
        flags, body = self.edits[-1]
        self.assertIn('--latest=false', flags)
        self.assertNotIn('--latest=true', flags)
        self.assertIn('--prerelease', flags)
        self.assertEqual(flags[flags.index('--title') + 1], 'experiment')
        for report in (body, self.uploads['certification-789-attempt-2.md'].decode()):
            self.assertIn('Hosted certification passed with documented exclusions', report)
            self.assertIn('This experiment remains a prerelease and is not eligible for Latest', report)
            self.assertNotIn('This ordinary release remains eligible for Latest', report)

    def test_incomplete_smoke_binds_evidence_and_keeps_ordinary_release_eligible(self):
        history = 'Earlier certification remains recorded.'
        self.published['body'] = certify.release.CERTIFICATION_PENDING + '\n' + history
        warnings = [self.smoke_warning(), self.smoke_warning(scope='published/ubuntu:24.04')]
        evidence = self.record(smoke_warnings=warnings)
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertTrue(evidence['latest_eligible'])
        self.assertIn('--latest=true', self.edits[-1][0])
        self.assertNotIn('warnings', evidence)
        self.assertEqual(evidence['smoke_warnings'], warnings)
        self.assertEqual(evidence['incomplete_smoke_coverage'], {
            warnings[0]['target']: ['published/ubuntu:24.04', 'source/native']})
        self.assertEqual(evidence['smoke_warning_policy'], certify.SMOKE_WARNING_POLICY)
        stem = 'certification-789-attempt-2'
        self.assertEqual(set(self.uploads), {
            stem + suffix for suffix in ('.json', '.md', '-smoke-warnings.json', '-smoke-warnings.log')})
        self.assertEqual(json.loads(self.uploads[stem + '.json']), evidence)
        self.assertEqual(json.loads(self.uploads[stem + '-smoke-warnings.json']), warnings)
        for kind in ('json', 'log'):
            name = stem + '-smoke-warnings.' + kind
            self.assertEqual(evidence[f'smoke_warning_{kind}_asset'], name)
            self.assertEqual(evidence[f'smoke_warning_{kind}_sha256'], digest(self.uploads[name]))
        log = self.uploads[stem + '-smoke-warnings.log'].decode()
        self.assertIn(SMOKE_DIAGNOSTIC, log)
        self.assertIn('Incomplete GUI smoke coverage, not a smoke pass', log)
        self.assertIn(warnings[0]['inventory_sha256'], log)
        self.assertIn('run 789, attempt 2', log)
        for report in (self.edits[-1][1], self.uploads[stem + '.md'].decode()):
            self.assertIn('Hosted certification passed with documented incomplete smoke coverage', report)
            self.assertIn('not a smoke pass', report)
            self.assertIn('This ordinary release remains eligible for Latest', report)
            self.assertIn('published/ubuntu:24.04', report)
            self.assertIn(stem + '-smoke-warnings.json', report)
            self.assertIn(stem + '-smoke-warnings.log', report)
        self.assertIn(history, self.edits[-1][1])

    def test_graphics_and_smoke_warnings_remain_separate(self):
        graphics = [certify.windows_certification.warning_record()]
        smoke = [self.smoke_warning()]
        evidence = self.record(warnings=graphics, smoke_warnings=smoke)
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertTrue(evidence['latest_eligible'])
        self.assertEqual(evidence['warnings'], graphics)
        self.assertEqual(evidence['smoke_warnings'], smoke)
        self.assertIn('windows-x86_64-rev', evidence['coverage_exclusions'])
        self.assertIn(smoke[0]['target'], evidence['incomplete_smoke_coverage'])
        self.assertEqual(len(self.uploads), 5)

    def test_incomplete_smoke_never_promotes_an_experiment(self):
        self.metadata.update(experiment=True, title='experiment')
        self.published.update(prerelease=True, name='experiment')
        self.refresh_metadata()
        evidence = self.record(smoke_warnings=[self.smoke_warning()])
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertFalse(evidence['latest_eligible'])
        flags, body = self.edits[-1]
        self.assertIn('--latest=false', flags)
        self.assertNotIn('--latest=true', flags)
        self.assertIn('--prerelease', flags)
        self.assertEqual(flags[flags.index('--title') + 1], 'experiment')
        self.assertIn('This experiment remains a prerelease and is not eligible for Latest', body)

    def test_incomplete_smoke_never_hides_failed_or_missing_required_jobs(self):
        for status in ('failure', 'cancelled', 'skipped', 'timed_out', 'pending', None):
            with self.subTest(status=status):
                jobs = dict.fromkeys(certify.required_jobs(self.metadata), 'success')
                if status is None:
                    jobs.pop('linux-tests')
                else:
                    jobs['linux-tests'] = status
                evidence = self.record(jobs=jobs, smoke_warnings=[self.smoke_warning()])
                self.assertEqual(evidence['status'], 'failed')
                self.assertFalse(evidence['latest_eligible'])
                self.assertIn('--latest=false', self.edits[-1][0])
                self.assertNotIn('--latest=true', self.edits[-1][0])
                self.assertIn('Hosted certification failed', self.edits[-1][1])
                self.assertNotIn('Hosted certification passed with documented incomplete smoke coverage',
                                 self.edits[-1][1])

    def test_smoke_warning_schema_release_run_and_scope_are_exact(self):
        known = self.smoke_warning()
        missing = dict(known)
        missing.pop('scope')
        cases = [None, {}, 'warning', [None], [missing], [dict(known, extra='not allowed')]]
        changes = [
            {'code': 'generic-timeout'}, {'target': 'unknown-target'},
            {'source_sha': 'b' * 40}, {'inventory_sha256': 'b' * 64},
            {'run_id': '788'}, {'run_attempt': '3'}, {'run_id': 789},
            {'scope': 'source/adapter'}, {'scope': 'published/'},
            {'scope': 'published/' + 'a' * 129}, {'scope': 'published/ubuntu:24.04\ninjected'},
            {'scope': 'published/`injected`'}, {'scope': None}, {'diagnostic': 75}]
        cases += [[dict(known, **change)] for change in changes]
        for warnings in cases:
            with self.subTest(warnings=warnings), self.assertRaisesRegex(ValueError, '[Ss]moke warning'):
                self.record(smoke_warnings=warnings)
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_smoke_warning_from_earlier_attempt_retains_its_origin(self):
        warning = self.smoke_warning(run_attempt='1')
        evidence = self.record(smoke_warnings=[warning])
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertTrue(evidence['latest_eligible'])
        self.assertEqual(evidence['run_attempt'], '2')
        self.assertEqual(evidence['smoke_warnings'], [warning])
        stem = 'certification-789-attempt-2'
        self.assertEqual(json.loads(self.uploads[stem + '-smoke-warnings.json']), [warning])
        log = self.uploads[stem + '-smoke-warnings.log'].decode()
        self.assertIn('run 789, attempt 1.', log)
        self.assertNotIn('run 789, attempt 2.', log)
        self.assertIn('/attempts/2', self.edits[-1][1])

    def test_smoke_warning_rejects_future_zero_and_malformed_attempts(self):
        for attempt in ('3', '0', '01', '-1', '1.0', '', ' 1', '1\n', 1, None):
            with self.subTest(attempt=attempt), self.assertRaisesRegex(ValueError, '[Ss]moke warning'):
                self.record(smoke_warnings=[self.smoke_warning(run_attempt=attempt)])
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_smoke_warning_requires_typed_recent_progress_diagnostic(self):
        invalid = ['Timeout 600 seconds', ' ' + SMOKE_DIAGNOSTIC,
                   SMOKE_DIAGNOSTIC + '\nAddressSanitizer: failure',
                   SMOKE_DIAGNOSTIC.replace('phase=17', 'phase=24'),
                   SMOKE_DIAGNOSTIC.replace('elapsed=600.018200', 'elapsed=599.999999'),
                   SMOKE_DIAGNOSTIC.replace('budget=600.000000', 'budget=9.000000'),
                   SMOKE_DIAGNOSTIC.replace('budget=600.000000', 'budget=1201.000000')
                       .replace('elapsed=600.018200', 'elapsed=1201.000000'),
                   SMOKE_DIAGNOSTIC.replace('tx_id=11', 'tx_id=0'),
                   SMOKE_DIAGNOSTIC.replace('fraction=0.225619', 'fraction=1.000001'),
                   SMOKE_DIAGNOSTIC.replace('progress_age=2.343160', 'progress_age=30.000001'),
                   SMOKE_DIAGNOSTIC.replace('result=incomplete', 'result=passed')]
        for diagnostic in invalid:
            with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(ValueError, 'diagnostic'):
                self.record(smoke_warnings=[self.smoke_warning(diagnostic=diagnostic)])
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_smoke_warning_uses_shared_budget_boundaries_and_retains_distinct_attempts(self):
        warnings = []
        for budget in (10, 1200):
            diagnostic = (SMOKE_DIAGNOSTIC.replace('budget=600.000000', f'budget={budget:.6f}')
                          .replace('elapsed=600.018200', f'elapsed={budget:.6f}')
                          .replace('progress_age=2.343160', 'progress_age=30.000000'))
            warnings.append(self.smoke_warning(diagnostic=diagnostic))
        evidence = self.record(smoke_warnings=warnings)
        self.assertEqual(evidence['status'], 'passed_with_warnings')
        self.assertEqual(evidence['smoke_warnings'], warnings)
        self.assertEqual(evidence['incomplete_smoke_coverage'], {
            warnings[0]['target']: ['source/native']})
        self.assertEqual(json.loads(self.uploads['certification-789-attempt-2-smoke-warnings.json']), warnings)

    def test_smoke_warning_count_and_duplicate_bounds(self):
        known = self.smoke_warning()
        for warnings in ([known, known], [self.smoke_warning(scope=f'published/fixture:{i}') for i in range(65)]):
            with self.subTest(count=len(warnings)), self.assertRaisesRegex(ValueError, '[Ss]moke warning'):
                self.record(smoke_warnings=warnings)
        self.assertFalse(self.uploads)
        warnings = [self.smoke_warning(scope=f'published/fixture:{i}') for i in range(64)]
        evidence = self.record(smoke_warnings=warnings)
        self.assertEqual(len(evidence['smoke_warnings']), 64)
        self.assertEqual(len(evidence['incomplete_smoke_coverage'][known['target']]), 64)

    def test_smoke_warning_assets_cannot_be_overwritten_or_promoted_before_upload(self):
        for suffix in ('-smoke-warnings.json', '-smoke-warnings.log'):
            self.assets.append({'name': 'certification-789-attempt-2' + suffix})
            with self.subTest(suffix=suffix), self.assertRaisesRegex(ValueError, 'already exists'):
                self.record(smoke_warnings=[self.smoke_warning()])
            self.assets.pop()
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

        def fail_upload(args, **options):
            if args[:2] == ['release', 'upload']:
                raise subprocess.CalledProcessError(1, args)
            return self.gh(args, **options)
        with patch.object(certify, 'gh', side_effect=fail_upload), self.assertRaises(subprocess.CalledProcessError):
            self.record(smoke_warnings=[self.smoke_warning()])
        self.assertFalse(self.edits)

    def test_warning_never_hides_other_failed_jobs(self):
        jobs = dict.fromkeys(certify.required_jobs(self.metadata), 'success')
        jobs['linux-tests'] = 'failure'
        evidence = self.record(jobs=jobs, warnings=[certify.windows_certification.warning_record()])
        self.assertEqual(evidence['status'], 'failed')
        self.assertFalse(evidence['latest_eligible'])
        self.assertIn('--latest=false', self.edits[-1][0])
        self.assertNotIn('--latest=true', self.edits[-1][0])
        for report in (self.edits[-1][1], self.uploads['certification-789-attempt-2.md'].decode()):
            self.assertIn('Hosted certification failed', report)
            self.assertIn('Failed or incomplete required checks keep this release ineligible for Latest', report)
            self.assertNotIn('Hosted certification passed with documented exclusions', report)

    def test_arbitrary_or_expanded_exclusions_are_rejected(self):
        known = certify.windows_certification.warning_record()
        changed = copy.deepcopy(known)
        changed['omitted_checks'].append('source:cli')
        wrong_target = dict(known, target='linux-x86_64-rev')
        wrong_probe = dict(known, probe={'test': 'gui_coordinates_1x', 'exit_code': 9,
                                        'output': known['probe']['output']})
        unknown = dict(known, code='unknown-hosted-limitation')
        for warnings in ({}, [known, known], [changed], [wrong_target], [wrong_probe], [unknown]):
            with self.subTest(warnings=warnings), self.assertRaisesRegex(ValueError, 'warning'):
                self.record(warnings=warnings)
        self.assertFalse(self.uploads)
        self.assertFalse(self.edits)

    def test_warning_record_cli_returns_success_but_other_failures_do_not(self):
        args = ['record', '--repo', self.repository, '--tag', self.tag, '--run-id', '789',
                '--results-json', str(self.results())]
        with patch.object(certify, 'record', return_value={'status': 'passed_with_warnings'}):
            self.assertEqual(certify.main(args), 0)
        with patch.object(certify, 'record', return_value={'status': 'failed'}):
            self.assertEqual(certify.main(args), 1)


if __name__ == '__main__':
    unittest.main()
