#!/usr/bin/env python3
"""Compose reviewed private replay/CPU layers; never modify launcher sources."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    root, report = args.root.resolve(strict=True), args.report.resolve()
    pins = json.loads((HERE / 'pins.json').read_text())
    for relative, expected in pins.items():
        if sha(HERE / relative) != expected:
            raise ValueError('reviewed component differs: ' + relative)
    spec = importlib.util.spec_from_file_location('accepted_capacity_capture', HERE.parent / 'capture.py')
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    base.prepare(root, report)
    archive = json.loads((report / 'source-archive.json').read_text())
    source = Path(archive['compiled_source_directory']).resolve(strict=True)
    first = report / 'overlay-evidence'
    replay, inner = report / 'replay-evidence', report / 'inner-evidence'
    subprocess.run(['python3', '-B', str(HERE / 'prepare-replay.py'),
                    '--source-root', str(root), '--first-layer', str(first),
                    '--fixture-dir', str(HERE / 'fixture'), '--output-dir', str(replay)], check=True)
    subprocess.run(['python3', '-B', str(HERE / 'prepare-inner.py'),
                    '--source-root', str(source), '--first-layer', str(first),
                    '--output-dir', str(inner)], check=True)
    replay_manifest = json.loads((replay / 'manifest.json').read_text())
    inner_manifest = json.loads((inner / 'manifest.json').read_text())
    inventory = dict(archive['compiled_overlay_sha256'])
    if set(replay_manifest['source_hashes']) & set(inner_manifest['second_layer']):
        raise ValueError('private layers overlap')
    for relative, item in replay_manifest['source_hashes'].items():
        prepared = replay / 'prepared-source' / relative
        if sha(source / relative) != item['before_sha256'] or sha(prepared) != item['after_sha256']:
            raise ValueError('replay composition mismatch: ' + relative)
        if item['before_sha256'] != item['reverse_sha256']:
            raise ValueError('replay reverse mismatch: ' + relative)
        shutil.copyfile(prepared, source / relative)
        inventory[relative] = item['after_sha256']
    for relative, item in inner_manifest['second_layer'].items():
        prepared = inner / (Path(relative).name + '.post-inner')
        if sha(source / relative) != item['before_sha256'] or sha(prepared) != item['after_sha256']:
            raise ValueError('inner composition mismatch: ' + relative)
        if item['before_sha256'] != item['reverse_sha256']:
            raise ValueError('inner reverse mismatch: ' + relative)
        shutil.copyfile(prepared, source / relative)
        inventory[relative] = item['after_sha256']
    for relative, expected in inventory.items():
        if sha(source / relative) != expected:
            raise ValueError('final compiled inventory mismatch: ' + relative)
    # The fixture is inside the source archive, independently pinned by both
    # preparation and the linked diagnostic's entry point.
    fixture_dir = source / 'tools/capacity-shape-diagnostic/acquisition/fixture'
    pcm = fixture_dir / 'a3-source-input.f32'
    sidecar = fixture_dir / 'a3-source-input.f32.partitions.json'
    oracle = fixture_dir / 'a3-oracle.json'
    for path in (pcm, sidecar, oracle):
        relative = str(path.relative_to(fixture_dir.parent))
        if sha(path) != pins[relative]:
            raise ValueError('archived fixture changed: ' + relative)
    integrated = {
        'launcher_source_commit': archive['launcher_source_commit'],
        'application_source_commit': archive['application_source_commit'],
        'classification': 'diagnostic_prefix_replay_only',
        'compiled_source_directory': str(source),
        'compiled_overlay_sha256': inventory,
        'first_layer_archive': archive,
        'layer_manifests': {str(replay / 'manifest.json'): sha(replay / 'manifest.json'),
                            str(inner / 'manifest.json'): sha(inner / 'manifest.json')},
        'reviewed_components': pins,
        'fixture': {'pcm_path': str(pcm), 'pcm_sha256': sha(pcm),
                    'sidecar_path': str(sidecar), 'sidecar_sha256': sha(sidecar),
                    'oracle_path': str(oracle), 'oracle_sha256': sha(oracle),
                    'generated_samples': 154676, 'committed_prefix_samples': 154038,
                    'partitions': 233, 'original_run': 39269648667, 'original_job': 118118268914},
        'limits': ['prefix diagnostic only; original full-fixture assertions not executed',
                   'no original publication/replay performance or full-memory equivalence',
                   'final complete CPU snapshot supports aggregate; no exact case deltas'],
    }
    target = report / 'integrated-source.json'
    target.write_text(json.dumps(integrated, indent=2) + '\n')
    if json.loads(target.read_text()) != integrated:
        raise ValueError('integrated identity readback mismatch')
    print(json.dumps({'status': 'prepared_diagnostic_only', 'launcher': integrated['launcher_source_commit'],
                      'compiled_paths': len(inventory), 'fixture': integrated['fixture']}))


if __name__ == '__main__':
    main()
