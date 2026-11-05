#!/usr/bin/env python3
"""Seal compact stochastic-study receipts; never regenerate experiments."""
from pathlib import Path
import hashlib,json,shutil,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
EVIDENCE=ROOT/'local-evidence/triad-stochastic'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def pack(path, files, base):
    with tarfile.open(path,'w:gz') as archive:
        for file in sorted(files):
            archive.add(file,arcname=str(file.relative_to(base)),recursive=False)
    return {'file':path.name,'bytes':path.stat().st_size,'sha256':digest(path),'members':len(files)}
if not EVIDENCE.is_dir(): raise SystemExit('Retained local evidence required; this does not rerun campaigns')
files=[p for p in EVIDENCE.rglob('*') if p.is_file() and p.suffix in
       {'.json','.jsonl','.log','.stderr','.py','.cpp','.sha256','.txt','.csv','.md'}
       and '__pycache__' not in p.parts]
artifacts=[pack(OUT/'compact-evidence.tar.gz',files,EVIDENCE)]
changed=subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=ROOT,text=True).splitlines()
new=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines()
paths=sorted({name for name in changed+new if not name.startswith('docs/validation-data/')})
sources=[ROOT/name for name in paths if (ROOT/name).is_file()]
artifacts.append(pack(OUT/'source-overlay.tar.gz',sources,ROOT))
frozen=Path('/tmp/triad-stochastic-cloud/overlay.tar.gz')
if frozen.is_file(): shutil.copyfile(frozen,OUT/'frozen-v1-overlay.tar.gz')
if not (OUT/'frozen-v1-overlay.tar.gz').is_file(): raise SystemExit('Frozen original campaign overlay required')
artifacts.append(dict(file='frozen-v1-overlay.tar.gz',bytes=(OUT/'frozen-v1-overlay.tar.gz').stat().st_size,
                      sha256=digest(OUT/'frozen-v1-overlay.tar.gz')))
manifest=dict(schema='triad-stochastic-artifacts-v1',base_revision=subprocess.check_output(
    ['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),artifacts=artifacts,
    current_source_sha256={str(p.relative_to(ROOT)):digest(p) for p in sources},
    compact_input_sha256={str(p.relative_to(EVIDENCE)):digest(p) for p in files},
    scope='Compact receipts and reproducible commands; raw I/Q/templates/binaries omitted. Source overlays require the recorded base revision. Historical and current identities remain distinct.')
(OUT/'artifact-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
check=[OUT/a['file'] for a in artifacts]+[OUT/'artifact-manifest.json']
(OUT/'SHA256SUMS').write_text(''.join(digest(p)+'  '+p.name+'\n' for p in check))
print(json.dumps({'artifacts':artifacts,'source_files':len(sources),'compact_files':len(files)},indent=2))
