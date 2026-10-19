#!/usr/bin/env python3
"""Compose reviewed storage, FFT and continuation probes on the A4 replay."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_inputs():
    pins = json.loads((HERE / 'pins.json').read_text())
    for name, digest in pins.items():
        if sha(HERE / name) != digest:
            raise ValueError('execution component hash mismatch: ' + name)
    return json.loads((HERE / 'manifest.json').read_text()), json.loads((HERE / 'census/manifest.json').read_text())


def prepare(root, report):
    root, report = root.resolve(strict=True), report.resolve(strict=True)
    manifest, census = checked_inputs()
    target = report / 'integrated-source.json'
    integrated = json.loads(target.read_text())
    source = Path(integrated['compiled_source_directory']).resolve(strict=True)
    if not source.is_relative_to(root / 'build') or source == root:
        raise ValueError('only a private prepared build source may be changed')
    evidence = report / 'execution-evidence'
    evidence.mkdir(exist_ok=False)
    shutil.copyfile(target, evidence / 'a4-integrated-source.json')
    before = {**census['read_pins'], **manifest['fft_preimages']}
    for name, digest in before.items():
        if sha(source / name) != digest:
            raise ValueError('A4 read pin mismatch before mutation: ' + name)
    for name, digest in integrated['compiled_overlay_sha256'].items():
        if sha(source / name) != digest:
            raise ValueError('A4 integrated overlay mismatch: ' + name)
    for patch, preimages, postimages in (
        (HERE / 'census/overlay.patch', census['preimages'], census['postimages']),
        (HERE / 'fft.patch', manifest['fft_preimages'], manifest['fft_postimages']),
        (HERE / 'continuation.patch', manifest['continuation_preimages'], manifest['continuation_postimages']),
    ):
        for name, digest in preimages.items():
            path = source / name
            if digest == hashlib.sha256(b'').hexdigest() and not path.exists():
                continue
            if sha(path) != digest:
                raise ValueError('patch preimage mismatch: ' + name)
        content = patch.read_bytes()
        # Git is an established runner dependency; the lean container does not
        # provide the standalone patch utility. Bind paths to the private tree.
        command = ['git', '-c', 'safe.directory=' + str(root), 'apply',
                   '--directory=' + str(source.relative_to(root)), '-p1']
        subprocess.run(command + ['--check'], input=content, cwd=root, check=True, capture_output=True)
        subprocess.run(command, input=content, cwd=root, check=True, capture_output=True)
        for name, digest in postimages.items():
            if sha(source / name) != digest:
                raise ValueError('patch postimage mismatch: ' + name)
    additional = {**manifest['fft_postimages'], **manifest['continuation_postimages']}
    expected = {**census['read_pins'], **census['postimages'], **additional}
    actual = {name: sha(source / name) for name in expected}
    if actual != expected:
        raise ValueError('complete composed source inventory mismatch')
    # The original census reader remains byte-for-byte unchanged. Its expected
    # inventory explicitly includes the separately reviewed scope postimages.
    composed = dict(census)
    composed['read_pins'] = {**census['read_pins'], **additional}
    composed['postimages'] = {**census['postimages'], **additional}
    composed['composition'] = {'parent_manifest_sha256': sha(HERE / 'census/manifest.json'),
                               'fft_patch_sha256': manifest['fft_patch_sha256'],
                               'continuation_patch_sha256': manifest['continuation_patch_sha256'],
                               'additional_postimages': additional}
    (evidence / 'composed-census-manifest.json').write_text(json.dumps(composed, indent=2) + '\n')
    for name in {**census['postimages'], **additional}:
        dest = evidence / 'compiled-source' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, dest)
    for name in ('manifest.json', 'pins.json', 'census/manifest.json', 'census/reader.py', 'census/overlay.patch', 'fft.patch', 'continuation.patch'):
        dest = evidence / 'components' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / name, dest)
    integrated['compiled_overlay_sha256'] = {**integrated['compiled_overlay_sha256'], **actual}
    integrated['execution_components'] = json.loads((HERE / 'pins.json').read_text())
    integrated['composed_census_manifest_path'] = str(evidence / 'composed-census-manifest.json')
    integrated['composed_census_manifest_sha256'] = sha(evidence / 'composed-census-manifest.json')
    integrated['limits'].append('terminal storage observer and bounded continuation markers constant ON; detailed FFT and continuation scopes switch together OFF/ON; terminal observer outside covered Context, inside child CPU; excludes writer thread and final free')
    target.write_text(json.dumps(integrated, indent=2) + '\n')
    if json.loads(target.read_text()) != integrated:
        raise ValueError('composed identity readback mismatch')
    print(json.dumps({'status': 'prepared_diagnostic_only', 'compiled_paths': len(integrated['compiled_overlay_sha256']),
                      'census_identity': census['overlay_identity'], 'fft_patch_sha256': manifest['fft_patch_sha256']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.root, args.report)
