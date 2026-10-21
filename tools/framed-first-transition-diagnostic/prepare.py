#!/usr/bin/env python3
"""Apply reviewed 4e90487 diagnostic postimages to a private source archive."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile

HERE=Path(__file__).resolve().parent
COMMIT='4e9048748e8d34ca4f90ef1dd01bd8d4588759b2'
ORIGIN=1792561396.3857498
ALLOWED={'src/live.cpp','src/pattern_receiver.cpp','src/pattern_correlator.cpp',
         'src/audio_trace.hpp','src/first_transition_probe.hpp','tests/test_live.cpp'}

def sha(data):return hashlib.sha256(data).hexdigest()
def packed(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def write_json(path,value):path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
def relative(value):
    p=PurePosixPath(value)
    if not value or p.is_absolute() or '\\' in value or '..' in p.parts or str(p)!=value:
        raise ValueError(f'unsafe path: {value!r}')
    return value
def load(path):
    def pairs(items):
        d={}
        for k,v in items:
            if k in d:raise ValueError(f'duplicate JSON key: {k}')
            d[k]=v
        return d
    return json.loads(path.read_text(),object_pairs_hook=pairs)
def inventory(root):
    result={}
    for prefix in ('src','include','tests','cmake'):
        base=root/prefix
        if not base.is_dir() or base.is_symlink():raise ValueError(f'bad source root: {base}')
        for p in sorted(base.rglob('*')):
            if p.is_symlink():raise ValueError(f'symlink in source closure: {p}')
            if p.is_file():result[p.relative_to(root).as_posix()]=sha(p.read_bytes())
    result['CMakeLists.txt']=sha((root/'CMakeLists.txt').read_bytes())
    return dict(sorted(result.items()))
def replace(path,data,mode):
    # Each source replacement is atomic; the caller rolls back its own preimages
    # if a later write fails. The archive belongs exclusively to this invocation.
    fd,name=tempfile.mkstemp(prefix='.framed-census-',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as stream:stream.write(data)
        os.chmod(name,mode);os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)
def prepare(source_root,output_dir):
    root=source_root.resolve(strict=True);out=output_dir.absolute()
    if out.exists():raise FileExistsError(f'refusing existing evidence directory: {out}')
    if out.is_relative_to(root):raise ValueError('evidence directory must be outside source root')
    spec=load(HERE/'manifest.json')
    if spec['baseline_source_commit']!=COMMIT or spec['fixed_origin']!=ORIGIN:raise ValueError('wrong adapter identity')
    if set(spec['files'])!=ALLOWED:raise ValueError('unexpected overlay file set')
    before=inventory(root)
    if before!=spec['input_source_inventory'] or sha(packed(before))!=spec['input_source_inventory_sha256']:
        raise ValueError('exact current4e input source closure does not match')
    payloads={};preimages={};modes={}
    for name,entry in spec['files'].items():
        relative(name);payload_name=relative(entry['payloadpath'])
        pp=(HERE/payload_name).resolve(strict=True)
        if not pp.is_relative_to(HERE) or not pp.is_file():raise ValueError('payload escapes adapter')
        data=pp.read_bytes()
        if sha(data)!=entry['after_sha256']:raise ValueError(f'payload hash mismatch: {name}')
        path=root/name
        prior=path.read_bytes() if path.exists() else None
        if (None if prior is None else sha(prior))!=entry['before_sha256']:raise ValueError(f'preimage mismatch: {name}')
        payloads[name]=data;preimages[name]=prior;modes[name]=path.stat().st_mode&0o777 if prior is not None else 0o644
    # All closure, preimages and payloads pass before any source writes.
    out.mkdir(parents=True,exist_ok=False)
    (out/'preimages').mkdir()
    for name,data in preimages.items():
        if data is not None:
            path=out/'preimages'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    for name in ('overlay.patch','reverse.patch'):(out/name).write_bytes((HERE/name).read_bytes())
    applied=[]
    try:
        for name,data in payloads.items():
            replace(root/name,data,modes[name]);applied.append(name)
        after=inventory(root)
        expected=dict(before)
        for name,data in payloads.items():expected[name]=sha(data)
        if after!=dict(sorted(expected.items())):raise ValueError('postimage source closure mismatch')
        inv={'baseline_source_commit':COMMIT,'files_sha256':after,'inventory_sha256':sha(packed(after))}
        write_json(out/'source-inventory.json',inv)
        result={k:v for k,v in spec.items() if k!='input_source_inventory'}
        result.update(applied=True,source_root=str(root),source_inventory_sha256=inv['inventory_sha256'],
                      adapter_manifest_sha256=sha((HERE/'manifest.json').read_bytes()),
                      scope='private first-loss census; no qualification, deadlines, workspace or retirement policy changed')
        write_json(out/'manifest.json',result)
        return result
    except BaseException:
        for name in reversed(applied):
            prior=preimages[name]
            if prior is None:(root/name).unlink()
            else:replace(root/name,prior,modes[name])
        write_json(out/'failed.json',{'applied':False,'rolled_back':inventory(root)==before})
        raise
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args();result=prepare(args.source_root,args.output_dir)
    print(json.dumps({k:result[k] for k in ('applied','baseline_source_commit','fixed_origin','source_inventory_sha256')},sort_keys=True))
if __name__=='__main__':main()
