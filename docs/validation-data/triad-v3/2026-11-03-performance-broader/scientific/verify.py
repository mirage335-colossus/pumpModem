#!/usr/bin/env python3
"""Verify this compact evidence archive without running receivers or benchmarks."""
from pathlib import Path
import ast
import gzip
import hashlib
import io
import json
import tarfile

HERE = Path(__file__).resolve().parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    for line in (HERE / 'SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        assert digest((HERE / name).read_bytes()) == expected, name
    preservation = json.loads((HERE / 'preservation.json').read_text())
    raw_cache = {}
    json_cache = {}

    def raw(name):
        if name not in raw_cache:
            data = (HERE / name).read_bytes()
            assert data[:3] == b'\x1f\x8b\x08' and data[3:8] == bytes(5), name
            raw_cache[name] = gzip.decompress(data)
        return raw_cache[name]

    def document(name):
        if name not in json_cache:
            json_cache[name] = json.loads(raw(name))
        return json_cache[name]

    by_input = {}
    all_records = preservation['records']
    if (HERE / 'additional-preservation.json').exists():
        all_records += json.loads((HERE / 'additional-preservation.json').read_text())['records']
    for record in all_records:
        if 'embedded_in' in record:
            value = document(record['embedded_in'])
            for key in record['json_path']:
                value = value[key]
            data = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
        elif 'object_from' in record:
            source = document(record['object_from'])
            value = {}
            for field in record['fields']:
                if 'literal' in field:
                    value[field['key']] = field['literal']
                else:
                    node = source
                    for key in field['json_path']:
                        node = node[key]
                    value[field['key']] = node
            data = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
        elif record.get('storage') == 'compact_json_reformatted_indent2_newline':
            data = (json.dumps(document(record['archive']), indent=2, allow_nan=False) + '\n').encode()
        else:
            data = raw(record['archive'])
        assert len(data) == record['raw_bytes'], record['input_path']
        assert digest(data) == record['raw_sha256'], record['input_path']
        by_input[record['input_path']] = data
    sources = json.loads((HERE / 'source-index.json').read_text())
    with tarfile.open(fileobj=io.BytesIO(raw(sources['archive'])), mode='r:') as tar:
        members = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
    assert len(members) == len(sources['members'])
    for member in sources['members']:
        data = members[member['member']]
        assert len(data) == member['bytes'] and digest(data) == member['sha256']
    binding_documents = {}
    for binding in sources['bindings']:
        if binding['record'] not in binding_documents:
            data = by_input['local-evidence/triad-performance-broader/' + binding['record']]
            binding_documents[binding['record']] = json.loads(data)
        value = binding_documents[binding['record']]
        for key in binding['json_path']:
            value = value[key]
        assert value == binding['sha256'] == digest(members[binding['member']]), binding['path']
    for fixture in json.loads((HERE / 'input-verification.json').read_text())['records']:
        for file in fixture['files']:
            assert file['observed_sha256'] == file['expected_sha256']
    references = json.loads((HERE / 'external-references.json').read_text())
    for reference in references['records']:
        assert digest((HERE / reference['relative_path']).read_bytes()) == reference['sha256']
    additional_bindings = body_count = additional_members = 0
    index_name = 'additional-source-index.json.gz'
    if (HERE / index_name).exists():
        extra = document(index_name)
        archives = {sources['archive']: members}
        parsed_sources = {}

        def source_bytes(row):
            if 'gzip_source' in row:
                return raw(row['gzip_source'])
            archive = row['archive']
            if archive not in archives:
                with tarfile.open(fileobj=io.BytesIO(raw(archive)), mode='r:') as tar:
                    archives[archive] = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
            return archives[archive][row['member']]

        for row in extra['members'] + extra['loose_sources']:
            value = source_bytes(row)
            assert len(value) == row['bytes'] and digest(value) == row['sha256']
        for binding in extra['bindings'] + extra['body_bindings']:
            record = binding['record']
            if record not in binding_documents:
                binding_documents[record] = json.loads(by_input['local-evidence/triad-performance-broader/' + record])
            value = binding_documents[record]
            for key in binding['json_path']:
                value = value[key]
            assert value == binding['sha256']
            source = source_bytes(binding)
            if 'function' in binding:
                assert digest(source) == binding['source_sha256']
                if binding['source_sha256'] not in parsed_sources:
                    parsed_sources[binding['source_sha256']] = ast.parse(source)
                node = next(n for n in parsed_sources[binding['source_sha256']].body
                            if isinstance(n, (ast.FunctionDef, ast.ClassDef)) and n.name == binding['function'])
                observed = digest(ast.dump(node, include_attributes=False,
                                          show_empty=binding['ast_dump_show_empty']).encode())
                assert observed == binding['sha256'], binding['function']
            else:
                assert digest(source) == binding['sha256']
        assert len(extra['unavailable']) == 1
        assert extra['unavailable'][0]['sha256'] == 'f388816d82ab53076510372149ec09696ca5ce800eb9c794e1c5babfe334d60d'
        additional_bindings = len(extra['bindings'])
        body_count = len(extra['body_bindings'])
        additional_members = len(extra['members']) + len(extra['loose_sources'])
        native = json.loads(by_input['local-evidence/triad-performance-broader/gpu/lifecycle/native-build/provenance.json'])
        gpu = json.loads(by_input['local-evidence/triad-performance-broader/gpu/legacy/legacy-qualification-001/report.cuda.json'])
        for side in ('candidate', 'reference'):
            assert native['binaries'][side]['triad_long'] == gpu['native_binary_sha256'][side]
    build = json.loads(by_input['local-evidence/triad-performance-broader/planner-build/build-evidence.json'])
    for side, dirname in (('candidate', 'dev-cli'), ('reference', 'dev-cli-242-reference-source')):
        assert digest(by_input[f'build/{dirname}/CMakeCache.txt']) == build['builds'][side]['cache_sha256']
        assert digest(by_input[f'local-evidence/triad-performance-broader/planner-build/{side}-commands.txt']) == build['builds'][side]['commands_sha256']
    print(f"Verified {len(all_records)} exact records, {len(members)+additional_members} source members, "
          f"{len(sources['bindings'])+additional_bindings} source bindings, {body_count} AST receipts, "
          f"and {len(references['records'])} retained references. One failed-attempt source gap remains explicit.")


if __name__ == '__main__':
    main()
