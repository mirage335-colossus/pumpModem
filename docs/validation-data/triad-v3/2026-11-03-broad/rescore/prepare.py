#!/usr/bin/env python3
"""Bind an explicit numerical rescore to immutable physical exports and jobs."""
from __future__ import annotations
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile

HERE=Path(__file__).resolve().parent
PREFIX='docs/validation-data/triad-v3/2026-11-03-broad/'
STAGES=('actual','conditional','benchmark')
SCORER=('triad-conditional-sweep.py','triad-noise-study.py','triad-rbpf-study.py',
    'triad-cfo-batch-study.py','triad-static-frequency-study.py','triad_gaussian_window.py',
    'triad_gaussian_state.py','triad_rbpf.py','triad_noise_likelihood.py',
    'triad_state_workspace.py','triad_phase_frequency.py')

def load_file(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

runner=load_file('rescore_original_runner',HERE.parent/'reproduction/run_plan.py')
sha=runner.sha;digest=runner.digest

def source_manifest(root):
    files=['tools/'+name for name in SCORER]
    files+=['tools/triad_bounded_run.py',PREFIX+'reproduction/run_plan.py',PREFIX+'reproduction/analyze.py']
    files += [PREFIX+'rescore/'+name for name in ('prepare.py','run.py','analyze.py','test_plan.py')]
    return {name:sha(root/name) for name in files}

def verify_hashes(root,files):
    changed=[name for name,value in files.items() if not (root/name).is_file() or sha(root/name)!=value]
    if changed:raise ValueError('source closure changed: '+', '.join(changed))

def check_receipt_paths(receipts):
    for entry in receipts:
        if sha(entry['path'])!=entry['sha256']:raise ValueError('baseline receipt bytes changed: '+entry['path'])

def read_baseline(manifest):
    ref=manifest['baseline_plan'];path=Path(ref['path'])
    if sha(path)!=ref['sha256']:raise ValueError('baseline plan bytes changed')
    core=json.loads(path.read_text());definitions={j['id']:j for j in core['jobs']}
    for job in definitions.values():runner.check_job(job)
    runner.verify_source(core,Path(manifest['original_root']))
    runner.verify_build(core,Path(manifest['build_provenance']['path']),Path(manifest['source_archive']['path']),sha(manifest['binary']['path']))
    if sha(manifest['binary']['path'])!=manifest['binary']['sha256']:raise ValueError('original binary changed')
    # This import belongs to the actual scorer closure but was absent from
    # the baseline plan's individual-file list. Its original bytes are still
    # bound by the build's complete source archive; verify without extraction.
    extra='tools/triad_phase_frequency.py'
    with tarfile.open(manifest['source_archive']['path'],'r:gz') as archive:
        content=archive.extractfile(extra)
        if content is None:raise ValueError('baseline scorer dependency absent from source archive')
        archive_hash=hashlib.sha256(content.read()).hexdigest()
    if sha(Path(manifest['original_root'])/extra)!=archive_hash:
        raise ValueError('baseline extra scorer dependency differs from original archive')
    if manifest.get('baseline_extra_file_sha256',{extra:archive_hash})!={extra:archive_hash}:
        raise ValueError('baseline extra scorer source binding differs')
    check_receipt_paths(manifest['baseline_receipts'])
    records,_=runner.read_prior([Path(r['path']) for r in manifest['baseline_receipts']],ref['sha256'],definitions,
        Path(manifest['original_output']),core['expected_binary_sha256'],core['build_provenance_sha256'],core['source_file_sha256'])
    required={j['id'] for j in core['jobs']}
    if set(records)!=required:raise ValueError('baseline receipt set does not account for every frozen core job')
    # read_prior checks every recorded file. Require the complete workspace
    # inventory as well: no missing/unrecorded arrays can enter the rescore.
    for job in core['jobs']:
        if job['stage']!='export' or records[job['id']]['status']!='completed':continue
        workspace=Path(job['workspace'].replace('{OUT}',manifest['original_output']))
        actual=runner.inventory(workspace,Path(manifest['original_output']))
        if actual!=records[job['id']]['output_files']:
            raise ValueError('workspace inventory differs: '+job['id'])
    return core,records

def variant_jobs(core,original_output):
    jobs=[]
    for original in core['jobs']:
        if original['stage'] not in STAGES:continue
        job=copy.deepcopy(original)
        workspace=original['workspace'].replace('{OUT}',str(original_output))
        job['argv']=[workspace if part==original['workspace'] else part for part in original['argv']]
        if job['argv'].count(workspace)!=1:raise ValueError('expected exactly one physical workspace argument')
        job['workspace']=workspace
        job['variant']='numerical-fix'
        job['original_definition_sha256']=original['definition_sha256']
        del job['definition_sha256'];job['definition_sha256']=digest(job)
        jobs.append(job)
    return jobs

def verify_manifest(manifest):
    body={k:v for k,v in manifest.items() if k!='definition_sha256'}
    if digest(body)!=manifest['definition_sha256']:raise ValueError('rescore manifest definition differs')
    core,records=read_baseline(manifest)
    if variant_jobs(core,manifest['original_output'])!=manifest['jobs']:
        raise ValueError('rescore changed scientific arguments or job identities')
    verify_hashes(Path(manifest['patched_root']),manifest['source_file_sha256'])
    for job in manifest['jobs']:runner.check_job(job)
    return core,records

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True,help='patched source root')
    p.add_argument('--scorer-manifest-only',action='store_true')
    p.add_argument('--baseline-plan',type=Path);p.add_argument('--receipts',nargs='+',type=Path)
    p.add_argument('--original-root',type=Path);p.add_argument('--original-output',type=Path)
    p.add_argument('--output-root',type=Path);p.add_argument('--binary',type=Path)
    p.add_argument('--build-provenance',type=Path);p.add_argument('--source-archive',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    if args.output.exists():p.error('manifest output must be fresh')
    root=args.root.resolve();sources=source_manifest(root)
    if args.scorer_manifest_only:
        result={'schema':'triad-broad-rescore-source-v1','source_file_sha256':sources}
    else:
        for field in ('baseline_plan','receipts','original_root','original_output','output_root','binary','build_provenance','source_archive'):
            if not getattr(args,field):p.error('required: --'+field.replace('_','-'))
        if args.original_root.resolve()==root:p.error('patched and original source trees must be distinct')
        if args.output_root.resolve().is_relative_to(args.original_root.resolve()):p.error('outputs must not modify original source tree')
        if args.output_root.exists():p.error('rescore output root must be fresh')
        result={'schema':'triad-broad-numerical-rescore-v1','variant':'numerical-fix',
            'baseline_plan':{'path':str(args.baseline_plan.resolve()),'sha256':sha(args.baseline_plan)},
            'baseline_receipts':[{'path':str(path.resolve()),'sha256':sha(path)} for path in args.receipts],
            'original_root':str(args.original_root.resolve()),'original_output':str(args.original_output.resolve()),
            'patched_root':str(root),'output_root':str(args.output_root.resolve()),
            'binary':{'path':str(args.binary.resolve()),'sha256':sha(args.binary)},
            'build_provenance':{'path':str(args.build_provenance.resolve()),'sha256':sha(args.build_provenance)},
            'source_archive':{'path':str(args.source_archive.resolve()),'sha256':sha(args.source_archive)},
            'source_file_sha256':sources,'limits':{'workers':8,'stage_seconds':3600},
            'scope':'Same observations, noise addresses, scientific arguments, precision and gates; explicit numerical source variant only.'}
        core,records=read_baseline(result)
        result['baseline_extra_file_sha256']={'tools/triad_phase_frequency.py':sha(args.original_root.resolve()/'tools/triad_phase_frequency.py')}
        result['jobs']=variant_jobs(core,result['original_output'])
        result['stage_counts']={stage:sum(j['stage']==stage for j in result['jobs']) for stage in STAGES}
        result['targets']=core['targets']
        result['baseline_status_counts']={s:sum(r['status']==s for r in records.values()) for s in sorted({r['status'] for r in records.values()})}
    result['definition_sha256']=digest(result)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'sha256':sha(args.output),'stage_counts':result.get('stage_counts')}))

if __name__=='__main__':main()
