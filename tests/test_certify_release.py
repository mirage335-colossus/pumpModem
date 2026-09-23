#!/usr/bin/env python3
"""Published-byte identity, archive extraction and certification promotion tests."""
import copy
from datetime import datetime, timezone
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
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('certify', ROOT / 'tools/certify-release.py')
certify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(certify)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class CertificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-certify-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.metadata = certify.release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='1',
            now=datetime(2026, 9, 22, 7, 52, tzinfo=timezone.utc), cmake_version='0.7.2')
        self.tag = self.metadata['tag']
        self.repository = 'owner/project'
        self.commit = self.metadata['source_sha']
        self.files = {'release-notes.md': b'Original release notes\n'}
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
        system, architectures, extension = certify.release.TARGETS[target]
        root = f'DataPump-0.7.2-{system}-{architectures[0]}-native'
        executable = '.exe' if system == 'Windows' else ''
        files = {f'{root}/manifest.sha256': b'inventory',
                 f'{root}/bin/pump{executable}': b'CLI', f'{root}/bin/datapump-gui{executable}': b'GUI'}
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
                 'jobs': dict.fromkeys(certify.REQUIRED_JOBS, 'success') if jobs is None else jobs}
        value.update(extra)
        path = self.root / 'results.json'
        path.write_text(json.dumps(value))
        return path

    def record(self, **options):
        return certify.record(self.repository, self.tag, '789', self.results(**options), '2')

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

    def test_passed_report_uploads_only_evidence_then_promotes_normal_release(self):
        evidence = self.record()
        self.assertEqual(evidence['status'], 'passed')
        self.assertEqual(set(evidence['assets']), set(certify.release.application_names(self.metadata).values()))
        self.assertEqual(set(self.uploads), {'certification-789-attempt-2.json', 'certification-789-attempt-2.md'})
        self.assertIn('--latest=true', self.edits[0][0])
        self.assertIn('--prerelease=false', self.edits[0][0])
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
                    self.assertIn('--latest=true', retry_flags)
                    self.assertIn('--prerelease=false', retry_flags)

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
                                 'Windows Server 2022 hosted runner')
                report = self.uploads['certification-789-attempt-2.md'].decode()
                self.assertIn(expected_url + '/attempts/2', report)
                self.assertIn('Source suites rebuild the recorded release commit', report)
                self.assertIn('Archive checks run the published bytes', report)
                self.assertIn('complete only when the report status is **passed**', report)

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


if __name__ == '__main__':
    unittest.main()
