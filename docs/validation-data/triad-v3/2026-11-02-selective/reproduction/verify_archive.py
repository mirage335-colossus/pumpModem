#!/usr/bin/env python3
"""Verify the completed selective study archive and scorer input receipts.

This validates bytes and completion records, not receiver qualification. Arrays
omitted by the collector can only be checked against its remote hash inventory.
"""
import argparse
import hashlib
import json
import tarfile
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = a.root.resolve()
    archive_hash = sha(a.archive)
    if archive_hash != '6bcdc7ee6d210d4b57c5aad7abdbb15fd987e12680d3bd7055053e5aff56b3b4':
        raise ValueError('archive differs from completed remote collection receipt')
    inventory_path = root / 'full-source-file-inventory.json'
    inv = json.loads(inventory_path.read_text())
    entries = {x['path']: x for x in inv['files']}
    assert len(entries) == len(inv['files'])
    # The fixed archive hash must bind the inventories used below, not merely
    # an unrelated file supplied beside the extracted directory.
    inventory_names = {'full-source-file-inventory.json', 'full-array-inventory.json'}
    with tarfile.open(a.archive, 'r:gz') as archive:
        members = archive.getmembers()
        assert len({m.name for m in members}) == len(members)
        assert all(m.isdir() or m.isfile() for m in members)
        regular = {m.name for m in members if m.isfile()}
        assert regular == {n for n, row in entries.items() if row['retained']} | inventory_names
        for name in inventory_names:
            assert archive.extractfile(name).read() == (root / name).read_bytes(), name
    retained = 0
    for name, row in entries.items():
        path = (root / name).resolve()
        assert path.is_relative_to(root)
        if row['retained']:
            assert path.is_file() and path.stat().st_size == row['bytes']
            assert sha(path) == row['sha256'], name
            retained += 1
        else:
            assert not path.exists(), name
    prefix = '/workspace/triad/local-evidence/selective/'
    reports = []
    receipt_count = 0
    for folder in ['scores', 'highspread-scores', 'holdout-scores', 'followup-scores',
                   'long-scores', 'long-refinement-scores', 'alternatives-scores', 'bench']:
        files = sorted((root / folder).glob('*.json'))
        assert files, folder
        for path in files:
            data = json.loads(path.read_text())
            assert not data['failed_physical_path_records']
            assert not data.get('unclassified_failed_physical_path_records', [])
            assert sha(path.parent / data['scores_file']) == data['scores_sha256']
            for row in data['paths']:
                assert row['status'] == 'completed' and row['input'].startswith(prefix)
                relative = row['input'][len(prefix):]
                receipts = row.get('input_hashes', row.get('sampled_input_hashes'))
                assert receipts and 'manifest.json' in receipts
                for name, digest in receipts.items():
                    assert entries[f'{relative}/{name}']['sha256'] == digest
                    receipt_count += 1
            reports.append({'path': str(path.relative_to(root)), 'sha256': sha(path),
                            'paths': len(data['paths'])})
    jobs = []
    for path in sorted(root.rglob('results.json')):
        data = json.loads(path.read_text())
        if 'plan' not in data or 'results' not in data:
            continue
        assert not data['cancelled']
        for row in data['results']:
            assert row['returncode'] == 0 and row['runner']['outcome'] == 'completed'
            assert row['runner']['termination']['group_empty']
        jobs.append({'path': str(path.relative_to(root)), 'jobs': len(data['results']),
                     'sha256': sha(path)})
    assert sum(x['jobs'] for x in jobs) == 412
    assert len(list(root.rglob('manifest.json'))) == 4772
    before = json.loads((root / 'provenance/build.json').read_text())
    after = json.loads((root / 'provenance/build-final.json').read_text())
    assert before == after
    arrays = json.loads((root / 'full-array-inventory.json').read_text())
    for row in arrays['arrays']:
        assert entries[row['path']]['sha256'] == row['sha256']
    result = {
        'schema': 'triad-selective-archive-verification-v1',
        'verifier_sha256': sha(Path(__file__)),
        'archive_sha256': archive_hash, 'archive_bytes': a.archive.stat().st_size,
        'local_inventories_and_regular_member_set_bound_to_archive': True,
        'full_source_inventory_sha256': sha(inventory_path),
        'full_array_inventory_sha256': sha(root / 'full-array-inventory.json'),
        'source_files': len(entries), 'retained_files_rehashed_locally': retained,
        'array_summary': arrays['summary'], 'selected_raw_bytes': arrays['selected_raw_bytes'],
        'selected_workspaces': arrays['selected_workspaces'],
        'score_reports': reports, 'score_input_hash_receipts_matched': receipt_count,
        'job_groups': jobs, 'successful_bounded_jobs': 412, 'workspace_manifests': 4772,
        'source_before_after_identical': True,
        'build_provenance_sha256': sha(root / 'provenance/build-final.json'),
        'source_bundle_sha256': after['source_bundle_sha256'],
        'binary_sha256': after['binary_sha256'],
        'scope': 'All retained bytes rehashed locally; all scorer input receipts matched to the remote full-file inventory. Omitted raw arrays were hashed remotely before deletion but cannot be locally rehashed. Completion receipts do not establish scientific qualification.'}
    with a.output.open('x') as stream:
        json.dump(result, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write('\n')
    print(json.dumps({k: result[k] for k in ['successful_bounded_jobs', 'workspace_manifests',
                     'retained_files_rehashed_locally', 'score_input_hash_receipts_matched']}))


if __name__ == '__main__':
    main()
