#!/usr/bin/env python3
"""Package compact, source-bound study evidence; never run a modem campaign."""
from pathlib import Path
import gzip
import hashlib
import io
import json
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[4]
DEST = Path(__file__).resolve().parent
EVIDENCE = ROOT / 'local-evidence/triad-helper-extended'
BASE = '388d0749bc3fae9c529a5d298946ab2d6ee19791'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if head != BASE:
        raise RuntimeError('source base changed; inspect provenance before repackaging')
    frozen = json.loads((EVIDENCE / 'candidate-v2-source-identities.json').read_text())
    for relative, expected in frozen['files'].items():
        if sha(ROOT / relative) != expected:
            raise RuntimeError('frozen candidate source changed: ' + relative)
    if not (EVIDENCE / 'remote/extended-main-v2/summary.json').is_file():
        raise RuntimeError('complete campaign receipt required; no partial package promotion')
    files = {}
    allowed = {'.json', '.jsonl', '.md', '.txt', '.log', '.py', '.cpp', '.hpp', '.stderr', '.patch'}
    for path in sorted(EVIDENCE.rglob('*')):
        if path.is_file() and not path.is_symlink() and path.suffix in allowed and '__pycache__' not in path.parts:
            if path.stat().st_size > 256 * 1024**2:
                raise RuntimeError('unexpectedly large compact evidence file: ' + str(path))
            files['study/' + path.relative_to(EVIDENCE).as_posix()] = path
    # Full current files accompany the base commit and tracked diff, including
    # new untracked domain/test files. Large binary inputs have retained hashes.
    for relative in frozen['files']:
        files['source/' + relative] = ROOT / relative
    for relative in ('tools/triad_helper_assistance.py', 'tools/triad_helper_fft.py',
                     'tools/triad_helper_broad_campaign.py', 'tools/triad_helper_statistics.py',
                     'tools/triad_helper_coverage.py'):
        files['source/' + relative] = ROOT / relative
    patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD', '--',
        'tools', 'tests', 'docs/README.md', 'docs/triad-helper.md',
        'docs/triad-helper-coverage.md', 'docs/triad-performance-matrix.md'], cwd=ROOT)
    rows = [dict(path=name, bytes=path.stat().st_size, sha256=sha(path)) for name,path in files.items()]
    rows.append(dict(path='source/changes-from-base.patch', bytes=len(patch), sha256=hashlib.sha256(patch).hexdigest()))
    archive = DEST / 'compact-evidence.tar.gz'
    with archive.open('wb') as sink, gzip.GzipFile(fileobj=sink, mode='wb', filename='', mtime=0) as zipped, tarfile.open(fileobj=zipped, mode='w|') as tar:
        for name,path in files.items():
            info = tarfile.TarInfo(name); info.size = path.stat().st_size; info.mode = 0o644
            with path.open('rb') as stream:
                tar.addfile(info, stream)
        info = tarfile.TarInfo('source/changes-from-base.patch'); info.size = len(patch); info.mode = 0o644
        tar.addfile(info, io.BytesIO(patch))
    manifest = dict(schema='triad-helper-extended-compact-evidence-v1', base_commit=BASE,
        archive=dict(file=archive.name, bytes=archive.stat().st_size, sha256=sha(archive)), files=rows,
        limits=['No raw waveform arrays or binaries: original hashes and generation commands retained.',
                'Source base plus patch/full candidate files reproduce the candidate; package creation is not receiver qualification.',
                'Provider billing, credentials, SSH endpoints and private resource reservations are intentionally outside this scientific package.'])
    mp = DEST / 'artifact-manifest.json'; mp.write_text(json.dumps(manifest, indent=2)+'\n')
    sums = [archive, mp, Path(__file__)]
    (DEST / 'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sums))
    print(json.dumps({'files':len(rows), **manifest['archive']}))


if __name__ == '__main__':
    main()
