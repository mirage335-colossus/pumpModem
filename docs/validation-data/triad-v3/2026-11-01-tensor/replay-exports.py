#!/usr/bin/env python3
"""Regenerate sampled inputs and verify every byte against the archived hashes.

Retain the original manifest as immutable input metadata after checking the
regenerated manifest (apart from relocated argv). Save the new invocation and
manifest separately, so reproduction is not misrepresented as the original run.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--group', choices=('physical', 'physical-holdout'), required=True)
    p.add_argument('--case', help='Optional single folder name for a cheap check')
    p.add_argument('--jobs', type=int, default=4, choices=range(1, 9))
    a = p.parse_args()
    a.binary = a.binary.resolve()
    if sha(a.binary) != '88ee2b6b90f6233bd6ec46ac299d77b30b742bba361ac3cc12af0124a46fec0a':
        p.error('Use the archived exporter binary; a rebuilt binary requires separate validation')
    base = a.evidence / ('physical-holdout' if a.group == 'physical-holdout' else 'prepared/inputs/physical')
    hashes = json.loads((a.evidence / 'prepared' / ('holdout-input-hashes.json' if a.group == 'physical-holdout' else 'physical-input-hashes.json')).read_text())
    manifests = sorted(base.glob('*/manifest.json'))
    if a.case:
        manifests = [f for f in manifests if f.parent.name == a.case]
    if not manifests:
        p.error('No matching archived manifests')

    def replay(original):
        folder = a.output.resolve() / 'inputs' / a.group / original.parent.name
        folder.mkdir(parents=True, exist_ok=False)
        record = folder.parent.parent.parent / 'replay' / a.group / original.parent.name
        record.mkdir(parents=True, exist_ok=False)
        manifest = json.loads(original.read_text())
        prefix = 'inputs/' + a.group + '/' + original.parent.name + '/'
        if sha(original) != hashes[prefix + 'manifest.json']:
            raise ValueError('Archived manifest hash mismatch')
        command = list(manifest['command_argv'])
        if command[command.index('--mode') + 1] != 'export-stats':
            raise ValueError('Unexpected exporter mode')
        command[0] = str(a.binary)
        command[command.index('--output') + 1] = str(folder)
        start = time.perf_counter()
        with (record / 'stdout').open('w') as so, (record / 'stderr').open('w') as se:
            subprocess.run(command, stdout=so, stderr=se, timeout=900, check=True)
        generated = (folder / 'manifest.json').read_bytes()
        (record / 'generated-manifest.json').write_bytes(generated)
        current = json.loads(generated)
        if {k:v for k,v in current.items() if k != 'command_argv'} != {k:v for k,v in manifest.items() if k != 'command_argv'}:
            raise ValueError('Regenerated physical metadata mismatch')
        verified = {}
        for name, expected in hashes.items():
            if name.startswith(prefix) and not name.endswith('/manifest.json'):
                actual = sha(folder / name[len(prefix):])
                if actual != expected:
                    raise ValueError('Regenerated sample/statistic bytes differ: ' + name)
                verified[name] = actual
        (record / 'replay.json').write_text(json.dumps({'command':command, 'binary_sha256':sha(a.binary), 'elapsed_seconds':time.perf_counter()-start, 'verified_files':verified, 'manifest_policy':'Original manifest restored after metadata and all component byte checks; actual replay argv and generated manifest retained here.'}, indent=2) + '\n')
        (folder / 'manifest.json').write_bytes(original.read_bytes())
        return {'case':original.parent.name, 'verified_components':len(verified)}

    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for result in pool.map(replay, manifests):
            print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
