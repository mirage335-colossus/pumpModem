#!/usr/bin/env python3
"""Apply the pinned source37 diagnostic to an isolated, unchanged checkout."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

BASE = '37f4315c553b9fc3807f2f173d9a0fae3f239ebc'
PATHS = {'src/live.cpp', 'src/gui/gui_smoke.cpp',
         'src/copied_help_candidate_trace.hpp', 'src/copied_help_pcm_probe.hpp'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--record', required=True, type=Path)
    args = parser.parse_args()
    base = args.base.resolve()
    tools = Path(__file__).resolve().parent
    manifest_bytes = (tools / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    git = ['git', '-c', 'safe.directory=' + str(base), '-C', str(base)]
    assert manifest['application_source'] == BASE and manifest['diagnostic_only'] is True
    assert subprocess.check_output(git + ['rev-parse', 'HEAD'], text=True, timeout=35).strip() == BASE
    subprocess.run(git + ['diff', '--exit-code', BASE, '--'], check=True, timeout=35)
    unknown = subprocess.check_output(git + ['ls-files', '--others', '--exclude-standard', '--',
                                           'src', 'include', 'tests', 'cmake'], text=True, timeout=35)
    assert not unknown, 'unexpected untracked compilation inputs: ' + unknown
    entries = manifest['files']
    assert len(entries) == len(PATHS) and {entry['path'] for entry in entries} == PATHS
    originals, proposed, changed = {}, {}, []
    # Verify every input before making the first change.
    for entry in entries:
        relative = entry['path']
        target = base / relative
        assert not target.is_symlink()
        originals[relative] = target.read_bytes() if target.exists() else None
        before = originals[relative]
        assert (digest(before) if before is not None else None) == entry['pre_sha256'], relative
        assert entry['payload'] == 'payload/' + relative
        proposed[relative] = (tools / entry['payload']).read_bytes()
        assert digest(proposed[relative]) == entry['post_sha256'], relative
        assert len(proposed[relative]) == entry['bytes'], relative
    record = {'schema': 1, 'base_commit': BASE, 'diagnostic_only': True,
              'manifest_sha256': digest(manifest_bytes), 'files': entries, 'applied': False}
    args.record.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive evidence creation also rejects an accidental repeated preparation.
    with args.record.open('x', encoding='utf-8') as output:
        try:
            for entry in entries:
                relative = entry['path']
                target = base / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                changed.append(relative)
                target.write_bytes(proposed[relative])
                assert digest(target.read_bytes()) == entry['post_sha256'], relative
            record['applied'] = True
        except BaseException as error:
            record['setup_error'] = repr(error)
            for relative in reversed(changed):
                target = base / relative
                if originals[relative] is None:
                    target.unlink(missing_ok=True)
                else:
                    target.write_bytes(originals[relative])
            record['restored_after_failure'] = all(
                not (base / relative).exists() if original is None else
                (base / relative).read_bytes() == original
                for relative, original in originals.items())
            raise
        finally:
            output.write(json.dumps(record, indent=2) + '\n')
    print('Verified source37 and all four diagnostic postimages; original assertions retained')


if __name__ == '__main__':
    main()
