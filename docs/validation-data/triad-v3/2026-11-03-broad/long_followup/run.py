#!/usr/bin/env python3
"""Execute one long followup stage, with atomic receipts and owned children."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor,wait,FIRST_COMPLETED
import json
from pathlib import Path
import sys
import time
import prepare

def read_prior(paths,manifest_hash,manifest,output):
    definitions={j['id']:j for j in manifest['jobs']};records={};refs=[]
    for path in paths:
        value=json.loads(path.read_text())
        if value.get('manifest_sha256')!=manifest_hash or value.get('inputs_unchanged_at_end') is not True or value.get('all_selected_jobs_accounted') is not True:
            raise ValueError('invalid prior followup receipt')
        if value['source_file_sha256']!=manifest['source_file_sha256']:raise ValueError('prior source closure differs')
        refs.append(dict(path=str(path),sha256=prepare.sha(path)))
        for row in value['jobs']:
            ident=row['id']
            if ident in records or row['definition_sha256']!=definitions[ident]['definition_sha256']:raise ValueError('duplicate or changed prior job')
            for item in row.get('output_files',[]):
                file=output/item['path']
                if file.stat().st_size!=item['size_bytes'] or prepare.sha(file)!=item['sha256']:raise ValueError('prior output changed')
            if row['stage']=='export' and row['status']=='completed':
                directory=Path(definitions[ident]['workspace'].replace('{OUT}',str(output)))
                if prepare.runner.inventory(directory,output)!=row['output_files']:raise ValueError('prior workspace inventory differs')
            records[ident]=row
    return records,refs

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--stage',choices=prepare.STAGES,required=True)
    p.add_argument('--python',type=Path,default=Path(sys.executable));p.add_argument('--run-dir',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=1);p.add_argument('--global-seconds',type=float,default=7200)
    p.add_argument('--only-id',action='append',default=[]);p.add_argument('--prior-results',action='append',type=Path,default=[])
    args=p.parse_args(argv);started=time.monotonic()
    maximum=1 if args.stage=='benchmark' else (20 if args.stage in ('configure','export') else 8)
    if not 1<=args.jobs<=maximum or not 0<args.global_seconds<=7200:p.error('worker/stage bound exceeded')
    args.python=args.python.absolute();args.run_dir=args.run_dir.resolve()
    if not args.python.is_file() or args.run_dir.exists():p.error('interpreter must exist and run directory must be fresh')
    manifest=json.loads(args.manifest.read_text());manifest_hash=prepare.sha(args.manifest)
    core,fixed,baseline,fixedrecords=prepare.verify_manifest(manifest)
    args.root=Path(fixed['patched_root']);args.output_root=Path(manifest['output_root'])
    prior,refs=read_prior(args.prior_results,manifest_hash,manifest,args.output_root)
    selected=[j for j in manifest['jobs'] if j['stage']==args.stage]
    if args.only_id:
        if not set(args.only_id)<=set(j['id'] for j in selected):p.error('unknown selected job')
        selected=[j for j in selected if j['id'] in args.only_id]
    if not selected or any(j['id'] in prior for j in selected):p.error('empty selection or already accounted job; no implicit retry')
    args.run_dir.mkdir(parents=True);args.output_root.mkdir(parents=True,exist_ok=True)
    bindings={'ROOT':str(args.root),'OUT':str(args.output_root),'PYTHON':str(args.python),'BINARY':fixed['binary']['path']}
    dependencies={**baseline,**prior};deadline=started+args.global_seconds;pending={};results=[];cursor=0
    def commit(record):
        prepare.atomic_json(args.run_dir/(record['id']+'.receipt.json'),record)
        results.append(record)
        print(json.dumps({'finished':record['id'],'status':record['status']}),flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor<len(selected) or pending:
            while cursor<len(selected) and len(pending)<args.jobs:
                job=selected[cursor];cursor+=1
                if time.monotonic()+job['timeout_seconds']+20>deadline:
                    commit(dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='not_started_deadline',depends_on=job['depends_on'],output_files=[]))
                else:pending[pool.submit(prepare.runner.execute,job,args,bindings,dependencies)]=job
            if pending:
                done,_=wait(pending,return_when=FIRST_COMPLETED,timeout=1)
                for future in done:
                    job=pending.pop(future)
                    try:record=future.result()
                    except Exception as error:record=dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='runner_error',reason=repr(error),depends_on=job['depends_on'],output_files=[])
                    record['variant']=job['variant'];commit(record)
    unchanged=True;error=None
    try:
        prepare.verify_manifest(manifest)
        read_prior(args.prior_results,manifest_hash,manifest,args.output_root)
    except Exception as caught:unchanged=False;error=repr(caught)
    counts={s:sum(r['status']==s for r in results) for s in sorted({r['status'] for r in results})}
    receipt=dict(schema='triad-broad-long-followup-stage-v1',manifest_sha256=manifest_hash,stage=args.stage,
        source_file_sha256=manifest['source_file_sha256'],binary_sha256=fixed['binary']['sha256'],
        fixed_manifest_sha256=manifest['fixed_manifest']['sha256'],inputs_unchanged_at_end=unchanged,input_error=error,
        all_selected_jobs_accounted=len(results)==len(selected),python_invocation=str(args.python),prior_receipts=refs,
        elapsed_seconds=time.monotonic()-started,global_seconds_bound=args.global_seconds,concurrent_jobs=args.jobs,
        counts=counts,jobs=sorted(results,key=lambda r:r['id']))
    prepare.atomic_json(args.run_dir/'results.json',receipt)
    print(json.dumps({'receipt':str(args.run_dir/'results.json'),'counts':counts}))
    return int(not unchanged or len(results)!=len(selected))

if __name__=='__main__':raise SystemExit(main())
