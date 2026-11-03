#!/usr/bin/env python3
"""Run one bounded, explicit source-variant rescore stage; never regenerate I/Q."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor,wait,FIRST_COMPLETED
import json
from pathlib import Path
import sys
import time
import prepare

def read_variant_receipts(paths,manifest_hash,definitions,output):
    records={};refs=[]
    for path in paths:
        value=json.loads(path.read_text())
        if value.get('manifest_sha256')!=manifest_hash or value.get('variant')!='numerical-fix':raise ValueError('variant receipt identity differs')
        if value.get('inputs_unchanged_at_end') is not True or value.get('all_selected_jobs_accounted') is not True:
            raise ValueError('variant receipt incomplete or changed inputs')
        refs.append({'path':str(path),'sha256':prepare.sha(path)})
        for row in value['jobs']:
            if row['id'] not in definitions or row['definition_sha256']!=definitions[row['id']]['definition_sha256']:
                raise ValueError('variant job definition differs')
            if row['id'] in records:raise ValueError('variant job already accounted: '+row['id'])
            for item in row.get('output_files',[]):
                file=output/item['path']
                if file.stat().st_size!=item['size_bytes'] or prepare.sha(file)!=item['sha256']:raise ValueError('variant output differs')
            records[row['id']]=row
    return records,refs

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--stage',choices=prepare.STAGES,required=True)
    p.add_argument('--python',type=Path,default=Path(sys.executable));p.add_argument('--run-dir',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=8);p.add_argument('--global-seconds',type=float,default=3600)
    p.add_argument('--only-id',action='append',default=[]);p.add_argument('--prior-results',action='append',type=Path,default=[])
    args=p.parse_args(argv)
    if not 1<=args.jobs<=8 or not 0<args.global_seconds<=3600:p.error('at most8 workers and1 hour per stage')
    args.python=args.python.absolute() # Do not resolve a virtual-environment symlink.
    args.run_dir=args.run_dir.resolve()
    if not args.python.is_file() or args.run_dir.exists():p.error('interpreter must exist and run-dir must be fresh')
    manifest=json.loads(args.manifest.read_text());manifest_hash=prepare.sha(args.manifest)
    core,baseline=prepare.verify_manifest(manifest)
    definitions={j['id']:j for j in manifest['jobs']}
    selected=[j for j in manifest['jobs'] if j['stage']==args.stage]
    if args.only_id:
        if not set(args.only_id)<=set(j['id'] for j in selected):p.error('unknown selected job')
        selected=[j for j in selected if j['id'] in args.only_id]
    if not selected:p.error('empty stage')
    args.root=Path(manifest['patched_root']);args.output_root=Path(manifest['output_root'])
    prior,prior_refs=read_variant_receipts(args.prior_results,manifest_hash,definitions,args.output_root)
    if any(j['id'] in prior for j in selected):p.error('selected variant job already accounted; no implicit retry')
    args.run_dir.mkdir(parents=True);args.output_root.mkdir(parents=True,exist_ok=True)
    bindings={'ROOT':str(args.root),'OUT':str(args.output_root),'PYTHON':str(args.python),'BINARY':manifest['binary']['path']}
    started=time.monotonic();deadline=started+args.global_seconds;results=[];pending={};cursor=0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor<len(selected) or pending:
            while cursor<len(selected) and len(pending)<args.jobs:
                job=selected[cursor];cursor+=1
                if time.monotonic()+job['timeout_seconds']+10>deadline:
                    results.append(dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='not_started_deadline',depends_on=job['depends_on'],output_files=[]))
                else:pending[pool.submit(prepare.runner.execute,job,args,bindings,baseline)]=job
            if pending:
                done,_=wait(pending,return_when=FIRST_COMPLETED,timeout=1)
                for future in done:
                    job=pending.pop(future)
                    try:record=future.result()
                    except Exception as error:record=dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='runner_error',reason=repr(error),depends_on=job['depends_on'],output_files=[])
                    record.update(variant='numerical-fix',original_definition_sha256=job['original_definition_sha256'])
                    results.append(record)
                    print(json.dumps({'finished':record['id'],'status':record['status']}),flush=True)
    unchanged=True;error=None
    try:prepare.verify_manifest(manifest)
    except Exception as caught:unchanged=False;error=repr(caught)
    counts={s:sum(r['status']==s for r in results) for s in sorted({r['status'] for r in results})}
    receipt=dict(schema='triad-broad-rescore-stage-v1',variant='numerical-fix',manifest_sha256=manifest_hash,
        baseline_plan_sha256=manifest['baseline_plan']['sha256'],stage=args.stage,
        source_file_sha256=manifest['source_file_sha256'],binary_sha256=manifest['binary']['sha256'],
        inputs_unchanged_at_end=unchanged,input_error=error,all_selected_jobs_accounted=len(results)==len(selected),
        python_invocation=str(args.python),run_py_sha256=prepare.sha(__file__),prior_receipts=prior_refs,
        elapsed_seconds=time.monotonic()-started,global_seconds_bound=args.global_seconds,concurrent_jobs=args.jobs,
        counts=counts,jobs=sorted(results,key=lambda r:r['id']))
    (args.run_dir/'results.json').write_text(json.dumps(receipt,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'receipt':str(args.run_dir/'results.json'),'counts':counts}))
    return int(not unchanged or len(results)!=len(selected))

if __name__=='__main__':raise SystemExit(main())
