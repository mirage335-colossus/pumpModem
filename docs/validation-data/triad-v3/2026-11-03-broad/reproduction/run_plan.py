#!/usr/bin/env python3
"""Execute one frozen diagnostic stage with owned, bounded child processes.

Failures are retained per job and do not cancel unrelated configurations.
No scientific retry or fallback is performed. Resource bounds concern this
runner's children, not other users or detached processes.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):h.update(part)
    return h.hexdigest()

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()

def check_job(job):
    body={k:v for k,v in job.items() if k!='definition_sha256'}
    if digest(body)!=job['definition_sha256']:raise ValueError('job definition hash mismatch: '+job['id'])
    if not job['id'] or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-' for c in job['id']):
        raise ValueError('unsafe job identity')
    if not 0<job['timeout_seconds']<=1200:raise ValueError('child time bound outside1..1200 seconds')

def translate(text,bindings):
    for key,value in bindings.items():text=text.replace('{'+key+'}',value)
    if '{' in text or '}' in text:raise ValueError('unresolved argv placeholder: '+text)
    return text

def verify_source(plan,root):
    bad=[p for p,d in plan['source_file_sha256'].items() if not (root/p).is_file() or sha(root/p)!=d]
    if bad:raise ValueError('frozen source differs: '+', '.join(bad))

def bounded_passed(value):
    return (value.get('outcome')=='completed' and value.get('exit_code')==0
        and value.get('child_exit_code')==0 and value.get('timed_out') is False
        and value.get('cancelled') is False
        and value.get('termination',{}).get('group_empty') is True
        and value.get('termination',{}).get('direct_child_reaped',True) is True)

def verify_build(plan,provenance,archive,binary_hash):
    value=json.loads(provenance.read_text())
    if value.get('all_required_checks_passed') is not True or value.get('binary_sha256')!=binary_hash:
        raise ValueError('build provenance has failed checks or mismatched binary')
    if not value.get('bounded_check_status') or any(not bounded_passed(v) for v in value['bounded_check_status'].values() if v is not None):
        raise ValueError('build provenance contains unsuccessful bounded checks')
    if sha(provenance)!=plan['build_provenance_sha256']:
        raise ValueError('build provenance differs from frozen plan')
    if sha(archive)!=plan['source_archive_sha256'] or value.get('source_archive_sha256')!=plan['source_archive_sha256']:
        raise ValueError('source archive binding differs')
    return value

def inventory(path,root):
    if not path.exists():return []
    paths=sorted(p for p in path.iterdir() if p.is_file()) if path.is_dir() else [path]
    return [{'path':str(p.relative_to(root)),'sha256':sha(p),'size_bytes':p.stat().st_size} for p in paths]

def read_prior(paths,plan_hash,definitions,output_root,expected_binary=None,expected_provenance=None,expected_source=None):
    records={};receipts=[];checked_files={}
    for path in paths:
        obj=json.loads(path.read_text())
        if obj.get('plan_sha256')!=plan_hash:raise ValueError('prior receipt binds a different plan: '+str(path))
        if not all(obj.get(key) is True for key in ('source_unchanged_at_end','binary_unchanged_at_end','all_selected_jobs_accounted')):
            raise ValueError('prior receipt has incomplete accounting or mutated source/binary: '+str(path))
        for key,expected in [('binary_sha256',expected_binary),('build_provenance_sha256',expected_provenance),('source_file_sha256',expected_source)]:
            if expected is not None and obj.get(key)!=expected:raise ValueError('prior '+key+' differs: '+str(path))
        receipts.append({'path':str(path),'sha256':sha(path)})
        for record in obj['jobs']:
            ident=record['id']
            if ident not in definitions or record['definition_sha256']!=definitions[ident]['definition_sha256']:
                raise ValueError('prior job definition mismatch: '+ident)
            if ident in records and digest(record)!=digest(records[ident]):raise ValueError('conflicting duplicate prior job: '+ident)
            for item in record.get('output_files',[]):
                p=output_root/item['path'];identity=(item['sha256'],item['size_bytes'])
                if str(p) in checked_files:
                    if checked_files[str(p)]!=identity:raise ValueError('conflicting prior file identity')
                else:
                    if not p.is_file() or p.stat().st_size!=item['size_bytes'] or sha(p)!=item['sha256']:
                        raise ValueError('prior output bytes differ: '+str(p))
                    checked_files[str(p)]=identity
            records[ident]=record
    return records,receipts

def configure_result(log):
    objects=[]
    for line in log.read_text(errors='replace').splitlines():
        try:value=json.loads(line)
        except ValueError:continue
        if isinstance(value,dict):objects.append(value)
    candidates=[value for value in objects if value.get('mode')=='configure' and isinstance(value.get('configuration'),dict)]
    if len(candidates)!=1:raise ValueError('configure output must contain exactly one configuration object')
    return candidates[0]

def execute(job,args,bindings,prior):
    started=time.monotonic();ident=job['id']
    record={'id':ident,'stage':job['stage'],'definition_sha256':job['definition_sha256'],
            'depends_on':job['depends_on'],'status':'not_started','output_files':[]}
    if job.get('predeclared_unsupported'):
        record.update(status='unsupported_receiver_grid',reason=job['predeclared_unsupported']);return record
    missing=[dep for dep in job['depends_on'] if dep not in prior or prior[dep]['status']!='completed']
    if missing:
        record.update(status='not_started_dependency',unavailable_dependencies=missing);return record
    record['input_bindings']={dep:{'definition_sha256':prior[dep]['definition_sha256'],
        'output_files':prior[dep].get('output_files',[])} for dep in job['depends_on']}
    argv=[translate(part,bindings) for part in job['argv']]
    record['argv']=argv;record['argv_sha256']=digest(argv)
    log=args.run_dir/(ident+'.log');status=args.run_dir/(ident+'.status.json')
    # Output directories/files are fresh; existing work is reused only through
    # an explicitly bound prior receipt, never overwritten or rerun implicitly.
    for field in ('workspace','report'):
        if field in job and (job['stage']=='export' or field=='report'):
            target=Path(translate(job[field],bindings))
            if target.exists():
                record.update(status='setup_error',reason='output already exists: '+str(target));return record
            target.parent.mkdir(parents=True,exist_ok=True)
    command=[str(args.python),str(args.root/'tools/triad_bounded_run.py'),
        '--cwd',str(args.root),'--log',str(log),'--status',str(status),
        '--timeout',str(job['timeout_seconds']),'--max-seconds',str(job['timeout_seconds']),
        '--term-grace','3','--',*argv]
    env=os.environ.copy();env.update(OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1',NUMEXPR_NUM_THREADS='1')
    # The inner runner owns and reaps the process group, including timeout
    # cleanup. The outer wrapper records its status without an unsafe second
    # kill path that could interrupt the owner during cleanup.
    result=subprocess.run(command,cwd=args.root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=False)
    record['wrapper_returncode']=result.returncode
    record['wrapper_output']=result.stdout[-2000:]
    record['elapsed_seconds']=time.monotonic()-started
    if status.is_file():
        record['bounded_status']=json.loads(status.read_text())
        record['status_sha256']=sha(status)
    record['log_sha256']=sha(log) if log.is_file() else None
    record['status']='completed' if result.returncode==0 and bounded_passed(record.get('bounded_status',{})) else 'execution_failed'
    if result.returncode==0 and record['status']!='completed':record['reason']='wrapper success lacks completed/reaped bounded child status'
    try:
        if job['stage']=='configure' and record['status']=='completed':
            native=configure_result(log);cfg=native['configuration'];record['native_configuration']=native
            record['capture_prior_feasible']=cfg['capture']['feasible_for_deterministic_prior']
            record['public_search']=cfg['public_search']
            if not record['capture_prior_feasible']:record['status']='capture_prior_infeasible'
        elif job['stage']=='export':
            directory=Path(translate(job['workspace'],bindings))
            record['output_files']=inventory(directory,args.output_root)
            if record['status']=='completed':
                manifest=json.loads((directory/'manifest.json').read_text())
                record['export_metadata']={k:manifest.get(k) for k in ('schema','format','sample_count','sample_rate','block_samples','block_count','dimension','control')}
                if not (directory/'observation.c128').is_file() or not (directory/'received_energy.f64').is_file():
                    raise ValueError('actual-noisy export did not retain observation and received energy')
        elif 'report' in job:
            report=Path(translate(job['report'],bindings))
            if report.is_file():
                record['output_files']=inventory(report,args.output_root)
                score=report.with_suffix('.scores.npz')
                if score.is_file():record['output_files']+=inventory(score,args.output_root)
                data=json.loads(report.read_text())
                record['receiver_failed_paths']=data.get('failed_physical_path_records')
                if result.returncode==0 and data.get('failed_physical_path_records')!=0:
                    raise ValueError('successful receiver exit disagrees with failure count')
            elif result.returncode==0:raise ValueError('successful receiver produced no report')
    except (OSError,ValueError,KeyError) as error:
        record.update(status='invalid_output',reason=str(error))
    return record

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,required=True);p.add_argument('--stage',required=True,choices=('configure','export','actual','conditional','benchmark'))
    p.add_argument('--root',type=Path,required=True);p.add_argument('--output-root',type=Path,required=True)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--python',type=Path,default=Path(sys.executable))
    p.add_argument('--build-provenance',type=Path,required=True);p.add_argument('--source-archive',type=Path,required=True)
    p.add_argument('--run-dir',type=Path,required=True);p.add_argument('--prior-results',action='append',type=Path,default=[])
    p.add_argument('--only-id',action='append',default=[]);p.add_argument('--jobs',type=int,default=2)
    p.add_argument('--global-seconds',type=float,default=7200)
    args=p.parse_args(argv)
    if not 1<=args.jobs<=12 or not 0<args.global_seconds<=25200:p.error('require1..12jobs and <=7hour stage bound')
    for name in ('root','output_root','binary','run_dir','plan'):
        setattr(args,name,getattr(args,name).resolve())
    # Python's invocation path selects its virtual environment; resolving its
    # executable symlink silently switches to the system package environment.
    args.python=args.python.absolute()
    if not args.binary.is_file() or not args.python.is_file():p.error('binary/interpreter must exist')
    if args.run_dir.exists():p.error('run-dir must be fresh')
    plan=json.loads(args.plan.read_text());plan_hash=sha(args.plan)
    definitions={job['id']:job for job in plan['jobs']}
    if len(definitions)!=len(plan['jobs']):p.error('duplicate job IDs')
    for job in definitions.values():check_job(job)
    selected=[j for j in plan['jobs'] if j['stage']==args.stage]
    if args.only_id:
        if not set(args.only_id)<=set(j['id'] for j in selected):p.error('unknown job ID for selected stage')
        selected=[j for j in selected if j['id'] in args.only_id]
    if not selected:p.error('empty stage selection')
    verify_source(plan,args.root)
    binary_hash=sha(args.binary)
    build=verify_build(plan,args.build_provenance,args.source_archive,binary_hash)
    prior,receipts=read_prior(args.prior_results,plan_hash,definitions,args.output_root,
        binary_hash,plan['build_provenance_sha256'],plan['source_file_sha256'])
    if any(j['id'] in prior for j in selected):p.error('selected job already has a prior receipt; no implicit rerun')
    args.run_dir.mkdir(parents=True);args.output_root.mkdir(parents=True,exist_ok=True)
    bindings={'ROOT':str(args.root),'OUT':str(args.output_root),'BINARY':str(args.binary),'PYTHON':str(args.python)}
    begun=time.monotonic();deadline=begun+args.global_seconds;results=[];cursor=0;pending={}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor<len(selected) or pending:
            while cursor<len(selected) and len(pending)<args.jobs:
                job=selected[cursor];cursor+=1
                if time.monotonic()+job['timeout_seconds']+10>deadline:
                    results.append({'id':job['id'],'stage':job['stage'],'definition_sha256':job['definition_sha256'],
                        'status':'not_started_deadline','depends_on':job['depends_on'],'output_files':[]})
                else:pending[pool.submit(execute,job,args,bindings,prior)]=job
            if pending:
                done,_=wait(pending,return_when=FIRST_COMPLETED,timeout=1)
                for future in done:
                    job=pending.pop(future)
                    try:record=future.result()
                    except Exception as error:
                        record={'id':job['id'],'stage':job['stage'],'definition_sha256':job['definition_sha256'],
                            'status':'runner_error','reason':repr(error),'depends_on':job['depends_on'],'output_files':[]}
                    results.append(record)
                    print(json.dumps({'finished':job['id'],'status':record['status'],'elapsed_seconds':record.get('elapsed_seconds')}),flush=True)
    source_valid=True;source_error=None
    try:verify_source(plan,args.root)
    except ValueError as error:source_valid=False;source_error=str(error)
    binary_valid=sha(args.binary)==binary_hash
    counts={status:sum(r['status']==status for r in results) for status in sorted(set(r['status'] for r in results))}
    out={'schema':'triad-broad-stage-receipt-v1','plan_sha256':plan_hash,'runner_sha256':sha(__file__),
        'stage':args.stage,'root':str(args.root),'output_root':str(args.output_root),
        'binary_sha256':binary_hash,'source_file_sha256':plan['source_file_sha256'],
        'build_provenance_sha256':sha(args.build_provenance),'source_archive_sha256':build['source_archive_sha256'],
        'source_unchanged_at_end':source_valid,'source_error':source_error,'binary_unchanged_at_end':binary_valid,
        'prior_receipts':receipts,'elapsed_seconds':time.monotonic()-begun,'global_seconds_bound':args.global_seconds,
        'concurrent_jobs':args.jobs,'counts':counts,'all_selected_jobs_accounted':len(results)==len(selected),
        'jobs':sorted(results,key=lambda r:r['id'])}
    (args.run_dir/'results.json').write_text(json.dumps(out,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'receipt':str(args.run_dir/'results.json'),'counts':counts,'elapsed_seconds':out['elapsed_seconds']}))
    return int(not(source_valid and binary_valid and out['all_selected_jobs_accounted']))

if __name__=='__main__':raise SystemExit(main())
