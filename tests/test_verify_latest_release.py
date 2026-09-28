#!/usr/bin/env python3
"""Check the final Latest gate against small, immutable published-byte fixtures."""
import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('verify_latest', ROOT / 'tools/verify-latest-release.py')
verify_latest = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify_latest)
certify = verify_latest.certify


def digest(value):
    return hashlib.sha256(value).hexdigest()


class VerifyLatestReleaseTests(unittest.TestCase):
    def setUp(self):
        self.repository, self.run_id, self.attempt = 'owner/project', '123', '2'
        self.source = 'a' * 40
        self.metadata = certify.release.make_metadata(
            source_sha=self.source, run_id=self.run_id, run_attempt='1',
            now=datetime(2026, 9, 26, 12, 30, tzinfo=timezone.utc), cmake_version='1.0.0',
            dependencies={'windows-base': '0123456789abcdef0123', 'linux-sdk': 'fedcba9876543210fedc'})
        self.tag = self.metadata['tag']
        self.published = {'id': 456, 'tag_name': self.tag, 'draft': False, 'prerelease': False,
                          'html_url': f'https://github.com/{self.repository}/releases/tag/{self.tag}'}
        self.latest = copy.deepcopy(self.published)
        self.files = {name: name.encode() for name in certify.release.required_assets(self.metadata)}
        self.files['release-metadata.json'] = json.dumps(self.metadata).encode()
        self.inventory = {name: digest(value) for name, value in self.files.items()}
        self.files['SHA256SUMS.txt'] = ''.join(
            f'{value}  {name}\n' for name, value in sorted(self.inventory.items())).encode()
        self.inventory_sha = digest(self.files['SHA256SUMS.txt'])
        names = certify.release.application_names(self.metadata)
        self.report = {'repository': self.repository, 'tag': self.tag, 'source_sha': self.source,
                       'run_id': self.run_id, 'run_attempt': self.attempt,
                       'inventory_sha256': self.inventory_sha, 'schema': self.metadata['schema'],
                       'status': 'passed', 'latest_eligible': True, 'experiment': False,
                       'required_jobs': sorted(certify.required_jobs(self.metadata)),
                       'jobs': dict.fromkeys(certify.required_jobs(self.metadata), 'success'),
                       'assets': {name: self.inventory[name] for name in names.values()},
                       'tested_targets': sorted(names)}
        self.report_name = f'certification-{self.run_id}-attempt-{self.attempt}.json'
        self.calls, self.downloads = [], []
        for module, name, replacement in (
                (certify, 'api', self.api), (certify.release, 'api_pages', self.pages),
                (certify.release, 'download_asset', self.download_asset)):
            patched = patch.object(module, name, side_effect=replacement)
            patched.start()
            self.addCleanup(patched.stop)
        # No publish, edit, upload, or other unmocked GitHub command is allowed.
        for module in (certify, certify.release):
            patched = patch.object(module, 'gh', side_effect=AssertionError('Unexpected GitHub command'))
            patched.start()
            self.addCleanup(patched.stop)

    def api(self, endpoint):
        self.calls.append(endpoint)
        if endpoint == f'repos/{self.repository}/releases/tags/{self.tag}':
            return copy.deepcopy(self.published)
        if endpoint == f'repos/{self.repository}/git/ref/tags/{self.tag}':
            return {'object': {'type': 'commit', 'sha': self.source}}
        if endpoint == f'repos/{self.repository}/releases/latest':
            return copy.deepcopy(self.latest)
        self.fail('Unexpected API call: ' + endpoint)

    def pages(self, endpoint):
        self.assertEqual(endpoint, f'repos/{self.repository}/releases/456/assets?per_page=100')
        return [{'id': index, 'name': name, 'digest': 'sha256:' + digest(value)}
                for index, (name, value) in enumerate(self.files.items(), 1)]

    def download_asset(self, repository, asset, path, **kwargs):
        self.assertEqual(repository, self.repository)
        self.downloads.append(asset['name'])
        path.write_bytes(self.files[asset['name']])

    def verify(self, **changes):
        self.files[self.report_name] = json.dumps(self.report).encode()
        options = {'repository': self.repository, 'tag': self.tag, 'source_sha': self.source,
                   'run_id': self.run_id, 'run_attempt': self.attempt,
                   'inventory_sha256': self.inventory_sha}
        options.update(changes)
        return verify_latest.verify(**options)

    def test_exact_certified_latest_is_verified_without_mutation(self):
        result = self.verify()
        self.assertTrue(result['latest'])
        self.assertEqual(result['release_id'], 456)
        self.assertEqual(result['run_attempt'], '2')
        self.assertEqual(self.metadata['run_attempt'], '1')  # Successful earlier producer is reusable.
        self.assertEqual(self.calls[-1], f'repos/{self.repository}/releases/latest')
        self.assertEqual(self.downloads, ['SHA256SUMS.txt', 'release-metadata.json', self.report_name])

    def test_documented_warning_status_remains_eligible(self):
        self.report['status'] = 'passed_with_warnings'
        self.assertEqual(self.verify()['status'], 'passed_with_warnings')

    def test_later_certification_binds_both_run_identities(self):
        self.report_name = f'certification-456-attempt-{self.attempt}.json'
        self.report['run_id'] = '456'
        result = self.verify(certification_run_id='456')
        self.assertEqual(result['run_id'], self.run_id)
        self.assertEqual(result['certification_run_id'], '456')
        self.assertEqual(self.downloads[-1], self.report_name)
        with self.assertRaisesRegex(ValueError, 'metadata differs'):
            self.verify(run_id='124', certification_run_id='456')

    def test_later_certification_cannot_substitute_report_identity(self):
        self.report_name = f'certification-456-attempt-{self.attempt}.json'
        for report_run in (self.run_id, '457'):
            with self.subTest(report_run=report_run), self.assertRaisesRegex(ValueError, 'report differs'):
                self.report['run_id'] = report_run
                self.verify(certification_run_id='456')

    def test_missing_later_certification_cannot_borrow_publisher_report(self):
        with self.assertRaisesRegex(ValueError, 'Missing published asset'):
            self.verify(certification_run_id='456')

    def test_wrong_latest_tag_or_release_identity_is_rejected(self):
        for changes in ({'id': 457}, {'id': True}, {'tag_name': 'other-tag'},
                        {'draft': True}, {'prerelease': True}):
            with self.subTest(changes=changes):
                self.latest = {**self.published, **changes}
                with self.assertRaisesRegex(ValueError, 'Latest does not point'):
                    self.verify()

    def test_expected_source_build_run_and_inventory_are_required(self):
        for changes in ({'source_sha': 'b' * 40}, {'run_id': '124'}, {'inventory_sha256': 'f' * 64}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.verify(**changes)

    def test_report_identity_cannot_be_substituted(self):
        for field, value in (('repository', 'other/project'), ('tag', 'other-tag'),
                             ('source_sha', 'b' * 40), ('inventory_sha256', 'f' * 64),
                             ('run_id', '124'), ('run_attempt', '1'), ('schema', 5)):
            original = self.report[field]
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'report differs'):
                self.report[field] = value
                self.verify()
            self.report[field] = original

    def test_unqualified_report_fails_even_if_it_is_already_latest(self):
        for field, value in (('status', 'failed'), ('status', 'pending'),
                             ('latest_eligible', False), ('latest_eligible', 'true'),
                             ('experiment', True)):
            original = self.report[field]
            with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, 'does not qualify'):
                self.report[field] = value
                self.verify()
            self.report[field] = original

    def test_missing_failed_cancelled_or_skipped_jobs_fail(self):
        for result in (None, 'failure', 'cancelled', 'skipped', 'pending'):
            with self.subTest(result=result):
                self.report['jobs'] = dict.fromkeys(certify.required_jobs(self.metadata), 'success')
                if result is None:
                    del self.report['jobs']['linux-tests']
                else:
                    self.report['jobs']['linux-tests'] = result
                with self.assertRaisesRegex(ValueError, 'incomplete required jobs'):
                    self.verify()

    def test_report_must_cover_all_exact_binary_hashes(self):
        name = next(iter(self.report['assets']))
        self.report['assets'][name] = 'f' * 64
        with self.assertRaisesRegex(ValueError, 'exact published binaries'):
            self.verify()
        self.report['assets'][name] = self.inventory[name]
        self.report['tested_targets'].pop()
        with self.assertRaisesRegex(ValueError, 'exact published binaries'):
            self.verify()

    def test_wrong_attempt_cannot_borrow_an_earlier_report(self):
        with self.assertRaisesRegex(ValueError, 'Missing published asset'):
            self.verify(run_attempt='3')

    def test_published_prerelease_and_draft_are_rejected(self):
        for field in ('prerelease', 'draft'):
            self.published[field] = True
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.verify()
            self.published[field] = False

    def test_corrupt_or_missing_report_is_rejected(self):
        self.report = []
        with self.assertRaisesRegex(ValueError, 'report differs'):
            self.verify()

    def test_report_bytes_are_checked_against_the_asset_digest(self):
        def corrupt_download(repository, asset, path, **kwargs):
            self.download_asset(repository, asset, path, **kwargs)
            if asset['name'] == self.report_name:
                path.write_bytes(path.read_bytes() + b' ')
        with patch.object(certify.release, 'download_asset', side_effect=corrupt_download):
            with self.assertRaisesRegex(ValueError, 'asset digest mismatch'):
                self.verify()

    def test_invalid_expected_identity_is_rejected_before_network_access(self):
        for changes in ({'source_sha': 'main'}, {'run_id': '0'}, {'run_attempt': '../1'},
                        {'inventory_sha256': 'invalid'}, {'certification_run_id': ''},
                        {'certification_run_id': '0'}, {'certification_run_id': '../1'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.verify(**changes)
        self.assertFalse(self.calls)


if __name__ == '__main__':
    unittest.main()
