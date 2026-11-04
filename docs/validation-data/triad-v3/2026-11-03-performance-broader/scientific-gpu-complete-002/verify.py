#!/usr/bin/env python3
"""Verify immutable evidence without importing receiver or performance modules."""
from pathlib import Path
import gzip
import hashlib
import io
import json
import tarfile

HERE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def render(value):
    return (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()


class Reader:
    def __init__(self, root):
        self.root = root
        self.bytes = {}
        self.documents = {}

    def raw(self, name):
        if name not in self.bytes:
            packed = (self.root / name).read_bytes()
            assert packed[:3] == b'\x1f\x8b\x08' and packed[3:8] == bytes(5), name
            self.bytes[name] = gzip.decompress(packed)
        return self.bytes[name]

    def document(self, name):
        if name not in self.documents:
            self.documents[name] = json.loads(self.raw(name))
        return self.documents[name]

    def record(self, row):
        if 'embedded_in' in row:
            value = self.document(row['embedded_in'])
            for key in row['json_path']:
                value = value[key]
            data = render(value)
        elif 'object_from' in row:
            source = self.document(row['object_from'])
            value = {}
            for field in row['fields']:
                if 'literal' in field:
                    value[field['key']] = field['literal']
                else:
                    node = source
                    for key in field['json_path']:
                        node = node[key]
                    value[field['key']] = node
            data = render(value)
        elif row.get('storage') == 'compact_json_reformatted_indent2_newline':
            data = render(self.document(row['archive']))
        else:
            data = self.raw(row['archive'])
        assert len(data) == row['raw_bytes'] and sha(data) == row['raw_sha256'], row['input_path']
        return data


def main():
    for line in (HERE / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert sha((HERE / name).read_bytes()) == digest, name
    index = json.loads((HERE / 'preservation.json').read_text())
    reader = Reader(HERE)
    records = {row['input_path']: row for row in index['records']}
    for row in index['records']:
        reader.record(row)
    sources = reader.document('source-index.json.gz')
    with tarfile.open(fileobj=io.BytesIO(reader.raw(sources['archive'])), mode='r:') as tar:
        members = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
    assert len(members) == len(sources['members'])
    for member in sources['members']:
        data = members[member['member']]
        assert len(data) == member['bytes'] and sha(data) == member['sha256']
    documents = {}
    for binding in sources['bindings']:
        name = binding['record']
        if name not in documents:
            documents[name] = json.loads(reader.record(records[name]))
        value = documents[name]
        for key in binding['json_path']:
            value = value[key]
        assert value == binding['sha256'] == sha(members[binding['member']]), binding['path']
    prefix = 'local-evidence/triad-performance-broader/gpu/final-routine/complete-routine-002/'
    summary = documents[prefix + 'summary.json']
    core = documents[prefix + 'native-fft.json']
    assert summary['status'] == 'incomplete' and summary['errors'] == []
    assert summary['outcomes']['performance_regression']['regressions'] == []
    assert summary['outcomes']['performance_regression']['status'] == 'inconclusive'
    assert set(summary['outcomes']['prediction_accuracy']['unresolved']) == {'broad-hf-timing', 'free-crystal'}
    assert len(summary['applicable_sampled_cases']) == 12
    assert len(summary['functional_commands']) == 17
    assert all(row['valid'] for row in summary['extension_outcomes'].values())
    refs = json.loads((HERE / 'external-inputs.json').read_text())['records']
    external_readers = {}
    coverage_hashes = set()
    for ref in refs:
        if 'record' in ref:
            if ref['bundle'] not in external_readers:
                external_readers[ref['bundle']] = Reader(HERE / ref['bundle'])
            external_readers[ref['bundle']].record(ref['record'])
        else:
            packed = (HERE / ref['relative_path']).read_bytes()
            assert sha(packed) == ref['file_sha256']
            data = gzip.decompress(packed) if ref['storage'] == 'gzip' else packed
            assert sha(data) == ref['raw_sha256']
            coverage_hashes.add(sha(data))
    assert coverage_hashes == {row['sha256'] for row in summary['coverage_receipts']}
    native_path = 'local-evidence/triad-performance-broader/gpu/lifecycle/native-build/provenance.json'
    native = json.loads(reader.record(records[native_path]))
    for side in ('candidate', 'reference'):
        assert native['binaries'][side]['triad_long'] == core['native_binary_sha256'][side]
    assert set(summary['extension_binary_sha256'].values()) == {'adfa35b5f24ece58f63607962a979bc45d03b5bba5bed9a3c3df342a11a10451'}
    post_count = post_sources = 0
    if (HERE / 'post-run/preservation.json').exists():
        post = json.loads((HERE / 'post-run/preservation.json').read_text())
        post_data = {row['input_path']: reader.record(row) for row in post['records']}
        replay_path = 'local-evidence/triad-performance-broader/gpu/final-routine/current-lifecycle-replay.json'
        replay = json.loads(post_data[replay_path])
        assert replay['valid'] and replay['status'] == 'passed' and replay['errors'] == []
        lifecycle = documents[prefix + 'extensions/lifecycle/summary.json']
        assert replay['source_transfer']['old_protocol_sha256'] == lifecycle['protocol_sha256']
        assert replay['source_transfer']['old_manifest_sha256'] == lifecycle['manifest_sha256']
        with tarfile.open(fileobj=io.BytesIO(reader.raw(post['source_archive'])), mode='r:') as tar:
            overlay = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
        assert len(overlay) == len(post['source_members']) == 5
        for member in post['source_members']:
            data = overlay[member['path']]
            assert len(data) == member['bytes'] and sha(data) == member['sha256']
        for name, digest in replay['current_source_sha256'].items():
            assert sha(overlay['tools/' + name]) == digest
        assert replay['source_transfer']['current_protocol_sha256'] == replay['current_source_sha256']['triad_perf_lifecycle.py']
        assert replay['source_transfer']['current_manifest_sha256'] == replay['current_source_sha256']['triad-performance-lifecycle.json']
        observed_path = 'local-evidence/triad-performance-broader/gpu/replay-current-lifecycle-observed.py'
        assert overlay['replay-current-lifecycle-observed.py'] == post_data[observed_path]
        post_count = len(post['records'])
        post_sources = len(overlay)
    if (HERE / 'post-run-final/preservation.json').exists():
        final = json.loads((HERE / 'post-run-final/preservation.json').read_text())
        final_data = {row['input_path']: reader.record(row) for row in final['records']}
        name = 'local-evidence/triad-performance-broader/gpu/final-routine/current-lifecycle-replay-93de7a47-afd35925.json'
        replay = json.loads(final_data[name])
        assert replay['valid'] and replay['status'] == 'passed' and replay['errors'] == []
        assert replay['review_required'] == [] and replay['outcomes']['sustained_stability'] == 'no_flag_observed'
        original = reader.record(records[prefix + 'extensions/lifecycle/summary.json'])
        assert sha(original) == replay['original_report_sha256']
        assert replay['original_context'] == summary['extensions']['lifecycle']['context']
        assert replay['original_context']['regression_profile'] == 'normal'
        with tarfile.open(fileobj=io.BytesIO(reader.raw(final['source_archive'])), mode='r:') as tar:
            overlay = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
        assert len(overlay) == len(final['sources']) == 4
        for row in final['sources']:
            assert len(overlay[row['path']]) == row['bytes'] and sha(overlay[row['path']]) == row['sha256']
        for name, digest in replay['current_source_sha256'].items():
            assert sha(overlay['tools/' + name]) == digest
        post_count += len(final['records'])
        post_sources += len(overlay)
    print(f"Verified {len(records)} exact records, {len(members)} source members, "
          f"{len(sources['bindings'])} source bindings, {len(refs)} retained inputs, "
          f"{post_count} separate post-run records/{post_sources} source files, and unchanged incomplete outcome.")


if __name__ == '__main__':
    main()
