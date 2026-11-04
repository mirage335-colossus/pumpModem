#!/usr/bin/env python3
"""Integrity checks only; never imports or runs the experiment."""
from pathlib import Path
import gzip
import hashlib
import io
import json
import tarfile

HERE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    for line in (HERE / 'SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        assert sha((HERE / name).read_bytes()) == expected, name
    index = json.loads((HERE / 'preservation.json').read_text())
    original = {}
    for record in index['records']:
        packed = (HERE / record['archive']).read_bytes()
        assert packed[:3] == b'\x1f\x8b\x08' and packed[3:8] == bytes(5)
        raw = gzip.decompress(packed)
        if record['storage'] == 'compact_json_reformatted_indent2_newline':
            raw = (json.dumps(json.loads(raw), indent=2, allow_nan=False) + '\n').encode()
        assert len(raw) == record['raw_bytes'] and sha(raw) == record['raw_sha256']
        original[record['input_path']] = raw
    prefix = 'local-evidence/triad-performance-broader/'
    plan_raw = original[prefix + 'savings-native-plan-001.json']
    plan = json.loads(plan_raw)
    semantic = {k: v for k, v in plan.items() if k != 'plan_sha256'}
    assert sha(json.dumps(semantic, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()) == plan['plan_sha256']
    receipt = json.loads(original[prefix + 'savings-native-plan-001.json.freeze-receipt.json'])
    assert receipt['plan_sha256'] == plan['plan_sha256'] == index['plan_semantic_sha256']
    assert receipt['plan_file_sha256'] == sha(plan_raw) == index['plan_file_sha256']
    summary = json.loads(original[prefix + 'savings-native-001/summary.json'])
    assert summary['status'] == 'incomparable' and len(summary['blocks']) == 2
    assert len(plan['orders']) == 3
    assert 'paired_mean_saved_seconds_95_interval' not in summary
    assert 'observed_saved_seconds' not in summary
    assert summary['blocks'][0]['matching_full_target_work']
    assert 'matching_full_target_work' not in summary['blocks'][1]
    for block in summary['blocks']:
        for arm in block['arms'].values():
            assert sha(original[arm['record']]) == arm['record_sha256']
    assert summary['blocks'][1]['arms']['abbreviated']['status'] == 'incomparable'
    with tarfile.open(fileobj=io.BytesIO(gzip.decompress((HERE / index['source_archive']).read_bytes())), mode='r:') as tar:
        sources = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
    assert len(sources) == len(index['sources']) == 6
    for source in index['sources']:
        raw = sources[source['path']]
        assert len(raw) == source['bytes'] and sha(raw) == source['sha256']
    assert sha(sources['savings-native-design/experiment.py']) == plan['script_sha256']
    for name, expected in plan['helper_sha256'].items():
        assert sha(sources['tools/' + name]) == expected
    anchor = next(raw for name, raw in original.items() if name.endswith('anchors-v3/native-vlf-low-cn0.native-anchor.json'))
    assert sha(anchor) == plan['anchor_file_sha256']
    native = json.loads(original['local-evidence/triad-performance-trace/reference-build-provenance.json'])
    assert native['reference_binary']['sha256'] == plan['binary_sha256']['reference']
    assert native['current_binary']['sha256'] == plan['binary_sha256']['candidate']
    print(f"Verified {len(original)} exact records, 6 frozen source files, all four arm receipts, and stopped incomparable status.")


if __name__ == '__main__':
    main()
