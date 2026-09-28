#!/usr/bin/env python3
"""Verify that this run's certified ordinary release is GitHub's current Latest."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile

SPEC = importlib.util.spec_from_file_location(
    'datapump_certify_release', Path(__file__).with_name('certify-release.py'))
certify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(certify)


def verify(repository, tag, source_sha, run_id, run_attempt, inventory_sha256=None,
           certification_run_id=None):
    certify.validate_location(repository, tag)
    if not certify.release.SHA.fullmatch(source_sha):
        raise ValueError('Expected source must be a full commit SHA')
    if certification_run_id is None:
        certification_run_id = run_id
    if any(not re.fullmatch(r'[1-9][0-9]*', value)
           for value in (run_id, run_attempt, certification_run_id)):
        raise ValueError('Expected run ID and certification attempt must be positive integers')
    if inventory_sha256 is not None and not re.fullmatch(r'[0-9a-f]{64}', inventory_sha256):
        raise ValueError('Expected inventory must be a SHA-256 digest')
    with tempfile.TemporaryDirectory(prefix='datapump-verify-latest-') as temporary:
        state = certify.prepare(repository, tag, Path(temporary) / 'release', inventory_sha256)
        metadata, published = state['metadata'], state['published']
        if (metadata['source_sha'] != source_sha or metadata['run_id'] != run_id
                or metadata['experiment'] or metadata['schema'] < 5):
            raise ValueError('Published metadata differs from this ordinary release run')
        if published.get('prerelease') is not False or published.get('draft') is not False:
            raise ValueError('Latest candidate must be a published ordinary release')
        report_name = f'certification-{certification_run_id}-attempt-{run_attempt}.json'
        report_path = certify.download_asset(repository, tag, report_name,
                                              state['directory'], state['assets'])
        report = json.loads(report_path.read_text(encoding='utf-8'))
        identity = {'repository': repository, 'tag': tag, 'source_sha': source_sha,
                    'run_id': certification_run_id, 'run_attempt': run_attempt,
                    'inventory_sha256': state['inventory_sha256'], 'schema': metadata['schema']}
        if not isinstance(report, dict) or any(report.get(key) != value for key, value in identity.items()):
            raise ValueError('Certification report differs from the expected release, source, inventory or run')
        if (report.get('status') not in ('passed', 'passed_with_warnings')
                or report.get('latest_eligible') is not True or report.get('experiment') is not False):
            raise ValueError('Certification report does not qualify this release for Latest')
        required = certify.required_jobs(metadata)
        jobs = report.get('jobs')
        if (report.get('required_jobs') != sorted(required) or not isinstance(jobs, dict)
                or not required <= jobs.keys() or any(result != 'success' for result in jobs.values())):
            raise ValueError('Certification report has incomplete required jobs')
        names = certify.release.application_names(metadata)
        if (report.get('assets') != {name: state['inventory'][name] for name in names.values()}
                or report.get('tested_targets') != sorted(names)):
            raise ValueError('Certification report does not cover the exact published binaries')
        # Read Latest after validating the report, so a concurrent promotion is
        # detected rather than returning success based on an earlier snapshot.
        latest = certify.api(f'repos/{repository}/releases/latest')
        if (not isinstance(latest, dict) or type(latest.get('id')) is not int or latest['id'] != published['id']
                or latest.get('tag_name') != tag or latest.get('draft') is not False
                or latest.get('prerelease') is not False):
            raise ValueError('GitHub Latest does not point to this certified release')
        return {**identity, 'run_id': run_id, 'certification_run_id': certification_run_id,
                'release_id': published['id'], 'status': report['status'],
                'release_url': published['html_url'], 'latest': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repo', 'tag', 'source-sha', 'run-id', 'run-attempt'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--inventory-sha256')
    parser.add_argument('--certification-run-id',
                        help='Separate certification run; defaults to the publication --run-id')
    args = parser.parse_args(argv)
    try:
        result = verify(args.repo, args.tag, args.source_sha, args.run_id,
                        args.run_attempt, args.inventory_sha256, args.certification_run_id)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, KeyError, OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'verify-latest: {certify.release.failure_message(error)}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
